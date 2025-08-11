// SPDX-License-Identifier: GPL-2.0-only
/*
 * Stack tracing support
 *
 * Copyright (C) 2012 ARM Ltd.
 */
#include <linux/kernel.h>
#include <linux/efi.h>
#include <linux/export.h>
#include <linux/filter.h>
#include <linux/ftrace.h>
#include <linux/kprobes.h>
#include <linux/sched.h>
#include <linux/sched/debug.h>
#include <linux/sched/task_stack.h>
#include <linux/stacktrace.h>

#include <asm/efi.h>
#include <asm/irq.h>
#include <asm/stack_pointer.h>
#include <asm/stacktrace.h>

enum kunwind_source {
	KUNWIND_SOURCE_UNKNOWN,
	KUNWIND_SOURCE_FRAME,
	KUNWIND_SOURCE_CALLER,
	KUNWIND_SOURCE_TASK,
	KUNWIND_SOURCE_REGS_PC,
};

union unwind_flags {
	unsigned long	all;
	struct {
		unsigned long	fgraph : 1,
				kretprobe : 1;
	};
};

/*
 * Kernel unwind state
 *
 * @common:      Common unwind state.
 * @task:        The task being unwound.
 * @graph_idx:   Used by ftrace_graph_ret_addr() for optimized stack unwinding.
 * @kr_cur:      When KRETPROBES is selected, holds the kretprobe instance
 *               associated with the most recently encountered replacement lr
 *               value.
 */
struct kunwind_state {
	struct unwind_state common;
	struct task_struct *task;
	int graph_idx;
#ifdef CONFIG_KRETPROBES
	struct llist_node *kr_cur;
#endif
	enum kunwind_source source;
	union unwind_flags flags;
	struct pt_regs *regs;
};

static __always_inline void
kunwind_init(struct kunwind_state *state,
	     struct task_struct *task)
{
	unwind_init_common(&state->common);
	state->task = task;
	state->source = KUNWIND_SOURCE_UNKNOWN;
	state->flags.all = 0;
	state->regs = NULL;
}

/*
 * Start an unwind from a pt_regs.
 *
 * The unwind will begin at the PC within the regs.
 *
 * The regs must be on a stack currently owned by the calling task.
 */
static __always_inline void
kunwind_init_from_regs(struct kunwind_state *state,
		       struct pt_regs *regs)
{
	kunwind_init(state, current);

	state->regs = regs;
	state->common.fp = regs->regs[29];
	state->common.pc = regs->pc;
	state->source = KUNWIND_SOURCE_REGS_PC;
}

/*
 * Start an unwind from a caller.
 *
 * The unwind will begin at the caller of whichever function this is inlined
 * into.
 *
 * The function which invokes this must be noinline.
 */
static __always_inline void
kunwind_init_from_caller(struct kunwind_state *state)
{
	kunwind_init(state, current);

	state->common.fp = (unsigned long)__builtin_frame_address(1);
	state->common.pc = (unsigned long)__builtin_return_address(0);
	state->source = KUNWIND_SOURCE_CALLER;
}

/*
 * Start an unwind from a blocked task.
 *
 * The unwind will begin at the blocked tasks saved PC (i.e. the caller of
 * cpu_switch_to()).
 *
 * The caller should ensure the task is blocked in cpu_switch_to() for the
 * duration of the unwind, or the unwind will be bogus. It is never valid to
 * call this for the current task.
 */
static __always_inline void
kunwind_init_from_task(struct kunwind_state *state,
		       struct task_struct *task)
{
	kunwind_init(state, task);

	state->common.fp = thread_saved_fp(task);
	state->common.pc = thread_saved_pc(task);
	state->source = KUNWIND_SOURCE_TASK;
}

static __always_inline int
kunwind_recover_return_address(struct kunwind_state *state)
{
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	if (state->task->ret_stack &&
	    (state->common.pc == (unsigned long)return_to_handler)) {
		unsigned long orig_pc;
		orig_pc = ftrace_graph_ret_addr(state->task, &state->graph_idx,
						state->common.pc,
						(void *)state->common.fp);
		if (state->common.pc == orig_pc) {
			WARN_ON_ONCE(state->task == current);
			return -EINVAL;
		}
		state->common.pc = orig_pc;
		state->flags.fgraph = 1;
	}
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */

#ifdef CONFIG_KRETPROBES
	if (is_kretprobe_trampoline(state->common.pc)) {
		unsigned long orig_pc;
		orig_pc = kretprobe_find_ret_addr(state->task,
						  (void *)state->common.fp,
						  &state->kr_cur);
		if (!orig_pc)
			return -EINVAL;
		state->common.pc = orig_pc;
		state->flags.kretprobe = 1;
	}
#endif /* CONFIG_KRETPROBES */

	return 0;
}

static __always_inline
int kunwind_next_regs_pc(struct kunwind_state *state)
{
	struct stack_info *info;
	unsigned long fp = state->common.fp;
	struct pt_regs *regs;

	regs = container_of((u64 *)fp, struct pt_regs, stackframe.record.fp);

	info = unwind_find_stack(&state->common, (unsigned long)regs, sizeof(*regs));
	if (!info)
		return -EINVAL;

	unwind_consume_stack(&state->common, info, (unsigned long)regs,
			     sizeof(*regs));

	state->regs = regs;
	state->common.pc = regs->pc;
	state->common.fp = regs->regs[29];
	state->regs = NULL;
	state->source = KUNWIND_SOURCE_REGS_PC;
	return 0;
}

static __always_inline int
kunwind_next_frame_record_meta(struct kunwind_state *state)
{
	struct task_struct *tsk = state->task;
	unsigned long fp = state->common.fp;
	struct frame_record_meta *meta;
	struct stack_info *info;

	info = unwind_find_stack(&state->common, fp, sizeof(*meta));
	if (!info)
		return -EINVAL;

	meta = (struct frame_record_meta *)fp;
	switch (READ_ONCE(meta->type)) {
	case FRAME_META_TYPE_FINAL:
		if (meta == &task_pt_regs(tsk)->stackframe)
			return -ENOENT;
		WARN_ON_ONCE(tsk == current);
		return -EINVAL;
	case FRAME_META_TYPE_PT_REGS:
		return kunwind_next_regs_pc(state);
	default:
		WARN_ON_ONCE(tsk == current);
		return -EINVAL;
	}
}

static __always_inline int
kunwind_next_frame_record(struct kunwind_state *state)
{
	unsigned long fp = state->common.fp;
	struct frame_record *record;
	struct stack_info *info;
	unsigned long new_fp, new_pc;

	if (fp & 0x7)
		return -EINVAL;

	info = unwind_find_stack(&state->common, fp, sizeof(*record));
	if (!info)
		return -EINVAL;

	record = (struct frame_record *)fp;
	new_fp = READ_ONCE(record->fp);
	new_pc = READ_ONCE(record->lr);

	if (!new_fp && !new_pc)
		return kunwind_next_frame_record_meta(state);

	unwind_consume_stack(&state->common, info, fp, sizeof(*record));

	state->common.fp = new_fp;
	state->common.pc = new_pc;
	state->source = KUNWIND_SOURCE_FRAME;

	return 0;
}

/*
 * Unwind from one frame record (A) to the next frame record (B).
 *
 * We terminate early if the location of B indicates a malformed chain of frame
 * records (e.g. a cycle), determined based on the location and fp value of A
 * and the location (but not the fp value) of B.
 */
static __always_inline int
kunwind_next(struct kunwind_state *state)
{
	int err;

	state->flags.all = 0;

	switch (state->source) {
	case KUNWIND_SOURCE_FRAME:
	case KUNWIND_SOURCE_CALLER:
	case KUNWIND_SOURCE_TASK:
	case KUNWIND_SOURCE_REGS_PC:
		err = kunwind_next_frame_record(state);
		break;
	default:
		err = -EINVAL;
	}

	if (err)
		return err;

	state->common.pc = ptrauth_strip_kernel_insn_pac(state->common.pc);

	return kunwind_recover_return_address(state);
}

typedef bool (*kunwind_consume_fn)(const struct kunwind_state *state, void *cookie);

static __always_inline int
do_kunwind(struct kunwind_state *state, kunwind_consume_fn consume_state,
	   void *cookie)
{
	int ret;

	ret = kunwind_recover_return_address(state);
	if (ret)
		return ret;

	while (1) {
		if (!consume_state(state, cookie))
			return -EINVAL;
		ret = kunwind_next(state);
		if (ret == -ENOENT)
			return 0;
		if (ret < 0)
			return ret;
	}
}

/*
 * Per-cpu stacks are only accessible when unwinding the current task in a
 * non-preemptible context.
 */
#define STACKINFO_CPU(name)					\
	({							\
		((task == current) && !preemptible())		\
			? stackinfo_get_##name()		\
			: stackinfo_get_unknown();		\
	})

/*
 * SDEI stacks are only accessible when unwinding the current task in an NMI
 * context.
 */
#define STACKINFO_SDEI(name)					\
	({							\
		((task == current) && in_nmi())			\
			? stackinfo_get_sdei_##name()		\
			: stackinfo_get_unknown();		\
	})

#define STACKINFO_EFI						\
	({							\
		((task == current) && current_in_efi())		\
			? stackinfo_get_efi()			\
			: stackinfo_get_unknown();		\
	})

static __always_inline int
kunwind_stack_walk(kunwind_consume_fn consume_state,
		   void *cookie, struct task_struct *task,
		   struct pt_regs *regs)
{
	struct stack_info stacks[] = {
		stackinfo_get_task(task),
		STACKINFO_CPU(irq),
		STACKINFO_CPU(overflow),
#if defined(CONFIG_ARM_SDE_INTERFACE)
		STACKINFO_SDEI(normal),
		STACKINFO_SDEI(critical),
#endif
#ifdef CONFIG_EFI
		STACKINFO_EFI,
#endif
	};
	struct kunwind_state state = {
		.common = {
			.stacks = stacks,
			.nr_stacks = ARRAY_SIZE(stacks),
		},
	};

	if (regs) {
		if (task != current)
			return -EINVAL;
		kunwind_init_from_regs(&state, regs);
	} else if (task == current) {
		kunwind_init_from_caller(&state);
	} else {
		kunwind_init_from_task(&state, task);
	}

	return do_kunwind(&state, consume_state, cookie);
}

struct kunwind_consume_entry_data {
	stack_trace_consume_fn consume_entry;
	void *cookie;
};

static __always_inline bool
arch_kunwind_consume_entry(const struct kunwind_state *state, void *cookie)
{
	struct kunwind_consume_entry_data *data = cookie;
	return data->consume_entry(data->cookie, state->common.pc);
}

noinline noinstr void arch_stack_walk(stack_trace_consume_fn consume_entry,
			      void *cookie, struct task_struct *task,
			      struct pt_regs *regs)
{
	struct kunwind_consume_entry_data data = {
		.consume_entry = consume_entry,
		.cookie = cookie,
	};

	kunwind_stack_walk(arch_kunwind_consume_entry, &data, task, regs);
}

static __always_inline bool
arch_reliable_kunwind_consume_entry(const struct kunwind_state *state, void *cookie)
{
	/*
	 * At an exception boundary we can reliably consume the saved PC. We do
	 * not know whether the LR was live when the exception was taken, and
	 * so we cannot perform the next unwind step reliably.
	 *
	 * All that matters is whether the *entire* unwind is reliable, so give
	 * up as soon as we hit an exception boundary.
	 */
	if (state->source == KUNWIND_SOURCE_REGS_PC)
		return false;

	return arch_kunwind_consume_entry(state, cookie);
}

noinline noinstr int arch_stack_walk_reliable(stack_trace_consume_fn consume_entry,
					      void *cookie,
					      struct task_struct *task)
{
	struct kunwind_consume_entry_data data = {
		.consume_entry = consume_entry,
		.cookie = cookie,
	};

	return kunwind_stack_walk(arch_reliable_kunwind_consume_entry, &data,
				  task, NULL);
}

struct bpf_unwind_consume_entry_data {
	bool (*consume_entry)(void *cookie, u64 ip, u64 sp, u64 fp);
	void *cookie;
};

static bool
arch_bpf_unwind_consume_entry(const struct kunwind_state *state, void *cookie)
{
	struct bpf_unwind_consume_entry_data *data = cookie;

	return data->consume_entry(data->cookie, state->common.pc, 0,
				   state->common.fp);
}

noinline noinstr void arch_bpf_stack_walk(bool (*consume_entry)(void *cookie, u64 ip, u64 sp,
								u64 fp), void *cookie)
{
	struct bpf_unwind_consume_entry_data data = {
		.consume_entry = consume_entry,
		.cookie = cookie,
	};

	kunwind_stack_walk(arch_bpf_unwind_consume_entry, &data, current, NULL);
}

static const char *state_source_string(const struct kunwind_state *state)
{
	switch (state->source) {
	case KUNWIND_SOURCE_FRAME:	return NULL;
	case KUNWIND_SOURCE_CALLER:	return "C";
	case KUNWIND_SOURCE_TASK:	return "T";
	case KUNWIND_SOURCE_REGS_PC:	return "P";
	default:			return "U";
	}
}

static bool dump_backtrace_entry(const struct kunwind_state *state, void *arg)
{
	const char *source = state_source_string(state);
	union unwind_flags flags = state->flags;
	bool has_info = source || flags.all;
	char *loglvl = arg;

	printk("%s %pSb%s%s%s%s%s\n", loglvl,
		(void *)state->common.pc,
		has_info ? " (" : "",
		source ? source : "",
		flags.fgraph ? "F" : "",
		flags.kretprobe ? "K" : "",
		has_info ? ")" : "");

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
	kunwind_stack_walk(dump_backtrace_entry, (void *)loglvl, tsk, regs);

	put_task_stack(tsk);
}

void show_stack(struct task_struct *tsk, unsigned long *sp, const char *loglvl)
{
	dump_backtrace(NULL, tsk, loglvl);
	barrier();
}

/*
 * The struct defined for userspace stack frame in AARCH64 mode.
 */
struct frame_tail {
	struct frame_tail	__user *fp;
	unsigned long		lr;
} __attribute__((packed));

/*
 * Get the return address for a single stackframe and return a pointer to the
 * next frame tail.
 */
static struct frame_tail __user *
unwind_user_frame(struct frame_tail __user *tail, void *cookie,
	       stack_trace_consume_fn consume_entry)
{
	struct frame_tail buftail;
	unsigned long err;
	unsigned long lr;

	/* Also check accessibility of one struct frame_tail beyond */
	if (!access_ok(tail, sizeof(buftail)))
		return NULL;

	pagefault_disable();
	err = __copy_from_user_inatomic(&buftail, tail, sizeof(buftail));
	pagefault_enable();

	if (err)
		return NULL;

	lr = ptrauth_strip_user_insn_pac(buftail.lr);

	if (!consume_entry(cookie, lr))
		return NULL;

	/*
	 * Frame pointers should strictly progress back up the stack
	 * (towards higher addresses).
	 */
	if (tail >= buftail.fp)
		return NULL;

	return buftail.fp;
}

#ifdef CONFIG_COMPAT
/*
 * The registers we're interested in are at the end of the variable
 * length saved register structure. The fp points at the end of this
 * structure so the address of this struct is:
 * (struct compat_frame_tail *)(xxx->fp)-1
 *
 * This code has been adapted from the ARM OProfile support.
 */
struct compat_frame_tail {
	compat_uptr_t	fp; /* a (struct compat_frame_tail *) in compat mode */
	u32		sp;
	u32		lr;
} __attribute__((packed));

static struct compat_frame_tail __user *
unwind_compat_user_frame(struct compat_frame_tail __user *tail, void *cookie,
				stack_trace_consume_fn consume_entry)
{
	struct compat_frame_tail buftail;
	unsigned long err;

	/* Also check accessibility of one struct frame_tail beyond */
	if (!access_ok(tail, sizeof(buftail)))
		return NULL;

	pagefault_disable();
	err = __copy_from_user_inatomic(&buftail, tail, sizeof(buftail));
	pagefault_enable();

	if (err)
		return NULL;

	if (!consume_entry(cookie, buftail.lr))
		return NULL;

	/*
	 * Frame pointers should strictly progress back up the stack
	 * (towards higher addresses).
	 */
	if (tail + 1 >= (struct compat_frame_tail __user *)
			compat_ptr(buftail.fp))
		return NULL;

	return (struct compat_frame_tail __user *)compat_ptr(buftail.fp) - 1;
}
#endif /* CONFIG_COMPAT */


void arch_stack_walk_user(stack_trace_consume_fn consume_entry, void *cookie,
					const struct pt_regs *regs)
{
	if (!consume_entry(cookie, regs->pc))
		return;

	if (!compat_user_mode(regs)) {
		/* AARCH64 mode */
		struct frame_tail __user *tail;

		tail = (struct frame_tail __user *)regs->regs[29];
		while (tail && !((unsigned long)tail & 0x7))
			tail = unwind_user_frame(tail, cookie, consume_entry);
	} else {
#ifdef CONFIG_COMPAT
		/* AARCH32 compat mode */
		struct compat_frame_tail __user *tail;

		tail = (struct compat_frame_tail __user *)regs->compat_fp - 1;
		while (tail && !((unsigned long)tail & 0x3))
			tail = unwind_compat_user_frame(tail, cookie, consume_entry);
#endif
	}
}
