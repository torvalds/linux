/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2020 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef _ATH12K_THERMAL_
#define _ATH12K_THERMAL_

#include <linux/mutex.h>

#define ATH12K_THERMAL_SYNC_TIMEOUT_HZ (5 * HZ)

#define ATH12K_THERMAL_DEFAULT_DUTY_CYCLE 100
#define ATH12K_THERMAL_THROTTLE_MAX 100

enum ath12k_thermal_cfg_idx {
	/* Internal Power Amplifier Device */
	ATH12K_TT_CFG_IDX_IPA,
	/* External Power Amplifier Device or External Front End Module */
	ATH12K_TT_CFG_IDX_XFEM,
	ATH12K_TT_CFG_IDX_MAX,
};

struct ath12k_thermal {
	struct completion wmi_sync;

	/* temperature value in Celsius degree protected by data_lock. */
	int temperature;
	struct device *hwmon_dev;
	const struct ath12k_wmi_tt_level_config_param *tt_level_configs;
	struct thermal_cooling_device *cdev;
	/* Serialize thermal operations and hwmon reads */
	struct mutex lock;
	u32 throttle_state;
};

#if IS_REACHABLE(CONFIG_THERMAL)
int ath12k_thermal_register(struct ath12k_base *ab);
void ath12k_thermal_unregister(struct ath12k_base *ab);
void ath12k_thermal_event_temperature(struct ath12k *ar, int temperature);
int ath12k_thermal_throttling_config_default(struct ath12k *ar);
void ath12k_thermal_init_configs(struct ath12k *ar);
int ath12k_thermal_set_throttling(struct ath12k *ar, u32 throttle_state);
#else
static inline int ath12k_thermal_register(struct ath12k_base *ab)
{
	return 0;
}

static inline void ath12k_thermal_unregister(struct ath12k_base *ab)
{
}

static inline void ath12k_thermal_event_temperature(struct ath12k *ar,
						    int temperature)
{
}

static inline int ath12k_thermal_throttling_config_default(struct ath12k *ar)
{
	return 0;
}

static inline void ath12k_thermal_init_configs(struct ath12k *ar)
{
}

static inline int ath12k_thermal_set_throttling(struct ath12k *ar,
						u32 throttle_state)
{
	return 0;
}
#endif
#endif /* _ATH12K_THERMAL_ */
