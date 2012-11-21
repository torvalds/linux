/*
 * Exynos Generic power domain support.
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Implementation of Exynos specific power domain control which is used in
 * conjunction with runtime-pm. Support for both device-tree and non-device-tree
 * based power domain support is included.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/io.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/pm_domain.h>
#include <linux/delay.h>
#include <linux/of_address.h>

#include <mach/regs-pmu.h>
#include <plat/devs.h>

/*
 * Exynos specific wrapper around the generic power domain
 */
struct exynos_pm_domain {
	void __iomem *base;
	char const *name;
	bool is_off;
	struct generic_pm_domain pd;
};

static int exynos_pd_power(struct generic_pm_domain *domain, bool power_on)
{
	struct exynos_pm_domain *pd;
	void __iomem *base;
	u32 timeout, pwr;
	char *op;

	pd = container_of(domain, struct exynos_pm_domain, pd);
	base = pd->base;

	pwr = power_on ? S5P_INT_LOCAL_PWR_EN : 0;
	__raw_writel(pwr, base);

	/* Wait max 1ms */
	timeout = 10;

	while ((__raw_readl(base + 0x4) & S5P_INT_LOCAL_PWR_EN)	!= pwr) {
		if (!timeout) {
			op = (power_on) ? "enable" : "disable";
			pr_err("Power domain %s %s failed\n", domain->name, op);
			return -ETIMEDOUT;
		}
		timeout--;
		cpu_relax();
		usleep_range(80, 100);
	}
	return 0;
}

static int exynos_pd_power_on(struct generic_pm_domain *domain)
{
	return exynos_pd_power(domain, true);
}

static int exynos_pd_power_off(struct generic_pm_domain *domain)
{
	return exynos_pd_power(domain, false);
}

#define EXYNOS_GPD(PD, BASE, NAME)			\
static struct exynos_pm_domain PD = {			\
	.base = (void __iomem *)BASE,			\
	.name = NAME,					\
	.pd = {						\
		.power_off = exynos_pd_power_off,	\
		.power_on = exynos_pd_power_on,	\
	},						\
}

#ifdef CONFIG_OF
static __init int exynos_pm_dt_parse_domains(void)
{
	struct device_node *np;

	for_each_compatible_node(np, NULL, "samsung,exynos4210-pd") {
		struct exynos_pm_domain *pd;
		int on;

		pd = kzalloc(sizeof(*pd), GFP_KERNEL);
		if (!pd) {
			pr_err("%s: failed to allocate memory for domain\n",
					__func__);
			return -ENOMEM;
		}

		pd->pd.name = kstrdup(np->name, GFP_KERNEL);
		pd->name = pd->pd.name;
		pd->base = of_iomap(np, 0);
		pd->pd.power_off = exynos_pd_power_off;
		pd->pd.power_on = exynos_pd_power_on;
		pd->pd.of_node = np;

		on = __raw_readl(pd->base + 0x4) & S5P_INT_LOCAL_PWR_EN;

		pm_genpd_init(&pd->pd, NULL, !on);
	}
	return 0;
}
#else
static __init int exynos_pm_dt_parse_domains(void)
{
	return 0;
}
#endif /* CONFIG_OF */

static __init __maybe_unused void exynos_pm_add_dev_to_genpd(struct platform_device *pdev,
						struct exynos_pm_domain *pd)
{
	if (pdev->dev.bus) {
		if (!pm_genpd_add_device(&pd->pd, &pdev->dev))
			pm_genpd_dev_need_restore(&pdev->dev, true);
		else
			pr_info("%s: error in adding %s device to %s power"
				"domain\n", __func__, dev_name(&pdev->dev),
				pd->name);
	}
}

EXYNOS_GPD(exynos4_pd_mfc, S5P_PMU_MFC_CONF, "pd-mfc");
EXYNOS_GPD(exynos4_pd_g3d, S5P_PMU_G3D_CONF, "pd-g3d");
EXYNOS_GPD(exynos4_pd_lcd0, S5P_PMU_LCD0_CONF, "pd-lcd0");
EXYNOS_GPD(exynos4_pd_lcd1, S5P_PMU_LCD1_CONF, "pd-lcd1");
EXYNOS_GPD(exynos4_pd_tv, S5P_PMU_TV_CONF, "pd-tv");
EXYNOS_GPD(exynos4_pd_cam, S5P_PMU_CAM_CONF, "pd-cam");
EXYNOS_GPD(exynos4_pd_gps, S5P_PMU_GPS_CONF, "pd-gps");

static struct exynos_pm_domain *exynos4_pm_domains[] = {
	&exynos4_pd_mfc,
	&exynos4_pd_g3d,
	&exynos4_pd_lcd0,
	&exynos4_pd_lcd1,
	&exynos4_pd_tv,
	&exynos4_pd_cam,
	&exynos4_pd_gps,
};

static __init int exynos4_pm_init_power_domain(void)
{
	int idx;

	if (of_have_populated_dt())
		return exynos_pm_dt_parse_domains();

	for (idx = 0; idx < ARRAY_SIZE(exynos4_pm_domains); idx++) {
		struct exynos_pm_domain *pd = exynos4_pm_domains[idx];
		int on = __raw_readl(pd->base + 0x4) & S5P_INT_LOCAL_PWR_EN;

		pm_genpd_init(&pd->pd, NULL, !on);
	}

#ifdef CONFIG_S5P_DEV_FIMD0
	exynos_pm_add_dev_to_genpd(&s5p_device_fimd0, &exynos4_pd_lcd0);
#endif
#ifdef CONFIG_S5P_DEV_TV
	exynos_pm_add_dev_to_genpd(&s5p_device_hdmi, &exynos4_pd_tv);
	exynos_pm_add_dev_to_genpd(&s5p_device_mixer, &exynos4_pd_tv);
#endif
#ifdef CONFIG_S5P_DEV_MFC
	exynos_pm_add_dev_to_genpd(&s5p_device_mfc, &exynos4_pd_mfc);
#endif
#ifdef CONFIG_S5P_DEV_FIMC0
	exynos_pm_add_dev_to_genpd(&s5p_device_fimc0, &exynos4_pd_cam);
#endif
#ifdef CONFIG_S5P_DEV_FIMC1
	exynos_pm_add_dev_to_genpd(&s5p_device_fimc1, &exynos4_pd_cam);
#endif
#ifdef CONFIG_S5P_DEV_FIMC2
	exynos_pm_add_dev_to_genpd(&s5p_device_fimc2, &exynos4_pd_cam);
#endif
#ifdef CONFIG_S5P_DEV_FIMC3
	exynos_pm_add_dev_to_genpd(&s5p_device_fimc3, &exynos4_pd_cam);
#endif
#ifdef CONFIG_S5P_DEV_CSIS0
	exynos_pm_add_dev_to_genpd(&s5p_device_mipi_csis0, &exynos4_pd_cam);
#endif
#ifdef CONFIG_S5P_DEV_CSIS1
	exynos_pm_add_dev_to_genpd(&s5p_device_mipi_csis1, &exynos4_pd_cam);
#endif
#ifdef CONFIG_S5P_DEV_G2D
	exynos_pm_add_dev_to_genpd(&s5p_device_g2d, &exynos4_pd_lcd0);
#endif
#ifdef CONFIG_S5P_DEV_JPEG
	exynos_pm_add_dev_to_genpd(&s5p_device_jpeg, &exynos4_pd_cam);
#endif
	return 0;
}
arch_initcall(exynos4_pm_init_power_domain);

int __init exynos_pm_late_initcall(void)
{
	pm_genpd_poweroff_unused();
	return 0;
}
