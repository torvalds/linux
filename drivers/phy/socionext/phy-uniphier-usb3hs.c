// SPDX-License-Identifier: GPL-2.0
/*
 * phy-uniphier-usb3hs.c - HS-PHY driver for Socionext UniPhier USB3 controller
 * Copyright 2015-2018 Socionext Inc.
 * Author:
 *      Kunihiko Hayashi <hayashi.kunihiko@socionext.com>
 * Contributors:
 *      Motoya Tanigawa <tanigawa.motoya@socionext.com>
 *      Masami Hiramatsu <masami.hiramatsu@linaro.org>
 */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/nvmem-consumer.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/reset.h>
#include <linux/slab.h>

#define HSPHY_CFG0		0x0
#define HSPHY_CFG0_HS_I_MASK	GENMASK(31, 28)
#define HSPHY_CFG0_HSDISC_MASK	GENMASK(27, 26)
#define HSPHY_CFG0_SWING_MASK	GENMASK(17, 16)
#define HSPHY_CFG0_SEL_T_MASK	GENMASK(15, 12)
#define HSPHY_CFG0_RTERM_MASK	GENMASK(7, 6)
#define HSPHY_CFG0_TRIMMASK	(HSPHY_CFG0_HS_I_MASK \
				 | HSPHY_CFG0_SEL_T_MASK \
				 | HSPHY_CFG0_RTERM_MASK)

#define HSPHY_CFG1		0x4
#define HSPHY_CFG1_DAT_EN	BIT(29)
#define HSPHY_CFG1_ADR_EN	BIT(28)
#define HSPHY_CFG1_ADR_MASK	GENMASK(27, 16)
#define HSPHY_CFG1_DAT_MASK	GENMASK(23, 16)

#define PHY_F(regno, msb, lsb) { (regno), (msb), (lsb) }

#define RX_CHK_SYNC	PHY_F(0, 5, 5)	/* RX sync mode */
#define RX_SYNC_SEL	PHY_F(1, 1, 0)	/* RX sync length */
#define LS_SLEW		PHY_F(10, 6, 6)	/* LS mode slew rate */
#define FS_LS_DRV	PHY_F(10, 5, 5)	/* FS/LS slew rate */

#define MAX_PHY_PARAMS	4

struct uniphier_u3hsphy_param {
	struct {
		int reg_no;
		int msb;
		int lsb;
	} field;
	u8 value;
};

struct uniphier_u3hsphy_trim_param {
	unsigned int rterm;
	unsigned int sel_t;
	unsigned int hs_i;
};

#define trim_param_is_valid(p)	((p)->rterm || (p)->sel_t || (p)->hs_i)

struct uniphier_u3hsphy_priv {
	struct device *dev;
	void __iomem *base;
	struct clk *clk, *clk_parent, *clk_ext, *clk_parent_gio;
	struct reset_control *rst, *rst_parent, *rst_parent_gio;
	struct regulator *vbus;
	const struct uniphier_u3hsphy_soc_data *data;
};

struct uniphier_u3hsphy_soc_data {
	bool is_legacy;
	int nparams;
	const struct uniphier_u3hsphy_param param[MAX_PHY_PARAMS];
	u32 config0;
	u32 config1;
	void (*trim_func)(struct uniphier_u3hsphy_priv *priv, u32 *pconfig,
			  struct uniphier_u3hsphy_trim_param *pt);
};

static void uniphier_u3hsphy_trim_ld20(struct uniphier_u3hsphy_priv *priv,
				       u32 *pconfig,
				       struct uniphier_u3hsphy_trim_param *pt)
{
	*pconfig &= ~HSPHY_CFG0_RTERM_MASK;
	*pconfig |= FIELD_PREP(HSPHY_CFG0_RTERM_MASK, pt->rterm);

	*pconfig &= ~HSPHY_CFG0_SEL_T_MASK;
	*pconfig |= FIELD_PREP(HSPHY_CFG0_SEL_T_MASK, pt->sel_t);

	*pconfig &= ~HSPHY_CFG0_HS_I_MASK;
	*pconfig |= FIELD_PREP(HSPHY_CFG0_HS_I_MASK,  pt->hs_i);
}

static int uniphier_u3hsphy_get_nvparam(struct uniphier_u3hsphy_priv *priv,
					const char *name, unsigned int *val)
{
	struct nvmem_cell *cell;
	u8 *buf;

	cell = devm_nvmem_cell_get(priv->dev, name);
	if (IS_ERR(cell))
		return PTR_ERR(cell);

	buf = nvmem_cell_read(cell, NULL);
	if (IS_ERR(buf))
		return PTR_ERR(buf);

	*val = *buf;

	kfree(buf);

	return 0;
}

static int uniphier_u3hsphy_get_nvparams(struct uniphier_u3hsphy_priv *priv,
					 struct uniphier_u3hsphy_trim_param *pt)
{
	int ret;

	ret = uniphier_u3hsphy_get_nvparam(priv, "rterm", &pt->rterm);
	if (ret)
		return ret;

	ret = uniphier_u3hsphy_get_nvparam(priv, "sel_t", &pt->sel_t);
	if (ret)
		return ret;

	ret = uniphier_u3hsphy_get_nvparam(priv, "hs_i", &pt->hs_i);
	if (ret)
		return ret;

	return 0;
}

static int uniphier_u3hsphy_update_config(struct uniphier_u3hsphy_priv *priv,
					  u32 *pconfig)
{
	struct uniphier_u3hsphy_trim_param trim;
	int ret, trimmed = 0;

	if (priv->data->trim_func) {
		ret = uniphier_u3hsphy_get_nvparams(priv, &trim);
		if (ret == -EPROBE_DEFER)
			return ret;

		/*
		 * call trim_func only when trimming parameters that aren't
		 * all-zero can be acquired. All-zero parameters mean nothing
		 * has been written to nvmem.
		 */
		if (!ret && trim_param_is_valid(&trim)) {
			priv->data->trim_func(priv, pconfig, &trim);
			trimmed = 1;
		} else {
			dev_dbg(priv->dev, "can't get parameter from nvmem\n");
		}
	}

	/* use default parameters without trimming values */
	if (!trimmed) {
		*pconfig &= ~HSPHY_CFG0_HSDISC_MASK;
		*pconfig |= FIELD_PREP(HSPHY_CFG0_HSDISC_MASK, 3);
	}

	return 0;
}

static void uniphier_u3hsphy_set_param(struct uniphier_u3hsphy_priv *priv,
				       const struct uniphier_u3hsphy_param *p)
{
	u32 val;
	u32 field_mask = GENMASK(p->field.msb, p->field.lsb);
	u8 data;

	val = readl(priv->base + HSPHY_CFG1);
	val &= ~HSPHY_CFG1_ADR_MASK;
	val |= FIELD_PREP(HSPHY_CFG1_ADR_MASK, p->field.reg_no)
		| HSPHY_CFG1_ADR_EN;
	writel(val, priv->base + HSPHY_CFG1);

	val = readl(priv->base + HSPHY_CFG1);
	val &= ~HSPHY_CFG1_ADR_EN;
	writel(val, priv->base + HSPHY_CFG1);

	val = readl(priv->base + HSPHY_CFG1);
	val &= ~FIELD_PREP(HSPHY_CFG1_DAT_MASK, field_mask);
	data = field_mask & (p->value << p->field.lsb);
	val |=  FIELD_PREP(HSPHY_CFG1_DAT_MASK, data) | HSPHY_CFG1_DAT_EN;
	writel(val, priv->base + HSPHY_CFG1);

	val = readl(priv->base + HSPHY_CFG1);
	val &= ~HSPHY_CFG1_DAT_EN;
	writel(val, priv->base + HSPHY_CFG1);
}

static int uniphier_u3hsphy_power_on(struct phy *phy)
{
	struct uniphier_u3hsphy_priv *priv = phy_get_drvdata(phy);
	int ret;

	ret = clk_prepare_enable(priv->clk_ext);
	if (ret)
		return ret;

	ret = clk_prepare_enable(priv->clk);
	if (ret)
		goto out_clk_ext_disable;

	ret = reset_control_deassert(priv->rst);
	if (ret)
		goto out_clk_disable;

	if (priv->vbus) {
		ret = regulator_enable(priv->vbus);
		if (ret)
			goto out_rst_assert;
	}

	return 0;

out_rst_assert:
	reset_control_assert(priv->rst);
out_clk_disable:
	clk_disable_unprepare(priv->clk);
out_clk_ext_disable:
	clk_disable_unprepare(priv->clk_ext);

	return ret;
}

static int uniphier_u3hsphy_power_off(struct phy *phy)
{
	struct uniphier_u3hsphy_priv *priv = phy_get_drvdata(phy);

	if (priv->vbus)
		regulator_disable(priv->vbus);

	reset_control_assert(priv->rst);
	clk_disable_unprepare(priv->clk);
	clk_disable_unprepare(priv->clk_ext);

	return 0;
}

static int uniphier_u3hsphy_init(struct phy *phy)
{
	struct uniphier_u3hsphy_priv *priv = phy_get_drvdata(phy);
	u32 config0, config1;
	int i, ret;

	ret = clk_prepare_enable(priv->clk_parent);
	if (ret)
		return ret;

	ret = clk_prepare_enable(priv->clk_parent_gio);
	if (ret)
		goto out_clk_disable;

	ret = reset_control_deassert(priv->rst_parent);
	if (ret)
		goto out_clk_gio_disable;

	ret = reset_control_deassert(priv->rst_parent_gio);
	if (ret)
		goto out_rst_assert;

	if ((priv->data->is_legacy)
	    || (!priv->data->config0 && !priv->data->config1))
		return 0;

	config0 = priv->data->config0;
	config1 = priv->data->config1;

	ret = uniphier_u3hsphy_update_config(priv, &config0);
	if (ret)
		goto out_rst_assert;

	writel(config0, priv->base + HSPHY_CFG0);
	writel(config1, priv->base + HSPHY_CFG1);

	for (i = 0; i < priv->data->nparams; i++)
		uniphier_u3hsphy_set_param(priv, &priv->data->param[i]);

	return 0;

out_rst_assert:
	reset_control_assert(priv->rst_parent);
out_clk_gio_disable:
	clk_disable_unprepare(priv->clk_parent_gio);
out_clk_disable:
	clk_disable_unprepare(priv->clk_parent);

	return ret;
}

static int uniphier_u3hsphy_exit(struct phy *phy)
{
	struct uniphier_u3hsphy_priv *priv = phy_get_drvdata(phy);

	reset_control_assert(priv->rst_parent_gio);
	reset_control_assert(priv->rst_parent);
	clk_disable_unprepare(priv->clk_parent_gio);
	clk_disable_unprepare(priv->clk_parent);

	return 0;
}

static const struct phy_ops uniphier_u3hsphy_ops = {
	.init           = uniphier_u3hsphy_init,
	.exit           = uniphier_u3hsphy_exit,
	.power_on       = uniphier_u3hsphy_power_on,
	.power_off      = uniphier_u3hsphy_power_off,
	.owner          = THIS_MODULE,
};

static int uniphier_u3hsphy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct uniphier_u3hsphy_priv *priv;
	struct phy_provider *phy_provider;
	struct phy *phy;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;
	priv->data = of_device_get_match_data(dev);
	if (WARN_ON(!priv->data ||
		    priv->data->nparams > MAX_PHY_PARAMS))
		return -EINVAL;

	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	if (!priv->data->is_legacy) {
		priv->clk = devm_clk_get(dev, "phy");
		if (IS_ERR(priv->clk))
			return PTR_ERR(priv->clk);

		priv->clk_ext = devm_clk_get_optional(dev, "phy-ext");
		if (IS_ERR(priv->clk_ext))
			return PTR_ERR(priv->clk_ext);

		priv->rst = devm_reset_control_get_shared(dev, "phy");
		if (IS_ERR(priv->rst))
			return PTR_ERR(priv->rst);

	} else {
		priv->clk_parent_gio = devm_clk_get(dev, "gio");
		if (IS_ERR(priv->clk_parent_gio))
			return PTR_ERR(priv->clk_parent_gio);

		priv->rst_parent_gio =
			devm_reset_control_get_shared(dev, "gio");
		if (IS_ERR(priv->rst_parent_gio))
			return PTR_ERR(priv->rst_parent_gio);
	}

	priv->clk_parent = devm_clk_get(dev, "link");
	if (IS_ERR(priv->clk_parent))
		return PTR_ERR(priv->clk_parent);

	priv->rst_parent = devm_reset_control_get_shared(dev, "link");
	if (IS_ERR(priv->rst_parent))
		return PTR_ERR(priv->rst_parent);

	priv->vbus = devm_regulator_get_optional(dev, "vbus");
	if (IS_ERR(priv->vbus)) {
		if (PTR_ERR(priv->vbus) == -EPROBE_DEFER)
			return PTR_ERR(priv->vbus);
		priv->vbus = NULL;
	}

	phy = devm_phy_create(dev, dev->of_node, &uniphier_u3hsphy_ops);
	if (IS_ERR(phy))
		return PTR_ERR(phy);

	phy_set_drvdata(phy, priv);
	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);

	return PTR_ERR_OR_ZERO(phy_provider);
}

static const struct uniphier_u3hsphy_soc_data uniphier_pro5_data = {
	.is_legacy = true,
	.nparams = 0,
};

static const struct uniphier_u3hsphy_soc_data uniphier_pxs2_data = {
	.is_legacy = false,
	.nparams = 2,
	.param = {
		{ RX_CHK_SYNC, 1 },
		{ RX_SYNC_SEL, 1 },
	},
};

static const struct uniphier_u3hsphy_soc_data uniphier_ld20_data = {
	.is_legacy = false,
	.nparams = 4,
	.param = {
		{ RX_CHK_SYNC, 1 },
		{ RX_SYNC_SEL, 1 },
		{ LS_SLEW, 1 },
		{ FS_LS_DRV, 1 },
	},
	.trim_func = uniphier_u3hsphy_trim_ld20,
	.config0 = 0x92316680,
	.config1 = 0x00000106,
};

static const struct uniphier_u3hsphy_soc_data uniphier_pxs3_data = {
	.is_legacy = false,
	.nparams = 2,
	.param = {
		{ RX_CHK_SYNC, 1 },
		{ RX_SYNC_SEL, 1 },
	},
	.trim_func = uniphier_u3hsphy_trim_ld20,
	.config0 = 0x92316680,
	.config1 = 0x00000106,
};

static const struct of_device_id uniphier_u3hsphy_match[] = {
	{
		.compatible = "socionext,uniphier-pro5-usb3-hsphy",
		.data = &uniphier_pro5_data,
	},
	{
		.compatible = "socionext,uniphier-pxs2-usb3-hsphy",
		.data = &uniphier_pxs2_data,
	},
	{
		.compatible = "socionext,uniphier-ld20-usb3-hsphy",
		.data = &uniphier_ld20_data,
	},
	{
		.compatible = "socionext,uniphier-pxs3-usb3-hsphy",
		.data = &uniphier_pxs3_data,
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, uniphier_u3hsphy_match);

static struct platform_driver uniphier_u3hsphy_driver = {
	.probe = uniphier_u3hsphy_probe,
	.driver	= {
		.name = "uniphier-usb3-hsphy",
		.of_match_table	= uniphier_u3hsphy_match,
	},
};

module_platform_driver(uniphier_u3hsphy_driver);

MODULE_AUTHOR("Kunihiko Hayashi <hayashi.kunihiko@socionext.com>");
MODULE_DESCRIPTION("UniPhier HS-PHY driver for USB3 controller");
MODULE_LICENSE("GPL v2");
