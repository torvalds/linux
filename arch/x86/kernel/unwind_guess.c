#include <linux/sched.h>
#include <linux/ftrace.h>
#include <asm/ptrace.h>
#include <asm/bitops.h>
#include <asm/stacktrace.h>
#include <asm/unwind.h>

unsigned long unwind_get_return_address(struct unwind_state *state)
{
	unsigned long addr;

	if (unwind_done(state))
		return 0;

	addr = READ_ONCE_NOCHECK(*state->sp);

	return ftrace_graph_ret_addr(state->task, &state->graph_idx,
				     addr, state->sp);
}
EXPORT_SYMBOL_GPL(unwind_get_return_address);

bool unwind_next_frame(struct unwind_state *state)
{
	struct stack_info *info = &state->stack_info;

	if (unwind_done(state))
		return false;

	do {
		for (state->sp++; state->sp < info->end; state->sp++) {
			unsigned long addr = READ_ONCE_NOCHECK(*state->sp);

			if (__kernel_text_address(addr))
				return true;
		}

		state->sp = info->next_sp;

	} while (!get_stack_info(state->sp, state->task, info,
				 &state->stack_mask));

	return false;
}
EXPORT_SYMBOL_GPL(unwind_next_frame);

void __unwind_start(struct unwind_state *state, struct task_struct *task,
		    struct pt_regs *regs, unsigned long *first_frame)
{
	memset(state, 0, sizeof(*state));

	state->task = task;
	state->sp   = first_frame;

	get_stack_info(first_frame, state->task, &state->stack_info,
		       &state->stack_mask);

	/*
	 * The caller can provide the address of the first frame directly
	 * (first_frame) or indirectly (regs->sp) to indicate which stack frame
	 * to start unwinding at.  Skip ahead until we reach it.
	 */
	if (!unwind_done(state) &&
	    (!on_stack(&state->stack_info, first_frame, sizeof(long)) ||
	    !__kernel_text_address(*first_frame)))
		unwind_next_frame(state);
}
EXPORT_SYMBOL_GPL(__unwind_start);
