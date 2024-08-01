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
#ifdef CONFIG_UNWINDER_FRAME_POINTER
	bool ex_frame;
#endif
};

static inline bool on_thread_stack(void)
{
	unsigned long delta = current_stack_pointer ^ (unsigned long)current->stack;

	return delta < THREAD_SIZE;
}

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
#ifdef CONFIG_UNWINDER_FRAME_POINTER
		frame->ex_frame = in_entry_text(frame->pc);
#endif
}

extern int unwind_frame(struct stackframe *frame);
extern void walk_stackframe(struct stackframe *frame,
			    bool (*fn)(void *, unsigned long), void *data);
extern void dump_mem(const char *lvl, const char *str, unsigned long bottom,
		     unsigned long top);
extern void dump_backtrace(struct pt_regs *regs, struct task_struct *tsk,
			   const char *loglvl);

#endif	/* __ASM_STACKTRACE_H */
