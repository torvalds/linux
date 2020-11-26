// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 ARM Ltd.
 */

#include <linux/bitops.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/prctl.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/string.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <linux/thread_info.h>
#include <linux/uio.h>

#include <asm/cpufeature.h>
#include <asm/mte.h>
#include <asm/ptrace.h>
#include <asm/sysreg.h>

static void mte_sync_page_tags(struct page *page, pte_t *ptep, bool check_swap)
{
	pte_t old_pte = READ_ONCE(*ptep);

	if (check_swap && is_swap_pte(old_pte)) {
		swp_entry_t entry = pte_to_swp_entry(old_pte);

		if (!non_swap_entry(entry) && mte_restore_tags(entry, page))
			return;
	}

	mte_clear_page_tags(page_address(page));
}

void mte_sync_tags(pte_t *ptep, pte_t pte)
{
	struct page *page = pte_page(pte);
	long i, nr_pages = compound_nr(page);
	bool check_swap = nr_pages == 1;

	/* if PG_mte_tagged is set, tags have already been initialised */
	for (i = 0; i < nr_pages; i++, page++) {
		if (!test_and_set_bit(PG_mte_tagged, &page->flags))
			mte_sync_page_tags(page, ptep, check_swap);
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

/*
 * Access MTE tags in another process' address space as given in mm. Update
 * the number of tags copied. Return 0 if any tags copied, error otherwise.
 * Inspired by __access_remote_vm().
 */
static int __access_remote_tags(struct mm_struct *mm, unsigned long addr,
				struct iovec *kiov, unsigned int gup_flags)
{
	struct vm_area_struct *vma;
	void __user *buf = kiov->iov_base;
	size_t len = kiov->iov_len;
	int ret;
	int write = gup_flags & FOLL_WRITE;

	if (!access_ok(buf, len))
		return -EFAULT;

	if (mmap_read_lock_killable(mm))
		return -EIO;

	while (len) {
		unsigned long tags, offset;
		void *maddr;
		struct page *page = NULL;

		ret = get_user_pages_remote(mm, addr, 1, gup_flags, &page,
					    &vma, NULL);
		if (ret <= 0)
			break;

		/*
		 * Only copy tags if the page has been mapped as PROT_MTE
		 * (PG_mte_tagged set). Otherwise the tags are not valid and
		 * not accessible to user. Moreover, an mprotect(PROT_MTE)
		 * would cause the existing tags to be cleared if the page
		 * was never mapped with PROT_MTE.
		 */
		if (!test_bit(PG_mte_tagged, &page->flags)) {
			ret = -EOPNOTSUPP;
			put_page(page);
			break;
		}

		/* limit access to the end of the page */
		offset = offset_in_page(addr);
		tags = min(len, (PAGE_SIZE - offset) / MTE_GRANULE_SIZE);

		maddr = page_address(page);
		if (write) {
			tags = mte_copy_tags_from_user(maddr + offset, buf, tags);
			set_page_dirty_lock(page);
		} else {
			tags = mte_copy_tags_to_user(buf, maddr + offset, tags);
		}
		put_page(page);

		/* error accessing the tracer's buffer */
		if (!tags)
			break;

		len -= tags;
		buf += tags;
		addr += tags * MTE_GRANULE_SIZE;
	}
	mmap_read_unlock(mm);

	/* return an error if no tags copied */
	kiov->iov_len = buf - kiov->iov_base;
	if (!kiov->iov_len) {
		/* check for error accessing the tracee's address space */
		if (ret <= 0)
			return -EIO;
		else
			return -EFAULT;
	}

	return 0;
}

/*
 * Copy MTE tags in another process' address space at 'addr' to/from tracer's
 * iovec buffer. Return 0 on success. Inspired by ptrace_access_vm().
 */
static int access_remote_tags(struct task_struct *tsk, unsigned long addr,
			      struct iovec *kiov, unsigned int gup_flags)
{
	struct mm_struct *mm;
	int ret;

	mm = get_task_mm(tsk);
	if (!mm)
		return -EPERM;

	if (!tsk->ptrace || (current != tsk->parent) ||
	    ((get_dumpable(mm) != SUID_DUMP_USER) &&
	     !ptracer_capable(tsk, mm->user_ns))) {
		mmput(mm);
		return -EPERM;
	}

	ret = __access_remote_tags(mm, addr, kiov, gup_flags);
	mmput(mm);

	return ret;
}

int mte_ptrace_copy_tags(struct task_struct *child, long request,
			 unsigned long addr, unsigned long data)
{
	int ret;
	struct iovec kiov;
	struct iovec __user *uiov = (void __user *)data;
	unsigned int gup_flags = FOLL_FORCE;

	if (!system_supports_mte())
		return -EIO;

	if (get_user(kiov.iov_base, &uiov->iov_base) ||
	    get_user(kiov.iov_len, &uiov->iov_len))
		return -EFAULT;

	if (request == PTRACE_POKEMTETAGS)
		gup_flags |= FOLL_WRITE;

	/* align addr to the MTE tag granule */
	addr &= MTE_GRANULE_MASK;

	ret = access_remote_tags(child, addr, &kiov, gup_flags);
	if (!ret)
		ret = put_user(kiov.iov_len, &uiov->iov_len);

	return ret;
}
