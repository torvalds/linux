/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_RUNTIME_CONST_H
#define _ASM_RUNTIME_CONST_H

#ifdef MODULE
  #error "Cannot use runtime-const infrastructure from modules"
#endif

#include <asm/cacheflush.h>
#include <asm/text-patching.h>

/* Sigh. You can still run arm64 in BE mode */
#include <asm/byteorder.h>

#define runtime_const_ptr(sym) ({				\
	typeof(sym) __ret;					\
	asm_inline("1:\t"					\
		"movz %0, #0xcdef\n\t"				\
		"movk %0, #0x89ab, lsl #16\n\t"			\
		"movk %0, #0x4567, lsl #32\n\t"			\
		"movk %0, #0x0123, lsl #48\n\t"			\
		".pushsection runtime_ptr_" #sym ",\"a\"\n\t"	\
		".long 1b - .\n\t"				\
		".popsection"					\
		:"=r" (__ret));					\
	__ret; })

#define runtime_const_shift_right_32(val, sym) ({		\
	unsigned long __ret;					\
	asm_inline("1:\t"					\
		"lsr %w0,%w1,#12\n\t"				\
		".pushsection runtime_shift_" #sym ",\"a\"\n\t"	\
		".long 1b - .\n\t"				\
		".popsection"					\
		:"=r" (__ret)					\
		:"r" (0u+(val)));				\
	__ret; })

#define runtime_const_mask_32(val, sym) ({			\
	unsigned long __ret;					\
	asm_inline("1:\t"					\
		"ubfx %w0, %w1, #0, #32\n\t"			\
		".pushsection runtime_mask_" #sym ",\"a\"\n\t"	\
		".long 1b - .\n\t"				\
		".popsection"					\
		:"=r" (__ret)					\
		:"r" (0u+(val)));				\
	__ret; })

#define runtime_const_init(type, sym) do {		\
	extern s32 __start_runtime_##type##_##sym[];	\
	extern s32 __stop_runtime_##type##_##sym[];	\
	runtime_const_fixup(__runtime_fixup_##type,	\
		(unsigned long)(sym), 			\
		__start_runtime_##type##_##sym,		\
		__stop_runtime_##type##_##sym);		\
} while (0)

/* 16-bit immediate for wide move (movz and movk) in bits 5..20 */
static inline void __runtime_fixup_16(__le32 *p, unsigned int val)
{
	u32 insn = le32_to_cpu(*p);
	insn &= 0xffe0001f;
	insn |= (val & 0xffff) << 5;
	aarch64_insn_patch_text_nosync(p, insn);
}

static inline void __runtime_fixup_ptr(void *where, unsigned long val)
{
	__le32 *p = where;
	__runtime_fixup_16(p, val);
	__runtime_fixup_16(p+1, val >> 16);
	__runtime_fixup_16(p+2, val >> 32);
	__runtime_fixup_16(p+3, val >> 48);
}

/* Immediate value is 6 bits starting at bit #16 */
static inline void __runtime_fixup_shift(void *where, unsigned long val)
{
	__le32 *p = where;
	u32 insn = le32_to_cpu(*p);
	insn &= 0xffc0ffff;
	insn |= (val & 63) << 16;
	aarch64_insn_patch_text_nosync(p, insn);
}

static inline void __runtime_fixup_mask(void *where, unsigned long val)
{
	unsigned int width = (val) ? __fls(val) + 1 : 0;
	__le32 *p = where;
	u32 insn;

	/*
	 * XXX: Current implementation only supports patching masks of
	 * form GENMASK(n, 0) (n >= 0) using a single UBFX instruction
	 * to improve performance, density, and covers all the current
	 * use-cases.
	 *
	 * When the need arises to support any generic mask, and this
	 * BUG_ON() is tripped, consider using a:
	 *
	 *   movz %w0, #imm16
	 *   movk %w0, #imm16, lsl #16
	 *
	 * sequence to load the 32bit const mask, and perform a logical
	 * and outside the asm block before returning the result. Fixup
	 * can simply reuse the existing __runtime_fixup_16() to patch
	 * the individual mov instructions.
	 */
	BUG_ON(!val || width > 32 || (GENMASK(width - 1, 0) != val));

	/*
	 * The width of the mask is encoded as (width - 1) in imms
	 * which is 6 bits starting at bit #10.
	 */
	insn = le32_to_cpu(*p);
	insn &= 0xffff03ff;
	insn |= ((width - 1) & 0x1f) << 10;
	aarch64_insn_patch_text_nosync(p, insn);
}

static inline void runtime_const_fixup(void (*fn)(void *, unsigned long),
	unsigned long val, s32 *start, s32 *end)
{
	while (start < end) {
		fn(*start + (void *)start, val);
		start++;
	}
}

#endif
