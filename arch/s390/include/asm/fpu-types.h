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

struct kernel_fpu_hdr {
	int	mask;
	u32	fpc;
};

struct kernel_fpu {
	struct kernel_fpu_hdr hdr;
	__vector128 vxrs[] __aligned(8);
};

#define KERNEL_FPU_STRUCT(vxr_size)				\
struct kernel_fpu_##vxr_size {					\
	struct kernel_fpu_hdr hdr;				\
	__vector128 vxrs[vxr_size] __aligned(8);		\
}

KERNEL_FPU_STRUCT(8);
KERNEL_FPU_STRUCT(16);
KERNEL_FPU_STRUCT(32);

#define DECLARE_KERNEL_FPU_ONSTACK(vxr_size, name)		\
	struct kernel_fpu_##vxr_size name __uninitialized

#define DECLARE_KERNEL_FPU_ONSTACK8(name)			\
	DECLARE_KERNEL_FPU_ONSTACK(8, name)

#define DECLARE_KERNEL_FPU_ONSTACK16(name)			\
	DECLARE_KERNEL_FPU_ONSTACK(16, name)

#define DECLARE_KERNEL_FPU_ONSTACK32(name)			\
	DECLARE_KERNEL_FPU_ONSTACK(32, name)

#endif /* _ASM_S390_FPU_TYPES_H */
