/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2018-2020, Intel Corporation. */

#ifndef _ICE_FLTR_H_
#define _ICE_FLTR_H_

void ice_fltr_free_list(struct device *dev, struct list_head *h);
enum ice_status
ice_fltr_add_mac_to_list(struct ice_vsi *vsi, struct list_head *list,
			 const u8 *mac, enum ice_sw_fwd_act_type action);
enum ice_status
ice_fltr_add_mac(struct ice_vsi *vsi, const u8 *mac,
		 enum ice_sw_fwd_act_type action);
enum ice_status
ice_fltr_add_mac_and_broadcast(struct ice_vsi *vsi, const u8 *mac,
			       enum ice_sw_fwd_act_type action);
enum ice_status
ice_fltr_add_mac_list(struct ice_vsi *vsi, struct list_head *list);
enum ice_status
ice_fltr_remove_mac(struct ice_vsi *vsi, const u8 *mac,
		    enum ice_sw_fwd_act_type action);
enum ice_status
ice_fltr_remove_mac_list(struct ice_vsi *vsi, struct list_head *list);

enum ice_status
ice_fltr_add_vlan(struct ice_vsi *vsi, u16 vid,
		  enum ice_sw_fwd_act_type action);
enum ice_status
ice_fltr_remove_vlan(struct ice_vsi *vsi, u16 vid,
		     enum ice_sw_fwd_act_type action);

enum ice_status
ice_fltr_add_eth(struct ice_vsi *vsi, u16 ethertype, u16 flag,
		 enum ice_sw_fwd_act_type action);
enum ice_status
ice_fltr_remove_eth(struct ice_vsi *vsi, u16 ethertype, u16 flag,
		    enum ice_sw_fwd_act_type action);
void ice_fltr_remove_all(struct ice_vsi *vsi);
#endif
