// SPDX-License-Identifier: GPL-2.0-only
/*
 * Implementation of host-to-chip MIBs of WFxxx Split Mac (WSM) API.
 *
 * Copyright (c) 2017-2020, Silicon Laboratories, Inc.
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
	struct hif_mib_beacon_wake_up_period arg = {
		.wakeup_period_min = dtim_interval,
		.receive_dtim = 0,
		.wakeup_period_max = listen_interval,
	};

	if (dtim_interval > 0xFF || listen_interval > 0xFFFF)
		return -EINVAL;
	return hif_write_mib(wvif->wdev, wvif->id,
			     HIF_MIB_ID_BEACON_WAKEUP_PERIOD,
			     &arg, sizeof(arg));
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

int hif_get_counters_table(struct wfx_dev *wdev, int vif_id,
			   struct hif_mib_extended_count_table *arg)
{
	if (wfx_api_older_than(wdev, 1, 3)) {
		// extended_count_table is wider than count_table
		memset(arg, 0xFF, sizeof(*arg));
		return hif_read_mib(wdev, vif_id, HIF_MIB_ID_COUNTERS_TABLE,
				    arg, sizeof(struct hif_mib_count_table));
	} else {
		return hif_read_mib(wdev, vif_id,
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
		      bool filter_bssid, bool filter_prbreq)
{
	struct hif_mib_rx_filter arg = { };

	if (filter_bssid)
		arg.bssid_filter = 1;
	if (!filter_prbreq)
		arg.fwd_probe_req = 1;
	return hif_write_mib(wvif->wdev, wvif->id, HIF_MIB_ID_RX_FILTER,
			     &arg, sizeof(arg));
}

int hif_set_beacon_filter_table(struct wfx_vif *wvif, int tbl_len,
				const struct hif_ie_table_entry *tbl)
{
	int ret;
	struct hif_mib_bcn_filter_table *arg;
	int buf_len = struct_size(arg, ie_table, tbl_len);

	arg = kzalloc(buf_len, GFP_KERNEL);
	if (!arg)
		return -ENOMEM;
	arg->num_of_info_elmts = cpu_to_le32(tbl_len);
	memcpy(arg->ie_table, tbl, flex_array_size(arg, ie_table, tbl_len));
	ret = hif_write_mib(wvif->wdev, wvif->id,
			    HIF_MIB_ID_BEACON_FILTER_TABLE, arg, buf_len);
	kfree(arg);
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
	struct hif_mib_gl_operational_power_mode arg = {
		.power_mode = mode,
		.wup_ind_activation = 1,
	};

	return hif_write_mib(wdev, -1, HIF_MIB_ID_GL_OPERATIONAL_POWER_MODE,
			     &arg, sizeof(arg));
}

int hif_set_template_frame(struct wfx_vif *wvif, struct sk_buff *skb,
			   u8 frame_type, int init_rate)
{
	struct hif_mib_template_frame *arg;

	WARN(skb->len > HIF_API_MAX_TEMPLATE_FRAME_SIZE, "frame is too big");
	skb_push(skb, 4);
	arg = (struct hif_mib_template_frame *)skb->data;
	skb_pull(skb, 4);
	arg->init_rate = init_rate;
	arg->frame_type = frame_type;
	arg->frame_length = cpu_to_le16(skb->len);
	return hif_write_mib(wvif->wdev, wvif->id, HIF_MIB_ID_TEMPLATE_FRAME,
			     arg, sizeof(*arg) + skb->len);
}

int hif_set_mfp(struct wfx_vif *wvif, bool capable, bool required)
{
	struct hif_mib_protected_mgmt_policy arg = { };

	WARN(required && !capable, "incoherent arguments");
	if (capable) {
		arg.pmf_enable = 1;
		arg.host_enc_auth_frames = 1;
	}
	if (!required)
		arg.unpmf_allowed = 1;
	return hif_write_mib(wvif->wdev, wvif->id,
			     HIF_MIB_ID_PROTECTED_MGMT_POLICY,
			     &arg, sizeof(arg));
}

int hif_set_block_ack_policy(struct wfx_vif *wvif,
			     u8 tx_tid_policy, u8 rx_tid_policy)
{
	struct hif_mib_block_ack_policy arg = {
		.block_ack_tx_tid_policy = tx_tid_policy,
		.block_ack_rx_tid_policy = rx_tid_policy,
	};

	return hif_write_mib(wvif->wdev, wvif->id, HIF_MIB_ID_BLOCK_ACK_POLICY,
			     &arg, sizeof(arg));
}

int hif_set_association_mode(struct wfx_vif *wvif, int ampdu_density,
			     bool greenfield, bool short_preamble)
{
	struct hif_mib_set_association_mode arg = {
		.preambtype_use = 1,
		.mode = 1,
		.spacing = 1,
		.short_preamble = short_preamble,
		.greenfield = greenfield,
		.mpdu_start_spacing = ampdu_density,
	};

	return hif_write_mib(wvif->wdev, wvif->id,
			     HIF_MIB_ID_SET_ASSOCIATION_MODE, &arg, sizeof(arg));
}

int hif_set_tx_rate_retry_policy(struct wfx_vif *wvif,
				 int policy_index, u8 *rates)
{
	struct hif_mib_set_tx_rate_retry_policy *arg;
	size_t size = struct_size(arg, tx_rate_retry_policy, 1);
	int ret;

	arg = kzalloc(size, GFP_KERNEL);
	if (!arg)
		return -ENOMEM;
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
