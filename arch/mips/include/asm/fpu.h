/*
 * Copyright (C) 2002 MontaVista Software Inc.
 * Author: Jun Sun, jsun@mvista.com or jsun@junsun.net
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#ifndef _ASM_FPU_H
#define _ASM_FPU_H

#include <linux/sched.h>
#include <linux/thread_info.h>
#include <linux/bitops.h>

#include <asm/mipsregs.h>
#include <asm/cpu.h>
#include <asm/cpu-features.h>
#include <asm/fpu_emulator.h>
#include <asm/hazards.h>
#include <asm/processor.h>
#include <asm/current.h>
#include <asm/msa.h>

#ifdef CONFIG_MIPS_MT_FPAFF
#include <asm/mips_mt.h>
#endif

struct sigcontext;
struct sigcontext32;

extern void _init_fpu(unsigned int);
extern void _save_fp(struct task_struct *);
extern void _restore_fp(struct task_struct *);

/*
 * This enum specifies a mode in which we want the FPU to operate, for cores
 * which implement the Status.FR bit. Note that the bottom bit of the value
 * purposefully matches the desired value of the Status.FR bit.
 */
enum fpu_mode {
	FPU_32BIT = 0,		/* FR = 0 */
	FPU_64BIT,		/* FR = 1, FRE = 0 */
	FPU_AS_IS,
	FPU_HYBRID,		/* FR = 1, FRE = 1 */

#define FPU_FR_MASK		0x1
};

#define __disable_fpu()							\
do {									\
	clear_c0_status(ST0_CU1);					\
	disable_fpu_hazard();						\
} while (0)

static inline int __enable_fpu(enum fpu_mode mode)
{
	int fr;

	switch (mode) {
	case FPU_AS_IS:
		/* just enable the FPU in its current mode */
		set_c0_status(ST0_CU1);
		enable_fpu_hazard();
		return 0;

	case FPU_HYBRID:
		if (!cpu_has_fre)
			return SIGFPE;

		/* set FRE */
		set_c0_config5(MIPS_CONF5_FRE);
		goto fr_common;

	case FPU_64BIT:
#if !(defined(CONFIG_CPU_MIPSR2) || defined(CONFIG_CPU_MIPSR6) \
      || defined(CONFIG_64BIT))
		/* we only have a 32-bit FPU */
		return SIGFPE;
#endif
		/* fall through */
	case FPU_32BIT:
		if (cpu_has_fre) {
			/* clear FRE */
			clear_c0_config5(MIPS_CONF5_FRE);
		}
fr_common:
		/* set CU1 & change FR appropriately */
		fr = (int)mode & FPU_FR_MASK;
		change_c0_status(ST0_CU1 | ST0_FR, ST0_CU1 | (fr ? ST0_FR : 0));
		enable_fpu_hazard();

		/* check FR has the desired value */
		if (!!(read_c0_status() & ST0_FR) == !!fr)
			return 0;

		/* unsupported FR value */
		__disable_fpu();
		return SIGFPE;

	default:
		BUG();
	}

	return SIGFPE;
}

#define clear_fpu_owner()	clear_thread_flag(TIF_USEDFPU)

static inline int __is_fpu_owner(void)
{
	return test_thread_flag(TIF_USEDFPU);
}

static inline int is_fpu_owner(void)
{
	return cpu_has_fpu && __is_fpu_owner();
}

static inline int __own_fpu(void)
{
	enum fpu_mode mode;
	int ret;

	if (test_thread_flag(TIF_HYBRID_FPREGS))
		mode = FPU_HYBRID;
	else
		mode = !test_thread_flag(TIF_32BIT_FPREGS);

	ret = __enable_fpu(mode);
	if (ret)
		return ret;

	KSTK_STATUS(current) |= ST0_CU1;
	if (mode == FPU_64BIT || mode == FPU_HYBRID)
		KSTK_STATUS(current) |= ST0_FR;
	else /* mode == FPU_32BIT */
		KSTK_STATUS(current) &= ~ST0_FR;

	set_thread_flag(TIF_USEDFPU);
	return 0;
}

static inline int own_fpu_inatomic(int restore)
{
	int ret = 0;

	if (cpu_has_fpu && !__is_fpu_owner()) {
		ret = __own_fpu();
		if (restore && !ret)
			_restore_fp(current);
	}
	return ret;
}

static inline int own_fpu(int restore)
{
	int ret;

	preempt_disable();
	ret = own_fpu_inatomic(restore);
	preempt_enable();
	return ret;
}

static inline void lose_fpu_inatomic(int save, struct task_struct *tsk)
{
	if (is_msa_enabled()) {
		if (save) {
			save_msa(tsk);
			tsk->thread.fpu.fcr31 =
					read_32bit_cp1_register(CP1_STATUS);
		}
		disable_msa();
		clear_tsk_thread_flag(tsk, TIF_USEDMSA);
		__disable_fpu();
	} else if (is_fpu_owner()) {
		if (save)
			_save_fp(tsk);
		__disable_fpu();
	}
	KSTK_STATUS(tsk) &= ~ST0_CU1;
	clear_tsk_thread_flag(tsk, TIF_USEDFPU);
}

static inline void lose_fpu(int save)
{
	preempt_disable();
	lose_fpu_inatomic(save, current);
	preempt_enable();
}

static inline int init_fpu(void)
{
	unsigned int fcr31 = current->thread.fpu.fcr31;
	int ret = 0;

	if (cpu_has_fpu) {
		unsigned int config5;

		ret = __own_fpu();
		if (ret)
			return ret;

		if (!cpu_has_fre) {
			_init_fpu(fcr31);

			return 0;
		}

		/*
		 * Ensure FRE is clear whilst running _init_fpu, since
		 * single precision FP instructions are used. If FRE
		 * was set then we'll just end up initialising all 32
		 * 64b registers.
		 */
		config5 = clear_c0_config5(MIPS_CONF5_FRE);
		enable_fpu_hazard();

		_init_fpu(fcr31);

		/* Restore FRE */
		write_c0_config5(config5);
		enable_fpu_hazard();
	} else
		fpu_emulator_init_fpu();

	return ret;
}

static inline void save_fp(struct task_struct *tsk)
{
	if (cpu_has_fpu)
		_save_fp(tsk);
}

static inline void restore_fp(struct task_struct *tsk)
{
	if (cpu_has_fpu)
		_restore_fp(tsk);
}

static inline union fpureg *get_fpu_regs(struct task_struct *tsk)
{
	if (tsk == current) {
		preempt_disable();
		if (is_fpu_owner())
			_save_fp(current);
		preempt_enable();
	}

	return tsk->thread.fpu.fpr;
}

#endif /* _ASM_FPU_H */
