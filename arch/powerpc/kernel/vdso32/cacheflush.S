/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * vDSO provided cache flush routines
 *
 * Copyright (C) 2004 Benjamin Herrenschmuidt (benh@kernel.crashing.org),
 *                    IBM Corp.
 */
#include <asm/processor.h>
#include <asm/ppc_asm.h>
#include <asm/vdso.h>
#include <asm/vdso_datapage.h>
#include <asm/asm-offsets.h>
#include <asm/cache.h>

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
BEGIN_FTR_SECTION
	b	3f
END_FTR_SECTION_IFSET(CPU_FTR_COHERENT_ICACHE)
#ifdef CONFIG_PPC64
	mflr	r12
  .cfi_register lr,r12
	get_datapage	r10
	mtlr	r12
  .cfi_restore	lr
#endif

#ifdef CONFIG_PPC64
	lwz	r7,CFG_DCACHE_BLOCKSZ(r10)
	addi	r5,r7,-1
#else
	li	r5, L1_CACHE_BYTES - 1
#endif
	andc	r6,r3,r5		/* round low to line bdy */
	subf	r8,r6,r4		/* compute length */
	add	r8,r8,r5		/* ensure we get enough */
#ifdef CONFIG_PPC64
	lwz	r9,CFG_DCACHE_LOGBLOCKSZ(r10)
	srw.	r8,r8,r9		/* compute line count */
#else
	srwi.	r8, r8, L1_CACHE_SHIFT
	mr	r7, r6
#endif
	crclr	cr0*4+so
	beqlr				/* nothing to do? */
	mtctr	r8
1:	dcbst	0,r6
#ifdef CONFIG_PPC64
	add	r6,r6,r7
#else
	addi	r6, r6, L1_CACHE_BYTES
#endif
	bdnz	1b
	sync

/* Now invalidate the instruction cache */

#ifdef CONFIG_PPC64
	lwz	r7,CFG_ICACHE_BLOCKSZ(r10)
	addi	r5,r7,-1
	andc	r6,r3,r5		/* round low to line bdy */
	subf	r8,r6,r4		/* compute length */
	add	r8,r8,r5
	lwz	r9,CFG_ICACHE_LOGBLOCKSZ(r10)
	srw.	r8,r8,r9		/* compute line count */
	crclr	cr0*4+so
	beqlr				/* nothing to do? */
#endif
	mtctr	r8
#ifdef CONFIG_PPC64
2:	icbi	0,r6
	add	r6,r6,r7
#else
2:	icbi	0, r7
	addi	r7, r7, L1_CACHE_BYTES
#endif
	bdnz	2b
	isync
	li	r3,0
	blr
3:
	crclr	cr0*4+so
	sync
	isync
	li	r3,0
	blr
  .cfi_endproc
V_FUNCTION_END(__kernel_sync_dicache)
