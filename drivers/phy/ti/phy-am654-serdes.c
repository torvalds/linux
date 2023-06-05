// SPDX-License-Identifier: GPL-2.0
/*
 * PCIe SERDES driver for AM654x SoC
 *
 * Copyright (C) 2018 - 2019 Texas Instruments Incorporated - http://www.ti.com/
 * Author: Kishon Vijay Abraham I <kishon@ti.com>
 */

#include <dt-bindings/phy/phy.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/mfd/syscon.h>
#include <linux/mux/consumer.h>
#include <linux/of_address.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>

#define CMU_R004		0x4
#define CMU_R060		0x60
#define CMU_R07C		0x7c
#define CMU_R088		0x88
#define CMU_R0D0		0xd0
#define CMU_R0E8		0xe8

#define LANE_R048		0x248
#define LANE_R058		0x258
#define LANE_R06c		0x26c
#define LANE_R070		0x270
#define LANE_R070		0x270
#define LANE_R19C		0x39c

#define COMLANE_R004		0xa04
#define COMLANE_R138		0xb38
#define VERSION_VAL		0x70

#define COMLANE_R190		0xb90
#define COMLANE_R194		0xb94

#define COMRXEQ_R004		0x1404
#define COMRXEQ_R008		0x1408
#define COMRXEQ_R00C		0x140c
#define COMRXEQ_R014		0x1414
#define COMRXEQ_R018		0x1418
#define COMRXEQ_R01C		0x141c
#define COMRXEQ_R04C		0x144c
#define COMRXEQ_R088		0x1488
#define COMRXEQ_R094		0x1494
#define COMRXEQ_R098		0x1498

#define SERDES_CTRL		0x1fd0

#define WIZ_LANEXCTL_STS	0x1fe0
#define TX0_DISABLE_STATE	0x4
#define TX0_SLEEP_STATE		0x5
#define TX0_SNOOZE_STATE	0x6
#define TX0_ENABLE_STATE	0x7

#define RX0_DISABLE_STATE	0x4
#define RX0_SLEEP_STATE		0x5
#define RX0_SNOOZE_STATE	0x6
#define RX0_ENABLE_STATE	0x7

#define WIZ_PLL_CTRL		0x1ff4
#define PLL_DISABLE_STATE	0x4
#define PLL_SLEEP_STATE		0x5
#define PLL_SNOOZE_STATE	0x6
#define PLL_ENABLE_STATE	0x7

#define PLL_LOCK_TIME		100000	/* in microseconds */
#define SLEEP_TIME		100	/* in microseconds */

#define LANE_USB3		0x0
#define LANE_PCIE0_LANE0	0x1

#define LANE_PCIE1_LANE0	0x0
#define LANE_PCIE0_LANE1	0x1

#define SERDES_NUM_CLOCKS	3

#define AM654_SERDES_CTRL_CLKSEL_MASK	GENMASK(7, 4)
#define AM654_SERDES_CTRL_CLKSEL_SHIFT	4

struct serdes_am654_clk_mux {
	struct clk_hw	hw;
	struct regmap	*regmap;
	unsigned int	reg;
	int		clk_id;
	struct clk_init_data clk_data;
};

#define to_serdes_am654_clk_mux(_hw)	\
		container_of(_hw, struct serdes_am654_clk_mux, hw)

static const struct regmap_config serdes_am654_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.fast_io = true,
	.max_register = 0x1ffc,
};

enum serdes_am654_fields {
	/* CMU PLL Control */
	CMU_PLL_CTRL,

	LANE_PLL_CTRL_RXEQ_RXIDLE,

	/* CMU VCO bias current and VREG setting */
	AHB_PMA_CM_VCO_VBIAS_VREG,
	AHB_PMA_CM_VCO_BIAS_VREG,

	AHB_PMA_CM_SR,
	AHB_SSC_GEN_Z_O_20_13,

	/* AHB PMA Lane Configuration */
	AHB_PMA_LN_AGC_THSEL_VREGH,

	/* AGC and Signal detect threshold for Gen3 */
	AHB_PMA_LN_GEN3_AGC_SD_THSEL,

	AHB_PMA_LN_RX_SELR_GEN3,
	AHB_PMA_LN_TX_DRV,

	/* CMU Master Reset */
	CMU_MASTER_CDN,

	/* P2S ring buffer initial startup pointer difference */
	P2S_RBUF_PTR_DIFF,

	CONFIG_VERSION,

	/* Lane 1 Master Reset */
	L1_MASTER_CDN,

	/* CMU OK Status */
	CMU_OK_I_0,

	/* Mid-speed initial calibration control */
	COMRXEQ_MS_INIT_CTRL_7_0,

	/* High-speed initial calibration control */
	COMRXEQ_HS_INIT_CAL_7_0,

	/* Mid-speed recalibration control */
	COMRXEQ_MS_RECAL_CTRL_7_0,

	/* High-speed recalibration control */
	COMRXEQ_HS_RECAL_CTRL_7_0,

	/* ATT configuration */
	COMRXEQ_CSR_ATT_CONFIG,

	/* Edge based boost adaptation window length */
	COMRXEQ_CSR_EBSTADAPT_WIN_LEN,

	/* COMRXEQ control 3 & 4 */
	COMRXEQ_CTRL_3_4,

	/* COMRXEQ control 14, 15 and 16*/
	COMRXEQ_CTRL_14_15_16,

	/* Threshold for errors in pattern data  */
	COMRXEQ_CSR_DLEV_ERR_THRESH,

	/* COMRXEQ control 25 */
	COMRXEQ_CTRL_25,

	/* Mid-speed rate change calibration control */
	CSR_RXEQ_RATE_CHANGE_CAL_RUN_RATE2_O,

	/* High-speed rate change calibration control */
	COMRXEQ_HS_RCHANGE_CTRL_7_0,

	/* Serdes reset */
	POR_EN,

	/* Tx Enable Value */
	TX0_ENABLE,

	/* Rx Enable Value */
	RX0_ENABLE,

	/* PLL Enable Value */
	PLL_ENABLE,

	/* PLL ready for use */
	PLL_OK,

	/* sentinel */
	MAX_FIELDS

};

static const struct reg_field serdes_am654_reg_fields[] = {
	[CMU_PLL_CTRL]			= REG_FIELD(CMU_R004, 8, 15),
	[AHB_PMA_CM_VCO_VBIAS_VREG]	= REG_FIELD(CMU_R060, 8, 15),
	[CMU_MASTER_CDN]		= REG_FIELD(CMU_R07C, 24, 31),
	[AHB_PMA_CM_VCO_BIAS_VREG]	= REG_FIELD(CMU_R088, 24, 31),
	[AHB_PMA_CM_SR]			= REG_FIELD(CMU_R0D0, 24, 31),
	[AHB_SSC_GEN_Z_O_20_13]		= REG_FIELD(CMU_R0E8, 8, 15),
	[LANE_PLL_CTRL_RXEQ_RXIDLE]	= REG_FIELD(LANE_R048, 8, 15),
	[AHB_PMA_LN_AGC_THSEL_VREGH]	= REG_FIELD(LANE_R058, 16, 23),
	[AHB_PMA_LN_GEN3_AGC_SD_THSEL]	= REG_FIELD(LANE_R06c, 0, 7),
	[AHB_PMA_LN_RX_SELR_GEN3]	= REG_FIELD(LANE_R070, 16, 23),
	[AHB_PMA_LN_TX_DRV]		= REG_FIELD(LANE_R19C, 16, 23),
	[P2S_RBUF_PTR_DIFF]		= REG_FIELD(COMLANE_R004, 0, 7),
	[CONFIG_VERSION]		= REG_FIELD(COMLANE_R138, 16, 23),
	[L1_MASTER_CDN]			= REG_FIELD(COMLANE_R190, 8, 15),
	[CMU_OK_I_0]			= REG_FIELD(COMLANE_R194, 19, 19),
	[COMRXEQ_MS_INIT_CTRL_7_0]	= REG_FIELD(COMRXEQ_R004, 24, 31),
	[COMRXEQ_HS_INIT_CAL_7_0]	= REG_FIELD(COMRXEQ_R008, 0, 7),
	[COMRXEQ_MS_RECAL_CTRL_7_0]	= REG_FIELD(COMRXEQ_R00C, 8, 15),
	[COMRXEQ_HS_RECAL_CTRL_7_0]	= REG_FIELD(COMRXEQ_R00C, 16, 23),
	[COMRXEQ_CSR_ATT_CONFIG]	= REG_FIELD(COMRXEQ_R014, 16, 23),
	[COMRXEQ_CSR_EBSTADAPT_WIN_LEN]	= REG_FIELD(COMRXEQ_R018, 16, 23),
	[COMRXEQ_CTRL_3_4]		= REG_FIELD(COMRXEQ_R01C, 8, 15),
	[COMRXEQ_CTRL_14_15_16]		= REG_FIELD(COMRXEQ_R04C, 0, 7),
	[COMRXEQ_CSR_DLEV_ERR_THRESH]	= REG_FIELD(COMRXEQ_R088, 16, 23),
	[COMRXEQ_CTRL_25]		= REG_FIELD(COMRXEQ_R094, 24, 31),
	[CSR_RXEQ_RATE_CHANGE_CAL_RUN_RATE2_O] = REG_FIELD(COMRXEQ_R098, 8, 15),
	[COMRXEQ_HS_RCHANGE_CTRL_7_0]	= REG_FIELD(COMRXEQ_R098, 16, 23),
	[POR_EN]			= REG_FIELD(SERDES_CTRL, 29, 29),
	[TX0_ENABLE]			= REG_FIELD(WIZ_LANEXCTL_STS, 29, 31),
	[RX0_ENABLE]			= REG_FIELD(WIZ_LANEXCTL_STS, 13, 15),
	[PLL_ENABLE]			= REG_FIELD(WIZ_PLL_CTRL, 29, 31),
	[PLL_OK]			= REG_FIELD(WIZ_PLL_CTRL, 28, 28),
};

struct serdes_am654 {
	struct regmap		*regmap;
	struct regmap_field	*fields[MAX_FIELDS];

	struct device		*dev;
	struct mux_control	*control;
	bool			busy;
	u32			type;
	struct device_node	*of_node;
	struct clk_onecell_data	clk_data;
	struct clk		*clks[SERDES_NUM_CLOCKS];
};

static int serdes_am654_enable_pll(struct serdes_am654 *phy)
{
	int ret;
	u32 val;

	ret = regmap_field_write(phy->fields[PLL_ENABLE], PLL_ENABLE_STATE);
	if (ret)
		return ret;

	return regmap_field_read_poll_timeout(phy->fields[PLL_OK], val, val,
					      1000, PLL_LOCK_TIME);
}

static void serdes_am654_disable_pll(struct serdes_am654 *phy)
{
	struct device *dev = phy->dev;
	int ret;

	ret = regmap_field_write(phy->fields[PLL_ENABLE], PLL_DISABLE_STATE);
	if (ret)
		dev_err(dev, "Failed to disable PLL\n");
}

static int serdes_am654_enable_txrx(struct serdes_am654 *phy)
{
	int ret = 0;

	/* Enable TX */
	ret |= regmap_field_write(phy->fields[TX0_ENABLE], TX0_ENABLE_STATE);

	/* Enable RX */
	ret |= regmap_field_write(phy->fields[RX0_ENABLE], RX0_ENABLE_STATE);

	if (ret)
		return -EIO;

	return 0;
}

static int serdes_am654_disable_txrx(struct serdes_am654 *phy)
{
	int ret = 0;

	/* Disable TX */
	ret |= regmap_field_write(phy->fields[TX0_ENABLE], TX0_DISABLE_STATE);

	/* Disable RX */
	ret |= regmap_field_write(phy->fields[RX0_ENABLE], RX0_DISABLE_STATE);

	if (ret)
		return -EIO;

	return 0;
}

static int serdes_am654_power_on(struct phy *x)
{
	struct serdes_am654 *phy = phy_get_drvdata(x);
	struct device *dev = phy->dev;
	int ret;
	u32 val;

	ret = serdes_am654_enable_pll(phy);
	if (ret) {
		dev_err(dev, "Failed to enable PLL\n");
		return ret;
	}

	ret = serdes_am654_enable_txrx(phy);
	if (ret) {
		dev_err(dev, "Failed to enable TX RX\n");
		return ret;
	}

	return regmap_field_read_poll_timeout(phy->fields[CMU_OK_I_0], val,
					      val, SLEEP_TIME, PLL_LOCK_TIME);
}

static int serdes_am654_power_off(struct phy *x)
{
	struct serdes_am654 *phy = phy_get_drvdata(x);

	serdes_am654_disable_txrx(phy);
	serdes_am654_disable_pll(phy);

	return 0;
}

#define SERDES_AM654_CFG(offset, a, b, val) \
	regmap_update_bits(phy->regmap, (offset),\
			   GENMASK((a), (b)), (val) << (b))

static int serdes_am654_usb3_init(struct serdes_am654 *phy)
{
	SERDES_AM654_CFG(0x0000, 31, 24, 0x17);
	SERDES_AM654_CFG(0x0004, 15, 8, 0x02);
	SERDES_AM654_CFG(0x0004, 7, 0, 0x0e);
	SERDES_AM654_CFG(0x0008, 23, 16, 0x2e);
	SERDES_AM654_CFG(0x0008, 31, 24, 0x2e);
	SERDES_AM654_CFG(0x0060, 7, 0, 0x4b);
	SERDES_AM654_CFG(0x0060, 15, 8, 0x98);
	SERDES_AM654_CFG(0x0060, 23, 16, 0x60);
	SERDES_AM654_CFG(0x00d0, 31, 24, 0x45);
	SERDES_AM654_CFG(0x00e8, 15, 8, 0x0e);
	SERDES_AM654_CFG(0x0220, 7, 0, 0x34);
	SERDES_AM654_CFG(0x0220, 15, 8, 0x34);
	SERDES_AM654_CFG(0x0220, 31, 24, 0x37);
	SERDES_AM654_CFG(0x0224, 7, 0, 0x37);
	SERDES_AM654_CFG(0x0224, 15, 8, 0x37);
	SERDES_AM654_CFG(0x0228, 23, 16, 0x37);
	SERDES_AM654_CFG(0x0228, 31, 24, 0x37);
	SERDES_AM654_CFG(0x022c, 7, 0, 0x37);
	SERDES_AM654_CFG(0x022c, 15, 8, 0x37);
	SERDES_AM654_CFG(0x0230, 15, 8, 0x2a);
	SERDES_AM654_CFG(0x0230, 23, 16, 0x2a);
	SERDES_AM654_CFG(0x0240, 23, 16, 0x10);
	SERDES_AM654_CFG(0x0240, 31, 24, 0x34);
	SERDES_AM654_CFG(0x0244, 7, 0, 0x40);
	SERDES_AM654_CFG(0x0244, 23, 16, 0x34);
	SERDES_AM654_CFG(0x0248, 15, 8, 0x0d);
	SERDES_AM654_CFG(0x0258, 15, 8, 0x16);
	SERDES_AM654_CFG(0x0258, 23, 16, 0x84);
	SERDES_AM654_CFG(0x0258, 31, 24, 0xf2);
	SERDES_AM654_CFG(0x025c, 7, 0, 0x21);
	SERDES_AM654_CFG(0x0260, 7, 0, 0x27);
	SERDES_AM654_CFG(0x0260, 15, 8, 0x04);
	SERDES_AM654_CFG(0x0268, 15, 8, 0x04);
	SERDES_AM654_CFG(0x0288, 15, 8, 0x2c);
	SERDES_AM654_CFG(0x0330, 31, 24, 0xa0);
	SERDES_AM654_CFG(0x0338, 23, 16, 0x03);
	SERDES_AM654_CFG(0x0338, 31, 24, 0x00);
	SERDES_AM654_CFG(0x033c, 7, 0, 0x00);
	SERDES_AM654_CFG(0x0344, 31, 24, 0x18);
	SERDES_AM654_CFG(0x034c, 7, 0, 0x18);
	SERDES_AM654_CFG(0x039c, 23, 16, 0x3b);
	SERDES_AM654_CFG(0x0a04, 7, 0, 0x03);
	SERDES_AM654_CFG(0x0a14, 31, 24, 0x3c);
	SERDES_AM654_CFG(0x0a18, 15, 8, 0x3c);
	SERDES_AM654_CFG(0x0a38, 7, 0, 0x3e);
	SERDES_AM654_CFG(0x0a38, 15, 8, 0x3e);
	SERDES_AM654_CFG(0x0ae0, 7, 0, 0x07);
	SERDES_AM654_CFG(0x0b6c, 23, 16, 0xcd);
	SERDES_AM654_CFG(0x0b6c, 31, 24, 0x04);
	SERDES_AM654_CFG(0x0b98, 23, 16, 0x03);
	SERDES_AM654_CFG(0x1400, 7, 0, 0x3f);
	SERDES_AM654_CFG(0x1404, 23, 16, 0x6f);
	SERDES_AM654_CFG(0x1404, 31, 24, 0x6f);
	SERDES_AM654_CFG(0x140c, 7, 0, 0x6f);
	SERDES_AM654_CFG(0x140c, 15, 8, 0x6f);
	SERDES_AM654_CFG(0x1410, 15, 8, 0x27);
	SERDES_AM654_CFG(0x1414, 7, 0, 0x0c);
	SERDES_AM654_CFG(0x1414, 23, 16, 0x07);
	SERDES_AM654_CFG(0x1418, 23, 16, 0x40);
	SERDES_AM654_CFG(0x141c, 7, 0, 0x00);
	SERDES_AM654_CFG(0x141c, 15, 8, 0x1f);
	SERDES_AM654_CFG(0x1428, 31, 24, 0x08);
	SERDES_AM654_CFG(0x1434, 31, 24, 0x00);
	SERDES_AM654_CFG(0x1444, 7, 0, 0x94);
	SERDES_AM654_CFG(0x1460, 31, 24, 0x7f);
	SERDES_AM654_CFG(0x1464, 7, 0, 0x43);
	SERDES_AM654_CFG(0x1464, 23, 16, 0x6f);
	SERDES_AM654_CFG(0x1464, 31, 24, 0x43);
	SERDES_AM654_CFG(0x1484, 23, 16, 0x8f);
	SERDES_AM654_CFG(0x1498, 7, 0, 0x4f);
	SERDES_AM654_CFG(0x1498, 23, 16, 0x4f);
	SERDES_AM654_CFG(0x007c, 31, 24, 0x0d);
	SERDES_AM654_CFG(0x0b90, 15, 8, 0x0f);

	return 0;
}

static int serdes_am654_pcie_init(struct serdes_am654 *phy)
{
	int ret = 0;

	ret |= regmap_field_write(phy->fields[CMU_PLL_CTRL], 0x2);
	ret |= regmap_field_write(phy->fields[AHB_PMA_CM_VCO_VBIAS_VREG], 0x98);
	ret |= regmap_field_write(phy->fields[AHB_PMA_CM_VCO_BIAS_VREG], 0x98);
	ret |= regmap_field_write(phy->fields[AHB_PMA_CM_SR], 0x45);
	ret |= regmap_field_write(phy->fields[AHB_SSC_GEN_Z_O_20_13], 0xe);
	ret |= regmap_field_write(phy->fields[LANE_PLL_CTRL_RXEQ_RXIDLE], 0x5);
	ret |= regmap_field_write(phy->fields[AHB_PMA_LN_AGC_THSEL_VREGH], 0x83);
	ret |= regmap_field_write(phy->fields[AHB_PMA_LN_GEN3_AGC_SD_THSEL], 0x83);
	ret |= regmap_field_write(phy->fields[AHB_PMA_LN_RX_SELR_GEN3],	0x81);
	ret |= regmap_field_write(phy->fields[AHB_PMA_LN_TX_DRV], 0x3b);
	ret |= regmap_field_write(phy->fields[P2S_RBUF_PTR_DIFF], 0x3);
	ret |= regmap_field_write(phy->fields[CONFIG_VERSION], VERSION_VAL);
	ret |= regmap_field_write(phy->fields[COMRXEQ_MS_INIT_CTRL_7_0], 0xf);
	ret |= regmap_field_write(phy->fields[COMRXEQ_HS_INIT_CAL_7_0], 0x4f);
	ret |= regmap_field_write(phy->fields[COMRXEQ_MS_RECAL_CTRL_7_0], 0xf);
	ret |= regmap_field_write(phy->fields[COMRXEQ_HS_RECAL_CTRL_7_0], 0x4f);
	ret |= regmap_field_write(phy->fields[COMRXEQ_CSR_ATT_CONFIG], 0x7);
	ret |= regmap_field_write(phy->fields[COMRXEQ_CSR_EBSTADAPT_WIN_LEN], 0x7f);
	ret |= regmap_field_write(phy->fields[COMRXEQ_CTRL_3_4], 0xf);
	ret |= regmap_field_write(phy->fields[COMRXEQ_CTRL_14_15_16], 0x9a);
	ret |= regmap_field_write(phy->fields[COMRXEQ_CSR_DLEV_ERR_THRESH], 0x32);
	ret |= regmap_field_write(phy->fields[COMRXEQ_CTRL_25], 0x80);
	ret |= regmap_field_write(phy->fields[CSR_RXEQ_RATE_CHANGE_CAL_RUN_RATE2_O], 0xf);
	ret |= regmap_field_write(phy->fields[COMRXEQ_HS_RCHANGE_CTRL_7_0], 0x4f);
	ret |= regmap_field_write(phy->fields[CMU_MASTER_CDN], 0x1);
	ret |= regmap_field_write(phy->fields[L1_MASTER_CDN], 0x2);

	if (ret)
		return -EIO;

	return 0;
}

static int serdes_am654_init(struct phy *x)
{
	struct serdes_am654 *phy = phy_get_drvdata(x);

	switch (phy->type) {
	case PHY_TYPE_PCIE:
		return serdes_am654_pcie_init(phy);
	case PHY_TYPE_USB3:
		return serdes_am654_usb3_init(phy);
	default:
		return -EINVAL;
	}
}

static int serdes_am654_reset(struct phy *x)
{
	struct serdes_am654 *phy = phy_get_drvdata(x);
	int ret = 0;

	serdes_am654_disable_pll(phy);
	serdes_am654_disable_txrx(phy);

	ret |= regmap_field_write(phy->fields[POR_EN], 0x1);

	mdelay(1);

	ret |= regmap_field_write(phy->fields[POR_EN], 0x0);

	if (ret)
		return -EIO;

	return 0;
}

static void serdes_am654_release(struct phy *x)
{
	struct serdes_am654 *phy = phy_get_drvdata(x);

	phy->type = PHY_NONE;
	phy->busy = false;
	mux_control_deselect(phy->control);
}

static struct phy *serdes_am654_xlate(struct device *dev,
				      struct of_phandle_args *args)
{
	struct serdes_am654 *am654_phy;
	struct phy *phy;
	int ret;

	phy = of_phy_simple_xlate(dev, args);
	if (IS_ERR(phy))
		return phy;

	am654_phy = phy_get_drvdata(phy);
	if (am654_phy->busy)
		return ERR_PTR(-EBUSY);

	ret = mux_control_select(am654_phy->control, args->args[1]);
	if (ret) {
		dev_err(dev, "Failed to select SERDES Lane Function\n");
		return ERR_PTR(ret);
	}

	am654_phy->busy = true;
	am654_phy->type = args->args[0];

	return phy;
}

static const struct phy_ops ops = {
	.reset		= serdes_am654_reset,
	.init		= serdes_am654_init,
	.power_on	= serdes_am654_power_on,
	.power_off	= serdes_am654_power_off,
	.release	= serdes_am654_release,
	.owner		= THIS_MODULE,
};

#define SERDES_NUM_MUX_COMBINATIONS 16

#define LICLK 0
#define EXT_REFCLK 1
#define RICLK 2

static const int
serdes_am654_mux_table[SERDES_NUM_MUX_COMBINATIONS][SERDES_NUM_CLOCKS] = {
	/*
	 * Each combination maps to one of
	 * "Figure 12-1986. SerDes Reference Clock Distribution"
	 * in TRM.
	 */
	 /* Parent of CMU refclk, Left output, Right output
	  * either of EXT_REFCLK, LICLK, RICLK
	  */
	{ EXT_REFCLK, EXT_REFCLK, EXT_REFCLK },	/* 0000 */
	{ RICLK, EXT_REFCLK, EXT_REFCLK },	/* 0001 */
	{ EXT_REFCLK, RICLK, LICLK },		/* 0010 */
	{ RICLK, RICLK, EXT_REFCLK },		/* 0011 */
	{ LICLK, EXT_REFCLK, EXT_REFCLK },	/* 0100 */
	{ EXT_REFCLK, EXT_REFCLK, EXT_REFCLK },	/* 0101 */
	{ LICLK, RICLK, LICLK },		/* 0110 */
	{ EXT_REFCLK, RICLK, LICLK },		/* 0111 */
	{ EXT_REFCLK, EXT_REFCLK, LICLK },	/* 1000 */
	{ RICLK, EXT_REFCLK, LICLK },		/* 1001 */
	{ EXT_REFCLK, RICLK, EXT_REFCLK },	/* 1010 */
	{ RICLK, RICLK, EXT_REFCLK },		/* 1011 */
	{ LICLK, EXT_REFCLK, LICLK },		/* 1100 */
	{ EXT_REFCLK, EXT_REFCLK, LICLK },	/* 1101 */
	{ LICLK, RICLK, EXT_REFCLK },		/* 1110 */
	{ EXT_REFCLK, RICLK, EXT_REFCLK },	/* 1111 */
};

static u8 serdes_am654_clk_mux_get_parent(struct clk_hw *hw)
{
	struct serdes_am654_clk_mux *mux = to_serdes_am654_clk_mux(hw);
	struct regmap *regmap = mux->regmap;
	unsigned int reg = mux->reg;
	unsigned int val;

	regmap_read(regmap, reg, &val);
	val &= AM654_SERDES_CTRL_CLKSEL_MASK;
	val >>= AM654_SERDES_CTRL_CLKSEL_SHIFT;

	return serdes_am654_mux_table[val][mux->clk_id];
}

static int serdes_am654_clk_mux_set_parent(struct clk_hw *hw, u8 index)
{
	struct serdes_am654_clk_mux *mux = to_serdes_am654_clk_mux(hw);
	struct regmap *regmap = mux->regmap;
	const char *name = clk_hw_get_name(hw);
	unsigned int reg = mux->reg;
	int clk_id = mux->clk_id;
	int parents[SERDES_NUM_CLOCKS];
	const int *p;
	u32 val;
	int found, i;
	int ret;

	/* get existing setting */
	regmap_read(regmap, reg, &val);
	val &= AM654_SERDES_CTRL_CLKSEL_MASK;
	val >>= AM654_SERDES_CTRL_CLKSEL_SHIFT;

	for (i = 0; i < SERDES_NUM_CLOCKS; i++)
		parents[i] = serdes_am654_mux_table[val][i];

	/* change parent of this clock. others left intact */
	parents[clk_id] = index;

	/* Find the match */
	for (val = 0; val < SERDES_NUM_MUX_COMBINATIONS; val++) {
		p = serdes_am654_mux_table[val];
		found = 1;
		for (i = 0; i < SERDES_NUM_CLOCKS; i++) {
			if (parents[i] != p[i]) {
				found = 0;
				break;
			}
		}

		if (found)
			break;
	}

	if (!found) {
		/*
		 * This can never happen, unless we missed
		 * a valid combination in serdes_am654_mux_table.
		 */
		WARN(1, "Failed to find the parent of %s clock\n", name);
		return -EINVAL;
	}

	val <<= AM654_SERDES_CTRL_CLKSEL_SHIFT;
	ret = regmap_update_bits(regmap, reg, AM654_SERDES_CTRL_CLKSEL_MASK,
				 val);

	return ret;
}

static const struct clk_ops serdes_am654_clk_mux_ops = {
	.set_parent = serdes_am654_clk_mux_set_parent,
	.get_parent = serdes_am654_clk_mux_get_parent,
};

static int serdes_am654_clk_register(struct serdes_am654 *am654_phy,
				     const char *clock_name, int clock_num)
{
	struct device_node *node = am654_phy->of_node;
	struct device *dev = am654_phy->dev;
	struct serdes_am654_clk_mux *mux;
	struct device_node *regmap_node;
	const char **parent_names;
	struct clk_init_data *init;
	unsigned int num_parents;
	struct regmap *regmap;
	const __be32 *addr;
	unsigned int reg;
	struct clk *clk;
	int ret = 0;

	mux = devm_kzalloc(dev, sizeof(*mux), GFP_KERNEL);
	if (!mux)
		return -ENOMEM;

	init = &mux->clk_data;

	regmap_node = of_parse_phandle(node, "ti,serdes-clk", 0);
	if (!regmap_node) {
		dev_err(dev, "Fail to get serdes-clk node\n");
		ret = -ENODEV;
		goto out_put_node;
	}

	regmap = syscon_node_to_regmap(regmap_node->parent);
	if (IS_ERR(regmap)) {
		dev_err(dev, "Fail to get Syscon regmap\n");
		ret = PTR_ERR(regmap);
		goto out_put_node;
	}

	num_parents = of_clk_get_parent_count(node);
	if (num_parents < 2) {
		dev_err(dev, "SERDES clock must have parents\n");
		ret = -EINVAL;
		goto out_put_node;
	}

	parent_names = devm_kzalloc(dev, (sizeof(char *) * num_parents),
				    GFP_KERNEL);
	if (!parent_names) {
		ret = -ENOMEM;
		goto out_put_node;
	}

	of_clk_parent_fill(node, parent_names, num_parents);

	addr = of_get_address(regmap_node, 0, NULL, NULL);
	if (!addr) {
		ret = -EINVAL;
		goto out_put_node;
	}

	reg = be32_to_cpu(*addr);

	init->ops = &serdes_am654_clk_mux_ops;
	init->flags = CLK_SET_RATE_NO_REPARENT;
	init->parent_names = parent_names;
	init->num_parents = num_parents;
	init->name = clock_name;

	mux->regmap = regmap;
	mux->reg = reg;
	mux->clk_id = clock_num;
	mux->hw.init = init;

	clk = devm_clk_register(dev, &mux->hw);
	if (IS_ERR(clk)) {
		ret = PTR_ERR(clk);
		goto out_put_node;
	}

	am654_phy->clks[clock_num] = clk;

out_put_node:
	of_node_put(regmap_node);
	return ret;
}

static const struct of_device_id serdes_am654_id_table[] = {
	{
		.compatible = "ti,phy-am654-serdes",
	},
	{}
};
MODULE_DEVICE_TABLE(of, serdes_am654_id_table);

static int serdes_am654_regfield_init(struct serdes_am654 *am654_phy)
{
	struct regmap *regmap = am654_phy->regmap;
	struct device *dev = am654_phy->dev;
	int i;

	for (i = 0; i < MAX_FIELDS; i++) {
		am654_phy->fields[i] = devm_regmap_field_alloc(dev,
							       regmap,
							       serdes_am654_reg_fields[i]);
		if (IS_ERR(am654_phy->fields[i])) {
			dev_err(dev, "Unable to allocate regmap field %d\n", i);
			return PTR_ERR(am654_phy->fields[i]);
		}
	}

	return 0;
}

static int serdes_am654_probe(struct platform_device *pdev)
{
	struct phy_provider *phy_provider;
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct clk_onecell_data *clk_data;
	struct serdes_am654 *am654_phy;
	struct mux_control *control;
	const char *clock_name;
	struct regmap *regmap;
	void __iomem *base;
	struct phy *phy;
	int ret;
	int i;

	am654_phy = devm_kzalloc(dev, sizeof(*am654_phy), GFP_KERNEL);
	if (!am654_phy)
		return -ENOMEM;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	regmap = devm_regmap_init_mmio(dev, base, &serdes_am654_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(dev, "Failed to initialize regmap\n");
		return PTR_ERR(regmap);
	}

	control = devm_mux_control_get(dev, NULL);
	if (IS_ERR(control))
		return PTR_ERR(control);

	am654_phy->dev = dev;
	am654_phy->of_node = node;
	am654_phy->regmap = regmap;
	am654_phy->control = control;
	am654_phy->type = PHY_NONE;

	ret = serdes_am654_regfield_init(am654_phy);
	if (ret) {
		dev_err(dev, "Failed to initialize regfields\n");
		return ret;
	}

	platform_set_drvdata(pdev, am654_phy);

	for (i = 0; i < SERDES_NUM_CLOCKS; i++) {
		ret = of_property_read_string_index(node, "clock-output-names",
						    i, &clock_name);
		if (ret) {
			dev_err(dev, "Failed to get clock name\n");
			return ret;
		}

		ret = serdes_am654_clk_register(am654_phy, clock_name, i);
		if (ret) {
			dev_err(dev, "Failed to initialize clock %s\n",
				clock_name);
			return ret;
		}
	}

	clk_data = &am654_phy->clk_data;
	clk_data->clks = am654_phy->clks;
	clk_data->clk_num = SERDES_NUM_CLOCKS;
	ret = of_clk_add_provider(node, of_clk_src_onecell_get, clk_data);
	if (ret)
		return ret;

	pm_runtime_enable(dev);

	phy = devm_phy_create(dev, NULL, &ops);
	if (IS_ERR(phy)) {
		ret = PTR_ERR(phy);
		goto clk_err;
	}

	phy_set_drvdata(phy, am654_phy);
	phy_provider = devm_of_phy_provider_register(dev, serdes_am654_xlate);
	if (IS_ERR(phy_provider)) {
		ret = PTR_ERR(phy_provider);
		goto clk_err;
	}

	return 0;

clk_err:
	of_clk_del_provider(node);
	pm_runtime_disable(dev);
	return ret;
}

static void serdes_am654_remove(struct platform_device *pdev)
{
	struct serdes_am654 *am654_phy = platform_get_drvdata(pdev);
	struct device_node *node = am654_phy->of_node;

	pm_runtime_disable(&pdev->dev);
	of_clk_del_provider(node);
}

static struct platform_driver serdes_am654_driver = {
	.probe		= serdes_am654_probe,
	.remove_new	= serdes_am654_remove,
	.driver		= {
		.name	= "phy-am654",
		.of_match_table = serdes_am654_id_table,
	},
};
module_platform_driver(serdes_am654_driver);

MODULE_AUTHOR("Texas Instruments Inc.");
MODULE_DESCRIPTION("TI AM654x SERDES driver");
MODULE_LICENSE("GPL v2");
