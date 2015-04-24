/*
 * Copyright (C) 1994 Linus Torvalds
 *
 * Pentium III FXSR, SSE support
 * General FPU state handling cleanups
 *	Gareth Hughes <gareth@valinux.com>, May 2000
 * x86-64 work by Andi Kleen 2002
 */

#ifndef _ASM_X86_FPU_API_H
#define _ASM_X86_FPU_API_H

#include <linux/sched.h>
#include <linux/hardirq.h>

struct pt_regs;
struct user_i387_struct;

extern int fpstate_alloc_init(struct fpu *fpu);
extern void fpstate_init(struct fpu *fpu);
extern void fpu__clear(struct task_struct *tsk);

extern int dump_fpu(struct pt_regs *, struct user_i387_struct *);
extern void fpu__restore(void);
extern void fpu__init_check_bugs(void);
extern void fpu__resume_cpu(void);

extern bool irq_fpu_usable(void);

/*
 * Careful: __kernel_fpu_begin/end() must be called with preempt disabled
 * and they don't touch the preempt state on their own.
 * If you enable preemption after __kernel_fpu_begin(), preempt notifier
 * should call the __kernel_fpu_end() to prevent the kernel/user FPU
 * state from getting corrupted. KVM for example uses this model.
 *
 * All other cases use kernel_fpu_begin/end() which disable preemption
 * during kernel FPU usage.
 */
extern void __kernel_fpu_begin(void);
extern void __kernel_fpu_end(void);

static inline void kernel_fpu_begin(void)
{
	preempt_disable();
	WARN_ON_ONCE(!irq_fpu_usable());
	__kernel_fpu_begin();
}

static inline void kernel_fpu_end(void)
{
	__kernel_fpu_end();
	preempt_enable();
}

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
	return current->thread.fpu.fpregs_active;
}

extern void fpu__save(struct fpu *fpu);

#endif /* _ASM_X86_FPU_API_H */
