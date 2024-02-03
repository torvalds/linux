/* SPDX-License-Identifier: GPL-2.0 */
/*
 * In-kernel FPU support functions
 *
 *
 * Consider these guidelines before using in-kernel FPU functions:
 *
 *  1. Use kernel_fpu_begin() and kernel_fpu_end() to enclose all in-kernel
 *     use of floating-point or vector registers and instructions.
 *
 *  2. For kernel_fpu_begin(), specify the vector register range you want to
 *     use with the KERNEL_VXR_* constants. Consider these usage guidelines:
 *
 *     a) If your function typically runs in process-context, use the lower
 *	  half of the vector registers, for example, specify KERNEL_VXR_LOW.
 *     b) If your function typically runs in soft-irq or hard-irq context,
 *	  prefer using the upper half of the vector registers, for example,
 *	  specify KERNEL_VXR_HIGH.
 *
 *     If you adhere to these guidelines, an interrupted process context
 *     does not require to save and restore vector registers because of
 *     disjoint register ranges.
 *
 *     Also note that the __kernel_fpu_begin()/__kernel_fpu_end() functions
 *     includes logic to save and restore up to 16 vector registers at once.
 *
 *  3. You can nest kernel_fpu_begin()/kernel_fpu_end() by using different
 *     struct kernel_fpu states.  Vector registers that are in use by outer
 *     levels are saved and restored.  You can minimize the save and restore
 *     effort by choosing disjoint vector register ranges.
 *
 *  5. To use vector floating-point instructions, specify the KERNEL_FPC
 *     flag to save and restore floating-point controls in addition to any
 *     vector register range.
 *
 *  6. To use floating-point registers and instructions only, specify the
 *     KERNEL_FPR flag.  This flag triggers a save and restore of vector
 *     registers V0 to V15 and floating-point controls.
 *
 * Copyright IBM Corp. 2015
 * Author(s): Hendrik Brueckner <brueckner@linux.vnet.ibm.com>
 */

#ifndef _ASM_S390_FPU_H
#define _ASM_S390_FPU_H

#include <linux/processor.h>
#include <linux/preempt.h>
#include <linux/string.h>
#include <asm/sigcontext.h>
#include <asm/fpu-types.h>
#include <asm/fpu-insn.h>
#include <asm/facility.h>

static inline bool cpu_has_vx(void)
{
	return likely(test_facility(129));
}

void save_fpu_regs(void);
void load_fpu_regs(void);
void __load_fpu_regs(void);

#define KERNEL_FPC		1
#define KERNEL_VXR_V0V7		2
#define KERNEL_VXR_V8V15	4
#define KERNEL_VXR_V16V23	8
#define KERNEL_VXR_V24V31	16

#define KERNEL_VXR_LOW		(KERNEL_VXR_V0V7   | KERNEL_VXR_V8V15)
#define KERNEL_VXR_MID		(KERNEL_VXR_V8V15  | KERNEL_VXR_V16V23)
#define KERNEL_VXR_HIGH		(KERNEL_VXR_V16V23 | KERNEL_VXR_V24V31)

#define KERNEL_VXR		(KERNEL_VXR_LOW	   | KERNEL_VXR_HIGH)
#define KERNEL_FPR		(KERNEL_FPC	   | KERNEL_VXR_LOW)

/*
 * Note the functions below must be called with preemption disabled.
 * Do not enable preemption before calling __kernel_fpu_end() to prevent
 * an corruption of an existing kernel FPU state.
 *
 * Prefer using the kernel_fpu_begin()/kernel_fpu_end() pair of functions.
 */
void __kernel_fpu_begin(struct kernel_fpu *state, u32 flags);
void __kernel_fpu_end(struct kernel_fpu *state, u32 flags);

static inline void kernel_fpu_begin(struct kernel_fpu *state, u32 flags)
{
	preempt_disable();
	state->mask = S390_lowcore.fpu_flags;
	if (!test_cpu_flag(CIF_FPU)) {
		/* Save user space FPU state and register contents */
		save_fpu_regs();
	} else if (state->mask & flags) {
		/* Save FPU/vector register in-use by the kernel */
		__kernel_fpu_begin(state, flags);
	}
	S390_lowcore.fpu_flags |= flags;
}

static inline void kernel_fpu_end(struct kernel_fpu *state, u32 flags)
{
	S390_lowcore.fpu_flags = state->mask;
	if (state->mask & flags) {
		/* Restore FPU/vector register in-use by the kernel */
		__kernel_fpu_end(state, flags);
	}
	preempt_enable();
}

static inline void save_vx_regs(__vector128 *vxrs)
{
	asm volatile("\n"
		"	la	1,%0\n"
		"	.word	0xe70f,0x1000,0x003e\n" /* vstm 0,15,0(1) */
		"	.word	0xe70f,0x1100,0x0c3e\n" /* vstm 16,31,256(1) */
		: "=Q" (*(struct vx_array *)vxrs) : : "1");
}

static inline void convert_vx_to_fp(freg_t *fprs, __vector128 *vxrs)
{
	int i;

	for (i = 0; i < __NUM_FPRS; i++)
		fprs[i].ui = vxrs[i].high;
}

static inline void convert_fp_to_vx(__vector128 *vxrs, freg_t *fprs)
{
	int i;

	for (i = 0; i < __NUM_FPRS; i++)
		vxrs[i].high = fprs[i].ui;
}

static inline void fpregs_store(_s390_fp_regs *fpregs, struct fpu *fpu)
{
	fpregs->pad = 0;
	fpregs->fpc = fpu->fpc;
	if (cpu_has_vx())
		convert_vx_to_fp((freg_t *)&fpregs->fprs, fpu->vxrs);
	else
		memcpy((freg_t *)&fpregs->fprs, fpu->fprs, sizeof(fpregs->fprs));
}

static inline void fpregs_load(_s390_fp_regs *fpregs, struct fpu *fpu)
{
	fpu->fpc = fpregs->fpc;
	if (cpu_has_vx())
		convert_fp_to_vx(fpu->vxrs, (freg_t *)&fpregs->fprs);
	else
		memcpy(fpu->fprs, (freg_t *)&fpregs->fprs, sizeof(fpregs->fprs));
}

#endif /* _ASM_S390_FPU_H */
