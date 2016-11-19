/*
 * arch/alpha/lib/ev6-memchr.S
 *
 * 21264 version contributed by Rick Gorton <rick.gorton@alpha-processor.com>
 *
 * Finds characters in a memory area.  Optimized for the Alpha:
 *
 *    - memory accessed as aligned quadwords only
 *    - uses cmpbge to compare 8 bytes in parallel
 *    - does binary search to find 0 byte in last
 *      quadword (HAKMEM needed 12 instructions to
 *      do this instead of the 9 instructions that
 *      binary search needs).
 *
 * For correctness consider that:
 *
 *    - only minimum number of quadwords may be accessed
 *    - the third argument is an unsigned long
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
        .set noreorder
        .set noat

	.align	4
	.globl memchr
	.ent memchr
memchr:
	.frame $30,0,$26,0
	.prologue 0

	# Hack -- if someone passes in (size_t)-1, hoping to just
	# search til the end of the address space, we will overflow
	# below when we find the address of the last byte.  Given
	# that we will never have a 56-bit address space, cropping
	# the length is the easiest way to avoid trouble.
	zap	$18, 0x80, $5	# U : Bound length
	beq	$18, $not_found	# U :
        ldq_u   $1, 0($16)	# L : load first quadword Latency=3
	and	$17, 0xff, $17	# E : L L U U : 00000000000000ch

	insbl	$17, 1, $2	# U : 000000000000ch00
	cmpult	$18, 9, $4	# E : small (< 1 quad) string?
	or	$2, $17, $17	# E : 000000000000chch
        lda     $3, -1($31)	# E : U L L U

	sll	$17, 16, $2	# U : 00000000chch0000
	addq	$16, $5, $5	# E : Max search address
	or	$2, $17, $17	# E : 00000000chchchch
	sll	$17, 32, $2	# U : U L L U : chchchch00000000

	or	$2, $17, $17	# E : chchchchchchchch
	extql	$1, $16, $7	# U : $7 is upper bits
	beq	$4, $first_quad	# U :
	ldq_u	$6, -1($5)	# L : L U U L : eight or less bytes to search Latency=3

	extqh	$6, $16, $6	# U : 2 cycle stall for $6
	mov	$16, $0		# E :
	nop			# E :
	or	$7, $6, $1	# E : L U L U $1 = quadword starting at $16

	# Deal with the case where at most 8 bytes remain to be searched
	# in $1.  E.g.:
	#	$18 = 6
	#	$1 = ????c6c5c4c3c2c1
$last_quad:
	negq	$18, $6		# E :
        xor	$17, $1, $1	# E :
	srl	$3, $6, $6	# U : $6 = mask of $18 bits set
        cmpbge  $31, $1, $2	# E : L U L U

	nop
	nop
	and	$2, $6, $2	# E :
        beq     $2, $not_found	# U : U L U L

$found_it:
#ifdef CONFIG_ALPHA_EV67
	/*
	 * Since we are guaranteed to have set one of the bits, we don't
	 * have to worry about coming back with a 0x40 out of cttz...
	 */
	cttz	$2, $3		# U0 :
	addq	$0, $3, $0	# E : All done
	nop			# E :
	ret			# L0 : L U L U
#else
	/*
	 * Slow and clunky.  It can probably be improved.
	 * An exercise left for others.
	 */
        negq    $2, $3		# E :
        and     $2, $3, $2	# E :
        and     $2, 0x0f, $1	# E :
        addq    $0, 4, $3	# E :

        cmoveq  $1, $3, $0	# E : Latency 2, extra map cycle
	nop			# E : keep with cmov
        and     $2, 0x33, $1	# E :
        addq    $0, 2, $3	# E : U L U L : 2 cycle stall on $0

        cmoveq  $1, $3, $0	# E : Latency 2, extra map cycle
	nop			# E : keep with cmov
        and     $2, 0x55, $1	# E :
        addq    $0, 1, $3	# E : U L U L : 2 cycle stall on $0

        cmoveq  $1, $3, $0	# E : Latency 2, extra map cycle
	nop
	nop
	ret			# L0 : L U L U
#endif

	# Deal with the case where $18 > 8 bytes remain to be
	# searched.  $16 may not be aligned.
	.align 4
$first_quad:
	andnot	$16, 0x7, $0	# E :
        insqh   $3, $16, $2	# U : $2 = 0000ffffffffffff ($16<0:2> ff)
        xor	$1, $17, $1	# E :
	or	$1, $2, $1	# E : U L U L $1 = ====ffffffffffff

        cmpbge  $31, $1, $2	# E :
        bne     $2, $found_it	# U :
	# At least one byte left to process.
	ldq	$1, 8($0)	# L :
	subq	$5, 1, $18	# E : U L U L

	addq	$0, 8, $0	# E :
	# Make $18 point to last quad to be accessed (the
	# last quad may or may not be partial).
	andnot	$18, 0x7, $18	# E :
	cmpult	$0, $18, $2	# E :
	beq	$2, $final	# U : U L U L

	# At least two quads remain to be accessed.

	subq	$18, $0, $4	# E : $4 <- nr quads to be processed
	and	$4, 8, $4	# E : odd number of quads?
	bne	$4, $odd_quad_count # U :
	# At least three quads remain to be accessed
	mov	$1, $4		# E : L U L U : move prefetched value to correct reg

	.align	4
$unrolled_loop:
	ldq	$1, 8($0)	# L : prefetch $1
	xor	$17, $4, $2	# E :
	cmpbge	$31, $2, $2	# E :
	bne	$2, $found_it	# U : U L U L

	addq	$0, 8, $0	# E :
	nop			# E :
	nop			# E :
	nop			# E :

$odd_quad_count:
	xor	$17, $1, $2	# E :
	ldq	$4, 8($0)	# L : prefetch $4
	cmpbge	$31, $2, $2	# E :
	addq	$0, 8, $6	# E :

	bne	$2, $found_it	# U :
	cmpult	$6, $18, $6	# E :
	addq	$0, 8, $0	# E :
	nop			# E :

	bne	$6, $unrolled_loop # U :
	mov	$4, $1		# E : move prefetched value into $1
	nop			# E :
	nop			# E :

$final:	subq	$5, $0, $18	# E : $18 <- number of bytes left to do
	nop			# E :
	nop			# E :
	bne	$18, $last_quad	# U :

$not_found:
	mov	$31, $0		# E :
	nop			# E :
	nop			# E :
	ret			# L0 :

        .end memchr
	EXPORT_SYMBOL(memchr)
