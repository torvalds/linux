/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Author: Huacai Chen <chenhuacai@loongson.cn>
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#ifndef _ASM_FPU_H
#define _ASM_FPU_H

#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/ptrace.h>
#include <linux/thread_info.h>
#include <linux/bitops.h>

#include <asm/cpu.h>
#include <asm/cpu-features.h>
#include <asm/current.h>
#include <asm/loongarch.h>
#include <asm/processor.h>
#include <asm/ptrace.h>

struct sigcontext;

extern void _init_fpu(unsigned int);
extern void _save_fp(struct loongarch_fpu *);
extern void _restore_fp(struct loongarch_fpu *);

/*
 * Mask the FCSR Cause bits according to the Enable bits, observing
 * that Unimplemented is always enabled.
 */
static inline unsigned long mask_fcsr_x(unsigned long fcsr)
{
	return fcsr & ((fcsr & FPU_CSR_ALL_E) <<
			(ffs(FPU_CSR_ALL_X) - ffs(FPU_CSR_ALL_E)));
}

static inline int is_fp_enabled(void)
{
	return (csr_read32(LOONGARCH_CSR_EUEN) & CSR_EUEN_FPEN) ?
		1 : 0;
}

#define enable_fpu()		set_csr_euen(CSR_EUEN_FPEN)

#define disable_fpu()		clear_csr_euen(CSR_EUEN_FPEN)

#define clear_fpu_owner()	clear_thread_flag(TIF_USEDFPU)

static inline int is_fpu_owner(void)
{
	return test_thread_flag(TIF_USEDFPU);
}

static inline void __own_fpu(void)
{
	enable_fpu();
	set_thread_flag(TIF_USEDFPU);
	KSTK_EUEN(current) |= CSR_EUEN_FPEN;
}

static inline void own_fpu_inatomic(int restore)
{
	if (cpu_has_fpu && !is_fpu_owner()) {
		__own_fpu();
		if (restore)
			_restore_fp(&current->thread.fpu);
	}
}

static inline void own_fpu(int restore)
{
	preempt_disable();
	own_fpu_inatomic(restore);
	preempt_enable();
}

static inline void lose_fpu_inatomic(int save, struct task_struct *tsk)
{
	if (is_fpu_owner()) {
		if (save)
			_save_fp(&tsk->thread.fpu);
		disable_fpu();
		clear_tsk_thread_flag(tsk, TIF_USEDFPU);
	}
	KSTK_EUEN(tsk) &= ~(CSR_EUEN_FPEN | CSR_EUEN_LSXEN | CSR_EUEN_LASXEN);
}

static inline void lose_fpu(int save)
{
	preempt_disable();
	lose_fpu_inatomic(save, current);
	preempt_enable();
}

static inline void init_fpu(void)
{
	unsigned int fcsr = current->thread.fpu.fcsr;

	__own_fpu();
	_init_fpu(fcsr);
	set_used_math();
}

static inline void save_fp(struct task_struct *tsk)
{
	if (cpu_has_fpu)
		_save_fp(&tsk->thread.fpu);
}

static inline void restore_fp(struct task_struct *tsk)
{
	if (cpu_has_fpu)
		_restore_fp(&tsk->thread.fpu);
}

static inline union fpureg *get_fpu_regs(struct task_struct *tsk)
{
	if (tsk == current) {
		preempt_disable();
		if (is_fpu_owner())
			_save_fp(&current->thread.fpu);
		preempt_enable();
	}

	return tsk->thread.fpu.fpr;
}

#endif /* _ASM_FPU_H */
