/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *    User Space Access Routines
 *
 *    Copyright (C) 2000-2002 Hewlett-Packard (John Marvin)
 *    Copyright (C) 2000 Richard Hirst <rhirst with parisc-linux.org>
 *    Copyright (C) 2001 Matthieu Delahaye <delahaym at esiee.fr>
 *    Copyright (C) 2003 Randolph Chung <tausq with parisc-linux.org>
 *    Copyright (C) 2017 Helge Deller <deller@gmx.de>
 *    Copyright (C) 2017 John David Anglin <dave.anglin@bell.net>
 */

/*
 * These routines still have plenty of room for optimization
 * (word & doubleword load/store, dual issue, store hints, etc.).
 */

/*
 * The following routines assume that space register 3 (sr3) contains
 * the space id associated with the current users address space.
 */


	.text
	
#include <asm/assembly.h>
#include <asm/errno.h>
#include <linux/linkage.h>

	/*
	 * get_sr gets the appropriate space value into
	 * sr1 for kernel/user space access, depending
	 * on the flag stored in the task structure.
	 */

	.macro  get_sr
	mfctl       %cr30,%r1
	ldw         TI_SEGMENT(%r1),%r22
	mfsp        %sr3,%r1
	or,<>       %r22,%r0,%r0
	copy        %r0,%r1
	mtsp        %r1,%sr1
	.endm

	/*
	 * unsigned long lclear_user(void *to, unsigned long n)
	 *
	 * Returns 0 for success.
	 * otherwise, returns number of bytes not transferred.
	 */

ENTRY_CFI(lclear_user)
	comib,=,n   0,%r25,$lclu_done
	get_sr
$lclu_loop:
	addib,<>    -1,%r25,$lclu_loop
1:      stbs,ma     %r0,1(%sr1,%r26)

$lclu_done:
	bv          %r0(%r2)
	copy        %r25,%r28

2:	b           $lclu_done
	ldo         1(%r25),%r25

	ASM_EXCEPTIONTABLE_ENTRY(1b,2b)
ENDPROC_CFI(lclear_user)


/*
 * unsigned long pa_memcpy(void *dstp, const void *srcp, unsigned long len)
 *
 * Inputs:
 * - sr1 already contains space of source region
 * - sr2 already contains space of destination region
 *
 * Returns:
 * - number of bytes that could not be copied.
 *   On success, this will be zero.
 *
 * This code is based on a C-implementation of a copy routine written by
 * Randolph Chung, which in turn was derived from the glibc.
 *
 * Several strategies are tried to try to get the best performance for various
 * conditions. In the optimal case, we copy by loops that copy 32- or 16-bytes
 * at a time using general registers.  Unaligned copies are handled either by
 * aligning the destination and then using shift-and-write method, or in a few
 * cases by falling back to a byte-at-a-time copy.
 *
 * Testing with various alignments and buffer sizes shows that this code is
 * often >10x faster than a simple byte-at-a-time copy, even for strangely
 * aligned operands. It is interesting to note that the glibc version of memcpy
 * (written in C) is actually quite fast already. This routine is able to beat
 * it by 30-40% for aligned copies because of the loop unrolling, but in some
 * cases the glibc version is still slightly faster. This lends more
 * credibility that gcc can generate very good code as long as we are careful.
 *
 * Possible optimizations:
 * - add cache prefetching
 * - try not to use the post-increment address modifiers; they may create
 *   additional interlocks. Assumption is that those were only efficient on old
 *   machines (pre PA8000 processors)
 */

	dst = arg0
	src = arg1
	len = arg2
	end = arg3
	t1  = r19
	t2  = r20
	t3  = r21
	t4  = r22
	srcspc = sr1
	dstspc = sr2

	t0 = r1
	a1 = t1
	a2 = t2
	a3 = t3
	a0 = t4

	save_src = ret0
	save_dst = ret1
	save_len = r31

ENTRY_CFI(pa_memcpy)
	/* Last destination address */
	add	dst,len,end

	/* short copy with less than 16 bytes? */
	cmpib,COND(>>=),n 15,len,.Lbyte_loop

	/* same alignment? */
	xor	src,dst,t0
	extru	t0,31,2,t1
	cmpib,<>,n  0,t1,.Lunaligned_copy

#ifdef CONFIG_64BIT
	/* only do 64-bit copies if we can get aligned. */
	extru	t0,31,3,t1
	cmpib,<>,n  0,t1,.Lalign_loop32

	/* loop until we are 64-bit aligned */
.Lalign_loop64:
	extru	dst,31,3,t1
	cmpib,=,n	0,t1,.Lcopy_loop_16_start
20:	ldb,ma	1(srcspc,src),t1
21:	stb,ma	t1,1(dstspc,dst)
	b	.Lalign_loop64
	ldo	-1(len),len

	ASM_EXCEPTIONTABLE_ENTRY(20b,.Lcopy_done)
	ASM_EXCEPTIONTABLE_ENTRY(21b,.Lcopy_done)

.Lcopy_loop_16_start:
	ldi	31,t0
.Lcopy_loop_16:
	cmpb,COND(>>=),n t0,len,.Lword_loop

10:	ldd	0(srcspc,src),t1
11:	ldd	8(srcspc,src),t2
	ldo	16(src),src
12:	std,ma	t1,8(dstspc,dst)
13:	std,ma	t2,8(dstspc,dst)
14:	ldd	0(srcspc,src),t1
15:	ldd	8(srcspc,src),t2
	ldo	16(src),src
16:	std,ma	t1,8(dstspc,dst)
17:	std,ma	t2,8(dstspc,dst)

	ASM_EXCEPTIONTABLE_ENTRY(10b,.Lcopy_done)
	ASM_EXCEPTIONTABLE_ENTRY(11b,.Lcopy16_fault)
	ASM_EXCEPTIONTABLE_ENTRY(12b,.Lcopy_done)
	ASM_EXCEPTIONTABLE_ENTRY(13b,.Lcopy_done)
	ASM_EXCEPTIONTABLE_ENTRY(14b,.Lcopy_done)
	ASM_EXCEPTIONTABLE_ENTRY(15b,.Lcopy16_fault)
	ASM_EXCEPTIONTABLE_ENTRY(16b,.Lcopy_done)
	ASM_EXCEPTIONTABLE_ENTRY(17b,.Lcopy_done)

	b	.Lcopy_loop_16
	ldo	-32(len),len

.Lword_loop:
	cmpib,COND(>>=),n 3,len,.Lbyte_loop
20:	ldw,ma	4(srcspc,src),t1
21:	stw,ma	t1,4(dstspc,dst)
	b	.Lword_loop
	ldo	-4(len),len

	ASM_EXCEPTIONTABLE_ENTRY(20b,.Lcopy_done)
	ASM_EXCEPTIONTABLE_ENTRY(21b,.Lcopy_done)

#endif /* CONFIG_64BIT */

	/* loop until we are 32-bit aligned */
.Lalign_loop32:
	extru	dst,31,2,t1
	cmpib,=,n	0,t1,.Lcopy_loop_8
20:	ldb,ma	1(srcspc,src),t1
21:	stb,ma	t1,1(dstspc,dst)
	b	.Lalign_loop32
	ldo	-1(len),len

	ASM_EXCEPTIONTABLE_ENTRY(20b,.Lcopy_done)
	ASM_EXCEPTIONTABLE_ENTRY(21b,.Lcopy_done)


.Lcopy_loop_8:
	cmpib,COND(>>=),n 15,len,.Lbyte_loop

10:	ldw	0(srcspc,src),t1
11:	ldw	4(srcspc,src),t2
12:	stw,ma	t1,4(dstspc,dst)
13:	stw,ma	t2,4(dstspc,dst)
14:	ldw	8(srcspc,src),t1
15:	ldw	12(srcspc,src),t2
	ldo	16(src),src
16:	stw,ma	t1,4(dstspc,dst)
17:	stw,ma	t2,4(dstspc,dst)

	ASM_EXCEPTIONTABLE_ENTRY(10b,.Lcopy_done)
	ASM_EXCEPTIONTABLE_ENTRY(11b,.Lcopy8_fault)
	ASM_EXCEPTIONTABLE_ENTRY(12b,.Lcopy_done)
	ASM_EXCEPTIONTABLE_ENTRY(13b,.Lcopy_done)
	ASM_EXCEPTIONTABLE_ENTRY(14b,.Lcopy_done)
	ASM_EXCEPTIONTABLE_ENTRY(15b,.Lcopy8_fault)
	ASM_EXCEPTIONTABLE_ENTRY(16b,.Lcopy_done)
	ASM_EXCEPTIONTABLE_ENTRY(17b,.Lcopy_done)

	b	.Lcopy_loop_8
	ldo	-16(len),len

.Lbyte_loop:
	cmpclr,COND(<>) len,%r0,%r0
	b,n	.Lcopy_done
20:	ldb	0(srcspc,src),t1
	ldo	1(src),src
21:	stb,ma	t1,1(dstspc,dst)
	b	.Lbyte_loop
	ldo	-1(len),len

	ASM_EXCEPTIONTABLE_ENTRY(20b,.Lcopy_done)
	ASM_EXCEPTIONTABLE_ENTRY(21b,.Lcopy_done)

.Lcopy_done:
	bv	%r0(%r2)
	sub	end,dst,ret0


	/* src and dst are not aligned the same way. */
	/* need to go the hard way */
.Lunaligned_copy:
	/* align until dst is 32bit-word-aligned */
	extru	dst,31,2,t1
	cmpib,=,n	0,t1,.Lcopy_dstaligned
20:	ldb	0(srcspc,src),t1
	ldo	1(src),src
21:	stb,ma	t1,1(dstspc,dst)
	b	.Lunaligned_copy
	ldo	-1(len),len

	ASM_EXCEPTIONTABLE_ENTRY(20b,.Lcopy_done)
	ASM_EXCEPTIONTABLE_ENTRY(21b,.Lcopy_done)

.Lcopy_dstaligned:

	/* store src, dst and len in safe place */
	copy	src,save_src
	copy	dst,save_dst
	copy	len,save_len

	/* len now needs give number of words to copy */
	SHRREG	len,2,len

	/*
	 * Copy from a not-aligned src to an aligned dst using shifts.
	 * Handles 4 words per loop.
	 */

	depw,z src,28,2,t0
	subi 32,t0,t0
	mtsar t0
	extru len,31,2,t0
	cmpib,= 2,t0,.Lcase2
	/* Make src aligned by rounding it down.  */
	depi 0,31,2,src

	cmpiclr,<> 3,t0,%r0
	b,n .Lcase3
	cmpiclr,<> 1,t0,%r0
	b,n .Lcase1
.Lcase0:
	cmpb,COND(=) %r0,len,.Lcda_finish
	nop

1:	ldw,ma 4(srcspc,src), a3
	ASM_EXCEPTIONTABLE_ENTRY(1b,.Lcda_rdfault)
1:	ldw,ma 4(srcspc,src), a0
	ASM_EXCEPTIONTABLE_ENTRY(1b,.Lcda_rdfault)
	b,n .Ldo3
.Lcase1:
1:	ldw,ma 4(srcspc,src), a2
	ASM_EXCEPTIONTABLE_ENTRY(1b,.Lcda_rdfault)
1:	ldw,ma 4(srcspc,src), a3
	ASM_EXCEPTIONTABLE_ENTRY(1b,.Lcda_rdfault)
	ldo -1(len),len
	cmpb,COND(=),n %r0,len,.Ldo0
.Ldo4:
1:	ldw,ma 4(srcspc,src), a0
	ASM_EXCEPTIONTABLE_ENTRY(1b,.Lcda_rdfault)
	shrpw a2, a3, %sar, t0
1:	stw,ma t0, 4(dstspc,dst)
	ASM_EXCEPTIONTABLE_ENTRY(1b,.Lcopy_done)
.Ldo3:
1:	ldw,ma 4(srcspc,src), a1
	ASM_EXCEPTIONTABLE_ENTRY(1b,.Lcda_rdfault)
	shrpw a3, a0, %sar, t0
1:	stw,ma t0, 4(dstspc,dst)
	ASM_EXCEPTIONTABLE_ENTRY(1b,.Lcopy_done)
.Ldo2:
1:	ldw,ma 4(srcspc,src), a2
	ASM_EXCEPTIONTABLE_ENTRY(1b,.Lcda_rdfault)
	shrpw a0, a1, %sar, t0
1:	stw,ma t0, 4(dstspc,dst)
	ASM_EXCEPTIONTABLE_ENTRY(1b,.Lcopy_done)
.Ldo1:
1:	ldw,ma 4(srcspc,src), a3
	ASM_EXCEPTIONTABLE_ENTRY(1b,.Lcda_rdfault)
	shrpw a1, a2, %sar, t0
1:	stw,ma t0, 4(dstspc,dst)
	ASM_EXCEPTIONTABLE_ENTRY(1b,.Lcopy_done)
	ldo -4(len),len
	cmpb,COND(<>) %r0,len,.Ldo4
	nop
.Ldo0:
	shrpw a2, a3, %sar, t0
1:	stw,ma t0, 4(dstspc,dst)
	ASM_EXCEPTIONTABLE_ENTRY(1b,.Lcopy_done)

.Lcda_rdfault:
.Lcda_finish:
	/* calculate new src, dst and len and jump to byte-copy loop */
	sub	dst,save_dst,t0
	add	save_src,t0,src
	b	.Lbyte_loop
	sub	save_len,t0,len

.Lcase3:
1:	ldw,ma 4(srcspc,src), a0
	ASM_EXCEPTIONTABLE_ENTRY(1b,.Lcda_rdfault)
1:	ldw,ma 4(srcspc,src), a1
	ASM_EXCEPTIONTABLE_ENTRY(1b,.Lcda_rdfault)
	b .Ldo2
	ldo 1(len),len
.Lcase2:
1:	ldw,ma 4(srcspc,src), a1
	ASM_EXCEPTIONTABLE_ENTRY(1b,.Lcda_rdfault)
1:	ldw,ma 4(srcspc,src), a2
	ASM_EXCEPTIONTABLE_ENTRY(1b,.Lcda_rdfault)
	b .Ldo1
	ldo 2(len),len


	/* fault exception fixup handlers: */
#ifdef CONFIG_64BIT
.Lcopy16_fault:
	b	.Lcopy_done
10:	std,ma	t1,8(dstspc,dst)
	ASM_EXCEPTIONTABLE_ENTRY(10b,.Lcopy_done)
#endif

.Lcopy8_fault:
	b	.Lcopy_done
10:	stw,ma	t1,4(dstspc,dst)
	ASM_EXCEPTIONTABLE_ENTRY(10b,.Lcopy_done)
ENDPROC_CFI(pa_memcpy)

	.end
