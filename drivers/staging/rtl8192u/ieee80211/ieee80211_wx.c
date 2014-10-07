/******************************************************************************

  Copyright(c) 2004 Intel Corporation. All rights reserved.

  Portions of this file are based on the WEP enablement code provided by the
  Host AP project hostap-drivers v0.1.3
  Copyright (c) 2001-2002, SSH Communications Security Corp and Jouni Malinen
  <jkmaline@cc.hut.fi>
  Copyright (c) 2002-2003, Jouni Malinen <jkmaline@cc.hut.fi>

  This program is free software; you can redistribute it and/or modify it
  under the terms of version 2 of the GNU General Public License as
  published by the Free Software Foundation.

  This program is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc., 59
  Temple Place - Suite 330, Boston, MA  02111-1307, USA.

  The full GNU General Public License is included in this distribution in the
  file called LICENSE.

  Contact Information:
  James P. Ketrenos <ipw2100-admin@linux.intel.com>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

******************************************************************************/
#include <linux/wireless.h>
#include <linux/kmod.h>
#include <linux/slab.h>
#include <linux/module.h>

#include "ieee80211.h"
struct modes_unit {
	char *mode_string;
	int mode_size;
};
static struct modes_unit ieee80211_modes[] = {
	{"a", 1},
	{"b", 1},
	{"g", 1},
	{"?", 1},
	{"N-24G", 5},
	{"N-5G", 4},
};

#define iwe_stream_add_event_rsl iwe_stream_add_event

#define MAX_CUSTOM_LEN 64
static inline char *rtl819x_translate_scan(struct ieee80211_device *ieee,
					   char *start, char *stop,
					   struct ieee80211_network *network,
					   struct iw_request_info *info)
{
	char custom[MAX_CUSTOM_LEN];
	char proto_name[IFNAMSIZ];
	char *pname = proto_name;
	char *p;
	struct iw_event iwe;
	int i, j;
	u16 max_rate, rate;
	static u8	EWC11NHTCap[] = {0x00, 0x90, 0x4c, 0x33};

	/* First entry *MUST* be the AP MAC address */
	iwe.cmd = SIOCGIWAP;
	iwe.u.ap_addr.sa_family = ARPHRD_ETHER;
	memcpy(iwe.u.ap_addr.sa_data, network->bssid, ETH_ALEN);
	start = iwe_stream_add_event_rsl(info, start, stop, &iwe, IW_EV_ADDR_LEN);
	/* Remaining entries will be displayed in the order we provide them */

	/* Add the ESSID */
	iwe.cmd = SIOCGIWESSID;
	iwe.u.data.flags = 1;
//	if (network->flags & NETWORK_EMPTY_ESSID) {
	if (network->ssid_len == 0) {
		iwe.u.data.length = sizeof("<hidden>");
		start = iwe_stream_add_point(info, start, stop, &iwe, "<hidden>");
	} else {
		iwe.u.data.length = min(network->ssid_len, (u8)32);
		start = iwe_stream_add_point(info, start, stop, &iwe, network->ssid);
	}
	/* Add the protocol name */
	iwe.cmd = SIOCGIWNAME;
	for(i=0; i<ARRAY_SIZE(ieee80211_modes); i++) {
		if(network->mode&(1<<i)) {
			sprintf(pname,ieee80211_modes[i].mode_string,ieee80211_modes[i].mode_size);
			pname +=ieee80211_modes[i].mode_size;
		}
	}
	*pname = '\0';
	snprintf(iwe.u.name, IFNAMSIZ, "IEEE802.11%s", proto_name);
	start = iwe_stream_add_event_rsl(info, start, stop, &iwe, IW_EV_CHAR_LEN);
	/* Add mode */
	iwe.cmd = SIOCGIWMODE;
	if (network->capability &
	    (WLAN_CAPABILITY_BSS | WLAN_CAPABILITY_IBSS)) {
		if (network->capability & WLAN_CAPABILITY_BSS)
			iwe.u.mode = IW_MODE_MASTER;
		else
			iwe.u.mode = IW_MODE_ADHOC;
		start = iwe_stream_add_event_rsl(info, start, stop, &iwe, IW_EV_UINT_LEN);
	}

	/* Add frequency/channel */
	iwe.cmd = SIOCGIWFREQ;
/*	iwe.u.freq.m = ieee80211_frequency(network->channel, network->mode);
	iwe.u.freq.e = 3; */
	iwe.u.freq.m = network->channel;
	iwe.u.freq.e = 0;
	iwe.u.freq.i = 0;
	start = iwe_stream_add_event_rsl(info, start, stop, &iwe, IW_EV_FREQ_LEN);
	/* Add encryption capability */
	iwe.cmd = SIOCGIWENCODE;
	if (network->capability & WLAN_CAPABILITY_PRIVACY)
		iwe.u.data.flags = IW_ENCODE_ENABLED | IW_ENCODE_NOKEY;
	else
		iwe.u.data.flags = IW_ENCODE_DISABLED;
	iwe.u.data.length = 0;
	start = iwe_stream_add_point(info, start, stop, &iwe, network->ssid);
	/* Add basic and extended rates */
	max_rate = 0;
	p = custom;
	p += snprintf(p, MAX_CUSTOM_LEN - (p - custom), " Rates (Mb/s): ");
	for (i = 0, j = 0; i < network->rates_len; ) {
		if (j < network->rates_ex_len &&
		    ((network->rates_ex[j] & 0x7F) <
		     (network->rates[i] & 0x7F)))
			rate = network->rates_ex[j++] & 0x7F;
		else
			rate = network->rates[i++] & 0x7F;
		if (rate > max_rate)
			max_rate = rate;
		p += snprintf(p, MAX_CUSTOM_LEN - (p - custom),
			      "%d%s ", rate >> 1, (rate & 1) ? ".5" : "");
	}
	for (; j < network->rates_ex_len; j++) {
		rate = network->rates_ex[j] & 0x7F;
		p += snprintf(p, MAX_CUSTOM_LEN - (p - custom),
			      "%d%s ", rate >> 1, (rate & 1) ? ".5" : "");
		if (rate > max_rate)
			max_rate = rate;
	}

	if (network->mode >= IEEE_N_24G)//add N rate here;
	{
		PHT_CAPABILITY_ELE ht_cap = NULL;
		bool is40M = false, isShortGI = false;
		u8 max_mcs = 0;
		if (!memcmp(network->bssht.bdHTCapBuf, EWC11NHTCap, 4))
			ht_cap = (PHT_CAPABILITY_ELE)&network->bssht.bdHTCapBuf[4];
		else
			ht_cap = (PHT_CAPABILITY_ELE)&network->bssht.bdHTCapBuf[0];
		is40M = (ht_cap->ChlWidth)?1:0;
		isShortGI = (ht_cap->ChlWidth)?
						((ht_cap->ShortGI40Mhz)?1:0):
						((ht_cap->ShortGI20Mhz)?1:0);

		max_mcs = HTGetHighestMCSRate(ieee, ht_cap->MCS, MCS_FILTER_ALL);
		rate = MCS_DATA_RATE[is40M][isShortGI][max_mcs&0x7f];
		if (rate > max_rate)
			max_rate = rate;
	}
	iwe.cmd = SIOCGIWRATE;
	iwe.u.bitrate.fixed = iwe.u.bitrate.disabled = 0;
	iwe.u.bitrate.value = max_rate * 500000;
	start = iwe_stream_add_event_rsl(info, start, stop, &iwe,
				     IW_EV_PARAM_LEN);
	iwe.cmd = IWEVCUSTOM;
	iwe.u.data.length = p - custom;
	if (iwe.u.data.length)
		start = iwe_stream_add_point(info, start, stop, &iwe, custom);
	/* Add quality statistics */
	/* TODO: Fix these values... */
	iwe.cmd = IWEVQUAL;
	iwe.u.qual.qual = network->stats.signal;
	iwe.u.qual.level = network->stats.rssi;
	iwe.u.qual.noise = network->stats.noise;
	iwe.u.qual.updated = network->stats.mask & IEEE80211_STATMASK_WEMASK;
	if (!(network->stats.mask & IEEE80211_STATMASK_RSSI))
		iwe.u.qual.updated |= IW_QUAL_LEVEL_INVALID;
	if (!(network->stats.mask & IEEE80211_STATMASK_NOISE))
		iwe.u.qual.updated |= IW_QUAL_NOISE_INVALID;
	if (!(network->stats.mask & IEEE80211_STATMASK_SIGNAL))
		iwe.u.qual.updated |= IW_QUAL_QUAL_INVALID;
	iwe.u.qual.updated = 7;
	start = iwe_stream_add_event_rsl(info, start, stop, &iwe, IW_EV_QUAL_LEN);
	iwe.cmd = IWEVCUSTOM;
	p = custom;

	iwe.u.data.length = p - custom;
	if (iwe.u.data.length)
	    start = iwe_stream_add_point(info, start, stop, &iwe, custom);

	if (ieee->wpa_enabled && network->wpa_ie_len){
		char buf[MAX_WPA_IE_LEN * 2 + 30];
	//	printk("WPA IE\n");
		u8 *p = buf;
		p += sprintf(p, "wpa_ie=");
		for (i = 0; i < network->wpa_ie_len; i++) {
			p += sprintf(p, "%02x", network->wpa_ie[i]);
		}

		memset(&iwe, 0, sizeof(iwe));
		iwe.cmd = IWEVCUSTOM;
		iwe.u.data.length = strlen(buf);
		start = iwe_stream_add_point(info, start, stop, &iwe, buf);
	}

	if (ieee->wpa_enabled && network->rsn_ie_len){
		char buf[MAX_WPA_IE_LEN * 2 + 30];

		u8 *p = buf;
		p += sprintf(p, "rsn_ie=");
		for (i = 0; i < network->rsn_ie_len; i++) {
			p += sprintf(p, "%02x", network->rsn_ie[i]);
		}

		memset(&iwe, 0, sizeof(iwe));
		iwe.cmd = IWEVCUSTOM;
		iwe.u.data.length = strlen(buf);
		start = iwe_stream_add_point(info, start, stop, &iwe, buf);
	}


	/* Add EXTRA: Age to display seconds since last beacon/probe response
	 * for given network. */
	iwe.cmd = IWEVCUSTOM;
	p = custom;
	p += snprintf(p, MAX_CUSTOM_LEN - (p - custom),
		      " Last beacon: %lums ago", (jiffies - network->last_scanned) / (HZ / 100));
	iwe.u.data.length = p - custom;
	if (iwe.u.data.length)
	    start = iwe_stream_add_point(info, start, stop, &iwe, custom);

	return start;
}

int ieee80211_wx_get_scan(struct ieee80211_device *ieee,
			  struct iw_request_info *info,
			  union iwreq_data *wrqu, char *extra)
{
	struct ieee80211_network *network;
	unsigned long flags;

	char *ev = extra;
//	char *stop = ev + IW_SCAN_MAX_DATA;
	char *stop = ev + wrqu->data.length;//IW_SCAN_MAX_DATA;
	//char *stop = ev + IW_SCAN_MAX_DATA;
	int i = 0;
	int err = 0;
	IEEE80211_DEBUG_WX("Getting scan\n");
	down(&ieee->wx_sem);
	spin_lock_irqsave(&ieee->lock, flags);

	list_for_each_entry(network, &ieee->network_list, list) {
		i++;
		if((stop-ev)<200)
		{
			err = -E2BIG;
			break;
												}
		if (ieee->scan_age == 0 ||
		    time_after(network->last_scanned + ieee->scan_age, jiffies))
			ev = rtl819x_translate_scan(ieee, ev, stop, network, info);
		else
			IEEE80211_DEBUG_SCAN(
				"Not showing network '%s ("
				"%pM)' due to age (%lums).\n",
				escape_essid(network->ssid,
					     network->ssid_len),
				network->bssid,
				(jiffies - network->last_scanned) / (HZ / 100));
	}

	spin_unlock_irqrestore(&ieee->lock, flags);
	up(&ieee->wx_sem);
	wrqu->data.length = ev -  extra;
	wrqu->data.flags = 0;

	IEEE80211_DEBUG_WX("exit: %d networks returned.\n", i);

	return err;
}
EXPORT_SYMBOL(ieee80211_wx_get_scan);

int ieee80211_wx_set_encode(struct ieee80211_device *ieee,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu, char *keybuf)
{
	struct iw_point *erq = &(wrqu->encoding);
	struct net_device *dev = ieee->dev;
	struct ieee80211_security sec = {
		.flags = 0
	};
	int i, key, key_provided, len;
	struct ieee80211_crypt_data **crypt;

	IEEE80211_DEBUG_WX("SET_ENCODE\n");

	key = erq->flags & IW_ENCODE_INDEX;
	if (key) {
		if (key > WEP_KEYS)
			return -EINVAL;
		key--;
		key_provided = 1;
	} else {
		key_provided = 0;
		key = ieee->tx_keyidx;
	}

	IEEE80211_DEBUG_WX("Key: %d [%s]\n", key, key_provided ?
			   "provided" : "default");
	crypt = &ieee->crypt[key];

	if (erq->flags & IW_ENCODE_DISABLED) {
		if (key_provided && *crypt) {
			IEEE80211_DEBUG_WX("Disabling encryption on key %d.\n",
					   key);
			ieee80211_crypt_delayed_deinit(ieee, crypt);
		} else
			IEEE80211_DEBUG_WX("Disabling encryption.\n");

		/* Check all the keys to see if any are still configured,
		 * and if no key index was provided, de-init them all */
		for (i = 0; i < WEP_KEYS; i++) {
			if (ieee->crypt[i] != NULL) {
				if (key_provided)
					break;
				ieee80211_crypt_delayed_deinit(
					ieee, &ieee->crypt[i]);
			}
		}

		if (i == WEP_KEYS) {
			sec.enabled = 0;
			sec.level = SEC_LEVEL_0;
			sec.flags |= SEC_ENABLED | SEC_LEVEL;
		}

		goto done;
	}



	sec.enabled = 1;
	sec.flags |= SEC_ENABLED;

	if (*crypt != NULL && (*crypt)->ops != NULL &&
	    strcmp((*crypt)->ops->name, "WEP") != 0) {
		/* changing to use WEP; deinit previously used algorithm
		 * on this key */
		ieee80211_crypt_delayed_deinit(ieee, crypt);
	}

	if (*crypt == NULL) {
		struct ieee80211_crypt_data *new_crypt;

		/* take WEP into use */
		new_crypt = kzalloc(sizeof(struct ieee80211_crypt_data),
				    GFP_KERNEL);
		if (new_crypt == NULL)
			return -ENOMEM;
		new_crypt->ops = ieee80211_get_crypto_ops("WEP");
		if (!new_crypt->ops) {
			request_module("ieee80211_crypt_wep");
			new_crypt->ops = ieee80211_get_crypto_ops("WEP");
		}
		if (new_crypt->ops && try_module_get(new_crypt->ops->owner))
			new_crypt->priv = new_crypt->ops->init(key);

		if (!new_crypt->ops || !new_crypt->priv) {
			kfree(new_crypt);
			new_crypt = NULL;

			printk(KERN_WARNING "%s: could not initialize WEP: "
			       "load module ieee80211_crypt_wep\n",
			       dev->name);
			return -EOPNOTSUPP;
		}
		*crypt = new_crypt;
	}

	/* If a new key was provided, set it up */
	if (erq->length > 0) {
		len = erq->length <= 5 ? 5 : 13;
		memcpy(sec.keys[key], keybuf, erq->length);
		if (len > erq->length)
			memset(sec.keys[key] + erq->length, 0,
			       len - erq->length);
		IEEE80211_DEBUG_WX("Setting key %d to '%s' (%d:%d bytes)\n",
				   key, escape_essid(sec.keys[key], len),
				   erq->length, len);
		sec.key_sizes[key] = len;
		(*crypt)->ops->set_key(sec.keys[key], len, NULL,
				       (*crypt)->priv);
		sec.flags |= (1 << key);
		/* This ensures a key will be activated if no key is
		 * explicitely set */
		if (key == sec.active_key)
			sec.flags |= SEC_ACTIVE_KEY;
		ieee->tx_keyidx = key;

	} else {
		len = (*crypt)->ops->get_key(sec.keys[key], WEP_KEY_LEN,
					     NULL, (*crypt)->priv);
		if (len == 0) {
			/* Set a default key of all 0 */
			printk("Setting key %d to all zero.\n",
					   key);

			IEEE80211_DEBUG_WX("Setting key %d to all zero.\n",
					   key);
			memset(sec.keys[key], 0, 13);
			(*crypt)->ops->set_key(sec.keys[key], 13, NULL,
					       (*crypt)->priv);
			sec.key_sizes[key] = 13;
			sec.flags |= (1 << key);
		}

		/* No key data - just set the default TX key index */
		if (key_provided) {
			IEEE80211_DEBUG_WX(
				"Setting key %d to default Tx key.\n", key);
			ieee->tx_keyidx = key;
			sec.active_key = key;
			sec.flags |= SEC_ACTIVE_KEY;
		}
	}

 done:
	ieee->open_wep = !(erq->flags & IW_ENCODE_RESTRICTED);
	ieee->auth_mode = ieee->open_wep ? WLAN_AUTH_OPEN : WLAN_AUTH_SHARED_KEY;
	sec.auth_mode = ieee->open_wep ? WLAN_AUTH_OPEN : WLAN_AUTH_SHARED_KEY;
	sec.flags |= SEC_AUTH_MODE;
	IEEE80211_DEBUG_WX("Auth: %s\n", sec.auth_mode == WLAN_AUTH_OPEN ?
			   "OPEN" : "SHARED KEY");

	/* For now we just support WEP, so only set that security level...
	 * TODO: When WPA is added this is one place that needs to change */
	sec.flags |= SEC_LEVEL;
	sec.level = SEC_LEVEL_1; /* 40 and 104 bit WEP */

	if (ieee->set_security)
		ieee->set_security(dev, &sec);

	/* Do not reset port if card is in Managed mode since resetting will
	 * generate new IEEE 802.11 authentication which may end up in looping
	 * with IEEE 802.1X.  If your hardware requires a reset after WEP
	 * configuration (for example... Prism2), implement the reset_port in
	 * the callbacks structures used to initialize the 802.11 stack. */
	if (ieee->reset_on_keychange &&
	    ieee->iw_mode != IW_MODE_INFRA &&
	    ieee->reset_port && ieee->reset_port(dev)) {
		printk(KERN_DEBUG "%s: reset_port failed\n", dev->name);
		return -EINVAL;
	}
	return 0;
}
EXPORT_SYMBOL(ieee80211_wx_set_encode);

int ieee80211_wx_get_encode(struct ieee80211_device *ieee,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu, char *keybuf)
{
	struct iw_point *erq = &(wrqu->encoding);
	int len, key;
	struct ieee80211_crypt_data *crypt;

	IEEE80211_DEBUG_WX("GET_ENCODE\n");

	if(ieee->iw_mode == IW_MODE_MONITOR)
		return -1;

	key = erq->flags & IW_ENCODE_INDEX;
	if (key) {
		if (key > WEP_KEYS)
			return -EINVAL;
		key--;
	} else
		key = ieee->tx_keyidx;

	crypt = ieee->crypt[key];
	erq->flags = key + 1;

	if (crypt == NULL || crypt->ops == NULL) {
		erq->length = 0;
		erq->flags |= IW_ENCODE_DISABLED;
		return 0;
	}
	len = crypt->ops->get_key(keybuf, SCM_KEY_LEN, NULL, crypt->priv);
	erq->length = (len >= 0 ? len : 0);

	erq->flags |= IW_ENCODE_ENABLED;

	if (ieee->open_wep)
		erq->flags |= IW_ENCODE_OPEN;
	else
		erq->flags |= IW_ENCODE_RESTRICTED;

	return 0;
}
EXPORT_SYMBOL(ieee80211_wx_get_encode);

int ieee80211_wx_set_encode_ext(struct ieee80211_device *ieee,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)
{
	int ret = 0;
	struct net_device *dev = ieee->dev;
	struct iw_point *encoding = &wrqu->encoding;
	struct iw_encode_ext *ext = (struct iw_encode_ext *)extra;
	int i, idx;
	int group_key = 0;
	const char *alg, *module;
	struct ieee80211_crypto_ops *ops;
	struct ieee80211_crypt_data **crypt;

	struct ieee80211_security sec = {
		.flags = 0,
	};
	//printk("======>encoding flag:%x,ext flag:%x, ext alg:%d\n", encoding->flags,ext->ext_flags, ext->alg);
	idx = encoding->flags & IW_ENCODE_INDEX;
	if (idx) {
		if (idx < 1 || idx > WEP_KEYS)
			return -EINVAL;
		idx--;
	} else
		idx = ieee->tx_keyidx;

	if (ext->ext_flags & IW_ENCODE_EXT_GROUP_KEY) {

		crypt = &ieee->crypt[idx];

		group_key = 1;
	} else {
		/* some Cisco APs use idx>0 for unicast in dynamic WEP */
		//printk("not group key, flags:%x, ext->alg:%d\n", ext->ext_flags, ext->alg);
		if (idx != 0 && ext->alg != IW_ENCODE_ALG_WEP)
			return -EINVAL;
		if (ieee->iw_mode == IW_MODE_INFRA)

			crypt = &ieee->crypt[idx];

		else
			return -EINVAL;
	}

	sec.flags |= SEC_ENABLED;// | SEC_ENCRYPT;
	if ((encoding->flags & IW_ENCODE_DISABLED) ||
	    ext->alg == IW_ENCODE_ALG_NONE) {
		if (*crypt)
			ieee80211_crypt_delayed_deinit(ieee, crypt);

		for (i = 0; i < WEP_KEYS; i++)

			if (ieee->crypt[i] != NULL)

				break;

		if (i == WEP_KEYS) {
			sec.enabled = 0;
		      //  sec.encrypt = 0;
			sec.level = SEC_LEVEL_0;
			sec.flags |= SEC_LEVEL;
		}
		//printk("disabled: flag:%x\n", encoding->flags);
		goto done;
	}

	sec.enabled = 1;
    //    sec.encrypt = 1;
	switch (ext->alg) {
	case IW_ENCODE_ALG_WEP:
		alg = "WEP";
		module = "ieee80211_crypt_wep";
		break;
	case IW_ENCODE_ALG_TKIP:
		alg = "TKIP";
		module = "ieee80211_crypt_tkip";
		break;
	case IW_ENCODE_ALG_CCMP:
		alg = "CCMP";
		module = "ieee80211_crypt_ccmp";
		break;
	default:
		IEEE80211_DEBUG_WX("%s: unknown crypto alg %d\n",
				   dev->name, ext->alg);
		ret = -EINVAL;
		goto done;
	}
	printk("alg name:%s\n",alg);

	 ops = ieee80211_get_crypto_ops(alg);
	if (ops == NULL) {
		request_module(module);
		ops = ieee80211_get_crypto_ops(alg);
	}
	if (ops == NULL) {
		IEEE80211_DEBUG_WX("%s: unknown crypto alg %d\n",
				   dev->name, ext->alg);
		printk("========>unknown crypto alg %d\n", ext->alg);
		ret = -EINVAL;
		goto done;
	}

	if (*crypt == NULL || (*crypt)->ops != ops) {
		struct ieee80211_crypt_data *new_crypt;

		ieee80211_crypt_delayed_deinit(ieee, crypt);

		new_crypt = kzalloc(sizeof(*new_crypt), GFP_KERNEL);
		if (new_crypt == NULL) {
			ret = -ENOMEM;
			goto done;
		}
		new_crypt->ops = ops;
		if (new_crypt->ops && try_module_get(new_crypt->ops->owner))
			new_crypt->priv = new_crypt->ops->init(idx);
		if (new_crypt->priv == NULL) {
			kfree(new_crypt);
			ret = -EINVAL;
			goto done;
		}
		*crypt = new_crypt;

	}

	if (ext->key_len > 0 && (*crypt)->ops->set_key &&
	    (*crypt)->ops->set_key(ext->key, ext->key_len, ext->rx_seq,
				   (*crypt)->priv) < 0) {
		IEEE80211_DEBUG_WX("%s: key setting failed\n", dev->name);
		printk("key setting failed\n");
		ret = -EINVAL;
		goto done;
	}
 //skip_host_crypt:
	//printk("skip_host_crypt:ext_flags:%x\n", ext->ext_flags);
	if (ext->ext_flags & IW_ENCODE_EXT_SET_TX_KEY) {
		ieee->tx_keyidx = idx;
		sec.active_key = idx;
		sec.flags |= SEC_ACTIVE_KEY;
	}

	if (ext->alg != IW_ENCODE_ALG_NONE) {
		//memcpy(sec.keys[idx], ext->key, ext->key_len);
		sec.key_sizes[idx] = ext->key_len;
		sec.flags |= (1 << idx);
		if (ext->alg == IW_ENCODE_ALG_WEP) {
		      //  sec.encode_alg[idx] = SEC_ALG_WEP;
			sec.flags |= SEC_LEVEL;
			sec.level = SEC_LEVEL_1;
		} else if (ext->alg == IW_ENCODE_ALG_TKIP) {
		      //  sec.encode_alg[idx] = SEC_ALG_TKIP;
			sec.flags |= SEC_LEVEL;
			sec.level = SEC_LEVEL_2;
		} else if (ext->alg == IW_ENCODE_ALG_CCMP) {
		       // sec.encode_alg[idx] = SEC_ALG_CCMP;
			sec.flags |= SEC_LEVEL;
			sec.level = SEC_LEVEL_3;
		}
		/* Don't set sec level for group keys. */
		if (group_key)
			sec.flags &= ~SEC_LEVEL;
	}
done:
	if (ieee->set_security)
		ieee->set_security(ieee->dev, &sec);

	 if (ieee->reset_on_keychange &&
	    ieee->iw_mode != IW_MODE_INFRA &&
	    ieee->reset_port && ieee->reset_port(dev)) {
		IEEE80211_DEBUG_WX("%s: reset_port failed\n", dev->name);
		return -EINVAL;
	}
	return ret;
}
EXPORT_SYMBOL(ieee80211_wx_set_encode_ext);

int ieee80211_wx_get_encode_ext(struct ieee80211_device *ieee,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)
{
	struct iw_point *encoding = &wrqu->encoding;
	struct iw_encode_ext *ext = (struct iw_encode_ext *)extra;
	struct ieee80211_crypt_data *crypt;
	int idx, max_key_len;

	max_key_len = encoding->length - sizeof(*ext);
	if (max_key_len < 0)
		return -EINVAL;

	idx = encoding->flags & IW_ENCODE_INDEX;
	if (idx) {
		if (idx < 1 || idx > WEP_KEYS)
			return -EINVAL;
		idx--;
	} else
		idx = ieee->tx_keyidx;

	if (!(ext->ext_flags & IW_ENCODE_EXT_GROUP_KEY) &&
	    ext->alg != IW_ENCODE_ALG_WEP)
		if (idx != 0 || ieee->iw_mode != IW_MODE_INFRA)
			return -EINVAL;

	crypt = ieee->crypt[idx];
	encoding->flags = idx + 1;
	memset(ext, 0, sizeof(*ext));

	if (crypt == NULL || crypt->ops == NULL ) {
		ext->alg = IW_ENCODE_ALG_NONE;
		ext->key_len = 0;
		encoding->flags |= IW_ENCODE_DISABLED;
	} else {
		if (strcmp(crypt->ops->name, "WEP") == 0 )
			ext->alg = IW_ENCODE_ALG_WEP;
		else if (strcmp(crypt->ops->name, "TKIP"))
			ext->alg = IW_ENCODE_ALG_TKIP;
		else if (strcmp(crypt->ops->name, "CCMP"))
			ext->alg = IW_ENCODE_ALG_CCMP;
		else
			return -EINVAL;
		ext->key_len = crypt->ops->get_key(ext->key, SCM_KEY_LEN, NULL, crypt->priv);
		encoding->flags |= IW_ENCODE_ENABLED;
		if (ext->key_len &&
		    (ext->alg == IW_ENCODE_ALG_TKIP ||
		     ext->alg == IW_ENCODE_ALG_CCMP))
			ext->ext_flags |= IW_ENCODE_EXT_TX_SEQ_VALID;

	}

	return 0;
}
EXPORT_SYMBOL(ieee80211_wx_get_encode_ext);

int ieee80211_wx_set_mlme(struct ieee80211_device *ieee,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)
{
	struct iw_mlme *mlme = (struct iw_mlme *) extra;
	switch (mlme->cmd) {
	case IW_MLME_DEAUTH:
	case IW_MLME_DISASSOC:
		ieee80211_disassociate(ieee);
		break;
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}
EXPORT_SYMBOL(ieee80211_wx_set_mlme);

int ieee80211_wx_set_auth(struct ieee80211_device *ieee,
			       struct iw_request_info *info,
			       struct iw_param *data, char *extra)
{
	switch (data->flags & IW_AUTH_INDEX) {
	case IW_AUTH_WPA_VERSION:
	     /*need to support wpa2 here*/
		//printk("wpa version:%x\n", data->value);
		break;
	case IW_AUTH_CIPHER_PAIRWISE:
	case IW_AUTH_CIPHER_GROUP:
	case IW_AUTH_KEY_MGMT:
		/*
 *                  * Host AP driver does not use these parameters and allows
 *                                   * wpa_supplicant to control them internally.
 *                                                    */
		break;
	case IW_AUTH_TKIP_COUNTERMEASURES:
		ieee->tkip_countermeasures = data->value;
		break;
	case IW_AUTH_DROP_UNENCRYPTED:
		ieee->drop_unencrypted = data->value;
		break;

	case IW_AUTH_80211_AUTH_ALG:
		//printk("======>%s():data->value is %d\n",__func__,data->value);
	//	ieee->open_wep = (data->value&IW_AUTH_ALG_OPEN_SYSTEM)?1:0;
		if(data->value & IW_AUTH_ALG_SHARED_KEY){
			ieee->open_wep = 0;
			ieee->auth_mode = 1;
		}
		else if(data->value & IW_AUTH_ALG_OPEN_SYSTEM){
			ieee->open_wep = 1;
			ieee->auth_mode = 0;
		}
		else if(data->value & IW_AUTH_ALG_LEAP){
			ieee->open_wep = 1;
			ieee->auth_mode = 2;
			//printk("hahahaa:LEAP\n");
		}
		else
			return -EINVAL;
		//printk("open_wep:%d\n", ieee->open_wep);
		break;

	case IW_AUTH_WPA_ENABLED:
		ieee->wpa_enabled = (data->value)?1:0;
		//printk("enalbe wpa:%d\n", ieee->wpa_enabled);
		break;

	case IW_AUTH_RX_UNENCRYPTED_EAPOL:
		ieee->ieee802_1x = data->value;
		break;
	case IW_AUTH_PRIVACY_INVOKED:
		ieee->privacy_invoked = data->value;
		break;
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}
EXPORT_SYMBOL(ieee80211_wx_set_auth);

int ieee80211_wx_set_gen_ie(struct ieee80211_device *ieee, u8 *ie, size_t len)
{
	u8 *buf;

	if (len>MAX_WPA_IE_LEN || (len && ie == NULL))
	{
	//	printk("return error out, len:%d\n", len);
	return -EINVAL;
	}


	if (len)
	{
		if (len != ie[1]+2)
		{
			printk("len:%zu, ie:%d\n", len, ie[1]);
			return -EINVAL;
		}
		buf = kmemdup(ie, len, GFP_KERNEL);
		if (buf == NULL)
			return -ENOMEM;
		kfree(ieee->wpa_ie);
		ieee->wpa_ie = buf;
		ieee->wpa_ie_len = len;
	}
	else{
		kfree(ieee->wpa_ie);
		ieee->wpa_ie = NULL;
		ieee->wpa_ie_len = 0;
	}
	return 0;

}
EXPORT_SYMBOL(ieee80211_wx_set_gen_ie);
