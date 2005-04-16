/* $Id: map.S,v 1.2 1999/11/19 05:53:02 davem Exp $
 * map.S: Tricky coding required to fixup the kernel OBP maps
 *	  properly.
 *
 * Copyright (C) 1999 David S. Miller (davem@redhat.com)
 */

	.text
	.align	8192
	.globl	prom_boot_page
prom_boot_page:
call_method:
	.asciz	"call-method"
	.align	8
map:
	.asciz	"map"
	.align	8

	/* When we are invoked, our caller has remapped us to
	 * page zero, therefore we must use PC relative addressing
	 * for everything after we begin performing the unmap/map
	 * calls.
	 */
	.globl	prom_remap
prom_remap:	/* %o0 = physpage, %o1 = virtpage, %o2 = mmu_ihandle */
	rd	%pc, %g1
	srl	%o2, 0, %o2			! kill sign extension
	sethi	%hi(p1275buf), %g2
	or	%g2, %lo(p1275buf), %g2
	ldx	[%g2 + 0x10], %g3		! prom_cif_stack
	save	%g3, -(192 + 128), %sp
	ldx	[%g2 + 0x08], %l0		! prom_cif_handler
	mov	%g6, %i3
	mov	%g4, %i4
	mov	%g5, %i5
	flushw

	sethi	%hi(prom_remap - call_method), %g7
	or	%g7, %lo(prom_remap - call_method), %g7
	sub	%g1, %g7, %l2			! call-method string
	sethi	%hi(prom_remap - map), %g7
	or	%g7, %lo(prom_remap - map), %g7
	sub	%g1, %g7, %l4			! map string

	/* OK, map the 4MB region we really live at. */
	stx	%l2, [%sp + 2047 + 128 + 0x00]	! call-method
	mov	7, %l5
	stx	%l5, [%sp + 2047 + 128 + 0x08]	! num_args
	mov	1, %l5
	stx	%l5, [%sp + 2047 + 128 + 0x10]	! num_rets
	stx	%l4, [%sp + 2047 + 128 + 0x18]	! map
	stx	%i2, [%sp + 2047 + 128 + 0x20]	! mmu_ihandle
	mov	-1, %l5
	stx	%l5, [%sp + 2047 + 128 + 0x28]	! mode == default
	sethi	%hi(4 * 1024 * 1024), %l5
	stx	%l5, [%sp + 2047 + 128 + 0x30]	! size
	stx	%i1, [%sp + 2047 + 128 + 0x38]	! vaddr
	stx	%g0, [%sp + 2047 + 128 + 0x40]	! filler
	stx	%i0, [%sp + 2047 + 128 + 0x48]	! paddr
	call	%l0
	 add	%sp, (2047 + 128), %o0		! argument array

	/* Restore hard-coded globals. */
	mov	%i3, %g6
	mov	%i4, %g4
	mov	%i5, %g5

	/* Wheee.... we are done. */
	ret
	restore

	.align	8192
