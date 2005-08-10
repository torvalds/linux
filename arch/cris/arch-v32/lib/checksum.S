/*
 * A fast checksum routine using movem
 * Copyright (c) 1998-2001, 2003 Axis Communications AB
 *
 * csum_partial(const unsigned char * buff, int len, unsigned int sum)
 */

	.globl	csum_partial
csum_partial:

	;; r10 - src
	;; r11 - length
	;; r12 - checksum

	;; check for breakeven length between movem and normal word looping versions
	;; we also do _NOT_ want to compute a checksum over more than the
	;; actual length when length < 40

	cmpu.w	80,$r11
	blo	_word_loop
	nop

	;; need to save the registers we use below in the movem loop
	;; this overhead is why we have a check above for breakeven length
	;; only r0 - r8 have to be saved, the other ones are clobber-able
	;; according to the ABI

	subq	9*4,$sp
	subq	10*4,$r11	; update length for the first loop
	movem	$r8,[$sp]

	;; do a movem checksum

_mloop:	movem	[$r10+],$r9	; read 10 longwords

	;; perform dword checksumming on the 10 longwords

	add.d	$r0,$r12
	addc	$r1,$r12
	addc	$r2,$r12
	addc	$r3,$r12
	addc	$r4,$r12
	addc	$r5,$r12
	addc	$r6,$r12
	addc	$r7,$r12
	addc	$r8,$r12
	addc	$r9,$r12

	;; fold the carry into the checksum, to avoid having to loop the carry
	;; back into the top

	addc	0,$r12
	addc	0,$r12		; do it again, since we might have generated a carry

	subq	10*4,$r11
	bge	_mloop
	nop

	addq	10*4,$r11	; compensate for last loop underflowing length

	movem	[$sp+],$r8	; restore regs

_word_loop:
	;; only fold if there is anything to fold.

	cmpq	0,$r12
	beq	_no_fold

	;; fold 32-bit checksum into a 16-bit checksum, to avoid carries below.
	;; r9 and r13 can be used as temporaries.

	moveq	-1,$r9		; put 0xffff in r9, faster than move.d 0xffff,r9
	lsrq	16,$r9

	move.d	$r12,$r13
	lsrq	16,$r13		; r13 = checksum >> 16
	and.d	$r9,$r12		; checksum = checksum & 0xffff
	add.d	$r13,$r12		; checksum += r13
	move.d	$r12,$r13		; do the same again, maybe we got a carry last add
	lsrq	16,$r13
	and.d	$r9,$r12
	add.d	$r13,$r12

_no_fold:
	cmpq	2,$r11
	blt	_no_words
	nop

	;; checksum the rest of the words

	subq	2,$r11

_wloop:	subq	2,$r11
	bge	_wloop
	addu.w	[$r10+],$r12

	addq	2,$r11

_no_words:
	;; see if we have one odd byte more
	cmpq	1,$r11
	beq	_do_byte
	nop
	ret
	move.d	$r12,$r10

_do_byte:
	;; copy and checksum the last byte
	addu.b	[$r10],$r12
	ret
	move.d	$r12,$r10
