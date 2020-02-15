/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2020 The Linux Foundation. All rights reserved.
 */

#ifndef _ATH11K_THERMAL_
#define _ATH11K_THERMAL_

#define ATH11K_THERMAL_TEMP_LOW_MARK -100
#define ATH11K_THERMAL_TEMP_HIGH_MARK 150
#define ATH11K_THERMAL_THROTTLE_MAX     100
#define ATH11K_THERMAL_DEFAULT_DUTY_CYCLE 100

struct ath11k_thermal {
	struct thermal_cooling_device *cdev;

	/* protected by conf_mutex */
	u32 throttle_state;
};

#if IS_REACHABLE(CONFIG_THERMAL)
int ath11k_thermal_register(struct ath11k_base *sc);
void ath11k_thermal_unregister(struct ath11k_base *sc);
int ath11k_thermal_set_throttling(struct ath11k *ar, u32 throttle_state);
#else
static inline int ath11k_thermal_register(struct ath11k_base *sc)
{
	return 0;
}

static inline void ath11k_thermal_unregister(struct ath11k *ar)
{
}

static inline int ath11k_thermal_set_throttling(struct ath11k *ar, u32 throttle_state)
{
}

#endif
#endif /* _ATH11K_THERMAL_ */
