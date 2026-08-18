// SPDX-License-Identifier: GPL-2.0

#include <linux/device.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pm_opp.h>

#include "tegra-emc-common.h"

/**
 * tegra_emc_rate_requests_init() - Initialize EMC rate request tracking
 * @reqs: struct tegra_emc_rate_requests to initialize.
 * @dev: EMC device.
 *
 * Initializes the rate request tracking state with default state
 * (no active requests). Must be called before using @reqs with
 * other functions.
 */
void tegra_emc_rate_requests_init(struct tegra_emc_rate_requests *reqs,
				  struct device *dev)
{
	unsigned int i;

	mutex_init(&reqs->rate_lock);
	reqs->dev = dev;

	for (i = 0; i < TEGRA_EMC_RATE_TYPE_MAX; i++) {
		reqs->requested_rate[i].min_rate = 0;
		reqs->requested_rate[i].max_rate = ULONG_MAX;
	}
}
EXPORT_SYMBOL_GPL(tegra_emc_rate_requests_init);

/* Caller must hold reqs->rate_lock. */
static int tegra_emc_request_rate(struct tegra_emc_rate_requests *reqs,
				  unsigned long new_min_rate,
				  unsigned long new_max_rate,
				  enum tegra_emc_rate_request_type type)
{
	struct tegra_emc_rate_request *req = reqs->requested_rate;
	unsigned long min_rate = 0, max_rate = ULONG_MAX;
	unsigned int i;
	int err;

	lockdep_assert_held(&reqs->rate_lock);

	/* select minimum and maximum rates among the requested rates */
	for (i = 0; i < TEGRA_EMC_RATE_TYPE_MAX; i++, req++) {
		if (i == type) {
			min_rate = max(new_min_rate, min_rate);
			max_rate = min(new_max_rate, max_rate);
		} else {
			min_rate = max(req->min_rate, min_rate);
			max_rate = min(req->max_rate, max_rate);
		}
	}

	if (min_rate > max_rate) {
		dev_err_ratelimited(reqs->dev, "%s: type %u: out of range: %lu %lu\n",
				    __func__, type, min_rate, max_rate);
		return -ERANGE;
	}

	/*
	 * EMC rate-changes should go via OPP API because it manages voltage
	 * changes.
	 */
	err = dev_pm_opp_set_rate(reqs->dev, min_rate);
	if (err)
		return err;

	reqs->requested_rate[type].min_rate = new_min_rate;
	reqs->requested_rate[type].max_rate = new_max_rate;

	return 0;
}

/**
 * tegra_emc_set_min_rate() - Update minimum rate request for a request type
 * @reqs: rate request tracking state
 * @rate: new minimum rate in Hz requested by @type
 * @type: type of request
 *
 * Records @rate as the new minimum rate request for @type, recalculates target
 * rate based on all requests and applies new rate through the OPP API.
 *
 * Context: Sleeps. Requests to same @reqs are synchronized via mutex.
 *
 * Return:
 * * %0 - success
 * * %-ERANGE - request would cause minimum rate request to be higher than
 *              maximum rate request
 * * other - setting new rate failed
 */
int tegra_emc_set_min_rate(struct tegra_emc_rate_requests *reqs,
			   unsigned long rate,
			   enum tegra_emc_rate_request_type type)
{
	struct tegra_emc_rate_request *req = &reqs->requested_rate[type];
	int ret;

	mutex_lock(&reqs->rate_lock);
	ret = tegra_emc_request_rate(reqs, rate, req->max_rate, type);
	mutex_unlock(&reqs->rate_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(tegra_emc_set_min_rate);

/**
 * tegra_emc_set_max_rate() - Update maximum rate request for a request type
 * @reqs: rate request tracking state
 * @rate: new maximum rate in Hz requested by @type
 * @type: type of request
 *
 * Records @rate as the new maximum rate request for @type, recalculates target
 * rate based on all requests and applies new rate through the OPP API.
 *
 * Context: Sleeps. Requests to same @reqs are synchronized via mutex.
 *
 * Return:
 * * %0 - success
 * * %-ERANGE - request would cause minimum rate request to be higher than
 *              maximum rate request
 * * other - setting new rate failed
 */
int tegra_emc_set_max_rate(struct tegra_emc_rate_requests *reqs,
			   unsigned long rate,
			   enum tegra_emc_rate_request_type type)
{
	struct tegra_emc_rate_request *req = &reqs->requested_rate[type];
	int ret;

	mutex_lock(&reqs->rate_lock);
	ret = tegra_emc_request_rate(reqs, req->min_rate, rate, type);
	mutex_unlock(&reqs->rate_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(tegra_emc_set_max_rate);

MODULE_DESCRIPTION("NVIDIA Tegra EMC common code");
MODULE_LICENSE("GPL");
