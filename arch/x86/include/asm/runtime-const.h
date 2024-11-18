/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_RUNTIME_CONST_H
#define _ASM_RUNTIME_CONST_H

#define runtime_const_ptr(sym) ({				\
	typeof(sym) __ret;					\
	asm_inline("mov %1,%0\n1:\n"				\
		".pushsection runtime_ptr_" #sym ",\"a\"\n\t"	\
		".long 1b - %c2 - .\n"				\
		".popsection"					\
		:"=r" (__ret)					\
		:"i" ((unsigned long)0x0123456789abcdefull),	\
		 "i" (sizeof(long)));				\
	__ret; })

// The 'typeof' will create at _least_ a 32-bit type, but
// will happily also take a bigger type and the 'shrl' will
// clear the upper bits
#define runtime_const_shift_right_32(val, sym) ({		\
	typeof(0u+(val)) __ret = (val);				\
	asm_inline("shrl $12,%k0\n1:\n"				\
		".pushsection runtime_shift_" #sym ",\"a\"\n\t"	\
		".long 1b - 1 - .\n"				\
		".popsection"					\
		:"+r" (__ret));					\
	__ret; })

#define runtime_const_init(type, sym) do {		\
	extern s32 __start_runtime_##type##_##sym[];	\
	extern s32 __stop_runtime_##type##_##sym[];	\
	runtime_const_fixup(__runtime_fixup_##type,	\
		(unsigned long)(sym), 			\
		__start_runtime_##type##_##sym,		\
		__stop_runtime_##type##_##sym);		\
} while (0)

/*
 * The text patching is trivial - you can only do this at init time,
 * when the text section hasn't been marked RO, and before the text
 * has ever been executed.
 */
static inline void __runtime_fixup_ptr(void *where, unsigned long val)
{
	*(unsigned long *)where = val;
}

static inline void __runtime_fixup_shift(void *where, unsigned long val)
{
	*(unsigned char *)where = val;
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
