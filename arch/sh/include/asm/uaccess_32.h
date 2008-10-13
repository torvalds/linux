/*
 * User space memory access functions
 *
 * Copyright (C) 1999, 2002  Niibe Yutaka
 * Copyright (C) 2003 - 2008  Paul Mundt
 *
 *  Based on:
 *     MIPS implementation version 1.15 by
 *              Copyright (C) 1996, 1997, 1998 by Ralf Baechle
 *     and i386 version.
 */
#ifndef __ASM_SH_UACCESS_32_H
#define __ASM_SH_UACCESS_32_H

#define __get_user_size(x,ptr,size,retval)			\
do {								\
	retval = 0;						\
	switch (size) {						\
	case 1:							\
		__get_user_asm(x, ptr, retval, "b");		\
		break;						\
	case 2:							\
		__get_user_asm(x, ptr, retval, "w");		\
		break;						\
	case 4:							\
		__get_user_asm(x, ptr, retval, "l");		\
		break;						\
	default:						\
		__get_user_unknown();				\
		break;						\
	}							\
} while (0)

#ifdef CONFIG_MMU
#define __get_user_asm(x, addr, err, insn) \
({ \
__asm__ __volatile__( \
	"1:\n\t" \
	"mov." insn "	%2, %1\n\t" \
	"2:\n" \
	".section	.fixup,\"ax\"\n" \
	"3:\n\t" \
	"mov	#0, %1\n\t" \
	"mov.l	4f, %0\n\t" \
	"jmp	@%0\n\t" \
	" mov	%3, %0\n\t" \
	".balign	4\n" \
	"4:	.long	2b\n\t" \
	".previous\n" \
	".section	__ex_table,\"a\"\n\t" \
	".long	1b, 3b\n\t" \
	".previous" \
	:"=&r" (err), "=&r" (x) \
	:"m" (__m(addr)), "i" (-EFAULT), "0" (err)); })
#else
#define __get_user_asm(x, addr, err, insn)		\
do {							\
	__asm__ __volatile__ (				\
		"mov." insn "	%1, %0\n\t"		\
		: "=&r" (x)				\
		: "m" (__m(addr))			\
	);						\
} while (0)
#endif /* CONFIG_MMU */

extern void __get_user_unknown(void);

#define __put_user_size(x,ptr,size,retval)		\
do {							\
	retval = 0;					\
	switch (size) {					\
	case 1:						\
		__put_user_asm(x, ptr, retval, "b");	\
		break;					\
	case 2:						\
		__put_user_asm(x, ptr, retval, "w");	\
		break;					\
	case 4:						\
		__put_user_asm(x, ptr, retval, "l");	\
		break;					\
	case 8:						\
		__put_user_u64(x, ptr, retval);		\
		break;					\
	default:					\
		__put_user_unknown();			\
	}						\
} while (0)

#ifdef CONFIG_MMU
#define __put_user_asm(x, addr, err, insn)			\
do {								\
	__asm__ __volatile__ (					\
		"1:\n\t"					\
		"mov." insn "	%1, %2\n\t"			\
		"2:\n"						\
		".section	.fixup,\"ax\"\n"		\
		"3:\n\t"					\
		"mov.l	4f, %0\n\t"				\
		"jmp	@%0\n\t"				\
		" mov	%3, %0\n\t"				\
		".balign	4\n"				\
		"4:	.long	2b\n\t"				\
		".previous\n"					\
		".section	__ex_table,\"a\"\n\t"		\
		".long	1b, 3b\n\t"				\
		".previous"					\
		: "=&r" (err)					\
		: "r" (x), "m" (__m(addr)), "i" (-EFAULT),	\
		  "0" (err)					\
		: "memory"					\
	);							\
} while (0)
#else
#define __put_user_asm(x, addr, err, insn)		\
do {							\
	__asm__ __volatile__ (				\
		"mov." insn "	%0, %1\n\t"		\
		: /* no outputs */			\
		: "r" (x), "m" (__m(addr))		\
		: "memory"				\
	);						\
} while (0)
#endif /* CONFIG_MMU */

#if defined(CONFIG_CPU_LITTLE_ENDIAN)
#define __put_user_u64(val,addr,retval) \
({ \
__asm__ __volatile__( \
	"1:\n\t" \
	"mov.l	%R1,%2\n\t" \
	"mov.l	%S1,%T2\n\t" \
	"2:\n" \
	".section	.fixup,\"ax\"\n" \
	"3:\n\t" \
	"mov.l	4f,%0\n\t" \
	"jmp	@%0\n\t" \
	" mov	%3,%0\n\t" \
	".balign	4\n" \
	"4:	.long	2b\n\t" \
	".previous\n" \
	".section	__ex_table,\"a\"\n\t" \
	".long	1b, 3b\n\t" \
	".previous" \
	: "=r" (retval) \
	: "r" (val), "m" (__m(addr)), "i" (-EFAULT), "0" (retval) \
        : "memory"); })
#else
#define __put_user_u64(val,addr,retval) \
({ \
__asm__ __volatile__( \
	"1:\n\t" \
	"mov.l	%S1,%2\n\t" \
	"mov.l	%R1,%T2\n\t" \
	"2:\n" \
	".section	.fixup,\"ax\"\n" \
	"3:\n\t" \
	"mov.l	4f,%0\n\t" \
	"jmp	@%0\n\t" \
	" mov	%3,%0\n\t" \
	".balign	4\n" \
	"4:	.long	2b\n\t" \
	".previous\n" \
	".section	__ex_table,\"a\"\n\t" \
	".long	1b, 3b\n\t" \
	".previous" \
	: "=r" (retval) \
	: "r" (val), "m" (__m(addr)), "i" (-EFAULT), "0" (retval) \
        : "memory"); })
#endif

extern void __put_user_unknown(void);

static inline int
__strncpy_from_user(unsigned long __dest, unsigned long __user __src, int __count)
{
	__kernel_size_t res;
	unsigned long __dummy, _d, _s, _c;

	__asm__ __volatile__(
		"9:\n"
		"mov.b	@%2+, %1\n\t"
		"cmp/eq	#0, %1\n\t"
		"bt/s	2f\n"
		"1:\n"
		"mov.b	%1, @%3\n\t"
		"dt	%4\n\t"
		"bf/s	9b\n\t"
		" add	#1, %3\n\t"
		"2:\n\t"
		"sub	%4, %0\n"
		"3:\n"
		".section .fixup,\"ax\"\n"
		"4:\n\t"
		"mov.l	5f, %1\n\t"
		"jmp	@%1\n\t"
		" mov	%9, %0\n\t"
		".balign 4\n"
		"5:	.long 3b\n"
		".previous\n"
		".section __ex_table,\"a\"\n"
		"	.balign 4\n"
		"	.long 9b,4b\n"
		".previous"
		: "=r" (res), "=&z" (__dummy), "=r" (_s), "=r" (_d), "=r"(_c)
		: "0" (__count), "2" (__src), "3" (__dest), "4" (__count),
		  "i" (-EFAULT)
		: "memory", "t");

	return res;
}

/*
 * Return the size of a string (including the ending 0 even when we have
 * exceeded the maximum string length).
 */
static inline long __strnlen_user(const char __user *__s, long __n)
{
	unsigned long res;
	unsigned long __dummy;

	__asm__ __volatile__(
		"1:\t"
		"mov.b	@(%0,%3), %1\n\t"
		"cmp/eq	%4, %0\n\t"
		"bt/s	2f\n\t"
		" add	#1, %0\n\t"
		"tst	%1, %1\n\t"
		"bf	1b\n\t"
		"2:\n"
		".section .fixup,\"ax\"\n"
		"3:\n\t"
		"mov.l	4f, %1\n\t"
		"jmp	@%1\n\t"
		" mov	#0, %0\n"
		".balign 4\n"
		"4:	.long 2b\n"
		".previous\n"
		".section __ex_table,\"a\"\n"
		"	.balign 4\n"
		"	.long 1b,3b\n"
		".previous"
		: "=z" (res), "=&r" (__dummy)
		: "0" (0), "r" (__s), "r" (__n)
		: "t");
	return res;
}

#endif /* __ASM_SH_UACCESS_32_H */
