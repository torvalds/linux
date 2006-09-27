/* $Id: checksum.S,v 1.10 2001/07/06 13:11:32 gniibe Exp $
 *
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		IP/TCP/UDP checksumming routines
 *
 * Authors:	Jorge Cwik, <jorge@laser.satlink.net>
 *		Arnt Gulbrandsen, <agulbra@nvg.unit.no>
 *		Tom May, <ftom@netcom.com>
 *              Pentium Pro/II routines:
 *              Alexander Kjeldaas <astor@guardian.no>
 *              Finn Arne Gangstad <finnag@guardian.no>
 *		Lots of code moved from tcp.c and ip.c; see those files
 *		for more names.
 *
 * Changes:     Ingo Molnar, converted csum_partial_copy() to 2.1 exception
 *			     handling.
 *		Andi Kleen,  add zeroing on error
 *                   converted to pure assembler
 *
 * SuperH version:  Copyright (C) 1999  Niibe Yutaka
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */

#include <asm/errno.h>
#include <linux/linkage.h>

/*
 * computes a partial checksum, e.g. for TCP/UDP fragments
 */

/*	
 * unsigned int csum_partial(const unsigned char *buf, int len,
 *                           unsigned int sum);
 */

.text
ENTRY(csum_partial)
	  /*
	   * Experiments with Ethernet and SLIP connections show that buff
	   * is aligned on either a 2-byte or 4-byte boundary.  We get at
	   * least a twofold speedup on 486 and Pentium if it is 4-byte aligned.
	   * Fortunately, it is easy to convert 2-byte alignment to 4-byte
	   * alignment for the unrolled loop.
	   */
	mov	r5, r1
	mov	r4, r0
	tst	#2, r0		! Check alignment.
	bt	2f		! Jump if alignment is ok.
	!
	add	#-2, r5		! Alignment uses up two bytes.
	cmp/pz	r5		!
	bt/s	1f		! Jump if we had at least two bytes.
	 clrt
	bra	6f
	 add	#2, r5		! r5 was < 2.  Deal with it.
1:
	mov	r5, r1		! Save new len for later use.
	mov.w	@r4+, r0
	extu.w	r0, r0
	addc	r0, r6
	bf	2f
	add	#1, r6
2:
	mov	#-5, r0
	shld	r0, r5
	tst	r5, r5
	bt/s	4f		! if it's =0, go to 4f
	 clrt
	.align	2
3:
	mov.l	@r4+, r0
	mov.l	@r4+, r2
	mov.l	@r4+, r3
	addc	r0, r6
	mov.l	@r4+, r0
	addc	r2, r6
	mov.l	@r4+, r2
	addc	r3, r6
	mov.l	@r4+, r3
	addc	r0, r6
	mov.l	@r4+, r0
	addc	r2, r6
	mov.l	@r4+, r2
	addc	r3, r6
	addc	r0, r6
	addc	r2, r6
	movt	r0
	dt	r5
	bf/s	3b
	 cmp/eq	#1, r0
	! here, we know r5==0
	addc	r5, r6			! add carry to r6
4:
	mov	r1, r0
	and	#0x1c, r0
	tst	r0, r0
	bt/s	6f
	 mov	r0, r5
	shlr2	r5
	mov	#0, r2
5:
	addc	r2, r6
	mov.l	@r4+, r2
	movt	r0
	dt	r5
	bf/s	5b
	 cmp/eq	#1, r0
	addc	r2, r6
	addc	r5, r6		! r5==0 here, so it means add carry-bit
6:
	mov	r1, r5
	mov	#3, r0
	and	r0, r5
	tst	r5, r5
	bt	9f		! if it's =0 go to 9f
	mov	#2, r1
	cmp/hs  r1, r5
	bf	7f
	mov.w	@r4+, r0
	extu.w	r0, r0
	cmp/eq	r1, r5
	bt/s	8f
	 clrt
	shll16	r0
	addc	r0, r6
7:
	mov.b	@r4+, r0
	extu.b	r0, r0
#ifndef	__LITTLE_ENDIAN__
	shll8	r0
#endif
8:
	addc	r0, r6
	mov	#0, r0
	addc	r0, r6 
9:
	rts
	 mov	r6, r0

/*
unsigned int csum_partial_copy_generic (const char *src, char *dst, int len, 
					int sum, int *src_err_ptr, int *dst_err_ptr)
 */ 

/*
 * Copy from ds while checksumming, otherwise like csum_partial
 *
 * The macros SRC and DST specify the type of access for the instruction.
 * thus we can call a custom exception handler for all access types.
 *
 * FIXME: could someone double-check whether I haven't mixed up some SRC and
 *	  DST definitions? It's damn hard to trigger all cases.  I hope I got
 *	  them all but there's no guarantee.
 */

#define SRC(...)			\
	9999: __VA_ARGS__ ;		\
	.section __ex_table, "a";	\
	.long 9999b, 6001f	;	\
	.previous

#define DST(...)			\
	9999: __VA_ARGS__ ;		\
	.section __ex_table, "a";	\
	.long 9999b, 6002f	;	\
	.previous

!
! r4:	const char *SRC
! r5:	char *DST
! r6:	int LEN
! r7:	int SUM
!
! on stack:
! int *SRC_ERR_PTR
! int *DST_ERR_PTR
!
ENTRY(csum_partial_copy_generic)
	mov.l	r5,@-r15
	mov.l	r6,@-r15

	mov	#3,r0		! Check src and dest are equally aligned
	mov	r4,r1
	and	r0,r1
	and	r5,r0
	cmp/eq	r1,r0
	bf	3f		! Different alignments, use slow version
	tst	#1,r0		! Check dest word aligned
	bf	3f		! If not, do it the slow way

	mov	#2,r0
	tst	r0,r5		! Check dest alignment. 
	bt	2f		! Jump if alignment is ok.
	add	#-2,r6		! Alignment uses up two bytes.
	cmp/pz	r6		! Jump if we had at least two bytes.
	bt/s	1f
	 clrt
	add	#2,r6		! r6 was < 2.	Deal with it.
	bra	4f
	 mov	r6,r2

3:	! Handle different src and dest alignments.
	! This is not common, so simple byte by byte copy will do.
	mov	r6,r2
	shlr	r6
	tst	r6,r6
	bt	4f
	clrt
	.align	2
5:
SRC(	mov.b	@r4+,r1 	)
SRC(	mov.b	@r4+,r0		)
	extu.b	r1,r1
DST(	mov.b	r1,@r5		)
DST(	mov.b	r0,@(1,r5)	)
	extu.b	r0,r0
	add	#2,r5

#ifdef	__LITTLE_ENDIAN__
	shll8	r0
#else
	shll8	r1
#endif
	or	r1,r0

	addc	r0,r7
	movt	r0
	dt	r6
	bf/s	5b
	 cmp/eq	#1,r0
	mov	#0,r0
	addc	r0, r7

	mov	r2, r0
	tst	#1, r0
	bt	7f
	bra	5f
	 clrt

	! src and dest equally aligned, but to a two byte boundary.
	! Handle first two bytes as a special case
	.align	2
1:	
SRC(	mov.w	@r4+,r0		)
DST(	mov.w	r0,@r5		)
	add	#2,r5
	extu.w	r0,r0
	addc	r0,r7
	mov	#0,r0
	addc	r0,r7
2:
	mov	r6,r2
	mov	#-5,r0
	shld	r0,r6
	tst	r6,r6
	bt/s	2f
	 clrt
	.align	2
1:	
SRC(	mov.l	@r4+,r0		)
SRC(	mov.l	@r4+,r1		)
	addc	r0,r7
DST(	mov.l	r0,@r5		)
DST(	mov.l	r1,@(4,r5)	)
	addc	r1,r7

SRC(	mov.l	@r4+,r0		)
SRC(	mov.l	@r4+,r1		)
	addc	r0,r7
DST(	mov.l	r0,@(8,r5)	)
DST(	mov.l	r1,@(12,r5)	)
	addc	r1,r7

SRC(	mov.l	@r4+,r0 	)
SRC(	mov.l	@r4+,r1		)
	addc	r0,r7
DST(	mov.l	r0,@(16,r5)	)
DST(	mov.l	r1,@(20,r5)	)
	addc	r1,r7

SRC(	mov.l	@r4+,r0		)
SRC(	mov.l	@r4+,r1		)
	addc	r0,r7
DST(	mov.l	r0,@(24,r5)	)
DST(	mov.l	r1,@(28,r5)	)
	addc	r1,r7
	add	#32,r5
	movt	r0
	dt	r6
	bf/s	1b
	 cmp/eq	#1,r0
	mov	#0,r0
	addc	r0,r7

2:	mov	r2,r6
	mov	#0x1c,r0
	and	r0,r6
	cmp/pl	r6
	bf/s	4f
	 clrt
	shlr2	r6
3:	
SRC(	mov.l	@r4+,r0	)
	addc	r0,r7
DST(	mov.l	r0,@r5	)
	add	#4,r5
	movt	r0
	dt	r6
	bf/s	3b
	 cmp/eq	#1,r0
	mov	#0,r0
	addc	r0,r7
4:	mov	r2,r6
	mov	#3,r0
	and	r0,r6
	cmp/pl	r6
	bf	7f
	mov	#2,r1
	cmp/hs	r1,r6
	bf	5f
SRC(	mov.w	@r4+,r0	)
DST(	mov.w	r0,@r5	)
	extu.w	r0,r0
	add	#2,r5
	cmp/eq	r1,r6
	bt/s	6f
	 clrt
	shll16	r0
	addc	r0,r7
5:	
SRC(	mov.b	@r4+,r0	)
DST(	mov.b	r0,@r5	)
	extu.b	r0,r0
#ifndef	__LITTLE_ENDIAN__
	shll8	r0
#endif
6:	addc	r0,r7
	mov	#0,r0
	addc	r0,r7
7:
5000:

# Exception handler:
.section .fixup, "ax"							

6001:
	mov.l	@(8,r15),r0			! src_err_ptr
	mov	#-EFAULT,r1
	mov.l	r1,@r0

	! zero the complete destination - computing the rest
	! is too much work 
	mov.l	@(4,r15),r5		! dst
	mov.l	@r15,r6			! len
	mov	#0,r7
1:	mov.b	r7,@r5
	dt	r6
	bf/s	1b
	 add	#1,r5
	mov.l	8000f,r0
	jmp	@r0
	 nop
	.align	2
8000:	.long	5000b

6002:
	mov.l	@(12,r15),r0			! dst_err_ptr
	mov	#-EFAULT,r1
	mov.l	r1,@r0
	mov.l	8001f,r0
	jmp	@r0
	 nop
	.align	2
8001:	.long	5000b

.previous
	add	#8,r15
	rts
	 mov	r7,r0
