#include "ppc_asm.h"

	.text
	.global _zimage_start
_zimage_start:

	/* PPC errata 213: needed by Virtex-4 FX */
	mfccr0  0
	oris    0,0,0x50000000@h
	mtccr0  0

	/*
	 * Invalidate the data cache if the data cache is turned off.
	 * - The 405 core does not invalidate the data cache on power-up
	 *   or reset but does turn off the data cache. We cannot assume
	 *   that the cache contents are valid.
	 * - If the data cache is turned on this must have been done by
	 *   a bootloader and we assume that the cache contents are
	 *   valid.
	 */
	mfdccr	r9
	cmplwi	r9,0
	bne	2f
	lis	r9,0
	li	r8,256
	mtctr	r8
1:	dccci	r0,r9
	addi	r9,r9,0x20
	bdnz	1b
2:	b	_zimage_start_lib
