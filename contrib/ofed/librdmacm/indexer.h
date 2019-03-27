/*
 * Copyright (c) 2011 Intel Corporation.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#if !defined(INDEXER_H)
#define INDEXER_H

#include <config.h>
#include <stddef.h>
#include <sys/types.h>

/*
 * Indexer - to find a structure given an index.  Synchronization
 * must be provided by the caller.  Caller must initialize the
 * indexer by setting free_list and size to 0.
 */

union idx_entry {
	void *item;
	int   next;
};

#define IDX_INDEX_BITS 16
#define IDX_ENTRY_BITS 10
#define IDX_ENTRY_SIZE (1 << IDX_ENTRY_BITS)
#define IDX_ARRAY_SIZE (1 << (IDX_INDEX_BITS - IDX_ENTRY_BITS))
#define IDX_MAX_INDEX  ((1 << IDX_INDEX_BITS) - 1)

struct indexer
{
	union idx_entry *array[IDX_ARRAY_SIZE];
	int		 free_list;
	int		 size;
};

#define idx_array_index(index) (index >> IDX_ENTRY_BITS)
#define idx_entry_index(index) (index & (IDX_ENTRY_SIZE - 1))

int idx_insert(struct indexer *idx, void *item);
void *idx_remove(struct indexer *idx, int index);
void idx_replace(struct indexer *idx, int index, void *item);

static inline void *idx_at(struct indexer *idx, int index)
{
	return (idx->array[idx_array_index(index)] + idx_entry_index(index))->item;
}

/*
 * Index map - associates a structure with an index.  Synchronization
 * must be provided by the caller.  Caller must initialize the
 * index map by setting it to 0.
 */

struct index_map
{
	void **array[IDX_ARRAY_SIZE];
};

int idm_set(struct index_map *idm, int index, void *item);
void *idm_clear(struct index_map *idm, int index);

static inline void *idm_at(struct index_map *idm, int index)
{
	void **entry;
	entry = idm->array[idx_array_index(index)];
	return entry[idx_entry_index(index)];
}

static inline void *idm_lookup(struct index_map *idm, int index)
{
	return ((index <= IDX_MAX_INDEX) && idm->array[idx_array_index(index)]) ?
		idm_at(idm, index) : NULL;
}

typedef struct _dlist_entry {
	struct _dlist_entry	*next;
	struct _dlist_entry	*prev;
}	dlist_entry;

static inline void dlist_init(dlist_entry *head)
{
	head->next = head;
	head->prev = head;
}

static inline int dlist_empty(dlist_entry *head)
{
	return head->next == head;
}

static inline void dlist_insert_after(dlist_entry *item, dlist_entry *head)
{
	item->next = head->next;
	item->prev = head;
	head->next->prev = item;
	head->next = item;
}

static inline void dlist_insert_before(dlist_entry *item, dlist_entry *head)
{
	dlist_insert_after(item, head->prev);
}

#define dlist_insert_head dlist_insert_after
#define dlist_insert_tail dlist_insert_before

static inline void dlist_remove(dlist_entry *item)
{
	item->prev->next = item->next;
	item->next->prev = item->prev;
}

#endif /* INDEXER_H */
