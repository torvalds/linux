/*
 * User address space access functions.
 * The non inlined parts of asm-m32r/uaccess.h are here.
 *
 * Copyright 1997 Andi Kleen <ak@muc.de>
 * Copyright 1997 Linus Torvalds
 * Copyright 2001, 2002, 2004 Hirokazu Takata
 */
#include <linux/prefetch.h>
#include <linux/string.h>
#include <linux/thread_info.h>
#include <linux/uaccess.h>

/*
 * Copy a null terminated string from userspace.
 */

#ifdef CONFIG_ISA_DUAL_ISSUE

#define __do_strncpy_from_user(dst,src,count,res)			\
do {									\
	int __d0, __d1, __d2;						\
	__asm__ __volatile__(						\
		"	beqz	%1, 2f\n"				\
		"	.fillinsn\n"					\
		"0:	ldb	r14, @%3    ||	addi	%3, #1\n"	\
		"	stb	r14, @%4    ||	addi	%4, #1\n"	\
		"	beqz	r14, 1f\n"				\
		"	addi	%1, #-1\n"				\
		"	bnez	%1, 0b\n"				\
		"	.fillinsn\n"					\
		"1:	sub	%0, %1\n"				\
		"	.fillinsn\n"					\
		"2:\n"							\
		".section .fixup,\"ax\"\n"				\
		"	.balign 4\n"					\
		"3:	seth	r14, #high(2b)\n"			\
		"	or3	r14, r14, #low(2b)\n"			\
		"	jmp	r14	    ||	ldi	%0, #%5\n"	\
		".previous\n"						\
		".section __ex_table,\"a\"\n"				\
		"	.balign 4\n"					\
		"	.long 0b,3b\n"					\
		".previous"						\
		: "=&r"(res), "=&r"(count), "=&r" (__d0), "=&r" (__d1),	\
		  "=&r" (__d2)						\
		: "i"(-EFAULT), "0"(count), "1"(count), "3"(src), 	\
		  "4"(dst)						\
		: "r14", "cbit", "memory");				\
} while (0)

#else /* not CONFIG_ISA_DUAL_ISSUE */

#define __do_strncpy_from_user(dst,src,count,res)			\
do {									\
	int __d0, __d1, __d2;						\
	__asm__ __volatile__(						\
		"	beqz	%1, 2f\n"				\
		"	.fillinsn\n"					\
		"0:	ldb	r14, @%3\n"				\
		"	stb	r14, @%4\n"				\
		"	addi	%3, #1\n"				\
		"	addi	%4, #1\n"				\
		"	beqz	r14, 1f\n"				\
		"	addi	%1, #-1\n"				\
		"	bnez	%1, 0b\n"				\
		"	.fillinsn\n"					\
		"1:	sub	%0, %1\n"				\
		"	.fillinsn\n"					\
		"2:\n"							\
		".section .fixup,\"ax\"\n"				\
		"	.balign 4\n"					\
		"3:	ldi	%0, #%5\n"				\
		"	seth	r14, #high(2b)\n"			\
		"	or3	r14, r14, #low(2b)\n"			\
		"	jmp	r14\n"					\
		".previous\n"						\
		".section __ex_table,\"a\"\n"				\
		"	.balign 4\n"					\
		"	.long 0b,3b\n"					\
		".previous"						\
		: "=&r"(res), "=&r"(count), "=&r" (__d0), "=&r" (__d1),	\
		  "=&r" (__d2)						\
		: "i"(-EFAULT), "0"(count), "1"(count), "3"(src),	\
		  "4"(dst)						\
		: "r14", "cbit", "memory");				\
} while (0)

#endif /* CONFIG_ISA_DUAL_ISSUE */

long
__strncpy_from_user(char *dst, const char __user *src, long count)
{
	long res;
	__do_strncpy_from_user(dst, src, count, res);
	return res;
}

long
strncpy_from_user(char *dst, const char __user *src, long count)
{
	long res = -EFAULT;
	if (access_ok(VERIFY_READ, src, 1))
		__do_strncpy_from_user(dst, src, count, res);
	return res;
}


/*
 * Zero Userspace
 */

#ifdef CONFIG_ISA_DUAL_ISSUE

#define __do_clear_user(addr,size)					\
do {									\
	int __dst, __c;							\
  	__asm__ __volatile__(						\
		"	beqz	%1, 9f\n"				\
		"	and3	r14, %0, #3\n"				\
		"	bnez	r14, 2f\n"				\
		"	and3	r14, %1, #3\n"				\
		"	bnez	r14, 2f\n"				\
		"	and3	%1, %1, #3\n"				\
		"	beqz	%2, 2f\n"				\
		"	addi	%0, #-4\n"				\
		"	.fillinsn\n"					\
		"0:	; word clear \n"				\
		"	st	%6, @+%0    ||	addi	%2, #-1\n"	\
		"	bnez	%2, 0b\n"				\
		"	beqz	%1, 9f\n"				\
		"	.fillinsn\n"					\
		"2:	; byte clear \n"				\
		"	stb	%6, @%0	    ||	addi	%1, #-1\n"	\
		"	addi	%0, #1\n"				\
		"	bnez	%1, 2b\n"				\
		"	.fillinsn\n"					\
		"9:\n"							\
		".section .fixup,\"ax\"\n"				\
		"	.balign 4\n"					\
		"4:	slli	%2, #2\n"				\
		"	seth	r14, #high(9b)\n"			\
		"	or3	r14, r14, #low(9b)\n"			\
		"	jmp	r14	    ||	add	%1, %2\n"	\
		".previous\n"						\
		".section __ex_table,\"a\"\n"				\
		"	.balign 4\n"					\
		"	.long 0b,4b\n"					\
		"	.long 2b,9b\n"					\
		".previous\n"						\
		: "=&r"(__dst), "=&r"(size), "=&r"(__c)			\
		: "0"(addr), "1"(size), "2"(size / 4), "r"(0)		\
		: "r14", "cbit", "memory");				\
} while (0)

#else /* not CONFIG_ISA_DUAL_ISSUE */

#define __do_clear_user(addr,size)					\
do {									\
	int __dst, __c;							\
  	__asm__ __volatile__(						\
		"	beqz	%1, 9f\n"				\
		"	and3	r14, %0, #3\n"				\
		"	bnez	r14, 2f\n"				\
		"	and3	r14, %1, #3\n"				\
		"	bnez	r14, 2f\n"				\
		"	and3	%1, %1, #3\n"				\
		"	beqz	%2, 2f\n"				\
		"	addi	%0, #-4\n"				\
		"	.fillinsn\n"					\
		"0:	st	%6, @+%0	; word clear \n"	\
		"	addi	%2, #-1\n"				\
		"	bnez	%2, 0b\n"				\
		"	beqz	%1, 9f\n"				\
		"	.fillinsn\n"					\
		"2:	stb	%6, @%0		; byte clear \n"	\
		"	addi	%1, #-1\n"				\
		"	addi	%0, #1\n"				\
		"	bnez	%1, 2b\n"				\
		"	.fillinsn\n"					\
		"9:\n"							\
		".section .fixup,\"ax\"\n"				\
		"	.balign 4\n"					\
		"4:	slli	%2, #2\n"				\
		"	add	%1, %2\n"				\
		"	seth	r14, #high(9b)\n"			\
		"	or3	r14, r14, #low(9b)\n"			\
		"	jmp	r14\n"					\
		".previous\n"						\
		".section __ex_table,\"a\"\n"				\
		"	.balign 4\n"					\
		"	.long 0b,4b\n"					\
		"	.long 2b,9b\n"					\
		".previous\n"						\
		: "=&r"(__dst), "=&r"(size), "=&r"(__c)			\
		: "0"(addr), "1"(size), "2"(size / 4), "r"(0)		\
		: "r14", "cbit", "memory");				\
} while (0)

#endif /* not CONFIG_ISA_DUAL_ISSUE */

unsigned long
clear_user(void __user *to, unsigned long n)
{
	if (access_ok(VERIFY_WRITE, to, n))
		__do_clear_user(to, n);
	return n;
}

unsigned long
__clear_user(void __user *to, unsigned long n)
{
	__do_clear_user(to, n);
	return n;
}

/*
 * Return the size of a string (including the ending 0)
 *
 * Return 0 on exception, a value greater than N if too long
 */

#ifdef CONFIG_ISA_DUAL_ISSUE

long strnlen_user(const char __user *s, long n)
{
	unsigned long mask = -__addr_ok(s);
	unsigned long res;

	__asm__ __volatile__(
		"	and	%0, %5	    ||	mv	r1, %1\n"
		"	beqz	%0, strnlen_exit\n"
		"	and3	r0, %1, #3\n"
		"	bnez	r0, strnlen_byte_loop\n"
		"	cmpui	%0, #4\n"
		"	bc	strnlen_byte_loop\n"
		"strnlen_word_loop:\n"
		"0:	ld	r0, @%1+\n"
		"	pcmpbz	r0\n"
		"	bc	strnlen_last_bytes_fixup\n"
		"	addi	%0, #-4\n"
		"	beqz	%0, strnlen_exit\n"
		"	bgtz	%0, strnlen_word_loop\n"
		"strnlen_last_bytes:\n"
		"	mv	%0, %4\n"
		"strnlen_last_bytes_fixup:\n"
		"	addi	%1, #-4\n"
		"strnlen_byte_loop:\n"
		"1:	ldb	r0, @%1	    ||	addi	%0, #-1\n"
		"	beqz	r0, strnlen_exit\n"
		"	addi	%1, #1\n"
		"	bnez	%0, strnlen_byte_loop\n"
		"strnlen_exit:\n"
		"	sub	%1, r1\n"
		"	add3	%0, %1, #1\n"
		"	.fillinsn\n"
		"9:\n"
		".section .fixup,\"ax\"\n"
		"	.balign 4\n"
		"4:	addi	%1, #-4\n"
		"	.fillinsn\n"
		"5:	seth	r1, #high(9b)\n"
		"	or3	r1, r1, #low(9b)\n"
		"	jmp	r1	    ||	ldi	%0, #0\n"
		".previous\n"
		".section __ex_table,\"a\"\n"
		"	.balign 4\n"
		"	.long 0b,4b\n"
		"	.long 1b,5b\n"
		".previous"
		: "=&r" (res), "=r" (s)
		: "0" (n), "1" (s), "r" (n & 3), "r" (mask), "r"(0x01010101)
		: "r0", "r1", "cbit");

	/* NOTE: strnlen_user() algorithm:
	 * {
	 *   char *p;
	 *   for (p = s; n-- && *p != '\0'; ++p)
	 *     ;
	 *   return p - s + 1;
	 * }
	 */

	/* NOTE: If a null char. exists, return 0.
	 * if ((x - 0x01010101) & ~x & 0x80808080)\n"
	 *   return 0;\n"
	 */

	return res & mask;
}

#else /* not CONFIG_ISA_DUAL_ISSUE */

long strnlen_user(const char __user *s, long n)
{
	unsigned long mask = -__addr_ok(s);
	unsigned long res;

	__asm__ __volatile__(
		"	and	%0, %5\n"
		"	mv	r1, %1\n"
		"	beqz	%0, strnlen_exit\n"
		"	and3	r0, %1, #3\n"
		"	bnez	r0, strnlen_byte_loop\n"
		"	cmpui	%0, #4\n"
		"	bc	strnlen_byte_loop\n"
		"	sll3	r3, %6, #7\n"
		"strnlen_word_loop:\n"
		"0:	ld	r0, @%1+\n"
		"	not	r2, r0\n"
		"	sub	r0, %6\n"
		"	and	r2, r3\n"
		"	and	r2, r0\n"
		"	bnez	r2, strnlen_last_bytes_fixup\n"
		"	addi	%0, #-4\n"
		"	beqz	%0, strnlen_exit\n"
		"	bgtz	%0, strnlen_word_loop\n"
		"strnlen_last_bytes:\n"
		"	mv	%0, %4\n"
		"strnlen_last_bytes_fixup:\n"
		"	addi	%1, #-4\n"
		"strnlen_byte_loop:\n"
		"1:	ldb	r0, @%1\n"
		"	addi	%0, #-1\n"
		"	beqz	r0, strnlen_exit\n"
		"	addi	%1, #1\n"
		"	bnez	%0, strnlen_byte_loop\n"
		"strnlen_exit:\n"
		"	sub	%1, r1\n"
		"	add3	%0, %1, #1\n"
		"	.fillinsn\n"
		"9:\n"
		".section .fixup,\"ax\"\n"
		"	.balign 4\n"
		"4:	addi	%1, #-4\n"
		"	.fillinsn\n"
		"5:	ldi	%0, #0\n"
		"	seth	r1, #high(9b)\n"
		"	or3	r1, r1, #low(9b)\n"
		"	jmp	r1\n"
		".previous\n"
		".section __ex_table,\"a\"\n"
		"	.balign 4\n"
		"	.long 0b,4b\n"
		"	.long 1b,5b\n"
		".previous"
		: "=&r" (res), "=r" (s)
		: "0" (n), "1" (s), "r" (n & 3), "r" (mask), "r"(0x01010101)
		: "r0", "r1", "r2", "r3", "cbit");

	/* NOTE: strnlen_user() algorithm:
	 * {
	 *   char *p;
	 *   for (p = s; n-- && *p != '\0'; ++p)
	 *     ;
	 *   return p - s + 1;
	 * }
	 */

	/* NOTE: If a null char. exists, return 0.
	 * if ((x - 0x01010101) & ~x & 0x80808080)\n"
	 *   return 0;\n"
	 */

	return res & mask;
}

#endif /* CONFIG_ISA_DUAL_ISSUE */

