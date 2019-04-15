/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_SPARC_SYSCALL_H
#define __ASM_SPARC_SYSCALL_H

#include <uapi/linux/audit.h>
#include <linux/kernel.h>
#include <linux/compat.h>
#include <linux/sched.h>
#include <asm/ptrace.h>
#include <asm/thread_info.h>

/*
 * The syscall table always contains 32 bit pointers since we know that the
 * address of the function to be called is (way) below 4GB.  So the "int"
 * type here is what we want [need] for both 32 bit and 64 bit systems.
 */
extern const unsigned int sys_call_table[];

/* The system call number is given by the user in %g1 */
static inline long syscall_get_nr(struct task_struct *task,
				  struct pt_regs *regs)
{
	int syscall_p = pt_regs_is_syscall(regs);

	return (syscall_p ? regs->u_regs[UREG_G1] : -1L);
}

static inline void syscall_rollback(struct task_struct *task,
				    struct pt_regs *regs)
{
	/* XXX This needs some thought.  On Sparc we don't
	 * XXX save away the original %o0 value somewhere.
	 * XXX Instead we hold it in register %l5 at the top
	 * XXX level trap frame and pass this down to the signal
	 * XXX dispatch code which is the only place that value
	 * XXX ever was needed.
	 */
}

#ifdef CONFIG_SPARC32
static inline bool syscall_has_error(struct pt_regs *regs)
{
	return (regs->psr & PSR_C) ? true : false;
}
static inline void syscall_set_error(struct pt_regs *regs)
{
	regs->psr |= PSR_C;
}
static inline void syscall_clear_error(struct pt_regs *regs)
{
	regs->psr &= ~PSR_C;
}
#else
static inline bool syscall_has_error(struct pt_regs *regs)
{
	return (regs->tstate & (TSTATE_XCARRY | TSTATE_ICARRY)) ? true : false;
}
static inline void syscall_set_error(struct pt_regs *regs)
{
	regs->tstate |= (TSTATE_XCARRY | TSTATE_ICARRY);
}
static inline void syscall_clear_error(struct pt_regs *regs)
{
	regs->tstate &= ~(TSTATE_XCARRY | TSTATE_ICARRY);
}
#endif

static inline long syscall_get_error(struct task_struct *task,
				     struct pt_regs *regs)
{
	long val = regs->u_regs[UREG_I0];

	return (syscall_has_error(regs) ? -val : 0);
}

static inline long syscall_get_return_value(struct task_struct *task,
					    struct pt_regs *regs)
{
	long val = regs->u_regs[UREG_I0];

	return val;
}

static inline void syscall_set_return_value(struct task_struct *task,
					    struct pt_regs *regs,
					    int error, long val)
{
	if (error) {
		syscall_set_error(regs);
		regs->u_regs[UREG_I0] = -error;
	} else {
		syscall_clear_error(regs);
		regs->u_regs[UREG_I0] = val;
	}
}

static inline void syscall_get_arguments(struct task_struct *task,
					 struct pt_regs *regs,
					 unsigned long *args)
{
	int zero_extend = 0;
	unsigned int j;
	unsigned int n = 6;

#ifdef CONFIG_SPARC64
	if (test_tsk_thread_flag(task, TIF_32BIT))
		zero_extend = 1;
#endif

	for (j = 0; j < n; j++) {
		unsigned long val = regs->u_regs[UREG_I0 + j];

		if (zero_extend)
			args[j] = (u32) val;
		else
			args[j] = val;
	}
}

static inline void syscall_set_arguments(struct task_struct *task,
					 struct pt_regs *regs,
					 const unsigned long *args)
{
	unsigned int i;

	for (i = 0; i < 6; i++)
		regs->u_regs[UREG_I0 + i] = args[i];
}

static inline int syscall_get_arch(void)
{
#if defined(CONFIG_SPARC64) && defined(CONFIG_COMPAT)
	return in_compat_syscall() ? AUDIT_ARCH_SPARC : AUDIT_ARCH_SPARC64;
#elif defined(CONFIG_SPARC64)
	return AUDIT_ARCH_SPARC64;
#else
	return AUDIT_ARCH_SPARC;
#endif
}

#endif /* __ASM_SPARC_SYSCALL_H */
