/* SPDX-License-Identifier: GPL-2.0 */
/*
 * R-Car LVDS Interface Registers Definitions
 *
 * Copyright (C) 2013-2015 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 */

#ifndef __RCAR_LVDS_REGS_H__
#define __RCAR_LVDS_REGS_H__

#define LVDCR0				0x0000
#define LVDCR0_DUSEL			(1 << 15)
#define LVDCR0_DMD			(1 << 12)		/* Gen2 only */
#define LVDCR0_LVMD_MASK		(0xf << 8)
#define LVDCR0_LVMD_SHIFT		8
#define LVDCR0_PLLON			(1 << 4)
#define LVDCR0_PWD			(1 << 2)		/* Gen3 only */
#define LVDCR0_BEN			(1 << 2)		/* Gen2 only */
#define LVDCR0_LVEN			(1 << 1)
#define LVDCR0_LVRES			(1 << 0)

#define LVDCR1				0x0004
#define LVDCR1_CKSEL			(1 << 15)		/* Gen2 only */
#define LVDCR1_CHSTBY(n)		(3 << (2 + (n) * 2))
#define LVDCR1_CLKSTBY			(3 << 0)

#define LVDPLLCR			0x0008
/* Gen2 & V3M */
#define LVDPLLCR_CEEN			(1 << 14)
#define LVDPLLCR_FBEN			(1 << 13)
#define LVDPLLCR_COSEL			(1 << 12)
#define LVDPLLCR_PLLDLYCNT_150M		(0x1bf << 0)
#define LVDPLLCR_PLLDLYCNT_121M		(0x22c << 0)
#define LVDPLLCR_PLLDLYCNT_60M		(0x77b << 0)
#define LVDPLLCR_PLLDLYCNT_38M		(0x69a << 0)
#define LVDPLLCR_PLLDLYCNT_MASK		(0x7ff << 0)
/* Gen3 but V3M,D3 and E3 */
#define LVDPLLCR_PLLDIVCNT_42M		(0x014cb << 0)
#define LVDPLLCR_PLLDIVCNT_85M		(0x00a45 << 0)
#define LVDPLLCR_PLLDIVCNT_128M		(0x006c3 << 0)
#define LVDPLLCR_PLLDIVCNT_148M		(0x046c1 << 0)
#define LVDPLLCR_PLLDIVCNT_MASK		(0x7ffff << 0)
/* D3 and E3 */
#define LVDPLLCR_PLLON			(1 << 22)
#define LVDPLLCR_PLLSEL_PLL0		(0 << 20)
#define LVDPLLCR_PLLSEL_LVX		(1 << 20)
#define LVDPLLCR_PLLSEL_PLL1		(2 << 20)
#define LVDPLLCR_CKSEL_LVX		(1 << 17)
#define LVDPLLCR_CKSEL_EXTAL		(3 << 17)
#define LVDPLLCR_CKSEL_DU_DOTCLKIN(n)	((5 + (n) * 2) << 17)
#define LVDPLLCR_OCKSEL			(1 << 16)
#define LVDPLLCR_STP_CLKOUTE		(1 << 14)
#define LVDPLLCR_OUTCLKSEL		(1 << 12)
#define LVDPLLCR_CLKOUT			(1 << 11)
#define LVDPLLCR_PLLE(n)		((n) << 10)
#define LVDPLLCR_PLLN(n)		((n) << 3)
#define LVDPLLCR_PLLM(n)		((n) << 0)

#define LVDCTRCR			0x000c
#define LVDCTRCR_CTR3SEL_ZERO		(0 << 12)
#define LVDCTRCR_CTR3SEL_ODD		(1 << 12)
#define LVDCTRCR_CTR3SEL_CDE		(2 << 12)
#define LVDCTRCR_CTR3SEL_MASK		(7 << 12)
#define LVDCTRCR_CTR2SEL_DISP		(0 << 8)
#define LVDCTRCR_CTR2SEL_ODD		(1 << 8)
#define LVDCTRCR_CTR2SEL_CDE		(2 << 8)
#define LVDCTRCR_CTR2SEL_HSYNC		(3 << 8)
#define LVDCTRCR_CTR2SEL_VSYNC		(4 << 8)
#define LVDCTRCR_CTR2SEL_MASK		(7 << 8)
#define LVDCTRCR_CTR1SEL_VSYNC		(0 << 4)
#define LVDCTRCR_CTR1SEL_DISP		(1 << 4)
#define LVDCTRCR_CTR1SEL_ODD		(2 << 4)
#define LVDCTRCR_CTR1SEL_CDE		(3 << 4)
#define LVDCTRCR_CTR1SEL_HSYNC		(4 << 4)
#define LVDCTRCR_CTR1SEL_MASK		(7 << 4)
#define LVDCTRCR_CTR0SEL_HSYNC		(0 << 0)
#define LVDCTRCR_CTR0SEL_VSYNC		(1 << 0)
#define LVDCTRCR_CTR0SEL_DISP		(2 << 0)
#define LVDCTRCR_CTR0SEL_ODD		(3 << 0)
#define LVDCTRCR_CTR0SEL_CDE		(4 << 0)
#define LVDCTRCR_CTR0SEL_MASK		(7 << 0)

#define LVDCHCR				0x0010
#define LVDCHCR_CHSEL_CH(n, c)		((((c) - (n)) & 3) << ((n) * 4))
#define LVDCHCR_CHSEL_MASK(n)		(3 << ((n) * 4))

/* All registers below are specific to D3 and E3 */
#define LVDSTRIPE			0x0014
#define LVDSTRIPE_ST_TRGSEL_DISP	(0 << 2)
#define LVDSTRIPE_ST_TRGSEL_HSYNC_R	(1 << 2)
#define LVDSTRIPE_ST_TRGSEL_HSYNC_F	(2 << 2)
#define LVDSTRIPE_ST_SWAP		(1 << 1)
#define LVDSTRIPE_ST_ON			(1 << 0)

#define LVDSCR				0x0018
#define LVDSCR_DEPTH(n)			(((n) - 1) << 29)
#define LVDSCR_BANDSET			(1 << 28)
#define LVDSCR_TWGCNT(n)		((((n) - 256) / 16) << 24)
#define LVDSCR_SDIV(n)			((n) << 22)
#define LVDSCR_MODE			(1 << 21)
#define LVDSCR_RSTN			(1 << 20)

#define LVDDIV				0x001c
#define LVDDIV_DIVSEL			(1 << 8)
#define LVDDIV_DIVRESET			(1 << 7)
#define LVDDIV_DIVSTP			(1 << 6)
#define LVDDIV_DIV(n)			((n) << 0)

#endif /* __RCAR_LVDS_REGS_H__ */
