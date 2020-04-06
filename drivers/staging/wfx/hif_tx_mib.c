// SPDX-License-Identifier: GPL-2.0-only
/*
 * Implementation of host-to-chip MIBs of WFxxx Split Mac (WSM) API.
 *
 * Copyright (c) 2017-2019, Silicon Laboratories, Inc.
 * Copyright (c) 2010, ST-Ericsson
 * Copyright (C) 2010, ST-Ericsson SA
 */

#include <linux/etherdevice.h>

#include "wfx.h"
#include "hif_tx.h"
#include "hif_tx_mib.h"
#include "hif_api_mib.h"

int hif_set_output_power(struct wfx_vif *wvif, int val)
{
	struct hif_mib_current_tx_power_level arg = {
		.power_level = cpu_to_le32(val * 10),
	};

	return hif_write_mib(wvif->wdev, wvif->id,
			     HIF_MIB_ID_CURRENT_TX_POWER_LEVEL,
			     &arg, sizeof(arg));
}

int hif_set_beacon_wakeup_period(struct wfx_vif *wvif,
				 unsigned int dtim_interval,
				 unsigned int listen_interval)
{
	struct hif_mib_beacon_wake_up_period val = {
		.wakeup_period_min = dtim_interval,
		.receive_dtim = 0,
		.wakeup_period_max = cpu_to_le16(listen_interval),
	};

	if (dtim_interval > 0xFF || listen_interval > 0xFFFF)
		return -EINVAL;
	return hif_write_mib(wvif->wdev, wvif->id,
			     HIF_MIB_ID_BEACON_WAKEUP_PERIOD,
			     &val, sizeof(val));
}

int hif_set_rcpi_rssi_threshold(struct wfx_vif *wvif,
				int rssi_thold, int rssi_hyst)
{
	struct hif_mib_rcpi_rssi_threshold arg = {
		.rolling_average_count = 8,
		.detection = 1,
	};

	if (!rssi_thold && !rssi_hyst) {
		arg.upperthresh = 1;
		arg.lowerthresh = 1;
	} else {
		arg.upper_threshold = rssi_thold + rssi_hyst;
		arg.upper_threshold = (arg.upper_threshold + 110) * 2;
		arg.lower_threshold = rssi_thold;
		arg.lower_threshold = (arg.lower_threshold + 110) * 2;
	}

	return hif_write_mib(wvif->wdev, wvif->id,
			     HIF_MIB_ID_RCPI_RSSI_THRESHOLD, &arg, sizeof(arg));
}

int hif_get_counters_table(struct wfx_dev *wdev,
			   struct hif_mib_extended_count_table *arg)
{
	if (wfx_api_older_than(wdev, 1, 3)) {
		// extended_count_table is wider than count_table
		memset(arg, 0xFF, sizeof(*arg));
		return hif_read_mib(wdev, 0, HIF_MIB_ID_COUNTERS_TABLE,
				    arg, sizeof(struct hif_mib_count_table));
	} else {
		return hif_read_mib(wdev, 0,
				    HIF_MIB_ID_EXTENDED_COUNTERS_TABLE, arg,
				sizeof(struct hif_mib_extended_count_table));
	}
}

int hif_set_macaddr(struct wfx_vif *wvif, u8 *mac)
{
	struct hif_mib_mac_address msg = { };

	if (mac)
		ether_addr_copy(msg.mac_addr, mac);
	return hif_write_mib(wvif->wdev, wvif->id, HIF_MIB_ID_DOT11_MAC_ADDRESS,
			     &msg, sizeof(msg));
}

int hif_set_rx_filter(struct wfx_vif *wvif,
		      bool filter_bssid, bool fwd_probe_req)
{
	struct hif_mib_rx_filter val = { };

	if (filter_bssid)
		val.bssid_filter = 1;
	if (fwd_probe_req)
		val.fwd_probe_req = 1;
	return hif_write_mib(wvif->wdev, wvif->id, HIF_MIB_ID_RX_FILTER,
			     &val, sizeof(val));
}

int hif_set_beacon_filter_table(struct wfx_vif *wvif, int tbl_len,
				const struct hif_ie_table_entry *tbl)
{
	int ret;
	struct hif_mib_bcn_filter_table *val;
	int buf_len = struct_size(val, ie_table, tbl_len);

	val = kzalloc(buf_len, GFP_KERNEL);
	if (!val)
		return -ENOMEM;
	val->num_of_info_elmts = cpu_to_le32(tbl_len);
	memcpy(val->ie_table, tbl, tbl_len * sizeof(*tbl));
	ret = hif_write_mib(wvif->wdev, wvif->id,
			    HIF_MIB_ID_BEACON_FILTER_TABLE, val, buf_len);
	kfree(val);
	return ret;
}

int hif_beacon_filter_control(struct wfx_vif *wvif,
			      int enable, int beacon_count)
{
	struct hif_mib_bcn_filter_enable arg = {
		.enable = cpu_to_le32(enable),
		.bcn_count = cpu_to_le32(beacon_count),
	};
	return hif_write_mib(wvif->wdev, wvif->id,
			     HIF_MIB_ID_BEACON_FILTER_ENABLE,
			     &arg, sizeof(arg));
}

int hif_set_operational_mode(struct wfx_dev *wdev, enum hif_op_power_mode mode)
{
	struct hif_mib_gl_operational_power_mode val = {
		.power_mode = mode,
		.wup_ind_activation = 1,
	};

	return hif_write_mib(wdev, -1, HIF_MIB_ID_GL_OPERATIONAL_POWER_MODE,
			     &val, sizeof(val));
}

int hif_set_template_frame(struct wfx_vif *wvif, struct sk_buff *skb,
			   u8 frame_type, int init_rate)
{
	struct hif_mib_template_frame *arg;

	skb_push(skb, 4);
	arg = (struct hif_mib_template_frame *)skb->data;
	skb_pull(skb, 4);
	arg->init_rate = init_rate;
	arg->frame_type = frame_type;
	arg->frame_length = cpu_to_le16(skb->len);
	return hif_write_mib(wvif->wdev, wvif->id, HIF_MIB_ID_TEMPLATE_FRAME,
			     arg, sizeof(*arg));
}

int hif_set_mfp(struct wfx_vif *wvif, bool capable, bool required)
{
	struct hif_mib_protected_mgmt_policy val = { };

	WARN(required && !capable, "incoherent arguments");
	if (capable) {
		val.pmf_enable = 1;
		val.host_enc_auth_frames = 1;
	}
	if (!required)
		val.unpmf_allowed = 1;
	return hif_write_mib(wvif->wdev, wvif->id,
			     HIF_MIB_ID_PROTECTED_MGMT_POLICY,
			     &val, sizeof(val));
}

int hif_set_block_ack_policy(struct wfx_vif *wvif,
			     u8 tx_tid_policy, u8 rx_tid_policy)
{
	struct hif_mib_block_ack_policy val = {
		.block_ack_tx_tid_policy = tx_tid_policy,
		.block_ack_rx_tid_policy = rx_tid_policy,
	};

	return hif_write_mib(wvif->wdev, wvif->id, HIF_MIB_ID_BLOCK_ACK_POLICY,
			     &val, sizeof(val));
}

int hif_set_association_mode(struct wfx_vif *wvif,
			     struct ieee80211_bss_conf *info)
{
	int basic_rates = wfx_rate_mask_to_hw(wvif->wdev, info->basic_rates);
	struct ieee80211_sta *sta = NULL;
	struct hif_mib_set_association_mode val = {
		.preambtype_use = 1,
		.mode = 1,
		.rateset = 1,
		.spacing = 1,
		.short_preamble = info->use_short_preamble,
		.basic_rate_set = cpu_to_le32(basic_rates)
	};

	rcu_read_lock(); // protect sta
	if (info->bssid && !info->ibss_joined)
		sta = ieee80211_find_sta(wvif->vif, info->bssid);

	// FIXME: it is strange to not retrieve all information from bss_info
	if (sta && sta->ht_cap.ht_supported) {
		val.mpdu_start_spacing = sta->ht_cap.ampdu_density;
		if (!(info->ht_operation_mode & IEEE80211_HT_OP_MODE_NON_GF_STA_PRSNT))
			val.greenfield = !!(sta->ht_cap.cap & IEEE80211_HT_CAP_GRN_FLD);
	}
	rcu_read_unlock();

	return hif_write_mib(wvif->wdev, wvif->id,
			     HIF_MIB_ID_SET_ASSOCIATION_MODE, &val, sizeof(val));
}

int hif_set_tx_rate_retry_policy(struct wfx_vif *wvif,
				 int policy_index, uint8_t *rates)
{
	struct hif_mib_set_tx_rate_retry_policy *arg;
	size_t size = struct_size(arg, tx_rate_retry_policy, 1);
	int ret;

	arg = kzalloc(size, GFP_KERNEL);
	arg->num_tx_rate_policies = 1;
	arg->tx_rate_retry_policy[0].policy_index = policy_index;
	arg->tx_rate_retry_policy[0].short_retry_count = 255;
	arg->tx_rate_retry_policy[0].long_retry_count = 255;
	arg->tx_rate_retry_policy[0].first_rate_sel = 1;
	arg->tx_rate_retry_policy[0].terminate = 1;
	arg->tx_rate_retry_policy[0].count_init = 1;
	memcpy(&arg->tx_rate_retry_policy[0].rates, rates,
	       sizeof(arg->tx_rate_retry_policy[0].rates));
	ret = hif_write_mib(wvif->wdev, wvif->id,
			    HIF_MIB_ID_SET_TX_RATE_RETRY_POLICY, arg, size);
	kfree(arg);
	return ret;
}

int hif_set_mac_addr_condition(struct wfx_vif *wvif,
			       int idx, const u8 *mac_addr)
{
	struct hif_mib_mac_addr_data_frame_condition val = {
		.condition_idx = idx,
		.address_type = HIF_MAC_ADDR_A1,
	};

	ether_addr_copy(val.mac_address, mac_addr);
	return hif_write_mib(wvif->wdev, wvif->id,
			     HIF_MIB_ID_MAC_ADDR_DATAFRAME_CONDITION,
			     &val, sizeof(val));
}

int hif_set_uc_mc_bc_condition(struct wfx_vif *wvif, int idx, u8 allowed_frames)
{
	struct hif_mib_uc_mc_bc_data_frame_condition val = {
		.condition_idx = idx,
		.allowed_frames = allowed_frames,
	};

	return hif_write_mib(wvif->wdev, wvif->id,
			     HIF_MIB_ID_UC_MC_BC_DATAFRAME_CONDITION,
			     &val, sizeof(val));
}

int hif_set_config_data_filter(struct wfx_vif *wvif, bool enable, int idx,
			       int mac_filters, int frames_types_filters)
{
	struct hif_mib_config_data_filter val = {
		.enable = enable,
		.filter_idx = idx,
		.mac_cond = mac_filters,
		.uc_mc_bc_cond = frames_types_filters,
	};

	return hif_write_mib(wvif->wdev, wvif->id,
			     HIF_MIB_ID_CONFIG_DATA_FILTER, &val, sizeof(val));
}

int hif_set_data_filtering(struct wfx_vif *wvif, bool enable, bool invert)
{
	struct hif_mib_set_data_filtering val = {
		.enable = enable,
		.invert_matching = invert,
	};

	return hif_write_mib(wvif->wdev, wvif->id,
			     HIF_MIB_ID_SET_DATA_FILTERING, &val, sizeof(val));
}

int hif_keep_alive_period(struct wfx_vif *wvif, int period)
{
	struct hif_mib_keep_alive_period arg = {
		.keep_alive_period = cpu_to_le16(period),
	};

	return hif_write_mib(wvif->wdev, wvif->id, HIF_MIB_ID_KEEP_ALIVE_PERIOD,
			     &arg, sizeof(arg));
};

int hif_set_arp_ipv4_filter(struct wfx_vif *wvif, int idx, __be32 *addr)
{
	struct hif_mib_arp_ip_addr_table arg = {
		.condition_idx = idx,
		.arp_enable = HIF_ARP_NS_FILTERING_DISABLE,
	};

	if (addr) {
		// Caution: type of addr is __be32
		memcpy(arg.ipv4_address, addr, sizeof(arg.ipv4_address));
		arg.arp_enable = HIF_ARP_NS_FILTERING_ENABLE;
	}
	return hif_write_mib(wvif->wdev, wvif->id,
			     HIF_MIB_ID_ARP_IP_ADDRESSES_TABLE,
			     &arg, sizeof(arg));
}

int hif_use_multi_tx_conf(struct wfx_dev *wdev, bool enable)
{
	struct hif_mib_gl_set_multi_msg arg = {
		.enable_multi_tx_conf = enable,
	};

	return hif_write_mib(wdev, -1, HIF_MIB_ID_GL_SET_MULTI_MSG,
			     &arg, sizeof(arg));
}

int hif_set_uapsd_info(struct wfx_vif *wvif, unsigned long val)
{
	struct hif_mib_set_uapsd_information arg = { };

	if (val & BIT(IEEE80211_AC_VO))
		arg.trig_voice = 1;
	if (val & BIT(IEEE80211_AC_VI))
		arg.trig_video = 1;
	if (val & BIT(IEEE80211_AC_BE))
		arg.trig_be = 1;
	if (val & BIT(IEEE80211_AC_BK))
		arg.trig_bckgrnd = 1;
	return hif_write_mib(wvif->wdev, wvif->id,
			     HIF_MIB_ID_SET_UAPSD_INFORMATION,
			     &arg, sizeof(arg));
}

int hif_erp_use_protection(struct wfx_vif *wvif, bool enable)
{
	struct hif_mib_non_erp_protection arg = {
		.use_cts_to_self = enable,
	};

	return hif_write_mib(wvif->wdev, wvif->id,
			     HIF_MIB_ID_NON_ERP_PROTECTION, &arg, sizeof(arg));
}

int hif_slot_time(struct wfx_vif *wvif, int val)
{
	struct hif_mib_slot_time arg = {
		.slot_time = cpu_to_le32(val),
	};

	return hif_write_mib(wvif->wdev, wvif->id, HIF_MIB_ID_SLOT_TIME,
			     &arg, sizeof(arg));
}

int hif_dual_cts_protection(struct wfx_vif *wvif, bool enable)
{
	struct hif_mib_set_ht_protection arg = {
		.dual_cts_prot = enable,
	};

	return hif_write_mib(wvif->wdev, wvif->id, HIF_MIB_ID_SET_HT_PROTECTION,
			     &arg, sizeof(arg));
}

int hif_wep_default_key_id(struct wfx_vif *wvif, int val)
{
	struct hif_mib_wep_default_key_id arg = {
		.wep_default_key_id = val,
	};

	return hif_write_mib(wvif->wdev, wvif->id,
			     HIF_MIB_ID_DOT11_WEP_DEFAULT_KEY_ID,
			     &arg, sizeof(arg));
}

int hif_rts_threshold(struct wfx_vif *wvif, int val)
{
	struct hif_mib_dot11_rts_threshold arg = {
		.threshold = cpu_to_le32(val >= 0 ? val : 0xFFFF),
	};

	return hif_write_mib(wvif->wdev, wvif->id,
			     HIF_MIB_ID_DOT11_RTS_THRESHOLD, &arg, sizeof(arg));
}
