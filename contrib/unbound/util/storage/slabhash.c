/*
 * util/storage/slabhash.c - hashtable consisting of several smaller tables.
 *
 * Copyright (c) 2007, NLnet Labs. All rights reserved.
 *
 * This software is open source.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 
 * Neither the name of the NLNET LABS nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * \file
 *
 * Implementation of hash table that consists of smaller hash tables.
 * This results in a partitioned lruhash table.
 * It cannot grow, but that gives it the ability to have multiple
 * locks. Also this means there are multiple LRU lists.
 */

#include "config.h"
#include "util/storage/slabhash.h"

struct slabhash* slabhash_create(size_t numtables, size_t start_size, 
	size_t maxmem, lruhash_sizefunc_type sizefunc, 
	lruhash_compfunc_type compfunc, lruhash_delkeyfunc_type delkeyfunc, 
	lruhash_deldatafunc_type deldatafunc, void* arg)
{
	size_t i;
	struct slabhash* sl = (struct slabhash*)calloc(1, 
		sizeof(struct slabhash));
	if(!sl) return NULL;
	sl->size = numtables;
	log_assert(sl->size > 0);
	sl->array = (struct lruhash**)calloc(sl->size, sizeof(struct lruhash*));
	if(!sl->array) {
		free(sl);
		return NULL;
	}
	sl->mask = (uint32_t)(sl->size - 1);
	if(sl->mask == 0) {
		sl->shift = 0;
	} else {
		log_assert( (sl->size & sl->mask) == 0 
			/* size must be power of 2 */ );
		sl->shift = 0;
		while(!(sl->mask & 0x80000000)) {
			sl->mask <<= 1;
			sl->shift ++;
		}
	}
	for(i=0; i<sl->size; i++) {
		sl->array[i] = lruhash_create(start_size, maxmem / sl->size,
			sizefunc, compfunc, delkeyfunc, deldatafunc, arg);
		if(!sl->array[i]) {
			slabhash_delete(sl);
			return NULL;
		}
	}
	return sl;
}

void slabhash_delete(struct slabhash* sl)
{
	if(!sl)
		return;
	if(sl->array) {
		size_t i;
		for(i=0; i<sl->size; i++)
			lruhash_delete(sl->array[i]);
		free(sl->array);
	}
	free(sl);
}

void slabhash_clear(struct slabhash* sl)
{
	size_t i;
	if(!sl)
		return;
	for(i=0; i<sl->size; i++)
		lruhash_clear(sl->array[i]);
}

/** helper routine to calculate the slabhash index */
static unsigned int
slab_idx(struct slabhash* sl, hashvalue_type hash)
{
	return ((hash & sl->mask) >> sl->shift);
}

void slabhash_insert(struct slabhash* sl, hashvalue_type hash, 
	struct lruhash_entry* entry, void* data, void* arg)
{
	lruhash_insert(sl->array[slab_idx(sl, hash)], hash, entry, data, arg);
}

struct lruhash_entry* slabhash_lookup(struct slabhash* sl, 
	hashvalue_type hash, void* key, int wr)
{
	return lruhash_lookup(sl->array[slab_idx(sl, hash)], hash, key, wr);
}

void slabhash_remove(struct slabhash* sl, hashvalue_type hash, void* key)
{
	lruhash_remove(sl->array[slab_idx(sl, hash)], hash, key);
}

void slabhash_status(struct slabhash* sl, const char* id, int extended)
{
	size_t i;
	char num[17];
	log_info("Slabhash %s: %u tables mask=%x shift=%d", 
		id, (unsigned)sl->size, (unsigned)sl->mask, sl->shift);
	for(i=0; i<sl->size; i++) {
		snprintf(num, sizeof(num), "table %u", (unsigned)i);
		lruhash_status(sl->array[i], num, extended);
	}
}

size_t slabhash_get_size(struct slabhash* sl)
{
	size_t i, total = 0;
	for(i=0; i<sl->size; i++) {
		lock_quick_lock(&sl->array[i]->lock);
		total += sl->array[i]->space_max;
		lock_quick_unlock(&sl->array[i]->lock);
	}
	return total;
}

int slabhash_is_size(struct slabhash* sl, size_t size, size_t slabs)
{
	/* divide by slabs and then multiply by the number of slabs,
	 * because if the size is not an even multiple of slabs, the
	 * uneven amount needs to be removed for comparison */
	if(!sl) return 0;
	if(sl->size != slabs) return 0;
	if(slabs == 0) return 0;
	if( (size/slabs)*slabs == slabhash_get_size(sl))
		return 1;
	return 0;
}

size_t slabhash_get_mem(struct slabhash* sl)
{	
	size_t i, total = sizeof(*sl);
	total += sizeof(struct lruhash*)*sl->size;
	for(i=0; i<sl->size; i++) {
		total += lruhash_get_mem(sl->array[i]);
	}
	return total;
}

struct lruhash* slabhash_gettable(struct slabhash* sl, hashvalue_type hash)
{
	return sl->array[slab_idx(sl, hash)];
}

/* test code, here to avoid linking problems with fptr_wlist */
/** delete key */
static void delkey(struct slabhash_testkey* k) {
	lock_rw_destroy(&k->entry.lock); free(k);}
/** delete data */
static void deldata(struct slabhash_testdata* d) {free(d);}

size_t test_slabhash_sizefunc(void* ATTR_UNUSED(key), void* ATTR_UNUSED(data))
{
	return sizeof(struct slabhash_testkey) + 
		sizeof(struct slabhash_testdata);
}

int test_slabhash_compfunc(void* key1, void* key2)
{
	struct slabhash_testkey* k1 = (struct slabhash_testkey*)key1;
	struct slabhash_testkey* k2 = (struct slabhash_testkey*)key2;
	if(k1->id == k2->id)
		return 0;
	if(k1->id > k2->id)
		return 1;
	return -1;
}

void test_slabhash_delkey(void* key, void* ATTR_UNUSED(arg))
{
	delkey((struct slabhash_testkey*)key);
}

void test_slabhash_deldata(void* data, void* ATTR_UNUSED(arg))
{
	deldata((struct slabhash_testdata*)data);
}

void slabhash_setmarkdel(struct slabhash* sl, lruhash_markdelfunc_type md)
{
	size_t i;
	for(i=0; i<sl->size; i++) {
		lruhash_setmarkdel(sl->array[i], md);
	}
}

void slabhash_traverse(struct slabhash* sh, int wr,
	void (*func)(struct lruhash_entry*, void*), void* arg)
{
	size_t i;
	for(i=0; i<sh->size; i++)
		lruhash_traverse(sh->array[i], wr, func, arg);
}

size_t count_slabhash_entries(struct slabhash* sh)
{
	size_t slab, cnt = 0;

	for(slab=0; slab<sh->size; slab++) {
		lock_quick_lock(&sh->array[slab]->lock);
		cnt += sh->array[slab]->num;
		lock_quick_unlock(&sh->array[slab]->lock);
	}
	return cnt;
}
