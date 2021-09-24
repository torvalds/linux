// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 ARM Ltd.
 */

#include <linux/bitops.h>
#include <linux/cpu.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/prctl.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/string.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <linux/thread_info.h>
#include <linux/types.h>
#include <linux/uio.h>

#include <asm/barrier.h>
#include <asm/cpufeature.h>
#include <asm/mte.h>
#include <asm/ptrace.h>
#include <asm/sysreg.h>

static bool report_fault_once = true;

static DEFINE_PER_CPU_READ_MOSTLY(u64, mte_tcf_preferred);

#ifdef CONFIG_KASAN_HW_TAGS
/* Whether the MTE asynchronous mode is enabled. */
DEFINE_STATIC_KEY_FALSE(mte_async_mode);
EXPORT_SYMBOL_GPL(mte_async_mode);
#endif

static void mte_sync_page_tags(struct page *page, pte_t *ptep, bool check_swap)
{
	pte_t old_pte = READ_ONCE(*ptep);

	if (check_swap && is_swap_pte(old_pte)) {
		swp_entry_t entry = pte_to_swp_entry(old_pte);

		if (!non_swap_entry(entry) && mte_restore_tags(entry, page))
			return;
	}

	page_kasan_tag_reset(page);
	/*
	 * We need smp_wmb() in between setting the flags and clearing the
	 * tags because if another thread reads page->flags and builds a
	 * tagged address out of it, there is an actual dependency to the
	 * memory access, but on the current thread we do not guarantee that
	 * the new page->flags are visible before the tags were updated.
	 */
	smp_wmb();
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

static inline void __mte_enable_kernel(const char *mode, unsigned long tcf)
{
	/* Enable MTE Sync Mode for EL1. */
	sysreg_clear_set(sctlr_el1, SCTLR_ELx_TCF_MASK, tcf);
	isb();

	pr_info_once("MTE: enabled in %s mode at EL1\n", mode);
}

#ifdef CONFIG_KASAN_HW_TAGS
void mte_enable_kernel_sync(void)
{
	/*
	 * Make sure we enter this function when no PE has set
	 * async mode previously.
	 */
	WARN_ONCE(system_uses_mte_async_mode(),
			"MTE async mode enabled system wide!");

	__mte_enable_kernel("synchronous", SCTLR_ELx_TCF_SYNC);
}

void mte_enable_kernel_async(void)
{
	__mte_enable_kernel("asynchronous", SCTLR_ELx_TCF_ASYNC);

	/*
	 * MTE async mode is set system wide by the first PE that
	 * executes this function.
	 *
	 * Note: If in future KASAN acquires a runtime switching
	 * mode in between sync and async, this strategy needs
	 * to be reviewed.
	 */
	if (!system_uses_mte_async_mode())
		static_branch_enable(&mte_async_mode);
}
#endif

void mte_set_report_once(bool state)
{
	WRITE_ONCE(report_fault_once, state);
}

bool mte_report_once(void)
{
	return READ_ONCE(report_fault_once);
}

#ifdef CONFIG_KASAN_HW_TAGS
void mte_check_tfsr_el1(void)
{
	u64 tfsr_el1 = read_sysreg_s(SYS_TFSR_EL1);

	if (unlikely(tfsr_el1 & SYS_TFSR_EL1_TF1)) {
		/*
		 * Note: isb() is not required after this direct write
		 * because there is no indirect read subsequent to it
		 * (per ARM DDI 0487F.c table D13-1).
		 */
		write_sysreg_s(0, SYS_TFSR_EL1);

		kasan_report_async();
	}
}
#endif

static void mte_update_sctlr_user(struct task_struct *task)
{
	/*
	 * This must be called with preemption disabled and can only be called
	 * on the current or next task since the CPU must match where the thread
	 * is going to run. The caller is responsible for calling
	 * update_sctlr_el1() later in the same preemption disabled block.
	 */
	unsigned long sctlr = task->thread.sctlr_user;
	unsigned long mte_ctrl = task->thread.mte_ctrl;
	unsigned long pref, resolved_mte_tcf;

	pref = __this_cpu_read(mte_tcf_preferred);
	resolved_mte_tcf = (mte_ctrl & pref) ? pref : mte_ctrl;
	sctlr &= ~SCTLR_EL1_TCF0_MASK;
	if (resolved_mte_tcf & MTE_CTRL_TCF_ASYNC)
		sctlr |= SCTLR_EL1_TCF0_ASYNC;
	else if (resolved_mte_tcf & MTE_CTRL_TCF_SYNC)
		sctlr |= SCTLR_EL1_TCF0_SYNC;
	task->thread.sctlr_user = sctlr;
}

static void mte_update_gcr_excl(struct task_struct *task)
{
	/*
	 * SYS_GCR_EL1 will be set to current->thread.mte_ctrl value by
	 * mte_set_user_gcr() in kernel_exit, but only if KASAN is enabled.
	 */
	if (kasan_hw_tags_enabled())
		return;

	write_sysreg_s(
		((task->thread.mte_ctrl >> MTE_CTRL_GCR_USER_EXCL_SHIFT) &
		 SYS_GCR_EL1_EXCL_MASK) | SYS_GCR_EL1_RRND,
		SYS_GCR_EL1);
}

void __init kasan_hw_tags_enable(struct alt_instr *alt, __le32 *origptr,
				 __le32 *updptr, int nr_inst)
{
	BUG_ON(nr_inst != 1); /* Branch -> NOP */

	if (kasan_hw_tags_enabled())
		*updptr = cpu_to_le32(aarch64_insn_gen_nop());
}

void mte_thread_init_user(void)
{
	if (!system_supports_mte())
		return;

	/* clear any pending asynchronous tag fault */
	dsb(ish);
	write_sysreg_s(0, SYS_TFSRE0_EL1);
	clear_thread_flag(TIF_MTE_ASYNC_FAULT);
	/* disable tag checking and reset tag generation mask */
	set_mte_ctrl(current, 0);
}

void mte_thread_switch(struct task_struct *next)
{
	if (!system_supports_mte())
		return;

	mte_update_sctlr_user(next);
	mte_update_gcr_excl(next);

	/*
	 * Check if an async tag exception occurred at EL1.
	 *
	 * Note: On the context switch path we rely on the dsb() present
	 * in __switch_to() to guarantee that the indirect writes to TFSR_EL1
	 * are synchronized before this point.
	 */
	isb();
	mte_check_tfsr_el1();
}

void mte_suspend_enter(void)
{
	if (!system_supports_mte())
		return;

	/*
	 * The barriers are required to guarantee that the indirect writes
	 * to TFSR_EL1 are synchronized before we report the state.
	 */
	dsb(nsh);
	isb();

	/* Report SYS_TFSR_EL1 before suspend entry */
	mte_check_tfsr_el1();
}

long set_mte_ctrl(struct task_struct *task, unsigned long arg)
{
	u64 mte_ctrl = (~((arg & PR_MTE_TAG_MASK) >> PR_MTE_TAG_SHIFT) &
			SYS_GCR_EL1_EXCL_MASK) << MTE_CTRL_GCR_USER_EXCL_SHIFT;

	if (!system_supports_mte())
		return 0;

	if (arg & PR_MTE_TCF_ASYNC)
		mte_ctrl |= MTE_CTRL_TCF_ASYNC;
	if (arg & PR_MTE_TCF_SYNC)
		mte_ctrl |= MTE_CTRL_TCF_SYNC;

	task->thread.mte_ctrl = mte_ctrl;
	if (task == current) {
		preempt_disable();
		mte_update_sctlr_user(task);
		mte_update_gcr_excl(task);
		update_sctlr_el1(task->thread.sctlr_user);
		preempt_enable();
	}

	return 0;
}

long get_mte_ctrl(struct task_struct *task)
{
	unsigned long ret;
	u64 mte_ctrl = task->thread.mte_ctrl;
	u64 incl = (~mte_ctrl >> MTE_CTRL_GCR_USER_EXCL_SHIFT) &
		   SYS_GCR_EL1_EXCL_MASK;

	if (!system_supports_mte())
		return 0;

	ret = incl << PR_MTE_TAG_SHIFT;
	if (mte_ctrl & MTE_CTRL_TCF_ASYNC)
		ret |= PR_MTE_TCF_ASYNC;
	if (mte_ctrl & MTE_CTRL_TCF_SYNC)
		ret |= PR_MTE_TCF_SYNC;

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
		if (!(vma->vm_flags & VM_MTE)) {
			ret = -EOPNOTSUPP;
			put_page(page);
			break;
		}
		WARN_ON_ONCE(!test_bit(PG_mte_tagged, &page->flags));

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

static ssize_t mte_tcf_preferred_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	switch (per_cpu(mte_tcf_preferred, dev->id)) {
	case MTE_CTRL_TCF_ASYNC:
		return sysfs_emit(buf, "async\n");
	case MTE_CTRL_TCF_SYNC:
		return sysfs_emit(buf, "sync\n");
	default:
		return sysfs_emit(buf, "???\n");
	}
}

static ssize_t mte_tcf_preferred_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
	u64 tcf;

	if (sysfs_streq(buf, "async"))
		tcf = MTE_CTRL_TCF_ASYNC;
	else if (sysfs_streq(buf, "sync"))
		tcf = MTE_CTRL_TCF_SYNC;
	else
		return -EINVAL;

	device_lock(dev);
	per_cpu(mte_tcf_preferred, dev->id) = tcf;
	device_unlock(dev);

	return count;
}
static DEVICE_ATTR_RW(mte_tcf_preferred);

static int register_mte_tcf_preferred_sysctl(void)
{
	unsigned int cpu;

	if (!system_supports_mte())
		return 0;

	for_each_possible_cpu(cpu) {
		per_cpu(mte_tcf_preferred, cpu) = MTE_CTRL_TCF_ASYNC;
		device_create_file(get_cpu_device(cpu),
				   &dev_attr_mte_tcf_preferred);
	}

	return 0;
}
subsys_initcall(register_mte_tcf_preferred_sysctl);
