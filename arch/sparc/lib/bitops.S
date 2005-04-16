/* bitops.S: Low level assembler bit operations.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/config.h>
#include <asm/ptrace.h>
#include <asm/psr.h>

	.text
	.align	4

	.globl  __bitops_begin
__bitops_begin:

	/* Take bits in %g2 and set them in word at %g1,
	 * return whether bits were set in original value
	 * in %g2.  %g4 holds value to restore into %o7
	 * in delay slot of jmpl return, %g3 + %g5 + %g7 can be
	 * used as temporaries and thus is considered clobbered
	 * by all callers.
	 */
	.globl	___set_bit
___set_bit:
	rd	%psr, %g3
	nop; nop; nop;
	or	%g3, PSR_PIL, %g5
	wr	%g5, 0x0, %psr
	nop; nop; nop
#ifdef CONFIG_SMP
	set	bitops_spinlock, %g5
2:	ldstub	[%g5], %g7		! Spin on the byte lock for SMP.
	orcc	%g7, 0x0, %g0		! Did we get it?
	bne	2b			! Nope...
#endif
	 ld	[%g1], %g7
	or	%g7, %g2, %g5
	and	%g7, %g2, %g2
#ifdef CONFIG_SMP
	st	%g5, [%g1]
	set	bitops_spinlock, %g5
	stb	%g0, [%g5]
#else
	st	%g5, [%g1]
#endif
	wr	%g3, 0x0, %psr
	nop; nop; nop
	jmpl	%o7, %g0
	 mov	%g4, %o7

	/* Same as above, but clears the bits from %g2 instead. */
	.globl	___clear_bit
___clear_bit:
	rd	%psr, %g3
	nop; nop; nop
	or	%g3, PSR_PIL, %g5
	wr	%g5, 0x0, %psr
	nop; nop; nop
#ifdef CONFIG_SMP
	set	bitops_spinlock, %g5
2:	ldstub	[%g5], %g7		! Spin on the byte lock for SMP.
	orcc	%g7, 0x0, %g0		! Did we get it?
	bne	2b			! Nope...
#endif
	 ld	[%g1], %g7
	andn	%g7, %g2, %g5
	and	%g7, %g2, %g2
#ifdef CONFIG_SMP
	st	%g5, [%g1]
	set	bitops_spinlock, %g5
	stb	%g0, [%g5]
#else
	st	%g5, [%g1]
#endif
	wr	%g3, 0x0, %psr
	nop; nop; nop
	jmpl	%o7, %g0
	 mov	%g4, %o7

	/* Same thing again, but this time toggles the bits from %g2. */
	.globl	___change_bit
___change_bit:
	rd	%psr, %g3
	nop; nop; nop
	or	%g3, PSR_PIL, %g5
	wr	%g5, 0x0, %psr
	nop; nop; nop
#ifdef CONFIG_SMP
	set	bitops_spinlock, %g5
2:	ldstub	[%g5], %g7		! Spin on the byte lock for SMP.
	orcc	%g7, 0x0, %g0		! Did we get it?
	bne	2b			! Nope...
#endif
	 ld	[%g1], %g7
	xor	%g7, %g2, %g5
	and	%g7, %g2, %g2
#ifdef CONFIG_SMP
	st	%g5, [%g1]
	set	bitops_spinlock, %g5
	stb	%g0, [%g5]
#else
	st	%g5, [%g1]
#endif
	wr	%g3, 0x0, %psr
	nop; nop; nop
	jmpl	%o7, %g0
	 mov	%g4, %o7

	.globl  __bitops_end
__bitops_end:
