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

#ifndef _ASM_S390_FPU_API_H
#define _ASM_S390_FPU_API_H

#include <linux/preempt.h>
#include <asm/asm-extable.h>
#include <asm/fpu/internal.h>

void save_fpu_regs(void);
void load_fpu_regs(void);
void __load_fpu_regs(void);

/**
 * sfpc_safe - Set floating point control register safely.
 * @fpc: new value for floating point control register
 *
 * Set floating point control register. This may lead to an exception,
 * since a saved value may have been modified by user space (ptrace,
 * signal return, kvm registers) to an invalid value. In such a case
 * set the floating point control register to zero.
 */
static inline void sfpc_safe(u32 fpc)
{
	asm volatile("\n"
		"0:	sfpc	%[fpc]\n"
		"1:	nopr	%%r7\n"
		".pushsection .fixup, \"ax\"\n"
		"2:	lghi	%[fpc],0\n"
		"	jg	0b\n"
		".popsection\n"
		EX_TABLE(1b, 2b)
		: [fpc] "+d" (fpc)
		: : "memory");
}

#define KERNEL_FPC		1
#define KERNEL_VXR_V0V7		2
#define KERNEL_VXR_V8V15	4
#define KERNEL_VXR_V16V23	8
#define KERNEL_VXR_V24V31	16

#define KERNEL_VXR_LOW		(KERNEL_VXR_V0V7|KERNEL_VXR_V8V15)
#define KERNEL_VXR_MID		(KERNEL_VXR_V8V15|KERNEL_VXR_V16V23)
#define KERNEL_VXR_HIGH		(KERNEL_VXR_V16V23|KERNEL_VXR_V24V31)

#define KERNEL_VXR		(KERNEL_VXR_LOW|KERNEL_VXR_HIGH)
#define KERNEL_FPR		(KERNEL_FPC|KERNEL_VXR_LOW)

struct kernel_fpu;

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
	if (!test_cpu_flag(CIF_FPU))
		/* Save user space FPU state and register contents */
		save_fpu_regs();
	else if (state->mask & flags)
		/* Save FPU/vector register in-use by the kernel */
		__kernel_fpu_begin(state, flags);
	S390_lowcore.fpu_flags |= flags;
}

static inline void kernel_fpu_end(struct kernel_fpu *state, u32 flags)
{
	S390_lowcore.fpu_flags = state->mask;
	if (state->mask & flags)
		/* Restore FPU/vector register in-use by the kernel */
		__kernel_fpu_end(state, flags);
	preempt_enable();
}

#endif /* _ASM_S390_FPU_API_H */
