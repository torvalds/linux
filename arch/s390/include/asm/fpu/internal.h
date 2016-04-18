/*
 * FPU state and register content conversion primitives
 *
 * Copyright IBM Corp. 2015
 * Author(s): Hendrik Brueckner <brueckner@linux.vnet.ibm.com>
 */

#ifndef _ASM_S390_FPU_INTERNAL_H
#define _ASM_S390_FPU_INTERNAL_H

#include <linux/string.h>
#include <asm/ctl_reg.h>
#include <asm/fpu/types.h>

static inline void save_vx_regs(__vector128 *vxrs)
{
	asm volatile(
		"	la	1,%0\n"
		"	.word	0xe70f,0x1000,0x003e\n"	/* vstm 0,15,0(1) */
		"	.word	0xe70f,0x1100,0x0c3e\n"	/* vstm 16,31,256(1) */
		: "=Q" (*(struct vx_array *) vxrs) : : "1");
}

static inline void convert_vx_to_fp(freg_t *fprs, __vector128 *vxrs)
{
	int i;

	for (i = 0; i < __NUM_FPRS; i++)
		fprs[i] = *(freg_t *)(vxrs + i);
}

static inline void convert_fp_to_vx(__vector128 *vxrs, freg_t *fprs)
{
	int i;

	for (i = 0; i < __NUM_FPRS; i++)
		*(freg_t *)(vxrs + i) = fprs[i];
}

static inline void fpregs_store(_s390_fp_regs *fpregs, struct fpu *fpu)
{
	fpregs->pad = 0;
	fpregs->fpc = fpu->fpc;
	if (MACHINE_HAS_VX)
		convert_vx_to_fp((freg_t *)&fpregs->fprs, fpu->vxrs);
	else
		memcpy((freg_t *)&fpregs->fprs, fpu->fprs,
		       sizeof(fpregs->fprs));
}

static inline void fpregs_load(_s390_fp_regs *fpregs, struct fpu *fpu)
{
	fpu->fpc = fpregs->fpc;
	if (MACHINE_HAS_VX)
		convert_fp_to_vx(fpu->vxrs, (freg_t *)&fpregs->fprs);
	else
		memcpy(fpu->fprs, (freg_t *)&fpregs->fprs,
		       sizeof(fpregs->fprs));
}

#endif /* _ASM_S390_FPU_INTERNAL_H */
