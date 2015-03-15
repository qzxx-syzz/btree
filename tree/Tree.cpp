/* Information about this file */

#include "Tree.h"
using namespace std;

//tree's operations should be atom(if read nodes)
//sum the request and send to Storage at last
//ensure that all nodes operated are in memory
int request = 0;

Tree::Tree()
{
	height = 0;
	mode = "";
	root = NULL;
	leaves = NULL;
	TSM = new Storage;
	storepath = "";
	filename = "";
}

Tree::Tree(const string& _storepath, const string& _filename, const char* _mode)
{
	storepath = _storepath;
	filename = _filename;
	this->height = 0;
	this->mode = string(_mode);
	string filepath = this->getFilePath();
	TSM = new Storage(filepath, this->mode, &this->height);
	if(this->mode == "open")
		this->TSM->preRead(this->root, this->leaves);
	else
		this->root = NULL;
}

string
Tree::getFilePath()
{
	return storepath+"/"+filename;
}

void
Tree::CopyToTransfer(const Bstr* _bstr)
{
	unsigned length = _bstr->getLen();
	if(length > this->transfer.getLen())
	{
		transfer.release();
		transfer.setLen(length);
		transfer.setStr((char*)malloc(length+1));
	}
	memcpy(this->transfer.getStr(), _bstr->getStr(), length);
}

unsigned 
Tree::getHeight() const
{
	return this->height;
}

void
Tree::setHeight(unsigned _h)
{
	this->height = _h;
}

Node*
Tree::getRoot() const
{
	return this->root;
}

void
Tree::prepare(Node* _np) const
{
	bool flag = _np->inMem();
	if(!flag)
		this->TSM->readNode(_np, &request);	//readNode deal with request
}

/*
bool
Tree::search(unsigned _len1, const char* _str1, unsigned& _len2, const char*& _str2) const
{
}
*/

bool
Tree::search(const Bstr* _key, const Bstr*& _value)
{
	request = 0;
	int store;	
	Node* ret = this->find(_key, &store, false);
	if(ret == NULL || store == -1)	//tree is empty or not found
		return false;	
	this->CopyToTransfer(ret->getValue(store));	//not sum to request
	_value = &transfer;
	this->TSM->request(request);
	return true;
}

bool
Tree::insert(Bstr* _key, Bstr* _value)
{
	request = 0;
	Node* ret;
	if(this->root == NULL)	//tree is empty
	{
		leaves = root = new LeafNode;
		request += LEAF_SIZE;
		this->height = 1;
		root->setHeight(1);	//add to heap later
	}
	//this->prepare(this->root); //root must be in-mem
	if(root->getNum() == Node::MAX_KEY_NUM)
	{
		Node* father = new IntlNode;
		request += INTL_SIZE;
		father->addChild(root, 0);
		ret = root->split(father, 0);
		if(ret->isLeaf())
			request += LEAF_SIZE;
		else
			request += INTL_SIZE;
		this->height++;		//height rises only when root splits
		//WARN: height area in Node: 4 bit!
		father->setHeight(this->height);	//add to heap later
		this->TSM->updateHeap(ret, ret->getRank(), false);
		this->root = father;
	}
	Node* p = this->root;
	Node* q;
	int i, j;
	Bstr bstr = *_key;
	while(!p->isLeaf())
	{
		j = p->getNum();
		for(i = 0; i < j; ++i)
			if(bstr < *(p->getKey(i)))
				break;
		q = p->getChild(i);
		this->prepare(q);
		if(q->getNum() == Node::MAX_KEY_NUM)
		{
			ret = q->split(p, i);
			if(ret->isLeaf())
				request += LEAF_SIZE;
			else
				request += INTL_SIZE;
			//BETTER: in loop may update multiple times
			this->TSM->updateHeap(ret, ret->getRank(), false);
			this->TSM->updateHeap(q, q->getRank(), true);
			this->TSM->updateHeap(p, p->getRank(), true);
			if(bstr < *(p->getKey(i)))
				p = q;
			else 
				p = ret;
		}
		else
		{
			p->setDirty();
			this->TSM->updateHeap(p, p->getRank(), true);
			p = q;
		}
	}
	j = p->getNum();
	for(i = 0; i < j; ++i)
		if(bstr < *(p->getKey(i)))
			break;
	//insert existing key is ok, but not inserted in
	//however, the tree-shape may change due to possible split in former code
	bool ifexist = false;
	if(i > 0 && bstr == *(p->getKey(i-1)))
		ifexist = true;
	else
	{
		p->addKey(_key, i);
		p->addValue(_value, i);
		p->addNum();
		request += (_key->getLen() + _value->getLen());
		p->setDirty();
		this->TSM->updateHeap(p, p->getRank(), true);
		//_key->clear();
		_value->clear();
	}
	this->TSM->request(request);
	bstr.clear();		//NOTICE: must be cleared!
	return !ifexist;		//QUERY(which case:return false)
}

/*
bool
Tree::insert(unsigned _len1, const char* _str1, unsigned _len2, const char* _str2)
{
}
*/

bool
Tree::modify(const Bstr* _key, Bstr* _value)
{					
	request = 0;
	int store;
	Node* ret = this->find(_key, &store, true);
	if(ret == NULL || store == -1)	//tree is empty or not found
		return false;
	unsigned len = ret->getValue(store)->getLen();
	ret->setValue(_value, store);
	request += (_value->getLen()-len);
	_value->clear();
	ret->setDirty();
	this->TSM->request(request);
	return true;
}

/*
bool
Tree::modify(unsigned _len1, const char* _str1, unsigned _len2, const char* _str2)
{
}
*/

/* this function is useful for search and modify */
Node*
Tree::find(const Bstr* _key, int* _store, bool ifmodify) const
{											//to assign value for this->bstr, function shouldn't be const!
	if(this->root == NULL)
		return NULL;						//Tree Is Empty
	Node* p = root;
	int i, j;
	Bstr bstr = *_key;					//local Bstr: multiple delete
	while(!p->isLeaf())
	{
		if(ifmodify)
			p->setDirty();
		j = p->getNum();
		for(i = 0; i < j; ++i)				//BETTER(Binary-Search)
			if(bstr < *(p->getKey(i)))
				break;
	 	p = p->getChild(i);
		this->prepare(p);
	}
	j = p->getNum();
	for(i = 0; i < j; ++i)
		if(bstr == *(p->getKey(i)))
			break;
	if(i == j)
		*_store = -1;	//Not Found
	else	
		*_store = i;
	bstr.clear();
	return p;
}

/*
Node*
Tree::find(unsigned _len, const char* _str, int* store) const
{
}
*/

bool	//BETTER: if not found, the road are also dirty! find first?
Tree::remove(const Bstr* _key)
{
	request = 0;
	Node* ret;
	if(this->root == NULL)	//tree is empty
		return false;
	Node* p = this->root;
	Node* q;
	int i, j;
	Bstr bstr = *_key;
	while(!p->isLeaf())
	{
		j = p->getNum();
		for(i = 0; i < j; ++i)
			if(bstr < *(p->getKey(i)))
				break;
		q = p->getChild(i);
		this->prepare(q);
		if(q->getNum() < Node::MIN_CHILD_NUM)	//==MIN_KEY_NUM
		{
			if(i > 0)
				this->prepare(p->getChild(i-1));
			if(i < j)
				this->prepare(p->getChild(i+1));
			ret = q->coalesce(p, i);
			if(ret != NULL)
				this->TSM->updateHeap(ret, 0, true);//non-sense node
			this->TSM->updateHeap(q, q->getRank(), true);
			if(q->isLeaf() && q->getPrev() == NULL)
				this->leaves = q;
			if(p->getNum() == 0)		//root shrinks
			{
				//this->leaves = q;
				this->root = q;
				this->TSM->updateHeap(p, 0, true);	//instead of delete p				
				this->height--;
			}
		}
		else 
			p->setDirty();
		this->TSM->updateHeap(p, p->getRank(), true);
		p = q;
	}
	bool flag = false;
	j = p->getNum();		//LeafNode(maybe root)
	for(i = 0; i < j; ++i)
		if(bstr == *(p->getKey(i)))
		{
			request -= p->getKey(i)->getLen();
			request -= p->getValue(i)->getLen();
			p->subKey(i, true);		//to release
			p->subValue(i, true);	//to release
			p->subNum();
			if(p->getNum() == 0)	//root leaf 0 key
			{
				this->root = NULL;
				this->leaves = NULL;
				this->height = 0;
				this->TSM->updateHeap(p, 0, true);	//instead of delete p
			}
			p->setDirty();
			flag = true;
			break;
		}
	this->TSM->request(request);
	bstr.clear();
	return flag;		//i == j, not found		
}

/*
bool
Tree::remove(unsigned _len, const char* _str)
{
}
*/

const Bstr*
Tree::getRangeValue()
{
	return this->VALUES.read();
}

bool	//TODO: not exist, not in order, one-edge-case
Tree::range_query(const Bstr* _key1, const Bstr* _key2)
{			
	request = 0;
	this->VALUES.open();
	/* find and write value */
	int store1, store2;
	Node* p1 = this->find(_key1, &store1, false);
	this->TSM->request(request);
	request = 0;
	Node* p2 = this->find(_key2, &store2, false);
	this->TSM->request(request);
	Node* p = p1;
	unsigned i, l, r;
	while(1)
	{
		request = 0;
		this->prepare(p);
		if(p == p1)
			l = store1;
		else
			l = 0;
		if(p == p2)
			r = store2 + 1;
		else
			r = p->getNum();
		for(i = l; i < r; ++i)
			this->VALUES.write(p->getValue(i));
		this->TSM->request(request);
		if(p != p2)
			p = p->getNext();
		else
			break;
	}
	this->VALUES.reset();
	return true;
}

bool 
Tree::save()	//save the whole tree to disk
{
	if(TSM->writeTree(this->root))
		return true;
	else
		return false;
}

void
Tree::release(Node* _np) const
{
	if(_np == NULL)	return;
	if(_np->isLeaf())
	{
		delete _np;
		return;
	}
	int cnt = _np->getNum();
	for(; cnt >= 0; --cnt)
		release(_np->getChild(cnt));
	delete _np;
}

Tree::~Tree()
{
	//delete VALUES;
	delete TSM;
	//recursively delete each Node
	release(root);
}

void
Tree::print(string s)
{
	Util::showtime();
	fputs("Class Tree\n", Util::logsfp);
	fputs("Message: ", Util::logsfp);
	fputs(s.c_str(), Util::logsfp);
	fputs("\n", Util::logsfp);
	fprintf(Util::logsfp, "Height: %d\n", this->height);
	if(s == "tree" || s == "TREE")
	{
		if(this->root == NULL)
		{
			fputs("Null Tree\n", Util::logsfp);
			return;
		}
		Node** ns = new Node*[this->height];
		int* ni = new int[this->height];
		Node* np;
		int i, pos = 0;
		ns[pos] = this->root;
		ni[pos] = this->root->getNum();
		pos++;
		while(pos > 0)
		{
			np = ns[pos-1];
			i = ni[pos-1];
			this->prepare(np);
			if(np->isLeaf() || i < 0)	//LeafNode or ready IntlNode
			{							//child-num ranges: 0~num
				if(s == "tree")
					np->print("node");
				else
					np->print("NODE");	//print full node-information
				pos--;
				continue;
			}
			else
			{
				ns[pos] = np->getChild(i);
				ni[pos-1]--;
				ni[pos] = ns[pos]->getNum();
				pos++;
			}
		}
		delete[] ns;
		delete[] ni;
	}
	else if(s == "LEAVES" || s == "leaves")
	{
		Node* np;
		for(np = this->leaves; np != NULL; np = np->getNext())
		{
			this->prepare(np);
			if(s == "leaves")
				np->print("node");
			else
				np->print("NODE");
		}
	}
	else if(s == "check tree")
	{
		//check the tree, if satisfy B+ definition
		//TODO	
	}
	else;
}
