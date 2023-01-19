// SPDX-License-Identifier: GPL-2.0
/*
 * In-kernel vector facility support functions
 *
 * Copyright IBM Corp. 2015
 * Author(s): Hendrik Brueckner <brueckner@linux.vnet.ibm.com>
 */
#include <linux/kernel.h>
#include <linux/cpu.h>
#include <linux/sched.h>
#include <asm/fpu/types.h>
#include <asm/fpu/api.h>
#include <asm/vx-insn.h>

void __kernel_fpu_begin(struct kernel_fpu *state, u32 flags)
{
	/*
	 * Limit the save to the FPU/vector registers already
	 * in use by the previous context
	 */
	flags &= state->mask;

	if (flags & KERNEL_FPC)
		/* Save floating point control */
		asm volatile("stfpc %0" : "=Q" (state->fpc));

	if (!MACHINE_HAS_VX) {
		if (flags & KERNEL_VXR_V0V7) {
			/* Save floating-point registers */
			asm volatile("std 0,%0" : "=Q" (state->fprs[0]));
			asm volatile("std 1,%0" : "=Q" (state->fprs[1]));
			asm volatile("std 2,%0" : "=Q" (state->fprs[2]));
			asm volatile("std 3,%0" : "=Q" (state->fprs[3]));
			asm volatile("std 4,%0" : "=Q" (state->fprs[4]));
			asm volatile("std 5,%0" : "=Q" (state->fprs[5]));
			asm volatile("std 6,%0" : "=Q" (state->fprs[6]));
			asm volatile("std 7,%0" : "=Q" (state->fprs[7]));
			asm volatile("std 8,%0" : "=Q" (state->fprs[8]));
			asm volatile("std 9,%0" : "=Q" (state->fprs[9]));
			asm volatile("std 10,%0" : "=Q" (state->fprs[10]));
			asm volatile("std 11,%0" : "=Q" (state->fprs[11]));
			asm volatile("std 12,%0" : "=Q" (state->fprs[12]));
			asm volatile("std 13,%0" : "=Q" (state->fprs[13]));
			asm volatile("std 14,%0" : "=Q" (state->fprs[14]));
			asm volatile("std 15,%0" : "=Q" (state->fprs[15]));
		}
		return;
	}

	/* Test and save vector registers */
	asm volatile (
		/*
		 * Test if any vector register must be saved and, if so,
		 * test if all register can be saved.
		 */
		"	la	1,%[vxrs]\n"	/* load save area */
		"	tmll	%[m],30\n"	/* KERNEL_VXR */
		"	jz	7f\n"		/* no work -> done */
		"	jo	5f\n"		/* -> save V0..V31 */
		/*
		 * Test for special case KERNEL_FPU_MID only. In this
		 * case a vstm V8..V23 is the best instruction
		 */
		"	chi	%[m],12\n"	/* KERNEL_VXR_MID */
		"	jne	0f\n"		/* -> save V8..V23 */
		"	VSTM	8,23,128,1\n"	/* vstm %v8,%v23,128(%r1) */
		"	j	7f\n"
		/* Test and save the first half of 16 vector registers */
		"0:	tmll	%[m],6\n"	/* KERNEL_VXR_LOW */
		"	jz	3f\n"		/* -> KERNEL_VXR_HIGH */
		"	jo	2f\n"		/* 11 -> save V0..V15 */
		"	brc	2,1f\n"		/* 10 -> save V8..V15 */
		"	VSTM	0,7,0,1\n"	/* vstm %v0,%v7,0(%r1) */
		"	j	3f\n"
		"1:	VSTM	8,15,128,1\n"	/* vstm %v8,%v15,128(%r1) */
		"	j	3f\n"
		"2:	VSTM	0,15,0,1\n"	/* vstm %v0,%v15,0(%r1) */
		/* Test and save the second half of 16 vector registers */
		"3:	tmll	%[m],24\n"	/* KERNEL_VXR_HIGH */
		"	jz	7f\n"
		"	jo	6f\n"		/* 11 -> save V16..V31 */
		"	brc	2,4f\n"		/* 10 -> save V24..V31 */
		"	VSTM	16,23,256,1\n"	/* vstm %v16,%v23,256(%r1) */
		"	j	7f\n"
		"4:	VSTM	24,31,384,1\n"	/* vstm %v24,%v31,384(%r1) */
		"	j	7f\n"
		"5:	VSTM	0,15,0,1\n"	/* vstm %v0,%v15,0(%r1) */
		"6:	VSTM	16,31,256,1\n"	/* vstm %v16,%v31,256(%r1) */
		"7:"
		: [vxrs] "=Q" (*(struct vx_array *) &state->vxrs)
		: [m] "d" (flags)
		: "1", "cc");
}
EXPORT_SYMBOL(__kernel_fpu_begin);

void __kernel_fpu_end(struct kernel_fpu *state, u32 flags)
{
	/*
	 * Limit the restore to the FPU/vector registers of the
	 * previous context that have been overwritte by the
	 * current context
	 */
	flags &= state->mask;

	if (flags & KERNEL_FPC)
		/* Restore floating-point controls */
		asm volatile("lfpc %0" : : "Q" (state->fpc));

	if (!MACHINE_HAS_VX) {
		if (flags & KERNEL_VXR_V0V7) {
			/* Restore floating-point registers */
			asm volatile("ld 0,%0" : : "Q" (state->fprs[0]));
			asm volatile("ld 1,%0" : : "Q" (state->fprs[1]));
			asm volatile("ld 2,%0" : : "Q" (state->fprs[2]));
			asm volatile("ld 3,%0" : : "Q" (state->fprs[3]));
			asm volatile("ld 4,%0" : : "Q" (state->fprs[4]));
			asm volatile("ld 5,%0" : : "Q" (state->fprs[5]));
			asm volatile("ld 6,%0" : : "Q" (state->fprs[6]));
			asm volatile("ld 7,%0" : : "Q" (state->fprs[7]));
			asm volatile("ld 8,%0" : : "Q" (state->fprs[8]));
			asm volatile("ld 9,%0" : : "Q" (state->fprs[9]));
			asm volatile("ld 10,%0" : : "Q" (state->fprs[10]));
			asm volatile("ld 11,%0" : : "Q" (state->fprs[11]));
			asm volatile("ld 12,%0" : : "Q" (state->fprs[12]));
			asm volatile("ld 13,%0" : : "Q" (state->fprs[13]));
			asm volatile("ld 14,%0" : : "Q" (state->fprs[14]));
			asm volatile("ld 15,%0" : : "Q" (state->fprs[15]));
		}
		return;
	}

	/* Test and restore (load) vector registers */
	asm volatile (
		/*
		 * Test if any vector register must be loaded and, if so,
		 * test if all registers can be loaded at once.
		 */
		"	la	1,%[vxrs]\n"	/* load restore area */
		"	tmll	%[m],30\n"	/* KERNEL_VXR */
		"	jz	7f\n"		/* no work -> done */
		"	jo	5f\n"		/* -> restore V0..V31 */
		/*
		 * Test for special case KERNEL_FPU_MID only. In this
		 * case a vlm V8..V23 is the best instruction
		 */
		"	chi	%[m],12\n"	/* KERNEL_VXR_MID */
		"	jne	0f\n"		/* -> restore V8..V23 */
		"	VLM	8,23,128,1\n"	/* vlm %v8,%v23,128(%r1) */
		"	j	7f\n"
		/* Test and restore the first half of 16 vector registers */
		"0:	tmll	%[m],6\n"	/* KERNEL_VXR_LOW */
		"	jz	3f\n"		/* -> KERNEL_VXR_HIGH */
		"	jo	2f\n"		/* 11 -> restore V0..V15 */
		"	brc	2,1f\n"		/* 10 -> restore V8..V15 */
		"	VLM	0,7,0,1\n"	/* vlm %v0,%v7,0(%r1) */
		"	j	3f\n"
		"1:	VLM	8,15,128,1\n"	/* vlm %v8,%v15,128(%r1) */
		"	j	3f\n"
		"2:	VLM	0,15,0,1\n"	/* vlm %v0,%v15,0(%r1) */
		/* Test and restore the second half of 16 vector registers */
		"3:	tmll	%[m],24\n"	/* KERNEL_VXR_HIGH */
		"	jz	7f\n"
		"	jo	6f\n"		/* 11 -> restore V16..V31 */
		"	brc	2,4f\n"		/* 10 -> restore V24..V31 */
		"	VLM	16,23,256,1\n"	/* vlm %v16,%v23,256(%r1) */
		"	j	7f\n"
		"4:	VLM	24,31,384,1\n"	/* vlm %v24,%v31,384(%r1) */
		"	j	7f\n"
		"5:	VLM	0,15,0,1\n"	/* vlm %v0,%v15,0(%r1) */
		"6:	VLM	16,31,256,1\n"	/* vlm %v16,%v31,256(%r1) */
		"7:"
		: [vxrs] "=Q" (*(struct vx_array *) &state->vxrs)
		: [m] "d" (flags)
		: "1", "cc");
}
EXPORT_SYMBOL(__kernel_fpu_end);

void __load_fpu_regs(void)
{
	struct fpu *state = &current->thread.fpu;
	unsigned long *regs = current->thread.fpu.regs;

	asm volatile("lfpc %0" : : "Q" (state->fpc));
	if (likely(MACHINE_HAS_VX)) {
		asm volatile("lgr	1,%0\n"
			     "VLM	0,15,0,1\n"
			     "VLM	16,31,256,1\n"
			     :
			     : "d" (regs)
			     : "1", "cc", "memory");
	} else {
		asm volatile("ld 0,%0" : : "Q" (regs[0]));
		asm volatile("ld 1,%0" : : "Q" (regs[1]));
		asm volatile("ld 2,%0" : : "Q" (regs[2]));
		asm volatile("ld 3,%0" : : "Q" (regs[3]));
		asm volatile("ld 4,%0" : : "Q" (regs[4]));
		asm volatile("ld 5,%0" : : "Q" (regs[5]));
		asm volatile("ld 6,%0" : : "Q" (regs[6]));
		asm volatile("ld 7,%0" : : "Q" (regs[7]));
		asm volatile("ld 8,%0" : : "Q" (regs[8]));
		asm volatile("ld 9,%0" : : "Q" (regs[9]));
		asm volatile("ld 10,%0" : : "Q" (regs[10]));
		asm volatile("ld 11,%0" : : "Q" (regs[11]));
		asm volatile("ld 12,%0" : : "Q" (regs[12]));
		asm volatile("ld 13,%0" : : "Q" (regs[13]));
		asm volatile("ld 14,%0" : : "Q" (regs[14]));
		asm volatile("ld 15,%0" : : "Q" (regs[15]));
	}
	clear_cpu_flag(CIF_FPU);
}
EXPORT_SYMBOL(__load_fpu_regs);

void load_fpu_regs(void)
{
	raw_local_irq_disable();
	__load_fpu_regs();
	raw_local_irq_enable();
}
EXPORT_SYMBOL(load_fpu_regs);

void save_fpu_regs(void)
{
	unsigned long flags, *regs;
	struct fpu *state;

	local_irq_save(flags);

	if (test_cpu_flag(CIF_FPU))
		goto out;

	state = &current->thread.fpu;
	regs = current->thread.fpu.regs;

	asm volatile("stfpc %0" : "=Q" (state->fpc));
	if (likely(MACHINE_HAS_VX)) {
		asm volatile("lgr	1,%0\n"
			     "VSTM	0,15,0,1\n"
			     "VSTM	16,31,256,1\n"
			     :
			     : "d" (regs)
			     : "1", "cc", "memory");
	} else {
		asm volatile("std 0,%0" : "=Q" (regs[0]));
		asm volatile("std 1,%0" : "=Q" (regs[1]));
		asm volatile("std 2,%0" : "=Q" (regs[2]));
		asm volatile("std 3,%0" : "=Q" (regs[3]));
		asm volatile("std 4,%0" : "=Q" (regs[4]));
		asm volatile("std 5,%0" : "=Q" (regs[5]));
		asm volatile("std 6,%0" : "=Q" (regs[6]));
		asm volatile("std 7,%0" : "=Q" (regs[7]));
		asm volatile("std 8,%0" : "=Q" (regs[8]));
		asm volatile("std 9,%0" : "=Q" (regs[9]));
		asm volatile("std 10,%0" : "=Q" (regs[10]));
		asm volatile("std 11,%0" : "=Q" (regs[11]));
		asm volatile("std 12,%0" : "=Q" (regs[12]));
		asm volatile("std 13,%0" : "=Q" (regs[13]));
		asm volatile("std 14,%0" : "=Q" (regs[14]));
		asm volatile("std 15,%0" : "=Q" (regs[15]));
	}
	set_cpu_flag(CIF_FPU);
out:
	local_irq_restore(flags);
}
EXPORT_SYMBOL(save_fpu_regs);
