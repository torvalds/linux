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

struct sgx_encl_page {
	unsigned long desc;
	unsigned long vm_max_prot_bits;
	struct sgx_epc_page *epc_page;
	struct sgx_encl *encl;
};

enum sgx_encl_flags {
	SGX_ENCL_IOCTL		= BIT(0),
	SGX_ENCL_DEBUG		= BIT(1),
	SGX_ENCL_CREATED	= BIT(2),
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
};

extern const struct vm_operations_struct sgx_vm_ops;

static inline int sgx_encl_find(struct mm_struct *mm, unsigned long addr,
				struct vm_area_struct **vma)
{
	struct vm_area_struct *result;

	result = find_vma(mm, addr);
	if (!result || result->vm_ops != &sgx_vm_ops || addr < result->vm_start)
		return -EINVAL;

	*vma = result;

	return 0;
}

int sgx_encl_may_map(struct sgx_encl *encl, unsigned long start,
		     unsigned long end, unsigned long vm_flags);

#endif /* _X86_ENCL_H */
