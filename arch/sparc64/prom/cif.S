/* cif.S: PROM entry/exit assembler trampolines.
 *
 * Copyright (C) 1996,1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 * Copyright (C) 2005 David S. Miller <davem@davemloft.net>
 */

#include <asm/pstate.h>

	.text
	.globl	prom_cif_interface
prom_cif_interface:
	sethi	%hi(p1275buf), %o0
	or	%o0, %lo(p1275buf), %o0
	ldx	[%o0 + 0x010], %o1	! prom_cif_stack
	save	%o1, -0x190, %sp
	ldx	[%i0 + 0x008], %l2	! prom_cif_handler
	rdpr	%pstate, %l4
	wrpr	%g0, 0x15, %pstate	! save alternate globals
	stx	%g1, [%sp + 2047 + 0x0b0]
	stx	%g2, [%sp + 2047 + 0x0b8]
	stx	%g3, [%sp + 2047 + 0x0c0]
	stx	%g4, [%sp + 2047 + 0x0c8]
	stx	%g5, [%sp + 2047 + 0x0d0]
	stx	%g6, [%sp + 2047 + 0x0d8]
	stx	%g7, [%sp + 2047 + 0x0e0]
	wrpr	%g0, 0x814, %pstate	! save interrupt globals
	stx	%g1, [%sp + 2047 + 0x0e8]
	stx	%g2, [%sp + 2047 + 0x0f0]
	stx	%g3, [%sp + 2047 + 0x0f8]
	stx	%g4, [%sp + 2047 + 0x100]
	stx	%g5, [%sp + 2047 + 0x108]
	stx	%g6, [%sp + 2047 + 0x110]
	stx	%g7, [%sp + 2047 + 0x118]
	wrpr	%g0, 0x14, %pstate	! save normal globals
	stx	%g1, [%sp + 2047 + 0x120]
	stx	%g2, [%sp + 2047 + 0x128]
	stx	%g3, [%sp + 2047 + 0x130]
	stx	%g4, [%sp + 2047 + 0x138]
	stx	%g5, [%sp + 2047 + 0x140]
	stx	%g6, [%sp + 2047 + 0x148]
	stx	%g7, [%sp + 2047 + 0x150]
	wrpr	%g0, 0x414, %pstate	! save mmu globals
	stx	%g1, [%sp + 2047 + 0x158]
	stx	%g2, [%sp + 2047 + 0x160]
	stx	%g3, [%sp + 2047 + 0x168]
	stx	%g4, [%sp + 2047 + 0x170]
	stx	%g5, [%sp + 2047 + 0x178]
	stx	%g6, [%sp + 2047 + 0x180]
	stx	%g7, [%sp + 2047 + 0x188]
	mov	%g1, %l0		! also save to locals, so we can handle
	mov	%g2, %l1		! tlb faults later on, when accessing
	mov	%g3, %l3		! the stack.
	mov	%g7, %l5
	wrpr	%l4, PSTATE_IE, %pstate	! turn off interrupts
	call	%l2
	 add	%i0, 0x018, %o0		! prom_args
	wrpr	%g0, 0x414, %pstate	! restore mmu globals
	mov	%l0, %g1
	mov	%l1, %g2
	mov	%l3, %g3
	mov	%l5, %g7
	wrpr	%g0, 0x14, %pstate	! restore normal globals
	ldx	[%sp + 2047 + 0x120], %g1
	ldx	[%sp + 2047 + 0x128], %g2
	ldx	[%sp + 2047 + 0x130], %g3
	ldx	[%sp + 2047 + 0x138], %g4
	ldx	[%sp + 2047 + 0x140], %g5
	ldx	[%sp + 2047 + 0x148], %g6
	ldx	[%sp + 2047 + 0x150], %g7
	wrpr	%g0, 0x814, %pstate	! restore interrupt globals
	ldx	[%sp + 2047 + 0x0e8], %g1
	ldx	[%sp + 2047 + 0x0f0], %g2
	ldx	[%sp + 2047 + 0x0f8], %g3
	ldx	[%sp + 2047 + 0x100], %g4
	ldx	[%sp + 2047 + 0x108], %g5
	ldx	[%sp + 2047 + 0x110], %g6
	ldx	[%sp + 2047 + 0x118], %g7
	wrpr	%g0, 0x15, %pstate	! restore alternate globals
	ldx	[%sp + 2047 + 0x0b0], %g1
	ldx	[%sp + 2047 + 0x0b8], %g2
	ldx	[%sp + 2047 + 0x0c0], %g3
	ldx	[%sp + 2047 + 0x0c8], %g4
	ldx	[%sp + 2047 + 0x0d0], %g5
	ldx	[%sp + 2047 + 0x0d8], %g6
	ldx	[%sp + 2047 + 0x0e0], %g7
	wrpr	%l4, 0, %pstate	! restore original pstate
	ret
	 restore

	.globl	prom_cif_callback
prom_cif_callback:
	sethi	%hi(p1275buf), %o1
	or	%o1, %lo(p1275buf), %o1
	save	%sp, -0x270, %sp
	rdpr	%pstate, %l4
	wrpr	%g0, 0x15, %pstate	! save PROM alternate globals
	stx	%g1, [%sp + 2047 + 0x0b0]
	stx	%g2, [%sp + 2047 + 0x0b8]
	stx	%g3, [%sp + 2047 + 0x0c0]
	stx	%g4, [%sp + 2047 + 0x0c8]
	stx	%g5, [%sp + 2047 + 0x0d0]
	stx	%g6, [%sp + 2047 + 0x0d8]
	stx	%g7, [%sp + 2047 + 0x0e0]
					! restore Linux alternate globals
	ldx	[%sp + 2047 + 0x190], %g1
	ldx	[%sp + 2047 + 0x198], %g2
	ldx	[%sp + 2047 + 0x1a0], %g3
	ldx	[%sp + 2047 + 0x1a8], %g4
	ldx	[%sp + 2047 + 0x1b0], %g5
	ldx	[%sp + 2047 + 0x1b8], %g6
	ldx	[%sp + 2047 + 0x1c0], %g7
	wrpr	%g0, 0x814, %pstate	! save PROM interrupt globals
	stx	%g1, [%sp + 2047 + 0x0e8]
	stx	%g2, [%sp + 2047 + 0x0f0]
	stx	%g3, [%sp + 2047 + 0x0f8]
	stx	%g4, [%sp + 2047 + 0x100]
	stx	%g5, [%sp + 2047 + 0x108]
	stx	%g6, [%sp + 2047 + 0x110]
	stx	%g7, [%sp + 2047 + 0x118]
					! restore Linux interrupt globals
	ldx	[%sp + 2047 + 0x1c8], %g1
	ldx	[%sp + 2047 + 0x1d0], %g2
	ldx	[%sp + 2047 + 0x1d8], %g3
	ldx	[%sp + 2047 + 0x1e0], %g4
	ldx	[%sp + 2047 + 0x1e8], %g5
	ldx	[%sp + 2047 + 0x1f0], %g6
	ldx	[%sp + 2047 + 0x1f8], %g7
	wrpr	%g0, 0x14, %pstate	! save PROM normal globals
	stx	%g1, [%sp + 2047 + 0x120]
	stx	%g2, [%sp + 2047 + 0x128]
	stx	%g3, [%sp + 2047 + 0x130]
	stx	%g4, [%sp + 2047 + 0x138]
	stx	%g5, [%sp + 2047 + 0x140]
	stx	%g6, [%sp + 2047 + 0x148]
	stx	%g7, [%sp + 2047 + 0x150]
					! restore Linux normal globals
	ldx	[%sp + 2047 + 0x200], %g1
	ldx	[%sp + 2047 + 0x208], %g2
	ldx	[%sp + 2047 + 0x210], %g3
	ldx	[%sp + 2047 + 0x218], %g4
	ldx	[%sp + 2047 + 0x220], %g5
	ldx	[%sp + 2047 + 0x228], %g6
	ldx	[%sp + 2047 + 0x230], %g7
	wrpr	%g0, 0x414, %pstate	! save PROM mmu globals
	stx	%g1, [%sp + 2047 + 0x158]
	stx	%g2, [%sp + 2047 + 0x160]
	stx	%g3, [%sp + 2047 + 0x168]
	stx	%g4, [%sp + 2047 + 0x170]
	stx	%g5, [%sp + 2047 + 0x178]
	stx	%g6, [%sp + 2047 + 0x180]
	stx	%g7, [%sp + 2047 + 0x188]
					! restore Linux mmu globals
	ldx	[%sp + 2047 + 0x238], %o0
	ldx	[%sp + 2047 + 0x240], %o1
	ldx	[%sp + 2047 + 0x248], %l2
	ldx	[%sp + 2047 + 0x250], %l3
	ldx	[%sp + 2047 + 0x258], %l5
	ldx	[%sp + 2047 + 0x260], %l6
	ldx	[%sp + 2047 + 0x268], %l7
					! switch to Linux tba
	sethi	%hi(sparc64_ttable_tl0), %l1
	rdpr	%tba, %l0		! save PROM tba
	mov	%o0, %g1
	mov	%o1, %g2
	mov	%l2, %g3
	mov	%l3, %g4
	mov	%l5, %g5
	mov	%l6, %g6
	mov	%l7, %g7
	wrpr	%l1, %tba		! install Linux tba
	wrpr	%l4, 0, %pstate		! restore PSTATE
	call	prom_world
	 mov	%g0, %o0
	ldx	[%i1 + 0x000], %l2
	call	%l2
	 mov	%i0, %o0
	mov	%o0, %l1
	call	prom_world
	 or	%g0, 1, %o0
	wrpr	%g0, 0x14, %pstate	! interrupts off
					! restore PROM mmu globals
	ldx	[%sp + 2047 + 0x158], %o0
	ldx	[%sp + 2047 + 0x160], %o1
	ldx	[%sp + 2047 + 0x168], %l2
	ldx	[%sp + 2047 + 0x170], %l3
	ldx	[%sp + 2047 + 0x178], %l5
	ldx	[%sp + 2047 + 0x180], %l6
	ldx	[%sp + 2047 + 0x188], %l7
	wrpr	%g0, 0x414, %pstate	! restore PROM mmu globals
	mov	%o0, %g1
	mov	%o1, %g2
	mov	%l2, %g3
	mov	%l3, %g4
	mov	%l5, %g5
	mov	%l6, %g6
	mov	%l7, %g7
	wrpr	%l0, %tba		! restore PROM tba
	wrpr	%g0, 0x14, %pstate	! restore PROM normal globals
	ldx	[%sp + 2047 + 0x120], %g1
	ldx	[%sp + 2047 + 0x128], %g2
	ldx	[%sp + 2047 + 0x130], %g3
	ldx	[%sp + 2047 + 0x138], %g4
	ldx	[%sp + 2047 + 0x140], %g5
	ldx	[%sp + 2047 + 0x148], %g6
	ldx	[%sp + 2047 + 0x150], %g7
	wrpr	%g0, 0x814, %pstate	! restore PROM interrupt globals
	ldx	[%sp + 2047 + 0x0e8], %g1
	ldx	[%sp + 2047 + 0x0f0], %g2
	ldx	[%sp + 2047 + 0x0f8], %g3
	ldx	[%sp + 2047 + 0x100], %g4
	ldx	[%sp + 2047 + 0x108], %g5
	ldx	[%sp + 2047 + 0x110], %g6
	ldx	[%sp + 2047 + 0x118], %g7
	wrpr	%g0, 0x15, %pstate	! restore PROM alternate globals
	ldx	[%sp + 2047 + 0x0b0], %g1
	ldx	[%sp + 2047 + 0x0b8], %g2
	ldx	[%sp + 2047 + 0x0c0], %g3
	ldx	[%sp + 2047 + 0x0c8], %g4
	ldx	[%sp + 2047 + 0x0d0], %g5
	ldx	[%sp + 2047 + 0x0d8], %g6
	ldx	[%sp + 2047 + 0x0e0], %g7
	wrpr	%l4, 0, %pstate
	ret
	 restore %l1, 0, %o0

