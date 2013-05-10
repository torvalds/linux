/* MN10300 Optimised simple memory to memory copy, with support for overlapping
 * regions
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
# void *memmove(void *dst, const void *src, size_t n)
#
###############################################################################
	.globl	memmove
        .type	memmove,@function
memmove:
	# fall back to memcpy if dst < src to work bottom up
	cmp	d1,d0
	bcs	memmove_memcpy

	# work top down
	movm	[d2,d3],(sp)
	mov	d0,(12,sp)
	mov	d1,(16,sp)
	mov	(20,sp),d2			# count
	add	d0,d2,a0			# dst end
	add	d1,d2,a1			# src end
	mov	d0,e3				# the return value

	cmp	+0,d2
	beq	memmove_done			# return if zero-length copy

	# see if the three parameters are all four-byte aligned
	or	d0,d1,d3
	or	d2,d3
	and	+3,d3
	bne	memmove_1			# jump if not

	# we want to transfer as much as we can in chunks of 32 bytes
	add	-4,a1
	cmp	+31,d2
	bls	memmove_4_remainder		# 4-byte aligned remainder

	add	-32,d2
	mov	+32,d3

memmove_4_loop:
	mov	(a1),d0
	sub_sub	+4,a1,+4,a0
	mov	d0,(a0)
	mov	(a1),d1
	sub_sub	+4,a1,+4,a0
	mov	d1,(a0)

	mov	(a1),d0
	sub_sub	+4,a1,+4,a0
	mov	d0,(a0)
	mov	(a1),d1
	sub_sub	+4,a1,+4,a0
	mov	d1,(a0)

	mov	(a1),d0
	sub_sub	+4,a1,+4,a0
	mov	d0,(a0)
	mov	(a1),d1
	sub_sub	+4,a1,+4,a0
	mov	d1,(a0)

	mov	(a1),d0
	sub_sub	+4,a1,+4,a0
	mov	d0,(a0)
	mov	(a1),d1
	sub_sub	+4,a1,+4,a0
	mov	d1,(a0)

	sub	d3,d2
	bcc	memmove_4_loop

	add	d3,d2
	beq	memmove_4_no_remainder

memmove_4_remainder:
	# cut 4-7 words down to 0-3
	cmp	+16,d2
	bcs	memmove_4_three_or_fewer_words
	mov	(a1),d0
	sub_sub	+4,a1,+4,a0
	mov	d0,(a0)
	mov	(a1),d1
	sub_sub	+4,a1,+4,a0
	mov	d1,(a0)
	mov	(a1),e0
	sub_sub	+4,a1,+4,a0
	mov	e0,(a0)
	mov	(a1),e1
	sub_sub	+4,a1,+4,a0
	mov	e1,(a0)
	add	-16,d2
	beq	memmove_4_no_remainder

	# copy the remaining 1, 2 or 3 words
memmove_4_three_or_fewer_words:
	cmp	+8,d2
	bcs	memmove_4_one_word
	beq	memmove_4_two_words
	mov	(a1),d0
	sub_sub	+4,a1,+4,a0
	mov	d0,(a0)
memmove_4_two_words:
	mov	(a1),d0
	sub_sub	+4,a1,+4,a0
	mov	d0,(a0)
memmove_4_one_word:
	mov	(a1),d0
	sub_sub	+4,a1,+4,a0
	mov	d0,(a0)

memmove_4_no_remainder:
	# check we copied the correct amount
	# TODO: REMOVE CHECK
	sub	e3,a0,d2
	beq	memmove_done
	break
	break
	break

memmove_done:
	mov	e3,a0
	ret	[d2,d3],8

	# handle misaligned copying
memmove_1:
	add	-1,a1
	add	-1,d2
	mov	+1,d3
	setlb					# setlb requires the next insns
						# to occupy exactly 4 bytes

	sub	d3,d2
	movbu	(a1),d0
	sub_sub	d3,a1,d3,a0
	movbu	d0,(a0)
	lcc

	mov	e3,a0
	ret	[d2,d3],8

memmove_memcpy:
	jmp	memcpy

memmove_end:
	.size	memmove, memmove_end-memmove
