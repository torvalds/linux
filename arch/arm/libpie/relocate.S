/*
 * arch/arm/libpie/relocate.S - Relocation updating for PIEs
 *
 * Copyright 2013 Texas Instruments, Inc.
 *	Russ Dill <russ.dill@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */

#include <linux/linkage.h>

/*
 * Update relocations based on current pc
 *
 * On exit:
 *  r4-r9 corrupted
 */

ENTRY(__pie_relocate)
	/* Calculate offset of our code compared to existing relocations */
	ldr	r4, pie_relocate_address
	adr	r5, __pie_relocate
	subs	r6, r5, r4
	moveq	pc, lr			/* 0 offset, no need to do anything */

	/* Base of PIE group */
	ldr	r7, reloc_offset
	sub	r5, r5, r7

	/* Calculate address of tail */
	ldr	r7, tail_offset
	add	r7, r7, r5

	/* First byte of tail is number of entries */
	ldr	r8, [r7], #4
	add	r8, r7, r8, lsl #2

	/*
	 * r5 - current base address of PIE group
	 * r6 - fixup offset needed for relocs
	 * r7 - relocs start
	 * r8 - relocs end
	 */

1:
	cmp	r7, r8
	ldrne	r4, [r7], #4	/* Load next reloc offset */

	addne	r4, r4, r5	/* Calculate address of reloc entry */
	ldrne	r9, [r4]
	addne	r9, r9, r6	/* Fixup reloc entry */
	strne	r9, [r4]

	bne	1b

	mov	pc, lr
ENDPROC(__pie_relocate)

/*
 * This ends up in the .rel.dyn section and can be used to read the current
 * relocation offset
 */
pie_relocate_address:
	.long 	__pie_relocate

/* Offset from PIE section start to reloc function */
.global reloc_offset
reloc_offset:
	.space	4

/* Offset from PIE section start to tail */
.globl tail_offset
tail_offset:
	.space	4
