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

#include <linux/module.h>
#include <linux/firmware.h>
#include <linux/platform_device.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/rpmsg.h>
#include <linux/soc/qcom/smem_state.h>
#include <linux/soc/qcom/wcnss_ctrl.h>
#include <net/ipv6.h>
#include "wcn36xx.h"
#include "testmode.h"

unsigned int wcn36xx_dbg_mask;
module_param_named(debug_mask, wcn36xx_dbg_mask, uint, 0644);
MODULE_PARM_DESC(debug_mask, "Debugging mask");

#define CHAN2G(_freq, _idx) { \
	.band = NL80211_BAND_2GHZ, \
	.center_freq = (_freq), \
	.hw_value = (_idx), \
	.max_power = 25, \
}

#define CHAN5G(_freq, _idx, _phy_val) { \
	.band = NL80211_BAND_5GHZ, \
	.center_freq = (_freq), \
	.hw_value = (_phy_val) << HW_VALUE_PHY_SHIFT | HW_VALUE_CHANNEL(_idx), \
	.max_power = 25, \
}

/* The wcn firmware expects channel values to matching
 * their mnemonic values. So use these for .hw_value. */
static struct ieee80211_channel wcn_2ghz_channels[] = {
	CHAN2G(2412, 1), /* Channel 1 */
	CHAN2G(2417, 2), /* Channel 2 */
	CHAN2G(2422, 3), /* Channel 3 */
	CHAN2G(2427, 4), /* Channel 4 */
	CHAN2G(2432, 5), /* Channel 5 */
	CHAN2G(2437, 6), /* Channel 6 */
	CHAN2G(2442, 7), /* Channel 7 */
	CHAN2G(2447, 8), /* Channel 8 */
	CHAN2G(2452, 9), /* Channel 9 */
	CHAN2G(2457, 10), /* Channel 10 */
	CHAN2G(2462, 11), /* Channel 11 */
	CHAN2G(2467, 12), /* Channel 12 */
	CHAN2G(2472, 13), /* Channel 13 */
	CHAN2G(2484, 14)  /* Channel 14 */

};

static struct ieee80211_channel wcn_5ghz_channels[] = {
	CHAN5G(5180, 36, PHY_QUADRUPLE_CHANNEL_20MHZ_LOW_40MHZ_LOW),
	CHAN5G(5200, 40, PHY_QUADRUPLE_CHANNEL_20MHZ_HIGH_40MHZ_LOW),
	CHAN5G(5220, 44, PHY_QUADRUPLE_CHANNEL_20MHZ_LOW_40MHZ_HIGH),
	CHAN5G(5240, 48, PHY_QUADRUPLE_CHANNEL_20MHZ_HIGH_40MHZ_HIGH),
	CHAN5G(5260, 52, PHY_QUADRUPLE_CHANNEL_20MHZ_LOW_40MHZ_LOW),
	CHAN5G(5280, 56, PHY_QUADRUPLE_CHANNEL_20MHZ_HIGH_40MHZ_LOW),
	CHAN5G(5300, 60, PHY_QUADRUPLE_CHANNEL_20MHZ_LOW_40MHZ_HIGH),
	CHAN5G(5320, 64, PHY_QUADRUPLE_CHANNEL_20MHZ_HIGH_40MHZ_HIGH),
	CHAN5G(5500, 100, PHY_QUADRUPLE_CHANNEL_20MHZ_LOW_40MHZ_LOW),
	CHAN5G(5520, 104, PHY_QUADRUPLE_CHANNEL_20MHZ_HIGH_40MHZ_LOW),
	CHAN5G(5540, 108, PHY_QUADRUPLE_CHANNEL_20MHZ_LOW_40MHZ_HIGH),
	CHAN5G(5560, 112, PHY_QUADRUPLE_CHANNEL_20MHZ_HIGH_40MHZ_HIGH),
	CHAN5G(5580, 116, PHY_QUADRUPLE_CHANNEL_20MHZ_LOW_40MHZ_LOW),
	CHAN5G(5600, 120, PHY_QUADRUPLE_CHANNEL_20MHZ_HIGH_40MHZ_LOW),
	CHAN5G(5620, 124, PHY_QUADRUPLE_CHANNEL_20MHZ_LOW_40MHZ_HIGH),
	CHAN5G(5640, 128, PHY_QUADRUPLE_CHANNEL_20MHZ_HIGH_40MHZ_HIGH),
	CHAN5G(5660, 132, PHY_QUADRUPLE_CHANNEL_20MHZ_LOW_40MHZ_LOW),
	CHAN5G(5680, 136, PHY_QUADRUPLE_CHANNEL_20MHZ_HIGH_40MHZ_LOW),
	CHAN5G(5700, 140, PHY_QUADRUPLE_CHANNEL_20MHZ_LOW_40MHZ_HIGH),
	CHAN5G(5720, 144, PHY_QUADRUPLE_CHANNEL_20MHZ_HIGH_40MHZ_HIGH),
	CHAN5G(5745, 149, PHY_QUADRUPLE_CHANNEL_20MHZ_LOW_40MHZ_LOW),
	CHAN5G(5765, 153, PHY_QUADRUPLE_CHANNEL_20MHZ_HIGH_40MHZ_LOW),
	CHAN5G(5785, 157, PHY_QUADRUPLE_CHANNEL_20MHZ_LOW_40MHZ_HIGH),
	CHAN5G(5805, 161, PHY_QUADRUPLE_CHANNEL_20MHZ_HIGH_40MHZ_HIGH),
	CHAN5G(5825, 165, 0)
};

#define RATE(_bitrate, _hw_rate, _flags) { \
	.bitrate        = (_bitrate),                   \
	.flags          = (_flags),                     \
	.hw_value       = (_hw_rate),                   \
	.hw_value_short = (_hw_rate)  \
}

static struct ieee80211_rate wcn_2ghz_rates[] = {
	RATE(10, HW_RATE_INDEX_1MBPS, 0),
	RATE(20, HW_RATE_INDEX_2MBPS, IEEE80211_RATE_SHORT_PREAMBLE),
	RATE(55, HW_RATE_INDEX_5_5MBPS, IEEE80211_RATE_SHORT_PREAMBLE),
	RATE(110, HW_RATE_INDEX_11MBPS, IEEE80211_RATE_SHORT_PREAMBLE),
	RATE(60, HW_RATE_INDEX_6MBPS, 0),
	RATE(90, HW_RATE_INDEX_9MBPS, 0),
	RATE(120, HW_RATE_INDEX_12MBPS, 0),
	RATE(180, HW_RATE_INDEX_18MBPS, 0),
	RATE(240, HW_RATE_INDEX_24MBPS, 0),
	RATE(360, HW_RATE_INDEX_36MBPS, 0),
	RATE(480, HW_RATE_INDEX_48MBPS, 0),
	RATE(540, HW_RATE_INDEX_54MBPS, 0)
};

static struct ieee80211_rate wcn_5ghz_rates[] = {
	RATE(60, HW_RATE_INDEX_6MBPS, 0),
	RATE(90, HW_RATE_INDEX_9MBPS, 0),
	RATE(120, HW_RATE_INDEX_12MBPS, 0),
	RATE(180, HW_RATE_INDEX_18MBPS, 0),
	RATE(240, HW_RATE_INDEX_24MBPS, 0),
	RATE(360, HW_RATE_INDEX_36MBPS, 0),
	RATE(480, HW_RATE_INDEX_48MBPS, 0),
	RATE(540, HW_RATE_INDEX_54MBPS, 0)
};

static struct ieee80211_supported_band wcn_band_2ghz = {
	.channels	= wcn_2ghz_channels,
	.n_channels	= ARRAY_SIZE(wcn_2ghz_channels),
	.bitrates	= wcn_2ghz_rates,
	.n_bitrates	= ARRAY_SIZE(wcn_2ghz_rates),
	.ht_cap		= {
		.cap =	IEEE80211_HT_CAP_GRN_FLD |
			IEEE80211_HT_CAP_SGI_20 |
			IEEE80211_HT_CAP_DSSSCCK40 |
			IEEE80211_HT_CAP_LSIG_TXOP_PROT |
			IEEE80211_HT_CAP_SGI_40 |
			IEEE80211_HT_CAP_SUP_WIDTH_20_40,
		.ht_supported = true,
		.ampdu_factor = IEEE80211_HT_MAX_AMPDU_64K,
		.ampdu_density = IEEE80211_HT_MPDU_DENSITY_16,
		.mcs = {
			.rx_mask = { 0xff, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
			.rx_highest = cpu_to_le16(72),
			.tx_params = IEEE80211_HT_MCS_TX_DEFINED,
		}
	}
};

static struct ieee80211_supported_band wcn_band_5ghz = {
	.channels	= wcn_5ghz_channels,
	.n_channels	= ARRAY_SIZE(wcn_5ghz_channels),
	.bitrates	= wcn_5ghz_rates,
	.n_bitrates	= ARRAY_SIZE(wcn_5ghz_rates),
	.ht_cap		= {
		.cap =	IEEE80211_HT_CAP_GRN_FLD |
			IEEE80211_HT_CAP_SGI_20 |
			IEEE80211_HT_CAP_DSSSCCK40 |
			IEEE80211_HT_CAP_LSIG_TXOP_PROT |
			IEEE80211_HT_CAP_SGI_40 |
			IEEE80211_HT_CAP_SUP_WIDTH_20_40,
		.ht_supported = true,
		.ampdu_factor = IEEE80211_HT_MAX_AMPDU_64K,
		.ampdu_density = IEEE80211_HT_MPDU_DENSITY_16,
		.mcs = {
			.rx_mask = { 0xff, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
			.rx_highest = cpu_to_le16(150),
			.tx_params = IEEE80211_HT_MCS_TX_DEFINED,
		}
	}
};

#ifdef CONFIG_PM

static const struct wiphy_wowlan_support wowlan_support = {
	.flags = WIPHY_WOWLAN_ANY	|
		 WIPHY_WOWLAN_MAGIC_PKT	|
		 WIPHY_WOWLAN_SUPPORTS_GTK_REKEY
};

#endif

static inline u8 get_sta_index(struct ieee80211_vif *vif,
			       struct wcn36xx_sta *sta_priv)
{
	return NL80211_IFTYPE_STATION == vif->type ?
	       sta_priv->bss_sta_index :
	       sta_priv->sta_index;
}

static const char * const wcn36xx_caps_names[] = {
	"MCC",				/* 0 */
	"P2P",				/* 1 */
	"DOT11AC",			/* 2 */
	"SLM_SESSIONIZATION",		/* 3 */
	"DOT11AC_OPMODE",		/* 4 */
	"SAP32STA",			/* 5 */
	"TDLS",				/* 6 */
	"P2P_GO_NOA_DECOUPLE_INIT_SCAN",/* 7 */
	"WLANACTIVE_OFFLOAD",		/* 8 */
	"BEACON_OFFLOAD",		/* 9 */
	"SCAN_OFFLOAD",			/* 10 */
	"ROAM_OFFLOAD",			/* 11 */
	"BCN_MISS_OFFLOAD",		/* 12 */
	"STA_POWERSAVE",		/* 13 */
	"STA_ADVANCED_PWRSAVE",		/* 14 */
	"AP_UAPSD",			/* 15 */
	"AP_DFS",			/* 16 */
	"BLOCKACK",			/* 17 */
	"PHY_ERR",			/* 18 */
	"BCN_FILTER",			/* 19 */
	"RTT",				/* 20 */
	"RATECTRL",			/* 21 */
	"WOW",				/* 22 */
	"WLAN_ROAM_SCAN_OFFLOAD",	/* 23 */
	"SPECULATIVE_PS_POLL",		/* 24 */
	"SCAN_SCH",			/* 25 */
	"IBSS_HEARTBEAT_OFFLOAD",	/* 26 */
	"WLAN_SCAN_OFFLOAD",		/* 27 */
	"WLAN_PERIODIC_TX_PTRN",	/* 28 */
	"ADVANCE_TDLS",			/* 29 */
	"BATCH_SCAN",			/* 30 */
	"FW_IN_TX_PATH",		/* 31 */
	"EXTENDED_NSOFFLOAD_SLOT",	/* 32 */
	"CH_SWITCH_V1",			/* 33 */
	"HT40_OBSS_SCAN",		/* 34 */
	"UPDATE_CHANNEL_LIST",		/* 35 */
	"WLAN_MCADDR_FLT",		/* 36 */
	"WLAN_CH144",			/* 37 */
	"NAN",				/* 38 */
	"TDLS_SCAN_COEXISTENCE",	/* 39 */
	"LINK_LAYER_STATS_MEAS",	/* 40 */
	"MU_MIMO",			/* 41 */
	"EXTENDED_SCAN",		/* 42 */
	"DYNAMIC_WMM_PS",		/* 43 */
	"MAC_SPOOFED_SCAN",		/* 44 */
	"BMU_ERROR_GENERIC_RECOVERY",	/* 45 */
	"DISA",				/* 46 */
	"FW_STATS",			/* 47 */
	"WPS_PRBRSP_TMPL",		/* 48 */
	"BCN_IE_FLT_DELTA",		/* 49 */
	"TDLS_OFF_CHANNEL",		/* 51 */
	"RTT3",				/* 52 */
	"MGMT_FRAME_LOGGING",		/* 53 */
	"ENHANCED_TXBD_COMPLETION",	/* 54 */
	"LOGGING_ENHANCEMENT",		/* 55 */
	"EXT_SCAN_ENHANCED",		/* 56 */
	"MEMORY_DUMP_SUPPORTED",	/* 57 */
	"PER_PKT_STATS_SUPPORTED",	/* 58 */
	"EXT_LL_STAT",			/* 60 */
	"WIFI_CONFIG",			/* 61 */
	"ANTENNA_DIVERSITY_SELECTION",	/* 62 */
};

static const char *wcn36xx_get_cap_name(enum place_holder_in_cap_bitmap x)
{
	if (x >= ARRAY_SIZE(wcn36xx_caps_names))
		return "UNKNOWN";
	return wcn36xx_caps_names[x];
}

static void wcn36xx_feat_caps_info(struct wcn36xx *wcn)
{
	int i;

	for (i = 0; i < MAX_FEATURE_SUPPORTED; i++) {
		if (get_feat_caps(wcn->fw_feat_caps, i))
			wcn36xx_dbg(WCN36XX_DBG_MAC, "FW Cap %s\n", wcn36xx_get_cap_name(i));
	}
}

static int wcn36xx_start(struct ieee80211_hw *hw)
{
	struct wcn36xx *wcn = hw->priv;
	int ret;

	wcn36xx_dbg(WCN36XX_DBG_MAC, "mac start\n");

	/* SMD initialization */
	ret = wcn36xx_smd_open(wcn);
	if (ret) {
		wcn36xx_err("Failed to open smd channel: %d\n", ret);
		goto out_err;
	}

	/* Allocate memory pools for Mgmt BD headers and Data BD headers */
	ret = wcn36xx_dxe_allocate_mem_pools(wcn);
	if (ret) {
		wcn36xx_err("Failed to alloc DXE mempool: %d\n", ret);
		goto out_smd_close;
	}

	ret = wcn36xx_dxe_alloc_ctl_blks(wcn);
	if (ret) {
		wcn36xx_err("Failed to alloc DXE ctl blocks: %d\n", ret);
		goto out_free_dxe_pool;
	}

	ret = wcn36xx_smd_load_nv(wcn);
	if (ret) {
		wcn36xx_err("Failed to push NV to chip\n");
		goto out_free_dxe_ctl;
	}

	ret = wcn36xx_smd_start(wcn);
	if (ret) {
		wcn36xx_err("Failed to start chip\n");
		goto out_free_dxe_ctl;
	}

	if (!wcn36xx_is_fw_version(wcn, 1, 2, 2, 24)) {
		ret = wcn36xx_smd_feature_caps_exchange(wcn);
		if (ret)
			wcn36xx_warn("Exchange feature caps failed\n");
		else
			wcn36xx_feat_caps_info(wcn);
	}

	/* DMA channel initialization */
	ret = wcn36xx_dxe_init(wcn);
	if (ret) {
		wcn36xx_err("DXE init failed\n");
		goto out_smd_stop;
	}

	wcn36xx_debugfs_init(wcn);

	INIT_LIST_HEAD(&wcn->vif_list);
	spin_lock_init(&wcn->dxe_lock);

	return 0;

out_smd_stop:
	wcn36xx_smd_stop(wcn);
out_free_dxe_ctl:
	wcn36xx_dxe_free_ctl_blks(wcn);
out_free_dxe_pool:
	wcn36xx_dxe_free_mem_pools(wcn);
out_smd_close:
	wcn36xx_smd_close(wcn);
out_err:
	return ret;
}

static void wcn36xx_stop(struct ieee80211_hw *hw)
{
	struct wcn36xx *wcn = hw->priv;

	wcn36xx_dbg(WCN36XX_DBG_MAC, "mac stop\n");

	mutex_lock(&wcn->scan_lock);
	if (wcn->scan_req) {
		struct cfg80211_scan_info scan_info = {
			.aborted = true,
		};

		ieee80211_scan_completed(wcn->hw, &scan_info);
	}
	wcn->scan_req = NULL;
	mutex_unlock(&wcn->scan_lock);

	wcn36xx_debugfs_exit(wcn);
	wcn36xx_smd_stop(wcn);
	wcn36xx_dxe_deinit(wcn);
	wcn36xx_smd_close(wcn);

	wcn36xx_dxe_free_mem_pools(wcn);
	wcn36xx_dxe_free_ctl_blks(wcn);
}

static void wcn36xx_change_ps(struct wcn36xx *wcn, bool enable)
{
	struct ieee80211_vif *vif = NULL;
	struct wcn36xx_vif *tmp;

	list_for_each_entry(tmp, &wcn->vif_list, list) {
		vif = wcn36xx_priv_to_vif(tmp);
		if (enable && !wcn->sw_scan) {
			if (vif->bss_conf.ps) /* ps allowed ? */
				wcn36xx_pmc_enter_bmps_state(wcn, vif);
		} else {
			wcn36xx_pmc_exit_bmps_state(wcn, vif);
		}
	}
}

static void wcn36xx_change_opchannel(struct wcn36xx *wcn, int ch)
{
	struct ieee80211_vif *vif = NULL;
	struct wcn36xx_vif *tmp;

	list_for_each_entry(tmp, &wcn->vif_list, list) {
		vif = wcn36xx_priv_to_vif(tmp);
		wcn36xx_smd_switch_channel(wcn, vif, ch);
	}
}

static int wcn36xx_config(struct ieee80211_hw *hw, u32 changed)
{
	struct wcn36xx *wcn = hw->priv;
	int ret;

	wcn36xx_dbg(WCN36XX_DBG_MAC, "mac config changed 0x%08x\n", changed);

	mutex_lock(&wcn->conf_mutex);

	if (changed & IEEE80211_CONF_CHANGE_CHANNEL) {
		int ch = WCN36XX_HW_CHANNEL(wcn);
		wcn36xx_dbg(WCN36XX_DBG_MAC, "wcn36xx_config channel switch=%d\n",
			    ch);

		if (wcn->sw_scan_opchannel == ch && wcn->sw_scan_channel) {
			/* If channel is the initial operating channel, we may
			 * want to receive/transmit regular data packets, then
			 * simply stop the scan session and exit PS mode.
			 */
			if (wcn->sw_scan_channel)
				wcn36xx_smd_end_scan(wcn, wcn->sw_scan_channel);
			if (wcn->sw_scan_init) {
				wcn36xx_smd_finish_scan(wcn, HAL_SYS_MODE_SCAN,
							wcn->sw_scan_vif);
			}
		} else if (wcn->sw_scan) {
			/* A scan is ongoing, do not change the operating
			 * channel, but start a scan session on the channel.
			 */
			if (wcn->sw_scan_channel)
				wcn36xx_smd_end_scan(wcn, wcn->sw_scan_channel);
			if (!wcn->sw_scan_init) {
				/* This can fail if we are unable to notify the
				 * operating channel.
				 */
				ret = wcn36xx_smd_init_scan(wcn,
							    HAL_SYS_MODE_SCAN,
							    wcn->sw_scan_vif);
				if (ret) {
					mutex_unlock(&wcn->conf_mutex);
					return -EIO;
				}
			}
			wcn36xx_smd_start_scan(wcn, ch);
		} else {
			wcn36xx_change_opchannel(wcn, ch);
		}
	}

	if (changed & IEEE80211_CONF_CHANGE_PS)
		wcn36xx_change_ps(wcn, hw->conf.flags & IEEE80211_CONF_PS);

	if (changed & IEEE80211_CONF_CHANGE_IDLE) {
		if (hw->conf.flags & IEEE80211_CONF_IDLE)
			wcn36xx_smd_enter_imps(wcn);
		else
			wcn36xx_smd_exit_imps(wcn);
	}

	mutex_unlock(&wcn->conf_mutex);

	return 0;
}

static void wcn36xx_configure_filter(struct ieee80211_hw *hw,
				     unsigned int changed,
				     unsigned int *total, u64 multicast)
{
	struct wcn36xx_hal_rcv_flt_mc_addr_list_type *fp;
	struct wcn36xx *wcn = hw->priv;
	struct wcn36xx_vif *tmp;
	struct ieee80211_vif *vif = NULL;

	wcn36xx_dbg(WCN36XX_DBG_MAC, "mac configure filter\n");

	mutex_lock(&wcn->conf_mutex);

	*total &= FIF_ALLMULTI;

	fp = (void *)(unsigned long)multicast;
	list_for_each_entry(tmp, &wcn->vif_list, list) {
		vif = wcn36xx_priv_to_vif(tmp);

		/* FW handles MC filtering only when connected as STA */
		if (*total & FIF_ALLMULTI)
			wcn36xx_smd_set_mc_list(wcn, vif, NULL);
		else if (NL80211_IFTYPE_STATION == vif->type && tmp->sta_assoc)
			wcn36xx_smd_set_mc_list(wcn, vif, fp);
	}

	mutex_unlock(&wcn->conf_mutex);
	kfree(fp);
}

static u64 wcn36xx_prepare_multicast(struct ieee80211_hw *hw,
				     struct netdev_hw_addr_list *mc_list)
{
	struct wcn36xx_hal_rcv_flt_mc_addr_list_type *fp;
	struct netdev_hw_addr *ha;

	wcn36xx_dbg(WCN36XX_DBG_MAC, "mac prepare multicast list\n");
	fp = kzalloc(sizeof(*fp), GFP_ATOMIC);
	if (!fp) {
		wcn36xx_err("Out of memory setting filters.\n");
		return 0;
	}

	fp->mc_addr_count = 0;
	/* update multicast filtering parameters */
	if (netdev_hw_addr_list_count(mc_list) <=
	    WCN36XX_HAL_MAX_NUM_MULTICAST_ADDRESS) {
		netdev_hw_addr_list_for_each(ha, mc_list) {
			memcpy(fp->mc_addr[fp->mc_addr_count],
					ha->addr, ETH_ALEN);
			fp->mc_addr_count++;
		}
	}

	return (u64)(unsigned long)fp;
}

static void wcn36xx_tx(struct ieee80211_hw *hw,
		       struct ieee80211_tx_control *control,
		       struct sk_buff *skb)
{
	struct wcn36xx *wcn = hw->priv;
	struct wcn36xx_sta *sta_priv = NULL;

	if (control->sta)
		sta_priv = wcn36xx_sta_to_priv(control->sta);

	if (wcn36xx_start_tx(wcn, sta_priv, skb))
		ieee80211_free_txskb(wcn->hw, skb);
}

static int wcn36xx_set_key(struct ieee80211_hw *hw, enum set_key_cmd cmd,
			   struct ieee80211_vif *vif,
			   struct ieee80211_sta *sta,
			   struct ieee80211_key_conf *key_conf)
{
	struct wcn36xx *wcn = hw->priv;
	struct wcn36xx_vif *vif_priv = wcn36xx_vif_to_priv(vif);
	struct wcn36xx_sta *sta_priv = sta ? wcn36xx_sta_to_priv(sta) : NULL;
	int ret = 0;
	u8 key[WLAN_MAX_KEY_LEN];

	wcn36xx_dbg(WCN36XX_DBG_MAC, "mac80211 set key\n");
	wcn36xx_dbg(WCN36XX_DBG_MAC, "Key: cmd=0x%x algo:0x%x, id:%d, len:%d flags 0x%x\n",
		    cmd, key_conf->cipher, key_conf->keyidx,
		    key_conf->keylen, key_conf->flags);
	wcn36xx_dbg_dump(WCN36XX_DBG_MAC, "KEY: ",
			 key_conf->key,
			 key_conf->keylen);

	mutex_lock(&wcn->conf_mutex);

	switch (key_conf->cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
		vif_priv->encrypt_type = WCN36XX_HAL_ED_WEP40;
		break;
	case WLAN_CIPHER_SUITE_WEP104:
		vif_priv->encrypt_type = WCN36XX_HAL_ED_WEP104;
		break;
	case WLAN_CIPHER_SUITE_CCMP:
		vif_priv->encrypt_type = WCN36XX_HAL_ED_CCMP;
		break;
	case WLAN_CIPHER_SUITE_TKIP:
		vif_priv->encrypt_type = WCN36XX_HAL_ED_TKIP;
		break;
	default:
		wcn36xx_err("Unsupported key type 0x%x\n",
			      key_conf->cipher);
		ret = -EOPNOTSUPP;
		goto out;
	}

	switch (cmd) {
	case SET_KEY:
		if (WCN36XX_HAL_ED_TKIP == vif_priv->encrypt_type) {
			/*
			 * Supplicant is sending key in the wrong order:
			 * Temporal Key (16 b) - TX MIC (8 b) - RX MIC (8 b)
			 * but HW expects it to be in the order as described in
			 * IEEE 802.11 spec (see chapter 11.7) like this:
			 * Temporal Key (16 b) - RX MIC (8 b) - TX MIC (8 b)
			 */
			memcpy(key, key_conf->key, 16);
			memcpy(key + 16, key_conf->key + 24, 8);
			memcpy(key + 24, key_conf->key + 16, 8);
		} else {
			memcpy(key, key_conf->key, key_conf->keylen);
		}

		if (IEEE80211_KEY_FLAG_PAIRWISE & key_conf->flags) {
			sta_priv->is_data_encrypted = true;
			/* Reconfigure bss with encrypt_type */
			if (NL80211_IFTYPE_STATION == vif->type) {
				wcn36xx_smd_config_bss(wcn,
						       vif,
						       sta,
						       sta->addr,
						       true);
				wcn36xx_smd_config_sta(wcn, vif, sta);
			}

			wcn36xx_smd_set_stakey(wcn,
				vif_priv->encrypt_type,
				key_conf->keyidx,
				key_conf->keylen,
				key,
				get_sta_index(vif, sta_priv));
		} else {
			wcn36xx_smd_set_bsskey(wcn,
				vif_priv->encrypt_type,
				vif_priv->bss_index,
				key_conf->keyidx,
				key_conf->keylen,
				key);

			if ((WLAN_CIPHER_SUITE_WEP40 == key_conf->cipher) ||
			    (WLAN_CIPHER_SUITE_WEP104 == key_conf->cipher)) {
				list_for_each_entry(sta_priv,
						    &vif_priv->sta_list, list) {
					sta_priv->is_data_encrypted = true;
					wcn36xx_smd_set_stakey(wcn,
						vif_priv->encrypt_type,
						key_conf->keyidx,
						key_conf->keylen,
						key,
						get_sta_index(vif, sta_priv));
				}
			}
		}
		break;
	case DISABLE_KEY:
		if (!(IEEE80211_KEY_FLAG_PAIRWISE & key_conf->flags)) {
			if (vif_priv->bss_index != WCN36XX_HAL_BSS_INVALID_IDX)
				wcn36xx_smd_remove_bsskey(wcn,
					vif_priv->encrypt_type,
					vif_priv->bss_index,
					key_conf->keyidx);

			vif_priv->encrypt_type = WCN36XX_HAL_ED_NONE;
		} else {
			sta_priv->is_data_encrypted = false;
			/* do not remove key if disassociated */
			if (sta_priv->aid)
				wcn36xx_smd_remove_stakey(wcn,
					vif_priv->encrypt_type,
					key_conf->keyidx,
					get_sta_index(vif, sta_priv));
		}
		break;
	default:
		wcn36xx_err("Unsupported key cmd 0x%x\n", cmd);
		ret = -EOPNOTSUPP;
		goto out;
	}

out:
	mutex_unlock(&wcn->conf_mutex);

	return ret;
}

static int wcn36xx_hw_scan(struct ieee80211_hw *hw,
			   struct ieee80211_vif *vif,
			   struct ieee80211_scan_request *hw_req)
{
	struct wcn36xx *wcn = hw->priv;

	if (!get_feat_caps(wcn->fw_feat_caps, SCAN_OFFLOAD)) {
		/* fallback to mac80211 software scan */
		return 1;
	}

	/* Firmware scan offload is limited to 48 channels, fallback to
	 * software driven scanning otherwise.
	 */
	if (hw_req->req.n_channels > 48) {
		wcn36xx_warn("Offload scan aborted, n_channels=%u",
			     hw_req->req.n_channels);
		return 1;
	}

	mutex_lock(&wcn->scan_lock);
	if (wcn->scan_req) {
		mutex_unlock(&wcn->scan_lock);
		return -EBUSY;
	}

	wcn->scan_aborted = false;
	wcn->scan_req = &hw_req->req;

	mutex_unlock(&wcn->scan_lock);

	wcn36xx_smd_update_channel_list(wcn, &hw_req->req);
	return wcn36xx_smd_start_hw_scan(wcn, vif, &hw_req->req);
}

static void wcn36xx_cancel_hw_scan(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif)
{
	struct wcn36xx *wcn = hw->priv;

	mutex_lock(&wcn->scan_lock);
	wcn->scan_aborted = true;
	mutex_unlock(&wcn->scan_lock);

	if (get_feat_caps(wcn->fw_feat_caps, SCAN_OFFLOAD)) {
		/* ieee80211_scan_completed will be called on FW scan
		 * indication */
		wcn36xx_smd_stop_hw_scan(wcn);
	}
}

static void wcn36xx_sw_scan_start(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif,
				  const u8 *mac_addr)
{
	struct wcn36xx *wcn = hw->priv;
	struct wcn36xx_vif *vif_priv = wcn36xx_vif_to_priv(vif);

	wcn36xx_dbg(WCN36XX_DBG_MAC, "sw_scan_start");

	wcn->sw_scan = true;
	wcn->sw_scan_vif = vif;
	wcn->sw_scan_channel = 0;
	if (vif_priv->sta_assoc)
		wcn->sw_scan_opchannel = WCN36XX_HW_CHANNEL(wcn);
	else
		wcn->sw_scan_opchannel = 0;
}

static void wcn36xx_sw_scan_complete(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif)
{
	struct wcn36xx *wcn = hw->priv;

	wcn36xx_dbg(WCN36XX_DBG_MAC, "sw_scan_complete");

	/* ensure that any scan session is finished */
	if (wcn->sw_scan_channel)
		wcn36xx_smd_end_scan(wcn, wcn->sw_scan_channel);
	if (wcn->sw_scan_init) {
		wcn36xx_smd_finish_scan(wcn, HAL_SYS_MODE_SCAN,
					wcn->sw_scan_vif);
	}
	wcn->sw_scan = false;
	wcn->sw_scan_opchannel = 0;
}

static void wcn36xx_update_allowed_rates(struct ieee80211_sta *sta,
					 enum nl80211_band band)
{
	int i, size;
	u16 *rates_table;
	struct wcn36xx_sta *sta_priv = wcn36xx_sta_to_priv(sta);
	u32 rates = sta->supp_rates[band];

	memset(&sta_priv->supported_rates, 0,
		sizeof(sta_priv->supported_rates));
	sta_priv->supported_rates.op_rate_mode = STA_11n;

	size = ARRAY_SIZE(sta_priv->supported_rates.dsss_rates);
	rates_table = sta_priv->supported_rates.dsss_rates;
	if (band == NL80211_BAND_2GHZ) {
		for (i = 0; i < size; i++) {
			if (rates & 0x01) {
				rates_table[i] = wcn_2ghz_rates[i].hw_value;
				rates = rates >> 1;
			}
		}
	}

	size = ARRAY_SIZE(sta_priv->supported_rates.ofdm_rates);
	rates_table = sta_priv->supported_rates.ofdm_rates;
	for (i = 0; i < size; i++) {
		if (rates & 0x01) {
			rates_table[i] = wcn_5ghz_rates[i].hw_value;
			rates = rates >> 1;
		}
	}

	if (sta->ht_cap.ht_supported) {
		BUILD_BUG_ON(sizeof(sta->ht_cap.mcs.rx_mask) >
			sizeof(sta_priv->supported_rates.supported_mcs_set));
		memcpy(sta_priv->supported_rates.supported_mcs_set,
		       sta->ht_cap.mcs.rx_mask,
		       sizeof(sta->ht_cap.mcs.rx_mask));
	}

	if (sta->vht_cap.vht_supported) {
		sta_priv->supported_rates.op_rate_mode = STA_11ac;
		sta_priv->supported_rates.vht_rx_mcs_map =
				sta->vht_cap.vht_mcs.rx_mcs_map;
		sta_priv->supported_rates.vht_tx_mcs_map =
				sta->vht_cap.vht_mcs.tx_mcs_map;
	}
}

void wcn36xx_set_default_rates(struct wcn36xx_hal_supported_rates *rates)
{
	u16 ofdm_rates[WCN36XX_HAL_NUM_OFDM_RATES] = {
		HW_RATE_INDEX_6MBPS,
		HW_RATE_INDEX_9MBPS,
		HW_RATE_INDEX_12MBPS,
		HW_RATE_INDEX_18MBPS,
		HW_RATE_INDEX_24MBPS,
		HW_RATE_INDEX_36MBPS,
		HW_RATE_INDEX_48MBPS,
		HW_RATE_INDEX_54MBPS
	};
	u16 dsss_rates[WCN36XX_HAL_NUM_DSSS_RATES] = {
		HW_RATE_INDEX_1MBPS,
		HW_RATE_INDEX_2MBPS,
		HW_RATE_INDEX_5_5MBPS,
		HW_RATE_INDEX_11MBPS
	};

	rates->op_rate_mode = STA_11n;
	memcpy(rates->dsss_rates, dsss_rates,
		sizeof(*dsss_rates) * WCN36XX_HAL_NUM_DSSS_RATES);
	memcpy(rates->ofdm_rates, ofdm_rates,
		sizeof(*ofdm_rates) * WCN36XX_HAL_NUM_OFDM_RATES);
	rates->supported_mcs_set[0] = 0xFF;
}

void wcn36xx_set_default_rates_v1(struct wcn36xx_hal_supported_rates_v1 *rates)
{
	rates->op_rate_mode = STA_11ac;
	rates->vht_rx_mcs_map = IEEE80211_VHT_MCS_SUPPORT_0_9;
	rates->vht_tx_mcs_map = IEEE80211_VHT_MCS_SUPPORT_0_9;
}

static void wcn36xx_bss_info_changed(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif,
				     struct ieee80211_bss_conf *bss_conf,
				     u32 changed)
{
	struct wcn36xx *wcn = hw->priv;
	struct sk_buff *skb = NULL;
	u16 tim_off, tim_len;
	enum wcn36xx_hal_link_state link_state;
	struct wcn36xx_vif *vif_priv = wcn36xx_vif_to_priv(vif);

	wcn36xx_dbg(WCN36XX_DBG_MAC, "mac bss info changed vif %p changed 0x%08x\n",
		    vif, changed);

	mutex_lock(&wcn->conf_mutex);

	if (changed & BSS_CHANGED_BEACON_INFO) {
		wcn36xx_dbg(WCN36XX_DBG_MAC,
			    "mac bss changed dtim period %d\n",
			    bss_conf->dtim_period);

		vif_priv->dtim_period = bss_conf->dtim_period;
	}

	if (changed & BSS_CHANGED_BSSID) {
		wcn36xx_dbg(WCN36XX_DBG_MAC, "mac bss changed_bssid %pM\n",
			    bss_conf->bssid);

		if (!is_zero_ether_addr(bss_conf->bssid)) {
			vif_priv->is_joining = true;
			vif_priv->bss_index = WCN36XX_HAL_BSS_INVALID_IDX;
			wcn36xx_smd_set_link_st(wcn, bss_conf->bssid, vif->addr,
						WCN36XX_HAL_LINK_PREASSOC_STATE);
			wcn36xx_smd_join(wcn, bss_conf->bssid,
					 vif->addr, WCN36XX_HW_CHANNEL(wcn));
			wcn36xx_smd_config_bss(wcn, vif, NULL,
					       bss_conf->bssid, false);
		} else {
			vif_priv->is_joining = false;
			wcn36xx_smd_delete_bss(wcn, vif);
			wcn36xx_smd_set_link_st(wcn, bss_conf->bssid, vif->addr,
						WCN36XX_HAL_LINK_IDLE_STATE);
			vif_priv->encrypt_type = WCN36XX_HAL_ED_NONE;
		}
	}

	if (changed & BSS_CHANGED_SSID) {
		wcn36xx_dbg(WCN36XX_DBG_MAC,
			    "mac bss changed ssid\n");
		wcn36xx_dbg_dump(WCN36XX_DBG_MAC, "ssid ",
				 bss_conf->ssid, bss_conf->ssid_len);

		vif_priv->ssid.length = bss_conf->ssid_len;
		memcpy(&vif_priv->ssid.ssid,
		       bss_conf->ssid,
		       bss_conf->ssid_len);
	}

	if (changed & BSS_CHANGED_ASSOC) {
		vif_priv->is_joining = false;
		if (bss_conf->assoc) {
			struct ieee80211_sta *sta;
			struct wcn36xx_sta *sta_priv;

			wcn36xx_dbg(WCN36XX_DBG_MAC,
				    "mac assoc bss %pM vif %pM AID=%d\n",
				     bss_conf->bssid,
				     vif->addr,
				     bss_conf->aid);

			vif_priv->sta_assoc = true;

			/*
			 * Holding conf_mutex ensures mutal exclusion with
			 * wcn36xx_sta_remove() and as such ensures that sta
			 * won't be freed while we're operating on it. As such
			 * we do not need to hold the rcu_read_lock().
			 */
			sta = ieee80211_find_sta(vif, bss_conf->bssid);
			if (!sta) {
				wcn36xx_err("sta %pM is not found\n",
					      bss_conf->bssid);
				goto out;
			}
			sta_priv = wcn36xx_sta_to_priv(sta);

			wcn36xx_update_allowed_rates(sta, WCN36XX_BAND(wcn));

			wcn36xx_smd_set_link_st(wcn, bss_conf->bssid,
				vif->addr,
				WCN36XX_HAL_LINK_POSTASSOC_STATE);
			wcn36xx_smd_config_bss(wcn, vif, sta,
					       bss_conf->bssid,
					       true);
			sta_priv->aid = bss_conf->aid;
			/*
			 * config_sta must be called from  because this is the
			 * place where AID is available.
			 */
			wcn36xx_smd_config_sta(wcn, vif, sta);
			wcn36xx_enable_keep_alive_null_packet(wcn, vif);
		} else {
			wcn36xx_dbg(WCN36XX_DBG_MAC,
				    "disassociated bss %pM vif %pM AID=%d\n",
				    bss_conf->bssid,
				    vif->addr,
				    bss_conf->aid);
			vif_priv->sta_assoc = false;
			wcn36xx_smd_set_link_st(wcn,
						bss_conf->bssid,
						vif->addr,
						WCN36XX_HAL_LINK_IDLE_STATE);
		}
	}

	if (changed & BSS_CHANGED_AP_PROBE_RESP) {
		wcn36xx_dbg(WCN36XX_DBG_MAC, "mac bss changed ap probe resp\n");
		skb = ieee80211_proberesp_get(hw, vif);
		if (!skb) {
			wcn36xx_err("failed to alloc probereq skb\n");
			goto out;
		}

		wcn36xx_smd_update_proberesp_tmpl(wcn, vif, skb);
		dev_kfree_skb(skb);
	}

	if (changed & BSS_CHANGED_BEACON_ENABLED ||
	    changed & BSS_CHANGED_BEACON) {
		wcn36xx_dbg(WCN36XX_DBG_MAC,
			    "mac bss changed beacon enabled %d\n",
			    bss_conf->enable_beacon);

		if (bss_conf->enable_beacon) {
			vif_priv->dtim_period = bss_conf->dtim_period;
			vif_priv->bss_index = WCN36XX_HAL_BSS_INVALID_IDX;
			wcn36xx_smd_config_bss(wcn, vif, NULL,
					       vif->addr, false);
			skb = ieee80211_beacon_get_tim(hw, vif, &tim_off,
						       &tim_len);
			if (!skb) {
				wcn36xx_err("failed to alloc beacon skb\n");
				goto out;
			}
			wcn36xx_smd_send_beacon(wcn, vif, skb, tim_off, 0);
			dev_kfree_skb(skb);

			if (vif->type == NL80211_IFTYPE_ADHOC ||
			    vif->type == NL80211_IFTYPE_MESH_POINT)
				link_state = WCN36XX_HAL_LINK_IBSS_STATE;
			else
				link_state = WCN36XX_HAL_LINK_AP_STATE;

			wcn36xx_smd_set_link_st(wcn, vif->addr, vif->addr,
						link_state);
		} else {
			wcn36xx_smd_delete_bss(wcn, vif);
			wcn36xx_smd_set_link_st(wcn, vif->addr, vif->addr,
						WCN36XX_HAL_LINK_IDLE_STATE);
		}
	}
out:

	mutex_unlock(&wcn->conf_mutex);
}

/* this is required when using IEEE80211_HW_HAS_RATE_CONTROL */
static int wcn36xx_set_rts_threshold(struct ieee80211_hw *hw, u32 value)
{
	struct wcn36xx *wcn = hw->priv;
	wcn36xx_dbg(WCN36XX_DBG_MAC, "mac set RTS threshold %d\n", value);

	mutex_lock(&wcn->conf_mutex);
	wcn36xx_smd_update_cfg(wcn, WCN36XX_HAL_CFG_RTS_THRESHOLD, value);
	mutex_unlock(&wcn->conf_mutex);

	return 0;
}

static void wcn36xx_remove_interface(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif)
{
	struct wcn36xx *wcn = hw->priv;
	struct wcn36xx_vif *vif_priv = wcn36xx_vif_to_priv(vif);
	wcn36xx_dbg(WCN36XX_DBG_MAC, "mac remove interface vif %p\n", vif);

	mutex_lock(&wcn->conf_mutex);

	list_del(&vif_priv->list);
	wcn36xx_smd_delete_sta_self(wcn, vif->addr);

	mutex_unlock(&wcn->conf_mutex);
}

static int wcn36xx_add_interface(struct ieee80211_hw *hw,
				 struct ieee80211_vif *vif)
{
	struct wcn36xx *wcn = hw->priv;
	struct wcn36xx_vif *vif_priv = wcn36xx_vif_to_priv(vif);

	wcn36xx_dbg(WCN36XX_DBG_MAC, "mac add interface vif %p type %d\n",
		    vif, vif->type);

	if (!(NL80211_IFTYPE_STATION == vif->type ||
	      NL80211_IFTYPE_AP == vif->type ||
	      NL80211_IFTYPE_ADHOC == vif->type ||
	      NL80211_IFTYPE_MESH_POINT == vif->type)) {
		wcn36xx_warn("Unsupported interface type requested: %d\n",
			     vif->type);
		return -EOPNOTSUPP;
	}

	mutex_lock(&wcn->conf_mutex);

	vif_priv->bss_index = WCN36XX_HAL_BSS_INVALID_IDX;
	INIT_LIST_HEAD(&vif_priv->sta_list);
	list_add(&vif_priv->list, &wcn->vif_list);
	wcn36xx_smd_add_sta_self(wcn, vif);

	mutex_unlock(&wcn->conf_mutex);

	return 0;
}

static int wcn36xx_sta_add(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			   struct ieee80211_sta *sta)
{
	struct wcn36xx *wcn = hw->priv;
	struct wcn36xx_vif *vif_priv = wcn36xx_vif_to_priv(vif);
	struct wcn36xx_sta *sta_priv = wcn36xx_sta_to_priv(sta);
	wcn36xx_dbg(WCN36XX_DBG_MAC, "mac sta add vif %p sta %pM\n",
		    vif, sta->addr);

	mutex_lock(&wcn->conf_mutex);

	spin_lock_init(&sta_priv->ampdu_lock);
	sta_priv->vif = vif_priv;
	list_add(&sta_priv->list, &vif_priv->sta_list);

	/*
	 * For STA mode HW will be configured on BSS_CHANGED_ASSOC because
	 * at this stage AID is not available yet.
	 */
	if (NL80211_IFTYPE_STATION != vif->type) {
		wcn36xx_update_allowed_rates(sta, WCN36XX_BAND(wcn));
		sta_priv->aid = sta->aid;
		wcn36xx_smd_config_sta(wcn, vif, sta);
	}

	mutex_unlock(&wcn->conf_mutex);

	return 0;
}

static int wcn36xx_sta_remove(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif,
			      struct ieee80211_sta *sta)
{
	struct wcn36xx *wcn = hw->priv;
	struct wcn36xx_sta *sta_priv = wcn36xx_sta_to_priv(sta);

	wcn36xx_dbg(WCN36XX_DBG_MAC, "mac sta remove vif %p sta %pM index %d\n",
		    vif, sta->addr, sta_priv->sta_index);

	mutex_lock(&wcn->conf_mutex);

	list_del(&sta_priv->list);
	wcn36xx_smd_delete_sta(wcn, sta_priv->sta_index);
	sta_priv->vif = NULL;

	mutex_unlock(&wcn->conf_mutex);

	return 0;
}

#ifdef CONFIG_PM

static struct ieee80211_vif *wcn36xx_get_first_assoc_vif(struct wcn36xx *wcn)
{
	struct wcn36xx_vif *vif_priv = NULL;
	struct ieee80211_vif *vif = NULL;

	list_for_each_entry(vif_priv, &wcn->vif_list, list) {
		if (vif_priv->sta_assoc) {
			vif = wcn36xx_priv_to_vif(vif_priv);
			break;
		}
	}
	return vif;
}

static int wcn36xx_suspend(struct ieee80211_hw *hw, struct cfg80211_wowlan *wow)
{
	struct wcn36xx *wcn = hw->priv;
	struct ieee80211_vif *vif = NULL;
	int ret = 0;

	wcn36xx_dbg(WCN36XX_DBG_MAC, "mac suspend\n");

	mutex_lock(&wcn->conf_mutex);

	vif = wcn36xx_get_first_assoc_vif(wcn);
	if (vif) {
		ret = wcn36xx_smd_arp_offload(wcn, vif, true);
		if (ret)
			goto out;
		ret = wcn36xx_smd_ipv6_ns_offload(wcn, vif, true);
		if (ret)
			goto out;
		ret = wcn36xx_smd_gtk_offload(wcn, vif, true);
		if (ret)
			goto out;
		ret = wcn36xx_smd_set_power_params(wcn, true);
		if (ret)
			goto out;
		ret = wcn36xx_smd_wlan_host_suspend_ind(wcn);
	}

	/* Disable IRQ, we don't want to handle any packet before mac80211 is
	 * resumed and ready to receive packets.
	 */
	disable_irq(wcn->tx_irq);
	disable_irq(wcn->rx_irq);

out:
	mutex_unlock(&wcn->conf_mutex);
	return ret;
}

static int wcn36xx_resume(struct ieee80211_hw *hw)
{
	struct wcn36xx *wcn = hw->priv;
	struct ieee80211_vif *vif = NULL;

	wcn36xx_dbg(WCN36XX_DBG_MAC, "mac resume\n");

	mutex_lock(&wcn->conf_mutex);
	vif = wcn36xx_get_first_assoc_vif(wcn);
	if (vif) {
		wcn36xx_smd_host_resume(wcn);
		wcn36xx_smd_set_power_params(wcn, false);
		wcn36xx_smd_gtk_offload_get_info(wcn, vif);
		wcn36xx_smd_gtk_offload(wcn, vif, false);
		wcn36xx_smd_ipv6_ns_offload(wcn, vif, false);
		wcn36xx_smd_arp_offload(wcn, vif, false);
	}

	enable_irq(wcn->tx_irq);
	enable_irq(wcn->rx_irq);

	mutex_unlock(&wcn->conf_mutex);

	return 0;
}

static void wcn36xx_set_rekey_data(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif,
				   struct cfg80211_gtk_rekey_data *data)
{
	struct wcn36xx *wcn = hw->priv;
	struct wcn36xx_vif *vif_priv = wcn36xx_vif_to_priv(vif);

	mutex_lock(&wcn->conf_mutex);

	memcpy(vif_priv->rekey_data.kek, data->kek, NL80211_KEK_LEN);
	memcpy(vif_priv->rekey_data.kck, data->kck, NL80211_KCK_LEN);
	vif_priv->rekey_data.replay_ctr =
		cpu_to_le64(be64_to_cpup((__be64 *)data->replay_ctr));
	vif_priv->rekey_data.valid = true;

	mutex_unlock(&wcn->conf_mutex);
}

#endif

static int wcn36xx_ampdu_action(struct ieee80211_hw *hw,
		    struct ieee80211_vif *vif,
		    struct ieee80211_ampdu_params *params)
{
	struct wcn36xx *wcn = hw->priv;
	struct wcn36xx_sta *sta_priv = wcn36xx_sta_to_priv(params->sta);
	struct ieee80211_sta *sta = params->sta;
	enum ieee80211_ampdu_mlme_action action = params->action;
	u16 tid = params->tid;
	u16 *ssn = &params->ssn;
	int ret = 0;
	u8 session;

	wcn36xx_dbg(WCN36XX_DBG_MAC, "mac ampdu action action %d tid %d\n",
		    action, tid);

	mutex_lock(&wcn->conf_mutex);

	switch (action) {
	case IEEE80211_AMPDU_RX_START:
		sta_priv->tid = tid;
		session = wcn36xx_smd_add_ba_session(wcn, sta, tid, ssn, 0,
						     get_sta_index(vif, sta_priv));
		wcn36xx_smd_add_ba(wcn, session);
		wcn36xx_smd_trigger_ba(wcn, get_sta_index(vif, sta_priv), tid,
				       session);
		break;
	case IEEE80211_AMPDU_RX_STOP:
		wcn36xx_smd_del_ba(wcn, tid, 0, get_sta_index(vif, sta_priv));
		break;
	case IEEE80211_AMPDU_TX_START:
		spin_lock_bh(&sta_priv->ampdu_lock);
		sta_priv->ampdu_state[tid] = WCN36XX_AMPDU_START;
		spin_unlock_bh(&sta_priv->ampdu_lock);

		ret = IEEE80211_AMPDU_TX_START_IMMEDIATE;
		break;
	case IEEE80211_AMPDU_TX_OPERATIONAL:
		spin_lock_bh(&sta_priv->ampdu_lock);
		sta_priv->ampdu_state[tid] = WCN36XX_AMPDU_OPERATIONAL;
		spin_unlock_bh(&sta_priv->ampdu_lock);

		wcn36xx_smd_add_ba_session(wcn, sta, tid, ssn, 1,
			get_sta_index(vif, sta_priv));
		break;
	case IEEE80211_AMPDU_TX_STOP_FLUSH:
	case IEEE80211_AMPDU_TX_STOP_FLUSH_CONT:
	case IEEE80211_AMPDU_TX_STOP_CONT:
		spin_lock_bh(&sta_priv->ampdu_lock);
		sta_priv->ampdu_state[tid] = WCN36XX_AMPDU_NONE;
		spin_unlock_bh(&sta_priv->ampdu_lock);

		wcn36xx_smd_del_ba(wcn, tid, 1, get_sta_index(vif, sta_priv));
		ieee80211_stop_tx_ba_cb_irqsafe(vif, sta->addr, tid);
		break;
	default:
		wcn36xx_err("Unknown AMPDU action\n");
	}

	mutex_unlock(&wcn->conf_mutex);

	return ret;
}

#if IS_ENABLED(CONFIG_IPV6)
static void wcn36xx_ipv6_addr_change(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif,
				     struct inet6_dev *idev)
{
	struct wcn36xx_vif *vif_priv = wcn36xx_vif_to_priv(vif);
	struct inet6_ifaddr *ifa;
	int idx = 0;

	memset(vif_priv->tentative_addrs, 0, sizeof(vif_priv->tentative_addrs));

	read_lock_bh(&idev->lock);
	list_for_each_entry(ifa, &idev->addr_list, if_list) {
		vif_priv->target_ipv6_addrs[idx] = ifa->addr;
		if (ifa->flags & IFA_F_TENTATIVE)
			__set_bit(idx, vif_priv->tentative_addrs);
		idx++;
		if (idx >= WCN36XX_HAL_IPV6_OFFLOAD_ADDR_MAX)
			break;
		wcn36xx_dbg(WCN36XX_DBG_MAC, "%pI6 %s\n", &ifa->addr,
			    (ifa->flags & IFA_F_TENTATIVE) ? "tentative" : NULL);
	}
	read_unlock_bh(&idev->lock);

	vif_priv->num_target_ipv6_addrs = idx;
}
#endif

static void wcn36xx_flush(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			  u32 queues, bool drop)
{
	struct wcn36xx *wcn = hw->priv;

	if (wcn36xx_dxe_tx_flush(wcn)) {
		wcn36xx_err("Failed to flush hardware tx queues\n");
	}
}

static const struct ieee80211_ops wcn36xx_ops = {
	.start			= wcn36xx_start,
	.stop			= wcn36xx_stop,
	.add_interface		= wcn36xx_add_interface,
	.remove_interface	= wcn36xx_remove_interface,
#ifdef CONFIG_PM
	.suspend		= wcn36xx_suspend,
	.resume			= wcn36xx_resume,
	.set_rekey_data		= wcn36xx_set_rekey_data,
#endif
	.config			= wcn36xx_config,
	.prepare_multicast	= wcn36xx_prepare_multicast,
	.configure_filter       = wcn36xx_configure_filter,
	.tx			= wcn36xx_tx,
	.set_key		= wcn36xx_set_key,
	.hw_scan		= wcn36xx_hw_scan,
	.cancel_hw_scan		= wcn36xx_cancel_hw_scan,
	.sw_scan_start		= wcn36xx_sw_scan_start,
	.sw_scan_complete	= wcn36xx_sw_scan_complete,
	.bss_info_changed	= wcn36xx_bss_info_changed,
	.set_rts_threshold	= wcn36xx_set_rts_threshold,
	.sta_add		= wcn36xx_sta_add,
	.sta_remove		= wcn36xx_sta_remove,
	.ampdu_action		= wcn36xx_ampdu_action,
#if IS_ENABLED(CONFIG_IPV6)
	.ipv6_addr_change	= wcn36xx_ipv6_addr_change,
#endif
	.flush			= wcn36xx_flush,

	CFG80211_TESTMODE_CMD(wcn36xx_tm_cmd)
};

static void
wcn36xx_set_ieee80211_vht_caps(struct ieee80211_sta_vht_cap *vht_cap)
{
	vht_cap->vht_supported = true;

	vht_cap->cap = (IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_3895 |
			IEEE80211_VHT_CAP_SHORT_GI_80 |
			IEEE80211_VHT_CAP_RXSTBC_1 |
			IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE |
			IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE |
			3 << IEEE80211_VHT_CAP_BEAMFORMEE_STS_SHIFT |
			7 << IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_SHIFT);

	vht_cap->vht_mcs.rx_mcs_map =
		cpu_to_le16(IEEE80211_VHT_MCS_SUPPORT_0_9 |
			    IEEE80211_VHT_MCS_NOT_SUPPORTED << 2 |
			    IEEE80211_VHT_MCS_NOT_SUPPORTED << 4 |
			    IEEE80211_VHT_MCS_NOT_SUPPORTED << 6 |
			    IEEE80211_VHT_MCS_NOT_SUPPORTED << 8 |
			    IEEE80211_VHT_MCS_NOT_SUPPORTED << 10 |
			    IEEE80211_VHT_MCS_NOT_SUPPORTED << 12 |
			    IEEE80211_VHT_MCS_NOT_SUPPORTED << 14);

	vht_cap->vht_mcs.rx_highest = cpu_to_le16(433);
	vht_cap->vht_mcs.tx_highest = vht_cap->vht_mcs.rx_highest;

	vht_cap->vht_mcs.tx_mcs_map = vht_cap->vht_mcs.rx_mcs_map;
}

static int wcn36xx_init_ieee80211(struct wcn36xx *wcn)
{
	static const u32 cipher_suites[] = {
		WLAN_CIPHER_SUITE_WEP40,
		WLAN_CIPHER_SUITE_WEP104,
		WLAN_CIPHER_SUITE_TKIP,
		WLAN_CIPHER_SUITE_CCMP,
	};

	ieee80211_hw_set(wcn->hw, TIMING_BEACON_ONLY);
	ieee80211_hw_set(wcn->hw, AMPDU_AGGREGATION);
	ieee80211_hw_set(wcn->hw, SUPPORTS_PS);
	ieee80211_hw_set(wcn->hw, SIGNAL_DBM);
	ieee80211_hw_set(wcn->hw, HAS_RATE_CONTROL);
	ieee80211_hw_set(wcn->hw, SINGLE_SCAN_ON_ALL_BANDS);
	ieee80211_hw_set(wcn->hw, REPORTS_TX_ACK_STATUS);

	wcn->hw->wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION) |
		BIT(NL80211_IFTYPE_AP) |
		BIT(NL80211_IFTYPE_ADHOC) |
		BIT(NL80211_IFTYPE_MESH_POINT);

	wcn->hw->wiphy->bands[NL80211_BAND_2GHZ] = &wcn_band_2ghz;
	if (wcn->rf_id != RF_IRIS_WCN3620)
		wcn->hw->wiphy->bands[NL80211_BAND_5GHZ] = &wcn_band_5ghz;

	if (wcn->rf_id == RF_IRIS_WCN3680)
		wcn36xx_set_ieee80211_vht_caps(&wcn_band_5ghz.vht_cap);

	wcn->hw->wiphy->max_scan_ssids = WCN36XX_MAX_SCAN_SSIDS;
	wcn->hw->wiphy->max_scan_ie_len = WCN36XX_MAX_SCAN_IE_LEN;

	wcn->hw->wiphy->cipher_suites = cipher_suites;
	wcn->hw->wiphy->n_cipher_suites = ARRAY_SIZE(cipher_suites);

#ifdef CONFIG_PM
	wcn->hw->wiphy->wowlan = &wowlan_support;
#endif

	wcn->hw->max_listen_interval = 200;

	wcn->hw->queues = 4;

	SET_IEEE80211_DEV(wcn->hw, wcn->dev);

	wcn->hw->sta_data_size = sizeof(struct wcn36xx_sta);
	wcn->hw->vif_data_size = sizeof(struct wcn36xx_vif);

	wiphy_ext_feature_set(wcn->hw->wiphy,
			      NL80211_EXT_FEATURE_CQM_RSSI_LIST);

	return 0;
}

static int wcn36xx_platform_get_resources(struct wcn36xx *wcn,
					  struct platform_device *pdev)
{
	struct device_node *mmio_node;
	struct device_node *iris_node;
	struct resource *res;
	int index;
	int ret;

	/* Set TX IRQ */
	res = platform_get_resource_byname(pdev, IORESOURCE_IRQ, "tx");
	if (!res) {
		wcn36xx_err("failed to get tx_irq\n");
		return -ENOENT;
	}
	wcn->tx_irq = res->start;

	/* Set RX IRQ */
	res = platform_get_resource_byname(pdev, IORESOURCE_IRQ, "rx");
	if (!res) {
		wcn36xx_err("failed to get rx_irq\n");
		return -ENOENT;
	}
	wcn->rx_irq = res->start;

	/* Acquire SMSM tx enable handle */
	wcn->tx_enable_state = qcom_smem_state_get(&pdev->dev,
			"tx-enable", &wcn->tx_enable_state_bit);
	if (IS_ERR(wcn->tx_enable_state)) {
		wcn36xx_err("failed to get tx-enable state\n");
		return PTR_ERR(wcn->tx_enable_state);
	}

	/* Acquire SMSM tx rings empty handle */
	wcn->tx_rings_empty_state = qcom_smem_state_get(&pdev->dev,
			"tx-rings-empty", &wcn->tx_rings_empty_state_bit);
	if (IS_ERR(wcn->tx_rings_empty_state)) {
		wcn36xx_err("failed to get tx-rings-empty state\n");
		return PTR_ERR(wcn->tx_rings_empty_state);
	}

	mmio_node = of_parse_phandle(pdev->dev.parent->of_node, "qcom,mmio", 0);
	if (!mmio_node) {
		wcn36xx_err("failed to acquire qcom,mmio reference\n");
		return -EINVAL;
	}

	wcn->is_pronto = !!of_device_is_compatible(mmio_node, "qcom,pronto");

	/* Map the CCU memory */
	index = of_property_match_string(mmio_node, "reg-names", "ccu");
	wcn->ccu_base = of_iomap(mmio_node, index);
	if (!wcn->ccu_base) {
		wcn36xx_err("failed to map ccu memory\n");
		ret = -ENOMEM;
		goto put_mmio_node;
	}

	/* Map the DXE memory */
	index = of_property_match_string(mmio_node, "reg-names", "dxe");
	wcn->dxe_base = of_iomap(mmio_node, index);
	if (!wcn->dxe_base) {
		wcn36xx_err("failed to map dxe memory\n");
		ret = -ENOMEM;
		goto unmap_ccu;
	}

	/* External RF module */
	iris_node = of_get_child_by_name(mmio_node, "iris");
	if (iris_node) {
		if (of_device_is_compatible(iris_node, "qcom,wcn3620"))
			wcn->rf_id = RF_IRIS_WCN3620;
		if (of_device_is_compatible(iris_node, "qcom,wcn3680"))
			wcn->rf_id = RF_IRIS_WCN3680;
		of_node_put(iris_node);
	}

	of_node_put(mmio_node);
	return 0;

unmap_ccu:
	iounmap(wcn->ccu_base);
put_mmio_node:
	of_node_put(mmio_node);
	return ret;
}

static int wcn36xx_probe(struct platform_device *pdev)
{
	struct ieee80211_hw *hw;
	struct wcn36xx *wcn;
	void *wcnss;
	int ret;
	const u8 *addr;

	wcn36xx_dbg(WCN36XX_DBG_MAC, "platform probe\n");

	wcnss = dev_get_drvdata(pdev->dev.parent);

	hw = ieee80211_alloc_hw(sizeof(struct wcn36xx), &wcn36xx_ops);
	if (!hw) {
		wcn36xx_err("failed to alloc hw\n");
		ret = -ENOMEM;
		goto out_err;
	}
	platform_set_drvdata(pdev, hw);
	wcn = hw->priv;
	wcn->hw = hw;
	wcn->dev = &pdev->dev;
	wcn->first_boot = true;
	mutex_init(&wcn->conf_mutex);
	mutex_init(&wcn->hal_mutex);
	mutex_init(&wcn->scan_lock);
	__skb_queue_head_init(&wcn->amsdu);

	wcn->hal_buf = devm_kmalloc(wcn->dev, WCN36XX_HAL_BUF_SIZE, GFP_KERNEL);
	if (!wcn->hal_buf) {
		ret = -ENOMEM;
		goto out_wq;
	}

	ret = dma_set_mask_and_coherent(wcn->dev, DMA_BIT_MASK(32));
	if (ret < 0) {
		wcn36xx_err("failed to set DMA mask: %d\n", ret);
		goto out_wq;
	}

	wcn->nv_file = WLAN_NV_FILE;
	ret = of_property_read_string(wcn->dev->parent->of_node, "firmware-name", &wcn->nv_file);
	if (ret < 0 && ret != -EINVAL) {
		wcn36xx_err("failed to read \"firmware-name\" property: %d\n", ret);
		goto out_wq;
	}

	wcn->smd_channel = qcom_wcnss_open_channel(wcnss, "WLAN_CTRL", wcn36xx_smd_rsp_process, hw);
	if (IS_ERR(wcn->smd_channel)) {
		wcn36xx_err("failed to open WLAN_CTRL channel\n");
		ret = PTR_ERR(wcn->smd_channel);
		goto out_wq;
	}

	addr = of_get_property(pdev->dev.of_node, "local-mac-address", &ret);
	if (addr && ret != ETH_ALEN) {
		wcn36xx_err("invalid local-mac-address\n");
		ret = -EINVAL;
		goto out_destroy_ept;
	} else if (addr) {
		wcn36xx_info("mac address: %pM\n", addr);
		SET_IEEE80211_PERM_ADDR(wcn->hw, addr);
	}

	ret = wcn36xx_platform_get_resources(wcn, pdev);
	if (ret)
		goto out_destroy_ept;

	wcn36xx_init_ieee80211(wcn);
	ret = ieee80211_register_hw(wcn->hw);
	if (ret)
		goto out_unmap;

	return 0;

out_unmap:
	iounmap(wcn->ccu_base);
	iounmap(wcn->dxe_base);
out_destroy_ept:
	rpmsg_destroy_ept(wcn->smd_channel);
out_wq:
	ieee80211_free_hw(hw);
out_err:
	return ret;
}

static int wcn36xx_remove(struct platform_device *pdev)
{
	struct ieee80211_hw *hw = platform_get_drvdata(pdev);
	struct wcn36xx *wcn = hw->priv;
	wcn36xx_dbg(WCN36XX_DBG_MAC, "platform remove\n");

	release_firmware(wcn->nv);

	ieee80211_unregister_hw(hw);

	qcom_smem_state_put(wcn->tx_enable_state);
	qcom_smem_state_put(wcn->tx_rings_empty_state);

	rpmsg_destroy_ept(wcn->smd_channel);

	iounmap(wcn->dxe_base);
	iounmap(wcn->ccu_base);

	__skb_queue_purge(&wcn->amsdu);

	mutex_destroy(&wcn->hal_mutex);
	ieee80211_free_hw(hw);

	return 0;
}

static const struct of_device_id wcn36xx_of_match[] = {
	{ .compatible = "qcom,wcnss-wlan" },
	{}
};
MODULE_DEVICE_TABLE(of, wcn36xx_of_match);

static struct platform_driver wcn36xx_driver = {
	.probe      = wcn36xx_probe,
	.remove     = wcn36xx_remove,
	.driver         = {
		.name   = "wcn36xx",
		.of_match_table = wcn36xx_of_match,
	},
};

module_platform_driver(wcn36xx_driver);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Eugene Krasnikov k.eugene.e@gmail.com");
MODULE_FIRMWARE(WLAN_NV_FILE);
