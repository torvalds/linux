/*
 * Backtrace support for Microblaze
 *
 * Copyright (C) 2010  Digital Design Corporation
 *
 * Based on arch/sh/kernel/cpu/sh5/unwind.c code which is:
 * Copyright (C) 2004  Paul Mundt
 * Copyright (C) 2004  Richard Curnow
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

/* #define DEBUG 1 */
#include <linux/kallsyms.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/stacktrace.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/io.h>
#include <asm/sections.h>
#include <asm/exceptions.h>
#include <asm/unwind.h>

struct stack_trace;

/*
 * On Microblaze, finding the previous stack frame is a little tricky.
 * At this writing (3/2010), Microblaze does not support CONFIG_FRAME_POINTERS,
 * and even if it did, gcc (4.1.2) does not store the frame pointer at
 * a consistent offset within each frame. To determine frame size, it is
 * necessary to search for the assembly instruction that creates or reclaims
 * the frame and extract the size from it.
 *
 * Microblaze stores the stack pointer in r1, and creates a frame via
 *
 *     addik r1, r1, -FRAME_SIZE
 *
 * The frame is reclaimed via
 *
 *     addik r1, r1, FRAME_SIZE
 *
 * Frame creation occurs at or near the top of a function.
 * Depending on the compiler, reclaim may occur at the end, or before
 * a mid-function return.
 *
 * A stack frame is usually not created in a leaf function.
 *
 */

/**
 * get_frame_size - Extract the stack adjustment from an
 *                  "addik r1, r1, adjust" instruction
 * @instr : Microblaze instruction
 *
 * Return - Number of stack bytes the instruction reserves or reclaims
 */
inline long get_frame_size(unsigned long instr)
{
	return abs((s16)(instr & 0xFFFF));
}

/**
 * find_frame_creation - Search backward to find the instruction that creates
 *                       the stack frame (hopefully, for the same function the
 *                       initial PC is in).
 * @pc : Program counter at which to begin the search
 *
 * Return - PC at which stack frame creation occurs
 *          NULL if this cannot be found, i.e. a leaf function
 */
static unsigned long *find_frame_creation(unsigned long *pc)
{
	int i;

	/* NOTE: Distance to search is arbitrary
	 *	 250 works well for most things,
	 *	 750 picks up things like tcp_recvmsg(),
	 *	1000 needed for fat_fill_super()
	 */
	for (i = 0; i < 1000; i++, pc--) {
		unsigned long instr;
		s16 frame_size;

		if (!kernel_text_address((unsigned long) pc))
			return NULL;

		instr = *pc;

		/* addik r1, r1, foo ? */
		if ((instr & 0xFFFF0000) != 0x30210000)
			continue;	/* No */

		frame_size = get_frame_size(instr);
		if ((frame_size < 8) || (frame_size & 3)) {
			pr_debug("    Invalid frame size %d at 0x%p\n",
				 frame_size, pc);
			return NULL;
		}

		pr_debug("    Found frame creation at 0x%p, size %d\n", pc,
			 frame_size);
		return pc;
	}

	return NULL;
}

/**
 * lookup_prev_stack_frame - Find the stack frame of the previous function.
 * @fp          : Frame (stack) pointer for current function
 * @pc          : Program counter within current function
 * @leaf_return : r15 value within current function. If the current function
 *		  is a leaf, this is the caller's return address.
 * @pprev_fp    : On exit, set to frame (stack) pointer for previous function
 * @pprev_pc    : On exit, set to current function caller's return address
 *
 * Return - 0 on success, -EINVAL if the previous frame cannot be found
 */
static int lookup_prev_stack_frame(unsigned long fp, unsigned long pc,
				   unsigned long leaf_return,
				   unsigned long *pprev_fp,
				   unsigned long *pprev_pc)
{
	unsigned long *prologue = NULL;

	/* _switch_to is a special leaf function */
	if (pc != (unsigned long) &_switch_to)
		prologue = find_frame_creation((unsigned long *)pc);

	if (prologue) {
		long frame_size = get_frame_size(*prologue);

		*pprev_fp = fp + frame_size;
		*pprev_pc = *(unsigned long *)fp;
	} else {
		if (!leaf_return)
			return -EINVAL;
		*pprev_pc = leaf_return;
		*pprev_fp = fp;
	}

	/* NOTE: don't check kernel_text_address here, to allow display
	 *	 of userland return address
	 */
	return (!*pprev_pc || (*pprev_pc & 3)) ? -EINVAL : 0;
}

static void microblaze_unwind_inner(struct task_struct *task,
				    unsigned long pc, unsigned long fp,
				    unsigned long leaf_return,
				    struct stack_trace *trace);

/**
 * unwind_trap - Unwind through a system trap, that stored previous state
 *		 on the stack.
 */
#ifdef CONFIG_MMU
static inline void unwind_trap(struct task_struct *task, unsigned long pc,
				unsigned long fp, struct stack_trace *trace)
{
	/* To be implemented */
}
#else
static inline void unwind_trap(struct task_struct *task, unsigned long pc,
				unsigned long fp, struct stack_trace *trace)
{
	const struct pt_regs *regs = (const struct pt_regs *) fp;
	microblaze_unwind_inner(task, regs->pc, regs->r1, regs->r15, trace);
}
#endif

/**
 * microblaze_unwind_inner - Unwind the stack from the specified point
 * @task  : Task whose stack we are to unwind (may be NULL)
 * @pc    : Program counter from which we start unwinding
 * @fp    : Frame (stack) pointer from which we start unwinding
 * @leaf_return : Value of r15 at pc. If the function is a leaf, this is
 *				  the caller's return address.
 * @trace : Where to store stack backtrace (PC values).
 *	    NULL == print backtrace to kernel log
 */
void microblaze_unwind_inner(struct task_struct *task,
			     unsigned long pc, unsigned long fp,
			     unsigned long leaf_return,
			     struct stack_trace *trace)
{
	int ofs = 0;

	pr_debug("    Unwinding with PC=%p, FP=%p\n", (void *)pc, (void *)fp);
	if (!pc || !fp || (pc & 3) || (fp & 3)) {
		pr_debug("    Invalid state for unwind, aborting\n");
		return;
	}
	for (; pc != 0;) {
		unsigned long next_fp, next_pc = 0;
		unsigned long return_to = pc +  2 * sizeof(unsigned long);
		const struct trap_handler_info *handler =
			&microblaze_trap_handlers;

		/* Is previous function the HW exception handler? */
		if ((return_to >= (unsigned long)&_hw_exception_handler)
		    &&(return_to < (unsigned long)&ex_handler_unhandled)) {
			/*
			 * HW exception handler doesn't save all registers,
			 * so we open-code a special case of unwind_trap()
			 */
#ifndef CONFIG_MMU
			const struct pt_regs *regs =
				(const struct pt_regs *) fp;
#endif
			pr_info("HW EXCEPTION\n");
#ifndef CONFIG_MMU
			microblaze_unwind_inner(task, regs->r17 - 4,
						fp + EX_HANDLER_STACK_SIZ,
						regs->r15, trace);
#endif
			return;
		}

		/* Is previous function a trap handler? */
		for (; handler->start_addr; ++handler) {
			if ((return_to >= handler->start_addr)
			    && (return_to <= handler->end_addr)) {
				if (!trace)
					pr_info("%s\n", handler->trap_name);
				unwind_trap(task, pc, fp, trace);
				return;
			}
		}
		pc -= ofs;

		if (trace) {
#ifdef CONFIG_STACKTRACE
			if (trace->skip > 0)
				trace->skip--;
			else
				trace->entries[trace->nr_entries++] = pc;

			if (trace->nr_entries >= trace->max_entries)
				break;
#endif
		} else {
			/* Have we reached userland? */
			if (unlikely(pc == task_pt_regs(task)->pc)) {
				pr_info("[<%p>] PID %lu [%s]\n",
					(void *) pc,
					(unsigned long) task->pid,
					task->comm);
				break;
			} else
				print_ip_sym(pc);
		}

		/* Stop when we reach anything not part of the kernel */
		if (!kernel_text_address(pc))
			break;

		if (lookup_prev_stack_frame(fp, pc, leaf_return, &next_fp,
					    &next_pc) == 0) {
			ofs = sizeof(unsigned long);
			pc = next_pc & ~3;
			fp = next_fp;
			leaf_return = 0;
		} else {
			pr_debug("    Failed to find previous stack frame\n");
			break;
		}

		pr_debug("    Next PC=%p, next FP=%p\n",
			 (void *)next_pc, (void *)next_fp);
	}
}

/**
 * microblaze_unwind - Stack unwinder for Microblaze (external entry point)
 * @task  : Task whose stack we are to unwind (NULL == current)
 * @trace : Where to store stack backtrace (PC values).
 *	    NULL == print backtrace to kernel log
 */
void microblaze_unwind(struct task_struct *task, struct stack_trace *trace)
{
	if (task) {
		if (task == current) {
			const struct pt_regs *regs = task_pt_regs(task);
			microblaze_unwind_inner(task, regs->pc, regs->r1,
						regs->r15, trace);
		} else {
			struct thread_info *thread_info =
				(struct thread_info *)(task->stack);
			const struct cpu_context *cpu_context =
				&thread_info->cpu_context;

			microblaze_unwind_inner(task,
						(unsigned long) &_switch_to,
						cpu_context->r1,
						cpu_context->r15, trace);
		}
	} else {
		unsigned long pc, fp;

		__asm__ __volatile__ ("or %0, r1, r0" : "=r" (fp));

		__asm__ __volatile__ (
			"brlid %0, 0f;"
			"nop;"
			"0:"
			: "=r" (pc)
		);

		/* Since we are not a leaf function, use leaf_return = 0 */
		microblaze_unwind_inner(current, pc, fp, 0, trace);
	}
}

