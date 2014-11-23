#ifndef _ASM_UML_STACKTRACE_H
#define _ASM_UML_STACKTRACE_H

#include <linux/uaccess.h>
#include <linux/ptrace.h>

struct stack_frame {
	struct stack_frame *next_frame;
	unsigned long return_address;
};

struct stacktrace_ops {
	void (*address)(void *data, unsigned long address, int reliable);
};

#ifdef CONFIG_FRAME_POINTER
static inline unsigned long
get_frame_pointer(struct task_struct *task, struct pt_regs *segv_regs)
{
	if (!task || task == current)
		return segv_regs ? PT_REGS_BP(segv_regs) : current_bp();
	return KSTK_EBP(task);
}
#else
static inline unsigned long
get_frame_pointer(struct task_struct *task, struct pt_regs *segv_regs)
{
	return 0;
}
#endif

static inline unsigned long
*get_stack_pointer(struct task_struct *task, struct pt_regs *segv_regs)
{
	if (!task || task == current)
		return segv_regs ? (unsigned long *)PT_REGS_SP(segv_regs) : current_sp();
	return (unsigned long *)KSTK_ESP(task);
}

void dump_trace(struct task_struct *tsk, const struct stacktrace_ops *ops, void *data);

#endif /* _ASM_UML_STACKTRACE_H */
