/*
 * General floating pointer and vector register helpers
 *
 * Copyright IBM Corp. 2015
 * Author(s): Hendrik Brueckner <brueckner@linux.vnet.ibm.com>
 */

#ifndef _ASM_S390_FPU_INTERNAL_H
#define _ASM_S390_FPU_INTERNAL_H

#include <linux/errno.h>
#include <linux/string.h>
#include <asm/linkage.h>
#include <asm/ctl_reg.h>
#include <asm/sigcontext.h>

struct fpu {
	__u32 fpc;			/* Floating-point control */
	__u32 pad;
	freg_t fprs[__NUM_FPRS];	/* Floating-point register save area */
	__vector128 *vxrs;		/* Vector register save area */
};

#define is_vx_fpu(fpu) (!!(fpu)->vxrs)
#define is_vx_task(tsk) (!!(tsk)->thread.fpu.vxrs)

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

static inline void save_fp_ctl(u32 *fpc)
{
	asm volatile(
		"       stfpc   %0\n"
		: "+Q" (*fpc));
}

static inline int restore_fp_ctl(u32 *fpc)
{
	int rc;

	asm volatile(
		"	lfpc    %1\n"
		"0:	la	%0,0\n"
		"1:\n"
		: "=d" (rc) : "Q" (*fpc), "0" (-EINVAL));
	return rc;
}

static inline void save_fp_regs(freg_t *fprs)
{
	asm volatile("std 0,%0" : "=Q" (fprs[0]));
	asm volatile("std 2,%0" : "=Q" (fprs[2]));
	asm volatile("std 4,%0" : "=Q" (fprs[4]));
	asm volatile("std 6,%0" : "=Q" (fprs[6]));
	asm volatile("std 1,%0" : "=Q" (fprs[1]));
	asm volatile("std 3,%0" : "=Q" (fprs[3]));
	asm volatile("std 5,%0" : "=Q" (fprs[5]));
	asm volatile("std 7,%0" : "=Q" (fprs[7]));
	asm volatile("std 8,%0" : "=Q" (fprs[8]));
	asm volatile("std 9,%0" : "=Q" (fprs[9]));
	asm volatile("std 10,%0" : "=Q" (fprs[10]));
	asm volatile("std 11,%0" : "=Q" (fprs[11]));
	asm volatile("std 12,%0" : "=Q" (fprs[12]));
	asm volatile("std 13,%0" : "=Q" (fprs[13]));
	asm volatile("std 14,%0" : "=Q" (fprs[14]));
	asm volatile("std 15,%0" : "=Q" (fprs[15]));
}

static inline void restore_fp_regs(freg_t *fprs)
{
	asm volatile("ld 0,%0" : : "Q" (fprs[0]));
	asm volatile("ld 2,%0" : : "Q" (fprs[2]));
	asm volatile("ld 4,%0" : : "Q" (fprs[4]));
	asm volatile("ld 6,%0" : : "Q" (fprs[6]));
	asm volatile("ld 1,%0" : : "Q" (fprs[1]));
	asm volatile("ld 3,%0" : : "Q" (fprs[3]));
	asm volatile("ld 5,%0" : : "Q" (fprs[5]));
	asm volatile("ld 7,%0" : : "Q" (fprs[7]));
	asm volatile("ld 8,%0" : : "Q" (fprs[8]));
	asm volatile("ld 9,%0" : : "Q" (fprs[9]));
	asm volatile("ld 10,%0" : : "Q" (fprs[10]));
	asm volatile("ld 11,%0" : : "Q" (fprs[11]));
	asm volatile("ld 12,%0" : : "Q" (fprs[12]));
	asm volatile("ld 13,%0" : : "Q" (fprs[13]));
	asm volatile("ld 14,%0" : : "Q" (fprs[14]));
	asm volatile("ld 15,%0" : : "Q" (fprs[15]));
}

static inline void save_vx_regs(__vector128 *vxrs)
{
	typedef struct { __vector128 _[__NUM_VXRS]; } addrtype;

	asm volatile(
		"	la	1,%0\n"
		"	.word	0xe70f,0x1000,0x003e\n"	/* vstm 0,15,0(1) */
		"	.word	0xe70f,0x1100,0x0c3e\n"	/* vstm 16,31,256(1) */
		: "=Q" (*(addrtype *) vxrs) : : "1");
}

static inline void save_vx_regs_safe(__vector128 *vxrs)
{
	unsigned long cr0, flags;

	flags = arch_local_irq_save();
	__ctl_store(cr0, 0, 0);
	__ctl_set_bit(0, 17);
	__ctl_set_bit(0, 18);
	save_vx_regs(vxrs);
	__ctl_load(cr0, 0, 0);
	arch_local_irq_restore(flags);
}

static inline void restore_vx_regs(__vector128 *vxrs)
{
	typedef struct { __vector128 _[__NUM_VXRS]; } addrtype;

	asm volatile(
		"	la	1,%0\n"
		"	.word	0xe70f,0x1000,0x0036\n"	/* vlm 0,15,0(1) */
		"	.word	0xe70f,0x1100,0x0c36\n"	/* vlm 16,31,256(1) */
		: : "Q" (*(addrtype *) vxrs) : "1");
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

static inline void save_fpu_regs(struct fpu *fpu)
{
	save_fp_ctl(&fpu->fpc);
	if (is_vx_fpu(fpu))
		save_vx_regs(fpu->vxrs);
	else
		save_fp_regs(fpu->fprs);
}

static inline void restore_fpu_regs(struct fpu *fpu)
{
	restore_fp_ctl(&fpu->fpc);
	if (is_vx_fpu(fpu))
		restore_vx_regs(fpu->vxrs);
	else
		restore_fp_regs(fpu->fprs);
}

#endif /* _ASM_S390_FPU_INTERNAL_H */
