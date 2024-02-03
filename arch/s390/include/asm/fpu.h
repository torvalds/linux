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
#include <linux/sched.h>
#include <asm/sigcontext.h>
#include <asm/fpu-types.h>
#include <asm/fpu-insn.h>
#include <asm/facility.h>

static inline bool cpu_has_vx(void)
{
	return likely(test_facility(129));
}

void save_user_fpu_regs(void);
void load_user_fpu_regs(void);
void __load_user_fpu_regs(void);

enum {
	KERNEL_FPC_BIT = 0,
	KERNEL_VXR_V0V7_BIT,
	KERNEL_VXR_V8V15_BIT,
	KERNEL_VXR_V16V23_BIT,
	KERNEL_VXR_V24V31_BIT,
};

#define KERNEL_FPC		BIT(KERNEL_FPC_BIT)
#define KERNEL_VXR_V0V7		BIT(KERNEL_VXR_V0V7_BIT)
#define KERNEL_VXR_V8V15	BIT(KERNEL_VXR_V8V15_BIT)
#define KERNEL_VXR_V16V23	BIT(KERNEL_VXR_V16V23_BIT)
#define KERNEL_VXR_V24V31	BIT(KERNEL_VXR_V24V31_BIT)

#define KERNEL_VXR_LOW		(KERNEL_VXR_V0V7   | KERNEL_VXR_V8V15)
#define KERNEL_VXR_MID		(KERNEL_VXR_V8V15  | KERNEL_VXR_V16V23)
#define KERNEL_VXR_HIGH		(KERNEL_VXR_V16V23 | KERNEL_VXR_V24V31)

#define KERNEL_VXR		(KERNEL_VXR_LOW	   | KERNEL_VXR_HIGH)
#define KERNEL_FPR		(KERNEL_FPC	   | KERNEL_VXR_LOW)

void __kernel_fpu_begin(struct kernel_fpu *state, int flags);
void __kernel_fpu_end(struct kernel_fpu *state, int flags);

static __always_inline void save_vx_regs(__vector128 *vxrs)
{
	fpu_vstm(0, 15, &vxrs[0]);
	fpu_vstm(16, 31, &vxrs[16]);
}

static __always_inline void load_vx_regs(__vector128 *vxrs)
{
	fpu_vlm(0, 15, &vxrs[0]);
	fpu_vlm(16, 31, &vxrs[16]);
}

static __always_inline void save_fp_regs(freg_t *fprs)
{
	fpu_std(0, &fprs[0]);
	fpu_std(1, &fprs[1]);
	fpu_std(2, &fprs[2]);
	fpu_std(3, &fprs[3]);
	fpu_std(4, &fprs[4]);
	fpu_std(5, &fprs[5]);
	fpu_std(6, &fprs[6]);
	fpu_std(7, &fprs[7]);
	fpu_std(8, &fprs[8]);
	fpu_std(9, &fprs[9]);
	fpu_std(10, &fprs[10]);
	fpu_std(11, &fprs[11]);
	fpu_std(12, &fprs[12]);
	fpu_std(13, &fprs[13]);
	fpu_std(14, &fprs[14]);
	fpu_std(15, &fprs[15]);
}

static __always_inline void load_fp_regs(freg_t *fprs)
{
	fpu_ld(0, &fprs[0]);
	fpu_ld(1, &fprs[1]);
	fpu_ld(2, &fprs[2]);
	fpu_ld(3, &fprs[3]);
	fpu_ld(4, &fprs[4]);
	fpu_ld(5, &fprs[5]);
	fpu_ld(6, &fprs[6]);
	fpu_ld(7, &fprs[7]);
	fpu_ld(8, &fprs[8]);
	fpu_ld(9, &fprs[9]);
	fpu_ld(10, &fprs[10]);
	fpu_ld(11, &fprs[11]);
	fpu_ld(12, &fprs[12]);
	fpu_ld(13, &fprs[13]);
	fpu_ld(14, &fprs[14]);
	fpu_ld(15, &fprs[15]);
}

static inline void kernel_fpu_begin(struct kernel_fpu *state, int flags)
{
	state->mask = READ_ONCE(current->thread.kfpu_flags);
	if (!test_thread_flag(TIF_FPU)) {
		/* Save user space FPU state and register contents */
		save_user_fpu_regs();
	} else if (state->mask & flags) {
		/* Save FPU/vector register in-use by the kernel */
		__kernel_fpu_begin(state, flags);
	}
	__atomic_or(flags, &current->thread.kfpu_flags);
}

static inline void kernel_fpu_end(struct kernel_fpu *state, int flags)
{
	WRITE_ONCE(current->thread.kfpu_flags, state->mask);
	if (state->mask & flags) {
		/* Restore FPU/vector register in-use by the kernel */
		__kernel_fpu_end(state, flags);
	}
}

static inline void save_kernel_fpu_regs(struct thread_struct *thread)
{
	struct fpu *state = &thread->kfpu;

	if (!thread->kfpu_flags)
		return;
	fpu_stfpc(&state->fpc);
	if (likely(cpu_has_vx()))
		save_vx_regs(state->vxrs);
	else
		save_fp_regs(state->fprs);
}

static inline void restore_kernel_fpu_regs(struct thread_struct *thread)
{
	struct fpu *state = &thread->kfpu;

	if (!thread->kfpu_flags)
		return;
	fpu_lfpc(&state->fpc);
	if (likely(cpu_has_vx()))
		load_vx_regs(state->vxrs);
	else
		load_fp_regs(state->fprs);
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
