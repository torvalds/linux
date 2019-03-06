/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2005-2018 Andes Technology Corporation */

#ifndef __ASM_NDS32_FPU_H
#define __ASM_NDS32_FPU_H

#if IS_ENABLED(CONFIG_FPU)
#ifndef __ASSEMBLY__
#include <linux/sched/task_stack.h>
#include <linux/preempt.h>
#include <asm/ptrace.h>

extern bool has_fpu;

extern void save_fpu(struct task_struct *__tsk);
extern void load_fpu(const struct fpu_struct *fpregs);
extern bool do_fpu_exception(unsigned int subtype, struct pt_regs *regs);
extern int do_fpuemu(struct pt_regs *regs, struct fpu_struct *fpu);

#define test_tsk_fpu(regs)	(regs->fucop_ctl & FUCOP_CTL_mskCP0EN)

/*
 * Initially load the FPU with signalling NANS.  This bit pattern
 * has the property that no matter whether considered as single or as
 * double precision, it still represents a signalling NAN.
 */

#define sNAN64    0xFFFFFFFFFFFFFFFFULL
#define sNAN32    0xFFFFFFFFUL

#if IS_ENABLED(CONFIG_SUPPORT_DENORMAL_ARITHMETIC)
/*
 * Denormalized number is unsupported by nds32 FPU. Hence the operation
 * is treated as underflow cases when the final result is a denormalized
 * number. To enhance precision, underflow exception trap should be
 * enabled by default and kerenl will re-execute it by fpu emulator
 * when getting underflow exception.
 */
#define FPCSR_INIT  FPCSR_mskUDFE
#else
#define FPCSR_INIT  0x0UL
#endif

extern const struct fpu_struct init_fpuregs;

static inline void disable_ptreg_fpu(struct pt_regs *regs)
{
	regs->fucop_ctl &= ~FUCOP_CTL_mskCP0EN;
}

static inline void enable_ptreg_fpu(struct pt_regs *regs)
{
	regs->fucop_ctl |= FUCOP_CTL_mskCP0EN;
}

static inline void enable_fpu(void)
{
	unsigned long fucop_ctl;

	fucop_ctl = __nds32__mfsr(NDS32_SR_FUCOP_CTL) | FUCOP_CTL_mskCP0EN;
	__nds32__mtsr(fucop_ctl, NDS32_SR_FUCOP_CTL);
	__nds32__isb();
}

static inline void disable_fpu(void)
{
	unsigned long fucop_ctl;

	fucop_ctl = __nds32__mfsr(NDS32_SR_FUCOP_CTL) & ~FUCOP_CTL_mskCP0EN;
	__nds32__mtsr(fucop_ctl, NDS32_SR_FUCOP_CTL);
	__nds32__isb();
}

static inline void lose_fpu(void)
{
	preempt_disable();
#if IS_ENABLED(CONFIG_LAZY_FPU)
	if (last_task_used_math == current) {
		last_task_used_math = NULL;
#else
	if (test_tsk_fpu(task_pt_regs(current))) {
#endif
		save_fpu(current);
	}
	disable_ptreg_fpu(task_pt_regs(current));
	preempt_enable();
}

static inline void own_fpu(void)
{
	preempt_disable();
#if IS_ENABLED(CONFIG_LAZY_FPU)
	if (last_task_used_math != current) {
		if (last_task_used_math != NULL)
			save_fpu(last_task_used_math);
		load_fpu(&current->thread.fpu);
		last_task_used_math = current;
	}
#else
	if (!test_tsk_fpu(task_pt_regs(current))) {
		load_fpu(&current->thread.fpu);
	}
#endif
	enable_ptreg_fpu(task_pt_regs(current));
	preempt_enable();
}

#if !IS_ENABLED(CONFIG_LAZY_FPU)
static inline void unlazy_fpu(struct task_struct *tsk)
{
	preempt_disable();
	if (test_tsk_fpu(task_pt_regs(tsk)))
		save_fpu(tsk);
	preempt_enable();
}
#endif /* !CONFIG_LAZY_FPU */
static inline void clear_fpu(struct pt_regs *regs)
{
	preempt_disable();
	if (test_tsk_fpu(regs))
		disable_ptreg_fpu(regs);
	preempt_enable();
}
#endif /* CONFIG_FPU */
#endif /* __ASSEMBLY__ */
#endif /* __ASM_NDS32_FPU_H */
