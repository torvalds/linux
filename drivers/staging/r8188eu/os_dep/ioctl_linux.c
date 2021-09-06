// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2007 - 2012 Realtek Corporation. */

#define _IOCTL_LINUX_C_

#include "../include/osdep_service.h"
#include "../include/drv_types.h"
#include "../include/wlan_bssdef.h"
#include "../include/rtw_debug.h"
#include "../include/wifi.h"
#include "../include/rtw_mlme.h"
#include "../include/rtw_mlme_ext.h"
#include "../include/rtw_ioctl.h"
#include "../include/rtw_ioctl_set.h"
#include "../include/rtw_mp_ioctl.h"
#include "../include/usb_ops.h"
#include "../include/rtl8188e_hal.h"
#include "../include/rtl8188e_led.h"

#include "../include/rtw_mp.h"
#include "../include/rtw_iol.h"

#define RTL_IOCTL_WPA_SUPPLICANT	(SIOCIWFIRSTPRIV + 30)

#define SCAN_ITEM_SIZE 768
#define MAX_CUSTOM_LEN 64
#define RATE_COUNT 4

/*  combo scan */
#define WEXT_CSCAN_AMOUNT 9
#define WEXT_CSCAN_BUF_LEN		360
#define WEXT_CSCAN_HEADER		"CSCAN S\x01\x00\x00S\x00"
#define WEXT_CSCAN_HEADER_SIZE		12
#define WEXT_CSCAN_SSID_SECTION		'S'
#define WEXT_CSCAN_CHANNEL_SECTION	'C'
#define WEXT_CSCAN_NPROBE_SECTION	'N'
#define WEXT_CSCAN_ACTV_DWELL_SECTION	'A'
#define WEXT_CSCAN_PASV_DWELL_SECTION	'P'
#define WEXT_CSCAN_HOME_DWELL_SECTION	'H'
#define WEXT_CSCAN_TYPE_SECTION		'T'

static struct mp_ioctl_handler mp_ioctl_hdl[] = {
/*0*/	GEN_HANDLER(sizeof(u32), rtl8188eu_oid_rt_pro_start_test_hdl, OID_RT_PRO_START_TEST)
	GEN_HANDLER(sizeof(u32), rtl8188eu_oid_rt_pro_stop_test_hdl, OID_RT_PRO_STOP_TEST)

	GEN_HANDLER(sizeof(struct rwreg_param), rtl8188eu_oid_rt_pro_read_register_hdl, OID_RT_PRO_READ_REGISTER)
	GEN_HANDLER(sizeof(struct rwreg_param), rtl8188eu_oid_rt_pro_write_register_hdl, OID_RT_PRO_WRITE_REGISTER)
	GEN_HANDLER(sizeof(struct bb_reg_param), rtl8188eu_oid_rt_pro_read_bb_reg_hdl, OID_RT_PRO_READ_BB_REG)
/*5*/	GEN_HANDLER(sizeof(struct bb_reg_param), rtl8188eu_oid_rt_pro_write_bb_reg_hdl, OID_RT_PRO_WRITE_BB_REG)
	GEN_HANDLER(sizeof(struct rf_reg_param), rtl8188eu_oid_rt_pro_read_rf_reg_hdl, OID_RT_PRO_RF_READ_REGISTRY)
	GEN_HANDLER(sizeof(struct rf_reg_param), rtl8188eu_oid_rt_pro_write_rf_reg_hdl, OID_RT_PRO_RF_WRITE_REGISTRY)

	GEN_HANDLER(sizeof(u32), rtl8188eu_oid_rt_pro_set_channel_direct_call_hdl, OID_RT_PRO_SET_CHANNEL_DIRECT_CALL)
	GEN_HANDLER(sizeof(struct txpower_param), rtl8188eu_oid_rt_pro_set_tx_power_control_hdl, OID_RT_PRO_SET_TX_POWER_CONTROL)
/*10*/	GEN_HANDLER(sizeof(u32), rtl8188eu_oid_rt_pro_set_data_rate_hdl, OID_RT_PRO_SET_DATA_RATE)
	GEN_HANDLER(sizeof(u32), rtl8188eu_oid_rt_set_bandwidth_hdl, OID_RT_SET_BANDWIDTH)
	GEN_HANDLER(sizeof(u32), rtl8188eu_oid_rt_pro_set_antenna_bb_hdl, OID_RT_PRO_SET_ANTENNA_BB)

	GEN_HANDLER(sizeof(u32), rtl8188eu_oid_rt_pro_set_continuous_tx_hdl, OID_RT_PRO_SET_CONTINUOUS_TX)
	GEN_HANDLER(sizeof(u32), rtl8188eu_oid_rt_pro_set_single_carrier_tx_hdl, OID_RT_PRO_SET_SINGLE_CARRIER_TX)
/*15*/	GEN_HANDLER(sizeof(u32), rtl8188eu_oid_rt_pro_set_carrier_suppression_tx_hdl, OID_RT_PRO_SET_CARRIER_SUPPRESSION_TX)
	GEN_HANDLER(sizeof(u32), rtl8188eu_oid_rt_pro_set_single_tone_tx_hdl, OID_RT_PRO_SET_SINGLE_TONE_TX)

	EXT_MP_IOCTL_HANDLER(0, xmit_packet, 0)

	GEN_HANDLER(sizeof(u32), rtl8188eu_oid_rt_set_rx_packet_type_hdl, OID_RT_SET_RX_PACKET_TYPE)
	GEN_HANDLER(0, rtl8188eu_oid_rt_reset_phy_rx_packet_count_hdl, OID_RT_RESET_PHY_RX_PACKET_COUNT)
/*20*/	GEN_HANDLER(sizeof(u32), rtl8188eu_oid_rt_get_phy_rx_packet_received_hdl, OID_RT_GET_PHY_RX_PACKET_RECEIVED)
	GEN_HANDLER(sizeof(u32), rtl8188eu_oid_rt_get_phy_rx_packet_crc32_error_hdl, OID_RT_GET_PHY_RX_PACKET_CRC32_ERROR)

	GEN_HANDLER(sizeof(struct eeprom_rw_param), NULL, 0)
	GEN_HANDLER(sizeof(struct eeprom_rw_param), NULL, 0)
	GEN_HANDLER(sizeof(struct efuse_access_struct), rtl8188eu_oid_rt_pro_efuse_hdl, OID_RT_PRO_EFUSE)
/*25*/	GEN_HANDLER(0, rtl8188eu_oid_rt_pro_efuse_map_hdl, OID_RT_PRO_EFUSE_MAP)
	GEN_HANDLER(sizeof(u32), rtl8188eu_oid_rt_get_efuse_max_size_hdl, OID_RT_GET_EFUSE_MAX_SIZE)
	GEN_HANDLER(sizeof(u32), rtl8188eu_oid_rt_get_efuse_current_size_hdl, OID_RT_GET_EFUSE_CURRENT_SIZE)

	GEN_HANDLER(sizeof(u32), rtl8188eu_oid_rt_get_thermal_meter_hdl, OID_RT_PRO_GET_THERMAL_METER)
	GEN_HANDLER(sizeof(u8), rtl8188eu_oid_rt_pro_set_power_tracking_hdl, OID_RT_PRO_SET_POWER_TRACKING)
/*30*/	GEN_HANDLER(sizeof(u8), rtl8188eu_oid_rt_set_power_down_hdl, OID_RT_SET_POWER_DOWN)
/*31*/	GEN_HANDLER(0, rtl8188eu_oid_rt_pro_trigger_gpio_hdl, 0)
};

static u32 rtw_rates[] = {1000000, 2000000, 5500000, 11000000,
	6000000, 9000000, 12000000, 18000000, 24000000, 36000000,
	48000000, 54000000};

void indicate_wx_scan_complete_event(struct adapter *padapter)
{
	union iwreq_data wrqu;

	memset(&wrqu, 0, sizeof(union iwreq_data));
	wireless_send_event(padapter->pnetdev, SIOCGIWSCAN, &wrqu, NULL);
}

void rtw_indicate_wx_assoc_event(struct adapter *padapter)
{
	union iwreq_data wrqu;
	struct	mlme_priv *pmlmepriv = &padapter->mlmepriv;

	memset(&wrqu, 0, sizeof(union iwreq_data));

	wrqu.ap_addr.sa_family = ARPHRD_ETHER;

	memcpy(wrqu.ap_addr.sa_data, pmlmepriv->cur_network.network.MacAddress, ETH_ALEN);

	DBG_88E_LEVEL(_drv_always_, "assoc success\n");
	wireless_send_event(padapter->pnetdev, SIOCGIWAP, &wrqu, NULL);
}

void rtw_indicate_wx_disassoc_event(struct adapter *padapter)
{
	union iwreq_data wrqu;

	memset(&wrqu, 0, sizeof(union iwreq_data));

	wrqu.ap_addr.sa_family = ARPHRD_ETHER;
	memset(wrqu.ap_addr.sa_data, 0, ETH_ALEN);

	DBG_88E_LEVEL(_drv_always_, "indicate disassoc\n");
	wireless_send_event(padapter->pnetdev, SIOCGIWAP, &wrqu, NULL);
}

static char *translate_scan(struct adapter *padapter,
			    struct iw_request_info *info,
			    struct wlan_network *pnetwork,
			    char *start, char *stop)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct iw_event iwe;
	u16 cap;
	__le16 le_tmp;
	u32 ht_ielen = 0;
	char *custom;
	char *p;
	u16 max_rate = 0, rate, ht_cap = false;
	u32 i = 0;
	u8 bw_40MHz = 0, short_GI = 0;
	u16 mcs_rate = 0;
	u8 ss, sq;
#ifdef CONFIG_88EU_P2P
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;

	if (!rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE)) {
		u32	blnGotP2PIE = false;

		/*	User is doing the P2P device discovery */
		/*	The prefix of SSID should be "DIRECT-" and the IE should contains the P2P IE. */
		/*	If not, the driver should ignore this AP and go to the next AP. */

		/*	Verifying the SSID */
		if (!memcmp(pnetwork->network.Ssid.Ssid, pwdinfo->p2p_wildcard_ssid, P2P_WILDCARD_SSID_LEN)) {
			u32	p2pielen = 0;

			if (pnetwork->network.Reserved[0] == 2) {/*  Probe Request */
				/*	Verifying the P2P IE */
				if (rtw_get_p2p_ie(pnetwork->network.IEs, pnetwork->network.IELength, NULL, &p2pielen))
					blnGotP2PIE = true;
			} else {/*  Beacon or Probe Respones */
				/*	Verifying the P2P IE */
				if (rtw_get_p2p_ie(&pnetwork->network.IEs[12], pnetwork->network.IELength - 12, NULL, &p2pielen))
					blnGotP2PIE = true;
			}
		}

		if (!blnGotP2PIE)
			return start;
	}
#endif /* CONFIG_88EU_P2P */

	/*  AP MAC address  */
	iwe.cmd = SIOCGIWAP;
	iwe.u.ap_addr.sa_family = ARPHRD_ETHER;

	memcpy(iwe.u.ap_addr.sa_data, pnetwork->network.MacAddress, ETH_ALEN);
	start = iwe_stream_add_event(info, start, stop, &iwe, IW_EV_ADDR_LEN);

	/* Add the ESSID */
	iwe.cmd = SIOCGIWESSID;
	iwe.u.data.flags = 1;
	iwe.u.data.length = min_t(u16, pnetwork->network.Ssid.SsidLength, 32);
	start = iwe_stream_add_point(info, start, stop, &iwe, pnetwork->network.Ssid.Ssid);

	/* parsing HT_CAP_IE */
	p = rtw_get_ie(&pnetwork->network.IEs[12], _HT_CAPABILITY_IE_, &ht_ielen, pnetwork->network.IELength - 12);

	if (p && ht_ielen > 0) {
		struct ieee80211_ht_cap *pht_capie;

		ht_cap = true;
		pht_capie = (struct ieee80211_ht_cap *)(p + 2);
		memcpy(&mcs_rate, pht_capie->mcs.rx_mask, 2);
		bw_40MHz = (le16_to_cpu(pht_capie->cap_info) &
			    IEEE80211_HT_CAP_SUP_WIDTH_20_40) ? 1 : 0;
		short_GI = (le16_to_cpu(pht_capie->cap_info) &
			    (IEEE80211_HT_CAP_SGI_20 | IEEE80211_HT_CAP_SGI_40)) ? 1 : 0;
	}

	/* Add the protocol name */
	iwe.cmd = SIOCGIWNAME;
	if ((rtw_is_cckratesonly_included((u8 *)&pnetwork->network.SupportedRates))) {
		if (ht_cap)
			snprintf(iwe.u.name, IFNAMSIZ, "IEEE 802.11bn");
		else
		snprintf(iwe.u.name, IFNAMSIZ, "IEEE 802.11b");
	} else if ((rtw_is_cckrates_included((u8 *)&pnetwork->network.SupportedRates))) {
		if (ht_cap)
			snprintf(iwe.u.name, IFNAMSIZ, "IEEE 802.11bgn");
		else
			snprintf(iwe.u.name, IFNAMSIZ, "IEEE 802.11bg");
	} else {
		if (ht_cap)
			snprintf(iwe.u.name, IFNAMSIZ, "IEEE 802.11gn");
		else
			snprintf(iwe.u.name, IFNAMSIZ, "IEEE 802.11g");
	}

	start = iwe_stream_add_event(info, start, stop, &iwe, IW_EV_CHAR_LEN);

	  /* Add mode */
	iwe.cmd = SIOCGIWMODE;
	memcpy(&le_tmp, rtw_get_capability_from_ie(pnetwork->network.IEs), 2);

	cap = le16_to_cpu(le_tmp);

	if (cap & (WLAN_CAPABILITY_IBSS | WLAN_CAPABILITY_BSS)) {
		if (cap & WLAN_CAPABILITY_BSS)
			iwe.u.mode = IW_MODE_MASTER;
		else
			iwe.u.mode = IW_MODE_ADHOC;

		start = iwe_stream_add_event(info, start, stop, &iwe, IW_EV_UINT_LEN);
	}

	if (pnetwork->network.Configuration.DSConfig < 1)
		pnetwork->network.Configuration.DSConfig = 1;

	 /* Add frequency/channel */
	iwe.cmd = SIOCGIWFREQ;
	iwe.u.freq.m = rtw_ch2freq(pnetwork->network.Configuration.DSConfig) * 100000;
	iwe.u.freq.e = 1;
	iwe.u.freq.i = pnetwork->network.Configuration.DSConfig;
	start = iwe_stream_add_event(info, start, stop, &iwe, IW_EV_FREQ_LEN);

	/* Add encryption capability */
	iwe.cmd = SIOCGIWENCODE;
	if (cap & WLAN_CAPABILITY_PRIVACY)
		iwe.u.data.flags = IW_ENCODE_ENABLED | IW_ENCODE_NOKEY;
	else
		iwe.u.data.flags = IW_ENCODE_DISABLED;
	iwe.u.data.length = 0;
	start = iwe_stream_add_point(info, start, stop, &iwe, pnetwork->network.Ssid.Ssid);

	/*Add basic and extended rates */
	max_rate = 0;
	custom = kzalloc(MAX_CUSTOM_LEN, GFP_ATOMIC);
	if (!custom)
		return start;
	p = custom;
	p += snprintf(p, MAX_CUSTOM_LEN - (p - custom), " Rates (Mb/s): ");
	while (pnetwork->network.SupportedRates[i] != 0) {
		rate = pnetwork->network.SupportedRates[i] & 0x7F;
		if (rate > max_rate)
			max_rate = rate;
		p += snprintf(p, MAX_CUSTOM_LEN - (p - custom),
			      "%d%s ", rate >> 1, (rate & 1) ? ".5" : "");
		i++;
	}

	if (ht_cap) {
		if (mcs_rate & 0x8000)/* MCS15 */
			max_rate = (bw_40MHz) ? ((short_GI) ? 300 : 270) : ((short_GI) ? 144 : 130);
		else if (mcs_rate & 0x0080)/* MCS7 */
			;
		else/* default MCS7 */
			max_rate = (bw_40MHz) ? ((short_GI) ? 150 : 135) : ((short_GI) ? 72 : 65);

		max_rate = max_rate * 2;/* Mbps/2; */
	}

	iwe.cmd = SIOCGIWRATE;
	iwe.u.bitrate.fixed = 0;
	iwe.u.bitrate.disabled = 0;
	iwe.u.bitrate.value = max_rate * 500000;
	start = iwe_stream_add_event(info, start, stop, &iwe, IW_EV_PARAM_LEN);

	/* parsing WPA/WPA2 IE */
	{
		u8 *buf;
		u8 *wpa_ie, *rsn_ie;
		u16 wpa_len = 0, rsn_len = 0;
		u8 *p;

		buf = kzalloc(MAX_WPA_IE_LEN, GFP_ATOMIC);
		if (!buf)
			goto exit;
		wpa_ie = kzalloc(255, GFP_ATOMIC);
		if (!wpa_ie) {
			kfree(buf);
			goto exit;
		}
		rsn_ie = kzalloc(255, GFP_ATOMIC);
		if (!rsn_ie) {
			kfree(buf);
			kfree(wpa_ie);
			goto exit;
		}
		rtw_get_sec_ie(pnetwork->network.IEs, pnetwork->network.IELength, rsn_ie, &rsn_len, wpa_ie, &wpa_len);

		if (wpa_len > 0) {
			p = buf;
			memset(buf, 0, MAX_WPA_IE_LEN);
			p += sprintf(p, "wpa_ie =");
			for (i = 0; i < wpa_len; i++)
				p += sprintf(p, "%02x", wpa_ie[i]);

			memset(&iwe, 0, sizeof(iwe));
			iwe.cmd = IWEVCUSTOM;
			iwe.u.data.length = strlen(buf);
			start = iwe_stream_add_point(info, start, stop, &iwe, buf);

			memset(&iwe, 0, sizeof(iwe));
			iwe.cmd = IWEVGENIE;
			iwe.u.data.length = wpa_len;
			start = iwe_stream_add_point(info, start, stop, &iwe, wpa_ie);
		}
		if (rsn_len > 0) {
			p = buf;
			memset(buf, 0, MAX_WPA_IE_LEN);
			p += sprintf(p, "rsn_ie =");
			for (i = 0; i < rsn_len; i++)
				p += sprintf(p, "%02x", rsn_ie[i]);
			memset(&iwe, 0, sizeof(iwe));
			iwe.cmd = IWEVCUSTOM;
			iwe.u.data.length = strlen(buf);
			start = iwe_stream_add_point(info, start, stop, &iwe, buf);

			memset(&iwe, 0, sizeof(iwe));
			iwe.cmd = IWEVGENIE;
			iwe.u.data.length = rsn_len;
			start = iwe_stream_add_point(info, start, stop, &iwe, rsn_ie);
		}
		kfree(buf);
		kfree(wpa_ie);
		kfree(rsn_ie);
	}

	{/* parsing WPS IE */
		uint cnt = 0, total_ielen;
		u8 *wpsie_ptr = NULL;
		uint wps_ielen = 0;

		u8 *ie_ptr = pnetwork->network.IEs + _FIXED_IE_LENGTH_;
		total_ielen = pnetwork->network.IELength - _FIXED_IE_LENGTH_;

		while (cnt < total_ielen) {
			if (rtw_is_wps_ie(&ie_ptr[cnt], &wps_ielen) && (wps_ielen > 2)) {
				wpsie_ptr = &ie_ptr[cnt];
				iwe.cmd = IWEVGENIE;
				iwe.u.data.length = (u16)wps_ielen;
				start = iwe_stream_add_point(info, start, stop, &iwe, wpsie_ptr);
			}
			cnt += ie_ptr[cnt + 1] + 2; /* goto next */
		}
	}

	/* Add quality statistics */
	iwe.cmd = IWEVQUAL;
	iwe.u.qual.updated = IW_QUAL_QUAL_UPDATED | IW_QUAL_LEVEL_UPDATED | IW_QUAL_NOISE_INVALID;

	if (check_fwstate(pmlmepriv, _FW_LINKED) &&
	    is_same_network(&pmlmepriv->cur_network.network, &pnetwork->network)) {
		ss = padapter->recvpriv.signal_strength;
		sq = padapter->recvpriv.signal_qual;
	} else {
		ss = pnetwork->network.PhyInfo.SignalStrength;
		sq = pnetwork->network.PhyInfo.SignalQuality;
	}

	iwe.u.qual.level = (u8)ss;
	iwe.u.qual.qual = (u8)sq;   /*  signal quality */
	iwe.u.qual.noise = 0; /*  noise level */
	start = iwe_stream_add_event(info, start, stop, &iwe, IW_EV_QUAL_LEN);
exit:
	kfree(custom);
	return start;
}

static int wpa_set_auth_algs(struct net_device *dev, u32 value)
{
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	int ret = 0;

	if ((value & AUTH_ALG_SHARED_KEY) && (value & AUTH_ALG_OPEN_SYSTEM)) {
		DBG_88E("wpa_set_auth_algs, AUTH_ALG_SHARED_KEY and  AUTH_ALG_OPEN_SYSTEM [value:0x%x]\n", value);
		padapter->securitypriv.ndisencryptstatus = Ndis802_11Encryption1Enabled;
		padapter->securitypriv.ndisauthtype = Ndis802_11AuthModeAutoSwitch;
		padapter->securitypriv.dot11AuthAlgrthm = dot11AuthAlgrthm_Auto;
	} else if (value & AUTH_ALG_SHARED_KEY) {
		DBG_88E("wpa_set_auth_algs, AUTH_ALG_SHARED_KEY  [value:0x%x]\n", value);
		padapter->securitypriv.ndisencryptstatus = Ndis802_11Encryption1Enabled;

		padapter->securitypriv.ndisauthtype = Ndis802_11AuthModeShared;
		padapter->securitypriv.dot11AuthAlgrthm = dot11AuthAlgrthm_Shared;
	} else if (value & AUTH_ALG_OPEN_SYSTEM) {
		DBG_88E("wpa_set_auth_algs, AUTH_ALG_OPEN_SYSTEM\n");
		if (padapter->securitypriv.ndisauthtype < Ndis802_11AuthModeWPAPSK) {
			padapter->securitypriv.ndisauthtype = Ndis802_11AuthModeOpen;
			padapter->securitypriv.dot11AuthAlgrthm = dot11AuthAlgrthm_Open;
		}
	} else if (value & AUTH_ALG_LEAP) {
		DBG_88E("wpa_set_auth_algs, AUTH_ALG_LEAP\n");
	} else {
		DBG_88E("wpa_set_auth_algs, error!\n");
		ret = -EINVAL;
	}
	return ret;
}

static int wpa_set_encryption(struct net_device *dev, struct ieee_param *param, u32 param_len)
{
	int ret = 0;
	u32 wep_key_idx, wep_key_len, wep_total_len;
	struct ndis_802_11_wep	 *pwep = NULL;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct security_priv *psecuritypriv = &padapter->securitypriv;
#ifdef CONFIG_88EU_P2P
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;
#endif /* CONFIG_88EU_P2P */

	param->u.crypt.err = 0;
	param->u.crypt.alg[IEEE_CRYPT_ALG_NAME_LEN - 1] = '\0';

	if (param_len < (u32)((u8 *)param->u.crypt.key - (u8 *)param) + param->u.crypt.key_len) {
		ret =  -EINVAL;
		goto exit;
	}

	if (param->sta_addr[0] == 0xff && param->sta_addr[1] == 0xff &&
	    param->sta_addr[2] == 0xff && param->sta_addr[3] == 0xff &&
	    param->sta_addr[4] == 0xff && param->sta_addr[5] == 0xff) {
		if (param->u.crypt.idx >= WEP_KEYS) {
			ret = -EINVAL;
			goto exit;
		}
	} else {
		ret = -EINVAL;
		goto exit;
	}

	if (strcmp(param->u.crypt.alg, "WEP") == 0) {
		DBG_88E("wpa_set_encryption, crypt.alg = WEP\n");

		padapter->securitypriv.ndisencryptstatus = Ndis802_11Encryption1Enabled;
		padapter->securitypriv.dot11PrivacyAlgrthm = _WEP40_;
		padapter->securitypriv.dot118021XGrpPrivacy = _WEP40_;

		wep_key_idx = param->u.crypt.idx;
		wep_key_len = param->u.crypt.key_len;

		DBG_88E("(1)wep_key_idx =%d\n", wep_key_idx);

		if (wep_key_idx > WEP_KEYS)
			return -EINVAL;

		if (wep_key_len > 0) {
			wep_key_len = wep_key_len <= 5 ? 5 : 13;
			wep_total_len = wep_key_len + FIELD_OFFSET(struct ndis_802_11_wep, KeyMaterial);
			pwep = kmalloc(wep_total_len, GFP_KERNEL);
			if (!pwep)
				goto exit;

			memset(pwep, 0, wep_total_len);
			pwep->KeyLength = wep_key_len;
			pwep->Length = wep_total_len;
			if (wep_key_len == 13) {
				padapter->securitypriv.dot11PrivacyAlgrthm = _WEP104_;
				padapter->securitypriv.dot118021XGrpPrivacy = _WEP104_;
			}
		} else {
			ret = -EINVAL;
			goto exit;
		}
		pwep->KeyIndex = wep_key_idx;
		pwep->KeyIndex |= 0x80000000;
		memcpy(pwep->KeyMaterial,  param->u.crypt.key, pwep->KeyLength);
		if (param->u.crypt.set_tx) {
			DBG_88E("wep, set_tx = 1\n");
			if (rtw_set_802_11_add_wep(padapter, pwep) == (u8)_FAIL)
				ret = -EOPNOTSUPP;
		} else {
			DBG_88E("wep, set_tx = 0\n");
			if (wep_key_idx >= WEP_KEYS) {
				ret = -EOPNOTSUPP;
				goto exit;
			}
			memcpy(&psecuritypriv->dot11DefKey[wep_key_idx].skey[0], pwep->KeyMaterial, pwep->KeyLength);
			psecuritypriv->dot11DefKeylen[wep_key_idx] = pwep->KeyLength;
			rtw_set_key(padapter, psecuritypriv, wep_key_idx, 0);
		}
		goto exit;
	}

	if (padapter->securitypriv.dot11AuthAlgrthm == dot11AuthAlgrthm_8021X) { /*  802_1x */
		struct sta_info *psta, *pbcmc_sta;
		struct sta_priv *pstapriv = &padapter->stapriv;

		if (check_fwstate(pmlmepriv, WIFI_STATION_STATE | WIFI_MP_STATE)) { /* sta mode */
			psta = rtw_get_stainfo(pstapriv, get_bssid(pmlmepriv));
			if (!psta) {
				;
			} else {
				if (strcmp(param->u.crypt.alg, "none") != 0)
					psta->ieee8021x_blocked = false;

				if ((padapter->securitypriv.ndisencryptstatus == Ndis802_11Encryption2Enabled) ||
				    (padapter->securitypriv.ndisencryptstatus ==  Ndis802_11Encryption3Enabled))
					psta->dot118021XPrivacy = padapter->securitypriv.dot11PrivacyAlgrthm;

				if (param->u.crypt.set_tx == 1) { /* pairwise key */
					memcpy(psta->dot118021x_UncstKey.skey,  param->u.crypt.key, (param->u.crypt.key_len > 16 ? 16 : param->u.crypt.key_len));

					if (strcmp(param->u.crypt.alg, "TKIP") == 0) { /* set mic key */
						memcpy(psta->dot11tkiptxmickey.skey, &param->u.crypt.key[16], 8);
						memcpy(psta->dot11tkiprxmickey.skey, &param->u.crypt.key[24], 8);
						padapter->securitypriv.busetkipkey = false;
					}

					DBG_88E(" ~~~~set sta key:unicastkey\n");

					rtw_setstakey_cmd(padapter, (unsigned char *)psta, true);
				} else { /* group key */
					memcpy(padapter->securitypriv.dot118021XGrpKey[param->u.crypt.idx].skey,  param->u.crypt.key, (param->u.crypt.key_len > 16 ? 16 : param->u.crypt.key_len));
					memcpy(padapter->securitypriv.dot118021XGrptxmickey[param->u.crypt.idx].skey, &param->u.crypt.key[16], 8);
					memcpy(padapter->securitypriv.dot118021XGrprxmickey[param->u.crypt.idx].skey, &param->u.crypt.key[24], 8);
					padapter->securitypriv.binstallGrpkey = true;
					DBG_88E(" ~~~~set sta key:groupkey\n");

					padapter->securitypriv.dot118021XGrpKeyid = param->u.crypt.idx;

					rtw_set_key(padapter, &padapter->securitypriv, param->u.crypt.idx, 1);
#ifdef CONFIG_88EU_P2P
					if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_PROVISIONING_ING))
						rtw_p2p_set_state(pwdinfo, P2P_STATE_PROVISIONING_DONE);
#endif /* CONFIG_88EU_P2P */
				}
			}
			pbcmc_sta = rtw_get_bcmc_stainfo(padapter);
			if (!pbcmc_sta) {
				;
			} else {
				/* Jeff: don't disable ieee8021x_blocked while clearing key */
				if (strcmp(param->u.crypt.alg, "none") != 0)
					pbcmc_sta->ieee8021x_blocked = false;

				if ((padapter->securitypriv.ndisencryptstatus == Ndis802_11Encryption2Enabled) ||
				    (padapter->securitypriv.ndisencryptstatus ==  Ndis802_11Encryption3Enabled))
					pbcmc_sta->dot118021XPrivacy = padapter->securitypriv.dot11PrivacyAlgrthm;
			}
		}
	}

exit:

	kfree(pwep);

	return ret;
}

static int rtw_set_wpa_ie(struct adapter *padapter, char *pie, unsigned short ielen)
{
	u8 *buf = NULL;
	int group_cipher = 0, pairwise_cipher = 0;
	int ret = 0;
#ifdef CONFIG_88EU_P2P
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;
#endif /* CONFIG_88EU_P2P */

	if (ielen > MAX_WPA_IE_LEN || !pie) {
		_clr_fwstate_(&padapter->mlmepriv, WIFI_UNDER_WPS);
		if (!pie)
			return ret;
		else
			return -EINVAL;
	}

	if (ielen) {
		buf = kmemdup(pie, ielen, GFP_KERNEL);
		if (!buf) {
			ret =  -ENOMEM;
			goto exit;
		}

		/* dump */
		{
			int i;
			DBG_88E("\n wpa_ie(length:%d):\n", ielen);
			for (i = 0; i < ielen; i += 8)
				DBG_88E("0x%.2x 0x%.2x 0x%.2x 0x%.2x 0x%.2x 0x%.2x 0x%.2x 0x%.2x\n", buf[i], buf[i + 1], buf[i + 2], buf[i + 3], buf[i + 4], buf[i + 5], buf[i + 6], buf[i + 7]);
		}

		if (ielen < RSN_HEADER_LEN) {
			ret  = -1;
			goto exit;
		}

		if (rtw_parse_wpa_ie(buf, ielen, &group_cipher, &pairwise_cipher, NULL) == _SUCCESS) {
			padapter->securitypriv.dot11AuthAlgrthm = dot11AuthAlgrthm_8021X;
			padapter->securitypriv.ndisauthtype = Ndis802_11AuthModeWPAPSK;
			memcpy(padapter->securitypriv.supplicant_ie, &buf[0], ielen);
		}

		if (rtw_parse_wpa2_ie(buf, ielen, &group_cipher, &pairwise_cipher, NULL) == _SUCCESS) {
			padapter->securitypriv.dot11AuthAlgrthm = dot11AuthAlgrthm_8021X;
			padapter->securitypriv.ndisauthtype = Ndis802_11AuthModeWPA2PSK;
			memcpy(padapter->securitypriv.supplicant_ie, &buf[0], ielen);
		}

		switch (group_cipher) {
		case WPA_CIPHER_NONE:
			padapter->securitypriv.dot118021XGrpPrivacy = _NO_PRIVACY_;
			padapter->securitypriv.ndisencryptstatus = Ndis802_11EncryptionDisabled;
			break;
		case WPA_CIPHER_WEP40:
			padapter->securitypriv.dot118021XGrpPrivacy = _WEP40_;
			padapter->securitypriv.ndisencryptstatus = Ndis802_11Encryption1Enabled;
			break;
		case WPA_CIPHER_TKIP:
			padapter->securitypriv.dot118021XGrpPrivacy = _TKIP_;
			padapter->securitypriv.ndisencryptstatus = Ndis802_11Encryption2Enabled;
			break;
		case WPA_CIPHER_CCMP:
			padapter->securitypriv.dot118021XGrpPrivacy = _AES_;
			padapter->securitypriv.ndisencryptstatus = Ndis802_11Encryption3Enabled;
			break;
		case WPA_CIPHER_WEP104:
			padapter->securitypriv.dot118021XGrpPrivacy = _WEP104_;
			padapter->securitypriv.ndisencryptstatus = Ndis802_11Encryption1Enabled;
			break;
		}

		switch (pairwise_cipher) {
		case WPA_CIPHER_NONE:
			padapter->securitypriv.dot11PrivacyAlgrthm = _NO_PRIVACY_;
			padapter->securitypriv.ndisencryptstatus = Ndis802_11EncryptionDisabled;
			break;
		case WPA_CIPHER_WEP40:
			padapter->securitypriv.dot11PrivacyAlgrthm = _WEP40_;
			padapter->securitypriv.ndisencryptstatus = Ndis802_11Encryption1Enabled;
			break;
		case WPA_CIPHER_TKIP:
			padapter->securitypriv.dot11PrivacyAlgrthm = _TKIP_;
			padapter->securitypriv.ndisencryptstatus = Ndis802_11Encryption2Enabled;
			break;
		case WPA_CIPHER_CCMP:
			padapter->securitypriv.dot11PrivacyAlgrthm = _AES_;
			padapter->securitypriv.ndisencryptstatus = Ndis802_11Encryption3Enabled;
			break;
		case WPA_CIPHER_WEP104:
			padapter->securitypriv.dot11PrivacyAlgrthm = _WEP104_;
			padapter->securitypriv.ndisencryptstatus = Ndis802_11Encryption1Enabled;
			break;
		}

		_clr_fwstate_(&padapter->mlmepriv, WIFI_UNDER_WPS);
		{/* set wps_ie */
			u16 cnt = 0;
			u8 eid, wps_oui[4] = {0x0, 0x50, 0xf2, 0x04};

			while (cnt < ielen) {
				eid = buf[cnt];
				if ((eid == _VENDOR_SPECIFIC_IE_) && (!memcmp(&buf[cnt + 2], wps_oui, 4))) {
					DBG_88E("SET WPS_IE\n");

					padapter->securitypriv.wps_ie_len = ((buf[cnt + 1] + 2) < (MAX_WPA_IE_LEN << 2)) ? (buf[cnt + 1] + 2) : (MAX_WPA_IE_LEN << 2);

					memcpy(padapter->securitypriv.wps_ie, &buf[cnt], padapter->securitypriv.wps_ie_len);

					set_fwstate(&padapter->mlmepriv, WIFI_UNDER_WPS);
#ifdef CONFIG_88EU_P2P
					if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_GONEGO_OK))
						rtw_p2p_set_state(pwdinfo, P2P_STATE_PROVISIONING_ING);
#endif /* CONFIG_88EU_P2P */
					cnt += buf[cnt + 1] + 2;
					break;
				} else {
					cnt += buf[cnt + 1] + 2; /* goto next */
				}
			}
		}
	}

exit:
	kfree(buf);
	return ret;
}

typedef unsigned char   NDIS_802_11_RATES_EX[NDIS_802_11_LENGTH_RATES_EX];

static int rtw_wx_get_name(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *extra)
{
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	u32 ht_ielen = 0;
	char *p;
	u8 ht_cap = false;
	struct	mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct wlan_bssid_ex  *pcur_bss = &pmlmepriv->cur_network.network;
	NDIS_802_11_RATES_EX *prates = NULL;

	if (check_fwstate(pmlmepriv, _FW_LINKED | WIFI_ADHOC_MASTER_STATE)) {
		/* parsing HT_CAP_IE */
		p = rtw_get_ie(&pcur_bss->IEs[12], _HT_CAPABILITY_IE_, &ht_ielen, pcur_bss->IELength - 12);
		if (p && ht_ielen > 0)
			ht_cap = true;

		prates = &pcur_bss->SupportedRates;

		if (rtw_is_cckratesonly_included((u8 *)prates)) {
			if (ht_cap)
				snprintf(wrqu->name, IFNAMSIZ, "IEEE 802.11bn");
			else
				snprintf(wrqu->name, IFNAMSIZ, "IEEE 802.11b");
		} else if (rtw_is_cckrates_included((u8 *)prates)) {
			if (ht_cap)
				snprintf(wrqu->name, IFNAMSIZ, "IEEE 802.11bgn");
			else
				snprintf(wrqu->name, IFNAMSIZ, "IEEE 802.11bg");
		} else {
			if (ht_cap)
				snprintf(wrqu->name, IFNAMSIZ, "IEEE 802.11gn");
			else
				snprintf(wrqu->name, IFNAMSIZ, "IEEE 802.11g");
		}
	} else {
		snprintf(wrqu->name, IFNAMSIZ, "unassociated");
	}



	return 0;
}

static int rtw_wx_get_freq(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *extra)
{
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct	mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct wlan_bssid_ex  *pcur_bss = &pmlmepriv->cur_network.network;

	if (check_fwstate(pmlmepriv, _FW_LINKED)) {
		/* wrqu->freq.m = ieee80211_wlan_frequencies[pcur_bss->Configuration.DSConfig-1] * 100000; */
		wrqu->freq.m = rtw_ch2freq(pcur_bss->Configuration.DSConfig) * 100000;
		wrqu->freq.e = 1;
		wrqu->freq.i = pcur_bss->Configuration.DSConfig;
	} else {
		wrqu->freq.m = rtw_ch2freq(padapter->mlmeextpriv.cur_channel) * 100000;
		wrqu->freq.e = 1;
		wrqu->freq.i = padapter->mlmeextpriv.cur_channel;
	}

	return 0;
}

static int rtw_wx_set_mode(struct net_device *dev, struct iw_request_info *a,
			     union iwreq_data *wrqu, char *b)
{
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	enum ndis_802_11_network_infra networkType;
	int ret = 0;



	if (_FAIL == rtw_pwr_wakeup(padapter)) {
		ret = -EPERM;
		goto exit;
	}

	if (!padapter->hw_init_completed) {
		ret = -EPERM;
		goto exit;
	}

	switch (wrqu->mode) {
	case IW_MODE_AUTO:
		networkType = Ndis802_11AutoUnknown;
		DBG_88E("set_mode = IW_MODE_AUTO\n");
		break;
	case IW_MODE_ADHOC:
		networkType = Ndis802_11IBSS;
		DBG_88E("set_mode = IW_MODE_ADHOC\n");
		break;
	case IW_MODE_MASTER:
		networkType = Ndis802_11APMode;
		DBG_88E("set_mode = IW_MODE_MASTER\n");
		break;
	case IW_MODE_INFRA:
		networkType = Ndis802_11Infrastructure;
		DBG_88E("set_mode = IW_MODE_INFRA\n");
		break;
	default:
		ret = -EINVAL;
		goto exit;
	}
	if (!rtw_set_802_11_infrastructure_mode(padapter, networkType)) {
		ret = -EPERM;
		goto exit;
	}
	rtw_setopmode_cmd(padapter, networkType);
exit:

	return ret;
}

static int rtw_wx_get_mode(struct net_device *dev, struct iw_request_info *a,
			     union iwreq_data *wrqu, char *b)
{
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct	mlme_priv	*pmlmepriv = &padapter->mlmepriv;

	if (check_fwstate(pmlmepriv, WIFI_STATION_STATE))
		wrqu->mode = IW_MODE_INFRA;
	else if  ((check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE)) ||
		  (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE)))
		wrqu->mode = IW_MODE_ADHOC;
	else if (check_fwstate(pmlmepriv, WIFI_AP_STATE))
		wrqu->mode = IW_MODE_MASTER;
	else
		wrqu->mode = IW_MODE_AUTO;



	return 0;
}

static int rtw_wx_set_pmkid(struct net_device *dev,
			    struct iw_request_info *a,
			    union iwreq_data *wrqu, char *extra)
{
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	u8   j, blInserted = false;
	int  ret = false;
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	struct iw_pmksa *pPMK = (struct iw_pmksa *)extra;
	u8     strZeroMacAddress[ETH_ALEN] = {0x00};
	u8     strIssueBssid[ETH_ALEN] = {0x00};

	memcpy(strIssueBssid, pPMK->bssid.sa_data, ETH_ALEN);
	if (pPMK->cmd == IW_PMKSA_ADD) {
		DBG_88E("[rtw_wx_set_pmkid] IW_PMKSA_ADD!\n");
		if (!memcmp(strIssueBssid, strZeroMacAddress, ETH_ALEN))
			return ret;
		else
			ret = true;
		blInserted = false;

		/* overwrite PMKID */
		for (j = 0; j < NUM_PMKID_CACHE; j++) {
			if (!memcmp(psecuritypriv->PMKIDList[j].Bssid, strIssueBssid, ETH_ALEN)) {
				/*  BSSID is matched, the same AP => rewrite with new PMKID. */
				DBG_88E("[rtw_wx_set_pmkid] BSSID exists in the PMKList.\n");
				memcpy(psecuritypriv->PMKIDList[j].PMKID, pPMK->pmkid, IW_PMKID_LEN);
				psecuritypriv->PMKIDList[j].bUsed = true;
				psecuritypriv->PMKIDIndex = j + 1;
				blInserted = true;
				break;
			}
		}

		if (!blInserted) {
			/*  Find a new entry */
			DBG_88E("[rtw_wx_set_pmkid] Use the new entry index = %d for this PMKID.\n",
				psecuritypriv->PMKIDIndex);

			memcpy(psecuritypriv->PMKIDList[psecuritypriv->PMKIDIndex].Bssid, strIssueBssid, ETH_ALEN);
			memcpy(psecuritypriv->PMKIDList[psecuritypriv->PMKIDIndex].PMKID, pPMK->pmkid, IW_PMKID_LEN);

			psecuritypriv->PMKIDList[psecuritypriv->PMKIDIndex].bUsed = true;
			psecuritypriv->PMKIDIndex++;
			if (psecuritypriv->PMKIDIndex == 16)
				psecuritypriv->PMKIDIndex = 0;
		}
	} else if (pPMK->cmd == IW_PMKSA_REMOVE) {
		DBG_88E("[rtw_wx_set_pmkid] IW_PMKSA_REMOVE!\n");
		ret = true;
		for (j = 0; j < NUM_PMKID_CACHE; j++) {
			if (!memcmp(psecuritypriv->PMKIDList[j].Bssid, strIssueBssid, ETH_ALEN)) {
				/*  BSSID is matched, the same AP => Remove this PMKID information and reset it. */
				memset(psecuritypriv->PMKIDList[j].Bssid, 0x00, ETH_ALEN);
				psecuritypriv->PMKIDList[j].bUsed = false;
				break;
			}
	       }
	} else if (pPMK->cmd == IW_PMKSA_FLUSH) {
		DBG_88E("[rtw_wx_set_pmkid] IW_PMKSA_FLUSH!\n");
		memset(&psecuritypriv->PMKIDList[0], 0x00, sizeof(struct rt_pmkid_list) * NUM_PMKID_CACHE);
		psecuritypriv->PMKIDIndex = 0;
		ret = true;
	}
	return ret;
}

static int rtw_wx_get_sens(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *extra)
{
	wrqu->sens.value = 0;
	wrqu->sens.fixed = 0;	/* no auto select */
	wrqu->sens.disabled = 1;
	return 0;
}

static int rtw_wx_get_range(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *extra)
{
	struct iw_range *range = (struct iw_range *)extra;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;

	u16 val;
	int i;

	wrqu->data.length = sizeof(*range);
	memset(range, 0, sizeof(*range));

	/* Let's try to keep this struct in the same order as in
	 * linux/include/wireless.h
	 */

	/* TODO: See what values we can set, and remove the ones we can't
	 * set, or fill them with some default data.
	 */

	/* ~5 Mb/s real (802.11b) */
	range->throughput = 5 * 1000 * 1000;

	/* signal level threshold range */

	/* percent values between 0 and 100. */
	range->max_qual.qual = 100;
	range->max_qual.level = 100;
	range->max_qual.noise = 100;
	range->max_qual.updated = 7; /* Updated all three */

	range->avg_qual.qual = 92; /* > 8% missed beacons is 'bad' */
	/* TODO: Find real 'good' to 'bad' threshol value for RSSI */
	range->avg_qual.level = 178; /* -78 dBm */
	range->avg_qual.noise = 0;
	range->avg_qual.updated = 7; /* Updated all three */

	range->num_bitrates = RATE_COUNT;

	for (i = 0; i < RATE_COUNT && i < IW_MAX_BITRATES; i++)
		range->bitrate[i] = rtw_rates[i];

	range->min_frag = MIN_FRAG_THRESHOLD;
	range->max_frag = MAX_FRAG_THRESHOLD;

	range->pm_capa = 0;

	range->we_version_compiled = WIRELESS_EXT;
	range->we_version_source = 16;

	for (i = 0, val = 0; i < MAX_CHANNEL_NUM; i++) {
		/*  Include only legal frequencies for some countries */
		if (pmlmeext->channel_set[i].ChannelNum != 0) {
			range->freq[val].i = pmlmeext->channel_set[i].ChannelNum;
			range->freq[val].m = rtw_ch2freq(pmlmeext->channel_set[i].ChannelNum) * 100000;
			range->freq[val].e = 1;
			val++;
		}

		if (val == IW_MAX_FREQUENCIES)
			break;
	}

	range->num_channels = val;
	range->num_frequency = val;

/*  The following code will proivde the security capability to network manager. */
/*  If the driver doesn't provide this capability to network manager, */
/*  the WPA/WPA2 routers can't be chosen in the network manager. */

/*
#define IW_SCAN_CAPA_NONE		0x00
#define IW_SCAN_CAPA_ESSID		0x01
#define IW_SCAN_CAPA_BSSID		0x02
#define IW_SCAN_CAPA_CHANNEL		0x04
#define IW_SCAN_CAPA_MODE		0x08
#define IW_SCAN_CAPA_RATE		0x10
#define IW_SCAN_CAPA_TYPE		0x20
#define IW_SCAN_CAPA_TIME		0x40
*/

	range->enc_capa = IW_ENC_CAPA_WPA | IW_ENC_CAPA_WPA2 |
			  IW_ENC_CAPA_CIPHER_TKIP | IW_ENC_CAPA_CIPHER_CCMP;

	range->scan_capa = IW_SCAN_CAPA_ESSID | IW_SCAN_CAPA_TYPE |
			   IW_SCAN_CAPA_BSSID | IW_SCAN_CAPA_CHANNEL |
			   IW_SCAN_CAPA_MODE | IW_SCAN_CAPA_RATE;


	return 0;
}

/* set bssid flow */
/* s1. rtw_set_802_11_infrastructure_mode() */
/* s2. rtw_set_802_11_authentication_mode() */
/* s3. set_802_11_encryption_mode() */
/* s4. rtw_set_802_11_bssid() */
static int rtw_wx_set_wap(struct net_device *dev,
			 struct iw_request_info *info,
			 union iwreq_data *awrq,
			 char *extra)
{
	uint ret = 0;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct sockaddr *temp = (struct sockaddr *)awrq;
	struct	mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct list_head *phead;
	u8 *dst_bssid, *src_bssid;
	struct __queue *queue	= &pmlmepriv->scanned_queue;
	struct	wlan_network	*pnetwork = NULL;
	enum ndis_802_11_auth_mode	authmode;



	if (_FAIL == rtw_pwr_wakeup(padapter)) {
		ret = -1;
		goto exit;
	}

	if (!padapter->bup) {
		ret = -1;
		goto exit;
	}

	if (temp->sa_family != ARPHRD_ETHER) {
		ret = -EINVAL;
		goto exit;
	}

	authmode = padapter->securitypriv.ndisauthtype;
	spin_lock_bh(&queue->lock);
	phead = get_list_head(queue);
	pmlmepriv->pscanned = phead->next;

	while (phead != pmlmepriv->pscanned) {

		pnetwork = container_of(pmlmepriv->pscanned, struct wlan_network, list);

		pmlmepriv->pscanned = pmlmepriv->pscanned->next;

		dst_bssid = pnetwork->network.MacAddress;

		src_bssid = temp->sa_data;

		if ((!memcmp(dst_bssid, src_bssid, ETH_ALEN))) {
			if (!rtw_set_802_11_infrastructure_mode(padapter, pnetwork->network.InfrastructureMode)) {
				ret = -1;
				spin_unlock_bh(&queue->lock);
				goto exit;
			}

			break;
		}
	}
	spin_unlock_bh(&queue->lock);

	rtw_set_802_11_authentication_mode(padapter, authmode);
	/* set_802_11_encryption_mode(padapter, padapter->securitypriv.ndisencryptstatus); */
	if (!rtw_set_802_11_bssid(padapter, temp->sa_data)) {
		ret = -1;
		goto exit;
	}

exit:



	return ret;
}

static int rtw_wx_get_wap(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu, char *extra)
{
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct	mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct wlan_bssid_ex  *pcur_bss = &pmlmepriv->cur_network.network;

	wrqu->ap_addr.sa_family = ARPHRD_ETHER;

	memset(wrqu->ap_addr.sa_data, 0, ETH_ALEN);

	if (check_fwstate(pmlmepriv, _FW_LINKED) ||
	    check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) ||
	    check_fwstate(pmlmepriv, WIFI_AP_STATE))
		memcpy(wrqu->ap_addr.sa_data, pcur_bss->MacAddress, ETH_ALEN);
	else
		memset(wrqu->ap_addr.sa_data, 0, ETH_ALEN);



	return 0;
}

static int rtw_wx_set_mlme(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *extra)
{
	int ret = 0;
	u16 reason;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct iw_mlme *mlme = (struct iw_mlme *)extra;

	if (!mlme)
		return -1;

	DBG_88E("%s\n", __func__);

	reason = mlme->reason_code;

	DBG_88E("%s, cmd =%d, reason =%d\n", __func__, mlme->cmd, reason);

	switch (mlme->cmd) {
	case IW_MLME_DEAUTH:
		if (!rtw_set_802_11_disassociate(padapter))
			ret = -1;
		break;
	case IW_MLME_DISASSOC:
		if (!rtw_set_802_11_disassociate(padapter))
			ret = -1;
		break;
	default:
		return -EOPNOTSUPP;
	}
	return ret;
}

static int rtw_wx_set_scan(struct net_device *dev, struct iw_request_info *a,
			     union iwreq_data *wrqu, char *extra)
{
	u8 _status = false;
	int ret = 0;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct ndis_802_11_ssid ssid[RTW_SSID_SCAN_AMOUNT];
#ifdef CONFIG_88EU_P2P
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;
#endif /* CONFIG_88EU_P2P */

	if (padapter->registrypriv.mp_mode == 1) {
		if (check_fwstate(pmlmepriv, WIFI_MP_STATE)) {
			ret = -1;
			goto exit;
		}
	}
	if (_FAIL == rtw_pwr_wakeup(padapter)) {
		ret = -1;
		goto exit;
	}

	if (padapter->bDriverStopped) {
		DBG_88E("bDriverStopped =%d\n", padapter->bDriverStopped);
		ret = -1;
		goto exit;
	}

	if (!padapter->bup) {
		ret = -1;
		goto exit;
	}

	if (!padapter->hw_init_completed) {
		ret = -1;
		goto exit;
	}

	/*  When Busy Traffic, driver do not site survey. So driver return success. */
	/*  wpa_supplicant will not issue SIOCSIWSCAN cmd again after scan timeout. */
	/*  modify by thomas 2011-02-22. */
	if (pmlmepriv->LinkDetectInfo.bBusyTraffic) {
		indicate_wx_scan_complete_event(padapter);
		goto exit;
	}

	if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY | _FW_UNDER_LINKING)) {
		indicate_wx_scan_complete_event(padapter);
		goto exit;
	}

/*	For the DMP WiFi Display project, the driver won't to scan because */
/*	the pmlmepriv->scan_interval is always equal to 3. */
/*	So, the wpa_supplicant won't find out the WPS SoftAP. */

#ifdef CONFIG_88EU_P2P
	if (pwdinfo->p2p_state != P2P_STATE_NONE) {
		rtw_p2p_set_pre_state(pwdinfo, rtw_p2p_state(pwdinfo));
		rtw_p2p_set_state(pwdinfo, P2P_STATE_FIND_PHASE_SEARCH);
		rtw_p2p_findphase_ex_set(pwdinfo, P2P_FINDPHASE_EX_FULL);
		rtw_free_network_queue(padapter, true);
	}
#endif /* CONFIG_88EU_P2P */

	memset(ssid, 0, sizeof(struct ndis_802_11_ssid) * RTW_SSID_SCAN_AMOUNT);

	if (wrqu->data.length == sizeof(struct iw_scan_req)) {
		struct iw_scan_req *req = (struct iw_scan_req *)extra;

		if (wrqu->data.flags & IW_SCAN_THIS_ESSID) {
			int len = min((int)req->essid_len, IW_ESSID_MAX_SIZE);

			memcpy(ssid[0].Ssid, req->essid, len);
			ssid[0].SsidLength = len;

			DBG_88E("IW_SCAN_THIS_ESSID, ssid =%s, len =%d\n", req->essid, req->essid_len);

			spin_lock_bh(&pmlmepriv->lock);

			_status = rtw_sitesurvey_cmd(padapter, ssid, 1, NULL, 0);

			spin_unlock_bh(&pmlmepriv->lock);
		} else if (req->scan_type == IW_SCAN_TYPE_PASSIVE) {
			DBG_88E("rtw_wx_set_scan, req->scan_type == IW_SCAN_TYPE_PASSIVE\n");
		}
	} else {
		if (wrqu->data.length >= WEXT_CSCAN_HEADER_SIZE &&
		    !memcmp(extra, WEXT_CSCAN_HEADER, WEXT_CSCAN_HEADER_SIZE)) {
			int len = wrqu->data.length - WEXT_CSCAN_HEADER_SIZE;
			char *pos = extra + WEXT_CSCAN_HEADER_SIZE;
			char section;
			char sec_len;
			int ssid_index = 0;

			while (len >= 1) {
				section = *(pos++);
				len -= 1;

				switch (section) {
				case WEXT_CSCAN_SSID_SECTION:
					if (len < 1) {
						len = 0;
						break;
					}
					sec_len = *(pos++); len -= 1;
					if (sec_len > 0 && sec_len <= len) {
						ssid[ssid_index].SsidLength = sec_len;
						memcpy(ssid[ssid_index].Ssid, pos, ssid[ssid_index].SsidLength);
						ssid_index++;
					}
					pos += sec_len;
					len -= sec_len;
					break;
				case WEXT_CSCAN_TYPE_SECTION:
				case WEXT_CSCAN_CHANNEL_SECTION:
					pos += 1;
					len -= 1;
					break;
				case WEXT_CSCAN_PASV_DWELL_SECTION:
				case WEXT_CSCAN_HOME_DWELL_SECTION:
				case WEXT_CSCAN_ACTV_DWELL_SECTION:
					pos += 2;
					len -= 2;
					break;
				default:
					len = 0; /*  stop parsing */
				}
			}

			/* it has still some scan parameter to parse, we only do this now... */
			_status = rtw_set_802_11_bssid_list_scan(padapter, ssid, RTW_SSID_SCAN_AMOUNT);
		} else {
			_status = rtw_set_802_11_bssid_list_scan(padapter, NULL, 0);
		}
	}

	if (!_status)
		ret = -1;

exit:

	return ret;
}

static int rtw_wx_get_scan(struct net_device *dev, struct iw_request_info *a,
			     union iwreq_data *wrqu, char *extra)
{
	struct list_head *plist, *phead;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct	mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct __queue *queue	= &pmlmepriv->scanned_queue;
	struct	wlan_network	*pnetwork = NULL;
	char *ev = extra;
	char *stop = ev + wrqu->data.length;
	u32 ret = 0;
	u32 cnt = 0;
	u32 wait_for_surveydone;
	int wait_status;
#ifdef CONFIG_88EU_P2P
	struct	wifidirect_info *pwdinfo = &padapter->wdinfo;
#endif /* CONFIG_88EU_P2P */

	if (padapter->pwrctrlpriv.brfoffbyhw && padapter->bDriverStopped) {
		ret = -EINVAL;
		goto exit;
	}

#ifdef CONFIG_88EU_P2P
	if (!rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE)) {
		/*	P2P is enabled */
		wait_for_surveydone = 200;
	} else {
		/*	P2P is disabled */
		wait_for_surveydone = 100;
	}
#else
	{
		wait_for_surveydone = 100;
	}
#endif /* CONFIG_88EU_P2P */

	wait_status = _FW_UNDER_SURVEY | _FW_UNDER_LINKING;

	while (check_fwstate(pmlmepriv, wait_status)) {
		msleep(30);
		cnt++;
		if (cnt > wait_for_surveydone)
			break;
	}

	spin_lock_bh(&pmlmepriv->scanned_queue.lock);

	phead = get_list_head(queue);
	plist = phead->next;

	while (phead != plist) {
		if ((stop - ev) < SCAN_ITEM_SIZE) {
			ret = -E2BIG;
			break;
		}

		pnetwork = container_of(plist, struct wlan_network, list);

		/* report network only if the current channel set contains the channel to which this network belongs */
		if (rtw_ch_set_search_ch(padapter->mlmeextpriv.channel_set, pnetwork->network.Configuration.DSConfig) >= 0)
			ev = translate_scan(padapter, a, pnetwork, ev, stop);

		plist = plist->next;
	}

	spin_unlock_bh(&pmlmepriv->scanned_queue.lock);

	wrqu->data.length = ev - extra;
	wrqu->data.flags = 0;

exit:

	return ret;
}

/* set ssid flow */
/* s1. rtw_set_802_11_infrastructure_mode() */
/* s2. set_802_11_authenticaion_mode() */
/* s3. set_802_11_encryption_mode() */
/* s4. rtw_set_802_11_ssid() */
static int rtw_wx_set_essid(struct net_device *dev,
			      struct iw_request_info *a,
			      union iwreq_data *wrqu, char *extra)
{
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct __queue *queue = &pmlmepriv->scanned_queue;
	struct list_head *phead;
	struct wlan_network *pnetwork = NULL;
	enum ndis_802_11_auth_mode authmode;
	struct ndis_802_11_ssid ndis_ssid;
	u8 *dst_ssid, *src_ssid;

	uint ret = 0, len;

	if (_FAIL == rtw_pwr_wakeup(padapter)) {
		ret = -1;
		goto exit;
	}

	if (!padapter->bup) {
		ret = -1;
		goto exit;
	}

	if (wrqu->essid.length > IW_ESSID_MAX_SIZE) {
		ret = -E2BIG;
		goto exit;
	}

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE)) {
		ret = -1;
		goto exit;
	}

	authmode = padapter->securitypriv.ndisauthtype;
	DBG_88E("=>%s\n", __func__);
	if (wrqu->essid.flags && wrqu->essid.length) {
		len = (wrqu->essid.length < IW_ESSID_MAX_SIZE) ? wrqu->essid.length : IW_ESSID_MAX_SIZE;

		if (wrqu->essid.length != 33)
			DBG_88E("ssid =%s, len =%d\n", extra, wrqu->essid.length);

		memset(&ndis_ssid, 0, sizeof(struct ndis_802_11_ssid));
		ndis_ssid.SsidLength = len;
		memcpy(ndis_ssid.Ssid, extra, len);
		src_ssid = ndis_ssid.Ssid;

		spin_lock_bh(&queue->lock);
		phead = get_list_head(queue);
		pmlmepriv->pscanned = phead->next;

		while (phead != pmlmepriv->pscanned) {
			pnetwork = container_of(pmlmepriv->pscanned, struct wlan_network, list);

			pmlmepriv->pscanned = pmlmepriv->pscanned->next;

			dst_ssid = pnetwork->network.Ssid.Ssid;

			if ((!memcmp(dst_ssid, src_ssid, ndis_ssid.SsidLength)) &&
			    (pnetwork->network.Ssid.SsidLength == ndis_ssid.SsidLength)) {

				if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE)) {
					if (pnetwork->network.InfrastructureMode != pmlmepriv->cur_network.network.InfrastructureMode)
						continue;
				}

				if (!rtw_set_802_11_infrastructure_mode(padapter, pnetwork->network.InfrastructureMode)) {
					ret = -1;
					spin_unlock_bh(&queue->lock);
					goto exit;
				}

				break;
			}
		}
		spin_unlock_bh(&queue->lock);
		rtw_set_802_11_authentication_mode(padapter, authmode);
		if (!rtw_set_802_11_ssid(padapter, &ndis_ssid)) {
			ret = -1;
			goto exit;
		}
	}

exit:

	DBG_88E("<=%s, ret %d\n", __func__, ret);



	return ret;
}

static int rtw_wx_get_essid(struct net_device *dev,
			      struct iw_request_info *a,
			      union iwreq_data *wrqu, char *extra)
{
	u32 len, ret = 0;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct	mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct wlan_bssid_ex  *pcur_bss = &pmlmepriv->cur_network.network;

	if ((check_fwstate(pmlmepriv, _FW_LINKED)) ||
	    (check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE))) {
		len = pcur_bss->Ssid.SsidLength;
		memcpy(extra, pcur_bss->Ssid.Ssid, len);
	} else {
		len = 0;
		*extra = 0;
	}
	wrqu->essid.length = len;
	wrqu->essid.flags = 1;

	return ret;
}

static int rtw_wx_set_rate(struct net_device *dev,
			      struct iw_request_info *a,
			      union iwreq_data *wrqu, char *extra)
{
	int i, ret = 0;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	u8 datarates[NumRates];
	u32	target_rate = wrqu->bitrate.value;
	u32	fixed = wrqu->bitrate.fixed;
	u32	ratevalue = 0;
	u8 mpdatarate[NumRates] = {11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 0xff};

	if (target_rate == -1) {
		ratevalue = 11;
		goto set_rate;
	}
	target_rate = target_rate / 100000;

	switch (target_rate) {
	case 10:
		ratevalue = 0;
		break;
	case 20:
		ratevalue = 1;
		break;
	case 55:
		ratevalue = 2;
		break;
	case 60:
		ratevalue = 3;
		break;
	case 90:
		ratevalue = 4;
		break;
	case 110:
		ratevalue = 5;
		break;
	case 120:
		ratevalue = 6;
		break;
	case 180:
		ratevalue = 7;
		break;
	case 240:
		ratevalue = 8;
		break;
	case 360:
		ratevalue = 9;
		break;
	case 480:
		ratevalue = 10;
		break;
	case 540:
		ratevalue = 11;
		break;
	default:
		ratevalue = 11;
		break;
	}

set_rate:

	for (i = 0; i < NumRates; i++) {
		if (ratevalue == mpdatarate[i]) {
			datarates[i] = mpdatarate[i];
			if (fixed == 0)
				break;
		} else {
			datarates[i] = 0xff;
		}
	}

	if (rtw_setdatarate_cmd(padapter, datarates) != _SUCCESS)
		ret = -1;

	return ret;
}

static int rtw_wx_get_rate(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *extra)
{
	u16 max_rate = 0;

	max_rate = rtw_get_cur_max_rate((struct adapter *)rtw_netdev_priv(dev));

	if (max_rate == 0)
		return -EPERM;

	wrqu->bitrate.fixed = 0;	/* no auto select */
	wrqu->bitrate.value = max_rate * 100000;

	return 0;
}

static int rtw_wx_set_rts(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *extra)
{
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);



	if (wrqu->rts.disabled) {
		padapter->registrypriv.rts_thresh = 2347;
	} else {
		if (wrqu->rts.value < 0 ||
		    wrqu->rts.value > 2347)
			return -EINVAL;

		padapter->registrypriv.rts_thresh = wrqu->rts.value;
	}

	DBG_88E("%s, rts_thresh =%d\n", __func__, padapter->registrypriv.rts_thresh);



	return 0;
}

static int rtw_wx_get_rts(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *extra)
{
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);



	DBG_88E("%s, rts_thresh =%d\n", __func__, padapter->registrypriv.rts_thresh);

	wrqu->rts.value = padapter->registrypriv.rts_thresh;
	wrqu->rts.fixed = 0;	/* no auto select */
	/* wrqu->rts.disabled = (wrqu->rts.value == DEFAULT_RTS_THRESHOLD); */



	return 0;
}

static int rtw_wx_set_frag(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *extra)
{
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);



	if (wrqu->frag.disabled) {
		padapter->xmitpriv.frag_len = MAX_FRAG_THRESHOLD;
	} else {
		if (wrqu->frag.value < MIN_FRAG_THRESHOLD ||
		    wrqu->frag.value > MAX_FRAG_THRESHOLD)
			return -EINVAL;

		padapter->xmitpriv.frag_len = wrqu->frag.value & ~0x1;
	}

	DBG_88E("%s, frag_len =%d\n", __func__, padapter->xmitpriv.frag_len);



	return 0;
}

static int rtw_wx_get_frag(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *extra)
{
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);



	DBG_88E("%s, frag_len =%d\n", __func__, padapter->xmitpriv.frag_len);

	wrqu->frag.value = padapter->xmitpriv.frag_len;
	wrqu->frag.fixed = 0;	/* no auto select */



	return 0;
}

static int rtw_wx_get_retry(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *extra)
{
	wrqu->retry.value = 7;
	wrqu->retry.fixed = 0;	/* no auto select */
	wrqu->retry.disabled = 1;

	return 0;
}

static int rtw_wx_set_enc(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu, char *keybuf)
{
	u32 key, ret = 0;
	u32 keyindex_provided;
	struct ndis_802_11_wep	 wep;
	enum ndis_802_11_auth_mode authmode;

	struct iw_point *erq = &wrqu->encoding;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
	DBG_88E("+rtw_wx_set_enc, flags = 0x%x\n", erq->flags);

	memset(&wep, 0, sizeof(struct ndis_802_11_wep));

	key = erq->flags & IW_ENCODE_INDEX;



	if (erq->flags & IW_ENCODE_DISABLED) {
		DBG_88E("EncryptionDisabled\n");
		padapter->securitypriv.ndisencryptstatus = Ndis802_11EncryptionDisabled;
		padapter->securitypriv.dot11PrivacyAlgrthm = _NO_PRIVACY_;
		padapter->securitypriv.dot118021XGrpPrivacy = _NO_PRIVACY_;
		padapter->securitypriv.dot11AuthAlgrthm = dot11AuthAlgrthm_Open; /* open system */
		authmode = Ndis802_11AuthModeOpen;
		padapter->securitypriv.ndisauthtype = authmode;

		goto exit;
	}

	if (key) {
		if (key > WEP_KEYS)
			return -EINVAL;
		key--;
		keyindex_provided = 1;
	} else {
		keyindex_provided = 0;
		key = padapter->securitypriv.dot11PrivacyKeyIndex;
		DBG_88E("rtw_wx_set_enc, key =%d\n", key);
	}

	/* set authentication mode */
	if (erq->flags & IW_ENCODE_OPEN) {
		DBG_88E("rtw_wx_set_enc():IW_ENCODE_OPEN\n");
		padapter->securitypriv.ndisencryptstatus = Ndis802_11Encryption1Enabled;/* Ndis802_11EncryptionDisabled; */
		padapter->securitypriv.dot11AuthAlgrthm = dot11AuthAlgrthm_Open;
		padapter->securitypriv.dot11PrivacyAlgrthm = _NO_PRIVACY_;
		padapter->securitypriv.dot118021XGrpPrivacy = _NO_PRIVACY_;
		authmode = Ndis802_11AuthModeOpen;
		padapter->securitypriv.ndisauthtype = authmode;
	} else if (erq->flags & IW_ENCODE_RESTRICTED) {
		DBG_88E("rtw_wx_set_enc():IW_ENCODE_RESTRICTED\n");
		padapter->securitypriv.ndisencryptstatus = Ndis802_11Encryption1Enabled;
		padapter->securitypriv.dot11AuthAlgrthm = dot11AuthAlgrthm_Shared;
		padapter->securitypriv.dot11PrivacyAlgrthm = _WEP40_;
		padapter->securitypriv.dot118021XGrpPrivacy = _WEP40_;
		authmode = Ndis802_11AuthModeShared;
		padapter->securitypriv.ndisauthtype = authmode;
	} else {
		DBG_88E("rtw_wx_set_enc():erq->flags = 0x%x\n", erq->flags);

		padapter->securitypriv.ndisencryptstatus = Ndis802_11Encryption1Enabled;/* Ndis802_11EncryptionDisabled; */
		padapter->securitypriv.dot11AuthAlgrthm = dot11AuthAlgrthm_Open; /* open system */
		padapter->securitypriv.dot11PrivacyAlgrthm = _NO_PRIVACY_;
		padapter->securitypriv.dot118021XGrpPrivacy = _NO_PRIVACY_;
		authmode = Ndis802_11AuthModeOpen;
		padapter->securitypriv.ndisauthtype = authmode;
	}

	wep.KeyIndex = key;
	if (erq->length > 0) {
		wep.KeyLength = erq->length <= 5 ? 5 : 13;

		wep.Length = wep.KeyLength + FIELD_OFFSET(struct ndis_802_11_wep, KeyMaterial);
	} else {
		wep.KeyLength = 0;

		if (keyindex_provided == 1) {
			/*  set key_id only, no given KeyMaterial(erq->length == 0). */
			padapter->securitypriv.dot11PrivacyKeyIndex = key;

			DBG_88E("(keyindex_provided == 1), keyid =%d, key_len =%d\n", key, padapter->securitypriv.dot11DefKeylen[key]);

			switch (padapter->securitypriv.dot11DefKeylen[key]) {
			case 5:
				padapter->securitypriv.dot11PrivacyAlgrthm = _WEP40_;
				break;
			case 13:
				padapter->securitypriv.dot11PrivacyAlgrthm = _WEP104_;
				break;
			default:
				padapter->securitypriv.dot11PrivacyAlgrthm = _NO_PRIVACY_;
				break;
			}

			goto exit;
		}
	}

	wep.KeyIndex |= 0x80000000;

	memcpy(wep.KeyMaterial, keybuf, wep.KeyLength);

	if (!rtw_set_802_11_add_wep(padapter, &wep)) {
		if (rf_on == pwrpriv->rf_pwrstate)
			ret = -EOPNOTSUPP;
		goto exit;
	}

exit:



	return ret;
}

static int rtw_wx_get_enc(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu, char *keybuf)
{
	uint key, ret = 0;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct iw_point *erq = &wrqu->encoding;
	struct	mlme_priv	*pmlmepriv = &padapter->mlmepriv;



	if (check_fwstate(pmlmepriv, _FW_LINKED) != true) {
		if (!check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE)) {
			erq->length = 0;
			erq->flags |= IW_ENCODE_DISABLED;
			return 0;
		}
	}

	key = erq->flags & IW_ENCODE_INDEX;

	if (key) {
		if (key > WEP_KEYS)
			return -EINVAL;
		key--;
	} else {
		key = padapter->securitypriv.dot11PrivacyKeyIndex;
	}

	erq->flags = key + 1;

	switch (padapter->securitypriv.ndisencryptstatus) {
	case Ndis802_11EncryptionNotSupported:
	case Ndis802_11EncryptionDisabled:
		erq->length = 0;
		erq->flags |= IW_ENCODE_DISABLED;
		break;
	case Ndis802_11Encryption1Enabled:
		erq->length = padapter->securitypriv.dot11DefKeylen[key];
		if (erq->length) {
			memcpy(keybuf, padapter->securitypriv.dot11DefKey[key].skey, padapter->securitypriv.dot11DefKeylen[key]);

			erq->flags |= IW_ENCODE_ENABLED;

			if (padapter->securitypriv.ndisauthtype == Ndis802_11AuthModeOpen)
				erq->flags |= IW_ENCODE_OPEN;
			else if (padapter->securitypriv.ndisauthtype == Ndis802_11AuthModeShared)
				erq->flags |= IW_ENCODE_RESTRICTED;
		} else {
			erq->length = 0;
			erq->flags |= IW_ENCODE_DISABLED;
		}
		break;
	case Ndis802_11Encryption2Enabled:
	case Ndis802_11Encryption3Enabled:
		erq->length = 16;
		erq->flags |= (IW_ENCODE_ENABLED | IW_ENCODE_OPEN | IW_ENCODE_NOKEY);
		break;
	default:
		erq->length = 0;
		erq->flags |= IW_ENCODE_DISABLED;
		break;
	}


	return ret;
}

static int rtw_wx_get_power(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *extra)
{
	wrqu->power.value = 0;
	wrqu->power.fixed = 0;	/* no auto select */
	wrqu->power.disabled = 1;

	return 0;
}

static int rtw_wx_set_gen_ie(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *extra)
{
	int ret;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);

	ret = rtw_set_wpa_ie(padapter, extra, wrqu->data.length);
	return ret;
}

static int rtw_wx_set_auth(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *extra)
{
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct iw_param *param = (struct iw_param *)&wrqu->param;
	int ret = 0;

	switch (param->flags & IW_AUTH_INDEX) {
	case IW_AUTH_WPA_VERSION:
		break;
	case IW_AUTH_CIPHER_PAIRWISE:

		break;
	case IW_AUTH_CIPHER_GROUP:

		break;
	case IW_AUTH_KEY_MGMT:
		/*
		 *  ??? does not use these parameters
		 */
		break;
	case IW_AUTH_TKIP_COUNTERMEASURES:
		if (param->value) {
			/*  wpa_supplicant is enabling the tkip countermeasure. */
			padapter->securitypriv.btkip_countermeasure = true;
		} else {
			/*  wpa_supplicant is disabling the tkip countermeasure. */
			padapter->securitypriv.btkip_countermeasure = false;
		}
		break;
	case IW_AUTH_DROP_UNENCRYPTED:
		/* HACK:
		 *
		 * wpa_supplicant calls set_wpa_enabled when the driver
		 * is loaded and unloaded, regardless of if WPA is being
		 * used.  No other calls are made which can be used to
		 * determine if encryption will be used or not prior to
		 * association being expected.  If encryption is not being
		 * used, drop_unencrypted is set to false, else true -- we
		 * can use this to determine if the CAP_PRIVACY_ON bit should
		 * be set.
		 */

		if (padapter->securitypriv.ndisencryptstatus == Ndis802_11Encryption1Enabled)
			break;/* it means init value, or using wep, ndisencryptstatus = Ndis802_11Encryption1Enabled, */
					/*  then it needn't reset it; */

		if (param->value) {
			padapter->securitypriv.ndisencryptstatus = Ndis802_11EncryptionDisabled;
			padapter->securitypriv.dot11PrivacyAlgrthm = _NO_PRIVACY_;
			padapter->securitypriv.dot118021XGrpPrivacy = _NO_PRIVACY_;
			padapter->securitypriv.dot11AuthAlgrthm = dot11AuthAlgrthm_Open; /* open system */
			padapter->securitypriv.ndisauthtype = Ndis802_11AuthModeOpen;
		}

		break;
	case IW_AUTH_80211_AUTH_ALG:
		/*
		 *  It's the starting point of a link layer connection using wpa_supplicant
		*/
		if (check_fwstate(&padapter->mlmepriv, _FW_LINKED)) {
			LeaveAllPowerSaveMode(padapter);
			rtw_disassoc_cmd(padapter, 500, false);
			DBG_88E("%s...call rtw_indicate_disconnect\n ", __func__);
			rtw_indicate_disconnect(padapter);
			rtw_free_assoc_resources(padapter, 1);
		}
		ret = wpa_set_auth_algs(dev, (u32)param->value);
		break;
	case IW_AUTH_WPA_ENABLED:
		break;
	case IW_AUTH_RX_UNENCRYPTED_EAPOL:
		break;
	case IW_AUTH_PRIVACY_INVOKED:
		break;
	default:
		return -EOPNOTSUPP;
	}

	return ret;
}

static int rtw_wx_set_enc_ext(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *extra)
{
	char *alg_name;
	u32 param_len;
	struct ieee_param *param = NULL;
	struct iw_point *pencoding = &wrqu->encoding;
	struct iw_encode_ext *pext = (struct iw_encode_ext *)extra;
	int ret = 0;

	param_len = sizeof(struct ieee_param) + pext->key_len;
	param = kzalloc(param_len, GFP_KERNEL);
	if (!param)
		return -ENOMEM;

	param->cmd = IEEE_CMD_SET_ENCRYPTION;
	memset(param->sta_addr, 0xff, ETH_ALEN);

	switch (pext->alg) {
	case IW_ENCODE_ALG_NONE:
		/* todo: remove key */
		/* remove = 1; */
		alg_name = "none";
		break;
	case IW_ENCODE_ALG_WEP:
		alg_name = "WEP";
		break;
	case IW_ENCODE_ALG_TKIP:
		alg_name = "TKIP";
		break;
	case IW_ENCODE_ALG_CCMP:
		alg_name = "CCMP";
		break;
	default:
		return -1;
	}

	strncpy((char *)param->u.crypt.alg, alg_name, IEEE_CRYPT_ALG_NAME_LEN);

	if (pext->ext_flags & IW_ENCODE_EXT_SET_TX_KEY)
		param->u.crypt.set_tx = 1;

	/* cliW: WEP does not have group key
	 * just not checking GROUP key setting
	 */
	if ((pext->alg != IW_ENCODE_ALG_WEP) &&
	    (pext->ext_flags & IW_ENCODE_EXT_GROUP_KEY))
		param->u.crypt.set_tx = 0;

	param->u.crypt.idx = (pencoding->flags & 0x00FF) - 1;

	if (pext->ext_flags & IW_ENCODE_EXT_RX_SEQ_VALID)
		memcpy(param->u.crypt.seq, pext->rx_seq, 8);

	if (pext->key_len) {
		param->u.crypt.key_len = pext->key_len;
		memcpy(param->u.crypt.key, pext + 1, pext->key_len);
	}

	ret =  wpa_set_encryption(dev, param, param_len);

	kfree(param);
	return ret;
}

static int rtw_wx_get_nick(struct net_device *dev,
			   struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	if (extra) {
		wrqu->data.length = 14;
		wrqu->data.flags = 1;
		memcpy(extra, "<WIFI@REALTEK>", 14);
	}

	/* dump debug info here */
	return 0;
}

static int rtw_wx_read32(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu, char *extra)
{
	struct adapter *padapter;
	struct iw_point *p;
	u16 len;
	u32 addr;
	u32 data32;
	u32 bytes;
	u8 *ptmp;

	padapter = (struct adapter *)rtw_netdev_priv(dev);
	p = &wrqu->data;
	len = p->length;
	ptmp = kmalloc(len, GFP_KERNEL);
	if (!ptmp)
		return -ENOMEM;

	if (copy_from_user(ptmp, p->pointer, len)) {
		kfree(ptmp);
		return -EFAULT;
	}

	bytes = 0;
	addr = 0;
	sscanf(ptmp, "%d,%x", &bytes, &addr);

	switch (bytes) {
	case 1:
		data32 = rtw_read8(padapter, addr);
		sprintf(extra, "0x%02X", data32);
		break;
	case 2:
		data32 = rtw_read16(padapter, addr);
		sprintf(extra, "0x%04X", data32);
		break;
	case 4:
		data32 = rtw_read32(padapter, addr);
		sprintf(extra, "0x%08X", data32);
		break;
	default:
		DBG_88E(KERN_INFO "%s: usage> read [bytes],[address(hex)]\n", __func__);
		return -EINVAL;
	}
	DBG_88E(KERN_INFO "%s: addr = 0x%08X data =%s\n", __func__, addr, extra);

	kfree(ptmp);
	return 0;
}

static int rtw_wx_write32(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu, char *extra)
{
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);

	u32 addr;
	u32 data32;
	u32 bytes;

	bytes = 0;
	addr = 0;
	data32 = 0;
	sscanf(extra, "%d,%x,%x", &bytes, &addr, &data32);

	switch (bytes) {
	case 1:
		rtw_write8(padapter, addr, (u8)data32);
		DBG_88E(KERN_INFO "%s: addr = 0x%08X data = 0x%02X\n", __func__, addr, (u8)data32);
		break;
	case 2:
		rtw_write16(padapter, addr, (u16)data32);
		DBG_88E(KERN_INFO "%s: addr = 0x%08X data = 0x%04X\n", __func__, addr, (u16)data32);
		break;
	case 4:
		rtw_write32(padapter, addr, data32);
		DBG_88E(KERN_INFO "%s: addr = 0x%08X data = 0x%08X\n", __func__, addr, data32);
		break;
	default:
		DBG_88E(KERN_INFO "%s: usage> write [bytes],[address(hex)],[data(hex)]\n", __func__);
		return -EINVAL;
	}

	return 0;
}

static int rtw_wx_read_rf(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu, char *extra)
{
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	u32 path, addr, data32;

	path = *(u32 *)extra;
	addr = *((u32 *)extra + 1);
	data32 = rtw_hal_read_rfreg(padapter, path, addr, 0xFFFFF);
	/*
	 * IMPORTANT!!
	 * Only when wireless private ioctl is at odd order,
	 * "extra" would be copied to user space.
	 */
	sprintf(extra, "0x%05x", data32);

	return 0;
}

static int rtw_wx_write_rf(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu, char *extra)
{
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	u32 path, addr, data32;

	path = *(u32 *)extra;
	addr = *((u32 *)extra + 1);
	data32 = *((u32 *)extra + 2);
	rtw_hal_write_rfreg(padapter, path, addr, 0xFFFFF, data32);

	return 0;
}

static int rtw_wx_priv_null(struct net_device *dev, struct iw_request_info *a,
		 union iwreq_data *wrqu, char *b)
{
	return -1;
}

static int dummy(struct net_device *dev, struct iw_request_info *a,
		 union iwreq_data *wrqu, char *b)
{
	return -1;
}

static int rtw_wx_set_channel_plan(struct net_device *dev,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)
{
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	u8 channel_plan_req = (u8)(*((int *)wrqu));

	if (_SUCCESS == rtw_set_chplan_cmd(padapter, channel_plan_req, 1))
		DBG_88E("%s set channel_plan = 0x%02X\n", __func__, pmlmepriv->ChannelPlan);
	else
		return -EPERM;

	return 0;
}

static int rtw_wx_set_mtk_wps_probe_ie(struct net_device *dev,
		struct iw_request_info *a,
		union iwreq_data *wrqu, char *b)
{
	return 0;
}

static int rtw_wx_get_sensitivity(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *buf)
{
	return 0;
}

static int rtw_wx_set_mtk_wps_ie(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *extra)
{
	return 0;
}

/*
 *	For all data larger than 16 octets, we need to use a
 *	pointer to memory allocated in user space.
 */
static  int rtw_drvext_hdl(struct net_device *dev, struct iw_request_info *info,
						union iwreq_data *wrqu, char *extra)
{
	return 0;
}

static void rtw_dbg_mode_hdl(struct adapter *padapter, u32 id, u8 *pdata, u32 len)
{
	struct mp_rw_reg *RegRWStruct;
	struct rf_reg_param *prfreg;
	u8 path;
	u8 offset;
	u32 value;

	DBG_88E("%s\n", __func__);

	switch (id) {
	case GEN_MP_IOCTL_SUBCODE(MP_START):
		DBG_88E("871x_driver is only for normal mode, can't enter mp mode\n");
		break;
	case GEN_MP_IOCTL_SUBCODE(READ_REG):
		RegRWStruct = (struct mp_rw_reg *)pdata;
		switch (RegRWStruct->width) {
		case 1:
			RegRWStruct->value = rtw_read8(padapter, RegRWStruct->offset);
			break;
		case 2:
			RegRWStruct->value = rtw_read16(padapter, RegRWStruct->offset);
			break;
		case 4:
			RegRWStruct->value = rtw_read32(padapter, RegRWStruct->offset);
			break;
		default:
			break;
		}

		break;
	case GEN_MP_IOCTL_SUBCODE(WRITE_REG):
		RegRWStruct = (struct mp_rw_reg *)pdata;
		switch (RegRWStruct->width) {
		case 1:
			rtw_write8(padapter, RegRWStruct->offset, (u8)RegRWStruct->value);
			break;
		case 2:
			rtw_write16(padapter, RegRWStruct->offset, (u16)RegRWStruct->value);
			break;
		case 4:
			rtw_write32(padapter, RegRWStruct->offset, (u32)RegRWStruct->value);
			break;
		default:
			break;
		}

		break;
	case GEN_MP_IOCTL_SUBCODE(READ_RF_REG):

		prfreg = (struct rf_reg_param *)pdata;

		path = (u8)prfreg->path;
		offset = (u8)prfreg->offset;

		value = rtw_hal_read_rfreg(padapter, path, offset, 0xffffffff);

		prfreg->value = value;

		break;
	case GEN_MP_IOCTL_SUBCODE(WRITE_RF_REG):

		prfreg = (struct rf_reg_param *)pdata;

		path = (u8)prfreg->path;
		offset = (u8)prfreg->offset;
		value = prfreg->value;

		rtw_hal_write_rfreg(padapter, path, offset, 0xffffffff, value);

		break;
	case GEN_MP_IOCTL_SUBCODE(TRIGGER_GPIO):
		DBG_88E("==> trigger gpio 0\n");
		rtw_hal_set_hwreg(padapter, HW_VAR_TRIGGER_GPIO_0, NULL);
		break;
	case GEN_MP_IOCTL_SUBCODE(GET_WIFI_STATUS):
		*pdata = rtw_hal_sreset_get_wifi_status(padapter);
		break;
	default:
		break;
	}
}

static int rtw_mp_ioctl_hdl(struct net_device *dev, struct iw_request_info *info,
						union iwreq_data *wrqu, char *extra)
{
	int ret = 0;
	u32 BytesRead, BytesWritten, BytesNeeded;
	struct oid_par_priv	oid_par;
	struct mp_ioctl_handler	*phandler;
	struct mp_ioctl_param	*poidparam;
	uint status = 0;
	u16 len;
	u8 *pparmbuf = NULL, bset;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct iw_point *p = &wrqu->data;

	if ((!p->length) || (!p->pointer)) {
		ret = -EINVAL;
		goto _rtw_mp_ioctl_hdl_exit;
	}
	pparmbuf = NULL;
	bset = (u8)(p->flags & 0xFFFF);
	len = p->length;
	pparmbuf = kmalloc(len, GFP_KERNEL);
	if (!pparmbuf) {
		ret = -ENOMEM;
		goto _rtw_mp_ioctl_hdl_exit;
	}

	if (copy_from_user(pparmbuf, p->pointer, len)) {
		ret = -EFAULT;
		goto _rtw_mp_ioctl_hdl_exit;
	}

	poidparam = (struct mp_ioctl_param *)pparmbuf;

	if (poidparam->subcode >= MAX_MP_IOCTL_SUBCODE) {
		ret = -EINVAL;
		goto _rtw_mp_ioctl_hdl_exit;
	}

	if (padapter->registrypriv.mp_mode == 1) {
		phandler = mp_ioctl_hdl + poidparam->subcode;

		if ((phandler->paramsize != 0) && (poidparam->len < phandler->paramsize)) {
			ret = -EINVAL;
			goto _rtw_mp_ioctl_hdl_exit;
		}

		if (phandler->handler) {
			oid_par.adapter_context = padapter;
			oid_par.oid = phandler->oid;
			oid_par.information_buf = poidparam->data;
			oid_par.information_buf_len = poidparam->len;
			oid_par.dbg = 0;

			BytesWritten = 0;
			BytesNeeded = 0;

			if (bset) {
				oid_par.bytes_rw = &BytesRead;
				oid_par.bytes_needed = &BytesNeeded;
				oid_par.type_of_oid = SET_OID;
			} else {
				oid_par.bytes_rw = &BytesWritten;
				oid_par.bytes_needed = &BytesNeeded;
				oid_par.type_of_oid = QUERY_OID;
			}

			status = phandler->handler(&oid_par);
		} else {
			DBG_88E("rtw_mp_ioctl_hdl(): err!, subcode =%d, oid =%d, handler =%p\n",
				poidparam->subcode, phandler->oid, phandler->handler);
			ret = -EFAULT;
			goto _rtw_mp_ioctl_hdl_exit;
		}
	} else {
		rtw_dbg_mode_hdl(padapter, poidparam->subcode, poidparam->data, poidparam->len);
	}

	if (bset == 0x00) {/* query info */
		if (copy_to_user(p->pointer, pparmbuf, len))
			ret = -EFAULT;
	}

	if (status) {
		ret = -EFAULT;
		goto _rtw_mp_ioctl_hdl_exit;
	}

_rtw_mp_ioctl_hdl_exit:

	kfree(pparmbuf);
	return ret;
}

static int rtw_get_ap_info(struct net_device *dev,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)
{
	int ret = 0;
	u32 cnt = 0, wpa_ielen;
	struct list_head *plist, *phead;
	unsigned char *pbuf;
	u8 bssid[ETH_ALEN];
	char data[32];
	struct wlan_network *pnetwork = NULL;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct __queue *queue = &pmlmepriv->scanned_queue;
	struct iw_point *pdata = &wrqu->data;

	DBG_88E("+rtw_get_aplist_info\n");

	if (padapter->bDriverStopped || !pdata) {
		ret = -EINVAL;
		goto exit;
	}

	while ((check_fwstate(pmlmepriv, (_FW_UNDER_SURVEY | _FW_UNDER_LINKING)))) {
		msleep(30);
		cnt++;
		if (cnt > 100)
			break;
	}
	pdata->flags = 0;
	if (pdata->length >= 32) {
		if (copy_from_user(data, pdata->pointer, 32)) {
			ret = -EINVAL;
			goto exit;
		}
	} else {
		ret = -EINVAL;
		goto exit;
	}

	spin_lock_bh(&pmlmepriv->scanned_queue.lock);

	phead = get_list_head(queue);
	plist = phead->next;

	while (phead != plist) {
		pnetwork = container_of(plist, struct wlan_network, list);

		if (!mac_pton(data, bssid)) {
			DBG_88E("Invalid BSSID '%s'.\n", (u8 *)data);
			spin_unlock_bh(&pmlmepriv->scanned_queue.lock);
			return -EINVAL;
		}

		if (!memcmp(bssid, pnetwork->network.MacAddress, ETH_ALEN)) {
			/* BSSID match, then check if supporting wpa/wpa2 */
			DBG_88E("BSSID:%pM\n", (bssid));

			pbuf = rtw_get_wpa_ie(&pnetwork->network.IEs[12], &wpa_ielen, pnetwork->network.IELength - 12);
			if (pbuf && (wpa_ielen > 0)) {
				pdata->flags = 1;
				break;
			}

			pbuf = rtw_get_wpa2_ie(&pnetwork->network.IEs[12], &wpa_ielen, pnetwork->network.IELength - 12);
			if (pbuf && (wpa_ielen > 0)) {
				pdata->flags = 2;
				break;
			}
		}

		plist = plist->next;
	}

	spin_unlock_bh(&pmlmepriv->scanned_queue.lock);

	if (pdata->length >= 34) {
		if (copy_to_user(pdata->pointer + 32, (u8 *)&pdata->flags, 1)) {
			ret = -EINVAL;
			goto exit;
		}
	}

exit:

	return ret;
}

static int rtw_set_pid(struct net_device *dev,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)
{
	int ret = 0;
	struct adapter *padapter = rtw_netdev_priv(dev);
	int *pdata = (int *)wrqu;
	int selector;

	if (padapter->bDriverStopped || !pdata) {
		ret = -EINVAL;
		goto exit;
	}

	selector = *pdata;
	if (selector < 3 && selector >= 0) {
		padapter->pid[selector] = *(pdata + 1);
		ui_pid[selector] = *(pdata + 1);
		DBG_88E("%s set pid[%d] =%d\n", __func__, selector, padapter->pid[selector]);
	} else {
		DBG_88E("%s selector %d error\n", __func__, selector);
	}
exit:
	return ret;
}

static int rtw_wps_start(struct net_device *dev,
			 struct iw_request_info *info,
			 union iwreq_data *wrqu, char *extra)
{
	int ret = 0;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct iw_point *pdata = &wrqu->data;
	u32   u32wps_start = 0;

	if (!pdata)
		return -EINVAL;
	ret = copy_from_user((void *)&u32wps_start, pdata->pointer, 4);
	if (ret) {
		ret = -EINVAL;
		goto exit;
	}

	if (padapter->bDriverStopped) {
		ret = -EINVAL;
		goto exit;
	}

	if (u32wps_start == 0)
		u32wps_start = *extra;

	DBG_88E("[%s] wps_start = %d\n", __func__, u32wps_start);

	if (u32wps_start == 1) /*  WPS Start */
		rtw_led_control(padapter, LED_CTL_START_WPS);
	else if (u32wps_start == 2) /*  WPS Stop because of wps success */
		rtw_led_control(padapter, LED_CTL_STOP_WPS);
	else if (u32wps_start == 3) /*  WPS Stop because of wps fail */
		rtw_led_control(padapter, LED_CTL_STOP_WPS_FAIL);

exit:
	return ret;
}

#ifdef CONFIG_88EU_P2P
static int rtw_wext_p2p_enable(struct net_device *dev,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)
{
	int ret = 0;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	enum P2P_ROLE init_role = P2P_ROLE_DISABLE;

	if (*extra == '0')
		init_role = P2P_ROLE_DISABLE;
	else if (*extra == '1')
		init_role = P2P_ROLE_DEVICE;
	else if (*extra == '2')
		init_role = P2P_ROLE_CLIENT;
	else if (*extra == '3')
		init_role = P2P_ROLE_GO;

	if (_FAIL == rtw_p2p_enable(padapter, init_role)) {
		ret = -EFAULT;
		goto exit;
	}

	/* set channel/bandwidth */
	if (init_role != P2P_ROLE_DISABLE) {
		u8 channel, ch_offset;
		u16 bwmode;

		if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_LISTEN)) {
			/*	Stay at the listen state and wait for discovery. */
			channel = pwdinfo->listen_channel;
			pwdinfo->operating_channel = pwdinfo->listen_channel;
			ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
			bwmode = HT_CHANNEL_WIDTH_20;
		} else {
			pwdinfo->operating_channel = pmlmeext->cur_channel;

			channel = pwdinfo->operating_channel;
			ch_offset = pmlmeext->cur_ch_offset;
			bwmode = pmlmeext->cur_bwmode;
		}

		set_channel_bwmode(padapter, channel, ch_offset, bwmode);
	}

exit:
	return ret;
}

static int rtw_p2p_set_go_nego_ssid(struct net_device *dev,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)
{
	int ret = 0;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;

	DBG_88E("[%s] ssid = %s, len = %zu\n", __func__, extra, strlen(extra));
	memcpy(pwdinfo->nego_ssid, extra, strlen(extra));
	pwdinfo->nego_ssidlen = strlen(extra);

	return ret;
}

static int rtw_p2p_set_intent(struct net_device *dev,
			      struct iw_request_info *info,
			      union iwreq_data *wrqu, char *extra)
{
	int ret = 0;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;
	u8 intent = pwdinfo->intent;

	switch (wrqu->data.length) {
	case 1:
		intent = extra[0] - '0';
		break;
	case 2:
		intent = str_2char2num(extra[0], extra[1]);
		break;
	}
	if (intent <= 15)
		pwdinfo->intent = intent;
	else
		ret = -1;
	DBG_88E("[%s] intent = %d\n", __func__, intent);
	return ret;
}

static int rtw_p2p_set_listen_ch(struct net_device *dev,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)
{
	int ret = 0;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;
	u8 listen_ch = pwdinfo->listen_channel;	/*	Listen channel number */

	switch (wrqu->data.length) {
	case 1:
		listen_ch = extra[0] - '0';
		break;
	case 2:
		listen_ch = str_2char2num(extra[0], extra[1]);
		break;
	}

	if ((listen_ch == 1) || (listen_ch == 6) || (listen_ch == 11)) {
		pwdinfo->listen_channel = listen_ch;
		set_channel_bwmode(padapter, pwdinfo->listen_channel, HAL_PRIME_CHNL_OFFSET_DONT_CARE, HT_CHANNEL_WIDTH_20);
	} else {
		ret = -1;
	}

	DBG_88E("[%s] listen_ch = %d\n", __func__, pwdinfo->listen_channel);

	return ret;
}

static int rtw_p2p_set_op_ch(struct net_device *dev,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)
{
/*	Commented by Albert 20110524 */
/*	This function is used to set the operating channel if the driver will become the group owner */

	int ret = 0;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;
	u8 op_ch = pwdinfo->operating_channel;	/*	Operating channel number */

	switch (wrqu->data.length) {
	case 1:
		op_ch = extra[0] - '0';
		break;
	case 2:
		op_ch = str_2char2num(extra[0], extra[1]);
		break;
	}

	if (op_ch > 0)
		pwdinfo->operating_channel = op_ch;
	else
		ret = -1;

	DBG_88E("[%s] op_ch = %d\n", __func__, pwdinfo->operating_channel);

	return ret;
}

static int rtw_p2p_profilefound(struct net_device *dev,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)
{
	int ret = 0;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;

	/*	Comment by Albert 2010/10/13 */
	/*	Input data format: */
	/*	Ex:  0 */
	/*	Ex:  1XX:XX:XX:XX:XX:XXYYSSID */
	/*	0 => Reflush the profile record list. */
	/*	1 => Add the profile list */
	/*	XX:XX:XX:XX:XX:XX => peer's MAC Address (ex: 00:E0:4C:00:00:01) */
	/*	YY => SSID Length */
	/*	SSID => SSID for persistence group */

	DBG_88E("[%s] In value = %s, len = %d\n", __func__, extra, wrqu->data.length - 1);

	/*	The upper application should pass the SSID to driver by using this rtw_p2p_profilefound function. */
	if (!rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE)) {
		if (extra[0] == '0') {
			/*	Remove all the profile information of wifidirect_info structure. */
			memset(&pwdinfo->profileinfo[0], 0x00, sizeof(struct profile_info) * P2P_MAX_PERSISTENT_GROUP_NUM);
			pwdinfo->profileindex = 0;
		} else {
			if (pwdinfo->profileindex >= P2P_MAX_PERSISTENT_GROUP_NUM) {
				ret = -1;
			} else {
				int jj, kk;

				/*	Add this profile information into pwdinfo->profileinfo */
				/*	Ex:  1XX:XX:XX:XX:XX:XXYYSSID */
				for (jj = 0, kk = 1; jj < ETH_ALEN; jj++, kk += 3)
					pwdinfo->profileinfo[pwdinfo->profileindex].peermac[jj] = key_2char2num(extra[kk], extra[kk + 1]);

				pwdinfo->profileinfo[pwdinfo->profileindex].ssidlen = (extra[18] - '0') * 10 + (extra[19] - '0');
				memcpy(pwdinfo->profileinfo[pwdinfo->profileindex].ssid, &extra[20], pwdinfo->profileinfo[pwdinfo->profileindex].ssidlen);
				pwdinfo->profileindex++;
			}
		}
	}

	return ret;
}

static int rtw_p2p_setDN(struct net_device *dev,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)
{
	int ret = 0;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;

	DBG_88E("[%s] %s %d\n", __func__, extra, wrqu->data.length - 1);
	memset(pwdinfo->device_name, 0x00, WPS_MAX_DEVICE_NAME_LEN);
	memcpy(pwdinfo->device_name, extra, wrqu->data.length - 1);
	pwdinfo->device_name_len = wrqu->data.length - 1;

	return ret;
}

static int rtw_p2p_get_status(struct net_device *dev,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)
{
	int ret = 0;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;

	if (padapter->bShowGetP2PState)
		DBG_88E("[%s] Role = %d, Status = %d, peer addr = %.2X:%.2X:%.2X:%.2X:%.2X:%.2X\n", __func__, rtw_p2p_role(pwdinfo), rtw_p2p_state(pwdinfo),
			pwdinfo->p2p_peer_interface_addr[0], pwdinfo->p2p_peer_interface_addr[1], pwdinfo->p2p_peer_interface_addr[2],
			pwdinfo->p2p_peer_interface_addr[3], pwdinfo->p2p_peer_interface_addr[4], pwdinfo->p2p_peer_interface_addr[5]);

	/*	Commented by Albert 2010/10/12 */
	/*	Because of the output size limitation, I had removed the "Role" information. */
	/*	About the "Role" information, we will use the new private IOCTL to get the "Role" information. */
	sprintf(extra, "\n\nStatus =%.2d\n", rtw_p2p_state(pwdinfo));
	wrqu->data.length = strlen(extra);

	return ret;
}

/*	Commented by Albert 20110520 */
/*	This function will return the config method description */
/*	This config method description will show us which config method the remote P2P device is intended to use */
/*	by sending the provisioning discovery request frame. */

static int rtw_p2p_get_req_cm(struct net_device *dev,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)
{
	int ret = 0;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;

	sprintf(extra, "\n\nCM =%s\n", pwdinfo->rx_prov_disc_info.strconfig_method_desc_of_prov_disc_req);
	wrqu->data.length = strlen(extra);
	return ret;
}

static int rtw_p2p_get_role(struct net_device *dev,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)
{
	int ret = 0;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;

	DBG_88E("[%s] Role = %d, Status = %d, peer addr = %.2X:%.2X:%.2X:%.2X:%.2X:%.2X\n", __func__, rtw_p2p_role(pwdinfo), rtw_p2p_state(pwdinfo),
			pwdinfo->p2p_peer_interface_addr[0], pwdinfo->p2p_peer_interface_addr[1], pwdinfo->p2p_peer_interface_addr[2],
			pwdinfo->p2p_peer_interface_addr[3], pwdinfo->p2p_peer_interface_addr[4], pwdinfo->p2p_peer_interface_addr[5]);

	sprintf(extra, "\n\nRole =%.2d\n", rtw_p2p_role(pwdinfo));
	wrqu->data.length = strlen(extra);
	return ret;
}

static int rtw_p2p_get_peer_ifaddr(struct net_device *dev,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)
{
	int ret = 0;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;

	DBG_88E("[%s] Role = %d, Status = %d, peer addr = %pM\n", __func__,
		rtw_p2p_role(pwdinfo), rtw_p2p_state(pwdinfo),
		pwdinfo->p2p_peer_interface_addr);
	sprintf(extra, "\nMAC %pM",
		pwdinfo->p2p_peer_interface_addr);
	wrqu->data.length = strlen(extra);
	return ret;
}

static int rtw_p2p_get_peer_devaddr(struct net_device *dev,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)

{
	int ret = 0;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;

	DBG_88E("[%s] Role = %d, Status = %d, peer addr = %pM\n", __func__,
		rtw_p2p_role(pwdinfo), rtw_p2p_state(pwdinfo),
		pwdinfo->rx_prov_disc_info.peerDevAddr);
	sprintf(extra, "\n%pM",
		pwdinfo->rx_prov_disc_info.peerDevAddr);
	wrqu->data.length = strlen(extra);
	return ret;
}

static int rtw_p2p_get_peer_devaddr_by_invitation(struct net_device *dev,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)

{
	int ret = 0;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;

	DBG_88E("[%s] Role = %d, Status = %d, peer addr = %pM\n",
		__func__, rtw_p2p_role(pwdinfo), rtw_p2p_state(pwdinfo),
		pwdinfo->p2p_peer_device_addr);
	sprintf(extra, "\nMAC %pM",
		pwdinfo->p2p_peer_device_addr);
	wrqu->data.length = strlen(extra);
	return ret;
}

static int rtw_p2p_get_groupid(struct net_device *dev,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)

{
	int ret = 0;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;

	sprintf(extra, "\n%.2X:%.2X:%.2X:%.2X:%.2X:%.2X %s",
		pwdinfo->groupid_info.go_device_addr[0], pwdinfo->groupid_info.go_device_addr[1],
		pwdinfo->groupid_info.go_device_addr[2], pwdinfo->groupid_info.go_device_addr[3],
		pwdinfo->groupid_info.go_device_addr[4], pwdinfo->groupid_info.go_device_addr[5],
		pwdinfo->groupid_info.ssid);
	wrqu->data.length = strlen(extra);
	return ret;
}

static int rtw_p2p_get_op_ch(struct net_device *dev,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)

{
	int ret = 0;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;

	DBG_88E("[%s] Op_ch = %02x\n", __func__, pwdinfo->operating_channel);

	sprintf(extra, "\n\nOp_ch =%.2d\n", pwdinfo->operating_channel);
	wrqu->data.length = strlen(extra);
	return ret;
}

static int rtw_p2p_get_wps_configmethod(struct net_device *dev,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)
{
	int ret = 0;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	u8 peerMAC[ETH_ALEN] = {0x00};
	int jj, kk;
	u8 peerMACStr[17] = {0x00};
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	struct list_head *plist, *phead;
	struct __queue *queue	= &pmlmepriv->scanned_queue;
	struct	wlan_network	*pnetwork = NULL;
	u8 blnMatch = 0;
	u16	attr_content = 0;
	uint attr_contentlen = 0;
	/* 6 is the string "wpsCM =", 17 is the MAC addr, we have to clear it at wrqu->data.pointer */
	u8 attr_content_str[6 + 17] = {0x00};

	/*	Commented by Albert 20110727 */
	/*	The input data is the MAC address which the application wants to know its WPS config method. */
	/*	After knowing its WPS config method, the application can decide the config method for provisioning discovery. */
	/*	Format: iwpriv wlanx p2p_get_wpsCM 00:E0:4C:00:00:05 */

	DBG_88E("[%s] data = %s\n", __func__, (char *)extra);
	if (copy_from_user(peerMACStr, wrqu->data.pointer + 6, 17))
		return -EFAULT;

	for (jj = 0, kk = 0; jj < ETH_ALEN; jj++, kk += 3)
		peerMAC[jj] = key_2char2num(peerMACStr[kk], peerMACStr[kk + 1]);

	spin_lock_bh(&pmlmepriv->scanned_queue.lock);

	phead = get_list_head(queue);
	plist = phead->next;

	while (phead != plist) {
		pnetwork = container_of(plist, struct wlan_network, list);
		if (!memcmp(pnetwork->network.MacAddress, peerMAC, ETH_ALEN)) {
			u8 *wpsie;
			uint wpsie_len = 0;
			__be16 be_tmp;

			/*  The mac address is matched. */
			wpsie = rtw_get_wps_ie(&pnetwork->network.IEs[12], pnetwork->network.IELength - 12, NULL, &wpsie_len);
			if (wpsie) {
				rtw_get_wps_attr_content(wpsie, wpsie_len, WPS_ATTR_CONF_METHOD, (u8 *)&be_tmp, &attr_contentlen);
				if (attr_contentlen) {
					attr_content = be16_to_cpu(be_tmp);
					sprintf(attr_content_str, "\n\nM =%.4d", attr_content);
					blnMatch = 1;
				}
			}
			break;
		}
		plist = plist->next;
	}

	spin_unlock_bh(&pmlmepriv->scanned_queue.lock);

	if (!blnMatch)
		sprintf(attr_content_str, "\n\nM = 0000");

	if (copy_to_user(wrqu->data.pointer, attr_content_str, 6 + 17))
		return -EFAULT;
	return ret;
}

static int rtw_p2p_get_go_device_address(struct net_device *dev,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)
{
	int ret = 0;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	u8 peerMAC[ETH_ALEN] = {0x00};
	int jj, kk;
	u8 peerMACStr[17] = {0x00};
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	struct list_head *plist, *phead;
	struct __queue *queue	= &pmlmepriv->scanned_queue;
	struct	wlan_network	*pnetwork = NULL;
	u8 blnMatch = 0;
	u8 *p2pie;
	uint p2pielen = 0, attr_contentlen = 0;
	u8 attr_content[100] = {0x00};

	u8 go_devadd_str[100 + 10] = {0x00};
	/*  +10 is for the str "go_devadd =", we have to clear it at wrqu->data.pointer */

	/*	Commented by Albert 20121209 */
	/*	The input data is the GO's interface address which the application wants to know its device address. */
	/*	Format: iwpriv wlanx p2p_get2 go_devadd = 00:E0:4C:00:00:05 */

	DBG_88E("[%s] data = %s\n", __func__, (char *)extra);
	if (copy_from_user(peerMACStr, wrqu->data.pointer + 10, 17))
		return -EFAULT;

	for (jj = 0, kk = 0; jj < ETH_ALEN; jj++, kk += 3)
		peerMAC[jj] = key_2char2num(peerMACStr[kk], peerMACStr[kk + 1]);

	spin_lock_bh(&pmlmepriv->scanned_queue.lock);

	phead = get_list_head(queue);
	plist = phead->next;

	while (phead != plist) {
		pnetwork = container_of(plist, struct wlan_network, list);
		if (!memcmp(pnetwork->network.MacAddress, peerMAC, ETH_ALEN)) {
			/*	Commented by Albert 2011/05/18 */
			/*	Match the device address located in the P2P IE */
			/*	This is for the case that the P2P device address is not the same as the P2P interface address. */

			p2pie = rtw_get_p2p_ie(&pnetwork->network.IEs[12], pnetwork->network.IELength - 12, NULL, &p2pielen);
			if (p2pie) {
				while (p2pie) {
					/*	The P2P Device ID attribute is included in the Beacon frame. */
					/*	The P2P Device Info attribute is included in the probe response frame. */

					memset(attr_content, 0x00, 100);
					if (rtw_get_p2p_attr_content(p2pie, p2pielen, P2P_ATTR_DEVICE_ID, attr_content, &attr_contentlen)) {
						/*	Handle the P2P Device ID attribute of Beacon first */
						blnMatch = 1;
						break;
					} else if (rtw_get_p2p_attr_content(p2pie, p2pielen, P2P_ATTR_DEVICE_INFO, attr_content, &attr_contentlen)) {
						/*	Handle the P2P Device Info attribute of probe response */
						blnMatch = 1;
						break;
					}

					/* Get the next P2P IE */
					p2pie = rtw_get_p2p_ie(p2pie + p2pielen, pnetwork->network.IELength - 12 - (p2pie - &pnetwork->network.IEs[12] + p2pielen), NULL, &p2pielen);
				}
			}
	     }

		plist = plist->next;
	}

	spin_unlock_bh(&pmlmepriv->scanned_queue.lock);

	if (!blnMatch)
		sprintf(go_devadd_str, "\n\ndev_add = NULL");
	else
		sprintf(go_devadd_str, "\ndev_add =%.2X:%.2X:%.2X:%.2X:%.2X:%.2X",
			attr_content[0], attr_content[1], attr_content[2], attr_content[3], attr_content[4], attr_content[5]);

	if (copy_to_user(wrqu->data.pointer, go_devadd_str, 10 + 17))
		return -EFAULT;
	return ret;
}

static int rtw_p2p_get_device_type(struct net_device *dev,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)
{
	int ret = 0;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	u8 peerMAC[ETH_ALEN] = {0x00};
	int jj, kk;
	u8 peerMACStr[17] = {0x00};
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	struct list_head *plist, *phead;
	struct __queue *queue	= &pmlmepriv->scanned_queue;
	struct	wlan_network	*pnetwork = NULL;
	u8 blnMatch = 0;
	u8 dev_type[8] = {0x00};
	uint dev_type_len = 0;
	u8 dev_type_str[17 + 9] = {0x00};	/*  +9 is for the str "dev_type =", we have to clear it at wrqu->data.pointer */

	/*	Commented by Albert 20121209 */
	/*	The input data is the MAC address which the application wants to know its device type. */
	/*	Such user interface could know the device type. */
	/*	Format: iwpriv wlanx p2p_get2 dev_type = 00:E0:4C:00:00:05 */

	DBG_88E("[%s] data = %s\n", __func__, (char *)extra);
	if (copy_from_user(peerMACStr, wrqu->data.pointer + 9, 17))
		return -EFAULT;

	for (jj = 0, kk = 0; jj < ETH_ALEN; jj++, kk += 3)
		peerMAC[jj] = key_2char2num(peerMACStr[kk], peerMACStr[kk + 1]);

	spin_lock_bh(&pmlmepriv->scanned_queue.lock);

	phead = get_list_head(queue);
	plist = phead->next;

	while (phead != plist) {
		pnetwork = container_of(plist, struct wlan_network, list);
		if (!memcmp(pnetwork->network.MacAddress, peerMAC, ETH_ALEN)) {
			u8 *wpsie;
			uint wpsie_len = 0;

		/*	The mac address is matched. */

			wpsie = rtw_get_wps_ie(&pnetwork->network.IEs[12],
					       pnetwork->network.IELength - 12,
					       NULL, &wpsie_len);
			if (wpsie) {
				rtw_get_wps_attr_content(wpsie, wpsie_len, WPS_ATTR_PRIMARY_DEV_TYPE, dev_type, &dev_type_len);
				if (dev_type_len) {
					u16	type = 0;
					__be16 be_tmp;

					memcpy(&be_tmp, dev_type, 2);
					type = be16_to_cpu(be_tmp);
					sprintf(dev_type_str, "\n\nN =%.2d", type);
					blnMatch = 1;
				}
			}
			break;
	     }

		plist = plist->next;
	}

	spin_unlock_bh(&pmlmepriv->scanned_queue.lock);

	if (!blnMatch)
		sprintf(dev_type_str, "\n\nN = 00");

	if (copy_to_user(wrqu->data.pointer, dev_type_str, 9 + 17)) {
		return -EFAULT;
	}

	return ret;
}

static int rtw_p2p_get_device_name(struct net_device *dev,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)
{
	int ret = 0;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	u8 peerMAC[ETH_ALEN] = {0x00};
	int jj, kk;
	u8 peerMACStr[17] = {0x00};
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	struct list_head *plist, *phead;
	struct __queue *queue	= &pmlmepriv->scanned_queue;
	struct	wlan_network	*pnetwork = NULL;
	u8 blnMatch = 0;
	u8 dev_name[WPS_MAX_DEVICE_NAME_LEN] = {0x00};
	uint dev_len = 0;
	u8 dev_name_str[WPS_MAX_DEVICE_NAME_LEN + 5] = {0x00};	/*  +5 is for the str "devN =", we have to clear it at wrqu->data.pointer */

	/*	Commented by Albert 20121225 */
	/*	The input data is the MAC address which the application wants to know its device name. */
	/*	Such user interface could show peer device's device name instead of ssid. */
	/*	Format: iwpriv wlanx p2p_get2 devN = 00:E0:4C:00:00:05 */

	DBG_88E("[%s] data = %s\n", __func__, (char *)extra);
	if (copy_from_user(peerMACStr, wrqu->data.pointer + 5, 17))
		return -EFAULT;

	for (jj = 0, kk = 0; jj < ETH_ALEN; jj++, kk += 3)
		peerMAC[jj] = key_2char2num(peerMACStr[kk], peerMACStr[kk + 1]);

	spin_lock_bh(&pmlmepriv->scanned_queue.lock);

	phead = get_list_head(queue);
	plist = phead->next;

	while (phead != plist) {
		pnetwork = container_of(plist, struct wlan_network, list);
		if (!memcmp(pnetwork->network.MacAddress, peerMAC, ETH_ALEN)) {
			u8 *wpsie;
			uint wpsie_len = 0;

			/*	The mac address is matched. */
			wpsie = rtw_get_wps_ie(&pnetwork->network.IEs[12], pnetwork->network.IELength - 12, NULL, &wpsie_len);
			if (wpsie) {
				rtw_get_wps_attr_content(wpsie, wpsie_len, WPS_ATTR_DEVICE_NAME, dev_name, &dev_len);
				if (dev_len) {
					sprintf(dev_name_str, "\n\nN =%s", dev_name);
					blnMatch = 1;
				}
			}
			break;
		}

		plist = plist->next;
	}

	spin_unlock_bh(&pmlmepriv->scanned_queue.lock);

	if (!blnMatch)
		sprintf(dev_name_str, "\n\nN = 0000");

	if (copy_to_user(wrqu->data.pointer, dev_name_str, 5 + ((dev_len > 17) ? dev_len : 17)))
		return -EFAULT;
	return ret;
}

static int rtw_p2p_get_invitation_procedure(struct net_device *dev,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)
{
	int ret = 0;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	u8 peerMAC[ETH_ALEN] = {0x00};
	int jj, kk;
	u8 peerMACStr[17] = {0x00};
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	struct list_head *plist, *phead;
	struct __queue *queue	= &pmlmepriv->scanned_queue;
	struct	wlan_network	*pnetwork = NULL;
	u8 blnMatch = 0;
	u8 *p2pie;
	uint p2pielen = 0, attr_contentlen = 0;
	u8 attr_content[2] = {0x00};

	u8 inv_proc_str[17 + 8] = {0x00};
	/*  +8 is for the str "InvProc =", we have to clear it at wrqu->data.pointer */

	/*	Commented by Ouden 20121226 */
	/*	The application wants to know P2P initiation procedure is supported or not. */
	/*	Format: iwpriv wlanx p2p_get2 InvProc = 00:E0:4C:00:00:05 */

	DBG_88E("[%s] data = %s\n", __func__, (char *)extra);
	if (copy_from_user(peerMACStr, wrqu->data.pointer + 8, 17))
		return -EFAULT;

	for (jj = 0, kk = 0; jj < ETH_ALEN; jj++, kk += 3)
		peerMAC[jj] = key_2char2num(peerMACStr[kk], peerMACStr[kk + 1]);

	spin_lock_bh(&pmlmepriv->scanned_queue.lock);

	phead = get_list_head(queue);
	plist = phead->next;

	while (phead != plist) {
		pnetwork = container_of(plist, struct wlan_network, list);
		if (!memcmp(pnetwork->network.MacAddress, peerMAC, ETH_ALEN)) {
			/*	Commented by Albert 20121226 */
			/*	Match the device address located in the P2P IE */
			/*	This is for the case that the P2P device address is not the same as the P2P interface address. */

			p2pie = rtw_get_p2p_ie(&pnetwork->network.IEs[12], pnetwork->network.IELength - 12, NULL, &p2pielen);
			if (p2pie) {
				while (p2pie) {
					if (rtw_get_p2p_attr_content(p2pie, p2pielen, P2P_ATTR_CAPABILITY, attr_content, &attr_contentlen)) {
						/*	Handle the P2P capability attribute */
						blnMatch = 1;
						break;
					}

					/* Get the next P2P IE */
					p2pie = rtw_get_p2p_ie(p2pie + p2pielen, pnetwork->network.IELength - 12 - (p2pie - &pnetwork->network.IEs[12] + p2pielen), NULL, &p2pielen);
				}
			}
		}
		plist = plist->next;
	}

	spin_unlock_bh(&pmlmepriv->scanned_queue.lock);

	if (!blnMatch) {
		sprintf(inv_proc_str, "\nIP =-1");
	} else {
		if (attr_content[0] & 0x20)
			sprintf(inv_proc_str, "\nIP = 1");
		else
			sprintf(inv_proc_str, "\nIP = 0");
	}
	if (copy_to_user(wrqu->data.pointer, inv_proc_str, 8 + 17))
		return -EFAULT;
	return ret;
}

static int rtw_p2p_connect(struct net_device *dev,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)
{
	int ret = 0;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;
	u8 peerMAC[ETH_ALEN] = {0x00};
	int jj, kk;
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	struct list_head *plist, *phead;
	struct __queue *queue	= &pmlmepriv->scanned_queue;
	struct	wlan_network	*pnetwork = NULL;
	uint uintPeerChannel = 0;

	/*	Commented by Albert 20110304 */
	/*	The input data contains two informations. */
	/*	1. First information is the MAC address which wants to formate with */
	/*	2. Second information is the WPS PINCode or "pbc" string for push button method */
	/*	Format: 00:E0:4C:00:00:05 */
	/*	Format: 00:E0:4C:00:00:05 */

	DBG_88E("[%s] data = %s\n", __func__, extra);

	if (pwdinfo->p2p_state == P2P_STATE_NONE) {
		DBG_88E("[%s] WiFi Direct is disable!\n", __func__);
		return ret;
	}

	if (pwdinfo->ui_got_wps_info == P2P_NO_WPSINFO)
		return -1;

	for (jj = 0, kk = 0; jj < ETH_ALEN; jj++, kk += 3)
		peerMAC[jj] = key_2char2num(extra[kk], extra[kk + 1]);

	spin_lock_bh(&pmlmepriv->scanned_queue.lock);

	phead = get_list_head(queue);
	plist = phead->next;

	while (phead != plist) {
		pnetwork = container_of(plist, struct wlan_network, list);
		if (!memcmp(pnetwork->network.MacAddress, peerMAC, ETH_ALEN)) {
			uintPeerChannel = pnetwork->network.Configuration.DSConfig;
			break;
		}

		plist = plist->next;
	}

	spin_unlock_bh(&pmlmepriv->scanned_queue.lock);

	if (uintPeerChannel) {
		memset(&pwdinfo->nego_req_info, 0x00, sizeof(struct tx_nego_req_info));
		memset(&pwdinfo->groupid_info, 0x00, sizeof(struct group_id_info));

		pwdinfo->nego_req_info.peer_channel_num[0] = uintPeerChannel;
		memcpy(pwdinfo->nego_req_info.peerDevAddr, pnetwork->network.MacAddress, ETH_ALEN);
		pwdinfo->nego_req_info.benable = true;

		_cancel_timer_ex(&pwdinfo->restore_p2p_state_timer);
		if (rtw_p2p_state(pwdinfo) != P2P_STATE_GONEGO_OK) {
			/*	Restore to the listen state if the current p2p state is not nego OK */
			rtw_p2p_set_state(pwdinfo, P2P_STATE_LISTEN);
		}

		rtw_p2p_set_pre_state(pwdinfo, rtw_p2p_state(pwdinfo));
		rtw_p2p_set_state(pwdinfo, P2P_STATE_GONEGO_ING);

		DBG_88E("[%s] Start PreTx Procedure!\n", __func__);
		_set_timer(&pwdinfo->pre_tx_scan_timer, P2P_TX_PRESCAN_TIMEOUT);
		_set_timer(&pwdinfo->restore_p2p_state_timer, P2P_GO_NEGO_TIMEOUT);
	} else {
		DBG_88E("[%s] Not Found in Scanning Queue~\n", __func__);
		ret = -1;
	}
	return ret;
}

static int rtw_p2p_invite_req(struct net_device *dev,
			      struct iw_request_info *info,
			      union iwreq_data *wrqu, char *extra)
{
	int ret = 0;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;
	int jj, kk;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct list_head *plist, *phead;
	struct __queue *queue	= &pmlmepriv->scanned_queue;
	struct	wlan_network	*pnetwork = NULL;
	uint uintPeerChannel = 0;
	u8 attr_content[50] = {0x00};
	u8 *p2pie;
	uint p2pielen = 0, attr_contentlen = 0;
	struct tx_invite_req_info *pinvite_req_info = &pwdinfo->invitereq_info;

	/*	The input data contains two informations. */
	/*	1. First information is the P2P device address which you want to send to. */
	/*	2. Second information is the group id which combines with GO's mac address, space and GO's ssid. */
	/*	Command line sample: iwpriv wlan0 p2p_set invite ="00:11:22:33:44:55 00:E0:4C:00:00:05 DIRECT-xy" */
	/*	Format: 00:11:22:33:44:55 00:E0:4C:00:00:05 DIRECT-xy */

	DBG_88E("[%s] data = %s\n", __func__, extra);

	if (wrqu->data.length <=  37) {
		DBG_88E("[%s] Wrong format!\n", __func__);
		return ret;
	}

	if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE)) {
		DBG_88E("[%s] WiFi Direct is disable!\n", __func__);
		return ret;
	} else {
		/*	Reset the content of struct tx_invite_req_info */
		pinvite_req_info->benable = false;
		memset(pinvite_req_info->go_bssid, 0x00, ETH_ALEN);
		memset(pinvite_req_info->go_ssid, 0x00, WLAN_SSID_MAXLEN);
		pinvite_req_info->ssidlen = 0x00;
		pinvite_req_info->operating_ch = pwdinfo->operating_channel;
		memset(pinvite_req_info->peer_macaddr, 0x00, ETH_ALEN);
		pinvite_req_info->token = 3;
	}

	for (jj = 0, kk = 0; jj < ETH_ALEN; jj++, kk += 3)
		pinvite_req_info->peer_macaddr[jj] = key_2char2num(extra[kk], extra[kk + 1]);

	spin_lock_bh(&pmlmepriv->scanned_queue.lock);

	phead = get_list_head(queue);
	plist = phead->next;

	while (phead != plist) {
		pnetwork = container_of(plist, struct wlan_network, list);

		/*	Commented by Albert 2011/05/18 */
		/*	Match the device address located in the P2P IE */
		/*	This is for the case that the P2P device address is not the same as the P2P interface address. */

		p2pie = rtw_get_p2p_ie(&pnetwork->network.IEs[12], pnetwork->network.IELength - 12, NULL, &p2pielen);
		if (p2pie) {
			/*	The P2P Device ID attribute is included in the Beacon frame. */
			/*	The P2P Device Info attribute is included in the probe response frame. */

			if (rtw_get_p2p_attr_content(p2pie, p2pielen, P2P_ATTR_DEVICE_ID, attr_content, &attr_contentlen)) {
				/*	Handle the P2P Device ID attribute of Beacon first */
				if (!memcmp(attr_content, pinvite_req_info->peer_macaddr, ETH_ALEN)) {
					uintPeerChannel = pnetwork->network.Configuration.DSConfig;
					break;
				}
			} else if (rtw_get_p2p_attr_content(p2pie, p2pielen, P2P_ATTR_DEVICE_INFO, attr_content, &attr_contentlen)) {
				/*	Handle the P2P Device Info attribute of probe response */
				if (!memcmp(attr_content, pinvite_req_info->peer_macaddr, ETH_ALEN)) {
					uintPeerChannel = pnetwork->network.Configuration.DSConfig;
					break;
				}
			}
		}
		plist = plist->next;
	}

	spin_unlock_bh(&pmlmepriv->scanned_queue.lock);

	if (uintPeerChannel) {
		/*	Store the GO's bssid */
		for (jj = 0, kk = 18; jj < ETH_ALEN; jj++, kk += 3)
			pinvite_req_info->go_bssid[jj] = key_2char2num(extra[kk], extra[kk + 1]);

		/*	Store the GO's ssid */
		pinvite_req_info->ssidlen = wrqu->data.length - 36;
		memcpy(pinvite_req_info->go_ssid, &extra[36], (u32)pinvite_req_info->ssidlen);
		pinvite_req_info->benable = true;
		pinvite_req_info->peer_ch = uintPeerChannel;

		rtw_p2p_set_pre_state(pwdinfo, rtw_p2p_state(pwdinfo));
		rtw_p2p_set_state(pwdinfo, P2P_STATE_TX_INVITE_REQ);

		set_channel_bwmode(padapter, uintPeerChannel, HAL_PRIME_CHNL_OFFSET_DONT_CARE, HT_CHANNEL_WIDTH_20);

		_set_timer(&pwdinfo->pre_tx_scan_timer, P2P_TX_PRESCAN_TIMEOUT);

		_set_timer(&pwdinfo->restore_p2p_state_timer, P2P_INVITE_TIMEOUT);
	} else {
		DBG_88E("[%s] NOT Found in the Scanning Queue!\n", __func__);
	}
	return ret;
}

static int rtw_p2p_set_persistent(struct net_device *dev,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)
{
	int ret = 0;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;

	/*	The input data is 0 or 1 */
	/*	0: disable persistent group functionality */
	/*	1: enable persistent group founctionality */

	DBG_88E("[%s] data = %s\n", __func__, extra);

	if (rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE)) {
		DBG_88E("[%s] WiFi Direct is disable!\n", __func__);
		return ret;
	} else {
		if (extra[0] == '0')	/*	Disable the persistent group function. */
			pwdinfo->persistent_supported = false;
		else if (extra[0] == '1')	/*	Enable the persistent group function. */
			pwdinfo->persistent_supported = true;
		else
			pwdinfo->persistent_supported = false;
	}
	pr_info("[%s] persistent_supported = %d\n", __func__, pwdinfo->persistent_supported);
	return ret;
}

static int rtw_p2p_prov_disc(struct net_device *dev,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)
{
	int ret = 0;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;
	u8 peerMAC[ETH_ALEN] = {0x00};
	int jj, kk;
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	struct list_head *plist, *phead;
	struct __queue *queue	= &pmlmepriv->scanned_queue;
	struct	wlan_network	*pnetwork = NULL;
	uint uintPeerChannel = 0;
	u8 attr_content[100] = {0x00};
	u8 *p2pie;
	uint p2pielen = 0, attr_contentlen = 0;

	/*	The input data contains two informations. */
	/*	1. First information is the MAC address which wants to issue the provisioning discovery request frame. */
	/*	2. Second information is the WPS configuration method which wants to discovery */
	/*	Format: 00:E0:4C:00:00:05_display */
	/*	Format: 00:E0:4C:00:00:05_keypad */
	/*	Format: 00:E0:4C:00:00:05_pbc */
	/*	Format: 00:E0:4C:00:00:05_label */

	DBG_88E("[%s] data = %s\n", __func__, extra);

	if (pwdinfo->p2p_state == P2P_STATE_NONE) {
		DBG_88E("[%s] WiFi Direct is disable!\n", __func__);
		return ret;
	} else {
		/*	Reset the content of struct tx_provdisc_req_info excluded the wps_config_method_request. */
		memset(pwdinfo->tx_prov_disc_info.peerDevAddr, 0x00, ETH_ALEN);
		memset(pwdinfo->tx_prov_disc_info.peerIFAddr, 0x00, ETH_ALEN);
		memset(&pwdinfo->tx_prov_disc_info.ssid, 0x00, sizeof(struct ndis_802_11_ssid));
		pwdinfo->tx_prov_disc_info.peer_channel_num[0] = 0;
		pwdinfo->tx_prov_disc_info.peer_channel_num[1] = 0;
		pwdinfo->tx_prov_disc_info.benable = false;
	}

	for (jj = 0, kk = 0; jj < ETH_ALEN; jj++, kk += 3)
		peerMAC[jj] = key_2char2num(extra[kk], extra[kk + 1]);

	if (!memcmp(&extra[18], "display", 7)) {
		pwdinfo->tx_prov_disc_info.wps_config_method_request = WPS_CM_DISPLYA;
	} else if (!memcmp(&extra[18], "keypad", 7)) {
		pwdinfo->tx_prov_disc_info.wps_config_method_request = WPS_CM_KEYPAD;
	} else if (!memcmp(&extra[18], "pbc", 3)) {
		pwdinfo->tx_prov_disc_info.wps_config_method_request = WPS_CM_PUSH_BUTTON;
	} else if (!memcmp(&extra[18], "label", 5)) {
		pwdinfo->tx_prov_disc_info.wps_config_method_request = WPS_CM_LABEL;
	} else {
		DBG_88E("[%s] Unknown WPS config methodn", __func__);
		return ret;
	}

	spin_lock_bh(&pmlmepriv->scanned_queue.lock);

	phead = get_list_head(queue);
	plist = phead->next;

	while (phead != plist) {
		if (uintPeerChannel != 0)
			break;

		pnetwork = container_of(plist, struct wlan_network, list);

		/*	Commented by Albert 2011/05/18 */
		/*	Match the device address located in the P2P IE */
		/*	This is for the case that the P2P device address is not the same as the P2P interface address. */

		p2pie = rtw_get_p2p_ie(&pnetwork->network.IEs[12], pnetwork->network.IELength - 12, NULL, &p2pielen);
		if (p2pie) {
			while (p2pie) {
				/*	The P2P Device ID attribute is included in the Beacon frame. */
				/*	The P2P Device Info attribute is included in the probe response frame. */

				if (rtw_get_p2p_attr_content(p2pie, p2pielen, P2P_ATTR_DEVICE_ID, attr_content, &attr_contentlen)) {
					/*	Handle the P2P Device ID attribute of Beacon first */
					if (!memcmp(attr_content, peerMAC, ETH_ALEN)) {
						uintPeerChannel = pnetwork->network.Configuration.DSConfig;
						break;
					}
				} else if (rtw_get_p2p_attr_content(p2pie, p2pielen, P2P_ATTR_DEVICE_INFO, attr_content, &attr_contentlen)) {
					/*	Handle the P2P Device Info attribute of probe response */
					if (!memcmp(attr_content, peerMAC, ETH_ALEN)) {
						uintPeerChannel = pnetwork->network.Configuration.DSConfig;
						break;
					}
				}

				/* Get the next P2P IE */
				p2pie = rtw_get_p2p_ie(p2pie + p2pielen, pnetwork->network.IELength - 12 - (p2pie - &pnetwork->network.IEs[12] + p2pielen), NULL, &p2pielen);
			}
		}

		plist = plist->next;
	}

	spin_unlock_bh(&pmlmepriv->scanned_queue.lock);

	if (uintPeerChannel) {
		DBG_88E("[%s] peer channel: %d!\n", __func__, uintPeerChannel);
		memcpy(pwdinfo->tx_prov_disc_info.peerIFAddr, pnetwork->network.MacAddress, ETH_ALEN);
		memcpy(pwdinfo->tx_prov_disc_info.peerDevAddr, peerMAC, ETH_ALEN);
		pwdinfo->tx_prov_disc_info.peer_channel_num[0] = (u16)uintPeerChannel;
		pwdinfo->tx_prov_disc_info.benable = true;
		rtw_p2p_set_pre_state(pwdinfo, rtw_p2p_state(pwdinfo));
		rtw_p2p_set_state(pwdinfo, P2P_STATE_TX_PROVISION_DIS_REQ);

		if (rtw_p2p_chk_role(pwdinfo, P2P_ROLE_CLIENT)) {
			memcpy(&pwdinfo->tx_prov_disc_info.ssid, &pnetwork->network.Ssid, sizeof(struct ndis_802_11_ssid));
		} else if (rtw_p2p_chk_role(pwdinfo, P2P_ROLE_DEVICE) || rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO)) {
			memcpy(pwdinfo->tx_prov_disc_info.ssid.Ssid, pwdinfo->p2p_wildcard_ssid, P2P_WILDCARD_SSID_LEN);
			pwdinfo->tx_prov_disc_info.ssid.SsidLength = P2P_WILDCARD_SSID_LEN;
		}

		set_channel_bwmode(padapter, uintPeerChannel, HAL_PRIME_CHNL_OFFSET_DONT_CARE, HT_CHANNEL_WIDTH_20);

		_set_timer(&pwdinfo->pre_tx_scan_timer, P2P_TX_PRESCAN_TIMEOUT);

		_set_timer(&pwdinfo->restore_p2p_state_timer, P2P_PROVISION_TIMEOUT);
	} else {
		DBG_88E("[%s] NOT Found in the Scanning Queue!\n", __func__);
	}
	return ret;
}

/*	This function is used to inform the driver the user had specified the pin code value or pbc */
/*	to application. */

static int rtw_p2p_got_wpsinfo(struct net_device *dev,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)
{
	int ret = 0;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;

	DBG_88E("[%s] data = %s\n", __func__, extra);
	/*	Added by Albert 20110328 */
	/*	if the input data is P2P_NO_WPSINFO -> reset the wpsinfo */
	/*	if the input data is P2P_GOT_WPSINFO_PEER_DISPLAY_PIN -> the utility just input the PIN code got from the peer P2P device. */
	/*	if the input data is P2P_GOT_WPSINFO_SELF_DISPLAY_PIN -> the utility just got the PIN code from itself. */
	/*	if the input data is P2P_GOT_WPSINFO_PBC -> the utility just determine to use the PBC */

	if (*extra == '0')
		pwdinfo->ui_got_wps_info = P2P_NO_WPSINFO;
	else if (*extra == '1')
		pwdinfo->ui_got_wps_info = P2P_GOT_WPSINFO_PEER_DISPLAY_PIN;
	else if (*extra == '2')
		pwdinfo->ui_got_wps_info = P2P_GOT_WPSINFO_SELF_DISPLAY_PIN;
	else if (*extra == '3')
		pwdinfo->ui_got_wps_info = P2P_GOT_WPSINFO_PBC;
	else
		pwdinfo->ui_got_wps_info = P2P_NO_WPSINFO;
	return ret;
}

#endif /* CONFIG_88EU_P2P */

static int rtw_p2p_set(struct net_device *dev,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)
{
	int ret = 0;

#ifdef CONFIG_88EU_P2P
	DBG_88E("[%s] extra = %s\n", __func__, extra);
	if (!memcmp(extra, "enable =", 7)) {
		rtw_wext_p2p_enable(dev, info, wrqu, &extra[7]);
	} else if (!memcmp(extra, "setDN =", 6)) {
		wrqu->data.length -= 6;
		rtw_p2p_setDN(dev, info, wrqu, &extra[6]);
	} else if (!memcmp(extra, "profilefound =", 13)) {
		wrqu->data.length -= 13;
		rtw_p2p_profilefound(dev, info, wrqu, &extra[13]);
	} else if (!memcmp(extra, "prov_disc =", 10)) {
		wrqu->data.length -= 10;
		rtw_p2p_prov_disc(dev, info, wrqu, &extra[10]);
	} else if (!memcmp(extra, "nego =", 5)) {
		wrqu->data.length -= 5;
		rtw_p2p_connect(dev, info, wrqu, &extra[5]);
	} else if (!memcmp(extra, "intent =", 7)) {
		/*	Commented by Albert 2011/03/23 */
		/*	The wrqu->data.length will include the null character */
		/*	So, we will decrease 7 + 1 */
		wrqu->data.length -= 8;
		rtw_p2p_set_intent(dev, info, wrqu, &extra[7]);
	} else if (!memcmp(extra, "ssid =", 5)) {
		wrqu->data.length -= 5;
		rtw_p2p_set_go_nego_ssid(dev, info, wrqu, &extra[5]);
	} else if (!memcmp(extra, "got_wpsinfo =", 12)) {
		wrqu->data.length -= 12;
		rtw_p2p_got_wpsinfo(dev, info, wrqu, &extra[12]);
	} else if (!memcmp(extra, "listen_ch =", 10)) {
		/*	Commented by Albert 2011/05/24 */
		/*	The wrqu->data.length will include the null character */
		/*	So, we will decrease (10 + 1) */
		wrqu->data.length -= 11;
		rtw_p2p_set_listen_ch(dev, info, wrqu, &extra[10]);
	} else if (!memcmp(extra, "op_ch =", 6)) {
		/*	Commented by Albert 2011/05/24 */
		/*	The wrqu->data.length will include the null character */
		/*	So, we will decrease (6 + 1) */
		wrqu->data.length -= 7;
		rtw_p2p_set_op_ch(dev, info, wrqu, &extra[6]);
	} else if (!memcmp(extra, "invite =", 7)) {
		wrqu->data.length -= 8;
		rtw_p2p_invite_req(dev, info, wrqu, &extra[7]);
	} else if (!memcmp(extra, "persistent =", 11)) {
		wrqu->data.length -= 11;
		rtw_p2p_set_persistent(dev, info, wrqu, &extra[11]);
	}
#endif /* CONFIG_88EU_P2P */

	return ret;
}

static int rtw_p2p_get(struct net_device *dev,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)
{
	int ret = 0;

#ifdef CONFIG_88EU_P2P
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);

	if (padapter->bShowGetP2PState)
		DBG_88E("[%s] extra = %s\n", __func__, (char *)wrqu->data.pointer);
	if (!memcmp(wrqu->data.pointer, "status", 6)) {
		rtw_p2p_get_status(dev, info, wrqu, extra);
	} else if (!memcmp(wrqu->data.pointer, "role", 4)) {
		rtw_p2p_get_role(dev, info, wrqu, extra);
	} else if (!memcmp(wrqu->data.pointer, "peer_ifa", 8)) {
		rtw_p2p_get_peer_ifaddr(dev, info, wrqu, extra);
	} else if (!memcmp(wrqu->data.pointer, "req_cm", 6)) {
		rtw_p2p_get_req_cm(dev, info, wrqu, extra);
	} else if (!memcmp(wrqu->data.pointer, "peer_deva", 9)) {
		/*	Get the P2P device address when receiving the provision discovery request frame. */
		rtw_p2p_get_peer_devaddr(dev, info, wrqu, extra);
	} else if (!memcmp(wrqu->data.pointer, "group_id", 8)) {
		rtw_p2p_get_groupid(dev, info, wrqu, extra);
	} else if (!memcmp(wrqu->data.pointer, "peer_deva_inv", 9)) {
		/*	Get the P2P device address when receiving the P2P Invitation request frame. */
		rtw_p2p_get_peer_devaddr_by_invitation(dev, info, wrqu, extra);
	} else if (!memcmp(wrqu->data.pointer, "op_ch", 5)) {
		rtw_p2p_get_op_ch(dev, info, wrqu, extra);
	}
#endif /* CONFIG_88EU_P2P */
	return ret;
}

static int rtw_p2p_get2(struct net_device *dev,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)
{
	int ret = 0;

#ifdef CONFIG_88EU_P2P
	DBG_88E("[%s] extra = %s\n", __func__, (char *)wrqu->data.pointer);
	if (!memcmp(extra, "wpsCM =", 6)) {
		wrqu->data.length -= 6;
		rtw_p2p_get_wps_configmethod(dev, info, wrqu,  &extra[6]);
	} else if (!memcmp(extra, "devN =", 5)) {
		wrqu->data.length -= 5;
		rtw_p2p_get_device_name(dev, info, wrqu, &extra[5]);
	} else if (!memcmp(extra, "dev_type =", 9)) {
		wrqu->data.length -= 9;
		rtw_p2p_get_device_type(dev, info, wrqu, &extra[9]);
	} else if (!memcmp(extra, "go_devadd =", 10)) {
		wrqu->data.length -= 10;
		rtw_p2p_get_go_device_address(dev, info, wrqu, &extra[10]);
	} else if (!memcmp(extra, "InvProc =", 8)) {
		wrqu->data.length -= 8;
		rtw_p2p_get_invitation_procedure(dev, info, wrqu, &extra[8]);
	}

#endif /* CONFIG_88EU_P2P */

	return ret;
}

static int rtw_cta_test_start(struct net_device *dev,
			      struct iw_request_info *info,
			      union iwreq_data *wrqu, char *extra)
{
	int ret = 0;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	DBG_88E("%s %s\n", __func__, extra);
	if (!strcmp(extra, "1"))
		padapter->in_cta_test = 1;
	else
		padapter->in_cta_test = 0;

	if (padapter->in_cta_test) {
		u32 v = rtw_read32(padapter, REG_RCR);
		v &= ~(RCR_CBSSID_DATA | RCR_CBSSID_BCN);/*  RCR_ADF */
		rtw_write32(padapter, REG_RCR, v);
		DBG_88E("enable RCR_ADF\n");
	} else {
		u32 v = rtw_read32(padapter, REG_RCR);
		v |= RCR_CBSSID_DATA | RCR_CBSSID_BCN;/*  RCR_ADF */
		rtw_write32(padapter, REG_RCR, v);
		DBG_88E("disable RCR_ADF\n");
	}
	return ret;
}

static int rtw_rereg_nd_name(struct net_device *dev,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)
{
	int ret = 0;
	struct adapter *padapter = rtw_netdev_priv(dev);
	struct rereg_nd_name_data *rereg_priv = &padapter->rereg_nd_name_priv;
	char new_ifname[IFNAMSIZ];

	if (rereg_priv->old_ifname[0] == 0) {
		char *reg_ifname;
		reg_ifname = padapter->registrypriv.if2name;

		strncpy(rereg_priv->old_ifname, reg_ifname, IFNAMSIZ);
		rereg_priv->old_ifname[IFNAMSIZ - 1] = 0;
	}

	if (wrqu->data.length > IFNAMSIZ)
		return -EFAULT;

	if (copy_from_user(new_ifname, wrqu->data.pointer, IFNAMSIZ))
		return -EFAULT;

	if (0 == strcmp(rereg_priv->old_ifname, new_ifname))
		return ret;

	DBG_88E("%s new_ifname:%s\n", __func__, new_ifname);
	ret = rtw_change_ifname(padapter, new_ifname);
	if (0 != ret)
		goto exit;

	if (!memcmp(rereg_priv->old_ifname, "disable%d", 9)) {
		padapter->ledpriv.bRegUseLed = rereg_priv->old_bRegUseLed;
		rtl8188eu_InitSwLeds(padapter);
		rtw_ips_mode_req(&padapter->pwrctrlpriv, rereg_priv->old_ips_mode);
	}

	strncpy(rereg_priv->old_ifname, new_ifname, IFNAMSIZ);
	rereg_priv->old_ifname[IFNAMSIZ - 1] = 0;

	if (!memcmp(new_ifname, "disable%d", 9)) {
		DBG_88E("%s disable\n", __func__);
		/*  free network queue for Android's timming issue */
		rtw_free_network_queue(padapter, true);

		/*  close led */
		rtw_led_control(padapter, LED_CTL_POWER_OFF);
		rereg_priv->old_bRegUseLed = padapter->ledpriv.bRegUseLed;
		padapter->ledpriv.bRegUseLed = false;
		rtl8188eu_DeInitSwLeds(padapter);

		/*  the interface is being "disabled", we can do deeper IPS */
		rereg_priv->old_ips_mode = rtw_get_ips_mode_req(&padapter->pwrctrlpriv);
		rtw_ips_mode_req(&padapter->pwrctrlpriv, IPS_NORMAL);
	}
exit:
	return ret;
}

static void mac_reg_dump(struct adapter *padapter)
{
	int i, j = 1;
	pr_info("\n ======= MAC REG =======\n");
	for (i = 0x0; i < 0x300; i += 4) {
		if (j % 4 == 1)
			pr_info("0x%02x", i);
		pr_info(" 0x%08x ", rtw_read32(padapter, i));
		if ((j++) % 4 == 0)
			pr_info("\n");
	}
	for (i = 0x400; i < 0x800; i += 4) {
		if (j % 4 == 1)
			pr_info("0x%02x", i);
		pr_info(" 0x%08x ", rtw_read32(padapter, i));
		if ((j++) % 4 == 0)
			pr_info("\n");
	}
}

static void bb_reg_dump(struct adapter *padapter)
{
	int i, j = 1;
	pr_info("\n ======= BB REG =======\n");
	for (i = 0x800; i < 0x1000; i += 4) {
		if (j % 4 == 1)
			pr_info("0x%02x", i);

		pr_info(" 0x%08x ", rtw_read32(padapter, i));
		if ((j++) % 4 == 0)
			pr_info("\n");
	}
}

static void rf_reg_dump(struct adapter *padapter)
{
	int i, j = 1, path;
	u32 value;
	u8 rf_type, path_nums = 0;
	rtw_hal_get_hwreg(padapter, HW_VAR_RF_TYPE, (u8 *)(&rf_type));

	pr_info("\n ======= RF REG =======\n");
	if ((RF_1T2R == rf_type) || (RF_1T1R == rf_type))
		path_nums = 1;
	else
		path_nums = 2;

	for (path = 0; path < path_nums; path++) {
		pr_info("\nRF_Path(%x)\n", path);
		for (i = 0; i < 0x100; i++) {
			value = rtw_hal_read_rfreg(padapter, path, i, 0xffffffff);
			if (j % 4 == 1)
				pr_info("0x%02x ", i);
			pr_info(" 0x%08x ", value);
			if ((j++) % 4 == 0)
				pr_info("\n");
		}
	}
}

static int rtw_dbg_port(struct net_device *dev,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)
{
	int ret = 0;
	u8 major_cmd, minor_cmd;
	u16 arg;
	s32 extra_arg;
	u32 *pdata, val32;
	struct sta_info *psta;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	struct wlan_network *cur_network = &pmlmepriv->cur_network;
	struct sta_priv *pstapriv = &padapter->stapriv;

	pdata = (u32 *)&wrqu->data;

	val32 = *pdata;
	arg = (u16)(val32 & 0x0000ffff);
	major_cmd = (u8)(val32 >> 24);
	minor_cmd = (u8)((val32 >> 16) & 0x00ff);

	extra_arg = *(pdata + 1);

	switch (major_cmd) {
	case 0x70:/* read_reg */
		switch (minor_cmd) {
		case 1:
			DBG_88E("rtw_read8(0x%x) = 0x%02x\n", arg, rtw_read8(padapter, arg));
			break;
		case 2:
			DBG_88E("rtw_read16(0x%x) = 0x%04x\n", arg, rtw_read16(padapter, arg));
			break;
		case 4:
			DBG_88E("rtw_read32(0x%x) = 0x%08x\n", arg, rtw_read32(padapter, arg));
			break;
		}
		break;
	case 0x71:/* write_reg */
		switch (minor_cmd) {
		case 1:
			rtw_write8(padapter, arg, extra_arg);
			DBG_88E("rtw_write8(0x%x) = 0x%02x\n", arg, rtw_read8(padapter, arg));
			break;
		case 2:
			rtw_write16(padapter, arg, extra_arg);
			DBG_88E("rtw_write16(0x%x) = 0x%04x\n", arg, rtw_read16(padapter, arg));
			break;
		case 4:
			rtw_write32(padapter, arg, extra_arg);
			DBG_88E("rtw_write32(0x%x) = 0x%08x\n", arg, rtw_read32(padapter, arg));
			break;
		}
		break;
	case 0x72:/* read_bb */
		DBG_88E("read_bbreg(0x%x) = 0x%x\n", arg, rtw_hal_read_bbreg(padapter, arg, 0xffffffff));
		break;
	case 0x73:/* write_bb */
		rtw_hal_write_bbreg(padapter, arg, 0xffffffff, extra_arg);
		DBG_88E("write_bbreg(0x%x) = 0x%x\n", arg, rtw_hal_read_bbreg(padapter, arg, 0xffffffff));
		break;
	case 0x74:/* read_rf */
		DBG_88E("read RF_reg path(0x%02x), offset(0x%x), value(0x%08x)\n", minor_cmd, arg, rtw_hal_read_rfreg(padapter, minor_cmd, arg, 0xffffffff));
		break;
	case 0x75:/* write_rf */
		rtw_hal_write_rfreg(padapter, minor_cmd, arg, 0xffffffff, extra_arg);
		DBG_88E("write RF_reg path(0x%02x), offset(0x%x), value(0x%08x)\n", minor_cmd, arg, rtw_hal_read_rfreg(padapter, minor_cmd, arg, 0xffffffff));
		break;

	case 0x76:
		switch (minor_cmd) {
		case 0x00: /* normal mode, */
			padapter->recvpriv.is_signal_dbg = 0;
			break;
		case 0x01: /* dbg mode */
			padapter->recvpriv.is_signal_dbg = 1;
			extra_arg = extra_arg > 100 ? 100 : extra_arg;
			extra_arg = extra_arg < 0 ? 0 : extra_arg;
			padapter->recvpriv.signal_strength_dbg = extra_arg;
			break;
		}
		break;
	case 0x78: /* IOL test */
		switch (minor_cmd) {
		case 0x04: /* LLT table initialization test */
		{
			u8 page_boundary = 0xf9;
			struct xmit_frame	*xmit_frame;

			xmit_frame = rtw_IOL_accquire_xmit_frame(padapter);
			if (!xmit_frame) {
				ret = -ENOMEM;
				break;
			}

			rtw_IOL_append_LLT_cmd(xmit_frame, page_boundary);

			if (_SUCCESS != rtw_IOL_exec_cmds_sync(padapter, xmit_frame, 500, 0))
				ret = -EPERM;
		}
			break;
		case 0x05: /* blink LED test */
		{
			u16 reg = 0x4c;
			u32 blink_num = 50;
			u32 blink_delay_ms = 200;
			int i;
			struct xmit_frame	*xmit_frame;

			xmit_frame = rtw_IOL_accquire_xmit_frame(padapter);
			if (!xmit_frame) {
				ret = -ENOMEM;
				break;
			}

			for (i = 0; i < blink_num; i++) {
				rtw_IOL_append_WB_cmd(xmit_frame, reg, 0x00, 0xff);
				rtw_IOL_append_DELAY_MS_cmd(xmit_frame, blink_delay_ms);
				rtw_IOL_append_WB_cmd(xmit_frame, reg, 0x08, 0xff);
				rtw_IOL_append_DELAY_MS_cmd(xmit_frame, blink_delay_ms);
			}
			if (_SUCCESS != rtw_IOL_exec_cmds_sync(padapter, xmit_frame, (blink_delay_ms * blink_num * 2) + 200, 0))
				ret = -EPERM;
		}
			break;

		case 0x06: /* continuous write byte test */
		{
			u16 reg = arg;
			u16 start_value = 0;
			u32 write_num = extra_arg;
			int i;
			u8 final;
			struct xmit_frame	*xmit_frame;

			xmit_frame = rtw_IOL_accquire_xmit_frame(padapter);
			if (!xmit_frame) {
				ret = -ENOMEM;
				break;
			}

			for (i = 0; i < write_num; i++)
				rtw_IOL_append_WB_cmd(xmit_frame, reg, i + start_value, 0xFF);
			if (_SUCCESS != rtw_IOL_exec_cmds_sync(padapter, xmit_frame, 5000, 0))
				ret = -EPERM;

			final = rtw_read8(padapter, reg);
			if (start_value + write_num - 1 == final)
				DBG_88E("continuous IOL_CMD_WB_REG to 0x%x %u times Success, start:%u, final:%u\n", reg, write_num, start_value, final);
			else
				DBG_88E("continuous IOL_CMD_WB_REG to 0x%x %u times Fail, start:%u, final:%u\n", reg, write_num, start_value, final);
		}
			break;

		case 0x07: /* continuous write word test */
		{
			u16 reg = arg;
			u16 start_value = 200;
			u32 write_num = extra_arg;

			int i;
			u16 final;
			struct xmit_frame	*xmit_frame;

			xmit_frame = rtw_IOL_accquire_xmit_frame(padapter);
			if (!xmit_frame) {
				ret = -ENOMEM;
				break;
			}

			for (i = 0; i < write_num; i++)
				rtw_IOL_append_WW_cmd(xmit_frame, reg, i + start_value, 0xFFFF);
			if (_SUCCESS != rtw_IOL_exec_cmds_sync(padapter, xmit_frame, 5000, 0))
				ret = -EPERM;

			final = rtw_read16(padapter, reg);
			if (start_value + write_num - 1 == final)
				DBG_88E("continuous IOL_CMD_WW_REG to 0x%x %u times Success, start:%u, final:%u\n", reg, write_num, start_value, final);
			else
				DBG_88E("continuous IOL_CMD_WW_REG to 0x%x %u times Fail, start:%u, final:%u\n", reg, write_num, start_value, final);
		}
			break;
		case 0x08: /* continuous write dword test */
		{
			u16 reg = arg;
			u32 start_value = 0x110000c7;
			u32 write_num = extra_arg;

			int i;
			u32 final;
			struct xmit_frame	*xmit_frame;

			xmit_frame = rtw_IOL_accquire_xmit_frame(padapter);
			if (!xmit_frame) {
				ret = -ENOMEM;
				break;
			}

			for (i = 0; i < write_num; i++)
				rtw_IOL_append_WD_cmd(xmit_frame, reg, i + start_value, 0xFFFFFFFF);
			if (_SUCCESS != rtw_IOL_exec_cmds_sync(padapter, xmit_frame, 5000, 0))
				ret = -EPERM;

			final = rtw_read32(padapter, reg);
			if (start_value + write_num - 1 == final)
				DBG_88E("continuous IOL_CMD_WD_REG to 0x%x %u times Success, start:%u, final:%u\n",
					reg, write_num, start_value, final);
			else
				DBG_88E("continuous IOL_CMD_WD_REG to 0x%x %u times Fail, start:%u, final:%u\n",
					reg, write_num, start_value, final);
		}
			break;
		}
		break;
	case 0x79:
		{
			/*
			* dbg 0x79000000 [value], set RESP_TXAGC to + value, value:0~15
			* dbg 0x79010000 [value], set RESP_TXAGC to - value, value:0~15
			*/
			u8 value =  extra_arg & 0x0f;
			u8 sign = minor_cmd;
			u16 write_value = 0;

			DBG_88E("%s set RESP_TXAGC to %s %u\n", __func__, sign ? "minus" : "plus", value);

			if (sign)
				value = value | 0x10;

			write_value = value | (value << 5);
			rtw_write16(padapter, 0x6d9, write_value);
		}
		break;
	case 0x7a:
		receive_disconnect(padapter, pmlmeinfo->network.MacAddress
			, WLAN_REASON_EXPIRATION_CHK);
		break;
	case 0x7F:
		switch (minor_cmd) {
		case 0x0:
			DBG_88E("fwstate = 0x%x\n", get_fwstate(pmlmepriv));
			break;
		case 0x01:
			DBG_88E("auth_alg = 0x%x, enc_alg = 0x%x, auth_type = 0x%x, enc_type = 0x%x\n",
				psecuritypriv->dot11AuthAlgrthm, psecuritypriv->dot11PrivacyAlgrthm,
				psecuritypriv->ndisauthtype, psecuritypriv->ndisencryptstatus);
			break;
		case 0x02:
			DBG_88E("pmlmeinfo->state = 0x%x\n", pmlmeinfo->state);
			break;
		case 0x03:
			DBG_88E("qos_option =%d\n", pmlmepriv->qospriv.qos_option);
			DBG_88E("ht_option =%d\n", pmlmepriv->htpriv.ht_option);
			break;
		case 0x04:
			DBG_88E("cur_ch =%d\n", pmlmeext->cur_channel);
			DBG_88E("cur_bw =%d\n", pmlmeext->cur_bwmode);
			DBG_88E("cur_ch_off =%d\n", pmlmeext->cur_ch_offset);
			break;
		case 0x05:
			psta = rtw_get_stainfo(pstapriv, cur_network->network.MacAddress);
			if (psta) {
				int i;
				struct recv_reorder_ctrl *preorder_ctrl;

				DBG_88E("SSID =%s\n", cur_network->network.Ssid.Ssid);
				DBG_88E("sta's macaddr: %pM\n", psta->hwaddr);
				DBG_88E("cur_channel =%d, cur_bwmode =%d, cur_ch_offset =%d\n", pmlmeext->cur_channel, pmlmeext->cur_bwmode, pmlmeext->cur_ch_offset);
				DBG_88E("rtsen =%d, cts2slef =%d\n", psta->rtsen, psta->cts2self);
				DBG_88E("state = 0x%x, aid =%d, macid =%d, raid =%d\n", psta->state, psta->aid, psta->mac_id, psta->raid);
				DBG_88E("qos_en =%d, ht_en =%d, init_rate =%d\n", psta->qos_option, psta->htpriv.ht_option, psta->init_rate);
				DBG_88E("bwmode =%d, ch_offset =%d, sgi =%d\n", psta->htpriv.bwmode, psta->htpriv.ch_offset, psta->htpriv.sgi);
				DBG_88E("ampdu_enable = %d\n", psta->htpriv.ampdu_enable);
				DBG_88E("agg_enable_bitmap =%x, candidate_tid_bitmap =%x\n", psta->htpriv.agg_enable_bitmap, psta->htpriv.candidate_tid_bitmap);
				for (i = 0; i < 16; i++) {
					preorder_ctrl = &psta->recvreorder_ctrl[i];
					if (preorder_ctrl->enable)
						DBG_88E("tid =%d, indicate_seq =%d\n", i, preorder_ctrl->indicate_seq);
				}
			} else {
				DBG_88E("can't get sta's macaddr, cur_network's macaddr:%pM\n", (cur_network->network.MacAddress));
			}
			break;
		case 0x06:
			{
				u32	ODMFlag;
				rtw_hal_get_hwreg(padapter, HW_VAR_DM_FLAG, (u8 *)(&ODMFlag));
				DBG_88E("(B)DMFlag = 0x%x, arg = 0x%x\n", ODMFlag, arg);
				ODMFlag = (u32)(0x0f & arg);
				DBG_88E("(A)DMFlag = 0x%x\n", ODMFlag);
				rtw_hal_set_hwreg(padapter, HW_VAR_DM_FLAG, (u8 *)(&ODMFlag));
			}
			break;
		case 0x07:
			DBG_88E("bSurpriseRemoved =%d, bDriverStopped =%d\n",
				padapter->bSurpriseRemoved, padapter->bDriverStopped);
			break;
		case 0x08:
			{
				struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
				struct recv_priv  *precvpriv = &padapter->recvpriv;

				DBG_88E("free_xmitbuf_cnt =%d, free_xmitframe_cnt =%d, free_xmit_extbuf_cnt =%d\n",
					pxmitpriv->free_xmitbuf_cnt, pxmitpriv->free_xmitframe_cnt, pxmitpriv->free_xmit_extbuf_cnt);
				DBG_88E("rx_urb_pending_cn =%d\n", precvpriv->rx_pending_cnt);
			}
			break;
		case 0x09:
			{
				int i, j;
				struct list_head *plist, *phead;
				struct recv_reorder_ctrl *preorder_ctrl;

#ifdef CONFIG_88EU_AP_MODE
				DBG_88E("sta_dz_bitmap = 0x%x, tim_bitmap = 0x%x\n", pstapriv->sta_dz_bitmap, pstapriv->tim_bitmap);
#endif
				spin_lock_bh(&pstapriv->sta_hash_lock);

				for (i = 0; i < NUM_STA; i++) {
					phead = &pstapriv->sta_hash[i];
					plist = phead->next;

					while (phead != plist) {
						psta = container_of(plist, struct sta_info, hash_list);

						plist = plist->next;

						if (extra_arg == psta->aid) {
							DBG_88E("sta's macaddr:%pM\n", (psta->hwaddr));
							DBG_88E("rtsen =%d, cts2slef =%d\n", psta->rtsen, psta->cts2self);
							DBG_88E("state = 0x%x, aid =%d, macid =%d, raid =%d\n", psta->state, psta->aid, psta->mac_id, psta->raid);
							DBG_88E("qos_en =%d, ht_en =%d, init_rate =%d\n", psta->qos_option, psta->htpriv.ht_option, psta->init_rate);
							DBG_88E("bwmode =%d, ch_offset =%d, sgi =%d\n", psta->htpriv.bwmode, psta->htpriv.ch_offset, psta->htpriv.sgi);
							DBG_88E("ampdu_enable = %d\n", psta->htpriv.ampdu_enable);
							DBG_88E("agg_enable_bitmap =%x, candidate_tid_bitmap =%x\n", psta->htpriv.agg_enable_bitmap, psta->htpriv.candidate_tid_bitmap);

#ifdef CONFIG_88EU_AP_MODE
							DBG_88E("capability = 0x%x\n", psta->capability);
							DBG_88E("flags = 0x%x\n", psta->flags);
							DBG_88E("wpa_psk = 0x%x\n", psta->wpa_psk);
							DBG_88E("wpa2_group_cipher = 0x%x\n", psta->wpa2_group_cipher);
							DBG_88E("wpa2_pairwise_cipher = 0x%x\n", psta->wpa2_pairwise_cipher);
							DBG_88E("qos_info = 0x%x\n", psta->qos_info);
#endif
							DBG_88E("dot118021XPrivacy = 0x%x\n", psta->dot118021XPrivacy);

							for (j = 0; j < 16; j++) {
								preorder_ctrl = &psta->recvreorder_ctrl[j];
								if (preorder_ctrl->enable)
									DBG_88E("tid =%d, indicate_seq =%d\n", j, preorder_ctrl->indicate_seq);
							}
						}
					}
				}
				spin_unlock_bh(&pstapriv->sta_hash_lock);
			}
			break;
		case 0x0c:/* dump rx/tx packet */
			if (arg == 0) {
				DBG_88E("dump rx packet (%d)\n", extra_arg);
				rtw_hal_set_def_var(padapter, HAL_DEF_DBG_DUMP_RXPKT, &(extra_arg));
			} else if (arg == 1) {
				DBG_88E("dump tx packet (%d)\n", extra_arg);
				rtw_hal_set_def_var(padapter, HAL_DEF_DBG_DUMP_TXPKT, &(extra_arg));
			}
			break;
		case 0x0f:
			if (extra_arg == 0) {
				DBG_88E("###### silent reset test.......#####\n");
				rtw_hal_sreset_reset(padapter);
			}
			break;
		case 0x15:
			{
				struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
				DBG_88E("==>silent resete cnts:%d\n", pwrpriv->ips_enter_cnts);
			}
			break;
		case 0x10:/*  driver version display */
			DBG_88E("rtw driver version =%s\n", DRIVERVERSION);
			break;
		case 0x11:
			DBG_88E("turn %s Rx RSSI display function\n", (extra_arg == 1) ? "on" : "off");
			padapter->bRxRSSIDisplay = extra_arg;
			break;
		case 0x12: /* set rx_stbc */
		{
			struct registry_priv	*pregpriv = &padapter->registrypriv;
			/*  0: disable, bit(0):enable 2.4g, bit(1):enable 5g, 0x3: enable both 2.4g and 5g */
			/* default is set to enable 2.4GHZ for IOT issue with bufflao's AP at 5GHZ */
			if (extra_arg == 0 ||
			    extra_arg == 1 ||
			    extra_arg == 2 ||
			    extra_arg == 3) {
				pregpriv->rx_stbc = extra_arg;
				DBG_88E("set rx_stbc =%d\n", pregpriv->rx_stbc);
			} else {
				DBG_88E("get rx_stbc =%d\n", pregpriv->rx_stbc);
			}
		}
			break;
		case 0x13: /* set ampdu_enable */
		{
			struct registry_priv	*pregpriv = &padapter->registrypriv;
			/*  0: disable, 0x1:enable (but wifi_spec should be 0), 0x2: force enable (don't care wifi_spec) */
			if (extra_arg >= 0 && extra_arg < 3) {
				pregpriv->ampdu_enable = extra_arg;
				DBG_88E("set ampdu_enable =%d\n", pregpriv->ampdu_enable);
			} else {
				DBG_88E("get ampdu_enable =%d\n", pregpriv->ampdu_enable);
			}
		}
			break;
		case 0x14: /* get wifi_spec */
		{
			struct registry_priv	*pregpriv = &padapter->registrypriv;
			DBG_88E("get wifi_spec =%d\n", pregpriv->wifi_spec);
		}
			break;
		case 0x23:
			DBG_88E("turn %s the bNotifyChannelChange Variable\n", (extra_arg == 1) ? "on" : "off");
			padapter->bNotifyChannelChange = extra_arg;
			break;
		case 0x24:
#ifdef CONFIG_88EU_P2P
			DBG_88E("turn %s the bShowGetP2PState Variable\n", (extra_arg == 1) ? "on" : "off");
			padapter->bShowGetP2PState = extra_arg;
#endif /*  CONFIG_88EU_P2P */
			break;
		case 0xaa:
			if (extra_arg > 0x13)
				extra_arg = 0xFF;
			DBG_88E("chang data rate to :0x%02x\n", extra_arg);
			padapter->fix_rate = extra_arg;
			break;
		case 0xdd:/* registers dump, 0 for mac reg, 1 for bb reg, 2 for rf reg */
			if (extra_arg == 0)
				mac_reg_dump(padapter);
			else if (extra_arg == 1)
				bb_reg_dump(padapter);
			else if (extra_arg == 2)
				rf_reg_dump(padapter);
			break;
		case 0xee:/* turn on/off dynamic funcs */
			{
				u32 odm_flag;

				if (0xf == extra_arg) {
					rtw_hal_get_def_var(padapter, HAL_DEF_DBG_DM_FUNC, &odm_flag);
					DBG_88E(" === DMFlag(0x%08x) ===\n", odm_flag);
					DBG_88E("extra_arg = 0  - disable all dynamic func\n");
					DBG_88E("extra_arg = 1  - disable DIG- BIT(0)\n");
					DBG_88E("extra_arg = 2  - disable High power - BIT(1)\n");
					DBG_88E("extra_arg = 3  - disable tx power tracking - BIT(2)\n");
					DBG_88E("extra_arg = 4  - disable BT coexistence - BIT(3)\n");
					DBG_88E("extra_arg = 5  - disable antenna diversity - BIT(4)\n");
					DBG_88E("extra_arg = 6  - enable all dynamic func\n");
				} else {
					/*	extra_arg = 0  - disable all dynamic func
						extra_arg = 1  - disable DIG
						extra_arg = 2  - disable tx power tracking
						extra_arg = 3  - turn on all dynamic func
					*/
					rtw_hal_set_def_var(padapter, HAL_DEF_DBG_DM_FUNC, &(extra_arg));
					rtw_hal_get_def_var(padapter, HAL_DEF_DBG_DM_FUNC, &odm_flag);
					DBG_88E(" === DMFlag(0x%08x) ===\n", odm_flag);
				}
			}
			break;

		case 0xfd:
			rtw_write8(padapter, 0xc50, arg);
			DBG_88E("wr(0xc50) = 0x%x\n", rtw_read8(padapter, 0xc50));
			rtw_write8(padapter, 0xc58, arg);
			DBG_88E("wr(0xc58) = 0x%x\n", rtw_read8(padapter, 0xc58));
			break;
		case 0xfe:
			DBG_88E("rd(0xc50) = 0x%x\n", rtw_read8(padapter, 0xc50));
			DBG_88E("rd(0xc58) = 0x%x\n", rtw_read8(padapter, 0xc58));
			break;
		case 0xff:
			DBG_88E("dbg(0x210) = 0x%x\n", rtw_read32(padapter, 0x210));
			DBG_88E("dbg(0x608) = 0x%x\n", rtw_read32(padapter, 0x608));
			DBG_88E("dbg(0x280) = 0x%x\n", rtw_read32(padapter, 0x280));
			DBG_88E("dbg(0x284) = 0x%x\n", rtw_read32(padapter, 0x284));
			DBG_88E("dbg(0x288) = 0x%x\n", rtw_read32(padapter, 0x288));

			DBG_88E("dbg(0x664) = 0x%x\n", rtw_read32(padapter, 0x664));

			DBG_88E("\n");

			DBG_88E("dbg(0x430) = 0x%x\n", rtw_read32(padapter, 0x430));
			DBG_88E("dbg(0x438) = 0x%x\n", rtw_read32(padapter, 0x438));

			DBG_88E("dbg(0x440) = 0x%x\n", rtw_read32(padapter, 0x440));

			DBG_88E("dbg(0x458) = 0x%x\n", rtw_read32(padapter, 0x458));

			DBG_88E("dbg(0x484) = 0x%x\n", rtw_read32(padapter, 0x484));
			DBG_88E("dbg(0x488) = 0x%x\n", rtw_read32(padapter, 0x488));

			DBG_88E("dbg(0x444) = 0x%x\n", rtw_read32(padapter, 0x444));
			DBG_88E("dbg(0x448) = 0x%x\n", rtw_read32(padapter, 0x448));
			DBG_88E("dbg(0x44c) = 0x%x\n", rtw_read32(padapter, 0x44c));
			DBG_88E("dbg(0x450) = 0x%x\n", rtw_read32(padapter, 0x450));
			break;
		}
		break;
	default:
		DBG_88E("error dbg cmd!\n");
		break;
	}
	return ret;
}

static int rtw_wx_set_priv(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *awrq,
				char *extra)
{
	int ret = 0;
	int len = 0;
	char *ext;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct iw_point *dwrq = (struct iw_point *)awrq;

	if (dwrq->length == 0)
		return -EFAULT;

	len = dwrq->length;
	ext = vmalloc(len);
	if (!ext)
		return -ENOMEM;

	if (copy_from_user(ext, dwrq->pointer, len)) {
		vfree(ext);
		return -EFAULT;
	}

	/* added for wps2.0 @20110524 */
	if (dwrq->flags == 0x8766 && len > 8) {
		u32 cp_sz;
		struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
		u8 *probereq_wpsie = ext;
		int probereq_wpsie_len = len;
		u8 wps_oui[4] = {0x0, 0x50, 0xf2, 0x04};

		if ((_VENDOR_SPECIFIC_IE_ == probereq_wpsie[0]) &&
		    (!memcmp(&probereq_wpsie[2], wps_oui, 4))) {
			cp_sz = probereq_wpsie_len > MAX_WPS_IE_LEN ? MAX_WPS_IE_LEN : probereq_wpsie_len;

			pmlmepriv->wps_probe_req_ie_len = 0;
			kfree(pmlmepriv->wps_probe_req_ie);
			pmlmepriv->wps_probe_req_ie = NULL;

			pmlmepriv->wps_probe_req_ie = kmalloc(cp_sz, GFP_KERNEL);
			if (!pmlmepriv->wps_probe_req_ie) {
				ret =  -EINVAL;
				goto FREE_EXT;
			}
			memcpy(pmlmepriv->wps_probe_req_ie, probereq_wpsie, cp_sz);
			pmlmepriv->wps_probe_req_ie_len = cp_sz;
		}
		goto FREE_EXT;
	}

	if (len >= WEXT_CSCAN_HEADER_SIZE &&
	    !memcmp(ext, WEXT_CSCAN_HEADER, WEXT_CSCAN_HEADER_SIZE)) {
		ret = rtw_wx_set_scan(dev, info, awrq, ext);
		goto FREE_EXT;
	}

FREE_EXT:

	vfree(ext);

	return ret;
}

static int rtw_pm_set(struct net_device *dev,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)
{
	int ret = 0;
	unsigned	mode = 0;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);

	DBG_88E("[%s] extra = %s\n", __func__, extra);

	if (!memcmp(extra, "lps =", 4)) {
		sscanf(extra + 4, "%u", &mode);
		ret = rtw_pm_set_lps(padapter, mode);
	} else if (!memcmp(extra, "ips =", 4)) {
		sscanf(extra + 4, "%u", &mode);
		ret = rtw_pm_set_ips(padapter, mode);
	} else {
		ret = -EINVAL;
	}

	return ret;
}

static int rtw_mp_efuse_get(struct net_device *dev,
			struct iw_request_info *info,
			union iwreq_data *wdata, char *extra)
{
	struct adapter *padapter = rtw_netdev_priv(dev);
	struct eeprom_priv *pEEPROM = GET_EEPROM_EFUSE_PRIV(padapter);
	struct hal_data_8188e *haldata = GET_HAL_DATA(padapter);
	struct efuse_hal *pEfuseHal;
	struct iw_point *wrqu;

	u8 *PROMContent = pEEPROM->efuse_eeprom_data;
	u8 ips_mode = 0, lps_mode = 0;
	struct pwrctrl_priv *pwrctrlpriv;
	u8 *data = NULL;
	u8 *rawdata = NULL;
	char *pch, *ptmp, *token, *tmp[3] = {NULL, NULL, NULL};
	u16 i = 0, j = 0, mapLen = 0, addr = 0, cnts = 0;
	u16 max_available_size = 0, raw_cursize = 0, raw_maxsize = 0;
	int err;
	u8 org_fw_iol = padapter->registrypriv.fw_iol;/*  0:Disable, 1:enable, 2:by usb speed */

	wrqu = (struct iw_point *)wdata;
	pwrctrlpriv = &padapter->pwrctrlpriv;
	pEfuseHal = &haldata->EfuseHal;

	err = 0;
	data = kzalloc(EFUSE_BT_MAX_MAP_LEN, GFP_KERNEL);
	if (!data) {
		err = -ENOMEM;
		goto exit;
	}
	rawdata = kzalloc(EFUSE_BT_MAX_MAP_LEN, GFP_KERNEL);
	if (!rawdata) {
		err = -ENOMEM;
		goto exit;
	}

	if (copy_from_user(extra, wrqu->pointer, wrqu->length)) {
		err = -EFAULT;
		goto exit;
	}
	lps_mode = pwrctrlpriv->power_mgnt;/* keep org value */
	rtw_pm_set_lps(padapter, PS_MODE_ACTIVE);

	ips_mode = pwrctrlpriv->ips_mode;/* keep org value */
	rtw_pm_set_ips(padapter, IPS_NONE);

	pch = extra;
	DBG_88E("%s: in =%s\n", __func__, extra);

	i = 0;
	/* mac 16 "00e04c871200" rmap, 00, 2 */
	while ((token = strsep(&pch, ",")) != NULL) {
		if (i > 2)
			break;
		tmp[i] = token;
		i++;
	}
	padapter->registrypriv.fw_iol = 0;/*  0:Disable, 1:enable, 2:by usb speed */

	if (strcmp(tmp[0], "status") == 0) {
		sprintf(extra, "Load File efuse =%s, Load File MAC =%s", (pEEPROM->bloadfile_fail_flag ? "FAIL" : "OK"), (pEEPROM->bloadmac_fail_flag ? "FAIL" : "OK"));

		goto exit;
	} else if (strcmp(tmp[0], "filemap") == 0) {
		mapLen = EFUSE_MAP_SIZE;

		sprintf(extra, "\n");
		for (i = 0; i < EFUSE_MAP_SIZE; i += 16) {
			sprintf(extra + strlen(extra), "0x%02x\t", i);
			for (j = 0; j < 8; j++)
				sprintf(extra + strlen(extra), "%02X ", PROMContent[i + j]);
			sprintf(extra + strlen(extra), "\t");
			for (; j < 16; j++)
				sprintf(extra + strlen(extra), "%02X ", PROMContent[i + j]);
			sprintf(extra + strlen(extra), "\n");
		}
	} else if (strcmp(tmp[0], "realmap") == 0) {
		mapLen = EFUSE_MAP_SIZE;
		if (rtw_efuse_map_read(padapter, 0, mapLen, pEfuseHal->fakeEfuseInitMap) == _FAIL) {
			DBG_88E("%s: read realmap Fail!!\n", __func__);
			err = -EFAULT;
			goto exit;
		}

		sprintf(extra, "\n");
		for (i = 0; i < EFUSE_MAP_SIZE; i += 16) {
			sprintf(extra + strlen(extra), "0x%02x\t", i);
			for (j = 0; j < 8; j++)
				sprintf(extra + strlen(extra), "%02X ", pEfuseHal->fakeEfuseInitMap[i + j]);
			sprintf(extra + strlen(extra), "\t");
			for (; j < 16; j++)
				sprintf(extra + strlen(extra), "%02X ", pEfuseHal->fakeEfuseInitMap[i + j]);
			sprintf(extra + strlen(extra), "\n");
		}
	} else if (strcmp(tmp[0], "rmap") == 0) {
		if (!tmp[1] || !tmp[2]) {
			DBG_88E("%s: rmap Fail!! Parameters error!\n", __func__);
			err = -EINVAL;
			goto exit;
		}

		/*  rmap addr cnts */
		addr = simple_strtoul(tmp[1], &ptmp, 16);
		DBG_88E("%s: addr =%x\n", __func__, addr);

		cnts = simple_strtoul(tmp[2], &ptmp, 10);
		if (cnts == 0) {
			DBG_88E("%s: rmap Fail!! cnts error!\n", __func__);
			err = -EINVAL;
			goto exit;
		}
		DBG_88E("%s: cnts =%d\n", __func__, cnts);

		EFUSE_GetEfuseDefinition(padapter, EFUSE_WIFI, TYPE_AVAILABLE_EFUSE_BYTES_TOTAL, (void *)&max_available_size, false);
		if ((addr + cnts) > max_available_size) {
			DBG_88E("%s: addr(0x%X)+cnts(%d) parameter error!\n", __func__, addr, cnts);
			err = -EINVAL;
			goto exit;
		}

		if (rtw_efuse_map_read(padapter, addr, cnts, data) == _FAIL) {
			DBG_88E("%s: rtw_efuse_map_read error!\n", __func__);
			err = -EFAULT;
			goto exit;
		}

		*extra = 0;
		for (i = 0; i < cnts; i++)
			sprintf(extra + strlen(extra), "0x%02X ", data[i]);
	} else if (strcmp(tmp[0], "realraw") == 0) {
		addr = 0;
		mapLen = EFUSE_MAX_SIZE;
		if (rtw_efuse_access(padapter, false, addr, mapLen, rawdata) == _FAIL) {
			DBG_88E("%s: rtw_efuse_access Fail!!\n", __func__);
			err = -EFAULT;
			goto exit;
		}

		sprintf(extra, "\n");
		for (i = 0; i < mapLen; i++) {
			sprintf(extra + strlen(extra), "%02X", rawdata[i]);

			if ((i & 0xF) == 0xF)
				sprintf(extra + strlen(extra), "\n");
			else if ((i & 0x7) == 0x7)
				sprintf(extra + strlen(extra), "\t");
			else
				sprintf(extra + strlen(extra), " ");
		}
	} else if (strcmp(tmp[0], "mac") == 0) {
		cnts = 6;

		EFUSE_GetEfuseDefinition(padapter, EFUSE_WIFI, TYPE_AVAILABLE_EFUSE_BYTES_TOTAL, (void *)&max_available_size, false);
		if ((addr + cnts) > max_available_size) {
			DBG_88E("%s: addr(0x%02x)+cnts(%d) parameter error!\n", __func__, addr, cnts);
			err = -EFAULT;
			goto exit;
		}

		if (rtw_efuse_map_read(padapter, addr, cnts, data) == _FAIL) {
			DBG_88E("%s: rtw_efuse_map_read error!\n", __func__);
			err = -EFAULT;
			goto exit;
		}

		*extra = 0;
		for (i = 0; i < cnts; i++) {
			sprintf(extra + strlen(extra), "%02X", data[i]);
			if (i != (cnts - 1))
				sprintf(extra + strlen(extra), ":");
		}
	} else if (strcmp(tmp[0], "vidpid") == 0) {
		cnts = 4;

		EFUSE_GetEfuseDefinition(padapter, EFUSE_WIFI, TYPE_AVAILABLE_EFUSE_BYTES_TOTAL, (void *)&max_available_size, false);
		if ((addr + cnts) > max_available_size) {
			DBG_88E("%s: addr(0x%02x)+cnts(%d) parameter error!\n", __func__, addr, cnts);
			err = -EFAULT;
			goto exit;
		}
		if (rtw_efuse_map_read(padapter, addr, cnts, data) == _FAIL) {
			DBG_88E("%s: rtw_efuse_access error!!\n", __func__);
			err = -EFAULT;
			goto exit;
		}

		*extra = 0;
		for (i = 0; i < cnts; i++) {
			sprintf(extra + strlen(extra), "0x%02X", data[i]);
			if (i != (cnts - 1))
				sprintf(extra + strlen(extra), ",");
		}
	} else if (strcmp(tmp[0], "ableraw") == 0) {
		efuse_GetCurrentSize(padapter, &raw_cursize);
		raw_maxsize = efuse_GetMaxSize(padapter);
		sprintf(extra, "[available raw size] = %d bytes", raw_maxsize - raw_cursize);
	} else if (strcmp(tmp[0], "btfmap") == 0) {
		mapLen = EFUSE_BT_MAX_MAP_LEN;
		if (rtw_BT_efuse_map_read(padapter, 0, mapLen, pEfuseHal->BTEfuseInitMap) == _FAIL) {
			DBG_88E("%s: rtw_BT_efuse_map_read Fail!!\n", __func__);
			err = -EFAULT;
			goto exit;
		}

		sprintf(extra, "\n");
		for (i = 0; i < 512; i += 16) {
			/*  set 512 because the iwpriv's extra size have limit 0x7FF */
			sprintf(extra + strlen(extra), "0x%03x\t", i);
			for (j = 0; j < 8; j++)
				sprintf(extra + strlen(extra), "%02X ", pEfuseHal->BTEfuseInitMap[i + j]);
			sprintf(extra + strlen(extra), "\t");
			for (; j < 16; j++)
				sprintf(extra + strlen(extra), "%02X ", pEfuseHal->BTEfuseInitMap[i + j]);
			sprintf(extra + strlen(extra), "\n");
		}
	} else if (strcmp(tmp[0], "btbmap") == 0) {
		mapLen = EFUSE_BT_MAX_MAP_LEN;
		if (rtw_BT_efuse_map_read(padapter, 0, mapLen, pEfuseHal->BTEfuseInitMap) == _FAIL) {
			DBG_88E("%s: rtw_BT_efuse_map_read Fail!!\n", __func__);
			err = -EFAULT;
			goto exit;
		}

		sprintf(extra, "\n");
		for (i = 512; i < 1024; i += 16) {
			sprintf(extra + strlen(extra), "0x%03x\t", i);
			for (j = 0; j < 8; j++)
				sprintf(extra + strlen(extra), "%02X ", pEfuseHal->BTEfuseInitMap[i + j]);
			sprintf(extra + strlen(extra), "\t");
			for (; j < 16; j++)
				sprintf(extra + strlen(extra), "%02X ", pEfuseHal->BTEfuseInitMap[i + j]);
			sprintf(extra + strlen(extra), "\n");
		}
	} else if (strcmp(tmp[0], "btrmap") == 0) {
		if (!tmp[1] || !tmp[2]) {
			err = -EINVAL;
			goto exit;
		}

		/*  rmap addr cnts */
		addr = simple_strtoul(tmp[1], &ptmp, 16);
		DBG_88E("%s: addr = 0x%X\n", __func__, addr);

		cnts = simple_strtoul(tmp[2], &ptmp, 10);
		if (cnts == 0) {
			DBG_88E("%s: btrmap Fail!! cnts error!\n", __func__);
			err = -EINVAL;
			goto exit;
		}
		DBG_88E("%s: cnts =%d\n", __func__, cnts);

		EFUSE_GetEfuseDefinition(padapter, EFUSE_BT, TYPE_AVAILABLE_EFUSE_BYTES_TOTAL, (void *)&max_available_size, false);
		if ((addr + cnts) > max_available_size) {
			DBG_88E("%s: addr(0x%X)+cnts(%d) parameter error!\n", __func__, addr, cnts);
			err = -EFAULT;
			goto exit;
		}

		if (rtw_BT_efuse_map_read(padapter, addr, cnts, data) == _FAIL) {
			DBG_88E("%s: rtw_BT_efuse_map_read error!!\n", __func__);
			err = -EFAULT;
			goto exit;
		}

		*extra = 0;
		for (i = 0; i < cnts; i++)
			sprintf(extra + strlen(extra), " 0x%02X ", data[i]);
	} else if (strcmp(tmp[0], "btffake") == 0) {
		sprintf(extra, "\n");
		for (i = 0; i < 512; i += 16) {
			sprintf(extra + strlen(extra), "0x%03x\t", i);
			for (j = 0; j < 8; j++)
				sprintf(extra + strlen(extra), "%02X ", pEfuseHal->fakeBTEfuseModifiedMap[i + j]);
			sprintf(extra + strlen(extra), "\t");
			for (; j < 16; j++)
				sprintf(extra + strlen(extra), "%02X ", pEfuseHal->fakeBTEfuseModifiedMap[i + j]);
			sprintf(extra + strlen(extra), "\n");
		}
	} else if (strcmp(tmp[0], "btbfake") == 0) {
		sprintf(extra, "\n");
		for (i = 512; i < 1024; i += 16) {
			sprintf(extra + strlen(extra), "0x%03x\t", i);
			for (j = 0; j < 8; j++)
				sprintf(extra + strlen(extra), "%02X ", pEfuseHal->fakeBTEfuseModifiedMap[i + j]);
			sprintf(extra + strlen(extra), "\t");
			for (; j < 16; j++)
				sprintf(extra + strlen(extra), "%02X ", pEfuseHal->fakeBTEfuseModifiedMap[i + j]);
			sprintf(extra + strlen(extra), "\n");
		}
	} else if (strcmp(tmp[0], "wlrfkmap") == 0) {
		sprintf(extra, "\n");
		for (i = 0; i < EFUSE_MAP_SIZE; i += 16) {
			sprintf(extra + strlen(extra), "0x%02x\t", i);
			for (j = 0; j < 8; j++)
				sprintf(extra + strlen(extra), "%02X ", pEfuseHal->fakeEfuseModifiedMap[i + j]);
			sprintf(extra + strlen(extra), "\t");
			for (; j < 16; j++)
				sprintf(extra + strlen(extra), " %02X", pEfuseHal->fakeEfuseModifiedMap[i + j]);
			sprintf(extra + strlen(extra), "\n");
		}
	} else {
		 sprintf(extra, "Command not found!");
	}

exit:
	kfree(data);
	kfree(rawdata);
	if (!err)
		wrqu->length = strlen(extra);

	rtw_pm_set_ips(padapter, ips_mode);
	rtw_pm_set_lps(padapter, lps_mode);
	padapter->registrypriv.fw_iol = org_fw_iol;/*  0:Disable, 1:enable, 2:by usb speed */
	return err;
}

static int rtw_mp_efuse_set(struct net_device *dev,
			struct iw_request_info *info,
			union iwreq_data *wdata, char *extra)
{
	struct adapter *padapter;
	struct pwrctrl_priv *pwrctrlpriv;
	struct hal_data_8188e *haldata;
	struct efuse_hal *pEfuseHal;

	u8 ips_mode = 0, lps_mode = 0;
	u32 i, jj, kk;
	u8 *setdata = NULL;
	u8 *ShadowMapBT = NULL;
	u8 *ShadowMapWiFi = NULL;
	u8 *setrawdata = NULL;
	char *pch, *ptmp, *token, *tmp[3] = {NULL, NULL, NULL};
	u16 addr = 0, cnts = 0, max_available_size = 0;
	int err;

	padapter = rtw_netdev_priv(dev);
	pwrctrlpriv = &padapter->pwrctrlpriv;
	haldata = GET_HAL_DATA(padapter);
	pEfuseHal = &haldata->EfuseHal;
	err = 0;
	setdata = kzalloc(1024, GFP_KERNEL);
	if (!setdata) {
		err = -ENOMEM;
		goto exit;
	}
	ShadowMapBT = kmalloc(EFUSE_BT_MAX_MAP_LEN, GFP_KERNEL);
	if (!ShadowMapBT) {
		err = -ENOMEM;
		goto exit;
	}
	ShadowMapWiFi = kmalloc(EFUSE_MAP_SIZE, GFP_KERNEL);
	if (!ShadowMapWiFi) {
		err = -ENOMEM;
		goto exit;
	}
	setrawdata = kmalloc(EFUSE_MAX_SIZE, GFP_KERNEL);
	if (!setrawdata) {
		err = -ENOMEM;
		goto exit;
	}

	lps_mode = pwrctrlpriv->power_mgnt;/* keep org value */
	rtw_pm_set_lps(padapter, PS_MODE_ACTIVE);

	ips_mode = pwrctrlpriv->ips_mode;/* keep org value */
	rtw_pm_set_ips(padapter, IPS_NONE);

	pch = extra;
	DBG_88E("%s: in =%s\n", __func__, extra);

	i = 0;
	while ((token = strsep(&pch, ",")) != NULL) {
		if (i > 2)
			break;
		tmp[i] = token;
		i++;
	}

	/*  tmp[0],[1],[2] */
	/*  wmap, addr, 00e04c871200 */
	if (strcmp(tmp[0], "wmap") == 0) {
		if (!tmp[1] || !tmp[2]) {
			err = -EINVAL;
			goto exit;
		}

		addr = simple_strtoul(tmp[1], &ptmp, 16);
		addr &= 0xFFF;

		cnts = strlen(tmp[2]);
		if (cnts % 2) {
			err = -EINVAL;
			goto exit;
		}
		cnts /= 2;
		if (cnts == 0) {
			err = -EINVAL;
			goto exit;
		}

		DBG_88E("%s: addr = 0x%X\n", __func__, addr);
		DBG_88E("%s: cnts =%d\n", __func__, cnts);
		DBG_88E("%s: map data =%s\n", __func__, tmp[2]);

		for (jj = 0, kk = 0; jj < cnts; jj++, kk += 2)
			setdata[jj] = key_2char2num(tmp[2][kk], tmp[2][kk + 1]);
		/* Change to check TYPE_EFUSE_MAP_LEN, because 8188E raw 256, logic map over 256. */
		EFUSE_GetEfuseDefinition(padapter, EFUSE_WIFI, TYPE_EFUSE_MAP_LEN, (void *)&max_available_size, false);
		if ((addr + cnts) > max_available_size) {
			DBG_88E("%s: addr(0x%X)+cnts(%d) parameter error!\n", __func__, addr, cnts);
			err = -EFAULT;
			goto exit;
		}

		if (rtw_efuse_map_write(padapter, addr, cnts, setdata) == _FAIL) {
			DBG_88E("%s: rtw_efuse_map_write error!!\n", __func__);
			err = -EFAULT;
			goto exit;
		}
	} else if (strcmp(tmp[0], "wraw") == 0) {
		if (!tmp[1] || !tmp[2]) {
			err = -EINVAL;
			goto exit;
		}

		addr = simple_strtoul(tmp[1], &ptmp, 16);
		addr &= 0xFFF;

		cnts = strlen(tmp[2]);
		if (cnts % 2) {
			err = -EINVAL;
			goto exit;
		}
		cnts /= 2;
		if (cnts == 0) {
			err = -EINVAL;
			goto exit;
		}

		DBG_88E("%s: addr = 0x%X\n", __func__, addr);
		DBG_88E("%s: cnts =%d\n", __func__, cnts);
		DBG_88E("%s: raw data =%s\n", __func__, tmp[2]);

		for (jj = 0, kk = 0; jj < cnts; jj++, kk += 2)
			setrawdata[jj] = key_2char2num(tmp[2][kk], tmp[2][kk + 1]);

		if (rtw_efuse_access(padapter, true, addr, cnts, setrawdata) == _FAIL) {
			DBG_88E("%s: rtw_efuse_access error!!\n", __func__);
			err = -EFAULT;
			goto exit;
		}
	} else if (strcmp(tmp[0], "mac") == 0) {
		if (!tmp[1]) {
			err = -EINVAL;
			goto exit;
		}

		/* mac, 00e04c871200 */
		addr = EEPROM_MAC_ADDR_88EU;
		cnts = strlen(tmp[1]);
		if (cnts % 2) {
			err = -EINVAL;
			goto exit;
		}
		cnts /= 2;
		if (cnts == 0) {
			err = -EINVAL;
			goto exit;
		}
		if (cnts > 6) {
			DBG_88E("%s: error data for mac addr =\"%s\"\n", __func__, tmp[1]);
			err = -EFAULT;
			goto exit;
		}

		DBG_88E("%s: addr = 0x%X\n", __func__, addr);
		DBG_88E("%s: cnts =%d\n", __func__, cnts);
		DBG_88E("%s: MAC address =%s\n", __func__, tmp[1]);

		for (jj = 0, kk = 0; jj < cnts; jj++, kk += 2)
			setdata[jj] = key_2char2num(tmp[1][kk], tmp[1][kk + 1]);
		/* Change to check TYPE_EFUSE_MAP_LEN, because 8188E raw 256, logic map over 256. */
		EFUSE_GetEfuseDefinition(padapter, EFUSE_WIFI, TYPE_EFUSE_MAP_LEN, (void *)&max_available_size, false);
		if ((addr + cnts) > max_available_size) {
			DBG_88E("%s: addr(0x%X)+cnts(%d) parameter error!\n", __func__, addr, cnts);
			err = -EFAULT;
			goto exit;
		}

		if (rtw_efuse_map_write(padapter, addr, cnts, setdata) == _FAIL) {
			DBG_88E("%s: rtw_efuse_map_write error!!\n", __func__);
			err = -EFAULT;
			goto exit;
		}
	} else if (strcmp(tmp[0], "vidpid") == 0) {
		if (!tmp[1]) {
			err = -EINVAL;
			goto exit;
		}

		/*  pidvid, da0b7881 */
		addr = EEPROM_VID_88EE;
		cnts = strlen(tmp[1]);
		if (cnts % 2) {
			err = -EINVAL;
			goto exit;
		}
		cnts /= 2;
		if (cnts == 0) {
			err = -EINVAL;
			goto exit;
		}

		DBG_88E("%s: addr = 0x%X\n", __func__, addr);
		DBG_88E("%s: cnts =%d\n", __func__, cnts);
		DBG_88E("%s: VID/PID =%s\n", __func__, tmp[1]);

		for (jj = 0, kk = 0; jj < cnts; jj++, kk += 2)
			setdata[jj] = key_2char2num(tmp[1][kk], tmp[1][kk + 1]);

		EFUSE_GetEfuseDefinition(padapter, EFUSE_WIFI, TYPE_AVAILABLE_EFUSE_BYTES_TOTAL, (void *)&max_available_size, false);
		if ((addr + cnts) > max_available_size) {
			DBG_88E("%s: addr(0x%X)+cnts(%d) parameter error!\n", __func__, addr, cnts);
			err = -EFAULT;
			goto exit;
		}

		if (rtw_efuse_map_write(padapter, addr, cnts, setdata) == _FAIL) {
			DBG_88E("%s: rtw_efuse_map_write error!!\n", __func__);
			err = -EFAULT;
			goto exit;
		}
	} else if (strcmp(tmp[0], "btwmap") == 0) {
		if (!tmp[1] || !tmp[2]) {
			err = -EINVAL;
			goto exit;
		}

		addr = simple_strtoul(tmp[1], &ptmp, 16);
		addr &= 0xFFF;

		cnts = strlen(tmp[2]);
		if (cnts % 2) {
			err = -EINVAL;
			goto exit;
		}
		cnts /= 2;
		if (cnts == 0) {
			err = -EINVAL;
			goto exit;
		}

		DBG_88E("%s: addr = 0x%X\n", __func__, addr);
		DBG_88E("%s: cnts =%d\n", __func__, cnts);
		DBG_88E("%s: BT data =%s\n", __func__, tmp[2]);

		for (jj = 0, kk = 0; jj < cnts; jj++, kk += 2)
			setdata[jj] = key_2char2num(tmp[2][kk], tmp[2][kk + 1]);

		EFUSE_GetEfuseDefinition(padapter, EFUSE_BT, TYPE_AVAILABLE_EFUSE_BYTES_TOTAL, (void *)&max_available_size, false);
		if ((addr + cnts) > max_available_size) {
			DBG_88E("%s: addr(0x%X)+cnts(%d) parameter error!\n", __func__, addr, cnts);
			err = -EFAULT;
			goto exit;
		}

		if (rtw_BT_efuse_map_write(padapter, addr, cnts, setdata) == _FAIL) {
			DBG_88E("%s: rtw_BT_efuse_map_write error!!\n", __func__);
			err = -EFAULT;
			goto exit;
		}
	} else if (strcmp(tmp[0], "btwfake") == 0) {
		if (!tmp[1] || !tmp[2]) {
			err = -EINVAL;
			goto exit;
		}

		addr = simple_strtoul(tmp[1], &ptmp, 16);
		addr &= 0xFFF;

		cnts = strlen(tmp[2]);
		if (cnts % 2) {
			err = -EINVAL;
			goto exit;
		}
		cnts /= 2;
		if (cnts == 0) {
			err = -EINVAL;
			goto exit;
		}

		DBG_88E("%s: addr = 0x%X\n", __func__, addr);
		DBG_88E("%s: cnts =%d\n", __func__, cnts);
		DBG_88E("%s: BT tmp data =%s\n", __func__, tmp[2]);

		for (jj = 0, kk = 0; jj < cnts; jj++, kk += 2)
			pEfuseHal->fakeBTEfuseModifiedMap[addr + jj] = key_2char2num(tmp[2][kk], tmp[2][kk + 1]);
	} else if (strcmp(tmp[0], "btdumpfake") == 0) {
		if (rtw_BT_efuse_map_read(padapter, 0, EFUSE_BT_MAX_MAP_LEN, pEfuseHal->fakeBTEfuseModifiedMap) == _SUCCESS) {
			DBG_88E("%s: BT read all map success\n", __func__);
		} else {
			DBG_88E("%s: BT read all map Fail!\n", __func__);
			err = -EFAULT;
		}
	} else if (strcmp(tmp[0], "wldumpfake") == 0) {
		if (rtw_efuse_map_read(padapter, 0, EFUSE_BT_MAX_MAP_LEN,  pEfuseHal->fakeEfuseModifiedMap) == _SUCCESS) {
			DBG_88E("%s: BT read all map success\n", __func__);
		} else {
			DBG_88E("%s: BT read all map  Fail\n", __func__);
			err = -EFAULT;
		}
	} else if (strcmp(tmp[0], "btfk2map") == 0) {
		memcpy(pEfuseHal->BTEfuseModifiedMap, pEfuseHal->fakeBTEfuseModifiedMap, EFUSE_BT_MAX_MAP_LEN);

		EFUSE_GetEfuseDefinition(padapter, EFUSE_BT, TYPE_AVAILABLE_EFUSE_BYTES_TOTAL, (void *)&max_available_size, false);
		if (max_available_size < 1) {
			err = -EFAULT;
			goto exit;
		}

		if (rtw_BT_efuse_map_write(padapter, 0x00, EFUSE_BT_MAX_MAP_LEN, pEfuseHal->fakeBTEfuseModifiedMap) == _FAIL) {
			DBG_88E("%s: rtw_BT_efuse_map_write error!\n", __func__);
			err = -EFAULT;
			goto exit;
		}
	} else if (strcmp(tmp[0], "wlfk2map") == 0) {
		EFUSE_GetEfuseDefinition(padapter, EFUSE_WIFI, TYPE_AVAILABLE_EFUSE_BYTES_TOTAL, (void *)&max_available_size, false);
		if (max_available_size < 1) {
			err = -EFAULT;
			goto exit;
		}

		if (rtw_efuse_map_write(padapter, 0x00, EFUSE_MAX_MAP_LEN, pEfuseHal->fakeEfuseModifiedMap) == _FAIL) {
			DBG_88E("%s: rtw_efuse_map_write error!\n", __func__);
			err = -EFAULT;
			goto exit;
		}
	} else if (strcmp(tmp[0], "wlwfake") == 0) {
		if (!tmp[1] || !tmp[2]) {
			err = -EINVAL;
			goto exit;
		}

		addr = simple_strtoul(tmp[1], &ptmp, 16);
		addr &= 0xFFF;

		cnts = strlen(tmp[2]);
		if (cnts % 2) {
			err = -EINVAL;
			goto exit;
		}
		cnts /= 2;
		if (cnts == 0) {
			err = -EINVAL;
			goto exit;
		}

		DBG_88E("%s: addr = 0x%X\n", __func__, addr);
		DBG_88E("%s: cnts =%d\n", __func__, cnts);
		DBG_88E("%s: map tmp data =%s\n", __func__, tmp[2]);

		for (jj = 0, kk = 0; jj < cnts; jj++, kk += 2)
			pEfuseHal->fakeEfuseModifiedMap[addr + jj] = key_2char2num(tmp[2][kk], tmp[2][kk + 1]);
	}

exit:
	kfree(setdata);
	kfree(ShadowMapBT);
	kfree(ShadowMapWiFi);
	kfree(setrawdata);

	rtw_pm_set_ips(padapter, ips_mode);
	rtw_pm_set_lps(padapter, lps_mode);

	return err;
}

/*
 * Input Format: %s,%d,%d
 *	%s is width, could be
 *		"b" for 1 byte
 *		"w" for WORD (2 bytes)
 *		"dw" for DWORD (4 bytes)
 *	1st %d is address(offset)
 *	2st %d is data to write
 */
static int rtw_mp_write_reg(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_point *wrqu, char *extra)
{
	char *pch, *pnext, *ptmp;
	char *width_str;
	char width;
	u32 addr, data;
	int ret;
	struct adapter *padapter = rtw_netdev_priv(dev);

	pch = extra;
	pnext = strpbrk(pch, ",.-");
	if (!pnext)
		return -EINVAL;
	*pnext = 0;
	width_str = pch;

	pch = pnext + 1;
	pnext = strpbrk(pch, ",.-");
	if (!pnext)
		return -EINVAL;
	*pnext = 0;
	addr = simple_strtoul(pch, &ptmp, 16);
	if (addr > 0x3FFF)
		return -EINVAL;

	pch = pnext + 1;
	if ((pch - extra) >= wrqu->length)
		return -EINVAL;
	data = simple_strtoul(pch, &ptmp, 16);

	ret = 0;
	width = width_str[0];
	switch (width) {
	case 'b':
		/*  1 byte */
		if (data > 0xFF) {
			ret = -EINVAL;
			break;
		}
		rtw_write8(padapter, addr, data);
		break;
	case 'w':
		/*  2 bytes */
		if (data > 0xFFFF) {
			ret = -EINVAL;
			break;
		}
		rtw_write16(padapter, addr, data);
		break;
	case 'd':
		/*  4 bytes */
		rtw_write32(padapter, addr, data);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

/*
 * Input Format: %s,%d
 *	%s is width, could be
 *		"b" for 1 byte
 *		"w" for WORD (2 bytes)
 *		"dw" for DWORD (4 bytes)
 *	%d is address(offset)
 *
 * Return:
 *	%d for data readed
 */
static int rtw_mp_read_reg(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_point *wrqu, char *extra)
{
	struct adapter *padapter = rtw_netdev_priv(dev);
	char	*input = kmalloc(wrqu->length, GFP_KERNEL);
	char *pch, *pnext, *ptmp;
	char *width_str;
	char width;
	char data[20], tmp[20];
	u32 addr;
	u32 ret, i = 0, j = 0, strtout = 0;

	if (!input)
		return -ENOMEM;
	if (copy_from_user(input, wrqu->pointer, wrqu->length)) {
		kfree(input);
		return -EFAULT;
	}
	memset(data, 0, 20);
	memset(tmp, 0, 20);
	memset(extra, 0, wrqu->length);

	pch = input;
	pnext = strpbrk(pch, ",.-");
	if (!pnext) {
		kfree(input);
		return -EINVAL;
	}
	*pnext = 0;
	width_str = pch;

	pch = pnext + 1;
	if ((pch - input) >= wrqu->length) {
		kfree(input);
		return -EINVAL;
	}
	kfree(input);
	addr = simple_strtoul(pch, &ptmp, 16);
	if (addr > 0x3FFF)
		return -EINVAL;

	ret = 0;
	width = width_str[0];
	switch (width) {
	case 'b':
		/*  1 byte */
		sprintf(extra, "%d\n",  rtw_read8(padapter, addr));
		wrqu->length = strlen(extra);
		break;
	case 'w':
		/*  2 bytes */
		sprintf(data, "%04x\n", rtw_read16(padapter, addr));
		for (i = 0; i <= strlen(data); i++) {
			if (i % 2 == 0) {
				tmp[j] = ' ';
				j++;
			}
			if (data[i] != '\0')
				tmp[j] = data[i];
			j++;
		}
		pch = tmp;
		DBG_88E("pch =%s", pch);

		while (*pch != '\0') {
			pnext = strpbrk(pch, " ");
			if (!pnext)
				break;

			pnext++;
			if (*pnext != '\0') {
				  strtout = simple_strtoul(pnext, &ptmp, 16);
				  sprintf(extra, "%s %d", extra, strtout);
			} else {
				  break;
			}
			pch = pnext;
		}
		wrqu->length = 6;
		break;
	case 'd':
		/*  4 bytes */
		sprintf(data, "%08x", rtw_read32(padapter, addr));
		/* add read data format blank */
		for (i = 0; i <= strlen(data); i++) {
			if (i % 2 == 0) {
				tmp[j] = ' ';
				j++;
			}
			if (data[i] != '\0')
				tmp[j] = data[i];

			j++;
		}
		pch = tmp;
		DBG_88E("pch =%s", pch);

		while (*pch != '\0') {
			pnext = strpbrk(pch, " ");
			if (!pnext)
				break;
			pnext++;
			if (*pnext != '\0') {
				strtout = simple_strtoul(pnext, &ptmp, 16);
				sprintf(extra, "%s %d", extra, strtout);
			} else {
				break;
			}
			pch = pnext;
		}
		wrqu->length = strlen(extra);
		break;
	default:
		wrqu->length = 0;
		ret = -EINVAL;
		break;
	}

	return ret;
}

/*
 * Input Format: %d,%x,%x
 *	%d is RF path, should be smaller than RF_PATH_MAX
 *	1st %x is address(offset)
 *	2st %x is data to write
 */
 static int rtw_mp_write_rf(struct net_device *dev,
			    struct iw_request_info *info,
			    struct iw_point *wrqu, char *extra)
{
	u32 path, addr, data;
	int ret;
	struct adapter *padapter = rtw_netdev_priv(dev);

	ret = sscanf(extra, "%d,%x,%x", &path, &addr, &data);
	if (ret < 3)
		return -EINVAL;

	if (path >= RF_PATH_MAX)
		return -EINVAL;
	if (addr > 0xFF)
		return -EINVAL;
	if (data > 0xFFFFF)
		return -EINVAL;

	memset(extra, 0, wrqu->length);

	write_rfreg(padapter, path, addr, data);

	sprintf(extra, "write_rf completed\n");
	wrqu->length = strlen(extra);

	return 0;
}

/*
 * Input Format: %d,%x
 *	%d is RF path, should be smaller than RF_PATH_MAX
 *	%x is address(offset)
 *
 * Return:
 *	%d for data readed
 */
static int rtw_mp_read_rf(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_point *wrqu, char *extra)
{
	char	*input = kmalloc(wrqu->length, GFP_KERNEL);
	char *pch, *pnext, *ptmp;
	char data[20], tmp[20];
	u32 path, addr;
	u32 ret, i = 0, j = 0, strtou = 0;
	struct adapter *padapter = rtw_netdev_priv(dev);

	if (!input)
		return -ENOMEM;
	if (copy_from_user(input, wrqu->pointer, wrqu->length)) {
		kfree(input);
		return -EFAULT;
	}
	ret = sscanf(input, "%d,%x", &path, &addr);
	kfree(input);
	if (ret < 2)
		return -EINVAL;

	if (path >= RF_PATH_MAX)
		return -EINVAL;
	if (addr > 0xFF)
		return -EINVAL;

	memset(extra, 0, wrqu->length);

	sprintf(data, "%08x", read_rfreg(padapter, path, addr));
	/* add read data format blank */
	for (i = 0; i <= strlen(data); i++) {
		if (i % 2 == 0) {
			tmp[j] = ' ';
			j++;
		}
		tmp[j] = data[i];
		j++;
	}
	pch = tmp;
	DBG_88E("pch =%s", pch);

	while (*pch != '\0') {
		pnext = strpbrk(pch, " ");
		pnext++;
		if (*pnext != '\0') {
			  strtou = simple_strtoul(pnext, &ptmp, 16);
			  sprintf(extra, "%s %d", extra, strtou);
		} else {
			  break;
		}
		pch = pnext;
	}
	wrqu->length = strlen(extra);
	return 0;
}

static int rtw_mp_start(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_point *wrqu, char *extra)
{
	struct adapter *padapter = rtw_netdev_priv(dev);

	if (padapter->registrypriv.mp_mode == 0) {
		padapter->registrypriv.mp_mode = 1;

		rtw_pm_set_ips(padapter, IPS_NONE);
		LeaveAllPowerSaveMode(padapter);

		MPT_InitializeAdapter(padapter, 1);
	}
	if (padapter->registrypriv.mp_mode == 0)
		return -EPERM;
	if (padapter->mppriv.mode == MP_OFF) {
		if (mp_start_test(padapter) == _FAIL)
			return -EPERM;
		padapter->mppriv.mode = MP_ON;
	}
	return 0;
}

static int rtw_mp_stop(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_point *wrqu, char *extra)
{
	struct adapter *padapter = rtw_netdev_priv(dev);

	if (padapter->registrypriv.mp_mode == 1) {
		MPT_DeInitAdapter(padapter);
		padapter->registrypriv.mp_mode = 0;
	}

	if (padapter->mppriv.mode != MP_OFF) {
		mp_stop_test(padapter);
		padapter->mppriv.mode = MP_OFF;
	}

	return 0;
}

extern int wifirate2_ratetbl_inx(unsigned char rate);

static int rtw_mp_rate(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_point *wrqu, char *extra)
{
	u32 rate = MPT_RATE_1M;
	char	*input = kmalloc(wrqu->length, GFP_KERNEL);
	struct adapter *padapter = rtw_netdev_priv(dev);

	if (!input)
		return -ENOMEM;
	if (copy_from_user(input, wrqu->pointer, wrqu->length)) {
		kfree(input);
		return -EFAULT;
	}
	rate = rtw_atoi(input);
	sprintf(extra, "Set data rate to %d", rate);
	kfree(input);
	if (rate <= 0x7f)
		rate = wifirate2_ratetbl_inx((u8)rate);
	else
		rate = (rate - 0x80 + MPT_RATE_MCS0);

	if (rate >= MPT_RATE_LAST)
		return -EINVAL;

	padapter->mppriv.rateidx = rate;
	Hal_SetDataRate(padapter);

	wrqu->length = strlen(extra) + 1;
	return 0;
}

static int rtw_mp_channel(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_point *wrqu, char *extra)
{
	struct adapter *padapter = rtw_netdev_priv(dev);
	char	*input = kmalloc(wrqu->length, GFP_KERNEL);
	u32	channel = 1;

	if (!input)
		return -ENOMEM;
	if (copy_from_user(input, wrqu->pointer, wrqu->length)) {
		kfree(input);
		return -EFAULT;
	}
	channel = rtw_atoi(input);
	sprintf(extra, "Change channel %d to channel %d", padapter->mppriv.channel, channel);

	padapter->mppriv.channel = channel;
	Hal_SetChannel(padapter);

	wrqu->length = strlen(extra) + 1;
	kfree(input);
	return 0;
}

static int rtw_mp_bandwidth(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_point *wrqu, char *extra)
{
	u32 bandwidth = 0, sg = 0;
	struct adapter *padapter = rtw_netdev_priv(dev);

	sscanf(extra, "40M =%d, shortGI =%d", &bandwidth, &sg);

	if (bandwidth != HT_CHANNEL_WIDTH_40)
		bandwidth = HT_CHANNEL_WIDTH_20;

	padapter->mppriv.bandwidth = (u8)bandwidth;
	padapter->mppriv.preamble = sg;

	SetBandwidth(padapter);

	return 0;
}

static int rtw_mp_txpower(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_point *wrqu, char *extra)
{
	u32		idx_a = 0, idx_b = 0;
	char	*input = kmalloc(wrqu->length, GFP_KERNEL);
	struct adapter *padapter = rtw_netdev_priv(dev);

	if (!input)
		return -ENOMEM;
	if (copy_from_user(input, wrqu->pointer, wrqu->length)) {
		kfree(input);
		return -EFAULT;
	}
	sscanf(input, "patha =%d, pathb =%d", &idx_a, &idx_b);

	sprintf(extra, "Set power level path_A:%d path_B:%d", idx_a, idx_b);
	padapter->mppriv.txpoweridx = (u8)idx_a;
	padapter->mppriv.txpoweridx_b = (u8)idx_b;
	padapter->mppriv.bSetTxPower = 1;
	Hal_SetAntennaPathPower(padapter);

	wrqu->length = strlen(extra) + 1;
	kfree(input);
	return 0;
}

static int rtw_mp_ant_tx(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_point *wrqu, char *extra)
{
	u8 i;
	char	*input = kmalloc(wrqu->length, GFP_KERNEL);
	u16 antenna = 0;
	struct adapter *padapter = rtw_netdev_priv(dev);

	if (!input)
		return -ENOMEM;
	if (copy_from_user(input, wrqu->pointer, wrqu->length)) {
		kfree(input);
		return -EFAULT;
	}

	sprintf(extra, "switch Tx antenna to %s", input);

	for (i = 0; i < strlen(input); i++) {
		switch (input[i]) {
		case 'a':
			antenna |= ANTENNA_A;
			break;
		case 'b':
			antenna |= ANTENNA_B;
			break;
		}
	}
	padapter->mppriv.antenna_tx = antenna;

	Hal_SetAntenna(padapter);

	wrqu->length = strlen(extra) + 1;
	kfree(input);
	return 0;
}

static int rtw_mp_ant_rx(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_point *wrqu, char *extra)
{
	u8 i;
	u16 antenna = 0;
	char	*input = kmalloc(wrqu->length, GFP_KERNEL);
	struct adapter *padapter = rtw_netdev_priv(dev);

	if (!input)
		return -ENOMEM;
	if (copy_from_user(input, wrqu->pointer, wrqu->length)) {
		kfree(input);
		return -EFAULT;
	}
	memset(extra, 0, wrqu->length);

	sprintf(extra, "switch Rx antenna to %s", input);

	for (i = 0; i < strlen(input); i++) {
		switch (input[i]) {
		case 'a':
			antenna |= ANTENNA_A;
			break;
		case 'b':
			antenna |= ANTENNA_B;
			break;
		}
	}

	padapter->mppriv.antenna_rx = antenna;
	Hal_SetAntenna(padapter);
	wrqu->length = strlen(extra);
	kfree(input);
	return 0;
}

static int rtw_mp_ctx(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_point *wrqu, char *extra)
{
	u32 pkTx = 1, countPkTx = 1, cotuTx = 1, CarrSprTx = 1, scTx = 1, sgleTx = 1, stop = 1;
	u32 bStartTest = 1;
	u32 count = 0;
	struct mp_priv *pmp_priv;
	struct pkt_attrib *pattrib;

	struct adapter *padapter = rtw_netdev_priv(dev);

	pmp_priv = &padapter->mppriv;

	if (copy_from_user(extra, wrqu->pointer, wrqu->length))
			return -EFAULT;

	DBG_88E("%s: in =%s\n", __func__, extra);

	countPkTx = strncmp(extra, "count =", 5); /*  strncmp true is 0 */
	cotuTx = strncmp(extra, "background", 20);
	CarrSprTx = strncmp(extra, "background, cs", 20);
	scTx = strncmp(extra, "background, sc", 20);
	sgleTx = strncmp(extra, "background, stone", 20);
	pkTx = strncmp(extra, "background, pkt", 20);
	stop = strncmp(extra, "stop", 4);
	sscanf(extra, "count =%d, pkt", &count);

	memset(extra, '\0', sizeof(*extra));

	if (stop == 0) {
		bStartTest = 0; /*  To set Stop */
		pmp_priv->tx.stop = 1;
		sprintf(extra, "Stop continuous Tx");
	} else {
		bStartTest = 1;
		if (pmp_priv->mode != MP_ON) {
			if (pmp_priv->tx.stop != 1) {
				DBG_88E("%s: MP_MODE != ON %d\n", __func__, pmp_priv->mode);
				return  -EFAULT;
			}
		}
	}

	if (pkTx == 0 || countPkTx == 0)
		pmp_priv->mode = MP_PACKET_TX;
	if (sgleTx == 0)
		pmp_priv->mode = MP_SINGLE_TONE_TX;
	if (cotuTx == 0)
		pmp_priv->mode = MP_CONTINUOUS_TX;
	if (CarrSprTx == 0)
		pmp_priv->mode = MP_CARRIER_SUPPRISSION_TX;
	if (scTx == 0)
		pmp_priv->mode = MP_SINGLE_CARRIER_TX;

	switch (pmp_priv->mode) {
	case MP_PACKET_TX:
		if (bStartTest == 0) {
			pmp_priv->tx.stop = 1;
			pmp_priv->mode = MP_ON;
			sprintf(extra, "Stop continuous Tx");
		} else if (pmp_priv->tx.stop == 1) {
			sprintf(extra, "Start continuous DA = ffffffffffff len = 1500 count =%u,\n", count);
			pmp_priv->tx.stop = 0;
			pmp_priv->tx.count = count;
			pmp_priv->tx.payload = 2;
			pattrib = &pmp_priv->tx.attrib;
			pattrib->pktlen = 1500;
			memset(pattrib->dst, 0xFF, ETH_ALEN);
			SetPacketTx(padapter);
		} else {
			return -EFAULT;
		}
			wrqu->length = strlen(extra);
			return 0;
	case MP_SINGLE_TONE_TX:
		if (bStartTest != 0)
			sprintf(extra, "Start continuous DA = ffffffffffff len = 1500\n infinite = yes.");
		Hal_SetSingleToneTx(padapter, (u8)bStartTest);
		break;
	case MP_CONTINUOUS_TX:
		if (bStartTest != 0)
			sprintf(extra, "Start continuous DA = ffffffffffff len = 1500\n infinite = yes.");
		Hal_SetContinuousTx(padapter, (u8)bStartTest);
		break;
	case MP_CARRIER_SUPPRISSION_TX:
		if (bStartTest != 0) {
			if (pmp_priv->rateidx <= MPT_RATE_11M) {
				sprintf(extra, "Start continuous DA = ffffffffffff len = 1500\n infinite = yes.");
				Hal_SetCarrierSuppressionTx(padapter, (u8)bStartTest);
			} else {
				sprintf(extra, "Specify carrier suppression but not CCK rate");
			}
		}
		break;
	case MP_SINGLE_CARRIER_TX:
		if (bStartTest != 0)
			sprintf(extra, "Start continuous DA = ffffffffffff len = 1500\n infinite = yes.");
		Hal_SetSingleCarrierTx(padapter, (u8)bStartTest);
		break;
	default:
		sprintf(extra, "Error! Continuous-Tx is not on-going.");
		return -EFAULT;
	}

	if (bStartTest == 1 && pmp_priv->mode != MP_ON) {
		struct mp_priv *pmp_priv = &padapter->mppriv;
		if (pmp_priv->tx.stop == 0) {
			pmp_priv->tx.stop = 1;
			msleep(5);
		}
		pmp_priv->tx.stop = 0;
		pmp_priv->tx.count = 1;
		SetPacketTx(padapter);
	} else {
		pmp_priv->mode = MP_ON;
	}

	wrqu->length = strlen(extra);
	return 0;
}

static int rtw_mp_arx(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_point *wrqu, char *extra)
{
	u8 bStartRx = 0, bStopRx = 0, bQueryPhy;
	u32 cckok = 0, cckcrc = 0, ofdmok = 0, ofdmcrc = 0, htok = 0, htcrc = 0, OFDM_FA = 0, CCK_FA = 0;
	char	*input = kmalloc(wrqu->length, GFP_KERNEL);
	struct adapter *padapter = rtw_netdev_priv(dev);

	if (!input)
		return -ENOMEM;

	if (copy_from_user(input, wrqu->pointer, wrqu->length)) {
		kfree(input);
		return -EFAULT;
	}
	DBG_88E("%s: %s\n", __func__, input);

	bStartRx = (strncmp(input, "start", 5) == 0) ? 1 : 0; /*  strncmp true is 0 */
	bStopRx = (strncmp(input, "stop", 5) == 0) ? 1 : 0; /*  strncmp true is 0 */
	bQueryPhy = (strncmp(input, "phy", 3) == 0) ? 1 : 0; /*  strncmp true is 0 */

	if (bStartRx) {
		sprintf(extra, "start");
		SetPacketRx(padapter, bStartRx);
	} else if (bStopRx) {
		SetPacketRx(padapter, 0);
		sprintf(extra, "Received packet OK:%d CRC error:%d", padapter->mppriv.rx_pktcount, padapter->mppriv.rx_crcerrpktcount);
	} else if (bQueryPhy) {
		/*
		OFDM FA
		RegCF0[15:0]
		RegCF2[31:16]
		RegDA0[31:16]
		RegDA4[15:0]
		RegDA4[31:16]
		RegDA8[15:0]
		CCK FA
		(RegA5B<<8) | RegA5C
		*/
		cckok = read_bbreg(padapter, 0xf88, 0xffffffff);
		cckcrc = read_bbreg(padapter, 0xf84, 0xffffffff);
		ofdmok = read_bbreg(padapter, 0xf94, 0x0000FFFF);
		ofdmcrc = read_bbreg(padapter, 0xf94, 0xFFFF0000);
		htok = read_bbreg(padapter, 0xf90, 0x0000FFFF);
		htcrc = read_bbreg(padapter, 0xf90, 0xFFFF0000);

		OFDM_FA = read_bbreg(padapter, 0xcf0, 0x0000FFFF);
		OFDM_FA = read_bbreg(padapter, 0xcf2, 0xFFFF0000);
		OFDM_FA = read_bbreg(padapter, 0xda0, 0xFFFF0000);
		OFDM_FA = read_bbreg(padapter, 0xda4, 0x0000FFFF);
		OFDM_FA = read_bbreg(padapter, 0xda4, 0xFFFF0000);
		OFDM_FA = read_bbreg(padapter, 0xda8, 0x0000FFFF);
		CCK_FA = (rtw_read8(padapter, 0xa5b) << 8) | (rtw_read8(padapter, 0xa5c));

		sprintf(extra, "Phy Received packet OK:%d CRC error:%d FA Counter: %d", cckok + ofdmok + htok, cckcrc + ofdmcrc + htcrc, OFDM_FA + CCK_FA);
	}
	wrqu->length = strlen(extra) + 1;
	kfree(input);
	return 0;
}

static int rtw_mp_trx_query(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_point *wrqu, char *extra)
{
	u32 txok, txfail, rxok, rxfail;
	struct adapter *padapter = rtw_netdev_priv(dev);

	txok = padapter->mppriv.tx.sended;
	txfail = 0;
	rxok = padapter->mppriv.rx_pktcount;
	rxfail = padapter->mppriv.rx_crcerrpktcount;

	memset(extra, '\0', 128);

	sprintf(extra, "Tx OK:%d, Tx Fail:%d, Rx OK:%d, CRC error:%d ", txok, txfail, rxok, rxfail);

	wrqu->length = strlen(extra) + 1;

	return 0;
}

static int rtw_mp_pwrtrk(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_point *wrqu, char *extra)
{
	u8 enable;
	u32 thermal;
	s32 ret;
	struct adapter *padapter = rtw_netdev_priv(dev);
	char	*input = kmalloc(wrqu->length, GFP_KERNEL);

	if (!input)
		return -ENOMEM;
	if (copy_from_user(input, wrqu->pointer, wrqu->length)) {
		kfree(input);
		return -EFAULT;
	}
	memset(extra, 0, wrqu->length);

	enable = 1;
	if (wrqu->length > 1) {/*  not empty string */
		if (strncmp(input, "stop", 4) == 0) {
			enable = 0;
			sprintf(extra, "mp tx power tracking stop");
		} else if (sscanf(input, "ther =%d", &thermal)) {
				ret = Hal_SetThermalMeter(padapter, (u8)thermal);
				if (ret == _FAIL)
					return -EPERM;
				sprintf(extra, "mp tx power tracking start, target value =%d ok ", thermal);
		} else {
			kfree(input);
			return -EINVAL;
		}
	}

	kfree(input);
	ret = Hal_SetPowerTracking(padapter, enable);
	if (ret == _FAIL)
		return -EPERM;

	wrqu->length = strlen(extra);
	return 0;
}

static int rtw_mp_psd(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_point *wrqu, char *extra)
{
	struct adapter *padapter = rtw_netdev_priv(dev);
	char	*input = kmalloc(wrqu->length, GFP_KERNEL);

	if (!input)
		return -ENOMEM;
	if (copy_from_user(input, wrqu->pointer, wrqu->length)) {
		kfree(input);
		return -EFAULT;
	}

	strcpy(extra, input);

	wrqu->length = mp_query_psd(padapter, extra);
	kfree(input);
	return 0;
}

static int rtw_mp_thermal(struct net_device *dev,
			  struct iw_request_info *info,
			  struct iw_point *wrqu, char *extra)
{
	u8 val;
	u16 bwrite = 1;
	u16 addr = EEPROM_THERMAL_METER_88E;

	u16 cnt = 1;
	u16 max_available_size = 0;
	struct adapter *padapter = rtw_netdev_priv(dev);

	if (copy_from_user(extra, wrqu->pointer, wrqu->length))
		return -EFAULT;

	bwrite = strncmp(extra, "write", 6); /*  strncmp true is 0 */

	Hal_GetThermalMeter(padapter, &val);

	if (bwrite == 0) {
		EFUSE_GetEfuseDefinition(padapter, EFUSE_WIFI, TYPE_AVAILABLE_EFUSE_BYTES_TOTAL, (void *)&max_available_size, false);
		if (2 > max_available_size) {
			DBG_88E("no available efuse!\n");
			return -EFAULT;
		}
		if (rtw_efuse_map_write(padapter, addr, cnt, &val) == _FAIL) {
			DBG_88E("rtw_efuse_map_write error\n");
			return -EFAULT;
		} else {
			 sprintf(extra, " efuse write ok :%d", val);
		}
	 } else {
			 sprintf(extra, "%d", val);
	 }
	wrqu->length = strlen(extra);

	return 0;
}

static int rtw_mp_reset_stats(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_point *wrqu, char *extra)
{
	struct mp_priv *pmp_priv;
	struct adapter *padapter = rtw_netdev_priv(dev);

	pmp_priv = &padapter->mppriv;

	pmp_priv->tx.sended = 0;
	pmp_priv->tx_pktcount = 0;
	pmp_priv->rx_pktcount = 0;
	pmp_priv->rx_crcerrpktcount = 0;

	/* reset phy counter */
	write_bbreg(padapter, 0xf14, BIT(16), 0x1);
	msleep(10);
	write_bbreg(padapter, 0xf14, BIT(16), 0x0);

	return 0;
}

static int rtw_mp_dump(struct net_device *dev,
		       struct iw_request_info *info,
		       struct iw_point *wrqu, char *extra)
{
	u32 value;
	u8 rf_type, path_nums = 0;
	u32 i, j = 1, path;
	struct adapter *padapter = rtw_netdev_priv(dev);

	if (strncmp(extra, "all", 4) == 0) {
		DBG_88E("\n ======= MAC REG =======\n");
		for (i = 0x0; i < 0x300; i += 4) {
			if (j % 4 == 1)
				DBG_88E("0x%02x", i);
			DBG_88E(" 0x%08x ", rtw_read32(padapter, i));
			if ((j++) % 4 == 0)
				DBG_88E("\n");
		}
		for (i = 0x400; i < 0x1000; i += 4) {
			if (j % 4 == 1)
				DBG_88E("0x%02x", i);
			DBG_88E(" 0x%08x ", rtw_read32(padapter, i));
			if ((j++) % 4 == 0)
				DBG_88E("\n");
		}

		j = 1;
		rtw_hal_get_hwreg(padapter, HW_VAR_RF_TYPE, (u8 *)(&rf_type));

		DBG_88E("\n ======= RF REG =======\n");
		if ((RF_1T2R == rf_type) || (RF_1T1R == rf_type))
			path_nums = 1;
		else
			path_nums = 2;

		for (path = 0; path < path_nums; path++) {
			for (i = 0; i < 0x34; i++) {
				value = rtw_hal_read_rfreg(padapter, path, i, 0xffffffff);
				if (j % 4 == 1)
					DBG_88E("0x%02x ", i);
				DBG_88E(" 0x%08x ", value);
				if ((j++) % 4 == 0)
					DBG_88E("\n");
			}
		}
	}
	return 0;
}

static int rtw_mp_phypara(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_point *wrqu, char *extra)
{
	char	*input = kmalloc(wrqu->length, GFP_KERNEL);
	u32		valxcap;

	if (!input)
		return -ENOMEM;
	if (copy_from_user(input, wrqu->pointer, wrqu->length)) {
		kfree(input);
		return -EFAULT;
	}

	DBG_88E("%s:iwpriv in =%s\n", __func__, input);

	sscanf(input, "xcap =%d", &valxcap);

	kfree(input);
	return 0;
}

static int rtw_mp_SetRFPath(struct net_device *dev,
			struct iw_request_info *info,
			union iwreq_data *wrqu, char *extra)
{
	struct adapter *padapter = rtw_netdev_priv(dev);
	char	*input = kmalloc(wrqu->data.length, GFP_KERNEL);
	u8 bMain = 1, bTurnoff = 1;

	if (!input)
		return -ENOMEM;
	if (copy_from_user(input, wrqu->data.pointer, wrqu->data.length))
			return -EFAULT;
	DBG_88E("%s:iwpriv in =%s\n", __func__, input);

	bMain = strncmp(input, "1", 2); /*  strncmp true is 0 */
	bTurnoff = strncmp(input, "0", 3); /*  strncmp true is 0 */

	if (bMain == 0) {
		MP_PHY_SetRFPathSwitch(padapter, true);
		DBG_88E("%s:PHY_SetRFPathSwitch = true\n", __func__);
	} else if (bTurnoff == 0) {
		MP_PHY_SetRFPathSwitch(padapter, false);
		DBG_88E("%s:PHY_SetRFPathSwitch = false\n", __func__);
	}
	kfree(input);
	return 0;
}

static int rtw_mp_QueryDrv(struct net_device *dev,
			struct iw_request_info *info,
			union iwreq_data *wrqu, char *extra)
{
	struct adapter *padapter = rtw_netdev_priv(dev);
	char	*input = kmalloc(wrqu->data.length, GFP_KERNEL);
	u8 qAutoLoad = 1;
	struct eeprom_priv *pEEPROM = GET_EEPROM_EFUSE_PRIV(padapter);

	if (!input)
		return -ENOMEM;

	if (copy_from_user(input, wrqu->data.pointer, wrqu->data.length))
			return -EFAULT;
	DBG_88E("%s:iwpriv in =%s\n", __func__, input);

	qAutoLoad = strncmp(input, "autoload", 8); /*  strncmp true is 0 */

	if (qAutoLoad == 0) {
		DBG_88E("%s:qAutoLoad\n", __func__);

		if (pEEPROM->bautoload_fail_flag)
			sprintf(extra, "fail");
		else
		sprintf(extra, "ok");
	}
	wrqu->data.length = strlen(extra) + 1;
	kfree(input);
	return 0;
}

static int rtw_mp_set(struct net_device *dev,
		      struct iw_request_info *info,
		      union iwreq_data *wdata, char *extra)
{
	struct iw_point *wrqu = (struct iw_point *)wdata;
	u32 subcmd = wrqu->flags;
	struct adapter *padapter = rtw_netdev_priv(dev);

	if (!padapter)
		return -ENETDOWN;

	if (!extra) {
		wrqu->length = 0;
		return -EIO;
	}

	switch (subcmd) {
	case MP_START:
		DBG_88E("set case mp_start\n");
		rtw_mp_start(dev, info, wrqu, extra);
		break;
	case MP_STOP:
		DBG_88E("set case mp_stop\n");
		rtw_mp_stop(dev, info, wrqu, extra);
		break;
	case MP_BANDWIDTH:
		DBG_88E("set case mp_bandwidth\n");
		rtw_mp_bandwidth(dev, info, wrqu, extra);
		break;
	case MP_RESET_STATS:
		DBG_88E("set case MP_RESET_STATS\n");
		rtw_mp_reset_stats(dev, info, wrqu, extra);
		break;
	case MP_SetRFPathSwh:
		DBG_88E("set MP_SetRFPathSwitch\n");
		rtw_mp_SetRFPath(dev, info, wdata, extra);
		break;
	case CTA_TEST:
		DBG_88E("set CTA_TEST\n");
		rtw_cta_test_start(dev, info, wdata, extra);
		break;
	}

	return 0;
}

static int rtw_mp_get(struct net_device *dev,
			struct iw_request_info *info,
			union iwreq_data *wdata, char *extra)
{
	struct iw_point *wrqu = (struct iw_point *)wdata;
	u32 subcmd = wrqu->flags;
	struct adapter *padapter = rtw_netdev_priv(dev);

	if (!padapter)
		return -ENETDOWN;
	if (!extra) {
		wrqu->length = 0;
		return -EIO;
	}

	switch (subcmd) {
	case WRITE_REG:
		rtw_mp_write_reg(dev, info, wrqu, extra);
		 break;
	case WRITE_RF:
		rtw_mp_write_rf(dev, info, wrqu, extra);
		 break;
	case MP_PHYPARA:
		DBG_88E("mp_get  MP_PHYPARA\n");
		rtw_mp_phypara(dev, info, wrqu, extra);
		break;
	case MP_CHANNEL:
		DBG_88E("set case mp_channel\n");
		rtw_mp_channel(dev, info, wrqu, extra);
		break;
	case READ_REG:
		DBG_88E("mp_get  READ_REG\n");
		rtw_mp_read_reg(dev, info, wrqu, extra);
		break;
	case READ_RF:
		DBG_88E("mp_get  READ_RF\n");
		rtw_mp_read_rf(dev, info, wrqu, extra);
		break;
	case MP_RATE:
		DBG_88E("set case mp_rate\n");
		rtw_mp_rate(dev, info, wrqu, extra);
		break;
	case MP_TXPOWER:
		DBG_88E("set case MP_TXPOWER\n");
		rtw_mp_txpower(dev, info, wrqu, extra);
		break;
	case MP_ANT_TX:
		DBG_88E("set case MP_ANT_TX\n");
		rtw_mp_ant_tx(dev, info, wrqu, extra);
		break;
	case MP_ANT_RX:
		DBG_88E("set case MP_ANT_RX\n");
		rtw_mp_ant_rx(dev, info, wrqu, extra);
		break;
	case MP_QUERY:
		rtw_mp_trx_query(dev, info, wrqu, extra);
		break;
	case MP_CTX:
		DBG_88E("set case MP_CTX\n");
		rtw_mp_ctx(dev, info, wrqu, extra);
		break;
	case MP_ARX:
		DBG_88E("set case MP_ARX\n");
		rtw_mp_arx(dev, info, wrqu, extra);
		break;
	case EFUSE_GET:
		DBG_88E("efuse get EFUSE_GET\n");
		rtw_mp_efuse_get(dev, info, wdata, extra);
		break;
	case MP_DUMP:
		DBG_88E("set case MP_DUMP\n");
		rtw_mp_dump(dev, info, wrqu, extra);
		break;
	case MP_PSD:
		DBG_88E("set case MP_PSD\n");
		rtw_mp_psd(dev, info, wrqu, extra);
		break;
	case MP_THER:
		DBG_88E("set case MP_THER\n");
		rtw_mp_thermal(dev, info, wrqu, extra);
		break;
	case MP_QueryDrvStats:
		DBG_88E("mp_get MP_QueryDrvStats\n");
		rtw_mp_QueryDrv(dev, info, wdata, extra);
		break;
	case MP_PWRTRK:
		DBG_88E("set case MP_PWRTRK\n");
		rtw_mp_pwrtrk(dev, info, wrqu, extra);
		break;
	case EFUSE_SET:
		DBG_88E("set case efuse set\n");
		rtw_mp_efuse_set(dev, info, wdata, extra);
		break;
	}

	msleep(10); /* delay 5ms for sending pkt before exit adb shell operation */
	return 0;
}

static int rtw_tdls(struct net_device *dev,
		    struct iw_request_info *info,
		    union iwreq_data *wrqu, char *extra)
{
	return 0;
}

static int rtw_tdls_get(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *extra)
{
	return 0;
}

static int rtw_test(
	struct net_device *dev,
	struct iw_request_info *info,
	union iwreq_data *wrqu, char *extra)
{
	u32 len;
	u8 *pbuf, *pch;
	char *ptmp;
	u8 *delim = ",";

	DBG_88E("+%s\n", __func__);
	len = wrqu->data.length;

	pbuf = kzalloc(len, GFP_KERNEL);
	if (!pbuf) {
		DBG_88E("%s: no memory!\n", __func__);
		return -ENOMEM;
	}

	if (copy_from_user(pbuf, wrqu->data.pointer, len)) {
		kfree(pbuf);
		DBG_88E("%s: copy from user fail!\n", __func__);
		return -EFAULT;
	}
	DBG_88E("%s: string =\"%s\"\n", __func__, pbuf);

	ptmp = (char *)pbuf;
	pch = strsep(&ptmp, delim);
	if (!pch || strlen(pch) == 0) {
		kfree(pbuf);
		DBG_88E("%s: parameter error(level 1)!\n", __func__);
		return -EFAULT;
	}
	kfree(pbuf);
	return 0;
}

static iw_handler rtw_handlers[] = {
	IW_HANDLER(SIOCGIWNAME, rtw_wx_get_name),
	IW_HANDLER(SIOCSIWNWID, dummy),
	IW_HANDLER(SIOCGIWNWID, dummy),
	IW_HANDLER(SIOCGIWFREQ, rtw_wx_get_freq),
	IW_HANDLER(SIOCSIWMODE, rtw_wx_set_mode),
	IW_HANDLER(SIOCGIWMODE, rtw_wx_get_mode),
	IW_HANDLER(SIOCSIWSENS, dummy),
	IW_HANDLER(SIOCGIWSENS, rtw_wx_get_sens),
	IW_HANDLER(SIOCGIWRANGE, rtw_wx_get_range),
	IW_HANDLER(SIOCSIWPRIV, rtw_wx_set_priv),
	IW_HANDLER(SIOCSIWSPY, dummy),
	IW_HANDLER(SIOCGIWSPY, dummy),
	IW_HANDLER(SIOCSIWAP, rtw_wx_set_wap),
	IW_HANDLER(SIOCGIWAP, rtw_wx_get_wap),
	IW_HANDLER(SIOCSIWMLME, rtw_wx_set_mlme),
	IW_HANDLER(SIOCGIWAPLIST, dummy),
	IW_HANDLER(SIOCSIWSCAN, rtw_wx_set_scan),
	IW_HANDLER(SIOCGIWSCAN, rtw_wx_get_scan),
	IW_HANDLER(SIOCSIWESSID, rtw_wx_set_essid),
	IW_HANDLER(SIOCGIWESSID, rtw_wx_get_essid),
	IW_HANDLER(SIOCSIWNICKN, dummy),
	IW_HANDLER(SIOCGIWNICKN, rtw_wx_get_nick),
	IW_HANDLER(SIOCSIWRATE, rtw_wx_set_rate),
	IW_HANDLER(SIOCGIWRATE, rtw_wx_get_rate),
	IW_HANDLER(SIOCSIWRTS, rtw_wx_set_rts),
	IW_HANDLER(SIOCGIWRTS, rtw_wx_get_rts),
	IW_HANDLER(SIOCSIWFRAG, rtw_wx_set_frag),
	IW_HANDLER(SIOCGIWFRAG, rtw_wx_get_frag),
	IW_HANDLER(SIOCSIWTXPOW, dummy),
	IW_HANDLER(SIOCGIWTXPOW, dummy),
	IW_HANDLER(SIOCSIWRETRY, dummy),
	IW_HANDLER(SIOCGIWRETRY, rtw_wx_get_retry),
	IW_HANDLER(SIOCSIWENCODE, rtw_wx_set_enc),
	IW_HANDLER(SIOCGIWENCODE, rtw_wx_get_enc),
	IW_HANDLER(SIOCSIWPOWER, dummy),
	IW_HANDLER(SIOCGIWPOWER, rtw_wx_get_power),
	IW_HANDLER(SIOCSIWGENIE, rtw_wx_set_gen_ie),
	IW_HANDLER(SIOCSIWAUTH, rtw_wx_set_auth),
	IW_HANDLER(SIOCSIWENCODEEXT, rtw_wx_set_enc_ext),
	IW_HANDLER(SIOCSIWPMKSA, rtw_wx_set_pmkid),
};

static const struct iw_priv_args rtw_private_args[] = {
	{
		SIOCIWFIRSTPRIV + 0x0,
		IW_PRIV_TYPE_CHAR | 0x7FF, 0, "write"
	},
	{
		SIOCIWFIRSTPRIV + 0x1,
		IW_PRIV_TYPE_CHAR | 0x7FF,
		IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_FIXED | IFNAMSIZ, "read"
	},
	{
		SIOCIWFIRSTPRIV + 0x2, 0, 0, "driver_ext"
	},
	{
		SIOCIWFIRSTPRIV + 0x3, 0, 0, "mp_ioctl"
	},
	{
		SIOCIWFIRSTPRIV + 0x4,
		IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "apinfo"
	},
	{
		SIOCIWFIRSTPRIV + 0x5,
		IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 2, 0, "setpid"
	},
	{
		SIOCIWFIRSTPRIV + 0x6,
		IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "wps_start"
	},
	{
		SIOCIWFIRSTPRIV + 0x7,
		IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "get_sensitivity"
	},
	{
		SIOCIWFIRSTPRIV + 0x8,
		IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "wps_prob_req_ie"
	},
	{
		SIOCIWFIRSTPRIV + 0x9,
		IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "wps_assoc_req_ie"
	},

	{
		SIOCIWFIRSTPRIV + 0xA,
		IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "channel_plan"
	},

	{
		SIOCIWFIRSTPRIV + 0xB,
		IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 2, 0, "dbg"
	},
	{
		SIOCIWFIRSTPRIV + 0xC,
		IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 3, 0, "rfw"
	},
	{
		SIOCIWFIRSTPRIV + 0xD,
		IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 2, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_FIXED | IFNAMSIZ, "rfr"
	},
	{
		SIOCIWFIRSTPRIV + 0x10,
		IW_PRIV_TYPE_CHAR | P2P_PRIVATE_IOCTL_SET_LEN, 0, "p2p_set"
	},
	{
		SIOCIWFIRSTPRIV + 0x11,
		IW_PRIV_TYPE_CHAR | P2P_PRIVATE_IOCTL_SET_LEN, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_FIXED | P2P_PRIVATE_IOCTL_SET_LEN, "p2p_get"
	},
	{
		SIOCIWFIRSTPRIV + 0x12,
		IW_PRIV_TYPE_CHAR | P2P_PRIVATE_IOCTL_SET_LEN, IW_PRIV_TYPE_CHAR | IFNAMSIZ, "p2p_get2"
	},
	{SIOCIWFIRSTPRIV + 0x13, IW_PRIV_TYPE_CHAR | 128, 0, "NULL"},
	{
		SIOCIWFIRSTPRIV + 0x14,
		IW_PRIV_TYPE_CHAR  | 64, 0, "tdls"
	},
	{
		SIOCIWFIRSTPRIV + 0x15,
		IW_PRIV_TYPE_CHAR | P2P_PRIVATE_IOCTL_SET_LEN, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_FIXED | P2P_PRIVATE_IOCTL_SET_LEN, "tdls_get"
	},
	{
		SIOCIWFIRSTPRIV + 0x16,
		IW_PRIV_TYPE_CHAR | 64, 0, "pm_set"
	},

	{SIOCIWFIRSTPRIV + 0x18, IW_PRIV_TYPE_CHAR | IFNAMSIZ, 0, "rereg_nd_name"},

	{SIOCIWFIRSTPRIV + 0x1A, IW_PRIV_TYPE_CHAR | 1024, 0, "efuse_set"},
	{SIOCIWFIRSTPRIV + 0x1B, IW_PRIV_TYPE_CHAR | 128, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, "efuse_get"},
	{SIOCIWFIRSTPRIV + 0x1D, IW_PRIV_TYPE_CHAR | 40, IW_PRIV_TYPE_CHAR | 0x7FF, "test"
	},

	{SIOCIWFIRSTPRIV + 0x0E, IW_PRIV_TYPE_CHAR | 1024, 0, ""},  /* set */
	{SIOCIWFIRSTPRIV + 0x0F, IW_PRIV_TYPE_CHAR | 1024, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, ""},/* get */
/* --- sub-ioctls definitions --- */

	{MP_START, IW_PRIV_TYPE_CHAR | 1024, 0, "mp_start"}, /* set */
	{MP_PHYPARA, IW_PRIV_TYPE_CHAR | 1024, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, "mp_phypara"},/* get */
	{MP_STOP, IW_PRIV_TYPE_CHAR | 1024, 0, "mp_stop"}, /* set */
	{MP_CHANNEL, IW_PRIV_TYPE_CHAR | 1024, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, "mp_channel"},/* get */
	{MP_BANDWIDTH, IW_PRIV_TYPE_CHAR | 1024, 0, "mp_bandwidth"}, /* set */
	{MP_RATE, IW_PRIV_TYPE_CHAR | 1024, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, "mp_rate"},/* get */
	{MP_RESET_STATS, IW_PRIV_TYPE_CHAR | 1024, 0, "mp_reset_stats"},
	{MP_QUERY, IW_PRIV_TYPE_CHAR | 1024, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, "mp_query"}, /* get */
	{READ_REG, IW_PRIV_TYPE_CHAR | 1024, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, "read_reg"},
	{MP_RATE, IW_PRIV_TYPE_CHAR | 1024, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, "mp_rate"},
	{READ_RF, IW_PRIV_TYPE_CHAR | 1024, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, "read_rf"},
	{MP_PSD, IW_PRIV_TYPE_CHAR | 1024, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, "mp_psd"},
	{MP_DUMP, IW_PRIV_TYPE_CHAR | 1024, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, "mp_dump"},
	{MP_TXPOWER, IW_PRIV_TYPE_CHAR | 1024, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, "mp_txpower"},
	{MP_ANT_TX, IW_PRIV_TYPE_CHAR | 1024,  IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, "mp_ant_tx"},
	{MP_ANT_RX, IW_PRIV_TYPE_CHAR | 1024, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, "mp_ant_rx"},
	{WRITE_REG, IW_PRIV_TYPE_CHAR | 1024, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, "write_reg"},
	{WRITE_RF, IW_PRIV_TYPE_CHAR | 1024, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, "write_rf"},
	{MP_CTX, IW_PRIV_TYPE_CHAR | 1024, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, "mp_ctx"},
	{MP_ARX, IW_PRIV_TYPE_CHAR | 1024, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, "mp_arx"},
	{MP_THER, IW_PRIV_TYPE_CHAR | 1024, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, "mp_ther"},
	{EFUSE_SET, IW_PRIV_TYPE_CHAR | 1024, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, "efuse_set"},
	{EFUSE_GET, IW_PRIV_TYPE_CHAR | 1024, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, "efuse_get"},
	{MP_PWRTRK, IW_PRIV_TYPE_CHAR | 1024, 0, "mp_pwrtrk"},
	{MP_QueryDrvStats, IW_PRIV_TYPE_CHAR | 1024, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, "mp_drvquery"},
	{MP_IOCTL, IW_PRIV_TYPE_CHAR | 1024, 0, "mp_ioctl"}, /*  mp_ioctl */
	{MP_SetRFPathSwh, IW_PRIV_TYPE_CHAR | 1024, 0, "mp_setrfpath"},
	{CTA_TEST, IW_PRIV_TYPE_CHAR | 1024, 0, "cta_test"},
};

static iw_handler rtw_private_handler[] = {
rtw_wx_write32,				/* 0x00 */
rtw_wx_read32,				/* 0x01 */
rtw_drvext_hdl,				/* 0x02 */
rtw_mp_ioctl_hdl,			/* 0x03 */

/*  for MM DTV platform */
	rtw_get_ap_info,		/* 0x04 */

	rtw_set_pid,			/* 0x05 */
	rtw_wps_start,			/* 0x06 */

	rtw_wx_get_sensitivity,		/* 0x07 */
	rtw_wx_set_mtk_wps_probe_ie,	/* 0x08 */
	rtw_wx_set_mtk_wps_ie,		/* 0x09 */

/*  Set Channel depend on the country code */
	rtw_wx_set_channel_plan,	/* 0x0A */

	rtw_dbg_port,			/* 0x0B */
	rtw_wx_write_rf,		/* 0x0C */
	rtw_wx_read_rf,			/* 0x0D */

	rtw_mp_set,			/* 0x0E */
	rtw_mp_get,			/* 0x0F */
	rtw_p2p_set,			/* 0x10 */
	rtw_p2p_get,			/* 0x11 */
	rtw_p2p_get2,			/* 0x12 */

	NULL,				/* 0x13 */
	rtw_tdls,			/* 0x14 */
	rtw_tdls_get,			/* 0x15 */

	rtw_pm_set,			/* 0x16 */
	rtw_wx_priv_null,		/* 0x17 */
	rtw_rereg_nd_name,		/* 0x18 */
	rtw_wx_priv_null,		/* 0x19 */

	rtw_mp_efuse_set,		/* 0x1A */
	rtw_mp_efuse_get,		/* 0x1B */
	NULL,				/*  0x1C is reserved for hostapd */
	rtw_test,			/*  0x1D */
};

static struct iw_statistics *rtw_get_wireless_stats(struct net_device *dev)
{
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct iw_statistics *piwstats = &padapter->iwstats;
	int tmp_noise = 0;
	int tmp;

	if (!check_fwstate(&padapter->mlmepriv, _FW_LINKED)) {
		piwstats->qual.qual = 0;
		piwstats->qual.level = 0;
		piwstats->qual.noise = 0;
	} else {
		tmp_noise = padapter->recvpriv.noise;

		piwstats->qual.level = padapter->signal_strength;
		tmp = 219 + 3 * padapter->signal_strength;
		tmp = min(100, tmp);
		tmp = max(0, tmp);
		piwstats->qual.qual = tmp;
		piwstats->qual.noise = tmp_noise;
	}
	piwstats->qual.updated = IW_QUAL_ALL_UPDATED | IW_QUAL_DBM;
	return &padapter->iwstats;
}

struct iw_handler_def rtw_handlers_def = {
	.standard = rtw_handlers,
	.num_standard = sizeof(rtw_handlers) / sizeof(iw_handler),
	.private = rtw_private_handler,
	.private_args = (struct iw_priv_args *)rtw_private_args,
	.num_private = sizeof(rtw_private_handler) / sizeof(iw_handler),
	.num_private_args = sizeof(rtw_private_args) / sizeof(struct iw_priv_args),
	.get_wireless_stats = rtw_get_wireless_stats,
};
