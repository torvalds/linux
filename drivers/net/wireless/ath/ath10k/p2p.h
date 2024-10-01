/* SPDX-License-Identifier: ISC */
/*
 * Copyright (c) 2015 Qualcomm Atheros, Inc.
 */

#ifndef _P2P_H
#define _P2P_H

struct ath10k_vif;
struct wmi_p2p_noa_info;

void ath10k_p2p_noa_update(struct ath10k_vif *arvif,
			   const struct wmi_p2p_noa_info *noa);
void ath10k_p2p_noa_update_by_vdev_id(struct ath10k *ar, u32 vdev_id,
				      const struct wmi_p2p_noa_info *noa);

#endif
