/* SPDX-License-Identifier: GPL-2.0 */
/*
 * FPU data structures
 *
 * Copyright IBM Corp. 2015
 * Author(s): Hendrik Brueckner <brueckner@linux.vnet.ibm.com>
 */

#ifndef _ASM_S390_FPU_TYPES_H
#define _ASM_S390_FPU_TYPES_H

#include <asm/sigcontext.h>

struct fpu {
	__u32 fpc;		/* Floating-point control */
	void *regs;		/* Pointer to the current save area */
	union {
		/* Floating-point register save area */
		freg_t fprs[__NUM_FPRS];
		/* Vector register save area */
		__vector128 vxrs[__NUM_VXRS];
	};
};

/* VX array structure for address operand constraints in inline assemblies */
struct vx_array { __vector128 _[__NUM_VXRS]; };

/* In-kernel FPU state structure */
struct kernel_fpu {
	u32	    mask;
	u32	    fpc;
	union {
		freg_t fprs[__NUM_FPRS];
		__vector128 vxrs[__NUM_VXRS];
	};
};

#endif /* _ASM_S390_FPU_TYPES_H */
