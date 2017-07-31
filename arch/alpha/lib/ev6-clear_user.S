/*
 * arch/alpha/lib/ev6-clear_user.S
 * 21264 version contributed by Rick Gorton <rick.gorton@alpha-processor.com>
 *
 * Zero user space, handling exceptions as we go.
 *
 * We have to make sure that $0 is always up-to-date and contains the
 * right "bytes left to zero" value (and that it is updated only _after_
 * a successful copy).  There is also some rather minor exception setup
 * stuff.
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
 * Determining actual stalls (other than slotting) doesn't appear to be easy to do.
 * From perusing the source code context where this routine is called, it is
 * a fair assumption that significant fractions of entire pages are zeroed, so
 * it's going to be worth the effort to hand-unroll a big loop, and use wh64.
 * ASSUMPTION:
 *	The believed purpose of only updating $0 after a store is that a signal
 *	may come along during the execution of this chunk of code, and we don't
 *	want to leave a hole (and we also want to avoid repeating lots of work)
 */

#include <asm/export.h>
/* Allow an exception for an insn; exit if we get one.  */
#define EX(x,y...)			\
	99: x,##y;			\
	.section __ex_table,"a";	\
	.long 99b - .;			\
	lda $31, $exception-99b($31); 	\
	.previous

	.set noat
	.set noreorder
	.align 4

	.globl __clear_user
	.ent __clear_user
	.frame	$30, 0, $26
	.prologue 0

				# Pipeline info : Slotting & Comments
__clear_user:
	and	$17, $17, $0
	and	$16, 7, $4	# .. E  .. ..	: find dest head misalignment
	beq	$0, $zerolength # U  .. .. ..	:  U L U L

	addq	$0, $4, $1	# .. .. .. E	: bias counter
	and	$1, 7, $2	# .. .. E  ..	: number of misaligned bytes in tail
# Note - we never actually use $2, so this is a moot computation
# and we can rewrite this later...
	srl	$1, 3, $1	# .. E  .. ..	: number of quadwords to clear
	beq	$4, $headalign	# U  .. .. ..	: U L U L

/*
 * Head is not aligned.  Write (8 - $4) bytes to head of destination
 * This means $16 is known to be misaligned
 */
	EX( ldq_u $5, 0($16) )	# .. .. .. L	: load dst word to mask back in
	beq	$1, $onebyte	# .. .. U  ..	: sub-word store?
	mskql	$5, $16, $5	# .. U  .. ..	: take care of misaligned head
	addq	$16, 8, $16	# E  .. .. .. 	: L U U L

	EX( stq_u $5, -8($16) )	# .. .. .. L	:
	subq	$1, 1, $1	# .. .. E  ..	:
	addq	$0, $4, $0	# .. E  .. ..	: bytes left -= 8 - misalignment
	subq	$0, 8, $0	# E  .. .. ..	: U L U L

	.align	4
/*
 * (The .align directive ought to be a moot point)
 * values upon initial entry to the loop
 * $1 is number of quadwords to clear (zero is a valid value)
 * $2 is number of trailing bytes (0..7) ($2 never used...)
 * $16 is known to be aligned 0mod8
 */
$headalign:
	subq	$1, 16, $4	# .. .. .. E	: If < 16, we can not use the huge loop
	and	$16, 0x3f, $2	# .. .. E  ..	: Forward work for huge loop
	subq	$2, 0x40, $3	# .. E  .. ..	: bias counter (huge loop)
	blt	$4, $trailquad	# U  .. .. ..	: U L U L

/*
 * We know that we're going to do at least 16 quads, which means we are
 * going to be able to use the large block clear loop at least once.
 * Figure out how many quads we need to clear before we are 0mod64 aligned
 * so we can use the wh64 instruction.
 */

	nop			# .. .. .. E
	nop			# .. .. E  ..
	nop			# .. E  .. ..
	beq	$3, $bigalign	# U  .. .. ..	: U L U L : Aligned 0mod64

$alignmod64:
	EX( stq_u $31, 0($16) )	# .. .. .. L
	addq	$3, 8, $3	# .. .. E  ..
	subq	$0, 8, $0	# .. E  .. ..
	nop			# E  .. .. ..	: U L U L

	nop			# .. .. .. E
	subq	$1, 1, $1	# .. .. E  ..
	addq	$16, 8, $16	# .. E  .. ..
	blt	$3, $alignmod64	# U  .. .. ..	: U L U L

$bigalign:
/*
 * $0 is the number of bytes left
 * $1 is the number of quads left
 * $16 is aligned 0mod64
 * we know that we'll be taking a minimum of one trip through
 * CWG Section 3.7.6: do not expect a sustained store rate of > 1/cycle
 * We are _not_ going to update $0 after every single store.  That
 * would be silly, because there will be cross-cluster dependencies
 * no matter how the code is scheduled.  By doing it in slightly
 * staggered fashion, we can still do this loop in 5 fetches
 * The worse case will be doing two extra quads in some future execution,
 * in the event of an interrupted clear.
 * Assumes the wh64 needs to be for 2 trips through the loop in the future
 * The wh64 is issued on for the starting destination address for trip +2
 * through the loop, and if there are less than two trips left, the target
 * address will be for the current trip.
 */
	nop			# E :
	nop			# E :
	nop			# E :
	bis	$16,$16,$3	# E : U L U L : Initial wh64 address is dest
	/* This might actually help for the current trip... */

$do_wh64:
	wh64	($3)		# .. .. .. L1	: memory subsystem hint
	subq	$1, 16, $4	# .. .. E  ..	: Forward calculation - repeat the loop?
	EX( stq_u $31, 0($16) )	# .. L  .. ..
	subq	$0, 8, $0	# E  .. .. ..	: U L U L

	addq	$16, 128, $3	# E : Target address of wh64
	EX( stq_u $31, 8($16) )	# L :
	EX( stq_u $31, 16($16) )	# L :
	subq	$0, 16, $0	# E : U L L U

	nop			# E :
	EX( stq_u $31, 24($16) )	# L :
	EX( stq_u $31, 32($16) )	# L :
	subq	$0, 168, $5	# E : U L L U : two trips through the loop left?
	/* 168 = 192 - 24, since we've already completed some stores */

	subq	$0, 16, $0	# E :
	EX( stq_u $31, 40($16) )	# L :
	EX( stq_u $31, 48($16) )	# L :
	cmovlt	$5, $16, $3	# E : U L L U : Latency 2, extra mapping cycle

	subq	$1, 8, $1	# E :
	subq	$0, 16, $0	# E :
	EX( stq_u $31, 56($16) )	# L :
	nop			# E : U L U L

	nop			# E :
	subq	$0, 8, $0	# E :
	addq	$16, 64, $16	# E :
	bge	$4, $do_wh64	# U : U L U L

$trailquad:
	# zero to 16 quadwords left to store, plus any trailing bytes
	# $1 is the number of quadwords left to go.
	# 
	nop			# .. .. .. E
	nop			# .. .. E  ..
	nop			# .. E  .. ..
	beq	$1, $trailbytes	# U  .. .. ..	: U L U L : Only 0..7 bytes to go

$onequad:
	EX( stq_u $31, 0($16) )	# .. .. .. L
	subq	$1, 1, $1	# .. .. E  ..
	subq	$0, 8, $0	# .. E  .. ..
	nop			# E  .. .. ..	: U L U L

	nop			# .. .. .. E
	nop			# .. .. E  ..
	addq	$16, 8, $16	# .. E  .. ..
	bgt	$1, $onequad	# U  .. .. ..	: U L U L

	# We have an unknown number of bytes left to go.
$trailbytes:
	nop			# .. .. .. E
	nop			# .. .. E  ..
	nop			# .. E  .. ..
	beq	$0, $zerolength	# U  .. .. ..	: U L U L

	# $0 contains the number of bytes left to copy (0..31)
	# so we will use $0 as the loop counter
	# We know for a fact that $0 > 0 zero due to previous context
$onebyte:
	EX( stb $31, 0($16) )	# .. .. .. L
	subq	$0, 1, $0	# .. .. E  ..	:
	addq	$16, 1, $16	# .. E  .. ..	:
	bgt	$0, $onebyte	# U  .. .. ..	: U L U L

$zerolength:
$exception:			# Destination for exception recovery(?)
	nop			# .. .. .. E	:
	nop			# .. .. E  ..	:
	nop			# .. E  .. ..	:
	ret	$31, ($26), 1	# L0 .. .. ..	: L U L U
	.end __clear_user
	EXPORT_SYMBOL(__clear_user)
