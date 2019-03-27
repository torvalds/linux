/*
 * validator/val_kcache.c - validator key shared cache with validated keys
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
 * This file contains functions for dealing with the validator key cache.
 */
#include "config.h"
#include "validator/val_kcache.h"
#include "validator/val_kentry.h"
#include "util/log.h"
#include "util/config_file.h"
#include "util/data/dname.h"
#include "util/module.h"

struct key_cache* 
key_cache_create(struct config_file* cfg)
{
	struct key_cache* kcache = (struct key_cache*)calloc(1, 
		sizeof(*kcache));
	size_t numtables, start_size, maxmem;
	if(!kcache) {
		log_err("malloc failure");
		return NULL;
	}
	numtables = cfg->key_cache_slabs;
	start_size = HASH_DEFAULT_STARTARRAY;
	maxmem = cfg->key_cache_size;
	kcache->slab = slabhash_create(numtables, start_size, maxmem,
		&key_entry_sizefunc, &key_entry_compfunc,
		&key_entry_delkeyfunc, &key_entry_deldatafunc, NULL);
	if(!kcache->slab) {
		log_err("malloc failure");
		free(kcache);
		return NULL;
	}
	return kcache;
}

void 
key_cache_delete(struct key_cache* kcache)
{
	if(!kcache)
		return;
	slabhash_delete(kcache->slab);
	free(kcache);
}

void 
key_cache_insert(struct key_cache* kcache, struct key_entry_key* kkey,
	struct module_qstate* qstate)
{
	struct key_entry_key* k = key_entry_copy(kkey);
	if(!k)
		return;
	if(key_entry_isbad(k) && qstate->errinf &&
		qstate->env->cfg->val_log_level >= 2) {
		/* on malloc failure there is simply no reason string */
		key_entry_set_reason(k, errinf_to_str_bogus(qstate));
	}
	key_entry_hash(k);
	slabhash_insert(kcache->slab, k->entry.hash, &k->entry, 
		k->entry.data, NULL);
}

/**
 * Lookup exactly in the key cache. Returns pointer to locked entry.
 * Caller must unlock it after use.
 * @param kcache: the key cache.
 * @param name: for what name to look; uncompressed wireformat
 * @param namelen: length of the name.
 * @param key_class: class of the key.
 * @param wr: set true to get a writelock.
 * @return key entry, locked, or NULL if not found. No TTL checking is
 * 	performed.
 */
static struct key_entry_key*
key_cache_search(struct key_cache* kcache, uint8_t* name, size_t namelen, 
	uint16_t key_class, int wr)
{
	struct lruhash_entry* e;
	struct key_entry_key lookfor;
	lookfor.entry.key = &lookfor;
	lookfor.name = name;
	lookfor.namelen = namelen;
	lookfor.key_class = key_class;
	key_entry_hash(&lookfor);
	e = slabhash_lookup(kcache->slab, lookfor.entry.hash, &lookfor, wr);
	if(!e) 
		return NULL;
	return (struct key_entry_key*)e->key;
}

struct key_entry_key* 
key_cache_obtain(struct key_cache* kcache, uint8_t* name, size_t namelen, 
	uint16_t key_class, struct regional* region, time_t now)
{
	/* keep looking until we find a nonexpired entry */
	while(1) {
		struct key_entry_key* k = key_cache_search(kcache, name, 
			namelen, key_class, 0);
		if(k) {
			/* see if TTL is OK */
			struct key_entry_data* d = (struct key_entry_data*)
				k->entry.data;
			if(now <= d->ttl) {
				/* copy and return it */
				struct key_entry_key* retkey =
					key_entry_copy_toregion(k, region);
				lock_rw_unlock(&k->entry.lock);
				return retkey;
			}
			lock_rw_unlock(&k->entry.lock);
		}
		/* snip off first label to continue */
		if(dname_is_root(name))
			break;
		dname_remove_label(&name, &namelen);
	}
	return NULL;
}

size_t 
key_cache_get_mem(struct key_cache* kcache)
{
	return sizeof(*kcache) + slabhash_get_mem(kcache->slab);
}

void key_cache_remove(struct key_cache* kcache,
	uint8_t* name, size_t namelen, uint16_t key_class)
{
	struct key_entry_key lookfor;
	lookfor.entry.key = &lookfor;
	lookfor.name = name;
	lookfor.namelen = namelen;
	lookfor.key_class = key_class;
	key_entry_hash(&lookfor);
	slabhash_remove(kcache->slab, lookfor.entry.hash, &lookfor);
}
