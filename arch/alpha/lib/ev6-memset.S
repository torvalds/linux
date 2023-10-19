/* SPDX-License-Identifier: GPL-2.0 */
/*
 * arch/alpha/lib/ev6-memset.S
 *
 * This is an efficient (and relatively small) implementation of the C library
 * "memset()" function for the 21264 implementation of Alpha.
 *
 * 21264 version  contributed by Rick Gorton <rick.gorton@alpha-processor.com>
 *
 * Much of the information about 21264 scheduling/coding comes from:
 *	Compiler Writer's Guide for the Alpha 21264
 *	abbreviated as 'CWG' in other comments here
 *	ftp.digital.com/pub/Digital/info/semiconductor/literature/dsc-library.html
 * Scheduling notation:
 *	E	- either cluster
 *	U	- upper subcluster; U0 - subcluster U0; U1 - subcluster U1
 *	L	- lower subcluster; L0 - subcluster L0; L1 - subcluster L1
 * The algorithm for the leading and trailing quadwords remains the same,
 * however the loop has been unrolled to enable better memory throughput,
 * and the code has been replicated for each of the entry points: __memset
 * and __memset16 to permit better scheduling to eliminate the stalling
 * encountered during the mask replication.
 * A future enhancement might be to put in a byte store loop for really
 * small (say < 32 bytes) memset()s.  Whether or not that change would be
 * a win in the kernel would depend upon the contextual usage.
 * WARNING: Maintaining this is going to be more work than the above version,
 * as fixes will need to be made in multiple places.  The performance gain
 * is worth it.
 */
#include <asm/export.h>
	.set noat
	.set noreorder
.text
	.globl memset
	.globl __memset
	.globl ___memset
	.globl __memset16
	.globl __constant_c_memset

	.ent ___memset
.align 5
___memset:
	.frame $30,0,$26,0
	.prologue 0

	/*
	 * Serious stalling happens.  The only way to mitigate this is to
	 * undertake a major re-write to interleave the constant materialization
	 * with other parts of the fall-through code.  This is important, even
	 * though it makes maintenance tougher.
	 * Do this later.
	 */
	and $17,255,$1		# E : 00000000000000ch
	insbl $17,1,$2		# U : 000000000000ch00
	bis $16,$16,$0		# E : return value
	ble $18,end_b		# U : zero length requested?

	addq $18,$16,$6		# E : max address to write to
	bis	$1,$2,$17	# E : 000000000000chch
	insbl	$1,2,$3		# U : 0000000000ch0000
	insbl	$1,3,$4		# U : 00000000ch000000

	or	$3,$4,$3	# E : 00000000chch0000
	inswl	$17,4,$5	# U : 0000chch00000000
	xor	$16,$6,$1	# E : will complete write be within one quadword?
	inswl	$17,6,$2	# U : chch000000000000

	or	$17,$3,$17	# E : 00000000chchchch
	or	$2,$5,$2	# E : chchchch00000000
	bic	$1,7,$1		# E : fit within a single quadword?
	and	$16,7,$3	# E : Target addr misalignment

	or	$17,$2,$17	# E : chchchchchchchch
	beq	$1,within_quad_b # U :
	nop			# E :
	beq	$3,aligned_b	# U : target is 0mod8

	/*
	 * Target address is misaligned, and won't fit within a quadword
	 */
	ldq_u $4,0($16)		# L : Fetch first partial
	bis $16,$16,$5		# E : Save the address
	insql $17,$16,$2	# U : Insert new bytes
	subq $3,8,$3		# E : Invert (for addressing uses)

	addq $18,$3,$18		# E : $18 is new count ($3 is negative)
	mskql $4,$16,$4		# U : clear relevant parts of the quad
	subq $16,$3,$16		# E : $16 is new aligned destination
	bis $2,$4,$1		# E : Final bytes

	nop
	stq_u $1,0($5)		# L : Store result
	nop
	nop

.align 4
aligned_b:
	/*
	 * We are now guaranteed to be quad aligned, with at least
	 * one partial quad to write.
	 */

	sra $18,3,$3		# U : Number of remaining quads to write
	and $18,7,$18		# E : Number of trailing bytes to write
	bis $16,$16,$5		# E : Save dest address
	beq $3,no_quad_b	# U : tail stuff only

	/*
	 * it's worth the effort to unroll this and use wh64 if possible
	 * Lifted a bunch of code from clear_user.S
	 * At this point, entry values are:
	 * $16	Current destination address
	 * $5	A copy of $16
	 * $6	The max quadword address to write to
	 * $18	Number trailer bytes
	 * $3	Number quads to write
	 */

	and	$16, 0x3f, $2	# E : Forward work (only useful for unrolled loop)
	subq	$3, 16, $4	# E : Only try to unroll if > 128 bytes
	subq	$2, 0x40, $1	# E : bias counter (aligning stuff 0mod64)
	blt	$4, loop_b	# U :

	/*
	 * We know we've got at least 16 quads, minimum of one trip
	 * through unrolled loop.  Do a quad at a time to get us 0mod64
	 * aligned.
	 */

	nop			# E :
	nop			# E :
	nop			# E :
	beq	$1, $bigalign_b	# U :

$alignmod64_b:
	stq	$17, 0($5)	# L :
	subq	$3, 1, $3	# E : For consistency later
	addq	$1, 8, $1	# E : Increment towards zero for alignment
	addq	$5, 8, $4	# E : Initial wh64 address (filler instruction)

	nop
	nop
	addq	$5, 8, $5	# E : Inc address
	blt	$1, $alignmod64_b # U :

$bigalign_b:
	/*
	 * $3 - number quads left to go
	 * $5 - target address (aligned 0mod64)
	 * $17 - mask of stuff to store
	 * Scratch registers available: $7, $2, $4, $1
	 * we know that we'll be taking a minimum of one trip through
 	 * CWG Section 3.7.6: do not expect a sustained store rate of > 1/cycle
	 * Assumes the wh64 needs to be for 2 trips through the loop in the future
	 * The wh64 is issued on for the starting destination address for trip +2
	 * through the loop, and if there are less than two trips left, the target
	 * address will be for the current trip.
	 */

$do_wh64_b:
	wh64	($4)		# L1 : memory subsystem write hint
	subq	$3, 24, $2	# E : For determining future wh64 addresses
	stq	$17, 0($5)	# L :
	nop			# E :

	addq	$5, 128, $4	# E : speculative target of next wh64
	stq	$17, 8($5)	# L :
	stq	$17, 16($5)	# L :
	addq	$5, 64, $7	# E : Fallback address for wh64 (== next trip addr)

	stq	$17, 24($5)	# L :
	stq	$17, 32($5)	# L :
	cmovlt	$2, $7, $4	# E : Latency 2, extra mapping cycle
	nop

	stq	$17, 40($5)	# L :
	stq	$17, 48($5)	# L :
	subq	$3, 16, $2	# E : Repeat the loop at least once more?
	nop

	stq	$17, 56($5)	# L :
	addq	$5, 64, $5	# E :
	subq	$3, 8, $3	# E :
	bge	$2, $do_wh64_b	# U :

	nop
	nop
	nop
	beq	$3, no_quad_b	# U : Might have finished already

.align 4
	/*
	 * Simple loop for trailing quadwords, or for small amounts
	 * of data (where we can't use an unrolled loop and wh64)
	 */
loop_b:
	stq $17,0($5)		# L :
	subq $3,1,$3		# E : Decrement number quads left
	addq $5,8,$5		# E : Inc address
	bne $3,loop_b		# U : more?

no_quad_b:
	/*
	 * Write 0..7 trailing bytes.
	 */
	nop			# E :
	beq $18,end_b		# U : All done?
	ldq $7,0($5)		# L :
	mskqh $7,$6,$2		# U : Mask final quad

	insqh $17,$6,$4		# U : New bits
	bis $2,$4,$1		# E : Put it all together
	stq $1,0($5)		# L : And back to memory
	ret $31,($26),1		# L0 :

within_quad_b:
	ldq_u $1,0($16)		# L :
	insql $17,$16,$2	# U : New bits
	mskql $1,$16,$4		# U : Clear old
	bis $2,$4,$2		# E : New result

	mskql $2,$6,$4		# U :
	mskqh $1,$6,$2		# U :
	bis $2,$4,$1		# E :
	stq_u $1,0($16)		# L :

end_b:
	nop
	nop
	nop
	ret $31,($26),1		# L0 :
	.end ___memset
	EXPORT_SYMBOL(___memset)

	/*
	 * This is the original body of code, prior to replication and
	 * rescheduling.  Leave it here, as there may be calls to this
	 * entry point.
	 */
.align 4
	.ent __constant_c_memset
__constant_c_memset:
	.frame $30,0,$26,0
	.prologue 0

	addq $18,$16,$6		# E : max address to write to
	bis $16,$16,$0		# E : return value
	xor $16,$6,$1		# E : will complete write be within one quadword?
	ble $18,end		# U : zero length requested?

	bic $1,7,$1		# E : fit within a single quadword
	beq $1,within_one_quad	# U :
	and $16,7,$3		# E : Target addr misalignment
	beq $3,aligned		# U : target is 0mod8

	/*
	 * Target address is misaligned, and won't fit within a quadword
	 */
	ldq_u $4,0($16)		# L : Fetch first partial
	bis $16,$16,$5		# E : Save the address
	insql $17,$16,$2	# U : Insert new bytes
	subq $3,8,$3		# E : Invert (for addressing uses)

	addq $18,$3,$18		# E : $18 is new count ($3 is negative)
	mskql $4,$16,$4		# U : clear relevant parts of the quad
	subq $16,$3,$16		# E : $16 is new aligned destination
	bis $2,$4,$1		# E : Final bytes

	nop
	stq_u $1,0($5)		# L : Store result
	nop
	nop

.align 4
aligned:
	/*
	 * We are now guaranteed to be quad aligned, with at least
	 * one partial quad to write.
	 */

	sra $18,3,$3		# U : Number of remaining quads to write
	and $18,7,$18		# E : Number of trailing bytes to write
	bis $16,$16,$5		# E : Save dest address
	beq $3,no_quad		# U : tail stuff only

	/*
	 * it's worth the effort to unroll this and use wh64 if possible
	 * Lifted a bunch of code from clear_user.S
	 * At this point, entry values are:
	 * $16	Current destination address
	 * $5	A copy of $16
	 * $6	The max quadword address to write to
	 * $18	Number trailer bytes
	 * $3	Number quads to write
	 */

	and	$16, 0x3f, $2	# E : Forward work (only useful for unrolled loop)
	subq	$3, 16, $4	# E : Only try to unroll if > 128 bytes
	subq	$2, 0x40, $1	# E : bias counter (aligning stuff 0mod64)
	blt	$4, loop	# U :

	/*
	 * We know we've got at least 16 quads, minimum of one trip
	 * through unrolled loop.  Do a quad at a time to get us 0mod64
	 * aligned.
	 */

	nop			# E :
	nop			# E :
	nop			# E :
	beq	$1, $bigalign	# U :

$alignmod64:
	stq	$17, 0($5)	# L :
	subq	$3, 1, $3	# E : For consistency later
	addq	$1, 8, $1	# E : Increment towards zero for alignment
	addq	$5, 8, $4	# E : Initial wh64 address (filler instruction)

	nop
	nop
	addq	$5, 8, $5	# E : Inc address
	blt	$1, $alignmod64	# U :

$bigalign:
	/*
	 * $3 - number quads left to go
	 * $5 - target address (aligned 0mod64)
	 * $17 - mask of stuff to store
	 * Scratch registers available: $7, $2, $4, $1
	 * we know that we'll be taking a minimum of one trip through
 	 * CWG Section 3.7.6: do not expect a sustained store rate of > 1/cycle
	 * Assumes the wh64 needs to be for 2 trips through the loop in the future
	 * The wh64 is issued on for the starting destination address for trip +2
	 * through the loop, and if there are less than two trips left, the target
	 * address will be for the current trip.
	 */

$do_wh64:
	wh64	($4)		# L1 : memory subsystem write hint
	subq	$3, 24, $2	# E : For determining future wh64 addresses
	stq	$17, 0($5)	# L :
	nop			# E :

	addq	$5, 128, $4	# E : speculative target of next wh64
	stq	$17, 8($5)	# L :
	stq	$17, 16($5)	# L :
	addq	$5, 64, $7	# E : Fallback address for wh64 (== next trip addr)

	stq	$17, 24($5)	# L :
	stq	$17, 32($5)	# L :
	cmovlt	$2, $7, $4	# E : Latency 2, extra mapping cycle
	nop

	stq	$17, 40($5)	# L :
	stq	$17, 48($5)	# L :
	subq	$3, 16, $2	# E : Repeat the loop at least once more?
	nop

	stq	$17, 56($5)	# L :
	addq	$5, 64, $5	# E :
	subq	$3, 8, $3	# E :
	bge	$2, $do_wh64	# U :

	nop
	nop
	nop
	beq	$3, no_quad	# U : Might have finished already

.align 4
	/*
	 * Simple loop for trailing quadwords, or for small amounts
	 * of data (where we can't use an unrolled loop and wh64)
	 */
loop:
	stq $17,0($5)		# L :
	subq $3,1,$3		# E : Decrement number quads left
	addq $5,8,$5		# E : Inc address
	bne $3,loop		# U : more?

no_quad:
	/*
	 * Write 0..7 trailing bytes.
	 */
	nop			# E :
	beq $18,end		# U : All done?
	ldq $7,0($5)		# L :
	mskqh $7,$6,$2		# U : Mask final quad

	insqh $17,$6,$4		# U : New bits
	bis $2,$4,$1		# E : Put it all together
	stq $1,0($5)		# L : And back to memory
	ret $31,($26),1		# L0 :

within_one_quad:
	ldq_u $1,0($16)		# L :
	insql $17,$16,$2	# U : New bits
	mskql $1,$16,$4		# U : Clear old
	bis $2,$4,$2		# E : New result

	mskql $2,$6,$4		# U :
	mskqh $1,$6,$2		# U :
	bis $2,$4,$1		# E :
	stq_u $1,0($16)		# L :

end:
	nop
	nop
	nop
	ret $31,($26),1		# L0 :
	.end __constant_c_memset
	EXPORT_SYMBOL(__constant_c_memset)

	/*
	 * This is a replicant of the __constant_c_memset code, rescheduled
	 * to mask stalls.  Note that entry point names also had to change
	 */
	.align 5
	.ent __memset16

__memset16:
	.frame $30,0,$26,0
	.prologue 0

	inswl $17,0,$5		# U : 000000000000c1c2
	inswl $17,2,$2		# U : 00000000c1c20000
	bis $16,$16,$0		# E : return value
	addq	$18,$16,$6	# E : max address to write to

	ble $18, end_w		# U : zero length requested?
	inswl	$17,4,$3	# U : 0000c1c200000000
	inswl	$17,6,$4	# U : c1c2000000000000
	xor	$16,$6,$1	# E : will complete write be within one quadword?

	or	$2,$5,$2	# E : 00000000c1c2c1c2
	or	$3,$4,$17	# E : c1c2c1c200000000
	bic	$1,7,$1		# E : fit within a single quadword
	and	$16,7,$3	# E : Target addr misalignment

	or	$17,$2,$17	# E : c1c2c1c2c1c2c1c2
	beq $1,within_quad_w	# U :
	nop
	beq $3,aligned_w	# U : target is 0mod8

	/*
	 * Target address is misaligned, and won't fit within a quadword
	 */
	ldq_u $4,0($16)		# L : Fetch first partial
	bis $16,$16,$5		# E : Save the address
	insql $17,$16,$2	# U : Insert new bytes
	subq $3,8,$3		# E : Invert (for addressing uses)

	addq $18,$3,$18		# E : $18 is new count ($3 is negative)
	mskql $4,$16,$4		# U : clear relevant parts of the quad
	subq $16,$3,$16		# E : $16 is new aligned destination
	bis $2,$4,$1		# E : Final bytes

	nop
	stq_u $1,0($5)		# L : Store result
	nop
	nop

.align 4
aligned_w:
	/*
	 * We are now guaranteed to be quad aligned, with at least
	 * one partial quad to write.
	 */

	sra $18,3,$3		# U : Number of remaining quads to write
	and $18,7,$18		# E : Number of trailing bytes to write
	bis $16,$16,$5		# E : Save dest address
	beq $3,no_quad_w	# U : tail stuff only

	/*
	 * it's worth the effort to unroll this and use wh64 if possible
	 * Lifted a bunch of code from clear_user.S
	 * At this point, entry values are:
	 * $16	Current destination address
	 * $5	A copy of $16
	 * $6	The max quadword address to write to
	 * $18	Number trailer bytes
	 * $3	Number quads to write
	 */

	and	$16, 0x3f, $2	# E : Forward work (only useful for unrolled loop)
	subq	$3, 16, $4	# E : Only try to unroll if > 128 bytes
	subq	$2, 0x40, $1	# E : bias counter (aligning stuff 0mod64)
	blt	$4, loop_w	# U :

	/*
	 * We know we've got at least 16 quads, minimum of one trip
	 * through unrolled loop.  Do a quad at a time to get us 0mod64
	 * aligned.
	 */

	nop			# E :
	nop			# E :
	nop			# E :
	beq	$1, $bigalign_w	# U :

$alignmod64_w:
	stq	$17, 0($5)	# L :
	subq	$3, 1, $3	# E : For consistency later
	addq	$1, 8, $1	# E : Increment towards zero for alignment
	addq	$5, 8, $4	# E : Initial wh64 address (filler instruction)

	nop
	nop
	addq	$5, 8, $5	# E : Inc address
	blt	$1, $alignmod64_w	# U :

$bigalign_w:
	/*
	 * $3 - number quads left to go
	 * $5 - target address (aligned 0mod64)
	 * $17 - mask of stuff to store
	 * Scratch registers available: $7, $2, $4, $1
	 * we know that we'll be taking a minimum of one trip through
 	 * CWG Section 3.7.6: do not expect a sustained store rate of > 1/cycle
	 * Assumes the wh64 needs to be for 2 trips through the loop in the future
	 * The wh64 is issued on for the starting destination address for trip +2
	 * through the loop, and if there are less than two trips left, the target
	 * address will be for the current trip.
	 */

$do_wh64_w:
	wh64	($4)		# L1 : memory subsystem write hint
	subq	$3, 24, $2	# E : For determining future wh64 addresses
	stq	$17, 0($5)	# L :
	nop			# E :

	addq	$5, 128, $4	# E : speculative target of next wh64
	stq	$17, 8($5)	# L :
	stq	$17, 16($5)	# L :
	addq	$5, 64, $7	# E : Fallback address for wh64 (== next trip addr)

	stq	$17, 24($5)	# L :
	stq	$17, 32($5)	# L :
	cmovlt	$2, $7, $4	# E : Latency 2, extra mapping cycle
	nop

	stq	$17, 40($5)	# L :
	stq	$17, 48($5)	# L :
	subq	$3, 16, $2	# E : Repeat the loop at least once more?
	nop

	stq	$17, 56($5)	# L :
	addq	$5, 64, $5	# E :
	subq	$3, 8, $3	# E :
	bge	$2, $do_wh64_w	# U :

	nop
	nop
	nop
	beq	$3, no_quad_w	# U : Might have finished already

.align 4
	/*
	 * Simple loop for trailing quadwords, or for small amounts
	 * of data (where we can't use an unrolled loop and wh64)
	 */
loop_w:
	stq $17,0($5)		# L :
	subq $3,1,$3		# E : Decrement number quads left
	addq $5,8,$5		# E : Inc address
	bne $3,loop_w		# U : more?

no_quad_w:
	/*
	 * Write 0..7 trailing bytes.
	 */
	nop			# E :
	beq $18,end_w		# U : All done?
	ldq $7,0($5)		# L :
	mskqh $7,$6,$2		# U : Mask final quad

	insqh $17,$6,$4		# U : New bits
	bis $2,$4,$1		# E : Put it all together
	stq $1,0($5)		# L : And back to memory
	ret $31,($26),1		# L0 :

within_quad_w:
	ldq_u $1,0($16)		# L :
	insql $17,$16,$2	# U : New bits
	mskql $1,$16,$4		# U : Clear old
	bis $2,$4,$2		# E : New result

	mskql $2,$6,$4		# U :
	mskqh $1,$6,$2		# U :
	bis $2,$4,$1		# E :
	stq_u $1,0($16)		# L :

end_w:
	nop
	nop
	nop
	ret $31,($26),1		# L0 :

	.end __memset16
	EXPORT_SYMBOL(__memset16)

memset = ___memset
__memset = ___memset
	EXPORT_SYMBOL(memset)
	EXPORT_SYMBOL(__memset)
