/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved..
 */

#ifndef ATH12K_P2P_H
#define ATH12K_P2P_H

#include "wmi.h"

struct ath12k_wmi_p2p_noa_info;

struct ath12k_p2p_noa_arg {
	u32 vdev_id;
	struct ath12k *ar;
	const struct ath12k_wmi_p2p_noa_info *noa;
};

void ath12k_p2p_noa_update(struct ath12k_vif *arvif,
			   const struct ath12k_wmi_p2p_noa_info *noa);
void ath12k_p2p_noa_update_by_vdev_id(struct ath12k *ar, u32 vdev_id,
				      const struct ath12k_wmi_p2p_noa_info *noa);

#endif
