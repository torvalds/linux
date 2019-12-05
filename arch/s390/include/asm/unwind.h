/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_S390_UNWIND_H
#define _ASM_S390_UNWIND_H

#include <linux/sched.h>
#include <linux/ftrace.h>
#include <asm/ptrace.h>
#include <asm/stacktrace.h>

/*
 * To use the stack unwinder it has to be initialized with unwind_start.
 * There four combinations for task and regs:
 * 1) task==NULL, regs==NULL: the unwind starts for the task that is currently
 *    running, sp/ip picked up from the CPU registers
 * 2) task==NULL, regs!=NULL: the unwind starts from the sp/ip found in
 *    the struct pt_regs of an interrupt frame for the current task
 * 3) task!=NULL, regs==NULL: the unwind starts for an inactive task with
 *    the sp picked up from task->thread.ksp and the ip picked up from the
 *    return address stored by __switch_to
 * 4) task!=NULL, regs!=NULL: the sp/ip are picked up from the interrupt
 *    frame 'regs' of a inactive task
 * If 'first_frame' is not zero unwind_start skips unwind frames until it
 * reaches the specified stack pointer.
 * The end of the unwinding is indicated with unwind_done, this can be true
 * right after unwind_start, e.g. with first_frame!=0 that can not be found.
 * unwind_next_frame skips to the next frame.
 * Once the unwind is completed unwind_error() can be used to check if there
 * has been a situation where the unwinder could not correctly understand
 * the tasks call chain.
 */

struct unwind_state {
	struct stack_info stack_info;
	unsigned long stack_mask;
	struct task_struct *task;
	struct pt_regs *regs;
	unsigned long sp, ip;
	int graph_idx;
	bool reliable;
	bool error;
};

void __unwind_start(struct unwind_state *state, struct task_struct *task,
		    struct pt_regs *regs, unsigned long first_frame);
bool unwind_next_frame(struct unwind_state *state);
unsigned long unwind_get_return_address(struct unwind_state *state);

static inline bool unwind_done(struct unwind_state *state)
{
	return state->stack_info.type == STACK_TYPE_UNKNOWN;
}

static inline bool unwind_error(struct unwind_state *state)
{
	return state->error;
}

static inline void unwind_start(struct unwind_state *state,
				struct task_struct *task,
				struct pt_regs *regs,
				unsigned long first_frame)
{
	task = task ?: current;
	first_frame = first_frame ?: get_stack_pointer(task, regs);
	__unwind_start(state, task, regs, first_frame);
}

static inline struct pt_regs *unwind_get_entry_regs(struct unwind_state *state)
{
	return unwind_done(state) ? NULL : state->regs;
}

#define unwind_for_each_frame(state, task, regs, first_frame)	\
	for (unwind_start(state, task, regs, first_frame);	\
	     !unwind_done(state);				\
	     unwind_next_frame(state))

static inline void unwind_init(void) {}
static inline void unwind_module_init(struct module *mod, void *orc_ip,
				      size_t orc_ip_size, void *orc,
				      size_t orc_size) {}

#endif /* _ASM_S390_UNWIND_H */
