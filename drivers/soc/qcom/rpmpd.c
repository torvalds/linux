// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2017-2018, The Linux Foundation. All rights reserved. */

#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/pm_domain.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/soc/qcom/smd-rpm.h>

#include <dt-bindings/power/qcom-rpmpd.h>

#define domain_to_rpmpd(domain) container_of(domain, struct rpmpd, pd)

/* Resource types */
#define RPMPD_SMPA 0x61706d73
#define RPMPD_LDOA 0x616f646c

/* Operation Keys */
#define KEY_CORNER		0x6e726f63 /* corn */
#define KEY_ENABLE		0x6e657773 /* swen */
#define KEY_FLOOR_CORNER	0x636676   /* vfc */

#define MAX_RPMPD_STATE		6

#define DEFINE_RPMPD_CORNER_SMPA(_platform, _name, _active, r_id)		\
	static struct rpmpd _platform##_##_active;			\
	static struct rpmpd _platform##_##_name = {			\
		.pd = {	.name = #_name,	},				\
		.peer = &_platform##_##_active,				\
		.res_type = RPMPD_SMPA,					\
		.res_id = r_id,						\
		.key = KEY_CORNER,					\
	};								\
	static struct rpmpd _platform##_##_active = {			\
		.pd = { .name = #_active, },				\
		.peer = &_platform##_##_name,				\
		.active_only = true,					\
		.res_type = RPMPD_SMPA,					\
		.res_id = r_id,						\
		.key = KEY_CORNER,					\
	}

#define DEFINE_RPMPD_CORNER_LDOA(_platform, _name, r_id)			\
	static struct rpmpd _platform##_##_name = {			\
		.pd = { .name = #_name, },				\
		.res_type = RPMPD_LDOA,					\
		.res_id = r_id,						\
		.key = KEY_CORNER,					\
	}

#define DEFINE_RPMPD_VFC(_platform, _name, r_id, r_type)		\
	static struct rpmpd _platform##_##_name = {			\
		.pd = { .name = #_name, },				\
		.res_type = r_type,					\
		.res_id = r_id,						\
		.key = KEY_FLOOR_CORNER,				\
	}

#define DEFINE_RPMPD_VFC_SMPA(_platform, _name, r_id)			\
	DEFINE_RPMPD_VFC(_platform, _name, r_id, RPMPD_SMPA)

#define DEFINE_RPMPD_VFC_LDOA(_platform, _name, r_id)			\
	DEFINE_RPMPD_VFC(_platform, _name, r_id, RPMPD_LDOA)

struct rpmpd_req {
	__le32 key;
	__le32 nbytes;
	__le32 value;
};

struct rpmpd {
	struct generic_pm_domain pd;
	struct rpmpd *peer;
	const bool active_only;
	unsigned int corner;
	bool enabled;
	const char *res_name;
	const int res_type;
	const int res_id;
	struct qcom_smd_rpm *rpm;
	__le32 key;
};

struct rpmpd_desc {
	struct rpmpd **rpmpds;
	size_t num_pds;
};

static DEFINE_MUTEX(rpmpd_lock);

/* msm8996 RPM Power domains */
DEFINE_RPMPD_CORNER_SMPA(msm8996, vddcx, vddcx_ao, 1);
DEFINE_RPMPD_CORNER_SMPA(msm8996, vddmx, vddmx_ao, 2);
DEFINE_RPMPD_CORNER_LDOA(msm8996, vddsscx, 26);

DEFINE_RPMPD_VFC_SMPA(msm8996, vddcx_vfc, 1);
DEFINE_RPMPD_VFC_LDOA(msm8996, vddsscx_vfc, 26);

static struct rpmpd *msm8996_rpmpds[] = {
	[MSM8996_VDDCX] =	&msm8996_vddcx,
	[MSM8996_VDDCX_AO] =	&msm8996_vddcx_ao,
	[MSM8996_VDDCX_VFC] =	&msm8996_vddcx_vfc,
	[MSM8996_VDDMX] =	&msm8996_vddmx,
	[MSM8996_VDDMX_AO] =	&msm8996_vddmx_ao,
	[MSM8996_VDDSSCX] =	&msm8996_vddsscx,
	[MSM8996_VDDSSCX_VFC] =	&msm8996_vddsscx_vfc,
};

static const struct rpmpd_desc msm8996_desc = {
	.rpmpds = msm8996_rpmpds,
	.num_pds = ARRAY_SIZE(msm8996_rpmpds),
};

static const struct of_device_id rpmpd_match_table[] = {
	{ .compatible = "qcom,msm8996-rpmpd", .data = &msm8996_desc },
	{ }
};

static int rpmpd_send_enable(struct rpmpd *pd, bool enable)
{
	struct rpmpd_req req = {
		.key = KEY_ENABLE,
		.nbytes = cpu_to_le32(sizeof(u32)),
		.value = cpu_to_le32(enable),
	};

	return qcom_rpm_smd_write(pd->rpm, QCOM_SMD_RPM_ACTIVE_STATE,
				  pd->res_type, pd->res_id, &req, sizeof(req));
}

static int rpmpd_send_corner(struct rpmpd *pd, int state, unsigned int corner)
{
	struct rpmpd_req req = {
		.key = pd->key,
		.nbytes = cpu_to_le32(sizeof(u32)),
		.value = cpu_to_le32(corner),
	};

	return qcom_rpm_smd_write(pd->rpm, state, pd->res_type, pd->res_id,
				  &req, sizeof(req));
};

static void to_active_sleep(struct rpmpd *pd, unsigned int corner,
			    unsigned int *active, unsigned int *sleep)
{
	*active = corner;

	if (pd->active_only)
		*sleep = 0;
	else
		*sleep = *active;
}

static int rpmpd_aggregate_corner(struct rpmpd *pd)
{
	int ret;
	struct rpmpd *peer = pd->peer;
	unsigned int active_corner, sleep_corner;
	unsigned int this_active_corner = 0, this_sleep_corner = 0;
	unsigned int peer_active_corner = 0, peer_sleep_corner = 0;

	to_active_sleep(pd, pd->corner, &this_active_corner, &this_sleep_corner);

	if (peer && peer->enabled)
		to_active_sleep(peer, peer->corner, &peer_active_corner,
				&peer_sleep_corner);

	active_corner = max(this_active_corner, peer_active_corner);

	ret = rpmpd_send_corner(pd, QCOM_SMD_RPM_ACTIVE_STATE, active_corner);
	if (ret)
		return ret;

	sleep_corner = max(this_sleep_corner, peer_sleep_corner);

	return rpmpd_send_corner(pd, QCOM_SMD_RPM_SLEEP_STATE, sleep_corner);
}

static int rpmpd_power_on(struct generic_pm_domain *domain)
{
	int ret;
	struct rpmpd *pd = domain_to_rpmpd(domain);

	mutex_lock(&rpmpd_lock);

	ret = rpmpd_send_enable(pd, true);
	if (ret)
		goto out;

	pd->enabled = true;

	if (pd->corner)
		ret = rpmpd_aggregate_corner(pd);

out:
	mutex_unlock(&rpmpd_lock);

	return ret;
}

static int rpmpd_power_off(struct generic_pm_domain *domain)
{
	int ret;
	struct rpmpd *pd = domain_to_rpmpd(domain);

	mutex_lock(&rpmpd_lock);

	ret = rpmpd_send_enable(pd, false);
	if (!ret)
		pd->enabled = false;

	mutex_unlock(&rpmpd_lock);

	return ret;
}

static int rpmpd_set_performance(struct generic_pm_domain *domain,
				 unsigned int state)
{
	int ret = 0;
	struct rpmpd *pd = domain_to_rpmpd(domain);

	if (state > MAX_RPMPD_STATE)
		state = MAX_RPMPD_STATE;

	mutex_lock(&rpmpd_lock);

	pd->corner = state;

	if (!pd->enabled && pd->key != KEY_FLOOR_CORNER)
		goto out;

	ret = rpmpd_aggregate_corner(pd);

out:
	mutex_unlock(&rpmpd_lock);

	return ret;
}

static unsigned int rpmpd_get_performance(struct generic_pm_domain *genpd,
					  struct dev_pm_opp *opp)
{
	return dev_pm_opp_get_level(opp);
}

static int rpmpd_probe(struct platform_device *pdev)
{
	int i;
	size_t num;
	struct genpd_onecell_data *data;
	struct qcom_smd_rpm *rpm;
	struct rpmpd **rpmpds;
	const struct rpmpd_desc *desc;

	rpm = dev_get_drvdata(pdev->dev.parent);
	if (!rpm) {
		dev_err(&pdev->dev, "Unable to retrieve handle to RPM\n");
		return -ENODEV;
	}

	desc = of_device_get_match_data(&pdev->dev);
	if (!desc)
		return -EINVAL;

	rpmpds = desc->rpmpds;
	num = desc->num_pds;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->domains = devm_kcalloc(&pdev->dev, num, sizeof(*data->domains),
				     GFP_KERNEL);
	data->num_domains = num;

	for (i = 0; i < num; i++) {
		if (!rpmpds[i]) {
			dev_warn(&pdev->dev, "rpmpds[] with empty entry at index=%d\n",
				 i);
			continue;
		}

		rpmpds[i]->rpm = rpm;
		rpmpds[i]->pd.power_off = rpmpd_power_off;
		rpmpds[i]->pd.power_on = rpmpd_power_on;
		rpmpds[i]->pd.set_performance_state = rpmpd_set_performance;
		rpmpds[i]->pd.opp_to_performance_state = rpmpd_get_performance;
		pm_genpd_init(&rpmpds[i]->pd, NULL, true);

		data->domains[i] = &rpmpds[i]->pd;
	}

	return of_genpd_add_provider_onecell(pdev->dev.of_node, data);
}

static struct platform_driver rpmpd_driver = {
	.driver = {
		.name = "qcom-rpmpd",
		.of_match_table = rpmpd_match_table,
		.suppress_bind_attrs = true,
	},
	.probe = rpmpd_probe,
};

static int __init rpmpd_init(void)
{
	return platform_driver_register(&rpmpd_driver);
}
core_initcall(rpmpd_init);
