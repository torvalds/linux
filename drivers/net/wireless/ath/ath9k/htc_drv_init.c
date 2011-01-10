/*
 * Copyright (c) 2010 Atheros Communications Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "htc.h"

MODULE_AUTHOR("Atheros Communications");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("Atheros driver 802.11n HTC based wireless devices");

static unsigned int ath9k_debug = ATH_DBG_DEFAULT;
module_param_named(debug, ath9k_debug, uint, 0);
MODULE_PARM_DESC(debug, "Debugging mask");

int htc_modparam_nohwcrypt;
module_param_named(nohwcrypt, htc_modparam_nohwcrypt, int, 0444);
MODULE_PARM_DESC(nohwcrypt, "Disable hardware encryption");

#define CHAN2G(_freq, _idx)  { \
	.center_freq = (_freq), \
	.hw_value = (_idx), \
	.max_power = 20, \
}

#define CHAN5G(_freq, _idx) { \
	.band = IEEE80211_BAND_5GHZ, \
	.center_freq = (_freq), \
	.hw_value = (_idx), \
	.max_power = 20, \
}

#define ATH_HTC_BTCOEX_PRODUCT_ID "wb193"

static struct ieee80211_channel ath9k_2ghz_channels[] = {
	CHAN2G(2412, 0), /* Channel 1 */
	CHAN2G(2417, 1), /* Channel 2 */
	CHAN2G(2422, 2), /* Channel 3 */
	CHAN2G(2427, 3), /* Channel 4 */
	CHAN2G(2432, 4), /* Channel 5 */
	CHAN2G(2437, 5), /* Channel 6 */
	CHAN2G(2442, 6), /* Channel 7 */
	CHAN2G(2447, 7), /* Channel 8 */
	CHAN2G(2452, 8), /* Channel 9 */
	CHAN2G(2457, 9), /* Channel 10 */
	CHAN2G(2462, 10), /* Channel 11 */
	CHAN2G(2467, 11), /* Channel 12 */
	CHAN2G(2472, 12), /* Channel 13 */
	CHAN2G(2484, 13), /* Channel 14 */
};

static struct ieee80211_channel ath9k_5ghz_channels[] = {
	/* _We_ call this UNII 1 */
	CHAN5G(5180, 14), /* Channel 36 */
	CHAN5G(5200, 15), /* Channel 40 */
	CHAN5G(5220, 16), /* Channel 44 */
	CHAN5G(5240, 17), /* Channel 48 */
	/* _We_ call this UNII 2 */
	CHAN5G(5260, 18), /* Channel 52 */
	CHAN5G(5280, 19), /* Channel 56 */
	CHAN5G(5300, 20), /* Channel 60 */
	CHAN5G(5320, 21), /* Channel 64 */
	/* _We_ call this "Middle band" */
	CHAN5G(5500, 22), /* Channel 100 */
	CHAN5G(5520, 23), /* Channel 104 */
	CHAN5G(5540, 24), /* Channel 108 */
	CHAN5G(5560, 25), /* Channel 112 */
	CHAN5G(5580, 26), /* Channel 116 */
	CHAN5G(5600, 27), /* Channel 120 */
	CHAN5G(5620, 28), /* Channel 124 */
	CHAN5G(5640, 29), /* Channel 128 */
	CHAN5G(5660, 30), /* Channel 132 */
	CHAN5G(5680, 31), /* Channel 136 */
	CHAN5G(5700, 32), /* Channel 140 */
	/* _We_ call this UNII 3 */
	CHAN5G(5745, 33), /* Channel 149 */
	CHAN5G(5765, 34), /* Channel 153 */
	CHAN5G(5785, 35), /* Channel 157 */
	CHAN5G(5805, 36), /* Channel 161 */
	CHAN5G(5825, 37), /* Channel 165 */
};

/* Atheros hardware rate code addition for short premble */
#define SHPCHECK(__hw_rate, __flags) \
	((__flags & IEEE80211_RATE_SHORT_PREAMBLE) ? (__hw_rate | 0x04) : 0)

#define RATE(_bitrate, _hw_rate, _flags) {		\
	.bitrate	= (_bitrate),			\
	.flags		= (_flags),			\
	.hw_value	= (_hw_rate),			\
	.hw_value_short = (SHPCHECK(_hw_rate, _flags))	\
}

static struct ieee80211_rate ath9k_legacy_rates[] = {
	RATE(10, 0x1b, 0),
	RATE(20, 0x1a, IEEE80211_RATE_SHORT_PREAMBLE), /* shortp : 0x1e */
	RATE(55, 0x19, IEEE80211_RATE_SHORT_PREAMBLE), /* shortp: 0x1d */
	RATE(110, 0x18, IEEE80211_RATE_SHORT_PREAMBLE), /* short: 0x1c */
	RATE(60, 0x0b, 0),
	RATE(90, 0x0f, 0),
	RATE(120, 0x0a, 0),
	RATE(180, 0x0e, 0),
	RATE(240, 0x09, 0),
	RATE(360, 0x0d, 0),
	RATE(480, 0x08, 0),
	RATE(540, 0x0c, 0),
};

static int ath9k_htc_wait_for_target(struct ath9k_htc_priv *priv)
{
	int time_left;

	if (atomic_read(&priv->htc->tgt_ready) > 0) {
		atomic_dec(&priv->htc->tgt_ready);
		return 0;
	}

	/* Firmware can take up to 50ms to get ready, to be safe use 1 second */
	time_left = wait_for_completion_timeout(&priv->htc->target_wait, HZ);
	if (!time_left) {
		dev_err(priv->dev, "ath9k_htc: Target is unresponsive\n");
		return -ETIMEDOUT;
	}

	atomic_dec(&priv->htc->tgt_ready);

	return 0;
}

static void ath9k_deinit_priv(struct ath9k_htc_priv *priv)
{
	ath9k_htc_exit_debug(priv->ah);
	ath9k_hw_deinit(priv->ah);
	tasklet_kill(&priv->wmi_tasklet);
	tasklet_kill(&priv->rx_tasklet);
	tasklet_kill(&priv->tx_tasklet);
	kfree(priv->ah);
	priv->ah = NULL;
}

static void ath9k_deinit_device(struct ath9k_htc_priv *priv)
{
	struct ieee80211_hw *hw = priv->hw;

	wiphy_rfkill_stop_polling(hw->wiphy);
	ath9k_deinit_leds(priv);
	ieee80211_unregister_hw(hw);
	ath9k_rx_cleanup(priv);
	ath9k_tx_cleanup(priv);
	ath9k_deinit_priv(priv);
}

static inline int ath9k_htc_connect_svc(struct ath9k_htc_priv *priv,
					u16 service_id,
					void (*tx) (void *,
						    struct sk_buff *,
						    enum htc_endpoint_id,
						    bool txok),
					enum htc_endpoint_id *ep_id)
{
	struct htc_service_connreq req;

	memset(&req, 0, sizeof(struct htc_service_connreq));

	req.service_id = service_id;
	req.ep_callbacks.priv = priv;
	req.ep_callbacks.rx = ath9k_htc_rxep;
	req.ep_callbacks.tx = tx;

	return htc_connect_service(priv->htc, &req, ep_id);
}

static int ath9k_init_htc_services(struct ath9k_htc_priv *priv, u16 devid)
{
	int ret;

	/* WMI CMD*/
	ret = ath9k_wmi_connect(priv->htc, priv->wmi, &priv->wmi_cmd_ep);
	if (ret)
		goto err;

	/* Beacon */
	ret = ath9k_htc_connect_svc(priv, WMI_BEACON_SVC, ath9k_htc_beaconep,
				    &priv->beacon_ep);
	if (ret)
		goto err;

	/* CAB */
	ret = ath9k_htc_connect_svc(priv, WMI_CAB_SVC, ath9k_htc_txep,
				    &priv->cab_ep);
	if (ret)
		goto err;


	/* UAPSD */
	ret = ath9k_htc_connect_svc(priv, WMI_UAPSD_SVC, ath9k_htc_txep,
				    &priv->uapsd_ep);
	if (ret)
		goto err;

	/* MGMT */
	ret = ath9k_htc_connect_svc(priv, WMI_MGMT_SVC, ath9k_htc_txep,
				    &priv->mgmt_ep);
	if (ret)
		goto err;

	/* DATA BE */
	ret = ath9k_htc_connect_svc(priv, WMI_DATA_BE_SVC, ath9k_htc_txep,
				    &priv->data_be_ep);
	if (ret)
		goto err;

	/* DATA BK */
	ret = ath9k_htc_connect_svc(priv, WMI_DATA_BK_SVC, ath9k_htc_txep,
				    &priv->data_bk_ep);
	if (ret)
		goto err;

	/* DATA VI */
	ret = ath9k_htc_connect_svc(priv, WMI_DATA_VI_SVC, ath9k_htc_txep,
				    &priv->data_vi_ep);
	if (ret)
		goto err;

	/* DATA VO */
	ret = ath9k_htc_connect_svc(priv, WMI_DATA_VO_SVC, ath9k_htc_txep,
				    &priv->data_vo_ep);
	if (ret)
		goto err;

	/*
	 * Setup required credits before initializing HTC.
	 * This is a bit hacky, but, since queuing is done in
	 * the HIF layer, shouldn't matter much.
	 */

	switch(devid) {
	case 0x7010:
	case 0x7015:
	case 0x9018:
	case 0xA704:
	case 0x1200:
		priv->htc->credits = 45;
		break;
	default:
		priv->htc->credits = 33;
	}

	ret = htc_init(priv->htc);
	if (ret)
		goto err;

	dev_info(priv->dev, "ath9k_htc: HTC initialized with %d credits\n",
		 priv->htc->credits);

	return 0;

err:
	dev_err(priv->dev, "ath9k_htc: Unable to initialize HTC services\n");
	return ret;
}

static int ath9k_reg_notifier(struct wiphy *wiphy,
			      struct regulatory_request *request)
{
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct ath9k_htc_priv *priv = hw->priv;

	return ath_reg_notifier_apply(wiphy, request,
				      ath9k_hw_regulatory(priv->ah));
}

static unsigned int ath9k_regread(void *hw_priv, u32 reg_offset)
{
	struct ath_hw *ah = (struct ath_hw *) hw_priv;
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath9k_htc_priv *priv = (struct ath9k_htc_priv *) common->priv;
	__be32 val, reg = cpu_to_be32(reg_offset);
	int r;

	r = ath9k_wmi_cmd(priv->wmi, WMI_REG_READ_CMDID,
			  (u8 *) &reg, sizeof(reg),
			  (u8 *) &val, sizeof(val),
			  100);
	if (unlikely(r)) {
		ath_print(common, ATH_DBG_WMI,
			  "REGISTER READ FAILED: (0x%04x, %d)\n",
			   reg_offset, r);
		return -EIO;
	}

	return be32_to_cpu(val);
}

static void ath9k_regwrite_single(void *hw_priv, u32 val, u32 reg_offset)
{
	struct ath_hw *ah = (struct ath_hw *) hw_priv;
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath9k_htc_priv *priv = (struct ath9k_htc_priv *) common->priv;
	__be32 buf[2] = {
		cpu_to_be32(reg_offset),
		cpu_to_be32(val),
	};
	int r;

	r = ath9k_wmi_cmd(priv->wmi, WMI_REG_WRITE_CMDID,
			  (u8 *) &buf, sizeof(buf),
			  (u8 *) &val, sizeof(val),
			  100);
	if (unlikely(r)) {
		ath_print(common, ATH_DBG_WMI,
			  "REGISTER WRITE FAILED:(0x%04x, %d)\n",
			  reg_offset, r);
	}
}

static void ath9k_regwrite_buffer(void *hw_priv, u32 val, u32 reg_offset)
{
	struct ath_hw *ah = (struct ath_hw *) hw_priv;
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath9k_htc_priv *priv = (struct ath9k_htc_priv *) common->priv;
	u32 rsp_status;
	int r;

	mutex_lock(&priv->wmi->multi_write_mutex);

	/* Store the register/value */
	priv->wmi->multi_write[priv->wmi->multi_write_idx].reg =
		cpu_to_be32(reg_offset);
	priv->wmi->multi_write[priv->wmi->multi_write_idx].val =
		cpu_to_be32(val);

	priv->wmi->multi_write_idx++;

	/* If the buffer is full, send it out. */
	if (priv->wmi->multi_write_idx == MAX_CMD_NUMBER) {
		r = ath9k_wmi_cmd(priv->wmi, WMI_REG_WRITE_CMDID,
			  (u8 *) &priv->wmi->multi_write,
			  sizeof(struct register_write) * priv->wmi->multi_write_idx,
			  (u8 *) &rsp_status, sizeof(rsp_status),
			  100);
		if (unlikely(r)) {
			ath_print(common, ATH_DBG_WMI,
				  "REGISTER WRITE FAILED, multi len: %d\n",
				  priv->wmi->multi_write_idx);
		}
		priv->wmi->multi_write_idx = 0;
	}

	mutex_unlock(&priv->wmi->multi_write_mutex);
}

static void ath9k_regwrite(void *hw_priv, u32 val, u32 reg_offset)
{
	struct ath_hw *ah = (struct ath_hw *) hw_priv;
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath9k_htc_priv *priv = (struct ath9k_htc_priv *) common->priv;

	if (atomic_read(&priv->wmi->mwrite_cnt))
		ath9k_regwrite_buffer(hw_priv, val, reg_offset);
	else
		ath9k_regwrite_single(hw_priv, val, reg_offset);
}

static void ath9k_enable_regwrite_buffer(void *hw_priv)
{
	struct ath_hw *ah = (struct ath_hw *) hw_priv;
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath9k_htc_priv *priv = (struct ath9k_htc_priv *) common->priv;

	atomic_inc(&priv->wmi->mwrite_cnt);
}

static void ath9k_regwrite_flush(void *hw_priv)
{
	struct ath_hw *ah = (struct ath_hw *) hw_priv;
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath9k_htc_priv *priv = (struct ath9k_htc_priv *) common->priv;
	u32 rsp_status;
	int r;

	atomic_dec(&priv->wmi->mwrite_cnt);

	mutex_lock(&priv->wmi->multi_write_mutex);

	if (priv->wmi->multi_write_idx) {
		r = ath9k_wmi_cmd(priv->wmi, WMI_REG_WRITE_CMDID,
			  (u8 *) &priv->wmi->multi_write,
			  sizeof(struct register_write) * priv->wmi->multi_write_idx,
			  (u8 *) &rsp_status, sizeof(rsp_status),
			  100);
		if (unlikely(r)) {
			ath_print(common, ATH_DBG_WMI,
				  "REGISTER WRITE FAILED, multi len: %d\n",
				  priv->wmi->multi_write_idx);
		}
		priv->wmi->multi_write_idx = 0;
	}

	mutex_unlock(&priv->wmi->multi_write_mutex);
}

static const struct ath_ops ath9k_common_ops = {
	.read = ath9k_regread,
	.write = ath9k_regwrite,
	.enable_write_buffer = ath9k_enable_regwrite_buffer,
	.write_flush = ath9k_regwrite_flush,
};

static void ath_usb_read_cachesize(struct ath_common *common, int *csz)
{
	*csz = L1_CACHE_BYTES >> 2;
}

static bool ath_usb_eeprom_read(struct ath_common *common, u32 off, u16 *data)
{
	struct ath_hw *ah = (struct ath_hw *) common->ah;

	(void)REG_READ(ah, AR5416_EEPROM_OFFSET + (off << AR5416_EEPROM_S));

	if (!ath9k_hw_wait(ah,
			   AR_EEPROM_STATUS_DATA,
			   AR_EEPROM_STATUS_DATA_BUSY |
			   AR_EEPROM_STATUS_DATA_PROT_ACCESS, 0,
			   AH_WAIT_TIMEOUT))
		return false;

	*data = MS(REG_READ(ah, AR_EEPROM_STATUS_DATA),
		   AR_EEPROM_STATUS_DATA_VAL);

	return true;
}

static const struct ath_bus_ops ath9k_usb_bus_ops = {
	.ath_bus_type = ATH_USB,
	.read_cachesize = ath_usb_read_cachesize,
	.eeprom_read = ath_usb_eeprom_read,
};

static void setup_ht_cap(struct ath9k_htc_priv *priv,
			 struct ieee80211_sta_ht_cap *ht_info)
{
	struct ath_common *common = ath9k_hw_common(priv->ah);
	u8 tx_streams, rx_streams;
	int i;

	ht_info->ht_supported = true;
	ht_info->cap = IEEE80211_HT_CAP_SUP_WIDTH_20_40 |
		       IEEE80211_HT_CAP_SM_PS |
		       IEEE80211_HT_CAP_SGI_40 |
		       IEEE80211_HT_CAP_DSSSCCK40;

	if (priv->ah->caps.hw_caps & ATH9K_HW_CAP_SGI_20)
		ht_info->cap |= IEEE80211_HT_CAP_SGI_20;

	ht_info->cap |= (1 << IEEE80211_HT_CAP_RX_STBC_SHIFT);

	ht_info->ampdu_factor = IEEE80211_HT_MAX_AMPDU_64K;
	ht_info->ampdu_density = IEEE80211_HT_MPDU_DENSITY_8;

	memset(&ht_info->mcs, 0, sizeof(ht_info->mcs));

	/* ath9k_htc supports only 1 or 2 stream devices */
	tx_streams = ath9k_cmn_count_streams(common->tx_chainmask, 2);
	rx_streams = ath9k_cmn_count_streams(common->rx_chainmask, 2);

	ath_print(common, ATH_DBG_CONFIG,
		  "TX streams %d, RX streams: %d\n",
		  tx_streams, rx_streams);

	if (tx_streams != rx_streams) {
		ht_info->mcs.tx_params |= IEEE80211_HT_MCS_TX_RX_DIFF;
		ht_info->mcs.tx_params |= ((tx_streams - 1) <<
					   IEEE80211_HT_MCS_TX_MAX_STREAMS_SHIFT);
	}

	for (i = 0; i < rx_streams; i++)
		ht_info->mcs.rx_mask[i] = 0xff;

	ht_info->mcs.tx_params |= IEEE80211_HT_MCS_TX_DEFINED;
}

static int ath9k_init_queues(struct ath9k_htc_priv *priv)
{
	struct ath_common *common = ath9k_hw_common(priv->ah);
	int i;

	for (i = 0; i < ARRAY_SIZE(priv->hwq_map); i++)
		priv->hwq_map[i] = -1;

	priv->beaconq = ath9k_hw_beaconq_setup(priv->ah);
	if (priv->beaconq == -1) {
		ath_print(common, ATH_DBG_FATAL,
			  "Unable to setup BEACON xmit queue\n");
		goto err;
	}

	priv->cabq = ath9k_htc_cabq_setup(priv);
	if (priv->cabq == -1) {
		ath_print(common, ATH_DBG_FATAL,
			  "Unable to setup CAB xmit queue\n");
		goto err;
	}

	if (!ath9k_htc_txq_setup(priv, WME_AC_BE)) {
		ath_print(common, ATH_DBG_FATAL,
			  "Unable to setup xmit queue for BE traffic\n");
		goto err;
	}

	if (!ath9k_htc_txq_setup(priv, WME_AC_BK)) {
		ath_print(common, ATH_DBG_FATAL,
			  "Unable to setup xmit queue for BK traffic\n");
		goto err;
	}
	if (!ath9k_htc_txq_setup(priv, WME_AC_VI)) {
		ath_print(common, ATH_DBG_FATAL,
			  "Unable to setup xmit queue for VI traffic\n");
		goto err;
	}
	if (!ath9k_htc_txq_setup(priv, WME_AC_VO)) {
		ath_print(common, ATH_DBG_FATAL,
			  "Unable to setup xmit queue for VO traffic\n");
		goto err;
	}

	return 0;

err:
	return -EINVAL;
}

static void ath9k_init_crypto(struct ath9k_htc_priv *priv)
{
	struct ath_common *common = ath9k_hw_common(priv->ah);
	int i = 0;

	/* Get the hardware key cache size. */
	common->keymax = priv->ah->caps.keycache_size;
	if (common->keymax > ATH_KEYMAX) {
		ath_print(common, ATH_DBG_ANY,
			  "Warning, using only %u entries in %u key cache\n",
			  ATH_KEYMAX, common->keymax);
		common->keymax = ATH_KEYMAX;
	}

	if (priv->ah->misc_mode & AR_PCU_MIC_NEW_LOC_ENA)
		common->crypt_caps |= ATH_CRYPT_CAP_MIC_COMBINED;

	/*
	 * Reset the key cache since some parts do not
	 * reset the contents on initial power up.
	 */
	for (i = 0; i < common->keymax; i++)
		ath_hw_keyreset(common, (u16) i);
}

static void ath9k_init_channels_rates(struct ath9k_htc_priv *priv)
{
	if (priv->ah->caps.hw_caps & ATH9K_HW_CAP_2GHZ) {
		priv->sbands[IEEE80211_BAND_2GHZ].channels =
			ath9k_2ghz_channels;
		priv->sbands[IEEE80211_BAND_2GHZ].band = IEEE80211_BAND_2GHZ;
		priv->sbands[IEEE80211_BAND_2GHZ].n_channels =
			ARRAY_SIZE(ath9k_2ghz_channels);
		priv->sbands[IEEE80211_BAND_2GHZ].bitrates = ath9k_legacy_rates;
		priv->sbands[IEEE80211_BAND_2GHZ].n_bitrates =
			ARRAY_SIZE(ath9k_legacy_rates);
	}

	if (priv->ah->caps.hw_caps & ATH9K_HW_CAP_5GHZ) {
		priv->sbands[IEEE80211_BAND_5GHZ].channels = ath9k_5ghz_channels;
		priv->sbands[IEEE80211_BAND_5GHZ].band = IEEE80211_BAND_5GHZ;
		priv->sbands[IEEE80211_BAND_5GHZ].n_channels =
			ARRAY_SIZE(ath9k_5ghz_channels);
		priv->sbands[IEEE80211_BAND_5GHZ].bitrates =
			ath9k_legacy_rates + 4;
		priv->sbands[IEEE80211_BAND_5GHZ].n_bitrates =
			ARRAY_SIZE(ath9k_legacy_rates) - 4;
	}
}

static void ath9k_init_misc(struct ath9k_htc_priv *priv)
{
	struct ath_common *common = ath9k_hw_common(priv->ah);

	common->tx_chainmask = priv->ah->caps.tx_chainmask;
	common->rx_chainmask = priv->ah->caps.rx_chainmask;

	memcpy(common->bssidmask, ath_bcast_mac, ETH_ALEN);

	priv->ah->opmode = NL80211_IFTYPE_STATION;
}

static void ath9k_init_btcoex(struct ath9k_htc_priv *priv)
{
	int qnum;

	switch (priv->ah->btcoex_hw.scheme) {
	case ATH_BTCOEX_CFG_NONE:
		break;
	case ATH_BTCOEX_CFG_3WIRE:
		priv->ah->btcoex_hw.btactive_gpio = 7;
		priv->ah->btcoex_hw.btpriority_gpio = 6;
		priv->ah->btcoex_hw.wlanactive_gpio = 8;
		priv->btcoex.bt_stomp_type = ATH_BTCOEX_STOMP_LOW;
		ath9k_hw_btcoex_init_3wire(priv->ah);
		ath_htc_init_btcoex_work(priv);
		qnum = priv->hwq_map[WME_AC_BE];
		ath9k_hw_init_btcoex_hw(priv->ah, qnum);
		break;
	default:
		WARN_ON(1);
		break;
	}
}

static int ath9k_init_priv(struct ath9k_htc_priv *priv,
			   u16 devid, char *product)
{
	struct ath_hw *ah = NULL;
	struct ath_common *common;
	int ret = 0, csz = 0;

	priv->op_flags |= OP_INVALID;

	ah = kzalloc(sizeof(struct ath_hw), GFP_KERNEL);
	if (!ah)
		return -ENOMEM;

	ah->hw_version.devid = devid;
	ah->hw_version.subsysid = 0; /* FIXME */
	priv->ah = ah;

	common = ath9k_hw_common(ah);
	common->ops = &ath9k_common_ops;
	common->bus_ops = &ath9k_usb_bus_ops;
	common->ah = ah;
	common->hw = priv->hw;
	common->priv = priv;
	common->debug_mask = ath9k_debug;

	spin_lock_init(&priv->wmi->wmi_lock);
	spin_lock_init(&priv->beacon_lock);
	spin_lock_init(&priv->tx_lock);
	mutex_init(&priv->mutex);
	mutex_init(&priv->htc_pm_lock);
	tasklet_init(&priv->wmi_tasklet, ath9k_wmi_tasklet,
		     (unsigned long)priv);
	tasklet_init(&priv->rx_tasklet, ath9k_rx_tasklet,
		     (unsigned long)priv);
	tasklet_init(&priv->tx_tasklet, ath9k_tx_tasklet, (unsigned long)priv);
	INIT_DELAYED_WORK(&priv->ath9k_ani_work, ath9k_ani_work);
	INIT_WORK(&priv->ps_work, ath9k_ps_work);

	/*
	 * Cache line size is used to size and align various
	 * structures used to communicate with the hardware.
	 */
	ath_read_cachesize(common, &csz);
	common->cachelsz = csz << 2; /* convert to bytes */

	ret = ath9k_hw_init(ah);
	if (ret) {
		ath_print(common, ATH_DBG_FATAL,
			  "Unable to initialize hardware; "
			  "initialization status: %d\n", ret);
		goto err_hw;
	}

	ret = ath9k_htc_init_debug(ah);
	if (ret) {
		ath_print(common, ATH_DBG_FATAL,
			  "Unable to create debugfs files\n");
		goto err_debug;
	}

	ret = ath9k_init_queues(priv);
	if (ret)
		goto err_queues;

	ath9k_init_crypto(priv);
	ath9k_init_channels_rates(priv);
	ath9k_init_misc(priv);

	if (product && strncmp(product, ATH_HTC_BTCOEX_PRODUCT_ID, 5) == 0) {
		ah->btcoex_hw.scheme = ATH_BTCOEX_CFG_3WIRE;
		ath9k_init_btcoex(priv);
	}

	return 0;

err_queues:
	ath9k_htc_exit_debug(ah);
err_debug:
	ath9k_hw_deinit(ah);
err_hw:

	kfree(ah);
	priv->ah = NULL;

	return ret;
}

static void ath9k_set_hw_capab(struct ath9k_htc_priv *priv,
			       struct ieee80211_hw *hw)
{
	struct ath_common *common = ath9k_hw_common(priv->ah);

	hw->flags = IEEE80211_HW_SIGNAL_DBM |
		IEEE80211_HW_AMPDU_AGGREGATION |
		IEEE80211_HW_SPECTRUM_MGMT |
		IEEE80211_HW_HAS_RATE_CONTROL |
		IEEE80211_HW_RX_INCLUDES_FCS |
		IEEE80211_HW_SUPPORTS_PS |
		IEEE80211_HW_PS_NULLFUNC_STACK;

	hw->wiphy->interface_modes =
		BIT(NL80211_IFTYPE_STATION) |
		BIT(NL80211_IFTYPE_ADHOC);

	hw->wiphy->flags &= ~WIPHY_FLAG_PS_ON_BY_DEFAULT;

	hw->queues = 4;
	hw->channel_change_time = 5000;
	hw->max_listen_interval = 10;
	hw->vif_data_size = sizeof(struct ath9k_htc_vif);
	hw->sta_data_size = sizeof(struct ath9k_htc_sta);

	/* tx_frame_hdr is larger than tx_mgmt_hdr anyway */
	hw->extra_tx_headroom = sizeof(struct tx_frame_hdr) +
		sizeof(struct htc_frame_hdr) + 4;

	if (priv->ah->caps.hw_caps & ATH9K_HW_CAP_2GHZ)
		hw->wiphy->bands[IEEE80211_BAND_2GHZ] =
			&priv->sbands[IEEE80211_BAND_2GHZ];
	if (priv->ah->caps.hw_caps & ATH9K_HW_CAP_5GHZ)
		hw->wiphy->bands[IEEE80211_BAND_5GHZ] =
			&priv->sbands[IEEE80211_BAND_5GHZ];

	if (priv->ah->caps.hw_caps & ATH9K_HW_CAP_HT) {
		if (priv->ah->caps.hw_caps & ATH9K_HW_CAP_2GHZ)
			setup_ht_cap(priv,
				     &priv->sbands[IEEE80211_BAND_2GHZ].ht_cap);
		if (priv->ah->caps.hw_caps & ATH9K_HW_CAP_5GHZ)
			setup_ht_cap(priv,
				     &priv->sbands[IEEE80211_BAND_5GHZ].ht_cap);
	}

	SET_IEEE80211_PERM_ADDR(hw, common->macaddr);
}

static int ath9k_init_device(struct ath9k_htc_priv *priv,
			     u16 devid, char *product)
{
	struct ieee80211_hw *hw = priv->hw;
	struct ath_common *common;
	struct ath_hw *ah;
	int error = 0;
	struct ath_regulatory *reg;

	/* Bring up device */
	error = ath9k_init_priv(priv, devid, product);
	if (error != 0)
		goto err_init;

	ah = priv->ah;
	common = ath9k_hw_common(ah);
	ath9k_set_hw_capab(priv, hw);

	/* Initialize regulatory */
	error = ath_regd_init(&common->regulatory, priv->hw->wiphy,
			      ath9k_reg_notifier);
	if (error)
		goto err_regd;

	reg = &common->regulatory;

	/* Setup TX */
	error = ath9k_tx_init(priv);
	if (error != 0)
		goto err_tx;

	/* Setup RX */
	error = ath9k_rx_init(priv);
	if (error != 0)
		goto err_rx;

	/* Register with mac80211 */
	error = ieee80211_register_hw(hw);
	if (error)
		goto err_register;

	/* Handle world regulatory */
	if (!ath_is_world_regd(reg)) {
		error = regulatory_hint(hw->wiphy, reg->alpha2);
		if (error)
			goto err_world;
	}

	ath9k_init_leds(priv);
	ath9k_start_rfkill_poll(priv);

	return 0;

err_world:
	ieee80211_unregister_hw(hw);
err_register:
	ath9k_rx_cleanup(priv);
err_rx:
	ath9k_tx_cleanup(priv);
err_tx:
	/* Nothing */
err_regd:
	ath9k_deinit_priv(priv);
err_init:
	return error;
}

int ath9k_htc_probe_device(struct htc_target *htc_handle, struct device *dev,
			   u16 devid, char *product)
{
	struct ieee80211_hw *hw;
	struct ath9k_htc_priv *priv;
	int ret;

	hw = ieee80211_alloc_hw(sizeof(struct ath9k_htc_priv), &ath9k_htc_ops);
	if (!hw)
		return -ENOMEM;

	priv = hw->priv;
	priv->hw = hw;
	priv->htc = htc_handle;
	priv->dev = dev;
	htc_handle->drv_priv = priv;
	SET_IEEE80211_DEV(hw, priv->dev);

	ret = ath9k_htc_wait_for_target(priv);
	if (ret)
		goto err_free;

	priv->wmi = ath9k_init_wmi(priv);
	if (!priv->wmi) {
		ret = -EINVAL;
		goto err_free;
	}

	ret = ath9k_init_htc_services(priv, devid);
	if (ret)
		goto err_init;

	/* The device may have been unplugged earlier. */
	priv->op_flags &= ~OP_UNPLUGGED;

	ret = ath9k_init_device(priv, devid, product);
	if (ret)
		goto err_init;

	return 0;

err_init:
	ath9k_deinit_wmi(priv);
err_free:
	ieee80211_free_hw(hw);
	return ret;
}

void ath9k_htc_disconnect_device(struct htc_target *htc_handle, bool hotunplug)
{
	if (htc_handle->drv_priv) {

		/* Check if the device has been yanked out. */
		if (hotunplug)
			htc_handle->drv_priv->op_flags |= OP_UNPLUGGED;

		ath9k_deinit_device(htc_handle->drv_priv);
		ath9k_deinit_wmi(htc_handle->drv_priv);
		ieee80211_free_hw(htc_handle->drv_priv->hw);
	}
}

#ifdef CONFIG_PM

void ath9k_htc_suspend(struct htc_target *htc_handle)
{
	ath9k_htc_setpower(htc_handle->drv_priv, ATH9K_PM_FULL_SLEEP);
}

int ath9k_htc_resume(struct htc_target *htc_handle)
{
	int ret;

	ret = ath9k_htc_wait_for_target(htc_handle->drv_priv);
	if (ret)
		return ret;

	ret = ath9k_init_htc_services(htc_handle->drv_priv,
			      htc_handle->drv_priv->ah->hw_version.devid);
	return ret;
}
#endif

static int __init ath9k_htc_init(void)
{
	int error;

	error = ath9k_htc_debug_create_root();
	if (error < 0) {
		printk(KERN_ERR
			"ath9k_htc: Unable to create debugfs root: %d\n",
			error);
		goto err_dbg;
	}

	error = ath9k_hif_usb_init();
	if (error < 0) {
		printk(KERN_ERR
			"ath9k_htc: No USB devices found,"
			" driver not installed.\n");
		error = -ENODEV;
		goto err_usb;
	}

	return 0;

err_usb:
	ath9k_htc_debug_remove_root();
err_dbg:
	return error;
}
module_init(ath9k_htc_init);

static void __exit ath9k_htc_exit(void)
{
	ath9k_hif_usb_exit();
	ath9k_htc_debug_remove_root();
	printk(KERN_INFO "ath9k_htc: Driver unloaded\n");
}
module_exit(ath9k_htc_exit);
