/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef	_SBCHIPC_H
#define	_SBCHIPC_H

#include "defs.h"		/* for PAD macro */

struct chipcregs {
	u32 chipid;		/* 0x0 */
	u32 capabilities;
	u32 corecontrol;	/* corerev >= 1 */
	u32 bist;

	/* OTP */
	u32 otpstatus;	/* 0x10, corerev >= 10 */
	u32 otpcontrol;
	u32 otpprog;
	u32 otplayout;	/* corerev >= 23 */

	/* Interrupt control */
	u32 intstatus;	/* 0x20 */
	u32 intmask;

	/* Chip specific regs */
	u32 chipcontrol;	/* 0x28, rev >= 11 */
	u32 chipstatus;	/* 0x2c, rev >= 11 */

	/* Jtag Master */
	u32 jtagcmd;		/* 0x30, rev >= 10 */
	u32 jtagir;
	u32 jtagdr;
	u32 jtagctrl;

	/* serial flash interface registers */
	u32 flashcontrol;	/* 0x40 */
	u32 flashaddress;
	u32 flashdata;
	u32 PAD[1];

	/* Silicon backplane configuration broadcast control */
	u32 broadcastaddress;	/* 0x50 */
	u32 broadcastdata;

	/* gpio - cleared only by power-on-reset */
	u32 gpiopullup;	/* 0x58, corerev >= 20 */
	u32 gpiopulldown;	/* 0x5c, corerev >= 20 */
	u32 gpioin;		/* 0x60 */
	u32 gpioout;		/* 0x64 */
	u32 gpioouten;	/* 0x68 */
	u32 gpiocontrol;	/* 0x6C */
	u32 gpiointpolarity;	/* 0x70 */
	u32 gpiointmask;	/* 0x74 */

	/* GPIO events corerev >= 11 */
	u32 gpioevent;
	u32 gpioeventintmask;

	/* Watchdog timer */
	u32 watchdog;	/* 0x80 */

	/* GPIO events corerev >= 11 */
	u32 gpioeventintpolarity;

	/* GPIO based LED powersave registers corerev >= 16 */
	u32 gpiotimerval;	/* 0x88 */
	u32 gpiotimeroutmask;

	/* clock control */
	u32 clockcontrol_n;	/* 0x90 */
	u32 clockcontrol_sb;	/* aka m0 */
	u32 clockcontrol_pci;	/* aka m1 */
	u32 clockcontrol_m2;	/* mii/uart/mipsref */
	u32 clockcontrol_m3;	/* cpu */
	u32 clkdiv;		/* corerev >= 3 */
	u32 gpiodebugsel;	/* corerev >= 28 */
	u32 capabilities_ext;	/* 0xac  */

	/* pll delay registers (corerev >= 4) */
	u32 pll_on_delay;	/* 0xb0 */
	u32 fref_sel_delay;
	u32 slow_clk_ctl;	/* 5 < corerev < 10 */
	u32 PAD;

	/* Instaclock registers (corerev >= 10) */
	u32 system_clk_ctl;	/* 0xc0 */
	u32 clkstatestretch;
	u32 PAD[2];

	/* Indirect backplane access (corerev >= 22) */
	u32 bp_addrlow;	/* 0xd0 */
	u32 bp_addrhigh;
	u32 bp_data;
	u32 PAD;
	u32 bp_indaccess;
	u32 PAD[3];

	/* More clock dividers (corerev >= 32) */
	u32 clkdiv2;
	u32 PAD[2];

	/* In AI chips, pointer to erom */
	u32 eromptr;		/* 0xfc */

	/* ExtBus control registers (corerev >= 3) */
	u32 pcmcia_config;	/* 0x100 */
	u32 pcmcia_memwait;
	u32 pcmcia_attrwait;
	u32 pcmcia_iowait;
	u32 ide_config;
	u32 ide_memwait;
	u32 ide_attrwait;
	u32 ide_iowait;
	u32 prog_config;
	u32 prog_waitcount;
	u32 flash_config;
	u32 flash_waitcount;
	u32 SECI_config;	/* 0x130 SECI configuration */
	u32 PAD[3];

	/* Enhanced Coexistence Interface (ECI) registers (corerev >= 21) */
	u32 eci_output;	/* 0x140 */
	u32 eci_control;
	u32 eci_inputlo;
	u32 eci_inputmi;
	u32 eci_inputhi;
	u32 eci_inputintpolaritylo;
	u32 eci_inputintpolaritymi;
	u32 eci_inputintpolarityhi;
	u32 eci_intmasklo;
	u32 eci_intmaskmi;
	u32 eci_intmaskhi;
	u32 eci_eventlo;
	u32 eci_eventmi;
	u32 eci_eventhi;
	u32 eci_eventmasklo;
	u32 eci_eventmaskmi;
	u32 eci_eventmaskhi;
	u32 PAD[3];

	/* SROM interface (corerev >= 32) */
	u32 sromcontrol;	/* 0x190 */
	u32 sromaddress;
	u32 sromdata;
	u32 PAD[17];

	/* Clock control and hardware workarounds (corerev >= 20) */
	u32 clk_ctl_st;	/* 0x1e0 */
	u32 hw_war;
	u32 PAD[70];

	/* UARTs */
	u8 uart0data;	/* 0x300 */
	u8 uart0imr;
	u8 uart0fcr;
	u8 uart0lcr;
	u8 uart0mcr;
	u8 uart0lsr;
	u8 uart0msr;
	u8 uart0scratch;
	u8 PAD[248];		/* corerev >= 1 */

	u8 uart1data;	/* 0x400 */
	u8 uart1imr;
	u8 uart1fcr;
	u8 uart1lcr;
	u8 uart1mcr;
	u8 uart1lsr;
	u8 uart1msr;
	u8 uart1scratch;
	u32 PAD[126];

	/* PMU registers (corerev >= 20) */
	u32 pmucontrol;	/* 0x600 */
	u32 pmucapabilities;
	u32 pmustatus;
	u32 res_state;
	u32 res_pending;
	u32 pmutimer;
	u32 min_res_mask;
	u32 max_res_mask;
	u32 res_table_sel;
	u32 res_dep_mask;
	u32 res_updn_timer;
	u32 res_timer;
	u32 clkstretch;
	u32 pmuwatchdog;
	u32 gpiosel;		/* 0x638, rev >= 1 */
	u32 gpioenable;	/* 0x63c, rev >= 1 */
	u32 res_req_timer_sel;
	u32 res_req_timer;
	u32 res_req_mask;
	u32 PAD;
	u32 chipcontrol_addr;	/* 0x650 */
	u32 chipcontrol_data;	/* 0x654 */
	u32 regcontrol_addr;
	u32 regcontrol_data;
	u32 pllcontrol_addr;
	u32 pllcontrol_data;
	u32 pmustrapopt;	/* 0x668, corerev >= 28 */
	u32 pmu_xtalfreq;	/* 0x66C, pmurev >= 10 */
	u32 PAD[100];
	u16 sromotp[768];
};

/* chipid */
#define	CID_ID_MASK		0x0000ffff	/* Chip Id mask */
#define	CID_REV_MASK		0x000f0000	/* Chip Revision mask */
#define	CID_REV_SHIFT		16	/* Chip Revision shift */
#define	CID_PKG_MASK		0x00f00000	/* Package Option mask */
#define	CID_PKG_SHIFT		20	/* Package Option shift */
#define	CID_CC_MASK		0x0f000000	/* CoreCount (corerev >= 4) */
#define CID_CC_SHIFT		24
#define	CID_TYPE_MASK		0xf0000000	/* Chip Type */
#define CID_TYPE_SHIFT		28

/* capabilities */
#define	CC_CAP_UARTS_MASK	0x00000003	/* Number of UARTs */
#define CC_CAP_MIPSEB		0x00000004	/* MIPS is in big-endian mode */
#define CC_CAP_UCLKSEL		0x00000018	/* UARTs clock select */
/* UARTs are driven by internal divided clock */
#define CC_CAP_UINTCLK		0x00000008
#define CC_CAP_UARTGPIO		0x00000020	/* UARTs own GPIOs 15:12 */
#define CC_CAP_EXTBUS_MASK	0x000000c0	/* External bus mask */
#define CC_CAP_EXTBUS_NONE	0x00000000	/* No ExtBus present */
#define CC_CAP_EXTBUS_FULL	0x00000040	/* ExtBus: PCMCIA, IDE & Prog */
#define CC_CAP_EXTBUS_PROG	0x00000080	/* ExtBus: ProgIf only */
#define	CC_CAP_FLASH_MASK	0x00000700	/* Type of flash */
#define	CC_CAP_PLL_MASK		0x00038000	/* Type of PLL */
#define CC_CAP_PWR_CTL		0x00040000	/* Power control */
#define CC_CAP_OTPSIZE		0x00380000	/* OTP Size (0 = none) */
#define CC_CAP_OTPSIZE_SHIFT	19	/* OTP Size shift */
#define CC_CAP_OTPSIZE_BASE	5	/* OTP Size base */
#define CC_CAP_JTAGP		0x00400000	/* JTAG Master Present */
#define CC_CAP_ROM		0x00800000	/* Internal boot rom active */
#define CC_CAP_BKPLN64		0x08000000	/* 64-bit backplane */
#define	CC_CAP_PMU		0x10000000	/* PMU Present, rev >= 20 */
#define	CC_CAP_SROM		0x40000000	/* Srom Present, rev >= 32 */
/* Nand flash present, rev >= 35 */
#define	CC_CAP_NFLASH		0x80000000

#define	CC_CAP2_SECI		0x00000001	/* SECI Present, rev >= 36 */
/* GSIO (spi/i2c) present, rev >= 37 */
#define	CC_CAP2_GSIO		0x00000002

/* pmucapabilities */
#define PCAP_REV_MASK	0x000000ff
#define PCAP_RC_MASK	0x00001f00
#define PCAP_RC_SHIFT	8
#define PCAP_TC_MASK	0x0001e000
#define PCAP_TC_SHIFT	13
#define PCAP_PC_MASK	0x001e0000
#define PCAP_PC_SHIFT	17
#define PCAP_VC_MASK	0x01e00000
#define PCAP_VC_SHIFT	21
#define PCAP_CC_MASK	0x1e000000
#define PCAP_CC_SHIFT	25
#define PCAP5_PC_MASK	0x003e0000	/* PMU corerev >= 5 */
#define PCAP5_PC_SHIFT	17
#define PCAP5_VC_MASK	0x07c00000
#define PCAP5_VC_SHIFT	22
#define PCAP5_CC_MASK	0xf8000000
#define PCAP5_CC_SHIFT	27

/*
* Maximum delay for the PMU state transition in us.
* This is an upper bound intended for spinwaits etc.
*/
#define PMU_MAX_TRANSITION_DLY	15000

#endif				/* _SBCHIPC_H */
