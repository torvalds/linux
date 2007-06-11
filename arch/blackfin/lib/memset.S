/*
 * File:         arch/blackfin/lib/memset.S
 * Based on:
 * Author:
 *
 * Created:
 * Description:
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

.align 2

#ifdef CONFIG_MEMSET_L1
.section .l1.text
#else
.text
#endif

/*
 * C Library function MEMSET
 * R0 = address (leave unchanged to form result)
 * R1 = filler byte
 * R2 = count
 * Favours word aligned data.
 */

ENTRY(_memset)
	P0 = R0 ;              /* P0 = address */
	P2 = R2 ;              /* P2 = count   */
	R3 = R0 + R2;          /* end          */
	CC = R2 <= 7(IU);
	IF CC JUMP  .Ltoo_small;
	R1 = R1.B (Z);         /* R1 = fill char */
	R2 =  3;
	R2 = R0 & R2;          /* addr bottom two bits */
	CC =  R2 == 0;             /* AZ set if zero.	*/
	IF !CC JUMP  .Lforce_align ;  /* Jump if addr not aligned. */

.Laligned:
	P1 = P2 >> 2;          /* count = n/4        */
	R2 = R1 <<  8;         /* create quad filler */
	R2.L = R2.L + R1.L(NS);
	R2.H = R2.L + R1.H(NS);
	P2 = R3;

	LSETUP (.Lquad_loop , .Lquad_loop) LC0=P1;
.Lquad_loop:
	[P0++] = R2;

	CC = P0 == P2;
	IF !CC JUMP .Lbytes_left;
	RTS;

.Lbytes_left:
	R2 = R3;                /* end point */
	R3 = P0;                /* current position */
	R2 = R2 - R3;           /* bytes left */
	P2 = R2;

.Ltoo_small:
	CC = P2 == 0;           /* Check zero count */
	IF CC JUMP .Lfinished;    /* Unusual */

.Lbytes:
	LSETUP (.Lbyte_loop , .Lbyte_loop) LC0=P2;
.Lbyte_loop:
	B[P0++] = R1;

.Lfinished:
	RTS;

.Lforce_align:
	CC = BITTST (R0, 0);  /* odd byte */
	R0 = 4;
	R0 = R0 - R2;
	P1 = R0;
	R0 = P0;		    /* Recover return address */
	IF !CC JUMP .Lskip1;
	B[P0++] = R1;
.Lskip1:
	CC = R2 <= 2;          /* 2 bytes */
	P2 -= P1;              /* reduce count */
	IF !CC JUMP .Laligned;
	B[P0++] = R1;
	B[P0++] = R1;
	JUMP .Laligned;

ENDPROC(_memset)
