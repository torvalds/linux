/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Most of this ideas comes from x86.
 *
 * Copyright (C) 2022 Loongson Technology Corporation Limited
 */
#ifndef _ASM_UNWIND_H
#define _ASM_UNWIND_H

#include <linux/sched.h>
#include <linux/ftrace.h>

#include <asm/ptrace.h>
#include <asm/stacktrace.h>

enum unwinder_type {
	UNWINDER_GUESS,
	UNWINDER_PROLOGUE,
	UNWINDER_ORC,
};

struct unwind_state {
	char type; /* UNWINDER_XXX */
	struct stack_info stack_info;
	struct task_struct *task;
	bool first, error, reset;
	int graph_idx;
	unsigned long sp, fp, pc, ra;
};

bool default_next_frame(struct unwind_state *state);

void unwind_start(struct unwind_state *state,
		  struct task_struct *task, struct pt_regs *regs);
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

#define GRAPH_FAKE_OFFSET (sizeof(struct pt_regs) - offsetof(struct pt_regs, regs[1]))

static inline unsigned long unwind_graph_addr(struct unwind_state *state,
					unsigned long pc, unsigned long cfa)
{
	return ftrace_graph_ret_addr(state->task, &state->graph_idx,
				     pc, (unsigned long *)(cfa - GRAPH_FAKE_OFFSET));
}

static __always_inline void __unwind_start(struct unwind_state *state,
					struct task_struct *task, struct pt_regs *regs)
{
	memset(state, 0, sizeof(*state));
	if (regs) {
		state->sp = regs->regs[3];
		state->pc = regs->csr_era;
		state->ra = regs->regs[1];
		state->fp = regs->regs[22];
	} else if (task && task != current) {
		state->sp = thread_saved_fp(task);
		state->pc = thread_saved_ra(task);
		state->ra = 0;
		state->fp = 0;
	} else {
		state->sp = (unsigned long)__builtin_frame_address(0);
		state->pc = (unsigned long)__builtin_return_address(0);
		state->ra = 0;
		state->fp = 0;
	}
	state->task = task;
	get_stack_info(state->sp, state->task, &state->stack_info);
	state->pc = unwind_graph_addr(state, state->pc, state->sp);
}

static __always_inline unsigned long __unwind_get_return_address(struct unwind_state *state)
{
	if (unwind_done(state))
		return 0;

	return __kernel_text_address(state->pc) ? state->pc : 0;
}

#ifdef CONFIG_UNWINDER_ORC
void unwind_init(void);
void unwind_module_init(struct module *mod, void *orc_ip, size_t orc_ip_size, void *orc, size_t orc_size);
#else
static inline void unwind_init(void) {}
static inline void unwind_module_init(struct module *mod, void *orc_ip, size_t orc_ip_size, void *orc, size_t orc_size) {}
#endif

#endif /* _ASM_UNWIND_H */
