/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994, 95, 96, 97, 98, 99, 2003, 06 by Ralf Baechle
 * Copyright (C) 1996 by Paul M. Antoine
 * Copyright (C) 1999 Silicon Graphics
 * Kevin D. Kissell, kevink@mips.org and Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 2000 MIPS Technologies, Inc.
 */
#ifndef _ASM_SWITCH_TO_H
#define _ASM_SWITCH_TO_H

#include <asm/cpu-features.h>
#include <asm/watch.h>
#include <asm/dsp.h>
#include <asm/cop2.h>
#include <asm/fpu.h>

struct task_struct;

/**
 * resume - resume execution of a task
 * @prev:	The task previously executed.
 * @next:	The task to begin executing.
 * @next_ti:	task_thread_info(next).
 *
 * This function is used whilst scheduling to save the context of prev & load
 * the context of next. Returns prev.
 */
extern asmlinkage struct task_struct *resume(struct task_struct *prev,
		struct task_struct *next, struct thread_info *next_ti);

extern unsigned int ll_bit;
extern struct task_struct *ll_task;

#ifdef CONFIG_MIPS_MT_FPAFF

/*
 * Handle the scheduler resume end of FPU affinity management.	We do this
 * inline to try to keep the overhead down. If we have been forced to run on
 * a "CPU" with an FPU because of a previous high level of FP computation,
 * but did not actually use the FPU during the most recent time-slice (CU1
 * isn't set), we undo the restriction on cpus_allowed.
 *
 * We're not calling set_cpus_allowed() here, because we have no need to
 * force prompt migration - we're already switching the current CPU to a
 * different thread.
 */

#define __mips_mt_fpaff_switch_to(prev)					\
do {									\
	struct thread_info *__prev_ti = task_thread_info(prev);		\
									\
	if (cpu_has_fpu &&						\
	    test_ti_thread_flag(__prev_ti, TIF_FPUBOUND) &&		\
	    (!(KSTK_STATUS(prev) & ST0_CU1))) {				\
		clear_ti_thread_flag(__prev_ti, TIF_FPUBOUND);		\
		prev->cpus_allowed = prev->thread.user_cpus_allowed;	\
	}								\
	next->thread.emulated_fp = 0;					\
} while(0)

#else
#define __mips_mt_fpaff_switch_to(prev) do { (void) (prev); } while (0)
#endif

/*
 * Clear LLBit during context switches on MIPSr6 such that eretnc can be used
 * unconditionally when returning to userland in entry.S.
 */
#define __clear_r6_hw_ll_bit() do {					\
	if (cpu_has_mips_r6)						\
		write_c0_lladdr(0);					\
} while (0)

#define __clear_software_ll_bit() do {					\
	if (!__builtin_constant_p(cpu_has_llsc) || !cpu_has_llsc)	\
		ll_bit = 0;						\
} while (0)

/*
 * Check FCSR for any unmasked exceptions pending set with `ptrace',
 * clear them and send a signal.
 */
#define __sanitize_fcr31(next)						\
do {									\
	unsigned long fcr31 = mask_fcr31_x(next->thread.fpu.fcr31);	\
	void __user *pc;						\
									\
	if (unlikely(fcr31)) {						\
		pc = (void __user *)task_pt_regs(next)->cp0_epc;	\
		next->thread.fpu.fcr31 &= ~fcr31;			\
		force_fcr31_sig(fcr31, pc, next);			\
	}								\
} while (0)

/*
 * For newly created kernel threads switch_to() will return to
 * ret_from_kernel_thread, newly created user threads to ret_from_fork.
 * That is, everything following resume() will be skipped for new threads.
 * So everything that matters to new threads should be placed before resume().
 */
#define switch_to(prev, next, last)					\
do {									\
	__mips_mt_fpaff_switch_to(prev);				\
	lose_fpu_inatomic(1, prev);					\
	if (tsk_used_math(next))					\
		__sanitize_fcr31(next);					\
	if (cpu_has_dsp) {						\
		__save_dsp(prev);					\
		__restore_dsp(next);					\
	}								\
	if (cop2_present) {						\
		set_c0_status(ST0_CU2);					\
		if ((KSTK_STATUS(prev) & ST0_CU2)) {			\
			if (cop2_lazy_restore)				\
				KSTK_STATUS(prev) &= ~ST0_CU2;		\
			cop2_save(prev);				\
		}							\
		if (KSTK_STATUS(next) & ST0_CU2 &&			\
		    !cop2_lazy_restore) {				\
			cop2_restore(next);				\
		}							\
		clear_c0_status(ST0_CU2);				\
	}								\
	__clear_r6_hw_ll_bit();						\
	__clear_software_ll_bit();					\
	if (cpu_has_userlocal)						\
		write_c0_userlocal(task_thread_info(next)->tp_value);	\
	__restore_watch(next);						\
	(last) = resume(prev, next, task_thread_info(next));		\
} while (0)

#endif /* _ASM_SWITCH_TO_H */
