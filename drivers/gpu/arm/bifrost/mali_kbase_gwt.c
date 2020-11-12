/*
 *
 * (C) COPYRIGHT 2010-2020 ARM Limited. All rights reserved.
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

#include "mali_kbase_gwt.h"
#include <linux/list_sort.h>

static inline void kbase_gpu_gwt_setup_page_permission(
				struct kbase_context *kctx,
				unsigned long flag,
				struct rb_node *node)
{
	struct rb_node *rbnode = node;

	while (rbnode) {
		struct kbase_va_region *reg;
		int err = 0;

		reg = rb_entry(rbnode, struct kbase_va_region, rblink);
		if (reg->nr_pages && !kbase_is_region_invalid_or_free(reg) &&
					(reg->flags & KBASE_REG_GPU_WR)) {
			err = kbase_mmu_update_pages(kctx, reg->start_pfn,
					kbase_get_gpu_phy_pages(reg),
					reg->gpu_alloc->nents,
					reg->flags & flag,
					reg->gpu_alloc->group_id);
			if (err)
				dev_warn(kctx->kbdev->dev, "kbase_mmu_update_pages failure\n");
		}

		rbnode = rb_next(rbnode);
	}
}

static void kbase_gpu_gwt_setup_pages(struct kbase_context *kctx,
					unsigned long flag)
{
	kbase_gpu_gwt_setup_page_permission(kctx, flag,
				rb_first(&(kctx->reg_rbtree_same)));
	kbase_gpu_gwt_setup_page_permission(kctx, flag,
				rb_first(&(kctx->reg_rbtree_custom)));
}


int kbase_gpu_gwt_start(struct kbase_context *kctx)
{
	kbase_gpu_vm_lock(kctx);
	if (kctx->gwt_enabled) {
		kbase_gpu_vm_unlock(kctx);
		return -EBUSY;
	}

	INIT_LIST_HEAD(&kctx->gwt_current_list);
	INIT_LIST_HEAD(&kctx->gwt_snapshot_list);

#if !MALI_USE_CSF
	/* If GWT is enabled using new vector dumping format
	 * from user space, back up status of the job serialization flag and
	 * use full serialisation of jobs for dumping.
	 * Status will be restored on end of dumping in gwt_stop.
	 */
	kctx->kbdev->backup_serialize_jobs = kctx->kbdev->serialize_jobs;
	kctx->kbdev->serialize_jobs = KBASE_SERIALIZE_INTRA_SLOT |
						KBASE_SERIALIZE_INTER_SLOT;

#endif
	/* Mark gwt enabled before making pages read only in case a
	   write page fault is triggered while we're still in this loop.
	   (kbase_gpu_vm_lock() doesn't prevent this!)
	*/
	kctx->gwt_enabled = true;
	kctx->gwt_was_enabled = true;

	kbase_gpu_gwt_setup_pages(kctx, ~KBASE_REG_GPU_WR);

	kbase_gpu_vm_unlock(kctx);
	return 0;
}

int kbase_gpu_gwt_stop(struct kbase_context *kctx)
{
	struct kbasep_gwt_list_element *pos, *n;

	kbase_gpu_vm_lock(kctx);
	if (!kctx->gwt_enabled) {
		kbase_gpu_vm_unlock(kctx);
		return -EINVAL;
	}

	list_for_each_entry_safe(pos, n, &kctx->gwt_current_list, link) {
		list_del(&pos->link);
		kfree(pos);
	}

	list_for_each_entry_safe(pos, n, &kctx->gwt_snapshot_list, link) {
		list_del(&pos->link);
		kfree(pos);
	}

#if !MALI_USE_CSF
	kctx->kbdev->serialize_jobs = kctx->kbdev->backup_serialize_jobs;
#endif

	kbase_gpu_gwt_setup_pages(kctx, ~0UL);

	kctx->gwt_enabled = false;
	kbase_gpu_vm_unlock(kctx);
	return 0;
}


static int list_cmp_function(void *priv, struct list_head *a,
				struct list_head *b)
{
	struct kbasep_gwt_list_element *elementA = container_of(a,
				struct kbasep_gwt_list_element, link);
	struct kbasep_gwt_list_element *elementB = container_of(b,
				struct kbasep_gwt_list_element, link);

	CSTD_UNUSED(priv);

	if (elementA->page_addr > elementB->page_addr)
		return 1;
	return -1;
}

static void kbase_gpu_gwt_collate(struct kbase_context *kctx,
		struct list_head *snapshot_list)
{
	struct kbasep_gwt_list_element *pos, *n;
	struct kbasep_gwt_list_element *collated = NULL;

	/* Sort the list */
	list_sort(NULL, snapshot_list, list_cmp_function);

	/* Combine contiguous areas. */
	list_for_each_entry_safe(pos, n, snapshot_list, link) {
		if (collated == NULL ||	collated->region !=
					pos->region ||
					(collated->page_addr +
					(collated->num_pages * PAGE_SIZE)) !=
					pos->page_addr) {
			/* This is the first time through, a new region or
			 * is not contiguous - start collating to this element
			 */
			collated = pos;
		} else {
			/* contiguous so merge */
			collated->num_pages += pos->num_pages;
			/* remove element from list */
			list_del(&pos->link);
			kfree(pos);
		}
	}
}

int kbase_gpu_gwt_dump(struct kbase_context *kctx,
			union kbase_ioctl_cinstr_gwt_dump *gwt_dump)
{
	const u32 ubuf_size = gwt_dump->in.len;
	u32 ubuf_count = 0;
	__user void *user_addr = (__user void *)
			(uintptr_t)gwt_dump->in.addr_buffer;
	__user void *user_sizes = (__user void *)
			(uintptr_t)gwt_dump->in.size_buffer;

	kbase_gpu_vm_lock(kctx);

	if (!kctx->gwt_enabled) {
		kbase_gpu_vm_unlock(kctx);
		/* gwt_dump shouldn't be called when gwt is disabled */
		return -EPERM;
	}

	if (!gwt_dump->in.len || !gwt_dump->in.addr_buffer
			|| !gwt_dump->in.size_buffer) {
		kbase_gpu_vm_unlock(kctx);
		/* We don't have any valid user space buffer to copy the
		 * write modified addresses.
		 */
		return -EINVAL;
	}

	if (list_empty(&kctx->gwt_snapshot_list) &&
			!list_empty(&kctx->gwt_current_list)) {

		list_replace_init(&kctx->gwt_current_list,
					&kctx->gwt_snapshot_list);

		/* We have collected all write faults so far
		 * and they will be passed on to user space.
		 * Reset the page flags state to allow collection of
		 * further write faults.
		 */
		kbase_gpu_gwt_setup_pages(kctx, ~KBASE_REG_GPU_WR);

		/* Sort and combine consecutive pages in the dump list*/
		kbase_gpu_gwt_collate(kctx, &kctx->gwt_snapshot_list);
	}

	while ((!list_empty(&kctx->gwt_snapshot_list))) {
		u64 addr_buffer[32];
		u64 num_page_buffer[32];
		u32 count = 0;
		int err;
		struct kbasep_gwt_list_element *dump_info, *n;

		list_for_each_entry_safe(dump_info, n,
				&kctx->gwt_snapshot_list, link) {
			addr_buffer[count] = dump_info->page_addr;
			num_page_buffer[count] = dump_info->num_pages;
			count++;
			list_del(&dump_info->link);
			kfree(dump_info);
			if (ARRAY_SIZE(addr_buffer) == count ||
					ubuf_size == (ubuf_count + count))
				break;
		}

		if (count) {
			err = copy_to_user((user_addr +
					(ubuf_count * sizeof(u64))),
					(void *)addr_buffer,
					count * sizeof(u64));
			if (err) {
				dev_err(kctx->kbdev->dev, "Copy to user failure\n");
				kbase_gpu_vm_unlock(kctx);
				return err;
			}
			err = copy_to_user((user_sizes +
					(ubuf_count * sizeof(u64))),
					(void *)num_page_buffer,
					count * sizeof(u64));
			if (err) {
				dev_err(kctx->kbdev->dev, "Copy to user failure\n");
				kbase_gpu_vm_unlock(kctx);
				return err;
			}

			ubuf_count += count;
		}

		if (ubuf_count == ubuf_size)
			break;
	}

	if (!list_empty(&kctx->gwt_snapshot_list))
		gwt_dump->out.more_data_available = 1;
	else
		gwt_dump->out.more_data_available = 0;

	gwt_dump->out.no_of_addr_collected = ubuf_count;
	kbase_gpu_vm_unlock(kctx);
	return 0;
}
