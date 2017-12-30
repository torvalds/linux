/* MN10300 Userspace accessor functions
 *
 * Copyright (C) 2007 Matsushita Electric Industrial Co., Ltd.
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#include <linux/uaccess.h>

/*
 * Copy a null terminated string from userspace.
 */
#define __do_strncpy_from_user(dst, src, count, res)		\
do {								\
	int w;							\
	asm volatile(						\
		"	mov	%1,%0\n"			\
		"	cmp	0,%1\n"				\
		"	beq	2f\n"				\
		"0:\n"						\
		"	movbu	(%5),%2\n"			\
		"1:\n"						\
		"	movbu	%2,(%6)\n"			\
		"	inc	%5\n"				\
		"	inc	%6\n"				\
		"	cmp	0,%2\n"				\
		"	beq	2f\n"				\
		"	add	-1,%1\n"			\
		"	bne	0b\n"				\
		"2:\n"						\
		"	sub	%1,%0\n"			\
		"3:\n"						\
		"	.section .fixup,\"ax\"\n"		\
		"4:\n"						\
		"	mov	%3,%0\n"			\
		"	jmp	3b\n"				\
		"	.previous\n"				\
		"	.section __ex_table,\"a\"\n"		\
		"	.balign 4\n"				\
		"	.long 0b,4b\n"				\
		"	.long 1b,4b\n"				\
		"	.previous"				\
		:"=&r"(res), "=r"(count), "=&r"(w)		\
		:"i"(-EFAULT), "1"(count), "a"(src), "a"(dst)	\
		: "memory", "cc");					\
} while (0)

long
strncpy_from_user(char *dst, const char *src, long count)
{
	long res = -EFAULT;
	if (access_ok(VERIFY_READ, src, 1))
		__do_strncpy_from_user(dst, src, count, res);
	return res;
}


/*
 * Clear a userspace memory
 */
#define __do_clear_user(addr, size)		\
do {						\
	int w;					\
	asm volatile(				\
		"	cmp 0,%0\n"		\
		"	beq 1f\n"		\
		"	clr %1\n"		\
		"0:	movbu %1,(%3,%2)\n"	\
		"	inc %3\n"		\
		"	cmp %0,%3\n"		\
		"	bne 0b\n"		\
		"1:\n"				\
		"	sub %3,%0\n"		\
		"2:\n"				\
		".section .fixup,\"ax\"\n"	\
		"3:	jmp 2b\n"		\
		".previous\n"			\
		".section __ex_table,\"a\"\n"	\
		"       .balign 4\n"		\
		"       .long 0b,3b\n"		\
		".previous\n"			\
		: "+r"(size), "=&r"(w)		\
		: "a"(addr), "d"(0)		\
		: "memory", "cc");		\
} while (0)

unsigned long
__clear_user(void *to, unsigned long n)
{
	__do_clear_user(to, n);
	return n;
}

unsigned long
clear_user(void *to, unsigned long n)
{
	if (access_ok(VERIFY_WRITE, to, n))
		__do_clear_user(to, n);
	return n;
}

/*
 * Return the size of a string (including the ending 0)
 *
 * Return 0 on exception, a value greater than N if too long
 */
long strnlen_user(const char *s, long n)
{
	unsigned long res, w;

	if (!__addr_ok(s))
		return 0;

	if (n < 0 || n + (u_long) s > current_thread_info()->addr_limit.seg)
		n = current_thread_info()->addr_limit.seg - (u_long)s;

	asm volatile(
		"0:	cmp %4,%0\n"
		"	beq 2f\n"
		"1:	movbu (%0,%3),%1\n"
		"	inc %0\n"
		"	cmp 0,%1\n"
		"	beq 3f\n"
		"	bra 0b\n"
		"2:	clr %0\n"
		"3:\n"
		".section .fixup,\"ax\"\n"
		"4:	jmp 2b\n"
		".previous\n"
		".section __ex_table,\"a\"\n"
		"	.balign 4\n"
		"	.long 1b,4b\n"
		".previous\n"
		:"=d"(res), "=&r"(w)
		:"0"(0), "a"(s), "r"(n)
		: "memory", "cc");
	return res;
}
