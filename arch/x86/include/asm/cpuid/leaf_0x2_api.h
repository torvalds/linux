/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_CPUID_LEAF_0x2_API_H
#define _ASM_X86_CPUID_LEAF_0x2_API_H

#include <asm/cpuid/api.h>
#include <asm/cpuid/types.h>

/**
 * cpuid_get_leaf_0x2_regs() - Return sanitized leaf 0x2 register output
 * @regs:	Output parameter
 *
 * Query CPUID leaf 0x2 and store its output in @regs.	Force set any
 * invalid 1-byte descriptor returned by the hardware to zero (the NULL
 * cache/TLB descriptor) before returning it to the caller.
 *
 * Use for_each_leaf_0x2_desc() to iterate over the returned output.
 */
static inline void cpuid_get_leaf_0x2_regs(union leaf_0x2_regs *regs)
{
	cpuid_leaf(0x2, regs);

	/*
	 * All Intel CPUs must report an iteration count of 1.	In case
	 * of bogus hardware, treat all returned descriptors as NULL.
	 */
	if (regs->desc[0] != 0x01) {
		for (int i = 0; i < 4; i++)
			regs->regv[i] = 0;
		return;
	}

	/*
	 * The most significant bit (MSB) of each register must be clear.
	 * If a register is invalid, replace its descriptors with NULL.
	 */
	for (int i = 0; i < 4; i++) {
		if (regs->reg[i].invalid)
			regs->regv[i] = 0;
	}
}

/**
 * for_each_leaf_0x2_desc() - Iterator for CPUID leaf 0x2 descriptors
 * @regs:	Leaf 0x2 output, as returned by cpuid_get_leaf_0x2_regs()
 * @desc:	Pointer to the returned descriptor for each iteration
 *
 * Loop over the 1-byte descriptors in the passed leaf 0x2 output registers
 * @regs.  Provide each descriptor through @desc.
 *
 * Note that the first byte is skipped as it is not a descriptor.
 *
 * Sample usage::
 *
 *	union leaf_0x2_regs regs;
 *	u8 *desc;
 *
 *	cpuid_get_leaf_0x2_regs(&regs);
 *	for_each_leaf_0x2_desc(regs, desc) {
 *		// Handle *desc value
 *	}
 */
#define for_each_leaf_0x2_desc(regs, desc)				\
	for (desc = &(regs).desc[1]; desc < &(regs).desc[16]; desc++)

#endif /* _ASM_X86_CPUID_LEAF_0x2_API_H */
