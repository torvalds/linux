#ifndef	__KERNEL__
# include "arm_arch.h"
#else
# define __ARM_ARCH__ __LINUX_ARM_ARCH__
# define __ARM_MAX_ARCH__ __LINUX_ARM_ARCH__
# define poly1305_init   poly1305_init_arm
# define poly1305_blocks poly1305_blocks_arm
# define poly1305_emit   poly1305_emit_arm
.globl	poly1305_blocks_neon
#endif

#if defined(__thumb2__)
.syntax	unified
.thumb
#else
.code	32
#endif

.text

.globl	poly1305_emit
.globl	poly1305_blocks
.globl	poly1305_init
.type	poly1305_init,%function
.align	5
poly1305_init:
.Lpoly1305_init:
	stmdb	sp!,{r4-r11}

	eor	r3,r3,r3
	cmp	r1,#0
	str	r3,[r0,#0]		@ zero hash value
	str	r3,[r0,#4]
	str	r3,[r0,#8]
	str	r3,[r0,#12]
	str	r3,[r0,#16]
	str	r3,[r0,#36]		@ clear is_base2_26
	add	r0,r0,#20

#ifdef	__thumb2__
	it	eq
#endif
	moveq	r0,#0
	beq	.Lno_key

#if	__ARM_MAX_ARCH__>=7
	mov	r3,#-1
	str	r3,[r0,#28]		@ impossible key power value
# ifndef __KERNEL__
	adr	r11,.Lpoly1305_init
	ldr	r12,.LOPENSSL_armcap
# endif
#endif
	ldrb	r4,[r1,#0]
	mov	r10,#0x0fffffff
	ldrb	r5,[r1,#1]
	and	r3,r10,#-4		@ 0x0ffffffc
	ldrb	r6,[r1,#2]
	ldrb	r7,[r1,#3]
	orr	r4,r4,r5,lsl#8
	ldrb	r5,[r1,#4]
	orr	r4,r4,r6,lsl#16
	ldrb	r6,[r1,#5]
	orr	r4,r4,r7,lsl#24
	ldrb	r7,[r1,#6]
	and	r4,r4,r10

#if	__ARM_MAX_ARCH__>=7 && !defined(__KERNEL__)
# if !defined(_WIN32)
	ldr	r12,[r11,r12]		@ OPENSSL_armcap_P
# endif
# if defined(__APPLE__) || defined(_WIN32)
	ldr	r12,[r12]
# endif
#endif
	ldrb	r8,[r1,#7]
	orr	r5,r5,r6,lsl#8
	ldrb	r6,[r1,#8]
	orr	r5,r5,r7,lsl#16
	ldrb	r7,[r1,#9]
	orr	r5,r5,r8,lsl#24
	ldrb	r8,[r1,#10]
	and	r5,r5,r3

#if	__ARM_MAX_ARCH__>=7 && !defined(__KERNEL__)
	tst	r12,#ARMV7_NEON		@ check for NEON
# ifdef	__thumb2__
	adr	r9,.Lpoly1305_blocks_neon
	adr	r11,.Lpoly1305_blocks
	it	ne
	movne	r11,r9
	adr	r12,.Lpoly1305_emit
	orr	r11,r11,#1		@ thumb-ify addresses
	orr	r12,r12,#1
# else
	add	r12,r11,#(.Lpoly1305_emit-.Lpoly1305_init)
	ite	eq
	addeq	r11,r11,#(.Lpoly1305_blocks-.Lpoly1305_init)
	addne	r11,r11,#(.Lpoly1305_blocks_neon-.Lpoly1305_init)
# endif
#endif
	ldrb	r9,[r1,#11]
	orr	r6,r6,r7,lsl#8
	ldrb	r7,[r1,#12]
	orr	r6,r6,r8,lsl#16
	ldrb	r8,[r1,#13]
	orr	r6,r6,r9,lsl#24
	ldrb	r9,[r1,#14]
	and	r6,r6,r3

	ldrb	r10,[r1,#15]
	orr	r7,r7,r8,lsl#8
	str	r4,[r0,#0]
	orr	r7,r7,r9,lsl#16
	str	r5,[r0,#4]
	orr	r7,r7,r10,lsl#24
	str	r6,[r0,#8]
	and	r7,r7,r3
	str	r7,[r0,#12]
#if	__ARM_MAX_ARCH__>=7 && !defined(__KERNEL__)
	stmia	r2,{r11,r12}		@ fill functions table
	mov	r0,#1
#else
	mov	r0,#0
#endif
.Lno_key:
	ldmia	sp!,{r4-r11}
#if	__ARM_ARCH__>=5
	bx	lr				@ bx	lr
#else
	tst	lr,#1
	moveq	pc,lr			@ be binary compatible with V4, yet
	.word	0xe12fff1e			@ interoperable with Thumb ISA:-)
#endif
.size	poly1305_init,.-poly1305_init
.type	poly1305_blocks,%function
.align	5
poly1305_blocks:
.Lpoly1305_blocks:
	stmdb	sp!,{r3-r11,lr}

	ands	r2,r2,#-16
	beq	.Lno_data

	add	r2,r2,r1		@ end pointer
	sub	sp,sp,#32

#if __ARM_ARCH__<7
	ldmia	r0,{r4-r12}		@ load context
	add	r0,r0,#20
	str	r2,[sp,#16]		@ offload stuff
	str	r0,[sp,#12]
#else
	ldr	lr,[r0,#36]		@ is_base2_26
	ldmia	r0!,{r4-r8}		@ load hash value
	str	r2,[sp,#16]		@ offload stuff
	str	r0,[sp,#12]

	adds	r9,r4,r5,lsl#26	@ base 2^26 -> base 2^32
	mov	r10,r5,lsr#6
	adcs	r10,r10,r6,lsl#20
	mov	r11,r6,lsr#12
	adcs	r11,r11,r7,lsl#14
	mov	r12,r7,lsr#18
	adcs	r12,r12,r8,lsl#8
	mov	r2,#0
	teq	lr,#0
	str	r2,[r0,#16]		@ clear is_base2_26
	adc	r2,r2,r8,lsr#24

	itttt	ne
	movne	r4,r9			@ choose between radixes
	movne	r5,r10
	movne	r6,r11
	movne	r7,r12
	ldmia	r0,{r9-r12}		@ load key
	it	ne
	movne	r8,r2
#endif

	mov	lr,r1
	cmp	r3,#0
	str	r10,[sp,#20]
	str	r11,[sp,#24]
	str	r12,[sp,#28]
	b	.Loop

.align	4
.Loop:
#if __ARM_ARCH__<7
	ldrb	r0,[lr],#16		@ load input
# ifdef	__thumb2__
	it	hi
# endif
	addhi	r8,r8,#1		@ 1<<128
	ldrb	r1,[lr,#-15]
	ldrb	r2,[lr,#-14]
	ldrb	r3,[lr,#-13]
	orr	r1,r0,r1,lsl#8
	ldrb	r0,[lr,#-12]
	orr	r2,r1,r2,lsl#16
	ldrb	r1,[lr,#-11]
	orr	r3,r2,r3,lsl#24
	ldrb	r2,[lr,#-10]
	adds	r4,r4,r3		@ accumulate input

	ldrb	r3,[lr,#-9]
	orr	r1,r0,r1,lsl#8
	ldrb	r0,[lr,#-8]
	orr	r2,r1,r2,lsl#16
	ldrb	r1,[lr,#-7]
	orr	r3,r2,r3,lsl#24
	ldrb	r2,[lr,#-6]
	adcs	r5,r5,r3

	ldrb	r3,[lr,#-5]
	orr	r1,r0,r1,lsl#8
	ldrb	r0,[lr,#-4]
	orr	r2,r1,r2,lsl#16
	ldrb	r1,[lr,#-3]
	orr	r3,r2,r3,lsl#24
	ldrb	r2,[lr,#-2]
	adcs	r6,r6,r3

	ldrb	r3,[lr,#-1]
	orr	r1,r0,r1,lsl#8
	str	lr,[sp,#8]		@ offload input pointer
	orr	r2,r1,r2,lsl#16
	add	r10,r10,r10,lsr#2
	orr	r3,r2,r3,lsl#24
#else
	ldr	r0,[lr],#16		@ load input
	it	hi
	addhi	r8,r8,#1		@ padbit
	ldr	r1,[lr,#-12]
	ldr	r2,[lr,#-8]
	ldr	r3,[lr,#-4]
# ifdef	__ARMEB__
	rev	r0,r0
	rev	r1,r1
	rev	r2,r2
	rev	r3,r3
# endif
	adds	r4,r4,r0		@ accumulate input
	str	lr,[sp,#8]		@ offload input pointer
	adcs	r5,r5,r1
	add	r10,r10,r10,lsr#2
	adcs	r6,r6,r2
#endif
	add	r11,r11,r11,lsr#2
	adcs	r7,r7,r3
	add	r12,r12,r12,lsr#2

	umull	r2,r3,r5,r9
	 adc	r8,r8,#0
	umull	r0,r1,r4,r9
	umlal	r2,r3,r8,r10
	umlal	r0,r1,r7,r10
	ldr	r10,[sp,#20]		@ reload r10
	umlal	r2,r3,r6,r12
	umlal	r0,r1,r5,r12
	umlal	r2,r3,r7,r11
	umlal	r0,r1,r6,r11
	umlal	r2,r3,r4,r10
	str	r0,[sp,#0]		@ future r4
	 mul	r0,r11,r8
	ldr	r11,[sp,#24]		@ reload r11
	adds	r2,r2,r1		@ d1+=d0>>32
	 eor	r1,r1,r1
	adc	lr,r3,#0		@ future r6
	str	r2,[sp,#4]		@ future r5

	mul	r2,r12,r8
	eor	r3,r3,r3
	umlal	r0,r1,r7,r12
	ldr	r12,[sp,#28]		@ reload r12
	umlal	r2,r3,r7,r9
	umlal	r0,r1,r6,r9
	umlal	r2,r3,r6,r10
	umlal	r0,r1,r5,r10
	umlal	r2,r3,r5,r11
	umlal	r0,r1,r4,r11
	umlal	r2,r3,r4,r12
	ldr	r4,[sp,#0]
	mul	r8,r9,r8
	ldr	r5,[sp,#4]

	adds	r6,lr,r0		@ d2+=d1>>32
	ldr	lr,[sp,#8]		@ reload input pointer
	adc	r1,r1,#0
	adds	r7,r2,r1		@ d3+=d2>>32
	ldr	r0,[sp,#16]		@ reload end pointer
	adc	r3,r3,#0
	add	r8,r8,r3		@ h4+=d3>>32

	and	r1,r8,#-4
	and	r8,r8,#3
	add	r1,r1,r1,lsr#2		@ *=5
	adds	r4,r4,r1
	adcs	r5,r5,#0
	adcs	r6,r6,#0
	adcs	r7,r7,#0
	adc	r8,r8,#0

	cmp	r0,lr			@ done yet?
	bhi	.Loop

	ldr	r0,[sp,#12]
	add	sp,sp,#32
	stmdb	r0,{r4-r8}		@ store the result

.Lno_data:
#if	__ARM_ARCH__>=5
	ldmia	sp!,{r3-r11,pc}
#else
	ldmia	sp!,{r3-r11,lr}
	tst	lr,#1
	moveq	pc,lr			@ be binary compatible with V4, yet
	.word	0xe12fff1e			@ interoperable with Thumb ISA:-)
#endif
.size	poly1305_blocks,.-poly1305_blocks
.type	poly1305_emit,%function
.align	5
poly1305_emit:
.Lpoly1305_emit:
	stmdb	sp!,{r4-r11}

	ldmia	r0,{r3-r7}

#if __ARM_ARCH__>=7
	ldr	ip,[r0,#36]		@ is_base2_26

	adds	r8,r3,r4,lsl#26	@ base 2^26 -> base 2^32
	mov	r9,r4,lsr#6
	adcs	r9,r9,r5,lsl#20
	mov	r10,r5,lsr#12
	adcs	r10,r10,r6,lsl#14
	mov	r11,r6,lsr#18
	adcs	r11,r11,r7,lsl#8
	mov	r0,#0
	adc	r0,r0,r7,lsr#24

	tst	ip,ip
	itttt	ne
	movne	r3,r8
	movne	r4,r9
	movne	r5,r10
	movne	r6,r11
	it	ne
	movne	r7,r0
#endif

	adds	r8,r3,#5		@ compare to modulus
	adcs	r9,r4,#0
	adcs	r10,r5,#0
	adcs	r11,r6,#0
	adc	r0,r7,#0
	tst	r0,#4			@ did it carry/borrow?

#ifdef	__thumb2__
	it	ne
#endif
	movne	r3,r8
	ldr	r8,[r2,#0]
#ifdef	__thumb2__
	it	ne
#endif
	movne	r4,r9
	ldr	r9,[r2,#4]
#ifdef	__thumb2__
	it	ne
#endif
	movne	r5,r10
	ldr	r10,[r2,#8]
#ifdef	__thumb2__
	it	ne
#endif
	movne	r6,r11
	ldr	r11,[r2,#12]

	adds	r3,r3,r8
	adcs	r4,r4,r9
	adcs	r5,r5,r10
	adc	r6,r6,r11

#if __ARM_ARCH__>=7
# ifdef __ARMEB__
	rev	r3,r3
	rev	r4,r4
	rev	r5,r5
	rev	r6,r6
# endif
	str	r3,[r1,#0]
	str	r4,[r1,#4]
	str	r5,[r1,#8]
	str	r6,[r1,#12]
#else
	strb	r3,[r1,#0]
	mov	r3,r3,lsr#8
	strb	r4,[r1,#4]
	mov	r4,r4,lsr#8
	strb	r5,[r1,#8]
	mov	r5,r5,lsr#8
	strb	r6,[r1,#12]
	mov	r6,r6,lsr#8

	strb	r3,[r1,#1]
	mov	r3,r3,lsr#8
	strb	r4,[r1,#5]
	mov	r4,r4,lsr#8
	strb	r5,[r1,#9]
	mov	r5,r5,lsr#8
	strb	r6,[r1,#13]
	mov	r6,r6,lsr#8

	strb	r3,[r1,#2]
	mov	r3,r3,lsr#8
	strb	r4,[r1,#6]
	mov	r4,r4,lsr#8
	strb	r5,[r1,#10]
	mov	r5,r5,lsr#8
	strb	r6,[r1,#14]
	mov	r6,r6,lsr#8

	strb	r3,[r1,#3]
	strb	r4,[r1,#7]
	strb	r5,[r1,#11]
	strb	r6,[r1,#15]
#endif
	ldmia	sp!,{r4-r11}
#if	__ARM_ARCH__>=5
	bx	lr				@ bx	lr
#else
	tst	lr,#1
	moveq	pc,lr			@ be binary compatible with V4, yet
	.word	0xe12fff1e			@ interoperable with Thumb ISA:-)
#endif
.size	poly1305_emit,.-poly1305_emit
#if	__ARM_MAX_ARCH__>=7
.fpu	neon

.type	poly1305_init_neon,%function
.align	5
poly1305_init_neon:
.Lpoly1305_init_neon:
	ldr	r3,[r0,#48]		@ first table element
	cmp	r3,#-1			@ is value impossible?
	bne	.Lno_init_neon

	ldr	r4,[r0,#20]		@ load key base 2^32
	ldr	r5,[r0,#24]
	ldr	r6,[r0,#28]
	ldr	r7,[r0,#32]

	and	r2,r4,#0x03ffffff	@ base 2^32 -> base 2^26
	mov	r3,r4,lsr#26
	mov	r4,r5,lsr#20
	orr	r3,r3,r5,lsl#6
	mov	r5,r6,lsr#14
	orr	r4,r4,r6,lsl#12
	mov	r6,r7,lsr#8
	orr	r5,r5,r7,lsl#18
	and	r3,r3,#0x03ffffff
	and	r4,r4,#0x03ffffff
	and	r5,r5,#0x03ffffff

	vdup.32	d0,r2			@ r^1 in both lanes
	add	r2,r3,r3,lsl#2		@ *5
	vdup.32	d1,r3
	add	r3,r4,r4,lsl#2
	vdup.32	d2,r2
	vdup.32	d3,r4
	add	r4,r5,r5,lsl#2
	vdup.32	d4,r3
	vdup.32	d5,r5
	add	r5,r6,r6,lsl#2
	vdup.32	d6,r4
	vdup.32	d7,r6
	vdup.32	d8,r5

	mov	r5,#2		@ counter

.Lsquare_neon:
	@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
	@ d0 = h0*r0 + h4*5*r1 + h3*5*r2 + h2*5*r3 + h1*5*r4
	@ d1 = h1*r0 + h0*r1   + h4*5*r2 + h3*5*r3 + h2*5*r4
	@ d2 = h2*r0 + h1*r1   + h0*r2   + h4*5*r3 + h3*5*r4
	@ d3 = h3*r0 + h2*r1   + h1*r2   + h0*r3   + h4*5*r4
	@ d4 = h4*r0 + h3*r1   + h2*r2   + h1*r3   + h0*r4

	vmull.u32	q5,d0,d0[1]
	vmull.u32	q6,d1,d0[1]
	vmull.u32	q7,d3,d0[1]
	vmull.u32	q8,d5,d0[1]
	vmull.u32	q9,d7,d0[1]

	vmlal.u32	q5,d7,d2[1]
	vmlal.u32	q6,d0,d1[1]
	vmlal.u32	q7,d1,d1[1]
	vmlal.u32	q8,d3,d1[1]
	vmlal.u32	q9,d5,d1[1]

	vmlal.u32	q5,d5,d4[1]
	vmlal.u32	q6,d7,d4[1]
	vmlal.u32	q8,d1,d3[1]
	vmlal.u32	q7,d0,d3[1]
	vmlal.u32	q9,d3,d3[1]

	vmlal.u32	q5,d3,d6[1]
	vmlal.u32	q8,d0,d5[1]
	vmlal.u32	q6,d5,d6[1]
	vmlal.u32	q7,d7,d6[1]
	vmlal.u32	q9,d1,d5[1]

	vmlal.u32	q8,d7,d8[1]
	vmlal.u32	q5,d1,d8[1]
	vmlal.u32	q6,d3,d8[1]
	vmlal.u32	q7,d5,d8[1]
	vmlal.u32	q9,d0,d7[1]

	@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
	@ lazy reduction as discussed in "NEON crypto" by D.J. Bernstein
	@ and P. Schwabe
	@
	@ H0>>+H1>>+H2>>+H3>>+H4
	@ H3>>+H4>>*5+H0>>+H1
	@
	@ Trivia.
	@
	@ Result of multiplication of n-bit number by m-bit number is
	@ n+m bits wide. However! Even though 2^n is a n+1-bit number,
	@ m-bit number multiplied by 2^n is still n+m bits wide.
	@
	@ Sum of two n-bit numbers is n+1 bits wide, sum of three - n+2,
	@ and so is sum of four. Sum of 2^m n-m-bit numbers and n-bit
	@ one is n+1 bits wide.
	@
	@ >>+ denotes Hnext += Hn>>26, Hn &= 0x3ffffff. This means that
	@ H0, H2, H3 are guaranteed to be 26 bits wide, while H1 and H4
	@ can be 27. However! In cases when their width exceeds 26 bits
	@ they are limited by 2^26+2^6. This in turn means that *sum*
	@ of the products with these values can still be viewed as sum
	@ of 52-bit numbers as long as the amount of addends is not a
	@ power of 2. For example,
	@
	@ H4 = H4*R0 + H3*R1 + H2*R2 + H1*R3 + H0 * R4,
	@
	@ which can't be larger than 5 * (2^26 + 2^6) * (2^26 + 2^6), or
	@ 5 * (2^52 + 2*2^32 + 2^12), which in turn is smaller than
	@ 8 * (2^52) or 2^55. However, the value is then multiplied by
	@ by 5, so we should be looking at 5 * 5 * (2^52 + 2^33 + 2^12),
	@ which is less than 32 * (2^52) or 2^57. And when processing
	@ data we are looking at triple as many addends...
	@
	@ In key setup procedure pre-reduced H0 is limited by 5*4+1 and
	@ 5*H4 - by 5*5 52-bit addends, or 57 bits. But when hashing the
	@ input H0 is limited by (5*4+1)*3 addends, or 58 bits, while
	@ 5*H4 by 5*5*3, or 59[!] bits. How is this relevant? vmlal.u32
	@ instruction accepts 2x32-bit input and writes 2x64-bit result.
	@ This means that result of reduction have to be compressed upon
	@ loop wrap-around. This can be done in the process of reduction
	@ to minimize amount of instructions [as well as amount of
	@ 128-bit instructions, which benefits low-end processors], but
	@ one has to watch for H2 (which is narrower than H0) and 5*H4
	@ not being wider than 58 bits, so that result of right shift
	@ by 26 bits fits in 32 bits. This is also useful on x86,
	@ because it allows to use paddd in place for paddq, which
	@ benefits Atom, where paddq is ridiculously slow.

	vshr.u64	q15,q8,#26
	vmovn.i64	d16,q8
	 vshr.u64	q4,q5,#26
	 vmovn.i64	d10,q5
	vadd.i64	q9,q9,q15		@ h3 -> h4
	vbic.i32	d16,#0xfc000000	@ &=0x03ffffff
	 vadd.i64	q6,q6,q4		@ h0 -> h1
	 vbic.i32	d10,#0xfc000000

	vshrn.u64	d30,q9,#26
	vmovn.i64	d18,q9
	 vshr.u64	q4,q6,#26
	 vmovn.i64	d12,q6
	 vadd.i64	q7,q7,q4		@ h1 -> h2
	vbic.i32	d18,#0xfc000000
	 vbic.i32	d12,#0xfc000000

	vadd.i32	d10,d10,d30
	vshl.u32	d30,d30,#2
	 vshrn.u64	d8,q7,#26
	 vmovn.i64	d14,q7
	vadd.i32	d10,d10,d30	@ h4 -> h0
	 vadd.i32	d16,d16,d8	@ h2 -> h3
	 vbic.i32	d14,#0xfc000000

	vshr.u32	d30,d10,#26
	vbic.i32	d10,#0xfc000000
	 vshr.u32	d8,d16,#26
	 vbic.i32	d16,#0xfc000000
	vadd.i32	d12,d12,d30	@ h0 -> h1
	 vadd.i32	d18,d18,d8	@ h3 -> h4

	subs		r5,r5,#1
	beq		.Lsquare_break_neon

	add		r6,r0,#(48+0*9*4)
	add		r7,r0,#(48+1*9*4)

	vtrn.32		d0,d10		@ r^2:r^1
	vtrn.32		d3,d14
	vtrn.32		d5,d16
	vtrn.32		d1,d12
	vtrn.32		d7,d18

	vshl.u32	d4,d3,#2		@ *5
	vshl.u32	d6,d5,#2
	vshl.u32	d2,d1,#2
	vshl.u32	d8,d7,#2
	vadd.i32	d4,d4,d3
	vadd.i32	d2,d2,d1
	vadd.i32	d6,d6,d5
	vadd.i32	d8,d8,d7

	vst4.32		{d0[0],d1[0],d2[0],d3[0]},[r6]!
	vst4.32		{d0[1],d1[1],d2[1],d3[1]},[r7]!
	vst4.32		{d4[0],d5[0],d6[0],d7[0]},[r6]!
	vst4.32		{d4[1],d5[1],d6[1],d7[1]},[r7]!
	vst1.32		{d8[0]},[r6,:32]
	vst1.32		{d8[1]},[r7,:32]

	b		.Lsquare_neon

.align	4
.Lsquare_break_neon:
	add		r6,r0,#(48+2*4*9)
	add		r7,r0,#(48+3*4*9)

	vmov		d0,d10		@ r^4:r^3
	vshl.u32	d2,d12,#2		@ *5
	vmov		d1,d12
	vshl.u32	d4,d14,#2
	vmov		d3,d14
	vshl.u32	d6,d16,#2
	vmov		d5,d16
	vshl.u32	d8,d18,#2
	vmov		d7,d18
	vadd.i32	d2,d2,d12
	vadd.i32	d4,d4,d14
	vadd.i32	d6,d6,d16
	vadd.i32	d8,d8,d18

	vst4.32		{d0[0],d1[0],d2[0],d3[0]},[r6]!
	vst4.32		{d0[1],d1[1],d2[1],d3[1]},[r7]!
	vst4.32		{d4[0],d5[0],d6[0],d7[0]},[r6]!
	vst4.32		{d4[1],d5[1],d6[1],d7[1]},[r7]!
	vst1.32		{d8[0]},[r6]
	vst1.32		{d8[1]},[r7]

.Lno_init_neon:
	bx	lr				@ bx	lr
.size	poly1305_init_neon,.-poly1305_init_neon

.type	poly1305_blocks_neon,%function
.align	5
poly1305_blocks_neon:
.Lpoly1305_blocks_neon:
	ldr	ip,[r0,#36]		@ is_base2_26

	cmp	r2,#64
	blo	.Lpoly1305_blocks

	stmdb	sp!,{r4-r7}
	vstmdb	sp!,{d8-d15}		@ ABI specification says so

	tst	ip,ip			@ is_base2_26?
	bne	.Lbase2_26_neon

	stmdb	sp!,{r1-r3,lr}
	bl	.Lpoly1305_init_neon

	ldr	r4,[r0,#0]		@ load hash value base 2^32
	ldr	r5,[r0,#4]
	ldr	r6,[r0,#8]
	ldr	r7,[r0,#12]
	ldr	ip,[r0,#16]

	and	r2,r4,#0x03ffffff	@ base 2^32 -> base 2^26
	mov	r3,r4,lsr#26
	 veor	d10,d10,d10
	mov	r4,r5,lsr#20
	orr	r3,r3,r5,lsl#6
	 veor	d12,d12,d12
	mov	r5,r6,lsr#14
	orr	r4,r4,r6,lsl#12
	 veor	d14,d14,d14
	mov	r6,r7,lsr#8
	orr	r5,r5,r7,lsl#18
	 veor	d16,d16,d16
	and	r3,r3,#0x03ffffff
	orr	r6,r6,ip,lsl#24
	 veor	d18,d18,d18
	and	r4,r4,#0x03ffffff
	mov	r1,#1
	and	r5,r5,#0x03ffffff
	str	r1,[r0,#36]		@ set is_base2_26

	vmov.32	d10[0],r2
	vmov.32	d12[0],r3
	vmov.32	d14[0],r4
	vmov.32	d16[0],r5
	vmov.32	d18[0],r6
	adr	r5,.Lzeros

	ldmia	sp!,{r1-r3,lr}
	b	.Lhash_loaded

.align	4
.Lbase2_26_neon:
	@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
	@ load hash value

	veor		d10,d10,d10
	veor		d12,d12,d12
	veor		d14,d14,d14
	veor		d16,d16,d16
	veor		d18,d18,d18
	vld4.32		{d10[0],d12[0],d14[0],d16[0]},[r0]!
	adr		r5,.Lzeros
	vld1.32		{d18[0]},[r0]
	sub		r0,r0,#16		@ rewind

.Lhash_loaded:
	add		r4,r1,#32
	mov		r3,r3,lsl#24
	tst		r2,#31
	beq		.Leven

	vld4.32		{d20[0],d22[0],d24[0],d26[0]},[r1]!
	vmov.32		d28[0],r3
	sub		r2,r2,#16
	add		r4,r1,#32

# ifdef	__ARMEB__
	vrev32.8	q10,q10
	vrev32.8	q13,q13
	vrev32.8	q11,q11
	vrev32.8	q12,q12
# endif
	vsri.u32	d28,d26,#8	@ base 2^32 -> base 2^26
	vshl.u32	d26,d26,#18

	vsri.u32	d26,d24,#14
	vshl.u32	d24,d24,#12
	vadd.i32	d29,d28,d18	@ add hash value and move to #hi

	vbic.i32	d26,#0xfc000000
	vsri.u32	d24,d22,#20
	vshl.u32	d22,d22,#6

	vbic.i32	d24,#0xfc000000
	vsri.u32	d22,d20,#26
	vadd.i32	d27,d26,d16

	vbic.i32	d20,#0xfc000000
	vbic.i32	d22,#0xfc000000
	vadd.i32	d25,d24,d14

	vadd.i32	d21,d20,d10
	vadd.i32	d23,d22,d12

	mov		r7,r5
	add		r6,r0,#48

	cmp		r2,r2
	b		.Long_tail

.align	4
.Leven:
	subs		r2,r2,#64
	it		lo
	movlo		r4,r5

	vmov.i32	q14,#1<<24		@ padbit, yes, always
	vld4.32		{d20,d22,d24,d26},[r1]	@ inp[0:1]
	add		r1,r1,#64
	vld4.32		{d21,d23,d25,d27},[r4]	@ inp[2:3] (or 0)
	add		r4,r4,#64
	itt		hi
	addhi		r7,r0,#(48+1*9*4)
	addhi		r6,r0,#(48+3*9*4)

# ifdef	__ARMEB__
	vrev32.8	q10,q10
	vrev32.8	q13,q13
	vrev32.8	q11,q11
	vrev32.8	q12,q12
# endif
	vsri.u32	q14,q13,#8		@ base 2^32 -> base 2^26
	vshl.u32	q13,q13,#18

	vsri.u32	q13,q12,#14
	vshl.u32	q12,q12,#12

	vbic.i32	q13,#0xfc000000
	vsri.u32	q12,q11,#20
	vshl.u32	q11,q11,#6

	vbic.i32	q12,#0xfc000000
	vsri.u32	q11,q10,#26

	vbic.i32	q10,#0xfc000000
	vbic.i32	q11,#0xfc000000

	bls		.Lskip_loop

	vld4.32		{d0[1],d1[1],d2[1],d3[1]},[r7]!	@ load r^2
	vld4.32		{d0[0],d1[0],d2[0],d3[0]},[r6]!	@ load r^4
	vld4.32		{d4[1],d5[1],d6[1],d7[1]},[r7]!
	vld4.32		{d4[0],d5[0],d6[0],d7[0]},[r6]!
	b		.Loop_neon

.align	5
.Loop_neon:
	@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
	@ ((inp[0]*r^4+inp[2]*r^2+inp[4])*r^4+inp[6]*r^2
	@ ((inp[1]*r^4+inp[3]*r^2+inp[5])*r^3+inp[7]*r
	@   ___________________/
	@ ((inp[0]*r^4+inp[2]*r^2+inp[4])*r^4+inp[6]*r^2+inp[8])*r^2
	@ ((inp[1]*r^4+inp[3]*r^2+inp[5])*r^4+inp[7]*r^2+inp[9])*r
	@   ___________________/ ____________________/
	@
	@ Note that we start with inp[2:3]*r^2. This is because it
	@ doesn't depend on reduction in previous iteration.
	@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
	@ d4 = h4*r0 + h3*r1   + h2*r2   + h1*r3   + h0*r4
	@ d3 = h3*r0 + h2*r1   + h1*r2   + h0*r3   + h4*5*r4
	@ d2 = h2*r0 + h1*r1   + h0*r2   + h4*5*r3 + h3*5*r4
	@ d1 = h1*r0 + h0*r1   + h4*5*r2 + h3*5*r3 + h2*5*r4
	@ d0 = h0*r0 + h4*5*r1 + h3*5*r2 + h2*5*r3 + h1*5*r4

	@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
	@ inp[2:3]*r^2

	vadd.i32	d24,d24,d14	@ accumulate inp[0:1]
	vmull.u32	q7,d25,d0[1]
	vadd.i32	d20,d20,d10
	vmull.u32	q5,d21,d0[1]
	vadd.i32	d26,d26,d16
	vmull.u32	q8,d27,d0[1]
	vmlal.u32	q7,d23,d1[1]
	vadd.i32	d22,d22,d12
	vmull.u32	q6,d23,d0[1]

	vadd.i32	d28,d28,d18
	vmull.u32	q9,d29,d0[1]
	subs		r2,r2,#64
	vmlal.u32	q5,d29,d2[1]
	it		lo
	movlo		r4,r5
	vmlal.u32	q8,d25,d1[1]
	vld1.32		d8[1],[r7,:32]
	vmlal.u32	q6,d21,d1[1]
	vmlal.u32	q9,d27,d1[1]

	vmlal.u32	q5,d27,d4[1]
	vmlal.u32	q8,d23,d3[1]
	vmlal.u32	q9,d25,d3[1]
	vmlal.u32	q6,d29,d4[1]
	vmlal.u32	q7,d21,d3[1]

	vmlal.u32	q8,d21,d5[1]
	vmlal.u32	q5,d25,d6[1]
	vmlal.u32	q9,d23,d5[1]
	vmlal.u32	q6,d27,d6[1]
	vmlal.u32	q7,d29,d6[1]

	vmlal.u32	q8,d29,d8[1]
	vmlal.u32	q5,d23,d8[1]
	vmlal.u32	q9,d21,d7[1]
	vmlal.u32	q6,d25,d8[1]
	vmlal.u32	q7,d27,d8[1]

	vld4.32		{d21,d23,d25,d27},[r4]	@ inp[2:3] (or 0)
	add		r4,r4,#64

	@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
	@ (hash+inp[0:1])*r^4 and accumulate

	vmlal.u32	q8,d26,d0[0]
	vmlal.u32	q5,d20,d0[0]
	vmlal.u32	q9,d28,d0[0]
	vmlal.u32	q6,d22,d0[0]
	vmlal.u32	q7,d24,d0[0]
	vld1.32		d8[0],[r6,:32]

	vmlal.u32	q8,d24,d1[0]
	vmlal.u32	q5,d28,d2[0]
	vmlal.u32	q9,d26,d1[0]
	vmlal.u32	q6,d20,d1[0]
	vmlal.u32	q7,d22,d1[0]

	vmlal.u32	q8,d22,d3[0]
	vmlal.u32	q5,d26,d4[0]
	vmlal.u32	q9,d24,d3[0]
	vmlal.u32	q6,d28,d4[0]
	vmlal.u32	q7,d20,d3[0]

	vmlal.u32	q8,d20,d5[0]
	vmlal.u32	q5,d24,d6[0]
	vmlal.u32	q9,d22,d5[0]
	vmlal.u32	q6,d26,d6[0]
	vmlal.u32	q8,d28,d8[0]

	vmlal.u32	q7,d28,d6[0]
	vmlal.u32	q5,d22,d8[0]
	vmlal.u32	q9,d20,d7[0]
	vmov.i32	q14,#1<<24		@ padbit, yes, always
	vmlal.u32	q6,d24,d8[0]
	vmlal.u32	q7,d26,d8[0]

	vld4.32		{d20,d22,d24,d26},[r1]	@ inp[0:1]
	add		r1,r1,#64
# ifdef	__ARMEB__
	vrev32.8	q10,q10
	vrev32.8	q11,q11
	vrev32.8	q12,q12
	vrev32.8	q13,q13
# endif

	@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
	@ lazy reduction interleaved with base 2^32 -> base 2^26 of
	@ inp[0:3] previously loaded to q10-q13 and smashed to q10-q14.

	vshr.u64	q15,q8,#26
	vmovn.i64	d16,q8
	 vshr.u64	q4,q5,#26
	 vmovn.i64	d10,q5
	vadd.i64	q9,q9,q15		@ h3 -> h4
	vbic.i32	d16,#0xfc000000
	  vsri.u32	q14,q13,#8		@ base 2^32 -> base 2^26
	 vadd.i64	q6,q6,q4		@ h0 -> h1
	  vshl.u32	q13,q13,#18
	 vbic.i32	d10,#0xfc000000

	vshrn.u64	d30,q9,#26
	vmovn.i64	d18,q9
	 vshr.u64	q4,q6,#26
	 vmovn.i64	d12,q6
	 vadd.i64	q7,q7,q4		@ h1 -> h2
	  vsri.u32	q13,q12,#14
	vbic.i32	d18,#0xfc000000
	  vshl.u32	q12,q12,#12
	 vbic.i32	d12,#0xfc000000

	vadd.i32	d10,d10,d30
	vshl.u32	d30,d30,#2
	  vbic.i32	q13,#0xfc000000
	 vshrn.u64	d8,q7,#26
	 vmovn.i64	d14,q7
	vaddl.u32	q5,d10,d30	@ h4 -> h0 [widen for a sec]
	  vsri.u32	q12,q11,#20
	 vadd.i32	d16,d16,d8	@ h2 -> h3
	  vshl.u32	q11,q11,#6
	 vbic.i32	d14,#0xfc000000
	  vbic.i32	q12,#0xfc000000

	vshrn.u64	d30,q5,#26		@ re-narrow
	vmovn.i64	d10,q5
	  vsri.u32	q11,q10,#26
	  vbic.i32	q10,#0xfc000000
	 vshr.u32	d8,d16,#26
	 vbic.i32	d16,#0xfc000000
	vbic.i32	d10,#0xfc000000
	vadd.i32	d12,d12,d30	@ h0 -> h1
	 vadd.i32	d18,d18,d8	@ h3 -> h4
	  vbic.i32	q11,#0xfc000000

	bhi		.Loop_neon

.Lskip_loop:
	@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
	@ multiply (inp[0:1]+hash) or inp[2:3] by r^2:r^1

	add		r7,r0,#(48+0*9*4)
	add		r6,r0,#(48+1*9*4)
	adds		r2,r2,#32
	it		ne
	movne		r2,#0
	bne		.Long_tail

	vadd.i32	d25,d24,d14	@ add hash value and move to #hi
	vadd.i32	d21,d20,d10
	vadd.i32	d27,d26,d16
	vadd.i32	d23,d22,d12
	vadd.i32	d29,d28,d18

.Long_tail:
	vld4.32		{d0[1],d1[1],d2[1],d3[1]},[r7]!	@ load r^1
	vld4.32		{d0[0],d1[0],d2[0],d3[0]},[r6]!	@ load r^2

	vadd.i32	d24,d24,d14	@ can be redundant
	vmull.u32	q7,d25,d0
	vadd.i32	d20,d20,d10
	vmull.u32	q5,d21,d0
	vadd.i32	d26,d26,d16
	vmull.u32	q8,d27,d0
	vadd.i32	d22,d22,d12
	vmull.u32	q6,d23,d0
	vadd.i32	d28,d28,d18
	vmull.u32	q9,d29,d0

	vmlal.u32	q5,d29,d2
	vld4.32		{d4[1],d5[1],d6[1],d7[1]},[r7]!
	vmlal.u32	q8,d25,d1
	vld4.32		{d4[0],d5[0],d6[0],d7[0]},[r6]!
	vmlal.u32	q6,d21,d1
	vmlal.u32	q9,d27,d1
	vmlal.u32	q7,d23,d1

	vmlal.u32	q8,d23,d3
	vld1.32		d8[1],[r7,:32]
	vmlal.u32	q5,d27,d4
	vld1.32		d8[0],[r6,:32]
	vmlal.u32	q9,d25,d3
	vmlal.u32	q6,d29,d4
	vmlal.u32	q7,d21,d3

	vmlal.u32	q8,d21,d5
	 it		ne
	 addne		r7,r0,#(48+2*9*4)
	vmlal.u32	q5,d25,d6
	 it		ne
	 addne		r6,r0,#(48+3*9*4)
	vmlal.u32	q9,d23,d5
	vmlal.u32	q6,d27,d6
	vmlal.u32	q7,d29,d6

	vmlal.u32	q8,d29,d8
	 vorn		q0,q0,q0	@ all-ones, can be redundant
	vmlal.u32	q5,d23,d8
	 vshr.u64	q0,q0,#38
	vmlal.u32	q9,d21,d7
	vmlal.u32	q6,d25,d8
	vmlal.u32	q7,d27,d8

	beq		.Lshort_tail

	@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
	@ (hash+inp[0:1])*r^4:r^3 and accumulate

	vld4.32		{d0[1],d1[1],d2[1],d3[1]},[r7]!	@ load r^3
	vld4.32		{d0[0],d1[0],d2[0],d3[0]},[r6]!	@ load r^4

	vmlal.u32	q7,d24,d0
	vmlal.u32	q5,d20,d0
	vmlal.u32	q8,d26,d0
	vmlal.u32	q6,d22,d0
	vmlal.u32	q9,d28,d0

	vmlal.u32	q5,d28,d2
	vld4.32		{d4[1],d5[1],d6[1],d7[1]},[r7]!
	vmlal.u32	q8,d24,d1
	vld4.32		{d4[0],d5[0],d6[0],d7[0]},[r6]!
	vmlal.u32	q6,d20,d1
	vmlal.u32	q9,d26,d1
	vmlal.u32	q7,d22,d1

	vmlal.u32	q8,d22,d3
	vld1.32		d8[1],[r7,:32]
	vmlal.u32	q5,d26,d4
	vld1.32		d8[0],[r6,:32]
	vmlal.u32	q9,d24,d3
	vmlal.u32	q6,d28,d4
	vmlal.u32	q7,d20,d3

	vmlal.u32	q8,d20,d5
	vmlal.u32	q5,d24,d6
	vmlal.u32	q9,d22,d5
	vmlal.u32	q6,d26,d6
	vmlal.u32	q7,d28,d6

	vmlal.u32	q8,d28,d8
	 vorn		q0,q0,q0	@ all-ones
	vmlal.u32	q5,d22,d8
	 vshr.u64	q0,q0,#38
	vmlal.u32	q9,d20,d7
	vmlal.u32	q6,d24,d8
	vmlal.u32	q7,d26,d8

.Lshort_tail:
	@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
	@ horizontal addition

	vadd.i64	d16,d16,d17
	vadd.i64	d10,d10,d11
	vadd.i64	d18,d18,d19
	vadd.i64	d12,d12,d13
	vadd.i64	d14,d14,d15

	@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
	@ lazy reduction, but without narrowing

	vshr.u64	q15,q8,#26
	vand.i64	q8,q8,q0
	 vshr.u64	q4,q5,#26
	 vand.i64	q5,q5,q0
	vadd.i64	q9,q9,q15		@ h3 -> h4
	 vadd.i64	q6,q6,q4		@ h0 -> h1

	vshr.u64	q15,q9,#26
	vand.i64	q9,q9,q0
	 vshr.u64	q4,q6,#26
	 vand.i64	q6,q6,q0
	 vadd.i64	q7,q7,q4		@ h1 -> h2

	vadd.i64	q5,q5,q15
	vshl.u64	q15,q15,#2
	 vshr.u64	q4,q7,#26
	 vand.i64	q7,q7,q0
	vadd.i64	q5,q5,q15		@ h4 -> h0
	 vadd.i64	q8,q8,q4		@ h2 -> h3

	vshr.u64	q15,q5,#26
	vand.i64	q5,q5,q0
	 vshr.u64	q4,q8,#26
	 vand.i64	q8,q8,q0
	vadd.i64	q6,q6,q15		@ h0 -> h1
	 vadd.i64	q9,q9,q4		@ h3 -> h4

	cmp		r2,#0
	bne		.Leven

	@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
	@ store hash value

	vst4.32		{d10[0],d12[0],d14[0],d16[0]},[r0]!
	vst1.32		{d18[0]},[r0]

	vldmia	sp!,{d8-d15}			@ epilogue
	ldmia	sp!,{r4-r7}
	bx	lr					@ bx	lr
.size	poly1305_blocks_neon,.-poly1305_blocks_neon

.align	5
.Lzeros:
.long	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
#ifndef	__KERNEL__
.LOPENSSL_armcap:
# ifdef	_WIN32
.word	OPENSSL_armcap_P
# else
.word	OPENSSL_armcap_P-.Lpoly1305_init
# endif
.comm	OPENSSL_armcap_P,4,4
.hidden	OPENSSL_armcap_P
#endif
#endif
.asciz	"Poly1305 for ARMv4/NEON, CRYPTOGAMS by @dot-asm"
.align	2
