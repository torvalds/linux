// SPDX-License-Identifier: GPL-2.0-only
/*
 * dwc3-generic-plat.c - DesignWare USB3 generic platform driver
 *
 * Copyright (C) 2025 Ze Huang <huang.ze@linux.dev>
 *
 * Inspired by dwc3-qcom.c and dwc3-of-simple.c
 */

#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include "glue.h"

#define EIC7700_HSP_BUS_FILTER_EN	BIT(0)
#define EIC7700_HSP_BUS_CLKEN_GM	BIT(9)
#define EIC7700_HSP_BUS_CLKEN_GS	BIT(16)
#define EIC7700_HSP_AXI_LP_XM_CSYSREQ	BIT(0)
#define EIC7700_HSP_AXI_LP_XS_CSYSREQ	BIT(16)

struct dwc3_generic {
	struct device		*dev;
	struct dwc3		dwc;
	struct clk_bulk_data	*clks;
	int			num_clocks;
	struct reset_control	*resets;
};

struct dwc3_generic_config {
	int (*init)(struct dwc3_generic *dwc3g);
	struct dwc3_properties properties;
};

#define to_dwc3_generic(d) container_of((d), struct dwc3_generic, dwc)

static void dwc3_generic_reset_control_assert(void *data)
{
	reset_control_assert(data);
}

static int dwc3_eic7700_init(struct dwc3_generic *dwc3g)
{
	struct device *dev = dwc3g->dev;
	struct regmap *regmap;
	u32 hsp_usb_axi_lp;
	u32 hsp_usb_bus;
	u32 args[2];
	u32 val;

	regmap = syscon_regmap_lookup_by_phandle_args(dev->of_node,
						      "eswin,hsp-sp-csr",
						      ARRAY_SIZE(args), args);
	if (IS_ERR(regmap)) {
		dev_err(dev, "No hsp-sp-csr phandle specified\n");
		return PTR_ERR(regmap);
	}

	hsp_usb_bus       = args[0];
	hsp_usb_axi_lp    = args[1];

	regmap_read(regmap, hsp_usb_bus, &val);
	regmap_write(regmap, hsp_usb_bus, val | EIC7700_HSP_BUS_FILTER_EN |
		     EIC7700_HSP_BUS_CLKEN_GM | EIC7700_HSP_BUS_CLKEN_GS);

	regmap_write(regmap, hsp_usb_axi_lp, EIC7700_HSP_AXI_LP_XM_CSYSREQ |
		     EIC7700_HSP_AXI_LP_XS_CSYSREQ);
	return 0;
}

static int dwc3_generic_probe(struct platform_device *pdev)
{
	const struct dwc3_generic_config *plat_config;
	struct dwc3_probe_data probe_data = {};
	struct device *dev = &pdev->dev;
	struct dwc3_generic *dwc3g;
	struct resource *res;
	int ret;

	dwc3g = devm_kzalloc(dev, sizeof(*dwc3g), GFP_KERNEL);
	if (!dwc3g)
		return -ENOMEM;

	dwc3g->dev = dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "missing memory resource\n");
		return -ENODEV;
	}

	dwc3g->resets = devm_reset_control_array_get_optional_exclusive(dev);
	if (IS_ERR(dwc3g->resets))
		return dev_err_probe(dev, PTR_ERR(dwc3g->resets), "failed to get resets\n");

	ret = reset_control_assert(dwc3g->resets);
	if (ret)
		return dev_err_probe(dev, ret, "failed to assert resets\n");

	/* Not strict timing, just for safety */
	udelay(2);

	ret = reset_control_deassert(dwc3g->resets);
	if (ret)
		return dev_err_probe(dev, ret, "failed to deassert resets\n");

	ret = devm_add_action_or_reset(dev, dwc3_generic_reset_control_assert, dwc3g->resets);
	if (ret)
		return ret;

	ret = devm_clk_bulk_get_all_enabled(dwc3g->dev, &dwc3g->clks);
	if (ret < 0)
		return dev_err_probe(dev, ret, "failed to get clocks\n");

	dwc3g->num_clocks = ret;
	dwc3g->dwc.dev = dev;
	probe_data.dwc = &dwc3g->dwc;
	probe_data.res = res;
	probe_data.ignore_clocks_and_resets = true;

	plat_config = of_device_get_match_data(dev);
	if (!plat_config) {
		probe_data.properties = DWC3_DEFAULT_PROPERTIES;
		goto core_probe;
	}

	probe_data.properties = plat_config->properties;
	if (plat_config->init) {
		ret = plat_config->init(dwc3g);
		if (ret)
			return dev_err_probe(dev, ret,
					     "failed to init platform\n");
	}

core_probe:
	ret = dwc3_core_probe(&probe_data);
	if (ret)
		return dev_err_probe(dev, ret, "failed to register DWC3 Core\n");

	return 0;
}

static void dwc3_generic_remove(struct platform_device *pdev)
{
	struct dwc3 *dwc = platform_get_drvdata(pdev);

	dwc3_core_remove(dwc);
}

static int dwc3_generic_suspend(struct device *dev)
{
	struct dwc3 *dwc = dev_get_drvdata(dev);
	struct dwc3_generic *dwc3g = to_dwc3_generic(dwc);
	int ret;

	ret = dwc3_pm_suspend(dwc);
	if (ret)
		return ret;

	clk_bulk_disable_unprepare(dwc3g->num_clocks, dwc3g->clks);

	return 0;
}

static int dwc3_generic_resume(struct device *dev)
{
	struct dwc3 *dwc = dev_get_drvdata(dev);
	struct dwc3_generic *dwc3g = to_dwc3_generic(dwc);
	int ret;

	ret = clk_bulk_prepare_enable(dwc3g->num_clocks, dwc3g->clks);
	if (ret)
		return ret;

	ret = dwc3_pm_resume(dwc);
	if (ret)
		return ret;

	return 0;
}

static int dwc3_generic_runtime_suspend(struct device *dev)
{
	return dwc3_runtime_suspend(dev_get_drvdata(dev));
}

static int dwc3_generic_runtime_resume(struct device *dev)
{
	return dwc3_runtime_resume(dev_get_drvdata(dev));
}

static int dwc3_generic_runtime_idle(struct device *dev)
{
	return dwc3_runtime_idle(dev_get_drvdata(dev));
}

static const struct dev_pm_ops dwc3_generic_dev_pm_ops = {
	SYSTEM_SLEEP_PM_OPS(dwc3_generic_suspend, dwc3_generic_resume)
	RUNTIME_PM_OPS(dwc3_generic_runtime_suspend, dwc3_generic_runtime_resume,
		       dwc3_generic_runtime_idle)
};

static const struct dwc3_generic_config fsl_ls1028_dwc3 = {
	.properties.gsbuscfg0_reqinfo = 0x2222,
};

static const struct dwc3_generic_config eic7700_dwc3 =  {
	.init = dwc3_eic7700_init,
	.properties = DWC3_DEFAULT_PROPERTIES,
};

static const struct of_device_id dwc3_generic_of_match[] = {
	{ .compatible = "spacemit,k1-dwc3", },
	{ .compatible = "fsl,ls1028a-dwc3", &fsl_ls1028_dwc3},
	{ .compatible = "eswin,eic7700-dwc3", &eic7700_dwc3},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, dwc3_generic_of_match);

static struct platform_driver dwc3_generic_driver = {
	.probe		= dwc3_generic_probe,
	.remove		= dwc3_generic_remove,
	.driver		= {
		.name	= "dwc3-generic-plat",
		.of_match_table = dwc3_generic_of_match,
		.pm	= pm_ptr(&dwc3_generic_dev_pm_ops),
	},
};
module_platform_driver(dwc3_generic_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("DesignWare USB3 generic platform driver");
