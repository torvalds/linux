/*
 * arch/arm/common/mcpm_head.S -- kernel entry point for multi-cluster PM
 *
 * Created by:  Nicolas Pitre, March 2012
 * Copyright:   (C) 2012-2013  Linaro Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/linkage.h>
#include <asm/mcpm.h>

	.macro	pr_dbg	string
#if defined(CONFIG_DEBUG_LL) && defined(DEBUG)
	b	1901f
1902:	.asciz	"CPU"
1903:	.asciz	" cluster"
1904:	.asciz	": \string"
	.align
1901:	adr	r0, 1902b
	bl	printascii
	mov	r0, r9
	bl	printhex8
	adr	r0, 1903b
	bl	printascii
	mov	r0, r10
	bl	printhex8
	adr	r0, 1904b
	bl	printascii
#endif
	.endm

	.arm
	.align

ENTRY(mcpm_entry_point)

 THUMB(	adr	r12, BSYM(1f)	)
 THUMB(	bx	r12		)
 THUMB(	.thumb			)
1:
	mrc	p15, 0, r0, c0, c0, 5		@ MPIDR
	ubfx	r9, r0, #0, #8			@ r9 = cpu
	ubfx	r10, r0, #8, #8			@ r10 = cluster
	mov	r3, #MAX_CPUS_PER_CLUSTER
	mla	r4, r3, r10, r9			@ r4 = canonical CPU index
	cmp	r4, #(MAX_CPUS_PER_CLUSTER * MAX_NR_CLUSTERS)
	blo	2f

	/* We didn't expect this CPU.  Try to cheaply make it quiet. */
1:	wfi
	wfe
	b	1b

2:	pr_dbg	"kernel mcpm_entry_point\n"

	/*
	 * MMU is off so we need to get to mcpm_entry_vectors in a
	 * position independent way.
	 */
	adr	r5, 3f
	ldr	r6, [r5]
	add	r6, r5, r6			@ r6 = mcpm_entry_vectors

mcpm_entry_gated:
	ldr	r5, [r6, r4, lsl #2]		@ r5 = CPU entry vector
	cmp	r5, #0
	wfeeq
	beq	mcpm_entry_gated
	pr_dbg	"released\n"
	bx	r5

	.align	2

3:	.word	mcpm_entry_vectors - .

ENDPROC(mcpm_entry_point)

	.bss
	.align	5

	.type	mcpm_entry_vectors, #object
ENTRY(mcpm_entry_vectors)
	.space	4 * MAX_NR_CLUSTERS * MAX_CPUS_PER_CLUSTER
