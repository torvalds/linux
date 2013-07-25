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

struct task_struct;

/*
 * switch_to(n) should switch tasks to task nr n, first
 * checking that n isn't the current task, in which case it does nothing.
 */
extern asmlinkage void *resume(void *last, void *next, void *next_ti, u32 __usedfpu);

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

#define __clear_software_ll_bit()					\
do {									\
	if (!__builtin_constant_p(cpu_has_llsc) || !cpu_has_llsc)	\
		ll_bit = 0;						\
} while (0)

#define switch_to(prev, next, last)					\
do {									\
	u32 __usedfpu, __c0_stat;					\
	__mips_mt_fpaff_switch_to(prev);				\
	if (cpu_has_dsp)						\
		__save_dsp(prev);					\
	if (cop2_present && (KSTK_STATUS(prev) & ST0_CU2)) {		\
		if (cop2_lazy_restore)					\
			KSTK_STATUS(prev) &= ~ST0_CU2;			\
		__c0_stat = read_c0_status();				\
		write_c0_status(__c0_stat | ST0_CU2);			\
		cop2_save(&prev->thread.cp2);				\
		write_c0_status(__c0_stat & ~ST0_CU2);			\
	}								\
	__clear_software_ll_bit();					\
	__usedfpu = test_and_clear_tsk_thread_flag(prev, TIF_USEDFPU);	\
	(last) = resume(prev, next, task_thread_info(next), __usedfpu); \
} while (0)

#define finish_arch_switch(prev)					\
do {									\
	u32 __c0_stat;							\
	if (cop2_present && !cop2_lazy_restore &&			\
			(KSTK_STATUS(current) & ST0_CU2)) {		\
		__c0_stat = read_c0_status();				\
		write_c0_status(__c0_stat | ST0_CU2);			\
		cop2_restore(&current->thread.cp2);			\
		write_c0_status(__c0_stat & ~ST0_CU2);			\
	}								\
	if (cpu_has_dsp)						\
		__restore_dsp(current);					\
	if (cpu_has_userlocal)						\
		write_c0_userlocal(current_thread_info()->tp_value);	\
	__restore_watch();						\
} while (0)

#endif /* _ASM_SWITCH_TO_H */
