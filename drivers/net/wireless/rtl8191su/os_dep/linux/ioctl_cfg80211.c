/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
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

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>		
#include <rtl871x_ioctl.h>
#include <rtl871x_ioctl_set.h>
#include <rtl871x_ioctl_query.h>
#include <xmit_osdep.h>

#ifdef CONFIG_IOCTL_CFG80211

#include "ioctl_cfg80211.h"

#define RTW_SCAN_IE_LEN_MAX      2304
#define RTW_MAX_REMAIN_ON_CHANNEL_DURATION 65535 //ms
#define RTW_MAX_NUM_PMKIDS 4

#define RTW_CH_MAX_2G_CHANNEL               14      /* Max channel in 2G band */

static const u32 rtw_cipher_suites[] = {
	WLAN_CIPHER_SUITE_WEP40,
	WLAN_CIPHER_SUITE_WEP104,
	WLAN_CIPHER_SUITE_TKIP,
	WLAN_CIPHER_SUITE_CCMP,
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
#define rtw_a_rates_size	8
#define rtw_g_rates		(rtw_rates + 0)
#define rtw_g_rates_size	12

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

//{{1,2,3,4,5,6,7,8,9,10,11,12,13,36,40,44,48,52,56,60,64,100,104,108,112,116,120,124,128,132,136,140,149,153,157,161,165},37},	// 0x12, RT_CHANNEL_DOMAIN_WORLD_WIDE37

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

static struct ieee80211_supported_band rtw_band_2ghz = {
	.band = IEEE80211_BAND_2GHZ,
	.channels = rtw_2ghz_channels,
	.n_channels = ARRAY_SIZE(rtw_2ghz_channels),
	.bitrates = rtw_g_rates,
	.n_bitrates = rtw_g_rates_size,
};

static struct ieee80211_supported_band rtw_band_5ghz = {
	.band = IEEE80211_BAND_5GHZ,
	.channels = rtw_5ghz_a_channels,
	.n_channels = ARRAY_SIZE(rtw_5ghz_a_channels),
	.bitrates = rtw_a_rates,
	.n_bitrates = rtw_a_rates_size,
};

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
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

static int rtw_cfg80211_inform_bss(_adapter *padapter, struct wlan_network *pnetwork)
{
	int ret=0;	
	struct ieee80211_channel *notify_channel;
	struct cfg80211_bss *bss;
	//struct ieee80211_supported_band *band;       
	u16 channel;
	u32 freq;
	u64 notify_timestamp;
	u16 notify_capability;
	u16 notify_interval;
	u8 *notify_ie;
	size_t notify_ielen;
	s32 notify_signal;
	u8 buf[768], *pbuf;
	size_t len;
	struct rtw_ieee80211_hdr *pwlanhdr;
	unsigned short *fctrl;
	u8	bc_addr[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	long rssi;
		
	struct wireless_dev *wdev = padapter->rtw_wdev;
	struct wiphy *wiphy = wdev->wiphy;


	//printk("%s\n", __func__);
	

   	channel = pnetwork->network.Configuration.DSConfig;
	if (channel <= RTW_CH_MAX_2G_CHANNEL)
	{
		//band = wiphy->bands[IEEE80211_BAND_2GHZ];
		freq = rtw_ieee80211_channel_to_frequency(channel, IEEE80211_BAND_2GHZ);
	}	
	else
	{
		//band = wiphy->bands[IEEE80211_BAND_5GHZ];
		freq = rtw_ieee80211_channel_to_frequency(channel, IEEE80211_BAND_5GHZ);
	}	
	
	notify_channel = ieee80211_get_channel(wiphy, freq);

	//rtw_get_timestampe_from_ie()
	notify_timestamp = jiffies_to_msecs(jiffies)*1000; /* uSec */

	notify_interval = le16_to_cpu(*(u16*)get_beacon_interval_from_ie(pnetwork->network.IEs));
	notify_capability = le16_to_cpu(*(u16*)get_capability_from_ie(pnetwork->network.IEs));		

	
	notify_ie = pnetwork->network.IEs+_FIXED_IE_LENGTH_;
	notify_ielen = pnetwork->network.IELength-_FIXED_IE_LENGTH_;
	   
	//notify_signal = (s16)le16_to_cpu(bi->RSSI) * 100;

	//We've set wiphy's signal_type as CFG80211_SIGNAL_TYPE_MBM: signal strength in mBm (100*dBm)
	//notify_signal = 100*( pnetwork->network.Rssi );
	rssi = signal_scale_mapping(pnetwork->network.Rssi);
	notify_signal = 100 * ((u8) ( (rssi+1) >> 1 ) - 95 );
		
/*
	printk("bssid: %2.2X:%2.2X:%2.2X:%2.2X:%2.2X:%2.2X\n",
                        pnetwork->network.MacAddress[0], pnetwork->network.MacAddress[1], pnetwork->network.MacAddress[2],
                        pnetwork->network.MacAddress[3], pnetwork->network.MacAddress[4], pnetwork->network.MacAddress[5]);
	printk("Channel: %d(%d)\n", channel, freq);
	printk("Capability: %X\n", notify_capability);
	printk("Beacon interval: %d\n", notify_interval);
	printk("Signal: %d\n", notify_signal);
	printk("notify_timestamp: %#018llx\n", notify_timestamp);
*/

	pbuf = buf;
	pwlanhdr = (struct rtw_ieee80211_hdr *)pbuf;	
	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;	

	SetSeqNum(pwlanhdr, 0/*pmlmeext->mgnt_seq*/);
	//pmlmeext->mgnt_seq++;

    if (pnetwork->network.Reserved[0] == 1) { // WIFI_BEACON

		_memcpy(pwlanhdr->addr1, bc_addr, ETH_ALEN);		
		
		SetFrameSubType(pbuf, WIFI_BEACON);
		
	} else {

		_memcpy(pwlanhdr->addr1, myid(&(padapter->eeprompriv)), ETH_ALEN);		
		
		SetFrameSubType(pbuf, WIFI_PROBERSP);
	}

	_memcpy(pwlanhdr->addr2, pnetwork->network.MacAddress, ETH_ALEN);
	_memcpy(pwlanhdr->addr3, pnetwork->network.MacAddress, ETH_ALEN);


	pbuf += sizeof(struct rtw_ieee80211_hdr_3addr);	
	len = sizeof (struct rtw_ieee80211_hdr_3addr);

	_memcpy(pbuf, pnetwork->network.IEs, pnetwork->network.IELength);
	len += pnetwork->network.IELength;
	

#if 1	
	bss = cfg80211_inform_bss_frame(wiphy, notify_channel, (struct ieee80211_mgmt *)buf,
		len, notify_signal, GFP_ATOMIC);
#else			 
			
	bss = cfg80211_inform_bss(wiphy, notify_channel, (const u8 *)pnetwork->network.MacAddress,
                notify_timestamp, notify_capability, notify_interval, notify_ie,
                notify_ielen, notify_signal, GFP_ATOMIC/*GFP_KERNEL*/);
#endif

	if (unlikely(!bss)) {
		printk("rtw_cfg80211_inform_bss error\n");
		return -EINVAL;
	}

	return ret;

}

void rtw_cfg80211_indicate_connect(_adapter *padapter)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct wlan_network  *cur_network = &(pmlmepriv->cur_network);
	struct wireless_dev *pwdev = padapter->rtw_wdev;

	printk("%s(padapter=%p)\n", __func__, padapter);

	if (pwdev->iftype != NL80211_IFTYPE_STATION
		#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
		&& pwdev->iftype != NL80211_IFTYPE_P2P_CLIENT
		#endif
	) {
		return;
	}

	if(check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE)
		return;
	
	#ifdef CONFIG_LAYER2_ROAMING
	if(pmlmepriv->to_roaming > 0) {
		//rtw_cfg80211_inform_bss(padapter, cur_network);
		DBG_871X("%s call cfg80211_roamed\n", __FUNCTION__);
		cfg80211_roamed(padapter->pnetdev,
			#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 39)
			NULL,
			#endif
			cur_network->network.MacAddress,
			cur_network->network.IEs+_FIXED_IE_LENGTH_, cur_network->network.IELength-_FIXED_IE_LENGTH_,
			NULL, 0, GFP_ATOMIC
		);
	}
	else 
	#endif
	{
		cfg80211_connect_result(padapter->pnetdev, cur_network->network.MacAddress, NULL, 0, NULL, 0, 
							WLAN_STATUS_SUCCESS, GFP_ATOMIC/*GFP_KERNEL*/);
	}
}

void rtw_cfg80211_indicate_disconnect(_adapter *padapter)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct wireless_dev *pwdev = padapter->rtw_wdev;

	printk("%s(padapter=%p)\n", __func__, padapter);

	if (pwdev->iftype != NL80211_IFTYPE_STATION 
		#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
		&& pwdev->iftype != NL80211_IFTYPE_P2P_CLIENT
		#endif
	) {
		return;
	}

	if(pwdev->sme_state==CFG80211_SME_CONNECTING)
		cfg80211_connect_result(padapter->pnetdev, NULL, NULL, 0, NULL, 0, 
							WLAN_STATUS_UNSPECIFIED_FAILURE, GFP_ATOMIC/*GFP_KERNEL*/);
	else if(pwdev->sme_state==CFG80211_SME_CONNECTED)
		cfg80211_disconnected(padapter->pnetdev, 0,
			   				NULL, 0, GFP_ATOMIC);
	else
		printk("pwdev->sme_state=%d\n", pwdev->sme_state);

}
 	
static int rtw_cfg80211_set_encryption(struct net_device *dev, struct ieee_param *param, u32 param_len)
{
	int ret = 0;
	u32 wep_key_idx, wep_key_len,wep_total_len;
	NDIS_802_11_WEP	 *pwep = NULL;	
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct mlme_priv 	*pmlmepriv = &padapter->mlmepriv;		
	struct security_priv *psecuritypriv = &padapter->securitypriv;

_func_enter_;

	printk("%s\n", __func__);

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
		if (param->u.crypt.idx >= WEP_KEYS)
		{
			ret = -EINVAL;			
			goto exit;
		}
	} else {
		ret = -EINVAL;		
		goto exit;
	}

	if (strcmp(param->u.crypt.alg, "WEP") == 0)
	{
		RT_TRACE(_module_rtl871x_ioctl_os_c,_drv_err_,("wpa_set_encryption, crypt.alg = WEP\n"));
		printk("wpa_set_encryption, crypt.alg = WEP\n");

		padapter->securitypriv.ndisencryptstatus = Ndis802_11Encryption1Enabled;
		padapter->securitypriv.dot11PrivacyAlgrthm=_WEP40_;
		padapter->securitypriv.dot118021XGrpPrivacy=_WEP40_;

		wep_key_idx = param->u.crypt.idx;
		wep_key_len = param->u.crypt.key_len;

		RT_TRACE(_module_rtl871x_ioctl_os_c,_drv_info_,("(1)wep_key_idx=%d\n", wep_key_idx));
		printk("(1)wep_key_idx=%d\n", wep_key_idx);

		if (wep_key_idx > WEP_KEYS)
			return -EINVAL;

		RT_TRACE(_module_rtl871x_ioctl_os_c,_drv_info_,("(2)wep_key_idx=%d\n", wep_key_idx));

		if (wep_key_len > 0) 
		{
		 	wep_key_len = wep_key_len <= 5 ? 5 : 13;
			wep_total_len = wep_key_len + FIELD_OFFSET(NDIS_802_11_WEP, KeyMaterial);
		 	pwep =(NDIS_802_11_WEP	 *) _malloc(wep_total_len);
			if(pwep == NULL){
				RT_TRACE(_module_rtl871x_ioctl_os_c,_drv_err_,(" wpa_set_encryption: pwep allocate fail !!!\n"));
				goto exit;
			}

		 	_memset(pwep, 0, wep_total_len);

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

		_memcpy(pwep->KeyMaterial,  param->u.crypt.key, pwep->KeyLength);

		if(param->u.crypt.set_tx)
		{
			printk("wep, set_tx=1\n");

			if(set_802_11_add_wep(padapter, pwep) == (u8)_FAIL)
			{
				ret = -EOPNOTSUPP ;
			}
		}
		else
		{
			printk("wep, set_tx=0\n");
			
			//don't update "psecuritypriv->dot11PrivacyAlgrthm" and 
			//"psecuritypriv->dot11PrivacyKeyIndex=keyid", but can rtw_set_key to fw/cam
			
			if (wep_key_idx >= WEP_KEYS) {
				ret = -EOPNOTSUPP ;
				goto exit;
			}				
			
		      _memcpy(&(psecuritypriv->dot11DefKey[wep_key_idx].skey[0]), pwep->KeyMaterial, pwep->KeyLength);
			psecuritypriv->dot11DefKeylen[wep_key_idx]=pwep->KeyLength;	
			//set_key(padapter, psecuritypriv, wep_key_idx, 0);
			set_key(padapter, psecuritypriv, wep_key_idx);
		}

		goto exit;		
	}

	if(padapter->securitypriv.dot11AuthAlgrthm == dot11AuthAlgrthm_8021X) // 802_1x
	{
		struct sta_info * psta,*pbcmc_sta;
		struct sta_priv * pstapriv = &padapter->stapriv;

		//printk("%s, : dot11AuthAlgrthm == dot11AuthAlgrthm_8021X \n", __func__);

		if (check_fwstate(pmlmepriv, WIFI_STATION_STATE | WIFI_MP_STATE) == _TRUE) //sta mode
		{
			psta = get_stainfo(pstapriv, get_bssid(pmlmepriv));				
			if (psta == NULL) {
				//DEBUG_ERR( ("Set wpa_set_encryption: Obtain Sta_info fail \n"));
				printk("%s, : Obtain Sta_info fail \n", __func__);
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

					printk("%s, : param->u.crypt.set_tx ==1 \n", __func__);
					
					_memcpy(psta->dot118021x_UncstKey.skey,  param->u.crypt.key, (param->u.crypt.key_len>16 ?16:param->u.crypt.key_len));
					
					if(strcmp(param->u.crypt.alg, "TKIP") == 0)//set mic key
					{						
						//DEBUG_ERR(("\nset key length :param->u.crypt.key_len=%d\n", param->u.crypt.key_len));
						_memcpy(psta->dot11tkiptxmickey.skey, &(param->u.crypt.key[16]), 8);
						_memcpy(psta->dot11tkiprxmickey.skey, &(param->u.crypt.key[24]), 8);

						padapter->securitypriv.busetkipkey=_FALSE;
						_set_timer(&padapter->securitypriv.tkip_timer, 50);						
					}

					//DEBUG_ERR(("\n param->u.crypt.key_len=%d\n",param->u.crypt.key_len));
					//DEBUG_ERR(("\n ~~~~stastakey:unicastkey\n"));
					printk("\n ~~~~stastakey:unicastkey\n");
					
					setstakey_cmd(padapter, (unsigned char *)psta, _TRUE);
				}
				else//group key
				{ 					
					if( ( 0 < param->u.crypt.idx ) &&  ( param->u.crypt.idx < 3 ) )
					{  //group key idx is 1 or 2
				
						_memcpy(padapter->securitypriv.dot118021XGrpKey[param->u.crypt.idx-1].skey,  param->u.crypt.key,(param->u.crypt.key_len>16 ?16:param->u.crypt.key_len));
						_memcpy(padapter->securitypriv.dot118021XGrptxmickey[param->u.crypt.idx-1].skey,&(param->u.crypt.key[16]),8);
						_memcpy(padapter->securitypriv.dot118021XGrprxmickey[param->u.crypt.idx-1].skey,&(param->u.crypt.key[24]),8);

						padapter->securitypriv.binstallGrpkey = _TRUE;
						//DEBUG_ERR(("\n param->u.crypt.key_len=%d\n", param->u.crypt.key_len));
						//DEBUG_ERR(("\n ~~~~stastakey:groupkey\n"));
						printk("\n ~~~~stastakey:groupkey\n");

						set_key(padapter,&padapter->securitypriv,param->u.crypt.idx);
						
#ifdef CONFIG_PWRCTRL
						if(padapter->registrypriv.power_mgnt > PS_MODE_ACTIVE){
							if(padapter->registrypriv.power_mgnt != padapter->pwrctrlpriv.pwr_mode){
								_set_timer(&(padapter->mlmepriv.dhcp_timer), 60000);
							}
						}
#endif
					}
					
				}						
			}

			pbcmc_sta=get_bcmc_stainfo(padapter);
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

exit:

	printk("%s, ret=%d\n", __func__, ret);
	
	if (pwep) {
		_mfree((u8 *)pwep,wep_total_len);		
	}	
	
	_func_exit_;
	
	return ret;	

}

static int cfg80211_rtw_add_key(struct wiphy *wiphy, struct net_device *ndev,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
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
	struct wireless_dev *rtw_wdev = wiphy_to_wdev(wiphy);
	_adapter *padapter = wiphy_to_adapter(wiphy);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	
	
	printk("%s(netdev=%p), Adding key for %pM\n", __func__, ndev, mac_addr);

	printk("cipher=0x%x\n", params->cipher);

	printk("key_len=0x%x\n", params->key_len);

	printk("seq_len=0x%x\n", params->seq_len);

	printk("key_index=%d\n", key_index);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
	printk("pairwise=%d\n", pairwise);
#endif	// (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))


	param_len = sizeof(struct ieee_param) + params->key_len;
	param = (struct ieee_param *)_malloc(param_len);
	if (param == NULL)
		return -1;
	
	_memset(param, 0, param_len);

	param->cmd = IEEE_CMD_SET_ENCRYPTION;
	_memset(param->sta_addr, 0xff, ETH_ALEN);

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
	default:	
		return -ENOTSUPP;
	}
	
	strncpy((char *)param->u.crypt.alg, alg_name, IEEE_CRYPT_ALG_NAME_LEN);
	

	if (!mac_addr || is_broadcast_ether_addr(mac_addr))
	{
		param->u.crypt.set_tx = 0;
	} else {
		param->u.crypt.set_tx = 1;
	}
	
	
	//param->u.crypt.idx = key_index - 1;
	param->u.crypt.idx = key_index;
	
	if (params->seq_len && params->seq) 
	{	
		_memcpy(param->u.crypt.seq, params->seq, params->seq_len);
	}

	if(params->key_len && params->key)
	{
		param->u.crypt.key_len = params->key_len;		
		_memcpy(param->u.crypt.key, params->key, params->key_len);
	}	

	//if(rtw_wdev->iftype == NL80211_IFTYPE_STATION)
	if(check_fwstate(pmlmepriv, WIFI_STATION_STATE) == _TRUE)
	{
		ret =  rtw_cfg80211_set_encryption(ndev, param, param_len);	
	}
	else if(check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE)//else if(rtw_wdev->iftype == NL80211_IFTYPE_AP)
	{
#ifdef CONFIG_AP_MODE
		if(mac_addr)
			_memcpy(param->sta_addr, (void*)mac_addr, ETH_ALEN);
	
		ret = rtw_cfg80211_ap_set_encryption(ndev, param, param_len);
#endif
	}
	else
	{
		printk("error! fw_state=0x%x, iftype=%d\n", pmlmepriv->fw_state, rtw_wdev->iftype);
		
	}

	if(param)
	{
		_mfree((u8*)param, param_len);
	}

	return ret;

}

static int cfg80211_rtw_get_key(struct wiphy *wiphy, struct net_device *ndev,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
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
	printk("%s\n", __func__);
	return 0;
}

static int cfg80211_rtw_del_key(struct wiphy *wiphy, struct net_device *ndev,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
				u8 key_index, bool pairwise, const u8 *mac_addr)
#else	// (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
				u8 key_index, const u8 *mac_addr)
#endif	// (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
{
#if 0
	struct iwm_priv *iwm = ndev_to_iwm(ndev);
	struct iwm_key *key = &iwm->keys[key_index];

	if (!iwm->keys[key_index].key_len) {
		IWM_DBG_WEXT(iwm, DBG, "Key %d not used\n", key_index);
		return 0;
	}

	if (key_index == iwm->default_key)
		iwm->default_key = -1;

	return iwm_set_key(iwm, 1, key);
#endif	
	printk("%s\n", __func__);
	return 0;
}

static int cfg80211_rtw_set_default_key(struct wiphy *wiphy,
					struct net_device *ndev,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,38))
					u8 key_index, bool unicast, bool multicast)
#else	// (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,38))
					u8 key_index)
#endif	// (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,38))
{
#if 0
	struct iwm_priv *iwm = ndev_to_iwm(ndev);

	IWM_DBG_WEXT(iwm, DBG, "Default key index is: %d\n", key_index);

	if (!iwm->keys[key_index].key_len) {
		IWM_ERR(iwm, "Key %d not used\n", key_index);
		return -EINVAL;
	}

	iwm->default_key = key_index;

	return iwm_set_tx_key(iwm, key_index);
#endif	
	printk("%s\n", __func__);
	return 0;
}

static int cfg80211_rtw_get_station(struct wiphy *wiphy,
				    struct net_device *ndev,
				    u8 *mac, struct station_info *sinfo)
{

	_adapter *padapter = wiphy_to_adapter(wiphy);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	
	if(!mac) {
		printk("%s, mac==%p\n", __func__, mac);
		return -ENOENT;
	}

	printk("%s, mac="MAC_FMT"\n", __func__, MAC_ARG(mac));

	//for infra./P2PClient mode
	if(	check_fwstate(pmlmepriv, WIFI_STATION_STATE)
		&& check_fwstate(pmlmepriv, _FW_LINKED)
	)
	{
		struct wlan_network  *cur_network = &(pmlmepriv->cur_network);
		
		if (_memcmp(mac, cur_network->network.MacAddress, ETH_ALEN) == _FALSE)
		{
			printk("%s, mismatch bssid="MAC_FMT"\n", __func__, MAC_ARG(cur_network->network.MacAddress));
			return -ENOENT;
		}	

		sinfo->filled |= STATION_INFO_SIGNAL;
		//sinfo->signal = translate_percentage_to_dbm(padapter->recvpriv.signal_strength);
		//sinfo->signal = (u8) ((cur_network->network.Rssi+1)>>1)-95;
		sinfo->signal = cur_network->network.Rssi;

		sinfo->filled |= STATION_INFO_TX_BITRATE;
		sinfo->txrate.legacy = 10 * rtw_get_network_max_rate(padapter, &pmlmepriv->cur_network.network);
	}
	
	//for Ad-Hoc/AP mode
	if (	(	check_fwstate(pmlmepriv, WIFI_ADHOC_STATE)
			||check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE)
			||check_fwstate(pmlmepriv, WIFI_AP_STATE) )
		&& check_fwstate(pmlmepriv, _FW_LINKED)
	)
	{
		struct sta_info *psta = NULL;	
		struct sta_priv *pstapriv = &padapter->stapriv;
	
		psta = get_stainfo(pstapriv, mac);
		if(psta == NULL)
		{
			printk("%s, sta_info is null\n", __func__);
			return -ENOENT;
		}			
		
		//TODO: should acquire station info...
	}

	return 0;
}

static int cfg80211_rtw_change_iface(struct wiphy *wiphy,
				     struct net_device *ndev,
				     enum nl80211_iftype type, u32 *flags,
				     struct vif_params *params)
{
#if 0
	enum nl80211_iftype old_type;
	NDIS_802_11_NETWORK_INFRASTRUCTURE networkType ;
	_adapter *padapter = wiphy_to_adapter(wiphy);
	struct wireless_dev *rtw_wdev = wiphy_to_wdev(wiphy);
	struct wifidirect_info *pwdinfo= &(padapter->wdinfo);		
	int ret = 0;
	u8 change = _FALSE;

	if(_FAIL == rtw_pwr_wakeup(padapter)) {
		ret= -EPERM;
		goto exit;
	}

	old_type = rtw_wdev->iftype;
	printk("%s, old_iftype=%d, new_iftype=%d\n", __func__, old_type, type);



	if(old_type != type)
		change = _TRUE;
		

	switch (type) {
	case NL80211_IFTYPE_ADHOC:
		networkType = Ndis802_11IBSS;
		break;
	case NL80211_IFTYPE_STATION:
		networkType = Ndis802_11Infrastructure;
		if(change && rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO))
		{

			_cancel_timer_ex( &pwdinfo->find_phase_timer );
			_cancel_timer_ex( &pwdinfo->restore_p2p_state_timer );
			_cancel_timer_ex( &pwdinfo->pre_tx_scan_timer);

			//it means remove GO and change mode from AP(GO) to station(P2P DEVICE)
			rtw_p2p_set_role(pwdinfo, P2P_ROLE_DEVICE);
			rtw_p2p_set_state(pwdinfo, rtw_p2p_pre_state(pwdinfo));

			printk("%s, role=%d, p2p_state=%d, pre_p2p_state=%d\n", __func__, rtw_p2p_role(pwdinfo), rtw_p2p_state(pwdinfo), rtw_p2p_pre_state(pwdinfo));
	
		}
		break;
	case NL80211_IFTYPE_AP:
		networkType = Ndis802_11APMode;
		if(change && !rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE))
		{
			//it means P2P Group created, we will be GO and change mode from  P2P DEVICE to AP(GO)
			rtw_p2p_set_role(pwdinfo, P2P_ROLE_GO);
		}	
		break;		
	default:
		return -EOPNOTSUPP;
	}

	rtw_wdev->iftype = type;
	
	if (rtw_set_802_11_infrastructure_mode(padapter, networkType) ==_FALSE)
	{
		rtw_wdev->iftype = old_type;
		ret = -EPERM;
		goto exit;
	}

	rtw_setopmode_cmd(padapter, networkType);	
	
exit:

	return ret;
#endif
	printk("%s \n", __FUNCTION__);
	return 0;
}

void rtw_cfg80211_indicate_scan_done(struct rtw_wdev_priv *pwdev_priv, bool aborted)
{
	_irqL	irqL;

	printk("%s \n", __FUNCTION__);

	_enter_critical(&pwdev_priv->scan_req_lock, &irqL);
	if(pwdev_priv->scan_request != NULL)
	{
		//struct cfg80211_scan_request *scan_request = pwdev_priv->scan_request;
	
		//avoid WARN_ON(request != wiphy_to_dev(request->wiphy)->scan_req);
		//if(scan_request == wiphy_to_dev(scan_request->wiphy)->scan_req)

		cfg80211_scan_done(pwdev_priv->scan_request, aborted);
		pwdev_priv->scan_request = NULL;
		
	} else {
		printk("%s without scan req\n", __FUNCTION__);
	}
	_exit_critical(&pwdev_priv->scan_req_lock, &irqL);

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

	struct rtw_wdev_priv *pwdev_priv = wdev_to_priv(padapter->rtw_wdev);
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;

	_enter_critical(&(pmlmepriv->scanned_queue.lock), &irqL);

	phead = get_list_head(queue);
	plist = get_next(phead);
       
	while(1)
	{
		if (end_of_queue_search(phead,plist)== _TRUE)
			break;

		pnetwork = LIST_CONTAINOR(plist, struct wlan_network, list);

#ifdef CONFIG_VALIDATE_SSID
		if(validate_ssid(&(pnetwork->network.Ssid))==_TRUE)
#endif
		{
			//ev = translate_scan(padapter, a, pnetwork, ev, stop);
			rtw_cfg80211_inform_bss(padapter, pnetwork);		
		}

		plist = get_next(plist);
	
	}
	
	_exit_critical(&(pmlmepriv->scanned_queue.lock), &irqL);
	
	//call this after other things have been done
	rtw_indicate_scan_done(padapter, _FALSE);

}

static int cfg80211_rtw_scan(struct wiphy *wiphy, struct net_device *ndev,
			     struct cfg80211_scan_request *request)
{
#define RTW_CFG80211_SCAN_AMOUNT 1
	int i;
	u8 _status = _FALSE;
	int ret = 0;	
	_adapter *padapter = wiphy_to_adapter(wiphy);
	struct mlme_priv *pmlmepriv= &padapter->mlmepriv;
	NDIS_802_11_SSID ssid[RTW_CFG80211_SCAN_AMOUNT];
	_irqL	irqL;

	struct rtw_wdev_priv *pwdev_priv = wdev_to_priv(padapter->rtw_wdev);
	struct cfg80211_ssid *ssids = request->ssids;
	bool need_indicate_scan_done = _FALSE;

#ifdef CONFIG_MP_INCLUDED
	if (check_fwstate(pmlmepriv, WIFI_MP_STATE) == _TRUE)
	{
		ret = -EPERM;
		goto exit;
	}
#endif

	_enter_critical(&pwdev_priv->scan_req_lock, &irqL);
	pwdev_priv->scan_request = request;
	_exit_critical(&pwdev_priv->scan_req_lock, &irqL);

	if (padapter->bDriverStopped == _TRUE) {
		printk("!r8711_wx_set_scan: bDriverStopped=%d\n", padapter->bDriverStopped);
		ret = -1;
		goto exit;
	}

	if (padapter->bup == _FALSE) {
		ret = -1;
		goto exit;
	}

	if (padapter->hw_init_completed == _FALSE) {
		ret = -1;
		goto exit;
	}

	if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY|_FW_UNDER_LINKING) == _TRUE)
	{
		printk("%s, fwstate=0x%x\n", __func__, pmlmepriv->fw_state);
		need_indicate_scan_done = _TRUE;
		goto check_need_indicate_scan_done;
	} 

	_memset(ssid, 0, sizeof(NDIS_802_11_SSID)*RTW_CFG80211_SCAN_AMOUNT);
	//parsing request ssids, n_ssids
	for (i = 0; i < request->n_ssids && i < RTW_CFG80211_SCAN_AMOUNT; i++) {
		#ifdef CONFIG_DEBUG_CFG80211
		printk("ssid=%s, len=%d\n", ssids[i].ssid, ssids[i].ssid_len);
		#endif
		_memcpy(ssid[i].Ssid, ssids[i].ssid, ssids[i].ssid_len);
		ssid[i].SsidLength = ssids[i].ssid_len;	
	}

	_enter_critical(&pmlmepriv->lock, &irqL);	
	_status = sitesurvey_cmd(padapter, ssid);
	_exit_critical(&pmlmepriv->lock, &irqL);

	if(_status == _FALSE)
	{
		ret = -1;
	}

check_need_indicate_scan_done:
	if(need_indicate_scan_done)
		rtw_cfg80211_surveydone_event_callback(padapter);

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
	printk("%s\n", __func__);
	return 0;
}

static int cfg80211_rtw_join_ibss(struct wiphy *wiphy, struct net_device *dev,
				  struct cfg80211_ibss_params *params)
{
#if 0
	struct iwm_priv *iwm = wiphy_to_iwm(wiphy);
	struct ieee80211_channel *chan = params->channel;

	if (!test_bit(IWM_STATUS_READY, &iwm->status))
		return -EIO;

	/* UMAC doesn't support creating or joining an IBSS network
	 * with specified bssid. */
	if (params->bssid)
		return -EOPNOTSUPP;

	iwm->channel = ieee80211_frequency_to_channel(chan->center_freq);
	iwm->umac_profile->ibss.band = chan->band;
	iwm->umac_profile->ibss.channel = iwm->channel;
	iwm->umac_profile->ssid.ssid_len = params->ssid_len;
	memcpy(iwm->umac_profile->ssid.ssid, params->ssid, params->ssid_len);

	return iwm_send_mlme_profile(iwm);
#endif	
	printk("%s\n", __func__);
	return 0;
}

static int cfg80211_rtw_leave_ibss(struct wiphy *wiphy, struct net_device *dev)
{
#if 0
	struct iwm_priv *iwm = wiphy_to_iwm(wiphy);

	if (iwm->umac_profile_active)
		return iwm_invalidate_mlme_profile(iwm);
#endif
	printk("%s\n", __func__);
	return 0;
}

static int rtw_cfg80211_set_wpa_version(struct security_priv *psecuritypriv, u32 wpa_version)
{
	printk("%s, wpa_version=%d\n", __func__, wpa_version);

	
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

	return 0;

}

static int rtw_cfg80211_set_auth_type(struct security_priv *psecuritypriv,
			     enum nl80211_auth_type sme_auth_type)
{
	printk("%s, nl80211_auth_type=%d\n", __func__, sme_auth_type);


	switch (sme_auth_type) {
	case NL80211_AUTHTYPE_AUTOMATIC:

		psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_Auto;

		break;
	case NL80211_AUTHTYPE_OPEN_SYSTEM:
	
		psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_Open;

		if(psecuritypriv->ndisauthtype>Ndis802_11AuthModeWPA)
			psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_8021X;
		
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

	printk("%s, ucast=%d, cipher=0x%x\n", __func__, ucast, cipher);


	if (!cipher) {
		*profile_cipher = _NO_PRIVACY_;
		psecuritypriv->ndisencryptstatus = ndisencryptstatus;
		return 0;
	}
	
	switch (cipher) {
	case IW_AUTH_CIPHER_NONE:
		*profile_cipher = _NO_PRIVACY_;
		ndisencryptstatus = Ndis802_11EncryptionDisabled;
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
	default:
		printk("Unsupported cipher: 0x%x\n", cipher);
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
	printk("%s, key_mgt=0x%x\n", __func__, key_mgt);

	if (key_mgt == WLAN_AKM_SUITE_8021X)
		//*auth_type = UMAC_AUTH_TYPE_8021X;
		psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_8021X;
	else if (key_mgt == WLAN_AKM_SUITE_PSK) {
		psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_8021X;
	} else {
		printk("Invalid key mgt: 0x%x\n", key_mgt);		
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


//	if((ielen > MAX_WPA_IE_LEN+MAX_WPS_IE_LEN+MAX_P2P_IE_LEN) || (pie == NULL)){
	if(pie == NULL){
		padapter->securitypriv.wps_phase = _FALSE;	
		if(pie == NULL)	
			return ret;
		else
			return -EINVAL;
	}

	if(ielen)
	{		
		buf = _malloc(ielen);
		if (buf == NULL){
			ret =  -ENOMEM;
			goto exit;
		}
	
		_memcpy(buf, pie , ielen);

		//dump
		{
			int i;
			printk("set wpa_ie(length:%d):\n", ielen);
			for(i=0;i<ielen;i=i+8)
				printk("0x%.2x 0x%.2x 0x%.2x 0x%.2x 0x%.2x 0x%.2x 0x%.2x 0x%.2x \n",buf[i],buf[i+1],buf[i+2],buf[i+3],buf[i+4],buf[i+5],buf[i+6],buf[i+7]);
		}
	
		pos = buf;
		if(ielen < RSN_HEADER_LEN){
			RT_TRACE(_module_rtl871x_ioctl_os_c,_drv_err_,("Ie len too short %d\n", ielen));
			ret  = -1;
			goto exit;
		}

#if 0
		pos += RSN_HEADER_LEN;
		left  = ielen - RSN_HEADER_LEN;
		
		if (left >= RSN_SELECTOR_LEN){
			pos += RSN_SELECTOR_LEN;
			left -= RSN_SELECTOR_LEN;
		}		
		else if (left > 0){
			RT_TRACE(_module_rtl871x_ioctl_os_c,_drv_err_,("Ie length mismatch, %u too much \n", left));
			ret =-1;
			goto exit;
		}
#endif

		pwpa = get_wpa_ie(buf, &wpa_ielen, ielen);
		pwpa2 = get_wpa2_ie(buf, &wpa2_ielen, ielen);

		if(pwpa && wpa_ielen>0)
		{
			if(parse_wpa_ie(pwpa, wpa_ielen+2, &group_cipher, &pairwise_cipher) == _SUCCESS)
			{
				padapter->securitypriv.dot11AuthAlgrthm= dot11AuthAlgrthm_8021X;
				padapter->securitypriv.ndisauthtype=Ndis802_11AuthModeWPAPSK;
				_memcpy(padapter->securitypriv.supplicant_ie, &pwpa[0], wpa_ielen+2);
				
				printk("got wpa_ie\n");
			}
		}

		if(pwpa2 && wpa2_ielen>0)
		{
			if(parse_wpa2_ie(pwpa2, wpa2_ielen+2, &group_cipher, &pairwise_cipher) == _SUCCESS)
			{
				padapter->securitypriv.dot11AuthAlgrthm= dot11AuthAlgrthm_8021X;
				padapter->securitypriv.ndisauthtype=Ndis802_11AuthModeWPA2PSK;	
				_memcpy(padapter->securitypriv.supplicant_ie, &pwpa2[0], wpa2_ielen+2);

				printk("got wpa2_ie\n");
			}
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
		

		padapter->securitypriv.wps_phase = _FALSE;			
		{//set wps_ie	
			u16 cnt = 0;	
			u8 eid, wps_oui[4]={0x0,0x50,0xf2,0x04};
			int wps_ielen=0;			
			u8 *pwps;
			
			pwps = get_wps_ie(buf, ielen, NULL, &wps_ielen);
			 
			//while( cnt < ielen )
			while( cnt < wps_ielen )
			{
				//eid = buf[cnt];
				eid = pwps[cnt];
		
				if((eid==_VENDOR_SPECIFIC_IE_)&&(_memcmp(&pwps[cnt+2], wps_oui, 4)==_TRUE))
				{
					printk("SET WPS_IE\n");

					padapter->securitypriv.wps_ie_len = ( (pwps[cnt+1]+2) < (MAX_WPA_IE_LEN<<2)) ? (pwps[cnt+1]+2):(MAX_WPA_IE_LEN<<2);
					
					_memcpy(padapter->securitypriv.wps_ie, &pwps[cnt], padapter->securitypriv.wps_ie_len);
					
					if(pwpa==NULL && pwpa2==NULL)
					{
						padapter->securitypriv.wps_phase = _TRUE;
					
						printk("SET WPS_IE, wps_phase==_TRUE\n");
					}					

					cnt += pwps[cnt+1]+2;
					
					break;
				} else {
					cnt += pwps[cnt+1]+2; //goto next	
				}				
			}			
		}//set wps_ie

	}

	RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_info_,
		 ("rtw_set_wpa_ie: pairwise_cipher=0x%08x padapter->securitypriv.ndisencryptstatus=%d padapter->securitypriv.ndisauthtype=%d\n",
		  pairwise_cipher, padapter->securitypriv.ndisencryptstatus, padapter->securitypriv.ndisauthtype));
 	
exit:

	if (buf) _mfree(buf, ielen);

	return ret;
}

static int cfg80211_rtw_connect(struct wiphy *wiphy, struct net_device *dev,
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
	_adapter *padapter = wiphy_to_adapter(wiphy);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;	
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	_queue *queue = &pmlmepriv->scanned_queue;	
	
	printk("\n=>%s(netdev=%p)\n",__FUNCTION__, dev);
	

	printk("privacy=%d, key=%p, key_len=%d, key_idx=%d\n", sme->privacy, sme->key, sme->key_len, sme->key_idx);

	if(check_fwstate(pmlmepriv, WIFI_AP_STATE)) {
		ret = -EPERM;
		goto exit;
	}

	if (!sme->ssid || !sme->ssid_len)
	{
		ret = -EINVAL;
		goto exit;
	}

	if (sme->ssid_len > IW_ESSID_MAX_SIZE){

		ret= -E2BIG;
		goto exit;
	}
	

	_memset(&ndis_ssid, 0, sizeof(NDIS_802_11_SSID));			
	ndis_ssid.SsidLength = sme->ssid_len;
	_memcpy(ndis_ssid.Ssid, sme->ssid, sme->ssid_len);

	printk("ssid=%s, len=%d\n", ndis_ssid.Ssid, sme->ssid_len);
	

	if (sme->bssid)
		printk("bssid="MAC_FMT"\n", MAC_ARG(sme->bssid));


	if(check_fwstate(pmlmepriv, _FW_UNDER_SURVEY|_FW_UNDER_LINKING) == _TRUE) 
	{	
		ret = -EBUSY;
		printk("%s, fw_state=0x%x, goto exit\n", __FUNCTION__, pmlmepriv->fw_state);
		goto exit;		
	}	
	

	_enter_critical(&queue->lock, &irqL);
	
	phead = get_list_head(queue);
	pmlmepriv->pscanned = get_next(phead);

	while (1)
	{			
		if (end_of_queue_search(phead, pmlmepriv->pscanned) == _TRUE)
		{
			break;
		}
	
		pnetwork = LIST_CONTAINOR(pmlmepriv->pscanned, struct wlan_network, list);
		pmlmepriv->pscanned = get_next(pmlmepriv->pscanned);

		dst_ssid = pnetwork->network.Ssid.Ssid;
		dst_bssid = pnetwork->network.MacAddress;

		if(sme->bssid)  {
			if(_memcmp(pnetwork->network.MacAddress, sme->bssid, ETH_ALEN) == _FALSE)
				continue;
		}
		
		if(sme->ssid && sme->ssid_len) {
			if(	pnetwork->network.Ssid.SsidLength != sme->ssid_len
				|| _memcmp(pnetwork->network.Ssid.Ssid, sme->ssid, sme->ssid_len) == _FALSE
			)
				continue;
		}
			

		if (sme->bssid)
		{
			src_bssid = sme->bssid;

			if ((_memcmp(dst_bssid, src_bssid, ETH_ALEN)) == _TRUE)
			{
				printk("matched by bssid\n");

				ndis_ssid.SsidLength = pnetwork->network.Ssid.SsidLength;				
				_memcpy(ndis_ssid.Ssid, pnetwork->network.Ssid.Ssid, pnetwork->network.Ssid.SsidLength);
				
				matched=_TRUE;
				break;
			}

		
		}
		else if (sme->ssid && sme->ssid_len)
		{		
			src_ssid = ndis_ssid.Ssid;

			if ((_memcmp(dst_ssid, src_ssid, ndis_ssid.SsidLength) == _TRUE) &&
				(pnetwork->network.Ssid.SsidLength==ndis_ssid.SsidLength))
			{
				printk("matched by ssid\n");
				matched=_TRUE;
				break;
			}
		}			
			
	}
	
	_exit_critical(&queue->lock, &irqL);

	if((matched == _FALSE) || (pnetwork== NULL))
	{
		ret = -EBUSY;
		printk("connect, matched == _FALSE, goto exit\n");
		goto exit;
	}


	if (set_802_11_infrastructure_mode(padapter, pnetwork->network.InfrastructureMode) == _FALSE)
	{
		ret = -EPERM;			
		goto exit;
	}
		

	psecuritypriv->ndisencryptstatus = Ndis802_11EncryptionDisabled;
	psecuritypriv->dot11PrivacyAlgrthm = _NO_PRIVACY_;
	psecuritypriv->dot118021XGrpPrivacy = _NO_PRIVACY_;
	psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_Open; //open system
	psecuritypriv->ndisauthtype = Ndis802_11AuthModeOpen;


	ret = rtw_cfg80211_set_wpa_version(psecuritypriv, sme->crypto.wpa_versions);
	if (ret < 0)
		goto exit;

	ret = rtw_cfg80211_set_auth_type(psecuritypriv, sme->auth_type);
	if (ret < 0)
		goto exit;


	if (sme->crypto.n_ciphers_pairwise) {		
		ret = rtw_cfg80211_set_cipher(psecuritypriv, sme->crypto.ciphers_pairwise[0], _TRUE);
		if (ret < 0)
			goto exit;
	}

	//For WEP Shared auth
	if(psecuritypriv->dot11AuthAlgrthm == dot11AuthAlgrthm_Shared
		|| psecuritypriv->dot11AuthAlgrthm == dot11AuthAlgrthm_Auto )
	{
		u32 wep_key_idx, wep_key_len,wep_total_len;
		NDIS_802_11_WEP	 *pwep = NULL;
		printk("%s(): Shared WEP\n",__FUNCTION__);

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
		 	pwep =(NDIS_802_11_WEP	 *) _malloc(wep_total_len);
			if(pwep == NULL){
				printk(" wpa_set_encryption: pwep allocate fail !!!\n");
				ret = -ENOMEM;
				goto exit;
			}

		 	_memset(pwep, 0, wep_total_len);

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

		_memcpy(pwep->KeyMaterial,  (void *)sme->key, pwep->KeyLength);

		if(set_802_11_add_wep(padapter, pwep) == (u8)_FAIL)
		{
			ret = -EOPNOTSUPP ;
		}

		if (pwep) {
			_mfree((u8 *)pwep,wep_total_len);		
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

	printk("%s, ie_len=%d\n", __func__, sme->ie_len);
			
	ret = rtw_cfg80211_set_wpa_ie(padapter, sme->ie, sme->ie_len);
	if (ret < 0)
		goto exit;

	authmode = psecuritypriv->ndisauthtype;
	set_802_11_authentication_mode(padapter, authmode);

	//rtw_set_802_11_encryption_mode(padapter, padapter->securitypriv.ndisencryptstatus);

	if (set_802_11_ssid(padapter, &ndis_ssid) == _FALSE) {
		ret = -1;
		goto exit;
	}


	printk("set ssid:dot11AuthAlgrthm=%d, dot11PrivacyAlgrthm=%d, dot118021XGrpPrivacy=%d\n", psecuritypriv->dot11AuthAlgrthm, psecuritypriv->dot11PrivacyAlgrthm, psecuritypriv->dot118021XGrpPrivacy);
	
exit:

	printk("<=%s, ret %d\n",__FUNCTION__, ret);
	
	return ret;
}

static int cfg80211_rtw_disconnect(struct wiphy *wiphy, struct net_device *dev,
				   u16 reason_code)
{
	_adapter *padapter = wiphy_to_adapter(wiphy);

	printk("\n%s(netdev=%p)\n", __func__, dev);

	if(check_fwstate(&padapter->mlmepriv, _FW_LINKED)) 
	{
		disassoc_cmd(padapter);
		
		printk("%s...call rtw_indicate_disconnect\n ", __FUNCTION__);
		
		indicate_disconnect(padapter);
		
		free_assoc_resources(padapter);
	}
	
	return 0;
}

static int cfg80211_rtw_set_txpower(struct wiphy *wiphy,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,36))
				    enum nl80211_tx_power_setting type, int mbm)
#else	// (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,36))
					enum tx_power_setting type, int dbm)
#endif	// (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,36))
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
	printk("%s\n", __func__);
	return 0;
}

static int cfg80211_rtw_get_txpower(struct wiphy *wiphy, int *dbm)
{
	//_adapter *padapter = wiphy_to_adapter(wiphy);

	printk("%s\n", __func__);

	*dbm = (12);
	
	return 0;
}

static int cfg80211_rtw_set_power_mgmt(struct wiphy *wiphy,
				       struct net_device *dev,
				       bool enabled, int timeout)
{
#if 0
	struct iwm_priv *iwm = wiphy_to_iwm(wiphy);
	u32 power_index;

	if (enabled)
		power_index = IWM_POWER_INDEX_DEFAULT;
	else
		power_index = IWM_POWER_INDEX_MIN;

	if (power_index == iwm->conf.power_index)
		return 0;

	iwm->conf.power_index = power_index;

	return iwm_umac_set_config_fix(iwm, UMAC_PARAM_TBL_CFG_FIX,
				       CFG_POWER_INDEX, iwm->conf.power_index);
#endif

	printk("%s\n", __func__);

	return 0;
}

static int cfg80211_rtw_set_pmksa(struct wiphy *wiphy,
				  struct net_device *netdev,
				  struct cfg80211_pmksa *pmksa)
{
	//struct iwm_priv *iwm = wiphy_to_iwm(wiphy);
	_adapter *padapter = wiphy_to_adapter(wiphy);

	printk("%s\n", __func__);

	//return iwm_send_pmkid_update(iwm, pmksa, IWM_CMD_PMKID_ADD);
	return 0;
}

static int cfg80211_rtw_del_pmksa(struct wiphy *wiphy,
				  struct net_device *netdev,
				  struct cfg80211_pmksa *pmksa)
{
	//struct iwm_priv *iwm = wiphy_to_iwm(wiphy);
	_adapter *padapter = wiphy_to_adapter(wiphy);

	printk("%s\n", __func__);

	//return iwm_send_pmkid_update(iwm, pmksa, IWM_CMD_PMKID_DEL);
	return 0;
}

static int cfg80211_rtw_flush_pmksa(struct wiphy *wiphy,
				    struct net_device *netdev)
{
	//struct iwm_priv *iwm = wiphy_to_iwm(wiphy);
	_adapter *padapter = wiphy_to_adapter(wiphy);
	struct cfg80211_pmksa pmksa;

	printk("%s\n", __func__);

	memset(&pmksa, 0, sizeof(struct cfg80211_pmksa));

	//return iwm_send_pmkid_update(iwm, &pmksa, IWM_CMD_PMKID_FLUSH);
	return 0;
}

static int	cfg80211_rtw_mgmt_tx(struct wiphy *wiphy, struct net_device *dev,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,38))
			struct ieee80211_channel *chan, bool offchan,
			enum nl80211_channel_type channel_type,
			bool channel_type_valid, unsigned int wait,
#else	//(LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,38))
			struct ieee80211_channel *chan,
			enum nl80211_channel_type channel_type,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))			  
			bool channel_type_valid,
#endif
#endif	//(LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,38))
			const u8 *buf, size_t len, u64 *cookie)
{
#if 0
	struct xmit_frame		*pmgntframe;
	struct pkt_attrib	*pattrib;
	unsigned char	*pframe;	
	const struct ieee80211_mgmt *mgmt;	
	//u8 category, action, OUI_Subtype, dialogToken=0;
	//unsigned char	*frame_body;
	int ret = 0;	
	int type = (-1);
	u16 fc;
	bool ack = _TRUE;
	struct rtw_ieee80211_hdr *pwlanhdr;
	_adapter *padapter = wiphy_to_adapter(wiphy);	
	struct xmit_priv	*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);


	/* cookie generation */
	*cookie = (unsigned long) buf;


	printk("%s(netdev=%p), len=%d, ch=%d, ch_type=%d\n", __func__, dev, len,
			ieee80211_frequency_to_channel(chan->center_freq), channel_type);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))	
	printk("channel_type_valid=%d\n", channel_type_valid);			 
#endif

	mgmt = (const struct ieee80211_mgmt *) buf;
	fc = mgmt->frame_control;
	if (fc != IEEE80211_STYPE_ACTION) 
	{
		if (fc == IEEE80211_STYPE_PROBE_RESP) 
		{
			printk("%s, fc == IEEE80211_STYPE_PROBE_RESP\n", __func__);
		}
		else
		{
			printk("%s, frame_control == 0x%x\n", __func__, fc);
		}
		
		//cfg80211_mgmt_tx_status(dev, *cookie, buf, len, ack, GFP_KERNEL);
		
		goto exit;
		
	} 
	else 
	{
		u32 cnt=0;
		u32 wait_for_surveydone;
		struct	mlme_priv	*pmlmepriv = &(padapter->mlmepriv);

#ifdef CONFIG_DEBUG_CFG80211
		printk("%s, do: scan_abort\n", __func__);
#endif

	    /* Abort the dwell time of any previous off-channel action frame that may
	     * be still in effect.  Sending off-channel action frames relies on the
	     * driver's scan engine.  If a previous off-channel action frame tx is
	     * still in progress (including the dwell time), then this new action
	     * frame will not be sent out.
	     */		

		rtw_cfg80211_scan_abort(padapter);
	}	
#if 0
	if (wl->p2p->vif_created) {
		wifi_p2p_pub_act_frame_t *act_frm =
			(wifi_p2p_pub_act_frame_t *) (action_frame->data);
		WL_DBG(("action_frame->len: %d chan %d category %d subtype %d\n",
			action_frame->len, af_params->channel,
			act_frm->category, act_frm->subtype));
		/*
		 * To make sure to send successfully action frame, we have to turn off mpc
		 */
		if ((act_frm->subtype == P2P_PAF_GON_REQ)||
		  (act_frm->subtype == P2P_PAF_GON_RSP)) {
			wldev_iovar_setint(dev, "mpc", 0);
		} else if (act_frm->subtype == P2P_PAF_GON_CONF) {
			wldev_iovar_setint(dev, "mpc", 1);
		} else if (act_frm->subtype == P2P_PAF_DEVDIS_REQ) {
			af_params->dwell_time = WL_LONG_DWELL_TIME;
		}
	}
#endif

/*
	frame_body = (unsigned char *)(buf + sizeof(struct rtw_ieee80211_hdr_3addr));	
	category = frame_body[0];
	//just for check
	if(category == RTW_WLAN_CATEGORY_PUBLIC)
	{
		action = frame_body[ 1 ];
		OUI_Subtype = frame_body[ 6 ];
		dialogToken = frame_body[7];

		if ( action == ACT_PUBLIC_P2P )
		{
			printk("ACTION_CATEGORY_PUBLIC: ACT_PUBLIC_P2P, OUI=0x%x, OUI_Subtype=%d, dialogToken=%d\n",
					cpu_to_be32( *( ( u32* ) ( frame_body + 2 ) ) ), OUI_Subtype, dialogToken);
		}
		else
		{
			printk("ACTION_CATEGORY_PUBLIC: action=%d, OUI=0x%x, OUI_Subtype=%d, dialogToken=%d\n",
					action, cpu_to_be32( *( ( u32* ) ( frame_body + 2 ) ) ), OUI_Subtype, dialogToken);
		}
		
	}	
	else if(category == RTW_WLAN_CATEGORY_P2P)
	{
		OUI_Subtype = frame_body[5];
		dialogToken = frame_body[6];

		printk("ACTION_CATEGORY_P2P: OUI=0x%x, OUI_Subtype=%d, dialogToken=%d\n",
					cpu_to_be32( *( ( u32* ) ( frame_body + 1 ) ) ), OUI_Subtype, dialogToken);

	}	
	else 
	{
		printk("%s, action frame category=%d\n", __func__, category);
		ack = _FALSE;		
		goto exit;
	}
*/

	if( ieee80211_frequency_to_channel(chan->center_freq) != pmlmeext->cur_channel )
	{
		pmlmeext->cur_channel = ieee80211_frequency_to_channel(chan->center_freq);
		set_channel_bwmode(padapter, pmlmeext->cur_channel, HAL_PRIME_CHNL_OFFSET_DONT_CARE, HT_CHANNEL_WIDTH_20);
	}
	

	if( (type = rtw_p2p_check_frames(padapter, buf, len, _TRUE)) < 0)
	{
		ack = _FALSE;		
		goto exit;
	}	


	//if(type == P2P_GO_NEGO_REQ)
	//{
	//	rtw_cfg80211_issue_p2p_provision_request(padapter, buf, len);
	//}
	
	
	//starting alloc mgmt frame to dump it
	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		ack = _FALSE;
		ret = -ENOMEM;
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

	
	pattrib->last_txcmdsz = pattrib->pktlen;
	
#ifdef CONFIG_DEBUG_CFG80211
	printk("%s, ack=%d, ok!\n", __func__, ack );
#endif

	//indicate ack before issue frame to avoid racing with rsp frame
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
	cfg80211_mgmt_tx_status(dev, *cookie, buf, len, ack, GFP_KERNEL);
#elif  (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,34) && LINUX_VERSION_CODE<=KERNEL_VERSION(2,6,35))
	cfg80211_action_tx_status(dev, *cookie, buf, len, ack, GFP_KERNEL);
#endif	
	
	dump_mgntframe(padapter, pmgntframe);
	
	return ret;
	
exit:
	
	printk("%s, ack=%d  \n", __func__, ack );

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
	cfg80211_mgmt_tx_status(dev, *cookie, buf, len, ack, GFP_KERNEL);
#elif  (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,34) && LINUX_VERSION_CODE<=KERNEL_VERSION(2,6,35))
	cfg80211_action_tx_status(dev, *cookie, buf, len, ack, GFP_KERNEL);
#endif	
	
	return ret;	
#endif
	printk("%s \n", __FUNCTION__);
	return 0;
}

static void cfg80211_rtw_mgmt_frame_register(struct wiphy *wiphy, struct net_device *dev,
	u16 frame_type, bool reg)
{
#if 0
	printk("%s: frame_type: %x, reg: %d\n", __func__, frame_type, reg);

	if (frame_type != (IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_PROBE_REQ))
		return;

	return;
#endif
	printk("%s \n", __FUNCTION__);
	return;
}

#include <rtw_android.h>
int rtw_cfg80211_do_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{

	//_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct iwreq *wrq = (struct iwreq *)rq;
	int ret=0;

#ifdef CONFIG_DEBUG_CFG80211
	printk("%s, cmd=0x%x\n", __func__, cmd);
#endif

	switch (cmd)
	{
		case (SIOCDEVPRIVATE+1):
				ret = rtw_android_priv_cmd(dev, rq, cmd);
				break;
		//case RTL_IOCTL_WPA_SUPPLICANT:	
				//ret = wpa_supplicant_ioctl(dev, &wrq->u.data);
		//		break;
#ifdef CONFIG_AP_MODE
				case RTL_IOCTL_HOSTAPD:
				//ret = rtw_hostapd_ioctl(dev, &wrq->u.data);			
				break;
#endif
	  	  default:
				ret = -EOPNOTSUPP;
				break;
	}
	
	return ret;

	return 0;
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
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
	.mgmt_tx = cfg80211_rtw_mgmt_tx,
	.mgmt_frame_register = cfg80211_rtw_mgmt_frame_register,
#elif  (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,34) && LINUX_VERSION_CODE<=KERNEL_VERSION(2,6,35))
	.action = cfg80211_rtw_mgmt_tx,
#endif
};

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
	if(rf_type == RTL8712_RF_1T1R)
	{
		ht_cap->mcs.rx_mask[0] = 0xFF;
		ht_cap->mcs.rx_mask[1] = 0x00;
		ht_cap->mcs.rx_mask[4] = 0x01;

		ht_cap->mcs.rx_highest = MAX_BIT_RATE_40MHZ_MCS7;
	}
	else if((rf_type == RTL8712_RF_1T2R) || (rf_type==RTL8712_RF_2T2R))
	{
		ht_cap->mcs.rx_mask[0] = 0xFF;
		ht_cap->mcs.rx_mask[1] = 0xFF;
		ht_cap->mcs.rx_mask[4] = 0x01;

		ht_cap->mcs.rx_highest = MAX_BIT_RATE_40MHZ_MCS15;
	}
	else
	{
		printk("%s, error rf_type=%d\n", __func__, rf_type);
	}	

}

void rtw_cfg80211_init_wiphy(_adapter *padapter)
{
	struct ieee80211_supported_band *bands;
	struct wireless_dev *pwdev = padapter->rtw_wdev;
	struct wiphy *wiphy = pwdev->wiphy;
	struct registry_priv *pregpriv = &padapter->registrypriv;
	
	printk("%s:rf_config=%d\n", __func__, pregpriv->rf_config);

	bands = wiphy->bands[IEEE80211_BAND_2GHZ];
	rtw_cfg80211_init_ht_capab(&bands->ht_cap, IEEE80211_BAND_2GHZ, pregpriv->rf_config);

}

static void rtw_cfg80211_preinit_wiphy(_adapter *padapter, struct wiphy *wiphy)
{

	wiphy->signal_type = CFG80211_SIGNAL_TYPE_MBM;

	wiphy->max_scan_ssids = RTW_SSID_SCAN_AMOUNT;
	wiphy->max_scan_ie_len = RTW_SCAN_IE_LEN_MAX;	
	wiphy->max_num_pmkids = RTW_MAX_NUM_PMKIDS;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,38))
	wiphy->max_remain_on_channel_duration = RTW_MAX_REMAIN_ON_CHANNEL_DURATION;
#endif
	
	wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION) |BIT(NL80211_IFTYPE_ADHOC);

	wiphy->cipher_suites = rtw_cipher_suites;
	wiphy->n_cipher_suites = ARRAY_SIZE(rtw_cipher_suites);

	wiphy->bands[IEEE80211_BAND_2GHZ] = &rtw_band_2ghz;
	wiphy->bands[IEEE80211_BAND_5GHZ] = &rtw_band_5ghz;
	
}

int rtw_wdev_alloc(_adapter *padapter, struct device *dev)
{
	int ret = 0;
	struct wireless_dev *wdev;
	struct rtw_wdev_priv *pwdev_priv;
	struct net_device *pnetdev = padapter->pnetdev;
	
	printk("%s\n", __func__);

	wdev = (struct wireless_dev *)_zmalloc(sizeof(struct wireless_dev));
	if (!wdev) {
		printk("Couldn't allocate wireless device\n");
		return (-ENOMEM);
	}

	wdev->wiphy = wiphy_new(&rtw_cfg80211_ops, sizeof(struct rtw_wdev_priv));
	if (!wdev->wiphy) {
		printk("Couldn't allocate wiphy device\n");
		ret = -ENOMEM;
		goto out_err_new;
	}

	set_wiphy_dev(wdev->wiphy, dev);
	
	//	
	padapter->rtw_wdev = wdev;
	pnetdev->ieee80211_ptr = wdev;

	//init pwdev_priv
	pwdev_priv = wdev_to_priv(wdev);
	pwdev_priv->pmon_ndev = NULL;
	pwdev_priv->ifname_mon[0] = '\0';	
	pwdev_priv->rtw_wdev = wdev;
	pwdev_priv->padapter = padapter;
	pwdev_priv->scan_request = NULL;
	_spinlock_init(&pwdev_priv->scan_req_lock);
		
	wdev->netdev = pnetdev;
	wdev->iftype = NL80211_IFTYPE_STATION;

	rtw_cfg80211_preinit_wiphy(padapter, wdev->wiphy);

	ret = wiphy_register(wdev->wiphy);
	if (ret < 0) {
		printk("Couldn't register wiphy device\n");
		goto out_err_register;
	}

	SET_NETDEV_DEV(pnetdev, wiphy_dev(wdev->wiphy));

	return ret;

 out_err_register:
	wiphy_free(wdev->wiphy);

 out_err_new:
	_mfree((u8*)wdev, sizeof(struct wireless_dev));

	return ret;
	
}

void rtw_wdev_free(struct wireless_dev *wdev)
{
	struct rtw_wdev_priv *pwdev_priv;

	printk("%s\n", __func__);

	if (!wdev)
		return;

	pwdev_priv = wdev_to_priv(wdev);
	
	printk("%s, scan abort when device remove\n", __func__);	
	rtw_cfg80211_indicate_scan_done(pwdev_priv, _TRUE);
	
	if(pwdev_priv->pmon_ndev)
	{
		printk("%s, unregister monitor interface\n", __func__);
	
		unregister_netdev(pwdev_priv->pmon_ndev);
		
		free_netdev(pwdev_priv->pmon_ndev);
	}		
	

	wiphy_unregister(wdev->wiphy);
	wiphy_free(wdev->wiphy);

	_mfree((u8*)wdev, sizeof(struct wireless_dev));
}

#endif //CONFIG_IOCTL_CFG80211

