// SPDX-License-Identifier: GPL-2.0-only
/*
 * Stack tracing support
 *
 * Copyright (C) 2012 ARM Ltd.
 */
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/ftrace.h>
#include <linux/kprobes.h>
#include <linux/sched.h>
#include <linux/sched/debug.h>
#include <linux/sched/task_stack.h>
#include <linux/stacktrace.h>

#include <asm/irq.h>
#include <asm/pointer_auth.h>
#include <asm/stack_pointer.h>
#include <asm/stacktrace.h>

/*
 * A snapshot of a frame record or fp/lr register values, along with some
 * accounting information necessary for robust unwinding.
 *
 * @fp:          The fp value in the frame record (or the real fp)
 * @pc:          The lr value in the frame record (or the real lr)
 *
 * @stacks_done: Stacks which have been entirely unwound, for which it is no
 *               longer valid to unwind to.
 *
 * @prev_fp:     The fp that pointed to this frame record, or a synthetic value
 *               of 0. This is used to ensure that within a stack, each
 *               subsequent frame record is at an increasing address.
 * @prev_type:   The type of stack this frame record was on, or a synthetic
 *               value of STACK_TYPE_UNKNOWN. This is used to detect a
 *               transition from one stack to another.
 *
 * @kr_cur:      When KRETPROBES is selected, holds the kretprobe instance
 *               associated with the most recently encountered replacement lr
 *               value.
 */
struct unwind_state {
	unsigned long fp;
	unsigned long pc;
	DECLARE_BITMAP(stacks_done, __NR_STACK_TYPES);
	unsigned long prev_fp;
	enum stack_type prev_type;
#ifdef CONFIG_KRETPROBES
	struct llist_node *kr_cur;
#endif
};

static notrace void unwind_init(struct unwind_state *state, unsigned long fp,
				unsigned long pc)
{
	state->fp = fp;
	state->pc = pc;
#ifdef CONFIG_KRETPROBES
	state->kr_cur = NULL;
#endif

	/*
	 * Prime the first unwind.
	 *
	 * In unwind_next() we'll check that the FP points to a valid stack,
	 * which can't be STACK_TYPE_UNKNOWN, and the first unwind will be
	 * treated as a transition to whichever stack that happens to be. The
	 * prev_fp value won't be used, but we set it to 0 such that it is
	 * definitely not an accessible stack address.
	 */
	bitmap_zero(state->stacks_done, __NR_STACK_TYPES);
	state->prev_fp = 0;
	state->prev_type = STACK_TYPE_UNKNOWN;
}
NOKPROBE_SYMBOL(unwind_init);

/*
 * Unwind from one frame record (A) to the next frame record (B).
 *
 * We terminate early if the location of B indicates a malformed chain of frame
 * records (e.g. a cycle), determined based on the location and fp value of A
 * and the location (but not the fp value) of B.
 */
static int notrace unwind_next(struct task_struct *tsk,
			       struct unwind_state *state)
{
	unsigned long fp = state->fp;
	struct stack_info info;

	/* Final frame; nothing to unwind */
	if (fp == (unsigned long)task_pt_regs(tsk)->stackframe)
		return -ENOENT;

	if (fp & 0x7)
		return -EINVAL;

	if (!on_accessible_stack(tsk, fp, 16, &info))
		return -EINVAL;

	if (test_bit(info.type, state->stacks_done))
		return -EINVAL;

	/*
	 * As stacks grow downward, any valid record on the same stack must be
	 * at a strictly higher address than the prior record.
	 *
	 * Stacks can nest in several valid orders, e.g.
	 *
	 * TASK -> IRQ -> OVERFLOW -> SDEI_NORMAL
	 * TASK -> SDEI_NORMAL -> SDEI_CRITICAL -> OVERFLOW
	 *
	 * ... but the nesting itself is strict. Once we transition from one
	 * stack to another, it's never valid to unwind back to that first
	 * stack.
	 */
	if (info.type == state->prev_type) {
		if (fp <= state->prev_fp)
			return -EINVAL;
	} else {
		set_bit(state->prev_type, state->stacks_done);
	}

	/*
	 * Record this frame record's values and location. The prev_fp and
	 * prev_type are only meaningful to the next unwind_next() invocation.
	 */
	state->fp = READ_ONCE_NOCHECK(*(unsigned long *)(fp));
	state->pc = READ_ONCE_NOCHECK(*(unsigned long *)(fp + 8));
	state->prev_fp = fp;
	state->prev_type = info.type;

	state->pc = ptrauth_strip_insn_pac(state->pc);

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	if (tsk->ret_stack &&
		(state->pc == (unsigned long)return_to_handler)) {
		unsigned long orig_pc;
		/*
		 * This is a case where function graph tracer has
		 * modified a return address (LR) in a stack frame
		 * to hook a function return.
		 * So replace it to an original value.
		 */
		orig_pc = ftrace_graph_ret_addr(tsk, NULL, state->pc,
						(void *)state->fp);
		if (WARN_ON_ONCE(state->pc == orig_pc))
			return -EINVAL;
		state->pc = orig_pc;
	}
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */
#ifdef CONFIG_KRETPROBES
	if (is_kretprobe_trampoline(state->pc))
		state->pc = kretprobe_find_ret_addr(tsk, (void *)state->fp, &state->kr_cur);
#endif

	return 0;
}
NOKPROBE_SYMBOL(unwind_next);

static void notrace unwind(struct task_struct *tsk,
			   struct unwind_state *state,
			   stack_trace_consume_fn consume_entry, void *cookie)
{
	while (1) {
		int ret;

		if (!consume_entry(cookie, state->pc))
			break;
		ret = unwind_next(tsk, state);
		if (ret < 0)
			break;
	}
}
NOKPROBE_SYMBOL(unwind);

static bool dump_backtrace_entry(void *arg, unsigned long where)
{
	char *loglvl = arg;
	printk("%s %pSb\n", loglvl, (void *)where);
	return true;
}

void dump_backtrace(struct pt_regs *regs, struct task_struct *tsk,
		    const char *loglvl)
{
	pr_debug("%s(regs = %p tsk = %p)\n", __func__, regs, tsk);

	if (regs && user_mode(regs))
		return;

	if (!tsk)
		tsk = current;

	if (!try_get_task_stack(tsk))
		return;

	printk("%sCall trace:\n", loglvl);
	arch_stack_walk(dump_backtrace_entry, (void *)loglvl, tsk, regs);

	put_task_stack(tsk);
}

void show_stack(struct task_struct *tsk, unsigned long *sp, const char *loglvl)
{
	dump_backtrace(NULL, tsk, loglvl);
	barrier();
}

noinline notrace void arch_stack_walk(stack_trace_consume_fn consume_entry,
			      void *cookie, struct task_struct *task,
			      struct pt_regs *regs)
{
	struct unwind_state state;

	if (regs)
		unwind_init(&state, regs->regs[29], regs->pc);
	else if (task == current)
		unwind_init(&state,
				(unsigned long)__builtin_frame_address(1),
				(unsigned long)__builtin_return_address(0));
	else
		unwind_init(&state, thread_saved_fp(task),
				thread_saved_pc(task));

	unwind(task, &state, consume_entry, cookie);
}
