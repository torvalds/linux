	.section	.text..SHmedia32,"ax"
	.align	2
	.global	__udivdi3
__udivdi3:
	shlri r3,1,r4
	nsb r4,r22
	shlld r3,r22,r6
	shlri r6,49,r5
	movi 0xffffffffffffbaf1,r21 /* .l shift count 17.  */
	sub r21,r5,r1
	mmulfx.w r1,r1,r4
	mshflo.w r1,r63,r1
	sub r63,r22,r20 // r63 == 64 % 64
	mmulfx.w r5,r4,r4
	pta large_divisor,tr0
	addi r20,32,r9
	msub.w r1,r4,r1
	madd.w r1,r1,r1
	mmulfx.w r1,r1,r4
	shlri r6,32,r7
	bgt/u r9,r63,tr0 // large_divisor
	mmulfx.w r5,r4,r4
	shlri r2,32+14,r19
	addi r22,-31,r0
	msub.w r1,r4,r1

	mulu.l r1,r7,r4
	addi r1,-3,r5
	mulu.l r5,r19,r5
	sub r63,r4,r4 // Negate to make sure r1 ends up <= 1/r2
	shlri r4,2,r4 /* chop off leading %0000000000000000 001.00000000000 - or, as
	                 the case may be, %0000000000000000 000.11111111111, still */
	muls.l r1,r4,r4 /* leaving at least one sign bit.  */
	mulu.l r5,r3,r8
	mshalds.l r1,r21,r1
	shari r4,26,r4
	shlld r8,r0,r8
	add r1,r4,r1 // 31 bit unsigned reciprocal now in r1 (msb equiv. 0.5)
	sub r2,r8,r2
	/* Can do second step of 64 : 32 div now, using r1 and the rest in r2.  */

	shlri r2,22,r21
	mulu.l r21,r1,r21
	shlld r5,r0,r8
	addi r20,30-22,r0
	shlrd r21,r0,r21
	mulu.l r21,r3,r5
	add r8,r21,r8
	mcmpgt.l r21,r63,r21 // See Note 1
	addi r20,30,r0
	mshfhi.l r63,r21,r21
	sub r2,r5,r2
	andc r2,r21,r2

	/* small divisor: need a third divide step */
	mulu.l r2,r1,r7
	ptabs r18,tr0
	addi r2,1,r2
	shlrd r7,r0,r7
	mulu.l r7,r3,r5
	add r8,r7,r8
	sub r2,r3,r2
	cmpgt r2,r5,r5
	add r8,r5,r2
	/* could test r3 here to check for divide by zero.  */
	blink tr0,r63

large_divisor:
	mmulfx.w r5,r4,r4
	shlrd r2,r9,r25
	shlri r25,32,r8
	msub.w r1,r4,r1

	mulu.l r1,r7,r4
	addi r1,-3,r5
	mulu.l r5,r8,r5
	sub r63,r4,r4 // Negate to make sure r1 ends up <= 1/r2
	shlri r4,2,r4 /* chop off leading %0000000000000000 001.00000000000 - or, as
	                 the case may be, %0000000000000000 000.11111111111, still */
	muls.l r1,r4,r4 /* leaving at least one sign bit.  */
	shlri r5,14-1,r8
	mulu.l r8,r7,r5
	mshalds.l r1,r21,r1
	shari r4,26,r4
	add r1,r4,r1 // 31 bit unsigned reciprocal now in r1 (msb equiv. 0.5)
	sub r25,r5,r25
	/* Can do second step of 64 : 32 div now, using r1 and the rest in r25.  */

	shlri r25,22,r21
	mulu.l r21,r1,r21
	pta no_lo_adj,tr0
	addi r22,32,r0
	shlri r21,40,r21
	mulu.l r21,r7,r5
	add r8,r21,r8
	shlld r2,r0,r2
	sub r25,r5,r25
	bgtu/u r7,r25,tr0 // no_lo_adj
	addi r8,1,r8
	sub r25,r7,r25
no_lo_adj:
	mextr4 r2,r25,r2

	/* large_divisor: only needs a few adjustments.  */
	mulu.l r8,r6,r5
	ptabs r18,tr0
	/* bubble */
	cmpgtu r5,r2,r5
	sub r8,r5,r2
	blink tr0,r63
	
/* Note 1: To shift the result of the second divide stage so that the result
   always fits into 32 bits, yet we still reduce the rest sufficiently
   would require a lot of instructions to do the shifts just right.  Using
   the full 64 bit shift result to multiply with the divisor would require
   four extra instructions for the upper 32 bits (shift / mulu / shift / sub).
   Fortunately, if the upper 32 bits of the shift result are nonzero, we
   know that the rest after taking this partial result into account will
   fit into 32 bits.  So we just clear the upper 32 bits of the rest if the
   upper 32 bits of the partial result are nonzero.  */
