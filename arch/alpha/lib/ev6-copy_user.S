/*
 * arch/alpha/lib/ev6-copy_user.S
 *
 * 21264 version contributed by Rick Gorton <rick.gorton@alpha-processor.com>
 *
 * Copy to/from user space, handling exceptions as we go..  This
 * isn't exactly pretty.
 *
 * This is essentially the same as "memcpy()", but with a few twists.
 * Notably, we have to make sure that $0 is always up-to-date and
 * contains the right "bytes left to copy" value (and that it is updated
 * only _after_ a successful copy). There is also some rather minor
 * exception setup stuff..
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

#include <asm/export.h>
/* Allow an exception for an insn; exit if we get one.  */
#define EXI(x,y...)			\
	99: x,##y;			\
	.section __ex_table,"a";	\
	.long 99b - .;			\
	lda $31, $exitin-99b($31);	\
	.previous

#define EXO(x,y...)			\
	99: x,##y;			\
	.section __ex_table,"a";	\
	.long 99b - .;			\
	lda $31, $exitout-99b($31);	\
	.previous

	.set noat
	.align 4
	.globl __copy_user
	.ent __copy_user
				# Pipeline info: Slotting & Comments
__copy_user:
	.prologue 0
	mov $18, $0		# .. .. .. E
	subq $18, 32, $1	# .. .. E. ..	: Is this going to be a small copy?
	nop			# .. E  .. ..
	beq $18, $zerolength	# U  .. .. ..	: U L U L

	and $16,7,$3		# .. .. .. E	: is leading dest misalignment
	ble $1, $onebyteloop	# .. .. U  ..	: 1st branch : small amount of data
	beq $3, $destaligned	# .. U  .. ..	: 2nd (one cycle fetcher stall)
	subq $3, 8, $3		# E  .. .. ..	: L U U L : trip counter
/*
 * The fetcher stall also hides the 1 cycle cross-cluster stall for $3 (L --> U)
 * This loop aligns the destination a byte at a time
 * We know we have at least one trip through this loop
 */
$aligndest:
	EXI( ldbu $1,0($17) )	# .. .. .. L	: Keep loads separate from stores
	addq $16,1,$16		# .. .. E  ..	: Section 3.8 in the CWG
	addq $3,1,$3		# .. E  .. ..	:
	nop			# E  .. .. ..	: U L U L

/*
 * the -1 is to compensate for the inc($16) done in a previous quadpack
 * which allows us zero dependencies within either quadpack in the loop
 */
	EXO( stb $1,-1($16) )	# .. .. .. L	:
	addq $17,1,$17		# .. .. E  ..	: Section 3.8 in the CWG
	subq $0,1,$0		# .. E  .. ..	:
	bne $3, $aligndest	# U  .. .. ..	: U L U L

/*
 * If we fell through into here, we have a minimum of 33 - 7 bytes
 * If we arrived via branch, we have a minimum of 32 bytes
 */
$destaligned:
	and $17,7,$1		# .. .. .. E	: Check _current_ source alignment
	bic $0,7,$4		# .. .. E  ..	: number bytes as a quadword loop
	EXI( ldq_u $3,0($17) )	# .. L  .. ..	: Forward fetch for fallthrough code
	beq $1,$quadaligned	# U  .. .. ..	: U L U L

/*
 * In the worst case, we've just executed an ldq_u here from 0($17)
 * and we'll repeat it once if we take the branch
 */

/* Misaligned quadword loop - not unrolled.  Leave it that way. */
$misquad:
	EXI( ldq_u $2,8($17) )	# .. .. .. L	:
	subq $4,8,$4		# .. .. E  ..	:
	extql $3,$17,$3		# .. U  .. ..	:
	extqh $2,$17,$1		# U  .. .. ..	: U U L L

	bis $3,$1,$1		# .. .. .. E	:
	EXO( stq $1,0($16) )	# .. .. L  ..	:
	addq $17,8,$17		# .. E  .. ..	:
	subq $0,8,$0		# E  .. .. ..	: U L L U

	addq $16,8,$16		# .. .. .. E	:
	bis $2,$2,$3		# .. .. E  ..	:
	nop			# .. E  .. ..	:
	bne $4,$misquad		# U  .. .. ..	: U L U L

	nop			# .. .. .. E
	nop			# .. .. E  ..
	nop			# .. E  .. ..
	beq $0,$zerolength	# U  .. .. ..	: U L U L

/* We know we have at least one trip through the byte loop */
	EXI ( ldbu $2,0($17) )	# .. .. .. L	: No loads in the same quad
	addq $16,1,$16		# .. .. E  ..	: as the store (Section 3.8 in CWG)
	nop			# .. E  .. ..	:
	br $31, $dirtyentry	# L0 .. .. ..	: L U U L
/* Do the trailing byte loop load, then hop into the store part of the loop */

/*
 * A minimum of (33 - 7) bytes to do a quad at a time.
 * Based upon the usage context, it's worth the effort to unroll this loop
 * $0 - number of bytes to be moved
 * $4 - number of bytes to move as quadwords
 * $16 is current destination address
 * $17 is current source address
 */
$quadaligned:
	subq	$4, 32, $2	# .. .. .. E	: do not unroll for small stuff
	nop			# .. .. E  ..
	nop			# .. E  .. ..
	blt	$2, $onequad	# U  .. .. ..	: U L U L

/*
 * There is a significant assumption here that the source and destination
 * addresses differ by more than 32 bytes.  In this particular case, a
 * sparsity of registers further bounds this to be a minimum of 8 bytes.
 * But if this isn't met, then the output result will be incorrect.
 * Furthermore, due to a lack of available registers, we really can't
 * unroll this to be an 8x loop (which would enable us to use the wh64
 * instruction memory hint instruction).
 */
$unroll4:
	EXI( ldq $1,0($17) )	# .. .. .. L
	EXI( ldq $2,8($17) )	# .. .. L  ..
	subq	$4,32,$4	# .. E  .. ..
	nop			# E  .. .. ..	: U U L L

	addq	$17,16,$17	# .. .. .. E
	EXO( stq $1,0($16) )	# .. .. L  ..
	EXO( stq $2,8($16) )	# .. L  .. ..
	subq	$0,16,$0	# E  .. .. ..	: U L L U

	addq	$16,16,$16	# .. .. .. E
	EXI( ldq $1,0($17) )	# .. .. L  ..
	EXI( ldq $2,8($17) )	# .. L  .. ..
	subq	$4, 32, $3	# E  .. .. ..	: U U L L : is there enough for another trip?

	EXO( stq $1,0($16) )	# .. .. .. L
	EXO( stq $2,8($16) )	# .. .. L  ..
	subq	$0,16,$0	# .. E  .. ..
	addq	$17,16,$17	# E  .. .. ..	: U L L U

	nop			# .. .. .. E
	nop			# .. .. E  ..
	addq	$16,16,$16	# .. E  .. ..
	bgt	$3,$unroll4	# U  .. .. ..	: U L U L

	nop
	nop
	nop
	beq	$4, $noquads

$onequad:
	EXI( ldq $1,0($17) )
	subq	$4,8,$4
	addq	$17,8,$17
	nop

	EXO( stq $1,0($16) )
	subq	$0,8,$0
	addq	$16,8,$16
	bne	$4,$onequad

$noquads:
	nop
	nop
	nop
	beq $0,$zerolength

/*
 * For small copies (or the tail of a larger copy), do a very simple byte loop.
 * There's no point in doing a lot of complex alignment calculations to try to
 * to quadword stuff for a small amount of data.
 *	$0 - remaining number of bytes left to copy
 *	$16 - current dest addr
 *	$17 - current source addr
 */

$onebyteloop:
	EXI ( ldbu $2,0($17) )	# .. .. .. L	: No loads in the same quad
	addq $16,1,$16		# .. .. E  ..	: as the store (Section 3.8 in CWG)
	nop			# .. E  .. ..	:
	nop			# E  .. .. ..	: U L U L

$dirtyentry:
/*
 * the -1 is to compensate for the inc($16) done in a previous quadpack
 * which allows us zero dependencies within either quadpack in the loop
 */
	EXO ( stb $2,-1($16) )	# .. .. .. L	:
	addq $17,1,$17		# .. .. E  ..	: quadpack as the load
	subq $0,1,$0		# .. E  .. ..	: change count _after_ copy
	bgt $0,$onebyteloop	# U  .. .. ..	: U L U L

$zerolength:
$exitin:
$exitout:			# Destination for exception recovery(?)
	nop			# .. .. .. E
	nop			# .. .. E  ..
	nop			# .. E  .. ..
	ret $31,($26),1		# L0 .. .. ..	: L U L U

	.end __copy_user
	EXPORT_SYMBOL(__copy_user)
