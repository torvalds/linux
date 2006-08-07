/**************************************************************************
 *
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
 *
 *
 **************************************************************************/
/*
 * Simple open hash tab implementation.
 *
 * Authors:
 * Thomas Hellström <thomas-at-tungstengraphics-dot-com>
 */

#ifndef DRM_HASHTAB_H
#define DRM_HASHTAB_H

#define drm_hash_entry(_ptr, _type, _member) container_of(_ptr, _type, _member)

typedef struct drm_hash_item{
	struct hlist_node head;
	unsigned long key;
} drm_hash_item_t;

typedef struct drm_open_hash{
	unsigned int size;
	unsigned int order;
	unsigned int fill;
	struct hlist_head *table;
} drm_open_hash_t;


extern int drm_ht_create(drm_open_hash_t *ht, unsigned int order);
extern int drm_ht_insert_item(drm_open_hash_t *ht, drm_hash_item_t *item);
extern int drm_ht_just_insert_please(drm_open_hash_t *ht, drm_hash_item_t *item,
				     unsigned long seed, int bits, int shift,
				     unsigned long add);
extern int drm_ht_find_item(drm_open_hash_t *ht, unsigned long key, drm_hash_item_t **item);

extern void drm_ht_verbose_list(drm_open_hash_t *ht, unsigned long key);
extern int drm_ht_remove_key(drm_open_hash_t *ht, unsigned long key);
extern int drm_ht_remove_item(drm_open_hash_t *ht, drm_hash_item_t *item);
extern void drm_ht_remove(drm_open_hash_t *ht);


#endif

