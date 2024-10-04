// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2015 - 2023 Beijing WangXun Technology Co., Ltd. */

#include <linux/pcs/pcs-xpcs.h>
#include <linux/mdio.h>
#include "pcs-xpcs.h"

/* VR_XS_PMA_MMD */
#define TXGBE_PMA_MMD			0x8020
#define TXGBE_TX_GENCTL1		0x11
#define TXGBE_TX_GENCTL1_VBOOST_LVL	GENMASK(10, 8)
#define TXGBE_TX_GENCTL1_VBOOST_EN0	BIT(4)
#define TXGBE_TX_GEN_CTL2		0x12
#define TXGBE_TX_GEN_CTL2_TX0_WIDTH(v)	FIELD_PREP(GENMASK(9, 8), v)
#define TXGBE_TX_RATE_CTL		0x14
#define TXGBE_TX_RATE_CTL_TX0_RATE(v)	FIELD_PREP(GENMASK(2, 0), v)
#define TXGBE_RX_GEN_CTL2		0x32
#define TXGBE_RX_GEN_CTL2_RX0_WIDTH(v)	FIELD_PREP(GENMASK(9, 8), v)
#define TXGBE_RX_GEN_CTL3		0x33
#define TXGBE_RX_GEN_CTL3_LOS_TRSHLD0	GENMASK(2, 0)
#define TXGBE_RX_RATE_CTL		0x34
#define TXGBE_RX_RATE_CTL_RX0_RATE(v)	FIELD_PREP(GENMASK(1, 0), v)
#define TXGBE_RX_EQ_ATTN_CTL		0x37
#define TXGBE_RX_EQ_ATTN_LVL0		GENMASK(2, 0)
#define TXGBE_RX_EQ_CTL0		0x38
#define TXGBE_RX_EQ_CTL0_VGA1_GAIN(v)	FIELD_PREP(GENMASK(15, 12), v)
#define TXGBE_RX_EQ_CTL0_VGA2_GAIN(v)	FIELD_PREP(GENMASK(11, 8), v)
#define TXGBE_RX_EQ_CTL0_CTLE_POLE(v)	FIELD_PREP(GENMASK(7, 5), v)
#define TXGBE_RX_EQ_CTL0_CTLE_BOOST(v)	FIELD_PREP(GENMASK(4, 0), v)
#define TXGBE_RX_EQ_CTL4		0x3C
#define TXGBE_RX_EQ_CTL4_CONT_OFF_CAN0	BIT(4)
#define TXGBE_RX_EQ_CTL4_CONT_ADAPT0	BIT(0)
#define TXGBE_AFE_DFE_ENABLE		0x3D
#define TXGBE_DFE_EN_0			BIT(4)
#define TXGBE_AFE_EN_0			BIT(0)
#define TXGBE_DFE_TAP_CTL0		0x3E
#define TXGBE_MPLLA_CTL0		0x51
#define TXGBE_MPLLA_CTL2		0x53
#define TXGBE_MPLLA_CTL2_DIV16P5_CLK_EN	BIT(10)
#define TXGBE_MPLLA_CTL2_DIV10_CLK_EN	BIT(9)
#define TXGBE_MPLLA_CTL3		0x57
#define TXGBE_MISC_CTL0			0x70
#define TXGBE_MISC_CTL0_PLL		BIT(15)
#define TXGBE_MISC_CTL0_CR_PARA_SEL	BIT(14)
#define TXGBE_MISC_CTL0_RX_VREF(v)	FIELD_PREP(GENMASK(12, 8), v)
#define TXGBE_VCO_CAL_LD0		0x72
#define TXGBE_VCO_CAL_REF0		0x76

static int txgbe_write_pma(struct dw_xpcs *xpcs, int reg, u16 val)
{
	return xpcs_write(xpcs, MDIO_MMD_PMAPMD, TXGBE_PMA_MMD + reg, val);
}

static int txgbe_modify_pma(struct dw_xpcs *xpcs, int reg, u16 mask, u16 set)
{
	return xpcs_modify(xpcs, MDIO_MMD_PMAPMD, TXGBE_PMA_MMD + reg, mask,
			   set);
}

static void txgbe_pma_config_10gbaser(struct dw_xpcs *xpcs)
{
	txgbe_write_pma(xpcs, TXGBE_MPLLA_CTL0, 0x21);
	txgbe_write_pma(xpcs, TXGBE_MPLLA_CTL3, 0);
	txgbe_modify_pma(xpcs, TXGBE_TX_GENCTL1, TXGBE_TX_GENCTL1_VBOOST_LVL,
			 FIELD_PREP(TXGBE_TX_GENCTL1_VBOOST_LVL, 0x5));
	txgbe_write_pma(xpcs, TXGBE_MISC_CTL0, TXGBE_MISC_CTL0_PLL |
			TXGBE_MISC_CTL0_CR_PARA_SEL | TXGBE_MISC_CTL0_RX_VREF(0xF));
	txgbe_write_pma(xpcs, TXGBE_VCO_CAL_LD0, 0x549);
	txgbe_write_pma(xpcs, TXGBE_VCO_CAL_REF0, 0x29);
	txgbe_write_pma(xpcs, TXGBE_TX_RATE_CTL, 0);
	txgbe_write_pma(xpcs, TXGBE_RX_RATE_CTL, 0);
	txgbe_write_pma(xpcs, TXGBE_TX_GEN_CTL2, TXGBE_TX_GEN_CTL2_TX0_WIDTH(3));
	txgbe_write_pma(xpcs, TXGBE_RX_GEN_CTL2, TXGBE_RX_GEN_CTL2_RX0_WIDTH(3));
	txgbe_write_pma(xpcs, TXGBE_MPLLA_CTL2, TXGBE_MPLLA_CTL2_DIV16P5_CLK_EN |
			TXGBE_MPLLA_CTL2_DIV10_CLK_EN);

	txgbe_write_pma(xpcs, TXGBE_RX_EQ_CTL0, TXGBE_RX_EQ_CTL0_CTLE_POLE(2) |
			TXGBE_RX_EQ_CTL0_CTLE_BOOST(5));
	txgbe_modify_pma(xpcs, TXGBE_RX_EQ_ATTN_CTL, TXGBE_RX_EQ_ATTN_LVL0, 0);
	txgbe_write_pma(xpcs, TXGBE_DFE_TAP_CTL0, 0xBE);
	txgbe_modify_pma(xpcs, TXGBE_AFE_DFE_ENABLE,
			 TXGBE_DFE_EN_0 | TXGBE_AFE_EN_0, 0);
	txgbe_modify_pma(xpcs, TXGBE_RX_EQ_CTL4, TXGBE_RX_EQ_CTL4_CONT_ADAPT0,
			 0);
}

static void txgbe_pma_config_1g(struct dw_xpcs *xpcs)
{
	txgbe_modify_pma(xpcs, TXGBE_TX_GENCTL1,
			 TXGBE_TX_GENCTL1_VBOOST_LVL |
			 TXGBE_TX_GENCTL1_VBOOST_EN0,
			 FIELD_PREP(TXGBE_TX_GENCTL1_VBOOST_LVL, 0x5));
	txgbe_write_pma(xpcs, TXGBE_MISC_CTL0, TXGBE_MISC_CTL0_PLL |
			TXGBE_MISC_CTL0_CR_PARA_SEL | TXGBE_MISC_CTL0_RX_VREF(0xF));

	txgbe_write_pma(xpcs, TXGBE_RX_EQ_CTL0, TXGBE_RX_EQ_CTL0_VGA1_GAIN(7) |
			TXGBE_RX_EQ_CTL0_VGA2_GAIN(7) | TXGBE_RX_EQ_CTL0_CTLE_BOOST(6));
	txgbe_modify_pma(xpcs, TXGBE_RX_EQ_ATTN_CTL, TXGBE_RX_EQ_ATTN_LVL0, 0);
	txgbe_write_pma(xpcs, TXGBE_DFE_TAP_CTL0, 0);
	txgbe_modify_pma(xpcs, TXGBE_RX_GEN_CTL3, TXGBE_RX_GEN_CTL3_LOS_TRSHLD0,
			 FIELD_PREP(TXGBE_RX_GEN_CTL3_LOS_TRSHLD0, 0x4));

	txgbe_write_pma(xpcs, TXGBE_MPLLA_CTL0, 0x20);
	txgbe_write_pma(xpcs, TXGBE_MPLLA_CTL3, 0x46);
	txgbe_write_pma(xpcs, TXGBE_VCO_CAL_LD0, 0x540);
	txgbe_write_pma(xpcs, TXGBE_VCO_CAL_REF0, 0x2A);
	txgbe_write_pma(xpcs, TXGBE_AFE_DFE_ENABLE, 0);
	txgbe_write_pma(xpcs, TXGBE_RX_EQ_CTL4, TXGBE_RX_EQ_CTL4_CONT_OFF_CAN0);
	txgbe_write_pma(xpcs, TXGBE_TX_RATE_CTL, TXGBE_TX_RATE_CTL_TX0_RATE(3));
	txgbe_write_pma(xpcs, TXGBE_RX_RATE_CTL, TXGBE_RX_RATE_CTL_RX0_RATE(3));
	txgbe_write_pma(xpcs, TXGBE_TX_GEN_CTL2, TXGBE_TX_GEN_CTL2_TX0_WIDTH(1));
	txgbe_write_pma(xpcs, TXGBE_RX_GEN_CTL2, TXGBE_RX_GEN_CTL2_RX0_WIDTH(1));
	txgbe_write_pma(xpcs, TXGBE_MPLLA_CTL2, TXGBE_MPLLA_CTL2_DIV10_CLK_EN);
}

static int txgbe_pcs_poll_power_up(struct dw_xpcs *xpcs)
{
	int val, ret;

	/* Wait xpcs power-up good */
	ret = read_poll_timeout(xpcs_read_vpcs, val,
				(val & DW_PSEQ_ST) == DW_PSEQ_ST_GOOD,
				10000, 1000000, false,
				xpcs, DW_VR_XS_PCS_DIG_STS);
	if (ret < 0)
		dev_err(&xpcs->mdiodev->dev, "xpcs power-up timeout\n");

	return ret;
}

static int txgbe_pma_init_done(struct dw_xpcs *xpcs)
{
	int val, ret;

	xpcs_write_vpcs(xpcs, DW_VR_XS_PCS_DIG_CTRL1, DW_VR_RST | DW_EN_VSMMD1);

	/* wait pma initialization done */
	ret = read_poll_timeout(xpcs_read_vpcs, val, !(val & DW_VR_RST),
				100000, 10000000, false,
				xpcs, DW_VR_XS_PCS_DIG_CTRL1);
	if (ret < 0)
		dev_err(&xpcs->mdiodev->dev, "xpcs pma initialization timeout\n");

	return ret;
}

static bool txgbe_xpcs_mode_quirk(struct dw_xpcs *xpcs)
{
	int ret;

	/* When txgbe do LAN reset, PCS will change to default 10GBASE-R mode */
	ret = xpcs_read(xpcs, MDIO_MMD_PCS, MDIO_CTRL2);
	ret &= MDIO_PCS_CTRL2_TYPE;
	if ((ret == MDIO_PCS_CTRL2_10GBR &&
	     xpcs->interface != PHY_INTERFACE_MODE_10GBASER) ||
	    xpcs->interface == PHY_INTERFACE_MODE_SGMII)
		return true;

	return false;
}

int txgbe_xpcs_switch_mode(struct dw_xpcs *xpcs, phy_interface_t interface)
{
	int ret;

	switch (interface) {
	case PHY_INTERFACE_MODE_10GBASER:
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_1000BASEX:
		break;
	default:
		return 0;
	}

	if (xpcs->interface == interface && !txgbe_xpcs_mode_quirk(xpcs))
		return 0;

	xpcs->interface = interface;

	ret = txgbe_pcs_poll_power_up(xpcs);
	if (ret < 0)
		return ret;

	if (interface == PHY_INTERFACE_MODE_10GBASER) {
		xpcs_write(xpcs, MDIO_MMD_PCS, MDIO_CTRL2, MDIO_PCS_CTRL2_10GBR);
		xpcs_modify(xpcs, MDIO_MMD_PMAPMD, MDIO_CTRL1,
			    MDIO_CTRL1_SPEED10G, MDIO_CTRL1_SPEED10G);
		txgbe_pma_config_10gbaser(xpcs);
	} else {
		xpcs_write(xpcs, MDIO_MMD_PCS, MDIO_CTRL2, MDIO_PCS_CTRL2_10GBX);
		xpcs_write(xpcs, MDIO_MMD_PMAPMD, MDIO_CTRL1, 0);
		xpcs_write(xpcs, MDIO_MMD_PCS, MDIO_CTRL1, 0);
		txgbe_pma_config_1g(xpcs);
	}

	return txgbe_pma_init_done(xpcs);
}
