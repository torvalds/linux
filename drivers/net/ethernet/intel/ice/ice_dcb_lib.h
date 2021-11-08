/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019, Intel Corporation. */

#ifndef _ICE_DCB_LIB_H_
#define _ICE_DCB_LIB_H_

#include "ice.h"
#include "ice_base.h"
#include "ice_lib.h"

#ifdef CONFIG_DCB
#define ICE_TC_MAX_BW		100 /* Default Max BW percentage */
#define ICE_DCB_HW_CHG_RST	0 /* DCB configuration changed with reset */
#define ICE_DCB_NO_HW_CHG	1 /* DCB configuration did not change */
#define ICE_DCB_HW_CHG		2 /* DCB configuration changed, no reset */

void ice_dcb_rebuild(struct ice_pf *pf);
int ice_dcb_sw_dflt_cfg(struct ice_pf *pf, bool ets_willing, bool locked);
u8 ice_dcb_get_ena_tc(struct ice_dcbx_cfg *dcbcfg);
u8 ice_dcb_get_num_tc(struct ice_dcbx_cfg *dcbcfg);
void ice_vsi_set_dcb_tc_cfg(struct ice_vsi *vsi);
bool ice_is_pfc_causing_hung_q(struct ice_pf *pf, unsigned int txqueue);
u8 ice_dcb_get_tc(struct ice_vsi *vsi, int queue_index);
int
ice_pf_dcb_cfg(struct ice_pf *pf, struct ice_dcbx_cfg *new_cfg, bool locked);
int ice_dcb_bwchk(struct ice_pf *pf, struct ice_dcbx_cfg *dcbcfg);
void ice_pf_dcb_recfg(struct ice_pf *pf);
void ice_vsi_cfg_dcb_rings(struct ice_vsi *vsi);
int ice_init_pf_dcb(struct ice_pf *pf, bool locked);
void ice_update_dcb_stats(struct ice_pf *pf);
void
ice_tx_prepare_vlan_flags_dcb(struct ice_ring *tx_ring,
			      struct ice_tx_buf *first);
void
ice_dcb_process_lldp_set_mib_change(struct ice_pf *pf,
				    struct ice_rq_event_info *event);
void ice_vsi_cfg_netdev_tc(struct ice_vsi *vsi, u8 ena_tc);

/**
 * ice_find_q_in_range
 * @low: start of queue range for a TC i.e. offset of TC
 * @high: start of queue for next TC
 * @tx_q: hung_queue/tx_queue
 *
 * finds if queue 'tx_q' falls between the two offsets of any given TC
 */
static inline bool ice_find_q_in_range(u16 low, u16 high, unsigned int tx_q)
{
	return (tx_q >= low) && (tx_q < high);
}

static inline void
ice_set_cgd_num(struct ice_tlan_ctx *tlan_ctx, struct ice_ring *ring)
{
	tlan_ctx->cgd_num = ring->dcb_tc;
}

static inline bool ice_is_dcb_active(struct ice_pf *pf)
{
	return (test_bit(ICE_FLAG_FW_LLDP_AGENT, pf->flags) ||
		test_bit(ICE_FLAG_DCB_ENA, pf->flags));
}

static inline u8 ice_get_pfc_mode(struct ice_pf *pf)
{
	return pf->hw.port_info->qos_cfg.local_dcbx_cfg.pfc_mode;
}

#else
static inline void ice_dcb_rebuild(struct ice_pf *pf) { }

static inline u8 ice_dcb_get_ena_tc(struct ice_dcbx_cfg __always_unused *dcbcfg)
{
	return ICE_DFLT_TRAFFIC_CLASS;
}

static inline u8 ice_dcb_get_num_tc(struct ice_dcbx_cfg __always_unused *dcbcfg)
{
	return 1;
}

static inline u8
ice_dcb_get_tc(struct ice_vsi __always_unused *vsi,
	       int __always_unused queue_index)
{
	return 0;
}

static inline int
ice_init_pf_dcb(struct ice_pf *pf, bool __always_unused locked)
{
	dev_dbg(ice_pf_to_dev(pf), "DCB not supported\n");
	return -EOPNOTSUPP;
}

static inline int
ice_pf_dcb_cfg(struct ice_pf __always_unused *pf,
	       struct ice_dcbx_cfg __always_unused *new_cfg,
	       bool __always_unused locked)
{
	return -EOPNOTSUPP;
}

static inline int
ice_tx_prepare_vlan_flags_dcb(struct ice_ring __always_unused *tx_ring,
			      struct ice_tx_buf __always_unused *first)
{
	return 0;
}

static inline bool ice_is_dcb_active(struct ice_pf __always_unused *pf)
{
	return false;
}

static inline bool
ice_is_pfc_causing_hung_q(struct ice_pf __always_unused *pf,
			  unsigned int __always_unused txqueue)
{
	return false;
}

static inline u8 ice_get_pfc_mode(struct ice_pf *pf)
{
	return 0;
}

static inline void ice_pf_dcb_recfg(struct ice_pf *pf) { }
static inline void ice_vsi_cfg_dcb_rings(struct ice_vsi *vsi) { }
static inline void ice_update_dcb_stats(struct ice_pf *pf) { }
static inline void
ice_dcb_process_lldp_set_mib_change(struct ice_pf *pf, struct ice_rq_event_info *event) { }
static inline void ice_vsi_cfg_netdev_tc(struct ice_vsi *vsi, u8 ena_tc) { }
static inline void ice_set_cgd_num(struct ice_tlan_ctx *tlan_ctx, struct ice_ring *ring) { }
#endif /* CONFIG_DCB */
#endif /* _ICE_DCB_LIB_H_ */
