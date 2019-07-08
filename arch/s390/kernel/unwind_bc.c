/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/sched/task_stack.h>
#include <linux/interrupt.h>
#include <asm/sections.h>
#include <asm/ptrace.h>
#include <asm/bitops.h>
#include <asm/stacktrace.h>
#include <asm/unwind.h>

unsigned long unwind_get_return_address(struct unwind_state *state)
{
	if (unwind_done(state))
		return 0;
	return __kernel_text_address(state->ip) ? state->ip : 0;
}
EXPORT_SYMBOL_GPL(unwind_get_return_address);

static bool outside_of_stack(struct unwind_state *state, unsigned long sp)
{
	return (sp <= state->sp) ||
		(sp + sizeof(struct stack_frame) > state->stack_info.end);
}

static bool update_stack_info(struct unwind_state *state, unsigned long sp)
{
	struct stack_info *info = &state->stack_info;
	unsigned long *mask = &state->stack_mask;

	/* New stack pointer leaves the current stack */
	if (get_stack_info(sp, state->task, info, mask) != 0 ||
	    !on_stack(info, sp, sizeof(struct stack_frame)))
		/* 'sp' does not point to a valid stack */
		return false;
	return true;
}

bool unwind_next_frame(struct unwind_state *state)
{
	struct stack_info *info = &state->stack_info;
	struct stack_frame *sf;
	struct pt_regs *regs;
	unsigned long sp, ip;
	bool reliable;

	regs = state->regs;
	if (unlikely(regs)) {
		sp = READ_ONCE_NOCHECK(regs->gprs[15]);
		if (unlikely(outside_of_stack(state, sp))) {
			if (!update_stack_info(state, sp))
				goto out_err;
		}
		sf = (struct stack_frame *) sp;
		ip = READ_ONCE_NOCHECK(sf->gprs[8]);
		reliable = false;
		regs = NULL;
	} else {
		sf = (struct stack_frame *) state->sp;
		sp = READ_ONCE_NOCHECK(sf->back_chain);
		if (likely(sp)) {
			/* Non-zero back-chain points to the previous frame */
			if (unlikely(outside_of_stack(state, sp))) {
				if (!update_stack_info(state, sp))
					goto out_err;
			}
			sf = (struct stack_frame *) sp;
			ip = READ_ONCE_NOCHECK(sf->gprs[8]);
			reliable = true;
		} else {
			/* No back-chain, look for a pt_regs structure */
			sp = state->sp + STACK_FRAME_OVERHEAD;
			if (!on_stack(info, sp, sizeof(struct pt_regs)))
				goto out_stop;
			regs = (struct pt_regs *) sp;
			if (READ_ONCE_NOCHECK(regs->psw.mask) & PSW_MASK_PSTATE)
				goto out_stop;
			ip = READ_ONCE_NOCHECK(regs->psw.addr);
			reliable = true;
		}
	}

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	/* Decode any ftrace redirection */
	if (ip == (unsigned long) return_to_handler)
		ip = ftrace_graph_ret_addr(state->task, &state->graph_idx,
					   ip, (void *) sp);
#endif

	/* Update unwind state */
	state->sp = sp;
	state->ip = ip;
	state->regs = regs;
	state->reliable = reliable;
	return true;

out_err:
	state->error = true;
out_stop:
	state->stack_info.type = STACK_TYPE_UNKNOWN;
	return false;
}
EXPORT_SYMBOL_GPL(unwind_next_frame);

void __unwind_start(struct unwind_state *state, struct task_struct *task,
		    struct pt_regs *regs, unsigned long sp)
{
	struct stack_info *info = &state->stack_info;
	unsigned long *mask = &state->stack_mask;
	struct stack_frame *sf;
	unsigned long ip;
	bool reliable;

	memset(state, 0, sizeof(*state));
	state->task = task;
	state->regs = regs;

	/* Don't even attempt to start from user mode regs: */
	if (regs && user_mode(regs)) {
		info->type = STACK_TYPE_UNKNOWN;
		return;
	}

	/* Get current stack pointer and initialize stack info */
	if (get_stack_info(sp, task, info, mask) != 0 ||
	    !on_stack(info, sp, sizeof(struct stack_frame))) {
		/* Something is wrong with the stack pointer */
		info->type = STACK_TYPE_UNKNOWN;
		state->error = true;
		return;
	}

	/* Get the instruction pointer from pt_regs or the stack frame */
	if (regs) {
		ip = READ_ONCE_NOCHECK(regs->psw.addr);
		reliable = true;
	} else {
		sf = (struct stack_frame *) sp;
		ip = READ_ONCE_NOCHECK(sf->gprs[8]);
		reliable = false;
	}

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	/* Decode any ftrace redirection */
	if (ip == (unsigned long) return_to_handler)
		ip = ftrace_graph_ret_addr(state->task, &state->graph_idx,
					   ip, NULL);
#endif

	/* Update unwind state */
	state->sp = sp;
	state->ip = ip;
	state->reliable = reliable;
}
EXPORT_SYMBOL_GPL(__unwind_start);
