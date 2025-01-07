// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2021 Linaro Limited
 *
 * Author: Daniel Lezcano <daniel.lezcano@linaro.org>
 *
 * The devfreq device combined with the energy model and the load can
 * give an estimation of the power consumption as well as limiting the
 * power.
 *
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/cpumask.h>
#include <linux/devfreq.h>
#include <linux/dtpm.h>
#include <linux/energy_model.h>
#include <linux/of.h>
#include <linux/pm_qos.h>
#include <linux/slab.h>
#include <linux/units.h>

struct dtpm_devfreq {
	struct dtpm dtpm;
	struct dev_pm_qos_request qos_req;
	struct devfreq *devfreq;
};

static struct dtpm_devfreq *to_dtpm_devfreq(struct dtpm *dtpm)
{
	return container_of(dtpm, struct dtpm_devfreq, dtpm);
}

static int update_pd_power_uw(struct dtpm *dtpm)
{
	struct dtpm_devfreq *dtpm_devfreq = to_dtpm_devfreq(dtpm);
	struct devfreq *devfreq = dtpm_devfreq->devfreq;
	struct device *dev = devfreq->dev.parent;
	struct em_perf_domain *pd = em_pd_get(dev);

	dtpm->power_min = pd->table[0].power;

	dtpm->power_max = pd->table[pd->nr_perf_states - 1].power;

	return 0;
}

static u64 set_pd_power_limit(struct dtpm *dtpm, u64 power_limit)
{
	struct dtpm_devfreq *dtpm_devfreq = to_dtpm_devfreq(dtpm);
	struct devfreq *devfreq = dtpm_devfreq->devfreq;
	struct device *dev = devfreq->dev.parent;
	struct em_perf_domain *pd = em_pd_get(dev);
	unsigned long freq;
	int i;

	for (i = 0; i < pd->nr_perf_states; i++) {
		if (pd->table[i].power > power_limit)
			break;
	}

	freq = pd->table[i - 1].frequency;

	dev_pm_qos_update_request(&dtpm_devfreq->qos_req, freq);

	power_limit = pd->table[i - 1].power;

	return power_limit;
}

static void _normalize_load(struct devfreq_dev_status *status)
{
	if (status->total_time > 0xfffff) {
		status->total_time >>= 10;
		status->busy_time >>= 10;
	}

	status->busy_time <<= 10;
	status->busy_time /= status->total_time ? : 1;

	status->busy_time = status->busy_time ? : 1;
	status->total_time = 1024;
}

static u64 get_pd_power_uw(struct dtpm *dtpm)
{
	struct dtpm_devfreq *dtpm_devfreq = to_dtpm_devfreq(dtpm);
	struct devfreq *devfreq = dtpm_devfreq->devfreq;
	struct device *dev = devfreq->dev.parent;
	struct em_perf_domain *pd = em_pd_get(dev);
	struct devfreq_dev_status status;
	unsigned long freq;
	u64 power;
	int i;

	mutex_lock(&devfreq->lock);
	status = devfreq->last_status;
	mutex_unlock(&devfreq->lock);

	freq = DIV_ROUND_UP(status.current_frequency, HZ_PER_KHZ);
	_normalize_load(&status);

	for (i = 0; i < pd->nr_perf_states; i++) {

		if (pd->table[i].frequency < freq)
			continue;

		power = pd->table[i].power;
		power *= status.busy_time;
		power >>= 10;

		return power;
	}

	return 0;
}

static void pd_release(struct dtpm *dtpm)
{
	struct dtpm_devfreq *dtpm_devfreq = to_dtpm_devfreq(dtpm);

	if (dev_pm_qos_request_active(&dtpm_devfreq->qos_req))
		dev_pm_qos_remove_request(&dtpm_devfreq->qos_req);

	kfree(dtpm_devfreq);
}

static struct dtpm_ops dtpm_ops = {
	.set_power_uw = set_pd_power_limit,
	.get_power_uw = get_pd_power_uw,
	.update_power_uw = update_pd_power_uw,
	.release = pd_release,
};

static int __dtpm_devfreq_setup(struct devfreq *devfreq, struct dtpm *parent)
{
	struct device *dev = devfreq->dev.parent;
	struct dtpm_devfreq *dtpm_devfreq;
	struct em_perf_domain *pd;
	int ret = -ENOMEM;

	pd = em_pd_get(dev);
	if (!pd) {
		ret = dev_pm_opp_of_register_em(dev, NULL);
		if (ret) {
			pr_err("No energy model available for '%s'\n", dev_name(dev));
			return -EINVAL;
		}
	}

	dtpm_devfreq = kzalloc(sizeof(*dtpm_devfreq), GFP_KERNEL);
	if (!dtpm_devfreq)
		return -ENOMEM;

	dtpm_init(&dtpm_devfreq->dtpm, &dtpm_ops);

	dtpm_devfreq->devfreq = devfreq;

	ret = dtpm_register(dev_name(dev), &dtpm_devfreq->dtpm, parent);
	if (ret) {
		pr_err("Failed to register '%s': %d\n", dev_name(dev), ret);
		kfree(dtpm_devfreq);
		return ret;
	}

	ret = dev_pm_qos_add_request(dev, &dtpm_devfreq->qos_req,
				     DEV_PM_QOS_MAX_FREQUENCY,
				     PM_QOS_MAX_FREQUENCY_DEFAULT_VALUE);
	if (ret < 0) {
		pr_err("Failed to add QoS request: %d\n", ret);
		goto out_dtpm_unregister;
	}

	dtpm_update_power(&dtpm_devfreq->dtpm);

	return 0;

out_dtpm_unregister:
	dtpm_unregister(&dtpm_devfreq->dtpm);

	return ret;
}

static int dtpm_devfreq_setup(struct dtpm *dtpm, struct device_node *np)
{
	struct devfreq *devfreq;

	devfreq = devfreq_get_devfreq_by_node(np);
	if (IS_ERR(devfreq))
		return 0;

	return __dtpm_devfreq_setup(devfreq, dtpm);
}

struct dtpm_subsys_ops dtpm_devfreq_ops = {
	.name = KBUILD_MODNAME,
	.setup = dtpm_devfreq_setup,
};
