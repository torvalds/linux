/* bitops.S: Sparc64 atomic bit operations.
 *
 * Copyright (C) 2000, 2007 David S. Miller (davem@davemloft.net)
 */

#include <asm/asi.h>
#include <asm/backoff.h>

	.text

	/* On SMP we need to use memory barriers to ensure
	 * correct memory operation ordering, nop these out
	 * for uniprocessor.
	 */

#ifdef CONFIG_SMP
#define BITOP_PRE_BARRIER	membar #StoreLoad | #LoadLoad
#define BITOP_POST_BARRIER	\
	ba,pt	%xcc, 80b;	\
	membar #StoreLoad | #StoreStore

80:	retl
	 nop
#else
#define BITOP_PRE_BARRIER
#define BITOP_POST_BARRIER
#endif

	.globl	test_and_set_bit
	.type	test_and_set_bit,#function
test_and_set_bit:	/* %o0=nr, %o1=addr */
	BACKOFF_SETUP(%o3)
	BITOP_PRE_BARRIER
	srlx	%o0, 6, %g1
	mov	1, %o2
	sllx	%g1, 3, %g3
	and	%o0, 63, %g2
	sllx	%o2, %g2, %o2
	add	%o1, %g3, %o1
1:	ldx	[%o1], %g7
	or	%g7, %o2, %g1
	casx	[%o1], %g7, %g1
	cmp	%g7, %g1
	bne,pn	%xcc, 2f
	 and	%g7, %o2, %g2
	clr	%o0
	movrne	%g2, 1, %o0
	BITOP_POST_BARRIER
	retl
	 nop
2:	BACKOFF_SPIN(%o3, %o4, 1b)
	.size	test_and_set_bit, .-test_and_set_bit

	.globl	test_and_clear_bit
	.type	test_and_clear_bit,#function
test_and_clear_bit:	/* %o0=nr, %o1=addr */
	BACKOFF_SETUP(%o3)
	BITOP_PRE_BARRIER
	srlx	%o0, 6, %g1
	mov	1, %o2
	sllx	%g1, 3, %g3
	and	%o0, 63, %g2
	sllx	%o2, %g2, %o2
	add	%o1, %g3, %o1
1:	ldx	[%o1], %g7
	andn	%g7, %o2, %g1
	casx	[%o1], %g7, %g1
	cmp	%g7, %g1
	bne,pn	%xcc, 2f
	 and	%g7, %o2, %g2
	clr	%o0
	movrne	%g2, 1, %o0
	BITOP_POST_BARRIER
	retl
	 nop
2:	BACKOFF_SPIN(%o3, %o4, 1b)
	.size	test_and_clear_bit, .-test_and_clear_bit

	.globl	test_and_change_bit
	.type	test_and_change_bit,#function
test_and_change_bit:	/* %o0=nr, %o1=addr */
	BACKOFF_SETUP(%o3)
	BITOP_PRE_BARRIER
	srlx	%o0, 6, %g1
	mov	1, %o2
	sllx	%g1, 3, %g3
	and	%o0, 63, %g2
	sllx	%o2, %g2, %o2
	add	%o1, %g3, %o1
1:	ldx	[%o1], %g7
	xor	%g7, %o2, %g1
	casx	[%o1], %g7, %g1
	cmp	%g7, %g1
	bne,pn	%xcc, 2f
	 and	%g7, %o2, %g2
	clr	%o0
	movrne	%g2, 1, %o0
	BITOP_POST_BARRIER
	retl
	 nop
2:	BACKOFF_SPIN(%o3, %o4, 1b)
	.size	test_and_change_bit, .-test_and_change_bit

	.globl	set_bit
	.type	set_bit,#function
set_bit:		/* %o0=nr, %o1=addr */
	BACKOFF_SETUP(%o3)
	srlx	%o0, 6, %g1
	mov	1, %o2
	sllx	%g1, 3, %g3
	and	%o0, 63, %g2
	sllx	%o2, %g2, %o2
	add	%o1, %g3, %o1
1:	ldx	[%o1], %g7
	or	%g7, %o2, %g1
	casx	[%o1], %g7, %g1
	cmp	%g7, %g1
	bne,pn	%xcc, 2f
	 nop
	retl
	 nop
2:	BACKOFF_SPIN(%o3, %o4, 1b)
	.size	set_bit, .-set_bit

	.globl	clear_bit
	.type	clear_bit,#function
clear_bit:		/* %o0=nr, %o1=addr */
	BACKOFF_SETUP(%o3)
	srlx	%o0, 6, %g1
	mov	1, %o2
	sllx	%g1, 3, %g3
	and	%o0, 63, %g2
	sllx	%o2, %g2, %o2
	add	%o1, %g3, %o1
1:	ldx	[%o1], %g7
	andn	%g7, %o2, %g1
	casx	[%o1], %g7, %g1
	cmp	%g7, %g1
	bne,pn	%xcc, 2f
	 nop
	retl
	 nop
2:	BACKOFF_SPIN(%o3, %o4, 1b)
	.size	clear_bit, .-clear_bit

	.globl	change_bit
	.type	change_bit,#function
change_bit:		/* %o0=nr, %o1=addr */
	BACKOFF_SETUP(%o3)
	srlx	%o0, 6, %g1
	mov	1, %o2
	sllx	%g1, 3, %g3
	and	%o0, 63, %g2
	sllx	%o2, %g2, %o2
	add	%o1, %g3, %o1
1:	ldx	[%o1], %g7
	xor	%g7, %o2, %g1
	casx	[%o1], %g7, %g1
	cmp	%g7, %g1
	bne,pn	%xcc, 2f
	 nop
	retl
	 nop
2:	BACKOFF_SPIN(%o3, %o4, 1b)
	.size	change_bit, .-change_bit
