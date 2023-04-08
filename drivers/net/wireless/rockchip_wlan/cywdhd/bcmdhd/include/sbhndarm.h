/*
 * Broadcom SiliconBackplane ARM definitions
 *
 * Portions of this code are copyright (c) 2022 Cypress Semiconductor Corporation
 *
 * Copyright (C) 1999-2017, Broadcom Corporation
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: sbhndarm.h 699160 2017-05-12 04:50:25Z $
 */

#ifndef	_sbhndarm_h_
#define	_sbhndarm_h_

#ifndef _LANGUAGE_ASSEMBLY

/* cpp contortions to concatenate w/arg prescan */
#ifndef PAD
#define	_PADLINE(line)	pad ## line
#define	_XSTR(line)	_PADLINE(line)
#define	PAD		_XSTR(__LINE__)
#endif	/* PAD */

/* cortex-m3 */
typedef volatile struct {
	uint32	corecontrol;	/* 0x0 */
	uint32	corestatus;	/* 0x4 */
	uint32	PAD[1];
	uint32	biststatus;	/* 0xc */
	uint32	nmiisrst;	/* 0x10 */
	uint32	nmimask;	/* 0x14 */
	uint32	isrmask;	/* 0x18 */
	uint32	PAD[1];
	uint32	resetlog;	/* 0x20 */
	uint32	gpioselect;	/* 0x24 */
	uint32	gpioenable;	/* 0x28 */
	uint32	PAD[1];
	uint32	bpaddrlo;	/* 0x30 */
	uint32	bpaddrhi;	/* 0x34 */
	uint32	bpdata;		/* 0x38 */
	uint32	bpindaccess;	/* 0x3c */
	uint32	ovlidx;		/* 0x40 */
	uint32	ovlmatch;	/* 0x44 */
	uint32	ovladdr;	/* 0x48 */
	uint32	PAD[13];
	uint32	bwalloc;	/* 0x80 */
	uint32	PAD[3];
	uint32	cyclecnt;	/* 0x90 */
	uint32	inttimer;	/* 0x94 */
	uint32	intmask;	/* 0x98 */
	uint32	intstatus;	/* 0x9c */
	uint32	PAD[80];
	uint32	clk_ctl_st;	/* 0x1e0 */
	uint32  PAD[1];
	uint32  powerctl;	/* 0x1e8 */
} cm3regs_t;
#define ARM_CM3_REG(regs, reg)	(&((cm3regs_t *)regs)->reg)

/* cortex-R4 */
typedef volatile struct {
	uint32	corecontrol;	/* 0x0 */
	uint32	corecapabilities; /* 0x4 */
	uint32	corestatus;	/* 0x8 */
	uint32	biststatus;	/* 0xc */
	uint32	nmiisrst;	/* 0x10 */
	uint32	nmimask;	/* 0x14 */
	uint32	isrmask;	/* 0x18 */
	uint32	swintreg;	/* 0x1C */
	uint32	intstatus;	/* 0x20 */
	uint32	intmask;	/* 0x24 */
	uint32	cyclecnt;	/* 0x28 */
	uint32	inttimer;	/* 0x2c */
	uint32	gpioselect;	/* 0x30 */
	uint32	gpioenable;	/* 0x34 */
	uint32	PAD[2];
	uint32	bankidx;	/* 0x40 */
	uint32	bankinfo;	/* 0x44 */
	uint32	bankstbyctl;	/* 0x48 */
	uint32	bankpda;	/* 0x4c */
	uint32	PAD[6];
	uint32	tcampatchctrl;	/* 0x68 */
	uint32	tcampatchtblbaseaddr;	/* 0x6c */
	uint32	tcamcmdreg;	/* 0x70 */
	uint32	tcamdatareg;	/* 0x74 */
	uint32	tcambankxmaskreg;	/* 0x78 */
	uint32	PAD[89];
	uint32	clk_ctl_st;	/* 0x1e0 */
	uint32  PAD[1];
	uint32  powerctl;	/* 0x1e8 */
} cr4regs_t;
#define ARM_CR4_REG(regs, reg)	(&((cr4regs_t *)regs)->reg)

/* cortex-A7 */
typedef volatile struct {
	uint32	corecontrol;	/* 0x0 */
	uint32	corecapabilities; /* 0x4 */
	uint32	corestatus;	/* 0x8 */
	uint32	tracecontrol;	/* 0xc */
	uint32	PAD[8];
	uint32	gpioselect;	/* 0x30 */
	uint32	gpioenable;	/* 0x34 */
	uint32	PAD[106];
	uint32	clk_ctl_st;	/* 0x1e0 */
	uint32  PAD[1];
	uint32  powerctl;	/* 0x1e8 */
} ca7regs_t;
#define ARM_CA7_REG(regs, reg)	(&((ca7regs_t *)regs)->reg)

#if defined(__ARM_ARCH_7M__)
#define ARMREG(regs, reg)	ARM_CM3_REG(regs, reg)
#endif	/* __ARM_ARCH_7M__ */

#if defined(__ARM_ARCH_7R__)
#define ARMREG(regs, reg)	ARM_CR4_REG(regs, reg)
#endif	/* __ARM_ARCH_7R__ */

#if defined(__ARM_ARCH_7A__)
#define ARMREG(regs, reg)	ARM_CA7_REG(regs, reg)
#endif	/* __ARM_ARCH_7A__ */

#endif	/* _LANGUAGE_ASSEMBLY */

#endif	/* _sbhndarm_h_ */
