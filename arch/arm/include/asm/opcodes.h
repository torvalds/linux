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
 * Assembler opcode byteswap helpers.
 * These are only intended for use by this header: don't use them directly,
 * because they will be suboptimal in most cases.
 */
#define ___asm_opcode_swab32(x) (	\
	  (((x) << 24) & 0xFF000000)	\
	| (((x) <<  8) & 0x00FF0000)	\
	| (((x) >>  8) & 0x0000FF00)	\
	| (((x) >> 24) & 0x000000FF)	\
)
#define ___asm_opcode_swab16(x) (	\
	  (((x) << 8) & 0xFF00)		\
	| (((x) >> 8) & 0x00FF)		\
)
#define ___asm_opcode_swahb32(x) (	\
	  (((x) << 8) & 0xFF00FF00)	\
	| (((x) >> 8) & 0x00FF00FF)	\
)
#define ___asm_opcode_swahw32(x) (	\
	  (((x) << 16) & 0xFFFF0000)	\
	| (((x) >> 16) & 0x0000FFFF)	\
)
#define ___asm_opcode_identity32(x) ((x) & 0xFFFFFFFF)
#define ___asm_opcode_identity16(x) ((x) & 0xFFFF)


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
 *
 * The ___asm variants are intended only for use by this header, in situations
 * involving inline assembler.  For .S files, the normal __opcode_*() macros
 * should do the right thing.
 */
#ifdef __ASSEMBLY__

#define ___opcode_swab32(x) ___asm_opcode_swab32(x)
#define ___opcode_swab16(x) ___asm_opcode_swab16(x)
#define ___opcode_swahb32(x) ___asm_opcode_swahb32(x)
#define ___opcode_swahw32(x) ___asm_opcode_swahw32(x)
#define ___opcode_identity32(x) ___asm_opcode_identity32(x)
#define ___opcode_identity16(x) ___asm_opcode_identity16(x)

#else /* ! __ASSEMBLY__ */

#include <linux/types.h>
#include <linux/swab.h>

#define ___opcode_swab32(x) swab32(x)
#define ___opcode_swab16(x) swab16(x)
#define ___opcode_swahb32(x) swahb32(x)
#define ___opcode_swahw32(x) swahw32(x)
#define ___opcode_identity32(x) ((u32)(x))
#define ___opcode_identity16(x) ((u16)(x))

#endif /* ! __ASSEMBLY__ */


#ifdef CONFIG_CPU_ENDIAN_BE8

#define __opcode_to_mem_arm(x) ___opcode_swab32(x)
#define __opcode_to_mem_thumb16(x) ___opcode_swab16(x)
#define __opcode_to_mem_thumb32(x) ___opcode_swahb32(x)
#define ___asm_opcode_to_mem_arm(x) ___asm_opcode_swab32(x)
#define ___asm_opcode_to_mem_thumb16(x) ___asm_opcode_swab16(x)
#define ___asm_opcode_to_mem_thumb32(x) ___asm_opcode_swahb32(x)

#else /* ! CONFIG_CPU_ENDIAN_BE8 */

#define __opcode_to_mem_arm(x) ___opcode_identity32(x)
#define __opcode_to_mem_thumb16(x) ___opcode_identity16(x)
#define ___asm_opcode_to_mem_arm(x) ___asm_opcode_identity32(x)
#define ___asm_opcode_to_mem_thumb16(x) ___asm_opcode_identity16(x)
#ifndef CONFIG_CPU_ENDIAN_BE32
/*
 * On BE32 systems, using 32-bit accesses to store Thumb instructions will not
 * work in all cases, due to alignment constraints.  For now, a correct
 * version is not provided for BE32.
 */
#define __opcode_to_mem_thumb32(x) ___opcode_swahw32(x)
#define ___asm_opcode_to_mem_thumb32(x) ___asm_opcode_swahw32(x)
#endif

#endif /* ! CONFIG_CPU_ENDIAN_BE8 */

#define __mem_to_opcode_arm(x) __opcode_to_mem_arm(x)
#define __mem_to_opcode_thumb16(x) __opcode_to_mem_thumb16(x)
#ifndef CONFIG_CPU_ENDIAN_BE32
#define __mem_to_opcode_thumb32(x) __opcode_to_mem_thumb32(x)
#endif

/* Operations specific to Thumb opcodes */

/* Instruction size checks: */
#define __opcode_is_thumb32(x) (		\
	   ((x) & 0xF8000000) == 0xE8000000	\
	|| ((x) & 0xF0000000) == 0xF0000000	\
)
#define __opcode_is_thumb16(x) (					\
	   ((x) & 0xFFFF0000) == 0					\
	&& !(((x) & 0xF800) == 0xE800 || ((x) & 0xF000) == 0xF000)	\
)

/* Operations to construct or split 32-bit Thumb instructions: */
#define __opcode_thumb32_first(x) (___opcode_identity16((x) >> 16))
#define __opcode_thumb32_second(x) (___opcode_identity16(x))
#define __opcode_thumb32_compose(first, second) (			\
	  (___opcode_identity32(___opcode_identity16(first)) << 16)	\
	| ___opcode_identity32(___opcode_identity16(second))		\
)
#define ___asm_opcode_thumb32_first(x) (___asm_opcode_identity16((x) >> 16))
#define ___asm_opcode_thumb32_second(x) (___asm_opcode_identity16(x))
#define ___asm_opcode_thumb32_compose(first, second) (			    \
	  (___asm_opcode_identity32(___asm_opcode_identity16(first)) << 16) \
	| ___asm_opcode_identity32(___asm_opcode_identity16(second))	    \
)

#endif /* __ASM_ARM_OPCODES_H */
