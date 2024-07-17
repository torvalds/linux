/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef ATH11K_P2P_H
#define ATH11K_P2P_H

#include "wmi.h"

struct ath11k_wmi_p2p_noa_info;

struct ath11k_p2p_noa_arg {
	u32 vdev_id;
	const struct ath11k_wmi_p2p_noa_info *noa;
};

void ath11k_p2p_noa_update(struct ath11k_vif *arvif,
			   const struct ath11k_wmi_p2p_noa_info *noa);
void ath11k_p2p_noa_update_by_vdev_id(struct ath11k *ar, u32 vdev_id,
				      const struct ath11k_wmi_p2p_noa_info *noa);
#endif
