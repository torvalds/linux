/*
 * File:         arch/blackfin/lib/memmove.S
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

/*
 * C Library function MEMMOVE
 * R0 = To Address (leave unchanged to form result)
 * R1 = From Address
 * R2 = count
 * Data may overlap
 */

ENTRY(_memmove)
	I1 = P3;
	P0 = R0;                  /* P0 = To address */
	P3 = R1;                  /* P3 = From Address */
	P2 = R2;                  /* P2 = count */
	CC = P2 == 0;             /* Check zero count*/
	IF CC JUMP .Lfinished;    /* very unlikely */

	CC = R1 < R0 (IU);        /* From < To */
	IF !CC JUMP .Lno_overlap;
	R3 = R1 + R2;
	CC = R0 <= R3 (IU);       /* (From+len) >= To */
	IF CC JUMP .Loverlap;
.Lno_overlap:
	R3 = 11;
	CC = R2 <= R3;
	IF CC JUMP .Lbytes;
	R3 = R1 | R0;             /* OR addresses together */
	R3 <<= 30;                /* check bottom two bits */
	CC =  AZ;                 /* AZ set if zero.*/
	IF !CC JUMP .Lbytes;      /* Jump if addrs not aligned.*/

	I0 = P3;
	P1 = P2 >> 2;             /* count = n/4 */
	P1 += -1;
	R3 =  3;
	R2 = R2 & R3;             /* remainder */
	P2 = R2;                  /* set remainder */
	R1 = [I0++];

	LSETUP (.Lquad_loop, .Lquad_loop) LC0=P1;
.Lquad_loop: MNOP || [P0++] = R1 || R1 = [I0++];
	[P0++] = R1;

	CC = P2 == 0;             /* any remaining bytes? */
	P3 = I0;                  /* Ammend P3 to updated ptr. */
	IF !CC JUMP .Lbytes;
	P3 = I1;
	RTS;

.Lbytes:     LSETUP (.Lbyte2_s, .Lbyte2_e) LC0=P2;
.Lbyte2_s:   R1 = B[P3++](Z);
.Lbyte2_e:   B[P0++] = R1;

.Lfinished:  P3 = I1;
	RTS;

.Loverlap:
	P2 += -1;
	P0 = P0 + P2;
	P3 = P3 + P2;
	R1 = B[P3--] (Z);
	CC = P2 == 0;
	IF CC JUMP .Lno_loop;
	LSETUP (.Lol_s, .Lol_e) LC0 = P2;
.Lol_s:    B[P0--] = R1;
.Lol_e:    R1 = B[P3--] (Z);
.Lno_loop: B[P0] = R1;
	P3 = I1;
	RTS;

ENDPROC(_memmove)
