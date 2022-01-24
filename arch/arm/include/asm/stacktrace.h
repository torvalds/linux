/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_STACKTRACE_H
#define __ASM_STACKTRACE_H

#include <asm/ptrace.h>
#include <linux/llist.h>

struct stackframe {
	/*
	 * FP member should hold R7 when CONFIG_THUMB2_KERNEL is enabled
	 * and R11 otherwise.
	 */
	unsigned long fp;
	unsigned long sp;
	unsigned long lr;
	unsigned long pc;

	/* address of the LR value on the stack */
	unsigned long *lr_addr;
#ifdef CONFIG_KRETPROBES
	struct llist_node *kr_cur;
	struct task_struct *tsk;
#endif
#ifdef CONFIG_ARM_UNWIND
	/*
	 * This field is used to track the stack pointer value when calling
	 * __entry routines. This is needed when IRQ stacks and overflow stacks
	 * are used, because in that case, the struct pt_regs passed to these
	 * __entry routines may be at the top of the task stack, while we are
	 * executing from another stack.
	 */
	unsigned long sp_low;
#endif
};

static __always_inline
void arm_get_current_stackframe(struct pt_regs *regs, struct stackframe *frame)
{
		frame->fp = frame_pointer(regs);
		frame->sp = regs->ARM_sp;
		frame->lr = regs->ARM_lr;
		frame->pc = regs->ARM_pc;
#ifdef CONFIG_KRETPROBES
		frame->kr_cur = NULL;
		frame->tsk = current;
#endif
}

extern int unwind_frame(struct stackframe *frame);
extern void walk_stackframe(struct stackframe *frame,
			    int (*fn)(struct stackframe *, void *), void *data);
extern void dump_mem(const char *lvl, const char *str, unsigned long bottom,
		     unsigned long top);

#endif	/* __ASM_STACKTRACE_H */
