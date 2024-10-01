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

void load_fpu_state(struct fpu *state, int flags);
void save_fpu_state(struct fpu *state, int flags);
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

static __always_inline void __save_fp_regs(freg_t *fprs, unsigned int offset)
{
	fpu_std(0, &fprs[0 * offset]);
	fpu_std(1, &fprs[1 * offset]);
	fpu_std(2, &fprs[2 * offset]);
	fpu_std(3, &fprs[3 * offset]);
	fpu_std(4, &fprs[4 * offset]);
	fpu_std(5, &fprs[5 * offset]);
	fpu_std(6, &fprs[6 * offset]);
	fpu_std(7, &fprs[7 * offset]);
	fpu_std(8, &fprs[8 * offset]);
	fpu_std(9, &fprs[9 * offset]);
	fpu_std(10, &fprs[10 * offset]);
	fpu_std(11, &fprs[11 * offset]);
	fpu_std(12, &fprs[12 * offset]);
	fpu_std(13, &fprs[13 * offset]);
	fpu_std(14, &fprs[14 * offset]);
	fpu_std(15, &fprs[15 * offset]);
}

static __always_inline void __load_fp_regs(freg_t *fprs, unsigned int offset)
{
	fpu_ld(0, &fprs[0 * offset]);
	fpu_ld(1, &fprs[1 * offset]);
	fpu_ld(2, &fprs[2 * offset]);
	fpu_ld(3, &fprs[3 * offset]);
	fpu_ld(4, &fprs[4 * offset]);
	fpu_ld(5, &fprs[5 * offset]);
	fpu_ld(6, &fprs[6 * offset]);
	fpu_ld(7, &fprs[7 * offset]);
	fpu_ld(8, &fprs[8 * offset]);
	fpu_ld(9, &fprs[9 * offset]);
	fpu_ld(10, &fprs[10 * offset]);
	fpu_ld(11, &fprs[11 * offset]);
	fpu_ld(12, &fprs[12 * offset]);
	fpu_ld(13, &fprs[13 * offset]);
	fpu_ld(14, &fprs[14 * offset]);
	fpu_ld(15, &fprs[15 * offset]);
}

static __always_inline void save_fp_regs(freg_t *fprs)
{
	__save_fp_regs(fprs, sizeof(freg_t) / sizeof(freg_t));
}

static __always_inline void load_fp_regs(freg_t *fprs)
{
	__load_fp_regs(fprs, sizeof(freg_t) / sizeof(freg_t));
}

static __always_inline void save_fp_regs_vx(__vector128 *vxrs)
{
	freg_t *fprs = (freg_t *)&vxrs[0].high;

	__save_fp_regs(fprs, sizeof(__vector128) / sizeof(freg_t));
}

static __always_inline void load_fp_regs_vx(__vector128 *vxrs)
{
	freg_t *fprs = (freg_t *)&vxrs[0].high;

	__load_fp_regs(fprs, sizeof(__vector128) / sizeof(freg_t));
}

static inline void load_user_fpu_regs(void)
{
	struct thread_struct *thread = &current->thread;

	if (!thread->ufpu_flags)
		return;
	load_fpu_state(&thread->ufpu, thread->ufpu_flags);
	thread->ufpu_flags = 0;
}

static __always_inline void __save_user_fpu_regs(struct thread_struct *thread, int flags)
{
	save_fpu_state(&thread->ufpu, flags);
	__atomic_or(flags, &thread->ufpu_flags);
}

static inline void save_user_fpu_regs(void)
{
	struct thread_struct *thread = &current->thread;
	int mask, flags;

	mask = __atomic_or(KERNEL_FPC | KERNEL_VXR, &thread->kfpu_flags);
	flags = ~READ_ONCE(thread->ufpu_flags) & (KERNEL_FPC | KERNEL_VXR);
	if (flags)
		__save_user_fpu_regs(thread, flags);
	barrier();
	WRITE_ONCE(thread->kfpu_flags, mask);
}

static __always_inline void _kernel_fpu_begin(struct kernel_fpu *state, int flags)
{
	struct thread_struct *thread = &current->thread;
	int mask, uflags;

	mask = __atomic_or(flags, &thread->kfpu_flags);
	state->hdr.mask = mask;
	uflags = READ_ONCE(thread->ufpu_flags);
	if ((uflags & flags) != flags)
		__save_user_fpu_regs(thread, ~uflags & flags);
	if (mask & flags)
		__kernel_fpu_begin(state, flags);
}

static __always_inline void _kernel_fpu_end(struct kernel_fpu *state, int flags)
{
	int mask = state->hdr.mask;

	if (mask & flags)
		__kernel_fpu_end(state, flags);
	barrier();
	WRITE_ONCE(current->thread.kfpu_flags, mask);
}

void __kernel_fpu_invalid_size(void);

static __always_inline void kernel_fpu_check_size(int flags, unsigned int size)
{
	unsigned int cnt = 0;

	if (flags & KERNEL_VXR_V0V7)
		cnt += 8;
	if (flags & KERNEL_VXR_V8V15)
		cnt += 8;
	if (flags & KERNEL_VXR_V16V23)
		cnt += 8;
	if (flags & KERNEL_VXR_V24V31)
		cnt += 8;
	if (cnt != size)
		__kernel_fpu_invalid_size();
}

#define kernel_fpu_begin(state, flags)					\
{									\
	typeof(state) s = (state);					\
	int _flags = (flags);						\
									\
	kernel_fpu_check_size(_flags, ARRAY_SIZE(s->vxrs));		\
	_kernel_fpu_begin((struct kernel_fpu *)s, _flags);		\
}

#define kernel_fpu_end(state, flags)					\
{									\
	typeof(state) s = (state);					\
	int _flags = (flags);						\
									\
	kernel_fpu_check_size(_flags, ARRAY_SIZE(s->vxrs));		\
	_kernel_fpu_end((struct kernel_fpu *)s, _flags);		\
}

static inline void save_kernel_fpu_regs(struct thread_struct *thread)
{
	if (!thread->kfpu_flags)
		return;
	save_fpu_state(&thread->kfpu, thread->kfpu_flags);
}

static inline void restore_kernel_fpu_regs(struct thread_struct *thread)
{
	if (!thread->kfpu_flags)
		return;
	load_fpu_state(&thread->kfpu, thread->kfpu_flags);
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
	convert_vx_to_fp((freg_t *)&fpregs->fprs, fpu->vxrs);
}

static inline void fpregs_load(_s390_fp_regs *fpregs, struct fpu *fpu)
{
	fpu->fpc = fpregs->fpc;
	convert_fp_to_vx(fpu->vxrs, (freg_t *)&fpregs->fprs);
}

#endif /* _ASM_S390_FPU_H */
