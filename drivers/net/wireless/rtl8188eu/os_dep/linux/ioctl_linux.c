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
#define  _IOCTL_LINUX_C_

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <wlan_bssdef.h>
#include <rtw_debug.h>
#include <wifi.h>
#include <rtw_mlme.h>
#include <rtw_mlme_ext.h>
#include <rtw_ioctl.h>
#include <rtw_ioctl_set.h>
#include <rtw_ioctl_query.h>

//#ifdef CONFIG_MP_INCLUDED
#include <rtw_mp_ioctl.h>
//#endif

#ifdef CONFIG_USB_HCI
#include <usb_ops.h>
#endif //CONFIG_USB_HCI
#include <rtw_version.h>

#ifdef CONFIG_MP_INCLUDED
#include <rtw_mp.h>
#endif //#ifdef CONFIG_MP_INCLUDED
#ifdef CONFIG_RTL8192C
#include <rtl8192c_hal.h>
#endif
#ifdef CONFIG_RTL8192D
#include <rtl8192d_hal.h>
#endif
#ifdef CONFIG_RTL8723A
#include <rtl8723a_pg.h>
#include <rtl8723a_hal.h>
#include "rtw_bt_mp.h"
#endif
#ifdef CONFIG_RTL8188E
#include <rtl8188e_hal.h>
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,27))
#define  iwe_stream_add_event(a, b, c, d, e)  iwe_stream_add_event(b, c, d, e)
#define  iwe_stream_add_point(a, b, c, d, e)  iwe_stream_add_point(b, c, d, e)
#endif


#define RTL_IOCTL_WPA_SUPPLICANT	SIOCIWFIRSTPRIV+30

#define SCAN_ITEM_SIZE 768
#define MAX_CUSTOM_LEN 64
#define RATE_COUNT 4

#ifdef CONFIG_GLOBAL_UI_PID
extern int ui_pid[3];
#endif

// combo scan
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


extern u8 key_2char2num(u8 hch, u8 lch);
extern u8 str_2char2num(u8 hch, u8 lch);

int rfpwrstate_check(_adapter *padapter);

u32 rtw_rates[] = {1000000,2000000,5500000,11000000,
	6000000,9000000,12000000,18000000,24000000,36000000,48000000,54000000};

static const char * const iw_operation_mode[] = 
{ 
	"Auto", "Ad-Hoc", "Managed",  "Master", "Repeater", "Secondary", "Monitor" 
};

static int hex2num_i(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static int hex2byte_i(const char *hex)
{
	int a, b;
	a = hex2num_i(*hex++);
	if (a < 0)
		return -1;
	b = hex2num_i(*hex++);
	if (b < 0)
		return -1;
	return (a << 4) | b;
}

/**
 * hwaddr_aton - Convert ASCII string to MAC address
 * @txt: MAC address as a string (e.g., "00:11:22:33:44:55")
 * @addr: Buffer for the MAC address (ETH_ALEN = 6 bytes)
 * Returns: 0 on success, -1 on failure (e.g., string not a MAC address)
 */
static int hwaddr_aton_i(const char *txt, u8 *addr)
{
	int i;

	for (i = 0; i < 6; i++) {
		int a, b;

		a = hex2num_i(*txt++);
		if (a < 0)
			return -1;
		b = hex2num_i(*txt++);
		if (b < 0)
			return -1;
		*addr++ = (a << 4) | b;
		if (i < 5 && *txt++ != ':')
			return -1;
	}

	return 0;
}

static void indicate_wx_custom_event(_adapter *padapter, char *msg)
{
	u8 *buff, *p;
	union iwreq_data wrqu;

	if (strlen(msg) > IW_CUSTOM_MAX) {
		DBG_871X("%s strlen(msg):%u > IW_CUSTOM_MAX:%u\n", __FUNCTION__ ,strlen(msg), IW_CUSTOM_MAX);
		return;
	}

	buff = rtw_zmalloc(IW_CUSTOM_MAX+1);
	if(!buff)
		return;

	_rtw_memcpy(buff, msg, strlen(msg));
		
	_rtw_memset(&wrqu,0,sizeof(wrqu));
	wrqu.data.length = strlen(msg);

	DBG_871X("%s %s\n", __FUNCTION__, buff);	
	wireless_send_event(padapter->pnetdev, IWEVCUSTOM, &wrqu, buff);

	rtw_mfree(buff, IW_CUSTOM_MAX+1);

}


static void request_wps_pbc_event(_adapter *padapter)
{
	u8 *buff, *p;
	union iwreq_data wrqu;


	buff = rtw_malloc(IW_CUSTOM_MAX);
	if(!buff)
		return;
		
	_rtw_memset(buff, 0, IW_CUSTOM_MAX);
		
	p=buff;
		
	p+=sprintf(p, "WPS_PBC_START.request=TRUE");
		
	_rtw_memset(&wrqu,0,sizeof(wrqu));
		
	wrqu.data.length = p-buff;
		
	wrqu.data.length = (wrqu.data.length<IW_CUSTOM_MAX) ? wrqu.data.length:IW_CUSTOM_MAX;

	DBG_871X("%s\n", __FUNCTION__);
		
	wireless_send_event(padapter->pnetdev, IWEVCUSTOM, &wrqu, buff);

	if(buff)
	{
		rtw_mfree(buff, IW_CUSTOM_MAX);
	}

}


void indicate_wx_scan_complete_event(_adapter *padapter)
{	
	union iwreq_data wrqu;
	struct	mlme_priv *pmlmepriv = &padapter->mlmepriv;	

	_rtw_memset(&wrqu, 0, sizeof(union iwreq_data));

	//DBG_871X("+rtw_indicate_wx_scan_complete_event\n");
	wireless_send_event(padapter->pnetdev, SIOCGIWSCAN, &wrqu, NULL);
}


void rtw_indicate_wx_assoc_event(_adapter *padapter)
{	
	union iwreq_data wrqu;
	struct	mlme_priv *pmlmepriv = &padapter->mlmepriv;	

	_rtw_memset(&wrqu, 0, sizeof(union iwreq_data));
	
	wrqu.ap_addr.sa_family = ARPHRD_ETHER;	
	
	_rtw_memcpy(wrqu.ap_addr.sa_data, pmlmepriv->cur_network.network.MacAddress, ETH_ALEN);

	//DBG_871X("+rtw_indicate_wx_assoc_event\n");
	wireless_send_event(padapter->pnetdev, SIOCGIWAP, &wrqu, NULL);
}

void rtw_indicate_wx_disassoc_event(_adapter *padapter)
{	
	union iwreq_data wrqu;

	_rtw_memset(&wrqu, 0, sizeof(union iwreq_data));

	wrqu.ap_addr.sa_family = ARPHRD_ETHER;
	_rtw_memset(wrqu.ap_addr.sa_data, 0, ETH_ALEN);
	
	//DBG_871X("+rtw_indicate_wx_disassoc_event\n");
	wireless_send_event(padapter->pnetdev, SIOCGIWAP, &wrqu, NULL);
}

/*
uint	rtw_is_cckrates_included(u8 *rate)
{	
		u32	i = 0;			

		while(rate[i]!=0)
		{		
			if  (  (((rate[i]) & 0x7f) == 2)	|| (((rate[i]) & 0x7f) == 4) ||		
			(((rate[i]) & 0x7f) == 11)  || (((rate[i]) & 0x7f) == 22) )		
			return _TRUE;	
			i++;
		}
		
		return _FALSE;
}

uint	rtw_is_cckratesonly_included(u8 *rate)
{
	u32 i = 0;

	while(rate[i]!=0)
	{
			if  (  (((rate[i]) & 0x7f) != 2) && (((rate[i]) & 0x7f) != 4) &&
				(((rate[i]) & 0x7f) != 11)  && (((rate[i]) & 0x7f) != 22) )
			return _FALSE;		
			i++;
	}
	
	return _TRUE;
}
*/

static char *translate_scan(_adapter *padapter, 
				struct iw_request_info* info, struct wlan_network *pnetwork,
				char *start, char *stop)
{
	struct iw_event iwe;
	u16 cap;
	u32 ht_ielen = 0;
	char custom[MAX_CUSTOM_LEN];
	char *p;
	u16 max_rate=0, rate, ht_cap=_FALSE;
	u32 i = 0;	
	char	*current_val;
	long rssi;
	u8 bw_40MHz=0, short_GI=0;
	u16 mcs_rate=0;
	struct registry_priv *pregpriv = &padapter->registrypriv;
#ifdef CONFIG_P2P
	struct wifidirect_info	*pwdinfo = &padapter->wdinfo;
#endif //CONFIG_P2P

#ifdef CONFIG_P2P
	if(!rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE) && !rtw_p2p_chk_state(pwdinfo, P2P_STATE_IDLE))
	{
		u32	blnGotP2PIE = _FALSE;
		
		//	User is doing the P2P device discovery
		//	The prefix of SSID should be "DIRECT-" and the IE should contains the P2P IE.
		//	If not, the driver should ignore this AP and go to the next AP.

		//	Verifying the SSID
		if ( _rtw_memcmp( pnetwork->network.Ssid.Ssid, pwdinfo->p2p_wildcard_ssid, P2P_WILDCARD_SSID_LEN ) )
		{
			u32	p2pielen = 0;

			//	Verifying the P2P IE
			if ( rtw_get_p2p_ie( &pnetwork->network.IEs[12], pnetwork->network.IELength - 12, NULL, &p2pielen) )
			{
				blnGotP2PIE = _TRUE;
			}

		}

		if ( blnGotP2PIE == _FALSE )
		{
			return start;
		}
		
	}

#endif //CONFIG_P2P

	/*  AP MAC address  */
	iwe.cmd = SIOCGIWAP;
	iwe.u.ap_addr.sa_family = ARPHRD_ETHER;

	_rtw_memcpy(iwe.u.ap_addr.sa_data, pnetwork->network.MacAddress, ETH_ALEN);
	start = iwe_stream_add_event(info, start, stop, &iwe, IW_EV_ADDR_LEN);

	/* Add the ESSID */
	iwe.cmd = SIOCGIWESSID;
	iwe.u.data.flags = 1;	
	iwe.u.data.length = min((u16)pnetwork->network.Ssid.SsidLength, (u16)32);
	start = iwe_stream_add_point(info, start, stop, &iwe, pnetwork->network.Ssid.Ssid);

	//parsing HT_CAP_IE
	p = rtw_get_ie(&pnetwork->network.IEs[12], _HT_CAPABILITY_IE_, &ht_ielen, pnetwork->network.IELength-12);

	if(p && ht_ielen>0)
	{
		struct rtw_ieee80211_ht_cap *pht_capie;
		ht_cap = _TRUE;			
		pht_capie = (struct rtw_ieee80211_ht_cap *)(p+2);		
		_rtw_memcpy(&mcs_rate , pht_capie->supp_mcs_set, 2);
		bw_40MHz = (pht_capie->cap_info&IEEE80211_HT_CAP_SUP_WIDTH) ? 1:0;
		short_GI = (pht_capie->cap_info&(IEEE80211_HT_CAP_SGI_20|IEEE80211_HT_CAP_SGI_40)) ? 1:0;
	}

	/* Add the protocol name */
	iwe.cmd = SIOCGIWNAME;
	if ((rtw_is_cckratesonly_included((u8*)&pnetwork->network.SupportedRates)) == _TRUE)		
	{
		if(ht_cap == _TRUE)
			snprintf(iwe.u.name, IFNAMSIZ, "IEEE 802.11bn");
		else
		snprintf(iwe.u.name, IFNAMSIZ, "IEEE 802.11b");
	}	
	else if ((rtw_is_cckrates_included((u8*)&pnetwork->network.SupportedRates)) == _TRUE)	
	{
		if(ht_cap == _TRUE)
			snprintf(iwe.u.name, IFNAMSIZ, "IEEE 802.11bgn");
		else
		snprintf(iwe.u.name, IFNAMSIZ, "IEEE 802.11bg");
	}	
	else
	{
		if(pnetwork->network.Configuration.DSConfig > 14)
		{
			if(ht_cap == _TRUE)
				snprintf(iwe.u.name, IFNAMSIZ, "IEEE 802.11an");
			else
				snprintf(iwe.u.name, IFNAMSIZ, "IEEE 802.11a");
		}
		else
		{
			if(ht_cap == _TRUE)
				snprintf(iwe.u.name, IFNAMSIZ, "IEEE 802.11gn");
			else
				snprintf(iwe.u.name, IFNAMSIZ, "IEEE 802.11g");
		}
	}	

	start = iwe_stream_add_event(info, start, stop, &iwe, IW_EV_CHAR_LEN);

	  /* Add mode */
        iwe.cmd = SIOCGIWMODE;
	_rtw_memcpy((u8 *)&cap, rtw_get_capability_from_ie(pnetwork->network.IEs), 2);


	cap = le16_to_cpu(cap);

	if(cap & (WLAN_CAPABILITY_IBSS |WLAN_CAPABILITY_BSS)){
		if (cap & WLAN_CAPABILITY_BSS)
			iwe.u.mode = IW_MODE_MASTER;
		else
			iwe.u.mode = IW_MODE_ADHOC;

		start = iwe_stream_add_event(info, start, stop, &iwe, IW_EV_UINT_LEN);
	}

	if(pnetwork->network.Configuration.DSConfig<1 /*|| pnetwork->network.Configuration.DSConfig>14*/)
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
	p = custom;
	p += snprintf(p, MAX_CUSTOM_LEN - (p - custom), " Rates (Mb/s): ");
	while(pnetwork->network.SupportedRates[i]!=0)
	{
		rate = pnetwork->network.SupportedRates[i]&0x7F; 
		if (rate > max_rate)
			max_rate = rate;
		p += snprintf(p, MAX_CUSTOM_LEN - (p - custom),
			      "%d%s ", rate >> 1, (rate & 1) ? ".5" : "");
		i++;
	}
	
	if(ht_cap == _TRUE)
	{
		if(mcs_rate&0x8000)//MCS15
		{
			max_rate = (bw_40MHz) ? ((short_GI)?300:270):((short_GI)?144:130);
			
		}
		else if(mcs_rate&0x0080)//MCS7
		{
			max_rate = (bw_40MHz) ? ((short_GI)?150:135):((short_GI)?72:65);
		}
		else//default MCS7
		{
			//DBG_871X("wx_get_scan, mcs_rate_bitmap=0x%x\n", mcs_rate);
			max_rate = (bw_40MHz) ? ((short_GI)?150:135):((short_GI)?72:65);
		}

		max_rate = max_rate*2;//Mbps/2;		
	}

	iwe.cmd = SIOCGIWRATE;
	iwe.u.bitrate.fixed = iwe.u.bitrate.disabled = 0;
	iwe.u.bitrate.value = max_rate * 500000;
	start = iwe_stream_add_event(info, start, stop, &iwe, IW_EV_PARAM_LEN);

	//parsing WPA/WPA2 IE
	{
		u8 buf[MAX_WPA_IE_LEN];
		u8 wpa_ie[255],rsn_ie[255];
		u16 wpa_len=0,rsn_len=0;
		u8 *p;
		sint out_len=0;
		out_len=rtw_get_sec_ie(pnetwork->network.IEs ,pnetwork->network.IELength,rsn_ie,&rsn_len,wpa_ie,&wpa_len);
		RT_TRACE(_module_rtl871x_mlme_c_,_drv_info_,("rtw_wx_get_scan: ssid=%s\n",pnetwork->network.Ssid.Ssid));
		RT_TRACE(_module_rtl871x_mlme_c_,_drv_info_,("rtw_wx_get_scan: wpa_len=%d rsn_len=%d\n",wpa_len,rsn_len));

		if (wpa_len > 0)
		{
			p=buf;
			_rtw_memset(buf, 0, MAX_WPA_IE_LEN);
			p += sprintf(p, "wpa_ie=");
			for (i = 0; i < wpa_len; i++) {
				p += sprintf(p, "%02x", wpa_ie[i]);
			}
	
			_rtw_memset(&iwe, 0, sizeof(iwe));
			iwe.cmd = IWEVCUSTOM;
			iwe.u.data.length = strlen(buf);
			start = iwe_stream_add_point(info, start, stop, &iwe,buf);
			
			_rtw_memset(&iwe, 0, sizeof(iwe));
			iwe.cmd =IWEVGENIE;
			iwe.u.data.length = wpa_len;
			start = iwe_stream_add_point(info, start, stop, &iwe, wpa_ie);			
		}
		if (rsn_len > 0)
		{
			p = buf;
			_rtw_memset(buf, 0, MAX_WPA_IE_LEN);
			p += sprintf(p, "rsn_ie=");
			for (i = 0; i < rsn_len; i++) {
				p += sprintf(p, "%02x", rsn_ie[i]);
			}
			_rtw_memset(&iwe, 0, sizeof(iwe));
			iwe.cmd = IWEVCUSTOM;
			iwe.u.data.length = strlen(buf);
			start = iwe_stream_add_point(info, start, stop, &iwe,buf);
		
			_rtw_memset(&iwe, 0, sizeof(iwe));
			iwe.cmd =IWEVGENIE;
			iwe.u.data.length = rsn_len;
			start = iwe_stream_add_point(info, start, stop, &iwe, rsn_ie);		
		}
	}

	{ //parsing WPS IE
		uint cnt = 0,total_ielen;	
		u8 *wpsie_ptr=NULL;
		uint wps_ielen = 0;		

		u8 *ie_ptr = pnetwork->network.IEs +_FIXED_IE_LENGTH_;
		total_ielen= pnetwork->network.IELength - _FIXED_IE_LENGTH_;

		while(cnt < total_ielen)
		{
			if(rtw_is_wps_ie(&ie_ptr[cnt], &wps_ielen) && (wps_ielen>2))			
			{
				wpsie_ptr = &ie_ptr[cnt];
				iwe.cmd =IWEVGENIE;
				iwe.u.data.length = (u16)wps_ielen;
				start = iwe_stream_add_point(info, start, stop, &iwe, wpsie_ptr);						
			}			
			cnt+=ie_ptr[cnt+1]+2; //goto next
		}
	}

	/* Add quality statistics */
	iwe.cmd = IWEVQUAL;
	rssi = pnetwork->network.Rssi;//dBM
	
#ifdef CONFIG_RTL8711	
	rssi = (rssi*2) + 190;
	if(rssi>100) rssi = 100;
	if(rssi<0) rssi = 0;
#endif	
	
	//DBG_871X("RSSI=0x%X%%\n", rssi);

	// we only update signal_level (signal strength) that is rssi.
	iwe.u.qual.updated = IW_QUAL_QUAL_UPDATED | IW_QUAL_LEVEL_UPDATED | IW_QUAL_NOISE_INVALID
	#ifdef CONFIG_SIGNAL_DISPLAY_DBM
		| IW_QUAL_DBM
	#endif
	;

	#ifdef CONFIG_SIGNAL_DISPLAY_DBM
	iwe.u.qual.level = (u8) translate_percentage_to_dbm(pnetwork->network.PhyInfo.SignalStrength);//dbm
	#else
	iwe.u.qual.level = (u8)pnetwork->network.PhyInfo.SignalStrength;//%
	#endif
	
	iwe.u.qual.qual = (u8)pnetwork->network.PhyInfo.SignalQuality;   // signal quality

	#ifdef CONFIG_PLATFORM_ROCKCHIPS
	iwe.u.qual.noise = -100; // noise level suggest by zhf@rockchips
	#else 
	iwe.u.qual.noise = 0; // noise level
	#endif //CONFIG_PLATFORM_ROCKCHIPS
	
	//DBG_871X("iqual=%d, ilevel=%d, inoise=%d, iupdated=%d\n", iwe.u.qual.qual, iwe.u.qual.level , iwe.u.qual.noise, iwe.u.qual.updated);

	start = iwe_stream_add_event(info, start, stop, &iwe, IW_EV_QUAL_LEN);
	
	//how to translate rssi to ?%
	//rssi = (iwe.u.qual.level*2) +  190;
	//if(rssi>100) rssi = 100;
	//if(rssi<0) rssi = 0;
	
	return start;	
}

static int wpa_set_auth_algs(struct net_device *dev, u32 value)
{	
	_adapter *padapter = (_adapter *) rtw_netdev_priv(dev);
	int ret = 0;

	if ((value & AUTH_ALG_SHARED_KEY)&&(value & AUTH_ALG_OPEN_SYSTEM))
	{
		DBG_871X("wpa_set_auth_algs, AUTH_ALG_SHARED_KEY and  AUTH_ALG_OPEN_SYSTEM [value:0x%x]\n",value);
		padapter->securitypriv.ndisencryptstatus = Ndis802_11Encryption1Enabled;
		padapter->securitypriv.ndisauthtype = Ndis802_11AuthModeAutoSwitch;
		padapter->securitypriv.dot11AuthAlgrthm = dot11AuthAlgrthm_Auto;
	} 
	else if (value & AUTH_ALG_SHARED_KEY)
	{
		DBG_871X("wpa_set_auth_algs, AUTH_ALG_SHARED_KEY  [value:0x%x]\n",value);
		padapter->securitypriv.ndisencryptstatus = Ndis802_11Encryption1Enabled;

#ifdef CONFIG_PLATFORM_MT53XX
		padapter->securitypriv.ndisauthtype = Ndis802_11AuthModeAutoSwitch;
		padapter->securitypriv.dot11AuthAlgrthm = dot11AuthAlgrthm_Auto;
#else
		padapter->securitypriv.ndisauthtype = Ndis802_11AuthModeShared;
		padapter->securitypriv.dot11AuthAlgrthm = dot11AuthAlgrthm_Shared;
#endif
	} 
	else if(value & AUTH_ALG_OPEN_SYSTEM)
	{
		DBG_871X("wpa_set_auth_algs, AUTH_ALG_OPEN_SYSTEM\n");
		//padapter->securitypriv.ndisencryptstatus = Ndis802_11EncryptionDisabled;
		if(padapter->securitypriv.ndisauthtype < Ndis802_11AuthModeWPAPSK)
		{
#ifdef CONFIG_PLATFORM_MT53XX
			padapter->securitypriv.ndisauthtype = Ndis802_11AuthModeAutoSwitch;
			padapter->securitypriv.dot11AuthAlgrthm = dot11AuthAlgrthm_Auto;
#else
			padapter->securitypriv.ndisauthtype = Ndis802_11AuthModeOpen;
 			padapter->securitypriv.dot11AuthAlgrthm = dot11AuthAlgrthm_Open;
#endif
		}
		
	}
	else if(value & AUTH_ALG_LEAP)
	{
		DBG_871X("wpa_set_auth_algs, AUTH_ALG_LEAP\n");
	}
	else
	{
		DBG_871X("wpa_set_auth_algs, error!\n");
		ret = -EINVAL;
	}

	return ret;
	
}

static int wpa_set_encryption(struct net_device *dev, struct ieee_param *param, u32 param_len)
{
	int ret = 0;
	u32 wep_key_idx, wep_key_len,wep_total_len;
	NDIS_802_11_WEP	 *pwep = NULL;	
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct mlme_priv 	*pmlmepriv = &padapter->mlmepriv;		
	struct security_priv *psecuritypriv = &padapter->securitypriv;
#ifdef CONFIG_P2P
	struct wifidirect_info* pwdinfo = &padapter->wdinfo;
#endif //CONFIG_P2P

_func_enter_;

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
		DBG_871X("wpa_set_encryption, crypt.alg = WEP\n");

		padapter->securitypriv.ndisencryptstatus = Ndis802_11Encryption1Enabled;
		padapter->securitypriv.dot11PrivacyAlgrthm=_WEP40_;
		padapter->securitypriv.dot118021XGrpPrivacy=_WEP40_;

		wep_key_idx = param->u.crypt.idx;
		wep_key_len = param->u.crypt.key_len;

		RT_TRACE(_module_rtl871x_ioctl_os_c,_drv_info_,("(1)wep_key_idx=%d\n", wep_key_idx));
		DBG_871X("(1)wep_key_idx=%d\n", wep_key_idx);

		if (wep_key_idx > WEP_KEYS)
			return -EINVAL;

		RT_TRACE(_module_rtl871x_ioctl_os_c,_drv_info_,("(2)wep_key_idx=%d\n", wep_key_idx));

		if (wep_key_len > 0) 
		{
		 	wep_key_len = wep_key_len <= 5 ? 5 : 13;
			wep_total_len = wep_key_len + FIELD_OFFSET(NDIS_802_11_WEP, KeyMaterial);
		 	pwep =(NDIS_802_11_WEP	 *) rtw_malloc(wep_total_len);
			if(pwep == NULL){
				RT_TRACE(_module_rtl871x_ioctl_os_c,_drv_err_,(" wpa_set_encryption: pwep allocate fail !!!\n"));
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

		_rtw_memcpy(pwep->KeyMaterial,  param->u.crypt.key, pwep->KeyLength);

		if(param->u.crypt.set_tx)
		{
			DBG_871X("wep, set_tx=1\n");

			if(rtw_set_802_11_add_wep(padapter, pwep) == (u8)_FAIL)
			{
				ret = -EOPNOTSUPP ;
			}
		}
		else
		{
			DBG_871X("wep, set_tx=0\n");
			
			//don't update "psecuritypriv->dot11PrivacyAlgrthm" and 
			//"psecuritypriv->dot11PrivacyKeyIndex=keyid", but can rtw_set_key to fw/cam
			
			if (wep_key_idx >= WEP_KEYS) {
				ret = -EOPNOTSUPP ;
				goto exit;
			}				
			
		      _rtw_memcpy(&(psecuritypriv->dot11DefKey[wep_key_idx].skey[0]), pwep->KeyMaterial, pwep->KeyLength);
			psecuritypriv->dot11DefKeylen[wep_key_idx]=pwep->KeyLength;	
			rtw_set_key(padapter, psecuritypriv, wep_key_idx, 0);
		}

		goto exit;		
	}

	if(padapter->securitypriv.dot11AuthAlgrthm == dot11AuthAlgrthm_8021X) // 802_1x
	{
		struct sta_info * psta,*pbcmc_sta;
		struct sta_priv * pstapriv = &padapter->stapriv;

		if (check_fwstate(pmlmepriv, WIFI_STATION_STATE | WIFI_MP_STATE) == _TRUE) //sta mode
		{
			psta = rtw_get_stainfo(pstapriv, get_bssid(pmlmepriv));				
			if (psta == NULL) {
				//DEBUG_ERR( ("Set wpa_set_encryption: Obtain Sta_info fail \n"));
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
					_rtw_memcpy(psta->dot118021x_UncstKey.skey,  param->u.crypt.key, (param->u.crypt.key_len>16 ?16:param->u.crypt.key_len));
					
					if(strcmp(param->u.crypt.alg, "TKIP") == 0)//set mic key
					{						
						//DEBUG_ERR(("\nset key length :param->u.crypt.key_len=%d\n", param->u.crypt.key_len));
						_rtw_memcpy(psta->dot11tkiptxmickey.skey, &(param->u.crypt.key[16]), 8);
						_rtw_memcpy(psta->dot11tkiprxmickey.skey, &(param->u.crypt.key[24]), 8);

						padapter->securitypriv.busetkipkey=_FALSE;
						//_set_timer(&padapter->securitypriv.tkip_timer, 50);						
					}

					//DEBUG_ERR(("\n param->u.crypt.key_len=%d\n",param->u.crypt.key_len));
					//DEBUG_ERR(("\n ~~~~stastakey:unicastkey\n"));
					DBG_871X("\n ~~~~stastakey:unicastkey\n");
					
					rtw_setstakey_cmd(padapter, (unsigned char *)psta, _TRUE);
				}
				else//group key
				{ 					
					_rtw_memcpy(padapter->securitypriv.dot118021XGrpKey[param->u.crypt.idx].skey,  param->u.crypt.key,(param->u.crypt.key_len>16 ?16:param->u.crypt.key_len));
					_rtw_memcpy(padapter->securitypriv.dot118021XGrptxmickey[param->u.crypt.idx].skey,&(param->u.crypt.key[16]),8);
					_rtw_memcpy(padapter->securitypriv.dot118021XGrprxmickey[param->u.crypt.idx].skey,&(param->u.crypt.key[24]),8);
                                        padapter->securitypriv.binstallGrpkey = _TRUE;	
					//DEBUG_ERR(("\n param->u.crypt.key_len=%d\n", param->u.crypt.key_len));
					//DEBUG_ERR(("\n ~~~~stastakey:groupkey\n"));
					DBG_871X("\n ~~~~stastakey:groupkey\n");

					padapter->securitypriv.dot118021XGrpKeyid = param->u.crypt.idx;

					rtw_set_key(padapter,&padapter->securitypriv,param->u.crypt.idx, 1);
#ifdef CONFIG_P2P
					if(rtw_p2p_chk_state(pwdinfo, P2P_STATE_PROVISIONING_ING))
					{
						rtw_p2p_set_state(pwdinfo, P2P_STATE_PROVISIONING_DONE);
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

exit:
	
	if (pwep) {
		rtw_mfree((u8 *)pwep, wep_total_len);		
	}	
	
_func_exit_;

	return ret;	
}

static int rtw_set_wpa_ie(_adapter *padapter, char *pie, unsigned short ielen)
{
	u8 *buf=NULL, *pos=NULL;	
	u32 left; 	
	int group_cipher = 0, pairwise_cipher = 0;
	int ret = 0;
#ifdef CONFIG_P2P
	struct wifidirect_info* pwdinfo = &padapter->wdinfo;
#endif //CONFIG_P2P

	if((ielen > MAX_WPA_IE_LEN) || (pie == NULL)){
		padapter->securitypriv.wps_phase = _FALSE;	
		if(pie == NULL)	
			return ret;
		else
			return -EINVAL;
	}

	if(ielen)
	{		
		buf = rtw_zmalloc(ielen);
		if (buf == NULL){
			ret =  -ENOMEM;
			goto exit;
		}
	
		_rtw_memcpy(buf, pie , ielen);

		//dump
		{
			int i;
			DBG_871X("\n wpa_ie(length:%d):\n", ielen);
			for(i=0;i<ielen;i=i+8)
				DBG_871X("0x%.2x 0x%.2x 0x%.2x 0x%.2x 0x%.2x 0x%.2x 0x%.2x 0x%.2x \n",buf[i],buf[i+1],buf[i+2],buf[i+3],buf[i+4],buf[i+5],buf[i+6],buf[i+7]);
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
		
		if(rtw_parse_wpa_ie(buf, ielen, &group_cipher, &pairwise_cipher) == _SUCCESS)
		{
			padapter->securitypriv.dot11AuthAlgrthm= dot11AuthAlgrthm_8021X;
			padapter->securitypriv.ndisauthtype=Ndis802_11AuthModeWPAPSK;
			_rtw_memcpy(padapter->securitypriv.supplicant_ie, &buf[0], ielen);	
		}
	
		if(rtw_parse_wpa2_ie(buf, ielen, &group_cipher, &pairwise_cipher) == _SUCCESS)
		{
			padapter->securitypriv.dot11AuthAlgrthm= dot11AuthAlgrthm_8021X;
			padapter->securitypriv.ndisauthtype=Ndis802_11AuthModeWPA2PSK;	
			_rtw_memcpy(padapter->securitypriv.supplicant_ie, &buf[0], ielen);	
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
			 
			while( cnt < ielen )
			{
				eid = buf[cnt];
		
				if((eid==_VENDOR_SPECIFIC_IE_)&&(_rtw_memcmp(&buf[cnt+2], wps_oui, 4)==_TRUE))
				{
					DBG_871X("SET WPS_IE\n");

					padapter->securitypriv.wps_ie_len = ( (buf[cnt+1]+2) < (MAX_WPA_IE_LEN<<2)) ? (buf[cnt+1]+2):(MAX_WPA_IE_LEN<<2);
					
					_rtw_memcpy(padapter->securitypriv.wps_ie, &buf[cnt], padapter->securitypriv.wps_ie_len);
					
					padapter->securitypriv.wps_phase = _TRUE;
					
#ifdef CONFIG_P2P
					if(rtw_p2p_chk_state(pwdinfo, P2P_STATE_GONEGO_OK))
					{
						rtw_p2p_set_state(pwdinfo, P2P_STATE_PROVISIONING_ING);
					}
#endif //CONFIG_P2P
					DBG_871X("SET WPS_IE, wps_phase==_TRUE\n");

					cnt += buf[cnt+1]+2;
					
					break;
				} else {
					cnt += buf[cnt+1]+2; //goto next	
				}				
			}			
		}		
	}
	
	RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_info_,
		 ("rtw_set_wpa_ie: pairwise_cipher=0x%08x padapter->securitypriv.ndisencryptstatus=%d padapter->securitypriv.ndisauthtype=%d\n",
		  pairwise_cipher, padapter->securitypriv.ndisencryptstatus, padapter->securitypriv.ndisauthtype));
 	
exit:

	if (buf) rtw_mfree(buf, ielen);

	return ret;
}

static int rtw_wx_get_name(struct net_device *dev, 
			     struct iw_request_info *info, 
			     union iwreq_data *wrqu, char *extra)
{
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	u16 cap;
	u32 ht_ielen = 0;
	char *p;
	u8 ht_cap=_FALSE;
	struct	mlme_priv	*pmlmepriv = &(padapter->mlmepriv);
	WLAN_BSSID_EX  *pcur_bss = &pmlmepriv->cur_network.network;
	NDIS_802_11_RATES_EX* prates = NULL;

	RT_TRACE(_module_rtl871x_mlme_c_,_drv_info_,("cmd_code=%x\n", info->cmd));

	_func_enter_;	

	if (check_fwstate(pmlmepriv, _FW_LINKED|WIFI_ADHOC_MASTER_STATE) == _TRUE)
	{
		//parsing HT_CAP_IE
		p = rtw_get_ie(&pcur_bss->IEs[12], _HT_CAPABILITY_IE_, &ht_ielen, pcur_bss->IELength-12);
		if(p && ht_ielen>0)
		{
			ht_cap = _TRUE;
		}

		prates = &pcur_bss->SupportedRates;

		if (rtw_is_cckratesonly_included((u8*)prates) == _TRUE)
		{
			if(ht_cap == _TRUE)
				snprintf(wrqu->name, IFNAMSIZ, "IEEE 802.11bn");
			else
				snprintf(wrqu->name, IFNAMSIZ, "IEEE 802.11b");
		}
		else if ((rtw_is_cckrates_included((u8*)prates)) == _TRUE)
		{
			if(ht_cap == _TRUE)
				snprintf(wrqu->name, IFNAMSIZ, "IEEE 802.11bgn");
			else
				snprintf(wrqu->name, IFNAMSIZ, "IEEE 802.11bg");
		}
		else
		{
			if(pcur_bss->Configuration.DSConfig > 14)
			{
				if(ht_cap == _TRUE)
					snprintf(wrqu->name, IFNAMSIZ, "IEEE 802.11an");
				else
					snprintf(wrqu->name, IFNAMSIZ, "IEEE 802.11a");
			}
			else
			{
				if(ht_cap == _TRUE)
					snprintf(wrqu->name, IFNAMSIZ, "IEEE 802.11gn");
				else
					snprintf(wrqu->name, IFNAMSIZ, "IEEE 802.11g");
			}
		}
	}
	else
	{
		//prates = &padapter->registrypriv.dev_network.SupportedRates;
		//snprintf(wrqu->name, IFNAMSIZ, "IEEE 802.11g");
		snprintf(wrqu->name, IFNAMSIZ, "unassociated");
	}

	_func_exit_;

	return 0;
}

static int rtw_wx_set_freq(struct net_device *dev, 
			     struct iw_request_info *info, 
			     union iwreq_data *wrqu, char *extra)
{	
	_func_enter_;

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_notice_, ("+rtw_wx_set_freq\n"));

	_func_exit_;
	
	return 0;
}

static int rtw_wx_get_freq(struct net_device *dev, 
			     struct iw_request_info *info, 
			     union iwreq_data *wrqu, char *extra)
{
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct	mlme_priv	*pmlmepriv = &(padapter->mlmepriv);
	WLAN_BSSID_EX  *pcur_bss = &pmlmepriv->cur_network.network;
	
	if(check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE)
	{
		//wrqu->freq.m = ieee80211_wlan_frequencies[pcur_bss->Configuration.DSConfig-1] * 100000;
		wrqu->freq.m = rtw_ch2freq(pcur_bss->Configuration.DSConfig) * 100000;
		wrqu->freq.e = 1;
		wrqu->freq.i = pcur_bss->Configuration.DSConfig;

	}
	else{
		wrqu->freq.m = rtw_ch2freq(padapter->mlmeextpriv.cur_channel) * 100000;
		wrqu->freq.e = 1;
		wrqu->freq.i = padapter->mlmeextpriv.cur_channel;
	}

	return 0;
}

static int rtw_wx_set_mode(struct net_device *dev, struct iw_request_info *a,
			     union iwreq_data *wrqu, char *b)
{
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	NDIS_802_11_NETWORK_INFRASTRUCTURE networkType ;
	int ret = 0;
	
	_func_enter_;
	
	if(_FAIL == rfpwrstate_check(padapter)) {
		ret= -EPERM;
		goto exit;
	}

	if (padapter->hw_init_completed==_FALSE){
		ret = -EPERM;
		goto exit;
	}
	
	switch(wrqu->mode)
	{
		case IW_MODE_AUTO:
			networkType = Ndis802_11AutoUnknown;
			DBG_871X("set_mode = IW_MODE_AUTO\n");	
			break;				
		case IW_MODE_ADHOC:		
			networkType = Ndis802_11IBSS;
			DBG_871X("set_mode = IW_MODE_ADHOC\n");			
			break;
		case IW_MODE_MASTER:		
			networkType = Ndis802_11APMode;
			DBG_871X("set_mode = IW_MODE_MASTER\n");
                        //rtw_setopmode_cmd(padapter, networkType);	
			break;				
		case IW_MODE_INFRA:
			networkType = Ndis802_11Infrastructure;
			DBG_871X("set_mode = IW_MODE_INFRA\n");			
			break;
	
		default :
			ret = -EINVAL;;
			RT_TRACE(_module_rtl871x_ioctl_os_c,_drv_err_,("\n Mode: %s is not supported  \n", iw_operation_mode[wrqu->mode]));
			goto exit;
	}
	
/*	
	if(Ndis802_11APMode == networkType)
	{
		rtw_setopmode_cmd(padapter, networkType);
	}	
	else
	{
		rtw_setopmode_cmd(padapter, Ndis802_11AutoUnknown);	
	}
*/
	
	if (rtw_set_802_11_infrastructure_mode(padapter, networkType) ==_FALSE){

		ret = -EPERM;
		goto exit;

	}

	rtw_setopmode_cmd(padapter, networkType);

exit:
	
	_func_exit_;
	
	return ret;
	
}

static int rtw_wx_get_mode(struct net_device *dev, struct iw_request_info *a,
			     union iwreq_data *wrqu, char *b)
{
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct	mlme_priv	*pmlmepriv = &(padapter->mlmepriv);
	
	RT_TRACE(_module_rtl871x_mlme_c_,_drv_info_,(" rtw_wx_get_mode \n"));

	_func_enter_;
	
	if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) == _TRUE)
	{
		wrqu->mode = IW_MODE_INFRA;
	}
	else if  ((check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == _TRUE) ||
		       (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == _TRUE))
		
	{
		wrqu->mode = IW_MODE_ADHOC;
	}
	else if(check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE)
	{
		wrqu->mode = IW_MODE_MASTER;
	}
	else
	{
		wrqu->mode = IW_MODE_AUTO;
	}

	_func_exit_;
	
	return 0;
	
}


static int rtw_wx_set_pmkid(struct net_device *dev,
	                     struct iw_request_info *a,
			     union iwreq_data *wrqu, char *extra)
{
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	u8          j,blInserted = _FALSE;
	int         intReturn = _FALSE;
	struct mlme_priv  *pmlmepriv = &padapter->mlmepriv;
	struct security_priv *psecuritypriv = &padapter->securitypriv;
        struct iw_pmksa*  pPMK = ( struct iw_pmksa* ) extra;
        u8     strZeroMacAddress[ ETH_ALEN ] = { 0x00 };
        u8     strIssueBssid[ ETH_ALEN ] = { 0x00 };
        
/*
        struct iw_pmksa
        {
            __u32   cmd;
            struct sockaddr bssid;
            __u8    pmkid[IW_PMKID_LEN];   //IW_PMKID_LEN=16
        }
        There are the BSSID information in the bssid.sa_data array.
        If cmd is IW_PMKSA_FLUSH, it means the wpa_suppplicant wants to clear all the PMKID information.
        If cmd is IW_PMKSA_ADD, it means the wpa_supplicant wants to add a PMKID/BSSID to driver.
        If cmd is IW_PMKSA_REMOVE, it means the wpa_supplicant wants to remove a PMKID/BSSID from driver.
        */

	_rtw_memcpy( strIssueBssid, pPMK->bssid.sa_data, ETH_ALEN);
        if ( pPMK->cmd == IW_PMKSA_ADD )
        {
                DBG_871X( "[rtw_wx_set_pmkid] IW_PMKSA_ADD!\n" );
                if ( _rtw_memcmp( strIssueBssid, strZeroMacAddress, ETH_ALEN ) == _TRUE )
                {
                    return( intReturn );
                }
                else
                {
                    intReturn = _TRUE;
                }
		blInserted = _FALSE;
		
		//overwrite PMKID
		for(j=0 ; j<NUM_PMKID_CACHE; j++)
		{
			if( _rtw_memcmp( psecuritypriv->PMKIDList[j].Bssid, strIssueBssid, ETH_ALEN) ==_TRUE )
			{ // BSSID is matched, the same AP => rewrite with new PMKID.
                                
                                DBG_871X( "[rtw_wx_set_pmkid] BSSID exists in the PMKList.\n" );

				_rtw_memcpy( psecuritypriv->PMKIDList[j].PMKID, pPMK->pmkid, IW_PMKID_LEN);
                                psecuritypriv->PMKIDList[ j ].bUsed = _TRUE;
				psecuritypriv->PMKIDIndex = j+1;
				blInserted = _TRUE;
				break;
			}	
	        }

	        if(!blInserted)
                {
		    // Find a new entry
                    DBG_871X( "[rtw_wx_set_pmkid] Use the new entry index = %d for this PMKID.\n",
                            psecuritypriv->PMKIDIndex );

	            _rtw_memcpy(psecuritypriv->PMKIDList[psecuritypriv->PMKIDIndex].Bssid, strIssueBssid, ETH_ALEN);
		    _rtw_memcpy(psecuritypriv->PMKIDList[psecuritypriv->PMKIDIndex].PMKID, pPMK->pmkid, IW_PMKID_LEN);

                    psecuritypriv->PMKIDList[ psecuritypriv->PMKIDIndex ].bUsed = _TRUE;
		    psecuritypriv->PMKIDIndex++ ;
		    if(psecuritypriv->PMKIDIndex==16)
                    {
		        psecuritypriv->PMKIDIndex =0;
                    }
		}
        }
        else if ( pPMK->cmd == IW_PMKSA_REMOVE )
        {
                DBG_871X( "[rtw_wx_set_pmkid] IW_PMKSA_REMOVE!\n" );
                intReturn = _TRUE;
		for(j=0 ; j<NUM_PMKID_CACHE; j++)
		{
			if( _rtw_memcmp( psecuritypriv->PMKIDList[j].Bssid, strIssueBssid, ETH_ALEN) ==_TRUE )
			{ // BSSID is matched, the same AP => Remove this PMKID information and reset it. 
                                _rtw_memset( psecuritypriv->PMKIDList[ j ].Bssid, 0x00, ETH_ALEN );
                                psecuritypriv->PMKIDList[ j ].bUsed = _FALSE;
				break;
			}	
	        }
        }
        else if ( pPMK->cmd == IW_PMKSA_FLUSH ) 
        {
            DBG_871X( "[rtw_wx_set_pmkid] IW_PMKSA_FLUSH!\n" );
            _rtw_memset( &psecuritypriv->PMKIDList[ 0 ], 0x00, sizeof( RT_PMKID_LIST ) * NUM_PMKID_CACHE );
            psecuritypriv->PMKIDIndex = 0;
            intReturn = _TRUE;
        }
    return( intReturn );
}

static int rtw_wx_get_sens(struct net_device *dev, 
			     struct iw_request_info *info, 
			     union iwreq_data *wrqu, char *extra)
{
	#ifdef CONFIG_PLATFORM_ROCKCHIPS
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv); 
	
	/*
	*  20110311 Commented by Jeff
	*  For rockchip platform's wpa_driver_wext_get_rssi
	*/
	if(check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE) {
		//wrqu->sens.value=-padapter->recvpriv.signal_strength;
		wrqu->sens.value=-padapter->recvpriv.rssi;
		//DBG_871X("%s: %d\n", __FUNCTION__, wrqu->sens.value);
		wrqu->sens.fixed = 0; /* no auto select */ 
	} else 
	#endif
	{
		wrqu->sens.value = 0;
		wrqu->sens.fixed = 0;	/* no auto select */
		wrqu->sens.disabled = 1;
	}
	return 0;
}

static int rtw_wx_get_range(struct net_device *dev, 
				struct iw_request_info *info, 
				union iwreq_data *wrqu, char *extra)
{
	struct iw_range *range = (struct iw_range *)extra;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;

	u16 val;
	int i;
	
	_func_enter_;
	
	RT_TRACE(_module_rtl871x_mlme_c_,_drv_info_,("rtw_wx_get_range. cmd_code=%x\n", info->cmd));

	wrqu->data.length = sizeof(*range);
	_rtw_memset(range, 0, sizeof(*range));

	/* Let's try to keep this struct in the same order as in
	 * linux/include/wireless.h
	 */

	/* TODO: See what values we can set, and remove the ones we can't
	 * set, or fill them with some default data.
	 */

	/* ~5 Mb/s real (802.11b) */
	range->throughput = 5 * 1000 * 1000;     

	// TODO: Not used in 802.11b?
//	range->min_nwid;	/* Minimal NWID we are able to set */
	// TODO: Not used in 802.11b?
//	range->max_nwid;	/* Maximal NWID we are able to set */

        /* Old Frequency (backward compat - moved lower ) */
//	range->old_num_channels; 
//	range->old_num_frequency;
//	range->old_freq[6]; /* Filler to keep "version" at the same offset */

	/* signal level threshold range */

	//percent values between 0 and 100.
	range->max_qual.qual = 100;	
	range->max_qual.level = 100;
	range->max_qual.noise = 100;
	range->max_qual.updated = 7; /* Updated all three */


	range->avg_qual.qual = 92; /* > 8% missed beacons is 'bad' */
	/* TODO: Find real 'good' to 'bad' threshol value for RSSI */
	range->avg_qual.level = 20 + -98;
	range->avg_qual.noise = 0;
	range->avg_qual.updated = 7; /* Updated all three */

	range->num_bitrates = RATE_COUNT;

	for (i = 0; i < RATE_COUNT && i < IW_MAX_BITRATES; i++) {
		range->bitrate[i] = rtw_rates[i];
	}

	range->min_frag = MIN_FRAG_THRESHOLD;
	range->max_frag = MAX_FRAG_THRESHOLD;

	range->pm_capa = 0;

	range->we_version_compiled = WIRELESS_EXT;
	range->we_version_source = 16;

//	range->retry_capa;	/* What retry options are supported */
//	range->retry_flags;	/* How to decode max/min retry limit */
//	range->r_time_flags;	/* How to decode max/min retry life */
//	range->min_retry;	/* Minimal number of retries */
//	range->max_retry;	/* Maximal number of retries */
//	range->min_r_time;	/* Minimal retry lifetime */
//	range->max_r_time;	/* Maximal retry lifetime */

	for (i = 0, val = 0; i < MAX_CHANNEL_NUM; i++) {

		// Include only legal frequencies for some countries
		if(pmlmeext->channel_set[i].ChannelNum != 0)
		{
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

// Commented by Albert 2009/10/13
// The following code will proivde the security capability to network manager.
// If the driver doesn't provide this capability to network manager,
// the WPA/WPA2 routers can't be choosen in the network manager.

/*
#define IW_SCAN_CAPA_NONE		0x00
#define IW_SCAN_CAPA_ESSID		0x01
#define IW_SCAN_CAPA_BSSID		0x02
#define IW_SCAN_CAPA_CHANNEL	0x04
#define IW_SCAN_CAPA_MODE		0x08
#define IW_SCAN_CAPA_RATE		0x10
#define IW_SCAN_CAPA_TYPE		0x20
#define IW_SCAN_CAPA_TIME		0x40
*/

#if WIRELESS_EXT > 17
	range->enc_capa = IW_ENC_CAPA_WPA|IW_ENC_CAPA_WPA2|
			  IW_ENC_CAPA_CIPHER_TKIP|IW_ENC_CAPA_CIPHER_CCMP;
#endif

#ifdef IW_SCAN_CAPA_ESSID //WIRELESS_EXT > 21
	range->scan_capa = IW_SCAN_CAPA_ESSID | IW_SCAN_CAPA_TYPE |IW_SCAN_CAPA_BSSID|
					IW_SCAN_CAPA_CHANNEL|IW_SCAN_CAPA_MODE|IW_SCAN_CAPA_RATE;
#endif


	_func_exit_;

	return 0;

}

//set bssid flow
//s1. rtw_set_802_11_infrastructure_mode()
//s2. rtw_set_802_11_authentication_mode()
//s3. set_802_11_encryption_mode()
//s4. rtw_set_802_11_bssid()
static int rtw_wx_set_wap(struct net_device *dev,
			 struct iw_request_info *info,
			 union iwreq_data *awrq,
			 char *extra)
{
	_irqL	irqL;
	uint ret = 0;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct sockaddr *temp = (struct sockaddr *)awrq;
	struct	mlme_priv	*pmlmepriv = &(padapter->mlmepriv);
	_list	*phead;
	u8 *dst_bssid, *src_bssid;
	_queue	*queue	= &(pmlmepriv->scanned_queue);
	struct	wlan_network	*pnetwork = NULL;
	NDIS_802_11_AUTHENTICATION_MODE	authmode;

	_func_enter_;
/*
#ifdef CONFIG_CONCURRENT_MODE
	if(padapter->iface_type > PRIMARY_IFACE)
	{
		ret = -EINVAL;
		goto exit;
	}
#endif	
*/	

#ifdef CONFIG_CONCURRENT_MODE
	if (check_buddy_fwstate(padapter, _FW_UNDER_SURVEY|_FW_UNDER_LINKING) == _TRUE)
	{
		DBG_871X("set bssid, but buddy_intf is under scanning or linking\n");

		ret = -EINVAL;

		goto exit;
	}
#endif

#ifdef CONFIG_DUALMAC_CONCURRENT
	if (dc_check_fwstate(padapter, _FW_UNDER_SURVEY|_FW_UNDER_LINKING)== _TRUE)
	{
		DBG_871X("set bssid, but buddy_intf is under scanning or linking\n");
		ret = -EINVAL;
		goto exit;
	}
#endif

	if(_FAIL == rfpwrstate_check(padapter))
	{
		ret= -1;
		goto exit;
	}
	
	if(!padapter->bup){
		ret = -1;
		goto exit;
	}

	
	if (temp->sa_family != ARPHRD_ETHER){
		ret = -EINVAL;
		goto exit;
	}

	authmode = padapter->securitypriv.ndisauthtype;
	_enter_critical_bh(&queue->lock, &irqL);
       phead = get_list_head(queue);
       pmlmepriv->pscanned = get_next(phead);

	while (1)
	 {
			
		if ((rtw_end_of_queue_search(phead, pmlmepriv->pscanned)) == _TRUE)
		{
#if 0		
			ret = -EINVAL;
			goto exit;

			if(check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == _TRUE)
			{
	            		rtw_set_802_11_bssid(padapter, temp->sa_data);
	    			goto exit;                    
			}
			else
			{
				ret = -EINVAL;
				goto exit;
			}
#endif

			break;
		}
	
		pnetwork = LIST_CONTAINOR(pmlmepriv->pscanned, struct wlan_network, list);

		pmlmepriv->pscanned = get_next(pmlmepriv->pscanned);

		dst_bssid = pnetwork->network.MacAddress;

		src_bssid = temp->sa_data;

		if ((_rtw_memcmp(dst_bssid, src_bssid, ETH_ALEN)) == _TRUE)
		{			
			if(!rtw_set_802_11_infrastructure_mode(padapter, pnetwork->network.InfrastructureMode))
			{
				ret = -1;
				_exit_critical_bh(&queue->lock, &irqL);
				goto exit;
			}

				break;			
		}

	}		
	_exit_critical_bh(&queue->lock, &irqL);
	
	rtw_set_802_11_authentication_mode(padapter, authmode);
	//set_802_11_encryption_mode(padapter, padapter->securitypriv.ndisencryptstatus);
	if (rtw_set_802_11_bssid(padapter, temp->sa_data) == _FALSE) {
		ret = -1;
		goto exit;		
	}	
	
exit:
	
	_func_exit_;
	
	return ret;	
}

static int rtw_wx_get_wap(struct net_device *dev, 
			    struct iw_request_info *info, 
			    union iwreq_data *wrqu, char *extra)
{

	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);	
	struct	mlme_priv	*pmlmepriv = &(padapter->mlmepriv);
	WLAN_BSSID_EX  *pcur_bss = &pmlmepriv->cur_network.network;	
	
	wrqu->ap_addr.sa_family = ARPHRD_ETHER;
	
	_rtw_memset(wrqu->ap_addr.sa_data, 0, ETH_ALEN);
	
	RT_TRACE(_module_rtl871x_mlme_c_,_drv_info_,("rtw_wx_get_wap\n"));

	_func_enter_;

	if  ( ((check_fwstate(pmlmepriv, _FW_LINKED)) == _TRUE) || 
			((check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE)) == _TRUE) ||
			((check_fwstate(pmlmepriv, WIFI_AP_STATE)) == _TRUE) )
	{

		_rtw_memcpy(wrqu->ap_addr.sa_data, pcur_bss->MacAddress, ETH_ALEN);
	}
	else
	{
	 	_rtw_memset(wrqu->ap_addr.sa_data, 0, ETH_ALEN);
	}		

	_func_exit_;
	
	return 0;
	
}

static int rtw_wx_set_mlme(struct net_device *dev, 
			     struct iw_request_info *info, 
			     union iwreq_data *wrqu, char *extra)
{
#if 0
/* SIOCSIWMLME data */
struct	iw_mlme
{
	__u16		cmd; /* IW_MLME_* */
	__u16		reason_code;
	struct sockaddr	addr;
};
#endif

	int ret=0;
	u16 reason;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct iw_mlme *mlme = (struct iw_mlme *) extra;


	if(mlme==NULL)
		return -1;

	DBG_871X("%s\n", __FUNCTION__);

	reason = cpu_to_le16(mlme->reason_code);


	DBG_871X("%s, cmd=%d, reason=%d\n", __FUNCTION__, mlme->cmd, reason);

	switch (mlme->cmd) 
	{
		case IW_MLME_DEAUTH:
				if(!rtw_set_802_11_disassociate(padapter))
				ret = -1;
				break;

		case IW_MLME_DISASSOC:
				if(!rtw_set_802_11_disassociate(padapter))
						ret = -1;

				break;

		default:
			return -EOPNOTSUPP;
	}

	return ret;
}

int rfpwrstate_check(_adapter *padapter)
{
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;	
	int ret = _SUCCESS;

	//block here for system suspend only
	if((pwrpriv->bInternalAutoSuspend == _FALSE) && (_TRUE == pwrpriv->bInSuspend )){
		ret = _FAIL;
		goto exit;
	}
		
	if( pwrpriv->power_mgnt == PS_MODE_ACTIVE ) {
		goto exit;
	}

	if((pwrpriv->bInternalAutoSuspend == _TRUE)  && (padapter->net_closed == _TRUE)) {
		ret = _FAIL;
		goto exit;
	}

	if (check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE)
	{
		ret = _SUCCESS;
		goto exit;
	}
		
	if(rf_off == pwrpriv->rf_pwrstate )
	{		
#ifdef CONFIG_USB_HCI
#ifdef CONFIG_AUTOSUSPEND
		 if(pwrpriv->brfoffbyhw==_TRUE)
		{
			DBG_871X("hw still in rf_off state ...........\n");
			ret = _FAIL;
			goto exit;
		}
		else if(padapter->registrypriv.usbss_enable)
		{
			DBG_871X("\n %s call autoresume_enter....\n",__FUNCTION__);	
			if(_FAIL ==  autoresume_enter(padapter))
			{
				DBG_871X("======> autoresume fail.............\n");
				ret = _FAIL;
				goto exit;
			}	
		}
		else
#endif
#endif
		{
#ifdef CONFIG_IPS
			DBG_871X("\n %s call ips_leave....\n",__FUNCTION__);				
			if(_FAIL ==  ips_leave(padapter))
			{
				DBG_871X("======> ips_leave fail.............\n");
				ret = _FAIL;
				goto exit;
			}
#endif
		}
	}else {
		//Jeff: reset timer to avoid falling ips or selective suspend soon
		if(pwrpriv->bips_processing == _FALSE)
			rtw_set_pwr_state_check_timer(pwrpriv);
	}

exit:
	return ret;

}

static int rtw_wx_set_scan(struct net_device *dev, struct iw_request_info *a,
			     union iwreq_data *wrqu, char *extra)
{
	u8 _status = _FALSE;
	int ret = 0;	
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv= &padapter->mlmepriv;
	NDIS_802_11_SSID ssid[RTW_SSID_SCAN_AMOUNT];
	_irqL	irqL;
#ifdef CONFIG_P2P
	struct wifidirect_info *pwdinfo= &(padapter->wdinfo);	
#endif //CONFIG_P2P
	RT_TRACE(_module_rtl871x_mlme_c_,_drv_info_,("rtw_wx_set_scan\n"));

_func_enter_;

	#ifdef DBG_IOCTL
	DBG_871X("DBG_IOCTL %s:%d\n",__FUNCTION__, __LINE__);
	#endif
/*
#ifdef CONFIG_CONCURRENT_MODE
	if(padapter->iface_type > PRIMARY_IFACE)
	{
		ret = -1;
		goto exit;
	}
#endif
*/

#ifdef CONFIG_MP_INCLUDED
	if (check_fwstate(pmlmepriv, WIFI_MP_STATE) == _TRUE)
	{
		ret = -1;
		goto exit;
	}
#endif

	if(_FAIL == rfpwrstate_check(padapter))
	{
		ret= -1;
		goto exit;
	}

	if(padapter->bDriverStopped){
           DBG_871X("bDriverStopped=%d\n", padapter->bDriverStopped);
		ret= -1;
		goto exit;
	}
	
	if(!padapter->bup){
		ret = -1;
		goto exit;
	}
	
	if (padapter->hw_init_completed==_FALSE){
		ret = -1;
		goto exit;
	}

	// When Busy Traffic, driver do not site survey. So driver return success.
	// wpa_supplicant will not issue SIOCSIWSCAN cmd again after scan timeout.
	// modify by thomas 2011-02-22.
	if (pmlmepriv->LinkDetectInfo.bBusyTraffic == _TRUE)
	{
		indicate_wx_scan_complete_event(padapter);
		goto exit;
	} 

	if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY|_FW_UNDER_LINKING) == _TRUE)
	{
		indicate_wx_scan_complete_event(padapter);
		goto exit;
	} 

#ifdef CONFIG_CONCURRENT_MODE
	if (check_buddy_fwstate(padapter, _FW_UNDER_SURVEY|_FW_UNDER_LINKING) == _TRUE)
	{		
		if(check_buddy_fwstate(padapter, _FW_UNDER_SURVEY))
		{
			DBG_871X("scanning_via_buddy_intf\n");
			pmlmepriv->scanning_via_buddy_intf = _TRUE;
		}		

		indicate_wx_scan_complete_event(padapter);
		
		goto exit;
	}
#endif

#ifdef CONFIG_DUALMAC_CONCURRENT
	if (dc_check_fwstate(padapter, _FW_UNDER_SURVEY|_FW_UNDER_LINKING)== _TRUE)
	{
		indicate_wx_scan_complete_event(padapter);
		goto exit;
	}
#endif

//	Mareded by Albert 20101103
//	For the DMP WiFi Display project, the driver won't to scan because
//	the pmlmepriv->scan_interval is always equal to 3.
//	So, the wpa_supplicant won't find out the WPS SoftAP.

/*
	if(pmlmepriv->scan_interval>10)
		pmlmepriv->scan_interval = 0;

	if(pmlmepriv->scan_interval > 0)
	{
		DBG_871X("scan done\n");
		ret = 0;
		goto exit;
	}
		
*/
#ifdef CONFIG_P2P
	if(!rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE) && !rtw_p2p_chk_state(pwdinfo, P2P_STATE_IDLE))
	{
		rtw_p2p_set_state(pwdinfo, P2P_STATE_FIND_PHASE_SEARCH);
		rtw_p2p_findphase_ex_set(pwdinfo, P2P_FINDPHASE_EX_FULL);
		rtw_free_network_queue(padapter, _TRUE);
	}
#endif //CONFIG_P2P

	_rtw_memset(ssid, 0, sizeof(NDIS_802_11_SSID)*RTW_SSID_SCAN_AMOUNT);

#if WIRELESS_EXT >= 17
	if (wrqu->data.length == sizeof(struct iw_scan_req)) 
	{
		struct iw_scan_req *req = (struct iw_scan_req *)extra;
	
		if (wrqu->data.flags & IW_SCAN_THIS_ESSID)
		{
			int len = min((int)req->essid_len, IW_ESSID_MAX_SIZE);

			_rtw_memcpy(ssid[0].Ssid, req->essid, len);
			ssid[0].SsidLength = len;	

			DBG_871X("IW_SCAN_THIS_ESSID, ssid=%s, len=%d\n", req->essid, req->essid_len);
		
			_enter_critical_bh(&pmlmepriv->lock, &irqL);				
		
			_status = rtw_sitesurvey_cmd(padapter, ssid, 1);
		
			_exit_critical_bh(&pmlmepriv->lock, &irqL);
			
		}
		else if (req->scan_type == IW_SCAN_TYPE_PASSIVE)
		{
			DBG_871X("rtw_wx_set_scan, req->scan_type == IW_SCAN_TYPE_PASSIVE\n");
		}
		
	}
	else
#endif

	if(	wrqu->data.length >= WEXT_CSCAN_HEADER_SIZE
		&& _rtw_memcmp(extra, WEXT_CSCAN_HEADER, WEXT_CSCAN_HEADER_SIZE) == _TRUE
	)
	{
		int len = wrqu->data.length -WEXT_CSCAN_HEADER_SIZE;
		char *pos = extra+WEXT_CSCAN_HEADER_SIZE;
		char section;
		char sec_len;
		int ssid_index = 0;

		//DBG_871X("%s COMBO_SCAN header is recognized\n", __FUNCTION__);
		
		while(len >= 1) {
			section = *(pos++); len-=1;

			switch(section) {
				case WEXT_CSCAN_SSID_SECTION:
					//DBG_871X("WEXT_CSCAN_SSID_SECTION\n");
					if(len < 1) {
						len = 0;
						break;
					}
					
					sec_len = *(pos++); len-=1;

					if(sec_len>0 && sec_len<=len) {
						ssid[ssid_index].SsidLength = sec_len;
						_rtw_memcpy(ssid[ssid_index].Ssid, pos, ssid[ssid_index].SsidLength);
						//DBG_871X("%s COMBO_SCAN with specific ssid:%s, %d\n", __FUNCTION__
						//	, ssid[ssid_index].Ssid, ssid[ssid_index].SsidLength);
						ssid_index++;
					}
					
					pos+=sec_len; len-=sec_len;
					break;
					
				
				case WEXT_CSCAN_CHANNEL_SECTION:
					//DBG_871X("WEXT_CSCAN_CHANNEL_SECTION\n");
					pos+=1; len-=1;
					break;
				case WEXT_CSCAN_ACTV_DWELL_SECTION:
					//DBG_871X("WEXT_CSCAN_ACTV_DWELL_SECTION\n");
					pos+=2; len-=2;
					break;
				case WEXT_CSCAN_PASV_DWELL_SECTION:
					//DBG_871X("WEXT_CSCAN_PASV_DWELL_SECTION\n");
					pos+=2; len-=2;					
					break;
				case WEXT_CSCAN_HOME_DWELL_SECTION:
					//DBG_871X("WEXT_CSCAN_HOME_DWELL_SECTION\n");
					pos+=2; len-=2;
					break;
				case WEXT_CSCAN_TYPE_SECTION:
					//DBG_871X("WEXT_CSCAN_TYPE_SECTION\n");
					pos+=1; len-=1;
					break;
				#if 0
				case WEXT_CSCAN_NPROBE_SECTION:
					DBG_871X("WEXT_CSCAN_NPROBE_SECTION\n");
					break;
				#endif
				
				default:
					//DBG_871X("Unknown CSCAN section %c\n", section);
					len = 0; // stop parsing
			}
			//DBG_871X("len:%d\n", len);
			
		}
		
		//jeff: it has still some scan paramater to parse, we only do this now...			
		_enter_critical_bh(&pmlmepriv->lock, &irqL);				
		_status = rtw_sitesurvey_cmd(padapter, ssid, RTW_SSID_SCAN_AMOUNT);	
		_exit_critical_bh(&pmlmepriv->lock, &irqL);
		
	} else
	
	{
		_status = rtw_set_802_11_bssid_list_scan(padapter);
	}

	if(_status == _FALSE)
		ret = -1;

exit:
	#ifdef DBG_IOCTL
	DBG_871X("DBG_IOCTL %s:%d return %d\n",__FUNCTION__, __LINE__, ret);
	#endif

_func_exit_;

	return ret;	
}

static int rtw_wx_get_scan(struct net_device *dev, struct iw_request_info *a,
			     union iwreq_data *wrqu, char *extra)
{
	_irqL	irqL;
	_list					*plist, *phead;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct	mlme_priv	*pmlmepriv = &(padapter->mlmepriv);
	_queue				*queue	= &(pmlmepriv->scanned_queue);
	struct	wlan_network	*pnetwork = NULL;
	char *ev = extra;
	char *stop = ev + wrqu->data.length;
	u32 ret = 0;
	u32 cnt=0;
	u32 wait_for_surveydone;
	sint wait_status;
#ifdef CONFIG_CONCURRENT_MODE
	//PADAPTER pbuddy_adapter = padapter->pbuddy_adapter;
	//struct mlme_priv *pbuddy_mlmepriv = &(pbuddy_adapter->mlmepriv);	
#endif
#ifdef CONFIG_P2P
	struct	wifidirect_info*	pwdinfo = &padapter->wdinfo;
#endif //CONFIG_P2P
	RT_TRACE(_module_rtl871x_mlme_c_,_drv_info_,("rtw_wx_get_scan\n"));
	RT_TRACE(_module_rtl871x_ioctl_os_c,_drv_info_, (" Start of Query SIOCGIWSCAN .\n"));

	_func_enter_;

	#ifdef DBG_IOCTL
	DBG_871X("DBG_IOCTL %s:%d\n",__FUNCTION__, __LINE__);
	#endif

/*
#ifdef CONFIG_CONCURRENT_MODE
	if(padapter->iface_type > PRIMARY_IFACE)
	{
		ret = -EINVAL;
		goto exit;
	}
#endif
*/	
	if(padapter->pwrctrlpriv.brfoffbyhw && padapter->bDriverStopped)
	{
		ret = -EINVAL;
		goto exit;
	}
  
#ifdef CONFIG_P2P
	if(!rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE))
	{
		//	P2P is enabled
		wait_for_surveydone = 200;
	}
	else
	{
		//	P2P is disabled
		wait_for_surveydone = 100;
	}
#else
	{
		wait_for_surveydone = 100;
	}
#endif //CONFIG_P2P

/*
#ifdef CONFIG_CONCURRENT_MODE	
	if(pmlmepriv->scanning_via_buddy_intf == _TRUE)
	{
		pmlmepriv->scanning_via_buddy_intf = _FALSE;//reset

		// change pointers to buddy interface
		padapter = pbuddy_adapter;
		pmlmepriv = pbuddy_mlmepriv;
		queue = &(pbuddy_mlmepriv->scanned_queue);		
		
	}
#endif // CONFIG_CONCURRENT_MODE			
*/

	wait_status = _FW_UNDER_SURVEY
		#ifndef CONFIG_ANDROID
		|_FW_UNDER_LINKING
		#endif
	;

#ifdef CONFIG_DUALMAC_CONCURRENT
	while(dc_check_fwstate(padapter, wait_status)== _TRUE)
	{
		rtw_msleep_os(30);
		cnt++;
		if(cnt > wait_for_surveydone )
			break;
	}
#endif // CONFIG_DUALMAC_CONCURRENT

 	while(check_fwstate(pmlmepriv, wait_status) == _TRUE)
	{	
		rtw_msleep_os(30);
		cnt++;
		if(cnt > wait_for_surveydone )
			break;
	}

	_enter_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);

	phead = get_list_head(queue);
	plist = get_next(phead);
       
	while(1)
	{
		if (rtw_end_of_queue_search(phead,plist)== _TRUE)
			break;

		if((stop - ev) < SCAN_ITEM_SIZE) {
			ret = -E2BIG;
			break;
		}

		pnetwork = LIST_CONTAINOR(plist, struct wlan_network, list);

		//report network only if the current channel set contains the channel to which this network belongs
		if( _TRUE == rtw_is_channel_set_contains_channel(padapter->mlmeextpriv.channel_set, pnetwork->network.Configuration.DSConfig)
			#ifdef CONFIG_VALIDATE_SSID
			&& _TRUE == rtw_validate_ssid(&(pnetwork->network.Ssid))
			#endif
		)
		{
			ev=translate_scan(padapter, a, pnetwork, ev, stop);
		}

		plist = get_next(plist);
	
	}        

	_exit_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);

       wrqu->data.length = ev-extra;
	wrqu->data.flags = 0;
	
exit:		
	
	_func_exit_;	
	
	#ifdef DBG_IOCTL
	DBG_871X("DBG_IOCTL %s:%d return %d\n",__FUNCTION__, __LINE__, ret);
	#endif
	
	return ret ;
	
}

//set ssid flow
//s1. rtw_set_802_11_infrastructure_mode()
//s2. set_802_11_authenticaion_mode()
//s3. set_802_11_encryption_mode()
//s4. rtw_set_802_11_ssid()
static int rtw_wx_set_essid(struct net_device *dev, 
			      struct iw_request_info *a,
			      union iwreq_data *wrqu, char *extra)
{
	_irqL irqL;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	_queue *queue = &pmlmepriv->scanned_queue;
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
	_list *phead;
	s8 status = _TRUE;
	struct wlan_network *pnetwork = NULL;
	NDIS_802_11_AUTHENTICATION_MODE authmode;	
	NDIS_802_11_SSID ndis_ssid;	
	u8 *dst_ssid, *src_ssid;

	uint ret = 0, len;

	_func_enter_;
	
	#ifdef DBG_IOCTL
	DBG_871X("DBG_IOCTL %s:%d\n",__FUNCTION__, __LINE__);
	#endif
	
/*
#ifdef CONFIG_CONCURRENT_MODE
	if(padapter->iface_type > PRIMARY_IFACE)
	{
		ret = -EINVAL;
		goto exit;
	}
#endif
*/

#ifdef CONFIG_CONCURRENT_MODE
	if (check_buddy_fwstate(padapter, _FW_UNDER_SURVEY|_FW_UNDER_LINKING) == _TRUE)
	{		
		DBG_871X("set ssid, but buddy_intf is under scanning or linking\n");
		
		ret = -EINVAL;
		
		goto exit;
	}
#endif

#ifdef CONFIG_DUALMAC_CONCURRENT
	if (dc_check_fwstate(padapter, _FW_UNDER_SURVEY|_FW_UNDER_LINKING)== _TRUE)
	{
		DBG_871X("set bssid, but buddy_intf is under scanning or linking\n");
		ret = -EINVAL;
		goto exit;
	}
#endif

	RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_info_,
		 ("+rtw_wx_set_essid: fw_state=0x%08x\n", get_fwstate(pmlmepriv)));
	if(_FAIL == rfpwrstate_check(padapter))
	{		
		ret = -1;
		goto exit;
	}

	if(!padapter->bup){
		ret = -1;
		goto exit;
	}

#if WIRELESS_EXT <= 20
	if ((wrqu->essid.length-1) > IW_ESSID_MAX_SIZE){
#else
	if (wrqu->essid.length > IW_ESSID_MAX_SIZE){
#endif
		ret= -E2BIG;
		goto exit;
	}
	
	if(check_fwstate(pmlmepriv, WIFI_AP_STATE)) {
		ret = -1;
		goto exit;
	}		
	
	authmode = padapter->securitypriv.ndisauthtype;
	DBG_871X("=>%s\n",__FUNCTION__);
	if (wrqu->essid.flags && wrqu->essid.length)
	{
		// Commented by Albert 20100519
		// We got the codes in "set_info" function of iwconfig source code.
		//	=========================================
		//	wrq.u.essid.length = strlen(essid) + 1;
	  	//	if(we_kernel_version > 20)
		//		wrq.u.essid.length--;
		//	=========================================
		//	That means, if the WIRELESS_EXT less than or equal to 20, the correct ssid len should subtract 1.
#if WIRELESS_EXT <= 20
		len = ((wrqu->essid.length-1) < IW_ESSID_MAX_SIZE) ? (wrqu->essid.length-1) : IW_ESSID_MAX_SIZE;
#else
		len = (wrqu->essid.length < IW_ESSID_MAX_SIZE) ? wrqu->essid.length : IW_ESSID_MAX_SIZE;
#endif

		DBG_871X("ssid=%s, len=%d\n", extra, wrqu->essid.length);

		_rtw_memset(&ndis_ssid, 0, sizeof(NDIS_802_11_SSID));
		ndis_ssid.SsidLength = len;
		_rtw_memcpy(ndis_ssid.Ssid, extra, len);		
		src_ssid = ndis_ssid.Ssid;
		
		RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_info_, ("rtw_wx_set_essid: ssid=[%s]\n", src_ssid));
		_enter_critical_bh(&queue->lock, &irqL);
	       phead = get_list_head(queue);
              pmlmepriv->pscanned = get_next(phead);

		while (1)
		{			
			if (rtw_end_of_queue_search(phead, pmlmepriv->pscanned) == _TRUE)
			{
#if 0			
				if(check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == _TRUE)
				{
	            			rtw_set_802_11_ssid(padapter, &ndis_ssid);

		    			goto exit;                    
				}
				else
				{
					RT_TRACE(_module_rtl871x_ioctl_os_c,_drv_info_,("rtw_wx_set_ssid(): scanned_queue is empty\n"));
					ret = -EINVAL;
					goto exit;
				}
#endif			
			        RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_warning_,
					 ("rtw_wx_set_essid: scan_q is empty, set ssid to check if scanning again!\n"));

				break;
			}
	
			pnetwork = LIST_CONTAINOR(pmlmepriv->pscanned, struct wlan_network, list);

			pmlmepriv->pscanned = get_next(pmlmepriv->pscanned);

			dst_ssid = pnetwork->network.Ssid.Ssid;

			RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_info_,
				 ("rtw_wx_set_essid: dst_ssid=%s\n",
				  pnetwork->network.Ssid.Ssid));

			if ((_rtw_memcmp(dst_ssid, src_ssid, ndis_ssid.SsidLength) == _TRUE) &&
				(pnetwork->network.Ssid.SsidLength==ndis_ssid.SsidLength))
			{
				RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_info_,
					 ("rtw_wx_set_essid: find match, set infra mode\n"));
				
				if(check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == _TRUE)
				{
					if(pnetwork->network.InfrastructureMode != pmlmepriv->cur_network.network.InfrastructureMode)
						continue;
				}	
					
				if (rtw_set_802_11_infrastructure_mode(padapter, pnetwork->network.InfrastructureMode) == _FALSE)
				{
					ret = -1;
					_exit_critical_bh(&queue->lock, &irqL);
					goto exit;
				}

				break;			
			}
		}
		_exit_critical_bh(&queue->lock, &irqL);
		RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_info_,
			 ("set ssid: set_802_11_auth. mode=%d\n", authmode));
		rtw_set_802_11_authentication_mode(padapter, authmode);
		//set_802_11_encryption_mode(padapter, padapter->securitypriv.ndisencryptstatus);
		if (rtw_set_802_11_ssid(padapter, &ndis_ssid) == _FALSE) {
			ret = -1;
			goto exit;
		}	
	}			
	
exit:

	DBG_871X("<=%s, ret %d\n",__FUNCTION__, ret);
	
	#ifdef DBG_IOCTL
	DBG_871X("DBG_IOCTL %s:%d return %d\n",__FUNCTION__, __LINE__, ret);
	#endif
	
	_func_exit_;
	
	return ret;	
}

static int rtw_wx_get_essid(struct net_device *dev, 
			      struct iw_request_info *a,
			      union iwreq_data *wrqu, char *extra)
{
	u32 len,ret = 0;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct	mlme_priv	*pmlmepriv = &(padapter->mlmepriv);
	WLAN_BSSID_EX  *pcur_bss = &pmlmepriv->cur_network.network;

	RT_TRACE(_module_rtl871x_mlme_c_,_drv_info_,("rtw_wx_get_essid\n"));

	_func_enter_;

	if ( (check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE) ||
	      (check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == _TRUE))
	{
		len = pcur_bss->Ssid.SsidLength;

		wrqu->essid.length = len;
			
		_rtw_memcpy(extra, pcur_bss->Ssid.Ssid, len);

		wrqu->essid.flags = 1;
	}
	else
	{
		ret = -1;
		goto exit;
	}

exit:
	
	_func_exit_;
	
	return ret;
	
}

static int rtw_wx_set_rate(struct net_device *dev, 
			      struct iw_request_info *a,
			      union iwreq_data *wrqu, char *extra)
{
	int	i, ret = 0;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	u8	datarates[NumRates];
	u32	target_rate = wrqu->bitrate.value;
	u32	fixed = wrqu->bitrate.fixed;
	u32	ratevalue = 0;
	 u8 mpdatarate[NumRates]={11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 0xff};

_func_enter_;

	RT_TRACE(_module_rtl871x_mlme_c_,_drv_info_,(" rtw_wx_set_rate \n"));
	RT_TRACE(_module_rtl871x_ioctl_os_c,_drv_info_,("target_rate = %d, fixed = %d\n",target_rate,fixed));
	
	if(target_rate == -1){
		ratevalue = 11;
		goto set_rate;
	}
	target_rate = target_rate/100000;

	switch(target_rate){
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

	for(i=0; i<NumRates; i++)
	{
		if(ratevalue==mpdatarate[i])
		{
			datarates[i] = mpdatarate[i];
			if(fixed == 0)
				break;
		}
		else{
			datarates[i] = 0xff;
		}

		RT_TRACE(_module_rtl871x_ioctl_os_c,_drv_info_,("datarate_inx=%d\n",datarates[i]));
	}

	if( rtw_setdatarate_cmd(padapter, datarates) !=_SUCCESS){
		RT_TRACE(_module_rtl871x_ioctl_os_c,_drv_err_,("rtw_wx_set_rate Fail!!!\n"));
		ret = -1;
	}

_func_exit_;

	return ret;
}

static int rtw_wx_get_rate(struct net_device *dev, 
			     struct iw_request_info *info, 
			     union iwreq_data *wrqu, char *extra)
{	
	u16 max_rate = 0;

	max_rate = rtw_get_cur_max_rate((_adapter *)rtw_netdev_priv(dev));

	if(max_rate == 0)
		return -EPERM;
	
	wrqu->bitrate.fixed = 0;	/* no auto select */
	wrqu->bitrate.value = max_rate * 100000;

	return 0;
}

static int rtw_wx_get_rts(struct net_device *dev, 
			     struct iw_request_info *info, 
			     union iwreq_data *wrqu, char *extra)
{
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	
	_func_enter_;
	RT_TRACE(_module_rtl871x_mlme_c_,_drv_info_,(" rtw_wx_get_rts \n"));
	
	wrqu->rts.value = padapter->registrypriv.rts_thresh;
	wrqu->rts.fixed = 0;	/* no auto select */
	//wrqu->rts.disabled = (wrqu->rts.value == DEFAULT_RTS_THRESHOLD);
	
	_func_exit_;
	
	return 0;
}

static int rtw_wx_set_frag(struct net_device *dev, 
			     struct iw_request_info *info, 
			     union iwreq_data *wrqu, char *extra)
{
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);

	_func_enter_;
	
	if (wrqu->frag.disabled)
		padapter->xmitpriv.frag_len = MAX_FRAG_THRESHOLD;
	else {
		if (wrqu->frag.value < MIN_FRAG_THRESHOLD ||
		    wrqu->frag.value > MAX_FRAG_THRESHOLD)
			return -EINVAL;
		
		padapter->xmitpriv.frag_len = wrqu->frag.value & ~0x1;
	}
	
	_func_exit_;
	
	return 0;
	
}


static int rtw_wx_get_frag(struct net_device *dev, 
			     struct iw_request_info *info, 
			     union iwreq_data *wrqu, char *extra)
{
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	
	_func_enter_;
	
	wrqu->frag.value = padapter->xmitpriv.frag_len;
	wrqu->frag.fixed = 0;	/* no auto select */
	//wrqu->frag.disabled = (wrqu->frag.value == DEFAULT_FRAG_THRESHOLD);
	
	_func_exit_;
	
	return 0;
}

static int rtw_wx_get_retry(struct net_device *dev, 
			     struct iw_request_info *info, 
			     union iwreq_data *wrqu, char *extra)
{
	//_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);

	
	wrqu->retry.value = 7;
	wrqu->retry.fixed = 0;	/* no auto select */
	wrqu->retry.disabled = 1;
	
	return 0;

}	

#if 0
#define IW_ENCODE_INDEX		0x00FF	/* Token index (if needed) */
#define IW_ENCODE_FLAGS		0xFF00	/* Flags defined below */
#define IW_ENCODE_MODE		0xF000	/* Modes defined below */
#define IW_ENCODE_DISABLED	0x8000	/* Encoding disabled */
#define IW_ENCODE_ENABLED	0x0000	/* Encoding enabled */
#define IW_ENCODE_RESTRICTED	0x4000	/* Refuse non-encoded packets */
#define IW_ENCODE_OPEN		0x2000	/* Accept non-encoded packets */
#define IW_ENCODE_NOKEY		0x0800  /* Key is write only, so not present */
#define IW_ENCODE_TEMP		0x0400  /* Temporary key */
/*
iwconfig wlan0 key on -> flags = 0x6001 -> maybe it means auto
iwconfig wlan0 key off -> flags = 0x8800
iwconfig wlan0 key open -> flags = 0x2800
iwconfig wlan0 key open 1234567890 -> flags = 0x2000
iwconfig wlan0 key restricted -> flags = 0x4800
iwconfig wlan0 key open [3] 1234567890 -> flags = 0x2003
iwconfig wlan0 key restricted [2] 1234567890 -> flags = 0x4002
iwconfig wlan0 key open [3] -> flags = 0x2803
iwconfig wlan0 key restricted [2] -> flags = 0x4802
*/
#endif

static int rtw_wx_set_enc(struct net_device *dev, 
			    struct iw_request_info *info, 
			    union iwreq_data *wrqu, char *keybuf)
{	
	u32 key, ret = 0;
	u32 keyindex_provided;
	NDIS_802_11_WEP	 wep;	
	NDIS_802_11_AUTHENTICATION_MODE authmode;

	struct iw_point *erq = &(wrqu->encoding);
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
	DBG_871X("+rtw_wx_set_enc, flags=0x%x\n", erq->flags);

	_rtw_memset(&wep, 0, sizeof(NDIS_802_11_WEP));
	
	key = erq->flags & IW_ENCODE_INDEX;
	
	_func_enter_;	

	if (erq->flags & IW_ENCODE_DISABLED)
	{
		DBG_871X("EncryptionDisabled\n");
		padapter->securitypriv.ndisencryptstatus = Ndis802_11EncryptionDisabled;
		padapter->securitypriv.dot11PrivacyAlgrthm=_NO_PRIVACY_;
		padapter->securitypriv.dot118021XGrpPrivacy=_NO_PRIVACY_;
		padapter->securitypriv.dot11AuthAlgrthm= dot11AuthAlgrthm_Open; //open system
  		authmode = Ndis802_11AuthModeOpen;
		padapter->securitypriv.ndisauthtype=authmode;
     		
		goto exit;
	}

	if (key) {
		if (key > WEP_KEYS)
			return -EINVAL;
		key--;
		keyindex_provided = 1;
	} 
	else
	{
		keyindex_provided = 0;
		key = padapter->securitypriv.dot11PrivacyKeyIndex;
		DBG_871X("rtw_wx_set_enc, key=%d\n", key);
	}
	
	//set authentication mode	
	if(erq->flags & IW_ENCODE_OPEN)
	{
		DBG_871X("rtw_wx_set_enc():IW_ENCODE_OPEN\n");
		padapter->securitypriv.ndisencryptstatus = Ndis802_11Encryption1Enabled;//Ndis802_11EncryptionDisabled;

#ifdef CONFIG_PLATFORM_MT53XX
		padapter->securitypriv.dot11AuthAlgrthm = dot11AuthAlgrthm_Auto;
#else
		padapter->securitypriv.dot11AuthAlgrthm= dot11AuthAlgrthm_Open;
#endif

		padapter->securitypriv.dot11PrivacyAlgrthm=_NO_PRIVACY_;
		padapter->securitypriv.dot118021XGrpPrivacy=_NO_PRIVACY_;
  		authmode = Ndis802_11AuthModeOpen;
		padapter->securitypriv.ndisauthtype=authmode;
	}	
	else if(erq->flags & IW_ENCODE_RESTRICTED)
	{		
		DBG_871X("rtw_wx_set_enc():IW_ENCODE_RESTRICTED\n");
		padapter->securitypriv.ndisencryptstatus = Ndis802_11Encryption1Enabled;

#ifdef CONFIG_PLATFORM_MT53XX
		padapter->securitypriv.dot11AuthAlgrthm = dot11AuthAlgrthm_Auto;
#else
		padapter->securitypriv.dot11AuthAlgrthm= dot11AuthAlgrthm_Shared;
#endif

		padapter->securitypriv.dot11PrivacyAlgrthm=_WEP40_;
		padapter->securitypriv.dot118021XGrpPrivacy=_WEP40_;			
		authmode = Ndis802_11AuthModeShared;
		padapter->securitypriv.ndisauthtype=authmode;
	}
	else
	{
		DBG_871X("rtw_wx_set_enc():erq->flags=0x%x\n", erq->flags);

		padapter->securitypriv.ndisencryptstatus = Ndis802_11Encryption1Enabled;//Ndis802_11EncryptionDisabled;
		padapter->securitypriv.dot11AuthAlgrthm= dot11AuthAlgrthm_Open; //open system
		padapter->securitypriv.dot11PrivacyAlgrthm=_NO_PRIVACY_;
		padapter->securitypriv.dot118021XGrpPrivacy=_NO_PRIVACY_;
  		authmode = Ndis802_11AuthModeOpen;
		padapter->securitypriv.ndisauthtype=authmode;
	}
	
	wep.KeyIndex = key;
	if (erq->length > 0)
	{
		wep.KeyLength = erq->length <= 5 ? 5 : 13;

		wep.Length = wep.KeyLength + FIELD_OFFSET(NDIS_802_11_WEP, KeyMaterial);
	}
	else
	{
		wep.KeyLength = 0 ;
		
		if(keyindex_provided == 1)// set key_id only, no given KeyMaterial(erq->length==0).
		{
			padapter->securitypriv.dot11PrivacyKeyIndex = key;

			DBG_871X("(keyindex_provided == 1), keyid=%d, key_len=%d\n", key, padapter->securitypriv.dot11DefKeylen[key]);

			switch(padapter->securitypriv.dot11DefKeylen[key])
			{
				case 5:
					padapter->securitypriv.dot11PrivacyAlgrthm=_WEP40_;					
					break;
				case 13:
					padapter->securitypriv.dot11PrivacyAlgrthm=_WEP104_;					
					break;
				default:
					padapter->securitypriv.dot11PrivacyAlgrthm=_NO_PRIVACY_;					
					break;
			}
				
			goto exit;
			
		}
		
	}

	wep.KeyIndex |= 0x80000000;

	_rtw_memcpy(wep.KeyMaterial, keybuf, wep.KeyLength);
	
	if (rtw_set_802_11_add_wep(padapter, &wep) == _FALSE) {
		if(rf_on == pwrpriv->rf_pwrstate )
			ret = -EOPNOTSUPP;
		goto exit;
	}	

exit:
	
	_func_exit_;
	
	return ret;
	
}

static int rtw_wx_get_enc(struct net_device *dev, 
			    struct iw_request_info *info, 
			    union iwreq_data *wrqu, char *keybuf)
{
	uint key, ret =0;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct iw_point *erq = &(wrqu->encoding);
	struct	mlme_priv	*pmlmepriv = &(padapter->mlmepriv);

	_func_enter_;
	
	if(check_fwstate(pmlmepriv, _FW_LINKED) != _TRUE)
	{
		 if(check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) != _TRUE)
		 {
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
	} else
	{
		key = padapter->securitypriv.dot11PrivacyKeyIndex;
	}	

	erq->flags = key + 1;

	//if(padapter->securitypriv.ndisauthtype == Ndis802_11AuthModeOpen)
	//{
	//      erq->flags |= IW_ENCODE_OPEN;
	//}	  
	
	switch(padapter->securitypriv.ndisencryptstatus)
	{
		case Ndis802_11EncryptionNotSupported:
		case Ndis802_11EncryptionDisabled:

		erq->length = 0;
		erq->flags |= IW_ENCODE_DISABLED;
	
		break;
		
		case Ndis802_11Encryption1Enabled:					
		
		erq->length = padapter->securitypriv.dot11DefKeylen[key];		

		if(erq->length)
		{
			_rtw_memcpy(keybuf, padapter->securitypriv.dot11DefKey[key].skey, padapter->securitypriv.dot11DefKeylen[key]);
		
		erq->flags |= IW_ENCODE_ENABLED;

			if(padapter->securitypriv.ndisauthtype == Ndis802_11AuthModeOpen)
			{
	     			erq->flags |= IW_ENCODE_OPEN;
			}
			else if(padapter->securitypriv.ndisauthtype == Ndis802_11AuthModeShared)
			{
		erq->flags |= IW_ENCODE_RESTRICTED;
			}	
		}	
		else
		{
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
	
	_func_exit_;
	
	return ret;
	
}				     

static int rtw_wx_get_power(struct net_device *dev, 
			     struct iw_request_info *info, 
			     union iwreq_data *wrqu, char *extra)
{
	//_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	
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
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	
       ret = rtw_set_wpa_ie(padapter, extra, wrqu->data.length);
	   
	return ret;
}	

static int rtw_wx_set_auth(struct net_device *dev, 
			     struct iw_request_info *info, 
			     union iwreq_data *wrqu, char *extra)
{
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct iw_param *param = (struct iw_param*)&(wrqu->param);
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
        {
	    if ( param->value )
            {  // wpa_supplicant is enabling the tkip countermeasure.
               padapter->securitypriv.btkip_countermeasure = _TRUE; 
            }
            else
            {  // wpa_supplicant is disabling the tkip countermeasure.
               padapter->securitypriv.btkip_countermeasure = _FALSE; 
            }
		break;
        }
	case IW_AUTH_DROP_UNENCRYPTED:
		{
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

			if(padapter->securitypriv.ndisencryptstatus == Ndis802_11Encryption1Enabled)
			{
				break;//it means init value, or using wep, ndisencryptstatus = Ndis802_11Encryption1Enabled, 
						// then it needn't reset it;
			}
			
			if(param->value){
				padapter->securitypriv.ndisencryptstatus = Ndis802_11EncryptionDisabled;
				padapter->securitypriv.dot11PrivacyAlgrthm=_NO_PRIVACY_;
				padapter->securitypriv.dot118021XGrpPrivacy=_NO_PRIVACY_;
				padapter->securitypriv.dot11AuthAlgrthm= dot11AuthAlgrthm_Open; //open system
				padapter->securitypriv.ndisauthtype=Ndis802_11AuthModeOpen;
			}
			
			break;
		}

	case IW_AUTH_80211_AUTH_ALG:

		#if defined(CONFIG_ANDROID) || 1
		/*
		 *  It's the starting point of a link layer connection using wpa_supplicant
		*/
		if(check_fwstate(&padapter->mlmepriv, _FW_LINKED)) {
			rtw_disassoc_cmd(padapter);
			DBG_871X("%s...call rtw_indicate_disconnect\n ",__FUNCTION__);
			rtw_indicate_disconnect(padapter);
			rtw_free_assoc_resources(padapter, 1);
		}
		#endif


		ret = wpa_set_auth_algs(dev, (u32)param->value);		
	
		break;

	case IW_AUTH_WPA_ENABLED:

		//if(param->value)
		//	padapter->securitypriv.dot11AuthAlgrthm = dot11AuthAlgrthm_8021X; //802.1x
		//else
		//	padapter->securitypriv.dot11AuthAlgrthm = dot11AuthAlgrthm_Open;//open system
		
		//_disassociate(priv);
		
		break;

	case IW_AUTH_RX_UNENCRYPTED_EAPOL:
		//ieee->ieee802_1x = param->value;
		break;

	case IW_AUTH_PRIVACY_INVOKED:
		//ieee->privacy_invoked = param->value;
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
	int ret=0;

	param_len = sizeof(struct ieee_param) + pext->key_len;
	param = (struct ieee_param *)rtw_malloc(param_len);
	if (param == NULL)
		return -1;
	
	_rtw_memset(param, 0, param_len);

	param->cmd = IEEE_CMD_SET_ENCRYPTION;
	_rtw_memset(param->sta_addr, 0xff, ETH_ALEN);


	switch (pext->alg) {
	case IW_ENCODE_ALG_NONE:
		//todo: remove key 
		//remove = 1;	
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

	
	if(pext->ext_flags & IW_ENCODE_EXT_GROUP_KEY)//?
	{
		param->u.crypt.set_tx = 0;
	}

	if (pext->ext_flags & IW_ENCODE_EXT_SET_TX_KEY)//?
	{
		param->u.crypt.set_tx = 1;
	}

	param->u.crypt.idx = (pencoding->flags&0x00FF) -1 ;
	
	if (pext->ext_flags & IW_ENCODE_EXT_RX_SEQ_VALID) 
	{	
		_rtw_memcpy(param->u.crypt.seq, pext->rx_seq, 8);
	}

	if(pext->key_len)
	{
		param->u.crypt.key_len = pext->key_len;
		//_rtw_memcpy(param + 1, pext + 1, pext->key_len);
		_rtw_memcpy(param->u.crypt.key, pext + 1, pext->key_len);
	}	

	
	if (pencoding->flags & IW_ENCODE_DISABLED)
	{		
		//todo: remove key 
		//remove = 1;		
	}	
	
	ret =  wpa_set_encryption(dev, param, param_len);	
	

	if(param)
	{
		rtw_mfree((u8*)param, param_len);
	}
		
	
	return ret;		

}


static int rtw_wx_get_nick(struct net_device *dev, 
			     struct iw_request_info *info, 
			     union iwreq_data *wrqu, char *extra)
{	
	//_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	 //struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	 //struct security_priv *psecuritypriv = &padapter->securitypriv;

	if(extra)
	{
		wrqu->data.length = 14;
		wrqu->data.flags = 1;
		_rtw_memcpy(extra, "<WIFI@REALTEK>", 14);
	}

	//rtw_signal_process(pid, SIGUSR1); //for test

	//dump debug info here	
/*
	u32 dot11AuthAlgrthm;		// 802.11 auth, could be open, shared, and 8021x
	u32 dot11PrivacyAlgrthm;	// This specify the privacy for shared auth. algorithm.
	u32 dot118021XGrpPrivacy;	// This specify the privacy algthm. used for Grp key 
	u32 ndisauthtype;
	u32 ndisencryptstatus;
*/

	//DBG_871X("auth_alg=0x%x, enc_alg=0x%x, auth_type=0x%x, enc_type=0x%x\n", 
	//		psecuritypriv->dot11AuthAlgrthm, psecuritypriv->dot11PrivacyAlgrthm,
	//		psecuritypriv->ndisauthtype, psecuritypriv->ndisencryptstatus);
	
	//DBG_871X("enc_alg=0x%x\n", psecuritypriv->dot11PrivacyAlgrthm);
	//DBG_871X("auth_type=0x%x\n", psecuritypriv->ndisauthtype);
	//DBG_871X("enc_type=0x%x\n", psecuritypriv->ndisencryptstatus);

#if 0
	DBG_871X("dbg(0x210)=0x%x\n", rtw_read32(padapter, 0x210));
	DBG_871X("dbg(0x608)=0x%x\n", rtw_read32(padapter, 0x608));
	DBG_871X("dbg(0x280)=0x%x\n", rtw_read32(padapter, 0x280));
	DBG_871X("dbg(0x284)=0x%x\n", rtw_read32(padapter, 0x284));
	DBG_871X("dbg(0x288)=0x%x\n", rtw_read32(padapter, 0x288));
	
	DBG_871X("dbg(0x664)=0x%x\n", rtw_read32(padapter, 0x664));


	DBG_871X("\n");

	DBG_871X("dbg(0x430)=0x%x\n", rtw_read32(padapter, 0x430));
	DBG_871X("dbg(0x438)=0x%x\n", rtw_read32(padapter, 0x438));

	DBG_871X("dbg(0x440)=0x%x\n", rtw_read32(padapter, 0x440));
	
	DBG_871X("dbg(0x458)=0x%x\n", rtw_read32(padapter, 0x458));
	
	DBG_871X("dbg(0x484)=0x%x\n", rtw_read32(padapter, 0x484));
	DBG_871X("dbg(0x488)=0x%x\n", rtw_read32(padapter, 0x488));
	
	DBG_871X("dbg(0x444)=0x%x\n", rtw_read32(padapter, 0x444));
	DBG_871X("dbg(0x448)=0x%x\n", rtw_read32(padapter, 0x448));
	DBG_871X("dbg(0x44c)=0x%x\n", rtw_read32(padapter, 0x44c));
	DBG_871X("dbg(0x450)=0x%x\n", rtw_read32(padapter, 0x450));
#endif
	
	return 0;

}

static int rtw_wx_read32(struct net_device *dev,
                            struct iw_request_info *info,
                            union iwreq_data *wrqu, char *extra)
{
	PADAPTER padapter;
	struct iw_point *p;
	u16 len;
	u32 addr;
	u32 data32;
	u32 bytes;
	u8 *ptmp;


	padapter = (PADAPTER)rtw_netdev_priv(dev);
	p = &wrqu->data;
	len = p->length;
	ptmp = (u8*)rtw_malloc(len);
	if (NULL == ptmp)
		return -ENOMEM;

	if (copy_from_user(ptmp, p->pointer, len)) {
		rtw_mfree(ptmp, len);
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
			DBG_871X(KERN_INFO "%s: usage> read [bytes],[address(hex)]\n", __func__);
			return -EINVAL;
	}
	DBG_871X(KERN_INFO "%s: addr=0x%08X data=%s\n", __func__, addr, extra);

	rtw_mfree(ptmp, len);

	return 0;
}

static int rtw_wx_write32(struct net_device *dev,
                            struct iw_request_info *info,
                            union iwreq_data *wrqu, char *extra)
{
	PADAPTER padapter = (PADAPTER)rtw_netdev_priv(dev);

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
			DBG_871X(KERN_INFO "%s: addr=0x%08X data=0x%02X\n", __func__, addr, (u8)data32);
			break;
		case 2:
			rtw_write16(padapter, addr, (u16)data32);
			DBG_871X(KERN_INFO "%s: addr=0x%08X data=0x%04X\n", __func__, addr, (u16)data32);
			break;
		case 4:
			rtw_write32(padapter, addr, data32);
			DBG_871X(KERN_INFO "%s: addr=0x%08X data=0x%08X\n", __func__, addr, data32);
			break;
		default:
			DBG_871X(KERN_INFO "%s: usage> write [bytes],[address(hex)],[data(hex)]\n", __func__);
			return -EINVAL;
	}

	return 0;
}

static int rtw_wx_read_rf(struct net_device *dev,
                            struct iw_request_info *info,
                            union iwreq_data *wrqu, char *extra)
{
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	u32 path, addr, data32;


	path = *(u32*)extra;
	addr = *((u32*)extra + 1);
	data32 = rtw_hal_read_rfreg(padapter, path, addr, 0xFFFFF);
//	DBG_871X("%s: path=%d addr=0x%02x data=0x%05x\n", __func__, path, addr, data32);
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
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	u32 path, addr, data32;


	path = *(u32*)extra;
	addr = *((u32*)extra + 1);
	data32 = *((u32*)extra + 2);
//	DBG_871X("%s: path=%d addr=0x%02x data=0x%05x\n", __func__, path, addr, data32);
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
	//_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);	
	//struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);

	//DBG_871X("cmd_code=%x, fwstate=0x%x\n", a->cmd, get_fwstate(pmlmepriv));
	
	return -1;
	
}

static int rtw_wx_set_channel_plan(struct net_device *dev,
                               struct iw_request_info *info,
                               union iwreq_data *wrqu, char *extra)
{
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct registry_priv *pregistrypriv = &padapter->registrypriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	extern int rtw_channel_plan;
	u8 channel_plan_req = (u8) (*((int *)wrqu));

	#if 0
	rtw_channel_plan = (int)wrqu->data.pointer;
	pregistrypriv->channel_plan = rtw_channel_plan;
	pmlmepriv->ChannelPlan = pregistrypriv->channel_plan;
	#endif

	if( _SUCCESS == rtw_set_chplan_cmd(padapter, channel_plan_req, 1) ) {
		DBG_871X("\n======== Set channel_plan = 0x%02X ========\n", pmlmepriv->ChannelPlan);
	} else 
		return -EPERM;

	return 0;
}

static int rtw_wx_set_mtk_wps_probe_ie(struct net_device *dev,
		struct iw_request_info *a,
		union iwreq_data *wrqu, char *b)
{
#ifdef CONFIG_PLATFORM_MT53XX
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

	RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_notice_,
		 ("WLAN IOCTL: cmd_code=%x, fwstate=0x%x\n",
		  a->cmd, get_fwstate(pmlmepriv)));
#endif
	return 0;
}

static int rtw_wx_get_sensitivity(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *buf)
{
#ifdef CONFIG_PLATFORM_MT53XX
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);

	//	Modified by Albert 20110914
	//	This is in dbm format for MTK platform.
	wrqu->qual.level = padapter->recvpriv.rssi;
	DBG_871X(" level = %u\n",  wrqu->qual.level );
#endif
	return 0;
}

static int rtw_wx_set_mtk_wps_ie(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *extra)
{
#ifdef CONFIG_PLATFORM_MT53XX
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);

	return rtw_set_wpa_ie(padapter, wrqu->data.pointer, wrqu->data.length);
#else
	return 0;
#endif
}

/*
typedef int (*iw_handler)(struct net_device *dev, struct iw_request_info *info,
			  union iwreq_data *wrqu, char *extra);
*/
/*
 *	For all data larger than 16 octets, we need to use a
 *	pointer to memory allocated in user space.
 */
static  int rtw_drvext_hdl(struct net_device *dev, struct iw_request_info *info,
						union iwreq_data *wrqu, char *extra)
{

 #if 0
struct	iw_point
{
  void __user	*pointer;	/* Pointer to the data  (in user space) */
  __u16		length;		/* number of fields or size in bytes */
  __u16		flags;		/* Optional params */
};
 #endif

#ifdef CONFIG_DRVEXT_MODULE
	u8 res;
	struct drvext_handler *phandler;	
	struct drvext_oidparam *poidparam;		
	int ret;
	u16 len;
	u8 *pparmbuf, bset;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct iw_point *p = &wrqu->data;

	if( (!p->length) || (!p->pointer)){
		ret = -EINVAL;
		goto _rtw_drvext_hdl_exit;
	}
	
	
	bset = (u8)(p->flags&0xFFFF);
	len = p->length;
	pparmbuf = (u8*)rtw_malloc(len);
	if (pparmbuf == NULL){
		ret = -ENOMEM;
		goto _rtw_drvext_hdl_exit;
	}
	
	if(bset)//set info
	{
		if (copy_from_user(pparmbuf, p->pointer,len)) {
			rtw_mfree(pparmbuf, len);
			ret = -EFAULT;
			goto _rtw_drvext_hdl_exit;
		}		
	}
	else//query info
	{
	
	}

	
	//
	poidparam = (struct drvext_oidparam *)pparmbuf;	
	
	RT_TRACE(_module_rtl871x_ioctl_os_c,_drv_info_,("drvext set oid subcode [%d], len[%d], InformationBufferLength[%d]\r\n",
        					 poidparam->subcode, poidparam->len, len));


	//check subcode	
	if ( poidparam->subcode >= MAX_DRVEXT_HANDLERS)
	{
		RT_TRACE(_module_rtl871x_ioctl_os_c,_drv_err_,("no matching drvext handlers\r\n"));		
		ret = -EINVAL;
		goto _rtw_drvext_hdl_exit;
	}


	if ( poidparam->subcode >= MAX_DRVEXT_OID_SUBCODES)
	{
		RT_TRACE(_module_rtl871x_ioctl_os_c,_drv_err_,("no matching drvext subcodes\r\n"));		
		ret = -EINVAL;
		goto _rtw_drvext_hdl_exit;
	}


	phandler = drvextoidhandlers + poidparam->subcode;

	if (poidparam->len != phandler->parmsize)
	{
		RT_TRACE(_module_rtl871x_ioctl_os_c,_drv_err_,("no matching drvext param size %d vs %d\r\n",			
						poidparam->len , phandler->parmsize));		
		ret = -EINVAL;		
		goto _rtw_drvext_hdl_exit;
	}


	res = phandler->handler(&padapter->drvextpriv, bset, poidparam->data);

	if(res==0)
	{
		ret = 0;
			
		if (bset == 0x00) {//query info
			//_rtw_memcpy(p->pointer, pparmbuf, len);
			if (copy_to_user(p->pointer, pparmbuf, len))
				ret = -EFAULT;
		}		
	}		
	else
		ret = -EFAULT;

	
_rtw_drvext_hdl_exit:	
	
	return ret;	
	
#endif

	return 0;

}

static void rtw_dbg_mode_hdl(_adapter *padapter, u32 id, u8 *pdata, u32 len)
{
	pRW_Reg 	RegRWStruct;
	struct rf_reg_param *prfreg;
	u8 path;
	u8 offset;
	u32 value;

	DBG_871X("%s\n", __FUNCTION__);

	switch(id)
	{
		case GEN_MP_IOCTL_SUBCODE(MP_START):
			DBG_871X("871x_driver is only for normal mode, can't enter mp mode\n");
			break;
		case GEN_MP_IOCTL_SUBCODE(READ_REG):
			RegRWStruct = (pRW_Reg)pdata;
			switch (RegRWStruct->width)
			{
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
			RegRWStruct = (pRW_Reg)pdata;
			switch (RegRWStruct->width)
			{
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
			DBG_871X("==> trigger gpio 0\n");
			rtw_hal_set_hwreg(padapter, HW_VAR_TRIGGER_GPIO_0, 0);
			break;	
#ifdef CONFIG_BT_COEXIST
		case GEN_MP_IOCTL_SUBCODE(SET_DM_BT):			
			DBG_871X("==> set dm_bt_coexist:%x\n",*(u8 *)pdata);
			rtw_hal_set_hwreg(padapter, HW_VAR_BT_SET_COEXIST, pdata);
			break;
		case GEN_MP_IOCTL_SUBCODE(DEL_BA):
			DBG_871X("==> delete ba:%x\n",*(u8 *)pdata);
			rtw_hal_set_hwreg(padapter, HW_VAR_BT_ISSUE_DELBA, pdata);
			break;
#endif
#ifdef DBG_CONFIG_ERROR_DETECT
		case GEN_MP_IOCTL_SUBCODE(GET_WIFI_STATUS):							
			*pdata = rtw_hal_sreset_get_wifi_status(padapter);                   
			break;
#endif
	
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
	uint status=0;
	u16 len;
	u8 *pparmbuf = NULL, bset;
	PADAPTER padapter = (PADAPTER)rtw_netdev_priv(dev);
	struct iw_point *p = &wrqu->data;

	//DBG_871X("+rtw_mp_ioctl_hdl\n");

	//mutex_lock(&ioctl_mutex);

	if ((!p->length) || (!p->pointer)) {
		ret = -EINVAL;
		goto _rtw_mp_ioctl_hdl_exit;
	}

	pparmbuf = NULL;
	bset = (u8)(p->flags & 0xFFFF);
	len = p->length;
	pparmbuf = (u8*)rtw_malloc(len);
	if (pparmbuf == NULL){
		ret = -ENOMEM;
		goto _rtw_mp_ioctl_hdl_exit;
	}

	if (copy_from_user(pparmbuf, p->pointer, len)) {
		ret = -EFAULT;
		goto _rtw_mp_ioctl_hdl_exit;
	}

	poidparam = (struct mp_ioctl_param *)pparmbuf;
	RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_info_,
		 ("rtw_mp_ioctl_hdl: subcode [%d], len[%d], buffer_len[%d]\r\n",
		  poidparam->subcode, poidparam->len, len));

	if (poidparam->subcode >= MAX_MP_IOCTL_SUBCODE) {
		RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_err_, ("no matching drvext subcodes\r\n"));
		ret = -EINVAL;
		goto _rtw_mp_ioctl_hdl_exit;
	}

	//DBG_871X("%s: %d\n", __func__, poidparam->subcode);

#ifdef CONFIG_MP_INCLUDED 
	phandler = mp_ioctl_hdl + poidparam->subcode;

	if ((phandler->paramsize != 0) && (poidparam->len < phandler->paramsize))
	{
		RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_err_,
			 ("no matching drvext param size %d vs %d\r\n",
			  poidparam->len, phandler->paramsize));
		ret = -EINVAL;
		goto _rtw_mp_ioctl_hdl_exit;
	}

	if (phandler->handler)
	{
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

		//todo:check status, BytesNeeded, etc.
	}
	else {
		DBG_871X("rtw_mp_ioctl_hdl(): err!, subcode=%d, oid=%d, handler=%p\n", 
			poidparam->subcode, phandler->oid, phandler->handler);
		ret = -EFAULT;
		goto _rtw_mp_ioctl_hdl_exit;
	}
#else

	rtw_dbg_mode_hdl(padapter, poidparam->subcode, poidparam->data, poidparam->len);
	
#endif

	if (bset == 0x00) {//query info
		if (copy_to_user(p->pointer, pparmbuf, len))
			ret = -EFAULT;
	}

	if (status) {
		ret = -EFAULT;
		goto _rtw_mp_ioctl_hdl_exit;
	}

_rtw_mp_ioctl_hdl_exit:

	if (pparmbuf)
		rtw_mfree(pparmbuf, len);

	//mutex_unlock(&ioctl_mutex);

	return ret;
}

static int rtw_get_ap_info(struct net_device *dev,
                               struct iw_request_info *info,
                               union iwreq_data *wrqu, char *extra)
{
	int bssid_match, ret = 0;
	u32 cnt=0, wpa_ielen;
	_irqL	irqL;
	_list	*plist, *phead;
	unsigned char *pbuf;
	u8 bssid[ETH_ALEN];
	char data[32];
	struct wlan_network *pnetwork = NULL;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);	
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);	
	_queue *queue = &(pmlmepriv->scanned_queue);
	struct iw_point *pdata = &wrqu->data;	

	DBG_871X("+rtw_get_aplist_info\n");

	if((padapter->bDriverStopped) || (pdata==NULL))
	{                
		ret= -EINVAL;
		goto exit;
	}		
  
 	while((check_fwstate(pmlmepriv, (_FW_UNDER_SURVEY|_FW_UNDER_LINKING))) == _TRUE)
	{	
		rtw_msleep_os(30);
		cnt++;
		if(cnt > 100)
			break;
	}
	

	//pdata->length = 0;//?	
	pdata->flags = 0;
	if(pdata->length>=32)
	{
		if(copy_from_user(data, pdata->pointer, 32))
		{
			ret= -EINVAL;
			goto exit;
		}
	}	
	else
	{
		ret= -EINVAL;
		goto exit;
	}	

	_enter_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);
	
	phead = get_list_head(queue);
	plist = get_next(phead);
       
	while(1)
	{
		if (rtw_end_of_queue_search(phead,plist)== _TRUE)
			break;


		pnetwork = LIST_CONTAINOR(plist, struct wlan_network, list);

		//if(hwaddr_aton_i(pdata->pointer, bssid)) 
		if(hwaddr_aton_i(data, bssid)) 
		{			
			DBG_871X("Invalid BSSID '%s'.\n", (u8*)data);
			_exit_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);
			return -EINVAL;
		}		
		
	
		if(_rtw_memcmp(bssid, pnetwork->network.MacAddress, ETH_ALEN) == _TRUE)//BSSID match, then check if supporting wpa/wpa2
		{
			DBG_871X("BSSID:" MAC_FMT "\n", MAC_ARG(bssid));
			
			pbuf = rtw_get_wpa_ie(&pnetwork->network.IEs[12], &wpa_ielen, pnetwork->network.IELength-12);				
			if(pbuf && (wpa_ielen>0))
			{
				pdata->flags = 1;
				break;
			}

			pbuf = rtw_get_wpa2_ie(&pnetwork->network.IEs[12], &wpa_ielen, pnetwork->network.IELength-12);
			if(pbuf && (wpa_ielen>0))
			{
				pdata->flags = 2;
				break;
			}
			
		}

		plist = get_next(plist);		
	
	}        

	_exit_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);

	if(pdata->length>=34)
	{
		if(copy_to_user((u8*)pdata->pointer+32, (u8*)&pdata->flags, 1))
		{
			ret= -EINVAL;
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
	_adapter *padapter = rtw_netdev_priv(dev);	
	int *pdata = (int *)wrqu;
	int selector;

	if((padapter->bDriverStopped) || (pdata==NULL))
	{                
		ret= -EINVAL;
		goto exit;
	}		
  
	selector = *pdata;
	if(selector < 3 && selector >=0) {
		padapter->pid[selector] = *(pdata+1);
		#ifdef CONFIG_GLOBAL_UI_PID
		ui_pid[selector] = *(pdata+1);
		#endif
		DBG_871X("%s set pid[%d]=%d\n", __FUNCTION__, selector ,padapter->pid[selector]);
	}
	else
		DBG_871X("%s selector %d error\n", __FUNCTION__, selector);

exit:
	
	return ret;
		
}

static int rtw_wps_start(struct net_device *dev,
                               struct iw_request_info *info,
                               union iwreq_data *wrqu, char *extra)
{
	
	int ret = 0;	
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);	
	struct iw_point *pdata = &wrqu->data;
	u32   u32wps_start = 0;
        unsigned int uintRet = 0;

        uintRet = copy_from_user( ( void* ) &u32wps_start, pdata->pointer, 4 );

	if((padapter->bDriverStopped) || (pdata==NULL))
	{                
		ret= -EINVAL;
		goto exit;
	}		

	if ( u32wps_start == 0 )
	{
		u32wps_start = *extra;
	}

	DBG_871X( "[%s] wps_start = %d\n", __FUNCTION__, u32wps_start );

	if ( u32wps_start == 1 ) // WPS Start
	{
		rtw_led_control(padapter, LED_CTL_START_WPS);
	}
	else if ( u32wps_start == 2 ) // WPS Stop because of wps success
	{
		rtw_led_control(padapter, LED_CTL_STOP_WPS);
	}
	else if ( u32wps_start == 3 ) // WPS Stop because of wps fail
	{
		rtw_led_control(padapter, LED_CTL_STOP_WPS_FAIL);
	}

#ifdef CONFIG_INTEL_WIDI
	process_intel_widi_wps_status(padapter, u32wps_start);
#endif //CONFIG_INTEL_WIDI
	
exit:
	
	return ret;
		
}

#ifdef CONFIG_P2P
static int rtw_wext_p2p_enable(struct net_device *dev,
                               struct iw_request_info *info,
                               union iwreq_data *wrqu, char *extra)
{
	
	int ret = 0;	
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);	
	struct iw_point *pdata = &wrqu->data;
	struct wifidirect_info *pwdinfo= &(padapter->wdinfo);
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
	enum P2P_ROLE init_role = P2P_ROLE_DISABLE;

	if(*extra == '0' )
		init_role = P2P_ROLE_DISABLE;
	else if(*extra == '1')
		init_role = P2P_ROLE_DEVICE;
	else if(*extra == '2')
		init_role = P2P_ROLE_CLIENT;
	else if(*extra == '3')
		init_role = P2P_ROLE_GO;

	if(_FAIL == rtw_p2p_enable(padapter, init_role))
	{
		ret = -EFAULT;
		goto exit;
	}

	//set channel/bandwidth
	if(init_role != P2P_ROLE_DISABLE) 
	{	
		u8 channel, ch_offset;
		u16 bwmode;

		if(rtw_p2p_chk_state(pwdinfo, P2P_STATE_LISTEN))
		{
			//	Stay at the listen state and wait for discovery.
			channel = pwdinfo->listen_channel;
			ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
			bwmode = HT_CHANNEL_WIDTH_20;			
		}
#ifdef CONFIG_CONCURRENT_MODE
		else if(rtw_p2p_chk_state(pwdinfo, P2P_STATE_IDLE))
		{
			_adapter				*pbuddy_adapter = padapter->pbuddy_adapter;
			//struct wifidirect_info	*pbuddy_wdinfo = &pbuddy_adapter->wdinfo;
			struct mlme_priv		*pbuddy_mlmepriv = &pbuddy_adapter->mlmepriv;
			struct mlme_ext_priv	*pbuddy_mlmeext = &pbuddy_adapter->mlmeextpriv;
			
			_set_timer( &pwdinfo->ap_p2p_switch_timer, pwdinfo->ext_listen_interval );
			if ( check_fwstate( pbuddy_mlmepriv, _FW_LINKED ) )
			{
				pwdinfo->operating_channel = pbuddy_mlmeext->cur_channel;
				//	How about the ch_offset and bwmode ??
			}
			else
			{
				pwdinfo->operating_channel = pwdinfo->listen_channel;
			}
		}
#endif
		else
		{
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
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);	
	struct iw_point *pdata = &wrqu->data;
	struct wifidirect_info *pwdinfo= &(padapter->wdinfo);

	DBG_871X( "[%s] ssid = %s, len = %d\n", __FUNCTION__, extra, strlen( extra ) );
	_rtw_memcpy( pwdinfo->nego_ssid, extra, strlen( extra ) );
	pwdinfo->nego_ssidlen = strlen( extra );
	
	return ret;
		
}


static int rtw_p2p_set_intent(struct net_device *dev,
                               struct iw_request_info *info,
                               union iwreq_data *wrqu, char *extra)
{
	int 							ret = 0;
	_adapter 						*padapter = (_adapter *)rtw_netdev_priv(dev);
	struct mlme_priv 				*pmlmepriv = &(padapter->mlmepriv);
	struct iw_point 				*pdata = &wrqu->data;
	struct wifidirect_info 			*pwdinfo= &(padapter->wdinfo);
	u8							intent = pwdinfo->intent;

	switch( wrqu->data.length )
	{
		case 1:
		{
			intent = extra[ 0 ] - '0';
			break;
		}
		case 2:
		{
			intent = str_2char2num( extra[ 0 ], extra[ 1 ]);
			break;
		}
	}

	if ( intent <= 15 )
	{
		pwdinfo->intent= intent;
	}
	else
	{
		ret = -1;
	}
	
	DBG_871X( "[%s] intent = %d\n", __FUNCTION__, intent);

	return ret;
		
}

static int rtw_p2p_set_listen_ch(struct net_device *dev,
                               struct iw_request_info *info,
                               union iwreq_data *wrqu, char *extra)
{
	
	int ret = 0;	
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);	
	struct iw_point *pdata = &wrqu->data;
	struct wifidirect_info *pwdinfo= &(padapter->wdinfo);
	u8	listen_ch = pwdinfo->listen_channel;	//	Listen channel number

	switch( wrqu->data.length )
	{
		case 1:
		{
			listen_ch = extra[ 0 ] - '0';
			break;
		}
		case 2:
		{
			listen_ch = str_2char2num( extra[ 0 ], extra[ 1 ]);
			break;
		}
	}

	if ( listen_ch > 0 && listen_ch <= 13 )
	{
		pwdinfo->listen_channel = listen_ch;
		set_channel_bwmode(padapter, pwdinfo->listen_channel, HAL_PRIME_CHNL_OFFSET_DONT_CARE, HT_CHANNEL_WIDTH_20);
	}
	else
	{
		ret = -1;
	}
	
	DBG_871X( "[%s] listen_ch = %d\n", __FUNCTION__, pwdinfo->listen_channel );
	
	return ret;
		
}

static int rtw_p2p_set_op_ch(struct net_device *dev,
                               struct iw_request_info *info,
                               union iwreq_data *wrqu, char *extra)
{
//	Commented by Albert 20110524
//	This function is used to set the operating channel if the driver will become the group owner

	int ret = 0;	
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);	
	struct iw_point *pdata = &wrqu->data;
	struct wifidirect_info *pwdinfo= &(padapter->wdinfo);
	u8	op_ch = pwdinfo->operating_channel;	//	Operating channel number

	switch( wrqu->data.length )
	{
		case 1:
		{
			op_ch = extra[ 0 ] - '0';
			break;
		}
		case 2:
		{
			op_ch = str_2char2num( extra[ 0 ], extra[ 1 ]);
			break;
		}
	}

	if ( op_ch > 0 && op_ch <= 13 )
	{
		pwdinfo->operating_channel = op_ch;
	}
	else
	{
		ret = -1;
	}
	
	DBG_871X( "[%s] op_ch = %d\n", __FUNCTION__, pwdinfo->operating_channel );
	
	return ret;
		
}


static int rtw_p2p_profilefound(struct net_device *dev,
                               struct iw_request_info *info,
                               union iwreq_data *wrqu, char *extra)
{
	
	int ret = 0;	
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);	
	struct iw_point *pdata = &wrqu->data;
	struct wifidirect_info *pwdinfo= &(padapter->wdinfo);

	//	Comment by Albert 2010/10/13
	//	Input data format:
	//	Ex:  0
	//	Ex:  1XX:XX:XX:XX:XX:XXYYSSID
	//	0 => Reflush the profile record list.
	//	1 => Add the profile list
	//	XX:XX:XX:XX:XX:XX => peer's MAC Address ( ex: 00:E0:4C:00:00:01 )
	//	YY => SSID Length
	//	SSID => SSID for persistence group

	DBG_871X( "[%s] In value = %s, len = %d \n", __FUNCTION__, extra, wrqu->data.length -1);

	
	//	The upper application should pass the SSID to driver by using this rtw_p2p_profilefound function.
	if(!rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE))
	{
		if ( extra[ 0 ] == '0' )
		{
			//	Remove all the profile information of wifidirect_info structure.
			_rtw_memset( &pwdinfo->profileinfo[ 0 ], 0x00, sizeof( struct profile_info ) * P2P_MAX_PERSISTENT_GROUP_NUM );
			pwdinfo->profileindex = 0;
		}
		else
		{
			if ( pwdinfo->profileindex >= P2P_MAX_PERSISTENT_GROUP_NUM )
		{
				ret = -1;
		}
		else
		{
				int jj, kk;
				
				//	Add this profile information into pwdinfo->profileinfo
				//	Ex:  1XX:XX:XX:XX:XX:XXYYSSID
				for( jj = 0, kk = 1; jj < ETH_ALEN; jj++, kk += 3 )
				{
					pwdinfo->profileinfo[ pwdinfo->profileindex ].peermac[ jj ] = key_2char2num(extra[ kk ], extra[ kk+ 1 ]);
				}

				pwdinfo->profileinfo[ pwdinfo->profileindex ].ssidlen = ( extra[18] - '0' ) * 10 + ( extra[ 19 ] - '0' );
				_rtw_memcpy( pwdinfo->profileinfo[ pwdinfo->profileindex ].ssid, &extra[ 20 ], pwdinfo->profileinfo[ pwdinfo->profileindex ].ssidlen );
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
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);	
	struct iw_point *pdata = &wrqu->data;
	struct wifidirect_info *pwdinfo= &(padapter->wdinfo);


	DBG_871X( "[%s] %s %d\n", __FUNCTION__, extra, wrqu->data.length -1  );
	pwdinfo->device_name_len = wrqu->data.length - 1;
	_rtw_memset( pwdinfo->device_name, 0x00, WPS_MAX_DEVICE_NAME_LEN );
	_rtw_memcpy( pwdinfo->device_name, extra, pwdinfo->device_name_len );
	
	return ret;
		
}


static int rtw_p2p_get_status(struct net_device *dev,
                               struct iw_request_info *info,
                               union iwreq_data *wrqu, char *extra)
{
	
	int ret = 0;	
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);	
	struct iw_point *pdata = &wrqu->data;
	struct wifidirect_info	*pwdinfo = &( padapter->wdinfo );

	
	DBG_871X( "[%s] Role = %d, Status = %d, peer addr = %.2X:%.2X:%.2X:%.2X:%.2X:%.2X\n", __FUNCTION__, rtw_p2p_role(pwdinfo), rtw_p2p_state(pwdinfo),
			pwdinfo->p2p_peer_interface_addr[ 0 ], pwdinfo->p2p_peer_interface_addr[ 1 ], pwdinfo->p2p_peer_interface_addr[ 2 ],
			pwdinfo->p2p_peer_interface_addr[ 3 ], pwdinfo->p2p_peer_interface_addr[ 4 ], pwdinfo->p2p_peer_interface_addr[ 5 ]);

	//	Commented by Albert 2010/10/12
	//	Because of the output size limitation, I had removed the "Role" information.
	//	About the "Role" information, we will use the new private IOCTL to get the "Role" information.
	sprintf( extra, "\n\nStatus=%.2d\n", rtw_p2p_state(pwdinfo) );
	wrqu->data.length = strlen( extra );

	if(rtw_p2p_chk_state(pwdinfo, P2P_STATE_LISTEN))
	{
		//	Stay at the listen state and wait for discovery.			
		set_channel_bwmode(padapter, pwdinfo->listen_channel, HAL_PRIME_CHNL_OFFSET_DONT_CARE, HT_CHANNEL_WIDTH_20);
	}
	
	return ret;
		
}

//	Commented by Albert 20110520
//	This function will return the config method description 
//	This config method description will show us which config method the remote P2P device is intented to use
//	by sending the provisioning discovery request frame.

static int rtw_p2p_get_req_cm(struct net_device *dev,
                               struct iw_request_info *info,
                               union iwreq_data *wrqu, char *extra)
{
	
	int ret = 0;	
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct iw_point *pdata = &wrqu->data;
	struct wifidirect_info	*pwdinfo = &( padapter->wdinfo );

	sprintf( extra, "\n\nCM=%s\n", pwdinfo->rx_prov_disc_info.strconfig_method_desc_of_prov_disc_req );
	wrqu->data.length = strlen( extra );
	return ret;
		
}


static int rtw_p2p_get_role(struct net_device *dev,
                               struct iw_request_info *info,
                               union iwreq_data *wrqu, char *extra)
{
	
	int ret = 0;	
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);	
	struct iw_point *pdata = &wrqu->data;
	struct wifidirect_info	*pwdinfo = &( padapter->wdinfo );

	
	DBG_871X( "[%s] Role = %d, Status = %d, peer addr = %.2X:%.2X:%.2X:%.2X:%.2X:%.2X\n", __FUNCTION__, rtw_p2p_role(pwdinfo), rtw_p2p_state(pwdinfo),
			pwdinfo->p2p_peer_interface_addr[ 0 ], pwdinfo->p2p_peer_interface_addr[ 1 ], pwdinfo->p2p_peer_interface_addr[ 2 ],
			pwdinfo->p2p_peer_interface_addr[ 3 ], pwdinfo->p2p_peer_interface_addr[ 4 ], pwdinfo->p2p_peer_interface_addr[ 5 ]);

	sprintf( extra, "\n\nRole=%.2d\n", rtw_p2p_role(pwdinfo) );
	wrqu->data.length = strlen( extra );
	return ret;
		
}


static int rtw_p2p_get_peer_ifaddr(struct net_device *dev,
                               struct iw_request_info *info,
                               union iwreq_data *wrqu, char *extra)
{
	
	int ret = 0;	
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);	
	struct iw_point *pdata = &wrqu->data;
	struct wifidirect_info	*pwdinfo = &( padapter->wdinfo );


	DBG_871X( "[%s] Role = %d, Status = %d, peer addr = %.2X:%.2X:%.2X:%.2X:%.2X:%.2X\n", __FUNCTION__, rtw_p2p_role(pwdinfo), rtw_p2p_state(pwdinfo),
			pwdinfo->p2p_peer_interface_addr[ 0 ], pwdinfo->p2p_peer_interface_addr[ 1 ], pwdinfo->p2p_peer_interface_addr[ 2 ],
			pwdinfo->p2p_peer_interface_addr[ 3 ], pwdinfo->p2p_peer_interface_addr[ 4 ], pwdinfo->p2p_peer_interface_addr[ 5 ]);

	sprintf( extra, "\nMAC %.2X:%.2X:%.2X:%.2X:%.2X:%.2X",
			pwdinfo->p2p_peer_interface_addr[ 0 ], pwdinfo->p2p_peer_interface_addr[ 1 ], pwdinfo->p2p_peer_interface_addr[ 2 ],
			pwdinfo->p2p_peer_interface_addr[ 3 ], pwdinfo->p2p_peer_interface_addr[ 4 ], pwdinfo->p2p_peer_interface_addr[ 5 ]);
	wrqu->data.length = strlen( extra );
	return ret;
		
}

static int rtw_p2p_get_peer_devaddr(struct net_device *dev,
                               struct iw_request_info *info,
                               union iwreq_data *wrqu, char *extra)

{
	
	int ret = 0;	
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);	
	struct iw_point *pdata = &wrqu->data;
	struct wifidirect_info	*pwdinfo = &( padapter->wdinfo );

	DBG_871X( "[%s] Role = %d, Status = %d, peer addr = %.2X:%.2X:%.2X:%.2X:%.2X:%.2X\n", __FUNCTION__, rtw_p2p_role(pwdinfo), rtw_p2p_state(pwdinfo),
			pwdinfo->rx_prov_disc_info.peerDevAddr[ 0 ], pwdinfo->rx_prov_disc_info.peerDevAddr[ 1 ], 
			pwdinfo->rx_prov_disc_info.peerDevAddr[ 2 ], pwdinfo->rx_prov_disc_info.peerDevAddr[ 3 ],
			pwdinfo->rx_prov_disc_info.peerDevAddr[ 4 ], pwdinfo->rx_prov_disc_info.peerDevAddr[ 5 ]);
	sprintf( extra, "\n%.2X%.2X%.2X%.2X%.2X%.2X",
			pwdinfo->rx_prov_disc_info.peerDevAddr[ 0 ], pwdinfo->rx_prov_disc_info.peerDevAddr[ 1 ], 
			pwdinfo->rx_prov_disc_info.peerDevAddr[ 2 ], pwdinfo->rx_prov_disc_info.peerDevAddr[ 3 ],
			pwdinfo->rx_prov_disc_info.peerDevAddr[ 4 ], pwdinfo->rx_prov_disc_info.peerDevAddr[ 5 ]);
	wrqu->data.length = strlen( extra );	
	return ret;
		
}

static int rtw_p2p_get_groupid(struct net_device *dev,
                               struct iw_request_info *info,
                               union iwreq_data *wrqu, char *extra)

{
	
	int ret = 0;	
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);	
	struct iw_point *pdata = &wrqu->data;
	struct wifidirect_info	*pwdinfo = &( padapter->wdinfo );

	sprintf( extra, "\n%.2X:%.2X:%.2X:%.2X:%.2X:%.2X-%s",
			pwdinfo->groupid_info.go_device_addr[ 0 ], pwdinfo->groupid_info.go_device_addr[ 1 ], 
			pwdinfo->groupid_info.go_device_addr[ 2 ], pwdinfo->groupid_info.go_device_addr[ 3 ],
			pwdinfo->groupid_info.go_device_addr[ 4 ], pwdinfo->groupid_info.go_device_addr[ 5 ],
			pwdinfo->groupid_info.ssid);
	wrqu->data.length = strlen( extra );	
	return ret;
		
}

static int rtw_p2p_get_wps_configmethod(struct net_device *dev,
                               struct iw_request_info *info,
                               union iwreq_data *wrqu, char *extra)
{
	
	int ret = 0;	
	_adapter 				*padapter = (_adapter *)rtw_netdev_priv(dev);	
	struct iw_point 		*pdata = &wrqu->data;
	struct wifidirect_info	*pwdinfo = &( padapter->wdinfo );
	u8					peerMAC[ ETH_ALEN ] = { 0x00 };
	int 					jj,kk;
	u8   					peerMACStr[ 17 ] = { 0x00 };
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	_irqL				irqL;
	_list					*plist, *phead;
	_queue				*queue	= &(pmlmepriv->scanned_queue);
	struct	wlan_network	*pnetwork = NULL;
	u8					blnMatch = 0;


	//	Commented by Albert 20110727
	//	The input data is the MAC address which the application wants to know its WPS config method.
	//	After knowing its WPS config method, the application can decide the config method for provisioning discovery.
	//	Format: iwpriv wlanx p2p_get_wpsCM 00:E0:4C:00:00:05

	DBG_871X( "[%s] data = %s\n", __FUNCTION__, ( char* ) extra );
	_rtw_memcpy( peerMACStr , extra , 17 );


	for( jj = 0, kk = 0; jj < ETH_ALEN; jj++, kk += 3 )
	{
		peerMAC[ jj ] = key_2char2num( peerMACStr[kk], peerMACStr[kk+ 1] );
	}

	_enter_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);

	phead = get_list_head(queue);
	plist = get_next(phead);
       
	while(1)
	{
		if (rtw_end_of_queue_search(phead,plist)== _TRUE)
			break;

		pnetwork = LIST_CONTAINOR(plist, struct wlan_network, list);
		if ( _rtw_memcmp( pnetwork->network.MacAddress, peerMAC, ETH_ALEN ) )
		{
			u8 *wpsie;
			uint	wpsie_len = 0;
			u16	attr_content = 0;
			uint	attr_contentlen = 0;
			
              	//	The mac address is matched.

			if ( (wpsie=rtw_get_wps_ie( &pnetwork->network.IEs[ 12 ], pnetwork->network.IELength - 12, NULL, &wpsie_len )) )
			{
				rtw_get_wps_attr_content( wpsie, wpsie_len, WPS_ATTR_CONF_METHOD, ( u8* ) &attr_content, &attr_contentlen);
				if ( attr_contentlen )
				{
					attr_content = be16_to_cpu( attr_content );
					sprintf( extra, "\n\nM=%.4d", attr_content );
					_rtw_memcpy( wrqu->data.pointer, extra, 4 + 4 );
					*( (u8*) wrqu->data.pointer + 4 + 4 ) = 0x00;
					blnMatch = 1;
				}
			}
			
			break;
              }

		plist = get_next(plist);
	
	}        

	_exit_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);

	if ( !blnMatch )
	{
		sprintf( extra, "\n\nM=0000" );	
		_rtw_memcpy( wrqu->data.pointer, extra, 4 + 4 );
		*( (u8*) wrqu->data.pointer + 4 + 4 ) = 0x00;
	}
	
	return ret;
		
}

#ifdef CONFIG_WFD
static int rtw_p2p_get_peer_WFD_port(struct net_device *dev,
                               struct iw_request_info *info,
                               union iwreq_data *wrqu, char *extra)
{
	
	int ret = 0;	
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);	
	struct iw_point *pdata = &wrqu->data;
	struct wifidirect_info	*pwdinfo = &( padapter->wdinfo );

	DBG_871X( "[%s] p2p_state = %d\n", __FUNCTION__, rtw_p2p_state(pwdinfo) );

	sprintf( extra, "\n\nPort=%d\n", pwdinfo->wfd_info.peer_rtsp_ctrlport );
	DBG_871X( "[%s] remote port = %d\n", __FUNCTION__, pwdinfo->wfd_info.peer_rtsp_ctrlport );
	
	wrqu->data.length = strlen( extra );
	return ret;
		
}
#endif // CONFIG_WFD

static int rtw_p2p_get_device_name(struct net_device *dev,
                               struct iw_request_info *info,
                               union iwreq_data *wrqu, char *extra)
{
	
	int ret = 0;	
	_adapter 				*padapter = (_adapter *)rtw_netdev_priv(dev);	
	struct iw_point 		*pdata = &wrqu->data;
	struct wifidirect_info	*pwdinfo = &( padapter->wdinfo );
	u8					peerMAC[ ETH_ALEN ] = { 0x00 };
	int 					jj,kk;
	u8   					peerMACStr[ 17 ] = { 0x00 };
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	_irqL				irqL;
	_list					*plist, *phead;
	_queue				*queue	= &(pmlmepriv->scanned_queue);
	struct	wlan_network	*pnetwork = NULL;
	u8					blnMatch = 0;


	//	Commented by Kurt 20110727
	//	The input data is the MAC address which the application wants to know its device name.
	//	Such user interface could show peer device's device name instead of ssid.
	//	Format: iwpriv wlanx p2p_get_wpsCM 00:E0:4C:00:00:05

	DBG_871X( "[%s] data = %s\n", __FUNCTION__, ( char* ) extra );
	_rtw_memcpy( peerMACStr , extra , 17 );


	for( jj = 0, kk = 0; jj < ETH_ALEN; jj++, kk += 3 )
	{
		peerMAC[ jj ] = key_2char2num( peerMACStr[kk], peerMACStr[kk+ 1] );
	}

	_enter_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);

	phead = get_list_head(queue);
	plist = get_next(phead);
       
	while(1)
	{
		if (rtw_end_of_queue_search(phead,plist)== _TRUE)
			break;

		pnetwork = LIST_CONTAINOR(plist, struct wlan_network, list);
		if ( _rtw_memcmp( pnetwork->network.MacAddress, peerMAC, ETH_ALEN ) )
		{
			u8 *wpsie;
			uint	wpsie_len = 0;
			u8	dev_name[ WPS_MAX_DEVICE_NAME_LEN ] = { 0x00 };
			uint	dev_len = 0;	

			
              	//	The mac address is matched.

			if ( (wpsie=rtw_get_wps_ie( &pnetwork->network.IEs[ 12 ], pnetwork->network.IELength - 12, NULL, &wpsie_len )) )
			{
				rtw_get_wps_attr_content( wpsie, wpsie_len, WPS_ATTR_DEVICE_NAME, dev_name, &dev_len);
				if ( dev_len )
				{
					sprintf( extra, "\n\nN=%s", dev_name );
					_rtw_memcpy( wrqu->data.pointer, extra, dev_len + 4 );
					*( (u8*) wrqu->data.pointer + dev_len + 4 ) = 0x00;
					blnMatch = 1;
				}
			}			
			break;
              }

		plist = get_next(plist);
	
	}        

	_exit_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);

	if ( !blnMatch )
	{
		sprintf( extra, "\n\nN=0000" );	
		_rtw_memcpy( wrqu->data.pointer, extra, 4 + 4 );
		*( (u8*) wrqu->data.pointer + 4 + 4 ) = 0x00;
	}
	
	return ret;
		
}

static int rtw_p2p_connect(struct net_device *dev,
                               struct iw_request_info *info,
                               union iwreq_data *wrqu, char *extra)
{
	
	int ret = 0;	
	_adapter 				*padapter = (_adapter *)rtw_netdev_priv(dev);	
	struct iw_point 		*pdata = &wrqu->data;
	struct wifidirect_info	*pwdinfo = &( padapter->wdinfo );
	u8					peerMAC[ ETH_ALEN ] = { 0x00 };
	int 					jj,kk;
	u8   					peerMACStr[ ETH_ALEN * 2 ] = { 0x00 };
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	_irqL				irqL;
	_list					*plist, *phead;
	_queue				*queue	= &(pmlmepriv->scanned_queue);
	struct	wlan_network	*pnetwork = NULL;
	uint					uintPeerChannel = 0;
	
	//	Commented by Albert 20110304
	//	The input data contains two informations.
	//	1. First information is the MAC address which wants to formate with
	//	2. Second information is the WPS PINCode or "pbc" string for push button method
	//	Format: 00:E0:4C:00:00:05
	//	Format: 00:E0:4C:00:00:05

	DBG_871X( "[%s] data = %s\n", __FUNCTION__, extra );

	if(rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE) || rtw_p2p_chk_state(pwdinfo, P2P_STATE_IDLE))
	{
		DBG_871X( "[%s] WiFi Direct is disable!\n", __FUNCTION__ );
		return ret;
	}
	
	if ( pwdinfo->ui_got_wps_info == P2P_NO_WPSINFO )
	{
		return -1;
	}
	
	for( jj = 0, kk = 0; jj < ETH_ALEN; jj++, kk += 3 )
	{
		peerMAC[ jj ] = key_2char2num( extra[kk], extra[kk+ 1] );
	}

	_enter_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);

	phead = get_list_head(queue);
	plist = get_next(phead);
       
	while(1)
	{
		if (rtw_end_of_queue_search(phead,plist)== _TRUE)
			break;

		pnetwork = LIST_CONTAINOR(plist, struct wlan_network, list);
		if ( _rtw_memcmp( pnetwork->network.MacAddress, peerMAC, ETH_ALEN ) )
		{
			uintPeerChannel = pnetwork->network.Configuration.DSConfig;
			break;
		}

		plist = get_next(plist);
	
	}

	_exit_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);

	if ( uintPeerChannel )
	{
		_rtw_memset( &pwdinfo->nego_req_info, 0x00, sizeof( struct tx_nego_req_info ) );
		_rtw_memset( &pwdinfo->groupid_info, 0x00, sizeof( struct group_id_info ) );
		
		pwdinfo->nego_req_info.peer_channel_num[ 0 ] = uintPeerChannel;
		_rtw_memcpy( pwdinfo->nego_req_info.peerDevAddr, pnetwork->network.MacAddress, ETH_ALEN );
		pwdinfo->nego_req_info.benable = _TRUE;
		
		rtw_p2p_set_pre_state(pwdinfo, rtw_p2p_state(pwdinfo));
		rtw_p2p_set_state(pwdinfo, P2P_STATE_GONEGO_ING);
		
		DBG_871X( "[%s] Start PreTx Procedure!\n", __FUNCTION__ );
		_set_timer( &pwdinfo->pre_tx_scan_timer, P2P_TX_PRESCAN_TIMEOUT );
		_set_timer( &pwdinfo->restore_p2p_state_timer, P2P_GO_NEGO_TIMEOUT );
	}
	else
	{
		DBG_871X( "[%s] Not Found in Scanning Queue~\n", __FUNCTION__ );
		ret = -1;
	}
exit:	
	return ret;
}

static int rtw_p2p_prov_disc(struct net_device *dev,
                               struct iw_request_info *info,
                               union iwreq_data *wrqu, char *extra)
{
	
	int ret = 0;	
	_adapter 				*padapter = (_adapter *)rtw_netdev_priv(dev);	
	struct iw_point 		*pdata = &wrqu->data;
	struct wifidirect_info	*pwdinfo = &( padapter->wdinfo );
	u8					peerMAC[ ETH_ALEN ] = { 0x00 };
	int 					jj,kk;
	u8   					peerMACStr[ ETH_ALEN * 2 ] = { 0x00 };
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	_list					*plist, *phead;
	_queue				*queue	= &(pmlmepriv->scanned_queue);
	struct	wlan_network	*pnetwork = NULL;
	uint					uintPeerChannel = 0;
	u8					attr_content[50] = { 0x00 }, _status = 0;
	u8 *p2pie;
	uint					p2pielen = 0, attr_contentlen = 0;
	_irqL				irqL;
	
	//	Commented by Albert 20110301
	//	The input data contains two informations.
	//	1. First information is the MAC address which wants to issue the provisioning discovery request frame.
	//	2. Second information is the WPS configuration method which wants to discovery
	//	Format: 00:E0:4C:00:00:05_display
	//	Format: 00:E0:4C:00:00:05_keypad
	//	Format: 00:E0:4C:00:00:05_pbc
	//	Format: 00:E0:4C:00:00:05_label

	DBG_871X( "[%s] data = %s\n", __FUNCTION__, extra );

	if(rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE) || rtw_p2p_chk_state(pwdinfo, P2P_STATE_IDLE))
	{
		DBG_871X( "[%s] WiFi Direct is disable!\n", __FUNCTION__ );
		return ret;
	}
	else
	{
		//	Reset the content of struct tx_provdisc_req_info excluded the wps_config_method_request.
		_rtw_memset( pwdinfo->tx_prov_disc_info.peerDevAddr, 0x00, ETH_ALEN );
		_rtw_memset( pwdinfo->tx_prov_disc_info.peerIFAddr, 0x00, ETH_ALEN );
		_rtw_memset( &pwdinfo->tx_prov_disc_info.ssid, 0x00, sizeof( NDIS_802_11_SSID ) );		
		pwdinfo->tx_prov_disc_info.peer_channel_num[ 0 ] = 0;
		pwdinfo->tx_prov_disc_info.peer_channel_num[ 1 ] = 0;
		pwdinfo->tx_prov_disc_info.benable = _FALSE;
	}
	
	for( jj = 0, kk = 0; jj < ETH_ALEN; jj++, kk += 3 )
	{
		peerMAC[ jj ] = key_2char2num( extra[kk], extra[kk+ 1] );
	}

	if ( _rtw_memcmp( &extra[ 18 ], "display", 7 ) )
	{
		pwdinfo->tx_prov_disc_info.wps_config_method_request = WPS_CM_DISPLYA;
	}
	else if ( _rtw_memcmp( &extra[ 18 ], "keypad", 7 ) )
	{
		pwdinfo->tx_prov_disc_info.wps_config_method_request = WPS_CM_KEYPAD;
	}
	else if ( _rtw_memcmp( &extra[ 18 ], "pbc", 3 ) )
	{
		pwdinfo->tx_prov_disc_info.wps_config_method_request = WPS_CM_PUSH_BUTTON;
	}
	else if ( _rtw_memcmp( &extra[ 18 ], "label", 5 ) )
	{
		pwdinfo->tx_prov_disc_info.wps_config_method_request = WPS_CM_LABEL;
	}
	else
	{
		DBG_871X( "[%s] Unknown WPS config methodn", __FUNCTION__ );
		return( ret );
	}

	_enter_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);

	phead = get_list_head(queue);
	plist = get_next(phead);
       
	while(1)
	{
		if (rtw_end_of_queue_search(phead,plist)== _TRUE)
			break;

		pnetwork = LIST_CONTAINOR(plist, struct wlan_network, list);

		//	Commented by Albert 2011/05/18
		//	Match the device address located in the P2P IE
		//	This is for the case that the P2P device address is not the same as the P2P interface address.

		if ( (p2pie=rtw_get_p2p_ie( &pnetwork->network.IEs[12], pnetwork->network.IELength - 12, NULL, &p2pielen)) )
		{
			//	The P2P Device ID attribute is included in the Beacon frame.
			//	The P2P Device Info attribute is included in the probe response frame.

			if ( rtw_get_p2p_attr_content( p2pie, p2pielen, P2P_ATTR_DEVICE_ID, attr_content, &attr_contentlen) )
			{
				//	Handle the P2P Device ID attribute of Beacon first
				if ( _rtw_memcmp( attr_content, peerMAC, ETH_ALEN ) )
				{
					uintPeerChannel = pnetwork->network.Configuration.DSConfig;
					break;
				}
			}
			else if ( rtw_get_p2p_attr_content( p2pie, p2pielen, P2P_ATTR_DEVICE_INFO, attr_content, &attr_contentlen) )
			{
				//	Handle the P2P Device Info attribute of probe response
				if ( _rtw_memcmp( attr_content, peerMAC, ETH_ALEN ) )
				{
					uintPeerChannel = pnetwork->network.Configuration.DSConfig;
					break;
				}					
			}

		}

		plist = get_next(plist);
	
	}

	_exit_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);

	if ( uintPeerChannel )
	{
		_rtw_memcpy( pwdinfo->tx_prov_disc_info.peerIFAddr, pnetwork->network.MacAddress, ETH_ALEN );
		_rtw_memcpy( pwdinfo->tx_prov_disc_info.peerDevAddr, peerMAC, ETH_ALEN );
		pwdinfo->tx_prov_disc_info.peer_channel_num[0] = ( u16 ) uintPeerChannel;
		pwdinfo->tx_prov_disc_info.benable = _TRUE;
		rtw_p2p_set_pre_state(pwdinfo, rtw_p2p_state(pwdinfo));
		rtw_p2p_set_state(pwdinfo, P2P_STATE_TX_PROVISION_DIS_REQ);

		if(rtw_p2p_chk_role(pwdinfo, P2P_ROLE_CLIENT))
		{
			_rtw_memcpy( &pwdinfo->tx_prov_disc_info.ssid, &pnetwork->network.Ssid, sizeof( NDIS_802_11_SSID ) );
		}
		else if(rtw_p2p_chk_role(pwdinfo, P2P_ROLE_DEVICE) || rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO))
		{
			_rtw_memcpy( pwdinfo->tx_prov_disc_info.ssid.Ssid, pwdinfo->p2p_wildcard_ssid, P2P_WILDCARD_SSID_LEN );
			pwdinfo->tx_prov_disc_info.ssid.SsidLength= P2P_WILDCARD_SSID_LEN;
		}

		set_channel_bwmode(padapter, uintPeerChannel, HAL_PRIME_CHNL_OFFSET_DONT_CARE, HT_CHANNEL_WIDTH_20);		
		_set_timer( &pwdinfo->pre_tx_scan_timer, P2P_TX_PRESCAN_TIMEOUT );
		_set_timer( &pwdinfo->restore_p2p_state_timer, P2P_PROVISION_TIMEOUT );
		
	}
	else
	{
		DBG_871X( "[%s] NOT Found in the Scanning Queue!\n", __FUNCTION__ );
	}
exit:
	
	return ret;
		
}

//	Added by Albert 20110328
//	This function is used to inform the driver the user had specified the pin code value or pbc
//	to application.

static int rtw_p2p_got_wpsinfo(struct net_device *dev,
                               struct iw_request_info *info,
                               union iwreq_data *wrqu, char *extra)
{
	
	int ret = 0;	
	_adapter 				*padapter = (_adapter *)rtw_netdev_priv(dev);	
	struct iw_point 		*pdata = &wrqu->data;
	struct wifidirect_info	*pwdinfo = &( padapter->wdinfo );
	

	DBG_871X( "[%s] data = %s\n", __FUNCTION__, extra );
	//	Added by Albert 20110328
	//	if the input data is P2P_NO_WPSINFO -> reset the wpsinfo
	//	if the input data is P2P_GOT_WPSINFO_PEER_DISPLAY_PIN -> the utility just input the PIN code got from the peer P2P device.
	//	if the input data is P2P_GOT_WPSINFO_SELF_DISPLAY_PIN -> the utility just got the PIN code from itself.
	//	if the input data is P2P_GOT_WPSINFO_PBC -> the utility just determine to use the PBC
	
	if ( *extra == '0' )
	{
		pwdinfo->ui_got_wps_info = P2P_NO_WPSINFO;
	}
	else if ( *extra == '1' )
	{
		pwdinfo->ui_got_wps_info = P2P_GOT_WPSINFO_PEER_DISPLAY_PIN;
	}
	else if ( *extra == '2' )
	{
		pwdinfo->ui_got_wps_info = P2P_GOT_WPSINFO_SELF_DISPLAY_PIN;
	}
	else if ( *extra == '3' )
	{
		pwdinfo->ui_got_wps_info = P2P_GOT_WPSINFO_PBC;
	}
	else
	{
		pwdinfo->ui_got_wps_info = P2P_NO_WPSINFO;
	}
	
	return ret;
		
}

#endif //CONFIG_P2P

static int rtw_p2p_set(struct net_device *dev,
                               struct iw_request_info *info,
                               union iwreq_data *wrqu, char *extra)
{
	
	int ret = 0;
#ifdef CONFIG_P2P

	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);	
	struct iw_point *pdata = &wrqu->data;
	struct wifidirect_info *pwdinfo= &(padapter->wdinfo);
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;

	DBG_871X( "[%s] extra = %s\n", __FUNCTION__, extra );

	if ( _rtw_memcmp( extra, "enable=", 7 ) )
	{
		rtw_wext_p2p_enable( dev, info, wrqu, &extra[7] );
	}
	else if ( _rtw_memcmp( extra, "setDN=", 6 ) )
	{
		wrqu->data.length -= 6;
		rtw_p2p_setDN( dev, info, wrqu, &extra[6] );
	}
	else if ( _rtw_memcmp( extra, "profilefound=", 13 ) )
	{
		wrqu->data.length -= 13;
		rtw_p2p_profilefound( dev, info, wrqu, &extra[13] );
	}
	else if ( _rtw_memcmp( extra, "prov_disc=", 10 ) )
	{
		wrqu->data.length -= 10;
		rtw_p2p_prov_disc( dev, info, wrqu, &extra[10] );
	}
	else if ( _rtw_memcmp( extra, "nego=", 5 ) )
	{
		wrqu->data.length -= 5;
		rtw_p2p_connect( dev, info, wrqu, &extra[5] );
	}
	else if ( _rtw_memcmp( extra, "intent=", 7 ) )
	{
		//	Commented by Albert 2011/03/23
		//	The wrqu->data.length will include the null character
		//	So, we will decrease 7 + 1
		wrqu->data.length -= 8;
		rtw_p2p_set_intent( dev, info, wrqu, &extra[7] );
	}
	else if ( _rtw_memcmp( extra, "ssid=", 5 ) )
	{
		wrqu->data.length -= 5;
		rtw_p2p_set_go_nego_ssid( dev, info, wrqu, &extra[5] );
	}
	else if ( _rtw_memcmp( extra, "got_wpsinfo=", 12 ) )
	{
		wrqu->data.length -= 12;
		rtw_p2p_got_wpsinfo( dev, info, wrqu, &extra[12] );
	}
	else if ( _rtw_memcmp( extra, "listen_ch=", 10 ) )
	{
		//	Commented by Albert 2011/05/24
		//	The wrqu->data.length will include the null character
		//	So, we will decrease (10 + 1)	
		wrqu->data.length -= 11;
		rtw_p2p_set_listen_ch( dev, info, wrqu, &extra[10] );
	}
	else if ( _rtw_memcmp( extra, "op_ch=", 6 ) )
	{
		//	Commented by Albert 2011/05/24
		//	The wrqu->data.length will include the null character
		//	So, we will decrease (6 + 1)	
		wrqu->data.length -= 7;
		rtw_p2p_set_op_ch( dev, info, wrqu, &extra[6] );
	}

	
#endif //CONFIG_P2P

	return ret;
		
}

static int rtw_p2p_get(struct net_device *dev,
                               struct iw_request_info *info,
                               union iwreq_data *wrqu, char *extra)
{
	
	int ret = 0;	
	
#ifdef CONFIG_P2P

	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);	
	struct iw_point *pdata = &wrqu->data;
	struct wifidirect_info *pwdinfo= &(padapter->wdinfo);
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;

	DBG_871X( "[%s] extra = %s\n", __FUNCTION__, (char*) wrqu->data.pointer );
	
	if ( _rtw_memcmp( wrqu->data.pointer, "status", 6 ) )
	{
		rtw_p2p_get_status( dev, info, wrqu, extra );
	}
	else if ( _rtw_memcmp( wrqu->data.pointer, "role", 4 ) )
	{
		rtw_p2p_get_role( dev, info, wrqu, extra);
	}
	else if ( _rtw_memcmp( wrqu->data.pointer, "peer_ifa", 8 ) )
	{
		rtw_p2p_get_peer_ifaddr( dev, info, wrqu, extra);
	}
	else if ( _rtw_memcmp( wrqu->data.pointer, "req_cm", 6 ) )
	{
		rtw_p2p_get_req_cm( dev, info, wrqu, extra);
	}
	else if ( _rtw_memcmp( wrqu->data.pointer, "peer_deva", 9 ) )
	{
		rtw_p2p_get_peer_devaddr( dev, info, wrqu, extra);
	}
#ifdef CONFIG_WFD
	else if ( _rtw_memcmp( wrqu->data.pointer, "peer_port", 9 ) )
	{
		rtw_p2p_get_peer_WFD_port( dev, info, wrqu, extra );
	}
#endif // CONFIG_WFD	
	
#endif //CONFIG_P2P

	return ret;
		
}

static int rtw_p2p_get2(struct net_device *dev,
                               struct iw_request_info *info,
                               union iwreq_data *wrqu, char *extra)
{
	
	int ret = 0;	
	
#ifdef CONFIG_P2P

	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);	
	struct iw_point *pdata = &wrqu->data;
	struct wifidirect_info *pwdinfo= &(padapter->wdinfo);
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;

	DBG_871X( "[%s] extra = %s\n", __FUNCTION__, (char*) wrqu->data.pointer );
	
	if ( _rtw_memcmp( extra, "wpsCM=", 6 ) )
	{
		wrqu->data.length -= 6;
		rtw_p2p_get_wps_configmethod( dev, info, wrqu,  &extra[6]);
	}
	else if ( _rtw_memcmp( extra, "devN=", 5 ) )
	{
		wrqu->data.length -= 5;
		rtw_p2p_get_device_name( dev, info, wrqu, &extra[5] );
	}
	
#endif //CONFIG_P2P

	return ret;
		
}

extern char *ifname;
extern int rtw_change_ifname(_adapter *padapter, const char *ifname);
static int rtw_rereg_nd_name(struct net_device *dev,
                               struct iw_request_info *info,
                               union iwreq_data *wrqu, char *extra)
{
	int ret = 0;	
	_adapter *padapter = rtw_netdev_priv(dev);
	struct rereg_nd_name_data *rereg_priv = &padapter->rereg_nd_name_priv;
	char new_ifname[IFNAMSIZ];

	if(rereg_priv->old_ifname[0] == 0) {
		strncpy(rereg_priv->old_ifname, ifname, IFNAMSIZ);
		rereg_priv->old_ifname[IFNAMSIZ-1] = 0;
	}

	//DBG_871X("%s wrqu->data.length:%d\n", __FUNCTION__, wrqu->data.length);
	if(wrqu->data.length > IFNAMSIZ)
		return -EFAULT;

	if ( copy_from_user(new_ifname, wrqu->data.pointer, IFNAMSIZ) ) {
		return -EFAULT;
	}

	if( 0 == strcmp(rereg_priv->old_ifname, new_ifname) ) {
		return ret;
	}

	DBG_871X("%s new_ifname:%s\n", __FUNCTION__, new_ifname);
	if( 0 != (ret = rtw_change_ifname(padapter, new_ifname)) ) {
		goto exit;
	}

	if(_rtw_memcmp(rereg_priv->old_ifname, "disable%d", 9) == _TRUE) {
		padapter->ledpriv.bRegUseLed= rereg_priv->old_bRegUseLed;
		rtw_hal_sw_led_init(padapter);
		rtw_ips_mode_req(&padapter->pwrctrlpriv, rereg_priv->old_ips_mode);
	}

	strncpy(rereg_priv->old_ifname, new_ifname, IFNAMSIZ);
	rereg_priv->old_ifname[IFNAMSIZ-1] = 0;
	
	if(_rtw_memcmp(new_ifname, "disable%d", 9) == _TRUE) {

		DBG_871X("%s disable\n", __FUNCTION__);
		// free network queue for Android's timming issue
		rtw_free_network_queue(padapter, _TRUE);
		
		// close led
		rtw_led_control(padapter, LED_CTL_POWER_OFF);
		rereg_priv->old_bRegUseLed = padapter->ledpriv.bRegUseLed;
		padapter->ledpriv.bRegUseLed= _FALSE;
		rtw_hal_sw_led_deinit(padapter);
		
		// the interface is being "disabled", we can do deeper IPS
		rereg_priv->old_ips_mode = rtw_get_ips_mode_req(&padapter->pwrctrlpriv);
		rtw_ips_mode_req(&padapter->pwrctrlpriv, IPS_NORMAL);
	}
exit:
	return ret;

}

#if 0
void mac_reg_dump(_adapter *padapter)
{
	int i,j=1;		
	DBG_871X("\n======= MAC REG =======\n");
	for(i=0x0;i<0x300;i+=4)
	{	
		if(j%4==1)	DBG_871X("0x%02x",i);
		DBG_871X(" 0x%08x ",rtw_read32(padapter,i));		
		if((j++)%4 == 0)	DBG_871X("\n");	
	}
	for(i=0x400;i<0x800;i+=4)
	{	
		if(j%4==1)	DBG_871X("0x%02x",i);
		DBG_871X(" 0x%08x ",rtw_read32(padapter,i));		
		if((j++)%4 == 0)	DBG_871X("\n");	
	}									
}
void bb_reg_dump(_adapter *padapter)
{
	int i,j=1;		
	DBG_871X("\n======= BB REG =======\n");
	for(i=0x800;i<0x1000;i+=4)
	{
		if(j%4==1) DBG_871X("0x%02x",i);
				
		DBG_871X(" 0x%08x ",rtw_read32(padapter,i));		
		if((j++)%4 == 0)	DBG_871X("\n");	
	}		
}
void rf_reg_dump(_adapter *padapter)
{	
	int i,j=1,path;
	u32 value;			
	DBG_871X("\n======= RF REG =======\n");
	for(path=0;path<2;path++)
	{
		DBG_871X("\nRF_Path(%x)\n",path);
		for(i=0;i<0x100;i++)
		{								
			value = PHY_QueryRFReg(padapter, (RF_RADIO_PATH_E)path,i, bMaskDWord);
			if(j%4==1)	DBG_871X("0x%02x ",i);
			DBG_871X(" 0x%08x ",value);
			if((j++)%4==0)	DBG_871X("\n");	
		}	
	}
}

#endif

void mac_reg_dump(_adapter *padapter)
{
	int i,j=1;		
	DBG_871X("\n======= MAC REG =======\n");
	for(i=0x0;i<0x300;i+=4)
	{	
		if(j%4==1)	DBG_871X("0x%02x",i);
		DBG_871X(" 0x%08x ",rtw_read32(padapter,i));		
		if((j++)%4 == 0)	DBG_871X("\n");	
	}
	for(i=0x400;i<0x800;i+=4)
	{	
		if(j%4==1)	DBG_871X("0x%02x",i);
		DBG_871X(" 0x%08x ",rtw_read32(padapter,i));		
		if((j++)%4 == 0)	DBG_871X("\n");	
	}									
}
void bb_reg_dump(_adapter *padapter)
{
	int i,j=1;		
	DBG_871X("\n======= BB REG =======\n");
	for(i=0x800;i<0x1000;i+=4)
	{
		if(j%4==1) DBG_871X("0x%02x",i);
				
		DBG_871X(" 0x%08x ",rtw_read32(padapter,i));		
		if((j++)%4 == 0)	DBG_871X("\n");	
	}		
}
void rf_reg_dump(_adapter *padapter)
{	
	int i,j=1,path;
	u32 value;	
	u8 rf_type,path_nums = 0;
	rtw_hal_get_hwreg(padapter, HW_VAR_RF_TYPE, (u8 *)(&rf_type));
		
	DBG_871X("\n======= RF REG =======\n");
	if((RF_1T2R == rf_type) ||(RF_1T1R ==rf_type ))	
		path_nums = 1;
	else	
		path_nums = 2;
		
	for(path=0;path<path_nums;path++)
	{
		DBG_871X("\nRF_Path(%x)\n",path);
		for(i=0;i<0x100;i++)
		{								
			//value = PHY_QueryRFReg(padapter, (RF_RADIO_PATH_E)path,i, bMaskDWord);
			value = rtw_hal_read_rfreg(padapter, path, i, 0xffffffff);
			if(j%4==1)	DBG_871X("0x%02x ",i);
			DBG_871X(" 0x%08x ",value);
			if((j++)%4==0)	DBG_871X("\n");	
		}	
	}
}

#ifdef CONFIG_IOL
#include <rtw_iol.h>
#endif
static int rtw_dbg_port(struct net_device *dev,
                               struct iw_request_info *info,
                               union iwreq_data *wrqu, char *extra)
{	
	_irqL irqL;
	int ret = 0;
	u8 major_cmd, minor_cmd;
	u16 arg;
	u32 extra_arg, *pdata, val32;
	struct sta_info *psta;						
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	struct wlan_network *cur_network = &(pmlmepriv->cur_network);
	struct sta_priv *pstapriv = &padapter->stapriv;
	

	pdata = (u32*)&wrqu->data;	

	val32 = *pdata;
	arg = (u16)(val32&0x0000ffff);
	major_cmd = (u8)(val32>>24);
	minor_cmd = (u8)((val32>>16)&0x00ff);

	extra_arg = *(pdata+1);
	
	switch(major_cmd)
	{
		case 0x70://read_reg
			switch(minor_cmd)
			{
				case 1:
					DBG_871X("rtw_read8(0x%x)=0x%02x\n", arg, rtw_read8(padapter, arg));
					break;
				case 2:
					DBG_871X("rtw_read16(0x%x)=0x%04x\n", arg, rtw_read16(padapter, arg));
					break;
				case 4:
					DBG_871X("rtw_read32(0x%x)=0x%08x\n", arg, rtw_read32(padapter, arg));
					break;
			}			
			break;
		case 0x71://write_reg
			switch(minor_cmd)
			{
				case 1:
					rtw_write8(padapter, arg, extra_arg);
					DBG_871X("rtw_write8(0x%x)=0x%02x\n", arg, rtw_read8(padapter, arg));
					break;
				case 2:
					rtw_write16(padapter, arg, extra_arg);
					DBG_871X("rtw_write16(0x%x)=0x%04x\n", arg, rtw_read16(padapter, arg));
					break;
				case 4:
					rtw_write32(padapter, arg, extra_arg);
					DBG_871X("rtw_write32(0x%x)=0x%08x\n", arg, rtw_read32(padapter, arg));
					break;
			}			
			break;
		case 0x72://read_bb
			DBG_871X("read_bbreg(0x%x)=0x%x\n", arg, rtw_hal_read_bbreg(padapter, arg, 0xffffffff));
			break;
		case 0x73://write_bb
			rtw_hal_write_bbreg(padapter, arg, 0xffffffff, extra_arg);
			DBG_871X("write_bbreg(0x%x)=0x%x\n", arg, rtw_hal_read_bbreg(padapter, arg, 0xffffffff));
			break;
		case 0x74://read_rf
			DBG_871X("read RF_reg path(0x%02x),offset(0x%x),value(0x%08x)\n",minor_cmd,arg,rtw_hal_read_rfreg(padapter, minor_cmd, arg, 0xffffffff));	
			break;
		case 0x75://write_rf
			rtw_hal_write_rfreg(padapter, minor_cmd, arg, 0xffffffff, extra_arg);
			DBG_871X("write RF_reg path(0x%02x),offset(0x%x),value(0x%08x)\n",minor_cmd,arg, rtw_hal_read_rfreg(padapter, minor_cmd, arg, 0xffffffff));
			break;	

		case 0x76:
			switch(minor_cmd)
			{
				case 0x00: //normal mode, 
					padapter->recvpriv.is_signal_dbg = 0;
					break;
				case 0x01: //dbg mode
					padapter->recvpriv.is_signal_dbg = 1;
					extra_arg = extra_arg>100?100:extra_arg;
					extra_arg = extra_arg<0?0:extra_arg;
					padapter->recvpriv.signal_strength_dbg=extra_arg;
					break;
			}
			break;
		case 0x78: //IOL test
			switch(minor_cmd)
			{
				#ifdef CONFIG_IOL
				case 0x04: //LLT table initialization test
				{
					u8 page_boundary = 0xf9;
					{
						struct xmit_frame	*xmit_frame;

						if((xmit_frame=rtw_IOL_accquire_xmit_frame(padapter)) == NULL) {
							ret = -ENOMEM;	
							break;
						}
						
						rtw_IOL_append_LLT_cmd(xmit_frame, page_boundary);


						if(_SUCCESS != rtw_IOL_exec_cmds_sync(padapter, xmit_frame, 500) )
							ret = -EPERM;
					}
				}
					break;
				case 0x05: //blink LED test
				{
					u16 reg = 0x4c;
					u32 blink_num = 50;
					u32 blink_delay_ms = 200;
					int i;
					
					{
						struct xmit_frame	*xmit_frame;

						if((xmit_frame=rtw_IOL_accquire_xmit_frame(padapter)) == NULL) {
							ret = -ENOMEM;	
							break;
						}

						for(i=0;i<blink_num;i++){
							rtw_IOL_append_WB_cmd(xmit_frame, reg, 0x00);
							rtw_IOL_append_DELAY_MS_cmd(xmit_frame, blink_delay_ms);
							rtw_IOL_append_WB_cmd(xmit_frame, reg, 0x08);
							rtw_IOL_append_DELAY_MS_cmd(xmit_frame, blink_delay_ms);
						}
						if(_SUCCESS != rtw_IOL_exec_cmds_sync(padapter, xmit_frame, (blink_delay_ms*blink_num*2)+200) )
							ret = -EPERM;
					}
				}
					break;
					
				case 0x06: //continuous wirte byte test
				{
					u16 reg = arg;
					u16 start_value = 0;
					u32 write_num = extra_arg;
					int i;
					u8 final;
					
					{
						struct xmit_frame	*xmit_frame;

						if((xmit_frame=rtw_IOL_accquire_xmit_frame(padapter)) == NULL) {
							ret = -ENOMEM;	
							break;
						}

						for(i=0;i<write_num;i++){
							rtw_IOL_append_WB_cmd(xmit_frame, reg, i+start_value);
						}
						if(_SUCCESS != rtw_IOL_exec_cmds_sync(padapter, xmit_frame, 5000))
							ret = -EPERM;
					}

					if(start_value+write_num-1 == (final=rtw_read8(padapter, reg)) ) {
						DBG_871X("continuous IOL_CMD_WB_REG to 0x%x %u times Success, start:%u, final:%u\n", reg, write_num, start_value, final);
					} else {
						DBG_871X("continuous IOL_CMD_WB_REG to 0x%x %u times Fail, start:%u, final:%u\n", reg, write_num, start_value, final);
					}
				}
					break;
					
				case 0x07: //continuous wirte word test
				{
					u16 reg = arg;
					u16 start_value = 200;
					u32 write_num = extra_arg;
				
					int i;
					u16 final;

					{
						struct xmit_frame	*xmit_frame;

						if((xmit_frame=rtw_IOL_accquire_xmit_frame(padapter)) == NULL) {
							ret = -ENOMEM;	
							break;
						}

						for(i=0;i<write_num;i++){
							rtw_IOL_append_WW_cmd(xmit_frame, reg, i+start_value);
						}
						if(_SUCCESS !=rtw_IOL_exec_cmds_sync(padapter, xmit_frame, 5000))
							ret = -EPERM;
					}

					if(start_value+write_num-1 == (final=rtw_read16(padapter, reg)) ) {
						DBG_871X("continuous IOL_CMD_WW_REG to 0x%x %u times Success, start:%u, final:%u\n", reg, write_num, start_value, final);
					} else {
						DBG_871X("continuous IOL_CMD_WW_REG to 0x%x %u times Fail, start:%u, final:%u\n", reg, write_num, start_value, final);
					}
				}
					break;
					
				case 0x08: //continuous wirte dword test
				{
					u16 reg = arg;
					u32 start_value = 0x110000c7;
					u32 write_num = extra_arg;
				
					int i;
					u32 final;

					{
						struct xmit_frame	*xmit_frame;

						if((xmit_frame=rtw_IOL_accquire_xmit_frame(padapter)) == NULL) {
							ret = -ENOMEM;	
							break;
						}

						for(i=0;i<write_num;i++){
							rtw_IOL_append_WD_cmd(xmit_frame, reg, i+start_value);
						}
						if(_SUCCESS !=rtw_IOL_exec_cmds_sync(padapter, xmit_frame, 5000))
							ret = -EPERM;
							
					}

					if(start_value+write_num-1 == (final=rtw_read32(padapter, reg)) ) {
						DBG_871X("continuous IOL_CMD_WD_REG to 0x%x %u times Success, start:%u, final:%u\n", reg, write_num, start_value, final);
					} else {
						DBG_871X("continuous IOL_CMD_WD_REG to 0x%x %u times Fail, start:%u, final:%u\n", reg, write_num, start_value, final);
					}
				}
					break;
				#endif //CONFIG_IOL
			}
			break;

		case 0x7F:
			switch(minor_cmd)
			{
				case 0x0:
					DBG_871X("fwstate=0x%x\n", get_fwstate(pmlmepriv));
					break;
				case 0x01:
					DBG_871X("auth_alg=0x%x, enc_alg=0x%x, auth_type=0x%x, enc_type=0x%x\n", 
						psecuritypriv->dot11AuthAlgrthm, psecuritypriv->dot11PrivacyAlgrthm,
						psecuritypriv->ndisauthtype, psecuritypriv->ndisencryptstatus);
					break;
				case 0x02:
					DBG_871X("pmlmeinfo->state=0x%x\n", pmlmeinfo->state);
					break;
				case 0x03:
					DBG_871X("qos_option=%d\n", pmlmepriv->qospriv.qos_option);
					DBG_871X("ht_option=%d\n", pmlmepriv->htpriv.ht_option);
					break;
				case 0x04:
					DBG_871X("cur_ch=%d\n", pmlmeext->cur_channel);
					DBG_871X("cur_bw=%d\n", pmlmeext->cur_bwmode);
					DBG_871X("cur_ch_off=%d\n", pmlmeext->cur_ch_offset);
					break;
				case 0x05:
					psta = rtw_get_stainfo(pstapriv, cur_network->network.MacAddress);
					if(psta)
					{
						int i;
						struct recv_reorder_ctrl *preorder_ctrl;
					
						DBG_871X("SSID=%s\n", cur_network->network.Ssid.Ssid);
						DBG_871X("sta's macaddr:" MAC_FMT "\n", MAC_ARG(psta->hwaddr));
						DBG_871X("cur_channel=%d, cur_bwmode=%d, cur_ch_offset=%d\n", pmlmeext->cur_channel, pmlmeext->cur_bwmode, pmlmeext->cur_ch_offset);
						DBG_871X("rtsen=%d, cts2slef=%d\n", psta->rtsen, psta->cts2self);
						DBG_871X("qos_en=%d, ht_en=%d, init_rate=%d\n", psta->qos_option, psta->htpriv.ht_option, psta->init_rate);	
						DBG_871X("state=0x%x, aid=%d, macid=%d, raid=%d\n", psta->state, psta->aid, psta->mac_id, psta->raid);	
						DBG_871X("bwmode=%d, ch_offset=%d, sgi=%d\n", psta->htpriv.bwmode, psta->htpriv.ch_offset, psta->htpriv.sgi);						
						DBG_871X("ampdu_enable = %d\n", psta->htpriv.ampdu_enable);	
						DBG_871X("agg_enable_bitmap=%x, candidate_tid_bitmap=%x\n", psta->htpriv.agg_enable_bitmap, psta->htpriv.candidate_tid_bitmap);
						
						for(i=0;i<16;i++)
						{							
							preorder_ctrl = &psta->recvreorder_ctrl[i];
							if(preorder_ctrl->enable)
							{
								DBG_871X("tid=%d, indicate_seq=%d\n", i, preorder_ctrl->indicate_seq);
							}
						}	
							
					}
					else
					{							
						DBG_871X("can't get sta's macaddr, cur_network's macaddr:" MAC_FMT "\n", MAC_ARG(cur_network->network.MacAddress));
					}					
					break;
				case 0x06:
					{
						u32	ODMFlag;
						rtw_hal_get_hwreg(padapter, HW_VAR_DM_FLAG, (u8*)(&ODMFlag));
						DBG_871X("(B)DMFlag=0x%x, arg=0x%x\n", ODMFlag, arg);
						ODMFlag = (u32)(0x0f&arg);
						DBG_871X("(A)DMFlag=0x%x\n", ODMFlag);
						rtw_hal_set_hwreg(padapter, HW_VAR_DM_FLAG, (u8 *)(&ODMFlag));
					}
					break;
				case 0x07:
					DBG_871X("bSurpriseRemoved=%d, bDriverStopped=%d\n", 
						padapter->bSurpriseRemoved, padapter->bDriverStopped);
					break;
				case 0x08:
					{
						struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
						struct recv_priv  *precvpriv = &padapter->recvpriv;
						
						DBG_871X("free_xmitbuf_cnt=%d, free_xmitframe_cnt=%d, free_xmit_extbuf_cnt=%d\n", 
							pxmitpriv->free_xmitbuf_cnt, pxmitpriv->free_xmitframe_cnt, pxmitpriv->free_xmit_extbuf_cnt);
						#ifdef CONFIG_USB_HCI
						DBG_871X("rx_urb_pending_cn=%d\n", precvpriv->rx_pending_cnt);
						#endif
					}
					break;	
				case 0x09:
					{
						int i, j;
						_list	*plist, *phead;
						struct recv_reorder_ctrl *preorder_ctrl;
						
#ifdef CONFIG_AP_MODE
						DBG_871X("sta_dz_bitmap=0x%x, tim_bitmap=0x%x\n", pstapriv->sta_dz_bitmap, pstapriv->tim_bitmap);
#endif						
						_enter_critical_bh(&pstapriv->sta_hash_lock, &irqL);

						for(i=0; i< NUM_STA; i++)
						{
							phead = &(pstapriv->sta_hash[i]);
							plist = get_next(phead);
		
							while ((rtw_end_of_queue_search(phead, plist)) == _FALSE)
							{
								psta = LIST_CONTAINOR(plist, struct sta_info, hash_list);

								plist = get_next(plist);

								if(extra_arg == psta->aid)
								{
									DBG_871X("sta's macaddr:" MAC_FMT "\n", MAC_ARG(psta->hwaddr));
									DBG_871X("rtsen=%d, cts2slef=%d\n", psta->rtsen, psta->cts2self);
									DBG_871X("qos_en=%d, ht_en=%d, init_rate=%d\n", psta->qos_option, psta->htpriv.ht_option, psta->init_rate);	
									DBG_871X("state=0x%x, aid=%d, macid=%d, raid=%d\n", psta->state, psta->aid, psta->mac_id, psta->raid);	
									DBG_871X("bwmode=%d, ch_offset=%d, sgi=%d\n", psta->htpriv.bwmode, psta->htpriv.ch_offset, psta->htpriv.sgi);						
									DBG_871X("ampdu_enable = %d\n", psta->htpriv.ampdu_enable);									
									DBG_871X("agg_enable_bitmap=%x, candidate_tid_bitmap=%x\n", psta->htpriv.agg_enable_bitmap, psta->htpriv.candidate_tid_bitmap);
#ifdef CONFIG_AP_MODE
									DBG_871X("capability=0x%x\n", psta->capability);
									DBG_871X("flags=0x%x\n", psta->flags);
									DBG_871X("wpa_psk=0x%x\n", psta->wpa_psk);
									DBG_871X("wpa2_group_cipher=0x%x\n", psta->wpa2_group_cipher);
									DBG_871X("wpa2_pairwise_cipher=0x%x\n", psta->wpa2_pairwise_cipher);
									DBG_871X("qos_info=0x%x\n", psta->qos_info);
#endif
									DBG_871X("dot118021XPrivacy=0x%x\n", psta->dot118021XPrivacy);
									
									
						
									for(j=0;j<16;j++)
									{							
										preorder_ctrl = &psta->recvreorder_ctrl[j];
										if(preorder_ctrl->enable)
										{
											DBG_871X("tid=%d, indicate_seq=%d\n", j, preorder_ctrl->indicate_seq);
										}
									}		
									
								}							
			
							}
						}
	
						_exit_critical_bh(&pstapriv->sta_hash_lock, &irqL);

					}
					break;

                                case 0x0c://dump rx/tx packet
					{
						if(arg == 0){
							DBG_871X("dump rx packet (%d)\n",extra_arg);						
							//pHalData->bDumpRxPkt =extra_arg;						
							rtw_hal_set_def_var(padapter, HAL_DEF_DBG_DUMP_RXPKT, &(extra_arg));
						}
						else if(arg==1){
							DBG_871X("dump tx packet (%d)\n",extra_arg);						
							rtw_hal_set_def_var(padapter, HAL_DEF_DBG_DUMP_TXPKT, &(extra_arg));
						}
					}
					break;
#if 0				
					case 0x0d://dump cam
					{
						//u8 entry = (u8) extra_arg;
						u8 entry=0;
						//dump cam
						for(entry=0;entry<32;entry++)
							read_cam(padapter,entry);
					}				
					break;
#endif
		#ifdef DBG_CONFIG_ERROR_DETECT
				case 0x0f:
						{
							if(extra_arg == 0){	
								DBG_871X("###### silent reset test.......#####\n");
								rtw_hal_sreset_reset(padapter);						
							}
							
						}
				break;
				case 0x12:
					{
						struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;	
						DBG_871X("==>silent resete cnts:%d\n",pwrpriv->ips_enter_cnts);
					}
					break;	
					
		#endif	

				case 0x10:// driver version display
					DBG_871X("rtw driver version=%s\n", DRIVERVERSION);
					break;
				case 0x11:
					{
						DBG_871X("turn %s Rx RSSI display function\n",(extra_arg==1)?"on":"off");
						padapter->bRxRSSIDisplay = extra_arg ;						
					}
					break;			
				case 0xaa:
					{
						if(extra_arg> 0x13) extra_arg = 0xFF;
						DBG_871X("chang data rate to :0x%02x\n",extra_arg);
						padapter->fix_rate = extra_arg;
					}
					break;	
				case 0xdd://registers dump , 0 for mac reg,1 for bb reg, 2 for rf reg
					{						
						if(extra_arg==0){
							mac_reg_dump(padapter);
						}
						else if(extra_arg==1){
							bb_reg_dump(padapter);
						}
						else if(extra_arg==2){
							rf_reg_dump(padapter);
						}
																				
					}
					break;		

				case 0xee://turn on/off dynamic funcs
					{
						u32 odm_flag;

						if(0xf==extra_arg){
							rtw_hal_get_def_var(padapter, HAL_DEF_DBG_DM_FUNC,&odm_flag);							
							DBG_871X(" === DMFlag(0x%08x) === \n",odm_flag);
							DBG_871X("extra_arg = 0  - disable all dynamic func \n");
							DBG_871X("extra_arg = 1  - disable DIG- BIT(0)\n");
							DBG_871X("extra_arg = 2  - disable High power - BIT(1)\n");
							DBG_871X("extra_arg = 3  - disable tx power tracking - BIT(2)\n");
							DBG_871X("extra_arg = 4  - disable BT coexistence - BIT(3)\n");
							DBG_871X("extra_arg = 5  - disable antenna diversity - BIT(4)\n");
							DBG_871X("extra_arg = 6  - enable all dynamic func \n");							
						}
						else{
							/*	extra_arg = 0  - disable all dynamic func
								extra_arg = 1  - disable DIG
								extra_arg = 2  - disable tx power tracking
								extra_arg = 3  - turn on all dynamic func
							*/			
							rtw_hal_set_def_var(padapter, HAL_DEF_DBG_DM_FUNC, &(extra_arg));
							rtw_hal_get_def_var(padapter, HAL_DEF_DBG_DM_FUNC,&odm_flag);							
							DBG_871X(" === DMFlag(0x%08x) === \n",odm_flag);
						}
					}
					break;

				case 0xfd:
					rtw_write8(padapter, 0xc50, arg);
					DBG_871X("wr(0xc50)=0x%x\n", rtw_read8(padapter, 0xc50));
					rtw_write8(padapter, 0xc58, arg);
					DBG_871X("wr(0xc58)=0x%x\n", rtw_read8(padapter, 0xc58));
					break;
				case 0xfe:
					DBG_871X("rd(0xc50)=0x%x\n", rtw_read8(padapter, 0xc50));
					DBG_871X("rd(0xc58)=0x%x\n", rtw_read8(padapter, 0xc58));
					break;
				case 0xff:
					{
						DBG_871X("dbg(0x210)=0x%x\n", rtw_read32(padapter, 0x210));
						DBG_871X("dbg(0x608)=0x%x\n", rtw_read32(padapter, 0x608));
						DBG_871X("dbg(0x280)=0x%x\n", rtw_read32(padapter, 0x280));
						DBG_871X("dbg(0x284)=0x%x\n", rtw_read32(padapter, 0x284));
						DBG_871X("dbg(0x288)=0x%x\n", rtw_read32(padapter, 0x288));
	
						DBG_871X("dbg(0x664)=0x%x\n", rtw_read32(padapter, 0x664));


						DBG_871X("\n");
		
						DBG_871X("dbg(0x430)=0x%x\n", rtw_read32(padapter, 0x430));
						DBG_871X("dbg(0x438)=0x%x\n", rtw_read32(padapter, 0x438));

						DBG_871X("dbg(0x440)=0x%x\n", rtw_read32(padapter, 0x440));
	
						DBG_871X("dbg(0x458)=0x%x\n", rtw_read32(padapter, 0x458));
	
						DBG_871X("dbg(0x484)=0x%x\n", rtw_read32(padapter, 0x484));
						DBG_871X("dbg(0x488)=0x%x\n", rtw_read32(padapter, 0x488));
	
						DBG_871X("dbg(0x444)=0x%x\n", rtw_read32(padapter, 0x444));
						DBG_871X("dbg(0x448)=0x%x\n", rtw_read32(padapter, 0x448));
						DBG_871X("dbg(0x44c)=0x%x\n", rtw_read32(padapter, 0x44c));
						DBG_871X("dbg(0x450)=0x%x\n", rtw_read32(padapter, 0x450));
					}
					break;
			}			
			break;
		default:
			DBG_871X("error dbg cmd!\n");
			break;	
	}
	

	return ret;

}

static int wpa_set_param(struct net_device *dev, u8 name, u32 value)
{
	uint ret=0;
	u32 flags;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	
	switch (name){
	case IEEE_PARAM_WPA_ENABLED:

		padapter->securitypriv.dot11AuthAlgrthm= dot11AuthAlgrthm_8021X; //802.1x
		
		//ret = ieee80211_wpa_enable(ieee, value);
		
		switch((value)&0xff)
		{
			case 1 : //WPA
			padapter->securitypriv.ndisauthtype = Ndis802_11AuthModeWPAPSK; //WPA_PSK
			padapter->securitypriv.ndisencryptstatus = Ndis802_11Encryption2Enabled;
				break;
			case 2: //WPA2
			padapter->securitypriv.ndisauthtype = Ndis802_11AuthModeWPA2PSK; //WPA2_PSK
			padapter->securitypriv.ndisencryptstatus = Ndis802_11Encryption3Enabled;
				break;
		}
		
		RT_TRACE(_module_rtl871x_ioctl_os_c,_drv_info_,("wpa_set_param:padapter->securitypriv.ndisauthtype=%d\n", padapter->securitypriv.ndisauthtype));
		
		break;

	case IEEE_PARAM_TKIP_COUNTERMEASURES:
		//ieee->tkip_countermeasures=value;
		break;

	case IEEE_PARAM_DROP_UNENCRYPTED: 
	{
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
		 
#if 0	 
		struct ieee80211_security sec = {
			.flags = SEC_ENABLED,
			.enabled = value,
		};
 		ieee->drop_unencrypted = value;
		/* We only change SEC_LEVEL for open mode. Others
		 * are set by ipw_wpa_set_encryption.
		 */
		if (!value) {
			sec.flags |= SEC_LEVEL;
			sec.level = SEC_LEVEL_0;
		}
		else {
			sec.flags |= SEC_LEVEL;
			sec.level = SEC_LEVEL_1;
		}
		if (ieee->set_security)
			ieee->set_security(ieee->dev, &sec);
#endif		
		break;

	}
	case IEEE_PARAM_PRIVACY_INVOKED:	
		
		//ieee->privacy_invoked=value;
		
		break;

	case IEEE_PARAM_AUTH_ALGS:
		
		ret = wpa_set_auth_algs(dev, value);
		
		break;

	case IEEE_PARAM_IEEE_802_1X:
		
		//ieee->ieee802_1x=value;		
		
		break;
		
	case IEEE_PARAM_WPAX_SELECT:
		
		// added for WPA2 mixed mode
		//DBG_871X(KERN_WARNING "------------------------>wpax value = %x\n", value);
		/*
		spin_lock_irqsave(&ieee->wpax_suitlist_lock,flags);
		ieee->wpax_type_set = 1;
		ieee->wpax_type_notify = value;
		spin_unlock_irqrestore(&ieee->wpax_suitlist_lock,flags);
		*/
		
		break;

	default:		


		
		ret = -EOPNOTSUPP;

		
		break;
	
	}

	return ret;
	
}

static int wpa_mlme(struct net_device *dev, u32 command, u32 reason)
{	
	int ret = 0;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);

	switch (command)
	{
		case IEEE_MLME_STA_DEAUTH:

			if(!rtw_set_802_11_disassociate(padapter))
				ret = -1;		
			
			break;

		case IEEE_MLME_STA_DISASSOC:
		
			if(!rtw_set_802_11_disassociate(padapter))
				ret = -1;		
	
			break;

		default:
			ret = -EOPNOTSUPP;
			break;
	}

	return ret;
	
}

static int wpa_supplicant_ioctl(struct net_device *dev, struct iw_point *p)
{
	struct ieee_param *param;
	uint ret=0;

	//down(&ieee->wx_sem);	

	if (p->length < sizeof(struct ieee_param) || !p->pointer){
		ret = -EINVAL;
		goto out;
	}
	
	param = (struct ieee_param *)rtw_malloc(p->length);
	if (param == NULL)
	{
		ret = -ENOMEM;
		goto out;
	}
	
	if (copy_from_user(param, p->pointer, p->length))
	{
		rtw_mfree((u8*)param, p->length);
		ret = -EFAULT;
		goto out;
	}

	switch (param->cmd) {

	case IEEE_CMD_SET_WPA_PARAM:
		ret = wpa_set_param(dev, param->u.wpa_param.name, param->u.wpa_param.value);
		break;

	case IEEE_CMD_SET_WPA_IE:
		//ret = wpa_set_wpa_ie(dev, param, p->length);
		ret =  rtw_set_wpa_ie((_adapter *)rtw_netdev_priv(dev), (char*)param->u.wpa_ie.data, (u16)param->u.wpa_ie.len);
		break;

	case IEEE_CMD_SET_ENCRYPTION:
		ret = wpa_set_encryption(dev, param, p->length);
		break;

	case IEEE_CMD_MLME:
		ret = wpa_mlme(dev, param->u.mlme.command, param->u.mlme.reason_code);
		break;

	default:
		DBG_871X("Unknown WPA supplicant request: %d\n", param->cmd);
		ret = -EOPNOTSUPP;
		break;
		
	}

	if (ret == 0 && copy_to_user(p->pointer, param, p->length))
		ret = -EFAULT;

	rtw_mfree((u8 *)param, p->length);
	
out:
	
	//up(&ieee->wx_sem);
	
	return ret;
	
}

#ifdef CONFIG_AP_MODE
static u8 set_pairwise_key(_adapter *padapter, struct sta_info *psta)
{
	struct cmd_obj*			ph2c;
	struct set_stakey_parm	*psetstakey_para;
	struct cmd_priv 			*pcmdpriv=&padapter->cmdpriv;	
	u8	res=_SUCCESS;

	ph2c = (struct cmd_obj*)rtw_zmalloc(sizeof(struct cmd_obj));
	if ( ph2c == NULL){
		res= _FAIL;
		goto exit;
	}

	psetstakey_para = (struct set_stakey_parm*)rtw_zmalloc(sizeof(struct set_stakey_parm));
	if(psetstakey_para==NULL){
		rtw_mfree((u8 *) ph2c, sizeof(struct cmd_obj));
		res=_FAIL;
		goto exit;
	}

	init_h2fwcmd_w_parm_no_rsp(ph2c, psetstakey_para, _SetStaKey_CMD_);


	psetstakey_para->algorithm = (u8)psta->dot118021XPrivacy;

	_rtw_memcpy(psetstakey_para->addr, psta->hwaddr, ETH_ALEN);	
	
	_rtw_memcpy(psetstakey_para->key, &psta->dot118021x_UncstKey, 16);

	
	res = rtw_enqueue_cmd(pcmdpriv, ph2c);	

exit:

	return res;
	
}

static int set_group_key(_adapter *padapter, u8 *key, u8 alg, int keyid)
{
	u8 keylen;
	struct cmd_obj* pcmd;
	struct setkey_parm *psetkeyparm;
	struct cmd_priv	*pcmdpriv=&(padapter->cmdpriv);	
	int res=_SUCCESS;

	DBG_871X("%s\n", __FUNCTION__);
	
	pcmd = (struct cmd_obj*)rtw_zmalloc(sizeof(struct	cmd_obj));
	if(pcmd==NULL){
		res= _FAIL;
		goto exit;
	}
	psetkeyparm=(struct setkey_parm*)rtw_zmalloc(sizeof(struct setkey_parm));
	if(psetkeyparm==NULL){
		rtw_mfree((unsigned char *)pcmd, sizeof(struct cmd_obj));
		res= _FAIL;
		goto exit;
	}

	_rtw_memset(psetkeyparm, 0, sizeof(struct setkey_parm));
		
	psetkeyparm->keyid=(u8)keyid;

	psetkeyparm->algorithm = alg;

	psetkeyparm->set_tx = 1;

	switch(alg)
	{
		case _WEP40_:					
			keylen = 5;
			break;
		case _WEP104_:
			keylen = 13;			
			break;
		case _TKIP_:
		case _TKIP_WTMIC_:		
		case _AES_:
			keylen = 16;		
		default:
			keylen = 16;		
	}

	_rtw_memcpy(&(psetkeyparm->key[0]), key, keylen);
	
	pcmd->cmdcode = _SetKey_CMD_;
	pcmd->parmbuf = (u8 *)psetkeyparm;   
	pcmd->cmdsz =  (sizeof(struct setkey_parm));  
	pcmd->rsp = NULL;
	pcmd->rspsz = 0;


	_rtw_init_listhead(&pcmd->list);

	res = rtw_enqueue_cmd(pcmdpriv, pcmd);

exit:

	return res;
	

}

static int set_wep_key(_adapter *padapter, u8 *key, u8 keylen, int keyid)
{	
	u8 alg;

	switch(keylen)
	{
		case 5:
			alg =_WEP40_;			
			break;
		case 13:
			alg =_WEP104_;			
			break;
		default:
			alg =_NO_PRIVACY_;			
	}

	return set_group_key(padapter, key, alg, keyid);

}


static int rtw_set_encryption(struct net_device *dev, struct ieee_param *param, u32 param_len)
{
	int ret = 0;
	u32 wep_key_idx, wep_key_len,wep_total_len;
	NDIS_802_11_WEP	 *pwep = NULL;
	struct sta_info *psta = NULL, *pbcmc_sta = NULL;	
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct mlme_priv 	*pmlmepriv = &padapter->mlmepriv;
	struct security_priv* psecuritypriv=&(padapter->securitypriv);
	struct sta_priv *pstapriv = &padapter->stapriv;

	DBG_871X("%s\n", __FUNCTION__);

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
			DBG_871X("rtw_set_encryption(), sta has already been removed or never been added\n");
			goto exit;
		}			
	}

	if (strcmp(param->u.crypt.alg, "none") == 0 && (psta==NULL))
	{
		//todo:clear default encryption keys

		DBG_871X("clear default encryption keys, keyid=%d\n", param->u.crypt.idx);
		
		goto exit;
	}


	if (strcmp(param->u.crypt.alg, "WEP") == 0 && (psta==NULL))
	{		
		DBG_871X("r871x_set_encryption, crypt.alg = WEP\n");
		
		wep_key_idx = param->u.crypt.idx;
		wep_key_len = param->u.crypt.key_len;
					
		DBG_871X("r871x_set_encryption, wep_key_idx=%d, len=%d\n", wep_key_idx, wep_key_len);

		if((wep_key_idx >= WEP_KEYS) || (wep_key_len<=0))
		{
			ret = -EINVAL;
			goto exit;
		}
			

		if (wep_key_len > 0) 
		{			
		 	wep_key_len = wep_key_len <= 5 ? 5 : 13;
			wep_total_len = wep_key_len + FIELD_OFFSET(NDIS_802_11_WEP, KeyMaterial);
		 	pwep =(NDIS_802_11_WEP *)rtw_malloc(wep_total_len);
			if(pwep == NULL){
				DBG_871X(" r871x_set_encryption: pwep allocate fail !!!\n");
				goto exit;
			}
			
		 	_rtw_memset(pwep, 0, wep_total_len);
		
		 	pwep->KeyLength = wep_key_len;
			pwep->Length = wep_total_len;
			
		}
		
		pwep->KeyIndex = wep_key_idx;

		_rtw_memcpy(pwep->KeyMaterial,  param->u.crypt.key, pwep->KeyLength);

		if(param->u.crypt.set_tx)
		{
			DBG_871X("wep, set_tx=1\n");

			psecuritypriv->ndisencryptstatus = Ndis802_11Encryption1Enabled;
			psecuritypriv->dot11PrivacyAlgrthm=_WEP40_;
			psecuritypriv->dot118021XGrpPrivacy=_WEP40_;
			
			if(pwep->KeyLength==13)
			{
				psecuritypriv->dot11PrivacyAlgrthm=_WEP104_;
				psecuritypriv->dot118021XGrpPrivacy=_WEP104_;
			}

		
			psecuritypriv->dot11PrivacyKeyIndex = wep_key_idx;
			
			_rtw_memcpy(&(psecuritypriv->dot11DefKey[wep_key_idx].skey[0]), pwep->KeyMaterial, pwep->KeyLength);

			psecuritypriv->dot11DefKeylen[wep_key_idx]=pwep->KeyLength;

			set_wep_key(padapter, pwep->KeyMaterial, pwep->KeyLength, wep_key_idx);		

			
		}
		else
		{
			DBG_871X("wep, set_tx=0\n");
			
			//don't update "psecuritypriv->dot11PrivacyAlgrthm" and 
			//"psecuritypriv->dot11PrivacyKeyIndex=keyid", but can rtw_set_key to cam
					
		      _rtw_memcpy(&(psecuritypriv->dot11DefKey[wep_key_idx].skey[0]), pwep->KeyMaterial, pwep->KeyLength);

			psecuritypriv->dot11DefKeylen[wep_key_idx] = pwep->KeyLength;			

			set_wep_key(padapter, pwep->KeyMaterial, pwep->KeyLength, wep_key_idx);
			
		}

		goto exit;
		
	}

	
	if(!psta && check_fwstate(pmlmepriv, WIFI_AP_STATE)) // //group key
	{
		if(param->u.crypt.set_tx ==1)
		{
			if(strcmp(param->u.crypt.alg, "WEP") == 0)
			{
				DBG_871X("%s, set group_key, WEP\n", __FUNCTION__);
				
				_rtw_memcpy(psecuritypriv->dot118021XGrpKey[param->u.crypt.idx].skey,  param->u.crypt.key, (param->u.crypt.key_len>16 ?16:param->u.crypt.key_len));
					
				psecuritypriv->dot118021XGrpPrivacy = _WEP40_;
				if(param->u.crypt.key_len==13)
				{						
						psecuritypriv->dot118021XGrpPrivacy = _WEP104_;
				}
				
			}
			else if(strcmp(param->u.crypt.alg, "TKIP") == 0)
			{						
				DBG_871X("%s, set group_key, TKIP\n", __FUNCTION__);
				
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
				DBG_871X("%s, set group_key, CCMP\n", __FUNCTION__);
			
				psecuritypriv->dot118021XGrpPrivacy = _AES_;

				_rtw_memcpy(psecuritypriv->dot118021XGrpKey[param->u.crypt.idx].skey,  param->u.crypt.key, (param->u.crypt.key_len>16 ?16:param->u.crypt.key_len));
			}
			else
			{
				DBG_871X("%s, set group_key, none\n", __FUNCTION__);
				
				psecuritypriv->dot118021XGrpPrivacy = _NO_PRIVACY_;
			}

			psecuritypriv->dot118021XGrpKeyid = param->u.crypt.idx;

			psecuritypriv->binstallGrpkey = _TRUE;

			psecuritypriv->dot11PrivacyAlgrthm = psecuritypriv->dot118021XGrpPrivacy;//!!!
								
			set_group_key(padapter, param->u.crypt.key, psecuritypriv->dot118021XGrpPrivacy, param->u.crypt.idx);
			
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
			if(param->u.crypt.set_tx ==1)
			{ 
				_rtw_memcpy(psta->dot118021x_UncstKey.skey,  param->u.crypt.key, (param->u.crypt.key_len>16 ?16:param->u.crypt.key_len));
				
				if(strcmp(param->u.crypt.alg, "WEP") == 0)
				{
					DBG_871X("%s, set pairwise key, WEP\n", __FUNCTION__);
					
					psta->dot118021XPrivacy = _WEP40_;
					if(param->u.crypt.key_len==13)
					{						
						psta->dot118021XPrivacy = _WEP104_;
					}
				}
				else if(strcmp(param->u.crypt.alg, "TKIP") == 0)
				{						
					DBG_871X("%s, set pairwise key, TKIP\n", __FUNCTION__);
					
					psta->dot118021XPrivacy = _TKIP_;
				
					//DEBUG_ERR("set key length :param->u.crypt.key_len=%d\n", param->u.crypt.key_len);
					//set mic key
					_rtw_memcpy(psta->dot11tkiptxmickey.skey, &(param->u.crypt.key[16]), 8);
					_rtw_memcpy(psta->dot11tkiprxmickey.skey, &(param->u.crypt.key[24]), 8);

					psecuritypriv->busetkipkey = _TRUE;
											
				}
				else if(strcmp(param->u.crypt.alg, "CCMP") == 0)
				{

					DBG_871X("%s, set pairwise key, CCMP\n", __FUNCTION__);
					
					psta->dot118021XPrivacy = _AES_;
				}
				else
				{
					DBG_871X("%s, set pairwise key, none\n", __FUNCTION__);
					
					psta->dot118021XPrivacy = _NO_PRIVACY_;
				}
						
				set_pairwise_key(padapter, psta);
					
				psta->ieee8021x_blocked = _FALSE;
					
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
								
				set_group_key(padapter, param->u.crypt.key, psecuritypriv->dot118021XGrpPrivacy, param->u.crypt.idx);
			
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

	if(pwep)
	{
		rtw_mfree((u8 *)pwep, wep_total_len);		
	}	
	
	return ret;
	
}

static int rtw_set_beacon(struct net_device *dev, struct ieee_param *param, int len)
{
	int ret=0;	
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct sta_priv *pstapriv = &padapter->stapriv;
	unsigned char *pbuf = param->u.bcn_ie.buf;


	DBG_871X("%s, len=%d\n", __FUNCTION__, len);

	if(check_fwstate(pmlmepriv, WIFI_AP_STATE) != _TRUE)
		return -EINVAL;

	_rtw_memcpy(&pstapriv->max_num_sta, param->u.bcn_ie.reserved, 2);

	if((pstapriv->max_num_sta>NUM_STA) || (pstapriv->max_num_sta<=0))
		pstapriv->max_num_sta = NUM_STA;


	if(rtw_check_beacon_data(padapter, pbuf,  (len-12-2)) == _SUCCESS)// 12 = param header, 2:no packed
		ret = 0;
	else
		ret = -EINVAL;
	

	return ret;
	
}

static int rtw_hostapd_sta_flush(struct net_device *dev)
{
	//_irqL irqL;
	//_list	*phead, *plist;
	int ret=0;	
	//struct sta_info *psta = NULL;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);	
	//struct sta_priv *pstapriv = &padapter->stapriv;

	DBG_871X("%s\n", __FUNCTION__);

	flush_all_cam_entry(padapter);	//clear CAM

#if 0
	phead = &pstapriv->asoc_list;
	plist = get_next(phead);
	
	//free sta asoc_queue
	while ((rtw_end_of_queue_search(phead, plist)) == _FALSE)	
	{		
		psta = LIST_CONTAINOR(plist, struct sta_info, asoc_list);
		
		plist = get_next(plist);

		rtw_list_delete(&psta->asoc_list);		

		//tear down Rx AMPDU
		send_delba(padapter, 0, psta->hwaddr);// recipient
	
		//tear down TX AMPDU
		send_delba(padapter, 1, psta->hwaddr);// // originator
		psta->htpriv.agg_enable_bitmap = 0x0;//reset
		psta->htpriv.candidate_tid_bitmap = 0x0;//reset

		issue_deauth(padapter, psta->hwaddr, WLAN_REASON_DEAUTH_LEAVING);

		_enter_critical_bh(&(pstapriv->sta_hash_lock), &irqL);					
		rtw_free_stainfo(padapter, psta);
		_exit_critical_bh(&(pstapriv->sta_hash_lock), &irqL);
		
	}
#endif

	ret = rtw_sta_flush(padapter);	

	return ret;

}

static int rtw_add_sta(struct net_device *dev, struct ieee_param *param)
{
	_irqL irqL;
	int ret=0;	
	struct sta_info *psta = NULL;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct sta_priv *pstapriv = &padapter->stapriv;

	DBG_871X("rtw_add_sta(aid=%d)=" MAC_FMT "\n", param->u.add_sta.aid, MAC_ARG(param->sta_addr));
	
	if(check_fwstate(pmlmepriv, (_FW_LINKED|WIFI_AP_STATE)) != _TRUE)	
	{
		return -EINVAL;		
	}

	if (param->sta_addr[0] == 0xff && param->sta_addr[1] == 0xff &&
	    param->sta_addr[2] == 0xff && param->sta_addr[3] == 0xff &&
	    param->sta_addr[4] == 0xff && param->sta_addr[5] == 0xff) 
	{
		return -EINVAL;	
	}

/*
	psta = rtw_get_stainfo(pstapriv, param->sta_addr);
	if(psta)
	{
		DBG_871X("rtw_add_sta(), free has been added psta=%p\n", psta);
		_enter_critical_bh(&(pstapriv->sta_hash_lock), &irqL);		
		rtw_free_stainfo(padapter,  psta);		
		_exit_critical_bh(&(pstapriv->sta_hash_lock), &irqL);

		psta = NULL;
	}	
*/
	//psta = rtw_alloc_stainfo(pstapriv, param->sta_addr);
	psta = rtw_get_stainfo(pstapriv, param->sta_addr);
	if(psta)
	{
		int flags = param->u.add_sta.flags;			
		
		//DBG_871X("rtw_add_sta(), init sta's variables, psta=%p\n", psta);
		
		psta->aid = param->u.add_sta.aid;//aid=1~2007

		_rtw_memcpy(psta->bssrateset, param->u.add_sta.tx_supp_rates, 16);
		
		
		//check wmm cap.
		if(WLAN_STA_WME&flags)
			psta->qos_option = 1;
		else
			psta->qos_option = 0;

		if(pmlmepriv->qospriv.qos_option == 0)	
			psta->qos_option = 0;

		
#ifdef CONFIG_80211N_HT		
		//chec 802.11n ht cap.
		if(WLAN_STA_HT&flags)
		{
			psta->htpriv.ht_option = _TRUE;
			psta->qos_option = 1;
			_rtw_memcpy((void*)&psta->htpriv.ht_cap, (void*)&param->u.add_sta.ht_cap, sizeof(struct rtw_ieee80211_ht_cap));
		}
		else		
		{
			psta->htpriv.ht_option = _FALSE;
		}
		
		if(pmlmepriv->htpriv.ht_option == _FALSE)	
			psta->htpriv.ht_option = _FALSE;
#endif		


		update_sta_info_apmode(padapter, psta);
		
		
	}
	else
	{
		ret = -ENOMEM;
	}	
	
	return ret;
	
}

static int rtw_del_sta(struct net_device *dev, struct ieee_param *param)
{
	_irqL irqL;
	int ret=0;	
	struct sta_info *psta = NULL;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct sta_priv *pstapriv = &padapter->stapriv;

	DBG_871X("rtw_del_sta=" MAC_FMT "\n", MAC_ARG(param->sta_addr));
		
	if(check_fwstate(pmlmepriv, (_FW_LINKED|WIFI_AP_STATE)) != _TRUE)		
	{
		return -EINVAL;		
	}

	if (param->sta_addr[0] == 0xff && param->sta_addr[1] == 0xff &&
	    param->sta_addr[2] == 0xff && param->sta_addr[3] == 0xff &&
	    param->sta_addr[4] == 0xff && param->sta_addr[5] == 0xff) 
	{
		return -EINVAL;	
	}

	psta = rtw_get_stainfo(pstapriv, param->sta_addr);
	if(psta)
	{
		//DBG_871X("free psta=%p, aid=%d\n", psta, psta->aid);
		
#if 0		
		//tear down Rx AMPDU
		send_delba(padapter, 0, psta->hwaddr);// recipient
	
		//tear down TX AMPDU
		send_delba(padapter, 1, psta->hwaddr);// // originator
		psta->htpriv.agg_enable_bitmap = 0x0;//reset
		psta->htpriv.candidate_tid_bitmap = 0x0;//reset
		
		issue_deauth(padapter, psta->hwaddr, WLAN_REASON_DEAUTH_LEAVING);
		
		_enter_critical_bh(&(pstapriv->sta_hash_lock), &irqL);		
		rtw_free_stainfo(padapter,  psta);		
		_exit_critical_bh(&(pstapriv->sta_hash_lock), &irqL);

		pstapriv->sta_dz_bitmap &=~BIT(psta->aid);
		pstapriv->tim_bitmap &=~BIT(psta->aid);		
#endif
		_enter_critical_bh(&pstapriv->asoc_list_lock, &irqL);
		if(rtw_is_list_empty(&psta->asoc_list)==_FALSE)
		{			
			rtw_list_delete(&psta->asoc_list);
			ap_free_sta(padapter, psta);

		}
		_exit_critical_bh(&pstapriv->asoc_list_lock, &irqL);
		

	
		psta = NULL;
		
	}
	else
	{
		DBG_871X("rtw_del_sta(), sta has already been removed or never been added\n");
		
		//ret = -1;
	}
	
	
	return ret;
	
}

static int rtw_get_sta_wpaie(struct net_device *dev, struct ieee_param *param)
{
	int ret=0;	
	struct sta_info *psta = NULL;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct sta_priv *pstapriv = &padapter->stapriv;

	DBG_871X("rtw_get_sta_wpaie, sta_addr: " MAC_FMT "\n", MAC_ARG(param->sta_addr));

	if(check_fwstate(pmlmepriv, (_FW_LINKED|WIFI_AP_STATE)) != _TRUE)		
	{
		return -EINVAL;		
	}

	if (param->sta_addr[0] == 0xff && param->sta_addr[1] == 0xff &&
	    param->sta_addr[2] == 0xff && param->sta_addr[3] == 0xff &&
	    param->sta_addr[4] == 0xff && param->sta_addr[5] == 0xff) 
	{
		return -EINVAL;	
	}

	psta = rtw_get_stainfo(pstapriv, param->sta_addr);
	if(psta)
	{
		if((psta->wpa_ie[0] == WLAN_EID_RSN) || (psta->wpa_ie[0] == WLAN_EID_GENERIC))
		{
			int wpa_ie_len;
			int copy_len;

			wpa_ie_len = psta->wpa_ie[1];
			
			copy_len = ((wpa_ie_len+2) > sizeof(psta->wpa_ie)) ? (sizeof(psta->wpa_ie)):(wpa_ie_len+2);
				
			param->u.wpa_ie.len = copy_len;

			_rtw_memcpy(param->u.wpa_ie.reserved, psta->wpa_ie, copy_len);
		}
		else
		{
			//ret = -1;
			DBG_871X("sta's wpa_ie is NONE\n");
		}		
	}
	else
	{
		ret = -1;
	}

	return ret;

}

static int rtw_set_wps_beacon(struct net_device *dev, struct ieee_param *param, int len)
{
	int ret=0;
	unsigned char wps_oui[4]={0x0,0x50,0xf2,0x04};
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);	
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	int ie_len;

	DBG_871X("%s, len=%d\n", __FUNCTION__, len);

	if(check_fwstate(pmlmepriv, WIFI_AP_STATE) != _TRUE)
		return -EINVAL;

	ie_len = len-12-2;// 12 = param header, 2:no packed


	if(pmlmepriv->wps_beacon_ie)
	{
		rtw_mfree(pmlmepriv->wps_beacon_ie, pmlmepriv->wps_beacon_ie_len);
		pmlmepriv->wps_beacon_ie = NULL;			
	}	

	if(ie_len>0)
	{
		pmlmepriv->wps_beacon_ie = rtw_malloc(ie_len);
		pmlmepriv->wps_beacon_ie_len = ie_len;
		if ( pmlmepriv->wps_beacon_ie == NULL) {
			DBG_871X("%s()-%d: rtw_malloc() ERROR!\n", __FUNCTION__, __LINE__);
			return -EINVAL;
		}

		_rtw_memcpy(pmlmepriv->wps_beacon_ie, param->u.bcn_ie.buf, ie_len);

		update_beacon(padapter, _VENDOR_SPECIFIC_IE_, wps_oui, _TRUE);
		
		pmlmeext->bstart_bss = _TRUE;
		
	}
	
	
	return ret;		

}

static int rtw_set_wps_probe_resp(struct net_device *dev, struct ieee_param *param, int len)
{
	int ret=0;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);	
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	int ie_len;

	DBG_871X("%s, len=%d\n", __FUNCTION__, len);

	if(check_fwstate(pmlmepriv, WIFI_AP_STATE) != _TRUE)
		return -EINVAL;

	ie_len = len-12-2;// 12 = param header, 2:no packed


	if(pmlmepriv->wps_probe_resp_ie)
	{
		rtw_mfree(pmlmepriv->wps_probe_resp_ie, pmlmepriv->wps_probe_resp_ie_len);
		pmlmepriv->wps_probe_resp_ie = NULL;			
	}	

	if(ie_len>0)
	{
		pmlmepriv->wps_probe_resp_ie = rtw_malloc(ie_len);
		pmlmepriv->wps_probe_resp_ie_len = ie_len;
		if ( pmlmepriv->wps_probe_resp_ie == NULL) {
			DBG_871X("%s()-%d: rtw_malloc() ERROR!\n", __FUNCTION__, __LINE__);
			return -EINVAL;
		}
		_rtw_memcpy(pmlmepriv->wps_probe_resp_ie, param->u.bcn_ie.buf, ie_len);		
	}
	
	
	return ret;

}

static int rtw_set_hidden_ssid(struct net_device *dev, struct ieee_param *param, int len)
{
	int ret=0;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);	
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	u8 value;

	if(check_fwstate(pmlmepriv, WIFI_AP_STATE) != _TRUE)
		return -EINVAL;

	if(param->u.wpa_param.name != 0) //dummy test...
	{
		DBG_871X("%s name(%u) != 0\n", __FUNCTION__, param->u.wpa_param.name);
	}
	
	value = param->u.wpa_param.value;

	//use the same definition of hostapd's ignore_broadcast_ssid
	if(value != 1 && value != 2)
		value = 0;

	DBG_871X("%s value(%u)\n", __FUNCTION__, value);
	pmlmeinfo->hidden_ssid_mode = value;

	return ret;		

}


static int rtw_set_wps_assoc_resp(struct net_device *dev, struct ieee_param *param, int len)
{
	int ret=0;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);	
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	int ie_len;

	DBG_871X("%s, len=%d\n", __FUNCTION__, len);

	if(check_fwstate(pmlmepriv, WIFI_AP_STATE) != _TRUE)
		return -EINVAL;

	ie_len = len-12-2;// 12 = param header, 2:no packed


	if(pmlmepriv->wps_assoc_resp_ie)
	{
		rtw_mfree(pmlmepriv->wps_assoc_resp_ie, pmlmepriv->wps_assoc_resp_ie_len);
		pmlmepriv->wps_assoc_resp_ie = NULL;			
	}	

	if(ie_len>0)
	{
		pmlmepriv->wps_assoc_resp_ie = rtw_malloc(ie_len);
		pmlmepriv->wps_assoc_resp_ie_len = ie_len;
		if ( pmlmepriv->wps_assoc_resp_ie == NULL) {
			DBG_871X("%s()-%d: rtw_malloc() ERROR!\n", __FUNCTION__, __LINE__);
			return -EINVAL;
		}
		
		_rtw_memcpy(pmlmepriv->wps_assoc_resp_ie, param->u.bcn_ie.buf, ie_len);		
	}
	
	
	return ret;

}

static int rtw_hostapd_ioctl(struct net_device *dev, struct iw_point *p)
{
	struct ieee_param *param;
	int ret=0;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);

	//DBG_871X("%s\n", __FUNCTION__);

	/*
	* this function is expect to call in master mode, which allows no power saving
	* so, we just check hw_init_completed instead of call rfpwrstate_check()
	*/

	if (padapter->hw_init_completed==_FALSE){
		ret = -EPERM;
		goto out;
	}


	//if (p->length < sizeof(struct ieee_param) || !p->pointer){
	if(!p->pointer){
		ret = -EINVAL;
		goto out;
	}
	
	param = (struct ieee_param *)rtw_malloc(p->length);
	if (param == NULL)
	{
		ret = -ENOMEM;
		goto out;
	}
	
	if (copy_from_user(param, p->pointer, p->length))
	{
		rtw_mfree((u8*)param, p->length);
		ret = -EFAULT;
		goto out;
	}

	//DBG_871X("%s, cmd=%d\n", __FUNCTION__, param->cmd);

	switch (param->cmd) 
	{	
		case RTL871X_HOSTAPD_FLUSH:

			ret = rtw_hostapd_sta_flush(dev);

			break;
	
		case RTL871X_HOSTAPD_ADD_STA:	
			
			ret = rtw_add_sta(dev, param);					
			
			break;

		case RTL871X_HOSTAPD_REMOVE_STA:

			ret = rtw_del_sta(dev, param);

			break;
	
		case RTL871X_HOSTAPD_SET_BEACON:

			ret = rtw_set_beacon(dev, param, p->length);

			break;
			
		case RTL871X_SET_ENCRYPTION:

			ret = rtw_set_encryption(dev, param, p->length);
			
			break;
			
		case RTL871X_HOSTAPD_GET_WPAIE_STA:

			ret = rtw_get_sta_wpaie(dev, param);
	
			break;
			
		case RTL871X_HOSTAPD_SET_WPS_BEACON:

			ret = rtw_set_wps_beacon(dev, param, p->length);

			break;

		case RTL871X_HOSTAPD_SET_WPS_PROBE_RESP:

			ret = rtw_set_wps_probe_resp(dev, param, p->length);
			
	 		break;
			
		case RTL871X_HOSTAPD_SET_WPS_ASSOC_RESP:

			ret = rtw_set_wps_assoc_resp(dev, param, p->length);
			
	 		break;

		case RTL871X_HOSTAPD_SET_HIDDEN_SSID:

			ret = rtw_set_hidden_ssid(dev, param, p->length);

			break;
			
		default:
			DBG_871X("Unknown hostapd request: %d\n", param->cmd);
			ret = -EOPNOTSUPP;
			break;
		
	}

	if (ret == 0 && copy_to_user(p->pointer, param, p->length))
		ret = -EFAULT;


	rtw_mfree((u8 *)param, p->length);
	
out:
		
	return ret;
	
}
#endif

#include <rtw_android.h>
static int rtw_wx_set_priv(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *awrq,
				char *extra)
{

#ifdef CONFIG_DEBUG_RTW_WX_SET_PRIV
	char *ext_dbg;
#endif

	int ret = 0;
	int len = 0;
	char *ext;
	int i;

	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct iw_point *dwrq = (struct iw_point*)awrq;

	//RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_notice_, ("+rtw_wx_set_priv\n"));

	len = dwrq->length;
	if (!(ext = rtw_vmalloc(len)))
		return -ENOMEM;

	if (copy_from_user(ext, dwrq->pointer, len)) {
		rtw_vmfree(ext, len);
		return -EFAULT;
	}


	//RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_notice_,
	//	 ("rtw_wx_set_priv: %s req=%s\n",
	//	  dev->name, ext));

	#ifdef CONFIG_DEBUG_RTW_WX_SET_PRIV	
	if (!(ext_dbg = rtw_vmalloc(len)))
	{
		rtw_vmfree(ext, len);
		return -ENOMEM;
	}	
	
	_rtw_memcpy(ext_dbg, ext, len);
	#endif

	//added for wps2.0 @20110524
	if(dwrq->flags == 0x8766 && len > 8)
	{
		u32 cp_sz;		
		struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
		u8 *probereq_wpsie = ext;
		int probereq_wpsie_len = len;
		u8 wps_oui[4]={0x0,0x50,0xf2,0x04};		
	
		if((_VENDOR_SPECIFIC_IE_ == probereq_wpsie[0]) &&
			(_rtw_memcmp(&probereq_wpsie[2], wps_oui, 4) ==_TRUE))
		{
			cp_sz = probereq_wpsie_len>MAX_WPS_IE_LEN ? MAX_WPS_IE_LEN:probereq_wpsie_len;

			//_rtw_memcpy(pmlmepriv->probereq_wpsie, probereq_wpsie, cp_sz);
			//pmlmepriv->probereq_wpsie_len = cp_sz;
					
			printk("probe_req_wps_ielen=%d\n", cp_sz);
						
			if(pmlmepriv->wps_probe_req_ie)
			{
				u32 free_len = pmlmepriv->wps_probe_req_ie_len;
				pmlmepriv->wps_probe_req_ie_len = 0;
				rtw_mfree(pmlmepriv->wps_probe_req_ie, free_len);
				pmlmepriv->wps_probe_req_ie = NULL;			
			}	

			pmlmepriv->wps_probe_req_ie = rtw_malloc(cp_sz);
			if ( pmlmepriv->wps_probe_req_ie == NULL) {
				printk("%s()-%d: rtw_malloc() ERROR!\n", __FUNCTION__, __LINE__);
				ret =  -EINVAL;
				goto FREE_EXT;
			
			}
			
			_rtw_memcpy(pmlmepriv->wps_probe_req_ie, probereq_wpsie, cp_sz);
			pmlmepriv->wps_probe_req_ie_len = cp_sz;					
			
		}	
		
		goto FREE_EXT;
		
	}

	if(	len >= WEXT_CSCAN_HEADER_SIZE
		&& _rtw_memcmp(ext, WEXT_CSCAN_HEADER, WEXT_CSCAN_HEADER_SIZE) == _TRUE
	){
		ret = rtw_wx_set_scan(dev, info, awrq, ext);
		goto FREE_EXT;
	}
	
#ifdef CONFIG_ANDROID
	//DBG_871X("rtw_wx_set_priv: %s req=%s\n", dev->name, ext);

	i = rtw_android_cmdstr_to_num(ext);

	switch(i) {
		case ANDROID_WIFI_CMD_START :
			indicate_wx_custom_event(padapter, "START");
			break;
		case ANDROID_WIFI_CMD_STOP :
			indicate_wx_custom_event(padapter, "STOP");
			break;
		case ANDROID_WIFI_CMD_RSSI :
			{
				struct	mlme_priv	*pmlmepriv = &(padapter->mlmepriv);	
				struct	wlan_network	*pcur_network = &pmlmepriv->cur_network;

				if(check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE) {
					sprintf(ext, "%s rssi %d", pcur_network->network.Ssid.Ssid, padapter->recvpriv.rssi);
				} else {
					sprintf(ext, "OK");
				}
			}
			break;
		case ANDROID_WIFI_CMD_LINKSPEED :
			{
				u16 mbps = rtw_get_cur_max_rate(padapter)/10;
				sprintf(ext, "LINKSPEED %d", mbps);
			}
			break;
		case ANDROID_WIFI_CMD_MACADDR :
			sprintf(ext, "MACADDR = " MAC_FMT, MAC_ARG(dev->dev_addr));
			break;
		case ANDROID_WIFI_CMD_SCAN_ACTIVE :
			{
				struct	mlme_priv	*pmlmepriv = &(padapter->mlmepriv);
				pmlmepriv->scan_mode=SCAN_ACTIVE;
				sprintf(ext, "OK");
			}
			break;
		case ANDROID_WIFI_CMD_SCAN_PASSIVE :
			{
				struct	mlme_priv	*pmlmepriv = &(padapter->mlmepriv);
				pmlmepriv->scan_mode=SCAN_PASSIVE;
				sprintf(ext, "OK");
			}
			break;

		case ANDROID_WIFI_CMD_COUNTRY :
			{
				char country_code[10];
				int channel_plan = RT_CHANNEL_DOMAIN_FCC;
				union iwreq_data wrqd;
				int ret_inner;
					
				sscanf(ext,"%*s %s",country_code);
		
				if(0 == strcmp(country_code, "US"))
					channel_plan = RT_CHANNEL_DOMAIN_FCC;
				else if(0 == strcmp(country_code, "EU"))
					channel_plan = RT_CHANNEL_DOMAIN_ETSI;
				else if(0 == strcmp(country_code, "JP"))
					channel_plan = RT_CHANNEL_DOMAIN_MKK;
				else if(0 == strcmp(country_code, "CN"))
					channel_plan = RT_CHANNEL_DOMAIN_CHINA;
				else
					DBG_871X("%s unknown country_code:%s, set to RT_CHANNEL_DOMAIN_FCC\n", __FUNCTION__, country_code);
				
				_rtw_memcpy(&wrqd, &channel_plan, sizeof(int));
				
				if( 0!=(ret_inner=rtw_wx_set_channel_plan(dev, info, &wrqd, extra)) ){
					DBG_871X("%s rtw_wx_set_channel_plan error\n", __FUNCTION__);
				}
				
				sprintf(ext, "OK");
			}
			break;
			
		default :
			#ifdef  CONFIG_DEBUG_RTW_WX_SET_PRIV
			DBG_871X("%s: %s unknowned req=%s\n", __FUNCTION__,
				dev->name, ext_dbg);
			#endif

			sprintf(ext, "OK");
		
	}

	if (copy_to_user(dwrq->pointer, ext, min(dwrq->length, (u16)(strlen(ext)+1)) ) )
		ret = -EFAULT;

	#ifdef CONFIG_DEBUG_RTW_WX_SET_PRIV
	DBG_871X("%s: %s req=%s rep=%s dwrq->length=%d, strlen(ext)+1=%d\n", __FUNCTION__,
		dev->name, ext_dbg ,ext, dwrq->length, (u16)(strlen(ext)+1));
	#endif
#endif //end of CONFIG_ANDROID


FREE_EXT:

	rtw_vmfree(ext, len);
	#ifdef CONFIG_DEBUG_RTW_WX_SET_PRIV
	rtw_vmfree(ext_dbg, len);
	#endif

	//DBG_871X("rtw_wx_set_priv: (SIOCSIWPRIV) %s ret=%d\n", 
	//		dev->name, ret);

	return ret;
	
}

static int rtw_pm_set_lps(_adapter *padapter, u8 mode)
{
	
	int	ret = 0;	
	
	struct pwrctrl_priv *pwrctrlpriv = &padapter->pwrctrlpriv;
	
	if ( mode < PS_MODE_NUM )
	{
		if(pwrctrlpriv->power_mgnt !=mode)
		{
			if(PS_MODE_ACTIVE == mode)
			{
				LeaveAllPowerSaveMode(padapter);
			}
			else
			{
				pwrctrlpriv->LpsIdleCount = 2;
			}
			pwrctrlpriv->power_mgnt = mode;
			pwrctrlpriv->bLeisurePs = (PS_MODE_ACTIVE != pwrctrlpriv->power_mgnt)?_TRUE:_FALSE;
		}
	}
	else
	{
		ret = -EINVAL;
	}

	return ret;
		
}

static int rtw_pm_set_ips(_adapter *padapter, u8 mode)
{	
	struct pwrctrl_priv *pwrctrlpriv = &padapter->pwrctrlpriv;	

	if( mode == IPS_NORMAL || mode == IPS_LEVEL_2 ) {
		rtw_ips_mode_req(pwrctrlpriv, mode);
		pwrctrlpriv->power_mgnt = PS_MODE_MIN;
		rtw_set_pwr_state_check_timer(pwrctrlpriv);
		DBG_871X("%s %s\n", __FUNCTION__, mode == IPS_NORMAL?"IPS_NORMAL":"IPS_LEVEL_2");
		return 0;
	} 
	else if(mode ==IPS_NONE){
		if(_FAIL == rfpwrstate_check(padapter))
		{
			return -EFAULT;
		}
		pwrctrlpriv->power_mgnt = PS_MODE_ACTIVE;
	}
	else {
		return -EINVAL;
	}
	return 0;
}

static int rtw_pm_set(struct net_device *dev,
                               struct iw_request_info *info,
                               union iwreq_data *wrqu, char *extra)
{
	int ret = 0;
	unsigned	mode = 0;

	
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);

	DBG_871X( "[%s] extra = %s\n", __FUNCTION__, extra );

	if ( _rtw_memcmp( extra, "lps=", 4 ) )
	{
		sscanf(extra+4, "%u", &mode);	
		ret = rtw_pm_set_lps(padapter,mode);
	}
	else if ( _rtw_memcmp( extra, "ips=", 4 ) )
	{
		sscanf(extra+4, "%u", &mode);
		ret = rtw_pm_set_ips(padapter,mode);
	}
	else{
		ret = -EINVAL;
	}

	return ret;
}

static int rtw_mp_efuse_get(struct net_device *dev,
			struct iw_request_info *info,
			union iwreq_data *wdata, char *extra)
{
	struct iw_point *wrqu;
	PADAPTER padapter;
	PHAL_DATA_TYPE pHalData;
	PEFUSE_HAL pEfuseHal;
	u8 ips_mode,lps_mode;
	struct pwrctrl_priv *pwrctrlpriv ;
	u8 *data = NULL;
	u8 *rawdata = NULL;
	char *pch, *ptmp, *token, *tmp[3]={0x00,0x00,0x00};
	u16 i=0, j=0, mapLen=0, addr=0, cnts=0;
	u16 max_available_size=0, raw_cursize=0, raw_maxsize=0;
	int err;


	wrqu = (struct iw_point*)wdata;
	padapter = rtw_netdev_priv(dev);
	pHalData = GET_HAL_DATA(padapter);
	pwrctrlpriv = &padapter->pwrctrlpriv; 
	pEfuseHal = &pHalData->EfuseHal;
	err = 0;
	data = _rtw_zmalloc(EFUSE_BT_MAX_MAP_LEN);
	if (data == NULL)
	{
		err = -ENOMEM;
		goto exit;
	}
	rawdata = _rtw_zmalloc(EFUSE_BT_MAX_MAP_LEN);
	if (rawdata == NULL)
	{
		err = -ENOMEM;
		goto exit;
	}

	if (copy_from_user(extra, wrqu->pointer, wrqu->length))
	{
		err = -EFAULT;
		goto exit;
	}
	#ifdef CONFIG_LPS
	lps_mode = pwrctrlpriv->power_mgnt;//keep org value
	rtw_pm_set_lps(padapter,PS_MODE_ACTIVE);
	#endif	
	
	#ifdef CONFIG_IPS	
	ips_mode = pwrctrlpriv->ips_mode;//keep org value
	rtw_pm_set_ips(padapter,IPS_NONE);
	#endif	
	
	pch = extra;
	DBG_871X("%s: in=%s\n", __FUNCTION__, extra);

	i = 0;
	//mac 16 "00e04c871200" rmap,00,2
	while ((token = strsep(&pch, ",")) != NULL)
	{
		if (i > 2) break;
		tmp[i] = token;
		i++;
	}

	if (strcmp(tmp[0], "realmap") == 0)
	{
		mapLen = EFUSE_MAP_SIZE;
		if (rtw_efuse_map_read(padapter, 0, mapLen, pEfuseHal->fakeEfuseInitMap) == _FAIL)
		{
			DBG_871X("%s: read realmap Fail!!\n", __FUNCTION__);
			err = -EFAULT;
			goto exit;
		}

//		DBG_871X("OFFSET\tVALUE(hex)\n");
		sprintf(extra, "\n");
		for (i = 0; i < EFUSE_MAP_SIZE; i += 16)
		{
//			DBG_871X("0x%02x\t", i);
			sprintf(extra, "%s0x%02x\t", extra, i);
			for (j=0; j<8; j++) {
//				DBG_871X("%02X ", data[i+j]);
				sprintf(extra, "%s%02X ", extra, pEfuseHal->fakeEfuseInitMap[i+j]);
			}
//			DBG_871X("\t");
			sprintf(extra, "%s\t", extra);
			for (; j<16; j++) {
//				DBG_871X("%02X ", data[i+j]);
				sprintf(extra, "%s%02X ", extra, pEfuseHal->fakeEfuseInitMap[i+j]);
			}
//			DBG_871X("\n");
			sprintf(extra,"%s\n",extra);
		}
//		DBG_871X("\n");
	}
	else if (strcmp(tmp[0], "rmap") == 0)
	{
		if ((tmp[1]==NULL) || (tmp[2]==NULL))
		{
			DBG_871X("%s: rmap Fail!! Parameters error!\n", __FUNCTION__);
			err = -EINVAL;
			goto exit;
		}

		// rmap addr cnts
		addr = simple_strtoul(tmp[1], &ptmp, 16);
		DBG_871X("%s: addr=%x\n", __FUNCTION__, addr);

		cnts = simple_strtoul(tmp[2], &ptmp, 10);
		if (cnts == 0)
		{
			DBG_871X("%s: rmap Fail!! cnts error!\n", __FUNCTION__);
			err = -EINVAL;
			goto exit;
		}
		DBG_871X("%s: cnts=%d\n", __FUNCTION__, cnts);

		EFUSE_GetEfuseDefinition(padapter, EFUSE_WIFI, TYPE_AVAILABLE_EFUSE_BYTES_TOTAL, (PVOID)&max_available_size, _FALSE);
		if ((addr + cnts) > max_available_size)
		{
			DBG_871X("%s: addr(0x%X)+cnts(%d) parameter error!\n", __FUNCTION__, addr, cnts);
			err = -EINVAL;
			goto exit;
		}

		if (rtw_efuse_map_read(padapter, addr, cnts, data) == _FAIL)
		{
			DBG_871X("%s: rtw_efuse_map_read error!\n", __FUNCTION__);
			err = -EFAULT;
			goto exit;
		}

//		DBG_871X("%s: data={", __FUNCTION__);
		*extra = 0;
		for (i=0; i<cnts; i++) {
//			DBG_871X("0x%02x ", data[i]);
			sprintf(extra, "%s0x%02X ", extra, data[i]);
		}
//		DBG_871X("}\n");
	}
	else if (strcmp(tmp[0], "realraw") == 0)
	{
		addr = 0;
		mapLen = EFUSE_MAX_SIZE;
		if (rtw_efuse_access(padapter, _FALSE, addr, mapLen, rawdata) == _FAIL)
		{
			DBG_871X("%s: rtw_efuse_access Fail!!\n", __FUNCTION__);
			err = -EFAULT;
			goto exit;
		}

//		DBG_871X("%s: realraw={\n", __FUNCTION__);
		sprintf(extra, "\n");
		for (i=0; i<mapLen; i++)
		{
//			DBG_871X("%02X", rawdata[i]);
			sprintf(extra, "%s%02X", extra, rawdata[i]);

			if ((i & 0xF) == 0xF) {
//				DBG_871X("\n");
				sprintf(extra, "%s\n", extra);
			}
			else if ((i & 0x7) == 0x7){
//				DBG_871X("\t");
				sprintf(extra, "%s\t", extra);
			} else {
//				DBG_871X(" ");
				sprintf(extra, "%s ", extra);
			}
		}
//		DBG_871X("}\n");
	}
	else if (strcmp(tmp[0], "mac") == 0)
	{
		#ifdef CONFIG_RTL8192C
		addr = 0x16; // EEPROM_MAC_ADDR
		#endif
		#ifdef CONFIG_RTL8192D
		addr = 0x19;
		#endif
		#ifdef CONFIG_RTL8723A
		#ifdef CONFIG_SDIO_HCI
		addr = EEPROM_MAC_ADDR_8723AS;
		#endif
		#ifdef CONFIG_USB_HCI
		addr = EEPROM_MAC_ADDR_8723AU;
		#endif
		#endif // CONFIG_RTL8723A
		cnts = 6;

		EFUSE_GetEfuseDefinition(padapter, EFUSE_WIFI, TYPE_AVAILABLE_EFUSE_BYTES_TOTAL, (PVOID)&max_available_size, _FALSE);
		if ((addr + cnts) > max_available_size) {
			DBG_871X("%s: addr(0x%02x)+cnts(%d) parameter error!\n", __FUNCTION__, addr, cnts);
			err = -EFAULT;
			goto exit;
		}

		if (rtw_efuse_map_read(padapter, addr, cnts, data) == _FAIL)
		{
			DBG_871X("%s: rtw_efuse_map_read error!\n", __FUNCTION__);
			err = -EFAULT;
			goto exit;
		}

//		DBG_871X("%s: MAC address={", __FUNCTION__);
		*extra = 0;
		for (i=0; i<cnts; i++)
		{
//			DBG_871X("%02X", data[i]);
			sprintf(extra, "%s%02X", extra, data[i]);
			if (i != (cnts-1))
			{
//				DBG_871X(":");
				sprintf(extra,"%s:",extra);
			}
		}
//		DBG_871X("}\n");
	}
	else if (strcmp(tmp[0], "vidpid") == 0)
	{
		#ifdef CONFIG_RTL8192C
		addr = 0x0a; // EEPROM_VID
		#endif
		#ifdef CONFIG_RTL8192D
		addr = 0x0c;
		#endif
		#ifdef CONFIG_RTL8723A
		addr = EEPROM_VID_8723AU;
		#endif
		cnts = 4;

		EFUSE_GetEfuseDefinition(padapter, EFUSE_WIFI, TYPE_AVAILABLE_EFUSE_BYTES_TOTAL, (PVOID)&max_available_size, _FALSE);
		if ((addr + cnts) > max_available_size)
		{
			DBG_871X("%s: addr(0x%02x)+cnts(%d) parameter error!\n", __FUNCTION__, addr, cnts);
			err = -EFAULT;
			goto exit;
		}
		if (rtw_efuse_map_read(padapter, addr, cnts, data) == _FAIL)
		{
			DBG_871X("%s: rtw_efuse_access error!!\n", __FUNCTION__);
			err = -EFAULT;
			goto exit;
		}

//		DBG_871X("%s: {VID,PID}={", __FUNCTION__);
		*extra = 0;
		for (i=0; i<cnts; i++)
		{
//			DBG_871X("0x%02x", data[i]);
			sprintf(extra, "%s0x%02X", extra, data[i]);
			if (i != (cnts-1))
			{
//				DBG_871X(",");
				sprintf(extra,"%s,",extra);
			}
		}
//		DBG_871X("}\n");
	}
	else if (strcmp(tmp[0], "ableraw") == 0)
	{
		efuse_GetCurrentSize(padapter,&raw_cursize);
		raw_maxsize = efuse_GetMaxSize(padapter);
		sprintf(extra, "[available raw size]= %d bytes", raw_maxsize-raw_cursize);
	}
	else if (strcmp(tmp[0], "btfmap") == 0)
	{
		mapLen = EFUSE_BT_MAX_MAP_LEN;
		if (rtw_BT_efuse_map_read(padapter, 0, mapLen, pEfuseHal->BTEfuseInitMap) == _FAIL)
		{
			DBG_871X("%s: rtw_BT_efuse_map_read Fail!!\n", __FUNCTION__);
			err = -EFAULT;
			goto exit;
		}

//		DBG_871X("OFFSET\tVALUE(hex)\n");
		sprintf(extra, "\n");
		for (i=0; i<512; i+=16) // set 512 because the iwpriv's extra size have limit 0x7FF
		{
//			DBG_871X("0x%03x\t", i);
			sprintf(extra, "%s0x%03x\t", extra, i);
			for (j=0; j<8; j++) {
//				DBG_871X("%02X ", pEfuseHal->BTEfuseInitMap[i+j]);
				sprintf(extra, "%s%02X ", extra, pEfuseHal->BTEfuseInitMap[i+j]);
			}
//			DBG_871X("\t");
			sprintf(extra,"%s\t",extra);
			for (; j<16; j++) {
//				DBG_871X("%02X ", pEfuseHal->BTEfuseInitMap[i+j]);
				sprintf(extra, "%s%02X ", extra, pEfuseHal->BTEfuseInitMap[i+j]);
			}
//			DBG_871X("\n");
			sprintf(extra, "%s\n", extra);
		}
//		DBG_871X("\n");
	}
	else if (strcmp(tmp[0],"btbmap") == 0)
	{
		mapLen = EFUSE_BT_MAX_MAP_LEN;
		if (rtw_BT_efuse_map_read(padapter, 0, mapLen, pEfuseHal->BTEfuseInitMap) == _FAIL)
		{
			DBG_871X("%s: rtw_BT_efuse_map_read Fail!!\n", __FUNCTION__);
			err = -EFAULT;
			goto exit;
		}

//		DBG_871X("OFFSET\tVALUE(hex)\n");
		sprintf(extra, "\n");
		for (i=512; i<1024 ; i+=16)
		{
//			DBG_871X("0x%03x\t", i);
			sprintf(extra, "%s0x%03x\t", extra, i);
			for (j=0; j<8; j++)
			{
//				DBG_871X("%02X ", data[i+j]);
				sprintf(extra, "%s%02X ", extra, pEfuseHal->BTEfuseInitMap[i+j]);
			}
//			DBG_871X("\t");
			sprintf(extra,"%s\t",extra);
			for (; j<16; j++) {
//				DBG_871X("%02X ", data[i+j]);
				sprintf(extra, "%s%02X ", extra, pEfuseHal->BTEfuseInitMap[i+j]);
			}
//			DBG_871X("\n");
			sprintf(extra, "%s\n", extra);
		}
//		DBG_871X("\n");
	}
	else if (strcmp(tmp[0],"btrmap") == 0)
	{
		if ((tmp[1]==NULL) || (tmp[2]==NULL))
		{
			err = -EINVAL;
			goto exit;
		}

		// rmap addr cnts
		addr = simple_strtoul(tmp[1], &ptmp, 16);
		DBG_871X("%s: addr=0x%X\n", __FUNCTION__, addr);

		cnts = simple_strtoul(tmp[2], &ptmp, 10);
		if (cnts == 0)
		{
			DBG_871X("%s: btrmap Fail!! cnts error!\n", __FUNCTION__);
			err = -EINVAL;
			goto exit;
		}
		DBG_871X("%s: cnts=%d\n", __FUNCTION__, cnts);

		EFUSE_GetEfuseDefinition(padapter, EFUSE_BT, TYPE_AVAILABLE_EFUSE_BYTES_TOTAL, (PVOID)&max_available_size, _FALSE);
		if ((addr + cnts) > max_available_size)
		{
			DBG_871X("%s: addr(0x%X)+cnts(%d) parameter error!\n", __FUNCTION__, addr, cnts);
			err = -EFAULT;
			goto exit;
		}

		if (rtw_BT_efuse_map_read(padapter, addr, cnts, data) == _FAIL) 
		{
			DBG_871X("%s: rtw_BT_efuse_map_read error!!\n", __FUNCTION__);
			err = -EFAULT;
			goto exit;
		}

		*extra = 0;
//		DBG_871X("%s: bt efuse data={", __FUNCTION__);
		for (i=0; i<cnts; i++)
		{
//			DBG_871X("0x%02x ", data[i]);
			sprintf(extra, "%s 0x%02X ", extra, data[i]);
		}
//		DBG_871X("}\n");
	}
	else if (strcmp(tmp[0], "btffake") == 0)
	{
//		DBG_871X("OFFSET\tVALUE(hex)\n");
		sprintf(extra, "\n");
		for (i=0; i<512; i+=16)
		{
//			DBG_871X("0x%03x\t", i);
			sprintf(extra, "%s0x%03x\t", extra, i);
			for (j=0; j<8; j++) {
//				DBG_871X("%02X ", pEfuseHal->fakeBTEfuseModifiedMap[i+j]);
				sprintf(extra, "%s%02X ", extra, pEfuseHal->fakeBTEfuseModifiedMap[i+j]);
			}
//			DBG_871X("\t");
			sprintf(extra, "%s\t", extra);
			for (; j<16; j++) {
//				DBG_871X("%02X ", pEfuseHal->fakeBTEfuseModifiedMap[i+j]);
				sprintf(extra, "%s%02X ", extra, pEfuseHal->fakeBTEfuseModifiedMap[i+j]);
			}
//			DBG_871X("\n");
			sprintf(extra, "%s\n", extra);
		}
//		DBG_871X("\n");
	}
	else if (strcmp(tmp[0],"btbfake") == 0)
	{
//		DBG_871X("OFFSET\tVALUE(hex)\n");
		sprintf(extra, "\n");
		for (i=512; i<1024; i+=16)
		{
//			DBG_871X("0x%03x\t", i);
			sprintf(extra, "%s0x%03x\t", extra, i);
			for (j=0; j<8; j++) {
//				DBG_871X("%02X ", pEfuseHal->fakeBTEfuseModifiedMap[i+j]);
				sprintf(extra, "%s%02X ", extra, pEfuseHal->fakeBTEfuseModifiedMap[i+j]);
			}
//			DBG_871X("\t");
			sprintf(extra, "%s\t", extra);
			for (; j<16; j++) {
//				DBG_871X("%02X ", pEfuseHal->fakeBTEfuseModifiedMap[i+j]);
				sprintf(extra, "%s%02X ", extra, pEfuseHal->fakeBTEfuseModifiedMap[i+j]);
			}
//			DBG_871X("\n");
			sprintf(extra, "%s\n", extra);
		}
//		DBG_871X("\n");
	}
	else if (strcmp(tmp[0],"wlrfkmap")== 0)
	{
//		DBG_871X("OFFSET\tVALUE(hex)\n");
		sprintf(extra, "\n");
		for (i=0; i<EFUSE_MAP_SIZE; i+=16)
		{
//			DBG_871X("\t0x%02x\t", i);
			sprintf(extra, "%s0x%02x\t", extra, i);
			for (j=0; j<8; j++) {
//				DBG_871X("%02X ", pEfuseHal->fakeEfuseModifiedMap[i+j]);
				sprintf(extra, "%s%02X ", extra, pEfuseHal->fakeEfuseModifiedMap[i+j]);
			}
//			DBG_871X("\t");
			sprintf(extra, "%s\t", extra);
			for (; j<16; j++) {
//				DBG_871X("%02X ", pEfuseHal->fakeEfuseModifiedMap[i+j]);
				sprintf(extra, "%s %02X", extra, pEfuseHal->fakeEfuseModifiedMap[i+j]);
			}
//			DBG_871X("\n");
			sprintf(extra, "%s\n", extra);
		}
//		DBG_871X("\n");
	}
	else
	{
		 sprintf(extra, "Command not found!");
	}

exit:
	if (data)
		_rtw_mfree(data, EFUSE_BT_MAX_MAP_LEN);
	if (rawdata)
		_rtw_mfree(rawdata, EFUSE_BT_MAX_MAP_LEN);
	if (!err)
		wrqu->length = strlen(extra);
	
	#ifdef CONFIG_IPS		
	rtw_pm_set_ips(padapter, ips_mode);
	#endif
	#ifdef CONFIG_LPS	
	rtw_pm_set_lps(padapter, lps_mode);
	#endif
	
	return err;
}

static int rtw_mp_efuse_set(struct net_device *dev,
			struct iw_request_info *info,
			union iwreq_data *wdata, char *extra)
{
	struct iw_point *wrqu;
	PADAPTER padapter;
	struct pwrctrl_priv *pwrctrlpriv ;
	PHAL_DATA_TYPE pHalData;
	PEFUSE_HAL pEfuseHal;

	u8 ips_mode,lps_mode;
	u32 i, jj, kk;
	u8 *setdata = NULL;
	u8 *ShadowMapBT = NULL;
	u8 *ShadowMapWiFi = NULL;
	u8 *setrawdata = NULL;
	char *pch, *ptmp, *token, *tmp[3]={0x00,0x00,0x00};
	u16 addr=0, cnts=0, max_available_size=0;
	int err;


	wrqu = (struct iw_point*)wdata;
	padapter = rtw_netdev_priv(dev);
	pwrctrlpriv = &padapter->pwrctrlpriv; 
	pHalData = GET_HAL_DATA(padapter);
	pEfuseHal = &pHalData->EfuseHal;
	err = 0;
	setdata = _rtw_zmalloc(1024);
	if (setdata == NULL)
	{
		err = -ENOMEM;
		goto exit;
	}
	ShadowMapBT = _rtw_malloc(EFUSE_BT_MAX_MAP_LEN);
	if (ShadowMapBT == NULL)
	{
		err = -ENOMEM;
		goto exit;
	}
	ShadowMapWiFi = _rtw_malloc(EFUSE_MAP_SIZE);
	if (ShadowMapWiFi == NULL)
	{
		err = -ENOMEM;
		goto exit;
	}
	setrawdata = _rtw_malloc(EFUSE_MAX_SIZE);
	if (setrawdata == NULL)
	{
		err = -ENOMEM;
		goto exit;
	}

	#ifdef CONFIG_LPS
	lps_mode = pwrctrlpriv->power_mgnt;//keep org value
	rtw_pm_set_lps(padapter,PS_MODE_ACTIVE);
	#endif	
	
	#ifdef CONFIG_IPS	
	ips_mode = pwrctrlpriv->ips_mode;//keep org value
	rtw_pm_set_ips(padapter,IPS_NONE);
	#endif	

	pch = extra;
	DBG_871X("%s: in=%s\n", __FUNCTION__, extra);
	
	i = 0;
	while ((token = strsep(&pch, ",")) != NULL)
	{
		if (i > 2) break;
		tmp[i] = token;
		i++;
	}

	// tmp[0],[1],[2]
	// wmap,addr,00e04c871200
	if (strcmp(tmp[0], "wmap") == 0)
	{
		if ((tmp[1]==NULL) || (tmp[2]==NULL))
		{
			err = -EINVAL;
			goto exit;
		}

		addr = simple_strtoul(tmp[1], &ptmp, 16);
		addr &= 0xFF;

		cnts = strlen(tmp[2]);
		if (cnts%2)
		{
			err = -EINVAL;
			goto exit;
		}
		cnts /= 2;
		if (cnts == 0)
		{
			err = -EINVAL;
			goto exit;
		}

		DBG_871X("%s: addr=0x%X\n", __FUNCTION__, addr);
		DBG_871X("%s: cnts=%d\n", __FUNCTION__, cnts);
		DBG_871X("%s: map data=%s\n", __FUNCTION__, tmp[2]);
				
		for (jj=0, kk=0; jj<cnts; jj++, kk+=2)
		{
			setdata[jj] = key_2char2num(tmp[2][kk], tmp[2][kk+1]);
		}

		EFUSE_GetEfuseDefinition(padapter, EFUSE_WIFI, TYPE_AVAILABLE_EFUSE_BYTES_TOTAL, (PVOID)&max_available_size, _FALSE);
		if ((addr+cnts) > max_available_size)
		{
			DBG_871X("%s: addr(0x%X)+cnts(%d) parameter error!\n", __FUNCTION__, addr, cnts);
			err = -EFAULT;
			goto exit;
		}

		if (rtw_efuse_map_write(padapter, addr, cnts, setdata) == _FAIL)
		{
			DBG_871X("%s: rtw_efuse_map_write error!!\n", __FUNCTION__);
			err = -EFAULT;
			goto exit;
		}
	}
	else if (strcmp(tmp[0], "wraw") == 0)
	{
		if ((tmp[1]==NULL) || (tmp[2]==NULL))
		{
			err = -EINVAL;
			goto exit;
		}

		addr = simple_strtoul( tmp[1], &ptmp, 16 );
		addr &= 0xFFF;

		cnts = strlen(tmp[2]);
		if (cnts%2)
		{
			err = -EINVAL;
			goto exit;
		}
		cnts /= 2;
		if (cnts == 0)
		{
			err = -EINVAL;
			goto exit;
		}

		DBG_871X("%s: addr=0x%X\n", __FUNCTION__, addr);
		DBG_871X("%s: cnts=%d\n", __FUNCTION__, cnts);
		DBG_871X("%s: raw data=%s\n", __FUNCTION__, tmp[2]);

		for (jj=0, kk=0; jj<cnts; jj++, kk+=2)
		{
			setrawdata[jj] = key_2char2num(tmp[2][kk], tmp[2][kk+1]);
		}

		if (rtw_efuse_access(padapter, _TRUE, addr, cnts, setrawdata) == _FAIL)
		{
			DBG_871X("%s: rtw_efuse_access error!!\n", __FUNCTION__);
			err = -EFAULT;
			goto exit;
		}
	}
	else if (strcmp(tmp[0], "mac") == 0)
	{
		if (tmp[1]==NULL)
		{
			err = -EINVAL;
			goto exit;
		}

		//mac,00e04c871200
		#ifdef CONFIG_RTL8192C
		addr = 0x16;
		#endif
		#ifdef CONFIG_RTL8192D
		addr = 0x19;
		#endif
		#ifdef CONFIG_RTL8723A
		#ifdef CONFIG_SDIO_HCI
		addr = EEPROM_MAC_ADDR_8723AS;
		#endif
		#ifdef CONFIG_USB_HCI
		addr = EEPROM_MAC_ADDR_8723AU;
		#endif
		#endif // CONFIG_RTL8723A

		cnts = strlen(tmp[1]);
		if (cnts%2)
		{
			err = -EINVAL;
			goto exit;
		}
		cnts /= 2;
		if (cnts == 0)
		{
			err = -EINVAL;
			goto exit;
		}
		if (cnts > 6)
		{
			DBG_871X("%s: error data for mac addr=\"%s\"\n", __FUNCTION__, tmp[1]);
			err = -EFAULT;
			goto exit;
		}

		DBG_871X("%s: addr=0x%X\n", __FUNCTION__, addr);
		DBG_871X("%s: cnts=%d\n", __FUNCTION__, cnts);
		DBG_871X("%s: MAC address=%s\n", __FUNCTION__, tmp[1]);

		for (jj=0, kk=0; jj<cnts; jj++, kk+=2)
		{
			setdata[jj] = key_2char2num(tmp[1][kk], tmp[1][kk+1]);
		}

		EFUSE_GetEfuseDefinition(padapter, EFUSE_WIFI, TYPE_AVAILABLE_EFUSE_BYTES_TOTAL, (PVOID)&max_available_size, _FALSE);
		if ((addr+cnts) > max_available_size)
		{
			DBG_871X("%s: addr(0x%X)+cnts(%d) parameter error!\n", __FUNCTION__, addr, cnts);
			err = -EFAULT;
			goto exit;
		}

		if (rtw_efuse_map_write(padapter, addr, cnts, setdata) == _FAIL)
		{
			DBG_871X("%s: rtw_efuse_map_write error!!\n", __FUNCTION__);
			err = -EFAULT;
			goto exit;
		}
	}
	else if (strcmp(tmp[0], "vidpid") == 0)
	{
		if (tmp[1]==NULL)
		{
			err = -EINVAL;
			goto exit;
		}

		// pidvid,da0b7881
		#ifdef CONFIG_RTL8192C
		addr = 0x0a;
		#endif
		#ifdef CONFIG_RTL8192D
		addr = 0x0c;
		#endif
		#ifdef CONFIG_RTL8723A
		addr = EEPROM_VID_8723AU;
		#endif

		cnts = strlen(tmp[1]);
		if (cnts%2)
		{
			err = -EINVAL;
			goto exit;
		}
		cnts /= 2;
		if (cnts == 0)
		{
			err = -EINVAL;
			goto exit;
		}

		DBG_871X("%s: addr=0x%X\n", __FUNCTION__, addr);
		DBG_871X("%s: cnts=%d\n", __FUNCTION__, cnts);
		DBG_871X("%s: VID/PID=%s\n", __FUNCTION__, tmp[1]);

		for (jj=0, kk=0; jj<cnts; jj++, kk+=2)
		{
			setdata[jj] = key_2char2num(tmp[1][kk], tmp[1][kk+1]);
		}

		EFUSE_GetEfuseDefinition(padapter, EFUSE_WIFI, TYPE_AVAILABLE_EFUSE_BYTES_TOTAL, (PVOID)&max_available_size, _FALSE);
		if ((addr+cnts) > max_available_size)
		{
			DBG_871X("%s: addr(0x%X)+cnts(%d) parameter error!\n", __FUNCTION__, addr, cnts);
			err = -EFAULT;
			goto exit;
		}

		if (rtw_efuse_map_write(padapter, addr, cnts, setdata) == _FAIL)
		{
			DBG_871X("%s: rtw_efuse_map_write error!!\n", __FUNCTION__);
			err = -EFAULT;
			goto exit;
		}
	}
	else if (strcmp(tmp[0], "btwmap") == 0)
	{
		if ((tmp[1]==NULL) || (tmp[2]==NULL))
		{
			err = -EINVAL;
			goto exit;
		}

		addr = simple_strtoul(tmp[1], &ptmp, 16);
		addr &= 0xFFF;

		cnts = strlen(tmp[2]);
		if (cnts%2)
		{
			err = -EINVAL;
			goto exit;
		}
		cnts /= 2;
		if (cnts == 0)
		{
			err = -EINVAL;
			goto exit;
		}

		DBG_871X("%s: addr=0x%X\n", __FUNCTION__, addr);
		DBG_871X("%s: cnts=%d\n", __FUNCTION__, cnts);
		DBG_871X("%s: BT data=%s\n", __FUNCTION__, tmp[2]);

		for (jj=0, kk=0; jj<cnts; jj++, kk+=2)
		{
			setdata[jj] = key_2char2num(tmp[2][kk], tmp[2][kk+1]);
		}

		EFUSE_GetEfuseDefinition(padapter, EFUSE_BT, TYPE_AVAILABLE_EFUSE_BYTES_TOTAL, (PVOID)&max_available_size, _FALSE);
		if ((addr+cnts) > max_available_size)
		{
			DBG_871X("%s: addr(0x%X)+cnts(%d) parameter error!\n", __FUNCTION__, addr, cnts);
			err = -EFAULT;
			goto exit;
		}

		if (rtw_BT_efuse_map_write(padapter, addr, cnts, setdata) == _FAIL)
		{
			DBG_871X("%s: rtw_BT_efuse_map_write error!!\n", __FUNCTION__);
			err = -EFAULT;
			goto exit;
		}
	}
	else if (strcmp(tmp[0], "btwfake") == 0)
	{
		if ((tmp[1]==NULL) || (tmp[2]==NULL))
		{
			err = -EINVAL;
			goto exit;
		}

		addr = simple_strtoul(tmp[1], &ptmp, 16);
		addr &= 0xFFF;

		cnts = strlen(tmp[2]);
		if (cnts%2)
		{
			err = -EINVAL;
			goto exit;
		}
		cnts /= 2;
		if (cnts == 0)
		{
			err = -EINVAL;
			goto exit;
		}

		DBG_871X("%s: addr=0x%X\n", __FUNCTION__, addr);
		DBG_871X("%s: cnts=%d\n", __FUNCTION__, cnts);
		DBG_871X("%s: BT tmp data=%s\n", __FUNCTION__, tmp[2]);
				
		for (jj=0, kk=0; jj<cnts; jj++, kk+=2)
		{
			pEfuseHal->fakeBTEfuseModifiedMap[addr+jj] = key_2char2num(tmp[2][kk], tmp[2][kk+1]);
		}
	}
	else if (strcmp(tmp[0], "btdumpfake") == 0)
	{
		if (rtw_BT_efuse_map_read(padapter, 0, EFUSE_BT_MAX_MAP_LEN, pEfuseHal->fakeBTEfuseModifiedMap) == _SUCCESS) {
			DBG_871X("%s: BT read all map success\n", __FUNCTION__);
		} else {
			DBG_871X("%s: BT read all map Fail!\n", __FUNCTION__);
			err = -EFAULT;
		}
	}
	else if (strcmp(tmp[0], "wldumpfake") == 0)
	{
		if (rtw_efuse_map_read(padapter, 0, EFUSE_BT_MAX_MAP_LEN,  pEfuseHal->fakeEfuseModifiedMap) == _SUCCESS) {
			DBG_871X("%s: BT read all map success \n", __FUNCTION__); 
		} else {
			DBG_871X("%s: BT read all map  Fail \n", __FUNCTION__);
			err = -EFAULT;
		}
	}
	else if (strcmp(tmp[0], "btfk2map") == 0)
	{
		_rtw_memcpy(pEfuseHal->BTEfuseModifiedMap, pEfuseHal->fakeBTEfuseModifiedMap, EFUSE_BT_MAX_MAP_LEN);
			
		EFUSE_GetEfuseDefinition(padapter, EFUSE_BT, TYPE_AVAILABLE_EFUSE_BYTES_TOTAL, (PVOID)&max_available_size, _FALSE);	
		if (max_available_size < 1)
		{
			err = -EFAULT;
			goto exit;
		}

		if (rtw_BT_efuse_map_write(padapter, 0x00, EFUSE_BT_MAX_MAP_LEN, pEfuseHal->fakeBTEfuseModifiedMap) == _FAIL)
		{
			DBG_871X("%s: rtw_BT_efuse_map_write error!\n", __FUNCTION__);
			err = -EFAULT;
			goto exit;
		}
	}
	else if (strcmp(tmp[0], "wlfk2map") == 0)
	{
		EFUSE_GetEfuseDefinition(padapter, EFUSE_WIFI, TYPE_AVAILABLE_EFUSE_BYTES_TOTAL, (PVOID)&max_available_size, _FALSE);					
		if (max_available_size < 1)
		{
			err = -EFAULT;
			goto exit;
		}

		if (rtw_efuse_map_write(padapter, 0x00, EFUSE_MAX_MAP_LEN, pEfuseHal->fakeEfuseModifiedMap) == _FAIL)
		{
			DBG_871X("%s: rtw_efuse_map_write error!\n", __FUNCTION__);
			err = -EFAULT;
			goto exit;
		}
	}
	else if (strcmp(tmp[0], "wlwfake") == 0)
	{
		if ((tmp[1]==NULL) || (tmp[2]==NULL))
		{
			err = -EINVAL;
			goto exit;
		}

		addr = simple_strtoul(tmp[1], &ptmp, 16);
		addr &= 0xFFF;

		cnts = strlen(tmp[2]);
		if (cnts%2)
		{
			err = -EINVAL;
			goto exit;
		}
		cnts /= 2;
		if (cnts == 0)
		{
			err = -EINVAL;
			goto exit;
		}

		DBG_871X("%s: addr=0x%X\n", __FUNCTION__, addr);
		DBG_871X("%s: cnts=%d\n", __FUNCTION__, cnts);
		DBG_871X("%s: map tmp data=%s\n", __FUNCTION__, tmp[2]);

		for (jj=0, kk=0; jj<cnts; jj++, kk+=2)
		{
			pEfuseHal->fakeEfuseModifiedMap[addr+jj] = key_2char2num(tmp[2][kk], tmp[2][kk+1]);
		}
	}

exit:
	if (setdata)
		_rtw_mfree(setdata, 1024);
	if (ShadowMapBT)
		_rtw_mfree(ShadowMapBT, EFUSE_BT_MAX_MAP_LEN);
	if (ShadowMapWiFi)
		_rtw_mfree(ShadowMapWiFi, EFUSE_MAP_SIZE);
	if (setrawdata)
		_rtw_mfree(setrawdata, EFUSE_MAX_SIZE);
	
	#ifdef CONFIG_IPS		
	rtw_pm_set_ips(padapter, ips_mode);
	#endif
	#ifdef CONFIG_LPS	
	rtw_pm_set_lps(padapter, lps_mode);
	#endif

	return err;
}

#if defined(CONFIG_MP_INCLUDED) && defined(CONFIG_MP_IWPRIV_SUPPORT)
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
	PADAPTER padapter = rtw_netdev_priv(dev);


	pch = extra;
	pnext = strpbrk(pch, " ,.-");
	if (pnext == NULL) return -EINVAL;
	*pnext = 0;
	width_str = pch;

	pch = pnext + 1;
	pnext = strpbrk(pch, " ,.-");
	if (pnext == NULL) return -EINVAL;
	*pnext = 0;
	addr = simple_strtoul(pch, &ptmp, 16);
	if (addr > 0x3FFF) return -EINVAL;

	pch = pnext + 1;
	if ((pch - extra) >= wrqu->length) return -EINVAL;
	data = simple_strtoul(pch, &ptmp, 16);

	ret = 0;
	width = width_str[0];
	switch (width) {
		case 'b':
			// 1 byte
			if (data > 0xFF) {
				ret = -EINVAL;
				break;
			}
			rtw_write8(padapter, addr, data);
			break;
		case 'w':
			// 2 bytes
			if (data > 0xFFFF) {
				ret = -EINVAL;
				break;
			}
			rtw_write16(padapter, addr, data);
			break;
		case 'd':
			// 4 bytes
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
	char input[wrqu->length];
	char *pch, *pnext, *ptmp;
	char *width_str;
	char width;
	char data[20],tmp[20];
	u32 addr;
	//u32 *data = (u32*)extra;
	u32 ret, i=0, j=0, strtout=0;
	PADAPTER padapter = rtw_netdev_priv(dev);


	if (wrqu->length > 128) 
		return -EFAULT;

	if (copy_from_user(input, wrqu->pointer, wrqu->length))
		return -EFAULT;

	_rtw_memset(data, 0, 20);
	_rtw_memset(tmp, 0, 20);

	pch = input;
	pnext = strpbrk(pch, " ,.-");
	if (pnext == NULL) return -EINVAL;
	*pnext = 0;
	width_str = pch;

	pch = pnext + 1;
	if ((pch - input) >= wrqu->length) return -EINVAL;
	
	addr = simple_strtoul(pch, &ptmp, 16);
	if (addr > 0x3FFF) return -EINVAL;

	ret = 0;
	width = width_str[0];
	switch (width) {
		case 'b':
			// 1 byte
			// *(u8*)data = rtw_read8(padapter, addr);
			sprintf(extra, "%x\n",  rtw_read8(padapter, addr));
			wrqu->length = 4;
			break;
		case 'w':
			// 2 bytes
			//*(u16*)data = rtw_read16(padapter, addr);
			sprintf(data, "%04x\n", rtw_read16(padapter, addr));
			for( i=0 ; i <= strlen(data) ; i++)
				{
					  if( i%2==0 )
					  {
						   tmp[j]=' ';
						   j++;
					  }
					  if ( data[i] != '\0' )
					 	 tmp[j] = data[i];
					 	
					  	 j++;
				}
				pch = tmp;		
				DBG_871X("pch=%s",pch);
				
				while( *pch != '\0' )
				{
					pnext = strpbrk(pch, " ");
					if (!pnext)
						break;
					
					pnext++;
					if ( *pnext != '\0' )
					{
						  strtout = simple_strtoul (pnext , &ptmp, 16);
						  sprintf( extra, "%s %d" ,extra ,strtout );
					}
					else{
						  break;
					}
					pch = pnext;
				}
			wrqu->length = 8;
			break;
		case 'd':
			// 4 bytes
			//*data = rtw_read32(padapter, addr);
			sprintf(data, "%08x", rtw_read32(padapter, addr));
				//add read data format blank
				for( i=0 ; i <= strlen(data) ; i++)
				{
					  if( i%2==0 )
					  {
						   tmp[j]=' ';
						   j++;
					  }
					  if ( data[i] != '\0' )
					  tmp[j] = data[i];
					  
					  j++;
				}
				pch = tmp;		
				DBG_871X("pch=%s",pch);
				
				while( *pch != '\0' )
				{
					pnext = strpbrk(pch, " ");
					if (!pnext)
						break;
					
					pnext++;
					if ( *pnext != '\0' )
					{
						  strtout = simple_strtoul (pnext , &ptmp, 16);
						  sprintf( extra, "%s %d" ,extra ,strtout );
					}
					else{
			break;
					}
					pch = pnext;
				}
			wrqu->length = 20;	
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
 *	%d is RF path, should be smaller than MAX_RF_PATH_NUMS
 *	1st %x is address(offset)
 *	2st %x is data to write
 */
 static int rtw_mp_write_rf(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_point *wrqu, char *extra)
{			
/*static int rtw_mp_write_rf(struct net_device *dev,
			struct iw_request_info *info,
			union iwreq_data *wrqu, char *extra)
*/
	u32 path, addr, data;
	int ret;
	PADAPTER padapter = rtw_netdev_priv(dev);


	ret = sscanf(extra, "%d,%x,%x", &path, &addr, &data);
	if (ret < 3) return -EINVAL;

	if (path >= MAX_RF_PATH_NUMS) return -EINVAL;
	if (addr > 0xFF) return -EINVAL;
	if (data > 0xFFFFF) return -EINVAL;

	write_rfreg(padapter, path, addr, data);

	return 0;
}

/*
 * Input Format: %d,%x
 *	%d is RF path, should be smaller than MAX_RF_PATH_NUMS
 *	%x is address(offset)
 *
 * Return:
 *	%d for data readed
 */
static int rtw_mp_read_rf(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_point *wrqu, char *extra)
{
	char input[wrqu->length];
	char *pch, *pnext, *ptmp;
	char data[20],tmp[20];
	//u32 *data = (u32*)extra;
	u32 path, addr;
	u32 ret,i=0 ,j=0,strtou=0;
	PADAPTER padapter = rtw_netdev_priv(dev);


	if (wrqu->length > 128) return -EFAULT;
	if (copy_from_user(input, wrqu->pointer, wrqu->length))
		return -EFAULT;

	ret = sscanf(input, "%d,%x", &path, &addr);
	if (ret < 2) return -EINVAL;

	if (path >= MAX_RF_PATH_NUMS) return -EINVAL;
	if (addr > 0xFF) return -EINVAL;

	//*data = read_rfreg(padapter, path, addr);
	sprintf(data, "%08x", read_rfreg(padapter, path, addr));
				//add read data format blank
				for( i=0 ; i <= strlen(data) ; i++)
				{
					  if( i%2==0 )
					  {
						   tmp[j]=' ';
						   j++;
					  }
					  tmp[j] = data[i];
					  j++;
				}
				pch = tmp;		
				DBG_871X("pch=%s",pch);
				
				while( *pch != '\0' )
				{
					pnext = strpbrk(pch, " ");
					pnext++;
					if ( *pnext != '\0' )
					{
						  strtou = simple_strtoul (pnext , &ptmp, 16);
						  sprintf( extra, "%s %d" ,extra ,strtou );
					}
					else{
						  break;
					}
					pch = pnext;
				}
			wrqu->length = 10;	

	return 0;
}

static int rtw_mp_start(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_point *wrqu, char *extra)
{
	u8 val8;
	PADAPTER padapter = rtw_netdev_priv(dev);


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
	PADAPTER padapter = rtw_netdev_priv(dev);


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
	u8 		input[wrqu->length];
	PADAPTER padapter = rtw_netdev_priv(dev);

	if (copy_from_user(input, wrqu->pointer, wrqu->length))
			return -EFAULT;
			
	rate = rtw_atoi(input);
	sprintf( extra, "Set data rate to %d" , rate );
		
	if(rate <= 0x7f)
		rate = wifirate2_ratetbl_inx( (u8)rate);	
	else 
		rate =(rate-0x80+MPT_RATE_MCS0);

	//DBG_871X("%s: rate=%d\n", __func__, rate);
	
	if (rate >= MPT_RATE_LAST )	
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

	PADAPTER padapter = rtw_netdev_priv(dev);
	u8 		input[wrqu->length];
	u32 	channel = 1;

	if (copy_from_user(input, wrqu->pointer, wrqu->length))
			return -EFAULT;
	
	channel = rtw_atoi(input);
	//DBG_871X("%s: channel=%d\n", __func__, channel);
	sprintf( extra, "Change channel %d to channel %d", padapter->mppriv.channel , channel );

	padapter->mppriv.channel = channel;
	Hal_SetChannel(padapter);

	wrqu->length = strlen(extra) + 1;
	return 0;
}

static int rtw_mp_bandwidth(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_point *wrqu, char *extra)
{
	u32 bandwidth=0, sg=0;
	//u8 buffer[40];
	PADAPTER padapter = rtw_netdev_priv(dev);
	//if (copy_from_user(buffer, (void*)wrqu->data.pointer, wrqu->data.length))
    //            return -EFAULT;
                
	//DBG_871X("%s:iwpriv in=%s\n", __func__, extra);
	
	sscanf(extra, "40M=%d,shortGI=%d", &bandwidth, &sg);
	
	if (bandwidth != HT_CHANNEL_WIDTH_40)
		bandwidth = HT_CHANNEL_WIDTH_20;

	//DBG_871X("%s: bw=%d sg=%d \n", __func__, bandwidth , sg);
	
	padapter->mppriv.bandwidth = (u8)bandwidth;
	padapter->mppriv.preamble = sg;
	
	SetBandwidth(padapter);

	return 0;
}

static int rtw_mp_txpower(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_point *wrqu, char *extra)
{
	u32		idx_a=0,idx_b=0;
	u8 		input[wrqu->length];

	PADAPTER padapter = rtw_netdev_priv(dev);


	if (copy_from_user(input, wrqu->pointer, wrqu->length))
			return -EFAULT;

	sscanf(input,"patha=%d,pathb=%d",&idx_a,&idx_b);
	//DBG_871X("%s: tx_pwr_idx_a=%x b=%x\n", __func__, idx_a, idx_b);

	sprintf( extra, "Set power level path_A:%d path_B:%d", idx_a , idx_b );
	padapter->mppriv.txpoweridx = (u8)idx_a;
	padapter->mppriv.txpoweridx_b = (u8)idx_b;
	
	Hal_SetAntennaPathPower(padapter);
	
	wrqu->length = strlen(extra) + 1;
	return 0;
}

static int rtw_mp_ant_tx(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_point *wrqu, char *extra)
{
	u8 i;
	u8 		input[wrqu->length];
	u16 antenna = 0;
	PADAPTER padapter = rtw_netdev_priv(dev);

	if (copy_from_user(input, wrqu->pointer, wrqu->length))
			return -EFAULT;
			
	DBG_871X("%s: input=%s\n", __func__, input);	
	
	sprintf( extra, "switch Tx antenna to %s", input );
	
	for (i=0; i < strlen(input); i++)
	{
		switch(input[i])
			{
				case 'a' :
								antenna|=ANTENNA_A;
								break;
				case 'b':
								antenna|=ANTENNA_B;
								break;
			}
	}
	//antenna |= BIT(extra[i]-'a');
	DBG_871X("%s: antenna=0x%x\n", __func__, antenna);		
	padapter->mppriv.antenna_tx = antenna;
	DBG_871X("%s:mppriv.antenna_rx=%d\n", __func__, padapter->mppriv.antenna_tx);
	
	Hal_SetAntenna(padapter);

	wrqu->length = strlen(extra) + 1;
	return 0;
}

static int rtw_mp_ant_rx(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_point *wrqu, char *extra)
{
	u8 i;
	u16 antenna = 0;
	u8 		input[wrqu->length];
	PADAPTER padapter = rtw_netdev_priv(dev);

	if (copy_from_user(input, wrqu->pointer, wrqu->length))
			return -EFAULT;
	//DBG_871X("%s: input=%s\n", __func__, input);

	for (i=0; i < strlen(input); i++) {
	
	switch( input[i] )
			{
				case 'a' :
								antenna|=ANTENNA_A;
								break;
				case 'b':
								antenna|=ANTENNA_B;
								break;
			}
	}
	
	//DBG_871X("%s: antenna=0x%x\n", __func__, antenna);		
	padapter->mppriv.antenna_rx = antenna;
	//DBG_871X("%s:mppriv.antenna_rx=%d\n", __func__, padapter->mppriv.antenna_rx);
	Hal_SetAntenna(padapter);
	wrqu->length = strlen(extra) + 1;
	
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

	PADAPTER padapter = rtw_netdev_priv(dev);


	pmp_priv = &padapter->mppriv;

	if (copy_from_user(extra, wrqu->pointer, wrqu->length))
			return -EFAULT;
			
	DBG_871X("%s: in=%s\n", __func__, extra);

	countPkTx = strncmp(extra, "count=", 5); // strncmp TRUE is 0
	cotuTx = strncmp(extra, "background", 20);
	CarrSprTx = strncmp(extra, "background,cs", 20);
	scTx = strncmp(extra, "background,sc", 20);
	sgleTx = strncmp(extra, "background,stone", 20);
	pkTx = strncmp(extra, "background,pkt", 20);
	stop = strncmp(extra, "stop", 4);
	sscanf(extra, "count=%d,pkt", &count);
	
	//DBG_871X("%s: count=%d countPkTx=%d cotuTx=%d CarrSprTx=%d scTx=%d sgleTx=%d pkTx=%d stop=%d\n", __func__, count, countPkTx, cotuTx, CarrSprTx, pkTx, sgleTx, scTx, stop);
	_rtw_memset(extra, '\0', sizeof(extra));
	
	if (stop == 0) {
		bStartTest = 0; // To set Stop
		pmp_priv->tx.stop = 1;
		sprintf( extra, "Stop continuous Tx");
	} else {
		bStartTest = 1;
		if (pmp_priv->mode != MP_ON) {
			if (pmp_priv->tx.stop != 1) {
				DBG_871X("%s: MP_MODE != ON %d\n", __func__, pmp_priv->mode);
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

	switch (pmp_priv->mode)
	{
		case MP_PACKET_TX:
		
			//DBG_871X("%s:pkTx %d\n", __func__,bStartTest);
			if (bStartTest == 0)
			{
				pmp_priv->tx.stop = 1;
				pmp_priv->mode = MP_ON;
				sprintf( extra, "Stop continuous Tx");
			}
			else if (pmp_priv->tx.stop == 1)
			{
				sprintf( extra, "Start continuous DA=ffffffffffff len=1500 count=%u,\n",count);
				//DBG_871X("%s:countPkTx %d\n", __func__,count);
				pmp_priv->tx.stop = 0;
				pmp_priv->tx.count = count;
				pmp_priv->tx.payload = 2;
				pattrib = &pmp_priv->tx.attrib;
				pattrib->pktlen = 1500;
				_rtw_memset(pattrib->dst, 0xFF, ETH_ALEN);
				SetPacketTx(padapter);
			} 
			else {
				//DBG_871X("%s: pkTx not stop\n", __func__);
				return -EFAULT;
			}
				wrqu->length = strlen(extra);
				return 0;

		case MP_SINGLE_TONE_TX:
			//DBG_871X("%s: sgleTx %d \n", __func__, bStartTest);
			if (bStartTest != 0){
				sprintf( extra, "Start continuous DA=ffffffffffff len=1500 \n infinite=yes.");
            }
				Hal_SetSingleToneTx(padapter, (u8)bStartTest);
			break;

		case MP_CONTINUOUS_TX:
			//DBG_871X("%s: cotuTx %d\n", __func__, bStartTest);
			if (bStartTest != 0){
				sprintf( extra, "Start continuous DA=ffffffffffff len=1500 \n infinite=yes.");
           		 }
           		  Hal_SetContinuousTx(padapter, (u8)bStartTest);
			break;

		case MP_CARRIER_SUPPRISSION_TX:
			//DBG_871X("%s: CarrSprTx %d\n", __func__, bStartTest);
			if (bStartTest != 0){
				if( pmp_priv->rateidx <= MPT_RATE_11M ) 
				{
					sprintf( extra, "Start continuous DA=ffffffffffff len=1500 \n infinite=yes.");
					Hal_SetCarrierSuppressionTx(padapter, (u8)bStartTest);
				}else
					sprintf( extra, "Specify carrier suppression but not CCK rate");
			}
			break;

		case MP_SINGLE_CARRIER_TX:
			//DBG_871X("%s: scTx %d\n", __func__, bStartTest);
			if (bStartTest != 0){
				sprintf( extra, "Start continuous DA=ffffffffffff len=1500 \n infinite=yes.");
			}
				Hal_SetSingleCarrierTx(padapter, (u8)bStartTest);
			break;

		default:
			//DBG_871X("%s:No Match MP_MODE\n", __func__);
			sprintf( extra, "Error! Continuous-Tx is not on-going.");
			return -EFAULT;
	}

	if ( bStartTest==1 && pmp_priv->mode != MP_ON) {
		struct mp_priv *pmp_priv = &padapter->mppriv;
		if (pmp_priv->tx.stop == 0) {
			pmp_priv->tx.stop = 1;
			//DBG_871X("%s: pkt tx is running...\n", __func__);
			rtw_msleep_os(5);
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
	u8 bStartRx=0,bStopRx=0,bQueryPhy;
	u32 cckok=0,cckcrc=0,ofdmok=0,ofdmcrc=0,htok=0,htcrc=0,OFDM_FA=0,CCK_FA=0;
	u8 		input[wrqu->length];

	PADAPTER padapter = rtw_netdev_priv(dev);


	if (copy_from_user(input, wrqu->pointer, wrqu->length))
			return -EFAULT;

	DBG_871X("%s: %s\n", __func__, input);

	bStartRx = (strncmp(input, "start", 5)==0)?1:0; // strncmp TRUE is 0
	bStopRx = (strncmp(input, "stop", 5)==0)?1:0; // strncmp TRUE is 0
	bQueryPhy = (strncmp(input, "phy", 3)==0)?1:0; // strncmp TRUE is 0

	if(bStartRx)
	{
		sprintf( extra, "start");
		SetPacketRx(padapter, bStartRx);
	}
	else if(bStopRx)
	{
		SetPacketRx(padapter, 0);
		sprintf( extra, "Received packet OK:%d CRC error:%d",padapter->mppriv.rx_pktcount,padapter->mppriv.rx_crcerrpktcount);
	}
	else if(bQueryPhy)
	{          
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
		cckok = read_bbreg(padapter, 0xf88, 0xffffffff );
		cckcrc = read_bbreg(padapter, 0xf84, 0xffffffff );
		ofdmok = read_bbreg(padapter, 0xf94, 0x0000FFFF );
		ofdmcrc = read_bbreg(padapter, 0xf94 , 0xFFFF0000 );
		htok = read_bbreg(padapter, 0xf90, 0x0000FFFF );
		htcrc = read_bbreg(padapter,0xf90, 0xFFFF0000 );

		OFDM_FA=+read_bbreg(padapter, 0xcf0, 0x0000FFFF );
		OFDM_FA=+read_bbreg(padapter, 0xcf2, 0xFFFF0000 );
		OFDM_FA=+read_bbreg(padapter, 0xda0, 0xFFFF0000 );
		OFDM_FA=+read_bbreg(padapter, 0xda4, 0x0000FFFF );
		OFDM_FA=+read_bbreg(padapter, 0xda4, 0xFFFF0000 );
		OFDM_FA=+read_bbreg(padapter, 0xda8, 0x0000FFFF );
 		CCK_FA=(rtw_read8(padapter, 0xa5b )<<8 ) | (rtw_read8(padapter, 0xa5c));
		
		sprintf( extra, "Phy Received packet OK:%d CRC error:%d FA Counter: %d",cckok+ofdmok+htok,cckcrc+ofdmcrc+htcrc,OFDM_FA+CCK_FA);
	}
		wrqu->length = strlen(extra) + 1;
	return 0;
}

static int rtw_mp_trx_query(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_point *wrqu, char *extra)
{
	u32 txok,txfail,rxok,rxfail;
	PADAPTER padapter = rtw_netdev_priv(dev);
	//if (copy_from_user(extra, wrqu->data.pointer, wrqu->data.length))
	//	return -EFAULT;

	txok=padapter->mppriv.tx.sended;
	txfail=0;
	rxok = padapter->mppriv.rx_pktcount;
	rxfail = padapter->mppriv.rx_crcerrpktcount;

	_rtw_memset(extra, '\0', 128);

	sprintf(extra, "Tx OK:%d, Tx Fail:%d, Rx OK:%d, CRC error:%d ", txok, txfail,rxok,rxfail);

	wrqu->length=strlen(extra)+1;

	return 0;
}

static int rtw_mp_pwrtrk(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_point *wrqu, char *extra)
{
	u8 enable;
	u32 thermal;
	s32 ret;
	PADAPTER padapter = rtw_netdev_priv(dev);


	enable = 1;
	if (wrqu->length > 1) { // not empty string
		if (strncmp(extra, "stop", 4) == 0)
			enable = 0;
		else {
			if (sscanf(extra, "ther=%d", &thermal)) {
				ret = Hal_SetThermalMeter(padapter, (u8)thermal);
				if (ret == _FAIL) return -EPERM;
			} else
				return -EINVAL;
		}
	}

	ret = Hal_SetPowerTracking(padapter, enable);
	if (ret == _FAIL) return -EPERM;

	return 0;
}

static int rtw_mp_psd(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_point *wrqu, char *extra)
{
	PADAPTER padapter = rtw_netdev_priv(dev);


	if (copy_from_user(extra, wrqu->pointer, wrqu->length))
		return -EFAULT;
	
	wrqu->length = mp_query_psd(padapter, extra);

	return 0;
}

static int rtw_mp_thermal(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_point *wrqu, char *extra)
{
	u8 val;
	u16 bwrite=1;
	
	#if defined(CONFIG_RTL8192C) || defined(CONFIG_RTL8192D)
			u16 addr=EEPROM_THERMAL_METER;
	#endif
	#ifdef CONFIG_RTL8723A
			u16 addr=EEPROM_THERMAL_METER_8723A;
	#endif
	#if defined(CONFIG_RTL8188E)
			u16 addr=EEPROM_THERMAL_METER_88E;
	#endif
	
	u16 cnt=1;
	u16 max_available_size=0;
	PADAPTER padapter = rtw_netdev_priv(dev);	

	if (copy_from_user(extra, wrqu->pointer, wrqu->length))
		return -EFAULT;

	//DBG_871X("print extra %s \n",extra); 
	 
	 bwrite = strncmp(extra, "write", 6); // strncmp TRUE is 0
	 
	 Hal_GetThermalMeter(padapter, &val);
	 
	 if( bwrite == 0 )	
	 {
		 //DBG_871X("to write val:%d",val);
			EFUSE_GetEfuseDefinition(padapter, EFUSE_WIFI, TYPE_AVAILABLE_EFUSE_BYTES_TOTAL, (PVOID)&max_available_size, _FALSE);
			if( 2 > max_available_size )
			{			
				DBG_871X("no available efuse!\n");
				return -EFAULT;
			}	
			if ( rtw_efuse_map_write(padapter, addr, cnt, &val) == _FAIL )
			{
				DBG_871X("rtw_efuse_map_write error \n");			
				return -EFAULT;
			} 
			else
			{
				 sprintf(extra, " efuse write ok :%d", val);	
			}
	  }
	  else
	  {
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
	struct pkt_attrib *pattrib;
	PADAPTER padapter = rtw_netdev_priv(dev);
	
	pmp_priv = &padapter->mppriv;
	
	pmp_priv->tx.sended = 0;
	pmp_priv->tx_pktcount = 0;
	pmp_priv->rx_pktcount = 0;
	pmp_priv->rx_crcerrpktcount = 0;

	//reset phy counter
	write_bbreg(padapter,0xf14,BIT16,0x1);
	rtw_msleep_os(10);
	write_bbreg(padapter,0xf14,BIT16,0x0);

	return 0;
}

static int rtw_mp_dump(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_point *wrqu, char *extra)
{
	struct mp_priv *pmp_priv;
	struct pkt_attrib *pattrib;
        u32 value;
	u8 rf_type,path_nums = 0;
	u32 i,j=1,path;
	PADAPTER padapter = rtw_netdev_priv(dev);
	
	pmp_priv = &padapter->mppriv;

	
	//if (copy_from_user(extra, wrqu->data.pointer, wrqu->data.length))
	//	return -EFAULT;
	
	if ( strncmp(extra, "all", 4)==0 )
	{
			DBG_871X("\n======= MAC REG =======\n");
			for ( i=0x0;i<0x300;i+=4 )
			{	
				if(j%4==1)	DBG_871X("0x%02x",i);
				DBG_871X(" 0x%08x ",rtw_read32(padapter,i));		
				if((j++)%4 == 0)	DBG_871X("\n");	
			}
			for( i=0x400;i<0x1000;i+=4 )
			{	
				if(j%4==1)	DBG_871X("0x%02x",i);
				DBG_871X(" 0x%08x ",rtw_read32(padapter,i));		
				if((j++)%4 == 0)	DBG_871X("\n");	
			}
			
			i,j=1;
			rtw_hal_get_hwreg(padapter, HW_VAR_RF_TYPE, (u8 *)(&rf_type));
				
			DBG_871X("\n======= RF REG =======\n");
			if(( RF_1T2R == rf_type ) ||( RF_1T1R ==rf_type ))	
				path_nums = 1;
			else	
				path_nums = 2;
				
			for(path=0;path<path_nums;path++)
			{
#ifdef CONFIG_RTL8192D
			  for (i = 0; i < 0x50; i++)
#else
	   		 for (i = 0; i < 0x34; i++)
#endif
				{								
					//value = PHY_QueryRFReg(padapter, (RF_RADIO_PATH_E)path,i, bMaskDWord);
					value = rtw_hal_read_rfreg(padapter, path, i, 0xffffffff);
					if(j%4==1)	DBG_871X("0x%02x ",i);
					DBG_871X(" 0x%08x ",value);
					if((j++)%4==0)	DBG_871X("\n");	
				}	
			}
	}
	return 0;
}

static int rtw_mp_phypara(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_point *wrqu, char *extra)
{

	PADAPTER padapter = rtw_netdev_priv(dev);
	char 	input[wrqu->length];
	u32		valxcap;
	
	if (copy_from_user(input, wrqu->pointer, wrqu->length))
			return -EFAULT;
	
	DBG_871X("%s:iwpriv in=%s\n", __func__, input);
	
	sscanf(input, "xcap=%d", &valxcap);

	if (!IS_HARDWARE_TYPE_8192D(padapter))
			return 0;
#ifdef CONFIG_RTL8192D
	Hal_ProSetCrystalCap( padapter , valxcap );
#endif

	sprintf( extra, "Set xcap=%d",valxcap );
	wrqu->length = strlen(extra) + 1;
	
return 0;

}

static int rtw_mp_SetRFPath(struct net_device *dev,
			struct iw_request_info *info,
			union iwreq_data *wrqu, char *extra)
{
	PADAPTER padapter = rtw_netdev_priv(dev);
	char 	input[wrqu->data.length];
	u8		bMain=1,bTurnoff=1;
	
	if (copy_from_user(input, wrqu->data.pointer, wrqu->data.length))
			return -EFAULT;
	DBG_871X("%s:iwpriv in=%s\n", __func__, input);	
	
	bMain = strncmp(input, "1", 2); // strncmp TRUE is 0
	bTurnoff = strncmp(input, "0", 3); // strncmp TRUE is 0

	if(bMain==0)
	{
		MP_PHY_SetRFPathSwitch(padapter,_TRUE);
		DBG_871X("%s:PHY_SetRFPathSwitch=TRUE\n", __func__);	
	}
	else if(bTurnoff==0)
	{
		MP_PHY_SetRFPathSwitch(padapter,_FALSE);
		DBG_871X("%s:PHY_SetRFPathSwitch=FALSE\n", __func__);	
	}
	
	return 0;
}

static int rtw_mp_QueryDrv(struct net_device *dev,
			struct iw_request_info *info,
			union iwreq_data *wrqu, char *extra)
{
	PADAPTER padapter = rtw_netdev_priv(dev);
	char	input[wrqu->data.length];
	u8		qAutoLoad=1;

	EEPROM_EFUSE_PRIV *pEEPROM = GET_EEPROM_EFUSE_PRIV(padapter);
	
	if (copy_from_user(input, wrqu->data.pointer, wrqu->data.length))
			return -EFAULT;
	DBG_871X("%s:iwpriv in=%s\n", __func__, input); 
	
	qAutoLoad = strncmp(input, "autoload", 8); // strncmp TRUE is 0

	if(qAutoLoad==0)
	{
		DBG_871X("%s:qAutoLoad\n", __func__);
		
		if(pEEPROM->bautoload_fail_flag)
			sprintf(extra, "fail");
		else
	        sprintf(extra, "ok");      
	}
		wrqu->data.length = strlen(extra) + 1;
	return 0;
}

/* update Tx AGC offset */
static int rtw_mp_antBdiff(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_point *wrqu, char *extra)
{


	// MPT_ProSetTxAGCOffset
	return 0;
}
#ifdef CONFIG_RTL8723A
/* update Tx AGC offset */
static int rtw_mp_SetBT(struct net_device *dev,
			struct iw_request_info *info,
			union iwreq_data *wrqu, char *extra)
{
	PADAPTER padapter = rtw_netdev_priv(dev);
	BT_REQ_CMD 	BtReq;
	char 	input[128];
	char *pch, *ptmp, *token, *tmp[2]={0x00,0x00};
	u8 setdata[100];

	u16 testmode=1,ready=1,trxparam=1,setgen=1,getgen=1,testctrl=1;	
	int i,ii,jj,kk,cnts,err;
	
	if (copy_from_user(extra, wrqu->data.pointer, wrqu->data.length))
			return -EFAULT;
	
	DBG_871X("%s:iwpriv in=%s\n", __func__, extra);	

	testmode = strncmp(extra, "testmode", 8); // strncmp TRUE is 0
	ready = strncmp(extra, "ready", 5); 
	trxparam = strncmp(extra, "trxparam", 8); 
	setgen = strncmp(extra, "setgen", 6);
	getgen = strncmp(extra, "getgen", 6); 
	testctrl = strncmp(extra, "testctrl", 8);
	
	if ( strncmp(extra, "dlfw", 4) == 0)
	{
		s32 ret;
		ret = rtl8723a_FirmwareDownload(padapter);
		printk("%s: download FW %s\n", __func__, (_FAIL==ret) ? "FAIL!":"OK.");
		sprintf(extra, "download FW %s", (_FAIL==ret) ? "FAIL!":"OK.");
		wrqu->data.length = strlen(extra) + 1;

		goto exit;
	}
	
	DBG_871X("%s:after strncmp\n", __func__);
	//sscanf( input, "testmode=%d", &inparm);
	
    pch = extra;	
	i = 0;
	while ((token = strsep(&pch, ",")) != NULL)
	{
		if (i > 1) break;
		tmp[i] = token;
		i++;
	}
	// tmp[0],[1],[2]
	// wmap,addr,00e04c871200
	if ((tmp[0]==NULL) || (tmp[1]==NULL))
	{
			err = -EINVAL;
			goto exit;
	}

	cnts = strlen(tmp[1]);
	
	DBG_871X("%s: cnts=%d\n", __FUNCTION__, cnts);
	DBG_871X("%s: map data=%s\n", __FUNCTION__, tmp[1]);
				
	for (jj=0, kk=0; jj<cnts; jj++, kk+=2)
	{
			setdata[jj] = key_2char2num(tmp[1][kk], tmp[1][kk+1]);
	}


	
	if( testmode==0 )	
	{
		DBG_871X("%s: BT_SET_MODE \n", __func__); 
		BtReq.opCodeVer=1;
		BtReq.OpCode=1; //BT_SET_MODE	1
		//BtReq.pParamStart[0]=(u8)inparm;
		BtReq.pParamStart[0]= setdata[0];
		//BtReq.pParamStart[0]=tmp[0];
		BtReq.paraLength=cnts/2;
	}
	else if( ready==0 )
	{
		DBG_871X("%s: BT_READY \n", __func__); 
		BtReq.opCodeVer=1;
		BtReq.OpCode=0; //BT_READY 	0
		//BtReq.pParamStart[0]=(u8)inparm;
		BtReq.pParamStart[0]= setdata[0];
		//BtReq.pParamStart[0]=tmp[0];
		BtReq.paraLength=cnts/2;	
	}
	else if( trxparam==0 )
	{
		DBG_871X("%s: BT_SET_TXRX_PARAMETER \n", __func__); 
		BtReq.opCodeVer=1;
		BtReq.OpCode=2; //BT_SET_TXRX_PARAMETER	2
		//BtReq.pParamStart[0]=(u8)inparm;
		BtReq.pParamStart[0]= setdata[0];
		//BtReq.pParamStart[0]=tmp[0];
		BtReq.paraLength=cnts/2;
	}
	else if( setgen==0 )
	{	
		DBG_871X("%s: BT_SET_GENERAL \n", __func__); 
		BtReq.opCodeVer=1;
		BtReq.OpCode=3; //BT_SET_GENERAL	3
		//BtReq.pParamStart[0]=(u8)inparm;
		BtReq.pParamStart[0]= setdata[0];
		//BtReq.pParamStart[0]=tmp[0];
		BtReq.paraLength=cnts/2;	
	}
	else if( getgen==0 )
	{	
		DBG_871X("%s: BT_GET_GENERAL \n", __func__); 
		BtReq.opCodeVer=1;
		BtReq.OpCode=4; //BT_GET_GENERAL	4
		//BtReq.pParamStart[0]=(u8)inparm;
		BtReq.pParamStart[0]= setdata[0];
		//BtReq.pParamStart[0]=tmp[0];
		BtReq.paraLength=cnts/2;	
	}
	else if( testctrl==0 )
	{	
		DBG_871X("%s: BT_TEST_CTRL \n", __func__);
		BtReq.opCodeVer=1;
		BtReq.OpCode=5; //BT_TEST_CTRL	5
		//BtReq.pParamStart[0]=(u8)inparm;
		BtReq.pParamStart[0]= setdata[0];
		//BtReq.pParamStart[0]=tmp[0];
		BtReq.paraLength=cnts/2;	
	}
	DBG_871X("%s: BtReq.paraLength =%d\n", __FUNCTION__, BtReq.paraLength);


	printk(" 0x%x 0x%x \n",BtReq.pParamStart[0],BtReq.pParamStart[1]);
	
	printk("opCodeVer=%d,OpCode=%d",BtReq.opCodeVer,BtReq.OpCode);

	DBG_871X("%s: 2 \n", __func__);
	
	mptbt_BtControlProcess(padapter,&BtReq);
	
exit:
		
	// MPT_ProSetTxAGCOffset
return err;	
	
}
#endif

static int rtw_mp_set(struct net_device *dev,
			struct iw_request_info *info,
			union iwreq_data *wdata, char *extra)
{
	struct iw_point *wrqu = (struct iw_point *)wdata;
	u32 subcmd = wrqu->flags;
	PADAPTER padapter = rtw_netdev_priv(dev);

	DBG_871X("in mp_set extra= %s \n",extra);

	if (padapter == NULL)
	{
		return -ENETDOWN;
	}

	//_rtw_memset(extra, 0x00, IW_PRIV_SIZE_MASK);

	if (extra == NULL)
	{
		wrqu->length = 0;
		return -EIO;
	}

	switch(subcmd)
	{
	case WRITE_REG :
			DBG_871X("set case write_reg \n");
			rtw_mp_write_reg (dev,info,wrqu,extra);
			 break;
			 
	case WRITE_RF:
			DBG_871X("set case write_rf \n");
			rtw_mp_write_rf (dev,info,wrqu,extra);
			 break; 
			 
	case MP_START:
			DBG_871X("set case mp_start \n");
			rtw_mp_start (dev,info,wrqu,extra);
			 break; 
			 
	case MP_STOP:
			DBG_871X("set case mp_stop \n");
			rtw_mp_stop (dev,info,wrqu,extra);
			 break; 
			 
	case MP_BANDWIDTH:
			DBG_871X("set case mp_bandwidth \n");
			rtw_mp_bandwidth (dev,info,wrqu,extra);
			break;
	case MP_PWRTRK:
			DBG_871X("set case MP_PWRTRK \n");
			rtw_mp_pwrtrk (dev,info,wrqu,extra);
			break;
				
	case MP_RESET_STATS:
			DBG_871X("set case MP_RESET_STATS \n");
			rtw_mp_reset_stats	(dev,info,wrqu,extra);
			break;
			
	case EFUSE_SET:
			DBG_871X("efuse set \n");
			rtw_mp_efuse_set (dev,info,wdata,extra);
			break;	
		 		
	case MP_SetRFPathSwh:		
			DBG_871X("set MP_SetRFPathSwitch \n");
			rtw_mp_SetRFPath  (dev,info,wdata,extra);
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
	PADAPTER padapter = rtw_netdev_priv(dev);

	DBG_871X("in mp_get extra= %s \n",extra);

	if (padapter == NULL)
	{
		return -ENETDOWN;
	}
	if (extra == NULL)
	{
		wrqu->length = 0;
		return -EIO;
	}
	
	switch(subcmd)
	{
	case MP_PHYPARA:
			DBG_871X("mp_get  MP_PHYPARA \n");
			rtw_mp_phypara(dev,info,wrqu,extra);	
			break;

	case MP_CHANNEL:
			DBG_871X("set case mp_channel \n");
			rtw_mp_channel (dev,info,wrqu,extra);
			break;
			
	case READ_REG:
			DBG_871X("mp_get  READ_REG \n");
			rtw_mp_read_reg (dev,info,wrqu,extra);
			 break; 
	case READ_RF:
			DBG_871X("mp_get  READ_RF \n");
			rtw_mp_read_rf (dev,info,wrqu,extra);
			break; 
			
	case MP_RATE:
			DBG_871X("mp_get case mp_rate \n");
			rtw_mp_rate (dev,info,wrqu,extra);
			break;
			
	case MP_TXPOWER:
			DBG_871X("mp_get case MP_TXPOWER \n");
			rtw_mp_txpower (dev,info,wrqu,extra);
			break;
			
	case MP_ANT_TX:
			DBG_871X("mp_get case MP_ANT_TX \n");
			rtw_mp_ant_tx (dev,info,wrqu,extra);
			break;
			
	case MP_ANT_RX:
			DBG_871X("mp_get case MP_ANT_RX \n");
			rtw_mp_ant_rx (dev,info,wrqu,extra);
			break;
			
	case MP_QUERY:
			DBG_871X("mp_get mp_query MP_QUERY \n");
			rtw_mp_trx_query(dev,info,wrqu,extra);
			break;
					
	case MP_CTX:
			DBG_871X("mp_get case MP_CTX \n");
			rtw_mp_ctx (dev,info,wrqu,extra);
			break;
			
	case MP_ARX:
			DBG_871X("mp_get case MP_ARX \n");
			rtw_mp_arx (dev,info,wrqu,extra);
			break;
			
	case EFUSE_GET:
			DBG_871X("efuse get EFUSE_GET \n");
			rtw_mp_efuse_get(dev,info,wdata,extra);
		 break; 
		 
	case MP_DUMP:
			DBG_871X("mp_get case MP_DUMP \n");
			rtw_mp_dump (dev,info,wrqu,extra);
		 break; 
	case MP_PSD:
			DBG_871X("mp_get case MP_PSD \n");
			rtw_mp_psd (dev,info,wrqu,extra);
		 break;
		 
	case MP_THER:
			DBG_871X("mp_get case MP_THER \n");
			rtw_mp_thermal (dev,info,wrqu,extra);
		break;

	case MP_QueryDrvStats:		
			DBG_871X("mp_get MP_QueryDrvStats \n");
			rtw_mp_QueryDrv  (dev,info,wdata,extra);
			break;
			 
#ifdef CONFIG_RTL8723A			
	case MP_SetBT:		
			DBG_871X("set MP_SetBT \n");
			rtw_mp_SetBT  (dev,info,wdata,extra);
			break;		 
#endif

	}

return 0;	
}

#endif //#if defined(CONFIG_MP_INCLUDED) && defined(CONFIG_MP_IWPRIV_SUPPORT)



static int rtw_tdls_setup(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *extra)
{
	int ret = 0;

#ifdef CONFIG_TDLS

	u8 i, j;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	u8 mac_addr[ETH_ALEN];

	DBG_871X( "[%s] %s %d\n", __FUNCTION__, extra, wrqu->data.length -1  );

	for( i=0, j=0 ; i < ETH_ALEN; i++, j+=3 ){
		mac_addr[i]=key_2char2num(*(extra+j), *(extra+j+1));
	}

	issue_tdls_setup_req(padapter, mac_addr);

#endif //CONFIG_TDLS
	
	return ret;
}


static int rtw_tdls_teardown(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *extra)
{
	int ret = 0;

#ifdef CONFIG_TDLS

	u8 i,j;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct sta_info *ptdls_sta = NULL;
	u8 mac_addr[ETH_ALEN];

	DBG_871X( "[%s] %s %d\n", __FUNCTION__, extra, wrqu->data.length -1  );

	for( i=0, j=0 ; i < ETH_ALEN; i++, j+=3 ){
		mac_addr[i]=key_2char2num(*(extra+j), *(extra+j+1));
	}
	
	ptdls_sta = rtw_get_stainfo( &(padapter->stapriv), mac_addr);
	
	if(ptdls_sta != NULL)
	{
		ptdls_sta->stat_code = _RSON_TDLS_TEAR_UN_RSN_;
		issue_tdls_teardown(padapter, mac_addr);
	}

#endif //CONFIG_TDLS
	
	return ret;
}


static int rtw_tdls_discovery(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *extra)
{
	int ret = 0;

#ifdef CONFIG_TDLS
	
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	DBG_871X( "[%s] %s %d\n", __FUNCTION__, extra, wrqu->data.length -1  );

	issue_tdls_dis_req(padapter, NULL);

#endif //CONFIG_TDLS

	return ret;
}

static int rtw_tdls_ch_switch(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *extra)
{
	int ret = 0;

#ifdef CONFIG_TDLS
	
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct tdls_info	*ptdlsinfo = &padapter->tdlsinfo;
	u8 i, j, mac_addr[ETH_ALEN];
	struct sta_info *ptdls_sta = NULL;

	DBG_8192C( "[%s] %s %d\n", __FUNCTION__, extra, wrqu->data.length -1  );

	for( i=0, j=0 ; i < ETH_ALEN; i++, j+=3 ){
		mac_addr[i]=key_2char2num(*(extra+j), *(extra+j+1));
	}

	ptdls_sta = rtw_get_stainfo(&padapter->stapriv, mac_addr);
	if( ptdls_sta == NULL )
		return ret;
	ptdls_sta->option=4;
	ptdlsinfo->ch_sensing=1;

	rtw_tdls_cmd(padapter, ptdls_sta->hwaddr, TDLS_INIT_CH_SEN);
	
#endif //CONFIG_TDLS

	return ret;
}
	
static int rtw_tdls_pson(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *extra)
{
	int ret = 0;

#ifdef CONFIG_TDLS
	
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	u8 i, j, mac_addr[ETH_ALEN];
	struct sta_info *ptdls_sta = NULL;

	DBG_871X( "[%s] %s %d\n", __FUNCTION__, extra, wrqu->data.length -1  );

#endif //CONFIG_TDLS

	return ret;

}
	
static int rtw_tdls_psoff(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *extra)
{
	int ret = 0;

#ifdef CONFIG_TDLS
	
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	u8 i, j, mac_addr[ETH_ALEN];
	struct sta_info *ptdls_sta = NULL;
	
	DBG_8192C( "[%s] %s %d\n", __FUNCTION__, extra, wrqu->data.length -1  );

	for( i=0, j=0 ; i < ETH_ALEN; i++, j+=3 ){
		mac_addr[i]=key_2char2num(*(extra+j), *(extra+j+1));
	}

	ptdls_sta = rtw_get_stainfo(&padapter->stapriv, mac_addr);

	issue_nulldata_to_TDLS_peer_STA(padapter, ptdls_sta, 0);
	
#endif //CONFIG_TDLS

	return ret;

}

static int rtw_tdls_ch_switch_off(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *extra)
{
	int ret = 0;

#ifdef CONFIG_TDLS
	
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	u8 i, j, mac_addr[ETH_ALEN];
	struct sta_info *ptdls_sta = NULL;
	
	DBG_871X( "[%s] %s %d\n", __FUNCTION__, extra, wrqu->data.length -1  );

	for( i=0, j=0 ; i < ETH_ALEN; i++, j+=3 ){
		mac_addr[i]=key_2char2num(*(extra+j), *(extra+j+1));
	}

	ptdls_sta = rtw_get_stainfo(&padapter->stapriv, mac_addr);

	ptdls_sta->tdls_sta_state |= TDLS_SW_OFF_STATE;
/*
	if((ptdls_sta->tdls_sta_state & TDLS_AT_OFF_CH_STATE) && (ptdls_sta->tdls_sta_state & TDLS_PEER_AT_OFF_STATE)){
		pmlmeinfo->tdls_candidate_ch= pmlmeext->cur_channel;
		issue_tdls_ch_switch_req(padapter, mac_addr);
		DBG_871X("issue tdls ch switch req back to base channel\n");
	}
*/
	
#endif //CONFIG_TDLS

	return ret;

}

static int rtw_tdls(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *extra)
{
	int ret = 0;

#ifdef CONFIG_TDLS

	DBG_871X( "[%s] extra = %s\n", __FUNCTION__, extra );

	if ( _rtw_memcmp( extra, "setup=", 6 ) )
	{
		wrqu->data.length -=6;
		rtw_tdls_setup( dev, info, wrqu, &extra[6] );
	}
	else if (_rtw_memcmp( extra, "tear=", 5 ) )
	{
		wrqu->data.length -= 5;
		rtw_tdls_teardown( dev, info, wrqu, &extra[5] );
	}
	else if (_rtw_memcmp( extra, "dis=", 4 ) )
	{
		wrqu->data.length -= 4;
		rtw_tdls_discovery( dev, info, wrqu, &extra[4] );
	}
	else if (_rtw_memcmp( extra, "sw=", 3 ) )
	{
		wrqu->data.length -= 3;
		rtw_tdls_ch_switch( dev, info, wrqu, &extra[3] );
	}
	else if (_rtw_memcmp( extra, "swoff=", 6 ) )
	{
		wrqu->data.length -= 6;
		rtw_tdls_ch_switch_off( dev, info, wrqu, &extra[6] );
	}	
	else if (_rtw_memcmp( extra, "pson=", 5 ) )
	{
		wrqu->data.length -= 5;
		rtw_tdls_pson( dev, info, wrqu, &extra[5] );
	}
	else if (_rtw_memcmp( extra, "psoff=", 6 ) )
	{
		wrqu->data.length -= 6;
		rtw_tdls_psoff( dev, info, wrqu, &extra[6] );
	}
	
#endif //CONFIG_TDLS
	
	return ret;

}




#ifdef CONFIG_INTEL_WIDI
static int rtw_widi_set(struct net_device *dev,
                               struct iw_request_info *info,
                               union iwreq_data *wrqu, char *extra)
{
	int ret = 0;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);

	process_intel_widi_cmd(padapter, extra);

	return ret;
}

static int rtw_widi_set_probe_request(struct net_device *dev,
                               struct iw_request_info *info,
                               union iwreq_data *wrqu, char *extra)
{
	int	ret = 0;
	u8	*pbuf = NULL;
	_adapter	*padapter = (_adapter *)rtw_netdev_priv(dev);

#if 1
	pbuf = rtw_malloc(sizeof(l2_msg_t));
	if(pbuf)
	{
		_rtw_memcpy(pbuf, wrqu->data.pointer, wrqu->data.length);
		intel_widi_wk_cmd(padapter, INTEL_WIDI_ISSUE_PROB_WK, pbuf);
	}
#else
	DBG_871X( "[%s] len = %d\n", __FUNCTION__,wrqu->data.length);

	issue_probereq_widi(padapter, wrqu->data.pointer);
#endif
	return ret;
}
#endif // CONFIG_INTEL_WIDI

#ifdef CONFIG_RTL8723A
#include <rtl8723a_hal.h>
//extern u8 _InitPowerOn(PADAPTER padapter);
//extern s32 rtl8723a_FirmwareDownload(PADAPTER padapter);
extern s32 FillH2CCmd(PADAPTER padapter, u8 ElementID, u32 CmdLen, u8 *pCmdBuffer);
#endif

#ifdef CONFIG_MAC_LOOPBACK_DRIVER

#ifdef CONFIG_RTL8723A
extern void rtl8723a_cal_txdesc_chksum(struct tx_desc *ptxdesc);
#define cal_txdesc_chksum rtl8723a_cal_txdesc_chksum
extern void rtl8723a_fill_default_txdesc(struct xmit_frame *pxmitframe, u8 *pbuf);
#define fill_default_txdesc rtl8723a_fill_default_txdesc
#elif defined(CONFIG_RTL8188E)
#include <rtl8188e_hal.h>
extern void rtl8188e_cal_txdesc_chksum(struct tx_desc *ptxdesc);
#define cal_txdesc_chksum rtl8188e_cal_txdesc_chksum
#ifdef CONFIG_SDIO_HCI
extern void rtl8188es_fill_default_txdesc(struct xmit_frame *pxmitframe, u8 *pbuf);
#define fill_default_txdesc rtl8188es_fill_default_txdesc
#endif // CONFIG_SDIO_HCI
#endif // CONFIG_RTL8188E
extern u32 get_txfifo_hwaddr(struct xmit_frame *pxmitframe);

static s32 initLoopback(PADAPTER padapter)
{
	PLOOPBACKDATA ploopback;


	if (padapter->ploopback == NULL) {
		ploopback = (PLOOPBACKDATA)rtw_zmalloc(sizeof(LOOPBACKDATA));
		if (ploopback == NULL) return -ENOMEM;

		_rtw_init_sema(&ploopback->sema, 0);
		ploopback->bstop = _TRUE;
		ploopback->cnt = 0;
		ploopback->size = 300;
		_rtw_memset(ploopback->msg, 0, sizeof(ploopback->msg));

		padapter->ploopback = ploopback;
	}

	return 0;
}

static void freeLoopback(PADAPTER padapter)
{
	PLOOPBACKDATA ploopback;


	ploopback = padapter->ploopback;
	if (ploopback) {
		rtw_mfree((u8*)ploopback, sizeof(LOOPBACKDATA));
		padapter->ploopback = NULL;
	}
}

static s32 initpseudoadhoc(PADAPTER padapter)
{
	NDIS_802_11_NETWORK_INFRASTRUCTURE networkType;
	s32 err;

	networkType = Ndis802_11IBSS;
	err = rtw_set_802_11_infrastructure_mode(padapter, networkType);
	if (err == _FALSE) return _FAIL;

	err = rtw_setopmode_cmd(padapter, networkType);
	if (err == _FAIL) return _FAIL;

	return _SUCCESS;
}

static s32 createpseudoadhoc(PADAPTER padapter)
{
	NDIS_802_11_AUTHENTICATION_MODE authmode;
	struct mlme_priv *pmlmepriv;
	NDIS_802_11_SSID *passoc_ssid;
	WLAN_BSSID_EX *pdev_network;
	u8 *pibss;
	u8 ssid[] = "pseduo_ad-hoc";
	s32 err;
	_irqL irqL;


	pmlmepriv = &padapter->mlmepriv;

	authmode = Ndis802_11AuthModeOpen;
	err = rtw_set_802_11_authentication_mode(padapter, authmode);
	if (err == _FALSE) return _FAIL;

	passoc_ssid = &pmlmepriv->assoc_ssid;
	_rtw_memset(passoc_ssid, 0, sizeof(NDIS_802_11_SSID));
	passoc_ssid->SsidLength = sizeof(ssid) - 1;
	_rtw_memcpy(passoc_ssid->Ssid, ssid, passoc_ssid->SsidLength);

	pdev_network = &padapter->registrypriv.dev_network;
	pibss = padapter->registrypriv.dev_network.MacAddress;
	_rtw_memcpy(&pdev_network->Ssid, passoc_ssid, sizeof(NDIS_802_11_SSID));

	rtw_update_registrypriv_dev_network(padapter);
	rtw_generate_random_ibss(pibss);

	_enter_critical_bh(&pmlmepriv->lock, &irqL);
	pmlmepriv->fw_state = WIFI_ADHOC_MASTER_STATE;
	_exit_critical_bh(&pmlmepriv->lock, &irqL);

#if 0
	err = rtw_createbss_cmd(padapter);
	if (err == _FAIL) return _FAIL;
#else
{
	struct wlan_network *pcur_network;
	struct sta_info *psta;

	//3  create a new psta
	pcur_network = &pmlmepriv->cur_network;

	//clear psta in the cur_network, if any
	psta = rtw_get_stainfo(&padapter->stapriv, pcur_network->network.MacAddress);
	if (psta) rtw_free_stainfo(padapter, psta);

	psta = rtw_alloc_stainfo(&padapter->stapriv, pibss);
	if (psta == NULL) return _FAIL;

	//3  join psudo AdHoc
	pcur_network->join_res = 1;
	pcur_network->aid = psta->aid = 1;
	_rtw_memcpy(&pcur_network->network, pdev_network, get_WLAN_BSSID_EX_sz(pdev_network));

	// set msr to WIFI_FW_ADHOC_STATE
#if 0
	Set_NETYPE0_MSR(padapter, WIFI_FW_ADHOC_STATE);
#else
	{
		u8 val8;

		val8 = rtw_read8(padapter, MSR);
		val8 &= 0xFC; // clear NETYPE0
		val8 |= WIFI_FW_ADHOC_STATE & 0x3;
		rtw_write8(padapter, MSR, val8);
	}
#endif
}
#endif

	return _SUCCESS;
}

static struct xmit_frame* createloopbackpkt(PADAPTER padapter, u32 size)
{
	struct xmit_priv *pxmitpriv;
	struct xmit_frame *pframe;
	struct xmit_buf *pxmitbuf;
	struct pkt_attrib *pattrib;
	struct tx_desc *desc;
	u8 *pkt_start, *pkt_end, *ptr;
	struct rtw_ieee80211_hdr *hdr;
	s32 bmcast;
	_irqL irqL;


	if ((TXDESC_SIZE + WLANHDR_OFFSET + size) > MAX_XMITBUF_SZ) return NULL;

	pxmitpriv = &padapter->xmitpriv;
	pframe = NULL;

	//2 1. allocate xmit frame
	pframe = rtw_alloc_xmitframe(pxmitpriv);
	if (pframe == NULL) return NULL;
	pframe->padapter = padapter;

	//2 2. allocate xmit buffer
	_enter_critical_bh(&pxmitpriv->lock, &irqL);
	pxmitbuf = rtw_alloc_xmitbuf(pxmitpriv);
	_exit_critical_bh(&pxmitpriv->lock, &irqL);
	if (pxmitbuf == NULL) {
		rtw_free_xmitframe_ex(pxmitpriv, pframe);
		return NULL;
	}

	pframe->pxmitbuf = pxmitbuf;
	pframe->buf_addr = pxmitbuf->pbuf;
	pxmitbuf->priv_data = pframe;

	//2 3. update_attrib()
	pattrib = &pframe->attrib;

	// init xmitframe attribute
	_rtw_memset(pattrib, 0, sizeof(struct pkt_attrib));

	pattrib->ether_type = 0x8723;
	_rtw_memcpy(pattrib->src, padapter->eeprompriv.mac_addr, ETH_ALEN);
	_rtw_memcpy(pattrib->ta, pattrib->src, ETH_ALEN);
	_rtw_memset(pattrib->dst, 0xFF, ETH_ALEN);
	_rtw_memcpy(pattrib->ra, pattrib->dst, ETH_ALEN);
//	pattrib->pctrl = 0;
//	pattrib->dhcp_pkt = 0;
//	pattrib->pktlen = 0;
	pattrib->ack_policy = 0;
//	pattrib->pkt_hdrlen = ETH_HLEN;
	pattrib->hdrlen = WLAN_HDR_A3_LEN;
	pattrib->subtype = WIFI_DATA;
	pattrib->priority = 0;
	pattrib->qsel = pattrib->priority;
//	do_queue_select(padapter, pattrib);
	pattrib->nr_frags = 1;
	pattrib->encrypt = 0;
	pattrib->bswenc = _FALSE;
	pattrib->qos_en = _FALSE;

	bmcast = IS_MCAST(pattrib->ra);
	if (bmcast) {
		pattrib->mac_id = 1;
		pattrib->psta = rtw_get_bcmc_stainfo(padapter);
	} else {
		pattrib->mac_id = 0;
		pattrib->psta = rtw_get_stainfo(&padapter->stapriv, get_bssid(&padapter->mlmepriv));
	}

	pattrib->pktlen = size;
	pattrib->last_txcmdsz = pattrib->hdrlen + pattrib->pktlen;

	//2 4. fill TX descriptor
	desc = (struct tx_desc*)pframe->buf_addr;
	_rtw_memset(desc, 0, TXDESC_SIZE);

	fill_default_txdesc(pframe, (u8*)desc);

	// Hw set sequence number
	((PTXDESC)desc)->hwseq_en = 0; // HWSEQ_EN, 0:disable, 1:enable
//	((PTXDESC)desc)->hwseq_sel = 0; // HWSEQ_SEL

	((PTXDESC)desc)->disdatafb = 1;

	// convert to little endian
	desc->txdw0 = cpu_to_le32(desc->txdw0);
	desc->txdw1 = cpu_to_le32(desc->txdw1);
	desc->txdw2 = cpu_to_le32(desc->txdw2);
	desc->txdw3 = cpu_to_le32(desc->txdw3);
	desc->txdw4 = cpu_to_le32(desc->txdw4);
	desc->txdw5 = cpu_to_le32(desc->txdw5);
	desc->txdw6 = cpu_to_le32(desc->txdw6);
	desc->txdw7 = cpu_to_le32(desc->txdw7);
#ifdef CONFIG_PCI_HCI
	desc->txdw8 = cpu_to_le32(desc->txdw8);
	desc->txdw9 = cpu_to_le32(desc->txdw9);
	desc->txdw10 = cpu_to_le32(desc->txdw10);
	desc->txdw11 = cpu_to_le32(desc->txdw11);
	desc->txdw12 = cpu_to_le32(desc->txdw12);
	desc->txdw13 = cpu_to_le32(desc->txdw13);
	desc->txdw14 = cpu_to_le32(desc->txdw14);
	desc->txdw15 = cpu_to_le32(desc->txdw15);
#endif

	cal_txdesc_chksum(desc);

	//2 5. coalesce
	pkt_start = pframe->buf_addr + TXDESC_SIZE;
	pkt_end = pkt_start + pattrib->last_txcmdsz;

	//3 5.1. make wlan header, make_wlanhdr()
	hdr = (struct rtw_ieee80211_hdr *)pkt_start;
	SetFrameSubType(&hdr->frame_ctl, pattrib->subtype);
	_rtw_memcpy(hdr->addr1, pattrib->dst, ETH_ALEN); // DA
	_rtw_memcpy(hdr->addr2, pattrib->src, ETH_ALEN); // SA
	_rtw_memcpy(hdr->addr3, get_bssid(&padapter->mlmepriv), ETH_ALEN); // RA, BSSID

	//3 5.2. make payload
	ptr = pkt_start + pattrib->hdrlen;
	get_random_bytes(ptr, pkt_end - ptr);

	pxmitbuf->len = TXDESC_SIZE + pattrib->last_txcmdsz;
	pxmitbuf->ptail += pxmitbuf->len;

	return pframe;
}

static void freeloopbackpkt(PADAPTER padapter, struct xmit_frame *pframe)
{
	struct xmit_priv *pxmitpriv;
	struct xmit_buf *pxmitbuf;


	pxmitpriv = &padapter->xmitpriv;
	pxmitbuf = pframe->pxmitbuf;

	rtw_free_xmitframe_ex(pxmitpriv, pframe);
	rtw_free_xmitbuf(pxmitpriv, pxmitbuf);
}

static void printdata(u8 *pbuf, u32 len)
{
	u32 i, val;


	for (i = 0; (i+4) <= len; i+=4) {
		printk("%08X", *(u32*)(pbuf + i));
		if ((i+4) & 0x1F) printk(" ");
		else printk("\n");
	}

	if (i < len)
	{
#ifdef CONFIG_BIG_ENDIAN
		for (; i < len, i++)
			printk("%02X", pbuf+i);
#else // CONFIG_LITTLE_ENDIAN
#if 0
		val = 0;
		_rtw_memcpy(&val, pbuf + i, len - i);
		printk("%8X", val);
#else
		u8 str[9];
		u8 n;
		val = 0;
		n = len - i;
		_rtw_memcpy(&val, pbuf+i, n);
		sprintf(str, "%08X", val);
		n = (4 - n) * 2;
		printk("%8s", str+n);
#endif
#endif // CONFIG_LITTLE_ENDIAN
	}
	printk("\n");
}

static u8 pktcmp(PADAPTER padapter, u8 *txbuf, u32 txsz, u8 *rxbuf, u32 rxsz)
{
	PHAL_DATA_TYPE phal;
	struct recv_stat *prxstat;
	struct recv_stat report;
	PRXREPORT prxreport;
	u32 drvinfosize;
	u32 rxpktsize;
	u8 fcssize;
	u8 ret = _FALSE;

	prxstat = (struct recv_stat*)rxbuf;
	report.rxdw0 = le32_to_cpu(prxstat->rxdw0);
	report.rxdw1 = le32_to_cpu(prxstat->rxdw1);
	report.rxdw2 = le32_to_cpu(prxstat->rxdw2);
	report.rxdw3 = le32_to_cpu(prxstat->rxdw3);
	report.rxdw4 = le32_to_cpu(prxstat->rxdw4);
	report.rxdw5 = le32_to_cpu(prxstat->rxdw5);

	prxreport = (PRXREPORT)&report;
	drvinfosize = prxreport->drvinfosize << 3;
	rxpktsize = prxreport->pktlen;

	phal = GET_HAL_DATA(padapter);
	if (phal->ReceiveConfig & RCR_APPFCS) fcssize = IEEE80211_FCS_LEN;
	else fcssize = 0;

	if ((txsz - TXDESC_SIZE) != (rxpktsize - fcssize)) {
		printk("%s: ERROR! size not match tx/rx=%d/%d !\n",
			__func__, txsz - TXDESC_SIZE, rxpktsize - fcssize);
		ret = _FALSE;
	} else {
		ret = _rtw_memcmp(txbuf + TXDESC_SIZE,\
						  rxbuf + RXDESC_SIZE + drvinfosize,\
						  txsz - TXDESC_SIZE);
		if (ret == _FALSE) {
			printk("%s: ERROR! pkt content mismatch!\n", __func__);
		}
	}

	if (ret == _FALSE)
	{
		printk("\n%s: TX PKT total=%d, desc=%d, content=%d\n",
			__func__, txsz, TXDESC_SIZE, txsz - TXDESC_SIZE);
		printk("%s: TX DESC size=%d\n", __func__, TXDESC_SIZE);
		printdata(txbuf, TXDESC_SIZE);
		printk("%s: TX content size=%d\n", __func__, txsz - TXDESC_SIZE);
		printdata(txbuf + TXDESC_SIZE, txsz - TXDESC_SIZE);

		printk("\n%s: RX PKT read=%d offset=%d(%d,%d) content=%d\n",
			__func__, rxsz, RXDESC_SIZE + drvinfosize, RXDESC_SIZE, drvinfosize, rxpktsize);
		if (rxpktsize != 0)
		{
			printk("%s: RX DESC size=%d\n", __func__, RXDESC_SIZE);
			printdata(rxbuf, RXDESC_SIZE);
			printk("%s: RX drvinfo size=%d\n", __func__, drvinfosize);
			printdata(rxbuf + RXDESC_SIZE, drvinfosize);
			printk("%s: RX content size=%d\n", __func__, rxpktsize);
			printdata(rxbuf + RXDESC_SIZE + drvinfosize, rxpktsize);
		} else {
			printk("%s: RX data size=%d\n", __func__, rxsz);
			printdata(rxbuf, rxsz);
		}
	}

	return ret;
}

thread_return lbk_thread(thread_context context)
{
	s32 err;
	PADAPTER padapter;
	PLOOPBACKDATA ploopback;
	struct xmit_frame *pxmitframe;
	u32 cnt, ok, fail, headerlen;
	u32 pktsize;
	u32 ff_hwaddr;


	padapter = (PADAPTER)context;
	ploopback = padapter->ploopback;
	if (ploopback == NULL) return -1;
	cnt = 0;
	ok = 0;
	fail = 0;

	daemonize("%s", "RTW_LBK_THREAD");
	allow_signal(SIGTERM);

	do {
		if (ploopback->size == 0) {
			get_random_bytes(&pktsize, 4);
			pktsize = (pktsize % 1535) + 1; // 1~1535
		} else
			pktsize = ploopback->size;
		
		pxmitframe = createloopbackpkt(padapter, pktsize);
		if (pxmitframe == NULL) {
			sprintf(ploopback->msg, "loopback FAIL! 3. create Packet FAIL!");
			break;
		}

		ploopback->txsize = TXDESC_SIZE + pxmitframe->attrib.last_txcmdsz;
		_rtw_memcpy(ploopback->txbuf, pxmitframe->buf_addr, ploopback->txsize);
		ff_hwaddr = get_txfifo_hwaddr(pxmitframe);
		cnt++;
		printk("%s: wirte port cnt=%d size=%d\n", __func__, cnt, ploopback->txsize);
		rtw_write_port(padapter, ff_hwaddr, ploopback->txsize, ploopback->txbuf);

		// wait for rx pkt
		_rtw_down_sema(&ploopback->sema);

		err = pktcmp(padapter, ploopback->txbuf, ploopback->txsize, ploopback->rxbuf, ploopback->rxsize);
		if (err == _TRUE)
			ok++;
		else
			fail++;

		ploopback->txsize = 0;
		_rtw_memset(ploopback->txbuf, 0, 0x8000);
		ploopback->rxsize = 0;
		_rtw_memset(ploopback->rxbuf, 0, 0x8000);

		freeloopbackpkt(padapter, pxmitframe);
		pxmitframe = NULL;

		if (signal_pending(current)) {
			flush_signals(current);
		}

		if ((ploopback->bstop == _TRUE) ||
			((ploopback->cnt != 0) && (ploopback->cnt == cnt)))
		{
			u32 ok_rate, fail_rate, all;
			all = cnt;
			ok_rate = (ok*100)/all;
			fail_rate = (fail*100)/all;
			sprintf(ploopback->msg,\
					"loopback result: ok=%d%%(%d/%d),error=%d%%(%d/%d)",\
					ok_rate, ok, all, fail_rate, fail, all);
			break;
		}
	} while (1);

	ploopback->bstop = _TRUE;

	thread_exit();
}

static void loopbackTest(PADAPTER padapter, u32 cnt, u32 size, u8* pmsg)
{
	PLOOPBACKDATA ploopback;
	u32 len;
	s32 err;


	ploopback = padapter->ploopback;

	if (ploopback)
	{
		if (ploopback->bstop == _FALSE) {
			ploopback->bstop = _TRUE;
			_rtw_up_sema(&ploopback->sema);
		}
		len = 0;
		do {
			len = strlen(ploopback->msg);
			if (len) break;
			rtw_msleep_os(1);
		} while (1);
		_rtw_memcpy(pmsg, ploopback->msg, len+1);
		freeLoopback(padapter);

		return;
	}

	// disable dynamic algorithm
	{
	u32 DMFlag = DYNAMIC_FUNC_DISABLE;
	rtw_hal_get_hwreg(padapter, HW_VAR_DM_FLAG, (u8*)&DMFlag);
	}

	// create pseudo ad-hoc connection
	err = initpseudoadhoc(padapter);
	if (err == _FAIL) {
		sprintf(pmsg, "loopback FAIL! 1.1 init ad-hoc FAIL!");
		return;
	}

	err = createpseudoadhoc(padapter);
	if (err == _FAIL) {
		sprintf(pmsg, "loopback FAIL! 1.2 create ad-hoc master FAIL!");
		return;
	}

	err = initLoopback(padapter);
	if (err) {
		sprintf(pmsg, "loopback FAIL! 2. init FAIL! error code=%d", err);
		return;
	}

	ploopback = padapter->ploopback;

	ploopback->bstop = _FALSE;
	ploopback->cnt = cnt;
	ploopback->size = size;
	ploopback->lbkthread = kernel_thread(lbk_thread, padapter, CLONE_FS|CLONE_FILES);
	if (ploopback->lbkthread < 0) {
		freeLoopback(padapter);
		sprintf(pmsg, "loopback start FAIL! cnt=%d", cnt);
		return;
	}

	sprintf(pmsg, "loopback start! cnt=%d", cnt);
}
#endif // CONFIG_MAC_LOOPBACK_DRIVER

static int rtw_test(
	struct net_device *dev,
	struct iw_request_info *info,
	union iwreq_data *wrqu, char *extra)
{
	u32 len;
	u8 *pbuf, *pch;
	char *ptmp;
	u8 *delim = ",";
	PADAPTER padapter = rtw_netdev_priv(dev);


	DBG_871X("+%s\n", __func__);
	len = wrqu->data.length;

	pbuf = (u8*)rtw_zmalloc(len);
	if (pbuf == NULL) {
		DBG_871X("%s: no memory!\n", __func__);
		return -ENOMEM;
	}

	if (copy_from_user(pbuf, wrqu->data.pointer, len)) {
		rtw_mfree(pbuf, len);
		DBG_871X("%s: copy from user fail!\n", __func__);
		return -EFAULT;
	}
	DBG_871X("%s: string=\"%s\"\n", __func__, pbuf);

	ptmp = (char*)pbuf;
	pch = strsep(&ptmp, delim);
	if ((pch == NULL) || (strlen(pch) == 0)) {
		rtw_mfree(pbuf, len);
		DBG_871X("%s: parameter error(level 1)!\n", __func__);
		return -EFAULT;
	}

#ifdef CONFIG_MAC_LOOPBACK_DRIVER
	if (strcmp(pch, "loopback") == 0)
	{
		s32 cnt = 0;
		u32 size = 64;

		pch = strsep(&ptmp, delim);
		if ((pch == NULL) || (strlen(pch) == 0)) {
			rtw_mfree(pbuf, len);
			DBG_871X("%s: parameter error(level 2)!\n", __func__);
			return -EFAULT;
		}

		sscanf(pch, "%d", &cnt);
		DBG_871X("%s: loopback cnt=%d\n", __func__, cnt);

		pch = strsep(&ptmp, delim);
		if ((pch == NULL) || (strlen(pch) == 0)) {
			rtw_mfree(pbuf, len);
			DBG_871X("%s: parameter error(level 2)!\n", __func__);
			return -EFAULT;
		}

		sscanf(pch, "%d", &size);
		DBG_871X("%s: loopback size=%d\n", __func__, size);

		loopbackTest(padapter, cnt, size, extra);
		wrqu->data.length = strlen(extra) + 1;

		rtw_mfree(pbuf, len);
		return 0;
	}
#endif

#ifdef CONFIG_RTL8723A
#if 0
	if (strcmp(pch, "poweron") == 0)
	{
		s32 ret;

		ret = _InitPowerOn(padapter);
		DBG_871X("%s: power on %s\n", __func__, (_FAIL==ret) ? "FAIL!":"OK.");
		sprintf(extra, "Power ON %s", (_FAIL==ret) ? "FAIL!":"OK.");
		wrqu->data.length = strlen(extra) + 1;

		rtw_mfree(pbuf, len);
		return 0;
	}

	if (strcmp(pch, "dlfw") == 0)
	{
		s32 ret;

		ret = rtl8723a_FirmwareDownload(padapter);
		DBG_871X("%s: download FW %s\n", __func__, (_FAIL==ret) ? "FAIL!":"OK.");
		sprintf(extra, "download FW %s", (_FAIL==ret) ? "FAIL!":"OK.");
		wrqu->data.length = strlen(extra) + 1;

		rtw_mfree(pbuf, len);
		return 0;
	}
#endif

#ifdef CONFIG_BT_COEXIST
#define GET_BT_INFO(padapter)	(&GET_HAL_DATA(padapter)->BtInfo)

	if (strcmp(pch, "btdbg") == 0)
	{
		printk("===== BT debug information Start =====\n");
		printk("WIFI status=\n");
		printk("BT status=\n");
		printk("BT profile=\n");
		printk("WIFI RSSI=%d\n", GET_HAL_DATA(padapter)->dmpriv.UndecoratedSmoothedPWDB);
		printk("BT RSSI=\n");
		printk("coex mechanism=\n");
		printk("BT counter TX/RX=/\n");
		printk("0x880=0x%08x\n", rtw_read32(padapter, 0x880));
		printk("0x6c0=0x%08x\n", rtw_read32(padapter, 0x6c0));
		printk("0x6c4=0x%08x\n", rtw_read32(padapter, 0x6c4));
		printk("0x6c8=0x%08x\n", rtw_read32(padapter, 0x6c8));
		printk("0x6cc=0x%08x\n", rtw_read32(padapter, 0x6cc));
		printk("0x778=0x%08x\n", rtw_read32(padapter, 0x778));
		printk("0xc50=0x%08x\n", rtw_read32(padapter, 0xc50));
		BT_DisplayBtCoexInfo(padapter);
		printk("===== BT debug information End =====\n");
	}

	if (strcmp(pch, "bton") == 0)
	{
		PBT30Info		pBTInfo = GET_BT_INFO(padapter);
		PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;

		pBtMgnt->ExtConfig.bManualControl = _FALSE;
	}

	if (strcmp(pch, "btoff") == 0)
	{
		PBT30Info		pBTInfo = GET_BT_INFO(padapter);
		PBT_MGNT		pBtMgnt = &pBTInfo->BtMgnt;

		pBtMgnt->ExtConfig.bManualControl = _TRUE;
	}
#endif // CONFIG_BT_COEXIST

	if (strcmp(pch, "h2c") == 0)
	{
		u8 param[6];
		u8 count = 0;
		u32 tmp;
		u8 i;
		u32 pos;
		s32 ret;


		do {
			pch = strsep(&ptmp, delim);
			if ((pch == NULL) || (strlen(pch) == 0))
				break;

			sscanf(pch, "%x", &tmp);
			param[count++] = (u8)tmp;
		} while (count < 6);

		if (count == 0) {
			rtw_mfree(pbuf, len);
			printk("%s: parameter error(level 2)!\n", __func__);
			return -EFAULT;
		}

		ret = FillH2CCmd(padapter, param[0], count-1, &param[1]);

		pos = sprintf(extra, "H2C ID=%x content=", param[0]);
		for (i=0; i<count; i++) {
			pos += sprintf(extra+pos, "%x,", param[i]);
		}
		extra[pos] = 0;
		pos--;
		pos += sprintf(extra+pos, " %s", ret==_FAIL?"FAIL":"OK");

		wrqu->data.length = strlen(extra) + 1;
	}
#endif // CONFIG_RTL8723A

	rtw_mfree(pbuf, len);
	return 0;
}

#include <rtw_android.h>
int rtw_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct iwreq *wrq = (struct iwreq *)rq;
	int ret=0;

	switch (cmd)
	{
	    case RTL_IOCTL_WPA_SUPPLICANT:	
			ret = wpa_supplicant_ioctl(dev, &wrq->u.data);
			break;
#ifdef CONFIG_AP_MODE
		case RTL_IOCTL_HOSTAPD:
			ret = rtw_hostapd_ioctl(dev, &wrq->u.data);			
			break;
#ifdef CONFIG_NO_WIRELESS_HANDLERS			
		case SIOCSIWMODE:
			ret = rtw_wx_set_mode(dev, NULL, &wrq->u, NULL);
			break;
#endif			
#endif
		case (SIOCDEVPRIVATE+1):
			ret = rtw_android_priv_cmd(dev, rq, cmd);
			break;
	    default:
			ret = -EOPNOTSUPP;
			break;
	}
	
	return ret;
}

static iw_handler rtw_handlers[] =
{
	NULL,					/* SIOCSIWCOMMIT */
	rtw_wx_get_name,		/* SIOCGIWNAME */
	dummy,					/* SIOCSIWNWID */
	dummy,					/* SIOCGIWNWID */
	rtw_wx_set_freq,		/* SIOCSIWFREQ */
	rtw_wx_get_freq,		/* SIOCGIWFREQ */
	rtw_wx_set_mode,		/* SIOCSIWMODE */
	rtw_wx_get_mode,		/* SIOCGIWMODE */
	dummy,					/* SIOCSIWSENS */
	rtw_wx_get_sens,		/* SIOCGIWSENS */
	NULL,					/* SIOCSIWRANGE */
	rtw_wx_get_range,		/* SIOCGIWRANGE */
	rtw_wx_set_priv,		/* SIOCSIWPRIV */
	NULL,					/* SIOCGIWPRIV */
	NULL,					/* SIOCSIWSTATS */
	NULL,					/* SIOCGIWSTATS */
	dummy,					/* SIOCSIWSPY */
	dummy,					/* SIOCGIWSPY */
	NULL,					/* SIOCGIWTHRSPY */
	NULL,					/* SIOCWIWTHRSPY */
	rtw_wx_set_wap,		/* SIOCSIWAP */
	rtw_wx_get_wap,		/* SIOCGIWAP */
	rtw_wx_set_mlme,		/* request MLME operation; uses struct iw_mlme */
	dummy,					/* SIOCGIWAPLIST -- depricated */
	rtw_wx_set_scan,		/* SIOCSIWSCAN */
	rtw_wx_get_scan,		/* SIOCGIWSCAN */
	rtw_wx_set_essid,		/* SIOCSIWESSID */
	rtw_wx_get_essid,		/* SIOCGIWESSID */
	dummy,					/* SIOCSIWNICKN */
	rtw_wx_get_nick,		/* SIOCGIWNICKN */
	NULL,					/* -- hole -- */
	NULL,					/* -- hole -- */
	rtw_wx_set_rate,		/* SIOCSIWRATE */
	rtw_wx_get_rate,		/* SIOCGIWRATE */
	dummy,					/* SIOCSIWRTS */
	rtw_wx_get_rts,			/* SIOCGIWRTS */
	rtw_wx_set_frag,		/* SIOCSIWFRAG */
	rtw_wx_get_frag,		/* SIOCGIWFRAG */
	dummy,					/* SIOCSIWTXPOW */
	dummy,					/* SIOCGIWTXPOW */
	dummy,					/* SIOCSIWRETRY */
	rtw_wx_get_retry,		/* SIOCGIWRETRY */
	rtw_wx_set_enc,			/* SIOCSIWENCODE */
	rtw_wx_get_enc,			/* SIOCGIWENCODE */
	dummy,					/* SIOCSIWPOWER */
	rtw_wx_get_power,		/* SIOCGIWPOWER */
	NULL,					/*---hole---*/
	NULL,					/*---hole---*/
	rtw_wx_set_gen_ie,		/* SIOCSIWGENIE */
	NULL,					/* SIOCGWGENIE */
	rtw_wx_set_auth,		/* SIOCSIWAUTH */
	NULL,					/* SIOCGIWAUTH */
	rtw_wx_set_enc_ext,		/* SIOCSIWENCODEEXT */
	NULL,					/* SIOCGIWENCODEEXT */
	rtw_wx_set_pmkid,		/* SIOCSIWPMKSA */
	NULL,					/*---hole---*/
}; 

#if defined(CONFIG_MP_INCLUDED) && defined(CONFIG_MP_IWPRIV_SUPPORT)

static const struct iw_priv_args rtw_private_args[] =
{	
	{ SIOCIWFIRSTPRIV + 0x00, IW_PRIV_TYPE_CHAR | 1024, 0 , ""},  //set 
	{ SIOCIWFIRSTPRIV + 0x01, IW_PRIV_TYPE_CHAR | 1024, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK , ""},//get
/* --- sub-ioctls definitions --- */   
		{ MP_START , IW_PRIV_TYPE_CHAR | 1024, 0, "mp_start" }, //set
		{ MP_PHYPARA, IW_PRIV_TYPE_CHAR | 1024, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, "mp_phypara" },//get
		{ MP_STOP , IW_PRIV_TYPE_CHAR | 1024, 0, "mp_stop" }, //set
		{ MP_CHANNEL , IW_PRIV_TYPE_CHAR | 1024 , IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, "mp_channel" },//get
		{ MP_BANDWIDTH , IW_PRIV_TYPE_CHAR | 1024, 0, "mp_bandwidth"}, //set
		{ MP_RATE , IW_PRIV_TYPE_CHAR | 1024, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, "mp_rate" },//get
		{ MP_RESET_STATS , IW_PRIV_TYPE_CHAR | 1024, 0, "mp_reset_stats"},
		{ MP_QUERY , IW_PRIV_TYPE_CHAR | 1024, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK , "mp_query"}, //get
		{ MP_NULL, IW_PRIV_TYPE_CHAR | 128, 0,"NULL"},//set
		{ READ_REG , IW_PRIV_TYPE_CHAR | 1024, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, "read_reg" },
		{ MP_NULL, IW_PRIV_TYPE_CHAR | 128, 0,"NULL"},//set
		{ MP_RATE , IW_PRIV_TYPE_CHAR | 1024, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, "mp_rate" },
		{ MP_NULL, IW_PRIV_TYPE_CHAR | 128, 0,"NULL"},//set
		{ READ_RF , IW_PRIV_TYPE_CHAR | 1024, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, "read_rf" },
		{ MP_NULL, IW_PRIV_TYPE_CHAR | 128, 0,"NULL"},//set
		{ MP_PSD , IW_PRIV_TYPE_CHAR | 1024, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, "mp_psd"}, 
		{ MP_NULL, IW_PRIV_TYPE_CHAR | 128, 0,"NULL"},//set
		{ MP_DUMP, IW_PRIV_TYPE_CHAR | 1024, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, "mp_dump" },
		{ MP_NULL, IW_PRIV_TYPE_CHAR | 128, 0,"NULL"},//set
		{ MP_TXPOWER , IW_PRIV_TYPE_CHAR | 1024, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, "mp_txpower"},
		{ MP_NULL, IW_PRIV_TYPE_CHAR | 128, 0,"NULL"},//set
		{ MP_ANT_TX , IW_PRIV_TYPE_CHAR | 1024,  IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, "mp_ant_tx"},
		{ MP_NULL, IW_PRIV_TYPE_CHAR | 128, 0,"NULL"},//set
		{ MP_ANT_RX , IW_PRIV_TYPE_CHAR | 1024, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, "mp_ant_rx"},
		{ WRITE_REG, IW_PRIV_TYPE_CHAR | 1024, 0,"write_reg"},//set
		{ MP_NULL, IW_PRIV_TYPE_CHAR | 1024, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, "NULL" },
		{ WRITE_RF, IW_PRIV_TYPE_CHAR | 1024, 0,"write_rf"},//set
		{ MP_NULL, IW_PRIV_TYPE_CHAR | 1024, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, "NULL" },
		{ MP_NULL, IW_PRIV_TYPE_CHAR | 128, 0,"NULL"},//set
		{ MP_CTX , IW_PRIV_TYPE_CHAR | 1024, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, "mp_ctx"},
		{ MP_NULL, IW_PRIV_TYPE_CHAR | 128, 0,"NULL"},//set
		{ MP_ARX , IW_PRIV_TYPE_CHAR | 1024, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, "mp_arx"},
		{ MP_NULL, IW_PRIV_TYPE_CHAR | 128, 0,"NULL"},//set
		{ MP_THER , IW_PRIV_TYPE_CHAR | 1024, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, "mp_ther"},
		{ EFUSE_SET, IW_PRIV_TYPE_CHAR | 1024, 0, "efuse_set" },
		{ EFUSE_GET, IW_PRIV_TYPE_CHAR | 1024, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, "efuse_get" },
		{ MP_PWRTRK , IW_PRIV_TYPE_CHAR | 1024, 0, "mp_pwrtrk"},
		{ MP_QueryDrvStats, IW_PRIV_TYPE_CHAR | 1024, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, "mp_drvquery" },
		{ MP_IOCTL, IW_PRIV_TYPE_CHAR | 1024, 0, "mp_ioctl"}, // mp_ioctl	
		{ MP_SetRFPathSwh, IW_PRIV_TYPE_CHAR | 1024, 0, "mp_setrfpath" },
		
#ifdef CONFIG_RTL8723A
		{ MP_SetBT, IW_PRIV_TYPE_CHAR | 1024, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, "mp_setbt" },
#endif

	{ SIOCIWFIRSTPRIV + 0x02, IW_PRIV_TYPE_CHAR | 1024, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK , "test"},//set
};


static iw_handler rtw_private_handler[] = 
{
	rtw_mp_set,
	rtw_mp_get,
};

#else // not inlucde MP

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
//for PLATFORM_MT53XX	
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

//for RTK_DMP_PLATFORM	
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
#if 0
	{
		SIOCIWFIRSTPRIV + 0xE,0,0, "wowlan_ctrl"
	},
#endif
	{
		SIOCIWFIRSTPRIV + 0x10,
		IW_PRIV_TYPE_CHAR | P2P_PRIVATE_IOCTL_SET_LEN, 0, "p2p_set"
	},
	{
		SIOCIWFIRSTPRIV + 0x11,
		IW_PRIV_TYPE_CHAR | P2P_PRIVATE_IOCTL_SET_LEN, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_FIXED | P2P_PRIVATE_IOCTL_SET_LEN , "p2p_get"
	},
	{
		SIOCIWFIRSTPRIV + 0x12,
		IW_PRIV_TYPE_CHAR | P2P_PRIVATE_IOCTL_SET_LEN, IW_PRIV_TYPE_CHAR | IFNAMSIZ , "p2p_get2"
	},	
#ifdef CONFIG_TDLS
	{SIOCIWFIRSTPRIV + 0x13, IW_PRIV_TYPE_CHAR | 128, 0,"NULL"},
	{
		SIOCIWFIRSTPRIV + 0x14,
		IW_PRIV_TYPE_CHAR  | 64, 0, "tdls"
	},
#endif
	{
		SIOCIWFIRSTPRIV + 0x16,
		IW_PRIV_TYPE_CHAR | 64, 0, "pm_set"
	},

	{SIOCIWFIRSTPRIV + 0x18, IW_PRIV_TYPE_CHAR | IFNAMSIZ , 0 , "rereg_nd_name"},

	{SIOCIWFIRSTPRIV + 0x1A, IW_PRIV_TYPE_CHAR | 1024, 0, "efuse_set"},
	{SIOCIWFIRSTPRIV + 0x1B, IW_PRIV_TYPE_CHAR | 128, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, "efuse_get"},
	{
		SIOCIWFIRSTPRIV + 0x1D,
		IW_PRIV_TYPE_CHAR | 40, IW_PRIV_TYPE_CHAR | 0x7FF, "test"
	},
#ifdef CONFIG_INTEL_WIDI
	{
		SIOCIWFIRSTPRIV + 0x1E,
		IW_PRIV_TYPE_CHAR | 64, 0, "widi_set"
	},
	{
		SIOCIWFIRSTPRIV + 0x1F,
		IW_PRIV_TYPE_CHAR | 128, 0, "widi_prob_req"
	},
#endif // CONFIG_INTEL_WIDI
};

static iw_handler rtw_private_handler[] = 
{
	rtw_wx_write32,					//0x00
	rtw_wx_read32,					//0x01
	rtw_drvext_hdl,					//0x02
	rtw_mp_ioctl_hdl,				//0x03

// for MM DTV platform
	rtw_get_ap_info,					//0x04

	rtw_set_pid,						//0x05
	rtw_wps_start,					//0x06

// for PLATFORM_MT53XX
	rtw_wx_get_sensitivity,			//0x07
	rtw_wx_set_mtk_wps_probe_ie,	//0x08
	rtw_wx_set_mtk_wps_ie,			//0x09

// for RTK_DMP_PLATFORM
// Set Channel depend on the country code
	rtw_wx_set_channel_plan,		//0x0A

	rtw_dbg_port,					//0x0B
	rtw_wx_write_rf,					//0x0C
	rtw_wx_read_rf,					//0x0D

#if 0
	rtw_wowlan_ctrl,					//0x0E
#else
	rtw_wx_priv_null,				//0x0E
#endif
	rtw_wx_priv_null,				//0x0F

	rtw_p2p_set,					//0x10
	rtw_p2p_get,					//0x11
	rtw_p2p_get2,					//0x12

	NULL,							//0x13
	rtw_tdls,						//0x14
	rtw_wx_priv_null,				//0x15

	rtw_pm_set,						//0x16
	rtw_wx_priv_null,				//0x17
	rtw_rereg_nd_name,				//0x18
	rtw_wx_priv_null,				//0x19

	rtw_mp_efuse_set,				//0x1A
	rtw_mp_efuse_get,				//0x1B
	NULL,							// 0x1C is reserved for hostapd
	rtw_test,						// 0x1D
#ifdef CONFIG_INTEL_WIDI
	rtw_widi_set,					//0x1E
	rtw_widi_set_probe_request,		//0x1F
#endif // CONFIG_INTEL_WIDI
};

#endif // #if defined(CONFIG_MP_INCLUDED) && defined(CONFIG_MP_IWPRIV_SUPPORT)

#if WIRELESS_EXT >= 17	
static struct iw_statistics *rtw_get_wireless_stats(struct net_device *dev)
{
       _adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	   struct iw_statistics *piwstats=&padapter->iwstats;
	int tmp_level = 0;
	int tmp_qual = 0;
	int tmp_noise = 0;

	if (check_fwstate(&padapter->mlmepriv, _FW_LINKED) != _TRUE)
	{
		piwstats->qual.qual = 0;
		piwstats->qual.level = 0;
		piwstats->qual.noise = 0;
		//DBG_871X("No link  level:%d, qual:%d, noise:%d\n", tmp_level, tmp_qual, tmp_noise);
	}
	else{
		#ifdef CONFIG_SIGNAL_DISPLAY_DBM
		tmp_level = translate_percentage_to_dbm(padapter->recvpriv.signal_strength); 
		#else
		tmp_level = padapter->recvpriv.signal_strength;
		#endif
		
		tmp_qual = padapter->recvpriv.signal_qual;
		tmp_noise =padapter->recvpriv.noise;		
		//DBG_871X("level:%d, qual:%d, noise:%d, rssi (%d)\n", tmp_level, tmp_qual, tmp_noise,padapter->recvpriv.rssi);

		piwstats->qual.level = tmp_level;
		piwstats->qual.qual = tmp_qual;
		piwstats->qual.noise = tmp_noise;
	}
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,14))
	piwstats->qual.updated = IW_QUAL_ALL_UPDATED ;//|IW_QUAL_DBM;
#else
#ifdef RTK_DMP_PLATFORM
	//IW_QUAL_DBM= 0x8, if driver use this flag, wireless extension will show value of dbm.
	//remove this flag for show percentage 0~100
	piwstats->qual.updated = 0x07;
#else
	piwstats->qual.updated = 0x0f;
#endif
#endif

	#ifdef CONFIG_SIGNAL_DISPLAY_DBM
	piwstats->qual.updated = piwstats->qual.updated | IW_QUAL_DBM;
	#endif

	return &padapter->iwstats;
}
#endif

struct iw_handler_def rtw_handlers_def =
{
	.standard = rtw_handlers,
	.num_standard = sizeof(rtw_handlers) / sizeof(iw_handler),
	.private = rtw_private_handler,
	.private_args = (struct iw_priv_args *)rtw_private_args,
	.num_private = sizeof(rtw_private_handler) / sizeof(iw_handler),
 	.num_private_args = sizeof(rtw_private_args) / sizeof(struct iw_priv_args),
#if WIRELESS_EXT >= 17
	.get_wireless_stats = rtw_get_wireless_stats,
#endif
};

