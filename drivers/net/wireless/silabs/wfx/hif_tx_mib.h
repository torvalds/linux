/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Implementation of the host-to-chip MIBs of the hardware API.
 *
 * Copyright (c) 2017-2020, Silicon Laboratories, Inc.
 * Copyright (c) 2010, ST-Ericsson
 * Copyright (C) 2010, ST-Ericsson SA
 */
#ifndef WFX_HIF_TX_MIB_H
#define WFX_HIF_TX_MIB_H

#include <linux/types.h>

struct sk_buff;
struct wfx_vif;
struct wfx_dev;
struct wfx_hif_ie_table_entry;
struct wfx_hif_mib_extended_count_table;

int wfx_hif_set_output_power(struct wfx_vif *wvif, int val);
int wfx_hif_set_beacon_wakeup_period(struct wfx_vif *wvif,
				     unsigned int dtim_interval, unsigned int listen_interval);
int wfx_hif_set_rcpi_rssi_threshold(struct wfx_vif *wvif, int rssi_thold, int rssi_hyst);
int wfx_hif_get_counters_table(struct wfx_dev *wdev, int vif_id,
			       struct wfx_hif_mib_extended_count_table *arg);
int wfx_hif_set_macaddr(struct wfx_vif *wvif, u8 *mac);
int wfx_hif_set_rx_filter(struct wfx_vif *wvif, bool filter_bssid, bool fwd_probe_req);
int wfx_hif_set_beacon_filter_table(struct wfx_vif *wvif, int tbl_len,
				    const struct wfx_hif_ie_table_entry *tbl);
int wfx_hif_beacon_filter_control(struct wfx_vif *wvif, int enable, int beacon_count);
int wfx_hif_set_operational_mode(struct wfx_dev *wdev, enum wfx_hif_op_power_mode mode);
int wfx_hif_set_template_frame(struct wfx_vif *wvif, struct sk_buff *skb,
			       u8 frame_type, int init_rate);
int wfx_hif_set_mfp(struct wfx_vif *wvif, bool capable, bool required);
int wfx_hif_set_block_ack_policy(struct wfx_vif *wvif, u8 tx_tid_policy, u8 rx_tid_policy);
int wfx_hif_set_association_mode(struct wfx_vif *wvif, int ampdu_density,
				 bool greenfield, bool short_preamble);
int wfx_hif_set_tx_rate_retry_policy(struct wfx_vif *wvif, int policy_index, u8 *rates);
int wfx_hif_keep_alive_period(struct wfx_vif *wvif, int period);
int wfx_hif_set_arp_ipv4_filter(struct wfx_vif *wvif, int idx, __be32 *addr);
int wfx_hif_use_multi_tx_conf(struct wfx_dev *wdev, bool enable);
int wfx_hif_set_uapsd_info(struct wfx_vif *wvif, unsigned long val);
int wfx_hif_erp_use_protection(struct wfx_vif *wvif, bool enable);
int wfx_hif_slot_time(struct wfx_vif *wvif, int val);
int wfx_hif_wep_default_key_id(struct wfx_vif *wvif, int val);
int wfx_hif_rts_threshold(struct wfx_vif *wvif, int val);

#endif
