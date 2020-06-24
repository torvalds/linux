// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Performance counter callchain support - powerpc architecture code
 *
 * Copyright © 2009 Paul Mackerras, IBM Corporation.
 */
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/perf_event.h>
#include <linux/percpu.h>
#include <linux/uaccess.h>
#include <linux/mm.h>
#include <asm/ptrace.h>
#include <asm/sigcontext.h>
#include <asm/ucontext.h>
#include <asm/vdso.h>
#include <asm/pte-walk.h>

#include "callchain.h"

#ifdef CONFIG_PPC64
#include "../kernel/ppc32.h"
#else  /* CONFIG_PPC64 */

#define __SIGNAL_FRAMESIZE32	__SIGNAL_FRAMESIZE
#define sigcontext32		sigcontext
#define mcontext32		mcontext
#define ucontext32		ucontext
#define compat_siginfo_t	struct siginfo

#endif /* CONFIG_PPC64 */

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

	rc = probe_user_read(ret, ptr, sizeof(*ret));

	if (IS_ENABLED(CONFIG_PPC64) && rc)
		return read_user_stack_slow(ptr, ret, 4);

	return rc;
}

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

void perf_callchain_user_32(struct perf_callchain_entry_ctx *entry,
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

	while (entry->nr < entry->max_stack) {
		fp = (unsigned int __user *) (unsigned long) sp;
		if (invalid_user_sp(sp) || read_user_stack_32(fp, &next_sp))
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
			perf_callchain_store_context(entry, PERF_CONTEXT_USER);
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
