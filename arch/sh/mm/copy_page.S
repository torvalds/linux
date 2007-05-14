/*
 * copy_page, __copy_user_page, __copy_user implementation of SuperH
 *
 * Copyright (C) 2001  Niibe Yutaka & Kaz Kojima
 * Copyright (C) 2002  Toshinobu Sugioka
 * Copyright (C) 2006  Paul Mundt
 */
#include <linux/linkage.h>
#include <asm/page.h>

/*
 * copy_page_slow
 * @to: P1 address
 * @from: P1 address
 *
 * void copy_page_slow(void *to, void *from)
 */

/*
 * r0, r1, r2, r3, r4, r5, r6, r7 --- scratch 
 * r8 --- from + PAGE_SIZE
 * r9 --- not used
 * r10 --- to
 * r11 --- from
 */
ENTRY(copy_page_slow)
	mov.l	r8,@-r15
	mov.l	r10,@-r15
	mov.l	r11,@-r15
	mov	r4,r10
	mov	r5,r11
	mov	r5,r8
	mov.l	.Lpsz,r0
	add	r0,r8
	!
1:	mov.l	@r11+,r0
	mov.l	@r11+,r1
	mov.l	@r11+,r2
	mov.l	@r11+,r3
	mov.l	@r11+,r4
	mov.l	@r11+,r5
	mov.l	@r11+,r6
	mov.l	@r11+,r7
#if defined(CONFIG_CPU_SH3)
	mov.l	r0,@r10
#elif defined(CONFIG_CPU_SH4)
	movca.l	r0,@r10
	mov	r10,r0
#endif
	add	#32,r10
	mov.l	r7,@-r10
	mov.l	r6,@-r10
	mov.l	r5,@-r10
	mov.l	r4,@-r10
	mov.l	r3,@-r10
	mov.l	r2,@-r10
	mov.l	r1,@-r10
#if defined(CONFIG_CPU_SH4)
	ocbwb	@r0
#endif
	cmp/eq	r11,r8
	bf/s	1b
	 add	#28,r10
	!
	mov.l	@r15+,r11
	mov.l	@r15+,r10
	mov.l	@r15+,r8
	rts
	 nop

#if defined(CONFIG_CPU_SH4)
/*
 * __copy_user_page
 * @to: P1 address (with same color)
 * @from: P1 address
 * @orig_to: P1 address
 *
 * void __copy_user_page(void *to, void *from, void *orig_to)
 */

/*
 * r0, r1, r2, r3, r4, r5, r6, r7 --- scratch 
 * r8 --- from + PAGE_SIZE
 * r9 --- orig_to
 * r10 --- to
 * r11 --- from
 */
ENTRY(__copy_user_page)
	mov.l	r8,@-r15
	mov.l	r9,@-r15
	mov.l	r10,@-r15
	mov.l	r11,@-r15
	mov	r4,r10
	mov	r5,r11
	mov	r6,r9
	mov	r5,r8
	mov.l	.Lpsz,r0
	add	r0,r8
	!
1:	ocbi	@r9
	add	#32,r9
	mov.l	@r11+,r0
	mov.l	@r11+,r1
	mov.l	@r11+,r2
	mov.l	@r11+,r3
	mov.l	@r11+,r4
	mov.l	@r11+,r5
	mov.l	@r11+,r6
	mov.l	@r11+,r7
	movca.l	r0,@r10
	mov	r10,r0
	add	#32,r10
	mov.l	r7,@-r10
	mov.l	r6,@-r10
	mov.l	r5,@-r10
	mov.l	r4,@-r10
	mov.l	r3,@-r10
	mov.l	r2,@-r10
	mov.l	r1,@-r10
	ocbwb	@r0
	cmp/eq	r11,r8
	bf/s	1b
	 add	#28,r10
	!
	mov.l	@r15+,r11
	mov.l	@r15+,r10
	mov.l	@r15+,r9
	mov.l	@r15+,r8
	rts
	 nop
#endif
	.align 2
.Lpsz:	.long	PAGE_SIZE
/*
 * __kernel_size_t __copy_user(void *to, const void *from, __kernel_size_t n);
 * Return the number of bytes NOT copied
 */
#define EX(...)			\
	9999: __VA_ARGS__ ;		\
	.section __ex_table, "a";	\
	.long 9999b, 6000f	;	\
	.previous
ENTRY(__copy_user)
	tst	r6,r6		! Check explicitly for zero
	bf	1f
	rts
	 mov	#0,r0		! normal return
1:
	mov.l	r10,@-r15
	mov.l	r9,@-r15
	mov.l	r8,@-r15
	mov	r4,r3
	add	r6,r3		! last destination address
	mov	#12,r0		! Check if small number of bytes
	cmp/gt	r0,r6
	bt	2f
	bra	.L_cleanup_loop
	 nop
2:
	neg	r5,r0		! Calculate bytes needed to align source
	add	#4,r0
	and	#3,r0
	tst	r0,r0
	bt	.L_jump
	mov	r0,r1

.L_loop1:
	! Copy bytes to align source
EX(	mov.b	@r5+,r0		)
	dt	r1
EX(	mov.b	r0,@r4		)
	add	#-1,r6
	bf/s	.L_loop1
	 add	#1,r4

.L_jump:
	mov	r6,r2		! Calculate number of longwords to copy
	shlr2	r2
	tst	r2,r2
	bt	.L_cleanup

	mov	r4,r0		! Jump to appropriate routine
	and	#3,r0
	mov	r0,r1
	shll2	r1
	mova	.L_jump_tbl,r0
	mov.l	@(r0,r1),r1
	jmp	@r1
	 nop

	.align 2
.L_jump_tbl:
	.long	.L_dest00
	.long	.L_dest01
	.long	.L_dest10
	.long	.L_dest11

! Destination = 00

.L_dest00:
	mov	r2,r7
	shlr2	r7
	shlr	r7
	tst	r7,r7
	mov	#7,r0
	bt/s	1f
	 and	r0,r2
	.align 2
2:
EX(	mov.l	@r5+,r0		)
EX(	mov.l	@r5+,r8		)
EX(	mov.l	@r5+,r9		)
EX(	mov.l	@r5+,r10	)
EX(	mov.l	r0,@r4		)
EX(	mov.l	r8,@(4,r4)	)
EX(	mov.l	r9,@(8,r4)	)
EX(	mov.l	r10,@(12,r4)	)
EX(	mov.l	@r5+,r0		)
EX(	mov.l	@r5+,r8		)
EX(	mov.l	@r5+,r9		)
EX(	mov.l	@r5+,r10	)
	dt	r7
EX(	mov.l	r0,@(16,r4)	)
EX(	mov.l	r8,@(20,r4)	)
EX(	mov.l	r9,@(24,r4)	)
EX(	mov.l	r10,@(28,r4)	)
	bf/s	2b
	 add	#32,r4
	tst	r2,r2
	bt	.L_cleanup
1:
EX(	mov.l	@r5+,r0		)
	dt	r2
EX(	mov.l	r0,@r4		)
	bf/s	1b
	 add	#4,r4

	bra	.L_cleanup
	 nop

! Destination = 10

.L_dest10:
	mov	r2,r7
	shlr2	r7
	shlr	r7
	tst	r7,r7
	mov	#7,r0
	bt/s	1f
	 and	r0,r2
2:
	dt	r7
#ifdef __LITTLE_ENDIAN__
EX(	mov.l	@r5+,r0		)
EX(	mov.l	@r5+,r1		)
EX(	mov.l	@r5+,r8		)
EX(	mov.l	@r5+,r9		)
EX(	mov.l	@r5+,r10	)
EX(	mov.w	r0,@r4		)
	add	#2,r4
	xtrct	r1,r0
	xtrct	r8,r1
	xtrct	r9,r8
	xtrct	r10,r9

EX(	mov.l	r0,@r4		)
EX(	mov.l	r1,@(4,r4)	)
EX(	mov.l	r8,@(8,r4)	)
EX(	mov.l	r9,@(12,r4)	)

EX(	mov.l	@r5+,r1		)
EX(	mov.l	@r5+,r8		)
EX(	mov.l	@r5+,r0		)
	xtrct	r1,r10
	xtrct	r8,r1
	xtrct	r0,r8
	shlr16	r0
EX(	mov.l	r10,@(16,r4)	)
EX(	mov.l	r1,@(20,r4)	)
EX(	mov.l	r8,@(24,r4)	)
EX(	mov.w	r0,@(28,r4)	)
	bf/s	2b
	 add	#30,r4
#else
EX(	mov.l	@(28,r5),r0	)
EX(	mov.l	@(24,r5),r8	)
EX(	mov.l	@(20,r5),r9	)
EX(	mov.l	@(16,r5),r10	)
EX(	mov.w	r0,@(30,r4)	)
	add	#-2,r4
	xtrct	r8,r0
	xtrct	r9,r8
	xtrct	r10,r9
EX(	mov.l	r0,@(28,r4)	)
EX(	mov.l	r8,@(24,r4)	)
EX(	mov.l	r9,@(20,r4)	)

EX(	mov.l	@(12,r5),r0	)
EX(	mov.l	@(8,r5),r8	)
	xtrct	r0,r10
EX(	mov.l	@(4,r5),r9	)
	mov.l	r10,@(16,r4)
EX(	mov.l	@r5,r10		)
	xtrct	r8,r0
	xtrct	r9,r8
	xtrct	r10,r9
EX(	mov.l	r0,@(12,r4)	)
EX(	mov.l	r8,@(8,r4)	)
	swap.w	r10,r0
EX(	mov.l	r9,@(4,r4)	)
EX(	mov.w	r0,@(2,r4)	)

	add	#32,r5
	bf/s	2b
	 add	#34,r4
#endif
	tst	r2,r2
	bt	.L_cleanup

1:	! Read longword, write two words per iteration
EX(	mov.l	@r5+,r0		)
	dt	r2
#ifdef __LITTLE_ENDIAN__
EX(	mov.w	r0,@r4		)
	shlr16	r0
EX(	mov.w 	r0,@(2,r4)	)
#else
EX(	mov.w	r0,@(2,r4)	)
	shlr16	r0
EX(	mov.w	r0,@r4		)
#endif
	bf/s	1b
	 add	#4,r4

	bra	.L_cleanup
	 nop

! Destination = 01 or 11

.L_dest01:
.L_dest11:
	! Read longword, write byte, word, byte per iteration
EX(	mov.l	@r5+,r0		)
	dt	r2
#ifdef __LITTLE_ENDIAN__
EX(	mov.b	r0,@r4		)
	shlr8	r0
	add	#1,r4
EX(	mov.w	r0,@r4		)
	shlr16	r0
EX(	mov.b	r0,@(2,r4)	)
	bf/s	.L_dest01
	 add	#3,r4
#else
EX(	mov.b	r0,@(3,r4)	)
	shlr8	r0
	swap.w	r0,r7
EX(	mov.b	r7,@r4		)
	add	#1,r4
EX(	mov.w	r0,@r4		)
	bf/s	.L_dest01
	 add	#3,r4
#endif

! Cleanup last few bytes
.L_cleanup:
	mov	r6,r0
	and	#3,r0
	tst	r0,r0
	bt	.L_exit
	mov	r0,r6

.L_cleanup_loop:
EX(	mov.b	@r5+,r0		)
	dt	r6
EX(	mov.b	r0,@r4		)
	bf/s	.L_cleanup_loop
	 add	#1,r4

.L_exit:
	mov	#0,r0		! normal return
5000:

# Exception handler:
.section .fixup, "ax"
6000:
	mov.l	8000f,r1
	mov	r3,r0
	jmp	@r1
	 sub	r4,r0
	.align	2
8000:	.long	5000b

.previous
	mov.l	@r15+,r8
	mov.l	@r15+,r9
	rts
	 mov.l	@r15+,r10
