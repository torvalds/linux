// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2022 NXP, Peng Fan <peng.fan@nxp.com>
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/sizes.h>

#include <dt-bindings/power/fsl,imx93-power.h>

#define BLK_SFT_RSTN	0x0
#define BLK_CLK_EN	0x4
#define BLK_MAX_CLKS	4

#define DOMAIN_MAX_CLKS 4

#define LCDIF_QOS_REG		0xC
#define LCDIF_DEFAULT_QOS_OFF	12
#define LCDIF_CFG_QOS_OFF	8

#define PXP_QOS_REG		0x10
#define PXP_R_DEFAULT_QOS_OFF	28
#define PXP_R_CFG_QOS_OFF	24
#define PXP_W_DEFAULT_QOS_OFF	20
#define PXP_W_CFG_QOS_OFF	16

#define ISI_CACHE_REG		0x14

#define ISI_QOS_REG		0x1C
#define ISI_V_DEFAULT_QOS_OFF	28
#define ISI_V_CFG_QOS_OFF	24
#define ISI_U_DEFAULT_QOS_OFF	20
#define ISI_U_CFG_QOS_OFF	16
#define ISI_Y_R_DEFAULT_QOS_OFF	12
#define ISI_Y_R_CFG_QOS_OFF	8
#define ISI_Y_W_DEFAULT_QOS_OFF	4
#define ISI_Y_W_CFG_QOS_OFF	0

#define PRIO_MASK		0xF

#define PRIO(X)			(X)

struct imx93_blk_ctrl_domain;

struct imx93_blk_ctrl {
	struct device *dev;
	struct regmap *regmap;
	int num_clks;
	struct clk_bulk_data clks[BLK_MAX_CLKS];
	struct imx93_blk_ctrl_domain *domains;
	struct genpd_onecell_data onecell_data;
};

#define DOMAIN_MAX_QOS 4

struct imx93_blk_ctrl_qos {
	u32 reg;
	u32 cfg_off;
	u32 default_prio;
	u32 cfg_prio;
};

struct imx93_blk_ctrl_domain_data {
	const char *name;
	const char * const *clk_names;
	int num_clks;
	u32 rst_mask;
	u32 clk_mask;
	int num_qos;
	struct imx93_blk_ctrl_qos qos[DOMAIN_MAX_QOS];
};

struct imx93_blk_ctrl_domain {
	struct generic_pm_domain genpd;
	const struct imx93_blk_ctrl_domain_data *data;
	struct clk_bulk_data clks[DOMAIN_MAX_CLKS];
	struct imx93_blk_ctrl *bc;
};

struct imx93_blk_ctrl_data {
	const struct imx93_blk_ctrl_domain_data *domains;
	int num_domains;
	const char * const *clk_names;
	int num_clks;
	const struct regmap_access_table *reg_access_table;
};

static inline struct imx93_blk_ctrl_domain *
to_imx93_blk_ctrl_domain(struct generic_pm_domain *genpd)
{
	return container_of(genpd, struct imx93_blk_ctrl_domain, genpd);
}

static int imx93_blk_ctrl_set_qos(struct imx93_blk_ctrl_domain *domain)
{
	const struct imx93_blk_ctrl_domain_data *data = domain->data;
	struct imx93_blk_ctrl *bc = domain->bc;
	const struct imx93_blk_ctrl_qos *qos;
	u32 val, mask;
	int i;

	for (i = 0; i < data->num_qos; i++) {
		qos = &data->qos[i];

		mask = PRIO_MASK << qos->cfg_off;
		mask |= PRIO_MASK << (qos->cfg_off + 4);
		val = qos->cfg_prio << qos->cfg_off;
		val |= qos->default_prio << (qos->cfg_off + 4);

		regmap_write_bits(bc->regmap, qos->reg, mask, val);

		dev_dbg(bc->dev, "data->qos[i].reg 0x%x 0x%x\n", qos->reg, val);
	}

	return 0;
}

static int imx93_blk_ctrl_power_on(struct generic_pm_domain *genpd)
{
	struct imx93_blk_ctrl_domain *domain = to_imx93_blk_ctrl_domain(genpd);
	const struct imx93_blk_ctrl_domain_data *data = domain->data;
	struct imx93_blk_ctrl *bc = domain->bc;
	int ret;

	ret = clk_bulk_prepare_enable(bc->num_clks, bc->clks);
	if (ret) {
		dev_err(bc->dev, "failed to enable bus clocks\n");
		return ret;
	}

	ret = clk_bulk_prepare_enable(data->num_clks, domain->clks);
	if (ret) {
		clk_bulk_disable_unprepare(bc->num_clks, bc->clks);
		dev_err(bc->dev, "failed to enable clocks\n");
		return ret;
	}

	ret = pm_runtime_get_sync(bc->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(bc->dev);
		dev_err(bc->dev, "failed to power up domain\n");
		goto disable_clk;
	}

	/* ungate clk */
	regmap_clear_bits(bc->regmap, BLK_CLK_EN, data->clk_mask);

	/* release reset */
	regmap_set_bits(bc->regmap, BLK_SFT_RSTN, data->rst_mask);

	dev_dbg(bc->dev, "pd_on: name: %s\n", genpd->name);

	return imx93_blk_ctrl_set_qos(domain);

disable_clk:
	clk_bulk_disable_unprepare(data->num_clks, domain->clks);

	clk_bulk_disable_unprepare(bc->num_clks, bc->clks);

	return ret;
}

static int imx93_blk_ctrl_power_off(struct generic_pm_domain *genpd)
{
	struct imx93_blk_ctrl_domain *domain = to_imx93_blk_ctrl_domain(genpd);
	const struct imx93_blk_ctrl_domain_data *data = domain->data;
	struct imx93_blk_ctrl *bc = domain->bc;

	dev_dbg(bc->dev, "pd_off: name: %s\n", genpd->name);

	regmap_clear_bits(bc->regmap, BLK_SFT_RSTN, data->rst_mask);
	regmap_set_bits(bc->regmap, BLK_CLK_EN, data->clk_mask);

	pm_runtime_put(bc->dev);

	clk_bulk_disable_unprepare(data->num_clks, domain->clks);

	clk_bulk_disable_unprepare(bc->num_clks, bc->clks);

	return 0;
}

static struct lock_class_key blk_ctrl_genpd_lock_class;

static int imx93_blk_ctrl_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct imx93_blk_ctrl_data *bc_data = of_device_get_match_data(dev);
	struct imx93_blk_ctrl *bc;
	void __iomem *base;
	int i, ret;

	struct regmap_config regmap_config = {
		.reg_bits	= 32,
		.val_bits	= 32,
		.reg_stride	= 4,
		.rd_table	= bc_data->reg_access_table,
		.wr_table	= bc_data->reg_access_table,
		.max_register   = SZ_4K,
	};

	bc = devm_kzalloc(dev, sizeof(*bc), GFP_KERNEL);
	if (!bc)
		return -ENOMEM;

	bc->dev = dev;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	bc->regmap = devm_regmap_init_mmio(dev, base, &regmap_config);
	if (IS_ERR(bc->regmap))
		return dev_err_probe(dev, PTR_ERR(bc->regmap),
				     "failed to init regmap\n");

	bc->domains = devm_kcalloc(dev, bc_data->num_domains,
				   sizeof(struct imx93_blk_ctrl_domain),
				   GFP_KERNEL);
	if (!bc->domains)
		return -ENOMEM;

	bc->onecell_data.num_domains = bc_data->num_domains;
	bc->onecell_data.domains =
		devm_kcalloc(dev, bc_data->num_domains,
			     sizeof(struct generic_pm_domain *), GFP_KERNEL);
	if (!bc->onecell_data.domains)
		return -ENOMEM;

	for (i = 0; i < bc_data->num_clks; i++)
		bc->clks[i].id = bc_data->clk_names[i];
	bc->num_clks = bc_data->num_clks;

	ret = devm_clk_bulk_get(dev, bc->num_clks, bc->clks);
	if (ret) {
		dev_err_probe(dev, ret, "failed to get bus clock\n");
		return ret;
	}

	for (i = 0; i < bc_data->num_domains; i++) {
		const struct imx93_blk_ctrl_domain_data *data = &bc_data->domains[i];
		struct imx93_blk_ctrl_domain *domain = &bc->domains[i];
		int j;

		domain->data = data;

		for (j = 0; j < data->num_clks; j++)
			domain->clks[j].id = data->clk_names[j];

		ret = devm_clk_bulk_get(dev, data->num_clks, domain->clks);
		if (ret) {
			dev_err_probe(dev, ret, "failed to get clock\n");
			goto cleanup_pds;
		}

		domain->genpd.name = data->name;
		domain->genpd.power_on = imx93_blk_ctrl_power_on;
		domain->genpd.power_off = imx93_blk_ctrl_power_off;
		domain->bc = bc;

		ret = pm_genpd_init(&domain->genpd, NULL, true);
		if (ret) {
			dev_err_probe(dev, ret, "failed to init power domain\n");
			goto cleanup_pds;
		}

		/*
		 * We use runtime PM to trigger power on/off of the upstream GPC
		 * domain, as a strict hierarchical parent/child power domain
		 * setup doesn't allow us to meet the sequencing requirements.
		 * This means we have nested locking of genpd locks, without the
		 * nesting being visible at the genpd level, so we need a
		 * separate lock class to make lockdep aware of the fact that
		 * this are separate domain locks that can be nested without a
		 * self-deadlock.
		 */
		lockdep_set_class(&domain->genpd.mlock,
				  &blk_ctrl_genpd_lock_class);

		bc->onecell_data.domains[i] = &domain->genpd;
	}

	pm_runtime_enable(dev);

	ret = of_genpd_add_provider_onecell(dev->of_node, &bc->onecell_data);
	if (ret) {
		dev_err_probe(dev, ret, "failed to add power domain provider\n");
		goto cleanup_pds;
	}

	dev_set_drvdata(dev, bc);

	return 0;

cleanup_pds:
	for (i--; i >= 0; i--)
		pm_genpd_remove(&bc->domains[i].genpd);

	return ret;
}

static int imx93_blk_ctrl_remove(struct platform_device *pdev)
{
	struct imx93_blk_ctrl *bc = dev_get_drvdata(&pdev->dev);
	int i;

	of_genpd_del_provider(pdev->dev.of_node);

	for (i = 0; bc->onecell_data.num_domains; i++) {
		struct imx93_blk_ctrl_domain *domain = &bc->domains[i];

		pm_genpd_remove(&domain->genpd);
	}

	return 0;
}

static const struct imx93_blk_ctrl_domain_data imx93_media_blk_ctl_domain_data[] = {
	[IMX93_MEDIABLK_PD_MIPI_DSI] = {
		.name = "mediablk-mipi-dsi",
		.clk_names = (const char *[]){ "dsi" },
		.num_clks = 1,
		.rst_mask = BIT(11) | BIT(12),
		.clk_mask = BIT(11) | BIT(12),
	},
	[IMX93_MEDIABLK_PD_MIPI_CSI] = {
		.name = "mediablk-mipi-csi",
		.clk_names = (const char *[]){ "cam", "csi" },
		.num_clks = 2,
		.rst_mask = BIT(9) | BIT(10),
		.clk_mask = BIT(9) | BIT(10),
	},
	[IMX93_MEDIABLK_PD_PXP] = {
		.name = "mediablk-pxp",
		.clk_names = (const char *[]){ "pxp" },
		.num_clks = 1,
		.rst_mask = BIT(7) | BIT(8),
		.clk_mask = BIT(7) | BIT(8),
		.num_qos = 2,
		.qos = {
			{
				.reg = PXP_QOS_REG,
				.cfg_off = PXP_R_CFG_QOS_OFF,
				.default_prio = PRIO(3),
				.cfg_prio = PRIO(6),
			}, {
				.reg = PXP_QOS_REG,
				.cfg_off = PXP_W_CFG_QOS_OFF,
				.default_prio = PRIO(3),
				.cfg_prio = PRIO(6),
			}
		}
	},
	[IMX93_MEDIABLK_PD_LCDIF] = {
		.name = "mediablk-lcdif",
		.clk_names = (const char *[]){ "disp", "lcdif" },
		.num_clks = 2,
		.rst_mask = BIT(4) | BIT(5) | BIT(6),
		.clk_mask = BIT(4) | BIT(5) | BIT(6),
		.num_qos = 1,
		.qos = {
			{
			.reg = LCDIF_QOS_REG,
			.cfg_off = LCDIF_CFG_QOS_OFF,
			.default_prio = PRIO(3),
			.cfg_prio = PRIO(7),
			}
		}
	},
	[IMX93_MEDIABLK_PD_ISI] = {
		.name = "mediablk-isi",
		.clk_names = (const char *[]){ "isi" },
		.num_clks = 1,
		.rst_mask = BIT(2) | BIT(3),
		.clk_mask = BIT(2) | BIT(3),
		.num_qos = 4,
		.qos = {
			{
				.reg = ISI_QOS_REG,
				.cfg_off = ISI_Y_W_CFG_QOS_OFF,
				.default_prio = PRIO(3),
				.cfg_prio = PRIO(7),
			}, {
				.reg = ISI_QOS_REG,
				.cfg_off = ISI_Y_R_CFG_QOS_OFF,
				.default_prio = PRIO(3),
				.cfg_prio = PRIO(7),
			}, {
				.reg = ISI_QOS_REG,
				.cfg_off = ISI_U_CFG_QOS_OFF,
				.default_prio = PRIO(3),
				.cfg_prio = PRIO(7),
			}, {
				.reg = ISI_QOS_REG,
				.cfg_off = ISI_V_CFG_QOS_OFF,
				.default_prio = PRIO(3),
				.cfg_prio = PRIO(7),
			}
		}
	},
};

static const struct regmap_range imx93_media_blk_ctl_yes_ranges[] = {
	regmap_reg_range(BLK_SFT_RSTN, BLK_CLK_EN),
	regmap_reg_range(LCDIF_QOS_REG, ISI_CACHE_REG),
	regmap_reg_range(ISI_QOS_REG, ISI_QOS_REG),
};

static const struct regmap_access_table imx93_media_blk_ctl_access_table = {
	.yes_ranges = imx93_media_blk_ctl_yes_ranges,
	.n_yes_ranges = ARRAY_SIZE(imx93_media_blk_ctl_yes_ranges),
};

static const struct imx93_blk_ctrl_data imx93_media_blk_ctl_dev_data = {
	.domains = imx93_media_blk_ctl_domain_data,
	.num_domains = ARRAY_SIZE(imx93_media_blk_ctl_domain_data),
	.clk_names = (const char *[]){ "axi", "apb", "nic", },
	.num_clks = 3,
	.reg_access_table = &imx93_media_blk_ctl_access_table,
};

static const struct of_device_id imx93_blk_ctrl_of_match[] = {
	{
		.compatible = "fsl,imx93-media-blk-ctrl",
		.data = &imx93_media_blk_ctl_dev_data
	}, {
		/* Sentinel */
	}
};
MODULE_DEVICE_TABLE(of, imx93_blk_ctrl_of_match);

static struct platform_driver imx93_blk_ctrl_driver = {
	.probe = imx93_blk_ctrl_probe,
	.remove = imx93_blk_ctrl_remove,
	.driver = {
		.name = "imx93-blk-ctrl",
		.of_match_table = imx93_blk_ctrl_of_match,
	},
};
module_platform_driver(imx93_blk_ctrl_driver);

MODULE_AUTHOR("Peng Fan <peng.fan@nxp.com>");
MODULE_DESCRIPTION("i.MX93 BLK CTRL driver");
MODULE_LICENSE("GPL");
