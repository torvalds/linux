/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_S390_RUNTIME_CONST_H
#define _ASM_S390_RUNTIME_CONST_H

#include <linux/uaccess.h>

#define runtime_const_ptr(sym)					\
({								\
	typeof(sym) __ret;					\
								\
	asm_inline(						\
		"0:	iihf	%[__ret],%[c1]\n"		\
		"	iilf	%[__ret],%[c2]\n"		\
		".pushsection runtime_ptr_" #sym ",\"a\"\n"	\
		".long 0b - .\n"				\
		".popsection"					\
		: [__ret] "=d" (__ret)				\
		: [c1] "i" (0x01234567UL),			\
		  [c2] "i" (0x89abcdefUL));			\
	__ret;							\
})

#define runtime_const_shift_right_32(val, sym)			\
({								\
	unsigned int __ret = (val);				\
								\
	asm_inline(						\
		"0:	srl	%[__ret],12\n"			\
		".pushsection runtime_shift_" #sym ",\"a\"\n"	\
		".long 0b - .\n"				\
		".popsection"					\
		: [__ret] "+d" (__ret));			\
	__ret;							\
})

#define runtime_const_init(type, sym) do {			\
	extern s32 __start_runtime_##type##_##sym[];		\
	extern s32 __stop_runtime_##type##_##sym[];		\
								\
	runtime_const_fixup(__runtime_fixup_##type,		\
			    (unsigned long)(sym),		\
			    __start_runtime_##type##_##sym,	\
			    __stop_runtime_##type##_##sym);	\
} while (0)

/* 32-bit immediate for iihf and iilf in bits in I2 field */
static inline void __runtime_fixup_32(u32 *p, unsigned int val)
{
	s390_kernel_write(p, &val, sizeof(val));
}

static inline void __runtime_fixup_ptr(void *where, unsigned long val)
{
	__runtime_fixup_32(where + 2, val >> 32);
	__runtime_fixup_32(where + 8, val);
}

/* Immediate value is lower 12 bits of D2 field of srl */
static inline void __runtime_fixup_shift(void *where, unsigned long val)
{
	u32 insn = *(u32 *)where;

	insn &= 0xfffff000;
	insn |= (val & 63);
	s390_kernel_write(where, &insn, sizeof(insn));
}

static inline void runtime_const_fixup(void (*fn)(void *, unsigned long),
				       unsigned long val, s32 *start, s32 *end)
{
	while (start < end) {
		fn(*start + (void *)start, val);
		start++;
	}
}

#endif /* _ASM_S390_RUNTIME_CONST_H */
