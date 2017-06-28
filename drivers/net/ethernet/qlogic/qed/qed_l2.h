/* QLogic qed NIC Driver
 * Copyright (c) 2015-2017  QLogic Corporation
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and /or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#ifndef _QED_L2_H
#define _QED_L2_H
#include <linux/types.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/qed/qed_eth_if.h>
#include "qed.h"
#include "qed_hw.h"
#include "qed_sp.h"
struct qed_rss_params {
	u8 update_rss_config;
	u8 rss_enable;
	u8 rss_eng_id;
	u8 update_rss_capabilities;
	u8 update_rss_ind_table;
	u8 update_rss_key;
	u8 rss_caps;
	u8 rss_table_size_log;

	/* Indirection table consist of rx queue handles */
	void *rss_ind_table[QED_RSS_IND_TABLE_SIZE];
	u32 rss_key[QED_RSS_KEY_SIZE];
};

struct qed_sge_tpa_params {
	u8 max_buffers_per_cqe;

	u8 update_tpa_en_flg;
	u8 tpa_ipv4_en_flg;
	u8 tpa_ipv6_en_flg;
	u8 tpa_ipv4_tunn_en_flg;
	u8 tpa_ipv6_tunn_en_flg;

	u8 update_tpa_param_flg;
	u8 tpa_pkt_split_flg;
	u8 tpa_hdr_data_split_flg;
	u8 tpa_gro_consistent_flg;
	u8 tpa_max_aggs_num;
	u16 tpa_max_size;
	u16 tpa_min_size_to_start;
	u16 tpa_min_size_to_cont;
};

enum qed_filter_opcode {
	QED_FILTER_ADD,
	QED_FILTER_REMOVE,
	QED_FILTER_MOVE,
	QED_FILTER_REPLACE,	/* Delete all MACs and add new one instead */
	QED_FILTER_FLUSH,	/* Removes all filters */
};

enum qed_filter_ucast_type {
	QED_FILTER_MAC,
	QED_FILTER_VLAN,
	QED_FILTER_MAC_VLAN,
	QED_FILTER_INNER_MAC,
	QED_FILTER_INNER_VLAN,
	QED_FILTER_INNER_PAIR,
	QED_FILTER_INNER_MAC_VNI_PAIR,
	QED_FILTER_MAC_VNI_PAIR,
	QED_FILTER_VNI,
};

struct qed_filter_ucast {
	enum qed_filter_opcode opcode;
	enum qed_filter_ucast_type type;
	u8 is_rx_filter;
	u8 is_tx_filter;
	u8 vport_to_add_to;
	u8 vport_to_remove_from;
	unsigned char mac[ETH_ALEN];
	u8 assert_on_error;
	u16 vlan;
	u32 vni;
};

struct qed_filter_mcast {
	/* MOVE is not supported for multicast */
	enum qed_filter_opcode opcode;
	u8 vport_to_add_to;
	u8 vport_to_remove_from;
	u8 num_mc_addrs;
#define QED_MAX_MC_ADDRS        64
	unsigned char mac[QED_MAX_MC_ADDRS][ETH_ALEN];
};

/**
 * @brief qed_eth_rx_queue_stop - This ramrod closes an Rx queue
 *
 * @param p_hwfn
 * @param p_rxq			Handler of queue to close
 * @param eq_completion_only	If True completion will be on
 *				EQe, if False completion will be
 *				on EQe if p_hwfn opaque
 *				different from the RXQ opaque
 *				otherwise on CQe.
 * @param cqe_completion	If True completion will be
 *				receive on CQe.
 * @return int
 */
int
qed_eth_rx_queue_stop(struct qed_hwfn *p_hwfn,
		      void *p_rxq,
		      bool eq_completion_only, bool cqe_completion);

/**
 * @brief qed_eth_tx_queue_stop - closes a Tx queue
 *
 * @param p_hwfn
 * @param p_txq - handle to Tx queue needed to be closed
 *
 * @return int
 */
int qed_eth_tx_queue_stop(struct qed_hwfn *p_hwfn, void *p_txq);

enum qed_tpa_mode {
	QED_TPA_MODE_NONE,
	QED_TPA_MODE_UNUSED,
	QED_TPA_MODE_GRO,
	QED_TPA_MODE_MAX
};

struct qed_sp_vport_start_params {
	enum qed_tpa_mode tpa_mode;
	bool remove_inner_vlan;
	bool tx_switching;
	bool handle_ptp_pkts;
	bool only_untagged;
	bool drop_ttl0;
	u8 max_buffers_per_cqe;
	u32 concrete_fid;
	u16 opaque_fid;
	u8 vport_id;
	u16 mtu;
	bool check_mac;
	bool check_ethtype;
};

int qed_sp_eth_vport_start(struct qed_hwfn *p_hwfn,
			   struct qed_sp_vport_start_params *p_params);


struct qed_filter_accept_flags {
	u8	update_rx_mode_config;
	u8	update_tx_mode_config;
	u8	rx_accept_filter;
	u8	tx_accept_filter;
#define QED_ACCEPT_NONE         0x01
#define QED_ACCEPT_UCAST_MATCHED        0x02
#define QED_ACCEPT_UCAST_UNMATCHED      0x04
#define QED_ACCEPT_MCAST_MATCHED        0x08
#define QED_ACCEPT_MCAST_UNMATCHED      0x10
#define QED_ACCEPT_BCAST                0x20
};

struct qed_sp_vport_update_params {
	u16				opaque_fid;
	u8				vport_id;
	u8				update_vport_active_rx_flg;
	u8				vport_active_rx_flg;
	u8				update_vport_active_tx_flg;
	u8				vport_active_tx_flg;
	u8				update_inner_vlan_removal_flg;
	u8				inner_vlan_removal_flg;
	u8				silent_vlan_removal_flg;
	u8				update_default_vlan_enable_flg;
	u8				default_vlan_enable_flg;
	u8				update_default_vlan_flg;
	u16				default_vlan;
	u8				update_tx_switching_flg;
	u8				tx_switching_flg;
	u8				update_approx_mcast_flg;
	u8				update_anti_spoofing_en_flg;
	u8				anti_spoofing_en;
	u8				update_accept_any_vlan_flg;
	u8				accept_any_vlan;
	unsigned long			bins[8];
	struct qed_rss_params		*rss_params;
	struct qed_filter_accept_flags	accept_flags;
	struct qed_sge_tpa_params	*sge_tpa_params;
};

int qed_sp_vport_update(struct qed_hwfn *p_hwfn,
			struct qed_sp_vport_update_params *p_params,
			enum spq_mode comp_mode,
			struct qed_spq_comp_cb *p_comp_data);

/**
 * @brief qed_sp_vport_stop -
 *
 * This ramrod closes a VPort after all its RX and TX queues are terminated.
 * An Assert is generated if any queues are left open.
 *
 * @param p_hwfn
 * @param opaque_fid
 * @param vport_id VPort ID
 *
 * @return int
 */
int qed_sp_vport_stop(struct qed_hwfn *p_hwfn, u16 opaque_fid, u8 vport_id);

int qed_sp_eth_filter_ucast(struct qed_hwfn *p_hwfn,
			    u16 opaque_fid,
			    struct qed_filter_ucast *p_filter_cmd,
			    enum spq_mode comp_mode,
			    struct qed_spq_comp_cb *p_comp_data);

/**
 * @brief qed_sp_rx_eth_queues_update -
 *
 * This ramrod updates an RX queue. It is used for setting the active state
 * of the queue and updating the TPA and SGE parameters.
 *
 * @note At the moment - only used by non-linux VFs.
 *
 * @param p_hwfn
 * @param pp_rxq_handlers	An array of queue handlers to be updated.
 * @param num_rxqs              number of queues to update.
 * @param complete_cqe_flg	Post completion to the CQE Ring if set
 * @param complete_event_flg	Post completion to the Event Ring if set
 * @param comp_mode
 * @param p_comp_data
 *
 * @return int
 */

int
qed_sp_eth_rx_queues_update(struct qed_hwfn *p_hwfn,
			    void **pp_rxq_handlers,
			    u8 num_rxqs,
			    u8 complete_cqe_flg,
			    u8 complete_event_flg,
			    enum spq_mode comp_mode,
			    struct qed_spq_comp_cb *p_comp_data);

void qed_get_vport_stats(struct qed_dev *cdev, struct qed_eth_stats *stats);

void qed_reset_vport_stats(struct qed_dev *cdev);

struct qed_queue_cid {
	/* 'Relative' is a relative term ;-). Usually the indices [not counting
	 * SBs] would be PF-relative, but there are some cases where that isn't
	 * the case - specifically for a PF configuring its VF indices it's
	 * possible some fields [E.g., stats-id] in 'rel' would already be abs.
	 */
	struct qed_queue_start_common_params rel;
	struct qed_queue_start_common_params abs;
	u32 cid;
	u16 opaque_fid;

	/* VFs queues are mapped differently, so we need to know the
	 * relative queue associated with them [0-based].
	 * Notice this is relevant on the *PF* queue-cid of its VF's queues,
	 * and not on the VF itself.
	 */
	bool is_vf;
	u8 vf_qid;

	/* Legacy VFs might have Rx producer located elsewhere */
	bool b_legacy_vf;

	struct qed_hwfn *p_owner;
};

void qed_eth_queue_cid_release(struct qed_hwfn *p_hwfn,
			       struct qed_queue_cid *p_cid);

struct qed_queue_cid *_qed_eth_queue_to_cid(struct qed_hwfn *p_hwfn,
					    u16 opaque_fid,
					    u32 cid,
					    u8 vf_qid,
					    struct qed_queue_start_common_params
					    *p_params);

int
qed_sp_eth_vport_start(struct qed_hwfn *p_hwfn,
		       struct qed_sp_vport_start_params *p_params);

/**
 * @brief - Starts an Rx queue, when queue_cid is already prepared
 *
 * @param p_hwfn
 * @param p_cid
 * @param bd_max_bytes
 * @param bd_chain_phys_addr
 * @param cqe_pbl_addr
 * @param cqe_pbl_size
 *
 * @return int
 */
int
qed_eth_rxq_start_ramrod(struct qed_hwfn *p_hwfn,
			 struct qed_queue_cid *p_cid,
			 u16 bd_max_bytes,
			 dma_addr_t bd_chain_phys_addr,
			 dma_addr_t cqe_pbl_addr, u16 cqe_pbl_size);

/**
 * @brief - Starts a Tx queue, where queue_cid is already prepared
 *
 * @param p_hwfn
 * @param p_cid
 * @param pbl_addr
 * @param pbl_size
 * @param p_pq_params - parameters for choosing the PQ for this Tx queue
 *
 * @return int
 */
int
qed_eth_txq_start_ramrod(struct qed_hwfn *p_hwfn,
			 struct qed_queue_cid *p_cid,
			 dma_addr_t pbl_addr, u16 pbl_size, u16 pq_id);

u8 qed_mcast_bin_from_mac(u8 *mac);

#endif /* _QED_L2_H */
