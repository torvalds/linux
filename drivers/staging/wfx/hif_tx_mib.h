/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Implementation of host-to-chip MIBs of WFxxx Split Mac (WSM) API.
 *
 * Copyright (c) 2017-2019, Silicon Laboratories, Inc.
 * Copyright (c) 2010, ST-Ericsson
 * Copyright (C) 2010, ST-Ericsson SA
 */
#ifndef WFX_HIF_TX_MIB_H
#define WFX_HIF_TX_MIB_H

#include "hif_api_mib.h"

struct wfx_vif;
struct sk_buff;

int hif_set_output_power(struct wfx_vif *wvif, int val);
int hif_set_beacon_wakeup_period(struct wfx_vif *wvif,
				 unsigned int dtim_interval,
				 unsigned int listen_interval);
int hif_set_rcpi_rssi_threshold(struct wfx_vif *wvif,
				int rssi_thold, int rssi_hyst);
int hif_get_counters_table(struct wfx_dev *wdev,
			   struct hif_mib_extended_count_table *arg);
int hif_set_macaddr(struct wfx_vif *wvif, u8 *mac);
int hif_set_rx_filter(struct wfx_vif *wvif,
		      bool filter_bssid, bool fwd_probe_req);
int hif_set_beacon_filter_table(struct wfx_vif *wvif, int tbl_len,
				const struct hif_ie_table_entry *tbl);
int hif_beacon_filter_control(struct wfx_vif *wvif,
			      int enable, int beacon_count);
int hif_set_operational_mode(struct wfx_dev *wdev, enum hif_op_power_mode mode);
int hif_set_template_frame(struct wfx_vif *wvif, struct sk_buff *skb,
			   u8 frame_type, int init_rate);
int hif_set_mfp(struct wfx_vif *wvif, bool capable, bool required);
int hif_set_block_ack_policy(struct wfx_vif *wvif,
			     u8 tx_tid_policy, u8 rx_tid_policy);
int hif_set_association_mode(struct wfx_vif *wvif,
			     struct ieee80211_bss_conf *info);
int hif_set_tx_rate_retry_policy(struct wfx_vif *wvif,
				 int policy_index, uint8_t *rates);
int hif_set_mac_addr_condition(struct wfx_vif *wvif,
			       int idx, const u8 *mac_addr);
int hif_set_uc_mc_bc_condition(struct wfx_vif *wvif,
			       int idx, u8 allowed_frames);
int hif_set_config_data_filter(struct wfx_vif *wvif, bool enable, int idx,
			       int mac_filters, int frames_types_filters);
int hif_set_data_filtering(struct wfx_vif *wvif, bool enable, bool invert);
int hif_keep_alive_period(struct wfx_vif *wvif, int period);
int hif_set_arp_ipv4_filter(struct wfx_vif *wvif, int idx, __be32 *addr);
int hif_use_multi_tx_conf(struct wfx_dev *wdev, bool enable);
int hif_set_uapsd_info(struct wfx_vif *wvif, unsigned long val);
int hif_erp_use_protection(struct wfx_vif *wvif, bool enable);
int hif_slot_time(struct wfx_vif *wvif, int val);
int hif_dual_cts_protection(struct wfx_vif *wvif, bool enable);
int hif_wep_default_key_id(struct wfx_vif *wvif, int val);
int hif_rts_threshold(struct wfx_vif *wvif, int val);

#endif
