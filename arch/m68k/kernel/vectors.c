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
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/linkage.h>
#include <linux/init.h>
#include <linux/kallsyms.h>

#include <asm/setup.h>
#include <asm/fpu.h>
#include <asm/system.h>
#include <asm/traps.h>

/* assembler routines */
asmlinkage void system_call(void);
asmlinkage void buserr(void);
asmlinkage void trap(void);
asmlinkage void nmihandler(void);
#ifdef CONFIG_M68KFPU_EMU
asmlinkage void fpu_emu(void);
#endif

e_vector vectors[256];

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

	vectors[VEC_BUSERR] = buserr;
	vectors[VEC_ILLEGAL] = trap;
	vectors[VEC_SYS] = system_call;
}

void __init trap_init (void)
{
	int i;

	for (i = VEC_SPUR; i <= VEC_INT7; i++)
		vectors[i] = bad_inthandler;

	for (i = 0; i < VEC_USER; i++)
		if (!vectors[i])
			vectors[i] = trap;

	for (i = VEC_USER; i < 256; i++)
		vectors[i] = bad_inthandler;

#ifdef CONFIG_M68KFPU_EMU
	if (FPU_IS_EMU)
		vectors[VEC_LINE11] = fpu_emu;
#endif

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
}

