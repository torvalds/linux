/*
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *  Copyright (C) 2000, 2001, 2002 Andi Kleen, SuSE Labs
 */

#ifndef DUMPSTACK_H
#define DUMPSTACK_H

#ifdef CONFIG_X86_32
#define STACKSLOTS_PER_LINE 8
#define get_bp(bp) asm("movl %%ebp, %0" : "=r" (bp) :)
#else
#define STACKSLOTS_PER_LINE 4
#define get_bp(bp) asm("movq %%rbp, %0" : "=r" (bp) :)
#endif

#include <linux/uaccess.h>

extern void
show_trace_log_lvl(struct task_struct *task, struct pt_regs *regs,
		unsigned long *stack, unsigned long bp, char *log_lvl);

extern void
show_stack_log_lvl(struct task_struct *task, struct pt_regs *regs,
		unsigned long *sp, unsigned long bp, char *log_lvl);

extern unsigned int code_bytes;

/* The form of the top of the frame on the stack */
struct stack_frame {
	struct stack_frame *next_frame;
	unsigned long return_address;
};

struct stack_frame_ia32 {
    u32 next_frame;
    u32 return_address;
};

static inline unsigned long rewind_frame_pointer(int n)
{
	struct stack_frame *frame;

	get_bp(frame);

#ifdef CONFIG_FRAME_POINTER
	while (n--) {
		if (probe_kernel_address(&frame->next_frame, frame))
			break;
	}
#endif

	return (unsigned long)frame;
}

#endif /* DUMPSTACK_H */
