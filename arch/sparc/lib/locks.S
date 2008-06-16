/*
 * locks.S: SMP low-level lock primitives on Sparc.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1998 Anton Blanchard (anton@progsoc.uts.edu.au)
 * Copyright (C) 1998 Jakub Jelinek   (jj@ultra.linux.cz)
 */

#include <asm/ptrace.h>
#include <asm/psr.h>
#include <asm/smp.h>
#include <asm/spinlock.h>

	.text
	.align	4

	/* Read/writer locks, as usual this is overly clever to make it
	 * as fast as possible.
	 */

	/* caches... */
___rw_read_enter_spin_on_wlock:
	orcc	%g2, 0x0, %g0
	be,a	___rw_read_enter
	 ldstub	[%g1 + 3], %g2
	b	___rw_read_enter_spin_on_wlock
	 ldub	[%g1 + 3], %g2
___rw_read_try_spin_on_wlock:
	andcc	%g2, 0xff, %g0
	be,a	___rw_read_try
	 ldstub	[%g1 + 3], %g2
	xnorcc	%g2, 0x0, %o0	/* if g2 is ~0, set o0 to 0 and bugger off */
	bne,a	___rw_read_enter_spin_on_wlock
	 ld	[%g1], %g2
	retl
	 mov	%g4, %o7
___rw_read_exit_spin_on_wlock:
	orcc	%g2, 0x0, %g0
	be,a	___rw_read_exit
	 ldstub	[%g1 + 3], %g2
	b	___rw_read_exit_spin_on_wlock
	 ldub	[%g1 + 3], %g2
___rw_write_enter_spin_on_wlock:
	orcc	%g2, 0x0, %g0
	be,a	___rw_write_enter
	 ldstub	[%g1 + 3], %g2
	b	___rw_write_enter_spin_on_wlock
	 ld	[%g1], %g2

	.globl	___rw_read_enter
___rw_read_enter:
	orcc	%g2, 0x0, %g0
	bne,a	___rw_read_enter_spin_on_wlock
	 ldub	[%g1 + 3], %g2
	ld	[%g1], %g2
	add	%g2, 1, %g2
	st	%g2, [%g1]
	retl
	 mov	%g4, %o7

	.globl	___rw_read_exit
___rw_read_exit:
	orcc	%g2, 0x0, %g0
	bne,a	___rw_read_exit_spin_on_wlock
	 ldub	[%g1 + 3], %g2
	ld	[%g1], %g2
	sub	%g2, 0x1ff, %g2
	st	%g2, [%g1]
	retl
	 mov	%g4, %o7

	.globl	___rw_read_try
___rw_read_try:
	orcc	%g2, 0x0, %g0
	bne	___rw_read_try_spin_on_wlock
	 ld	[%g1], %g2
	add	%g2, 1, %g2
	st	%g2, [%g1]
	set	1, %o1
	retl
	 mov	%g4, %o7

	.globl	___rw_write_enter
___rw_write_enter:
	orcc	%g2, 0x0, %g0
	bne	___rw_write_enter_spin_on_wlock
	 ld	[%g1], %g2
	andncc	%g2, 0xff, %g0
	bne,a	___rw_write_enter_spin_on_wlock
	 stb	%g0, [%g1 + 3]
	retl
	 mov	%g4, %o7
