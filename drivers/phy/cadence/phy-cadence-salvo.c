// SPDX-License-Identifier: GPL-2.0+
/*
 * Salvo PHY is a 28nm PHY, it is a legacy PHY, and only
 * for USB3 and USB2.
 *
 * Copyright (c) 2019-2020 NXP
 */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_platform.h>

/* PHY register definition */
#define PHY_PMA_CMN_CTRL1			0xC800
#define TB_ADDR_CMN_DIAG_HSCLK_SEL		0x01e0
#define TB_ADDR_CMN_PLL0_VCOCAL_INIT_TMR	0x0084
#define TB_ADDR_CMN_PLL0_VCOCAL_ITER_TMR	0x0085
#define TB_ADDR_CMN_PLL0_INTDIV	                0x0094
#define TB_ADDR_CMN_PLL0_FRACDIV		0x0095
#define TB_ADDR_CMN_PLL0_HIGH_THR		0x0096
#define TB_ADDR_CMN_PLL0_SS_CTRL1		0x0098
#define TB_ADDR_CMN_PLL0_SS_CTRL2		0x0099
#define TB_ADDR_CMN_PLL0_DSM_DIAG		0x0097
#define TB_ADDR_CMN_DIAG_PLL0_OVRD		0x01c2
#define TB_ADDR_CMN_DIAG_PLL0_FBH_OVRD		0x01c0
#define TB_ADDR_CMN_DIAG_PLL0_FBL_OVRD		0x01c1
#define TB_ADDR_CMN_DIAG_PLL0_V2I_TUNE          0x01C5
#define TB_ADDR_CMN_DIAG_PLL0_CP_TUNE           0x01C6
#define TB_ADDR_CMN_DIAG_PLL0_LF_PROG           0x01C7
#define TB_ADDR_CMN_DIAG_PLL0_TEST_MODE		0x01c4
#define TB_ADDR_CMN_PSM_CLK_CTRL		0x0061
#define TB_ADDR_XCVR_DIAG_RX_LANE_CAL_RST_TMR	0x40ea
#define TB_ADDR_XCVR_PSM_RCTRL	                0x4001
#define TB_ADDR_TX_PSC_A0		        0x4100
#define TB_ADDR_TX_PSC_A1		        0x4101
#define TB_ADDR_TX_PSC_A2		        0x4102
#define TB_ADDR_TX_PSC_A3		        0x4103
#define TB_ADDR_TX_DIAG_ECTRL_OVRD		0x41f5
#define TB_ADDR_TX_PSC_CAL		        0x4106
#define TB_ADDR_TX_PSC_RDY		        0x4107
#define TB_ADDR_RX_PSC_A0	                0x8000
#define TB_ADDR_RX_PSC_A1	                0x8001
#define TB_ADDR_RX_PSC_A2	                0x8002
#define TB_ADDR_RX_PSC_A3	                0x8003
#define TB_ADDR_RX_PSC_CAL	                0x8006
#define TB_ADDR_RX_PSC_RDY	                0x8007
#define TB_ADDR_TX_TXCC_MGNLS_MULT_000		0x4058
#define TB_ADDR_TX_DIAG_BGREF_PREDRV_DELAY	0x41e7
#define TB_ADDR_RX_SLC_CU_ITER_TMR		0x80e3
#define TB_ADDR_RX_SIGDET_HL_FILT_TMR		0x8090
#define TB_ADDR_RX_SAMP_DAC_CTRL		0x8058
#define TB_ADDR_RX_DIAG_SIGDET_TUNE		0x81dc
#define TB_ADDR_RX_DIAG_LFPSDET_TUNE2		0x81df
#define TB_ADDR_RX_DIAG_BS_TM	                0x81f5
#define TB_ADDR_RX_DIAG_DFE_CTRL1		0x81d3
#define TB_ADDR_RX_DIAG_ILL_IQE_TRIM4		0x81c7
#define TB_ADDR_RX_DIAG_ILL_E_TRIM0		0x81c2
#define TB_ADDR_RX_DIAG_ILL_IQ_TRIM0		0x81c1
#define TB_ADDR_RX_DIAG_ILL_IQE_TRIM6		0x81c9
#define TB_ADDR_RX_DIAG_RXFE_TM3		0x81f8
#define TB_ADDR_RX_DIAG_RXFE_TM4		0x81f9
#define TB_ADDR_RX_DIAG_LFPSDET_TUNE		0x81dd
#define TB_ADDR_RX_DIAG_DFE_CTRL3		0x81d5
#define TB_ADDR_RX_DIAG_SC2C_DELAY		0x81e1
#define TB_ADDR_RX_REE_VGA_GAIN_NODFE		0x81bf
#define TB_ADDR_XCVR_PSM_CAL_TMR		0x4002
#define TB_ADDR_XCVR_PSM_A0BYP_TMR		0x4004
#define TB_ADDR_XCVR_PSM_A0IN_TMR		0x4003
#define TB_ADDR_XCVR_PSM_A1IN_TMR		0x4005
#define TB_ADDR_XCVR_PSM_A2IN_TMR		0x4006
#define TB_ADDR_XCVR_PSM_A3IN_TMR		0x4007
#define TB_ADDR_XCVR_PSM_A4IN_TMR		0x4008
#define TB_ADDR_XCVR_PSM_A5IN_TMR		0x4009
#define TB_ADDR_XCVR_PSM_A0OUT_TMR		0x400a
#define TB_ADDR_XCVR_PSM_A1OUT_TMR		0x400b
#define TB_ADDR_XCVR_PSM_A2OUT_TMR		0x400c
#define TB_ADDR_XCVR_PSM_A3OUT_TMR		0x400d
#define TB_ADDR_XCVR_PSM_A4OUT_TMR		0x400e
#define TB_ADDR_XCVR_PSM_A5OUT_TMR		0x400f
#define TB_ADDR_TX_RCVDET_EN_TMR	        0x4122
#define TB_ADDR_TX_RCVDET_ST_TMR	        0x4123
#define TB_ADDR_XCVR_DIAG_LANE_FCM_EN_MGN_TMR	0x40f2
#define TB_ADDR_TX_RCVDETSC_CTRL	        0x4124

/* TB_ADDR_TX_RCVDETSC_CTRL */
#define RXDET_IN_P3_32KHZ			BIT(0)

struct cdns_reg_pairs {
	u16 val;
	u32 off;
};

struct cdns_salvo_data {
	u8 reg_offset_shift;
	const struct cdns_reg_pairs *init_sequence_val;
	u8 init_sequence_length;
};

struct cdns_salvo_phy {
	struct phy *phy;
	struct clk *clk;
	void __iomem *base;
	struct cdns_salvo_data *data;
};

static const struct of_device_id cdns_salvo_phy_of_match[];
static u16 cdns_salvo_read(struct cdns_salvo_phy *salvo_phy, u32 reg)
{
	return (u16)readl(salvo_phy->base +
		reg * (1 << salvo_phy->data->reg_offset_shift));
}

static void cdns_salvo_write(struct cdns_salvo_phy *salvo_phy,
			     u32 reg, u16 val)
{
	writel(val, salvo_phy->base +
		reg * (1 << salvo_phy->data->reg_offset_shift));
}

/*
 * Below bringup sequence pair are from Cadence PHY's User Guide
 * and NXP platform tuning results.
 */
static const struct cdns_reg_pairs cdns_nxp_sequence_pair[] = {
	{0x0830, PHY_PMA_CMN_CTRL1},
	{0x0010, TB_ADDR_CMN_DIAG_HSCLK_SEL},
	{0x00f0, TB_ADDR_CMN_PLL0_VCOCAL_INIT_TMR},
	{0x0018, TB_ADDR_CMN_PLL0_VCOCAL_ITER_TMR},
	{0x00d0, TB_ADDR_CMN_PLL0_INTDIV},
	{0x4aaa, TB_ADDR_CMN_PLL0_FRACDIV},
	{0x0034, TB_ADDR_CMN_PLL0_HIGH_THR},
	{0x01ee, TB_ADDR_CMN_PLL0_SS_CTRL1},
	{0x7f03, TB_ADDR_CMN_PLL0_SS_CTRL2},
	{0x0020, TB_ADDR_CMN_PLL0_DSM_DIAG},
	{0x0000, TB_ADDR_CMN_DIAG_PLL0_OVRD},
	{0x0000, TB_ADDR_CMN_DIAG_PLL0_FBH_OVRD},
	{0x0000, TB_ADDR_CMN_DIAG_PLL0_FBL_OVRD},
	{0x0007, TB_ADDR_CMN_DIAG_PLL0_V2I_TUNE},
	{0x0027, TB_ADDR_CMN_DIAG_PLL0_CP_TUNE},
	{0x0008, TB_ADDR_CMN_DIAG_PLL0_LF_PROG},
	{0x0022, TB_ADDR_CMN_DIAG_PLL0_TEST_MODE},
	{0x000a, TB_ADDR_CMN_PSM_CLK_CTRL},
	{0x0139, TB_ADDR_XCVR_DIAG_RX_LANE_CAL_RST_TMR},
	{0xbefc, TB_ADDR_XCVR_PSM_RCTRL},

	{0x7799, TB_ADDR_TX_PSC_A0},
	{0x7798, TB_ADDR_TX_PSC_A1},
	{0x509b, TB_ADDR_TX_PSC_A2},
	{0x0003, TB_ADDR_TX_DIAG_ECTRL_OVRD},
	{0x509b, TB_ADDR_TX_PSC_A3},
	{0x2090, TB_ADDR_TX_PSC_CAL},
	{0x2090, TB_ADDR_TX_PSC_RDY},

	{0xA6FD, TB_ADDR_RX_PSC_A0},
	{0xA6FD, TB_ADDR_RX_PSC_A1},
	{0xA410, TB_ADDR_RX_PSC_A2},
	{0x2410, TB_ADDR_RX_PSC_A3},

	{0x23FF, TB_ADDR_RX_PSC_CAL},
	{0x2010, TB_ADDR_RX_PSC_RDY},

	{0x0020, TB_ADDR_TX_TXCC_MGNLS_MULT_000},
	{0x00ff, TB_ADDR_TX_DIAG_BGREF_PREDRV_DELAY},
	{0x0002, TB_ADDR_RX_SLC_CU_ITER_TMR},
	{0x0013, TB_ADDR_RX_SIGDET_HL_FILT_TMR},
	{0x0000, TB_ADDR_RX_SAMP_DAC_CTRL},
	{0x1004, TB_ADDR_RX_DIAG_SIGDET_TUNE},
	{0x4041, TB_ADDR_RX_DIAG_LFPSDET_TUNE2},
	{0x0480, TB_ADDR_RX_DIAG_BS_TM},
	{0x8006, TB_ADDR_RX_DIAG_DFE_CTRL1},
	{0x003f, TB_ADDR_RX_DIAG_ILL_IQE_TRIM4},
	{0x543f, TB_ADDR_RX_DIAG_ILL_E_TRIM0},
	{0x543f, TB_ADDR_RX_DIAG_ILL_IQ_TRIM0},
	{0x0000, TB_ADDR_RX_DIAG_ILL_IQE_TRIM6},
	{0x8000, TB_ADDR_RX_DIAG_RXFE_TM3},
	{0x0003, TB_ADDR_RX_DIAG_RXFE_TM4},
	{0x2408, TB_ADDR_RX_DIAG_LFPSDET_TUNE},
	{0x05ca, TB_ADDR_RX_DIAG_DFE_CTRL3},
	{0x0258, TB_ADDR_RX_DIAG_SC2C_DELAY},
	{0x1fff, TB_ADDR_RX_REE_VGA_GAIN_NODFE},

	{0x02c6, TB_ADDR_XCVR_PSM_CAL_TMR},
	{0x0002, TB_ADDR_XCVR_PSM_A0BYP_TMR},
	{0x02c6, TB_ADDR_XCVR_PSM_A0IN_TMR},
	{0x0010, TB_ADDR_XCVR_PSM_A1IN_TMR},
	{0x0010, TB_ADDR_XCVR_PSM_A2IN_TMR},
	{0x0010, TB_ADDR_XCVR_PSM_A3IN_TMR},
	{0x0010, TB_ADDR_XCVR_PSM_A4IN_TMR},
	{0x0010, TB_ADDR_XCVR_PSM_A5IN_TMR},

	{0x0002, TB_ADDR_XCVR_PSM_A0OUT_TMR},
	{0x0002, TB_ADDR_XCVR_PSM_A1OUT_TMR},
	{0x0002, TB_ADDR_XCVR_PSM_A2OUT_TMR},
	{0x0002, TB_ADDR_XCVR_PSM_A3OUT_TMR},
	{0x0002, TB_ADDR_XCVR_PSM_A4OUT_TMR},
	{0x0002, TB_ADDR_XCVR_PSM_A5OUT_TMR},
	/* Change rx detect parameter */
	{0x0960, TB_ADDR_TX_RCVDET_EN_TMR},
	{0x01e0, TB_ADDR_TX_RCVDET_ST_TMR},
	{0x0090, TB_ADDR_XCVR_DIAG_LANE_FCM_EN_MGN_TMR},
};

static int cdns_salvo_phy_init(struct phy *phy)
{
	struct cdns_salvo_phy *salvo_phy = phy_get_drvdata(phy);
	struct cdns_salvo_data *data = salvo_phy->data;
	int ret, i;
	u16 value;

	ret = clk_prepare_enable(salvo_phy->clk);
	if (ret)
		return ret;

	for (i = 0; i < data->init_sequence_length; i++) {
		const struct cdns_reg_pairs *reg_pair = data->init_sequence_val + i;

		cdns_salvo_write(salvo_phy, reg_pair->off, reg_pair->val);
	}

	/* RXDET_IN_P3_32KHZ, Receiver detect slow clock enable */
	value = cdns_salvo_read(salvo_phy, TB_ADDR_TX_RCVDETSC_CTRL);
	value |= RXDET_IN_P3_32KHZ;
	cdns_salvo_write(salvo_phy, TB_ADDR_TX_RCVDETSC_CTRL,
			 RXDET_IN_P3_32KHZ);

	udelay(10);

	clk_disable_unprepare(salvo_phy->clk);

	return ret;
}

static int cdns_salvo_phy_power_on(struct phy *phy)
{
	struct cdns_salvo_phy *salvo_phy = phy_get_drvdata(phy);

	return clk_prepare_enable(salvo_phy->clk);
}

static int cdns_salvo_phy_power_off(struct phy *phy)
{
	struct cdns_salvo_phy *salvo_phy = phy_get_drvdata(phy);

	clk_disable_unprepare(salvo_phy->clk);

	return 0;
}

static const struct phy_ops cdns_salvo_phy_ops = {
	.init		= cdns_salvo_phy_init,
	.power_on	= cdns_salvo_phy_power_on,
	.power_off	= cdns_salvo_phy_power_off,
	.owner		= THIS_MODULE,
};

static int cdns_salvo_phy_probe(struct platform_device *pdev)
{
	struct phy_provider *phy_provider;
	struct device *dev = &pdev->dev;
	struct cdns_salvo_phy *salvo_phy;
	struct cdns_salvo_data *data;

	data = (struct cdns_salvo_data *)of_device_get_match_data(dev);
	salvo_phy = devm_kzalloc(dev, sizeof(*salvo_phy), GFP_KERNEL);
	if (!salvo_phy)
		return -ENOMEM;

	salvo_phy->data = data;
	salvo_phy->clk = devm_clk_get_optional(dev, "salvo_phy_clk");
	if (IS_ERR(salvo_phy->clk))
		return PTR_ERR(salvo_phy->clk);

	salvo_phy->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(salvo_phy->base))
		return PTR_ERR(salvo_phy->base);

	salvo_phy->phy = devm_phy_create(dev, NULL, &cdns_salvo_phy_ops);
	if (IS_ERR(salvo_phy->phy))
		return PTR_ERR(salvo_phy->phy);

	phy_set_drvdata(salvo_phy->phy, salvo_phy);

	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	return PTR_ERR_OR_ZERO(phy_provider);
}

static const struct cdns_salvo_data cdns_nxp_salvo_data = {
	2,
	cdns_nxp_sequence_pair,
	ARRAY_SIZE(cdns_nxp_sequence_pair),
};

static const struct of_device_id cdns_salvo_phy_of_match[] = {
	{
		.compatible = "nxp,salvo-phy",
		.data = &cdns_nxp_salvo_data,
	},
	{}
};
MODULE_DEVICE_TABLE(of, cdns_salvo_phy_of_match);

static struct platform_driver cdns_salvo_phy_driver = {
	.probe	= cdns_salvo_phy_probe,
	.driver = {
		.name	= "cdns-salvo-phy",
		.of_match_table	= cdns_salvo_phy_of_match,
	}
};
module_platform_driver(cdns_salvo_phy_driver);

MODULE_AUTHOR("Peter Chen <peter.chen@nxp.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Cadence SALVO PHY Driver");
