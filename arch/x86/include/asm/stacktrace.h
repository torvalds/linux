/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *  Copyright (C) 2000, 2001, 2002 Andi Kleen, SuSE Labs
 */

#ifndef _ASM_X86_STACKTRACE_H
#define _ASM_X86_STACKTRACE_H

#include <linux/uaccess.h>
#include <linux/ptrace.h>
#include <asm/switch_to.h>

enum stack_type {
	STACK_TYPE_UNKNOWN,
	STACK_TYPE_TASK,
	STACK_TYPE_IRQ,
	STACK_TYPE_SOFTIRQ,
	STACK_TYPE_ENTRY,
	STACK_TYPE_EXCEPTION,
	STACK_TYPE_EXCEPTION_LAST = STACK_TYPE_EXCEPTION + N_EXCEPTION_STACKS-1,
};

struct stack_info {
	enum stack_type type;
	unsigned long *begin, *end, *next_sp;
};

bool in_task_stack(unsigned long *stack, struct task_struct *task,
		   struct stack_info *info);

bool in_entry_stack(unsigned long *stack, struct stack_info *info);

int get_stack_info(unsigned long *stack, struct task_struct *task,
		   struct stack_info *info, unsigned long *visit_mask);

const char *stack_type_name(enum stack_type type);

static inline bool on_stack(struct stack_info *info, void *addr, size_t len)
{
	void *begin = info->begin;
	void *end   = info->end;

	return (info->type != STACK_TYPE_UNKNOWN &&
		addr >= begin && addr < end &&
		addr + len > begin && addr + len <= end);
}

#ifdef CONFIG_X86_32
#define STACKSLOTS_PER_LINE 8
#else
#define STACKSLOTS_PER_LINE 4
#endif

#ifdef CONFIG_FRAME_POINTER
static inline unsigned long *
get_frame_pointer(struct task_struct *task, struct pt_regs *regs)
{
	if (regs)
		return (unsigned long *)regs->bp;

	if (task == current)
		return __builtin_frame_address(0);

	return &((struct inactive_task_frame *)task->thread.sp)->bp;
}
#else
static inline unsigned long *
get_frame_pointer(struct task_struct *task, struct pt_regs *regs)
{
	return NULL;
}
#endif /* CONFIG_FRAME_POINTER */

static inline unsigned long *
get_stack_pointer(struct task_struct *task, struct pt_regs *regs)
{
	if (regs)
		return (unsigned long *)kernel_stack_pointer(regs);

	if (task == current)
		return __builtin_frame_address(0);

	return (unsigned long *)task->thread.sp;
}

void show_trace_log_lvl(struct task_struct *task, struct pt_regs *regs,
			unsigned long *stack, char *log_lvl);

/* The form of the top of the frame on the stack */
struct stack_frame {
	struct stack_frame *next_frame;
	unsigned long return_address;
};

struct stack_frame_ia32 {
    u32 next_frame;
    u32 return_address;
};

static inline unsigned long caller_frame_pointer(void)
{
	struct stack_frame *frame;

	frame = __builtin_frame_address(0);

#ifdef CONFIG_FRAME_POINTER
	frame = frame->next_frame;
#endif

	return (unsigned long)frame;
}

void show_opcodes(struct pt_regs *regs, const char *loglvl);
void show_ip(struct pt_regs *regs, const char *loglvl);
#endif /* _ASM_X86_STACKTRACE_H */
