/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * FacetimeHD camera driver
 *
 * Copyright (C) 2014 Patrik Jakobsson (patrik.r.jakobsson@gmail.com)
 *
 */

#ifndef _FTHD_REG_H
#define _FTHD_REG_H

#include <linux/kernel.h>

/* PCIE link regs */
#define S2_PCIE_LINK_D000	0xd000
#define S2_PCIE_LINK_D120	0xd120
#define S2_PCIE_LINK_D124	0xd124
#define S2_PCIE_LINK_D128	0xd128
#define S2_PCIE_LINK_D12C	0xd12c

/* Unknown */
#define S2_D104			0xd104
#define S2_D108			0xd108

/* These are written to 0x203 before DDR soc init */
#define S2_DDR_REG_1100		0x1100
#define S2_DDR_REG_1104		0x1104
#define S2_DDR_REG_1108		0x1108
#define S2_DDR_REG_110C		0x110c
#define S2_DDR_REG_1110		0x1110
#define S2_DDR_REG_1114		0x1114
#define S2_DDR_REG_1118		0x1118
#define S2_DDR_REG_111C		0x111c

#define S2_PLL_REFCLK		0x04
#define S2_PLL_REFCLK_25MHZ	(1 << 3) /* 1 = 25MHz, 0 = 24MHz */

#define S2_PLL_CMU_STATUS	0x0c	/* Register is called CMU_R_PLL_STS_MEMADDR */
#define S2_PLL_CMU_STATUS_LOCKED (1 << 15) /* 1 = PLL locked, 0 = PLL not locked */

#define S2_PLL_STATUS_A8	0xa8
#define S2_PLL_BYPASS		(1 << 0) /* 1 = bypass, 0 = non-bypass */

#define S2_PLL_CTRL_14		0x0014
#define S2_PLL_CTRL_20		0x0020
#define S2_PLL_CTRL_24		0x0024
#define S2_PLL_CTRL_2C		0x002c
#define S2_PLL_CTRL_9C		0x009c
#define S2_PLL_CTRL_100		0x0100
#define S2_PLL_CTRL_510		0x0510

/* Probably DDR PHY PLL registers */
#define S2_DDR_2004			0x2004
#define S2_DDR_2008			0x2008
#define S2_DDR_2014			0x2014
#define S2_DDR_STATUS_2018		0x2018
#define S2_DDR_STATUS_BUSY		(1 << 0)

#define S2_DDR_20A0			0x20a0
#define S2_DDR_20A4			0x20a4
#define S2_DDR_20A8			0x20a8
#define S2_DDR_20B0			0x20b0

#define S2_20F8				0x20f8
#define S2_DDR_2118			0x2118

#define S2_2424				0x2424
#define S2_2430				0x2430
#define S2_2434				0x2434
#define S2_2438				0x2438

/* PLL for stage 2 */
#define S2_DDR_PLL_STATUS_241C		0x241c
#define S2_DDR_PLL_STATUS_241C_LOCKED	(1 << 10)

/* PLL for stage 1 */
#define S2_DDR_PLL_STATUS_2444		0x2444
#define S2_DDR_PLL_STATUS_2444_LOCKED	(1 << 13)

/*
 * These registers must be saved and restored across suspend/resume
 * FIXME: Double check these
 */
static const u32 fthd_ddr_phy_reg_map[] = {
	0x0000, 0x0004, 0x0010, 0x0014, 0x0018, 0x001c, 0x0020, 0x0030,
	0x0034, 0x0038, 0x003c, 0x0040, 0x0044, 0x0048, 0x004c, 0x0050,
	0x0054, 0x0058, 0x005c, 0x0060, 0x0064, 0x0068, 0x006c, 0x0070,
	0x0074, 0x0078, 0x007c, 0x0080, 0x0084, 0x0090, 0x0094, 0x0098,
	0x009c, 0x00a0, 0x00a4, 0x00b0, 0x00b4, 0x00b8, 0x00bc, 0x00c0,
	0x0200, 0x0204, 0x0208, 0x020c, 0x0210, 0x0214, 0x0218, 0x021c,
	0x0220, 0x0224, 0x0228, 0x022c, 0x0230, 0x0234, 0x0238, 0x023c,
	0x0240, 0x0244, 0x0248, 0x024c, 0x0250, 0x0254, 0x0258, 0x025c,
	0x0260, 0x0264, 0x0268, 0x026c, 0x0270, 0x0274, 0x02a4, 0x02a8,
	0x02ac, 0x02b0, 0x02b4, 0x02b8, 0x02bc, 0x02c0, 0x02c4, 0x02c8,
	0x02cc, 0x02d0, 0x02d4, 0x02d8, 0x02dc, 0x02e0, 0x02e4, 0x02e8,
	0x02ec, 0x02f0, 0x02f4, 0x02f8, 0x02fc, 0x0300, 0x0304, 0x0308,
	0x030c, 0x0310, 0x0314, 0x0328, 0x032c, 0x0330, 0x0334, 0x0338,
	0x033c, 0x0348, 0x034c, 0x0350, 0x0354, 0x0358, 0x035c, 0x0360,
	0x0364, 0x0370, 0x0374, 0x0378, 0x037c, 0x0380, 0x0384, 0x0388,
	0x038c, 0x0390, 0x0394, 0x03a0, 0x03a4, 0x03a8, 0x03ac,
};

#define DDR_PHY_NUM_REG ARRAY_SIZE(fthd_ddr_phy_reg_map)
#define DDR_PHY_REG_BASE		0x2800

/* DDR40 */
#define S2_DDR40_PHY_PLL_STATUS		0x2810
#define S2_DDR40_PHY_PLL_STATUS_LOCKED	(1 << 0)
#define S2_DDR40_PHY_PLL_CFG		0x2814
#define S2_DDR40_PHY_PLL_DIV		0x281c
#define S2_DDR40_PHY_AUX_CTL		0x2820

#define S2_DDR40_PHY_VDL_OVR_COARSE	0x2830
#define S2_DDR40_PHY_VDL_OVR_FINE	0x2834

#define S2_DDR40_PHY_ZQ_PVT_COMP_CTL	0x283c
#define S2_DDR40_PHY_DRV_PAD_CTL	0x2840

#define S2_DDR40_PHY_VDL_CTL		0x2848

#define S2_DDR40_PHY_VDL_STATUS		0x284c
#define S2_DDR40_PHY_VDL_STEP_MASK	0x0ffc
#define S2_DDR40_PHY_VDL_STEP_SHIFT	2

#define S2_DDR40_PHY_DQ_CALIB_STATUS	0x2850
#define S2_DDR40_PHY_VDL_CHAN_STATUS	0x2854

#define S2_DDR40_PHY_VTT_CTL		0x285c
#define S2_DDR40_PHY_VTT_STATUS		0x2860
#define S2_DDR40_PHY_VTT_CONNECTIONS	0x2864
#define S2_DDR40_PHY_VTT_OVERRIDE	0x2868

#define S2_DDR40_STRAP_CTL		0x28b0
#define S2_DDR40_STRAP_CTL_2		0x28b4
#define S2_DDR40_STRAP_STATUS		0x28b8

/* FIXME: Come up with a better name */
#define S2_DDR40_BYTE_LANE_SIZE		0xa0
#define S2_DDR40_NUM_BYTE_LANES		2

#define S2_DDR40_RDEN_BYTE		0x2a00
#define S2_DDR40_2A08			0x2a08
#define S2_DDR40_2A0C			0x2a0c
#define S2_DDR40_2A10			0x2a10
#define S2_DDR40_2A34			0x2a34
#define S2_DDR40_2A38			0x2a38
#define S2_DDR40_RDEN_BYTE0		0x2a74
#define S2_DDR40_2AA8			0x2aa8
#define S2_DDR40_2AAC			0x2aac
#define S2_DDR40_RDEN_BYTE1		0x2b14
#define S2_DDR40_WL_RD_DATA_DLY		0x2b60
#define S2_DDR40_WL_READ_CTL		0x2b64
#define S2_DDR40_WL_READ_FIFO_STATUS	0x2b90
#define S2_DDR40_WL_READ_FIFO_CLEAR	0x2b94
#define S2_DDR40_WL_DRV_PAD_CTL		0x2ba4
#define S2_DDR40_WL_CLK_PAD_DISABLE	0x2ba8
#define S2_DDR40_WL_IDLE_PAD_CTL	0x2ba0
#define S2_DDR40_WL_WR_PREAMBLE_MODE	0x2bac

#define S2_3200				0x3200
#define S2_3204				0x3204
#define S2_3208				0x3208

/* On iomem with pointer at 0x0ff0 (Bar 4: 1MB) */
#define ISP_FW_CHAN_CTRL	0xc3000
#define ISP_FW_QUEUE_CTRL	0xc3004
#define ISP_FW_SIZE		0xc3008
#define ISP_FW_HEAP_SIZE	0xc300c
#define ISP_FW_HEAP_ADDR	0xc3010
#define ISP_FW_HEAP_SIZE2	0xc3014
#define ISP_REG_C3018		0xc3018 /* Module params or cmd buf? */
#define ISP_REG_C301C		0xc301c
#define ISP_REG_40004		0x40004
#define ISP_REG_40008		0x40008
#define ISP_IRQ_STATUS		0x41000
#define ISP_IRQ_ENABLE		0x41004
#define ISP_REG_41020		0x41020
#define ISP_IRQ_CLEAR		0x41024

#define ISP_FW_CHAN_START	0x0128
#define ISP_FW_CHAN_END		0x0220

#endif
