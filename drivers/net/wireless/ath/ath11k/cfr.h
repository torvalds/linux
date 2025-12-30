/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2020-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef ATH11K_CFR_H
#define ATH11K_CFR_H

#include "dbring.h"
#include "wmi.h"

#define ATH11K_CFR_NUM_RESP_PER_EVENT   1
#define ATH11K_CFR_EVENT_TIMEOUT_MS     1
#define ATH11K_CFR_NUM_RING_ENTRIES     1

#define ATH11K_MAX_CFR_ENABLED_CLIENTS 10
#define CFR_MAX_LUT_ENTRIES 136

#define HOST_MAX_CHAINS 8

struct ath11k_sta;
struct ath11k_per_peer_cfr_capture;

struct ath11k_cfr_dma_hdr {
	u16 info0;
	u16 info1;
	u16 sw_peer_id;
	u16 phy_ppdu_id;
};

struct ath11k_look_up_table {
	bool dbr_recv;
	bool tx_recv;
	u8 *data;
	u32 data_len;
	u16 dbr_ppdu_id;
	u16 tx_ppdu_id;
	dma_addr_t dbr_address;
	struct ath11k_cfr_dma_hdr hdr;
	u64 txrx_tstamp;
	u64 dbr_tstamp;
	u32 header_length;
	u32 payload_length;
	struct ath11k_dbring_element *buff;
};

struct cfr_unassoc_pool_entry {
	u8 peer_mac[ETH_ALEN];
	u32 period;
	bool is_valid;
};

struct ath11k_cfr {
	struct ath11k_dbring rx_ring;
	/* Protects cfr data */
	spinlock_t lock;
	/* Protect for lut entries */
	spinlock_t lut_lock;
	struct ath11k_look_up_table *lut;
	struct dentry *enable_cfr;
	struct dentry *cfr_unassoc;
	struct rchan *rfs_cfr_capture;
	u8 cfr_enabled_peer_cnt;
	u32 lut_num;
	u64 tx_evt_cnt;
	u64 dbr_evt_cnt;
	u64 release_cnt;
	u64 tx_peer_status_cfr_fail;
	u64 tx_evt_status_cfr_fail;
	u64 tx_dbr_lookup_fail;
	u64 last_success_tstamp;
	u64 flush_dbr_cnt;
	u64 clear_txrx_event;
	u64 cfr_dma_aborts;
	bool enabled;
	struct cfr_unassoc_pool_entry unassoc_pool[ATH11K_MAX_CFR_ENABLED_CLIENTS];
};

enum ath11k_cfr_capture_method {
	ATH11K_CFR_CAPTURE_METHOD_NULL_FRAME,
	ATH11K_CFR_CAPTURE_METHOD_NULL_FRAME_WITH_PHASE,
	ATH11K_CFR_CAPTURE_METHOD_PROBE_RESP,
	ATH11K_CFR_CAPTURE_METHOD_MAX,
};

enum ath11k_cfr_capture_bw {
	ATH11K_CFR_CAPTURE_BW_20,
	ATH11K_CFR_CAPTURE_BW_40,
	ATH11K_CFR_CAPTURE_BW_80,
	ATH11K_CFR_CAPTURE_BW_MAX,
};

#ifdef CONFIG_ATH11K_CFR
int ath11k_cfr_init(struct ath11k_base *ab);
void ath11k_cfr_deinit(struct ath11k_base *ab);
void ath11k_cfr_lut_update_paddr(struct ath11k *ar, dma_addr_t paddr,
				 u32 buf_id);
void ath11k_cfr_decrement_peer_count(struct ath11k *ar,
				     struct ath11k_sta *arsta);
void ath11k_cfr_update_unassoc_pool_entry(struct ath11k *ar,
					  const u8 *peer_mac);
bool ath11k_cfr_peer_is_in_cfr_unassoc_pool(struct ath11k *ar,
					    const u8 *peer_mac);
void ath11k_cfr_update_unassoc_pool(struct ath11k *ar,
				    struct ath11k_per_peer_cfr_capture *params,
				    u8 *peer_mac);
int ath11k_cfr_send_peer_cfr_capture_cmd(struct ath11k *ar,
					 struct ath11k_sta *arsta,
					 struct ath11k_per_peer_cfr_capture *params,
					 const u8 *peer_mac);

#else
static inline int ath11k_cfr_init(struct ath11k_base *ab)
{
	return 0;
}

static inline void ath11k_cfr_deinit(struct ath11k_base *ab)
{
}

static inline void ath11k_cfr_lut_update_paddr(struct ath11k *ar,
					       dma_addr_t paddr, u32 buf_id)
{
}

static inline void ath11k_cfr_decrement_peer_count(struct ath11k *ar,
						   struct ath11k_sta *arsta)
{
}

static inline void ath11k_cfr_update_unassoc_pool_entry(struct ath11k *ar,
							const u8 *peer_mac)
{
}

static inline bool
ath11k_cfr_peer_is_in_cfr_unassoc_pool(struct ath11k *ar, const u8 *peer_mac)
{
	return false;
}

static inline void
ath11k_cfr_update_unassoc_pool(struct ath11k *ar,
			       struct ath11k_per_peer_cfr_capture *params,
			       u8 *peer_mac)
{
}

static inline int
ath11k_cfr_send_peer_cfr_capture_cmd(struct ath11k *ar,
				     struct ath11k_sta *arsta,
				     struct ath11k_per_peer_cfr_capture *params,
				     const u8 *peer_mac)
{
	return 0;
}
#endif /* CONFIG_ATH11K_CFR */
#endif /* ATH11K_CFR_H */
