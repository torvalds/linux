/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __MMU_H
#define __MMU_H

#include <linux/cpumask.h>
#include <linux/errno.h>

typedef struct {
	spinlock_t lock;
	cpumask_t cpu_attach_mask;
	atomic_t flush_count;
	unsigned int flush_mm;
	struct list_head pgtable_list;
	struct list_head gmap_list;
	unsigned long gmap_asce;
	unsigned long asce;
	unsigned long asce_limit;
	unsigned long vdso_base;
	/* The mmu context allocates 4K page tables. */
	unsigned int alloc_pgste:1;
	/* The mmu context uses extended page tables. */
	unsigned int has_pgste:1;
	/* The mmu context uses storage keys. */
	unsigned int use_skey:1;
	/* The mmu context uses CMMA. */
	unsigned int use_cmma:1;
} mm_context_t;

#define INIT_MM_CONTEXT(name)						   \
	.context.lock =	__SPIN_LOCK_UNLOCKED(name.context.lock),	   \
	.context.pgtable_list = LIST_HEAD_INIT(name.context.pgtable_list), \
	.context.gmap_list = LIST_HEAD_INIT(name.context.gmap_list),

static inline int tprot(unsigned long addr)
{
	int rc = -EFAULT;

	asm volatile(
		"	tprot	0(%1),0\n"
		"0:	ipm	%0\n"
		"	srl	%0,28\n"
		"1:\n"
		EX_TABLE(0b,1b)
		: "+d" (rc) : "a" (addr) : "cc");
	return rc;
}

#endif
