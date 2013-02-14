#ifndef _ASM_SCORE_PTRACE_H
#define _ASM_SCORE_PTRACE_H

#include <uapi/asm/ptrace.h>


struct task_struct;

/*
 * Does the process account for user or for system time?
 */
#define user_mode(regs) 	((regs->cp0_psr & 8) == 8)

#define instruction_pointer(regs)	((unsigned long)(regs)->cp0_epc)
#define profile_pc(regs)		instruction_pointer(regs)
#define user_stack_pointer(r)		((unsigned long)(r)->regs[0])

extern void do_syscall_trace(struct pt_regs *regs, int entryexit);
extern int read_tsk_long(struct task_struct *, unsigned long, unsigned long *);
extern int read_tsk_short(struct task_struct *, unsigned long,
			 unsigned short *);

#define arch_has_single_step()	(1)

#endif /* _ASM_SCORE_PTRACE_H */
