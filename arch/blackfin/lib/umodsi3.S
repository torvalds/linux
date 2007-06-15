/*
 * File:         arch/blackfin/lib/umodsi3.S
 * Based on:
 * Author:
 *
 * Created:
 * Description:  libgcc1 routines for Blackfin 5xx
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

#ifdef CONFIG_ARITHMETIC_OPS_L1
.section .l1.text
#else
.text
#endif

.extern ___udivsi3;
.type ___udivsi3, STT_FUNC;
.globl	___umodsi3
.type ___umodsi3, STT_FUNC;
___umodsi3:

	CC=R0==0;
	IF CC JUMP .LRETURN_R0;		/* Return 0, if NR == 0 */
	CC= R1==0;
	IF CC JUMP .LRETURN_ZERO_VAL;	/* Return 0, if DR == 0 */
	CC=R0==R1;
	IF CC JUMP .LRETURN_ZERO_VAL;	/* Return 0, if NR == DR */
	CC = R1 == 1;
	IF CC JUMP .LRETURN_ZERO_VAL;	/* Return 0, if  DR == 1 */
	CC = R0<R1 (IU);
	IF CC JUMP .LRETURN_R0;		/* Return dividend (R0),IF NR<DR */

	[--SP] = (R7:6);		/* Push registers and */
	[--SP] = RETS;			/* Return address */
	R7 = R0;			/* Copy of R0 */
	R6 = R1;
	SP += -12;			/* Should always provide this space */
	CALL ___udivsi3;		/* Compute unsigned quotient using ___udiv32()*/
	SP += 12;
	R0 *= R6;			/* Quotient * divisor */
	R0 = R7 - R0;			/* Dividend - (quotient * divisor) */
	RETS = [SP++];			/* Pop return address */
	( R7:6) = [SP++];		/* And registers */
	RTS;				/* Return remainder */
.LRETURN_ZERO_VAL:
	R0 = 0;
.LRETURN_R0:
	RTS;

.size ___umodsi3, .-___umodsi3
