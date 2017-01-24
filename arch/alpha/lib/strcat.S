/*
 * arch/alpha/lib/strcat.S
 * Contributed by Richard Henderson (rth@tamu.edu)
 *
 * Append a null-terminated string from SRC to DST.
 */
#include <asm/export.h>

	.text

	.align 3
	.globl strcat
	.ent strcat
strcat:
	.frame $30, 0, $26
	.prologue 0

	mov	$16, $0		# set up return value

	/* Find the end of the string.  */

	ldq_u   $1, 0($16)	# load first quadword (a0 may be misaligned)
	lda     $2, -1
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

	mov	$26, $23
	br	__stxcpy

	.end strcat
EXPORT_SYMBOL(strcat);
