/*
 *  arch/arm/include/asm/opcodes.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARM_OPCODES_H
#define __ASM_ARM_OPCODES_H

#ifndef __ASSEMBLY__
extern asmlinkage unsigned int arm_check_condition(u32 opcode, u32 psr);
#endif

#define ARM_OPCODE_CONDTEST_FAIL   0
#define ARM_OPCODE_CONDTEST_PASS   1
#define ARM_OPCODE_CONDTEST_UNCOND 2


/*
 * Opcode byteswap helpers
 *
 * These macros help with converting instructions between a canonical integer
 * format and in-memory representation, in an endianness-agnostic manner.
 *
 * __mem_to_opcode_*() convert from in-memory representation to canonical form.
 * __opcode_to_mem_*() convert from canonical form to in-memory representation.
 *
 *
 * Canonical instruction representation:
 *
 *	ARM:		0xKKLLMMNN
 *	Thumb 16-bit:	0x0000KKLL, where KK < 0xE8
 *	Thumb 32-bit:	0xKKLLMMNN, where KK >= 0xE8
 *
 * There is no way to distinguish an ARM instruction in canonical representation
 * from a Thumb instruction (just as these cannot be distinguished in memory).
 * Where this distinction is important, it needs to be tracked separately.
 *
 * Note that values in the range 0x0000E800..0xE7FFFFFF intentionally do not
 * represent any valid Thumb-2 instruction.  For this range,
 * __opcode_is_thumb32() and __opcode_is_thumb16() will both be false.
 */

#ifndef __ASSEMBLY__

#include <linux/types.h>
#include <linux/swab.h>

#ifdef CONFIG_CPU_ENDIAN_BE8

#define __opcode_to_mem_arm(x) swab32(x)
#define __opcode_to_mem_thumb16(x) swab16(x)
#define __opcode_to_mem_thumb32(x) swahb32(x)

#else /* ! CONFIG_CPU_ENDIAN_BE8 */

#define __opcode_to_mem_arm(x) ((u32)(x))
#define __opcode_to_mem_thumb16(x) ((u16)(x))
#ifndef CONFIG_CPU_ENDIAN_BE32
/*
 * On BE32 systems, using 32-bit accesses to store Thumb instructions will not
 * work in all cases, due to alignment constraints.  For now, a correct
 * version is not provided for BE32.
 */
#define __opcode_to_mem_thumb32(x) swahw32(x)
#endif

#endif /* ! CONFIG_CPU_ENDIAN_BE8 */

#define __mem_to_opcode_arm(x) __opcode_to_mem_arm(x)
#define __mem_to_opcode_thumb16(x) __opcode_to_mem_thumb16(x)
#ifndef CONFIG_CPU_ENDIAN_BE32
#define __mem_to_opcode_thumb32(x) __opcode_to_mem_thumb32(x)
#endif

/* Operations specific to Thumb opcodes */

/* Instruction size checks: */
#define __opcode_is_thumb32(x) ((u32)(x) >= 0xE8000000UL)
#define __opcode_is_thumb16(x) ((u32)(x) < 0xE800UL)

/* Operations to construct or split 32-bit Thumb instructions: */
#define __opcode_thumb32_first(x) ((u16)((x) >> 16))
#define __opcode_thumb32_second(x) ((u16)(x))
#define __opcode_thumb32_compose(first, second) \
	(((u32)(u16)(first) << 16) | (u32)(u16)(second))

#endif /* __ASSEMBLY__ */

#endif /* __ASM_ARM_OPCODES_H */
