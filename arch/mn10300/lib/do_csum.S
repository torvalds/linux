/* Optimised simple memory checksum
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#include <asm/cache.h>

	.section .text
	.balign	L1_CACHE_BYTES

###############################################################################
#
# unsigned int do_csum(const unsigned char *buff, int len)
#
###############################################################################
	.globl	do_csum
	.type	do_csum,@function
do_csum:
	movm	[d2,d3],(sp)
	mov	d1,d2				# count
	mov	d0,a0				# buff
	mov	a0,a1
	clr	d1				# accumulator

	cmp	+0,d2
	ble	do_csum_done			# check for zero length or negative

	# 4-byte align the buffer pointer
	btst	+3,a0
	beq	do_csum_now_4b_aligned

	btst	+1,a0
	beq	do_csum_addr_not_odd
	movbu	(a0),d0
	inc	a0
	asl	+8,d0
	add	d0,d1
	add	-1,d2

do_csum_addr_not_odd:
	cmp	+2,d2
	bcs	do_csum_fewer_than_4
	btst	+2,a0
	beq	do_csum_now_4b_aligned
	movhu	(a0+),d0
	add	d0,d1
	add	-2,d2
	cmp	+4,d2
	bcs	do_csum_fewer_than_4

do_csum_now_4b_aligned:
	# we want to checksum as much as we can in chunks of 32 bytes
	cmp	+31,d2
	bls	do_csum_remainder		# 4-byte aligned remainder

	add	-32,d2
	mov	+32,d3

do_csum_loop:
	mov	(a0+),d0
	mov	(a0+),e0
	mov	(a0+),e1
	mov	(a0+),e3
	add	d0,d1
	addc	e0,d1
	addc	e1,d1
	addc	e3,d1
	mov	(a0+),d0
	mov	(a0+),e0
	mov	(a0+),e1
	mov	(a0+),e3
	addc	d0,d1
	addc	e0,d1
	addc	e1,d1
	addc	e3,d1
	addc	+0,d1

	sub	d3,d2
	bcc	do_csum_loop

	add	d3,d2
	beq	do_csum_done

do_csum_remainder:
	# cut 16-31 bytes down to 0-15
	cmp	+16,d2
	bcs	do_csum_fewer_than_16
	mov	(a0+),d0
	mov	(a0+),e0
	mov	(a0+),e1
	mov	(a0+),e3
	add	d0,d1
	addc	e0,d1
	addc	e1,d1
	addc	e3,d1
	addc	+0,d1
	add	-16,d2
	beq	do_csum_done

do_csum_fewer_than_16:
	# copy the remaining whole words
	cmp	+4,d2
	bcs	do_csum_fewer_than_4
	cmp	+8,d2
	bcs	do_csum_one_word
	cmp	+12,d2
	bcs	do_csum_two_words
	mov	(a0+),d0
	add	d0,d1
	addc	+0,d1
do_csum_two_words:
	mov	(a0+),d0
	add	d0,d1
	addc	+0,d1
do_csum_one_word:
	mov	(a0+),d0
	add	d0,d1
	addc	+0,d1

do_csum_fewer_than_4:
	and	+3,d2
	beq	do_csum_done
	xor_cmp	d0,d0,+2,d2
	bcs	do_csum_fewer_than_2
	movhu	(a0+),d0
	and	+1,d2
	beq	do_csum_add_last_bit
do_csum_fewer_than_2:
	movbu	(a0),d3
	add	d3,d0
do_csum_add_last_bit:
	add	d0,d1
	addc	+0,d1

do_csum_done:
	# compress the checksum down to 16 bits
	mov	+0xffff0000,d0
	and	d1,d0
	asl	+16,d1
	add	d1,d0
	addc	+0xffff,d0
	lsr	+16,d0

	# flip the halves of the word result if the buffer was oddly aligned
	and	+1,a1
	beq	do_csum_not_oddly_aligned
	swaph	d0,d0				# exchange bits 15:8 with 7:0

do_csum_not_oddly_aligned:
	ret	[d2,d3],8

	.size	do_csum, .-do_csum
