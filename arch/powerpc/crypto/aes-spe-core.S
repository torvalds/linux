/*
 * Fast AES implementation for SPE instruction set (PPC)
 *
 * This code makes use of the SPE SIMD instruction set as defined in
 * http://cache.freescale.com/files/32bit/doc/ref_manual/SPEPIM.pdf
 * Implementation is based on optimization guide notes from
 * http://cache.freescale.com/files/32bit/doc/app_note/AN2665.pdf
 *
 * Copyright (c) 2015 Markus Stockhausen <stockhausen@collogia.de>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */

#include <asm/ppc_asm.h>
#include "aes-spe-regs.h"

#define	EAD(in, bpos) \
	rlwimi		rT0,in,28-((bpos+3)%4)*8,20,27;

#define DAD(in, bpos) \
	rlwimi		rT1,in,24-((bpos+3)%4)*8,24,31;

#define LWH(out, off) \
	evlwwsplat	out,off(rT0);	/* load word high		*/

#define LWL(out, off) \
	lwz		out,off(rT0);	/* load word low		*/

#define LBZ(out, tab, off) \
	lbz		out,off(tab);	/* load byte			*/

#define LAH(out, in, bpos, off) \
	EAD(in, bpos)			/* calc addr + load word high	*/ \
	LWH(out, off)

#define LAL(out, in, bpos, off) \
	EAD(in, bpos)			/* calc addr + load word low	*/ \
	LWL(out, off)

#define LAE(out, in, bpos) \
	EAD(in, bpos)			/* calc addr + load enc byte	*/ \
	LBZ(out, rT0, 8)

#define LBE(out) \
	LBZ(out, rT0, 8)		/* load enc byte		*/

#define LAD(out, in, bpos) \
	DAD(in, bpos)			/* calc addr + load dec byte	*/ \
	LBZ(out, rT1, 0)

#define LBD(out) \
	LBZ(out, rT1, 0)

/*
 * ppc_encrypt_block: The central encryption function for a single 16 bytes
 * block. It does no stack handling or register saving to support fast calls
 * via bl/blr. It expects that caller has pre-xored input data with first
 * 4 words of encryption key into rD0-rD3. Pointer/counter registers must
 * have also been set up before (rT0, rKP, CTR). Output is stored in rD0-rD3
 * and rW0-rW3 and caller must execute a final xor on the ouput registers.
 * All working registers rD0-rD3 & rW0-rW7 are overwritten during processing.
 *
 */
_GLOBAL(ppc_encrypt_block)
	LAH(rW4, rD1, 2, 4)
	LAH(rW6, rD0, 3, 0)
	LAH(rW3, rD0, 1, 8)
ppc_encrypt_block_loop:
	LAH(rW0, rD3, 0, 12)
	LAL(rW0, rD0, 0, 12)
	LAH(rW1, rD1, 0, 12)
	LAH(rW2, rD2, 1, 8)
	LAL(rW2, rD3, 1, 8)
	LAL(rW3, rD1, 1, 8)
	LAL(rW4, rD2, 2, 4)
	LAL(rW6, rD1, 3, 0)
	LAH(rW5, rD3, 2, 4)
	LAL(rW5, rD0, 2, 4)
	LAH(rW7, rD2, 3, 0)
	evldw		rD1,16(rKP)
	EAD(rD3, 3)
	evxor		rW2,rW2,rW4
	LWL(rW7, 0)
	evxor		rW2,rW2,rW6
	EAD(rD2, 0)
	evxor		rD1,rD1,rW2
	LWL(rW1, 12)
	evxor		rD1,rD1,rW0
	evldw		rD3,24(rKP)
	evmergehi	rD0,rD0,rD1
	EAD(rD1, 2)
	evxor		rW3,rW3,rW5
	LWH(rW4, 4)
	evxor		rW3,rW3,rW7
	EAD(rD0, 3)
	evxor		rD3,rD3,rW3
	LWH(rW6, 0)
	evxor		rD3,rD3,rW1
	EAD(rD0, 1)
	evmergehi	rD2,rD2,rD3
	LWH(rW3, 8)
	LAH(rW0, rD3, 0, 12)
	LAL(rW0, rD0, 0, 12)
	LAH(rW1, rD1, 0, 12)
	LAH(rW2, rD2, 1, 8)
	LAL(rW2, rD3, 1, 8)
	LAL(rW3, rD1, 1, 8)
	LAL(rW4, rD2, 2, 4)
	LAL(rW6, rD1, 3, 0)
	LAH(rW5, rD3, 2, 4)
	LAL(rW5, rD0, 2, 4)
	LAH(rW7, rD2, 3, 0)
	evldw		rD1,32(rKP)
	EAD(rD3, 3)
	evxor		rW2,rW2,rW4
	LWL(rW7, 0)
	evxor		rW2,rW2,rW6
	EAD(rD2, 0)
	evxor		rD1,rD1,rW2
	LWL(rW1, 12)
	evxor		rD1,rD1,rW0
	evldw		rD3,40(rKP)
	evmergehi	rD0,rD0,rD1
	EAD(rD1, 2)
	evxor		rW3,rW3,rW5
	LWH(rW4, 4)
	evxor		rW3,rW3,rW7
	EAD(rD0, 3)
	evxor		rD3,rD3,rW3
	LWH(rW6, 0)
	evxor		rD3,rD3,rW1
	EAD(rD0, 1)
	evmergehi	rD2,rD2,rD3
	LWH(rW3, 8)
	addi		rKP,rKP,32
	bdnz		ppc_encrypt_block_loop
	LAH(rW0, rD3, 0, 12)
	LAL(rW0, rD0, 0, 12)
	LAH(rW1, rD1, 0, 12)
	LAH(rW2, rD2, 1, 8)
	LAL(rW2, rD3, 1, 8)
	LAL(rW3, rD1, 1, 8)
	LAL(rW4, rD2, 2, 4)
	LAH(rW5, rD3, 2, 4)
	LAL(rW6, rD1, 3, 0)
	LAL(rW5, rD0, 2, 4)
	LAH(rW7, rD2, 3, 0)
	evldw		rD1,16(rKP)
	EAD(rD3, 3)
	evxor		rW2,rW2,rW4
	LWL(rW7, 0)
	evxor		rW2,rW2,rW6
	EAD(rD2, 0)
	evxor		rD1,rD1,rW2
	LWL(rW1, 12)
	evxor		rD1,rD1,rW0
	evldw		rD3,24(rKP)
	evmergehi	rD0,rD0,rD1
	EAD(rD1, 0)
	evxor		rW3,rW3,rW5
	LBE(rW2)
	evxor		rW3,rW3,rW7
	EAD(rD0, 1)
	evxor		rD3,rD3,rW3
	LBE(rW6)
	evxor		rD3,rD3,rW1
	EAD(rD0, 0)
	evmergehi	rD2,rD2,rD3
	LBE(rW1)
	LAE(rW0, rD3, 0)
	LAE(rW1, rD0, 0)
	LAE(rW4, rD2, 1)
	LAE(rW5, rD3, 1)
	LAE(rW3, rD2, 0)
	LAE(rW7, rD1, 1)
	rlwimi		rW0,rW4,8,16,23
	rlwimi		rW1,rW5,8,16,23
	LAE(rW4, rD1, 2)
	LAE(rW5, rD2, 2)
	rlwimi		rW2,rW6,8,16,23
	rlwimi		rW3,rW7,8,16,23
	LAE(rW6, rD3, 2)
	LAE(rW7, rD0, 2)
	rlwimi		rW0,rW4,16,8,15
	rlwimi		rW1,rW5,16,8,15
	LAE(rW4, rD0, 3)
	LAE(rW5, rD1, 3)
	rlwimi		rW2,rW6,16,8,15
	lwz		rD0,32(rKP)
	rlwimi		rW3,rW7,16,8,15
	lwz		rD1,36(rKP)
	LAE(rW6, rD2, 3)
	LAE(rW7, rD3, 3)
	rlwimi		rW0,rW4,24,0,7
	lwz		rD2,40(rKP)
	rlwimi		rW1,rW5,24,0,7
	lwz		rD3,44(rKP)
	rlwimi		rW2,rW6,24,0,7
	rlwimi		rW3,rW7,24,0,7
	blr

/*
 * ppc_decrypt_block: The central decryption function for a single 16 bytes
 * block. It does no stack handling or register saving to support fast calls
 * via bl/blr. It expects that caller has pre-xored input data with first
 * 4 words of encryption key into rD0-rD3. Pointer/counter registers must
 * have also been set up before (rT0, rKP, CTR). Output is stored in rD0-rD3
 * and rW0-rW3 and caller must execute a final xor on the ouput registers.
 * All working registers rD0-rD3 & rW0-rW7 are overwritten during processing.
 *
 */
_GLOBAL(ppc_decrypt_block)
	LAH(rW0, rD1, 0, 12)
	LAH(rW6, rD0, 3, 0)
	LAH(rW3, rD0, 1, 8)
ppc_decrypt_block_loop:
	LAH(rW1, rD3, 0, 12)
	LAL(rW0, rD2, 0, 12)
	LAH(rW2, rD2, 1, 8)
	LAL(rW2, rD3, 1, 8)
	LAH(rW4, rD3, 2, 4)
	LAL(rW4, rD0, 2, 4)
	LAL(rW6, rD1, 3, 0)
	LAH(rW5, rD1, 2, 4)
	LAH(rW7, rD2, 3, 0)
	LAL(rW7, rD3, 3, 0)
	LAL(rW3, rD1, 1, 8)
	evldw		rD1,16(rKP)
	EAD(rD0, 0)
	evxor		rW4,rW4,rW6
	LWL(rW1, 12)
	evxor		rW0,rW0,rW4
	EAD(rD2, 2)
	evxor		rW0,rW0,rW2
	LWL(rW5, 4)
	evxor		rD1,rD1,rW0
	evldw		rD3,24(rKP)
	evmergehi	rD0,rD0,rD1
	EAD(rD1, 0)
	evxor		rW3,rW3,rW7
	LWH(rW0, 12)
	evxor		rW3,rW3,rW1
	EAD(rD0, 3)
	evxor		rD3,rD3,rW3
	LWH(rW6, 0)
	evxor		rD3,rD3,rW5
	EAD(rD0, 1)
	evmergehi	rD2,rD2,rD3
	LWH(rW3, 8)
	LAH(rW1, rD3, 0, 12)
	LAL(rW0, rD2, 0, 12)
	LAH(rW2, rD2, 1, 8)
	LAL(rW2, rD3, 1, 8)
	LAH(rW4, rD3, 2, 4)
	LAL(rW4, rD0, 2, 4)
	LAL(rW6, rD1, 3, 0)
	LAH(rW5, rD1, 2, 4)
	LAH(rW7, rD2, 3, 0)
	LAL(rW7, rD3, 3, 0)
	LAL(rW3, rD1, 1, 8)
	evldw		 rD1,32(rKP)
	EAD(rD0, 0)
	evxor		rW4,rW4,rW6
	LWL(rW1, 12)
	evxor		rW0,rW0,rW4
	EAD(rD2, 2)
	evxor		rW0,rW0,rW2
	LWL(rW5, 4)
	evxor		rD1,rD1,rW0
	evldw		rD3,40(rKP)
	evmergehi	rD0,rD0,rD1
	EAD(rD1, 0)
	evxor		rW3,rW3,rW7
	LWH(rW0, 12)
	evxor		rW3,rW3,rW1
	EAD(rD0, 3)
	evxor		rD3,rD3,rW3
	LWH(rW6, 0)
	evxor		rD3,rD3,rW5
	EAD(rD0, 1)
	evmergehi	rD2,rD2,rD3
	LWH(rW3, 8)
	addi		rKP,rKP,32
	bdnz		ppc_decrypt_block_loop
	LAH(rW1, rD3, 0, 12)
	LAL(rW0, rD2, 0, 12)
	LAH(rW2, rD2, 1, 8)
	LAL(rW2, rD3, 1, 8)
	LAH(rW4, rD3, 2, 4)
	LAL(rW4, rD0, 2, 4)
	LAL(rW6, rD1, 3, 0)
	LAH(rW5, rD1, 2, 4)
	LAH(rW7, rD2, 3, 0)
	LAL(rW7, rD3, 3, 0)
	LAL(rW3, rD1, 1, 8)
	evldw		 rD1,16(rKP)
	EAD(rD0, 0)
	evxor		rW4,rW4,rW6
	LWL(rW1, 12)
	evxor		rW0,rW0,rW4
	EAD(rD2, 2)
	evxor		rW0,rW0,rW2
	LWL(rW5, 4)
	evxor		rD1,rD1,rW0
	evldw		rD3,24(rKP)
	evmergehi	rD0,rD0,rD1
	DAD(rD1, 0)
	evxor		rW3,rW3,rW7
	LBD(rW0)
	evxor		rW3,rW3,rW1
	DAD(rD0, 1)
	evxor		rD3,rD3,rW3
	LBD(rW6)
	evxor		rD3,rD3,rW5
	DAD(rD0, 0)
	evmergehi	rD2,rD2,rD3
	LBD(rW3)
	LAD(rW2, rD3, 0)
	LAD(rW1, rD2, 0)
	LAD(rW4, rD2, 1)
	LAD(rW5, rD3, 1)
	LAD(rW7, rD1, 1)
	rlwimi		rW0,rW4,8,16,23
	rlwimi		rW1,rW5,8,16,23
	LAD(rW4, rD3, 2)
	LAD(rW5, rD0, 2)
	rlwimi		rW2,rW6,8,16,23
	rlwimi		rW3,rW7,8,16,23
	LAD(rW6, rD1, 2)
	LAD(rW7, rD2, 2)
	rlwimi		rW0,rW4,16,8,15
	rlwimi		rW1,rW5,16,8,15
	LAD(rW4, rD0, 3)
	LAD(rW5, rD1, 3)
	rlwimi		rW2,rW6,16,8,15
	lwz		rD0,32(rKP)
	rlwimi		rW3,rW7,16,8,15
	lwz		rD1,36(rKP)
	LAD(rW6, rD2, 3)
	LAD(rW7, rD3, 3)
	rlwimi		rW0,rW4,24,0,7
	lwz		rD2,40(rKP)
	rlwimi		rW1,rW5,24,0,7
	lwz		rD3,44(rKP)
	rlwimi		rW2,rW6,24,0,7
	rlwimi		rW3,rW7,24,0,7
	blr
