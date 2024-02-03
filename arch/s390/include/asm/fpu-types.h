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
	u32 fpc;
	__vector128 vxrs[__NUM_VXRS] __aligned(8);
};

/* In-kernel FPU state structure */
struct kernel_fpu {
	int	    mask;
	u32	    fpc;
	__vector128 vxrs[__NUM_VXRS] __aligned(8);
};

#define DECLARE_KERNEL_FPU_ONSTACK(name)	\
	struct kernel_fpu name __uninitialized

#endif /* _ASM_S390_FPU_TYPES_H */
