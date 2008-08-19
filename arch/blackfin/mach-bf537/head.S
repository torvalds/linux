/*
 * File:         arch/blackfin/mach-bf537/head.S
 * Based on:     arch/blackfin/mach-bf533/head.S
 * Author:       Jeff Dionne <jeff@uclinux.org> COPYRIGHT 1998 D. Jeff Dionne
 *
 * Created:      1998
 * Description:  Startup code for Blackfin BF537
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
#include <linux/init.h>
#include <asm/blackfin.h>
#ifdef CONFIG_BFIN_KERNEL_CLOCK
#include <asm/mach-common/clocks.h>
#include <asm/mach/mem_init.h>
#endif

.section .l1.text
#ifdef CONFIG_BFIN_KERNEL_CLOCK
ENTRY(_start_dma_code)

	/* Enable PHY CLK buffer output */
	p0.h = hi(VR_CTL);
	p0.l = lo(VR_CTL);
	r0.l = w[p0];
	bitset(r0, 14);
	w[p0] = r0.l;
	ssync;

	p0.h = hi(SIC_IWR);
	p0.l = lo(SIC_IWR);
	r0.l = 0x1;
	r0.h = 0x0;
	[p0] = r0;
	SSYNC;

	/*
	 *  Set PLL_CTL
	 *   - [14:09] = MSEL[5:0] : CLKIN / VCO multiplication factors
	 *   - [8]     = BYPASS    : BYPASS the PLL, run CLKIN into CCLK/SCLK
	 *   - [7]     = output delay (add 200ps of delay to mem signals)
	 *   - [6]     = input delay (add 200ps of input delay to mem signals)
	 *   - [5]     = PDWN      : 1=All Clocks off
	 *   - [3]     = STOPCK    : 1=Core Clock off
	 *   - [1]     = PLL_OFF   : 1=Disable Power to PLL
	 *   - [0]     = DF        : 1=Pass CLKIN/2 to PLL / 0=Pass CLKIN to PLL
	 *   all other bits set to zero
	 */

	p0.h = hi(PLL_LOCKCNT);
	p0.l = lo(PLL_LOCKCNT);
	r0 = 0x300(Z);
	w[p0] = r0.l;
	ssync;

	P2.H = hi(EBIU_SDGCTL);
	P2.L = lo(EBIU_SDGCTL);
	R0 = [P2];
	BITSET (R0, 24);
	[P2] = R0;
	SSYNC;

	r0 = CONFIG_VCO_MULT & 63;       /* Load the VCO multiplier         */
	r0 = r0 << 9;                    /* Shift it over,                  */
	r1 = CLKIN_HALF;                 /* Do we need to divide CLKIN by 2?*/
	r0 = r1 | r0;
	r1 = PLL_BYPASS;                 /* Bypass the PLL?                 */
	r1 = r1 << 8;                    /* Shift it over                   */
	r0 = r1 | r0;                    /* add them all together           */

	p0.h = hi(PLL_CTL);
	p0.l = lo(PLL_CTL);              /* Load the address                */
	cli r2;                          /* Disable interrupts              */
	ssync;
	w[p0] = r0.l;                    /* Set the value                   */
	idle;                            /* Wait for the PLL to stablize    */
	sti r2;                          /* Enable interrupts               */

.Lcheck_again:
	p0.h = hi(PLL_STAT);
	p0.l = lo(PLL_STAT);
	R0 = W[P0](Z);
	CC = BITTST(R0,5);
	if ! CC jump .Lcheck_again;

	/* Configure SCLK & CCLK Dividers */
	r0 = (CONFIG_CCLK_ACT_DIV | CONFIG_SCLK_DIV);
	p0.h = hi(PLL_DIV);
	p0.l = lo(PLL_DIV);
	w[p0] = r0.l;
	ssync;

	p0.l = lo(EBIU_SDRRC);
	p0.h = hi(EBIU_SDRRC);
	r0 = mem_SDRRC;
	w[p0] = r0.l;
	ssync;

	P2.H = hi(EBIU_SDGCTL);
	P2.L = lo(EBIU_SDGCTL);
	R0 = [P2];
	BITCLR (R0, 24);
	p0.h = hi(EBIU_SDSTAT);
	p0.l = lo(EBIU_SDSTAT);
	r2.l = w[p0];
	cc = bittst(r2,3);
	if !cc jump .Lskip;
	NOP;
	BITSET (R0, 23);
.Lskip:
	[P2] = R0;
	SSYNC;

	R0.L = lo(mem_SDGCTL);
	R0.H = hi(mem_SDGCTL);
	R1 = [p2];
	R1 = R1 | R0;
	[P2] = R1;
	SSYNC;

	RTS;
ENDPROC(_start_dma_code)
#endif /* CONFIG_BFIN_KERNEL_CLOCK */
