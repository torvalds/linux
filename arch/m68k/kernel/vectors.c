/*
 *  vectors.c
 *
 *  Copyright (C) 1993, 1994 by Hamish Macdonald
 *
 *  68040 fixes by Michael Rausch
 *  68040 fixes by Martin Apel
 *  68040 fixes and writeback by Richard Zidlicky
 *  68060 fixes by Roman Hodek
 *  68060 fixes by Jesper Skov
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

/*
 * Sets up all exception vectors
 */
#include <linux/cpu.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/linkage.h>
#include <linux/init.h>
#include <linux/kallsyms.h>

#include <asm/setup.h>
#include <asm/fpu.h>
#include <asm/traps.h>

#include "vectors.h"

/* assembler routines */
asmlinkage void system_call(void);
asmlinkage void buserr(void);
asmlinkage void trap(void);
asmlinkage void nmihandler(void);
#ifdef CONFIG_M68KFPU_EMU
asmlinkage void fpu_emu(void);
#endif

#ifndef CONFIG_M68000
e_vector vectors[256];
#endif

/* nmi handler for the Amiga */
asm(".text\n"
    __ALIGN_STR "\n"
    "nmihandler: rte");

/*
 * this must be called very early as the kernel might
 * use some instruction that are emulated on the 060
 * and so we're prepared for early probe attempts (e.g. nf_init).
 */
void __init base_trap_init(void)
{
#ifndef CONFIG_M68000
	if (MACH_IS_SUN3X) {
		extern e_vector *sun3x_prom_vbr;

		__asm__ volatile ("movec %%vbr, %0" : "=r" (sun3x_prom_vbr));
	}

	/* setup the exception vector table */
	__asm__ volatile ("movec %0,%%vbr" : : "r" ((void*)vectors));

	if (CPU_IS_060) {
		/* set up ISP entry points */
		asmlinkage void unimp_vec(void) asm ("_060_isp_unimp");

		vectors[VEC_UNIMPII] = unimp_vec;
	}
#endif

	vectors[VEC_BUSERR] = buserr;
	vectors[VEC_ILLEGAL] = trap;
	vectors[VEC_SYS] = system_call;
}

void __init trap_init (void)
{
	int i;

	for (i = VEC_SPUR; i <= VEC_INT7; i++)
		vectors[i] = bad_inthandler;

#ifndef CONFIG_M68000
	for (i = 0; i < VEC_USER; i++)
		if (!vectors[i])
			vectors[i] = trap;
#else
	vectors[VEC_TRAP1] = trap1;
	vectors[VEC_TRAP2] = trap2;
	vectors[VEC_TRAP3] = trap3;
	vectors[VEC_TRAP4] = trap4;
	vectors[VEC_TRAP5] = trap5;
	vectors[VEC_TRAP6] = trap6;
	vectors[VEC_TRAP7] = trap7;
	vectors[VEC_TRAP8] = trap8;
	vectors[VEC_TRAP9] = trap9;
	vectors[VEC_TRAP10] = trap10;
	vectors[VEC_TRAP11] = trap11;
	vectors[VEC_TRAP12] = trap12;
	vectors[VEC_TRAP13] = trap13;
	vectors[VEC_TRAP14] = trap14;
	vectors[VEC_TRAP15] = trap15;
	vectors[VEC_ADDRERR] = trap_vec_addrerr;
	vectors[VEC_ILLEGAL] = trap_vec_illegal;
	vectors[VEC_LINE10] = trap_vec_line10;
	vectors[VEC_LINE11] = trap_vec_line11;
	vectors[VEC_PRIV] = trap_vec_priv;
	vectors[VEC_COPROC] = trap_vec_coproc;
	vectors[VEC_FPBRUC] = trap_vec_fpbruc;
	vectors[VEC_FPBRUC] = trap_vec_fpbruc;
	vectors[VEC_FPOE] = trap_vec_fpoe;
	vectors[VEC_FPNAN] = trap_vec_fpnan;
	vectors[VEC_FPIR] = trap_vec_fpir;
	vectors[VEC_FPDIVZ] = trap_vec_fpdivz;
	vectors[VEC_FPUNDER] = trap_vec_fpunder;
	vectors[VEC_FPOVER] = trap_vec_fpover;
	vectors[VEC_ZERODIV] = trap_vec_zerodiv;
	vectors[VEC_CHK] = trap_vec_chk;
	vectors[VEC_TRAP] = trap_vec_trap;
	vectors[VEC_TRACE] = trap_vec_trace;
#endif

	for (i = VEC_USER; i < 256; i++)
		vectors[i] = bad_inthandler;

#ifdef CONFIG_M68KFPU_EMU
	if (FPU_IS_EMU)
		vectors[VEC_LINE11] = fpu_emu;
#endif
#ifndef CONFIG_M68000
	if (CPU_IS_040 && !FPU_IS_EMU) {
		/* set up FPSP entry points */
		asmlinkage void dz_vec(void) asm ("dz");
		asmlinkage void inex_vec(void) asm ("inex");
		asmlinkage void ovfl_vec(void) asm ("ovfl");
		asmlinkage void unfl_vec(void) asm ("unfl");
		asmlinkage void snan_vec(void) asm ("snan");
		asmlinkage void operr_vec(void) asm ("operr");
		asmlinkage void bsun_vec(void) asm ("bsun");
		asmlinkage void fline_vec(void) asm ("fline");
		asmlinkage void unsupp_vec(void) asm ("unsupp");

		vectors[VEC_FPDIVZ] = dz_vec;
		vectors[VEC_FPIR] = inex_vec;
		vectors[VEC_FPOVER] = ovfl_vec;
		vectors[VEC_FPUNDER] = unfl_vec;
		vectors[VEC_FPNAN] = snan_vec;
		vectors[VEC_FPOE] = operr_vec;
		vectors[VEC_FPBRUC] = bsun_vec;
		vectors[VEC_LINE11] = fline_vec;
		vectors[VEC_FPUNSUP] = unsupp_vec;
	}

	if (CPU_IS_060 && !FPU_IS_EMU) {
		/* set up IFPSP entry points */
		asmlinkage void snan_vec6(void) asm ("_060_fpsp_snan");
		asmlinkage void operr_vec6(void) asm ("_060_fpsp_operr");
		asmlinkage void ovfl_vec6(void) asm ("_060_fpsp_ovfl");
		asmlinkage void unfl_vec6(void) asm ("_060_fpsp_unfl");
		asmlinkage void dz_vec6(void) asm ("_060_fpsp_dz");
		asmlinkage void inex_vec6(void) asm ("_060_fpsp_inex");
		asmlinkage void fline_vec6(void) asm ("_060_fpsp_fline");
		asmlinkage void unsupp_vec6(void) asm ("_060_fpsp_unsupp");
		asmlinkage void effadd_vec6(void) asm ("_060_fpsp_effadd");

		vectors[VEC_FPNAN] = snan_vec6;
		vectors[VEC_FPOE] = operr_vec6;
		vectors[VEC_FPOVER] = ovfl_vec6;
		vectors[VEC_FPUNDER] = unfl_vec6;
		vectors[VEC_FPDIVZ] = dz_vec6;
		vectors[VEC_FPIR] = inex_vec6;
		vectors[VEC_LINE11] = fline_vec6;
		vectors[VEC_FPUNSUP] = unsupp_vec6;
		vectors[VEC_UNIMPEA] = effadd_vec6;
	}

        /* if running on an amiga, make the NMI interrupt do nothing */
	if (MACH_IS_AMIGA) {
		vectors[VEC_INT7] = nmihandler;
	}
#endif
}

