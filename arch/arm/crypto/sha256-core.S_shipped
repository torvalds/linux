
@ ====================================================================
@ Written by Andy Polyakov <appro@openssl.org> for the OpenSSL
@ project. The module is, however, dual licensed under OpenSSL and
@ CRYPTOGAMS licenses depending on where you obtain it. For further
@ details see http://www.openssl.org/~appro/cryptogams/.
@
@ Permission to use under GPL terms is granted.
@ ====================================================================

@ SHA256 block procedure for ARMv4. May 2007.

@ Performance is ~2x better than gcc 3.4 generated code and in "abso-
@ lute" terms is ~2250 cycles per 64-byte block or ~35 cycles per
@ byte [on single-issue Xscale PXA250 core].

@ July 2010.
@
@ Rescheduling for dual-issue pipeline resulted in 22% improvement on
@ Cortex A8 core and ~20 cycles per processed byte.

@ February 2011.
@
@ Profiler-assisted and platform-specific optimization resulted in 16%
@ improvement on Cortex A8 core and ~15.4 cycles per processed byte.

@ September 2013.
@
@ Add NEON implementation. On Cortex A8 it was measured to process one
@ byte in 12.5 cycles or 23% faster than integer-only code. Snapdragon
@ S4 does it in 12.5 cycles too, but it's 50% faster than integer-only
@ code (meaning that latter performs sub-optimally, nothing was done
@ about it).

@ May 2014.
@
@ Add ARMv8 code path performing at 2.0 cpb on Apple A7.

#ifndef __KERNEL__
# include "arm_arch.h"
#else
# define __ARM_ARCH__ __LINUX_ARM_ARCH__
# define __ARM_MAX_ARCH__ 7
#endif

.text
#if __ARM_ARCH__<7
.code	32
#else
.syntax unified
# ifdef __thumb2__
#  define adrl adr
.thumb
# else
.code   32
# endif
#endif

.type	K256,%object
.align	5
K256:
.word	0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5
.word	0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5
.word	0xd807aa98,0x12835b01,0x243185be,0x550c7dc3
.word	0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174
.word	0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc
.word	0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da
.word	0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7
.word	0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967
.word	0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13
.word	0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85
.word	0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3
.word	0xd192e819,0xd6990624,0xf40e3585,0x106aa070
.word	0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5
.word	0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3
.word	0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208
.word	0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
.size	K256,.-K256
.word	0				@ terminator
#if __ARM_MAX_ARCH__>=7 && !defined(__KERNEL__)
.LOPENSSL_armcap:
.word	OPENSSL_armcap_P-sha256_block_data_order
#endif
.align	5

.global	sha256_block_data_order
.type	sha256_block_data_order,%function
sha256_block_data_order:
#if __ARM_ARCH__<7
	sub	r3,pc,#8		@ sha256_block_data_order
#else
	adr	r3,sha256_block_data_order
#endif
#if __ARM_MAX_ARCH__>=7 && !defined(__KERNEL__)
	ldr	r12,.LOPENSSL_armcap
	ldr	r12,[r3,r12]		@ OPENSSL_armcap_P
	tst	r12,#ARMV8_SHA256
	bne	.LARMv8
	tst	r12,#ARMV7_NEON
	bne	.LNEON
#endif
	add	r2,r1,r2,lsl#6	@ len to point at the end of inp
	stmdb	sp!,{r0,r1,r2,r4-r11,lr}
	ldmia	r0,{r4,r5,r6,r7,r8,r9,r10,r11}
	sub	r14,r3,#256+32	@ K256
	sub	sp,sp,#16*4		@ alloca(X[16])
.Loop:
# if __ARM_ARCH__>=7
	ldr	r2,[r1],#4
# else
	ldrb	r2,[r1,#3]
# endif
	eor	r3,r5,r6		@ magic
	eor	r12,r12,r12
#if __ARM_ARCH__>=7
	@ ldr	r2,[r1],#4			@ 0
# if 0==15
	str	r1,[sp,#17*4]			@ make room for r1
# endif
	eor	r0,r8,r8,ror#5
	add	r4,r4,r12			@ h+=Maj(a,b,c) from the past
	eor	r0,r0,r8,ror#19	@ Sigma1(e)
# ifndef __ARMEB__
	rev	r2,r2
# endif
#else
	@ ldrb	r2,[r1,#3]			@ 0
	add	r4,r4,r12			@ h+=Maj(a,b,c) from the past
	ldrb	r12,[r1,#2]
	ldrb	r0,[r1,#1]
	orr	r2,r2,r12,lsl#8
	ldrb	r12,[r1],#4
	orr	r2,r2,r0,lsl#16
# if 0==15
	str	r1,[sp,#17*4]			@ make room for r1
# endif
	eor	r0,r8,r8,ror#5
	orr	r2,r2,r12,lsl#24
	eor	r0,r0,r8,ror#19	@ Sigma1(e)
#endif
	ldr	r12,[r14],#4			@ *K256++
	add	r11,r11,r2			@ h+=X[i]
	str	r2,[sp,#0*4]
	eor	r2,r9,r10
	add	r11,r11,r0,ror#6	@ h+=Sigma1(e)
	and	r2,r2,r8
	add	r11,r11,r12			@ h+=K256[i]
	eor	r2,r2,r10			@ Ch(e,f,g)
	eor	r0,r4,r4,ror#11
	add	r11,r11,r2			@ h+=Ch(e,f,g)
#if 0==31
	and	r12,r12,#0xff
	cmp	r12,#0xf2			@ done?
#endif
#if 0<15
# if __ARM_ARCH__>=7
	ldr	r2,[r1],#4			@ prefetch
# else
	ldrb	r2,[r1,#3]
# endif
	eor	r12,r4,r5			@ a^b, b^c in next round
#else
	ldr	r2,[sp,#2*4]		@ from future BODY_16_xx
	eor	r12,r4,r5			@ a^b, b^c in next round
	ldr	r1,[sp,#15*4]	@ from future BODY_16_xx
#endif
	eor	r0,r0,r4,ror#20	@ Sigma0(a)
	and	r3,r3,r12			@ (b^c)&=(a^b)
	add	r7,r7,r11			@ d+=h
	eor	r3,r3,r5			@ Maj(a,b,c)
	add	r11,r11,r0,ror#2	@ h+=Sigma0(a)
	@ add	r11,r11,r3			@ h+=Maj(a,b,c)
#if __ARM_ARCH__>=7
	@ ldr	r2,[r1],#4			@ 1
# if 1==15
	str	r1,[sp,#17*4]			@ make room for r1
# endif
	eor	r0,r7,r7,ror#5
	add	r11,r11,r3			@ h+=Maj(a,b,c) from the past
	eor	r0,r0,r7,ror#19	@ Sigma1(e)
# ifndef __ARMEB__
	rev	r2,r2
# endif
#else
	@ ldrb	r2,[r1,#3]			@ 1
	add	r11,r11,r3			@ h+=Maj(a,b,c) from the past
	ldrb	r3,[r1,#2]
	ldrb	r0,[r1,#1]
	orr	r2,r2,r3,lsl#8
	ldrb	r3,[r1],#4
	orr	r2,r2,r0,lsl#16
# if 1==15
	str	r1,[sp,#17*4]			@ make room for r1
# endif
	eor	r0,r7,r7,ror#5
	orr	r2,r2,r3,lsl#24
	eor	r0,r0,r7,ror#19	@ Sigma1(e)
#endif
	ldr	r3,[r14],#4			@ *K256++
	add	r10,r10,r2			@ h+=X[i]
	str	r2,[sp,#1*4]
	eor	r2,r8,r9
	add	r10,r10,r0,ror#6	@ h+=Sigma1(e)
	and	r2,r2,r7
	add	r10,r10,r3			@ h+=K256[i]
	eor	r2,r2,r9			@ Ch(e,f,g)
	eor	r0,r11,r11,ror#11
	add	r10,r10,r2			@ h+=Ch(e,f,g)
#if 1==31
	and	r3,r3,#0xff
	cmp	r3,#0xf2			@ done?
#endif
#if 1<15
# if __ARM_ARCH__>=7
	ldr	r2,[r1],#4			@ prefetch
# else
	ldrb	r2,[r1,#3]
# endif
	eor	r3,r11,r4			@ a^b, b^c in next round
#else
	ldr	r2,[sp,#3*4]		@ from future BODY_16_xx
	eor	r3,r11,r4			@ a^b, b^c in next round
	ldr	r1,[sp,#0*4]	@ from future BODY_16_xx
#endif
	eor	r0,r0,r11,ror#20	@ Sigma0(a)
	and	r12,r12,r3			@ (b^c)&=(a^b)
	add	r6,r6,r10			@ d+=h
	eor	r12,r12,r4			@ Maj(a,b,c)
	add	r10,r10,r0,ror#2	@ h+=Sigma0(a)
	@ add	r10,r10,r12			@ h+=Maj(a,b,c)
#if __ARM_ARCH__>=7
	@ ldr	r2,[r1],#4			@ 2
# if 2==15
	str	r1,[sp,#17*4]			@ make room for r1
# endif
	eor	r0,r6,r6,ror#5
	add	r10,r10,r12			@ h+=Maj(a,b,c) from the past
	eor	r0,r0,r6,ror#19	@ Sigma1(e)
# ifndef __ARMEB__
	rev	r2,r2
# endif
#else
	@ ldrb	r2,[r1,#3]			@ 2
	add	r10,r10,r12			@ h+=Maj(a,b,c) from the past
	ldrb	r12,[r1,#2]
	ldrb	r0,[r1,#1]
	orr	r2,r2,r12,lsl#8
	ldrb	r12,[r1],#4
	orr	r2,r2,r0,lsl#16
# if 2==15
	str	r1,[sp,#17*4]			@ make room for r1
# endif
	eor	r0,r6,r6,ror#5
	orr	r2,r2,r12,lsl#24
	eor	r0,r0,r6,ror#19	@ Sigma1(e)
#endif
	ldr	r12,[r14],#4			@ *K256++
	add	r9,r9,r2			@ h+=X[i]
	str	r2,[sp,#2*4]
	eor	r2,r7,r8
	add	r9,r9,r0,ror#6	@ h+=Sigma1(e)
	and	r2,r2,r6
	add	r9,r9,r12			@ h+=K256[i]
	eor	r2,r2,r8			@ Ch(e,f,g)
	eor	r0,r10,r10,ror#11
	add	r9,r9,r2			@ h+=Ch(e,f,g)
#if 2==31
	and	r12,r12,#0xff
	cmp	r12,#0xf2			@ done?
#endif
#if 2<15
# if __ARM_ARCH__>=7
	ldr	r2,[r1],#4			@ prefetch
# else
	ldrb	r2,[r1,#3]
# endif
	eor	r12,r10,r11			@ a^b, b^c in next round
#else
	ldr	r2,[sp,#4*4]		@ from future BODY_16_xx
	eor	r12,r10,r11			@ a^b, b^c in next round
	ldr	r1,[sp,#1*4]	@ from future BODY_16_xx
#endif
	eor	r0,r0,r10,ror#20	@ Sigma0(a)
	and	r3,r3,r12			@ (b^c)&=(a^b)
	add	r5,r5,r9			@ d+=h
	eor	r3,r3,r11			@ Maj(a,b,c)
	add	r9,r9,r0,ror#2	@ h+=Sigma0(a)
	@ add	r9,r9,r3			@ h+=Maj(a,b,c)
#if __ARM_ARCH__>=7
	@ ldr	r2,[r1],#4			@ 3
# if 3==15
	str	r1,[sp,#17*4]			@ make room for r1
# endif
	eor	r0,r5,r5,ror#5
	add	r9,r9,r3			@ h+=Maj(a,b,c) from the past
	eor	r0,r0,r5,ror#19	@ Sigma1(e)
# ifndef __ARMEB__
	rev	r2,r2
# endif
#else
	@ ldrb	r2,[r1,#3]			@ 3
	add	r9,r9,r3			@ h+=Maj(a,b,c) from the past
	ldrb	r3,[r1,#2]
	ldrb	r0,[r1,#1]
	orr	r2,r2,r3,lsl#8
	ldrb	r3,[r1],#4
	orr	r2,r2,r0,lsl#16
# if 3==15
	str	r1,[sp,#17*4]			@ make room for r1
# endif
	eor	r0,r5,r5,ror#5
	orr	r2,r2,r3,lsl#24
	eor	r0,r0,r5,ror#19	@ Sigma1(e)
#endif
	ldr	r3,[r14],#4			@ *K256++
	add	r8,r8,r2			@ h+=X[i]
	str	r2,[sp,#3*4]
	eor	r2,r6,r7
	add	r8,r8,r0,ror#6	@ h+=Sigma1(e)
	and	r2,r2,r5
	add	r8,r8,r3			@ h+=K256[i]
	eor	r2,r2,r7			@ Ch(e,f,g)
	eor	r0,r9,r9,ror#11
	add	r8,r8,r2			@ h+=Ch(e,f,g)
#if 3==31
	and	r3,r3,#0xff
	cmp	r3,#0xf2			@ done?
#endif
#if 3<15
# if __ARM_ARCH__>=7
	ldr	r2,[r1],#4			@ prefetch
# else
	ldrb	r2,[r1,#3]
# endif
	eor	r3,r9,r10			@ a^b, b^c in next round
#else
	ldr	r2,[sp,#5*4]		@ from future BODY_16_xx
	eor	r3,r9,r10			@ a^b, b^c in next round
	ldr	r1,[sp,#2*4]	@ from future BODY_16_xx
#endif
	eor	r0,r0,r9,ror#20	@ Sigma0(a)
	and	r12,r12,r3			@ (b^c)&=(a^b)
	add	r4,r4,r8			@ d+=h
	eor	r12,r12,r10			@ Maj(a,b,c)
	add	r8,r8,r0,ror#2	@ h+=Sigma0(a)
	@ add	r8,r8,r12			@ h+=Maj(a,b,c)
#if __ARM_ARCH__>=7
	@ ldr	r2,[r1],#4			@ 4
# if 4==15
	str	r1,[sp,#17*4]			@ make room for r1
# endif
	eor	r0,r4,r4,ror#5
	add	r8,r8,r12			@ h+=Maj(a,b,c) from the past
	eor	r0,r0,r4,ror#19	@ Sigma1(e)
# ifndef __ARMEB__
	rev	r2,r2
# endif
#else
	@ ldrb	r2,[r1,#3]			@ 4
	add	r8,r8,r12			@ h+=Maj(a,b,c) from the past
	ldrb	r12,[r1,#2]
	ldrb	r0,[r1,#1]
	orr	r2,r2,r12,lsl#8
	ldrb	r12,[r1],#4
	orr	r2,r2,r0,lsl#16
# if 4==15
	str	r1,[sp,#17*4]			@ make room for r1
# endif
	eor	r0,r4,r4,ror#5
	orr	r2,r2,r12,lsl#24
	eor	r0,r0,r4,ror#19	@ Sigma1(e)
#endif
	ldr	r12,[r14],#4			@ *K256++
	add	r7,r7,r2			@ h+=X[i]
	str	r2,[sp,#4*4]
	eor	r2,r5,r6
	add	r7,r7,r0,ror#6	@ h+=Sigma1(e)
	and	r2,r2,r4
	add	r7,r7,r12			@ h+=K256[i]
	eor	r2,r2,r6			@ Ch(e,f,g)
	eor	r0,r8,r8,ror#11
	add	r7,r7,r2			@ h+=Ch(e,f,g)
#if 4==31
	and	r12,r12,#0xff
	cmp	r12,#0xf2			@ done?
#endif
#if 4<15
# if __ARM_ARCH__>=7
	ldr	r2,[r1],#4			@ prefetch
# else
	ldrb	r2,[r1,#3]
# endif
	eor	r12,r8,r9			@ a^b, b^c in next round
#else
	ldr	r2,[sp,#6*4]		@ from future BODY_16_xx
	eor	r12,r8,r9			@ a^b, b^c in next round
	ldr	r1,[sp,#3*4]	@ from future BODY_16_xx
#endif
	eor	r0,r0,r8,ror#20	@ Sigma0(a)
	and	r3,r3,r12			@ (b^c)&=(a^b)
	add	r11,r11,r7			@ d+=h
	eor	r3,r3,r9			@ Maj(a,b,c)
	add	r7,r7,r0,ror#2	@ h+=Sigma0(a)
	@ add	r7,r7,r3			@ h+=Maj(a,b,c)
#if __ARM_ARCH__>=7
	@ ldr	r2,[r1],#4			@ 5
# if 5==15
	str	r1,[sp,#17*4]			@ make room for r1
# endif
	eor	r0,r11,r11,ror#5
	add	r7,r7,r3			@ h+=Maj(a,b,c) from the past
	eor	r0,r0,r11,ror#19	@ Sigma1(e)
# ifndef __ARMEB__
	rev	r2,r2
# endif
#else
	@ ldrb	r2,[r1,#3]			@ 5
	add	r7,r7,r3			@ h+=Maj(a,b,c) from the past
	ldrb	r3,[r1,#2]
	ldrb	r0,[r1,#1]
	orr	r2,r2,r3,lsl#8
	ldrb	r3,[r1],#4
	orr	r2,r2,r0,lsl#16
# if 5==15
	str	r1,[sp,#17*4]			@ make room for r1
# endif
	eor	r0,r11,r11,ror#5
	orr	r2,r2,r3,lsl#24
	eor	r0,r0,r11,ror#19	@ Sigma1(e)
#endif
	ldr	r3,[r14],#4			@ *K256++
	add	r6,r6,r2			@ h+=X[i]
	str	r2,[sp,#5*4]
	eor	r2,r4,r5
	add	r6,r6,r0,ror#6	@ h+=Sigma1(e)
	and	r2,r2,r11
	add	r6,r6,r3			@ h+=K256[i]
	eor	r2,r2,r5			@ Ch(e,f,g)
	eor	r0,r7,r7,ror#11
	add	r6,r6,r2			@ h+=Ch(e,f,g)
#if 5==31
	and	r3,r3,#0xff
	cmp	r3,#0xf2			@ done?
#endif
#if 5<15
# if __ARM_ARCH__>=7
	ldr	r2,[r1],#4			@ prefetch
# else
	ldrb	r2,[r1,#3]
# endif
	eor	r3,r7,r8			@ a^b, b^c in next round
#else
	ldr	r2,[sp,#7*4]		@ from future BODY_16_xx
	eor	r3,r7,r8			@ a^b, b^c in next round
	ldr	r1,[sp,#4*4]	@ from future BODY_16_xx
#endif
	eor	r0,r0,r7,ror#20	@ Sigma0(a)
	and	r12,r12,r3			@ (b^c)&=(a^b)
	add	r10,r10,r6			@ d+=h
	eor	r12,r12,r8			@ Maj(a,b,c)
	add	r6,r6,r0,ror#2	@ h+=Sigma0(a)
	@ add	r6,r6,r12			@ h+=Maj(a,b,c)
#if __ARM_ARCH__>=7
	@ ldr	r2,[r1],#4			@ 6
# if 6==15
	str	r1,[sp,#17*4]			@ make room for r1
# endif
	eor	r0,r10,r10,ror#5
	add	r6,r6,r12			@ h+=Maj(a,b,c) from the past
	eor	r0,r0,r10,ror#19	@ Sigma1(e)
# ifndef __ARMEB__
	rev	r2,r2
# endif
#else
	@ ldrb	r2,[r1,#3]			@ 6
	add	r6,r6,r12			@ h+=Maj(a,b,c) from the past
	ldrb	r12,[r1,#2]
	ldrb	r0,[r1,#1]
	orr	r2,r2,r12,lsl#8
	ldrb	r12,[r1],#4
	orr	r2,r2,r0,lsl#16
# if 6==15
	str	r1,[sp,#17*4]			@ make room for r1
# endif
	eor	r0,r10,r10,ror#5
	orr	r2,r2,r12,lsl#24
	eor	r0,r0,r10,ror#19	@ Sigma1(e)
#endif
	ldr	r12,[r14],#4			@ *K256++
	add	r5,r5,r2			@ h+=X[i]
	str	r2,[sp,#6*4]
	eor	r2,r11,r4
	add	r5,r5,r0,ror#6	@ h+=Sigma1(e)
	and	r2,r2,r10
	add	r5,r5,r12			@ h+=K256[i]
	eor	r2,r2,r4			@ Ch(e,f,g)
	eor	r0,r6,r6,ror#11
	add	r5,r5,r2			@ h+=Ch(e,f,g)
#if 6==31
	and	r12,r12,#0xff
	cmp	r12,#0xf2			@ done?
#endif
#if 6<15
# if __ARM_ARCH__>=7
	ldr	r2,[r1],#4			@ prefetch
# else
	ldrb	r2,[r1,#3]
# endif
	eor	r12,r6,r7			@ a^b, b^c in next round
#else
	ldr	r2,[sp,#8*4]		@ from future BODY_16_xx
	eor	r12,r6,r7			@ a^b, b^c in next round
	ldr	r1,[sp,#5*4]	@ from future BODY_16_xx
#endif
	eor	r0,r0,r6,ror#20	@ Sigma0(a)
	and	r3,r3,r12			@ (b^c)&=(a^b)
	add	r9,r9,r5			@ d+=h
	eor	r3,r3,r7			@ Maj(a,b,c)
	add	r5,r5,r0,ror#2	@ h+=Sigma0(a)
	@ add	r5,r5,r3			@ h+=Maj(a,b,c)
#if __ARM_ARCH__>=7
	@ ldr	r2,[r1],#4			@ 7
# if 7==15
	str	r1,[sp,#17*4]			@ make room for r1
# endif
	eor	r0,r9,r9,ror#5
	add	r5,r5,r3			@ h+=Maj(a,b,c) from the past
	eor	r0,r0,r9,ror#19	@ Sigma1(e)
# ifndef __ARMEB__
	rev	r2,r2
# endif
#else
	@ ldrb	r2,[r1,#3]			@ 7
	add	r5,r5,r3			@ h+=Maj(a,b,c) from the past
	ldrb	r3,[r1,#2]
	ldrb	r0,[r1,#1]
	orr	r2,r2,r3,lsl#8
	ldrb	r3,[r1],#4
	orr	r2,r2,r0,lsl#16
# if 7==15
	str	r1,[sp,#17*4]			@ make room for r1
# endif
	eor	r0,r9,r9,ror#5
	orr	r2,r2,r3,lsl#24
	eor	r0,r0,r9,ror#19	@ Sigma1(e)
#endif
	ldr	r3,[r14],#4			@ *K256++
	add	r4,r4,r2			@ h+=X[i]
	str	r2,[sp,#7*4]
	eor	r2,r10,r11
	add	r4,r4,r0,ror#6	@ h+=Sigma1(e)
	and	r2,r2,r9
	add	r4,r4,r3			@ h+=K256[i]
	eor	r2,r2,r11			@ Ch(e,f,g)
	eor	r0,r5,r5,ror#11
	add	r4,r4,r2			@ h+=Ch(e,f,g)
#if 7==31
	and	r3,r3,#0xff
	cmp	r3,#0xf2			@ done?
#endif
#if 7<15
# if __ARM_ARCH__>=7
	ldr	r2,[r1],#4			@ prefetch
# else
	ldrb	r2,[r1,#3]
# endif
	eor	r3,r5,r6			@ a^b, b^c in next round
#else
	ldr	r2,[sp,#9*4]		@ from future BODY_16_xx
	eor	r3,r5,r6			@ a^b, b^c in next round
	ldr	r1,[sp,#6*4]	@ from future BODY_16_xx
#endif
	eor	r0,r0,r5,ror#20	@ Sigma0(a)
	and	r12,r12,r3			@ (b^c)&=(a^b)
	add	r8,r8,r4			@ d+=h
	eor	r12,r12,r6			@ Maj(a,b,c)
	add	r4,r4,r0,ror#2	@ h+=Sigma0(a)
	@ add	r4,r4,r12			@ h+=Maj(a,b,c)
#if __ARM_ARCH__>=7
	@ ldr	r2,[r1],#4			@ 8
# if 8==15
	str	r1,[sp,#17*4]			@ make room for r1
# endif
	eor	r0,r8,r8,ror#5
	add	r4,r4,r12			@ h+=Maj(a,b,c) from the past
	eor	r0,r0,r8,ror#19	@ Sigma1(e)
# ifndef __ARMEB__
	rev	r2,r2
# endif
#else
	@ ldrb	r2,[r1,#3]			@ 8
	add	r4,r4,r12			@ h+=Maj(a,b,c) from the past
	ldrb	r12,[r1,#2]
	ldrb	r0,[r1,#1]
	orr	r2,r2,r12,lsl#8
	ldrb	r12,[r1],#4
	orr	r2,r2,r0,lsl#16
# if 8==15
	str	r1,[sp,#17*4]			@ make room for r1
# endif
	eor	r0,r8,r8,ror#5
	orr	r2,r2,r12,lsl#24
	eor	r0,r0,r8,ror#19	@ Sigma1(e)
#endif
	ldr	r12,[r14],#4			@ *K256++
	add	r11,r11,r2			@ h+=X[i]
	str	r2,[sp,#8*4]
	eor	r2,r9,r10
	add	r11,r11,r0,ror#6	@ h+=Sigma1(e)
	and	r2,r2,r8
	add	r11,r11,r12			@ h+=K256[i]
	eor	r2,r2,r10			@ Ch(e,f,g)
	eor	r0,r4,r4,ror#11
	add	r11,r11,r2			@ h+=Ch(e,f,g)
#if 8==31
	and	r12,r12,#0xff
	cmp	r12,#0xf2			@ done?
#endif
#if 8<15
# if __ARM_ARCH__>=7
	ldr	r2,[r1],#4			@ prefetch
# else
	ldrb	r2,[r1,#3]
# endif
	eor	r12,r4,r5			@ a^b, b^c in next round
#else
	ldr	r2,[sp,#10*4]		@ from future BODY_16_xx
	eor	r12,r4,r5			@ a^b, b^c in next round
	ldr	r1,[sp,#7*4]	@ from future BODY_16_xx
#endif
	eor	r0,r0,r4,ror#20	@ Sigma0(a)
	and	r3,r3,r12			@ (b^c)&=(a^b)
	add	r7,r7,r11			@ d+=h
	eor	r3,r3,r5			@ Maj(a,b,c)
	add	r11,r11,r0,ror#2	@ h+=Sigma0(a)
	@ add	r11,r11,r3			@ h+=Maj(a,b,c)
#if __ARM_ARCH__>=7
	@ ldr	r2,[r1],#4			@ 9
# if 9==15
	str	r1,[sp,#17*4]			@ make room for r1
# endif
	eor	r0,r7,r7,ror#5
	add	r11,r11,r3			@ h+=Maj(a,b,c) from the past
	eor	r0,r0,r7,ror#19	@ Sigma1(e)
# ifndef __ARMEB__
	rev	r2,r2
# endif
#else
	@ ldrb	r2,[r1,#3]			@ 9
	add	r11,r11,r3			@ h+=Maj(a,b,c) from the past
	ldrb	r3,[r1,#2]
	ldrb	r0,[r1,#1]
	orr	r2,r2,r3,lsl#8
	ldrb	r3,[r1],#4
	orr	r2,r2,r0,lsl#16
# if 9==15
	str	r1,[sp,#17*4]			@ make room for r1
# endif
	eor	r0,r7,r7,ror#5
	orr	r2,r2,r3,lsl#24
	eor	r0,r0,r7,ror#19	@ Sigma1(e)
#endif
	ldr	r3,[r14],#4			@ *K256++
	add	r10,r10,r2			@ h+=X[i]
	str	r2,[sp,#9*4]
	eor	r2,r8,r9
	add	r10,r10,r0,ror#6	@ h+=Sigma1(e)
	and	r2,r2,r7
	add	r10,r10,r3			@ h+=K256[i]
	eor	r2,r2,r9			@ Ch(e,f,g)
	eor	r0,r11,r11,ror#11
	add	r10,r10,r2			@ h+=Ch(e,f,g)
#if 9==31
	and	r3,r3,#0xff
	cmp	r3,#0xf2			@ done?
#endif
#if 9<15
# if __ARM_ARCH__>=7
	ldr	r2,[r1],#4			@ prefetch
# else
	ldrb	r2,[r1,#3]
# endif
	eor	r3,r11,r4			@ a^b, b^c in next round
#else
	ldr	r2,[sp,#11*4]		@ from future BODY_16_xx
	eor	r3,r11,r4			@ a^b, b^c in next round
	ldr	r1,[sp,#8*4]	@ from future BODY_16_xx
#endif
	eor	r0,r0,r11,ror#20	@ Sigma0(a)
	and	r12,r12,r3			@ (b^c)&=(a^b)
	add	r6,r6,r10			@ d+=h
	eor	r12,r12,r4			@ Maj(a,b,c)
	add	r10,r10,r0,ror#2	@ h+=Sigma0(a)
	@ add	r10,r10,r12			@ h+=Maj(a,b,c)
#if __ARM_ARCH__>=7
	@ ldr	r2,[r1],#4			@ 10
# if 10==15
	str	r1,[sp,#17*4]			@ make room for r1
# endif
	eor	r0,r6,r6,ror#5
	add	r10,r10,r12			@ h+=Maj(a,b,c) from the past
	eor	r0,r0,r6,ror#19	@ Sigma1(e)
# ifndef __ARMEB__
	rev	r2,r2
# endif
#else
	@ ldrb	r2,[r1,#3]			@ 10
	add	r10,r10,r12			@ h+=Maj(a,b,c) from the past
	ldrb	r12,[r1,#2]
	ldrb	r0,[r1,#1]
	orr	r2,r2,r12,lsl#8
	ldrb	r12,[r1],#4
	orr	r2,r2,r0,lsl#16
# if 10==15
	str	r1,[sp,#17*4]			@ make room for r1
# endif
	eor	r0,r6,r6,ror#5
	orr	r2,r2,r12,lsl#24
	eor	r0,r0,r6,ror#19	@ Sigma1(e)
#endif
	ldr	r12,[r14],#4			@ *K256++
	add	r9,r9,r2			@ h+=X[i]
	str	r2,[sp,#10*4]
	eor	r2,r7,r8
	add	r9,r9,r0,ror#6	@ h+=Sigma1(e)
	and	r2,r2,r6
	add	r9,r9,r12			@ h+=K256[i]
	eor	r2,r2,r8			@ Ch(e,f,g)
	eor	r0,r10,r10,ror#11
	add	r9,r9,r2			@ h+=Ch(e,f,g)
#if 10==31
	and	r12,r12,#0xff
	cmp	r12,#0xf2			@ done?
#endif
#if 10<15
# if __ARM_ARCH__>=7
	ldr	r2,[r1],#4			@ prefetch
# else
	ldrb	r2,[r1,#3]
# endif
	eor	r12,r10,r11			@ a^b, b^c in next round
#else
	ldr	r2,[sp,#12*4]		@ from future BODY_16_xx
	eor	r12,r10,r11			@ a^b, b^c in next round
	ldr	r1,[sp,#9*4]	@ from future BODY_16_xx
#endif
	eor	r0,r0,r10,ror#20	@ Sigma0(a)
	and	r3,r3,r12			@ (b^c)&=(a^b)
	add	r5,r5,r9			@ d+=h
	eor	r3,r3,r11			@ Maj(a,b,c)
	add	r9,r9,r0,ror#2	@ h+=Sigma0(a)
	@ add	r9,r9,r3			@ h+=Maj(a,b,c)
#if __ARM_ARCH__>=7
	@ ldr	r2,[r1],#4			@ 11
# if 11==15
	str	r1,[sp,#17*4]			@ make room for r1
# endif
	eor	r0,r5,r5,ror#5
	add	r9,r9,r3			@ h+=Maj(a,b,c) from the past
	eor	r0,r0,r5,ror#19	@ Sigma1(e)
# ifndef __ARMEB__
	rev	r2,r2
# endif
#else
	@ ldrb	r2,[r1,#3]			@ 11
	add	r9,r9,r3			@ h+=Maj(a,b,c) from the past
	ldrb	r3,[r1,#2]
	ldrb	r0,[r1,#1]
	orr	r2,r2,r3,lsl#8
	ldrb	r3,[r1],#4
	orr	r2,r2,r0,lsl#16
# if 11==15
	str	r1,[sp,#17*4]			@ make room for r1
# endif
	eor	r0,r5,r5,ror#5
	orr	r2,r2,r3,lsl#24
	eor	r0,r0,r5,ror#19	@ Sigma1(e)
#endif
	ldr	r3,[r14],#4			@ *K256++
	add	r8,r8,r2			@ h+=X[i]
	str	r2,[sp,#11*4]
	eor	r2,r6,r7
	add	r8,r8,r0,ror#6	@ h+=Sigma1(e)
	and	r2,r2,r5
	add	r8,r8,r3			@ h+=K256[i]
	eor	r2,r2,r7			@ Ch(e,f,g)
	eor	r0,r9,r9,ror#11
	add	r8,r8,r2			@ h+=Ch(e,f,g)
#if 11==31
	and	r3,r3,#0xff
	cmp	r3,#0xf2			@ done?
#endif
#if 11<15
# if __ARM_ARCH__>=7
	ldr	r2,[r1],#4			@ prefetch
# else
	ldrb	r2,[r1,#3]
# endif
	eor	r3,r9,r10			@ a^b, b^c in next round
#else
	ldr	r2,[sp,#13*4]		@ from future BODY_16_xx
	eor	r3,r9,r10			@ a^b, b^c in next round
	ldr	r1,[sp,#10*4]	@ from future BODY_16_xx
#endif
	eor	r0,r0,r9,ror#20	@ Sigma0(a)
	and	r12,r12,r3			@ (b^c)&=(a^b)
	add	r4,r4,r8			@ d+=h
	eor	r12,r12,r10			@ Maj(a,b,c)
	add	r8,r8,r0,ror#2	@ h+=Sigma0(a)
	@ add	r8,r8,r12			@ h+=Maj(a,b,c)
#if __ARM_ARCH__>=7
	@ ldr	r2,[r1],#4			@ 12
# if 12==15
	str	r1,[sp,#17*4]			@ make room for r1
# endif
	eor	r0,r4,r4,ror#5
	add	r8,r8,r12			@ h+=Maj(a,b,c) from the past
	eor	r0,r0,r4,ror#19	@ Sigma1(e)
# ifndef __ARMEB__
	rev	r2,r2
# endif
#else
	@ ldrb	r2,[r1,#3]			@ 12
	add	r8,r8,r12			@ h+=Maj(a,b,c) from the past
	ldrb	r12,[r1,#2]
	ldrb	r0,[r1,#1]
	orr	r2,r2,r12,lsl#8
	ldrb	r12,[r1],#4
	orr	r2,r2,r0,lsl#16
# if 12==15
	str	r1,[sp,#17*4]			@ make room for r1
# endif
	eor	r0,r4,r4,ror#5
	orr	r2,r2,r12,lsl#24
	eor	r0,r0,r4,ror#19	@ Sigma1(e)
#endif
	ldr	r12,[r14],#4			@ *K256++
	add	r7,r7,r2			@ h+=X[i]
	str	r2,[sp,#12*4]
	eor	r2,r5,r6
	add	r7,r7,r0,ror#6	@ h+=Sigma1(e)
	and	r2,r2,r4
	add	r7,r7,r12			@ h+=K256[i]
	eor	r2,r2,r6			@ Ch(e,f,g)
	eor	r0,r8,r8,ror#11
	add	r7,r7,r2			@ h+=Ch(e,f,g)
#if 12==31
	and	r12,r12,#0xff
	cmp	r12,#0xf2			@ done?
#endif
#if 12<15
# if __ARM_ARCH__>=7
	ldr	r2,[r1],#4			@ prefetch
# else
	ldrb	r2,[r1,#3]
# endif
	eor	r12,r8,r9			@ a^b, b^c in next round
#else
	ldr	r2,[sp,#14*4]		@ from future BODY_16_xx
	eor	r12,r8,r9			@ a^b, b^c in next round
	ldr	r1,[sp,#11*4]	@ from future BODY_16_xx
#endif
	eor	r0,r0,r8,ror#20	@ Sigma0(a)
	and	r3,r3,r12			@ (b^c)&=(a^b)
	add	r11,r11,r7			@ d+=h
	eor	r3,r3,r9			@ Maj(a,b,c)
	add	r7,r7,r0,ror#2	@ h+=Sigma0(a)
	@ add	r7,r7,r3			@ h+=Maj(a,b,c)
#if __ARM_ARCH__>=7
	@ ldr	r2,[r1],#4			@ 13
# if 13==15
	str	r1,[sp,#17*4]			@ make room for r1
# endif
	eor	r0,r11,r11,ror#5
	add	r7,r7,r3			@ h+=Maj(a,b,c) from the past
	eor	r0,r0,r11,ror#19	@ Sigma1(e)
# ifndef __ARMEB__
	rev	r2,r2
# endif
#else
	@ ldrb	r2,[r1,#3]			@ 13
	add	r7,r7,r3			@ h+=Maj(a,b,c) from the past
	ldrb	r3,[r1,#2]
	ldrb	r0,[r1,#1]
	orr	r2,r2,r3,lsl#8
	ldrb	r3,[r1],#4
	orr	r2,r2,r0,lsl#16
# if 13==15
	str	r1,[sp,#17*4]			@ make room for r1
# endif
	eor	r0,r11,r11,ror#5
	orr	r2,r2,r3,lsl#24
	eor	r0,r0,r11,ror#19	@ Sigma1(e)
#endif
	ldr	r3,[r14],#4			@ *K256++
	add	r6,r6,r2			@ h+=X[i]
	str	r2,[sp,#13*4]
	eor	r2,r4,r5
	add	r6,r6,r0,ror#6	@ h+=Sigma1(e)
	and	r2,r2,r11
	add	r6,r6,r3			@ h+=K256[i]
	eor	r2,r2,r5			@ Ch(e,f,g)
	eor	r0,r7,r7,ror#11
	add	r6,r6,r2			@ h+=Ch(e,f,g)
#if 13==31
	and	r3,r3,#0xff
	cmp	r3,#0xf2			@ done?
#endif
#if 13<15
# if __ARM_ARCH__>=7
	ldr	r2,[r1],#4			@ prefetch
# else
	ldrb	r2,[r1,#3]
# endif
	eor	r3,r7,r8			@ a^b, b^c in next round
#else
	ldr	r2,[sp,#15*4]		@ from future BODY_16_xx
	eor	r3,r7,r8			@ a^b, b^c in next round
	ldr	r1,[sp,#12*4]	@ from future BODY_16_xx
#endif
	eor	r0,r0,r7,ror#20	@ Sigma0(a)
	and	r12,r12,r3			@ (b^c)&=(a^b)
	add	r10,r10,r6			@ d+=h
	eor	r12,r12,r8			@ Maj(a,b,c)
	add	r6,r6,r0,ror#2	@ h+=Sigma0(a)
	@ add	r6,r6,r12			@ h+=Maj(a,b,c)
#if __ARM_ARCH__>=7
	@ ldr	r2,[r1],#4			@ 14
# if 14==15
	str	r1,[sp,#17*4]			@ make room for r1
# endif
	eor	r0,r10,r10,ror#5
	add	r6,r6,r12			@ h+=Maj(a,b,c) from the past
	eor	r0,r0,r10,ror#19	@ Sigma1(e)
# ifndef __ARMEB__
	rev	r2,r2
# endif
#else
	@ ldrb	r2,[r1,#3]			@ 14
	add	r6,r6,r12			@ h+=Maj(a,b,c) from the past
	ldrb	r12,[r1,#2]
	ldrb	r0,[r1,#1]
	orr	r2,r2,r12,lsl#8
	ldrb	r12,[r1],#4
	orr	r2,r2,r0,lsl#16
# if 14==15
	str	r1,[sp,#17*4]			@ make room for r1
# endif
	eor	r0,r10,r10,ror#5
	orr	r2,r2,r12,lsl#24
	eor	r0,r0,r10,ror#19	@ Sigma1(e)
#endif
	ldr	r12,[r14],#4			@ *K256++
	add	r5,r5,r2			@ h+=X[i]
	str	r2,[sp,#14*4]
	eor	r2,r11,r4
	add	r5,r5,r0,ror#6	@ h+=Sigma1(e)
	and	r2,r2,r10
	add	r5,r5,r12			@ h+=K256[i]
	eor	r2,r2,r4			@ Ch(e,f,g)
	eor	r0,r6,r6,ror#11
	add	r5,r5,r2			@ h+=Ch(e,f,g)
#if 14==31
	and	r12,r12,#0xff
	cmp	r12,#0xf2			@ done?
#endif
#if 14<15
# if __ARM_ARCH__>=7
	ldr	r2,[r1],#4			@ prefetch
# else
	ldrb	r2,[r1,#3]
# endif
	eor	r12,r6,r7			@ a^b, b^c in next round
#else
	ldr	r2,[sp,#0*4]		@ from future BODY_16_xx
	eor	r12,r6,r7			@ a^b, b^c in next round
	ldr	r1,[sp,#13*4]	@ from future BODY_16_xx
#endif
	eor	r0,r0,r6,ror#20	@ Sigma0(a)
	and	r3,r3,r12			@ (b^c)&=(a^b)
	add	r9,r9,r5			@ d+=h
	eor	r3,r3,r7			@ Maj(a,b,c)
	add	r5,r5,r0,ror#2	@ h+=Sigma0(a)
	@ add	r5,r5,r3			@ h+=Maj(a,b,c)
#if __ARM_ARCH__>=7
	@ ldr	r2,[r1],#4			@ 15
# if 15==15
	str	r1,[sp,#17*4]			@ make room for r1
# endif
	eor	r0,r9,r9,ror#5
	add	r5,r5,r3			@ h+=Maj(a,b,c) from the past
	eor	r0,r0,r9,ror#19	@ Sigma1(e)
# ifndef __ARMEB__
	rev	r2,r2
# endif
#else
	@ ldrb	r2,[r1,#3]			@ 15
	add	r5,r5,r3			@ h+=Maj(a,b,c) from the past
	ldrb	r3,[r1,#2]
	ldrb	r0,[r1,#1]
	orr	r2,r2,r3,lsl#8
	ldrb	r3,[r1],#4
	orr	r2,r2,r0,lsl#16
# if 15==15
	str	r1,[sp,#17*4]			@ make room for r1
# endif
	eor	r0,r9,r9,ror#5
	orr	r2,r2,r3,lsl#24
	eor	r0,r0,r9,ror#19	@ Sigma1(e)
#endif
	ldr	r3,[r14],#4			@ *K256++
	add	r4,r4,r2			@ h+=X[i]
	str	r2,[sp,#15*4]
	eor	r2,r10,r11
	add	r4,r4,r0,ror#6	@ h+=Sigma1(e)
	and	r2,r2,r9
	add	r4,r4,r3			@ h+=K256[i]
	eor	r2,r2,r11			@ Ch(e,f,g)
	eor	r0,r5,r5,ror#11
	add	r4,r4,r2			@ h+=Ch(e,f,g)
#if 15==31
	and	r3,r3,#0xff
	cmp	r3,#0xf2			@ done?
#endif
#if 15<15
# if __ARM_ARCH__>=7
	ldr	r2,[r1],#4			@ prefetch
# else
	ldrb	r2,[r1,#3]
# endif
	eor	r3,r5,r6			@ a^b, b^c in next round
#else
	ldr	r2,[sp,#1*4]		@ from future BODY_16_xx
	eor	r3,r5,r6			@ a^b, b^c in next round
	ldr	r1,[sp,#14*4]	@ from future BODY_16_xx
#endif
	eor	r0,r0,r5,ror#20	@ Sigma0(a)
	and	r12,r12,r3			@ (b^c)&=(a^b)
	add	r8,r8,r4			@ d+=h
	eor	r12,r12,r6			@ Maj(a,b,c)
	add	r4,r4,r0,ror#2	@ h+=Sigma0(a)
	@ add	r4,r4,r12			@ h+=Maj(a,b,c)
.Lrounds_16_xx:
	@ ldr	r2,[sp,#1*4]		@ 16
	@ ldr	r1,[sp,#14*4]
	mov	r0,r2,ror#7
	add	r4,r4,r12			@ h+=Maj(a,b,c) from the past
	mov	r12,r1,ror#17
	eor	r0,r0,r2,ror#18
	eor	r12,r12,r1,ror#19
	eor	r0,r0,r2,lsr#3	@ sigma0(X[i+1])
	ldr	r2,[sp,#0*4]
	eor	r12,r12,r1,lsr#10	@ sigma1(X[i+14])
	ldr	r1,[sp,#9*4]

	add	r12,r12,r0
	eor	r0,r8,r8,ror#5	@ from BODY_00_15
	add	r2,r2,r12
	eor	r0,r0,r8,ror#19	@ Sigma1(e)
	add	r2,r2,r1			@ X[i]
	ldr	r12,[r14],#4			@ *K256++
	add	r11,r11,r2			@ h+=X[i]
	str	r2,[sp,#0*4]
	eor	r2,r9,r10
	add	r11,r11,r0,ror#6	@ h+=Sigma1(e)
	and	r2,r2,r8
	add	r11,r11,r12			@ h+=K256[i]
	eor	r2,r2,r10			@ Ch(e,f,g)
	eor	r0,r4,r4,ror#11
	add	r11,r11,r2			@ h+=Ch(e,f,g)
#if 16==31
	and	r12,r12,#0xff
	cmp	r12,#0xf2			@ done?
#endif
#if 16<15
# if __ARM_ARCH__>=7
	ldr	r2,[r1],#4			@ prefetch
# else
	ldrb	r2,[r1,#3]
# endif
	eor	r12,r4,r5			@ a^b, b^c in next round
#else
	ldr	r2,[sp,#2*4]		@ from future BODY_16_xx
	eor	r12,r4,r5			@ a^b, b^c in next round
	ldr	r1,[sp,#15*4]	@ from future BODY_16_xx
#endif
	eor	r0,r0,r4,ror#20	@ Sigma0(a)
	and	r3,r3,r12			@ (b^c)&=(a^b)
	add	r7,r7,r11			@ d+=h
	eor	r3,r3,r5			@ Maj(a,b,c)
	add	r11,r11,r0,ror#2	@ h+=Sigma0(a)
	@ add	r11,r11,r3			@ h+=Maj(a,b,c)
	@ ldr	r2,[sp,#2*4]		@ 17
	@ ldr	r1,[sp,#15*4]
	mov	r0,r2,ror#7
	add	r11,r11,r3			@ h+=Maj(a,b,c) from the past
	mov	r3,r1,ror#17
	eor	r0,r0,r2,ror#18
	eor	r3,r3,r1,ror#19
	eor	r0,r0,r2,lsr#3	@ sigma0(X[i+1])
	ldr	r2,[sp,#1*4]
	eor	r3,r3,r1,lsr#10	@ sigma1(X[i+14])
	ldr	r1,[sp,#10*4]

	add	r3,r3,r0
	eor	r0,r7,r7,ror#5	@ from BODY_00_15
	add	r2,r2,r3
	eor	r0,r0,r7,ror#19	@ Sigma1(e)
	add	r2,r2,r1			@ X[i]
	ldr	r3,[r14],#4			@ *K256++
	add	r10,r10,r2			@ h+=X[i]
	str	r2,[sp,#1*4]
	eor	r2,r8,r9
	add	r10,r10,r0,ror#6	@ h+=Sigma1(e)
	and	r2,r2,r7
	add	r10,r10,r3			@ h+=K256[i]
	eor	r2,r2,r9			@ Ch(e,f,g)
	eor	r0,r11,r11,ror#11
	add	r10,r10,r2			@ h+=Ch(e,f,g)
#if 17==31
	and	r3,r3,#0xff
	cmp	r3,#0xf2			@ done?
#endif
#if 17<15
# if __ARM_ARCH__>=7
	ldr	r2,[r1],#4			@ prefetch
# else
	ldrb	r2,[r1,#3]
# endif
	eor	r3,r11,r4			@ a^b, b^c in next round
#else
	ldr	r2,[sp,#3*4]		@ from future BODY_16_xx
	eor	r3,r11,r4			@ a^b, b^c in next round
	ldr	r1,[sp,#0*4]	@ from future BODY_16_xx
#endif
	eor	r0,r0,r11,ror#20	@ Sigma0(a)
	and	r12,r12,r3			@ (b^c)&=(a^b)
	add	r6,r6,r10			@ d+=h
	eor	r12,r12,r4			@ Maj(a,b,c)
	add	r10,r10,r0,ror#2	@ h+=Sigma0(a)
	@ add	r10,r10,r12			@ h+=Maj(a,b,c)
	@ ldr	r2,[sp,#3*4]		@ 18
	@ ldr	r1,[sp,#0*4]
	mov	r0,r2,ror#7
	add	r10,r10,r12			@ h+=Maj(a,b,c) from the past
	mov	r12,r1,ror#17
	eor	r0,r0,r2,ror#18
	eor	r12,r12,r1,ror#19
	eor	r0,r0,r2,lsr#3	@ sigma0(X[i+1])
	ldr	r2,[sp,#2*4]
	eor	r12,r12,r1,lsr#10	@ sigma1(X[i+14])
	ldr	r1,[sp,#11*4]

	add	r12,r12,r0
	eor	r0,r6,r6,ror#5	@ from BODY_00_15
	add	r2,r2,r12
	eor	r0,r0,r6,ror#19	@ Sigma1(e)
	add	r2,r2,r1			@ X[i]
	ldr	r12,[r14],#4			@ *K256++
	add	r9,r9,r2			@ h+=X[i]
	str	r2,[sp,#2*4]
	eor	r2,r7,r8
	add	r9,r9,r0,ror#6	@ h+=Sigma1(e)
	and	r2,r2,r6
	add	r9,r9,r12			@ h+=K256[i]
	eor	r2,r2,r8			@ Ch(e,f,g)
	eor	r0,r10,r10,ror#11
	add	r9,r9,r2			@ h+=Ch(e,f,g)
#if 18==31
	and	r12,r12,#0xff
	cmp	r12,#0xf2			@ done?
#endif
#if 18<15
# if __ARM_ARCH__>=7
	ldr	r2,[r1],#4			@ prefetch
# else
	ldrb	r2,[r1,#3]
# endif
	eor	r12,r10,r11			@ a^b, b^c in next round
#else
	ldr	r2,[sp,#4*4]		@ from future BODY_16_xx
	eor	r12,r10,r11			@ a^b, b^c in next round
	ldr	r1,[sp,#1*4]	@ from future BODY_16_xx
#endif
	eor	r0,r0,r10,ror#20	@ Sigma0(a)
	and	r3,r3,r12			@ (b^c)&=(a^b)
	add	r5,r5,r9			@ d+=h
	eor	r3,r3,r11			@ Maj(a,b,c)
	add	r9,r9,r0,ror#2	@ h+=Sigma0(a)
	@ add	r9,r9,r3			@ h+=Maj(a,b,c)
	@ ldr	r2,[sp,#4*4]		@ 19
	@ ldr	r1,[sp,#1*4]
	mov	r0,r2,ror#7
	add	r9,r9,r3			@ h+=Maj(a,b,c) from the past
	mov	r3,r1,ror#17
	eor	r0,r0,r2,ror#18
	eor	r3,r3,r1,ror#19
	eor	r0,r0,r2,lsr#3	@ sigma0(X[i+1])
	ldr	r2,[sp,#3*4]
	eor	r3,r3,r1,lsr#10	@ sigma1(X[i+14])
	ldr	r1,[sp,#12*4]

	add	r3,r3,r0
	eor	r0,r5,r5,ror#5	@ from BODY_00_15
	add	r2,r2,r3
	eor	r0,r0,r5,ror#19	@ Sigma1(e)
	add	r2,r2,r1			@ X[i]
	ldr	r3,[r14],#4			@ *K256++
	add	r8,r8,r2			@ h+=X[i]
	str	r2,[sp,#3*4]
	eor	r2,r6,r7
	add	r8,r8,r0,ror#6	@ h+=Sigma1(e)
	and	r2,r2,r5
	add	r8,r8,r3			@ h+=K256[i]
	eor	r2,r2,r7			@ Ch(e,f,g)
	eor	r0,r9,r9,ror#11
	add	r8,r8,r2			@ h+=Ch(e,f,g)
#if 19==31
	and	r3,r3,#0xff
	cmp	r3,#0xf2			@ done?
#endif
#if 19<15
# if __ARM_ARCH__>=7
	ldr	r2,[r1],#4			@ prefetch
# else
	ldrb	r2,[r1,#3]
# endif
	eor	r3,r9,r10			@ a^b, b^c in next round
#else
	ldr	r2,[sp,#5*4]		@ from future BODY_16_xx
	eor	r3,r9,r10			@ a^b, b^c in next round
	ldr	r1,[sp,#2*4]	@ from future BODY_16_xx
#endif
	eor	r0,r0,r9,ror#20	@ Sigma0(a)
	and	r12,r12,r3			@ (b^c)&=(a^b)
	add	r4,r4,r8			@ d+=h
	eor	r12,r12,r10			@ Maj(a,b,c)
	add	r8,r8,r0,ror#2	@ h+=Sigma0(a)
	@ add	r8,r8,r12			@ h+=Maj(a,b,c)
	@ ldr	r2,[sp,#5*4]		@ 20
	@ ldr	r1,[sp,#2*4]
	mov	r0,r2,ror#7
	add	r8,r8,r12			@ h+=Maj(a,b,c) from the past
	mov	r12,r1,ror#17
	eor	r0,r0,r2,ror#18
	eor	r12,r12,r1,ror#19
	eor	r0,r0,r2,lsr#3	@ sigma0(X[i+1])
	ldr	r2,[sp,#4*4]
	eor	r12,r12,r1,lsr#10	@ sigma1(X[i+14])
	ldr	r1,[sp,#13*4]

	add	r12,r12,r0
	eor	r0,r4,r4,ror#5	@ from BODY_00_15
	add	r2,r2,r12
	eor	r0,r0,r4,ror#19	@ Sigma1(e)
	add	r2,r2,r1			@ X[i]
	ldr	r12,[r14],#4			@ *K256++
	add	r7,r7,r2			@ h+=X[i]
	str	r2,[sp,#4*4]
	eor	r2,r5,r6
	add	r7,r7,r0,ror#6	@ h+=Sigma1(e)
	and	r2,r2,r4
	add	r7,r7,r12			@ h+=K256[i]
	eor	r2,r2,r6			@ Ch(e,f,g)
	eor	r0,r8,r8,ror#11
	add	r7,r7,r2			@ h+=Ch(e,f,g)
#if 20==31
	and	r12,r12,#0xff
	cmp	r12,#0xf2			@ done?
#endif
#if 20<15
# if __ARM_ARCH__>=7
	ldr	r2,[r1],#4			@ prefetch
# else
	ldrb	r2,[r1,#3]
# endif
	eor	r12,r8,r9			@ a^b, b^c in next round
#else
	ldr	r2,[sp,#6*4]		@ from future BODY_16_xx
	eor	r12,r8,r9			@ a^b, b^c in next round
	ldr	r1,[sp,#3*4]	@ from future BODY_16_xx
#endif
	eor	r0,r0,r8,ror#20	@ Sigma0(a)
	and	r3,r3,r12			@ (b^c)&=(a^b)
	add	r11,r11,r7			@ d+=h
	eor	r3,r3,r9			@ Maj(a,b,c)
	add	r7,r7,r0,ror#2	@ h+=Sigma0(a)
	@ add	r7,r7,r3			@ h+=Maj(a,b,c)
	@ ldr	r2,[sp,#6*4]		@ 21
	@ ldr	r1,[sp,#3*4]
	mov	r0,r2,ror#7
	add	r7,r7,r3			@ h+=Maj(a,b,c) from the past
	mov	r3,r1,ror#17
	eor	r0,r0,r2,ror#18
	eor	r3,r3,r1,ror#19
	eor	r0,r0,r2,lsr#3	@ sigma0(X[i+1])
	ldr	r2,[sp,#5*4]
	eor	r3,r3,r1,lsr#10	@ sigma1(X[i+14])
	ldr	r1,[sp,#14*4]

	add	r3,r3,r0
	eor	r0,r11,r11,ror#5	@ from BODY_00_15
	add	r2,r2,r3
	eor	r0,r0,r11,ror#19	@ Sigma1(e)
	add	r2,r2,r1			@ X[i]
	ldr	r3,[r14],#4			@ *K256++
	add	r6,r6,r2			@ h+=X[i]
	str	r2,[sp,#5*4]
	eor	r2,r4,r5
	add	r6,r6,r0,ror#6	@ h+=Sigma1(e)
	and	r2,r2,r11
	add	r6,r6,r3			@ h+=K256[i]
	eor	r2,r2,r5			@ Ch(e,f,g)
	eor	r0,r7,r7,ror#11
	add	r6,r6,r2			@ h+=Ch(e,f,g)
#if 21==31
	and	r3,r3,#0xff
	cmp	r3,#0xf2			@ done?
#endif
#if 21<15
# if __ARM_ARCH__>=7
	ldr	r2,[r1],#4			@ prefetch
# else
	ldrb	r2,[r1,#3]
# endif
	eor	r3,r7,r8			@ a^b, b^c in next round
#else
	ldr	r2,[sp,#7*4]		@ from future BODY_16_xx
	eor	r3,r7,r8			@ a^b, b^c in next round
	ldr	r1,[sp,#4*4]	@ from future BODY_16_xx
#endif
	eor	r0,r0,r7,ror#20	@ Sigma0(a)
	and	r12,r12,r3			@ (b^c)&=(a^b)
	add	r10,r10,r6			@ d+=h
	eor	r12,r12,r8			@ Maj(a,b,c)
	add	r6,r6,r0,ror#2	@ h+=Sigma0(a)
	@ add	r6,r6,r12			@ h+=Maj(a,b,c)
	@ ldr	r2,[sp,#7*4]		@ 22
	@ ldr	r1,[sp,#4*4]
	mov	r0,r2,ror#7
	add	r6,r6,r12			@ h+=Maj(a,b,c) from the past
	mov	r12,r1,ror#17
	eor	r0,r0,r2,ror#18
	eor	r12,r12,r1,ror#19
	eor	r0,r0,r2,lsr#3	@ sigma0(X[i+1])
	ldr	r2,[sp,#6*4]
	eor	r12,r12,r1,lsr#10	@ sigma1(X[i+14])
	ldr	r1,[sp,#15*4]

	add	r12,r12,r0
	eor	r0,r10,r10,ror#5	@ from BODY_00_15
	add	r2,r2,r12
	eor	r0,r0,r10,ror#19	@ Sigma1(e)
	add	r2,r2,r1			@ X[i]
	ldr	r12,[r14],#4			@ *K256++
	add	r5,r5,r2			@ h+=X[i]
	str	r2,[sp,#6*4]
	eor	r2,r11,r4
	add	r5,r5,r0,ror#6	@ h+=Sigma1(e)
	and	r2,r2,r10
	add	r5,r5,r12			@ h+=K256[i]
	eor	r2,r2,r4			@ Ch(e,f,g)
	eor	r0,r6,r6,ror#11
	add	r5,r5,r2			@ h+=Ch(e,f,g)
#if 22==31
	and	r12,r12,#0xff
	cmp	r12,#0xf2			@ done?
#endif
#if 22<15
# if __ARM_ARCH__>=7
	ldr	r2,[r1],#4			@ prefetch
# else
	ldrb	r2,[r1,#3]
# endif
	eor	r12,r6,r7			@ a^b, b^c in next round
#else
	ldr	r2,[sp,#8*4]		@ from future BODY_16_xx
	eor	r12,r6,r7			@ a^b, b^c in next round
	ldr	r1,[sp,#5*4]	@ from future BODY_16_xx
#endif
	eor	r0,r0,r6,ror#20	@ Sigma0(a)
	and	r3,r3,r12			@ (b^c)&=(a^b)
	add	r9,r9,r5			@ d+=h
	eor	r3,r3,r7			@ Maj(a,b,c)
	add	r5,r5,r0,ror#2	@ h+=Sigma0(a)
	@ add	r5,r5,r3			@ h+=Maj(a,b,c)
	@ ldr	r2,[sp,#8*4]		@ 23
	@ ldr	r1,[sp,#5*4]
	mov	r0,r2,ror#7
	add	r5,r5,r3			@ h+=Maj(a,b,c) from the past
	mov	r3,r1,ror#17
	eor	r0,r0,r2,ror#18
	eor	r3,r3,r1,ror#19
	eor	r0,r0,r2,lsr#3	@ sigma0(X[i+1])
	ldr	r2,[sp,#7*4]
	eor	r3,r3,r1,lsr#10	@ sigma1(X[i+14])
	ldr	r1,[sp,#0*4]

	add	r3,r3,r0
	eor	r0,r9,r9,ror#5	@ from BODY_00_15
	add	r2,r2,r3
	eor	r0,r0,r9,ror#19	@ Sigma1(e)
	add	r2,r2,r1			@ X[i]
	ldr	r3,[r14],#4			@ *K256++
	add	r4,r4,r2			@ h+=X[i]
	str	r2,[sp,#7*4]
	eor	r2,r10,r11
	add	r4,r4,r0,ror#6	@ h+=Sigma1(e)
	and	r2,r2,r9
	add	r4,r4,r3			@ h+=K256[i]
	eor	r2,r2,r11			@ Ch(e,f,g)
	eor	r0,r5,r5,ror#11
	add	r4,r4,r2			@ h+=Ch(e,f,g)
#if 23==31
	and	r3,r3,#0xff
	cmp	r3,#0xf2			@ done?
#endif
#if 23<15
# if __ARM_ARCH__>=7
	ldr	r2,[r1],#4			@ prefetch
# else
	ldrb	r2,[r1,#3]
# endif
	eor	r3,r5,r6			@ a^b, b^c in next round
#else
	ldr	r2,[sp,#9*4]		@ from future BODY_16_xx
	eor	r3,r5,r6			@ a^b, b^c in next round
	ldr	r1,[sp,#6*4]	@ from future BODY_16_xx
#endif
	eor	r0,r0,r5,ror#20	@ Sigma0(a)
	and	r12,r12,r3			@ (b^c)&=(a^b)
	add	r8,r8,r4			@ d+=h
	eor	r12,r12,r6			@ Maj(a,b,c)
	add	r4,r4,r0,ror#2	@ h+=Sigma0(a)
	@ add	r4,r4,r12			@ h+=Maj(a,b,c)
	@ ldr	r2,[sp,#9*4]		@ 24
	@ ldr	r1,[sp,#6*4]
	mov	r0,r2,ror#7
	add	r4,r4,r12			@ h+=Maj(a,b,c) from the past
	mov	r12,r1,ror#17
	eor	r0,r0,r2,ror#18
	eor	r12,r12,r1,ror#19
	eor	r0,r0,r2,lsr#3	@ sigma0(X[i+1])
	ldr	r2,[sp,#8*4]
	eor	r12,r12,r1,lsr#10	@ sigma1(X[i+14])
	ldr	r1,[sp,#1*4]

	add	r12,r12,r0
	eor	r0,r8,r8,ror#5	@ from BODY_00_15
	add	r2,r2,r12
	eor	r0,r0,r8,ror#19	@ Sigma1(e)
	add	r2,r2,r1			@ X[i]
	ldr	r12,[r14],#4			@ *K256++
	add	r11,r11,r2			@ h+=X[i]
	str	r2,[sp,#8*4]
	eor	r2,r9,r10
	add	r11,r11,r0,ror#6	@ h+=Sigma1(e)
	and	r2,r2,r8
	add	r11,r11,r12			@ h+=K256[i]
	eor	r2,r2,r10			@ Ch(e,f,g)
	eor	r0,r4,r4,ror#11
	add	r11,r11,r2			@ h+=Ch(e,f,g)
#if 24==31
	and	r12,r12,#0xff
	cmp	r12,#0xf2			@ done?
#endif
#if 24<15
# if __ARM_ARCH__>=7
	ldr	r2,[r1],#4			@ prefetch
# else
	ldrb	r2,[r1,#3]
# endif
	eor	r12,r4,r5			@ a^b, b^c in next round
#else
	ldr	r2,[sp,#10*4]		@ from future BODY_16_xx
	eor	r12,r4,r5			@ a^b, b^c in next round
	ldr	r1,[sp,#7*4]	@ from future BODY_16_xx
#endif
	eor	r0,r0,r4,ror#20	@ Sigma0(a)
	and	r3,r3,r12			@ (b^c)&=(a^b)
	add	r7,r7,r11			@ d+=h
	eor	r3,r3,r5			@ Maj(a,b,c)
	add	r11,r11,r0,ror#2	@ h+=Sigma0(a)
	@ add	r11,r11,r3			@ h+=Maj(a,b,c)
	@ ldr	r2,[sp,#10*4]		@ 25
	@ ldr	r1,[sp,#7*4]
	mov	r0,r2,ror#7
	add	r11,r11,r3			@ h+=Maj(a,b,c) from the past
	mov	r3,r1,ror#17
	eor	r0,r0,r2,ror#18
	eor	r3,r3,r1,ror#19
	eor	r0,r0,r2,lsr#3	@ sigma0(X[i+1])
	ldr	r2,[sp,#9*4]
	eor	r3,r3,r1,lsr#10	@ sigma1(X[i+14])
	ldr	r1,[sp,#2*4]

	add	r3,r3,r0
	eor	r0,r7,r7,ror#5	@ from BODY_00_15
	add	r2,r2,r3
	eor	r0,r0,r7,ror#19	@ Sigma1(e)
	add	r2,r2,r1			@ X[i]
	ldr	r3,[r14],#4			@ *K256++
	add	r10,r10,r2			@ h+=X[i]
	str	r2,[sp,#9*4]
	eor	r2,r8,r9
	add	r10,r10,r0,ror#6	@ h+=Sigma1(e)
	and	r2,r2,r7
	add	r10,r10,r3			@ h+=K256[i]
	eor	r2,r2,r9			@ Ch(e,f,g)
	eor	r0,r11,r11,ror#11
	add	r10,r10,r2			@ h+=Ch(e,f,g)
#if 25==31
	and	r3,r3,#0xff
	cmp	r3,#0xf2			@ done?
#endif
#if 25<15
# if __ARM_ARCH__>=7
	ldr	r2,[r1],#4			@ prefetch
# else
	ldrb	r2,[r1,#3]
# endif
	eor	r3,r11,r4			@ a^b, b^c in next round
#else
	ldr	r2,[sp,#11*4]		@ from future BODY_16_xx
	eor	r3,r11,r4			@ a^b, b^c in next round
	ldr	r1,[sp,#8*4]	@ from future BODY_16_xx
#endif
	eor	r0,r0,r11,ror#20	@ Sigma0(a)
	and	r12,r12,r3			@ (b^c)&=(a^b)
	add	r6,r6,r10			@ d+=h
	eor	r12,r12,r4			@ Maj(a,b,c)
	add	r10,r10,r0,ror#2	@ h+=Sigma0(a)
	@ add	r10,r10,r12			@ h+=Maj(a,b,c)
	@ ldr	r2,[sp,#11*4]		@ 26
	@ ldr	r1,[sp,#8*4]
	mov	r0,r2,ror#7
	add	r10,r10,r12			@ h+=Maj(a,b,c) from the past
	mov	r12,r1,ror#17
	eor	r0,r0,r2,ror#18
	eor	r12,r12,r1,ror#19
	eor	r0,r0,r2,lsr#3	@ sigma0(X[i+1])
	ldr	r2,[sp,#10*4]
	eor	r12,r12,r1,lsr#10	@ sigma1(X[i+14])
	ldr	r1,[sp,#3*4]

	add	r12,r12,r0
	eor	r0,r6,r6,ror#5	@ from BODY_00_15
	add	r2,r2,r12
	eor	r0,r0,r6,ror#19	@ Sigma1(e)
	add	r2,r2,r1			@ X[i]
	ldr	r12,[r14],#4			@ *K256++
	add	r9,r9,r2			@ h+=X[i]
	str	r2,[sp,#10*4]
	eor	r2,r7,r8
	add	r9,r9,r0,ror#6	@ h+=Sigma1(e)
	and	r2,r2,r6
	add	r9,r9,r12			@ h+=K256[i]
	eor	r2,r2,r8			@ Ch(e,f,g)
	eor	r0,r10,r10,ror#11
	add	r9,r9,r2			@ h+=Ch(e,f,g)
#if 26==31
	and	r12,r12,#0xff
	cmp	r12,#0xf2			@ done?
#endif
#if 26<15
# if __ARM_ARCH__>=7
	ldr	r2,[r1],#4			@ prefetch
# else
	ldrb	r2,[r1,#3]
# endif
	eor	r12,r10,r11			@ a^b, b^c in next round
#else
	ldr	r2,[sp,#12*4]		@ from future BODY_16_xx
	eor	r12,r10,r11			@ a^b, b^c in next round
	ldr	r1,[sp,#9*4]	@ from future BODY_16_xx
#endif
	eor	r0,r0,r10,ror#20	@ Sigma0(a)
	and	r3,r3,r12			@ (b^c)&=(a^b)
	add	r5,r5,r9			@ d+=h
	eor	r3,r3,r11			@ Maj(a,b,c)
	add	r9,r9,r0,ror#2	@ h+=Sigma0(a)
	@ add	r9,r9,r3			@ h+=Maj(a,b,c)
	@ ldr	r2,[sp,#12*4]		@ 27
	@ ldr	r1,[sp,#9*4]
	mov	r0,r2,ror#7
	add	r9,r9,r3			@ h+=Maj(a,b,c) from the past
	mov	r3,r1,ror#17
	eor	r0,r0,r2,ror#18
	eor	r3,r3,r1,ror#19
	eor	r0,r0,r2,lsr#3	@ sigma0(X[i+1])
	ldr	r2,[sp,#11*4]
	eor	r3,r3,r1,lsr#10	@ sigma1(X[i+14])
	ldr	r1,[sp,#4*4]

	add	r3,r3,r0
	eor	r0,r5,r5,ror#5	@ from BODY_00_15
	add	r2,r2,r3
	eor	r0,r0,r5,ror#19	@ Sigma1(e)
	add	r2,r2,r1			@ X[i]
	ldr	r3,[r14],#4			@ *K256++
	add	r8,r8,r2			@ h+=X[i]
	str	r2,[sp,#11*4]
	eor	r2,r6,r7
	add	r8,r8,r0,ror#6	@ h+=Sigma1(e)
	and	r2,r2,r5
	add	r8,r8,r3			@ h+=K256[i]
	eor	r2,r2,r7			@ Ch(e,f,g)
	eor	r0,r9,r9,ror#11
	add	r8,r8,r2			@ h+=Ch(e,f,g)
#if 27==31
	and	r3,r3,#0xff
	cmp	r3,#0xf2			@ done?
#endif
#if 27<15
# if __ARM_ARCH__>=7
	ldr	r2,[r1],#4			@ prefetch
# else
	ldrb	r2,[r1,#3]
# endif
	eor	r3,r9,r10			@ a^b, b^c in next round
#else
	ldr	r2,[sp,#13*4]		@ from future BODY_16_xx
	eor	r3,r9,r10			@ a^b, b^c in next round
	ldr	r1,[sp,#10*4]	@ from future BODY_16_xx
#endif
	eor	r0,r0,r9,ror#20	@ Sigma0(a)
	and	r12,r12,r3			@ (b^c)&=(a^b)
	add	r4,r4,r8			@ d+=h
	eor	r12,r12,r10			@ Maj(a,b,c)
	add	r8,r8,r0,ror#2	@ h+=Sigma0(a)
	@ add	r8,r8,r12			@ h+=Maj(a,b,c)
	@ ldr	r2,[sp,#13*4]		@ 28
	@ ldr	r1,[sp,#10*4]
	mov	r0,r2,ror#7
	add	r8,r8,r12			@ h+=Maj(a,b,c) from the past
	mov	r12,r1,ror#17
	eor	r0,r0,r2,ror#18
	eor	r12,r12,r1,ror#19
	eor	r0,r0,r2,lsr#3	@ sigma0(X[i+1])
	ldr	r2,[sp,#12*4]
	eor	r12,r12,r1,lsr#10	@ sigma1(X[i+14])
	ldr	r1,[sp,#5*4]

	add	r12,r12,r0
	eor	r0,r4,r4,ror#5	@ from BODY_00_15
	add	r2,r2,r12
	eor	r0,r0,r4,ror#19	@ Sigma1(e)
	add	r2,r2,r1			@ X[i]
	ldr	r12,[r14],#4			@ *K256++
	add	r7,r7,r2			@ h+=X[i]
	str	r2,[sp,#12*4]
	eor	r2,r5,r6
	add	r7,r7,r0,ror#6	@ h+=Sigma1(e)
	and	r2,r2,r4
	add	r7,r7,r12			@ h+=K256[i]
	eor	r2,r2,r6			@ Ch(e,f,g)
	eor	r0,r8,r8,ror#11
	add	r7,r7,r2			@ h+=Ch(e,f,g)
#if 28==31
	and	r12,r12,#0xff
	cmp	r12,#0xf2			@ done?
#endif
#if 28<15
# if __ARM_ARCH__>=7
	ldr	r2,[r1],#4			@ prefetch
# else
	ldrb	r2,[r1,#3]
# endif
	eor	r12,r8,r9			@ a^b, b^c in next round
#else
	ldr	r2,[sp,#14*4]		@ from future BODY_16_xx
	eor	r12,r8,r9			@ a^b, b^c in next round
	ldr	r1,[sp,#11*4]	@ from future BODY_16_xx
#endif
	eor	r0,r0,r8,ror#20	@ Sigma0(a)
	and	r3,r3,r12			@ (b^c)&=(a^b)
	add	r11,r11,r7			@ d+=h
	eor	r3,r3,r9			@ Maj(a,b,c)
	add	r7,r7,r0,ror#2	@ h+=Sigma0(a)
	@ add	r7,r7,r3			@ h+=Maj(a,b,c)
	@ ldr	r2,[sp,#14*4]		@ 29
	@ ldr	r1,[sp,#11*4]
	mov	r0,r2,ror#7
	add	r7,r7,r3			@ h+=Maj(a,b,c) from the past
	mov	r3,r1,ror#17
	eor	r0,r0,r2,ror#18
	eor	r3,r3,r1,ror#19
	eor	r0,r0,r2,lsr#3	@ sigma0(X[i+1])
	ldr	r2,[sp,#13*4]
	eor	r3,r3,r1,lsr#10	@ sigma1(X[i+14])
	ldr	r1,[sp,#6*4]

	add	r3,r3,r0
	eor	r0,r11,r11,ror#5	@ from BODY_00_15
	add	r2,r2,r3
	eor	r0,r0,r11,ror#19	@ Sigma1(e)
	add	r2,r2,r1			@ X[i]
	ldr	r3,[r14],#4			@ *K256++
	add	r6,r6,r2			@ h+=X[i]
	str	r2,[sp,#13*4]
	eor	r2,r4,r5
	add	r6,r6,r0,ror#6	@ h+=Sigma1(e)
	and	r2,r2,r11
	add	r6,r6,r3			@ h+=K256[i]
	eor	r2,r2,r5			@ Ch(e,f,g)
	eor	r0,r7,r7,ror#11
	add	r6,r6,r2			@ h+=Ch(e,f,g)
#if 29==31
	and	r3,r3,#0xff
	cmp	r3,#0xf2			@ done?
#endif
#if 29<15
# if __ARM_ARCH__>=7
	ldr	r2,[r1],#4			@ prefetch
# else
	ldrb	r2,[r1,#3]
# endif
	eor	r3,r7,r8			@ a^b, b^c in next round
#else
	ldr	r2,[sp,#15*4]		@ from future BODY_16_xx
	eor	r3,r7,r8			@ a^b, b^c in next round
	ldr	r1,[sp,#12*4]	@ from future BODY_16_xx
#endif
	eor	r0,r0,r7,ror#20	@ Sigma0(a)
	and	r12,r12,r3			@ (b^c)&=(a^b)
	add	r10,r10,r6			@ d+=h
	eor	r12,r12,r8			@ Maj(a,b,c)
	add	r6,r6,r0,ror#2	@ h+=Sigma0(a)
	@ add	r6,r6,r12			@ h+=Maj(a,b,c)
	@ ldr	r2,[sp,#15*4]		@ 30
	@ ldr	r1,[sp,#12*4]
	mov	r0,r2,ror#7
	add	r6,r6,r12			@ h+=Maj(a,b,c) from the past
	mov	r12,r1,ror#17
	eor	r0,r0,r2,ror#18
	eor	r12,r12,r1,ror#19
	eor	r0,r0,r2,lsr#3	@ sigma0(X[i+1])
	ldr	r2,[sp,#14*4]
	eor	r12,r12,r1,lsr#10	@ sigma1(X[i+14])
	ldr	r1,[sp,#7*4]

	add	r12,r12,r0
	eor	r0,r10,r10,ror#5	@ from BODY_00_15
	add	r2,r2,r12
	eor	r0,r0,r10,ror#19	@ Sigma1(e)
	add	r2,r2,r1			@ X[i]
	ldr	r12,[r14],#4			@ *K256++
	add	r5,r5,r2			@ h+=X[i]
	str	r2,[sp,#14*4]
	eor	r2,r11,r4
	add	r5,r5,r0,ror#6	@ h+=Sigma1(e)
	and	r2,r2,r10
	add	r5,r5,r12			@ h+=K256[i]
	eor	r2,r2,r4			@ Ch(e,f,g)
	eor	r0,r6,r6,ror#11
	add	r5,r5,r2			@ h+=Ch(e,f,g)
#if 30==31
	and	r12,r12,#0xff
	cmp	r12,#0xf2			@ done?
#endif
#if 30<15
# if __ARM_ARCH__>=7
	ldr	r2,[r1],#4			@ prefetch
# else
	ldrb	r2,[r1,#3]
# endif
	eor	r12,r6,r7			@ a^b, b^c in next round
#else
	ldr	r2,[sp,#0*4]		@ from future BODY_16_xx
	eor	r12,r6,r7			@ a^b, b^c in next round
	ldr	r1,[sp,#13*4]	@ from future BODY_16_xx
#endif
	eor	r0,r0,r6,ror#20	@ Sigma0(a)
	and	r3,r3,r12			@ (b^c)&=(a^b)
	add	r9,r9,r5			@ d+=h
	eor	r3,r3,r7			@ Maj(a,b,c)
	add	r5,r5,r0,ror#2	@ h+=Sigma0(a)
	@ add	r5,r5,r3			@ h+=Maj(a,b,c)
	@ ldr	r2,[sp,#0*4]		@ 31
	@ ldr	r1,[sp,#13*4]
	mov	r0,r2,ror#7
	add	r5,r5,r3			@ h+=Maj(a,b,c) from the past
	mov	r3,r1,ror#17
	eor	r0,r0,r2,ror#18
	eor	r3,r3,r1,ror#19
	eor	r0,r0,r2,lsr#3	@ sigma0(X[i+1])
	ldr	r2,[sp,#15*4]
	eor	r3,r3,r1,lsr#10	@ sigma1(X[i+14])
	ldr	r1,[sp,#8*4]

	add	r3,r3,r0
	eor	r0,r9,r9,ror#5	@ from BODY_00_15
	add	r2,r2,r3
	eor	r0,r0,r9,ror#19	@ Sigma1(e)
	add	r2,r2,r1			@ X[i]
	ldr	r3,[r14],#4			@ *K256++
	add	r4,r4,r2			@ h+=X[i]
	str	r2,[sp,#15*4]
	eor	r2,r10,r11
	add	r4,r4,r0,ror#6	@ h+=Sigma1(e)
	and	r2,r2,r9
	add	r4,r4,r3			@ h+=K256[i]
	eor	r2,r2,r11			@ Ch(e,f,g)
	eor	r0,r5,r5,ror#11
	add	r4,r4,r2			@ h+=Ch(e,f,g)
#if 31==31
	and	r3,r3,#0xff
	cmp	r3,#0xf2			@ done?
#endif
#if 31<15
# if __ARM_ARCH__>=7
	ldr	r2,[r1],#4			@ prefetch
# else
	ldrb	r2,[r1,#3]
# endif
	eor	r3,r5,r6			@ a^b, b^c in next round
#else
	ldr	r2,[sp,#1*4]		@ from future BODY_16_xx
	eor	r3,r5,r6			@ a^b, b^c in next round
	ldr	r1,[sp,#14*4]	@ from future BODY_16_xx
#endif
	eor	r0,r0,r5,ror#20	@ Sigma0(a)
	and	r12,r12,r3			@ (b^c)&=(a^b)
	add	r8,r8,r4			@ d+=h
	eor	r12,r12,r6			@ Maj(a,b,c)
	add	r4,r4,r0,ror#2	@ h+=Sigma0(a)
	@ add	r4,r4,r12			@ h+=Maj(a,b,c)
#if __ARM_ARCH__>=7
	ite	eq			@ Thumb2 thing, sanity check in ARM
#endif
	ldreq	r3,[sp,#16*4]		@ pull ctx
	bne	.Lrounds_16_xx

	add	r4,r4,r12		@ h+=Maj(a,b,c) from the past
	ldr	r0,[r3,#0]
	ldr	r2,[r3,#4]
	ldr	r12,[r3,#8]
	add	r4,r4,r0
	ldr	r0,[r3,#12]
	add	r5,r5,r2
	ldr	r2,[r3,#16]
	add	r6,r6,r12
	ldr	r12,[r3,#20]
	add	r7,r7,r0
	ldr	r0,[r3,#24]
	add	r8,r8,r2
	ldr	r2,[r3,#28]
	add	r9,r9,r12
	ldr	r1,[sp,#17*4]		@ pull inp
	ldr	r12,[sp,#18*4]		@ pull inp+len
	add	r10,r10,r0
	add	r11,r11,r2
	stmia	r3,{r4,r5,r6,r7,r8,r9,r10,r11}
	cmp	r1,r12
	sub	r14,r14,#256	@ rewind Ktbl
	bne	.Loop

	add	sp,sp,#19*4	@ destroy frame
#if __ARM_ARCH__>=5
	ldmia	sp!,{r4-r11,pc}
#else
	ldmia	sp!,{r4-r11,lr}
	tst	lr,#1
	moveq	pc,lr			@ be binary compatible with V4, yet
	.word	0xe12fff1e			@ interoperable with Thumb ISA:-)
#endif
.size	sha256_block_data_order,.-sha256_block_data_order
#if __ARM_MAX_ARCH__>=7
.arch	armv7-a
.fpu	neon

.global	sha256_block_data_order_neon
.type	sha256_block_data_order_neon,%function
.align	4
sha256_block_data_order_neon:
.LNEON:
	stmdb	sp!,{r4-r12,lr}

	sub	r11,sp,#16*4+16
	adrl	r14,K256
	bic	r11,r11,#15		@ align for 128-bit stores
	mov	r12,sp
	mov	sp,r11			@ alloca
	add	r2,r1,r2,lsl#6	@ len to point at the end of inp

	vld1.8		{q0},[r1]!
	vld1.8		{q1},[r1]!
	vld1.8		{q2},[r1]!
	vld1.8		{q3},[r1]!
	vld1.32		{q8},[r14,:128]!
	vld1.32		{q9},[r14,:128]!
	vld1.32		{q10},[r14,:128]!
	vld1.32		{q11},[r14,:128]!
	vrev32.8	q0,q0		@ yes, even on
	str		r0,[sp,#64]
	vrev32.8	q1,q1		@ big-endian
	str		r1,[sp,#68]
	mov		r1,sp
	vrev32.8	q2,q2
	str		r2,[sp,#72]
	vrev32.8	q3,q3
	str		r12,[sp,#76]		@ save original sp
	vadd.i32	q8,q8,q0
	vadd.i32	q9,q9,q1
	vst1.32		{q8},[r1,:128]!
	vadd.i32	q10,q10,q2
	vst1.32		{q9},[r1,:128]!
	vadd.i32	q11,q11,q3
	vst1.32		{q10},[r1,:128]!
	vst1.32		{q11},[r1,:128]!

	ldmia		r0,{r4-r11}
	sub		r1,r1,#64
	ldr		r2,[sp,#0]
	eor		r12,r12,r12
	eor		r3,r5,r6
	b		.L_00_48

.align	4
.L_00_48:
	vext.8	q8,q0,q1,#4
	add	r11,r11,r2
	eor	r2,r9,r10
	eor	r0,r8,r8,ror#5
	vext.8	q9,q2,q3,#4
	add	r4,r4,r12
	and	r2,r2,r8
	eor	r12,r0,r8,ror#19
	vshr.u32	q10,q8,#7
	eor	r0,r4,r4,ror#11
	eor	r2,r2,r10
	vadd.i32	q0,q0,q9
	add	r11,r11,r12,ror#6
	eor	r12,r4,r5
	vshr.u32	q9,q8,#3
	eor	r0,r0,r4,ror#20
	add	r11,r11,r2
	vsli.32	q10,q8,#25
	ldr	r2,[sp,#4]
	and	r3,r3,r12
	vshr.u32	q11,q8,#18
	add	r7,r7,r11
	add	r11,r11,r0,ror#2
	eor	r3,r3,r5
	veor	q9,q9,q10
	add	r10,r10,r2
	vsli.32	q11,q8,#14
	eor	r2,r8,r9
	eor	r0,r7,r7,ror#5
	vshr.u32	d24,d7,#17
	add	r11,r11,r3
	and	r2,r2,r7
	veor	q9,q9,q11
	eor	r3,r0,r7,ror#19
	eor	r0,r11,r11,ror#11
	vsli.32	d24,d7,#15
	eor	r2,r2,r9
	add	r10,r10,r3,ror#6
	vshr.u32	d25,d7,#10
	eor	r3,r11,r4
	eor	r0,r0,r11,ror#20
	vadd.i32	q0,q0,q9
	add	r10,r10,r2
	ldr	r2,[sp,#8]
	veor	d25,d25,d24
	and	r12,r12,r3
	add	r6,r6,r10
	vshr.u32	d24,d7,#19
	add	r10,r10,r0,ror#2
	eor	r12,r12,r4
	vsli.32	d24,d7,#13
	add	r9,r9,r2
	eor	r2,r7,r8
	veor	d25,d25,d24
	eor	r0,r6,r6,ror#5
	add	r10,r10,r12
	vadd.i32	d0,d0,d25
	and	r2,r2,r6
	eor	r12,r0,r6,ror#19
	vshr.u32	d24,d0,#17
	eor	r0,r10,r10,ror#11
	eor	r2,r2,r8
	vsli.32	d24,d0,#15
	add	r9,r9,r12,ror#6
	eor	r12,r10,r11
	vshr.u32	d25,d0,#10
	eor	r0,r0,r10,ror#20
	add	r9,r9,r2
	veor	d25,d25,d24
	ldr	r2,[sp,#12]
	and	r3,r3,r12
	vshr.u32	d24,d0,#19
	add	r5,r5,r9
	add	r9,r9,r0,ror#2
	eor	r3,r3,r11
	vld1.32	{q8},[r14,:128]!
	add	r8,r8,r2
	vsli.32	d24,d0,#13
	eor	r2,r6,r7
	eor	r0,r5,r5,ror#5
	veor	d25,d25,d24
	add	r9,r9,r3
	and	r2,r2,r5
	vadd.i32	d1,d1,d25
	eor	r3,r0,r5,ror#19
	eor	r0,r9,r9,ror#11
	vadd.i32	q8,q8,q0
	eor	r2,r2,r7
	add	r8,r8,r3,ror#6
	eor	r3,r9,r10
	eor	r0,r0,r9,ror#20
	add	r8,r8,r2
	ldr	r2,[sp,#16]
	and	r12,r12,r3
	add	r4,r4,r8
	vst1.32	{q8},[r1,:128]!
	add	r8,r8,r0,ror#2
	eor	r12,r12,r10
	vext.8	q8,q1,q2,#4
	add	r7,r7,r2
	eor	r2,r5,r6
	eor	r0,r4,r4,ror#5
	vext.8	q9,q3,q0,#4
	add	r8,r8,r12
	and	r2,r2,r4
	eor	r12,r0,r4,ror#19
	vshr.u32	q10,q8,#7
	eor	r0,r8,r8,ror#11
	eor	r2,r2,r6
	vadd.i32	q1,q1,q9
	add	r7,r7,r12,ror#6
	eor	r12,r8,r9
	vshr.u32	q9,q8,#3
	eor	r0,r0,r8,ror#20
	add	r7,r7,r2
	vsli.32	q10,q8,#25
	ldr	r2,[sp,#20]
	and	r3,r3,r12
	vshr.u32	q11,q8,#18
	add	r11,r11,r7
	add	r7,r7,r0,ror#2
	eor	r3,r3,r9
	veor	q9,q9,q10
	add	r6,r6,r2
	vsli.32	q11,q8,#14
	eor	r2,r4,r5
	eor	r0,r11,r11,ror#5
	vshr.u32	d24,d1,#17
	add	r7,r7,r3
	and	r2,r2,r11
	veor	q9,q9,q11
	eor	r3,r0,r11,ror#19
	eor	r0,r7,r7,ror#11
	vsli.32	d24,d1,#15
	eor	r2,r2,r5
	add	r6,r6,r3,ror#6
	vshr.u32	d25,d1,#10
	eor	r3,r7,r8
	eor	r0,r0,r7,ror#20
	vadd.i32	q1,q1,q9
	add	r6,r6,r2
	ldr	r2,[sp,#24]
	veor	d25,d25,d24
	and	r12,r12,r3
	add	r10,r10,r6
	vshr.u32	d24,d1,#19
	add	r6,r6,r0,ror#2
	eor	r12,r12,r8
	vsli.32	d24,d1,#13
	add	r5,r5,r2
	eor	r2,r11,r4
	veor	d25,d25,d24
	eor	r0,r10,r10,ror#5
	add	r6,r6,r12
	vadd.i32	d2,d2,d25
	and	r2,r2,r10
	eor	r12,r0,r10,ror#19
	vshr.u32	d24,d2,#17
	eor	r0,r6,r6,ror#11
	eor	r2,r2,r4
	vsli.32	d24,d2,#15
	add	r5,r5,r12,ror#6
	eor	r12,r6,r7
	vshr.u32	d25,d2,#10
	eor	r0,r0,r6,ror#20
	add	r5,r5,r2
	veor	d25,d25,d24
	ldr	r2,[sp,#28]
	and	r3,r3,r12
	vshr.u32	d24,d2,#19
	add	r9,r9,r5
	add	r5,r5,r0,ror#2
	eor	r3,r3,r7
	vld1.32	{q8},[r14,:128]!
	add	r4,r4,r2
	vsli.32	d24,d2,#13
	eor	r2,r10,r11
	eor	r0,r9,r9,ror#5
	veor	d25,d25,d24
	add	r5,r5,r3
	and	r2,r2,r9
	vadd.i32	d3,d3,d25
	eor	r3,r0,r9,ror#19
	eor	r0,r5,r5,ror#11
	vadd.i32	q8,q8,q1
	eor	r2,r2,r11
	add	r4,r4,r3,ror#6
	eor	r3,r5,r6
	eor	r0,r0,r5,ror#20
	add	r4,r4,r2
	ldr	r2,[sp,#32]
	and	r12,r12,r3
	add	r8,r8,r4
	vst1.32	{q8},[r1,:128]!
	add	r4,r4,r0,ror#2
	eor	r12,r12,r6
	vext.8	q8,q2,q3,#4
	add	r11,r11,r2
	eor	r2,r9,r10
	eor	r0,r8,r8,ror#5
	vext.8	q9,q0,q1,#4
	add	r4,r4,r12
	and	r2,r2,r8
	eor	r12,r0,r8,ror#19
	vshr.u32	q10,q8,#7
	eor	r0,r4,r4,ror#11
	eor	r2,r2,r10
	vadd.i32	q2,q2,q9
	add	r11,r11,r12,ror#6
	eor	r12,r4,r5
	vshr.u32	q9,q8,#3
	eor	r0,r0,r4,ror#20
	add	r11,r11,r2
	vsli.32	q10,q8,#25
	ldr	r2,[sp,#36]
	and	r3,r3,r12
	vshr.u32	q11,q8,#18
	add	r7,r7,r11
	add	r11,r11,r0,ror#2
	eor	r3,r3,r5
	veor	q9,q9,q10
	add	r10,r10,r2
	vsli.32	q11,q8,#14
	eor	r2,r8,r9
	eor	r0,r7,r7,ror#5
	vshr.u32	d24,d3,#17
	add	r11,r11,r3
	and	r2,r2,r7
	veor	q9,q9,q11
	eor	r3,r0,r7,ror#19
	eor	r0,r11,r11,ror#11
	vsli.32	d24,d3,#15
	eor	r2,r2,r9
	add	r10,r10,r3,ror#6
	vshr.u32	d25,d3,#10
	eor	r3,r11,r4
	eor	r0,r0,r11,ror#20
	vadd.i32	q2,q2,q9
	add	r10,r10,r2
	ldr	r2,[sp,#40]
	veor	d25,d25,d24
	and	r12,r12,r3
	add	r6,r6,r10
	vshr.u32	d24,d3,#19
	add	r10,r10,r0,ror#2
	eor	r12,r12,r4
	vsli.32	d24,d3,#13
	add	r9,r9,r2
	eor	r2,r7,r8
	veor	d25,d25,d24
	eor	r0,r6,r6,ror#5
	add	r10,r10,r12
	vadd.i32	d4,d4,d25
	and	r2,r2,r6
	eor	r12,r0,r6,ror#19
	vshr.u32	d24,d4,#17
	eor	r0,r10,r10,ror#11
	eor	r2,r2,r8
	vsli.32	d24,d4,#15
	add	r9,r9,r12,ror#6
	eor	r12,r10,r11
	vshr.u32	d25,d4,#10
	eor	r0,r0,r10,ror#20
	add	r9,r9,r2
	veor	d25,d25,d24
	ldr	r2,[sp,#44]
	and	r3,r3,r12
	vshr.u32	d24,d4,#19
	add	r5,r5,r9
	add	r9,r9,r0,ror#2
	eor	r3,r3,r11
	vld1.32	{q8},[r14,:128]!
	add	r8,r8,r2
	vsli.32	d24,d4,#13
	eor	r2,r6,r7
	eor	r0,r5,r5,ror#5
	veor	d25,d25,d24
	add	r9,r9,r3
	and	r2,r2,r5
	vadd.i32	d5,d5,d25
	eor	r3,r0,r5,ror#19
	eor	r0,r9,r9,ror#11
	vadd.i32	q8,q8,q2
	eor	r2,r2,r7
	add	r8,r8,r3,ror#6
	eor	r3,r9,r10
	eor	r0,r0,r9,ror#20
	add	r8,r8,r2
	ldr	r2,[sp,#48]
	and	r12,r12,r3
	add	r4,r4,r8
	vst1.32	{q8},[r1,:128]!
	add	r8,r8,r0,ror#2
	eor	r12,r12,r10
	vext.8	q8,q3,q0,#4
	add	r7,r7,r2
	eor	r2,r5,r6
	eor	r0,r4,r4,ror#5
	vext.8	q9,q1,q2,#4
	add	r8,r8,r12
	and	r2,r2,r4
	eor	r12,r0,r4,ror#19
	vshr.u32	q10,q8,#7
	eor	r0,r8,r8,ror#11
	eor	r2,r2,r6
	vadd.i32	q3,q3,q9
	add	r7,r7,r12,ror#6
	eor	r12,r8,r9
	vshr.u32	q9,q8,#3
	eor	r0,r0,r8,ror#20
	add	r7,r7,r2
	vsli.32	q10,q8,#25
	ldr	r2,[sp,#52]
	and	r3,r3,r12
	vshr.u32	q11,q8,#18
	add	r11,r11,r7
	add	r7,r7,r0,ror#2
	eor	r3,r3,r9
	veor	q9,q9,q10
	add	r6,r6,r2
	vsli.32	q11,q8,#14
	eor	r2,r4,r5
	eor	r0,r11,r11,ror#5
	vshr.u32	d24,d5,#17
	add	r7,r7,r3
	and	r2,r2,r11
	veor	q9,q9,q11
	eor	r3,r0,r11,ror#19
	eor	r0,r7,r7,ror#11
	vsli.32	d24,d5,#15
	eor	r2,r2,r5
	add	r6,r6,r3,ror#6
	vshr.u32	d25,d5,#10
	eor	r3,r7,r8
	eor	r0,r0,r7,ror#20
	vadd.i32	q3,q3,q9
	add	r6,r6,r2
	ldr	r2,[sp,#56]
	veor	d25,d25,d24
	and	r12,r12,r3
	add	r10,r10,r6
	vshr.u32	d24,d5,#19
	add	r6,r6,r0,ror#2
	eor	r12,r12,r8
	vsli.32	d24,d5,#13
	add	r5,r5,r2
	eor	r2,r11,r4
	veor	d25,d25,d24
	eor	r0,r10,r10,ror#5
	add	r6,r6,r12
	vadd.i32	d6,d6,d25
	and	r2,r2,r10
	eor	r12,r0,r10,ror#19
	vshr.u32	d24,d6,#17
	eor	r0,r6,r6,ror#11
	eor	r2,r2,r4
	vsli.32	d24,d6,#15
	add	r5,r5,r12,ror#6
	eor	r12,r6,r7
	vshr.u32	d25,d6,#10
	eor	r0,r0,r6,ror#20
	add	r5,r5,r2
	veor	d25,d25,d24
	ldr	r2,[sp,#60]
	and	r3,r3,r12
	vshr.u32	d24,d6,#19
	add	r9,r9,r5
	add	r5,r5,r0,ror#2
	eor	r3,r3,r7
	vld1.32	{q8},[r14,:128]!
	add	r4,r4,r2
	vsli.32	d24,d6,#13
	eor	r2,r10,r11
	eor	r0,r9,r9,ror#5
	veor	d25,d25,d24
	add	r5,r5,r3
	and	r2,r2,r9
	vadd.i32	d7,d7,d25
	eor	r3,r0,r9,ror#19
	eor	r0,r5,r5,ror#11
	vadd.i32	q8,q8,q3
	eor	r2,r2,r11
	add	r4,r4,r3,ror#6
	eor	r3,r5,r6
	eor	r0,r0,r5,ror#20
	add	r4,r4,r2
	ldr	r2,[r14]
	and	r12,r12,r3
	add	r8,r8,r4
	vst1.32	{q8},[r1,:128]!
	add	r4,r4,r0,ror#2
	eor	r12,r12,r6
	teq	r2,#0				@ check for K256 terminator
	ldr	r2,[sp,#0]
	sub	r1,r1,#64
	bne	.L_00_48

	ldr		r1,[sp,#68]
	ldr		r0,[sp,#72]
	sub		r14,r14,#256	@ rewind r14
	teq		r1,r0
	it		eq
	subeq		r1,r1,#64		@ avoid SEGV
	vld1.8		{q0},[r1]!		@ load next input block
	vld1.8		{q1},[r1]!
	vld1.8		{q2},[r1]!
	vld1.8		{q3},[r1]!
	it		ne
	strne		r1,[sp,#68]
	mov		r1,sp
	add	r11,r11,r2
	eor	r2,r9,r10
	eor	r0,r8,r8,ror#5
	add	r4,r4,r12
	vld1.32	{q8},[r14,:128]!
	and	r2,r2,r8
	eor	r12,r0,r8,ror#19
	eor	r0,r4,r4,ror#11
	eor	r2,r2,r10
	vrev32.8	q0,q0
	add	r11,r11,r12,ror#6
	eor	r12,r4,r5
	eor	r0,r0,r4,ror#20
	add	r11,r11,r2
	vadd.i32	q8,q8,q0
	ldr	r2,[sp,#4]
	and	r3,r3,r12
	add	r7,r7,r11
	add	r11,r11,r0,ror#2
	eor	r3,r3,r5
	add	r10,r10,r2
	eor	r2,r8,r9
	eor	r0,r7,r7,ror#5
	add	r11,r11,r3
	and	r2,r2,r7
	eor	r3,r0,r7,ror#19
	eor	r0,r11,r11,ror#11
	eor	r2,r2,r9
	add	r10,r10,r3,ror#6
	eor	r3,r11,r4
	eor	r0,r0,r11,ror#20
	add	r10,r10,r2
	ldr	r2,[sp,#8]
	and	r12,r12,r3
	add	r6,r6,r10
	add	r10,r10,r0,ror#2
	eor	r12,r12,r4
	add	r9,r9,r2
	eor	r2,r7,r8
	eor	r0,r6,r6,ror#5
	add	r10,r10,r12
	and	r2,r2,r6
	eor	r12,r0,r6,ror#19
	eor	r0,r10,r10,ror#11
	eor	r2,r2,r8
	add	r9,r9,r12,ror#6
	eor	r12,r10,r11
	eor	r0,r0,r10,ror#20
	add	r9,r9,r2
	ldr	r2,[sp,#12]
	and	r3,r3,r12
	add	r5,r5,r9
	add	r9,r9,r0,ror#2
	eor	r3,r3,r11
	add	r8,r8,r2
	eor	r2,r6,r7
	eor	r0,r5,r5,ror#5
	add	r9,r9,r3
	and	r2,r2,r5
	eor	r3,r0,r5,ror#19
	eor	r0,r9,r9,ror#11
	eor	r2,r2,r7
	add	r8,r8,r3,ror#6
	eor	r3,r9,r10
	eor	r0,r0,r9,ror#20
	add	r8,r8,r2
	ldr	r2,[sp,#16]
	and	r12,r12,r3
	add	r4,r4,r8
	add	r8,r8,r0,ror#2
	eor	r12,r12,r10
	vst1.32	{q8},[r1,:128]!
	add	r7,r7,r2
	eor	r2,r5,r6
	eor	r0,r4,r4,ror#5
	add	r8,r8,r12
	vld1.32	{q8},[r14,:128]!
	and	r2,r2,r4
	eor	r12,r0,r4,ror#19
	eor	r0,r8,r8,ror#11
	eor	r2,r2,r6
	vrev32.8	q1,q1
	add	r7,r7,r12,ror#6
	eor	r12,r8,r9
	eor	r0,r0,r8,ror#20
	add	r7,r7,r2
	vadd.i32	q8,q8,q1
	ldr	r2,[sp,#20]
	and	r3,r3,r12
	add	r11,r11,r7
	add	r7,r7,r0,ror#2
	eor	r3,r3,r9
	add	r6,r6,r2
	eor	r2,r4,r5
	eor	r0,r11,r11,ror#5
	add	r7,r7,r3
	and	r2,r2,r11
	eor	r3,r0,r11,ror#19
	eor	r0,r7,r7,ror#11
	eor	r2,r2,r5
	add	r6,r6,r3,ror#6
	eor	r3,r7,r8
	eor	r0,r0,r7,ror#20
	add	r6,r6,r2
	ldr	r2,[sp,#24]
	and	r12,r12,r3
	add	r10,r10,r6
	add	r6,r6,r0,ror#2
	eor	r12,r12,r8
	add	r5,r5,r2
	eor	r2,r11,r4
	eor	r0,r10,r10,ror#5
	add	r6,r6,r12
	and	r2,r2,r10
	eor	r12,r0,r10,ror#19
	eor	r0,r6,r6,ror#11
	eor	r2,r2,r4
	add	r5,r5,r12,ror#6
	eor	r12,r6,r7
	eor	r0,r0,r6,ror#20
	add	r5,r5,r2
	ldr	r2,[sp,#28]
	and	r3,r3,r12
	add	r9,r9,r5
	add	r5,r5,r0,ror#2
	eor	r3,r3,r7
	add	r4,r4,r2
	eor	r2,r10,r11
	eor	r0,r9,r9,ror#5
	add	r5,r5,r3
	and	r2,r2,r9
	eor	r3,r0,r9,ror#19
	eor	r0,r5,r5,ror#11
	eor	r2,r2,r11
	add	r4,r4,r3,ror#6
	eor	r3,r5,r6
	eor	r0,r0,r5,ror#20
	add	r4,r4,r2
	ldr	r2,[sp,#32]
	and	r12,r12,r3
	add	r8,r8,r4
	add	r4,r4,r0,ror#2
	eor	r12,r12,r6
	vst1.32	{q8},[r1,:128]!
	add	r11,r11,r2
	eor	r2,r9,r10
	eor	r0,r8,r8,ror#5
	add	r4,r4,r12
	vld1.32	{q8},[r14,:128]!
	and	r2,r2,r8
	eor	r12,r0,r8,ror#19
	eor	r0,r4,r4,ror#11
	eor	r2,r2,r10
	vrev32.8	q2,q2
	add	r11,r11,r12,ror#6
	eor	r12,r4,r5
	eor	r0,r0,r4,ror#20
	add	r11,r11,r2
	vadd.i32	q8,q8,q2
	ldr	r2,[sp,#36]
	and	r3,r3,r12
	add	r7,r7,r11
	add	r11,r11,r0,ror#2
	eor	r3,r3,r5
	add	r10,r10,r2
	eor	r2,r8,r9
	eor	r0,r7,r7,ror#5
	add	r11,r11,r3
	and	r2,r2,r7
	eor	r3,r0,r7,ror#19
	eor	r0,r11,r11,ror#11
	eor	r2,r2,r9
	add	r10,r10,r3,ror#6
	eor	r3,r11,r4
	eor	r0,r0,r11,ror#20
	add	r10,r10,r2
	ldr	r2,[sp,#40]
	and	r12,r12,r3
	add	r6,r6,r10
	add	r10,r10,r0,ror#2
	eor	r12,r12,r4
	add	r9,r9,r2
	eor	r2,r7,r8
	eor	r0,r6,r6,ror#5
	add	r10,r10,r12
	and	r2,r2,r6
	eor	r12,r0,r6,ror#19
	eor	r0,r10,r10,ror#11
	eor	r2,r2,r8
	add	r9,r9,r12,ror#6
	eor	r12,r10,r11
	eor	r0,r0,r10,ror#20
	add	r9,r9,r2
	ldr	r2,[sp,#44]
	and	r3,r3,r12
	add	r5,r5,r9
	add	r9,r9,r0,ror#2
	eor	r3,r3,r11
	add	r8,r8,r2
	eor	r2,r6,r7
	eor	r0,r5,r5,ror#5
	add	r9,r9,r3
	and	r2,r2,r5
	eor	r3,r0,r5,ror#19
	eor	r0,r9,r9,ror#11
	eor	r2,r2,r7
	add	r8,r8,r3,ror#6
	eor	r3,r9,r10
	eor	r0,r0,r9,ror#20
	add	r8,r8,r2
	ldr	r2,[sp,#48]
	and	r12,r12,r3
	add	r4,r4,r8
	add	r8,r8,r0,ror#2
	eor	r12,r12,r10
	vst1.32	{q8},[r1,:128]!
	add	r7,r7,r2
	eor	r2,r5,r6
	eor	r0,r4,r4,ror#5
	add	r8,r8,r12
	vld1.32	{q8},[r14,:128]!
	and	r2,r2,r4
	eor	r12,r0,r4,ror#19
	eor	r0,r8,r8,ror#11
	eor	r2,r2,r6
	vrev32.8	q3,q3
	add	r7,r7,r12,ror#6
	eor	r12,r8,r9
	eor	r0,r0,r8,ror#20
	add	r7,r7,r2
	vadd.i32	q8,q8,q3
	ldr	r2,[sp,#52]
	and	r3,r3,r12
	add	r11,r11,r7
	add	r7,r7,r0,ror#2
	eor	r3,r3,r9
	add	r6,r6,r2
	eor	r2,r4,r5
	eor	r0,r11,r11,ror#5
	add	r7,r7,r3
	and	r2,r2,r11
	eor	r3,r0,r11,ror#19
	eor	r0,r7,r7,ror#11
	eor	r2,r2,r5
	add	r6,r6,r3,ror#6
	eor	r3,r7,r8
	eor	r0,r0,r7,ror#20
	add	r6,r6,r2
	ldr	r2,[sp,#56]
	and	r12,r12,r3
	add	r10,r10,r6
	add	r6,r6,r0,ror#2
	eor	r12,r12,r8
	add	r5,r5,r2
	eor	r2,r11,r4
	eor	r0,r10,r10,ror#5
	add	r6,r6,r12
	and	r2,r2,r10
	eor	r12,r0,r10,ror#19
	eor	r0,r6,r6,ror#11
	eor	r2,r2,r4
	add	r5,r5,r12,ror#6
	eor	r12,r6,r7
	eor	r0,r0,r6,ror#20
	add	r5,r5,r2
	ldr	r2,[sp,#60]
	and	r3,r3,r12
	add	r9,r9,r5
	add	r5,r5,r0,ror#2
	eor	r3,r3,r7
	add	r4,r4,r2
	eor	r2,r10,r11
	eor	r0,r9,r9,ror#5
	add	r5,r5,r3
	and	r2,r2,r9
	eor	r3,r0,r9,ror#19
	eor	r0,r5,r5,ror#11
	eor	r2,r2,r11
	add	r4,r4,r3,ror#6
	eor	r3,r5,r6
	eor	r0,r0,r5,ror#20
	add	r4,r4,r2
	ldr	r2,[sp,#64]
	and	r12,r12,r3
	add	r8,r8,r4
	add	r4,r4,r0,ror#2
	eor	r12,r12,r6
	vst1.32	{q8},[r1,:128]!
	ldr	r0,[r2,#0]
	add	r4,r4,r12			@ h+=Maj(a,b,c) from the past
	ldr	r12,[r2,#4]
	ldr	r3,[r2,#8]
	ldr	r1,[r2,#12]
	add	r4,r4,r0			@ accumulate
	ldr	r0,[r2,#16]
	add	r5,r5,r12
	ldr	r12,[r2,#20]
	add	r6,r6,r3
	ldr	r3,[r2,#24]
	add	r7,r7,r1
	ldr	r1,[r2,#28]
	add	r8,r8,r0
	str	r4,[r2],#4
	add	r9,r9,r12
	str	r5,[r2],#4
	add	r10,r10,r3
	str	r6,[r2],#4
	add	r11,r11,r1
	str	r7,[r2],#4
	stmia	r2,{r8-r11}

	ittte	ne
	movne	r1,sp
	ldrne	r2,[sp,#0]
	eorne	r12,r12,r12
	ldreq	sp,[sp,#76]			@ restore original sp
	itt	ne
	eorne	r3,r5,r6
	bne	.L_00_48

	ldmia	sp!,{r4-r12,pc}
.size	sha256_block_data_order_neon,.-sha256_block_data_order_neon
#endif
#if __ARM_MAX_ARCH__>=7 && !defined(__KERNEL__)

# ifdef __thumb2__
#  define INST(a,b,c,d)	.byte	c,d|0xc,a,b
# else
#  define INST(a,b,c,d)	.byte	a,b,c,d
# endif

.type	sha256_block_data_order_armv8,%function
.align	5
sha256_block_data_order_armv8:
.LARMv8:
	vld1.32	{q0,q1},[r0]
# ifdef __thumb2__
	adr	r3,.LARMv8
	sub	r3,r3,#.LARMv8-K256
# else
	adrl	r3,K256
# endif
	add	r2,r1,r2,lsl#6	@ len to point at the end of inp

.Loop_v8:
	vld1.8		{q8-q9},[r1]!
	vld1.8		{q10-q11},[r1]!
	vld1.32		{q12},[r3]!
	vrev32.8	q8,q8
	vrev32.8	q9,q9
	vrev32.8	q10,q10
	vrev32.8	q11,q11
	vmov		q14,q0	@ offload
	vmov		q15,q1
	teq		r1,r2
	vld1.32		{q13},[r3]!
	vadd.i32	q12,q12,q8
	INST(0xe2,0x03,0xfa,0xf3)	@ sha256su0 q8,q9
	vmov		q2,q0
	INST(0x68,0x0c,0x02,0xf3)	@ sha256h q0,q1,q12
	INST(0x68,0x2c,0x14,0xf3)	@ sha256h2 q1,q2,q12
	INST(0xe6,0x0c,0x64,0xf3)	@ sha256su1 q8,q10,q11
	vld1.32		{q12},[r3]!
	vadd.i32	q13,q13,q9
	INST(0xe4,0x23,0xfa,0xf3)	@ sha256su0 q9,q10
	vmov		q2,q0
	INST(0x6a,0x0c,0x02,0xf3)	@ sha256h q0,q1,q13
	INST(0x6a,0x2c,0x14,0xf3)	@ sha256h2 q1,q2,q13
	INST(0xe0,0x2c,0x66,0xf3)	@ sha256su1 q9,q11,q8
	vld1.32		{q13},[r3]!
	vadd.i32	q12,q12,q10
	INST(0xe6,0x43,0xfa,0xf3)	@ sha256su0 q10,q11
	vmov		q2,q0
	INST(0x68,0x0c,0x02,0xf3)	@ sha256h q0,q1,q12
	INST(0x68,0x2c,0x14,0xf3)	@ sha256h2 q1,q2,q12
	INST(0xe2,0x4c,0x60,0xf3)	@ sha256su1 q10,q8,q9
	vld1.32		{q12},[r3]!
	vadd.i32	q13,q13,q11
	INST(0xe0,0x63,0xfa,0xf3)	@ sha256su0 q11,q8
	vmov		q2,q0
	INST(0x6a,0x0c,0x02,0xf3)	@ sha256h q0,q1,q13
	INST(0x6a,0x2c,0x14,0xf3)	@ sha256h2 q1,q2,q13
	INST(0xe4,0x6c,0x62,0xf3)	@ sha256su1 q11,q9,q10
	vld1.32		{q13},[r3]!
	vadd.i32	q12,q12,q8
	INST(0xe2,0x03,0xfa,0xf3)	@ sha256su0 q8,q9
	vmov		q2,q0
	INST(0x68,0x0c,0x02,0xf3)	@ sha256h q0,q1,q12
	INST(0x68,0x2c,0x14,0xf3)	@ sha256h2 q1,q2,q12
	INST(0xe6,0x0c,0x64,0xf3)	@ sha256su1 q8,q10,q11
	vld1.32		{q12},[r3]!
	vadd.i32	q13,q13,q9
	INST(0xe4,0x23,0xfa,0xf3)	@ sha256su0 q9,q10
	vmov		q2,q0
	INST(0x6a,0x0c,0x02,0xf3)	@ sha256h q0,q1,q13
	INST(0x6a,0x2c,0x14,0xf3)	@ sha256h2 q1,q2,q13
	INST(0xe0,0x2c,0x66,0xf3)	@ sha256su1 q9,q11,q8
	vld1.32		{q13},[r3]!
	vadd.i32	q12,q12,q10
	INST(0xe6,0x43,0xfa,0xf3)	@ sha256su0 q10,q11
	vmov		q2,q0
	INST(0x68,0x0c,0x02,0xf3)	@ sha256h q0,q1,q12
	INST(0x68,0x2c,0x14,0xf3)	@ sha256h2 q1,q2,q12
	INST(0xe2,0x4c,0x60,0xf3)	@ sha256su1 q10,q8,q9
	vld1.32		{q12},[r3]!
	vadd.i32	q13,q13,q11
	INST(0xe0,0x63,0xfa,0xf3)	@ sha256su0 q11,q8
	vmov		q2,q0
	INST(0x6a,0x0c,0x02,0xf3)	@ sha256h q0,q1,q13
	INST(0x6a,0x2c,0x14,0xf3)	@ sha256h2 q1,q2,q13
	INST(0xe4,0x6c,0x62,0xf3)	@ sha256su1 q11,q9,q10
	vld1.32		{q13},[r3]!
	vadd.i32	q12,q12,q8
	INST(0xe2,0x03,0xfa,0xf3)	@ sha256su0 q8,q9
	vmov		q2,q0
	INST(0x68,0x0c,0x02,0xf3)	@ sha256h q0,q1,q12
	INST(0x68,0x2c,0x14,0xf3)	@ sha256h2 q1,q2,q12
	INST(0xe6,0x0c,0x64,0xf3)	@ sha256su1 q8,q10,q11
	vld1.32		{q12},[r3]!
	vadd.i32	q13,q13,q9
	INST(0xe4,0x23,0xfa,0xf3)	@ sha256su0 q9,q10
	vmov		q2,q0
	INST(0x6a,0x0c,0x02,0xf3)	@ sha256h q0,q1,q13
	INST(0x6a,0x2c,0x14,0xf3)	@ sha256h2 q1,q2,q13
	INST(0xe0,0x2c,0x66,0xf3)	@ sha256su1 q9,q11,q8
	vld1.32		{q13},[r3]!
	vadd.i32	q12,q12,q10
	INST(0xe6,0x43,0xfa,0xf3)	@ sha256su0 q10,q11
	vmov		q2,q0
	INST(0x68,0x0c,0x02,0xf3)	@ sha256h q0,q1,q12
	INST(0x68,0x2c,0x14,0xf3)	@ sha256h2 q1,q2,q12
	INST(0xe2,0x4c,0x60,0xf3)	@ sha256su1 q10,q8,q9
	vld1.32		{q12},[r3]!
	vadd.i32	q13,q13,q11
	INST(0xe0,0x63,0xfa,0xf3)	@ sha256su0 q11,q8
	vmov		q2,q0
	INST(0x6a,0x0c,0x02,0xf3)	@ sha256h q0,q1,q13
	INST(0x6a,0x2c,0x14,0xf3)	@ sha256h2 q1,q2,q13
	INST(0xe4,0x6c,0x62,0xf3)	@ sha256su1 q11,q9,q10
	vld1.32		{q13},[r3]!
	vadd.i32	q12,q12,q8
	vmov		q2,q0
	INST(0x68,0x0c,0x02,0xf3)	@ sha256h q0,q1,q12
	INST(0x68,0x2c,0x14,0xf3)	@ sha256h2 q1,q2,q12

	vld1.32		{q12},[r3]!
	vadd.i32	q13,q13,q9
	vmov		q2,q0
	INST(0x6a,0x0c,0x02,0xf3)	@ sha256h q0,q1,q13
	INST(0x6a,0x2c,0x14,0xf3)	@ sha256h2 q1,q2,q13

	vld1.32		{q13},[r3]
	vadd.i32	q12,q12,q10
	sub		r3,r3,#256-16	@ rewind
	vmov		q2,q0
	INST(0x68,0x0c,0x02,0xf3)	@ sha256h q0,q1,q12
	INST(0x68,0x2c,0x14,0xf3)	@ sha256h2 q1,q2,q12

	vadd.i32	q13,q13,q11
	vmov		q2,q0
	INST(0x6a,0x0c,0x02,0xf3)	@ sha256h q0,q1,q13
	INST(0x6a,0x2c,0x14,0xf3)	@ sha256h2 q1,q2,q13

	vadd.i32	q0,q0,q14
	vadd.i32	q1,q1,q15
	it		ne
	bne		.Loop_v8

	vst1.32		{q0,q1},[r0]

	bx	lr		@ bx lr
.size	sha256_block_data_order_armv8,.-sha256_block_data_order_armv8
#endif
.asciz  "SHA256 block transform for ARMv4/NEON/ARMv8, CRYPTOGAMS by <appro@openssl.org>"
.align	2
#if __ARM_MAX_ARCH__>=7 && !defined(__KERNEL__)
.comm   OPENSSL_armcap_P,4,4
#endif
