/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2021-2022, NVIDIA CORPORATION & AFFILIATES.
 *
 */
#ifndef __IO_PAGETABLE_H
#define __IO_PAGETABLE_H

#include <linux/interval_tree.h>
#include <linux/mutex.h>
#include <linux/kref.h>
#include <linux/xarray.h>

#include "iommufd_private.h"

struct iommu_domain;

/*
 * Each io_pagetable is composed of intervals of areas which cover regions of
 * the iova that are backed by something. iova not covered by areas is not
 * populated in the page table. Each area is fully populated with pages.
 *
 * iovas are in byte units, but must be iopt->iova_alignment aligned.
 *
 * pages can be NULL, this means some other thread is still working on setting
 * up or tearing down the area. When observed under the write side of the
 * domain_rwsem a NULL pages must mean the area is still being setup and no
 * domains are filled.
 *
 * storage_domain points at an arbitrary iommu_domain that is holding the PFNs
 * for this area. It is locked by the pages->mutex. This simplifies the locking
 * as the pages code can rely on the storage_domain without having to get the
 * iopt->domains_rwsem.
 *
 * The io_pagetable::iova_rwsem protects node
 * The iopt_pages::mutex protects pages_node
 * iopt and immu_prot are immutable
 * The pages::mutex protects num_accesses
 */
struct iopt_area {
	struct interval_tree_node node;
	struct interval_tree_node pages_node;
	struct io_pagetable *iopt;
	struct iopt_pages *pages;
	struct iommu_domain *storage_domain;
	/* How many bytes into the first page the area starts */
	unsigned int page_offset;
	/* IOMMU_READ, IOMMU_WRITE, etc */
	int iommu_prot;
	unsigned int num_accesses;
};

static inline unsigned long iopt_area_index(struct iopt_area *area)
{
	return area->pages_node.start;
}

static inline unsigned long iopt_area_last_index(struct iopt_area *area)
{
	return area->pages_node.last;
}

static inline unsigned long iopt_area_iova(struct iopt_area *area)
{
	return area->node.start;
}

static inline unsigned long iopt_area_last_iova(struct iopt_area *area)
{
	return area->node.last;
}

enum {
	IOPT_PAGES_ACCOUNT_NONE = 0,
	IOPT_PAGES_ACCOUNT_USER = 1,
	IOPT_PAGES_ACCOUNT_MM = 2,
};

/*
 * This holds a pinned page list for multiple areas of IO address space. The
 * pages always originate from a linear chunk of userspace VA. Multiple
 * io_pagetable's, through their iopt_area's, can share a single iopt_pages
 * which avoids multi-pinning and double accounting of page consumption.
 *
 * indexes in this structure are measured in PAGE_SIZE units, are 0 based from
 * the start of the uptr and extend to npages. pages are pinned dynamically
 * according to the intervals in the access_itree and domains_itree, npinned
 * records the current number of pages pinned.
 */
struct iopt_pages {
	struct kref kref;
	struct mutex mutex;
	size_t npages;
	size_t npinned;
	size_t last_npinned;
	struct task_struct *source_task;
	struct mm_struct *source_mm;
	struct user_struct *source_user;
	void __user *uptr;
	bool writable:1;
	u8 account_mode;

	struct xarray pinned_pfns;
	/* Of iopt_pages_access::node */
	struct rb_root_cached access_itree;
	/* Of iopt_area::pages_node */
	struct rb_root_cached domains_itree;
};

#endif
