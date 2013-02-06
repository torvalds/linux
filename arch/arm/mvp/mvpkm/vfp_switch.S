/*
 * Linux 2.6.32 and later Kernel module for VMware MVP Hypervisor Support
 *
 * Copyright (C) 2010-2012 VMware, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; see the file COPYING.  If not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#line 5

#include "arm_defs.h"
#include "platdefx.h"
#include "arm_as_macros.h"

/**
 *  @file
 *
 *  @brief Save and Load VFP entire VFP state.
 */

	.text

/**
 * @brief Save VFP context
 * @param R0 = save area pointer:
 *             .long   fpexc,fpscr,fpinst,fpinst2,cpacr,fpexc'
 *             .double d0..d15
 *             .double d16..d31
 * Note: VFP is left in an enable state regardless of initial state.
 */
	.align	4
	.global SaveVFP
SaveVFP:
	/*
	 * Save registers.  GCC does not expect us to preserve R0..R3,R12,LR.
	 */
	stmdb	sp!, {r4-r6}

	/*
	 * Save Coproc Access Control register.
	 */
	mrc_p15	COPROC_ACCESS_CONTROL, r5

	/*
	 * If CP10/11 are disabled, enable them so we can save VFP state.
	 * The host (or guest) may have left data in the data registers that
	 * must be preserved.
	 */
	orr	r2, r5, #CPACR_CP10_CP11_PRIV_ONLY
	mcr_p15	COPROC_ACCESS_CONTROL, r2
        isb

	/*
	 * Follow procedure on AppxB-22 ARM DDI0406B to save FPINST[2].
	 * Also enable VFP access with FPEXC_EN.
	 */
	fmrx	r1, fpexc		@ get existing FPEXC system register
	orr	r6, r1, #ARM_VFP_SYSTEM_REG_FPEXC_EX|ARM_VFP_SYSTEM_REG_FPEXC_FP2V|ARM_VFP_SYSTEM_REG_FPEXC_EN
#if !defined(MVP_HOST_CODE_forceon)
	fmxr	fpexc, r6		@ set FPEXC.EX, .FP2V and .EN
	fmrx	r6, fpexc		@ read them back
	tst	r6, #ARM_VFP_SYSTEM_REG_FPEXC_EX @ see if either one is valid
	beq	1000f			@ neither, skip it all
	fmrx	r3, FPINST		@ FPINST is valid, save it
	tst	r6, #ARM_VFP_SYSTEM_REG_FPEXC_FP2V @ see if FPINST2 is valid
	beq	1000f
	fmrx	r4, FPINST2		@ FPINST2 is valid, save it
1000:
#else
	mov	r6, r1
#endif
	fmrx	r2, FPSCR		@ always save FPSCR system register

	/*
	 * At this point:
	 *   R1 = original FPEXC
	 *   R2 = FPSCR
	 *   R3 = FPINST
	 *   R4 = FPINST2
	 *   R5 = original CPACR
	 *   R6 = FPEXC readback with FPEXC.EX, .FP2V and .EN set
	 *        telling us whether FPINST/2 are valid
	 */
	stmia	r0!, {r1-r6}

	/*
	 * Save floating point data registers.
	 */
	vstmia	r0!, {d0-d15}		@ Save d0 thru d15

	/**
	 * @todo We should probably just read MVFR0 once at boot/initialization
	 * time and store it in some variable, to save having to do what might
	 * be expensive coprocessor accesses.
	 */
	fmrx	r1, MVFR0		@ Read Media and VFP Feature Register 0
	and	r1, r1, #ARM_VFP_SYSTEM_REG_MVFR0_A_SIMD_MASK  @ A_SIMD field
	cmp	r1, #2			@ 32 x 64bit registers?
	bne	2000f
	vstmia	r0!, {d16-d31}
2000:

	/*
	 * Restore scratch registers and return.
	 */
	ldmia	sp!, {r4-r6}
	mov	pc, lr


/**
 * @brief Load VFP context
 * @param R0 = load area pointer:
 *             .long   fpexc,fpscr,fpinst,fpinst2,cpacr,fpexc'
 *             .double d0..d15
 *             .double d16..d31
 * @note VFP is assumed to be in an enabled state on entry.
 */
	.align	4
	.global LoadVFP
LoadVFP:
	/*
	 * Save registers.  GCC does not expect us to preserve R0..R3,R12,LR.
	 */
	stmdb	sp!, {r4-r6}

	/*
	 * Get status register contents:
	 *   R1 = original FPEXC
	 *   R2 = FPSCR
	 *   R3 = FPINST
	 *   R4 = FPINST2
	 *   R5 = original CPACR
	 *   R6 = FPEXC readback with FPEXC.EX, .FP2V and .EN set
	 *        telling us whether FPINST/2 are valid
	 */
	ldmia	r0!, {r1-r6}

	/*
	 * Restore some initial FP status registers.
	 */
	fmxr	fpexc, r6		@ with FPEXC.EX, .FP2V and .EN set
	fmxr	FPSCR, r2		@ always load FPSCR system register

	/*
	 * Follow procedure on AppxB-22 ARM DDI0406B to load FPINST[2].
	 */
#if !defined(MVP_HOST_CODE_forceon)
	fmrx	r6, fpexc		@ initial call might have different bits
					@ ... because FPEXC.EX, .FP2V and .EN
					@     are forced set by init code in
					@     mvpd.c SetupMonitor()
	tst	r6, #ARM_VFP_SYSTEM_REG_FPEXC_EX @ see if either one is valid
	beq	1000f			@ neither, skip it all
	fmxr	FPINST, r3		@ FPINST is valid, save it
	tst	r6, #ARM_VFP_SYSTEM_REG_FPEXC_FP2V @ see if FPINST2 is valid
	beq	1000f
	fmxr	FPINST2, r4		@ FPINST2 is valid, save it
1000:
#endif

	/*
	 * Load floating point data registers.
	 */
	vldmia	r0!, {d0-d15}

	/**
	 * @todo We should probably just read MVFR0 once at boot/initialization
	 * time and store it in some variable, to save having to do what might
	 * be expensive coprocessor accesses.
	 */
	fmrx	r3, MVFR0		@ Read Media and VFP Feature Register 0
	and	r3, r3, #ARM_VFP_SYSTEM_REG_MVFR0_A_SIMD_MASK  @ A_SIMD field
	cmp	r3, #2			@ 32 x 64bit registers?
	bne	2000f
	vldmia	r0!, {d16-d31}
2000:

	/*
	 * Now that VFP registers are all loaded, we put the restored values
	 * back in the registers, possibly disabling the VFP.
	 */
	fmxr	fpexc, r1		@ with original FPEXC.EX, FPEXC.FP2V
					@ and FPEXC.EN values

	/*
	 * Load Coproc Access Control CP10/CP11 enable bits, possibly disabling
	 * VFP access.
	 */
	mrc_p15	COPROC_ACCESS_CONTROL, r0
	bic	r0, r0, #CPACR_CP10_CP11_MASK
	and	r5, r5, #CPACR_CP10_CP11_MASK
	orr	r0, r0, r5
	mcr_p15	COPROC_ACCESS_CONTROL, r0
        isb

	/*
	 * Restore scratch registers and return.
	 */
	ldmia	sp!, {r4-r6}
	mov	pc, lr

	.align	4
	.global GetFPEXC
GetFPEXC:
	fmrx	r0, fpexc		@ get existing FPEXC system register
	mov	pc, lr
