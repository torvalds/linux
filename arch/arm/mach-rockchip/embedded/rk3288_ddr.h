/*
 * Copyright (c) 2014, Fuzhou Rockchip Electronics Co., Ltd
 * Author: Chris Zhong <zyw@rock-chips.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef __MACH_ROCKCHIP_RK3288_DDR_H
#define __MACH_ROCKCHIP_RK3288_DDR_H

/* DDR pctl register */
#define DDR_PCTL_SCFG			0x0000
#define DDR_PCTL_SCTL			0x0004
#define DDR_PCTL_STAT			0x0008
#define DDR_PCTL_MCMD			0x0040
#define DDR_PCTL_POWCTL			0x0044
#define DDR_PCTL_POWSTAT		0x0048
#define DDR_PCTL_CMDTSTATEN		0x0050
#define DDR_PCTL_MRRCFG0		0x0060
#define DDR_PCTL_MRRSTAT0		0x0064
#define DDR_PCTL_MRRSTAT1		0x0068
#define DDR_PCTL_MCFG1			0x007c
#define DDR_PCTL_MCFG			0x0080
#define DDR_PCTL_PPCFG			0x0084
#define DDR_PCTL_TOGCNT1U		0x00c0
#define DDR_PCTL_TINIT			0x00c4
#define DDR_PCTL_TRSTH			0x00c8
#define DDR_PCTL_TOGCNT100N		0x00cc
#define DDR_PCTL_TREFI			0x00d0
#define DDR_PCTL_TMRD			0x00d4
#define DDR_PCTL_TRFC			0x00d8
#define DDR_PCTL_TRP			0x00dc
#define DDR_PCTL_TRTW			0x00e0
#define DDR_PCTL_TAL			0x00e4
#define DDR_PCTL_TCL			0x00e8
#define DDR_PCTL_TCWL			0x00ec
#define DDR_PCTL_TRAS			0x00f0
#define DDR_PCTL_TRC			0x00f4
#define DDR_PCTL_TRCD			0x00f8
#define DDR_PCTL_TRRD			0x00fc
#define DDR_PCTL_TRTP			0x0100
#define DDR_PCTL_TWR			0x0104
#define DDR_PCTL_TWTR			0x0108
#define DDR_PCTL_TEXSR			0x010c
#define DDR_PCTL_TXP			0x0110
#define DDR_PCTL_TXPDLL			0x0114
#define DDR_PCTL_TZQCS			0x0118
#define DDR_PCTL_TZQCSI			0x011c
#define DDR_PCTL_TDQS			0x0120
#define DDR_PCTL_TCKSRE			0x0124
#define DDR_PCTL_TCKSRX			0x0128
#define DDR_PCTL_TCKE			0x012c
#define DDR_PCTL_TMOD			0x0130
#define DDR_PCTL_TRSTL			0x0134
#define DDR_PCTL_TZQCL			0x0138
#define DDR_PCTL_TMRR			0x013c
#define DDR_PCTL_TCKESR			0x0140
#define DDR_PCTL_TDPD			0x0144
#define DDR_PCTL_DFITCTRLDELAY		0x0240
#define DDR_PCTL_DFIODTCFG		0x0244
#define DDR_PCTL_DFIODTCFG1		0x0248
#define DDR_PCTL_DFIODTRANKMAP		0x024c
#define DDR_PCTL_DFITPHYWRDATA		0x0250
#define DDR_PCTL_DFITPHYWRLAT		0x0254
#define DDR_PCTL_DFITRDDATAEN		0x0260
#define DDR_PCTL_DFITPHYRDLAT           0x0264
#define DDR_PCTL_DFITPHYUPDTYPE0        0x0270
#define DDR_PCTL_DFITPHYUPDTYPE1        0x0274
#define DDR_PCTL_DFITPHYUPDTYPE2        0x0278
#define DDR_PCTL_DFITPHYUPDTYPE3        0x027c
#define DDR_PCTL_DFITCTRLUPDMIN         0x0280
#define DDR_PCTL_DFITCTRLUPDMAX         0x0284
#define DDR_PCTL_DFITCTRLUPDDLY         0x0288
#define DDR_PCTL_DFIUPDCFG		0x0290
#define DDR_PCTL_DFITREFMSKI            0x0294
#define DDR_PCTL_DFITCTRLUPDI           0x0298
#define DDR_PCTL_DFISTCFG0		0x02c4
#define DDR_PCTL_DFISTCFG1		0x02c8
#define DDR_PCTL_DFITDRAMCLKEN          0x02d0
#define DDR_PCTL_DFITDRAMCLKDIS         0x02d4
#define DDR_PCTL_DFISTCFG2		0x02d8
#define DDR_PCTL_DFILPCFG0		0x02f0

/* DDR phy register */
#define DDR_PUBL_RIDR		0x0000
#define DDR_PUBL_PIR		0x0004
#define DDR_PUBL_PGCR		0x0008
#define DDR_PUBL_PGSR		0x000c
#define DDR_PUBL_DLLGCR		0x0010
#define DDR_PUBL_ACDLLCR	0x0014
#define DDR_PUBL_PTR0		0x0018
#define DDR_PUBL_PTR1		0x001c
#define DDR_PUBL_PTR2		0x0020
#define DDR_PUBL_ACIOCR		0x0024
#define DDR_PUBL_DXCCR		0x0028
#define DDR_PUBL_DSGCR		0x002c
#define DDR_PUBL_DCR		0x0030
#define DDR_PUBL_DTPR0		0x0034
#define DDR_PUBL_DTPR1		0x0038
#define DDR_PUBL_DTPR2		0x003c
#define DDR_PUBL_MR0		0x0040
#define DDR_PUBL_MR1		0x0044
#define DDR_PUBL_MR2		0x0048
#define DDR_PUBL_MR3		0x004c
#define DDR_PUBL_ODTCR		0x0050
#define DDR_PUBL_DTAR		0x0054
#define DDR_PUBL_ZQ0CR0		0x0180
#define DDR_PUBL_ZQ0CR1		0x0184
#define DDR_PUBL_ZQ1CR0		0x0190
#define DDR_PUBL_DX0GCR		0x01c0
#define DDR_PUBL_DX0GSR0	0x01c4
#define DDR_PUBL_DX0GSR1	0x01c8
#define DDR_PUBL_DX0DLLCR	0x01cc
#define DDR_PUBL_DX0DQTR	0x01d0
#define DDR_PUBL_DX0DQSTR	0x01d4
#define DDR_PUBL_DX1GCR		0x0200
#define DDR_PUBL_DX1GSR0	0x0204
#define DDR_PUBL_DX1GSR1	0x0208
#define DDR_PUBL_DX1DLLCR	0x020c
#define DDR_PUBL_DX1DQTR	0x0210
#define DDR_PUBL_DX1DQSTR	0x0214
#define DDR_PUBL_DX2GCR		0x0240
#define DDR_PUBL_DX2GSR0	0x0244
#define DDR_PUBL_DX2GSR1	0x0248
#define DDR_PUBL_DX2DLLCR	0x024c
#define DDR_PUBL_DX2DQTR	0x0250
#define DDR_PUBL_DX2DQSTR	0x0254
#define DDR_PUBL_DX3GCR		0x0280
#define DDR_PUBL_DX3GSR0	0x0284
#define DDR_PUBL_DX3GSR1	0x0288
#define DDR_PUBL_DX3DLLCR	0x028c
#define DDR_PUBL_DX3DQTR	0x0290
#define DDR_PUBL_DX3DQSTR	0x0294

/* DDR msch register */
#define DDR_MSCH_DDRCONF	0x0008
#define DDR_MSCH_DDRTIMING	0x000c
#define DDR_MSCH_DDRMODE	0x0010
#define DDR_MSCH_READLATENCY	0x0014
#define DDR_MSCH_ACTIVATE	0x0038
#define DDR_MSCH_DEVTODEV	0x003c

#define DLLSRST			BIT(30)
#define POWER_UP_START		BIT(0)
#define POWER_UP_DONE		BIT(0)
#define DDR0IO_RET_DE_REQ	BIT(21)
#define DDR0I1_RET_DE_REQ	BIT(22)

#define PCTL_STAT_MSK			(7)
#define LP_TRIG_VAL(n)			(((n) >> 4) & 7)

/* SCTL */
#define INIT_STATE			(0)
#define CFG_STATE			(1)
#define GO_STATE			(2)
#define SLEEP_STATE			(3)
#define WAKEUP_STATE			(4)

/* STAT */
#define LP_TRIG_VAL(n)			(((n) >> 4) & 7)
#define PCTL_STAT_MSK			(7)
#define INIT_MEM			(0)
#define CONFIG				(1)
#define CONFIG_REQ			(2)
#define ACCESS				(3)
#define ACCESS_REQ			(4)
#define LOW_POWER			(5)
#define LOW_POWER_ENTRY_REQ		(6)
#define LOW_POWER_EXIT_REQ		(7)

/* PGSR */
#define PGSR_IDONE			(1 << 0)
#define PGSR_DLDONE			(1 << 1)
#define PGSR_ZCDONE			(1 << 2)
#define PGSR_DIDONE			(1 << 3)
#define PGSR_DTDONE			(1 << 4)
#define PGSR_DTERR			(1 << 5)
#define PGSR_DTIERR			(1 << 6)
#define PGSR_DFTERR			(1 << 7)
#define PGSR_RVERR			(1 << 8)
#define PGSR_RVEIRR			(1 << 9)

/* PIR */
#define PIR_INIT			(1 << 0)
#define PIR_DLLSRST			(1 << 1)
#define PIR_DLLLOCK			(1 << 2)
#define PIR_ZCAL			(1 << 3)
#define PIR_ITMSRST			(1 << 4)
#define PIR_DRAMRST			(1 << 5)
#define PIR_DRAMINIT			(1 << 6)
#define PIR_QSTRN			(1 << 7)
#define PIR_RVTRN			(1 << 8)
#define PIR_ICPC			(1 << 16)
#define PIR_DLLBYP			(1 << 17)
#define PIR_CTLDINIT			(1 << 18)
#define PIR_CLRSR			(1 << 28)
#define PIR_LOCKBYP			(1 << 29)
#define PIR_ZCALBYP			(1 << 30)
#define PIR_INITBYP			(1u << 31)

#endif /* __MACH_ROCKCHIP_RK3288_DDR_H */
