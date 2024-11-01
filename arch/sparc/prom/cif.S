/* SPDX-License-Identifier: GPL-2.0 */
/* cif.S: PROM entry/exit assembler trampolines.
 *
 * Copyright (C) 1996, 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 * Copyright (C) 2005, 2006 David S. Miller <davem@davemloft.net>
 */

#include <asm/pstate.h>
#include <asm/cpudata.h>
#include <asm/thread_info.h>

	.text
	.globl	prom_cif_direct
prom_cif_direct:
	save	%sp, -192, %sp
	sethi	%hi(p1275buf), %o1
	or	%o1, %lo(p1275buf), %o1
	ldx	[%o1 + 0x0008], %l2	! prom_cif_handler
	mov	%g4, %l0
	mov	%g5, %l1
	mov	%g6, %l3
	call	%l2
	 mov	%i0, %o0		! prom_args
	mov	%l0, %g4
	mov	%l1, %g5
	mov	%l3, %g6
	ret
	 restore

	.globl	prom_cif_callback
prom_cif_callback:
	sethi	%hi(p1275buf), %o1
	or	%o1, %lo(p1275buf), %o1
	save	%sp, -192, %sp
	TRAP_LOAD_THREAD_REG(%g6, %g1)
	LOAD_PER_CPU_BASE(%g5, %g6, %g4, %g3, %o0)
	ldx	[%g6 + TI_TASK], %g4
	call	prom_world
	 mov	0, %o0
	ldx	[%i1 + 0x000], %l2
	call	%l2
	 mov	%i0, %o0
	mov	%o0, %l1
	call	prom_world
	 mov	1, %o0
	ret
	 restore %l1, 0, %o0

