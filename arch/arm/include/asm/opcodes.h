/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  arch/arm/include/asm/opcodes.h
 */

#ifndef __ASM_ARM_OPCODES_H
#define __ASM_ARM_OPCODES_H

#ifndef __ASSEMBLY__
#include <linux/linkage.h>
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
#ifdef CONFIG_CPU_ENDIAN_BE32
#ifndef __ASSEMBLY__
/*
 * On BE32 systems, using 32-bit accesses to store Thumb instructions will not
 * work in all cases, due to alignment constraints.  For now, a correct
 * version is not provided for BE32, but the prototype needs to be there
 * to compile patch.c.
 */
extern __u32 __opcode_to_mem_thumb32(__u32);
#endif
#else
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

/*
 * Opcode injection helpers
 *
 * In rare cases it is necessary to assemble an opcode which the
 * assembler does not support directly, or which would normally be
 * rejected because of the CFLAGS or AFLAGS used to build the affected
 * file.
 *
 * Before using these macros, consider carefully whether it is feasible
 * instead to change the build flags for your file, or whether it really
 * makes sense to support old assembler versions when building that
 * particular kernel feature.
 *
 * The macros defined here should only be used where there is no viable
 * alternative.
 *
 *
 * __inst_arm(x): emit the specified ARM opcode
 * __inst_thumb16(x): emit the specified 16-bit Thumb opcode
 * __inst_thumb32(x): emit the specified 32-bit Thumb opcode
 *
 * __inst_arm_thumb16(arm, thumb): emit either the specified arm or
 *	16-bit Thumb opcode, depending on whether an ARM or Thumb-2
 *	kernel is being built
 *
 * __inst_arm_thumb32(arm, thumb): emit either the specified arm or
 *	32-bit Thumb opcode, depending on whether an ARM or Thumb-2
 *	kernel is being built
 *
 *
 * Note that using these macros directly is poor practice.  Instead, you
 * should use them to define human-readable wrapper macros to encode the
 * instructions that you care about.  In code which might run on ARMv7 or
 * above, you can usually use the __inst_arm_thumb{16,32} macros to
 * specify the ARM and Thumb alternatives at the same time.  This ensures
 * that the correct opcode gets emitted depending on the instruction set
 * used for the kernel build.
 *
 * Look at opcodes-virt.h for an example of how to use these macros.
 */
#include <linux/stringify.h>

#define __inst_arm(x) ___inst_arm(___asm_opcode_to_mem_arm(x))
#define __inst_thumb32(x) ___inst_thumb32(				\
	___asm_opcode_to_mem_thumb16(___asm_opcode_thumb32_first(x)),	\
	___asm_opcode_to_mem_thumb16(___asm_opcode_thumb32_second(x))	\
)
#define __inst_thumb16(x) ___inst_thumb16(___asm_opcode_to_mem_thumb16(x))

#ifdef CONFIG_THUMB2_KERNEL
#define __inst_arm_thumb16(arm_opcode, thumb_opcode) \
	__inst_thumb16(thumb_opcode)
#define __inst_arm_thumb32(arm_opcode, thumb_opcode) \
	__inst_thumb32(thumb_opcode)
#else
#define __inst_arm_thumb16(arm_opcode, thumb_opcode) __inst_arm(arm_opcode)
#define __inst_arm_thumb32(arm_opcode, thumb_opcode) __inst_arm(arm_opcode)
#endif

/* Helpers for the helpers.  Don't use these directly. */
#ifdef __ASSEMBLY__
#define ___inst_arm(x) .long x
#define ___inst_thumb16(x) .short x
#define ___inst_thumb32(first, second) .short first, second
#else
#define ___inst_arm(x) ".long " __stringify(x) "\n\t"
#define ___inst_thumb16(x) ".short " __stringify(x) "\n\t"
#define ___inst_thumb32(first, second) \
	".short " __stringify(first) ", " __stringify(second) "\n\t"
#endif

#endif /* __ASM_ARM_OPCODES_H */
