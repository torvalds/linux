/*
 * Basic assembly code to read BHRB entries
 *
 * Copyright 2013 Anshuman Khandual, IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <asm/ppc_asm.h>
#include <asm/ppc-opcode.h>

	.text

.balign 8

/* r3 = n  (where n = [0-31])
 * The maximum number of BHRB entries supported with PPC_MFBHRBE instruction
 * is 1024. We have limited number of table entries here as POWER8 implements
 * 32 BHRB entries.
 */

/* .global read_bhrb */
_GLOBAL(read_bhrb)
	cmpldi	r3,31
	bgt	1f
	ld	r4,bhrb_table@got(r2)
	sldi	r3,r3,3
	add	r3,r4,r3
	mtctr	r3
	bctr
1:	li	r3,0
	blr

#define MFBHRB_TABLE1(n) PPC_MFBHRBE(R3,n); blr
#define MFBHRB_TABLE2(n) MFBHRB_TABLE1(n); MFBHRB_TABLE1(n+1)
#define MFBHRB_TABLE4(n) MFBHRB_TABLE2(n); MFBHRB_TABLE2(n+2)
#define MFBHRB_TABLE8(n) MFBHRB_TABLE4(n); MFBHRB_TABLE4(n+4)
#define MFBHRB_TABLE16(n) MFBHRB_TABLE8(n); MFBHRB_TABLE8(n+8)
#define MFBHRB_TABLE32(n) MFBHRB_TABLE16(n); MFBHRB_TABLE16(n+16)

bhrb_table:
	MFBHRB_TABLE32(0)
