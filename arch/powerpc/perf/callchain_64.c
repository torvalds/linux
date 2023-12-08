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

static int read_user_stack_64(const unsigned long __user *ptr, unsigned long *ret)
{
	return __read_user_stack(ptr, ret, sizeof(*ret));
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
	if (current->mm->context.vdso &&
	    nip == VDSO64_SYMBOL(current->mm->context.vdso, sigtramp_rt64))
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

void perf_callchain_user_64(struct perf_callchain_entry_ctx *entry,
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

	while (entry->nr < entry->max_stack) {
		fp = (unsigned long __user *) sp;
		if (invalid_user_sp(sp) || read_user_stack_64(fp, &next_sp))
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
