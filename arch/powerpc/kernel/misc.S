/*
 * This file contains miscellaneous low-level functions.
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 * Largely rewritten by Cort Dougan (cort@cs.nmt.edu)
 * and Paul Mackerras.
 *
 * Adapted for iSeries by Mike Corrigan (mikejc@us.ibm.com)
 * PPC64 updates by Dave Engebretsen (engebret@us.ibm.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <asm/ppc_asm.h>

	.text

/*
 * Returns (address we are running at) - (address we were linked at)
 * for use before the text and data are mapped to KERNELBASE.
 */

_GLOBAL(reloc_offset)
	mflr	r0
	bl	1f
1:	mflr	r3
	LOAD_REG_IMMEDIATE(r4,1b)
	subf	r3,r4,r3
	mtlr	r0
	blr

/*
 * add_reloc_offset(x) returns x + reloc_offset().
 */
_GLOBAL(add_reloc_offset)
	mflr	r0
	bl	1f
1:	mflr	r5
	LOAD_REG_IMMEDIATE(r4,1b)
	subf	r5,r4,r5
	add	r3,r3,r5
	mtlr	r0
	blr

/*
 * I/O string operations
 *
 * insb(port, buf, len)
 * outsb(port, buf, len)
 * insw(port, buf, len)
 * outsw(port, buf, len)
 * insl(port, buf, len)
 * outsl(port, buf, len)
 * insw_ns(port, buf, len)
 * outsw_ns(port, buf, len)
 * insl_ns(port, buf, len)
 * outsl_ns(port, buf, len)
 *
 * The *_ns versions don't do byte-swapping.
 */
_GLOBAL(_insb)
	sync
	cmpwi	0,r5,0
	mtctr	r5
	subi	r4,r4,1
	blelr-
00:	lbz	r5,0(r3)
	eieio
	stbu	r5,1(r4)
	bdnz	00b
	twi	0,r5,0
	isync
	blr

_GLOBAL(_outsb)
	cmpwi	0,r5,0
	mtctr	r5
	subi	r4,r4,1
	blelr-
	sync
00:	lbzu	r5,1(r4)
	stb	r5,0(r3)
	bdnz	00b
	sync
	blr

_GLOBAL(_insw)
	sync
	cmpwi	0,r5,0
	mtctr	r5
	subi	r4,r4,2
	blelr-
00:	lhbrx	r5,0,r3
	eieio
	sthu	r5,2(r4)
	bdnz	00b
	twi	0,r5,0
	isync
	blr

_GLOBAL(_outsw)
	cmpwi	0,r5,0
	mtctr	r5
	subi	r4,r4,2
	blelr-
	sync
00:	lhzu	r5,2(r4)
	sthbrx	r5,0,r3
	bdnz	00b
	sync
	blr

_GLOBAL(_insl)
	sync
	cmpwi	0,r5,0
	mtctr	r5
	subi	r4,r4,4
	blelr-
00:	lwbrx	r5,0,r3
	eieio
	stwu	r5,4(r4)
	bdnz	00b
	twi	0,r5,0
	isync
	blr

_GLOBAL(_outsl)
	cmpwi	0,r5,0
	mtctr	r5
	subi	r4,r4,4
	blelr-
	sync
00:	lwzu	r5,4(r4)
	stwbrx	r5,0,r3
	bdnz	00b
	sync
	blr

#ifdef CONFIG_PPC32
_GLOBAL(__ide_mm_insw)
#endif
_GLOBAL(_insw_ns)
	sync
	cmpwi	0,r5,0
	mtctr	r5
	subi	r4,r4,2
	blelr-
00:	lhz	r5,0(r3)
	eieio
	sthu	r5,2(r4)
	bdnz	00b
	twi	0,r5,0
	isync
	blr

#ifdef CONFIG_PPC32
_GLOBAL(__ide_mm_outsw)
#endif
_GLOBAL(_outsw_ns)
	cmpwi	0,r5,0
	mtctr	r5
	subi	r4,r4,2
	blelr-
	sync
00:	lhzu	r5,2(r4)
	sth	r5,0(r3)
	bdnz	00b
	sync
	blr

#ifdef CONFIG_PPC32
_GLOBAL(__ide_mm_insl)
#endif
_GLOBAL(_insl_ns)
	sync
	cmpwi	0,r5,0
	mtctr	r5
	subi	r4,r4,4
	blelr-
00:	lwz	r5,0(r3)
	eieio
	stwu	r5,4(r4)
	bdnz	00b
	twi	0,r5,0
	isync
	blr

#ifdef CONFIG_PPC32
_GLOBAL(__ide_mm_outsl)
#endif
_GLOBAL(_outsl_ns)
	cmpwi	0,r5,0
	mtctr	r5
	subi	r4,r4,4
	blelr-
	sync
00:	lwzu	r5,4(r4)
	stw	r5,0(r3)
	bdnz	00b
	sync
	blr

