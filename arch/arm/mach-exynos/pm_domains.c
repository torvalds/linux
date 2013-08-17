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
#include <linux/clk.h>
#include <linux/list.h>
#include <linux/suspend.h>

#include <mach/regs-pmu.h>
#include <mach/regs-clock.h>
#include <mach/sysmmu.h>
#include <mach/bts.h>
#include <mach/devfreq.h>
#include <plat/cpu.h>
#include <plat/clock.h>
#include <plat/devs.h>

/*
 * Exynos specific wrapper around the generic power domain
 */
struct exynos_pm_domain {
	struct list_head list;
	struct list_head pwr_list;
	void __iomem *base;
	void __iomem *read_base;
	bool is_off;
	struct generic_pm_domain pd;
};

struct exynos_pm_clk {
	struct list_head node;
	struct clk *clk;
};

struct exynos_pm_pwr {
	struct list_head node;
	void __iomem *reg;
};

struct exynos_pm_dev {
	struct exynos_pm_domain *pd;
	struct platform_device *pdev;
	char const *con_id;
};

#define EXYNOS_PM_DEV(NAME, PD, DEV, CON)		\
static struct exynos_pm_dev exynos5_pm_dev_##NAME = {	\
	.pd = &exynos5_pd_##PD,				\
	.pdev = DEV,					\
	.con_id = CON,					\
}

static bool exynos_need_bts(const char *name)
{
	if (!strcmp(name, "pd-gscl") || !strcmp(name, "pd-mfc") ||
		!strcmp(name, "pd-g2d") || !strcmp(name, "pd-g3d") ||
		!strcmp(name, "pd-disp1") ||
		!strcmp(name, "pd-isp"))
		return true;

	return false;
}

static unsigned int exynos_pd_gscl_clock_control(bool on)
{
	struct clk *aclk_333_432_gscl_sub;
	struct clk *target_parent;

	aclk_333_432_gscl_sub = clk_get(NULL, "aclk_333_432_gscl");

	if (IS_ERR(aclk_333_432_gscl_sub)) {
		pr_err("%s : clk_get(aclk_333_432_gscl_sub) failed\n", __func__);
		return PTR_ERR(aclk_333_432_gscl_sub);
	}

	if (on) {
		/*
		 * If GSCL power domain is turned on
		 * parent of aclk_333_432_gscl is set with
		 * dout_aclk_333_432_gscl
		 */
		target_parent = clk_get(NULL, "dout_aclk_333_432_gscl");

		if (IS_ERR(target_parent)) {
			pr_err("%s : clk_get(target_parent) failed[turn on]\n", __func__);
			return PTR_ERR(target_parent);
		}
	} else {
		/*
		 * If GSCL power domain is turned off
		 * parent of aclk_333_432_gscl is set with ext_xtal
		 */
		target_parent = clk_get(NULL, "ext_xtal");

		if (IS_ERR(target_parent)) {
			pr_err("%s : clk_get(target_parent) failed[turn off]\n", __func__);
			return PTR_ERR(target_parent);
		}
	}

	clk_set_parent(aclk_333_432_gscl_sub, target_parent);

	return 0;
}

static void exynos_gscl_ctrl_save(unsigned int *gscl0, unsigned int *gscl1, bool on)
{
	if (on) {
		*gscl0 = __raw_readl(EXYNOS5_CLKGATE_IP_GSCL0);
		__raw_writel(0xFFFFFFFF, EXYNOS5_CLKGATE_IP_GSCL0);
		*gscl1 = __raw_readl(EXYNOS5_CLKGATE_IP_GSCL);
		__raw_writel(0x0000FFFF, EXYNOS5_CLKGATE_IP_GSCL);
	} else {
		__raw_writel(*gscl0, EXYNOS5_CLKGATE_IP_GSCL0);
		__raw_writel(*gscl1, EXYNOS5_CLKGATE_IP_GSCL);
	}
}

static int exynos_pd_power(struct generic_pm_domain *domain, bool power_on)
{
	struct gpd_link *link;
	struct exynos_pm_domain *pd, *spd;
	struct exynos_pm_clk *pclk, *spclk;
	struct exynos_pm_pwr *tpwr;
	void __iomem *base;
	unsigned int tmp;
	u32 timeout, pwr;
	char *op;
	int ret = 0;
	void __iomem *regs;

	unsigned int gscl0, gscl1;

	pd = container_of(domain, struct exynos_pm_domain, pd);
	base = pd->base;

	if (!base) {
		pr_debug("%s: Failed to get %s power domain base address\n",
			__func__, domain->name);
		bts_initialize(domain->name, power_on);
		return ret;
	}

	/* Enable all the clocks of IPs in power domain */
	pr_debug("%s is %s\n", domain->name, power_on ? "on" : "off");
	list_for_each_entry(pclk, &pd->list, node) {
		if (clk_enable(pclk->clk)) {
			pr_err("failed to enable clk %s\n", pclk->clk->name);
			ret = -EINVAL;
			goto unwind;
		} else {
			pr_debug("%s is enabled\n", pclk->clk->name);
		}
	}

	list_for_each_entry(link, &domain->master_links, master_node) {
		spd = container_of(link->slave, struct exynos_pm_domain, pd);
		list_for_each_entry(spclk, &spd->list, node) {
			if (clk_enable(spclk->clk)) {
				pr_err("failed to enable clk %s\n", spclk->clk->name);
				ret = -EINVAL;
				goto s_unwind;
			} else {
				pr_debug("%s is enabled\n", spclk->clk->name);
			}
		}
	}

	if (!power_on) {
		if (exynos_need_bts(domain->name))
			bts_initialize(domain->name, power_on);
	}

	list_for_each_entry(tpwr, &pd->pwr_list, node)
		__raw_writel(0, tpwr->reg);

	if (soc_is_exynos5250() &&
		!power_on && base == EXYNOS5_ISP_CONFIGURATION)
		__raw_writel(0x0, EXYNOS5_CMU_RESET_ISP_SYS_PWR_REG);

	if ((soc_is_exynos5250() || soc_is_exynos5410()) &&
		!power_on && base == EXYNOS5_MAU_CONFIGURATION) {
		__raw_writel(0x0, EXYNOS5_PAD_RETENTION_MAU_SYS_PWR_REG);
	}

	if (soc_is_exynos5410() && base == EXYNOS5_GSCL_CONFIGURATION)
		exynos_gscl_ctrl_save(&gscl0, &gscl1, true);

	if (soc_is_exynos5410() &&
		!power_on && base == EXYNOS5_GSCL_CONFIGURATION)
		exynos_pd_gscl_clock_control(false);

	pwr = power_on ? EXYNOS_INT_LOCAL_PWR_EN : 0;

	__raw_writel(pwr, base);

	/* Wait max 1ms */
	timeout = 10;

	while ((__raw_readl(base + 0x4) & EXYNOS_INT_LOCAL_PWR_EN) != pwr) {
		if (!timeout) {
			op = (power_on) ? "enable" : "disable";
			pr_err("Power domain %s %s failed\n", domain->name, op);

			/* If ISP power domain can not power off, try power off forcely */
			if (soc_is_exynos5410() &&
				!power_on && base == EXYNOS5_ISP_CONFIGURATION) {
				__raw_writel(0x0, EXYNOS5410_ISP_ARM_OPTION);

				timeout = 500;
				do {
					tmp = __raw_readl(base + 0x4) & EXYNOS_INT_LOCAL_PWR_EN;
					usleep_range(80, 100);
					timeout--;
				} while ((tmp != pwr) && timeout);

				if (!timeout) {
					pr_err("ISP WFI unset power down fail(state:%x)\n", __raw_readl(base + 0x4));

					tmp = __raw_readl(EXYNOS5410_LPI_BUS_MASK0);
					tmp |= (EXYNOS5410_LPI_BUS_MASK0_ISP0 | EXYNOS5410_LPI_BUS_MASK0_ISP1);
					__raw_writel(tmp, EXYNOS5410_LPI_BUS_MASK0);

					tmp = __raw_readl(EXYNOS5410_LPI_BUS_MASK1);
					tmp |= EXYNOS5410_LPI_BUS_MASK1_P_ISP;
					__raw_writel(tmp, EXYNOS5410_LPI_BUS_MASK1);

					timeout = 100;

					do {
						tmp = __raw_readl(base + 0x4) & EXYNOS_INT_LOCAL_PWR_EN;
						udelay(1);
						timeout--;
					} while ((tmp != pwr) && timeout);

					if (!timeout) {
						pr_err("ISP force timeout fail\n");
					} else {
						pr_err("ISP force timeout success\n");
						tmp = __raw_readl(EXYNOS5410_LPI_BUS_MASK0);
						tmp &= ~(EXYNOS5410_LPI_BUS_MASK0_ISP0 | EXYNOS5410_LPI_BUS_MASK0_ISP1);
						__raw_writel(tmp, EXYNOS5410_LPI_BUS_MASK0);

						tmp = __raw_readl(EXYNOS5410_LPI_BUS_MASK1);
						tmp &= ~EXYNOS5410_LPI_BUS_MASK1_P_ISP;
						__raw_writel(tmp, EXYNOS5410_LPI_BUS_MASK1);
					}
				} else {
					pr_err("WFI unset power down success\n");
				}
			}

			ret = -ETIMEDOUT;
			break;
		}
		timeout--;
		cpu_relax();
		usleep_range(80, 100);
	}

	if (soc_is_exynos5410() &&
		power_on && base == EXYNOS5_GSCL_CONFIGURATION)
		exynos_pd_gscl_clock_control(true);

	if (soc_is_exynos5410() && base == EXYNOS5_GSCL_CONFIGURATION)
		exynos_gscl_ctrl_save(&gscl0, &gscl1, false);

	if (power_on)
		if (exynos_need_bts(domain->name))
			bts_initialize(domain->name, power_on);

	if ((soc_is_exynos5250() || soc_is_exynos5410()) &&
		power_on && base == EXYNOS5_MAU_CONFIGURATION) {
		__raw_writel(0x10000000, EXYNOS_PAD_RET_MAUDIO_OPTION);
	}

	if (soc_is_exynos5410() &&
			power_on && base == EXYNOS5410_DISP1_CONFIGURATION) {
		regs = ioremap(0x14530000, SZ_32);
		__raw_writel(0x1, regs + 0x30);
		pr_info("HDMI phy power off : %x\n", __raw_readl(regs + 0x30));
		iounmap(regs);
	}

	/* Disable all the clocks of IPs in power domain */
	list_for_each_entry(link, &domain->master_links, master_node) {
		spd = container_of(link->slave, struct exynos_pm_domain, pd);
		list_for_each_entry(spclk, &spd->list, node) {
			clk_disable(spclk->clk);
		}
	}

	/* dummy read to check the completion of power-on sequence */
	if (power_on && pd->read_base)
		__raw_readl(pd->read_base);

	list_for_each_entry(pclk, &pd->list, node)
		clk_disable(pclk->clk);

	return ret;

s_unwind:
	list_for_each_entry_continue_reverse(link, &domain->master_links, master_node) {
		spd = container_of(link->slave, struct exynos_pm_domain, pd);
		list_for_each_entry_continue_reverse(spclk, &spd->list, node) {
			clk_disable(spclk->clk);
		}
	}
unwind:
	list_for_each_entry_continue_reverse(pclk, &pd->list, node)
		clk_disable(pclk->clk);

	return ret;
}

static int exynos_pd_power_on(struct generic_pm_domain *domain)
{
	return exynos_pd_power(domain, true);
}

static int exynos_pd_power_off(struct generic_pm_domain *domain)
{
	return exynos_pd_power(domain, false);
}

static int exynos_sub_power_on(struct generic_pm_domain *domain)
{
	return 0;
}

static int exynos_sub_power_off(struct generic_pm_domain *domain)
{
	return 0;
}

#define EXYNOS_GPD(PD, BASE, PHYS, NAME)		\
static struct exynos_pm_domain PD = {			\
	.list = LIST_HEAD_INIT((PD).list),		\
	.pwr_list = LIST_HEAD_INIT((PD).pwr_list),	\
	.base = (void __iomem *)BASE,			\
	.read_base = (void __iomem *)PHYS,		\
	.pd = {						\
		.name = NAME,					\
		.power_off = exynos_pd_power_off,	\
		.power_on = exynos_pd_power_on,	\
	},						\
}

#define EXYNOS_SUB_GPD(PD, NAME)			\
static struct exynos_pm_domain PD = {			\
	.list = LIST_HEAD_INIT((PD).list),		\
	.pwr_list = LIST_HEAD_INIT((PD).pwr_list),	\
	.pd = {						\
		.name = NAME,					\
		.power_off = exynos_sub_power_off,	\
		.power_on = exynos_sub_power_on,	\
	},						\
}

#ifdef CONFIG_OF
static __init int exynos_pm_dt_parse_domains(void)
{
	struct device_node *np;

	for_each_compatible_node(np, NULL, "samsung,exynos4210-pd") {
		struct exynos_pm_domain *pd;

		pd = kzalloc(sizeof(*pd), GFP_KERNEL);
		if (!pd) {
			pr_err("%s: failed to allocate memory for domain\n",
					__func__);
			return -ENOMEM;
		}

		if (of_get_property(np, "samsung,exynos4210-pd-off", NULL))
			pd->is_off = true;
		pd->name = np->name;
		pd->base = of_iomap(np, 0);
		pd->pd.power_off = exynos_pd_power_off;
		pd->pd.power_on = exynos_pd_power_on;
		pd->pd.of_node = np;
		pm_genpd_init(&pd->pd, NULL, false);
	}
	return 0;
}
#else
static __init int exynos_pm_dt_parse_domains(void)
{
	return 0;
}
#endif /* CONFIG_OF */

static __init void exynos_pm_add_subdomain_to_genpd(struct generic_pm_domain *genpd,
						struct generic_pm_domain *subdomain)
{
	if (pm_genpd_add_subdomain(genpd, subdomain))
		pr_debug("%s: error in adding %s subdomain to %s power domain\n",
			 __func__, subdomain->name, genpd->name);
}

static __init void exynos_pm_add_dev_to_genpd(struct platform_device *pdev,
						struct exynos_pm_domain *pd)
{
	if (pdev->dev.bus) {
		if (!pm_genpd_add_device(&pd->pd, &pdev->dev)) {
			pm_genpd_dev_need_restore(&pdev->dev, true);
			pr_info("PowerDomain : %s, Device : %s Registered\n", pd->pd.name, pdev->name);
		} else {
			pr_debug("%s: error in adding %s device to %s power domain\n",
				__func__, dev_name(&pdev->dev), pd->pd.name);
		}
	}
}

static void __init exynos_add_pwr_reg_to_pd(struct exynos_pm_domain *pd, void __iomem **array, int size)
{
	struct exynos_pm_pwr *pwr;
	void __iomem *base;
	int i;

	for (i = 0; i < size; i++) {
		base = array[i];

		if (!base)
			continue;

		pwr = kzalloc(sizeof(struct exynos_pm_pwr), GFP_KERNEL);

		if (!pwr) {
			pr_err("Unable to create new exynos_pm_pwr\n");
			continue;
		}

		pwr->reg =  base;
		list_add(&pwr->node, &pd->pwr_list);
	}
}

static void __init exynos_pm_genpd_init(struct exynos_pm_domain *pd)
{
	if (pd->read_base) {
		pd->read_base = ioremap((unsigned long)pd->read_base, SZ_4K);
		if (!pd->read_base)
			pr_err("ioremap failed.\n");
	}

	pm_genpd_init(&pd->pd, NULL, pd->is_off);
}

/* For EXYNOS4 */
EXYNOS_GPD(exynos4_pd_mfc, EXYNOS4_MFC_CONFIGURATION, EXYNOS4_PA_MFC, "pd-mfc");
EXYNOS_GPD(exynos4_pd_g3d, EXYNOS4_G3D_CONFIGURATION, EXYNOS4_PA_G3D, "pd-g3d");
EXYNOS_GPD(exynos4_pd_lcd0, EXYNOS4_LCD0_CONFIGURATION, EXYNOS4_PA_FIMD0, "pd-lcd0");
EXYNOS_GPD(exynos4_pd_tv, EXYNOS4_TV_CONFIGURATION, EXYNOS4_PA_VP, "pd-tv");
EXYNOS_GPD(exynos4_pd_cam, EXYNOS4_CAM_CONFIGURATION, EXYNOS4_PA_FIMC0, "pd-cam");
EXYNOS_GPD(exynos4_pd_gps, EXYNOS4_GPS_CONFIGURATION, EXYNOS4_PA_GPS, "pd-gps");

/* For EXYNOS4210 */
EXYNOS_GPD(exynos4210_pd_lcd1, EXYNOS4210_LCD1_CONFIGURATION, NULL, "pd-lcd1");

/* For EXYNOS4x12 */
EXYNOS_GPD(exynos4_pd_isp, EXYNOS4x12_ISP_CONFIGURATION, EXYNOS4_PA_FIMC_IS, "pd-isp");

static struct exynos_pm_domain *exynos4_pm_domains[] = {
	&exynos4_pd_mfc,
	&exynos4_pd_g3d,
	&exynos4_pd_lcd0,
	&exynos4_pd_tv,
	&exynos4_pd_cam,
	&exynos4_pd_gps,
};

static struct exynos_pm_domain *exynos4210_pm_domains[] = {
	&exynos4210_pd_lcd1,
};

static struct exynos_pm_domain *exynos4x12_pm_domains[] = {
	&exynos4_pd_isp,
};

static __init int exynos4_pm_init_power_domain(void)
{
	int idx;

	if (of_have_populated_dt())
		return exynos_pm_dt_parse_domains();

	for (idx = 0; idx < ARRAY_SIZE(exynos4_pm_domains); idx++)
		exynos_pm_genpd_init(exynos4_pm_domains[idx]);

	if (soc_is_exynos4210())
		for (idx = 0; idx < ARRAY_SIZE(exynos4210_pm_domains); idx++)
			exynos_pm_genpd_init(exynos4210_pm_domains[idx]);

	if (soc_is_exynos4212() || soc_is_exynos4412())
		for (idx = 0; idx < ARRAY_SIZE(exynos4x12_pm_domains); idx++)
			exynos_pm_genpd_init(exynos4x12_pm_domains[idx]);

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
#ifdef CONFIG_VIDEO_EXYNOS_MIPI_CSIS
	exynos_pm_add_dev_to_genpd(&s5p_device_mipi_csis0, &exynos4_pd_cam);
	exynos_pm_add_dev_to_genpd(&s5p_device_mipi_csis1, &exynos4_pd_cam);
#endif
#ifdef CONFIG_S5P_DEV_G2D
	exynos_pm_add_dev_to_genpd(&s5p_device_g2d, &exynos4_pd_lcd0);
#endif
#ifdef CONFIG_S5P_DEV_JPEG
	exynos_pm_add_dev_to_genpd(&s5p_device_jpeg, &exynos4_pd_cam);
#endif
#ifdef CONFIG_EXYNOS4_DEV_FIMC_IS
	exynos_pm_add_dev_to_genpd(&exynos4_device_fimc_is, &exynos4_pd_isp);
#endif

	exynos_pm_add_dev_to_genpd(&exynos4_device_g3d, &exynos4_pd_g3d);
	exynos_pm_add_dev_to_genpd(&SYSMMU_PLATDEV(camif0), &exynos4_pd_cam);
	exynos_pm_add_dev_to_genpd(&SYSMMU_PLATDEV(camif1), &exynos4_pd_cam);
	exynos_pm_add_dev_to_genpd(&SYSMMU_PLATDEV(fimc0), &exynos4_pd_cam);
	exynos_pm_add_dev_to_genpd(&SYSMMU_PLATDEV(fimc1), &exynos4_pd_cam);
	exynos_pm_add_dev_to_genpd(&SYSMMU_PLATDEV(fimc2), &exynos4_pd_cam);
	exynos_pm_add_dev_to_genpd(&SYSMMU_PLATDEV(fimc3), &exynos4_pd_cam);
	exynos_pm_add_dev_to_genpd(&SYSMMU_PLATDEV(mfc_lr), &exynos4_pd_mfc);
	exynos_pm_add_dev_to_genpd(&SYSMMU_PLATDEV(fimd0), &exynos4_pd_lcd0);
	exynos_pm_add_dev_to_genpd(&SYSMMU_PLATDEV(tv), &exynos4_pd_tv);
	exynos_pm_add_dev_to_genpd(&SYSMMU_PLATDEV(isp0), &exynos4_pd_isp);
	exynos_pm_add_dev_to_genpd(&SYSMMU_PLATDEV(isp1), &exynos4_pd_isp);
	exynos_pm_add_dev_to_genpd(&SYSMMU_PLATDEV(isp2), &exynos4_pd_isp);
	return 0;
}

/* For EXYNOS5 */
EXYNOS_GPD(exynos5_pd_mfc, EXYNOS5_MFC_CONFIGURATION, NULL, "pd-mfc");
EXYNOS_GPD(exynos5_pd_maudio, EXYNOS5_MAU_CONFIGURATION, NULL, "pd-maudio");
EXYNOS_GPD(exynos5_pd_disp1, EXYNOS5_DISP1_CONFIGURATION, NULL, "pd-disp1");
EXYNOS_SUB_GPD(exynos5_pd_fimd1, "pd-fimd1");
EXYNOS_SUB_GPD(exynos5_pd_hdmi, "pd-hdmi");
EXYNOS_SUB_GPD(exynos5_pd_mixer, "pd-mixer");
EXYNOS_SUB_GPD(exynos5_pd_dp, "pd-dp");
EXYNOS_GPD(exynos5_pd_gscl, EXYNOS5_GSCL_CONFIGURATION, NULL, "pd-gscl");
EXYNOS_SUB_GPD(exynos5_pd_gscl0, "pd-gscl0");
EXYNOS_SUB_GPD(exynos5_pd_gscl1, "pd-gscl1");
EXYNOS_SUB_GPD(exynos5_pd_gscl2, "pd-gscl2");
EXYNOS_SUB_GPD(exynos5_pd_gscl3, "pd-gscl3");
EXYNOS_SUB_GPD(exynos5_pd_gscl4, "pd-gscl4");
#ifdef CONFIG_VIDEO_EXYNOS_MIPI_CSIS
EXYNOS_SUB_GPD(exynos5_pd_mipi_csis0, "pd-mipi-csis0");
EXYNOS_SUB_GPD(exynos5_pd_mipi_csis1, "pd-mipi-csis1");
EXYNOS_SUB_GPD(exynos5_pd_mipi_csis2, "pd-mipi-csis2");
#endif
EXYNOS_SUB_GPD(exynos5_pd_flite0, "pd-flite0");
EXYNOS_SUB_GPD(exynos5_pd_flite1, "pd-flite1");
EXYNOS_SUB_GPD(exynos5_pd_flite2, "pd-flite2");

#ifdef CONFIG_S5P_DEV_MIPI_DSIM0
EXYNOS_SUB_GPD(exynos5_pd_mipi_dsim0, "pd-mipi_dsim");
#endif
#ifdef CONFIG_S5P_DEV_MIPI_DSIM1
EXYNOS_SUB_GPD(exynos5_pd_mipi_dsim1, "pd-mipi_dsim");
#endif

EXYNOS_GPD(exynos5_pd_isp, EXYNOS5_ISP_CONFIGURATION, NULL, "pd-isp");
EXYNOS_GPD(exynos5_pd_g3d, EXYNOS5_G3D_CONFIGURATION, NULL, "pd-g3d");
EXYNOS_GPD(exynos5_pd_g2d, NULL, NULL, "pd-g2d");

static struct exynos_pm_domain *exynos5_pm_domains[] = {
	&exynos5_pd_mfc,
#ifdef CONFIG_SND_SAMSUNG_I2S
	&exynos5_pd_maudio,
#endif
	&exynos5_pd_disp1,
	&exynos5_pd_fimd1,
#ifdef CONFIG_S5P_DEV_MIPI_DSIM0
	&exynos5_pd_mipi_dsim0,
#endif	
#ifdef CONFIG_S5P_DEV_MIPI_DSIM1
	&exynos5_pd_mipi_dsim1,
#endif
	&exynos5_pd_hdmi,
	&exynos5_pd_mixer,
	&exynos5_pd_dp,
	&exynos5_pd_gscl,
	&exynos5_pd_gscl0,
	&exynos5_pd_gscl1,
	&exynos5_pd_gscl2,
	&exynos5_pd_gscl3,
	&exynos5_pd_gscl4,
#ifdef CONFIG_VIDEO_EXYNOS_MIPI_CSIS
	&exynos5_pd_mipi_csis0,
	&exynos5_pd_mipi_csis1,
	&exynos5_pd_mipi_csis2,
#endif
	&exynos5_pd_flite0,
	&exynos5_pd_flite1,
	&exynos5_pd_flite2,

	&exynos5_pd_isp,
	&exynos5_pd_g3d,
	&exynos5_pd_g2d,
};

/* For EXYNOS5410 */
EXYNOS_GPD(exynos5410_pd_disp0, EXYNOS5410_DISP0_CONFIGURATION, NULL, "pd-disp0");

static struct exynos_pm_domain *exynos5410_pm_domains[] = {
	/*Need to add here after adding pm_add_dev_to_genpd*/
	&exynos5410_pd_disp0,
};

#ifdef CONFIG_S5P_DEV_MFC
EXYNOS_PM_DEV(mfc, mfc, &s5p_device_mfc, "mfc");
EXYNOS_PM_DEV(mfcppmu, mfc, NULL, "mfc.ppmu");
EXYNOS_PM_DEV(smmumfc, mfc, &SYSMMU_PLATDEV(mfc_lr), SYSMMU_CLOCK_NAME);
#endif
#ifdef CONFIG_SND_SAMSUNG_I2S
EXYNOS_PM_DEV(maudio, maudio, &exynos5_device_i2s0, NULL);
#endif
#ifdef CONFIG_S5P_DEV_FIMD1
EXYNOS_PM_DEV(fimd1, fimd1, &s5p_device_fimd1, "fimd");
EXYNOS_PM_DEV(axi_disp1, fimd1, NULL, "axi_disp1");
#endif
#ifdef CONFIG_S5P_DEV_MIPI_DSIM0
EXYNOS_PM_DEV(dsim0, mipi_dsim0, &s5p_device_mipi_dsim0, "dsim0");
#endif
#ifdef CONFIG_S5P_DEV_MIPI_DSIM1
EXYNOS_PM_DEV(dsim1, mipi_dsim1, &s5p_device_mipi_dsim1, "dsim1");
#endif
#ifdef CONFIG_S5P_DEV_TV
EXYNOS_PM_DEV(hdmi, hdmi, &s5p_device_hdmi, "hdmi");
EXYNOS_PM_DEV(mixer, mixer, &s5p_device_mixer, "mixer");
EXYNOS_PM_DEV(smmutv, mixer, &SYSMMU_PLATDEV(tv), SYSMMU_CLOCK_NAME);
#endif
#ifdef CONFIG_S5P_DEV_DP
EXYNOS_PM_DEV(dp, dp, &s5p_device_dp, "dp");
#endif

#ifdef CONFIG_EXYNOS5_DEV_GSC
EXYNOS_PM_DEV(gscl0, gscl0, &exynos5_device_gsc0, "gscl");
EXYNOS_PM_DEV(gscl1, gscl1, &exynos5_device_gsc1, "gscl");
EXYNOS_PM_DEV(gscl2, gscl2, &exynos5_device_gsc2, "gscl");
EXYNOS_PM_DEV(gscl3, gscl3, &exynos5_device_gsc3, "gscl");
#ifdef CONFIG_EXYNOS5_DEV_SCALER
EXYNOS_PM_DEV(scaler, gscl, &exynos5_device_scaler0, "sc-pclk");
#endif
#endif
#ifdef CONFIG_EXYNOS5_DEV_FIMC_IS
EXYNOS_PM_DEV(isp, isp, &exynos5_device_fimc_is, NULL);
#endif
#ifdef CONFIG_S5P_DEV_FIMG2D
EXYNOS_PM_DEV(g2d, g2d, &s5p_device_fimg2d, "fimg2d");
#endif
EXYNOS_PM_DEV(sgx_core, g3d, NULL, "sgx_core");
EXYNOS_PM_DEV(sgx_hyd, g3d, NULL, "sgx_hyd");

static struct exynos_pm_dev *exynos_pm_devs[] = {
#ifdef CONFIG_S5P_DEV_MFC
	&exynos5_pm_dev_mfc,
	&exynos5_pm_dev_mfcppmu,
	&exynos5_pm_dev_smmumfc,
#endif
#ifdef CONFIG_SND_SAMSUNG_I2S
	&exynos5_pm_dev_maudio,
#endif
#ifdef CONFIG_S5P_DEV_FIMD1
	&exynos5_pm_dev_fimd1,
	&exynos5_pm_dev_axi_disp1,
#endif
#ifdef CONFIG_S5P_DEV_MIPI_DSIM0
	&exynos5_pm_dev_dsim0,
#endif
#ifdef CONFIG_S5P_DEV_MIPI_DSIM1
	&exynos5_pm_dev_dsim1,
#endif
#ifdef CONFIG_S5P_DEV_TV
	&exynos5_pm_dev_hdmi,
	&exynos5_pm_dev_mixer,
	&exynos5_pm_dev_smmutv,
#endif
#ifdef CONFIG_S5P_DEV_DP
	&exynos5_pm_dev_dp,
#endif
#ifdef CONFIG_EXYNOS5_DEV_GSC
	&exynos5_pm_dev_gscl0,
	&exynos5_pm_dev_gscl1,
	&exynos5_pm_dev_gscl2,
	&exynos5_pm_dev_gscl3,
#ifdef CONFIG_EXYNOS5_DEV_SCALER
	&exynos5_pm_dev_scaler,
#endif
#endif
#ifdef CONFIG_EXYNOS5_DEV_FIMC_IS
	&exynos5_pm_dev_isp,
#endif
#ifdef CONFIG_S5P_DEV_FIMG2D
	&exynos5_pm_dev_g2d,
#endif
	&exynos5_pm_dev_sgx_core,
	&exynos5_pm_dev_sgx_hyd,
};

static void __iomem *exynos5_pwr_reg_g3d[] = {
	EXYNOS5_CMU_CLKSTOP_G3D_SYS_PWR_REG,
	EXYNOS5_CMU_SYSCLK_G3D_SYS_PWR_REG,
	EXYNOS5_CMU_RESET_G3D_SYS_PWR_REG,
};

static void __iomem *exynos5_pwr_reg_mfc[] = {
	EXYNOS5_CMU_CLKSTOP_MFC_SYS_PWR_REG,
	EXYNOS5_CMU_SYSCLK_MFC_SYS_PWR_REG,
	EXYNOS5_CMU_RESET_MFC_SYS_PWR_REG,
};

static void __iomem *exynos5_pwr_reg_mau[] = {
	EXYNOS5_CMU_CLKSTOP_MAU_SYS_PWR_REG,
	EXYNOS5_CMU_SYSCLK_MAU_SYS_PWR_REG,
	EXYNOS5_CMU_RESET_MAU_SYS_PWR_REG,
};

static void __iomem *exynos5_pwr_reg_disp1[] = {
	EXYNOS5_CMU_CLKSTOP_DISP1_SYS_PWR_REG,
	EXYNOS5_CMU_SYSCLK_DISP1_SYS_PWR_REG,
	EXYNOS5_CMU_RESET_DISP1_SYS_PWR_REG,
};

static void __iomem *exynos5_pwr_reg_isp[] = {
	EXYNOS5_CMU_CLKSTOP_ISP_SYS_PWR_REG,
	EXYNOS5_CMU_SYSCLK_ISP_SYS_PWR_REG,
	EXYNOS5_CMU_RESET_ISP_SYS_PWR_REG,
};

static void __init exynos5_add_device_to_pd(struct exynos_pm_dev **pm_dev, int size)
{
	struct exynos_pm_dev *tdev;
	struct exynos_pm_clk *pclk;
	struct clk *clk;
	int i;

	if (pm_dev == NULL) {
		pr_err("Cant add device to power domain\n");
		return;
	}

	for (i = 0; i < size; i++) {
		tdev = pm_dev[i];

		if (tdev == NULL) {
			pr_err("Cant add null device to power domain\n");
			continue;
		}

		if (!tdev->con_id)
			continue;

		pclk = kzalloc(sizeof(struct exynos_pm_clk), GFP_KERNEL);

		if (!pclk) {
			pr_err("Unable to create new exynos_pm_clk\n");
			continue;
		}

		if (tdev->pdev != NULL)
			clk = clk_get(&tdev->pdev->dev, tdev->con_id);
		else
			clk = clk_get(NULL, tdev->con_id);

		if (!IS_ERR(clk)) {
			pclk->clk =  clk;
			list_add(&pclk->node, &tdev->pd->list);
		} else {
			if (tdev->pdev != NULL)
				pr_err("Failed to get %s clock\n", dev_name(&tdev->pdev->dev));
			else
				pr_err("Failed to get clock, %s:%d\n", __func__, __LINE__);
			kfree(pclk);
		}

	}
}
/**
 * exynos_pm_notifier_fn - EXYNOS PM notifier routine.
 * @notifier: Unused.
 * @pm_event: Event being handled.
 * @unused: Unused.
 */
static int exynos_pm_notifier_fn(struct notifier_block *notifier,
		unsigned long pm_event, void *unused)
{
	switch (pm_event) {
	case PM_SUSPEND_PREPARE:
		pm_genpd_poweron(&exynos5_pd_g3d.pd);
		break;
	case PM_POST_SUSPEND:
		bts_initialize("pd-eagle", true);
		bts_initialize("pd-kfc", true);
		break;
	}

	return NOTIFY_DONE;
}

static int __init exynos5_pm_init_power_domain(void)
{
	int idx;

	if (of_have_populated_dt())
		return exynos_pm_dt_parse_domains();

	if (soc_is_exynos5410()) {
		exynos5_pd_mfc.base = EXYNOS5410_MFC_CONFIGURATION;
		exynos5_pd_g3d.base = EXYNOS5410_G3D_CONFIGURATION;
		exynos5_pd_disp1.base = EXYNOS5410_DISP1_CONFIGURATION;
		exynos5_pd_maudio.base = EXYNOS5410_MAU_CONFIGURATION;

		for (idx = 0; idx < ARRAY_SIZE(exynos5410_pm_domains); idx++)
			pm_genpd_init(&exynos5410_pm_domains[idx]->pd, NULL,
					exynos5410_pm_domains[idx]->is_off);
	}

	for (idx = 0; idx < ARRAY_SIZE(exynos5_pm_domains); idx++)
		exynos_pm_genpd_init(exynos5_pm_domains[idx]);

#ifdef CONFIG_S5P_DEV_MFC
	exynos_pm_add_dev_to_genpd(&s5p_device_mfc, &exynos5_pd_mfc);

	if (soc_is_exynos5410())
		exynos_add_pwr_reg_to_pd(&exynos5_pd_mfc, exynos5_pwr_reg_mfc, ARRAY_SIZE(exynos5_pwr_reg_mfc));
#endif
#ifdef CONFIG_VIDEO_EXYNOS_MIPI_CSIS
	exynos_pm_add_subdomain_to_genpd(&exynos5_pd_gscl.pd,
					 &exynos5_pd_mipi_csis0.pd);
	exynos_pm_add_subdomain_to_genpd(&exynos5_pd_gscl.pd,
					 &exynos5_pd_mipi_csis1.pd);
	exynos_pm_add_subdomain_to_genpd(&exynos5_pd_gscl.pd,
					 &exynos5_pd_mipi_csis2.pd);
	exynos_pm_add_dev_to_genpd(&s5p_device_mipi_csis0, &exynos5_pd_mipi_csis0);
	exynos_pm_add_dev_to_genpd(&s5p_device_mipi_csis1, &exynos5_pd_mipi_csis1);
	exynos_pm_add_dev_to_genpd(&s5p_device_mipi_csis2, &exynos5_pd_mipi_csis2);
#endif
#ifdef CONFIG_VIDEO_EXYNOS_FIMC_LITE
	exynos_pm_add_subdomain_to_genpd(&exynos5_pd_gscl.pd, &exynos5_pd_flite0.pd);
	exynos_pm_add_subdomain_to_genpd(&exynos5_pd_gscl.pd, &exynos5_pd_flite1.pd);
	exynos_pm_add_subdomain_to_genpd(&exynos5_pd_gscl.pd, &exynos5_pd_flite2.pd);
	exynos_pm_add_dev_to_genpd(&exynos_device_flite0, &exynos5_pd_flite0);
	exynos_pm_add_dev_to_genpd(&exynos_device_flite1, &exynos5_pd_flite1);
	exynos_pm_add_dev_to_genpd(&exynos_device_flite2, &exynos5_pd_flite2);
#endif
#ifdef CONFIG_SND_SAMSUNG_I2S
	exynos_pm_add_dev_to_genpd(&exynos5_device_i2s0, &exynos5_pd_maudio);

	if (soc_is_exynos5410())
		exynos_add_pwr_reg_to_pd(&exynos5_pd_maudio, exynos5_pwr_reg_mau, ARRAY_SIZE(exynos5_pwr_reg_mau));
#endif
#ifdef CONFIG_S5P_DEV_FIMD1
	exynos_pm_add_subdomain_to_genpd(&exynos5_pd_disp1.pd, &exynos5_pd_fimd1.pd);
	exynos_pm_add_dev_to_genpd(&s5p_device_fimd1, &exynos5_pd_fimd1);

	if (soc_is_exynos5410())
		exynos_add_pwr_reg_to_pd(&exynos5_pd_disp1, exynos5_pwr_reg_disp1, ARRAY_SIZE(exynos5_pwr_reg_disp1));
#endif
#ifdef CONFIG_S5P_DEV_MIPI_DSIM0
	exynos_pm_add_subdomain_to_genpd(&exynos5_pd_disp1.pd, &exynos5_pd_mipi_dsim0.pd);
	exynos_pm_add_dev_to_genpd(&s5p_device_mipi_dsim0, &exynos5_pd_mipi_dsim0);
#endif
#ifdef CONFIG_S5P_DEV_MIPI_DSIM1
	exynos_pm_add_subdomain_to_genpd(&exynos5_pd_disp1.pd, &exynos5_pd_mipi_dsim1.pd);
	exynos_pm_add_dev_to_genpd(&s5p_device_mipi_dsim1, &exynos5_pd_mipi_dsim1);
#endif
#ifdef CONFIG_S5P_DEV_TV
	exynos_pm_add_subdomain_to_genpd(&exynos5_pd_disp1.pd, &exynos5_pd_hdmi.pd);
	exynos_pm_add_subdomain_to_genpd(&exynos5_pd_disp1.pd, &exynos5_pd_mixer.pd);
	exynos_pm_add_dev_to_genpd(&s5p_device_hdmi, &exynos5_pd_hdmi);
	exynos_pm_add_dev_to_genpd(&s5p_device_mixer, &exynos5_pd_mixer);
#endif
#ifdef CONFIG_S5P_DEV_DP
	exynos_pm_add_subdomain_to_genpd(&exynos5_pd_disp1.pd, &exynos5_pd_dp.pd);
	exynos_pm_add_dev_to_genpd(&s5p_device_dp, &exynos5_pd_dp);
#endif
#ifdef CONFIG_EXYNOS5_DEV_GSC
	exynos_pm_add_subdomain_to_genpd(&exynos5_pd_gscl.pd, &exynos5_pd_gscl0.pd);
	exynos_pm_add_subdomain_to_genpd(&exynos5_pd_gscl.pd, &exynos5_pd_gscl1.pd);
	exynos_pm_add_subdomain_to_genpd(&exynos5_pd_gscl.pd, &exynos5_pd_gscl2.pd);
	exynos_pm_add_subdomain_to_genpd(&exynos5_pd_gscl.pd, &exynos5_pd_gscl3.pd);
#ifdef CONFIG_EXYNOS5_DEV_SCALER
	exynos_pm_add_subdomain_to_genpd(&exynos5_pd_gscl.pd, &exynos5_pd_gscl4.pd);
#endif
	exynos_pm_add_dev_to_genpd(&exynos5_device_gsc0, &exynos5_pd_gscl0);
	exynos_pm_add_dev_to_genpd(&exynos5_device_gsc1, &exynos5_pd_gscl1);
	exynos_pm_add_dev_to_genpd(&exynos5_device_gsc2, &exynos5_pd_gscl2);
	exynos_pm_add_dev_to_genpd(&exynos5_device_gsc3, &exynos5_pd_gscl3);
#ifdef CONFIG_EXYNOS5_DEV_SCALER
	exynos_pm_add_dev_to_genpd(&exynos5_device_scaler0, &exynos5_pd_gscl4);
#endif
#endif
#ifdef CONFIG_EXYNOS5_DEV_FIMC_IS
	exynos_pm_add_dev_to_genpd(&exynos5_device_fimc_is, &exynos5_pd_isp);
	exynos_pm_add_dev_to_genpd(&s3c64xx_device_spi3, &exynos5_pd_isp);
	exynos_pm_add_subdomain_to_genpd(&exynos5_pd_gscl.pd, &exynos5_pd_isp.pd);

	if (soc_is_exynos5410())
		exynos_add_pwr_reg_to_pd(&exynos5_pd_isp, exynos5_pwr_reg_isp, ARRAY_SIZE(exynos5_pwr_reg_isp));
#endif

#ifdef CONFIG_PVR_SGX
	exynos_pm_add_dev_to_genpd(&exynos5_device_g3d, &exynos5_pd_g3d);

	if (soc_is_exynos5410())
		exynos_add_pwr_reg_to_pd(&exynos5_pd_g3d, exynos5_pwr_reg_g3d, ARRAY_SIZE(exynos5_pwr_reg_g3d));
#endif
#ifdef CONFIG_S5P_DEV_FIMG2D
	exynos_pm_add_dev_to_genpd(&s5p_device_fimg2d, &exynos5_pd_g2d);
#endif

	exynos_pm_add_dev_to_genpd(&SYSMMU_PLATDEV(camif0), &exynos5_pd_gscl);
	exynos_pm_add_dev_to_genpd(&SYSMMU_PLATDEV(camif1), &exynos5_pd_gscl);
	exynos_pm_add_dev_to_genpd(&SYSMMU_PLATDEV(camif2), &exynos5_pd_gscl);
	exynos_pm_add_dev_to_genpd(&SYSMMU_PLATDEV(gsc0), &exynos5_pd_gscl0);
	exynos_pm_add_dev_to_genpd(&SYSMMU_PLATDEV(gsc1), &exynos5_pd_gscl1);
	exynos_pm_add_dev_to_genpd(&SYSMMU_PLATDEV(gsc2), &exynos5_pd_gscl2);
	exynos_pm_add_dev_to_genpd(&SYSMMU_PLATDEV(gsc3), &exynos5_pd_gscl3);
	exynos_pm_add_dev_to_genpd(&SYSMMU_PLATDEV(isp3), &exynos5_pd_gscl);
	exynos_pm_add_dev_to_genpd(&SYSMMU_PLATDEV(s3d), &exynos5_pd_gscl);
#ifdef CONFIG_EXYNOS5_DEV_SCALER
	exynos_pm_add_dev_to_genpd(&SYSMMU_PLATDEV(scaler), &exynos5_pd_gscl4);
#endif
	exynos_pm_add_dev_to_genpd(&SYSMMU_PLATDEV(fimd1), &exynos5_pd_fimd1);
	exynos_pm_add_dev_to_genpd(&SYSMMU_PLATDEV(tv), &exynos5_pd_mixer);
	exynos_pm_add_dev_to_genpd(&SYSMMU_PLATDEV(isp0), &exynos5_pd_isp);
	exynos_pm_add_dev_to_genpd(&SYSMMU_PLATDEV(isp1), &exynos5_pd_isp);
	exynos_pm_add_dev_to_genpd(&SYSMMU_PLATDEV(isp2), &exynos5_pd_isp);
	exynos_pm_add_dev_to_genpd(&SYSMMU_PLATDEV(mfc_lr), &exynos5_pd_mfc);
	exynos_pm_add_dev_to_genpd(&SYSMMU_PLATDEV(2d), &exynos5_pd_g2d);

	exynos5_add_device_to_pd(exynos_pm_devs, ARRAY_SIZE(exynos_pm_devs));

	/* Initialize BTS */
	bts_initialize("pd-mfc", true);
	bts_initialize("pd-gscl", true);
	bts_initialize("pd-g3d", true);

	return 0;
}

static int __init exynos_pm_init_power_domain(void)
{
	if (soc_is_exynos5250() || soc_is_exynos5410()) {
		pm_notifier(exynos_pm_notifier_fn, 0);

		return exynos5_pm_init_power_domain();
	} else {
		return exynos4_pm_init_power_domain();
	}
}
arch_initcall(exynos_pm_init_power_domain);

static __init int exynos_pm_late_initcall(void)
{
	pm_genpd_poweroff_unused();
	return 0;
}
late_initcall(exynos_pm_late_initcall);
