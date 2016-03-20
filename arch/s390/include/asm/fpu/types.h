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
	__u32 fpc;			/* Floating-point control */
	union {
		void *regs;
		freg_t *fprs;		/* Floating-point register save area */
		__vector128 *vxrs;	/* Vector register save area */
	};
};

/* VX array structure for address operand constraints in inline assemblies */
struct vx_array { __vector128 _[__NUM_VXRS]; };

#endif /* _ASM_S390_FPU_TYPES_H */
