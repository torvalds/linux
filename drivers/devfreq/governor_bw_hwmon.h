/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2013-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _GOVERNOR_BW_HWMON_H
#define _GOVERNOR_BW_HWMON_H

#include <linux/kernel.h>
#include <linux/devfreq.h>

/**
 * struct bw_hwmon - dev BW HW monitor info
 * @start_hwmon:		Start the HW monitoring of the dev BW
 * @stop_hwmon:			Stop the HW monitoring of dev BW
 * @set_thres:			Set the count threshold to generate an IRQ
 * @get_bytes_and_clear:	Get the bytes transferred since the last call
 *				and reset the counter to start over.
 * @set_throttle_adj:		Set throttle adjust field to the given value
 * @get_throttle_adj:		Get the value written to throttle adjust field
 * @dev:			Pointer to device that this HW monitor can
 *				monitor.
 * @of_node:			OF node of device that this HW monitor can
 *				monitor.
 * @gov:			devfreq_governor struct that should be used
 *				when registering this HW monitor with devfreq.
 *				Only the name field is expected to be
 *				initialized.
 * @df:				Devfreq node that this HW monitor is being
 *				used for. NULL when not actively in use and
 *				non-NULL when in use.
 *
 * One of dev, of_node or governor_name needs to be specified for a
 * successful registration.
 *
 */
struct bw_hwmon {
	int			(*start_hwmon)(struct bw_hwmon *hw,
					unsigned long mbps);
	void			(*stop_hwmon)(struct bw_hwmon *hw);
	int			(*suspend_hwmon)(struct bw_hwmon *hw);
	int			(*resume_hwmon)(struct bw_hwmon *hw);
	unsigned long		(*set_thres)(struct bw_hwmon *hw,
					unsigned long bytes);
	unsigned long		(*set_hw_events)(struct bw_hwmon *hw,
					unsigned int sample_ms);
	unsigned long		(*get_bytes_and_clear)(struct bw_hwmon *hw);
	int			(*set_throttle_adj)(struct bw_hwmon *hw,
					uint adj);
	u32			(*get_throttle_adj)(struct bw_hwmon *hw);
	struct device		*dev;
	struct device_node	*of_node;
	struct devfreq_governor	*gov;
	unsigned long		up_wake_mbps;
	unsigned long		down_wake_mbps;
	unsigned int		down_cnt;
	struct devfreq		*df;
};

#if IS_ENABLED(CONFIG_DEVFREQ_GOV_QCOM_BW_HWMON)
int register_bw_hwmon(struct device *dev, struct bw_hwmon *hwmon);
int update_bw_hwmon(struct bw_hwmon *hwmon);
int bw_hwmon_sample_end(struct bw_hwmon *hwmon);
#else
static inline int register_bw_hwmon(struct device *dev,
					struct bw_hwmon *hwmon)
{
	return 0;
}
static inline int update_bw_hwmon(struct bw_hwmon *hwmon)
{
	return 0;
}
static inline int bw_hwmon_sample_end(struct bw_hwmon *hwmon)
{
	return 0;
}
#endif

#endif /* _GOVERNOR_BW_HWMON_H */
