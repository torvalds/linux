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

#endif /* __ASM_SH_UACCESS_32_H */
