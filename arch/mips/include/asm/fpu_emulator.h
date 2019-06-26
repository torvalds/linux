/*
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Further private data for which no space exists in mips_fpu_struct.
 * This should be subsumed into the mips_fpu_struct structure as
 * defined in processor.h as soon as the absurd wired absolute assembler
 * offsets become dynamic at compile time.
 *
 * Kevin D. Kissell, kevink@mips.com and Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 2000 MIPS Technologies, Inc.  All rights reserved.
 */
#ifndef _ASM_FPU_EMULATOR_H
#define _ASM_FPU_EMULATOR_H

#include <linux/sched.h>
#include <asm/dsemul.h>
#include <asm/thread_info.h>
#include <asm/inst.h>
#include <asm/local.h>
#include <asm/processor.h>

#ifdef CONFIG_DEBUG_FS

struct mips_fpu_emulator_stats {
	unsigned long emulated;
	unsigned long loads;
	unsigned long stores;
	unsigned long branches;
	unsigned long cp1ops;
	unsigned long cp1xops;
	unsigned long errors;
	unsigned long ieee754_inexact;
	unsigned long ieee754_underflow;
	unsigned long ieee754_overflow;
	unsigned long ieee754_zerodiv;
	unsigned long ieee754_invalidop;
	unsigned long ds_emul;

	unsigned long abs_s;
	unsigned long abs_d;
	unsigned long add_s;
	unsigned long add_d;
	unsigned long bc1eqz;
	unsigned long bc1nez;
	unsigned long ceil_w_s;
	unsigned long ceil_w_d;
	unsigned long ceil_l_s;
	unsigned long ceil_l_d;
	unsigned long class_s;
	unsigned long class_d;
	unsigned long cmp_af_s;
	unsigned long cmp_af_d;
	unsigned long cmp_eq_s;
	unsigned long cmp_eq_d;
	unsigned long cmp_le_s;
	unsigned long cmp_le_d;
	unsigned long cmp_lt_s;
	unsigned long cmp_lt_d;
	unsigned long cmp_ne_s;
	unsigned long cmp_ne_d;
	unsigned long cmp_or_s;
	unsigned long cmp_or_d;
	unsigned long cmp_ueq_s;
	unsigned long cmp_ueq_d;
	unsigned long cmp_ule_s;
	unsigned long cmp_ule_d;
	unsigned long cmp_ult_s;
	unsigned long cmp_ult_d;
	unsigned long cmp_un_s;
	unsigned long cmp_un_d;
	unsigned long cmp_une_s;
	unsigned long cmp_une_d;
	unsigned long cmp_saf_s;
	unsigned long cmp_saf_d;
	unsigned long cmp_seq_s;
	unsigned long cmp_seq_d;
	unsigned long cmp_sle_s;
	unsigned long cmp_sle_d;
	unsigned long cmp_slt_s;
	unsigned long cmp_slt_d;
	unsigned long cmp_sne_s;
	unsigned long cmp_sne_d;
	unsigned long cmp_sor_s;
	unsigned long cmp_sor_d;
	unsigned long cmp_sueq_s;
	unsigned long cmp_sueq_d;
	unsigned long cmp_sule_s;
	unsigned long cmp_sule_d;
	unsigned long cmp_sult_s;
	unsigned long cmp_sult_d;
	unsigned long cmp_sun_s;
	unsigned long cmp_sun_d;
	unsigned long cmp_sune_s;
	unsigned long cmp_sune_d;
	unsigned long cvt_d_l;
	unsigned long cvt_d_s;
	unsigned long cvt_d_w;
	unsigned long cvt_l_s;
	unsigned long cvt_l_d;
	unsigned long cvt_s_d;
	unsigned long cvt_s_l;
	unsigned long cvt_s_w;
	unsigned long cvt_w_s;
	unsigned long cvt_w_d;
	unsigned long div_s;
	unsigned long div_d;
	unsigned long floor_w_s;
	unsigned long floor_w_d;
	unsigned long floor_l_s;
	unsigned long floor_l_d;
	unsigned long maddf_s;
	unsigned long maddf_d;
	unsigned long max_s;
	unsigned long max_d;
	unsigned long maxa_s;
	unsigned long maxa_d;
	unsigned long min_s;
	unsigned long min_d;
	unsigned long mina_s;
	unsigned long mina_d;
	unsigned long mov_s;
	unsigned long mov_d;
	unsigned long msubf_s;
	unsigned long msubf_d;
	unsigned long mul_s;
	unsigned long mul_d;
	unsigned long neg_s;
	unsigned long neg_d;
	unsigned long recip_s;
	unsigned long recip_d;
	unsigned long rint_s;
	unsigned long rint_d;
	unsigned long round_w_s;
	unsigned long round_w_d;
	unsigned long round_l_s;
	unsigned long round_l_d;
	unsigned long rsqrt_s;
	unsigned long rsqrt_d;
	unsigned long sel_s;
	unsigned long sel_d;
	unsigned long seleqz_s;
	unsigned long seleqz_d;
	unsigned long selnez_s;
	unsigned long selnez_d;
	unsigned long sqrt_s;
	unsigned long sqrt_d;
	unsigned long sub_s;
	unsigned long sub_d;
	unsigned long trunc_w_s;
	unsigned long trunc_w_d;
	unsigned long trunc_l_s;
	unsigned long trunc_l_d;
};

DECLARE_PER_CPU(struct mips_fpu_emulator_stats, fpuemustats);

#define MIPS_FPU_EMU_INC_STATS(M)					\
do {									\
	preempt_disable();						\
	__this_cpu_inc(fpuemustats.M);					\
	preempt_enable();						\
} while (0)

#else
#define MIPS_FPU_EMU_INC_STATS(M) do { } while (0)
#endif /* CONFIG_DEBUG_FS */

extern int fpu_emulator_cop1Handler(struct pt_regs *xcp,
				    struct mips_fpu_struct *ctx, int has_fpu,
				    void __user **fault_addr);
void force_fcr31_sig(unsigned long fcr31, void __user *fault_addr,
		     struct task_struct *tsk);
int process_fpemu_return(int sig, void __user *fault_addr,
			 unsigned long fcr31);
int isBranchInstr(struct pt_regs *regs, struct mm_decoded_insn dec_insn,
		  unsigned long *contpc);
int mm_isBranchInstr(struct pt_regs *regs, struct mm_decoded_insn dec_insn,
		     unsigned long *contpc);

/*
 * Mask the FCSR Cause bits according to the Enable bits, observing
 * that Unimplemented is always enabled.
 */
static inline unsigned long mask_fcr31_x(unsigned long fcr31)
{
	return fcr31 & (FPU_CSR_UNI_X |
			((fcr31 & FPU_CSR_ALL_E) <<
			 (ffs(FPU_CSR_ALL_X) - ffs(FPU_CSR_ALL_E))));
}

#endif /* _ASM_FPU_EMULATOR_H */
