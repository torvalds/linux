// SPDX-License-Identifier: GPL-2.0-only
/*
 * Rockchip Generic power domain support.
 *
 * Copyright (c) 2015 ROCKCHIP, Co. Ltd.
 */

#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/pm_clock.h>
#include <linux/pm_domain.h>
#include <linux/of_address.h>
#include <linux/of_clk.h>
#include <linux/of_platform.h>
#include <linux/clk.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <soc/rockchip/pm_domains.h>
#include <dt-bindings/power/px30-power.h>
#include <dt-bindings/power/rockchip,rv1126-power.h>
#include <dt-bindings/power/rk3036-power.h>
#include <dt-bindings/power/rk3066-power.h>
#include <dt-bindings/power/rk3128-power.h>
#include <dt-bindings/power/rk3188-power.h>
#include <dt-bindings/power/rk3228-power.h>
#include <dt-bindings/power/rk3288-power.h>
#include <dt-bindings/power/rk3328-power.h>
#include <dt-bindings/power/rk3366-power.h>
#include <dt-bindings/power/rk3368-power.h>
#include <dt-bindings/power/rk3399-power.h>
#include <dt-bindings/power/rk3568-power.h>
#include <dt-bindings/power/rk3588-power.h>

struct rockchip_domain_info {
	const char *name;
	int pwr_mask;
	int status_mask;
	int req_mask;
	int idle_mask;
	int ack_mask;
	bool active_wakeup;
	int pwr_w_mask;
	int req_w_mask;
	int mem_status_mask;
	int repair_status_mask;
	u32 pwr_offset;
	u32 mem_offset;
	u32 req_offset;
};

struct rockchip_pmu_info {
	u32 pwr_offset;
	u32 status_offset;
	u32 req_offset;
	u32 idle_offset;
	u32 ack_offset;
	u32 mem_pwr_offset;
	u32 chain_status_offset;
	u32 mem_status_offset;
	u32 repair_status_offset;

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
	struct clk_bulk_data *clks;
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

#define DOMAIN(_name, pwr, status, req, idle, ack, wakeup)	\
{							\
	.name = _name,				\
	.pwr_mask = (pwr),				\
	.status_mask = (status),			\
	.req_mask = (req),				\
	.idle_mask = (idle),				\
	.ack_mask = (ack),				\
	.active_wakeup = (wakeup),			\
}

#define DOMAIN_M(_name, pwr, status, req, idle, ack, wakeup)	\
{							\
	.name = _name,				\
	.pwr_w_mask = (pwr) << 16,			\
	.pwr_mask = (pwr),				\
	.status_mask = (status),			\
	.req_w_mask = (req) << 16,			\
	.req_mask = (req),				\
	.idle_mask = (idle),				\
	.ack_mask = (ack),				\
	.active_wakeup = wakeup,			\
}

#define DOMAIN_M_O_R(_name, p_offset, pwr, status, m_offset, m_status, r_status, r_offset, req, idle, ack, wakeup)	\
{							\
	.name = _name,					\
	.pwr_offset = p_offset,				\
	.pwr_w_mask = (pwr) << 16,			\
	.pwr_mask = (pwr),				\
	.status_mask = (status),			\
	.mem_offset = m_offset,				\
	.mem_status_mask = (m_status),			\
	.repair_status_mask = (r_status),		\
	.req_offset = r_offset,				\
	.req_w_mask = (req) << 16,			\
	.req_mask = (req),				\
	.idle_mask = (idle),				\
	.ack_mask = (ack),				\
	.active_wakeup = wakeup,			\
}

#define DOMAIN_RK3036(_name, req, ack, idle, wakeup)		\
{							\
	.name = _name,				\
	.req_mask = (req),				\
	.req_w_mask = (req) << 16,			\
	.ack_mask = (ack),				\
	.idle_mask = (idle),				\
	.active_wakeup = wakeup,			\
}

#define DOMAIN_PX30(name, pwr, status, req, wakeup)		\
	DOMAIN_M(name, pwr, status, req, (req) << 16, req, wakeup)

#define DOMAIN_RV1126(name, pwr, req, idle, wakeup)		\
	DOMAIN_M(name, pwr, pwr, req, idle, idle, wakeup)

#define DOMAIN_RK3288(name, pwr, status, req, wakeup)		\
	DOMAIN(name, pwr, status, req, req, (req) << 16, wakeup)

#define DOMAIN_RK3328(name, pwr, status, req, wakeup)		\
	DOMAIN_M(name, pwr, pwr, req, (req) << 10, req, wakeup)

#define DOMAIN_RK3368(name, pwr, status, req, wakeup)		\
	DOMAIN(name, pwr, status, req, (req) << 16, req, wakeup)

#define DOMAIN_RK3399(name, pwr, status, req, wakeup)		\
	DOMAIN(name, pwr, status, req, req, req, wakeup)

#define DOMAIN_RK3568(name, pwr, req, wakeup)		\
	DOMAIN_M(name, pwr, pwr, req, req, req, wakeup)

/*
 * Dynamic Memory Controller may need to coordinate with us -- see
 * rockchip_pmu_block().
 *
 * dmc_pmu_mutex protects registration-time races, so DMC driver doesn't try to
 * block() while we're initializing the PMU.
 */
static DEFINE_MUTEX(dmc_pmu_mutex);
static struct rockchip_pmu *dmc_pmu;

/*
 * Block PMU transitions and make sure they don't interfere with ARM Trusted
 * Firmware operations. There are two conflicts, noted in the comments below.
 *
 * Caller must unblock PMU transitions via rockchip_pmu_unblock().
 */
int rockchip_pmu_block(void)
{
	struct rockchip_pmu *pmu;
	struct generic_pm_domain *genpd;
	struct rockchip_pm_domain *pd;
	int i, ret;

	mutex_lock(&dmc_pmu_mutex);

	/* No PMU (yet)? Then we just block rockchip_pmu_probe(). */
	if (!dmc_pmu)
		return 0;
	pmu = dmc_pmu;

	/*
	 * mutex blocks all idle transitions: we can't touch the
	 * PMU_BUS_IDLE_REQ (our ".idle_offset") register while ARM Trusted
	 * Firmware might be using it.
	 */
	mutex_lock(&pmu->mutex);

	/*
	 * Power domain clocks: Per Rockchip, we *must* keep certain clocks
	 * enabled for the duration of power-domain transitions. Most
	 * transitions are handled by this driver, but some cases (in
	 * particular, DRAM DVFS / memory-controller idle) must be handled by
	 * firmware. Firmware can handle most clock management via a special
	 * "ungate" register (PMU_CRU_GATEDIS_CON0), but unfortunately, this
	 * doesn't handle PLLs. We can assist this transition by doing the
	 * clock management on behalf of firmware.
	 */
	for (i = 0; i < pmu->genpd_data.num_domains; i++) {
		genpd = pmu->genpd_data.domains[i];
		if (genpd) {
			pd = to_rockchip_pd(genpd);
			ret = clk_bulk_enable(pd->num_clks, pd->clks);
			if (ret < 0) {
				dev_err(pmu->dev,
					"failed to enable clks for domain '%s': %d\n",
					genpd->name, ret);
				goto err;
			}
		}
	}

	return 0;

err:
	for (i = i - 1; i >= 0; i--) {
		genpd = pmu->genpd_data.domains[i];
		if (genpd) {
			pd = to_rockchip_pd(genpd);
			clk_bulk_disable(pd->num_clks, pd->clks);
		}
	}
	mutex_unlock(&pmu->mutex);
	mutex_unlock(&dmc_pmu_mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(rockchip_pmu_block);

/* Unblock PMU transitions. */
void rockchip_pmu_unblock(void)
{
	struct rockchip_pmu *pmu;
	struct generic_pm_domain *genpd;
	struct rockchip_pm_domain *pd;
	int i;

	if (dmc_pmu) {
		pmu = dmc_pmu;
		for (i = 0; i < pmu->genpd_data.num_domains; i++) {
			genpd = pmu->genpd_data.domains[i];
			if (genpd) {
				pd = to_rockchip_pd(genpd);
				clk_bulk_disable(pd->num_clks, pd->clks);
			}
		}

		mutex_unlock(&pmu->mutex);
	}

	mutex_unlock(&dmc_pmu_mutex);
}
EXPORT_SYMBOL_GPL(rockchip_pmu_unblock);

#define DOMAIN_RK3588(name, p_offset, pwr, status, m_offset, m_status, r_status, r_offset, req, idle, wakeup)	\
	DOMAIN_M_O_R(name, p_offset, pwr, status, m_offset, m_status, r_status, r_offset, req, idle, idle, wakeup)

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
	u32 pd_req_offset = pd_info->req_offset;
	unsigned int target_ack;
	unsigned int val;
	bool is_idle;
	int ret;

	if (pd_info->req_mask == 0)
		return 0;
	else if (pd_info->req_w_mask)
		regmap_write(pmu->regmap, pmu->info->req_offset + pd_req_offset,
			     idle ? (pd_info->req_mask | pd_info->req_w_mask) :
			     pd_info->req_w_mask);
	else
		regmap_update_bits(pmu->regmap, pmu->info->req_offset + pd_req_offset,
				   pd_info->req_mask, idle ? -1U : 0);

	wmb();

	/* Wait util idle_ack = 1 */
	target_ack = idle ? pd_info->ack_mask : 0;
	ret = readx_poll_timeout_atomic(rockchip_pmu_read_ack, pmu, val,
					(val & pd_info->ack_mask) == target_ack,
					0, 10000);
	if (ret) {
		dev_err(pmu->dev,
			"failed to get ack on domain '%s', val=0x%x\n",
			genpd->name, val);
		return ret;
	}

	ret = readx_poll_timeout_atomic(rockchip_pmu_domain_is_idle, pd,
					is_idle, is_idle == idle, 0, 10000);
	if (ret) {
		dev_err(pmu->dev,
			"failed to set idle on domain '%s', val=%d\n",
			genpd->name, is_idle);
		return ret;
	}

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

	if (pd->info->repair_status_mask) {
		regmap_read(pmu->regmap, pmu->info->repair_status_offset, &val);
		/* 1'b1: power on, 1'b0: power off */
		return val & pd->info->repair_status_mask;
	}

	/* check idle status for idle-only domains */
	if (pd->info->status_mask == 0)
		return !rockchip_pmu_domain_is_idle(pd);

	regmap_read(pmu->regmap, pmu->info->status_offset, &val);

	/* 1'b0: power on, 1'b1: power off */
	return !(val & pd->info->status_mask);
}

static bool rockchip_pmu_domain_is_mem_on(struct rockchip_pm_domain *pd)
{
	struct rockchip_pmu *pmu = pd->pmu;
	unsigned int val;

	regmap_read(pmu->regmap,
		    pmu->info->mem_status_offset + pd->info->mem_offset, &val);

	/* 1'b0: power on, 1'b1: power off */
	return !(val & pd->info->mem_status_mask);
}

static bool rockchip_pmu_domain_is_chain_on(struct rockchip_pm_domain *pd)
{
	struct rockchip_pmu *pmu = pd->pmu;
	unsigned int val;

	regmap_read(pmu->regmap,
		    pmu->info->chain_status_offset + pd->info->mem_offset, &val);

	/* 1'b1: power on, 1'b0: power off */
	return val & pd->info->mem_status_mask;
}

static int rockchip_pmu_domain_mem_reset(struct rockchip_pm_domain *pd)
{
	struct rockchip_pmu *pmu = pd->pmu;
	struct generic_pm_domain *genpd = &pd->genpd;
	bool is_on;
	int ret = 0;

	ret = readx_poll_timeout_atomic(rockchip_pmu_domain_is_chain_on, pd, is_on,
					is_on == true, 0, 10000);
	if (ret) {
		dev_err(pmu->dev,
			"failed to get chain status '%s', target_on=1, val=%d\n",
			genpd->name, is_on);
		goto error;
	}

	udelay(20);

	regmap_write(pmu->regmap, pmu->info->mem_pwr_offset + pd->info->pwr_offset,
		     (pd->info->pwr_mask | pd->info->pwr_w_mask));
	wmb();

	ret = readx_poll_timeout_atomic(rockchip_pmu_domain_is_mem_on, pd, is_on,
					is_on == false, 0, 10000);
	if (ret) {
		dev_err(pmu->dev,
			"failed to get mem status '%s', target_on=0, val=%d\n",
			genpd->name, is_on);
		goto error;
	}

	regmap_write(pmu->regmap, pmu->info->mem_pwr_offset + pd->info->pwr_offset,
		     pd->info->pwr_w_mask);
	wmb();

	ret = readx_poll_timeout_atomic(rockchip_pmu_domain_is_mem_on, pd, is_on,
					is_on == true, 0, 10000);
	if (ret) {
		dev_err(pmu->dev,
			"failed to get mem status '%s', target_on=1, val=%d\n",
			genpd->name, is_on);
	}

error:
	return ret;
}

static void rockchip_do_pmu_set_power_domain(struct rockchip_pm_domain *pd,
					     bool on)
{
	struct rockchip_pmu *pmu = pd->pmu;
	struct generic_pm_domain *genpd = &pd->genpd;
	u32 pd_pwr_offset = pd->info->pwr_offset;
	bool is_on, is_mem_on = false;

	if (pd->info->pwr_mask == 0)
		return;

	if (on && pd->info->mem_status_mask)
		is_mem_on = rockchip_pmu_domain_is_mem_on(pd);

	if (pd->info->pwr_w_mask)
		regmap_write(pmu->regmap, pmu->info->pwr_offset + pd_pwr_offset,
			     on ? pd->info->pwr_w_mask :
			     (pd->info->pwr_mask | pd->info->pwr_w_mask));
	else
		regmap_update_bits(pmu->regmap, pmu->info->pwr_offset + pd_pwr_offset,
				   pd->info->pwr_mask, on ? 0 : -1U);

	wmb();

	if (is_mem_on && rockchip_pmu_domain_mem_reset(pd))
		return;

	if (readx_poll_timeout_atomic(rockchip_pmu_domain_is_on, pd, is_on,
				      is_on == on, 0, 10000)) {
		dev_err(pmu->dev,
			"failed to set domain '%s', val=%d\n",
			genpd->name, is_on);
		return;
	}
}

static int rockchip_pd_power(struct rockchip_pm_domain *pd, bool power_on)
{
	struct rockchip_pmu *pmu = pd->pmu;
	int ret;

	mutex_lock(&pmu->mutex);

	if (rockchip_pmu_domain_is_on(pd) != power_on) {
		ret = clk_bulk_enable(pd->num_clks, pd->clks);
		if (ret < 0) {
			dev_err(pmu->dev, "failed to enable clocks\n");
			mutex_unlock(&pmu->mutex);
			return ret;
		}

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

		clk_bulk_disable(pd->num_clks, pd->clks);
	}

	mutex_unlock(&pmu->mutex);
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

static int rockchip_pm_add_one_domain(struct rockchip_pmu *pmu,
				      struct device_node *node)
{
	const struct rockchip_domain_info *pd_info;
	struct rockchip_pm_domain *pd;
	struct device_node *qos_node;
	int i, j;
	u32 id;
	int error;

	error = of_property_read_u32(node, "reg", &id);
	if (error) {
		dev_err(pmu->dev,
			"%pOFn: failed to retrieve domain id (reg): %d\n",
			node, error);
		return -EINVAL;
	}

	if (id >= pmu->info->num_domains) {
		dev_err(pmu->dev, "%pOFn: invalid domain id %d\n",
			node, id);
		return -EINVAL;
	}
	/* RK3588 has domains with two parents (RKVDEC0/RKVDEC1) */
	if (pmu->genpd_data.domains[id])
		return 0;

	pd_info = &pmu->info->domain_info[id];
	if (!pd_info) {
		dev_err(pmu->dev, "%pOFn: undefined domain id %d\n",
			node, id);
		return -EINVAL;
	}

	pd = devm_kzalloc(pmu->dev, sizeof(*pd), GFP_KERNEL);
	if (!pd)
		return -ENOMEM;

	pd->info = pd_info;
	pd->pmu = pmu;

	pd->num_clks = of_clk_get_parent_count(node);
	if (pd->num_clks > 0) {
		pd->clks = devm_kcalloc(pmu->dev, pd->num_clks,
					sizeof(*pd->clks), GFP_KERNEL);
		if (!pd->clks)
			return -ENOMEM;
	} else {
		dev_dbg(pmu->dev, "%pOFn: doesn't have clocks: %d\n",
			node, pd->num_clks);
		pd->num_clks = 0;
	}

	for (i = 0; i < pd->num_clks; i++) {
		pd->clks[i].clk = of_clk_get(node, i);
		if (IS_ERR(pd->clks[i].clk)) {
			error = PTR_ERR(pd->clks[i].clk);
			dev_err(pmu->dev,
				"%pOFn: failed to get clk at index %d: %d\n",
				node, i, error);
			return error;
		}
	}

	error = clk_bulk_prepare(pd->num_clks, pd->clks);
	if (error)
		goto err_put_clocks;

	pd->num_qos = of_count_phandle_with_args(node, "pm_qos",
						 NULL);

	if (pd->num_qos > 0) {
		pd->qos_regmap = devm_kcalloc(pmu->dev, pd->num_qos,
					      sizeof(*pd->qos_regmap),
					      GFP_KERNEL);
		if (!pd->qos_regmap) {
			error = -ENOMEM;
			goto err_unprepare_clocks;
		}

		for (j = 0; j < MAX_QOS_REGS_NUM; j++) {
			pd->qos_save_regs[j] = devm_kcalloc(pmu->dev,
							    pd->num_qos,
							    sizeof(u32),
							    GFP_KERNEL);
			if (!pd->qos_save_regs[j]) {
				error = -ENOMEM;
				goto err_unprepare_clocks;
			}
		}

		for (j = 0; j < pd->num_qos; j++) {
			qos_node = of_parse_phandle(node, "pm_qos", j);
			if (!qos_node) {
				error = -ENODEV;
				goto err_unprepare_clocks;
			}
			pd->qos_regmap[j] = syscon_node_to_regmap(qos_node);
			if (IS_ERR(pd->qos_regmap[j])) {
				error = -ENODEV;
				of_node_put(qos_node);
				goto err_unprepare_clocks;
			}
			of_node_put(qos_node);
		}
	}

	if (pd->info->name)
		pd->genpd.name = pd->info->name;
	else
		pd->genpd.name = kbasename(node->full_name);
	pd->genpd.power_off = rockchip_pd_power_off;
	pd->genpd.power_on = rockchip_pd_power_on;
	pd->genpd.attach_dev = rockchip_pd_attach_dev;
	pd->genpd.detach_dev = rockchip_pd_detach_dev;
	pd->genpd.flags = GENPD_FLAG_PM_CLK;
	if (pd_info->active_wakeup)
		pd->genpd.flags |= GENPD_FLAG_ACTIVE_WAKEUP;
	pm_genpd_init(&pd->genpd, NULL,
		      !rockchip_pmu_domain_is_on(pd) ||
		      (pd->info->mem_status_mask && !rockchip_pmu_domain_is_mem_on(pd)));

	pmu->genpd_data.domains[id] = &pd->genpd;
	return 0;

err_unprepare_clocks:
	clk_bulk_unprepare(pd->num_clks, pd->clks);
err_put_clocks:
	clk_bulk_put(pd->num_clks, pd->clks);
	return error;
}

static void rockchip_pm_remove_one_domain(struct rockchip_pm_domain *pd)
{
	int ret;

	/*
	 * We're in the error cleanup already, so we only complain,
	 * but won't emit another error on top of the original one.
	 */
	ret = pm_genpd_remove(&pd->genpd);
	if (ret < 0)
		dev_err(pd->pmu->dev, "failed to remove domain '%s' : %d - state may be inconsistent\n",
			pd->genpd.name, ret);

	clk_bulk_unprepare(pd->num_clks, pd->clks);
	clk_bulk_put(pd->num_clks, pd->clks);

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
				"%pOFn: failed to retrieve domain id (reg): %d\n",
				parent, error);
			goto err_out;
		}
		parent_domain = pmu->genpd_data.domains[idx];

		error = rockchip_pm_add_one_domain(pmu, np);
		if (error) {
			dev_err(pmu->dev, "failed to handle node %pOFn: %d\n",
				np, error);
			goto err_out;
		}

		error = of_property_read_u32(np, "reg", &idx);
		if (error) {
			dev_err(pmu->dev,
				"%pOFn: failed to retrieve domain id (reg): %d\n",
				np, error);
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
			   struct_size(pmu, domains, pmu_info->num_domains),
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
	if (pmu_info->core_power_transition_time)
		rockchip_configure_pd_cnt(pmu, pmu_info->core_pwrcnt_offset,
					pmu_info->core_power_transition_time);
	if (pmu_info->gpu_pwrcnt_offset)
		rockchip_configure_pd_cnt(pmu, pmu_info->gpu_pwrcnt_offset,
					pmu_info->gpu_power_transition_time);

	error = -ENODEV;

	/*
	 * Prevent any rockchip_pmu_block() from racing with the remainder of
	 * setup (clocks, register initialization).
	 */
	mutex_lock(&dmc_pmu_mutex);

	for_each_available_child_of_node(np, node) {
		error = rockchip_pm_add_one_domain(pmu, node);
		if (error) {
			dev_err(dev, "failed to handle node %pOFn: %d\n",
				node, error);
			of_node_put(node);
			goto err_out;
		}

		error = rockchip_pm_add_subdomain(pmu, node);
		if (error < 0) {
			dev_err(dev, "failed to handle subdomain node %pOFn: %d\n",
				node, error);
			of_node_put(node);
			goto err_out;
		}
	}

	if (error) {
		dev_dbg(dev, "no power domains defined\n");
		goto err_out;
	}

	error = of_genpd_add_provider_onecell(np, &pmu->genpd_data);
	if (error) {
		dev_err(dev, "failed to add provider: %d\n", error);
		goto err_out;
	}

	/* We only expect one PMU. */
	if (!WARN_ON_ONCE(dmc_pmu))
		dmc_pmu = pmu;

	mutex_unlock(&dmc_pmu_mutex);

	return 0;

err_out:
	rockchip_pm_domain_cleanup(pmu);
	mutex_unlock(&dmc_pmu_mutex);
	return error;
}

static const struct rockchip_domain_info px30_pm_domains[] = {
	[PX30_PD_USB]		= DOMAIN_PX30("usb",      BIT(5),  BIT(5),  BIT(10), false),
	[PX30_PD_SDCARD]	= DOMAIN_PX30("sdcard",   BIT(8),  BIT(8),  BIT(9),  false),
	[PX30_PD_GMAC]		= DOMAIN_PX30("gmac",     BIT(10), BIT(10), BIT(6),  false),
	[PX30_PD_MMC_NAND]	= DOMAIN_PX30("mmc_nand", BIT(11), BIT(11), BIT(5),  false),
	[PX30_PD_VPU]		= DOMAIN_PX30("vpu",      BIT(12), BIT(12), BIT(14), false),
	[PX30_PD_VO]		= DOMAIN_PX30("vo",       BIT(13), BIT(13), BIT(7),  false),
	[PX30_PD_VI]		= DOMAIN_PX30("vi",       BIT(14), BIT(14), BIT(8),  false),
	[PX30_PD_GPU]		= DOMAIN_PX30("gpu",      BIT(15), BIT(15), BIT(2),  false),
};

static const struct rockchip_domain_info rv1126_pm_domains[] = {
	[RV1126_PD_VEPU]	= DOMAIN_RV1126("vepu", BIT(2),  BIT(9),  BIT(9), false),
	[RV1126_PD_VI]		= DOMAIN_RV1126("vi", BIT(4),  BIT(6),  BIT(6),  false),
	[RV1126_PD_VO]		= DOMAIN_RV1126("vo", BIT(5),  BIT(7),  BIT(7),  false),
	[RV1126_PD_ISPP]	= DOMAIN_RV1126("ispp", BIT(1), BIT(8), BIT(8),  false),
	[RV1126_PD_VDPU]	= DOMAIN_RV1126("vdpu", BIT(3), BIT(10), BIT(10), false),
	[RV1126_PD_NVM]		= DOMAIN_RV1126("nvm", BIT(7), BIT(11), BIT(11),  false),
	[RV1126_PD_SDIO]	= DOMAIN_RV1126("sdio", BIT(8), BIT(13), BIT(13),  false),
	[RV1126_PD_USB]		= DOMAIN_RV1126("usb", BIT(9), BIT(15), BIT(15),  false),
};

static const struct rockchip_domain_info rk3036_pm_domains[] = {
	[RK3036_PD_MSCH]	= DOMAIN_RK3036("msch", BIT(14), BIT(23), BIT(30), true),
	[RK3036_PD_CORE]	= DOMAIN_RK3036("core", BIT(13), BIT(17), BIT(24), false),
	[RK3036_PD_PERI]	= DOMAIN_RK3036("peri", BIT(12), BIT(18), BIT(25), false),
	[RK3036_PD_VIO]		= DOMAIN_RK3036("vio",  BIT(11), BIT(19), BIT(26), false),
	[RK3036_PD_VPU]		= DOMAIN_RK3036("vpu",  BIT(10), BIT(20), BIT(27), false),
	[RK3036_PD_GPU]		= DOMAIN_RK3036("gpu",  BIT(9),  BIT(21), BIT(28), false),
	[RK3036_PD_SYS]		= DOMAIN_RK3036("sys",  BIT(8),  BIT(22), BIT(29), false),
};

static const struct rockchip_domain_info rk3066_pm_domains[] = {
	[RK3066_PD_GPU]		= DOMAIN("gpu",   BIT(9), BIT(9), BIT(3), BIT(24), BIT(29), false),
	[RK3066_PD_VIDEO]	= DOMAIN("video", BIT(8), BIT(8), BIT(4), BIT(23), BIT(28), false),
	[RK3066_PD_VIO]		= DOMAIN("vio",   BIT(7), BIT(7), BIT(5), BIT(22), BIT(27), false),
	[RK3066_PD_PERI]	= DOMAIN("peri",  BIT(6), BIT(6), BIT(2), BIT(25), BIT(30), false),
	[RK3066_PD_CPU]		= DOMAIN("cpu",   0,      BIT(5), BIT(1), BIT(26), BIT(31), false),
};

static const struct rockchip_domain_info rk3128_pm_domains[] = {
	[RK3128_PD_CORE]	= DOMAIN_RK3288("core",  BIT(0), BIT(0), BIT(4), false),
	[RK3128_PD_MSCH]	= DOMAIN_RK3288("msch",  0,      0,      BIT(6), true),
	[RK3128_PD_VIO]		= DOMAIN_RK3288("vio",   BIT(3), BIT(3), BIT(2), false),
	[RK3128_PD_VIDEO]	= DOMAIN_RK3288("video", BIT(2), BIT(2), BIT(1), false),
	[RK3128_PD_GPU]		= DOMAIN_RK3288("gpu",   BIT(1), BIT(1), BIT(3), false),
};

static const struct rockchip_domain_info rk3188_pm_domains[] = {
	[RK3188_PD_GPU]		= DOMAIN("gpu",   BIT(9), BIT(9), BIT(3), BIT(24), BIT(29), false),
	[RK3188_PD_VIDEO]	= DOMAIN("video", BIT(8), BIT(8), BIT(4), BIT(23), BIT(28), false),
	[RK3188_PD_VIO]		= DOMAIN("vio",   BIT(7), BIT(7), BIT(5), BIT(22), BIT(27), false),
	[RK3188_PD_PERI]	= DOMAIN("peri",  BIT(6), BIT(6), BIT(2), BIT(25), BIT(30), false),
	[RK3188_PD_CPU]		= DOMAIN("cpu",   BIT(5), BIT(5), BIT(1), BIT(26), BIT(31), false),
};

static const struct rockchip_domain_info rk3228_pm_domains[] = {
	[RK3228_PD_CORE]	= DOMAIN_RK3036("core", BIT(0),  BIT(0),  BIT(16), true),
	[RK3228_PD_MSCH]	= DOMAIN_RK3036("msch", BIT(1),  BIT(1),  BIT(17), true),
	[RK3228_PD_BUS]		= DOMAIN_RK3036("bus",  BIT(2),  BIT(2),  BIT(18), true),
	[RK3228_PD_SYS]		= DOMAIN_RK3036("sys",  BIT(3),  BIT(3),  BIT(19), true),
	[RK3228_PD_VIO]		= DOMAIN_RK3036("vio",  BIT(4),  BIT(4),  BIT(20), false),
	[RK3228_PD_VOP]		= DOMAIN_RK3036("vop",  BIT(5),  BIT(5),  BIT(21), false),
	[RK3228_PD_VPU]		= DOMAIN_RK3036("vpu",  BIT(6),  BIT(6),  BIT(22), false),
	[RK3228_PD_RKVDEC]	= DOMAIN_RK3036("vdec", BIT(7),  BIT(7),  BIT(23), false),
	[RK3228_PD_GPU]		= DOMAIN_RK3036("gpu",  BIT(8),  BIT(8),  BIT(24), false),
	[RK3228_PD_PERI]	= DOMAIN_RK3036("peri", BIT(9),  BIT(9),  BIT(25), true),
	[RK3228_PD_GMAC]	= DOMAIN_RK3036("gmac", BIT(10), BIT(10), BIT(26), false),
};

static const struct rockchip_domain_info rk3288_pm_domains[] = {
	[RK3288_PD_VIO]		= DOMAIN_RK3288("vio",   BIT(7),  BIT(7),  BIT(4), false),
	[RK3288_PD_HEVC]	= DOMAIN_RK3288("hevc",  BIT(14), BIT(10), BIT(9), false),
	[RK3288_PD_VIDEO]	= DOMAIN_RK3288("video", BIT(8),  BIT(8),  BIT(3), false),
	[RK3288_PD_GPU]		= DOMAIN_RK3288("gpu",   BIT(9),  BIT(9),  BIT(2), false),
};

static const struct rockchip_domain_info rk3328_pm_domains[] = {
	[RK3328_PD_CORE]	= DOMAIN_RK3328("core",  0, BIT(0), BIT(0), false),
	[RK3328_PD_GPU]		= DOMAIN_RK3328("gpu",   0, BIT(1), BIT(1), false),
	[RK3328_PD_BUS]		= DOMAIN_RK3328("bus",   0, BIT(2), BIT(2), true),
	[RK3328_PD_MSCH]	= DOMAIN_RK3328("msch",  0, BIT(3), BIT(3), true),
	[RK3328_PD_PERI]	= DOMAIN_RK3328("peri",  0, BIT(4), BIT(4), true),
	[RK3328_PD_VIDEO]	= DOMAIN_RK3328("video", 0, BIT(5), BIT(5), false),
	[RK3328_PD_HEVC]	= DOMAIN_RK3328("hevc",  0, BIT(6), BIT(6), false),
	[RK3328_PD_VIO]		= DOMAIN_RK3328("vio",   0, BIT(8), BIT(8), false),
	[RK3328_PD_VPU]		= DOMAIN_RK3328("vpu",   0, BIT(9), BIT(9), false),
};

static const struct rockchip_domain_info rk3366_pm_domains[] = {
	[RK3366_PD_PERI]	= DOMAIN_RK3368("peri",   BIT(10), BIT(10), BIT(6), true),
	[RK3366_PD_VIO]		= DOMAIN_RK3368("vio",    BIT(14), BIT(14), BIT(8), false),
	[RK3366_PD_VIDEO]	= DOMAIN_RK3368("video",  BIT(13), BIT(13), BIT(7), false),
	[RK3366_PD_RKVDEC]	= DOMAIN_RK3368("vdec",   BIT(11), BIT(11), BIT(7), false),
	[RK3366_PD_WIFIBT]	= DOMAIN_RK3368("wifibt", BIT(8),  BIT(8),  BIT(9), false),
	[RK3366_PD_VPU]		= DOMAIN_RK3368("vpu",    BIT(12), BIT(12), BIT(7), false),
	[RK3366_PD_GPU]		= DOMAIN_RK3368("gpu",    BIT(15), BIT(15), BIT(2), false),
};

static const struct rockchip_domain_info rk3368_pm_domains[] = {
	[RK3368_PD_PERI]	= DOMAIN_RK3368("peri",  BIT(13), BIT(12), BIT(6), true),
	[RK3368_PD_VIO]		= DOMAIN_RK3368("vio",   BIT(15), BIT(14), BIT(8), false),
	[RK3368_PD_VIDEO]	= DOMAIN_RK3368("video", BIT(14), BIT(13), BIT(7), false),
	[RK3368_PD_GPU_0]	= DOMAIN_RK3368("gpu_0", BIT(16), BIT(15), BIT(2), false),
	[RK3368_PD_GPU_1]	= DOMAIN_RK3368("gpu_1", BIT(17), BIT(16), BIT(2), false),
};

static const struct rockchip_domain_info rk3399_pm_domains[] = {
	[RK3399_PD_TCPD0]	= DOMAIN_RK3399("tcpd0",     BIT(8),  BIT(8),  0,       false),
	[RK3399_PD_TCPD1]	= DOMAIN_RK3399("tcpd1",     BIT(9),  BIT(9),  0,       false),
	[RK3399_PD_CCI]		= DOMAIN_RK3399("cci",       BIT(10), BIT(10), 0,       true),
	[RK3399_PD_CCI0]	= DOMAIN_RK3399("cci0",      0,       0,       BIT(15), true),
	[RK3399_PD_CCI1]	= DOMAIN_RK3399("cci1",      0,       0,       BIT(16), true),
	[RK3399_PD_PERILP]	= DOMAIN_RK3399("perilp",    BIT(11), BIT(11), BIT(1),  true),
	[RK3399_PD_PERIHP]	= DOMAIN_RK3399("perihp",    BIT(12), BIT(12), BIT(2),  true),
	[RK3399_PD_CENTER]	= DOMAIN_RK3399("center",    BIT(13), BIT(13), BIT(14), true),
	[RK3399_PD_VIO]		= DOMAIN_RK3399("vio",       BIT(14), BIT(14), BIT(17), false),
	[RK3399_PD_GPU]		= DOMAIN_RK3399("gpu",       BIT(15), BIT(15), BIT(0),  false),
	[RK3399_PD_VCODEC]	= DOMAIN_RK3399("vcodec",    BIT(16), BIT(16), BIT(3),  false),
	[RK3399_PD_VDU]		= DOMAIN_RK3399("vdu",       BIT(17), BIT(17), BIT(4),  false),
	[RK3399_PD_RGA]		= DOMAIN_RK3399("rga",       BIT(18), BIT(18), BIT(5),  false),
	[RK3399_PD_IEP]		= DOMAIN_RK3399("iep",       BIT(19), BIT(19), BIT(6),  false),
	[RK3399_PD_VO]		= DOMAIN_RK3399("vo",        BIT(20), BIT(20), 0,       false),
	[RK3399_PD_VOPB]	= DOMAIN_RK3399("vopb",      0,       0,       BIT(7),  false),
	[RK3399_PD_VOPL]	= DOMAIN_RK3399("vopl",      0,       0,       BIT(8),  false),
	[RK3399_PD_ISP0]	= DOMAIN_RK3399("isp0",      BIT(22), BIT(22), BIT(9),  false),
	[RK3399_PD_ISP1]	= DOMAIN_RK3399("isp1",      BIT(23), BIT(23), BIT(10), false),
	[RK3399_PD_HDCP]	= DOMAIN_RK3399("hdcp",      BIT(24), BIT(24), BIT(11), false),
	[RK3399_PD_GMAC]	= DOMAIN_RK3399("gmac",      BIT(25), BIT(25), BIT(23), true),
	[RK3399_PD_EMMC]	= DOMAIN_RK3399("emmc",      BIT(26), BIT(26), BIT(24), true),
	[RK3399_PD_USB3]	= DOMAIN_RK3399("usb3",      BIT(27), BIT(27), BIT(12), true),
	[RK3399_PD_EDP]		= DOMAIN_RK3399("edp",       BIT(28), BIT(28), BIT(22), false),
	[RK3399_PD_GIC]		= DOMAIN_RK3399("gic",       BIT(29), BIT(29), BIT(27), true),
	[RK3399_PD_SD]		= DOMAIN_RK3399("sd",        BIT(30), BIT(30), BIT(28), true),
	[RK3399_PD_SDIOAUDIO]	= DOMAIN_RK3399("sdioaudio", BIT(31), BIT(31), BIT(29), true),
};

static const struct rockchip_domain_info rk3568_pm_domains[] = {
	[RK3568_PD_NPU]		= DOMAIN_RK3568("npu",  BIT(1), BIT(2),  false),
	[RK3568_PD_GPU]		= DOMAIN_RK3568("gpu",  BIT(0), BIT(1),  false),
	[RK3568_PD_VI]		= DOMAIN_RK3568("vi",   BIT(6), BIT(3),  false),
	[RK3568_PD_VO]		= DOMAIN_RK3568("vo",   BIT(7), BIT(4),  false),
	[RK3568_PD_RGA]		= DOMAIN_RK3568("rga",  BIT(5), BIT(5),  false),
	[RK3568_PD_VPU]		= DOMAIN_RK3568("vpu",  BIT(2), BIT(6),  false),
	[RK3568_PD_RKVDEC]	= DOMAIN_RK3568("vdec", BIT(4), BIT(8),  false),
	[RK3568_PD_RKVENC]	= DOMAIN_RK3568("venc", BIT(3), BIT(7),  false),
	[RK3568_PD_PIPE]	= DOMAIN_RK3568("pipe", BIT(8), BIT(11), false),
};

static const struct rockchip_domain_info rk3588_pm_domains[] = {
	[RK3588_PD_GPU]		= DOMAIN_RK3588("gpu",     0x0, BIT(0),  0,       0x0, 0,       BIT(1),  0x0, BIT(0),  BIT(0),  false),
	[RK3588_PD_NPU]		= DOMAIN_RK3588("npu",     0x0, BIT(1),  BIT(1),  0x0, 0,       0,       0x0, 0,       0,       false),
	[RK3588_PD_VCODEC]	= DOMAIN_RK3588("vcodec",  0x0, BIT(2),  BIT(2),  0x0, 0,       0,       0x0, 0,       0,       false),
	[RK3588_PD_NPUTOP]	= DOMAIN_RK3588("nputop",  0x0, BIT(3),  0,       0x0, BIT(11), BIT(2),  0x0, BIT(1),  BIT(1),  false),
	[RK3588_PD_NPU1]	= DOMAIN_RK3588("npu1",    0x0, BIT(4),  0,       0x0, BIT(12), BIT(3),  0x0, BIT(2),  BIT(2),  false),
	[RK3588_PD_NPU2]	= DOMAIN_RK3588("npu2",    0x0, BIT(5),  0,       0x0, BIT(13), BIT(4),  0x0, BIT(3),  BIT(3),  false),
	[RK3588_PD_VENC0]	= DOMAIN_RK3588("venc0",   0x0, BIT(6),  0,       0x0, BIT(14), BIT(5),  0x0, BIT(4),  BIT(4),  false),
	[RK3588_PD_VENC1]	= DOMAIN_RK3588("venc1",   0x0, BIT(7),  0,       0x0, BIT(15), BIT(6),  0x0, BIT(5),  BIT(5),  false),
	[RK3588_PD_RKVDEC0]	= DOMAIN_RK3588("rkvdec0", 0x0, BIT(8),  0,       0x0, BIT(16), BIT(7),  0x0, BIT(6),  BIT(6),  false),
	[RK3588_PD_RKVDEC1]	= DOMAIN_RK3588("rkvdec1", 0x0, BIT(9),  0,       0x0, BIT(17), BIT(8),  0x0, BIT(7),  BIT(7),  false),
	[RK3588_PD_VDPU]	= DOMAIN_RK3588("vdpu",    0x0, BIT(10), 0,       0x0, BIT(18), BIT(9),  0x0, BIT(8),  BIT(8),  false),
	[RK3588_PD_RGA30]	= DOMAIN_RK3588("rga30",   0x0, BIT(11), 0,       0x0, BIT(19), BIT(10), 0x0, 0,       0,       false),
	[RK3588_PD_AV1]		= DOMAIN_RK3588("av1",     0x0, BIT(12), 0,       0x0, BIT(20), BIT(11), 0x0, BIT(9),  BIT(9),  false),
	[RK3588_PD_VI]		= DOMAIN_RK3588("vi",      0x0, BIT(13), 0,       0x0, BIT(21), BIT(12), 0x0, BIT(10), BIT(10), false),
	[RK3588_PD_FEC]		= DOMAIN_RK3588("fec",     0x0, BIT(14), 0,       0x0, BIT(22), BIT(13), 0x0, 0,       0,       false),
	[RK3588_PD_ISP1]	= DOMAIN_RK3588("isp1",    0x0, BIT(15), 0,       0x0, BIT(23), BIT(14), 0x0, BIT(11), BIT(11), false),
	[RK3588_PD_RGA31]	= DOMAIN_RK3588("rga31",   0x4, BIT(0),  0,       0x0, BIT(24), BIT(15), 0x0, BIT(12), BIT(12), false),
	[RK3588_PD_VOP]		= DOMAIN_RK3588("vop",     0x4, BIT(1),  0,       0x0, BIT(25), BIT(16), 0x0, BIT(13) | BIT(14), BIT(13) | BIT(14), false),
	[RK3588_PD_VO0]		= DOMAIN_RK3588("vo0",     0x4, BIT(2),  0,       0x0, BIT(26), BIT(17), 0x0, BIT(15), BIT(15), false),
	[RK3588_PD_VO1]		= DOMAIN_RK3588("vo1",     0x4, BIT(3),  0,       0x0, BIT(27), BIT(18), 0x4, BIT(0),  BIT(16), false),
	[RK3588_PD_AUDIO]	= DOMAIN_RK3588("audio",   0x4, BIT(4),  0,       0x0, BIT(28), BIT(19), 0x4, BIT(1),  BIT(17), false),
	[RK3588_PD_PHP]		= DOMAIN_RK3588("php",     0x4, BIT(5),  0,       0x0, BIT(29), BIT(20), 0x4, BIT(5),  BIT(21), false),
	[RK3588_PD_GMAC]	= DOMAIN_RK3588("gmac",    0x4, BIT(6),  0,       0x0, BIT(30), BIT(21), 0x0, 0,       0,       false),
	[RK3588_PD_PCIE]	= DOMAIN_RK3588("pcie",    0x4, BIT(7),  0,       0x0, BIT(31), BIT(22), 0x0, 0,       0,       true),
	[RK3588_PD_NVM]		= DOMAIN_RK3588("nvm",     0x4, BIT(8),  BIT(24), 0x4, 0,       0,       0x4, BIT(2),  BIT(18), false),
	[RK3588_PD_NVM0]	= DOMAIN_RK3588("nvm0",    0x4, BIT(9),  0,       0x4, BIT(1),  BIT(23), 0x0, 0,       0,       false),
	[RK3588_PD_SDIO]	= DOMAIN_RK3588("sdio",    0x4, BIT(10), 0,       0x4, BIT(2),  BIT(24), 0x4, BIT(3),  BIT(19), false),
	[RK3588_PD_USB]		= DOMAIN_RK3588("usb",     0x4, BIT(11), 0,       0x4, BIT(3),  BIT(25), 0x4, BIT(4),  BIT(20), true),
	[RK3588_PD_SDMMC]	= DOMAIN_RK3588("sdmmc",   0x4, BIT(13), 0,       0x4, BIT(5),  BIT(26), 0x0, 0,       0,       false),
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

static const struct rockchip_pmu_info rk3036_pmu = {
	.req_offset = 0x148,
	.idle_offset = 0x14c,
	.ack_offset = 0x14c,

	.num_domains = ARRAY_SIZE(rk3036_pm_domains),
	.domain_info = rk3036_pm_domains,
};

static const struct rockchip_pmu_info rk3066_pmu = {
	.pwr_offset = 0x08,
	.status_offset = 0x0c,
	.req_offset = 0x38, /* PMU_MISC_CON1 */
	.idle_offset = 0x0c,
	.ack_offset = 0x0c,

	.num_domains = ARRAY_SIZE(rk3066_pm_domains),
	.domain_info = rk3066_pm_domains,
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

static const struct rockchip_pmu_info rk3188_pmu = {
	.pwr_offset = 0x08,
	.status_offset = 0x0c,
	.req_offset = 0x38, /* PMU_MISC_CON1 */
	.idle_offset = 0x0c,
	.ack_offset = 0x0c,

	.num_domains = ARRAY_SIZE(rk3188_pm_domains),
	.domain_info = rk3188_pm_domains,
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

	/* ARM Trusted Firmware manages power transition times */

	.num_domains = ARRAY_SIZE(rk3399_pm_domains),
	.domain_info = rk3399_pm_domains,
};

static const struct rockchip_pmu_info rk3568_pmu = {
	.pwr_offset = 0xa0,
	.status_offset = 0x98,
	.req_offset = 0x50,
	.idle_offset = 0x68,
	.ack_offset = 0x60,

	.num_domains = ARRAY_SIZE(rk3568_pm_domains),
	.domain_info = rk3568_pm_domains,
};

static const struct rockchip_pmu_info rk3588_pmu = {
	.pwr_offset = 0x14c,
	.status_offset = 0x180,
	.req_offset = 0x10c,
	.idle_offset = 0x120,
	.ack_offset = 0x118,
	.mem_pwr_offset = 0x1a0,
	.chain_status_offset = 0x1f0,
	.mem_status_offset = 0x1f8,
	.repair_status_offset = 0x290,

	.num_domains = ARRAY_SIZE(rk3588_pm_domains),
	.domain_info = rk3588_pm_domains,
};

static const struct rockchip_pmu_info rv1126_pmu = {
	.pwr_offset = 0x110,
	.status_offset = 0x108,
	.req_offset = 0xc0,
	.idle_offset = 0xd8,
	.ack_offset = 0xd0,

	.num_domains = ARRAY_SIZE(rv1126_pm_domains),
	.domain_info = rv1126_pm_domains,
};

static const struct of_device_id rockchip_pm_domain_dt_match[] = {
	{
		.compatible = "rockchip,px30-power-controller",
		.data = (void *)&px30_pmu,
	},
	{
		.compatible = "rockchip,rk3036-power-controller",
		.data = (void *)&rk3036_pmu,
	},
	{
		.compatible = "rockchip,rk3066-power-controller",
		.data = (void *)&rk3066_pmu,
	},
	{
		.compatible = "rockchip,rk3128-power-controller",
		.data = (void *)&rk3128_pmu,
	},
	{
		.compatible = "rockchip,rk3188-power-controller",
		.data = (void *)&rk3188_pmu,
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
	{
		.compatible = "rockchip,rk3568-power-controller",
		.data = (void *)&rk3568_pmu,
	},
	{
		.compatible = "rockchip,rk3588-power-controller",
		.data = (void *)&rk3588_pmu,
	},
	{
		.compatible = "rockchip,rv1126-power-controller",
		.data = (void *)&rv1126_pmu,
	},
	{ /* sentinel */ },
};

static struct platform_driver rockchip_pm_domain_driver = {
	.probe = rockchip_pm_domain_probe,
	.driver = {
		.name   = "rockchip-pm-domain",
		.of_match_table = rockchip_pm_domain_dt_match,
		/*
		 * We can't forcibly eject devices from the power
		 * domain, so we can't really remove power domains
		 * once they were added.
		 */
		.suppress_bind_attrs = true,
	},
};

static int __init rockchip_pm_domain_drv_register(void)
{
	return platform_driver_register(&rockchip_pm_domain_driver);
}
postcore_initcall(rockchip_pm_domain_drv_register);
