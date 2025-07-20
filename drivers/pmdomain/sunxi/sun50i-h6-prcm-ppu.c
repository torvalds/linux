// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) Arm Ltd. 2024
 *
 * Allwinner H6/H616 PRCM power domain driver.
 * This covers a few registers inside the PRCM (Power Reset Clock Management)
 * block that control some power rails, most prominently for the Mali GPU.
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/reset.h>

/*
 * The PRCM block covers multiple devices, starting with some clocks,
 * then followed by the power rails.
 * The clocks are covered by a different driver, so this driver's MMIO range
 * starts later in the PRCM MMIO frame, not at the beginning of it.
 * To keep the register offsets consistent with other PRCM documentation,
 * express the registers relative to the beginning of the whole PRCM, and
 * subtract the PPU offset this driver is bound to.
 */
#define PD_H6_PPU_OFFSET		0x250
#define PD_H6_VDD_SYS_REG		0x250
#define PD_H616_ANA_VDD_GATE			BIT(4)
#define PD_H6_CPUS_VDD_GATE			BIT(3)
#define PD_H6_AVCC_VDD_GATE			BIT(2)
#define PD_H6_GPU_REG			0x254
#define PD_H6_GPU_GATE				BIT(0)

struct sun50i_h6_ppu_pd {
	struct generic_pm_domain	genpd;
	void __iomem			*reg;
	u32				gate_mask;
	bool				negated;
};

#define FLAG_PPU_ALWAYS_ON	BIT(0)
#define FLAG_PPU_NEGATED	BIT(1)

struct sun50i_h6_ppu_desc {
	const char *name;
	u32 offset;
	u32 mask;
	unsigned int flags;
};

static const struct sun50i_h6_ppu_desc sun50i_h6_ppus[] = {
	{ "AVCC", PD_H6_VDD_SYS_REG, PD_H6_AVCC_VDD_GATE },
	{ "CPUS", PD_H6_VDD_SYS_REG, PD_H6_CPUS_VDD_GATE },
	{ "GPU", PD_H6_GPU_REG, PD_H6_GPU_GATE },
};
static const struct sun50i_h6_ppu_desc sun50i_h616_ppus[] = {
	{ "PLL", PD_H6_VDD_SYS_REG, PD_H6_AVCC_VDD_GATE,
		FLAG_PPU_ALWAYS_ON | FLAG_PPU_NEGATED },
	{ "ANA", PD_H6_VDD_SYS_REG, PD_H616_ANA_VDD_GATE, FLAG_PPU_ALWAYS_ON },
	{ "GPU", PD_H6_GPU_REG, PD_H6_GPU_GATE, FLAG_PPU_NEGATED },
};

struct sun50i_h6_ppu_data {
	const struct sun50i_h6_ppu_desc *descs;
	int nr_domains;
};

static const struct sun50i_h6_ppu_data sun50i_h6_ppu_data = {
	.descs = sun50i_h6_ppus,
	.nr_domains = ARRAY_SIZE(sun50i_h6_ppus),
};

static const struct sun50i_h6_ppu_data sun50i_h616_ppu_data = {
	.descs = sun50i_h616_ppus,
	.nr_domains = ARRAY_SIZE(sun50i_h616_ppus),
};

#define to_sun50i_h6_ppu_pd(_genpd) \
	container_of(_genpd, struct sun50i_h6_ppu_pd, genpd)

static bool sun50i_h6_ppu_power_status(const struct sun50i_h6_ppu_pd *pd)
{
	bool bit = readl(pd->reg) & pd->gate_mask;

	return bit ^ pd->negated;
}

static int sun50i_h6_ppu_pd_set_power(const struct sun50i_h6_ppu_pd *pd,
				      bool set_bit)
{
	u32 reg = readl(pd->reg);

	if (set_bit)
		writel(reg | pd->gate_mask, pd->reg);
	else
		writel(reg & ~pd->gate_mask, pd->reg);

	return 0;
}

static int sun50i_h6_ppu_pd_power_on(struct generic_pm_domain *genpd)
{
	const struct sun50i_h6_ppu_pd *pd = to_sun50i_h6_ppu_pd(genpd);

	return sun50i_h6_ppu_pd_set_power(pd, !pd->negated);
}

static int sun50i_h6_ppu_pd_power_off(struct generic_pm_domain *genpd)
{
	const struct sun50i_h6_ppu_pd *pd = to_sun50i_h6_ppu_pd(genpd);

	return sun50i_h6_ppu_pd_set_power(pd, pd->negated);
}

static int sun50i_h6_ppu_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct genpd_onecell_data *ppu;
	struct sun50i_h6_ppu_pd *pds;
	const struct sun50i_h6_ppu_data *data;
	void __iomem *base;
	int ret, i;

	data = of_device_get_match_data(dev);
	if (!data)
		return -EINVAL;

	pds = devm_kcalloc(dev, data->nr_domains, sizeof(*pds), GFP_KERNEL);
	if (!pds)
		return -ENOMEM;

	ppu = devm_kzalloc(dev, sizeof(*ppu), GFP_KERNEL);
	if (!ppu)
		return -ENOMEM;

	ppu->num_domains = data->nr_domains;
	ppu->domains = devm_kcalloc(dev, data->nr_domains,
				    sizeof(*ppu->domains), GFP_KERNEL);
	if (!ppu->domains)
		return -ENOMEM;

	platform_set_drvdata(pdev, ppu);

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	for (i = 0; i < data->nr_domains; i++) {
		struct sun50i_h6_ppu_pd *pd = &pds[i];
		const struct sun50i_h6_ppu_desc *desc = &data->descs[i];

		pd->genpd.name		= desc->name;
		pd->genpd.power_off	= sun50i_h6_ppu_pd_power_off;
		pd->genpd.power_on	= sun50i_h6_ppu_pd_power_on;
		if (desc->flags & FLAG_PPU_ALWAYS_ON)
			pd->genpd.flags = GENPD_FLAG_ALWAYS_ON;
		pd->negated		= !!(desc->flags & FLAG_PPU_NEGATED);
		pd->reg			= base + desc->offset - PD_H6_PPU_OFFSET;
		pd->gate_mask		= desc->mask;

		ret = pm_genpd_init(&pd->genpd, NULL,
				    !sun50i_h6_ppu_power_status(pd));
		if (ret) {
			dev_warn(dev, "Failed to add %s power domain: %d\n",
				 desc->name, ret);
			goto out_remove_pds;
		}
		ppu->domains[i] = &pd->genpd;
	}

	ret = of_genpd_add_provider_onecell(dev->of_node, ppu);
	if (!ret)
		return 0;

	dev_warn(dev, "Failed to add provider: %d\n", ret);
out_remove_pds:
	for (i--; i >= 0; i--)
		pm_genpd_remove(&pds[i].genpd);

	return ret;
}

static const struct of_device_id sun50i_h6_ppu_of_match[] = {
	{ .compatible	= "allwinner,sun50i-h6-prcm-ppu",
	  .data		= &sun50i_h6_ppu_data },
	{ .compatible	= "allwinner,sun50i-h616-prcm-ppu",
	  .data		= &sun50i_h616_ppu_data },
	{ }
};
MODULE_DEVICE_TABLE(of, sun50i_h6_ppu_of_match);

static struct platform_driver sun50i_h6_ppu_driver = {
	.probe	= sun50i_h6_ppu_probe,
	.driver	= {
		.name			= "sun50i-h6-prcm-ppu",
		.of_match_table		= sun50i_h6_ppu_of_match,
		/* Power domains cannot be removed while they are in use. */
		.suppress_bind_attrs	= true,
	},
};
module_platform_driver(sun50i_h6_ppu_driver);

MODULE_AUTHOR("Andre Przywara <andre.przywara@arm.com>");
MODULE_DESCRIPTION("Allwinner H6 PRCM power domain driver");
MODULE_LICENSE("GPL");
