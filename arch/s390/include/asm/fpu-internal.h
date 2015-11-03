/*
 * General floating pointer and vector register helpers
 *
 * Copyright IBM Corp. 2015
 * Author(s): Hendrik Brueckner <brueckner@linux.vnet.ibm.com>
 */

#ifndef _ASM_S390_FPU_INTERNAL_H
#define _ASM_S390_FPU_INTERNAL_H

#define FPU_USE_VX		1	/* Vector extension is active */

#ifndef __ASSEMBLY__

#include <linux/errno.h>
#include <linux/string.h>
#include <asm/linkage.h>
#include <asm/ctl_reg.h>
#include <asm/sigcontext.h>

struct fpu {
	__u32 fpc;			/* Floating-point control */
	__u32 flags;
	union {
		void *regs;
		freg_t *fprs;		/* Floating-point register save area */
		__vector128 *vxrs;	/* Vector register save area */
	};
};

void save_fpu_regs(void);

#define is_vx_fpu(fpu) (!!((fpu)->flags & FPU_USE_VX))
#define is_vx_task(tsk) (!!((tsk)->thread.fpu.flags & FPU_USE_VX))

/* VX array structure for address operand constraints in inline assemblies */
struct vx_array { __vector128 _[__NUM_VXRS]; };

static inline int test_fp_ctl(u32 fpc)
{
	u32 orig_fpc;
	int rc;

	asm volatile(
		"	efpc    %1\n"
		"	sfpc	%2\n"
		"0:	sfpc	%1\n"
		"	la	%0,0\n"
		"1:\n"
		EX_TABLE(0b,1b)
		: "=d" (rc), "=d" (orig_fpc)
		: "d" (fpc), "0" (-EINVAL));
	return rc;
}

static inline void save_vx_regs_safe(__vector128 *vxrs)
{
	unsigned long cr0, flags;

	flags = arch_local_irq_save();
	__ctl_store(cr0, 0, 0);
	__ctl_set_bit(0, 17);
	__ctl_set_bit(0, 18);
	asm volatile(
		"	la	1,%0\n"
		"	.word	0xe70f,0x1000,0x003e\n"	/* vstm 0,15,0(1) */
		"	.word	0xe70f,0x1100,0x0c3e\n"	/* vstm 16,31,256(1) */
		: "=Q" (*(struct vx_array *) vxrs) : : "1");
	__ctl_load(cr0, 0, 0);
	arch_local_irq_restore(flags);
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
	if (is_vx_fpu(fpu))
		convert_vx_to_fp((freg_t *)&fpregs->fprs, fpu->vxrs);
	else
		memcpy((freg_t *)&fpregs->fprs, fpu->fprs,
		       sizeof(fpregs->fprs));
}

static inline void fpregs_load(_s390_fp_regs *fpregs, struct fpu *fpu)
{
	if (is_vx_fpu(fpu))
		convert_fp_to_vx(fpu->vxrs, (freg_t *)&fpregs->fprs);
	else
		memcpy(fpu->fprs, (freg_t *)&fpregs->fprs,
		       sizeof(fpregs->fprs));
}

#endif

#endif /* _ASM_S390_FPU_INTERNAL_H */
