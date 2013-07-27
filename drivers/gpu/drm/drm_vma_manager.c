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
 * The above copyright notice and this permission notice shall be included in
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

#include <drm/drmP.h>
#include <drm/drm_mm.h>
#include <drm/drm_vma_manager.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/rbtree.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/types.h>

/**
 * DOC: vma offset manager
 *
 * The vma-manager is responsible to map arbitrary driver-dependent memory
 * regions into the linear user address-space. It provides offsets to the
 * caller which can then be used on the address_space of the drm-device. It
 * takes care to not overlap regions, size them appropriately and to not
 * confuse mm-core by inconsistent fake vm_pgoff fields.
 * Drivers shouldn't use this for object placement in VMEM. This manager should
 * only be used to manage mappings into linear user-space VMs.
 *
 * We use drm_mm as backend to manage object allocations. But it is highly
 * optimized for alloc/free calls, not lookups. Hence, we use an rb-tree to
 * speed up offset lookups.
 *
 * You must not use multiple offset managers on a single address_space.
 * Otherwise, mm-core will be unable to tear down memory mappings as the VM will
 * no longer be linear. Please use VM_NONLINEAR in that case and implement your
 * own offset managers.
 *
 * This offset manager works on page-based addresses. That is, every argument
 * and return code (with the exception of drm_vma_node_offset_addr()) is given
 * in number of pages, not number of bytes. That means, object sizes and offsets
 * must always be page-aligned (as usual).
 * If you want to get a valid byte-based user-space address for a given offset,
 * please see drm_vma_node_offset_addr().
 */

/**
 * drm_vma_offset_manager_init - Initialize new offset-manager
 * @mgr: Manager object
 * @page_offset: Offset of available memory area (page-based)
 * @size: Size of available address space range (page-based)
 *
 * Initialize a new offset-manager. The offset and area size available for the
 * manager are given as @page_offset and @size. Both are interpreted as
 * page-numbers, not bytes.
 *
 * Adding/removing nodes from the manager is locked internally and protected
 * against concurrent access. However, node allocation and destruction is left
 * for the caller. While calling into the vma-manager, a given node must
 * always be guaranteed to be referenced.
 */
void drm_vma_offset_manager_init(struct drm_vma_offset_manager *mgr,
				 unsigned long page_offset, unsigned long size)
{
	rwlock_init(&mgr->vm_lock);
	mgr->vm_addr_space_rb = RB_ROOT;
	drm_mm_init(&mgr->vm_addr_space_mm, page_offset, size);
}
EXPORT_SYMBOL(drm_vma_offset_manager_init);

/**
 * drm_vma_offset_manager_destroy() - Destroy offset manager
 * @mgr: Manager object
 *
 * Destroy an object manager which was previously created via
 * drm_vma_offset_manager_init(). The caller must remove all allocated nodes
 * before destroying the manager. Otherwise, drm_mm will refuse to free the
 * requested resources.
 *
 * The manager must not be accessed after this function is called.
 */
void drm_vma_offset_manager_destroy(struct drm_vma_offset_manager *mgr)
{
	/* take the lock to protect against buggy drivers */
	write_lock(&mgr->vm_lock);
	drm_mm_takedown(&mgr->vm_addr_space_mm);
	write_unlock(&mgr->vm_lock);
}
EXPORT_SYMBOL(drm_vma_offset_manager_destroy);

/**
 * drm_vma_offset_lookup() - Find node in offset space
 * @mgr: Manager object
 * @start: Start address for object (page-based)
 * @pages: Size of object (page-based)
 *
 * Find a node given a start address and object size. This returns the _best_
 * match for the given node. That is, @start may point somewhere into a valid
 * region and the given node will be returned, as long as the node spans the
 * whole requested area (given the size in number of pages as @pages).
 *
 * RETURNS:
 * Returns NULL if no suitable node can be found. Otherwise, the best match
 * is returned. It's the caller's responsibility to make sure the node doesn't
 * get destroyed before the caller can access it.
 */
struct drm_vma_offset_node *drm_vma_offset_lookup(struct drm_vma_offset_manager *mgr,
						  unsigned long start,
						  unsigned long pages)
{
	struct drm_vma_offset_node *node;

	read_lock(&mgr->vm_lock);
	node = drm_vma_offset_lookup_locked(mgr, start, pages);
	read_unlock(&mgr->vm_lock);

	return node;
}
EXPORT_SYMBOL(drm_vma_offset_lookup);

/**
 * drm_vma_offset_lookup_locked() - Find node in offset space
 * @mgr: Manager object
 * @start: Start address for object (page-based)
 * @pages: Size of object (page-based)
 *
 * Same as drm_vma_offset_lookup() but requires the caller to lock offset lookup
 * manually. See drm_vma_offset_lock_lookup() for an example.
 *
 * RETURNS:
 * Returns NULL if no suitable node can be found. Otherwise, the best match
 * is returned.
 */
struct drm_vma_offset_node *drm_vma_offset_lookup_locked(struct drm_vma_offset_manager *mgr,
							 unsigned long start,
							 unsigned long pages)
{
	struct drm_vma_offset_node *node, *best;
	struct rb_node *iter;
	unsigned long offset;

	iter = mgr->vm_addr_space_rb.rb_node;
	best = NULL;

	while (likely(iter)) {
		node = rb_entry(iter, struct drm_vma_offset_node, vm_rb);
		offset = node->vm_node.start;
		if (start >= offset) {
			iter = iter->rb_right;
			best = node;
			if (start == offset)
				break;
		} else {
			iter = iter->rb_left;
		}
	}

	/* verify that the node spans the requested area */
	if (best) {
		offset = best->vm_node.start + best->vm_node.size;
		if (offset < start + pages)
			best = NULL;
	}

	return best;
}
EXPORT_SYMBOL(drm_vma_offset_lookup_locked);

/* internal helper to link @node into the rb-tree */
static void _drm_vma_offset_add_rb(struct drm_vma_offset_manager *mgr,
				   struct drm_vma_offset_node *node)
{
	struct rb_node **iter = &mgr->vm_addr_space_rb.rb_node;
	struct rb_node *parent = NULL;
	struct drm_vma_offset_node *iter_node;

	while (likely(*iter)) {
		parent = *iter;
		iter_node = rb_entry(*iter, struct drm_vma_offset_node, vm_rb);

		if (node->vm_node.start < iter_node->vm_node.start)
			iter = &(*iter)->rb_left;
		else if (node->vm_node.start > iter_node->vm_node.start)
			iter = &(*iter)->rb_right;
		else
			BUG();
	}

	rb_link_node(&node->vm_rb, parent, iter);
	rb_insert_color(&node->vm_rb, &mgr->vm_addr_space_rb);
}

/**
 * drm_vma_offset_add() - Add offset node to manager
 * @mgr: Manager object
 * @node: Node to be added
 * @pages: Allocation size visible to user-space (in number of pages)
 *
 * Add a node to the offset-manager. If the node was already added, this does
 * nothing and return 0. @pages is the size of the object given in number of
 * pages.
 * After this call succeeds, you can access the offset of the node until it
 * is removed again.
 *
 * If this call fails, it is safe to retry the operation or call
 * drm_vma_offset_remove(), anyway. However, no cleanup is required in that
 * case.
 *
 * @pages is not required to be the same size as the underlying memory object
 * that you want to map. It only limits the size that user-space can map into
 * their address space.
 *
 * RETURNS:
 * 0 on success, negative error code on failure.
 */
int drm_vma_offset_add(struct drm_vma_offset_manager *mgr,
		       struct drm_vma_offset_node *node, unsigned long pages)
{
	int ret;

	write_lock(&mgr->vm_lock);

	if (drm_mm_node_allocated(&node->vm_node)) {
		ret = 0;
		goto out_unlock;
	}

	ret = drm_mm_insert_node(&mgr->vm_addr_space_mm, &node->vm_node,
				 pages, 0, DRM_MM_SEARCH_DEFAULT);
	if (ret)
		goto out_unlock;

	_drm_vma_offset_add_rb(mgr, node);

out_unlock:
	write_unlock(&mgr->vm_lock);
	return ret;
}
EXPORT_SYMBOL(drm_vma_offset_add);

/**
 * drm_vma_offset_remove() - Remove offset node from manager
 * @mgr: Manager object
 * @node: Node to be removed
 *
 * Remove a node from the offset manager. If the node wasn't added before, this
 * does nothing. After this call returns, the offset and size will be 0 until a
 * new offset is allocated via drm_vma_offset_add() again. Helper functions like
 * drm_vma_node_start() and drm_vma_node_offset_addr() will return 0 if no
 * offset is allocated.
 */
void drm_vma_offset_remove(struct drm_vma_offset_manager *mgr,
			   struct drm_vma_offset_node *node)
{
	write_lock(&mgr->vm_lock);

	if (drm_mm_node_allocated(&node->vm_node)) {
		rb_erase(&node->vm_rb, &mgr->vm_addr_space_rb);
		drm_mm_remove_node(&node->vm_node);
		memset(&node->vm_node, 0, sizeof(node->vm_node));
	}

	write_unlock(&mgr->vm_lock);
}
EXPORT_SYMBOL(drm_vma_offset_remove);
