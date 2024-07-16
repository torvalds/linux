/* SPDX-License-Identifier: GPL-2.0 */
/*
 * strlen() for PPC32
 *
 * Copyright (C) 2018 Christophe Leroy CS Systemes d'Information.
 *
 * Inspired from glibc implementation
 */
#include <asm/ppc_asm.h>
#include <asm/export.h>
#include <asm/cache.h>

	.text

/*
 * Algorithm:
 *
 * 1) Given a word 'x', we can test to see if it contains any 0 bytes
 *    by subtracting 0x01010101, and seeing if any of the high bits of each
 *    byte changed from 0 to 1. This works because the least significant
 *    0 byte must have had no incoming carry (otherwise it's not the least
 *    significant), so it is 0x00 - 0x01 == 0xff. For all other
 *    byte values, either they have the high bit set initially, or when
 *    1 is subtracted you get a value in the range 0x00-0x7f, none of which
 *    have their high bit set. The expression here is
 *    (x - 0x01010101) & ~x & 0x80808080), which gives 0x00000000 when
 *    there were no 0x00 bytes in the word.  You get 0x80 in bytes that
 *    match, but possibly false 0x80 matches in the next more significant
 *    byte to a true match due to carries.  For little-endian this is
 *    of no consequence since the least significant match is the one
 *    we're interested in, but big-endian needs method 2 to find which
 *    byte matches.
 * 2) Given a word 'x', we can test to see _which_ byte was zero by
 *    calculating ~(((x & ~0x80808080) - 0x80808080 - 1) | x | ~0x80808080).
 *    This produces 0x80 in each byte that was zero, and 0x00 in all
 *    the other bytes. The '| ~0x80808080' clears the low 7 bits in each
 *    byte, and the '| x' part ensures that bytes with the high bit set
 *    produce 0x00. The addition will carry into the high bit of each byte
 *    iff that byte had one of its low 7 bits set. We can then just see
 *    which was the most significant bit set and divide by 8 to find how
 *    many to add to the index.
 *    This is from the book 'The PowerPC Compiler Writer's Guide',
 *    by Steve Hoxey, Faraydon Karim, Bill Hay and Hank Warren.
 */

_GLOBAL(strlen)
	andi.   r0, r3, 3
	lis	r7, 0x0101
	addi	r10, r3, -4
	addic	r7, r7, 0x0101	/* r7 = 0x01010101 (lomagic) & clear XER[CA] */
	rotlwi	r6, r7, 31 	/* r6 = 0x80808080 (himagic) */
	bne-	3f
	.balign IFETCH_ALIGN_BYTES
1:	lwzu	r9, 4(r10)
2:	subf	r8, r7, r9
	and.	r8, r8, r6
	beq+	1b
	andc.	r8, r8, r9
	beq+	1b
	andc	r8, r9, r6
	orc	r9, r9, r6
	subfe	r8, r6, r8
	nor	r8, r8, r9
	cntlzw	r8, r8
	subf	r3, r3, r10
	srwi	r8, r8, 3
	add	r3, r3, r8
	blr

	/* Missaligned string: make sure bytes before string are seen not 0 */
3:	xor	r10, r10, r0
	orc	r8, r8, r8
	lwzu	r9, 4(r10)
	slwi	r0, r0, 3
	srw	r8, r8, r0
	orc	r9, r9, r8
	b	2b
EXPORT_SYMBOL(strlen)
