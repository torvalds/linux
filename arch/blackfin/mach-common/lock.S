/*
 * File:         arch/blackfin/mach-common/lock.S
 * Based on:
 * Author:       LG Soft India
 *
 * Created:      ?
 * Description:  kernel locks
 *
 * Modified:
 *               Copyright 2004-2006 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/linkage.h>
#include <asm/blackfin.h>

.text

/* When you come here, it is assumed that
 * R0 - Which way to be locked
 */

ENTRY(_cache_grab_lock)

	[--SP]=( R7:0,P5:0 );

	P1.H = HI(IMEM_CONTROL);
	P1.L = LO(IMEM_CONTROL);
	P5.H = HI(ICPLB_ADDR0);
	P5.L = LO(ICPLB_ADDR0);
	P4.H = HI(ICPLB_DATA0);
	P4.L = LO(ICPLB_DATA0);
	R7 = R0;

	/* If the code of interest already resides in the cache
	 * invalidate the entire cache itself.
	 * invalidate_entire_icache;
	 */

	SP += -12;
	[--SP] = RETS;
	CALL _invalidate_entire_icache;
	RETS = [SP++];
	SP += 12;

	/* Disable the Interrupts*/

	CLI R3;

.LLOCK_WAY:

	/* Way0 - 0xFFA133E0
	 * Way1 - 0xFFA137E0
	 * Way2 - 0xFFA13BE0	Total Way Size = 4K
	 * Way3 - 0xFFA13FE0
	 */

	/* Procedure Ex. -Set the locks for other ways by setting ILOC[3:1]
	 * Only Way0 of the instruction cache can now be
	 * replaced by a new code
	 */

	R5 = R7;
	CC = BITTST(R7,0);
	IF CC JUMP .LCLEAR1;
	R7 = 0;
	BITSET(R7,0);
	JUMP .LDONE1;

.LCLEAR1:
	R7 = 0;
	BITCLR(R7,0);
.LDONE1:	R4 = R7 << 3;
	R7 = [P1];
	R7 = R7 | R4;
	SSYNC;		/* SSYNC required writing to IMEM_CONTROL. */
	.align 8;
	[P1] = R7;
	SSYNC;

	R7 = R5;
	CC = BITTST(R7,1);
	IF CC JUMP .LCLEAR2;
	R7 = 0;
	BITSET(R7,1);
	JUMP .LDONE2;

.LCLEAR2:
	R7 = 0;
	BITCLR(R7,1);
.LDONE2:	R4 = R7 << 3;
	R7 = [P1];
	R7 = R7 | R4;
	SSYNC;		/* SSYNC required writing to IMEM_CONTROL. */
	.align 8;
	[P1] = R7;
	SSYNC;

	R7 = R5;
	CC = BITTST(R7,2);
	IF CC JUMP .LCLEAR3;
	R7 = 0;
	BITSET(R7,2);
	JUMP .LDONE3;
.LCLEAR3:
	R7 = 0;
	BITCLR(R7,2);
.LDONE3:	R4 = R7 << 3;
	R7 = [P1];
	R7 = R7 | R4;
	SSYNC;		/* SSYNC required writing to IMEM_CONTROL. */
	.align 8;
	[P1] = R7;
	SSYNC;


	R7 = R5;
	CC = BITTST(R7,3);
	IF CC JUMP .LCLEAR4;
	R7 = 0;
	BITSET(R7,3);
	JUMP .LDONE4;
.LCLEAR4:
	R7 = 0;
	BITCLR(R7,3);
.LDONE4:	R4 = R7 << 3;
	R7 = [P1];
	R7 = R7 | R4;
	SSYNC;		/* SSYNC required writing to IMEM_CONTROL. */
	.align 8;
	[P1] = R7;
	SSYNC;

	STI R3;

	( R7:0,P5:0 ) = [SP++];

	RTS;
ENDPROC(_cache_grab_lock)

/* After the execution of critical code, the code is now locked into
 * the cache way. Now we need to set ILOC.
 *
 * R0 - Which way to be locked
 */

ENTRY(_bfin_cache_lock)

	[--SP]=( R7:0,P5:0 );

	P1.H = HI(IMEM_CONTROL);
	P1.L = LO(IMEM_CONTROL);

	/* Disable the Interrupts*/
	CLI R3;

	R7 = [P1];
	R2 = ~(0x78) (X);	/* mask out ILOC */
	R7 = R7 & R2;
	R0 = R0 << 3;
	R7 = R0 | R7;
	SSYNC;		/* SSYNC required writing to IMEM_CONTROL. */
	.align 8;
	[P1] = R7;
	SSYNC;
	/* Renable the Interrupts */
	STI R3;

	( R7:0,P5:0 ) = [SP++];
	RTS;
ENDPROC(_bfin_cache_lock)

/* Invalidate the Entire Instruction cache by
 * disabling IMC bit
 */
ENTRY(_invalidate_entire_icache)
	[--SP] = ( R7:5);

	P0.L = LO(IMEM_CONTROL);
	P0.H = HI(IMEM_CONTROL);
	R7 = [P0];

	/* Clear the IMC bit , All valid bits in the instruction
	 * cache are set to the invalid state
	 */
	BITCLR(R7,IMC_P);
	CLI R6;
	SSYNC;		/* SSYNC required before invalidating cache. */
	.align 8;
	[P0] = R7;
	SSYNC;
	STI R6;

	/* Configures the instruction cache agian */
	R6 = (IMC | ENICPLB);
	R7 = R7 | R6;

	CLI R6;
	SSYNC;		/* SSYNC required before writing to IMEM_CONTROL. */
	.align 8;
	[P0] = R7;
	SSYNC;
	STI R6;

	( R7:5) = [SP++];
	RTS;
ENDPROC(_invalidate_entire_icache)
