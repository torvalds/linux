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
#include <linux/iopoll.h>
#include <linux/err.h>
#include <linux/pm_clock.h>
#include <linux/pm_domain.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/clk.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/regulator/consumer.h>
#include <soc/rockchip/pm_domains.h>
#include <soc/rockchip/rockchip_dmc.h>
#include <dt-bindings/power/px30-power.h>
#include <dt-bindings/power/rk1808-power.h>
#include <dt-bindings/power/rk3036-power.h>
#include <dt-bindings/power/rk3128-power.h>
#include <dt-bindings/power/rk3228-power.h>
#include <dt-bindings/power/rk3288-power.h>
#include <dt-bindings/power/rk3328-power.h>
#include <dt-bindings/power/rk3366-power.h>
#include <dt-bindings/power/rk3368-power.h>
#include <dt-bindings/power/rk3399-power.h>

struct rockchip_domain_info {
	int pwr_mask;
	int status_mask;
	int req_mask;
	int idle_mask;
	int ack_mask;
	bool active_wakeup;
	int pwr_w_mask;
	int req_w_mask;
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
	bool is_ignore_pwr;
	bool is_qos_saved;
	struct regulator *supply;
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

static void rockchip_pmu_lock(struct rockchip_pm_domain *pd)
{
	mutex_lock(&pd->pmu->mutex);
	rockchip_dmcfreq_lock();
}

static void rockchip_pmu_unlock(struct rockchip_pm_domain *pd)
{
	rockchip_dmcfreq_unlock();
	mutex_unlock(&pd->pmu->mutex);
}

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

#define DOMAIN_M(pwr, status, req, idle, ack, wakeup)	\
{							\
	.pwr_w_mask = (pwr >= 0) ? BIT(pwr + 16) : 0,	\
	.pwr_mask = (pwr >= 0) ? BIT(pwr) : 0,		\
	.status_mask = (status >= 0) ? BIT(status) : 0,	\
	.req_w_mask = (req >= 0) ?  BIT(req + 16) : 0,	\
	.req_mask = (req >= 0) ?  BIT(req) : 0,		\
	.idle_mask = (idle >= 0) ? BIT(idle) : 0,	\
	.ack_mask = (ack >= 0) ? BIT(ack) : 0,		\
	.active_wakeup = wakeup,			\
}

#define DOMAIN_RK3036(req, ack, idle, wakeup)		\
{							\
	.req_mask = (req >= 0) ? BIT(req) : 0,		\
	.req_w_mask = (req >= 0) ?  BIT(req + 16) : 0,	\
	.ack_mask = (ack >= 0) ? BIT(ack) : 0,		\
	.idle_mask = (idle >= 0) ? BIT(idle) : 0,	\
	.active_wakeup = wakeup,			\
}

#define DOMAIN_PX30(pwr, status, req, wakeup)		\
	DOMAIN_M(pwr, status, req, (req) + 16, req, wakeup)

#define DOMAIN_RK3288(pwr, status, req, wakeup)		\
	DOMAIN(pwr, status, req, req, (req) + 16, wakeup)

#define DOMAIN_RK3328(pwr, status, req, wakeup)		\
	DOMAIN_M(pwr, pwr, req, (req) + 10, req, wakeup)

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

static unsigned int rockchip_pmu_read_ack(struct rockchip_pmu *pmu)
{
	unsigned int val;

	regmap_read(pmu->regmap, pmu->info->ack_offset, &val);
	return val;
}

static int rockchip_pmu_set_idle_request(struct rockchip_pm_domain *pd,
					 bool idle)
{
	const struct rockchip_domain_info *pd_info = pd->info;
	struct generic_pm_domain *genpd = &pd->genpd;
	struct rockchip_pmu *pmu = pd->pmu;
	unsigned int target_ack;
	unsigned int val;
	bool is_idle;
	int ret = 0;

	if (pd_info->req_mask == 0)
		return 0;
	else if (pd_info->req_w_mask)
		regmap_write(pmu->regmap, pmu->info->req_offset,
			     idle ? (pd_info->req_mask | pd_info->req_w_mask) :
			     pd_info->req_w_mask);
	else
		regmap_update_bits(pmu->regmap, pmu->info->req_offset,
				   pd_info->req_mask, idle ? -1U : 0);

	dsb(sy);

	/* Wait util idle_ack = 1 */
	target_ack = idle ? pd_info->ack_mask : 0;
	ret = readx_poll_timeout_atomic(rockchip_pmu_read_ack, pmu, val,
					(val & pd_info->ack_mask) == target_ack,
					0, 10000);
	if (ret) {
		dev_err(pmu->dev,
			"failed to get ack on domain '%s', target_idle = %d, target_ack = %d, val=0x%x\n",
			genpd->name, idle, target_ack, val);
		goto error;
	}

	ret = readx_poll_timeout_atomic(rockchip_pmu_domain_is_idle, pd,
					is_idle, is_idle == idle, 0, 10000);
	if (ret) {
		dev_err(pmu->dev,
			"failed to set idle on domain '%s',  target_idle = %d, val=%d\n",
			genpd->name, idle, is_idle);
		goto error;
	}

	return ret;
error:
	panic("panic_on_set_idle set ...\n");
	return ret;
}

int rockchip_pmu_idle_request(struct device *dev, bool idle)
{
	struct generic_pm_domain *genpd;
	struct rockchip_pm_domain *pd;
	int ret;

	if (IS_ERR_OR_NULL(dev))
		return -EINVAL;

	if (IS_ERR_OR_NULL(dev->pm_domain))
		return -EINVAL;

	genpd = pd_to_genpd(dev->pm_domain);
	pd = to_rockchip_pd(genpd);

	rockchip_pmu_lock(pd);
	ret = rockchip_pmu_set_idle_request(pd, idle);
	rockchip_pmu_unlock(pd);

	return ret;
}
EXPORT_SYMBOL(rockchip_pmu_idle_request);

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

int rockchip_save_qos(struct device *dev)
{
	struct generic_pm_domain *genpd;
	struct rockchip_pm_domain *pd;
	int ret;

	if (IS_ERR_OR_NULL(dev))
		return -EINVAL;

	if (IS_ERR_OR_NULL(dev->pm_domain))
		return -EINVAL;

	genpd = pd_to_genpd(dev->pm_domain);
	pd = to_rockchip_pd(genpd);

	rockchip_pmu_lock(pd);
	ret = rockchip_pmu_save_qos(pd);
	rockchip_pmu_unlock(pd);

	return ret;
}
EXPORT_SYMBOL(rockchip_save_qos);

int rockchip_restore_qos(struct device *dev)
{
	struct generic_pm_domain *genpd;
	struct rockchip_pm_domain *pd;
	int ret;

	if (IS_ERR_OR_NULL(dev))
		return -EINVAL;

	if (IS_ERR_OR_NULL(dev->pm_domain))
		return -EINVAL;

	genpd = pd_to_genpd(dev->pm_domain);
	pd = to_rockchip_pd(genpd);

	rockchip_pmu_lock(pd);
	ret = rockchip_pmu_restore_qos(pd);
	rockchip_pmu_unlock(pd);

	return ret;
}
EXPORT_SYMBOL(rockchip_restore_qos);

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

static int rockchip_do_pmu_set_power_domain(struct rockchip_pm_domain *pd,
					    bool on)
{
	struct rockchip_pmu *pmu = pd->pmu;
	struct generic_pm_domain *genpd = &pd->genpd;
	bool is_on;
	int ret = 0;

	if (pd->info->pwr_mask == 0)
		return 0;
	else if (pd->info->pwr_w_mask)
		regmap_write(pmu->regmap, pmu->info->pwr_offset,
			     on ? pd->info->pwr_w_mask :
			     (pd->info->pwr_mask | pd->info->pwr_w_mask));
	else
		regmap_update_bits(pmu->regmap, pmu->info->pwr_offset,
				   pd->info->pwr_mask, on ? 0 : -1U);

	dsb(sy);

	ret = readx_poll_timeout_atomic(rockchip_pmu_domain_is_on, pd, is_on,
					is_on == on, 0, 10000);
	if (ret) {
		dev_err(pmu->dev,
			"failed to set domain '%s', target_on= %d, val=%d\n",
			genpd->name, on, is_on);
			goto error;
	}
	return ret;

error:
	panic("panic_on_set_domain set ...\n");
	return ret;
}

static int rockchip_pd_power(struct rockchip_pm_domain *pd, bool power_on)
{
	int i, ret = 0;
	struct generic_pm_domain *genpd = &pd->genpd;

	rockchip_pmu_lock(pd);

	if (rockchip_pmu_domain_is_on(pd) != power_on) {
		if (IS_ERR_OR_NULL(pd->supply) &&
		    PTR_ERR(pd->supply) != -ENODEV)
			pd->supply = devm_regulator_get_optional(pd->pmu->dev,
								 genpd->name);

		if (power_on && !IS_ERR(pd->supply)) {
			ret = regulator_enable(pd->supply);
			if (ret < 0) {
				dev_err(pd->pmu->dev, "failed to set vdd supply enable '%s',\n",
					genpd->name);
				rockchip_pmu_unlock(pd);
				return ret;
			}
		}

		for (i = 0; i < pd->num_clks; i++)
			clk_enable(pd->clks[i]);

		if (!power_on) {
			rockchip_pmu_save_qos(pd);
			pd->is_qos_saved = true;

			/* if powering down, idle request to NIU first */
			ret = rockchip_pmu_set_idle_request(pd, true);
			if (ret) {
				dev_err(pd->pmu->dev, "failed to set idle request '%s',\n",
					genpd->name);
				goto out;
			}
		}

		ret = rockchip_do_pmu_set_power_domain(pd, power_on);
		if (ret) {
			dev_err(pd->pmu->dev, "failed to set power '%s' = %d,\n",
				genpd->name, power_on);
			goto out;
		}

		if (power_on) {
			/* if powering up, leave idle mode */
			ret = rockchip_pmu_set_idle_request(pd, false);
			if (ret) {
				dev_err(pd->pmu->dev, "failed to set deidle request '%s',\n",
					genpd->name);
				goto out;
			}

			if (pd->is_qos_saved)
				rockchip_pmu_restore_qos(pd);
		}
out:
		for (i = pd->num_clks - 1; i >= 0; i--)
			clk_disable(pd->clks[i]);

		if (!power_on && !IS_ERR(pd->supply))
			ret = regulator_disable(pd->supply);
	}

	rockchip_pmu_unlock(pd);
	return ret;
}

static int rockchip_pd_power_on(struct generic_pm_domain *domain)
{
	struct rockchip_pm_domain *pd = to_rockchip_pd(domain);

	if (pd->is_ignore_pwr)
		return 0;

	return rockchip_pd_power(pd, true);
}

static int rockchip_pd_power_off(struct generic_pm_domain *domain)
{
	struct rockchip_pm_domain *pd = to_rockchip_pd(domain);

	if (pd->is_ignore_pwr)
		return 0;

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
	int clk_cnt, num_qos = 0, num_qos_reg = 0;
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
	if (!pd_info->pwr_mask)
		pd->is_ignore_pwr = true;

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

	num_qos = of_count_phandle_with_args(node, "pm_qos", NULL);

	for (j = 0; j < num_qos; j++) {
		qos_node = of_parse_phandle(node, "pm_qos", j);
		if (qos_node && of_device_is_available(qos_node))
			pd->num_qos++;
		of_node_put(qos_node);
	}

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

		for (j = 0; j < num_qos; j++) {
			qos_node = of_parse_phandle(node, "pm_qos", j);
			if (!qos_node) {
				error = -ENODEV;
				goto err_out;
			}
			if (of_device_is_available(qos_node)) {
				pd->qos_regmap[num_qos_reg] =
					syscon_node_to_regmap(qos_node);
				if (IS_ERR(pd->qos_regmap[num_qos_reg])) {
					error = -ENODEV;
					of_node_put(qos_node);
					goto err_out;
				}
				num_qos_reg++;
			}
			of_node_put(qos_node);
			if (num_qos_reg > pd->num_qos)
				goto err_out;
		}
	}

	pd->genpd.name = node->name;
	pd->genpd.power_off = rockchip_pd_power_off;
	pd->genpd.power_on = rockchip_pd_power_on;
	pd->genpd.attach_dev = rockchip_pd_attach_dev;
	pd->genpd.detach_dev = rockchip_pd_detach_dev;
	pd->genpd.dev_ops.active_wakeup = rockchip_active_wakeup;
	pd->genpd.flags = GENPD_FLAG_PM_CLK;
	pm_genpd_init(&pd->genpd, NULL, !rockchip_pmu_domain_is_on(pd));

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
	rockchip_pmu_lock(pd);
	pd->num_clks = 0;
	rockchip_pmu_unlock(pd);

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
	struct rockchip_pm_domain *child_pd, *parent_pd;
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

		/*
		 * If child_pd doesn't do idle request or power on/off,
		 * parent_pd may fail to do power on/off, so if parent_pd
		 * need to power on/off, child_pd can't ignore to do idle
		 * request and power on/off.
		 */
		child_pd = to_rockchip_pd(child_domain);
		parent_pd = to_rockchip_pd(parent_domain);
		if (!parent_pd->is_ignore_pwr)
			child_pd->is_ignore_pwr = false;

		rockchip_pm_add_subdomain(pmu, np);
	}

	return 0;

err_out:
	of_node_put(np);
	return error;
}

static void __iomem *pd_base;

void rockchip_dump_pmu(void)
{
	if (pd_base) {
		pr_warn("PMU:\n");
		print_hex_dump(KERN_WARNING, "", DUMP_PREFIX_OFFSET,
			       32, 4, pd_base,
			       0x100, false);
	}
}
EXPORT_SYMBOL_GPL(rockchip_dump_pmu);

static int rockchip_pmu_panic(struct notifier_block *this,
			     unsigned long ev, void *ptr)
{
	rockchip_dump_pmu();
	return NOTIFY_DONE;
}

static struct notifier_block pmu_panic_block = {
	.notifier_call = rockchip_pmu_panic,
};

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
	void __iomem *reg_base;

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

	reg_base = of_iomap(parent->of_node, 0);
	if (!reg_base) {
		dev_err(dev, "%s: could not map pmu region\n", __func__);
		return -ENOMEM;
	}

	pd_base = reg_base;

	/*
	 * Configure power up and down transition delays for CORE
	 * and GPU domains.
	 */
	if (pmu_info->core_pwrcnt_offset)
		rockchip_configure_pd_cnt(pmu, pmu_info->core_pwrcnt_offset,
					  pmu_info->core_power_transition_time);
	if (pmu_info->gpu_pwrcnt_offset)
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

	atomic_notifier_chain_register(&panic_notifier_list,
				       &pmu_panic_block);

	return 0;

err_out:
	rockchip_pm_domain_cleanup(pmu);
	return error;
}

static const struct rockchip_domain_info px30_pm_domains[] = {
	[PX30_PD_USB]		= DOMAIN_PX30(5, 5, 10, false),
	[PX30_PD_SDCARD]	= DOMAIN_PX30(8, 8, 9, false),
	[PX30_PD_GMAC]		= DOMAIN_PX30(10, 10, 6, false),
	[PX30_PD_MMC_NAND]	= DOMAIN_PX30(11, 11, 5, false),
	[PX30_PD_VPU]		= DOMAIN_PX30(12, 12, 14, false),
	[PX30_PD_VO]		= DOMAIN_PX30(13, 13, 7, false),
	[PX30_PD_VI]		= DOMAIN_PX30(14, 14, 8, false),
	[PX30_PD_GPU]		= DOMAIN_PX30(15, 15, 2, false),
};

static const struct rockchip_domain_info rk1808_pm_domains[] = {
	[RK1808_VD_NPU]		= DOMAIN_PX30(15, 15, 2, false),
	[RK1808_PD_PCIE]	= DOMAIN_PX30(9, 9, 4, true),
	[RK1808_PD_VPU]		= DOMAIN_PX30(13, 13, 7, false),
	[RK1808_PD_VIO]		= DOMAIN_PX30(14, 14, 8, false),
};

static const struct rockchip_domain_info rk3036_pm_domains[] = {
	[RK3036_PD_MSCH]	= DOMAIN_RK3036(14, 23, 30, true),
	[RK3036_PD_CORE]	= DOMAIN_RK3036(13, 17, 24, false),
	[RK3036_PD_PERI]	= DOMAIN_RK3036(12, 18, 25, false),
	[RK3036_PD_VIO]		= DOMAIN_RK3036(11, 19, 26, false),
	[RK3036_PD_VPU]		= DOMAIN_RK3036(10, 20, 27, false),
	[RK3036_PD_GPU]		= DOMAIN_RK3036(9, 21, 28, false),
	[RK3036_PD_SYS]		= DOMAIN_RK3036(8, 22, 29, false),
};

static const struct rockchip_domain_info rk3128_pm_domains[] = {
	[RK3128_PD_CORE]	= DOMAIN_RK3288(0, 0, 4, false),
	[RK3128_PD_MSCH]	= DOMAIN_RK3288(-1, -1, 6, true),
	[RK3128_PD_VIO]		= DOMAIN_RK3288(3, 3, 2, false),
	[RK3128_PD_VIDEO]	= DOMAIN_RK3288(2, 2, 1, false),
	[RK3128_PD_GPU]		= DOMAIN_RK3288(1, 1, 3, false),
};

static const struct rockchip_domain_info rk3228_pm_domains[] = {
	[RK3228_PD_CORE]	= DOMAIN_RK3036(0, 0, 16, true),
	[RK3228_PD_MSCH]	= DOMAIN_RK3036(1, 1, 17, true),
	[RK3228_PD_BUS]		= DOMAIN_RK3036(2, 2, 18, true),
	[RK3228_PD_SYS]		= DOMAIN_RK3036(3, 3, 19, true),
	[RK3228_PD_VIO]		= DOMAIN_RK3036(4, 4, 20, false),
	[RK3228_PD_VOP]		= DOMAIN_RK3036(5, 5, 21, false),
	[RK3228_PD_VPU]		= DOMAIN_RK3036(6, 6, 22, false),
	[RK3228_PD_RKVDEC]	= DOMAIN_RK3036(7, 7, 23, false),
	[RK3228_PD_GPU]		= DOMAIN_RK3036(8, 8, 24, false),
	[RK3228_PD_PERI]	= DOMAIN_RK3036(9, 9, 25, true),
	[RK3228_PD_GMAC]	= DOMAIN_RK3036(10, 10, 26, false),
};

static const struct rockchip_domain_info rk3288_pm_domains[] = {
	[RK3288_PD_VIO]		= DOMAIN_RK3288(7, 7, 4, false),
	[RK3288_PD_HEVC]	= DOMAIN_RK3288(14, 10, 9, false),
	[RK3288_PD_VIDEO]	= DOMAIN_RK3288(8, 8, 3, false),
	[RK3288_PD_GPU]		= DOMAIN_RK3288(9, 9, 2, false),
};

static const struct rockchip_domain_info rk3328_pm_domains[] = {
	[RK3328_PD_CORE]	= DOMAIN_RK3328(-1, 0, 0, false),
	[RK3328_PD_GPU]		= DOMAIN_RK3328(-1, 1, 1, false),
	[RK3328_PD_BUS]		= DOMAIN_RK3328(-1, 2, 2, true),
	[RK3328_PD_MSCH]	= DOMAIN_RK3328(-1, 3, 3, true),
	[RK3328_PD_PERI]	= DOMAIN_RK3328(-1, 4, 4, true),
	[RK3328_PD_VIDEO]	= DOMAIN_RK3328(-1, 5, 5, false),
	[RK3328_PD_HEVC]	= DOMAIN_RK3328(-1, 6, 6, false),
	[RK3328_PD_VIO]		= DOMAIN_RK3328(-1, 8, 8, false),
	[RK3328_PD_VPU]		= DOMAIN_RK3328(-1, 9, 9, false),
};

static const struct rockchip_domain_info rk3366_pm_domains[] = {
	[RK3366_PD_PERI]	= DOMAIN_RK3368(10, 10, 6, true),
	[RK3366_PD_VIO]		= DOMAIN_RK3368(14, 14, 8, false),
	[RK3366_PD_VIDEO]	= DOMAIN_RK3368(13, 13, 7, false),
	[RK3366_PD_RKVDEC]	= DOMAIN_RK3368(11, 11, -1, false),
	[RK3366_PD_WIFIBT]	= DOMAIN_RK3368(8, 8, 9, false),
	[RK3366_PD_VPU]		= DOMAIN_RK3368(12, 12, -1, false),
	[RK3366_PD_GPU]		= DOMAIN_RK3368(15, 15, 2, false),
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

static const struct rockchip_pmu_info px30_pmu = {
	.pwr_offset = 0x18,
	.status_offset = 0x20,
	.req_offset = 0x64,
	.idle_offset = 0x6c,
	.ack_offset = 0x6c,

	.num_domains = ARRAY_SIZE(px30_pm_domains),
	.domain_info = px30_pm_domains,
};

static const struct rockchip_pmu_info rk1808_pmu = {
	.pwr_offset = 0x18,
	.status_offset = 0x20,
	.req_offset = 0x64,
	.idle_offset = 0x6c,
	.ack_offset = 0x6c,

	.num_domains = ARRAY_SIZE(rk1808_pm_domains),
	.domain_info = rk1808_pm_domains,
};

static const struct rockchip_pmu_info rk3036_pmu = {
	.req_offset = 0x148,
	.idle_offset = 0x14c,
	.ack_offset = 0x14c,

	.num_domains = ARRAY_SIZE(rk3036_pm_domains),
	.domain_info = rk3036_pm_domains,
};

static const struct rockchip_pmu_info rk3128_pmu = {
	.pwr_offset = 0x04,
	.status_offset = 0x08,
	.req_offset = 0x0c,
	.idle_offset = 0x10,
	.ack_offset = 0x10,

	.num_domains = ARRAY_SIZE(rk3128_pm_domains),
	.domain_info = rk3128_pm_domains,
};

static const struct rockchip_pmu_info rk3228_pmu = {
	.req_offset = 0x40c,
	.idle_offset = 0x488,
	.ack_offset = 0x488,

	.num_domains = ARRAY_SIZE(rk3228_pm_domains),
	.domain_info = rk3228_pm_domains,
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

static const struct rockchip_pmu_info rk3328_pmu = {
	.req_offset = 0x414,
	.idle_offset = 0x484,
	.ack_offset = 0x484,

	.num_domains = ARRAY_SIZE(rk3328_pm_domains),
	.domain_info = rk3328_pm_domains,
};

static const struct rockchip_pmu_info rk3366_pmu = {
	.pwr_offset = 0x0c,
	.status_offset = 0x10,
	.req_offset = 0x3c,
	.idle_offset = 0x40,
	.ack_offset = 0x40,

	.core_pwrcnt_offset = 0x48,
	.gpu_pwrcnt_offset = 0x50,

	.core_power_transition_time = 24,
	.gpu_power_transition_time = 24,

	.num_domains = ARRAY_SIZE(rk3366_pm_domains),
	.domain_info = rk3366_pm_domains,
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

	.core_pwrcnt_offset = 0xac,
	.gpu_pwrcnt_offset = 0xac,

	.core_power_transition_time = 6, /* 0.25us */
	.gpu_power_transition_time = 6, /* 0.25us */

	.num_domains = ARRAY_SIZE(rk3399_pm_domains),
	.domain_info = rk3399_pm_domains,
};

static const struct of_device_id rockchip_pm_domain_dt_match[] = {
	{
		.compatible = "rockchip,px30-power-controller",
		.data = (void *)&px30_pmu,
	},
	{
		.compatible = "rockchip,rk1808-power-controller",
		.data = (void *)&rk1808_pmu,
	},
	{
		.compatible = "rockchip,rk3036-power-controller",
		.data = (void *)&rk3036_pmu,
	},
	{
		.compatible = "rockchip,rk3128-power-controller",
		.data = (void *)&rk3128_pmu,
	},
	{
		.compatible = "rockchip,rk3228-power-controller",
		.data = (void *)&rk3228_pmu,
	},
	{
		.compatible = "rockchip,rk3288-power-controller",
		.data = (void *)&rk3288_pmu,
	},
	{
		.compatible = "rockchip,rk3328-power-controller",
		.data = (void *)&rk3328_pmu,
	},
	{
		.compatible = "rockchip,rk3366-power-controller",
		.data = (void *)&rk3366_pmu,
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
