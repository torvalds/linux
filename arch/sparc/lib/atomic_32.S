/* atomic.S: Move this stuff here for better ICACHE hit rates.
 *
 * Copyright (C) 1996 David S. Miller (davem@caipfs.rutgers.edu)
 */

#include <asm/ptrace.h>
#include <asm/psr.h>

	.text
	.align	4

	.globl  __atomic_begin
__atomic_begin:

#ifndef CONFIG_SMP
	.globl	___xchg32_sun4c
___xchg32_sun4c:
	rd	%psr, %g3
	andcc	%g3, PSR_PIL, %g0
	bne	1f
	 nop
	wr	%g3, PSR_PIL, %psr
	nop; nop; nop
1:
	andcc	%g3, PSR_PIL, %g0
	ld	[%g1], %g7
	bne	1f
	 st	%g2, [%g1]
	wr	%g3, 0x0, %psr
	nop; nop; nop
1:
	mov	%g7, %g2
	jmpl	%o7 + 8, %g0
	 mov	%g4, %o7

	.globl	___xchg32_sun4md
___xchg32_sun4md:
	swap	[%g1], %g2
	jmpl	%o7 + 8, %g0
	 mov	%g4, %o7
#endif

	.globl  __atomic_end
__atomic_end:
