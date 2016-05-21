/*
 * Performance counter callchain support - powerpc architecture code
 *
 * Copyright © 2009 Paul Mackerras, IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/perf_event.h>
#include <linux/percpu.h>
#include <linux/uaccess.h>
#include <linux/mm.h>
#include <asm/ptrace.h>
#include <asm/pgtable.h>
#include <asm/sigcontext.h>
#include <asm/ucontext.h>
#include <asm/vdso.h>
#ifdef CONFIG_PPC64
#include "../kernel/ppc32.h"
#endif


/*
 * Is sp valid as the address of the next kernel stack frame after prev_sp?
 * The next frame may be in a different stack area but should not go
 * back down in the same stack area.
 */
static int valid_next_sp(unsigned long sp, unsigned long prev_sp)
{
	if (sp & 0xf)
		return 0;		/* must be 16-byte aligned */
	if (!validate_sp(sp, current, STACK_FRAME_OVERHEAD))
		return 0;
	if (sp >= prev_sp + STACK_FRAME_MIN_SIZE)
		return 1;
	/*
	 * sp could decrease when we jump off an interrupt stack
	 * back to the regular process stack.
	 */
	if ((sp & ~(THREAD_SIZE - 1)) != (prev_sp & ~(THREAD_SIZE - 1)))
		return 1;
	return 0;
}

void
perf_callchain_kernel(struct perf_callchain_entry *entry, struct pt_regs *regs)
{
	unsigned long sp, next_sp;
	unsigned long next_ip;
	unsigned long lr;
	long level = 0;
	unsigned long *fp;

	lr = regs->link;
	sp = regs->gpr[1];
	perf_callchain_store(entry, perf_instruction_pointer(regs));

	if (!validate_sp(sp, current, STACK_FRAME_OVERHEAD))
		return;

	for (;;) {
		fp = (unsigned long *) sp;
		next_sp = fp[0];

		if (next_sp == sp + STACK_INT_FRAME_SIZE &&
		    fp[STACK_FRAME_MARKER] == STACK_FRAME_REGS_MARKER) {
			/*
			 * This looks like an interrupt frame for an
			 * interrupt that occurred in the kernel
			 */
			regs = (struct pt_regs *)(sp + STACK_FRAME_OVERHEAD);
			next_ip = regs->nip;
			lr = regs->link;
			level = 0;
			perf_callchain_store(entry, PERF_CONTEXT_KERNEL);

		} else {
			if (level == 0)
				next_ip = lr;
			else
				next_ip = fp[STACK_FRAME_LR_SAVE];

			/*
			 * We can't tell which of the first two addresses
			 * we get are valid, but we can filter out the
			 * obviously bogus ones here.  We replace them
			 * with 0 rather than removing them entirely so
			 * that userspace can tell which is which.
			 */
			if ((level == 1 && next_ip == lr) ||
			    (level <= 1 && !kernel_text_address(next_ip)))
				next_ip = 0;

			++level;
		}

		perf_callchain_store(entry, next_ip);
		if (!valid_next_sp(next_sp, sp))
			return;
		sp = next_sp;
	}
}

#ifdef CONFIG_PPC64
/*
 * On 64-bit we don't want to invoke hash_page on user addresses from
 * interrupt context, so if the access faults, we read the page tables
 * to find which page (if any) is mapped and access it directly.
 */
static int read_user_stack_slow(void __user *ptr, void *buf, int nb)
{
	int ret = -EFAULT;
	pgd_t *pgdir;
	pte_t *ptep, pte;
	unsigned shift;
	unsigned long addr = (unsigned long) ptr;
	unsigned long offset;
	unsigned long pfn, flags;
	void *kaddr;

	pgdir = current->mm->pgd;
	if (!pgdir)
		return -EFAULT;

	local_irq_save(flags);
	ptep = find_linux_pte_or_hugepte(pgdir, addr, NULL, &shift);
	if (!ptep)
		goto err_out;
	if (!shift)
		shift = PAGE_SHIFT;

	/* align address to page boundary */
	offset = addr & ((1UL << shift) - 1);

	pte = READ_ONCE(*ptep);
	if (!pte_present(pte) || !pte_user(pte))
		goto err_out;
	pfn = pte_pfn(pte);
	if (!page_is_ram(pfn))
		goto err_out;

	/* no highmem to worry about here */
	kaddr = pfn_to_kaddr(pfn);
	memcpy(buf, kaddr + offset, nb);
	ret = 0;
err_out:
	local_irq_restore(flags);
	return ret;
}

static int read_user_stack_64(unsigned long __user *ptr, unsigned long *ret)
{
	if ((unsigned long)ptr > TASK_SIZE - sizeof(unsigned long) ||
	    ((unsigned long)ptr & 7))
		return -EFAULT;

	pagefault_disable();
	if (!__get_user_inatomic(*ret, ptr)) {
		pagefault_enable();
		return 0;
	}
	pagefault_enable();

	return read_user_stack_slow(ptr, ret, 8);
}

static int read_user_stack_32(unsigned int __user *ptr, unsigned int *ret)
{
	if ((unsigned long)ptr > TASK_SIZE - sizeof(unsigned int) ||
	    ((unsigned long)ptr & 3))
		return -EFAULT;

	pagefault_disable();
	if (!__get_user_inatomic(*ret, ptr)) {
		pagefault_enable();
		return 0;
	}
	pagefault_enable();

	return read_user_stack_slow(ptr, ret, 4);
}

static inline int valid_user_sp(unsigned long sp, int is_64)
{
	if (!sp || (sp & 7) || sp > (is_64 ? TASK_SIZE : 0x100000000UL) - 32)
		return 0;
	return 1;
}

/*
 * 64-bit user processes use the same stack frame for RT and non-RT signals.
 */
struct signal_frame_64 {
	char		dummy[__SIGNAL_FRAMESIZE];
	struct ucontext	uc;
	unsigned long	unused[2];
	unsigned int	tramp[6];
	struct siginfo	*pinfo;
	void		*puc;
	struct siginfo	info;
	char		abigap[288];
};

static int is_sigreturn_64_address(unsigned long nip, unsigned long fp)
{
	if (nip == fp + offsetof(struct signal_frame_64, tramp))
		return 1;
	if (vdso64_rt_sigtramp && current->mm->context.vdso_base &&
	    nip == current->mm->context.vdso_base + vdso64_rt_sigtramp)
		return 1;
	return 0;
}

/*
 * Do some sanity checking on the signal frame pointed to by sp.
 * We check the pinfo and puc pointers in the frame.
 */
static int sane_signal_64_frame(unsigned long sp)
{
	struct signal_frame_64 __user *sf;
	unsigned long pinfo, puc;

	sf = (struct signal_frame_64 __user *) sp;
	if (read_user_stack_64((unsigned long __user *) &sf->pinfo, &pinfo) ||
	    read_user_stack_64((unsigned long __user *) &sf->puc, &puc))
		return 0;
	return pinfo == (unsigned long) &sf->info &&
		puc == (unsigned long) &sf->uc;
}

static void perf_callchain_user_64(struct perf_callchain_entry *entry,
				   struct pt_regs *regs)
{
	unsigned long sp, next_sp;
	unsigned long next_ip;
	unsigned long lr;
	long level = 0;
	struct signal_frame_64 __user *sigframe;
	unsigned long __user *fp, *uregs;

	next_ip = perf_instruction_pointer(regs);
	lr = regs->link;
	sp = regs->gpr[1];
	perf_callchain_store(entry, next_ip);

	while (entry->nr < sysctl_perf_event_max_stack) {
		fp = (unsigned long __user *) sp;
		if (!valid_user_sp(sp, 1) || read_user_stack_64(fp, &next_sp))
			return;
		if (level > 0 && read_user_stack_64(&fp[2], &next_ip))
			return;

		/*
		 * Note: the next_sp - sp >= signal frame size check
		 * is true when next_sp < sp, which can happen when
		 * transitioning from an alternate signal stack to the
		 * normal stack.
		 */
		if (next_sp - sp >= sizeof(struct signal_frame_64) &&
		    (is_sigreturn_64_address(next_ip, sp) ||
		     (level <= 1 && is_sigreturn_64_address(lr, sp))) &&
		    sane_signal_64_frame(sp)) {
			/*
			 * This looks like an signal frame
			 */
			sigframe = (struct signal_frame_64 __user *) sp;
			uregs = sigframe->uc.uc_mcontext.gp_regs;
			if (read_user_stack_64(&uregs[PT_NIP], &next_ip) ||
			    read_user_stack_64(&uregs[PT_LNK], &lr) ||
			    read_user_stack_64(&uregs[PT_R1], &sp))
				return;
			level = 0;
			perf_callchain_store(entry, PERF_CONTEXT_USER);
			perf_callchain_store(entry, next_ip);
			continue;
		}

		if (level == 0)
			next_ip = lr;
		perf_callchain_store(entry, next_ip);
		++level;
		sp = next_sp;
	}
}

static inline int current_is_64bit(void)
{
	/*
	 * We can't use test_thread_flag() here because we may be on an
	 * interrupt stack, and the thread flags don't get copied over
	 * from the thread_info on the main stack to the interrupt stack.
	 */
	return !test_ti_thread_flag(task_thread_info(current), TIF_32BIT);
}

#else  /* CONFIG_PPC64 */
/*
 * On 32-bit we just access the address and let hash_page create a
 * HPTE if necessary, so there is no need to fall back to reading
 * the page tables.  Since this is called at interrupt level,
 * do_page_fault() won't treat a DSI as a page fault.
 */
static int read_user_stack_32(unsigned int __user *ptr, unsigned int *ret)
{
	int rc;

	if ((unsigned long)ptr > TASK_SIZE - sizeof(unsigned int) ||
	    ((unsigned long)ptr & 3))
		return -EFAULT;

	pagefault_disable();
	rc = __get_user_inatomic(*ret, ptr);
	pagefault_enable();

	return rc;
}

static inline void perf_callchain_user_64(struct perf_callchain_entry *entry,
					  struct pt_regs *regs)
{
}

static inline int current_is_64bit(void)
{
	return 0;
}

static inline int valid_user_sp(unsigned long sp, int is_64)
{
	if (!sp || (sp & 7) || sp > TASK_SIZE - 32)
		return 0;
	return 1;
}

#define __SIGNAL_FRAMESIZE32	__SIGNAL_FRAMESIZE
#define sigcontext32		sigcontext
#define mcontext32		mcontext
#define ucontext32		ucontext
#define compat_siginfo_t	struct siginfo

#endif /* CONFIG_PPC64 */

/*
 * Layout for non-RT signal frames
 */
struct signal_frame_32 {
	char			dummy[__SIGNAL_FRAMESIZE32];
	struct sigcontext32	sctx;
	struct mcontext32	mctx;
	int			abigap[56];
};

/*
 * Layout for RT signal frames
 */
struct rt_signal_frame_32 {
	char			dummy[__SIGNAL_FRAMESIZE32 + 16];
	compat_siginfo_t	info;
	struct ucontext32	uc;
	int			abigap[56];
};

static int is_sigreturn_32_address(unsigned int nip, unsigned int fp)
{
	if (nip == fp + offsetof(struct signal_frame_32, mctx.mc_pad))
		return 1;
	if (vdso32_sigtramp && current->mm->context.vdso_base &&
	    nip == current->mm->context.vdso_base + vdso32_sigtramp)
		return 1;
	return 0;
}

static int is_rt_sigreturn_32_address(unsigned int nip, unsigned int fp)
{
	if (nip == fp + offsetof(struct rt_signal_frame_32,
				 uc.uc_mcontext.mc_pad))
		return 1;
	if (vdso32_rt_sigtramp && current->mm->context.vdso_base &&
	    nip == current->mm->context.vdso_base + vdso32_rt_sigtramp)
		return 1;
	return 0;
}

static int sane_signal_32_frame(unsigned int sp)
{
	struct signal_frame_32 __user *sf;
	unsigned int regs;

	sf = (struct signal_frame_32 __user *) (unsigned long) sp;
	if (read_user_stack_32((unsigned int __user *) &sf->sctx.regs, &regs))
		return 0;
	return regs == (unsigned long) &sf->mctx;
}

static int sane_rt_signal_32_frame(unsigned int sp)
{
	struct rt_signal_frame_32 __user *sf;
	unsigned int regs;

	sf = (struct rt_signal_frame_32 __user *) (unsigned long) sp;
	if (read_user_stack_32((unsigned int __user *) &sf->uc.uc_regs, &regs))
		return 0;
	return regs == (unsigned long) &sf->uc.uc_mcontext;
}

static unsigned int __user *signal_frame_32_regs(unsigned int sp,
				unsigned int next_sp, unsigned int next_ip)
{
	struct mcontext32 __user *mctx = NULL;
	struct signal_frame_32 __user *sf;
	struct rt_signal_frame_32 __user *rt_sf;

	/*
	 * Note: the next_sp - sp >= signal frame size check
	 * is true when next_sp < sp, for example, when
	 * transitioning from an alternate signal stack to the
	 * normal stack.
	 */
	if (next_sp - sp >= sizeof(struct signal_frame_32) &&
	    is_sigreturn_32_address(next_ip, sp) &&
	    sane_signal_32_frame(sp)) {
		sf = (struct signal_frame_32 __user *) (unsigned long) sp;
		mctx = &sf->mctx;
	}

	if (!mctx && next_sp - sp >= sizeof(struct rt_signal_frame_32) &&
	    is_rt_sigreturn_32_address(next_ip, sp) &&
	    sane_rt_signal_32_frame(sp)) {
		rt_sf = (struct rt_signal_frame_32 __user *) (unsigned long) sp;
		mctx = &rt_sf->uc.uc_mcontext;
	}

	if (!mctx)
		return NULL;
	return mctx->mc_gregs;
}

static void perf_callchain_user_32(struct perf_callchain_entry *entry,
				   struct pt_regs *regs)
{
	unsigned int sp, next_sp;
	unsigned int next_ip;
	unsigned int lr;
	long level = 0;
	unsigned int __user *fp, *uregs;

	next_ip = perf_instruction_pointer(regs);
	lr = regs->link;
	sp = regs->gpr[1];
	perf_callchain_store(entry, next_ip);

	while (entry->nr < sysctl_perf_event_max_stack) {
		fp = (unsigned int __user *) (unsigned long) sp;
		if (!valid_user_sp(sp, 0) || read_user_stack_32(fp, &next_sp))
			return;
		if (level > 0 && read_user_stack_32(&fp[1], &next_ip))
			return;

		uregs = signal_frame_32_regs(sp, next_sp, next_ip);
		if (!uregs && level <= 1)
			uregs = signal_frame_32_regs(sp, next_sp, lr);
		if (uregs) {
			/*
			 * This looks like an signal frame, so restart
			 * the stack trace with the values in it.
			 */
			if (read_user_stack_32(&uregs[PT_NIP], &next_ip) ||
			    read_user_stack_32(&uregs[PT_LNK], &lr) ||
			    read_user_stack_32(&uregs[PT_R1], &sp))
				return;
			level = 0;
			perf_callchain_store(entry, PERF_CONTEXT_USER);
			perf_callchain_store(entry, next_ip);
			continue;
		}

		if (level == 0)
			next_ip = lr;
		perf_callchain_store(entry, next_ip);
		++level;
		sp = next_sp;
	}
}

void
perf_callchain_user(struct perf_callchain_entry *entry, struct pt_regs *regs)
{
	if (current_is_64bit())
		perf_callchain_user_64(entry, regs);
	else
		perf_callchain_user_32(entry, regs);
}
