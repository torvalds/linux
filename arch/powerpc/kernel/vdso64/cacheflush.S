/*
 * vDSO provided cache flush routines
 *
 * Copyright (C) 2004 Benjamin Herrenschmuidt (benh@kernel.crashing.org),
 *                    IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <asm/processor.h>
#include <asm/ppc_asm.h>
#include <asm/vdso.h>
#include <asm/asm-offsets.h>

	.text

/*
 * Default "generic" version of __kernel_sync_dicache.
 *
 * void __kernel_sync_dicache(unsigned long start, unsigned long end)
 *
 * Flushes the data cache & invalidate the instruction cache for the
 * provided range [start, end[
 *
 * Note: all CPUs supported by this kernel have a 128 bytes cache
 * line size so we don't have to peek that info from the datapage
 */
V_FUNCTION_BEGIN(__kernel_sync_dicache)
  .cfi_startproc
	li	r5,127
	andc	r6,r3,r5		/* round low to line bdy */
	subf	r8,r6,r4		/* compute length */
	add	r8,r8,r5		/* ensure we get enough */
	srwi.	r8,r8,7			/* compute line count */
	crclr	cr0*4+so
	beqlr				/* nothing to do? */
	mtctr	r8
	mr	r3,r6
1:	dcbst	0,r3
	addi	r3,r3,128
	bdnz	1b
	sync
	mtctr	r8
1:	icbi	0,r6
	addi	r6,r6,128
	bdnz	1b
	isync
	li	r3,0
	blr
  .cfi_endproc
V_FUNCTION_END(__kernel_sync_dicache)


/*
 * POWER5 version of __kernel_sync_dicache
 */
V_FUNCTION_BEGIN(__kernel_sync_dicache_p5)
  .cfi_startproc
	crclr	cr0*4+so
	sync
	isync
	li	r3,0
	blr
  .cfi_endproc
V_FUNCTION_END(__kernel_sync_dicache_p5)
