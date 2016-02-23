/*
 * Simplistic strlen() implementation for SHmedia.
 *
 * Copyright (C) 2003  Paul Mundt <lethal@linux-sh.org>
 */

	.section .text..SHmedia32,"ax"
	.globl	strlen
	.type	strlen,@function

	.balign 16
strlen:
	ptabs	r18, tr4

	/*
	 * Note: We could easily deal with the NULL case here with a simple
	 * sanity check, though it seems that the behavior we want is to fault
	 * in the event that r2 == NULL, so we don't bother.
	 */
/*	beqi    r2, 0, tr4 */	! Sanity check

	movi	-1, r0
	pta/l	loop, tr0
loop:
	ld.b	r2, 0, r1
	addi	r2, 1, r2
	addi	r0, 1, r0
	bnei/l	r1, 0, tr0

	or	r0, r63, r2
	blink	tr4, r63

	.size	strlen,.-strlen
