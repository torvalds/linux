// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2015-2017 Pengutronix, Lucas Stach <kernel@pengutronix.de>
 * Copyright 2011-2013 Freescale Semiconductor, Inc.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>

#define GPC_CNTR		0x000

#define GPC_PGC_CTRL_OFFS	0x0
#define GPC_PGC_PUPSCR_OFFS	0x4
#define GPC_PGC_PDNSCR_OFFS	0x8
#define GPC_PGC_SW2ISO_SHIFT	0x8
#define GPC_PGC_SW_SHIFT	0x0

#define GPC_PGC_PCI_PDN		0x200
#define GPC_PGC_PCI_SR		0x20c

#define GPC_PGC_GPU_PDN		0x260
#define GPC_PGC_GPU_PUPSCR	0x264
#define GPC_PGC_GPU_PDNSCR	0x268
#define GPC_PGC_GPU_SR		0x26c

#define GPC_PGC_DISP_PDN	0x240
#define GPC_PGC_DISP_SR		0x24c

#define GPU_VPU_PUP_REQ		BIT(1)
#define GPU_VPU_PDN_REQ		BIT(0)

#define GPC_CLK_MAX		7

#define PGC_DOMAIN_FLAG_NO_PD		BIT(0)

struct imx_pm_domain {
	struct generic_pm_domain base;
	struct regmap *regmap;
	struct regulator *supply;
	struct clk *clk[GPC_CLK_MAX];
	int num_clks;
	unsigned int reg_offs;
	signed char cntr_pdn_bit;
	unsigned int ipg_rate_mhz;
};

static inline struct imx_pm_domain *
to_imx_pm_domain(struct generic_pm_domain *genpd)
{
	return container_of(genpd, struct imx_pm_domain, base);
}

static int imx6_pm_domain_power_off(struct generic_pm_domain *genpd)
{
	struct imx_pm_domain *pd = to_imx_pm_domain(genpd);
	int iso, iso2sw;
	u32 val;

	/* Read ISO and ISO2SW power down delays */
	regmap_read(pd->regmap, pd->reg_offs + GPC_PGC_PDNSCR_OFFS, &val);
	iso = val & 0x3f;
	iso2sw = (val >> 8) & 0x3f;

	/* Gate off domain when powered down */
	regmap_update_bits(pd->regmap, pd->reg_offs + GPC_PGC_CTRL_OFFS,
			   0x1, 0x1);

	/* Request GPC to power down domain */
	val = BIT(pd->cntr_pdn_bit);
	regmap_update_bits(pd->regmap, GPC_CNTR, val, val);

	/* Wait ISO + ISO2SW IPG clock cycles */
	udelay(DIV_ROUND_UP(iso + iso2sw, pd->ipg_rate_mhz));

	if (pd->supply)
		regulator_disable(pd->supply);

	return 0;
}

static int imx6_pm_domain_power_on(struct generic_pm_domain *genpd)
{
	struct imx_pm_domain *pd = to_imx_pm_domain(genpd);
	int i, ret;
	u32 val, req;

	if (pd->supply) {
		ret = regulator_enable(pd->supply);
		if (ret) {
			pr_err("%s: failed to enable regulator: %d\n",
			       __func__, ret);
			return ret;
		}
	}

	/* Enable reset clocks for all devices in the domain */
	for (i = 0; i < pd->num_clks; i++)
		clk_prepare_enable(pd->clk[i]);

	/* Gate off domain when powered down */
	regmap_update_bits(pd->regmap, pd->reg_offs + GPC_PGC_CTRL_OFFS,
			   0x1, 0x1);

	/* Request GPC to power up domain */
	req = BIT(pd->cntr_pdn_bit + 1);
	regmap_update_bits(pd->regmap, GPC_CNTR, req, req);

	/* Wait for the PGC to handle the request */
	ret = regmap_read_poll_timeout(pd->regmap, GPC_CNTR, val, !(val & req),
				       1, 50);
	if (ret)
		pr_err("powerup request on domain %s timed out\n", genpd->name);

	/* Wait for reset to propagate through peripherals */
	usleep_range(5, 10);

	/* Disable reset clocks for all devices in the domain */
	for (i = 0; i < pd->num_clks; i++)
		clk_disable_unprepare(pd->clk[i]);

	return 0;
}

static int imx_pgc_get_clocks(struct device *dev, struct imx_pm_domain *domain)
{
	int i, ret;

	for (i = 0; ; i++) {
		struct clk *clk = of_clk_get(dev->of_node, i);
		if (IS_ERR(clk))
			break;
		if (i >= GPC_CLK_MAX) {
			dev_err(dev, "more than %d clocks\n", GPC_CLK_MAX);
			ret = -EINVAL;
			goto clk_err;
		}
		domain->clk[i] = clk;
	}
	domain->num_clks = i;

	return 0;

clk_err:
	while (i--)
		clk_put(domain->clk[i]);

	return ret;
}

static void imx_pgc_put_clocks(struct imx_pm_domain *domain)
{
	int i;

	for (i = domain->num_clks - 1; i >= 0; i--)
		clk_put(domain->clk[i]);
}

static int imx_pgc_parse_dt(struct device *dev, struct imx_pm_domain *domain)
{
	/* try to get the domain supply regulator */
	domain->supply = devm_regulator_get_optional(dev, "power");
	if (IS_ERR(domain->supply)) {
		if (PTR_ERR(domain->supply) == -ENODEV)
			domain->supply = NULL;
		else
			return PTR_ERR(domain->supply);
	}

	/* try to get all clocks needed for reset propagation */
	return imx_pgc_get_clocks(dev, domain);
}

static int imx_pgc_power_domain_probe(struct platform_device *pdev)
{
	struct imx_pm_domain *domain = pdev->dev.platform_data;
	struct device *dev = &pdev->dev;
	int ret;

	/* if this PD is associated with a DT node try to parse it */
	if (dev->of_node) {
		ret = imx_pgc_parse_dt(dev, domain);
		if (ret)
			return ret;
	}

	/* initially power on the domain */
	if (domain->base.power_on)
		domain->base.power_on(&domain->base);

	if (IS_ENABLED(CONFIG_PM_GENERIC_DOMAINS)) {
		pm_genpd_init(&domain->base, NULL, false);
		ret = of_genpd_add_provider_simple(dev->of_node, &domain->base);
		if (ret)
			goto genpd_err;
	}

	device_link_add(dev, dev->parent, DL_FLAG_AUTOREMOVE_CONSUMER);

	return 0;

genpd_err:
	pm_genpd_remove(&domain->base);
	imx_pgc_put_clocks(domain);

	return ret;
}

static void imx_pgc_power_domain_remove(struct platform_device *pdev)
{
	struct imx_pm_domain *domain = pdev->dev.platform_data;

	if (IS_ENABLED(CONFIG_PM_GENERIC_DOMAINS)) {
		of_genpd_del_provider(pdev->dev.of_node);
		pm_genpd_remove(&domain->base);
		imx_pgc_put_clocks(domain);
	}
}

static const struct platform_device_id imx_pgc_power_domain_id[] = {
	{ "imx-pgc-power-domain"},
	{ },
};

static struct platform_driver imx_pgc_power_domain_driver = {
	.driver = {
		.name = "imx-pgc-pd",
	},
	.probe = imx_pgc_power_domain_probe,
	.remove_new = imx_pgc_power_domain_remove,
	.id_table = imx_pgc_power_domain_id,
};
builtin_platform_driver(imx_pgc_power_domain_driver)

#define GPC_PGC_DOMAIN_ARM	0
#define GPC_PGC_DOMAIN_PU	1
#define GPC_PGC_DOMAIN_DISPLAY	2
#define GPC_PGC_DOMAIN_PCI	3

static struct genpd_power_state imx6_pm_domain_pu_state = {
	.power_off_latency_ns = 25000,
	.power_on_latency_ns = 2000000,
};

static struct imx_pm_domain imx_gpc_domains[] = {
	[GPC_PGC_DOMAIN_ARM] = {
		.base = {
			.name = "ARM",
			.flags = GENPD_FLAG_ALWAYS_ON,
		},
	},
	[GPC_PGC_DOMAIN_PU] = {
		.base = {
			.name = "PU",
			.power_off = imx6_pm_domain_power_off,
			.power_on = imx6_pm_domain_power_on,
			.states = &imx6_pm_domain_pu_state,
			.state_count = 1,
		},
		.reg_offs = 0x260,
		.cntr_pdn_bit = 0,
	},
	[GPC_PGC_DOMAIN_DISPLAY] = {
		.base = {
			.name = "DISPLAY",
			.power_off = imx6_pm_domain_power_off,
			.power_on = imx6_pm_domain_power_on,
		},
		.reg_offs = 0x240,
		.cntr_pdn_bit = 4,
	},
	[GPC_PGC_DOMAIN_PCI] = {
		.base = {
			.name = "PCI",
			.power_off = imx6_pm_domain_power_off,
			.power_on = imx6_pm_domain_power_on,
		},
		.reg_offs = 0x200,
		.cntr_pdn_bit = 6,
	},
};

struct imx_gpc_dt_data {
	int num_domains;
	bool err009619_present;
	bool err006287_present;
};

static const struct imx_gpc_dt_data imx6q_dt_data = {
	.num_domains = 2,
	.err009619_present = false,
	.err006287_present = false,
};

static const struct imx_gpc_dt_data imx6qp_dt_data = {
	.num_domains = 2,
	.err009619_present = true,
	.err006287_present = false,
};

static const struct imx_gpc_dt_data imx6sl_dt_data = {
	.num_domains = 3,
	.err009619_present = false,
	.err006287_present = true,
};

static const struct imx_gpc_dt_data imx6sx_dt_data = {
	.num_domains = 4,
	.err009619_present = false,
	.err006287_present = false,
};

static const struct of_device_id imx_gpc_dt_ids[] = {
	{ .compatible = "fsl,imx6q-gpc", .data = &imx6q_dt_data },
	{ .compatible = "fsl,imx6qp-gpc", .data = &imx6qp_dt_data },
	{ .compatible = "fsl,imx6sl-gpc", .data = &imx6sl_dt_data },
	{ .compatible = "fsl,imx6sx-gpc", .data = &imx6sx_dt_data },
	{ }
};

static const struct regmap_range yes_ranges[] = {
	regmap_reg_range(GPC_CNTR, GPC_CNTR),
	regmap_reg_range(GPC_PGC_PCI_PDN, GPC_PGC_PCI_SR),
	regmap_reg_range(GPC_PGC_GPU_PDN, GPC_PGC_GPU_SR),
	regmap_reg_range(GPC_PGC_DISP_PDN, GPC_PGC_DISP_SR),
};

static const struct regmap_access_table access_table = {
	.yes_ranges	= yes_ranges,
	.n_yes_ranges	= ARRAY_SIZE(yes_ranges),
};

static const struct regmap_config imx_gpc_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.rd_table = &access_table,
	.wr_table = &access_table,
	.max_register = 0x2ac,
	.fast_io = true,
};

static struct generic_pm_domain *imx_gpc_onecell_domains[] = {
	&imx_gpc_domains[GPC_PGC_DOMAIN_ARM].base,
	&imx_gpc_domains[GPC_PGC_DOMAIN_PU].base,
};

static struct genpd_onecell_data imx_gpc_onecell_data = {
	.domains = imx_gpc_onecell_domains,
	.num_domains = 2,
};

static int imx_gpc_old_dt_init(struct device *dev, struct regmap *regmap,
			       unsigned int num_domains)
{
	struct imx_pm_domain *domain;
	int i, ret;

	for (i = 0; i < num_domains; i++) {
		domain = &imx_gpc_domains[i];
		domain->regmap = regmap;
		domain->ipg_rate_mhz = 66;

		if (i == 1) {
			domain->supply = devm_regulator_get(dev, "pu");
			if (IS_ERR(domain->supply))
				return PTR_ERR(domain->supply);

			ret = imx_pgc_get_clocks(dev, domain);
			if (ret)
				goto clk_err;

			domain->base.power_on(&domain->base);
		}
	}

	for (i = 0; i < num_domains; i++)
		pm_genpd_init(&imx_gpc_domains[i].base, NULL, false);

	if (IS_ENABLED(CONFIG_PM_GENERIC_DOMAINS)) {
		ret = of_genpd_add_provider_onecell(dev->of_node,
						    &imx_gpc_onecell_data);
		if (ret)
			goto genpd_err;
	}

	return 0;

genpd_err:
	for (i = 0; i < num_domains; i++)
		pm_genpd_remove(&imx_gpc_domains[i].base);
	imx_pgc_put_clocks(&imx_gpc_domains[GPC_PGC_DOMAIN_PU]);
clk_err:
	return ret;
}

static int imx_gpc_probe(struct platform_device *pdev)
{
	const struct imx_gpc_dt_data *of_id_data = device_get_match_data(&pdev->dev);
	struct device_node *pgc_node;
	struct regmap *regmap;
	void __iomem *base;
	int ret;

	pgc_node = of_get_child_by_name(pdev->dev.of_node, "pgc");

	/* bail out if DT too old and doesn't provide the necessary info */
	if (!of_property_read_bool(pdev->dev.of_node, "#power-domain-cells") &&
	    !pgc_node)
		return 0;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	regmap = devm_regmap_init_mmio_clk(&pdev->dev, NULL, base,
					   &imx_gpc_regmap_config);
	if (IS_ERR(regmap)) {
		ret = PTR_ERR(regmap);
		dev_err(&pdev->dev, "failed to init regmap: %d\n",
			ret);
		return ret;
	}

	/*
	 * Disable PU power down by runtime PM if ERR009619 is present.
	 *
	 * The PRE clock will be paused for several cycles when turning on the
	 * PU domain LDO from power down state. If PRE is in use at that time,
	 * the IPU/PRG cannot get the correct display data from the PRE.
	 *
	 * This is not a concern when the whole system enters suspend state, so
	 * it's safe to power down PU in this case.
	 */
	if (of_id_data->err009619_present)
		imx_gpc_domains[GPC_PGC_DOMAIN_PU].base.flags |=
				GENPD_FLAG_RPM_ALWAYS_ON;

	/* Keep DISP always on if ERR006287 is present */
	if (of_id_data->err006287_present)
		imx_gpc_domains[GPC_PGC_DOMAIN_DISPLAY].base.flags |=
				GENPD_FLAG_ALWAYS_ON;

	if (!pgc_node) {
		ret = imx_gpc_old_dt_init(&pdev->dev, regmap,
					  of_id_data->num_domains);
		if (ret)
			return ret;
	} else {
		struct imx_pm_domain *domain;
		struct platform_device *pd_pdev;
		struct clk *ipg_clk;
		unsigned int ipg_rate_mhz;
		int domain_index;

		ipg_clk = devm_clk_get(&pdev->dev, "ipg");
		if (IS_ERR(ipg_clk))
			return PTR_ERR(ipg_clk);
		ipg_rate_mhz = clk_get_rate(ipg_clk) / 1000000;

		for_each_child_of_node_scoped(pgc_node, np) {
			ret = of_property_read_u32(np, "reg", &domain_index);
			if (ret)
				return ret;

			if (domain_index >= of_id_data->num_domains)
				continue;

			pd_pdev = platform_device_alloc("imx-pgc-power-domain",
							domain_index);
			if (!pd_pdev)
				return -ENOMEM;

			ret = platform_device_add_data(pd_pdev,
						       &imx_gpc_domains[domain_index],
						       sizeof(imx_gpc_domains[domain_index]));
			if (ret) {
				platform_device_put(pd_pdev);
				return ret;
			}
			domain = pd_pdev->dev.platform_data;
			domain->regmap = regmap;
			domain->ipg_rate_mhz = ipg_rate_mhz;

			pd_pdev->dev.parent = &pdev->dev;
			pd_pdev->dev.of_node = np;
			pd_pdev->dev.fwnode = of_fwnode_handle(np);

			ret = platform_device_add(pd_pdev);
			if (ret) {
				platform_device_put(pd_pdev);
				return ret;
			}
		}
	}

	return 0;
}

static void imx_gpc_remove(struct platform_device *pdev)
{
	struct device_node *pgc_node;
	int ret;

	pgc_node = of_get_child_by_name(pdev->dev.of_node, "pgc");

	/* bail out if DT too old and doesn't provide the necessary info */
	if (!of_property_read_bool(pdev->dev.of_node, "#power-domain-cells") &&
	    !pgc_node)
		return;

	/*
	 * If the old DT binding is used the toplevel driver needs to
	 * de-register the power domains
	 */
	if (!pgc_node) {
		of_genpd_del_provider(pdev->dev.of_node);

		ret = pm_genpd_remove(&imx_gpc_domains[GPC_PGC_DOMAIN_PU].base);
		if (ret) {
			dev_err(&pdev->dev, "Failed to remove PU power domain (%pe)\n",
				ERR_PTR(ret));
			return;
		}
		imx_pgc_put_clocks(&imx_gpc_domains[GPC_PGC_DOMAIN_PU]);

		ret = pm_genpd_remove(&imx_gpc_domains[GPC_PGC_DOMAIN_ARM].base);
		if (ret) {
			dev_err(&pdev->dev, "Failed to remove ARM power domain (%pe)\n",
				ERR_PTR(ret));
			return;
		}
	}
}

static struct platform_driver imx_gpc_driver = {
	.driver = {
		.name = "imx-gpc",
		.of_match_table = imx_gpc_dt_ids,
	},
	.probe = imx_gpc_probe,
	.remove_new = imx_gpc_remove,
};
builtin_platform_driver(imx_gpc_driver)
