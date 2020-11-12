/*
 *
 * (C) COPYRIGHT 2020 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 * SPDX-License-Identifier: GPL-2.0
 *
 */

#include <mali_kbase.h>
#include <mali_kbase_mem_linux.h>
#include <mali_kbase_defs.h>
#include <mali_kbase_trace_gpu_mem.h>

/**
 * struct kbase_dma_buf - Object instantiated when a dma-buf imported allocation
 *                        is mapped to GPU for the first time within a process.
 *                        Another instantiation is done for the case when that
 *                        allocation is mapped for the first time to GPU.
 *
 * @dma_buf:              Reference to dma_buf been imported.
 * @dma_buf_node:         Link node to maintain a rb_tree of kbase_dma_buf.
 * @import_count:         The number of times the dma_buf was imported.
 */
struct kbase_dma_buf {
	struct dma_buf *dma_buf;
	struct rb_node dma_buf_node;
	u32 import_count;
};

/**
 * kbase_delete_dma_buf_mapping - Delete a dma buffer mapping.
 *
 * @kctx: Pointer to kbase context.
 * @dma_buf: Pointer to a dma buffer mapping.
 * @tree: Pointer to root of rb_tree containing the dma_buf's mapped.
 *
 * when we un-map any dma mapping we need to remove them from rb_tree,
 * rb_tree is maintained at kbase_device level and kbase_process level
 * by passing the root of kbase_device or kbase_process we can remove
 * the node from the tree.
 */
static bool kbase_delete_dma_buf_mapping(struct kbase_context *kctx,
					 struct dma_buf *dma_buf,
					 struct rb_root *tree)
{
	struct kbase_dma_buf *buf_node = NULL;
	struct rb_node *node = tree->rb_node;
	bool mapping_removed = false;

	lockdep_assert_held(&kctx->kbdev->dma_buf_lock);

	while (node) {
		buf_node = rb_entry(node, struct kbase_dma_buf, dma_buf_node);

		if (dma_buf == buf_node->dma_buf) {
			WARN_ON(!buf_node->import_count);

			buf_node->import_count--;

			if (!buf_node->import_count) {
				rb_erase(&buf_node->dma_buf_node, tree);
				kfree(buf_node);
				mapping_removed = true;
			}

			break;
		}

		if (dma_buf < buf_node->dma_buf)
			node = node->rb_left;
		else
			node = node->rb_right;
	}

	WARN_ON(!buf_node);
	return mapping_removed;
}

/**
 * kbase_capture_dma_buf_mapping - capture a dma buffer mapping.
 *
 * @kctx: Pointer to kbase context.
 * @dma_buf: Pointer to a dma buffer mapping.
 * @root: Pointer to root of rb_tree containing the dma_buf's.
 *
 * We maintain a kbase_device level and kbase_process level rb_tree
 * of all unique dma_buf's mapped to gpu memory. So when attach any
 * dma_buf add it the rb_tree's. To add the unique mapping we need
 * check if the mapping is not a duplicate and then add them.
 */
static bool kbase_capture_dma_buf_mapping(struct kbase_context *kctx,
					  struct dma_buf *dma_buf,
					  struct rb_root *root)
{
	struct kbase_dma_buf *buf_node = NULL;
	struct rb_node *node = root->rb_node;
	bool unique_buf_imported = true;

	lockdep_assert_held(&kctx->kbdev->dma_buf_lock);

	while (node) {
		buf_node = rb_entry(node, struct kbase_dma_buf, dma_buf_node);

		if (dma_buf == buf_node->dma_buf) {
			unique_buf_imported = false;
			break;
		}

		if (dma_buf < buf_node->dma_buf)
			node = node->rb_left;
		else
			node = node->rb_right;
	}

	if (unique_buf_imported) {
		struct kbase_dma_buf *buf_node =
			kzalloc(sizeof(*buf_node), GFP_KERNEL);

		if (buf_node == NULL) {
			dev_err(kctx->kbdev->dev, "Error allocating memory for kbase_dma_buf\n");
			/* Dont account for it if we fail to allocate memory */
			unique_buf_imported = false;
		} else {
			struct rb_node **new = &(root->rb_node), *parent = NULL;

			buf_node->dma_buf = dma_buf;
			buf_node->import_count = 1;
			while (*new) {
				struct kbase_dma_buf *node;

				parent = *new;
				node = rb_entry(parent, struct kbase_dma_buf,
						dma_buf_node);
				if (dma_buf < node->dma_buf)
					new = &(*new)->rb_left;
				else
					new = &(*new)->rb_right;
			}
			rb_link_node(&buf_node->dma_buf_node, parent, new);
			rb_insert_color(&buf_node->dma_buf_node, root);
		}
	} else if (!WARN_ON(!buf_node)) {
		buf_node->import_count++;
	}

	return unique_buf_imported;
}

void kbase_remove_dma_buf_usage(struct kbase_context *kctx,
				struct kbase_mem_phy_alloc *alloc)
{
	struct kbase_device *kbdev = kctx->kbdev;
	bool dev_mapping_removed, prcs_mapping_removed;

	mutex_lock(&kbdev->dma_buf_lock);

	dev_mapping_removed = kbase_delete_dma_buf_mapping(
		kctx, alloc->imported.umm.dma_buf, &kbdev->dma_buf_root);

	prcs_mapping_removed = kbase_delete_dma_buf_mapping(
		kctx, alloc->imported.umm.dma_buf, &kctx->kprcs->dma_buf_root);

	WARN_ON(dev_mapping_removed && !prcs_mapping_removed);

	spin_lock(&kbdev->gpu_mem_usage_lock);
	if (dev_mapping_removed)
		kbdev->total_gpu_pages -= alloc->nents;

	if (prcs_mapping_removed)
		kctx->kprcs->total_gpu_pages -= alloc->nents;

	if (dev_mapping_removed || prcs_mapping_removed)
		kbase_trace_gpu_mem_usage(kbdev, kctx);
	spin_unlock(&kbdev->gpu_mem_usage_lock);

	mutex_unlock(&kbdev->dma_buf_lock);
}

void kbase_add_dma_buf_usage(struct kbase_context *kctx,
				    struct kbase_mem_phy_alloc *alloc)
{
	struct kbase_device *kbdev = kctx->kbdev;
	bool unique_dev_dmabuf, unique_prcs_dmabuf;

	mutex_lock(&kbdev->dma_buf_lock);

	/* add dma_buf to device and process. */
	unique_dev_dmabuf = kbase_capture_dma_buf_mapping(
		kctx, alloc->imported.umm.dma_buf, &kbdev->dma_buf_root);

	unique_prcs_dmabuf = kbase_capture_dma_buf_mapping(
		kctx, alloc->imported.umm.dma_buf, &kctx->kprcs->dma_buf_root);

	WARN_ON(unique_dev_dmabuf && !unique_prcs_dmabuf);

	spin_lock(&kbdev->gpu_mem_usage_lock);
	if (unique_dev_dmabuf)
		kbdev->total_gpu_pages += alloc->nents;

	if (unique_prcs_dmabuf)
		kctx->kprcs->total_gpu_pages += alloc->nents;

	if (unique_prcs_dmabuf || unique_dev_dmabuf)
		kbase_trace_gpu_mem_usage(kbdev, kctx);
	spin_unlock(&kbdev->gpu_mem_usage_lock);

	mutex_unlock(&kbdev->dma_buf_lock);
}

#if !defined(CONFIG_TRACE_GPU_MEM) && !MALI_CUSTOMER_RELEASE
#define CREATE_TRACE_POINTS
#include "mali_gpu_mem_trace.h"
#endif
