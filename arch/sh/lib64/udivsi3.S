/* SPDX-License-Identifier: GPL-2.0 */
	.global	__udivsi3
	.section	.text..SHmedia32,"ax"
	.align	2

/*
   inputs: r4,r5
   clobbered: r18,r19,r20,r21,r22,r25,tr0
   result in r0.
 */
__udivsi3:
	addz.l r5,r63,r22
	nsb r22,r0
	shlld r22,r0,r25
	shlri r25,48,r25
	movi 0xffffffffffffbb0c,r20 /* shift count eqiv 76 */
	sub r20,r25,r21
	mmulfx.w r21,r21,r19
	mshflo.w r21,r63,r21
	ptabs r18,tr0
	mmulfx.w r25,r19,r19
	sub r20,r0,r0
	/* bubble */
	msub.w r21,r19,r19

	/*
	 * It would be nice for scheduling to do this add to r21 before
	 * the msub.w, but we need a different value for r19 to keep
	 * errors under control.
	 */
	addi r19,-2,r21
	mulu.l r4,r21,r18
	mmulfx.w r19,r19,r19
	shlli r21,15,r21
	shlrd r18,r0,r18
	mulu.l r18,r22,r20
	mmacnfx.wl r25,r19,r21
	/* bubble */
	sub r4,r20,r25

	mulu.l r25,r21,r19
	addi r0,14,r0
	/* bubble */
	shlrd r19,r0,r19
	mulu.l r19,r22,r20
	add r18,r19,r18
	/* bubble */
	sub.l r25,r20,r25

	mulu.l r25,r21,r19
	addz.l r25,r63,r25
	sub r25,r22,r25
	shlrd r19,r0,r19
	mulu.l r19,r22,r20
	addi r25,1,r25
	add r18,r19,r18

	cmpgt r25,r20,r25
	add.l r18,r25,r0
	blink tr0,r63
