// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2026 Morse Micro
 */
#include "core.h"
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/crc32.h>
#include <net/mac80211.h>
#include <asm/div64.h>
#include <linux/kernel.h>
#include "hif.h"
#include "mac.h"
#include "bus.h"
#include "ps.h"
#include "rc.h"

/*
 * Arbitrary size limit for the filter command address list, to ensure that
 * the command does not exceed page/MTU size. This will be far greater than
 * the number of filters supported by the firmware.
 */
#define MCAST_FILTER_COUNT_MAX (1024 / sizeof(filter->addr_list[0]))

/* Calculate average RSSI for Rx status */
#define CALC_AVG_RSSI(_avg, _sample) ((((_avg) * 9 + (_sample)) / 10))

/*
 * When automatically trying MCS0 before MCS10, this is how many
 * MCS0 attempts to make
 */
#define MCS0_BEFORE_MCS10_COUNT (1)

/* Maximum TX power (default) */
#define MAX_TX_POWER_MBM (2200)

/*
 * Since S1G runs at 1/10th the clockrate of VHT, the worst-case
 * transmission time is significantly longer then that of non-S1G
 * PHYs.
 */
#define MM81X_FLUSH_TIMEOUT (16 * HZ)

/* Default queue count */
#define MM81X_HW_QUEUE_COUNT (4)

/* Max rates per skb */
#define MM81X_HW_MAX_RATES (4)

/* Max reported rates */
#define MM81X_HW_MAX_REPORT_RATES (4)

/* Max rate attempts */
#define MM81X_HW_MAX_RATE_TRIES (1)

/* Max sk pacing shift */
#define MM81X_HW_TX_SK_PACING_SHIFT (3)

/* NSS/MCS map values */
#define MM81X_NSS_MCS_BYTE_0 0xfe /* 1SS */
#define MM81X_NSS_MCS_BYTE_1 0x00
#define MM81X_NSS_MCS_BYTE_2 0xfc /* 1SS */
#define MM81X_NSS_MCS_BYTE_3 0x01
#define MM81X_NSS_MCS_BYTE_4 0x00

/* HW restart delay time before terminating hardware IF work items */
#define MM81X_HW_RESTART_DELAY_MS 20

/* clang-format off */

/* mm81x chips do not support 16MHz */
#define CHANS1G(channel, frequency, offset, chan_flags)		\
{								\
	.band = NL80211_BAND_S1GHZ,				\
	.center_freq = (frequency),				\
	.freq_offset = (offset),				\
	.hw_value = (channel),					\
	.flags = ((chan_flags) | IEEE80211_CHAN_NO_16MHZ),	\
	.max_antenna_gain = 0,					\
	.max_power = 30,					\
}

static struct ieee80211_channel mors_s1ghz_channels[] = {
	CHANS1G(1, 902, 500, IEEE80211_CHAN_S1G_NO_PRIMARY),
	CHANS1G(3, 903, 500, 0),
	CHANS1G(5, 904, 500, 0),
	CHANS1G(7, 905, 500, 0),
	CHANS1G(9, 906, 500, 0),
	CHANS1G(11, 907, 500, 0),
	CHANS1G(13, 908, 500, 0),
	CHANS1G(15, 909, 500, 0),
	CHANS1G(17, 910, 500, 0),
	CHANS1G(19, 911, 500, 0),
	CHANS1G(21, 912, 500, 0),
	CHANS1G(23, 913, 500, 0),
	CHANS1G(25, 914, 500, 0),
	CHANS1G(27, 915, 500, 0),
	CHANS1G(29, 916, 500, 0),
	CHANS1G(31, 917, 500, 0),
	CHANS1G(33, 918, 500, 0),
	CHANS1G(35, 919, 500, 0),
	CHANS1G(37, 920, 500, 0),
	CHANS1G(39, 921, 500, 0),
	CHANS1G(41, 922, 500, 0),
	CHANS1G(43, 923, 500, 0),
	CHANS1G(45, 924, 500, 0),
	CHANS1G(47, 925, 500, 0),
	CHANS1G(49, 926, 500, 0),
	CHANS1G(51, 927, 500, IEEE80211_CHAN_S1G_NO_PRIMARY),
};

/* clang-format on */

static struct ieee80211_supported_band mors_band_s1ghz = {
	.band = NL80211_BAND_S1GHZ,
	.s1g_cap.s1g = true,
	.channels = mors_s1ghz_channels,
	.n_channels = ARRAY_SIZE(mors_s1ghz_channels),
	.bitrates = NULL,
	.n_bitrates = 0,
	.s1g_cap.cap[4] = 0x80 /* STA type sensor only for AP & STA */
};

static struct ieee80211_iface_limit mors_if_limits[] = {
	{
		.max = MM81X_MAX_IF,
		.types = BIT(NL80211_IFTYPE_STATION) | BIT(NL80211_IFTYPE_AP),
	},
};

static struct ieee80211_iface_combination mors_if_combs[] = {
	{
		.limits = mors_if_limits,
		.n_limits = ARRAY_SIZE(mors_if_limits),
		.max_interfaces = MM81X_MAX_IF,
		.num_different_channels = 1,
	},
};

/* Convert from a time in time units (1024us) to us */
#define MM81X_TU_TO_US(x) ((x) * 1024UL)

/* Convert from a time in time units (1024us) to ms */
#define MM81X_TU_TO_MS(x) (MM81X_TU_TO_US(x) / 1000UL)

/* Default time to dwell on a scan channel */
#define MM81X_HWSCAN_DEFAULT_DWELL_TIME_MS (30)

/* Default time to dwell on a scan channel for passive scan */
#define MM81X_HWSCAN_DEFAULT_PASSIVE_DWELL_TIME_MS (110)

/* Default time to dwell on home channel, in between scan channels */
#define MM81X_HWSCAN_DEFAULT_DWELL_ON_HOME_MS (200)

/* Typical time it takes to send the probe */
#define MM81X_HWSCAN_PROBE_DELAY_MS (30)

/* A margin to account for event/command processing */
#define MM81X_HWSCAN_TIMEOUT_OVERHEAD_MS (2000)

/* Scan channel frequency mask */
#define HW_SCAN_CH_LIST_FREQ_KHZ GENMASK(19, 0)

/*
 * Scan channel bandwidth mask.
 * Encoded as: 0 = 1MHz, 1 = 2MHz, 2 = 4MHz, 3 = 8MHz
 */
#define HW_SCAN_CH_LIST_OP_BW GENMASK(21, 20)

/*
 * Scan channel primary channel width.
 * Encoded as: 0 = 1MHz, 1 = 2MHz
 */
#define HW_SCAN_CH_LIST_PRIM_CH_WIDTH BIT(22)

/* Index into power_list for tx power of channel */
#define HW_SCAN_CH_LIST_PWR_LIST_IDX GENMASK(31, 26)

struct hw_scan_tlv_hdr {
	__le16 tag;
	__le16 len;
} __packed;

struct hw_scan_tlv_channel_list {
	struct hw_scan_tlv_hdr hdr;
	__le32 channels[];
} __packed;

struct hw_scan_tlv_power_list {
	struct hw_scan_tlv_hdr hdr;
	s32 tx_power_qdbm[];
} __packed;

struct hw_scan_tlv_probe_req {
	struct hw_scan_tlv_hdr hdr;
	/* Probe request frame template (including SSIDs) */
	u8 buf[];
} __packed;

struct hw_scan_tlv_dwell_on_home {
	struct hw_scan_tlv_hdr hdr;
	/* Time to dwell on home between scan channels */
	__le32 home_dwell_time_ms;
} __packed;

#define DOT11AH_BA_MAX_MPDU_PER_AMPDU (32)

/* wiphy scan params */
#define MM81X_MAX_SCAN_IE_LEN 512
#define MM81X_MAX_SCAN_SSIDS 1
#define MM81X_MAX_REMAIN_ON_CHAN_DURATION 10000

static bool mm81x_reg_h_cc_equal(const char *cc1, const char *cc2)
{
	return (cc1[0] == cc2[0]) && (cc1[1] == cc2[1]);
}

static bool mm81x_tx_h_pkt_over_rts_threshold(struct mm81x *mors,
					      struct ieee80211_tx_info *info,
					      struct sk_buff *skb)
{
	u8 ccmp_len;

	if (!info->control.hw_key)
		return ((skb->len + FCS_LEN) > mors->rts_threshold);

	if (info->control.hw_key->keylen == 32)
		ccmp_len =
			IEEE80211_CCMP_256_HDR_LEN + IEEE80211_CCMP_256_MIC_LEN;
	else if (info->control.hw_key->keylen == 16)
		ccmp_len = IEEE80211_CCMP_HDR_LEN + IEEE80211_CCMP_MIC_LEN;
	else
		ccmp_len = 0;

	return ((skb->len + FCS_LEN + ccmp_len) > mors->rts_threshold);
}

static bool mm81x_tx_h_ps_filtered_for_sta(struct mm81x *mors,
					   struct sk_buff *skb,
					   struct ieee80211_sta *sta)
{
	struct mm81x_sta *mors_sta;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);

	if (!sta)
		return false;

	mors_sta = (struct mm81x_sta *)sta->drv_priv;

	if (!mors_sta->tx_ps_filter_en)
		return false;

	dev_dbg(mors->dev, "Frame for sta[%pM] PS filtered", mors_sta->addr);

	info->flags |= IEEE80211_TX_STAT_TX_FILTERED;
	info->flags &= ~IEEE80211_TX_CTL_AMPDU;

	ieee80211_tx_status_skb(mors->hw, skb);
	return true;
}

static void mm81x_mac_check_fw_disabled_chans(struct ieee80211_hw *hw)
{
	int ret = 0;
	u32 i;
	struct mm81x *mors = hw->priv;
	struct host_cmd_resp_get_disabled_channels *resp;
	u32 resp_len = sizeof(struct host_cmd_disabled_channel_entry) *
			       ARRAY_SIZE(mors_s1ghz_channels) +
		       sizeof(*resp);

	resp = kzalloc(resp_len, GFP_KERNEL);
	if (!resp) {
		ret = -ENOMEM;
		goto out;
	}

	ret = mm81x_cmd_get_disabled_channels(mors, resp, resp_len);
	if (ret)
		goto out;

	for (i = 0; i < ARRAY_SIZE(mors_s1ghz_channels); i++) {
		struct ieee80211_channel *ch = &mors_s1ghz_channels[i];

		if (ch->flags & IEEE80211_CHAN_DISABLED)
			continue;

		ch->flags &= ~IEEE80211_CHAN_S1G_NO_PRIMARY;
	}

	for (i = 0; i < le32_to_cpu(resp->n_channels); i++) {
		struct ieee80211_channel *ch;
		struct host_cmd_disabled_channel_entry *entry =
			&resp->channels[i];

		if (entry->bw_mhz != 1)
			continue;

		ch = ieee80211_get_channel_khz(
			hw->wiphy,
			KHZ100_TO_KHZ(le16_to_cpu(entry->freq_100khz)));
		if (!ch)
			continue;

		ch->flags |= IEEE80211_CHAN_S1G_NO_PRIMARY;
		dev_dbg(mors->dev, "set NO_PRIMARY on %u KHz",
			ieee80211_channel_to_khz(ch));
	}

out:
	if (ret)
		dev_err(mors->dev, "failed to set disabled primary channels");

	kfree(resp);
}

static int mm81x_mac_ops_start(struct ieee80211_hw *hw)
{
	struct mm81x *mors = hw->priv;

	mors->started = true;
	return 0;
}

static int mm81x_tx_h_get_max_bw(struct mm81x *mors)
{
	return MM81X_FW_SUPP(&mors->fw_caps, 8MHZ) ? 8 :
	       MM81X_FW_SUPP(&mors->fw_caps, 4MHZ) ? 4 :
	       MM81X_FW_SUPP(&mors->fw_caps, 2MHZ) ? 2 :
						     1;
}

static void mm81x_mac_caps_init(struct mm81x *mors)
{
	struct mm81x_fw_caps *fw_caps = &mors->fw_caps;
	struct ieee80211_sta_s1g_cap *s1g = &mors_band_s1ghz.s1g_cap;

#define __FW_CAP_N(_n, _cap, _bit)                \
	do {                                      \
		if (MM81X_FW_SUPP(fw_caps, _cap)) \
			s1g->cap[_n] |= (_bit);   \
	} while (0)

#define FW_CAP0(_cap, _bit) __FW_CAP_N(0, _cap, _bit)
#define FW_CAP3(_cap, _bit) __FW_CAP_N(3, _cap, _bit)
#define FW_CAP5(_cap, _bit) __FW_CAP_N(5, _cap, _bit)
#define FW_CAP6(_cap, _bit) __FW_CAP_N(6, _cap, _bit)
#define FW_CAP7(_cap, _bit) __FW_CAP_N(7, _cap, _bit)
#define FW_CAP8(_cap, _bit) __FW_CAP_N(8, _cap, _bit)
#define FW_CAP9(_cap, _bit) __FW_CAP_N(9, _cap, _bit)

	FW_CAP0(S1G_LONG, S1G_CAP0_S1G_LONG);

	s1g->cap[0] |= S1G_CAP0_SGI_1MHZ;
	if (MM81X_FW_SUPP(fw_caps, SGI)) {
		FW_CAP0(2MHZ, S1G_CAP0_SGI_2MHZ);
		FW_CAP0(4MHZ, S1G_CAP0_SGI_4MHZ);
		FW_CAP0(8MHZ, S1G_CAP0_SGI_8MHZ);
	}

	if (MM81X_FW_SUPP(fw_caps, 8MHZ))
		s1g->cap[0] |= S1G_SUPP_CH_WIDTH_8;
	else if (MM81X_FW_SUPP(fw_caps, 4MHZ))
		s1g->cap[0] |= S1G_SUPP_CH_WIDTH_4;
	else if (MM81X_FW_SUPP(fw_caps, 2MHZ))
		s1g->cap[0] |= S1G_SUPP_CH_WIDTH_2;

	FW_CAP3(RD_RESPONDER, S1G_CAP3_RD_RESPONDER);
	FW_CAP3(LONG_MPDU, S1G_CAP3_MAX_MPDU_LEN);

	FW_CAP5(AMSDU, S1G_CAP5_AMSDU);
	FW_CAP5(AMPDU, S1G_CAP5_AMPDU);
	FW_CAP5(ASYMMETRIC_BA_SUPPORT, S1G_CAP5_ASYMMETRIC_BA);
	FW_CAP5(FLOW_CONTROL, S1G_CAP5_FLOW_CONTROL);

	FW_CAP6(OBSS_MITIGATION, S1G_CAP6_OBSS_MITIGATION);
	FW_CAP6(FRAGMENT_BA, S1G_CAP6_FRAGMENT_BA);
	FW_CAP6(NDP_PSPOLL, S1G_CAP6_NDP_PS_POLL);
	FW_CAP6(TXOP_SHARING_IMPLICIT_ACK, S1G_CAP6_TXOP_SHARING_IMP_ACK);
	FW_CAP6(HTC_VHT_MFB, S1G_CAP6_VHT_LINK_ADAPT);

	FW_CAP7(TACK_AS_PSPOLL, S1G_CAP7_TACK_AS_PS_POLL);
	FW_CAP7(DUPLICATE_1MHZ, S1G_CAP7_DUP_1MHZ);
	FW_CAP7(MCS_NEGOTIATION, S1G_CAP7_MCS_NEGOTIATION);
	FW_CAP7(1MHZ_CONTROL_RESPONSE_PREAMBLE,
		S1G_CAP7_1MHZ_CTL_RESPONSE_PREAMBLE);
	FW_CAP7(SECTOR_TRAINING, S1G_CAP7_SECTOR_TRAINING_OPERATION);
	FW_CAP7(TMP_PS_MODE_SWITCH, S1G_CAP7_TEMP_PS_MODE_SWITCH);

	FW_CAP8(BDT, S1G_CAP8_BDT);

	FW_CAP9(LINK_ADAPTATION_WO_NDP_CMAC,
		S1G_CAP9_LINK_ADAPT_PER_CONTROL_RESPONSE);

	/* 1SS MCS 9 for Rx / Tx map */
	s1g->nss_mcs[0] = MM81X_NSS_MCS_BYTE_0;
	s1g->nss_mcs[1] = MM81X_NSS_MCS_BYTE_1;
	s1g->nss_mcs[2] = MM81X_NSS_MCS_BYTE_2;
	s1g->nss_mcs[3] = MM81X_NSS_MCS_BYTE_3;
	s1g->nss_mcs[4] = MM81X_NSS_MCS_BYTE_4;

#undef FW_CAP0
#undef FW_CAP3
#undef FW_CAP5
#undef FW_CAP6
#undef FW_CAP7
#undef FW_CAP8
#undef FW_CAP9
#undef __FW_CAP_N
}

static void mm81x_mac_beacon_irq_enable(struct mm81x_vif *mors_vif, bool enable)
{
	struct mm81x *mors = mm81x_vif_to_mors(mors_vif);
	u8 beacon_irq_num = MM81X_INT_BEACON_BASE_NUM + mors_vif->id;

	enable ? set_bit(beacon_irq_num, &mors->beacon_irqs_enabled) :
		 clear_bit(beacon_irq_num, &mors->beacon_irqs_enabled);

	mm81x_hw_irq_enable(mors, beacon_irq_num, enable);
}

static void mm81x_beacon_h_fill_tx_info(struct mm81x *mors,
					struct mm81x_skb_tx_info *tx_info,
					struct mm81x_vif *mors_vif,
					int tx_bw_mhz)
{
	enum dot11_bandwidth bw_idx =
		mm81x_ratecode_bw_mhz_to_bw_index(tx_bw_mhz);
	enum mm81x_rate_preamble pream = MM81X_RATE_PREAMBLE_S1G_SHORT;

	tx_info->flags |=
		cpu_to_le32(MM81X_TX_CONF_FLAGS_VIF_ID_SET(mors_vif->id));

	if (bw_idx == DOT11_BANDWIDTH_1MHZ)
		pream = MM81X_RATE_PREAMBLE_S1G_1M;

	tx_info->rates[0].count = 1;
	tx_info->rates[1].count = 0;
	tx_info->rates[0].mm81x_ratecode =
		mm81x_ratecode_init(bw_idx, 0, 0, pream);

	if (mors->fw_flags & MM81X_FW_FLAGS_REPORTS_TX_BEACON_COMPLETION)
		tx_info->flags |=
			cpu_to_le32(MM81X_TX_CONF_FLAGS_IMMEDIATE_REPORT);
}

static void mm81x_mac_beacon_work(struct work_struct *work)
{
	struct mm81x_vif *mors_vif =
		from_work(mors_vif, work, u.ap.beacon_work);
	struct mm81x *mors = mm81x_vif_to_mors(mors_vif);
	struct mm81x_skbq *mq;
	struct sk_buff *beacon;
	struct ieee80211_vif *vif = mm81x_vif_to_ieee80211_vif(mors_vif);
	struct mm81x_skb_tx_info tx_info = { 0 };
	int num_bcn_vifs = atomic_read(&mors->num_bcn_vifs);

	mq = mm81x_hif_get_tx_beacon_queue(mors);
	if (!mq) {
		dev_err(mors->dev, "no matching beacon Q found");
		return;
	}

	if (mm81x_skbq_count(mq) >= num_bcn_vifs) {
		dev_err(mors->dev,
			"previous beacon not consumed, dropping req [id:%d]",
			mors_vif->id);
		return;
	}

	beacon = ieee80211_beacon_get(mors->hw, vif, false);
	if (!beacon)
		return;

	mm81x_beacon_h_fill_tx_info(mors, &tx_info, mors_vif,
				    cfg80211_chandef_s1g_pri_width(&mors->chandef));
	mm81x_skbq_skb_tx(mq, &beacon, &tx_info, MM81X_SKB_CHAN_BEACON);
}

void mm81x_mac_beacon_irq_handle(struct mm81x *mors, u32 status)
{
	int vif_id;
	unsigned long masked_status = (status & mors->beacon_irqs_enabled) >>
				      MM81X_INT_BEACON_BASE_NUM;

	guard(rcu)();
	for_each_set_bit(vif_id, &masked_status, MM81X_MAX_IF) {
		struct mm81x_vif *mors_vif;
		struct ieee80211_vif *vif;

		vif = mm81x_rcu_dereference_vif_id(mors, vif_id, true);
		if (vif) {
			mors_vif = ieee80211_vif_to_mors_vif(vif);
			queue_work(system_bh_wq, &mors_vif->u.ap.beacon_work);
		}
	}
}

static void mm81x_mac_beacon_init(struct mm81x_vif *mors_vif)
{
	struct mm81x *mors = mm81x_vif_to_mors(mors_vif);

	INIT_WORK(&mors_vif->u.ap.beacon_work, mm81x_mac_beacon_work);
	mm81x_mac_beacon_irq_enable(mors_vif, true);
	atomic_inc(&mors->num_bcn_vifs);
}

static struct hw_scan_tlv_hdr mm81x_hw_scan_h_pack_tlv_hdr(u16 tag, u16 len)
{
	struct hw_scan_tlv_hdr hdr = { .tag = cpu_to_le16(tag),
				       .len = cpu_to_le16(len) };
	return hdr;
}

static __le32 mm81x_hw_scan_h_pack_channel(struct ieee80211_channel *chan,
					   u8 pwr_idx)
{
	__le32 packed = 0;
	u32 freq_khz = ieee80211_channel_to_khz(chan);

	packed |= le32_encode_bits(freq_khz, HW_SCAN_CH_LIST_FREQ_KHZ);
	packed |= le32_encode_bits(mm81x_ratecode_bw_mhz_to_bw_index(1),
				   HW_SCAN_CH_LIST_OP_BW);
	packed |= le32_encode_bits(mm81x_ratecode_bw_mhz_to_bw_index(1),
				   HW_SCAN_CH_LIST_PRIM_CH_WIDTH);
	packed |= le32_encode_bits(pwr_idx, HW_SCAN_CH_LIST_PWR_LIST_IDX);

	return packed;
}

static u8 *
mm81x_hw_scan_h_add_channel_list_tlv(u8 *buf,
				     struct mm81x_hw_scan_params *params)
{
	int i;
	struct hw_scan_tlv_channel_list *ch_list =
		(struct hw_scan_tlv_channel_list *)buf;

	ch_list->hdr = mm81x_hw_scan_h_pack_tlv_hdr(
		HOST_CMD_HW_SCAN_TLV_TAG_CHAN_LIST,
		params->num_chans * sizeof(ch_list->channels[0]));

	for (i = 0; i < params->num_chans; i++) {
		struct ieee80211_channel *chan = params->channels[i].channel;

		ch_list->channels[i] = mm81x_hw_scan_h_pack_channel(
			chan, params->channels[i].power_idx);
	}

	return (u8 *)&ch_list->channels[i];
}

static u8 *
mm81x_hw_scan_h_add_power_list_tlv(u8 *buf, struct mm81x_hw_scan_params *params)
{
	int i;
	struct hw_scan_tlv_power_list *pwr_list =
		(struct hw_scan_tlv_power_list *)buf;
	size_t size = sizeof(pwr_list->tx_power_qdbm[0]) * params->n_powers;

	pwr_list->hdr = mm81x_hw_scan_h_pack_tlv_hdr(
		HOST_CMD_HW_SCAN_TLV_TAG_POWER_LIST, size);

	for (i = 0; i < params->n_powers; i++)
		pwr_list->tx_power_qdbm[i] = params->powers_qdbm[i];

	return (u8 *)&pwr_list->tx_power_qdbm[i];
}

static u8 *
mm81x_hw_scan_h_add_probe_req_tlv(u8 *buf, struct mm81x_hw_scan_params *params)
{
	struct sk_buff *skb = params->probe_req;
	struct hw_scan_tlv_probe_req *probe_req =
		(struct hw_scan_tlv_probe_req *)buf;

	probe_req->hdr = mm81x_hw_scan_h_pack_tlv_hdr(
		HOST_CMD_HW_SCAN_TLV_TAG_PROBE_REQ, skb->len);
	memcpy(probe_req->buf, skb->data, skb->len);

	return buf + sizeof(*probe_req) + skb->len;
}

static u8 *
mm81x_hw_scan_h_insert_dwell_time_tlv(u8 *buf,
				      struct mm81x_hw_scan_params *params)
{
	struct hw_scan_tlv_dwell_on_home *dwell =
		(struct hw_scan_tlv_dwell_on_home *)buf;

	dwell->hdr = mm81x_hw_scan_h_pack_tlv_hdr(
		HOST_CMD_HW_SCAN_TLV_TAG_DWELL_ON_HOME,
		sizeof(*dwell) - sizeof(dwell->hdr));
	dwell->home_dwell_time_ms = cpu_to_le32(params->dwell_on_home_ms);

	return buf + sizeof(*dwell);
}

static int __mm81x_hw_scan_h_init_probe_req(struct mm81x_hw_scan_params *params,
					    u8 *ssid, u8 ssid_len,
					    struct ieee80211_scan_ies *ies)
{
	u8 *pos;
	struct sk_buff *probe_req;
	struct ieee80211_tx_info *info;
	u16 ies_len = ies->len[NL80211_BAND_S1GHZ] + ies->common_ie_len;

	probe_req = ieee80211_probereq_get(params->hw, params->vif->addr, ssid,
					   ssid_len, ies_len);
	if (!probe_req)
		return -ENOMEM;

	pos = skb_put(probe_req, ies_len);
	memcpy(pos, ies->common_ies, ies->common_ie_len);
	pos += ies->common_ie_len;
	memcpy(pos, ies->ies[NL80211_BAND_S1GHZ], ies->len[NL80211_BAND_S1GHZ]);

	info = IEEE80211_SKB_CB(probe_req);
	info->control.vif = params->vif;
	params->probe_req = probe_req;

	return 0;
}

static void mm81x_hw_scan_h_init_ssid(struct mm81x *mors,
				      struct cfg80211_ssid *ssids, int n_ssids,
				      u8 **out_ssid, u8 *out_ssid_len)
{
	*out_ssid = NULL;
	*out_ssid_len = 0;

	if (n_ssids > 0) {
		if (n_ssids > 1) {
			dev_warn(
				mors->dev,
				"Multiple SSIDs found when only one supported. Using the first only.");
		}
		*out_ssid_len = ssids[0].ssid_len;
		*out_ssid = ssids[0].ssid;
	}
}

static int
mm81x_hw_scan_h_init_probe_req(struct mm81x_hw_scan_params *params,
			       struct ieee80211_scan_request *scan_req)
{
	struct mm81x *mors = params->hw->priv;
	struct cfg80211_scan_request *req = &scan_req->req;
	struct ieee80211_scan_ies *ies = &scan_req->ies;
	u8 ssid_len = 0;
	u8 *ssid = NULL;

	mm81x_hw_scan_h_init_ssid(mors, req->ssids, req->n_ssids, &ssid,
				  &ssid_len);

	return __mm81x_hw_scan_h_init_probe_req(params, ssid, ssid_len, ies);
}

static bool
mm81x_hw_scan_h_is_chan_present(const struct mm81x_hw_scan_params *params,
				const struct ieee80211_channel *chan)
{
	int channel;

	for (channel = 0; channel < params->num_chans; channel++) {
		if (params->channels[channel].channel == chan)
			return true;
	}

	return false;
}

static int mm81x_hw_scan_h_insert_chan(struct mm81x_hw_scan_params *params,
				       struct ieee80211_channel *chan)
{
	if (!params->channels)
		return -EFAULT;

	if (!chan)
		return -EFAULT;

	if (params->num_chans >= params->allocated_chans)
		return -ENOMEM;

	if (mm81x_hw_scan_h_is_chan_present(params, chan))
		return 0;

	params->channels[params->num_chans].channel = chan;
	params->num_chans++;
	return 0;
}

static int mm81x_hw_scan_h_init_chan_list(struct mm81x_hw_scan_params *params,
					  struct ieee80211_channel **chans,
					  u32 n_channels)
{
	int i, j;
	int num_pwrs_coarse = 0;
	int last_pwr = INT_MIN;
	int chans_to_allocate = 0;

	for (i = 0; i < n_channels; i++)
		if (chans[i])
			chans_to_allocate++;

	params->num_chans = 0;
	params->allocated_chans = 0;
	params->channels = kcalloc(chans_to_allocate, sizeof(*params->channels),
				   GFP_KERNEL);
	if (!params->channels)
		return -ENOMEM;

	params->allocated_chans = chans_to_allocate;

	for (i = 0; i < n_channels; i++)
		if (chans[i])
			mm81x_hw_scan_h_insert_chan(params, chans[i]);

	/*
	 * Calculate a rough estimate of number of different channel
	 * powers required
	 */
	for (i = 0; i < params->num_chans; i++) {
		if (chans[i]->max_reg_power != last_pwr) {
			last_pwr = chans[i]->max_reg_power;
			num_pwrs_coarse++;
		}
	}

	params->powers_qdbm = kmalloc_array(
		num_pwrs_coarse, sizeof(*params->powers_qdbm), GFP_KERNEL);
	if (!params->powers_qdbm)
		return -ENOMEM;

	params->n_powers = 0;

	for (i = 0; i < params->num_chans; i++) {
		s32 power_qdbm =
			MBM_TO_QDBM(DBM_TO_MBM(chans[i]->max_reg_power));

		/* Try and find the power in the list */
		for (j = 0; j < params->n_powers; j++)
			if (params->powers_qdbm[j] == power_qdbm)
				break;

		/* Reached the end of the list - add the new power option */
		if (j == params->n_powers) {
			params->powers_qdbm[j] = power_qdbm;
			params->n_powers++;
			if (params->n_powers > num_pwrs_coarse) {
				WARN_ON(1);
				return -EFAULT;
			}
		}

		/* Give the index of the power level to the channel */
		params->channels[i].power_idx = j;
	}
	return 0;
}

static void mm81x_hw_scan_h_clean_params(struct mm81x_hw_scan_params *params)
{
	if (params->probe_req)
		dev_kfree_skb_any(params->probe_req);
	kfree(params->channels);
	kfree(params->powers_qdbm);

	params->num_chans = 0;
	params->allocated_chans = 0;
}

size_t mm81x_hw_scan_h_get_cmd_size(struct mm81x_hw_scan_params *params)
{
	struct hw_scan_tlv_channel_list *ch_list;
	struct hw_scan_tlv_power_list *pwr_list;
	struct hw_scan_tlv_probe_req *probe_req;
	struct hw_scan_tlv_dwell_on_home *dwell;
	struct host_cmd_req_hw_scan *req;
	size_t cmd_size = sizeof(*req);

	/* No TLVs if simple abort command */
	if (params->operation != MM81X_HW_SCAN_OP_START)
		return cmd_size;

	cmd_size += struct_size(ch_list, channels, params->num_chans);
	cmd_size += struct_size(pwr_list, tx_power_qdbm, params->n_powers);

	if (params->probe_req)
		cmd_size += struct_size(probe_req, buf, params->probe_req->len);
	if (params->dwell_on_home_ms)
		cmd_size += sizeof(*dwell);

	return cmd_size;
}

u8 *mm81x_hw_scan_h_insert_tlvs(struct mm81x_hw_scan_params *params, u8 *buf)
{
	buf = mm81x_hw_scan_h_add_channel_list_tlv(buf, params);
	buf = mm81x_hw_scan_h_add_power_list_tlv(buf, params);

	if (params->dwell_on_home_ms)
		buf = mm81x_hw_scan_h_insert_dwell_time_tlv(buf, params);
	if (params->probe_req)
		buf = mm81x_hw_scan_h_add_probe_req_tlv(buf, params);

	return buf;
}

static u32 mm81x_hw_scan_h_get_dwell_on_home(struct mm81x *mors,
					     struct ieee80211_vif *vif)
{
	if (vif->type == NL80211_IFTYPE_STATION && vif->cfg.assoc)
		return mors->hw_scan.home_dwell_ms;
	return 0;
}

static struct mm81x_hw_scan_params *
__mm81x_hw_scan_h_init_params(struct mm81x *mors)
{
	struct mm81x_hw_scan_params *params = mors->hw_scan.params;

	if (!params) {
		params = kzalloc_obj(*params, GFP_KERNEL);
		if (params)
			mors->hw_scan.params = params;
	} else {
		mm81x_hw_scan_h_clean_params(params);
		memset(params, 0, sizeof(*params));
	}

	return params;
}

static int mm81x_hw_scan_h_init_params(struct mm81x *mors,
				       struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif,
				       struct cfg80211_scan_request *req)
{
	struct mm81x_hw_scan_params *params = mors->hw_scan.params;

	params = __mm81x_hw_scan_h_init_params(mors);
	if (!params) {
		mors->hw_scan.state = HW_SCAN_STATE_IDLE;
		return -ENOMEM;
	}

	params->hw = hw;
	params->vif = vif;
	params->has_directed_ssid = (req->ssids && req->ssids[0].ssid_len > 0);
	params->operation = MM81X_HW_SCAN_OP_START;
	params->dwell_on_home_ms = mm81x_hw_scan_h_get_dwell_on_home(mors, vif);

	if (req->duration)
		params->dwell_time_ms = MM81X_TU_TO_MS(req->duration);
	else if (req->n_ssids == 0)
		params->dwell_time_ms =
			MM81X_HWSCAN_DEFAULT_PASSIVE_DWELL_TIME_MS;
	else
		params->dwell_time_ms = MM81X_HWSCAN_DEFAULT_DWELL_TIME_MS;

	return 0;
}

static u32 mm81x_hw_scan_h_calc_timeout(struct mm81x_hw_scan_params *params)
{
	u32 ret = 0;

	ret = params->dwell_time_ms + params->dwell_on_home_ms;
	if (params->probe_req)
		ret += MM81X_HWSCAN_PROBE_DELAY_MS;

	ret *= params->num_chans;
	ret += MM81X_HWSCAN_TIMEOUT_OVERHEAD_MS;

	return ret;
}

static int mm81x_mac_ops_hw_scan(struct ieee80211_hw *hw,
				 struct ieee80211_vif *vif,
				 struct ieee80211_scan_request *hw_req)
{
	int ret = 0;
	struct mm81x *mors = hw->priv;
	struct cfg80211_scan_request *req = &hw_req->req;
	struct mm81x_hw_scan_params *params;
	struct ieee80211_channel **chans = hw_req->req.channels;

	dev_dbg(mors->dev, "state %d", mors->hw_scan.state);

	if (!mors->started) {
		dev_warn(mors->dev, "device not ready");
		ret = -ENODEV;
		goto exit;
	}

	switch (mors->hw_scan.state) {
	case HW_SCAN_STATE_IDLE:
		mors->hw_scan.state = HW_SCAN_STATE_RUNNING;
		reinit_completion(&mors->hw_scan.scan_done);
		break;
	case HW_SCAN_STATE_RUNNING:
	case HW_SCAN_STATE_ABORTING:
		ret = -EBUSY;
		goto exit;
	}

	ret = mm81x_hw_scan_h_init_params(mors, hw, vif, req);
	if (ret)
		goto exit;

	params = mors->hw_scan.params;

	ret = mm81x_hw_scan_h_init_chan_list(params, chans,
					     hw_req->req.n_channels);
	if (ret)
		goto exit;

	/* Only init the probe request template if this is an active scan */
	if (req->n_ssids > 0) {
		ret = mm81x_hw_scan_h_init_probe_req(params, hw_req);
		if (ret) {
			dev_err(mors->dev, "Failed to init probe req %d", ret);
			goto exit;
		}
	}

	ret = mm81x_cmd_hw_scan(mors, params, false);
	if (ret) {
		mors->hw_scan.state = HW_SCAN_STATE_IDLE;
		goto exit;
	}

	ieee80211_queue_delayed_work(
		mors->hw, &mors->hw_scan.timeout,
		msecs_to_jiffies(mm81x_hw_scan_h_calc_timeout(params)));
exit:
	return ret;
}

static void mm81x_hw_scan_abort(struct mm81x *mors)
{
	int ret;
	struct mm81x_hw_scan_params params = { 0 };

	switch (mors->hw_scan.state) {
	case HW_SCAN_STATE_IDLE:
	case HW_SCAN_STATE_ABORTING:
		/* scan not running */
		return;
	case HW_SCAN_STATE_RUNNING:
		mors->hw_scan.state = HW_SCAN_STATE_ABORTING;
		break;
	}

	params.operation = MM81X_HW_SCAN_OP_STOP;

	ret = mm81x_cmd_hw_scan(mors, &params, false);

	if (ret || !mors->started ||
	    !wait_for_completion_timeout(&mors->hw_scan.scan_done, 1 * HZ)) {
		/*
		 * We may have lost the event on the bus, the chip could be
		 * wedged, or the cmd failed for another reason. Nevertheless,
		 * we should call the done event so mac80211 knows to unblock
		 * itself.
		 */
		struct cfg80211_scan_info info = { .aborted = true };

		ieee80211_scan_completed(mors->hw, &info);
		mors->hw_scan.state = HW_SCAN_STATE_IDLE;
	}
}

static void mm81x_mac_ops_cancel_hw_scan(struct ieee80211_hw *hw,
					 struct ieee80211_vif *vif)
{
	struct mm81x *mors = hw->priv;

	cancel_delayed_work_sync(&mors->hw_scan.timeout);
	mm81x_hw_scan_abort(mors);
}

static void mm81x_mac_hw_scan_done_event(struct ieee80211_hw *hw)
{
	struct mm81x *mors = hw->priv;
	struct cfg80211_scan_info info = { 0 };

	dev_dbg(mors->dev, "completing hw scan");

	switch (mors->hw_scan.state) {
	case HW_SCAN_STATE_IDLE:
		/* Scan has already been stopped. Just continue */
		goto exit;
	case HW_SCAN_STATE_RUNNING:
	case HW_SCAN_STATE_ABORTING:
		info.aborted = (mors->hw_scan.state == HW_SCAN_STATE_ABORTING);
		mors->hw_scan.state = HW_SCAN_STATE_IDLE;
	}

	ieee80211_scan_completed(mors->hw, &info);
exit:
	complete(&mors->hw_scan.scan_done);
	cancel_delayed_work_sync(&mors->hw_scan.timeout);
}

static void mm81x_mac_hw_scan_timeout_work(struct work_struct *work)
{
	struct mm81x *mors =
		container_of(work, struct mm81x, hw_scan.timeout.work);

	dev_err(mors->dev, "hw scan timed out, aborting");
	mm81x_hw_scan_abort(mors);
}

static void mm81x_mac_hw_scan_init(struct mm81x *mors)
{
	mors->hw_scan.state = HW_SCAN_STATE_IDLE;
	mors->hw_scan.params = NULL;
	mors->hw_scan.home_dwell_ms = MM81X_HWSCAN_DEFAULT_DWELL_ON_HOME_MS;

	init_completion(&mors->hw_scan.scan_done);
	INIT_DELAYED_WORK(&mors->hw_scan.timeout,
			  mm81x_mac_hw_scan_timeout_work);
}

static void mm81x_mac_hw_scan_destroy(struct mm81x *mors)
{
	cancel_delayed_work_sync(&mors->hw_scan.timeout);
	if (mors->hw_scan.params)
		mm81x_hw_scan_h_clean_params(mors->hw_scan.params);
	kfree(mors->hw_scan.params);
	mors->hw_scan.params = NULL;
}

static void mm81x_mac_hw_scan_finish(struct mm81x *mors)
{
	struct cfg80211_scan_info info = {
		.aborted = true,
	};

	if (mors->hw_scan.state == HW_SCAN_STATE_IDLE)
		return;

	ieee80211_scan_completed(mors->hw, &info);
	complete(&mors->hw_scan.scan_done);
	mors->hw_scan.state = HW_SCAN_STATE_IDLE;
	cancel_delayed_work_sync(&mors->hw_scan.timeout);
}

int mm81x_mac_event_recv(struct mm81x *mors, struct sk_buff *skb)
{
	struct host_cmd_event *event = (struct host_cmd_event *)(skb->data);
	u16 event_id = le16_to_cpu(event->hdr.message_id);
	u16 event_iid = le16_to_cpu(event->hdr.host_id);
	u16 vif_id = le16_to_cpu(event->hdr.vif_id);
	struct ieee80211_vif *vif;

	if (!HOST_CMD_IS_EVT(event) || event_iid != 0)
		return -EINVAL;

	switch (event_id) {
	case HOST_CMD_ID_EVT_HW_SCAN_DONE:
		dev_dbg(mors->dev,
			"Event: HOST_CMD_ID_EVT_HW_SCAN_DONE Received.");
		mm81x_mac_hw_scan_done_event(mors->hw);
		break;
	case HOST_CMD_ID_EVT_BEACON_LOSS:
		dev_dbg(mors->dev,
			"Event: HOST_CMD_ID_EVT_BEACON_LOSS Received");
		scoped_guard(rcu) {
			vif = mm81x_rcu_dereference_vif_id(mors, vif_id, true);
			if (vif)
				ieee80211_beacon_loss(vif);
		}
		break;
	default:
		break;
	}

	return 0;
}

static void mm81x_tx_h_apply_mcs10(struct mm81x *mors,
				   struct mm81x_skb_tx_info *tx_info)
{
	u8 i;
	u8 j;
	int mcs0_first_idx = -1;
	int mcs0_last_idx = -1;

	/* Find out where our first and last MCS0 entries are. */
	for (i = 0; i < IEEE80211_TX_MAX_RATES; i++) {
		enum dot11_bandwidth bw_idx = mm81x_ratecode_bw_index_get(
			tx_info->rates[i].mm81x_ratecode);

		if (bw_idx == DOT11_BANDWIDTH_1MHZ) {
			mcs0_last_idx = i;
			if (mcs0_first_idx == -1)
				mcs0_first_idx = i;
		}

		/*
		 * If the count is 0 then we are at the end of the table.
		 * Break to allow us to reuse i indicating the end of the
		 * table.
		 */
		if (tx_info->rates[i].count == 0)
			break;
	}

	/* If there aren't any MCS0 (at 1MHz) entries we are done. */
	if (mcs0_first_idx < 0)
		return;

	/*
	 * If we are in MCS10_MODE_AUTO add MCS10 counts to the table if they
	 * will fit. There should be three cases:
	 *
	 * - There is one MSC0 entry and the table is full -> do nothing
	 * - There is one MSC0 entry and the table has space -> adjust MSC0
	 *   down and add MCS 10
	 * - There are multiple MCS0 entries -> replace entries after the first
	 *   with MCS 10
	 */
	/* Case 3 - replace additional entries. */
	if (mcs0_last_idx > mcs0_first_idx) {
		for (j = mcs0_first_idx + 1; j < i; j++) {
			enum dot11_bandwidth bw_idx =
				mm81x_ratecode_bw_index_get(
					tx_info->rates[j].mm81x_ratecode);
			u8 mcs_index = mm81x_ratecode_mcs_index_get(
				tx_info->rates[j].mm81x_ratecode);
			if (mcs_index == 0 && bw_idx == DOT11_BANDWIDTH_1MHZ) {
				mm81x_ratecode_mcs_index_set(
					&tx_info->rates[j].mm81x_ratecode, 10);
			}
		}
		/* Case 2 - add additional MCS10 entry. */
	} else if (mcs0_last_idx == mcs0_first_idx &&
		   i < (IEEE80211_TX_MAX_RATES)) {
		int pre_mcs10_mcs0_count =
			min_t(u8, tx_info->rates[mcs0_last_idx].count,
			      MCS0_BEFORE_MCS10_COUNT);
		int mcs10_count = tx_info->rates[mcs0_last_idx].count -
				  pre_mcs10_mcs0_count;

		/*
		 * If there were less retries than our desired minimum MCS0 we
		 * don't add MCS10 retries.
		 */
		if (mcs10_count > 0) {
			/* Use the same flags for MCS10 as MCS0. */
			tx_info->rates[i].mm81x_ratecode =
				tx_info->rates[mcs0_last_idx].mm81x_ratecode;
			mm81x_ratecode_mcs_index_set(
				&tx_info->rates[i].mm81x_ratecode, 10);
			tx_info->rates[mcs0_last_idx].count =
				pre_mcs10_mcs0_count;
			tx_info->rates[i].count = mcs10_count;
		}
	}
}

void mm81x_tx_h_check_aggr(struct ieee80211_sta *pubsta, struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct mm81x_sta *mors_sta = (struct mm81x_sta *)pubsta->drv_priv;
	u8 tid = ieee80211_get_tid(hdr);

	/* we are already aggregating */
	if (mors_sta->tid_tx[tid] || mors_sta->tid_start_tx[tid])
		return;

	if (mors_sta->state < IEEE80211_STA_AUTHORIZED)
		return;

	if (skb_get_queue_mapping(skb) == IEEE80211_AC_VO)
		return;

	if (unlikely(!ieee80211_is_data_qos(hdr->frame_control)))
		return;

	if (unlikely(skb->protocol == cpu_to_be16(ETH_P_PAE)))
		return;

	mors_sta->tid_start_tx[tid] = true;
	ieee80211_start_tx_ba_session(pubsta, tid, 0);
}

int mm81x_tx_h_get_attempts(struct mm81x *mors,
			    struct mm81x_skb_tx_status *tx_sts)
{
	int attempts = 0;
	int i;
	int count = min_t(int, MM81X_SKB_MAX_RATES, IEEE80211_TX_MAX_RATES);

	for (i = 0; i < count; i++) {
		if (tx_sts->rates[i].count > 0)
			attempts += tx_sts->rates[i].count;
		else
			break;
	}

	return attempts;
}

static void mm81x_tx_h_fill_info(struct mm81x *mors,
				 struct mm81x_skb_tx_info *tx_info,
				 struct sk_buff *skb, struct ieee80211_vif *vif,
				 int tx_bw_mhz, struct ieee80211_sta *sta)
{
	int i;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct mm81x_vif *mors_vif = ieee80211_vif_to_mors_vif(vif);
	struct mm81x_sta *mors_sta = NULL;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	int op_bw_mhz = cfg80211_chandef_get_width(&mors->chandef);
	u8 tid = skb->priority & IEEE80211_QOS_CTL_TAG1D_MASK;
	bool rts_allowed = op_bw_mhz < 8;

	if (sta)
		mors_sta = (struct mm81x_sta *)sta->drv_priv;

	rts_allowed &= mm81x_tx_h_pkt_over_rts_threshold(mors, info, skb);

	mm81x_rc_sta_fill_tx_rates(mors, tx_info, skb, sta, tx_bw_mhz,
				   rts_allowed);

	for (i = 0; i < IEEE80211_TX_MAX_RATES; i++) {
		if (rts_allowed)
			mm81x_ratecode_enable_rts(
				&tx_info->rates[i].mm81x_ratecode);

		if (info->control.rates[i].flags & IEEE80211_TX_RC_SHORT_GI)
			mm81x_ratecode_enable_sgi(
				&tx_info->rates[i].mm81x_ratecode);
	}

	/* Apply change of MCS0 to MCS10 if required. */
	mm81x_tx_h_apply_mcs10(mors, tx_info);

	tx_info->flags |=
		cpu_to_le32(MM81X_TX_CONF_FLAGS_VIF_ID_SET(mors_vif->id));

	if (info->flags & IEEE80211_TX_CTL_AMPDU)
		tx_info->flags |= cpu_to_le32(MM81X_TX_CONF_FLAGS_CTL_AMPDU);

	if (info->flags & IEEE80211_TX_CTL_SEND_AFTER_DTIM)
		tx_info->flags |=
			cpu_to_le32(MM81X_TX_CONF_FLAGS_SEND_AFTER_DTIM);

	if (info->flags & IEEE80211_TX_CTL_NO_PS_BUFFER) {
		tx_info->flags |= cpu_to_le32(MM81X_TX_CONF_NO_PS_BUFFER);

		if (info->flags & IEEE80211_TX_STATUS_EOSP)
			tx_info->flags |= cpu_to_le32(
				MM81X_TX_CONF_FLAGS_IMMEDIATE_REPORT);
	} else if (ieee80211_is_mgmt(hdr->frame_control) &&
		   !ieee80211_is_bufferable_mmpdu(skb)) {
		tx_info->flags |= cpu_to_le32(MM81X_TX_CONF_NO_PS_BUFFER);
	}

	if (info->control.hw_key) {
		tx_info->flags |= cpu_to_le32(MM81X_TX_CONF_FLAGS_HW_ENCRYPT);
		tx_info->flags |= cpu_to_le32(MM81X_TX_CONF_FLAGS_KEY_IDX_SET(
			info->control.hw_key->hw_key_idx));
	}

	tx_info->tid = tid;
	if (mors_sta) {
		tx_info->tid_params = mors_sta->tid_params[tid];

		if (info->flags & IEEE80211_TX_CTL_CLEAR_PS_FILT) {
			if (mors_sta->tx_ps_filter_en)
				dev_dbg(mors->dev,
					"TX ps filter cleared sta[%pM]",
					mors_sta->addr);
			mors_sta->tx_ps_filter_en = false;
		}
	}
}

static void mm81x_mac_ops_tx(struct ieee80211_hw *hw,
			     struct ieee80211_tx_control *control,
			     struct sk_buff *skb)
{
	struct mm81x *mors = hw->priv;
	struct mm81x_skbq *mq = NULL;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_vif *vif = info->control.vif;
	struct mm81x_skb_tx_info tx_info = { 0 };
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	bool is_mgmt = ieee80211_is_mgmt(hdr->frame_control);
	int tx_bw_mhz = cfg80211_chandef_get_width(&mors->chandef);
	struct ieee80211_sta *sta = control->sta;
	int max_tx_bw = 0, sta_max_bw_mhz = 0;

	if (sta) {
		struct mm81x_sta *mors_sta = (struct mm81x_sta *)sta->drv_priv;

		sta_max_bw_mhz = mors_sta->max_bw_mhz;
	}

	max_tx_bw = mm81x_tx_h_get_max_bw(mors);
	tx_bw_mhz = min(max_tx_bw, tx_bw_mhz);

	if (is_mgmt)
		tx_bw_mhz = cfg80211_chandef_s1g_pri_width(&mors->chandef);
	if (sta_max_bw_mhz)
		tx_bw_mhz = min(tx_bw_mhz, sta_max_bw_mhz);
	if (ieee80211_is_probe_resp(hdr->frame_control))
		tx_bw_mhz = 1;

	mm81x_tx_h_fill_info(mors, &tx_info, skb, vif, tx_bw_mhz, sta);

	if (mm81x_tx_h_ps_filtered_for_sta(mors, skb, sta))
		return;

	if (is_mgmt)
		mq = mm81x_hif_get_tx_mgmt_queue(mors);
	else
		mq = mm81x_hif_get_tx_data_queue(mors,
						 dot11_tid_to_ac(tx_info.tid));

	mm81x_skbq_skb_tx(mq, &skb, &tx_info,
			  (is_mgmt) ? MM81X_SKB_CHAN_MGMT :
				      MM81X_SKB_CHAN_DATA);
}

static void mm81x_mac_ops_stop(struct ieee80211_hw *hw, bool suspend)
{
	struct mm81x *mors = hw->priv;

	mors->started = false;
}

static void mm81x_mac_beacon_finish(struct mm81x_vif *mors_vif)
{
	struct mm81x *mors = mm81x_vif_to_mors(mors_vif);

	mm81x_mac_beacon_irq_enable(mors_vif, false);
	cancel_work_sync(&mors_vif->u.ap.beacon_work);
	/*
	 * Side effect of the restarting required when
	 * reacting to regdom changes...
	 */
	atomic_add_unless(&mors->num_bcn_vifs, -1, 0);
}

static void mm81x_mac_ops_remove_interface(struct ieee80211_hw *hw,
					   struct ieee80211_vif *vif)
{
	int ret;
	struct mm81x *mors = hw->priv;
	struct mm81x_vif *mors_vif = (struct mm81x_vif *)vif->drv_priv;

	ret = mm81x_cmd_rm_if(mors, mors_vif->id);
	if (ret)
		dev_err(mors->dev, "mm81x_cmd_rm_if failed %d", ret);

	RCU_INIT_POINTER(mors->vifs[mors_vif->id], NULL);
}

static s32 mm81x_mac_get_max_txpower(struct mm81x *mors)
{
	int ret;
	s32 power_mbm;

	/* Retrieve maximum TX power the chip can transmit */
	ret = mm81x_cmd_get_max_txpower(mors, &power_mbm);
	if (ret) {
		dev_err(mors->dev, "using default tx max power %d mBm",
			MAX_TX_POWER_MBM);
		return MAX_TX_POWER_MBM;
	}

	dev_dbg(mors->dev, "Max tx power detected %d mBm", power_mbm);
	return power_mbm;
}

static s32 mm81x_mac_set_txpower(struct mm81x *mors, s32 power_mbm)
{
	int ret;
	s32 out_power_mbm;

	if (mors->tx_max_power_mbm == INT_MAX)
		mors->tx_max_power_mbm = mm81x_mac_get_max_txpower(mors);

	power_mbm = min(power_mbm, mors->tx_max_power_mbm);
	if (power_mbm == mors->tx_power_mbm)
		return mors->tx_power_mbm;

	ret = mm81x_cmd_set_txpower(mors, &out_power_mbm, power_mbm);
	if (ret) {
		dev_err(mors->dev, "failed, power %d mBm ret %d", power_mbm,
			ret);
		return mors->tx_power_mbm;
	}

	if (out_power_mbm != mors->tx_power_mbm) {
		dev_dbg(mors->dev, "%d -> %d mBm", mors->tx_power_mbm,
			out_power_mbm);
		mors->tx_power_mbm = out_power_mbm;
	}

	return mors->tx_power_mbm;
}

static int mm81x_mac_set_channel(struct mm81x *mors, u32 op_chan_freq_hz,
				 u8 pri_1mhz_chan_idx, u8 op_bw_mhz,
				 u8 pri_bw_mhz)
{
	int ret;

	ret = mm81x_cmd_set_channel(mors, op_chan_freq_hz, pri_1mhz_chan_idx,
				    op_bw_mhz, pri_bw_mhz, &mors->tx_power_mbm);
	if (ret) {
		dev_err(mors->dev, "mm81x_cmd_set_channel() failed, ret %d",
			ret);
		return ret;
	}

	mm81x_mac_set_txpower(mors, mors->tx_power_mbm);
	return 0;
}

static u8 mm81x_mac_pri_chan_to_index(const struct cfg80211_chan_def *chandef)
{
	u32 bw_mhz = cfg80211_chandef_get_width(chandef);
	u32 op_center_khz = ieee80211_chandef_to_khz(chandef);
	u32 first_1mhz_center_khz = op_center_khz - (bw_mhz * 500) + 500;
	u32 pri_1mhz_khz = ieee80211_channel_to_khz(chandef->chan);

	return (pri_1mhz_khz - first_1mhz_center_khz) / 1000;
}

static int mm81x_mac_ops_change_channel(struct ieee80211_hw *hw,
					struct cfg80211_chan_def *chandef)
{
	int ret;
	struct mm81x *mors = hw->priv;
	u64 freq_hz = KHZ_TO_HZ(ieee80211_chandef_to_khz(chandef));
	u8 op_bw_mhz = cfg80211_chandef_get_width(chandef);
	u8 pri_1mhz_idx = mm81x_mac_pri_chan_to_index(chandef);
	int pri_chan_width_mhz = cfg80211_chandef_s1g_pri_width(chandef);

	dev_dbg(mors->dev, "ch: freq=%llu Hz bw=%u pri_idx=%d pri_bw=%d",
		freq_hz, op_bw_mhz, pri_1mhz_idx, pri_chan_width_mhz);

	ret = mm81x_mac_set_channel(mors, freq_hz, (u8)pri_1mhz_idx, op_bw_mhz,
				    pri_chan_width_mhz);
	if (ret)
		return ret;

	memcpy(&mors->chandef, chandef, sizeof(mors->chandef));
	return 0;
}

static int mm81x_mac_ops_config(struct ieee80211_hw *hw, int radio_idx,
				u32 changed)
{
	int ret;
	struct mm81x *mors = hw->priv;
	struct ieee80211_conf *conf = &hw->conf;
	struct ieee80211_channel *channel = conf->chandef.chan;

	if (!mors->started)
		return 0;

	if (changed & IEEE80211_CONF_CHANGE_CHANNEL) {
		ret = mm81x_mac_ops_change_channel(hw, &conf->chandef);
		if (ret < 0)
			return ret;
	}

	if ((changed & IEEE80211_CONF_CHANGE_POWER) &&
	    !(changed & IEEE80211_CONF_CHANGE_CHANNEL) &&
	    !(conf->flags & IEEE80211_CONF_MONITOR)) {
		s32 power_mbm = DBM_TO_MBM(conf->power_level);

		power_mbm = min(DBM_TO_MBM(channel->max_reg_power), power_mbm);
		power_mbm = mm81x_mac_set_txpower(mors, power_mbm);
		conf->power_level = MBM_TO_DBM(power_mbm);
	}

	return 0;
}

static int mm81x_mac_ops_get_txpower(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif,
				     unsigned int link_id, int *dbm)
{
	struct mm81x *mors = hw->priv;
	struct ieee80211_chanctx_conf *chanctx_conf;
	struct cfg80211_chan_def *chandef = &vif->bss_conf.chanreq.oper;

	scoped_guard(rcu) {
		chanctx_conf = rcu_access_pointer(vif->bss_conf.chanctx_conf);
		if (!chanctx_conf ||
		    !cfg80211_chandef_identical(chandef, &chanctx_conf->def))
			return -ENODATA;
	}

	*dbm = MBM_TO_DBM(mors->tx_power_mbm);
	return 0;
}

static void mm81x_mac_config_ps(struct mm81x *mors, struct ieee80211_vif *vif)
{
	bool en_ps = vif->cfg.ps;

	if (vif->type == NL80211_IFTYPE_AP || !mors->ps.enable)
		return;

	if (mors->config_ps == en_ps)
		return;

	dev_dbg(mors->dev, "change powersave mode: %d (current %d)", en_ps,
		mors->config_ps);

	mors->config_ps = en_ps;

	if (en_ps) {
		mm81x_cmd_set_ps(mors, true);
		mm81x_ps_enable(mors);
	} else {
		mm81x_ps_disable(mors);
		mm81x_cmd_set_ps(mors, false);
	}
}

static void mm81x_mac_ops_bss_info_changed(struct ieee80211_hw *hw,
					   struct ieee80211_vif *vif,
					   struct ieee80211_bss_conf *info,
					   u64 changed)
{
	int ret;
	struct mm81x *mors = hw->priv;
	struct mm81x_vif *mors_vif = (struct mm81x_vif *)vif->drv_priv;

	if (changed & BSS_CHANGED_PS)
		mm81x_mac_config_ps(mors, vif);

	if (changed & BSS_CHANGED_BEACON_ENABLED) {
		mm81x_cmd_config_beacon_timer(mors, mors_vif,
					      info->enable_beacon);

		if (!info->enable_beacon)
			mm81x_mac_beacon_finish(mors_vif);
	}

	if (changed & BSS_CHANGED_BEACON_INT || changed & BSS_CHANGED_SSID) {
		ret = mm81x_cmd_cfg_bss(mors, mors_vif->id, info->beacon_int,
					info->dtim_period,
					mm81x_vif_generate_cssid(vif));
		if (ret)
			dev_err(mors->dev, "mm81x_cmd_cfg_bss failed %d", ret);
	}
}

static u64 mm81x_mac_ops_prepare_multicast(struct ieee80211_hw *hw,
					   struct netdev_hw_addr_list *mc_list)
{
	struct mm81x *mors = hw->priv;
	struct mcast_filter *filter;
	struct netdev_hw_addr *addr;
	u16 addr_count = netdev_hw_addr_list_count(mc_list);
	u16 len = sizeof(*filter) + addr_count * sizeof(filter->addr_list[0]);

	filter = kzalloc(len, GFP_ATOMIC);
	if (!filter)
		return 0;

	if (addr_count > MCAST_FILTER_COUNT_MAX) {
		dev_warn(
			mors->dev,
			"Multicast filtering disabled - too many groups (%d) > %u",
			addr_count, (u16)MCAST_FILTER_COUNT_MAX);
		filter->count = 0;
	} else {
		netdev_hw_addr_list_for_each(addr, mc_list) {
			dev_dbg(mors->dev, "mcast whitelist (%d): %pM",
				filter->count, addr->addr);
			filter->addr_list[filter->count++] =
				mac2le32(addr->addr);
		}
	}

	return (u64)(unsigned long)filter;
}

static void mm81x_mac_ops_configure_filter(struct ieee80211_hw *hw,
					   unsigned int changed_flags,
					   unsigned int *total_flags,
					   u64 multicast)
{
	struct mm81x *mors = hw->priv;
	struct mcast_filter *cmd = (void *)(unsigned long)multicast;
	struct mm81x_vif *mors_vif = NULL;
	struct ieee80211_vif *vif = NULL;
	int vif_id = 0;
	int ret = 0;

	if (!cmd)
		goto out;

	kfree(mors->mcast_filter);
	mors->mcast_filter = cmd;

	for (vif_id = 0; vif_id < ARRAY_SIZE(mors->vifs); vif_id++) {
		vif = mm81x_rcu_dereference_vif_id(mors, vif_id, false);
		if (!vif)
			continue;

		mors_vif = ieee80211_vif_to_mors_vif(vif);

		ret = mm81x_cmd_cfg_multicast_filter(mors, mors_vif);
		if (!ret)
			continue;

		dev_err(mors->dev, "Multicast filtering failed - rc=%d", ret);
		mors->mcast_filter = NULL;
		kfree(cmd);
		break;
	}

out:
	*total_flags &= 0;
}

static int mm81x_mac_ops_conf_tx(struct ieee80211_hw *hw,
				 struct ieee80211_vif *vif,
				 unsigned int link_id, u16 ac,
				 const struct ieee80211_tx_queue_params *params)
{
	int ret;
	struct mm81x *mors = hw->priv;
	struct mm81x_queue_params mqp;

	mqp.aci = map_mac80211q_2_mm81x_aci(ac);
	mqp.aifs = params->aifs;
	mqp.cw_max = params->cw_max;
	mqp.cw_min = params->cw_min;
	mqp.uapsd = params->uapsd;
	mqp.txop = params->txop << 5;

	dev_dbg(mors->dev, "queue:%d txop:%d cw_min:%d cw_max:%d aifs:%d",
		mqp.aci, mqp.txop, mqp.cw_min, mqp.cw_max, mqp.aifs);

	ret = mm81x_cmd_cfg_qos(mors, &mqp);
	if (ret)
		dev_dbg(mors->dev, "mm81x_cmd_cfg_qos failed %d", ret);
	return ret;
}

static int mm81x_mac_ops_sta_state(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif,
				   struct ieee80211_sta *sta,
				   enum ieee80211_sta_state old_state,
				   enum ieee80211_sta_state new_state)
{
	u16 aid;
	int ret;
	struct mm81x *mors = hw->priv;
	struct mm81x_vif *mors_vif = (struct mm81x_vif *)vif->drv_priv;
	struct mm81x_sta *mors_sta = (struct mm81x_sta *)sta->drv_priv;

	/* Ignore both NOTEXIST to NONE and NONE to NOTEXIST */
	if ((old_state == IEEE80211_STA_NOTEXIST &&
	     new_state == IEEE80211_STA_NONE) ||
	    (old_state == IEEE80211_STA_NONE &&
	     new_state == IEEE80211_STA_NOTEXIST))
		return 0;

	if (vif->type == NL80211_IFTYPE_STATION)
		aid = vif->cfg.aid;
	else
		aid = sta->aid;

	ret = mm81x_cmd_sta_state(mors, mors_vif, aid, sta, new_state);
	if (ret < 0)
		goto exit;

	ether_addr_copy(mors_sta->addr, sta->addr);
	mors_sta->state = new_state;

	if (new_state > old_state && new_state == IEEE80211_STA_ASSOC) {
		if (vif->type == NL80211_IFTYPE_AP)
			mors_vif->u.ap.num_stas++;
		else if (vif->type == NL80211_IFTYPE_STATION)
			mors_vif->u.sta.is_assoc = true;
	}

	if (new_state < old_state && new_state == IEEE80211_STA_NONE) {
		if (vif->type == NL80211_IFTYPE_AP)
			mors_vif->u.ap.num_stas--;
		else if (vif->type == NL80211_IFTYPE_STATION)
			mors_vif->u.sta.is_assoc = false;
	}

exit:
	/*
	 * Always update our mmrc sta state even on failure to ensure
	 * we don't hold a dangling sta on error
	 */
	mm81x_rc_sta_state_check(mors, vif, sta, old_state, new_state);
	return new_state < old_state ? 0 : ret;
}

static int mm81x_mac_ops_ampdu_action(struct ieee80211_hw *hw,
				      struct ieee80211_vif *vif,
				      struct ieee80211_ampdu_params *params)
{
	u16 tid = params->tid;
	struct mm81x *mors = hw->priv;
	struct ieee80211_sta *sta = params->sta;
	struct mm81x_sta *mors_sta = (struct mm81x_sta *)sta->drv_priv;
	u16 buf_size =
		min_t(u16, params->buf_size, DOT11AH_BA_MAX_MPDU_PER_AMPDU);

	switch (params->action) {
	case IEEE80211_AMPDU_TX_START:
		dev_dbg(mors->dev, "%pM.%d A-MPDU TX start", mors_sta->addr,
			tid);
		ieee80211_start_tx_ba_cb_irqsafe(vif, sta->addr, tid);
		break;
	case IEEE80211_AMPDU_TX_STOP_CONT:
	case IEEE80211_AMPDU_TX_STOP_FLUSH:
	case IEEE80211_AMPDU_TX_STOP_FLUSH_CONT:
		dev_dbg(mors->dev, "%pM.%d A-MPDU TX flush", mors_sta->addr,
			tid);
		mors_sta->tid_start_tx[tid] = false;
		mors_sta->tid_tx[tid] = false;
		mors_sta->tid_params[tid] = 0;
		ieee80211_stop_tx_ba_cb_irqsafe(vif, sta->addr, tid);
		break;
	case IEEE80211_AMPDU_TX_OPERATIONAL:
		dev_dbg(mors->dev, "%pM.%d A-MPDU TX oper", mors_sta->addr,
			tid);
		mors_sta->tid_tx[tid] = true;
		if (!buf_size) {
			dev_err(mors->dev, "%pM.%d A-MPDU Invalid buf size",
				mors_sta->addr, tid);
			break;
		}
		mors_sta->tid_params[tid] =
			u8_encode_bits(buf_size - 1,
				       TX_INFO_TID_PARAMS_MAX_REORDER_BUF) |
			u8_encode_bits(1, TX_INFO_TID_PARAMS_AMPDU_ENABLED) |
			u8_encode_bits(params->amsdu,
				       TX_INFO_TID_PARAMS_AMSDU_SUPPORTED);
		break;
	default:
		break;
	}

	return 0;
}

static int mm81x_mac_ops_set_key(struct ieee80211_hw *hw, enum set_key_cmd cmd,
				 struct ieee80211_vif *vif,
				 struct ieee80211_sta *sta,
				 struct ieee80211_key_conf *key)
{
	u16 aid;
	int ret = -EOPNOTSUPP;
	struct mm81x *mors = hw->priv;
	struct mm81x_vif *mors_vif = (struct mm81x_vif *)vif->drv_priv;
	enum host_cmd_key_cipher cipher;
	enum host_cmd_aes_key_len length;

	if (vif->type == NL80211_IFTYPE_STATION) {
		aid = vif->cfg.aid;
	} else if (sta) {
		aid = sta->aid;
	} else {
		/* Is a group key - AID is unused */
		WARN_ON(key->flags & IEEE80211_KEY_FLAG_PAIRWISE);
		aid = 0;
	}

	switch (cmd) {
	case SET_KEY: {
		switch (key->cipher) {
		case WLAN_CIPHER_SUITE_CCMP:
		case WLAN_CIPHER_SUITE_CCMP_256:
			cipher = HOST_CMD_KEY_CIPHER_AES_CCM;
			break;
		case WLAN_CIPHER_SUITE_GCMP:
		case WLAN_CIPHER_SUITE_GCMP_256:
			cipher = HOST_CMD_KEY_CIPHER_AES_GCM;
			break;
		default:
			/* Cipher suite currently not supported */
			ret = -EOPNOTSUPP;
			goto exit;
		}

		switch (key->keylen) {
		case 16:
			length = HOST_CMD_AES_KEY_LEN_LENGTH_128;
			break;
		case 32:
			length = HOST_CMD_AES_KEY_LEN_LENGTH_256;
			break;
		default:
			/* Key length not supported */
			ret = -EOPNOTSUPP;
			goto exit;
		}

		ret = mm81x_cmd_install_key(mors, mors_vif, aid, key, cipher,
					    length);
		break;
	}
	case DISABLE_KEY:
		ret = mm81x_cmd_disable_key(mors, mors_vif, aid, key);
		if (ret) {
			/* Must return 0 */
			dev_warn(mors->dev, "Failed to remove key");
			ret = 0;
		}
		break;
	default:
		WARN_ON(1);
	}

	if (ret) {
		dev_dbg(mors->dev, "Falling back to software crypto");
		ret = 1;
	}

exit:
	return ret;
}

static int mm81x_mac_set_frag_threshold(struct ieee80211_hw *hw, int radio_idx,
					u32 value)
{
	struct mm81x *mors = hw->priv;

	return mm81x_cmd_set_frag_threshold(mors, value);
}

static u8 mm81x_rx_h_rc_bw_to_rx_bw(__le32 ratecode)
{
	enum dot11_bandwidth bw = mm81x_ratecode_bw_index_get(ratecode);

	switch (bw) {
	case DOT11_BANDWIDTH_1MHZ:
		return RATE_INFO_BW_1;
	case DOT11_BANDWIDTH_2MHZ:
		return RATE_INFO_BW_2;
	case DOT11_BANDWIDTH_4MHZ:
		return RATE_INFO_BW_4;
	case DOT11_BANDWIDTH_8MHZ:
		return RATE_INFO_BW_8;
	default:
		return RATE_INFO_BW_1;
	}
}

static void mm81x_rx_h_fill_status(struct mm81x *mors,
				   struct mm81x_skb_rx_status *hdr_rx_status,
				   struct ieee80211_rx_status *rx_status,
				   struct sk_buff *skb)
{
	u32 flags = le32_to_cpu(hdr_rx_status->flags);
	u16 freq_100khz = le16_to_cpu(hdr_rx_status->freq_100khz);
	__le32 ratecode = hdr_rx_status->mm81x_ratecode;

	rx_status->signal = le16_to_cpu(hdr_rx_status->rssi);
	rx_status->encoding = RX_ENC_S1G;
	rx_status->band = NL80211_BAND_S1GHZ;
	rx_status->freq = KHZ100_TO_MHZ(freq_100khz);
	rx_status->freq_offset = (freq_100khz % 10) ? 1 : 0;
	rx_status->nss = NSS_IDX_TO_NSS(mm81x_ratecode_nss_index_get(ratecode));

	if (flags & MM81X_RX_STATUS_FLAGS_DECRYPTED)
		rx_status->flag |= RX_FLAG_DECRYPTED;

	rx_status->rate_idx = mm81x_ratecode_mcs_index_get(ratecode);
	rx_status->bw = mm81x_rx_h_rc_bw_to_rx_bw(ratecode);

	if (mm81x_ratecode_sgi_get(ratecode))
		rx_status->enc_flags |= RX_ENC_FLAG_SHORT_GI;
}

static void mm81x_rx_h_update_sta(struct ieee80211_vif *vif,
				  struct ieee80211_hdr *hdr,
				  struct ieee80211_rx_status *rx_status)
{
	struct ieee80211_sta *sta;
	struct mm81x_sta *msta;
	u8 *lookup = ieee80211_is_s1g_beacon(hdr->frame_control) ? hdr->addr1 :
								   hdr->addr2;

	lockdep_assert_in_rcu_read_lock();

	sta = ieee80211_find_sta(vif, lookup);
	if (!sta)
		return;

	msta = (void *)sta->drv_priv;
	if (msta->avg_rssi) {
		msta->avg_rssi =
			CALC_AVG_RSSI(msta->avg_rssi, rx_status->signal);
	} else {
		msta->avg_rssi = rx_status->signal;
	}
}

static struct ieee80211_vif *
mm81x_rx_h_skb_get_vif(struct mm81x *mors, struct sk_buff *skb,
		       struct mm81x_skb_rx_status *hdr_rx_status)
{
	u8 vif_id = u32_get_bits(le32_to_cpu(hdr_rx_status->flags),
				 MM81X_RX_STATUS_FLAGS_VIF_ID);

	lockdep_assert_in_rcu_read_lock();

	if (vif_id == INVALID_VIF_INDEX)
		return NULL;

	return mm81x_rcu_dereference_vif_id(mors, vif_id, true);
}

void mm81x_mac_rx_skb(struct mm81x *mors, struct sk_buff *skb,
		      struct mm81x_skb_rx_status *hdr_rx_status)
{
	struct ieee80211_vif *vif;
	struct ieee80211_hw *hw = mors->hw;
	struct ieee80211_rx_status rx_status;
	struct ieee80211_hdr *hdr = (void *)skb->data;

	memset(&rx_status, 0, sizeof(rx_status));

	if (!mors->started || !skb->data || !skb->len) {
		dev_kfree_skb_any(skb);
		return;
	}

	mm81x_rx_h_fill_status(mors, hdr_rx_status, &rx_status, skb);

	scoped_guard(rcu) {
		vif = mm81x_rx_h_skb_get_vif(mors, skb, hdr_rx_status);
		if (!vif)
			goto rx;

		mm81x_rx_h_update_sta(vif, hdr, &rx_status);
	}

rx:
	memcpy(IEEE80211_SKB_RXCB(skb), &rx_status, sizeof(rx_status));
	ieee80211_rx_ni(hw, skb);
}

static void mm81x_mac_flush_queues(struct mm81x *mors)
{
	/*
	 * No need to call mm81x_skbq_stop_tx_queues as mac80211
	 * has already cancelled each queue prior to calling .flush()
	 */
	mm81x_skbq_data_traffic_pause(mors);

	flush_work(&mors->hif_work);
	flush_work(&mors->tx_stale_work);

	mm81x_hif_clear_events(mors);
	mm81x_hif_flush_tx_data(mors);
	mm81x_hif_flush_cmds(mors);

	/* Re-enable data, not that there will be any */
	mm81x_skbq_data_traffic_resume(mors);
}

static bool mm81x_mac_has_tx_pending(struct mm81x *mors)
{
	struct mm81x_skbq *mgmt_q = mm81x_hif_get_tx_mgmt_queue(mors);
	struct mm81x_skbq *tx_qs;
	int num_qs, i;

	mm81x_hif_skbq_get_tx_qs(mors, &tx_qs, &num_qs);
	for (i = 0; i < num_qs; i++)
		if (mm81x_skbq_count(&tx_qs[i]) ||
		    mm81x_skbq_pending_count(&tx_qs[i]))
			return true;

	if (mm81x_skbq_count(mgmt_q) || mm81x_skbq_pending_count(mgmt_q))
		return true;

	return false;
}

static void mm81x_mac_wait_queues(struct mm81x *mors)
{
	if (!wait_event_timeout(mors->tx_empty_waitq,
				!mm81x_mac_has_tx_pending(mors),
				MM81X_FLUSH_TIMEOUT))
		dev_warn(mors->dev, "Unable to empty queues before timeout");
}

static void mm81x_mac_ops_flush(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif, u32 queues,
				bool drop)
{
	struct mm81x *mors = hw->priv;

	/* We don't support IEEE80211_HW_QUEUE_CONTROL so flush all queues */
	if (drop)
		mm81x_mac_flush_queues(mors);
	else
		mm81x_mac_wait_queues(mors);
}

static int mm81x_mac_ops_set_rts_threshold(struct ieee80211_hw *hw,
					   int radio_idx, u32 value)
{
	struct mm81x *mors = hw->priv;

	mors->rts_threshold = value;
	return 0;
}

static void mm81x_mac_ops_sta_statistics(struct ieee80211_hw *hw,
					 struct ieee80211_vif *vif,
					 struct ieee80211_sta *sta,
					 struct station_info *sinfo)
{
	struct mm81x_sta *msta = (struct mm81x_sta *)sta->drv_priv;
	struct mm81x *mors = hw->priv;
	const struct mmrc_table *tb = msta->rc.tb;
	struct mmrc_rate rate;

	if (!tb || tb->best_tp.rate == MMRC_MCS_UNUSED) {
		sinfo->filled &= ~BIT_ULL(NL80211_STA_INFO_TX_BITRATE);
		return;
	}

	rate = tb->best_tp;
	sinfo->txrate.mcs = rate.rate;
	sinfo->txrate.nss = NSS_IDX_TO_NSS(rate.ss);
	sinfo->txrate.flags = RATE_INFO_FLAGS_S1G_MCS;
	switch (rate.bw) {
	case MMRC_BW_1MHZ:
		sinfo->txrate.bw = RATE_INFO_BW_1;
		break;
	case MMRC_BW_2MHZ:
		sinfo->txrate.bw = RATE_INFO_BW_2;
		break;
	case MMRC_BW_4MHZ:
		sinfo->txrate.bw = RATE_INFO_BW_4;
		break;
	case MMRC_BW_8MHZ:
		sinfo->txrate.bw = RATE_INFO_BW_8;
		break;
	default:
		break;
	}

	if (rate.guard == MMRC_GUARD_SHORT)
		sinfo->txrate.flags |= (RATE_INFO_FLAGS_SHORT_GI);

	dev_dbg(mors->dev, "mcs: %d, bw: %d, flag: 0x%x", rate.rate, rate.bw,
		sinfo->txrate.flags);
	sinfo->filled |= BIT_ULL(NL80211_STA_INFO_TX_BITRATE);
}

static u32 mm81x_get_expected_throughput(struct ieee80211_hw *hw,
					 struct ieee80211_sta *sta)
{
	struct mm81x_sta *msta = (struct mm81x_sta *)sta->drv_priv;
	struct mm81x *mors = hw->priv;
	const struct mmrc_table *tb = msta->rc.tb;
	struct mmrc_rate rate;
	u32 tput;

	if (!tb || tb->best_tp.rate == MMRC_MCS_UNUSED)
		return 0;

	rate = tb->best_tp;
	tput = BPS_TO_KBPS(mmrc_calculate_theoretical_throughput(rate));
	dev_dbg(mors->dev, "Throughput: MCS: %d, BW: %d, GI: %d -> %u",
		rate.rate, 1 << rate.bw, rate.guard, tput);

	return tput;
}

static void mm81x_mac_restart_cleanup_iter(void *data, u8 *mac,
					   struct ieee80211_vif *vif)
{
	if (vif->type == NL80211_IFTYPE_AP)
		mm81x_mac_beacon_finish((struct mm81x_vif *)vif->drv_priv);
}

static void mm81x_mac_restart_cleanup(struct mm81x *mors)
{
	ieee80211_iterate_active_interfaces(mors->hw,
					    IEEE80211_IFACE_ITER_NORMAL,
					    mm81x_mac_restart_cleanup_iter,
					    NULL);
	mm81x_mac_hw_scan_finish(mors);
}

static int mm81x_mac_restart(struct mm81x *mors)
{
	int ret;
	u32 chip_id;

	mors->started = false;
	mm81x_ps_disable(mors);
	mm81x_bus_set_irq(mors, false);
	mm81x_hw_irq_clear(mors);
	ieee80211_stop_queues(mors->hw);

	set_bit(MM81X_STATE_DATA_TX_STOPPED, &mors->state_flags);
	set_bit(MM81X_STATE_DATA_QS_STOPPED, &mors->state_flags);

	/* Allow time for in-transit tx/rx packets to settle */
	mdelay(MM81X_HW_RESTART_DELAY_MS);
	flush_work(&mors->hif_work);
	flush_work(&mors->tx_stale_work);
	mm81x_hif_clear_events(mors);
	mm81x_hif_flush_tx_data(mors);
	mm81x_hif_flush_cmds(mors);

	mm81x_claim_bus(mors);
	ret = mm81x_reg32_read(mors, MM81X_REG_CHIP_ID(mors), &chip_id);
	mm81x_release_bus(mors);

	if (ret < 0) {
		dev_err(mors->dev, "Failed to access HW: %d", ret);
		goto exit;
	}

	mm81x_mac_restart_cleanup(mors);

	ret = mm81x_fw_init(mors, true);
	if (ret < 0) {
		dev_err(mors->dev, "Failed to init firmware: %d", ret);
		goto exit;
	}

	mm81x_hw_irq_enable(mors, MM81X_INT_HW_STOP_NOTIFICATION_NUM, true);

	ret = mm81x_fw_parse_ext_host_tbl(mors);
	if (ret) {
		dev_err(mors->dev, "failed to parse extended host table: %d",
			ret);
		goto exit;
	}

	mm81x_mac_caps_init(mors);

	mm81x_bus_set_irq(mors, true);
	clear_bit(MM81X_STATE_DATA_TX_STOPPED, &mors->state_flags);
	clear_bit(MM81X_STATE_DATA_QS_STOPPED, &mors->state_flags);
	clear_bit(MM81X_STATE_CHIP_UNRESPONSIVE, &mors->state_flags);
	clear_bit(MM81X_STATE_RELOAD_FW_AFTER_START, &mors->state_flags);
	mm81x_mac_check_fw_disabled_chans(mors->hw);
	ieee80211_restart_hw(mors->hw);

exit:
	mm81x_ps_enable(mors);
	return ret;
}

static int mm81x_mac_ops_add_interface(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif)
{
	int ret = 0;
	struct mm81x *mors = hw->priv;
	struct mm81x_vif *mors_vif = (struct mm81x_vif *)vif->drv_priv;

	if (test_bit(MM81X_STATE_RELOAD_FW_AFTER_START, &mors->state_flags)) {
		dev_info(mors->dev, "Restarting chip with regdom: %s",
			 mors->country);

		ret = mm81x_mac_restart(mors);
		if (ret) {
			dev_err(mors->dev, "Failed to restart chip");
			return ret;
		}

		/*
		 * mac_restart will trigger ieee80211_hw_restart and
		 * add_interface will re-enter. just exit here instead.
		 */
		return 0;
	}

	vif->driver_flags |= IEEE80211_VIF_BEACON_FILTER;
	mors_vif->mors = mors;

	ret = mm81x_cmd_add_if(mors, &mors_vif->id, vif->addr, vif->type);
	if (ret) {
		dev_err(mors->dev, "mm81x_cmd_add_if failed %d", ret);
		return ret;
	}

	if (mors_vif->id >= ARRAY_SIZE(mors->vifs)) {
		dev_err(mors->dev, "vif_id is too large %u", mors_vif->id);
		ret = -EOPNOTSUPP;
		return ret;
	}

	if (mors_vif->id != (mors_vif->id & MM81X_TX_CONF_FLAGS_VIF_ID_MASK)) {
		dev_err(mors->dev, "invalid vif_id %u", mors_vif->id);
		ret = -EOPNOTSUPP;
		return ret;
	}

	rcu_assign_pointer(mors->vifs[mors_vif->id], vif);

	if (vif->type == NL80211_IFTYPE_AP)
		mm81x_mac_beacon_init(mors_vif);

	ret = mm81x_cmd_get_capabilities(mors, mors_vif->id, &mors->fw_caps);
	if (ret) {
		dev_err(mors->dev,
			"mm81x_cmd_get_capabilities failed for vif %d",
			mors_vif->id);
		return ret;
	}

	ieee80211_wake_queues(mors->hw);
	return ret;
}

static const struct ieee80211_ops mm81x_ops = {
	.start = mm81x_mac_ops_start,
	.stop = mm81x_mac_ops_stop,
	.config = mm81x_mac_ops_config,
	.wake_tx_queue = ieee80211_handle_wake_tx_queue,
	.tx = mm81x_mac_ops_tx,
	.add_interface = mm81x_mac_ops_add_interface,
	.remove_interface = mm81x_mac_ops_remove_interface,
	.configure_filter = mm81x_mac_ops_configure_filter,
	.sta_state = mm81x_mac_ops_sta_state,
	.flush = mm81x_mac_ops_flush,
	.set_frag_threshold = mm81x_mac_set_frag_threshold,
	.set_rts_threshold = mm81x_mac_ops_set_rts_threshold,
	.sta_statistics = mm81x_mac_ops_sta_statistics,
	.get_expected_throughput = mm81x_get_expected_throughput,
	.hw_scan = mm81x_mac_ops_hw_scan,
	.cancel_hw_scan = mm81x_mac_ops_cancel_hw_scan,
	.get_txpower = mm81x_mac_ops_get_txpower,
	.bss_info_changed = mm81x_mac_ops_bss_info_changed,
	.prepare_multicast = mm81x_mac_ops_prepare_multicast,
	.conf_tx = mm81x_mac_ops_conf_tx,
	.ampdu_action = mm81x_mac_ops_ampdu_action,
	.set_key = mm81x_mac_ops_set_key,
	.add_chanctx = ieee80211_emulate_add_chanctx,
	.remove_chanctx = ieee80211_emulate_remove_chanctx,
	.change_chanctx = ieee80211_emulate_change_chanctx,
	.switch_vif_chanctx = ieee80211_emulate_switch_vif_chanctx,
};

static void mm81x_reg_notifier(struct wiphy *wiphy,
			       struct regulatory_request *request)
{
	int ret;
	struct mm81x *mors = wiphy_to_ieee80211_hw(wiphy)->priv;

	if (mm81x_reg_h_cc_equal(request->alpha2, "00") ||
	    mm81x_reg_h_cc_equal(request->alpha2, mors->country))
		return;

	memcpy(mors->country, request->alpha2, sizeof(mors->country));

	ret = mm81x_mac_restart(mors);
	if (ret)
		dev_err(mors->dev, "Failed to restart chip: %d", ret);
}

static void mm81x_mac_config_hw(struct mm81x *mors)
{
	int i;
	struct ieee80211_hw *hw = mors->hw;
	struct wiphy *wiphy;

	for (i = 0; i < NUM_NL80211_BANDS; i++)
		hw->wiphy->bands[i] = NULL;

	hw->wiphy->bands[NL80211_BAND_S1GHZ] = &mors_band_s1ghz;
	hw->wiphy->interface_modes = BIT(NL80211_IFTYPE_AP) |
				     BIT(NL80211_IFTYPE_STATION);
	hw->wiphy->reg_notifier = mm81x_reg_notifier;
	hw->queues = MM81X_HW_QUEUE_COUNT;
	hw->max_rates = MM81X_HW_MAX_RATES;
	hw->max_report_rates = MM81X_HW_MAX_REPORT_RATES;
	hw->max_rate_tries = MM81X_HW_MAX_RATE_TRIES;
	hw->tx_sk_pacing_shift = MM81X_HW_TX_SK_PACING_SHIFT;
	hw->vif_data_size = sizeof(struct mm81x_vif);
	hw->sta_data_size = sizeof(struct mm81x_sta);
	hw->extra_tx_headroom =
		sizeof(struct mm81x_skb_hdr) + mm81x_bus_get_alignment(mors);

	mors->wiphy = hw->wiphy;

	ieee80211_hw_set(hw, SIGNAL_DBM);
	ieee80211_hw_set(hw, MFP_CAPABLE);
	ieee80211_hw_set(hw, REPORTS_TX_ACK_STATUS);
	ieee80211_hw_set(hw, AMPDU_AGGREGATION);
	ieee80211_hw_set(hw, HOST_BROADCAST_PS_BUFFERING);
	ieee80211_hw_set(hw, HAS_RATE_CONTROL);
	ieee80211_hw_set(hw, SUPPORTS_PS);
	ieee80211_hw_set(hw, NEED_DTIM_BEFORE_ASSOC);
	ieee80211_hw_set(hw, PS_NULLFUNC_STACK);
	ieee80211_hw_set(hw, SUPPORTS_TX_FRAG);
	ieee80211_hw_set(hw, SUPPORTS_NDP_BLOCKACK);

	SET_IEEE80211_PERM_ADDR(hw, mors->macaddr);

	wiphy = mors->wiphy;

	wiphy->flags |= WIPHY_FLAG_AP_UAPSD;
	wiphy->flags |= WIPHY_FLAG_PS_ON_BY_DEFAULT;

	if (!mors->ps.enable)
		wiphy->flags &= ~WIPHY_FLAG_PS_ON_BY_DEFAULT;

	wiphy->features |= NL80211_FEATURE_AP_MODE_CHAN_WIDTH_CHANGE |
			   NL80211_FEATURE_TX_POWER_INSERTION;

	wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_AIRTIME_FAIRNESS);
	wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_SET_SCAN_DWELL);

	wiphy->iface_combinations = mors_if_combs;
	wiphy->n_iface_combinations = ARRAY_SIZE(mors_if_combs);
	wiphy->max_scan_ie_len = MM81X_MAX_SCAN_IE_LEN;
	wiphy->max_scan_ssids = MM81X_MAX_SCAN_SSIDS;
	wiphy->signal_type = CFG80211_SIGNAL_TYPE_MBM;
	wiphy->max_remain_on_channel_duration =
		MM81X_MAX_REMAIN_ON_CHAN_DURATION;
}

static void mm81x_stale_tx_status_timer(struct timer_list *t)
{
	struct mm81x *mors = timer_container_of(mors, t, stale_status.timer);

	spin_lock_bh(&mors->stale_status.lock);
	if (mm81x_hif_get_tx_status_pending_count(mors))
		queue_work(mors->net_wq, &mors->tx_stale_work);
	spin_unlock_bh(&mors->stale_status.lock);
}

static void mm81x_stale_tx_status_timer_finish(struct mm81x *mors)
{
	timer_shutdown_sync(&mors->stale_status.timer);
}

static void mm81x_mac_stale_tx_status_timer_init(struct mm81x *mors)
{
	spin_lock_init(&mors->stale_status.lock);
	timer_setup(&mors->stale_status.timer, mm81x_stale_tx_status_timer, 0);
}

int mm81x_mac_register(struct mm81x *mors)
{
	int ret;
	struct ieee80211_hw *hw = mors->hw;

	mors->tx_power_mbm = INT_MAX;
	mors->tx_max_power_mbm = INT_MAX;
	mors->rts_threshold = IEEE80211_MAX_RTS_THRESHOLD;

	ret = mm81x_ps_init(mors);
	if (ret)
		return ret;

	mm81x_mac_config_hw(mors);
	mm81x_mac_hw_scan_init(mors);
	mm81x_mac_stale_tx_status_timer_init(mors);

	ret = ieee80211_register_hw(hw);
	if (ret) {
		dev_err(mors->dev, "ieee80211_register_hw failed %d", ret);
		mm81x_mac_unregister(mors);
		return ret;
	}

	mm81x_rc_init(mors);

	/*
	 * At this stage, we know bus and pager system interrupts are enabled.
	 * Trigger the receive workqueue to drain any incoming chip-to-host
	 * pending packets been pushed in the period between the firmware
	 * initialization and interrupts being enabled.
	 */
	set_bit(MM81X_HIF_EVT_RX_PEND, &mors->hif.event_flags);
	queue_work(mors->chip_wq, &mors->hif_work);

	return ret;
}

void mm81x_mac_unregister(struct mm81x *mors)
{
	mm81x_ps_disable(mors);
	mm81x_rc_deinit(mors);
	mm81x_mac_hw_scan_destroy(mors);

	ieee80211_stop_queues(mors->hw);
	ieee80211_unregister_hw(mors->hw);

	mm81x_hif_flush_tx_data(mors);
	mm81x_hif_flush_cmds(mors);
	mm81x_stale_tx_status_timer_finish(mors);
	mm81x_ps_finish(mors);

	kfree(mors->mcast_filter);
}

struct mm81x *mm81x_mac_alloc(size_t priv_size, struct device *dev)
{
	struct ieee80211_hw *hw;
	struct mm81x *mors;

	hw = ieee80211_alloc_hw(sizeof(*mors) + priv_size, &mm81x_ops);
	if (!hw) {
		dev_err(dev, "ieee80211_alloc_hw failed\r\n");
		return NULL;
	}

	SET_IEEE80211_DEV(hw, dev);
	memset(hw->priv, 0, sizeof(*mors));

	mors = hw->priv;
	mors->hw = hw;
	mors->dev = dev;
	mutex_init(&mors->cmd_lock);
	mutex_init(&mors->cmd_wait);
	init_waitqueue_head(&mors->tx_empty_waitq);

	return mors;
}

void mm81x_mac_free(struct mm81x *mors)
{
	ieee80211_free_hw(mors->hw);
}
