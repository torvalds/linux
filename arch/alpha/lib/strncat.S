/* SPDX-License-Identifier: GPL-2.0 */
/*
 * arch/alpha/lib/strncat.S
 * Contributed by Richard Henderson (rth@tamu.edu)
 *
 * Append no more than COUNT characters from the null-terminated string SRC
 * to the null-terminated string DST.  Always null-terminate the new DST.
 *
 * This differs slightly from the semantics in libc in that we never write
 * past count, whereas libc may write to count+1.  This follows the generic
 * implementation in lib/string.c and is, IMHO, more sensible.
 */
#include <linux/export.h>
	.text

	.align 3
	.globl strncat
	.ent strncat
strncat:
	.frame $30, 0, $26
	.prologue 0

	mov	$16, $0		# set up return value
	beq	$18, $zerocount

	/* Find the end of the string.  */

	ldq_u   $1, 0($16)	# load first quadword ($16 may be misaligned)
	lda     $2, -1($31)
	insqh   $2, $16, $2
	andnot  $16, 7, $16
	or      $2, $1, $1
	cmpbge  $31, $1, $2	# bits set iff byte == 0
	bne     $2, $found

$loop:	ldq     $1, 8($16)
	addq    $16, 8, $16
	cmpbge  $31, $1, $2
	beq     $2, $loop

$found:	negq    $2, $3		# clear all but least set bit
	and     $2, $3, $2

	and     $2, 0xf0, $3	# binary search for that set bit
	and	$2, 0xcc, $4
	and	$2, 0xaa, $5
	cmovne	$3, 4, $3
	cmovne	$4, 2, $4
	cmovne	$5, 1, $5
	addq	$3, $4, $3
	addq	$16, $5, $16
	addq	$16, $3, $16

	/* Now do the append.  */

	bsr	$23, __stxncpy

	/* Worry about the null termination.  */

	zapnot	$1, $27, $2	# was last byte a null?
	bne	$2, 0f
	ret

0:	cmplt	$27, $24, $2	# did we fill the buffer completely?
	or	$2, $18, $2
	bne	$2, 2f

	and	$24, 0x80, $2	# no zero next byte
	bne	$2, 1f

	/* Here there are bytes left in the current word.  Clear one.  */
	addq	$24, $24, $24	# end-of-count bit <<= 1
2:	zap	$1, $24, $1
	stq_u	$1, 0($16)
	ret

1:	/* Here we must read the next DST word and clear the first byte.  */
	ldq_u	$1, 8($16)
	zap	$1, 1, $1
	stq_u	$1, 8($16)

$zerocount:
	ret

	.end strncat
	EXPORT_SYMBOL(strncat)
