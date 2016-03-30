/*
 * rcar_lvds_regs.h  --  R-Car LVDS Interface Registers Definitions
 *
 * Copyright (C) 2013-2015 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
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
#define LVDCR0_LVEN			(1 << 1)		/* Gen2 only */
#define LVDCR0_LVRES			(1 << 0)

#define LVDCR1				0x0004
#define LVDCR1_CKSEL			(1 << 15)		/* Gen2 only */
#define LVDCR1_CHSTBY_GEN2(n)		(3 << (2 + (n) * 2))	/* Gen2 only */
#define LVDCR1_CHSTBY_GEN3(n)		(1 << (2 + (n) * 2))	/* Gen3 only */
#define LVDCR1_CLKSTBY_GEN2		(3 << 0)		/* Gen2 only */
#define LVDCR1_CLKSTBY_GEN3		(1 << 0)		/* Gen3 only */

#define LVDPLLCR			0x0008
#define LVDPLLCR_CEEN			(1 << 14)
#define LVDPLLCR_FBEN			(1 << 13)
#define LVDPLLCR_COSEL			(1 << 12)
/* Gen2 */
#define LVDPLLCR_PLLDLYCNT_150M		(0x1bf << 0)
#define LVDPLLCR_PLLDLYCNT_121M		(0x22c << 0)
#define LVDPLLCR_PLLDLYCNT_60M		(0x77b << 0)
#define LVDPLLCR_PLLDLYCNT_38M		(0x69a << 0)
#define LVDPLLCR_PLLDLYCNT_MASK		(0x7ff << 0)
/* Gen3 */
#define LVDPLLCR_PLLDIVCNT_42M		(0x014cb << 0)
#define LVDPLLCR_PLLDIVCNT_85M		(0x00a45 << 0)
#define LVDPLLCR_PLLDIVCNT_128M		(0x006c3 << 0)
#define LVDPLLCR_PLLDIVCNT_148M		(0x046c1 << 0)
#define LVDPLLCR_PLLDIVCNT_MASK		(0x7ffff << 0)

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

#endif /* __RCAR_LVDS_REGS_H__ */
