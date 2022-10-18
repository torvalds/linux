/* SPDX-License-Identifier: GPL-2.0 */
/**
 * Copyright(c) 2016-20 Intel Corporation.
 *
 * Contains the software defined data structures for enclaves.
 */
#ifndef _X86_ENCL_H
#define _X86_ENCL_H

#include <linux/cpumask.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/mm_types.h>
#include <linux/mmu_notifier.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/srcu.h>
#include <linux/workqueue.h>
#include <linux/xarray.h>
#include "sgx.h"

/* 'desc' bits holding the offset in the VA (version array) page. */
#define SGX_ENCL_PAGE_VA_OFFSET_MASK	GENMASK_ULL(11, 3)

/* 'desc' bit marking that the page is being reclaimed. */
#define SGX_ENCL_PAGE_BEING_RECLAIMED	BIT(3)

struct sgx_encl_page {
	unsigned long desc;
	unsigned long vm_max_prot_bits:8;
	enum sgx_page_type type:16;
	struct sgx_epc_page *epc_page;
	struct sgx_encl *encl;
	struct sgx_va_page *va_page;
};

enum sgx_encl_flags {
	SGX_ENCL_IOCTL		= BIT(0),
	SGX_ENCL_DEBUG		= BIT(1),
	SGX_ENCL_CREATED	= BIT(2),
	SGX_ENCL_INITIALIZED	= BIT(3),
};

struct sgx_encl_mm {
	struct sgx_encl *encl;
	struct mm_struct *mm;
	struct list_head list;
	struct mmu_notifier mmu_notifier;
};

struct sgx_encl {
	unsigned long base;
	unsigned long size;
	unsigned long flags;
	unsigned int page_cnt;
	unsigned int secs_child_cnt;
	struct mutex lock;
	struct xarray page_array;
	struct sgx_encl_page secs;
	unsigned long attributes;
	unsigned long attributes_mask;

	cpumask_t cpumask;
	struct file *backing;
	struct kref refcount;
	struct list_head va_pages;
	unsigned long mm_list_version;
	struct list_head mm_list;
	spinlock_t mm_lock;
	struct srcu_struct srcu;
};

#define SGX_VA_SLOT_COUNT 512

struct sgx_va_page {
	struct sgx_epc_page *epc_page;
	DECLARE_BITMAP(slots, SGX_VA_SLOT_COUNT);
	struct list_head list;
};

struct sgx_backing {
	struct page *contents;
	struct page *pcmd;
	unsigned long pcmd_offset;
};

extern const struct vm_operations_struct sgx_vm_ops;

static inline int sgx_encl_find(struct mm_struct *mm, unsigned long addr,
				struct vm_area_struct **vma)
{
	struct vm_area_struct *result;

	result = vma_lookup(mm, addr);
	if (!result || result->vm_ops != &sgx_vm_ops)
		return -EINVAL;

	*vma = result;

	return 0;
}

int sgx_encl_may_map(struct sgx_encl *encl, unsigned long start,
		     unsigned long end, unsigned long vm_flags);

bool current_is_ksgxd(void);
void sgx_encl_release(struct kref *ref);
int sgx_encl_mm_add(struct sgx_encl *encl, struct mm_struct *mm);
const cpumask_t *sgx_encl_cpumask(struct sgx_encl *encl);
int sgx_encl_alloc_backing(struct sgx_encl *encl, unsigned long page_index,
			   struct sgx_backing *backing);
void sgx_encl_put_backing(struct sgx_backing *backing);
int sgx_encl_test_and_clear_young(struct mm_struct *mm,
				  struct sgx_encl_page *page);
struct sgx_encl_page *sgx_encl_page_alloc(struct sgx_encl *encl,
					  unsigned long offset,
					  u64 secinfo_flags);
void sgx_zap_enclave_ptes(struct sgx_encl *encl, unsigned long addr);
struct sgx_epc_page *sgx_alloc_va_page(bool reclaim);
unsigned int sgx_alloc_va_slot(struct sgx_va_page *va_page);
void sgx_free_va_slot(struct sgx_va_page *va_page, unsigned int offset);
bool sgx_va_page_full(struct sgx_va_page *va_page);
void sgx_encl_free_epc_page(struct sgx_epc_page *page);
struct sgx_encl_page *sgx_encl_load_page(struct sgx_encl *encl,
					 unsigned long addr);
struct sgx_va_page *sgx_encl_grow(struct sgx_encl *encl, bool reclaim);
void sgx_encl_shrink(struct sgx_encl *encl, struct sgx_va_page *va_page);

#endif /* _X86_ENCL_H */
