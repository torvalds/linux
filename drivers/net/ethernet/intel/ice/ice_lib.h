/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018, Intel Corporation. */

#ifndef _ICE_LIB_H_
#define _ICE_LIB_H_

#include "ice.h"
#include "ice_vlan.h"

/* Flags used for VSI configuration and rebuild */
#define ICE_VSI_FLAG_INIT	BIT(0)
#define ICE_VSI_FLAG_NO_INIT	0

/**
 * struct ice_vsi_cfg_params - VSI configuration parameters
 * @pi: pointer to the port_info instance for the VSI
 * @ch: pointer to the channel structure for the VSI, may be NULL
 * @vf: pointer to the VF associated with this VSI, may be NULL
 * @type: the type of VSI to configure
 * @flags: VSI flags used for rebuild and configuration
 *
 * Parameter structure used when configuring a new VSI.
 */
struct ice_vsi_cfg_params {
	struct ice_port_info *pi;
	struct ice_channel *ch;
	struct ice_vf *vf;
	enum ice_vsi_type type;
	u32 flags;
};

/**
 * ice_vsi_to_params - Get parameters for an existing VSI
 * @vsi: the VSI to get parameters for
 *
 * Fill a parameter structure for reconfiguring a VSI with its current
 * parameters, such as during a rebuild operation.
 */
static inline struct ice_vsi_cfg_params ice_vsi_to_params(struct ice_vsi *vsi)
{
	struct ice_vsi_cfg_params params = {};

	params.pi = vsi->port_info;
	params.ch = vsi->ch;
	params.vf = vsi->vf;
	params.type = vsi->type;

	return params;
}

const char *ice_vsi_type_str(enum ice_vsi_type vsi_type);

bool ice_pf_state_is_nominal(struct ice_pf *pf);

void ice_update_eth_stats(struct ice_vsi *vsi);

int ice_vsi_cfg_single_rxq(struct ice_vsi *vsi, u16 q_idx);

int ice_vsi_cfg_single_txq(struct ice_vsi *vsi, struct ice_tx_ring **tx_rings, u16 q_idx);

int ice_vsi_cfg_rxqs(struct ice_vsi *vsi);

int ice_vsi_cfg_lan_txqs(struct ice_vsi *vsi);

void ice_vsi_cfg_msix(struct ice_vsi *vsi);

int ice_vsi_start_all_rx_rings(struct ice_vsi *vsi);

int ice_vsi_stop_all_rx_rings(struct ice_vsi *vsi);

int
ice_vsi_stop_lan_tx_rings(struct ice_vsi *vsi, enum ice_disq_rst_src rst_src,
			  u16 rel_vmvf_num);

int ice_vsi_cfg_xdp_txqs(struct ice_vsi *vsi);

int ice_vsi_stop_xdp_tx_rings(struct ice_vsi *vsi);

bool ice_vsi_is_vlan_pruning_ena(struct ice_vsi *vsi);

void ice_cfg_sw_lldp(struct ice_vsi *vsi, bool tx, bool create);

int ice_set_link(struct ice_vsi *vsi, bool ena);

void ice_vsi_delete(struct ice_vsi *vsi);

int ice_vsi_cfg_tc(struct ice_vsi *vsi, u8 ena_tc);

int ice_vsi_cfg_rss_lut_key(struct ice_vsi *vsi);

void ice_vsi_cfg_netdev_tc(struct ice_vsi *vsi, u8 ena_tc);

struct ice_vsi *
ice_vsi_setup(struct ice_pf *pf, struct ice_vsi_cfg_params *params);

void ice_napi_del(struct ice_vsi *vsi);

int ice_vsi_release(struct ice_vsi *vsi);

void ice_vsi_close(struct ice_vsi *vsi);

int ice_ena_vsi(struct ice_vsi *vsi, bool locked);

void ice_vsi_decfg(struct ice_vsi *vsi);
void ice_dis_vsi(struct ice_vsi *vsi, bool locked);

int ice_vsi_rebuild(struct ice_vsi *vsi, u32 vsi_flags);
int ice_vsi_cfg(struct ice_vsi *vsi, struct ice_vsi_cfg_params *params);

bool ice_is_reset_in_progress(unsigned long *state);
int ice_wait_for_reset(struct ice_pf *pf, unsigned long timeout);

void
ice_write_qrxflxp_cntxt(struct ice_hw *hw, u16 pf_q, u32 rxdid, u32 prio,
			bool ena_ts);

void ice_vsi_dis_irq(struct ice_vsi *vsi);

void ice_vsi_free_irq(struct ice_vsi *vsi);

void ice_vsi_free_rx_rings(struct ice_vsi *vsi);

void ice_vsi_free_tx_rings(struct ice_vsi *vsi);

void ice_vsi_manage_rss_lut(struct ice_vsi *vsi, bool ena);

void ice_vsi_cfg_crc_strip(struct ice_vsi *vsi, bool disable);

void ice_update_tx_ring_stats(struct ice_tx_ring *ring, u64 pkts, u64 bytes);

void ice_update_rx_ring_stats(struct ice_rx_ring *ring, u64 pkts, u64 bytes);

void ice_vsi_cfg_frame_size(struct ice_vsi *vsi);
void ice_write_intrl(struct ice_q_vector *q_vector, u8 intrl);
void ice_write_itr(struct ice_ring_container *rc, u16 itr);
void ice_set_q_vector_intrl(struct ice_q_vector *q_vector);

int ice_vsi_cfg_mac_fltr(struct ice_vsi *vsi, const u8 *macaddr, bool set);

bool ice_is_safe_mode(struct ice_pf *pf);
bool ice_is_rdma_ena(struct ice_pf *pf);
bool ice_is_dflt_vsi_in_use(struct ice_port_info *pi);
bool ice_is_vsi_dflt_vsi(struct ice_vsi *vsi);
int ice_set_dflt_vsi(struct ice_vsi *vsi);
int ice_clear_dflt_vsi(struct ice_vsi *vsi);
int ice_set_min_bw_limit(struct ice_vsi *vsi, u64 min_tx_rate);
int ice_set_max_bw_limit(struct ice_vsi *vsi, u64 max_tx_rate);
int ice_get_link_speed_kbps(struct ice_vsi *vsi);
int ice_get_link_speed_mbps(struct ice_vsi *vsi);
int
ice_vsi_update_security(struct ice_vsi *vsi, void (*fill)(struct ice_vsi_ctx *));

void ice_vsi_ctx_set_antispoof(struct ice_vsi_ctx *ctx);

void ice_vsi_ctx_clear_antispoof(struct ice_vsi_ctx *ctx);

void ice_vsi_ctx_set_allow_override(struct ice_vsi_ctx *ctx);

void ice_vsi_ctx_clear_allow_override(struct ice_vsi_ctx *ctx);
int ice_vsi_update_local_lb(struct ice_vsi *vsi, bool set);
int ice_vsi_add_vlan_zero(struct ice_vsi *vsi);
int ice_vsi_del_vlan_zero(struct ice_vsi *vsi);
bool ice_vsi_has_non_zero_vlans(struct ice_vsi *vsi);
u16 ice_vsi_num_non_zero_vlans(struct ice_vsi *vsi);
bool ice_is_feature_supported(struct ice_pf *pf, enum ice_feature f);
void ice_set_feature_support(struct ice_pf *pf, enum ice_feature f);
void ice_clear_feature_support(struct ice_pf *pf, enum ice_feature f);
void ice_init_feature_support(struct ice_pf *pf);
bool ice_vsi_is_rx_queue_active(struct ice_vsi *vsi);
#endif /* !_ICE_LIB_H_ */
