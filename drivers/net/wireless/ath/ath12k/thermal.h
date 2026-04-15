/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2020 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef _ATH12K_THERMAL_
#define _ATH12K_THERMAL_

#define ATH12K_THERMAL_SYNC_TIMEOUT_HZ (5 * HZ)

struct ath12k_thermal {
	struct completion wmi_sync;

	/* temperature value in Celsius degree protected by data_lock. */
	int temperature;
	struct device *hwmon_dev;
};

#if IS_REACHABLE(CONFIG_THERMAL)
int ath12k_thermal_register(struct ath12k_base *ab);
void ath12k_thermal_unregister(struct ath12k_base *ab);
void ath12k_thermal_event_temperature(struct ath12k *ar, int temperature);
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

#endif
#endif /* _ATH12K_THERMAL_ */
