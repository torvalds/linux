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
 * Use for_each_leaf_0x2_entry() to iterate over the register output in
 * parsed form.
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
 * for_each_leaf_0x2_entry() - Iterator for parsed leaf 0x2 descriptors
 * @regs:   Leaf 0x2 register output, returned by cpuid_get_leaf_0x2_regs()
 * @__ptr:  u8 pointer, for macro internal use only
 * @entry:  Pointer to parsed descriptor information at each iteration
 *
 * Loop over the 1-byte descriptors in the passed leaf 0x2 output registers
 * @regs.  Provide the parsed information for each descriptor through @entry.
 *
 * To handle cache-specific descriptors, switch on @entry->c_type.  For TLB
 * descriptors, switch on @entry->t_type.
 *
 * Example usage for cache descriptors::
 *
 *	const struct leaf_0x2_table *entry;
 *	union leaf_0x2_regs regs;
 *	u8 *ptr;
 *
 *	cpuid_get_leaf_0x2_regs(&regs);
 *	for_each_leaf_0x2_entry(regs, ptr, entry) {
 *		switch (entry->c_type) {
 *			...
 *		}
 *	}
 */
#define for_each_leaf_0x2_entry(regs, __ptr, entry)				\
	for (__ptr = &(regs).desc[1];						\
	     __ptr < &(regs).desc[16] && (entry = &cpuid_0x2_table[*__ptr]);	\
	     __ptr++)

#endif /* _ASM_X86_CPUID_LEAF_0x2_API_H */
