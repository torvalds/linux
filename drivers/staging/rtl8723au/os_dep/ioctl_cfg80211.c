/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 ******************************************************************************/
#define  _IOCTL_CFG80211_C_

#include <osdep_service.h>
#include <drv_types.h>
#include <xmit_osdep.h>

#include "ioctl_cfg80211.h"

#define RTW_MAX_MGMT_TX_CNT 8

#define RTW_MAX_REMAIN_ON_CHANNEL_DURATION 65535	/* ms */
#define RTW_MAX_NUM_PMKIDS 4

static const u32 rtw_cipher_suites[] = {
	WLAN_CIPHER_SUITE_WEP40,
	WLAN_CIPHER_SUITE_WEP104,
	WLAN_CIPHER_SUITE_TKIP,
	WLAN_CIPHER_SUITE_CCMP,
};

#define RATETAB_ENT(_rate, _rateid, _flags) {			\
	.bitrate	= (_rate),				\
	.hw_value	= (_rateid),				\
	.flags		= (_flags),				\
}

#define CHAN2G(_channel, _freq, _flags) {			\
	.band			= IEEE80211_BAND_2GHZ,		\
	.center_freq		= (_freq),			\
	.hw_value		= (_channel),			\
	.flags			= (_flags),			\
	.max_antenna_gain	= 0,				\
	.max_power		= 30,				\
}

#define CHAN5G(_channel, _flags) {				\
	.band			= IEEE80211_BAND_5GHZ,		\
	.center_freq		= 5000 + (5 * (_channel)),	\
	.hw_value		= (_channel),			\
	.flags			= (_flags),			\
	.max_antenna_gain	= 0,				\
	.max_power		= 30,				\
}

static struct ieee80211_rate rtw_rates[] = {
	RATETAB_ENT(10, 0x1, 0),
	RATETAB_ENT(20, 0x2, 0),
	RATETAB_ENT(55, 0x4, 0),
	RATETAB_ENT(110, 0x8, 0),
	RATETAB_ENT(60, 0x10, 0),
	RATETAB_ENT(90, 0x20, 0),
	RATETAB_ENT(120, 0x40, 0),
	RATETAB_ENT(180, 0x80, 0),
	RATETAB_ENT(240, 0x100, 0),
	RATETAB_ENT(360, 0x200, 0),
	RATETAB_ENT(480, 0x400, 0),
	RATETAB_ENT(540, 0x800, 0),
};

#define rtw_a_rates		(rtw_rates + 4)
#define RTW_A_RATES_NUM	8
#define rtw_g_rates		(rtw_rates + 0)
#define RTW_G_RATES_NUM	12

#define RTW_2G_CHANNELS_NUM 14
#define RTW_5G_CHANNELS_NUM 37

static struct ieee80211_channel rtw_2ghz_channels[] = {
	CHAN2G(1, 2412, 0),
	CHAN2G(2, 2417, 0),
	CHAN2G(3, 2422, 0),
	CHAN2G(4, 2427, 0),
	CHAN2G(5, 2432, 0),
	CHAN2G(6, 2437, 0),
	CHAN2G(7, 2442, 0),
	CHAN2G(8, 2447, 0),
	CHAN2G(9, 2452, 0),
	CHAN2G(10, 2457, 0),
	CHAN2G(11, 2462, 0),
	CHAN2G(12, 2467, 0),
	CHAN2G(13, 2472, 0),
	CHAN2G(14, 2484, 0),
};

static struct ieee80211_channel rtw_5ghz_a_channels[] = {
	CHAN5G(34, 0), CHAN5G(36, 0),
	CHAN5G(38, 0), CHAN5G(40, 0),
	CHAN5G(42, 0), CHAN5G(44, 0),
	CHAN5G(46, 0), CHAN5G(48, 0),
	CHAN5G(52, 0), CHAN5G(56, 0),
	CHAN5G(60, 0), CHAN5G(64, 0),
	CHAN5G(100, 0), CHAN5G(104, 0),
	CHAN5G(108, 0), CHAN5G(112, 0),
	CHAN5G(116, 0), CHAN5G(120, 0),
	CHAN5G(124, 0), CHAN5G(128, 0),
	CHAN5G(132, 0), CHAN5G(136, 0),
	CHAN5G(140, 0), CHAN5G(149, 0),
	CHAN5G(153, 0), CHAN5G(157, 0),
	CHAN5G(161, 0), CHAN5G(165, 0),
	CHAN5G(184, 0), CHAN5G(188, 0),
	CHAN5G(192, 0), CHAN5G(196, 0),
	CHAN5G(200, 0), CHAN5G(204, 0),
	CHAN5G(208, 0), CHAN5G(212, 0),
	CHAN5G(216, 0),
};

static void rtw_2g_channels_init(struct ieee80211_channel *channels)
{
	memcpy((void *)channels, (void *)rtw_2ghz_channels,
	       sizeof(struct ieee80211_channel) * RTW_2G_CHANNELS_NUM);
}

static void rtw_5g_channels_init(struct ieee80211_channel *channels)
{
	memcpy((void *)channels, (void *)rtw_5ghz_a_channels,
	       sizeof(struct ieee80211_channel) * RTW_5G_CHANNELS_NUM);
}

static void rtw_2g_rates_init(struct ieee80211_rate *rates)
{
	memcpy(rates, rtw_g_rates,
	       sizeof(struct ieee80211_rate) * RTW_G_RATES_NUM);
}

static void rtw_5g_rates_init(struct ieee80211_rate *rates)
{
	memcpy(rates, rtw_a_rates,
	       sizeof(struct ieee80211_rate) * RTW_A_RATES_NUM);
}

static struct ieee80211_supported_band *
rtw_spt_band_alloc(enum ieee80211_band band)
{
	struct ieee80211_supported_band *spt_band = NULL;
	int n_channels, n_bitrates;

	if (band == IEEE80211_BAND_2GHZ) {
		n_channels = RTW_2G_CHANNELS_NUM;
		n_bitrates = RTW_G_RATES_NUM;
	} else if (band == IEEE80211_BAND_5GHZ) {
		n_channels = RTW_5G_CHANNELS_NUM;
		n_bitrates = RTW_A_RATES_NUM;
	} else {
		goto exit;
	}
	spt_band = kzalloc(sizeof(struct ieee80211_supported_band) +
			   sizeof(struct ieee80211_channel) * n_channels +
			   sizeof(struct ieee80211_rate) * n_bitrates,
			   GFP_KERNEL);
	if (!spt_band)
		goto exit;

	spt_band->channels =
		(struct ieee80211_channel *)(((u8 *) spt_band) +
					     sizeof(struct
						    ieee80211_supported_band));
	spt_band->bitrates =
		(struct ieee80211_rate *)(((u8 *) spt_band->channels) +
					  sizeof(struct ieee80211_channel) *
					  n_channels);
	spt_band->band = band;
	spt_band->n_channels = n_channels;
	spt_band->n_bitrates = n_bitrates;

	if (band == IEEE80211_BAND_2GHZ) {
		rtw_2g_channels_init(spt_band->channels);
		rtw_2g_rates_init(spt_band->bitrates);
	} else if (band == IEEE80211_BAND_5GHZ) {
		rtw_5g_channels_init(spt_band->channels);
		rtw_5g_rates_init(spt_band->bitrates);
	}

	/* spt_band.ht_cap */

exit:
	return spt_band;
}

static const struct ieee80211_txrx_stypes
rtw_cfg80211_default_mgmt_stypes[NUM_NL80211_IFTYPES] = {
	[NL80211_IFTYPE_ADHOC] = {
		.tx = 0xffff,
		.rx = BIT(IEEE80211_STYPE_ACTION >> 4)
	},
	[NL80211_IFTYPE_STATION] = {
		.tx = 0xffff,
		.rx = BIT(IEEE80211_STYPE_ACTION >> 4) |
		      BIT(IEEE80211_STYPE_PROBE_REQ >> 4)
	},
	[NL80211_IFTYPE_AP] = {
		.tx = 0xffff,
		.rx = BIT(IEEE80211_STYPE_ASSOC_REQ >> 4) |
		      BIT(IEEE80211_STYPE_REASSOC_REQ >> 4) |
		      BIT(IEEE80211_STYPE_PROBE_REQ >> 4) |
		      BIT(IEEE80211_STYPE_DISASSOC >> 4) |
		      BIT(IEEE80211_STYPE_AUTH >> 4) |
		      BIT(IEEE80211_STYPE_DEAUTH >> 4) |
		      BIT(IEEE80211_STYPE_ACTION >> 4)
	},
	[NL80211_IFTYPE_AP_VLAN] = {
		/* copy AP */
		.tx = 0xffff,
		.rx = BIT(IEEE80211_STYPE_ASSOC_REQ >> 4) |
		      BIT(IEEE80211_STYPE_REASSOC_REQ >> 4) |
		      BIT(IEEE80211_STYPE_PROBE_REQ >> 4) |
		      BIT(IEEE80211_STYPE_DISASSOC >> 4) |
		      BIT(IEEE80211_STYPE_AUTH >> 4) |
		      BIT(IEEE80211_STYPE_DEAUTH >> 4) |
		      BIT(IEEE80211_STYPE_ACTION >> 4)
	},
	[NL80211_IFTYPE_P2P_CLIENT] = {
		.tx = 0xffff,
		.rx = BIT(IEEE80211_STYPE_ACTION >> 4) |
		      BIT(IEEE80211_STYPE_PROBE_REQ >> 4)
	},
	[NL80211_IFTYPE_P2P_GO] = {
		.tx = 0xffff,
		.rx = BIT(IEEE80211_STYPE_ASSOC_REQ >> 4) |
		      BIT(IEEE80211_STYPE_REASSOC_REQ >> 4) |
		      BIT(IEEE80211_STYPE_PROBE_REQ >> 4) |
		      BIT(IEEE80211_STYPE_DISASSOC >> 4) |
		      BIT(IEEE80211_STYPE_AUTH >> 4) |
		      BIT(IEEE80211_STYPE_DEAUTH >> 4) |
		      BIT(IEEE80211_STYPE_ACTION >> 4)
	},
};

static int rtw_cfg80211_inform_bss(struct rtw_adapter *padapter,
				   struct wlan_network *pnetwork)
{
	int ret = 0;
	struct ieee80211_channel *notify_channel;
	struct cfg80211_bss *bss;
	u16 channel;
	u32 freq;
	u8 *notify_ie;
	size_t notify_ielen;
	s32 notify_signal;
	struct wireless_dev *wdev = padapter->rtw_wdev;
	struct wiphy *wiphy = wdev->wiphy;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

	channel = pnetwork->network.DSConfig;
	if (channel <= RTW_CH_MAX_2G_CHANNEL)
		freq = ieee80211_channel_to_frequency(channel,
						      IEEE80211_BAND_2GHZ);
	else
		freq = ieee80211_channel_to_frequency(channel,
						      IEEE80211_BAND_5GHZ);

	notify_channel = ieee80211_get_channel(wiphy, freq);

	notify_ie = pnetwork->network.IEs;
	notify_ielen = pnetwork->network.IELength;

	/* We've set wiphy's signal_type as CFG80211_SIGNAL_TYPE_MBM:
	 *  signal strength in mBm (100*dBm)
	 */
	if (check_fwstate(pmlmepriv, _FW_LINKED) &&
	    is_same_network23a(&pmlmepriv->cur_network.network,
			    &pnetwork->network)) {
		notify_signal = 100 * translate_percentage_to_dbm(padapter->recvpriv.signal_strength);	/* dbm */
	} else {
		notify_signal = 100 * translate_percentage_to_dbm(pnetwork->network.PhyInfo.SignalStrength);	/* dbm */
	}

	bss = cfg80211_inform_bss(wiphy, notify_channel,
				  CFG80211_BSS_FTYPE_UNKNOWN,
				  pnetwork->network.MacAddress,
				  pnetwork->network.tsf,
				  pnetwork->network.capability,
				  pnetwork->network.beacon_interval,
				  notify_ie, notify_ielen,
				  notify_signal, GFP_ATOMIC);

	if (unlikely(!bss)) {
		DBG_8723A("rtw_cfg80211_inform_bss error\n");
		return -EINVAL;
	}

	cfg80211_put_bss(wiphy, bss);

	return ret;
}

void rtw_cfg80211_indicate_connect(struct rtw_adapter *padapter)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct wlan_network *cur_network = &pmlmepriv->cur_network;
	struct wireless_dev *pwdev = padapter->rtw_wdev;

	DBG_8723A("%s(padapter =%p)\n", __func__, padapter);

	if (pwdev->iftype != NL80211_IFTYPE_STATION &&
	    pwdev->iftype != NL80211_IFTYPE_P2P_CLIENT)
		return;

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE))
		return;

	if (padapter->mlmepriv.to_roaming > 0) {
		struct wiphy *wiphy = pwdev->wiphy;
		struct ieee80211_channel *notify_channel;
		u32 freq;
		u16 channel = cur_network->network.DSConfig;

		if (channel <= RTW_CH_MAX_2G_CHANNEL)
			freq =
			    ieee80211_channel_to_frequency(channel,
							   IEEE80211_BAND_2GHZ);
		else
			freq =
			    ieee80211_channel_to_frequency(channel,
							   IEEE80211_BAND_5GHZ);

		notify_channel = ieee80211_get_channel(wiphy, freq);

		DBG_8723A("%s call cfg80211_roamed\n", __func__);
		cfg80211_roamed(padapter->pnetdev, notify_channel,
				cur_network->network.MacAddress,
				pmlmepriv->assoc_req +
				sizeof(struct ieee80211_hdr_3addr) + 2,
				pmlmepriv->assoc_req_len -
				sizeof(struct ieee80211_hdr_3addr) - 2,
				pmlmepriv->assoc_rsp +
				sizeof(struct ieee80211_hdr_3addr) + 6,
				pmlmepriv->assoc_rsp_len -
				sizeof(struct ieee80211_hdr_3addr) - 6,
				GFP_ATOMIC);
	} else {
		cfg80211_connect_result(padapter->pnetdev,
					cur_network->network.MacAddress,
					pmlmepriv->assoc_req +
					sizeof(struct ieee80211_hdr_3addr) + 2,
					pmlmepriv->assoc_req_len -
					sizeof(struct ieee80211_hdr_3addr) - 2,
					pmlmepriv->assoc_rsp +
					sizeof(struct ieee80211_hdr_3addr) + 6,
					pmlmepriv->assoc_rsp_len -
					sizeof(struct ieee80211_hdr_3addr) - 6,
					WLAN_STATUS_SUCCESS, GFP_ATOMIC);
	}
}

void rtw_cfg80211_indicate_disconnect(struct rtw_adapter *padapter)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct wireless_dev *pwdev = padapter->rtw_wdev;

	DBG_8723A("%s(padapter =%p)\n", __func__, padapter);

	if (pwdev->iftype != NL80211_IFTYPE_STATION &&
	    pwdev->iftype != NL80211_IFTYPE_P2P_CLIENT)
		return;

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE))
		return;

	if (!padapter->mlmepriv.not_indic_disco) {
		if (check_fwstate(&padapter->mlmepriv, WIFI_UNDER_LINKING)) {
			cfg80211_connect_result(padapter->pnetdev, NULL, NULL,
						0, NULL, 0,
						WLAN_STATUS_UNSPECIFIED_FAILURE,
						GFP_ATOMIC);
		} else {
			cfg80211_disconnected(padapter->pnetdev, 0, NULL,
					      0, GFP_ATOMIC);
		}
	}
}

#ifdef CONFIG_8723AU_AP_MODE
static int set_pairwise_key(struct rtw_adapter *padapter, struct sta_info *psta)
{
	struct cmd_obj *ph2c;
	struct set_stakey_parm *psetstakey_para;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;
	int res = _SUCCESS;

	ph2c = kzalloc(sizeof(struct cmd_obj), GFP_KERNEL);
	if (ph2c == NULL) {
		res = _FAIL;
		goto exit;
	}

	psetstakey_para = kzalloc(sizeof(struct set_stakey_parm), GFP_KERNEL);
	if (psetstakey_para == NULL) {
		kfree(ph2c);
		res = _FAIL;
		goto exit;
	}

	init_h2fwcmd_w_parm_no_rsp(ph2c, psetstakey_para, _SetStaKey_CMD_);

	psetstakey_para->algorithm = psta->dot118021XPrivacy;

	ether_addr_copy(psetstakey_para->addr, psta->hwaddr);

	memcpy(psetstakey_para->key, &psta->dot118021x_UncstKey, 16);

	res = rtw_enqueue_cmd23a(pcmdpriv, ph2c);

exit:
	return res;
}

static int set_group_key(struct rtw_adapter *padapter, struct key_params *parms,
			 u32 alg, u8 keyid)
{
	struct cmd_obj *pcmd;
	struct setkey_parm *psetkeyparm;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;
	int res = _SUCCESS;

	DBG_8723A("%s\n", __func__);

	if (keyid >= 4) {
		res = _FAIL;
		goto exit;
	}

	pcmd = kzalloc(sizeof(struct cmd_obj), GFP_KERNEL);
	if (!pcmd) {
		res = _FAIL;
		goto exit;
	}
	psetkeyparm = kzalloc(sizeof(struct setkey_parm), GFP_KERNEL);
	if (!psetkeyparm) {
		kfree(pcmd);
		res = _FAIL;
		goto exit;
	}

	psetkeyparm->keyid = keyid;
	if (is_wep_enc(alg))
		padapter->mlmepriv.key_mask |= BIT(psetkeyparm->keyid);

	psetkeyparm->algorithm = alg;

	psetkeyparm->set_tx = 1;

	memcpy(&psetkeyparm->key, parms->key, parms->key_len);

	pcmd->cmdcode = _SetKey_CMD_;
	pcmd->parmbuf = (u8 *) psetkeyparm;
	pcmd->cmdsz = (sizeof(struct setkey_parm));
	pcmd->rsp = NULL;
	pcmd->rspsz = 0;

	res = rtw_enqueue_cmd23a(pcmdpriv, pcmd);

exit:
	return res;
}

static int rtw_cfg80211_ap_set_encryption(struct net_device *dev, u8 key_index,
					  int set_tx, const u8 *sta_addr,
					  struct key_params *keyparms)
{
	int ret = 0;
	int key_len;
	struct sta_info *psta = NULL, *pbcmc_sta = NULL;
	struct rtw_adapter *padapter = netdev_priv(dev);
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	struct sta_priv *pstapriv = &padapter->stapriv;

	DBG_8723A("%s\n", __func__);

	if (!is_broadcast_ether_addr(sta_addr)) {
		psta = rtw_get_stainfo23a(pstapriv, sta_addr);
		if (!psta) {
			/* ret = -EINVAL; */
			DBG_8723A("rtw_set_encryption(), sta has already "
				  "been removed or never been added\n");
			goto exit;
		}
	}

	key_len = keyparms->key_len;

	if (!psta && (keyparms->cipher == WLAN_CIPHER_SUITE_WEP40 ||
		      keyparms->cipher == WLAN_CIPHER_SUITE_WEP104)) {
		DBG_8723A("r871x_set_encryption, crypt.alg = WEP\n");

		DBG_8723A("r871x_set_encryption, wep_key_idx =%d, len =%d\n",
			  key_index, key_len);

		if (psecuritypriv->bWepDefaultKeyIdxSet == 0) {
			/* wep default key has not been set, so use
			   this key index as default key. */

			psecuritypriv->ndisencryptstatus =
				Ndis802_11Encryption1Enabled;
			psecuritypriv->dot11PrivacyAlgrthm = keyparms->cipher;
			psecuritypriv->dot118021XGrpPrivacy = keyparms->cipher;

			psecuritypriv->dot11PrivacyKeyIndex = key_index;
		}

		memcpy(&psecuritypriv->wep_key[key_index].key,
		       keyparms->key, key_len);

		psecuritypriv->wep_key[key_index].keylen = key_len;

		set_group_key(padapter, keyparms, keyparms->cipher, key_index);

		goto exit;
	}

	if (!psta) {	/*  group key */
		if (set_tx == 0) {	/* group key */
			if (keyparms->cipher == WLAN_CIPHER_SUITE_WEP40 ||
			    keyparms->cipher == WLAN_CIPHER_SUITE_WEP104) {
				DBG_8723A("%s, set group_key, WEP\n", __func__);

				memcpy(psecuritypriv->
				       dot118021XGrpKey[key_index].skey,
				       keyparms->key, key_len);

				psecuritypriv->dot118021XGrpPrivacy =
					keyparms->cipher;
			} else if (keyparms->cipher == WLAN_CIPHER_SUITE_TKIP) {
				DBG_8723A("%s, set group_key, TKIP\n",
					  __func__);

				psecuritypriv->dot118021XGrpPrivacy =
					WLAN_CIPHER_SUITE_TKIP;

				memcpy(psecuritypriv->
				       dot118021XGrpKey[key_index].skey,
				       keyparms->key,
				       (key_len > 16 ? 16 : key_len));

				/* set mic key */
				memcpy(psecuritypriv->
				       dot118021XGrptxmickey[key_index].skey,
				       &keyparms->key[16], 8);
				memcpy(psecuritypriv->
				       dot118021XGrprxmickey[key_index].skey,
				       &keyparms->key[24], 8);

				psecuritypriv->busetkipkey = 1;

			} else if (keyparms->cipher == WLAN_CIPHER_SUITE_CCMP) {
					DBG_8723A("%s, set group_key, CCMP\n",
					  __func__);

				psecuritypriv->dot118021XGrpPrivacy =
					WLAN_CIPHER_SUITE_CCMP;

				memcpy(psecuritypriv->
				       dot118021XGrpKey[key_index].skey,
				       keyparms->key,
				       (key_len > 16 ? 16 : key_len));
			} else {
				DBG_8723A("%s, set group_key, none\n",
					  __func__);

				psecuritypriv->dot118021XGrpPrivacy = 0;
			}

			psecuritypriv->dot118021XGrpKeyid = key_index;

			psecuritypriv->binstallGrpkey = 1;

			psecuritypriv->dot11PrivacyAlgrthm =
				psecuritypriv->dot118021XGrpPrivacy;

			set_group_key(padapter, keyparms,
				      psecuritypriv->dot118021XGrpPrivacy,
				      key_index);

			pbcmc_sta = rtw_get_bcmc_stainfo23a(padapter);
			if (pbcmc_sta) {
				pbcmc_sta->ieee8021x_blocked = false;
				/* rx will use bmc_sta's dot118021XPrivacy */
				pbcmc_sta->dot118021XPrivacy =
					psecuritypriv->dot118021XGrpPrivacy;

			}

		}

		goto exit;
	}

	if (psecuritypriv->dot11AuthAlgrthm == dot11AuthAlgrthm_8021X && psta) {
		/*  psk/802_1x */
		if (set_tx == 1) {
			/* pairwise key */
			memcpy(psta->dot118021x_UncstKey.skey,
			       keyparms->key, (key_len > 16 ? 16 : key_len));

			if (keyparms->cipher == WLAN_CIPHER_SUITE_WEP40 ||
			    keyparms->cipher == WLAN_CIPHER_SUITE_WEP104) {
				DBG_8723A("%s, set pairwise key, WEP\n",
					  __func__);

				psecuritypriv->dot118021XGrpPrivacy =
					keyparms->cipher;
			} else if (keyparms->cipher == WLAN_CIPHER_SUITE_TKIP) {
				DBG_8723A("%s, set pairwise key, TKIP\n",
					  __func__);

				psta->dot118021XPrivacy =
					WLAN_CIPHER_SUITE_TKIP;

				/* set mic key */
				memcpy(psta->dot11tkiptxmickey.skey,
				       &keyparms->key[16], 8);
				memcpy(psta->dot11tkiprxmickey.skey,
				       &keyparms->key[24], 8);

				psecuritypriv->busetkipkey = 1;

			} else if (keyparms->cipher == WLAN_CIPHER_SUITE_CCMP) {
				DBG_8723A("%s, set pairwise key, CCMP\n",
					  __func__);

				psta->dot118021XPrivacy =
					WLAN_CIPHER_SUITE_CCMP;
			} else {
				DBG_8723A("%s, set pairwise key, none\n",
					  __func__);

				psta->dot118021XPrivacy = 0;
			}

			set_pairwise_key(padapter, psta);

			psta->ieee8021x_blocked = false;

			psta->bpairwise_key_installed = true;
		} else {	/* group key??? */
			if (keyparms->cipher == WLAN_CIPHER_SUITE_WEP40 ||
			    keyparms->cipher == WLAN_CIPHER_SUITE_WEP104) {
				memcpy(psecuritypriv->
				       dot118021XGrpKey[key_index].skey,
				       keyparms->key, key_len);

				psecuritypriv->dot118021XGrpPrivacy =
					keyparms->cipher;
			} else if (keyparms->cipher == WLAN_CIPHER_SUITE_TKIP) {
				psecuritypriv->dot118021XGrpPrivacy =
					WLAN_CIPHER_SUITE_TKIP;

				memcpy(psecuritypriv->
				       dot118021XGrpKey[key_index].skey,
				       keyparms->key,
				       (key_len > 16 ? 16 : key_len));

				/* set mic key */
				memcpy(psecuritypriv->
				       dot118021XGrptxmickey[key_index].skey,
				       &keyparms->key[16], 8);
				memcpy(psecuritypriv->
				       dot118021XGrprxmickey[key_index].skey,
				       &keyparms->key[24], 8);

				psecuritypriv->busetkipkey = 1;
			} else if (keyparms->cipher == WLAN_CIPHER_SUITE_CCMP) {
				psecuritypriv->dot118021XGrpPrivacy =
					WLAN_CIPHER_SUITE_CCMP;

				memcpy(psecuritypriv->
				       dot118021XGrpKey[key_index].skey,
				       keyparms->key,
				       (key_len > 16 ? 16 : key_len));
			} else {
				psecuritypriv->dot118021XGrpPrivacy = 0;
			}

			psecuritypriv->dot118021XGrpKeyid = key_index;

			psecuritypriv->binstallGrpkey = 1;

			psecuritypriv->dot11PrivacyAlgrthm =
				psecuritypriv->dot118021XGrpPrivacy;

			set_group_key(padapter, keyparms,
				      psecuritypriv->dot118021XGrpPrivacy,
				      key_index);

			pbcmc_sta = rtw_get_bcmc_stainfo23a(padapter);
			if (pbcmc_sta) {
				/* rx will use bmc_sta's
				   dot118021XPrivacy */
				pbcmc_sta->ieee8021x_blocked = false;
				pbcmc_sta->dot118021XPrivacy =
					psecuritypriv->dot118021XGrpPrivacy;
			}
		}
	}

exit:

	return ret;
}
#endif

static int rtw_cfg80211_set_encryption(struct net_device *dev, u8 key_index,
				       int set_tx, const u8 *sta_addr,
				       struct key_params *keyparms)
{
	int ret = 0;
	int key_len;
	struct rtw_adapter *padapter = netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct security_priv *psecuritypriv = &padapter->securitypriv;

	DBG_8723A("%s\n", __func__);

	key_len = keyparms->key_len;

	if (keyparms->cipher == WLAN_CIPHER_SUITE_WEP40 ||
	    keyparms->cipher == WLAN_CIPHER_SUITE_WEP104) {
		RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_err_,
			 ("wpa_set_encryption, crypt.alg = WEP\n"));
		DBG_8723A("wpa_set_encryption, crypt.alg = WEP\n");

		if (psecuritypriv->bWepDefaultKeyIdxSet == 0) {
			/* wep default key has not been set, so use this
			   key index as default key. */

			psecuritypriv->ndisencryptstatus =
				Ndis802_11Encryption1Enabled;
			psecuritypriv->dot11PrivacyAlgrthm = keyparms->cipher;
			psecuritypriv->dot118021XGrpPrivacy = keyparms->cipher;

			psecuritypriv->dot11PrivacyKeyIndex = key_index;
		}

		memcpy(&psecuritypriv->wep_key[key_index].key,
		       keyparms->key, key_len);

		psecuritypriv->wep_key[key_index].keylen = key_len;

		rtw_set_key23a(padapter, psecuritypriv, key_index, 0);

		goto exit;
	}

	if (padapter->securitypriv.dot11AuthAlgrthm ==
	    dot11AuthAlgrthm_8021X) {	/*  802_1x */
		struct sta_info *psta, *pbcmc_sta;
		struct sta_priv *pstapriv = &padapter->stapriv;

		if (check_fwstate(pmlmepriv,
				  WIFI_STATION_STATE | WIFI_MP_STATE)) {
			/* sta mode */
			psta = rtw_get_stainfo23a(pstapriv, get_bssid(pmlmepriv));
			if (psta == NULL) {
				DBG_8723A("%s, : Obtain Sta_info fail\n",
					  __func__);
			} else {
				/* Jeff: don't disable ieee8021x_blocked
				   while clearing key */
				if (keyparms->cipher != IW_AUTH_CIPHER_NONE &&
				    keyparms->cipher != 0)
					psta->ieee8021x_blocked = false;

				if ((padapter->securitypriv.ndisencryptstatus ==
				     Ndis802_11Encryption2Enabled) ||
				    (padapter->securitypriv.ndisencryptstatus ==
				     Ndis802_11Encryption3Enabled)) {
					psta->dot118021XPrivacy =
						padapter->securitypriv.
						dot11PrivacyAlgrthm;
				}

				if (set_tx == 1) {
					/* pairwise key */
					DBG_8723A("%s, : set_tx == 1\n",
						  __func__);

					memcpy(psta->dot118021x_UncstKey.skey,
					       keyparms->key,
					       (key_len > 16 ? 16 : key_len));

					if (keyparms->cipher ==
					    WLAN_CIPHER_SUITE_TKIP) {
						memcpy(psta->dot11tkiptxmickey.
						       skey,
						       &keyparms->key[16], 8);
						memcpy(psta->dot11tkiprxmickey.
						       skey,
						       &keyparms->key[24], 8);

						padapter->securitypriv.
							busetkipkey = 0;
					}
					DBG_8723A(" ~~~~set sta key:unicastkey\n");

					rtw_setstakey_cmd23a(padapter,
							  (unsigned char *)psta,
							  true);
				} else {	/* group key */
					memcpy(padapter->securitypriv.
					       dot118021XGrpKey[key_index].skey,
					       keyparms->key,
					       (key_len > 16 ? 16 : key_len));
					memcpy(padapter->securitypriv.
					       dot118021XGrptxmickey[key_index].
					       skey, &keyparms->key[16], 8);
					memcpy(padapter->securitypriv.
					       dot118021XGrprxmickey[key_index].
					       skey, &keyparms->key[24], 8);
					padapter->securitypriv.binstallGrpkey =
						1;
					DBG_8723A
					    (" ~~~~set sta key:groupkey\n");

					padapter->securitypriv.
					    dot118021XGrpKeyid = key_index;

					rtw_set_key23a(padapter,
						    &padapter->securitypriv,
						    key_index, 1);
				}
			}

			pbcmc_sta = rtw_get_bcmc_stainfo23a(padapter);
			if (pbcmc_sta) {
				/* Jeff: don't disable ieee8021x_blocked
				   while clearing key */
				if (keyparms->cipher != IW_AUTH_CIPHER_NONE &&
				    keyparms->cipher != 0)
					pbcmc_sta->ieee8021x_blocked = false;

				if ((padapter->securitypriv.ndisencryptstatus ==
				     Ndis802_11Encryption2Enabled) ||
				    (padapter->securitypriv.ndisencryptstatus ==
				     Ndis802_11Encryption3Enabled)) {
					pbcmc_sta->dot118021XPrivacy =
					    padapter->securitypriv.
					    dot11PrivacyAlgrthm;
				}
			}
		} else if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE)) {	/* adhoc mode */
		}
	}

exit:

	DBG_8723A("%s, ret =%d\n", __func__, ret);



	return ret;
}

static int cfg80211_rtw_add_key(struct wiphy *wiphy, struct net_device *ndev,
				u8 key_index, bool pairwise,
				const u8 *mac_addr, struct key_params *params)
{
	int set_tx, ret = 0;
	struct wireless_dev *rtw_wdev = wiphy_to_wdev(wiphy);
	struct rtw_adapter *padapter = wiphy_to_adapter(wiphy);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	u8 sta_addr[ETH_ALEN];

	DBG_8723A("%s(%s): adding key for %pM\n", __func__, ndev->name,
		  mac_addr);
	DBG_8723A("cipher = 0x%x\n", params->cipher);
	DBG_8723A("key_len = 0x%x\n", params->key_len);
	DBG_8723A("seq_len = 0x%x\n", params->seq_len);
	DBG_8723A("key_index =%d\n", key_index);
	DBG_8723A("pairwise =%d\n", pairwise);

	switch (params->cipher) {
	case IW_AUTH_CIPHER_NONE:
	case WLAN_CIPHER_SUITE_WEP40:
		if (params->key_len != WLAN_KEY_LEN_WEP40) {
			ret = -EINVAL;
			goto exit;
		}
	case WLAN_CIPHER_SUITE_WEP104:
		if (params->key_len != WLAN_KEY_LEN_WEP104) {
			ret = -EINVAL;
			goto exit;
		}
	case WLAN_CIPHER_SUITE_TKIP:
	case WLAN_CIPHER_SUITE_CCMP:
		break;
	default:
		ret = -ENOTSUPP;
		goto exit;
	}

	if (key_index >= WEP_KEYS || params->key_len < 0) {
		ret = -EINVAL;
		goto exit;
	}

	eth_broadcast_addr(sta_addr);

	if (!mac_addr || is_broadcast_ether_addr(mac_addr))
		set_tx = 0;	/* for wpa/wpa2 group key */
	else
		set_tx = 1;	/* for wpa/wpa2 pairwise key */

	if (check_fwstate(pmlmepriv, WIFI_STATION_STATE)) {
		ret = rtw_cfg80211_set_encryption(ndev, key_index, set_tx,
						  sta_addr, params);
	} else if (check_fwstate(pmlmepriv, WIFI_AP_STATE)) {
#ifdef CONFIG_8723AU_AP_MODE
		if (mac_addr)
			ether_addr_copy(sta_addr, mac_addr);

		ret = rtw_cfg80211_ap_set_encryption(ndev, key_index, set_tx,
						     sta_addr, params);
#endif
	} else {
		DBG_8723A("error! fw_state = 0x%x, iftype =%d\n",
			  pmlmepriv->fw_state, rtw_wdev->iftype);

	}

exit:
	return ret;
}

static int
cfg80211_rtw_get_key(struct wiphy *wiphy, struct net_device *ndev,
		     u8 key_index, bool pairwise, const u8 *mac_addr,
		     void *cookie,
		     void (*callback) (void *cookie, struct key_params *))
{
	DBG_8723A("%s(%s)\n", __func__, ndev->name);
	return 0;
}

static int cfg80211_rtw_del_key(struct wiphy *wiphy, struct net_device *ndev,
				u8 key_index, bool pairwise,
				const u8 *mac_addr)
{
	struct rtw_adapter *padapter = netdev_priv(ndev);
	struct security_priv *psecuritypriv = &padapter->securitypriv;

	DBG_8723A("%s(%s): key_index =%d\n", __func__, ndev->name, key_index);

	if (key_index == psecuritypriv->dot11PrivacyKeyIndex) {
		/* clear the flag of wep default key set. */
		psecuritypriv->bWepDefaultKeyIdxSet = 0;
	}

	return 0;
}

static int cfg80211_rtw_set_default_key(struct wiphy *wiphy,
					struct net_device *ndev, u8 key_index,
					bool unicast, bool multicast)
{
	struct rtw_adapter *padapter = netdev_priv(ndev);
	struct security_priv *psecuritypriv = &padapter->securitypriv;

	DBG_8723A("%s(%s): key_index =%d, unicast =%d, multicast =%d.\n",
		  __func__, ndev->name, key_index, unicast, multicast);

	if (key_index < NUM_WEP_KEYS &&
	    (psecuritypriv->dot11PrivacyAlgrthm == WLAN_CIPHER_SUITE_WEP40 ||
	     psecuritypriv->dot11PrivacyAlgrthm == WLAN_CIPHER_SUITE_WEP104)) {
		/* set wep default key */
		psecuritypriv->ndisencryptstatus = Ndis802_11Encryption1Enabled;

		psecuritypriv->dot11PrivacyKeyIndex = key_index;

		psecuritypriv->dot11PrivacyAlgrthm = WLAN_CIPHER_SUITE_WEP40;
		psecuritypriv->dot118021XGrpPrivacy = WLAN_CIPHER_SUITE_WEP40;
		if (psecuritypriv->wep_key[key_index].keylen == 13) {
			psecuritypriv->dot11PrivacyAlgrthm =
				WLAN_CIPHER_SUITE_WEP104;
			psecuritypriv->dot118021XGrpPrivacy =
				WLAN_CIPHER_SUITE_WEP104;
		}

		/* set the flag to represent that wep default key
		   has been set */
		psecuritypriv->bWepDefaultKeyIdxSet = 1;
	}

	return 0;
}

static u16 rtw_get_cur_max_rate(struct rtw_adapter *adapter)
{
	int i = 0;
	const u8 *p;
	u16 rate = 0, max_rate = 0;
	struct mlme_ext_priv *pmlmeext = &adapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	struct registry_priv *pregistrypriv = &adapter->registrypriv;
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	struct wlan_bssid_ex  *pcur_bss = &pmlmepriv->cur_network.network;
	struct ieee80211_ht_cap *pht_capie;
	u8 rf_type = 0;
	u8 bw_40MHz = 0, short_GI_20 = 0, short_GI_40 = 0;
	u16 mcs_rate = 0;

	p = cfg80211_find_ie(WLAN_EID_HT_CAPABILITY,
			     pcur_bss->IEs, pcur_bss->IELength);
	if (p && p[1] > 0) {
		pht_capie = (struct ieee80211_ht_cap *)(p + 2);

		memcpy(&mcs_rate, &pht_capie->mcs, 2);

		/* bw_40MHz = (pht_capie->cap_info&
		   IEEE80211_HT_CAP_SUP_WIDTH_20_40) ? 1:0; */
		/* cur_bwmod is updated by beacon, pmlmeinfo is
		   updated by association response */
		bw_40MHz = (pmlmeext->cur_bwmode &&
			    (pmlmeinfo->HT_info.ht_param &
			     IEEE80211_HT_PARAM_CHAN_WIDTH_ANY)) ? 1:0;

		/* short_GI = (pht_capie->cap_info & (IEEE80211_HT_CAP
		   _SGI_20|IEEE80211_HT_CAP_SGI_40)) ? 1 : 0; */
		short_GI_20 = (pmlmeinfo->ht_cap.cap_info &
			       cpu_to_le16(IEEE80211_HT_CAP_SGI_20)) ? 1:0;
		short_GI_40 = (pmlmeinfo->ht_cap.cap_info &
			       cpu_to_le16(IEEE80211_HT_CAP_SGI_40)) ? 1:0;

		rf_type = rtl8723a_get_rf_type(adapter);
		max_rate = rtw_mcs_rate23a(rf_type, bw_40MHz &
					   pregistrypriv->cbw40_enable,
					   short_GI_20, short_GI_40,
					   &pmlmeinfo->ht_cap.mcs);
	} else {
		while (pcur_bss->SupportedRates[i] != 0 &&
		       pcur_bss->SupportedRates[i] != 0xFF) {
			rate = pcur_bss->SupportedRates[i] & 0x7F;
			if (rate>max_rate)
				max_rate = rate;
			i++;
		}

		max_rate = max_rate * 10 / 2;
	}

	return max_rate;
}

static int cfg80211_rtw_get_station(struct wiphy *wiphy,
				    struct net_device *ndev,
				    const u8 *mac, struct station_info *sinfo)
{
	int ret = 0;
	struct rtw_adapter *padapter = wiphy_to_adapter(wiphy);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct sta_info *psta = NULL;
	struct sta_priv *pstapriv = &padapter->stapriv;

	sinfo->filled = 0;

	if (!mac) {
		DBG_8723A("%s(%s): mac ==%p\n", __func__, ndev->name, mac);
		ret = -ENOENT;
		goto exit;
	}

	psta = rtw_get_stainfo23a(pstapriv, mac);
	if (psta == NULL) {
		DBG_8723A("%s, sta_info is null\n", __func__);
		ret = -ENOENT;
		goto exit;
	}
	DBG_8723A("%s(%s): mac =" MAC_FMT "\n", __func__, ndev->name,
		  MAC_ARG(mac));

	/* for infra./P2PClient mode */
	if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) &&
	    check_fwstate(pmlmepriv, _FW_LINKED)) {
		struct wlan_network *cur_network = &pmlmepriv->cur_network;

		if (!ether_addr_equal(mac, cur_network->network.MacAddress)) {
			DBG_8723A("%s, mismatch bssid =" MAC_FMT "\n", __func__,
				  MAC_ARG(cur_network->network.MacAddress));
			ret = -ENOENT;
			goto exit;
		}

		sinfo->filled |= STATION_INFO_SIGNAL;
		sinfo->signal = translate_percentage_to_dbm(padapter->recvpriv.
							    signal_strength);

		sinfo->filled |= STATION_INFO_TX_BITRATE;
		sinfo->txrate.legacy = rtw_get_cur_max_rate(padapter);

		sinfo->filled |= STATION_INFO_RX_PACKETS;
		sinfo->rx_packets = sta_rx_data_pkts(psta);

		sinfo->filled |= STATION_INFO_TX_PACKETS;
		sinfo->tx_packets = psta->sta_stats.tx_pkts;
	}

	/* for Ad-Hoc/AP mode */
	if ((check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) ||
	     check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) ||
	     check_fwstate(pmlmepriv, WIFI_AP_STATE)) &&
	    check_fwstate(pmlmepriv, _FW_LINKED)
	    ) {
		/* TODO: should acquire station info... */
	}

exit:
	return ret;
}

static int cfg80211_infrastructure_mode(struct rtw_adapter *padapter,
				 enum nl80211_iftype ifmode)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct wlan_network *cur_network = &pmlmepriv->cur_network;
	enum nl80211_iftype old_mode;

	old_mode = cur_network->network.ifmode;

	RT_TRACE(_module_rtl871x_ioctl_set_c_, _drv_notice_,
		 ("+%s: old =%d new =%d fw_state = 0x%08x\n", __func__,
		  old_mode, ifmode, get_fwstate(pmlmepriv)));

	if (old_mode != ifmode) {
		spin_lock_bh(&pmlmepriv->lock);

		RT_TRACE(_module_rtl871x_ioctl_set_c_, _drv_info_,
			 (" change mode!"));

		if (old_mode == NL80211_IFTYPE_AP ||
		    old_mode == NL80211_IFTYPE_P2P_GO) {
			/* change to other mode from Ndis802_11APMode */
			cur_network->join_res = -1;

#ifdef CONFIG_8723AU_AP_MODE
			stop_ap_mode23a(padapter);
#endif
		}

		if (check_fwstate(pmlmepriv, _FW_LINKED) ||
		    old_mode == NL80211_IFTYPE_ADHOC)
			rtw_disassoc_cmd23a(padapter, 0, true);

		if (check_fwstate(pmlmepriv, _FW_LINKED) ||
		    check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE))
			rtw_free_assoc_resources23a(padapter, 1);

		if (old_mode == NL80211_IFTYPE_STATION ||
		    old_mode == NL80211_IFTYPE_P2P_CLIENT ||
		    old_mode == NL80211_IFTYPE_ADHOC) {
			if (check_fwstate(pmlmepriv, _FW_LINKED)) {
				/* will clr Linked_state; before this function,
				   we must have chked whether issue
				   dis-assoc_cmd or not */
				rtw_indicate_disconnect23a(padapter);
			}
	       }

		cur_network->network.ifmode = ifmode;

		_clr_fwstate_(pmlmepriv, ~WIFI_NULL_STATE);

		switch (ifmode) {
		case NL80211_IFTYPE_ADHOC:
			set_fwstate(pmlmepriv, WIFI_ADHOC_STATE);
			break;

		case NL80211_IFTYPE_P2P_CLIENT:
		case NL80211_IFTYPE_STATION:
			set_fwstate(pmlmepriv, WIFI_STATION_STATE);
			break;

		case NL80211_IFTYPE_P2P_GO:
		case NL80211_IFTYPE_AP:
			set_fwstate(pmlmepriv, WIFI_AP_STATE);
#ifdef CONFIG_8723AU_AP_MODE
			start_ap_mode23a(padapter);
			/* rtw_indicate_connect23a(padapter); */
#endif
			break;

		default:
			break;
		}

		/* SecClearAllKeys(adapter); */

		/* RT_TRACE(COMP_OID_SET, DBG_LOUD,
		   ("set_infrastructure: fw_state:%x after changing mode\n", */
		/* get_fwstate(pmlmepriv))); */

		spin_unlock_bh(&pmlmepriv->lock);
	}

	return _SUCCESS;
}

static int cfg80211_rtw_change_iface(struct wiphy *wiphy,
				     struct net_device *ndev,
				     enum nl80211_iftype type, u32 *flags,
				     struct vif_params *params)
{
	enum nl80211_iftype old_type;
	struct rtw_adapter *padapter = wiphy_to_adapter(wiphy);
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct wireless_dev *rtw_wdev = wiphy_to_wdev(wiphy);
	int ret = 0;

	DBG_8723A("%s(%s): call netdev_open23a\n", __func__, ndev->name);

	old_type = rtw_wdev->iftype;
	DBG_8723A("%s(%s): old_iftype =%d, new_iftype =%d\n",
		  __func__, ndev->name, old_type, type);

	if (old_type != type) {
		pmlmeext->action_public_rxseq = 0xffff;
		pmlmeext->action_public_dialog_token = 0xff;
	}

	switch (type) {
	case NL80211_IFTYPE_ADHOC:
	case NL80211_IFTYPE_P2P_CLIENT:
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_P2P_GO:
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_UNSPECIFIED:
		break;
	default:
		return -EOPNOTSUPP;
	}

	rtw_wdev->iftype = type;

	if (cfg80211_infrastructure_mode(padapter, type) != _SUCCESS) {
		rtw_wdev->iftype = old_type;
		ret = -EPERM;
		goto exit;
	}

	rtw_setopmode_cmd23a(padapter, type);

exit:
	return ret;
}

void rtw_cfg80211_indicate_scan_done(struct rtw_wdev_priv *pwdev_priv,
				     bool aborted)
{
	spin_lock_bh(&pwdev_priv->scan_req_lock);
	if (pwdev_priv->scan_request != NULL) {
		DBG_8723A("%s with scan req\n", __func__);

		if (pwdev_priv->scan_request->wiphy !=
		    pwdev_priv->rtw_wdev->wiphy)
			DBG_8723A("error wiphy compare\n");
		else
			cfg80211_scan_done(pwdev_priv->scan_request, aborted);

		pwdev_priv->scan_request = NULL;
	} else {
		DBG_8723A("%s without scan req\n", __func__);
	}
	spin_unlock_bh(&pwdev_priv->scan_req_lock);
}

void rtw_cfg80211_surveydone_event_callback(struct rtw_adapter *padapter)
{
	struct list_head *plist, *phead, *ptmp;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct rtw_queue *queue = &pmlmepriv->scanned_queue;
	struct wlan_network *pnetwork;

	spin_lock_bh(&pmlmepriv->scanned_queue.lock);

	phead = get_list_head(queue);

	list_for_each_safe(plist, ptmp, phead) {
		pnetwork = container_of(plist, struct wlan_network, list);

		/* report network only if the current channel set
		   contains the channel to which this network belongs */
		if (rtw_ch_set_search_ch23a
		    (padapter->mlmeextpriv.channel_set,
		     pnetwork->network.DSConfig) >= 0)
			rtw_cfg80211_inform_bss(padapter, pnetwork);
	}

	spin_unlock_bh(&pmlmepriv->scanned_queue.lock);

	/* call this after other things have been done */
	rtw_cfg80211_indicate_scan_done(wdev_to_priv(padapter->rtw_wdev),
					false);
}

static int rtw_cfg80211_set_probe_req_wpsp2pie(struct rtw_adapter *padapter,
					       char *buf, int len)
{
	int ret = 0;
	const u8 *wps_ie;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

	DBG_8723A("%s, ielen =%d\n", __func__, len);

	if (len > 0) {
		wps_ie = cfg80211_find_vendor_ie(WLAN_OUI_MICROSOFT,
						 WLAN_OUI_TYPE_MICROSOFT_WPS,
						 buf, len);
		if (wps_ie) {
			DBG_8723A("probe_req_wps_ielen =%d\n", wps_ie[1]);

			if (pmlmepriv->wps_probe_req_ie) {
				pmlmepriv->wps_probe_req_ie_len = 0;
				kfree(pmlmepriv->wps_probe_req_ie);
				pmlmepriv->wps_probe_req_ie = NULL;
			}

			pmlmepriv->wps_probe_req_ie = kmemdup(wps_ie, wps_ie[1],
							      GFP_KERNEL);
			if (pmlmepriv->wps_probe_req_ie == NULL) {
				DBG_8723A("%s()-%d: kmalloc() ERROR!\n",
					  __func__, __LINE__);
				return -EINVAL;
			}
			pmlmepriv->wps_probe_req_ie_len = wps_ie[1];
		}
	}

	return ret;
}

static int cfg80211_rtw_scan(struct wiphy *wiphy,
			     struct cfg80211_scan_request *request)
{
	int i;
	u8 _status = false;
	int ret = 0;
	struct rtw_adapter *padapter = wiphy_to_adapter(wiphy);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct cfg80211_ssid ssid[RTW_SSID_SCAN_AMOUNT];
	struct rtw_ieee80211_channel ch[RTW_CHANNEL_SCAN_AMOUNT];
	struct rtw_wdev_priv *pwdev_priv = wdev_to_priv(padapter->rtw_wdev);
	struct cfg80211_ssid *ssids = request->ssids;
	bool need_indicate_scan_done = false;

	DBG_8723A("%s(%s)\n", __func__, padapter->pnetdev->name);

	spin_lock_bh(&pwdev_priv->scan_req_lock);
	pwdev_priv->scan_request = request;
	spin_unlock_bh(&pwdev_priv->scan_req_lock);

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE)) {
		DBG_8723A("%s under WIFI_AP_STATE\n", __func__);
		/* need_indicate_scan_done = true; */
		/* goto check_need_indicate_scan_done; */
	}

	if (rtw_pwr_wakeup(padapter) == _FAIL) {
		need_indicate_scan_done = true;
		goto check_need_indicate_scan_done;
	}

	if (request->ie && request->ie_len > 0) {
		rtw_cfg80211_set_probe_req_wpsp2pie(padapter,
						    (u8 *) request->ie,
						    request->ie_len);
	}

	if (pmlmepriv->LinkDetectInfo.bBusyTraffic == true) {
		DBG_8723A("%s, bBusyTraffic == true\n", __func__);
		need_indicate_scan_done = true;
		goto check_need_indicate_scan_done;
	}
	if (rtw_is_scan_deny(padapter)) {
		DBG_8723A("%s(%s): scan deny\n", __func__,
			  padapter->pnetdev->name);
		need_indicate_scan_done = true;
		goto check_need_indicate_scan_done;
	}

	if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY | _FW_UNDER_LINKING) ==
	    true) {
		DBG_8723A("%s, fwstate = 0x%x\n", __func__, pmlmepriv->fw_state);
		need_indicate_scan_done = true;
		goto check_need_indicate_scan_done;
	}

	memset(ssid, 0, sizeof(struct cfg80211_ssid) * RTW_SSID_SCAN_AMOUNT);
	/* parsing request ssids, n_ssids */
	for (i = 0; i < request->n_ssids && i < RTW_SSID_SCAN_AMOUNT; i++) {
		DBG_8723A("ssid =%s, len =%d\n", ssids[i].ssid,
			  ssids[i].ssid_len);
		memcpy(ssid[i].ssid, ssids[i].ssid, ssids[i].ssid_len);
		ssid[i].ssid_len = ssids[i].ssid_len;
	}

	/* parsing channels, n_channels */
	memset(ch, 0,
	       sizeof(struct rtw_ieee80211_channel) * RTW_CHANNEL_SCAN_AMOUNT);

	if (request->n_channels == 1) {
		for (i = 0; i < request->n_channels &&
		     i < RTW_CHANNEL_SCAN_AMOUNT; i++) {
			DBG_8723A("%s:(%s):" CHAN_FMT "\n",
				  __func__, padapter->pnetdev->name,
				  CHAN_ARG(request->channels[i]));
			ch[i].hw_value = request->channels[i]->hw_value;
			ch[i].flags = request->channels[i]->flags;
		}
	}

	spin_lock_bh(&pmlmepriv->lock);
	if (request->n_channels == 1) {
		memcpy(&ch[1], &ch[0], sizeof(struct rtw_ieee80211_channel));
		memcpy(&ch[2], &ch[0], sizeof(struct rtw_ieee80211_channel));
		_status = rtw_sitesurvey_cmd23a(padapter, ssid,
					     RTW_SSID_SCAN_AMOUNT, ch, 3);
	} else {
		_status = rtw_sitesurvey_cmd23a(padapter, ssid,
					     RTW_SSID_SCAN_AMOUNT, NULL, 0);
	}
	spin_unlock_bh(&pmlmepriv->lock);

	if (_status == false)
		ret = -1;

check_need_indicate_scan_done:
	if (need_indicate_scan_done)
		rtw_cfg80211_surveydone_event_callback(padapter);
	return ret;
}

static int cfg80211_rtw_set_wiphy_params(struct wiphy *wiphy, u32 changed)
{
	DBG_8723A("%s\n", __func__);
	return 0;
}

static int cfg80211_rtw_join_ibss(struct wiphy *wiphy, struct net_device *ndev,
				  struct cfg80211_ibss_params *params)
{
	DBG_8723A("%s(%s)\n", __func__, ndev->name);
	return 0;
}

static int cfg80211_rtw_leave_ibss(struct wiphy *wiphy, struct net_device *ndev)
{
	DBG_8723A("%s(%s)\n", __func__, ndev->name);
	return 0;
}

static int rtw_cfg80211_set_wpa_version(struct security_priv *psecuritypriv,
					u32 wpa_version)
{
	DBG_8723A("%s, wpa_version =%d\n", __func__, wpa_version);

	if (!wpa_version) {
		psecuritypriv->ndisauthtype = Ndis802_11AuthModeOpen;
		return 0;
	}

	if (wpa_version & (NL80211_WPA_VERSION_1 | NL80211_WPA_VERSION_2)) {
		psecuritypriv->ndisauthtype = Ndis802_11AuthModeWPAPSK;
	}

/*
	if (wpa_version & NL80211_WPA_VERSION_2)
	{
		psecuritypriv->ndisauthtype = Ndis802_11AuthModeWPA2PSK;
	}
*/

	return 0;
}

static int rtw_cfg80211_set_auth_type(struct security_priv *psecuritypriv,
				      enum nl80211_auth_type sme_auth_type)
{
	DBG_8723A("%s, nl80211_auth_type =%d\n", __func__, sme_auth_type);

	switch (sme_auth_type) {
	case NL80211_AUTHTYPE_AUTOMATIC:
		psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_Auto;

		break;
	case NL80211_AUTHTYPE_OPEN_SYSTEM:
		psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_Open;

		if (psecuritypriv->ndisauthtype > Ndis802_11AuthModeWPA)
			psecuritypriv->dot11AuthAlgrthm =
				dot11AuthAlgrthm_8021X;
		break;
	case NL80211_AUTHTYPE_SHARED_KEY:
		psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_Shared;

		psecuritypriv->ndisencryptstatus = Ndis802_11Encryption1Enabled;
		break;
	default:
		psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_Open;
		/* return -ENOTSUPP; */
	}

	return 0;
}

static int rtw_cfg80211_set_cipher(struct security_priv *psecuritypriv,
				   u32 cipher, bool ucast)
{
	u32 ndisencryptstatus = Ndis802_11EncryptionDisabled;

	u32 *profile_cipher = ucast ? &psecuritypriv->dot11PrivacyAlgrthm :
	    &psecuritypriv->dot118021XGrpPrivacy;

	DBG_8723A("%s, ucast =%d, cipher = 0x%x\n", __func__, ucast, cipher);

	if (!cipher) {
		*profile_cipher = 0;
		psecuritypriv->ndisencryptstatus = ndisencryptstatus;
		return 0;
	}

	switch (cipher) {
	case IW_AUTH_CIPHER_NONE:
		*profile_cipher = 0;
		ndisencryptstatus = Ndis802_11EncryptionDisabled;
		break;
	case WLAN_CIPHER_SUITE_WEP40:
		*profile_cipher = WLAN_CIPHER_SUITE_WEP40;
		ndisencryptstatus = Ndis802_11Encryption1Enabled;
		break;
	case WLAN_CIPHER_SUITE_WEP104:
		*profile_cipher = WLAN_CIPHER_SUITE_WEP104;
		ndisencryptstatus = Ndis802_11Encryption1Enabled;
		break;
	case WLAN_CIPHER_SUITE_TKIP:
		*profile_cipher = WLAN_CIPHER_SUITE_TKIP;
		ndisencryptstatus = Ndis802_11Encryption2Enabled;
		break;
	case WLAN_CIPHER_SUITE_CCMP:
		*profile_cipher = WLAN_CIPHER_SUITE_CCMP;
		ndisencryptstatus = Ndis802_11Encryption3Enabled;
		break;
	default:
		DBG_8723A("Unsupported cipher: 0x%x\n", cipher);
		return -ENOTSUPP;
	}

	if (ucast)
		psecuritypriv->ndisencryptstatus = ndisencryptstatus;

	return 0;
}

static int rtw_cfg80211_set_key_mgt(struct security_priv *psecuritypriv,
				    u32 key_mgt)
{
	DBG_8723A("%s, key_mgt = 0x%x\n", __func__, key_mgt);

	if (key_mgt == WLAN_AKM_SUITE_8021X)
		psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_8021X;
	else if (key_mgt == WLAN_AKM_SUITE_PSK)
		psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_8021X;
	else
		DBG_8723A("Invalid key mgt: 0x%x\n", key_mgt);

	return 0;
}

static int rtw_cfg80211_set_wpa_ie(struct rtw_adapter *padapter, const u8 *pie,
				   size_t ielen)
{
	const u8 *wps_ie;
	int group_cipher = 0, pairwise_cipher = 0;
	int ret = 0;
	const u8 *pwpa, *pwpa2;
	int i;

	if (!pie || !ielen) {
		/* Treat this as normal case, but need to clear
		   WIFI_UNDER_WPS */
		_clr_fwstate_(&padapter->mlmepriv, WIFI_UNDER_WPS);
		goto exit;
	}
	if (ielen > MAX_WPA_IE_LEN + MAX_WPS_IE_LEN + MAX_P2P_IE_LEN) {
		ret = -EINVAL;
		goto exit;
	}

	/* dump */
	DBG_8723A("set wpa_ie(length:%zu):\n", ielen);
	for (i = 0; i < ielen; i = i + 8)
		DBG_8723A("0x%.2x 0x%.2x 0x%.2x 0x%.2x "
			  "0x%.2x 0x%.2x 0x%.2x 0x%.2x\n",
			  pie[i], pie[i + 1], pie[i + 2], pie[i + 3],
			  pie[i + 4], pie[i + 5], pie[i + 6], pie[i + 7]);
	if (ielen < RSN_HEADER_LEN) {
		RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_err_,
			 ("Ie len too short %d\n", (int)ielen));
		ret = -1;
		goto exit;
	}

	pwpa = cfg80211_find_vendor_ie(WLAN_OUI_MICROSOFT,
				       WLAN_OUI_TYPE_MICROSOFT_WPA,
				       pie, ielen);
	if (pwpa && pwpa[1] > 0) {
		if (rtw_parse_wpa_ie23a(pwpa, pwpa[1] + 2, &group_cipher,
					&pairwise_cipher, NULL) == _SUCCESS) {
			padapter->securitypriv.dot11AuthAlgrthm =
				dot11AuthAlgrthm_8021X;
			padapter->securitypriv.ndisauthtype =
				Ndis802_11AuthModeWPAPSK;
			memcpy(padapter->securitypriv.supplicant_ie, pwpa,
			       pwpa[1] + 2);

			DBG_8723A("got wpa_ie, wpa_ielen:%u\n", pwpa[1]);
		}
	}

	pwpa2 = cfg80211_find_ie(WLAN_EID_RSN, pie, ielen);
	if (pwpa2 && pwpa2[1] > 0) {
		if (rtw_parse_wpa2_ie23a (pwpa2, pwpa2[1] + 2, &group_cipher,
					  &pairwise_cipher, NULL) == _SUCCESS) {
			padapter->securitypriv.dot11AuthAlgrthm =
				dot11AuthAlgrthm_8021X;
			padapter->securitypriv.ndisauthtype =
				Ndis802_11AuthModeWPA2PSK;
			memcpy(padapter->securitypriv.supplicant_ie, pwpa2,
			       pwpa2[1] + 2);

			DBG_8723A("got wpa2_ie, wpa2_ielen:%u\n", pwpa2[1]);
		}
	}

	if (group_cipher == 0) {
		group_cipher = WPA_CIPHER_NONE;
	}
	if (pairwise_cipher == 0) {
		pairwise_cipher = WPA_CIPHER_NONE;
	}

	switch (group_cipher) {
	case WPA_CIPHER_NONE:
		padapter->securitypriv.dot118021XGrpPrivacy = 0;
		padapter->securitypriv.ndisencryptstatus =
			Ndis802_11EncryptionDisabled;
		break;
	case WPA_CIPHER_WEP40:
		padapter->securitypriv.dot118021XGrpPrivacy = WLAN_CIPHER_SUITE_WEP40;
		padapter->securitypriv.ndisencryptstatus =
			Ndis802_11Encryption1Enabled;
		break;
	case WPA_CIPHER_TKIP:
		padapter->securitypriv.dot118021XGrpPrivacy = WLAN_CIPHER_SUITE_TKIP;
		padapter->securitypriv.ndisencryptstatus =
			Ndis802_11Encryption2Enabled;
		break;
	case WPA_CIPHER_CCMP:
		padapter->securitypriv.dot118021XGrpPrivacy = WLAN_CIPHER_SUITE_CCMP;
		padapter->securitypriv.ndisencryptstatus =
			Ndis802_11Encryption3Enabled;
		break;
	case WPA_CIPHER_WEP104:
		padapter->securitypriv.dot118021XGrpPrivacy = WLAN_CIPHER_SUITE_WEP104;
		padapter->securitypriv.ndisencryptstatus =
			Ndis802_11Encryption1Enabled;
		break;
	}

	switch (pairwise_cipher) {
	case WPA_CIPHER_NONE:
		padapter->securitypriv.dot11PrivacyAlgrthm = 0;
		padapter->securitypriv.ndisencryptstatus =
			Ndis802_11EncryptionDisabled;
		break;
	case WPA_CIPHER_WEP40:
		padapter->securitypriv.dot11PrivacyAlgrthm = WLAN_CIPHER_SUITE_WEP40;
		padapter->securitypriv.ndisencryptstatus =
			Ndis802_11Encryption1Enabled;
		break;
	case WPA_CIPHER_TKIP:
		padapter->securitypriv.dot11PrivacyAlgrthm = WLAN_CIPHER_SUITE_TKIP;
		padapter->securitypriv.ndisencryptstatus =
			Ndis802_11Encryption2Enabled;
		break;
	case WPA_CIPHER_CCMP:
		padapter->securitypriv.dot11PrivacyAlgrthm = WLAN_CIPHER_SUITE_CCMP;
		padapter->securitypriv.ndisencryptstatus =
			Ndis802_11Encryption3Enabled;
		break;
	case WPA_CIPHER_WEP104:
		padapter->securitypriv.dot11PrivacyAlgrthm = WLAN_CIPHER_SUITE_WEP104;
		padapter->securitypriv.ndisencryptstatus =
			Ndis802_11Encryption1Enabled;
		break;
	}

	wps_ie = cfg80211_find_vendor_ie(WLAN_OUI_MICROSOFT,
					 WLAN_OUI_TYPE_MICROSOFT_WPS,
					 pie, ielen);
	if (wps_ie && wps_ie[1] > 0) {
		DBG_8723A("got wps_ie, wps_ielen:%u\n", wps_ie[1]);
		padapter->securitypriv.wps_ie_len = wps_ie[1];
		memcpy(padapter->securitypriv.wps_ie, wps_ie,
		       padapter->securitypriv.wps_ie_len);
		set_fwstate(&padapter->mlmepriv, WIFI_UNDER_WPS);
	} else {
		_clr_fwstate_(&padapter->mlmepriv, WIFI_UNDER_WPS);
	}

	/* TKIP and AES disallow multicast packets until installing group key */
	if (padapter->securitypriv.dot11PrivacyAlgrthm ==
	    WLAN_CIPHER_SUITE_TKIP ||
	    padapter->securitypriv.dot11PrivacyAlgrthm ==
	    WLAN_CIPHER_SUITE_CCMP)
		/* WPS open need to enable multicast */
		/* check_fwstate(&padapter->mlmepriv, WIFI_UNDER_WPS) == true)*/
		rtl8723a_off_rcr_am(padapter);

	RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_info_,
		 ("rtw_set_wpa_ie: pairwise_cipher = 0x%08x padapter->"
		  "securitypriv.ndisencryptstatus =%d padapter->"
		  "securitypriv.ndisauthtype =%d\n", pairwise_cipher,
		  padapter->securitypriv.ndisencryptstatus,
		  padapter->securitypriv.ndisauthtype));

exit:
	if (ret)
		_clr_fwstate_(&padapter->mlmepriv, WIFI_UNDER_WPS);
	return ret;
}

static int rtw_cfg80211_add_wep(struct rtw_adapter *padapter,
				struct rtw_wep_key *wep, u8 keyid)
{
	int res;
	struct security_priv *psecuritypriv = &padapter->securitypriv;

	if (keyid >= NUM_WEP_KEYS) {
		RT_TRACE(_module_rtl871x_ioctl_set_c_, _drv_err_,
			 ("%s:keyid>4 =>fail\n", __func__));
		res = _FAIL;
		goto exit;
	}

	switch (wep->keylen) {
	case WLAN_KEY_LEN_WEP40:
		psecuritypriv->dot11PrivacyAlgrthm = WLAN_CIPHER_SUITE_WEP40;
		RT_TRACE(_module_rtl871x_ioctl_set_c_, _drv_info_,
			 ("%s:wep->KeyLength = 5\n", __func__));
		break;
	case WLAN_KEY_LEN_WEP104:
		psecuritypriv->dot11PrivacyAlgrthm = WLAN_CIPHER_SUITE_WEP104;
		RT_TRACE(_module_rtl871x_ioctl_set_c_, _drv_info_,
			 ("%s:wep->KeyLength = 13\n", __func__));
		break;
	default:
		psecuritypriv->dot11PrivacyAlgrthm = 0;
		RT_TRACE(_module_rtl871x_ioctl_set_c_, _drv_info_,
			 ("%s:wep->KeyLength!= 5 or 13\n", __func__));
		res = _FAIL;
		goto exit;
	}

	RT_TRACE(_module_rtl871x_ioctl_set_c_, _drv_info_,
		 ("%s:before memcpy, wep->KeyLength = 0x%x keyid =%x\n",
		  __func__, wep->keylen, keyid));

	memcpy(&psecuritypriv->wep_key[keyid], wep, sizeof(struct rtw_wep_key));

	psecuritypriv->dot11PrivacyKeyIndex = keyid;

	RT_TRACE(_module_rtl871x_ioctl_set_c_, _drv_info_,
		 ("%s:security key material : "
		  "%x %x %x %x %x %x %x %x %x %x %x %x %x\n", __func__,
		  psecuritypriv->wep_key[keyid].key[0],
		  psecuritypriv->wep_key[keyid].key[1],
		  psecuritypriv->wep_key[keyid].key[2],
		  psecuritypriv->wep_key[keyid].key[3],
		  psecuritypriv->wep_key[keyid].key[4],
		  psecuritypriv->wep_key[keyid].key[5],
		  psecuritypriv->wep_key[keyid].key[6],
		  psecuritypriv->wep_key[keyid].key[7],
		  psecuritypriv->wep_key[keyid].key[8],
		  psecuritypriv->wep_key[keyid].key[9],
		  psecuritypriv->wep_key[keyid].key[10],
		  psecuritypriv->wep_key[keyid].key[11],
		  psecuritypriv->wep_key[keyid].key[12]));

	res = rtw_set_key23a(padapter, psecuritypriv, keyid, 1);

exit:

	return res;
}

static int rtw_set_ssid(struct rtw_adapter *padapter,
			struct wlan_network *newnetwork)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct wlan_network *pnetwork = &pmlmepriv->cur_network;
	int status = _SUCCESS;
	u32 cur_time = 0;

	DBG_8723A_LEVEL(_drv_always_, "set ssid [%s] fw_state = 0x%08x\n",
			newnetwork->network.Ssid.ssid, get_fwstate(pmlmepriv));

	if (padapter->hw_init_completed == false) {
		RT_TRACE(_module_rtl871x_ioctl_set_c_, _drv_err_,
			 ("set_ssid: hw_init_completed == false =>exit!!!\n"));
		status = _FAIL;
		goto exit;
	}

	spin_lock_bh(&pmlmepriv->lock);

	DBG_8723A("Set SSID under fw_state = 0x%08x\n", get_fwstate(pmlmepriv));
	if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY))
		goto handle_tkip_countermeasure;

	if (check_fwstate(pmlmepriv, _FW_LINKED|WIFI_ADHOC_MASTER_STATE)) {
		RT_TRACE(_module_rtl871x_ioctl_set_c_, _drv_info_,
			 ("set_ssid: _FW_LINKED||WIFI_ADHOC_MASTER_STATE\n"));

		if (pmlmepriv->assoc_ssid.ssid_len ==
		    newnetwork->network.Ssid.ssid_len &&
		    !memcmp(&pmlmepriv->assoc_ssid.ssid,
			    newnetwork->network.Ssid.ssid,
			    newnetwork->network.Ssid.ssid_len)) {
			if (!check_fwstate(pmlmepriv, WIFI_STATION_STATE)) {
				RT_TRACE(_module_rtl871x_ioctl_set_c_,
					 _drv_err_, ("New SSID is same SSID, "
						     "fw_state = 0x%08x\n",
						     get_fwstate(pmlmepriv)));

				if (rtw_is_same_ibss23a(padapter, pnetwork)) {
					/*
					 * it means driver is in
					 * WIFI_ADHOC_MASTER_STATE, we needn't
					 * create bss again.
					 */
					goto release_mlme_lock;
				}

				/*
				 * if in WIFI_ADHOC_MASTER_STATE |
				 * WIFI_ADHOC_STATE, create bss or
				 * rejoin again
				 */
				rtw_disassoc_cmd23a(padapter, 0, true);

				if (check_fwstate(pmlmepriv, _FW_LINKED))
					rtw_indicate_disconnect23a(padapter);

				rtw_free_assoc_resources23a(padapter, 1);

				if (check_fwstate(pmlmepriv,
						  WIFI_ADHOC_MASTER_STATE)) {
					_clr_fwstate_(pmlmepriv,
						      WIFI_ADHOC_MASTER_STATE);
					set_fwstate(pmlmepriv,
						    WIFI_ADHOC_STATE);
				}
			} else {
				rtw_lps_ctrl_wk_cmd23a(padapter,
						       LPS_CTRL_JOINBSS, 1);
			}
		} else {
			RT_TRACE(_module_rtl871x_ioctl_set_c_, _drv_info_,
				 ("Set SSID not the same ssid\n"));
			RT_TRACE(_module_rtl871x_ioctl_set_c_, _drv_info_,
				 ("set_ssid =[%s] len = 0x%x\n",
				  newnetwork->network.Ssid.ssid,
				  newnetwork->network.Ssid.ssid_len));
			RT_TRACE(_module_rtl871x_ioctl_set_c_, _drv_info_,
				 ("assoc_ssid =[%s] len = 0x%x\n",
				  pmlmepriv->assoc_ssid.ssid,
				  pmlmepriv->assoc_ssid.ssid_len));

			rtw_disassoc_cmd23a(padapter, 0, true);

			if (check_fwstate(pmlmepriv, _FW_LINKED))
				rtw_indicate_disconnect23a(padapter);

			rtw_free_assoc_resources23a(padapter, 1);

			if (check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE)) {
				_clr_fwstate_(pmlmepriv, WIFI_ADHOC_MASTER_STATE);
				set_fwstate(pmlmepriv, WIFI_ADHOC_STATE);
			}
		}
	}

handle_tkip_countermeasure:

	if (padapter->securitypriv.btkip_countermeasure == true) {
		cur_time = jiffies;

		if ((cur_time -
		     padapter->securitypriv.btkip_countermeasure_time) >
		    60 * HZ) {
			padapter->securitypriv.btkip_countermeasure = false;
			padapter->securitypriv.btkip_countermeasure_time = 0;
		} else {
			status = _FAIL;
			goto release_mlme_lock;
		}
	}

	memcpy(&pmlmepriv->assoc_ssid, &newnetwork->network.Ssid,
	       sizeof(struct cfg80211_ssid));

	pmlmepriv->assoc_by_bssid = false;

	pmlmepriv->to_join = true;

	if (!check_fwstate(pmlmepriv, _FW_UNDER_SURVEY)) {
		pmlmepriv->cur_network.join_res = -2;

		status = rtw_do_join_network(padapter, newnetwork);
		if (status == _SUCCESS) {
			pmlmepriv->to_join = false;
		} else {
			if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE)) {
				/* switch to ADHOC_MASTER */
				status = rtw_do_join_adhoc(padapter);
				if (status != _SUCCESS)
					goto release_mlme_lock;
			} else {
				/* can't associate ; reset under-linking */
				_clr_fwstate_(pmlmepriv, _FW_UNDER_LINKING);
				status = _FAIL;
				pmlmepriv->to_join = false;
			}
		}
	}
release_mlme_lock:
	spin_unlock_bh(&pmlmepriv->lock);

exit:
	RT_TRACE(_module_rtl871x_ioctl_set_c_, _drv_err_,
		 ("-%s: status =%d\n", __func__, status));

	return status;
}

static int cfg80211_rtw_connect(struct wiphy *wiphy, struct net_device *ndev,
				struct cfg80211_connect_params *sme)
{
	int ret = 0;
	struct list_head *phead, *plist, *ptmp;
	struct wlan_network *pnetwork = NULL;
	/* u8 matched_by_bssid = false; */
	/* u8 matched_by_ssid = false; */
	u8 matched = false;
	struct rtw_adapter *padapter = wiphy_to_adapter(wiphy);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	struct rtw_queue *queue = &pmlmepriv->scanned_queue;

	DBG_8723A("=>" "%s(%s)\n", __func__, ndev->name);
	DBG_8723A("privacy =%d, key =%p, key_len =%d, key_idx =%d\n",
		  sme->privacy, sme->key, sme->key_len, sme->key_idx);

	if (_FAIL == rtw_pwr_wakeup(padapter)) {
		ret = -EPERM;
		goto exit;
	}

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE)) {
		ret = -EPERM;
		goto exit;
	}

	if (!sme->ssid || !sme->ssid_len ||
	    sme->ssid_len > IEEE80211_MAX_SSID_LEN) {
		ret = -EINVAL;
		goto exit;
	}

	DBG_8723A("ssid =%s, len =%zu\n", sme->ssid, sme->ssid_len);

	if (sme->bssid)
		DBG_8723A("bssid =" MAC_FMT "\n", MAC_ARG(sme->bssid));

	if (check_fwstate(pmlmepriv, _FW_UNDER_LINKING)) {
		ret = -EBUSY;
		DBG_8723A("%s, fw_state = 0x%x, goto exit\n", __func__,
			  pmlmepriv->fw_state);
		goto exit;
	}
	if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY)) {
		rtw_scan_abort23a(padapter);
	}

	spin_lock_bh(&queue->lock);

	phead = get_list_head(queue);

	list_for_each_safe(plist, ptmp, phead) {
		pnetwork = container_of(plist, struct wlan_network, list);

		if (sme->bssid) {
			if (!ether_addr_equal(pnetwork->network.MacAddress,
					      sme->bssid))
				continue;
		}

		if (sme->ssid && sme->ssid_len) {
			if (pnetwork->network.Ssid.ssid_len != sme->ssid_len ||
			    memcmp(pnetwork->network.Ssid.ssid, sme->ssid,
				   sme->ssid_len))
				continue;
		}

		if (sme->bssid) {
			if (ether_addr_equal(pnetwork->network.MacAddress,
					     sme->bssid)) {
				DBG_8723A("matched by bssid\n");

				matched = true;
				break;
			}
		} else if (sme->ssid && sme->ssid_len) {
			if (!memcmp(pnetwork->network.Ssid.ssid,
				    sme->ssid, sme->ssid_len) &&
			    pnetwork->network.Ssid.ssid_len == sme->ssid_len) {
				DBG_8723A("matched by ssid\n");

				matched = true;
				break;
			}
		}
	}

	spin_unlock_bh(&queue->lock);

	if (!matched || !pnetwork) {
		ret = -ENOENT;
		DBG_8723A("connect, matched == false, goto exit\n");
		goto exit;
	}

	if (cfg80211_infrastructure_mode(
		    padapter, pnetwork->network.ifmode) != _SUCCESS) {
		ret = -EPERM;
		goto exit;
	}

	psecuritypriv->ndisencryptstatus = Ndis802_11EncryptionDisabled;
	psecuritypriv->dot11PrivacyAlgrthm = 0;
	psecuritypriv->dot118021XGrpPrivacy = 0;
	psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_Open;
	psecuritypriv->ndisauthtype = Ndis802_11AuthModeOpen;

	ret = rtw_cfg80211_set_wpa_version(psecuritypriv,
					   sme->crypto.wpa_versions);
	if (ret < 0)
		goto exit;

	ret = rtw_cfg80211_set_auth_type(psecuritypriv, sme->auth_type);

	if (ret < 0)
		goto exit;

	DBG_8723A("%s, ie_len =%zu\n", __func__, sme->ie_len);

	ret = rtw_cfg80211_set_wpa_ie(padapter, sme->ie, sme->ie_len);
	if (ret < 0)
		goto exit;

	if (sme->crypto.n_ciphers_pairwise) {
		ret = rtw_cfg80211_set_cipher(psecuritypriv,
					      sme->crypto.ciphers_pairwise[0],
					      true);
		if (ret < 0)
			goto exit;
	}

	/* For WEP Shared auth */
	if ((psecuritypriv->dot11AuthAlgrthm == dot11AuthAlgrthm_Shared ||
	     psecuritypriv->dot11AuthAlgrthm == dot11AuthAlgrthm_Auto) &&
	    sme->key) {
		struct rtw_wep_key wep_key;
		u8 wep_key_idx, wep_key_len;
		DBG_8723A("%s(): Shared/Auto WEP\n", __func__);

		wep_key_idx = sme->key_idx;
		wep_key_len = sme->key_len;

		if (wep_key_idx > WEP_KEYS || !wep_key_len ||
		    wep_key_len > WLAN_KEY_LEN_WEP104) {
			ret = -EINVAL;
			goto exit;
		}

		wep_key_len = wep_key_len <= 5 ? 5 : 13;

		memset(&wep_key, 0, sizeof(struct rtw_wep_key));

		wep_key.keylen = wep_key_len;

		if (wep_key_len == 13) {
			padapter->securitypriv.dot11PrivacyAlgrthm =
				WLAN_CIPHER_SUITE_WEP104;
			padapter->securitypriv.dot118021XGrpPrivacy =
				WLAN_CIPHER_SUITE_WEP104;
		} else {
			padapter->securitypriv.dot11PrivacyAlgrthm =
				WLAN_CIPHER_SUITE_WEP40;
			padapter->securitypriv.dot118021XGrpPrivacy =
				WLAN_CIPHER_SUITE_WEP40;
		}

		memcpy(wep_key.key, (void *)sme->key, wep_key.keylen);

		if (rtw_cfg80211_add_wep(padapter, &wep_key, wep_key_idx) !=
		    _SUCCESS)
			ret = -EOPNOTSUPP;

		if (ret < 0)
			goto exit;
	}

	ret = rtw_cfg80211_set_cipher(psecuritypriv,
				      sme->crypto.cipher_group, false);
	if (ret < 0)
		goto exit;

	if (sme->crypto.n_akm_suites) {
		ret = rtw_cfg80211_set_key_mgt(psecuritypriv,
					       sme->crypto.akm_suites[0]);
		if (ret < 0)
			goto exit;
	}

	if (psecuritypriv->ndisauthtype > 3)
		psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_8021X;

	if (rtw_set_auth23a(padapter, psecuritypriv) != _SUCCESS) {
		ret = -EBUSY;
		goto exit;
	}

	/* rtw_set_802_11_encryption_mode(padapter,
	   padapter->securitypriv.ndisencryptstatus); */

	if (rtw_set_ssid(padapter, pnetwork) != _SUCCESS) {
		ret = -EBUSY;
		goto exit;
	}

	DBG_8723A("set ssid:dot11AuthAlgrthm =%d, dot11PrivacyAlgrthm =%d, "
		  "dot118021XGrpPrivacy =%d\n", psecuritypriv->dot11AuthAlgrthm,
		  psecuritypriv->dot11PrivacyAlgrthm,
		  psecuritypriv->dot118021XGrpPrivacy);

exit:

	DBG_8723A("<=%s, ret %d\n", __func__, ret);

	return ret;
}

static int cfg80211_rtw_disconnect(struct wiphy *wiphy, struct net_device *ndev,
				   u16 reason_code)
{
	struct rtw_adapter *padapter = wiphy_to_adapter(wiphy);

	DBG_8723A("%s(%s)\n", __func__, ndev->name);

	rtw_set_roaming(padapter, 0);

	if (check_fwstate(&padapter->mlmepriv, _FW_LINKED)) {
		rtw_scan_abort23a(padapter);
		LeaveAllPowerSaveMode23a(padapter);
		rtw_disassoc_cmd23a(padapter, 500, false);

		DBG_8723A("%s...call rtw_indicate_disconnect23a\n", __func__);

		padapter->mlmepriv.not_indic_disco = true;
		rtw_indicate_disconnect23a(padapter);
		padapter->mlmepriv.not_indic_disco = false;

		rtw_free_assoc_resources23a(padapter, 1);
	}

	return 0;
}

static int cfg80211_rtw_set_txpower(struct wiphy *wiphy,
				    struct wireless_dev *wdev,
				    enum nl80211_tx_power_setting type, int mbm)
{
	DBG_8723A("%s\n", __func__);
	return 0;
}

static int cfg80211_rtw_get_txpower(struct wiphy *wiphy,
				    struct wireless_dev *wdev, int *dbm)
{
	DBG_8723A("%s\n", __func__);
	*dbm = (12);
	return 0;
}

inline bool rtw_cfg80211_pwr_mgmt(struct rtw_adapter *adapter)
{
	struct rtw_wdev_priv *rtw_wdev_priv = wdev_to_priv(adapter->rtw_wdev);
	return rtw_wdev_priv->power_mgmt;
}

static int cfg80211_rtw_set_power_mgmt(struct wiphy *wiphy,
				       struct net_device *ndev,
				       bool enabled, int timeout)
{
	struct rtw_adapter *padapter = wiphy_to_adapter(wiphy);
	struct rtw_wdev_priv *rtw_wdev_priv = wdev_to_priv(padapter->rtw_wdev);

	DBG_8723A("%s(%s): enabled:%u, timeout:%d\n",
		  __func__, ndev->name, enabled, timeout);

	rtw_wdev_priv->power_mgmt = enabled;

	if (!enabled)
		LPS_Leave23a(padapter);

	return 0;
}

static int cfg80211_rtw_set_pmksa(struct wiphy *wiphy,
				  struct net_device *netdev,
				  struct cfg80211_pmksa *pmksa)
{
	u8 index, blInserted = false;
	struct rtw_adapter *padapter = wiphy_to_adapter(wiphy);
	struct security_priv *psecuritypriv = &padapter->securitypriv;

	DBG_8723A("%s(%s)\n", __func__, netdev->name);

	if (is_zero_ether_addr(pmksa->bssid))
		return -EINVAL;

	blInserted = false;

	/* overwrite PMKID */
	for (index = 0; index < NUM_PMKID_CACHE; index++) {
		if (ether_addr_equal(psecuritypriv->PMKIDList[index].Bssid,
				     pmksa->bssid)) {
			/* BSSID is matched, the same AP => rewrite with
			   new PMKID. */
			DBG_8723A("%s(%s):  BSSID exists in the PMKList.\n",
				  __func__, netdev->name);

			memcpy(psecuritypriv->PMKIDList[index].PMKID,
			       pmksa->pmkid, WLAN_PMKID_LEN);
			psecuritypriv->PMKIDList[index].bUsed = true;
			psecuritypriv->PMKIDIndex = index + 1;
			blInserted = true;
			break;
		}
	}

	if (!blInserted) {
		/*  Find a new entry */
		DBG_8723A("%s(%s): Use new entry index = %d for this PMKID\n",
			  __func__, netdev->name, psecuritypriv->PMKIDIndex);

		ether_addr_copy(
			psecuritypriv->PMKIDList[psecuritypriv->PMKIDIndex].
			Bssid, pmksa->bssid);
		memcpy(psecuritypriv->PMKIDList[psecuritypriv->PMKIDIndex].
		       PMKID, pmksa->pmkid, WLAN_PMKID_LEN);

		psecuritypriv->PMKIDList[psecuritypriv->PMKIDIndex].bUsed =
			true;
		psecuritypriv->PMKIDIndex++;
		if (psecuritypriv->PMKIDIndex == 16) {
			psecuritypriv->PMKIDIndex = 0;
		}
	}

	return 0;
}

static int cfg80211_rtw_del_pmksa(struct wiphy *wiphy,
				  struct net_device *netdev,
				  struct cfg80211_pmksa *pmksa)
{
	u8 index, bMatched = false;
	struct rtw_adapter *padapter = wiphy_to_adapter(wiphy);
	struct security_priv *psecuritypriv = &padapter->securitypriv;

	DBG_8723A("%s(%s)\n", __func__, netdev->name);

	for (index = 0; index < NUM_PMKID_CACHE; index++) {
		if (ether_addr_equal(psecuritypriv->PMKIDList[index].Bssid,
				     pmksa->bssid)) {
			/* BSSID is matched, the same AP => Remove this PMKID
			   information and reset it. */
			eth_zero_addr(psecuritypriv->PMKIDList[index].Bssid);
			memset(psecuritypriv->PMKIDList[index].PMKID, 0x00,
			       WLAN_PMKID_LEN);
			psecuritypriv->PMKIDList[index].bUsed = false;
			bMatched = true;
			break;
		}
	}

	if (false == bMatched) {
		DBG_8723A("%s(%s): do not have matched BSSID\n", __func__,
			  netdev->name);
		return -EINVAL;
	}

	return 0;
}

static int cfg80211_rtw_flush_pmksa(struct wiphy *wiphy,
				    struct net_device *netdev)
{
	struct rtw_adapter *padapter = wiphy_to_adapter(wiphy);
	struct security_priv *psecuritypriv = &padapter->securitypriv;

	DBG_8723A("%s(%s)\n", __func__, netdev->name);

	memset(&psecuritypriv->PMKIDList[0], 0x00,
	       sizeof(struct rt_pmkid_list) * NUM_PMKID_CACHE);
	psecuritypriv->PMKIDIndex = 0;

	return 0;
}

#ifdef CONFIG_8723AU_AP_MODE
void rtw_cfg80211_indicate_sta_assoc(struct rtw_adapter *padapter,
				     u8 *pmgmt_frame, uint frame_len)
{
	s32 freq;
	int channel;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct net_device *ndev = padapter->pnetdev;

	DBG_8723A("%s(padapter =%p,%s)\n", __func__, padapter, ndev->name);

#if defined(RTW_USE_CFG80211_STA_EVENT)
	{
		struct station_info sinfo;
		u8 ie_offset;

		if (ieee80211_is_assoc_req(hdr->frame_control))
			ie_offset = offsetof(struct ieee80211_mgmt,
					     u.assoc_req.variable);
		else		/*  WIFI_REASSOCREQ */
			ie_offset = offsetof(struct ieee80211_mgmt,
					     u.reassoc_req.variable);

		sinfo.filled = 0;
		sinfo.filled = STATION_INFO_ASSOC_REQ_IES;
		sinfo.assoc_req_ies = pmgmt_frame + ie_offset;
		sinfo.assoc_req_ies_len = frame_len - ie_offset;
		cfg80211_new_sta(ndev, hdr->addr2, &sinfo, GFP_ATOMIC);
	}
#else /* defined(RTW_USE_CFG80211_STA_EVENT) */
	channel = pmlmeext->cur_channel;
	if (channel <= RTW_CH_MAX_2G_CHANNEL)
		freq = ieee80211_channel_to_frequency(channel,
						      IEEE80211_BAND_2GHZ);
	else
		freq = ieee80211_channel_to_frequency(channel,
						      IEEE80211_BAND_5GHZ);

	cfg80211_rx_mgmt(padapter->rtw_wdev, freq, 0, pmgmt_frame, frame_len,
			 0);
#endif /* defined(RTW_USE_CFG80211_STA_EVENT) */
}

void rtw_cfg80211_indicate_sta_disassoc(struct rtw_adapter *padapter,
					unsigned char *da,
					unsigned short reason)
{
	s32 freq;
	int channel;
	uint frame_len;
	struct ieee80211_mgmt mgmt;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	struct net_device *ndev = padapter->pnetdev;

	DBG_8723A("%s(padapter =%p,%s)\n", __func__, padapter, ndev->name);

	memset(&mgmt, 0, sizeof(struct ieee80211_mgmt));

#if defined(RTW_USE_CFG80211_STA_EVENT)
	cfg80211_del_sta(ndev, da, GFP_ATOMIC);
#else /* defined(RTW_USE_CFG80211_STA_EVENT) */
	channel = pmlmeext->cur_channel;
	if (channel <= RTW_CH_MAX_2G_CHANNEL)
		freq = ieee80211_channel_to_frequency(channel,
						      IEEE80211_BAND_2GHZ);
	else
		freq = ieee80211_channel_to_frequency(channel,
						      IEEE80211_BAND_5GHZ);

	mgmt.frame_control =
		cpu_to_le16(IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_DEAUTH);

	ether_addr_copy(mgmt.da, myid(&padapter->eeprompriv));
	ether_addr_copy(mgmt.sa, da);
	ether_addr_copy(mgmt.bssid, get_my_bssid23a(&pmlmeinfo->network));

	mgmt.seq_ctrl = cpu_to_le16(IEEE80211_SN_TO_SEQ(pmlmeext->mgnt_seq));
	pmlmeext->mgnt_seq++;

	mgmt.u.disassoc.reason_code = cpu_to_le16(reason);

	frame_len = sizeof(struct ieee80211_hdr_3addr) + 2;

	cfg80211_rx_mgmt(padapter->rtw_wdev, freq, 0, (u8 *)&mgmt, frame_len,
			 0);
#endif /* defined(RTW_USE_CFG80211_STA_EVENT) */
}

static int rtw_cfg80211_monitor_if_open(struct net_device *ndev)
{
	int ret = 0;

	DBG_8723A("%s\n", __func__);

	return ret;
}

static int rtw_cfg80211_monitor_if_close(struct net_device *ndev)
{
	int ret = 0;

	DBG_8723A("%s\n", __func__);

	return ret;
}

static int rtw_cfg80211_monitor_if_xmit_entry(struct sk_buff *skb,
					      struct net_device *ndev)
{
	int ret = 0;
	int rtap_len;
	int qos_len = 0;
	int dot11_hdr_len = 24;
	int snap_len = 6;
	unsigned char *pdata;
	unsigned char src_mac_addr[6];
	unsigned char dst_mac_addr[6];
	struct ieee80211_hdr *dot11_hdr;
	struct ieee80211_radiotap_header *rtap_hdr;
	struct rtw_adapter *padapter = netdev_priv(ndev);

	DBG_8723A("%s(%s)\n", __func__, ndev->name);

	if (unlikely(skb->len < sizeof(struct ieee80211_radiotap_header)))
		goto fail;

	rtap_hdr = (struct ieee80211_radiotap_header *)skb->data;
	if (unlikely(rtap_hdr->it_version))
		goto fail;

	rtap_len = ieee80211_get_radiotap_len(skb->data);
	if (unlikely(skb->len < rtap_len))
		goto fail;

	if (rtap_len != 14) {
		DBG_8723A("radiotap len (should be 14): %d\n", rtap_len);
		goto fail;
	}

	/* Skip the ratio tap header */
	skb_pull(skb, rtap_len);

	dot11_hdr = (struct ieee80211_hdr *)skb->data;
	/* Check if the QoS bit is set */
	if (ieee80211_is_data(dot11_hdr->frame_control)) {
		/* Check if this ia a Wireless Distribution System (WDS) frame
		 * which has 4 MAC addresses
		 */
		if (ieee80211_is_data_qos(dot11_hdr->frame_control))
			qos_len = IEEE80211_QOS_CTL_LEN;
		if (ieee80211_has_a4(dot11_hdr->frame_control))
			dot11_hdr_len += 6;

		memcpy(dst_mac_addr, dot11_hdr->addr1, sizeof(dst_mac_addr));
		memcpy(src_mac_addr, dot11_hdr->addr2, sizeof(src_mac_addr));

		/*
		 * Skip the 802.11 header, QoS (if any) and SNAP,
		 * but leave spaces for two MAC addresses
		 */
		skb_pull(skb, dot11_hdr_len + qos_len + snap_len -
			 ETH_ALEN * 2);
		pdata = (unsigned char *)skb->data;
		ether_addr_copy(pdata, dst_mac_addr);
		ether_addr_copy(pdata + ETH_ALEN, src_mac_addr);

		DBG_8723A("should be eapol packet\n");

		/* Use the real net device to transmit the packet */
		ret = rtw_xmit23a_entry23a(skb, padapter->pnetdev);

		return ret;

	} else if (ieee80211_is_action(dot11_hdr->frame_control)) {
		struct ieee80211_mgmt *mgmt;
		/* only for action frames */
		struct xmit_frame *pmgntframe;
		struct pkt_attrib *pattrib;
		unsigned char *pframe;
		/* u8 category, action, OUI_Subtype, dialogToken = 0; */
		/* unsigned char        *frame_body; */
		struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
		struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
		u32 len = skb->len;
		u8 category, action;

		mgmt = (struct ieee80211_mgmt *)dot11_hdr;

		DBG_8723A("RTW_Tx:da =" MAC_FMT " via %s(%s)\n",
			  MAC_ARG(mgmt->da), __func__, ndev->name);
		category = mgmt->u.action.category;
		action = mgmt->u.action.u.wme_action.action_code;
		DBG_8723A("RTW_Tx:category(%u), action(%u)\n",
			  category, action);

		/* starting alloc mgmt frame to dump it */
		pmgntframe = alloc_mgtxmitframe23a(pxmitpriv);
		if (pmgntframe == NULL)
			goto fail;

		/* update attribute */
		pattrib = &pmgntframe->attrib;
		update_mgntframe_attrib23a(padapter, pattrib);
		pattrib->retry_ctrl = false;

		memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

		pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;

		memcpy(pframe, skb->data, len);
		pattrib->pktlen = len;

		/* update seq number */
		pmlmeext->mgnt_seq = le16_to_cpu(dot11_hdr->seq_ctrl) >> 4;
		pattrib->seqnum = pmlmeext->mgnt_seq;
		pmlmeext->mgnt_seq++;

		pattrib->last_txcmdsz = pattrib->pktlen;

		dump_mgntframe23a(padapter, pmgntframe);
	}

fail:

	dev_kfree_skb(skb);

	return 0;
}

static int
rtw_cfg80211_monitor_if_set_mac_address(struct net_device *ndev, void *addr)
{
	int ret = 0;

	DBG_8723A("%s\n", __func__);

	return ret;
}

static const struct net_device_ops rtw_cfg80211_monitor_if_ops = {
	.ndo_open = rtw_cfg80211_monitor_if_open,
	.ndo_stop = rtw_cfg80211_monitor_if_close,
	.ndo_start_xmit = rtw_cfg80211_monitor_if_xmit_entry,
	.ndo_set_mac_address = rtw_cfg80211_monitor_if_set_mac_address,
};

static int rtw_cfg80211_add_monitor_if(struct rtw_adapter *padapter, char *name,
				       struct net_device **ndev)
{
	int ret = 0;
	struct net_device *mon_ndev = NULL;
	struct wireless_dev *mon_wdev = NULL;
	struct rtw_wdev_priv *pwdev_priv = wdev_to_priv(padapter->rtw_wdev);

	if (!name) {
		DBG_8723A("%s(%s): without specific name\n",
			  __func__, padapter->pnetdev->name);
		ret = -EINVAL;
		goto out;
	}

	if (pwdev_priv->pmon_ndev) {
		DBG_8723A("%s(%s): monitor interface exist: %s\n", __func__,
			  padapter->pnetdev->name, pwdev_priv->pmon_ndev->name);
		ret = -EBUSY;
		goto out;
	}

	mon_ndev = alloc_etherdev(sizeof(struct rtw_adapter));
	if (!mon_ndev) {
		DBG_8723A("%s(%s): allocate ndev fail\n", __func__,
			  padapter->pnetdev->name);
		ret = -ENOMEM;
		goto out;
	}

	mon_ndev->type = ARPHRD_IEEE80211_RADIOTAP;
	strncpy(mon_ndev->name, name, IFNAMSIZ);
	mon_ndev->name[IFNAMSIZ - 1] = 0;
	mon_ndev->destructor = rtw_ndev_destructor;

	mon_ndev->netdev_ops = &rtw_cfg80211_monitor_if_ops;

	/*  wdev */
	mon_wdev = kzalloc(sizeof(struct wireless_dev), GFP_KERNEL);
	if (!mon_wdev) {
		DBG_8723A("%s(%s): allocate mon_wdev fail\n", __func__,
			  padapter->pnetdev->name);
		ret = -ENOMEM;
		goto out;
	}

	mon_wdev->wiphy = padapter->rtw_wdev->wiphy;
	mon_wdev->netdev = mon_ndev;
	mon_wdev->iftype = NL80211_IFTYPE_MONITOR;
	mon_ndev->ieee80211_ptr = mon_wdev;

	ret = register_netdevice(mon_ndev);
	if (ret) {
		goto out;
	}

	*ndev = pwdev_priv->pmon_ndev = mon_ndev;
	memcpy(pwdev_priv->ifname_mon, name, IFNAMSIZ + 1);

out:
	if (ret) {
		kfree(mon_wdev);
		mon_wdev = NULL;
	}

	if (ret && mon_ndev) {
		free_netdev(mon_ndev);
		*ndev = mon_ndev = NULL;
	}

	return ret;
}

static struct wireless_dev *
cfg80211_rtw_add_virtual_intf(struct wiphy *wiphy, const char *name,
			      enum nl80211_iftype type, u32 *flags,
			      struct vif_params *params)
{
	int ret = 0;
	struct net_device *ndev = NULL;
	struct rtw_adapter *padapter = wiphy_to_adapter(wiphy);

	DBG_8723A("%s(%s): wiphy:%s, name:%s, type:%d\n", __func__,
		  padapter->pnetdev->name, wiphy_name(wiphy), name, type);

	switch (type) {
	case NL80211_IFTYPE_ADHOC:
	case NL80211_IFTYPE_AP_VLAN:
	case NL80211_IFTYPE_WDS:
	case NL80211_IFTYPE_MESH_POINT:
		ret = -ENODEV;
		break;
	case NL80211_IFTYPE_MONITOR:
		ret =
		    rtw_cfg80211_add_monitor_if(padapter, (char *)name, &ndev);
		break;

	case NL80211_IFTYPE_P2P_CLIENT:
	case NL80211_IFTYPE_STATION:
		ret = -ENODEV;
		break;

	case NL80211_IFTYPE_P2P_GO:
	case NL80211_IFTYPE_AP:
		ret = -ENODEV;
		break;
	default:
		ret = -ENODEV;
		DBG_8723A("Unsupported interface type\n");
		break;
	}

	DBG_8723A("%s(%s): ndev:%p, ret:%d\n", __func__,
		  padapter->pnetdev->name,
		  ndev, ret);

	return ndev ? ndev->ieee80211_ptr : ERR_PTR(ret);
}

static int cfg80211_rtw_del_virtual_intf(struct wiphy *wiphy,
					 struct wireless_dev *wdev)
{
	struct rtw_wdev_priv *pwdev_priv =
	    (struct rtw_wdev_priv *)wiphy_priv(wiphy);
	struct net_device *ndev;
	ndev = wdev ? wdev->netdev : NULL;

	if (!ndev)
		goto exit;

	unregister_netdevice(ndev);

	if (ndev == pwdev_priv->pmon_ndev) {
		pwdev_priv->pmon_ndev = NULL;
		pwdev_priv->ifname_mon[0] = '\0';
		DBG_8723A("%s(%s): remove monitor interface\n",
			  __func__, ndev->name);
	}

exit:
	return 0;
}

static int rtw_add_beacon(struct rtw_adapter *adapter, const u8 *head,
			  size_t head_len, const u8 *tail, size_t tail_len)
{
	int ret = 0;
	u8 *pbuf;
	uint len, ielen, wps_ielen = 0;
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	struct wlan_bssid_ex *bss = &pmlmepriv->cur_network.network;
	const struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)head;
	struct ieee80211_mgmt *tmpmgmt;
	/* struct sta_priv *pstapriv = &padapter->stapriv; */

	DBG_8723A("%s beacon_head_len =%zu, beacon_tail_len =%zu\n",
		  __func__, head_len, tail_len);

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE) != true)
		return -EINVAL;

	if (head_len < offsetof(struct ieee80211_mgmt, u.beacon.variable))
		return -EINVAL;

	pbuf = kzalloc(head_len + tail_len, GFP_KERNEL);
	if (!pbuf)
		return -ENOMEM;
	tmpmgmt = (struct ieee80211_mgmt *)pbuf;

	bss->beacon_interval = get_unaligned_le16(&mgmt->u.beacon.beacon_int);
	bss->capability = get_unaligned_le16(&mgmt->u.beacon.capab_info);
	bss->tsf = get_unaligned_le64(&mgmt->u.beacon.timestamp);

	/*  24 = beacon header len. */
	memcpy(pbuf, (void *)head, head_len);
	memcpy(pbuf + head_len, (void *)tail, tail_len);

	len = head_len + tail_len;
	ielen = len - offsetof(struct ieee80211_mgmt, u.beacon.variable);
	/* check wps ie if inclued */
	if (cfg80211_find_vendor_ie(WLAN_OUI_MICROSOFT,
				    WLAN_OUI_TYPE_MICROSOFT_WPS,
				    tmpmgmt->u.beacon.variable, ielen))
		DBG_8723A("add bcn, wps_ielen =%d\n", wps_ielen);

	/* pbss_network->IEs will not include p2p_ie, wfd ie */
	rtw_ies_remove_ie23a(tmpmgmt->u.beacon.variable, &ielen, 0,
			     WLAN_EID_VENDOR_SPECIFIC, P2P_OUI23A, 4);
	rtw_ies_remove_ie23a(tmpmgmt->u.beacon.variable, &ielen, 0,
			     WLAN_EID_VENDOR_SPECIFIC, WFD_OUI23A, 4);

	len = ielen + offsetof(struct ieee80211_mgmt, u.beacon.variable);
	if (rtw_check_beacon_data23a(adapter, tmpmgmt, len) == _SUCCESS) {
		ret = 0;
	} else {
		ret = -EINVAL;
	}

	kfree(pbuf);

	return ret;
}

static int cfg80211_rtw_start_ap(struct wiphy *wiphy, struct net_device *ndev,
				 struct cfg80211_ap_settings *settings)
{
	int ret = 0;
	struct rtw_adapter *adapter = wiphy_to_adapter(wiphy);

	DBG_8723A("%s(%s): hidden_ssid:%d, auth_type:%d\n",
		  __func__, ndev->name, settings->hidden_ssid,
		  settings->auth_type);

	ret = rtw_add_beacon(adapter, settings->beacon.head,
			     settings->beacon.head_len, settings->beacon.tail,
			     settings->beacon.tail_len);

	adapter->mlmeextpriv.mlmext_info.hidden_ssid_mode =
		settings->hidden_ssid;

	if (settings->ssid && settings->ssid_len) {
		struct wlan_bssid_ex *pbss_network =
			&adapter->mlmepriv.cur_network.network;
		struct wlan_bssid_ex *pbss_network_ext =
			&adapter->mlmeextpriv.mlmext_info.network;

		memcpy(pbss_network->Ssid.ssid, (void *)settings->ssid,
		       settings->ssid_len);
		pbss_network->Ssid.ssid_len = settings->ssid_len;
		memcpy(pbss_network_ext->Ssid.ssid, (void *)settings->ssid,
		       settings->ssid_len);
		pbss_network_ext->Ssid.ssid_len = settings->ssid_len;
	}

	return ret;
}

static int cfg80211_rtw_change_beacon(struct wiphy *wiphy,
				      struct net_device *ndev,
				      struct cfg80211_beacon_data *info)
{
	int ret = 0;
	struct rtw_adapter *adapter = wiphy_to_adapter(wiphy);

	DBG_8723A("%s(%s)\n", __func__, ndev->name);

	ret = rtw_add_beacon(adapter, info->head, info->head_len, info->tail,
			     info->tail_len);

	return ret;
}

static int cfg80211_rtw_stop_ap(struct wiphy *wiphy, struct net_device *ndev)
{
	DBG_8723A("%s(%s)\n", __func__, ndev->name);
	return 0;
}

static int cfg80211_rtw_add_station(struct wiphy *wiphy,
				    struct net_device *ndev, const u8 *mac,
				    struct station_parameters *params)
{
	DBG_8723A("%s(%s)\n", __func__, ndev->name);

	return 0;
}

static int cfg80211_rtw_del_station(struct wiphy *wiphy,
				    struct net_device *ndev,
				    struct station_del_parameters *params)
{
	const u8 *mac = params->mac;
	int ret = 0;
	struct list_head *phead, *plist, *ptmp;
	u8 updated = 0;
	struct sta_info *psta;
	struct rtw_adapter *padapter = netdev_priv(ndev);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct sta_priv *pstapriv = &padapter->stapriv;

	DBG_8723A("+%s(%s)\n", __func__, ndev->name);

	if (check_fwstate(pmlmepriv, (_FW_LINKED | WIFI_AP_STATE)) != true) {
		DBG_8723A("%s, fw_state != FW_LINKED|WIFI_AP_STATE\n",
			  __func__);
		return -EINVAL;
	}

	if (!mac) {
		DBG_8723A("flush all sta, and cam_entry\n");

		flush_all_cam_entry23a(padapter);	/* clear CAM */

		ret = rtw_sta_flush23a(padapter);

		return ret;
	}

	DBG_8723A("free sta macaddr =" MAC_FMT "\n", MAC_ARG(mac));

	if (is_broadcast_ether_addr(mac))
		return -EINVAL;

	spin_lock_bh(&pstapriv->asoc_list_lock);

	phead = &pstapriv->asoc_list;

	/* check asoc_queue */
	list_for_each_safe(plist, ptmp, phead) {
		psta = container_of(plist, struct sta_info, asoc_list);

		if (ether_addr_equal(mac, psta->hwaddr)) {
			if (psta->dot8021xalg == 1 &&
			    psta->bpairwise_key_installed == false) {
				DBG_8723A("%s, sta's dot8021xalg = 1 and "
					  "key_installed = false\n", __func__);
			} else {
				DBG_8723A("free psta =%p, aid =%d\n", psta,
					  psta->aid);

				list_del_init(&psta->asoc_list);
				pstapriv->asoc_list_cnt--;

				/* spin_unlock_bh(&pstapriv->asoc_list_lock); */
				updated =
				    ap_free_sta23a(padapter, psta, true,
						WLAN_REASON_DEAUTH_LEAVING);
				/* spin_lock_bh(&pstapriv->asoc_list_lock); */

				psta = NULL;

				break;
			}
		}
	}

	spin_unlock_bh(&pstapriv->asoc_list_lock);

	associated_clients_update23a(padapter, updated);

	DBG_8723A("-%s(%s)\n", __func__, ndev->name);

	return ret;
}

static int cfg80211_rtw_change_station(struct wiphy *wiphy,
				       struct net_device *ndev, const u8 *mac,
				       struct station_parameters *params)
{
	DBG_8723A("%s(%s)\n", __func__, ndev->name);
	return 0;
}

static int cfg80211_rtw_dump_station(struct wiphy *wiphy,
				     struct net_device *ndev, int idx, u8 *mac,
				     struct station_info *sinfo)
{
	DBG_8723A("%s(%s)\n", __func__, ndev->name);

	/* TODO: dump scanned queue */

	return -ENOENT;
}

static int cfg80211_rtw_change_bss(struct wiphy *wiphy, struct net_device *ndev,
				   struct bss_parameters *params)
{
	DBG_8723A("%s(%s)\n", __func__, ndev->name);
	return 0;
}
#endif /* CONFIG_8723AU_AP_MODE */

static int _cfg80211_rtw_mgmt_tx(struct rtw_adapter *padapter, u8 tx_ch,
				 const u8 *buf, size_t len)
{
	struct xmit_frame *pmgntframe;
	struct pkt_attrib *pattrib;
	unsigned char *pframe;
	int ret = _FAIL;
	struct ieee80211_hdr *pwlanhdr;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;

	if (_FAIL == rtw_pwr_wakeup(padapter)) {
		ret = -EFAULT;
		goto exit;
	}

	rtw_set_scan_deny(padapter, 1000);

	rtw_scan_abort23a(padapter);

	if (tx_ch != rtw_get_oper_ch23a(padapter)) {
		if (!check_fwstate(&padapter->mlmepriv, _FW_LINKED))
			pmlmeext->cur_channel = tx_ch;
		set_channel_bwmode23a(padapter, tx_ch,
				   HAL_PRIME_CHNL_OFFSET_DONT_CARE,
				   HT_CHANNEL_WIDTH_20);
	}

	/* starting alloc mgmt frame to dump it */
	pmgntframe = alloc_mgtxmitframe23a(pxmitpriv);
	if (!pmgntframe) {
		/* ret = -ENOMEM; */
		ret = _FAIL;
		goto exit;
	}

	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib23a(padapter, pattrib);
	pattrib->retry_ctrl = false;

	memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *) (pmgntframe->buf_addr) + TXDESC_OFFSET;

	memcpy(pframe, (void *)buf, len);
	pattrib->pktlen = len;

	pwlanhdr = (struct ieee80211_hdr *)pframe;
	/* update seq number */
	pmlmeext->mgnt_seq = le16_to_cpu(pwlanhdr->seq_ctrl) >> 4;
	pattrib->seqnum = pmlmeext->mgnt_seq;
	pmlmeext->mgnt_seq++;

	pattrib->last_txcmdsz = pattrib->pktlen;

	ret = dump_mgntframe23a_and_wait_ack23a(padapter, pmgntframe);

	if (ret  != _SUCCESS)
		DBG_8723A("%s, ack == false\n", __func__);
	else
		DBG_8723A("%s, ack == true\n", __func__);

exit:

	DBG_8723A("%s, ret =%d\n", __func__, ret);

	return ret;
}

static int cfg80211_rtw_mgmt_tx(struct wiphy *wiphy, struct wireless_dev *wdev,
				struct cfg80211_mgmt_tx_params *params,
				u64 *cookie)
{
	struct rtw_adapter *padapter =
		(struct rtw_adapter *)wiphy_to_adapter(wiphy);
	int ret = 0;
	int tx_ret;
	u32 dump_limit = RTW_MAX_MGMT_TX_CNT;
	u32 dump_cnt = 0;
	bool ack = true;
	u8 category, action;
	unsigned long start = jiffies;
	size_t len = params->len;
	struct ieee80211_channel *chan = params->chan;
	const u8 *buf = params->buf;
	struct ieee80211_mgmt *hdr = (struct ieee80211_mgmt *)buf;
	u8 tx_ch = (u8) ieee80211_frequency_to_channel(chan->center_freq);

	if (!ieee80211_is_action(hdr->frame_control))
		return -EINVAL;

	/* cookie generation */
	*cookie = (unsigned long)buf;

	DBG_8723A("%s(%s): len =%zu, ch =%d\n", __func__,
		  padapter->pnetdev->name, len, tx_ch);

	/* indicate ack before issue frame to avoid racing with rsp frame */
	cfg80211_mgmt_tx_status(padapter->rtw_wdev, *cookie, buf, len, ack,
				GFP_KERNEL);

	DBG_8723A("RTW_Tx:tx_ch =%d, da =" MAC_FMT "\n", tx_ch,
		  MAC_ARG(hdr->da));
	category = hdr->u.action.category;
	action = hdr->u.action.u.wme_action.action_code;
	DBG_8723A("RTW_Tx:category(%u), action(%u)\n", category, action);

	do {
		dump_cnt++;
		tx_ret = _cfg80211_rtw_mgmt_tx(padapter, tx_ch, buf, len);
	} while (dump_cnt < dump_limit && tx_ret != _SUCCESS);

	if (tx_ret != _SUCCESS || dump_cnt > 1) {
		DBG_8723A("%s(%s): %s (%d/%d) in %d ms\n",
			  __func__, padapter->pnetdev->name,
			  tx_ret == _SUCCESS ? "OK" : "FAIL", dump_cnt,
			  dump_limit, jiffies_to_msecs(jiffies - start));
	}

	return ret;
}

static void cfg80211_rtw_mgmt_frame_register(struct wiphy *wiphy,
					     struct wireless_dev *wdev,
					     u16 frame_type, bool reg)
{
	if (frame_type != (IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_PROBE_REQ))
		return;

	return;
}

static struct cfg80211_ops rtw_cfg80211_ops = {
	.change_virtual_intf = cfg80211_rtw_change_iface,
	.add_key = cfg80211_rtw_add_key,
	.get_key = cfg80211_rtw_get_key,
	.del_key = cfg80211_rtw_del_key,
	.set_default_key = cfg80211_rtw_set_default_key,
	.get_station = cfg80211_rtw_get_station,
	.scan = cfg80211_rtw_scan,
	.set_wiphy_params = cfg80211_rtw_set_wiphy_params,
	.connect = cfg80211_rtw_connect,
	.disconnect = cfg80211_rtw_disconnect,
	.join_ibss = cfg80211_rtw_join_ibss,
	.leave_ibss = cfg80211_rtw_leave_ibss,
	.set_tx_power = cfg80211_rtw_set_txpower,
	.get_tx_power = cfg80211_rtw_get_txpower,
	.set_power_mgmt = cfg80211_rtw_set_power_mgmt,
	.set_pmksa = cfg80211_rtw_set_pmksa,
	.del_pmksa = cfg80211_rtw_del_pmksa,
	.flush_pmksa = cfg80211_rtw_flush_pmksa,

#ifdef CONFIG_8723AU_AP_MODE
	.add_virtual_intf = cfg80211_rtw_add_virtual_intf,
	.del_virtual_intf = cfg80211_rtw_del_virtual_intf,

	.start_ap = cfg80211_rtw_start_ap,
	.change_beacon = cfg80211_rtw_change_beacon,
	.stop_ap = cfg80211_rtw_stop_ap,

	.add_station = cfg80211_rtw_add_station,
	.del_station = cfg80211_rtw_del_station,
	.change_station = cfg80211_rtw_change_station,
	.dump_station = cfg80211_rtw_dump_station,
	.change_bss = cfg80211_rtw_change_bss,
#endif /* CONFIG_8723AU_AP_MODE */

	.mgmt_tx = cfg80211_rtw_mgmt_tx,
	.mgmt_frame_register = cfg80211_rtw_mgmt_frame_register,
};

static void rtw_cfg80211_init_ht_capab(struct ieee80211_sta_ht_cap *ht_cap,
				       enum ieee80211_band band, u8 rf_type)
{

#define MAX_BIT_RATE_40MHZ_MCS15	300	/* Mbps */
#define MAX_BIT_RATE_40MHZ_MCS7		150	/* Mbps */

	ht_cap->ht_supported = true;

	ht_cap->cap = IEEE80211_HT_CAP_SUP_WIDTH_20_40 |
	    IEEE80211_HT_CAP_SGI_40 | IEEE80211_HT_CAP_SGI_20 |
	    IEEE80211_HT_CAP_DSSSCCK40 | IEEE80211_HT_CAP_MAX_AMSDU;

	/*
	 *Maximum length of AMPDU that the STA can receive.
	 *Length = 2 ^ (13 + max_ampdu_length_exp) - 1 (octets)
	 */
	ht_cap->ampdu_factor = IEEE80211_HT_MAX_AMPDU_64K;

	/*Minimum MPDU start spacing , */
	ht_cap->ampdu_density = IEEE80211_HT_MPDU_DENSITY_16;

	ht_cap->mcs.tx_params = IEEE80211_HT_MCS_TX_DEFINED;

	/*
	 *hw->wiphy->bands[IEEE80211_BAND_2GHZ]
	 *base on ant_num
	 *rx_mask: RX mask
	 *if rx_ant = 1 rx_mask[0]= 0xff;==>MCS0-MCS7
	 *if rx_ant = 2 rx_mask[1]= 0xff;==>MCS8-MCS15
	 *if rx_ant >= 3 rx_mask[2]= 0xff;
	 *if BW_40 rx_mask[4]= 0x01;
	 *highest supported RX rate
	 */
	if (rf_type == RF_1T1R) {
		ht_cap->mcs.rx_mask[0] = 0xFF;
		ht_cap->mcs.rx_mask[1] = 0x00;
		ht_cap->mcs.rx_mask[4] = 0x01;

		ht_cap->mcs.rx_highest = cpu_to_le16(MAX_BIT_RATE_40MHZ_MCS7);
	} else if ((rf_type == RF_1T2R) || (rf_type == RF_2T2R)) {
		ht_cap->mcs.rx_mask[0] = 0xFF;
		ht_cap->mcs.rx_mask[1] = 0xFF;
		ht_cap->mcs.rx_mask[4] = 0x01;

		ht_cap->mcs.rx_highest = cpu_to_le16(MAX_BIT_RATE_40MHZ_MCS15);
	} else {
		DBG_8723A("%s, error rf_type =%d\n", __func__, rf_type);
	}

}

void rtw_cfg80211_init_wiphy(struct rtw_adapter *padapter)
{
	u8 rf_type;
	struct ieee80211_supported_band *bands;
	struct wireless_dev *pwdev = padapter->rtw_wdev;
	struct wiphy *wiphy = pwdev->wiphy;

	rf_type = rtl8723a_get_rf_type(padapter);

	DBG_8723A("%s:rf_type =%d\n", __func__, rf_type);

	/* if (padapter->registrypriv.wireless_mode & WIRELESS_11G) */
	{
		bands = wiphy->bands[IEEE80211_BAND_2GHZ];
		if (bands)
			rtw_cfg80211_init_ht_capab(&bands->ht_cap,
						   IEEE80211_BAND_2GHZ,
						   rf_type);
	}

	/* if (padapter->registrypriv.wireless_mode & WIRELESS_11A) */
	{
		bands = wiphy->bands[IEEE80211_BAND_5GHZ];
		if (bands)
			rtw_cfg80211_init_ht_capab(&bands->ht_cap,
						   IEEE80211_BAND_5GHZ,
						   rf_type);
	}
}

static void rtw_cfg80211_preinit_wiphy(struct rtw_adapter *padapter,
				       struct wiphy *wiphy)
{
	wiphy->signal_type = CFG80211_SIGNAL_TYPE_MBM;

	wiphy->max_scan_ssids = RTW_SSID_SCAN_AMOUNT;
	wiphy->max_scan_ie_len = IEEE80211_MAX_DATA_LEN;
	wiphy->max_num_pmkids = RTW_MAX_NUM_PMKIDS;

	wiphy->max_remain_on_channel_duration =
	    RTW_MAX_REMAIN_ON_CHANNEL_DURATION;

	wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION) |
	    BIT(NL80211_IFTYPE_ADHOC) |
#ifdef CONFIG_8723AU_AP_MODE
	    BIT(NL80211_IFTYPE_AP) | BIT(NL80211_IFTYPE_MONITOR) |
#endif
	    0;

#ifdef CONFIG_8723AU_AP_MODE
	wiphy->mgmt_stypes = rtw_cfg80211_default_mgmt_stypes;
#endif /* CONFIG_8723AU_AP_MODE */

	wiphy->software_iftypes |= BIT(NL80211_IFTYPE_MONITOR);

	/*
	   wiphy->iface_combinations = &rtw_combinations;
	   wiphy->n_iface_combinations = 1;
	 */

	wiphy->cipher_suites = rtw_cipher_suites;
	wiphy->n_cipher_suites = ARRAY_SIZE(rtw_cipher_suites);

	/* if (padapter->registrypriv.wireless_mode & WIRELESS_11G) */
	wiphy->bands[IEEE80211_BAND_2GHZ] =
	    rtw_spt_band_alloc(IEEE80211_BAND_2GHZ);
	/* if (padapter->registrypriv.wireless_mode & WIRELESS_11A) */
	wiphy->bands[IEEE80211_BAND_5GHZ] =
	    rtw_spt_band_alloc(IEEE80211_BAND_5GHZ);

	wiphy->flags |= WIPHY_FLAG_HAS_REMAIN_ON_CHANNEL;
	wiphy->flags |= WIPHY_FLAG_OFFCHAN_TX | WIPHY_FLAG_HAVE_AP_SME;

	if (padapter->registrypriv.power_mgnt != PS_MODE_ACTIVE)
		wiphy->flags |= WIPHY_FLAG_PS_ON_BY_DEFAULT;
	else
		wiphy->flags &= ~WIPHY_FLAG_PS_ON_BY_DEFAULT;
}

int rtw_wdev_alloc(struct rtw_adapter *padapter, struct device *dev)
{
	int ret = 0;
	struct wiphy *wiphy;
	struct wireless_dev *wdev;
	struct rtw_wdev_priv *pwdev_priv;
	struct net_device *pnetdev = padapter->pnetdev;

	DBG_8723A("%s(padapter =%p)\n", __func__, padapter);

	/* wiphy */
	wiphy = wiphy_new(&rtw_cfg80211_ops, sizeof(struct rtw_wdev_priv));
	if (!wiphy) {
		DBG_8723A("Couldn't allocate wiphy device\n");
		ret = -ENOMEM;
		goto exit;
	}

	/*  wdev */
	wdev = kzalloc(sizeof(struct wireless_dev), GFP_KERNEL);
	if (!wdev) {
		DBG_8723A("Couldn't allocate wireless device\n");
		ret = -ENOMEM;
		goto free_wiphy;
	}

	set_wiphy_dev(wiphy, dev);
	rtw_cfg80211_preinit_wiphy(padapter, wiphy);

	ret = wiphy_register(wiphy);
	if (ret < 0) {
		DBG_8723A("Couldn't register wiphy device\n");
		goto free_wdev;
	}

	wdev->wiphy = wiphy;
	wdev->netdev = pnetdev;
	/* wdev->iftype = NL80211_IFTYPE_STATION; */
	/*  for rtw_setopmode_cmd23a() in cfg80211_rtw_change_iface() */
	wdev->iftype = NL80211_IFTYPE_MONITOR;
	padapter->rtw_wdev = wdev;
	pnetdev->ieee80211_ptr = wdev;

	/* init pwdev_priv */
	pwdev_priv = wdev_to_priv(wdev);
	pwdev_priv->rtw_wdev = wdev;
	pwdev_priv->pmon_ndev = NULL;
	pwdev_priv->ifname_mon[0] = '\0';
	pwdev_priv->padapter = padapter;
	pwdev_priv->scan_request = NULL;
	spin_lock_init(&pwdev_priv->scan_req_lock);

	pwdev_priv->p2p_enabled = false;

	if (padapter->registrypriv.power_mgnt != PS_MODE_ACTIVE)
		pwdev_priv->power_mgmt = true;
	else
		pwdev_priv->power_mgmt = false;

	return ret;
free_wdev:
	kfree(wdev);
free_wiphy:
	wiphy_free(wiphy);
exit:
	return ret;
}

void rtw_wdev_free(struct wireless_dev *wdev)
{
	DBG_8723A("%s(wdev =%p)\n", __func__, wdev);

	if (!wdev)
		return;

	kfree(wdev->wiphy->bands[IEEE80211_BAND_2GHZ]);
	kfree(wdev->wiphy->bands[IEEE80211_BAND_5GHZ]);

	wiphy_free(wdev->wiphy);

	kfree(wdev);
}

void rtw_wdev_unregister(struct wireless_dev *wdev)
{
	struct rtw_wdev_priv *pwdev_priv;

	DBG_8723A("%s(wdev =%p)\n", __func__, wdev);

	if (!wdev)
		return;

	pwdev_priv = wdev_to_priv(wdev);

	rtw_cfg80211_indicate_scan_done(pwdev_priv, true);

	if (pwdev_priv->pmon_ndev) {
		DBG_8723A("%s, unregister monitor interface\n", __func__);
		unregister_netdev(pwdev_priv->pmon_ndev);
	}

	wiphy_unregister(wdev->wiphy);
}
