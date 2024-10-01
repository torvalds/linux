// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
 *
 * Contact Information: wlanfae <wlanfae@realtek.com>
 */
#include <linux/string.h>
#include "rtl_core.h"
#include "rtl_wx.h"

#define RATE_COUNT 12
static u32 rtl8192_rates[] = {
	1000000, 2000000, 5500000, 11000000, 6000000, 9000000, 12000000,
	18000000, 24000000, 36000000, 48000000, 54000000
};

#ifndef ENETDOWN
#define ENETDOWN 1
#endif

static int _rtl92e_wx_get_freq(struct net_device *dev,
			       struct iw_request_info *a,
			       union iwreq_data *wrqu, char *b)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	return rtllib_wx_get_freq(priv->rtllib, a, wrqu, b);
}

static int _rtl92e_wx_get_mode(struct net_device *dev,
			       struct iw_request_info *a,
			       union iwreq_data *wrqu, char *b)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	return rtllib_wx_get_mode(priv->rtllib, a, wrqu, b);
}

static int _rtl92e_wx_get_rate(struct net_device *dev,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	return rtllib_wx_get_rate(priv->rtllib, info, wrqu, extra);
}

static int _rtl92e_wx_set_rate(struct net_device *dev,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)
{
	int ret;
	struct r8192_priv *priv = rtllib_priv(dev);

	if (priv->hw_radio_off)
		return 0;

	mutex_lock(&priv->wx_mutex);

	ret = rtllib_wx_set_rate(priv->rtllib, info, wrqu, extra);

	mutex_unlock(&priv->wx_mutex);

	return ret;
}

static int _rtl92e_wx_set_rts(struct net_device *dev,
			      struct iw_request_info *info,
			      union iwreq_data *wrqu, char *extra)
{
	int ret;
	struct r8192_priv *priv = rtllib_priv(dev);

	if (priv->hw_radio_off)
		return 0;

	mutex_lock(&priv->wx_mutex);

	ret = rtllib_wx_set_rts(priv->rtllib, info, wrqu, extra);

	mutex_unlock(&priv->wx_mutex);

	return ret;
}

static int _rtl92e_wx_get_rts(struct net_device *dev,
			      struct iw_request_info *info,
			      union iwreq_data *wrqu, char *extra)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	return rtllib_wx_get_rts(priv->rtllib, info, wrqu, extra);
}

static int _rtl92e_wx_set_power(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *extra)
{
	int ret;
	struct r8192_priv *priv = rtllib_priv(dev);

	if (priv->hw_radio_off) {
		netdev_warn(dev, "%s(): Can't set Power: Radio is Off.\n",
			    __func__);
		return 0;
	}
	mutex_lock(&priv->wx_mutex);

	ret = rtllib_wx_set_power(priv->rtllib, info, wrqu, extra);

	mutex_unlock(&priv->wx_mutex);

	return ret;
}

static int _rtl92e_wx_get_power(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *extra)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	return rtllib_wx_get_power(priv->rtllib, info, wrqu, extra);
}

static int _rtl92e_wx_set_mode(struct net_device *dev,
			       struct iw_request_info *a,
			       union iwreq_data *wrqu, char *b)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	enum rt_rf_power_state rt_state;
	int ret;

	if (priv->hw_radio_off)
		return 0;
	rt_state = priv->rtllib->rf_power_state;
	mutex_lock(&priv->wx_mutex);
	if (wrqu->mode == IW_MODE_MONITOR) {
		if (rt_state == rf_off) {
			if (priv->rtllib->rf_off_reason >
			    RF_CHANGE_BY_IPS) {
				netdev_warn(dev, "%s(): RF is OFF.\n",
					    __func__);
				mutex_unlock(&priv->wx_mutex);
				return -1;
			}
			netdev_info(dev,
				    "=========>%s(): rtl92e_ips_leave\n",
				    __func__);
			mutex_lock(&priv->rtllib->ips_mutex);
			rtl92e_ips_leave(dev);
			mutex_unlock(&priv->rtllib->ips_mutex);
		}
	}
	ret = rtllib_wx_set_mode(priv->rtllib, a, wrqu, b);

	mutex_unlock(&priv->wx_mutex);
	return ret;
}

static int _rtl92e_wx_get_range(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *extra)
{
	struct iw_range *range = (struct iw_range *)extra;
	struct r8192_priv *priv = rtllib_priv(dev);
	u16 val;
	int i;

	wrqu->data.length = sizeof(*range);
	memset(range, 0, sizeof(*range));

	/* ~130 Mb/s real (802.11n) */
	range->throughput = 130 * 1000 * 1000;

	range->max_qual.qual = 100;
	range->max_qual.level = 0;
	range->max_qual.noise = 0;
	range->max_qual.updated = 7; /* Updated all three */

	range->avg_qual.qual = 70; /* > 8% missed beacons is 'bad' */
	range->avg_qual.level = 0;
	range->avg_qual.noise = 0;
	range->avg_qual.updated = 7; /* Updated all three */

	range->num_bitrates = min(RATE_COUNT, IW_MAX_BITRATES);

	for (i = 0; i < range->num_bitrates; i++)
		range->bitrate[i] = rtl8192_rates[i];

	range->max_rts = DEFAULT_RTS_THRESHOLD;
	range->min_frag = MIN_FRAG_THRESHOLD;
	range->max_frag = MAX_FRAG_THRESHOLD;

	range->min_pmp = 0;
	range->max_pmp = 5000000;
	range->min_pmt = 0;
	range->max_pmt = 65535 * 1000;
	range->pmp_flags = IW_POWER_PERIOD;
	range->pmt_flags = IW_POWER_TIMEOUT;
	range->pm_capa = IW_POWER_PERIOD | IW_POWER_TIMEOUT | IW_POWER_ALL_R;
	range->we_version_compiled = WIRELESS_EXT;
	range->we_version_source = 18;

	for (i = 0, val = 0; i < 14; i++) {
		if ((priv->rtllib->active_channel_map)[i + 1]) {
			s32 freq_khz;

			range->freq[val].i = i + 1;
			freq_khz = ieee80211_channel_to_freq_khz(i + 1, NL80211_BAND_2GHZ);
			range->freq[val].m = freq_khz * 100;
			range->freq[val].e = 1;
			val++;
		}

		if (val == IW_MAX_FREQUENCIES)
			break;
	}
	range->num_frequency = val;
	range->num_channels = val;
	range->enc_capa = IW_ENC_CAPA_WPA | IW_ENC_CAPA_WPA2 |
			  IW_ENC_CAPA_CIPHER_TKIP | IW_ENC_CAPA_CIPHER_CCMP;
	range->scan_capa = IW_SCAN_CAPA_ESSID | IW_SCAN_CAPA_TYPE;

	/* Event capability (kernel + driver) */

	return 0;
}

static int _rtl92e_wx_set_scan(struct net_device *dev,
			       struct iw_request_info *a,
			       union iwreq_data *wrqu, char *b)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	struct rtllib_device *ieee = priv->rtllib;
	enum rt_rf_power_state rt_state;
	int ret;

	if (!(ieee->softmac_features & IEEE_SOFTMAC_SCAN)) {
		if ((ieee->link_state >= RTLLIB_ASSOCIATING) &&
		    (ieee->link_state <= RTLLIB_ASSOCIATING_AUTHENTICATED))
			return 0;
		if ((priv->rtllib->link_state == MAC80211_LINKED) &&
		    (priv->rtllib->cnt_after_link < 2))
			return 0;
	}

	if (priv->hw_radio_off) {
		netdev_info(dev, "================>%s(): hwradio off\n",
			    __func__);
		return 0;
	}
	rt_state = priv->rtllib->rf_power_state;
	if (!priv->up)
		return -ENETDOWN;
	if (priv->rtllib->link_detect_info.busy_traffic)
		return -EAGAIN;

	if (wrqu->data.flags & IW_SCAN_THIS_ESSID) {
		struct iw_scan_req *req = (struct iw_scan_req *)b;

		if (req->essid_len) {
			int len = min_t(int, req->essid_len, IW_ESSID_MAX_SIZE);

			ieee->current_network.ssid_len = len;
			memcpy(ieee->current_network.ssid, req->essid, len);
		}
	}

	mutex_lock(&priv->wx_mutex);

	priv->rtllib->first_ie_in_scan = true;

	if (priv->rtllib->link_state != MAC80211_LINKED) {
		if (rt_state == rf_off) {
			if (priv->rtllib->rf_off_reason >
			    RF_CHANGE_BY_IPS) {
				netdev_warn(dev, "%s(): RF is OFF.\n",
					    __func__);
				mutex_unlock(&priv->wx_mutex);
				return -1;
			}
			mutex_lock(&priv->rtllib->ips_mutex);
			rtl92e_ips_leave(dev);
			mutex_unlock(&priv->rtllib->ips_mutex);
		}
		rtllib_stop_scan(priv->rtllib);
		if (priv->rtllib->rf_power_state != rf_off) {
			priv->rtllib->actscanning = true;

			ieee->scan_operation_backup_handler(ieee->dev, SCAN_OPT_BACKUP);

			rtllib_start_scan_syncro(priv->rtllib);

			ieee->scan_operation_backup_handler(ieee->dev, SCAN_OPT_RESTORE);
		}
		ret = 0;
	} else {
		priv->rtllib->actscanning = true;
		ret = rtllib_wx_set_scan(priv->rtllib, a, wrqu, b);
	}

	mutex_unlock(&priv->wx_mutex);
	return ret;
}

static int _rtl92e_wx_get_scan(struct net_device *dev,
			       struct iw_request_info *a,
			       union iwreq_data *wrqu, char *b)
{
	int ret;
	struct r8192_priv *priv = rtllib_priv(dev);

	if (!priv->up)
		return -ENETDOWN;

	if (priv->hw_radio_off)
		return 0;

	mutex_lock(&priv->wx_mutex);

	ret = rtllib_wx_get_scan(priv->rtllib, a, wrqu, b);

	mutex_unlock(&priv->wx_mutex);

	return ret;
}

static int _rtl92e_wx_set_essid(struct net_device *dev,
				struct iw_request_info *a,
				union iwreq_data *wrqu, char *b)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	int ret;

	if (priv->hw_radio_off) {
		netdev_info(dev,
			    "=========>%s():hw radio off,or Rf state is rf_off, return\n",
			    __func__);
		return 0;
	}
	mutex_lock(&priv->wx_mutex);
	ret = rtllib_wx_set_essid(priv->rtllib, a, wrqu, b);

	mutex_unlock(&priv->wx_mutex);

	return ret;
}

static int _rtl92e_wx_get_essid(struct net_device *dev,
				struct iw_request_info *a,
				union iwreq_data *wrqu, char *b)
{
	int ret;
	struct r8192_priv *priv = rtllib_priv(dev);

	mutex_lock(&priv->wx_mutex);

	ret = rtllib_wx_get_essid(priv->rtllib, a, wrqu, b);

	mutex_unlock(&priv->wx_mutex);

	return ret;
}

static int _rtl92e_wx_set_nick(struct net_device *dev,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	if (wrqu->data.length > IW_ESSID_MAX_SIZE)
		return -E2BIG;
	mutex_lock(&priv->wx_mutex);
	wrqu->data.length = min_t(size_t, wrqu->data.length,
				  sizeof(priv->nick));
	memset(priv->nick, 0, sizeof(priv->nick));
	memcpy(priv->nick, extra, wrqu->data.length);
	mutex_unlock(&priv->wx_mutex);
	return 0;
}

static int _rtl92e_wx_get_nick(struct net_device *dev,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	mutex_lock(&priv->wx_mutex);
	wrqu->data.length = strlen(priv->nick);
	memcpy(extra, priv->nick, wrqu->data.length);
	wrqu->data.flags = 1;   /* active */
	mutex_unlock(&priv->wx_mutex);
	return 0;
}

static int _rtl92e_wx_set_freq(struct net_device *dev,
			       struct iw_request_info *a,
			       union iwreq_data *wrqu, char *b)
{
	int ret;
	struct r8192_priv *priv = rtllib_priv(dev);

	if (priv->hw_radio_off)
		return 0;

	mutex_lock(&priv->wx_mutex);

	ret = rtllib_wx_set_freq(priv->rtllib, a, wrqu, b);

	mutex_unlock(&priv->wx_mutex);
	return ret;
}

static int _rtl92e_wx_get_name(struct net_device *dev,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	return rtllib_wx_get_name(priv->rtllib, info, wrqu, extra);
}

static int _rtl92e_wx_set_frag(struct net_device *dev,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	if (priv->hw_radio_off)
		return 0;

	if (wrqu->frag.disabled) {
		priv->rtllib->fts = DEFAULT_FRAG_THRESHOLD;
	} else {
		if (wrqu->frag.value < MIN_FRAG_THRESHOLD ||
		    wrqu->frag.value > MAX_FRAG_THRESHOLD)
			return -EINVAL;

		priv->rtllib->fts = wrqu->frag.value & ~0x1;
	}

	return 0;
}

static int _rtl92e_wx_get_frag(struct net_device *dev,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	wrqu->frag.value = priv->rtllib->fts;
	wrqu->frag.fixed = 0;	/* no auto select */
	wrqu->frag.disabled = (wrqu->frag.value == DEFAULT_FRAG_THRESHOLD);

	return 0;
}

static int _rtl92e_wx_set_wap(struct net_device *dev,
			      struct iw_request_info *info,
			      union iwreq_data *awrq, char *extra)
{
	int ret;
	struct r8192_priv *priv = rtllib_priv(dev);

	if (priv->hw_radio_off)
		return 0;

	mutex_lock(&priv->wx_mutex);

	ret = rtllib_wx_set_wap(priv->rtllib, info, awrq, extra);

	mutex_unlock(&priv->wx_mutex);

	return ret;
}

static int _rtl92e_wx_get_wap(struct net_device *dev,
			      struct iw_request_info *info,
			      union iwreq_data *wrqu, char *extra)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	return rtllib_wx_get_wap(priv->rtllib, info, wrqu, extra);
}

static int _rtl92e_wx_get_enc(struct net_device *dev,
			      struct iw_request_info *info,
			      union iwreq_data *wrqu, char *key)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	return rtllib_wx_get_encode(priv->rtllib, info, wrqu, key);
}

static int _rtl92e_wx_set_enc(struct net_device *dev,
			      struct iw_request_info *info,
			      union iwreq_data *wrqu, char *key)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	int ret;

	struct rtllib_device *ieee = priv->rtllib;
	u32 hwkey[4] = {0, 0, 0, 0};
	u8 mask = 0xff;
	u32 key_idx = 0;
	u8 zero_addr[4][6] = {{0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
			     {0x00, 0x00, 0x00, 0x00, 0x00, 0x01},
			     {0x00, 0x00, 0x00, 0x00, 0x00, 0x02},
			     {0x00, 0x00, 0x00, 0x00, 0x00, 0x03} };
	int i;

	if (priv->hw_radio_off)
		return 0;

	if (!priv->up)
		return -ENETDOWN;

	priv->rtllib->wx_set_enc = 1;
	mutex_lock(&priv->rtllib->ips_mutex);
	rtl92e_ips_leave(dev);
	mutex_unlock(&priv->rtllib->ips_mutex);
	mutex_lock(&priv->wx_mutex);

	ret = rtllib_wx_set_encode(priv->rtllib, info, wrqu, key);
	mutex_unlock(&priv->wx_mutex);

	if (wrqu->encoding.flags & IW_ENCODE_DISABLED) {
		ieee->pairwise_key_type = KEY_TYPE_NA;
		ieee->group_key_type = KEY_TYPE_NA;
		rtl92e_cam_reset(dev);
		memset(priv->rtllib->swcamtable, 0,
		       sizeof(struct sw_cam_table) * 32);
		goto end_hw_sec;
	}
	if (wrqu->encoding.length != 0) {
		for (i = 0; i < 4; i++) {
			hwkey[i] |=  key[4 * i + 0] & mask;
			if (i == 1 && (4 * i + 1) == wrqu->encoding.length)
				mask = 0x00;
			if (i == 3 && (4 * i + 1) == wrqu->encoding.length)
				mask = 0x00;
			hwkey[i] |= (key[4 * i + 1] & mask) << 8;
			hwkey[i] |= (key[4 * i + 2] & mask) << 16;
			hwkey[i] |= (key[4 * i + 3] & mask) << 24;
		}

		switch (wrqu->encoding.flags & IW_ENCODE_INDEX) {
		case 0:
			key_idx = ieee->crypt_info.tx_keyidx;
			break;
		case 1:
			key_idx = 0;
			break;
		case 2:
			key_idx = 1;
			break;
		case 3:
			key_idx = 2;
			break;
		case 4:
			key_idx	= 3;
			break;
		default:
			break;
		}
		if (wrqu->encoding.length == 0x5) {
			ieee->pairwise_key_type = KEY_TYPE_WEP40;
			rtl92e_enable_hw_security_config(dev);
		}

		else if (wrqu->encoding.length == 0xd) {
			ieee->pairwise_key_type = KEY_TYPE_WEP104;
			rtl92e_enable_hw_security_config(dev);
			rtl92e_set_key(dev, key_idx, key_idx, KEY_TYPE_WEP104,
				       zero_addr[key_idx], 0, hwkey);
			rtl92e_set_swcam(dev, key_idx, key_idx, KEY_TYPE_WEP104,
					 zero_addr[key_idx], hwkey);
		} else {
			netdev_info(dev,
				    "wrong type in WEP, not WEP40 and WEP104\n");
		}
	}

end_hw_sec:
	priv->rtllib->wx_set_enc = 0;
	return ret;
}

#define R8192_MAX_RETRY 255
static int _rtl92e_wx_set_retry(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *extra)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	int err = 0;

	if (priv->hw_radio_off)
		return 0;

	mutex_lock(&priv->wx_mutex);

	if (wrqu->retry.flags & IW_RETRY_LIFETIME ||
	    wrqu->retry.disabled) {
		err = -EINVAL;
		goto exit;
	}
	if (!(wrqu->retry.flags & IW_RETRY_LIMIT)) {
		err = -EINVAL;
		goto exit;
	}

	if (wrqu->retry.value > R8192_MAX_RETRY) {
		err = -EINVAL;
		goto exit;
	}
	if (wrqu->retry.flags & IW_RETRY_MAX)
		priv->retry_rts = wrqu->retry.value;
	else
		priv->retry_data = wrqu->retry.value;

	rtl92e_commit(dev);
exit:
	mutex_unlock(&priv->wx_mutex);

	return err;
}

static int _rtl92e_wx_get_retry(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *extra)
{
	struct r8192_priv *priv = rtllib_priv(dev);

	wrqu->retry.disabled = 0; /* can't be disabled */

	if ((wrqu->retry.flags & IW_RETRY_TYPE) ==
	    IW_RETRY_LIFETIME)
		return -EINVAL;

	if (wrqu->retry.flags & IW_RETRY_MAX) {
		wrqu->retry.flags = IW_RETRY_LIMIT | IW_RETRY_MAX;
		wrqu->retry.value = priv->retry_rts;
	} else {
		wrqu->retry.flags = IW_RETRY_LIMIT | IW_RETRY_MIN;
		wrqu->retry.value = priv->retry_data;
	}
	return 0;
}

static int _rtl92e_wx_set_encode_ext(struct net_device *dev,
				     struct iw_request_info *info,
				     union iwreq_data *wrqu, char *extra)
{
	int ret = 0;
	struct r8192_priv *priv = rtllib_priv(dev);
	struct rtllib_device *ieee = priv->rtllib;

	if (priv->hw_radio_off)
		return 0;

	mutex_lock(&priv->wx_mutex);

	priv->rtllib->wx_set_enc = 1;
	mutex_lock(&priv->rtllib->ips_mutex);
	rtl92e_ips_leave(dev);
	mutex_unlock(&priv->rtllib->ips_mutex);

	ret = rtllib_wx_set_encode_ext(ieee, info, wrqu, extra);
	{
		const u8 broadcast_addr[ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
		const u8 zero[ETH_ALEN] = {0};
		u32 key[4] = {0};
		struct iw_encode_ext *ext = (struct iw_encode_ext *)extra;
		struct iw_point *encoding = &wrqu->encoding;
		u8 idx = 0, alg = 0, group = 0;

		if ((encoding->flags & IW_ENCODE_DISABLED) ||
		    ext->alg == IW_ENCODE_ALG_NONE) {
			ieee->pairwise_key_type = KEY_TYPE_NA;
			ieee->group_key_type = KEY_TYPE_NA;
			rtl92e_cam_reset(dev);
			memset(priv->rtllib->swcamtable, 0,
			       sizeof(struct sw_cam_table) * 32);
			goto end_hw_sec;
		}
		alg = (ext->alg == IW_ENCODE_ALG_CCMP) ? KEY_TYPE_CCMP :
		      ext->alg;
		idx = encoding->flags & IW_ENCODE_INDEX;
		if (idx)
			idx--;
		group = ext->ext_flags & IW_ENCODE_EXT_GROUP_KEY;

		if ((!group) || (alg ==  KEY_TYPE_WEP40)) {
			if ((ext->key_len == 13) && (alg == KEY_TYPE_WEP40))
				alg = KEY_TYPE_WEP104;
			ieee->pairwise_key_type = alg;
			rtl92e_enable_hw_security_config(dev);
		}
		memcpy((u8 *)key, ext->key, 16);

		if ((alg & KEY_TYPE_WEP40) && (ieee->auth_mode != 2)) {
			if (ext->key_len == 13)
				ieee->pairwise_key_type = alg = KEY_TYPE_WEP104;
			rtl92e_set_key(dev, idx, idx, alg, zero, 0, key);
			rtl92e_set_swcam(dev, idx, idx, alg, zero, key);
		} else if (group) {
			ieee->group_key_type = alg;
			rtl92e_set_key(dev, idx, idx, alg, broadcast_addr, 0,
				       key);
			rtl92e_set_swcam(dev, idx, idx, alg, broadcast_addr, key);
		} else {
			if ((ieee->pairwise_key_type == KEY_TYPE_CCMP) &&
			    ieee->ht_info->current_ht_support)
				rtl92e_writeb(dev, 0x173, 1);
			rtl92e_set_key(dev, 4, idx, alg,
				       (u8 *)ieee->ap_mac_addr, 0, key);
			rtl92e_set_swcam(dev, 4, idx, alg, (u8 *)ieee->ap_mac_addr, key);
		}
	}

end_hw_sec:
	priv->rtllib->wx_set_enc = 0;
	mutex_unlock(&priv->wx_mutex);
	return ret;
}

static int _rtl92e_wx_set_auth(struct net_device *dev,
			       struct iw_request_info *info,
			       union iwreq_data *data, char *extra)
{
	int ret = 0;

	struct r8192_priv *priv = rtllib_priv(dev);

	if (priv->hw_radio_off)
		return 0;

	mutex_lock(&priv->wx_mutex);
	ret = rtllib_wx_set_auth(priv->rtllib, info, &data->param, extra);
	mutex_unlock(&priv->wx_mutex);
	return ret;
}

static int _rtl92e_wx_set_mlme(struct net_device *dev,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)
{
	int ret = 0;

	struct r8192_priv *priv = rtllib_priv(dev);

	if (priv->hw_radio_off)
		return 0;

	mutex_lock(&priv->wx_mutex);
	ret = rtllib_wx_set_mlme(priv->rtllib, info, wrqu, extra);
	mutex_unlock(&priv->wx_mutex);
	return ret;
}

static int _rtl92e_wx_set_gen_ie(struct net_device *dev,
				 struct iw_request_info *info,
				 union iwreq_data *data, char *extra)
{
	int ret = 0;

	struct r8192_priv *priv = rtllib_priv(dev);

	if (priv->hw_radio_off)
		return 0;

	mutex_lock(&priv->wx_mutex);
	ret = rtllib_wx_set_gen_ie(priv->rtllib, extra, data->data.length);
	mutex_unlock(&priv->wx_mutex);
	return ret;
}

static int _rtl92e_wx_get_gen_ie(struct net_device *dev,
				 struct iw_request_info *info,
				 union iwreq_data *data, char *extra)
{
	int ret = 0;
	struct r8192_priv *priv = rtllib_priv(dev);
	struct rtllib_device *ieee = priv->rtllib;

	if (ieee->wpa_ie_len == 0 || !ieee->wpa_ie) {
		data->data.length = 0;
		return 0;
	}

	if (data->data.length < ieee->wpa_ie_len)
		return -E2BIG;

	data->data.length = ieee->wpa_ie_len;
	memcpy(extra, ieee->wpa_ie, ieee->wpa_ie_len);
	return ret;
}

#define IW_IOCTL(x) ((x) - SIOCSIWCOMMIT)
static iw_handler r8192_wx_handlers[] = {
	[IW_IOCTL(SIOCGIWNAME)] = _rtl92e_wx_get_name,
	[IW_IOCTL(SIOCSIWFREQ)] = _rtl92e_wx_set_freq,
	[IW_IOCTL(SIOCGIWFREQ)] = _rtl92e_wx_get_freq,
	[IW_IOCTL(SIOCSIWMODE)] = _rtl92e_wx_set_mode,
	[IW_IOCTL(SIOCGIWMODE)] = _rtl92e_wx_get_mode,
	[IW_IOCTL(SIOCGIWRANGE)] = _rtl92e_wx_get_range,
	[IW_IOCTL(SIOCSIWAP)] = _rtl92e_wx_set_wap,
	[IW_IOCTL(SIOCGIWAP)] = _rtl92e_wx_get_wap,
	[IW_IOCTL(SIOCSIWSCAN)] = _rtl92e_wx_set_scan,
	[IW_IOCTL(SIOCGIWSCAN)] = _rtl92e_wx_get_scan,
	[IW_IOCTL(SIOCSIWESSID)] = _rtl92e_wx_set_essid,
	[IW_IOCTL(SIOCGIWESSID)] = _rtl92e_wx_get_essid,
	[IW_IOCTL(SIOCSIWNICKN)] = _rtl92e_wx_set_nick,
	[IW_IOCTL(SIOCGIWNICKN)] = _rtl92e_wx_get_nick,
	[IW_IOCTL(SIOCSIWRATE)] = _rtl92e_wx_set_rate,
	[IW_IOCTL(SIOCGIWRATE)] = _rtl92e_wx_get_rate,
	[IW_IOCTL(SIOCSIWRTS)] = _rtl92e_wx_set_rts,
	[IW_IOCTL(SIOCGIWRTS)] = _rtl92e_wx_get_rts,
	[IW_IOCTL(SIOCSIWFRAG)] = _rtl92e_wx_set_frag,
	[IW_IOCTL(SIOCGIWFRAG)] = _rtl92e_wx_get_frag,
	[IW_IOCTL(SIOCSIWRETRY)] = _rtl92e_wx_set_retry,
	[IW_IOCTL(SIOCGIWRETRY)] = _rtl92e_wx_get_retry,
	[IW_IOCTL(SIOCSIWENCODE)] = _rtl92e_wx_set_enc,
	[IW_IOCTL(SIOCGIWENCODE)] = _rtl92e_wx_get_enc,
	[IW_IOCTL(SIOCSIWPOWER)] = _rtl92e_wx_set_power,
	[IW_IOCTL(SIOCGIWPOWER)] = _rtl92e_wx_get_power,
	[IW_IOCTL(SIOCSIWGENIE)] = _rtl92e_wx_set_gen_ie,
	[IW_IOCTL(SIOCGIWGENIE)] = _rtl92e_wx_get_gen_ie,
	[IW_IOCTL(SIOCSIWMLME)] = _rtl92e_wx_set_mlme,
	[IW_IOCTL(SIOCSIWAUTH)] = _rtl92e_wx_set_auth,
	[IW_IOCTL(SIOCSIWENCODEEXT)] = _rtl92e_wx_set_encode_ext,
};

static struct iw_statistics *_rtl92e_get_wireless_stats(struct net_device *dev)
{
	struct r8192_priv *priv = rtllib_priv(dev);
	struct rtllib_device *ieee = priv->rtllib;
	struct iw_statistics *wstats = &priv->wstats;
	int tmp_level = 0;
	int tmp_qual = 0;
	int tmp_noise = 0;

	if (ieee->link_state < MAC80211_LINKED) {
		wstats->qual.qual = 10;
		wstats->qual.level = 0;
		wstats->qual.noise = 0x100 - 100;	/* -100 dBm */
		wstats->qual.updated = IW_QUAL_ALL_UPDATED | IW_QUAL_DBM;
		return wstats;
	}

	tmp_level = (&ieee->current_network)->stats.rssi;
	tmp_qual = (&ieee->current_network)->stats.signal;
	tmp_noise = (&ieee->current_network)->stats.noise;

	wstats->qual.level = tmp_level;
	wstats->qual.qual = tmp_qual;
	wstats->qual.noise = tmp_noise;
	wstats->qual.updated = IW_QUAL_ALL_UPDATED | IW_QUAL_DBM;
	return wstats;
}

const struct iw_handler_def r8192_wx_handlers_def = {
	.standard = r8192_wx_handlers,
	.num_standard = ARRAY_SIZE(r8192_wx_handlers),
	.get_wireless_stats = _rtl92e_get_wireless_stats,
};
