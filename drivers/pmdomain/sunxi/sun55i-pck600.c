// SPDX-License-Identifier: GPL-2.0-only
/*
 * Allwinner PCK-600 power domain support
 *
 * Copyright (c) 2025 Chen-Yu Tsai <wens@csie.org>
 *
 * The hardware is likely based on the Arm PCK-600 IP, since some of
 * the registers match Arm's documents, with additional delay controls
 * that are in registers listed as reserved.
 *
 * Documents include:
 * - "Arm CoreLink PCK-600 Power Control Kit" TRM
 * - "Arm Power Policy Unit" architecture specification (DEN0051E)
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/container_of.h>
#include <linux/device.h>
#include <linux/dev_printk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <linux/string_choices.h>

#define PPU_PWPR    0x0
#define PPU_PWSR    0x8
#define PPU_DCDR0   0x170
#define PPU_DCDR1   0x174

/* shared definition for PPU_PWPR and PPU_PWSR */
#define PPU_PWR_STATUS	GENMASK(3, 0)
#define PPU_POWER_MODE_ON	0x8
#define PPU_POWER_MODE_OFF	0x0

#define PPU_REG_SIZE	0x1000

struct sunxi_pck600_desc {
	const char * const *pd_names;
	unsigned int num_domains;
	u32 logic_power_switch0_delay_offset;
	u32 logic_power_switch1_delay_offset;
	u32 off2on_delay_offset;
	u32 device_ctrl0_delay;
	u32 device_ctrl1_delay;
	u32 logic_power_switch0_delay;
	u32 logic_power_switch1_delay;
	u32 off2on_delay;
};

struct sunxi_pck600_pd {
	struct generic_pm_domain genpd;
	struct sunxi_pck600 *pck;
	void __iomem *base;
};

struct sunxi_pck600 {
	struct device *dev;
	struct genpd_onecell_data genpd_data;
	struct sunxi_pck600_pd pds[];
};

#define to_sunxi_pd(gpd) container_of(gpd, struct sunxi_pck600_pd, genpd)

static int sunxi_pck600_pd_set_power(struct sunxi_pck600_pd *pd, bool on)
{
	struct sunxi_pck600 *pck = pd->pck;
	struct generic_pm_domain *genpd = &pd->genpd;
	int ret;
	u32 val, reg;

	val = on ? PPU_POWER_MODE_ON : PPU_POWER_MODE_OFF;

	reg = readl(pd->base + PPU_PWPR);
	FIELD_MODIFY(PPU_PWR_STATUS, &reg, val);
	writel(reg, pd->base + PPU_PWPR);

	/* push write out to hardware */
	reg = readl(pd->base + PPU_PWPR);

	ret = readl_poll_timeout_atomic(pd->base + PPU_PWSR, reg,
					FIELD_GET(PPU_PWR_STATUS, reg) == val,
					0, 10000);
	if (ret)
		dev_err(pck->dev, "failed to turn domain \"%s\" %s: %d\n",
			genpd->name, str_on_off(on), ret);

	return ret;
}

static int sunxi_pck600_power_on(struct generic_pm_domain *domain)
{
	struct sunxi_pck600_pd *pd = to_sunxi_pd(domain);

	return sunxi_pck600_pd_set_power(pd, true);
}

static int sunxi_pck600_power_off(struct generic_pm_domain *domain)
{
	struct sunxi_pck600_pd *pd = to_sunxi_pd(domain);

	return sunxi_pck600_pd_set_power(pd, false);
}

static void sunxi_pck600_pd_setup(struct sunxi_pck600_pd *pd,
				  const struct sunxi_pck600_desc *desc)
{
	writel(desc->device_ctrl0_delay, pd->base + PPU_DCDR0);
	writel(desc->device_ctrl1_delay, pd->base + PPU_DCDR1);
	writel(desc->logic_power_switch0_delay,
	       pd->base + desc->logic_power_switch0_delay_offset);
	writel(desc->logic_power_switch1_delay,
	       pd->base + desc->logic_power_switch1_delay_offset);
	writel(desc->off2on_delay, pd->base + desc->off2on_delay_offset);
}

static int sunxi_pck600_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct sunxi_pck600_desc *desc;
	struct genpd_onecell_data *genpds;
	struct sunxi_pck600 *pck;
	struct reset_control *rst;
	struct clk *clk;
	void __iomem *base;
	int i, ret;

	desc = of_device_get_match_data(dev);

	pck = devm_kzalloc(dev, struct_size(pck, pds, desc->num_domains), GFP_KERNEL);
	if (!pck)
		return -ENOMEM;

	pck->dev = &pdev->dev;
	platform_set_drvdata(pdev, pck);

	genpds = &pck->genpd_data;
	genpds->num_domains = desc->num_domains;
	genpds->domains = devm_kcalloc(dev, desc->num_domains,
				       sizeof(*genpds->domains), GFP_KERNEL);
	if (!genpds->domains)
		return -ENOMEM;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	rst = devm_reset_control_get_exclusive_released(dev, NULL);
	if (IS_ERR(rst))
		return dev_err_probe(dev, PTR_ERR(rst), "failed to get reset control\n");

	clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(clk))
		return dev_err_probe(dev, PTR_ERR(clk), "failed to get clock\n");

	for (i = 0; i < desc->num_domains; i++) {
		struct sunxi_pck600_pd *pd = &pck->pds[i];

		pd->genpd.name = desc->pd_names[i];
		pd->genpd.power_off = sunxi_pck600_power_off;
		pd->genpd.power_on = sunxi_pck600_power_on;
		pd->base = base + PPU_REG_SIZE * i;

		sunxi_pck600_pd_setup(pd, desc);
		ret = pm_genpd_init(&pd->genpd, NULL, false);
		if (ret) {
			dev_err_probe(dev, ret, "failed to initialize power domain\n");
			goto err_remove_pds;
		}

		genpds->domains[i] = &pd->genpd;
	}

	ret = of_genpd_add_provider_onecell(dev_of_node(dev), genpds);
	if (ret) {
		dev_err_probe(dev, ret, "failed to add PD provider\n");
		goto err_remove_pds;
	}

	return 0;

err_remove_pds:
	for (i--; i >= 0; i--)
		pm_genpd_remove(genpds->domains[i]);

	return ret;
}

static const char * const sun55i_a523_pck600_pd_names[] = {
	"VE", "GPU", "VI", "VO0", "VO1", "DE", "NAND", "PCIE"
};

static const struct sunxi_pck600_desc sun55i_a523_pck600_desc = {
	.pd_names = sun55i_a523_pck600_pd_names,
	.num_domains = ARRAY_SIZE(sun55i_a523_pck600_pd_names),
	.logic_power_switch0_delay_offset = 0xc00,
	.logic_power_switch1_delay_offset = 0xc04,
	.off2on_delay_offset = 0xc10,
	.device_ctrl0_delay = 0xffffff,
	.device_ctrl1_delay = 0xffff,
	.logic_power_switch0_delay = 0x8080808,
	.logic_power_switch1_delay = 0x808,
	.off2on_delay = 0x8
};

static const struct of_device_id sunxi_pck600_of_match[] = {
	{
		.compatible	= "allwinner,sun55i-a523-pck-600",
		.data		= &sun55i_a523_pck600_desc,
	},
	{}
};
MODULE_DEVICE_TABLE(of, sunxi_pck600_of_match);

static struct platform_driver sunxi_pck600_driver = {
	.probe = sunxi_pck600_probe,
	.driver = {
		.name   = "sunxi-pck-600",
		.of_match_table = sunxi_pck600_of_match,
		/* Power domains cannot be removed if in use. */
		.suppress_bind_attrs = true,
	},
};
module_platform_driver(sunxi_pck600_driver);

MODULE_DESCRIPTION("Allwinner PCK-600 power domain driver");
MODULE_AUTHOR("Chen-Yu Tsai <wens@csie.org>");
MODULE_LICENSE("GPL");
