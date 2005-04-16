/*
 * arch/alpha/lib/strlen_user.S
 *
 * Return the length of the string including the NUL terminator
 * (strlen+1) or zero if an error occurred.
 *
 * In places where it is critical to limit the processing time,
 * and the data is not trusted, strnlen_user() should be used.
 * It will return a value greater than its second argument if
 * that limit would be exceeded. This implementation is allowed
 * to access memory beyond the limit, but will not cross a page
 * boundary when doing so.
 */

#include <asm/regdef.h>


/* Allow an exception for an insn; exit if we get one.  */
#define EX(x,y...)			\
	99: x,##y;			\
	.section __ex_table,"a";	\
	.long 99b - .;			\
	lda v0, $exception-99b(zero);	\
	.previous


	.set noreorder
	.set noat
	.text

	.globl __strlen_user
	.ent __strlen_user
	.frame sp, 0, ra

	.align 3
__strlen_user:
	ldah	a1, 32767(zero)	# do not use plain strlen_user() for strings
				# that might be almost 2 GB long; you should
				# be using strnlen_user() instead

	.globl __strnlen_user

	.align 3
__strnlen_user:
	.prologue 0

	EX( ldq_u t0, 0(a0) )	# load first quadword (a0 may be misaligned)
	lda     t1, -1(zero)
	insqh   t1, a0, t1
	andnot  a0, 7, v0
	or      t1, t0, t0
	subq	a0, 1, a0	# get our +1 for the return 
	cmpbge  zero, t0, t1	# t1 <- bitmask: bit i == 1 <==> i-th byte == 0
	subq	a1, 7, t2
	subq	a0, v0, t0
	bne     t1, $found

	addq	t2, t0, t2
	addq	a1, 1, a1

	.align 3
$loop:	ble	t2, $limit
	EX( ldq t0, 8(v0) )
	subq	t2, 8, t2
	addq    v0, 8, v0	# addr += 8
	cmpbge  zero, t0, t1
	beq     t1, $loop

$found:	negq    t1, t2		# clear all but least set bit
	and     t1, t2, t1

	and     t1, 0xf0, t2	# binary search for that set bit
	and	t1, 0xcc, t3
	and	t1, 0xaa, t4
	cmovne	t2, 4, t2
	cmovne	t3, 2, t3
	cmovne	t4, 1, t4
	addq	t2, t3, t2
	addq	v0, t4, v0
	addq	v0, t2, v0
	nop			# dual issue next two on ev4 and ev5
	subq    v0, a0, v0
$exception:
	ret

	.align 3		# currently redundant
$limit:
	subq	a1, t2, v0
	ret

	.end __strlen_user
