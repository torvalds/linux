/*
 * Copyright (C) 1994 Linus Torvalds
 *
 * Pentium III FXSR, SSE support
 * General FPU state handling cleanups
 *	Gareth Hughes <gareth@valinux.com>, May 2000
 * x86-64 work by Andi Kleen 2002
 */

#ifndef _ASM_X86_I387_H
#define _ASM_X86_I387_H

#ifndef __ASSEMBLY__

#include <linux/sched.h>
#include <linux/hardirq.h>

struct pt_regs;
struct user_i387_struct;

extern int init_fpu(struct task_struct *child);
extern void fpu_finit(struct fpu *fpu);
extern int dump_fpu(struct pt_regs *, struct user_i387_struct *);
extern void math_state_restore(void);

extern bool irq_fpu_usable(void);
extern void kernel_fpu_begin(void);
extern void kernel_fpu_end(void);

/*
 * Some instructions like VIA's padlock instructions generate a spurious
 * DNA fault but don't modify SSE registers. And these instructions
 * get used from interrupt context as well. To prevent these kernel instructions
 * in interrupt context interacting wrongly with other user/kernel fpu usage, we
 * should use them only in the context of irq_ts_save/restore()
 */
static inline int irq_ts_save(void)
{
	/*
	 * If in process context and not atomic, we can take a spurious DNA fault.
	 * Otherwise, doing clts() in process context requires disabling preemption
	 * or some heavy lifting like kernel_fpu_begin()
	 */
	if (!in_atomic())
		return 0;

	if (read_cr0() & X86_CR0_TS) {
		clts();
		return 1;
	}

	return 0;
}

static inline void irq_ts_restore(int TS_state)
{
	if (TS_state)
		stts();
}

/*
 * The question "does this thread have fpu access?"
 * is slightly racy, since preemption could come in
 * and revoke it immediately after the test.
 *
 * However, even in that very unlikely scenario,
 * we can just assume we have FPU access - typically
 * to save the FP state - we'll just take a #NM
 * fault and get the FPU access back.
 */
static inline int user_has_fpu(void)
{
	return current->thread.fpu.has_fpu;
}

extern void unlazy_fpu(struct task_struct *tsk);

#endif /* __ASSEMBLY__ */

#endif /* _ASM_X86_I387_H */
