/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Regents of the University of California
 */

#ifndef _ASM_RISCV_SWITCH_TO_H
#define _ASM_RISCV_SWITCH_TO_H

#include <linux/jump_label.h>
#include <linux/sched/task_stack.h>
#include <linux/mm_types.h>
#include <asm/vector.h>
#include <asm/cpufeature.h>
#include <asm/processor.h>
#include <asm/ptrace.h>
#include <asm/csr.h>

#ifdef CONFIG_FPU
extern void __fstate_save(struct task_struct *save_to);
extern void __fstate_restore(struct task_struct *restore_from);

static inline void __fstate_clean(struct pt_regs *regs)
{
	regs->status = (regs->status & ~SR_FS) | SR_FS_CLEAN;
}

static inline void fstate_off(struct task_struct *task,
			      struct pt_regs *regs)
{
	regs->status = (regs->status & ~SR_FS) | SR_FS_OFF;
}

static inline void fstate_save(struct task_struct *task,
			       struct pt_regs *regs)
{
	if ((regs->status & SR_FS) == SR_FS_DIRTY) {
		__fstate_save(task);
		__fstate_clean(regs);
	}
}

static inline void fstate_restore(struct task_struct *task,
				  struct pt_regs *regs)
{
	if ((regs->status & SR_FS) != SR_FS_OFF) {
		__fstate_restore(task);
		__fstate_clean(regs);
	}
}

static inline void __switch_to_fpu(struct task_struct *prev,
				   struct task_struct *next)
{
	struct pt_regs *regs;

	regs = task_pt_regs(prev);
	fstate_save(prev, regs);
	fstate_restore(next, task_pt_regs(next));
}

static __always_inline bool has_fpu(void)
{
	return riscv_has_extension_likely(RISCV_ISA_EXT_f) ||
		riscv_has_extension_likely(RISCV_ISA_EXT_d);
}
#else
static __always_inline bool has_fpu(void) { return false; }
#define fstate_save(task, regs) do { } while (0)
#define fstate_restore(task, regs) do { } while (0)
#define __switch_to_fpu(__prev, __next) do { } while (0)
#endif

static inline void envcfg_update_bits(struct task_struct *task,
				      unsigned long mask, unsigned long val)
{
	unsigned long envcfg;

	envcfg = (task->thread.envcfg & ~mask) | val;
	task->thread.envcfg = envcfg;
	if (task == current)
		csr_write(CSR_ENVCFG, envcfg);
}

static inline void __switch_to_envcfg(struct task_struct *next)
{
	asm volatile (ALTERNATIVE("nop", "csrw " __stringify(CSR_ENVCFG) ", %0",
				  0, RISCV_ISA_EXT_XLINUXENVCFG, 1)
			:: "r" (next->thread.envcfg) : "memory");
}

extern struct task_struct *__switch_to(struct task_struct *,
				       struct task_struct *);

static inline bool switch_to_should_flush_icache(struct task_struct *task)
{
#ifdef CONFIG_SMP
	bool stale_mm = task->mm && task->mm->context.force_icache_flush;
	bool stale_thread = task->thread.force_icache_flush;
	bool thread_migrated = smp_processor_id() != task->thread.prev_cpu;

	return thread_migrated && (stale_mm || stale_thread);
#else
	return false;
#endif
}

#ifdef CONFIG_SMP
#define __set_prev_cpu(thread) ((thread).prev_cpu = smp_processor_id())
#else
#define __set_prev_cpu(thread)
#endif

#define switch_to(prev, next, last)			\
do {							\
	struct task_struct *__prev = (prev);		\
	struct task_struct *__next = (next);		\
	__set_prev_cpu(__prev->thread);			\
	if (has_fpu())					\
		__switch_to_fpu(__prev, __next);	\
	if (has_vector() || has_xtheadvector())		\
		__switch_to_vector(__prev, __next);	\
	if (switch_to_should_flush_icache(__next))	\
		local_flush_icache_all();		\
	__switch_to_envcfg(__next);			\
	((last) = __switch_to(__prev, __next));		\
} while (0)

#endif /* _ASM_RISCV_SWITCH_TO_H */
