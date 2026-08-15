/* SPDX-License-Identifier: GPL-2.0 */

#ifndef TEGRA_EMC_COMMON_H
#define TEGRA_EMC_COMMON_H

#include <linux/device.h>
#include <linux/mutex.h>

/**
 * enum tegra_emc_rate_request_type - source of rate request
 * @TEGRA_EMC_RATE_DEVFREQ: rate requested by devfreq governor
 * @TEGRA_EMC_RATE_DEBUG: rate requested through debugfs knobs
 * @TEGRA_EMC_RATE_ICC: rate requested by ICC framework
 * @TEGRA_EMC_RATE_TYPE_MAX: number of valid request types
 */
enum tegra_emc_rate_request_type {
	TEGRA_EMC_RATE_DEVFREQ,
	TEGRA_EMC_RATE_DEBUG,
	TEGRA_EMC_RATE_ICC,
	TEGRA_EMC_RATE_TYPE_MAX,
};

struct tegra_emc_rate_request {
	unsigned long min_rate;
	unsigned long max_rate;
};

struct tegra_emc_rate_requests {
	struct tegra_emc_rate_request requested_rate[TEGRA_EMC_RATE_TYPE_MAX];
	/* Protects @requested_rate. */
	struct mutex rate_lock;
	struct device *dev;
};

void tegra_emc_rate_requests_init(struct tegra_emc_rate_requests *reqs,
				  struct device *dev);

int tegra_emc_set_min_rate(struct tegra_emc_rate_requests *reqs,
			   unsigned long rate,
			   enum tegra_emc_rate_request_type type);

int tegra_emc_set_max_rate(struct tegra_emc_rate_requests *reqs,
			   unsigned long rate,
			   enum tegra_emc_rate_request_type type);

#endif /* TEGRA_EMC_COMMON_H */
