/*
 * User address space access functions.
 * The non-inlined parts of asm-metag/uaccess.h are here.
 *
 * Copyright (C) 2006, Imagination Technologies.
 * Copyright (C) 2000, Axis Communications AB.
 *
 * Written by Hans-Peter Nilsson.
 * Pieces used from memcpy, originally by Kenny Ranerup long time ago.
 * Modified for Meta by Will Newton.
 */

#include <linux/export.h>
#include <linux/uaccess.h>
#include <asm/cache.h>			/* def of L1_CACHE_BYTES */

#define USE_RAPF
#define RAPF_MIN_BUF_SIZE	(3*L1_CACHE_BYTES)


/* The "double write" in this code is because the Meta will not fault
 * immediately unless the memory pipe is forced to by e.g. a data stall or
 * another memory op. The second write should be discarded by the write
 * combiner so should have virtually no cost.
 */

#define __asm_copy_user_cont(to, from, ret, COPY, FIXUP, TENTRY) \
	asm volatile (						 \
		COPY						 \
		"1:\n"						 \
		"	.section .fixup,\"ax\"\n"		 \
		"	MOV D1Ar1,#0\n"				 \
		FIXUP						 \
		"	MOVT    D1Ar1,#HI(1b)\n"		 \
		"	JUMP    D1Ar1,#LO(1b)\n"		 \
		"	.previous\n"				 \
		"	.section __ex_table,\"a\"\n"		 \
		TENTRY						 \
		"	.previous\n"				 \
		: "=r" (to), "=r" (from), "=r" (ret)		 \
		: "0" (to), "1" (from), "2" (ret)		 \
		: "D1Ar1", "memory")


#define __asm_copy_to_user_1(to, from, ret)	\
	__asm_copy_user_cont(to, from, ret,	\
		"	GETB D1Ar1,[%1++]\n"	\
		"	SETB [%0],D1Ar1\n"	\
		"2:	SETB [%0++],D1Ar1\n",	\
		"3:	ADD  %2,%2,#1\n",	\
		"	.long 2b,3b\n")

#define __asm_copy_to_user_2x_cont(to, from, ret, COPY, FIXUP, TENTRY) \
	__asm_copy_user_cont(to, from, ret,		\
		"	GETW D1Ar1,[%1++]\n"		\
		"	SETW [%0],D1Ar1\n"		\
		"2:	SETW [%0++],D1Ar1\n" COPY,	\
		"3:	ADD  %2,%2,#2\n" FIXUP,		\
		"	.long 2b,3b\n" TENTRY)

#define __asm_copy_to_user_2(to, from, ret) \
	__asm_copy_to_user_2x_cont(to, from, ret, "", "", "")

#define __asm_copy_to_user_3(to, from, ret) \
	__asm_copy_to_user_2x_cont(to, from, ret,	\
		"	GETB D1Ar1,[%1++]\n"		\
		"	SETB [%0],D1Ar1\n"		\
		"4:	SETB [%0++],D1Ar1\n",		\
		"5:	ADD  %2,%2,#1\n",		\
		"	.long 4b,5b\n")

#define __asm_copy_to_user_4x_cont(to, from, ret, COPY, FIXUP, TENTRY) \
	__asm_copy_user_cont(to, from, ret,		\
		"	GETD D1Ar1,[%1++]\n"		\
		"	SETD [%0],D1Ar1\n"		\
		"2:	SETD [%0++],D1Ar1\n" COPY,	\
		"3:	ADD  %2,%2,#4\n" FIXUP,		\
		"	.long 2b,3b\n" TENTRY)

#define __asm_copy_to_user_4(to, from, ret) \
	__asm_copy_to_user_4x_cont(to, from, ret, "", "", "")

#define __asm_copy_to_user_5(to, from, ret) \
	__asm_copy_to_user_4x_cont(to, from, ret,	\
		"	GETB D1Ar1,[%1++]\n"		\
		"	SETB [%0],D1Ar1\n"		\
		"4:	SETB [%0++],D1Ar1\n",		\
		"5:	ADD  %2,%2,#1\n",		\
		"	.long 4b,5b\n")

#define __asm_copy_to_user_6x_cont(to, from, ret, COPY, FIXUP, TENTRY) \
	__asm_copy_to_user_4x_cont(to, from, ret,	\
		"	GETW D1Ar1,[%1++]\n"		\
		"	SETW [%0],D1Ar1\n"		\
		"4:	SETW [%0++],D1Ar1\n" COPY,	\
		"5:	ADD  %2,%2,#2\n" FIXUP,		\
		"	.long 4b,5b\n" TENTRY)

#define __asm_copy_to_user_6(to, from, ret) \
	__asm_copy_to_user_6x_cont(to, from, ret, "", "", "")

#define __asm_copy_to_user_7(to, from, ret) \
	__asm_copy_to_user_6x_cont(to, from, ret,	\
		"	GETB D1Ar1,[%1++]\n"		\
		"	SETB [%0],D1Ar1\n"		\
		"6:	SETB [%0++],D1Ar1\n",		\
		"7:	ADD  %2,%2,#1\n",		\
		"	.long 6b,7b\n")

#define __asm_copy_to_user_8x_cont(to, from, ret, COPY, FIXUP, TENTRY) \
	__asm_copy_to_user_4x_cont(to, from, ret,	\
		"	GETD D1Ar1,[%1++]\n"		\
		"	SETD [%0],D1Ar1\n"		\
		"4:	SETD [%0++],D1Ar1\n" COPY,	\
		"5:	ADD  %2,%2,#4\n"  FIXUP,	\
		"	.long 4b,5b\n" TENTRY)

#define __asm_copy_to_user_8(to, from, ret) \
	__asm_copy_to_user_8x_cont(to, from, ret, "", "", "")

#define __asm_copy_to_user_9(to, from, ret) \
	__asm_copy_to_user_8x_cont(to, from, ret,	\
		"	GETB D1Ar1,[%1++]\n"		\
		"	SETB [%0],D1Ar1\n"		\
		"6:	SETB [%0++],D1Ar1\n",		\
		"7:	ADD  %2,%2,#1\n",		\
		"	.long 6b,7b\n")

#define __asm_copy_to_user_10x_cont(to, from, ret, COPY, FIXUP, TENTRY) \
	__asm_copy_to_user_8x_cont(to, from, ret,	\
		"	GETW D1Ar1,[%1++]\n"		\
		"	SETW [%0],D1Ar1\n"		\
		"6:	SETW [%0++],D1Ar1\n" COPY,	\
		"7:	ADD  %2,%2,#2\n" FIXUP,		\
		"	.long 6b,7b\n" TENTRY)

#define __asm_copy_to_user_10(to, from, ret) \
	__asm_copy_to_user_10x_cont(to, from, ret, "", "", "")

#define __asm_copy_to_user_11(to, from, ret) \
	__asm_copy_to_user_10x_cont(to, from, ret,	\
		"	GETB D1Ar1,[%1++]\n"		\
		"	SETB [%0],D1Ar1\n"		\
		"8:	SETB [%0++],D1Ar1\n",		\
		"9:	ADD  %2,%2,#1\n",		\
		"	.long 8b,9b\n")

#define __asm_copy_to_user_12x_cont(to, from, ret, COPY, FIXUP, TENTRY) \
	__asm_copy_to_user_8x_cont(to, from, ret,	\
		"	GETD D1Ar1,[%1++]\n"		\
		"	SETD [%0],D1Ar1\n"		\
		"6:	SETD [%0++],D1Ar1\n" COPY,	\
		"7:	ADD  %2,%2,#4\n" FIXUP,		\
		"	.long 6b,7b\n" TENTRY)
#define __asm_copy_to_user_12(to, from, ret) \
	__asm_copy_to_user_12x_cont(to, from, ret, "", "", "")

#define __asm_copy_to_user_13(to, from, ret) \
	__asm_copy_to_user_12x_cont(to, from, ret,	\
		"	GETB D1Ar1,[%1++]\n"		\
		"	SETB [%0],D1Ar1\n"		\
		"8:	SETB [%0++],D1Ar1\n",		\
		"9:	ADD  %2,%2,#1\n",		\
		"	.long 8b,9b\n")

#define __asm_copy_to_user_14x_cont(to, from, ret, COPY, FIXUP, TENTRY) \
	__asm_copy_to_user_12x_cont(to, from, ret,	\
		"	GETW D1Ar1,[%1++]\n"		\
		"	SETW [%0],D1Ar1\n"		\
		"8:	SETW [%0++],D1Ar1\n" COPY,	\
		"9:	ADD  %2,%2,#2\n" FIXUP,		\
		"	.long 8b,9b\n" TENTRY)

#define __asm_copy_to_user_14(to, from, ret) \
	__asm_copy_to_user_14x_cont(to, from, ret, "", "", "")

#define __asm_copy_to_user_15(to, from, ret) \
	__asm_copy_to_user_14x_cont(to, from, ret,	\
		"	GETB D1Ar1,[%1++]\n"		\
		"	SETB [%0],D1Ar1\n"		\
		"10:	SETB [%0++],D1Ar1\n",		\
		"11:	ADD  %2,%2,#1\n",		\
		"	.long 10b,11b\n")

#define __asm_copy_to_user_16x_cont(to, from, ret, COPY, FIXUP, TENTRY) \
	__asm_copy_to_user_12x_cont(to, from, ret,	\
		"	GETD D1Ar1,[%1++]\n"		\
		"	SETD [%0],D1Ar1\n"		\
		"8:	SETD [%0++],D1Ar1\n" COPY,	\
		"9:	ADD  %2,%2,#4\n" FIXUP,		\
		"	.long 8b,9b\n" TENTRY)

#define __asm_copy_to_user_16(to, from, ret) \
		__asm_copy_to_user_16x_cont(to, from, ret, "", "", "")

#define __asm_copy_to_user_8x64(to, from, ret) \
	asm volatile (					\
		"	GETL D0Ar2,D1Ar1,[%1++]\n"	\
		"	SETL [%0],D0Ar2,D1Ar1\n"	\
		"2:	SETL [%0++],D0Ar2,D1Ar1\n"	\
		"1:\n"					\
		"	.section .fixup,\"ax\"\n"	\
		"3:	ADD  %2,%2,#8\n"		\
		"	MOVT    D0Ar2,#HI(1b)\n"	\
		"	JUMP    D0Ar2,#LO(1b)\n"	\
		"	.previous\n"			\
		"	.section __ex_table,\"a\"\n"	\
		"	.long 2b,3b\n"			\
		"	.previous\n"			\
		: "=r" (to), "=r" (from), "=r" (ret)	\
		: "0" (to), "1" (from), "2" (ret)	\
		: "D1Ar1", "D0Ar2", "memory")

/*
 *	optimized copying loop using RAPF when 64 bit aligned
 *
 *	n		will be automatically decremented inside the loop
 *	ret		will be left intact. if error occurs we will rewind
 *			so that the original non optimized code will fill up
 *			this value correctly.
 *
 *	on fault:
 *		>	n will hold total number of uncopied bytes
 *
 *		>	{'to','from'} will be rewind back so that
 *			the non-optimized code will do the proper fix up
 *
 *	DCACHE drops the cacheline which helps in reducing cache
 *	pollution.
 *
 *	We introduce an extra SETL at the end of the loop to
 *	ensure we don't fall off the loop before we catch all
 *	erros.
 *
 *	NOTICE:
 *		LSM_STEP in TXSTATUS must be cleared in fix up code.
 *		since we're using M{S,G}ETL, a fault might happen at
 *		any address in the middle of M{S,G}ETL causing
 *		the value of LSM_STEP to be incorrect which can
 *		cause subsequent use of M{S,G}ET{L,D} to go wrong.
 *		ie: if LSM_STEP was 1 when a fault occurs, the
 *		next call to M{S,G}ET{L,D} will skip the first
 *		copy/getting as it think that the first 1 has already
 *		been done.
 *
 */
#define __asm_copy_user_64bit_rapf_loop(				\
		to, from, ret, n, id, FIXUP)				\
	asm volatile (							\
		".balign 8\n"						\
		"MOV	RAPF, %1\n"					\
		"MSETL	[A0StP++], D0Ar6, D0FrT, D0.5, D0.6, D0.7\n"	\
		"MOV	D0Ar6, #0\n"					\
		"LSR	D1Ar5, %3, #6\n"				\
		"SUB	TXRPT, D1Ar5, #2\n"				\
		"MOV	RAPF, %1\n"					\
		"$Lloop"id":\n"						\
		"ADD	RAPF, %1, #64\n"				\
		"21:\n"							\
		"MGETL	D0FrT, D0.5, D0.6, D0.7, [%1++]\n"		\
		"22:\n"							\
		"MSETL	[%0++], D0FrT, D0.5, D0.6, D0.7\n"		\
		"SUB	%3, %3, #32\n"					\
		"23:\n"							\
		"MGETL	D0FrT, D0.5, D0.6, D0.7, [%1++]\n"		\
		"24:\n"							\
		"MSETL	[%0++], D0FrT, D0.5, D0.6, D0.7\n"		\
		"SUB	%3, %3, #32\n"					\
		"DCACHE	[%1+#-64], D0Ar6\n"				\
		"BR	$Lloop"id"\n"					\
									\
		"MOV	RAPF, %1\n"					\
		"25:\n"							\
		"MGETL	D0FrT, D0.5, D0.6, D0.7, [%1++]\n"		\
		"26:\n"							\
		"MSETL	[%0++], D0FrT, D0.5, D0.6, D0.7\n"		\
		"SUB	%3, %3, #32\n"					\
		"27:\n"							\
		"MGETL	D0FrT, D0.5, D0.6, D0.7, [%1++]\n"		\
		"28:\n"							\
		"MSETL	[%0++], D0FrT, D0.5, D0.6, D0.7\n"		\
		"SUB	%0, %0, #8\n"					\
		"29:\n"							\
		"SETL	[%0++], D0.7, D1.7\n"				\
		"SUB	%3, %3, #32\n"					\
		"1:"							\
		"DCACHE	[%1+#-64], D0Ar6\n"				\
		"GETL    D0Ar6, D1Ar5, [A0StP+#-40]\n"			\
		"GETL    D0FrT, D1RtP, [A0StP+#-32]\n"			\
		"GETL    D0.5, D1.5, [A0StP+#-24]\n"			\
		"GETL    D0.6, D1.6, [A0StP+#-16]\n"			\
		"GETL    D0.7, D1.7, [A0StP+#-8]\n"			\
		"SUB A0StP, A0StP, #40\n"				\
		"	.section .fixup,\"ax\"\n"			\
		"4:\n"							\
		"	ADD	%0, %0, #8\n"				\
		"3:\n"							\
		"	MOV	D0Ar2, TXSTATUS\n"			\
		"	MOV	D1Ar1, TXSTATUS\n"			\
		"	AND	D1Ar1, D1Ar1, #0xFFFFF8FF\n"		\
		"	MOV	TXSTATUS, D1Ar1\n"			\
			FIXUP						\
		"	MOVT    D0Ar2,#HI(1b)\n"			\
		"	JUMP    D0Ar2,#LO(1b)\n"			\
		"	.previous\n"					\
		"	.section __ex_table,\"a\"\n"			\
		"	.long 21b,3b\n"					\
		"	.long 22b,3b\n"					\
		"	.long 23b,3b\n"					\
		"	.long 24b,3b\n"					\
		"	.long 25b,3b\n"					\
		"	.long 26b,3b\n"					\
		"	.long 27b,3b\n"					\
		"	.long 28b,3b\n"					\
		"	.long 29b,4b\n"					\
		"	.previous\n"					\
		: "=r" (to), "=r" (from), "=r" (ret), "=d" (n)		\
		: "0" (to), "1" (from), "2" (ret), "3" (n)		\
		: "D1Ar1", "D0Ar2", "memory")

/*	rewind 'to' and 'from'  pointers when a fault occurs
 *
 *	Rationale:
 *		A fault always occurs on writing to user buffer. A fault
 *		is at a single address, so we need to rewind by only 4
 *		bytes.
 *		Since we do a complete read from kernel buffer before
 *		writing, we need to rewind it also. The amount to be
 *		rewind equals the number of faulty writes in MSETD
 *		which is: [4 - (LSM_STEP-1)]*8
 *		LSM_STEP is bits 10:8 in TXSTATUS which is already read
 *		and stored in D0Ar2
 *
 *		NOTE: If a fault occurs at the last operation in M{G,S}ETL
 *			LSM_STEP will be 0. ie: we do 4 writes in our case, if
 *			a fault happens at the 4th write, LSM_STEP will be 0
 *			instead of 4. The code copes with that.
 *
 *		n is updated by the number of successful writes, which is:
 *		n = n - (LSM_STEP-1)*8
 */
#define __asm_copy_to_user_64bit_rapf_loop(to,	from, ret, n, id)\
	__asm_copy_user_64bit_rapf_loop(to, from, ret, n, id,		\
		"LSR	D0Ar2, D0Ar2, #8\n"				\
		"AND	D0Ar2, D0Ar2, #0x7\n"				\
		"ADDZ	D0Ar2, D0Ar2, #4\n"				\
		"SUB	D0Ar2, D0Ar2, #1\n"				\
		"MOV	D1Ar1, #4\n"					\
		"SUB	D0Ar2, D1Ar1, D0Ar2\n"				\
		"LSL	D0Ar2, D0Ar2, #3\n"				\
		"LSL	D1Ar1, D1Ar1, #3\n"				\
		"SUB	D1Ar1, D1Ar1, D0Ar2\n"				\
		"SUB	%0, %0, #8\n"					\
		"SUB	%1,	%1,D0Ar2\n"				\
		"SUB	%3, %3, D1Ar1\n")

/*
 *	optimized copying loop using RAPF when 32 bit aligned
 *
 *	n		will be automatically decremented inside the loop
 *	ret		will be left intact. if error occurs we will rewind
 *			so that the original non optimized code will fill up
 *			this value correctly.
 *
 *	on fault:
 *		>	n will hold total number of uncopied bytes
 *
 *		>	{'to','from'} will be rewind back so that
 *			the non-optimized code will do the proper fix up
 *
 *	DCACHE drops the cacheline which helps in reducing cache
 *	pollution.
 *
 *	We introduce an extra SETD at the end of the loop to
 *	ensure we don't fall off the loop before we catch all
 *	erros.
 *
 *	NOTICE:
 *		LSM_STEP in TXSTATUS must be cleared in fix up code.
 *		since we're using M{S,G}ETL, a fault might happen at
 *		any address in the middle of M{S,G}ETL causing
 *		the value of LSM_STEP to be incorrect which can
 *		cause subsequent use of M{S,G}ET{L,D} to go wrong.
 *		ie: if LSM_STEP was 1 when a fault occurs, the
 *		next call to M{S,G}ET{L,D} will skip the first
 *		copy/getting as it think that the first 1 has already
 *		been done.
 *
 */
#define __asm_copy_user_32bit_rapf_loop(				\
			to,	from, ret, n, id, FIXUP)		\
	asm volatile (							\
		".balign 8\n"						\
		"MOV	RAPF, %1\n"					\
		"MSETL	[A0StP++], D0Ar6, D0FrT, D0.5, D0.6, D0.7\n"	\
		"MOV	D0Ar6, #0\n"					\
		"LSR	D1Ar5, %3, #6\n"				\
		"SUB	TXRPT, D1Ar5, #2\n"				\
		"MOV	RAPF, %1\n"					\
	"$Lloop"id":\n"							\
		"ADD	RAPF, %1, #64\n"				\
		"21:\n"							\
		"MGETD	D0FrT, D0.5, D0.6, D0.7, [%1++]\n"		\
		"22:\n"							\
		"MSETD	[%0++], D0FrT, D0.5, D0.6, D0.7\n"		\
		"SUB	%3, %3, #16\n"					\
		"23:\n"							\
		"MGETD	D0FrT, D0.5, D0.6, D0.7, [%1++]\n"		\
		"24:\n"							\
		"MSETD	[%0++], D0FrT, D0.5, D0.6, D0.7\n"		\
		"SUB	%3, %3, #16\n"					\
		"25:\n"							\
		"MGETD	D0FrT, D0.5, D0.6, D0.7, [%1++]\n"		\
		"26:\n"							\
		"MSETD	[%0++], D0FrT, D0.5, D0.6, D0.7\n"		\
		"SUB	%3, %3, #16\n"					\
		"27:\n"							\
		"MGETD	D0FrT, D0.5, D0.6, D0.7, [%1++]\n"		\
		"28:\n"							\
		"MSETD	[%0++], D0FrT, D0.5, D0.6, D0.7\n"		\
		"SUB	%3, %3, #16\n"					\
		"DCACHE	[%1+#-64], D0Ar6\n"				\
		"BR	$Lloop"id"\n"					\
									\
		"MOV	RAPF, %1\n"					\
		"29:\n"							\
		"MGETD	D0FrT, D0.5, D0.6, D0.7, [%1++]\n"		\
		"30:\n"							\
		"MSETD	[%0++], D0FrT, D0.5, D0.6, D0.7\n"		\
		"SUB	%3, %3, #16\n"					\
		"31:\n"							\
		"MGETD	D0FrT, D0.5, D0.6, D0.7, [%1++]\n"		\
		"32:\n"							\
		"MSETD	[%0++], D0FrT, D0.5, D0.6, D0.7\n"		\
		"SUB	%3, %3, #16\n"					\
		"33:\n"							\
		"MGETD	D0FrT, D0.5, D0.6, D0.7, [%1++]\n"		\
		"34:\n"							\
		"MSETD	[%0++], D0FrT, D0.5, D0.6, D0.7\n"		\
		"SUB	%3, %3, #16\n"					\
		"35:\n"							\
		"MGETD	D0FrT, D0.5, D0.6, D0.7, [%1++]\n"		\
		"36:\n"							\
		"MSETD	[%0++], D0FrT, D0.5, D0.6, D0.7\n"		\
		"SUB	%0, %0, #4\n"					\
		"37:\n"							\
		"SETD	[%0++], D0.7\n"					\
		"SUB	%3, %3, #16\n"					\
		"1:"							\
		"DCACHE	[%1+#-64], D0Ar6\n"				\
		"GETL    D0Ar6, D1Ar5, [A0StP+#-40]\n"			\
		"GETL    D0FrT, D1RtP, [A0StP+#-32]\n"			\
		"GETL    D0.5, D1.5, [A0StP+#-24]\n"			\
		"GETL    D0.6, D1.6, [A0StP+#-16]\n"			\
		"GETL    D0.7, D1.7, [A0StP+#-8]\n"			\
		"SUB A0StP, A0StP, #40\n"				\
		"	.section .fixup,\"ax\"\n"			\
		"4:\n"							\
		"	ADD		%0, %0, #4\n"			\
		"3:\n"							\
		"	MOV	D0Ar2, TXSTATUS\n"			\
		"	MOV	D1Ar1, TXSTATUS\n"			\
		"	AND	D1Ar1, D1Ar1, #0xFFFFF8FF\n"		\
		"	MOV	TXSTATUS, D1Ar1\n"			\
			FIXUP						\
		"	MOVT    D0Ar2,#HI(1b)\n"			\
		"	JUMP    D0Ar2,#LO(1b)\n"			\
		"	.previous\n"					\
		"	.section __ex_table,\"a\"\n"			\
		"	.long 21b,3b\n"					\
		"	.long 22b,3b\n"					\
		"	.long 23b,3b\n"					\
		"	.long 24b,3b\n"					\
		"	.long 25b,3b\n"					\
		"	.long 26b,3b\n"					\
		"	.long 27b,3b\n"					\
		"	.long 28b,3b\n"					\
		"	.long 29b,3b\n"					\
		"	.long 30b,3b\n"					\
		"	.long 31b,3b\n"					\
		"	.long 32b,3b\n"					\
		"	.long 33b,3b\n"					\
		"	.long 34b,3b\n"					\
		"	.long 35b,3b\n"					\
		"	.long 36b,3b\n"					\
		"	.long 37b,4b\n"					\
		"	.previous\n"					\
		: "=r" (to), "=r" (from), "=r" (ret), "=d" (n)		\
		: "0" (to), "1" (from), "2" (ret), "3" (n)		\
		: "D1Ar1", "D0Ar2", "memory")

/*	rewind 'to' and 'from'  pointers when a fault occurs
 *
 *	Rationale:
 *		A fault always occurs on writing to user buffer. A fault
 *		is at a single address, so we need to rewind by only 4
 *		bytes.
 *		Since we do a complete read from kernel buffer before
 *		writing, we need to rewind it also. The amount to be
 *		rewind equals the number of faulty writes in MSETD
 *		which is: [4 - (LSM_STEP-1)]*4
 *		LSM_STEP is bits 10:8 in TXSTATUS which is already read
 *		and stored in D0Ar2
 *
 *		NOTE: If a fault occurs at the last operation in M{G,S}ETL
 *			LSM_STEP will be 0. ie: we do 4 writes in our case, if
 *			a fault happens at the 4th write, LSM_STEP will be 0
 *			instead of 4. The code copes with that.
 *
 *		n is updated by the number of successful writes, which is:
 *		n = n - (LSM_STEP-1)*4
 */
#define __asm_copy_to_user_32bit_rapf_loop(to, from, ret, n, id)\
	__asm_copy_user_32bit_rapf_loop(to, from, ret, n, id,		\
		"LSR	D0Ar2, D0Ar2, #8\n"				\
		"AND	D0Ar2, D0Ar2, #0x7\n"				\
		"ADDZ	D0Ar2, D0Ar2, #4\n"				\
		"SUB	D0Ar2, D0Ar2, #1\n"				\
		"MOV	D1Ar1, #4\n"					\
		"SUB	D0Ar2, D1Ar1, D0Ar2\n"				\
		"LSL	D0Ar2, D0Ar2, #2\n"				\
		"LSL	D1Ar1, D1Ar1, #2\n"				\
		"SUB	D1Ar1, D1Ar1, D0Ar2\n"				\
		"SUB	%0, %0, #4\n"					\
		"SUB	%1,	%1,	D0Ar2\n"			\
		"SUB	%3, %3, D1Ar1\n")

unsigned long __copy_user(void __user *pdst, const void *psrc,
			  unsigned long n)
{
	register char __user *dst asm ("A0.2") = pdst;
	register const char *src asm ("A1.2") = psrc;
	unsigned long retn = 0;

	if (n == 0)
		return 0;

	if ((unsigned long) src & 1) {
		__asm_copy_to_user_1(dst, src, retn);
		n--;
	}
	if ((unsigned long) dst & 1) {
		/* Worst case - byte copy */
		while (n > 0) {
			__asm_copy_to_user_1(dst, src, retn);
			n--;
		}
	}
	if (((unsigned long) src & 2) && n >= 2) {
		__asm_copy_to_user_2(dst, src, retn);
		n -= 2;
	}
	if ((unsigned long) dst & 2) {
		/* Second worst case - word copy */
		while (n >= 2) {
			__asm_copy_to_user_2(dst, src, retn);
			n -= 2;
		}
	}

#ifdef USE_RAPF
	/* 64 bit copy loop */
	if (!(((unsigned long) src | (__force unsigned long) dst) & 7)) {
		if (n >= RAPF_MIN_BUF_SIZE) {
			/* copy user using 64 bit rapf copy */
			__asm_copy_to_user_64bit_rapf_loop(dst, src, retn,
							n, "64cu");
		}
		while (n >= 8) {
			__asm_copy_to_user_8x64(dst, src, retn);
			n -= 8;
		}
	}
	if (n >= RAPF_MIN_BUF_SIZE) {
		/* copy user using 32 bit rapf copy */
		__asm_copy_to_user_32bit_rapf_loop(dst, src, retn, n, "32cu");
	}
#else
	/* 64 bit copy loop */
	if (!(((unsigned long) src | (__force unsigned long) dst) & 7)) {
		while (n >= 8) {
			__asm_copy_to_user_8x64(dst, src, retn);
			n -= 8;
		}
	}
#endif

	while (n >= 16) {
		__asm_copy_to_user_16(dst, src, retn);
		n -= 16;
	}

	while (n >= 4) {
		__asm_copy_to_user_4(dst, src, retn);
		n -= 4;
	}

	switch (n) {
	case 0:
		break;
	case 1:
		__asm_copy_to_user_1(dst, src, retn);
		break;
	case 2:
		__asm_copy_to_user_2(dst, src, retn);
		break;
	case 3:
		__asm_copy_to_user_3(dst, src, retn);
		break;
	}

	return retn;
}
EXPORT_SYMBOL(__copy_user);

#define __asm_copy_from_user_1(to, from, ret) \
	__asm_copy_user_cont(to, from, ret,	\
		"	GETB D1Ar1,[%1++]\n"	\
		"2:	SETB [%0++],D1Ar1\n",	\
		"3:	ADD  %2,%2,#1\n"	\
		"	SETB [%0++],D1Ar1\n",	\
		"	.long 2b,3b\n")

#define __asm_copy_from_user_2x_cont(to, from, ret, COPY, FIXUP, TENTRY) \
	__asm_copy_user_cont(to, from, ret,		\
		"	GETW D1Ar1,[%1++]\n"		\
		"2:	SETW [%0++],D1Ar1\n" COPY,	\
		"3:	ADD  %2,%2,#2\n"		\
		"	SETW [%0++],D1Ar1\n" FIXUP,	\
		"	.long 2b,3b\n" TENTRY)

#define __asm_copy_from_user_2(to, from, ret) \
	__asm_copy_from_user_2x_cont(to, from, ret, "", "", "")

#define __asm_copy_from_user_3(to, from, ret)		\
	__asm_copy_from_user_2x_cont(to, from, ret,	\
		"	GETB D1Ar1,[%1++]\n"		\
		"4:	SETB [%0++],D1Ar1\n",		\
		"5:	ADD  %2,%2,#1\n"		\
		"	SETB [%0++],D1Ar1\n",		\
		"	.long 4b,5b\n")

#define __asm_copy_from_user_4x_cont(to, from, ret, COPY, FIXUP, TENTRY) \
	__asm_copy_user_cont(to, from, ret,		\
		"	GETD D1Ar1,[%1++]\n"		\
		"2:	SETD [%0++],D1Ar1\n" COPY,	\
		"3:	ADD  %2,%2,#4\n"		\
		"	SETD [%0++],D1Ar1\n" FIXUP,	\
		"	.long 2b,3b\n" TENTRY)

#define __asm_copy_from_user_4(to, from, ret) \
	__asm_copy_from_user_4x_cont(to, from, ret, "", "", "")


#define __asm_copy_from_user_8x64(to, from, ret) \
	asm volatile (				\
		"	GETL D0Ar2,D1Ar1,[%1++]\n"	\
		"2:	SETL [%0++],D0Ar2,D1Ar1\n"	\
		"1:\n"					\
		"	.section .fixup,\"ax\"\n"	\
		"	MOV D1Ar1,#0\n"			\
		"	MOV D0Ar2,#0\n"			\
		"3:	ADD  %2,%2,#8\n"		\
		"	SETL [%0++],D0Ar2,D1Ar1\n"	\
		"	MOVT    D0Ar2,#HI(1b)\n"	\
		"	JUMP    D0Ar2,#LO(1b)\n"	\
		"	.previous\n"			\
		"	.section __ex_table,\"a\"\n"	\
		"	.long 2b,3b\n"			\
		"	.previous\n"			\
		: "=a" (to), "=r" (from), "=r" (ret)	\
		: "0" (to), "1" (from), "2" (ret)	\
		: "D1Ar1", "D0Ar2", "memory")

/*	rewind 'from' pointer when a fault occurs
 *
 *	Rationale:
 *		A fault occurs while reading from user buffer, which is the
 *		source. Since the fault is at a single address, we only
 *		need to rewind by 8 bytes.
 *		Since we don't write to kernel buffer until we read first,
 *		the kernel buffer is at the right state and needn't be
 *		corrected.
 */
#define __asm_copy_from_user_64bit_rapf_loop(to, from, ret, n, id)	\
	__asm_copy_user_64bit_rapf_loop(to, from, ret, n, id,		\
		"SUB	%1, %1, #8\n")

/*	rewind 'from' pointer when a fault occurs
 *
 *	Rationale:
 *		A fault occurs while reading from user buffer, which is the
 *		source. Since the fault is at a single address, we only
 *		need to rewind by 4 bytes.
 *		Since we don't write to kernel buffer until we read first,
 *		the kernel buffer is at the right state and needn't be
 *		corrected.
 */
#define __asm_copy_from_user_32bit_rapf_loop(to, from, ret, n, id)	\
	__asm_copy_user_32bit_rapf_loop(to, from, ret, n, id,		\
		"SUB	%1, %1, #4\n")


/* Copy from user to kernel, zeroing the bytes that were inaccessible in
   userland.  The return-value is the number of bytes that were
   inaccessible.  */
unsigned long __copy_user_zeroing(void *pdst, const void __user *psrc,
				  unsigned long n)
{
	register char *dst asm ("A0.2") = pdst;
	register const char __user *src asm ("A1.2") = psrc;
	unsigned long retn = 0;

	if (n == 0)
		return 0;

	if ((unsigned long) src & 1) {
		__asm_copy_from_user_1(dst, src, retn);
		n--;
		if (retn)
			goto copy_exception_bytes;
	}
	if ((unsigned long) dst & 1) {
		/* Worst case - byte copy */
		while (n > 0) {
			__asm_copy_from_user_1(dst, src, retn);
			n--;
			if (retn)
				goto copy_exception_bytes;
		}
	}
	if (((unsigned long) src & 2) && n >= 2) {
		__asm_copy_from_user_2(dst, src, retn);
		n -= 2;
		if (retn)
			goto copy_exception_bytes;
	}
	if ((unsigned long) dst & 2) {
		/* Second worst case - word copy */
		while (n >= 2) {
			__asm_copy_from_user_2(dst, src, retn);
			n -= 2;
			if (retn)
				goto copy_exception_bytes;
		}
	}

#ifdef USE_RAPF
	/* 64 bit copy loop */
	if (!(((unsigned long) src | (unsigned long) dst) & 7)) {
		if (n >= RAPF_MIN_BUF_SIZE) {
			/* Copy using fast 64bit rapf */
			__asm_copy_from_user_64bit_rapf_loop(dst, src, retn,
							n, "64cuz");
		}
		while (n >= 8) {
			__asm_copy_from_user_8x64(dst, src, retn);
			n -= 8;
			if (retn)
				goto copy_exception_bytes;
		}
	}

	if (n >= RAPF_MIN_BUF_SIZE) {
		/* Copy using fast 32bit rapf */
		__asm_copy_from_user_32bit_rapf_loop(dst, src, retn,
						n, "32cuz");
	}
#else
	/* 64 bit copy loop */
	if (!(((unsigned long) src | (unsigned long) dst) & 7)) {
		while (n >= 8) {
			__asm_copy_from_user_8x64(dst, src, retn);
			n -= 8;
			if (retn)
				goto copy_exception_bytes;
		}
	}
#endif

	while (n >= 4) {
		__asm_copy_from_user_4(dst, src, retn);
		n -= 4;

		if (retn)
			goto copy_exception_bytes;
	}

	/* If we get here, there were no memory read faults.  */
	switch (n) {
		/* These copies are at least "naturally aligned" (so we don't
		   have to check each byte), due to the src alignment code.
		   The *_3 case *will* get the correct count for retn.  */
	case 0:
		/* This case deliberately left in (if you have doubts check the
		   generated assembly code).  */
		break;
	case 1:
		__asm_copy_from_user_1(dst, src, retn);
		break;
	case 2:
		__asm_copy_from_user_2(dst, src, retn);
		break;
	case 3:
		__asm_copy_from_user_3(dst, src, retn);
		break;
	}

	/* If we get here, retn correctly reflects the number of failing
	   bytes.  */
	return retn;

 copy_exception_bytes:
	/* We already have "retn" bytes cleared, and need to clear the
	   remaining "n" bytes.  A non-optimized simple byte-for-byte in-line
	   memset is preferred here, since this isn't speed-critical code and
	   we'd rather have this a leaf-function than calling memset.  */
	{
		char *endp;
		for (endp = dst + n; dst < endp; dst++)
			*dst = 0;
	}

	return retn + n;
}
EXPORT_SYMBOL(__copy_user_zeroing);

#define __asm_clear_8x64(to, ret) \
	asm volatile (					\
		"	MOV  D0Ar2,#0\n"		\
		"	MOV  D1Ar1,#0\n"		\
		"	SETL [%0],D0Ar2,D1Ar1\n"	\
		"2:	SETL [%0++],D0Ar2,D1Ar1\n"	\
		"1:\n"					\
		"	.section .fixup,\"ax\"\n"	\
		"3:	ADD  %1,%1,#8\n"		\
		"	MOVT    D0Ar2,#HI(1b)\n"	\
		"	JUMP    D0Ar2,#LO(1b)\n"	\
		"	.previous\n"			\
		"	.section __ex_table,\"a\"\n"	\
		"	.long 2b,3b\n"			\
		"	.previous\n"			\
		: "=r" (to), "=r" (ret)	\
		: "0" (to), "1" (ret)	\
		: "D1Ar1", "D0Ar2", "memory")

/* Zero userspace.  */

#define __asm_clear(to, ret, CLEAR, FIXUP, TENTRY) \
	asm volatile (					\
		"	MOV D1Ar1,#0\n"			\
			CLEAR				\
		"1:\n"					\
		"	.section .fixup,\"ax\"\n"	\
			FIXUP				\
		"	MOVT    D1Ar1,#HI(1b)\n"	\
		"	JUMP    D1Ar1,#LO(1b)\n"	\
		"	.previous\n"			\
		"	.section __ex_table,\"a\"\n"	\
			TENTRY				\
		"	.previous"			\
		: "=r" (to), "=r" (ret)			\
		: "0" (to), "1" (ret)			\
		: "D1Ar1", "memory")

#define __asm_clear_1(to, ret) \
	__asm_clear(to, ret,			\
		"	SETB [%0],D1Ar1\n"	\
		"2:	SETB [%0++],D1Ar1\n",	\
		"3:	ADD  %1,%1,#1\n",	\
		"	.long 2b,3b\n")

#define __asm_clear_2(to, ret) \
	__asm_clear(to, ret,			\
		"	SETW [%0],D1Ar1\n"	\
		"2:	SETW [%0++],D1Ar1\n",	\
		"3:	ADD  %1,%1,#2\n",	\
		"	.long 2b,3b\n")

#define __asm_clear_3(to, ret) \
	__asm_clear(to, ret,			\
		 "2:	SETW [%0++],D1Ar1\n"	\
		 "	SETB [%0],D1Ar1\n"	\
		 "3:	SETB [%0++],D1Ar1\n",	\
		 "4:	ADD  %1,%1,#2\n"	\
		 "5:	ADD  %1,%1,#1\n",	\
		 "	.long 2b,4b\n"		\
		 "	.long 3b,5b\n")

#define __asm_clear_4x_cont(to, ret, CLEAR, FIXUP, TENTRY) \
	__asm_clear(to, ret,				\
		"	SETD [%0],D1Ar1\n"		\
		"2:	SETD [%0++],D1Ar1\n" CLEAR,	\
		"3:	ADD  %1,%1,#4\n" FIXUP,		\
		"	.long 2b,3b\n" TENTRY)

#define __asm_clear_4(to, ret) \
	__asm_clear_4x_cont(to, ret, "", "", "")

#define __asm_clear_8x_cont(to, ret, CLEAR, FIXUP, TENTRY) \
	__asm_clear_4x_cont(to, ret,			\
		"	SETD [%0],D1Ar1\n"		\
		"4:	SETD [%0++],D1Ar1\n" CLEAR,	\
		"5:	ADD  %1,%1,#4\n" FIXUP,		\
		"	.long 4b,5b\n" TENTRY)

#define __asm_clear_8(to, ret) \
	__asm_clear_8x_cont(to, ret, "", "", "")

#define __asm_clear_12x_cont(to, ret, CLEAR, FIXUP, TENTRY) \
	__asm_clear_8x_cont(to, ret,			\
		"	SETD [%0],D1Ar1\n"		\
		"6:	SETD [%0++],D1Ar1\n" CLEAR,	\
		"7:	ADD  %1,%1,#4\n" FIXUP,		\
		"	.long 6b,7b\n" TENTRY)

#define __asm_clear_12(to, ret) \
	__asm_clear_12x_cont(to, ret, "", "", "")

#define __asm_clear_16x_cont(to, ret, CLEAR, FIXUP, TENTRY) \
	__asm_clear_12x_cont(to, ret,			\
		"	SETD [%0],D1Ar1\n"		\
		"8:	SETD [%0++],D1Ar1\n" CLEAR,	\
		"9:	ADD  %1,%1,#4\n" FIXUP,		\
		"	.long 8b,9b\n" TENTRY)

#define __asm_clear_16(to, ret) \
	__asm_clear_16x_cont(to, ret, "", "", "")

unsigned long __do_clear_user(void __user *pto, unsigned long pn)
{
	register char __user *dst asm ("D0Re0") = pto;
	register unsigned long n asm ("D1Re0") = pn;
	register unsigned long retn asm ("D0Ar6") = 0;

	if ((unsigned long) dst & 1) {
		__asm_clear_1(dst, retn);
		n--;
	}

	if ((unsigned long) dst & 2) {
		__asm_clear_2(dst, retn);
		n -= 2;
	}

	/* 64 bit copy loop */
	if (!((__force unsigned long) dst & 7)) {
		while (n >= 8) {
			__asm_clear_8x64(dst, retn);
			n -= 8;
		}
	}

	while (n >= 16) {
		__asm_clear_16(dst, retn);
		n -= 16;
	}

	while (n >= 4) {
		__asm_clear_4(dst, retn);
		n -= 4;
	}

	switch (n) {
	case 0:
		break;
	case 1:
		__asm_clear_1(dst, retn);
		break;
	case 2:
		__asm_clear_2(dst, retn);
		break;
	case 3:
		__asm_clear_3(dst, retn);
		break;
	}

	return retn;
}
EXPORT_SYMBOL(__do_clear_user);

unsigned char __get_user_asm_b(const void __user *addr, long *err)
{
	register unsigned char x asm ("D0Re0") = 0;
	asm volatile (
		"	GETB %0,[%2]\n"
		"1:\n"
		"	GETB %0,[%2]\n"
		"2:\n"
		"	.section .fixup,\"ax\"\n"
		"3:	MOV     D0FrT,%3\n"
		"	SETD    [%1],D0FrT\n"
		"	MOVT    D0FrT,#HI(2b)\n"
		"	JUMP    D0FrT,#LO(2b)\n"
		"	.previous\n"
		"	.section __ex_table,\"a\"\n"
		"	.long 1b,3b\n"
		"	.previous\n"
		: "=r" (x)
		: "r" (err), "r" (addr), "P" (-EFAULT)
		: "D0FrT");
	return x;
}
EXPORT_SYMBOL(__get_user_asm_b);

unsigned short __get_user_asm_w(const void __user *addr, long *err)
{
	register unsigned short x asm ("D0Re0") = 0;
	asm volatile (
		"	GETW %0,[%2]\n"
		"1:\n"
		"	GETW %0,[%2]\n"
		"2:\n"
		"	.section .fixup,\"ax\"\n"
		"3:	MOV     D0FrT,%3\n"
		"	SETD    [%1],D0FrT\n"
		"	MOVT    D0FrT,#HI(2b)\n"
		"	JUMP    D0FrT,#LO(2b)\n"
		"	.previous\n"
		"	.section __ex_table,\"a\"\n"
		"	.long 1b,3b\n"
		"	.previous\n"
		: "=r" (x)
		: "r" (err), "r" (addr), "P" (-EFAULT)
		: "D0FrT");
	return x;
}
EXPORT_SYMBOL(__get_user_asm_w);

unsigned int __get_user_asm_d(const void __user *addr, long *err)
{
	register unsigned int x asm ("D0Re0") = 0;
	asm volatile (
		"	GETD %0,[%2]\n"
		"1:\n"
		"	GETD %0,[%2]\n"
		"2:\n"
		"	.section .fixup,\"ax\"\n"
		"3:	MOV     D0FrT,%3\n"
		"	SETD    [%1],D0FrT\n"
		"	MOVT    D0FrT,#HI(2b)\n"
		"	JUMP    D0FrT,#LO(2b)\n"
		"	.previous\n"
		"	.section __ex_table,\"a\"\n"
		"	.long 1b,3b\n"
		"	.previous\n"
		: "=r" (x)
		: "r" (err), "r" (addr), "P" (-EFAULT)
		: "D0FrT");
	return x;
}
EXPORT_SYMBOL(__get_user_asm_d);

long __put_user_asm_b(unsigned int x, void __user *addr)
{
	register unsigned int err asm ("D0Re0") = 0;
	asm volatile (
		"	MOV  %0,#0\n"
		"	SETB [%2],%1\n"
		"1:\n"
		"	SETB [%2],%1\n"
		"2:\n"
		".section .fixup,\"ax\"\n"
		"3:	MOV     %0,%3\n"
		"	MOVT    D0FrT,#HI(2b)\n"
		"	JUMP    D0FrT,#LO(2b)\n"
		".previous\n"
		".section __ex_table,\"a\"\n"
		"	.long 1b,3b\n"
		".previous"
		: "=r"(err)
		: "d" (x), "a" (addr), "P"(-EFAULT)
		: "D0FrT");
	return err;
}
EXPORT_SYMBOL(__put_user_asm_b);

long __put_user_asm_w(unsigned int x, void __user *addr)
{
	register unsigned int err asm ("D0Re0") = 0;
	asm volatile (
		"	MOV  %0,#0\n"
		"	SETW [%2],%1\n"
		"1:\n"
		"	SETW [%2],%1\n"
		"2:\n"
		".section .fixup,\"ax\"\n"
		"3:	MOV     %0,%3\n"
		"	MOVT    D0FrT,#HI(2b)\n"
		"	JUMP    D0FrT,#LO(2b)\n"
		".previous\n"
		".section __ex_table,\"a\"\n"
		"	.long 1b,3b\n"
		".previous"
		: "=r"(err)
		: "d" (x), "a" (addr), "P"(-EFAULT)
		: "D0FrT");
	return err;
}
EXPORT_SYMBOL(__put_user_asm_w);

long __put_user_asm_d(unsigned int x, void __user *addr)
{
	register unsigned int err asm ("D0Re0") = 0;
	asm volatile (
		"	MOV  %0,#0\n"
		"	SETD [%2],%1\n"
		"1:\n"
		"	SETD [%2],%1\n"
		"2:\n"
		".section .fixup,\"ax\"\n"
		"3:	MOV     %0,%3\n"
		"	MOVT    D0FrT,#HI(2b)\n"
		"	JUMP    D0FrT,#LO(2b)\n"
		".previous\n"
		".section __ex_table,\"a\"\n"
		"	.long 1b,3b\n"
		".previous"
		: "=r"(err)
		: "d" (x), "a" (addr), "P"(-EFAULT)
		: "D0FrT");
	return err;
}
EXPORT_SYMBOL(__put_user_asm_d);

long __put_user_asm_l(unsigned long long x, void __user *addr)
{
	register unsigned int err asm ("D0Re0") = 0;
	asm volatile (
		"	MOV  %0,#0\n"
		"	SETL [%2],%1,%t1\n"
		"1:\n"
		"	SETL [%2],%1,%t1\n"
		"2:\n"
		".section .fixup,\"ax\"\n"
		"3:	MOV     %0,%3\n"
		"	MOVT    D0FrT,#HI(2b)\n"
		"	JUMP    D0FrT,#LO(2b)\n"
		".previous\n"
		".section __ex_table,\"a\"\n"
		"	.long 1b,3b\n"
		".previous"
		: "=r"(err)
		: "d" (x), "a" (addr), "P"(-EFAULT)
		: "D0FrT");
	return err;
}
EXPORT_SYMBOL(__put_user_asm_l);

long strnlen_user(const char __user *src, long count)
{
	long res;

	if (!access_ok(VERIFY_READ, src, 0))
		return 0;

	asm volatile ("	MOV     D0Ar4, %1\n"
		      "	MOV     D0Ar6, %2\n"
		      "0:\n"
		      "	SUBS    D0FrT, D0Ar6, #0\n"
		      "	SUB     D0Ar6, D0Ar6, #1\n"
		      "	BLE     2f\n"
		      "	GETB    D0FrT, [D0Ar4+#1++]\n"
		      "1:\n"
		      "	TST     D0FrT, #255\n"
		      "	BNE     0b\n"
		      "2:\n"
		      "	SUB     %0, %2, D0Ar6\n"
		      "3:\n"
		      "	.section .fixup,\"ax\"\n"
		      "4:\n"
		      "	MOV     %0, #0\n"
		      "	MOVT    D0FrT,#HI(3b)\n"
		      "	JUMP    D0FrT,#LO(3b)\n"
		      "	.previous\n"
		      "	.section __ex_table,\"a\"\n"
		      "	.long 1b,4b\n"
		      "	.previous\n"
		      : "=r" (res)
		      : "r" (src), "r" (count)
		      : "D0FrT", "D0Ar4", "D0Ar6", "cc");

	return res;
}
EXPORT_SYMBOL(strnlen_user);

long __strncpy_from_user(char *dst, const char __user *src, long count)
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
	 *      char tmp2;
	 *      long tmp1, tmp3;
	 *      tmp1 = count;
	 *      while ((*dst++ = (tmp2 = *src++)) != 0
	 *             && --tmp1)
	 *        ;
	 *
	 *      res = count - tmp1;
	 *
	 *  with tweaks.
	 */

	asm volatile ("	MOV  %0,%3\n"
		      "1:\n"
		      "	GETB D0FrT,[%2++]\n"
		      "2:\n"
		      "	CMP  D0FrT,#0\n"
		      "	SETB [%1++],D0FrT\n"
		      "	BEQ  3f\n"
		      "	SUBS %0,%0,#1\n"
		      "	BNZ  1b\n"
		      "3:\n"
		      "	SUB  %0,%3,%0\n"
		      "4:\n"
		      "	.section .fixup,\"ax\"\n"
		      "5:\n"
		      "	MOV  %0,%7\n"
		      "	MOVT    D0FrT,#HI(4b)\n"
		      "	JUMP    D0FrT,#LO(4b)\n"
		      "	.previous\n"
		      "	.section __ex_table,\"a\"\n"
		      "	.long 2b,5b\n"
		      "	.previous"
		      : "=r" (res), "=r" (dst), "=r" (src), "=r" (count)
		      : "3" (count), "1" (dst), "2" (src), "P" (-EFAULT)
		      : "D0FrT", "memory", "cc");

	return res;
}
EXPORT_SYMBOL(__strncpy_from_user);
