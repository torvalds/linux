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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#define  _IOCTL_CFG80211_C_

#include <drv_types.h>

#ifdef CONFIG_IOCTL_CFG80211

#include <rtw_wifi_regd.h>

#define RTW_MAX_MGMT_TX_CNT (8)

#define RTW_SCAN_IE_LEN_MAX      2304
#define RTW_MAX_REMAIN_ON_CHANNEL_DURATION 3000 //ms
#define RTW_MAX_NUM_PMKIDS 4

#define RTW_CH_MAX_2G_CHANNEL               14      /* Max channel in 2G band */

#ifdef CONFIG_WAPI_SUPPORT

#ifndef WLAN_CIPHER_SUITE_SMS4
#define WLAN_CIPHER_SUITE_SMS4          0x00147201
#endif

#ifndef WLAN_AKM_SUITE_WAPI_PSK
#define WLAN_AKM_SUITE_WAPI_PSK         0x000FAC04
#endif

#ifndef WLAN_AKM_SUITE_WAPI_CERT
#define WLAN_AKM_SUITE_WAPI_CERT        0x000FAC12
#endif

#ifndef NL80211_WAPI_VERSION_1
#define NL80211_WAPI_VERSION_1          (1 << 2)
#endif

#endif

static const u32 rtw_cipher_suites[] = {
	WLAN_CIPHER_SUITE_WEP40,
	WLAN_CIPHER_SUITE_WEP104,
	WLAN_CIPHER_SUITE_TKIP,
	WLAN_CIPHER_SUITE_CCMP,
#ifdef CONFIG_WAPI_SUPPORT
	WLAN_CIPHER_SUITE_SMS4,
#endif // CONFIG_WAPI_SUPPORT
#ifdef CONFIG_IEEE80211W
	WLAN_CIPHER_SUITE_AES_CMAC,
#endif //CONFIG_IEEE80211W
};

#define RATETAB_ENT(_rate, _rateid, _flags) \
	{								\
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

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0))
/* if wowlan is not supported, kernel generate a disconnect at each suspend
 * cf: /net/wireless/sysfs.c, so register a stub wowlan.
 * Moreover wowlan has to be enabled via a the nl80211_set_wowlan callback.
 * (from user space, e.g. iw phy0 wowlan enable)
 */
static const struct wiphy_wowlan_support wowlan_stub = {
	.flags = WIPHY_WOWLAN_ANY,
	.n_patterns = 0,
	.pattern_max_len = 0,
	.pattern_min_len = 0,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0))
	.max_pkt_offset = 0,
#endif
};
#endif

static struct ieee80211_rate rtw_rates[] = {
	RATETAB_ENT(10,  0x1,   0),
	RATETAB_ENT(20,  0x2,   0),
	RATETAB_ENT(55,  0x4,   0),
	RATETAB_ENT(110, 0x8,   0),
	RATETAB_ENT(60,  0x10,  0),
	RATETAB_ENT(90,  0x20,  0),
	RATETAB_ENT(120, 0x40,  0),
	RATETAB_ENT(180, 0x80,  0),
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
	CHAN5G(34, 0),		CHAN5G(36, 0),
	CHAN5G(38, 0),		CHAN5G(40, 0),
	CHAN5G(42, 0),		CHAN5G(44, 0),
	CHAN5G(46, 0),		CHAN5G(48, 0),
	CHAN5G(52, 0),		CHAN5G(56, 0),
	CHAN5G(60, 0),		CHAN5G(64, 0),
	CHAN5G(100, 0),		CHAN5G(104, 0),
	CHAN5G(108, 0),		CHAN5G(112, 0),
	CHAN5G(116, 0),		CHAN5G(120, 0),
	CHAN5G(124, 0),		CHAN5G(128, 0),
	CHAN5G(132, 0),		CHAN5G(136, 0),
	CHAN5G(140, 0),		CHAN5G(149, 0),
	CHAN5G(153, 0),		CHAN5G(157, 0),
	CHAN5G(161, 0),		CHAN5G(165, 0),
	CHAN5G(184, 0),		CHAN5G(188, 0),
	CHAN5G(192, 0),		CHAN5G(196, 0),
	CHAN5G(200, 0),		CHAN5G(204, 0),
	CHAN5G(208, 0),		CHAN5G(212, 0),
	CHAN5G(216, 0),
};


void rtw_2g_channels_init(struct ieee80211_channel *channels)
{
	_rtw_memcpy((void*)channels, (void*)rtw_2ghz_channels,
		sizeof(struct ieee80211_channel)*RTW_2G_CHANNELS_NUM
	);
}

void rtw_5g_channels_init(struct ieee80211_channel *channels)
{
	_rtw_memcpy((void*)channels, (void*)rtw_5ghz_a_channels,
		sizeof(struct ieee80211_channel)*RTW_5G_CHANNELS_NUM
	);
}

void rtw_2g_rates_init(struct ieee80211_rate *rates)
{
	_rtw_memcpy(rates, rtw_g_rates,
		sizeof(struct ieee80211_rate)*RTW_G_RATES_NUM
	);
}

void rtw_5g_rates_init(struct ieee80211_rate *rates)
{
	_rtw_memcpy(rates, rtw_a_rates,
		sizeof(struct ieee80211_rate)*RTW_A_RATES_NUM
	);
}

struct ieee80211_supported_band *rtw_spt_band_alloc(
	enum ieee80211_band band
	)
{
	struct ieee80211_supported_band *spt_band = NULL;
	int n_channels, n_bitrates;

	if(band == IEEE80211_BAND_2GHZ)
	{
		n_channels = RTW_2G_CHANNELS_NUM;
		n_bitrates = RTW_G_RATES_NUM;
	}
	else if(band == IEEE80211_BAND_5GHZ)
	{
		n_channels = RTW_5G_CHANNELS_NUM;
		n_bitrates = RTW_A_RATES_NUM;
	}
	else
	{
		goto exit;
	}

	spt_band = (struct ieee80211_supported_band *)rtw_zmalloc(
		sizeof(struct ieee80211_supported_band)
		+ sizeof(struct ieee80211_channel)*n_channels
		+ sizeof(struct ieee80211_rate)*n_bitrates
	);
	if(!spt_band)
		goto exit;

	spt_band->channels = (struct ieee80211_channel*)(((u8*)spt_band)+sizeof(struct ieee80211_supported_band));
	spt_band->bitrates= (struct ieee80211_rate*)(((u8*)spt_band->channels)+sizeof(struct ieee80211_channel)*n_channels);
	spt_band->band = band;
	spt_band->n_channels = n_channels;
	spt_band->n_bitrates = n_bitrates;

	if(band == IEEE80211_BAND_2GHZ)
	{
		rtw_2g_channels_init(spt_band->channels);
		rtw_2g_rates_init(spt_band->bitrates);
	}
	else if(band == IEEE80211_BAND_5GHZ)
	{
		rtw_5g_channels_init(spt_band->channels);
		rtw_5g_rates_init(spt_band->bitrates);
	}

	//spt_band.ht_cap
	
exit:

	return spt_band;
}

void rtw_spt_band_free(struct ieee80211_supported_band *spt_band)
{
	u32 size = 0;

	if(!spt_band)
		return;
	
	if(spt_band->band == IEEE80211_BAND_2GHZ)
	{
		size = sizeof(struct ieee80211_supported_band)
			+ sizeof(struct ieee80211_channel)*RTW_2G_CHANNELS_NUM
			+ sizeof(struct ieee80211_rate)*RTW_G_RATES_NUM;
	}
	else if(spt_band->band == IEEE80211_BAND_5GHZ)
	{
		size = sizeof(struct ieee80211_supported_band)
			+ sizeof(struct ieee80211_channel)*RTW_5G_CHANNELS_NUM
			+ sizeof(struct ieee80211_rate)*RTW_A_RATES_NUM;		
	}
	else
	{
		
	}
	rtw_mfree((u8*)spt_band, size);
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)) || defined(COMPAT_KERNEL_RELEASE)
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
#endif

static int rtw_ieee80211_channel_to_frequency(int chan, int band)
{
	/* see 802.11 17.3.8.3.2 and Annex J
	* there are overlapping channel numbers in 5GHz and 2GHz bands */
           
	if (band == IEEE80211_BAND_5GHZ) {
       	if (chan >= 182 && chan <= 196)
			return 4000 + chan * 5;
             else
                    return 5000 + chan * 5;
       } else { /* IEEE80211_BAND_2GHZ */
		if (chan == 14)
			return 2484;
             else if (chan < 14)
			return 2407 + chan * 5;
             else
			return 0; /* not supported */
	}
}

#define MAX_BSSINFO_LEN 1000
struct cfg80211_bss *rtw_cfg80211_inform_bss(_adapter *padapter, struct wlan_network *pnetwork)
{
	struct ieee80211_channel *notify_channel;
	struct cfg80211_bss *bss = NULL;
	//struct ieee80211_supported_band *band;       
	u16 channel;
	u32 freq;
	u64 notify_timestamp;
	u16 notify_capability;
	u16 notify_interval;
	u8 *notify_ie;
	size_t notify_ielen;
	s32 notify_signal;
	u8 buf[MAX_BSSINFO_LEN], *pbuf;
	size_t len,bssinf_len=0;
	struct rtw_ieee80211_hdr *pwlanhdr;
	unsigned short *fctrl;
	u8	bc_addr[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
		
	struct wireless_dev *wdev = padapter->rtw_wdev;
	struct wiphy *wiphy = wdev->wiphy;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);


	//DBG_8192C("%s\n", __func__);

	bssinf_len = pnetwork->network.IELength+sizeof (struct rtw_ieee80211_hdr_3addr);
	if(bssinf_len > MAX_BSSINFO_LEN){
		DBG_871X("%s IE Length too long > %d byte \n",__FUNCTION__,MAX_BSSINFO_LEN);
		goto exit;
	}

#ifndef CONFIG_WAPI_SUPPORT
	{
		u16 wapi_len = 0;
	
		if(rtw_get_wapi_ie(pnetwork->network.IEs, pnetwork->network.IELength, NULL, &wapi_len)>0)
		{
			if(wapi_len > 0)
			{
				DBG_871X("%s, no support wapi!\n",__FUNCTION__);
				goto exit;
			}	
		}		
	}	
#endif //!CONFIG_WAPI_SUPPORT

	//To reduce PBC Overlap rate
	//_enter_critical_bh(&pwdev_priv->scan_req_lock, &irqL);
	if(adapter_wdev_data(padapter)->scan_request != NULL)
	{
		u8 *psr=NULL, sr = 0;
		NDIS_802_11_SSID *pssid = &pnetwork->network.Ssid;
		struct cfg80211_scan_request *request = adapter_wdev_data(padapter)->scan_request;
		struct cfg80211_ssid *ssids = request->ssids;
		u32 wpsielen=0;
		u8 *wpsie=NULL;	
		
		wpsie = rtw_get_wps_ie(pnetwork->network.IEs+_FIXED_IE_LENGTH_, pnetwork->network.IELength-_FIXED_IE_LENGTH_, NULL, &wpsielen);
		
		if(wpsie && wpsielen>0)
			psr = rtw_get_wps_attr_content(wpsie,  wpsielen, WPS_ATTR_SELECTED_REGISTRAR, (u8*)(&sr), NULL);
		
		if (sr != 0)
		{			
			if(request->n_ssids == 1 && request->n_channels == 1) // it means under processing WPS
			{
				DBG_8192C("ssid=%s, len=%d\n", pssid->Ssid, pssid->SsidLength);

				if (ssids[0].ssid_len == 0) {
				}
				else if(pssid->SsidLength == ssids[0].ssid_len &&
					_rtw_memcmp(pssid->Ssid, ssids[0].ssid, ssids[0].ssid_len))
				{
					DBG_871X("%s, got sr and ssid match!\n", __func__);
				}
				else
				{
					if(psr !=NULL)
						*psr = 0; //clear sr
				
#if 0			
					WLAN_BSSID_EX  *pselect_network = &pnetwork->network;
					struct cfg80211_bss *pselect_bss = NULL;
					struct ieee80211_channel *notify_channel = NULL;
					u32 freq;

					DBG_871X("%s, got sr, but ssid mismatch, to remove this bss\n", __func__);

					if (pselect_network->Configuration.DSConfig <= RTW_CH_MAX_2G_CHANNEL)
						freq = rtw_ieee80211_channel_to_frequency(pselect_network->Configuration.DSConfig, IEEE80211_BAND_2GHZ);
					else
						freq = rtw_ieee80211_channel_to_frequency(pselect_network->Configuration.DSConfig, IEEE80211_BAND_5GHZ);

					notify_channel = ieee80211_get_channel(wiphy, freq);
					pselect_bss = cfg80211_get_bss(wiphy, NULL/*notify_channel*/,
								pselect_network->MacAddress, pselect_network->Ssid.Ssid,
								pselect_network->Ssid.SsidLength, 0/*WLAN_CAPABILITY_ESS*/, 
								0/*WLAN_CAPABILITY_ESS*/);

					if(pselect_bss)
					{
						DBG_871X("%s, got bss for cfg80211 for unlinking bss\n", __func__);

						cfg80211_unlink_bss(wiphy, pselect_bss);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0)
						cfg80211_put_bss(wiphy, pselect_bss);
#else
						cfg80211_put_bss(pselect_bss);
#endif
						
					}
					
					goto exit;
#endif 					
				}			
			}			
		}		
	}
	//_exit_critical_bh(&pwdev_priv->scan_req_lock, &irqL);
	

	channel = pnetwork->network.Configuration.DSConfig;
	if (channel <= RTW_CH_MAX_2G_CHANNEL)
		freq = rtw_ieee80211_channel_to_frequency(channel, IEEE80211_BAND_2GHZ);
	else
		freq = rtw_ieee80211_channel_to_frequency(channel, IEEE80211_BAND_5GHZ);
	
	notify_channel = ieee80211_get_channel(wiphy, freq);

	//rtw_get_timestampe_from_ie()
	notify_timestamp = jiffies_to_msecs(jiffies)*1000; /* uSec */

	notify_interval = le16_to_cpu(*(u16*)rtw_get_beacon_interval_from_ie(pnetwork->network.IEs));
	notify_capability = le16_to_cpu(*(u16*)rtw_get_capability_from_ie(pnetwork->network.IEs));		

	
	notify_ie = pnetwork->network.IEs+_FIXED_IE_LENGTH_;
	notify_ielen = pnetwork->network.IELength-_FIXED_IE_LENGTH_;

	//We've set wiphy's signal_type as CFG80211_SIGNAL_TYPE_MBM: signal strength in mBm (100*dBm)
	if ( check_fwstate(pmlmepriv, _FW_LINKED)== _TRUE &&
		is_same_network(&pmlmepriv->cur_network.network, &pnetwork->network, 0)) {
		notify_signal = 100*translate_percentage_to_dbm(padapter->recvpriv.signal_strength);//dbm
	} else {
		notify_signal = 100*translate_percentage_to_dbm(pnetwork->network.PhyInfo.SignalStrength);//dbm
	}
		
	#if 0
	DBG_8192C("bssid: "MAC_FMT"\n", MAC_ARG(pnetwork->network.MacAddress));
	DBG_8192C("Channel: %d(%d)\n", channel, freq);
	DBG_8192C("Capability: %X\n", notify_capability);
	DBG_8192C("Beacon interval: %d\n", notify_interval);
	DBG_8192C("Signal: %d\n", notify_signal);
	DBG_8192C("notify_timestamp: %#018llx\n", notify_timestamp);
	#endif

	pbuf = buf;
	
	pwlanhdr = (struct rtw_ieee80211_hdr *)pbuf;	
	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;	

	SetSeqNum(pwlanhdr, 0/*pmlmeext->mgnt_seq*/);
	//pmlmeext->mgnt_seq++;

	if (pnetwork->network.Reserved[0] == 1) { // WIFI_BEACON
		_rtw_memcpy(pwlanhdr->addr1, bc_addr, ETH_ALEN);		
		SetFrameSubType(pbuf, WIFI_BEACON);
	} else {
		_rtw_memcpy(pwlanhdr->addr1, myid(&(padapter->eeprompriv)), ETH_ALEN);
		SetFrameSubType(pbuf, WIFI_PROBERSP);
	}

	_rtw_memcpy(pwlanhdr->addr2, pnetwork->network.MacAddress, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, pnetwork->network.MacAddress, ETH_ALEN);


	pbuf += sizeof(struct rtw_ieee80211_hdr_3addr);	
	len = sizeof (struct rtw_ieee80211_hdr_3addr);

	_rtw_memcpy(pbuf, pnetwork->network.IEs, pnetwork->network.IELength);
	len += pnetwork->network.IELength;

	//#ifdef CONFIG_P2P
	//if(rtw_get_p2p_ie(pnetwork->network.IEs+12, pnetwork->network.IELength-12, NULL, NULL))
	//{
	//	DBG_8192C("%s, got p2p_ie\n", __func__);
	//}
	//#endif
	

#if 1	
	bss = cfg80211_inform_bss_frame(wiphy, notify_channel, (struct ieee80211_mgmt *)buf,
		len, notify_signal, GFP_ATOMIC);
#else			 
			
	bss = cfg80211_inform_bss(wiphy, notify_channel, (const u8 *)pnetwork->network.MacAddress,
                notify_timestamp, notify_capability, notify_interval, notify_ie,
                notify_ielen, notify_signal, GFP_ATOMIC/*GFP_KERNEL*/);
#endif

	if (unlikely(!bss)) {
		DBG_8192C(FUNC_ADPT_FMT" bss NULL\n", FUNC_ADPT_ARG(padapter));
		goto exit;
	}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,38))
#ifndef COMPAT_KERNEL_RELEASE
	//patch for cfg80211, update beacon ies to information_elements
	if (pnetwork->network.Reserved[0] == 1) { // WIFI_BEACON
	
		 if(bss->len_information_elements != bss->len_beacon_ies)
		 {
			bss->information_elements = bss->beacon_ies;			
			bss->len_information_elements =  bss->len_beacon_ies;
		 }		
	}
#endif //COMPAT_KERNEL_RELEASE
#endif //LINUX_VERSION_CODE < KERNEL_VERSION(2,6,38)

/*	
	{
		if( bss->information_elements == bss->proberesp_ies) 
		{
			if( bss->len_information_elements !=  bss->len_proberesp_ies)
			{
				DBG_8192C("error!, len_information_elements !=  bss->len_proberesp_ies\n");
			}
							
		}
		else if(bss->len_information_elements <  bss->len_beacon_ies)
		{
			bss->information_elements = bss->beacon_ies;			
			bss->len_information_elements =  bss->len_beacon_ies;
		}
	}
*/
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0)
	cfg80211_put_bss(wiphy, bss);
#else
	cfg80211_put_bss(bss);
#endif

exit:	
	return bss;
	
}

/*
	Check the given bss is valid by kernel API cfg80211_get_bss()
	@padapter : the given adapter
	
	return _TRUE if bss is valid,  _FALSE for not found.
*/
int rtw_cfg80211_check_bss(_adapter *padapter)
{
	WLAN_BSSID_EX  *pnetwork = &(padapter->mlmeextpriv.mlmext_info.network);
	struct cfg80211_bss *bss = NULL;
	struct ieee80211_channel *notify_channel = NULL;
	u32 freq;

	if (!(pnetwork) || !(padapter->rtw_wdev))
		return _FALSE;

	if (pnetwork->Configuration.DSConfig <= RTW_CH_MAX_2G_CHANNEL)
		freq = rtw_ieee80211_channel_to_frequency(pnetwork->Configuration.DSConfig, IEEE80211_BAND_2GHZ);
	else
		freq = rtw_ieee80211_channel_to_frequency(pnetwork->Configuration.DSConfig, IEEE80211_BAND_5GHZ);

	notify_channel = ieee80211_get_channel(padapter->rtw_wdev->wiphy, freq);
	bss = cfg80211_get_bss(padapter->rtw_wdev->wiphy, notify_channel,
			pnetwork->MacAddress, pnetwork->Ssid.Ssid,
			pnetwork->Ssid.SsidLength,
			WLAN_CAPABILITY_ESS, WLAN_CAPABILITY_ESS);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0)
	cfg80211_put_bss(padapter->rtw_wdev->wiphy, bss);
#else
	cfg80211_put_bss(bss);
#endif

	return 	(bss!=NULL);
}

void rtw_cfg80211_ibss_indicate_connect(_adapter *padapter)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct wlan_network  *cur_network = &(pmlmepriv->cur_network);
	struct wireless_dev *pwdev = padapter->rtw_wdev;
	struct cfg80211_bss *bss = NULL;

	DBG_871X(FUNC_ADPT_FMT"\n", FUNC_ADPT_ARG(padapter));
	if (pwdev->iftype != NL80211_IFTYPE_ADHOC) 
	{
		return;
	}

	if (!rtw_cfg80211_check_bss(padapter)) {
		WLAN_BSSID_EX  *pnetwork = &(padapter->mlmeextpriv.mlmext_info.network);
		struct wlan_network *scanned = pmlmepriv->cur_network_scanned;

		if(check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE)==_TRUE)
		{
			
			_rtw_memcpy(&cur_network->network, pnetwork, sizeof(WLAN_BSSID_EX));
			if(cur_network)
			{
				if (!rtw_cfg80211_inform_bss(padapter,cur_network))
					DBG_871X(FUNC_ADPT_FMT" inform fail !!\n", FUNC_ADPT_ARG(padapter));
				else
					DBG_871X(FUNC_ADPT_FMT" inform success !!\n", FUNC_ADPT_ARG(padapter));
			}
			else
			{
				DBG_871X("cur_network is not exist!!!\n");
				return ;
			}
		}
		else
		{
			if(scanned == NULL)
				rtw_warn_on(1);

			if (_rtw_memcmp(&(scanned->network.Ssid), &(pnetwork->Ssid), sizeof(NDIS_802_11_SSID)) == _TRUE
				&& _rtw_memcmp(scanned->network.MacAddress, pnetwork->MacAddress, sizeof(NDIS_802_11_MAC_ADDRESS)) == _TRUE
			) {
				if (!rtw_cfg80211_inform_bss(padapter,scanned)) {
					DBG_871X(FUNC_ADPT_FMT" inform fail !!\n", FUNC_ADPT_ARG(padapter));
				} else {
					//DBG_871X(FUNC_ADPT_FMT" inform success !!\n", FUNC_ADPT_ARG(padapter));
				}
			} else {
				DBG_871X("scanned & pnetwork compare fail\n");
				rtw_warn_on(1);
			}
		}

		if (!rtw_cfg80211_check_bss(padapter))
			DBG_871X_LEVEL(_drv_always_, FUNC_ADPT_FMT" BSS not found !!\n", FUNC_ADPT_ARG(padapter));
	}
	//notify cfg80211 that device joined an IBSS
	cfg80211_ibss_joined(padapter->pnetdev, cur_network->network.MacAddress, GFP_ATOMIC);
}

void rtw_cfg80211_indicate_connect(_adapter *padapter)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct wlan_network  *cur_network = &(pmlmepriv->cur_network);
	struct wireless_dev *pwdev = padapter->rtw_wdev;
#ifdef CONFIG_P2P
	struct wifidirect_info *pwdinfo= &(padapter->wdinfo);
#endif
	struct cfg80211_bss *bss = NULL;

	DBG_871X(FUNC_ADPT_FMT"\n", FUNC_ADPT_ARG(padapter));
	if (pwdev->iftype != NL80211_IFTYPE_STATION
		#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)) || defined(COMPAT_KERNEL_RELEASE)
		&& pwdev->iftype != NL80211_IFTYPE_P2P_CLIENT
		#endif
	) {
		return;
	}

	if(check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE)
		return;

#ifdef CONFIG_P2P
	if(pwdinfo->driver_interface == DRIVER_CFG80211 )
	{
		if(!rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE))
		{
			rtw_p2p_set_pre_state(pwdinfo, rtw_p2p_state(pwdinfo));
			rtw_p2p_set_role(pwdinfo, P2P_ROLE_CLIENT);
			rtw_p2p_set_state(pwdinfo, P2P_STATE_GONEGO_OK);
			DBG_8192C("%s, role=%d, p2p_state=%d, pre_p2p_state=%d\n", __func__, rtw_p2p_role(pwdinfo), rtw_p2p_state(pwdinfo), rtw_p2p_pre_state(pwdinfo));
		}
	}
#endif //CONFIG_P2P

	{
		WLAN_BSSID_EX  *pnetwork = &(padapter->mlmeextpriv.mlmext_info.network);
		struct wlan_network *scanned = pmlmepriv->cur_network_scanned;

		//DBG_871X(FUNC_ADPT_FMT" BSS not found\n", FUNC_ADPT_ARG(padapter));

		if(scanned == NULL) {
			rtw_warn_on(1);
			goto check_bss;
		}

		if (_rtw_memcmp(scanned->network.MacAddress, pnetwork->MacAddress, sizeof(NDIS_802_11_MAC_ADDRESS)) == _TRUE
			&& _rtw_memcmp(&(scanned->network.Ssid), &(pnetwork->Ssid), sizeof(NDIS_802_11_SSID)) == _TRUE
		) {
			if (!rtw_cfg80211_inform_bss(padapter,scanned)) {
				DBG_871X(FUNC_ADPT_FMT" inform fail !!\n", FUNC_ADPT_ARG(padapter));
			} else {
				//DBG_871X(FUNC_ADPT_FMT" inform success !!\n", FUNC_ADPT_ARG(padapter));
			}
		} else {
			DBG_871X("scanned: %s("MAC_FMT"), cur: %s("MAC_FMT")\n",
				scanned->network.Ssid.Ssid, MAC_ARG(scanned->network.MacAddress),
				pnetwork->Ssid.Ssid, MAC_ARG(pnetwork->MacAddress)
			);
			rtw_warn_on(1);
		}
	}

check_bss:
	if (!rtw_cfg80211_check_bss(padapter))
		DBG_871X_LEVEL(_drv_always_, FUNC_ADPT_FMT" BSS not found !!\n", FUNC_ADPT_ARG(padapter));

	if (rtw_to_roam(padapter) > 0) {
		#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 39) || defined(COMPAT_KERNEL_RELEASE)
		struct wiphy *wiphy = pwdev->wiphy;
		struct ieee80211_channel *notify_channel;
		u32 freq;
		u16 channel = cur_network->network.Configuration.DSConfig;

		if (channel <= RTW_CH_MAX_2G_CHANNEL)
			freq = rtw_ieee80211_channel_to_frequency(channel, IEEE80211_BAND_2GHZ);
		else
			freq = rtw_ieee80211_channel_to_frequency(channel, IEEE80211_BAND_5GHZ);

		notify_channel = ieee80211_get_channel(wiphy, freq);
		#endif

		DBG_871X(FUNC_ADPT_FMT" call cfg80211_roamed\n", FUNC_ADPT_ARG(padapter));
		cfg80211_roamed(padapter->pnetdev
			#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 39) || defined(COMPAT_KERNEL_RELEASE)
			, notify_channel
			#endif
			, cur_network->network.MacAddress
			, pmlmepriv->assoc_req+sizeof(struct rtw_ieee80211_hdr_3addr)+2
			, pmlmepriv->assoc_req_len-sizeof(struct rtw_ieee80211_hdr_3addr)-2
			, pmlmepriv->assoc_rsp+sizeof(struct rtw_ieee80211_hdr_3addr)+6
			, pmlmepriv->assoc_rsp_len-sizeof(struct rtw_ieee80211_hdr_3addr)-6
			, GFP_ATOMIC);
	}
	else
	{
		DBG_8192C("pwdev->sme_state(b)=%d\n", pwdev->sme_state);
		cfg80211_connect_result(padapter->pnetdev, cur_network->network.MacAddress
			, pmlmepriv->assoc_req+sizeof(struct rtw_ieee80211_hdr_3addr)+2
			, pmlmepriv->assoc_req_len-sizeof(struct rtw_ieee80211_hdr_3addr)-2
			, pmlmepriv->assoc_rsp+sizeof(struct rtw_ieee80211_hdr_3addr)+6
			, pmlmepriv->assoc_rsp_len-sizeof(struct rtw_ieee80211_hdr_3addr)-6
			, WLAN_STATUS_SUCCESS, GFP_ATOMIC);
		DBG_8192C("pwdev->sme_state(a)=%d\n", pwdev->sme_state);
	}
}

void rtw_cfg80211_indicate_disconnect(_adapter *padapter)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct wireless_dev *pwdev = padapter->rtw_wdev;
#ifdef CONFIG_P2P
	struct wifidirect_info *pwdinfo= &(padapter->wdinfo);
#endif

	DBG_871X(FUNC_ADPT_FMT"\n", FUNC_ADPT_ARG(padapter));

	if (pwdev->iftype != NL80211_IFTYPE_STATION 
		#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)) || defined(COMPAT_KERNEL_RELEASE)
		&& pwdev->iftype != NL80211_IFTYPE_P2P_CLIENT
		#endif
	) {
		return;
	}

	if(check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE)
		return;

#ifdef CONFIG_P2P	
	if( pwdinfo->driver_interface == DRIVER_CFG80211 )
	{
		if(!rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE))
		{
			rtw_p2p_set_state(pwdinfo, rtw_p2p_pre_state(pwdinfo));
			rtw_p2p_set_role(pwdinfo, P2P_ROLE_DEVICE);

			DBG_8192C("%s, role=%d, p2p_state=%d, pre_p2p_state=%d\n", __func__, rtw_p2p_role(pwdinfo), rtw_p2p_state(pwdinfo), rtw_p2p_pre_state(pwdinfo));
		}
	}
#endif //CONFIG_P2P

	if (!padapter->mlmepriv.not_indic_disco) {
		DBG_8192C("pwdev->sme_state(b)=%d\n", pwdev->sme_state);

		if(pwdev->sme_state==CFG80211_SME_CONNECTING)
			cfg80211_connect_result(padapter->pnetdev, NULL, NULL, 0, NULL, 0, 
				WLAN_STATUS_UNSPECIFIED_FAILURE, GFP_ATOMIC/*GFP_KERNEL*/);
		else if(pwdev->sme_state==CFG80211_SME_CONNECTED)
			cfg80211_disconnected(padapter->pnetdev, 0, NULL, 0, GFP_ATOMIC);
		//else
			//DBG_8192C("pwdev->sme_state=%d\n", pwdev->sme_state);

		DBG_8192C("pwdev->sme_state(a)=%d\n", pwdev->sme_state);
	}
}
 	

#ifdef CONFIG_AP_MODE
static int rtw_cfg80211_ap_set_encryption(struct net_device *dev, struct ieee_param *param, u32 param_len)
{
	int ret = 0;
	u32 wep_key_idx, wep_key_len,wep_total_len;
	struct sta_info *psta = NULL, *pbcmc_sta = NULL;	
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct mlme_priv 	*pmlmepriv = &padapter->mlmepriv;
	struct security_priv* psecuritypriv=&(padapter->securitypriv);
	struct sta_priv *pstapriv = &padapter->stapriv;

	DBG_8192C("%s\n", __FUNCTION__);

	param->u.crypt.err = 0;
	param->u.crypt.alg[IEEE_CRYPT_ALG_NAME_LEN - 1] = '\0';

	//sizeof(struct ieee_param) = 64 bytes;
	//if (param_len !=  (u32) ((u8 *) param->u.crypt.key - (u8 *) param) + param->u.crypt.key_len)
	if (param_len !=  sizeof(struct ieee_param) + param->u.crypt.key_len)
	{
		ret =  -EINVAL;
		goto exit;
	}

	if (param->sta_addr[0] == 0xff && param->sta_addr[1] == 0xff &&
	    param->sta_addr[2] == 0xff && param->sta_addr[3] == 0xff &&
	    param->sta_addr[4] == 0xff && param->sta_addr[5] == 0xff) 
	{
		if (param->u.crypt.idx >= WEP_KEYS)
		{
			ret = -EINVAL;
			goto exit;
		}	
	}
	else 
	{		
		psta = rtw_get_stainfo(pstapriv, param->sta_addr);
		if(!psta)
		{
			//ret = -EINVAL;
			DBG_8192C("rtw_set_encryption(), sta has already been removed or never been added\n");
			goto exit;
		}			
	}

	if (strcmp(param->u.crypt.alg, "none") == 0 && (psta==NULL))
	{
		//todo:clear default encryption keys

		DBG_8192C("clear default encryption keys, keyid=%d\n", param->u.crypt.idx);
		
		goto exit;
	}


	if (strcmp(param->u.crypt.alg, "WEP") == 0 && (psta==NULL))
	{		
		DBG_8192C("r871x_set_encryption, crypt.alg = WEP\n");
		
		wep_key_idx = param->u.crypt.idx;
		wep_key_len = param->u.crypt.key_len;
					
		DBG_8192C("r871x_set_encryption, wep_key_idx=%d, len=%d\n", wep_key_idx, wep_key_len);

		if((wep_key_idx >= WEP_KEYS) || (wep_key_len<=0))
		{
			ret = -EINVAL;
			goto exit;
		}
			
		if (wep_key_len > 0) 
		{			
		 	wep_key_len = wep_key_len <= 5 ? 5 : 13;
		}
		
		if (psecuritypriv->bWepDefaultKeyIdxSet == 0)
		{
			//wep default key has not been set, so use this key index as default key.

			psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_Auto;
			psecuritypriv->ndisencryptstatus = Ndis802_11Encryption1Enabled;
			psecuritypriv->dot11PrivacyAlgrthm=_WEP40_;
			psecuritypriv->dot118021XGrpPrivacy=_WEP40_;
			
			if(wep_key_len == 13)
			{
				psecuritypriv->dot11PrivacyAlgrthm=_WEP104_;
				psecuritypriv->dot118021XGrpPrivacy=_WEP104_;
			}

			psecuritypriv->dot11PrivacyKeyIndex = wep_key_idx;
		}
			
		_rtw_memcpy(&(psecuritypriv->dot11DefKey[wep_key_idx].skey[0]), param->u.crypt.key, wep_key_len);
					
		psecuritypriv->dot11DefKeylen[wep_key_idx] = wep_key_len;

		rtw_ap_set_wep_key(padapter, param->u.crypt.key, wep_key_len, wep_key_idx, 1);

		goto exit;
		
	}

	
	if(!psta && check_fwstate(pmlmepriv, WIFI_AP_STATE)) // //group key
	{
		if(param->u.crypt.set_tx == 0) //group key
		{
			if(strcmp(param->u.crypt.alg, "WEP") == 0)
			{
				DBG_8192C("%s, set group_key, WEP\n", __FUNCTION__);
				
				_rtw_memcpy(psecuritypriv->dot118021XGrpKey[param->u.crypt.idx].skey,  param->u.crypt.key, (param->u.crypt.key_len>16 ?16:param->u.crypt.key_len));
					
				psecuritypriv->dot118021XGrpPrivacy = _WEP40_;
				if(param->u.crypt.key_len==13)
				{						
						psecuritypriv->dot118021XGrpPrivacy = _WEP104_;
				}
				
			}
			else if(strcmp(param->u.crypt.alg, "TKIP") == 0)
			{						
				DBG_8192C("%s, set group_key, TKIP\n", __FUNCTION__);
				
				psecuritypriv->dot118021XGrpPrivacy = _TKIP_;

				_rtw_memcpy(psecuritypriv->dot118021XGrpKey[param->u.crypt.idx].skey,  param->u.crypt.key, (param->u.crypt.key_len>16 ?16:param->u.crypt.key_len));
				
				//DEBUG_ERR("set key length :param->u.crypt.key_len=%d\n", param->u.crypt.key_len);
				//set mic key
				_rtw_memcpy(psecuritypriv->dot118021XGrptxmickey[param->u.crypt.idx].skey, &(param->u.crypt.key[16]), 8);
				_rtw_memcpy(psecuritypriv->dot118021XGrprxmickey[param->u.crypt.idx].skey, &(param->u.crypt.key[24]), 8);

				psecuritypriv->busetkipkey = _TRUE;
											
			}
			else if(strcmp(param->u.crypt.alg, "CCMP") == 0)
			{
				DBG_8192C("%s, set group_key, CCMP\n", __FUNCTION__);
			
				psecuritypriv->dot118021XGrpPrivacy = _AES_;

				_rtw_memcpy(psecuritypriv->dot118021XGrpKey[param->u.crypt.idx].skey,  param->u.crypt.key, (param->u.crypt.key_len>16 ?16:param->u.crypt.key_len));
			}
			else
			{
				DBG_8192C("%s, set group_key, none\n", __FUNCTION__);
				
				psecuritypriv->dot118021XGrpPrivacy = _NO_PRIVACY_;
			}

			psecuritypriv->dot118021XGrpKeyid = param->u.crypt.idx;

			psecuritypriv->binstallGrpkey = _TRUE;

			psecuritypriv->dot11PrivacyAlgrthm = psecuritypriv->dot118021XGrpPrivacy;//!!!
								
			rtw_ap_set_group_key(padapter, param->u.crypt.key, psecuritypriv->dot118021XGrpPrivacy, param->u.crypt.idx);
			
			pbcmc_sta=rtw_get_bcmc_stainfo(padapter);
			if(pbcmc_sta)
			{
				pbcmc_sta->ieee8021x_blocked = _FALSE;
				pbcmc_sta->dot118021XPrivacy= psecuritypriv->dot118021XGrpPrivacy;//rx will use bmc_sta's dot118021XPrivacy			
			}	
						
		}

		goto exit;
		
	}	

	if(psecuritypriv->dot11AuthAlgrthm == dot11AuthAlgrthm_8021X && psta) // psk/802_1x
	{
		if(check_fwstate(pmlmepriv, WIFI_AP_STATE))
		{
			if(param->u.crypt.set_tx ==1) //pairwise key
			{ 
				_rtw_memcpy(psta->dot118021x_UncstKey.skey,  param->u.crypt.key, (param->u.crypt.key_len>16 ?16:param->u.crypt.key_len));
				
				if(strcmp(param->u.crypt.alg, "WEP") == 0)
				{
					DBG_8192C("%s, set pairwise key, WEP\n", __FUNCTION__);
					
					psta->dot118021XPrivacy = _WEP40_;
					if(param->u.crypt.key_len==13)
					{						
						psta->dot118021XPrivacy = _WEP104_;
					}
				}
				else if(strcmp(param->u.crypt.alg, "TKIP") == 0)
				{						
					DBG_8192C("%s, set pairwise key, TKIP\n", __FUNCTION__);
					
					psta->dot118021XPrivacy = _TKIP_;
				
					//DEBUG_ERR("set key length :param->u.crypt.key_len=%d\n", param->u.crypt.key_len);
					//set mic key
					_rtw_memcpy(psta->dot11tkiptxmickey.skey, &(param->u.crypt.key[16]), 8);
					_rtw_memcpy(psta->dot11tkiprxmickey.skey, &(param->u.crypt.key[24]), 8);

					psecuritypriv->busetkipkey = _TRUE;
											
				}
				else if(strcmp(param->u.crypt.alg, "CCMP") == 0)
				{

					DBG_8192C("%s, set pairwise key, CCMP\n", __FUNCTION__);
					
					psta->dot118021XPrivacy = _AES_;
				}
				else
				{
					DBG_8192C("%s, set pairwise key, none\n", __FUNCTION__);
					
					psta->dot118021XPrivacy = _NO_PRIVACY_;
				}
						
				rtw_ap_set_pairwise_key(padapter, psta);
					
				psta->ieee8021x_blocked = _FALSE;
					
				psta->bpairwise_key_installed = _TRUE;
					
			}			
			else//group key???
			{ 
				if(strcmp(param->u.crypt.alg, "WEP") == 0)
				{
					_rtw_memcpy(psecuritypriv->dot118021XGrpKey[param->u.crypt.idx].skey,  param->u.crypt.key, (param->u.crypt.key_len>16 ?16:param->u.crypt.key_len));
					
					psecuritypriv->dot118021XGrpPrivacy = _WEP40_;
					if(param->u.crypt.key_len==13)
					{						
						psecuritypriv->dot118021XGrpPrivacy = _WEP104_;
					}
				}
				else if(strcmp(param->u.crypt.alg, "TKIP") == 0)
				{						
					psecuritypriv->dot118021XGrpPrivacy = _TKIP_;

					_rtw_memcpy(psecuritypriv->dot118021XGrpKey[param->u.crypt.idx].skey,  param->u.crypt.key, (param->u.crypt.key_len>16 ?16:param->u.crypt.key_len));
				
					//DEBUG_ERR("set key length :param->u.crypt.key_len=%d\n", param->u.crypt.key_len);
					//set mic key
					_rtw_memcpy(psecuritypriv->dot118021XGrptxmickey[param->u.crypt.idx].skey, &(param->u.crypt.key[16]), 8);
					_rtw_memcpy(psecuritypriv->dot118021XGrprxmickey[param->u.crypt.idx].skey, &(param->u.crypt.key[24]), 8);

					psecuritypriv->busetkipkey = _TRUE;
											
				}
				else if(strcmp(param->u.crypt.alg, "CCMP") == 0)
				{
					psecuritypriv->dot118021XGrpPrivacy = _AES_;

					_rtw_memcpy(psecuritypriv->dot118021XGrpKey[param->u.crypt.idx].skey,  param->u.crypt.key, (param->u.crypt.key_len>16 ?16:param->u.crypt.key_len));
				}
				else
				{
					psecuritypriv->dot118021XGrpPrivacy = _NO_PRIVACY_;
				}

				psecuritypriv->dot118021XGrpKeyid = param->u.crypt.idx;

				psecuritypriv->binstallGrpkey = _TRUE;	
								
				psecuritypriv->dot11PrivacyAlgrthm = psecuritypriv->dot118021XGrpPrivacy;//!!!
								
				rtw_ap_set_group_key(padapter, param->u.crypt.key, psecuritypriv->dot118021XGrpPrivacy, param->u.crypt.idx);
			
				pbcmc_sta=rtw_get_bcmc_stainfo(padapter);
				if(pbcmc_sta)
				{
					pbcmc_sta->ieee8021x_blocked = _FALSE;
					pbcmc_sta->dot118021XPrivacy= psecuritypriv->dot118021XGrpPrivacy;//rx will use bmc_sta's dot118021XPrivacy			
				}					

			}
			
		}
				
	}

exit:

	return ret;
	
}
#endif

static int rtw_cfg80211_set_encryption(struct net_device *dev, struct ieee_param *param, u32 param_len)
{
	int ret = 0;
	u32 wep_key_idx, wep_key_len,wep_total_len;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct mlme_priv 	*pmlmepriv = &padapter->mlmepriv;		
	struct security_priv *psecuritypriv = &padapter->securitypriv;
#ifdef CONFIG_P2P
	struct wifidirect_info* pwdinfo = &padapter->wdinfo;
#endif //CONFIG_P2P

_func_enter_;

	DBG_8192C("%s\n", __func__);

	param->u.crypt.err = 0;
	param->u.crypt.alg[IEEE_CRYPT_ALG_NAME_LEN - 1] = '\0';

	if (param_len < (u32) ((u8 *) param->u.crypt.key - (u8 *) param) + param->u.crypt.key_len)
	{
		ret =  -EINVAL;		
		goto exit;
	}

	if (param->sta_addr[0] == 0xff && param->sta_addr[1] == 0xff &&
	    param->sta_addr[2] == 0xff && param->sta_addr[3] == 0xff &&
	    param->sta_addr[4] == 0xff && param->sta_addr[5] == 0xff) 
	{
		if (param->u.crypt.idx >= WEP_KEYS
#ifdef CONFIG_IEEE80211W
			&& param->u.crypt.idx > BIP_MAX_KEYID
#endif //CONFIG_IEEE80211W
		)
		{
			ret = -EINVAL;
			goto exit;
		}
	} else {
#ifdef CONFIG_WAPI_SUPPORT
		if (strcmp(param->u.crypt.alg, "SMS4"))
#endif
		{
		ret = -EINVAL;
		goto exit;
	}
	}

	if (strcmp(param->u.crypt.alg, "WEP") == 0)
	{
		RT_TRACE(_module_rtl871x_ioctl_os_c,_drv_err_,("wpa_set_encryption, crypt.alg = WEP\n"));
		DBG_8192C("wpa_set_encryption, crypt.alg = WEP\n");

		wep_key_idx = param->u.crypt.idx;
		wep_key_len = param->u.crypt.key_len;

		if ((wep_key_idx > WEP_KEYS) || (wep_key_len <= 0))
		{
			ret = -EINVAL;
			goto exit;
		}

		if (psecuritypriv->bWepDefaultKeyIdxSet == 0)
		{
			//wep default key has not been set, so use this key index as default key.

			wep_key_len = wep_key_len <= 5 ? 5 : 13;

              	psecuritypriv->ndisencryptstatus = Ndis802_11Encryption1Enabled;
			psecuritypriv->dot11PrivacyAlgrthm = _WEP40_;
			psecuritypriv->dot118021XGrpPrivacy = _WEP40_;

			if(wep_key_len==13)
			{
				psecuritypriv->dot11PrivacyAlgrthm = _WEP104_;
				psecuritypriv->dot118021XGrpPrivacy = _WEP104_;
			}

			psecuritypriv->dot11PrivacyKeyIndex = wep_key_idx;
		}
			
		_rtw_memcpy(&(psecuritypriv->dot11DefKey[wep_key_idx].skey[0]), param->u.crypt.key, wep_key_len);
			
		psecuritypriv->dot11DefKeylen[wep_key_idx] = wep_key_len;
			
		rtw_set_key(padapter, psecuritypriv, wep_key_idx, 0, _TRUE);

		goto exit;		
	}

	if(padapter->securitypriv.dot11AuthAlgrthm == dot11AuthAlgrthm_8021X) // 802_1x
	{
		struct sta_info * psta,*pbcmc_sta;
		struct sta_priv * pstapriv = &padapter->stapriv;

		//DBG_8192C("%s, : dot11AuthAlgrthm == dot11AuthAlgrthm_8021X \n", __func__);

		if (check_fwstate(pmlmepriv, WIFI_STATION_STATE | WIFI_MP_STATE) == _TRUE) //sta mode
		{
			psta = rtw_get_stainfo(pstapriv, get_bssid(pmlmepriv));			
			if (psta == NULL) {
				//DEBUG_ERR( ("Set wpa_set_encryption: Obtain Sta_info fail \n"));
				DBG_8192C("%s, : Obtain Sta_info fail \n", __func__);
			}
			else
			{
				//Jeff: don't disable ieee8021x_blocked while clearing key
				if (strcmp(param->u.crypt.alg, "none") != 0) 
					psta->ieee8021x_blocked = _FALSE;

				
				if((padapter->securitypriv.ndisencryptstatus == Ndis802_11Encryption2Enabled)||
						(padapter->securitypriv.ndisencryptstatus ==  Ndis802_11Encryption3Enabled))
				{
					psta->dot118021XPrivacy = padapter->securitypriv.dot11PrivacyAlgrthm;
				}		

				if(param->u.crypt.set_tx ==1)//pairwise key
				{ 

					DBG_8192C("%s, : param->u.crypt.set_tx ==1 \n", __func__);
					
					_rtw_memcpy(psta->dot118021x_UncstKey.skey,  param->u.crypt.key, (param->u.crypt.key_len>16 ?16:param->u.crypt.key_len));
					
					if(strcmp(param->u.crypt.alg, "TKIP") == 0)//set mic key
					{						
						//DEBUG_ERR(("\nset key length :param->u.crypt.key_len=%d\n", param->u.crypt.key_len));
						_rtw_memcpy(psta->dot11tkiptxmickey.skey, &(param->u.crypt.key[16]), 8);
						_rtw_memcpy(psta->dot11tkiprxmickey.skey, &(param->u.crypt.key[24]), 8);

						padapter->securitypriv.busetkipkey=_FALSE;
						//_set_timer(&padapter->securitypriv.tkip_timer, 50);						
					}

					//DEBUG_ERR((" param->u.crypt.key_len=%d\n",param->u.crypt.key_len));
					DBG_871X(" ~~~~set sta key:unicastkey\n");
					
					rtw_setstakey_cmd(padapter, (unsigned char *)psta, _TRUE, _TRUE);
				}
				else//group key
				{
					if(strcmp(param->u.crypt.alg, "TKIP") == 0 || strcmp(param->u.crypt.alg, "CCMP") == 0)
					{
						_rtw_memcpy(padapter->securitypriv.dot118021XGrpKey[param->u.crypt.idx].skey,  param->u.crypt.key,(param->u.crypt.key_len>16 ?16:param->u.crypt.key_len));
						_rtw_memcpy(padapter->securitypriv.dot118021XGrptxmickey[param->u.crypt.idx].skey,&(param->u.crypt.key[16]),8);
						_rtw_memcpy(padapter->securitypriv.dot118021XGrprxmickey[param->u.crypt.idx].skey,&(param->u.crypt.key[24]),8);
	                                        padapter->securitypriv.binstallGrpkey = _TRUE;	
						//DEBUG_ERR((" param->u.crypt.key_len=%d\n", param->u.crypt.key_len));
						DBG_871X(" ~~~~set sta key:groupkey\n");
	
						padapter->securitypriv.dot118021XGrpKeyid = param->u.crypt.idx;
#ifdef CONFIG_CONCURRENT_MODE 
						if (padapter->adapter_type == PRIMARY_ADAPTER)
							rtw_set_key(padapter,&padapter->securitypriv,param->u.crypt.idx, 1, _TRUE);
						else
							DBG_871X_LEVEL(_drv_always_, "second interface do not set cam.\n");
#else
						rtw_set_key(padapter,&padapter->securitypriv,param->u.crypt.idx, 1, _TRUE);
#endif
					}
#ifdef CONFIG_IEEE80211W
					else if(strcmp(param->u.crypt.alg, "BIP") == 0)
					{
						int no;
						//DBG_871X("BIP key_len=%d , index=%d @@@@@@@@@@@@@@@@@@\n", param->u.crypt.key_len, param->u.crypt.idx);
						//save the IGTK key, length 16 bytes
						_rtw_memcpy(padapter->securitypriv.dot11wBIPKey[param->u.crypt.idx].skey,  param->u.crypt.key,(param->u.crypt.key_len>16 ?16:param->u.crypt.key_len));
						/*DBG_871X("IGTK key below:\n");
						for(no=0;no<16;no++)
							printk(" %02x ", padapter->securitypriv.dot11wBIPKey[param->u.crypt.idx].skey[no]);
						DBG_871X("\n");*/
						padapter->securitypriv.dot11wBIPKeyid = param->u.crypt.idx;
						padapter->securitypriv.binstallBIPkey = _TRUE;
						DBG_871X(" ~~~~set sta key:IGKT\n");
					}
#endif //CONFIG_IEEE80211W
					
#ifdef CONFIG_P2P
					if(pwdinfo->driver_interface == DRIVER_CFG80211 )
					{
						if(rtw_p2p_chk_state(pwdinfo, P2P_STATE_PROVISIONING_ING))
						{
							rtw_p2p_set_state(pwdinfo, P2P_STATE_PROVISIONING_DONE);
						}
					}
#endif //CONFIG_P2P
					
				}						
			}

			pbcmc_sta=rtw_get_bcmc_stainfo(padapter);
			if(pbcmc_sta==NULL)
			{
				//DEBUG_ERR( ("Set OID_802_11_ADD_KEY: bcmc stainfo is null \n"));
			}
			else
			{
				//Jeff: don't disable ieee8021x_blocked while clearing key
				if (strcmp(param->u.crypt.alg, "none") != 0) 
					pbcmc_sta->ieee8021x_blocked = _FALSE;
				
				if((padapter->securitypriv.ndisencryptstatus == Ndis802_11Encryption2Enabled)||
						(padapter->securitypriv.ndisencryptstatus ==  Ndis802_11Encryption3Enabled))
				{							
					pbcmc_sta->dot118021XPrivacy = padapter->securitypriv.dot11PrivacyAlgrthm;
				}					
			}				
		}
		else if(check_fwstate(pmlmepriv, WIFI_ADHOC_STATE)) //adhoc mode
		{		
		}			
	}

#ifdef CONFIG_WAPI_SUPPORT
	if (strcmp(param->u.crypt.alg, "SMS4") == 0)
	{
		PRT_WAPI_T			pWapiInfo = &padapter->wapiInfo;
		PRT_WAPI_STA_INFO	pWapiSta;
		u8					WapiASUEPNInitialValueSrc[16] = {0x36,0x5C,0x36,0x5C,0x36,0x5C,0x36,0x5C,0x36,0x5C,0x36,0x5C,0x36,0x5C,0x36,0x5C} ;
		u8					WapiAEPNInitialValueSrc[16] = {0x37,0x5C,0x36,0x5C,0x36,0x5C,0x36,0x5C,0x36,0x5C,0x36,0x5C,0x36,0x5C,0x36,0x5C} ;
		u8 					WapiAEMultiCastPNInitialValueSrc[16] = {0x36,0x5C,0x36,0x5C,0x36,0x5C,0x36,0x5C,0x36,0x5C,0x36,0x5C,0x36,0x5C,0x36,0x5C} ;

		if(param->u.crypt.set_tx == 1)
		{
			list_for_each_entry(pWapiSta, &pWapiInfo->wapiSTAUsedList, list) {
				if(_rtw_memcmp(pWapiSta->PeerMacAddr,param->sta_addr,6))
				{
					_rtw_memcpy(pWapiSta->lastTxUnicastPN,WapiASUEPNInitialValueSrc,16);

					pWapiSta->wapiUsk.bSet = true;
					_rtw_memcpy(pWapiSta->wapiUsk.dataKey,param->u.crypt.key,16);
					_rtw_memcpy(pWapiSta->wapiUsk.micKey,param->u.crypt.key+16,16);
					pWapiSta->wapiUsk.keyId = param->u.crypt.idx ;
					pWapiSta->wapiUsk.bTxEnable = true;

					_rtw_memcpy(pWapiSta->lastRxUnicastPNBEQueue,WapiAEPNInitialValueSrc,16);
					_rtw_memcpy(pWapiSta->lastRxUnicastPNBKQueue,WapiAEPNInitialValueSrc,16);
					_rtw_memcpy(pWapiSta->lastRxUnicastPNVIQueue,WapiAEPNInitialValueSrc,16);
					_rtw_memcpy(pWapiSta->lastRxUnicastPNVOQueue,WapiAEPNInitialValueSrc,16);
					_rtw_memcpy(pWapiSta->lastRxUnicastPN,WapiAEPNInitialValueSrc,16);
					pWapiSta->wapiUskUpdate.bTxEnable = false;
					pWapiSta->wapiUskUpdate.bSet = false;

					if (psecuritypriv->sw_encrypt== false || psecuritypriv->sw_decrypt == false)
					{
						//set unicast key for ASUE
						rtw_wapi_set_key(padapter, &pWapiSta->wapiUsk, pWapiSta, false, false);
					}
				}
			}
		}
		else
		{
			list_for_each_entry(pWapiSta, &pWapiInfo->wapiSTAUsedList, list) {
				if(_rtw_memcmp(pWapiSta->PeerMacAddr,get_bssid(pmlmepriv),6))
				{
					pWapiSta->wapiMsk.bSet = true;
					_rtw_memcpy(pWapiSta->wapiMsk.dataKey,param->u.crypt.key,16);
					_rtw_memcpy(pWapiSta->wapiMsk.micKey,param->u.crypt.key+16,16);
					pWapiSta->wapiMsk.keyId = param->u.crypt.idx ;
					pWapiSta->wapiMsk.bTxEnable = false;
					if(!pWapiSta->bSetkeyOk)
						pWapiSta->bSetkeyOk = true;
					pWapiSta->bAuthenticateInProgress = false;

					_rtw_memcpy(pWapiSta->lastRxMulticastPN, WapiAEMultiCastPNInitialValueSrc, 16);

					if (psecuritypriv->sw_decrypt == false)
					{
						//set rx broadcast key for ASUE
						rtw_wapi_set_key(padapter, &pWapiSta->wapiMsk, pWapiSta, true, false);
					}
				}

			}
		}
	}
#endif


exit:

	DBG_8192C("%s, ret=%d\n", __func__, ret);
	
	_func_exit_;
	
	return ret;	
}

static int cfg80211_rtw_add_key(struct wiphy *wiphy, struct net_device *ndev,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)) || defined(COMPAT_KERNEL_RELEASE)
				u8 key_index, bool pairwise, const u8 *mac_addr,
#else	// (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
				u8 key_index, const u8 *mac_addr,
#endif	// (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
				struct key_params *params)
{
	char *alg_name;
	u32 param_len;
	struct ieee_param *param = NULL;	
	int ret=0;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(ndev);
	struct wireless_dev *rtw_wdev = padapter->rtw_wdev;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	
	DBG_871X(FUNC_NDEV_FMT" adding key for %pM\n", FUNC_NDEV_ARG(ndev), mac_addr);
	DBG_871X("cipher=0x%x\n", params->cipher);
	DBG_871X("key_len=0x%x\n", params->key_len);
	DBG_871X("seq_len=0x%x\n", params->seq_len);
	DBG_871X("key_index=%d\n", key_index);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)) || defined(COMPAT_KERNEL_RELEASE)
	DBG_871X("pairwise=%d\n", pairwise);
#endif	// (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))

	param_len = sizeof(struct ieee_param) + params->key_len;
	param = (struct ieee_param *)rtw_malloc(param_len);
	if (param == NULL)
		return -1;
	
	_rtw_memset(param, 0, param_len);

	param->cmd = IEEE_CMD_SET_ENCRYPTION;
	_rtw_memset(param->sta_addr, 0xff, ETH_ALEN);

	switch (params->cipher) {
	case IW_AUTH_CIPHER_NONE:
		//todo: remove key 
		//remove = 1;	
		alg_name = "none";
		break;
	case WLAN_CIPHER_SUITE_WEP40:
	case WLAN_CIPHER_SUITE_WEP104:
		alg_name = "WEP";
		break;
	case WLAN_CIPHER_SUITE_TKIP:
		alg_name = "TKIP";
		break;
	case WLAN_CIPHER_SUITE_CCMP:
		alg_name = "CCMP";
		break;
#ifdef CONFIG_IEEE80211W
	case WLAN_CIPHER_SUITE_AES_CMAC:
		alg_name = "BIP";
		break;
#endif //CONFIG_IEEE80211W
#ifdef CONFIG_WAPI_SUPPORT
	case WLAN_CIPHER_SUITE_SMS4:
		alg_name= "SMS4";
		if(pairwise == NL80211_KEYTYPE_PAIRWISE) {
			if (key_index != 0 && key_index != 1) {
				ret = -ENOTSUPP;
				goto addkey_end;
			}
			_rtw_memcpy((void*)param->sta_addr, (void*)mac_addr, ETH_ALEN);
		} else {
			DBG_871X("mac_addr is null \n");
		}
		DBG_871X("rtw_wx_set_enc_ext: SMS4 case \n");
		break;
#endif

	default:	
		ret = -ENOTSUPP;
		goto addkey_end;
	}
	
	strncpy((char *)param->u.crypt.alg, alg_name, IEEE_CRYPT_ALG_NAME_LEN);
	

	if (!mac_addr || is_broadcast_ether_addr(mac_addr))
	{
		param->u.crypt.set_tx = 0; //for wpa/wpa2 group key
	} else {
		param->u.crypt.set_tx = 1; //for wpa/wpa2 pairwise key
	}
	
	
	//param->u.crypt.idx = key_index - 1;
	param->u.crypt.idx = key_index;
	
	if (params->seq_len && params->seq) 
	{	
		_rtw_memcpy(param->u.crypt.seq, params->seq, params->seq_len);
	}

	if(params->key_len && params->key)
	{
		param->u.crypt.key_len = params->key_len;		
		_rtw_memcpy(param->u.crypt.key, params->key, params->key_len);
	}	

	if(check_fwstate(pmlmepriv, WIFI_STATION_STATE) == _TRUE)
	{
		ret =  rtw_cfg80211_set_encryption(ndev, param, param_len);	
	}
	else if(check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE)
	{
#ifdef CONFIG_AP_MODE
		if(mac_addr)
			_rtw_memcpy(param->sta_addr, (void*)mac_addr, ETH_ALEN);
	
		ret = rtw_cfg80211_ap_set_encryption(ndev, param, param_len);
#endif
	}
        else if(check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == _TRUE
                || check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == _TRUE)
        {
                //DBG_8192C("@@@@@@@@@@ fw_state=0x%x, iftype=%d\n", pmlmepriv->fw_state, rtw_wdev->iftype);
                ret =  rtw_cfg80211_set_encryption(ndev, param, param_len);
        }
	else
	{
		DBG_8192C("error! fw_state=0x%x, iftype=%d\n", pmlmepriv->fw_state, rtw_wdev->iftype);
		
	}

addkey_end:
	if(param)
	{
		rtw_mfree((u8*)param, param_len);
	}

	return ret;

}

static int cfg80211_rtw_get_key(struct wiphy *wiphy, struct net_device *ndev,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)) || defined(COMPAT_KERNEL_RELEASE)
				u8 key_index, bool pairwise, const u8 *mac_addr,
#else	// (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
				u8 key_index, const u8 *mac_addr,
#endif	// (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
				void *cookie,
				void (*callback)(void *cookie,
						 struct key_params*))
{
#if 0
	struct iwm_priv *iwm = ndev_to_iwm(ndev);
	struct iwm_key *key = &iwm->keys[key_index];
	struct key_params params;

	IWM_DBG_WEXT(iwm, DBG, "Getting key %d\n", key_index);

	memset(&params, 0, sizeof(params));

	params.cipher = key->cipher;
	params.key_len = key->key_len;
	params.seq_len = key->seq_len;
	params.seq = key->seq;
	params.key = key->key;

	callback(cookie, &params);

	return key->key_len ? 0 : -ENOENT;
#endif
	DBG_871X(FUNC_NDEV_FMT"\n", FUNC_NDEV_ARG(ndev));
	return 0;
}

static int cfg80211_rtw_del_key(struct wiphy *wiphy, struct net_device *ndev,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)) || defined(COMPAT_KERNEL_RELEASE)
				u8 key_index, bool pairwise, const u8 *mac_addr)
#else	// (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
				u8 key_index, const u8 *mac_addr)
#endif	// (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
{
	_adapter *padapter = (_adapter *)rtw_netdev_priv(ndev);		
	struct security_priv *psecuritypriv = &padapter->securitypriv;

	DBG_871X(FUNC_NDEV_FMT" key_index=%d\n", FUNC_NDEV_ARG(ndev), key_index);

	if (key_index == psecuritypriv->dot11PrivacyKeyIndex)
	{
		//clear the flag of wep default key set.
		psecuritypriv->bWepDefaultKeyIdxSet = 0;
	}

	return 0;
}

static int cfg80211_rtw_set_default_key(struct wiphy *wiphy,
	struct net_device *ndev, u8 key_index
	#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,38)) || defined(COMPAT_KERNEL_RELEASE)
	, bool unicast, bool multicast
	#endif
	)
{
	_adapter *padapter = (_adapter *)rtw_netdev_priv(ndev);		
	struct security_priv *psecuritypriv = &padapter->securitypriv;

		DBG_871X(FUNC_NDEV_FMT" key_index=%d"
		#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,38)) || defined(COMPAT_KERNEL_RELEASE)
		", unicast=%d, multicast=%d"
		#endif
		".\n", FUNC_NDEV_ARG(ndev), key_index
		#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,38)) || defined(COMPAT_KERNEL_RELEASE)
		, unicast, multicast
		#endif
		);

	if ((key_index < WEP_KEYS) && ((psecuritypriv->dot11PrivacyAlgrthm == _WEP40_) || (psecuritypriv->dot11PrivacyAlgrthm == _WEP104_))) //set wep default key
	{
		psecuritypriv->ndisencryptstatus = Ndis802_11Encryption1Enabled;

		psecuritypriv->dot11PrivacyKeyIndex = key_index;

		psecuritypriv->dot11PrivacyAlgrthm = _WEP40_;
		psecuritypriv->dot118021XGrpPrivacy = _WEP40_;
		if (psecuritypriv->dot11DefKeylen[key_index] == 13)
		{
			psecuritypriv->dot11PrivacyAlgrthm = _WEP104_;
			psecuritypriv->dot118021XGrpPrivacy = _WEP104_;
		}

		psecuritypriv->bWepDefaultKeyIdxSet = 1; //set the flag to represent that wep default key has been set
	}

	return 0;

}

static int cfg80211_rtw_get_station(struct wiphy *wiphy,
				    struct net_device *ndev,
				    u8 *mac, struct station_info *sinfo)
{
	int ret = 0;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(ndev);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct sta_info *psta = NULL;
	struct sta_priv *pstapriv = &padapter->stapriv;

	sinfo->filled = 0;

	if (!mac) {
		DBG_871X(FUNC_NDEV_FMT" mac==%p\n", FUNC_NDEV_ARG(ndev), mac);
		ret = -ENOENT;
		goto exit;
	}

	psta = rtw_get_stainfo(pstapriv, mac);
	if (psta == NULL) {
		DBG_8192C("%s, sta_info is null\n", __func__);
		ret = -ENOENT;
		goto exit;
	}

#ifdef CONFIG_DEBUG_CFG80211
	DBG_871X(FUNC_NDEV_FMT" mac="MAC_FMT"\n", FUNC_NDEV_ARG(ndev), MAC_ARG(mac));
#endif

	//for infra./P2PClient mode
	if(	check_fwstate(pmlmepriv, WIFI_STATION_STATE)
		&& check_fwstate(pmlmepriv, _FW_LINKED)
	)
	{
		struct wlan_network  *cur_network = &(pmlmepriv->cur_network);

		if (_rtw_memcmp(mac, cur_network->network.MacAddress, ETH_ALEN) == _FALSE) {
			DBG_871X("%s, mismatch bssid="MAC_FMT"\n", __func__, MAC_ARG(cur_network->network.MacAddress));
			ret = -ENOENT;
			goto exit;
		}

		sinfo->filled |= STATION_INFO_SIGNAL;
		sinfo->signal = translate_percentage_to_dbm(padapter->recvpriv.signal_strength);

		sinfo->filled |= STATION_INFO_TX_BITRATE;
		sinfo->txrate.legacy = rtw_get_cur_max_rate(padapter);

		sinfo->filled |= STATION_INFO_RX_PACKETS;
		sinfo->rx_packets = sta_rx_data_pkts(psta);

		sinfo->filled |= STATION_INFO_TX_PACKETS;
		sinfo->tx_packets = psta->sta_stats.tx_pkts;

	}

	//for Ad-Hoc/AP mode
	if ((check_fwstate(pmlmepriv, WIFI_ADHOC_STATE)
			||check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE)
			||check_fwstate(pmlmepriv, WIFI_AP_STATE))
		&& check_fwstate(pmlmepriv, _FW_LINKED)
	)
	{
		//TODO: should acquire station info...
	}

exit:
	return ret;
}

extern int netdev_open(struct net_device *pnetdev);
#ifdef CONFIG_CONCURRENT_MODE
extern int netdev_if2_open(struct net_device *pnetdev);
#endif

/*
enum nl80211_iftype {
	NL80211_IFTYPE_UNSPECIFIED,
       NL80211_IFTYPE_ADHOC, //1
       NL80211_IFTYPE_STATION, //2
       NL80211_IFTYPE_AP, //3
       NL80211_IFTYPE_AP_VLAN,
       NL80211_IFTYPE_WDS,
       NL80211_IFTYPE_MONITOR, //6
       NL80211_IFTYPE_MESH_POINT,
       NL80211_IFTYPE_P2P_CLIENT, //8
	NL80211_IFTYPE_P2P_GO, //9
       //keep last 
       NUM_NL80211_IFTYPES,
       NL80211_IFTYPE_MAX = NUM_NL80211_IFTYPES - 1
};
*/
static int cfg80211_rtw_change_iface(struct wiphy *wiphy,
				     struct net_device *ndev,
				     enum nl80211_iftype type, u32 *flags,
				     struct vif_params *params)
{
	enum nl80211_iftype old_type;
	NDIS_802_11_NETWORK_INFRASTRUCTURE networkType;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(ndev);
	struct wireless_dev *rtw_wdev = padapter->rtw_wdev;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
#ifdef CONFIG_P2P
	struct wifidirect_info *pwdinfo= &(padapter->wdinfo);
#endif
	int ret = 0;
	u8 change = _FALSE;

	DBG_871X(FUNC_NDEV_FMT" type=%d\n", FUNC_NDEV_ARG(ndev), type);

	if(adapter_to_dvobj(padapter)->processing_dev_remove == _TRUE)
	{
		ret= -EPERM;
		goto exit;
	}		
	
#ifdef CONFIG_CONCURRENT_MODE
	if(padapter->adapter_type == SECONDARY_ADAPTER)
	{
		DBG_871X(FUNC_NDEV_FMT" call netdev_if2_open\n", FUNC_NDEV_ARG(ndev));
		if(netdev_if2_open(ndev) != 0) {
			DBG_871X(FUNC_NDEV_FMT" call netdev_if2_open fail\n", FUNC_NDEV_ARG(ndev));
			ret= -EPERM;
			goto exit;
		}
	}
	else if(padapter->adapter_type == PRIMARY_ADAPTER)
#endif //CONFIG_CONCURRENT_MODE
	{
		DBG_871X(FUNC_NDEV_FMT" call netdev_open\n", FUNC_NDEV_ARG(ndev));
		if(netdev_open(ndev) != 0) {
			DBG_871X(FUNC_NDEV_FMT" call netdev_open fail\n", FUNC_NDEV_ARG(ndev));
			ret= -EPERM;
			goto exit;
		}
	}

	if(_FAIL == rtw_pwr_wakeup(padapter)) {
		DBG_871X(FUNC_NDEV_FMT" call rtw_pwr_wakeup fail\n", FUNC_NDEV_ARG(ndev));
		ret= -EPERM;
		goto exit;
	}

	old_type = rtw_wdev->iftype;
	DBG_871X(FUNC_NDEV_FMT" old_iftype=%d, new_iftype=%d\n",
		FUNC_NDEV_ARG(ndev), old_type, type);

	if(old_type != type)
	{
		change = _TRUE;
		pmlmeext->action_public_rxseq = 0xffff;
		pmlmeext->action_public_dialog_token = 0xff;
	}	
		
	switch (type) {
	case NL80211_IFTYPE_ADHOC:
		networkType = Ndis802_11IBSS;
		break;
#if defined(CONFIG_P2P) && ((LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)) || defined(COMPAT_KERNEL_RELEASE))
	case NL80211_IFTYPE_P2P_CLIENT:
#endif
	case NL80211_IFTYPE_STATION:
		networkType = Ndis802_11Infrastructure;
		#ifdef CONFIG_P2P
		if(pwdinfo->driver_interface == DRIVER_CFG80211 )
		{
			if(change && rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO))
			{
				//it means remove GO and change mode from AP(GO) to station(P2P DEVICE)
				rtw_p2p_set_role(pwdinfo, P2P_ROLE_DEVICE);
				rtw_p2p_set_state(pwdinfo, rtw_p2p_pre_state(pwdinfo));

				DBG_8192C("%s, role=%d, p2p_state=%d, pre_p2p_state=%d\n", __func__, rtw_p2p_role(pwdinfo), rtw_p2p_state(pwdinfo), rtw_p2p_pre_state(pwdinfo));
			}
		}
		#endif //CONFIG_P2P
		break;
#if defined(CONFIG_P2P) && ((LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)) || defined(COMPAT_KERNEL_RELEASE))
	case NL80211_IFTYPE_P2P_GO:
#endif
	case NL80211_IFTYPE_AP:
		networkType = Ndis802_11APMode;
		#ifdef CONFIG_P2P
		if(pwdinfo->driver_interface == DRIVER_CFG80211 )
		{
			if(change && !rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE))
			{
				//it means P2P Group created, we will be GO and change mode from  P2P DEVICE to AP(GO)
				rtw_p2p_set_role(pwdinfo, P2P_ROLE_GO);
			}
		}
		#endif //CONFIG_P2P
		break;		
	default:
		ret = -EOPNOTSUPP;
		goto exit;
	}

	rtw_wdev->iftype = type;
	
	if (rtw_set_802_11_infrastructure_mode(padapter, networkType) ==_FALSE)
	{
		rtw_wdev->iftype = old_type;
		ret = -EPERM;
		goto exit;
	}

	rtw_setopmode_cmd(padapter, networkType,_TRUE);	
	
exit:

	DBG_871X(FUNC_NDEV_FMT" ret:%d\n", FUNC_NDEV_ARG(ndev), ret);
	return ret;
}

void rtw_cfg80211_indicate_scan_done(_adapter *adapter, bool aborted)
{
	struct rtw_wdev_priv *pwdev_priv = adapter_wdev_data(adapter);
	_irqL	irqL;

	_enter_critical_bh(&pwdev_priv->scan_req_lock, &irqL);
	if (pwdev_priv->scan_request != NULL) {
		#ifdef CONFIG_DEBUG_CFG80211
		DBG_871X("%s with scan req\n", __FUNCTION__);
		#endif

		/* avoid WARN_ON(request != wiphy_to_dev(request->wiphy)->scan_req); */
		if(pwdev_priv->scan_request->wiphy != pwdev_priv->rtw_wdev->wiphy)
		{
			DBG_8192C("error wiphy compare\n");
		}
		else
		{
			cfg80211_scan_done(pwdev_priv->scan_request, aborted);
		}

		pwdev_priv->scan_request = NULL;
	} else {
		#ifdef CONFIG_DEBUG_CFG80211
		DBG_871X("%s without scan req\n", __FUNCTION__);
		#endif
	}
	_exit_critical_bh(&pwdev_priv->scan_req_lock, &irqL);
}

void rtw_cfg80211_surveydone_event_callback(_adapter *padapter)
{
	_irqL	irqL;
	_list					*plist, *phead;	
	struct	mlme_priv	*pmlmepriv = &(padapter->mlmepriv);	
	_queue				*queue	= &(pmlmepriv->scanned_queue);	
	struct	wlan_network	*pnetwork = NULL;
	u32 cnt=0;
	u32 wait_for_surveydone;
	sint wait_status;
#ifdef CONFIG_P2P
	struct	wifidirect_info*	pwdinfo = &padapter->wdinfo;
#endif //CONFIG_P2P
	struct rtw_wdev_priv *pwdev_priv = adapter_wdev_data(padapter);

#ifdef CONFIG_DEBUG_CFG80211
	DBG_8192C("%s\n", __func__);
#endif

	_enter_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);

	phead = get_list_head(queue);
	plist = get_next(phead);
       
	while(1)
	{
		if (rtw_end_of_queue_search(phead,plist)== _TRUE)
			break;

		pnetwork = LIST_CONTAINOR(plist, struct wlan_network, list);

		//report network only if the current channel set contains the channel to which this network belongs
		if(rtw_ch_set_search_ch(padapter->mlmeextpriv.channel_set, pnetwork->network.Configuration.DSConfig) >= 0
			&& rtw_mlme_band_check(padapter, pnetwork->network.Configuration.DSConfig) == _TRUE
			&& _TRUE == rtw_validate_ssid(&(pnetwork->network.Ssid))
		)
		{		
			//ev=translate_scan(padapter, a, pnetwork, ev, stop);
			rtw_cfg80211_inform_bss(padapter, pnetwork);		
		}

		plist = get_next(plist);
	
	}
	
	_exit_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);
}

static int rtw_cfg80211_set_probe_req_wpsp2pie(_adapter *padapter, char *buf, int len)
{
	int ret = 0;
	uint wps_ielen = 0;
	u8 *wps_ie;
	u32	p2p_ielen = 0;	
	u8 *p2p_ie;
	u32	wfd_ielen = 0;	
	u8 *wfd_ie;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);	

#ifdef CONFIG_DEBUG_CFG80211
	DBG_8192C("%s, ielen=%d\n", __func__, len);
#endif
	
	if(len>0)
	{
		if((wps_ie = rtw_get_wps_ie(buf, len, NULL, &wps_ielen)))
		{
			#ifdef CONFIG_DEBUG_CFG80211
			DBG_8192C("probe_req_wps_ielen=%d\n", wps_ielen);
			#endif
			
			if(pmlmepriv->wps_probe_req_ie)
			{
				u32 free_len = pmlmepriv->wps_probe_req_ie_len;
				pmlmepriv->wps_probe_req_ie_len = 0;
				rtw_mfree(pmlmepriv->wps_probe_req_ie, free_len);
				pmlmepriv->wps_probe_req_ie = NULL;			
			}	

			pmlmepriv->wps_probe_req_ie = rtw_malloc(wps_ielen);
			if ( pmlmepriv->wps_probe_req_ie == NULL) {
				DBG_8192C("%s()-%d: rtw_malloc() ERROR!\n", __FUNCTION__, __LINE__);
				return -EINVAL;
			
			}
			_rtw_memcpy(pmlmepriv->wps_probe_req_ie, wps_ie, wps_ielen);
			pmlmepriv->wps_probe_req_ie_len = wps_ielen;
		}

		//buf += wps_ielen;
		//len -= wps_ielen;

		#ifdef CONFIG_P2P
		if((p2p_ie=rtw_get_p2p_ie(buf, len, NULL, &p2p_ielen))) 
		{
			struct wifidirect_info *wdinfo = &padapter->wdinfo;
			u32 attr_contentlen = 0;
			u8 listen_ch_attr[5];

			#ifdef CONFIG_DEBUG_CFG80211
			DBG_8192C("probe_req_p2p_ielen=%d\n", p2p_ielen);
			#endif
			
			if(pmlmepriv->p2p_probe_req_ie)
			{
				u32 free_len = pmlmepriv->p2p_probe_req_ie_len;
				pmlmepriv->p2p_probe_req_ie_len = 0;
				rtw_mfree(pmlmepriv->p2p_probe_req_ie, free_len);
				pmlmepriv->p2p_probe_req_ie = NULL;
			}	

			pmlmepriv->p2p_probe_req_ie = rtw_malloc(p2p_ielen);
			if ( pmlmepriv->p2p_probe_req_ie == NULL) {
				DBG_8192C("%s()-%d: rtw_malloc() ERROR!\n", __FUNCTION__, __LINE__);
				return -EINVAL;
			
			}
			_rtw_memcpy(pmlmepriv->p2p_probe_req_ie, p2p_ie, p2p_ielen);
			pmlmepriv->p2p_probe_req_ie_len = p2p_ielen;

			if(rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_LISTEN_CH, (u8*)listen_ch_attr, (uint*) &attr_contentlen)
				&& attr_contentlen == 5)
			{
				if (wdinfo->listen_channel !=  listen_ch_attr[4]) {
					DBG_871X(FUNC_ADPT_FMT" listen channel - country:%c%c%c, class:%u, ch:%u\n",
						FUNC_ADPT_ARG(padapter), listen_ch_attr[0], listen_ch_attr[1], listen_ch_attr[2],
						listen_ch_attr[3], listen_ch_attr[4]);
					wdinfo->listen_channel = listen_ch_attr[4];
				}
			}
		}
		#endif //CONFIG_P2P
		
		//buf += p2p_ielen;
		//len -= p2p_ielen;

		#ifdef CONFIG_WFD
		if(rtw_get_wfd_ie(buf, len, NULL, &wfd_ielen)) 
		{
			#ifdef CONFIG_DEBUG_CFG80211
			DBG_8192C("probe_req_wfd_ielen=%d\n", wfd_ielen);
			#endif
			
			if(pmlmepriv->wfd_probe_req_ie)
			{
				u32 free_len = pmlmepriv->wfd_probe_req_ie_len;
				pmlmepriv->wfd_probe_req_ie_len = 0;
				rtw_mfree(pmlmepriv->wfd_probe_req_ie, free_len);
				pmlmepriv->wfd_probe_req_ie = NULL;
			}	

			pmlmepriv->wfd_probe_req_ie = rtw_malloc(wfd_ielen);
			if ( pmlmepriv->wfd_probe_req_ie == NULL) {
				DBG_8192C("%s()-%d: rtw_malloc() ERROR!\n", __FUNCTION__, __LINE__);
				return -EINVAL;
			
			}
			rtw_get_wfd_ie(buf, len, pmlmepriv->wfd_probe_req_ie, &pmlmepriv->wfd_probe_req_ie_len);			
		}
		#endif //CONFIG_WFD
		
	}

	return ret;
	
}

static int cfg80211_rtw_scan(struct wiphy *wiphy
	#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0))
	, struct net_device *ndev
	#endif
	, struct cfg80211_scan_request *request)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0))
	struct net_device *ndev = wdev_to_ndev(request->wdev);
#endif
	int i;
	u8 _status = _FALSE;
	int ret = 0;	
	NDIS_802_11_SSID ssid[RTW_SSID_SCAN_AMOUNT];
	struct rtw_ieee80211_channel ch[RTW_CHANNEL_SCAN_AMOUNT];
	_irqL	irqL;
	u8 *wps_ie=NULL;
	uint wps_ielen=0;	
	u8 *p2p_ie=NULL;
	uint p2p_ielen=0;
	u8 survey_times=3;
	u8 survey_times_for_one_ch=6;
	struct cfg80211_ssid *ssids = request->ssids;
	int social_channel = 0, j = 0;
	bool need_indicate_scan_done = _FALSE;

	_adapter *padapter;
	struct rtw_wdev_priv *pwdev_priv;
	struct mlme_priv *pmlmepriv;
#ifdef CONFIG_P2P
	struct wifidirect_info *pwdinfo;
#endif //CONFIG_P2P
#ifdef CONFIG_CONCURRENT_MODE	
	PADAPTER pbuddy_adapter = NULL;
	struct mlme_priv *pbuddy_mlmepriv = NULL;
#endif //CONFIG_CONCURRENT_MODE

	if (ndev == NULL) {
		ret = -EINVAL;
		goto exit;
	}

	padapter = (_adapter *)rtw_netdev_priv(ndev);
	pwdev_priv = adapter_wdev_data(padapter);
	pmlmepriv= &padapter->mlmepriv;
#ifdef CONFIG_P2P
	pwdinfo= &(padapter->wdinfo);	
#endif //CONFIG_P2P

//#ifdef CONFIG_DEBUG_CFG80211
	DBG_871X(FUNC_ADPT_FMT"\n", FUNC_ADPT_ARG(padapter));
//#endif

#ifdef CONFIG_CONCURRENT_MODE
	if (padapter->pbuddy_adapter) {
		pbuddy_adapter = padapter->pbuddy_adapter;
		pbuddy_mlmepriv = &(pbuddy_adapter->mlmepriv);	
	}
#endif //CONFIG_CONCURRENT_MODE

#ifdef CONFIG_MP_INCLUDED
if (padapter->registrypriv.mp_mode == 1)
{
		DBG_871X(FUNC_ADPT_FMT ": MP mode block Scan request\n", FUNC_ADPT_ARG(padapter));  
		ret = -EPERM;
		goto exit;
}
#ifdef CONFIG_CONCURRENT_MODE
	if (padapter->pbuddy_adapter) {
		if (padapter->pbuddy_adapter->registrypriv.mp_mode == 1)
		{
			DBG_871X(FUNC_ADPT_FMT ": MP mode block Scan request\n", FUNC_ADPT_ARG(padapter->pbuddy_adapter));
			ret = -EPERM;
			goto exit;
		}
	}
#endif //CONFIG_CONCURRENT_MODE
#endif

	_enter_critical_bh(&pwdev_priv->scan_req_lock, &irqL);
	pwdev_priv->scan_request = request;
	_exit_critical_bh(&pwdev_priv->scan_req_lock, &irqL);

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE)
	{
#ifdef CONFIG_DEBUG_CFG80211
		DBG_871X("%s under WIFI_AP_STATE\n", __FUNCTION__);
#endif

		if (check_fwstate(pmlmepriv, WIFI_UNDER_WPS|_FW_UNDER_SURVEY|_FW_UNDER_LINKING) == _TRUE)
		{
			DBG_8192C("%s, fwstate=0x%x\n", __func__, pmlmepriv->fw_state);

			if(check_fwstate(pmlmepriv, WIFI_UNDER_WPS))
			{
				DBG_8192C("AP mode process WPS \n");
			}
			
			need_indicate_scan_done = _TRUE;
			goto check_need_indicate_scan_done;
		} 
	}

	rtw_ps_deny(padapter, PS_DENY_SCAN);
	if(_FAIL == rtw_pwr_wakeup(padapter)) {
		need_indicate_scan_done = _TRUE;
		goto check_need_indicate_scan_done;
	}

	#ifdef CONFIG_P2P
	if( pwdinfo->driver_interface == DRIVER_CFG80211 )
	{
		if(ssids->ssid != NULL
			&& _rtw_memcmp(ssids->ssid, "DIRECT-", 7)
			&& rtw_get_p2p_ie((u8 *)request->ie, request->ie_len, NULL, NULL)
		)
		{
			if(rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE))
			{
				rtw_p2p_enable(padapter, P2P_ROLE_DEVICE);
				adapter_wdev_data(padapter)->p2p_enabled = _TRUE;
			}
			else
			{
				rtw_p2p_set_pre_state(pwdinfo, rtw_p2p_state(pwdinfo));
				#ifdef CONFIG_DEBUG_CFG80211
				DBG_8192C("%s, role=%d, p2p_state=%d\n", __func__, rtw_p2p_role(pwdinfo), rtw_p2p_state(pwdinfo));
				#endif
			}
			rtw_p2p_set_state(pwdinfo, P2P_STATE_LISTEN);
		
			if(request->n_channels == 3 &&
				request->channels[0]->hw_value == 1 &&
				request->channels[1]->hw_value == 6 &&
				request->channels[2]->hw_value == 11
			)
			{
				social_channel = 1;
			}
		}
	}
	#endif //CONFIG_P2P

	if(request->ie && request->ie_len>0)
	{
		rtw_cfg80211_set_probe_req_wpsp2pie(padapter, (u8 *)request->ie, request->ie_len );
	}

	if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY) == _TRUE) {
		DBG_8192C("%s, fwstate=0x%x\n", __func__, pmlmepriv->fw_state);
		need_indicate_scan_done = _TRUE;
		goto check_need_indicate_scan_done;
	} else if (check_fwstate(pmlmepriv, _FW_UNDER_LINKING) == _TRUE) {
		DBG_8192C("%s, fwstate=0x%x\n", __func__, pmlmepriv->fw_state);
		ret = -EBUSY;
		goto check_need_indicate_scan_done;
	}

	if (pmlmepriv->LinkDetectInfo.bBusyTraffic == _TRUE)
	{
#if 1 // Miracast can't do AP scan
		static u32 lastscantime = 0;
		u32 passtime;

		passtime = rtw_get_passing_time_ms(lastscantime);
		lastscantime = rtw_get_current_time();
		if (passtime > 12000)
#endif
		{
			DBG_871X("%s: bBusyTraffic == _TRUE\n", __FUNCTION__);
			need_indicate_scan_done = _TRUE;
			goto check_need_indicate_scan_done;
		}
	}

	if (rtw_is_scan_deny(padapter)){
		DBG_871X(FUNC_ADPT_FMT  ": scan deny\n", FUNC_ADPT_ARG(padapter));
		need_indicate_scan_done = _TRUE;
		goto check_need_indicate_scan_done;
	}

#ifdef CONFIG_CONCURRENT_MODE
	if(pbuddy_mlmepriv && (pbuddy_mlmepriv->LinkDetectInfo.bBusyTraffic == _TRUE))	
	{
#if 1 // Miracast can't do AP scan
		static u32 buddylastscantime = 0;
		u32 passtime;

		passtime = rtw_get_passing_time_ms(buddylastscantime);
		buddylastscantime = rtw_get_current_time();
		if ((passtime > 12000)
//#ifdef CONFIG_P2P
//			||(!rtw_p2p_chk_state(&padapter->wdinfo, P2P_STATE_NONE))
//#endif //CONFIG_P2P
		)
#endif
		{
			DBG_871X("%s: bBusyTraffic == _TRUE at buddy_intf\n", __FUNCTION__);
			need_indicate_scan_done = _TRUE;
			goto check_need_indicate_scan_done;
		}
	}

	if (check_buddy_fwstate(padapter, _FW_UNDER_LINKING|WIFI_UNDER_WPS) == _TRUE) {
		DBG_871X("buddy_intf's mlme state:0x%x\n", pbuddy_mlmepriv->fw_state);
		need_indicate_scan_done = _TRUE;
		goto check_need_indicate_scan_done;

	} else if (check_buddy_fwstate(padapter, _FW_UNDER_SURVEY)) {
		bool scan_via_buddy = _FALSE;
		struct rtw_wdev_priv *buddy_wdev_priv = adapter_wdev_data(pbuddy_adapter);

		_enter_critical_bh(&pwdev_priv->scan_req_lock, &irqL);
		_enter_critical_bh(&buddy_wdev_priv->scan_req_lock, &irqL);
		if (buddy_wdev_priv->scan_request) {
			DBG_871X("scan via buddy\n");
			pmlmepriv->scanning_via_buddy_intf = _TRUE;
			_enter_critical_bh(&pmlmepriv->lock, &irqL);
			set_fwstate(pmlmepriv, _FW_UNDER_SURVEY);
			_exit_critical_bh(&pmlmepriv->lock, &irqL);
			scan_via_buddy = _TRUE;
		}
		_exit_critical_bh(&buddy_wdev_priv->scan_req_lock, &irqL);
		_exit_critical_bh(&pwdev_priv->scan_req_lock, &irqL);

		if (scan_via_buddy == _FALSE)
			need_indicate_scan_done = _TRUE;

		goto check_need_indicate_scan_done;
	}
#endif /* CONFIG_CONCURRENT_MODE */

#ifdef CONFIG_P2P
	if(!rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE) && !rtw_p2p_chk_state(pwdinfo, P2P_STATE_IDLE))
	{
		rtw_p2p_set_state(pwdinfo, P2P_STATE_FIND_PHASE_SEARCH);
		rtw_free_network_queue(padapter, _TRUE);

		if(social_channel == 0)
			rtw_p2p_findphase_ex_set(pwdinfo, P2P_FINDPHASE_EX_NONE);
		else
			rtw_p2p_findphase_ex_set(pwdinfo, P2P_FINDPHASE_EX_SOCIAL_LAST);
	}
#endif //CONFIG_P2P


	_rtw_memset(ssid, 0, sizeof(NDIS_802_11_SSID)*RTW_SSID_SCAN_AMOUNT);
	//parsing request ssids, n_ssids
	for (i = 0; i < request->n_ssids && i < RTW_SSID_SCAN_AMOUNT; i++) {
		#ifdef CONFIG_DEBUG_CFG80211
		DBG_8192C("ssid=%s, len=%d\n", ssids[i].ssid, ssids[i].ssid_len);
		#endif
		_rtw_memcpy(ssid[i].Ssid, ssids[i].ssid, ssids[i].ssid_len);
		ssid[i].SsidLength = ssids[i].ssid_len;	
	}

	/* parsing channels, n_channels */
	_rtw_memset(ch, 0, sizeof(struct rtw_ieee80211_channel)*RTW_CHANNEL_SCAN_AMOUNT);
	for (i=0;i<request->n_channels && i<RTW_CHANNEL_SCAN_AMOUNT;i++) {
		#ifdef CONFIG_DEBUG_CFG80211
		DBG_871X(FUNC_ADPT_FMT CHAN_FMT"\n", FUNC_ADPT_ARG(padapter), CHAN_ARG(request->channels[i]));
		#endif
		ch[i].hw_value = request->channels[i]->hw_value;
		ch[i].flags = request->channels[i]->flags;
	}

	_enter_critical_bh(&pmlmepriv->lock, &irqL);
	if (request->n_channels == 1) {
		for(i=1;i<survey_times_for_one_ch;i++)
			_rtw_memcpy(&ch[i], &ch[0], sizeof(struct rtw_ieee80211_channel));
		_status = rtw_sitesurvey_cmd(padapter, ssid, RTW_SSID_SCAN_AMOUNT, ch, survey_times_for_one_ch);
	} else if (request->n_channels <= 4) {
		for(j=request->n_channels-1;j>=0;j--)
			for(i=0;i<survey_times;i++)
		{
			_rtw_memcpy(&ch[j*survey_times+i], &ch[j], sizeof(struct rtw_ieee80211_channel));
		}
		_status = rtw_sitesurvey_cmd(padapter, ssid, RTW_SSID_SCAN_AMOUNT, ch, survey_times * request->n_channels);
	} else {
		_status = rtw_sitesurvey_cmd(padapter, ssid, RTW_SSID_SCAN_AMOUNT, NULL, 0);
	}
	_exit_critical_bh(&pmlmepriv->lock, &irqL);


	if(_status == _FALSE)
	{
		ret = -1;
	}

check_need_indicate_scan_done:
	if (_TRUE == need_indicate_scan_done)
	{
		rtw_cfg80211_surveydone_event_callback(padapter);
		rtw_cfg80211_indicate_scan_done(padapter, _FALSE);
	}

cancel_ps_deny:
	rtw_ps_deny_cancel(padapter, PS_DENY_SCAN);

exit:
	return ret;
	
}

static int cfg80211_rtw_set_wiphy_params(struct wiphy *wiphy, u32 changed)
{
#if 0
	struct iwm_priv *iwm = wiphy_to_iwm(wiphy);

	if (changed & WIPHY_PARAM_RTS_THRESHOLD &&
	    (iwm->conf.rts_threshold != wiphy->rts_threshold)) {
		int ret;

		iwm->conf.rts_threshold = wiphy->rts_threshold;

		ret = iwm_umac_set_config_fix(iwm, UMAC_PARAM_TBL_CFG_FIX,
					     CFG_RTS_THRESHOLD,
					     iwm->conf.rts_threshold);
		if (ret < 0)
			return ret;
	}

	if (changed & WIPHY_PARAM_FRAG_THRESHOLD &&
	    (iwm->conf.frag_threshold != wiphy->frag_threshold)) {
		int ret;

		iwm->conf.frag_threshold = wiphy->frag_threshold;

		ret = iwm_umac_set_config_fix(iwm, UMAC_PARAM_TBL_FA_CFG_FIX,
					     CFG_FRAG_THRESHOLD,
					     iwm->conf.frag_threshold);
		if (ret < 0)
			return ret;
	}
#endif
	DBG_8192C("%s\n", __func__);
	return 0;
}



static int rtw_cfg80211_set_wpa_version(struct security_priv *psecuritypriv, u32 wpa_version)
{	
	DBG_8192C("%s, wpa_version=%d\n", __func__, wpa_version);
	
	if (!wpa_version) {		
		psecuritypriv->ndisauthtype = Ndis802_11AuthModeOpen;		
		return 0;
	}


	if (wpa_version & (NL80211_WPA_VERSION_1 | NL80211_WPA_VERSION_2))
	{		
		psecuritypriv->ndisauthtype = Ndis802_11AuthModeWPAPSK;		
	}

/*
	if (wpa_version & NL80211_WPA_VERSION_2)
	{		
		psecuritypriv->ndisauthtype = Ndis802_11AuthModeWPA2PSK;
	}
*/

	#ifdef CONFIG_WAPI_SUPPORT
	if (wpa_version & NL80211_WAPI_VERSION_1)
	{
		psecuritypriv->ndisauthtype = Ndis802_11AuthModeWAPI;
	}
	#endif

	return 0;

}

static int rtw_cfg80211_set_auth_type(struct security_priv *psecuritypriv,
			     enum nl80211_auth_type sme_auth_type)
{
	DBG_8192C("%s, nl80211_auth_type=%d\n", __func__, sme_auth_type);


	switch (sme_auth_type) {
	case NL80211_AUTHTYPE_AUTOMATIC:

		psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_Auto;

		break;
	case NL80211_AUTHTYPE_OPEN_SYSTEM:
	
		psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_Open;

		if(psecuritypriv->ndisauthtype>Ndis802_11AuthModeWPA)
			psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_8021X;
		
#ifdef CONFIG_WAPI_SUPPORT
		if(psecuritypriv->ndisauthtype == Ndis802_11AuthModeWAPI)
			psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_WAPI;
#endif

		break;
	case NL80211_AUTHTYPE_SHARED_KEY:

		psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_Shared;

		psecuritypriv->ndisencryptstatus = Ndis802_11Encryption1Enabled;


		break;
	default:		
		psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_Open;
		//return -ENOTSUPP;
	}

	return 0;

}

static int rtw_cfg80211_set_cipher(struct security_priv *psecuritypriv, u32 cipher, bool ucast)
{
	u32 ndisencryptstatus = Ndis802_11EncryptionDisabled;

	u32 *profile_cipher = ucast ? &psecuritypriv->dot11PrivacyAlgrthm :
		&psecuritypriv->dot118021XGrpPrivacy;

	DBG_8192C("%s, ucast=%d, cipher=0x%x\n", __func__, ucast, cipher);


	if (!cipher) {
		*profile_cipher = _NO_PRIVACY_;
		psecuritypriv->ndisencryptstatus = ndisencryptstatus;
		return 0;
	}
	
	switch (cipher) {
	case IW_AUTH_CIPHER_NONE:
		*profile_cipher = _NO_PRIVACY_;
		ndisencryptstatus = Ndis802_11EncryptionDisabled;
#ifdef CONFIG_WAPI_SUPPORT
		if(psecuritypriv->dot11PrivacyAlgrthm ==_SMS4_ )
		{
			*profile_cipher = _SMS4_;
		}
#endif
		break;
	case WLAN_CIPHER_SUITE_WEP40:
		*profile_cipher = _WEP40_;
		ndisencryptstatus = Ndis802_11Encryption1Enabled;
		break;
	case WLAN_CIPHER_SUITE_WEP104:
		*profile_cipher = _WEP104_;
		ndisencryptstatus = Ndis802_11Encryption1Enabled;
		break;
	case WLAN_CIPHER_SUITE_TKIP:
		*profile_cipher = _TKIP_;
		ndisencryptstatus = Ndis802_11Encryption2Enabled;
		break;
	case WLAN_CIPHER_SUITE_CCMP:
		*profile_cipher = _AES_;
		ndisencryptstatus = Ndis802_11Encryption3Enabled;
		break;
#ifdef CONFIG_WAPI_SUPPORT
	case WLAN_CIPHER_SUITE_SMS4:
		*profile_cipher = _SMS4_;
		ndisencryptstatus = Ndis802_11_EncrypteionWAPI;
		break;
#endif
	default:
		DBG_8192C("Unsupported cipher: 0x%x\n", cipher);
		return -ENOTSUPP;
	}

	if(ucast)
	{
		psecuritypriv->ndisencryptstatus = ndisencryptstatus;
		
		//if(psecuritypriv->dot11PrivacyAlgrthm >= _AES_)
		//	psecuritypriv->ndisauthtype = Ndis802_11AuthModeWPA2PSK;
	}	

	return 0;
}

static int rtw_cfg80211_set_key_mgt(struct security_priv *psecuritypriv, u32 key_mgt)
{
	DBG_8192C("%s, key_mgt=0x%x\n", __func__, key_mgt);

	if (key_mgt == WLAN_AKM_SUITE_8021X)
		//*auth_type = UMAC_AUTH_TYPE_8021X;
		psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_8021X;
	else if (key_mgt == WLAN_AKM_SUITE_PSK) {
		psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_8021X;
	}
#ifdef CONFIG_WAPI_SUPPORT
	else if(key_mgt ==WLAN_AKM_SUITE_WAPI_PSK){
		psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_WAPI;
	}
	else if(key_mgt ==WLAN_AKM_SUITE_WAPI_CERT){
		psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_WAPI;
	}
#endif


	else {
		DBG_8192C("Invalid key mgt: 0x%x\n", key_mgt);
		//return -EINVAL;
	}

	return 0;
}

static int rtw_cfg80211_set_wpa_ie(_adapter *padapter, u8 *pie, size_t ielen)
{
	u8 *buf=NULL, *pos=NULL;	
	u32 left; 	
	int group_cipher = 0, pairwise_cipher = 0;
	int ret = 0;
	int wpa_ielen=0;
	int wpa2_ielen=0;
	u8 *pwpa, *pwpa2;
	u8 null_addr[]= {0,0,0,0,0,0};

	if (pie == NULL || !ielen) {
		/* Treat this as normal case, but need to clear WIFI_UNDER_WPS */
		_clr_fwstate_(&padapter->mlmepriv, WIFI_UNDER_WPS);
		goto exit;
	}

	if (ielen > MAX_WPA_IE_LEN+MAX_WPS_IE_LEN+MAX_P2P_IE_LEN) {
		ret = -EINVAL;
		goto exit;
	}

	buf = rtw_zmalloc(ielen);
	if (buf == NULL){
		ret =  -ENOMEM;
		goto exit;
	}

	_rtw_memcpy(buf, pie , ielen);

	//dump
	{
		int i;
		DBG_8192C("set wpa_ie(length:%zu):\n", ielen);
		for(i=0;i<ielen;i=i+8)
			DBG_8192C("0x%.2x 0x%.2x 0x%.2x 0x%.2x 0x%.2x 0x%.2x 0x%.2x 0x%.2x \n",buf[i],buf[i+1],buf[i+2],buf[i+3],buf[i+4],buf[i+5],buf[i+6],buf[i+7]);
	}

	pos = buf;
	if(ielen < RSN_HEADER_LEN){
		RT_TRACE(_module_rtl871x_ioctl_os_c,_drv_err_,("Ie len too short %d\n", ielen));
		ret  = -1;
		goto exit;
	}

	pwpa = rtw_get_wpa_ie(buf, &wpa_ielen, ielen);
	if(pwpa && wpa_ielen>0)
	{
		if(rtw_parse_wpa_ie(pwpa, wpa_ielen+2, &group_cipher, &pairwise_cipher, NULL) == _SUCCESS)
		{
			padapter->securitypriv.dot11AuthAlgrthm= dot11AuthAlgrthm_8021X;
			padapter->securitypriv.ndisauthtype=Ndis802_11AuthModeWPAPSK;
			_rtw_memcpy(padapter->securitypriv.supplicant_ie, &pwpa[0], wpa_ielen+2);
			
			DBG_8192C("got wpa_ie, wpa_ielen:%u\n", wpa_ielen);
		}
	}

	pwpa2 = rtw_get_wpa2_ie(buf, &wpa2_ielen, ielen);
	if(pwpa2 && wpa2_ielen>0)
	{
		if(rtw_parse_wpa2_ie(pwpa2, wpa2_ielen+2, &group_cipher, &pairwise_cipher, NULL) == _SUCCESS)
		{
			padapter->securitypriv.dot11AuthAlgrthm= dot11AuthAlgrthm_8021X;
			padapter->securitypriv.ndisauthtype=Ndis802_11AuthModeWPA2PSK;	
			_rtw_memcpy(padapter->securitypriv.supplicant_ie, &pwpa2[0], wpa2_ielen+2);

			DBG_8192C("got wpa2_ie, wpa2_ielen:%u\n", wpa2_ielen);
		}
	}

	if (group_cipher == 0)
	{
		group_cipher = WPA_CIPHER_NONE;
	}
	if (pairwise_cipher == 0)
	{
		pairwise_cipher = WPA_CIPHER_NONE;
	}
	
	switch(group_cipher)
	{
		case WPA_CIPHER_NONE:
			padapter->securitypriv.dot118021XGrpPrivacy=_NO_PRIVACY_;
			padapter->securitypriv.ndisencryptstatus=Ndis802_11EncryptionDisabled;
			break;
		case WPA_CIPHER_WEP40:
			padapter->securitypriv.dot118021XGrpPrivacy=_WEP40_;
			padapter->securitypriv.ndisencryptstatus = Ndis802_11Encryption1Enabled;
			break;
		case WPA_CIPHER_TKIP:
			padapter->securitypriv.dot118021XGrpPrivacy=_TKIP_;
			padapter->securitypriv.ndisencryptstatus = Ndis802_11Encryption2Enabled;
			break;
		case WPA_CIPHER_CCMP:
			padapter->securitypriv.dot118021XGrpPrivacy=_AES_;
			padapter->securitypriv.ndisencryptstatus = Ndis802_11Encryption3Enabled;
			break;
		case WPA_CIPHER_WEP104:	
			padapter->securitypriv.dot118021XGrpPrivacy=_WEP104_;
			padapter->securitypriv.ndisencryptstatus = Ndis802_11Encryption1Enabled;
			break;
	}

	switch(pairwise_cipher)
	{
		case WPA_CIPHER_NONE:
			padapter->securitypriv.dot11PrivacyAlgrthm=_NO_PRIVACY_;
			padapter->securitypriv.ndisencryptstatus=Ndis802_11EncryptionDisabled;
			break;
		case WPA_CIPHER_WEP40:
			padapter->securitypriv.dot11PrivacyAlgrthm=_WEP40_;
			padapter->securitypriv.ndisencryptstatus = Ndis802_11Encryption1Enabled;
			break;
		case WPA_CIPHER_TKIP:
			padapter->securitypriv.dot11PrivacyAlgrthm=_TKIP_;
			padapter->securitypriv.ndisencryptstatus = Ndis802_11Encryption2Enabled;
			break;
		case WPA_CIPHER_CCMP:
			padapter->securitypriv.dot11PrivacyAlgrthm=_AES_;
			padapter->securitypriv.ndisencryptstatus = Ndis802_11Encryption3Enabled;
			break;
		case WPA_CIPHER_WEP104:	
			padapter->securitypriv.dot11PrivacyAlgrthm=_WEP104_;
			padapter->securitypriv.ndisencryptstatus = Ndis802_11Encryption1Enabled;
			break;
	}
	
	{/* handle wps_ie */
		uint wps_ielen;	
		u8 *wps_ie;

		wps_ie = rtw_get_wps_ie(buf, ielen, NULL, &wps_ielen);
		if (wps_ie && wps_ielen > 0) {
			DBG_8192C("got wps_ie, wps_ielen:%u\n", wps_ielen);
			padapter->securitypriv.wps_ie_len = wps_ielen<MAX_WPS_IE_LEN?wps_ielen:MAX_WPS_IE_LEN;
			_rtw_memcpy(padapter->securitypriv.wps_ie, wps_ie, padapter->securitypriv.wps_ie_len);
			set_fwstate(&padapter->mlmepriv, WIFI_UNDER_WPS);
		} else {
			_clr_fwstate_(&padapter->mlmepriv, WIFI_UNDER_WPS);
		}
	}

	#ifdef CONFIG_P2P
	{//check p2p_ie for assoc req; 
		uint p2p_ielen=0;
		u8 *p2p_ie;
		struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);

		if((p2p_ie=rtw_get_p2p_ie(buf, ielen, NULL, &p2p_ielen)))
		{
			#ifdef CONFIG_DEBUG_CFG80211
			DBG_8192C("%s p2p_assoc_req_ielen=%d\n", __FUNCTION__, p2p_ielen);
			#endif

			if(pmlmepriv->p2p_assoc_req_ie)
			{
				u32 free_len = pmlmepriv->p2p_assoc_req_ie_len;
				pmlmepriv->p2p_assoc_req_ie_len = 0;
				rtw_mfree(pmlmepriv->p2p_assoc_req_ie, free_len);
				pmlmepriv->p2p_assoc_req_ie = NULL;
			}

			pmlmepriv->p2p_assoc_req_ie = rtw_malloc(p2p_ielen);
			if ( pmlmepriv->p2p_assoc_req_ie == NULL) {
				DBG_8192C("%s()-%d: rtw_malloc() ERROR!\n", __FUNCTION__, __LINE__);
				goto exit;
			}
			_rtw_memcpy(pmlmepriv->p2p_assoc_req_ie, p2p_ie, p2p_ielen);
			pmlmepriv->p2p_assoc_req_ie_len = p2p_ielen;
		}
	}
	#endif //CONFIG_P2P

	#ifdef CONFIG_WFD
	{//check wfd_ie for assoc req; 
		uint wfd_ielen=0;
		u8 *wfd_ie;
		struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);

		if(rtw_get_wfd_ie(buf, ielen, NULL, &wfd_ielen))
		{
			#ifdef CONFIG_DEBUG_CFG80211
			DBG_8192C("%s wfd_assoc_req_ielen=%d\n", __FUNCTION__, wfd_ielen);
			#endif

			if(pmlmepriv->wfd_assoc_req_ie)
			{
				u32 free_len = pmlmepriv->wfd_assoc_req_ie_len;
				pmlmepriv->wfd_assoc_req_ie_len = 0;
				rtw_mfree(pmlmepriv->wfd_assoc_req_ie, free_len);
				pmlmepriv->wfd_assoc_req_ie = NULL;
			}

			pmlmepriv->wfd_assoc_req_ie = rtw_malloc(wfd_ielen);
			if ( pmlmepriv->wfd_assoc_req_ie == NULL) {
				DBG_8192C("%s()-%d: rtw_malloc() ERROR!\n", __FUNCTION__, __LINE__);
				goto exit;
			}
			rtw_get_wfd_ie(buf, ielen, pmlmepriv->wfd_assoc_req_ie, &pmlmepriv->wfd_assoc_req_ie_len);			
		}
	}
	#endif //CONFIG_WFD

	//TKIP and AES disallow multicast packets until installing group key
	if(padapter->securitypriv.dot11PrivacyAlgrthm == _TKIP_
		|| padapter->securitypriv.dot11PrivacyAlgrthm == _TKIP_WTMIC_
		|| padapter->securitypriv.dot11PrivacyAlgrthm == _AES_)
		//WPS open need to enable multicast
		//|| check_fwstate(&padapter->mlmepriv, WIFI_UNDER_WPS) == _TRUE)
		rtw_hal_set_hwreg(padapter, HW_VAR_OFF_RCR_AM, null_addr);

	RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_info_,
		("rtw_set_wpa_ie: pairwise_cipher=0x%08x padapter->securitypriv.ndisencryptstatus=%d padapter->securitypriv.ndisauthtype=%d\n",
		pairwise_cipher, padapter->securitypriv.ndisencryptstatus, padapter->securitypriv.ndisauthtype));

exit:
	if (buf)
		rtw_mfree(buf, ielen);
	if (ret)
		_clr_fwstate_(&padapter->mlmepriv, WIFI_UNDER_WPS);
	return ret;
}

static int cfg80211_rtw_join_ibss(struct wiphy *wiphy, struct net_device *ndev,
				  struct cfg80211_ibss_params *params)
{
	_adapter *padapter = (_adapter *)rtw_netdev_priv(ndev);
	NDIS_802_11_SSID ndis_ssid;
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	int ret=0;
	
	if(_FAIL == rtw_pwr_wakeup(padapter)) {
		ret= -EPERM;
		goto exit;
	}

	if(check_fwstate(pmlmepriv, WIFI_AP_STATE)) {
		ret = -EPERM;
		goto exit;
	}

#ifdef CONFIG_CONCURRENT_MODE
	if (check_buddy_fwstate(padapter, _FW_UNDER_LINKING) == _TRUE) {
		DBG_8192C("%s, but buddy_intf is under linking\n", __FUNCTION__);
		ret = -EINVAL;
		goto exit;
	}
	if (check_buddy_fwstate(padapter, _FW_UNDER_SURVEY) == _TRUE) {
		rtw_scan_abort(padapter->pbuddy_adapter);
	}
#endif //CONFIG_CONCURRENT_MODE
	
	if (!params->ssid || !params->ssid_len)
	{
		ret = -EINVAL;
		goto exit;
	}

	if (params->ssid_len > IW_ESSID_MAX_SIZE){

		ret= -E2BIG;
		goto exit;
	}
	
	_rtw_memset(&ndis_ssid, 0, sizeof(NDIS_802_11_SSID));			
	ndis_ssid.SsidLength = params->ssid_len;
	_rtw_memcpy(ndis_ssid.Ssid, params->ssid, params->ssid_len);

	//DBG_8192C("ssid=%s, len=%zu\n", ndis_ssid.Ssid, params->ssid_len);
	
	psecuritypriv->ndisencryptstatus = Ndis802_11EncryptionDisabled;
	psecuritypriv->dot11PrivacyAlgrthm = _NO_PRIVACY_;
	psecuritypriv->dot118021XGrpPrivacy = _NO_PRIVACY_;
	psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_Open; //open system
	psecuritypriv->ndisauthtype = Ndis802_11AuthModeOpen;
	
	ret = rtw_cfg80211_set_auth_type(psecuritypriv, NL80211_AUTHTYPE_OPEN_SYSTEM);
	rtw_set_802_11_authentication_mode(padapter, psecuritypriv->ndisauthtype);
	
	if (rtw_set_802_11_ssid(padapter, &ndis_ssid) == _FALSE) 
	{
		ret = -1;
		goto exit;
	}	
	
exit:
	return ret;
}

static int cfg80211_rtw_leave_ibss(struct wiphy *wiphy, struct net_device *ndev)
{
	_adapter *padapter = (_adapter *)rtw_netdev_priv(ndev);
	struct wireless_dev *rtw_wdev = padapter->rtw_wdev;
	enum nl80211_iftype old_type;
	int ret = 0;

	DBG_871X(FUNC_NDEV_FMT"\n", FUNC_NDEV_ARG(ndev));

	padapter->mlmepriv.not_indic_disco = _TRUE;
	
	old_type = rtw_wdev->iftype;
	
	rtw_set_to_roam(padapter, 0);

	if(check_fwstate(&padapter->mlmepriv, _FW_LINKED)) 
	{
		rtw_scan_abort(padapter);
		LeaveAllPowerSaveMode(padapter);
		
		rtw_wdev->iftype = NL80211_IFTYPE_STATION;
		
		if (rtw_set_802_11_infrastructure_mode(padapter, Ndis802_11Infrastructure) ==_FALSE)
		{
			rtw_wdev->iftype = old_type;
			ret = -EPERM;
			goto leave_ibss;
		}
		rtw_setopmode_cmd(padapter, Ndis802_11Infrastructure,_TRUE);	
	}

leave_ibss:
	padapter->mlmepriv.not_indic_disco = _FALSE;

	return 0;
}

static int cfg80211_rtw_connect(struct wiphy *wiphy, struct net_device *ndev,
				 struct cfg80211_connect_params *sme)
{
	int ret=0;
	_irqL irqL;	
	_list *phead;	
	struct wlan_network *pnetwork = NULL;
	NDIS_802_11_AUTHENTICATION_MODE authmode;	
	NDIS_802_11_SSID ndis_ssid;	
	u8 *dst_ssid, *src_ssid;
	u8 *dst_bssid, *src_bssid;
	//u8 matched_by_bssid=_FALSE;
	//u8 matched_by_ssid=_FALSE;
	u8 matched=_FALSE;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(ndev);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;	
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	_queue *queue = &pmlmepriv->scanned_queue;	

	padapter->mlmepriv.not_indic_disco = _TRUE;
	
	DBG_871X("=>"FUNC_NDEV_FMT"\n", FUNC_NDEV_ARG(ndev));
	DBG_871X("privacy=%d, key=%p, key_len=%d, key_idx=%d\n",
		sme->privacy, sme->key, sme->key_len, sme->key_idx);


	if(adapter_wdev_data(padapter)->block == _TRUE)
	{
		ret = -EBUSY;
		DBG_871X("%s wdev_priv.block is set\n", __FUNCTION__);
		goto exit;
	}

#ifdef CONFIG_PLATFORM_MSTAR_SCAN_BEFORE_CONNECT
	printk("MStar Android!\n");
	if(adapter_wdev_data(padapter)->bandroid_scan == _FALSE)
	{
#ifdef CONFIG_P2P
		struct wifidirect_info *pwdinfo= &(padapter->wdinfo);	
		if(rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE))
#endif //CONFIG_P2P
		{
			ret = -EBUSY;
			printk("Android hasn't attached yet!\n");
			goto exit;
		}	
	}
#endif

	rtw_ps_deny(padapter, PS_DENY_JOIN);
	if(_FAIL == rtw_pwr_wakeup(padapter)) {
		ret= -EPERM;
		goto exit;
	}

	if(check_fwstate(pmlmepriv, WIFI_AP_STATE)) {
		ret = -EPERM;
		goto exit;
	}

#ifdef CONFIG_CONCURRENT_MODE
	if (check_buddy_fwstate(padapter, _FW_UNDER_LINKING) == _TRUE) {
		DBG_8192C("%s, but buddy_intf is under linking\n", __FUNCTION__);
		ret = -EINVAL;
		goto exit;
	}
	if (check_buddy_fwstate(padapter, _FW_UNDER_SURVEY) == _TRUE) {
		rtw_scan_abort(padapter->pbuddy_adapter);
	}
#endif

	if (!sme->ssid || !sme->ssid_len)
	{
		ret = -EINVAL;
		goto exit;
	}

	if (sme->ssid_len > IW_ESSID_MAX_SIZE){

		ret= -E2BIG;
		goto exit;
	}
	
	_rtw_memset(&ndis_ssid, 0, sizeof(NDIS_802_11_SSID));			
	ndis_ssid.SsidLength = sme->ssid_len;
	_rtw_memcpy(ndis_ssid.Ssid, sme->ssid, sme->ssid_len);

	DBG_8192C("ssid=%s, len=%zu\n", ndis_ssid.Ssid, sme->ssid_len);
	

	if (sme->bssid)
		DBG_8192C("bssid="MAC_FMT"\n", MAC_ARG(sme->bssid));


	if (check_fwstate(pmlmepriv, _FW_UNDER_LINKING) == _TRUE) {
		ret = -EBUSY;
		DBG_8192C("%s, fw_state=0x%x, goto exit\n", __FUNCTION__, pmlmepriv->fw_state);
		goto exit;		
	}
	if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY) == _TRUE) {
		rtw_scan_abort(padapter);
	}

	psecuritypriv->ndisencryptstatus = Ndis802_11EncryptionDisabled;
	psecuritypriv->dot11PrivacyAlgrthm = _NO_PRIVACY_;
	psecuritypriv->dot118021XGrpPrivacy = _NO_PRIVACY_;
	psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_Open; //open system
	psecuritypriv->ndisauthtype = Ndis802_11AuthModeOpen;

#ifdef CONFIG_WAPI_SUPPORT
	 padapter->wapiInfo.bWapiEnable = false;
#endif

	ret = rtw_cfg80211_set_wpa_version(psecuritypriv, sme->crypto.wpa_versions);
	if (ret < 0)
		goto exit;

#ifdef CONFIG_WAPI_SUPPORT
	if(sme->crypto.wpa_versions & NL80211_WAPI_VERSION_1)
	{
		padapter->wapiInfo.bWapiEnable = true;
		padapter->wapiInfo.extra_prefix_len = WAPI_EXT_LEN;
		padapter->wapiInfo.extra_postfix_len = SMS4_MIC_LEN;
	}
#endif

	ret = rtw_cfg80211_set_auth_type(psecuritypriv, sme->auth_type);

#ifdef CONFIG_WAPI_SUPPORT
	if(psecuritypriv->dot11AuthAlgrthm == dot11AuthAlgrthm_WAPI)
		padapter->mlmeextpriv.mlmext_info.auth_algo = psecuritypriv->dot11AuthAlgrthm;
#endif


	if (ret < 0)
		goto exit;

	DBG_8192C("%s, ie_len=%zu\n", __func__, sme->ie_len);
			
	ret = rtw_cfg80211_set_wpa_ie(padapter, sme->ie, sme->ie_len);
	if (ret < 0)
		goto exit;

	if (sme->crypto.n_ciphers_pairwise) {		
		ret = rtw_cfg80211_set_cipher(psecuritypriv, sme->crypto.ciphers_pairwise[0], _TRUE);
		if (ret < 0)
			goto exit;
	}

	//For WEP Shared auth
	if((psecuritypriv->dot11AuthAlgrthm == dot11AuthAlgrthm_Shared
		|| psecuritypriv->dot11AuthAlgrthm == dot11AuthAlgrthm_Auto) && sme->key
	)
	{
		u32 wep_key_idx, wep_key_len,wep_total_len;
		NDIS_802_11_WEP	 *pwep = NULL;
		DBG_871X("%s(): Shared/Auto WEP\n",__FUNCTION__);

		wep_key_idx = sme->key_idx;
		wep_key_len = sme->key_len;

		if (sme->key_idx > WEP_KEYS) {
			ret = -EINVAL;
			goto exit;
		}

		if (wep_key_len > 0) 
		{
		 	wep_key_len = wep_key_len <= 5 ? 5 : 13;
			wep_total_len = wep_key_len + FIELD_OFFSET(NDIS_802_11_WEP, KeyMaterial);
		 	pwep =(NDIS_802_11_WEP	 *) rtw_malloc(wep_total_len);
			if(pwep == NULL){
				DBG_871X(" wpa_set_encryption: pwep allocate fail !!!\n");
				ret = -ENOMEM;
				goto exit;
			}

		 	_rtw_memset(pwep, 0, wep_total_len);

		 	pwep->KeyLength = wep_key_len;
			pwep->Length = wep_total_len;

			if(wep_key_len==13)
			{
				padapter->securitypriv.dot11PrivacyAlgrthm=_WEP104_;
				padapter->securitypriv.dot118021XGrpPrivacy=_WEP104_;
			}
		}
		else {		
			ret = -EINVAL;
			goto exit;
		}

		pwep->KeyIndex = wep_key_idx;
		pwep->KeyIndex |= 0x80000000;

		_rtw_memcpy(pwep->KeyMaterial,  (void *)sme->key, pwep->KeyLength);

		if(rtw_set_802_11_add_wep(padapter, pwep) == (u8)_FAIL)
		{
			ret = -EOPNOTSUPP ;
		}

		if (pwep) {
			rtw_mfree((u8 *)pwep,wep_total_len);		
		}

		if(ret < 0)
			goto exit;
	}

	ret = rtw_cfg80211_set_cipher(psecuritypriv, sme->crypto.cipher_group, _FALSE);
	if (ret < 0)
		return ret;

	if (sme->crypto.n_akm_suites) {
		ret = rtw_cfg80211_set_key_mgt(psecuritypriv, sme->crypto.akm_suites[0]);
		if (ret < 0)
			goto exit;
	}

#ifdef CONFIG_WAPI_SUPPORT
      if(sme->crypto.akm_suites[0] ==WLAN_AKM_SUITE_WAPI_PSK){
		padapter->wapiInfo.bWapiPSK = true;
	}
	else if(sme->crypto.akm_suites[0] ==WLAN_AKM_SUITE_WAPI_CERT){
	      padapter->wapiInfo.bWapiPSK = false;
	}
#endif

	authmode = psecuritypriv->ndisauthtype;
	rtw_set_802_11_authentication_mode(padapter, authmode);

	//rtw_set_802_11_encryption_mode(padapter, padapter->securitypriv.ndisencryptstatus);

	if (rtw_set_802_11_connect(padapter, sme->bssid, &ndis_ssid) == _FALSE) {
		ret = -1;
		goto exit;
	}

	DBG_8192C("set ssid:dot11AuthAlgrthm=%d, dot11PrivacyAlgrthm=%d, dot118021XGrpPrivacy=%d\n", psecuritypriv->dot11AuthAlgrthm, psecuritypriv->dot11PrivacyAlgrthm, psecuritypriv->dot118021XGrpPrivacy);
	
exit:

	rtw_ps_deny_cancel(padapter, PS_DENY_JOIN);

	DBG_8192C("<=%s, ret %d\n",__FUNCTION__, ret);

	padapter->mlmepriv.not_indic_disco = _FALSE;

	return ret;
}

static int cfg80211_rtw_disconnect(struct wiphy *wiphy, struct net_device *ndev,
				   u16 reason_code)
{
	_adapter *padapter = (_adapter *)rtw_netdev_priv(ndev);

	DBG_871X(FUNC_NDEV_FMT"\n", FUNC_NDEV_ARG(ndev));

	padapter->mlmepriv.not_indic_disco = _TRUE;

	rtw_set_to_roam(padapter, 0);

	//if(check_fwstate(&padapter->mlmepriv, _FW_LINKED))
	{
		rtw_scan_abort(padapter);
		LeaveAllPowerSaveMode(padapter);
		rtw_disassoc_cmd(padapter, 500, _FALSE);
		
		DBG_871X("%s...call rtw_indicate_disconnect\n", __FUNCTION__);
		
		rtw_indicate_disconnect(padapter);
		
		rtw_free_assoc_resources(padapter, 1);
		rtw_pwr_wakeup(padapter);		
	}

	padapter->mlmepriv.not_indic_disco = _FALSE;

	DBG_871X(FUNC_NDEV_FMT" return 0\n", FUNC_NDEV_ARG(ndev));
	return 0;
}

static int cfg80211_rtw_set_txpower(struct wiphy *wiphy,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,8,0))
	struct wireless_dev *wdev,
#endif
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,36)) || defined(COMPAT_KERNEL_RELEASE)
	enum nl80211_tx_power_setting type, int mbm)
#else
	enum tx_power_setting type, int dbm)
#endif
{
#if 0
	struct iwm_priv *iwm = wiphy_to_iwm(wiphy);
	int ret;

	switch (type) {
	case NL80211_TX_POWER_AUTOMATIC:
		return 0;
	case NL80211_TX_POWER_FIXED:
		if (mbm < 0 || (mbm % 100))
			return -EOPNOTSUPP;

		if (!test_bit(IWM_STATUS_READY, &iwm->status))
			return 0;

		ret = iwm_umac_set_config_fix(iwm, UMAC_PARAM_TBL_CFG_FIX,
					      CFG_TX_PWR_LIMIT_USR,
					      MBM_TO_DBM(mbm) * 2);
		if (ret < 0)
			return ret;

		return iwm_tx_power_trigger(iwm);
	default:
		IWM_ERR(iwm, "Unsupported power type: %d\n", type);
		return -EOPNOTSUPP;
	}
#endif
	DBG_8192C("%s\n", __func__);
	return 0;
}

static int cfg80211_rtw_get_txpower(struct wiphy *wiphy,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,8,0))
	struct wireless_dev *wdev,
#endif
	int *dbm)
{
	DBG_8192C("%s\n", __func__);

	*dbm = (12);
	
	return 0;
}

inline bool rtw_cfg80211_pwr_mgmt(_adapter *adapter)
{
	struct rtw_wdev_priv *rtw_wdev_priv = adapter_wdev_data(adapter);
	return rtw_wdev_priv->power_mgmt;
}

static int cfg80211_rtw_set_power_mgmt(struct wiphy *wiphy,
				       struct net_device *ndev,
				       bool enabled, int timeout)
{
	_adapter *padapter = (_adapter *)rtw_netdev_priv(ndev);
	struct rtw_wdev_priv *rtw_wdev_priv = adapter_wdev_data(padapter);
	
	DBG_871X(FUNC_NDEV_FMT" enabled:%u, timeout:%d\n", FUNC_NDEV_ARG(ndev),
		enabled, timeout);

	rtw_wdev_priv->power_mgmt = enabled;

	#ifdef CONFIG_LPS
	if (!enabled)
		LPS_Leave(padapter, "CFG80211_PWRMGMT");
	#endif

	return 0;
}

static int cfg80211_rtw_set_pmksa(struct wiphy *wiphy,
				  struct net_device *ndev,
				  struct cfg80211_pmksa *pmksa)
{
	u8	index,blInserted = _FALSE;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(ndev);
	struct security_priv	*psecuritypriv = &padapter->securitypriv;
	u8	strZeroMacAddress[ ETH_ALEN ] = { 0x00 };

	DBG_871X(FUNC_NDEV_FMT"\n", FUNC_NDEV_ARG(ndev));

	if ( _rtw_memcmp( pmksa->bssid, strZeroMacAddress, ETH_ALEN ) == _TRUE )
	{
		return -EINVAL;
	}

	blInserted = _FALSE;
	
	//overwrite PMKID
	for(index=0 ; index<NUM_PMKID_CACHE; index++)
	{
		if( _rtw_memcmp( psecuritypriv->PMKIDList[index].Bssid, pmksa->bssid, ETH_ALEN) ==_TRUE )
		{ // BSSID is matched, the same AP => rewrite with new PMKID.
			DBG_871X(FUNC_NDEV_FMT" BSSID exists in the PMKList.\n", FUNC_NDEV_ARG(ndev));

			_rtw_memcpy( psecuritypriv->PMKIDList[index].PMKID, pmksa->pmkid, WLAN_PMKID_LEN);
			psecuritypriv->PMKIDList[index].bUsed = _TRUE;
			psecuritypriv->PMKIDIndex = index+1;
			blInserted = _TRUE;
			break;
		}	
	}

	if(!blInserted)
	{
		// Find a new entry
		DBG_871X(FUNC_NDEV_FMT" Use the new entry index = %d for this PMKID.\n",
			FUNC_NDEV_ARG(ndev), psecuritypriv->PMKIDIndex );

		_rtw_memcpy(psecuritypriv->PMKIDList[psecuritypriv->PMKIDIndex].Bssid, pmksa->bssid, ETH_ALEN);
		_rtw_memcpy(psecuritypriv->PMKIDList[psecuritypriv->PMKIDIndex].PMKID, pmksa->pmkid, WLAN_PMKID_LEN);

		psecuritypriv->PMKIDList[psecuritypriv->PMKIDIndex].bUsed = _TRUE;
		psecuritypriv->PMKIDIndex++ ;
		if(psecuritypriv->PMKIDIndex==16)
		{
			psecuritypriv->PMKIDIndex =0;
		} 
	} 

	return 0;
}

static int cfg80211_rtw_del_pmksa(struct wiphy *wiphy,
				  struct net_device *ndev,
				  struct cfg80211_pmksa *pmksa)
{
	u8	index, bMatched = _FALSE;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(ndev);
	struct security_priv	*psecuritypriv = &padapter->securitypriv;

	DBG_871X(FUNC_NDEV_FMT"\n", FUNC_NDEV_ARG(ndev));

	for(index=0 ; index<NUM_PMKID_CACHE; index++)
	{
		if( _rtw_memcmp( psecuritypriv->PMKIDList[index].Bssid, pmksa->bssid, ETH_ALEN) ==_TRUE )
		{ // BSSID is matched, the same AP => Remove this PMKID information and reset it. 
			_rtw_memset( psecuritypriv->PMKIDList[index].Bssid, 0x00, ETH_ALEN );
			_rtw_memset( psecuritypriv->PMKIDList[index].PMKID, 0x00, WLAN_PMKID_LEN );
			psecuritypriv->PMKIDList[index].bUsed = _FALSE;
			bMatched = _TRUE;
			break;
		}	
	}

	if(_FALSE == bMatched)
	{
		DBG_871X(FUNC_NDEV_FMT" do not have matched BSSID\n"
			, FUNC_NDEV_ARG(ndev));
		return -EINVAL;
	}

	return 0;
}

static int cfg80211_rtw_flush_pmksa(struct wiphy *wiphy,
				    struct net_device *ndev)
{
	_adapter *padapter = (_adapter *)rtw_netdev_priv(ndev);
	struct security_priv	*psecuritypriv = &padapter->securitypriv;

	DBG_871X(FUNC_NDEV_FMT"\n", FUNC_NDEV_ARG(ndev));

	_rtw_memset( &psecuritypriv->PMKIDList[ 0 ], 0x00, sizeof( RT_PMKID_LIST ) * NUM_PMKID_CACHE );
	psecuritypriv->PMKIDIndex = 0;

	return 0;
}

#ifdef CONFIG_AP_MODE
void rtw_cfg80211_indicate_sta_assoc(_adapter *padapter, u8 *pmgmt_frame, uint frame_len)
{
	s32 freq;
	int channel;
	struct wireless_dev *pwdev = padapter->rtw_wdev;
	struct mlme_ext_priv *pmlmeext = &(padapter->mlmeextpriv);	
	struct net_device *ndev = padapter->pnetdev;

	DBG_871X(FUNC_ADPT_FMT"\n", FUNC_ADPT_ARG(padapter));

#if defined(RTW_USE_CFG80211_STA_EVENT) || defined(COMPAT_KERNEL_RELEASE)
	{
		struct station_info sinfo;
		u8 ie_offset;
		if (GetFrameSubType(pmgmt_frame) == WIFI_ASSOCREQ)
			ie_offset = _ASOCREQ_IE_OFFSET_;
		else // WIFI_REASSOCREQ
			ie_offset = _REASOCREQ_IE_OFFSET_;
	
		sinfo.filled = 0;
		sinfo.filled = STATION_INFO_ASSOC_REQ_IES;
		sinfo.assoc_req_ies = pmgmt_frame + WLAN_HDR_A3_LEN + ie_offset;
		sinfo.assoc_req_ies_len = frame_len - WLAN_HDR_A3_LEN - ie_offset;
		cfg80211_new_sta(ndev, GetAddr2Ptr(pmgmt_frame), &sinfo, GFP_ATOMIC);
	}
#else /* defined(RTW_USE_CFG80211_STA_EVENT) */
	channel = pmlmeext->cur_channel;
	if (channel <= RTW_CH_MAX_2G_CHANNEL)
		freq = rtw_ieee80211_channel_to_frequency(channel, IEEE80211_BAND_2GHZ);
	else
		freq = rtw_ieee80211_channel_to_frequency(channel, IEEE80211_BAND_5GHZ);

	#ifdef COMPAT_KERNEL_RELEASE
	rtw_cfg80211_rx_mgmt(padapter, freq, 0, pmgmt_frame, frame_len, GFP_ATOMIC);
	#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)) && !defined(CONFIG_CFG80211_FORCE_COMPATIBLE_2_6_37_UNDER)
	rtw_cfg80211_rx_mgmt(padapter, freq, 0, pmgmt_frame, frame_len, GFP_ATOMIC);
	#else //COMPAT_KERNEL_RELEASE
	{
		//to avoid WARN_ON(wdev->iftype != NL80211_IFTYPE_STATION)  when calling cfg80211_send_rx_assoc()
		#ifndef CONFIG_PLATFORM_MSTAR
		pwdev->iftype = NL80211_IFTYPE_STATION;
		#endif //CONFIG_PLATFORM_MSTAR
		DBG_8192C("iftype=%d before call cfg80211_send_rx_assoc()\n", pwdev->iftype);
		rtw_cfg80211_send_rx_assoc(padapter, NULL, pmgmt_frame, frame_len);
		DBG_8192C("iftype=%d after call cfg80211_send_rx_assoc()\n", pwdev->iftype);
		pwdev->iftype = NL80211_IFTYPE_AP;
		//cfg80211_rx_action(padapter->pnetdev, freq, pmgmt_frame, frame_len, GFP_ATOMIC);
	}
	#endif //COMPAT_KERNEL_RELEASE
#endif /* defined(RTW_USE_CFG80211_STA_EVENT) */

}

void rtw_cfg80211_indicate_sta_disassoc(_adapter *padapter, unsigned char *da, unsigned short reason)
{
	s32 freq;
	int channel;
	u8 *pmgmt_frame;
	uint frame_len;
	struct rtw_ieee80211_hdr *pwlanhdr;
	unsigned short *fctrl;	
	u8 mgmt_buf[128] = {0};
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct net_device *ndev = padapter->pnetdev;
	
	DBG_871X(FUNC_ADPT_FMT"\n", FUNC_ADPT_ARG(padapter));

#if defined(RTW_USE_CFG80211_STA_EVENT) || defined(COMPAT_KERNEL_RELEASE)
	cfg80211_del_sta(ndev, da, GFP_ATOMIC);
#else /* defined(RTW_USE_CFG80211_STA_EVENT) */
	channel = pmlmeext->cur_channel;
	if (channel <= RTW_CH_MAX_2G_CHANNEL)
		freq = rtw_ieee80211_channel_to_frequency(channel, IEEE80211_BAND_2GHZ);
	else
		freq = rtw_ieee80211_channel_to_frequency(channel, IEEE80211_BAND_5GHZ);

	pmgmt_frame = mgmt_buf;	
	pwlanhdr = (struct rtw_ieee80211_hdr *)pmgmt_frame;

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	//_rtw_memcpy(pwlanhdr->addr1, da, ETH_ALEN);
	//_rtw_memcpy(pwlanhdr->addr2, myid(&(padapter->eeprompriv)), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr1, myid(&(padapter->eeprompriv)), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, da, ETH_ALEN);	
	_rtw_memcpy(pwlanhdr->addr3, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pmgmt_frame, WIFI_DEAUTH);

	pmgmt_frame += sizeof(struct rtw_ieee80211_hdr_3addr);
	frame_len = sizeof(struct rtw_ieee80211_hdr_3addr);

	reason = cpu_to_le16(reason);
	pmgmt_frame = rtw_set_fixed_ie(pmgmt_frame, _RSON_CODE_ , (unsigned char *)&reason, &frame_len);

	#ifdef COMPAT_KERNEL_RELEASE
	rtw_cfg80211_rx_mgmt(padapter, freq, 0, mgmt_buf, frame_len, GFP_ATOMIC);
	#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)) && !defined(CONFIG_CFG80211_FORCE_COMPATIBLE_2_6_37_UNDER)
	rtw_cfg80211_rx_mgmt(padapter, freq, 0, mgmt_buf, frame_len, GFP_ATOMIC);
	#else //COMPAT_KERNEL_RELEASE
	cfg80211_send_disassoc(padapter->pnetdev, mgmt_buf, frame_len);	
	//cfg80211_rx_action(padapter->pnetdev, freq, mgmt_buf, frame_len, GFP_ATOMIC);
	#endif //COMPAT_KERNEL_RELEASE
#endif /* defined(RTW_USE_CFG80211_STA_EVENT) */
}

static int rtw_cfg80211_monitor_if_open(struct net_device *ndev)
{
	int ret = 0;

	DBG_8192C("%s\n", __func__);
	
	return ret;
}

static int rtw_cfg80211_monitor_if_close(struct net_device *ndev)
{
	int ret = 0;

	DBG_8192C("%s\n", __func__);
	
	return ret;
}

static int rtw_cfg80211_monitor_if_xmit_entry(struct sk_buff *skb, struct net_device *ndev)
{	
	int ret = 0;
	int rtap_len;
	int qos_len = 0;
	int dot11_hdr_len = 24;
	int snap_len = 6;
	unsigned char *pdata;
	u16 frame_ctl;
	unsigned char src_mac_addr[6];
	unsigned char dst_mac_addr[6];
	struct ieee80211_hdr *dot11_hdr;
	struct ieee80211_radiotap_header *rtap_hdr;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(ndev);
	
	DBG_871X(FUNC_NDEV_FMT"\n", FUNC_NDEV_ARG(ndev));

	if (skb)
		rtw_mstat_update(MSTAT_TYPE_SKB, MSTAT_ALLOC_SUCCESS, skb->truesize);

	if (unlikely(skb->len < sizeof(struct ieee80211_radiotap_header)))
		goto fail;

	rtap_hdr = (struct ieee80211_radiotap_header *)skb->data;
	if (unlikely(rtap_hdr->it_version))
		goto fail;

	rtap_len = ieee80211_get_radiotap_len(skb->data);
	if (unlikely(skb->len < rtap_len))
		goto fail;

	if(rtap_len != 14)
	{
		DBG_8192C("radiotap len (should be 14): %d\n", rtap_len);
		goto fail;
	}	

	/* Skip the ratio tap header */
	skb_pull(skb, rtap_len);

	dot11_hdr = (struct ieee80211_hdr *)skb->data;
	frame_ctl = le16_to_cpu(dot11_hdr->frame_control);
	/* Check if the QoS bit is set */
	if ((frame_ctl & RTW_IEEE80211_FCTL_FTYPE) == RTW_IEEE80211_FTYPE_DATA) {
		/* Check if this ia a Wireless Distribution System (WDS) frame
		 * which has 4 MAC addresses
		 */
		if (dot11_hdr->frame_control & 0x0080)
			qos_len = 2;
		if ((dot11_hdr->frame_control & 0x0300) == 0x0300)
			dot11_hdr_len += 6;

		memcpy(dst_mac_addr, dot11_hdr->addr1, sizeof(dst_mac_addr));
		memcpy(src_mac_addr, dot11_hdr->addr2, sizeof(src_mac_addr));

		/* Skip the 802.11 header, QoS (if any) and SNAP, but leave spaces for
		 * for two MAC addresses
		 */
		skb_pull(skb, dot11_hdr_len + qos_len + snap_len - sizeof(src_mac_addr) * 2);
		pdata = (unsigned char*)skb->data;
		memcpy(pdata, dst_mac_addr, sizeof(dst_mac_addr));
		memcpy(pdata + sizeof(dst_mac_addr), src_mac_addr, sizeof(src_mac_addr));

		DBG_8192C("should be eapol packet\n");

		/* Use the real net device to transmit the packet */
		ret = _rtw_xmit_entry(skb, padapter->pnetdev);

		return ret;

	}
	else if ((frame_ctl & (RTW_IEEE80211_FCTL_FTYPE|RTW_IEEE80211_FCTL_STYPE))
		== (RTW_IEEE80211_FTYPE_MGMT|RTW_IEEE80211_STYPE_ACTION)
	) 
	{
		//only for action frames
		struct xmit_frame		*pmgntframe;
		struct pkt_attrib	*pattrib;
		unsigned char	*pframe;	
		//u8 category, action, OUI_Subtype, dialogToken=0;
		//unsigned char	*frame_body;
		struct rtw_ieee80211_hdr *pwlanhdr;	
		struct xmit_priv	*pxmitpriv = &(padapter->xmitpriv);
		struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
		u8 *buf = skb->data;
		u32 len = skb->len;
		u8 category, action;
		int type = -1;

		if (rtw_action_frame_parse(buf, len, &category, &action) == _FALSE) {
			DBG_8192C(FUNC_NDEV_FMT" frame_control:0x%x\n", FUNC_NDEV_ARG(ndev),
				le16_to_cpu(((struct rtw_ieee80211_hdr_3addr *)buf)->frame_ctl));
			goto fail;
		}
		
		DBG_8192C("RTW_Tx:da="MAC_FMT" via "FUNC_NDEV_FMT"\n",
			MAC_ARG(GetAddr1Ptr(buf)), FUNC_NDEV_ARG(ndev));
		#ifdef CONFIG_P2P
		if((type = rtw_p2p_check_frames(padapter, buf, len, _TRUE)) >= 0)
			goto dump;
		#endif
		if (category == RTW_WLAN_CATEGORY_PUBLIC)
			DBG_871X("RTW_Tx:%s\n", action_public_str(action));
		else
			DBG_871X("RTW_Tx:category(%u), action(%u)\n", category, action);

dump:
		//starting alloc mgmt frame to dump it
		if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
		{			
			goto fail;
		}

		//update attribute
		pattrib = &pmgntframe->attrib;
		update_mgntframe_attrib(padapter, pattrib);
		pattrib->retry_ctrl = _FALSE;

		_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

		pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;

		_rtw_memcpy(pframe, (void*)buf, len);
		#ifdef CONFIG_WFD
		if (type >= 0)
		{
			struct wifi_display_info		*pwfd_info;
			
			pwfd_info = padapter->wdinfo.wfd_info;
			
			if ( _TRUE == pwfd_info->wfd_enable )
			{
				rtw_append_wfd_ie( padapter, pframe, &len );
			}
		}
		#endif // CONFIG_WFD
		pattrib->pktlen = len;	
	
		pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;
		//update seq number
		pmlmeext->mgnt_seq = GetSequence(pwlanhdr);
		pattrib->seqnum = pmlmeext->mgnt_seq;
		pmlmeext->mgnt_seq++;

	
		pattrib->last_txcmdsz = pattrib->pktlen;
	
		dump_mgntframe(padapter, pmgntframe);
		
	}
	else
	{
		DBG_8192C("frame_ctl=0x%x\n", frame_ctl & (RTW_IEEE80211_FCTL_FTYPE|RTW_IEEE80211_FCTL_STYPE));
	}

	
fail:
	
	rtw_skb_free(skb);

	return 0;
	
}

static void rtw_cfg80211_monitor_if_set_multicast_list(struct net_device *ndev)
{
	DBG_8192C("%s\n", __func__);
}

static int rtw_cfg80211_monitor_if_set_mac_address(struct net_device *ndev, void *addr)
{
	int ret = 0;
	
	DBG_8192C("%s\n", __func__);
	
	return ret;
}

#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,29))
static const struct net_device_ops rtw_cfg80211_monitor_if_ops = {
	.ndo_open = rtw_cfg80211_monitor_if_open,
       .ndo_stop = rtw_cfg80211_monitor_if_close,
       .ndo_start_xmit = rtw_cfg80211_monitor_if_xmit_entry,
       #if (LINUX_VERSION_CODE < KERNEL_VERSION(3,2,0))
       .ndo_set_multicast_list = rtw_cfg80211_monitor_if_set_multicast_list,
       #endif
       .ndo_set_mac_address = rtw_cfg80211_monitor_if_set_mac_address,       
};
#endif

static int rtw_cfg80211_add_monitor_if(_adapter *padapter, char *name, struct net_device **ndev)
{
	int ret = 0;
	struct net_device* mon_ndev = NULL;
	struct wireless_dev* mon_wdev = NULL;
	struct rtw_netdev_priv_indicator *pnpi;
	struct rtw_wdev_priv *pwdev_priv = adapter_wdev_data(padapter);
		
	if (!name ) {
		DBG_871X(FUNC_ADPT_FMT" without specific name\n", FUNC_ADPT_ARG(padapter));
		ret = -EINVAL;
		goto out;
	}

	if (pwdev_priv->pmon_ndev) {
		DBG_871X(FUNC_ADPT_FMT" monitor interface exist: "NDEV_FMT"\n",
			FUNC_ADPT_ARG(padapter), NDEV_ARG(pwdev_priv->pmon_ndev));
		ret = -EBUSY;
		goto out;
	}

	mon_ndev = alloc_etherdev(sizeof(struct rtw_netdev_priv_indicator));
	if (!mon_ndev) {
		DBG_871X(FUNC_ADPT_FMT" allocate ndev fail\n", FUNC_ADPT_ARG(padapter));
		ret = -ENOMEM;
		goto out;
	}

	mon_ndev->type = ARPHRD_IEEE80211_RADIOTAP;
	strncpy(mon_ndev->name, name, IFNAMSIZ);
	mon_ndev->name[IFNAMSIZ - 1] = 0;
	mon_ndev->destructor = rtw_ndev_destructor;
	
#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,29))
	mon_ndev->netdev_ops = &rtw_cfg80211_monitor_if_ops;
#else
	mon_ndev->open = rtw_cfg80211_monitor_if_open;
	mon_ndev->stop = rtw_cfg80211_monitor_if_close;
	mon_ndev->hard_start_xmit = rtw_cfg80211_monitor_if_xmit_entry;
	mon_ndev->set_mac_address = rtw_cfg80211_monitor_if_set_mac_address;
#endif

	pnpi = netdev_priv(mon_ndev);
	pnpi->priv = padapter;
	pnpi->sizeof_priv = sizeof(_adapter);

	/*  wdev */
	mon_wdev = (struct wireless_dev *)rtw_zmalloc(sizeof(struct wireless_dev));
	if (!mon_wdev) {
		DBG_871X(FUNC_ADPT_FMT" allocate mon_wdev fail\n", FUNC_ADPT_ARG(padapter));
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
	_rtw_memcpy(pwdev_priv->ifname_mon, name, IFNAMSIZ+1);

out:
	if (ret && mon_wdev) {
		rtw_mfree((u8*)mon_wdev, sizeof(struct wireless_dev));
		mon_wdev = NULL;
	}

	if (ret && mon_ndev) {
		free_netdev(mon_ndev);
		*ndev = mon_ndev = NULL;
	}

	return ret;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0))
static struct wireless_dev *
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,38)) || defined(COMPAT_KERNEL_RELEASE)
static struct net_device *
#else
static int
#endif
	cfg80211_rtw_add_virtual_intf(
		struct wiphy *wiphy,
	#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,7,0))
		const char *name,
	#else
		char *name,
	#endif
		enum nl80211_iftype type, u32 *flags, struct vif_params *params)
{
	int ret = 0;
	struct net_device* ndev = NULL;
	_adapter *padapter = wiphy_to_adapter(wiphy);

	DBG_871X(FUNC_ADPT_FMT " wiphy:%s, name:%s, type:%d\n",
		FUNC_ADPT_ARG(padapter), wiphy_name(wiphy), name, type);

	switch (type) {
	case NL80211_IFTYPE_ADHOC:
	case NL80211_IFTYPE_AP_VLAN:
	case NL80211_IFTYPE_WDS:
	case NL80211_IFTYPE_MESH_POINT:
		ret = -ENODEV;
		break;
	case NL80211_IFTYPE_MONITOR:
		ret = rtw_cfg80211_add_monitor_if(padapter, (char *)name, &ndev);
		break;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)) || defined(COMPAT_KERNEL_RELEASE)
	case NL80211_IFTYPE_P2P_CLIENT:
#endif
	case NL80211_IFTYPE_STATION:
		ret = -ENODEV;
		break;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)) || defined(COMPAT_KERNEL_RELEASE)
	case NL80211_IFTYPE_P2P_GO:
#endif
	case NL80211_IFTYPE_AP:
		ret = -ENODEV;
		break;
	default:
		ret = -ENODEV;
		DBG_871X("Unsupported interface type\n");
		break;
	}

	DBG_871X(FUNC_ADPT_FMT" ndev:%p, ret:%d\n", FUNC_ADPT_ARG(padapter), ndev, ret);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0))
	return ndev ? ndev->ieee80211_ptr : ERR_PTR(ret);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,38)) || defined(COMPAT_KERNEL_RELEASE)
	return ndev ? ndev : ERR_PTR(ret);
#else
	return ret;
#endif
}

static int cfg80211_rtw_del_virtual_intf(struct wiphy *wiphy,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0))
	struct wireless_dev *wdev
#else
	struct net_device *ndev
#endif
)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0))
	struct net_device *ndev = wdev_to_ndev(wdev);
#endif
	int ret = 0;
	_adapter *adapter;
	struct rtw_wdev_priv *pwdev_priv;

	if (!ndev) {
		ret = -EINVAL;
		goto exit;
	}

	adapter = (_adapter *)rtw_netdev_priv(ndev);
	pwdev_priv = adapter_wdev_data(adapter);

	unregister_netdevice(ndev);

	if (ndev == pwdev_priv->pmon_ndev) {
		pwdev_priv->pmon_ndev = NULL;
		pwdev_priv->ifname_mon[0] = '\0';
		DBG_871X(FUNC_NDEV_FMT" remove monitor interface\n", FUNC_NDEV_ARG(ndev));
	}

exit:
	return ret;
}

static int rtw_add_beacon(_adapter *adapter, const u8 *head, size_t head_len, const u8 *tail, size_t tail_len)
{
	int ret=0;
	u8 *pbuf = NULL;
	uint len, wps_ielen=0;	
	uint p2p_ielen=0;
	u8 *p2p_ie;
	u8 got_p2p_ie = _FALSE;
	struct mlme_priv *pmlmepriv = &(adapter->mlmepriv);
	//struct sta_priv *pstapriv = &padapter->stapriv;
	

	DBG_8192C("%s beacon_head_len=%zu, beacon_tail_len=%zu\n", __FUNCTION__, head_len, tail_len);

	
	if(check_fwstate(pmlmepriv, WIFI_AP_STATE) != _TRUE)
		return -EINVAL;

	if(head_len<24)
		return -EINVAL;
	

	pbuf = rtw_zmalloc(head_len+tail_len);
	if(!pbuf)
		return -ENOMEM;
	

	//_rtw_memcpy(&pstapriv->max_num_sta, param->u.bcn_ie.reserved, 2);

	//if((pstapriv->max_num_sta>NUM_STA) || (pstapriv->max_num_sta<=0))
	//	pstapriv->max_num_sta = NUM_STA;

	
	_rtw_memcpy(pbuf, (void *)head+24, head_len-24);// 24=beacon header len.
	_rtw_memcpy(pbuf+head_len-24, (void *)tail, tail_len);

	len = head_len+tail_len-24;

	//check wps ie if inclued
	if(rtw_get_wps_ie(pbuf+_FIXED_IE_LENGTH_, len-_FIXED_IE_LENGTH_, NULL, &wps_ielen))
		DBG_8192C("add bcn, wps_ielen=%d\n", wps_ielen);

#ifdef CONFIG_P2P
	if( adapter->wdinfo.driver_interface == DRIVER_CFG80211 )
	{
		//check p2p if enable
		if(rtw_get_p2p_ie(pbuf+_FIXED_IE_LENGTH_, len-_FIXED_IE_LENGTH_, NULL, &p2p_ielen))
		{		
			struct mlme_ext_priv *pmlmeext = &adapter->mlmeextpriv;
			struct wifidirect_info *pwdinfo= &(adapter->wdinfo);

			DBG_8192C("got p2p_ie, len=%d\n", p2p_ielen);
			
			got_p2p_ie = _TRUE;	
		
			if(rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE))
			{			
				DBG_8192C("Enable P2P function for the first time\n");
				rtw_p2p_enable(adapter, P2P_ROLE_GO);
				adapter_wdev_data(adapter)->p2p_enabled = _TRUE;
				
				adapter->stapriv.expire_to = 3; // 3x2 = 6 sec in p2p mode
			}
			else
			{
				DBG_8192C("enter GO Mode, p2p_ielen=%d\n", p2p_ielen);

				rtw_p2p_set_role(pwdinfo, P2P_ROLE_GO);
				rtw_p2p_set_state(pwdinfo, P2P_STATE_GONEGO_OK);
				pwdinfo->intent = 15;		
			}
		}
	}
#endif // CONFIG_P2P

	/* pbss_network->IEs will not include p2p_ie, wfd ie */
	rtw_ies_remove_ie(pbuf, &len, _BEACON_IE_OFFSET_, _VENDOR_SPECIFIC_IE_, P2P_OUI, 4);
	rtw_ies_remove_ie(pbuf, &len, _BEACON_IE_OFFSET_, _VENDOR_SPECIFIC_IE_, WFD_OUI, 4);

	if (rtw_check_beacon_data(adapter, pbuf,  len) == _SUCCESS) 
	{
#ifdef  CONFIG_P2P		
		//check p2p if enable
		if(got_p2p_ie == _TRUE)
		{
			struct mlme_ext_priv *pmlmeext = &adapter->mlmeextpriv;
			struct wifidirect_info *pwdinfo= &(adapter->wdinfo);
			pwdinfo->operating_channel = pmlmeext->cur_channel;			
		}
#endif //CONFIG_P2P
		ret = 0;
	}	
	else
	{
		ret = -EINVAL;
	}	
	

	rtw_mfree(pbuf, head_len+tail_len);	
	
	return ret;	
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,4,0)) && !defined(COMPAT_KERNEL_RELEASE)
static int cfg80211_rtw_add_beacon(struct wiphy *wiphy, struct net_device *ndev,
			      struct beacon_parameters *info)
{
	int ret=0;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(ndev);

	DBG_871X(FUNC_NDEV_FMT"\n", FUNC_NDEV_ARG(ndev));
	ret = rtw_add_beacon(adapter, info->head, info->head_len, info->tail, info->tail_len);

	return ret;
}

static int cfg80211_rtw_set_beacon(struct wiphy *wiphy, struct net_device *ndev,
			      struct beacon_parameters *info)
{
	_adapter *adapter = (_adapter *)rtw_netdev_priv(ndev);
	struct mlme_ext_priv *pmlmeext = &(adapter->mlmeextpriv);
	
	DBG_871X(FUNC_NDEV_FMT"\n", FUNC_NDEV_ARG(ndev));

	pmlmeext->bstart_bss = _TRUE;

	cfg80211_rtw_add_beacon(wiphy, ndev, info);
	
	return 0;
}

static int	cfg80211_rtw_del_beacon(struct wiphy *wiphy, struct net_device *ndev)
{
	DBG_871X(FUNC_NDEV_FMT"\n", FUNC_NDEV_ARG(ndev));
	
	return 0;
}
#else
static int cfg80211_rtw_start_ap(struct wiphy *wiphy, struct net_device *ndev,
								struct cfg80211_ap_settings *settings)
{
	int ret = 0;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(ndev);
	
	DBG_871X(FUNC_NDEV_FMT" hidden_ssid:%d, auth_type:%d\n", FUNC_NDEV_ARG(ndev),
		settings->hidden_ssid, settings->auth_type);

	ret = rtw_add_beacon(adapter, settings->beacon.head, settings->beacon.head_len,
		settings->beacon.tail, settings->beacon.tail_len);

	adapter->mlmeextpriv.mlmext_info.hidden_ssid_mode = settings->hidden_ssid;
	
	if (settings->ssid && settings->ssid_len) {
		WLAN_BSSID_EX *pbss_network = &adapter->mlmepriv.cur_network.network;
		WLAN_BSSID_EX *pbss_network_ext = &adapter->mlmeextpriv.mlmext_info.network;

		if(0)
		DBG_871X(FUNC_ADPT_FMT" ssid:(%s,%zu), from ie:(%s,%d)\n", FUNC_ADPT_ARG(adapter),
			settings->ssid, settings->ssid_len,
			pbss_network->Ssid.Ssid, pbss_network->Ssid.SsidLength);

		_rtw_memcpy(pbss_network->Ssid.Ssid, (void *)settings->ssid, settings->ssid_len);
		pbss_network->Ssid.SsidLength = settings->ssid_len;
		_rtw_memcpy(pbss_network_ext->Ssid.Ssid, (void *)settings->ssid, settings->ssid_len);
		pbss_network_ext->Ssid.SsidLength = settings->ssid_len;

		if(0)
		DBG_871X(FUNC_ADPT_FMT" after ssid:(%s,%d), (%s,%d)\n", FUNC_ADPT_ARG(adapter),
			pbss_network->Ssid.Ssid, pbss_network->Ssid.SsidLength,
			pbss_network_ext->Ssid.Ssid, pbss_network_ext->Ssid.SsidLength);
	}

	return ret;
}

static int cfg80211_rtw_change_beacon(struct wiphy *wiphy, struct net_device *ndev,
                                struct cfg80211_beacon_data *info)
{
	int ret = 0;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(ndev);

	DBG_871X(FUNC_NDEV_FMT"\n", FUNC_NDEV_ARG(ndev));

	ret = rtw_add_beacon(adapter, info->head, info->head_len, info->tail, info->tail_len);

	return ret;
}

static int cfg80211_rtw_stop_ap(struct wiphy *wiphy, struct net_device *ndev)
{
	DBG_871X(FUNC_NDEV_FMT"\n", FUNC_NDEV_ARG(ndev));
	return 0;
}

#endif //(LINUX_VERSION_CODE < KERNEL_VERSION(3,4,0))

static int	cfg80211_rtw_add_station(struct wiphy *wiphy, struct net_device *ndev,
			       u8 *mac, struct station_parameters *params)
{
	DBG_871X(FUNC_NDEV_FMT"\n", FUNC_NDEV_ARG(ndev));
	
	return 0;
}

static int	cfg80211_rtw_del_station(struct wiphy *wiphy, struct net_device *ndev,
			       u8 *mac)
{
	int ret=0;	
	_irqL irqL;
	_list	*phead, *plist;
	u8 updated = _FALSE;
	struct sta_info *psta = NULL;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(ndev);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct sta_priv *pstapriv = &padapter->stapriv;

	DBG_871X("+"FUNC_NDEV_FMT"\n", FUNC_NDEV_ARG(ndev));

	if(check_fwstate(pmlmepriv, (_FW_LINKED|WIFI_AP_STATE)) != _TRUE)		
	{
		DBG_8192C("%s, fw_state != FW_LINKED|WIFI_AP_STATE\n", __func__);
		return -EINVAL;		
	}


	if(!mac)
	{
		DBG_8192C("flush all sta, and cam_entry\n");

		flush_all_cam_entry(padapter);	//clear CAM

		ret = rtw_sta_flush(padapter);
		
		return ret;
	}	


	DBG_8192C("free sta macaddr =" MAC_FMT "\n", MAC_ARG(mac));

	if (mac[0] == 0xff && mac[1] == 0xff &&
	    mac[2] == 0xff && mac[3] == 0xff &&
	    mac[4] == 0xff && mac[5] == 0xff) 
	{
		return -EINVAL;	
	}


	_enter_critical_bh(&pstapriv->asoc_list_lock, &irqL);
	
	phead = &pstapriv->asoc_list;
	plist = get_next(phead);

	//check asoc_queue
	while ((rtw_end_of_queue_search(phead, plist)) == _FALSE)	
	{
		psta = LIST_CONTAINOR(plist, struct sta_info, asoc_list);
		
		plist = get_next(plist);	
	
		if(_rtw_memcmp(mac, psta->hwaddr, ETH_ALEN))		
		{
			if(psta->dot8021xalg == 1 && psta->bpairwise_key_installed == _FALSE)
			{
				DBG_8192C("%s, sta's dot8021xalg = 1 and key_installed = _FALSE\n", __func__);
			}
			else
			{
				DBG_8192C("free psta=%p, aid=%d\n", psta, psta->aid);

				rtw_list_delete(&psta->asoc_list);
				pstapriv->asoc_list_cnt--;

				//_exit_critical_bh(&pstapriv->asoc_list_lock, &irqL);
				updated = ap_free_sta(padapter, psta, _TRUE, WLAN_REASON_DEAUTH_LEAVING);
				//_enter_critical_bh(&pstapriv->asoc_list_lock, &irqL);

				psta = NULL;

				break;
			}		
					
		}
		
	}

	_exit_critical_bh(&pstapriv->asoc_list_lock, &irqL);

	associated_clients_update(padapter, updated);

	DBG_871X("-"FUNC_NDEV_FMT"\n", FUNC_NDEV_ARG(ndev));
	
	return ret;	

}

static int	cfg80211_rtw_change_station(struct wiphy *wiphy, struct net_device *ndev,
				  u8 *mac, struct station_parameters *params)
{
	DBG_871X(FUNC_NDEV_FMT"\n", FUNC_NDEV_ARG(ndev));
	
	return 0;
}

static int	cfg80211_rtw_dump_station(struct wiphy *wiphy, struct net_device *ndev,
			       int idx, u8 *mac, struct station_info *sinfo)
{
	DBG_871X(FUNC_NDEV_FMT"\n", FUNC_NDEV_ARG(ndev));

	//TODO: dump scanned queue

	return -ENOENT;
}

static int	cfg80211_rtw_change_bss(struct wiphy *wiphy, struct net_device *ndev,
			      struct bss_parameters *params)
{
	u8 i;

	DBG_871X(FUNC_NDEV_FMT"\n", FUNC_NDEV_ARG(ndev));
/*
	DBG_8192C("use_cts_prot=%d\n", params->use_cts_prot);
	DBG_8192C("use_short_preamble=%d\n", params->use_short_preamble);
	DBG_8192C("use_short_slot_time=%d\n", params->use_short_slot_time);
	DBG_8192C("ap_isolate=%d\n", params->ap_isolate);

	DBG_8192C("basic_rates_len=%d\n", params->basic_rates_len);
	for(i=0; i<params->basic_rates_len; i++)
	{		
		DBG_8192C("basic_rates=%d\n", params->basic_rates[i]);
		
	}	
*/	
	return 0;
	
}

static int	cfg80211_rtw_set_channel(struct wiphy *wiphy
	#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,35))
	, struct net_device *ndev
	#endif
	, struct ieee80211_channel *chan, enum nl80211_channel_type channel_type)
{
	#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,35))
	DBG_871X(FUNC_NDEV_FMT"\n", FUNC_NDEV_ARG(ndev));
	#endif
	
	return 0;
}

static int	cfg80211_rtw_auth(struct wiphy *wiphy, struct net_device *ndev,
			struct cfg80211_auth_request *req)
{
	DBG_871X(FUNC_NDEV_FMT"\n", FUNC_NDEV_ARG(ndev));
	
	return 0;
}

static int	cfg80211_rtw_assoc(struct wiphy *wiphy, struct net_device *ndev,
			 struct cfg80211_assoc_request *req)
{
	DBG_871X(FUNC_NDEV_FMT"\n", FUNC_NDEV_ARG(ndev));
	
	return 0;
}
#endif //CONFIG_AP_MODE

void rtw_cfg80211_rx_action_p2p(_adapter *padapter, u8 *pmgmt_frame, uint frame_len)
{
	int type;
	s32 freq;
	int channel;
	struct mlme_ext_priv *pmlmeext = &(padapter->mlmeextpriv);
	u8 category, action;

	channel = rtw_get_oper_ch(padapter);

	DBG_8192C("RTW_Rx:cur_ch=%d\n", channel);
	#ifdef CONFIG_P2P
	type = rtw_p2p_check_frames(padapter, pmgmt_frame, frame_len, _FALSE);
	if (type >= 0)
		goto indicate;
	#endif
	rtw_action_frame_parse(pmgmt_frame, frame_len, &category, &action);
	DBG_871X("RTW_Rx:category(%u), action(%u)\n", category, action);

indicate:
	if (channel <= RTW_CH_MAX_2G_CHANNEL)
		freq = rtw_ieee80211_channel_to_frequency(channel, IEEE80211_BAND_2GHZ);
	else
		freq = rtw_ieee80211_channel_to_frequency(channel, IEEE80211_BAND_5GHZ);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)) || defined(COMPAT_KERNEL_RELEASE)
	rtw_cfg80211_rx_mgmt(padapter, freq, 0, pmgmt_frame, frame_len, GFP_ATOMIC);
#else
	cfg80211_rx_action(padapter->pnetdev, freq, pmgmt_frame, frame_len, GFP_ATOMIC);
#endif
}

void rtw_cfg80211_rx_p2p_action_public(_adapter *padapter, u8 *pmgmt_frame, uint frame_len)
{
	int type;
	s32 freq;
	int channel;
	struct mlme_ext_priv *pmlmeext = &(padapter->mlmeextpriv);
	u8 category, action;

	channel = rtw_get_oper_ch(padapter);

	DBG_8192C("RTW_Rx:cur_ch=%d\n", channel);
	#ifdef CONFIG_P2P
	type = rtw_p2p_check_frames(padapter, pmgmt_frame, frame_len, _FALSE);
	if (type >= 0) {
		switch (type) {
		case P2P_GO_NEGO_CONF:
		case P2P_PROVISION_DISC_RESP:
		case P2P_INVIT_RESP:
			rtw_set_scan_deny(padapter, 2000);			
			rtw_clear_scan_deny(padapter);
		}
		goto indicate;
	}
	#endif
	rtw_action_frame_parse(pmgmt_frame, frame_len, &category, &action);
	DBG_871X("RTW_Rx:category(%u), action(%u)\n", category, action);

indicate:
	if (channel <= RTW_CH_MAX_2G_CHANNEL)
		freq = rtw_ieee80211_channel_to_frequency(channel, IEEE80211_BAND_2GHZ);
	else
		freq = rtw_ieee80211_channel_to_frequency(channel, IEEE80211_BAND_5GHZ);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)) || defined(COMPAT_KERNEL_RELEASE)
	rtw_cfg80211_rx_mgmt(padapter, freq, 0, pmgmt_frame, frame_len, GFP_ATOMIC);
#else
	cfg80211_rx_action(padapter->pnetdev, freq, pmgmt_frame, frame_len, GFP_ATOMIC);
#endif
}

void rtw_cfg80211_rx_action(_adapter *adapter, u8 *frame, uint frame_len, const char*msg)
{
	s32 freq;
	int channel;
	struct mlme_ext_priv *pmlmeext = &(adapter->mlmeextpriv);	
	struct rtw_wdev_priv *pwdev_priv = adapter_wdev_data(adapter);
	u8 category, action;

	channel = rtw_get_oper_ch(adapter);

	rtw_action_frame_parse(frame, frame_len, &category, &action);

	DBG_8192C("RTW_Rx:cur_ch=%d\n", channel);
	if (msg)
		DBG_871X("RTW_Rx:%s\n", msg);
	else
		DBG_871X("RTW_Rx:category(%u), action(%u)\n", category, action);

	if (channel <= RTW_CH_MAX_2G_CHANNEL)
		freq = rtw_ieee80211_channel_to_frequency(channel, IEEE80211_BAND_2GHZ);
	else
		freq = rtw_ieee80211_channel_to_frequency(channel, IEEE80211_BAND_5GHZ);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)) || defined(COMPAT_KERNEL_RELEASE)
	rtw_cfg80211_rx_mgmt(adapter, freq, 0, frame, frame_len, GFP_ATOMIC);
#else
	cfg80211_rx_action(adapter->pnetdev, freq, frame, frame_len, GFP_ATOMIC);
#endif

}

#ifdef CONFIG_P2P
void rtw_cfg80211_issue_p2p_provision_request(_adapter *padapter, const u8 *buf, size_t len)
{
	u16	wps_devicepassword_id = 0x0000;
	uint	wps_devicepassword_id_len = 0;
	u8			wpsie[ 255 ] = { 0x00 }, p2p_ie[ 255 ] = { 0x00 };
	uint			p2p_ielen = 0;
	uint			wpsielen = 0;
	u32	devinfo_contentlen = 0;
	u8	devinfo_content[64] = { 0x00 };
	u16	capability = 0;
	uint capability_len = 0;
	
	unsigned char category = RTW_WLAN_CATEGORY_PUBLIC;
	u8			action = P2P_PUB_ACTION_ACTION;
	u8			dialogToken = 1;
	u32			p2poui = cpu_to_be32(P2POUI);
	u8			oui_subtype = P2P_PROVISION_DISC_REQ;	
	u32			p2pielen = 0;
#ifdef CONFIG_WFD
	u32					wfdielen = 0;
#endif //CONFIG_WFD		
	
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	unsigned char					*pframe;
	struct rtw_ieee80211_hdr	*pwlanhdr;
	unsigned short				*fctrl;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	
	struct wifidirect_info *pwdinfo = &(padapter->wdinfo);
	u8 *frame_body = (unsigned char *)(buf + sizeof(struct rtw_ieee80211_hdr_3addr));
	size_t frame_body_len = len - sizeof(struct rtw_ieee80211_hdr_3addr);


	DBG_871X( "[%s] In\n", __FUNCTION__ );

	//prepare for building provision_request frame	
	_rtw_memcpy(pwdinfo->tx_prov_disc_info.peerIFAddr, GetAddr1Ptr(buf), ETH_ALEN);
	_rtw_memcpy(pwdinfo->tx_prov_disc_info.peerDevAddr, GetAddr1Ptr(buf), ETH_ALEN);
	
	pwdinfo->tx_prov_disc_info.wps_config_method_request = WPS_CM_PUSH_BUTTON;
		
	rtw_get_wps_ie( frame_body + _PUBLIC_ACTION_IE_OFFSET_, frame_body_len - _PUBLIC_ACTION_IE_OFFSET_, wpsie, &wpsielen);
	rtw_get_wps_attr_content( wpsie, wpsielen, WPS_ATTR_DEVICE_PWID, (u8*) &wps_devicepassword_id, &wps_devicepassword_id_len);
	wps_devicepassword_id = be16_to_cpu( wps_devicepassword_id );

	switch(wps_devicepassword_id) 
	{
		case WPS_DPID_PIN:
			pwdinfo->tx_prov_disc_info.wps_config_method_request = WPS_CM_LABEL;
			break;
		case WPS_DPID_USER_SPEC:
			pwdinfo->tx_prov_disc_info.wps_config_method_request = WPS_CM_DISPLYA;
			break;
		case WPS_DPID_MACHINE_SPEC:
			break;
		case WPS_DPID_REKEY:
			break;
		case WPS_DPID_PBC:
			pwdinfo->tx_prov_disc_info.wps_config_method_request = WPS_CM_PUSH_BUTTON;
			break;
		case WPS_DPID_REGISTRAR_SPEC:
			pwdinfo->tx_prov_disc_info.wps_config_method_request = WPS_CM_KEYPAD;
			break;
		default:
			break;
	}


	if ( rtw_get_p2p_ie( frame_body + _PUBLIC_ACTION_IE_OFFSET_, frame_body_len - _PUBLIC_ACTION_IE_OFFSET_, p2p_ie, &p2p_ielen ) )
	{	

		rtw_get_p2p_attr_content( p2p_ie, p2p_ielen, P2P_ATTR_DEVICE_INFO, devinfo_content, &devinfo_contentlen);					
		rtw_get_p2p_attr_content( p2p_ie, p2p_ielen, P2P_ATTR_CAPABILITY, (u8*)&capability, &capability_len);
						
	}


	//start to build provision_request frame	
	_rtw_memset(wpsie, 0, sizeof(wpsie));
	_rtw_memset(p2p_ie, 0, sizeof(p2p_ie));
	p2p_ielen = 0;	
	
	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		return;
	}

	
	//update attribute
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);

	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	_rtw_memcpy(pwlanhdr->addr1, pwdinfo->tx_prov_disc_info.peerDevAddr, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, myid(&(padapter->eeprompriv)), ETH_ALEN);	
	_rtw_memcpy(pwlanhdr->addr3, pwdinfo->tx_prov_disc_info.peerDevAddr, ETH_ALEN);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_ACTION);

	pframe += sizeof(struct rtw_ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);

	pframe = rtw_set_fixed_ie(pframe, 1, &(category), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(action), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 4, (unsigned char *) &(p2poui), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(oui_subtype), &(pattrib->pktlen));	
	pframe = rtw_set_fixed_ie(pframe, 1, &(dialogToken), &(pattrib->pktlen));		


	//build_prov_disc_request_p2p_ie	
	//	P2P OUI
	p2pielen = 0;
	p2p_ie[ p2pielen++ ] = 0x50;
	p2p_ie[ p2pielen++ ] = 0x6F;
	p2p_ie[ p2pielen++ ] = 0x9A;
	p2p_ie[ p2pielen++ ] = 0x09;	//	WFA P2P v1.0

	//	Commented by Albert 20110301
	//	According to the P2P Specification, the provision discovery request frame should contain 3 P2P attributes
	//	1. P2P Capability
	//	2. Device Info
	//	3. Group ID ( When joining an operating P2P Group )

	//	P2P Capability ATTR
	//	Type:	
	p2p_ie[ p2pielen++ ] = P2P_ATTR_CAPABILITY;

	//	Length:
	//*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x0002 );
	RTW_PUT_LE16(p2p_ie + p2pielen, 0x0002);
	p2pielen += 2;

	//	Value:
	//	Device Capability Bitmap, 1 byte
	//	Group Capability Bitmap, 1 byte
	_rtw_memcpy(p2p_ie + p2pielen, &capability, 2);
	p2pielen += 2;
	

	//	Device Info ATTR
	//	Type:
	p2p_ie[ p2pielen++ ] = P2P_ATTR_DEVICE_INFO;

	//	Length:
	//	21 -> P2P Device Address (6bytes) + Config Methods (2bytes) + Primary Device Type (8bytes) 
	//	+ NumofSecondDevType (1byte) + WPS Device Name ID field (2bytes) + WPS Device Name Len field (2bytes)
	//*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 21 + pwdinfo->device_name_len );
	RTW_PUT_LE16(p2p_ie + p2pielen, devinfo_contentlen);
	p2pielen += 2;

	//	Value:
	_rtw_memcpy(p2p_ie + p2pielen, devinfo_content, devinfo_contentlen);
	p2pielen += devinfo_contentlen;


	pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, p2pielen, (unsigned char *) p2p_ie, &p2p_ielen);			
	//p2pielen = build_prov_disc_request_p2p_ie( pwdinfo, pframe, NULL, 0, pwdinfo->tx_prov_disc_info.peerDevAddr);
	//pframe += p2pielen;
	pattrib->pktlen += p2p_ielen;

	wpsielen = 0;
	//	WPS OUI
	*(u32*) ( wpsie ) = cpu_to_be32( WPSOUI );
	wpsielen += 4;

	//	WPS version
	//	Type:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_VER1 );
	wpsielen += 2;

	//	Length:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( 0x0001 );
	wpsielen += 2;

	//	Value:
	wpsie[wpsielen++] = WPS_VERSION_1;	//	Version 1.0

	//	Config Method
	//	Type:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_CONF_METHOD );
	wpsielen += 2;

	//	Length:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( 0x0002 );
	wpsielen += 2;

	//	Value:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( pwdinfo->tx_prov_disc_info.wps_config_method_request );
	wpsielen += 2;

	pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, wpsielen, (unsigned char *) wpsie, &pattrib->pktlen );


#ifdef CONFIG_WFD
	wfdielen = build_provdisc_req_wfd_ie(pwdinfo, pframe);
	pframe += wfdielen;
	pattrib->pktlen += wfdielen;
#endif //CONFIG_WFD

	pattrib->last_txcmdsz = pattrib->pktlen;

	//dump_mgntframe(padapter, pmgntframe);
	if (dump_mgntframe_and_wait_ack(padapter, pmgntframe) != _SUCCESS)
		DBG_8192C("%s, ack to\n", __func__);

	//if(wps_devicepassword_id == WPS_DPID_REGISTRAR_SPEC)
	//{
	//	DBG_8192C("waiting for p2p peer key-in PIN CODE\n");
	//	rtw_msleep_os(15000); // 15 sec for key in PIN CODE, workaround for GS2 before issuing Nego Req.
	//}	

}

static s32 cfg80211_rtw_remain_on_channel(struct wiphy *wiphy,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0))
	struct wireless_dev *wdev,
#else
	struct net_device *ndev,
#endif
	struct ieee80211_channel * channel,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,8,0))
	enum nl80211_channel_type channel_type,
#endif
	unsigned int duration, u64 *cookie)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0))
	struct net_device *ndev = wdev_to_ndev(wdev);
#endif
	s32 err = 0;
	u8 remain_ch = (u8) ieee80211_frequency_to_channel(channel->center_freq);
	u8 ready_on_channel = _FALSE;
	_adapter *padapter;
	struct rtw_wdev_priv *pwdev_priv;
	struct mlme_ext_priv *pmlmeext;
	struct wifidirect_info *pwdinfo;
	struct cfg80211_wifidirect_info *pcfg80211_wdinfo;

	if (ndev == NULL) {
		return  -EINVAL;
	}

	padapter = (_adapter *)rtw_netdev_priv(ndev);
	pwdev_priv = adapter_wdev_data(padapter);
	pmlmeext = &padapter->mlmeextpriv;
	pwdinfo = &padapter->wdinfo;
	pcfg80211_wdinfo = &padapter->cfg80211_wdinfo;

	DBG_871X(FUNC_ADPT_FMT" ch:%u duration:%d\n", FUNC_ADPT_ARG(padapter), remain_ch, duration);

	if(pcfg80211_wdinfo->is_ro_ch == _TRUE)
	{
		DBG_8192C("%s, cancel ro ch timer\n", __func__);
		
		_cancel_timer_ex(&padapter->cfg80211_wdinfo.remain_on_ch_timer);

#ifdef CONFIG_CONCURRENT_MODE
                ATOMIC_SET(&pwdev_priv->ro_ch_to, 1);			
#endif //CONFIG_CONCURRENT_MODE	

		p2p_protocol_wk_hdl(padapter, P2P_RO_CH_WK);
	}

	pcfg80211_wdinfo->is_ro_ch = _TRUE;
	pcfg80211_wdinfo->last_ro_ch_time = rtw_get_current_time();

	if(_FAIL == rtw_pwr_wakeup(padapter)) {
		err = -EFAULT;
		goto exit;
	}

	_rtw_memcpy(&pcfg80211_wdinfo->remain_on_ch_channel, channel, sizeof(struct ieee80211_channel));
	#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,8,0))
	pcfg80211_wdinfo->remain_on_ch_type= channel_type;
	#endif
	pcfg80211_wdinfo->remain_on_ch_cookie= *cookie;

	rtw_scan_abort(padapter);
#ifdef CONFIG_CONCURRENT_MODE		
	if(rtw_buddy_adapter_up(padapter))	
		rtw_scan_abort(padapter->pbuddy_adapter);			
#endif //CONFIG_CONCURRENT_MODE

	if (check_fwstate(&padapter->mlmepriv, _FW_UNDER_LINKING|WIFI_UNDER_WPS) == _TRUE)
	{
		DBG_871X("mlme state:0x%x\n", get_fwstate(&padapter->mlmepriv));
		remain_ch = padapter->mlmeextpriv.cur_channel;
	}
#ifdef CONFIG_CONCURRENT_MODE
	if (check_buddy_fwstate(padapter, _FW_UNDER_LINKING|WIFI_UNDER_WPS) == _TRUE)
	{
		DBG_871X("buddy_intf's mlme state:0x%x\n", get_fwstate(&(padapter->pbuddy_adapter->mlmepriv)));
		remain_ch = padapter->pbuddy_adapter->mlmeextpriv.cur_channel;
	}
#endif /* CONFIG_CONCURRENT_MODE */

	//if(!rtw_p2p_chk_role(pwdinfo, P2P_ROLE_CLIENT) && !rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO))
	if(rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE))
	{
		rtw_p2p_enable(padapter, P2P_ROLE_DEVICE);
		adapter_wdev_data(padapter)->p2p_enabled = _TRUE;
		padapter->wdinfo.listen_channel = remain_ch;
	}
	else
	{
		rtw_p2p_set_pre_state(pwdinfo, rtw_p2p_state(pwdinfo));
#ifdef CONFIG_DEBUG_CFG80211		
		DBG_8192C("%s, role=%d, p2p_state=%d\n", __func__, rtw_p2p_role(pwdinfo), rtw_p2p_state(pwdinfo));
#endif
	}


	rtw_p2p_set_state(pwdinfo, P2P_STATE_LISTEN);
	
	
	if(duration < 400)
		duration = duration*3;//extend from exper.


#ifdef	CONFIG_CONCURRENT_MODE
	if(check_buddy_fwstate(padapter, _FW_LINKED) &&
		(duration<pwdinfo->ext_listen_interval)) 
	{
		duration = duration + 	pwdinfo->ext_listen_interval;
	}
#endif

	pcfg80211_wdinfo->restore_channel = rtw_get_oper_ch(padapter);

	if(rtw_ch_set_search_ch(pmlmeext->channel_set, remain_ch) >= 0) {
#ifdef	CONFIG_CONCURRENT_MODE
		if ( check_buddy_fwstate(padapter, _FW_LINKED) )
		{
			PADAPTER pbuddy_adapter = padapter->pbuddy_adapter;			
			struct mlme_ext_priv *pbuddy_mlmeext = &pbuddy_adapter->mlmeextpriv;	

			if((remain_ch != pbuddy_mlmeext->cur_channel) && !check_fwstate(&padapter->mlmepriv, _FW_LINKED))
			{	
				if(ATOMIC_READ(&pwdev_priv->switch_ch_to)==1 ||
					(remain_ch != pmlmeext->cur_channel))
				{
					DBG_8192C("%s, issue nulldata pwrbit=1\n", __func__);		
					issue_nulldata(padapter->pbuddy_adapter, NULL, 1, 3, 500);
				
					ATOMIC_SET(&pwdev_priv->switch_ch_to, 0);
			
					DBG_8192C("%s, set switch ch timer, duration=%d\n", __func__, duration-pwdinfo->ext_listen_interval);
					_set_timer(&pwdinfo->ap_p2p_switch_timer, duration-pwdinfo->ext_listen_interval);	
				}			
			}
		
			ready_on_channel = _TRUE;
			//pmlmeext->cur_channel = remain_ch;			
			//set_channel_bwmode(padapter, remain_ch, HAL_PRIME_CHNL_OFFSET_DONT_CARE, CHANNEL_WIDTH_20);
		}else 
#endif //CONFIG_CONCURRENT_MODE
		if(remain_ch != rtw_get_oper_ch(padapter) )
		{
			ready_on_channel = _TRUE;
			//pmlmeext->cur_channel = remain_ch;			
			//set_channel_bwmode(padapter, remain_ch, HAL_PRIME_CHNL_OFFSET_DONT_CARE, CHANNEL_WIDTH_20);
		}
	} else {
		DBG_871X("%s remain_ch:%u not in channel plan!!!!\n", __FUNCTION__, remain_ch);
	}
	

	//call this after other things have been done
#ifdef	CONFIG_CONCURRENT_MODE	
	if(ATOMIC_READ(&pwdev_priv->ro_ch_to)==1 ||
		(remain_ch != rtw_get_oper_ch(padapter)))
	{
		u8 co_channel = 0xff;
		ATOMIC_SET(&pwdev_priv->ro_ch_to, 0);
#endif

		if(ready_on_channel == _TRUE)
		{			
			if ( !check_fwstate(&padapter->mlmepriv, _FW_LINKED ) )
			{
				pmlmeext->cur_channel = remain_ch;
			
#ifdef	CONFIG_CONCURRENT_MODE
				co_channel = rtw_get_oper_ch(padapter);

				if(co_channel !=remain_ch)
#endif
				{
					//if (!padapter->mlmepriv.LinkDetectInfo.bBusyTraffic)
					set_channel_bwmode(padapter, remain_ch, HAL_PRIME_CHNL_OFFSET_DONT_CARE, CHANNEL_WIDTH_20);
				}
			}
		}
		DBG_8192C("%s, set ro ch timer, duration=%d\n", __func__, duration);
		_set_timer( &pcfg80211_wdinfo->remain_on_ch_timer, duration);

#ifdef	CONFIG_CONCURRENT_MODE
	}
#endif

	rtw_cfg80211_ready_on_channel(padapter, *cookie, channel, channel_type, duration, GFP_KERNEL);

exit:
	if (err) {
		pcfg80211_wdinfo->is_ro_ch = _FALSE;
		pcfg80211_wdinfo->last_ro_ch_time = rtw_get_current_time();
	}

	return err;
}

static s32 cfg80211_rtw_cancel_remain_on_channel(struct wiphy *wiphy,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0))
	struct wireless_dev *wdev,
#else
	struct net_device *ndev,
#endif
	u64 cookie)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0))
	struct net_device *ndev = wdev_to_ndev(wdev);
#endif
	s32 err = 0;
	_adapter *padapter;
	struct rtw_wdev_priv *pwdev_priv;
	struct wifidirect_info *pwdinfo;
	struct cfg80211_wifidirect_info *pcfg80211_wdinfo;

	if (ndev == NULL) {
		err = -EINVAL;
		goto exit;
	}

	padapter = (_adapter *)rtw_netdev_priv(ndev);
	pwdev_priv = adapter_wdev_data(padapter);
	pwdinfo = &padapter->wdinfo;
	pcfg80211_wdinfo = &padapter->cfg80211_wdinfo;

	DBG_871X(FUNC_ADPT_FMT"\n", FUNC_ADPT_ARG(padapter));

	if (pcfg80211_wdinfo->is_ro_ch == _TRUE) {
		DBG_8192C("%s, cancel ro ch timer\n", __func__);
		_cancel_timer_ex(&padapter->cfg80211_wdinfo.remain_on_ch_timer);
		#ifdef CONFIG_CONCURRENT_MODE
		ATOMIC_SET(&pwdev_priv->ro_ch_to, 1);
		#endif
		p2p_protocol_wk_hdl(padapter, P2P_RO_CH_WK);
	}

	#if 0
	//	Disable P2P Listen State
	if(!rtw_p2p_chk_role(pwdinfo, P2P_ROLE_CLIENT) && !rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO))
	{
		if(!rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE))
		{
			rtw_p2p_set_state(pwdinfo, P2P_STATE_NONE);
			_rtw_memset(pwdinfo, 0x00, sizeof(struct wifidirect_info));
		}
	}
	else
	#endif
	{
		rtw_p2p_set_state(pwdinfo, rtw_p2p_pre_state(pwdinfo));
#ifdef CONFIG_DEBUG_CFG80211		 
		DBG_8192C("%s, role=%d, p2p_state=%d\n", __func__, rtw_p2p_role(pwdinfo), rtw_p2p_state(pwdinfo));
#endif
	}

	pcfg80211_wdinfo->is_ro_ch = _FALSE;
	pcfg80211_wdinfo->last_ro_ch_time = rtw_get_current_time();

exit:
	return err;
}

#endif //CONFIG_P2P

static int _cfg80211_rtw_mgmt_tx(_adapter *padapter, u8 tx_ch, const u8 *buf, size_t len)
{
	struct xmit_frame	*pmgntframe;
	struct pkt_attrib	*pattrib;
	unsigned char	*pframe;
	int ret = _FAIL;
	bool ack = _TRUE;
	struct rtw_ieee80211_hdr *pwlanhdr;
	struct rtw_wdev_priv *pwdev_priv = adapter_wdev_data(padapter);
	struct xmit_priv	*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);	
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
#ifdef 	CONFIG_P2P
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;
#endif //CONFIG_P2P
	//struct cfg80211_wifidirect_info *pcfg80211_wdinfo = &padapter->cfg80211_wdinfo;

	rtw_set_scan_deny(padapter, 1000);

	rtw_scan_abort(padapter);
	#ifdef CONFIG_CONCURRENT_MODE		
	if(rtw_buddy_adapter_up(padapter))	
		rtw_scan_abort(padapter->pbuddy_adapter);
	#endif /* CONFIG_CONCURRENT_MODE */
#ifdef CONFIG_P2P
	if (padapter->cfg80211_wdinfo.is_ro_ch == _TRUE) {
		//DBG_8192C("%s, cancel ro ch timer\n", __func__);
		//_cancel_timer_ex(&padapter->cfg80211_wdinfo.remain_on_ch_timer);
		//padapter->cfg80211_wdinfo.is_ro_ch = _FALSE;
		#ifdef CONFIG_CONCURRENT_MODE			
		if (!check_fwstate(&padapter->mlmepriv, _FW_LINKED ))
		{
			DBG_8192C("%s, extend ro ch time\n", __func__);
			_set_timer( &padapter->cfg80211_wdinfo.remain_on_ch_timer, pwdinfo->ext_listen_period);
		}	
		#endif //CONFIG_CONCURRENT_MODE			
	}
#endif //CONFIG_P2P
#ifdef CONFIG_CONCURRENT_MODE
	if (check_buddy_fwstate(padapter, _FW_LINKED )) {
		u8 co_channel=0xff;
		PADAPTER pbuddy_adapter = padapter->pbuddy_adapter;			
		struct mlme_ext_priv *pbuddy_mlmeext = &pbuddy_adapter->mlmeextpriv;	

		co_channel = rtw_get_oper_ch(padapter);

		if (tx_ch != pbuddy_mlmeext->cur_channel) {

			u16 ext_listen_period;
			
			if (ATOMIC_READ(&pwdev_priv->switch_ch_to)==1) {
				DBG_8192C("%s, issue nulldata pwrbit=1\n", __func__);
				issue_nulldata(padapter->pbuddy_adapter, NULL, 1, 3, 500);

				ATOMIC_SET(&pwdev_priv->switch_ch_to, 0);

				//DBG_8192C("%s, set switch ch timer, period=%d\n", __func__, pwdinfo->ext_listen_period);
				//_set_timer(&pwdinfo->ap_p2p_switch_timer, pwdinfo->ext_listen_period);
			}

			if (check_fwstate(&padapter->mlmepriv, _FW_LINKED ))
			{
				ext_listen_period = 500;// 500ms
			}	
			else
			{				
				ext_listen_period = pwdinfo->ext_listen_period;
			}

			DBG_8192C("%s, set switch ch timer, period=%d\n", __func__, ext_listen_period);
			_set_timer(&pwdinfo->ap_p2p_switch_timer, ext_listen_period);	
			
		}

		if (!check_fwstate(&padapter->mlmepriv, _FW_LINKED ))
			pmlmeext->cur_channel = tx_ch;

		if (tx_ch != co_channel)
			set_channel_bwmode(padapter, tx_ch, HAL_PRIME_CHNL_OFFSET_DONT_CARE, CHANNEL_WIDTH_20);
	}else 
#endif //CONFIG_CONCURRENT_MODE
	//if (tx_ch != pmlmeext->cur_channel) {
	if(tx_ch != rtw_get_oper_ch(padapter)) {
		if (!check_fwstate(&padapter->mlmepriv, _FW_LINKED ))
			pmlmeext->cur_channel = tx_ch;
		set_channel_bwmode(padapter, tx_ch, HAL_PRIME_CHNL_OFFSET_DONT_CARE, CHANNEL_WIDTH_20);
	}

	//starting alloc mgmt frame to dump it
	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		//ret = -ENOMEM;
		ret = _FAIL;
		goto exit;
	}

	//update attribute
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);
	pattrib->retry_ctrl = _FALSE;

	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;

	_rtw_memcpy(pframe, (void*)buf, len);
	pattrib->pktlen = len;	
	
	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;
	//update seq number
	pmlmeext->mgnt_seq = GetSequence(pwlanhdr);
	pattrib->seqnum = pmlmeext->mgnt_seq;
	pmlmeext->mgnt_seq++;

#ifdef CONFIG_WFD
	{
		struct wifi_display_info	*pwfd_info;
			
		pwfd_info = padapter->wdinfo.wfd_info;
			
		if ( _TRUE == pwfd_info->wfd_enable )
		{
			rtw_append_wfd_ie( padapter, pframe, &pattrib->pktlen );
		}
	}
#endif // CONFIG_WFD
	
	pattrib->last_txcmdsz = pattrib->pktlen;

	if (dump_mgntframe_and_wait_ack(padapter, pmgntframe) != _SUCCESS) 
	{
		ack = _FALSE;
		ret = _FAIL;

		#ifdef CONFIG_DEBUG_CFG80211
		DBG_8192C("%s, ack == _FAIL\n", __func__);
		#endif
	}
	else 
	{

#ifdef CONFIG_XMIT_ACK
		rtw_msleep_os(50);
#endif	
		#ifdef CONFIG_DEBUG_CFG80211
		DBG_8192C("%s, ack=%d, ok!\n", __func__, ack);
		#endif
		ret = _SUCCESS;
	}
	
exit:

	#ifdef CONFIG_DEBUG_CFG80211
	DBG_8192C("%s, ret=%d\n", __func__, ret);
	#endif
	
	return ret;		
	
}

static int cfg80211_rtw_mgmt_tx(struct wiphy *wiphy,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0))
	struct wireless_dev *wdev,
#else
	struct net_device *ndev,
#endif
	struct ieee80211_channel *chan,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,38)) || defined(COMPAT_KERNEL_RELEASE)
	bool offchan,
#endif
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,8,0))
	enum nl80211_channel_type channel_type,
	#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)) || defined(COMPAT_KERNEL_RELEASE)
	bool channel_type_valid,
	#endif
#endif
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,38)) || defined(COMPAT_KERNEL_RELEASE)
	unsigned int wait,
#endif
	const u8 *buf, size_t len,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0))
	bool no_cck,
#endif
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,3,0))
	bool dont_wait_for_ack,
#endif
	u64 *cookie)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0))
	struct net_device *ndev = wdev_to_ndev(wdev);
#endif
	int ret = 0;
	int tx_ret;
	u32 dump_limit = RTW_MAX_MGMT_TX_CNT;
	u32 dump_cnt = 0;
	bool ack = _TRUE;
	u8 tx_ch = (u8)ieee80211_frequency_to_channel(chan->center_freq);
	u8 category, action;
	int type = (-1);
	u32 start = rtw_get_current_time();
	_adapter *padapter;
	struct rtw_wdev_priv *pwdev_priv;

	if (ndev == NULL) {
		ret = -EINVAL;
		goto exit;
	}

	padapter = (_adapter *)rtw_netdev_priv(ndev);
	pwdev_priv = adapter_wdev_data(padapter);

	/* cookie generation */
	*cookie = (unsigned long) buf;

#ifdef CONFIG_DEBUG_CFG80211
	DBG_871X(FUNC_ADPT_FMT" len=%zu, ch=%d"
	#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,8,0))
		", ch_type=%d"
		#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)) || defined(COMPAT_KERNEL_RELEASE)	
		", channel_type_valid=%d"
		#endif
	#endif
		"\n", FUNC_ADPT_ARG(padapter),
		len, tx_ch
	#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,8,0))
		, channel_type
		#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)) || defined(COMPAT_KERNEL_RELEASE)	
		, channel_type_valid
		#endif
	#endif
	);
#endif /* CONFIG_DEBUG_CFG80211 */

	/* indicate ack before issue frame to avoid racing with rsp frame */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)) || defined(COMPAT_KERNEL_RELEASE)
	rtw_cfg80211_mgmt_tx_status(padapter, *cookie, buf, len, ack, GFP_KERNEL);
#elif  (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,34) && LINUX_VERSION_CODE<=KERNEL_VERSION(2,6,35))
	cfg80211_action_tx_status(ndev, *cookie, buf, len, ack, GFP_KERNEL);
#endif

	if (rtw_action_frame_parse(buf, len, &category, &action) == _FALSE) {
		DBG_8192C(FUNC_ADPT_FMT" frame_control:0x%x\n", FUNC_ADPT_ARG(padapter),
			le16_to_cpu(((struct rtw_ieee80211_hdr_3addr *)buf)->frame_ctl));
		goto exit;
	}

	DBG_8192C("RTW_Tx:tx_ch=%d, da="MAC_FMT"\n", tx_ch, MAC_ARG(GetAddr1Ptr(buf)));
	#ifdef CONFIG_P2P
	if((type = rtw_p2p_check_frames(padapter, buf, len, _TRUE)) >= 0) {
		goto dump;
	}
	#endif
	if (category == RTW_WLAN_CATEGORY_PUBLIC)
		DBG_871X("RTW_Tx:%s\n", action_public_str(action));
	else
		DBG_871X("RTW_Tx:category(%u), action(%u)\n", category, action);

dump:

	rtw_ps_deny(padapter, PS_DENY_MGNT_TX);
	if(_FAIL == rtw_pwr_wakeup(padapter)) {
		ret = -EFAULT;
		goto cancel_ps_deny;
	}

	do {
		dump_cnt++;
		tx_ret = _cfg80211_rtw_mgmt_tx(padapter, tx_ch, buf, len);
	} while (dump_cnt < dump_limit && tx_ret != _SUCCESS);

	if (tx_ret != _SUCCESS || dump_cnt > 1) {
		DBG_871X(FUNC_ADPT_FMT" %s (%d/%d) in %d ms\n", FUNC_ADPT_ARG(padapter),
			tx_ret==_SUCCESS?"OK":"FAIL", dump_cnt, dump_limit, rtw_get_passing_time_ms(start));
	}

	switch (type) {
	case P2P_GO_NEGO_CONF:
		rtw_clear_scan_deny(padapter);
		break;
	case P2P_INVIT_RESP:
		if (pwdev_priv->invit_info.flags & BIT(0)
			&& pwdev_priv->invit_info.status == 0) 
		{
			DBG_871X(FUNC_ADPT_FMT" agree with invitation of persistent group\n",
				FUNC_ADPT_ARG(padapter));
			rtw_set_scan_deny(padapter, 5000);
			rtw_pwr_wakeup_ex(padapter, 5000);
			rtw_clear_scan_deny(padapter);
		}
		break;
	}	

cancel_ps_deny:
	rtw_ps_deny_cancel(padapter, PS_DENY_MGNT_TX);
exit:
	return ret;
}

static void cfg80211_rtw_mgmt_frame_register(struct wiphy *wiphy,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0))
	struct wireless_dev *wdev,
#else
	struct net_device *ndev,
#endif
	u16 frame_type, bool reg)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0))
	struct net_device *ndev = wdev_to_ndev(wdev);
#endif
	_adapter *adapter;

	if (ndev == NULL)
		goto exit;

	adapter = (_adapter *)rtw_netdev_priv(ndev);

#ifdef CONFIG_DEBUG_CFG80211
	DBG_871X(FUNC_ADPT_FMT" frame_type:%x, reg:%d\n", FUNC_ADPT_ARG(adapter),
		frame_type, reg);
#endif

	if (frame_type != (IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_PROBE_REQ))
		return;
exit:
	return;
}

#if defined(CONFIG_TDLS) && (LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0))
static int cfg80211_rtw_tdls_mgmt(struct wiphy *wiphy,
	struct net_device *ndev,
	u8 *peer,
	u8 action_code,
	u8 dialog_token,
	u16 status_code,
	const u8 *buf,
	size_t len)
{
	_adapter *padapter = (_adapter *)rtw_netdev_priv(ndev);
	int ret = 0;
	struct tdls_txmgmt txmgmt;

	//TDLS: discard wpa_supplicant's frame mgmt
	DBG_871X("%s %d\n", __FUNCTION__, __LINE__);
	return 0;

	_rtw_memset(&txmgmt, 0x00, sizeof(struct tdls_txmgmt));
	_rtw_memcpy(txmgmt.peer, peer, ETH_ALEN);
	txmgmt.action_code = action_code;
	txmgmt.dialog_token= dialog_token;
	txmgmt.status_code = status_code;
	txmgmt.len = len;
	txmgmt.buf = (u8 *)rtw_malloc(txmgmt.len);
	if (txmgmt.buf == NULL)
	{
		ret = -ENOMEM;
		goto bad;
	}
	_rtw_memcpy(txmgmt.buf, (void*)buf, txmgmt.len);
	txmgmt.external_support = _TRUE;

//TDLS: Debug purpose
#if 0
	DBG_871X("%s %d\n", __FUNCTION__, __LINE__);
	DBG_871X("peer:"MAC_FMT" ", MAC_ARG(txmgmt.peer));
	DBG_871X("action code:%d ", txmgmt.action_code);
	DBG_871X("dialog:%d ", txmgmt.dialog_token);
	DBG_871X("status code:%d\n", txmgmt.status_code);
	if( txmgmt.len > 0 )
	{
		int i=0;
		for(;i < len; i++)
			DBG_871X("%02x ", *(txmgmt.buf+i));
			DBG_871X("\n len:%d\n", txmgmt.len);
	}
#endif

	switch(txmgmt.action_code) {
		case TDLS_SETUP_REQUEST:
			issue_tdls_setup_req(padapter, &txmgmt, _TRUE);
			break;
		case TDLS_SETUP_RESPONSE:
			issue_tdls_setup_rsp(padapter, &txmgmt);
			break;
		case TDLS_SETUP_CONFIRM:
			issue_tdls_setup_cfm(padapter, &txmgmt);
			break;
		case TDLS_TEARDOWN:
			break;
		case TDLS_DISCOVERY_REQUEST:
			issue_tdls_dis_req(padapter, &txmgmt);
		break;
	}

bad:
	if (txmgmt.buf)
	{
		rtw_mfree(txmgmt.buf, txmgmt.len);
	}

	return ret;
}

static int cfg80211_rtw_tdls_oper(struct wiphy *wiphy,
	struct net_device *ndev,
	u8 *peer,
	enum nl80211_tdls_operation oper)
{
	_adapter *padapter = (_adapter *)rtw_netdev_priv(ndev);
	struct tdls_txmgmt	txmgmt;
	struct sta_info *ptdls_sta = NULL;

	DBG_871X(FUNC_NDEV_FMT", nl80211_tdls_operation:%d\n", FUNC_NDEV_ARG(ndev), oper);

#ifdef CONFIG_LPS
	rtw_lps_ctrl_wk_cmd(padapter, LPS_CTRL_LEAVE, 1);
#endif //CONFIG_LPS

	_rtw_memset(&txmgmt, 0x00, sizeof(struct tdls_txmgmt));
	if(peer)
		_rtw_memcpy(txmgmt.peer, peer, ETH_ALEN);
#if 0
	CFG80211_TDLS_DISCOVERY_REQ,
	CFG80211_TDLS_SETUP,
	CFG80211_TDLS_TEARDOWN,
	CFG80211_TDLS_ENABLE_LINK,
	CFG80211_TDLS_DISABLE_LINK,
	CFG80211_TDLS_ENABLE,
	CFG80211_TDLS_DISABLE
#endif
	switch(oper) {
		case NL80211_TDLS_DISCOVERY_REQ:
			issue_tdls_dis_req(padapter, &txmgmt);
			break;
		case NL80211_TDLS_SETUP:
#ifdef CONFIG_WFD
			if ( _AES_ != padapter->securitypriv.dot11PrivacyAlgrthm )
			{
				if ( padapter->wdinfo.wfd_tdls_weaksec == _TRUE)
					issue_tdls_setup_req(padapter, &txmgmt, _TRUE);
				else
					DBG_871X( "[%s] Current link is not AES, SKIP sending the tdls setup request!!\n", __FUNCTION__ );
			}
			else
#endif // CONFIG_WFD
			{
				issue_tdls_setup_req(padapter, &txmgmt, _TRUE);
			}
			break;
		case NL80211_TDLS_TEARDOWN:
			ptdls_sta = rtw_get_stainfo( &(padapter->stapriv), txmgmt.peer);
			if(ptdls_sta != NULL)
			{
				txmgmt.status_code = _RSON_TDLS_TEAR_UN_RSN_;
				issue_tdls_teardown(padapter, &txmgmt, _FALSE);
			}
			else
				DBG_871X( "TDLS peer not found\n");
			break;
		case NL80211_TDLS_ENABLE_LINK:
			break;
		case NL80211_TDLS_DISABLE_LINK:
			break;
	}
	return 0;
}
#endif /* CONFIG_TDLS */

#if defined(CONFIG_PNO_SUPPORT) && (LINUX_VERSION_CODE >= KERNEL_VERSION(3,0,0))
static int cfg80211_rtw_sched_scan_start(struct wiphy *wiphy,
		struct net_device *dev,
		struct cfg80211_sched_scan_request *request) {

	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct	mlme_priv	*pmlmepriv = &(padapter->mlmepriv);
	u8 ret;

	if (padapter->bup == _FALSE) {
		DBG_871X("%s: net device is down.\n", __func__);
		return -EIO;
	}

	if(check_fwstate(pmlmepriv, _FW_UNDER_SURVEY) == _TRUE ||
		check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE  ||
		check_fwstate(pmlmepriv, _FW_UNDER_LINKING) == _TRUE) {
		DBG_871X("%s: device is busy.\n", __func__);
		rtw_scan_abort(padapter);
	}

	if (request == NULL) {
		DBG_871X("%s: invalid cfg80211_requests parameters.\n", __func__);
		return -EINVAL;
	}

	ret = rtw_android_cfg80211_pno_setup(dev, request->ssids,
			request->n_ssids, request->interval);

	if (ret < 0) {
		DBG_871X("%s ret: %d\n", __func__, ret);
		goto exit;
	}

	ret = rtw_android_pno_enable(dev, _TRUE);
	if (ret < 0) {
		DBG_871X("%s ret: %d\n", __func__, ret);
		goto exit;
	}
exit:
	return ret;
}

static int cfg80211_rtw_sched_scan_stop(struct wiphy *wiphy,
		struct net_device *dev) {
	return rtw_android_pno_enable(dev, _FALSE);
}
#endif /* CONFIG_PNO_SUPPORT */

static int rtw_cfg80211_set_beacon_wpsp2pie(struct net_device *ndev, char *buf, int len)
{	
	int ret = 0;
	uint wps_ielen = 0;
	u8 *wps_ie;
	u32	p2p_ielen = 0;
	u8 wps_oui[8]={0x0,0x50,0xf2,0x04};	
	u8 *p2p_ie;
	u32	wfd_ielen = 0;	
	u8 *wfd_ie;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(ndev);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv *pmlmeext = &(padapter->mlmeextpriv);

	DBG_871X(FUNC_NDEV_FMT" ielen=%d\n", FUNC_NDEV_ARG(ndev), len);
	
	if(len>0)
	{
		if((wps_ie = rtw_get_wps_ie(buf, len, NULL, &wps_ielen)))
		{	
			#ifdef CONFIG_DEBUG_CFG80211
			DBG_8192C("bcn_wps_ielen=%d\n", wps_ielen);
			#endif
		
			if(pmlmepriv->wps_beacon_ie)
			{
				u32 free_len = pmlmepriv->wps_beacon_ie_len;
				pmlmepriv->wps_beacon_ie_len = 0;
				rtw_mfree(pmlmepriv->wps_beacon_ie, free_len);
				pmlmepriv->wps_beacon_ie = NULL;			
			}	

			pmlmepriv->wps_beacon_ie = rtw_malloc(wps_ielen);
			if ( pmlmepriv->wps_beacon_ie == NULL) {
				DBG_8192C("%s()-%d: rtw_malloc() ERROR!\n", __FUNCTION__, __LINE__);
				return -EINVAL;
			
			}
			
			_rtw_memcpy(pmlmepriv->wps_beacon_ie, wps_ie, wps_ielen);
			pmlmepriv->wps_beacon_ie_len = wps_ielen;
	
			update_beacon(padapter, _VENDOR_SPECIFIC_IE_, wps_oui, _TRUE);

		}

		//buf += wps_ielen;
		//len -= wps_ielen;

		#ifdef CONFIG_P2P
		if((p2p_ie=rtw_get_p2p_ie(buf, len, NULL, &p2p_ielen)))
		{
			#ifdef CONFIG_DEBUG_CFG80211
			DBG_8192C("bcn_p2p_ielen=%d\n", p2p_ielen);
			#endif
		
			if(pmlmepriv->p2p_beacon_ie)
			{
				u32 free_len = pmlmepriv->p2p_beacon_ie_len;
				pmlmepriv->p2p_beacon_ie_len = 0;
				rtw_mfree(pmlmepriv->p2p_beacon_ie, free_len);
				pmlmepriv->p2p_beacon_ie = NULL;			
			}	

			pmlmepriv->p2p_beacon_ie = rtw_malloc(p2p_ielen);
			if ( pmlmepriv->p2p_beacon_ie == NULL) {
				DBG_8192C("%s()-%d: rtw_malloc() ERROR!\n", __FUNCTION__, __LINE__);
				return -EINVAL;
			
			}
			
			_rtw_memcpy(pmlmepriv->p2p_beacon_ie, p2p_ie, p2p_ielen);
			pmlmepriv->p2p_beacon_ie_len = p2p_ielen;
			
		}
		#endif //CONFIG_P2P
		
		//buf += p2p_ielen;
		//len -= p2p_ielen;

		#ifdef CONFIG_WFD
		if(rtw_get_wfd_ie(buf, len, NULL, &wfd_ielen)) 
		{
			#ifdef CONFIG_DEBUG_CFG80211
			DBG_8192C("bcn_wfd_ielen=%d\n", wfd_ielen);
			#endif
			
			if(pmlmepriv->wfd_beacon_ie)
			{
				u32 free_len = pmlmepriv->wfd_beacon_ie_len;
				pmlmepriv->wfd_beacon_ie_len = 0;
				rtw_mfree(pmlmepriv->wfd_beacon_ie, free_len);
				pmlmepriv->wfd_beacon_ie = NULL;
			}	

			pmlmepriv->wfd_beacon_ie = rtw_malloc(wfd_ielen);
			if ( pmlmepriv->wfd_beacon_ie == NULL) {
				DBG_8192C("%s()-%d: rtw_malloc() ERROR!\n", __FUNCTION__, __LINE__);
				return -EINVAL;
			
			}
			rtw_get_wfd_ie(buf, len, pmlmepriv->wfd_beacon_ie, &pmlmepriv->wfd_beacon_ie_len);			
		}
		#endif //CONFIG_WFD
		
		pmlmeext->bstart_bss = _TRUE;
		
	}

	return ret;
	
}

static int rtw_cfg80211_set_probe_resp_wpsp2pie(struct net_device *net, char *buf, int len)
{
	int ret = 0;
	uint wps_ielen = 0;
	u8 *wps_ie;
	u32	p2p_ielen = 0;	
	u8 *p2p_ie;
	u32	wfd_ielen = 0;	
	u8 *wfd_ie;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(net);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);	

#ifdef CONFIG_DEBUG_CFG80211
	DBG_8192C("%s, ielen=%d\n", __func__, len);
#endif
	
	if(len>0)
	{
		if((wps_ie = rtw_get_wps_ie(buf, len, NULL, &wps_ielen)))
		{	
			uint	attr_contentlen = 0;
			u16	uconfig_method, *puconfig_method = NULL;

			#ifdef CONFIG_DEBUG_CFG80211			
			DBG_8192C("probe_resp_wps_ielen=%d\n", wps_ielen);
			#endif
		
			if(check_fwstate(pmlmepriv, WIFI_UNDER_WPS))
			{
				u8 sr = 0;
				rtw_get_wps_attr_content(wps_ie,  wps_ielen, WPS_ATTR_SELECTED_REGISTRAR, (u8*)(&sr), NULL);

				if (sr != 0)
				{
					DBG_871X("%s, got sr\n", __func__);
				}	
				else
				{				
					DBG_8192C("GO mode process WPS under site-survey,  sr no set\n");
					return ret;
				}	
			}			
		
			if(pmlmepriv->wps_probe_resp_ie)
			{
				u32 free_len = pmlmepriv->wps_probe_resp_ie_len;
				pmlmepriv->wps_probe_resp_ie_len = 0;
				rtw_mfree(pmlmepriv->wps_probe_resp_ie, free_len);
				pmlmepriv->wps_probe_resp_ie = NULL;			
			}	

			pmlmepriv->wps_probe_resp_ie = rtw_malloc(wps_ielen);
			if ( pmlmepriv->wps_probe_resp_ie == NULL) {
				DBG_8192C("%s()-%d: rtw_malloc() ERROR!\n", __FUNCTION__, __LINE__);
				return -EINVAL;
			
			}
			
			//add PUSH_BUTTON config_method by driver self in wpsie of probe_resp at GO Mode 
			if ( (puconfig_method = (u16*)rtw_get_wps_attr_content( wps_ie, wps_ielen, WPS_ATTR_CONF_METHOD , NULL, &attr_contentlen)) != NULL )
			{
				//struct registry_priv *pregistrypriv = &padapter->registrypriv;
				struct wireless_dev *wdev = padapter->rtw_wdev;
				
				#ifdef CONFIG_DEBUG_CFG80211		
				//printk("config_method in wpsie of probe_resp = 0x%x\n", be16_to_cpu(*puconfig_method));
				#endif
				
				//if(check_fwstate(pmlmepriv, WIFI_AP_STATE) != _TRUE)
				#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)) || defined(COMPAT_KERNEL_RELEASE)
				if(wdev->iftype != NL80211_IFTYPE_P2P_GO) //for WIFI-DIRECT LOGO 4.2.2, AUTO GO can't set PUSH_BUTTON flags
				{
					uconfig_method = WPS_CM_PUSH_BUTTON;
					uconfig_method = cpu_to_be16( uconfig_method );
			
					*puconfig_method |= uconfig_method;			
				}	
				#endif
			}
			
			_rtw_memcpy(pmlmepriv->wps_probe_resp_ie, wps_ie, wps_ielen);
			pmlmepriv->wps_probe_resp_ie_len = wps_ielen;
			
		}

		//buf += wps_ielen;
		//len -= wps_ielen;

		#ifdef CONFIG_P2P
		if((p2p_ie=rtw_get_p2p_ie(buf, len, NULL, &p2p_ielen))) 
		{
			u8 is_GO = _FALSE;			
			u32 attr_contentlen = 0;
			u16 cap_attr=0;

			#ifdef CONFIG_DEBUG_CFG80211
			DBG_8192C("probe_resp_p2p_ielen=%d\n", p2p_ielen);
			#endif			

			//Check P2P Capability ATTR
			if( rtw_get_p2p_attr_content( p2p_ie, p2p_ielen, P2P_ATTR_CAPABILITY, (u8*)&cap_attr, (uint*) &attr_contentlen) )
			{
				u8 grp_cap=0;
				//DBG_8192C( "[%s] Got P2P Capability Attr!!\n", __FUNCTION__ );
				cap_attr = le16_to_cpu(cap_attr);
				grp_cap = (u8)((cap_attr >> 8)&0xff);
				
				is_GO = (grp_cap&BIT(0)) ? _TRUE:_FALSE;

				if(is_GO)
					DBG_8192C("Got P2P Capability Attr, grp_cap=0x%x, is_GO\n", grp_cap);
			}


			if(is_GO == _FALSE)
			{
				if(pmlmepriv->p2p_probe_resp_ie)
				{
					u32 free_len = pmlmepriv->p2p_probe_resp_ie_len;
					pmlmepriv->p2p_probe_resp_ie_len = 0;
					rtw_mfree(pmlmepriv->p2p_probe_resp_ie, free_len);
					pmlmepriv->p2p_probe_resp_ie = NULL;		
				}	

				pmlmepriv->p2p_probe_resp_ie = rtw_malloc(p2p_ielen);
				if ( pmlmepriv->p2p_probe_resp_ie == NULL) {
					DBG_8192C("%s()-%d: rtw_malloc() ERROR!\n", __FUNCTION__, __LINE__);
					return -EINVAL;
				
				}
				_rtw_memcpy(pmlmepriv->p2p_probe_resp_ie, p2p_ie, p2p_ielen);
				pmlmepriv->p2p_probe_resp_ie_len = p2p_ielen;
			}		
			else
			{
				if(pmlmepriv->p2p_go_probe_resp_ie)
				{
					u32 free_len = pmlmepriv->p2p_go_probe_resp_ie_len;
					pmlmepriv->p2p_go_probe_resp_ie_len = 0;
					rtw_mfree(pmlmepriv->p2p_go_probe_resp_ie, free_len);
					pmlmepriv->p2p_go_probe_resp_ie = NULL;			
				}	

				pmlmepriv->p2p_go_probe_resp_ie = rtw_malloc(p2p_ielen);
				if ( pmlmepriv->p2p_go_probe_resp_ie == NULL) {
					DBG_8192C("%s()-%d: rtw_malloc() ERROR!\n", __FUNCTION__, __LINE__);
					return -EINVAL;
			
				}
				_rtw_memcpy(pmlmepriv->p2p_go_probe_resp_ie, p2p_ie, p2p_ielen);
				pmlmepriv->p2p_go_probe_resp_ie_len = p2p_ielen;
			}
			
		}
		#endif //CONFIG_P2P
		
		//buf += p2p_ielen;
		//len -= p2p_ielen;

		#ifdef CONFIG_WFD
		if(rtw_get_wfd_ie(buf, len, NULL, &wfd_ielen)) 
		{
			#ifdef CONFIG_DEBUG_CFG80211
			DBG_8192C("probe_resp_wfd_ielen=%d\n", wfd_ielen);
			#endif
			
			if(pmlmepriv->wfd_probe_resp_ie)
			{
				u32 free_len = pmlmepriv->wfd_probe_resp_ie_len;
				pmlmepriv->wfd_probe_resp_ie_len = 0;
				rtw_mfree(pmlmepriv->wfd_probe_resp_ie, free_len);
				pmlmepriv->wfd_probe_resp_ie = NULL;
			}	

			pmlmepriv->wfd_probe_resp_ie = rtw_malloc(wfd_ielen);
			if ( pmlmepriv->wfd_probe_resp_ie == NULL) {
				DBG_8192C("%s()-%d: rtw_malloc() ERROR!\n", __FUNCTION__, __LINE__);
				return -EINVAL;
			
			}
			rtw_get_wfd_ie(buf, len, pmlmepriv->wfd_probe_resp_ie, &pmlmepriv->wfd_probe_resp_ie_len);			
		}
		#endif //CONFIG_WFD
		
	}

	return ret;
	
}

static int rtw_cfg80211_set_assoc_resp_wpsp2pie(struct net_device *net, char *buf, int len)
{
	int ret = 0;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(net);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);

	DBG_8192C("%s, ielen=%d\n", __func__, len);
	
	if(len>0)
	{
		if(pmlmepriv->wps_assoc_resp_ie)
		{
			u32 free_len = pmlmepriv->wps_assoc_resp_ie_len;
			pmlmepriv->wps_assoc_resp_ie_len = 0;
			rtw_mfree(pmlmepriv->wps_assoc_resp_ie, free_len);
			pmlmepriv->wps_assoc_resp_ie = NULL;
		}	

		pmlmepriv->wps_assoc_resp_ie = rtw_malloc(len);
		if ( pmlmepriv->wps_assoc_resp_ie == NULL) {
			DBG_8192C("%s()-%d: rtw_malloc() ERROR!\n", __FUNCTION__, __LINE__);
			return -EINVAL;
			
		}
		_rtw_memcpy(pmlmepriv->wps_assoc_resp_ie, buf, len);
		pmlmepriv->wps_assoc_resp_ie_len = len;
	}

	return ret;
	
}

int rtw_cfg80211_set_mgnt_wpsp2pie(struct net_device *net, char *buf, int len,
	int type)
{
	int ret = 0;
	uint wps_ielen = 0;
	u32	p2p_ielen = 0;

#ifdef CONFIG_DEBUG_CFG80211
	DBG_8192C("%s, ielen=%d\n", __func__, len);
#endif

	if(	(rtw_get_wps_ie(buf, len, NULL, &wps_ielen) && (wps_ielen>0))
		#ifdef CONFIG_P2P
		|| (rtw_get_p2p_ie(buf, len, NULL, &p2p_ielen) && (p2p_ielen>0))
		#endif
	)		
	{	
		if (net != NULL) 
		{
			switch (type) 
			{
				case 0x1: //BEACON
				ret = rtw_cfg80211_set_beacon_wpsp2pie(net, buf, len);
				break;
				case 0x2: //PROBE_RESP
				ret = rtw_cfg80211_set_probe_resp_wpsp2pie(net, buf, len);
				break;
				case 0x4: //ASSOC_RESP
				ret = rtw_cfg80211_set_assoc_resp_wpsp2pie(net, buf, len);
				break;
			}		
		}
	}	

	return ret;
	
}

static void rtw_cfg80211_init_ht_capab(struct ieee80211_sta_ht_cap *ht_cap, enum ieee80211_band band, u8 rf_type)
{

#define MAX_BIT_RATE_40MHZ_MCS15 	300	/* Mbps */
#define MAX_BIT_RATE_40MHZ_MCS7 	150	/* Mbps */

	ht_cap->ht_supported = _TRUE;

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
	 *if rx_ant =1 rx_mask[0]=0xff;==>MCS0-MCS7
	 *if rx_ant =2 rx_mask[1]=0xff;==>MCS8-MCS15
	 *if rx_ant >=3 rx_mask[2]=0xff;
	 *if BW_40 rx_mask[4]=0x01;
	 *highest supported RX rate
	 */
	if(rf_type == RF_1T1R)
	{
		ht_cap->mcs.rx_mask[0] = 0xFF;
		ht_cap->mcs.rx_mask[1] = 0x00;
		ht_cap->mcs.rx_mask[4] = 0x01;

		ht_cap->mcs.rx_highest = MAX_BIT_RATE_40MHZ_MCS7;
	}
	else if((rf_type == RF_1T2R) || (rf_type==RF_2T2R))
	{
		ht_cap->mcs.rx_mask[0] = 0xFF;
		ht_cap->mcs.rx_mask[1] = 0xFF;
		ht_cap->mcs.rx_mask[4] = 0x01;

		ht_cap->mcs.rx_highest = MAX_BIT_RATE_40MHZ_MCS15;
	}
	else
	{
		DBG_8192C("%s, error rf_type=%d\n", __func__, rf_type);
	}	
	
}

void rtw_cfg80211_init_wiphy(_adapter *padapter)
{
	u8 rf_type;
	struct ieee80211_supported_band *bands;
	struct wireless_dev *pwdev = padapter->rtw_wdev;
	struct wiphy *wiphy = pwdev->wiphy;
	
	rtw_hal_get_hwreg(padapter, HW_VAR_RF_TYPE, (u8 *)(&rf_type));

	DBG_8192C("%s:rf_type=%d\n", __func__, rf_type);

	/* if (padapter->registrypriv.wireless_mode & WIRELESS_11G) */
	{
		bands = wiphy->bands[IEEE80211_BAND_2GHZ];
		if(bands)
			rtw_cfg80211_init_ht_capab(&bands->ht_cap, IEEE80211_BAND_2GHZ, rf_type);
	}

	/* if (padapter->registrypriv.wireless_mode & WIRELESS_11A) */
	{
		bands = wiphy->bands[IEEE80211_BAND_5GHZ];
		if(bands)
			rtw_cfg80211_init_ht_capab(&bands->ht_cap, IEEE80211_BAND_5GHZ, rf_type);
	}

	/* init regulary domain */
	rtw_regd_init(padapter, rtw_reg_notifier);

	/* copy mac_addr to wiphy */
	_rtw_memcpy(wiphy->perm_addr, padapter->eeprompriv.mac_addr, ETH_ALEN);

}

/*
struct ieee80211_iface_limit rtw_limits[] = {
	{ .max = 1, .types = BIT(NL80211_IFTYPE_STATION)
					| BIT(NL80211_IFTYPE_ADHOC)
#ifdef CONFIG_AP_MODE
					| BIT(NL80211_IFTYPE_AP)
#endif
#if defined(CONFIG_P2P) && ((LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)) || defined(COMPAT_KERNEL_RELEASE))
					| BIT(NL80211_IFTYPE_P2P_CLIENT)
					| BIT(NL80211_IFTYPE_P2P_GO)
#endif
	},
	{.max = 1, .types = BIT(NL80211_IFTYPE_MONITOR)},
};

struct ieee80211_iface_combination rtw_combinations = {
	.limits = rtw_limits,
	.n_limits = ARRAY_SIZE(rtw_limits),
	.max_interfaces = 2,
	.num_different_channels = 1,
};
*/

static void rtw_cfg80211_preinit_wiphy(_adapter *padapter, struct wiphy *wiphy)
{

	wiphy->signal_type = CFG80211_SIGNAL_TYPE_MBM;

	wiphy->max_scan_ssids = RTW_SSID_SCAN_AMOUNT;
	wiphy->max_scan_ie_len = RTW_SCAN_IE_LEN_MAX;	
	wiphy->max_num_pmkids = RTW_MAX_NUM_PMKIDS;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,38)) || defined(COMPAT_KERNEL_RELEASE)	 
	wiphy->max_remain_on_channel_duration = RTW_MAX_REMAIN_ON_CHANNEL_DURATION;
#endif
	
	wiphy->interface_modes =	BIT(NL80211_IFTYPE_STATION)
								| BIT(NL80211_IFTYPE_ADHOC)
#ifdef CONFIG_AP_MODE
								| BIT(NL80211_IFTYPE_AP)
								| BIT(NL80211_IFTYPE_MONITOR)
#endif
#if defined(CONFIG_P2P) && ((LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)) || defined(COMPAT_KERNEL_RELEASE))
								| BIT(NL80211_IFTYPE_P2P_CLIENT)
								| BIT(NL80211_IFTYPE_P2P_GO)
#endif
								;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)) || defined(COMPAT_KERNEL_RELEASE)	
#ifdef CONFIG_AP_MODE
	wiphy->mgmt_stypes = rtw_cfg80211_default_mgmt_stypes;
#endif //CONFIG_AP_MODE	
#endif		

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,0,0))
	wiphy->software_iftypes |= BIT(NL80211_IFTYPE_MONITOR);
#endif

	/*
	wiphy->iface_combinations = &rtw_combinations;
	wiphy->n_iface_combinations = 1;
	*/

	wiphy->cipher_suites = rtw_cipher_suites;
	wiphy->n_cipher_suites = ARRAY_SIZE(rtw_cipher_suites);

	/* if (padapter->registrypriv.wireless_mode & WIRELESS_11G) */
		wiphy->bands[IEEE80211_BAND_2GHZ] = rtw_spt_band_alloc(IEEE80211_BAND_2GHZ);
	/* if (padapter->registrypriv.wireless_mode & WIRELESS_11A) */
		wiphy->bands[IEEE80211_BAND_5GHZ] = rtw_spt_band_alloc(IEEE80211_BAND_5GHZ);
	
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,38) && LINUX_VERSION_CODE < KERNEL_VERSION(3,0,0))
	wiphy->flags |= WIPHY_FLAG_SUPPORTS_SEPARATE_DEFAULT_KEYS;
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,3,0))
	wiphy->flags |= WIPHY_FLAG_HAS_REMAIN_ON_CHANNEL;
	wiphy->flags |= WIPHY_FLAG_OFFCHAN_TX | WIPHY_FLAG_HAVE_AP_SME;
#endif

#if defined(CONFIG_PM) && (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0))
	wiphy->flags |= WIPHY_FLAG_SUPPORTS_SCHED_SCAN;
#ifdef CONFIG_PNO_SUPPORT
	wiphy->max_sched_scan_ssids = MAX_PNO_LIST_COUNT;
#endif
#endif

#if defined(CONFIG_PM) && (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0))
	wiphy->wowlan = wowlan_stub;
#endif

#if defined(CONFIG_TDLS) && (LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0))
	wiphy->flags |= WIPHY_FLAG_SUPPORTS_TDLS;
	//wiphy->flags |= WIPHY_FLAG_TDLS_EXTERNAL_SETUP;
#endif /* CONFIG_TDLS */

	if(padapter->registrypriv.power_mgnt != PS_MODE_ACTIVE)
		wiphy->flags |= WIPHY_FLAG_PS_ON_BY_DEFAULT;
	else 
		wiphy->flags &= ~WIPHY_FLAG_PS_ON_BY_DEFAULT;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0))
	//wiphy->flags |= WIPHY_FLAG_SUPPORTS_FW_ROAM;
#endif
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
	
#ifdef CONFIG_AP_MODE
	.add_virtual_intf = cfg80211_rtw_add_virtual_intf,
	.del_virtual_intf = cfg80211_rtw_del_virtual_intf,

	#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 4, 0)) && !defined(COMPAT_KERNEL_RELEASE)
	.add_beacon = cfg80211_rtw_add_beacon,
	.set_beacon = cfg80211_rtw_set_beacon,
	.del_beacon = cfg80211_rtw_del_beacon,
	#else
	.start_ap = cfg80211_rtw_start_ap,
	.change_beacon = cfg80211_rtw_change_beacon,
	.stop_ap = cfg80211_rtw_stop_ap,
	#endif
	
	.add_station = cfg80211_rtw_add_station,
	.del_station = cfg80211_rtw_del_station,
	.change_station = cfg80211_rtw_change_station,
	.dump_station = cfg80211_rtw_dump_station,
	.change_bss = cfg80211_rtw_change_bss,
	#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0))
	.set_channel = cfg80211_rtw_set_channel,
	#endif
	//.auth = cfg80211_rtw_auth,
	//.assoc = cfg80211_rtw_assoc,	
#endif //CONFIG_AP_MODE

#ifdef CONFIG_P2P
	.remain_on_channel = cfg80211_rtw_remain_on_channel,
	.cancel_remain_on_channel = cfg80211_rtw_cancel_remain_on_channel,
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)) || defined(COMPAT_KERNEL_RELEASE)	 
	.mgmt_tx = cfg80211_rtw_mgmt_tx,
	.mgmt_frame_register = cfg80211_rtw_mgmt_frame_register,
#elif  (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,34) && LINUX_VERSION_CODE<=KERNEL_VERSION(2,6,35))
	.action = cfg80211_rtw_mgmt_tx,
#endif

#if defined(CONFIG_TDLS) && (LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0))
	.tdls_mgmt = cfg80211_rtw_tdls_mgmt,
	.tdls_oper = cfg80211_rtw_tdls_oper,
#endif /* CONFIG_TDLS */

#if defined(CONFIG_PNO_SUPPORT) && (LINUX_VERSION_CODE >= KERNEL_VERSION(3,0,0))
	.sched_scan_start = cfg80211_rtw_sched_scan_start,
	.sched_scan_stop = cfg80211_rtw_sched_scan_stop,
#endif /* CONFIG_PNO_SUPPORT */
};

int rtw_wdev_alloc(_adapter *padapter, struct device *dev)
{
	int ret = 0;
	struct wiphy *wiphy;
	struct wireless_dev *wdev;
	struct rtw_wdev_priv *pwdev_priv;
	struct net_device *pnetdev = padapter->pnetdev;
	
	DBG_8192C("%s(padapter=%p)\n", __func__, padapter);

	/* wiphy */
	wiphy = wiphy_new(&rtw_cfg80211_ops, sizeof(_adapter*));
	if (!wiphy) {
		DBG_8192C("Couldn't allocate wiphy device\n");
		ret = -ENOMEM;
		goto exit;
	}
	set_wiphy_dev(wiphy, dev);
	*((_adapter**)wiphy_priv(wiphy)) = padapter;
	rtw_cfg80211_preinit_wiphy(padapter, wiphy);

	ret = wiphy_register(wiphy);
	if (ret < 0) {
		DBG_8192C("Couldn't register wiphy device\n");
		goto free_wiphy;
	}

	/*  wdev */
	wdev = (struct wireless_dev *)rtw_zmalloc(sizeof(struct wireless_dev));
	if (!wdev) {
		DBG_8192C("Couldn't allocate wireless device\n");
		ret = -ENOMEM;
		goto unregister_wiphy;
	}
	wdev->wiphy = wiphy;
	wdev->netdev = pnetdev;

	wdev->iftype = NL80211_IFTYPE_STATION; // will be init in rtw_hal_init()
	                                       // Must sync with _rtw_init_mlme_priv() 
										   // pmlmepriv->fw_state = WIFI_STATION_STATE
	//wdev->iftype = NL80211_IFTYPE_MONITOR; // for rtw_setopmode_cmd() in cfg80211_rtw_change_iface()
	padapter->rtw_wdev = wdev;
	pnetdev->ieee80211_ptr = wdev;

	//init pwdev_priv
	pwdev_priv = adapter_wdev_data(padapter);
	pwdev_priv->rtw_wdev = wdev;
	pwdev_priv->pmon_ndev = NULL;
	pwdev_priv->ifname_mon[0] = '\0';
	pwdev_priv->padapter = padapter;
	pwdev_priv->scan_request = NULL;
	_rtw_spinlock_init(&pwdev_priv->scan_req_lock);
		
	pwdev_priv->p2p_enabled = _FALSE;
	pwdev_priv->provdisc_req_issued = _FALSE;
	rtw_wdev_invit_info_init(&pwdev_priv->invit_info);
	rtw_wdev_nego_info_init(&pwdev_priv->nego_info);
		
	pwdev_priv->bandroid_scan = _FALSE;

	if(padapter->registrypriv.power_mgnt != PS_MODE_ACTIVE)
		pwdev_priv->power_mgmt = _TRUE;
	else
		pwdev_priv->power_mgmt = _FALSE;
	
#ifdef CONFIG_CONCURRENT_MODE
	ATOMIC_SET(&pwdev_priv->switch_ch_to, 1);	
	ATOMIC_SET(&pwdev_priv->ro_ch_to, 1);	
#endif

	return ret;

	rtw_mfree((u8*)wdev, sizeof(struct wireless_dev));
unregister_wiphy:
	wiphy_unregister(wiphy);
 free_wiphy:
 	wiphy_free(wiphy);
exit:
	return ret;
	
}

void rtw_wdev_free(struct wireless_dev *wdev)
{
	DBG_8192C("%s(wdev=%p)\n", __func__, wdev);

	if (!wdev)
		return;

	rtw_spt_band_free(wdev->wiphy->bands[IEEE80211_BAND_2GHZ]);
	rtw_spt_band_free(wdev->wiphy->bands[IEEE80211_BAND_5GHZ]);
	
	wiphy_free(wdev->wiphy);

	rtw_mfree((u8*)wdev, sizeof(struct wireless_dev));
}

void rtw_wdev_unregister(struct wireless_dev *wdev)
{
	struct net_device *ndev;
	_adapter *adapter;
	struct rtw_wdev_priv *pwdev_priv;

	DBG_8192C("%s(wdev=%p)\n", __func__, wdev);

	if (!wdev)
		return;

	if(!(ndev = wdev_to_ndev(wdev)))
		return;

	adapter = (_adapter *)rtw_netdev_priv(ndev);
	pwdev_priv = adapter_wdev_data(adapter);

	rtw_cfg80211_indicate_scan_done(adapter, _TRUE);

	if (pwdev_priv->pmon_ndev) {
		DBG_8192C("%s, unregister monitor interface\n", __func__);
		unregister_netdev(pwdev_priv->pmon_ndev);
	}

	wiphy_unregister(wdev->wiphy);
}

#endif //CONFIG_IOCTL_CFG80211

