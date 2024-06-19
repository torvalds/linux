/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2020 The Linux Foundation. All rights reserved.
 * Copyright (c) 2022, 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef ATH12K_WOW_H
#define ATH12K_WOW_H

#define ATH12K_WOW_RETRY_NUM		10
#define ATH12K_WOW_RETRY_WAIT_MS	200

#ifdef CONFIG_PM

int ath12k_wow_enable(struct ath12k *ar);
int ath12k_wow_wakeup(struct ath12k *ar);

#else

static inline int ath12k_wow_enable(struct ath12k *ar)
{
	return 0;
}

static inline int ath12k_wow_wakeup(struct ath12k *ar)
{
	return 0;
}
#endif /* CONFIG_PM */
#endif /* ATH12K_WOW_H */
