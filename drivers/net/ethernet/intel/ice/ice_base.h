/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019, Intel Corporation. */

#ifndef _ICE_BASE_H_
#define _ICE_BASE_H_

#include "ice.h"

int ice_vsi_cfg_rxq(struct ice_rx_ring *ring);
int __ice_vsi_get_qs(struct ice_qs_cfg *qs_cfg);
int
ice_vsi_ctrl_one_rx_ring(struct ice_vsi *vsi, bool ena, u16 rxq_idx, bool wait);
int ice_vsi_wait_one_rx_ring(struct ice_vsi *vsi, bool ena, u16 rxq_idx);
int ice_vsi_alloc_q_vectors(struct ice_vsi *vsi);
void ice_vsi_map_rings_to_vectors(struct ice_vsi *vsi);
void ice_vsi_free_q_vectors(struct ice_vsi *vsi);
int
ice_vsi_cfg_txq(struct ice_vsi *vsi, struct ice_tx_ring *ring,
		struct ice_aqc_add_tx_qgrp *qg_buf);
void ice_cfg_itr(struct ice_hw *hw, struct ice_q_vector *q_vector);
void
ice_cfg_txq_interrupt(struct ice_vsi *vsi, u16 txq, u16 msix_idx, u16 itr_idx);
void
ice_cfg_rxq_interrupt(struct ice_vsi *vsi, u16 rxq, u16 msix_idx, u16 itr_idx);
void ice_trigger_sw_intr(struct ice_hw *hw, const struct ice_q_vector *q_vector);
int
ice_vsi_stop_tx_ring(struct ice_vsi *vsi, enum ice_disq_rst_src rst_src,
		     u16 rel_vmvf_num, struct ice_tx_ring *ring,
		     struct ice_txq_meta *txq_meta);
void
ice_fill_txq_meta(const struct ice_vsi *vsi, struct ice_tx_ring *ring,
		  struct ice_txq_meta *txq_meta);
#endif /* _ICE_BASE_H_ */
