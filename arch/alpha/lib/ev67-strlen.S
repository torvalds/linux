/*
 * arch/alpha/lib/ev67-strlen.S
 * 21264 version by Rick Gorton <rick.gorton@alpha-processor.com>
 *
 * Finds length of a 0-terminated string.  Optimized for the
 * Alpha architecture:
 *
 *	- memory accessed as aligned quadwords only
 *	- uses bcmpge to compare 8 bytes in parallel
 *
 * Much of the information about 21264 scheduling/coding comes from:
 *	Compiler Writer's Guide for the Alpha 21264
 *	abbreviated as 'CWG' in other comments here
 *	ftp.digital.com/pub/Digital/info/semiconductor/literature/dsc-library.html
 * Scheduling notation:
 *	E	- either cluster
 *	U	- upper subcluster; U0 - subcluster U0; U1 - subcluster U1
 *	L	- lower subcluster; L0 - subcluster L0; L1 - subcluster L1
 */

	.set noreorder
	.set noat

	.globl	strlen
	.ent	strlen
	.align 4
strlen:
	ldq_u	$1, 0($16)	# L : load first quadword ($16  may be misaligned)
	lda	$2, -1($31)	# E :
	insqh	$2, $16, $2	# U :
	andnot	$16, 7, $0	# E :

	or	$2, $1, $1	# E :
	cmpbge	$31, $1, $2	# E : $2  <- bitmask: bit i == 1 <==> i-th byte == 0
	nop			# E :
	bne	$2, $found	# U :

$loop:	ldq	$1, 8($0)	# L :
	addq	$0, 8, $0	# E : addr += 8
	cmpbge	$31, $1, $2	# E :
	beq	$2, $loop	# U :

$found:
	cttz	$2, $3		# U0 :
	addq	$0, $3, $0	# E :
	subq	$0, $16, $0	# E :
	ret	$31, ($26)	# L0 :

	.end	strlen
