/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_UNWIND_H
#define _ASM_X86_UNWIND_H

#include <linux/sched.h>
#include <linux/ftrace.h>
#include <asm/ptrace.h>
#include <asm/stacktrace.h>

#define IRET_FRAME_OFFSET (offsetof(struct pt_regs, ip))
#define IRET_FRAME_SIZE   (sizeof(struct pt_regs) - IRET_FRAME_OFFSET)

struct unwind_state {
	struct stack_info stack_info;
	unsigned long stack_mask;
	struct task_struct *task;
	int graph_idx;
	bool error;
#if defined(CONFIG_UNWINDER_ORC)
	bool signal, full_regs;
	unsigned long sp, bp, ip;
	struct pt_regs *regs, *prev_regs;
#elif defined(CONFIG_UNWINDER_FRAME_POINTER)
	bool got_irq;
	unsigned long *bp, *orig_sp, ip;
	/*
	 * If non-NULL: The current frame is incomplete and doesn't contain a
	 * valid BP. When looking for the next frame, use this instead of the
	 * non-existent saved BP.
	 */
	unsigned long *next_bp;
	struct pt_regs *regs;
#else
	unsigned long *sp;
#endif
};

void __unwind_start(struct unwind_state *state, struct task_struct *task,
		    struct pt_regs *regs, unsigned long *first_frame);
bool unwind_next_frame(struct unwind_state *state);
unsigned long unwind_get_return_address(struct unwind_state *state);
unsigned long *unwind_get_return_address_ptr(struct unwind_state *state);

static inline bool unwind_done(struct unwind_state *state)
{
	return state->stack_info.type == STACK_TYPE_UNKNOWN;
}

static inline bool unwind_error(struct unwind_state *state)
{
	return state->error;
}

static inline
void unwind_start(struct unwind_state *state, struct task_struct *task,
		  struct pt_regs *regs, unsigned long *first_frame)
{
	first_frame = first_frame ? : get_stack_pointer(task, regs);

	__unwind_start(state, task, regs, first_frame);
}

#if defined(CONFIG_UNWINDER_ORC) || defined(CONFIG_UNWINDER_FRAME_POINTER)
/*
 * If 'partial' returns true, only the iret frame registers are valid.
 */
static inline struct pt_regs *unwind_get_entry_regs(struct unwind_state *state,
						    bool *partial)
{
	if (unwind_done(state))
		return NULL;

	if (partial) {
#ifdef CONFIG_UNWINDER_ORC
		*partial = !state->full_regs;
#else
		*partial = false;
#endif
	}

	return state->regs;
}
#else
static inline struct pt_regs *unwind_get_entry_regs(struct unwind_state *state,
						    bool *partial)
{
	return NULL;
}
#endif

#ifdef CONFIG_UNWINDER_ORC
void unwind_init(void);
void unwind_module_init(struct module *mod, void *orc_ip, size_t orc_ip_size,
			void *orc, size_t orc_size);
#else
static inline void unwind_init(void) {}
static inline
void unwind_module_init(struct module *mod, void *orc_ip, size_t orc_ip_size,
			void *orc, size_t orc_size) {}
#endif

/*
 * This disables KASAN checking when reading a value from another task's stack,
 * since the other task could be running on another CPU and could have poisoned
 * the stack in the meantime.
 */
#define READ_ONCE_TASK_STACK(task, x)			\
({							\
	unsigned long val;				\
	if (task == current)				\
		val = READ_ONCE(x);			\
	else						\
		val = READ_ONCE_NOCHECK(x);		\
	val;						\
})

static inline bool task_on_another_cpu(struct task_struct *task)
{
#ifdef CONFIG_SMP
	return task != current && task->on_cpu;
#else
	return false;
#endif
}

#endif /* _ASM_X86_UNWIND_H */
