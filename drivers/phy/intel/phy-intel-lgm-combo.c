// SPDX-License-Identifier: GPL-2.0
/*
 * Intel Combo-PHY driver
 *
 * Copyright (C) 2019-2020 Intel Corporation.
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/iopoll.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset.h>

#include <dt-bindings/phy/phy.h>

#define PCIE_PHY_GEN_CTRL	0x00
#define PCIE_PHY_CLK_PAD	BIT(17)

#define PAD_DIS_CFG		0x174

#define PCS_XF_ATE_OVRD_IN_2	0x3008
#define ADAPT_REQ_MSK		GENMASK(5, 4)

#define PCS_XF_RX_ADAPT_ACK	0x3010
#define RX_ADAPT_ACK_BIT	BIT(0)

#define CR_ADDR(addr, lane)	(((addr) + (lane) * 0x100) << 2)
#define REG_COMBO_MODE(x)	((x) * 0x200)
#define REG_CLK_DISABLE(x)	((x) * 0x200 + 0x124)

#define COMBO_PHY_ID(x)		((x)->parent->id)
#define PHY_ID(x)		((x)->id)

#define CLK_100MHZ		100000000
#define CLK_156_25MHZ		156250000

static const unsigned long intel_iphy_clk_rates[] = {
	CLK_100MHZ, CLK_156_25MHZ, CLK_100MHZ,
};

enum {
	PHY_0,
	PHY_1,
	PHY_MAX_NUM
};

/*
 * Clock Register bit fields to enable clocks
 * for ComboPhy according to the mode.
 */
enum intel_phy_mode {
	PHY_PCIE_MODE = 0,
	PHY_XPCS_MODE,
	PHY_SATA_MODE,
};

/* ComboPhy mode Register values */
enum intel_combo_mode {
	PCIE0_PCIE1_MODE = 0,
	PCIE_DL_MODE,
	RXAUI_MODE,
	XPCS0_XPCS1_MODE,
	SATA0_SATA1_MODE,
};

enum aggregated_mode {
	PHY_SL_MODE,
	PHY_DL_MODE,
};

struct intel_combo_phy;

struct intel_cbphy_iphy {
	struct phy		*phy;
	struct intel_combo_phy	*parent;
	struct reset_control	*app_rst;
	u32			id;
};

struct intel_combo_phy {
	struct device		*dev;
	struct clk		*core_clk;
	unsigned long		clk_rate;
	void __iomem		*app_base;
	void __iomem		*cr_base;
	struct regmap		*syscfg;
	struct regmap		*hsiocfg;
	u32			id;
	u32			bid;
	struct reset_control	*phy_rst;
	struct reset_control	*core_rst;
	struct intel_cbphy_iphy	iphy[PHY_MAX_NUM];
	enum intel_phy_mode	phy_mode;
	enum aggregated_mode	aggr_mode;
	u32			init_cnt;
	struct mutex		lock;
};

static int intel_cbphy_iphy_enable(struct intel_cbphy_iphy *iphy, bool set)
{
	struct intel_combo_phy *cbphy = iphy->parent;
	u32 mask = BIT(cbphy->phy_mode * 2 + iphy->id);
	u32 val;

	/* Register: 0 is enable, 1 is disable */
	val = set ? 0 : mask;

	return regmap_update_bits(cbphy->hsiocfg, REG_CLK_DISABLE(cbphy->bid),
				  mask, val);
}

static int intel_cbphy_pcie_refclk_cfg(struct intel_cbphy_iphy *iphy, bool set)
{
	struct intel_combo_phy *cbphy = iphy->parent;
	u32 mask = BIT(cbphy->id * 2 + iphy->id);
	u32 val;

	/* Register: 0 is enable, 1 is disable */
	val = set ? 0 : mask;

	return regmap_update_bits(cbphy->syscfg, PAD_DIS_CFG, mask, val);
}

static inline void combo_phy_w32_off_mask(void __iomem *base, unsigned int reg,
					  u32 mask, u32 val)
{
	u32 reg_val;

	reg_val = readl(base + reg);
	reg_val &= ~mask;
	reg_val |= val;
	writel(reg_val, base + reg);
}

static int intel_cbphy_iphy_cfg(struct intel_cbphy_iphy *iphy,
				int (*phy_cfg)(struct intel_cbphy_iphy *))
{
	struct intel_combo_phy *cbphy = iphy->parent;
	int ret;

	ret = phy_cfg(iphy);
	if (ret)
		return ret;

	if (cbphy->aggr_mode != PHY_DL_MODE)
		return 0;

	return phy_cfg(&cbphy->iphy[PHY_1]);
}

static int intel_cbphy_pcie_en_pad_refclk(struct intel_cbphy_iphy *iphy)
{
	struct intel_combo_phy *cbphy = iphy->parent;
	int ret;

	ret = intel_cbphy_pcie_refclk_cfg(iphy, true);
	if (ret) {
		dev_err(cbphy->dev, "Failed to enable PCIe pad refclk\n");
		return ret;
	}

	if (cbphy->init_cnt)
		return 0;

	combo_phy_w32_off_mask(cbphy->app_base, PCIE_PHY_GEN_CTRL,
			       PCIE_PHY_CLK_PAD, FIELD_PREP(PCIE_PHY_CLK_PAD, 0));

	/* Delay for stable clock PLL */
	usleep_range(50, 100);

	return 0;
}

static int intel_cbphy_pcie_dis_pad_refclk(struct intel_cbphy_iphy *iphy)
{
	struct intel_combo_phy *cbphy = iphy->parent;
	int ret;

	ret = intel_cbphy_pcie_refclk_cfg(iphy, false);
	if (ret) {
		dev_err(cbphy->dev, "Failed to disable PCIe pad refclk\n");
		return ret;
	}

	if (cbphy->init_cnt)
		return 0;

	combo_phy_w32_off_mask(cbphy->app_base, PCIE_PHY_GEN_CTRL,
			       PCIE_PHY_CLK_PAD, FIELD_PREP(PCIE_PHY_CLK_PAD, 1));

	return 0;
}

static int intel_cbphy_set_mode(struct intel_combo_phy *cbphy)
{
	enum intel_combo_mode cb_mode;
	enum aggregated_mode aggr = cbphy->aggr_mode;
	struct device *dev = cbphy->dev;
	enum intel_phy_mode mode;
	int ret;

	mode = cbphy->phy_mode;

	switch (mode) {
	case PHY_PCIE_MODE:
		cb_mode = (aggr == PHY_DL_MODE) ? PCIE_DL_MODE : PCIE0_PCIE1_MODE;
		break;

	case PHY_XPCS_MODE:
		cb_mode = (aggr == PHY_DL_MODE) ? RXAUI_MODE : XPCS0_XPCS1_MODE;
		break;

	case PHY_SATA_MODE:
		if (aggr == PHY_DL_MODE) {
			dev_err(dev, "Mode:%u not support dual lane!\n", mode);
			return -EINVAL;
		}

		cb_mode = SATA0_SATA1_MODE;
		break;
	default:
		return -EINVAL;
	}

	ret = regmap_write(cbphy->hsiocfg, REG_COMBO_MODE(cbphy->bid), cb_mode);
	if (ret)
		dev_err(dev, "Failed to set ComboPhy mode: %d\n", ret);

	return ret;
}

static void intel_cbphy_rst_assert(struct intel_combo_phy *cbphy)
{
	reset_control_assert(cbphy->core_rst);
	reset_control_assert(cbphy->phy_rst);
}

static void intel_cbphy_rst_deassert(struct intel_combo_phy *cbphy)
{
	reset_control_deassert(cbphy->core_rst);
	reset_control_deassert(cbphy->phy_rst);
	/* Delay to ensure reset process is done */
	usleep_range(10, 20);
}

static int intel_cbphy_iphy_power_on(struct intel_cbphy_iphy *iphy)
{
	struct intel_combo_phy *cbphy = iphy->parent;
	int ret;

	if (!cbphy->init_cnt) {
		ret = clk_prepare_enable(cbphy->core_clk);
		if (ret) {
			dev_err(cbphy->dev, "Clock enable failed!\n");
			return ret;
		}

		ret = clk_set_rate(cbphy->core_clk, cbphy->clk_rate);
		if (ret) {
			dev_err(cbphy->dev, "Clock freq set to %lu failed!\n",
				cbphy->clk_rate);
			goto clk_err;
		}

		intel_cbphy_rst_assert(cbphy);
		intel_cbphy_rst_deassert(cbphy);
		ret = intel_cbphy_set_mode(cbphy);
		if (ret)
			goto clk_err;
	}

	ret = intel_cbphy_iphy_enable(iphy, true);
	if (ret) {
		dev_err(cbphy->dev, "Failed enabling PHY core\n");
		goto clk_err;
	}

	ret = reset_control_deassert(iphy->app_rst);
	if (ret) {
		dev_err(cbphy->dev, "PHY(%u:%u) reset deassert failed!\n",
			COMBO_PHY_ID(iphy), PHY_ID(iphy));
		goto clk_err;
	}

	/* Delay to ensure reset process is done */
	udelay(1);

	return 0;

clk_err:
	clk_disable_unprepare(cbphy->core_clk);

	return ret;
}

static int intel_cbphy_iphy_power_off(struct intel_cbphy_iphy *iphy)
{
	struct intel_combo_phy *cbphy = iphy->parent;
	int ret;

	ret = reset_control_assert(iphy->app_rst);
	if (ret) {
		dev_err(cbphy->dev, "PHY(%u:%u) reset assert failed!\n",
			COMBO_PHY_ID(iphy), PHY_ID(iphy));
		return ret;
	}

	ret = intel_cbphy_iphy_enable(iphy, false);
	if (ret) {
		dev_err(cbphy->dev, "Failed disabling PHY core\n");
		return ret;
	}

	if (cbphy->init_cnt)
		return 0;

	clk_disable_unprepare(cbphy->core_clk);
	intel_cbphy_rst_assert(cbphy);

	return 0;
}

static int intel_cbphy_init(struct phy *phy)
{
	struct intel_cbphy_iphy *iphy = phy_get_drvdata(phy);
	struct intel_combo_phy *cbphy = iphy->parent;
	int ret;

	mutex_lock(&cbphy->lock);
	ret = intel_cbphy_iphy_cfg(iphy, intel_cbphy_iphy_power_on);
	if (ret)
		goto err;

	if (cbphy->phy_mode == PHY_PCIE_MODE) {
		ret = intel_cbphy_iphy_cfg(iphy, intel_cbphy_pcie_en_pad_refclk);
		if (ret)
			goto err;
	}

	cbphy->init_cnt++;

err:
	mutex_unlock(&cbphy->lock);

	return ret;
}

static int intel_cbphy_exit(struct phy *phy)
{
	struct intel_cbphy_iphy *iphy = phy_get_drvdata(phy);
	struct intel_combo_phy *cbphy = iphy->parent;
	int ret;

	mutex_lock(&cbphy->lock);
	cbphy->init_cnt--;
	if (cbphy->phy_mode == PHY_PCIE_MODE) {
		ret = intel_cbphy_iphy_cfg(iphy, intel_cbphy_pcie_dis_pad_refclk);
		if (ret)
			goto err;
	}

	ret = intel_cbphy_iphy_cfg(iphy, intel_cbphy_iphy_power_off);

err:
	mutex_unlock(&cbphy->lock);

	return ret;
}

static int intel_cbphy_calibrate(struct phy *phy)
{
	struct intel_cbphy_iphy *iphy = phy_get_drvdata(phy);
	struct intel_combo_phy *cbphy = iphy->parent;
	void __iomem *cr_base = cbphy->cr_base;
	int val, ret, id;

	if (cbphy->phy_mode != PHY_XPCS_MODE)
		return 0;

	id = PHY_ID(iphy);

	/* trigger auto RX adaptation */
	combo_phy_w32_off_mask(cr_base, CR_ADDR(PCS_XF_ATE_OVRD_IN_2, id),
			       ADAPT_REQ_MSK, FIELD_PREP(ADAPT_REQ_MSK, 3));
	/* Wait RX adaptation to finish */
	ret = readl_poll_timeout(cr_base + CR_ADDR(PCS_XF_RX_ADAPT_ACK, id),
				 val, val & RX_ADAPT_ACK_BIT, 10, 5000);
	if (ret)
		dev_err(cbphy->dev, "RX Adaptation failed!\n");
	else
		dev_dbg(cbphy->dev, "RX Adaptation success!\n");

	/* Stop RX adaptation */
	combo_phy_w32_off_mask(cr_base, CR_ADDR(PCS_XF_ATE_OVRD_IN_2, id),
			       ADAPT_REQ_MSK, FIELD_PREP(ADAPT_REQ_MSK, 0));

	return ret;
}

static int intel_cbphy_fwnode_parse(struct intel_combo_phy *cbphy)
{
	struct device *dev = cbphy->dev;
	struct platform_device *pdev = to_platform_device(dev);
	struct fwnode_handle *fwnode = dev_fwnode(dev);
	struct fwnode_reference_args ref;
	int ret;
	u32 val;

	cbphy->core_clk = devm_clk_get(dev, NULL);
	if (IS_ERR(cbphy->core_clk))
		return dev_err_probe(dev, PTR_ERR(cbphy->core_clk),
				     "Get clk failed!\n");

	cbphy->core_rst = devm_reset_control_get_optional(dev, "core");
	if (IS_ERR(cbphy->core_rst))
		return dev_err_probe(dev, PTR_ERR(cbphy->core_rst),
				     "Get core reset control err!\n");

	cbphy->phy_rst = devm_reset_control_get_optional(dev, "phy");
	if (IS_ERR(cbphy->phy_rst))
		return dev_err_probe(dev, PTR_ERR(cbphy->phy_rst),
				     "Get PHY reset control err!\n");

	cbphy->iphy[0].app_rst = devm_reset_control_get_optional(dev, "iphy0");
	if (IS_ERR(cbphy->iphy[0].app_rst))
		return dev_err_probe(dev, PTR_ERR(cbphy->iphy[0].app_rst),
				     "Get phy0 reset control err!\n");

	cbphy->iphy[1].app_rst = devm_reset_control_get_optional(dev, "iphy1");
	if (IS_ERR(cbphy->iphy[1].app_rst))
		return dev_err_probe(dev, PTR_ERR(cbphy->iphy[1].app_rst),
				     "Get phy1 reset control err!\n");

	cbphy->app_base = devm_platform_ioremap_resource_byname(pdev, "app");
	if (IS_ERR(cbphy->app_base))
		return PTR_ERR(cbphy->app_base);

	cbphy->cr_base = devm_platform_ioremap_resource_byname(pdev, "core");
	if (IS_ERR(cbphy->cr_base))
		return PTR_ERR(cbphy->cr_base);

	/*
	 * syscfg and hsiocfg variables stores the handle of the registers set
	 * in which ComboPhy subsystem specific registers are subset. Using
	 * Register map framework to access the registers set.
	 */
	ret = fwnode_property_get_reference_args(fwnode, "intel,syscfg", NULL,
						 1, 0, &ref);
	if (ret < 0)
		return ret;

	cbphy->id = ref.args[0];
	cbphy->syscfg = device_node_to_regmap(to_of_node(ref.fwnode));
	fwnode_handle_put(ref.fwnode);

	ret = fwnode_property_get_reference_args(fwnode, "intel,hsio", NULL, 1,
						 0, &ref);
	if (ret < 0)
		return ret;

	cbphy->bid = ref.args[0];
	cbphy->hsiocfg = device_node_to_regmap(to_of_node(ref.fwnode));
	fwnode_handle_put(ref.fwnode);

	ret = fwnode_property_read_u32_array(fwnode, "intel,phy-mode", &val, 1);
	if (ret)
		return ret;

	switch (val) {
	case PHY_TYPE_PCIE:
		cbphy->phy_mode = PHY_PCIE_MODE;
		break;

	case PHY_TYPE_SATA:
		cbphy->phy_mode = PHY_SATA_MODE;
		break;

	case PHY_TYPE_XPCS:
		cbphy->phy_mode = PHY_XPCS_MODE;
		break;

	default:
		dev_err(dev, "Invalid PHY mode: %u\n", val);
		return -EINVAL;
	}

	cbphy->clk_rate = intel_iphy_clk_rates[cbphy->phy_mode];

	if (fwnode_property_present(fwnode, "intel,aggregation"))
		cbphy->aggr_mode = PHY_DL_MODE;
	else
		cbphy->aggr_mode = PHY_SL_MODE;

	return 0;
}

static const struct phy_ops intel_cbphy_ops = {
	.init		= intel_cbphy_init,
	.exit		= intel_cbphy_exit,
	.calibrate	= intel_cbphy_calibrate,
	.owner		= THIS_MODULE,
};

static struct phy *intel_cbphy_xlate(struct device *dev,
				     struct of_phandle_args *args)
{
	struct intel_combo_phy *cbphy = dev_get_drvdata(dev);
	u32 iphy_id;

	if (args->args_count < 1) {
		dev_err(dev, "Invalid number of arguments\n");
		return ERR_PTR(-EINVAL);
	}

	iphy_id = args->args[0];
	if (iphy_id >= PHY_MAX_NUM) {
		dev_err(dev, "Invalid phy instance %d\n", iphy_id);
		return ERR_PTR(-EINVAL);
	}

	if (cbphy->aggr_mode == PHY_DL_MODE && iphy_id == PHY_1) {
		dev_err(dev, "Invalid. ComboPhy is in Dual lane mode %d\n", iphy_id);
		return ERR_PTR(-EINVAL);
	}

	return cbphy->iphy[iphy_id].phy;
}

static int intel_cbphy_create(struct intel_combo_phy *cbphy)
{
	struct phy_provider *phy_provider;
	struct device *dev = cbphy->dev;
	struct intel_cbphy_iphy *iphy;
	int i;

	for (i = 0; i < PHY_MAX_NUM; i++) {
		iphy = &cbphy->iphy[i];
		iphy->parent = cbphy;
		iphy->id = i;

		/* In dual lane mode skip phy creation for the second phy */
		if (cbphy->aggr_mode == PHY_DL_MODE && iphy->id == PHY_1)
			continue;

		iphy->phy = devm_phy_create(dev, NULL, &intel_cbphy_ops);
		if (IS_ERR(iphy->phy)) {
			dev_err(dev, "PHY[%u:%u]: create PHY instance failed!\n",
				COMBO_PHY_ID(iphy), PHY_ID(iphy));

			return PTR_ERR(iphy->phy);
		}

		phy_set_drvdata(iphy->phy, iphy);
	}

	dev_set_drvdata(dev, cbphy);
	phy_provider = devm_of_phy_provider_register(dev, intel_cbphy_xlate);
	if (IS_ERR(phy_provider))
		dev_err(dev, "Register PHY provider failed!\n");

	return PTR_ERR_OR_ZERO(phy_provider);
}

static int intel_cbphy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct intel_combo_phy *cbphy;
	int ret;

	cbphy = devm_kzalloc(dev, sizeof(*cbphy), GFP_KERNEL);
	if (!cbphy)
		return -ENOMEM;

	cbphy->dev = dev;
	cbphy->init_cnt = 0;
	mutex_init(&cbphy->lock);
	ret = intel_cbphy_fwnode_parse(cbphy);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, cbphy);

	return intel_cbphy_create(cbphy);
}

static int intel_cbphy_remove(struct platform_device *pdev)
{
	struct intel_combo_phy *cbphy = platform_get_drvdata(pdev);

	intel_cbphy_rst_assert(cbphy);
	clk_disable_unprepare(cbphy->core_clk);
	return 0;
}

static const struct of_device_id of_intel_cbphy_match[] = {
	{ .compatible = "intel,combo-phy" },
	{ .compatible = "intel,combophy-lgm" },
	{}
};

static struct platform_driver intel_cbphy_driver = {
	.probe = intel_cbphy_probe,
	.remove = intel_cbphy_remove,
	.driver = {
		.name = "intel-combo-phy",
		.of_match_table = of_intel_cbphy_match,
	}
};

module_platform_driver(intel_cbphy_driver);

MODULE_DESCRIPTION("Intel Combo-phy driver");
MODULE_LICENSE("GPL v2");
