/*
 * File:         arch/blackfin/mach-common/dpmc.S
 * Based on:
 * Author:       LG Soft India
 *
 * Created:      ?
 * Description:  Watchdog Timer APIs
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
#include <asm/mach/irq.h>


.section .l1.text

ENTRY(_sleep_mode)
	[--SP] = ( R7:0, P5:0 );
	[--SP] =  RETS;

	call _set_sic_iwr;

	R0 = 0xFFFF (Z);
	call _set_rtc_istat;

	P0.H = hi(PLL_CTL);
	P0.L = lo(PLL_CTL);
	R1 = W[P0](z);
	BITSET (R1, 3);
	W[P0] = R1.L;

	CLI R2;
	SSYNC;
	IDLE;
	STI R2;

	call _test_pll_locked;

	R0 = IWR_ENABLE(0);
	R1 = IWR_DISABLE_ALL;
	R2 = IWR_DISABLE_ALL;

	call _set_sic_iwr;

	P0.H = hi(PLL_CTL);
	P0.L = lo(PLL_CTL);
	R7 = w[p0](z);
	BITCLR (R7, 3);
	BITCLR (R7, 5);
	w[p0] = R7.L;
	IDLE;
	call _test_pll_locked;

	RETS = [SP++];
	( R7:0, P5:0 ) = [SP++];
	RTS;

ENTRY(_hibernate_mode)
	[--SP] = ( R7:0, P5:0 );
	[--SP] =  RETS;

	call _set_sic_iwr;

	R0 = 0xFFFF (Z);
	call _set_rtc_istat;

	P0.H = hi(VR_CTL);
	P0.L = lo(VR_CTL);
	R1 = W[P0](z);
	BITSET (R1, 8);
	BITCLR (R1, 0);
	BITCLR (R1, 1);
	W[P0] = R1.L;
	SSYNC;

	CLI R2;
	IDLE;

	/* Actually, adding anything may not be necessary...SDRAM contents
	 * are lost
	 */

ENTRY(_deep_sleep)
	[--SP] = ( R7:0, P5:0 );
	[--SP] =  RETS;

	CLI R4;

	R0 = IWR_ENABLE(0);
	R1 = IWR_DISABLE_ALL;
	R2 = IWR_DISABLE_ALL;

	call _set_sic_iwr;

	call _set_dram_srfs;

	/* Clear all the interrupts,bits sticky */
	R0 = 0xFFFF (Z);
	call _set_rtc_istat

	P0.H = hi(PLL_CTL);
	P0.L = lo(PLL_CTL);
	R0 = W[P0](z);
	BITSET (R0, 5);
	W[P0] = R0.L;

	call _test_pll_locked;

	SSYNC;
	IDLE;

	call _unset_dram_srfs;

	call _test_pll_locked;

	R0 = IWR_ENABLE(0);
	R1 = IWR_DISABLE_ALL;
	R2 = IWR_DISABLE_ALL;

	call _set_sic_iwr;

	P0.H = hi(PLL_CTL);
	P0.L = lo(PLL_CTL);
	R0 = w[p0](z);
	BITCLR (R0, 3);
	BITCLR (R0, 5);
	BITCLR (R0, 8);
	w[p0] = R0;
	IDLE;
	call _test_pll_locked;

	STI R4;

	RETS = [SP++];
	( R7:0, P5:0 ) = [SP++];
	RTS;

ENTRY(_sleep_deeper)
	[--SP] = ( R7:0, P5:0 );
	[--SP] =  RETS;

	CLI R4;

	P3 = R0;
	P4 = R1;
	P5 = R2;

	R0 = IWR_ENABLE(0);
	R1 = IWR_DISABLE_ALL;
	R2 = IWR_DISABLE_ALL;

	call _set_sic_iwr;
	call _set_dram_srfs;	/* Set SDRAM Self Refresh */

	/* Clear all the interrupts,bits sticky */
	R0 = 0xFFFF (Z);
	call _set_rtc_istat;
	P0.H = hi(PLL_DIV);
	P0.L = lo(PLL_DIV);
	R6 = W[P0](z);
	R0.L = 0xF;
	W[P0] = R0.l;		/* Set Max VCO to SCLK divider */

	P0.H = hi(PLL_CTL);
	P0.L = lo(PLL_CTL);
	R5 = W[P0](z);
	R0.L = (CONFIG_MIN_VCO_HZ/CONFIG_CLKIN_HZ) << 9;
	W[P0] = R0.l;		/* Set Min CLKIN to VCO multiplier */

	SSYNC;
	IDLE;

	call _test_pll_locked;

	P0.H = hi(VR_CTL);
	P0.L = lo(VR_CTL);
	R7 = W[P0](z);
	R1 = 0x6;
	R1 <<= 16;
	R2 = 0x0404(Z);
	R1 = R1|R2;

	R2 = DEPOSIT(R7, R1);
	W[P0] = R2;		/* Set Min Core Voltage */

	SSYNC;
	IDLE;

	call _test_pll_locked;

	R0 = P3;
	R1 = P4;
	R3 = P5;
	call _set_sic_iwr;	/* Set Awake from IDLE */

	P0.H = hi(PLL_CTL);
	P0.L = lo(PLL_CTL);
	R0 = W[P0](z);
	BITSET (R0, 3);
	W[P0] = R0.L;		/* Turn CCLK OFF */
	SSYNC;
	IDLE;

	call _test_pll_locked;

	R0 = IWR_ENABLE(0);
	R1 = IWR_DISABLE_ALL;
	R2 = IWR_DISABLE_ALL;

	call _set_sic_iwr;	/* Set Awake from IDLE PLL */

	P0.H = hi(VR_CTL);
	P0.L = lo(VR_CTL);
	W[P0]= R7;

	SSYNC;
	IDLE;

	call _test_pll_locked;

	P0.H = hi(PLL_DIV);
	P0.L = lo(PLL_DIV);
	W[P0]= R6;		/* Restore CCLK and SCLK divider */

	P0.H = hi(PLL_CTL);
	P0.L = lo(PLL_CTL);
	w[p0] = R5;		/* Restore VCO multiplier */
	IDLE;
	call _test_pll_locked;

	call _unset_dram_srfs;	/* SDRAM Self Refresh Off */

	STI R4;

	RETS = [SP++];
	( R7:0, P5:0 ) = [SP++];
	RTS;

ENTRY(_set_dram_srfs)
	/*  set the dram to self refresh mode */
#if defined(CONFIG_BF54x)
	P0.H = hi(EBIU_RSTCTL);
	P0.L = lo(EBIU_RSTCTL);
	R2 = [P0];
	R3.H = hi(SRREQ);
	R3.L = lo(SRREQ);
#else
	P0.H = hi(EBIU_SDGCTL);
	P0.L = lo(EBIU_SDGCTL);
	R2 = [P0];
	R3.H = hi(SRFS);
	R3.L = lo(SRFS);
#endif
	R2 = R2|R3;
	[P0] = R2;
	ssync;
#if defined(CONFIG_BF54x)
.LSRR_MODE:
	R2 = [P0];
	CC = BITTST(R2, 4);
	if !CC JUMP .LSRR_MODE;
#endif
	RTS;

ENTRY(_unset_dram_srfs)
	/*  set the dram out of self refresh mode */
#if defined(CONFIG_BF54x)
	P0.H = hi(EBIU_RSTCTL);
	P0.L = lo(EBIU_RSTCTL);
	R2 = [P0];
	R3.H = hi(SRREQ);
	R3.L = lo(SRREQ);
#else
	P0.H = hi(EBIU_SDGCTL);
	P0.L = lo(EBIU_SDGCTL);
	R2 = [P0];
	R3.H = hi(SRFS);
	R3.L = lo(SRFS);
#endif
	R3 = ~R3;
	R2 = R2&R3;
	[P0] = R2;
	ssync;
	RTS;

ENTRY(_set_sic_iwr)
#if defined(CONFIG_BF54x) || defined(CONFIG_BF52x)  || defined(CONFIG_BF561)
	P0.H = hi(SIC_IWR0);
	P0.L = lo(SIC_IWR0);
	P1.H = hi(SIC_IWR1);
	P1.L = lo(SIC_IWR1);
	[P1] = R1;
#if defined(CONFIG_BF54x)
	P1.H = hi(SIC_IWR2);
	P1.L = lo(SIC_IWR2);
	[P1] = R2;
#endif
#else
	P0.H = hi(SIC_IWR);
	P0.L = lo(SIC_IWR);
#endif
	[P0] = R0;

	SSYNC;
	RTS;

ENTRY(_set_rtc_istat)
#ifndef CONFIG_BF561
	P0.H = hi(RTC_ISTAT);
	P0.L = lo(RTC_ISTAT);
	w[P0] = R0.L;
	SSYNC;
#endif
	RTS;

ENTRY(_test_pll_locked)
	P0.H = hi(PLL_STAT);
	P0.L = lo(PLL_STAT);
1:
	R0 = W[P0] (Z);
	CC = BITTST(R0,5);
	IF !CC JUMP 1b;
	RTS;
