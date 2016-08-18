/*
 * Rockchip Generic power domain support.
 *
 * Copyright (c) 2015 ROCKCHIP, Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/io.h>
#include <linux/err.h>
#include <linux/pm_clock.h>
#include <linux/pm_domain.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/clk.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <dt-bindings/power/rk3288-power.h>
#include <dt-bindings/power/rk3368-power.h>
#include <dt-bindings/power/rk3399-power.h>

struct rockchip_domain_info {
	int pwr_mask;
	int status_mask;
	int req_mask;
	int idle_mask;
	int ack_mask;
	bool active_wakeup;
};

struct rockchip_pmu_info {
	u32 pwr_offset;
	u32 status_offset;
	u32 req_offset;
	u32 idle_offset;
	u32 ack_offset;

	u32 core_pwrcnt_offset;
	u32 gpu_pwrcnt_offset;

	unsigned int core_power_transition_time;
	unsigned int gpu_power_transition_time;

	int num_domains;
	const struct rockchip_domain_info *domain_info;
};

#define MAX_QOS_REGS_NUM	5
#define QOS_PRIORITY		0x08
#define QOS_MODE		0x0c
#define QOS_BANDWIDTH		0x10
#define QOS_SATURATION		0x14
#define QOS_EXTCONTROL		0x18

struct rockchip_pm_domain {
	struct generic_pm_domain genpd;
	const struct rockchip_domain_info *info;
	struct rockchip_pmu *pmu;
	int num_qos;
	struct regmap **qos_regmap;
	u32 *qos_save_regs[MAX_QOS_REGS_NUM];
	int num_clks;
	struct clk *clks[];
};

struct rockchip_pmu {
	struct device *dev;
	struct regmap *regmap;
	const struct rockchip_pmu_info *info;
	struct mutex mutex; /* mutex lock for pmu */
	struct genpd_onecell_data genpd_data;
	struct generic_pm_domain *domains[];
};

#define to_rockchip_pd(gpd) container_of(gpd, struct rockchip_pm_domain, genpd)

#define DOMAIN(pwr, status, req, idle, ack, wakeup)	\
{						\
	.pwr_mask = (pwr >= 0) ? BIT(pwr) : 0,		\
	.status_mask = (status >= 0) ? BIT(status) : 0,	\
	.req_mask = (req >= 0) ? BIT(req) : 0,		\
	.idle_mask = (idle >= 0) ? BIT(idle) : 0,	\
	.ack_mask = (ack >= 0) ? BIT(ack) : 0,		\
	.active_wakeup = wakeup,			\
}

#define DOMAIN_RK3288(pwr, status, req, wakeup)		\
	DOMAIN(pwr, status, req, req, (req) + 16, wakeup)

#define DOMAIN_RK3368(pwr, status, req, wakeup)		\
	DOMAIN(pwr, status, req, (req) + 16, req, wakeup)

#define DOMAIN_RK3399(pwr, status, req, wakeup)		\
	DOMAIN(pwr, status, req, req, req, wakeup)

static bool rockchip_pmu_domain_is_idle(struct rockchip_pm_domain *pd)
{
	struct rockchip_pmu *pmu = pd->pmu;
	const struct rockchip_domain_info *pd_info = pd->info;
	unsigned int val;

	regmap_read(pmu->regmap, pmu->info->idle_offset, &val);
	return (val & pd_info->idle_mask) == pd_info->idle_mask;
}

static int rockchip_pmu_set_idle_request(struct rockchip_pm_domain *pd,
					 bool idle)
{
	const struct rockchip_domain_info *pd_info = pd->info;
	struct rockchip_pmu *pmu = pd->pmu;
	unsigned int val;

	if (pd_info->req_mask == 0)
		return 0;

	regmap_update_bits(pmu->regmap, pmu->info->req_offset,
			   pd_info->req_mask, idle ? -1U : 0);

	dsb(sy);

	do {
		regmap_read(pmu->regmap, pmu->info->ack_offset, &val);
	} while ((val & pd_info->ack_mask) != (idle ? pd_info->ack_mask : 0));

	while (rockchip_pmu_domain_is_idle(pd) != idle)
		cpu_relax();

	return 0;
}

static int rockchip_pmu_save_qos(struct rockchip_pm_domain *pd)
{
	int i;

	for (i = 0; i < pd->num_qos; i++) {
		regmap_read(pd->qos_regmap[i],
			    QOS_PRIORITY,
			    &pd->qos_save_regs[0][i]);
		regmap_read(pd->qos_regmap[i],
			    QOS_MODE,
			    &pd->qos_save_regs[1][i]);
		regmap_read(pd->qos_regmap[i],
			    QOS_BANDWIDTH,
			    &pd->qos_save_regs[2][i]);
		regmap_read(pd->qos_regmap[i],
			    QOS_SATURATION,
			    &pd->qos_save_regs[3][i]);
		regmap_read(pd->qos_regmap[i],
			    QOS_EXTCONTROL,
			    &pd->qos_save_regs[4][i]);
	}
	return 0;
}

static int rockchip_pmu_restore_qos(struct rockchip_pm_domain *pd)
{
	int i;

	for (i = 0; i < pd->num_qos; i++) {
		regmap_write(pd->qos_regmap[i],
			     QOS_PRIORITY,
			     pd->qos_save_regs[0][i]);
		regmap_write(pd->qos_regmap[i],
			     QOS_MODE,
			     pd->qos_save_regs[1][i]);
		regmap_write(pd->qos_regmap[i],
			     QOS_BANDWIDTH,
			     pd->qos_save_regs[2][i]);
		regmap_write(pd->qos_regmap[i],
			     QOS_SATURATION,
			     pd->qos_save_regs[3][i]);
		regmap_write(pd->qos_regmap[i],
			     QOS_EXTCONTROL,
			     pd->qos_save_regs[4][i]);
	}

	return 0;
}

static bool rockchip_pmu_domain_is_on(struct rockchip_pm_domain *pd)
{
	struct rockchip_pmu *pmu = pd->pmu;
	unsigned int val;

	/* check idle status for idle-only domains */
	if (pd->info->status_mask == 0)
		return !rockchip_pmu_domain_is_idle(pd);

	regmap_read(pmu->regmap, pmu->info->status_offset, &val);

	/* 1'b0: power on, 1'b1: power off */
	return !(val & pd->info->status_mask);
}

static void rockchip_do_pmu_set_power_domain(struct rockchip_pm_domain *pd,
					     bool on)
{
	struct rockchip_pmu *pmu = pd->pmu;

	if (pd->info->pwr_mask == 0)
		return;

	regmap_update_bits(pmu->regmap, pmu->info->pwr_offset,
			   pd->info->pwr_mask, on ? 0 : -1U);

	dsb(sy);

	while (rockchip_pmu_domain_is_on(pd) != on)
		cpu_relax();
}

static int rockchip_pd_power(struct rockchip_pm_domain *pd, bool power_on)
{
	int i;

	mutex_lock(&pd->pmu->mutex);

	if (rockchip_pmu_domain_is_on(pd) != power_on) {
		for (i = 0; i < pd->num_clks; i++)
			clk_enable(pd->clks[i]);

		if (!power_on) {
			rockchip_pmu_save_qos(pd);

			/* if powering down, idle request to NIU first */
			rockchip_pmu_set_idle_request(pd, true);
		}

		rockchip_do_pmu_set_power_domain(pd, power_on);

		if (power_on) {
			/* if powering up, leave idle mode */
			rockchip_pmu_set_idle_request(pd, false);

			rockchip_pmu_restore_qos(pd);
		}

		for (i = pd->num_clks - 1; i >= 0; i--)
			clk_disable(pd->clks[i]);
	}

	mutex_unlock(&pd->pmu->mutex);
	return 0;
}

static int rockchip_pd_power_on(struct generic_pm_domain *domain)
{
	struct rockchip_pm_domain *pd = to_rockchip_pd(domain);

	return rockchip_pd_power(pd, true);
}

static int rockchip_pd_power_off(struct generic_pm_domain *domain)
{
	struct rockchip_pm_domain *pd = to_rockchip_pd(domain);

	return rockchip_pd_power(pd, false);
}

static int rockchip_pd_attach_dev(struct generic_pm_domain *genpd,
				  struct device *dev)
{
	struct clk *clk;
	int i;
	int error;

	dev_dbg(dev, "attaching to power domain '%s'\n", genpd->name);

	error = pm_clk_create(dev);
	if (error) {
		dev_err(dev, "pm_clk_create failed %d\n", error);
		return error;
	}

	i = 0;
	while ((clk = of_clk_get(dev->of_node, i++)) && !IS_ERR(clk)) {
		dev_dbg(dev, "adding clock '%pC' to list of PM clocks\n", clk);
		error = pm_clk_add_clk(dev, clk);
		if (error) {
			dev_err(dev, "pm_clk_add_clk failed %d\n", error);
			clk_put(clk);
			pm_clk_destroy(dev);
			return error;
		}
	}

	return 0;
}

static void rockchip_pd_detach_dev(struct generic_pm_domain *genpd,
				   struct device *dev)
{
	dev_dbg(dev, "detaching from power domain '%s'\n", genpd->name);

	pm_clk_destroy(dev);
}

static bool rockchip_active_wakeup(struct device *dev)
{
	struct generic_pm_domain *genpd;
	struct rockchip_pm_domain *pd;

	genpd = pd_to_genpd(dev->pm_domain);
	pd = container_of(genpd, struct rockchip_pm_domain, genpd);

	return pd->info->active_wakeup;
}

static int rockchip_pm_add_one_domain(struct rockchip_pmu *pmu,
				      struct device_node *node)
{
	const struct rockchip_domain_info *pd_info;
	struct rockchip_pm_domain *pd;
	struct device_node *qos_node;
	struct clk *clk;
	int clk_cnt;
	int i, j;
	u32 id;
	int error;

	error = of_property_read_u32(node, "reg", &id);
	if (error) {
		dev_err(pmu->dev,
			"%s: failed to retrieve domain id (reg): %d\n",
			node->name, error);
		return -EINVAL;
	}

	if (id >= pmu->info->num_domains) {
		dev_err(pmu->dev, "%s: invalid domain id %d\n",
			node->name, id);
		return -EINVAL;
	}

	pd_info = &pmu->info->domain_info[id];
	if (!pd_info) {
		dev_err(pmu->dev, "%s: undefined domain id %d\n",
			node->name, id);
		return -EINVAL;
	}

	clk_cnt = of_count_phandle_with_args(node, "clocks", "#clock-cells");
	pd = devm_kzalloc(pmu->dev,
			  sizeof(*pd) + clk_cnt * sizeof(pd->clks[0]),
			  GFP_KERNEL);
	if (!pd)
		return -ENOMEM;

	pd->info = pd_info;
	pd->pmu = pmu;

	for (i = 0; i < clk_cnt; i++) {
		clk = of_clk_get(node, i);
		if (IS_ERR(clk)) {
			error = PTR_ERR(clk);
			dev_err(pmu->dev,
				"%s: failed to get clk at index %d: %d\n",
				node->name, i, error);
			goto err_out;
		}

		error = clk_prepare(clk);
		if (error) {
			dev_err(pmu->dev,
				"%s: failed to prepare clk %pC (index %d): %d\n",
				node->name, clk, i, error);
			clk_put(clk);
			goto err_out;
		}

		pd->clks[pd->num_clks++] = clk;

		dev_dbg(pmu->dev, "added clock '%pC' to domain '%s'\n",
			clk, node->name);
	}

	pd->num_qos = of_count_phandle_with_args(node, "pm_qos",
						 NULL);

	if (pd->num_qos > 0) {
		pd->qos_regmap = devm_kcalloc(pmu->dev, pd->num_qos,
					      sizeof(*pd->qos_regmap),
					      GFP_KERNEL);
		if (!pd->qos_regmap) {
			error = -ENOMEM;
			goto err_out;
		}

		for (j = 0; j < MAX_QOS_REGS_NUM; j++) {
			pd->qos_save_regs[j] = devm_kcalloc(pmu->dev,
							    pd->num_qos,
							    sizeof(u32),
							    GFP_KERNEL);
			if (!pd->qos_save_regs[j]) {
				error = -ENOMEM;
				goto err_out;
			}
		}

		for (j = 0; j < pd->num_qos; j++) {
			qos_node = of_parse_phandle(node, "pm_qos", j);
			if (!qos_node) {
				error = -ENODEV;
				goto err_out;
			}
			pd->qos_regmap[j] = syscon_node_to_regmap(qos_node);
			if (IS_ERR(pd->qos_regmap[j])) {
				error = -ENODEV;
				of_node_put(qos_node);
				goto err_out;
			}
			of_node_put(qos_node);
		}
	}

	error = rockchip_pd_power(pd, true);
	if (error) {
		dev_err(pmu->dev,
			"failed to power on domain '%s': %d\n",
			node->name, error);
		goto err_out;
	}

	pd->genpd.name = node->name;
	pd->genpd.power_off = rockchip_pd_power_off;
	pd->genpd.power_on = rockchip_pd_power_on;
	pd->genpd.attach_dev = rockchip_pd_attach_dev;
	pd->genpd.detach_dev = rockchip_pd_detach_dev;
	pd->genpd.dev_ops.active_wakeup = rockchip_active_wakeup;
	pd->genpd.flags = GENPD_FLAG_PM_CLK;
	pm_genpd_init(&pd->genpd, NULL, false);

	pmu->genpd_data.domains[id] = &pd->genpd;
	return 0;

err_out:
	while (--i >= 0) {
		clk_unprepare(pd->clks[i]);
		clk_put(pd->clks[i]);
	}
	return error;
}

static void rockchip_pm_remove_one_domain(struct rockchip_pm_domain *pd)
{
	int i;

	for (i = 0; i < pd->num_clks; i++) {
		clk_unprepare(pd->clks[i]);
		clk_put(pd->clks[i]);
	}

	/* protect the zeroing of pm->num_clks */
	mutex_lock(&pd->pmu->mutex);
	pd->num_clks = 0;
	mutex_unlock(&pd->pmu->mutex);

	/* devm will free our memory */
}

static void rockchip_pm_domain_cleanup(struct rockchip_pmu *pmu)
{
	struct generic_pm_domain *genpd;
	struct rockchip_pm_domain *pd;
	int i;

	for (i = 0; i < pmu->genpd_data.num_domains; i++) {
		genpd = pmu->genpd_data.domains[i];
		if (genpd) {
			pd = to_rockchip_pd(genpd);
			rockchip_pm_remove_one_domain(pd);
		}
	}

	/* devm will free our memory */
}

static void rockchip_configure_pd_cnt(struct rockchip_pmu *pmu,
				      u32 domain_reg_offset,
				      unsigned int count)
{
	/* First configure domain power down transition count ... */
	regmap_write(pmu->regmap, domain_reg_offset, count);
	/* ... and then power up count. */
	regmap_write(pmu->regmap, domain_reg_offset + 4, count);
}

static int rockchip_pm_add_subdomain(struct rockchip_pmu *pmu,
				     struct device_node *parent)
{
	struct device_node *np;
	struct generic_pm_domain *child_domain, *parent_domain;
	int error;

	for_each_child_of_node(parent, np) {
		u32 idx;

		error = of_property_read_u32(parent, "reg", &idx);
		if (error) {
			dev_err(pmu->dev,
				"%s: failed to retrieve domain id (reg): %d\n",
				parent->name, error);
			goto err_out;
		}
		parent_domain = pmu->genpd_data.domains[idx];

		error = rockchip_pm_add_one_domain(pmu, np);
		if (error) {
			dev_err(pmu->dev, "failed to handle node %s: %d\n",
				np->name, error);
			goto err_out;
		}

		error = of_property_read_u32(np, "reg", &idx);
		if (error) {
			dev_err(pmu->dev,
				"%s: failed to retrieve domain id (reg): %d\n",
				np->name, error);
			goto err_out;
		}
		child_domain = pmu->genpd_data.domains[idx];

		error = pm_genpd_add_subdomain(parent_domain, child_domain);
		if (error) {
			dev_err(pmu->dev, "%s failed to add subdomain %s: %d\n",
				parent_domain->name, child_domain->name, error);
			goto err_out;
		} else {
			dev_dbg(pmu->dev, "%s add subdomain: %s\n",
				parent_domain->name, child_domain->name);
		}

		rockchip_pm_add_subdomain(pmu, np);
	}

	return 0;

err_out:
	of_node_put(np);
	return error;
}

static int rockchip_pm_domain_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct device_node *node;
	struct device *parent;
	struct rockchip_pmu *pmu;
	const struct of_device_id *match;
	const struct rockchip_pmu_info *pmu_info;
	int error;

	if (!np) {
		dev_err(dev, "device tree node not found\n");
		return -ENODEV;
	}

	match = of_match_device(dev->driver->of_match_table, dev);
	if (!match || !match->data) {
		dev_err(dev, "missing pmu data\n");
		return -EINVAL;
	}

	pmu_info = match->data;

	pmu = devm_kzalloc(dev,
			   sizeof(*pmu) +
				pmu_info->num_domains * sizeof(pmu->domains[0]),
			   GFP_KERNEL);
	if (!pmu)
		return -ENOMEM;

	pmu->dev = &pdev->dev;
	mutex_init(&pmu->mutex);

	pmu->info = pmu_info;

	pmu->genpd_data.domains = pmu->domains;
	pmu->genpd_data.num_domains = pmu_info->num_domains;

	parent = dev->parent;
	if (!parent) {
		dev_err(dev, "no parent for syscon devices\n");
		return -ENODEV;
	}

	pmu->regmap = syscon_node_to_regmap(parent->of_node);
	if (IS_ERR(pmu->regmap)) {
		dev_err(dev, "no regmap available\n");
		return PTR_ERR(pmu->regmap);
	}

	/*
	 * Configure power up and down transition delays for CORE
	 * and GPU domains.
	 */
	rockchip_configure_pd_cnt(pmu, pmu_info->core_pwrcnt_offset,
				  pmu_info->core_power_transition_time);
	rockchip_configure_pd_cnt(pmu, pmu_info->gpu_pwrcnt_offset,
				  pmu_info->gpu_power_transition_time);

	error = -ENODEV;

	for_each_available_child_of_node(np, node) {
		error = rockchip_pm_add_one_domain(pmu, node);
		if (error) {
			dev_err(dev, "failed to handle node %s: %d\n",
				node->name, error);
			of_node_put(node);
			goto err_out;
		}

		error = rockchip_pm_add_subdomain(pmu, node);
		if (error < 0) {
			dev_err(dev, "failed to handle subdomain node %s: %d\n",
				node->name, error);
			of_node_put(node);
			goto err_out;
		}
	}

	if (error) {
		dev_dbg(dev, "no power domains defined\n");
		goto err_out;
	}

	of_genpd_add_provider_onecell(np, &pmu->genpd_data);

	return 0;

err_out:
	rockchip_pm_domain_cleanup(pmu);
	return error;
}

static const struct rockchip_domain_info rk3288_pm_domains[] = {
	[RK3288_PD_VIO]		= DOMAIN_RK3288(7, 7, 4, false),
	[RK3288_PD_HEVC]	= DOMAIN_RK3288(14, 10, 9, false),
	[RK3288_PD_VIDEO]	= DOMAIN_RK3288(8, 8, 3, false),
	[RK3288_PD_GPU]		= DOMAIN_RK3288(9, 9, 2, false),
};

static const struct rockchip_domain_info rk3368_pm_domains[] = {
	[RK3368_PD_PERI]	= DOMAIN_RK3368(13, 12, 6, true),
	[RK3368_PD_VIO]		= DOMAIN_RK3368(15, 14, 8, false),
	[RK3368_PD_VIDEO]	= DOMAIN_RK3368(14, 13, 7, false),
	[RK3368_PD_GPU_0]	= DOMAIN_RK3368(16, 15, 2, false),
	[RK3368_PD_GPU_1]	= DOMAIN_RK3368(17, 16, 2, false),
};

static const struct rockchip_domain_info rk3399_pm_domains[] = {
	[RK3399_PD_TCPD0]	= DOMAIN_RK3399(8, 8, -1, false),
	[RK3399_PD_TCPD1]	= DOMAIN_RK3399(9, 9, -1, false),
	[RK3399_PD_CCI]		= DOMAIN_RK3399(10, 10, -1, true),
	[RK3399_PD_CCI0]	= DOMAIN_RK3399(-1, -1, 15, true),
	[RK3399_PD_CCI1]	= DOMAIN_RK3399(-1, -1, 16, true),
	[RK3399_PD_PERILP]	= DOMAIN_RK3399(11, 11, 1, true),
	[RK3399_PD_PERIHP]	= DOMAIN_RK3399(12, 12, 2, true),
	[RK3399_PD_CENTER]	= DOMAIN_RK3399(13, 13, 14, true),
	[RK3399_PD_VIO]		= DOMAIN_RK3399(14, 14, 17, false),
	[RK3399_PD_GPU]		= DOMAIN_RK3399(15, 15, 0, false),
	[RK3399_PD_VCODEC]	= DOMAIN_RK3399(16, 16, 3, false),
	[RK3399_PD_VDU]		= DOMAIN_RK3399(17, 17, 4, false),
	[RK3399_PD_RGA]		= DOMAIN_RK3399(18, 18, 5, false),
	[RK3399_PD_IEP]		= DOMAIN_RK3399(19, 19, 6, false),
	[RK3399_PD_VO]		= DOMAIN_RK3399(20, 20, -1, false),
	[RK3399_PD_VOPB]	= DOMAIN_RK3399(-1, -1, 7, false),
	[RK3399_PD_VOPL]	= DOMAIN_RK3399(-1, -1, 8, false),
	[RK3399_PD_ISP0]	= DOMAIN_RK3399(22, 22, 9, false),
	[RK3399_PD_ISP1]	= DOMAIN_RK3399(23, 23, 10, false),
	[RK3399_PD_HDCP]	= DOMAIN_RK3399(24, 24, 11, false),
	[RK3399_PD_GMAC]	= DOMAIN_RK3399(25, 25, 23, true),
	[RK3399_PD_EMMC]	= DOMAIN_RK3399(26, 26, 24, true),
	[RK3399_PD_USB3]	= DOMAIN_RK3399(27, 27, 12, true),
	[RK3399_PD_EDP]		= DOMAIN_RK3399(28, 28, 22, false),
	[RK3399_PD_GIC]		= DOMAIN_RK3399(29, 29, 27, true),
	[RK3399_PD_SD]		= DOMAIN_RK3399(30, 30, 28, true),
	[RK3399_PD_SDIOAUDIO]	= DOMAIN_RK3399(31, 31, 29, true),
};

static const struct rockchip_pmu_info rk3288_pmu = {
	.pwr_offset = 0x08,
	.status_offset = 0x0c,
	.req_offset = 0x10,
	.idle_offset = 0x14,
	.ack_offset = 0x14,

	.core_pwrcnt_offset = 0x34,
	.gpu_pwrcnt_offset = 0x3c,

	.core_power_transition_time = 24, /* 1us */
	.gpu_power_transition_time = 24, /* 1us */

	.num_domains = ARRAY_SIZE(rk3288_pm_domains),
	.domain_info = rk3288_pm_domains,
};

static const struct rockchip_pmu_info rk3368_pmu = {
	.pwr_offset = 0x0c,
	.status_offset = 0x10,
	.req_offset = 0x3c,
	.idle_offset = 0x40,
	.ack_offset = 0x40,

	.core_pwrcnt_offset = 0x48,
	.gpu_pwrcnt_offset = 0x50,

	.core_power_transition_time = 24,
	.gpu_power_transition_time = 24,

	.num_domains = ARRAY_SIZE(rk3368_pm_domains),
	.domain_info = rk3368_pm_domains,
};

static const struct rockchip_pmu_info rk3399_pmu = {
	.pwr_offset = 0x14,
	.status_offset = 0x18,
	.req_offset = 0x60,
	.idle_offset = 0x64,
	.ack_offset = 0x68,

	.core_pwrcnt_offset = 0x9c,
	.gpu_pwrcnt_offset = 0xa4,

	.core_power_transition_time = 24,
	.gpu_power_transition_time = 24,

	.num_domains = ARRAY_SIZE(rk3399_pm_domains),
	.domain_info = rk3399_pm_domains,
};

static const struct of_device_id rockchip_pm_domain_dt_match[] = {
	{
		.compatible = "rockchip,rk3288-power-controller",
		.data = (void *)&rk3288_pmu,
	},
	{
		.compatible = "rockchip,rk3368-power-controller",
		.data = (void *)&rk3368_pmu,
	},
	{
		.compatible = "rockchip,rk3399-power-controller",
		.data = (void *)&rk3399_pmu,
	},
	{ /* sentinel */ },
};

static struct platform_driver rockchip_pm_domain_driver = {
	.probe = rockchip_pm_domain_probe,
	.driver = {
		.name   = "rockchip-pm-domain",
		.of_match_table = rockchip_pm_domain_dt_match,
		/*
		 * We can't forcibly eject devices form power domain,
		 * so we can't really remove power domains once they
		 * were added.
		 */
		.suppress_bind_attrs = true,
	},
};

static int __init rockchip_pm_domain_drv_register(void)
{
	return platform_driver_register(&rockchip_pm_domain_driver);
}
postcore_initcall(rockchip_pm_domain_drv_register);
