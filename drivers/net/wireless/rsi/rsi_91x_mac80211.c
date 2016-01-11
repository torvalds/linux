/**
 * Copyright (c) 2014 Redpine Signals Inc.
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

#include <linux/etherdevice.h>
#include "rsi_debugfs.h"
#include "rsi_mgmt.h"
#include "rsi_common.h"

static const struct ieee80211_channel rsi_2ghz_channels[] = {
	{ .band = IEEE80211_BAND_2GHZ, .center_freq = 2412,
	  .hw_value = 1 }, /* Channel 1 */
	{ .band = IEEE80211_BAND_2GHZ, .center_freq = 2417,
	  .hw_value = 2 }, /* Channel 2 */
	{ .band = IEEE80211_BAND_2GHZ, .center_freq = 2422,
	  .hw_value = 3 }, /* Channel 3 */
	{ .band = IEEE80211_BAND_2GHZ, .center_freq = 2427,
	  .hw_value = 4 }, /* Channel 4 */
	{ .band = IEEE80211_BAND_2GHZ, .center_freq = 2432,
	  .hw_value = 5 }, /* Channel 5 */
	{ .band = IEEE80211_BAND_2GHZ, .center_freq = 2437,
	  .hw_value = 6 }, /* Channel 6 */
	{ .band = IEEE80211_BAND_2GHZ, .center_freq = 2442,
	  .hw_value = 7 }, /* Channel 7 */
	{ .band = IEEE80211_BAND_2GHZ, .center_freq = 2447,
	  .hw_value = 8 }, /* Channel 8 */
	{ .band = IEEE80211_BAND_2GHZ, .center_freq = 2452,
	  .hw_value = 9 }, /* Channel 9 */
	{ .band = IEEE80211_BAND_2GHZ, .center_freq = 2457,
	  .hw_value = 10 }, /* Channel 10 */
	{ .band = IEEE80211_BAND_2GHZ, .center_freq = 2462,
	  .hw_value = 11 }, /* Channel 11 */
	{ .band = IEEE80211_BAND_2GHZ, .center_freq = 2467,
	  .hw_value = 12 }, /* Channel 12 */
	{ .band = IEEE80211_BAND_2GHZ, .center_freq = 2472,
	  .hw_value = 13 }, /* Channel 13 */
	{ .band = IEEE80211_BAND_2GHZ, .center_freq = 2484,
	  .hw_value = 14 }, /* Channel 14 */
};

static const struct ieee80211_channel rsi_5ghz_channels[] = {
	{ .band = IEEE80211_BAND_5GHZ, .center_freq = 5180,
	  .hw_value = 36,  }, /* Channel 36 */
	{ .band = IEEE80211_BAND_5GHZ, .center_freq = 5200,
	  .hw_value = 40, }, /* Channel 40 */
	{ .band = IEEE80211_BAND_5GHZ, .center_freq = 5220,
	  .hw_value = 44, }, /* Channel 44 */
	{ .band = IEEE80211_BAND_5GHZ, .center_freq = 5240,
	  .hw_value = 48, }, /* Channel 48 */
	{ .band = IEEE80211_BAND_5GHZ, .center_freq = 5260,
	  .hw_value = 52, }, /* Channel 52 */
	{ .band = IEEE80211_BAND_5GHZ, .center_freq = 5280,
	  .hw_value = 56, }, /* Channel 56 */
	{ .band = IEEE80211_BAND_5GHZ, .center_freq = 5300,
	  .hw_value = 60, }, /* Channel 60 */
	{ .band = IEEE80211_BAND_5GHZ, .center_freq = 5320,
	  .hw_value = 64, }, /* Channel 64 */
	{ .band = IEEE80211_BAND_5GHZ, .center_freq = 5500,
	  .hw_value = 100, }, /* Channel 100 */
	{ .band = IEEE80211_BAND_5GHZ, .center_freq = 5520,
	  .hw_value = 104, }, /* Channel 104 */
	{ .band = IEEE80211_BAND_5GHZ, .center_freq = 5540,
	  .hw_value = 108, }, /* Channel 108 */
	{ .band = IEEE80211_BAND_5GHZ, .center_freq = 5560,
	  .hw_value = 112, }, /* Channel 112 */
	{ .band = IEEE80211_BAND_5GHZ, .center_freq = 5580,
	  .hw_value = 116, }, /* Channel 116 */
	{ .band = IEEE80211_BAND_5GHZ, .center_freq = 5600,
	  .hw_value = 120, }, /* Channel 120 */
	{ .band = IEEE80211_BAND_5GHZ, .center_freq = 5620,
	  .hw_value = 124, }, /* Channel 124 */
	{ .band = IEEE80211_BAND_5GHZ, .center_freq = 5640,
	  .hw_value = 128, }, /* Channel 128 */
	{ .band = IEEE80211_BAND_5GHZ, .center_freq = 5660,
	  .hw_value = 132, }, /* Channel 132 */
	{ .band = IEEE80211_BAND_5GHZ, .center_freq = 5680,
	  .hw_value = 136, }, /* Channel 136 */
	{ .band = IEEE80211_BAND_5GHZ, .center_freq = 5700,
	  .hw_value = 140, }, /* Channel 140 */
	{ .band = IEEE80211_BAND_5GHZ, .center_freq = 5745,
	  .hw_value = 149, }, /* Channel 149 */
	{ .band = IEEE80211_BAND_5GHZ, .center_freq = 5765,
	  .hw_value = 153, }, /* Channel 153 */
	{ .band = IEEE80211_BAND_5GHZ, .center_freq = 5785,
	  .hw_value = 157, }, /* Channel 157 */
	{ .band = IEEE80211_BAND_5GHZ, .center_freq = 5805,
	  .hw_value = 161, }, /* Channel 161 */
	{ .band = IEEE80211_BAND_5GHZ, .center_freq = 5825,
	  .hw_value = 165, }, /* Channel 165 */
};

struct ieee80211_rate rsi_rates[12] = {
	{ .bitrate = STD_RATE_01  * 5, .hw_value = RSI_RATE_1 },
	{ .bitrate = STD_RATE_02  * 5, .hw_value = RSI_RATE_2 },
	{ .bitrate = STD_RATE_5_5 * 5, .hw_value = RSI_RATE_5_5 },
	{ .bitrate = STD_RATE_11  * 5, .hw_value = RSI_RATE_11 },
	{ .bitrate = STD_RATE_06  * 5, .hw_value = RSI_RATE_6 },
	{ .bitrate = STD_RATE_09  * 5, .hw_value = RSI_RATE_9 },
	{ .bitrate = STD_RATE_12  * 5, .hw_value = RSI_RATE_12 },
	{ .bitrate = STD_RATE_18  * 5, .hw_value = RSI_RATE_18 },
	{ .bitrate = STD_RATE_24  * 5, .hw_value = RSI_RATE_24 },
	{ .bitrate = STD_RATE_36  * 5, .hw_value = RSI_RATE_36 },
	{ .bitrate = STD_RATE_48  * 5, .hw_value = RSI_RATE_48 },
	{ .bitrate = STD_RATE_54  * 5, .hw_value = RSI_RATE_54 },
};

const u16 rsi_mcsrates[8] = {
	RSI_RATE_MCS0, RSI_RATE_MCS1, RSI_RATE_MCS2, RSI_RATE_MCS3,
	RSI_RATE_MCS4, RSI_RATE_MCS5, RSI_RATE_MCS6, RSI_RATE_MCS7
};

/**
 * rsi_is_cipher_wep() -  This function determines if the cipher is WEP or not.
 * @common: Pointer to the driver private structure.
 *
 * Return: If cipher type is WEP, a value of 1 is returned, else 0.
 */

bool rsi_is_cipher_wep(struct rsi_common *common)
{
	if (((common->secinfo.gtk_cipher == WLAN_CIPHER_SUITE_WEP104) ||
	     (common->secinfo.gtk_cipher == WLAN_CIPHER_SUITE_WEP40)) &&
	    (!common->secinfo.ptk_cipher))
		return true;
	else
		return false;
}

/**
 * rsi_register_rates_channels() - This function registers channels and rates.
 * @adapter: Pointer to the adapter structure.
 * @band: Operating band to be set.
 *
 * Return: None.
 */
static void rsi_register_rates_channels(struct rsi_hw *adapter, int band)
{
	struct ieee80211_supported_band *sbands = &adapter->sbands[band];
	void *channels = NULL;

	if (band == IEEE80211_BAND_2GHZ) {
		channels = kmalloc(sizeof(rsi_2ghz_channels), GFP_KERNEL);
		memcpy(channels,
		       rsi_2ghz_channels,
		       sizeof(rsi_2ghz_channels));
		sbands->band = IEEE80211_BAND_2GHZ;
		sbands->n_channels = ARRAY_SIZE(rsi_2ghz_channels);
		sbands->bitrates = rsi_rates;
		sbands->n_bitrates = ARRAY_SIZE(rsi_rates);
	} else {
		channels = kmalloc(sizeof(rsi_5ghz_channels), GFP_KERNEL);
		memcpy(channels,
		       rsi_5ghz_channels,
		       sizeof(rsi_5ghz_channels));
		sbands->band = IEEE80211_BAND_5GHZ;
		sbands->n_channels = ARRAY_SIZE(rsi_5ghz_channels);
		sbands->bitrates = &rsi_rates[4];
		sbands->n_bitrates = ARRAY_SIZE(rsi_rates) - 4;
	}

	sbands->channels = channels;

	memset(&sbands->ht_cap, 0, sizeof(struct ieee80211_sta_ht_cap));
	sbands->ht_cap.ht_supported = true;
	sbands->ht_cap.cap = (IEEE80211_HT_CAP_SUP_WIDTH_20_40 |
			      IEEE80211_HT_CAP_SGI_20 |
			      IEEE80211_HT_CAP_SGI_40);
	sbands->ht_cap.ampdu_factor = IEEE80211_HT_MAX_AMPDU_16K;
	sbands->ht_cap.ampdu_density = IEEE80211_HT_MPDU_DENSITY_NONE;
	sbands->ht_cap.mcs.rx_mask[0] = 0xff;
	sbands->ht_cap.mcs.tx_params = IEEE80211_HT_MCS_TX_DEFINED;
	/* sbands->ht_cap.mcs.rx_highest = 0x82; */
}

/**
 * rsi_mac80211_detach() - This function is used to de-initialize the
 *			   Mac80211 stack.
 * @adapter: Pointer to the adapter structure.
 *
 * Return: None.
 */
void rsi_mac80211_detach(struct rsi_hw *adapter)
{
	struct ieee80211_hw *hw = adapter->hw;

	if (hw) {
		ieee80211_stop_queues(hw);
		ieee80211_unregister_hw(hw);
		ieee80211_free_hw(hw);
	}

	rsi_remove_dbgfs(adapter);
}
EXPORT_SYMBOL_GPL(rsi_mac80211_detach);

/**
 * rsi_indicate_tx_status() - This function indicates the transmit status.
 * @adapter: Pointer to the adapter structure.
 * @skb: Pointer to the socket buffer structure.
 * @status: Status
 *
 * Return: None.
 */
void rsi_indicate_tx_status(struct rsi_hw *adapter,
			    struct sk_buff *skb,
			    int status)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);

	memset(info->driver_data, 0, IEEE80211_TX_INFO_DRIVER_DATA_SIZE);

	if (!status)
		info->flags |= IEEE80211_TX_STAT_ACK;

	ieee80211_tx_status_irqsafe(adapter->hw, skb);
}

/**
 * rsi_mac80211_tx() - This is the handler that 802.11 module calls for each
 *		       transmitted frame.SKB contains the buffer starting
 *		       from the IEEE 802.11 header.
 * @hw: Pointer to the ieee80211_hw structure.
 * @control: Pointer to the ieee80211_tx_control structure
 * @skb: Pointer to the socket buffer structure.
 *
 * Return: None
 */
static void rsi_mac80211_tx(struct ieee80211_hw *hw,
			    struct ieee80211_tx_control *control,
			    struct sk_buff *skb)
{
	struct rsi_hw *adapter = hw->priv;
	struct rsi_common *common = adapter->priv;

	rsi_core_xmit(common, skb);
}

/**
 * rsi_mac80211_start() - This is first handler that 802.11 module calls, since
 *			  the driver init is complete by then, just
 *			  returns success.
 * @hw: Pointer to the ieee80211_hw structure.
 *
 * Return: 0 as success.
 */
static int rsi_mac80211_start(struct ieee80211_hw *hw)
{
	struct rsi_hw *adapter = hw->priv;
	struct rsi_common *common = adapter->priv;

	mutex_lock(&common->mutex);
	common->iface_down = false;
	mutex_unlock(&common->mutex);

	return 0;
}

/**
 * rsi_mac80211_stop() - This is the last handler that 802.11 module calls.
 * @hw: Pointer to the ieee80211_hw structure.
 *
 * Return: None.
 */
static void rsi_mac80211_stop(struct ieee80211_hw *hw)
{
	struct rsi_hw *adapter = hw->priv;
	struct rsi_common *common = adapter->priv;

	mutex_lock(&common->mutex);
	common->iface_down = true;
	mutex_unlock(&common->mutex);
}

/**
 * rsi_mac80211_add_interface() - This function is called when a netdevice
 *				  attached to the hardware is enabled.
 * @hw: Pointer to the ieee80211_hw structure.
 * @vif: Pointer to the ieee80211_vif structure.
 *
 * Return: ret: 0 on success, negative error code on failure.
 */
static int rsi_mac80211_add_interface(struct ieee80211_hw *hw,
				      struct ieee80211_vif *vif)
{
	struct rsi_hw *adapter = hw->priv;
	struct rsi_common *common = adapter->priv;
	int ret = -EOPNOTSUPP;

	mutex_lock(&common->mutex);
	switch (vif->type) {
	case NL80211_IFTYPE_STATION:
		if (!adapter->sc_nvifs) {
			++adapter->sc_nvifs;
			adapter->vifs[0] = vif;
			ret = rsi_set_vap_capabilities(common, STA_OPMODE);
		}
		break;
	default:
		rsi_dbg(ERR_ZONE,
			"%s: Interface type %d not supported\n", __func__,
			vif->type);
	}
	mutex_unlock(&common->mutex);

	return ret;
}

/**
 * rsi_mac80211_remove_interface() - This function notifies driver that an
 *				     interface is going down.
 * @hw: Pointer to the ieee80211_hw structure.
 * @vif: Pointer to the ieee80211_vif structure.
 *
 * Return: None.
 */
static void rsi_mac80211_remove_interface(struct ieee80211_hw *hw,
					  struct ieee80211_vif *vif)
{
	struct rsi_hw *adapter = hw->priv;
	struct rsi_common *common = adapter->priv;

	mutex_lock(&common->mutex);
	if (vif->type == NL80211_IFTYPE_STATION)
		adapter->sc_nvifs--;

	if (!memcmp(adapter->vifs[0], vif, sizeof(struct ieee80211_vif)))
		adapter->vifs[0] = NULL;
	mutex_unlock(&common->mutex);
}

/**
 * rsi_channel_change() - This function is a performs the checks
 *			  required for changing a channel and sets
 *			  the channel accordingly.
 * @hw: Pointer to the ieee80211_hw structure.
 *
 * Return: 0 on success, negative error code on failure.
 */
static int rsi_channel_change(struct ieee80211_hw *hw)
{
	struct rsi_hw *adapter = hw->priv;
	struct rsi_common *common = adapter->priv;
	int status = -EOPNOTSUPP;
	struct ieee80211_channel *curchan = hw->conf.chandef.chan;
	u16 channel = curchan->hw_value;
	struct ieee80211_bss_conf *bss = &adapter->vifs[0]->bss_conf;

	rsi_dbg(INFO_ZONE,
		"%s: Set channel: %d MHz type: %d channel_no %d\n",
		__func__, curchan->center_freq,
		curchan->flags, channel);

	if (bss->assoc) {
		if (!common->hw_data_qs_blocked &&
		    (rsi_get_connected_channel(adapter) != channel)) {
			rsi_dbg(INFO_ZONE, "blk data q %d\n", channel);
			if (!rsi_send_block_unblock_frame(common, true))
				common->hw_data_qs_blocked = true;
		}
	}

	status = rsi_band_check(common);
	if (!status)
		status = rsi_set_channel(adapter->priv, channel);

	if (bss->assoc) {
		if (common->hw_data_qs_blocked &&
		    (rsi_get_connected_channel(adapter) == channel)) {
			rsi_dbg(INFO_ZONE, "unblk data q %d\n", channel);
			if (!rsi_send_block_unblock_frame(common, false))
				common->hw_data_qs_blocked = false;
		}
	} else {
		if (common->hw_data_qs_blocked) {
			rsi_dbg(INFO_ZONE, "unblk data q %d\n", channel);
			if (!rsi_send_block_unblock_frame(common, false))
				common->hw_data_qs_blocked = false;
		}
	}

	return status;
}

/**
 * rsi_mac80211_config() - This function is a handler for configuration
 *			   requests. The stack calls this function to
 *			   change hardware configuration, e.g., channel.
 * @hw: Pointer to the ieee80211_hw structure.
 * @changed: Changed flags set.
 *
 * Return: 0 on success, negative error code on failure.
 */
static int rsi_mac80211_config(struct ieee80211_hw *hw,
			       u32 changed)
{
	struct rsi_hw *adapter = hw->priv;
	struct rsi_common *common = adapter->priv;
	int status = -EOPNOTSUPP;

	mutex_lock(&common->mutex);

	if (changed & IEEE80211_CONF_CHANGE_CHANNEL)
		status = rsi_channel_change(hw);

	mutex_unlock(&common->mutex);

	return status;
}

/**
 * rsi_get_connected_channel() - This function is used to get the current
 *				 connected channel number.
 * @adapter: Pointer to the adapter structure.
 *
 * Return: Current connected AP's channel number is returned.
 */
u16 rsi_get_connected_channel(struct rsi_hw *adapter)
{
	struct ieee80211_vif *vif = adapter->vifs[0];
	if (vif) {
		struct ieee80211_bss_conf *bss = &vif->bss_conf;
		struct ieee80211_channel *channel = bss->chandef.chan;
		return channel->hw_value;
	}

	return 0;
}

/**
 * rsi_mac80211_bss_info_changed() - This function is a handler for config
 *				     requests related to BSS parameters that
 *				     may vary during BSS's lifespan.
 * @hw: Pointer to the ieee80211_hw structure.
 * @vif: Pointer to the ieee80211_vif structure.
 * @bss_conf: Pointer to the ieee80211_bss_conf structure.
 * @changed: Changed flags set.
 *
 * Return: None.
 */
static void rsi_mac80211_bss_info_changed(struct ieee80211_hw *hw,
					  struct ieee80211_vif *vif,
					  struct ieee80211_bss_conf *bss_conf,
					  u32 changed)
{
	struct rsi_hw *adapter = hw->priv;
	struct rsi_common *common = adapter->priv;

	mutex_lock(&common->mutex);
	if (changed & BSS_CHANGED_ASSOC) {
		rsi_dbg(INFO_ZONE, "%s: Changed Association status: %d\n",
			__func__, bss_conf->assoc);
		rsi_inform_bss_status(common,
				      bss_conf->assoc,
				      bss_conf->bssid,
				      bss_conf->qos,
				      bss_conf->aid);
	}

	if (changed & BSS_CHANGED_CQM) {
		common->cqm_info.last_cqm_event_rssi = 0;
		common->cqm_info.rssi_thold = bss_conf->cqm_rssi_thold;
		common->cqm_info.rssi_hyst = bss_conf->cqm_rssi_hyst;
		rsi_dbg(INFO_ZONE, "RSSI throld & hysteresis are: %d %d\n",
			common->cqm_info.rssi_thold,
			common->cqm_info.rssi_hyst);
	}
	mutex_unlock(&common->mutex);
}

/**
 * rsi_mac80211_conf_filter() - This function configure the device's RX filter.
 * @hw: Pointer to the ieee80211_hw structure.
 * @changed: Changed flags set.
 * @total_flags: Total initial flags set.
 * @multicast: Multicast.
 *
 * Return: None.
 */
static void rsi_mac80211_conf_filter(struct ieee80211_hw *hw,
				     u32 changed_flags,
				     u32 *total_flags,
				     u64 multicast)
{
	/* Not doing much here as of now */
	*total_flags &= RSI_SUPP_FILTERS;
}

/**
 * rsi_mac80211_conf_tx() - This function configures TX queue parameters
 *			    (EDCF (aifs, cw_min, cw_max), bursting)
 *			    for a hardware TX queue.
 * @hw: Pointer to the ieee80211_hw structure
 * @vif: Pointer to the ieee80211_vif structure.
 * @queue: Queue number.
 * @params: Pointer to ieee80211_tx_queue_params structure.
 *
 * Return: 0 on success, negative error code on failure.
 */
static int rsi_mac80211_conf_tx(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif, u16 queue,
				const struct ieee80211_tx_queue_params *params)
{
	struct rsi_hw *adapter = hw->priv;
	struct rsi_common *common = adapter->priv;
	u8 idx = 0;

	if (queue >= IEEE80211_NUM_ACS)
		return 0;

	rsi_dbg(INFO_ZONE,
		"%s: Conf queue %d, aifs: %d, cwmin: %d cwmax: %d, txop: %d\n",
		__func__, queue, params->aifs,
		params->cw_min, params->cw_max, params->txop);

	mutex_lock(&common->mutex);
	/* Map into the way the f/w expects */
	switch (queue) {
	case IEEE80211_AC_VO:
		idx = VO_Q;
		break;
	case IEEE80211_AC_VI:
		idx = VI_Q;
		break;
	case IEEE80211_AC_BE:
		idx = BE_Q;
		break;
	case IEEE80211_AC_BK:
		idx = BK_Q;
		break;
	default:
		idx = BE_Q;
		break;
	}

	memcpy(&common->edca_params[idx],
	       params,
	       sizeof(struct ieee80211_tx_queue_params));
	mutex_unlock(&common->mutex);

	return 0;
}

/**
 * rsi_hal_key_config() - This function loads the keys into the firmware.
 * @hw: Pointer to the ieee80211_hw structure.
 * @vif: Pointer to the ieee80211_vif structure.
 * @key: Pointer to the ieee80211_key_conf structure.
 *
 * Return: status: 0 on success, -1 on failure.
 */
static int rsi_hal_key_config(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif,
			      struct ieee80211_key_conf *key)
{
	struct rsi_hw *adapter = hw->priv;
	int status;
	u8 key_type;

	if (key->flags & IEEE80211_KEY_FLAG_PAIRWISE)
		key_type = RSI_PAIRWISE_KEY;
	else
		key_type = RSI_GROUP_KEY;

	rsi_dbg(ERR_ZONE, "%s: Cipher 0x%x key_type: %d key_len: %d\n",
		__func__, key->cipher, key_type, key->keylen);

	if ((key->cipher == WLAN_CIPHER_SUITE_WEP104) ||
	    (key->cipher == WLAN_CIPHER_SUITE_WEP40)) {
		status = rsi_hal_load_key(adapter->priv,
					  key->key,
					  key->keylen,
					  RSI_PAIRWISE_KEY,
					  key->keyidx,
					  key->cipher);
		if (status)
			return status;
	}
	return rsi_hal_load_key(adapter->priv,
				key->key,
				key->keylen,
				key_type,
				key->keyidx,
				key->cipher);
}

/**
 * rsi_mac80211_set_key() - This function sets type of key to be loaded.
 * @hw: Pointer to the ieee80211_hw structure.
 * @cmd: enum set_key_cmd.
 * @vif: Pointer to the ieee80211_vif structure.
 * @sta: Pointer to the ieee80211_sta structure.
 * @key: Pointer to the ieee80211_key_conf structure.
 *
 * Return: status: 0 on success, negative error code on failure.
 */
static int rsi_mac80211_set_key(struct ieee80211_hw *hw,
				enum set_key_cmd cmd,
				struct ieee80211_vif *vif,
				struct ieee80211_sta *sta,
				struct ieee80211_key_conf *key)
{
	struct rsi_hw *adapter = hw->priv;
	struct rsi_common *common = adapter->priv;
	struct security_info *secinfo = &common->secinfo;
	int status;

	mutex_lock(&common->mutex);
	switch (cmd) {
	case SET_KEY:
		secinfo->security_enable = true;
		status = rsi_hal_key_config(hw, vif, key);
		if (status) {
			mutex_unlock(&common->mutex);
			return status;
		}

		if (key->flags & IEEE80211_KEY_FLAG_PAIRWISE)
			secinfo->ptk_cipher = key->cipher;
		else
			secinfo->gtk_cipher = key->cipher;

		key->hw_key_idx = key->keyidx;
		key->flags |= IEEE80211_KEY_FLAG_GENERATE_IV;

		rsi_dbg(ERR_ZONE, "%s: RSI set_key\n", __func__);
		break;

	case DISABLE_KEY:
		secinfo->security_enable = false;
		rsi_dbg(ERR_ZONE, "%s: RSI del key\n", __func__);
		memset(key, 0, sizeof(struct ieee80211_key_conf));
		status = rsi_hal_key_config(hw, vif, key);
		break;

	default:
		status = -EOPNOTSUPP;
		break;
	}

	mutex_unlock(&common->mutex);
	return status;
}

/**
 * rsi_mac80211_ampdu_action() - This function selects the AMPDU action for
 *				 the corresponding mlme_action flag and
 *				 informs the f/w regarding this.
 * @hw: Pointer to the ieee80211_hw structure.
 * @vif: Pointer to the ieee80211_vif structure.
 * @action: ieee80211_ampdu_mlme_action enum.
 * @sta: Pointer to the ieee80211_sta structure.
 * @tid: Traffic identifier.
 * @ssn: Pointer to ssn value.
 * @buf_size: Buffer size (for kernel version > 2.6.38).
 * @amsdu: is AMSDU in AMPDU allowed
 *
 * Return: status: 0 on success, negative error code on failure.
 */
static int rsi_mac80211_ampdu_action(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif,
				     enum ieee80211_ampdu_mlme_action action,
				     struct ieee80211_sta *sta,
				     unsigned short tid,
				     unsigned short *ssn,
				     unsigned char buf_size,
				     bool amsdu)
{
	int status = -EOPNOTSUPP;
	struct rsi_hw *adapter = hw->priv;
	struct rsi_common *common = adapter->priv;
	u16 seq_no = 0;
	u8 ii = 0;

	for (ii = 0; ii < RSI_MAX_VIFS; ii++) {
		if (vif == adapter->vifs[ii])
			break;
	}

	mutex_lock(&common->mutex);
	rsi_dbg(INFO_ZONE, "%s: AMPDU action %d called\n", __func__, action);
	if (ssn != NULL)
		seq_no = *ssn;

	switch (action) {
	case IEEE80211_AMPDU_RX_START:
		status = rsi_send_aggregation_params_frame(common,
							   tid,
							   seq_no,
							   buf_size,
							   STA_RX_ADDBA_DONE);
		break;

	case IEEE80211_AMPDU_RX_STOP:
		status = rsi_send_aggregation_params_frame(common,
							   tid,
							   0,
							   buf_size,
							   STA_RX_DELBA);
		break;

	case IEEE80211_AMPDU_TX_START:
		common->vif_info[ii].seq_start = seq_no;
		ieee80211_start_tx_ba_cb_irqsafe(vif, sta->addr, tid);
		status = 0;
		break;

	case IEEE80211_AMPDU_TX_STOP_CONT:
	case IEEE80211_AMPDU_TX_STOP_FLUSH:
	case IEEE80211_AMPDU_TX_STOP_FLUSH_CONT:
		status = rsi_send_aggregation_params_frame(common,
							   tid,
							   seq_no,
							   buf_size,
							   STA_TX_DELBA);
		if (!status)
			ieee80211_stop_tx_ba_cb_irqsafe(vif, sta->addr, tid);
		break;

	case IEEE80211_AMPDU_TX_OPERATIONAL:
		status = rsi_send_aggregation_params_frame(common,
							   tid,
							   common->vif_info[ii]
								.seq_start,
							   buf_size,
							   STA_TX_ADDBA_DONE);
		break;

	default:
		rsi_dbg(ERR_ZONE, "%s: Uknown AMPDU action\n", __func__);
		break;
	}

	mutex_unlock(&common->mutex);
	return status;
}

/**
 * rsi_mac80211_set_rts_threshold() - This function sets rts threshold value.
 * @hw: Pointer to the ieee80211_hw structure.
 * @value: Rts threshold value.
 *
 * Return: 0 on success.
 */
static int rsi_mac80211_set_rts_threshold(struct ieee80211_hw *hw,
					  u32 value)
{
	struct rsi_hw *adapter = hw->priv;
	struct rsi_common *common = adapter->priv;

	mutex_lock(&common->mutex);
	common->rts_threshold = value;
	mutex_unlock(&common->mutex);

	return 0;
}

/**
 * rsi_mac80211_set_rate_mask() - This function sets bitrate_mask to be used.
 * @hw: Pointer to the ieee80211_hw structure
 * @vif: Pointer to the ieee80211_vif structure.
 * @mask: Pointer to the cfg80211_bitrate_mask structure.
 *
 * Return: 0 on success.
 */
static int rsi_mac80211_set_rate_mask(struct ieee80211_hw *hw,
				      struct ieee80211_vif *vif,
				      const struct cfg80211_bitrate_mask *mask)
{
	struct rsi_hw *adapter = hw->priv;
	struct rsi_common *common = adapter->priv;
	enum ieee80211_band band = hw->conf.chandef.chan->band;

	mutex_lock(&common->mutex);
	common->fixedrate_mask[band] = 0;

	if (mask->control[band].legacy == 0xfff) {
		common->fixedrate_mask[band] =
			(mask->control[band].ht_mcs[0] << 12);
	} else {
		common->fixedrate_mask[band] =
			mask->control[band].legacy;
	}
	mutex_unlock(&common->mutex);

	return 0;
}

/**
 * rsi_perform_cqm() - This function performs cqm.
 * @common: Pointer to the driver private structure.
 * @bssid: pointer to the bssid.
 * @rssi: RSSI value.
 */
static void rsi_perform_cqm(struct rsi_common *common,
			    u8 *bssid,
			    s8 rssi)
{
	struct rsi_hw *adapter = common->priv;
	s8 last_event = common->cqm_info.last_cqm_event_rssi;
	int thold = common->cqm_info.rssi_thold;
	u32 hyst = common->cqm_info.rssi_hyst;
	enum nl80211_cqm_rssi_threshold_event event;

	if (rssi < thold && (last_event == 0 || rssi < (last_event - hyst)))
		event = NL80211_CQM_RSSI_THRESHOLD_EVENT_LOW;
	else if (rssi > thold &&
		 (last_event == 0 || rssi > (last_event + hyst)))
		event = NL80211_CQM_RSSI_THRESHOLD_EVENT_HIGH;
	else
		return;

	common->cqm_info.last_cqm_event_rssi = rssi;
	rsi_dbg(INFO_ZONE, "CQM: Notifying event: %d\n", event);
	ieee80211_cqm_rssi_notify(adapter->vifs[0], event, GFP_KERNEL);

	return;
}

/**
 * rsi_fill_rx_status() - This function fills rx status in
 *			  ieee80211_rx_status structure.
 * @hw: Pointer to the ieee80211_hw structure.
 * @skb: Pointer to the socket buffer structure.
 * @common: Pointer to the driver private structure.
 * @rxs: Pointer to the ieee80211_rx_status structure.
 *
 * Return: None.
 */
static void rsi_fill_rx_status(struct ieee80211_hw *hw,
			       struct sk_buff *skb,
			       struct rsi_common *common,
			       struct ieee80211_rx_status *rxs)
{
	struct ieee80211_bss_conf *bss = &common->priv->vifs[0]->bss_conf;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct skb_info *rx_params = (struct skb_info *)info->driver_data;
	struct ieee80211_hdr *hdr;
	char rssi = rx_params->rssi;
	u8 hdrlen = 0;
	u8 channel = rx_params->channel;
	s32 freq;

	hdr = ((struct ieee80211_hdr *)(skb->data));
	hdrlen = ieee80211_hdrlen(hdr->frame_control);

	memset(info, 0, sizeof(struct ieee80211_tx_info));

	rxs->signal = -(rssi);

	rxs->band = common->band;

	freq = ieee80211_channel_to_frequency(channel, rxs->band);

	if (freq)
		rxs->freq = freq;

	if (ieee80211_has_protected(hdr->frame_control)) {
		if (rsi_is_cipher_wep(common)) {
			memmove(skb->data + 4, skb->data, hdrlen);
			skb_pull(skb, 4);
		} else {
			memmove(skb->data + 8, skb->data, hdrlen);
			skb_pull(skb, 8);
			rxs->flag |= RX_FLAG_MMIC_STRIPPED;
		}
		rxs->flag |= RX_FLAG_DECRYPTED;
		rxs->flag |= RX_FLAG_IV_STRIPPED;
	}

	/* CQM only for connected AP beacons, the RSSI is a weighted avg */
	if (bss->assoc && !(memcmp(bss->bssid, hdr->addr2, ETH_ALEN))) {
		if (ieee80211_is_beacon(hdr->frame_control))
			rsi_perform_cqm(common, hdr->addr2, rxs->signal);
	}

	return;
}

/**
 * rsi_indicate_pkt_to_os() - This function sends recieved packet to mac80211.
 * @common: Pointer to the driver private structure.
 * @skb: Pointer to the socket buffer structure.
 *
 * Return: None.
 */
void rsi_indicate_pkt_to_os(struct rsi_common *common,
			    struct sk_buff *skb)
{
	struct rsi_hw *adapter = common->priv;
	struct ieee80211_hw *hw = adapter->hw;
	struct ieee80211_rx_status *rx_status = IEEE80211_SKB_RXCB(skb);

	if ((common->iface_down) || (!adapter->sc_nvifs)) {
		dev_kfree_skb(skb);
		return;
	}

	/* filling in the ieee80211_rx_status flags */
	rsi_fill_rx_status(hw, skb, common, rx_status);

	ieee80211_rx_irqsafe(hw, skb);
}

static void rsi_set_min_rate(struct ieee80211_hw *hw,
			     struct ieee80211_sta *sta,
			     struct rsi_common *common)
{
	u8 band = hw->conf.chandef.chan->band;
	u8 ii;
	u32 rate_bitmap;
	bool matched = false;

	common->bitrate_mask[band] = sta->supp_rates[band];

	rate_bitmap = (common->fixedrate_mask[band] & sta->supp_rates[band]);

	if (rate_bitmap & 0xfff) {
		/* Find out the min rate */
		for (ii = 0; ii < ARRAY_SIZE(rsi_rates); ii++) {
			if (rate_bitmap & BIT(ii)) {
				common->min_rate = rsi_rates[ii].hw_value;
				matched = true;
				break;
			}
		}
	}

	common->vif_info[0].is_ht = sta->ht_cap.ht_supported;

	if ((common->vif_info[0].is_ht) && (rate_bitmap >> 12)) {
		for (ii = 0; ii < ARRAY_SIZE(rsi_mcsrates); ii++) {
			if ((rate_bitmap >> 12) & BIT(ii)) {
				common->min_rate = rsi_mcsrates[ii];
				matched = true;
				break;
			}
		}
	}

	if (!matched)
		common->min_rate = 0xffff;
}

/**
 * rsi_mac80211_sta_add() - This function notifies driver about a peer getting
 *			    connected.
 * @hw: pointer to the ieee80211_hw structure.
 * @vif: Pointer to the ieee80211_vif structure.
 * @sta: Pointer to the ieee80211_sta structure.
 *
 * Return: 0 on success, -1 on failure.
 */
static int rsi_mac80211_sta_add(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif,
				struct ieee80211_sta *sta)
{
	struct rsi_hw *adapter = hw->priv;
	struct rsi_common *common = adapter->priv;

	mutex_lock(&common->mutex);

	rsi_set_min_rate(hw, sta, common);

	if ((sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_20) ||
	    (sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_40)) {
		common->vif_info[0].sgi = true;
	}

	if (sta->ht_cap.ht_supported)
		ieee80211_start_tx_ba_session(sta, 0, 0);

	mutex_unlock(&common->mutex);

	return 0;
}

/**
 * rsi_mac80211_sta_remove() - This function notifies driver about a peer
 *			       getting disconnected.
 * @hw: Pointer to the ieee80211_hw structure.
 * @vif: Pointer to the ieee80211_vif structure.
 * @sta: Pointer to the ieee80211_sta structure.
 *
 * Return: 0 on success, -1 on failure.
 */
static int rsi_mac80211_sta_remove(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif,
				   struct ieee80211_sta *sta)
{
	struct rsi_hw *adapter = hw->priv;
	struct rsi_common *common = adapter->priv;

	mutex_lock(&common->mutex);
	/* Resetting all the fields to default values */
	common->bitrate_mask[IEEE80211_BAND_2GHZ] = 0;
	common->bitrate_mask[IEEE80211_BAND_5GHZ] = 0;
	common->min_rate = 0xffff;
	common->vif_info[0].is_ht = false;
	common->vif_info[0].sgi = false;
	common->vif_info[0].seq_start = 0;
	common->secinfo.ptk_cipher = 0;
	common->secinfo.gtk_cipher = 0;
	mutex_unlock(&common->mutex);

	return 0;
}

static struct ieee80211_ops mac80211_ops = {
	.tx = rsi_mac80211_tx,
	.start = rsi_mac80211_start,
	.stop = rsi_mac80211_stop,
	.add_interface = rsi_mac80211_add_interface,
	.remove_interface = rsi_mac80211_remove_interface,
	.config = rsi_mac80211_config,
	.bss_info_changed = rsi_mac80211_bss_info_changed,
	.conf_tx = rsi_mac80211_conf_tx,
	.configure_filter = rsi_mac80211_conf_filter,
	.set_key = rsi_mac80211_set_key,
	.set_rts_threshold = rsi_mac80211_set_rts_threshold,
	.set_bitrate_mask = rsi_mac80211_set_rate_mask,
	.ampdu_action = rsi_mac80211_ampdu_action,
	.sta_add = rsi_mac80211_sta_add,
	.sta_remove = rsi_mac80211_sta_remove,
};

/**
 * rsi_mac80211_attach() - This function is used to initialize Mac80211 stack.
 * @common: Pointer to the driver private structure.
 *
 * Return: 0 on success, -1 on failure.
 */
int rsi_mac80211_attach(struct rsi_common *common)
{
	int status = 0;
	struct ieee80211_hw *hw = NULL;
	struct wiphy *wiphy = NULL;
	struct rsi_hw *adapter = common->priv;
	u8 addr_mask[ETH_ALEN] = {0x0, 0x0, 0x0, 0x0, 0x0, 0x3};

	rsi_dbg(INIT_ZONE, "%s: Performing mac80211 attach\n", __func__);

	hw = ieee80211_alloc_hw(sizeof(struct rsi_hw), &mac80211_ops);
	if (!hw) {
		rsi_dbg(ERR_ZONE, "%s: ieee80211 hw alloc failed\n", __func__);
		return -ENOMEM;
	}

	wiphy = hw->wiphy;

	SET_IEEE80211_DEV(hw, adapter->device);

	hw->priv = adapter;
	adapter->hw = hw;

	ieee80211_hw_set(hw, SIGNAL_DBM);
	ieee80211_hw_set(hw, HAS_RATE_CONTROL);
	ieee80211_hw_set(hw, AMPDU_AGGREGATION);

	hw->queues = MAX_HW_QUEUES;
	hw->extra_tx_headroom = RSI_NEEDED_HEADROOM;

	hw->max_rates = 1;
	hw->max_rate_tries = MAX_RETRIES;

	hw->max_tx_aggregation_subframes = 6;
	rsi_register_rates_channels(adapter, IEEE80211_BAND_2GHZ);
	rsi_register_rates_channels(adapter, IEEE80211_BAND_5GHZ);
	hw->rate_control_algorithm = "AARF";

	SET_IEEE80211_PERM_ADDR(hw, common->mac_addr);
	ether_addr_copy(hw->wiphy->addr_mask, addr_mask);

	wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION);
	wiphy->signal_type = CFG80211_SIGNAL_TYPE_MBM;
	wiphy->retry_short = RETRY_SHORT;
	wiphy->retry_long  = RETRY_LONG;
	wiphy->frag_threshold = IEEE80211_MAX_FRAG_THRESHOLD;
	wiphy->rts_threshold = IEEE80211_MAX_RTS_THRESHOLD;
	wiphy->flags = 0;

	wiphy->available_antennas_rx = 1;
	wiphy->available_antennas_tx = 1;
	wiphy->bands[IEEE80211_BAND_2GHZ] =
		&adapter->sbands[IEEE80211_BAND_2GHZ];
	wiphy->bands[IEEE80211_BAND_5GHZ] =
		&adapter->sbands[IEEE80211_BAND_5GHZ];

	status = ieee80211_register_hw(hw);
	if (status)
		return status;

	return rsi_init_dbgfs(adapter);
}
