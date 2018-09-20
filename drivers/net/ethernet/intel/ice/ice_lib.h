/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018, Intel Corporation. */

#ifndef _ICE_LIB_H_
#define _ICE_LIB_H_

#include "ice.h"

int ice_add_mac_to_list(struct ice_vsi *vsi, struct list_head *add_list,
			const u8 *macaddr);

void ice_free_fltr_list(struct device *dev, struct list_head *h);

void ice_update_eth_stats(struct ice_vsi *vsi);

int ice_vsi_cfg_rxqs(struct ice_vsi *vsi);

int ice_vsi_cfg_txqs(struct ice_vsi *vsi);

void ice_vsi_cfg_msix(struct ice_vsi *vsi);

int ice_vsi_add_vlan(struct ice_vsi *vsi, u16 vid);

int ice_vsi_kill_vlan(struct ice_vsi *vsi, u16 vid);

int ice_vsi_manage_vlan_insertion(struct ice_vsi *vsi);

int ice_vsi_manage_vlan_stripping(struct ice_vsi *vsi, bool ena);

int ice_vsi_start_rx_rings(struct ice_vsi *vsi);

int ice_vsi_stop_rx_rings(struct ice_vsi *vsi);

int ice_vsi_stop_tx_rings(struct ice_vsi *vsi);

#endif /* !_ICE_LIB_H_ */
