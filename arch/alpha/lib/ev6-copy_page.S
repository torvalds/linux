/* SPDX-License-Identifier: GPL-2.0 */
/*
 * arch/alpha/lib/ev6-copy_page.S
 *
 * Copy an entire page.
 */

/* The following comparison of this routine vs the normal copy_page.S
   was written by an unnamed ev6 hardware designer and forwarded to me
   via Steven Hobbs <hobbs@steven.zko.dec.com>.
 
   First Problem: STQ overflows.
   -----------------------------

	It would be nice if EV6 handled every resource overflow efficiently,
	but for some it doesn't.  Including store queue overflows.  It causes
	a trap and a restart of the pipe.

	To get around this we sometimes use (to borrow a term from a VSSAD
	researcher) "aeration".  The idea is to slow the rate at which the
	processor receives valid instructions by inserting nops in the fetch
	path.  In doing so, you can prevent the overflow and actually make
	the code run faster.  You can, of course, take advantage of the fact
	that the processor can fetch at most 4 aligned instructions per cycle.

	I inserted enough nops to force it to take 10 cycles to fetch the
	loop code.  In theory, EV6 should be able to execute this loop in
	9 cycles but I was not able to get it to run that fast -- the initial
	conditions were such that I could not reach this optimum rate on
	(chaotic) EV6.  I wrote the code such that everything would issue
	in order. 

   Second Problem: Dcache index matches.
   -------------------------------------

	If you are going to use this routine on random aligned pages, there
	is a 25% chance that the pages will be at the same dcache indices.
	This results in many nasty memory traps without care.

	The solution is to schedule the prefetches to avoid the memory
	conflicts.  I schedule the wh64 prefetches farther ahead of the
	read prefetches to avoid this problem.

   Third Problem: Needs more prefetching.
   --------------------------------------

	In order to improve the code I added deeper prefetching to take the
	most advantage of EV6's bandwidth.

	I also prefetched the read stream. Note that adding the read prefetch
	forced me to add another cycle to the inner-most kernel - up to 11
	from the original 8 cycles per iteration.  We could improve performance
	further by unrolling the loop and doing multiple prefetches per cycle.

   I think that the code below will be very robust and fast code for the
   purposes of copying aligned pages.  It is slower when both source and
   destination pages are in the dcache, but it is my guess that this is
   less important than the dcache miss case.  */

#include <linux/export.h>
	.text
	.align 4
	.global copy_page
	.ent copy_page
copy_page:
	.prologue 0

	/* Prefetch 5 read cachelines; write-hint 10 cache lines.  */
	wh64	($16)
	ldl	$31,0($17)
	ldl	$31,64($17)
	lda	$1,1*64($16)

	wh64	($1)
	ldl	$31,128($17)
	ldl	$31,192($17)
	lda	$1,2*64($16)

	wh64	($1)
	ldl	$31,256($17)
	lda	$18,118
	lda	$1,3*64($16)

	wh64	($1)
	nop
	lda	$1,4*64($16)
	lda	$2,5*64($16)

	wh64	($1)
	wh64	($2)
	lda	$1,6*64($16)
	lda	$2,7*64($16)

	wh64	($1)
	wh64	($2)
	lda	$1,8*64($16)
	lda	$2,9*64($16)

	wh64	($1)
	wh64	($2)
	lda	$19,10*64($16)
	nop

	/* Main prefetching/write-hinting loop.  */
1:	ldq	$0,0($17)
	ldq	$1,8($17)
	unop
	unop

	unop
	unop
	ldq	$2,16($17)
	ldq	$3,24($17)

	ldq	$4,32($17)
	ldq	$5,40($17)
	unop
	unop

	unop
	unop
	ldq	$6,48($17)
	ldq	$7,56($17)

	ldl	$31,320($17)
	unop
	unop
	unop

	/* This gives the extra cycle of aeration above the minimum.  */
	unop			
	unop
	unop
	unop

	wh64	($19)
	unop
	unop
	unop

	stq	$0,0($16)
	subq	$18,1,$18
	stq	$1,8($16)
	unop

	unop
	stq	$2,16($16)
	addq	$17,64,$17
	stq	$3,24($16)

	stq	$4,32($16)
	stq	$5,40($16)
	addq	$19,64,$19
	unop

	stq	$6,48($16)
	stq	$7,56($16)
	addq	$16,64,$16
	bne	$18, 1b

	/* Prefetch the final 5 cache lines of the read stream.  */
	lda	$18,10
	ldl	$31,320($17)
	ldl	$31,384($17)
	ldl	$31,448($17)

	ldl	$31,512($17)
	ldl	$31,576($17)
	nop
	nop

	/* Non-prefetching, non-write-hinting cleanup loop for the
	   final 10 cache lines.  */
2:	ldq	$0,0($17)
	ldq	$1,8($17)
	ldq	$2,16($17)
	ldq	$3,24($17)

	ldq	$4,32($17)
	ldq	$5,40($17)
	ldq	$6,48($17)
	ldq	$7,56($17)

	stq	$0,0($16)
	subq	$18,1,$18
	stq	$1,8($16)
	addq	$17,64,$17

	stq	$2,16($16)
	stq	$3,24($16)
	stq	$4,32($16)
	stq	$5,40($16)

	stq	$6,48($16)
	stq	$7,56($16)
	addq	$16,64,$16
	bne	$18, 2b

	ret
	nop
	unop
	nop

	.end copy_page
	EXPORT_SYMBOL(copy_page)
