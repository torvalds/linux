/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2023, MediaTek Inc.
 * Copyright (c) 2023, BayLibre Inc.
 */

#ifndef __PHY_MTK_MIPI_CSI_V_0_5_RX_REG_H__
#define __PHY_MTK_MIPI_CSI_V_0_5_RX_REG_H__

/*
 * CSI1 and CSI2 are identical, and similar to CSI0. All CSIX macros are
 * applicable to the three PHYs. Where differences exist, they are denoted by
 * macro names using CSI0 and CSI1, the latter being applicable to CSI1 and
 * CSI2 alike.
 */

#define MIPI_RX_ANA00_CSIXA			0x0000
#define RG_CSI0A_CPHY_EN			BIT(0)
#define RG_CSIXA_EQ_PROTECT_EN			BIT(1)
#define RG_CSIXA_BG_LPF_EN			BIT(2)
#define RG_CSIXA_BG_CORE_EN			BIT(3)
#define RG_CSIXA_DPHY_L0_CKMODE_EN		BIT(5)
#define RG_CSIXA_DPHY_L0_CKSEL			BIT(6)
#define RG_CSIXA_DPHY_L1_CKMODE_EN		BIT(8)
#define RG_CSIXA_DPHY_L1_CKSEL			BIT(9)
#define RG_CSIXA_DPHY_L2_CKMODE_EN		BIT(11)
#define RG_CSIXA_DPHY_L2_CKSEL			BIT(12)

#define MIPI_RX_ANA18_CSIXA			0x0018
#define RG_CSI0A_L0_T0AB_EQ_IS			GENMASK(5, 4)
#define RG_CSI0A_L0_T0AB_EQ_BW			GENMASK(7, 6)
#define RG_CSI0A_L1_T1AB_EQ_IS			GENMASK(21, 20)
#define RG_CSI0A_L1_T1AB_EQ_BW			GENMASK(23, 22)
#define RG_CSI0A_L2_T1BC_EQ_IS			GENMASK(21, 20)
#define RG_CSI0A_L2_T1BC_EQ_BW			GENMASK(23, 22)
#define RG_CSI1A_L0_EQ_IS			GENMASK(5, 4)
#define RG_CSI1A_L0_EQ_BW			GENMASK(7, 6)
#define RG_CSI1A_L1_EQ_IS			GENMASK(21, 20)
#define RG_CSI1A_L1_EQ_BW			GENMASK(23, 22)
#define RG_CSI1A_L2_EQ_IS			GENMASK(5, 4)
#define RG_CSI1A_L2_EQ_BW			GENMASK(7, 6)

#define MIPI_RX_ANA1C_CSIXA			0x001c
#define MIPI_RX_ANA20_CSI0A			0x0020

#define MIPI_RX_ANA24_CSIXA			0x0024
#define RG_CSIXA_RESERVE			GENMASK(31, 24)

#define MIPI_RX_ANA40_CSIXA			0x0040
#define RG_CSIXA_CPHY_FMCK_SEL			GENMASK(1, 0)
#define RG_CSIXA_ASYNC_OPTION			GENMASK(7, 4)
#define RG_CSIXA_CPHY_SPARE			GENMASK(31, 16)

#define MIPI_RX_WRAPPER80_CSIXA			0x0080
#define CSR_CSI_RST_MODE			GENMASK(17, 16)

#define MIPI_RX_ANAA8_CSIXA			0x00a8
#define RG_CSIXA_CDPHY_L0_T0_BYTECK_INVERT	BIT(0)
#define RG_CSIXA_DPHY_L1_BYTECK_INVERT		BIT(1)
#define RG_CSIXA_CDPHY_L2_T1_BYTECK_INVERT	BIT(2)

#endif
