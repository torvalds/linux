/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2020 The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _ATH11K_THERMAL_
#define _ATH11K_THERMAL_

#define ATH11K_THERMAL_TEMP_LOW_MARK -100
#define ATH11K_THERMAL_TEMP_HIGH_MARK 150
#define ATH11K_THERMAL_THROTTLE_MAX     100
#define ATH11K_THERMAL_DEFAULT_DUTY_CYCLE 100
#define ATH11K_HWMON_NAME_LEN           15
#define ATH11K_THERMAL_SYNC_TIMEOUT_HZ (5 * HZ)

struct ath11k_thermal {
	struct thermal_cooling_device *cdev;
	struct completion wmi_sync;

	/* protected by conf_mutex */
	u32 throttle_state;
	/* temperature value in Celsius degree
	 * protected by data_lock
	 */
	int temperature;
};

#if IS_REACHABLE(CONFIG_THERMAL)
int ath11k_thermal_register(struct ath11k_base *ab);
void ath11k_thermal_unregister(struct ath11k_base *ab);
int ath11k_thermal_set_throttling(struct ath11k *ar, u32 throttle_state);
void ath11k_thermal_event_temperature(struct ath11k *ar, int temperature);
#else
static inline int ath11k_thermal_register(struct ath11k_base *ab)
{
	return 0;
}

static inline void ath11k_thermal_unregister(struct ath11k_base *ab)
{
}

static inline int ath11k_thermal_set_throttling(struct ath11k *ar, u32 throttle_state)
{
	return 0;
}

static inline void ath11k_thermal_event_temperature(struct ath11k *ar,
						    int temperature)
{
}

#endif
#endif /* _ATH11K_THERMAL_ */
