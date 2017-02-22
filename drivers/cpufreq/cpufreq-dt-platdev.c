/*
 * Copyright (C) 2016 Linaro.
 * Viresh Kumar <viresh.kumar@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/err.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include "cpufreq-dt.h"

static const struct of_device_id machines[] __initconst = {
	{ .compatible = "allwinner,sun4i-a10", },
	{ .compatible = "allwinner,sun5i-a10s", },
	{ .compatible = "allwinner,sun5i-a13", },
	{ .compatible = "allwinner,sun5i-r8", },
	{ .compatible = "allwinner,sun6i-a31", },
	{ .compatible = "allwinner,sun6i-a31s", },
	{ .compatible = "allwinner,sun7i-a20", },
	{ .compatible = "allwinner,sun8i-a23", },
	{ .compatible = "allwinner,sun8i-a33", },
	{ .compatible = "allwinner,sun8i-a83t", },
	{ .compatible = "allwinner,sun8i-h3", },

	{ .compatible = "apm,xgene-shadowcat", },

	{ .compatible = "arm,integrator-ap", },
	{ .compatible = "arm,integrator-cp", },

	{ .compatible = "hisilicon,hi6220", },

	{ .compatible = "fsl,imx27", },
	{ .compatible = "fsl,imx51", },
	{ .compatible = "fsl,imx53", },
	{ .compatible = "fsl,imx7d", },

	{ .compatible = "marvell,berlin", },
	{ .compatible = "marvell,pxa250", },
	{ .compatible = "marvell,pxa270", },

	{ .compatible = "samsung,exynos3250", },
	{ .compatible = "samsung,exynos4210", },
	{ .compatible = "samsung,exynos4212", },
	{ .compatible = "samsung,exynos4412", },
	{ .compatible = "samsung,exynos5250", },
#ifndef CONFIG_BL_SWITCHER
	{ .compatible = "samsung,exynos5420", },
	{ .compatible = "samsung,exynos5433", },
	{ .compatible = "samsung,exynos5800", },
#endif

	{ .compatible = "renesas,emev2", },
	{ .compatible = "renesas,r7s72100", },
	{ .compatible = "renesas,r8a73a4", },
	{ .compatible = "renesas,r8a7740", },
	{ .compatible = "renesas,r8a7743", },
	{ .compatible = "renesas,r8a7745", },
	{ .compatible = "renesas,r8a7778", },
	{ .compatible = "renesas,r8a7779", },
	{ .compatible = "renesas,r8a7790", },
	{ .compatible = "renesas,r8a7791", },
	{ .compatible = "renesas,r8a7792", },
	{ .compatible = "renesas,r8a7793", },
	{ .compatible = "renesas,r8a7794", },
	{ .compatible = "renesas,sh73a0", },

	{ .compatible = "rockchip,rk2928", },
	{ .compatible = "rockchip,rk3036", },
	{ .compatible = "rockchip,rk3066a", },
	{ .compatible = "rockchip,rk3066b", },
	{ .compatible = "rockchip,rk3188", },
	{ .compatible = "rockchip,rk3228", },
	{ .compatible = "rockchip,rk3288", },
	{ .compatible = "rockchip,rk3366", },
	{ .compatible = "rockchip,rk3368", },
	{ .compatible = "rockchip,rk3399", },

	{ .compatible = "sigma,tango4" },

	{ .compatible = "socionext,uniphier-pro5", },
	{ .compatible = "socionext,uniphier-pxs2", },
	{ .compatible = "socionext,uniphier-ld6b", },
	{ .compatible = "socionext,uniphier-ld11", },
	{ .compatible = "socionext,uniphier-ld20", },

	{ .compatible = "ti,am33xx", },
	{ .compatible = "ti,dra7", },
	{ .compatible = "ti,omap2", },
	{ .compatible = "ti,omap3", },
	{ .compatible = "ti,omap4", },
	{ .compatible = "ti,omap5", },

	{ .compatible = "xlnx,zynq-7000", },

	{ .compatible = "zte,zx296718", },

	{ }
};

static int __init cpufreq_dt_platdev_init(void)
{
	struct device_node *np = of_find_node_by_path("/");
	const struct of_device_id *match;

	if (!np)
		return -ENODEV;

	match = of_match_node(machines, np);
	of_node_put(np);
	if (!match)
		return -ENODEV;

	return PTR_ERR_OR_ZERO(platform_device_register_data(NULL, "cpufreq-dt",
			       -1, match->data,
			       sizeof(struct cpufreq_dt_platform_data)));
}
device_initcall(cpufreq_dt_platdev_init);
