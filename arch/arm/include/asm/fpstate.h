/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  arch/arm/include/asm/fpstate.h
 *
 *  Copyright (C) 1995 Russell King
 */

#ifndef __ASM_ARM_FPSTATE_H
#define __ASM_ARM_FPSTATE_H


#ifndef __ASSEMBLY__

/*
 * VFP storage area has:
 *  - FPEXC, FPSCR, FPINST and FPINST2.
 *  - 16 or 32 double precision data registers
 *  - an implementation-dependent word of state for FLDMX/FSTMX (pre-ARMv6)
 * 
 *  FPEXC will always be non-zero once the VFP has been used in this process.
 */

struct vfp_hard_struct {
#ifdef CONFIG_VFPv3
	__u64 fpregs[32];
#else
	__u64 fpregs[16];
#endif
#if __LINUX_ARM_ARCH__ < 6
	__u32 fpmx_state;
#endif
	__u32 fpexc;
	__u32 fpscr;
	/*
	 * VFP implementation specific state
	 */
	__u32 fpinst;
	__u32 fpinst2;

#ifdef CONFIG_SMP
	__u32 cpu;
#endif
};

union vfp_state {
	struct vfp_hard_struct	hard;
};

#define FP_HARD_SIZE 35

struct fp_hard_struct {
	unsigned int save[FP_HARD_SIZE];		/* as yet undefined */
};

#define FP_SOFT_SIZE 35

struct fp_soft_struct {
	unsigned int save[FP_SOFT_SIZE];		/* undefined information */
};

#define IWMMXT_SIZE	0x98

struct iwmmxt_struct {
	unsigned int save[IWMMXT_SIZE / sizeof(unsigned int)];
};

union fp_state {
	struct fp_hard_struct	hard;
	struct fp_soft_struct	soft;
#ifdef CONFIG_IWMMXT
	struct iwmmxt_struct	iwmmxt;
#endif
};

#define FP_SIZE (sizeof(union fp_state) / sizeof(int))

#endif

#endif
