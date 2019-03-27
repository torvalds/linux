/* Copyright (c) 2013, Vsevolod Stakhov
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *       * Redistributions of source code must retain the above copyright
 *         notice, this list of conditions and the following disclaimer.
 *       * Redistributions in binary form must reproduce the above copyright
 *         notice, this list of conditions and the following disclaimer in the
 *         documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __UCL_HASH_H
#define __UCL_HASH_H

#include "ucl.h"

/******************************************************************************/

struct ucl_hash_node_s;
typedef struct ucl_hash_node_s ucl_hash_node_t;

typedef int (*ucl_hash_cmp_func) (const void* void_a, const void* void_b);
typedef void (*ucl_hash_free_func) (void *ptr);
typedef void* ucl_hash_iter_t;


/**
 * Linear chained hashtable.
 */
struct ucl_hash_struct;
typedef struct ucl_hash_struct ucl_hash_t;


/**
 * Initializes the hashtable.
 */
ucl_hash_t* ucl_hash_create (bool ignore_case);

/**
 * Deinitializes the hashtable.
 */
void ucl_hash_destroy (ucl_hash_t* hashlin, ucl_hash_free_func func);

/**
 * Inserts an element in the the hashtable.
 */
void ucl_hash_insert (ucl_hash_t* hashlin, const ucl_object_t *obj, const char *key,
		unsigned keylen);

/**
 * Replace element in the hash
 */
void ucl_hash_replace (ucl_hash_t* hashlin, const ucl_object_t *old,
		const ucl_object_t *new);

/**
 * Delete an element from the the hashtable.
 */
void ucl_hash_delete (ucl_hash_t* hashlin, const ucl_object_t *obj);

/**
 * Searches an element in the hashtable.
 */
const ucl_object_t* ucl_hash_search (ucl_hash_t* hashlin, const char *key,
		unsigned keylen);


/**
 * Iterate over hash table
 * @param hashlin hash
 * @param iter iterator (must be NULL on first iteration)
 * @return the next object
 */
const void* ucl_hash_iterate (ucl_hash_t *hashlin, ucl_hash_iter_t *iter);

/**
 * Check whether an iterator has next element
 */
bool ucl_hash_iter_has_next (ucl_hash_t *hashlin, ucl_hash_iter_t iter);

#endif
