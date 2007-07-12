/**************************************************************************
 *
 * Copyright 2006 Tungsten Graphics, Inc., Bismarck, ND., USA.
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
 * Simple memory MANager interface that keeps track on allocate regions on a
 * per "owner" basis. All regions associated with an "owner" can be released
 * with a simple call. Typically if the "owner" exists. The owner is any
 * "unsigned long" identifier. Can typically be a pointer to a file private
 * struct or a context identifier.
 *
 * Authors:
 * Thomas Hellström <thomas-at-tungstengraphics-dot-com>
 */

#ifndef DRM_SMAN_H
#define DRM_SMAN_H

#include "drmP.h"
#include "drm_hashtab.h"

/*
 * A class that is an abstration of a simple memory allocator.
 * The sman implementation provides a default such allocator
 * using the drm_mm.c implementation. But the user can replace it.
 * See the SiS implementation, which may use the SiS FB kernel module
 * for memory management.
 */

struct drm_sman_mm {
	/* private info. If allocated, needs to be destroyed by the destroy
	   function */
	void *private;

	/* Allocate a memory block with given size and alignment.
	   Return an opaque reference to the memory block */

	void *(*allocate) (void *private, unsigned long size,
			   unsigned alignment);

	/* Free a memory block. "ref" is the opaque reference that we got from
	   the "alloc" function */

	void (*free) (void *private, void *ref);

	/* Free all resources associated with this allocator */

	void (*destroy) (void *private);

	/* Return a memory offset from the opaque reference returned from the
	   "alloc" function */

	unsigned long (*offset) (void *private, void *ref);
};

struct drm_memblock_item {
	struct list_head owner_list;
	struct drm_hash_item user_hash;
	void *mm_info;
	struct drm_sman_mm *mm;
	struct drm_sman *sman;
};

struct drm_sman {
	struct drm_sman_mm *mm;
	int num_managers;
	struct drm_open_hash owner_hash_tab;
	struct drm_open_hash user_hash_tab;
	struct list_head owner_items;
};

/*
 * Take down a memory manager. This function should only be called after a
 * successful init and after a call to drm_sman_cleanup.
 */

extern void drm_sman_takedown(struct drm_sman * sman);

/*
 * Allocate structures for a manager.
 * num_managers are the number of memory pools to manage. (VRAM, AGP, ....)
 * user_order is the log2 of the number of buckets in the user hash table.
 *	    set this to approximately log2 of the max number of memory regions
 *	    that will be allocated for _all_ pools together.
 * owner_order is the log2 of the number of buckets in the owner hash table.
 *	    set this to approximately log2 of
 *	    the number of client file connections that will
 *	    be using the manager.
 *
 */

extern int drm_sman_init(struct drm_sman * sman, unsigned int num_managers,
			 unsigned int user_order, unsigned int owner_order);

/*
 * Initialize a drm_mm.c allocator. Should be called only once for each
 * manager unless a customized allogator is used.
 */

extern int drm_sman_set_range(struct drm_sman * sman, unsigned int manager,
			      unsigned long start, unsigned long size);

/*
 * Initialize a customized allocator for one of the managers.
 * (See the SiS module). The object pointed to by "allocator" is copied,
 * so it can be destroyed after this call.
 */

extern int drm_sman_set_manager(struct drm_sman * sman, unsigned int mananger,
				struct drm_sman_mm * allocator);

/*
 * Allocate a memory block. Aligment is not implemented yet.
 */

extern struct drm_memblock_item *drm_sman_alloc(struct drm_sman * sman,
						unsigned int manager,
						unsigned long size,
						unsigned alignment,
						unsigned long owner);
/*
 * Free a memory block identified by its user hash key.
 */

extern int drm_sman_free_key(struct drm_sman * sman, unsigned int key);

/*
 * returns 1 iff there are no stale memory blocks associated with this owner.
 * Typically called to determine if we need to idle the hardware and call
 * drm_sman_owner_cleanup. If there are no stale memory blocks, it removes all
 * resources associated with owner.
 */

extern int drm_sman_owner_clean(struct drm_sman * sman, unsigned long owner);

/*
 * Frees all stale memory blocks associated with this owner. Note that this
 * requires that the hardware is finished with all blocks, so the graphics engine
 * should be idled before this call is made. This function also frees
 * any resources associated with "owner" and should be called when owner
 * is not going to be referenced anymore.
 */

extern void drm_sman_owner_cleanup(struct drm_sman * sman, unsigned long owner);

/*
 * Frees all stale memory blocks associated with the memory manager.
 * See idling above.
 */

extern void drm_sman_cleanup(struct drm_sman * sman);

#endif
