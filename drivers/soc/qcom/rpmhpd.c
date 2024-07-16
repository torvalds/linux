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
 * @pd:			generic_pm_domain corresponding to the power domain
 * @parent:		generic_pm_domain corresponding to the parent's power domain
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

/* RPMH powerdomains */

static struct rpmhpd cx_ao;
static struct rpmhpd mx;
static struct rpmhpd mx_ao;
static struct rpmhpd cx = {
	.pd = { .name = "cx", },
	.peer = &cx_ao,
	.res_name = "cx.lvl",
};

static struct rpmhpd cx_ao = {
	.pd = { .name = "cx_ao", },
	.active_only = true,
	.peer = &cx,
	.res_name = "cx.lvl",
};

static struct rpmhpd cx_ao_w_mx_parent;
static struct rpmhpd cx_w_mx_parent = {
	.pd = { .name = "cx", },
	.peer = &cx_ao_w_mx_parent,
	.parent = &mx.pd,
	.res_name = "cx.lvl",
};

static struct rpmhpd cx_ao_w_mx_parent = {
	.pd = { .name = "cx_ao", },
	.active_only = true,
	.peer = &cx_w_mx_parent,
	.parent = &mx_ao.pd,
	.res_name = "cx.lvl",
};

static struct rpmhpd ebi = {
	.pd = { .name = "ebi", },
	.res_name = "ebi.lvl",
};

static struct rpmhpd gfx = {
	.pd = { .name = "gfx", },
	.res_name = "gfx.lvl",
};

static struct rpmhpd lcx = {
	.pd = { .name = "lcx", },
	.res_name = "lcx.lvl",
};

static struct rpmhpd lmx = {
	.pd = { .name = "lmx", },
	.res_name = "lmx.lvl",
};

static struct rpmhpd mmcx_ao;
static struct rpmhpd mmcx = {
	.pd = { .name = "mmcx", },
	.peer = &mmcx_ao,
	.res_name = "mmcx.lvl",
};

static struct rpmhpd mmcx_ao = {
	.pd = { .name = "mmcx_ao", },
	.active_only = true,
	.peer = &mmcx,
	.res_name = "mmcx.lvl",
};

static struct rpmhpd mmcx_ao_w_cx_parent;
static struct rpmhpd mmcx_w_cx_parent = {
	.pd = { .name = "mmcx", },
	.peer = &mmcx_ao_w_cx_parent,
	.parent = &cx.pd,
	.res_name = "mmcx.lvl",
};

static struct rpmhpd mmcx_ao_w_cx_parent = {
	.pd = { .name = "mmcx_ao", },
	.active_only = true,
	.peer = &mmcx_w_cx_parent,
	.parent = &cx_ao.pd,
	.res_name = "mmcx.lvl",
};

static struct rpmhpd mss = {
	.pd = { .name = "mss", },
	.res_name = "mss.lvl",
};

static struct rpmhpd mx_ao;
static struct rpmhpd mx = {
	.pd = { .name = "mx", },
	.peer = &mx_ao,
	.res_name = "mx.lvl",
};

static struct rpmhpd mx_ao = {
	.pd = { .name = "mx_ao", },
	.active_only = true,
	.peer = &mx,
	.res_name = "mx.lvl",
};

static struct rpmhpd mxc_ao;
static struct rpmhpd mxc = {
	.pd = { .name = "mxc", },
	.peer = &mxc_ao,
	.res_name = "mxc.lvl",
};

static struct rpmhpd mxc_ao = {
	.pd = { .name = "mxc_ao", },
	.active_only = true,
	.peer = &mxc,
	.res_name = "mxc.lvl",
};

static struct rpmhpd nsp = {
	.pd = { .name = "nsp", },
	.res_name = "nsp.lvl",
};

static struct rpmhpd qphy = {
	.pd = { .name = "qphy", },
	.res_name = "qphy.lvl",
};

/* SA8540P RPMH powerdomains */
static struct rpmhpd *sa8540p_rpmhpds[] = {
	[SC8280XP_CX] = &cx,
	[SC8280XP_CX_AO] = &cx_ao,
	[SC8280XP_EBI] = &ebi,
	[SC8280XP_LCX] = &lcx,
	[SC8280XP_LMX] = &lmx,
	[SC8280XP_MMCX] = &mmcx,
	[SC8280XP_MMCX_AO] = &mmcx_ao,
	[SC8280XP_MX] = &mx,
	[SC8280XP_MX_AO] = &mx_ao,
	[SC8280XP_NSP] = &nsp,
};

static const struct rpmhpd_desc sa8540p_desc = {
	.rpmhpds = sa8540p_rpmhpds,
	.num_pds = ARRAY_SIZE(sa8540p_rpmhpds),
};

/* SDM845 RPMH powerdomains */
static struct rpmhpd *sdm845_rpmhpds[] = {
	[SDM845_CX] = &cx_w_mx_parent,
	[SDM845_CX_AO] = &cx_ao_w_mx_parent,
	[SDM845_EBI] = &ebi,
	[SDM845_GFX] = &gfx,
	[SDM845_LCX] = &lcx,
	[SDM845_LMX] = &lmx,
	[SDM845_MSS] = &mss,
	[SDM845_MX] = &mx,
	[SDM845_MX_AO] = &mx_ao,
};

static const struct rpmhpd_desc sdm845_desc = {
	.rpmhpds = sdm845_rpmhpds,
	.num_pds = ARRAY_SIZE(sdm845_rpmhpds),
};

/* SDX55 RPMH powerdomains */
static struct rpmhpd *sdx55_rpmhpds[] = {
	[SDX55_CX] = &cx_w_mx_parent,
	[SDX55_MSS] = &mss,
	[SDX55_MX] = &mx,
};

static const struct rpmhpd_desc sdx55_desc = {
	.rpmhpds = sdx55_rpmhpds,
	.num_pds = ARRAY_SIZE(sdx55_rpmhpds),
};

/* SDX65 RPMH powerdomains */
static struct rpmhpd *sdx65_rpmhpds[] = {
	[SDX65_CX] = &cx_w_mx_parent,
	[SDX65_CX_AO] = &cx_ao_w_mx_parent,
	[SDX65_MSS] = &mss,
	[SDX65_MX] = &mx,
	[SDX65_MX_AO] = &mx_ao,
	[SDX65_MXC] = &mxc,
};

static const struct rpmhpd_desc sdx65_desc = {
	.rpmhpds = sdx65_rpmhpds,
	.num_pds = ARRAY_SIZE(sdx65_rpmhpds),
};

/* SM6350 RPMH powerdomains */
static struct rpmhpd *sm6350_rpmhpds[] = {
	[SM6350_CX] = &cx_w_mx_parent,
	[SM6350_GFX] = &gfx,
	[SM6350_LCX] = &lcx,
	[SM6350_LMX] = &lmx,
	[SM6350_MSS] = &mss,
	[SM6350_MX] = &mx,
};

static const struct rpmhpd_desc sm6350_desc = {
	.rpmhpds = sm6350_rpmhpds,
	.num_pds = ARRAY_SIZE(sm6350_rpmhpds),
};

/* SM8150 RPMH powerdomains */
static struct rpmhpd *sm8150_rpmhpds[] = {
	[SM8150_CX] = &cx_w_mx_parent,
	[SM8150_CX_AO] = &cx_ao_w_mx_parent,
	[SM8150_EBI] = &ebi,
	[SM8150_GFX] = &gfx,
	[SM8150_LCX] = &lcx,
	[SM8150_LMX] = &lmx,
	[SM8150_MMCX] = &mmcx,
	[SM8150_MMCX_AO] = &mmcx_ao,
	[SM8150_MSS] = &mss,
	[SM8150_MX] = &mx,
	[SM8150_MX_AO] = &mx_ao,
};

static const struct rpmhpd_desc sm8150_desc = {
	.rpmhpds = sm8150_rpmhpds,
	.num_pds = ARRAY_SIZE(sm8150_rpmhpds),
};

/* SM8250 RPMH powerdomains */
static struct rpmhpd *sm8250_rpmhpds[] = {
	[SM8250_CX] = &cx_w_mx_parent,
	[SM8250_CX_AO] = &cx_ao_w_mx_parent,
	[SM8250_EBI] = &ebi,
	[SM8250_GFX] = &gfx,
	[SM8250_LCX] = &lcx,
	[SM8250_LMX] = &lmx,
	[SM8250_MMCX] = &mmcx,
	[SM8250_MMCX_AO] = &mmcx_ao,
	[SM8250_MX] = &mx,
	[SM8250_MX_AO] = &mx_ao,
};

static const struct rpmhpd_desc sm8250_desc = {
	.rpmhpds = sm8250_rpmhpds,
	.num_pds = ARRAY_SIZE(sm8250_rpmhpds),
};

/* SM8350 Power domains */
static struct rpmhpd *sm8350_rpmhpds[] = {
	[SM8350_CX] = &cx_w_mx_parent,
	[SM8350_CX_AO] = &cx_ao_w_mx_parent,
	[SM8350_EBI] = &ebi,
	[SM8350_GFX] = &gfx,
	[SM8350_LCX] = &lcx,
	[SM8350_LMX] = &lmx,
	[SM8350_MMCX] = &mmcx,
	[SM8350_MMCX_AO] = &mmcx_ao,
	[SM8350_MSS] = &mss,
	[SM8350_MX] = &mx,
	[SM8350_MX_AO] = &mx_ao,
	[SM8350_MXC] = &mxc,
	[SM8350_MXC_AO] = &mxc_ao,
};

static const struct rpmhpd_desc sm8350_desc = {
	.rpmhpds = sm8350_rpmhpds,
	.num_pds = ARRAY_SIZE(sm8350_rpmhpds),
};

/* SM8450 RPMH powerdomains */
static struct rpmhpd *sm8450_rpmhpds[] = {
	[SM8450_CX] = &cx,
	[SM8450_CX_AO] = &cx_ao,
	[SM8450_EBI] = &ebi,
	[SM8450_GFX] = &gfx,
	[SM8450_LCX] = &lcx,
	[SM8450_LMX] = &lmx,
	[SM8450_MMCX] = &mmcx_w_cx_parent,
	[SM8450_MMCX_AO] = &mmcx_ao_w_cx_parent,
	[SM8450_MSS] = &mss,
	[SM8450_MX] = &mx,
	[SM8450_MX_AO] = &mx_ao,
	[SM8450_MXC] = &mxc,
	[SM8450_MXC_AO] = &mxc_ao,
};

static const struct rpmhpd_desc sm8450_desc = {
	.rpmhpds = sm8450_rpmhpds,
	.num_pds = ARRAY_SIZE(sm8450_rpmhpds),
};

/* SC7180 RPMH powerdomains */
static struct rpmhpd *sc7180_rpmhpds[] = {
	[SC7180_CX] = &cx_w_mx_parent,
	[SC7180_CX_AO] = &cx_ao_w_mx_parent,
	[SC7180_GFX] = &gfx,
	[SC7180_LCX] = &lcx,
	[SC7180_LMX] = &lmx,
	[SC7180_MSS] = &mss,
	[SC7180_MX] = &mx,
	[SC7180_MX_AO] = &mx_ao,
};

static const struct rpmhpd_desc sc7180_desc = {
	.rpmhpds = sc7180_rpmhpds,
	.num_pds = ARRAY_SIZE(sc7180_rpmhpds),
};

/* SC7280 RPMH powerdomains */
static struct rpmhpd *sc7280_rpmhpds[] = {
	[SC7280_CX] = &cx,
	[SC7280_CX_AO] = &cx_ao,
	[SC7280_EBI] = &ebi,
	[SC7280_GFX] = &gfx,
	[SC7280_LCX] = &lcx,
	[SC7280_LMX] = &lmx,
	[SC7280_MSS] = &mss,
	[SC7280_MX] = &mx,
	[SC7280_MX_AO] = &mx_ao,
};

static const struct rpmhpd_desc sc7280_desc = {
	.rpmhpds = sc7280_rpmhpds,
	.num_pds = ARRAY_SIZE(sc7280_rpmhpds),
};

/* SC8180x RPMH powerdomains */
static struct rpmhpd *sc8180x_rpmhpds[] = {
	[SC8180X_CX] = &cx_w_mx_parent,
	[SC8180X_CX_AO] = &cx_ao_w_mx_parent,
	[SC8180X_EBI] = &ebi,
	[SC8180X_GFX] = &gfx,
	[SC8180X_LCX] = &lcx,
	[SC8180X_LMX] = &lmx,
	[SC8180X_MMCX] = &mmcx,
	[SC8180X_MMCX_AO] = &mmcx_ao,
	[SC8180X_MSS] = &mss,
	[SC8180X_MX] = &mx,
	[SC8180X_MX_AO] = &mx_ao,
};

static const struct rpmhpd_desc sc8180x_desc = {
	.rpmhpds = sc8180x_rpmhpds,
	.num_pds = ARRAY_SIZE(sc8180x_rpmhpds),
};

/* SC8280xp RPMH powerdomains */
static struct rpmhpd *sc8280xp_rpmhpds[] = {
	[SC8280XP_CX] = &cx,
	[SC8280XP_CX_AO] = &cx_ao,
	[SC8280XP_EBI] = &ebi,
	[SC8280XP_GFX] = &gfx,
	[SC8280XP_LCX] = &lcx,
	[SC8280XP_LMX] = &lmx,
	[SC8280XP_MMCX] = &mmcx,
	[SC8280XP_MMCX_AO] = &mmcx_ao,
	[SC8280XP_MX] = &mx,
	[SC8280XP_MX_AO] = &mx_ao,
	[SC8280XP_NSP] = &nsp,
	[SC8280XP_QPHY] = &qphy,
};

static const struct rpmhpd_desc sc8280xp_desc = {
	.rpmhpds = sc8280xp_rpmhpds,
	.num_pds = ARRAY_SIZE(sc8280xp_rpmhpds),
};

static const struct of_device_id rpmhpd_match_table[] = {
	{ .compatible = "qcom,sa8540p-rpmhpd", .data = &sa8540p_desc },
	{ .compatible = "qcom,sc7180-rpmhpd", .data = &sc7180_desc },
	{ .compatible = "qcom,sc7280-rpmhpd", .data = &sc7280_desc },
	{ .compatible = "qcom,sc8180x-rpmhpd", .data = &sc8180x_desc },
	{ .compatible = "qcom,sc8280xp-rpmhpd", .data = &sc8280xp_desc },
	{ .compatible = "qcom,sdm845-rpmhpd", .data = &sdm845_desc },
	{ .compatible = "qcom,sdx55-rpmhpd", .data = &sdx55_desc},
	{ .compatible = "qcom,sdx65-rpmhpd", .data = &sdx65_desc},
	{ .compatible = "qcom,sm6350-rpmhpd", .data = &sm6350_desc },
	{ .compatible = "qcom,sm8150-rpmhpd", .data = &sm8150_desc },
	{ .compatible = "qcom,sm8250-rpmhpd", .data = &sm8250_desc },
	{ .compatible = "qcom,sm8350-rpmhpd", .data = &sm8350_desc },
	{ .compatible = "qcom,sm8450-rpmhpd", .data = &sm8450_desc },
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
	unsigned int peer_enabled_corner;

	to_active_sleep(pd, corner, &this_active_corner, &this_sleep_corner);

	if (peer && peer->enabled) {
		peer_enabled_corner = max(peer->corner, peer->enable_corner);
		to_active_sleep(peer, peer_enabled_corner, &peer_active_corner,
				&peer_sleep_corner);
	}

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
		if (!rpmhpds[i])
			continue;

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
