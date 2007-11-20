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
 */
V_FUNCTION_BEGIN(__kernel_sync_dicache)
  .cfi_startproc
	mflr	r12
  .cfi_register lr,r12
	mr	r11,r3
	bl	__get_datapage@local
	mtlr	r12
	mr	r10,r3

	lwz	r7,CFG_DCACHE_BLOCKSZ(r10)
	addi	r5,r7,-1
	andc	r6,r11,r5		/* round low to line bdy */
	subf	r8,r6,r4		/* compute length */
	add	r8,r8,r5		/* ensure we get enough */
	lwz	r9,CFG_DCACHE_LOGBLOCKSZ(r10)
	srw.	r8,r8,r9		/* compute line count */
	crclr	cr0*4+so
	beqlr				/* nothing to do? */
	mtctr	r8
1:	dcbst	0,r6
	add	r6,r6,r7
	bdnz	1b
	sync

/* Now invalidate the instruction cache */

	lwz	r7,CFG_ICACHE_BLOCKSZ(r10)
	addi	r5,r7,-1
	andc	r6,r11,r5		/* round low to line bdy */
	subf	r8,r6,r4		/* compute length */
	add	r8,r8,r5
	lwz	r9,CFG_ICACHE_LOGBLOCKSZ(r10)
	srw.	r8,r8,r9		/* compute line count */
	crclr	cr0*4+so
	beqlr				/* nothing to do? */
	mtctr	r8
2:	icbi	0,r6
	add	r6,r6,r7
	bdnz	2b
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

