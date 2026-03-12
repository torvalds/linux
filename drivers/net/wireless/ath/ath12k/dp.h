/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef ATH12K_DP_H
#define ATH12K_DP_H

#include "hw.h"
#include "dp_htt.h"
#include "dp_cmn.h"
#include <linux/rhashtable.h>

#define MAX_RXDMA_PER_PDEV     2

struct ath12k_base;
struct ath12k_dp_link_peer;
struct ath12k_dp;
struct ath12k_vif;
struct ath12k_link_vif;
struct ath12k_ext_irq_grp;
struct ath12k_dp_rx_tid;
struct ath12k_dp_rx_tid_rxq;

#define DP_MON_PURGE_TIMEOUT_MS     100
#define DP_MON_SERVICE_BUDGET       128

#define DP_ENCAP_TYPE_MAX	4
#define DP_ENCRYPT_TYPE_MAX	12
#define DP_DESC_TYPE_MAX	2

struct dp_srng {
	u32 *vaddr_unaligned;
	u32 *vaddr;
	dma_addr_t paddr_unaligned;
	dma_addr_t paddr;
	int size;
	u32 ring_id;
};

struct dp_rxdma_mon_ring {
	struct dp_srng refill_buf_ring;
	struct idr bufs_idr;
	/* Protects bufs_idr */
	spinlock_t idr_lock;
	int bufs_max;
};

struct dp_rxdma_ring {
	struct dp_srng refill_buf_ring;
	int bufs_max;
};

#define ATH12K_TX_COMPL_NEXT(ab, x)	(((x) + 1) % DP_TX_COMP_RING_SIZE(ab))

struct dp_tx_ring {
	u8 tcl_data_ring_id;
	struct dp_srng tcl_data_ring;
	struct dp_srng tcl_comp_ring;
	struct hal_wbm_completion_ring_tx *tx_status;
	int tx_status_head;
	int tx_status_tail;
};

struct ath12k_pdev_mon_stats {
	u32 status_ppdu_state;
	u32 status_ppdu_start;
	u32 status_ppdu_end;
	u32 status_ppdu_compl;
	u32 status_ppdu_start_mis;
	u32 status_ppdu_end_mis;
	u32 status_ppdu_done;
	u32 dest_ppdu_done;
	u32 dest_mpdu_done;
	u32 dest_mpdu_drop;
	u32 dup_mon_linkdesc_cnt;
	u32 dup_mon_buf_cnt;
	u32 dest_mon_stuck;
	u32 dest_mon_not_reaped;
};

enum dp_mon_status_buf_state {
	DP_MON_STATUS_MATCH,
	DP_MON_STATUS_NO_DMA,
	DP_MON_STATUS_LAG,
	DP_MON_STATUS_LEAD,
	DP_MON_STATUS_REPLINISH,
};

struct dp_link_desc_bank {
	void *vaddr_unaligned;
	void *vaddr;
	dma_addr_t paddr_unaligned;
	dma_addr_t paddr;
	u32 size;
};

/* Size to enforce scatter idle list mode */
#define DP_LINK_DESC_ALLOC_SIZE_THRESH 0x200000
#define DP_LINK_DESC_BANKS_MAX 8

#define DP_LINK_DESC_START	0x4000
#define DP_LINK_DESC_SHIFT	3

#define DP_LINK_DESC_COOKIE_SET(id, page) \
	((((id) + DP_LINK_DESC_START) << DP_LINK_DESC_SHIFT) | (page))

#define DP_LINK_DESC_BANK_MASK	GENMASK(2, 0)

#define DP_RX_DESC_COOKIE_INDEX_MAX		0x3ffff
#define DP_RX_DESC_COOKIE_POOL_ID_MAX		0x1c0000
#define DP_RX_DESC_COOKIE_MAX	\
	(DP_RX_DESC_COOKIE_INDEX_MAX | DP_RX_DESC_COOKIE_POOL_ID_MAX)
#define DP_NOT_PPDU_ID_WRAP_AROUND 20000

enum ath12k_dp_ppdu_state {
	DP_PPDU_STATUS_START,
	DP_PPDU_STATUS_DONE,
};

struct dp_mon_mpdu {
	struct list_head list;
	struct sk_buff *head;
	struct sk_buff *tail;
	u32 err_bitmap;
	u8 decap_format;
};

#define DP_MON_MAX_STATUS_BUF 32

struct ath12k_mon_data {
	struct dp_link_desc_bank link_desc_banks[DP_LINK_DESC_BANKS_MAX];
	struct hal_rx_mon_ppdu_info mon_ppdu_info;

	u32 mon_ppdu_status;
	u32 mon_last_buf_cookie;
	u64 mon_last_linkdesc_paddr;
	u16 chan_noise_floor;
	u32 err_bitmap;
	u8 decap_format;

	struct ath12k_pdev_mon_stats rx_mon_stats;
	enum dp_mon_status_buf_state buf_state;
	/* lock for monitor data */
	spinlock_t mon_lock;
	struct sk_buff_head rx_status_q;
	struct dp_mon_mpdu *mon_mpdu;
	struct list_head dp_rx_mon_mpdu_list;
	struct dp_mon_tx_ppdu_info *tx_prot_ppdu_info;
	struct dp_mon_tx_ppdu_info *tx_data_ppdu_info;
};

struct ath12k_pdev_dp {
	u32 mac_id;
	atomic_t num_tx_pending;
	wait_queue_head_t tx_empty_waitq;

	struct ath12k_dp *dp;
	struct ieee80211_hw *hw;
	u8 hw_link_id;
	struct ath12k_dp_hw *dp_hw;

	/* Protects ppdu stats */
	spinlock_t ppdu_list_lock;
	struct ath12k_per_peer_tx_stats peer_tx_stats;
	struct list_head ppdu_stats_info;
	u32 ppdu_stat_list_depth;

	struct dp_srng rxdma_mon_dst_ring[MAX_RXDMA_PER_PDEV];
	struct dp_srng tx_mon_dst_ring[MAX_RXDMA_PER_PDEV];

	struct ieee80211_rx_status rx_status;
	struct ath12k_mon_data mon_data;
};

#define DP_NUM_CLIENTS_MAX 64
#define DP_AVG_TIDS_PER_CLIENT 2
#define DP_NUM_TIDS_MAX (DP_NUM_CLIENTS_MAX * DP_AVG_TIDS_PER_CLIENT)
#define DP_AVG_MSDUS_PER_FLOW 128
#define DP_AVG_FLOWS_PER_TID 2
#define DP_AVG_MPDUS_PER_TID_MAX 128
#define DP_AVG_MSDUS_PER_MPDU 4

#define DP_RX_HASH_ENABLE	1 /* Enable hash based Rx steering */

#define DP_BA_WIN_SZ_MAX	1024

#define DP_TCL_NUM_RING_MAX	4

#define DP_IDLE_SCATTER_BUFS_MAX 16

#define DP_WBM_RELEASE_RING_SIZE	64
#define DP_TCL_DATA_RING_SIZE		512
#define DP_TX_COMP_RING_SIZE(ab) \
	((ab)->profile_param->dp_params.tx_comp_ring_size)
#define DP_TX_IDR_SIZE(ab)		DP_TX_COMP_RING_SIZE(ab)
#define DP_TCL_CMD_RING_SIZE		32
#define DP_TCL_STATUS_RING_SIZE		32
#define DP_REO_DST_RING_MAX		8
#define DP_REO_DST_RING_SIZE		2048
#define DP_REO_REINJECT_RING_SIZE	32
#define DP_RX_RELEASE_RING_SIZE		1024
#define DP_REO_EXCEPTION_RING_SIZE	128
#define DP_REO_CMD_RING_SIZE		256
#define DP_REO_STATUS_RING_SIZE		2048
#define DP_RXDMA_BUF_RING_SIZE		4096
#define DP_RX_MAC_BUF_RING_SIZE		2048
#define DP_RXDMA_REFILL_RING_SIZE	2048
#define DP_RXDMA_ERR_DST_RING_SIZE	1024
#define DP_RXDMA_MON_STATUS_RING_SIZE	1024
#define DP_RXDMA_MONITOR_BUF_RING_SIZE(ab) \
	((ab)->profile_param->dp_params.rxdma_monitor_buf_ring_size)
#define DP_RXDMA_MONITOR_DST_RING_SIZE(ab) \
	((ab)->profile_param->dp_params.rxdma_monitor_dst_ring_size)
#define DP_RXDMA_MONITOR_DESC_RING_SIZE	4096
#define DP_TX_MONITOR_BUF_RING_SIZE	4096
#define DP_TX_MONITOR_DEST_RING_SIZE	2048

#define DP_TX_MONITOR_BUF_SIZE		2048
#define DP_TX_MONITOR_BUF_SIZE_MIN	48
#define DP_TX_MONITOR_BUF_SIZE_MAX	8192

#define DP_RX_BUFFER_SIZE	2048
#define DP_RX_BUFFER_SIZE_LITE	1024
#define DP_RX_BUFFER_ALIGN_SIZE	128

#define RX_MON_STATUS_BASE_BUF_SIZE	2048
#define RX_MON_STATUS_BUF_ALIGN		128
#define RX_MON_STATUS_BUF_RESERVATION	128
#define RX_MON_STATUS_BUF_SIZE		(RX_MON_STATUS_BASE_BUF_SIZE - \
				 (RX_MON_STATUS_BUF_RESERVATION + \
				  RX_MON_STATUS_BUF_ALIGN + \
				  SKB_DATA_ALIGN(sizeof(struct skb_shared_info))))

#define DP_RXDMA_BUF_COOKIE_BUF_ID	GENMASK(17, 0)
#define DP_RXDMA_BUF_COOKIE_PDEV_ID	GENMASK(19, 18)

#define DP_HW2SW_MACID(mac_id) ({ typeof(mac_id) x = (mac_id); x ? x - 1 : 0; })
#define DP_SW2HW_MACID(mac_id) ((mac_id) + 1)

#define DP_TX_DESC_ID_MAC_ID  GENMASK(1, 0)
#define DP_TX_DESC_ID_MSDU_ID GENMASK(18, 2)
#define DP_TX_DESC_ID_POOL_ID GENMASK(20, 19)

#define ATH12K_SHADOW_DP_TIMER_INTERVAL 20
#define ATH12K_SHADOW_CTRL_TIMER_INTERVAL 10

#define ATH12K_NUM_POOL_TX_DESC(ab) \
	((ab)->profile_param->dp_params.num_pool_tx_desc)
/* TODO: revisit this count during testing */
#define ATH12K_RX_DESC_COUNT(ab) \
	((ab)->profile_param->dp_params.rx_desc_count)

#define ATH12K_PAGE_SIZE	PAGE_SIZE

/* Total 1024 entries in PPT, i.e 4K/4 considering 4K aligned
 * SPT pages which makes lower 12bits 0
 */
#define ATH12K_MAX_PPT_ENTRIES	1024

/* Total 512 entries in a SPT, i.e 4K Page/8 */
#define ATH12K_MAX_SPT_ENTRIES	512

#define ATH12K_NUM_RX_SPT_PAGES(ab)	((ATH12K_RX_DESC_COUNT(ab)) / \
					  ATH12K_MAX_SPT_ENTRIES)

#define ATH12K_TX_SPT_PAGES_PER_POOL(ab) (ATH12K_NUM_POOL_TX_DESC(ab) / \
					  ATH12K_MAX_SPT_ENTRIES)
#define ATH12K_NUM_TX_SPT_PAGES(ab)	(ATH12K_TX_SPT_PAGES_PER_POOL(ab) * \
					 ATH12K_HW_MAX_QUEUES)

#define ATH12K_TX_SPT_PAGE_OFFSET 0
#define ATH12K_RX_SPT_PAGE_OFFSET(ab) ATH12K_NUM_TX_SPT_PAGES(ab)

/* The SPT pages are divided for RX and TX, first block for RX
 * and remaining for TX
 */
#define ATH12K_NUM_TX_SPT_PAGE_START(ab) ATH12K_NUM_RX_SPT_PAGES(ab)

#define ATH12K_DP_RX_DESC_MAGIC	0xBABABABA

/* 4K aligned address have last 12 bits set to 0, this check is done
 * so that two spt pages address can be stored per 8bytes
 * of CMEM (PPT)
 */
#define ATH12K_SPT_4K_ALIGN_CHECK 0xFFF
#define ATH12K_SPT_4K_ALIGN_OFFSET 12
#define ATH12K_PPT_ADDR_OFFSET(ppt_index) (4 * (ppt_index))

/* To indicate HW of CMEM address, b0-31 are cmem base received via QMI */
#define ATH12K_CMEM_ADDR_MSB 0x10

/* Of 20 bits cookie, b0-b8 is to indicate SPT offset and b9-19 for PPT */
#define ATH12K_CC_SPT_MSB 8
#define ATH12K_CC_PPT_MSB 19
#define ATH12K_CC_PPT_SHIFT 9
#define ATH12K_DP_CC_COOKIE_SPT	GENMASK(8, 0)
#define ATH12K_DP_CC_COOKIE_PPT	GENMASK(19, 9)

#define DP_REO_QREF_NUM		GENMASK(31, 16)
#define DP_MAX_PEER_ID		2047

/* Total size of the LUT is based on 2K peers, each having reference
 * for 17tids, note each entry is of type ath12k_reo_queue_ref
 * hence total size is 2048 * 17 * 8 = 278528
 */
#define DP_REOQ_LUT_SIZE	278528

/* Invalid TX Bank ID value */
#define DP_INVALID_BANK_ID -1

#define MAX_TQM_RELEASE_REASON 15
#define MAX_FW_TX_STATUS 7

struct ath12k_dp_tx_bank_profile {
	u8 is_configured;
	u32 num_users;
	u32 bank_config;
};

struct ath12k_hp_update_timer {
	struct timer_list timer;
	bool started;
	bool init;
	u32 tx_num;
	u32 timer_tx_num;
	u32 ring_id;
	u32 interval;
	struct ath12k_base *ab;
};

struct ath12k_rx_desc_info {
	struct list_head list;
	struct sk_buff *skb;
	u32 cookie;
	u32 magic;
	u8 in_use	: 1,
	   device_id	: 3,
	   reserved	: 4;
};

struct ath12k_tx_desc_info {
	struct list_head list;
	struct sk_buff *skb;
	struct sk_buff *skb_ext_desc;
	u32 desc_id; /* Cookie */
	u8 mac_id;
	u8 pool_id;
};

struct ath12k_tx_desc_params {
	struct sk_buff *skb;
	struct sk_buff *skb_ext_desc;
	u8 mac_id;
};

struct ath12k_spt_info {
	dma_addr_t paddr;
	u64 *vaddr;
};

struct ath12k_reo_queue_ref {
	u32 info0;
	u32 info1;
} __packed;

struct ath12k_reo_q_addr_lut {
	u32 *vaddr_unaligned;
	u32 *vaddr;
	dma_addr_t paddr_unaligned;
	dma_addr_t paddr;
	u32 size;
};

struct ath12k_link_stats {
	u32 tx_enqueued;
	u32 tx_completed;
	u32 tx_bcast_mcast;
	u32 tx_dropped;
	u32 tx_encap_type[DP_ENCAP_TYPE_MAX];
	u32 tx_encrypt_type[DP_ENCRYPT_TYPE_MAX];
	u32 tx_desc_type[DP_DESC_TYPE_MAX];
};

/* DP arch ops to communicate from common module
 * to arch specific module
 */
struct ath12k_dp_arch_ops {
	int (*service_srng)(struct ath12k_dp *dp,
			    struct ath12k_ext_irq_grp *irq_grp,
			    int budget);
	u32 (*tx_get_vdev_bank_config)(struct ath12k_base *ab,
				       struct ath12k_link_vif *arvif);
	int (*reo_cmd_send)(struct ath12k_base *ab,
			    struct ath12k_dp_rx_tid_rxq *rx_tid,
			    enum hal_reo_cmd_type type,
			    struct ath12k_hal_reo_cmd *cmd,
			    void (*cb)(struct ath12k_dp *dp, void *ctx,
				       enum hal_reo_cmd_status status));
	void (*setup_pn_check_reo_cmd)(struct ath12k_hal_reo_cmd *cmd,
				       struct ath12k_dp_rx_tid *rx_tid,
				       u32 cipher, enum set_key_cmd key_cmd);
	void (*rx_peer_tid_delete)(struct ath12k_base *ab,
				   struct ath12k_dp_link_peer *peer, u8 tid);
	int (*reo_cache_flush)(struct ath12k_base *ab,
			       struct ath12k_dp_rx_tid_rxq *rx_tid);
	int (*rx_link_desc_return)(struct ath12k_dp *dp,
				   struct ath12k_buffer_addr *buf_addr_info,
				   enum hal_wbm_rel_bm_act action);
	void (*rx_frags_cleanup)(struct ath12k_dp_rx_tid *rx_tid,
				 bool rel_link_desc);
	int (*peer_rx_tid_reo_update)(struct ath12k_dp *dp,
				      struct ath12k_dp_link_peer *peer,
				      struct ath12k_dp_rx_tid *rx_tid,
				      u32 ba_win_sz, u16 ssn,
				      bool update_ssn);
	int (*rx_assign_reoq)(struct ath12k_base *ab, struct ath12k_dp_peer *dp_peer,
			      struct ath12k_dp_rx_tid *rx_tid,
			      u16 ssn, enum hal_pn_type pn_type);
	void (*peer_rx_tid_qref_setup)(struct ath12k_base *ab, u16 peer_id, u16 tid,
				       dma_addr_t paddr);
	void (*peer_rx_tid_qref_reset)(struct ath12k_base *ab, u16 peer_id, u16 tid);
	int (*rx_tid_delete_handler)(struct ath12k_base *ab,
				     struct ath12k_dp_rx_tid_rxq *rx_tid);
};

struct ath12k_device_dp_tx_err_stats {
	/* TCL Ring Descriptor unavailable */
	u32 desc_na[DP_TCL_NUM_RING_MAX];
	/* Other failures during dp_tx due to mem allocation failure
	 * idr unavailable etc.
	 */
	atomic_t misc_fail;
};

struct ath12k_device_dp_stats {
	u32 err_ring_pkts;
	u32 invalid_rbm;
	u32 rxdma_error[HAL_REO_ENTR_RING_RXDMA_ECODE_MAX];
	u32 reo_error[HAL_REO_DEST_RING_ERROR_CODE_MAX];
	u32 hal_reo_error[DP_REO_DST_RING_MAX];
	struct ath12k_device_dp_tx_err_stats tx_err;
	u32 reo_rx[DP_REO_DST_RING_MAX][ATH12K_MAX_DEVICES];
	u32 rx_wbm_rel_source[HAL_WBM_REL_SRC_MODULE_MAX][ATH12K_MAX_DEVICES];
	u32 tqm_rel_reason[MAX_TQM_RELEASE_REASON];
	u32 fw_tx_status[MAX_FW_TX_STATUS];
	u32 tx_wbm_rel_source[HAL_WBM_REL_SRC_MODULE_MAX];
	u32 tx_enqueued[DP_TCL_NUM_RING_MAX];
	u32 tx_completed[DP_TCL_NUM_RING_MAX];
	u32 reo_excep_msdu_buf_type;
};

struct ath12k_dp {
	struct ath12k_base *ab;
	u32 mon_dest_ring_stuck_cnt;
	u8 num_bank_profiles;
	/* protects the access and update of bank_profiles */
	spinlock_t tx_bank_lock;
	struct ath12k_dp_tx_bank_profile *bank_profiles;
	enum ath12k_htc_ep_id eid;
	struct completion htt_tgt_version_received;
	u8 htt_tgt_ver_major;
	u8 htt_tgt_ver_minor;
	struct dp_link_desc_bank link_desc_banks[DP_LINK_DESC_BANKS_MAX];
	enum hal_rx_buf_return_buf_manager idle_link_rbm;
	struct dp_srng wbm_idle_ring;
	struct dp_srng wbm_desc_rel_ring;
	struct dp_srng reo_reinject_ring;
	struct dp_srng rx_rel_ring;
	struct dp_srng reo_except_ring;
	struct dp_srng reo_cmd_ring;
	struct dp_srng reo_status_ring;
	enum ath12k_peer_metadata_version peer_metadata_ver;
	struct dp_srng reo_dst_ring[DP_REO_DST_RING_MAX];
	struct dp_tx_ring tx_ring[DP_TCL_NUM_RING_MAX];
	struct hal_wbm_idle_scatter_list scatter_list[DP_IDLE_SCATTER_BUFS_MAX];
	struct list_head reo_cmd_update_rx_queue_list;
	struct list_head reo_cmd_cache_flush_list;
	u32 reo_cmd_cache_flush_count;
	/* protects access to below fields,
	 * - reo_cmd_update_rx_queue_list
	 * - reo_cmd_cache_flush_list
	 * - reo_cmd_cache_flush_count
	 */
	spinlock_t reo_rxq_flush_lock;
	struct list_head reo_cmd_list;
	/* protects access to below fields,
	 * - reo_cmd_list
	 */
	spinlock_t reo_cmd_lock;
	struct ath12k_hp_update_timer reo_cmd_timer;
	struct ath12k_hp_update_timer tx_ring_timer[DP_TCL_NUM_RING_MAX];
	struct ath12k_spt_info *spt_info;
	u32 num_spt_pages;
	u32 rx_ppt_base;
	struct ath12k_rx_desc_info **rxbaddr;
	struct ath12k_tx_desc_info **txbaddr;
	struct list_head rx_desc_free_list;
	/* protects the free desc list */
	spinlock_t rx_desc_lock;

	struct list_head tx_desc_free_list[ATH12K_HW_MAX_QUEUES];
	struct list_head tx_desc_used_list[ATH12K_HW_MAX_QUEUES];
	/* protects the free and used desc lists */
	spinlock_t tx_desc_lock[ATH12K_HW_MAX_QUEUES];

	struct dp_rxdma_ring rx_refill_buf_ring;
	struct dp_srng rx_mac_buf_ring[MAX_RXDMA_PER_PDEV];
	struct dp_srng rxdma_err_dst_ring[MAX_RXDMA_PER_PDEV];
	struct dp_rxdma_mon_ring rxdma_mon_buf_ring;
	struct dp_rxdma_mon_ring tx_mon_buf_ring;
	struct dp_rxdma_mon_ring rx_mon_status_refill_ring[MAX_RXDMA_PER_PDEV];
	struct ath12k_reo_q_addr_lut reoq_lut;
	struct ath12k_reo_q_addr_lut ml_reoq_lut;
	const struct ath12k_hw_params *hw_params;
	struct device *dev;
	struct ath12k_hal *hal;

	/* RCU on dp_pdevs[] provides a teardown synchronization mechanism,
	 * ensuring in-flight data path readers complete before reclaim. Writers
	 * update internal fields under their own synchronization, while readers of
	 * internal fields may perform lockless read if occasional inconsistency
	 * is acceptable or use additional synchronization for a coherent view.
	 *
	 * RCU is used for dp_pdevs[] at this stage to align with
	 * ab->pdevs_active[]. However, if the teardown paths ensure quiescence,
	 * both dp_pdevs[] and pdevs_active[] can be converted to plain pointers,
	 * removing RCU synchronize overhead.
	 *
	 * TODO: evaluate removal of RCU from dp_pdevs in the future
	 */
	struct ath12k_pdev_dp __rcu *dp_pdevs[MAX_RADIOS];

	struct ath12k_hw_group *ag;
	u8 device_id;

	/* Lock for protection of peers and rhead_peer_addr */
	spinlock_t dp_lock;

	struct ath12k_dp_arch_ops *ops;

	/* Linked list of struct ath12k_dp_link_peer */
	struct list_head peers;

	/* For rhash table init and deinit protection */
	struct mutex link_peer_rhash_tbl_lock;

	/* The rhashtable containing struct ath12k_link_peer keyed by mac addr */
	struct rhashtable *rhead_peer_addr;
	struct rhashtable_params rhash_peer_addr_param;
	struct ath12k_device_dp_stats device_stats;
};

static inline u32 ath12k_dp_arch_tx_get_vdev_bank_config(struct ath12k_dp *dp,
							 struct ath12k_link_vif *arvif)
{
	return dp->ops->tx_get_vdev_bank_config(dp->ab, arvif);
}

static inline int ath12k_dp_arch_reo_cmd_send(struct ath12k_dp *dp,
					      struct ath12k_dp_rx_tid_rxq *rx_tid,
					      enum hal_reo_cmd_type type,
					      struct ath12k_hal_reo_cmd *cmd,
					      void (*cb)(struct ath12k_dp *dp, void *ctx,
							 enum hal_reo_cmd_status status))
{
	return dp->ops->reo_cmd_send(dp->ab, rx_tid, type, cmd, cb);
}

static inline
void ath12k_dp_arch_setup_pn_check_reo_cmd(struct ath12k_dp *dp,
					   struct ath12k_hal_reo_cmd *cmd,
					   struct ath12k_dp_rx_tid *rx_tid,
					   u32 cipher,
					   enum set_key_cmd key_cmd)
{
	dp->ops->setup_pn_check_reo_cmd(cmd, rx_tid, cipher, key_cmd);
}

static inline void ath12k_dp_arch_rx_peer_tid_delete(struct ath12k_dp *dp,
						     struct ath12k_dp_link_peer *peer,
						     u8 tid)
{
	dp->ops->rx_peer_tid_delete(dp->ab, peer, tid);
}

static inline int ath12k_dp_arch_reo_cache_flush(struct ath12k_dp *dp,
						 struct ath12k_dp_rx_tid_rxq *rx_tid)
{
	return dp->ops->reo_cache_flush(dp->ab, rx_tid);
}

static inline
int ath12k_dp_arch_rx_link_desc_return(struct ath12k_dp *dp,
				       struct ath12k_buffer_addr *buf_addr_info,
				       enum hal_wbm_rel_bm_act action)
{
	return dp->ops->rx_link_desc_return(dp, buf_addr_info, action);
}

static inline
void ath12k_dp_arch_rx_frags_cleanup(struct ath12k_dp *dp,
				     struct ath12k_dp_rx_tid *rx_tid,
				     bool rel_link_desc)
{
	dp->ops->rx_frags_cleanup(rx_tid, rel_link_desc);
}

static inline int ath12k_dp_arch_peer_rx_tid_reo_update(struct ath12k_dp *dp,
							struct ath12k_dp_link_peer *peer,
							struct ath12k_dp_rx_tid *rx_tid,
							u32 ba_win_sz, u16 ssn,
							bool update_ssn)
{
	return dp->ops->peer_rx_tid_reo_update(dp, peer, rx_tid,
					       ba_win_sz, ssn, update_ssn);
}

static inline int ath12k_dp_arch_rx_assign_reoq(struct ath12k_dp *dp,
						struct ath12k_dp_peer *dp_peer,
						struct ath12k_dp_rx_tid *rx_tid,
						u16 ssn, enum hal_pn_type pn_type)
{
	return dp->ops->rx_assign_reoq(dp->ab, dp_peer, rx_tid, ssn, pn_type);
}

static inline void ath12k_dp_arch_peer_rx_tid_qref_setup(struct ath12k_dp *dp,
							 u16 peer_id, u16 tid,
							 dma_addr_t paddr)
{
	dp->ops->peer_rx_tid_qref_setup(dp->ab, peer_id, tid, paddr);
}

static inline void ath12k_dp_arch_peer_rx_tid_qref_reset(struct ath12k_dp *dp,
							 u16 peer_id, u16 tid)
{
	dp->ops->peer_rx_tid_qref_reset(dp->ab, peer_id, tid);
}

static inline
int ath12k_dp_arch_rx_tid_delete_handler(struct ath12k_dp *dp,
					 struct ath12k_dp_rx_tid_rxq *rx_tid)
{
	return dp->ops->rx_tid_delete_handler(dp->ab, rx_tid);
}

static inline void ath12k_dp_get_mac_addr(u32 addr_l32, u16 addr_h16, u8 *addr)
{
	memcpy(addr, &addr_l32, 4);
	memcpy(addr + 4, &addr_h16, ETH_ALEN - 4);
}

static inline struct ath12k_dp *
ath12k_dp_hw_grp_to_dp(struct ath12k_dp_hw_group *dp_hw_grp, u8 device_id)
{
	return dp_hw_grp->dp[device_id];
}

static inline int
ath12k_dp_service_srng(struct ath12k_dp *dp, struct ath12k_ext_irq_grp *irq_grp,
		       int budget)
{
	return dp->ops->service_srng(dp, irq_grp, budget);
}

static inline struct ieee80211_hw *
ath12k_pdev_dp_to_hw(struct ath12k_pdev_dp *pdev)
{
	return pdev->hw;
}

static inline struct ath12k_pdev_dp *
ath12k_dp_to_pdev_dp(struct ath12k_dp *dp, u8 pdev_idx)
{
	RCU_LOCKDEP_WARN(!rcu_read_lock_held(),
			 "ath12k dp to dp pdev called without rcu lock");

	return rcu_dereference(dp->dp_pdevs[pdev_idx]);
}

void ath12k_dp_vdev_tx_attach(struct ath12k *ar, struct ath12k_link_vif *arvif);
void ath12k_dp_partner_cc_init(struct ath12k_base *ab);
int ath12k_dp_pdev_alloc(struct ath12k_base *ab);
void ath12k_dp_pdev_pre_alloc(struct ath12k *ar);
void ath12k_dp_pdev_free(struct ath12k_base *ab);
int ath12k_dp_peer_setup(struct ath12k *ar, int vdev_id, const u8 *addr);
void ath12k_dp_peer_cleanup(struct ath12k *ar, int vdev_id, const u8 *addr);
void ath12k_dp_srng_cleanup(struct ath12k_base *ab, struct dp_srng *ring);
int ath12k_dp_srng_setup(struct ath12k_base *ab, struct dp_srng *ring,
			 enum hal_ring_type type, int ring_num,
			 int mac_id, int num_entries);
void ath12k_dp_link_desc_cleanup(struct ath12k_base *ab,
				 struct dp_link_desc_bank *desc_bank,
				 u32 ring_type, struct dp_srng *ring);
int ath12k_dp_link_desc_setup(struct ath12k_base *ab,
			      struct dp_link_desc_bank *link_desc_banks,
			      u32 ring_type, struct hal_srng *srng,
			      u32 n_link_desc);
struct ath12k_rx_desc_info *ath12k_dp_get_rx_desc(struct ath12k_dp *dp,
						  u32 cookie);
struct ath12k_tx_desc_info *ath12k_dp_get_tx_desc(struct ath12k_dp *dp,
						  u32 desc_id);
#endif
