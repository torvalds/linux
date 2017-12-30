/*
 * Marvell Armada AP806 System Controller
 *
 * Copyright (C) 2016 Marvell
 *
 * Thomas Petazzoni <thomas.petazzoni@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#define pr_fmt(fmt) "ap806-system-controller: " fmt

#include <linux/clk-provider.h>
#include <linux/mfd/syscon.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#define AP806_SAR_REG			0x400
#define AP806_SAR_CLKFREQ_MODE_MASK	0x1f

#define AP806_CLK_NUM			5

static struct clk *ap806_clks[AP806_CLK_NUM];

static struct clk_onecell_data ap806_clk_data = {
	.clks = ap806_clks,
	.clk_num = AP806_CLK_NUM,
};

static char *ap806_unique_name(struct device *dev, struct device_node *np,
			       char *name)
{
	const __be32 *reg;
	u64 addr;

	reg = of_get_property(np, "reg", NULL);
	addr = of_translate_address(np, reg);
	return devm_kasprintf(dev, GFP_KERNEL, "%llx-%s",
			(unsigned long long)addr, name);
}

static int ap806_syscon_common_probe(struct platform_device *pdev,
				     struct device_node *syscon_node)
{
	unsigned int freq_mode, cpuclk_freq;
	const char *name, *fixedclk_name;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct regmap *regmap;
	u32 reg;
	int ret;

	regmap = syscon_node_to_regmap(syscon_node);
	if (IS_ERR(regmap)) {
		dev_err(dev, "cannot get regmap\n");
		return PTR_ERR(regmap);
	}

	ret = regmap_read(regmap, AP806_SAR_REG, &reg);
	if (ret) {
		dev_err(dev, "cannot read from regmap\n");
		return ret;
	}

	freq_mode = reg & AP806_SAR_CLKFREQ_MODE_MASK;
	switch (freq_mode) {
	case 0x0:
	case 0x1:
		cpuclk_freq = 2000;
		break;
	case 0x6:
	case 0x7:
		cpuclk_freq = 1800;
		break;
	case 0x4:
	case 0xB:
	case 0xD:
		cpuclk_freq = 1600;
		break;
	case 0x1a:
		cpuclk_freq = 1400;
		break;
	case 0x14:
	case 0x17:
		cpuclk_freq = 1300;
		break;
	case 0x19:
		cpuclk_freq = 1200;
		break;
	case 0x13:
	case 0x1d:
		cpuclk_freq = 1000;
		break;
	case 0x1c:
		cpuclk_freq = 800;
		break;
	case 0x1b:
		cpuclk_freq = 600;
		break;
	default:
		dev_err(dev, "invalid SAR value\n");
		return -EINVAL;
	}

	/* Convert to hertz */
	cpuclk_freq *= 1000 * 1000;

	/* CPU clocks depend on the Sample At Reset configuration */
	name = ap806_unique_name(dev, syscon_node, "cpu-cluster-0");
	ap806_clks[0] = clk_register_fixed_rate(dev, name, NULL,
						0, cpuclk_freq);
	if (IS_ERR(ap806_clks[0])) {
		ret = PTR_ERR(ap806_clks[0]);
		goto fail0;
	}

	name = ap806_unique_name(dev, syscon_node, "cpu-cluster-1");
	ap806_clks[1] = clk_register_fixed_rate(dev, name, NULL, 0,
						cpuclk_freq);
	if (IS_ERR(ap806_clks[1])) {
		ret = PTR_ERR(ap806_clks[1]);
		goto fail1;
	}

	/* Fixed clock is always 1200 Mhz */
	fixedclk_name = ap806_unique_name(dev, syscon_node, "fixed");
	ap806_clks[2] = clk_register_fixed_rate(dev, fixedclk_name, NULL,
						0, 1200 * 1000 * 1000);
	if (IS_ERR(ap806_clks[2])) {
		ret = PTR_ERR(ap806_clks[2]);
		goto fail2;
	}

	/* MSS Clock is fixed clock divided by 6 */
	name = ap806_unique_name(dev, syscon_node, "mss");
	ap806_clks[3] = clk_register_fixed_factor(NULL, name, fixedclk_name,
						  0, 1, 6);
	if (IS_ERR(ap806_clks[3])) {
		ret = PTR_ERR(ap806_clks[3]);
		goto fail3;
	}

	/* SDIO(/eMMC) Clock is fixed clock divided by 3 */
	name = ap806_unique_name(dev, syscon_node, "sdio");
	ap806_clks[4] = clk_register_fixed_factor(NULL, name,
						  fixedclk_name,
						  0, 1, 3);
	if (IS_ERR(ap806_clks[4])) {
		ret = PTR_ERR(ap806_clks[4]);
		goto fail4;
	}

	of_clk_add_provider(np, of_clk_src_onecell_get, &ap806_clk_data);
	ret = of_clk_add_provider(np, of_clk_src_onecell_get, &ap806_clk_data);
	if (ret)
		goto fail_clk_add;

	return 0;

fail_clk_add:
	clk_unregister_fixed_factor(ap806_clks[4]);
fail4:
	clk_unregister_fixed_factor(ap806_clks[3]);
fail3:
	clk_unregister_fixed_rate(ap806_clks[2]);
fail2:
	clk_unregister_fixed_rate(ap806_clks[1]);
fail1:
	clk_unregister_fixed_rate(ap806_clks[0]);
fail0:
	return ret;
}

static int ap806_syscon_legacy_probe(struct platform_device *pdev)
{
	dev_warn(&pdev->dev, FW_WARN "Using legacy device tree binding\n");
	dev_warn(&pdev->dev, FW_WARN "Update your device tree:\n");
	dev_warn(&pdev->dev, FW_WARN
		 "This binding won't be supported in future kernel\n");

	return ap806_syscon_common_probe(pdev, pdev->dev.of_node);

}

static int ap806_clock_probe(struct platform_device *pdev)
{
	return ap806_syscon_common_probe(pdev, pdev->dev.of_node->parent);
}

static const struct of_device_id ap806_syscon_legacy_of_match[] = {
	{ .compatible = "marvell,ap806-system-controller", },
	{ }
};

static struct platform_driver ap806_syscon_legacy_driver = {
	.probe = ap806_syscon_legacy_probe,
	.driver		= {
		.name	= "marvell-ap806-system-controller",
		.of_match_table = ap806_syscon_legacy_of_match,
		.suppress_bind_attrs = true,
	},
};
builtin_platform_driver(ap806_syscon_legacy_driver);

static const struct of_device_id ap806_clock_of_match[] = {
	{ .compatible = "marvell,ap806-clock", },
	{ }
};

static struct platform_driver ap806_clock_driver = {
	.probe = ap806_clock_probe,
	.driver		= {
		.name	= "marvell-ap806-clock",
		.of_match_table = ap806_clock_of_match,
		.suppress_bind_attrs = true,
	},
};
builtin_platform_driver(ap806_clock_driver);
