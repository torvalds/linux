/*
 * "memcpy" implementation of SuperH
 *
 * Copyright (C) 1999  Niibe Yutaka
 * Copyright (c) 2002  STMicroelectronics Ltd
 *   Modified from memcpy.S and micro-optimised for SH4
 *   Stuart Menefy (stuart.menefy@st.com)
 *
 */
#include <linux/linkage.h>
#include <linux/config.h>

/*
 * void *memcpy(void *dst, const void *src, size_t n);
 *
 * It is assumed that there is no overlap between src and dst.
 * If there is an overlap, then the results are undefined.
 */

	!
	!	GHIJ KLMN OPQR -->  ...G HIJK LMNO PQR.
	!

	! Size is 16 or greater, and may have trailing bytes

	.balign	32
.Lcase1:
	! Read a long word and write a long word at once
	! At the start of each iteration, r7 contains last long load
	add	#-1,r5		!  79 EX
	mov	r4,r2		!   5 MT (0 cycles latency)

	mov.l	@(r0,r5),r7	!  21 LS (2 cycles latency)
	add	#-4,r5		!  50 EX

	add	#7,r2		!  79 EX
	!
#ifdef CONFIG_CPU_LITTLE_ENDIAN
	! 6 cycles, 4 bytes per iteration
3:	mov.l	@(r0,r5),r1	!  21 LS (latency=2)	! NMLK
	mov	r7, r3		!   5 MT (latency=0)	! RQPO

	cmp/hi	r2,r0		!  57 MT
	shll16	r3		! 103 EX

	mov	r1,r6		!   5 MT (latency=0)
	shll8	r3		! 102 EX		! Oxxx

	shlr8	r6		! 106 EX		! xNML
	mov	r1, r7		!   5 MT (latency=0)

	or	r6,r3		!  82 EX		! ONML
	bt/s	3b		! 109 BR

	 mov.l	r3,@-r0		!  30 LS
#else
3:	mov.l	@(r0,r5),r1	!  21 LS (latency=2)	! KLMN
	mov	r7,r3		!   5 MT (latency=0)	! OPQR

	cmp/hi	r2,r0		!  57 MT
	shlr16	r3		! 107 EX

	shlr8	r3		! 106 EX		! xxxO
	mov	r1,r6		!   5 MT (latency=0)

	shll8	r6		! 102 EX		! LMNx
	mov	r1,r7		!   5 MT (latency=0)

	or	r6,r3		!  82 EX		! LMNO
	bt/s	3b		! 109 BR

	 mov.l	r3,@-r0		!  30 LS
#endif
	! Finally, copy a byte at once, if necessary

	add	#4,r5		!  50 EX
	cmp/eq	r4,r0		!  54 MT

	add	#-6,r2		!  50 EX
	bt	9f		! 109 BR

8:	cmp/hi	r2,r0		!  57 MT
	mov.b	@(r0,r5),r1	!  20 LS (latency=2)

	bt/s	8b		! 109 BR

	 mov.b	r1,@-r0		!  29 LS

9:	rts
	 nop


	!
	!	GHIJ KLMN OPQR -->  .GHI JKLM NOPQ R...
	!

	! Size is 16 or greater, and may have trailing bytes

	.balign	32
.Lcase3:
	! Read a long word and write a long word at once
	! At the start of each iteration, r7 contains last long load
	add	#-3,r5		! 79 EX
	mov	r4,r2		!  5 MT (0 cycles latency)

	mov.l	@(r0,r5),r7	! 21 LS (2 cycles latency)
	add	#-4,r5		! 50 EX

	add	#7,r2		!  79 EX
	!
#ifdef CONFIG_CPU_LITTLE_ENDIAN
	! 6 cycles, 4 bytes per iteration
3:	mov.l	@(r0,r5),r1	!  21 LS (latency=2)	! NMLK
	mov	r7, r3		!   5 MT (latency=0)	! RQPO

	cmp/hi	r2,r0		!  57 MT
	shll8	r3		! 102 EX		! QPOx

	mov	r1,r6		!   5 MT (latency=0)
	shlr16	r6		! 107 EX

	shlr8	r6		! 106 EX		! xxxN
	mov	r1, r7		!   5 MT (latency=0)

	or	r6,r3		!  82 EX		! QPON
	bt/s	3b		! 109 BR

	 mov.l	r3,@-r0		!  30 LS
#else
3:	mov	r1,r3		! OPQR
	shlr8	r3		! xOPQ
	mov.l	@(r0,r5),r1	! KLMN
	mov	r1,r6
	shll16	r6
	shll8	r6		! Nxxx
	or	r6,r3		! NOPQ
	cmp/hi	r2,r0
	bt/s	3b
	 mov.l	r3,@-r0
#endif

	! Finally, copy a byte at once, if necessary

	add	#6,r5		!  50 EX
	cmp/eq	r4,r0		!  54 MT

	add	#-6,r2		!  50 EX
	bt	9f		! 109 BR

8:	cmp/hi	r2,r0		!  57 MT
	mov.b	@(r0,r5),r1	!  20 LS (latency=2)

	bt/s	8b		! 109 BR

	 mov.b	r1,@-r0		!  29 LS

9:	rts
	 nop

ENTRY(memcpy)

	! Calculate the invariants which will be used in the remainder
	! of the code:
	!
	!      r4   -->  [ ...  ] DST             [ ...  ] SRC
	!	         [ ...  ]                 [ ...  ]
	!	           :                        :
	!      r0   -->  [ ...  ]       r0+r5 --> [ ...  ]
	!
	!

	! Short circuit the common case of src, dst and len being 32 bit aligned
	! and test for zero length move

	mov	r6, r0		!   5 MT (0 cycle latency)
	or	r4, r0		!  82 EX

	or	r5, r0		!  82 EX
	tst	r6, r6		!  86 MT

	bt/s	99f		! 111 BR		(zero len)
	 tst	#3, r0		!  87 MT

	mov	r4, r0		!   5 MT (0 cycle latency)
	add	r6, r0		!  49 EX

	mov	#16, r1		!   6 EX
	bt/s	.Lcase00	! 111 BR		(aligned)

	 sub	r4, r5		!  75 EX

	! Arguments are not nicely long word aligned or zero len.
	! Check for small copies, and if so do a simple byte at a time copy.
	!
	! Deciding on an exact value of 'small' is not easy, as the point at which
	! using the optimised routines become worthwhile varies (these are the
	! cycle counts for differnet sizes using byte-at-a-time vs. optimised):
	!	size	byte-at-time	long	word	byte
	!	16	42		39-40	46-50	50-55
	!	24	58		43-44	54-58	62-67
	!	36	82		49-50	66-70	80-85
	! However the penalty for getting it 'wrong' is much higher for long word
	! aligned data (and this is more common), so use a value of 16.

	cmp/gt	r6,r1		!  56 MT

	add	#-1,r5		!  50 EX
	bf/s	6f		! 108 BR		(not small)

	 mov	r5, r3		!   5 MT (latency=0)
	shlr	r6		! 104 EX

	mov.b	@(r0,r5),r1	!  20 LS (latency=2)
	bf/s	4f		! 111 BR

	 add	#-1,r3		!  50 EX
	tst	r6, r6		!  86 MT

	bt/s	98f		! 110 BR
	 mov.b	r1,@-r0		!  29 LS

	! 4 cycles, 2 bytes per iteration
3:	mov.b	@(r0,r5),r1	!  20 LS (latency=2)

4:	mov.b	@(r0,r3),r2	!  20 LS (latency=2)
	dt	r6		!  67 EX

	mov.b	r1,@-r0		!  29 LS
	bf/s	3b		! 111 BR

	 mov.b	r2,@-r0		!  29 LS
98:
	rts
	 nop

99:	rts
	 mov	r4, r0

	! Size is not small, so its worthwhile looking for optimisations.
	! First align destination to a long word boundary.
	!
	! r5 = normal value -1

6:	tst	#3, r0		!  87 MT
        mov	#3, r3		!   6 EX

	bt/s	2f		! 111 BR
	 and	r0,r3		!  78 EX

	! 3 cycles, 1 byte per iteration
1:	dt	r3		!  67 EX
	mov.b	@(r0,r5),r1	!  19 LS (latency=2)

	add	#-1, r6		!  79 EX
	bf/s	1b		! 109 BR

	 mov.b	r1,@-r0		!  28 LS

2:	add	#1, r5		!  79 EX

	! Now select the appropriate bulk transfer code based on relative
	! alignment of src and dst.

	mov	r0, r3		!   5 MT (latency=0)

	mov	r5, r0		!   5 MT (latency=0)
	tst	#1, r0		!  87 MT

	bf/s	1f		! 111 BR
	 mov	#64, r7		!   6 EX

	! bit 0 clear

	cmp/ge	r7, r6		!  55 MT

	bt/s	2f		! 111 BR
	 tst	#2, r0		!  87 MT

	! small
	bt/s	.Lcase0
	 mov	r3, r0

	bra	.Lcase2
	 nop

	! big
2:	bt/s	.Lcase0b
	 mov	r3, r0

	bra	.Lcase2b
	 nop

	! bit 0 set
1:	tst	#2, r0		! 87 MT

	bt/s	.Lcase1
	 mov	r3, r0

	bra	.Lcase3
	 nop


	!
	!	GHIJ KLMN OPQR -->  GHIJ KLMN OPQR
	!

	! src, dst and size are all long word aligned
	! size is non-zero

	.balign	32
.Lcase00:
	mov	#64, r1		!   6 EX
	mov	r5, r3		!   5 MT (latency=0)

	cmp/gt	r6, r1		!  56 MT
	add	#-4, r5		!  50 EX

	bf	.Lcase00b	! 108 BR		(big loop)
	shlr2	r6		! 105 EX

	shlr	r6		! 104 EX
	mov.l	@(r0, r5), r1	!  21 LS (latency=2)

	bf/s	4f		! 111 BR
	 add	#-8, r3		!  50 EX

	tst	r6, r6		!  86 MT
	bt/s	5f		! 110 BR

	 mov.l	r1,@-r0		!  30 LS

	! 4 cycles, 2 long words per iteration
3:	mov.l	@(r0, r5), r1	!  21 LS (latency=2)

4:	mov.l	@(r0, r3), r2	!  21 LS (latency=2)
	dt	r6		!  67 EX

	mov.l	r1, @-r0	!  30 LS
	bf/s	3b		! 109 BR

	 mov.l	r2, @-r0	!  30 LS

5:	rts
	 nop


	! Size is 16 or greater and less than 64, but may have trailing bytes

	.balign	32
.Lcase0:
	add	#-4, r5		!  50 EX
	mov	r4, r7		!   5 MT (latency=0)

	mov.l	@(r0, r5), r1	!  21 LS (latency=2)
	mov	#4, r2		!   6 EX

	add	#11, r7		!  50 EX
	tst	r2, r6		!  86 MT

	mov	r5, r3		!   5 MT (latency=0)
	bt/s	4f		! 111 BR

	 add	#-4, r3		!  50 EX
	mov.l	r1,@-r0		!  30 LS

	! 4 cycles, 2 long words per iteration
3:	mov.l	@(r0, r5), r1	!  21 LS (latency=2)

4:	mov.l	@(r0, r3), r2	!  21 LS (latency=2)
	cmp/hi	r7, r0

	mov.l	r1, @-r0	!  30 LS
	bt/s	3b		! 109 BR

	 mov.l	r2, @-r0	!  30 LS

	! Copy the final 0-3 bytes

	add	#3,r5		!  50 EX

	cmp/eq	r0, r4		!  54 MT
	add	#-10, r7	!  50 EX

	bt	9f		! 110 BR

	! 3 cycles, 1 byte per iteration
1:	mov.b	@(r0,r5),r1	!  19 LS
	cmp/hi	r7,r0		!  57 MT

	bt/s	1b		! 111 BR
	 mov.b	r1,@-r0		!  28 LS

9:	rts
	 nop

	! Size is at least 64 bytes, so will be going round the big loop at least once.
	!
	!   r2 = rounded up r4
	!   r3 = rounded down r0

	.balign	32
.Lcase0b:
	add	#-4, r5		!  50 EX

.Lcase00b:
	mov	r0, r3		!   5 MT (latency=0)
	mov	#(~0x1f), r1	!   6 EX

	and	r1, r3		!  78 EX
	mov	r4, r2		!   5 MT (latency=0)

	cmp/eq	r3, r0		!  54 MT
	add	#0x1f, r2	!  50 EX

	bt/s	1f		! 110 BR
	 and	r1, r2		!  78 EX

	! copy initial words until cache line aligned

	mov.l	@(r0, r5), r1	!  21 LS (latency=2)
	tst	#4, r0		!  87 MT

	mov	r5, r6		!   5 MT (latency=0)
	add	#-4, r6		!  50 EX

	bt/s	4f		! 111 BR
	 add	#8, r3		!  50 EX

	tst	#0x18, r0	!  87 MT

	bt/s	1f		! 109 BR
	 mov.l	r1,@-r0		!  30 LS

	! 4 cycles, 2 long words per iteration
3:	mov.l	@(r0, r5), r1	!  21 LS (latency=2)

4:	mov.l	@(r0, r6), r7	!  21 LS (latency=2)
	cmp/eq	r3, r0		!  54 MT

	mov.l	r1, @-r0	!  30 LS
	bf/s	3b		! 109 BR

	 mov.l	r7, @-r0	!  30 LS

	! Copy the cache line aligned blocks
	!
	! In use: r0, r2, r4, r5
	! Scratch: r1, r3, r6, r7
	!
	! We could do this with the four scratch registers, but if src
	! and dest hit the same cache line, this will thrash, so make
	! use of additional registers.
	!
	! We also need r0 as a temporary (for movca), so 'undo' the invariant:
	!   r5:	 src (was r0+r5)
	!   r1:	 dest (was r0)
	! this can be reversed at the end, so we don't need to save any extra
	! state.
	!
1:	mov.l	r8, @-r15	!  30 LS
	add	r0, r5		!  49 EX

	mov.l	r9, @-r15	!  30 LS
	mov	r0, r1		!   5 MT (latency=0)

	mov.l	r10, @-r15	!  30 LS
	add	#-0x1c, r5	!  50 EX

	mov.l	r11, @-r15	!  30 LS

	! 16 cycles, 32 bytes per iteration
2:	mov.l	@(0x00,r5),r0	! 18 LS (latency=2)
	add	#-0x20, r1	! 50 EX
	mov.l	@(0x04,r5),r3	! 18 LS (latency=2)
	mov.l	@(0x08,r5),r6	! 18 LS (latency=2)
	mov.l	@(0x0c,r5),r7	! 18 LS (latency=2)
	mov.l	@(0x10,r5),r8	! 18 LS (latency=2)
	mov.l	@(0x14,r5),r9	! 18 LS (latency=2)
	mov.l	@(0x18,r5),r10	! 18 LS (latency=2)
	mov.l	@(0x1c,r5),r11	! 18 LS (latency=2)
	movca.l	r0,@r1		! 40 LS (latency=3-7)
	mov.l	r3,@(0x04,r1)	! 33 LS
	mov.l	r6,@(0x08,r1)	! 33 LS
	mov.l	r7,@(0x0c,r1)	! 33 LS

	mov.l	r8,@(0x10,r1)	! 33 LS
	add	#-0x20, r5	! 50 EX

	mov.l	r9,@(0x14,r1)	! 33 LS
	cmp/eq	r2,r1		! 54 MT

	mov.l	r10,@(0x18,r1)	!  33 LS
	bf/s	2b		! 109 BR

	 mov.l	r11,@(0x1c,r1)	!  33 LS

	mov	r1, r0		!   5 MT (latency=0)

	mov.l	@r15+, r11	!  15 LS
	sub	r1, r5		!  75 EX

	mov.l	@r15+, r10	!  15 LS
	cmp/eq	r4, r0		!  54 MT

	bf/s	1f		! 109 BR
	 mov.l	 @r15+, r9	!  15 LS

	rts
1:	 mov.l	@r15+, r8	!  15 LS
	sub	r4, r1		!  75 EX		(len remaining)

	! number of trailing bytes is non-zero
	!
	! invariants restored (r5 already decremented by 4)
	! also r1=num bytes remaining

	mov	#4, r2		!   6 EX
	mov	r4, r7		!   5 MT (latency=0)

	add	#0x1c, r5	!  50 EX		(back to -4)
	cmp/hs	r2, r1		!  58 MT

	bf/s	5f		! 108 BR
	 add	 #11, r7	!  50 EX

	mov.l	@(r0, r5), r6	!  21 LS (latency=2)
	tst	r2, r1		!  86 MT

	mov	r5, r3		!   5 MT (latency=0)
	bt/s	4f		! 111 BR

	 add	#-4, r3		!  50 EX
	cmp/hs	r2, r1		!  58 MT

	bt/s	5f		! 111 BR
	 mov.l	r6,@-r0		!  30 LS

	! 4 cycles, 2 long words per iteration
3:	mov.l	@(r0, r5), r6	!  21 LS (latency=2)

4:	mov.l	@(r0, r3), r2	!  21 LS (latency=2)
	cmp/hi	r7, r0

	mov.l	r6, @-r0	!  30 LS
	bt/s	3b		! 109 BR

	 mov.l	r2, @-r0	!  30 LS

	! Copy the final 0-3 bytes

5:	cmp/eq	r0, r4		!  54 MT
	add	#-10, r7	!  50 EX

	bt	9f		! 110 BR
	add	#3,r5		!  50 EX

	! 3 cycles, 1 byte per iteration
1:	mov.b	@(r0,r5),r1	!  19 LS
	cmp/hi	r7,r0		!  57 MT

	bt/s	1b		! 111 BR
	 mov.b	r1,@-r0		!  28 LS

9:	rts
	 nop

	!
	!	GHIJ KLMN OPQR -->  ..GH IJKL MNOP QR..
	!

	.balign	32
.Lcase2:
	! Size is 16 or greater and less then 64, but may have trailing bytes

2:	mov	r5, r6		!   5 MT (latency=0)
	add	#-2,r5		!  50 EX

	mov	r4,r2		!   5 MT (latency=0)
	add	#-4,r6		!  50 EX

	add	#7,r2		!  50 EX
3:	mov.w	@(r0,r5),r1	!  20 LS (latency=2)

	mov.w	@(r0,r6),r3	!  20 LS (latency=2)
	cmp/hi	r2,r0		!  57 MT

	mov.w	r1,@-r0		!  29 LS
	bt/s	3b		! 111 BR

	 mov.w	r3,@-r0		!  29 LS

	bra	10f
	 nop


	.balign	32
.Lcase2b:
	! Size is at least 64 bytes, so will be going round the big loop at least once.
	!
	!   r2 = rounded up r4
	!   r3 = rounded down r0

	mov	r0, r3		!   5 MT (latency=0)
	mov	#(~0x1f), r1	!   6 EX

	and	r1, r3		!  78 EX
	mov	r4, r2		!   5 MT (latency=0)

	cmp/eq	r3, r0		!  54 MT
	add	#0x1f, r2	!  50 EX

	add	#-2, r5		!  50 EX
	bt/s	1f		! 110 BR
	 and	r1, r2		!  78 EX

	! Copy a short word one at a time until we are cache line aligned
	!   Normal values: r0, r2, r3, r4
	!   Unused: r1, r6, r7
	!   Mod: r5 (=r5-2)
	!
	add	#2, r3		!  50 EX

2:	mov.w	@(r0,r5),r1	!  20 LS (latency=2)
	cmp/eq	r3,r0		!  54 MT

	bf/s	2b		! 111 BR

	 mov.w	r1,@-r0		!  29 LS

	! Copy the cache line aligned blocks
	!
	! In use: r0, r2, r4, r5 (=r5-2)
	! Scratch: r1, r3, r6, r7
	!
	! We could do this with the four scratch registers, but if src
	! and dest hit the same cache line, this will thrash, so make
	! use of additional registers.
	!
	! We also need r0 as a temporary (for movca), so 'undo' the invariant:
	!   r5:	 src (was r0+r5)
	!   r1:	 dest (was r0)
	! this can be reversed at the end, so we don't need to save any extra
	! state.
	!
1:	mov.l	r8, @-r15	!  30 LS
	add	r0, r5		!  49 EX

	mov.l	r9, @-r15	!  30 LS
	mov	r0, r1		!   5 MT (latency=0)

	mov.l	r10, @-r15	!  30 LS
	add	#-0x1e, r5	!  50 EX

	mov.l	r11, @-r15	!  30 LS

	mov.l	r12, @-r15	!  30 LS

	! 17 cycles, 32 bytes per iteration
#ifdef CONFIG_CPU_LITTLE_ENDIAN
2:	mov.w	@r5+, r0	!  14 LS (latency=2)		..JI
	add	#-0x20, r1	!  50 EX

	mov.l	@r5+, r3	!  15 LS (latency=2)		NMLK

	mov.l	@r5+, r6	!  15 LS (latency=2)		RQPO
	shll16	r0		! 103 EX			JI..

	mov.l	@r5+, r7	!  15 LS (latency=2)
	xtrct	r3, r0		!  48 EX			LKJI

	mov.l	@r5+, r8	!  15 LS (latency=2)
	xtrct	r6, r3		!  48 EX			PONM

	mov.l	@r5+, r9	!  15 LS (latency=2)
	xtrct	r7, r6		!  48 EX

	mov.l	@r5+, r10	!  15 LS (latency=2)
	xtrct	r8, r7		!  48 EX

	mov.l	@r5+, r11	!  15 LS (latency=2)
	xtrct	r9, r8		!  48 EX

	mov.w	@r5+, r12	!  15 LS (latency=2)
	xtrct	r10, r9		!  48 EX

	movca.l	r0,@r1		!  40 LS (latency=3-7)
	xtrct	r11, r10	!  48 EX

	mov.l	r3, @(0x04,r1)	!  33 LS
	xtrct	r12, r11	!  48 EX

	mov.l	r6, @(0x08,r1)	!  33 LS

	mov.l	r7, @(0x0c,r1)	!  33 LS

	mov.l	r8, @(0x10,r1)	!  33 LS
	add	#-0x40, r5	!  50 EX

	mov.l	r9, @(0x14,r1)	!  33 LS
	cmp/eq	r2,r1		!  54 MT

	mov.l	r10, @(0x18,r1)	!  33 LS
	bf/s	2b		! 109 BR

	 mov.l	r11, @(0x1c,r1)	!  33 LS
#else
2:	mov.w	@(0x1e,r5), r0	!  17 LS (latency=2)
	add	#-2, r5		!  50 EX

	mov.l	@(0x1c,r5), r3	!  18 LS (latency=2)
	add	#-4, r1		!  50 EX

	mov.l	@(0x18,r5), r6	!  18 LS (latency=2)
	shll16	r0		! 103 EX

	mov.l	@(0x14,r5), r7	!  18 LS (latency=2)
	xtrct	r3, r0		!  48 EX

	mov.l	@(0x10,r5), r8	!  18 LS (latency=2)
	xtrct	r6, r3		!  48 EX

	mov.l	@(0x0c,r5), r9	!  18 LS (latency=2)
	xtrct	r7, r6		!  48 EX

	mov.l	@(0x08,r5), r10	!  18 LS (latency=2)
	xtrct	r8, r7		!  48 EX

	mov.l	@(0x04,r5), r11	!  18 LS (latency=2)
	xtrct	r9, r8		!  48 EX

	mov.w	@(0x02,r5), r12	!  18 LS (latency=2)
	xtrct	r10, r9		!  48 EX

	movca.l	r0,@r1		!  40 LS (latency=3-7)
	add	#-0x1c, r1	!  50 EX

	mov.l	r3, @(0x1c,r1)	!  33 LS
	xtrct	r11, r10	!  48 EX

	mov.l	r6, @(0x18,r1)	!  33 LS
	xtrct	r12, r11	!  48 EX

	mov.l	r7, @(0x14,r1)	!  33 LS

	mov.l	r8, @(0x10,r1)	!  33 LS
	add	#-0x3e, r5	!  50 EX

	mov.l	r9, @(0x0c,r1)	!  33 LS
	cmp/eq	r2,r1		!  54 MT

	mov.l	r10, @(0x08,r1)	!  33 LS
	bf/s	2b		! 109 BR

	 mov.l	r11, @(0x04,r1)	!  33 LS
#endif

	mov.l	@r15+, r12
	mov	r1, r0		!   5 MT (latency=0)

	mov.l	@r15+, r11	!  15 LS
	sub	r1, r5		!  75 EX

	mov.l	@r15+, r10	!  15 LS
	cmp/eq	r4, r0		!  54 MT

	bf/s	1f		! 109 BR
	 mov.l	 @r15+, r9	!  15 LS

	rts
1:	 mov.l	@r15+, r8	!  15 LS

	add	#0x1e, r5	!  50 EX

	! Finish off a short word at a time
	! r5 must be invariant - 2
10:	mov	r4,r2		!   5 MT (latency=0)
	add	#1,r2		!  50 EX

	cmp/hi	r2, r0		!  57 MT
	bf/s	1f		! 109 BR

	 add	#2, r2		!  50 EX

3:	mov.w	@(r0,r5),r1	!  20 LS
	cmp/hi	r2,r0		!  57 MT

	bt/s	3b		! 109 BR

	 mov.w	r1,@-r0		!  29 LS
1:

	!
	! Finally, copy the last byte if necessary
	cmp/eq	r4,r0		!  54 MT
	bt/s	9b
	 add	#1,r5
	mov.b	@(r0,r5),r1
	rts
	 mov.b	r1,@-r0

