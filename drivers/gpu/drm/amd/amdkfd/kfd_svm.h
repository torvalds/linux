/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/*
 * Copyright 2020-2021 Advanced Micro Devices, Inc.
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
 *
 */

#ifndef KFD_SVM_H_
#define KFD_SVM_H_

#include <linux/rwsem.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/sched/mm.h>
#include <linux/hmm.h>
#include "amdgpu.h"
#include "kfd_priv.h"

enum svm_work_list_ops {
	SVM_OP_NULL,
	SVM_OP_UNMAP_RANGE,
	SVM_OP_UPDATE_RANGE_NOTIFIER,
	SVM_OP_ADD_RANGE
};

struct svm_work_list_item {
	enum svm_work_list_ops op;
	struct mm_struct *mm;
};

/**
 * struct svm_range - shared virtual memory range
 *
 * @svms:       list of svm ranges, structure defined in kfd_process
 * @start:      range start address in pages
 * @last:       range last address in pages
 * @it_node:    node [start, last] stored in interval tree, start, last are page
 *              aligned, page size is (last - start + 1)
 * @list:       link list node, used to scan all ranges of svms
 * @update_list:link list node used to add to update_list
 * @remove_list:link list node used to add to remove list
 * @insert_list:link list node used to add to insert list
 * @mapping:    bo_va mapping structure to create and update GPU page table
 * @npages:     number of pages
 * @dma_addr:   dma mapping address on each GPU for system memory physical page
 * @lock:       protect prange start, last, child_list, svm_bo_list
 * @saved_flags:save/restore current PF_MEMALLOC flags
 * @flags:      flags defined as KFD_IOCTL_SVM_FLAG_*
 * @perferred_loc: perferred location, 0 for CPU, or GPU id
 * @perfetch_loc: last prefetch location, 0 for CPU, or GPU id
 * @actual_loc: the actual location, 0 for CPU, or GPU id
 * @granularity:migration granularity, log2 num pages
 * @invalid:    not 0 means cpu page table is invalidated
 * @notifier:   register mmu interval notifier
 * @work_item:  deferred work item information
 * @deferred_list: list header used to add range to deferred list
 * @child_list: list header for split ranges which are not added to svms yet
 * @bitmap_access: index bitmap of GPUs which can access the range
 * @bitmap_aip: index bitmap of GPUs which can access the range in place
 *
 * Data structure for virtual memory range shared by CPU and GPUs, it can be
 * allocated from system memory ram or device vram, and migrate from ram to vram
 * or from vram to ram.
 */
struct svm_range {
	struct svm_range_list		*svms;
	unsigned long			start;
	unsigned long			last;
	struct interval_tree_node	it_node;
	struct list_head		list;
	struct list_head		update_list;
	struct list_head		remove_list;
	struct list_head		insert_list;
	struct amdgpu_bo_va_mapping	mapping;
	uint64_t			npages;
	dma_addr_t			*dma_addr[MAX_GPU_INSTANCE];
	struct mutex                    lock;
	unsigned int                    saved_flags;
	uint32_t			flags;
	uint32_t			preferred_loc;
	uint32_t			prefetch_loc;
	uint32_t			actual_loc;
	uint8_t				granularity;
	atomic_t			invalid;
	struct mmu_interval_notifier	notifier;
	struct svm_work_list_item	work_item;
	struct list_head		deferred_list;
	struct list_head		child_list;
	DECLARE_BITMAP(bitmap_access, MAX_GPU_INSTANCE);
	DECLARE_BITMAP(bitmap_aip, MAX_GPU_INSTANCE);
};

static inline void svm_range_lock(struct svm_range *prange)
{
	mutex_lock(&prange->lock);
	prange->saved_flags = memalloc_noreclaim_save();

}
static inline void svm_range_unlock(struct svm_range *prange)
{
	memalloc_noreclaim_restore(prange->saved_flags);
	mutex_unlock(&prange->lock);
}

int svm_range_list_init(struct kfd_process *p);
void svm_range_list_fini(struct kfd_process *p);
int svm_ioctl(struct kfd_process *p, enum kfd_ioctl_svm_op op, uint64_t start,
	      uint64_t size, uint32_t nattrs,
	      struct kfd_ioctl_svm_attribute *attrs);

#endif /* KFD_SVM_H_ */
