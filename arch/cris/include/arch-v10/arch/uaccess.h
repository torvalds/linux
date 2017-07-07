/* 
 * Authors:    Bjorn Wesen (bjornw@axis.com)
 *	       Hans-Peter Nilsson (hp@axis.com)
 *
 */
#ifndef _CRIS_ARCH_UACCESS_H
#define _CRIS_ARCH_UACCESS_H

/*
 * We don't tell gcc that we are accessing memory, but this is OK
 * because we do not write to any memory gcc knows about, so there
 * are no aliasing issues.
 *
 * Note that PC at a fault is the address *after* the faulting
 * instruction.
 */
#define __put_user_asm(x, addr, err, op)			\
	__asm__ __volatile__(					\
		"	"op" %1,[%2]\n"				\
		"2:\n"						\
		"	.section .fixup,\"ax\"\n"		\
		"3:	move.d %3,%0\n"				\
		"	jump 2b\n"				\
		"	.previous\n"				\
		"	.section __ex_table,\"a\"\n"		\
		"	.dword 2b,3b\n"				\
		"	.previous\n"				\
		: "=r" (err)					\
		: "r" (x), "r" (addr), "g" (-EFAULT), "0" (err))

#define __put_user_asm_64(x, addr, err)				\
	__asm__ __volatile__(					\
		"	move.d %M1,[%2]\n"			\
		"2:	move.d %H1,[%2+4]\n"			\
		"4:\n"						\
		"	.section .fixup,\"ax\"\n"		\
		"3:	move.d %3,%0\n"				\
		"	jump 4b\n"				\
		"	.previous\n"				\
		"	.section __ex_table,\"a\"\n"		\
		"	.dword 2b,3b\n"				\
		"	.dword 4b,3b\n"				\
		"	.previous\n"				\
		: "=r" (err)					\
		: "r" (x), "r" (addr), "g" (-EFAULT), "0" (err))

/* See comment before __put_user_asm.  */

#define __get_user_asm(x, addr, err, op)		\
	__asm__ __volatile__(				\
		"	"op" [%2],%1\n"			\
		"2:\n"					\
		"	.section .fixup,\"ax\"\n"	\
		"3:	move.d %3,%0\n"			\
		"	moveq 0,%1\n"			\
		"	jump 2b\n"			\
		"	.previous\n"			\
		"	.section __ex_table,\"a\"\n"	\
		"	.dword 2b,3b\n"			\
		"	.previous\n"			\
		: "=r" (err), "=r" (x)			\
		: "r" (addr), "g" (-EFAULT), "0" (err))

#define __get_user_asm_64(x, addr, err)			\
	__asm__ __volatile__(				\
		"	move.d [%2],%M1\n"		\
		"2:	move.d [%2+4],%H1\n"		\
		"4:\n"					\
		"	.section .fixup,\"ax\"\n"	\
		"3:	move.d %3,%0\n"			\
		"	moveq 0,%1\n"			\
		"	jump 4b\n"			\
		"	.previous\n"			\
		"	.section __ex_table,\"a\"\n"	\
		"	.dword 2b,3b\n"			\
		"	.dword 4b,3b\n"			\
		"	.previous\n"			\
		: "=r" (err), "=r" (x)			\
		: "r" (addr), "g" (-EFAULT), "0" (err))

/*
 * Copy a null terminated string from userspace.
 *
 * Must return:
 * -EFAULT		for an exception
 * count		if we hit the buffer limit
 * bytes copied		if we hit a null byte
 * (without the null byte)
 */
static inline long
__do_strncpy_from_user(char *dst, const char *src, long count)
{
	long res;

	if (count == 0)
		return 0;

	/*
	 * Currently, in 2.4.0-test9, most ports use a simple byte-copy loop.
	 *  So do we.
	 *
	 *  This code is deduced from:
	 *
	 *	char tmp2;
	 *	long tmp1, tmp3	
	 *	tmp1 = count;
	 *	while ((*dst++ = (tmp2 = *src++)) != 0
	 *	       && --tmp1)
	 *	  ;
	 *
	 *	res = count - tmp1;
	 *
	 *  with tweaks.
	 */

	__asm__ __volatile__ (
		"	move.d %3,%0\n"
		"	move.b [%2+],$r9\n"
		"1:	beq 2f\n"
		"	move.b $r9,[%1+]\n"

		"	subq 1,%0\n"
		"	bne 1b\n"
		"	move.b [%2+],$r9\n"

		"2:	sub.d %3,%0\n"
		"	neg.d %0,%0\n"
		"3:\n"
		"	.section .fixup,\"ax\"\n"
		"4:	move.d %7,%0\n"
		"	jump 3b\n"

		/* There's one address for a fault at the first move, and
		   two possible PC values for a fault at the second move,
		   being a delay-slot filler.  However, the branch-target
		   for the second move is the same as the first address.
		   Just so you don't get confused...  */
		"	.previous\n"
		"	.section __ex_table,\"a\"\n"
		"	.dword 1b,4b\n"
		"	.dword 2b,4b\n"
		"	.previous"
		: "=r" (res), "=r" (dst), "=r" (src), "=r" (count)
		: "3" (count), "1" (dst), "2" (src), "g" (-EFAULT)
		: "r9");

	return res;
}

/* A few copy asms to build up the more complex ones from.

   Note again, a post-increment is performed regardless of whether a bus
   fault occurred in that instruction, and PC for a faulted insn is the
   address *after* the insn.  */

#define __asm_copy_user_cont(to, from, ret, COPY, FIXUP, TENTRY) \
	__asm__ __volatile__ (				\
			COPY				\
		"1:\n"					\
		"	.section .fixup,\"ax\"\n"	\
			FIXUP				\
		"	jump 1b\n"			\
		"	.previous\n"			\
		"	.section __ex_table,\"a\"\n"	\
			TENTRY				\
		"	.previous\n"			\
		: "=r" (to), "=r" (from), "=r" (ret)	\
		: "0" (to), "1" (from), "2" (ret)	\
		: "r9", "memory")

#define __asm_copy_from_user_1(to, from, ret) \
	__asm_copy_user_cont(to, from, ret,	\
		"	move.b [%1+],$r9\n"	\
		"2:	move.b $r9,[%0+]\n",	\
		"3:	addq 1,%2\n",		\
		"	.dword 2b,3b\n")

#define __asm_copy_from_user_2x_cont(to, from, ret, COPY, FIXUP, TENTRY) \
	__asm_copy_user_cont(to, from, ret,		\
		"	move.w [%1+],$r9\n"		\
		"2:	move.w $r9,[%0+]\n" COPY,	\
		"3:	addq 2,%2\n" FIXUP,		\
		"	.dword 2b,3b\n" TENTRY)

#define __asm_copy_from_user_2(to, from, ret) \
	__asm_copy_from_user_2x_cont(to, from, ret, "", "", "")

#define __asm_copy_from_user_3(to, from, ret)		\
	__asm_copy_from_user_2x_cont(to, from, ret,	\
		"	move.b [%1+],$r9\n"		\
		"4:	move.b $r9,[%0+]\n",		\
		"5:	addq 1,%2\n",			\
		"	.dword 4b,5b\n")

#define __asm_copy_from_user_4x_cont(to, from, ret, COPY, FIXUP, TENTRY) \
	__asm_copy_user_cont(to, from, ret,		\
		"	move.d [%1+],$r9\n"		\
		"2:	move.d $r9,[%0+]\n" COPY,	\
		"3:	addq 4,%2\n" FIXUP,		\
		"	.dword 2b,3b\n" TENTRY)

#define __asm_copy_from_user_4(to, from, ret) \
	__asm_copy_from_user_4x_cont(to, from, ret, "", "", "")

#define __asm_copy_from_user_5(to, from, ret) \
	__asm_copy_from_user_4x_cont(to, from, ret,	\
		"	move.b [%1+],$r9\n"		\
		"4:	move.b $r9,[%0+]\n",		\
		"5:	addq 1,%2\n",			\
		"	.dword 4b,5b\n")

#define __asm_copy_from_user_6x_cont(to, from, ret, COPY, FIXUP, TENTRY) \
	__asm_copy_from_user_4x_cont(to, from, ret,	\
		"	move.w [%1+],$r9\n"		\
		"4:	move.w $r9,[%0+]\n" COPY,	\
		"5:	addq 2,%2\n"			\
			FIXUP,				\
		"	.dword 4b,5b\n" TENTRY)

#define __asm_copy_from_user_6(to, from, ret) \
	__asm_copy_from_user_6x_cont(to, from, ret, "", "", "")

#define __asm_copy_from_user_7(to, from, ret) \
	__asm_copy_from_user_6x_cont(to, from, ret,	\
		"	move.b [%1+],$r9\n"		\
		"6:	move.b $r9,[%0+]\n",		\
		"7:	addq 1,%2\n",			\
		"	.dword 6b,7b\n")

#define __asm_copy_from_user_8x_cont(to, from, ret, COPY, FIXUP, TENTRY) \
	__asm_copy_from_user_4x_cont(to, from, ret,	\
		"	move.d [%1+],$r9\n"		\
		"4:	move.d $r9,[%0+]\n" COPY,	\
		"5:	addq 4,%2\n"			\
			FIXUP,				\
		"	.dword 4b,5b\n" TENTRY)

#define __asm_copy_from_user_8(to, from, ret) \
	__asm_copy_from_user_8x_cont(to, from, ret, "", "", "")

#define __asm_copy_from_user_9(to, from, ret) \
	__asm_copy_from_user_8x_cont(to, from, ret,	\
		"	move.b [%1+],$r9\n"		\
		"6:	move.b $r9,[%0+]\n",		\
		"7:	addq 1,%2\n",			\
		"	.dword 6b,7b\n")

#define __asm_copy_from_user_10x_cont(to, from, ret, COPY, FIXUP, TENTRY) \
	__asm_copy_from_user_8x_cont(to, from, ret,	\
		"	move.w [%1+],$r9\n"		\
		"6:	move.w $r9,[%0+]\n" COPY,	\
		"7:	addq 2,%2\n"			\
			FIXUP,				\
		"	.dword 6b,7b\n" TENTRY)

#define __asm_copy_from_user_10(to, from, ret) \
	__asm_copy_from_user_10x_cont(to, from, ret, "", "", "")

#define __asm_copy_from_user_11(to, from, ret)		\
	__asm_copy_from_user_10x_cont(to, from, ret,	\
		"	move.b [%1+],$r9\n"		\
		"8:	move.b $r9,[%0+]\n",		\
		"9:	addq 1,%2\n",			\
		"	.dword 8b,9b\n")

#define __asm_copy_from_user_12x_cont(to, from, ret, COPY, FIXUP, TENTRY) \
	__asm_copy_from_user_8x_cont(to, from, ret,	\
		"	move.d [%1+],$r9\n"		\
		"6:	move.d $r9,[%0+]\n" COPY,	\
		"7:	addq 4,%2\n"			\
			FIXUP,				\
		"	.dword 6b,7b\n" TENTRY)

#define __asm_copy_from_user_12(to, from, ret) \
	__asm_copy_from_user_12x_cont(to, from, ret, "", "", "")

#define __asm_copy_from_user_13(to, from, ret) \
	__asm_copy_from_user_12x_cont(to, from, ret,	\
		"	move.b [%1+],$r9\n"		\
		"8:	move.b $r9,[%0+]\n",		\
		"9:	addq 1,%2\n",			\
		"	.dword 8b,9b\n")

#define __asm_copy_from_user_14x_cont(to, from, ret, COPY, FIXUP, TENTRY) \
	__asm_copy_from_user_12x_cont(to, from, ret,	\
		"	move.w [%1+],$r9\n"		\
		"8:	move.w $r9,[%0+]\n" COPY,	\
		"9:	addq 2,%2\n"			\
			FIXUP,				\
		"	.dword 8b,9b\n" TENTRY)

#define __asm_copy_from_user_14(to, from, ret) \
	__asm_copy_from_user_14x_cont(to, from, ret, "", "", "")

#define __asm_copy_from_user_15(to, from, ret) \
	__asm_copy_from_user_14x_cont(to, from, ret,	\
		"	move.b [%1+],$r9\n"		\
		"10:	move.b $r9,[%0+]\n",		\
		"11:	addq 1,%2\n",			\
		"	.dword 10b,11b\n")

#define __asm_copy_from_user_16x_cont(to, from, ret, COPY, FIXUP, TENTRY) \
	__asm_copy_from_user_12x_cont(to, from, ret,	\
		"	move.d [%1+],$r9\n"		\
		"8:	move.d $r9,[%0+]\n" COPY,	\
		"9:	addq 4,%2\n"			\
			FIXUP,				\
		"	.dword 8b,9b\n" TENTRY)

#define __asm_copy_from_user_16(to, from, ret) \
	__asm_copy_from_user_16x_cont(to, from, ret, "", "", "")

#define __asm_copy_from_user_20x_cont(to, from, ret, COPY, FIXUP, TENTRY) \
	__asm_copy_from_user_16x_cont(to, from, ret,	\
		"	move.d [%1+],$r9\n"		\
		"10:	move.d $r9,[%0+]\n" COPY,	\
		"11:	addq 4,%2\n"			\
			FIXUP,				\
		"	.dword 10b,11b\n" TENTRY)

#define __asm_copy_from_user_20(to, from, ret) \
	__asm_copy_from_user_20x_cont(to, from, ret, "", "", "")

#define __asm_copy_from_user_24x_cont(to, from, ret, COPY, FIXUP, TENTRY) \
	__asm_copy_from_user_20x_cont(to, from, ret,	\
		"	move.d [%1+],$r9\n"		\
		"12:	move.d $r9,[%0+]\n" COPY,	\
		"13:	addq 4,%2\n"			\
			FIXUP,				\
		"	.dword 12b,13b\n" TENTRY)

#define __asm_copy_from_user_24(to, from, ret) \
	__asm_copy_from_user_24x_cont(to, from, ret, "", "", "")

/* And now, the to-user ones.  */

#define __asm_copy_to_user_1(to, from, ret)	\
	__asm_copy_user_cont(to, from, ret,	\
		"	move.b [%1+],$r9\n"	\
		"	move.b $r9,[%0+]\n2:\n",	\
		"3:	addq 1,%2\n",		\
		"	.dword 2b,3b\n")

#define __asm_copy_to_user_2x_cont(to, from, ret, COPY, FIXUP, TENTRY) \
	__asm_copy_user_cont(to, from, ret,		\
		"	move.w [%1+],$r9\n"		\
		"	move.w $r9,[%0+]\n2:\n" COPY,	\
		"3:	addq 2,%2\n" FIXUP,		\
		"	.dword 2b,3b\n" TENTRY)

#define __asm_copy_to_user_2(to, from, ret) \
	__asm_copy_to_user_2x_cont(to, from, ret, "", "", "")

#define __asm_copy_to_user_3(to, from, ret) \
	__asm_copy_to_user_2x_cont(to, from, ret,	\
		"	move.b [%1+],$r9\n"		\
		"	move.b $r9,[%0+]\n4:\n",		\
		"5:	addq 1,%2\n",			\
		"	.dword 4b,5b\n")

#define __asm_copy_to_user_4x_cont(to, from, ret, COPY, FIXUP, TENTRY) \
	__asm_copy_user_cont(to, from, ret,		\
		"	move.d [%1+],$r9\n"		\
		"	move.d $r9,[%0+]\n2:\n" COPY,	\
		"3:	addq 4,%2\n" FIXUP,		\
		"	.dword 2b,3b\n" TENTRY)

#define __asm_copy_to_user_4(to, from, ret) \
	__asm_copy_to_user_4x_cont(to, from, ret, "", "", "")

#define __asm_copy_to_user_5(to, from, ret) \
	__asm_copy_to_user_4x_cont(to, from, ret,	\
		"	move.b [%1+],$r9\n"		\
		"	move.b $r9,[%0+]\n4:\n",		\
		"5:	addq 1,%2\n",			\
		"	.dword 4b,5b\n")

#define __asm_copy_to_user_6x_cont(to, from, ret, COPY, FIXUP, TENTRY) \
	__asm_copy_to_user_4x_cont(to, from, ret,	\
		"	move.w [%1+],$r9\n"		\
		"	move.w $r9,[%0+]\n4:\n" COPY,	\
		"5:	addq 2,%2\n" FIXUP,		\
		"	.dword 4b,5b\n" TENTRY)

#define __asm_copy_to_user_6(to, from, ret) \
	__asm_copy_to_user_6x_cont(to, from, ret, "", "", "")

#define __asm_copy_to_user_7(to, from, ret) \
	__asm_copy_to_user_6x_cont(to, from, ret,	\
		"	move.b [%1+],$r9\n"		\
		"	move.b $r9,[%0+]\n6:\n",		\
		"7:	addq 1,%2\n",			\
		"	.dword 6b,7b\n")

#define __asm_copy_to_user_8x_cont(to, from, ret, COPY, FIXUP, TENTRY) \
	__asm_copy_to_user_4x_cont(to, from, ret,	\
		"	move.d [%1+],$r9\n"		\
		"	move.d $r9,[%0+]\n4:\n" COPY,	\
		"5:	addq 4,%2\n"  FIXUP,		\
		"	.dword 4b,5b\n" TENTRY)

#define __asm_copy_to_user_8(to, from, ret) \
	__asm_copy_to_user_8x_cont(to, from, ret, "", "", "")

#define __asm_copy_to_user_9(to, from, ret) \
	__asm_copy_to_user_8x_cont(to, from, ret,	\
		"	move.b [%1+],$r9\n"		\
		"	move.b $r9,[%0+]\n6:\n",		\
		"7:	addq 1,%2\n",			\
		"	.dword 6b,7b\n")

#define __asm_copy_to_user_10x_cont(to, from, ret, COPY, FIXUP, TENTRY) \
	__asm_copy_to_user_8x_cont(to, from, ret,	\
		"	move.w [%1+],$r9\n"		\
		"	move.w $r9,[%0+]\n6:\n" COPY,	\
		"7:	addq 2,%2\n" FIXUP,		\
		"	.dword 6b,7b\n" TENTRY)

#define __asm_copy_to_user_10(to, from, ret) \
	__asm_copy_to_user_10x_cont(to, from, ret, "", "", "")

#define __asm_copy_to_user_11(to, from, ret) \
	__asm_copy_to_user_10x_cont(to, from, ret,	\
		"	move.b [%1+],$r9\n"		\
		"	move.b $r9,[%0+]\n8:\n",		\
		"9:	addq 1,%2\n",			\
		"	.dword 8b,9b\n")

#define __asm_copy_to_user_12x_cont(to, from, ret, COPY, FIXUP, TENTRY) \
	__asm_copy_to_user_8x_cont(to, from, ret,	\
		"	move.d [%1+],$r9\n"		\
		"	move.d $r9,[%0+]\n6:\n" COPY,	\
		"7:	addq 4,%2\n" FIXUP,		\
		"	.dword 6b,7b\n" TENTRY)

#define __asm_copy_to_user_12(to, from, ret) \
	__asm_copy_to_user_12x_cont(to, from, ret, "", "", "")

#define __asm_copy_to_user_13(to, from, ret) \
	__asm_copy_to_user_12x_cont(to, from, ret,	\
		"	move.b [%1+],$r9\n"		\
		"	move.b $r9,[%0+]\n8:\n",		\
		"9:	addq 1,%2\n",			\
		"	.dword 8b,9b\n")

#define __asm_copy_to_user_14x_cont(to, from, ret, COPY, FIXUP, TENTRY) \
	__asm_copy_to_user_12x_cont(to, from, ret,	\
		"	move.w [%1+],$r9\n"		\
		"	move.w $r9,[%0+]\n8:\n" COPY,	\
		"9:	addq 2,%2\n" FIXUP,		\
		"	.dword 8b,9b\n" TENTRY)

#define __asm_copy_to_user_14(to, from, ret)	\
	__asm_copy_to_user_14x_cont(to, from, ret, "", "", "")

#define __asm_copy_to_user_15(to, from, ret) \
	__asm_copy_to_user_14x_cont(to, from, ret,	\
		"	move.b [%1+],$r9\n"		\
		"	move.b $r9,[%0+]\n10:\n",		\
		"11:	addq 1,%2\n",			\
		"	.dword 10b,11b\n")

#define __asm_copy_to_user_16x_cont(to, from, ret, COPY, FIXUP, TENTRY) \
	__asm_copy_to_user_12x_cont(to, from, ret,	\
		"	move.d [%1+],$r9\n"		\
		"	move.d $r9,[%0+]\n8:\n" COPY,	\
		"9:	addq 4,%2\n" FIXUP,		\
		"	.dword 8b,9b\n" TENTRY)

#define __asm_copy_to_user_16(to, from, ret) \
	__asm_copy_to_user_16x_cont(to, from, ret, "", "", "")

#define __asm_copy_to_user_20x_cont(to, from, ret, COPY, FIXUP, TENTRY) \
	__asm_copy_to_user_16x_cont(to, from, ret,	\
		"	move.d [%1+],$r9\n"		\
		"	move.d $r9,[%0+]\n10:\n" COPY,	\
		"11:	addq 4,%2\n" FIXUP,		\
		"	.dword 10b,11b\n" TENTRY)

#define __asm_copy_to_user_20(to, from, ret) \
	__asm_copy_to_user_20x_cont(to, from, ret, "", "", "")

#define __asm_copy_to_user_24x_cont(to, from, ret, COPY, FIXUP, TENTRY)	\
	__asm_copy_to_user_20x_cont(to, from, ret,	\
		"	move.d [%1+],$r9\n"		\
		"	move.d $r9,[%0+]\n12:\n" COPY,	\
		"13:	addq 4,%2\n" FIXUP,		\
		"	.dword 12b,13b\n" TENTRY)

#define __asm_copy_to_user_24(to, from, ret)	\
	__asm_copy_to_user_24x_cont(to, from, ret, "", "", "")

/* Define a few clearing asms with exception handlers.  */

/* This frame-asm is like the __asm_copy_user_cont one, but has one less
   input.  */

#define __asm_clear(to, ret, CLEAR, FIXUP, TENTRY) \
	__asm__ __volatile__ (				\
			CLEAR				\
		"1:\n"					\
		"	.section .fixup,\"ax\"\n"	\
			FIXUP				\
		"	jump 1b\n"			\
		"	.previous\n"			\
		"	.section __ex_table,\"a\"\n"	\
			TENTRY				\
		"	.previous"			\
		: "=r" (to), "=r" (ret)			\
		: "0" (to), "1" (ret)			\
		: "memory")

#define __asm_clear_1(to, ret) \
	__asm_clear(to, ret,			\
		"	clear.b [%0+]\n2:\n",	\
		"3:	addq 1,%1\n",		\
		"	.dword 2b,3b\n")

#define __asm_clear_2(to, ret) \
	__asm_clear(to, ret,			\
		"	clear.w [%0+]\n2:\n",	\
		"3:	addq 2,%1\n",		\
		"	.dword 2b,3b\n")

#define __asm_clear_3(to, ret) \
     __asm_clear(to, ret,			\
		 "	clear.w [%0+]\n"	\
		 "2:	clear.b [%0+]\n3:\n",	\
		 "4:	addq 2,%1\n"		\
		 "5:	addq 1,%1\n",		\
		 "	.dword 2b,4b\n"		\
		 "	.dword 3b,5b\n")

#define __asm_clear_4x_cont(to, ret, CLEAR, FIXUP, TENTRY) \
	__asm_clear(to, ret,				\
		"	clear.d [%0+]\n2:\n" CLEAR,	\
		"3:	addq 4,%1\n" FIXUP,		\
		"	.dword 2b,3b\n" TENTRY)

#define __asm_clear_4(to, ret) \
	__asm_clear_4x_cont(to, ret, "", "", "")

#define __asm_clear_8x_cont(to, ret, CLEAR, FIXUP, TENTRY) \
	__asm_clear_4x_cont(to, ret,			\
		"	clear.d [%0+]\n4:\n" CLEAR,	\
		"5:	addq 4,%1\n" FIXUP,		\
		"	.dword 4b,5b\n" TENTRY)

#define __asm_clear_8(to, ret) \
	__asm_clear_8x_cont(to, ret, "", "", "")

#define __asm_clear_12x_cont(to, ret, CLEAR, FIXUP, TENTRY) \
	__asm_clear_8x_cont(to, ret,			\
		"	clear.d [%0+]\n6:\n" CLEAR,	\
		"7:	addq 4,%1\n" FIXUP,		\
		"	.dword 6b,7b\n" TENTRY)

#define __asm_clear_12(to, ret) \
	__asm_clear_12x_cont(to, ret, "", "", "")

#define __asm_clear_16x_cont(to, ret, CLEAR, FIXUP, TENTRY) \
	__asm_clear_12x_cont(to, ret,			\
		"	clear.d [%0+]\n8:\n" CLEAR,	\
		"9:	addq 4,%1\n" FIXUP,		\
		"	.dword 8b,9b\n" TENTRY)

#define __asm_clear_16(to, ret) \
	__asm_clear_16x_cont(to, ret, "", "", "")

#define __asm_clear_20x_cont(to, ret, CLEAR, FIXUP, TENTRY) \
	__asm_clear_16x_cont(to, ret,			\
		"	clear.d [%0+]\n10:\n" CLEAR,	\
		"11:	addq 4,%1\n" FIXUP,		\
		"	.dword 10b,11b\n" TENTRY)

#define __asm_clear_20(to, ret) \
	__asm_clear_20x_cont(to, ret, "", "", "")

#define __asm_clear_24x_cont(to, ret, CLEAR, FIXUP, TENTRY) \
	__asm_clear_20x_cont(to, ret,			\
		"	clear.d [%0+]\n12:\n" CLEAR,	\
		"13:	addq 4,%1\n" FIXUP,		\
		"	.dword 12b,13b\n" TENTRY)

#define __asm_clear_24(to, ret) \
	__asm_clear_24x_cont(to, ret, "", "", "")

/*
 * Return the size of a string (including the ending 0)
 *
 * Return length of string in userspace including terminating 0
 * or 0 for error.  Return a value greater than N if too long.
 */

static inline long
strnlen_user(const char *s, long n)
{
	long res, tmp1;

	if (!access_ok(VERIFY_READ, s, 0))
		return 0;

	/*
	 * This code is deduced from:
	 *
	 *	tmp1 = n;
	 *	while (tmp1-- > 0 && *s++)
	 *	  ;
	 *
	 *	res = n - tmp1;
	 *
	 *  (with tweaks).
	 */

	__asm__ __volatile__ (
		"	move.d %1,$r9\n"
		"0:\n"
		"	ble 1f\n"
		"	subq 1,$r9\n"

		"	test.b [%0+]\n"
		"	bne 0b\n"
		"	test.d $r9\n"
		"1:\n"
		"	move.d %1,%0\n"
		"	sub.d $r9,%0\n"
		"2:\n"
		"	.section .fixup,\"ax\"\n"

		"3:	clear.d %0\n"
		"	jump 2b\n"

		/* There's one address for a fault at the first move, and
		   two possible PC values for a fault at the second move,
		   being a delay-slot filler.  However, the branch-target
		   for the second move is the same as the first address.
		   Just so you don't get confused...  */
		"	.previous\n"
		"	.section __ex_table,\"a\"\n"
		"	.dword 0b,3b\n"
		"	.dword 1b,3b\n"
		"	.previous\n"
		: "=r" (res), "=r" (tmp1)
		: "0" (s), "1" (n)
		: "r9");

	return res;
}

#endif
