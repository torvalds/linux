// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2018, The Linux Foundation. All rights reserved.*/

#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pm_domain.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <soc/qcom/cmd-db.h>
#include <soc/qcom/rpmh.h>
#include <dt-bindings/power/qcom-rpmpd.h>

#define domain_to_rpmhpd(domain) container_of(domain, struct rpmhpd, pd)

#define RPMH_ARC_MAX_LEVELS	16

/**
 * struct rpmhpd - top level RPMh power domain resource data structure
 * @dev:		rpmh power domain controller device
 * @pd:			generic_pm_domain corrresponding to the power domain
 * @parent:		generic_pm_domain corrresponding to the parent's power domain
 * @peer:		A peer power domain in case Active only Voting is
 *			supported
 * @active_only:	True if it represents an Active only peer
 * @corner:		current corner
 * @active_corner:	current active corner
 * @enable_corner:	lowest non-zero corner
 * @level:		An array of level (vlvl) to corner (hlvl) mappings
 *			derived from cmd-db
 * @level_count:	Number of levels supported by the power domain. max
 *			being 16 (0 - 15)
 * @enabled:		true if the power domain is enabled
 * @res_name:		Resource name used for cmd-db lookup
 * @addr:		Resource address as looped up using resource name from
 *			cmd-db
 */
struct rpmhpd {
	struct device	*dev;
	struct generic_pm_domain pd;
	struct generic_pm_domain *parent;
	struct rpmhpd	*peer;
	const bool	active_only;
	unsigned int	corner;
	unsigned int	active_corner;
	unsigned int	enable_corner;
	u32		level[RPMH_ARC_MAX_LEVELS];
	size_t		level_count;
	bool		enabled;
	const char	*res_name;
	u32		addr;
};

struct rpmhpd_desc {
	struct rpmhpd **rpmhpds;
	size_t num_pds;
};

static DEFINE_MUTEX(rpmhpd_lock);

/* SDM845 RPMH powerdomains */

static struct rpmhpd sdm845_ebi = {
	.pd = { .name = "ebi", },
	.res_name = "ebi.lvl",
};

static struct rpmhpd sdm845_lmx = {
	.pd = { .name = "lmx", },
	.res_name = "lmx.lvl",
};

static struct rpmhpd sdm845_lcx = {
	.pd = { .name = "lcx", },
	.res_name = "lcx.lvl",
};

static struct rpmhpd sdm845_gfx = {
	.pd = { .name = "gfx", },
	.res_name = "gfx.lvl",
};

static struct rpmhpd sdm845_mss = {
	.pd = { .name = "mss", },
	.res_name = "mss.lvl",
};

static struct rpmhpd sdm845_mx_ao;
static struct rpmhpd sdm845_mx = {
	.pd = { .name = "mx", },
	.peer = &sdm845_mx_ao,
	.res_name = "mx.lvl",
};

static struct rpmhpd sdm845_mx_ao = {
	.pd = { .name = "mx_ao", },
	.active_only = true,
	.peer = &sdm845_mx,
	.res_name = "mx.lvl",
};

static struct rpmhpd sdm845_cx_ao;
static struct rpmhpd sdm845_cx = {
	.pd = { .name = "cx", },
	.peer = &sdm845_cx_ao,
	.parent = &sdm845_mx.pd,
	.res_name = "cx.lvl",
};

static struct rpmhpd sdm845_cx_ao = {
	.pd = { .name = "cx_ao", },
	.active_only = true,
	.peer = &sdm845_cx,
	.parent = &sdm845_mx_ao.pd,
	.res_name = "cx.lvl",
};

static struct rpmhpd *sdm845_rpmhpds[] = {
	[SDM845_EBI] = &sdm845_ebi,
	[SDM845_MX] = &sdm845_mx,
	[SDM845_MX_AO] = &sdm845_mx_ao,
	[SDM845_CX] = &sdm845_cx,
	[SDM845_CX_AO] = &sdm845_cx_ao,
	[SDM845_LMX] = &sdm845_lmx,
	[SDM845_LCX] = &sdm845_lcx,
	[SDM845_GFX] = &sdm845_gfx,
	[SDM845_MSS] = &sdm845_mss,
};

static const struct rpmhpd_desc sdm845_desc = {
	.rpmhpds = sdm845_rpmhpds,
	.num_pds = ARRAY_SIZE(sdm845_rpmhpds),
};

/* SDX55 RPMH powerdomains */
static struct rpmhpd *sdx55_rpmhpds[] = {
	[SDX55_MSS] = &sdm845_mss,
	[SDX55_MX] = &sdm845_mx,
	[SDX55_CX] = &sdm845_cx,
};

static const struct rpmhpd_desc sdx55_desc = {
	.rpmhpds = sdx55_rpmhpds,
	.num_pds = ARRAY_SIZE(sdx55_rpmhpds),
};

/* SM6350 RPMH powerdomains */
static struct rpmhpd *sm6350_rpmhpds[] = {
	[SM6350_CX] = &sdm845_cx,
	[SM6350_GFX] = &sdm845_gfx,
	[SM6350_LCX] = &sdm845_lcx,
	[SM6350_LMX] = &sdm845_lmx,
	[SM6350_MSS] = &sdm845_mss,
	[SM6350_MX] = &sdm845_mx,
};

static const struct rpmhpd_desc sm6350_desc = {
	.rpmhpds = sm6350_rpmhpds,
	.num_pds = ARRAY_SIZE(sm6350_rpmhpds),
};

/* SM8150 RPMH powerdomains */

static struct rpmhpd sm8150_mmcx_ao;
static struct rpmhpd sm8150_mmcx = {
	.pd = { .name = "mmcx", },
	.peer = &sm8150_mmcx_ao,
	.res_name = "mmcx.lvl",
};

static struct rpmhpd sm8150_mmcx_ao = {
	.pd = { .name = "mmcx_ao", },
	.active_only = true,
	.peer = &sm8150_mmcx,
	.res_name = "mmcx.lvl",
};

static struct rpmhpd *sm8150_rpmhpds[] = {
	[SM8150_MSS] = &sdm845_mss,
	[SM8150_EBI] = &sdm845_ebi,
	[SM8150_LMX] = &sdm845_lmx,
	[SM8150_LCX] = &sdm845_lcx,
	[SM8150_GFX] = &sdm845_gfx,
	[SM8150_MX] = &sdm845_mx,
	[SM8150_MX_AO] = &sdm845_mx_ao,
	[SM8150_CX] = &sdm845_cx,
	[SM8150_CX_AO] = &sdm845_cx_ao,
	[SM8150_MMCX] = &sm8150_mmcx,
	[SM8150_MMCX_AO] = &sm8150_mmcx_ao,
};

static const struct rpmhpd_desc sm8150_desc = {
	.rpmhpds = sm8150_rpmhpds,
	.num_pds = ARRAY_SIZE(sm8150_rpmhpds),
};

static struct rpmhpd *sm8250_rpmhpds[] = {
	[SM8250_CX] = &sdm845_cx,
	[SM8250_CX_AO] = &sdm845_cx_ao,
	[SM8250_EBI] = &sdm845_ebi,
	[SM8250_GFX] = &sdm845_gfx,
	[SM8250_LCX] = &sdm845_lcx,
	[SM8250_LMX] = &sdm845_lmx,
	[SM8250_MMCX] = &sm8150_mmcx,
	[SM8250_MMCX_AO] = &sm8150_mmcx_ao,
	[SM8250_MX] = &sdm845_mx,
	[SM8250_MX_AO] = &sdm845_mx_ao,
};

static const struct rpmhpd_desc sm8250_desc = {
	.rpmhpds = sm8250_rpmhpds,
	.num_pds = ARRAY_SIZE(sm8250_rpmhpds),
};

/* SM8350 Power domains */
static struct rpmhpd sm8350_mxc_ao;
static struct rpmhpd sm8350_mxc = {
	.pd = { .name = "mxc", },
	.peer = &sm8350_mxc_ao,
	.res_name = "mxc.lvl",
};

static struct rpmhpd sm8350_mxc_ao = {
	.pd = { .name = "mxc_ao", },
	.active_only = true,
	.peer = &sm8350_mxc,
	.res_name = "mxc.lvl",
};

static struct rpmhpd *sm8350_rpmhpds[] = {
	[SM8350_CX] = &sdm845_cx,
	[SM8350_CX_AO] = &sdm845_cx_ao,
	[SM8350_EBI] = &sdm845_ebi,
	[SM8350_GFX] = &sdm845_gfx,
	[SM8350_LCX] = &sdm845_lcx,
	[SM8350_LMX] = &sdm845_lmx,
	[SM8350_MMCX] = &sm8150_mmcx,
	[SM8350_MMCX_AO] = &sm8150_mmcx_ao,
	[SM8350_MX] = &sdm845_mx,
	[SM8350_MX_AO] = &sdm845_mx_ao,
	[SM8350_MXC] = &sm8350_mxc,
	[SM8350_MXC_AO] = &sm8350_mxc_ao,
	[SM8350_MSS] = &sdm845_mss,
};

static const struct rpmhpd_desc sm8350_desc = {
	.rpmhpds = sm8350_rpmhpds,
	.num_pds = ARRAY_SIZE(sm8350_rpmhpds),
};

/* SC7180 RPMH powerdomains */
static struct rpmhpd *sc7180_rpmhpds[] = {
	[SC7180_CX] = &sdm845_cx,
	[SC7180_CX_AO] = &sdm845_cx_ao,
	[SC7180_GFX] = &sdm845_gfx,
	[SC7180_MX] = &sdm845_mx,
	[SC7180_MX_AO] = &sdm845_mx_ao,
	[SC7180_LMX] = &sdm845_lmx,
	[SC7180_LCX] = &sdm845_lcx,
	[SC7180_MSS] = &sdm845_mss,
};

static const struct rpmhpd_desc sc7180_desc = {
	.rpmhpds = sc7180_rpmhpds,
	.num_pds = ARRAY_SIZE(sc7180_rpmhpds),
};

/* SC7280 RPMH powerdomains */
static struct rpmhpd *sc7280_rpmhpds[] = {
	[SC7280_CX] = &sdm845_cx,
	[SC7280_CX_AO] = &sdm845_cx_ao,
	[SC7280_EBI] = &sdm845_ebi,
	[SC7280_GFX] = &sdm845_gfx,
	[SC7280_MX] = &sdm845_mx,
	[SC7280_MX_AO] = &sdm845_mx_ao,
	[SC7280_LMX] = &sdm845_lmx,
	[SC7280_LCX] = &sdm845_lcx,
	[SC7280_MSS] = &sdm845_mss,
};

static const struct rpmhpd_desc sc7280_desc = {
	.rpmhpds = sc7280_rpmhpds,
	.num_pds = ARRAY_SIZE(sc7280_rpmhpds),
};

/* SC8180x RPMH powerdomains */
static struct rpmhpd *sc8180x_rpmhpds[] = {
	[SC8180X_CX] = &sdm845_cx,
	[SC8180X_CX_AO] = &sdm845_cx_ao,
	[SC8180X_EBI] = &sdm845_ebi,
	[SC8180X_GFX] = &sdm845_gfx,
	[SC8180X_LCX] = &sdm845_lcx,
	[SC8180X_LMX] = &sdm845_lmx,
	[SC8180X_MMCX] = &sm8150_mmcx,
	[SC8180X_MMCX_AO] = &sm8150_mmcx_ao,
	[SC8180X_MSS] = &sdm845_mss,
	[SC8180X_MX] = &sdm845_mx,
	[SC8180X_MX_AO] = &sdm845_mx_ao,
};

static const struct rpmhpd_desc sc8180x_desc = {
	.rpmhpds = sc8180x_rpmhpds,
	.num_pds = ARRAY_SIZE(sc8180x_rpmhpds),
};

static const struct of_device_id rpmhpd_match_table[] = {
	{ .compatible = "qcom,sc7180-rpmhpd", .data = &sc7180_desc },
	{ .compatible = "qcom,sc7280-rpmhpd", .data = &sc7280_desc },
	{ .compatible = "qcom,sc8180x-rpmhpd", .data = &sc8180x_desc },
	{ .compatible = "qcom,sdm845-rpmhpd", .data = &sdm845_desc },
	{ .compatible = "qcom,sdx55-rpmhpd", .data = &sdx55_desc},
	{ .compatible = "qcom,sm6350-rpmhpd", .data = &sm6350_desc },
	{ .compatible = "qcom,sm8150-rpmhpd", .data = &sm8150_desc },
	{ .compatible = "qcom,sm8250-rpmhpd", .data = &sm8250_desc },
	{ .compatible = "qcom,sm8350-rpmhpd", .data = &sm8350_desc },
	{ }
};
MODULE_DEVICE_TABLE(of, rpmhpd_match_table);

static int rpmhpd_send_corner(struct rpmhpd *pd, int state,
			      unsigned int corner, bool sync)
{
	struct tcs_cmd cmd = {
		.addr = pd->addr,
		.data = corner,
	};

	/*
	 * Wait for an ack only when we are increasing the
	 * perf state of the power domain
	 */
	if (sync)
		return rpmh_write(pd->dev, state, &cmd, 1);
	else
		return rpmh_write_async(pd->dev, state, &cmd, 1);
}

static void to_active_sleep(struct rpmhpd *pd, unsigned int corner,
			    unsigned int *active, unsigned int *sleep)
{
	*active = corner;

	if (pd->active_only)
		*sleep = 0;
	else
		*sleep = *active;
}

/*
 * This function is used to aggregate the votes across the active only
 * resources and its peers. The aggregated votes are sent to RPMh as
 * ACTIVE_ONLY votes (which take effect immediately), as WAKE_ONLY votes
 * (applied by RPMh on system wakeup) and as SLEEP votes (applied by RPMh
 * on system sleep).
 * We send ACTIVE_ONLY votes for resources without any peers. For others,
 * which have an active only peer, all 3 votes are sent.
 */
static int rpmhpd_aggregate_corner(struct rpmhpd *pd, unsigned int corner)
{
	int ret;
	struct rpmhpd *peer = pd->peer;
	unsigned int active_corner, sleep_corner;
	unsigned int this_active_corner = 0, this_sleep_corner = 0;
	unsigned int peer_active_corner = 0, peer_sleep_corner = 0;

	to_active_sleep(pd, corner, &this_active_corner, &this_sleep_corner);

	if (peer && peer->enabled)
		to_active_sleep(peer, peer->corner, &peer_active_corner,
				&peer_sleep_corner);

	active_corner = max(this_active_corner, peer_active_corner);

	ret = rpmhpd_send_corner(pd, RPMH_ACTIVE_ONLY_STATE, active_corner,
				 active_corner > pd->active_corner);
	if (ret)
		return ret;

	pd->active_corner = active_corner;

	if (peer) {
		peer->active_corner = active_corner;

		ret = rpmhpd_send_corner(pd, RPMH_WAKE_ONLY_STATE,
					 active_corner, false);
		if (ret)
			return ret;

		sleep_corner = max(this_sleep_corner, peer_sleep_corner);

		return rpmhpd_send_corner(pd, RPMH_SLEEP_STATE, sleep_corner,
					  false);
	}

	return ret;
}

static int rpmhpd_power_on(struct generic_pm_domain *domain)
{
	struct rpmhpd *pd = domain_to_rpmhpd(domain);
	unsigned int corner;
	int ret;

	mutex_lock(&rpmhpd_lock);

	corner = max(pd->corner, pd->enable_corner);
	ret = rpmhpd_aggregate_corner(pd, corner);
	if (!ret)
		pd->enabled = true;

	mutex_unlock(&rpmhpd_lock);

	return ret;
}

static int rpmhpd_power_off(struct generic_pm_domain *domain)
{
	struct rpmhpd *pd = domain_to_rpmhpd(domain);
	int ret;

	mutex_lock(&rpmhpd_lock);

	ret = rpmhpd_aggregate_corner(pd, 0);
	if (!ret)
		pd->enabled = false;

	mutex_unlock(&rpmhpd_lock);

	return ret;
}

static int rpmhpd_set_performance_state(struct generic_pm_domain *domain,
					unsigned int level)
{
	struct rpmhpd *pd = domain_to_rpmhpd(domain);
	int ret = 0, i;

	mutex_lock(&rpmhpd_lock);

	for (i = 0; i < pd->level_count; i++)
		if (level <= pd->level[i])
			break;

	/*
	 * If the level requested is more than that supported by the
	 * max corner, just set it to max anyway.
	 */
	if (i == pd->level_count)
		i--;

	if (pd->enabled) {
		/* Ensure that the domain isn't turn off */
		if (i < pd->enable_corner)
			i = pd->enable_corner;

		ret = rpmhpd_aggregate_corner(pd, i);
		if (ret)
			goto out;
	}

	pd->corner = i;
out:
	mutex_unlock(&rpmhpd_lock);

	return ret;
}

static unsigned int rpmhpd_get_performance_state(struct generic_pm_domain *genpd,
						 struct dev_pm_opp *opp)
{
	return dev_pm_opp_get_level(opp);
}

static int rpmhpd_update_level_mapping(struct rpmhpd *rpmhpd)
{
	int i;
	const u16 *buf;

	buf = cmd_db_read_aux_data(rpmhpd->res_name, &rpmhpd->level_count);
	if (IS_ERR(buf))
		return PTR_ERR(buf);

	/* 2 bytes used for each command DB aux data entry */
	rpmhpd->level_count >>= 1;

	if (rpmhpd->level_count > RPMH_ARC_MAX_LEVELS)
		return -EINVAL;

	for (i = 0; i < rpmhpd->level_count; i++) {
		rpmhpd->level[i] = buf[i];

		/* Remember the first corner with non-zero level */
		if (!rpmhpd->level[rpmhpd->enable_corner] && rpmhpd->level[i])
			rpmhpd->enable_corner = i;

		/*
		 * The AUX data may be zero padded.  These 0 valued entries at
		 * the end of the map must be ignored.
		 */
		if (i > 0 && rpmhpd->level[i] == 0) {
			rpmhpd->level_count = i;
			break;
		}
		pr_debug("%s: ARC hlvl=%2d --> vlvl=%4u\n", rpmhpd->res_name, i,
			 rpmhpd->level[i]);
	}

	return 0;
}

static int rpmhpd_probe(struct platform_device *pdev)
{
	int i, ret;
	size_t num_pds;
	struct device *dev = &pdev->dev;
	struct genpd_onecell_data *data;
	struct rpmhpd **rpmhpds;
	const struct rpmhpd_desc *desc;

	desc = of_device_get_match_data(dev);
	if (!desc)
		return -EINVAL;

	rpmhpds = desc->rpmhpds;
	num_pds = desc->num_pds;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->domains = devm_kcalloc(dev, num_pds, sizeof(*data->domains),
				     GFP_KERNEL);
	if (!data->domains)
		return -ENOMEM;

	data->num_domains = num_pds;

	for (i = 0; i < num_pds; i++) {
		if (!rpmhpds[i]) {
			dev_warn(dev, "rpmhpds[%d] is empty\n", i);
			continue;
		}

		rpmhpds[i]->dev = dev;
		rpmhpds[i]->addr = cmd_db_read_addr(rpmhpds[i]->res_name);
		if (!rpmhpds[i]->addr) {
			dev_err(dev, "Could not find RPMh address for resource %s\n",
				rpmhpds[i]->res_name);
			return -ENODEV;
		}

		ret = cmd_db_read_slave_id(rpmhpds[i]->res_name);
		if (ret != CMD_DB_HW_ARC) {
			dev_err(dev, "RPMh slave ID mismatch\n");
			return -EINVAL;
		}

		ret = rpmhpd_update_level_mapping(rpmhpds[i]);
		if (ret)
			return ret;

		rpmhpds[i]->pd.power_off = rpmhpd_power_off;
		rpmhpds[i]->pd.power_on = rpmhpd_power_on;
		rpmhpds[i]->pd.set_performance_state = rpmhpd_set_performance_state;
		rpmhpds[i]->pd.opp_to_performance_state = rpmhpd_get_performance_state;
		pm_genpd_init(&rpmhpds[i]->pd, NULL, true);

		data->domains[i] = &rpmhpds[i]->pd;
	}

	/* Add subdomains */
	for (i = 0; i < num_pds; i++) {
		if (!rpmhpds[i])
			continue;
		if (rpmhpds[i]->parent)
			pm_genpd_add_subdomain(rpmhpds[i]->parent,
					       &rpmhpds[i]->pd);
	}

	return of_genpd_add_provider_onecell(pdev->dev.of_node, data);
}

static struct platform_driver rpmhpd_driver = {
	.driver = {
		.name = "qcom-rpmhpd",
		.of_match_table = rpmhpd_match_table,
		.suppress_bind_attrs = true,
	},
	.probe = rpmhpd_probe,
};

static int __init rpmhpd_init(void)
{
	return platform_driver_register(&rpmhpd_driver);
}
core_initcall(rpmhpd_init);

MODULE_DESCRIPTION("Qualcomm Technologies, Inc. RPMh Power Domain Driver");
MODULE_LICENSE("GPL v2");
