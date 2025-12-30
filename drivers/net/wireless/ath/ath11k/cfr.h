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

enum ath11k_cfr_correlate_event_type {
	ATH11K_CORRELATE_DBR_EVENT,
	ATH11K_CORRELATE_TX_EVENT,
};

struct ath11k_sta;
struct ath11k_per_peer_cfr_capture;

#define ATH11K_CFR_START_MAGIC 0xDEADBEAF
#define ATH11K_CFR_END_MAGIC 0xBEAFDEAD

#define VENDOR_QCA 0x8cfdf0
#define PLATFORM_TYPE_ARM 2

enum ath11k_cfr_meta_version {
	ATH11K_CFR_META_VERSION_NONE,
	ATH11K_CFR_META_VERSION_1,
	ATH11K_CFR_META_VERSION_2,
	ATH11K_CFR_META_VERSION_3,
	ATH11K_CFR_META_VERSION_4,
	ATH11K_CFR_META_VERSION_MAX = 0xFF,
};

enum ath11k_cfr_data_version {
	ATH11K_CFR_DATA_VERSION_NONE,
	ATH11K_CFR_DATA_VERSION_1,
	ATH11K_CFR_DATA_VERSION_MAX = 0xFF,
};

enum ath11k_cfr_capture_ack_mode {
	ATH11K_CFR_CAPTURE_LEGACY_ACK,
	ATH11K_CFR_CAPTURE_DUP_LEGACY_ACK,
	ATH11K_CFR_CAPTURE_HT_ACK,
	ATH11K_CFR_CAPTURE_VHT_ACK,

	/*Always keep this at last*/
	ATH11K_CFR_CAPTURE_INVALID_ACK
};

enum ath11k_cfr_correlate_status {
	ATH11K_CORRELATE_STATUS_RELEASE,
	ATH11K_CORRELATE_STATUS_HOLD,
	ATH11K_CORRELATE_STATUS_ERR,
};

enum ath11k_cfr_preamble_type {
	ATH11K_CFR_PREAMBLE_TYPE_LEGACY,
	ATH11K_CFR_PREAMBLE_TYPE_HT,
	ATH11K_CFR_PREAMBLE_TYPE_VHT,
};

struct ath11k_cfr_peer_tx_param {
	u32 capture_method;
	u32 vdev_id;
	u8 peer_mac_addr[ETH_ALEN];
	u32 primary_20mhz_chan;
	u32 bandwidth;
	u32 phy_mode;
	u32 band_center_freq1;
	u32 band_center_freq2;
	u32 spatial_streams;
	u32 correlation_info_1;
	u32 correlation_info_2;
	u32 status;
	u32 timestamp_us;
	u32 counter;
	u32 chain_rssi[WMI_MAX_CHAINS];
	u16 chain_phase[WMI_MAX_CHAINS];
	u32 cfo_measurement;
	u8 agc_gain[HOST_MAX_CHAINS];
	u32 rx_start_ts;
};

struct cfr_metadata {
	u8 peer_addr[ETH_ALEN];
	u8 status;
	u8 capture_bw;
	u8 channel_bw;
	u8 phy_mode;
	u16 prim20_chan;
	u16 center_freq1;
	u16 center_freq2;
	u8 capture_mode;
	u8 capture_type;
	u8 sts_count;
	u8 num_rx_chain;
	u32 timestamp;
	u32 length;
	u32 chain_rssi[HOST_MAX_CHAINS];
	u16 chain_phase[HOST_MAX_CHAINS];
	u32 cfo_measurement;
	u8 agc_gain[HOST_MAX_CHAINS];
	u32 rx_start_ts;
} __packed;

struct ath11k_csi_cfr_header {
	u32 start_magic_num;
	u32 vendorid;
	u8 cfr_metadata_version;
	u8 cfr_data_version;
	u8 chip_type;
	u8 platform_type;
	u32 cfr_metadata_len;
	struct cfr_metadata meta_data;
} __packed;

#define TONES_IN_20MHZ  256
#define TONES_IN_40MHZ  512
#define TONES_IN_80MHZ  1024
#define TONES_IN_160MHZ 2048 /* 160 MHz isn't supported yet */
#define TONES_INVALID   0

#define CFIR_DMA_HDR_INFO0_TAG GENMASK(7, 0)
#define CFIR_DMA_HDR_INFO0_LEN GENMASK(13, 8)

#define CFIR_DMA_HDR_INFO1_UPLOAD_DONE      GENMASK(0, 0)
#define CFIR_DMA_HDR_INFO1_CAPTURE_TYPE     GENMASK(3, 1)
#define CFIR_DMA_HDR_INFO1_PREAMBLE_TYPE    GENMASK(5, 4)
#define CFIR_DMA_HDR_INFO1_NSS              GENMASK(8, 6)
#define CFIR_DMA_HDR_INFO1_NUM_CHAINS       GENMASK(11, 9)
#define CFIR_DMA_HDR_INFO1_UPLOAD_PKT_BW    GENMASK(14, 12)
#define CFIR_DMA_HDR_INFO1_SW_PEER_ID_VALID GENMASK(15, 15)

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
	struct ath11k_csi_cfr_header header;
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
	enum wmi_phy_mode phymode;
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
struct ath11k_dbring *ath11k_cfr_get_dbring(struct ath11k *ar);
void ath11k_cfr_release_lut_entry(struct ath11k_look_up_table *lut);
int ath11k_process_cfr_capture_event(struct ath11k_base *ab,
				     struct ath11k_cfr_peer_tx_param *params);
void ath11k_cfr_update_phymode(struct ath11k *ar, enum wmi_phy_mode phymode);
#else
static inline void ath11k_cfr_update_phymode(struct ath11k *ar,
					     enum wmi_phy_mode phymode)
{
}

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

static inline void ath11k_cfr_release_lut_entry(struct ath11k_look_up_table *lut)
{
}

static inline
struct ath11k_dbring *ath11k_cfr_get_dbring(struct ath11k *ar)
{
	return NULL;
}

static inline
int ath11k_process_cfr_capture_event(struct ath11k_base *ab,
				     struct ath11k_cfr_peer_tx_param *params)
{
	return 0;
}
#endif /* CONFIG_ATH11K_CFR */
#endif /* ATH11K_CFR_H */
