/*
 * Copyright (c) 2013 Eugene Krasnikov <k.eugene.e@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/etherdevice.h>
#include <linux/firmware.h>
#include <linux/bitops.h>
#include <linux/rpmsg.h>
#include "smd.h"

struct wcn36xx_cfg_val {
	u32 cfg_id;
	u32 value;
};

#define WCN36XX_CFG_VAL(id, val) \
{ \
	.cfg_id = WCN36XX_HAL_CFG_ ## id, \
	.value = val \
}

static struct wcn36xx_cfg_val wcn36xx_cfg_vals[] = {
	WCN36XX_CFG_VAL(CURRENT_TX_ANTENNA, 1),
	WCN36XX_CFG_VAL(CURRENT_RX_ANTENNA, 1),
	WCN36XX_CFG_VAL(LOW_GAIN_OVERRIDE, 0),
	WCN36XX_CFG_VAL(POWER_STATE_PER_CHAIN, 785),
	WCN36XX_CFG_VAL(CAL_PERIOD, 5),
	WCN36XX_CFG_VAL(CAL_CONTROL, 1),
	WCN36XX_CFG_VAL(PROXIMITY, 0),
	WCN36XX_CFG_VAL(NETWORK_DENSITY, 3),
	WCN36XX_CFG_VAL(MAX_MEDIUM_TIME, 6000),
	WCN36XX_CFG_VAL(MAX_MPDUS_IN_AMPDU, 64),
	WCN36XX_CFG_VAL(RTS_THRESHOLD, 2347),
	WCN36XX_CFG_VAL(SHORT_RETRY_LIMIT, 15),
	WCN36XX_CFG_VAL(LONG_RETRY_LIMIT, 15),
	WCN36XX_CFG_VAL(FRAGMENTATION_THRESHOLD, 8000),
	WCN36XX_CFG_VAL(DYNAMIC_THRESHOLD_ZERO, 5),
	WCN36XX_CFG_VAL(DYNAMIC_THRESHOLD_ONE, 10),
	WCN36XX_CFG_VAL(DYNAMIC_THRESHOLD_TWO, 15),
	WCN36XX_CFG_VAL(FIXED_RATE, 0),
	WCN36XX_CFG_VAL(RETRYRATE_POLICY, 4),
	WCN36XX_CFG_VAL(RETRYRATE_SECONDARY, 0),
	WCN36XX_CFG_VAL(RETRYRATE_TERTIARY, 0),
	WCN36XX_CFG_VAL(FORCE_POLICY_PROTECTION, 5),
	WCN36XX_CFG_VAL(FIXED_RATE_MULTICAST_24GHZ, 1),
	WCN36XX_CFG_VAL(FIXED_RATE_MULTICAST_5GHZ, 5),
	WCN36XX_CFG_VAL(DEFAULT_RATE_INDEX_5GHZ, 5),
	WCN36XX_CFG_VAL(MAX_BA_SESSIONS, 40),
	WCN36XX_CFG_VAL(PS_DATA_INACTIVITY_TIMEOUT, 200),
	WCN36XX_CFG_VAL(PS_ENABLE_BCN_FILTER, 1),
	WCN36XX_CFG_VAL(PS_ENABLE_RSSI_MONITOR, 1),
	WCN36XX_CFG_VAL(NUM_BEACON_PER_RSSI_AVERAGE, 20),
	WCN36XX_CFG_VAL(STATS_PERIOD, 10),
	WCN36XX_CFG_VAL(CFP_MAX_DURATION, 30000),
	WCN36XX_CFG_VAL(FRAME_TRANS_ENABLED, 0),
	WCN36XX_CFG_VAL(BA_THRESHOLD_HIGH, 128),
	WCN36XX_CFG_VAL(MAX_BA_BUFFERS, 2560),
	WCN36XX_CFG_VAL(DYNAMIC_PS_POLL_VALUE, 0),
	WCN36XX_CFG_VAL(TX_PWR_CTRL_ENABLE, 1),
	WCN36XX_CFG_VAL(ENABLE_CLOSE_LOOP, 1),
	WCN36XX_CFG_VAL(ENABLE_LPWR_IMG_TRANSITION, 0),
	WCN36XX_CFG_VAL(BTC_STATIC_LEN_LE_BT, 120000),
	WCN36XX_CFG_VAL(BTC_STATIC_LEN_LE_WLAN, 30000),
	WCN36XX_CFG_VAL(MAX_ASSOC_LIMIT, 10),
	WCN36XX_CFG_VAL(ENABLE_MCC_ADAPTIVE_SCHEDULER, 0),
	WCN36XX_CFG_VAL(ENABLE_DYNAMIC_RA_START_RATE, 133), /* MCS 5 */
	WCN36XX_CFG_VAL(LINK_FAIL_TX_CNT, 1000),
};

static struct wcn36xx_cfg_val wcn3680_cfg_vals[] = {
	WCN36XX_CFG_VAL(CURRENT_TX_ANTENNA, 1),
	WCN36XX_CFG_VAL(CURRENT_RX_ANTENNA, 1),
	WCN36XX_CFG_VAL(LOW_GAIN_OVERRIDE, 0),
	WCN36XX_CFG_VAL(POWER_STATE_PER_CHAIN, 785),
	WCN36XX_CFG_VAL(CAL_PERIOD, 5),
	WCN36XX_CFG_VAL(CAL_CONTROL, 1),
	WCN36XX_CFG_VAL(PROXIMITY, 0),
	WCN36XX_CFG_VAL(NETWORK_DENSITY, 3),
	WCN36XX_CFG_VAL(MAX_MEDIUM_TIME, 4096),
	WCN36XX_CFG_VAL(MAX_MPDUS_IN_AMPDU, 64),
	WCN36XX_CFG_VAL(RTS_THRESHOLD, 2347),
	WCN36XX_CFG_VAL(SHORT_RETRY_LIMIT, 15),
	WCN36XX_CFG_VAL(LONG_RETRY_LIMIT, 15),
	WCN36XX_CFG_VAL(FRAGMENTATION_THRESHOLD, 8000),
	WCN36XX_CFG_VAL(DYNAMIC_THRESHOLD_ZERO, 5),
	WCN36XX_CFG_VAL(DYNAMIC_THRESHOLD_ONE, 10),
	WCN36XX_CFG_VAL(DYNAMIC_THRESHOLD_TWO, 15),
	WCN36XX_CFG_VAL(FIXED_RATE, 0),
	WCN36XX_CFG_VAL(RETRYRATE_POLICY, 4),
	WCN36XX_CFG_VAL(RETRYRATE_SECONDARY, 0),
	WCN36XX_CFG_VAL(RETRYRATE_TERTIARY, 0),
	WCN36XX_CFG_VAL(FORCE_POLICY_PROTECTION, 5),
	WCN36XX_CFG_VAL(FIXED_RATE_MULTICAST_24GHZ, 1),
	WCN36XX_CFG_VAL(FIXED_RATE_MULTICAST_5GHZ, 5),
	WCN36XX_CFG_VAL(DEFAULT_RATE_INDEX_24GHZ, 1),
	WCN36XX_CFG_VAL(DEFAULT_RATE_INDEX_5GHZ, 5),
	WCN36XX_CFG_VAL(MAX_BA_SESSIONS, 40),
	WCN36XX_CFG_VAL(PS_DATA_INACTIVITY_TIMEOUT, 200),
	WCN36XX_CFG_VAL(PS_ENABLE_BCN_FILTER, 1),
	WCN36XX_CFG_VAL(PS_ENABLE_RSSI_MONITOR, 1),
	WCN36XX_CFG_VAL(NUM_BEACON_PER_RSSI_AVERAGE, 20),
	WCN36XX_CFG_VAL(STATS_PERIOD, 10),
	WCN36XX_CFG_VAL(CFP_MAX_DURATION, 30000),
	WCN36XX_CFG_VAL(FRAME_TRANS_ENABLED, 0),
	WCN36XX_CFG_VAL(BA_THRESHOLD_HIGH, 128),
	WCN36XX_CFG_VAL(MAX_BA_BUFFERS, 2560),
	WCN36XX_CFG_VAL(DYNAMIC_PS_POLL_VALUE, 0),
	WCN36XX_CFG_VAL(TX_PWR_CTRL_ENABLE, 1),
	WCN36XX_CFG_VAL(ENABLE_CLOSE_LOOP, 1),
	WCN36XX_CFG_VAL(ENABLE_LPWR_IMG_TRANSITION, 0),
	WCN36XX_CFG_VAL(BTC_STATIC_LEN_LE_BT, 120000),
	WCN36XX_CFG_VAL(BTC_STATIC_LEN_LE_WLAN, 30000),
	WCN36XX_CFG_VAL(MAX_ASSOC_LIMIT, 10),
	WCN36XX_CFG_VAL(ENABLE_MCC_ADAPTIVE_SCHEDULER, 0),
	WCN36XX_CFG_VAL(TDLS_PUAPSD_MASK, 0),
	WCN36XX_CFG_VAL(TDLS_PUAPSD_BUFFER_STA_CAPABLE, 1),
	WCN36XX_CFG_VAL(TDLS_PUAPSD_INACTIVITY_TIME, 0),
	WCN36XX_CFG_VAL(TDLS_PUAPSD_RX_FRAME_THRESHOLD, 10),
	WCN36XX_CFG_VAL(TDLS_OFF_CHANNEL_CAPABLE, 1),
	WCN36XX_CFG_VAL(ENABLE_ADAPTIVE_RX_DRAIN, 1),
	WCN36XX_CFG_VAL(FLEXCONNECT_POWER_FACTOR, 0),
	WCN36XX_CFG_VAL(ANTENNA_DIVERSITY, 3),
	WCN36XX_CFG_VAL(ATH_DISABLE, 0),
	WCN36XX_CFG_VAL(BTC_STATIC_OPP_WLAN_ACTIVE_WLAN_LEN, 60000),
	WCN36XX_CFG_VAL(BTC_STATIC_OPP_WLAN_ACTIVE_BT_LEN, 90000),
	WCN36XX_CFG_VAL(BTC_SAP_STATIC_OPP_ACTIVE_WLAN_LEN, 30000),
	WCN36XX_CFG_VAL(BTC_SAP_STATIC_OPP_ACTIVE_BT_LEN, 30000),
	WCN36XX_CFG_VAL(ASD_PROBE_INTERVAL, 50),
	WCN36XX_CFG_VAL(ASD_TRIGGER_THRESHOLD, -60),
	WCN36XX_CFG_VAL(ASD_RTT_RSSI_HYST_THRESHOLD, 3),
	WCN36XX_CFG_VAL(BTC_CTS2S_ON_STA_DURING_SCO, 0),
	WCN36XX_CFG_VAL(RA_FILTER_ENABLE, 0),
	WCN36XX_CFG_VAL(RA_RATE_LIMIT_INTERVAL, 60),
	WCN36XX_CFG_VAL(BTC_FATAL_HID_NSNIFF_BLK, 2),
	WCN36XX_CFG_VAL(BTC_CRITICAL_HID_NSNIFF_BLK, 1),
	WCN36XX_CFG_VAL(BTC_DYN_A2DP_TX_QUEUE_THOLD, 0),
	WCN36XX_CFG_VAL(BTC_DYN_OPP_TX_QUEUE_THOLD, 1),
	WCN36XX_CFG_VAL(MAX_UAPSD_CONSEC_SP, 10),
	WCN36XX_CFG_VAL(MAX_UAPSD_CONSEC_RX_CNT, 50),
	WCN36XX_CFG_VAL(MAX_UAPSD_CONSEC_TX_CNT, 50),
	WCN36XX_CFG_VAL(MAX_UAPSD_CONSEC_TX_CNT_MEAS_WINDOW, 500),
	WCN36XX_CFG_VAL(MAX_UAPSD_CONSEC_RX_CNT_MEAS_WINDOW, 500),
	WCN36XX_CFG_VAL(MAX_PSPOLL_IN_WMM_UAPSD_PS_MODE, 0),
	WCN36XX_CFG_VAL(MAX_UAPSD_INACTIVITY_INTERVALS, 10),
	WCN36XX_CFG_VAL(ENABLE_DYNAMIC_WMMPS, 1),
	WCN36XX_CFG_VAL(BURST_MODE_BE_TXOP_VALUE, 0),
	WCN36XX_CFG_VAL(ENABLE_DYNAMIC_RA_START_RATE, 136),
	WCN36XX_CFG_VAL(BTC_FAST_WLAN_CONN_PREF, 1),
	WCN36XX_CFG_VAL(ENABLE_RTSCTS_HTVHT, 0),
	WCN36XX_CFG_VAL(BTC_STATIC_OPP_WLAN_IDLE_WLAN_LEN, 30000),
	WCN36XX_CFG_VAL(BTC_STATIC_OPP_WLAN_IDLE_BT_LEN, 120000),
	WCN36XX_CFG_VAL(LINK_FAIL_TX_CNT, 1000),
	WCN36XX_CFG_VAL(TOGGLE_ARP_BDRATES, 0),
	WCN36XX_CFG_VAL(OPTIMIZE_CA_EVENT, 0),
	WCN36XX_CFG_VAL(EXT_SCAN_CONC_MODE, 0),
	WCN36XX_CFG_VAL(BAR_WAKEUP_HOST_DISABLE, 0),
	WCN36XX_CFG_VAL(SAR_BOFFSET_CORRECTION_ENABLE, 0),
	WCN36XX_CFG_VAL(BTC_DISABLE_WLAN_LINK_CRITICAL, 5),
	WCN36XX_CFG_VAL(DISABLE_SCAN_DURING_SCO, 2),
	WCN36XX_CFG_VAL(CONS_BCNMISS_COUNT, 0),
	WCN36XX_CFG_VAL(UNITS_OF_BCN_WAIT_TIME, 0),
	WCN36XX_CFG_VAL(TRIGGER_NULLFRAME_BEFORE_HB, 0),
	WCN36XX_CFG_VAL(ENABLE_POWERSAVE_OFFLOAD, 0),
};

static int put_cfg_tlv_u32(struct wcn36xx *wcn, size_t *len, u32 id, u32 value)
{
	struct wcn36xx_hal_cfg *entry;
	u32 *val;

	if (*len + sizeof(*entry) + sizeof(u32) >= WCN36XX_HAL_BUF_SIZE) {
		wcn36xx_err("Not enough room for TLV entry\n");
		return -ENOMEM;
	}

	entry = (struct wcn36xx_hal_cfg *) (wcn->hal_buf + *len);
	entry->id = id;
	entry->len = sizeof(u32);
	entry->pad_bytes = 0;
	entry->reserve = 0;

	val = (u32 *) (entry + 1);
	*val = value;

	*len += sizeof(*entry) + sizeof(u32);

	return 0;
}

static void wcn36xx_smd_set_bss_nw_type(struct wcn36xx *wcn,
		struct ieee80211_sta *sta,
		struct wcn36xx_hal_config_bss_params *bss_params)
{
	if (NL80211_BAND_5GHZ == WCN36XX_BAND(wcn))
		bss_params->nw_type = WCN36XX_HAL_11A_NW_TYPE;
	else if (sta && sta->ht_cap.ht_supported)
		bss_params->nw_type = WCN36XX_HAL_11N_NW_TYPE;
	else if (sta && (sta->supp_rates[NL80211_BAND_2GHZ] & 0x7f))
		bss_params->nw_type = WCN36XX_HAL_11G_NW_TYPE;
	else
		bss_params->nw_type = WCN36XX_HAL_11B_NW_TYPE;
}

static inline u8 is_cap_supported(unsigned long caps, unsigned long flag)
{
	return caps & flag ? 1 : 0;
}

static void wcn36xx_smd_set_bss_ht_params(struct ieee80211_vif *vif,
		struct ieee80211_sta *sta,
		struct wcn36xx_hal_config_bss_params *bss_params)
{
	if (sta && sta->ht_cap.ht_supported) {
		unsigned long caps = sta->ht_cap.cap;
		bss_params->ht = sta->ht_cap.ht_supported;
		bss_params->tx_channel_width_set = is_cap_supported(caps,
			IEEE80211_HT_CAP_SUP_WIDTH_20_40);
		bss_params->lsig_tx_op_protection_full_support =
			is_cap_supported(caps,
					 IEEE80211_HT_CAP_LSIG_TXOP_PROT);

		bss_params->ht_oper_mode = vif->bss_conf.ht_operation_mode;
		bss_params->lln_non_gf_coexist =
			!!(vif->bss_conf.ht_operation_mode &
			   IEEE80211_HT_OP_MODE_NON_GF_STA_PRSNT);
		/* IEEE80211_HT_STBC_PARAM_DUAL_CTS_PROT */
		bss_params->dual_cts_protection = 0;
		/* IEEE80211_HT_OP_MODE_PROTECTION_20MHZ */
		bss_params->ht20_coexist = 0;
	}
}

static void
wcn36xx_smd_set_bss_vht_params(struct ieee80211_vif *vif,
			       struct ieee80211_sta *sta,
			       struct wcn36xx_hal_config_bss_params_v1 *bss)
{
	if (sta && sta->vht_cap.vht_supported)
		bss->vht_capable = 1;
}

static void wcn36xx_smd_set_sta_ht_params(struct ieee80211_sta *sta,
		struct wcn36xx_hal_config_sta_params *sta_params)
{
	if (sta->ht_cap.ht_supported) {
		unsigned long caps = sta->ht_cap.cap;
		sta_params->ht_capable = sta->ht_cap.ht_supported;
		sta_params->tx_channel_width_set = is_cap_supported(caps,
			IEEE80211_HT_CAP_SUP_WIDTH_20_40);
		sta_params->lsig_txop_protection = is_cap_supported(caps,
			IEEE80211_HT_CAP_LSIG_TXOP_PROT);

		sta_params->max_ampdu_size = sta->ht_cap.ampdu_factor;
		sta_params->max_ampdu_density = sta->ht_cap.ampdu_density;
		sta_params->max_amsdu_size = is_cap_supported(caps,
			IEEE80211_HT_CAP_MAX_AMSDU);
		sta_params->sgi_20Mhz = is_cap_supported(caps,
			IEEE80211_HT_CAP_SGI_20);
		sta_params->sgi_40mhz =	is_cap_supported(caps,
			IEEE80211_HT_CAP_SGI_40);
		sta_params->green_field_capable = is_cap_supported(caps,
			IEEE80211_HT_CAP_GRN_FLD);
		sta_params->delayed_ba_support = is_cap_supported(caps,
			IEEE80211_HT_CAP_DELAY_BA);
		sta_params->dsss_cck_mode_40mhz = is_cap_supported(caps,
			IEEE80211_HT_CAP_DSSSCCK40);
	}
}

static void wcn36xx_smd_set_sta_vht_params(struct wcn36xx *wcn,
		struct ieee80211_sta *sta,
		struct wcn36xx_hal_config_sta_params_v1 *sta_params)
{
	if (sta->vht_cap.vht_supported) {
		unsigned long caps = sta->vht_cap.cap;

		sta_params->vht_capable = sta->vht_cap.vht_supported;
		sta_params->vht_ldpc_enabled =
			is_cap_supported(caps, IEEE80211_VHT_CAP_RXLDPC);
		if (get_feat_caps(wcn->fw_feat_caps, MU_MIMO)) {
			sta_params->vht_tx_mu_beamformee_capable =
				is_cap_supported(caps, IEEE80211_VHT_CAP_MU_BEAMFORMER_CAPABLE);
			if (sta_params->vht_tx_mu_beamformee_capable)
			       sta_params->vht_tx_bf_enabled = 1;
		} else {
			sta_params->vht_tx_mu_beamformee_capable = 0;
		}
		sta_params->vht_tx_channel_width_set = 0;
	}
}

static void wcn36xx_smd_set_sta_ht_ldpc_params(struct ieee80211_sta *sta,
		struct wcn36xx_hal_config_sta_params_v1 *sta_params)
{
	if (sta->ht_cap.ht_supported) {
		sta_params->ht_ldpc_enabled =
			is_cap_supported(sta->ht_cap.cap, IEEE80211_HT_CAP_LDPC_CODING);
	}
}

static void wcn36xx_smd_set_sta_default_ht_params(
		struct wcn36xx_hal_config_sta_params *sta_params)
{
	sta_params->ht_capable = 1;
	sta_params->tx_channel_width_set = 1;
	sta_params->lsig_txop_protection = 1;
	sta_params->max_ampdu_size = 3;
	sta_params->max_ampdu_density = 5;
	sta_params->max_amsdu_size = 0;
	sta_params->sgi_20Mhz = 1;
	sta_params->sgi_40mhz = 1;
	sta_params->green_field_capable = 1;
	sta_params->delayed_ba_support = 0;
	sta_params->dsss_cck_mode_40mhz = 1;
}

static void wcn36xx_smd_set_sta_default_vht_params(struct wcn36xx *wcn,
		struct wcn36xx_hal_config_sta_params_v1 *sta_params)
{
	if (wcn->rf_id == RF_IRIS_WCN3680) {
		sta_params->vht_capable = 1;
		sta_params->vht_tx_mu_beamformee_capable = 1;
	} else {
		sta_params->vht_capable = 0;
		sta_params->vht_tx_mu_beamformee_capable = 0;
	}

	sta_params->vht_ldpc_enabled = 0;
	sta_params->vht_tx_channel_width_set = 0;
	sta_params->vht_tx_bf_enabled = 0;
}

static void wcn36xx_smd_set_sta_default_ht_ldpc_params(struct wcn36xx *wcn,
		struct wcn36xx_hal_config_sta_params_v1 *sta_params)
{
	if (wcn->rf_id == RF_IRIS_WCN3680)
		sta_params->ht_ldpc_enabled = 1;
	else
		sta_params->ht_ldpc_enabled = 0;
}

static void wcn36xx_smd_set_sta_params(struct wcn36xx *wcn,
		struct ieee80211_vif *vif,
		struct ieee80211_sta *sta,
		struct wcn36xx_hal_config_sta_params *sta_params)
{
	struct wcn36xx_vif *vif_priv = wcn36xx_vif_to_priv(vif);
	struct wcn36xx_sta *sta_priv = NULL;
	if (vif->type == NL80211_IFTYPE_ADHOC ||
	    vif->type == NL80211_IFTYPE_AP ||
	    vif->type == NL80211_IFTYPE_MESH_POINT) {
		sta_params->type = 1;
		sta_params->sta_index = WCN36XX_HAL_STA_INVALID_IDX;
	} else {
		sta_params->type = 0;
		sta_params->sta_index = vif_priv->self_sta_index;
	}

	sta_params->listen_interval = WCN36XX_LISTEN_INTERVAL(wcn);

	/*
	 * In STA mode ieee80211_sta contains bssid and ieee80211_vif
	 * contains our mac address. In  AP mode we are bssid so vif
	 * contains bssid and ieee80211_sta contains mac.
	 */
	if (NL80211_IFTYPE_STATION == vif->type)
		memcpy(&sta_params->mac, vif->addr, ETH_ALEN);
	else
		memcpy(&sta_params->bssid, vif->addr, ETH_ALEN);

	sta_params->encrypt_type = vif_priv->encrypt_type;
	sta_params->short_preamble_supported = true;

	sta_params->rifs_mode = 0;
	sta_params->rmf = 0;
	sta_params->action = 0;
	sta_params->uapsd = 0;
	sta_params->mimo_ps = WCN36XX_HAL_HT_MIMO_PS_STATIC;
	sta_params->max_ampdu_duration = 0;
	sta_params->bssid_index = vif_priv->bss_index;
	sta_params->p2p = 0;

	if (sta) {
		sta_priv = wcn36xx_sta_to_priv(sta);
		if (NL80211_IFTYPE_STATION == vif->type)
			memcpy(&sta_params->bssid, sta->addr, ETH_ALEN);
		else
			memcpy(&sta_params->mac, sta->addr, ETH_ALEN);
		sta_params->wmm_enabled = sta->wme;
		sta_params->max_sp_len = sta->max_sp;
		sta_params->aid = sta_priv->aid;
		wcn36xx_smd_set_sta_ht_params(sta, sta_params);
		memcpy(&sta_params->supported_rates, &sta_priv->supported_rates,
			sizeof(struct wcn36xx_hal_supported_rates));
	} else {
		wcn36xx_set_default_rates((struct wcn36xx_hal_supported_rates *)
					  &sta_params->supported_rates);
		wcn36xx_smd_set_sta_default_ht_params(sta_params);
	}
}

static int wcn36xx_smd_send_and_wait(struct wcn36xx *wcn, size_t len)
{
	int ret;
	unsigned long start;
	struct wcn36xx_hal_msg_header *hdr =
		(struct wcn36xx_hal_msg_header *)wcn->hal_buf;
	u16 req_type = hdr->msg_type;

	wcn36xx_dbg_dump(WCN36XX_DBG_SMD_DUMP, "HAL >>> ", wcn->hal_buf, len);

	init_completion(&wcn->hal_rsp_compl);
	start = jiffies;
	ret = rpmsg_send(wcn->smd_channel, wcn->hal_buf, len);
	if (ret) {
		wcn36xx_err("HAL TX failed for req %d\n", req_type);
		goto out;
	}
	if (wait_for_completion_timeout(&wcn->hal_rsp_compl,
		msecs_to_jiffies(HAL_MSG_TIMEOUT)) <= 0) {
		wcn36xx_err("Timeout! No SMD response to req %d in %dms\n",
			    req_type, HAL_MSG_TIMEOUT);
		ret = -ETIME;
		goto out;
	}
	wcn36xx_dbg(WCN36XX_DBG_SMD,
		    "SMD command (req %d, rsp %d) completed in %dms\n",
		    req_type, hdr->msg_type,
		    jiffies_to_msecs(jiffies - start));
out:
	return ret;
}

static void init_hal_msg(struct wcn36xx_hal_msg_header *hdr,
			 enum wcn36xx_hal_host_msg_type msg_type,
			 size_t msg_size)
{
	memset(hdr, 0, msg_size + sizeof(*hdr));
	hdr->msg_type = msg_type;
	hdr->msg_version = WCN36XX_HAL_MSG_VERSION0;
	hdr->len = msg_size + sizeof(*hdr);
}

#define __INIT_HAL_MSG(msg_body, type, version) \
	do {								\
		memset(&msg_body, 0, sizeof(msg_body));			\
		msg_body.header.msg_type = type;			\
		msg_body.header.msg_version = version;			\
		msg_body.header.len = sizeof(msg_body);			\
	} while (0)							\

#define INIT_HAL_MSG(msg_body, type)	\
	__INIT_HAL_MSG(msg_body, type, WCN36XX_HAL_MSG_VERSION0)

#define INIT_HAL_MSG_V1(msg_body, type) \
	__INIT_HAL_MSG(msg_body, type, WCN36XX_HAL_MSG_VERSION1)

#define INIT_HAL_PTT_MSG(p_msg_body, ppt_msg_len) \
	do { \
		memset(p_msg_body, 0, sizeof(*p_msg_body) + ppt_msg_len); \
		p_msg_body->header.msg_type = WCN36XX_HAL_PROCESS_PTT_REQ; \
		p_msg_body->header.msg_version = WCN36XX_HAL_MSG_VERSION0; \
		p_msg_body->header.len = sizeof(*p_msg_body) + ppt_msg_len; \
	} while (0)

#define PREPARE_HAL_BUF(send_buf, msg_body) \
	do {							\
		memset(send_buf, 0, msg_body.header.len);	\
		memcpy(send_buf, &msg_body, sizeof(msg_body));	\
	} while (0)						\

#define PREPARE_HAL_PTT_MSG_BUF(send_buf, p_msg_body) \
	do {							\
		memset(send_buf, 0, p_msg_body->header.len); \
		memcpy(send_buf, p_msg_body, p_msg_body->header.len); \
	} while (0)

static int wcn36xx_smd_rsp_status_check(void *buf, size_t len)
{
	struct wcn36xx_fw_msg_status_rsp *rsp;

	if (len < sizeof(struct wcn36xx_hal_msg_header) +
	    sizeof(struct wcn36xx_fw_msg_status_rsp))
		return -EIO;

	rsp = (struct wcn36xx_fw_msg_status_rsp *)
		(buf + sizeof(struct wcn36xx_hal_msg_header));

	if (WCN36XX_FW_MSG_RESULT_SUCCESS != rsp->status)
		return rsp->status;

	return 0;
}

int wcn36xx_smd_load_nv(struct wcn36xx *wcn)
{
	struct nv_data *nv_d;
	struct wcn36xx_hal_nv_img_download_req_msg msg_body;
	int fw_bytes_left;
	int ret;
	u16 fm_offset = 0;

	if (!wcn->nv) {
		ret = request_firmware(&wcn->nv, WLAN_NV_FILE, wcn->dev);
		if (ret) {
			wcn36xx_err("Failed to load nv file %s: %d\n",
				      WLAN_NV_FILE, ret);
			goto out;
		}
	}

	nv_d = (struct nv_data *)wcn->nv->data;
	INIT_HAL_MSG(msg_body, WCN36XX_HAL_DOWNLOAD_NV_REQ);

	msg_body.header.len += WCN36XX_NV_FRAGMENT_SIZE;

	msg_body.frag_number = 0;
	/* hal_buf must be protected with  mutex */
	mutex_lock(&wcn->hal_mutex);

	do {
		fw_bytes_left = wcn->nv->size - fm_offset - 4;
		if (fw_bytes_left > WCN36XX_NV_FRAGMENT_SIZE) {
			msg_body.last_fragment = 0;
			msg_body.nv_img_buffer_size = WCN36XX_NV_FRAGMENT_SIZE;
		} else {
			msg_body.last_fragment = 1;
			msg_body.nv_img_buffer_size = fw_bytes_left;

			/* Do not forget update general message len */
			msg_body.header.len = sizeof(msg_body) + fw_bytes_left;

		}

		/* Add load NV request message header */
		memcpy(wcn->hal_buf, &msg_body,	sizeof(msg_body));

		/* Add NV body itself */
		memcpy(wcn->hal_buf + sizeof(msg_body),
		       &nv_d->table + fm_offset,
		       msg_body.nv_img_buffer_size);

		ret = wcn36xx_smd_send_and_wait(wcn, msg_body.header.len);
		if (ret)
			goto out_unlock;
		ret = wcn36xx_smd_rsp_status_check(wcn->hal_buf,
						   wcn->hal_rsp_len);
		if (ret) {
			wcn36xx_err("hal_load_nv response failed err=%d\n",
				    ret);
			goto out_unlock;
		}
		msg_body.frag_number++;
		fm_offset += WCN36XX_NV_FRAGMENT_SIZE;

	} while (msg_body.last_fragment != 1);

out_unlock:
	mutex_unlock(&wcn->hal_mutex);
out:	return ret;
}

static int wcn36xx_smd_start_rsp(struct wcn36xx *wcn, void *buf, size_t len)
{
	struct wcn36xx_hal_mac_start_rsp_msg *rsp;

	if (len < sizeof(*rsp))
		return -EIO;

	rsp = (struct wcn36xx_hal_mac_start_rsp_msg *)buf;

	if (WCN36XX_FW_MSG_RESULT_SUCCESS != rsp->start_rsp_params.status)
		return -EIO;

	memcpy(wcn->crm_version, rsp->start_rsp_params.crm_version,
	       WCN36XX_HAL_VERSION_LENGTH);
	memcpy(wcn->wlan_version, rsp->start_rsp_params.wlan_version,
	       WCN36XX_HAL_VERSION_LENGTH);

	/* null terminate the strings, just in case */
	wcn->crm_version[WCN36XX_HAL_VERSION_LENGTH] = '\0';
	wcn->wlan_version[WCN36XX_HAL_VERSION_LENGTH] = '\0';

	wcn->fw_revision = rsp->start_rsp_params.version.revision;
	wcn->fw_version = rsp->start_rsp_params.version.version;
	wcn->fw_minor = rsp->start_rsp_params.version.minor;
	wcn->fw_major = rsp->start_rsp_params.version.major;

	if (wcn->first_boot) {
		wcn->first_boot = false;
		wcn36xx_info("firmware WLAN version '%s' and CRM version '%s'\n",
			     wcn->wlan_version, wcn->crm_version);

		wcn36xx_info("firmware API %u.%u.%u.%u, %u stations, %u bssids\n",
			     wcn->fw_major, wcn->fw_minor,
			     wcn->fw_version, wcn->fw_revision,
			     rsp->start_rsp_params.stations,
			     rsp->start_rsp_params.bssids);
	}
	return 0;
}

int wcn36xx_smd_start(struct wcn36xx *wcn)
{
	struct wcn36xx_hal_mac_start_req_msg msg_body, *body;
	int ret;
	int i;
	size_t len;
	int cfg_elements;
	static struct wcn36xx_cfg_val *cfg_vals;

	mutex_lock(&wcn->hal_mutex);
	INIT_HAL_MSG(msg_body, WCN36XX_HAL_START_REQ);

	msg_body.params.type = DRIVER_TYPE_PRODUCTION;
	msg_body.params.len = 0;

	PREPARE_HAL_BUF(wcn->hal_buf, msg_body);

	body = (struct wcn36xx_hal_mac_start_req_msg *)wcn->hal_buf;
	len = body->header.len;

	if (wcn->rf_id == RF_IRIS_WCN3680) {
		cfg_vals = wcn3680_cfg_vals;
		cfg_elements = ARRAY_SIZE(wcn3680_cfg_vals);
	} else {
		cfg_vals = wcn36xx_cfg_vals;
		cfg_elements = ARRAY_SIZE(wcn36xx_cfg_vals);
	}

	for (i = 0; i < cfg_elements; i++) {
		ret = put_cfg_tlv_u32(wcn, &len, cfg_vals[i].cfg_id,
				      cfg_vals[i].value);
		if (ret)
			goto out;
	}
	body->header.len = len;
	body->params.len = len - sizeof(*body);

	wcn36xx_dbg(WCN36XX_DBG_HAL, "hal start type %d\n",
		    msg_body.params.type);

	ret = wcn36xx_smd_send_and_wait(wcn, body->header.len);
	if (ret) {
		wcn36xx_err("Sending hal_start failed\n");
		goto out;
	}

	ret = wcn36xx_smd_start_rsp(wcn, wcn->hal_buf, wcn->hal_rsp_len);
	if (ret) {
		wcn36xx_err("hal_start response failed err=%d\n", ret);
		goto out;
	}

out:
	mutex_unlock(&wcn->hal_mutex);
	return ret;
}

int wcn36xx_smd_stop(struct wcn36xx *wcn)
{
	struct wcn36xx_hal_mac_stop_req_msg msg_body;
	int ret;

	mutex_lock(&wcn->hal_mutex);
	INIT_HAL_MSG(msg_body, WCN36XX_HAL_STOP_REQ);

	msg_body.stop_req_params.reason = HAL_STOP_TYPE_RF_KILL;

	PREPARE_HAL_BUF(wcn->hal_buf, msg_body);

	ret = wcn36xx_smd_send_and_wait(wcn, msg_body.header.len);
	if (ret) {
		wcn36xx_err("Sending hal_stop failed\n");
		goto out;
	}
	ret = wcn36xx_smd_rsp_status_check(wcn->hal_buf, wcn->hal_rsp_len);
	if (ret) {
		wcn36xx_err("hal_stop response failed err=%d\n", ret);
		goto out;
	}
out:
	mutex_unlock(&wcn->hal_mutex);
	return ret;
}

int wcn36xx_smd_init_scan(struct wcn36xx *wcn, enum wcn36xx_hal_sys_mode mode,
			  struct ieee80211_vif *vif)
{
	struct wcn36xx_vif *vif_priv = wcn36xx_vif_to_priv(vif);
	struct wcn36xx_hal_init_scan_req_msg msg_body;
	int ret;

	mutex_lock(&wcn->hal_mutex);
	INIT_HAL_MSG(msg_body, WCN36XX_HAL_INIT_SCAN_REQ);

	msg_body.mode = mode;
	if (vif_priv->bss_index != WCN36XX_HAL_BSS_INVALID_IDX) {
		/* Notify BSSID with null DATA packet */
		msg_body.frame_type = 2;
		msg_body.notify = 1;
		msg_body.scan_entry.bss_index[0] = vif_priv->bss_index;
		msg_body.scan_entry.active_bss_count = 1;
	}

	PREPARE_HAL_BUF(wcn->hal_buf, msg_body);

	wcn36xx_dbg(WCN36XX_DBG_HAL, "hal init scan mode %d\n", msg_body.mode);

	ret = wcn36xx_smd_send_and_wait(wcn, msg_body.header.len);
	if (ret) {
		wcn36xx_err("Sending hal_init_scan failed\n");
		goto out;
	}
	ret = wcn36xx_smd_rsp_status_check(wcn->hal_buf, wcn->hal_rsp_len);
	if (ret) {
		wcn36xx_err("hal_init_scan response failed err=%d\n", ret);
		goto out;
	}
out:
	mutex_unlock(&wcn->hal_mutex);
	return ret;
}

int wcn36xx_smd_start_scan(struct wcn36xx *wcn, u8 scan_channel)
{
	struct wcn36xx_hal_start_scan_req_msg msg_body;
	int ret;

	mutex_lock(&wcn->hal_mutex);
	INIT_HAL_MSG(msg_body, WCN36XX_HAL_START_SCAN_REQ);

	msg_body.scan_channel = scan_channel;

	PREPARE_HAL_BUF(wcn->hal_buf, msg_body);

	wcn36xx_dbg(WCN36XX_DBG_HAL, "hal start scan channel %d\n",
		    msg_body.scan_channel);

	ret = wcn36xx_smd_send_and_wait(wcn, msg_body.header.len);
	if (ret) {
		wcn36xx_err("Sending hal_start_scan failed\n");
		goto out;
	}
	ret = wcn36xx_smd_rsp_status_check(wcn->hal_buf, wcn->hal_rsp_len);
	if (ret) {
		wcn36xx_err("hal_start_scan response failed err=%d\n", ret);
		goto out;
	}
out:
	mutex_unlock(&wcn->hal_mutex);
	return ret;
}

int wcn36xx_smd_end_scan(struct wcn36xx *wcn, u8 scan_channel)
{
	struct wcn36xx_hal_end_scan_req_msg msg_body;
	int ret;

	mutex_lock(&wcn->hal_mutex);
	INIT_HAL_MSG(msg_body, WCN36XX_HAL_END_SCAN_REQ);

	msg_body.scan_channel = scan_channel;

	PREPARE_HAL_BUF(wcn->hal_buf, msg_body);

	wcn36xx_dbg(WCN36XX_DBG_HAL, "hal end scan channel %d\n",
		    msg_body.scan_channel);

	ret = wcn36xx_smd_send_and_wait(wcn, msg_body.header.len);
	if (ret) {
		wcn36xx_err("Sending hal_end_scan failed\n");
		goto out;
	}
	ret = wcn36xx_smd_rsp_status_check(wcn->hal_buf, wcn->hal_rsp_len);
	if (ret) {
		wcn36xx_err("hal_end_scan response failed err=%d\n", ret);
		goto out;
	}
out:
	mutex_unlock(&wcn->hal_mutex);
	return ret;
}

int wcn36xx_smd_finish_scan(struct wcn36xx *wcn,
			    enum wcn36xx_hal_sys_mode mode,
			    struct ieee80211_vif *vif)
{
	struct wcn36xx_vif *vif_priv = wcn36xx_vif_to_priv(vif);
	struct wcn36xx_hal_finish_scan_req_msg msg_body;
	int ret;

	mutex_lock(&wcn->hal_mutex);
	INIT_HAL_MSG(msg_body, WCN36XX_HAL_FINISH_SCAN_REQ);

	msg_body.mode = mode;
	msg_body.oper_channel = WCN36XX_HW_CHANNEL(wcn);
	if (vif_priv->bss_index != WCN36XX_HAL_BSS_INVALID_IDX) {
		/* Notify BSSID with null data packet */
		msg_body.notify = 1;
		msg_body.frame_type = 2;
		msg_body.scan_entry.bss_index[0] = vif_priv->bss_index;
		msg_body.scan_entry.active_bss_count = 1;
	}

	PREPARE_HAL_BUF(wcn->hal_buf, msg_body);

	wcn36xx_dbg(WCN36XX_DBG_HAL, "hal finish scan mode %d\n",
		    msg_body.mode);

	ret = wcn36xx_smd_send_and_wait(wcn, msg_body.header.len);
	if (ret) {
		wcn36xx_err("Sending hal_finish_scan failed\n");
		goto out;
	}
	ret = wcn36xx_smd_rsp_status_check(wcn->hal_buf, wcn->hal_rsp_len);
	if (ret) {
		wcn36xx_err("hal_finish_scan response failed err=%d\n", ret);
		goto out;
	}
out:
	mutex_unlock(&wcn->hal_mutex);
	return ret;
}

int wcn36xx_smd_start_hw_scan(struct wcn36xx *wcn, struct ieee80211_vif *vif,
			      struct cfg80211_scan_request *req)
{
	struct wcn36xx_vif *vif_priv = wcn36xx_vif_to_priv(vif);
	struct wcn36xx_hal_start_scan_offload_req_msg *msg_body;
	int ret, i;

	if (req->ie_len > WCN36XX_MAX_SCAN_IE_LEN)
		return -EINVAL;

	mutex_lock(&wcn->hal_mutex);
	msg_body = kzalloc(sizeof(*msg_body), GFP_KERNEL);
	if (!msg_body) {
		ret = -ENOMEM;
		goto out;
	}

	INIT_HAL_MSG((*msg_body), WCN36XX_HAL_START_SCAN_OFFLOAD_REQ);

	msg_body->scan_type = WCN36XX_HAL_SCAN_TYPE_ACTIVE;
	msg_body->min_ch_time = 30;
	msg_body->max_ch_time = 100;
	msg_body->scan_hidden = 1;
	memcpy(msg_body->mac, vif->addr, ETH_ALEN);
	msg_body->bss_type = vif_priv->bss_type;
	msg_body->p2p_search = vif->p2p;

	msg_body->num_ssid = min_t(u8, req->n_ssids, ARRAY_SIZE(msg_body->ssids));
	for (i = 0; i < msg_body->num_ssid; i++) {
		msg_body->ssids[i].length = min_t(u8, req->ssids[i].ssid_len,
						sizeof(msg_body->ssids[i].ssid));
		memcpy(msg_body->ssids[i].ssid, req->ssids[i].ssid,
		       msg_body->ssids[i].length);
	}

	msg_body->num_channel = min_t(u8, req->n_channels,
				     sizeof(msg_body->channels));
	for (i = 0; i < msg_body->num_channel; i++) {
		msg_body->channels[i] =
			HW_VALUE_CHANNEL(req->channels[i]->hw_value);
	}

	msg_body->header.len -= WCN36XX_MAX_SCAN_IE_LEN;

	if (req->ie_len > 0) {
		msg_body->ie_len = req->ie_len;
		msg_body->header.len += req->ie_len;
		memcpy(msg_body->ie, req->ie, req->ie_len);
	}

	PREPARE_HAL_BUF(wcn->hal_buf, (*msg_body));

	wcn36xx_dbg(WCN36XX_DBG_HAL,
		    "hal start hw-scan (channels: %u; ssids: %u; p2p: %s)\n",
		    msg_body->num_channel, msg_body->num_ssid,
		    msg_body->p2p_search ? "yes" : "no");

	ret = wcn36xx_smd_send_and_wait(wcn, msg_body->header.len);
	if (ret) {
		wcn36xx_err("Sending hal_start_scan_offload failed\n");
		goto out;
	}
	ret = wcn36xx_smd_rsp_status_check(wcn->hal_buf, wcn->hal_rsp_len);
	if (ret) {
		wcn36xx_err("hal_start_scan_offload response failed err=%d\n",
			    ret);
		goto out;
	}
out:
	kfree(msg_body);
	mutex_unlock(&wcn->hal_mutex);
	return ret;
}

int wcn36xx_smd_stop_hw_scan(struct wcn36xx *wcn)
{
	struct wcn36xx_hal_stop_scan_offload_req_msg msg_body;
	int ret;

	mutex_lock(&wcn->hal_mutex);
	INIT_HAL_MSG(msg_body, WCN36XX_HAL_STOP_SCAN_OFFLOAD_REQ);
	PREPARE_HAL_BUF(wcn->hal_buf, msg_body);

	wcn36xx_dbg(WCN36XX_DBG_HAL, "hal stop hw-scan\n");

	ret = wcn36xx_smd_send_and_wait(wcn, msg_body.header.len);
	if (ret) {
		wcn36xx_err("Sending hal_stop_scan_offload failed\n");
		goto out;
	}
	ret = wcn36xx_smd_rsp_status_check(wcn->hal_buf, wcn->hal_rsp_len);
	if (ret) {
		wcn36xx_err("hal_stop_scan_offload response failed err=%d\n",
			    ret);
		goto out;
	}
out:
	mutex_unlock(&wcn->hal_mutex);
	return ret;
}

static int wcn36xx_smd_switch_channel_rsp(void *buf, size_t len)
{
	struct wcn36xx_hal_switch_channel_rsp_msg *rsp;
	int ret;

	ret = wcn36xx_smd_rsp_status_check(buf, len);
	if (ret)
		return ret;
	rsp = (struct wcn36xx_hal_switch_channel_rsp_msg *)buf;
	wcn36xx_dbg(WCN36XX_DBG_HAL, "channel switched to: %d, status: %d\n",
		    rsp->channel_number, rsp->status);
	return ret;
}

int wcn36xx_smd_switch_channel(struct wcn36xx *wcn,
			       struct ieee80211_vif *vif, int ch)
{
	struct wcn36xx_hal_switch_channel_req_msg msg_body;
	int ret;

	mutex_lock(&wcn->hal_mutex);
	INIT_HAL_MSG(msg_body, WCN36XX_HAL_CH_SWITCH_REQ);

	msg_body.channel_number = (u8)ch;
	msg_body.tx_mgmt_power = 0xbf;
	msg_body.max_tx_power = 0xbf;
	memcpy(msg_body.self_sta_mac_addr, vif->addr, ETH_ALEN);

	PREPARE_HAL_BUF(wcn->hal_buf, msg_body);

	ret = wcn36xx_smd_send_and_wait(wcn, msg_body.header.len);
	if (ret) {
		wcn36xx_err("Sending hal_switch_channel failed\n");
		goto out;
	}
	ret = wcn36xx_smd_switch_channel_rsp(wcn->hal_buf, wcn->hal_rsp_len);
	if (ret) {
		wcn36xx_err("hal_switch_channel response failed err=%d\n", ret);
		goto out;
	}
out:
	mutex_unlock(&wcn->hal_mutex);
	return ret;
}

static int wcn36xx_smd_process_ptt_msg_rsp(void *buf, size_t len,
					   void **p_ptt_rsp_msg)
{
	struct wcn36xx_hal_process_ptt_msg_rsp_msg *rsp;
	int ret;

	ret = wcn36xx_smd_rsp_status_check(buf, len);
	if (ret)
		return ret;

	rsp = (struct wcn36xx_hal_process_ptt_msg_rsp_msg *)buf;

	wcn36xx_dbg(WCN36XX_DBG_HAL, "process ptt msg responded with length %d\n",
		    rsp->header.len);
	wcn36xx_dbg_dump(WCN36XX_DBG_HAL_DUMP, "HAL_PTT_MSG_RSP:", rsp->ptt_msg,
			 rsp->header.len - sizeof(rsp->ptt_msg_resp_status));

	if (rsp->header.len > 0) {
		*p_ptt_rsp_msg = kmemdup(rsp->ptt_msg, rsp->header.len,
					 GFP_ATOMIC);
		if (!*p_ptt_rsp_msg)
			return -ENOMEM;
	}
	return ret;
}

int wcn36xx_smd_process_ptt_msg(struct wcn36xx *wcn,
				struct ieee80211_vif *vif, void *ptt_msg, size_t len,
		void **ptt_rsp_msg)
{
	struct wcn36xx_hal_process_ptt_msg_req_msg *p_msg_body;
	int ret;

	mutex_lock(&wcn->hal_mutex);
	p_msg_body = kmalloc(
		sizeof(struct wcn36xx_hal_process_ptt_msg_req_msg) + len,
		GFP_ATOMIC);
	if (!p_msg_body) {
		ret = -ENOMEM;
		goto out_nomem;
	}
	INIT_HAL_PTT_MSG(p_msg_body, len);

	memcpy(&p_msg_body->ptt_msg, ptt_msg, len);

	PREPARE_HAL_PTT_MSG_BUF(wcn->hal_buf, p_msg_body);

	ret = wcn36xx_smd_send_and_wait(wcn, p_msg_body->header.len);
	if (ret) {
		wcn36xx_err("Sending hal_process_ptt_msg failed\n");
		goto out;
	}
	ret = wcn36xx_smd_process_ptt_msg_rsp(wcn->hal_buf, wcn->hal_rsp_len,
					      ptt_rsp_msg);
	if (ret) {
		wcn36xx_err("process_ptt_msg response failed err=%d\n", ret);
		goto out;
	}
out:
	kfree(p_msg_body);
out_nomem:
	mutex_unlock(&wcn->hal_mutex);
	return ret;
}

static int wcn36xx_smd_update_scan_params_rsp(void *buf, size_t len)
{
	struct wcn36xx_hal_update_scan_params_resp *rsp;

	rsp = (struct wcn36xx_hal_update_scan_params_resp *)buf;

	/* Remove the PNO version bit */
	rsp->status &= (~(WCN36XX_FW_MSG_PNO_VERSION_MASK));

	if (WCN36XX_FW_MSG_RESULT_SUCCESS != rsp->status) {
		wcn36xx_warn("error response from update scan\n");
		return rsp->status;
	}

	return 0;
}

int wcn36xx_smd_update_scan_params(struct wcn36xx *wcn,
				   u8 *channels, size_t channel_count)
{
	struct wcn36xx_hal_update_scan_params_req_ex msg_body;
	int ret;

	mutex_lock(&wcn->hal_mutex);
	INIT_HAL_MSG(msg_body, WCN36XX_HAL_UPDATE_SCAN_PARAM_REQ);

	msg_body.dot11d_enabled	= false;
	msg_body.dot11d_resolved = true;

	msg_body.channel_count = channel_count;
	memcpy(msg_body.channels, channels, channel_count);
	msg_body.active_min_ch_time = 60;
	msg_body.active_max_ch_time = 120;
	msg_body.passive_min_ch_time = 60;
	msg_body.passive_max_ch_time = 110;
	msg_body.state = PHY_SINGLE_CHANNEL_CENTERED;

	PREPARE_HAL_BUF(wcn->hal_buf, msg_body);

	wcn36xx_dbg(WCN36XX_DBG_HAL,
		    "hal update scan params channel_count %d\n",
		    msg_body.channel_count);

	ret = wcn36xx_smd_send_and_wait(wcn, msg_body.header.len);
	if (ret) {
		wcn36xx_err("Sending hal_update_scan_params failed\n");
		goto out;
	}
	ret = wcn36xx_smd_update_scan_params_rsp(wcn->hal_buf,
						 wcn->hal_rsp_len);
	if (ret) {
		wcn36xx_err("hal_update_scan_params response failed err=%d\n",
			    ret);
		goto out;
	}
out:
	mutex_unlock(&wcn->hal_mutex);
	return ret;
}

static int wcn36xx_smd_add_sta_self_rsp(struct wcn36xx *wcn,
					struct ieee80211_vif *vif,
					void *buf,
					size_t len)
{
	struct wcn36xx_hal_add_sta_self_rsp_msg *rsp;
	struct wcn36xx_vif *vif_priv = wcn36xx_vif_to_priv(vif);

	if (len < sizeof(*rsp))
		return -EINVAL;

	rsp = (struct wcn36xx_hal_add_sta_self_rsp_msg *)buf;

	if (rsp->status != WCN36XX_FW_MSG_RESULT_SUCCESS) {
		wcn36xx_warn("hal add sta self failure: %d\n",
			     rsp->status);
		return rsp->status;
	}

	wcn36xx_dbg(WCN36XX_DBG_HAL,
		    "hal add sta self status %d self_sta_index %d dpu_index %d\n",
		    rsp->status, rsp->self_sta_index, rsp->dpu_index);

	vif_priv->self_sta_index = rsp->self_sta_index;
	vif_priv->self_dpu_desc_index = rsp->dpu_index;

	return 0;
}

int wcn36xx_smd_add_sta_self(struct wcn36xx *wcn, struct ieee80211_vif *vif)
{
	struct wcn36xx_hal_add_sta_self_req msg_body;
	int ret;

	mutex_lock(&wcn->hal_mutex);
	INIT_HAL_MSG(msg_body, WCN36XX_HAL_ADD_STA_SELF_REQ);

	memcpy(&msg_body.self_addr, vif->addr, ETH_ALEN);

	PREPARE_HAL_BUF(wcn->hal_buf, msg_body);

	wcn36xx_dbg(WCN36XX_DBG_HAL,
		    "hal add sta self self_addr %pM status %d\n",
		    msg_body.self_addr, msg_body.status);

	ret = wcn36xx_smd_send_and_wait(wcn, msg_body.header.len);
	if (ret) {
		wcn36xx_err("Sending hal_add_sta_self failed\n");
		goto out;
	}
	ret = wcn36xx_smd_add_sta_self_rsp(wcn,
					   vif,
					   wcn->hal_buf,
					   wcn->hal_rsp_len);
	if (ret) {
		wcn36xx_err("hal_add_sta_self response failed err=%d\n", ret);
		goto out;
	}
out:
	mutex_unlock(&wcn->hal_mutex);
	return ret;
}

int wcn36xx_smd_delete_sta_self(struct wcn36xx *wcn, u8 *addr)
{
	struct wcn36xx_hal_del_sta_self_req_msg msg_body;
	int ret;

	mutex_lock(&wcn->hal_mutex);
	INIT_HAL_MSG(msg_body, WCN36XX_HAL_DEL_STA_SELF_REQ);

	memcpy(&msg_body.self_addr, addr, ETH_ALEN);

	PREPARE_HAL_BUF(wcn->hal_buf, msg_body);

	ret = wcn36xx_smd_send_and_wait(wcn, msg_body.header.len);
	if (ret) {
		wcn36xx_err("Sending hal_delete_sta_self failed\n");
		goto out;
	}
	ret = wcn36xx_smd_rsp_status_check(wcn->hal_buf, wcn->hal_rsp_len);
	if (ret) {
		wcn36xx_err("hal_delete_sta_self response failed err=%d\n",
			    ret);
		goto out;
	}
out:
	mutex_unlock(&wcn->hal_mutex);
	return ret;
}

int wcn36xx_smd_delete_sta(struct wcn36xx *wcn, u8 sta_index)
{
	struct wcn36xx_hal_delete_sta_req_msg msg_body;
	int ret;

	mutex_lock(&wcn->hal_mutex);
	INIT_HAL_MSG(msg_body, WCN36XX_HAL_DELETE_STA_REQ);

	msg_body.sta_index = sta_index;

	PREPARE_HAL_BUF(wcn->hal_buf, msg_body);

	wcn36xx_dbg(WCN36XX_DBG_HAL,
		    "hal delete sta sta_index %d\n",
		    msg_body.sta_index);

	ret = wcn36xx_smd_send_and_wait(wcn, msg_body.header.len);
	if (ret) {
		wcn36xx_err("Sending hal_delete_sta failed\n");
		goto out;
	}
	ret = wcn36xx_smd_rsp_status_check(wcn->hal_buf, wcn->hal_rsp_len);
	if (ret) {
		wcn36xx_err("hal_delete_sta response failed err=%d\n", ret);
		goto out;
	}
out:
	mutex_unlock(&wcn->hal_mutex);
	return ret;
}

static int wcn36xx_smd_join_rsp(void *buf, size_t len)
{
	struct wcn36xx_hal_join_rsp_msg *rsp;

	if (wcn36xx_smd_rsp_status_check(buf, len))
		return -EIO;

	rsp = (struct wcn36xx_hal_join_rsp_msg *)buf;

	wcn36xx_dbg(WCN36XX_DBG_HAL,
		    "hal rsp join status %d tx_mgmt_power %d\n",
		    rsp->status, rsp->tx_mgmt_power);

	return 0;
}

int wcn36xx_smd_join(struct wcn36xx *wcn, const u8 *bssid, u8 *vif, u8 ch)
{
	struct wcn36xx_hal_join_req_msg msg_body;
	int ret;

	mutex_lock(&wcn->hal_mutex);
	INIT_HAL_MSG(msg_body, WCN36XX_HAL_JOIN_REQ);

	memcpy(&msg_body.bssid, bssid, ETH_ALEN);
	memcpy(&msg_body.self_sta_mac_addr, vif, ETH_ALEN);
	msg_body.channel = ch;

	if (conf_is_ht40_minus(&wcn->hw->conf))
		msg_body.secondary_channel_offset =
			PHY_DOUBLE_CHANNEL_HIGH_PRIMARY;
	else if (conf_is_ht40_plus(&wcn->hw->conf))
		msg_body.secondary_channel_offset =
			PHY_DOUBLE_CHANNEL_LOW_PRIMARY;
	else
		msg_body.secondary_channel_offset =
			PHY_SINGLE_CHANNEL_CENTERED;

	msg_body.link_state = WCN36XX_HAL_LINK_PREASSOC_STATE;

	msg_body.max_tx_power = 0xbf;
	PREPARE_HAL_BUF(wcn->hal_buf, msg_body);

	wcn36xx_dbg(WCN36XX_DBG_HAL,
		    "hal join req bssid %pM self_sta_mac_addr %pM channel %d link_state %d\n",
		    msg_body.bssid, msg_body.self_sta_mac_addr,
		    msg_body.channel, msg_body.link_state);

	ret = wcn36xx_smd_send_and_wait(wcn, msg_body.header.len);
	if (ret) {
		wcn36xx_err("Sending hal_join failed\n");
		goto out;
	}
	ret = wcn36xx_smd_join_rsp(wcn->hal_buf, wcn->hal_rsp_len);
	if (ret) {
		wcn36xx_err("hal_join response failed err=%d\n", ret);
		goto out;
	}
out:
	mutex_unlock(&wcn->hal_mutex);
	return ret;
}

int wcn36xx_smd_set_link_st(struct wcn36xx *wcn, const u8 *bssid,
			    const u8 *sta_mac,
			    enum wcn36xx_hal_link_state state)
{
	struct wcn36xx_hal_set_link_state_req_msg msg_body;
	int ret;

	mutex_lock(&wcn->hal_mutex);
	INIT_HAL_MSG(msg_body, WCN36XX_HAL_SET_LINK_ST_REQ);

	memcpy(&msg_body.bssid, bssid, ETH_ALEN);
	memcpy(&msg_body.self_mac_addr, sta_mac, ETH_ALEN);
	msg_body.state = state;

	PREPARE_HAL_BUF(wcn->hal_buf, msg_body);

	wcn36xx_dbg(WCN36XX_DBG_HAL,
		    "hal set link state bssid %pM self_mac_addr %pM state %d\n",
		    msg_body.bssid, msg_body.self_mac_addr, msg_body.state);

	ret = wcn36xx_smd_send_and_wait(wcn, msg_body.header.len);
	if (ret) {
		wcn36xx_err("Sending hal_set_link_st failed\n");
		goto out;
	}
	ret = wcn36xx_smd_rsp_status_check(wcn->hal_buf, wcn->hal_rsp_len);
	if (ret) {
		wcn36xx_err("hal_set_link_st response failed err=%d\n", ret);
		goto out;
	}
out:
	mutex_unlock(&wcn->hal_mutex);
	return ret;
}

static void wcn36xx_smd_convert_sta_to_v1(struct wcn36xx *wcn,
			const struct wcn36xx_hal_config_sta_params *orig,
			struct wcn36xx_hal_config_sta_params_v1 *v1)
{
	/* convert orig to v1 format */
	memcpy(&v1->bssid, orig->bssid, ETH_ALEN);
	memcpy(&v1->mac, orig->mac, ETH_ALEN);
	v1->aid = orig->aid;
	v1->type = orig->type;
	v1->short_preamble_supported = orig->short_preamble_supported;
	v1->listen_interval = orig->listen_interval;
	v1->wmm_enabled = orig->wmm_enabled;
	v1->ht_capable = orig->ht_capable;
	v1->tx_channel_width_set = orig->tx_channel_width_set;
	v1->rifs_mode = orig->rifs_mode;
	v1->lsig_txop_protection = orig->lsig_txop_protection;
	v1->max_ampdu_size = orig->max_ampdu_size;
	v1->max_ampdu_density = orig->max_ampdu_density;
	v1->sgi_40mhz = orig->sgi_40mhz;
	v1->sgi_20Mhz = orig->sgi_20Mhz;
	v1->rmf = orig->rmf;
	v1->encrypt_type = orig->encrypt_type;
	v1->action = orig->action;
	v1->uapsd = orig->uapsd;
	v1->max_sp_len = orig->max_sp_len;
	v1->green_field_capable = orig->green_field_capable;
	v1->mimo_ps = orig->mimo_ps;
	v1->delayed_ba_support = orig->delayed_ba_support;
	v1->max_ampdu_duration = orig->max_ampdu_duration;
	v1->dsss_cck_mode_40mhz = orig->dsss_cck_mode_40mhz;
	memcpy(&v1->supported_rates, &orig->supported_rates,
	       sizeof(orig->supported_rates));
	v1->sta_index = orig->sta_index;
	v1->bssid_index = orig->bssid_index;
	v1->p2p = orig->p2p;
}

static void
wcn36xx_smd_set_sta_params_v1(struct wcn36xx *wcn,
			      struct ieee80211_vif *vif,
			      struct ieee80211_sta *sta,
			      struct wcn36xx_hal_config_sta_params_v1 *sta_par)
{
	struct wcn36xx_sta *sta_priv = NULL;
	struct wcn36xx_hal_config_sta_params sta_par_v0;

	wcn36xx_smd_set_sta_params(wcn, vif, sta, &sta_par_v0);
	wcn36xx_smd_convert_sta_to_v1(wcn, &sta_par_v0, sta_par);

	if (sta) {
		sta_priv = wcn36xx_sta_to_priv(sta);
		wcn36xx_smd_set_sta_vht_params(wcn, sta, sta_par);
		wcn36xx_smd_set_sta_ht_ldpc_params(sta, sta_par);
		memcpy(&sta_par->supported_rates, &sta_priv->supported_rates,
		       sizeof(sta_par->supported_rates));
	} else {
		wcn36xx_set_default_rates_v1(&sta_par->supported_rates);
		wcn36xx_smd_set_sta_default_vht_params(wcn, sta_par);
		wcn36xx_smd_set_sta_default_ht_ldpc_params(wcn, sta_par);
	}
}

static int wcn36xx_smd_config_sta_rsp(struct wcn36xx *wcn,
				      struct ieee80211_sta *sta,
				      void *buf,
				      size_t len)
{
	struct wcn36xx_hal_config_sta_rsp_msg *rsp;
	struct config_sta_rsp_params *params;
	struct wcn36xx_sta *sta_priv = wcn36xx_sta_to_priv(sta);

	if (len < sizeof(*rsp))
		return -EINVAL;

	rsp = (struct wcn36xx_hal_config_sta_rsp_msg *)buf;
	params = &rsp->params;

	if (params->status != WCN36XX_FW_MSG_RESULT_SUCCESS) {
		wcn36xx_warn("hal config sta response failure: %d\n",
			     params->status);
		return -EIO;
	}

	sta_priv->sta_index = params->sta_index;
	sta_priv->dpu_desc_index = params->dpu_index;
	sta_priv->ucast_dpu_sign = params->uc_ucast_sig;

	wcn36xx_dbg(WCN36XX_DBG_HAL,
		    "hal config sta rsp status %d sta_index %d bssid_index %d uc_ucast_sig %d p2p %d\n",
		    params->status, params->sta_index, params->bssid_index,
		    params->uc_ucast_sig, params->p2p);

	return 0;
}

static int wcn36xx_smd_config_sta_v1(struct wcn36xx *wcn,
				     struct ieee80211_vif *vif,
				     struct ieee80211_sta *sta)
{
	struct wcn36xx_hal_config_sta_req_msg_v1 msg_body;
	struct wcn36xx_hal_config_sta_params_v1 *sta_params;

	if (wcn->rf_id == RF_IRIS_WCN3680) {
		INIT_HAL_MSG_V1(msg_body, WCN36XX_HAL_CONFIG_STA_REQ);
	} else {
		INIT_HAL_MSG(msg_body, WCN36XX_HAL_CONFIG_STA_REQ);
		msg_body.header.len -= WCN36XX_DIFF_STA_PARAMS_V1_NOVHT;
	}

	sta_params = &msg_body.sta_params;

	wcn36xx_smd_set_sta_params_v1(wcn, vif, sta, sta_params);

	PREPARE_HAL_BUF(wcn->hal_buf, msg_body);

	wcn36xx_dbg(WCN36XX_DBG_HAL,
		    "hal config sta v1 action %d sta_index %d bssid_index %d bssid %pM type %d mac %pM aid %d\n",
		    sta_params->action, sta_params->sta_index, sta_params->bssid_index,
		    sta_params->bssid, sta_params->type, sta_params->mac, sta_params->aid);

	return wcn36xx_smd_send_and_wait(wcn, msg_body.header.len);
}

static int wcn36xx_smd_config_sta_v0(struct wcn36xx *wcn,
				     struct ieee80211_vif *vif,
				     struct ieee80211_sta *sta)
{
	struct wcn36xx_hal_config_sta_req_msg msg;
	struct wcn36xx_hal_config_sta_params *sta_params;

	INIT_HAL_MSG(msg, WCN36XX_HAL_CONFIG_STA_REQ);

	sta_params = &msg.sta_params;

	wcn36xx_smd_set_sta_params(wcn, vif, sta, sta_params);

	PREPARE_HAL_BUF(wcn->hal_buf, msg);

	wcn36xx_dbg(WCN36XX_DBG_HAL,
		    "hal config sta action %d sta_index %d bssid_index %d bssid %pM type %d mac %pM aid %d\n",
		    sta_params->action, sta_params->sta_index,
		    sta_params->bssid_index, sta_params->bssid,
		    sta_params->type, sta_params->mac, sta_params->aid);

	return wcn36xx_smd_send_and_wait(wcn, msg.header.len);
}

int wcn36xx_smd_config_sta(struct wcn36xx *wcn, struct ieee80211_vif *vif,
			   struct ieee80211_sta *sta)
{
	int ret;

	mutex_lock(&wcn->hal_mutex);

	if (!wcn36xx_is_fw_version(wcn, 1, 2, 2, 24))
		ret = wcn36xx_smd_config_sta_v1(wcn, vif, sta);
	else
		ret = wcn36xx_smd_config_sta_v0(wcn, vif, sta);

	if (ret) {
		wcn36xx_err("Sending hal_config_sta failed\n");
		goto out;
	}
	ret = wcn36xx_smd_config_sta_rsp(wcn,
					 sta,
					 wcn->hal_buf,
					 wcn->hal_rsp_len);
	if (ret) {
		wcn36xx_err("hal_config_sta response failed err=%d\n", ret);
		goto out;
	}
out:
	mutex_unlock(&wcn->hal_mutex);
	return ret;
}

static void wcn36xx_smd_set_bss_params(struct wcn36xx *wcn,
				       struct ieee80211_vif *vif,
				       struct ieee80211_sta *sta,
				       const u8 *bssid,
				       bool update,
				       struct wcn36xx_hal_config_bss_params *bss)
{
	struct wcn36xx_vif *vif_priv = wcn36xx_vif_to_priv(vif);

	WARN_ON(is_zero_ether_addr(bssid));

	memcpy(&bss->bssid, bssid, ETH_ALEN);

	memcpy(bss->self_mac_addr, vif->addr, ETH_ALEN);

	if (vif->type == NL80211_IFTYPE_STATION) {
		bss->bss_type = WCN36XX_HAL_INFRASTRUCTURE_MODE;

		/* STA */
		bss->oper_mode = 1;
		bss->wcn36xx_hal_persona = WCN36XX_HAL_STA_MODE;
	} else if (vif->type == NL80211_IFTYPE_AP ||
		   vif->type == NL80211_IFTYPE_MESH_POINT) {
		bss->bss_type = WCN36XX_HAL_INFRA_AP_MODE;

		/* AP */
		bss->oper_mode = 0;
		bss->wcn36xx_hal_persona = WCN36XX_HAL_STA_SAP_MODE;
	} else if (vif->type == NL80211_IFTYPE_ADHOC) {
		bss->bss_type = WCN36XX_HAL_IBSS_MODE;

		/* STA */
		bss->oper_mode = 1;
	} else {
		wcn36xx_warn("Unknown type for bss config: %d\n", vif->type);
	}

	if (vif->type == NL80211_IFTYPE_STATION)
		wcn36xx_smd_set_bss_nw_type(wcn, sta, bss);
	else
		bss->nw_type = WCN36XX_HAL_11N_NW_TYPE;

	bss->short_slot_time_supported = vif->bss_conf.use_short_slot;
	bss->lla_coexist = 0;
	bss->llb_coexist = 0;
	bss->llg_coexist = 0;
	bss->rifs_mode = 0;
	bss->beacon_interval = vif->bss_conf.beacon_int;
	bss->dtim_period = vif_priv->dtim_period;

	wcn36xx_smd_set_bss_ht_params(vif, sta, bss);

	bss->oper_channel = WCN36XX_HW_CHANNEL(wcn);

	if (conf_is_ht40_minus(&wcn->hw->conf))
		bss->ext_channel = IEEE80211_HT_PARAM_CHA_SEC_BELOW;
	else if (conf_is_ht40_plus(&wcn->hw->conf))
		bss->ext_channel = IEEE80211_HT_PARAM_CHA_SEC_ABOVE;
	else
		bss->ext_channel = IEEE80211_HT_PARAM_CHA_SEC_NONE;

	bss->reserved = 0;

	/* wcn->ssid is only valid in AP and IBSS mode */
	bss->ssid.length = vif_priv->ssid.length;
	memcpy(bss->ssid.ssid, vif_priv->ssid.ssid, vif_priv->ssid.length);

	bss->obss_prot_enabled = 0;
	bss->rmf = 0;
	bss->max_probe_resp_retry_limit = 0;
	bss->hidden_ssid = vif->bss_conf.hidden_ssid;
	bss->proxy_probe_resp = 0;
	bss->edca_params_valid = 0;

	/* FIXME: set acbe, acbk, acvi and acvo */

	bss->ext_set_sta_key_param_valid = 0;

	/* FIXME: set ext_set_sta_key_param */

	bss->spectrum_mgt_enable = 0;
	bss->tx_mgmt_power = 0;
	bss->max_tx_power = WCN36XX_MAX_POWER(wcn);
	bss->action = update;

	vif_priv->bss_type = bss->bss_type;
}

static int wcn36xx_smd_config_bss_v1(struct wcn36xx *wcn,
				     struct ieee80211_vif *vif,
				     struct ieee80211_sta *sta_80211,
				     const u8 *bssid,
				     bool update)
{
	struct wcn36xx_hal_config_bss_req_msg_v1 *msg_body;
	struct wcn36xx_hal_config_bss_params_v1 *bss;
	struct wcn36xx_hal_config_bss_params bss_v0;
	struct wcn36xx_hal_config_sta_params_v1 *sta;
	struct cfg80211_chan_def *chandef;
	int ret;

	msg_body = kzalloc(sizeof(*msg_body), GFP_KERNEL);
	if (!msg_body)
		return -ENOMEM;

	if (wcn->rf_id == RF_IRIS_WCN3680) {
		INIT_HAL_MSG_V1((*msg_body), WCN36XX_HAL_CONFIG_BSS_REQ);
	} else {
		INIT_HAL_MSG((*msg_body), WCN36XX_HAL_CONFIG_BSS_REQ);
		msg_body->header.len -= WCN36XX_DIFF_BSS_PARAMS_V1_NOVHT;
	}

	bss = &msg_body->bss_params;
	sta = &bss->sta;

	memset(&bss_v0, 0x00, sizeof(bss_v0));
	wcn36xx_smd_set_bss_params(wcn, vif, sta_80211, bssid, update, &bss_v0);
	wcn36xx_smd_set_sta_params_v1(wcn, vif, sta_80211, sta);

	/* convert orig to v1 */
	memcpy(bss->bssid, &bss_v0.bssid, ETH_ALEN);
	memcpy(bss->self_mac_addr, &bss_v0.self_mac_addr, ETH_ALEN);

	bss->bss_type = bss_v0.bss_type;
	bss->oper_mode = bss_v0.oper_mode;
	bss->nw_type = bss_v0.nw_type;

	bss->short_slot_time_supported =
		bss_v0.short_slot_time_supported;
	bss->lla_coexist = bss_v0.lla_coexist;
	bss->llb_coexist = bss_v0.llb_coexist;
	bss->llg_coexist = bss_v0.llg_coexist;
	bss->ht20_coexist = bss_v0.ht20_coexist;
	bss->lln_non_gf_coexist = bss_v0.lln_non_gf_coexist;

	bss->lsig_tx_op_protection_full_support =
		bss_v0.lsig_tx_op_protection_full_support;
	bss->rifs_mode = bss_v0.rifs_mode;
	bss->beacon_interval = bss_v0.beacon_interval;
	bss->dtim_period = bss_v0.dtim_period;
	bss->tx_channel_width_set = bss_v0.tx_channel_width_set;
	bss->oper_channel = bss_v0.oper_channel;

	if (wcn->hw->conf.chandef.width == NL80211_CHAN_WIDTH_80) {
		chandef = &wcn->hw->conf.chandef;
		bss->ext_channel = HW_VALUE_PHY(chandef->chan->hw_value);
	} else {
		bss->ext_channel = bss_v0.ext_channel;
	}

	bss->reserved = bss_v0.reserved;

	memcpy(&bss->ssid, &bss_v0.ssid,
	       sizeof(bss_v0.ssid));

	bss->action = bss_v0.action;
	bss->rateset = bss_v0.rateset;
	bss->ht = bss_v0.ht;
	bss->obss_prot_enabled = bss_v0.obss_prot_enabled;
	bss->rmf = bss_v0.rmf;
	bss->ht_oper_mode = bss_v0.ht_oper_mode;
	bss->dual_cts_protection = bss_v0.dual_cts_protection;

	bss->max_probe_resp_retry_limit =
		bss_v0.max_probe_resp_retry_limit;
	bss->hidden_ssid = bss_v0.hidden_ssid;
	bss->proxy_probe_resp =	bss_v0.proxy_probe_resp;
	bss->edca_params_valid = bss_v0.edca_params_valid;

	memcpy(&bss->acbe, &bss_v0.acbe,
	       sizeof(bss_v0.acbe));
	memcpy(&bss->acbk, &bss_v0.acbk,
	       sizeof(bss_v0.acbk));
	memcpy(&bss->acvi, &bss_v0.acvi,
	       sizeof(bss_v0.acvi));
	memcpy(&bss->acvo, &bss_v0.acvo,
	       sizeof(bss_v0.acvo));

	bss->ext_set_sta_key_param_valid =
		bss_v0.ext_set_sta_key_param_valid;

	memcpy(&bss->ext_set_sta_key_param,
	       &bss_v0.ext_set_sta_key_param,
	       sizeof(bss_v0.acvo));

	bss->wcn36xx_hal_persona = bss_v0.wcn36xx_hal_persona;
	bss->spectrum_mgt_enable = bss_v0.spectrum_mgt_enable;
	bss->tx_mgmt_power = bss_v0.tx_mgmt_power;
	bss->max_tx_power = bss_v0.max_tx_power;

	wcn36xx_smd_set_bss_vht_params(vif, sta_80211, bss);

	PREPARE_HAL_BUF(wcn->hal_buf, (*msg_body));

	wcn36xx_dbg(WCN36XX_DBG_HAL,
		    "hal config bss v1 bssid %pM self_mac_addr %pM bss_type %d oper_mode %d nw_type %d\n",
		    bss->bssid, bss->self_mac_addr, bss->bss_type,
		    bss->oper_mode, bss->nw_type);

	wcn36xx_dbg(WCN36XX_DBG_HAL,
		    "- sta bssid %pM action %d sta_index %d bssid_index %d aid %d type %d mac %pM\n",
		    sta->bssid, sta->action, sta->sta_index,
		    sta->bssid_index, sta->aid, sta->type, sta->mac);

	ret = wcn36xx_smd_send_and_wait(wcn, msg_body->header.len);
	kfree(msg_body);

	return ret;
}

static int wcn36xx_smd_config_bss_v0(struct wcn36xx *wcn,
				     struct ieee80211_vif *vif,
				     struct ieee80211_sta *sta,
				     const u8 *bssid,
				     bool update)
{
	struct wcn36xx_hal_config_bss_req_msg *msg;
	struct wcn36xx_hal_config_bss_params *bss;
	struct wcn36xx_hal_config_sta_params *sta_params;
	int ret;

	msg = kzalloc(sizeof(*msg), GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	INIT_HAL_MSG((*msg), WCN36XX_HAL_CONFIG_BSS_REQ);

	bss = &msg->bss_params;
	sta_params = &bss->sta;

	wcn36xx_smd_set_bss_params(wcn, vif, sta, bssid, update, bss);
	wcn36xx_smd_set_sta_params(wcn, vif, sta, sta_params);

	PREPARE_HAL_BUF(wcn->hal_buf, (*msg));

	wcn36xx_dbg(WCN36XX_DBG_HAL,
		    "hal config bss bssid %pM self_mac_addr %pM bss_type %d oper_mode %d nw_type %d\n",
		    bss->bssid, bss->self_mac_addr, bss->bss_type,
		    bss->oper_mode, bss->nw_type);

	wcn36xx_dbg(WCN36XX_DBG_HAL,
		    "- sta bssid %pM action %d sta_index %d bssid_index %d aid %d type %d mac %pM\n",
		    sta_params->bssid, sta_params->action,
		    sta_params->sta_index, sta_params->bssid_index,
		    sta_params->aid, sta_params->type,
		    sta_params->mac);

	ret = wcn36xx_smd_send_and_wait(wcn, msg->header.len);
	kfree(msg);

	return ret;
}

static int wcn36xx_smd_config_bss_rsp(struct wcn36xx *wcn,
				      struct ieee80211_vif *vif,
				      struct ieee80211_sta *sta,
				      void *buf,
				      size_t len)
{
	struct wcn36xx_hal_config_bss_rsp_msg *rsp;
	struct wcn36xx_hal_config_bss_rsp_params *params;
	struct wcn36xx_vif *vif_priv = wcn36xx_vif_to_priv(vif);

	if (len < sizeof(*rsp))
		return -EINVAL;

	rsp = (struct wcn36xx_hal_config_bss_rsp_msg *)buf;
	params = &rsp->bss_rsp_params;

	if (params->status != WCN36XX_FW_MSG_RESULT_SUCCESS) {
		wcn36xx_warn("hal config bss response failure: %d\n",
			     params->status);
		return -EIO;
	}

	wcn36xx_dbg(WCN36XX_DBG_HAL,
		    "hal config bss rsp status %d bss_idx %d dpu_desc_index %d"
		    " sta_idx %d self_idx %d bcast_idx %d mac %pM"
		    " power %d ucast_dpu_signature %d\n",
		    params->status, params->bss_index, params->dpu_desc_index,
		    params->bss_sta_index, params->bss_self_sta_index,
		    params->bss_bcast_sta_idx, params->mac,
		    params->tx_mgmt_power, params->ucast_dpu_signature);

	vif_priv->bss_index = params->bss_index;

	if (sta) {
		struct wcn36xx_sta *sta_priv = wcn36xx_sta_to_priv(sta);
		sta_priv->bss_sta_index = params->bss_sta_index;
		sta_priv->bss_dpu_desc_index = params->dpu_desc_index;
	}

	vif_priv->self_ucast_dpu_sign = params->ucast_dpu_signature;

	return 0;
}

int wcn36xx_smd_config_bss(struct wcn36xx *wcn, struct ieee80211_vif *vif,
			   struct ieee80211_sta *sta, const u8 *bssid,
			   bool update)
{
	int ret;

	mutex_lock(&wcn->hal_mutex);

	if (!wcn36xx_is_fw_version(wcn, 1, 2, 2, 24))
		ret = wcn36xx_smd_config_bss_v1(wcn, vif, sta, bssid, update);
	else
		ret = wcn36xx_smd_config_bss_v0(wcn, vif, sta, bssid, update);

	if (ret) {
		wcn36xx_err("Sending hal_config_bss failed\n");
		goto out;
	}
	ret = wcn36xx_smd_config_bss_rsp(wcn,
					 vif,
					 sta,
					 wcn->hal_buf,
					 wcn->hal_rsp_len);
	if (ret)
		wcn36xx_err("hal_config_bss response failed err=%d\n", ret);

out:
	mutex_unlock(&wcn->hal_mutex);
	return ret;
}

int wcn36xx_smd_delete_bss(struct wcn36xx *wcn, struct ieee80211_vif *vif)
{
	struct wcn36xx_hal_delete_bss_req_msg msg_body;
	struct wcn36xx_vif *vif_priv = wcn36xx_vif_to_priv(vif);
	int ret = 0;

	mutex_lock(&wcn->hal_mutex);

	if (vif_priv->bss_index == WCN36XX_HAL_BSS_INVALID_IDX)
		goto out;

	INIT_HAL_MSG(msg_body, WCN36XX_HAL_DELETE_BSS_REQ);

	msg_body.bss_index = vif_priv->bss_index;

	PREPARE_HAL_BUF(wcn->hal_buf, msg_body);

	wcn36xx_dbg(WCN36XX_DBG_HAL, "hal delete bss %d\n", msg_body.bss_index);

	ret = wcn36xx_smd_send_and_wait(wcn, msg_body.header.len);
	if (ret) {
		wcn36xx_err("Sending hal_delete_bss failed\n");
		goto out;
	}
	ret = wcn36xx_smd_rsp_status_check(wcn->hal_buf, wcn->hal_rsp_len);
	if (ret) {
		wcn36xx_err("hal_delete_bss response failed err=%d\n", ret);
		goto out;
	}

	vif_priv->bss_index = WCN36XX_HAL_BSS_INVALID_IDX;
out:
	mutex_unlock(&wcn->hal_mutex);
	return ret;
}

int wcn36xx_smd_send_beacon(struct wcn36xx *wcn, struct ieee80211_vif *vif,
			    struct sk_buff *skb_beacon, u16 tim_off,
			    u16 p2p_off)
{
	struct wcn36xx_hal_send_beacon_req_msg msg_body;
	int ret, pad, pvm_len;

	mutex_lock(&wcn->hal_mutex);
	INIT_HAL_MSG(msg_body, WCN36XX_HAL_SEND_BEACON_REQ);

	pvm_len = skb_beacon->data[tim_off + 1] - 3;
	pad = TIM_MIN_PVM_SIZE - pvm_len;

	/* Padding is irrelevant to mesh mode since tim_off is always 0. */
	if (vif->type == NL80211_IFTYPE_MESH_POINT)
		pad = 0;

	msg_body.beacon_length = skb_beacon->len + pad;
	/* TODO need to find out why + 6 is needed */
	msg_body.beacon_length6 = msg_body.beacon_length + 6;

	if (msg_body.beacon_length > BEACON_TEMPLATE_SIZE) {
		wcn36xx_err("Beacon is too big: beacon size=%d\n",
			      msg_body.beacon_length);
		ret = -ENOMEM;
		goto out;
	}
	memcpy(msg_body.beacon, skb_beacon->data, skb_beacon->len);
	memcpy(msg_body.bssid, vif->addr, ETH_ALEN);

	if (pad > 0) {
		/*
		 * The wcn36xx FW has a fixed size for the PVM in the TIM. If
		 * given the beacon template from mac80211 with a PVM shorter
		 * than the FW expectes it will overwrite the data after the
		 * TIM.
		 */
		wcn36xx_dbg(WCN36XX_DBG_HAL, "Pad TIM PVM. %d bytes at %d\n",
			    pad, pvm_len);
		memmove(&msg_body.beacon[tim_off + 5 + pvm_len + pad],
			&msg_body.beacon[tim_off + 5 + pvm_len],
			skb_beacon->len - (tim_off + 5 + pvm_len));
		memset(&msg_body.beacon[tim_off + 5 + pvm_len], 0, pad);
		msg_body.beacon[tim_off + 1] += pad;
	}

	/* TODO need to find out why this is needed? */
	if (vif->type == NL80211_IFTYPE_MESH_POINT)
		/* mesh beacon don't need this, so push further down */
		msg_body.tim_ie_offset = 256;
	else
		msg_body.tim_ie_offset = tim_off+4;
	msg_body.p2p_ie_offset = p2p_off;
	PREPARE_HAL_BUF(wcn->hal_buf, msg_body);

	wcn36xx_dbg(WCN36XX_DBG_HAL,
		    "hal send beacon beacon_length %d\n",
		    msg_body.beacon_length);

	ret = wcn36xx_smd_send_and_wait(wcn, msg_body.header.len);
	if (ret) {
		wcn36xx_err("Sending hal_send_beacon failed\n");
		goto out;
	}
	ret = wcn36xx_smd_rsp_status_check(wcn->hal_buf, wcn->hal_rsp_len);
	if (ret) {
		wcn36xx_err("hal_send_beacon response failed err=%d\n", ret);
		goto out;
	}
out:
	mutex_unlock(&wcn->hal_mutex);
	return ret;
}

int wcn36xx_smd_update_proberesp_tmpl(struct wcn36xx *wcn,
				      struct ieee80211_vif *vif,
				      struct sk_buff *skb)
{
	struct wcn36xx_hal_send_probe_resp_req_msg msg;
	int ret;

	mutex_lock(&wcn->hal_mutex);
	INIT_HAL_MSG(msg, WCN36XX_HAL_UPDATE_PROBE_RSP_TEMPLATE_REQ);

	if (skb->len > BEACON_TEMPLATE_SIZE) {
		wcn36xx_warn("probe response template is too big: %d\n",
			     skb->len);
		ret = -E2BIG;
		goto out;
	}

	msg.probe_resp_template_len = skb->len;
	memcpy(&msg.probe_resp_template, skb->data, skb->len);

	memcpy(msg.bssid, vif->addr, ETH_ALEN);

	PREPARE_HAL_BUF(wcn->hal_buf, msg);

	wcn36xx_dbg(WCN36XX_DBG_HAL,
		    "hal update probe rsp len %d bssid %pM\n",
		    msg.probe_resp_template_len, msg.bssid);

	ret = wcn36xx_smd_send_and_wait(wcn, msg.header.len);
	if (ret) {
		wcn36xx_err("Sending hal_update_proberesp_tmpl failed\n");
		goto out;
	}
	ret = wcn36xx_smd_rsp_status_check(wcn->hal_buf, wcn->hal_rsp_len);
	if (ret) {
		wcn36xx_err("hal_update_proberesp_tmpl response failed err=%d\n",
			    ret);
		goto out;
	}
out:
	mutex_unlock(&wcn->hal_mutex);
	return ret;
}

int wcn36xx_smd_set_stakey(struct wcn36xx *wcn,
			   enum ani_ed_type enc_type,
			   u8 keyidx,
			   u8 keylen,
			   u8 *key,
			   u8 sta_index)
{
	struct wcn36xx_hal_set_sta_key_req_msg msg_body;
	int ret;

	mutex_lock(&wcn->hal_mutex);
	INIT_HAL_MSG(msg_body, WCN36XX_HAL_SET_STAKEY_REQ);

	msg_body.set_sta_key_params.sta_index = sta_index;
	msg_body.set_sta_key_params.enc_type = enc_type;

	if (enc_type == WCN36XX_HAL_ED_WEP104 ||
	    enc_type == WCN36XX_HAL_ED_WEP40) {
		/* Use bss key for wep (static) */
		msg_body.set_sta_key_params.def_wep_idx = keyidx;
		msg_body.set_sta_key_params.wep_type = 0;
	} else {
		msg_body.set_sta_key_params.key[0].id = keyidx;
		msg_body.set_sta_key_params.key[0].unicast = 1;
		msg_body.set_sta_key_params.key[0].direction = WCN36XX_HAL_TX_RX;
		msg_body.set_sta_key_params.key[0].pae_role = 0;
		msg_body.set_sta_key_params.key[0].length = keylen;
		memcpy(msg_body.set_sta_key_params.key[0].key, key, keylen);
	}

	msg_body.set_sta_key_params.single_tid_rc = 1;

	PREPARE_HAL_BUF(wcn->hal_buf, msg_body);

	ret = wcn36xx_smd_send_and_wait(wcn, msg_body.header.len);
	if (ret) {
		wcn36xx_err("Sending hal_set_stakey failed\n");
		goto out;
	}
	ret = wcn36xx_smd_rsp_status_check(wcn->hal_buf, wcn->hal_rsp_len);
	if (ret) {
		wcn36xx_err("hal_set_stakey response failed err=%d\n", ret);
		goto out;
	}
out:
	mutex_unlock(&wcn->hal_mutex);
	return ret;
}

int wcn36xx_smd_set_bsskey(struct wcn36xx *wcn,
			   enum ani_ed_type enc_type,
			   u8 bssidx,
			   u8 keyidx,
			   u8 keylen,
			   u8 *key)
{
	struct wcn36xx_hal_set_bss_key_req_msg msg_body;
	int ret;

	mutex_lock(&wcn->hal_mutex);
	INIT_HAL_MSG(msg_body, WCN36XX_HAL_SET_BSSKEY_REQ);
	msg_body.bss_idx = bssidx;
	msg_body.enc_type = enc_type;
	msg_body.num_keys = 1;
	msg_body.keys[0].id = keyidx;
	msg_body.keys[0].unicast = 0;
	msg_body.keys[0].direction = WCN36XX_HAL_RX_ONLY;
	msg_body.keys[0].pae_role = 0;
	msg_body.keys[0].length = keylen;
	memcpy(msg_body.keys[0].key, key, keylen);

	PREPARE_HAL_BUF(wcn->hal_buf, msg_body);

	ret = wcn36xx_smd_send_and_wait(wcn, msg_body.header.len);
	if (ret) {
		wcn36xx_err("Sending hal_set_bsskey failed\n");
		goto out;
	}
	ret = wcn36xx_smd_rsp_status_check(wcn->hal_buf, wcn->hal_rsp_len);
	if (ret) {
		wcn36xx_err("hal_set_bsskey response failed err=%d\n", ret);
		goto out;
	}
out:
	mutex_unlock(&wcn->hal_mutex);
	return ret;
}

int wcn36xx_smd_remove_stakey(struct wcn36xx *wcn,
			      enum ani_ed_type enc_type,
			      u8 keyidx,
			      u8 sta_index)
{
	struct wcn36xx_hal_remove_sta_key_req_msg msg_body;
	int ret;

	mutex_lock(&wcn->hal_mutex);
	INIT_HAL_MSG(msg_body, WCN36XX_HAL_RMV_STAKEY_REQ);

	msg_body.sta_idx = sta_index;
	msg_body.enc_type = enc_type;
	msg_body.key_id = keyidx;

	PREPARE_HAL_BUF(wcn->hal_buf, msg_body);

	ret = wcn36xx_smd_send_and_wait(wcn, msg_body.header.len);
	if (ret) {
		wcn36xx_err("Sending hal_remove_stakey failed\n");
		goto out;
	}
	ret = wcn36xx_smd_rsp_status_check(wcn->hal_buf, wcn->hal_rsp_len);
	if (ret) {
		wcn36xx_err("hal_remove_stakey response failed err=%d\n", ret);
		goto out;
	}
out:
	mutex_unlock(&wcn->hal_mutex);
	return ret;
}

int wcn36xx_smd_remove_bsskey(struct wcn36xx *wcn,
			      enum ani_ed_type enc_type,
			      u8 bssidx,
			      u8 keyidx)
{
	struct wcn36xx_hal_remove_bss_key_req_msg msg_body;
	int ret;

	mutex_lock(&wcn->hal_mutex);
	INIT_HAL_MSG(msg_body, WCN36XX_HAL_RMV_BSSKEY_REQ);
	msg_body.bss_idx = bssidx;
	msg_body.enc_type = enc_type;
	msg_body.key_id = keyidx;

	PREPARE_HAL_BUF(wcn->hal_buf, msg_body);

	ret = wcn36xx_smd_send_and_wait(wcn, msg_body.header.len);
	if (ret) {
		wcn36xx_err("Sending hal_remove_bsskey failed\n");
		goto out;
	}
	ret = wcn36xx_smd_rsp_status_check(wcn->hal_buf, wcn->hal_rsp_len);
	if (ret) {
		wcn36xx_err("hal_remove_bsskey response failed err=%d\n", ret);
		goto out;
	}
out:
	mutex_unlock(&wcn->hal_mutex);
	return ret;
}

int wcn36xx_smd_enter_bmps(struct wcn36xx *wcn, struct ieee80211_vif *vif)
{
	struct wcn36xx_hal_enter_bmps_req_msg msg_body;
	struct wcn36xx_vif *vif_priv = wcn36xx_vif_to_priv(vif);
	int ret;

	mutex_lock(&wcn->hal_mutex);
	INIT_HAL_MSG(msg_body, WCN36XX_HAL_ENTER_BMPS_REQ);

	msg_body.bss_index = vif_priv->bss_index;
	msg_body.tbtt = vif->bss_conf.sync_tsf;
	msg_body.dtim_period = vif_priv->dtim_period;

	PREPARE_HAL_BUF(wcn->hal_buf, msg_body);

	ret = wcn36xx_smd_send_and_wait(wcn, msg_body.header.len);
	if (ret) {
		wcn36xx_err("Sending hal_enter_bmps failed\n");
		goto out;
	}
	ret = wcn36xx_smd_rsp_status_check(wcn->hal_buf, wcn->hal_rsp_len);
	if (ret) {
		wcn36xx_err("hal_enter_bmps response failed err=%d\n", ret);
		goto out;
	}
out:
	mutex_unlock(&wcn->hal_mutex);
	return ret;
}

int wcn36xx_smd_exit_bmps(struct wcn36xx *wcn, struct ieee80211_vif *vif)
{
	struct wcn36xx_hal_exit_bmps_req_msg msg_body;
	struct wcn36xx_vif *vif_priv = wcn36xx_vif_to_priv(vif);
	int ret;

	mutex_lock(&wcn->hal_mutex);
	INIT_HAL_MSG(msg_body, WCN36XX_HAL_EXIT_BMPS_REQ);

	msg_body.bss_index = vif_priv->bss_index;
	msg_body.send_data_null = 1;

	PREPARE_HAL_BUF(wcn->hal_buf, msg_body);

	ret = wcn36xx_smd_send_and_wait(wcn, msg_body.header.len);
	if (ret) {
		wcn36xx_err("Sending hal_exit_bmps failed\n");
		goto out;
	}
	ret = wcn36xx_smd_rsp_status_check(wcn->hal_buf, wcn->hal_rsp_len);
	if (ret) {
		wcn36xx_err("hal_exit_bmps response failed err=%d\n", ret);
		goto out;
	}
out:
	mutex_unlock(&wcn->hal_mutex);
	return ret;
}

int wcn36xx_smd_set_power_params(struct wcn36xx *wcn, bool ignore_dtim)
{
	struct wcn36xx_hal_set_power_params_req_msg msg_body;
	int ret;

	mutex_lock(&wcn->hal_mutex);
	INIT_HAL_MSG(msg_body, WCN36XX_HAL_SET_POWER_PARAMS_REQ);

	/*
	 * When host is down ignore every second dtim
	 */
	if (ignore_dtim) {
		msg_body.ignore_dtim = 1;
		msg_body.dtim_period = 2;
	}
	msg_body.listen_interval = WCN36XX_LISTEN_INTERVAL(wcn);

	PREPARE_HAL_BUF(wcn->hal_buf, msg_body);

	ret = wcn36xx_smd_send_and_wait(wcn, msg_body.header.len);
	if (ret) {
		wcn36xx_err("Sending hal_set_power_params failed\n");
		goto out;
	}

out:
	mutex_unlock(&wcn->hal_mutex);
	return ret;
}

/* Notice: This function should be called after associated, or else it
 * will be invalid
 */
int wcn36xx_smd_keep_alive_req(struct wcn36xx *wcn,
			       struct ieee80211_vif *vif,
			       int packet_type)
{
	struct wcn36xx_hal_keep_alive_req_msg msg_body;
	struct wcn36xx_vif *vif_priv = wcn36xx_vif_to_priv(vif);
	int ret;

	mutex_lock(&wcn->hal_mutex);
	INIT_HAL_MSG(msg_body, WCN36XX_HAL_KEEP_ALIVE_REQ);

	if (packet_type == WCN36XX_HAL_KEEP_ALIVE_NULL_PKT) {
		msg_body.bss_index = vif_priv->bss_index;
		msg_body.packet_type = WCN36XX_HAL_KEEP_ALIVE_NULL_PKT;
		msg_body.time_period = WCN36XX_KEEP_ALIVE_TIME_PERIOD;
	} else if (packet_type == WCN36XX_HAL_KEEP_ALIVE_UNSOLICIT_ARP_RSP) {
		/* TODO: it also support ARP response type */
	} else {
		wcn36xx_warn("unknown keep alive packet type %d\n", packet_type);
		ret = -EINVAL;
		goto out;
	}

	PREPARE_HAL_BUF(wcn->hal_buf, msg_body);

	ret = wcn36xx_smd_send_and_wait(wcn, msg_body.header.len);
	if (ret) {
		wcn36xx_err("Sending hal_keep_alive failed\n");
		goto out;
	}
	ret = wcn36xx_smd_rsp_status_check(wcn->hal_buf, wcn->hal_rsp_len);
	if (ret) {
		wcn36xx_err("hal_keep_alive response failed err=%d\n", ret);
		goto out;
	}
out:
	mutex_unlock(&wcn->hal_mutex);
	return ret;
}

int wcn36xx_smd_dump_cmd_req(struct wcn36xx *wcn, u32 arg1, u32 arg2,
			     u32 arg3, u32 arg4, u32 arg5)
{
	struct wcn36xx_hal_dump_cmd_req_msg msg_body;
	int ret;

	mutex_lock(&wcn->hal_mutex);
	INIT_HAL_MSG(msg_body, WCN36XX_HAL_DUMP_COMMAND_REQ);

	msg_body.arg1 = arg1;
	msg_body.arg2 = arg2;
	msg_body.arg3 = arg3;
	msg_body.arg4 = arg4;
	msg_body.arg5 = arg5;

	PREPARE_HAL_BUF(wcn->hal_buf, msg_body);

	ret = wcn36xx_smd_send_and_wait(wcn, msg_body.header.len);
	if (ret) {
		wcn36xx_err("Sending hal_dump_cmd failed\n");
		goto out;
	}
	ret = wcn36xx_smd_rsp_status_check(wcn->hal_buf, wcn->hal_rsp_len);
	if (ret) {
		wcn36xx_err("hal_dump_cmd response failed err=%d\n", ret);
		goto out;
	}
out:
	mutex_unlock(&wcn->hal_mutex);
	return ret;
}

void set_feat_caps(u32 *bitmap, enum place_holder_in_cap_bitmap cap)
{
	int arr_idx, bit_idx;

	if (cap < 0 || cap > 127) {
		wcn36xx_warn("error cap idx %d\n", cap);
		return;
	}

	arr_idx = cap / 32;
	bit_idx = cap % 32;
	bitmap[arr_idx] |= (1 << bit_idx);
}

int get_feat_caps(u32 *bitmap, enum place_holder_in_cap_bitmap cap)
{
	int arr_idx, bit_idx;

	if (cap < 0 || cap > 127) {
		wcn36xx_warn("error cap idx %d\n", cap);
		return -EINVAL;
	}

	arr_idx = cap / 32;
	bit_idx = cap % 32;

	return (bitmap[arr_idx] & (1 << bit_idx)) ? 1 : 0;
}

void clear_feat_caps(u32 *bitmap, enum place_holder_in_cap_bitmap cap)
{
	int arr_idx, bit_idx;

	if (cap < 0 || cap > 127) {
		wcn36xx_warn("error cap idx %d\n", cap);
		return;
	}

	arr_idx = cap / 32;
	bit_idx = cap % 32;
	bitmap[arr_idx] &= ~(1 << bit_idx);
}

int wcn36xx_smd_feature_caps_exchange(struct wcn36xx *wcn)
{
	struct wcn36xx_hal_feat_caps_msg msg_body, *rsp;
	int ret, i;

	mutex_lock(&wcn->hal_mutex);
	INIT_HAL_MSG(msg_body, WCN36XX_HAL_FEATURE_CAPS_EXCHANGE_REQ);

	set_feat_caps(msg_body.feat_caps, STA_POWERSAVE);
	if (wcn->rf_id == RF_IRIS_WCN3680)
		set_feat_caps(msg_body.feat_caps, DOT11AC);

	PREPARE_HAL_BUF(wcn->hal_buf, msg_body);

	ret = wcn36xx_smd_send_and_wait(wcn, msg_body.header.len);
	if (ret) {
		wcn36xx_err("Sending hal_feature_caps_exchange failed\n");
		goto out;
	}
	if (wcn->hal_rsp_len != sizeof(*rsp)) {
		wcn36xx_err("Invalid hal_feature_caps_exchange response");
		goto out;
	}

	rsp = (struct wcn36xx_hal_feat_caps_msg *) wcn->hal_buf;

	for (i = 0; i < WCN36XX_HAL_CAPS_SIZE; i++)
		wcn->fw_feat_caps[i] = rsp->feat_caps[i];
out:
	mutex_unlock(&wcn->hal_mutex);
	return ret;
}

static int wcn36xx_smd_add_ba_session_rsp(void *buf, int len, u8 *session)
{
	struct wcn36xx_hal_add_ba_session_rsp_msg *rsp;

	if (len < sizeof(*rsp))
		return -EINVAL;

	rsp = (struct wcn36xx_hal_add_ba_session_rsp_msg *)buf;
	if (rsp->status != WCN36XX_FW_MSG_RESULT_SUCCESS)
		return rsp->status;

	*session = rsp->ba_session_id;

	return 0;
}

int wcn36xx_smd_add_ba_session(struct wcn36xx *wcn,
		struct ieee80211_sta *sta,
		u16 tid,
		u16 *ssn,
		u8 direction,
		u8 sta_index)
{
	struct wcn36xx_hal_add_ba_session_req_msg msg_body;
	u8 session_id;
	int ret;

	mutex_lock(&wcn->hal_mutex);
	INIT_HAL_MSG(msg_body, WCN36XX_HAL_ADD_BA_SESSION_REQ);

	msg_body.sta_index = sta_index;
	memcpy(&msg_body.mac_addr, sta->addr, ETH_ALEN);
	msg_body.dialog_token = 0x10;
	msg_body.tid = tid;

	/* Immediate BA because Delayed BA is not supported */
	msg_body.policy = 1;
	msg_body.buffer_size = WCN36XX_AGGR_BUFFER_SIZE;
	msg_body.timeout = 0;
	if (ssn)
		msg_body.ssn = *ssn;
	msg_body.direction = direction;

	PREPARE_HAL_BUF(wcn->hal_buf, msg_body);

	ret = wcn36xx_smd_send_and_wait(wcn, msg_body.header.len);
	if (ret) {
		wcn36xx_err("Sending hal_add_ba_session failed\n");
		goto out;
	}
	ret = wcn36xx_smd_add_ba_session_rsp(wcn->hal_buf, wcn->hal_rsp_len,
					     &session_id);
	if (ret) {
		wcn36xx_err("hal_add_ba_session response failed err=%d\n", ret);
		goto out;
	}

	ret = session_id;
out:
	mutex_unlock(&wcn->hal_mutex);
	return ret;
}

int wcn36xx_smd_add_ba(struct wcn36xx *wcn, u8 session_id)
{
	struct wcn36xx_hal_add_ba_req_msg msg_body;
	int ret;

	mutex_lock(&wcn->hal_mutex);
	INIT_HAL_MSG(msg_body, WCN36XX_HAL_ADD_BA_REQ);

	msg_body.session_id = session_id;
	msg_body.win_size = WCN36XX_AGGR_BUFFER_SIZE;

	PREPARE_HAL_BUF(wcn->hal_buf, msg_body);

	ret = wcn36xx_smd_send_and_wait(wcn, msg_body.header.len);
	if (ret) {
		wcn36xx_err("Sending hal_add_ba failed\n");
		goto out;
	}
	ret = wcn36xx_smd_rsp_status_check(wcn->hal_buf, wcn->hal_rsp_len);
	if (ret) {
		wcn36xx_err("hal_add_ba response failed err=%d\n", ret);
		goto out;
	}
out:
	mutex_unlock(&wcn->hal_mutex);
	return ret;
}

int wcn36xx_smd_del_ba(struct wcn36xx *wcn, u16 tid, u8 sta_index)
{
	struct wcn36xx_hal_del_ba_req_msg msg_body;
	int ret;

	mutex_lock(&wcn->hal_mutex);
	INIT_HAL_MSG(msg_body, WCN36XX_HAL_DEL_BA_REQ);

	msg_body.sta_index = sta_index;
	msg_body.tid = tid;
	msg_body.direction = 0;
	PREPARE_HAL_BUF(wcn->hal_buf, msg_body);

	ret = wcn36xx_smd_send_and_wait(wcn, msg_body.header.len);
	if (ret) {
		wcn36xx_err("Sending hal_del_ba failed\n");
		goto out;
	}
	ret = wcn36xx_smd_rsp_status_check(wcn->hal_buf, wcn->hal_rsp_len);
	if (ret) {
		wcn36xx_err("hal_del_ba response failed err=%d\n", ret);
		goto out;
	}
out:
	mutex_unlock(&wcn->hal_mutex);
	return ret;
}

static int wcn36xx_smd_trigger_ba_rsp(void *buf, int len)
{
	struct wcn36xx_hal_trigger_ba_rsp_msg *rsp;

	if (len < sizeof(*rsp))
		return -EINVAL;

	rsp = (struct wcn36xx_hal_trigger_ba_rsp_msg *) buf;
	return rsp->status;
}

int wcn36xx_smd_trigger_ba(struct wcn36xx *wcn, u8 sta_index, u16 tid, u8 session_id)
{
	struct wcn36xx_hal_trigger_ba_req_msg msg_body;
	struct wcn36xx_hal_trigger_ba_req_candidate *candidate;
	int ret;

	mutex_lock(&wcn->hal_mutex);
	INIT_HAL_MSG(msg_body, WCN36XX_HAL_TRIGGER_BA_REQ);

	msg_body.session_id = session_id;
	msg_body.candidate_cnt = 1;
	msg_body.header.len += sizeof(*candidate);
	PREPARE_HAL_BUF(wcn->hal_buf, msg_body);

	candidate = (struct wcn36xx_hal_trigger_ba_req_candidate *)
		(wcn->hal_buf + sizeof(msg_body));
	candidate->sta_index = sta_index;
	candidate->tid_bitmap = 1 << tid;

	ret = wcn36xx_smd_send_and_wait(wcn, msg_body.header.len);
	if (ret) {
		wcn36xx_err("Sending hal_trigger_ba failed\n");
		goto out;
	}
	ret = wcn36xx_smd_trigger_ba_rsp(wcn->hal_buf, wcn->hal_rsp_len);
	if (ret) {
		wcn36xx_err("hal_trigger_ba response failed err=%d\n", ret);
		goto out;
	}
out:
	mutex_unlock(&wcn->hal_mutex);
	return ret;
}

static int wcn36xx_smd_tx_compl_ind(struct wcn36xx *wcn, void *buf, size_t len)
{
	struct wcn36xx_hal_tx_compl_ind_msg *rsp = buf;

	if (len != sizeof(*rsp)) {
		wcn36xx_warn("Bad TX complete indication\n");
		return -EIO;
	}

	wcn36xx_dxe_tx_ack_ind(wcn, rsp->status);

	return 0;
}

static int wcn36xx_smd_hw_scan_ind(struct wcn36xx *wcn, void *buf, size_t len)
{
	struct wcn36xx_hal_scan_offload_ind *rsp = buf;
	struct cfg80211_scan_info scan_info = {};

	if (len != sizeof(*rsp)) {
		wcn36xx_warn("Corrupted delete scan indication\n");
		return -EIO;
	}

	wcn36xx_dbg(WCN36XX_DBG_HAL, "scan indication (type %x)\n", rsp->type);

	switch (rsp->type) {
	case WCN36XX_HAL_SCAN_IND_FAILED:
	case WCN36XX_HAL_SCAN_IND_DEQUEUED:
		scan_info.aborted = true;
		/* fall through */
	case WCN36XX_HAL_SCAN_IND_COMPLETED:
		mutex_lock(&wcn->scan_lock);
		wcn->scan_req = NULL;
		if (wcn->scan_aborted)
			scan_info.aborted = true;
		mutex_unlock(&wcn->scan_lock);
		ieee80211_scan_completed(wcn->hw, &scan_info);
		break;
	case WCN36XX_HAL_SCAN_IND_STARTED:
	case WCN36XX_HAL_SCAN_IND_FOREIGN_CHANNEL:
	case WCN36XX_HAL_SCAN_IND_PREEMPTED:
	case WCN36XX_HAL_SCAN_IND_RESTARTED:
		break;
	default:
		wcn36xx_warn("Unknown scan indication type %x\n", rsp->type);
	}

	return 0;
}

static int wcn36xx_smd_missed_beacon_ind(struct wcn36xx *wcn,
					 void *buf,
					 size_t len)
{
	struct wcn36xx_hal_missed_beacon_ind_msg *rsp = buf;
	struct ieee80211_vif *vif = NULL;
	struct wcn36xx_vif *tmp;

	/* Old FW does not have bss index */
	if (wcn36xx_is_fw_version(wcn, 1, 2, 2, 24)) {
		list_for_each_entry(tmp, &wcn->vif_list, list) {
			wcn36xx_dbg(WCN36XX_DBG_HAL, "beacon missed bss_index %d\n",
				    tmp->bss_index);
			vif = wcn36xx_priv_to_vif(tmp);
			ieee80211_connection_loss(vif);
		}
		return 0;
	}

	if (len != sizeof(*rsp)) {
		wcn36xx_warn("Corrupted missed beacon indication\n");
		return -EIO;
	}

	list_for_each_entry(tmp, &wcn->vif_list, list) {
		if (tmp->bss_index == rsp->bss_index) {
			wcn36xx_dbg(WCN36XX_DBG_HAL, "beacon missed bss_index %d\n",
				    rsp->bss_index);
			vif = wcn36xx_priv_to_vif(tmp);
			ieee80211_connection_loss(vif);
			return 0;
		}
	}

	wcn36xx_warn("BSS index %d not found\n", rsp->bss_index);
	return -ENOENT;
}

static int wcn36xx_smd_delete_sta_context_ind(struct wcn36xx *wcn,
					      void *buf,
					      size_t len)
{
	struct wcn36xx_hal_delete_sta_context_ind_msg *rsp = buf;
	struct wcn36xx_vif *tmp;
	struct ieee80211_sta *sta;

	if (len != sizeof(*rsp)) {
		wcn36xx_warn("Corrupted delete sta indication\n");
		return -EIO;
	}

	wcn36xx_dbg(WCN36XX_DBG_HAL, "delete station indication %pM index %d\n",
		    rsp->addr2, rsp->sta_id);

	list_for_each_entry(tmp, &wcn->vif_list, list) {
		rcu_read_lock();
		sta = ieee80211_find_sta(wcn36xx_priv_to_vif(tmp), rsp->addr2);
		if (sta)
			ieee80211_report_low_ack(sta, 0);
		rcu_read_unlock();
		if (sta)
			return 0;
	}

	wcn36xx_warn("STA with addr %pM and index %d not found\n",
		     rsp->addr2,
		     rsp->sta_id);
	return -ENOENT;
}

static int wcn36xx_smd_print_reg_info_ind(struct wcn36xx *wcn,
					  void *buf,
					  size_t len)
{
	struct wcn36xx_hal_print_reg_info_ind *rsp = buf;
	int i;

	if (len < sizeof(*rsp)) {
		wcn36xx_warn("Corrupted print reg info indication\n");
		return -EIO;
	}

	wcn36xx_dbg(WCN36XX_DBG_HAL,
		    "reginfo indication, scenario: 0x%x reason: 0x%x\n",
		    rsp->scenario, rsp->reason);

	for (i = 0; i < rsp->count; i++) {
		wcn36xx_dbg(WCN36XX_DBG_HAL, "\t0x%x: 0x%x\n",
			    rsp->regs[i].addr, rsp->regs[i].value);
	}

	return 0;
}

int wcn36xx_smd_update_cfg(struct wcn36xx *wcn, u32 cfg_id, u32 value)
{
	struct wcn36xx_hal_update_cfg_req_msg msg_body, *body;
	size_t len;
	int ret;

	mutex_lock(&wcn->hal_mutex);
	INIT_HAL_MSG(msg_body, WCN36XX_HAL_UPDATE_CFG_REQ);

	PREPARE_HAL_BUF(wcn->hal_buf, msg_body);

	body = (struct wcn36xx_hal_update_cfg_req_msg *) wcn->hal_buf;
	len = msg_body.header.len;

	put_cfg_tlv_u32(wcn, &len, cfg_id, value);
	body->header.len = len;
	body->len = len - sizeof(*body);

	ret = wcn36xx_smd_send_and_wait(wcn, body->header.len);
	if (ret) {
		wcn36xx_err("Sending hal_update_cfg failed\n");
		goto out;
	}
	ret = wcn36xx_smd_rsp_status_check(wcn->hal_buf, wcn->hal_rsp_len);
	if (ret) {
		wcn36xx_err("hal_update_cfg response failed err=%d\n", ret);
		goto out;
	}
out:
	mutex_unlock(&wcn->hal_mutex);
	return ret;
}

int wcn36xx_smd_set_mc_list(struct wcn36xx *wcn,
			    struct ieee80211_vif *vif,
			    struct wcn36xx_hal_rcv_flt_mc_addr_list_type *fp)
{
	struct wcn36xx_vif *vif_priv = wcn36xx_vif_to_priv(vif);
	struct wcn36xx_hal_rcv_flt_pkt_set_mc_list_req_msg *msg_body = NULL;
	int ret;

	mutex_lock(&wcn->hal_mutex);

	msg_body = (struct wcn36xx_hal_rcv_flt_pkt_set_mc_list_req_msg *)
		   wcn->hal_buf;
	init_hal_msg(&msg_body->header, WCN36XX_HAL_8023_MULTICAST_LIST_REQ,
		     sizeof(msg_body->mc_addr_list));

	/* An empty list means all mc traffic will be received */
	if (fp)
		memcpy(&msg_body->mc_addr_list, fp,
		       sizeof(msg_body->mc_addr_list));
	else
		msg_body->mc_addr_list.mc_addr_count = 0;

	msg_body->mc_addr_list.bss_index = vif_priv->bss_index;

	ret = wcn36xx_smd_send_and_wait(wcn, msg_body->header.len);
	if (ret) {
		wcn36xx_err("Sending HAL_8023_MULTICAST_LIST failed\n");
		goto out;
	}
	ret = wcn36xx_smd_rsp_status_check(wcn->hal_buf, wcn->hal_rsp_len);
	if (ret) {
		wcn36xx_err("HAL_8023_MULTICAST_LIST rsp failed err=%d\n", ret);
		goto out;
	}
out:
	mutex_unlock(&wcn->hal_mutex);
	return ret;
}

int wcn36xx_smd_rsp_process(struct rpmsg_device *rpdev,
			    void *buf, int len, void *priv, u32 addr)
{
	const struct wcn36xx_hal_msg_header *msg_header = buf;
	struct ieee80211_hw *hw = priv;
	struct wcn36xx *wcn = hw->priv;
	struct wcn36xx_hal_ind_msg *msg_ind;
	wcn36xx_dbg_dump(WCN36XX_DBG_SMD_DUMP, "SMD <<< ", buf, len);

	switch (msg_header->msg_type) {
	case WCN36XX_HAL_START_RSP:
	case WCN36XX_HAL_CONFIG_STA_RSP:
	case WCN36XX_HAL_CONFIG_BSS_RSP:
	case WCN36XX_HAL_ADD_STA_SELF_RSP:
	case WCN36XX_HAL_STOP_RSP:
	case WCN36XX_HAL_DEL_STA_SELF_RSP:
	case WCN36XX_HAL_DELETE_STA_RSP:
	case WCN36XX_HAL_INIT_SCAN_RSP:
	case WCN36XX_HAL_START_SCAN_RSP:
	case WCN36XX_HAL_END_SCAN_RSP:
	case WCN36XX_HAL_FINISH_SCAN_RSP:
	case WCN36XX_HAL_DOWNLOAD_NV_RSP:
	case WCN36XX_HAL_DELETE_BSS_RSP:
	case WCN36XX_HAL_SEND_BEACON_RSP:
	case WCN36XX_HAL_SET_LINK_ST_RSP:
	case WCN36XX_HAL_UPDATE_PROBE_RSP_TEMPLATE_RSP:
	case WCN36XX_HAL_SET_BSSKEY_RSP:
	case WCN36XX_HAL_SET_STAKEY_RSP:
	case WCN36XX_HAL_RMV_STAKEY_RSP:
	case WCN36XX_HAL_RMV_BSSKEY_RSP:
	case WCN36XX_HAL_ENTER_BMPS_RSP:
	case WCN36XX_HAL_SET_POWER_PARAMS_RSP:
	case WCN36XX_HAL_EXIT_BMPS_RSP:
	case WCN36XX_HAL_KEEP_ALIVE_RSP:
	case WCN36XX_HAL_DUMP_COMMAND_RSP:
	case WCN36XX_HAL_ADD_BA_SESSION_RSP:
	case WCN36XX_HAL_ADD_BA_RSP:
	case WCN36XX_HAL_DEL_BA_RSP:
	case WCN36XX_HAL_TRIGGER_BA_RSP:
	case WCN36XX_HAL_UPDATE_CFG_RSP:
	case WCN36XX_HAL_JOIN_RSP:
	case WCN36XX_HAL_UPDATE_SCAN_PARAM_RSP:
	case WCN36XX_HAL_CH_SWITCH_RSP:
	case WCN36XX_HAL_PROCESS_PTT_RSP:
	case WCN36XX_HAL_FEATURE_CAPS_EXCHANGE_RSP:
	case WCN36XX_HAL_8023_MULTICAST_LIST_RSP:
	case WCN36XX_HAL_START_SCAN_OFFLOAD_RSP:
	case WCN36XX_HAL_STOP_SCAN_OFFLOAD_RSP:
		memcpy(wcn->hal_buf, buf, len);
		wcn->hal_rsp_len = len;
		complete(&wcn->hal_rsp_compl);
		break;

	case WCN36XX_HAL_COEX_IND:
	case WCN36XX_HAL_AVOID_FREQ_RANGE_IND:
	case WCN36XX_HAL_DEL_BA_IND:
	case WCN36XX_HAL_OTA_TX_COMPL_IND:
	case WCN36XX_HAL_MISSED_BEACON_IND:
	case WCN36XX_HAL_DELETE_STA_CONTEXT_IND:
	case WCN36XX_HAL_PRINT_REG_INFO_IND:
	case WCN36XX_HAL_SCAN_OFFLOAD_IND:
		msg_ind = kmalloc(sizeof(*msg_ind) + len, GFP_ATOMIC);
		if (!msg_ind) {
			wcn36xx_err("Run out of memory while handling SMD_EVENT (%d)\n",
				    msg_header->msg_type);
			return -ENOMEM;
		}

		msg_ind->msg_len = len;
		memcpy(msg_ind->msg, buf, len);

		spin_lock(&wcn->hal_ind_lock);
		list_add_tail(&msg_ind->list, &wcn->hal_ind_queue);
		queue_work(wcn->hal_ind_wq, &wcn->hal_ind_work);
		spin_unlock(&wcn->hal_ind_lock);
		wcn36xx_dbg(WCN36XX_DBG_HAL, "indication arrived\n");
		break;
	default:
		wcn36xx_err("SMD_EVENT (%d) not supported\n",
			      msg_header->msg_type);
	}

	return 0;
}

static void wcn36xx_ind_smd_work(struct work_struct *work)
{
	struct wcn36xx *wcn =
		container_of(work, struct wcn36xx, hal_ind_work);

	for (;;) {
		struct wcn36xx_hal_msg_header *msg_header;
		struct wcn36xx_hal_ind_msg *hal_ind_msg;
		unsigned long flags;

		spin_lock_irqsave(&wcn->hal_ind_lock, flags);

		if (list_empty(&wcn->hal_ind_queue)) {
			spin_unlock_irqrestore(&wcn->hal_ind_lock, flags);
			return;
		}

		hal_ind_msg = list_first_entry(&wcn->hal_ind_queue,
					       struct wcn36xx_hal_ind_msg,
					       list);
		list_del(&hal_ind_msg->list);
		spin_unlock_irqrestore(&wcn->hal_ind_lock, flags);

		msg_header = (struct wcn36xx_hal_msg_header *)hal_ind_msg->msg;

		switch (msg_header->msg_type) {
		case WCN36XX_HAL_COEX_IND:
		case WCN36XX_HAL_DEL_BA_IND:
		case WCN36XX_HAL_AVOID_FREQ_RANGE_IND:
			break;
		case WCN36XX_HAL_OTA_TX_COMPL_IND:
			wcn36xx_smd_tx_compl_ind(wcn,
						 hal_ind_msg->msg,
						 hal_ind_msg->msg_len);
			break;
		case WCN36XX_HAL_MISSED_BEACON_IND:
			wcn36xx_smd_missed_beacon_ind(wcn,
						      hal_ind_msg->msg,
						      hal_ind_msg->msg_len);
			break;
		case WCN36XX_HAL_DELETE_STA_CONTEXT_IND:
			wcn36xx_smd_delete_sta_context_ind(wcn,
							   hal_ind_msg->msg,
							   hal_ind_msg->msg_len);
			break;
		case WCN36XX_HAL_PRINT_REG_INFO_IND:
			wcn36xx_smd_print_reg_info_ind(wcn,
						       hal_ind_msg->msg,
						       hal_ind_msg->msg_len);
			break;
		case WCN36XX_HAL_SCAN_OFFLOAD_IND:
			wcn36xx_smd_hw_scan_ind(wcn, hal_ind_msg->msg,
						hal_ind_msg->msg_len);
			break;
		default:
			wcn36xx_err("SMD_EVENT (%d) not supported\n",
				    msg_header->msg_type);
		}

		kfree(hal_ind_msg);
	}
}

int wcn36xx_smd_open(struct wcn36xx *wcn)
{
	wcn->hal_ind_wq = create_freezable_workqueue("wcn36xx_smd_ind");
	if (!wcn->hal_ind_wq)
		return -ENOMEM;

	INIT_WORK(&wcn->hal_ind_work, wcn36xx_ind_smd_work);
	INIT_LIST_HEAD(&wcn->hal_ind_queue);
	spin_lock_init(&wcn->hal_ind_lock);

	return 0;
}

void wcn36xx_smd_close(struct wcn36xx *wcn)
{
	struct wcn36xx_hal_ind_msg *msg, *tmp;

	cancel_work_sync(&wcn->hal_ind_work);
	destroy_workqueue(wcn->hal_ind_wq);

	list_for_each_entry_safe(msg, tmp, &wcn->hal_ind_queue, list)
		kfree(msg);
}
