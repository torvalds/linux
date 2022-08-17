/*
 * OpenRISC unwinder.c
 *
 * Reusable arch specific api for unwinding stacks.
 *
 * Copyright (C) 2017 Stafford Horne <shorne@gmail.com>
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#include <linux/sched/task_stack.h>
#include <linux/kernel.h>

#include <asm/unwinder.h>

#ifdef CONFIG_FRAME_POINTER
struct or1k_frameinfo {
	unsigned long *fp;
	unsigned long ra;
	unsigned long top;
};

/*
 * Verify a frameinfo structure.  The return address should be a valid text
 * address.  The frame pointer may be null if its the last frame, otherwise
 * the frame pointer should point to a location in the stack after the
 * top of the next frame up.
 */
static inline int or1k_frameinfo_valid(struct or1k_frameinfo *frameinfo)
{
	return (frameinfo->fp == NULL ||
		(!kstack_end(frameinfo->fp) &&
		 frameinfo->fp > &frameinfo->top)) &&
	       __kernel_text_address(frameinfo->ra);
}

/*
 * Create a stack trace doing scanning which is frame pointer aware. We can
 * get reliable stack traces by matching the previously found frame
 * pointer with the top of the stack address every time we find a valid
 * or1k_frameinfo.
 *
 * Ideally the stack parameter will be passed as FP, but it can not be
 * guaranteed.  Therefore we scan each address looking for the first sign
 * of a return address.
 *
 * The OpenRISC stack frame looks something like the following.  The
 * location SP is held in r1 and location FP is held in r2 when frame pointers
 * enabled.
 *
 * SP   -> (top of stack)
 *      -  (callee saved registers)
 *      -  (local variables)
 * FP-8 -> previous FP             \
 * FP-4 -> return address          |- or1k_frameinfo
 * FP   -> (previous top of stack) /
 */
void unwind_stack(void *data, unsigned long *stack,
		  void (*trace)(void *data, unsigned long addr, int reliable))
{
	unsigned long *next_fp = NULL;
	struct or1k_frameinfo *frameinfo = NULL;
	int reliable = 0;

	while (!kstack_end(stack)) {
		frameinfo = container_of(stack,
					 struct or1k_frameinfo,
					 top);

		if (__kernel_text_address(frameinfo->ra)) {
			if (or1k_frameinfo_valid(frameinfo) &&
			    (next_fp == NULL ||
			     next_fp == &frameinfo->top)) {
				reliable = 1;
				next_fp = frameinfo->fp;
			} else
				reliable = 0;

			trace(data, frameinfo->ra, reliable);
		}
		stack++;
	}
}

#else /* CONFIG_FRAME_POINTER */

/*
 * Create a stack trace by doing a simple scan treating all text addresses
 * as return addresses.
 */
void unwind_stack(void *data, unsigned long *stack,
		   void (*trace)(void *data, unsigned long addr, int reliable))
{
	unsigned long addr;

	while (!kstack_end(stack)) {
		addr = *stack++;
		if (__kernel_text_address(addr))
			trace(data, addr, 0);
	}
}
#endif /* CONFIG_FRAME_POINTER */

