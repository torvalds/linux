/*
 * Intel Wireless Multicomm 3200 WiFi driver
 *
 * Copyright (C) 2009 Intel Corporation <ilw@linux.intel.com>
 * Samuel Ortiz <samuel.ortiz@intel.com>
 * Zhu Yi <yi.zhu@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */

#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/wireless.h>
#include <linux/if_arp.h>
#include <linux/etherdevice.h>
#include <net/cfg80211.h>
#include <net/iw_handler.h>

#include "iwm.h"
#include "umac.h"
#include "commands.h"
#include "debug.h"

static struct iw_statistics *iwm_get_wireless_stats(struct net_device *dev)
{
	struct iwm_priv *iwm = ndev_to_iwm(dev);
	struct iw_statistics *wstats = &iwm->wstats;

	if (!test_bit(IWM_STATUS_ASSOCIATED, &iwm->status)) {
		memset(wstats, 0, sizeof(struct iw_statistics));
		wstats->qual.updated = IW_QUAL_ALL_INVALID;
	}

	return wstats;
}

static int iwm_wext_siwfreq(struct net_device *dev,
			    struct iw_request_info *info,
			    struct iw_freq *freq, char *extra)
{
	struct iwm_priv *iwm = ndev_to_iwm(dev);

	if (freq->flags == IW_FREQ_AUTO)
		return 0;

	/* frequency/channel can only be set in IBSS mode */
	if (iwm->conf.mode != UMAC_MODE_IBSS)
		return -EOPNOTSUPP;

	return cfg80211_ibss_wext_siwfreq(dev, info, freq, extra);
}

static int iwm_wext_giwfreq(struct net_device *dev,
			    struct iw_request_info *info,
			    struct iw_freq *freq, char *extra)
{
	struct iwm_priv *iwm = ndev_to_iwm(dev);

	if (iwm->conf.mode == UMAC_MODE_IBSS)
		return cfg80211_ibss_wext_giwfreq(dev, info, freq, extra);

	freq->e = 0;
	freq->m = iwm->channel;

	return 0;
}

static int iwm_wext_siwap(struct net_device *dev, struct iw_request_info *info,
			  struct sockaddr *ap_addr, char *extra)
{
	struct iwm_priv *iwm = ndev_to_iwm(dev);

	if (iwm->conf.mode == UMAC_MODE_IBSS)
		return cfg80211_ibss_wext_siwap(dev, info, ap_addr, extra);

	if (!test_bit(IWM_STATUS_READY, &iwm->status))
		return -EIO;

	if (is_zero_ether_addr(ap_addr->sa_data) ||
	    is_broadcast_ether_addr(ap_addr->sa_data)) {
		IWM_DBG_WEXT(iwm, DBG, "clear mandatory bssid %pM\n",
			     iwm->umac_profile->bssid[0]);
		memset(&iwm->umac_profile->bssid[0], 0, ETH_ALEN);
		iwm->umac_profile->bss_num = 0;
	} else {
		IWM_DBG_WEXT(iwm, DBG, "add mandatory bssid %pM\n",
			     ap_addr->sa_data);
		memcpy(&iwm->umac_profile->bssid[0], ap_addr->sa_data,
		       ETH_ALEN);
		iwm->umac_profile->bss_num = 1;
	}

	if (iwm->umac_profile_active) {
		if (!memcmp(&iwm->umac_profile->bssid[0], iwm->bssid, ETH_ALEN))
			return 0;

		iwm_invalidate_mlme_profile(iwm);
	}

	if (iwm->umac_profile->ssid.ssid_len)
		return iwm_send_mlme_profile(iwm);

	return 0;
}

static int iwm_wext_giwap(struct net_device *dev, struct iw_request_info *info,
			  struct sockaddr *ap_addr, char *extra)
{
	struct iwm_priv *iwm = ndev_to_iwm(dev);

	switch (iwm->conf.mode) {
	case UMAC_MODE_IBSS:
		return cfg80211_ibss_wext_giwap(dev, info, ap_addr, extra);
	case UMAC_MODE_BSS:
		if (test_bit(IWM_STATUS_ASSOCIATED, &iwm->status)) {
			ap_addr->sa_family = ARPHRD_ETHER;
			memcpy(&ap_addr->sa_data, iwm->bssid, ETH_ALEN);
		} else
			memset(&ap_addr->sa_data, 0, ETH_ALEN);
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int iwm_wext_siwessid(struct net_device *dev,
			     struct iw_request_info *info,
			     struct iw_point *data, char *ssid)
{
	struct iwm_priv *iwm = ndev_to_iwm(dev);
	size_t len = data->length;
	int ret;

	if (iwm->conf.mode == UMAC_MODE_IBSS)
		return cfg80211_ibss_wext_siwessid(dev, info, data, ssid);

	if (!test_bit(IWM_STATUS_READY, &iwm->status))
		return -EIO;

	if (len > 0 && ssid[len - 1] == '\0')
		len--;

	if (iwm->umac_profile_active) {
		if (iwm->umac_profile->ssid.ssid_len == len &&
		    !memcmp(iwm->umac_profile->ssid.ssid, ssid, len))
			return 0;

		ret = iwm_invalidate_mlme_profile(iwm);
		if (ret < 0) {
			IWM_ERR(iwm, "Couldn't invalidate profile\n");
			return ret;
		}
	}

	iwm->umac_profile->ssid.ssid_len = len;
	memcpy(iwm->umac_profile->ssid.ssid, ssid, len);

	return iwm_send_mlme_profile(iwm);
}

static int iwm_wext_giwessid(struct net_device *dev,
			     struct iw_request_info *info,
			     struct iw_point *data, char *ssid)
{
	struct iwm_priv *iwm = ndev_to_iwm(dev);

	if (iwm->conf.mode == UMAC_MODE_IBSS)
		return cfg80211_ibss_wext_giwessid(dev, info, data, ssid);

	if (!test_bit(IWM_STATUS_READY, &iwm->status))
		return -EIO;

	data->length = iwm->umac_profile->ssid.ssid_len;
	if (data->length) {
		memcpy(ssid, iwm->umac_profile->ssid.ssid, data->length);
		data->flags = 1;
	} else
		data->flags = 0;

	return 0;
}

static struct iwm_key *
iwm_key_init(struct iwm_priv *iwm, u8 key_idx, bool in_use,
	     struct iw_encode_ext *ext, u8 alg)
{
	struct iwm_key *key = &iwm->keys[key_idx];

	memset(key, 0, sizeof(struct iwm_key));
	memcpy(key->hdr.mac, ext->addr.sa_data, ETH_ALEN);
	key->hdr.key_idx = key_idx;
	if (is_broadcast_ether_addr(ext->addr.sa_data))
		key->hdr.multicast = 1;

	key->in_use = in_use;
	key->flags = ext->ext_flags;
	key->alg = alg;
	key->key_len = ext->key_len;
	memcpy(key->key, ext->key, ext->key_len);

	return key;
}

static int iwm_wext_giwrate(struct net_device *dev,
			    struct iw_request_info *info,
			    struct iw_param *rate, char *extra)
{
	struct iwm_priv *iwm = ndev_to_iwm(dev);

	rate->value = iwm->rate * 1000000;

	return 0;
}

static int iwm_wext_siwencode(struct net_device *dev,
			      struct iw_request_info *info,
			      struct iw_point *erq, char *key_buf)
{
	struct iwm_priv *iwm = ndev_to_iwm(dev);
	struct iwm_key *uninitialized_var(key);
	int idx, i, uninitialized_var(alg), remove = 0, ret;

	IWM_DBG_WEXT(iwm, DBG, "key len: %d\n", erq->length);
	IWM_DBG_WEXT(iwm, DBG, "flags: 0x%x\n", erq->flags);

	if (!iwm->umac_profile) {
		IWM_ERR(iwm, "UMAC profile not allocated yet\n");
		return -ENODEV;
	}

	if (erq->length == WLAN_KEY_LEN_WEP40) {
		alg = UMAC_CIPHER_TYPE_WEP_40;
		iwm->umac_profile->sec.ucast_cipher = UMAC_CIPHER_TYPE_WEP_40;
		iwm->umac_profile->sec.mcast_cipher = UMAC_CIPHER_TYPE_WEP_40;
	} else if (erq->length == WLAN_KEY_LEN_WEP104) {
		alg = UMAC_CIPHER_TYPE_WEP_104;
		iwm->umac_profile->sec.ucast_cipher = UMAC_CIPHER_TYPE_WEP_104;
		iwm->umac_profile->sec.mcast_cipher = UMAC_CIPHER_TYPE_WEP_104;
	}

	if (erq->flags & IW_ENCODE_RESTRICTED)
		iwm->umac_profile->sec.auth_type = UMAC_AUTH_TYPE_LEGACY_PSK;
	else
		iwm->umac_profile->sec.auth_type = UMAC_AUTH_TYPE_OPEN;

	idx = erq->flags & IW_ENCODE_INDEX;
	if (idx == 0) {
		if (iwm->default_key)
			for (i = 0; i < IWM_NUM_KEYS; i++) {
				if (iwm->default_key == &iwm->keys[i]) {
					idx = i;
					break;
				}
			}
		else
			iwm->default_key = &iwm->keys[idx];
	} else if (idx < 1 || idx > 4) {
		return -EINVAL;
	} else
		idx--;

	if (erq->flags & IW_ENCODE_DISABLED)
		remove = 1;
	else if (erq->length == 0) {
		if (!iwm->keys[idx].in_use)
			return -EINVAL;
		iwm->default_key = &iwm->keys[idx];
	}

	if (erq->length) {
		key = &iwm->keys[idx];
		memset(key, 0, sizeof(struct iwm_key));
		memset(key->hdr.mac, 0xff, ETH_ALEN);
		key->hdr.key_idx = idx;
		key->hdr.multicast = 1;
		key->in_use = !remove;
		key->alg = alg;
		key->key_len = erq->length;
		memcpy(key->key, key_buf, erq->length);

		IWM_DBG_WEXT(iwm, DBG, "Setting key %d, default: %d\n",
			     idx, !!iwm->default_key);
	}

	if (remove) {
		if ((erq->flags & IW_ENCODE_NOKEY) || (erq->length == 0)) {
			int j;
			for (j = 0; j < IWM_NUM_KEYS; j++)
				if (iwm->keys[j].in_use) {
					struct iwm_key *k = &iwm->keys[j];

					k->in_use = 0;
					ret = iwm_set_key(iwm, remove, 0, k);
					if (ret < 0)
						return ret;
				}

			iwm->umac_profile->sec.ucast_cipher =
							UMAC_CIPHER_TYPE_NONE;
			iwm->umac_profile->sec.mcast_cipher =
							UMAC_CIPHER_TYPE_NONE;
			iwm->umac_profile->sec.auth_type =
							UMAC_AUTH_TYPE_OPEN;

			return 0;
		} else {
			key->in_use = 0;
			return iwm_set_key(iwm, remove, 0, key);
		}
	}

	/*
	 * If we havent set a profile yet, we cant set keys.
	 * Keys will be pushed after we're associated.
	 */
	if (!iwm->umac_profile_active)
		return 0;

	/*
	 * If there is a current active profile, but no
	 * default key, it's not worth trying to associate again.
	 */
	if (!iwm->default_key)
		return 0;

	/*
	 * Here we have an active profile, but a key setting changed.
	 * We thus have to invalidate the current profile, and push the
	 * new one. Keys will be pushed when association takes place.
	 */
	ret = iwm_invalidate_mlme_profile(iwm);
	if (ret < 0) {
		IWM_ERR(iwm, "Couldn't invalidate profile\n");
		return ret;
	}

	return iwm_send_mlme_profile(iwm);
}

static int iwm_wext_giwencode(struct net_device *dev,
			      struct iw_request_info *info,
			      struct iw_point *erq, char *key)
{
	struct iwm_priv *iwm = ndev_to_iwm(dev);
	int idx, i;

	idx = erq->flags & IW_ENCODE_INDEX;
	if (idx < 1 || idx > 4) {
		idx = -1;
		if (!iwm->default_key) {
			erq->length = 0;
			erq->flags |= IW_ENCODE_NOKEY;
			return 0;
		} else
			for (i = 0; i < IWM_NUM_KEYS; i++) {
				if (iwm->default_key == &iwm->keys[i]) {
					idx = i;
					break;
				}
			}
		if (idx < 0)
			return -EINVAL;
	} else
		idx--;

	erq->flags = idx + 1;

	if (!iwm->keys[idx].in_use) {
		erq->length = 0;
		erq->flags |= IW_ENCODE_DISABLED;
		return 0;
	}

	memcpy(key, iwm->keys[idx].key,
	       min_t(int, erq->length, iwm->keys[idx].key_len));
	erq->length = iwm->keys[idx].key_len;
	erq->flags |= IW_ENCODE_ENABLED;

	if (iwm->umac_profile->mode == UMAC_MODE_BSS) {
		switch (iwm->umac_profile->sec.auth_type) {
		case UMAC_AUTH_TYPE_OPEN:
			erq->flags |= IW_ENCODE_OPEN;
			break;
		default:
			erq->flags |= IW_ENCODE_RESTRICTED;
			break;
		}
	}

	return 0;
}

static int iwm_set_wpa_version(struct iwm_priv *iwm, u8 wpa_version)
{
	if (wpa_version & IW_AUTH_WPA_VERSION_WPA2)
		iwm->umac_profile->sec.flags = UMAC_SEC_FLG_RSNA_ON_MSK;
	else if (wpa_version & IW_AUTH_WPA_VERSION_WPA)
		iwm->umac_profile->sec.flags = UMAC_SEC_FLG_WPA_ON_MSK;
	else
		iwm->umac_profile->sec.flags = UMAC_SEC_FLG_LEGACY_PROFILE;

	return 0;
}

static int iwm_wext_siwpower(struct net_device *dev,
			     struct iw_request_info *info,
			     struct iw_param *wrq, char *extra)
{
	struct iwm_priv *iwm = ndev_to_iwm(dev);
	u32 power_index;

	if (wrq->disabled) {
		power_index = IWM_POWER_INDEX_MIN;
		goto set;
	} else
		power_index = IWM_POWER_INDEX_DEFAULT;

	switch (wrq->flags & IW_POWER_MODE) {
	case IW_POWER_ON:
	case IW_POWER_MODE:
	case IW_POWER_ALL_R:
		break;
	default:
		return -EINVAL;
	}

 set:
	if (power_index == iwm->conf.power_index)
		return 0;

	iwm->conf.power_index = power_index;

	return iwm_umac_set_config_fix(iwm, UMAC_PARAM_TBL_CFG_FIX,
				       CFG_POWER_INDEX, iwm->conf.power_index);
}

static int iwm_wext_giwpower(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *extra)
{
	struct iwm_priv *iwm = ndev_to_iwm(dev);

	wrqu->power.disabled = (iwm->conf.power_index == IWM_POWER_INDEX_MIN);

	return 0;
}

static int iwm_set_key_mgt(struct iwm_priv *iwm, u8 key_mgt)
{
	u8 *auth_type = &iwm->umac_profile->sec.auth_type;

	if (key_mgt == IW_AUTH_KEY_MGMT_802_1X)
		*auth_type = UMAC_AUTH_TYPE_8021X;
	else if (key_mgt == IW_AUTH_KEY_MGMT_PSK) {
		if (iwm->umac_profile->sec.flags &
		    (UMAC_SEC_FLG_WPA_ON_MSK | UMAC_SEC_FLG_RSNA_ON_MSK))
			*auth_type = UMAC_AUTH_TYPE_RSNA_PSK;
		else
			*auth_type = UMAC_AUTH_TYPE_LEGACY_PSK;
	} else {
		IWM_ERR(iwm, "Invalid key mgt: 0x%x\n", key_mgt);
		return -EINVAL;
	}

	return 0;
}

static int iwm_set_cipher(struct iwm_priv *iwm, u8 cipher, u8 ucast)
{
	u8 *profile_cipher = ucast ? &iwm->umac_profile->sec.ucast_cipher :
		&iwm->umac_profile->sec.mcast_cipher;

	switch (cipher) {
	case IW_AUTH_CIPHER_NONE:
		*profile_cipher = UMAC_CIPHER_TYPE_NONE;
		break;
	case IW_AUTH_CIPHER_WEP40:
		*profile_cipher = UMAC_CIPHER_TYPE_WEP_40;
		break;
	case IW_AUTH_CIPHER_TKIP:
		*profile_cipher = UMAC_CIPHER_TYPE_TKIP;
		break;
	case IW_AUTH_CIPHER_CCMP:
		*profile_cipher = UMAC_CIPHER_TYPE_CCMP;
		break;
	case IW_AUTH_CIPHER_WEP104:
		*profile_cipher = UMAC_CIPHER_TYPE_WEP_104;
		break;
	default:
		IWM_ERR(iwm, "Unsupported cipher: 0x%x\n", cipher);
		return -ENOTSUPP;
	}

	return 0;
}

static int iwm_set_auth_alg(struct iwm_priv *iwm, u8 auth_alg)
{
	u8 *auth_type = &iwm->umac_profile->sec.auth_type;

	switch (auth_alg) {
	case IW_AUTH_ALG_OPEN_SYSTEM:
		*auth_type = UMAC_AUTH_TYPE_OPEN;
		break;
	case IW_AUTH_ALG_SHARED_KEY:
		if (iwm->umac_profile->sec.flags &
		    (UMAC_SEC_FLG_WPA_ON_MSK | UMAC_SEC_FLG_RSNA_ON_MSK)) {
			if (*auth_type == UMAC_AUTH_TYPE_8021X)
				return -EINVAL;
			*auth_type = UMAC_AUTH_TYPE_RSNA_PSK;
		} else {
			*auth_type = UMAC_AUTH_TYPE_LEGACY_PSK;
		}
		break;
	case IW_AUTH_ALG_LEAP:
	default:
		IWM_ERR(iwm, "Unsupported auth alg: 0x%x\n", auth_alg);
		return -ENOTSUPP;
	}

	return 0;
}

static int iwm_wext_siwauth(struct net_device *dev,
			    struct iw_request_info *info,
			    struct iw_param *data, char *extra)
{
	struct iwm_priv *iwm = ndev_to_iwm(dev);
	int ret;

	if ((data->flags) &
	    (IW_AUTH_WPA_VERSION | IW_AUTH_KEY_MGMT |
	     IW_AUTH_WPA_ENABLED | IW_AUTH_80211_AUTH_ALG)) {
		/* We need to invalidate the current profile */
		if (iwm->umac_profile_active) {
			ret = iwm_invalidate_mlme_profile(iwm);
			if (ret < 0) {
				IWM_ERR(iwm, "Couldn't invalidate profile\n");
				return ret;
			}
		}
	}

	switch (data->flags & IW_AUTH_INDEX) {
	case IW_AUTH_WPA_VERSION:
		return iwm_set_wpa_version(iwm, data->value);
		break;
	case IW_AUTH_CIPHER_PAIRWISE:
		return iwm_set_cipher(iwm, data->value, 1);
		break;
	case IW_AUTH_CIPHER_GROUP:
		return iwm_set_cipher(iwm, data->value, 0);
		break;
	case IW_AUTH_KEY_MGMT:
		return iwm_set_key_mgt(iwm, data->value);
		break;
	case IW_AUTH_80211_AUTH_ALG:
		return iwm_set_auth_alg(iwm, data->value);
		break;
	default:
		return -ENOTSUPP;
	}

	return 0;
}

static int iwm_wext_giwauth(struct net_device *dev,
			    struct iw_request_info *info,
			    struct iw_param *data, char *extra)
{
	return 0;
}

static int iwm_wext_siwencodeext(struct net_device *dev,
				 struct iw_request_info *info,
				 struct iw_point *erq, char *extra)
{
	struct iwm_priv *iwm = ndev_to_iwm(dev);
	struct iwm_key *key;
	struct iw_encode_ext *ext = (struct iw_encode_ext *) extra;
	int uninitialized_var(alg), idx, i, remove = 0;

	IWM_DBG_WEXT(iwm, DBG, "alg: 0x%x\n", ext->alg);
	IWM_DBG_WEXT(iwm, DBG, "key len: %d\n", ext->key_len);
	IWM_DBG_WEXT(iwm, DBG, "ext_flags: 0x%x\n", ext->ext_flags);
	IWM_DBG_WEXT(iwm, DBG, "flags: 0x%x\n", erq->flags);
	IWM_DBG_WEXT(iwm, DBG, "length: 0x%x\n", erq->length);

	switch (ext->alg) {
	case IW_ENCODE_ALG_NONE:
		remove = 1;
		break;
	case IW_ENCODE_ALG_WEP:
		if (ext->key_len == WLAN_KEY_LEN_WEP40)
			alg = UMAC_CIPHER_TYPE_WEP_40;
		else if (ext->key_len == WLAN_KEY_LEN_WEP104)
			alg = UMAC_CIPHER_TYPE_WEP_104;
		else {
			IWM_ERR(iwm, "Invalid key length: %d\n", ext->key_len);
			return -EINVAL;
		}

		break;
	case IW_ENCODE_ALG_TKIP:
		alg = UMAC_CIPHER_TYPE_TKIP;
		break;
	case IW_ENCODE_ALG_CCMP:
		alg = UMAC_CIPHER_TYPE_CCMP;
		break;
	default:
		return -EOPNOTSUPP;
	}

	idx = erq->flags & IW_ENCODE_INDEX;

	if (idx == 0) {
		if (iwm->default_key)
			for (i = 0; i < IWM_NUM_KEYS; i++) {
				if (iwm->default_key == &iwm->keys[i]) {
					idx = i;
					break;
				}
			}
	} else if (idx < 1 || idx > 4) {
		return -EINVAL;
	} else
		idx--;

	if (erq->flags & IW_ENCODE_DISABLED)
		remove = 1;
	else if ((erq->length == 0) ||
		 (ext->ext_flags & IW_ENCODE_EXT_SET_TX_KEY)) {
		iwm->default_key = &iwm->keys[idx];
		if (iwm->umac_profile_active && ext->alg == IW_ENCODE_ALG_WEP)
			return iwm_set_tx_key(iwm, idx);
	}

	key = iwm_key_init(iwm, idx, !remove, ext, alg);

	return iwm_set_key(iwm, remove, !iwm->default_key, key);
}

static const iw_handler iwm_handlers[] =
{
	(iw_handler) NULL,				/* SIOCSIWCOMMIT */
	(iw_handler) cfg80211_wext_giwname,		/* SIOCGIWNAME */
	(iw_handler) NULL,				/* SIOCSIWNWID */
	(iw_handler) NULL,				/* SIOCGIWNWID */
	(iw_handler) iwm_wext_siwfreq,			/* SIOCSIWFREQ */
	(iw_handler) iwm_wext_giwfreq,			/* SIOCGIWFREQ */
	(iw_handler) cfg80211_wext_siwmode,		/* SIOCSIWMODE */
	(iw_handler) cfg80211_wext_giwmode,		/* SIOCGIWMODE */
	(iw_handler) NULL,				/* SIOCSIWSENS */
	(iw_handler) NULL,				/* SIOCGIWSENS */
	(iw_handler) NULL /* not used */,		/* SIOCSIWRANGE */
	(iw_handler) cfg80211_wext_giwrange,		/* SIOCGIWRANGE */
	(iw_handler) NULL /* not used */,		/* SIOCSIWPRIV */
	(iw_handler) NULL /* kernel code */,		/* SIOCGIWPRIV */
	(iw_handler) NULL /* not used */,		/* SIOCSIWSTATS */
	(iw_handler) NULL /* kernel code */,		/* SIOCGIWSTATS */
	(iw_handler) NULL,				/* SIOCSIWSPY */
	(iw_handler) NULL,				/* SIOCGIWSPY */
	(iw_handler) NULL,				/* SIOCSIWTHRSPY */
	(iw_handler) NULL,				/* SIOCGIWTHRSPY */
	(iw_handler) iwm_wext_siwap,	                /* SIOCSIWAP */
	(iw_handler) iwm_wext_giwap,			/* SIOCGIWAP */
	(iw_handler) NULL,			        /* SIOCSIWMLME */
	(iw_handler) NULL,				/* SIOCGIWAPLIST */
	(iw_handler) cfg80211_wext_siwscan,		/* SIOCSIWSCAN */
	(iw_handler) cfg80211_wext_giwscan,		/* SIOCGIWSCAN */
	(iw_handler) iwm_wext_siwessid,			/* SIOCSIWESSID */
	(iw_handler) iwm_wext_giwessid,			/* SIOCGIWESSID */
	(iw_handler) NULL,				/* SIOCSIWNICKN */
	(iw_handler) NULL,				/* SIOCGIWNICKN */
	(iw_handler) NULL,				/* -- hole -- */
	(iw_handler) NULL,				/* -- hole -- */
	(iw_handler) NULL,				/* SIOCSIWRATE */
	(iw_handler) iwm_wext_giwrate,			/* SIOCGIWRATE */
	(iw_handler) cfg80211_wext_siwrts,		/* SIOCSIWRTS */
	(iw_handler) cfg80211_wext_giwrts,		/* SIOCGIWRTS */
	(iw_handler) cfg80211_wext_siwfrag,	        /* SIOCSIWFRAG */
	(iw_handler) cfg80211_wext_giwfrag,		/* SIOCGIWFRAG */
	(iw_handler) NULL,				/* SIOCSIWTXPOW */
	(iw_handler) NULL,				/* SIOCGIWTXPOW */
	(iw_handler) NULL,				/* SIOCSIWRETRY */
	(iw_handler) NULL,				/* SIOCGIWRETRY */
	(iw_handler) iwm_wext_siwencode,		/* SIOCSIWENCODE */
	(iw_handler) iwm_wext_giwencode,		/* SIOCGIWENCODE */
	(iw_handler) iwm_wext_siwpower,			/* SIOCSIWPOWER */
	(iw_handler) iwm_wext_giwpower,			/* SIOCGIWPOWER */
	(iw_handler) NULL,				/* -- hole -- */
	(iw_handler) NULL,				/* -- hole -- */
	(iw_handler) NULL,                              /* SIOCSIWGENIE */
	(iw_handler) NULL,				/* SIOCGIWGENIE */
	(iw_handler) iwm_wext_siwauth,			/* SIOCSIWAUTH */
	(iw_handler) iwm_wext_giwauth,			/* SIOCGIWAUTH */
	(iw_handler) iwm_wext_siwencodeext,	        /* SIOCSIWENCODEEXT */
	(iw_handler) NULL,				/* SIOCGIWENCODEEXT */
	(iw_handler) NULL,				/* SIOCSIWPMKSA */
	(iw_handler) NULL,				/* -- hole -- */
};

const struct iw_handler_def iwm_iw_handler_def = {
	.num_standard	= ARRAY_SIZE(iwm_handlers),
	.standard	= (iw_handler *) iwm_handlers,
	.get_wireless_stats = iwm_get_wireless_stats,
};

