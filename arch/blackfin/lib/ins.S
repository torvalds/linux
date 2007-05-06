/*
 * File:         arch/blackfin/lib/ins.S
 * Based on:
 * Author:       Bas Vermeulen <bas@buyways.nl>
 *
 * Created:      Tue Mar 22 15:27:24 CEST 2005
 * Description:  Implementation of ins{bwl} for BlackFin processors using zero overhead loops.
 *
 * Modified:
 *               Copyright 2004-2006 Analog Devices Inc.
 *               Copyright (C) 2005 Bas Vermeulen, BuyWays BV <bas@buyways.nl>
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

ENTRY(_insl)
	P0 = R0;	/* P0 = port */
	cli R3;
	P1 = R1;	/* P1 = address */
	P2 = R2;	/* P2 = count */
	SSYNC;
	LSETUP( .Llong_loop_s, .Llong_loop_e) LC0 = P2;
.Llong_loop_s: R0 = [P0];
.Llong_loop_e: [P1++] = R0;
	sti R3;
	RTS;

ENTRY(_insw)
	P0 = R0;	/* P0 = port */
	cli R3;
	P1 = R1;	/* P1 = address */
	P2 = R2;	/* P2 = count */
	SSYNC;
	LSETUP( .Lword_loop_s, .Lword_loop_e) LC0 = P2;
.Lword_loop_s: R0 = W[P0];
.Lword_loop_e: W[P1++] = R0;
	sti R3;
	RTS;

ENTRY(_insb)
	P0 = R0;	/* P0 = port */
	cli R3;
	P1 = R1;	/* P1 = address */
	P2 = R2;	/* P2 = count */
	SSYNC;
	LSETUP( .Lbyte_loop_s, .Lbyte_loop_e) LC0 = P2;
.Lbyte_loop_s: R0 = B[P0];
.Lbyte_loop_e: B[P1++] = R0;
	sti R3;
	RTS;
