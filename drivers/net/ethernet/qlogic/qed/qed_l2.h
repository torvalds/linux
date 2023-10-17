/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/* QLogic qed NIC Driver
 * Copyright (c) 2015-2017  QLogic Corporation
 * Copyright (c) 2019-2020 Marvell International Ltd.
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
 * qed_eth_rx_queue_stop(): This ramrod closes an Rx queue.
 *
 * @p_hwfn: HW device data.
 * @p_rxq: Handler of queue to close
 * @eq_completion_only: If True completion will be on
 *                      EQe, if False completion will be
 *                      on EQe if p_hwfn opaque
 *                      different from the RXQ opaque
 *                      otherwise on CQe.
 * @cqe_completion: If True completion will be receive on CQe.
 *
 * Return: Int.
 */
int
qed_eth_rx_queue_stop(struct qed_hwfn *p_hwfn,
		      void *p_rxq,
		      bool eq_completion_only, bool cqe_completion);

/**
 * qed_eth_tx_queue_stop(): Closes a Tx queue.
 *
 * @p_hwfn: HW device data.
 * @p_txq: handle to Tx queue needed to be closed.
 *
 * Return: Int.
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
#define QED_ACCEPT_ANY_VNI              0x40
};

struct qed_arfs_config_params {
	bool tcp;
	bool udp;
	bool ipv4;
	bool ipv6;
	enum qed_filter_config_mode mode;
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
	u32				bins[8];
	struct qed_rss_params		*rss_params;
	struct qed_filter_accept_flags	accept_flags;
	struct qed_sge_tpa_params	*sge_tpa_params;
	u8				update_ctl_frame_check;
	u8				mac_chk_en;
	u8				ethtype_chk_en;
};

int qed_sp_vport_update(struct qed_hwfn *p_hwfn,
			struct qed_sp_vport_update_params *p_params,
			enum spq_mode comp_mode,
			struct qed_spq_comp_cb *p_comp_data);

/**
 * qed_sp_vport_stop: This ramrod closes a VPort after all its
 *                    RX and TX queues are terminated.
 *                    An Assert is generated if any queues are left open.
 *
 * @p_hwfn: HW device data.
 * @opaque_fid: Opaque FID
 * @vport_id: VPort ID.
 *
 * Return: Int.
 */
int qed_sp_vport_stop(struct qed_hwfn *p_hwfn, u16 opaque_fid, u8 vport_id);

int qed_sp_eth_filter_ucast(struct qed_hwfn *p_hwfn,
			    u16 opaque_fid,
			    struct qed_filter_ucast *p_filter_cmd,
			    enum spq_mode comp_mode,
			    struct qed_spq_comp_cb *p_comp_data);

/**
 * qed_sp_eth_rx_queues_update(): This ramrod updates an RX queue.
 *                                It is used for setting the active state
 *                                of the queue and updating the TPA and
 *                                SGE parameters.
 * @p_hwfn: HW device data.
 * @pp_rxq_handlers: An array of queue handlers to be updated.
 * @num_rxqs: number of queues to update.
 * @complete_cqe_flg: Post completion to the CQE Ring if set.
 * @complete_event_flg: Post completion to the Event Ring if set.
 * @comp_mode: Comp mode.
 * @p_comp_data: Pointer Comp data.
 *
 * Return: Int.
 *
 * Note At the moment - only used by non-linux VFs.
 */

int
qed_sp_eth_rx_queues_update(struct qed_hwfn *p_hwfn,
			    void **pp_rxq_handlers,
			    u8 num_rxqs,
			    u8 complete_cqe_flg,
			    u8 complete_event_flg,
			    enum spq_mode comp_mode,
			    struct qed_spq_comp_cb *p_comp_data);

/**
 * qed_get_vport_stats(): Fills provided statistics
 *			  struct with statistics.
 *
 * @cdev: Qed dev pointer.
 * @stats: Points to struct that will be filled with statistics.
 *
 * Return: Void.
 */
void qed_get_vport_stats(struct qed_dev *cdev, struct qed_eth_stats *stats);

/**
 * qed_get_vport_stats_context(): Fills provided statistics
 *				  struct with statistics.
 *
 * @cdev: Qed dev pointer.
 * @stats: Points to struct that will be filled with statistics.
 * @is_atomic: Hint from the caller - if the func can sleep or not.
 *
 * Context: The function should not sleep in case is_atomic == true.
 * Return: Void.
 */
void qed_get_vport_stats_context(struct qed_dev *cdev,
				 struct qed_eth_stats *stats,
				 bool is_atomic);

void qed_reset_vport_stats(struct qed_dev *cdev);

/**
 * qed_arfs_mode_configure(): Enable or disable rfs mode.
 *                            It must accept at least one of tcp or udp true
 *                            and at least one of ipv4 or ipv6 true to enable
 *                            rfs mode.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: P_ptt.
 * @p_cfg_params: arfs mode configuration parameters.
 *
 * Return. Void.
 */
void qed_arfs_mode_configure(struct qed_hwfn *p_hwfn,
			     struct qed_ptt *p_ptt,
			     struct qed_arfs_config_params *p_cfg_params);

/**
 * qed_configure_rfs_ntuple_filter(): This ramrod should be used to add
 *                                     or remove arfs hw filter
 *
 * @p_hwfn: HW device data.
 * @p_cb: Used for QED_SPQ_MODE_CB,where client would initialize
 *        it with cookie and callback function address, if not
 *        using this mode then client must pass NULL.
 * @p_params: Pointer to params.
 *
 * Return: Void.
 */
int
qed_configure_rfs_ntuple_filter(struct qed_hwfn *p_hwfn,
				struct qed_spq_comp_cb *p_cb,
				struct qed_ntuple_filter_params *p_params);

#define MAX_QUEUES_PER_QZONE    (sizeof(unsigned long) * 8)
#define QED_QUEUE_CID_SELF	(0xff)

/* Almost identical to the qed_queue_start_common_params,
 * but here we maintain the SB index in IGU CAM.
 */
struct qed_queue_cid_params {
	u8 vport_id;
	u16 queue_id;
	u8 stats_id;
};

/* Additional parameters required for initialization of the queue_cid
 * and are relevant only for a PF initializing one for its VFs.
 */
struct qed_queue_cid_vf_params {
	/* Should match the VF's relative index */
	u8 vfid;

	/* 0-based queue index. Should reflect the relative qzone the
	 * VF thinks is associated with it [in its range].
	 */
	u8 vf_qid;

	/* Indicates a VF is legacy, making it differ in several things:
	 *  - Producers would be placed in a different place.
	 *  - Makes assumptions regarding the CIDs.
	 */
	u8 vf_legacy;

	u8 qid_usage_idx;
};

struct qed_queue_cid {
	/* For stats-id, the `rel' is actually absolute as well */
	struct qed_queue_cid_params rel;
	struct qed_queue_cid_params abs;

	/* These have no 'relative' meaning */
	u16 sb_igu_id;
	u8 sb_idx;

	u32 cid;
	u16 opaque_fid;

	bool b_is_rx;

	/* VFs queues are mapped differently, so we need to know the
	 * relative queue associated with them [0-based].
	 * Notice this is relevant on the *PF* queue-cid of its VF's queues,
	 * and not on the VF itself.
	 */
	u8 vfid;
	u8 vf_qid;

	/* We need an additional index to differentiate between queues opened
	 * for same queue-zone, as VFs would have to communicate the info
	 * to the PF [otherwise PF has no way to differentiate].
	 */
	u8 qid_usage_idx;

	u8 vf_legacy;
#define QED_QCID_LEGACY_VF_RX_PROD	(BIT(0))
#define QED_QCID_LEGACY_VF_CID		(BIT(1))

	struct qed_hwfn *p_owner;
};

int qed_l2_alloc(struct qed_hwfn *p_hwfn);
void qed_l2_setup(struct qed_hwfn *p_hwfn);
void qed_l2_free(struct qed_hwfn *p_hwfn);

void qed_eth_queue_cid_release(struct qed_hwfn *p_hwfn,
			       struct qed_queue_cid *p_cid);

struct qed_queue_cid *
qed_eth_queue_to_cid(struct qed_hwfn *p_hwfn,
		     u16 opaque_fid,
		     struct qed_queue_start_common_params *p_params,
		     bool b_is_rx,
		     struct qed_queue_cid_vf_params *p_vf_params);

int
qed_sp_eth_vport_start(struct qed_hwfn *p_hwfn,
		       struct qed_sp_vport_start_params *p_params);

/**
 * qed_eth_rxq_start_ramrod(): Starts an Rx queue, when queue_cid is
 *                             already prepared
 *
 * @p_hwfn: HW device data.
 * @p_cid: Pointer CID.
 * @bd_max_bytes: Max bytes.
 * @bd_chain_phys_addr: Chain physcial address.
 * @cqe_pbl_addr: PBL address.
 * @cqe_pbl_size: PBL size.
 *
 * Return: Int.
 */
int
qed_eth_rxq_start_ramrod(struct qed_hwfn *p_hwfn,
			 struct qed_queue_cid *p_cid,
			 u16 bd_max_bytes,
			 dma_addr_t bd_chain_phys_addr,
			 dma_addr_t cqe_pbl_addr, u16 cqe_pbl_size);

/**
 * qed_eth_txq_start_ramrod(): Starts a Tx queue, where queue_cid is
 *                             already prepared
 *
 * @p_hwfn: HW device data.
 * @p_cid: Pointer CID.
 * @pbl_addr: PBL address.
 * @pbl_size: PBL size.
 * @pq_id: Parameters for choosing the PQ for this Tx queue.
 *
 * Return: Int.
 */
int
qed_eth_txq_start_ramrod(struct qed_hwfn *p_hwfn,
			 struct qed_queue_cid *p_cid,
			 dma_addr_t pbl_addr, u16 pbl_size, u16 pq_id);

u8 qed_mcast_bin_from_mac(u8 *mac);

int qed_set_rxq_coalesce(struct qed_hwfn *p_hwfn,
			 struct qed_ptt *p_ptt,
			 u16 coalesce, struct qed_queue_cid *p_cid);

int qed_set_txq_coalesce(struct qed_hwfn *p_hwfn,
			 struct qed_ptt *p_ptt,
			 u16 coalesce, struct qed_queue_cid *p_cid);

int qed_get_rxq_coalesce(struct qed_hwfn *p_hwfn,
			 struct qed_ptt *p_ptt,
			 struct qed_queue_cid *p_cid, u16 *p_hw_coal);

int qed_get_txq_coalesce(struct qed_hwfn *p_hwfn,
			 struct qed_ptt *p_ptt,
			 struct qed_queue_cid *p_cid, u16 *p_hw_coal);

#endif
