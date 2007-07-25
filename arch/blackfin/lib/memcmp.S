/*
 * File:         arch/blackfin/lib/memcmp.S
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

/* int memcmp(const void *s1, const void *s2, size_t n);
 * R0 = First Address (s1)
 * R1 = Second Address (s2)
 * R2 = count (n)
 *
 * Favours word aligned data.
 */

.text

.align 2

ENTRY(_memcmp)
	I1 = P3;
	P0 = R0;			/* P0 = s1 address */
	P3 = R1;			/* P3 = s2 Address  */
	P2 = R2 ;			/* P2 = count */
	CC = R2 <= 7(IU);
	IF CC JUMP .Ltoo_small;
	I0 = R1;			/* s2 */
	R1 = R1 | R0;		/* OR addresses together */
	R1 <<= 30;		/* check bottom two bits */
	CC =  AZ;			/* AZ set if zero. */
	IF !CC JUMP .Lbytes ;	/* Jump if addrs not aligned. */

	P1 = P2 >> 2;		/* count = n/4 */
	R3 =  3;
	R2 = R2 & R3;		/* remainder */
	P2 = R2;			/* set remainder */

	LSETUP (.Lquad_loop_s, .Lquad_loop_e) LC0=P1;
.Lquad_loop_s:
#if ANOMALY_05000202
	R0 = [P0++];
	R1 = [I0++];
#else
	MNOP || R0 = [P0++] || R1 = [I0++];
#endif
	CC = R0 == R1;
	IF !CC JUMP .Lquad_different;
.Lquad_loop_e:
	NOP;

	P3 = I0;			/* s2 */
.Ltoo_small:
	CC = P2 == 0;		/* Check zero count*/
	IF CC JUMP .Lfinished;	/* very unlikely*/

.Lbytes:
	LSETUP (.Lbyte_loop_s, .Lbyte_loop_e) LC0=P2;
.Lbyte_loop_s:
	R1 = B[P3++](Z);	/* *s2 */
	R0 = B[P0++](Z);	/* *s1 */
	CC = R0 == R1;
	IF !CC JUMP .Ldifferent;
.Lbyte_loop_e:
	NOP;

.Ldifferent:
	R0 = R0 - R1;
	P3 = I1;
	RTS;

.Lquad_different:
	/* We've read two quads which don't match.
	 * Can't just compare them, because we're
	 * a little-endian machine, so the MSBs of
	 * the regs occur at later addresses in the
	 * string.
	 * Arrange to re-read those two quads again,
	 * byte-by-byte.
	 */
	P0 += -4;		/* back up to the start of the */
	P3 = I0;		/* quads, and increase the*/
	P2 += 4;		/* remainder count*/
	P3 += -4;
	JUMP .Lbytes;

.Lfinished:
	R0 = 0;
	P3 = I1;
	RTS;

ENDPROC(_memcmp)
