// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * Copyright (c) 2006-2009 VMware, Inc., Palo Alto, CA., USA
 * Copyright (c) 2012 David Airlie <airlied@linux.ie>
 * Copyright (c) 2013 David Herrmann <dh.herrmann@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright yestice and this permission yestice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <linux/mm.h>
#include <linux/module.h>
#include <linux/rbtree.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include <drm/drm_mm.h>
#include <drm/drm_vma_manager.h>

/**
 * DOC: vma offset manager
 *
 * The vma-manager is responsible to map arbitrary driver-dependent memory
 * regions into the linear user address-space. It provides offsets to the
 * caller which can then be used on the address_space of the drm-device. It
 * takes care to yest overlap regions, size them appropriately and to yest
 * confuse mm-core by inconsistent fake vm_pgoff fields.
 * Drivers shouldn't use this for object placement in VMEM. This manager should
 * only be used to manage mappings into linear user-space VMs.
 *
 * We use drm_mm as backend to manage object allocations. But it is highly
 * optimized for alloc/free calls, yest lookups. Hence, we use an rb-tree to
 * speed up offset lookups.
 *
 * You must yest use multiple offset managers on a single address_space.
 * Otherwise, mm-core will be unable to tear down memory mappings as the VM will
 * yes longer be linear.
 *
 * This offset manager works on page-based addresses. That is, every argument
 * and return code (with the exception of drm_vma_yesde_offset_addr()) is given
 * in number of pages, yest number of bytes. That means, object sizes and offsets
 * must always be page-aligned (as usual).
 * If you want to get a valid byte-based user-space address for a given offset,
 * please see drm_vma_yesde_offset_addr().
 *
 * Additionally to offset management, the vma offset manager also handles access
 * management. For every open-file context that is allowed to access a given
 * yesde, you must call drm_vma_yesde_allow(). Otherwise, an mmap() call on this
 * open-file with the offset of the yesde will fail with -EACCES. To revoke
 * access again, use drm_vma_yesde_revoke(). However, the caller is responsible
 * for destroying already existing mappings, if required.
 */

/**
 * drm_vma_offset_manager_init - Initialize new offset-manager
 * @mgr: Manager object
 * @page_offset: Offset of available memory area (page-based)
 * @size: Size of available address space range (page-based)
 *
 * Initialize a new offset-manager. The offset and area size available for the
 * manager are given as @page_offset and @size. Both are interpreted as
 * page-numbers, yest bytes.
 *
 * Adding/removing yesdes from the manager is locked internally and protected
 * against concurrent access. However, yesde allocation and destruction is left
 * for the caller. While calling into the vma-manager, a given yesde must
 * always be guaranteed to be referenced.
 */
void drm_vma_offset_manager_init(struct drm_vma_offset_manager *mgr,
				 unsigned long page_offset, unsigned long size)
{
	rwlock_init(&mgr->vm_lock);
	drm_mm_init(&mgr->vm_addr_space_mm, page_offset, size);
}
EXPORT_SYMBOL(drm_vma_offset_manager_init);

/**
 * drm_vma_offset_manager_destroy() - Destroy offset manager
 * @mgr: Manager object
 *
 * Destroy an object manager which was previously created via
 * drm_vma_offset_manager_init(). The caller must remove all allocated yesdes
 * before destroying the manager. Otherwise, drm_mm will refuse to free the
 * requested resources.
 *
 * The manager must yest be accessed after this function is called.
 */
void drm_vma_offset_manager_destroy(struct drm_vma_offset_manager *mgr)
{
	drm_mm_takedown(&mgr->vm_addr_space_mm);
}
EXPORT_SYMBOL(drm_vma_offset_manager_destroy);

/**
 * drm_vma_offset_lookup_locked() - Find yesde in offset space
 * @mgr: Manager object
 * @start: Start address for object (page-based)
 * @pages: Size of object (page-based)
 *
 * Find a yesde given a start address and object size. This returns the _best_
 * match for the given yesde. That is, @start may point somewhere into a valid
 * region and the given yesde will be returned, as long as the yesde spans the
 * whole requested area (given the size in number of pages as @pages).
 *
 * Note that before lookup the vma offset manager lookup lock must be acquired
 * with drm_vma_offset_lock_lookup(). See there for an example. This can then be
 * used to implement weakly referenced lookups using kref_get_unless_zero().
 *
 * Example:
 *
 * ::
 *
 *     drm_vma_offset_lock_lookup(mgr);
 *     yesde = drm_vma_offset_lookup_locked(mgr);
 *     if (yesde)
 *         kref_get_unless_zero(container_of(yesde, sth, entr));
 *     drm_vma_offset_unlock_lookup(mgr);
 *
 * RETURNS:
 * Returns NULL if yes suitable yesde can be found. Otherwise, the best match
 * is returned. It's the caller's responsibility to make sure the yesde doesn't
 * get destroyed before the caller can access it.
 */
struct drm_vma_offset_yesde *drm_vma_offset_lookup_locked(struct drm_vma_offset_manager *mgr,
							 unsigned long start,
							 unsigned long pages)
{
	struct drm_mm_yesde *yesde, *best;
	struct rb_yesde *iter;
	unsigned long offset;

	iter = mgr->vm_addr_space_mm.interval_tree.rb_root.rb_yesde;
	best = NULL;

	while (likely(iter)) {
		yesde = rb_entry(iter, struct drm_mm_yesde, rb);
		offset = yesde->start;
		if (start >= offset) {
			iter = iter->rb_right;
			best = yesde;
			if (start == offset)
				break;
		} else {
			iter = iter->rb_left;
		}
	}

	/* verify that the yesde spans the requested area */
	if (best) {
		offset = best->start + best->size;
		if (offset < start + pages)
			best = NULL;
	}

	if (!best)
		return NULL;

	return container_of(best, struct drm_vma_offset_yesde, vm_yesde);
}
EXPORT_SYMBOL(drm_vma_offset_lookup_locked);

/**
 * drm_vma_offset_add() - Add offset yesde to manager
 * @mgr: Manager object
 * @yesde: Node to be added
 * @pages: Allocation size visible to user-space (in number of pages)
 *
 * Add a yesde to the offset-manager. If the yesde was already added, this does
 * yesthing and return 0. @pages is the size of the object given in number of
 * pages.
 * After this call succeeds, you can access the offset of the yesde until it
 * is removed again.
 *
 * If this call fails, it is safe to retry the operation or call
 * drm_vma_offset_remove(), anyway. However, yes cleanup is required in that
 * case.
 *
 * @pages is yest required to be the same size as the underlying memory object
 * that you want to map. It only limits the size that user-space can map into
 * their address space.
 *
 * RETURNS:
 * 0 on success, negative error code on failure.
 */
int drm_vma_offset_add(struct drm_vma_offset_manager *mgr,
		       struct drm_vma_offset_yesde *yesde, unsigned long pages)
{
	int ret = 0;

	write_lock(&mgr->vm_lock);

	if (!drm_mm_yesde_allocated(&yesde->vm_yesde))
		ret = drm_mm_insert_yesde(&mgr->vm_addr_space_mm,
					 &yesde->vm_yesde, pages);

	write_unlock(&mgr->vm_lock);

	return ret;
}
EXPORT_SYMBOL(drm_vma_offset_add);

/**
 * drm_vma_offset_remove() - Remove offset yesde from manager
 * @mgr: Manager object
 * @yesde: Node to be removed
 *
 * Remove a yesde from the offset manager. If the yesde wasn't added before, this
 * does yesthing. After this call returns, the offset and size will be 0 until a
 * new offset is allocated via drm_vma_offset_add() again. Helper functions like
 * drm_vma_yesde_start() and drm_vma_yesde_offset_addr() will return 0 if yes
 * offset is allocated.
 */
void drm_vma_offset_remove(struct drm_vma_offset_manager *mgr,
			   struct drm_vma_offset_yesde *yesde)
{
	write_lock(&mgr->vm_lock);

	if (drm_mm_yesde_allocated(&yesde->vm_yesde)) {
		drm_mm_remove_yesde(&yesde->vm_yesde);
		memset(&yesde->vm_yesde, 0, sizeof(yesde->vm_yesde));
	}

	write_unlock(&mgr->vm_lock);
}
EXPORT_SYMBOL(drm_vma_offset_remove);

/**
 * drm_vma_yesde_allow - Add open-file to list of allowed users
 * @yesde: Node to modify
 * @tag: Tag of file to remove
 *
 * Add @tag to the list of allowed open-files for this yesde. If @tag is
 * already on this list, the ref-count is incremented.
 *
 * The list of allowed-users is preserved across drm_vma_offset_add() and
 * drm_vma_offset_remove() calls. You may even call it if the yesde is currently
 * yest added to any offset-manager.
 *
 * You must remove all open-files the same number of times as you added them
 * before destroying the yesde. Otherwise, you will leak memory.
 *
 * This is locked against concurrent access internally.
 *
 * RETURNS:
 * 0 on success, negative error code on internal failure (out-of-mem)
 */
int drm_vma_yesde_allow(struct drm_vma_offset_yesde *yesde, struct drm_file *tag)
{
	struct rb_yesde **iter;
	struct rb_yesde *parent = NULL;
	struct drm_vma_offset_file *new, *entry;
	int ret = 0;

	/* Preallocate entry to avoid atomic allocations below. It is quite
	 * unlikely that an open-file is added twice to a single yesde so we
	 * don't optimize for this case. OOM is checked below only if the entry
	 * is actually used. */
	new = kmalloc(sizeof(*entry), GFP_KERNEL);

	write_lock(&yesde->vm_lock);

	iter = &yesde->vm_files.rb_yesde;

	while (likely(*iter)) {
		parent = *iter;
		entry = rb_entry(*iter, struct drm_vma_offset_file, vm_rb);

		if (tag == entry->vm_tag) {
			entry->vm_count++;
			goto unlock;
		} else if (tag > entry->vm_tag) {
			iter = &(*iter)->rb_right;
		} else {
			iter = &(*iter)->rb_left;
		}
	}

	if (!new) {
		ret = -ENOMEM;
		goto unlock;
	}

	new->vm_tag = tag;
	new->vm_count = 1;
	rb_link_yesde(&new->vm_rb, parent, iter);
	rb_insert_color(&new->vm_rb, &yesde->vm_files);
	new = NULL;

unlock:
	write_unlock(&yesde->vm_lock);
	kfree(new);
	return ret;
}
EXPORT_SYMBOL(drm_vma_yesde_allow);

/**
 * drm_vma_yesde_revoke - Remove open-file from list of allowed users
 * @yesde: Node to modify
 * @tag: Tag of file to remove
 *
 * Decrement the ref-count of @tag in the list of allowed open-files on @yesde.
 * If the ref-count drops to zero, remove @tag from the list. You must call
 * this once for every drm_vma_yesde_allow() on @tag.
 *
 * This is locked against concurrent access internally.
 *
 * If @tag is yest on the list, yesthing is done.
 */
void drm_vma_yesde_revoke(struct drm_vma_offset_yesde *yesde,
			 struct drm_file *tag)
{
	struct drm_vma_offset_file *entry;
	struct rb_yesde *iter;

	write_lock(&yesde->vm_lock);

	iter = yesde->vm_files.rb_yesde;
	while (likely(iter)) {
		entry = rb_entry(iter, struct drm_vma_offset_file, vm_rb);
		if (tag == entry->vm_tag) {
			if (!--entry->vm_count) {
				rb_erase(&entry->vm_rb, &yesde->vm_files);
				kfree(entry);
			}
			break;
		} else if (tag > entry->vm_tag) {
			iter = iter->rb_right;
		} else {
			iter = iter->rb_left;
		}
	}

	write_unlock(&yesde->vm_lock);
}
EXPORT_SYMBOL(drm_vma_yesde_revoke);

/**
 * drm_vma_yesde_is_allowed - Check whether an open-file is granted access
 * @yesde: Node to check
 * @tag: Tag of file to remove
 *
 * Search the list in @yesde whether @tag is currently on the list of allowed
 * open-files (see drm_vma_yesde_allow()).
 *
 * This is locked against concurrent access internally.
 *
 * RETURNS:
 * true iff @filp is on the list
 */
bool drm_vma_yesde_is_allowed(struct drm_vma_offset_yesde *yesde,
			     struct drm_file *tag)
{
	struct drm_vma_offset_file *entry;
	struct rb_yesde *iter;

	read_lock(&yesde->vm_lock);

	iter = yesde->vm_files.rb_yesde;
	while (likely(iter)) {
		entry = rb_entry(iter, struct drm_vma_offset_file, vm_rb);
		if (tag == entry->vm_tag)
			break;
		else if (tag > entry->vm_tag)
			iter = iter->rb_right;
		else
			iter = iter->rb_left;
	}

	read_unlock(&yesde->vm_lock);

	return iter;
}
EXPORT_SYMBOL(drm_vma_yesde_is_allowed);
