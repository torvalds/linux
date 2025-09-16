// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2025, The Linux Foundation. All rights reserved.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/units.h>

#define RST_ASSERT_DELAY_MIN_US		100
#define RST_ASSERT_DELAY_MAX_US		150
#define PIPE_CLK_DELAY_MIN_US		5000
#define PIPE_CLK_DELAY_MAX_US		5100
#define CLK_EN_DELAY_MIN_US		30
#define CLK_EN_DELAY_MAX_US		50
#define CDR_CTRL_REG_1		0x80
#define CDR_CTRL_REG_2		0x84
#define CDR_CTRL_REG_3		0x88
#define CDR_CTRL_REG_4		0x8c
#define CDR_CTRL_REG_5		0x90
#define CDR_CTRL_REG_6		0x94
#define CDR_CTRL_REG_7		0x98
#define SSCG_CTRL_REG_1		0x9c
#define SSCG_CTRL_REG_2		0xa0
#define SSCG_CTRL_REG_3		0xa4
#define SSCG_CTRL_REG_4		0xa8
#define SSCG_CTRL_REG_5		0xac
#define SSCG_CTRL_REG_6		0xb0
#define PCS_INTERNAL_CONTROL_2	0x2d8

#define PHY_CFG_PLLCFG				0x220
#define PHY_CFG_EIOS_DTCT_REG			0x3e4
#define PHY_CFG_GEN3_ALIGN_HOLDOFF_TIME		0x3e8

enum qcom_uniphy_pcie_type {
	PHY_TYPE_PCIE = 1,
	PHY_TYPE_PCIE_GEN2,
	PHY_TYPE_PCIE_GEN3,
};

struct qcom_uniphy_pcie_regs {
	u32 offset;
	u32 val;
};

struct qcom_uniphy_pcie_data {
	int lane_offset; /* offset between the lane register bases */
	u32 phy_type;
	const struct qcom_uniphy_pcie_regs *init_seq;
	u32 init_seq_num;
	u32 pipe_clk_rate;
};

struct qcom_uniphy_pcie {
	struct phy phy;
	struct device *dev;
	const struct qcom_uniphy_pcie_data *data;
	struct clk_bulk_data *clks;
	int num_clks;
	struct reset_control *resets;
	void __iomem *base;
	int lanes;
};

#define phy_to_dw_phy(x)	container_of((x), struct qca_uni_pcie_phy, phy)

static const struct qcom_uniphy_pcie_regs ipq5018_regs[] = {
	{
		.offset = SSCG_CTRL_REG_4,
		.val = 0x1cb9,
	}, {
		.offset = SSCG_CTRL_REG_5,
		.val = 0x023a,
	}, {
		.offset = SSCG_CTRL_REG_3,
		.val = 0xd360,
	}, {
		.offset = SSCG_CTRL_REG_1,
		.val = 0x1,
	}, {
		.offset = SSCG_CTRL_REG_2,
		.val = 0xeb,
	}, {
		.offset = CDR_CTRL_REG_4,
		.val = 0x3f9,
	}, {
		.offset = CDR_CTRL_REG_5,
		.val = 0x1c9,
	}, {
		.offset = CDR_CTRL_REG_2,
		.val = 0x419,
	}, {
		.offset = CDR_CTRL_REG_1,
		.val = 0x200,
	}, {
		.offset = PCS_INTERNAL_CONTROL_2,
		.val = 0xf101,
	},
};

static const struct qcom_uniphy_pcie_regs ipq5332_regs[] = {
	{
		.offset = PHY_CFG_PLLCFG,
		.val = 0x30,
	}, {
		.offset = PHY_CFG_EIOS_DTCT_REG,
		.val = 0x53ef,
	}, {
		.offset = PHY_CFG_GEN3_ALIGN_HOLDOFF_TIME,
		.val = 0xcf,
	},
};

static const struct qcom_uniphy_pcie_data ipq5018_data = {
	.lane_offset	= 0x800,
	.phy_type	= PHY_TYPE_PCIE_GEN2,
	.init_seq	= ipq5018_regs,
	.init_seq_num	= ARRAY_SIZE(ipq5018_regs),
	.pipe_clk_rate	= 125 * MEGA,
};

static const struct qcom_uniphy_pcie_data ipq5332_data = {
	.lane_offset	= 0x800,
	.phy_type	= PHY_TYPE_PCIE_GEN3,
	.init_seq	= ipq5332_regs,
	.init_seq_num	= ARRAY_SIZE(ipq5332_regs),
	.pipe_clk_rate	= 250 * MEGA,
};

static void qcom_uniphy_pcie_init(struct qcom_uniphy_pcie *phy)
{
	const struct qcom_uniphy_pcie_data *data = phy->data;
	const struct qcom_uniphy_pcie_regs *init_seq;
	void __iomem *base = phy->base;
	int lane, i;

	for (lane = 0; lane < phy->lanes; lane++) {
		init_seq = data->init_seq;

		for (i = 0; i < data->init_seq_num; i++)
			writel(init_seq[i].val, base + init_seq[i].offset);

		base += data->lane_offset;
	}
}

static int qcom_uniphy_pcie_power_off(struct phy *x)
{
	struct qcom_uniphy_pcie *phy = phy_get_drvdata(x);

	clk_bulk_disable_unprepare(phy->num_clks, phy->clks);

	return reset_control_assert(phy->resets);
}

static int qcom_uniphy_pcie_power_on(struct phy *x)
{
	struct qcom_uniphy_pcie *phy = phy_get_drvdata(x);
	int ret;

	ret = reset_control_assert(phy->resets);
	if (ret) {
		dev_err(phy->dev, "reset assert failed (%d)\n", ret);
		return ret;
	}

	usleep_range(RST_ASSERT_DELAY_MIN_US, RST_ASSERT_DELAY_MAX_US);

	ret = reset_control_deassert(phy->resets);
	if (ret) {
		dev_err(phy->dev, "reset deassert failed (%d)\n", ret);
		return ret;
	}

	usleep_range(PIPE_CLK_DELAY_MIN_US, PIPE_CLK_DELAY_MAX_US);

	ret = clk_bulk_prepare_enable(phy->num_clks, phy->clks);
	if (ret) {
		dev_err(phy->dev, "clk prepare and enable failed %d\n", ret);
		return ret;
	}

	usleep_range(CLK_EN_DELAY_MIN_US, CLK_EN_DELAY_MAX_US);

	qcom_uniphy_pcie_init(phy);

	return 0;
}

static inline int qcom_uniphy_pcie_get_resources(struct platform_device *pdev,
						 struct qcom_uniphy_pcie *phy)
{
	struct resource *res;

	phy->base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(phy->base))
		return PTR_ERR(phy->base);

	phy->num_clks = devm_clk_bulk_get_all(phy->dev, &phy->clks);
	if (phy->num_clks < 0)
		return phy->num_clks;

	phy->resets = devm_reset_control_array_get_exclusive(phy->dev);
	if (IS_ERR(phy->resets))
		return PTR_ERR(phy->resets);

	return 0;
}

/*
 * Register a fixed rate pipe clock.
 *
 * The <s>_pipe_clksrc generated by PHY goes to the GCC that gate
 * controls it. The <s>_pipe_clk coming out of the GCC is requested
 * by the PHY driver for its operations.
 * We register the <s>_pipe_clksrc here. The gcc driver takes care
 * of assigning this <s>_pipe_clksrc as parent to <s>_pipe_clk.
 * Below picture shows this relationship.
 *
 *         +---------------+
 *         |   PHY block   |<<---------------------------------------+
 *         |               |                                         |
 *         |   +-------+   |                   +-----+               |
 *   I/P---^-->|  PLL  |---^--->pipe_clksrc--->| GCC |--->pipe_clk---+
 *    clk  |   +-------+   |                   +-----+
 *         +---------------+
 */
static inline int phy_pipe_clk_register(struct qcom_uniphy_pcie *phy, int id)
{
	const struct qcom_uniphy_pcie_data *data = phy->data;
	struct clk_hw *hw;
	char name[64];

	snprintf(name, sizeof(name), "phy%d_pipe_clk_src", id);
	hw = devm_clk_hw_register_fixed_rate(phy->dev, name, NULL, 0,
					     data->pipe_clk_rate);
	if (IS_ERR(hw))
		return dev_err_probe(phy->dev, PTR_ERR(hw),
				     "Unable to register %s\n", name);

	return devm_of_clk_add_hw_provider(phy->dev, of_clk_hw_simple_get, hw);
}

static const struct of_device_id qcom_uniphy_pcie_id_table[] = {
	{
		.compatible = "qcom,ipq5018-uniphy-pcie-phy",
		.data = &ipq5018_data,
	}, {
		.compatible = "qcom,ipq5332-uniphy-pcie-phy",
		.data = &ipq5332_data,
	}, {
		/* Sentinel */
	},
};
MODULE_DEVICE_TABLE(of, qcom_uniphy_pcie_id_table);

static const struct phy_ops pcie_ops = {
	.power_on	= qcom_uniphy_pcie_power_on,
	.power_off	= qcom_uniphy_pcie_power_off,
	.owner          = THIS_MODULE,
};

static int qcom_uniphy_pcie_probe(struct platform_device *pdev)
{
	struct phy_provider *phy_provider;
	struct device *dev = &pdev->dev;
	struct qcom_uniphy_pcie *phy;
	struct phy *generic_phy;
	int ret;

	phy = devm_kzalloc(&pdev->dev, sizeof(*phy), GFP_KERNEL);
	if (!phy)
		return -ENOMEM;

	platform_set_drvdata(pdev, phy);
	phy->dev = &pdev->dev;

	phy->data = of_device_get_match_data(dev);
	if (!phy->data)
		return -EINVAL;

	ret = of_property_read_u32(dev_of_node(dev), "num-lanes", &phy->lanes);
	if (ret)
		return dev_err_probe(dev, ret, "Couldn't read num-lanes\n");

	ret = qcom_uniphy_pcie_get_resources(pdev, phy);
	if (ret < 0)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to get resources: %d\n", ret);

	generic_phy = devm_phy_create(phy->dev, NULL, &pcie_ops);
	if (IS_ERR(generic_phy))
		return PTR_ERR(generic_phy);

	phy_set_drvdata(generic_phy, phy);

	ret = phy_pipe_clk_register(phy, generic_phy->id);
	if (ret)
		dev_err(&pdev->dev, "failed to register phy pipe clk\n");

	phy_provider = devm_of_phy_provider_register(phy->dev,
						     of_phy_simple_xlate);
	if (IS_ERR(phy_provider))
		return PTR_ERR(phy_provider);

	return 0;
}

static struct platform_driver qcom_uniphy_pcie_driver = {
	.probe		= qcom_uniphy_pcie_probe,
	.driver		= {
		.name	= "qcom-uniphy-pcie",
		.of_match_table = qcom_uniphy_pcie_id_table,
	},
};

module_platform_driver(qcom_uniphy_pcie_driver);

MODULE_DESCRIPTION("PCIE QCOM UNIPHY driver");
MODULE_LICENSE("GPL");
