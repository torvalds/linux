/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __MMU_H
#define __MMU_H

#include <linux/cpumask.h>
#include <linux/errno.h>
#include <asm/asm-extable.h>

typedef struct {
	spinlock_t lock;
	cpumask_t cpu_attach_mask;
	atomic_t flush_count;
	unsigned int flush_mm;
	struct list_head gmap_list;
	unsigned long gmap_asce;
	unsigned long asce;
	unsigned long asce_limit;
	unsigned long vdso_base;
	/* The mmu context belongs to a secure guest. */
	atomic_t protected_count;
	/*
	 * The following bitfields need a down_write on the mm
	 * semaphore when they are written to. As they are only
	 * written once, they can be read without a lock.
	 */
	/* The mmu context uses extended page tables. */
	unsigned int has_pgste:1;
	/* The mmu context uses storage keys. */
	unsigned int uses_skeys:1;
	/* The mmu context uses CMM. */
	unsigned int uses_cmm:1;
	/*
	 * The mmu context allows COW-sharing of memory pages (KSM, zeropage).
	 * Note that COW-sharing during fork() is currently always allowed.
	 */
	unsigned int allow_cow_sharing:1;
	/* The gmaps associated with this context are allowed to use huge pages. */
	unsigned int allow_gmap_hpage_1m:1;
} mm_context_t;

#define INIT_MM_CONTEXT(name)						   \
	.context.lock =	__SPIN_LOCK_UNLOCKED(name.context.lock),	   \
	.context.gmap_list = LIST_HEAD_INIT(name.context.gmap_list),

#endif
