/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author:
 *      hjc <hjc@rock-chips.com>
 *      mark yao <mark.yao@rock-chips.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _ROCKCHIP_LVDS_
#define _ROCKCHIP_LVDS_

#define RK3288_LVDS_CH0_REG0			0x00
#define RK3288_LVDS_CH0_REG0_LVDS_EN		BIT(7)
#define RK3288_LVDS_CH0_REG0_TTL_EN		BIT(6)
#define RK3288_LVDS_CH0_REG0_LANECK_EN		BIT(5)
#define RK3288_LVDS_CH0_REG0_LANE4_EN		BIT(4)
#define RK3288_LVDS_CH0_REG0_LANE3_EN		BIT(3)
#define RK3288_LVDS_CH0_REG0_LANE2_EN		BIT(2)
#define RK3288_LVDS_CH0_REG0_LANE1_EN		BIT(1)
#define RK3288_LVDS_CH0_REG0_LANE0_EN		BIT(0)

#define RK3288_LVDS_CH0_REG1			0x04
#define RK3288_LVDS_CH0_REG1_LANECK_BIAS	BIT(5)
#define RK3288_LVDS_CH0_REG1_LANE4_BIAS		BIT(4)
#define RK3288_LVDS_CH0_REG1_LANE3_BIAS		BIT(3)
#define RK3288_LVDS_CH0_REG1_LANE2_BIAS		BIT(2)
#define RK3288_LVDS_CH0_REG1_LANE1_BIAS		BIT(1)
#define RK3288_LVDS_CH0_REG1_LANE0_BIAS		BIT(0)

#define RK3288_LVDS_CH0_REG2			0x08
#define RK3288_LVDS_CH0_REG2_RESERVE_ON		BIT(7)
#define RK3288_LVDS_CH0_REG2_LANECK_LVDS_MODE	BIT(6)
#define RK3288_LVDS_CH0_REG2_LANE4_LVDS_MODE	BIT(5)
#define RK3288_LVDS_CH0_REG2_LANE3_LVDS_MODE	BIT(4)
#define RK3288_LVDS_CH0_REG2_LANE2_LVDS_MODE	BIT(3)
#define RK3288_LVDS_CH0_REG2_LANE1_LVDS_MODE	BIT(2)
#define RK3288_LVDS_CH0_REG2_LANE0_LVDS_MODE	BIT(1)
#define RK3288_LVDS_CH0_REG2_PLL_FBDIV8		BIT(0)

#define RK3288_LVDS_CH0_REG3			0x0c
#define RK3288_LVDS_CH0_REG3_PLL_FBDIV_MASK	0xff

#define RK3288_LVDS_CH0_REG4			0x10
#define RK3288_LVDS_CH0_REG4_LANECK_TTL_MODE	BIT(5)
#define RK3288_LVDS_CH0_REG4_LANE4_TTL_MODE	BIT(4)
#define RK3288_LVDS_CH0_REG4_LANE3_TTL_MODE	BIT(3)
#define RK3288_LVDS_CH0_REG4_LANE2_TTL_MODE	BIT(2)
#define RK3288_LVDS_CH0_REG4_LANE1_TTL_MODE	BIT(1)
#define RK3288_LVDS_CH0_REG4_LANE0_TTL_MODE	BIT(0)

#define RK3288_LVDS_CH0_REG5			0x14
#define RK3288_LVDS_CH0_REG5_LANECK_TTL_DATA	BIT(5)
#define RK3288_LVDS_CH0_REG5_LANE4_TTL_DATA	BIT(4)
#define RK3288_LVDS_CH0_REG5_LANE3_TTL_DATA	BIT(3)
#define RK3288_LVDS_CH0_REG5_LANE2_TTL_DATA	BIT(2)
#define RK3288_LVDS_CH0_REG5_LANE1_TTL_DATA	BIT(1)
#define RK3288_LVDS_CH0_REG5_LANE0_TTL_DATA	BIT(0)

#define RK3288_LVDS_CFG_REGC			0x30
#define RK3288_LVDS_CFG_REGC_PLL_ENABLE		0x00
#define RK3288_LVDS_CFG_REGC_PLL_DISABLE	0xff

#define RK3288_LVDS_CH0_REGD			0x34
#define RK3288_LVDS_CH0_REGD_PLL_PREDIV_MASK	0x1f

#define RK3288_LVDS_CH0_REG20			0x80
#define RK3288_LVDS_CH0_REG20_MSB		0x45
#define RK3288_LVDS_CH0_REG20_LSB		0x44

#define RK3288_LVDS_CFG_REG21			0x84
#define RK3288_LVDS_CFG_REG21_TX_ENABLE		0x92
#define RK3288_LVDS_CFG_REG21_TX_DISABLE	0x00
#define RK3288_LVDS_CH1_OFFSET			0x100

/* fbdiv value is split over 2 registers, with bit8 in reg2 */
#define RK3288_LVDS_PLL_FBDIV_REG2(_fbd) \
		(_fbd & BIT(8) ? RK3288_LVDS_CH0_REG2_PLL_FBDIV8 : 0)
#define RK3288_LVDS_PLL_FBDIV_REG3(_fbd) \
		(_fbd & RK3288_LVDS_CH0_REG3_PLL_FBDIV_MASK)
#define RK3288_LVDS_PLL_PREDIV_REGD(_pd) \
		(_pd & RK3288_LVDS_CH0_REGD_PLL_PREDIV_MASK)

#define RK3288_GRF_SOC_CON6		0x025c
#define RK3288_GRF_SOC_CON7		0x0260

#define RK3288_LVDS_SOC_CON6_SEL_VOP_LIT	BIT(3)

#define RK3366_GRF_SOC_CON0	0x0400
#define RK3366_LVDS_VOP_SEL_LIT	(BITS_MASK(1, 1, 0) | BITS_EN(1, 0))
#define RK3366_LVDS_VOP_SEL_BIG	(BITS_MASK(0, 1, 0) | BITS_EN(1, 0))
#define RK3366_GRF_SOC_CON5	0x0414
#define RK3366_GRF_SOC_CON6	0x0418

#define RK3368_GRF_SOC_CON7	0x041c
#define RK3368_GRF_SOC_CON15	0x043c

#define RK3126_GRF_LVDS_CON0	0x0150
#define RK3126_GRF_CON1		0x0144

#define PX30_GRF_PD_VO_CON0	0x0434
#define PX30_GRF_PD_VO_CON1	0x0438
#define PX30_LVDS_OUTPUT_FORMAT(x)	(BITS_MASK(x, 0x3, 13) | BITS_EN(0x3, 13))
#define PX30_LVDS_PHY_MODE(x)		(BITS_MASK(x, 0x1, 12) | BITS_EN(0x1, 12))
#define PX30_LVDS_MSBSEL(x)		(BITS_MASK(x, 0x1, 11) | BITS_EN(0x1, 11))
#define PX30_DPHY_FORCERXMODE(x)	(BITS_MASK(x, 0x1,  6) | BITS_EN(0x1,  6))
#define PX30_LCDC_DCLK_INV(v)		(BITS_MASK(x, 0x1,  4) | BITS_EN(0x1,  4))
#define PX30_RGB_SYNC_BYPASS(x)		(BITS_MASK(x, 0x1,  3) | BITS_EN(0x1,  3))
#define PX30_RGB_VOP_SEL(x)		(BITS_MASK(x, 0x1,  2) | BITS_EN(0x1,  2))
#define PX30_LVDS_VOP_SEL(x)		(BITS_MASK(x, 0x1,  1) | BITS_EN(0x1,  1))

#define LVDS_FMT_MASK				(0x07 << 16)
#define LVDS_MSB				BIT(3)
#define LVDS_DUAL				BIT(4)
#define LVDS_FMT_1				BIT(5)
#define LVDS_TTL_EN				BIT(6)
#define LVDS_START_PHASE_RST_1			BIT(7)
#define LVDS_DCLK_INV				BIT(8)
#define LVDS_CH0_EN				BIT(11)
#define LVDS_CH1_EN				BIT(12)
#define LVDS_PWRDN				BIT(15)

#define LVDS_24BIT				(0 << 1)
#define LVDS_18BIT				(1 << 1)
#define LVDS_FORMAT_VESA			(0 << 0)
#define LVDS_FORMAT_JEIDA			(1 << 0)

#define BITS(x, bit)		((x) << (bit))
#define BITS_MASK(x, mask, bit)	BITS((x) & (mask), bit)
#define BITS_EN(mask, bit)	BITS(mask, bit + 16)

#define MIPIPHY_REG0		0x0000

#define MIPIPHY_REG1		0x0004
#define m_SYNC_RST		BIT(0)
#define m_LDO_PWR_DOWN		BIT(1)
#define m_PLL_PWR_DOWN		BIT(2)
#define v_SYNC_RST(x)		BITS_MASK(x, 1, 0)
#define v_LDO_PWR_DOWN(x)	BITS_MASK(x, 1, 1)
#define v_PLL_PWR_DOWN(x)	BITS_MASK(x, 1, 2)

#define MIPIPHY_REG3		0x000c
#define m_PREDIV		GENMASK(4, 0)
#define m_FBDIV_MSB		BIT(5)
#define v_PREDIV(x)		BITS_MASK(x, 0x1f, 0)
#define v_FBDIV_MSB(x)		BITS_MASK(x, 1, 5)

#define MIPIPHY_REG4		0x0010
#define v_FBDIV_LSB(x)		BITS_MASK(x, 0xff, 0)

#define MIPIPHY_REGE0		0x0380
#define m_MSB_SEL		BIT(0)
#define m_DIG_INTER_RST		BIT(2)
#define m_LVDS_MODE_EN		BIT(5)
#define m_TTL_MODE_EN		BIT(6)
#define m_MIPI_MODE_EN		BIT(7)
#define v_MSB_SEL(x)		BITS_MASK(x, 1, 0)
#define v_DIG_INTER_RST(x)	BITS_MASK(x, 1, 2)
#define v_LVDS_MODE_EN(x)	BITS_MASK(x, 1, 5)
#define v_TTL_MODE_EN(x)	BITS_MASK(x, 1, 6)
#define v_MIPI_MODE_EN(x)	BITS_MASK(x, 1, 7)

#define MIPIPHY_REGE1		0x0384
#define m_DIG_INTER_EN		BIT(7)
#define v_DIG_INTER_EN(x)	BITS_MASK(x, 1, 7)

#define MIPIPHY_REGE3		0x038c
#define m_MIPI_EN		BIT(0)
#define m_LVDS_EN		BIT(1)
#define m_TTL_EN		BIT(2)
#define v_MIPI_EN(x)		BITS_MASK(x, 1, 0)
#define v_LVDS_EN(x)		BITS_MASK(x, 1, 1)
#define v_TTL_EN(x)		BITS_MASK(x, 1, 2)

#define MIPIPHY_REGE4		0x0390
#define m_VOCM			GENMASK(5, 4)
#define m_DIFF_V		GENMASK(7, 6)

#define v_VOCM(x)		BITS_MASK(x, 3, 4)
#define v_DIFF_V(x)		BITS_MASK(x, 3, 6)

#define MIPIPHY_REGE8		0x03a0

#define MIPIPHY_REGEB		0x03ac
#define v_PLL_PWR_OFF(x)	BITS_MASK(x, 1, 2)
#define v_LANECLK_EN(x)		BITS_MASK(x, 1, 3)
#define v_LANE3_EN(x)		BITS_MASK(x, 1, 4)
#define v_LANE2_EN(x)		BITS_MASK(x, 1, 5)
#define v_LANE1_EN(x)		BITS_MASK(x, 1, 6)
#define v_LANE0_EN(x)		BITS_MASK(x, 1, 7)

/* MIPI DSI Controller register */
#define MIPIC_PHY_RSTZ		0x00a0
#define m_PHY_ENABLE_CLK	BIT(2)
#define MIPIC_PHY_STATUS	0x00b0
#define m_PHY_LOCK_STATUS	BIT(0)

#define v_RK336X_LVDS_OUTPUT_FORMAT(x)	(BITS_MASK(x, 3, 13) | BITS_EN(3, 13))
#define v_RK336X_LVDS_MSBSEL(x)		(BITS_MASK(x, 1, 11) | BITS_EN(1, 11))
#define v_RK336X_LVDSMODE_EN(x)		(BITS_MASK(x, 1, 12) | BITS_EN(1, 12))
#define v_RK336X_MIPIPHY_TTL_EN(x)	(BITS_MASK(x, 1, 15) | BITS_EN(1, 15))
#define v_RK336X_MIPIPHY_LANE0_EN(x)	(BITS_MASK(x, 1, 5) | BITS_EN(1, 5))
#define v_RK336X_MIPIDPI_FORCEX_EN(x)	(BITS_MASK(x, 1, 6) | BITS_EN(1, 6))
#define v_RK336X_FORCE_JETAG(x)		(BITS_MASK(x, 1, 13) | BITS_EN(1, 13))

#define v_RK3126_LVDS_OUTPUT_FORMAT(x)	(BITS_MASK(x, 3, 1) | BITS_EN(3, 1))
#define v_RK3126_LVDS_MSBSEL(x)		(BITS_MASK(x, 1, 3) | BITS_EN(1, 3))
#define v_RK3126_LVDSMODE_EN(x)		(BITS_MASK(x, 1, 6) | BITS_EN(1, 6))
#define v_RK3126_MIPIPHY_TTL_EN(x)	(BITS_MASK(x, 1, 7) | BITS_EN(1, 7))
#define v_RK3126_MIPIPHY_LANE0_EN(x)	(BITS_MASK(x, 1, 8) | BITS_EN(1, 8))
#define v_RK3126_MIPIDPI_FORCEX_EN(x)	(BITS_MASK(x, 1, 9) | BITS_EN(1, 9))

#define v_RK3126_MIPITTL_CLK_EN(x)	(BITS_MASK(x, 1, 7) | BITS_EN(1, 7))
#define v_RK3126_MIPITTL_LANE0_EN(x)	(BITS_MASK(x, 1, 11) | BITS_EN(1, 11))
#define v_RK3126_MIPITTL_LANE1_EN(x)	(BITS_MASK(x, 1, 12) | BITS_EN(1, 12))
#define v_RK3126_MIPITTL_LANE2_EN(x)	(BITS_MASK(x, 1, 13) | BITS_EN(1, 13))
#define v_RK3126_MIPITTL_LANE3_EN(x)	(BITS_MASK(x, 1, 14) | BITS_EN(1, 14))

enum {
	LVDS_MSB_D0 = 0,
	LVDS_MSB_D7,
};

#endif /* _ROCKCHIP_LVDS_ */
