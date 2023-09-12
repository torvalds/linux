/* SPDX-License-Identifier: GPL-2.0 */
/*
 * arch/alpha/lib/ev67-strcat.S
 * 21264 version contributed by Rick Gorton <rick.gorton@alpha-processor.com>
 *
 * Append a null-terminated string from SRC to DST.
 *
 * Much of the information about 21264 scheduling/coding comes from:
 *	Compiler Writer's Guide for the Alpha 21264
 *	abbreviated as 'CWG' in other comments here
 *	ftp.digital.com/pub/Digital/info/semiconductor/literature/dsc-library.html
 * Scheduling notation:
 *	E	- either cluster
 *	U	- upper subcluster; U0 - subcluster U0; U1 - subcluster U1
 *	L	- lower subcluster; L0 - subcluster L0; L1 - subcluster L1
 * Try not to change the actual algorithm if possible for consistency.
 * Commentary: It seems bogus to walk the input string twice - once
 * to determine the length, and then again while doing the copy.
 * A significant (future) enhancement would be to only read the input
 * string once.
 */

#include <linux/export.h>
	.text

	.align 4
	.globl strcat
	.ent strcat
strcat:
	.frame $30, 0, $26
	.prologue 0

	mov	$16, $0		# E : set up return value
	/* Find the end of the string.  */
	ldq_u   $1, 0($16)	# L : load first quadword (a0 may be misaligned)
	lda     $2, -1		# E :
	insqh   $2, $16, $2	# U :

	andnot  $16, 7, $16	# E :
	or      $2, $1, $1	# E :
	cmpbge  $31, $1, $2	# E : bits set iff byte == 0
	bne     $2, $found	# U :

$loop:	ldq     $1, 8($16)	# L :
	addq    $16, 8, $16	# E :
	cmpbge  $31, $1, $2	# E :
	beq     $2, $loop	# U :

$found:	cttz	$2, $3		# U0 :
	addq	$16, $3, $16	# E :
	/* Now do the append.  */
	mov	$26, $23	# E :
	br	__stxcpy	# L0 :

	.end strcat
	EXPORT_SYMBOL(strcat)
