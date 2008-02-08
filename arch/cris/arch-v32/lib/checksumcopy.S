/*
 * A fast checksum+copy routine using movem
 * Copyright (c) 1998-2007 Axis Communications AB
 *
 * Authors:	Bjorn Wesen
 *
 * csum_partial_copy_nocheck(const char *src, char *dst,
 *		             int len, unsigned int sum)
 */

	.globl	csum_partial_copy_nocheck
csum_partial_copy_nocheck:

	;; r10 - src
	;; r11 - dst
	;; r12 - length
	;; r13 - checksum

	;; Optimized for large packets
	subq	10*4, $r12
	blt	_word_loop
	move.d	$r12, $acr

	subq	9*4,$sp
	clearf	c
	movem	$r8,[$sp]

	;; do a movem copy and checksum
1:	;; A failing userspace access (the read) will have this as PC.
_mloop:	movem	[$r10+],$r9	; read 10 longwords
	addoq	-10*4, $acr, $acr ; loop counter in latency cycle
	movem	$r9,[$r11+]	; write 10 longwords

	;; perform dword checksumming on the 10 longwords
	addc	$r0,$r13
	addc	$r1,$r13
	addc	$r2,$r13
	addc	$r3,$r13
	addc	$r4,$r13
	addc	$r5,$r13
	addc	$r6,$r13
	addc	$r7,$r13
	addc	$r8,$r13
	addc	$r9,$r13

	;; test $acr, without trashing carry.
	move.d	$acr, $acr
	bpl	_mloop
	;; r12 <= acr  is needed after mloop and in the exception handlers.
	move.d	$acr, $r12

	;; fold the last carry into r13
	addc	0, $r13
	movem	[$sp+],$r8	; restore regs

_word_loop:
	addq	10*4,$r12	; compensate for last loop underflowing length

	;; fold 32-bit checksum into a 16-bit checksum, to avoid carries below
	;; r9 can be used as temporary.
	move.d	$r13,$r9
	lsrq	16,$r9		; r0 = checksum >> 16
	and.d	0xffff,$r13	; checksum = checksum & 0xffff

	subq	2, $r12
	blt	_no_words
	add.d	$r9,$r13	; checksum += r0

	;; copy and checksum the rest of the words
2:	;; A failing userspace access for the read below will have this as PC.
_wloop:	move.w	[$r10+],$r9
	addu.w	$r9,$r13
	subq	2,$r12
	bge	_wloop
	move.w	$r9,[$r11+]

_no_words:
	addq	2,$r12
	bne	_do_byte
	nop
	ret
	move.d	$r13,$r10

_do_byte:
	;; copy and checksum the last byte
3:	;; A failing userspace access for the read below will have this as PC.
	move.b	[$r10],$r9
	addu.b	$r9,$r13
	move.b	$r9,[$r11]
	ret
	move.d	$r13,$r10
