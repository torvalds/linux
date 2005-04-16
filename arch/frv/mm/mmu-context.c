/* mmu-context.c: MMU context allocation and management
 *
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/sched.h>
#include <linux/mm.h>
#include <asm/tlbflush.h>

#define NR_CXN	4096

static unsigned long cxn_bitmap[NR_CXN / (sizeof(unsigned long) * 8)];
static LIST_HEAD(cxn_owners_lru);
static DEFINE_SPINLOCK(cxn_owners_lock);

int __nongpreldata cxn_pinned = -1;


/*****************************************************************************/
/*
 * initialise a new context
 */
int init_new_context(struct task_struct *tsk, struct mm_struct *mm)
{
	memset(&mm->context, 0, sizeof(mm->context));
	INIT_LIST_HEAD(&mm->context.id_link);
	mm->context.itlb_cached_pge = 0xffffffffUL;
	mm->context.dtlb_cached_pge = 0xffffffffUL;

	return 0;
} /* end init_new_context() */

/*****************************************************************************/
/*
 * make sure a kernel MMU context has a CPU context number
 * - call with cxn_owners_lock held
 */
static unsigned get_cxn(mm_context_t *ctx)
{
	struct list_head *_p;
	mm_context_t *p;
	unsigned cxn;

	if (!list_empty(&ctx->id_link)) {
		list_move_tail(&ctx->id_link, &cxn_owners_lru);
	}
	else {
		/* find the first unallocated context number
		 * - 0 is reserved for the kernel
		 */
		cxn = find_next_zero_bit(&cxn_bitmap, NR_CXN, 1);
		if (cxn < NR_CXN) {
			set_bit(cxn, &cxn_bitmap);
		}
		else {
			/* none remaining - need to steal someone else's cxn */
			p = NULL;
			list_for_each(_p, &cxn_owners_lru) {
				p = list_entry(_p, mm_context_t, id_link);
				if (!p->id_busy && p->id != cxn_pinned)
					break;
			}

			BUG_ON(_p == &cxn_owners_lru);

			cxn = p->id;
			p->id = 0;
			list_del_init(&p->id_link);
			__flush_tlb_mm(cxn);
		}

		ctx->id = cxn;
		list_add_tail(&ctx->id_link, &cxn_owners_lru);
	}

	return ctx->id;
} /* end get_cxn() */

/*****************************************************************************/
/*
 * restore the current TLB miss handler mapped page tables into the MMU context and set up a
 * mapping for the page directory
 */
void change_mm_context(mm_context_t *old, mm_context_t *ctx, pgd_t *pgd)
{
	unsigned long _pgd;

	_pgd = virt_to_phys(pgd);

	/* save the state of the outgoing MMU context */
	old->id_busy = 0;

	asm volatile("movsg scr0,%0"   : "=r"(old->itlb_cached_pge));
	asm volatile("movsg dampr4,%0" : "=r"(old->itlb_ptd_mapping));
	asm volatile("movsg scr1,%0"   : "=r"(old->dtlb_cached_pge));
	asm volatile("movsg dampr5,%0" : "=r"(old->dtlb_ptd_mapping));

	/* select an MMU context number */
	spin_lock(&cxn_owners_lock);
	get_cxn(ctx);
	ctx->id_busy = 1;
	spin_unlock(&cxn_owners_lock);

	asm volatile("movgs %0,cxnr"   : : "r"(ctx->id));

	/* restore the state of the incoming MMU context */
	asm volatile("movgs %0,scr0"   : : "r"(ctx->itlb_cached_pge));
	asm volatile("movgs %0,dampr4" : : "r"(ctx->itlb_ptd_mapping));
	asm volatile("movgs %0,scr1"   : : "r"(ctx->dtlb_cached_pge));
	asm volatile("movgs %0,dampr5" : : "r"(ctx->dtlb_ptd_mapping));

	/* map the PGD into uncached virtual memory */
	asm volatile("movgs %0,ttbr"   : : "r"(_pgd));
	asm volatile("movgs %0,dampr3"
		     :: "r"(_pgd | xAMPRx_L | xAMPRx_M | xAMPRx_SS_16Kb |
			    xAMPRx_S | xAMPRx_C | xAMPRx_V));

} /* end change_mm_context() */

/*****************************************************************************/
/*
 * finished with an MMU context number
 */
void destroy_context(struct mm_struct *mm)
{
	mm_context_t *ctx = &mm->context;

	spin_lock(&cxn_owners_lock);

	if (!list_empty(&ctx->id_link)) {
		if (ctx->id == cxn_pinned)
			cxn_pinned = -1;

		list_del_init(&ctx->id_link);
		clear_bit(ctx->id, &cxn_bitmap);
		__flush_tlb_mm(ctx->id);
		ctx->id = 0;
	}

	spin_unlock(&cxn_owners_lock);
} /* end destroy_context() */

/*****************************************************************************/
/*
 * display the MMU context currently a process is currently using
 */
#ifdef CONFIG_PROC_FS
char *proc_pid_status_frv_cxnr(struct mm_struct *mm, char *buffer)
{
	spin_lock(&cxn_owners_lock);
	buffer += sprintf(buffer, "CXNR: %u\n", mm->context.id);
	spin_unlock(&cxn_owners_lock);

	return buffer;
} /* end proc_pid_status_frv_cxnr() */
#endif

/*****************************************************************************/
/*
 * (un)pin a process's mm_struct's MMU context ID
 */
int cxn_pin_by_pid(pid_t pid)
{
	struct task_struct *tsk;
	struct mm_struct *mm = NULL;
	int ret;

	/* unpin if pid is zero */
	if (pid == 0) {
		cxn_pinned = -1;
		return 0;
	}

	ret = -ESRCH;

	/* get a handle on the mm_struct */
	read_lock(&tasklist_lock);
	tsk = find_task_by_pid(pid);
	if (tsk) {
		ret = -EINVAL;

		task_lock(tsk);
		if (tsk->mm) {
			mm = tsk->mm;
			atomic_inc(&mm->mm_users);
			ret = 0;
		}
		task_unlock(tsk);
	}
	read_unlock(&tasklist_lock);

	if (ret < 0)
		return ret;

	/* make sure it has a CXN and pin it */
	spin_lock(&cxn_owners_lock);
	cxn_pinned = get_cxn(&mm->context);
	spin_unlock(&cxn_owners_lock);

	mmput(mm);
	return 0;
} /* end cxn_pin_by_pid() */
