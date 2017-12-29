/* SPDX-License-Identifier: GPL-2.0 */
/*
 * arch/alpha/lib/ev67-strncat.S
 * 21264 version contributed by Rick Gorton <rick.gorton@api-networks.com>
 *
 * Append no more than COUNT characters from the null-terminated string SRC
 * to the null-terminated string DST.  Always null-terminate the new DST.
 *
 * This differs slightly from the semantics in libc in that we never write
 * past count, whereas libc may write to count+1.  This follows the generic
 * implementation in lib/string.c and is, IMHO, more sensible.
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
 */

#include <asm/export.h>
	.text

	.align 4
	.globl strncat
	.ent strncat
strncat:
	.frame $30, 0, $26
	.prologue 0

	mov	$16, $0		# set up return value
	beq	$18, $zerocount	# U :
	/* Find the end of the string.  */
	ldq_u   $1, 0($16)	# L : load first quadword ($16 may be misaligned)
	lda     $2, -1($31)	# E :

	insqh   $2, $0, $2	# U :
	andnot  $16, 7, $16	# E :
	nop			# E :
	or      $2, $1, $1	# E :

	nop			# E :
	nop			# E :
	cmpbge  $31, $1, $2	# E : bits set iff byte == 0
	bne     $2, $found	# U :

$loop:	ldq     $1, 8($16)	# L :
	addq    $16, 8, $16	# E :
	cmpbge  $31, $1, $2	# E :
	beq     $2, $loop	# U :

$found:	cttz	$2, $3		# U0 :
	addq	$16, $3, $16	# E :
	nop			# E :
	bsr	$23, __stxncpy	# L0 :/* Now do the append.  */

	/* Worry about the null termination.  */

	zapnot	$1, $27, $2	# U : was last byte a null?
	cmplt	$27, $24, $5	# E : did we fill the buffer completely?
	bne	$2, 0f		# U :
	ret			# L0 :

0:	or	$5, $18, $2	# E :
	nop
	bne	$2, 2f		# U :
	and	$24, 0x80, $3	# E : no zero next byte

	nop			# E :
	bne	$3, 1f		# U :
	/* Here there are bytes left in the current word.  Clear one.  */
	addq	$24, $24, $24	# E : end-of-count bit <<= 1
	nop			# E :

2:	zap	$1, $24, $1	# U :
	nop			# E :
	stq_u	$1, 0($16)	# L :
	ret			# L0 :

1:	/* Here we must clear the first byte of the next DST word */
	stb	$31, 8($16)	# L :
	nop			# E :
	nop			# E :
	ret			# L0 :

$zerocount:
	nop			# E :
	nop			# E :
	nop			# E :
	ret			# L0 :

	.end strncat
	EXPORT_SYMBOL(strncat)
