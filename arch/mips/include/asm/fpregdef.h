/*
 * Definitions for the FPU register names
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995, 1999 Ralf Baechle
 * Copyright (C) 1985 MIPS Computer Systems, Inc.
 * Copyright (C) 1990 - 1992, 1999 Silicon Graphics, Inc.
 */
#ifndef _ASM_FPREGDEF_H
#define _ASM_FPREGDEF_H

#include <asm/sgidefs.h>

/*
 * starting with binutils 2.24.51.20140729, MIPS binutils warn about mixing
 * hardfloat and softfloat object files.  The kernel build uses soft-float by
 * default, so we also need to pass -msoft-float along to GAS if it supports it.
 * But this in turn causes assembler errors in files which access hardfloat
 * registers.  We detect if GAS supports "-msoft-float" in the Makefile and
 * explicitly put ".set hardfloat" where floating point registers are touched.
 */
#ifdef GAS_HAS_SET_HARDFLOAT
#define SET_HARDFLOAT .set hardfloat
#else
#define SET_HARDFLOAT
#endif

#if _MIPS_SIM == _MIPS_SIM_ABI32

/*
 * These definitions only cover the R3000-ish 16/32 register model.
 * But we're trying to be R3000 friendly anyway ...
 */
#define fv0	$f0	 /* return value */
#define fv0f	$f1
#define fv1	$f2
#define fv1f	$f3
#define fa0	$f12	 /* argument registers */
#define fa0f	$f13
#define fa1	$f14
#define fa1f	$f15
#define ft0	$f4	 /* caller saved */
#define ft0f	$f5
#define ft1	$f6
#define ft1f	$f7
#define ft2	$f8
#define ft2f	$f9
#define ft3	$f10
#define ft3f	$f11
#define ft4	$f16
#define ft4f	$f17
#define ft5	$f18
#define ft5f	$f19
#define fs0	$f20	 /* callee saved */
#define fs0f	$f21
#define fs1	$f22
#define fs1f	$f23
#define fs2	$f24
#define fs2f	$f25
#define fs3	$f26
#define fs3f	$f27
#define fs4	$f28
#define fs4f	$f29
#define fs5	$f30
#define fs5f	$f31

#define fcr31	$31	 /* FPU status register */

#endif /* _MIPS_SIM == _MIPS_SIM_ABI32 */

#if _MIPS_SIM == _MIPS_SIM_ABI64 || _MIPS_SIM == _MIPS_SIM_NABI32

#define fv0	$f0	/* return value */
#define fv1	$f2
#define fa0	$f12	/* argument registers */
#define fa1	$f13
#define fa2	$f14
#define fa3	$f15
#define fa4	$f16
#define fa5	$f17
#define fa6	$f18
#define fa7	$f19
#define ft0	$f4	/* caller saved */
#define ft1	$f5
#define ft2	$f6
#define ft3	$f7
#define ft4	$f8
#define ft5	$f9
#define ft6	$f10
#define ft7	$f11
#define ft8	$f20
#define ft9	$f21
#define ft10	$f22
#define ft11	$f23
#define ft12	$f1
#define ft13	$f3
#define fs0	$f24	/* callee saved */
#define fs1	$f25
#define fs2	$f26
#define fs3	$f27
#define fs4	$f28
#define fs5	$f29
#define fs6	$f30
#define fs7	$f31

#define fcr31	$31

#endif /* _MIPS_SIM == _MIPS_SIM_ABI64 || _MIPS_SIM == _MIPS_SIM_NABI32 */

#endif /* _ASM_FPREGDEF_H */
