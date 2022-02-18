/*
 * Copyright 2006 Tungsten Graphics, Inc., Bismack, ND. USA.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * Simple open hash tab implementation.
 *
 * Authors:
 * Thomas Hellström <thomas-at-tungstengraphics-dot-com>
 */

/*
 * TODO: Replace this hashtable with Linux' generic implementation
 *       from <linux/hashtable.h>.
 */

#ifndef VMWGFX_HASHTAB_H
#define VMWGFX_HASHTAB_H

#include <linux/list.h>

#define drm_hash_entry(_ptr, _type, _member) container_of(_ptr, _type, _member)

struct vmwgfx_hash_item {
	struct hlist_node head;
	unsigned long key;
};

struct vmwgfx_open_hash {
	struct hlist_head *table;
	u8 order;
};

int vmwgfx_ht_create(struct vmwgfx_open_hash *ht, unsigned int order);
int vmwgfx_ht_insert_item(struct vmwgfx_open_hash *ht, struct vmwgfx_hash_item *item);
int vmwgfx_ht_just_insert_please(struct vmwgfx_open_hash *ht, struct vmwgfx_hash_item *item,
				 unsigned long seed, int bits, int shift,
				 unsigned long add);
int vmwgfx_ht_find_item(struct vmwgfx_open_hash *ht, unsigned long key,
			struct vmwgfx_hash_item **item);

void vmwgfx_ht_verbose_list(struct vmwgfx_open_hash *ht, unsigned long key);
int vmwgfx_ht_remove_key(struct vmwgfx_open_hash *ht, unsigned long key);
int vmwgfx_ht_remove_item(struct vmwgfx_open_hash *ht, struct vmwgfx_hash_item *item);
void vmwgfx_ht_remove(struct vmwgfx_open_hash *ht);

/*
 * RCU-safe interface
 *
 * The user of this API needs to make sure that two or more instances of the
 * hash table manipulation functions are never run simultaneously.
 * The lookup function vmwgfx_ht_find_item_rcu may, however, run simultaneously
 * with any of the manipulation functions as long as it's called from within
 * an RCU read-locked section.
 */
#define vmwgfx_ht_insert_item_rcu vmwgfx_ht_insert_item
#define vmwgfx_ht_just_insert_please_rcu vmwgfx_ht_just_insert_please
#define vmwgfx_ht_remove_key_rcu vmwgfx_ht_remove_key
#define vmwgfx_ht_remove_item_rcu vmwgfx_ht_remove_item
#define vmwgfx_ht_find_item_rcu vmwgfx_ht_find_item

#endif
