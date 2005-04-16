/*
 * arch/alpha/lib/ev6-strncpy_from_user.S
 * 21264 version contributed by Rick Gorton <rick.gorton@alpha-processor.com>
 *
 * Just like strncpy except in the return value:
 *
 * -EFAULT       if an exception occurs before the terminator is copied.
 * N             if the buffer filled.
 *
 * Otherwise the length of the string is returned.
 *
 * Much of the information about 21264 scheduling/coding comes from:
 *	Compiler Writer's Guide for the Alpha 21264
 *	abbreviated as 'CWG' in other comments here
 *	ftp.digital.com/pub/Digital/info/semiconductor/literature/dsc-library.html
 * Scheduling notation:
 *	E	- either cluster
 *	U	- upper subcluster; U0 - subcluster U0; U1 - subcluster U1
 *	L	- lower subcluster; L0 - subcluster L0; L1 - subcluster L1
 * A bunch of instructions got moved and temp registers were changed
 * to aid in scheduling.  Control flow was also re-arranged to eliminate
 * branches, and to provide longer code sequences to enable better scheduling.
 * A total rewrite (using byte load/stores for start & tail sequences)
 * is desirable, but very difficult to do without a from-scratch rewrite.
 * Save that for the future.
 */


#include <asm/errno.h>
#include <asm/regdef.h>


/* Allow an exception for an insn; exit if we get one.  */
#define EX(x,y...)			\
	99: x,##y;			\
	.section __ex_table,"a";	\
	.long 99b - .;			\
	lda $31, $exception-99b($0); 	\
	.previous


	.set noat
	.set noreorder
	.text

	.globl __strncpy_from_user
	.ent __strncpy_from_user
	.frame $30, 0, $26
	.prologue 0

	.align 4
__strncpy_from_user:
	and	a0, 7, t3	# E : find dest misalignment
	beq	a2, $zerolength	# U :

	/* Are source and destination co-aligned?  */
	mov	a0, v0		# E : save the string start
	xor	a0, a1, t4	# E :
	EX( ldq_u t1, 0(a1) )	# L : Latency=3 load first quadword
	ldq_u	t0, 0(a0)	# L : load first (partial) aligned dest quadword

	addq	a2, t3, a2	# E : bias count by dest misalignment
	subq	a2, 1, a3	# E :
	addq	zero, 1, t10	# E :
	and	t4, 7, t4	# E : misalignment between the two

	and	a3, 7, t6	# E : number of tail bytes
	sll	t10, t6, t10	# E : t10 = bitmask of last count byte
	bne	t4, $unaligned	# U :
	lda	t2, -1		# E : build a mask against false zero

	/*
	 * We are co-aligned; take care of a partial first word.
	 * On entry to this basic block:
	 * t0 == the first destination word for masking back in
	 * t1 == the first source word.
	 */

	srl	a3, 3, a2	# E : a2 = loop counter = (count - 1)/8
	addq	a1, 8, a1	# E :
	mskqh	t2, a1, t2	# U :   detection in the src word
	nop

	/* Create the 1st output word and detect 0's in the 1st input word.  */
	mskqh	t1, a1, t3	# U :
	mskql	t0, a1, t0	# U : assemble the first output word
	ornot	t1, t2, t2	# E :
	nop

	cmpbge	zero, t2, t8	# E : bits set iff null found
	or	t0, t3, t0	# E :
	beq	a2, $a_eoc	# U :
	bne	t8, $a_eos	# U : 2nd branch in a quad.  Bad.

	/* On entry to this basic block:
	 * t0 == a source quad not containing a null.
	 * a0 - current aligned destination address
	 * a1 - current aligned source address
	 * a2 - count of quadwords to move.
	 * NOTE: Loop improvement - unrolling this is going to be
	 *	a huge win, since we're going to stall otherwise.
	 *	Fix this later.  For _really_ large copies, look
	 *	at using wh64 on a look-ahead basis.  See the code
	 *	in clear_user.S and copy_user.S.
	 * Presumably, since (a0) and (a1) do not overlap (by C definition)
	 * Lots of nops here:
	 *	- Separate loads from stores
	 *	- Keep it to 1 branch/quadpack so the branch predictor
	 *	  can train.
	 */
$a_loop:
	stq_u	t0, 0(a0)	# L :
	addq	a0, 8, a0	# E :
	nop
	subq	a2, 1, a2	# E :

	EX( ldq_u t0, 0(a1) )	# L :
	addq	a1, 8, a1	# E :
	cmpbge	zero, t0, t8	# E : Stall 2 cycles on t0
	beq	a2, $a_eoc      # U :

	beq	t8, $a_loop	# U :
	nop
	nop
	nop

	/* Take care of the final (partial) word store.  At this point
	 * the end-of-count bit is set in t8 iff it applies.
	 *
	 * On entry to this basic block we have:
	 * t0 == the source word containing the null
	 * t8 == the cmpbge mask that found it.
	 */
$a_eos:
	negq	t8, t12		# E : find low bit set
	and	t8, t12, t12	# E : 

	/* We're doing a partial word store and so need to combine
	   our source and original destination words.  */
	ldq_u	t1, 0(a0)	# L :
	subq	t12, 1, t6	# E :

	or	t12, t6, t8	# E :
	zapnot	t0, t8, t0	# U : clear src bytes > null
	zap	t1, t8, t1	# U : clear dst bytes <= null
	or	t0, t1, t0	# E :

	stq_u	t0, 0(a0)	# L :
	br	$finish_up	# L0 :
	nop
	nop

	/* Add the end-of-count bit to the eos detection bitmask.  */
	.align 4
$a_eoc:
	or	t10, t8, t8
	br	$a_eos
	nop
	nop


/* The source and destination are not co-aligned.  Align the destination
   and cope.  We have to be very careful about not reading too much and
   causing a SEGV.  */

	.align 4
$u_head:
	/* We know just enough now to be able to assemble the first
	   full source word.  We can still find a zero at the end of it
	   that prevents us from outputting the whole thing.

	   On entry to this basic block:
	   t0 == the first dest word, unmasked
	   t1 == the shifted low bits of the first source word
	   t6 == bytemask that is -1 in dest word bytes */

	EX( ldq_u t2, 8(a1) )	# L : load second src word
	addq	a1, 8, a1	# E :
	mskql	t0, a0, t0	# U : mask trailing garbage in dst
	extqh	t2, a1, t4	# U :

	or	t1, t4, t1	# E : first aligned src word complete
	mskqh	t1, a0, t1	# U : mask leading garbage in src
	or	t0, t1, t0	# E : first output word complete
	or	t0, t6, t6	# E : mask original data for zero test

	cmpbge	zero, t6, t8	# E :
	beq	a2, $u_eocfin	# U :
	bne	t8, $u_final	# U : bad news - 2nd branch in a quad
	lda	t6, -1		# E : mask out the bits we have

	mskql	t6, a1, t6	# U :   already seen
	stq_u	t0, 0(a0)	# L : store first output word
	or      t6, t2, t2	# E :
	cmpbge	zero, t2, t8	# E : find nulls in second partial

	addq	a0, 8, a0		# E :
	subq	a2, 1, a2		# E :
	bne	t8, $u_late_head_exit	# U :
	nop

	/* Finally, we've got all the stupid leading edge cases taken care
	   of and we can set up to enter the main loop.  */

	extql	t2, a1, t1	# U : position hi-bits of lo word
	EX( ldq_u t2, 8(a1) )	# L : read next high-order source word
	addq	a1, 8, a1	# E :
	cmpbge	zero, t2, t8	# E :

	beq	a2, $u_eoc	# U :
	bne	t8, $u_eos	# U :
	nop
	nop

	/* Unaligned copy main loop.  In order to avoid reading too much,
	   the loop is structured to detect zeros in aligned source words.
	   This has, unfortunately, effectively pulled half of a loop
	   iteration out into the head and half into the tail, but it does
	   prevent nastiness from accumulating in the very thing we want
	   to run as fast as possible.

	   On entry to this basic block:
	   t1 == the shifted high-order bits from the previous source word
	   t2 == the unshifted current source word

	   We further know that t2 does not contain a null terminator.  */

	/*
	 * Extra nops here:
	 *	separate load quads from store quads
	 *	only one branch/quad to permit predictor training
	 */

	.align 4
$u_loop:
	extqh	t2, a1, t0	# U : extract high bits for current word
	addq	a1, 8, a1	# E :
	extql	t2, a1, t3	# U : extract low bits for next time
	addq	a0, 8, a0	# E :

	or	t0, t1, t0	# E : current dst word now complete
	EX( ldq_u t2, 0(a1) )	# L : load high word for next time
	subq	a2, 1, a2	# E :
	nop

	stq_u	t0, -8(a0)	# L : save the current word
	mov	t3, t1		# E :
	cmpbge	zero, t2, t8	# E : test new word for eos
	beq	a2, $u_eoc	# U :

	beq	t8, $u_loop	# U :
	nop
	nop
	nop

	/* We've found a zero somewhere in the source word we just read.
	   If it resides in the lower half, we have one (probably partial)
	   word to write out, and if it resides in the upper half, we
	   have one full and one partial word left to write out.

	   On entry to this basic block:
	   t1 == the shifted high-order bits from the previous source word
	   t2 == the unshifted current source word.  */
	.align 4
$u_eos:
	extqh	t2, a1, t0	# U :
	or	t0, t1, t0	# E : first (partial) source word complete
	cmpbge	zero, t0, t8	# E : is the null in this first bit?
	nop

	bne	t8, $u_final	# U :
	stq_u	t0, 0(a0)	# L : the null was in the high-order bits
	addq	a0, 8, a0	# E :
	subq	a2, 1, a2	# E :

	.align 4
$u_late_head_exit:
	extql	t2, a1, t0	# U :
	cmpbge	zero, t0, t8	# E :
	or	t8, t10, t6	# E :
	cmoveq	a2, t6, t8	# E :

	/* Take care of a final (probably partial) result word.
	   On entry to this basic block:
	   t0 == assembled source word
	   t8 == cmpbge mask that found the null.  */
	.align 4
$u_final:
	negq	t8, t6		# E : isolate low bit set
	and	t6, t8, t12	# E :
	ldq_u	t1, 0(a0)	# L :
	subq	t12, 1, t6	# E :

	or	t6, t12, t8	# E :
	zapnot	t0, t8, t0	# U : kill source bytes > null
	zap	t1, t8, t1	# U : kill dest bytes <= null
	or	t0, t1, t0	# E :

	stq_u	t0, 0(a0)	# E :
	br	$finish_up	# U :
	nop
	nop

	.align 4
$u_eoc:				# end-of-count
	extqh	t2, a1, t0	# U :
	or	t0, t1, t0	# E :
	cmpbge	zero, t0, t8	# E :
	nop

	.align 4
$u_eocfin:			# end-of-count, final word
	or	t10, t8, t8	# E :
	br	$u_final	# U :
	nop
	nop

	/* Unaligned copy entry point.  */
	.align 4
$unaligned:

	srl	a3, 3, a2	# U : a2 = loop counter = (count - 1)/8
	and	a0, 7, t4	# E : find dest misalignment
	and	a1, 7, t5	# E : find src misalignment
	mov	zero, t0	# E :

	/* Conditionally load the first destination word and a bytemask
	   with 0xff indicating that the destination byte is sacrosanct.  */

	mov	zero, t6	# E :
	beq	t4, 1f		# U :
	ldq_u	t0, 0(a0)	# L :
	lda	t6, -1		# E :

	mskql	t6, a0, t6	# E :
	nop
	nop
	nop

	.align 4
1:
	subq	a1, t4, a1	# E : sub dest misalignment from src addr
	/* If source misalignment is larger than dest misalignment, we need
	   extra startup checks to avoid SEGV.  */
	cmplt	t4, t5, t12	# E :
	extql	t1, a1, t1	# U : shift src into place
	lda	t2, -1		# E : for creating masks later

	beq	t12, $u_head	# U :
	mskqh	t2, t5, t2	# U : begin src byte validity mask
	cmpbge	zero, t1, t8	# E : is there a zero?
	nop

	extql	t2, a1, t2	# U :
	or	t8, t10, t5	# E : test for end-of-count too
	cmpbge	zero, t2, t3	# E :
	cmoveq	a2, t5, t8	# E : Latency=2, extra map slot

	nop			# E : goes with cmov
	andnot	t8, t3, t8	# E :
	beq	t8, $u_head	# U :
	nop

	/* At this point we've found a zero in the first partial word of
	   the source.  We need to isolate the valid source data and mask
	   it into the original destination data.  (Incidentally, we know
	   that we'll need at least one byte of that original dest word.) */

	ldq_u	t0, 0(a0)	# L :
	negq	t8, t6		# E : build bitmask of bytes <= zero
	mskqh	t1, t4, t1	# U :
	and	t6, t8, t12	# E :

	subq	t12, 1, t6	# E :
	or	t6, t12, t8	# E :
	zapnot	t2, t8, t2	# U : prepare source word; mirror changes
	zapnot	t1, t8, t1	# U : to source validity mask

	andnot	t0, t2, t0	# E : zero place for source to reside
	or	t0, t1, t0	# E : and put it there
	stq_u	t0, 0(a0)	# L :
	nop

	.align 4
$finish_up:
	zapnot	t0, t12, t4	# U : was last byte written null?
	and	t12, 0xf0, t3	# E : binary search for the address of the
	cmovne	t4, 1, t4	# E : Latency=2, extra map slot
	nop			# E : with cmovne

	and	t12, 0xcc, t2	# E : last byte written
	and	t12, 0xaa, t1	# E :
	cmovne	t3, 4, t3	# E : Latency=2, extra map slot
	nop			# E : with cmovne

	bic	a0, 7, t0
	cmovne	t2, 2, t2	# E : Latency=2, extra map slot
	nop			# E : with cmovne
	nop

	cmovne	t1, 1, t1	# E : Latency=2, extra map slot
	nop			# E : with cmovne
	addq	t0, t3, t0	# E :
	addq	t1, t2, t1	# E :

	addq	t0, t1, t0	# E :
	addq	t0, t4, t0	# add one if we filled the buffer
	subq	t0, v0, v0	# find string length
	ret			# L0 :

	.align 4
$zerolength:
	nop
	nop
	nop
	clr	v0

$exception:
	nop
	nop
	nop
	ret

	.end __strncpy_from_user
