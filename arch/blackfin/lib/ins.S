/*
 * File:         arch/blackfin/lib/ins.S
 * Based on:
 * Author:       Bas Vermeulen <bas@buyways.nl>
 *
 * Created:      Tue Mar 22 15:27:24 CEST 2005
 * Description:  Implementation of ins{bwl} for BlackFin processors using zero overhead loops.
 *
 * Modified:
 *               Copyright 2004-2008 Analog Devices Inc.
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
#include <asm/blackfin.h>

.align 2

/*
 * Reads on the Blackfin are speculative. In Blackfin terms, this means they
 * can be interrupted at any time (even after they have been issued on to the
 * external bus), and re-issued after the interrupt occurs.
 *
 * If a FIFO is sitting on the end of the read, it will see two reads,
 * when the core only sees one. The FIFO receives the read which is cancelled,
 * and not delivered to the core.
 *
 * To solve this, interrupts are turned off before reads occur to I/O space.
 * There are 3 versions of all these functions
 *  - turns interrupts off every read (higher overhead, but lower latency)
 *  - turns interrupts off every loop (low overhead, but longer latency)
 *  - DMA version, which do not suffer from this issue. DMA versions have
 *      different name (prefixed by dma_ ), and are located in
 *      ../kernel/bfin_dma_5xx.c
 * Using the dma related functions are recommended for transfering large
 * buffers in/out of FIFOs.
 */

ENTRY(_insl)
#ifdef CONFIG_BFIN_INS_LOWOVERHEAD
	P0 = R0;	/* P0 = port */
	cli R3;
	P1 = R1;	/* P1 = address */
	P2 = R2;	/* P2 = count */
	SSYNC;
	LSETUP( .Llong_loop_s, .Llong_loop_e) LC0 = P2;
.Llong_loop_s:  R0 = [P0];
		[P1++] = R0;
		NOP;
.Llong_loop_e: 	NOP;
	sti R3;
	RTS;
#else
	P0 = R0;	/* P0 = port */
	P1 = R1;	/* P1 = address */
	P2 = R2;	/* P2 = count */
	SSYNC;
	LSETUP( .Llong_loop_s, .Llong_loop_e) LC0 = P2;
.Llong_loop_s:
	CLI R3;
	NOP; NOP; NOP;
	R0 = [P0];
	[P1++] = R0;
.Llong_loop_e:
	STI R3;

	RTS;
#endif
ENDPROC(_insl)

ENTRY(_insw)
#ifdef CONFIG_BFIN_INS_LOWOVERHEAD
	P0 = R0;	/* P0 = port */
	cli R3;
	P1 = R1;	/* P1 = address */
	P2 = R2;	/* P2 = count */
	SSYNC;
	LSETUP( .Lword_loop_s, .Lword_loop_e) LC0 = P2;
.Lword_loop_s:  R0 = W[P0];
		W[P1++] = R0;
		NOP;
.Lword_loop_e: 	NOP;
	sti R3;
	RTS;
#else
	P0 = R0;	/* P0 = port */
	P1 = R1;	/* P1 = address */
	P2 = R2;	/* P2 = count */
	SSYNC;
	LSETUP( .Lword_loop_s, .Lword_loop_e) LC0 = P2;
.Lword_loop_s:
	CLI R3;
	NOP; NOP; NOP;
	R0 = W[P0];
	W[P1++] = R0;
.Lword_loop_e:
	STI R3;
	RTS;

#endif
ENDPROC(_insw)

ENTRY(_insw_8)
#ifdef CONFIG_BFIN_INS_LOWOVERHEAD
	P0 = R0;	/* P0 = port */
	cli R3;
	P1 = R1;	/* P1 = address */
	P2 = R2;	/* P2 = count */
	SSYNC;
	LSETUP( .Lword8_loop_s, .Lword8_loop_e) LC0 = P2;
.Lword8_loop_s:  R0 = W[P0];
		B[P1++] = R0;
		R0 = R0 >> 8;
		B[P1++] = R0;
		NOP;
.Lword8_loop_e: NOP;
	sti R3;
	RTS;
#else
	P0 = R0;	/* P0 = port */
	P1 = R1;	/* P1 = address */
	P2 = R2;	/* P2 = count */
	SSYNC;
	LSETUP( .Lword8_loop_s, .Lword8_loop_e) LC0 = P2;
.Lword8_loop_s:
	CLI R3;
	NOP; NOP; NOP;
	R0 = W[P0];
	B[P1++] = R0;
	R0 = R0 >> 8;
	B[P1++] = R0;
	NOP;
.Lword8_loop_e:
	STI R3;

	RTS;
#endif
ENDPROC(_insw_8)

ENTRY(_insb)
#ifdef CONFIG_BFIN_INS_LOWOVERHEAD
	P0 = R0;	/* P0 = port */
	cli R3;
	P1 = R1;	/* P1 = address */
	P2 = R2;	/* P2 = count */
	SSYNC;
	LSETUP( .Lbyte_loop_s, .Lbyte_loop_e) LC0 = P2;
.Lbyte_loop_s:  R0 = B[P0];
		B[P1++] = R0;
		NOP;
.Lbyte_loop_e:  NOP;
	sti R3;
	RTS;
#else
	P0 = R0;        /* P0 = port */
	P1 = R1;        /* P1 = address */
	P2 = R2;        /* P2 = count */
	SSYNC;
	LSETUP( .Lbyte_loop_s, .Lbyte_loop_e) LC0 = P2;
.Lbyte_loop_s:
	CLI R3;
	NOP; NOP; NOP;
	R0 = B[P0];
	B[P1++] = R0;
.Lbyte_loop_e:
	STI R3;

	RTS;
#endif
ENDPROC(_insb)

ENTRY(_insl_16)
#ifdef CONFIG_BFIN_INS_LOWOVERHEAD
	P0 = R0;	/* P0 = port */
	cli R3;
	P1 = R1;	/* P1 = address */
	P2 = R2;	/* P2 = count */
	SSYNC;
	LSETUP( .Llong16_loop_s, .Llong16_loop_e) LC0 = P2;
.Llong16_loop_s:  R0 = [P0];
		  W[P1++] = R0;
		  R0 = R0 >> 16;
		  W[P1++] = R0;
		  NOP;
.Llong16_loop_e:  NOP;
	sti R3;
	RTS;
#else
	P0 = R0;	/* P0 = port */
	P1 = R1;	/* P1 = address */
	P2 = R2;	/* P2 = count */
	SSYNC;
	LSETUP( .Llong16_loop_s, .Llong16_loop_e) LC0 = P2;
.Llong16_loop_s:
	CLI R3;
	NOP; NOP; NOP;
	R0 = [P0];
	W[P1++] = R0;
	R0 = R0 >> 16;
	W[P1++] = R0;
.Llong16_loop_e:
	STI R3;
	RTS;
#endif
ENDPROC(_insl_16)
