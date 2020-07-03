// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 ARM Ltd.
 */

#include <linux/bitops.h>
#include <linux/mm.h>
#include <linux/prctl.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/thread_info.h>

#include <asm/cpufeature.h>
#include <asm/mte.h>
#include <asm/sysreg.h>

void mte_sync_tags(pte_t *ptep, pte_t pte)
{
	struct page *page = pte_page(pte);
	long i, nr_pages = compound_nr(page);

	/* if PG_mte_tagged is set, tags have already been initialised */
	for (i = 0; i < nr_pages; i++, page++) {
		if (!test_and_set_bit(PG_mte_tagged, &page->flags))
			mte_clear_page_tags(page_address(page));
	}
}

int memcmp_pages(struct page *page1, struct page *page2)
{
	char *addr1, *addr2;
	int ret;

	addr1 = page_address(page1);
	addr2 = page_address(page2);
	ret = memcmp(addr1, addr2, PAGE_SIZE);

	if (!system_supports_mte() || ret)
		return ret;

	/*
	 * If the page content is identical but at least one of the pages is
	 * tagged, return non-zero to avoid KSM merging. If only one of the
	 * pages is tagged, set_pte_at() may zero or change the tags of the
	 * other page via mte_sync_tags().
	 */
	if (test_bit(PG_mte_tagged, &page1->flags) ||
	    test_bit(PG_mte_tagged, &page2->flags))
		return addr1 != addr2;

	return ret;
}

static void update_sctlr_el1_tcf0(u64 tcf0)
{
	/* ISB required for the kernel uaccess routines */
	sysreg_clear_set(sctlr_el1, SCTLR_EL1_TCF0_MASK, tcf0);
	isb();
}

static void set_sctlr_el1_tcf0(u64 tcf0)
{
	/*
	 * mte_thread_switch() checks current->thread.sctlr_tcf0 as an
	 * optimisation. Disable preemption so that it does not see
	 * the variable update before the SCTLR_EL1.TCF0 one.
	 */
	preempt_disable();
	current->thread.sctlr_tcf0 = tcf0;
	update_sctlr_el1_tcf0(tcf0);
	preempt_enable();
}

static void update_gcr_el1_excl(u64 incl)
{
	u64 excl = ~incl & SYS_GCR_EL1_EXCL_MASK;

	/*
	 * Note that 'incl' is an include mask (controlled by the user via
	 * prctl()) while GCR_EL1 accepts an exclude mask.
	 * No need for ISB since this only affects EL0 currently, implicit
	 * with ERET.
	 */
	sysreg_clear_set_s(SYS_GCR_EL1, SYS_GCR_EL1_EXCL_MASK, excl);
}

static void set_gcr_el1_excl(u64 incl)
{
	current->thread.gcr_user_incl = incl;
	update_gcr_el1_excl(incl);
}

void flush_mte_state(void)
{
	if (!system_supports_mte())
		return;

	/* clear any pending asynchronous tag fault */
	dsb(ish);
	write_sysreg_s(0, SYS_TFSRE0_EL1);
	clear_thread_flag(TIF_MTE_ASYNC_FAULT);
	/* disable tag checking */
	set_sctlr_el1_tcf0(SCTLR_EL1_TCF0_NONE);
	/* reset tag generation mask */
	set_gcr_el1_excl(0);
}

void mte_thread_switch(struct task_struct *next)
{
	if (!system_supports_mte())
		return;

	/* avoid expensive SCTLR_EL1 accesses if no change */
	if (current->thread.sctlr_tcf0 != next->thread.sctlr_tcf0)
		update_sctlr_el1_tcf0(next->thread.sctlr_tcf0);
	update_gcr_el1_excl(next->thread.gcr_user_incl);
}

void mte_suspend_exit(void)
{
	if (!system_supports_mte())
		return;

	update_gcr_el1_excl(current->thread.gcr_user_incl);
}

long set_mte_ctrl(struct task_struct *task, unsigned long arg)
{
	u64 tcf0;
	u64 gcr_incl = (arg & PR_MTE_TAG_MASK) >> PR_MTE_TAG_SHIFT;

	if (!system_supports_mte())
		return 0;

	switch (arg & PR_MTE_TCF_MASK) {
	case PR_MTE_TCF_NONE:
		tcf0 = SCTLR_EL1_TCF0_NONE;
		break;
	case PR_MTE_TCF_SYNC:
		tcf0 = SCTLR_EL1_TCF0_SYNC;
		break;
	case PR_MTE_TCF_ASYNC:
		tcf0 = SCTLR_EL1_TCF0_ASYNC;
		break;
	default:
		return -EINVAL;
	}

	if (task != current) {
		task->thread.sctlr_tcf0 = tcf0;
		task->thread.gcr_user_incl = gcr_incl;
	} else {
		set_sctlr_el1_tcf0(tcf0);
		set_gcr_el1_excl(gcr_incl);
	}

	return 0;
}

long get_mte_ctrl(struct task_struct *task)
{
	unsigned long ret;

	if (!system_supports_mte())
		return 0;

	ret = task->thread.gcr_user_incl << PR_MTE_TAG_SHIFT;

	switch (task->thread.sctlr_tcf0) {
	case SCTLR_EL1_TCF0_NONE:
		return PR_MTE_TCF_NONE;
	case SCTLR_EL1_TCF0_SYNC:
		ret |= PR_MTE_TCF_SYNC;
		break;
	case SCTLR_EL1_TCF0_ASYNC:
		ret |= PR_MTE_TCF_ASYNC;
		break;
	}

	return ret;
}
