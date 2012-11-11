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
#include <rtw_ioctl.h>
#include <rtw_ioctl_set.h>
#include <rtw_ioctl_query.h>
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
	u32 size;

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

	notify_interval = le16_to_cpu(*(u16*)rtw_get_beacon_interval_from_ie(pnetwork->network.IEs));
	notify_capability = le16_to_cpu(*(u16*)rtw_get_capability_from_ie(pnetwork->network.IEs));		

	
	notify_ie = pnetwork->network.IEs+_FIXED_IE_LENGTH_;
	notify_ielen = pnetwork->network.IELength-_FIXED_IE_LENGTH_;
	   
	//notify_signal = (s16)le16_to_cpu(bi->RSSI) * 100;

	//We've set wiphy's signal_type as CFG80211_SIGNAL_TYPE_MBM: signal strength in mBm (100*dBm)
	notify_signal = 100*translate_percentage_to_dbm(pnetwork->network.PhyInfo.SignalStrength);//dbm
		
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

	#ifdef CONFIG_P2P
	if(rtw_get_p2p_ie(pnetwork->network.IEs+12, pnetwork->network.IELength-12, NULL, NULL))
	{
		printk("%s, got p2p_ie\n", __func__);					
	}
	#endif
	

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
				printk("error!, len_information_elements !=  bss->len_proberesp_ies\n");
			}
							
		}
		else if(bss->len_information_elements <  bss->len_beacon_ies)
		{
			bss->information_elements = bss->beacon_ies;			
			bss->len_information_elements =  bss->len_beacon_ies;
		}
	}
*/

	return ret;
	
}

void rtw_cfg80211_indicate_connect(_adapter *padapter)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct wlan_network  *cur_network = &(pmlmepriv->cur_network);
	struct wireless_dev *pwdev = padapter->rtw_wdev;
#ifdef CONFIG_P2P
	struct wifidirect_info *pwdinfo= &(padapter->wdinfo);
#endif


	printk("%s(padapter=%p)\n", __func__, padapter);

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
	if(!rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE))
	{
		rtw_p2p_set_pre_state(pwdinfo, rtw_p2p_state(pwdinfo));
		rtw_p2p_set_role(pwdinfo, P2P_ROLE_CLIENT);
		rtw_p2p_set_state(pwdinfo, P2P_STATE_GONEGO_OK);
		printk("%s, role=%d, p2p_state=%d, pre_p2p_state=%d\n", __func__, rtw_p2p_role(pwdinfo), rtw_p2p_state(pwdinfo), rtw_p2p_pre_state(pwdinfo));
	}
#endif //CONFIG_P2P

	#ifdef CONFIG_LAYER2_ROAMING
	if(pmlmepriv->to_roaming > 0) {
		//rtw_cfg80211_inform_bss(padapter, cur_network);
		DBG_871X("%s call cfg80211_roamed\n", __FUNCTION__);
		cfg80211_roamed(padapter->pnetdev,
			#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 39) || defined(COMPAT_KERNEL_RELEASE)
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
		printk("pwdev->sme_state(b)=%d\n", pwdev->sme_state);
		cfg80211_connect_result(padapter->pnetdev, cur_network->network.MacAddress, NULL, 0, NULL, 0, 
							WLAN_STATUS_SUCCESS, GFP_ATOMIC/*GFP_KERNEL*/);
		printk("pwdev->sme_state(a)=%d\n", pwdev->sme_state);
	}
}

void rtw_cfg80211_indicate_disconnect(_adapter *padapter)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct wireless_dev *pwdev = padapter->rtw_wdev;
#ifdef CONFIG_P2P
	struct wifidirect_info *pwdinfo= &(padapter->wdinfo);
#endif

	printk("%s(padapter=%p)\n", __func__, padapter);

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
	if(!rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE))
	{
		_cancel_timer_ex( &pwdinfo->find_phase_timer );
		_cancel_timer_ex( &pwdinfo->restore_p2p_state_timer );
		_cancel_timer_ex( &pwdinfo->pre_tx_scan_timer);
	
		rtw_p2p_set_state(pwdinfo, rtw_p2p_pre_state(pwdinfo));
		rtw_p2p_set_role(pwdinfo, P2P_ROLE_DEVICE);

		printk("%s, role=%d, p2p_state=%d, pre_p2p_state=%d\n", __func__, rtw_p2p_role(pwdinfo), rtw_p2p_state(pwdinfo), rtw_p2p_pre_state(pwdinfo));
	}
#endif //CONFIG_P2P

	printk("pwdev->sme_state(b)=%d\n", pwdev->sme_state);

	if(pwdev->sme_state==CFG80211_SME_CONNECTING)
		cfg80211_connect_result(padapter->pnetdev, NULL, NULL, 0, NULL, 0, 
							WLAN_STATUS_UNSPECIFIED_FAILURE, GFP_ATOMIC/*GFP_KERNEL*/);
	else if(pwdev->sme_state==CFG80211_SME_CONNECTED)
		cfg80211_disconnected(padapter->pnetdev, 0,
			   				NULL, 0, GFP_ATOMIC);
	//else
		//printk("pwdev->sme_state=%d\n", pwdev->sme_state);

	printk("pwdev->sme_state(a)=%d\n", pwdev->sme_state);

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

	DBG_8192C("%s\n", __FUNCTION__);
	
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

static int rtw_cfg80211_ap_set_encryption(struct net_device *dev, struct ieee_param *param, u32 param_len)
{
	int ret = 0;
	u32 wep_key_idx, wep_key_len,wep_total_len;
	NDIS_802_11_WEP	 *pwep = NULL;
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
			wep_total_len = wep_key_len + FIELD_OFFSET(NDIS_802_11_WEP, KeyMaterial);
		 	pwep =(NDIS_802_11_WEP *)rtw_malloc(wep_total_len);
			if(pwep == NULL){
				DBG_8192C(" r871x_set_encryption: pwep allocate fail !!!\n");
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
			DBG_8192C("wep, set_tx=1\n");

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
			DBG_8192C("wep, set_tx=0\n");
			
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
		//if(param->u.crypt.set_tx == 1)
		if(param->u.crypt.set_tx == 0)
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
						
				set_pairwise_key(padapter, psta);
					
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
#endif

static int rtw_cfg80211_set_encryption(struct net_device *dev, struct ieee_param *param, u32 param_len)
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
		DBG_8192C("wpa_set_encryption, crypt.alg = WEP\n");

		padapter->securitypriv.ndisencryptstatus = Ndis802_11Encryption1Enabled;
		padapter->securitypriv.dot11PrivacyAlgrthm=_WEP40_;
		padapter->securitypriv.dot118021XGrpPrivacy=_WEP40_;

		wep_key_idx = param->u.crypt.idx;
		wep_key_len = param->u.crypt.key_len;

		RT_TRACE(_module_rtl871x_ioctl_os_c,_drv_info_,("(1)wep_key_idx=%d\n", wep_key_idx));
		DBG_8192C("(1)wep_key_idx=%d\n", wep_key_idx);

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
			DBG_8192C("wep, set_tx=1\n");

			if(rtw_set_802_11_add_wep(padapter, pwep) == (u8)_FAIL)
			{
				ret = -EOPNOTSUPP ;
			}
		}
		else
		{
			DBG_8192C("wep, set_tx=0\n");
			
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

		//printk("%s, : dot11AuthAlgrthm == dot11AuthAlgrthm_8021X \n", __func__);

		if (check_fwstate(pmlmepriv, WIFI_STATION_STATE | WIFI_MP_STATE) == _TRUE) //sta mode
		{
			psta = rtw_get_stainfo(pstapriv, get_bssid(pmlmepriv));				
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

	printk("%s, ret=%d\n", __func__, ret);
	
	if (pwep) {
		rtw_mfree((u8 *)pwep,wep_total_len);		
	}	
	
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
	struct wireless_dev *rtw_wdev = wiphy_to_wdev(wiphy);
	_adapter *padapter = wiphy_to_adapter(wiphy);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	
	
	printk("%s(netdev=%p), Adding key for %pM\n", __func__, ndev, mac_addr);

	printk("cipher=0x%x\n", params->cipher);

	printk("key_len=0x%x\n", params->key_len);

	printk("seq_len=0x%x\n", params->seq_len);

	printk("key_index=%d\n", key_index);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)) || defined(COMPAT_KERNEL_RELEASE)
	printk("pairwise=%d\n", pairwise);
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
		_rtw_memcpy(param->u.crypt.seq, params->seq, params->seq_len);
	}

	if(params->key_len && params->key)
	{
		param->u.crypt.key_len = params->key_len;		
		_rtw_memcpy(param->u.crypt.key, params->key, params->key_len);
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
			_rtw_memcpy(param->sta_addr, (void*)mac_addr, ETH_ALEN);
	
		ret = rtw_cfg80211_ap_set_encryption(ndev, param, param_len);
#endif
	}
	else
	{
		printk("error! fw_state=0x%x, iftype=%d\n", pmlmepriv->fw_state, rtw_wdev->iftype);
		
	}

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
	printk("%s\n", __func__);
	return 0;
}

static int cfg80211_rtw_del_key(struct wiphy *wiphy, struct net_device *ndev,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)) || defined(COMPAT_KERNEL_RELEASE)
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
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,38)) || defined(COMPAT_KERNEL_RELEASE)
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
		DBG_871X("%s, mac==%p\n", __func__, mac);
		return -ENOENT;
	}

#ifdef CONFIG_DEBUG_CFG80211
	DBG_871X("%s, mac="MAC_FMT"\n", __func__, MAC_ARG(mac));
#endif

	//for infra./P2PClient mode
	if(	check_fwstate(pmlmepriv, WIFI_STATION_STATE)
		&& check_fwstate(pmlmepriv, _FW_LINKED)
	)
	{
		struct wlan_network  *cur_network = &(pmlmepriv->cur_network);
		
		if (_rtw_memcmp(mac, cur_network->network.MacAddress, ETH_ALEN) == _FALSE)
		{
			DBG_871X("%s, mismatch bssid="MAC_FMT"\n", __func__, MAC_ARG(cur_network->network.MacAddress));
			return -ENOENT;
		}	

			sinfo->filled |= STATION_INFO_SIGNAL;
		sinfo->signal = translate_percentage_to_dbm(padapter->recvpriv.signal_strength);

		sinfo->filled |= STATION_INFO_TX_BITRATE;
		sinfo->txrate.legacy = rtw_get_cur_max_rate(padapter);
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
	
		psta = rtw_get_stainfo(pstapriv, mac);
		if(psta == NULL)
		{
			printk("%s, sta_info is null\n", __func__);
			return -ENOENT;
		}			
		
		//TODO: should acquire station info...
	}

	return 0;
}

extern int netdev_open(struct net_device *pnetdev);
#ifdef CONFIG_CONCURRENT_MODE
extern int netdev_if2_open(struct net_device *pnetdev);
#endif

static int cfg80211_rtw_change_iface(struct wiphy *wiphy,
				     struct net_device *ndev,
				     enum nl80211_iftype type, u32 *flags,
				     struct vif_params *params)
{
	enum nl80211_iftype old_type;
	NDIS_802_11_NETWORK_INFRASTRUCTURE networkType ;
	_adapter *padapter = wiphy_to_adapter(wiphy);
	struct wireless_dev *rtw_wdev = wiphy_to_wdev(wiphy);
#ifdef CONFIG_P2P
	struct wifidirect_info *pwdinfo= &(padapter->wdinfo);
#endif
	int ret = 0;
	u8 change = _FALSE;

#ifdef CONFIG_CONCURRENT_MODE
	if(padapter->isprimary)
	{
	DBG_871X("%s call netdev_open\n", __FUNCTION__);
	if(netdev_open(ndev) != 0) {
		ret= -EPERM;
		goto exit;
	}
	}
	else
	{
		DBG_871X("%s call netdev_if2_open\n", __FUNCTION__);
		if(netdev_if2_open(ndev) != 0) {
			ret= -EPERM;
			goto exit;
		}
	}
#else //CONFIG_CONCURRENT_MODE
	DBG_871X("%s call netdev_open\n", __FUNCTION__);
	if(netdev_open(ndev) != 0) {
		ret= -EPERM;
		goto exit;
	}
#endif //CONFIG_CONCURRENT_MODE


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
		#ifdef CONFIG_P2P
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
		#endif //CONFIG_P2P
		break;
	case NL80211_IFTYPE_AP:
		networkType = Ndis802_11APMode;
		#ifdef CONFIG_P2P
		if(change && !rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE))
		{
			//it means P2P Group created, we will be GO and change mode from  P2P DEVICE to AP(GO)
			rtw_p2p_set_role(pwdinfo, P2P_ROLE_GO);
		}
		#endif //CONFIG_P2P
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
}

void rtw_cfg80211_indicate_scan_done(struct rtw_wdev_priv *pwdev_priv, bool aborted)
{
	_irqL	irqL;

	_enter_critical_bh(&pwdev_priv->scan_req_lock, &irqL);
	if(pwdev_priv->scan_request != NULL)
	{
		//struct cfg80211_scan_request *scan_request = pwdev_priv->scan_request;
	
#ifdef CONFIG_DEBUG_CFG80211	
		DBG_871X("%s with scan req\n", __FUNCTION__);	
#endif

		//avoid WARN_ON(request != wiphy_to_dev(request->wiphy)->scan_req);
		//if(scan_request == wiphy_to_dev(scan_request->wiphy)->scan_req)
		if(pwdev_priv->scan_request->wiphy != pwdev_priv->rtw_wdev->wiphy)
		{
			printk("error wiphy compare\n");
		}
		else
		{
			cfg80211_scan_done(pwdev_priv->scan_request, aborted);
		}
		
		pwdev_priv->scan_request = NULL;
		
	} else {
		DBG_871X("%s without scan req\n", __FUNCTION__);	
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
	struct rtw_wdev_priv *pwdev_priv = wdev_to_priv(padapter->rtw_wdev);
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;

#ifdef CONFIG_DEBUG_CFG80211
	printk("%s\n", __func__);
#endif

#if 0
	if(padapter->pwrctrlpriv.brfoffbyhw && padapter->bDriverStopped)
	{
		return;
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


	wait_status = _FW_UNDER_SURVEY
		#ifndef CONFIG_ANDROID
		|_FW_UNDER_LINKING
		#endif
	;

 	while(check_fwstate(pmlmepriv, wait_status) == _TRUE)
	{	
		rtw_msleep_os(30);
		cnt++;
		if(cnt > wait_for_surveydone )
			break;
	}
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
		if( _TRUE == rtw_is_channel_set_contains_channel(padapter->mlmeextpriv.channel_set, pnetwork->network.Configuration.DSConfig)
			#ifdef CONFIG_VALIDATE_SSID
			&& _TRUE == rtw_validate_ssid(&(pnetwork->network.Ssid))
			#endif
		)
		{		
			//ev=translate_scan(padapter, a, pnetwork, ev, stop);
			rtw_cfg80211_inform_bss(padapter, pnetwork);		
		}

		plist = get_next(plist);
	
	}
	
	_exit_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);
	

	#if 0
	//	Disable P2P Listen State
	if(!rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE))
	{
		_cancel_timer_ex( &pwdinfo->find_phase_timer );
		_cancel_timer_ex( &pwdinfo->restore_p2p_state_timer );
		_cancel_timer_ex( &pwdinfo->pre_tx_scan_timer);

		//rtw_p2p_set_state(pwdinfo, P2P_STATE_NONE);
		rtw_p2p_set_state(pwdinfo, rtw_p2p_pre_state(pwdinfo));
#ifdef CONFIG_DEBUG_CFG80211		
		printk("%s, role=%d, p2p_state=%d\n", __func__, rtw_p2p_role(pwdinfo), rtw_p2p_state(pwdinfo));
#endif

		if(pwrpriv->bips_processing == _FALSE){
			rtw_set_pwr_state_check_timer(pwrpriv);
		}
	}
	#endif

	//call this after other things have been done
	rtw_indicate_scan_done(padapter, _FALSE);
		
}

static int rtw_cfg80211_set_probe_req_wpsp2pie(struct net_device *net, char *buf, int len)
{
	int ret = 0;
	uint wps_ielen = 0;
	u8 *wps_ie;
	u32	p2p_ielen = 0;	
	u8 *p2p_ie;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(net);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);	

#ifdef CONFIG_DEBUG_CFG80211
	printk("%s, ielen=%d\n", __func__, len);
#endif
	
	if(len>0)
	{
		if((wps_ie = rtw_get_wps_ie(buf, len, NULL, &wps_ielen)))
		{
			#ifdef CONFIG_DEBUG_CFG80211
			printk("probe_req_wps_ielen=%d\n", wps_ielen);
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
				printk("%s()-%d: rtw_malloc() ERROR!\n", __FUNCTION__, __LINE__);
				return -EINVAL;
			
			}
			_rtw_memcpy(pmlmepriv->wps_probe_req_ie, wps_ie, wps_ielen);
			pmlmepriv->wps_probe_req_ie_len = wps_ielen;
		}

		buf += wps_ielen;
		len -= wps_ielen;

		#ifdef CONFIG_P2P
		if((p2p_ie=rtw_get_p2p_ie(buf, len, NULL, &p2p_ielen))) 
		{
			#ifdef CONFIG_DEBUG_CFG80211
			printk("probe_req_p2p_ielen=%d\n", p2p_ielen);
			#endif
			
			if(pmlmepriv->p2p_probe_req_ie)
			{
				u32 free_len = pmlmepriv->p2p_probe_req_ie_len;
				pmlmepriv->p2p_probe_req_ie_len = 0;
				rtw_mfree(pmlmepriv->p2p_probe_req_ie, free_len);
				pmlmepriv->p2p_probe_req_ie = NULL;
			}	

			pmlmepriv->p2p_probe_req_ie = rtw_malloc(len);
			if ( pmlmepriv->p2p_probe_req_ie == NULL) {
				printk("%s()-%d: rtw_malloc() ERROR!\n", __FUNCTION__, __LINE__);
				return -EINVAL;
			
			}
			_rtw_memcpy(pmlmepriv->p2p_probe_req_ie, p2p_ie, p2p_ielen);
			pmlmepriv->p2p_probe_req_ie_len = p2p_ielen;
		}
		#endif //CONFIG_P2P
		
	}

	return ret;
	
}

void rtw_cfg80211_scan_abort(_adapter *padapter)
{
	u32 cnt=0;
	u32 wait_for_surveydone;
	struct mlme_priv	*pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);

//#ifdef CONFIG_DEBUG_CFG80211
	printk("%s\n", __func__);
//#endif

	wait_for_surveydone = 10;
			
	pmlmeext->scan_abort = _TRUE;

	while(check_fwstate(pmlmepriv, _FW_UNDER_SURVEY) == _TRUE)
	{	
		printk("%s : fw_state=_FW_UNDER_SURVEY!\n", __func__);
		
		rtw_msleep_os(20);
		cnt++;
		if(cnt > wait_for_surveydone )
		{
			printk("waiting for scan_abort time out!\n");
		
#ifdef CONFIG_PLATFORM_MSTAR_TITANIA12	
			//_clr_fwstate_(pmlmepriv, _FW_UNDER_SURVEY);
			set_survey_timer(pmlmeext, 0);
			_set_timer(&pmlmepriv->scan_to_timer, 50);
#endif		
			break;
		}	
	}

	pmlmeext->scan_abort = _FALSE;

	rtw_cfg80211_indicate_scan_done(wdev_to_priv(padapter->rtw_wdev), _TRUE);

}

static int cfg80211_rtw_scan(struct wiphy *wiphy, struct net_device *ndev,
			     struct cfg80211_scan_request *request)
{
	int i;
	u8 _status = _FALSE;
	int ret = 0;	
	_adapter *padapter = wiphy_to_adapter(wiphy);
	struct mlme_priv *pmlmepriv= &padapter->mlmepriv;
	NDIS_802_11_SSID ssid[RTW_SSID_SCAN_AMOUNT];
	_irqL	irqL;
	u8 *wps_ie=NULL;
	uint wps_ielen=0;	
	u8 *p2p_ie=NULL;
	uint p2p_ielen=0;
#ifdef CONFIG_P2P
	struct wifidirect_info *pwdinfo= &(padapter->wdinfo);	
#endif //CONFIG_P2P
	struct rtw_wdev_priv *pwdev_priv = wdev_to_priv(padapter->rtw_wdev);
	struct cfg80211_ssid *ssids = request->ssids;
	int social_channel = 0, j = 0;
	bool need_indicate_scan_done = _FALSE;
#ifdef CONFIG_CONCURRENT_MODE	
	PADAPTER pbuddy_adapter = padapter->pbuddy_adapter;
	struct mlme_priv *pbuddy_mlmepriv = &(pbuddy_adapter->mlmepriv);	
#endif

#ifdef CONFIG_DEBUG_CFG80211
	printk("%s(netdev=%p)\n", __func__, ndev);
#endif

#ifdef CONFIG_MP_INCLUDED
	if (check_fwstate(pmlmepriv, WIFI_MP_STATE) == _TRUE)
	{
		ret = -EPERM;
		goto exit;
	}
#endif

	_enter_critical_bh(&pwdev_priv->scan_req_lock, &irqL);
	pwdev_priv->scan_request = request;
	_exit_critical_bh(&pwdev_priv->scan_req_lock, &irqL);

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE)
	{

#ifdef CONFIG_DEBUG_CFG80211
		DBG_871X("%s under WIFI_AP_STATE\n", __FUNCTION__);
#endif
		//need_indicate_scan_done = _TRUE;
		//goto check_need_indicate_scan_done;
	}

	if(_FAIL == rtw_pwr_wakeup(padapter)) {
		need_indicate_scan_done = _TRUE;
		goto check_need_indicate_scan_done;
	}

	#ifdef CONFIG_P2P
	if( ssids->ssid != NULL )
	{
		if( _rtw_memcmp(ssids->ssid, "DIRECT-", 7)
			&& rtw_get_p2p_ie((u8 *)request->ie, request->ie_len, NULL, NULL)
		)
		{
			if(rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE))
			{
				rtw_p2p_enable(padapter, P2P_ROLE_DEVICE);
				wdev_to_priv(padapter->rtw_wdev)->p2p_enabled = _TRUE;
			}
			else
			{
				rtw_p2p_set_pre_state(pwdinfo, rtw_p2p_state(pwdinfo));
				#ifdef CONFIG_DEBUG_CFG80211
				printk("%s, role=%d, p2p_state=%d\n", __func__, rtw_p2p_role(pwdinfo), rtw_p2p_state(pwdinfo));
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
		rtw_cfg80211_set_probe_req_wpsp2pie( ndev, (u8 *)request->ie, request->ie_len );
	}

	if (pmlmepriv->LinkDetectInfo.bBusyTraffic == _TRUE)
	{
		printk("%s, bBusyTraffic == _TRUE\n", __func__);
		need_indicate_scan_done = _TRUE;
		goto check_need_indicate_scan_done;
	} 

#ifdef CONFIG_CONCURRENT_MODE
	if(pbuddy_mlmepriv->LinkDetectInfo.bBusyTraffic == _TRUE)	
	{
		printk("%s, bBusyTraffic == _TRUE at buddy_intf\n", __func__);
		need_indicate_scan_done = _TRUE;
		goto check_need_indicate_scan_done;
	} 
#endif

	if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY|_FW_UNDER_LINKING) == _TRUE)
	{
		printk("%s, fwstate=0x%x\n", __func__, pmlmepriv->fw_state);
		need_indicate_scan_done = _TRUE;
		goto check_need_indicate_scan_done;
	} 

#ifdef CONFIG_CONCURRENT_MODE
	if (check_fwstate(pbuddy_mlmepriv, _FW_UNDER_SURVEY|_FW_UNDER_LINKING) == _TRUE)
	{		
		if(check_fwstate(pbuddy_mlmepriv, _FW_UNDER_SURVEY))
		{
			printk("scanning_via_buddy_intf\n");
			pmlmepriv->scanning_via_buddy_intf = _TRUE;
		}		

		printk("buddy_intf is now scanning or connecting\n");
		
		need_indicate_scan_done = _TRUE;
		goto check_need_indicate_scan_done;
	}
#endif


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
		printk("ssid=%s, len=%d\n", ssids[i].ssid, ssids[i].ssid_len);
		#endif
		_rtw_memcpy(ssid[i].Ssid, ssids[i].ssid, ssids[i].ssid_len);
		ssid[i].SsidLength = ssids[i].ssid_len;	
	}
	

#ifdef CONFIG_DEBUG_CFG80211
	//parsing channels, n_channels
	DBG_871X("%s n_channels:%u\n", __FUNCTION__, request->n_channels);
#endif

	_enter_critical_bh(&pmlmepriv->lock, &irqL);	
	_status = rtw_sitesurvey_cmd(padapter, ssid, RTW_SSID_SCAN_AMOUNT);
	_exit_critical_bh(&pmlmepriv->lock, &irqL);


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
#ifdef CONFIG_P2P
	struct wifidirect_info* pwdinfo = &padapter->wdinfo;
#endif //CONFIG_P2P
	int wpa_ielen=0;
	int wpa2_ielen=0;
	u8 *pwpa, *pwpa2;


	if((ielen > MAX_WPA_IE_LEN+MAX_WPS_IE_LEN+MAX_P2P_IE_LEN) || (pie == NULL)){
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
			DBG_8192C("set wpa_ie(length:%d):\n", ielen);
			for(i=0;i<ielen;i=i+8)
				DBG_8192C("0x%.2x 0x%.2x 0x%.2x 0x%.2x 0x%.2x 0x%.2x 0x%.2x 0x%.2x \n",buf[i],buf[i+1],buf[i+2],buf[i+3],buf[i+4],buf[i+5],buf[i+6],buf[i+7]);
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

		pwpa = rtw_get_wpa_ie(buf, &wpa_ielen, ielen);
		pwpa2 = rtw_get_wpa2_ie(buf, &wpa2_ielen, ielen);

		if(pwpa && wpa_ielen>0)
		{
			if(rtw_parse_wpa_ie(pwpa, wpa_ielen+2, &group_cipher, &pairwise_cipher) == _SUCCESS)
			{
				padapter->securitypriv.dot11AuthAlgrthm= dot11AuthAlgrthm_8021X;
				padapter->securitypriv.ndisauthtype=Ndis802_11AuthModeWPAPSK;
				_rtw_memcpy(padapter->securitypriv.supplicant_ie, &pwpa[0], wpa_ielen+2);
				
				printk("got wpa_ie\n");
			}
		}

		if(pwpa2 && wpa2_ielen>0)
		{
			if(rtw_parse_wpa2_ie(pwpa2, wpa2_ielen+2, &group_cipher, &pairwise_cipher) == _SUCCESS)
			{
				padapter->securitypriv.dot11AuthAlgrthm= dot11AuthAlgrthm_8021X;
				padapter->securitypriv.ndisauthtype=Ndis802_11AuthModeWPA2PSK;	
				_rtw_memcpy(padapter->securitypriv.supplicant_ie, &pwpa2[0], wpa2_ielen+2);

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
			uint wps_ielen=0;			
			u8 *pwps;

			pwps = rtw_get_wps_ie(buf, ielen, NULL, &wps_ielen);
			 
			//while( cnt < ielen )
			while( cnt < wps_ielen )
			{
				//eid = buf[cnt];
				eid = pwps[cnt];
		
				if((eid==_VENDOR_SPECIFIC_IE_)&&(_rtw_memcmp(&pwps[cnt+2], wps_oui, 4)==_TRUE))
				{
					DBG_8192C("SET WPS_IE\n");

					padapter->securitypriv.wps_ie_len = ( (pwps[cnt+1]+2) < (MAX_WPA_IE_LEN<<2)) ? (pwps[cnt+1]+2):(MAX_WPA_IE_LEN<<2);
					
					_rtw_memcpy(padapter->securitypriv.wps_ie, &pwps[cnt], padapter->securitypriv.wps_ie_len);
					
					if(pwpa==NULL && pwpa2==NULL)
					{
						padapter->securitypriv.wps_phase = _TRUE;
					
						DBG_8192C("SET WPS_IE, wps_phase==_TRUE\n");
					}					
#ifdef CONFIG_P2P
					if(rtw_p2p_chk_state(pwdinfo, P2P_STATE_GONEGO_OK))
					{
						//rtw_p2p_set_state(pwdinfo, P2P_STATE_PROVISIONING_ING);
					}
#endif //CONFIG_P2P

					cnt += pwps[cnt+1]+2;
					
					break;
				} else {
					cnt += pwps[cnt+1]+2; //goto next	
				}				
			}			
		}//set wps_ie

		#ifdef CONFIG_P2P
		{//check p2p_ie for assoc req; 
			uint p2p_ielen=0;
			u8 *p2p_ie;
			struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);	

			if((p2p_ie=rtw_get_p2p_ie(buf, ielen, NULL, &p2p_ielen)))
			{
				#ifdef CONFIG_DEBUG_CFG80211
				printk("%s p2p_assoc_req_ielen=%d\n", __FUNCTION__, p2p_ielen);
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
					printk("%s()-%d: rtw_malloc() ERROR!\n", __FUNCTION__, __LINE__);
					goto exit;
			
				}
				_rtw_memcpy(pmlmepriv->p2p_assoc_req_ie, p2p_ie, p2p_ielen);
				pmlmepriv->p2p_assoc_req_ie_len = p2p_ielen;
				
			}
		}
		#endif //CONFIG_P2P
		
	}
	
	
	RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_info_,
		 ("rtw_set_wpa_ie: pairwise_cipher=0x%08x padapter->securitypriv.ndisencryptstatus=%d padapter->securitypriv.ndisauthtype=%d\n",
		  pairwise_cipher, padapter->securitypriv.ndisencryptstatus, padapter->securitypriv.ndisauthtype));
 	
exit:

	if (buf) rtw_mfree(buf, ielen);
	
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


	if(wdev_to_priv(padapter->rtw_wdev)->block == _TRUE)
	{
		ret = -EBUSY;
		DBG_871X("%s wdev_priv.block is set\n", __FUNCTION__);
		goto exit;
	}

#ifdef CONFIG_PLATFORM_MSTAR_TITANIA12
	printk("MStar Android!\n");
	if((wdev_to_priv(padapter->rtw_wdev))->bandroid_scan == _FALSE)
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

	if(_FAIL == rtw_pwr_wakeup(padapter)) {
		ret= -EPERM;
		goto exit;
	}

	if(check_fwstate(pmlmepriv, WIFI_AP_STATE)) {
		ret = -EPERM;
		goto exit;
	}

#ifdef CONFIG_CONCURRENT_MODE
	if (check_buddy_fwstate(padapter, _FW_UNDER_SURVEY|_FW_UNDER_LINKING) == _TRUE)
	{		
		printk("%s, but buddy_intf is under scanning or linking\n", __FUNCTION__);
		
		ret = -EINVAL;
		
		goto exit;
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

	DBG_8192C("ssid=%s, len=%d\n", ndis_ssid.Ssid, sme->ssid_len);
	

	if (sme->bssid)
		printk("bssid="MAC_FMT"\n", MAC_ARG(sme->bssid));


	if(check_fwstate(pmlmepriv, _FW_UNDER_SURVEY|_FW_UNDER_LINKING) == _TRUE) 
	{	
		ret = -EBUSY;
		printk("%s, fw_state=0x%x, goto exit\n", __FUNCTION__, pmlmepriv->fw_state);
		goto exit;		
	}	
	

	_enter_critical_bh(&queue->lock, &irqL);
	
	phead = get_list_head(queue);
	pmlmepriv->pscanned = get_next(phead);

	while (1)
	{			
		if (rtw_end_of_queue_search(phead, pmlmepriv->pscanned) == _TRUE)
		{
			break;
		}
	
		pnetwork = LIST_CONTAINOR(pmlmepriv->pscanned, struct wlan_network, list);
		pmlmepriv->pscanned = get_next(pmlmepriv->pscanned);

		dst_ssid = pnetwork->network.Ssid.Ssid;
		dst_bssid = pnetwork->network.MacAddress;

		if(sme->bssid)  {
			if(_rtw_memcmp(pnetwork->network.MacAddress, sme->bssid, ETH_ALEN) == _FALSE)
				continue;
		}
		
		if(sme->ssid && sme->ssid_len) {
			if(	pnetwork->network.Ssid.SsidLength != sme->ssid_len
				|| _rtw_memcmp(pnetwork->network.Ssid.Ssid, sme->ssid, sme->ssid_len) == _FALSE
			)
				continue;
		}
			

		if (sme->bssid)
		{
			src_bssid = sme->bssid;

			if ((_rtw_memcmp(dst_bssid, src_bssid, ETH_ALEN)) == _TRUE)
			{
				printk("matched by bssid\n");

				ndis_ssid.SsidLength = pnetwork->network.Ssid.SsidLength;				
				_rtw_memcpy(ndis_ssid.Ssid, pnetwork->network.Ssid.Ssid, pnetwork->network.Ssid.SsidLength);
				
				matched=_TRUE;
				break;
			}
		
		}
		else if (sme->ssid && sme->ssid_len)
		{		
			src_ssid = ndis_ssid.Ssid;

			if ((_rtw_memcmp(dst_ssid, src_ssid, ndis_ssid.SsidLength) == _TRUE) &&
				(pnetwork->network.Ssid.SsidLength==ndis_ssid.SsidLength))
			{
				printk("matched by ssid\n");
				matched=_TRUE;
				break;
			}		
		}			
			
	}
	
	_exit_critical_bh(&queue->lock, &irqL);

	if((matched == _FALSE) || (pnetwork== NULL))
	{
		ret = -ENOENT;
		printk("connect, matched == _FALSE, goto exit\n");
		goto exit;
	}


	if (rtw_set_802_11_infrastructure_mode(padapter, pnetwork->network.InfrastructureMode) == _FALSE)
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

	printk("%s, ie_len=%d\n", __func__, sme->ie_len);
			
	ret = rtw_cfg80211_set_wpa_ie(padapter, sme->ie, sme->ie_len);
	if (ret < 0)
		goto exit;

	authmode = psecuritypriv->ndisauthtype;
	rtw_set_802_11_authentication_mode(padapter, authmode);

	//rtw_set_802_11_encryption_mode(padapter, padapter->securitypriv.ndisencryptstatus);

	if (rtw_set_802_11_ssid(padapter, &ndis_ssid) == _FALSE) {
		ret = -1;
		goto exit;
	}


	printk("set ssid:dot11AuthAlgrthm=%d, dot11PrivacyAlgrthm=%d, dot118021XGrpPrivacy=%d\n", psecuritypriv->dot11AuthAlgrthm, psecuritypriv->dot11PrivacyAlgrthm, psecuritypriv->dot118021XGrpPrivacy);
	
exit:

	DBG_8192C("<=%s, ret %d\n",__FUNCTION__, ret);
	
	return ret;
}

static int cfg80211_rtw_disconnect(struct wiphy *wiphy, struct net_device *dev,
				   u16 reason_code)
{
	_adapter *padapter = wiphy_to_adapter(wiphy);

	printk("\n%s(netdev=%p)\n", __func__, dev);

	if(check_fwstate(&padapter->mlmepriv, _FW_LINKED)) 
	{
		rtw_disassoc_cmd(padapter);
		
		DBG_871X("%s...call rtw_indicate_disconnect\n ", __FUNCTION__);
		
		rtw_indicate_disconnect(padapter);
		
		rtw_free_assoc_resources(padapter, 1);
	}
	
	return 0;
}

static int cfg80211_rtw_set_txpower(struct wiphy *wiphy,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,36)) || defined(COMPAT_KERNEL_RELEASE)
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
	u8	index,blInserted = _FALSE;
	_adapter	*padapter = wiphy_to_adapter(wiphy);
	struct security_priv	*psecuritypriv = &padapter->securitypriv;
	u8	strZeroMacAddress[ ETH_ALEN ] = { 0x00 };

	DBG_871X("%s\n", __func__);

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
			DBG_871X( "[%s] BSSID exists in the PMKList.\n", __func__);

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
		DBG_871X( "[%s] Use the new entry index = %d for this PMKID.\n", __func__, psecuritypriv->PMKIDIndex );

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
				  struct net_device *netdev,
				  struct cfg80211_pmksa *pmksa)
{
	u8	index, bMatched = _FALSE;
	_adapter	*padapter = wiphy_to_adapter(wiphy);
	struct security_priv	*psecuritypriv = &padapter->securitypriv;

	DBG_871X("%s\n", __func__);

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
		DBG_871X("[%s] do not have matched BSSID\n", __func__);
		return -EINVAL;
	}

	return 0;
}

static int cfg80211_rtw_flush_pmksa(struct wiphy *wiphy,
				    struct net_device *netdev)
{
	_adapter	*padapter = wiphy_to_adapter(wiphy);
	struct security_priv	*psecuritypriv = &padapter->securitypriv;

	DBG_871X("%s\n", __func__);

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

	printk("%s(padapter=%p)\n", __func__, padapter);

	channel = pmlmeext->cur_channel;
	
	if (channel <= RTW_CH_MAX_2G_CHANNEL)
	{		
		freq = rtw_ieee80211_channel_to_frequency(channel, IEEE80211_BAND_2GHZ);
	}	
	else
	{		
		freq = rtw_ieee80211_channel_to_frequency(channel, IEEE80211_BAND_5GHZ);
	}

#ifdef COMPAT_KERNEL_RELEASE
	rtw_cfg80211_rx_mgmt(padapter->pnetdev, freq, 0, pmgmt_frame, frame_len, GFP_ATOMIC);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)) && !defined(CONFIG_CFG80211_FORCE_COMPATIBLE_2_6_37_UNDER)
	rtw_cfg80211_rx_mgmt(padapter->pnetdev, freq, 0, pmgmt_frame, frame_len, GFP_ATOMIC);
#else //COMPAT_KERNEL_RELEASE
	//to avoid WARN_ON(wdev->iftype != NL80211_IFTYPE_STATION)  when calling cfg80211_send_rx_assoc()
	#ifndef CONFIG_PLATFORM_MSTAR_TITANIA12	
	pwdev->iftype = NL80211_IFTYPE_STATION;
	#endif //CONFIG_PLATFORM_MSTAR_TITANIA12
	printk("iftype=%d before call cfg80211_send_rx_assoc()\n", pwdev->iftype);
	rtw_cfg80211_send_rx_assoc(padapter->pnetdev, NULL, pmgmt_frame, frame_len);
	printk("iftype=%d after call cfg80211_send_rx_assoc()\n", pwdev->iftype);
	pwdev->iftype = NL80211_IFTYPE_AP;
	//cfg80211_rx_action(padapter->pnetdev, freq, pmgmt_frame, frame_len, GFP_ATOMIC);
#endif //COMPAT_KERNEL_RELEASE

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
	
	
	printk("%s(padapter=%p)\n", __func__, padapter);

	channel = pmlmeext->cur_channel;
	
	if (channel <= RTW_CH_MAX_2G_CHANNEL)
	{		
		freq = rtw_ieee80211_channel_to_frequency(channel, IEEE80211_BAND_2GHZ);
	}	
	else
	{		
		freq = rtw_ieee80211_channel_to_frequency(channel, IEEE80211_BAND_5GHZ);
	}	


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
	rtw_cfg80211_rx_mgmt(padapter->pnetdev, freq, 0, mgmt_buf, frame_len, GFP_ATOMIC);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)) && !defined(CONFIG_CFG80211_FORCE_COMPATIBLE_2_6_37_UNDER)
	rtw_cfg80211_rx_mgmt(padapter->pnetdev, freq, 0, mgmt_buf, frame_len, GFP_ATOMIC);
#else //COMPAT_KERNEL_RELEASE
	cfg80211_send_disassoc(padapter->pnetdev, mgmt_buf, frame_len);	
	//cfg80211_rx_action(padapter->pnetdev, freq, mgmt_buf, frame_len, GFP_ATOMIC);
#endif //COMPAT_KERNEL_RELEASE
	
}

static int rtw_cfg80211_monitor_if_open(struct net_device *ndev)
{
	int ret = 0;

	printk("%s\n", __func__);
	
	return ret;
}

static int rtw_cfg80211_monitor_if_close(struct net_device *ndev)
{
	int ret = 0;

	printk("%s\n", __func__);
	
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
	unsigned short frame_ctl;
	unsigned char src_mac_addr[6];
	unsigned char dst_mac_addr[6];
	struct ieee80211_hdr *dot11_hdr;
	struct ieee80211_radiotap_header *rtap_hdr;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(ndev);
	
	printk("%s(netdev=%p)\n", __func__, ndev);

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
		printk("radiotap len (should be 14): %d\n", rtap_len);
		goto fail;
	}	

	/* Skip the ratio tap header */
	skb_pull(skb, rtap_len);

	dot11_hdr = (struct ieee80211_hdr *)skb->data;
	frame_ctl = le16_to_cpu(dot11_hdr->frame_control);
	/* Check if the QoS bit is set */
	if ((frame_ctl & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_DATA) {
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

		printk("should be eapol packet\n");

		/* Use the real net device to transmit the packet */		
		ret =  rtw_xmit_entry(skb, padapter->pnetdev);

		return ret;

	}
	else if((frame_ctl & (IEEE80211_FCTL_FTYPE|IEEE80211_FCTL_STYPE)) == cpu_to_le16(IEEE80211_STYPE_ACTION)) 
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

		#ifdef CONFIG_P2P
		if(rtw_p2p_check_frames(padapter, buf, len, _TRUE) < 0)
		{
			goto fail;
		}
		#endif
	
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
		printk("frame_ctl=0x%x\n", frame_ctl & (IEEE80211_FCTL_FTYPE|IEEE80211_FCTL_STYPE));
	}

	
fail:
	
	dev_kfree_skb(skb);

	return 0;
	
}

static void rtw_cfg80211_monitor_if_set_multicast_list(struct net_device *ndev)
{
	printk("%s\n", __func__);
}

static int rtw_cfg80211_monitor_if_set_mac_address(struct net_device *ndev, void *addr)
{
	int ret = 0;
	
	printk("%s\n", __func__);
	
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

static struct net_device *rtw_cfg80211_add_monitor_if(_adapter *padapter, char *name)
{
	int ret = 0;
	struct net_device* ndev = NULL;
	struct rtw_netdev_priv_indicator *pnpi;
	struct rtw_wdev_priv *pwdev_priv = wdev_to_priv(padapter->rtw_wdev);

	printk("%s\n", __func__);
		
	if (!name ) {		
		ret = -EINVAL;
		goto out;
	}

	if((strnicmp(name, pwdev_priv->ifname_mon, strlen(name)) ==0)
		&& pwdev_priv->pmon_ndev)
	{
		ndev = pwdev_priv->pmon_ndev;

		printk("%s, monitor interface(%s) has existed\n", __func__, name);
		
		goto out;
	}
		
	
	ndev = alloc_etherdev(sizeof(struct rtw_netdev_priv_indicator));
	if (!ndev) {		
		ret = -ENOMEM;
		goto out;
	}

	ndev->type = ARPHRD_IEEE80211_RADIOTAP;
	strncpy(ndev->name, name, IFNAMSIZ);
	ndev->name[IFNAMSIZ - 1] = 0;
	
#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,29))	
	ndev->netdev_ops = &rtw_cfg80211_monitor_if_ops;
#else
	ndev->open = rtw_cfg80211_monitor_if_open;
	ndev->stop = rtw_cfg80211_monitor_if_close;	
	ndev->hard_start_xmit = rtw_cfg80211_monitor_if_xmit_entry;
	ndev->set_mac_address = rtw_cfg80211_monitor_if_set_mac_address;
#endif

	pnpi = netdev_priv(ndev);	
	pnpi->priv = padapter;
	pnpi->sizeof_priv = sizeof(_adapter);

	ret = register_netdevice(ndev);
	if (ret) {		
		goto out;
	}

	pwdev_priv->pmon_ndev = ndev;	
	_rtw_memcpy(pwdev_priv->ifname_mon, name, IFNAMSIZ+1);

out:
	if (ret && ndev)
	{		
		free_netdev(ndev);
		ndev = NULL;
	}
	
	
	printk("%s, ndev=%p, pmon_ndev=%p, ret=%d\n", __func__, ndev, pwdev_priv->pmon_ndev, ret);
	
	return ndev;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,38)) || defined(COMPAT_KERNEL_RELEASE)
static struct net_device * cfg80211_rtw_add_virtual_intf(struct wiphy *wiphy, char *name,
#else	// (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,38))
static int	cfg80211_rtw_add_virtual_intf(struct wiphy *wiphy, char *name,
#endif	// (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,38))
				    enum nl80211_iftype type, u32 *flags,
				    struct vif_params *params)
{
	struct net_device* ndev = NULL;
	_adapter *padapter = wiphy_to_adapter(wiphy);	

	printk("%s(padapter=%p), ifname=%s, type=%d\n", __func__, padapter, name, type);
	
	
	switch (type) {
	case NL80211_IFTYPE_ADHOC:
	case NL80211_IFTYPE_AP_VLAN:
	case NL80211_IFTYPE_WDS:
	case NL80211_IFTYPE_MESH_POINT:
		
		break;
	case NL80211_IFTYPE_MONITOR:
		ndev = rtw_cfg80211_add_monitor_if(padapter, name);		
		break;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)) || defined(COMPAT_KERNEL_RELEASE)
	case NL80211_IFTYPE_P2P_CLIENT:
#endif
	case NL80211_IFTYPE_STATION:
		
		break;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)) || defined(COMPAT_KERNEL_RELEASE)
	case NL80211_IFTYPE_P2P_GO:
#endif
	case NL80211_IFTYPE_AP:
		
		break;
	default:
		printk("Unsupported interface type\n");
		break;
	}

	printk("ndev=%p\n", ndev);
		
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,38)) || defined(COMPAT_KERNEL_RELEASE)
	return ndev;
#else	// (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,38))
	return 0;
#endif	// (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,38))
	
}

static int	cfg80211_rtw_del_virtual_intf(struct wiphy *wiphy, struct net_device *dev)
{
	_adapter *padapter = wiphy_to_adapter(wiphy);
	struct rtw_wdev_priv *pwdev_priv = (struct rtw_wdev_priv *)wiphy_priv(wiphy);

	printk("%s(netdev=%p)\n", __func__, dev);

	if(dev)
	{
		unregister_netdev(dev);
		
		free_netdev(dev);

		if(dev == pwdev_priv->pmon_ndev)
		{
			printk("remove monitor interface\n");
			pwdev_priv->pmon_ndev = 	NULL;
			pwdev_priv->ifname_mon[0] = '\0';
		}		
	}	
	
	return 0;
}

static int rtw_add_beacon(_adapter *adapter, const u8 *head, size_t head_len, const u8 *tail, size_t tail_len)
{
	int ret=0;
	u8 *pbuf = NULL;
	uint len, wps_ielen=0;	
	uint p2p_ielen=0;
	u8 *p2p_ie;
	struct mlme_priv *pmlmepriv = &(adapter->mlmepriv);
	//struct sta_priv *pstapriv = &padapter->stapriv;
	

	printk("%s, beacon_head_len=%d, beacon_tail_len=%d\n", __FUNCTION__, head_len, tail_len);

	
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
		printk("add bcn, wps_ielen=%d\n", wps_ielen);

#ifdef CONFIG_P2P
	//check p2p ie if inclued
	if(rtw_get_p2p_ie(pbuf+_FIXED_IE_LENGTH_, len-_FIXED_IE_LENGTH_, NULL, &p2p_ielen))
		printk("got p2p_ie, len=%d\n", p2p_ielen);
#endif
	
	// pbss_network->IEs will not include p2p_ie
	if(rtw_check_beacon_data(adapter, pbuf,  len-p2p_ielen) == _SUCCESS)
	//if(rtw_check_beacon_data(padapter, pbuf,  len) == _SUCCESS)
	{
#ifdef  CONFIG_P2P		
		//check p2p if enable
		if((p2p_ie=rtw_get_p2p_ie(pbuf+_FIXED_IE_LENGTH_, len-_FIXED_IE_LENGTH_, NULL, &p2p_ielen)))
		{
			struct mlme_ext_priv *pmlmeext = &adapter->mlmeextpriv;
			struct wifidirect_info *pwdinfo= &(adapter->wdinfo);
		
			if(rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE))
			{			
				printk("Enable P2P function for the first time\n");
				rtw_p2p_enable(adapter, P2P_ROLE_GO);
				wdev_to_priv(adapter->rtw_wdev)->p2p_enabled = _TRUE;
			}
			else
			{
				_cancel_timer_ex( &pwdinfo->find_phase_timer );
				_cancel_timer_ex( &pwdinfo->restore_p2p_state_timer );
				_cancel_timer_ex( &pwdinfo->pre_tx_scan_timer);	

				printk("enter GO Mode, p2p_ielen=%d\n", p2p_ielen);			

				rtw_p2p_set_role(pwdinfo, P2P_ROLE_GO);
				rtw_p2p_set_state(pwdinfo, P2P_STATE_GONEGO_OK);
				pwdinfo->intent = 15;		
			}				

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

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,4,0))
static int	cfg80211_rtw_add_beacon(struct wiphy *wiphy, struct net_device *dev,
			      struct beacon_parameters *info)
{
	int ret=0;
	_adapter *adapter = wiphy_to_adapter(wiphy);

	ret = rtw_add_beacon(adapter, info->head, info->head_len, info->tail, info->tail_len);

	return ret;
}

static int	cfg80211_rtw_set_beacon(struct wiphy *wiphy, struct net_device *dev,
			      struct beacon_parameters *info)
{
	_adapter *padapter = wiphy_to_adapter(wiphy);	
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	
	printk("%s\n", __func__);

	pmlmeext->bstart_bss = _TRUE;

	cfg80211_rtw_add_beacon(wiphy, dev, info);
	
	return 0;
}

static int	cfg80211_rtw_del_beacon(struct wiphy *wiphy, struct net_device *dev)
{
	DBG_871X("%s\n", __func__);
	
	return 0;
}
#else
static int cfg80211_rtw_start_ap(struct wiphy *wiphy, struct net_device *dev,
								struct cfg80211_ap_settings *settings)
{
	int ret = 0;
	_adapter *adapter = wiphy_to_adapter(wiphy);
	
	DBG_871X("%s\n", __func__);
	
	ret = rtw_add_beacon(adapter, settings->beacon.head, settings->beacon.head_len,
		settings->beacon.tail, settings->beacon.tail_len);

	return ret;
}

static int cfg80211_rtw_change_beacon(struct wiphy *wiphy, struct net_device *dev,
                                struct cfg80211_beacon_data *info)
{
	int ret = 0;
	_adapter *adapter = wiphy_to_adapter(wiphy);

	DBG_871X("%s\n", __func__);

	ret = rtw_add_beacon(adapter, info->head, info->head_len, info->tail, info->tail_len);

	return ret;
}

static int cfg80211_rtw_stop_ap(struct wiphy *wiphy, struct net_device *dev)
{
	DBG_871X("%s\n", __func__);
	return 0;
}

#endif //(LINUX_VERSION_CODE < KERNEL_VERSION(3,4,0))

static int	cfg80211_rtw_add_station(struct wiphy *wiphy, struct net_device *dev,
			       u8 *mac, struct station_parameters *params)
{
	printk("%s\n", __func__);
	
	return 0;
}

static int	cfg80211_rtw_del_station(struct wiphy *wiphy, struct net_device *dev,
			       u8 *mac)
{
	int ret=0;	
	_irqL irqL;
	_list	*phead, *plist;
	struct sta_info *psta = NULL;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct sta_priv *pstapriv = &padapter->stapriv;

	printk("+%s\n", __func__);

	if(check_fwstate(pmlmepriv, (_FW_LINKED|WIFI_AP_STATE)) != _TRUE)		
	{
		printk("%s, fw_state != FW_LINKED|WIFI_AP_STATE\n", __func__);
		return -EINVAL;		
	}


	if(!mac)
	{
		printk("flush all sta, and cam_entry\n");

		flush_all_cam_entry(padapter);	//clear CAM

		ret = rtw_sta_flush(padapter);
		
		return ret;
	}	


	printk("free sta macaddr =" MAC_FMT "\n", MAC_ARG(mac));

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

				//_exit_critical_bh(&pstapriv->asoc_list_lock, &irqL);
				ap_free_sta(padapter, psta);
				//_enter_critical_bh(&pstapriv->asoc_list_lock, &irqL);

				psta = NULL;

				break;
			}		
					
		}
		
	}

	_exit_critical_bh(&pstapriv->asoc_list_lock, &irqL);

	
#if 0
	psta = rtw_get_stainfo(pstapriv, mac);
	if(psta)
	{
		//DBG_8192C("free psta=%p, aid=%d\n", psta, psta->aid);
	
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
		DBG_8192C("cfg80211_rtw_del_station(), sta has already been removed or never been added\n");
		
		//ret = -1;
	}
#endif	
	
	printk("-%s\n", __func__);
	
	return ret;	

}

static int	cfg80211_rtw_change_station(struct wiphy *wiphy, struct net_device *dev,
				  u8 *mac, struct station_parameters *params)
{
	printk("%s\n", __func__);
	
	return 0;
}

static int	cfg80211_rtw_dump_station(struct wiphy *wiphy, struct net_device *dev,
			       int idx, u8 *mac, struct station_info *sinfo)
{
	printk("%s\n", __func__);

	//TODO: dump scanned queue

	return -ENOENT;
}

static int	cfg80211_rtw_change_bss(struct wiphy *wiphy, struct net_device *dev,
			      struct bss_parameters *params)
{
	u8 i;

	printk("%s\n", __func__);
/*
	printk("use_cts_prot=%d\n", params->use_cts_prot);
	printk("use_short_preamble=%d\n", params->use_short_preamble);
	printk("use_short_slot_time=%d\n", params->use_short_slot_time);
	printk("ap_isolate=%d\n", params->ap_isolate);

	printk("basic_rates_len=%d\n", params->basic_rates_len);
	for(i=0; i<params->basic_rates_len; i++)
	{		
		printk("basic_rates=%d\n", params->basic_rates[i]);
		
	}	
*/	
	return 0;
	
}

static int	cfg80211_rtw_set_channel(struct wiphy *wiphy, struct net_device *dev,
			       struct ieee80211_channel *chan,
			       enum nl80211_channel_type channel_type)
{
	printk("%s\n", __func__);
	
	return 0;
}

static int	cfg80211_rtw_auth(struct wiphy *wiphy, struct net_device *dev,
			struct cfg80211_auth_request *req)
{
	printk("%s\n", __func__);
	
	return 0;
}

static int	cfg80211_rtw_assoc(struct wiphy *wiphy, struct net_device *dev,
			 struct cfg80211_assoc_request *req)
{
	printk("%s\n", __func__);
	
	return 0;
}
#endif //CONFIG_AP_MODE

void rtw_cfg80211_rx_action_p2p(_adapter *padapter, u8 *pmgmt_frame, uint frame_len)
{
	s32 freq;
	int channel;	
	struct mlme_ext_priv *pmlmeext = &(padapter->mlmeextpriv);	

#ifdef CONFIG_CONCURRENT_MODE
	if(padapter->pcodatapriv)
		channel = padapter->pcodatapriv->co_ch;
#else
	channel = pmlmeext->cur_channel;
#endif

//#ifdef CONFIG_DEBUG_CFG80211
	printk("%s, cur_ch=%d\n", __func__, channel);
//#endif

#ifdef CONFIG_P2P
	rtw_p2p_check_frames(padapter, pmgmt_frame, frame_len, _FALSE);
#endif //CONFIG_P2P
	
	if (channel <= RTW_CH_MAX_2G_CHANNEL)
	{		
		freq = rtw_ieee80211_channel_to_frequency(channel, IEEE80211_BAND_2GHZ);
	}	
	else
	{		
		freq = rtw_ieee80211_channel_to_frequency(channel, IEEE80211_BAND_5GHZ);
	}	

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)) || defined(COMPAT_KERNEL_RELEASE)
	rtw_cfg80211_rx_mgmt(padapter->pnetdev, freq, 0, pmgmt_frame, frame_len, GFP_ATOMIC);
#else
	cfg80211_rx_action(padapter->pnetdev, freq, pmgmt_frame, frame_len, GFP_ATOMIC);
#endif

}

void rtw_cfg80211_rx_p2p_action_public(_adapter *padapter, u8 *pmgmt_frame, uint frame_len)
{
	s32 freq;
	int channel;
	struct mlme_ext_priv *pmlmeext = &(padapter->mlmeextpriv);	

#ifdef CONFIG_CONCURRENT_MODE
	if(padapter->pcodatapriv)
		channel = padapter->pcodatapriv->co_ch;
#else
	channel = pmlmeext->cur_channel;
#endif

//#ifdef CONFIG_DEBUG_CFG80211
	printk("%s, cur_ch=%d\n", __func__, channel);
//#endif

	#ifdef CONFIG_P2P
	rtw_p2p_check_frames(padapter, pmgmt_frame, frame_len, _FALSE);
	#endif

	if (channel <= RTW_CH_MAX_2G_CHANNEL)
	{		
		freq = rtw_ieee80211_channel_to_frequency(channel, IEEE80211_BAND_2GHZ);
	}	
	else
	{		
		freq = rtw_ieee80211_channel_to_frequency(channel, IEEE80211_BAND_5GHZ);
	}	

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)) || defined(COMPAT_KERNEL_RELEASE)
	rtw_cfg80211_rx_mgmt(padapter->pnetdev, freq, 0, pmgmt_frame, frame_len, GFP_ATOMIC);
#else
	cfg80211_rx_action(padapter->pnetdev, freq, pmgmt_frame, frame_len, GFP_ATOMIC);
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

	dump_mgntframe(padapter, pmgntframe);

	//if(wps_devicepassword_id == WPS_DPID_REGISTRAR_SPEC)
	//{
	//	printk("waiting for p2p peer key-in PIN CODE\n");
	//	rtw_msleep_os(15000); // 15 sec for key in PIN CODE, workaround for GS2 before issuing Nego Req.
	//}	

}

static s32 cfg80211_rtw_remain_on_channel(struct wiphy *wiphy, struct net_device *dev,
	struct ieee80211_channel * channel,
	enum nl80211_channel_type channel_type,
	unsigned int duration, u64 *cookie)
{
	s32 err = 0;
	_adapter *padapter = wiphy_to_adapter(wiphy);
	struct rtw_wdev_priv *pwdev_priv = wdev_to_priv(padapter->rtw_wdev);
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;
	struct cfg80211_wifidirect_info *pcfg80211_wdinfo = &padapter->cfg80211_wdinfo;
	u8 remain_ch = (u8) ieee80211_frequency_to_channel(channel->center_freq);
	u8 ready_on_channel = _FALSE;

	printk("%s(netdev=%p)  channel %02u duration %d\n", __func__, dev, remain_ch, duration);

	if(_FAIL == rtw_pwr_wakeup(padapter)) {
		err = -EFAULT;
		goto exit;
	}

	pcfg80211_wdinfo->remain_on_ch_dev = dev;
	_rtw_memcpy(&pcfg80211_wdinfo->remain_on_ch_channel, channel, sizeof(struct ieee80211_channel));
	pcfg80211_wdinfo->remain_on_ch_type= channel_type;
	pcfg80211_wdinfo->remain_on_ch_cookie= *cookie;	

	rtw_cfg80211_scan_abort(padapter);

	//if(!rtw_p2p_chk_role(pwdinfo, P2P_ROLE_CLIENT) && !rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO))
	if(rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE))
	{
		rtw_p2p_enable(padapter, P2P_ROLE_DEVICE);
		wdev_to_priv(padapter->rtw_wdev)->p2p_enabled = _TRUE;
	}
	else
	{
		rtw_p2p_set_pre_state(pwdinfo, rtw_p2p_state(pwdinfo));
#ifdef CONFIG_DEBUG_CFG80211		
		printk("%s, role=%d, p2p_state=%d\n", __func__, rtw_p2p_role(pwdinfo), rtw_p2p_state(pwdinfo));
#endif
	}


	rtw_p2p_set_state(pwdinfo, P2P_STATE_LISTEN);
	
#ifdef	CONFIG_CONCURRENT_MODE
	if(check_buddy_fwstate(padapter, _FW_LINKED) &&
		(duration<pwdinfo->ext_listen_interval)) 
	{
		duration = duration + 	pwdinfo->ext_listen_interval;
	}
#endif

	pcfg80211_wdinfo->restore_channel = pmlmeext->cur_channel;

	if(rtw_is_channel_set_contains_channel(pmlmeext->channel_set, remain_ch)) {
#ifdef	CONFIG_CONCURRENT_MODE
		if ( check_buddy_fwstate(padapter, _FW_LINKED ) )
		{
			PADAPTER pbuddy_adapter = padapter->pbuddy_adapter;			
			struct mlme_ext_priv *pbuddy_mlmeext = &pbuddy_adapter->mlmeextpriv;	

			if(remain_ch != pbuddy_mlmeext->cur_channel)
			{	
				if(ATOMIC_READ(&pwdev_priv->switch_ch_to)==1)
				{
					printk("%s, issue nulldata pwrbit=1\n", __func__);		
					issue_nulldata(padapter->pbuddy_adapter, 1);
				
					ATOMIC_SET(&pwdev_priv->switch_ch_to, 0);
			
					printk("%s, set switch ch timer\n", __func__);
					_set_timer(&pwdinfo->ap_p2p_switch_timer, duration-pwdinfo->ext_listen_interval);	
				}			
			}
		
			ready_on_channel = _TRUE;
			//pmlmeext->cur_channel = remain_ch;			
			//set_channel_bwmode(padapter, remain_ch, HAL_PRIME_CHNL_OFFSET_DONT_CARE, HT_CHANNEL_WIDTH_20);
		}else 
#endif //CONFIG_CONCURRENT_MODE
		if(remain_ch != pmlmeext->cur_channel )
		{
			ready_on_channel = _TRUE;
			//pmlmeext->cur_channel = remain_ch;			
			//set_channel_bwmode(padapter, remain_ch, HAL_PRIME_CHNL_OFFSET_DONT_CARE, HT_CHANNEL_WIDTH_20);
		}
	} else {
		DBG_871X("%s remain_ch:%u not in channel plan!!!!\n", __FUNCTION__, remain_ch);
	}
	



	//call this after other things have been done
#ifdef	CONFIG_CONCURRENT_MODE	
	if(ATOMIC_READ(&pwdev_priv->ro_ch_to)==1)
	{
		u8 co_channel = 0xff;
		ATOMIC_SET(&pwdev_priv->ro_ch_to, 0);
#endif

		if(ready_on_channel == _TRUE)
		{			
			pmlmeext->cur_channel = remain_ch;
			
#ifdef	CONFIG_CONCURRENT_MODE
			if(padapter->pcodatapriv)
				co_channel = padapter->pcodatapriv->co_ch;

			if(co_channel !=remain_ch)
#endif
				set_channel_bwmode(padapter, remain_ch, HAL_PRIME_CHNL_OFFSET_DONT_CARE, HT_CHANNEL_WIDTH_20);			
		}
		
		printk("%s, set ro ch timer\n", __func__);

	_set_timer( &pcfg80211_wdinfo->remain_on_ch_timer, duration);

#ifdef	CONFIG_CONCURRENT_MODE
	}	
#endif

       	cfg80211_ready_on_channel(dev, *cookie, channel, channel_type, duration, GFP_KERNEL);

	pwdinfo->listen_channel = pmlmeext->cur_channel;

exit:
	return err;
}

static s32 cfg80211_rtw_cancel_remain_on_channel(struct wiphy *wiphy, struct net_device *dev,
	u64 cookie)
{
	s32 err = 0;
	_adapter *padapter = wiphy_to_adapter(wiphy);
	struct rtw_wdev_priv *pwdev_priv = wdev_to_priv(padapter->rtw_wdev);
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
	
	printk("%s(netdev=%p)\n", __func__, dev);

	//Modified bu Kurt 20120114
	_cancel_timer_ex(&padapter->cfg80211_wdinfo.remain_on_ch_timer);
#ifdef CONFIG_CONCURRENT_MODE
	ATOMIC_SET(&pwdev_priv->ro_ch_to, 1);
#endif

	#if 0
	//	Disable P2P Listen State
	if(!rtw_p2p_chk_role(pwdinfo, P2P_ROLE_CLIENT) && !rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO))
	{
		if(!rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE))
		{
			_cancel_timer_ex( &pwdinfo->find_phase_timer );
			_cancel_timer_ex( &pwdinfo->restore_p2p_state_timer );
			_cancel_timer_ex( &pwdinfo->pre_tx_scan_timer);
			
			rtw_p2p_set_state(pwdinfo, P2P_STATE_NONE);
			_rtw_memset(pwdinfo, 0x00, sizeof(struct wifidirect_info));

			if(pwrpriv->bips_processing == _FALSE){
				rtw_set_pwr_state_check_timer(pwrpriv);
			}
		}
	}
	else
	#endif
	{
		 rtw_p2p_set_state(pwdinfo, rtw_p2p_pre_state(pwdinfo));
#ifdef CONFIG_DEBUG_CFG80211		 
		 printk("%s, role=%d, p2p_state=%d\n", __func__, rtw_p2p_role(pwdinfo), rtw_p2p_state(pwdinfo));
#endif
	}
	
	return err;
}

#endif //CONFIG_P2P

static int	cfg80211_rtw_mgmt_tx(struct wiphy *wiphy, struct net_device *dev,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,38)) || defined(COMPAT_KERNEL_RELEASE)
			struct ieee80211_channel *chan, bool offchan,
			enum nl80211_channel_type channel_type,
			bool channel_type_valid, unsigned int wait,
#else	//(LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,38))
			struct ieee80211_channel *chan,
			enum nl80211_channel_type channel_type,
		#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)) || defined(COMPAT_KERNEL_RELEASE)	  
			bool channel_type_valid,
		#endif
#endif	//(LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,38))
			const u8 *buf, size_t len,
		#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0))
			bool no_cck,
		#endif
		#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,3,0))
			bool dont_wait_for_ack,
		#endif
			u64 *cookie)
{
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
	struct rtw_wdev_priv *pwdev_priv = wdev_to_priv(padapter->rtw_wdev);
	struct xmit_priv	*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;
	u8 tx_ch = (u8)ieee80211_frequency_to_channel(chan->center_freq);


	/* cookie generation */
	*cookie = (unsigned long) buf;


	printk("%s(netdev=%p), len=%d, ch=%d, ch_type=%d\n", __func__, dev, len,
			ieee80211_frequency_to_channel(chan->center_freq), channel_type);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)) || defined(COMPAT_KERNEL_RELEASE)
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

#ifdef	CONFIG_CONCURRENT_MODE
	if ( check_buddy_fwstate(padapter, _FW_LINKED )	)	
	{			
		u8 co_channel=0xff;
		PADAPTER pbuddy_adapter = padapter->pbuddy_adapter;			
		struct mlme_ext_priv *pbuddy_mlmeext = &pbuddy_adapter->mlmeextpriv;	

		if(padapter->pcodatapriv)
			co_channel = padapter->pcodatapriv->co_ch;

		if(tx_ch != pbuddy_mlmeext->cur_channel)
	{
			if(ATOMIC_READ(&pwdev_priv->switch_ch_to)==1)
			{		
				printk("%s, issue nulldata pwrbit=1\n", __func__);				
				issue_nulldata(padapter->pbuddy_adapter, 1);					

				ATOMIC_SET(&pwdev_priv->switch_ch_to, 0);

				printk("%s, set switch ch timer\n", __func__);
				_set_timer(&pwdinfo->ap_p2p_switch_timer, pwdinfo->ext_listen_period);	
			}			
		}

		pmlmeext->cur_channel = tx_ch;
		
		if(tx_ch != co_channel)
			set_channel_bwmode(padapter, tx_ch, HAL_PRIME_CHNL_OFFSET_DONT_CARE, HT_CHANNEL_WIDTH_20);
		
	}else 
#endif //CONFIG_CONCURRENT_MODE
	if( tx_ch != pmlmeext->cur_channel )
	{
		pmlmeext->cur_channel = tx_ch;
		set_channel_bwmode(padapter, tx_ch, HAL_PRIME_CHNL_OFFSET_DONT_CARE, HT_CHANNEL_WIDTH_20);	
	}
	
	#ifdef CONFIG_P2P
	if( (type = rtw_p2p_check_frames(padapter, buf, len, _TRUE)) < 0)
	{
		ack = _FALSE;		
		goto exit;
	}
	#endif


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
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)) || defined(COMPAT_KERNEL_RELEASE)
	cfg80211_mgmt_tx_status(dev, *cookie, buf, len, ack, GFP_KERNEL);
#elif  (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,34) && LINUX_VERSION_CODE<=KERNEL_VERSION(2,6,35))
	cfg80211_action_tx_status(dev, *cookie, buf, len, ack, GFP_KERNEL);
#endif	
	
	dump_mgntframe(padapter, pmgntframe);
	
	return ret;
	
exit:
	
	printk("%s, ack=%d  \n", __func__, ack );

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)) || defined(COMPAT_KERNEL_RELEASE)	 
	cfg80211_mgmt_tx_status(dev, *cookie, buf, len, ack, GFP_KERNEL);
#elif  (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,34) && LINUX_VERSION_CODE<=KERNEL_VERSION(2,6,35))
	cfg80211_action_tx_status(dev, *cookie, buf, len, ack, GFP_KERNEL);
#endif	
	
	return ret;	

}

static void cfg80211_rtw_mgmt_frame_register(struct wiphy *wiphy, struct net_device *dev,
	u16 frame_type, bool reg)
{

#ifdef CONFIG_DEBUG_CFG80211
	printk("%s: frame_type: %x, reg: %d\n", __func__, frame_type, reg);
#endif

	if (frame_type != (IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_PROBE_REQ))
		return;

	return;
}

static int rtw_cfg80211_set_beacon_wpsp2pie(struct net_device *net, char *buf, int len)
{	
	int ret = 0;
	uint wps_ielen = 0;
	u8 *wps_ie;
	u32	p2p_ielen = 0;
	u8 wps_oui[8]={0x0,0x50,0xf2,0x04};	
	u8 *p2p_ie;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(net);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv *pmlmeext = &(padapter->mlmeextpriv);

	printk("%s(netdev=%p), ielen=%d\n", __func__, net, len);
	
	if(len>0)
	{
		if((wps_ie = rtw_get_wps_ie(buf, len, NULL, &wps_ielen)))
		{	
			#ifdef CONFIG_DEBUG_CFG80211
			printk("bcn_wps_ielen=%d\n", wps_ielen);
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
				printk("%s()-%d: rtw_malloc() ERROR!\n", __FUNCTION__, __LINE__);
				return -EINVAL;
			
			}
			
			_rtw_memcpy(pmlmepriv->wps_beacon_ie, wps_ie, wps_ielen);
			pmlmepriv->wps_beacon_ie_len = wps_ielen;
	
			update_beacon(padapter, _VENDOR_SPECIFIC_IE_, wps_oui, _TRUE);

		}

		buf += wps_ielen;
		len -= wps_ielen;

		#ifdef CONFIG_P2P
		if((p2p_ie=rtw_get_p2p_ie(buf, len, NULL, &p2p_ielen)))
		{
			#ifdef CONFIG_DEBUG_CFG80211
			printk("bcn_p2p_ielen=%d\n", p2p_ielen);
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
				printk("%s()-%d: rtw_malloc() ERROR!\n", __FUNCTION__, __LINE__);
				return -EINVAL;
			
			}
			
			_rtw_memcpy(pmlmepriv->p2p_beacon_ie, p2p_ie, p2p_ielen);
			pmlmepriv->p2p_beacon_ie_len = p2p_ielen;
			
		}
		#endif //CONFIG_P2P
		
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
	_adapter *padapter = (_adapter *)rtw_netdev_priv(net);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);	

#ifdef CONFIG_DEBUG_CFG80211
	printk("%s, ielen=%d\n", __func__, len);
#endif
	
	if(len>0)
	{
		if((wps_ie = rtw_get_wps_ie(buf, len, NULL, &wps_ielen)))
		{	
			uint	attr_contentlen = 0;
			u16	uconfig_method, *puconfig_method = NULL;

			#ifdef CONFIG_DEBUG_CFG80211			
			printk("probe_resp_wps_ielen=%d\n", wps_ielen);
			#endif
		
			if(pmlmepriv->wps_probe_resp_ie)
			{
				u32 free_len = pmlmepriv->wps_probe_resp_ie_len;
				pmlmepriv->wps_probe_resp_ie_len = 0;
				rtw_mfree(pmlmepriv->wps_probe_resp_ie, free_len);
				pmlmepriv->wps_probe_resp_ie = NULL;			
			}	

			pmlmepriv->wps_probe_resp_ie = rtw_malloc(wps_ielen);
			if ( pmlmepriv->wps_probe_resp_ie == NULL) {
				printk("%s()-%d: rtw_malloc() ERROR!\n", __FUNCTION__, __LINE__);
				return -EINVAL;
			
			}
			
			//add PUSH_BUTTON config_method by driver self in wpsie of probe_resp at GO Mode 
			if ( (puconfig_method = (u16*)rtw_get_wps_attr_content( wps_ie, wps_ielen, WPS_ATTR_CONF_METHOD , NULL, &attr_contentlen)) != NULL )
			{
				#ifdef CONFIG_DEBUG_CFG80211		
				//printk("config_method in wpsie of probe_resp = 0x%x\n", be16_to_cpu(*puconfig_method));
				#endif
				
				uconfig_method = WPS_CM_PUSH_BUTTON;
				uconfig_method = cpu_to_be16( uconfig_method );

				*puconfig_method |= uconfig_method;			
			}
			
			_rtw_memcpy(pmlmepriv->wps_probe_resp_ie, wps_ie, wps_ielen);
			pmlmepriv->wps_probe_resp_ie_len = wps_ielen;
			
		}

		buf += wps_ielen;
		len -= wps_ielen;

		#ifdef CONFIG_P2P
		if((p2p_ie=rtw_get_p2p_ie(buf, len, NULL, &p2p_ielen))) 
		{
			u8 is_GO = _FALSE;			
			u32 attr_contentlen = 0;
			u16 cap_attr=0;

			#ifdef CONFIG_DEBUG_CFG80211
			printk("probe_resp_p2p_ielen=%d\n", p2p_ielen);
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
					printk("Got P2P Capability Attr, grp_cap=0x%x, is_GO\n", grp_cap);
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
					printk("%s()-%d: rtw_malloc() ERROR!\n", __FUNCTION__, __LINE__);
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
					printk("%s()-%d: rtw_malloc() ERROR!\n", __FUNCTION__, __LINE__);
					return -EINVAL;
			
				}
				_rtw_memcpy(pmlmepriv->p2p_go_probe_resp_ie, p2p_ie, p2p_ielen);
				pmlmepriv->p2p_go_probe_resp_ie_len = p2p_ielen;
			}
			
		}
		#endif //CONFIG_P2P
		
	}

	return ret;
	
}

static int rtw_cfg80211_set_assoc_resp_wpsp2pie(struct net_device *net, char *buf, int len)
{
	int ret = 0;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(net);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);

	printk("%s, ielen=%d\n", __func__, len);
	
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
			printk("%s()-%d: rtw_malloc() ERROR!\n", __FUNCTION__, __LINE__);
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
	printk("%s, ielen=%d\n", __func__, len);
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

	#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 4, 0))
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
	.set_channel = cfg80211_rtw_set_channel,
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
		printk("%s, error rf_type=%d\n", __func__, rf_type);
	}	
	
}

void rtw_cfg80211_init_wiphy(_adapter *padapter)
{
	u8 rf_type;
	struct ieee80211_supported_band *bands;
	struct wireless_dev *pwdev = padapter->rtw_wdev;
	struct wiphy *wiphy = pwdev->wiphy;
	
	rtw_hal_get_hwreg(padapter, HW_VAR_RF_TYPE, (u8 *)(&rf_type));

	printk("%s:rf_type=%d\n", __func__, rf_type);

	bands = wiphy->bands[IEEE80211_BAND_2GHZ];
	if(bands)
		rtw_cfg80211_init_ht_capab(&bands->ht_cap, IEEE80211_BAND_2GHZ, rf_type);

	bands = wiphy->bands[IEEE80211_BAND_5GHZ];
	if(bands)
		rtw_cfg80211_init_ht_capab(&bands->ht_cap, IEEE80211_BAND_5GHZ, rf_type);	
}

static void rtw_cfg80211_preinit_wiphy(_adapter *padapter, struct wiphy *wiphy)
{

	wiphy->signal_type = CFG80211_SIGNAL_TYPE_MBM;

	wiphy->max_scan_ssids = RTW_SSID_SCAN_AMOUNT;
	wiphy->max_scan_ie_len = RTW_SCAN_IE_LEN_MAX;	
	wiphy->max_num_pmkids = RTW_MAX_NUM_PMKIDS;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,38)) || defined(COMPAT_KERNEL_RELEASE)	 
	wiphy->max_remain_on_channel_duration = RTW_MAX_REMAIN_ON_CHANNEL_DURATION;
#endif
	
	wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION) |
				       BIT(NL80211_IFTYPE_ADHOC)
#ifdef CONFIG_AP_MODE
					| BIT(NL80211_IFTYPE_AP) | BIT(NL80211_IFTYPE_MONITOR)
#endif
#if defined(CONFIG_P2P) && ((LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)) || defined(COMPAT_KERNEL_RELEASE))
					| BIT(NL80211_IFTYPE_P2P_CLIENT) | BIT(NL80211_IFTYPE_P2P_GO)
#endif					
				       ;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)) || defined(COMPAT_KERNEL_RELEASE)	
#ifdef CONFIG_AP_MODE
	wiphy->mgmt_stypes = rtw_cfg80211_default_mgmt_stypes;
#endif //CONFIG_AP_MODE	
#endif		

	wiphy->cipher_suites = rtw_cipher_suites;
	wiphy->n_cipher_suites = ARRAY_SIZE(rtw_cipher_suites);


	wiphy->bands[IEEE80211_BAND_2GHZ] = rtw_spt_band_alloc(IEEE80211_BAND_2GHZ);
	wiphy->bands[IEEE80211_BAND_5GHZ] = rtw_spt_band_alloc(IEEE80211_BAND_5GHZ);
	
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,38) && LINUX_VERSION_CODE < KERNEL_VERSION(3,0,0))
	wiphy->flags |= WIPHY_FLAG_SUPPORTS_SEPARATE_DEFAULT_KEYS;
#endif
	
}

int rtw_wdev_alloc(_adapter *padapter, struct device *dev)
{
	int ret = 0;
	struct wireless_dev *wdev;
	struct rtw_wdev_priv *pwdev_priv;
	struct net_device *pnetdev = padapter->pnetdev;
	
	printk("%s(padapter=%p)\n", __func__, padapter);

	wdev = (struct wireless_dev *)rtw_zmalloc(sizeof(struct wireless_dev));
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
	_rtw_spinlock_init(&pwdev_priv->scan_req_lock);
		
	pwdev_priv->p2p_enabled = _FALSE;
	pwdev_priv->provdisc_req_issued = _FALSE;
		
	pwdev_priv->bandroid_scan = _FALSE;
	
#ifdef CONFIG_CONCURRENT_MODE
	ATOMIC_SET(&pwdev_priv->switch_ch_to, 1);	
	ATOMIC_SET(&pwdev_priv->ro_ch_to, 1);	
#endif	

	wdev->netdev = pnetdev;
	//wdev->iftype = NL80211_IFTYPE_STATION;
	wdev->iftype = NL80211_IFTYPE_MONITOR; // for rtw_setopmode_cmd() in cfg80211_rtw_change_iface()

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
	rtw_mfree((u8*)wdev, sizeof(struct wireless_dev));

	return ret;
	
}

void rtw_wdev_free(struct wireless_dev *wdev)
{
	struct rtw_wdev_priv *pwdev_priv;

	printk("%s(wdev=%p)\n", __func__, wdev);

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

	rtw_spt_band_free(wdev->wiphy->bands[IEEE80211_BAND_2GHZ]);
	rtw_spt_band_free(wdev->wiphy->bands[IEEE80211_BAND_5GHZ]);
	
	wiphy_free(wdev->wiphy);

	rtw_mfree((u8*)wdev, sizeof(struct wireless_dev));
}

#endif //CONFIG_IOCTL_CFG80211

