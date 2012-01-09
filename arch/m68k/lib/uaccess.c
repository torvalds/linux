/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/module.h>
#include <asm/uaccess.h>

unsigned long __generic_copy_from_user(void *to, const void __user *from,
				       unsigned long n)
{
	unsigned long tmp, res;

	asm volatile ("\n"
		"	tst.l	%0\n"
		"	jeq	2f\n"
		"1:	"MOVES".l	(%1)+,%3\n"
		"	move.l	%3,(%2)+\n"
		"	subq.l	#1,%0\n"
		"	jne	1b\n"
		"2:	btst	#1,%5\n"
		"	jeq	4f\n"
		"3:	"MOVES".w	(%1)+,%3\n"
		"	move.w	%3,(%2)+\n"
		"4:	btst	#0,%5\n"
		"	jeq	6f\n"
		"5:	"MOVES".b	(%1)+,%3\n"
		"	move.b  %3,(%2)+\n"
		"6:\n"
		"	.section .fixup,\"ax\"\n"
		"	.even\n"
		"10:	move.l	%0,%3\n"
		"7:	clr.l	(%2)+\n"
		"	subq.l	#1,%3\n"
		"	jne	7b\n"
		"	lsl.l	#2,%0\n"
		"	btst	#1,%5\n"
		"	jeq	8f\n"
		"30:	clr.w	(%2)+\n"
		"	addq.l	#2,%0\n"
		"8:	btst	#0,%5\n"
		"	jeq	6b\n"
		"50:	clr.b	(%2)+\n"
		"	addq.l	#1,%0\n"
		"	jra	6b\n"
		"	.previous\n"
		"\n"
		"	.section __ex_table,\"a\"\n"
		"	.align	4\n"
		"	.long	1b,10b\n"
		"	.long	3b,30b\n"
		"	.long	5b,50b\n"
		"	.previous"
		: "=d" (res), "+a" (from), "+a" (to), "=&r" (tmp)
		: "0" (n / 4), "d" (n & 3));

	return res;
}
EXPORT_SYMBOL(__generic_copy_from_user);

unsigned long __generic_copy_to_user(void __user *to, const void *from,
				     unsigned long n)
{
	unsigned long tmp, res;

	asm volatile ("\n"
		"	tst.l	%0\n"
		"	jeq	4f\n"
		"1:	move.l	(%1)+,%3\n"
		"2:	"MOVES".l	%3,(%2)+\n"
		"3:	subq.l	#1,%0\n"
		"	jne	1b\n"
		"4:	btst	#1,%5\n"
		"	jeq	6f\n"
		"	move.w	(%1)+,%3\n"
		"5:	"MOVES".w	%3,(%2)+\n"
		"6:	btst	#0,%5\n"
		"	jeq	8f\n"
		"	move.b	(%1)+,%3\n"
		"7:	"MOVES".b  %3,(%2)+\n"
		"8:\n"
		"	.section .fixup,\"ax\"\n"
		"	.even\n"
		"20:	lsl.l	#2,%0\n"
		"50:	add.l	%5,%0\n"
		"	jra	8b\n"
		"	.previous\n"
		"\n"
		"	.section __ex_table,\"a\"\n"
		"	.align	4\n"
		"	.long	2b,20b\n"
		"	.long	3b,20b\n"
		"	.long	5b,50b\n"
		"	.long	6b,50b\n"
		"	.long	7b,50b\n"
		"	.long	8b,50b\n"
		"	.previous"
		: "=d" (res), "+a" (from), "+a" (to), "=&r" (tmp)
		: "0" (n / 4), "d" (n & 3));

	return res;
}
EXPORT_SYMBOL(__generic_copy_to_user);

/*
 * Copy a null terminated string from userspace.
 */
long strncpy_from_user(char *dst, const char __user *src, long count)
{
	long res;
	char c;

	if (count <= 0)
		return count;

	asm volatile ("\n"
		"1:	"MOVES".b	(%2)+,%4\n"
		"	move.b	%4,(%1)+\n"
		"	jeq	2f\n"
		"	subq.l	#1,%3\n"
		"	jne	1b\n"
		"2:	sub.l	%3,%0\n"
		"3:\n"
		"	.section .fixup,\"ax\"\n"
		"	.even\n"
		"10:	move.l	%5,%0\n"
		"	jra	3b\n"
		"	.previous\n"
		"\n"
		"	.section __ex_table,\"a\"\n"
		"	.align	4\n"
		"	.long	1b,10b\n"
		"	.previous"
		: "=d" (res), "+a" (dst), "+a" (src), "+r" (count), "=&d" (c)
		: "i" (-EFAULT), "0" (count));

	return res;
}
EXPORT_SYMBOL(strncpy_from_user);

/*
 * Return the size of a string (including the ending 0)
 *
 * Return 0 on exception, a value greater than N if too long
 */
long strnlen_user(const char __user *src, long n)
{
	char c;
	long res;

	asm volatile ("\n"
		"1:	subq.l	#1,%1\n"
		"	jmi	3f\n"
		"2:	"MOVES".b	(%0)+,%2\n"
		"	tst.b	%2\n"
		"	jne	1b\n"
		"	jra	4f\n"
		"\n"
		"3:	addq.l	#1,%0\n"
		"4:	sub.l	%4,%0\n"
		"5:\n"
		"	.section .fixup,\"ax\"\n"
		"	.even\n"
		"20:	sub.l	%0,%0\n"
		"	jra	5b\n"
		"	.previous\n"
		"\n"
		"	.section __ex_table,\"a\"\n"
		"	.align	4\n"
		"	.long	2b,20b\n"
		"	.previous\n"
		: "=&a" (res), "+d" (n), "=&d" (c)
		: "0" (src), "r" (src));

	return res;
}
EXPORT_SYMBOL(strnlen_user);

/*
 * Zero Userspace
 */

unsigned long __clear_user(void __user *to, unsigned long n)
{
	unsigned long res;

	asm volatile ("\n"
		"	tst.l	%0\n"
		"	jeq	3f\n"
		"1:	"MOVES".l	%2,(%1)+\n"
		"2:	subq.l	#1,%0\n"
		"	jne	1b\n"
		"3:	btst	#1,%4\n"
		"	jeq	5f\n"
		"4:	"MOVES".w	%2,(%1)+\n"
		"5:	btst	#0,%4\n"
		"	jeq	7f\n"
		"6:	"MOVES".b	%2,(%1)\n"
		"7:\n"
		"	.section .fixup,\"ax\"\n"
		"	.even\n"
		"10:	lsl.l	#2,%0\n"
		"40:	add.l	%4,%0\n"
		"	jra	7b\n"
		"	.previous\n"
		"\n"
		"	.section __ex_table,\"a\"\n"
		"	.align	4\n"
		"	.long	1b,10b\n"
		"	.long	2b,10b\n"
		"	.long	4b,40b\n"
		"	.long	5b,40b\n"
		"	.long	6b,40b\n"
		"	.long	7b,40b\n"
		"	.previous"
		: "=d" (res), "+a" (to)
		: "r" (0), "0" (n / 4), "d" (n & 3));

    return res;
}
EXPORT_SYMBOL(__clear_user);
