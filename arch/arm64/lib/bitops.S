/*
 * Based on arch/arm/lib/bitops.h
 *
 * Copyright (C) 2013 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/lse.h>

/*
 * x0: bits 5:0  bit offset
 *     bits 31:6 word offset
 * x1: address
 */
	.macro	bitop, name, llsc, lse
ENTRY(	\name	)
	and	w3, w0, #63		// Get bit offset
	eor	w0, w0, w3		// Clear low bits
	mov	x2, #1
	add	x1, x1, x0, lsr #3	// Get word offset
alt_lse "	prfm	pstl1strm, [x1]",	"nop"
	lsl	x3, x2, x3		// Create mask

alt_lse	"1:	ldxr	x2, [x1]",		"\lse	x3, [x1]"
alt_lse	"	\llsc	x2, x2, x3",		"nop"
alt_lse	"	stxr	w0, x2, [x1]",		"nop"
alt_lse	"	cbnz	w0, 1b",		"nop"

	ret
ENDPROC(\name	)
	.endm

	.macro	testop, name, llsc, lse
ENTRY(	\name	)
	and	w3, w0, #63		// Get bit offset
	eor	w0, w0, w3		// Clear low bits
	mov	x2, #1
	add	x1, x1, x0, lsr #3	// Get word offset
alt_lse "	prfm	pstl1strm, [x1]",	"nop"
	lsl	x4, x2, x3		// Create mask

alt_lse	"1:	ldxr	x2, [x1]",		"\lse	x4, x2, [x1]"
	lsr	x0, x2, x3
alt_lse	"	\llsc	x2, x2, x4",		"nop"
alt_lse	"	stlxr	w5, x2, [x1]",		"nop"
alt_lse	"	cbnz	w5, 1b",		"nop"
alt_lse	"	dmb	ish",			"nop"

	and	x0, x0, #1
	ret
ENDPROC(\name	)
	.endm

/*
 * Atomic bit operations.
 */
	bitop	change_bit, eor, steor
	bitop	clear_bit, bic, stclr
	bitop	set_bit, orr, stset

	testop	test_and_change_bit, eor, ldeoral
	testop	test_and_clear_bit, bic, ldclral
	testop	test_and_set_bit, orr, ldsetal
