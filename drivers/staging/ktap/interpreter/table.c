/*
 * table.c - ktap table data structure manipulation function
 *
 * This file is part of ktap by Jovi Zhangwei.
 *
 * Copyright (C) 2012-2013 Jovi Zhangwei <jovi.zhangwei@gmail.com>.
 *
 * Copyright (C) 1994-2013 Lua.org, PUC-Rio.
 *  - The part of code in this file is copied from lua initially.
 *  - lua's MIT license is compatible with GPL.
 *
 * ktap is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * ktap is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifdef __KERNEL__
#include "../include/ktap.h"
#include <linux/spinlock.h>
#include <linux/kallsyms.h>
#include <linux/sort.h>
#else
#include "../include/ktap_types.h"

static inline void sort(void *base, size_t num, size_t size,
			int (*cmp_func)(const void *, const void *),
			void (*swap_func)(void *, void *, int size))
{}
#endif


#ifdef __KERNEL__
#define kp_table_lock_init(t)	\
	do {	\
		t->lock = (arch_spinlock_t)__ARCH_SPIN_LOCK_UNLOCKED;	\
	} while (0)
#define kp_table_lock(t)	\
	do {	\
		local_irq_save(flags);	\
		arch_spin_lock(&t->lock);	\
	} while (0)
#define kp_table_unlock(t)	\
	do {	\
		arch_spin_unlock(&t->lock);	\
		local_irq_restore(flags);	\
	} while (0)

#else
#define kp_table_lock_init(t)
#define kp_table_lock(t)
#define kp_table_unlock(t)
#endif

#define MAXBITS         30
#define MAXASIZE        (1 << MAXBITS)


#define NILCONSTANT     {NULL}, KTAP_TNIL
const struct ktap_value ktap_nilobjectv = {NILCONSTANT};
#define ktap_nilobject	(&ktap_nilobjectv)

static const ktap_tnode dummynode_ = {
	{NILCONSTANT}, /* value */
	{{NILCONSTANT, NULL}}, /* key */
};

#define gnode(t,i)      (&(t)->node[i])
#define gkey(n)         (&(n)->i_key.tvk)
#define gval(n)         (&(n)->i_val)
#define gnext(n)        ((n)->i_key.nk.next)

#define twoto(x)        (1<<(x))
#define sizenode(t)	(twoto((t)->lsizenode))

#define hashpow2(t,n)           (gnode(t, lmod((n), sizenode(t))))

#define hashmod(t,n)		(gnode(t, ((n) % ((sizenode(t)-1)|1))))

#define hashstr(t,str)          hashpow2(t, (str)->tsv.hash)
#define hashboolean(t,p)        hashpow2(t, p)
#define hashnum(t, n)		hashmod(t, (unsigned int)n)
#define hashpointer(t,p)	hashmod(t, (unsigned long)(p))

#define dummynode	(&dummynode_)
#define isdummy(n)	((n) == dummynode)

static void table_setint(ktap_state *ks, ktap_table *t, int key, ktap_value *v);
static ktap_value *table_set(ktap_state *ks, ktap_table *t,
			     const ktap_value *key);
static void setnodevector(ktap_state *ks, ktap_table *t, int size);

static int ceillog2(unsigned int x)
{
	static const u8 log_2[256] = {
	0,1,2,2,3,3,3,3,4,4,4,4,4,4,4,4,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
	6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
	7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
	7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
	8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
	8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
	8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
	8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8
	};

	int l = 0;

	x--;
	while (x >= 256) { l += 8; x >>= 8; }
	return l + log_2[x];
}


ktap_table *kp_table_new(ktap_state *ks)
{
	ktap_table *t = &kp_newobject(ks, KTAP_TTABLE, sizeof(ktap_table),
				      NULL)->h;
	t->flags = (u8)(~0);
	t->array = NULL;
	t->sizearray = 0;
	t->node = (ktap_tnode *)dummynode;
	t->gclist = NULL;
	setnodevector(ks, t, 0);

	kp_table_lock_init(t);
	return t;
}

static const ktap_value *table_getint(ktap_table *t, int key)
{
	ktap_tnode *n;

	if ((unsigned int)(key - 1) < (unsigned int)t->sizearray)
		return &t->array[key - 1];

	n = hashnum(t, key);
	do {
		if (ttisnumber(gkey(n)) && nvalue(gkey(n)) == key)
			return gval(n);
		else
			n = gnext(n);
	} while (n);

	return ktap_nilobject;
}

const ktap_value *kp_table_getint(ktap_table *t, int key)
{
	const ktap_value *val;
	unsigned long __maybe_unused flags;

	kp_table_lock(t);
	val = table_getint(t, key);
	kp_table_unlock(t);

	return val;
}

static ktap_tnode *mainposition (const ktap_table *t, const ktap_value *key)
{
	switch (ttype(key)) {
	case KTAP_TNUMBER:
		return hashnum(t, nvalue(key));
	case KTAP_TLNGSTR: {
		ktap_string *s = rawtsvalue(key);
		if (s->tsv.extra == 0) {  /* no hash? */
			s->tsv.hash = kp_string_hash(getstr(s), s->tsv.len,
						     s->tsv.hash);
			s->tsv.extra = 1;  /* now it has its hash */
		}
		return hashstr(t, rawtsvalue(key));
		}
	case KTAP_TSHRSTR:
		return hashstr(t, rawtsvalue(key));
	case KTAP_TBOOLEAN:
		return hashboolean(t, bvalue(key));
	case KTAP_TLIGHTUSERDATA:
		return hashpointer(t, pvalue(key));
	case KTAP_TLCF:
		return hashpointer(t, fvalue(key));
	case KTAP_TBTRACE:
		/* use first entry as hash key, cannot use gcvalue as key */
		return hashpointer(t, btvalue(key)->entries[0]);
	default:
		return hashpointer(t, gcvalue(key));
	}
}

static int arrayindex(const ktap_value *key)
{
	if (ttisnumber(key)) {
		ktap_number n = nvalue(key);
		int k = (int)n;
		if ((ktap_number)k == n)
			return k;
	}

	/* `key' did not match some condition */
	return -1;
}

/*
 * returns the index of a `key' for table traversals. First goes all
 * elements in the array part, then elements in the hash part. The
 * beginning of a traversal is signaled by -1.
 */
static int findindex(ktap_state *ks, ktap_table *t, StkId key)
{
	int i;

	if (ttisnil(key))
		return -1;  /* first iteration */

	i = arrayindex(key);
	if (i > 0 && i <= t->sizearray)  /* is `key' inside array part? */
		return i - 1;  /* yes; that's the index (corrected to C) */
	else {
		ktap_tnode *n = mainposition(t, key);
		for (;;) {  /* check whether `key' is somewhere in the chain */
			/* key may be dead already, but it is ok to use it in `next' */
			if (kp_equalobjv(ks, gkey(n), key)) {
				i = n - gnode(t, 0);  /* key index in hash table */
				/* hash elements are numbered after array ones */
				return i + t->sizearray;
			} else
				n = gnext(n);

			if (n == NULL)
				/* key not found */
				kp_error(ks, "invalid table key to next");
		}
	}
}

int kp_table_next(ktap_state *ks, ktap_table *t, StkId key)
{
	unsigned long __maybe_unused flags;
	int i;

	kp_table_lock(t);

	i = findindex(ks, t, key);  /* find original element */

	for (i++; i < t->sizearray; i++) {  /* try first array part */
	        if (!ttisnil(&t->array[i])) {  /* a non-nil value? */
			setnvalue(key, i+1);
			setobj(key+1, &t->array[i]);
			kp_table_unlock(t);
			return 1;
		}
	}

	for (i -= t->sizearray; i < sizenode(t); i++) {  /* then hash part */
		if (!ttisnil(gval(gnode(t, i)))) {  /* a non-nil value? */
			setobj(key, gkey(gnode(t, i)));
			setobj(key+1, gval(gnode(t, i)));
			kp_table_unlock(t);
			return 1;
		}
	}

	kp_table_unlock(t);
	return 0;  /* no more elements */
}



static int computesizes (int nums[], int *narray)
{
	int i;
	int twotoi;  /* 2^i */
	int a = 0;  /* number of elements smaller than 2^i */
	int na = 0;  /* number of elements to go to array part */
	int n = 0;  /* optimal size for array part */

	for (i = 0, twotoi = 1; twotoi/2 < *narray; i++, twotoi *= 2) {
		if (nums[i] > 0) {
			a += nums[i];
			/* more than half elements present? */
			if (a > twotoi/2) {
				/* optimal size (till now) */
				n = twotoi;
				/* 
				 * all elements smaller than n will go to
				 * array part
				 */
				na = a;
			}
		}
		if (a == *narray)
			break;  /* all elements already counted */
	}
	*narray = n;
	return na;
}


static int countint(const ktap_value *key, int *nums)
{
	int k = arrayindex(key);

	/* is `key' an appropriate array index? */
	if (0 < k && k <= MAXASIZE) {
		nums[ceillog2(k)]++;  /* count as such */
		return 1;
	} else
		return 0;
}


static int numusearray(const ktap_table *t, int *nums)
{
	int lg;
	int ttlg;  /* 2^lg */
	int ause = 0;  /* summation of `nums' */
	int i = 1;  /* count to traverse all array keys */

	/* for each slice */
	for (lg=0, ttlg=1; lg <= MAXBITS; lg++, ttlg *= 2) {
		int lc = 0;  /* counter */
		int lim = ttlg;

		if (lim > t->sizearray) {
			lim = t->sizearray;  /* adjust upper limit */
			if (i > lim)
				break;  /* no more elements to count */
		}

		/* count elements in range (2^(lg-1), 2^lg] */
		for (; i <= lim; i++) {
			if (!ttisnil(&t->array[i-1]))
				lc++;
		}
		nums[lg] += lc;
		ause += lc;
	}
	return ause;
}

static int numusehash(const ktap_table *t, int *nums, int *pnasize)
{
	int totaluse = 0;  /* total number of elements */
	int ause = 0;  /* summation of `nums' */
	int i = sizenode(t);

	while (i--) {
		ktap_tnode *n = &t->node[i];
		if (!isnil(gval(n))) {
			ause += countint(gkey(n), nums);
			totaluse++;
		}
	}

	*pnasize += ause;
	return totaluse;
}


static void setarrayvector(ktap_state *ks, ktap_table *t, int size)
{
	int i;

	kp_realloc(ks, t->array, t->sizearray, size, ktap_value);
	for (i = t->sizearray; i < size; i++)
		setnilvalue(&t->array[i]);

	t->sizearray = size;
}

static void setnodevector(ktap_state *ks, ktap_table *t, int size)
{
	int lsize;

	if (size == 0) {  /* no elements to hash part? */
		t->node = (ktap_tnode *)dummynode;  /* use common `dummynode' */
		lsize = 0;
	} else {
		int i;
		lsize = ceillog2(size);
		if (lsize > MAXBITS) {
			kp_error(ks, "table overflow\n");
			return;
		}

		size = twoto(lsize);
		t->node = kp_malloc(ks, size * sizeof(ktap_tnode));
		for (i = 0; i < size; i++) {
			ktap_tnode *n = gnode(t, i);
			gnext(n) = NULL;
			setnilvalue(gkey(n));
			setnilvalue(gval(n));
		}
	}

	t->lsizenode = (u8)lsize;
	t->lastfree = gnode(t, size);  /* all positions are free */
}

static void table_resize(ktap_state *ks, ktap_table *t, int nasize, int nhsize)
{
	int i;
	int oldasize = t->sizearray;
	int oldhsize = t->lsizenode;
	ktap_tnode *nold = t->node;  /* save old hash ... */

#ifdef __KERNEL__
	kp_verbose_printf(ks, "table resize, nasize: %d, nhsize: %d\n",
				nasize, nhsize);
#endif

	if (nasize > oldasize)  /* array part must grow? */
		setarrayvector(ks, t, nasize);

	/* create new hash part with appropriate size */
	setnodevector(ks, t, nhsize);

	if (nasize < oldasize) {  /* array part must shrink? */
		t->sizearray = nasize;
		/* re-insert elements from vanishing slice */
		for (i=nasize; i<oldasize; i++) {
			if (!ttisnil(&t->array[i]))
				table_setint(ks, t, i + 1, &t->array[i]);
		}

		/* shrink array */
		kp_realloc(ks, t->array, oldasize, nasize, ktap_value);
	}

	/* re-insert elements from hash part */
	for (i = twoto(oldhsize) - 1; i >= 0; i--) {
		ktap_tnode *old = nold+i;
		if (!ttisnil(gval(old))) {
			/*
			 * doesn't need barrier/invalidate cache, as entry was
			 * already present in the table
			 */
			setobj(table_set(ks, t, gkey(old)), gval(old));
		}
	}

	if (!isdummy(nold))
		kp_free(ks, nold); /* free old array */
}

void kp_table_resize(ktap_state *ks, ktap_table *t, int nasize, int nhsize)
{
	unsigned long __maybe_unused flags;

	kp_table_lock(t);
	table_resize(ks, t, nasize, nhsize);
	kp_table_unlock(t);
}

void kp_table_resizearray(ktap_state *ks, ktap_table *t, int nasize)
{
	unsigned long __maybe_unused flags;
	int nsize;

	kp_table_lock(t);

	nsize = isdummy(t->node) ? 0 : sizenode(t);
	table_resize(ks, t, nasize, nsize);

	kp_table_unlock(t);
}

static void rehash(ktap_state *ks, ktap_table *t, const ktap_value *ek)
{
	int nasize, na;
	/* nums[i] = number of keys with 2^(i-1) < k <= 2^i */
	int nums[MAXBITS+1];
	int i;
	int totaluse;

	for (i = 0; i <= MAXBITS; i++)
		nums[i] = 0;  /* reset counts */

	nasize = numusearray(t, nums);  /* count keys in array part */
	totaluse = nasize;  /* all those keys are integer keys */
	totaluse += numusehash(t, nums, &nasize);  /* count keys in hash part */
	/* count extra key */
	nasize += countint(ek, nums);
	totaluse++;
	/* compute new size for array part */
	na = computesizes(nums, &nasize);
	/* resize the table to new computed sizes */
	table_resize(ks, t, nasize, totaluse - na);
}


static ktap_tnode *getfreepos(ktap_table *t)
{
	while (t->lastfree > t->node) {
		t->lastfree--;
		if (isnil(gkey(t->lastfree)))
			return t->lastfree;
	}
	return NULL;  /* could not find a free place */
}


static ktap_value *table_newkey(ktap_state *ks, ktap_table *t,
				const ktap_value *key)
{
	ktap_tnode *mp;
	ktap_value newkey;

	mp = mainposition(t, key);
	if (!isnil(gval(mp)) || isdummy(mp)) {  /* main position is taken? */
		ktap_tnode *othern;
		ktap_tnode *n = getfreepos(t);  /* get a free place */
		if (n == NULL) {  /* cannot find a free place? */
			rehash(ks, t, key);  /* grow table */
			/* whatever called 'newkey' take care of TM cache and GC barrier */
			return table_set(ks, t, key);  /* insert key into grown table */
		}

		othern = mainposition(t, gkey(mp));
		if (othern != mp) {  /* is colliding node out of its main position? */
			/* yes; move colliding node into free position */
			while (gnext(othern) != mp)
				othern = gnext(othern);  /* find previous */
			gnext(othern) = n;  /* redo the chain with `n' in place of `mp' */
			*n = *mp;  /* copy colliding node into free pos. (mp->next also goes) */
			gnext(mp) = NULL;  /* now `mp' is free */
			setnilvalue(gval(mp));
		} else {  /* colliding node is in its own main position */
			/* new node will go into free position */
			gnext(n) = gnext(mp);  /* chain new position */
			gnext(mp) = n;
			mp = n;
		}
	}

	/* special handling for cloneable object, maily for btrace object */
	if (ttisclone(key))
		kp_objclone(ks, key, &newkey, &t->gclist);
	else
		newkey = *key;

	setobj(gkey(mp), &newkey);
	return gval(mp);
}


/*
 * search function for short strings
 */
static const ktap_value *table_getstr(ktap_table *t, ktap_string *key)
{
	ktap_tnode *n = hashstr(t, key);

	do {  /* check whether `key' is somewhere in the chain */
		if (ttisshrstring(gkey(n)) && eqshrstr(rawtsvalue(gkey(n)),
								key))
			return gval(n);  /* that's it */
		else
			n = gnext(n);
	} while (n);

	return ktap_nilobject;
}


/*
 * main search function
 */
static const ktap_value *table_get(ktap_table *t, const ktap_value *key)
{
	switch (ttype(key)) {
	case KTAP_TNIL:
		return ktap_nilobject;
	case KTAP_TSHRSTR:
		return table_getstr(t, rawtsvalue(key));
	case KTAP_TNUMBER: {
		ktap_number n = nvalue(key);
		int k = (int)n;
		if ((ktap_number)k == nvalue(key)) /* index is int? */
			return table_getint(t, k);  /* use specialized version */
		/* else go through */
	}
	default: {
		ktap_tnode *n = mainposition(t, key);
		do {  /* check whether `key' is somewhere in the chain */
			if (rawequalobj(gkey(n), key))
				return gval(n);  /* that's it */
			else
				n = gnext(n);
		} while (n);

		return ktap_nilobject;
	}
	}
}

const ktap_value *kp_table_get(ktap_table *t, const ktap_value *key)
{
	const ktap_value *val;
	unsigned long __maybe_unused flags;

	kp_table_lock(t);
	val = table_get(t, key);
	kp_table_unlock(t);

	return val;
}

static ktap_value *table_set(ktap_state *ks, ktap_table *t,
			     const ktap_value *key)
{
	const ktap_value *p = table_get(t, key);

	if (p != ktap_nilobject)
		return (ktap_value *)p;
	else
		return table_newkey(ks, t, key);
}

void kp_table_setvalue(ktap_state *ks, ktap_table *t,
		       const ktap_value *key, ktap_value *val)
{
	unsigned long __maybe_unused flags;

	if (isnil(key)) {
		kp_printf(ks, "table index is nil\n");
		kp_exit(ks);
		return;
	}

	kp_table_lock(t);
	setobj(table_set(ks, t, key), val);
	kp_table_unlock(t);
}

static void table_setint(ktap_state *ks, ktap_table *t, int key, ktap_value *v)
{
	const ktap_value *p;
	ktap_value *cell;

	p = table_getint(t, key);

	if (p != ktap_nilobject)
		cell = (ktap_value *)p;
	else {
		ktap_value k;
		setnvalue(&k, key);
		cell = table_newkey(ks, t, &k);
	}

	setobj(cell, v);
}

void kp_table_setint(ktap_state *ks, ktap_table *t, int key, ktap_value *val)
{
	unsigned long __maybe_unused flags;

	kp_table_lock(t);
	table_setint(ks, t, key, val);
	kp_table_unlock(t);
}

void kp_table_atomic_inc(ktap_state *ks, ktap_table *t, ktap_value *key, int n)
{
	unsigned long __maybe_unused flags;
	ktap_value *v;

	if (isnil(key)) {
		kp_printf(ks, "table index is nil\n");
		kp_exit(ks);
		return;
	}

	kp_table_lock(t);

	v = table_set(ks, t, key);
	if (isnil(v)) {
		setnvalue(v, n);
	} else
		setnvalue(v, nvalue(v) + n);

	kp_table_unlock(t);
}

int kp_table_length(ktap_state *ks, ktap_table *t)
{
	unsigned long __maybe_unused flags;
	int i, len = 0;

	kp_table_lock(t);

	for (i = 0; i < t->sizearray; i++) {
		ktap_value *v = &t->array[i];

		if (isnil(v))
			continue;
		len++;
	}

	for (i = 0; i < sizenode(t); i++) {
		ktap_tnode *n = &t->node[i];

		if (isnil(gkey(n)))
			continue;

		len++;
	}
	
	kp_table_unlock(t);
	return len;
}

void kp_table_free(ktap_state *ks, ktap_table *t)
{
	if (t->sizearray > 0)
		kp_free(ks, t->array);
	if (!isdummy(t->node))
		kp_free(ks, t->node);

	kp_free_gclist(ks, t->gclist);
	kp_free(ks, t);
}

void kp_table_dump(ktap_state *ks, ktap_table *t)
{
	int i, count = 0;

	kp_puts(ks, "{");
	for (i = 0; i < t->sizearray; i++) {
		ktap_value *v = &t->array[i];

		if (isnil(v))
			continue;

		if (count)
			kp_puts(ks, ", ");

		kp_printf(ks, "(%d: ", i + 1);
		kp_showobj(ks, v);
		kp_puts(ks, ")");
		count++;
	}

	for (i = 0; i < sizenode(t); i++) {
		ktap_tnode *n = &t->node[i];

		if (isnil(gkey(n)))
			continue;

		if (count)
			kp_puts(ks, ", ");

		kp_puts(ks, "(");
		kp_showobj(ks, gkey(n));
		kp_puts(ks, ": ");
		kp_showobj(ks, gval(n));
		kp_puts(ks, ")");
		count++;
	}
	kp_puts(ks, "}");
}

/*
 * table-clear only set nil of all elements, not free t->array and nodes.
 * we assume user will reuse table soon after clear table, so reserve array
 * and nodes will avoid memory allocation when insert key-value again.
 */
void kp_table_clear(ktap_state *ks, ktap_table *t)
{
	unsigned long __maybe_unused flags;
	int i;

	kp_table_lock(t);

	for (i = 0; i < t->sizearray; i++) {
		ktap_value *v = &t->array[i];

		if (isnil(v))
			continue;

		setnilvalue(v);
	}

	for (i = 0; i < sizenode(t); i++) {
		ktap_tnode *n = &t->node[i];

		if (isnil(gkey(n)))
			continue;

		setnilvalue(gkey(n));
		setnilvalue(gval(n));
	}

	kp_table_unlock(t);
}

#ifdef __KERNEL__
static void string_convert(char *output, const char *input)
{
	if (strlen(input) > 32) {
		strncpy(output, input, 32-4);
		memset(output + 32-4, '.', 3);
	} else
		memcpy(output, input, strlen(input));
}

struct table_hist_record {
	ktap_value key;
	ktap_value val;
};

static int hist_record_cmp(const void *r1, const void *r2)
{
	const struct table_hist_record *i = r1;
	const struct table_hist_record *j = r2;

	if ((nvalue(&i->val) == nvalue(&j->val))) {
		return 0;
	} else if ((nvalue(&i->val) < nvalue(&j->val))) {
		return 1;
	} else
		return -1;
}

static int kp_aggracc_read(ktap_aggraccval *acc);

/* histogram: key should be number or string, value must be number */
static void table_histdump(ktap_state *ks, ktap_table *t, int shownums)
{
	struct table_hist_record *thr;
	unsigned long __maybe_unused flags;
	char dist_str[40];
	int i, ratio, total = 0, count = 0, top_num, is_kernel_address = 0;
	int size, num;

	size = sizeof(*thr) * (t->sizearray + sizenode(t));
	thr = kp_malloc(ks, size);
	if (!thr) {
		kp_error(ks, "Cannot allocate %d of histogram memory", size);
		return;
	}

	kp_table_lock(t);

	for (i = 0; i < t->sizearray; i++) {
		ktap_value *v = &t->array[i];

		if (isnil(v))
			continue;

		if (ttisnumber(v))
			num = nvalue(v);
		else if (ttisaggracc(v))
			num = kp_aggracc_read(aggraccvalue(v));
		else {
			kp_table_unlock(t);
			goto error;
		}

		setnvalue(&thr[count].key, i + 1);
		setnvalue(&thr[count].val, num);
		count++;
		total += num;
	}

	for (i = 0; i < sizenode(t); i++) {
		ktap_tnode *n = &t->node[i];
		ktap_value *v = gval(n);

		if (isnil(gkey(n)))
			continue;

		if (ttisnumber(v))
			num = nvalue(v);
		else if (ttisaggracc(v))
			num = kp_aggracc_read(aggraccvalue(v));
		else {
			kp_table_unlock(t);
			goto error;
		}

		setobj(&thr[count].key, gkey(n));
		setnvalue(&thr[count].val, num);
		count++;
		total += num;
	}

	kp_table_unlock(t);

	sort(thr, count, sizeof(struct table_hist_record),
	     hist_record_cmp, NULL);

	dist_str[sizeof(dist_str) - 1] = '\0';

	/* check the first key is a kernel text symbol or not */
	if (ttisnumber(&thr[0].key)) {
		char str[KSYM_SYMBOL_LEN];

		SPRINT_SYMBOL(str, nvalue(&thr[0].key));
		if (str[0] != '0' || str[1] != 'x')
			is_kernel_address = 1;
	}

	top_num = min(shownums, count);
	for (i = 0; i < top_num; i++) {
		ktap_value *key = &thr[i].key;
		ktap_value *val = &thr[i].val;

		memset(dist_str, ' ', sizeof(dist_str) - 1);
		ratio = (nvalue(val) * (sizeof(dist_str) - 1)) / total;
		memset(dist_str, '@', ratio);

		if (ttisstring(key)) {
			char buf[32 + 1] = {0};

			string_convert(buf, svalue(key));
			kp_printf(ks, "%32s |%s%-7d\n", buf, dist_str,
				      nvalue(val));
		} else if (ttisnumber(key)) {
			char str[KSYM_SYMBOL_LEN];
			char buf[32 + 1] = {0};

			if (is_kernel_address) {
				/* suppose it's a symbol, fix it in future */
				SPRINT_SYMBOL(str, nvalue(key));
				string_convert(buf, str);
				kp_printf(ks, "%32s |%s%-7d\n", buf, dist_str,
						nvalue(val));
			} else {
				kp_printf(ks, "%32d |%s%-7d\n", nvalue(key),
						dist_str, nvalue(val));
			}
		}
	}

	if (count > shownums)
		kp_printf(ks, "%32s |\n", "...");

	goto out;

 error:
	kp_puts(ks, "error: table histogram only handle "
			" (key: string/number val: number)\n");
 out:
	kp_free(ks, thr);
}

#define HISTOGRAM_DEFAULT_TOP_NUM	20

#define DISTRIBUTION_STR "------------- Distribution -------------"
void kp_table_histogram(ktap_state *ks, ktap_table *t)
{
	kp_printf(ks, "%32s%s%s\n", "value ", DISTRIBUTION_STR, " count");
	table_histdump(ks, t, HISTOGRAM_DEFAULT_TOP_NUM);
}

/*
 * Aggregation Table
 */

static ktap_table *table_new2(ktap_state *ks, ktap_gcobject **list)
{
	ktap_table *t = &kp_newobject(ks, KTAP_TTABLE, sizeof(ktap_table),
				      list)->h;
	t->flags = (u8)(~0);
	t->array = NULL;
	t->sizearray = 0;
	t->node = (ktap_tnode *)dummynode;
	t->gclist = NULL;
	setnodevector(ks, t, 0);

	kp_table_lock_init(t);
	return t;
}

static int kp_aggracc_read(ktap_aggraccval *acc)
{
	switch (acc->type) {
	case AGGREGATION_TYPE_COUNT:
	case AGGREGATION_TYPE_MAX:
	case AGGREGATION_TYPE_MIN:
	case AGGREGATION_TYPE_SUM:
		return acc->val;
	case AGGREGATION_TYPE_AVG:
		return acc->val / acc->more;
	default:
		return 0;
	}

}

void kp_aggraccval_dump(ktap_state *ks, ktap_aggraccval *acc)
{
	switch (acc->type) {
	case AGGREGATION_TYPE_COUNT:
	case AGGREGATION_TYPE_MAX:
	case AGGREGATION_TYPE_MIN:
	case AGGREGATION_TYPE_SUM:
		kp_printf(ks, "%d", acc->val);
		break;
	case AGGREGATION_TYPE_AVG:
		kp_printf(ks, "%d", acc->val / acc->more);
		break;
	default:
		break;
	}
}

static void synth_acc(ktap_aggraccval *acc1, ktap_aggraccval *acc2)
{
	switch (acc1->type) {
	case AGGREGATION_TYPE_COUNT:
		acc2->val += acc1->val;
		break;
	case AGGREGATION_TYPE_MAX:
		acc2->val = max(acc1->val, acc2->val);
		break;
	case AGGREGATION_TYPE_MIN:
		acc2->val = min(acc1->val, acc2->val);
		break;
	case AGGREGATION_TYPE_SUM:
		acc2->val += acc1->val;
		break;
	case AGGREGATION_TYPE_AVG:
		acc2->val += acc1->val;
		acc2->more += acc1->more;
		break;
	default:
		break;
	}
}

static ktap_aggraccval *get_accval(ktap_state *ks, int type,
				   ktap_gcobject **list)
{
	ktap_aggraccval *acc;

	acc = &kp_newobject(ks, KTAP_TAGGRACCVAL, sizeof(ktap_aggraccval),
				list)->acc;
	acc->type = type;
	acc->val = 0;
	acc->more = 0;
	return acc;
}

static void synth_accval(ktap_state *ks, ktap_value *o1, ktap_value *o2,
			 ktap_gcobject **list)
{
	ktap_aggraccval *acc;

	if (isnil(o2)) {
		acc = get_accval(ks, aggraccvalue(o1)->type, list);
		acc->val = aggraccvalue(o1)->val;
		acc->more = aggraccvalue(o1)->more;
		setaggraccvalue(o2, acc);
		return;
	}

	synth_acc(aggraccvalue(o1), aggraccvalue(o2));
}

static void move_table(ktap_state *ks, ktap_table *t1, ktap_table *t2)
{
	ktap_value *newv;
	ktap_value n;
	int i;

	for (i = 0; i < t1->sizearray; i++) {
		ktap_value *v = &t1->array[i];

		if (isnil(v))
			continue;

		setnvalue(&n, i);

		newv = table_set(ks, t2, &n);
		synth_accval(ks, v, newv, &t2->gclist);
	}

	for (i = 0; i < sizenode(t1); i++) {
		ktap_tnode *node = &t1->node[i];

		if (isnil(gkey(node)))
			continue;

		newv = table_set(ks, t2, gkey(node));
		synth_accval(ks, gval(node), newv, &t2->gclist);
	}
}

ktap_table *kp_aggrtable_synthesis(ktap_state *ks, ktap_aggrtable *ah)
{
	ktap_table *synth_tbl;
	int cpu;

	synth_tbl = table_new2(ks, &ah->gclist);

	for_each_possible_cpu(cpu) {
		ktap_table **t = per_cpu_ptr(ah->pcpu_tbl, cpu);
		move_table(ks, *t, synth_tbl);
	}

	return synth_tbl;
}

void kp_aggrtable_dump(ktap_state *ks, ktap_aggrtable *ah)
{
	kp_table_dump(ks, kp_aggrtable_synthesis(ks, ah));
}

ktap_aggrtable *kp_aggrtable_new(ktap_state *ks)
{
	ktap_aggrtable *ah;
	int cpu;

	ah = &kp_newobject(ks, KTAP_TAGGRTABLE, sizeof(ktap_aggrtable),
			NULL)->ah;
	ah->pcpu_tbl = alloc_percpu(ktap_table *);
	ah->gclist = NULL;

	for_each_possible_cpu(cpu) {
		ktap_table **t = per_cpu_ptr(ah->pcpu_tbl, cpu);
		*t = table_new2(ks, &ah->gclist);
	}	

	return ah;
}

void kp_aggrtable_free(ktap_state *ks, ktap_aggrtable *ah)
{
	free_percpu(ah->pcpu_tbl);
	kp_free_gclist(ks, ah->gclist);
	kp_free(ks, ah);
}

static
void handle_aggr_count(ktap_state *ks, ktap_aggrtable *ah, ktap_value *key)
{
	ktap_table *t = *__this_cpu_ptr(ah->pcpu_tbl);
	ktap_value *v = table_set(ks, t, key);
	ktap_aggraccval *acc;

	if (isnil(v)) {
		acc = get_accval(ks, AGGREGATION_TYPE_COUNT, &t->gclist);
		acc->val = 1;
		setaggraccvalue(v, acc);
		return;
	}

	acc = aggraccvalue(v);
	acc->val += 1;
}

static 
void handle_aggr_max(ktap_state *ks, ktap_aggrtable *ah, ktap_value *key)
{
	ktap_table *t = *__this_cpu_ptr(ah->pcpu_tbl);
	ktap_value *v = table_set(ks, t, key);
	ktap_aggraccval *acc;

	if (isnil(v)) {
		acc = get_accval(ks, AGGREGATION_TYPE_MAX, &t->gclist);
		acc->val = ks->aggr_accval;
		setaggraccvalue(v, acc);
		return;
	}

	acc = aggraccvalue(v);
	acc->val = max(acc->val, ks->aggr_accval);
}

static 
void handle_aggr_min(ktap_state *ks, ktap_aggrtable *ah, ktap_value *key)
{
	ktap_table *t = *__this_cpu_ptr(ah->pcpu_tbl);
	ktap_value *v = table_set(ks, t, key);
	ktap_aggraccval *acc;

	if (isnil(v)) {
		acc = get_accval(ks, AGGREGATION_TYPE_MIN, &t->gclist);
		acc->val = ks->aggr_accval;
		setaggraccvalue(v, acc);
		return;
	}

	acc = aggraccvalue(v);
	acc->val = min(acc->val, ks->aggr_accval);
}

static 
void handle_aggr_sum(ktap_state *ks, ktap_aggrtable *ah, ktap_value *key)
{
	ktap_table *t = *__this_cpu_ptr(ah->pcpu_tbl);
	ktap_value *v = table_set(ks, t, key);
	ktap_aggraccval *acc;

	if (isnil(v)) {
		acc = get_accval(ks, AGGREGATION_TYPE_SUM, &t->gclist);
		acc->val = ks->aggr_accval;
		setaggraccvalue(v, acc);
		return;
	}

	acc = aggraccvalue(v);
	acc->val += ks->aggr_accval;
}

static 
void handle_aggr_avg(ktap_state *ks, ktap_aggrtable *ah, ktap_value *key)
{
	ktap_table *t = *__this_cpu_ptr(ah->pcpu_tbl);
	ktap_value *v = table_set(ks, t, key);
	ktap_aggraccval *acc;

	if (isnil(v)) {
		acc = get_accval(ks, AGGREGATION_TYPE_AVG, &t->gclist);
		acc->val = ks->aggr_accval;
		acc->more = 1;
		setaggraccvalue(v, acc);
		return;
	}

	acc = aggraccvalue(v);
	acc->val += ks->aggr_accval;
	acc->more++;
}

typedef void (*aggr_func_t)(ktap_state *ks, ktap_aggrtable *ah, ktap_value *k);
static aggr_func_t kp_aggregation_handler[] = {
	handle_aggr_count,
	handle_aggr_max,
	handle_aggr_min,
	handle_aggr_sum,
	handle_aggr_avg
};

void kp_aggrtable_set(ktap_state *ks, ktap_aggrtable *ah,
			ktap_value *key, ktap_value *val)
{
	if (unlikely(!ttisaggrval(val))) {
		kp_error(ks, "set invalid value to aggregation table\n");
		return;
	}

	kp_aggregation_handler[nvalue(val)](ks, ah, key);
}


void kp_aggrtable_get(ktap_state *ks, ktap_aggrtable *ah, ktap_value *key,
		      ktap_value *val)
{
	ktap_aggraccval acc; /* in stack */
	const ktap_value *v;
	int cpu;

	acc.val = -1;
	acc.more = -1;

	for_each_possible_cpu(cpu) {
		ktap_table **t = per_cpu_ptr(ah->pcpu_tbl, cpu);

		v = table_get(*t, key);
		if (isnil(v))
			continue;

		if (acc.more == -1) {
			acc = *aggraccvalue(v);
			continue;
		}

		synth_acc(aggraccvalue(v), &acc);
	}

	if (acc.more == -1) {
		setnilvalue(val);
	} else {
		setnvalue(val, kp_aggracc_read(&acc));
	}
}

void kp_aggrtable_histogram(ktap_state *ks, ktap_aggrtable *ah)
{
	kp_table_histogram(ks, kp_aggrtable_synthesis(ks, ah));
}
#endif
