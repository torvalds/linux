// SPDX-License-Identifier: GPL-2.0-only

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/reset.h>

#define PD_STATE_ON			1
#define PD_STATE_OFF			2

#define PD_RSTN_REG			0x00
#define PD_CLK_GATE_REG			0x04
#define PD_PWROFF_GATE_REG		0x08
#define PD_PSW_ON_REG			0x0c
#define PD_PSW_OFF_REG			0x10
#define PD_PSW_DELAY_REG		0x14
#define PD_OFF_DELAY_REG		0x18
#define PD_ON_DELAY_REG			0x1c
#define PD_COMMAND_REG			0x20
#define PD_STATUS_REG			0x24
#define PD_STATUS_COMPLETE			BIT(1)
#define PD_STATUS_BUSY				BIT(3)
#define PD_STATUS_STATE				GENMASK(17, 16)
#define PD_ACTIVE_CTRL_REG		0x2c
#define PD_GATE_STATUS_REG		0x30
#define PD_RSTN_STATUS				BIT(0)
#define PD_CLK_GATE_STATUS			BIT(1)
#define PD_PWROFF_GATE_STATUS			BIT(2)
#define PD_PSW_STATUS_REG		0x34

#define PD_REGS_SIZE			0x80

struct sun20i_ppu_desc {
	const char *const		*names;
	unsigned int			num_domains;
};

struct sun20i_ppu_pd {
	struct generic_pm_domain	genpd;
	void __iomem			*base;
};

#define to_sun20i_ppu_pd(_genpd) \
	container_of(_genpd, struct sun20i_ppu_pd, genpd)

static bool sun20i_ppu_pd_is_on(const struct sun20i_ppu_pd *pd)
{
	u32 status = readl(pd->base + PD_STATUS_REG);

	return FIELD_GET(PD_STATUS_STATE, status) == PD_STATE_ON;
}

static int sun20i_ppu_pd_set_power(const struct sun20i_ppu_pd *pd, bool power_on)
{
	u32 state, status;
	int ret;

	if (sun20i_ppu_pd_is_on(pd) == power_on)
		return 0;

	/* Wait for the power controller to be idle. */
	ret = readl_poll_timeout(pd->base + PD_STATUS_REG, status,
				 !(status & PD_STATUS_BUSY), 100, 1000);
	if (ret)
		return ret;

	state = power_on ? PD_STATE_ON : PD_STATE_OFF;
	writel(state, pd->base + PD_COMMAND_REG);

	/* Wait for the state transition to complete. */
	ret = readl_poll_timeout(pd->base + PD_STATUS_REG, status,
				 FIELD_GET(PD_STATUS_STATE, status) == state &&
				 (status & PD_STATUS_COMPLETE), 100, 1000);
	if (ret)
		return ret;

	/* Clear the completion flag. */
	writel(status, pd->base + PD_STATUS_REG);

	return 0;
}

static int sun20i_ppu_pd_power_on(struct generic_pm_domain *genpd)
{
	const struct sun20i_ppu_pd *pd = to_sun20i_ppu_pd(genpd);

	return sun20i_ppu_pd_set_power(pd, true);
}

static int sun20i_ppu_pd_power_off(struct generic_pm_domain *genpd)
{
	const struct sun20i_ppu_pd *pd = to_sun20i_ppu_pd(genpd);

	return sun20i_ppu_pd_set_power(pd, false);
}

static int sun20i_ppu_probe(struct platform_device *pdev)
{
	const struct sun20i_ppu_desc *desc;
	struct device *dev = &pdev->dev;
	struct genpd_onecell_data *ppu;
	struct sun20i_ppu_pd *pds;
	struct reset_control *rst;
	void __iomem *base;
	struct clk *clk;
	int ret;

	desc = of_device_get_match_data(dev);
	if (!desc)
		return -EINVAL;

	pds = devm_kcalloc(dev, desc->num_domains, sizeof(*pds), GFP_KERNEL);
	if (!pds)
		return -ENOMEM;

	ppu = devm_kzalloc(dev, sizeof(*ppu), GFP_KERNEL);
	if (!ppu)
		return -ENOMEM;

	ppu->domains = devm_kcalloc(dev, desc->num_domains,
				    sizeof(*ppu->domains), GFP_KERNEL);
	if (!ppu->domains)
		return -ENOMEM;

	ppu->num_domains = desc->num_domains;
	platform_set_drvdata(pdev, ppu);

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	rst = devm_reset_control_get_exclusive(dev, NULL);
	if (IS_ERR(rst))
		return PTR_ERR(rst);

	ret = reset_control_deassert(rst);
	if (ret)
		return ret;

	for (unsigned int i = 0; i < ppu->num_domains; ++i) {
		struct sun20i_ppu_pd *pd = &pds[i];

		pd->genpd.name		= desc->names[i];
		pd->genpd.power_off	= sun20i_ppu_pd_power_off;
		pd->genpd.power_on	= sun20i_ppu_pd_power_on;
		pd->base		= base + PD_REGS_SIZE * i;

		ret = pm_genpd_init(&pd->genpd, NULL, sun20i_ppu_pd_is_on(pd));
		if (ret) {
			dev_warn(dev, "Failed to add '%s' domain: %d\n",
				 pd->genpd.name, ret);
			continue;
		}

		ppu->domains[i] = &pd->genpd;
	}

	ret = of_genpd_add_provider_onecell(dev->of_node, ppu);
	if (ret)
		dev_warn(dev, "Failed to add provider: %d\n", ret);

	return 0;
}

static const char *const sun20i_d1_ppu_pd_names[] = {
	"CPU",
	"VE",
	"DSP",
};

static const struct sun20i_ppu_desc sun20i_d1_ppu_desc = {
	.names		= sun20i_d1_ppu_pd_names,
	.num_domains	= ARRAY_SIZE(sun20i_d1_ppu_pd_names),
};

static const char *const sun8i_v853_ppu_pd_names[] = {
	"RISCV",
	"NPU",
	"VE",
};

static const struct sun20i_ppu_desc sun8i_v853_ppu_desc = {
	.names		= sun8i_v853_ppu_pd_names,
	.num_domains	= ARRAY_SIZE(sun8i_v853_ppu_pd_names),
};

static const char *const sun55i_a523_ppu_pd_names[] = {
	"DSP",
	"NPU",
	"AUDIO",
	"SRAM",
	"RISCV",
};

static const struct sun20i_ppu_desc sun55i_a523_ppu_desc = {
	.names		= sun55i_a523_ppu_pd_names,
	.num_domains	= ARRAY_SIZE(sun55i_a523_ppu_pd_names),
};

static const struct of_device_id sun20i_ppu_of_match[] = {
	{
		.compatible	= "allwinner,sun20i-d1-ppu",
		.data		= &sun20i_d1_ppu_desc,
	},
	{
		.compatible	= "allwinner,sun8i-v853-ppu",
		.data		= &sun8i_v853_ppu_desc,
	},
	{
		.compatible	= "allwinner,sun55i-a523-ppu",
		.data		= &sun55i_a523_ppu_desc,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, sun20i_ppu_of_match);

static struct platform_driver sun20i_ppu_driver = {
	.probe	= sun20i_ppu_probe,
	.driver	= {
		.name			= "sun20i-ppu",
		.of_match_table		= sun20i_ppu_of_match,
		/* Power domains cannot be removed while they are in use. */
		.suppress_bind_attrs	= true,
	},
};
module_platform_driver(sun20i_ppu_driver);

MODULE_AUTHOR("Samuel Holland <samuel@sholland.org>");
MODULE_DESCRIPTION("Allwinner D1 PPU power domain driver");
MODULE_LICENSE("GPL");
