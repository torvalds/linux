/* MN10300 Optimised simple memory to memory copy
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
# void *memcpy(void *dst, const void *src, size_t n)
#
###############################################################################
	.globl	memcpy
        .type	memcpy,@function
memcpy:
	movm	[d2,d3],(sp)
	mov	d0,(12,sp)
	mov	d1,(16,sp)
	mov	(20,sp),d2			# count
	mov	d0,a0				# dst
	mov	d1,a1				# src
	mov	d0,e3				# the return value

	cmp	+0,d2
	beq	memcpy_done			# return if zero-length copy

	# see if the three parameters are all four-byte aligned
	or	d0,d1,d3
	or	d2,d3
	and	+3,d3
	bne	memcpy_1			# jump if not

	# we want to transfer as much as we can in chunks of 32 bytes
	cmp	+31,d2
	bls	memcpy_4_remainder		# 4-byte aligned remainder

	movm	[exreg1],(sp)
	add	-32,d2
	mov	+32,d3

memcpy_4_loop:
	mov	(a1+),d0
	mov	(a1+),d1
	mov	(a1+),e0
	mov	(a1+),e1
	mov	(a1+),e4
	mov	(a1+),e5
	mov	(a1+),e6
	mov	(a1+),e7
	mov	d0,(a0+)
	mov	d1,(a0+)
	mov	e0,(a0+)
	mov	e1,(a0+)
	mov	e4,(a0+)
	mov	e5,(a0+)
	mov	e6,(a0+)
	mov	e7,(a0+)

	sub	d3,d2
	bcc	memcpy_4_loop

	movm	(sp),[exreg1]
	add	d3,d2
	beq	memcpy_4_no_remainder

memcpy_4_remainder:
	# cut 4-7 words down to 0-3
	cmp	+16,d2
	bcs	memcpy_4_three_or_fewer_words
	mov	(a1+),d0
	mov	(a1+),d1
	mov	(a1+),e0
	mov	(a1+),e1
	mov	d0,(a0+)
	mov	d1,(a0+)
	mov	e0,(a0+)
	mov	e1,(a0+)
	add	-16,d2
	beq	memcpy_4_no_remainder

	# copy the remaining 1, 2 or 3 words
memcpy_4_three_or_fewer_words:
	cmp	+8,d2
	bcs	memcpy_4_one_word
	beq	memcpy_4_two_words
	mov	(a1+),d0
	mov	d0,(a0+)
memcpy_4_two_words:
	mov	(a1+),d0
	mov	d0,(a0+)
memcpy_4_one_word:
	mov	(a1+),d0
	mov	d0,(a0+)

memcpy_4_no_remainder:
	# check we copied the correct amount
	# TODO: REMOVE CHECK
	sub	e3,a0,d2
	mov	(20,sp),d1
	cmp	d2,d1
	beq	memcpy_done
	break
	break
	break

memcpy_done:
	mov	e3,a0
	ret	[d2,d3],8

	# handle misaligned copying
memcpy_1:
	add	-1,d2
	mov	+1,d3
	setlb					# setlb requires the next insns
						# to occupy exactly 4 bytes

	sub	d3,d2
	movbu	(a1),d0
	movbu	d0,(a0)
	add_add	d3,a1,d3,a0
	lcc

	mov	e3,a0
	ret	[d2,d3],8

memcpy_end:
	.size	memcpy, memcpy_end-memcpy
