/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018, Intel Corporation. */

#ifndef _ICE_LIB_H_
#define _ICE_LIB_H_

#include "ice.h"

struct ice_txq_meta {
	/* Tx-scheduler element identifier */
	u32 q_teid;
	/* Entry in VSI's txq_map bitmap */
	u16 q_id;
	/* Relative index of Tx queue within TC */
	u16 q_handle;
	/* VSI index that Tx queue belongs to */
	u16 vsi_idx;
	/* TC number that Tx queue belongs to */
	u8 tc;
};

int
ice_add_mac_to_list(struct ice_vsi *vsi, struct list_head *add_list,
		    const u8 *macaddr);

void ice_free_fltr_list(struct device *dev, struct list_head *h);

void ice_update_eth_stats(struct ice_vsi *vsi);

int ice_vsi_cfg_rxqs(struct ice_vsi *vsi);

int ice_vsi_cfg_lan_txqs(struct ice_vsi *vsi);

void ice_vsi_cfg_msix(struct ice_vsi *vsi);

#ifdef CONFIG_PCI_IOV
void
ice_cfg_txq_interrupt(struct ice_vsi *vsi, u16 txq, u16 msix_idx, u16 itr_idx);

void
ice_cfg_rxq_interrupt(struct ice_vsi *vsi, u16 rxq, u16 msix_idx, u16 itr_idx);

int
ice_vsi_stop_tx_ring(struct ice_vsi *vsi, enum ice_disq_rst_src rst_src,
		     u16 rel_vmvf_num, struct ice_ring *ring,
		     struct ice_txq_meta *txq_meta);

void ice_fill_txq_meta(struct ice_vsi *vsi, struct ice_ring *ring,
		       struct ice_txq_meta *txq_meta);

int ice_vsi_ctrl_rx_ring(struct ice_vsi *vsi, bool ena, u16 rxq_idx);
#endif /* CONFIG_PCI_IOV */

int ice_vsi_add_vlan(struct ice_vsi *vsi, u16 vid);

int ice_vsi_kill_vlan(struct ice_vsi *vsi, u16 vid);

int ice_vsi_manage_vlan_insertion(struct ice_vsi *vsi);

int ice_vsi_manage_vlan_stripping(struct ice_vsi *vsi, bool ena);

int ice_vsi_start_rx_rings(struct ice_vsi *vsi);

int ice_vsi_stop_rx_rings(struct ice_vsi *vsi);

int
ice_vsi_stop_lan_tx_rings(struct ice_vsi *vsi, enum ice_disq_rst_src rst_src,
			  u16 rel_vmvf_num);

int ice_cfg_vlan_pruning(struct ice_vsi *vsi, bool ena, bool vlan_promisc);

void ice_cfg_sw_lldp(struct ice_vsi *vsi, bool tx, bool create);

void ice_vsi_delete(struct ice_vsi *vsi);

int ice_vsi_clear(struct ice_vsi *vsi);

#ifdef CONFIG_DCB
int ice_vsi_cfg_tc(struct ice_vsi *vsi, u8 ena_tc);
#endif /* CONFIG_DCB */

struct ice_vsi *
ice_vsi_setup(struct ice_pf *pf, struct ice_port_info *pi,
	      enum ice_vsi_type type, u16 vf_id);

void ice_napi_del(struct ice_vsi *vsi);

int ice_vsi_release(struct ice_vsi *vsi);

void ice_vsi_close(struct ice_vsi *vsi);

int ice_free_res(struct ice_res_tracker *res, u16 index, u16 id);

int
ice_get_res(struct ice_pf *pf, struct ice_res_tracker *res, u16 needed, u16 id);

int ice_vsi_rebuild(struct ice_vsi *vsi);

bool ice_is_reset_in_progress(unsigned long *state);

void ice_vsi_free_q_vectors(struct ice_vsi *vsi);

void ice_trigger_sw_intr(struct ice_hw *hw, struct ice_q_vector *q_vector);

void ice_vsi_put_qs(struct ice_vsi *vsi);

#ifdef CONFIG_DCB
void ice_vsi_map_rings_to_vectors(struct ice_vsi *vsi);
#endif /* CONFIG_DCB */

void ice_vsi_dis_irq(struct ice_vsi *vsi);

void ice_vsi_free_irq(struct ice_vsi *vsi);

void ice_vsi_free_rx_rings(struct ice_vsi *vsi);

void ice_vsi_free_tx_rings(struct ice_vsi *vsi);

int ice_vsi_manage_rss_lut(struct ice_vsi *vsi, bool ena);

u32 ice_intrl_usec_to_reg(u8 intrl, u8 gran);

char *ice_nvm_version_str(struct ice_hw *hw);

enum ice_status
ice_vsi_cfg_mac_fltr(struct ice_vsi *vsi, const u8 *macaddr, bool set);

bool ice_is_safe_mode(struct ice_pf *pf);
#endif /* !_ICE_LIB_H_ */
