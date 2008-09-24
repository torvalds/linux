#ifndef __ASM_SPARC_SYSCALL_H
#define __ASM_SPARC_SYSCALL_H

#include <linux/kernel.h>
#include <linux/sched.h>
#include <asm/ptrace.h>

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
					 unsigned int i, unsigned int n,
					 unsigned long *args)
{
	int zero_extend = 0;
	unsigned int j;

#ifdef CONFIG_SPARC64
	if (test_tsk_thread_flag(task, TIF_32BIT))
		zero_extend = 1;
#endif

	for (j = 0; j < n; j++) {
		unsigned long val = regs->u_regs[UREG_I0 + i + j];

		if (zero_extend)
			args[j] = (u32) val;
		else
			args[j] = val;
	}
}

static inline void syscall_set_arguments(struct task_struct *task,
					 struct pt_regs *regs,
					 unsigned int i, unsigned int n,
					 const unsigned long *args)
{
	unsigned int j;

	for (j = 0; j < n; j++)
		regs->u_regs[UREG_I0 + i + j] = args[j];
}

#endif /* __ASM_SPARC_SYSCALL_H */
