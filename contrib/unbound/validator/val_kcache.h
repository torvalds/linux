/*
 * validator/val_kcache.h - validator key shared cache with validated keys
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
 * This file contains functions for caching validated key entries. 
 */

#ifndef VALIDATOR_VAL_KCACHE_H
#define VALIDATOR_VAL_KCACHE_H
#include "util/storage/slabhash.h"
struct key_entry_key;
struct key_entry_data;
struct config_file;
struct regional;
struct module_qstate;

/**
 * Key cache
 */
struct key_cache {
	/** uses slabhash for storage, type key_entry_key, key_entry_data */
	struct slabhash* slab;
};

/**
 * Create the key cache
 * @param cfg: config settings for the key cache.
 * @return new key cache or NULL on malloc failure.
 */
struct key_cache* key_cache_create(struct config_file* cfg);

/**
 * Delete the key cache
 * @param kcache: to delete
 */
void key_cache_delete(struct key_cache* kcache);

/**
 * Insert or update a key cache entry. Note that the insert may silently
 * fail if there is not enough memory.
 *
 * @param kcache: the key cache.
 * @param kkey: key entry key, assumed malloced in a region, is copied
 * 	to perform update or insertion. Its data pointer is also copied.
 * @param qstate: store errinf reason in case its bad.
 */
void key_cache_insert(struct key_cache* kcache, struct key_entry_key* kkey,
	struct module_qstate* qstate);

/**
 * Remove an entry from the key cache.
 * @param kcache: the key cache.
 * @param name: for what name to look; uncompressed wireformat
 * @param namelen: length of the name.
 * @param key_class: class of the key.
 */
void key_cache_remove(struct key_cache* kcache,
	uint8_t* name, size_t namelen, uint16_t key_class);

/**
 * Lookup key entry in the cache. Looks up the closest key entry above the
 * given name.
 * @param kcache: the key cache.
 * @param name: for what name to look; uncompressed wireformat
 * @param namelen: length of the name.
 * @param key_class: class of the key.
 * @param region: a copy of the key_entry is allocated in this region.
 * @param now: current time.
 * @return pointer to a newly allocated key_entry copy in the region, if
 * 	a key entry could be found, and allocation succeeded and TTL was OK.
 * 	Otherwise, NULL is returned.
 */
struct key_entry_key* key_cache_obtain(struct key_cache* kcache,
	uint8_t* name, size_t namelen, uint16_t key_class, 
	struct regional* region, time_t now);

/**
 * Get memory in use by the key cache.
 * @param kcache: the key cache.
 * @return memory in use in bytes.
 */
size_t key_cache_get_mem(struct key_cache* kcache);

#endif /* VALIDATOR_VAL_KCACHE_H */
