/* SPDX-License-Identifier: GPL-2.0 */
/*
 * arch/alpha/lib/ev6-memcpy.S
 * 21264 version by Rick Gorton <rick.gorton@alpha-processor.com>
 *
 * Reasonably optimized memcpy() routine for the Alpha 21264
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
 *
 * Temp usage notes:
 *	$1,$2,		- scratch
 */
#include <asm/export.h>
	.set noreorder
	.set noat

	.align	4
	.globl memcpy
	.ent memcpy
memcpy:
	.frame $30,0,$26,0
	.prologue 0

	mov	$16, $0			# E : copy dest to return
	ble	$18, $nomoredata	# U : done with the copy?
	xor	$16, $17, $1		# E : are source and dest alignments the same?
	and	$1, 7, $1		# E : are they the same mod 8?

	bne	$1, $misaligned		# U : Nope - gotta do this the slow way
	/* source and dest are same mod 8 address */
	and	$16, 7, $1		# E : Are both 0mod8?
	beq	$1, $both_0mod8		# U : Yes
	nop				# E :

	/*
	 * source and dest are same misalignment.  move a byte at a time
	 * until a 0mod8 alignment for both is reached.
	 * At least one byte more to move
	 */

$head_align:
	ldbu	$1, 0($17)		# L : grab a byte
	subq	$18, 1, $18		# E : count--
	addq	$17, 1, $17		# E : src++
	stb	$1, 0($16)		# L :
	addq	$16, 1, $16		# E : dest++
	and	$16, 7, $1		# E : Are we at 0mod8 yet?
	ble	$18, $nomoredata	# U : done with the copy?
	bne	$1, $head_align		# U :

$both_0mod8:
	cmple	$18, 127, $1		# E : Can we unroll the loop?
	bne	$1, $no_unroll		# U :
	and	$16, 63, $1		# E : get mod64 alignment
	beq	$1, $do_unroll		# U : no single quads to fiddle

$single_head_quad:
	ldq	$1, 0($17)		# L : get 8 bytes
	subq	$18, 8, $18		# E : count -= 8
	addq	$17, 8, $17		# E : src += 8
	nop				# E :

	stq	$1, 0($16)		# L : store
	addq	$16, 8, $16		# E : dest += 8
	and	$16, 63, $1		# E : get mod64 alignment
	bne	$1, $single_head_quad	# U : still not fully aligned

$do_unroll:
	addq	$16, 64, $7		# E : Initial (+1 trip) wh64 address
	cmple	$18, 127, $1		# E : Can we go through the unrolled loop?
	bne	$1, $tail_quads		# U : Nope
	nop				# E : 

$unroll_body:
	wh64	($7)			# L1 : memory subsystem hint: 64 bytes at
					# ($7) are about to be over-written
	ldq	$6, 0($17)		# L0 : bytes 0..7
	nop				# E :
	nop				# E :

	ldq	$4, 8($17)		# L : bytes 8..15
	ldq	$5, 16($17)		# L : bytes 16..23
	addq	$7, 64, $7		# E : Update next wh64 address
	nop				# E :

	ldq	$3, 24($17)		# L : bytes 24..31
	addq	$16, 64, $1		# E : fallback value for wh64
	nop				# E :
	nop				# E :

	addq	$17, 32, $17		# E : src += 32 bytes
	stq	$6, 0($16)		# L : bytes 0..7
	nop				# E :
	nop				# E :

	stq	$4, 8($16)		# L : bytes 8..15
	stq	$5, 16($16)		# L : bytes 16..23
	subq	$18, 192, $2		# E : At least two more trips to go?
	nop				# E :

	stq	$3, 24($16)		# L : bytes 24..31
	addq	$16, 32, $16		# E : dest += 32 bytes
	nop				# E :
	nop				# E :

	ldq	$6, 0($17)		# L : bytes 0..7
	ldq	$4, 8($17)		# L : bytes 8..15
	cmovlt	$2, $1, $7		# E : Latency 2, extra map slot - Use
					# fallback wh64 address if < 2 more trips
	nop				# E :

	ldq	$5, 16($17)		# L : bytes 16..23
	ldq	$3, 24($17)		# L : bytes 24..31
	addq	$16, 32, $16		# E : dest += 32
	subq	$18, 64, $18		# E : count -= 64

	addq	$17, 32, $17		# E : src += 32
	stq	$6, -32($16)		# L : bytes 0..7
	stq	$4, -24($16)		# L : bytes 8..15
	cmple	$18, 63, $1		# E : At least one more trip?

	stq	$5, -16($16)		# L : bytes 16..23
	stq	$3, -8($16)		# L : bytes 24..31
	nop				# E :
	beq	$1, $unroll_body

$tail_quads:
$no_unroll:
	.align 4
	subq	$18, 8, $18		# E : At least a quad left?
	blt	$18, $less_than_8	# U : Nope
	nop				# E :
	nop				# E :

$move_a_quad:
	ldq	$1, 0($17)		# L : fetch 8
	subq	$18, 8, $18		# E : count -= 8
	addq	$17, 8, $17		# E : src += 8
	nop				# E :

	stq	$1, 0($16)		# L : store 8
	addq	$16, 8, $16		# E : dest += 8
	bge	$18, $move_a_quad	# U :
	nop				# E :

$less_than_8:
	.align 4
	addq	$18, 8, $18		# E : add back for trailing bytes
	ble	$18, $nomoredata	# U : All-done
	nop				# E :
	nop				# E :

	/* Trailing bytes */
$tail_bytes:
	subq	$18, 1, $18		# E : count--
	ldbu	$1, 0($17)		# L : fetch a byte
	addq	$17, 1, $17		# E : src++
	nop				# E :

	stb	$1, 0($16)		# L : store a byte
	addq	$16, 1, $16		# E : dest++
	bgt	$18, $tail_bytes	# U : more to be done?
	nop				# E :

	/* branching to exit takes 3 extra cycles, so replicate exit here */
	ret	$31, ($26), 1		# L0 :
	nop				# E :
	nop				# E :
	nop				# E :

$misaligned:
	mov	$0, $4			# E : dest temp
	and	$0, 7, $1		# E : dest alignment mod8
	beq	$1, $dest_0mod8		# U : life doesnt totally suck
	nop

$aligndest:
	ble	$18, $nomoredata	# U :
	ldbu	$1, 0($17)		# L : fetch a byte
	subq	$18, 1, $18		# E : count--
	addq	$17, 1, $17		# E : src++

	stb	$1, 0($4)		# L : store it
	addq	$4, 1, $4		# E : dest++
	and	$4, 7, $1		# E : dest 0mod8 yet?
	bne	$1, $aligndest		# U : go until we are aligned.

	/* Source has unknown alignment, but dest is known to be 0mod8 */
$dest_0mod8:
	subq	$18, 8, $18		# E : At least a quad left?
	blt	$18, $misalign_tail	# U : Nope
	ldq_u	$3, 0($17)		# L : seed (rotating load) of 8 bytes
	nop				# E :

$mis_quad:
	ldq_u	$16, 8($17)		# L : Fetch next 8
	extql	$3, $17, $3		# U : masking
	extqh	$16, $17, $1		# U : masking
	bis	$3, $1, $1		# E : merged bytes to store

	subq	$18, 8, $18		# E : count -= 8
	addq	$17, 8, $17		# E : src += 8
	stq	$1, 0($4)		# L : store 8 (aligned)
	mov	$16, $3			# E : "rotate" source data

	addq	$4, 8, $4		# E : dest += 8
	bge	$18, $mis_quad		# U : More quads to move
	nop
	nop

$misalign_tail:
	addq	$18, 8, $18		# E : account for tail stuff
	ble	$18, $nomoredata	# U :
	nop
	nop

$misalign_byte:
	ldbu	$1, 0($17)		# L : fetch 1
	subq	$18, 1, $18		# E : count--
	addq	$17, 1, $17		# E : src++
	nop				# E :

	stb	$1, 0($4)		# L : store
	addq	$4, 1, $4		# E : dest++
	bgt	$18, $misalign_byte	# U : more to go?
	nop


$nomoredata:
	ret	$31, ($26), 1		# L0 :
	nop				# E :
	nop				# E :
	nop				# E :

	.end memcpy
	EXPORT_SYMBOL(memcpy)

/* For backwards module compatibility.  */
__memcpy = memcpy
.globl __memcpy
