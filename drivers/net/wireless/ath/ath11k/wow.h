/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2020 The Linux Foundation. All rights reserved.
 */

#define ATH11K_WOW_RETRY_NUM		3
#define ATH11K_WOW_RETRY_WAIT_MS	200

int ath11k_wow_enable(struct ath11k_base *ab);
int ath11k_wow_wakeup(struct ath11k_base *ab);
