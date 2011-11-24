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
#define _RTW_MLME_EXT_C_

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <wifi.h>
#include <rtw_mlme_ext.h>
#include <wlan_bssdef.h>
#include <mlme_osdep.h>
#include <recv_osdep.h>

struct mlme_handler mlme_sta_tbl[]={
	{WIFI_ASSOCREQ,		"OnAssocReq",	&OnAssocReq},
	{WIFI_ASSOCRSP,		"OnAssocRsp",	&OnAssocRsp},
	{WIFI_REASSOCREQ,	"OnReAssocReq",	&OnAssocReq},
	{WIFI_REASSOCRSP,	"OnReAssocRsp",	&OnAssocRsp},
	{WIFI_PROBEREQ,		"OnProbeReq",	&OnProbeReq},
	{WIFI_PROBERSP,		"OnProbeRsp",		&OnProbeRsp},

	/*----------------------------------------------------------
					below 2 are reserved
	-----------------------------------------------------------*/
	{0,					"DoReserved",		&DoReserved},
	{0,					"DoReserved",		&DoReserved},
	{WIFI_BEACON,		"OnBeacon",		&OnBeacon},
	{WIFI_ATIM,			"OnATIM",		&OnAtim},
	{WIFI_DISASSOC,		"OnDisassoc",		&OnDisassoc},
	{WIFI_AUTH,			"OnAuth",		&OnAuthClient},
	{WIFI_DEAUTH,		"OnDeAuth",		&OnDeAuth},
	{WIFI_ACTION,		"OnAction",		&OnAction},
};

#ifdef _CONFIG_NATIVEAP_MLME_
struct mlme_handler mlme_ap_tbl[]={
	{WIFI_ASSOCREQ,		"OnAssocReq",	&OnAssocReq},
	{WIFI_ASSOCRSP,		"OnAssocRsp",	&OnAssocRsp},
	{WIFI_REASSOCREQ,	"OnReAssocReq",	&OnAssocReq},
	{WIFI_REASSOCRSP,	"OnReAssocRsp",	&OnAssocRsp},
	{WIFI_PROBEREQ,		"OnProbeReq",	&OnProbeReq},
	{WIFI_PROBERSP,		"OnProbeRsp",		&OnProbeRsp},

	/*----------------------------------------------------------
					below 2 are reserved
	-----------------------------------------------------------*/
	{0,					"DoReserved",		&DoReserved},
	{0,					"DoReserved",		&DoReserved},
	{WIFI_BEACON,		"OnBeacon",		&OnBeacon},
	{WIFI_ATIM,			"OnATIM",		&OnAtim},
	{WIFI_DISASSOC,		"OnDisassoc",		&OnDisassoc},
	{WIFI_AUTH,			"OnAuth",		&OnAuth},
	{WIFI_DEAUTH,		"OnDeAuth",		&OnDeAuth},
	{WIFI_ACTION,		"OnAction",		&OnAction},
};
#endif

struct action_handler OnAction_tbl[]={
	{WLAN_CATEGORY_SPECTRUM_MGMT,	 "ACTION_SPECTRUM_MGMT", &DoReserved},
	{WLAN_CATEGORY_QOS, "ACTION_QOS", &OnAction_qos},
	{WLAN_CATEGORY_DLS, "ACTION_DLS", &OnAction_dls},
	{WLAN_CATEGORY_BACK, "ACTION_BACK", &OnAction_back},
	{WLAN_CATEGORY_PUBLIC, "ACTION_PUBLIC", &OnAction_public},
	{WLAN_CATEGORY_RADIO_MEASUREMENT, "ACTION_RADIO_MEASUREMENT", &DoReserved},
	{WLAN_CATEGORY_FT, "ACTION_FT",	&DoReserved},
	{WLAN_CATEGORY_HT,	"ACTION_HT",	&OnAction_ht},
	{WLAN_CATEGORY_SA_QUERY, "ACTION_SA_QUERY", &DoReserved},
	{WLAN_CATEGORY_WMM, "ACTION_WMM", &OnAction_wmm},	
	{WLAN_CATEGORY_P2P, "ACTION_P2P", &OnAction_p2p},	
};


/**************************************************
OUI definitions for the vendor specific IE
***************************************************/
unsigned char	WPA_OUI[] = {0x00, 0x50, 0xf2, 0x01};
unsigned char WMM_OUI[] = {0x00, 0x50, 0xf2, 0x02};
unsigned char	WPS_OUI[] = {0x00, 0x50, 0xf2, 0x04};

unsigned char	WMM_INFO_OUI[] = {0x00, 0x50, 0xf2, 0x02, 0x00, 0x01};
unsigned char	WMM_PARA_OUI[] = {0x00, 0x50, 0xf2, 0x02, 0x01, 0x01};

unsigned char WPA_TKIP_CIPHER[4] = {0x00, 0x50, 0xf2, 0x02};
unsigned char RSN_TKIP_CIPHER[4] = {0x00, 0x0f, 0xac, 0x02};

extern unsigned char REALTEK_96B_IE[];

/********************************************************
MCS rate definitions
*********************************************************/

unsigned char	MCS_rate_2R[16] = {0xff, 0xff, 0x0, 0x0, 0x01, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0};
unsigned char	MCS_rate_1R[16] = {0xff, 0x00, 0x0, 0x0, 0x01, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0};

/********************************************************
ChannelPlan definitions
*********************************************************/
static RT_CHANNEL_PLAN	DefaultChannelPlan[RT_CHANNEL_DOMAIN_MAX] = {
							{{1,2,3,4,5,6,7,8,9,10,11,36,40,44,48,52,56,60,64,100,104,108,112,116,132,136,140,149,153,157,161,165},32},	// 0x00, RT_CHANNEL_DOMAIN_FCC
							{{1,2,3,4,5,6,7,8,9,10,11,36,40,44,48,52,56,60,64,100,104,108,112,116,136,140,149,153,157,161,165},31},					// 0x01, RT_CHANNEL_DOMAIN_IC
							{{1,2,3,4,5,6,7,8,9,10,11,12,13,36,40,44,48,52,56,60,64,100,104,108,112,116,120,124,128,132,136,140},32},				// 0x02, RT_CHANNEL_DOMAIN_ETSI
							{{1,2,3,4,5,6,7,8,9,10,11,12,13},13},												// 0x03, RT_CHANNEL_DOMAIN_SPAIN
							{{1,2,3,4,5,6,7,8,9,10,11,12,13},13},												// 0x04, RT_CHANNEL_DOMAIN_FRANCE
							{{1,2,3,4,5,6,7,8,9,10,11,12,13,14,36,40,44,48,52,56,60,64},22},						// 0x05, RT_CHANNEL_DOMAIN_MKK
							{{1,2,3,4,5,6,7,8,9,10,11,12,13,14,36,40,44,48,52,56,60,64},22},						// 0x06, RT_CHANNEL_DOMAIN_MKK1
							{{1,2,3,4,5,6,7,8,9,10,11,12,13,36,40,44,48,52,56,60,64},21},							// 0x07, RT_CHANNEL_DOMAIN_ISRAEL
							{{1,2,3,4,5,6,7,8,9,10,11,12,13,14,36,40,44,48,52,56,60,64},22},						// 0x08, RT_CHANNEL_DOMAIN_TELEC
#if 0 /* Not using EEPROM_CHANNEL_PLAN directly */
							{{1,2,3,4,5,6,7,8,9,10,11,12,13,36,40,44,48,52,56,60,64},21},							// 0x09, RT_CHANNEL_DOMAIN_MIC
							{{1,2,3,4,5,6,7,8,9,10,11,12,13,14},14},									// 0x0A, RT_CHANNEL_DOMAIN_GLOBAL_DOAMIN
							{{1,2,3,4,5,6,7,8,9,10,11,12,13},13},										// 0x0B, RT_CHANNEL_DOMAIN_WORLD_WIDE_13
							{{1,2,3,4,5,6,7,8,9,10,11,12,13,36,40,44,48,52,56,60,64},21},							// 0x0C, RT_CHANNEL_DOMAIN_TELEC_NETGEAR
							{{1,2,3,4,5,6,7,8,9,10,11,36,40,44,48,52,56,60,64,149,153,157,161,165},24},					// 0x0D, RT_CHANNEL_DOMAIN_NCC
#endif /* Not using EEPROM_CHANNEL_PLAN directly */
							{{1,2,3,4,5,6,7,8,9,10,11,12,13,14},14},									// 0x09, RT_CHANNEL_DOMAIN_GLOBAL_DOAMIN
							{{1,2,3,4,5,6,7,8,9,10,11,12,13},13},										// 0x0A, RT_CHANNEL_DOMAIN_WORLD_WIDE_13
							{{1,2,3,4,5,6,7,8,9,10,11,56,60,64,100,104,108,112,116,136,140,149,153,157,161,165},26},			// 0x0B, RT_CHANNEL_DOMAIN_NCC
							{{1,2,3,4,5,6,7,8,9,10,11,149,153,157,161,165},16},								// 0x0C, RT_CHANNEL_DOMAIN_CHINA
							{{1,2,3,4,5,6,7,8,9,10,11,36,40,44,48,52,56,60,64,149,153,157,161,165},24},					// 0x0D, RT_CHANNEL_DOMAIN__SINGAPORE_INDIA_MEXICO
							{{1,2,3,4,5,6,7,8,9,10,11,36,40,44,48,52,56,60,64,100,104,108,112,116,120,124,149,153,157,161,165},31},		// 0x0E, RT_CHANNEL_DOMAIN_KOREA
							{{1,2,3,4,5,6,7,8,9,10,11,36,40,44,48,52,56,60,64},19},								// 0x0F, RT_CHANNEL_DOMAIN_TURKEY
							{{1,2,3,4,5,6,7,8,9,10,11,12,13,36,40,44,48,52,56,60,64,100,104,108,112,116,120,124,128,132,136,140},32},	// 0x10, RT_CHANNEL_DOMAIN_JAPAN
							{{1,2,3,4,5,6,7,8,9,10,11,36,40,44,48,149,153,157,161,165},20},							// 0x11, RT_CHANNEL_DOMAIN_FCC_NO_DFS
							{{1,2,3,4,5,6,7,8,9,10,11,12,13,36,40,44,48},17},								// 0x12, RT_CHANNEL_DOMAIN_JAPAN_NO_DFS
							};

/****************************************************************************

Following are the initialization functions for WiFi MLME

*****************************************************************************/

extern void joinbss_event_prehandle(_adapter *adapter, u8 *pbuf);

int init_hw_mlme_ext(_adapter *padapter)
{
	struct	mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;

	//set_opmode_cmd(padapter, infra_client_with_mlme);//removed

	set_channel_bwmode(padapter, pmlmeext->cur_channel, pmlmeext->cur_ch_offset, pmlmeext->cur_bwmode);

	return _SUCCESS;
}

static void init_mlme_ext_priv_value(_adapter* padapter)
{
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

#ifdef CONFIG_TDLS
	u8 i;
#endif

	//unsigned char default_channel_set[MAX_CHANNEL_NUM] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 0, 0};
	unsigned char	mixed_datarate[NumRates] = {_1M_RATE_, _2M_RATE_, _5M_RATE_, _11M_RATE_, _6M_RATE_,_9M_RATE_, _12M_RATE_, _18M_RATE_, _24M_RATE_, _36M_RATE_, _48M_RATE_, _54M_RATE_, 0xff};
	unsigned char	mixed_basicrate[NumRates] ={_1M_RATE_, _2M_RATE_, _5M_RATE_, _11M_RATE_, 0xff,};

	ATOMIC_SET(&pmlmeext->event_seq, 0);
	pmlmeext->mgnt_seq = 0;//reset to zero when disconnect at client mode

	pmlmeext->cur_channel = padapter->registrypriv.channel;
	pmlmeext->cur_bwmode = HT_CHANNEL_WIDTH_20;
	pmlmeext->cur_ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
	pmlmeext->retry = 0;

	pmlmeext->cur_wireless_mode = padapter->registrypriv.wireless_mode;

	//_rtw_memcpy(pmlmeext->channel_set, DefaultChannelPlan[padapter->mlmepriv.ChannelPlan].Channel, DefaultChannelPlan[padapter->mlmepriv.ChannelPlan].Len);
	//_rtw_memcpy(pmlmeext->channel_set, default_channel_set, MAX_CHANNEL_NUM);
	_rtw_memcpy(pmlmeext->datarate, mixed_datarate, NumRates);
	_rtw_memcpy(pmlmeext->basicrate, mixed_basicrate, NumRates);

	pmlmeext->sitesurvey_res.state = SCAN_DISABLE;
	pmlmeext->sitesurvey_res.channel_idx = 0;
	pmlmeext->sitesurvey_res.bss_cnt = 0;

#ifdef CONFIG_TDLS
	_init_workitem(&pmlmeext->TDLS_restore_workitem, TDLS_restore_workitem_callback, pmlmeext);
#endif

	pmlmeinfo->state = WIFI_FW_NULL_STATE;
	pmlmeinfo->reauth_count = 0;
	pmlmeinfo->reassoc_count = 0;
	pmlmeinfo->link_count = 0;
	pmlmeinfo->auth_seq = 0;
	pmlmeinfo->auth_algo = dot11AuthAlgrthm_Open;
	pmlmeinfo->key_index = 0;
	pmlmeinfo->iv = 0;

#ifdef CONFIG_TDLS
	pmlmeinfo->tdls_setup_state=UN_TDLS_STATE;
	pmlmeinfo->tdls_sta_cnt=0;
	pmlmeinfo->tdls_dis_req=0;
	pmlmeinfo->tdls_cam_entry_to_write=6;
	pmlmeinfo->tdls_cam_entry_to_clear=0;
	pmlmeinfo->tdls_ch_sensing=0;
	pmlmeinfo->tdls_cur_channel=0;
	pmlmeinfo->tdls_candidate_ch=1;	//when inplement channel switching, default candidate channel is 1
	for(i=0; i<14; i++)
		pmlmeinfo->tdls_collect_pkt_num[i]=0;
#endif

	pmlmeinfo->enc_algo = _NO_PRIVACY_;
	pmlmeinfo->authModeToggle = 0;

	_rtw_memset(pmlmeinfo->chg_txt, 0, 128);

	pmlmeinfo->slotTime = SHORT_SLOT_TIME;
	pmlmeinfo->preamble_mode = PREAMBLE_AUTO;

	pmlmeinfo->dialogToken = 0;
}

static u8 init_channel_set(_adapter* padapter, u8 ChannelPlan, RT_CHANNEL_INFO *channel_set)
{
	u8	index,chanset_size = 0;
	u8	b5GBand = _FALSE, b2_4GBand = _FALSE;

	_rtw_memset(channel_set, 0, sizeof(RT_CHANNEL_INFO)*MAX_CHANNEL_NUM);

	if(ChannelPlan >= RT_CHANNEL_DOMAIN_MAX)
	{
		DBG_8192C("channel plan id error \n");
		return chanset_size;
	}

	if(padapter->registrypriv.wireless_mode & WIRELESS_11G)
	{
		b2_4GBand = _TRUE;
	}

	if(padapter->registrypriv.wireless_mode & WIRELESS_11A)
	{
		b5GBand = _TRUE;
	}

	for(index=0;index<DefaultChannelPlan[ChannelPlan].Len;index++)
	{
		if((DefaultChannelPlan[ChannelPlan].Channel[index]  <= 14) && (b2_4GBand))
		{
			channel_set[chanset_size].ChannelNum = DefaultChannelPlan[ChannelPlan].Channel[index];

			if(RT_CHANNEL_DOMAIN_GLOBAL_DOAMIN == ChannelPlan) //Channel 1~11 is active, and 12~14 is passive
			{
				if(channel_set[chanset_size].ChannelNum >= 1 && channel_set[chanset_size].ChannelNum <= 11)
					channel_set[chanset_size].ScanType = SCAN_ACTIVE;
				else if((channel_set[chanset_size].ChannelNum  >= 12 && channel_set[chanset_size].ChannelNum  <= 14))
					channel_set[chanset_size].ScanType  = SCAN_PASSIVE;			
			}
			else if(RT_CHANNEL_DOMAIN_WORLD_WIDE_13 == ChannelPlan)// channel 12~13, passive scan
			{
				if(channel_set[chanset_size].ChannelNum <= 11)
					channel_set[chanset_size].ScanType = SCAN_ACTIVE;
				else
					channel_set[chanset_size].ScanType = SCAN_PASSIVE;
			}
			else
			{
				channel_set[chanset_size].ScanType = SCAN_ACTIVE;
			}

			chanset_size++;
		}
		else if((DefaultChannelPlan[ChannelPlan].Channel[index]  >= 36) && (b5GBand))
		{
#ifdef DFS
			channel_set[chanset_size].ChannelNum = DefaultChannelPlan[ChannelPlan].Channel[index];
			if ( channel_set[chanset_size].ChannelNum <= 48 
				|| channel_set[chanset_size].ChannelNum >= 149 )
				channel_set[chanset_size].ScanType = SCAN_ACTIVE;
			else
				channel_set[chanset_size].ScanType = SCAN_PASSIVE;
			chanset_size++;
#else /* DFS */
			if ( DefaultChannelPlan[ChannelPlan].Channel[index] <= 48 
				|| DefaultChannelPlan[ChannelPlan].Channel[index] >= 149 ) {
				channel_set[chanset_size].ChannelNum = DefaultChannelPlan[ChannelPlan].Channel[index];
				channel_set[chanset_size].ScanType = SCAN_ACTIVE;
				DBG_8192C("%s(): channel_set[%d].ChannelNum = %d\n", __FUNCTION__, chanset_size, channel_set[chanset_size].ChannelNum);
				chanset_size++;
			}
#endif /* DFS */
		}
	}
		
	return chanset_size;
}

int	init_mlme_ext_priv(_adapter* padapter)
{
	int	res = _SUCCESS;
	struct registry_priv* pregistrypriv = &padapter->registrypriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	_rtw_memset((u8 *)pmlmeext, 0, sizeof(struct mlme_ext_priv));
	pmlmeext->padapter = padapter;

	//fill_fwpriv(padapter, &(pmlmeext->fwpriv));

	init_mlme_ext_priv_value(padapter);
	pmlmeinfo->bAcceptAddbaReq = pregistrypriv->bAcceptAddbaReq;
	
	init_mlme_ext_timer(padapter);

#ifdef CONFIG_AP_MODE
	init_mlme_ap_info(padapter);	
#endif

	pmlmeext->max_chan_nums = init_channel_set(padapter, pmlmepriv->ChannelPlan,pmlmeext->channel_set);

	pmlmeext->chan_scan_time = SURVEY_TO;
	pmlmeext->mlmeext_init = _TRUE;

	return res;

}

void free_mlme_ext_priv (struct mlme_ext_priv *pmlmeext)
{
	_adapter *padapter = pmlmeext->padapter;

	if (!padapter)
		return;

	if (padapter->bDriverStopped == _TRUE)
	{
		_cancel_timer_ex(&pmlmeext->survey_timer);
		_cancel_timer_ex(&pmlmeext->link_timer);
		//_cancel_timer_ex(&pmlmeext->ADDBA_timer);
	}
}

static void UpdateBrateTbl(
	IN PADAPTER		Adapter,
	IN u8			*mBratesOS
)
{
	u8	i;
	u8	rate;

	// 1M, 2M, 5.5M, 11M, 6M, 12M, 24M are mandatory.
	for(i=0;i<NDIS_802_11_LENGTH_RATES_EX;i++)
	{
		rate = mBratesOS[i] & 0x7f;
		switch(rate)
		{
			case IEEE80211_CCK_RATE_1MB:
			case IEEE80211_CCK_RATE_2MB:
			case IEEE80211_CCK_RATE_5MB:
			case IEEE80211_CCK_RATE_11MB:
			case IEEE80211_OFDM_RATE_6MB:
			case IEEE80211_OFDM_RATE_12MB:
			case IEEE80211_OFDM_RATE_24MB:
				mBratesOS[i] |= IEEE80211_BASIC_RATE_MASK;
				break;
		}
	}

}

static void _mgt_dispatcher(_adapter *padapter, struct mlme_handler *ptable, union recv_frame *precv_frame)
{
	u8 bc_addr[ETH_ALEN] = {0xff,0xff,0xff,0xff,0xff,0xff};
	u8 *pframe = precv_frame->u.hdr.rx_data; 
	uint len = precv_frame->u.hdr.len;

	  if(ptable->func)
        {
       	 //receive the frames that ra(a1) is my address or ra(a1) is bc address.
		if (!_rtw_memcmp(GetAddr1Ptr(pframe), myid(&padapter->eeprompriv), ETH_ALEN) &&
			!_rtw_memcmp(GetAddr1Ptr(pframe), bc_addr, ETH_ALEN)) 
		{
			return;
		}
		
		ptable->func(padapter, precv_frame);
        }
	
}

void mgt_dispatcher(_adapter *padapter, union recv_frame *precv_frame)
{
	int index;
	struct mlme_handler *ptable;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	u8 bc_addr[ETH_ALEN] = {0xff,0xff,0xff,0xff,0xff,0xff};
	u8 *pframe = precv_frame->u.hdr.rx_data;
	uint len = precv_frame->u.hdr.len;

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_,
		 ("+mgt_dispatcher: type(0x%x) subtype(0x%x)\n",
		  GetFrameType(pframe), GetFrameSubType(pframe)));

#if 0
	{
		u8 *pbuf;
		pbuf = GetAddr1Ptr(pframe);
		DBG_8192C("A1-%x:%x:%x:%x:%x:%x\n", *pbuf, *(pbuf+1), *(pbuf+2), *(pbuf+3), *(pbuf+4), *(pbuf+5));
		pbuf = GetAddr2Ptr(pframe);
		DBG_8192C("A2-%x:%x:%x:%x:%x:%x\n", *pbuf, *(pbuf+1), *(pbuf+2), *(pbuf+3), *(pbuf+4), *(pbuf+5));
		pbuf = GetAddr3Ptr(pframe);
		DBG_8192C("A3-%x:%x:%x:%x:%x:%x\n", *pbuf, *(pbuf+1), *(pbuf+2), *(pbuf+3), *(pbuf+4), *(pbuf+5));
	}
#endif

	if (GetFrameType(pframe) != WIFI_MGT_TYPE)
	{
		RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_, ("mgt_dispatcher: type(0x%x) error!\n", GetFrameType(pframe)));
		return;
	}

	//receive the frames that ra(a1) is my address or ra(a1) is bc address.
	if (!_rtw_memcmp(GetAddr1Ptr(pframe), myid(&padapter->eeprompriv), ETH_ALEN) &&
		!_rtw_memcmp(GetAddr1Ptr(pframe), bc_addr, ETH_ALEN))
	{
		return;
	}

	ptable = mlme_sta_tbl;

	index = GetFrameSubType(pframe) >> 4;

#ifdef CONFIG_TDLS
	if((index << 4)==WIFI_ACTION){
		//category==public (4), action==TDLS_DISCOVERY_RESPONSE
		if(*(pframe+24)==0x04 && *(pframe+25)==TDLS_DISCOVERY_RESPONSE){
			DBG_8192C("recv tdls discovery response frame\n");
			On_TDLS_Dis_Rsp(padapter, precv_frame);
		}
	}
#endif

	if (index > 13)
	{
		RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("Currently we do not support reserved sub-fr-type=%d\n", index));
		return;
	}
	ptable += index;

#if 0//gtest
	sa = get_sa(pframe);
	psta = search_assoc_sta(sa, padapter);
	// only check last cache seq number for management frame
	if (psta != NULL) {
		if (GetRetry(pframe)) {
			if (GetTupleCache(pframe) == psta->rxcache->nonqos_seq){
				RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("drop due to decache!\n"));
				return;
			}
		}
		psta->rxcache->nonqos_seq = GetTupleCache(pframe);
	}
#else

	if(GetRetry(pframe))
	{
		//RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("drop due to decache!\n"));
		//return;
	}
#endif

#ifdef CONFIG_AP_MODE
	switch (GetFrameSubType(pframe)) 
	{
		case WIFI_AUTH:
			if(check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE)
				ptable->func = &OnAuth;
			else
				ptable->func = &OnAuthClient;
			//pass through
		case WIFI_ASSOCREQ:
		case WIFI_REASSOCREQ:
			_mgt_dispatcher(padapter, ptable, precv_frame);	
#ifdef CONFIG_HOSTAPD_MLME				
			if(check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE)
				rtw_hostapd_mlme_rx(padapter, precv_frame);
#endif			
			break;
		case WIFI_PROBEREQ:
			if(check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE)
			{
#ifdef CONFIG_HOSTAPD_MLME		
				rtw_hostapd_mlme_rx(padapter, precv_frame);		
#else
				_mgt_dispatcher(padapter, ptable, precv_frame);
#endif
			}
			else
				_mgt_dispatcher(padapter, ptable, precv_frame);
			break;
		case WIFI_BEACON:			
			_mgt_dispatcher(padapter, ptable, precv_frame);
			break;
		case WIFI_ACTION:
			//if(check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE)
			_mgt_dispatcher(padapter, ptable, precv_frame);		
			break;
		default:
			_mgt_dispatcher(padapter, ptable, precv_frame);	
			if(check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE)
				rtw_hostapd_mlme_rx(padapter, precv_frame);			
			break;
	}
#else

	_mgt_dispatcher(padapter, ptable, precv_frame);	
	
#endif

}

#ifdef CONFIG_P2P
u32 p2p_listen_state_process(_adapter *padapter, unsigned char *da)
{
	issue_probersp_p2p( padapter, da);
	return _SUCCESS;
}
#endif //CONFIG_P2P


/****************************************************************************

Following are the callback functions for each subtype of the management frames

*****************************************************************************/

unsigned int OnProbeReq(_adapter *padapter, union recv_frame *precv_frame)
{
	unsigned int	ielen;
	unsigned char	*p;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX 	*cur = &(pmlmeinfo->network);
	u8 *pframe = precv_frame->u.hdr.rx_data;
	uint len = precv_frame->u.hdr.len;
	u8 is_valid_p2p_probereq = _FALSE;

#ifdef CONFIG_P2P
	struct wifidirect_info	*pwdinfo = &(padapter->wdinfo);
	struct rx_pkt_attrib	*pattrib = &precv_frame->u.hdr.attrib;
	
	if ( ( pwdinfo->p2p_state != P2P_STATE_NONE ) && 
		( pwdinfo->p2p_state != P2P_STATE_IDLE ) && 
		( pwdinfo->role != P2P_ROLE_CLIENT ) &&
		( pwdinfo->p2p_state != P2P_STATE_FIND_PHASE_SEARCH ) &&
		(pwdinfo->p2p_state != P2P_STATE_SCAN )
	   )
	{
		//	Commented by Albert 2011/03/17
		//	mcs_rate = 0 -> CCK 1M rate
		//	mcs_rate = 1 -> CCK 2M rate
		//	mcs_rate = 2 -> CCK 5.5M rate
		//	mcs_rate = 3 -> CCK 11M rate
		//	In the P2P mode, the driver should not support the CCK rate
		if ( pattrib->mcs_rate > 3 )
		{
			if((is_valid_p2p_probereq = process_probe_req_p2p_ie(pwdinfo, pframe, len)) == _TRUE)
			{
				if(pwdinfo->role == P2P_ROLE_DEVICE)
				{
					p2p_listen_state_process( padapter,  get_sa(pframe));

					return _SUCCESS;	
				}

				if(pwdinfo->role == P2P_ROLE_GO)
				{
					goto _continue;
				}
			}
		}
	}
#endif //CONFIG_P2P

_continue:

	if(check_fwstate(pmlmepriv, WIFI_STATION_STATE))
	{
		return _SUCCESS;
	}

	if(check_fwstate(pmlmepriv, _FW_LINKED) == _FALSE && 
		check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE|WIFI_AP_STATE)==_FALSE)
	{
		return _SUCCESS;
	}


	//DBG_871X("+OnProbeReq\n");

	p = rtw_get_ie(pframe + WLAN_HDR_A3_LEN + _PROBEREQ_IE_OFFSET_, _SSID_IE_, (int *)&ielen,
			len - WLAN_HDR_A3_LEN - _PROBEREQ_IE_OFFSET_);


	//check (wildcard) SSID 
	if (p != NULL)
	{
		if(is_valid_p2p_probereq == _TRUE)
		{
			goto _issue_probersp;
		}

		if ((ielen != 0) && (!_rtw_memcmp((void *)(p+2), (void *)cur->Ssid.Ssid, le32_to_cpu(cur->Ssid.SsidLength))))
		{
			return _SUCCESS;
		}

_issue_probersp:

		issue_probersp(padapter, get_sa(pframe), is_valid_p2p_probereq);		

	}

	return _SUCCESS;

}

unsigned int OnProbeRsp(_adapter *padapter, union recv_frame *precv_frame)
{
	struct sta_info		*psta;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct sta_priv		*pstapriv = &padapter->stapriv;
	u8	*pframe = precv_frame->u.hdr.rx_data;
#ifdef CONFIG_P2P
	struct wifidirect_info	*pwdinfo = &padapter->wdinfo;
#endif


#ifdef CONFIG_P2P
	if (P2P_STATE_TX_PROVISION_DIS_REQ == pwdinfo->p2p_state )
	{
		if ( _TRUE == pwdinfo->tx_prov_disc_info.benable )
		{
			if( _rtw_memcmp( pwdinfo->tx_prov_disc_info.peerIFAddr, GetAddr2Ptr(pframe), ETH_ALEN ) )
			{
				if ( P2P_ROLE_CLIENT == pwdinfo->role )
				{
					pwdinfo->tx_prov_disc_info.benable = _FALSE;
					issue_p2p_provision_request( padapter,
												pwdinfo->tx_prov_disc_info.peerIFAddr,
												pwdinfo->tx_prov_disc_info.ssid.Ssid, 
												pwdinfo->tx_prov_disc_info.ssid.SsidLength,
												pwdinfo->tx_prov_disc_info.peerDevAddr );
				}
				else if ( ( P2P_ROLE_DEVICE == pwdinfo->role ) || ( P2P_ROLE_GO == pwdinfo->role ) )
				{
					pwdinfo->tx_prov_disc_info.benable = _FALSE;
					issue_p2p_provision_request( padapter,
												pwdinfo->tx_prov_disc_info.peerIFAddr,
												NULL, 
												0,
												pwdinfo->tx_prov_disc_info.peerDevAddr );
				}
			}		
		}
		return _SUCCESS;
	}
	else if ( P2P_STATE_GONEGO_ING == pwdinfo->p2p_state )
	{
		if ( _TRUE == pwdinfo->nego_req_info.benable )
		{
			printk( "[%s] P2P State is GONEGO ING!\n", __FUNCTION__ );
			if( _rtw_memcmp( pwdinfo->nego_req_info.peerDevAddr, GetAddr2Ptr(pframe), ETH_ALEN ) )
			{
				pwdinfo->nego_req_info.benable = _FALSE;
				issue_p2p_GO_request( padapter, pwdinfo->nego_req_info.peerDevAddr);
			}
		}
	}
#endif


	if (pmlmeext->sitesurvey_res.state == SCAN_PROCESS)
	{
		report_survey_event(padapter, precv_frame);	
		return _SUCCESS;
	}

	if (_rtw_memcmp(GetAddr3Ptr(pframe), get_my_bssid(&pmlmeinfo->network), ETH_ALEN))
	{
		if (pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS)
		{
			if ((psta = rtw_get_stainfo(pstapriv, GetAddr2Ptr(pframe))) != NULL)
			{
				psta->sta_stats.rx_pkts++;
			}
		}
	}
	
	return _SUCCESS;
	
}

unsigned int OnBeacon(_adapter *padapter, union recv_frame *precv_frame)
{
	int cam_idx;
	struct sta_info	*psta;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct sta_priv	*pstapriv = &padapter->stapriv;
	u8 *pframe = precv_frame->u.hdr.rx_data;
	uint len = precv_frame->u.hdr.len;

	if (pmlmeext->sitesurvey_res.state == SCAN_PROCESS)
	{
		report_survey_event(padapter, precv_frame);

		return _SUCCESS;
	}

	if (_rtw_memcmp(GetAddr3Ptr(pframe), get_my_bssid(&pmlmeinfo->network), ETH_ALEN))
	{
		if (pmlmeinfo->state & WIFI_FW_AUTH_NULL)
		{
			//check the vendor of the assoc AP
			pmlmeinfo->assoc_AP_vendor = check_assoc_AP(pframe+sizeof(struct ieee80211_hdr_3addr), len-sizeof(struct ieee80211_hdr_3addr));				

			//update TSF Value
			update_TSF(pmlmeext, pframe, len);

			//start auth
			start_clnt_auth(padapter);

			return _SUCCESS;
		}

		if(((pmlmeinfo->state&0x03) == WIFI_FW_STATION_STATE) && (pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS))
		{
			if ((psta = rtw_get_stainfo(pstapriv, GetAddr2Ptr(pframe))) != NULL)
			{
				//update WMM, ERP in the beacon
				//todo: the timer is used instead of the number of the beacon received
				if ((psta->sta_stats.rx_pkts & 0xf) == 0)
				{
					//DBG_871X("update_bcn_info\n");
					update_beacon_info(padapter, pframe, len, psta);
				}
#ifdef CONFIG_P2P
				process_p2p_ps_ie(padapter, (pframe + WLAN_HDR_A3_LEN), (len - WLAN_HDR_A3_LEN));
#endif //CONFIG_P2P
				psta->sta_stats.rx_pkts++;
			}
		}
		else if((pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE)
		{
			if ((psta = rtw_get_stainfo(pstapriv, GetAddr2Ptr(pframe))) != NULL)
			{
				//update WMM, ERP in the beacon
				//todo: the timer is used instead of the number of the beacon received
				if ((psta->sta_stats.rx_pkts & 0xf) == 0)
				{
					//DBG_871X("update_bcn_info\n");
					update_beacon_info(padapter, pframe, len, psta);
				}
				psta->sta_stats.rx_pkts++;
			}
			else
			{
				//allocate a new CAM entry for IBSS station
				if ((cam_idx = allocate_fw_sta_entry(padapter)) == NUM_STA)
				{
					goto _END_ONBEACON_;
				}

				//get supported rate
				if (update_sta_support_rate(padapter, (pframe + WLAN_HDR_A3_LEN + _BEACON_IE_OFFSET_), (len - WLAN_HDR_A3_LEN - _BEACON_IE_OFFSET_), cam_idx) == _FAIL)
				{
					pmlmeinfo->FW_sta_info[cam_idx].status = 0;
					goto _END_ONBEACON_;
				}

				//update TSF Value
				update_TSF(pmlmeext, pframe, len);			

				//report sta add event
				report_add_sta_event(padapter, GetAddr2Ptr(pframe), cam_idx);

				//pmlmeext->linked_to = LINKED_TO;
			}
		}
	}

_END_ONBEACON_:

	return _SUCCESS;

}

unsigned int OnAuth(_adapter *padapter, union recv_frame *precv_frame)
{
#ifdef CONFIG_AP_MODE
	unsigned int	auth_mode, seq, ie_len;	
	unsigned char	*sa, *p;	
	u16	algorithm;
	int	status;
	static struct sta_info stat;	
	struct	sta_info	*pstat=NULL;	
	struct	sta_priv *pstapriv = &padapter->stapriv;
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	u8 *pframe = precv_frame->u.hdr.rx_data; 
	uint len = precv_frame->u.hdr.len;

	if((pmlmeinfo->state&0x03) != WIFI_FW_AP_STATE)
		return _FAIL;

	DBG_871X("+OnAuth\n");
	
	sa = GetAddr2Ptr(pframe);
	
	auth_mode = psecuritypriv->dot11AuthAlgrthm;
	seq = cpu_to_le16(*(unsigned short *)((unsigned int)pframe + WLAN_HDR_A3_LEN + 2));
	algorithm = cpu_to_le16(*(unsigned short *)((unsigned int)pframe + WLAN_HDR_A3_LEN));

	if (GetPrivacy(pframe))
	{	
#if 0 //TODO: SW rtw_wep_decrypt
		if (SWCRYPTO)
		{
			status = rtw_wep_decrypt(priv, pframe, pfrinfo->pktlen,
				priv->pmib->dot1180211AuthEntry.dot11PrivacyAlgrthm);
			if (status == FALSE)
			{
				SAVE_INT_AND_CLI(flags);
				RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,"wep-decrypt a Auth frame error!\n");
				status = _STATS_CHALLENGE_FAIL_;
				goto auth_fail;
			}
		}

		seq = cpu_to_le16(*(unsigned short *)((unsigned int)pframe + WLAN_HDR_A3_LEN + 4 + 2));
		algorithm = cpu_to_le16(*(unsigned short *)((unsigned int)pframe + WLAN_HDR_A3_LEN + 4));
#endif
	}


	DBG_871X("auth alg=%x, seq=%X\n", algorithm, seq);

	if (auth_mode == 2 &&
			psecuritypriv->dot11PrivacyAlgrthm != _WEP40_ &&
			psecuritypriv->dot11PrivacyAlgrthm != _WEP104_)
		auth_mode = 0;

	if ((algorithm > 0 && auth_mode == 0) ||	// rx a shared-key auth but shared not enabled
		(algorithm == 0 && auth_mode == 1) )	// rx a open-system auth but shared-key is enabled
	{		
		DBG_871X("auth rejected due to bad alg [alg=%d, auth_mib=%d] %02X%02X%02X%02X%02X%02X\n",
			algorithm, auth_mode, sa[0], sa[1], sa[2], sa[3], sa[4], sa[5]);
		
		status = _STATS_NO_SUPP_ALG_;
		
		goto auth_fail;
	}
	
#if 0 //TODO:ACL control	
	phead = &priv->wlan_acl_list;
	plist = phead->next;
	//check sa
	if (acl_mode == 1)		// 1: positive check, only those on acl_list can be connected.
		res = FAIL;
	else
		res = SUCCESS;

	while(plist != phead)
	{
		paclnode = list_entry(plist, struct rtw_wlan_acl_node, list);
		plist = plist->next;
		if (!memcmp((void *)sa, paclnode->addr, 6)) {
			if (paclnode->mode & 2) { // deny
				res = FAIL;
				break;
			}
			else {
				res = SUCCESS;
				break;
			}
		}
	}

	if (res != SUCCESS) {
		RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,"auth abort because ACL!\n");
		return FAIL;
	}
#endif

	pstat = rtw_get_stainfo(pstapriv, sa);
	if (pstat == NULL)
	{
		// allocate a new one
		DBG_871X("going to alloc stainfo for sa=%02X%02X%02X%02X%02X%02X\n",  sa[0],sa[1],sa[2],sa[3],sa[4],sa[5]);
		pstat = rtw_alloc_stainfo(pstapriv, sa);
		if (pstat == NULL)
		{
			DBG_871X(" Exceed the upper limit of supported clients...\n");
			status = _STATS_UNABLE_HANDLE_STA_;
			goto auth_fail;
		}
		
		pstat->state = WIFI_FW_AUTH_NULL;
		pstat->auth_seq = 0;
		
		//pstat->flags = 0;
		//pstat->capability = 0;
	}
	else
	{		
		if(rtw_is_list_empty(&pstat->asoc_list)==_FALSE)
		{			
			rtw_list_delete(&pstat->asoc_list);
			if (pstat->expire_to > 0)
			{
				//TODO: STA re_auth within expire_to
			}
		}
		if (seq==1) {
			//TODO: STA re_auth and auth timeout 
		}
	}

	if (rtw_is_list_empty(&pstat->auth_list))
	{		
		rtw_list_insert_tail(&pstat->auth_list, &pstapriv->auth_list);
	}	
		

	if (pstat->auth_seq == 0)
		pstat->expire_to = pstapriv->auth_to;

	if ((pstat->auth_seq + 1) != seq)
	{
		DBG_871X("(1)auth rejected because out of seq [rx_seq=%d, exp_seq=%d]!\n",
			seq, pstat->auth_seq+1);
		status = _STATS_OUT_OF_AUTH_SEQ_;
		goto auth_fail;
	}

	if (algorithm==0 && (auth_mode == 0 || auth_mode == 2))
	{
		if (seq == 1)
		{
			pstat->state &= ~WIFI_FW_AUTH_NULL;
			pstat->state |= WIFI_FW_AUTH_SUCCESS;
			pstat->expire_to = pstapriv->assoc_to;
			pstat->authalg = algorithm;
		}
		else
		{
			DBG_871X("(2)auth rejected because out of seq [rx_seq=%d, exp_seq=%d]!\n",
				seq, pstat->auth_seq+1);
			status = _STATS_OUT_OF_AUTH_SEQ_;
			goto auth_fail;
		}
	}
	else // shared system or auto authentication
	{
		if (seq == 1)
		{
			//prepare for the challenging txt...
			
			//get_random_bytes((void *)pstat->chg_txt, 128);//TODO:
			
			pstat->state &= ~WIFI_FW_AUTH_NULL;
			pstat->state |= WIFI_FW_AUTH_STATE;
			pstat->authalg = algorithm;
			pstat->auth_seq = 2;
		}
		else if (seq == 3)
		{
			//checking for challenging txt...
			DBG_871X("checking for challenging txt...\n");
			
			p = rtw_get_ie(pframe + WLAN_HDR_A3_LEN + 4 + _AUTH_IE_OFFSET_ , _CHLGETXT_IE_, (int *)&ie_len,
					len - WLAN_HDR_A3_LEN - _AUTH_IE_OFFSET_ - 4);

			if((p==NULL) || (ie_len<=0))
			{
				DBG_871X("auth rejected because challenge failure!(1)\n");
				status = _STATS_CHALLENGE_FAIL_;
				goto auth_fail;
			}
			
			if (_rtw_memcmp((void *)(p + 2), pstat->chg_txt, 128))
			{
				pstat->state &= (~WIFI_FW_AUTH_STATE);
				pstat->state |= WIFI_FW_AUTH_SUCCESS;
				// challenging txt is correct...
				pstat->expire_to =  pstapriv->assoc_to;
			}
			else
			{
				DBG_871X("auth rejected because challenge failure!\n");
				status = _STATS_CHALLENGE_FAIL_;
				goto auth_fail;
			}
		}
		else
		{
			DBG_871X("(3)auth rejected because out of seq [rx_seq=%d, exp_seq=%d]!\n",
				seq, pstat->auth_seq+1);
			status = _STATS_OUT_OF_AUTH_SEQ_;
			goto auth_fail;
		}
	}


	// Now, we are going to issue_auth...
	pstat->auth_seq = seq + 1;	
	
#ifdef CONFIG_NATIVEAP_MLME
	issue_auth(padapter, pstat, (unsigned short)(_STATS_SUCCESSFUL_));
#endif

	if (pstat->state & WIFI_FW_AUTH_SUCCESS)
		pstat->auth_seq = 0;

		
	return _SUCCESS;

auth_fail:

	if (pstat) 
	{
		pstat = &stat;
		_rtw_memset((char *)pstat, '\0', sizeof(stat));
		pstat->auth_seq = 2;
		_rtw_memcpy(pstat->hwaddr, sa, 6);
	}
	
#ifdef CONFIG_NATIVEAP_MLME
	issue_auth(padapter, pstat, (unsigned short)status);	
#endif

#endif
	return _FAIL;

}

unsigned int OnAuthClient(_adapter *padapter, union recv_frame *precv_frame)
{
	unsigned int	seq, len, status, algthm, offset;
	unsigned char	*p;
	unsigned int	go2asoc = 0;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	u8 *pframe = precv_frame->u.hdr.rx_data;
	uint pkt_len = precv_frame->u.hdr.len;

	DBG_871X("%s\n", __FUNCTION__);

	//check A1 matches or not
	if (!_rtw_memcmp(myid(&(padapter->eeprompriv)), get_da(pframe), ETH_ALEN))
		return _SUCCESS;

	if (!(pmlmeinfo->state & WIFI_FW_AUTH_STATE))
		return _SUCCESS;

	offset = (GetPrivacy(pframe))? 4: 0;

	algthm 	= le16_to_cpu(*(unsigned short *)((SIZE_PTR)pframe + WLAN_HDR_A3_LEN + offset));
	seq 	= le16_to_cpu(*(unsigned short *)((SIZE_PTR)pframe + WLAN_HDR_A3_LEN + offset + 2));
	status 	= le16_to_cpu(*(unsigned short *)((SIZE_PTR)pframe + WLAN_HDR_A3_LEN + offset + 4));

	if (status != 0)
	{
		DBG_871X("clnt auth fail, status: %d\n", status);
		goto authclnt_fail;
	}

	if (seq == 2)
	{
		if (pmlmeinfo->auth_algo == dot11AuthAlgrthm_Shared)
		{
			 // legendary shared system
			p = rtw_get_ie(pframe + WLAN_HDR_A3_LEN + _AUTH_IE_OFFSET_, _CHLGETXT_IE_, (int *)&len,
				pkt_len - WLAN_HDR_A3_LEN - _AUTH_IE_OFFSET_);

			if (p == NULL)
			{
				//DBG_8192C("marc: no challenge text?\n");
				goto authclnt_fail;
			}

			_rtw_memcpy((void *)(pmlmeinfo->chg_txt), (void *)(p + 2), len);
			pmlmeinfo->auth_seq = 3;
			issue_auth(padapter, NULL, 0);
			set_link_timer(pmlmeext, REAUTH_TO);

			return _SUCCESS;
		}
		else
		{
			// open system
			go2asoc = 1;
		}
	}
	else if (seq == 4)
	{
		if (pmlmeinfo->auth_algo == dot11AuthAlgrthm_Shared)
		{
			go2asoc = 1;
		}
		else
		{
			goto authclnt_fail;
		}
	}
	else
	{
		// this is also illegal
		//DBG_8192C("marc: clnt auth failed due to illegal seq=%x\n", seq);
		goto authclnt_fail;
	}

	if (go2asoc)
	{
		start_clnt_assoc(padapter);
		return _SUCCESS;
	}

authclnt_fail:

	//pmlmeinfo->state &= ~(WIFI_FW_AUTH_STATE);

	return _FAIL;

}

unsigned int OnAssocReq(_adapter *padapter, union recv_frame *precv_frame)
{
#ifdef CONFIG_AP_MODE
	u16 capab_info, listen_interval;
	struct ieee802_11_elems elems;	
	struct sta_info	*pstat;
	unsigned char		reassoc, *p, *pos, *wpa_ie;
	unsigned char		rsnie_hdr[4]={0x00, 0x50, 0xf2, 0x01};
	unsigned char WMM_IE[] = {0x00, 0x50, 0xf2, 0x02, 0x00, 0x01};
	int		i, ie_len, wpa_ie_len, left;
	unsigned long		flags;
	unsigned char		supportRate[16];
	int					supportRateNum;
	unsigned short		status = _STATS_SUCCESSFUL_;
	unsigned short		frame_type, ie_offset=0;	
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);	
	WLAN_BSSID_EX 	*cur = &(pmlmeinfo->network);
	struct sta_priv *pstapriv = &padapter->stapriv;
	u8 *pframe = precv_frame->u.hdr.rx_data;
	uint pkt_len = precv_frame->u.hdr.len;
#ifdef CONFIG_P2P
	struct wifidirect_info	*pwdinfo = &(padapter->wdinfo);
	u8 p2p_status_code = P2P_STATUS_SUCCESS;
	u8 p2pie[ MAX_P2P_IE_LEN] = { 0x00 };
	u32 p2pielen = 0;
#endif //CONFIG_P2P

	if((pmlmeinfo->state&0x03) != WIFI_FW_AP_STATE)
		return _FAIL;
	
	frame_type = GetFrameSubType(pframe);
	if (frame_type == WIFI_ASSOCREQ)
	{
		reassoc = 0;
		ie_offset = _ASOCREQ_IE_OFFSET_;
	}	
	else // WIFI_REASSOCREQ
	{
		reassoc = 1;
		ie_offset = _REASOCREQ_IE_OFFSET_;
	}
	

	if (pkt_len < IEEE80211_3ADDR_LEN + ie_offset) {
		DBG_871X("handle_assoc(reassoc=%d) - too short payload (len=%lu)"
		       "\n", reassoc, (unsigned long)pkt_len);
		return _FAIL;
	}
	
	pstat = rtw_get_stainfo(pstapriv, GetAddr2Ptr(pframe));
	if (pstat == (struct sta_info *)NULL)
	{
		status = _RSON_CLS2_;
		goto asoc_class2_error;
	}

	capab_info = RTW_GET_LE16(pframe + WLAN_HDR_A3_LEN);
	//capab_info = le16_to_cpu(*(unsigned short *)(pframe + WLAN_HDR_A3_LEN));	
	//listen_interval = le16_to_cpu(*(unsigned short *)(pframe + WLAN_HDR_A3_LEN+2));
	listen_interval = RTW_GET_LE16(pframe + WLAN_HDR_A3_LEN+2);

	left = pkt_len - (IEEE80211_3ADDR_LEN + ie_offset);
	pos = pframe + (IEEE80211_3ADDR_LEN + ie_offset);
	

	DBG_871X("%s\n", __FUNCTION__);

	// check if this stat has been successfully authenticated/assocated
	if (!((pstat->state) & WIFI_FW_AUTH_SUCCESS))
	{
		if (!((pstat->state) & WIFI_FW_ASSOC_SUCCESS))
		{
			status = _RSON_CLS2_;
			goto asoc_class2_error;
		}
		else
		{
			pstat->state &= (~WIFI_FW_ASSOC_SUCCESS);
			pstat->state |= WIFI_FW_ASSOC_STATE;				
		}
	}
	else
	{
		pstat->state &= (~WIFI_FW_AUTH_SUCCESS);
		pstat->state |= WIFI_FW_ASSOC_STATE;
	}


#if 0// todo:tkip_countermeasures
	if (hapd->tkip_countermeasures) {
		resp = WLAN_REASON_MICHAEL_MIC_FAILURE;
		goto fail;
	}
#endif

	pstat->capability = capab_info;

#if 0//todo:
	//check listen_interval
	if (listen_interval > hapd->conf->max_listen_interval) {
		hostapd_logger(hapd, mgmt->sa, HOSTAPD_MODULE_IEEE80211,
			       HOSTAPD_LEVEL_DEBUG,
			       "Too large Listen Interval (%d)",
			       listen_interval);
		resp = WLAN_STATUS_ASSOC_DENIED_LISTEN_INT_TOO_LARGE;
		goto fail;
	}
	
	pstat->listen_interval = listen_interval;
#endif

	//now parse all ieee802_11 ie to point to elems
	if (rtw_ieee802_11_parse_elems(pos, left, &elems, 1) == ParseFailed ||
	    !elems.ssid) {
		DBG_871X("STA " MAC_FMT " sent invalid association request\n",
		       MAC_ARG(pstat->hwaddr));
		status = _STATS_FAILURE_;		
		goto OnAssocReqFail;
	}


	// now we should check all the fields...
	// checking SSID
	p = rtw_get_ie(pframe + WLAN_HDR_A3_LEN + ie_offset, _SSID_IE_, &ie_len,
		pkt_len - WLAN_HDR_A3_LEN - ie_offset);
	if (p == NULL)
	{
		status = _STATS_FAILURE_;		
	}

	if (ie_len == 0) // broadcast ssid, however it is not allowed in assocreq
		status = _STATS_FAILURE_;
	else
	{
		// check if ssid match
		if (!_rtw_memcmp((void *)(p+2), cur->Ssid.Ssid, cur->Ssid.SsidLength))
			status = _STATS_FAILURE_;

		if (ie_len != cur->Ssid.SsidLength)
			status = _STATS_FAILURE_;
	}

	if(_STATS_SUCCESSFUL_ != status)
		goto OnAssocReqFail;

	// check if the supported rate is ok
	p = rtw_get_ie(pframe + WLAN_HDR_A3_LEN + ie_offset, _SUPPORTEDRATES_IE_, &ie_len, pkt_len - WLAN_HDR_A3_LEN - ie_offset);
	if (p == NULL) {
		DBG_871X("Rx a sta assoc-req which supported rate is empty!\n");
		// use our own rate set as statoin used
		//_rtw_memcpy(supportRate, AP_BSSRATE, AP_BSSRATE_LEN);
		//supportRateNum = AP_BSSRATE_LEN;
		
		status = _STATS_FAILURE_;
		goto OnAssocReqFail;
	}
	else {
		_rtw_memcpy(supportRate, p+2, ie_len);
		supportRateNum = ie_len;

		p = rtw_get_ie(pframe + WLAN_HDR_A3_LEN + ie_offset, _EXT_SUPPORTEDRATES_IE_ , &ie_len,
				pkt_len - WLAN_HDR_A3_LEN - ie_offset);
		if (p !=  NULL) {
			
			if(supportRateNum<=sizeof(supportRate))
			{
				_rtw_memcpy(supportRate+supportRateNum, p+2, ie_len);
				supportRateNum += ie_len;
			}			
		}
	}

	//todo: mask supportRate between AP & STA -> move to update raid
	//get_matched_rate(pmlmeext, supportRate, &supportRateNum, 0);

	//update station supportRate	
	pstat->bssratelen = supportRateNum;
	_rtw_memcpy(pstat->bssrateset, supportRate, supportRateNum);


	//check RSN/WPA/WPS
	pstat->dot8021xalg = 0;
      	pstat->wpa_psk = 0;
	pstat->wpa_group_cipher = 0;
	pstat->wpa2_group_cipher = 0;
	pstat->wpa_pairwise_cipher = 0;
	pstat->wpa2_pairwise_cipher = 0;
	_rtw_memset(pstat->wpa_ie, 0, sizeof(pstat->wpa_ie));
	if((psecuritypriv->wpa_psk & BIT(1)) && elems.rsn_ie) {

		int group_cipher=0, pairwise_cipher=0;	
		
		wpa_ie = elems.rsn_ie;
		wpa_ie_len = elems.rsn_ie_len;

		if(rtw_parse_wpa2_ie(wpa_ie-2, wpa_ie_len+2, &group_cipher, &pairwise_cipher) == _SUCCESS)
		{
			pstat->dot8021xalg = 1;//psk,  todo:802.1x						
			pstat->wpa_psk |= BIT(1);

			pstat->wpa2_group_cipher = group_cipher&psecuritypriv->wpa2_group_cipher;				
			pstat->wpa2_pairwise_cipher = pairwise_cipher&psecuritypriv->wpa2_pairwise_cipher;
			
			if(!pstat->wpa2_group_cipher)
				status = WLAN_STATUS_GROUP_CIPHER_NOT_VALID;

			if(!pstat->wpa2_pairwise_cipher)
				status = WLAN_STATUS_PAIRWISE_CIPHER_NOT_VALID;
		}
		else
		{
			status = WLAN_STATUS_INVALID_IE;
		}	
			
	} else if ((psecuritypriv->wpa_psk & BIT(0)) && elems.wpa_ie) {

		int group_cipher=0, pairwise_cipher=0;	
		
		wpa_ie = elems.wpa_ie;
		wpa_ie_len = elems.wpa_ie_len;

		if(rtw_parse_wpa_ie(wpa_ie-2, wpa_ie_len+2, &group_cipher, &pairwise_cipher) == _SUCCESS)
		{
			pstat->dot8021xalg = 1;//psk,  todo:802.1x						
			pstat->wpa_psk |= BIT(0);

			pstat->wpa_group_cipher = group_cipher&psecuritypriv->wpa_group_cipher;				
			pstat->wpa_pairwise_cipher = pairwise_cipher&psecuritypriv->wpa_pairwise_cipher;
			
			if(!pstat->wpa_group_cipher)
				status = WLAN_STATUS_GROUP_CIPHER_NOT_VALID;

			if(!pstat->wpa_pairwise_cipher)
				status = WLAN_STATUS_PAIRWISE_CIPHER_NOT_VALID;
		
		}
		else
		{
			status = WLAN_STATUS_INVALID_IE;
		}
		
	} else {
		wpa_ie = NULL;
		wpa_ie_len = 0;
	}

	if(_STATS_SUCCESSFUL_ != status)
		goto OnAssocReqFail;

	pstat->flags &= ~(WLAN_STA_WPS | WLAN_STA_MAYBE_WPS);
	//if (hapd->conf->wps_state && wpa_ie == NULL) { //todo: to check ap if supporting WPS
	if(wpa_ie == NULL) {
		if (elems.wps_ie) {
			DBG_871X("STA included WPS IE in "
				   "(Re)Association Request - assume WPS is "
				   "used\n");
			pstat->flags |= WLAN_STA_WPS;
			//wpabuf_free(sta->wps_ie);
			//sta->wps_ie = wpabuf_alloc_copy(elems.wps_ie + 4,
			//				elems.wps_ie_len - 4);
		} else {
			DBG_871X("STA did not include WPA/RSN IE "
				   "in (Re)Association Request - possible WPS "
				   "use\n");
			pstat->flags |= WLAN_STA_MAYBE_WPS;
		}
	}
	else
	{
		int copy_len;

		if(psecuritypriv->wpa_psk == 0)
		{
			DBG_871X("STA " MAC_FMT ": WPA/RSN IE in association "
		       	"request, but AP don't support WPA/RSN\n", MAC_ARG(pstat->hwaddr));
			
			status = WLAN_STATUS_INVALID_IE;
			
			goto OnAssocReqFail;

		}

		if (elems.wps_ie) {
			DBG_871X("STA included WPS IE in "
				   "(Re)Association Request - WPS is "
				   "used\n");
			pstat->flags |= WLAN_STA_WPS;
			copy_len=0;
		}
		else
		{
			copy_len = ((wpa_ie_len+2) > sizeof(pstat->wpa_ie)) ? (sizeof(pstat->wpa_ie)):(wpa_ie_len+2);
		}


		if(copy_len>0)
			_rtw_memcpy(pstat->wpa_ie, wpa_ie-2, copy_len);
		
	}


	// check if there is WMM IE & support WWM-PS
	pstat->flags &= ~WLAN_STA_WME;
	pstat->qos_option = 0;
	pstat->qos_info = 0;
	pstat->has_legacy_ac = _TRUE;
	pstat->uapsd_vo = 0;
	pstat->uapsd_vi = 0;
	pstat->uapsd_be = 0;
	pstat->uapsd_bk = 0;
	if (pmlmepriv->qospriv.qos_option) 
	{
		p = pframe + WLAN_HDR_A3_LEN + ie_offset; ie_len = 0;
		for (;;) 
		{
			p = rtw_get_ie(p, _VENDOR_SPECIFIC_IE_, &ie_len, pkt_len - WLAN_HDR_A3_LEN - ie_offset);
			if (p != NULL) {
				if (_rtw_memcmp(p+2, WMM_IE, 6)) {

					pstat->flags |= WLAN_STA_WME;
					
					pstat->qos_option = 1;				
					pstat->qos_info = *(p+8);
					
					pstat->max_sp_len = (pstat->qos_info>>5)&0x3;

					if((pstat->qos_info&0xf) !=0xf)
						pstat->has_legacy_ac = _TRUE;
					else
						pstat->has_legacy_ac = _FALSE;
					
					if(pstat->qos_info&0xf)
					{
						if(pstat->qos_info&BIT(0))
							pstat->uapsd_vo = BIT(0)|BIT(1);
						else
							pstat->uapsd_vo = 0;
		
						if(pstat->qos_info&BIT(1))
							pstat->uapsd_vi = BIT(0)|BIT(1);
						else
							pstat->uapsd_vi = 0;
			
						if(pstat->qos_info&BIT(2))
							pstat->uapsd_bk = BIT(0)|BIT(1);
						else
							pstat->uapsd_bk = 0;
			
						if(pstat->qos_info&BIT(3))			
							pstat->uapsd_be = BIT(0)|BIT(1);
						else
							pstat->uapsd_be = 0;
		
					}
	
					break;
				}
			}
			else {
				break;
			}
			p = p + ie_len + 2;
		}
	}


#ifdef CONFIG_80211N_HT
	/* save HT capabilities in the sta object */
	_rtw_memset(&pstat->htpriv.ht_cap, 0, sizeof(struct ieee80211_ht_cap));
	if (elems.ht_capabilities && elems.ht_capabilities_len >= sizeof(struct ieee80211_ht_cap)) 
	{
		pstat->flags |= WLAN_STA_HT;
		
		pstat->flags |= WLAN_STA_WME;
		
		_rtw_memcpy(&pstat->htpriv.ht_cap, elems.ht_capabilities, sizeof(struct ieee80211_ht_cap));			
		
	} else
		pstat->flags &= ~WLAN_STA_HT;

	
	if((pmlmepriv->htpriv.ht_option == _FALSE) && (pstat->flags&WLAN_STA_HT))
	{
		status = _STATS_FAILURE_;
		goto OnAssocReqFail;
	}
		

	if ((pstat->flags & WLAN_STA_HT) &&
		    ((pstat->wpa2_pairwise_cipher&WPA_CIPHER_TKIP) ||
		      (pstat->wpa_pairwise_cipher&WPA_CIPHER_TKIP)))
	{		    
		DBG_871X("HT: " MAC_FMT " tried to "
				   "use TKIP with HT association\n", MAC_ARG(pstat->hwaddr));
		
		//status = WLAN_STATUS_CIPHER_REJECTED_PER_POLICY;
		//goto OnAssocReqFail;
	}
#endif /* CONFIG_80211N_HT */

       //
       //if (hapd->iface->current_mode->mode == HOSTAPD_MODE_IEEE80211G)//?
	pstat->flags |= WLAN_STA_NONERP;	
	for (i = 0; i < pstat->bssratelen; i++) {
		if ((pstat->bssrateset[i] & 0x7f) > 22) {
			pstat->flags &= ~WLAN_STA_NONERP;
			break;
		}
	}

	if (pstat->capability & WLAN_CAPABILITY_SHORT_PREAMBLE)
		pstat->flags |= WLAN_STA_SHORT_PREAMBLE;
	else
		pstat->flags &= ~WLAN_STA_SHORT_PREAMBLE;

	
	
	if (status != _STATS_SUCCESSFUL_)
		goto OnAssocReqFail;

#ifdef CONFIG_P2P
	pstat->is_p2p_device = _FALSE;
	if(pwdinfo->role == P2P_ROLE_GO)
	{		
		if(rtw_get_p2p_ie(pframe + WLAN_HDR_A3_LEN + ie_offset , pkt_len - WLAN_HDR_A3_LEN - ie_offset , p2pie, &p2pielen))
		{
			pstat->is_p2p_device = _TRUE;
			if((p2p_status_code=(u8)process_assoc_req_p2p_ie(pwdinfo, p2pie, p2pielen, pstat))>0)
			{
				pstat->p2p_status_code = p2p_status_code;
				status = _STATS_CAP_FAIL_;
				goto OnAssocReqFail;
			}
		}
	}	
	pstat->p2p_status_code = p2p_status_code;
#endif //CONFIG_P2P

	//TODO: identify_proprietary_vendor_ie();
	// Realtek proprietary IE
	// identify if this is Broadcom sta
	// identify if this is ralink sta
	// Customer proprietary IE

	

	/* get a unique AID */
	if (pstat->aid > 0) {
		DBG_871X("  old AID %d\n", pstat->aid);
	} else {
		for (pstat->aid = 1; pstat->aid <= NUM_STA; pstat->aid++)
			if (pstapriv->sta_aid[pstat->aid - 1] == NULL)
				break;
				
		//if (pstat->aid > NUM_STA) {
		if (pstat->aid > pstapriv->max_num_sta) {
				
			pstat->aid = 0;
				
			DBG_871X("  no room for more AIDs\n");

			status = WLAN_STATUS_AP_UNABLE_TO_HANDLE_NEW_STA;
				
			goto OnAssocReqFail;
				
			
		} else {
			pstapriv->sta_aid[pstat->aid - 1] = pstat;
			DBG_871X("allocate new AID = (%d)\n", pstat->aid);
		}	
	}


	pstat->state &= (~WIFI_FW_ASSOC_STATE);	
	pstat->state |= WIFI_FW_ASSOC_SUCCESS;
	
	if (!rtw_is_list_empty(&pstat->auth_list))
	{
		rtw_list_delete(&pstat->auth_list);
	}
	if (rtw_is_list_empty(&pstat->asoc_list))
	{
		pstat->expire_to = pstapriv->expire_to;
		rtw_list_insert_tail(&pstat->asoc_list, &pstapriv->asoc_list);		
	}
	

	// now the station is qualified to join our BSS...	
	if(pstat && (pstat->state & WIFI_FW_ASSOC_SUCCESS) && (_STATS_SUCCESSFUL_==status))
	{
#ifdef CONFIG_NATIVEAP_MLME
		//.1 bss_cap_update
		//bss_cap_update(padapter, pstat);


		//.2 -
		DBG_871X("indicate_sta_join_event to upper layer - hostapd\n");		 
		rtw_indicate_sta_assoc_event(padapter, pstat);
		
	
		//.3-(1) report sta add event
		report_add_sta_event(padapter, pstat->hwaddr, pstat->aid);
		
		//.3 -(2)
		//sta_info_update(padapter, pstat);		
		
		if (frame_type == WIFI_ASSOCREQ)
			issue_asocrsp(padapter, status, pstat, WIFI_ASSOCRSP);
		else
			issue_asocrsp(padapter, status, pstat, WIFI_REASSOCRSP);
	
#endif
	}

	return _SUCCESS;

asoc_class2_error:

#ifdef CONFIG_NATIVEAP_MLME
	issue_deauth(padapter, (void *)GetAddr2Ptr(pframe), status);
#endif

	return _FAIL;		

OnAssocReqFail:


#ifdef CONFIG_NATIVEAP_MLME
	pstat->aid = 0;
	if (frame_type == WIFI_ASSOCREQ)
		issue_asocrsp(padapter, status, pstat, WIFI_ASSOCRSP);
	else
		issue_asocrsp(padapter, status, pstat, WIFI_REASSOCRSP);
#endif


#endif /* CONFIG_AP_MODE */

	return _FAIL;		

}

unsigned int OnAssocRsp(_adapter *padapter, union recv_frame *precv_frame)
{
	uint i;
	int res;
	unsigned short	status;
	PNDIS_802_11_VARIABLE_IEs	pIE;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	//WLAN_BSSID_EX 		*cur_network = &(pmlmeinfo->network);
	u8 *pframe = precv_frame->u.hdr.rx_data;
	uint pkt_len = precv_frame->u.hdr.len;

	DBG_871X("%s\n", __FUNCTION__);
	
	//check A1 matches or not
	if (!_rtw_memcmp(myid(&(padapter->eeprompriv)), get_da(pframe), ETH_ALEN))
		return _SUCCESS;

	if (!(pmlmeinfo->state & (WIFI_FW_AUTH_SUCCESS | WIFI_FW_ASSOC_STATE)))
		return _SUCCESS;

	if (pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS)
		return _SUCCESS;

	_cancel_timer_ex(&pmlmeext->link_timer);

	//status
	if ((status = le16_to_cpu(*(unsigned short *)(pframe + WLAN_HDR_A3_LEN + 2))) > 0)
	{
		DBG_871X("assoc reject, status code: %d\n", status);
		pmlmeinfo->state = WIFI_FW_NULL_STATE;
		res = -4;
		goto report_assoc_result;
	}

	//get capabilities
	pmlmeinfo->capability = le16_to_cpu(*(unsigned short *)(pframe + WLAN_HDR_A3_LEN));

	//set slot time
	pmlmeinfo->slotTime = (pmlmeinfo->capability & BIT(10))? 9: 20;

	//AID
	res = pmlmeinfo->aid = (int)(le16_to_cpu(*(unsigned short *)(pframe + WLAN_HDR_A3_LEN + 4))&0x3fff);

	//following are moved to join event callback function
	//to handle HT, WMM, rate adaptive, update MAC reg
	//for not to handle the synchronous IO in the tasklet
	for (i = (6 + WLAN_HDR_A3_LEN); i < pkt_len;)
	{
		pIE = (PNDIS_802_11_VARIABLE_IEs)(pframe + i);

		switch (pIE->ElementID)
		{
			case _VENDOR_SPECIFIC_IE_:
				if (_rtw_memcmp(pIE->data, WMM_PARA_OUI, 6))	//WMM
				{
					WMM_param_handler(padapter, pIE);
				}
				break;

			case _HT_CAPABILITY_IE_:	//HT caps
				HT_caps_handler(padapter, pIE);
				break;

			case _HT_EXTRA_INFO_IE_:	//HT info
				HT_info_handler(padapter, pIE);
				break;

			case _ERPINFO_IE_:
				ERP_IE_handler(padapter, pIE);

			default:
				break;
		}

		i += (pIE->Length + 2);
	}

	pmlmeinfo->state &= (~WIFI_FW_ASSOC_STATE);
	pmlmeinfo->state |= WIFI_FW_ASSOC_SUCCESS;

	//Update Basic Rate Table for spec, 2010-12-28 , by thomas
	UpdateBrateTbl(padapter, pmlmeinfo->network.SupportedRates);

report_assoc_result:

	report_join_res(padapter, res);

	return _SUCCESS;
}

unsigned int OnDeAuth(_adapter *padapter, union recv_frame *precv_frame)
{
	unsigned short	reason;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	u8 *pframe = precv_frame->u.hdr.rx_data;
	uint pktlen = precv_frame->u.hdr.len;

	//check A3
	if (!(_rtw_memcmp(GetAddr3Ptr(pframe), get_my_bssid(&pmlmeinfo->network), ETH_ALEN)))
		return _SUCCESS;

	reason = le16_to_cpu(*(unsigned short *)(pframe + WLAN_HDR_A3_LEN));

	DBG_871X("%s Reason code(%d)\n", __FUNCTION__,reason);

#ifdef CONFIG_AP_MODE
	if(check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE)
	{		
#if 0	
		_irqL irqL;
		struct sta_info *psta;
		struct sta_priv *pstapriv = &padapter->stapriv;
		
		psta = rtw_get_stainfo(pstapriv, GetAddr2Ptr(pframe));
		_enter_critical_bh(&(pstapriv->sta_hash_lock), &irqL);		
		rtw_free_stainfo(padapter, psta);
		_exit_critical_bh(&(pstapriv->sta_hash_lock), &irqL);		
#endif
		ap_free_sta(padapter, rtw_get_stainfo(&padapter->stapriv, GetAddr2Ptr(pframe)));

		return _SUCCESS;
	}
	else
#endif
	{
		receive_disconnect(padapter, GetAddr3Ptr(pframe) ,reason);
	}	
	pmlmepriv->LinkDetectInfo.bBusyTraffic = _FALSE;
	return _SUCCESS;

}

unsigned int OnDisassoc(_adapter *padapter, union recv_frame *precv_frame)
{
	unsigned short	reason;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	u8 *pframe = precv_frame->u.hdr.rx_data;
	uint pktlen = precv_frame->u.hdr.len;

	//check A3
	if (!(_rtw_memcmp(GetAddr3Ptr(pframe), get_my_bssid(&pmlmeinfo->network), ETH_ALEN)))
		return _SUCCESS;

	reason = le16_to_cpu(*(unsigned short *)(pframe + WLAN_HDR_A3_LEN));

        DBG_871X("%s Reason code(%d)\n", __FUNCTION__,reason);

#ifdef CONFIG_AP_MODE
	if(check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE)
	{	
#if 0	
		_irqL irqL;
		struct sta_info *psta;
		struct sta_priv *pstapriv = &padapter->stapriv;
		
		psta = rtw_get_stainfo(pstapriv, GetAddr2Ptr(pframe));
		_enter_critical_bh(&(pstapriv->sta_hash_lock), &irqL);	
		rtw_free_stainfo(padapter, psta);
		_exit_critical_bh(&(pstapriv->sta_hash_lock), &irqL);		
#endif

		ap_free_sta(padapter, rtw_get_stainfo(&padapter->stapriv, GetAddr2Ptr(pframe)));

		return _SUCCESS;
	}
	else
#endif
	{
		receive_disconnect(padapter, GetAddr3Ptr(pframe), reason);
	}	
	pmlmepriv->LinkDetectInfo.bBusyTraffic = _FALSE;
	return _SUCCESS;

}

unsigned int OnAtim(_adapter *padapter, union recv_frame *precv_frame)
{
	DBG_871X("%s\n", __FUNCTION__);
	return _SUCCESS;
}

unsigned int OnAction_qos(_adapter *padapter, union recv_frame *precv_frame)
{
	return _SUCCESS;
}

unsigned int OnAction_dls(_adapter *padapter, union recv_frame *precv_frame)
{
	return _SUCCESS;
}

unsigned int OnAction_back(_adapter *padapter, union recv_frame *precv_frame)
{
	u8 *addr;
	struct sta_info *psta=NULL;
	struct recv_reorder_ctrl *preorder_ctrl;
	unsigned char		*frame_body;
	unsigned char		category, action;
	unsigned short	tid, status, reason_code = 0;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	u8 *pframe = precv_frame->u.hdr.rx_data;
	struct sta_priv *pstapriv = &padapter->stapriv;
					
	uint len = precv_frame->u.hdr.len;

	//check RA matches or not	
	if (!_rtw_memcmp(myid(&(padapter->eeprompriv)), GetAddr1Ptr(pframe), ETH_ALEN))//for if1, sta/ap mode
		return _SUCCESS;

/*
	//check A1 matches or not
	if (!_rtw_memcmp(myid(&(padapter->eeprompriv)), get_da(pframe), ETH_ALEN))
		return _SUCCESS;
*/
	DBG_871X("%s\n", __FUNCTION__);

	if((pmlmeinfo->state&0x03) != WIFI_FW_AP_STATE)	
		if (!(pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS))
			return _SUCCESS;

	addr = GetAddr2Ptr(pframe);
	psta = rtw_get_stainfo(pstapriv, addr);

	if(psta==NULL)
		return _SUCCESS;

	frame_body = (unsigned char *)(pframe + sizeof(struct ieee80211_hdr_3addr));

	category = frame_body[0];
	if (category == WLAN_CATEGORY_BACK)// representing Block Ack
	{
		if (!pmlmeinfo->HT_enable)
		{
			return _SUCCESS;
		}

		action = frame_body[1];
		DBG_871X("%s, action=%d\n", __FUNCTION__, action);
		switch (action)
		{
			case WLAN_ACTION_ADDBA_REQ: //ADDBA request

				_rtw_memcpy(&(pmlmeinfo->ADDBA_req), &(frame_body[2]), sizeof(struct ADDBA_request));
				//process_addba_req(padapter, (u8*)&(pmlmeinfo->ADDBA_req), GetAddr3Ptr(pframe));
				process_addba_req(padapter, (u8*)&(pmlmeinfo->ADDBA_req), addr);
				
				if(pmlmeinfo->bAcceptAddbaReq == _TRUE)
				{
					issue_action_BA(padapter, addr, WLAN_ACTION_ADDBA_RESP, 0);
				}
				else
				{
					issue_action_BA(padapter, addr, WLAN_ACTION_ADDBA_RESP, 37);//reject ADDBA Req
				}
								
				break;

			case WLAN_ACTION_ADDBA_RESP: //ADDBA response

				//status = frame_body[3] | (frame_body[4] << 8); //endian issue
				status = RTW_GET_LE16(&frame_body[3]);
				tid = ((frame_body[5] >> 2) & 0x7);

				if (status == 0)
				{	//successful					
					DBG_871X("agg_enable for TID=%d\n", tid);
					psta->htpriv.agg_enable_bitmap |= 1 << tid;					
					psta->htpriv.candidate_tid_bitmap &= ~BIT(tid);				
				}
				else
				{					
					psta->htpriv.agg_enable_bitmap &= ~BIT(tid);					
				}

				//DBG_8192C("marc: ADDBA RSP: %x\n", pmlmeinfo->agg_enable_bitmap);
				break;

			case WLAN_ACTION_DELBA: //DELBA
				if ((frame_body[3] & BIT(3)) == 0)
				{
					psta->htpriv.agg_enable_bitmap &= ~(1 << ((frame_body[3] >> 4) & 0xf));
					psta->htpriv.candidate_tid_bitmap &= ~(1 << ((frame_body[3] >> 4) & 0xf));
					
					//reason_code = frame_body[4] | (frame_body[5] << 8);
					reason_code = RTW_GET_LE16(&frame_body[4]);
				}
				else if((frame_body[3] & BIT(3)) == BIT(3))
				{						
					tid = (frame_body[3] >> 4) & 0x0F;
				
					preorder_ctrl =  &psta->recvreorder_ctrl[tid];
					preorder_ctrl->enable = _FALSE;
					preorder_ctrl->indicate_seq = 0xffff;
					#ifdef DBG_RX_SEQ
					DBG_871X("DBG_RX_SEQ %s:%d indicate_seq:%u \n", __FUNCTION__, __LINE__,
						preorder_ctrl->indicate_seq);
					#endif
				}
				
				DBG_8192C("%s(): DELBA: %x(%x)\n", __FUNCTION__,pmlmeinfo->agg_enable_bitmap, reason_code);
				//todo: how to notify the host while receiving DELETE BA
				break;

			default:
				break;
		}
	}

	return _SUCCESS;
}

#ifdef CONFIG_P2P
void issue_p2p_GO_request(_adapter *padapter, u8* raddr)
{

	unsigned char category = WLAN_CATEGORY_PUBLIC;
	u8			action = P2P_PUB_ACTION_ACTION;
	u32			p2poui = cpu_to_be32(P2POUI);
	u8			oui_subtype = P2P_GO_NEGO_REQ;
	u8			wpsie[ 255 ] = { 0x00 }, p2pie[ 255 ] = { 0x00 };
	u8			wpsielen = 0, p2pielen = 0, i;
	u16			chnum = 0;
#ifdef CONFIG_WFD
	u32					wfdielen = 0;
#endif //CONFIG_WFD	
	
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	unsigned char					*pframe;
	struct ieee80211_hdr	*pwlanhdr;
	unsigned short				*fctrl;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wifidirect_info	*pwdinfo = &( padapter->wdinfo);


	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		return;
	}

	DBG_8192C( "[%s] In\n", __FUNCTION__ );
	//update attribute
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);

	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	_rtw_memcpy(pwlanhdr->addr1, raddr, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, myid(&(padapter->eeprompriv)), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, myid(&(padapter->eeprompriv)), ETH_ALEN);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_ACTION);

	pframe += sizeof(struct ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct ieee80211_hdr_3addr);

	pframe = rtw_set_fixed_ie(pframe, 1, &(category), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(action), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 4, (unsigned char *) &(p2poui), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(oui_subtype), &(pattrib->pktlen));	
	pwdinfo->negotiation_dialog_token = 1;	//	Initialize the dialog value
	pframe = rtw_set_fixed_ie(pframe, 1, &pwdinfo->negotiation_dialog_token, &(pattrib->pktlen));

	

	//	WPS Section
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

	//	Device Password ID
	//	Type:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_DEVICE_PWID );
	wpsielen += 2;

	//	Length:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( 0x0002 );
	wpsielen += 2;

	//	Value:

	if ( pwdinfo->ui_got_wps_info == P2P_GOT_WPSINFO_PEER_DISPLAY_PIN )
	{
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_DPID_USER_SPEC );
	}
	else if ( pwdinfo->ui_got_wps_info == P2P_GOT_WPSINFO_SELF_DISPLAY_PIN )
	{
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_DPID_REGISTRAR_SPEC );
	}
	else if ( pwdinfo->ui_got_wps_info == P2P_GOT_WPSINFO_PBC )
	{
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_DPID_PBC );
	}

	wpsielen += 2;

	pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, wpsielen, (unsigned char *) wpsie, &pattrib->pktlen );


	//	P2P IE Section.

	//	P2P OUI
	p2pielen = 0;
	p2pie[ p2pielen++ ] = 0x50;
	p2pie[ p2pielen++ ] = 0x6F;
	p2pie[ p2pielen++ ] = 0x9A;
	p2pie[ p2pielen++ ] = 0x09;	//	WFA P2P v1.0

	//	Commented by Albert 20110306
	//	According to the P2P Specification, the group negoitation request frame should contain 9 P2P attributes
	//	1. P2P Capability
	//	2. Group Owner Intent
	//	3. Configuration Timeout
	//	4. Listen Channel
	//	5. Extended Listen Timing
	//	6. Intended P2P Interface Address
	//	7. Channel List
	//	8. P2P Device Info
	//	9. Operating Channel


	//	P2P Capability
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_CAPABILITY;

	//	Length:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x0002 );
	p2pielen += 2;

	//	Value:
	//	Device Capability Bitmap, 1 byte
	//	Be able to participate in additional P2P Groups and
	//	support the P2P Invitation Procedure
	p2pie[ p2pielen++ ] = P2P_DEVCAP_INVITATION_PROC;
	
	//	Group Capability Bitmap, 1 byte
	p2pie[ p2pielen++ ] = 0x00;

	//	Group Owner Intent
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_GO_INTENT;

	//	Length:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x0001 );
	p2pielen += 2;

	//	Value:
	//	Todo the tie breaker bit.
	p2pie[ p2pielen++ ] = ( ( pwdinfo->intent << 1 ) | BIT(0) );

	//	Configuration Timeout
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_CONF_TIMEOUT;

	//	Length:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x0002 );
	p2pielen += 2;

	//	Value:
	p2pie[ p2pielen++ ] = 200;	//	2 seconds needed to be the P2P GO
	p2pie[ p2pielen++ ] = 200;	//	2 seconds needed to be the P2P Client


	//	Listen Channel
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_LISTEN_CH;

	//	Length:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x0005 );
	p2pielen += 2;

	//	Value:
	//	Country String
	p2pie[ p2pielen++ ] = 'U';
	p2pie[ p2pielen++ ] = 'S';
	
	//	The third byte should be set to 0x04.
	//	Described in the "Operating Channel Attribute" section.
	p2pie[ p2pielen++ ] = 0x04;

	//	Operating Class
	p2pie[ p2pielen++ ] = 0x51;	//	Copy from SD7
	
	//	Channel Number
	p2pie[ p2pielen++ ] = pwdinfo->listen_channel;	//	listening channel number
	

	//	Extended Listen Timing ATTR
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_EX_LISTEN_TIMING;

	//	Length:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x0004 );
	p2pielen += 2;

	//	Value:
	//	Availability Period
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0xFFFF );
	p2pielen += 2;

	//	Availability Interval
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0xFFFF );
	p2pielen += 2;


	//	Intended P2P Interface Address
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_INTENTED_IF_ADDR;

	//	Length:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( ETH_ALEN );
	p2pielen += 2;

	//	Value:
	_rtw_memcpy( p2pie + p2pielen, myid( &padapter->eeprompriv ), ETH_ALEN );
	p2pielen += ETH_ALEN;


	//	Channel List
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_CH_LIST;

	//	Length:
	chnum = ( u16 ) pmlmeext->max_chan_nums;
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 5 + chnum );
	p2pielen += 2;

	//	Value:
	//	Country String
	p2pie[ p2pielen++ ] = 'U';
	p2pie[ p2pielen++ ] = 'S';
	
	//	The third byte should be set to 0x04.
	//	Described in the "Operating Channel Attribute" section.
	p2pie[ p2pielen++ ] = 0x04;

	//	Channel Entry List
	//	Operating Class
	p2pie[ p2pielen++ ] = 0x51;	//	Copy from SD7

	//	Number of Channels
	//	Depends on the channel plan
	p2pie[ p2pielen++ ] = pmlmeext->max_chan_nums;	

	//	Channel List
	for( i = 0; i < pmlmeext->max_chan_nums; i++ )
	{
		p2pie[ p2pielen++ ] = pmlmeext->channel_set[ i ].ChannelNum;
	}

	//	Device Info
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_DEVICE_INFO;

	//	Length:
	//	21 -> P2P Device Address (6bytes) + Config Methods (2bytes) + Primary Device Type (8bytes) 
	//	+ NumofSecondDevType (1byte) + WPS Device Name ID field (2bytes) + WPS Device Name Len field (2bytes)
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 21 + pwdinfo->device_name_len );
	p2pielen += 2;

	//	Value:
	//	P2P Device Address
	_rtw_memcpy( p2pie + p2pielen, myid( &padapter->eeprompriv ), ETH_ALEN );
	p2pielen += ETH_ALEN;

	//	Config Method
	//	This field should be big endian. Noted by P2P specification.

	*(u16*) ( p2pie + p2pielen ) = cpu_to_be16( pwdinfo->supported_wps_cm );

	p2pielen += 2;

	//	Primary Device Type
	//	Category ID
	*(u16*) ( p2pie + p2pielen ) = cpu_to_be16( WPS_PDT_CID_RTK_WIDI );
	p2pielen += 2;

	//	OUI
	*(u32*) ( p2pie + p2pielen ) = cpu_to_be32( WPSOUI );
	p2pielen += 4;

	//	Sub Category ID
	*(u16*) ( p2pie + p2pielen ) = cpu_to_be16( WPS_PDT_SCID_RTK_DMP );
	p2pielen += 2;

	//	Number of Secondary Device Types
	p2pie[ p2pielen++ ] = 0x00;	//	No Secondary Device Type List

	//	Device Name
	//	Type:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_be16( WPS_ATTR_DEVICE_NAME );
	p2pielen += 2;

	//	Length:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_be16( pwdinfo->device_name_len );
	p2pielen += 2;

	//	Value:
	_rtw_memcpy( p2pie + p2pielen, pwdinfo->device_name , pwdinfo->device_name_len );
	p2pielen += pwdinfo->device_name_len;	
	

	//	Operating Channel
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_OPERATING_CH;

	//	Length:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x0005 );
	p2pielen += 2;

	//	Value:
	//	Country String
	p2pie[ p2pielen++ ] = 'U';
	p2pie[ p2pielen++ ] = 'S';
	
	//	The third byte should be set to 0x04.
	//	Described in the "Operating Channel Attribute" section.
	p2pie[ p2pielen++ ] = 0x04;

	//	Operating Class
	p2pie[ p2pielen++ ] = 0x51;	//	Copy from SD7
	
	//	Channel Number
	p2pie[ p2pielen++ ] = pwdinfo->operating_channel;	//	operating channel number

	pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, p2pielen, (unsigned char *) p2pie, &pattrib->pktlen );		

#ifdef CONFIG_WFD
	wfdielen = build_nego_req_wfd_ie(pwdinfo, pframe);
	pframe += wfdielen;
	pattrib->pktlen += wfdielen;
#endif //CONFIG_WFD

	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe(padapter, pmgntframe);

	return;

}


void issue_p2p_GO_response(_adapter *padapter, u8* raddr, u8* frame_body,uint len, u8 result)
{

	unsigned char category = WLAN_CATEGORY_PUBLIC;
	u8			action = P2P_PUB_ACTION_ACTION;
	u32			p2poui = cpu_to_be32(P2POUI);
	u8			oui_subtype = P2P_GO_NEGO_RESP;
	u8			wpsie[ 255 ] = { 0x00 }, p2pie[ 255 ] = { 0x00 };
	u8			p2pielen = 0, i;
	uint			wpsielen = 0;
	u16			wps_devicepassword_id = 0x0000;
	uint			wps_devicepassword_id_len = 0;
	u16			chnum = 0;
	
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	unsigned char					*pframe;
	struct ieee80211_hdr	*pwlanhdr;
	unsigned short				*fctrl;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wifidirect_info	*pwdinfo = &( padapter->wdinfo);


	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		return;
	}

	DBG_8192C( "[%s] In\n", __FUNCTION__ );
	//update attribute
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);

	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	_rtw_memcpy(pwlanhdr->addr1, raddr, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, myid(&(padapter->eeprompriv)), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, myid(&(padapter->eeprompriv)), ETH_ALEN);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_ACTION);

	pframe += sizeof(struct ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct ieee80211_hdr_3addr);

	pframe = rtw_set_fixed_ie(pframe, 1, &(category), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(action), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 4, (unsigned char *) &(p2poui), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(oui_subtype), &(pattrib->pktlen));	
	pwdinfo->negotiation_dialog_token = frame_body[7];	//	The Dialog Token of provisioning discovery request frame.
	pframe = rtw_set_fixed_ie(pframe, 1, &(pwdinfo->negotiation_dialog_token), &(pattrib->pktlen));

	//	Commented by Albert 20110328
	//	Try to get the device password ID from the WPS IE of group negotiation request frame
	//	WiFi Direct test plan 5.1.15
	rtw_get_wps_ie_p2p( frame_body + _PUBLIC_ACTION_IE_OFFSET_, len - _PUBLIC_ACTION_IE_OFFSET_, wpsie, &wpsielen);
	rtw_get_wps_attr_content( wpsie, wpsielen, WPS_ATTR_DEVICE_PWID, (u8*) &wps_devicepassword_id, &wps_devicepassword_id_len);
	wps_devicepassword_id = be16_to_cpu( wps_devicepassword_id );

	_rtw_memset( wpsie, 0x00, 255 );
	wpsielen = 0;

	//	WPS Section
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

	//	Device Password ID
	//	Type:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_DEVICE_PWID );
	wpsielen += 2;

	//	Length:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( 0x0002 );
	wpsielen += 2;

	//	Value:
	if ( wps_devicepassword_id == WPS_DPID_USER_SPEC )
	{
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_DPID_REGISTRAR_SPEC );
	}
	else if ( wps_devicepassword_id == WPS_DPID_REGISTRAR_SPEC )
	{
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_DPID_USER_SPEC );
	}
	else
	{
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_DPID_PBC );
	}
	wpsielen += 2;

	pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, wpsielen, (unsigned char *) wpsie, &pattrib->pktlen );


	//	P2P IE Section.

	//	P2P OUI
	p2pielen = 0;
	p2pie[ p2pielen++ ] = 0x50;
	p2pie[ p2pielen++ ] = 0x6F;
	p2pie[ p2pielen++ ] = 0x9A;
	p2pie[ p2pielen++ ] = 0x09;	//	WFA P2P v1.0

	//	Commented by Albert 20100908
	//	According to the P2P Specification, the group negoitation response frame should contain 9 P2P attributes
	//	1. Status
	//	2. P2P Capability
	//	3. Group Owner Intent
	//	4. Configuration Timeout
	//	5. Operating Channel
	//	6. Intended P2P Interface Address
	//	7. Channel List
	//	8. Device Info
	//	9. Group ID	( Only GO )


	//	ToDo:

	//	P2P Status
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_STATUS;

	//	Length:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x0001 );
	p2pielen += 2;

	//	Value:
	p2pie[ p2pielen++ ] = result;
	
	//	P2P Capability
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_CAPABILITY;

	//	Length:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x0002 );
	p2pielen += 2;

	//	Value:
	//	Device Capability Bitmap, 1 byte

	if ( pwdinfo->role == P2P_ROLE_CLIENT )
	{
		//	Commented by Albert 2011/03/08
		//	According to the P2P specification
		//	if the sending device will be client, the P2P Capability should be reserved of group negotation response frame
		p2pie[ p2pielen++ ] = 0;
	}
	else
	{
		//	Be group owner or meet the error case
		//	Be able to participate in additional P2P Groups and
		//	support the P2P Invitation Procedure	
		p2pie[ p2pielen++ ] = P2P_DEVCAP_INVITATION_PROC;
	}
	
	//	Group Capability Bitmap, 1 byte
	p2pie[ p2pielen++ ] = 0x00;

	//	Group Owner Intent
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_GO_INTENT;

	//	Length:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x0001 );
	p2pielen += 2;

	//	Value:
	if ( pwdinfo->peer_intent & 0x01 )
	{
		//	Peer's tie breaker bit is 1, our tie breaker bit should be 0
		p2pie[ p2pielen++ ] = ( pwdinfo->intent << 1 );
	}
	else
	{
		//	Peer's tie breaker bit is 0, our tie breaker bit should be 1
		p2pie[ p2pielen++ ] = ( ( pwdinfo->intent << 1 ) | BIT(0) );
	}


	//	Configuration Timeout
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_CONF_TIMEOUT;

	//	Length:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x0002 );
	p2pielen += 2;

	//	Value:
	p2pie[ p2pielen++ ] = 200;	//	2 seconds needed to be the P2P GO
	p2pie[ p2pielen++ ] = 200;	//	2 seconds needed to be the P2P Client

	//	Operating Channel
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_OPERATING_CH;

	//	Length:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x0005 );
	p2pielen += 2;

	//	Value:
	//	Country String
	p2pie[ p2pielen++ ] = 'U';
	p2pie[ p2pielen++ ] = 'S';
	
	//	The third byte should be set to 0x04.
	//	Described in the "Operating Channel Attribute" section.
	p2pie[ p2pielen++ ] = 0x04;

	//	Operating Class
	p2pie[ p2pielen++ ] = 0x51;	//	Copy from SD7
	
	//	Channel Number
	p2pie[ p2pielen++ ] = pwdinfo->operating_channel;	//	operating channel number

	//	Intended P2P Interface Address	
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_INTENTED_IF_ADDR;

	//	Length:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( ETH_ALEN );
	p2pielen += 2;

	//	Value:
	_rtw_memcpy( p2pie + p2pielen, myid( &padapter->eeprompriv ), ETH_ALEN );
	p2pielen += ETH_ALEN;

	//	Channel List
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_CH_LIST;

	//	Length:
	chnum = ( u16 ) pmlmeext->max_chan_nums;
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 5 + chnum );
	p2pielen += 2;

	//	Value:
	//	Country String
	p2pie[ p2pielen++ ] = 'U';
	p2pie[ p2pielen++ ] = 'S';
	
	//	The third byte should be set to 0x04.
	//	Described in the "Operating Channel Attribute" section.
	p2pie[ p2pielen++ ] = 0x04;

	//	Channel Entry List
	//	Operating Class
	p2pie[ p2pielen++ ] = 0x51;	//	Copy from SD7

	//	Number of Channels
	//	Depends on the channel plan
	p2pie[ p2pielen++ ] = pmlmeext->max_chan_nums;	

	//	Channel List
	for( i = 0; i < pmlmeext->max_chan_nums; i++ )
	{
		p2pie[ p2pielen++ ] = pmlmeext->channel_set[ i ].ChannelNum;
	}
	
	//	Device Info
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_DEVICE_INFO;

	//	Length:
	//	21 -> P2P Device Address (6bytes) + Config Methods (2bytes) + Primary Device Type (8bytes) 
	//	+ NumofSecondDevType (1byte) + WPS Device Name ID field (2bytes) + WPS Device Name Len field (2bytes)
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 21 + pwdinfo->device_name_len );
	p2pielen += 2;

	//	Value:
	//	P2P Device Address
	_rtw_memcpy( p2pie + p2pielen, myid( &padapter->eeprompriv ), ETH_ALEN );
	p2pielen += ETH_ALEN;

	//	Config Method
	//	This field should be big endian. Noted by P2P specification.

	*(u16*) ( p2pie + p2pielen ) = cpu_to_be16( pwdinfo->supported_wps_cm );

	p2pielen += 2;

	//	Primary Device Type
	//	Category ID
	*(u16*) ( p2pie + p2pielen ) = cpu_to_be16( WPS_PDT_CID_RTK_WIDI );
	p2pielen += 2;

	//	OUI
	*(u32*) ( p2pie + p2pielen ) = cpu_to_be32( WPSOUI );
	p2pielen += 4;

	//	Sub Category ID
	*(u16*) ( p2pie + p2pielen ) = cpu_to_be16( WPS_PDT_SCID_RTK_DMP );
	p2pielen += 2;

	//	Number of Secondary Device Types
	p2pie[ p2pielen++ ] = 0x00;	//	No Secondary Device Type List

	//	Device Name
	//	Type:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_be16( WPS_ATTR_DEVICE_NAME );
	p2pielen += 2;

	//	Length:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_be16( pwdinfo->device_name_len );
	p2pielen += 2;

	//	Value:
	_rtw_memcpy( p2pie + p2pielen, pwdinfo->device_name , pwdinfo->device_name_len );
	p2pielen += pwdinfo->device_name_len;	
	
	if ( pwdinfo->role == P2P_ROLE_GO )
	{
		//	Group ID Attribute
		//	Type:
		p2pie[ p2pielen++ ] = P2P_ATTR_GROUP_ID;

		//	Length:
		*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( ETH_ALEN + pwdinfo->nego_ssidlen );
		p2pielen += 2;

		//	Value:
		//	p2P Device Address
		_rtw_memcpy( p2pie + p2pielen , pwdinfo->device_addr, ETH_ALEN );
		p2pielen += ETH_ALEN;

		//	SSID
		_rtw_memcpy( p2pie + p2pielen, pwdinfo->nego_ssid, pwdinfo->nego_ssidlen );
		p2pielen += pwdinfo->nego_ssidlen;
		
	}
	
	pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, p2pielen, (unsigned char *) p2pie, &pattrib->pktlen );	
	

	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe(padapter, pmgntframe);

	return;

}

void issue_p2p_GO_confirm(_adapter *padapter, u8* raddr, u8 result)
{

	unsigned char category = WLAN_CATEGORY_PUBLIC;
	u8			action = P2P_PUB_ACTION_ACTION;
	u32			p2poui = cpu_to_be32(P2POUI);
	u8			oui_subtype = P2P_GO_NEGO_CONF;
	u8			wpsie[ 255 ] = { 0x00 }, p2pie[ 255 ] = { 0x00 };
	u8			wpsielen = 0, p2pielen = 0;
	
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	unsigned char					*pframe;
	struct ieee80211_hdr	*pwlanhdr;
	unsigned short				*fctrl;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wifidirect_info	*pwdinfo = &( padapter->wdinfo);


	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		return;
	}

	DBG_8192C( "[%s] In\n", __FUNCTION__ );
	//update attribute
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);

	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	_rtw_memcpy(pwlanhdr->addr1, raddr, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, myid(&(padapter->eeprompriv)), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, myid(&(padapter->eeprompriv)), ETH_ALEN);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_ACTION);

	pframe += sizeof(struct ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct ieee80211_hdr_3addr);

	pframe = rtw_set_fixed_ie(pframe, 1, &(category), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(action), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 4, (unsigned char *) &(p2poui), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(oui_subtype), &(pattrib->pktlen));	
	pframe = rtw_set_fixed_ie(pframe, 1, &(pwdinfo->negotiation_dialog_token), &(pattrib->pktlen));

	

	//	P2P IE Section.

	//	P2P OUI
	p2pielen = 0;
	p2pie[ p2pielen++ ] = 0x50;
	p2pie[ p2pielen++ ] = 0x6F;
	p2pie[ p2pielen++ ] = 0x9A;
	p2pie[ p2pielen++ ] = 0x09;	//	WFA P2P v1.0

	//	Commented by Albert 20110306
	//	According to the P2P Specification, the group negoitation request frame should contain 5 P2P attributes
	//	1. Status
	//	2. P2P Capability
	//	3. Operating Channel
	//	4. Channel List
	//	5. Group ID	( if this WiFi is GO )

	//	P2P Status
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_STATUS;

	//	Length:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x0001 );
	p2pielen += 2;

	//	Value:
	p2pie[ p2pielen++ ] = result;

	//	P2P Capability
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_CAPABILITY;

	//	Length:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x0002 );
	p2pielen += 2;

	//	Value:
	//	Device Capability Bitmap, 1 byte
	//	Be able to participate in additional P2P Groups and
	//	support the P2P Invitation Procedure
	p2pie[ p2pielen++ ] = P2P_DEVCAP_INVITATION_PROC;
	
	//	Group Capability Bitmap, 1 byte
	p2pie[ p2pielen++ ] = 0x00;


	//	Operating Channel
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_OPERATING_CH;

	//	Length:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x0005 );
	p2pielen += 2;

	//	Value:
	//	Country String
	p2pie[ p2pielen++ ] = 'U';
	p2pie[ p2pielen++ ] = 'S';
	
	//	The third byte should be set to 0x04.
	//	Described in the "Operating Channel Attribute" section.
	p2pie[ p2pielen++ ] = 0x04;

	//	Operating Class
	p2pie[ p2pielen++ ] = 0x51;	//	Copy from SD7

	if ( pwdinfo->role == P2P_ROLE_CLIENT )
	{
		p2pie[ p2pielen++ ] = pwdinfo->peer_operating_ch;
	}
	else
	{
		//	Channel Number
		p2pie[ p2pielen++ ] = pwdinfo->operating_channel;		//	Use the listen channel as the operating channel
	}


	//	Channel List
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_CH_LIST;

	//	Length:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 5 + pwdinfo->channel_cnt );
	p2pielen += 2;

	//	Value:
	//	Country String
	p2pie[ p2pielen++ ] = 'U';
	p2pie[ p2pielen++ ] = 'S';
	
	//	The third byte should be set to 0x04.
	//	Described in the "Operating Channel Attribute" section.
	p2pie[ p2pielen++ ] = 0x04;

	//	Channel Entry List
	//	Operating Class
	p2pie[ p2pielen++ ] = 0x51;	//	Copy from SD7

	//	Number of Channels
	p2pie[ p2pielen++ ] = pwdinfo->channel_cnt;

	//	Channel List
	_rtw_memcpy( p2pie + p2pielen, pwdinfo->channel_list, pwdinfo->channel_cnt );
	p2pielen += pwdinfo->channel_cnt;

	if ( pwdinfo->role == P2P_ROLE_GO )
	{
		//	Group ID Attribute
		//	Type:
		p2pie[ p2pielen++ ] = P2P_ATTR_GROUP_ID;

		//	Length:
		*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( ETH_ALEN + pwdinfo->nego_ssidlen );
		p2pielen += 2;

		//	Value:
		//	p2P Device Address
		_rtw_memcpy( p2pie + p2pielen , pwdinfo->device_addr, ETH_ALEN );
		p2pielen += ETH_ALEN;

		//	SSID
		_rtw_memcpy( p2pie + p2pielen, pwdinfo->nego_ssid, pwdinfo->nego_ssidlen );
		p2pielen += pwdinfo->nego_ssidlen;
	}
	
	pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, p2pielen, (unsigned char *) p2pie, &pattrib->pktlen );

	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe(padapter, pmgntframe);

	return;

}

void issue_p2p_invitation_request(_adapter *padapter, u8* raddr )
{

	unsigned char category = WLAN_CATEGORY_PUBLIC;
	u8			action = P2P_PUB_ACTION_ACTION;
	u32			p2poui = cpu_to_be32(P2POUI);
	u8			oui_subtype = P2P_INVIT_REQ;
	u8			p2pie[ 255 ] = { 0x00 };
	u8			p2pielen = 0;
	u8			dialogToken = 3;
	
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	unsigned char					*pframe;
	struct ieee80211_hdr	*pwlanhdr;
	unsigned short				*fctrl;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wifidirect_info	*pwdinfo = &( padapter->wdinfo);


	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		return;
	}

	//update attribute
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);

	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	_rtw_memcpy(pwlanhdr->addr1, raddr, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, myid(&(padapter->eeprompriv)), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, raddr,  ETH_ALEN);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_ACTION);

	pframe += sizeof(struct ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct ieee80211_hdr_3addr);

	pframe = rtw_set_fixed_ie(pframe, 1, &(category), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(action), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 4, (unsigned char *) &(p2poui), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(oui_subtype), &(pattrib->pktlen));	
	pframe = rtw_set_fixed_ie(pframe, 1, &(dialogToken), &(pattrib->pktlen));

	//	P2P IE Section.

	//	P2P OUI
	p2pielen = 0;
	p2pie[ p2pielen++ ] = 0x50;
	p2pie[ p2pielen++ ] = 0x6F;
	p2pie[ p2pielen++ ] = 0x9A;
	p2pie[ p2pielen++ ] = 0x09;	//	WFA P2P v1.0

	//	Commented by Albert 20101011
	//	According to the P2P Specification, the P2P Invitation request frame should contain 7 P2P attributes
	//	1. Configuration Timeout
	//	2. Invitation Flags
	//	3. Operating Channel	( Only GO )
	//	4. P2P Group BSSID	( Only GO )
	//	5. Channel List
	//	6. P2P Group ID
	//	7. P2P Device Info

	//	Configuration Timeout
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_CONF_TIMEOUT;

	//	Length:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x0002 );
	p2pielen += 2;

	//	Value:
	p2pie[ p2pielen++ ] = 200;	//	2 seconds needed to be the P2P GO
	p2pie[ p2pielen++ ] = 200;	//	2 seconds needed to be the P2P Client

	//	Invitation Flags
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_INVITATION_FLAGS;

	//	Length:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x0001 );
	p2pielen += 2;

	//	Value:
	p2pie[ p2pielen++ ] = P2P_INVITATION_FLAGS_PERSISTENT;


	//	Channel List
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_CH_LIST;

	//	Length:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x0010 );
	p2pielen += 2;

	//	Value:
	//	Country String
	p2pie[ p2pielen++ ] = 'U';
	p2pie[ p2pielen++ ] = 'S';
	
	//	The third byte should be set to 0x04.
	//	Described in the "Operating Channel Attribute" section.
	p2pie[ p2pielen++ ] = 0x04;

	//	Channel Entry List
	//	Operating Class
	p2pie[ p2pielen++ ] = 0x51;	//	Copy from SD7

	//	Number of Channels
	p2pie[ p2pielen++ ] = 0x0B;	//	support channel 1 - 11

	//	Channel List
	p2pie[ p2pielen++ ] = 0x01;
	p2pie[ p2pielen++ ] = 0x02;
	p2pie[ p2pielen++ ] = 0x03;
	p2pie[ p2pielen++ ] = 0x04;
	p2pie[ p2pielen++ ] = 0x05;
	p2pie[ p2pielen++ ] = 0x06;
	p2pie[ p2pielen++ ] = 0x07;
	p2pie[ p2pielen++ ] = 0x08;
	p2pie[ p2pielen++ ] = 0x09;
	p2pie[ p2pielen++ ] = 0x0A;
	p2pie[ p2pielen++ ] = 0x0B;	

	//	P2P Group ID
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_GROUP_ID;

	//	Length:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 6 + pwdinfo->invitereq_info.ssidlen );
	p2pielen += 2;

	//	Value:
	//	P2P Device Address for GO
	_rtw_memcpy( p2pie + p2pielen, raddr, ETH_ALEN );
	p2pielen += ETH_ALEN;

	//	SSID
	_rtw_memcpy( p2pie + p2pielen, pwdinfo->invitereq_info.ssid, pwdinfo->invitereq_info.ssidlen );
	p2pielen += pwdinfo->invitereq_info.ssidlen;
	

	//	Device Info
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_DEVICE_INFO;

	//	Length:
	//	21 -> P2P Device Address (6bytes) + Config Methods (2bytes) + Primary Device Type (8bytes) 
	//	+ NumofSecondDevType (1byte) + WPS Device Name ID field (2bytes) + WPS Device Name Len field (2bytes)
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 21 + pwdinfo->device_name_len );
	p2pielen += 2;
	
	//	Value:
	//	P2P Device Address
	_rtw_memcpy( p2pie + p2pielen, myid( &padapter->eeprompriv ), ETH_ALEN );
	p2pielen += ETH_ALEN;

	//	Config Method
	//	This field should be big endian. Noted by P2P specification.
	*(u16*) ( p2pie + p2pielen ) = cpu_to_be16( WPS_CONFIG_METHOD_DISPLAY );
	p2pielen += 2;

	//	Primary Device Type
	//	Category ID
	*(u16*) ( p2pie + p2pielen ) = cpu_to_be16( WPS_PDT_CID_RTK_WIDI );
	p2pielen += 2;

	//	OUI
	*(u32*) ( p2pie + p2pielen ) = cpu_to_be32( WPSOUI );
	p2pielen += 4;

	//	Sub Category ID
	*(u16*) ( p2pie + p2pielen ) = cpu_to_be16( WPS_PDT_SCID_RTK_DMP );
	p2pielen += 2;

	//	Number of Secondary Device Types
	p2pie[ p2pielen++ ] = 0x00;	//	No Secondary Device Type List

	//	Device Name
	//	Type:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_be16( WPS_ATTR_DEVICE_NAME );
	p2pielen += 2;

	//	Length:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_be16( pwdinfo->device_name_len );
	p2pielen += 2;

	//	Value:
	_rtw_memcpy( p2pie + p2pielen, pwdinfo->device_name, pwdinfo->device_name_len );
	p2pielen += pwdinfo->device_name_len;
		
	pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, p2pielen, (unsigned char *) p2pie, &pattrib->pktlen );	
	

	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe(padapter, pmgntframe);

	return;

}

void issue_p2p_invitation_response(_adapter *padapter, u8* raddr, u8 dialogToken, u8 success)
{

	unsigned char category = WLAN_CATEGORY_PUBLIC;
	u8			action = P2P_PUB_ACTION_ACTION;
	u32			p2poui = cpu_to_be32(P2POUI);
	u8			oui_subtype = P2P_INVIT_RESP;
	u8			p2pie[ 255 ] = { 0x00 };
	u8			p2pielen = 0;
	
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	unsigned char					*pframe;
	struct ieee80211_hdr	*pwlanhdr;
	unsigned short				*fctrl;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wifidirect_info	*pwdinfo = &( padapter->wdinfo);


	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		return;
	}

	//update attribute
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);

	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	_rtw_memcpy(pwlanhdr->addr1, raddr, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, myid(&(padapter->eeprompriv)), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, raddr,  ETH_ALEN);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_ACTION);

	pframe += sizeof(struct ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct ieee80211_hdr_3addr);

	pframe = rtw_set_fixed_ie(pframe, 1, &(category), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(action), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 4, (unsigned char *) &(p2poui), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(oui_subtype), &(pattrib->pktlen));	
	pframe = rtw_set_fixed_ie(pframe, 1, &(dialogToken), &(pattrib->pktlen));

	//	P2P IE Section.

	//	P2P OUI
	p2pielen = 0;
	p2pie[ p2pielen++ ] = 0x50;
	p2pie[ p2pielen++ ] = 0x6F;
	p2pie[ p2pielen++ ] = 0x9A;
	p2pie[ p2pielen++ ] = 0x09;	//	WFA P2P v1.0

	//	Commented by Albert 20101005
	//	According to the P2P Specification, the P2P Invitation response frame should contain 5 P2P attributes
	//	1. Status
	//	2. Configuration Timeout
	//	3. Operating Channel	( Only GO )
	//	4. P2P Group BSSID	( Only GO )
	//	5. Channel List

	//	P2P Status
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_STATUS;

	//	Length:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x0001 );
	p2pielen += 2;

	//	Value:
	if ( success )
	{
		p2pie[ p2pielen++ ] = P2P_STATUS_SUCCESS;
	}
	else
	{
		//	Sent the event receiving the P2P Invitation Req frame to DMP UI.
		//	DMP had to compare the MAC address to find out the profile.
		//	So, the WiFi driver will send the P2P_STATUS_FAIL_INFO_UNAVAILABLE to NB.
		//	If the UI found the corresponding profile, the WiFi driver sends the P2P Invitation Req
		//	to NB to rebuild the persistent group.
		p2pie[ p2pielen++ ] = P2P_STATUS_FAIL_INFO_UNAVAILABLE;
	}
	
	//	Configuration Timeout
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_CONF_TIMEOUT;

	//	Length:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x0002 );
	p2pielen += 2;

	//	Value:
	p2pie[ p2pielen++ ] = 200;	//	2 seconds needed to be the P2P GO
	p2pie[ p2pielen++ ] = 200;	//	2 seconds needed to be the P2P Client


	if ( success )
	{
		//	Channel List
		//	Type:
		p2pie[ p2pielen++ ] = P2P_ATTR_CH_LIST;

		//	Length:
		*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x0010 );
		p2pielen += 2;

		//	Value:
		//	Country String
		p2pie[ p2pielen++ ] = 'U';
		p2pie[ p2pielen++ ] = 'S';
	
		//	The third byte should be set to 0x04.
		//	Described in the "Operating Channel Attribute" section.
		p2pie[ p2pielen++ ] = 0x04;

		//	Channel Entry List
		//	Operating Class
		p2pie[ p2pielen++ ] = 0x51;	//	Copy from SD7

		//	Number of Channels
		p2pie[ p2pielen++ ] = 0x0B;	//	support channel 1 - 11

		//	Channel List
		p2pie[ p2pielen++ ] = 0x01;
		p2pie[ p2pielen++ ] = 0x02;
		p2pie[ p2pielen++ ] = 0x03;
		p2pie[ p2pielen++ ] = 0x04;
		p2pie[ p2pielen++ ] = 0x05;
		p2pie[ p2pielen++ ] = 0x06;
		p2pie[ p2pielen++ ] = 0x07;
		p2pie[ p2pielen++ ] = 0x08;
		p2pie[ p2pielen++ ] = 0x09;
		p2pie[ p2pielen++ ] = 0x0A;
		p2pie[ p2pielen++ ] = 0x0B;	
	}
		
	pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, p2pielen, (unsigned char *) p2pie, &pattrib->pktlen );	
	

	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe(padapter, pmgntframe);

	return;

}

void issue_p2p_provision_request(_adapter *padapter, u8* pinterface_raddr, u8* pssid, u8 ussidlen, u8* pdev_raddr )
{
	unsigned char category = WLAN_CATEGORY_PUBLIC;
	u8			action = P2P_PUB_ACTION_ACTION;
	u8			dialogToken = 1;
	u32			p2poui = cpu_to_be32(P2POUI);
	u8			oui_subtype = P2P_PROVISION_DISC_REQ;
	u8			wpsie[ 100 ] = { 0x00 };
	u8			wpsielen = 0;
	u32			p2pielen = 0;
	
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	unsigned char					*pframe;
	struct ieee80211_hdr	*pwlanhdr;
	unsigned short				*fctrl;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wifidirect_info	*pwdinfo = &(padapter->wdinfo);


	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		return;
	}

	DBG_8192C( "[%s] In\n", __FUNCTION__ );
	//update attribute
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);

	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	_rtw_memcpy(pwlanhdr->addr1, pinterface_raddr, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, myid(&(padapter->eeprompriv)), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, myid(&(padapter->eeprompriv)), ETH_ALEN);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_ACTION);

	pframe += sizeof(struct ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct ieee80211_hdr_3addr);

	pframe = rtw_set_fixed_ie(pframe, 1, &(category), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(action), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 4, (unsigned char *) &(p2poui), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(oui_subtype), &(pattrib->pktlen));	
	pframe = rtw_set_fixed_ie(pframe, 1, &(dialogToken), &(pattrib->pktlen));		

	p2pielen = build_prov_disc_request_p2p_ie( pwdinfo, pframe, pssid, ussidlen, pdev_raddr );

	pframe += p2pielen;
	pattrib->pktlen += p2pielen;

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

	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe(padapter, pmgntframe);

	return;

}

u8 is_matched_in_profilelist( u8* peermacaddr, struct profile_info* profileinfo )
{
	u8 i, match_result = 0;

	DBG_8192C( "[%s] peermac = %.2X %.2X %.2X %.2X %.2X %.2X\n", __FUNCTION__,
	  	    peermacaddr[0], peermacaddr[1],peermacaddr[2],peermacaddr[3],peermacaddr[4],peermacaddr[5]);
	
	for( i = 0; i < P2P_MAX_PERSISTENT_GROUP_NUM; i++, profileinfo++ )
	{
	       DBG_8192C( "[%s] profileinfo_mac = %.2X %.2X %.2X %.2X %.2X %.2X\n", __FUNCTION__,
		   	    profileinfo->peermac[0], profileinfo->peermac[1],profileinfo->peermac[2],profileinfo->peermac[3],profileinfo->peermac[4],profileinfo->peermac[5]);		   
		if ( _rtw_memcmp( peermacaddr, profileinfo->peermac, ETH_ALEN ) )
		{
			match_result = 1;
			DBG_8192C( "[%s] Match!\n", __FUNCTION__ );
			break;
		}
	}
	
	return (match_result );
}

void issue_probersp_p2p(_adapter *padapter, unsigned char *da)
{
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	unsigned char					*pframe;
	struct ieee80211_hdr	*pwlanhdr;
	unsigned short				*fctrl;	
	unsigned char					*mac;
	struct xmit_priv	*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	//WLAN_BSSID_EX 		*cur_network = &(pmlmeinfo->network);
	u16					beacon_interval = 100;
	u16					capInfo = 0;
	struct wifidirect_info	*pwdinfo = &(padapter->wdinfo);
	u8					wpsie[255] = { 0x00 };
	u32					wpsielen = 0, p2pielen = 0;
#ifdef CONFIG_WFD
	u32					wfdielen = 0;
#endif //CONFIG_WFD
	
	
	DBG_871X("%s\n", __FUNCTION__);
	
	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		return;
	}
	
	//update attribute
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);	
	
	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);
		
	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct ieee80211_hdr *)pframe;	
	
	mac = myid(&(padapter->eeprompriv));
	
	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;
	_rtw_memcpy(pwlanhdr->addr1, da, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, mac, ETH_ALEN);
	
	//	Use the device address for BSSID field.	
	_rtw_memcpy(pwlanhdr->addr3, mac, ETH_ALEN);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(fctrl, WIFI_PROBERSP);
	
	pattrib->hdrlen = sizeof(struct ieee80211_hdr_3addr);
	pattrib->pktlen = pattrib->hdrlen;
	pframe += pattrib->hdrlen;

	//timestamp will be inserted by hardware
	pframe += 8;
	pattrib->pktlen += 8;

	// beacon interval: 2 bytes
	_rtw_memcpy(pframe, (unsigned char *) &beacon_interval, 2); 
	pframe += 2;
	pattrib->pktlen += 2;

	//	capability info: 2 bytes
	//	ESS and IBSS bits must be 0 (defined in the 3.1.2.1.1 of WiFi Direct Spec)
	capInfo |= cap_ShortPremble;
	capInfo |= cap_ShortSlot;
	
	_rtw_memcpy(pframe, (unsigned char *) &capInfo, 2);
	pframe += 2;
	pattrib->pktlen += 2;


	// SSID
	pframe = rtw_set_ie(pframe, _SSID_IE_, 7, pwdinfo->p2p_wildcard_ssid, &pattrib->pktlen);

	// supported rates...
	//	Use the OFDM rate in the P2P probe response frame. ( 6(B), 9(B), 12, 18, 24, 36, 48, 54 )
	pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_, 8, pwdinfo->support_rate, &pattrib->pktlen);

	// DS parameter set
	pframe = rtw_set_ie(pframe, _DSSET_IE_, 1, (unsigned char *)&pwdinfo->listen_channel, &pattrib->pktlen);

	//	Todo: WPS IE
	//	Noted by Albert 20100907
	//	According to the WPS specification, all the WPS attribute is presented by Big Endian.

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

	//	WiFi Simple Config State
	//	Type:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_SIMPLE_CONF_STATE );
	wpsielen += 2;

	//	Length:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( 0x0001 );
	wpsielen += 2;

	//	Value:
	wpsie[wpsielen++] = WPS_WSC_STATE_NOT_CONFIG;	//	Not Configured.

	//	Response Type
	//	Type:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_RESP_TYPE );
	wpsielen += 2;

	//	Length:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( 0x0001 );
	wpsielen += 2;

	//	Value:
	wpsie[wpsielen++] = WPS_RESPONSE_TYPE_8021X;

	//	UUID-E
	//	Type:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_UUID_E );
	wpsielen += 2;

	//	Length:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( 0x0010 );
	wpsielen += 2;

	//	Value:
	_rtw_memcpy( wpsie + wpsielen, myid( &padapter->eeprompriv ), ETH_ALEN );
	wpsielen += 0x10;

	//	Manufacturer
	//	Type:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_MANUFACTURER );
	wpsielen += 2;

	//	Length:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( 0x0007 );
	wpsielen += 2;

	//	Value:
	_rtw_memcpy( wpsie + wpsielen, "Realtek", 7 );
	wpsielen += 7;

	//	Model Name
	//	Type:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_MODEL_NAME );
	wpsielen += 2;

	//	Length:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( 0x0006 );
	wpsielen += 2;	

	//	Value:
	_rtw_memcpy( wpsie + wpsielen, "8192CU", 6 );
	wpsielen += 6;

	//	Model Number
	//	Type:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_MODEL_NUMBER );
	wpsielen += 2;

	//	Length:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( 0x0001 );
	wpsielen += 2;

	//	Value:
	wpsie[ wpsielen++ ] = 0x31;		//	character 1

	//	Serial Number
	//	Type:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_SERIAL_NUMBER );
	wpsielen += 2;

	//	Length:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( ETH_ALEN );
	wpsielen += 2;

	//	Value:
	_rtw_memcpy( wpsie + wpsielen, "123456" , ETH_ALEN );
	wpsielen += ETH_ALEN;

	//	Primary Device Type
	//	Type:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_PRIMARY_DEV_TYPE );
	wpsielen += 2;

	//	Length:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( 0x0008 );
	wpsielen += 2;

	//	Value:
	//	Category ID
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_PDT_CID_RTK_WIDI );
	wpsielen += 2;

	//	OUI
	*(u32*) ( wpsie + wpsielen ) = cpu_to_be32( WPSOUI );
	wpsielen += 4;

	//	Sub Category ID
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_PDT_SCID_RTK_DMP );
	wpsielen += 2;

	//	Device Name
	//	Type:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_DEVICE_NAME );
	wpsielen += 2;

	//	Length:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( pwdinfo->device_name_len );
	wpsielen += 2;

	//	Value:
	_rtw_memcpy( wpsie + wpsielen, pwdinfo->device_name, pwdinfo->device_name_len );
	wpsielen += pwdinfo->device_name_len;

	//	Config Method
	//	Type:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_CONF_METHOD );
	wpsielen += 2;

	//	Length:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( 0x0002 );
	wpsielen += 2;

	//	Value:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( pwdinfo->supported_wps_cm );
	wpsielen += 2;
	

	pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, wpsielen, (unsigned char *) wpsie, &pattrib->pktlen );
	

	p2pielen = build_probe_resp_p2p_ie(pwdinfo, pframe);
	pframe += p2pielen;
	pattrib->pktlen += p2pielen;

#ifdef CONFIG_WFD
	wfdielen = build_probe_resp_wfd_ie(pwdinfo, pframe);
	pframe += wfdielen;
	pattrib->pktlen += wfdielen;
#endif //CONFIG_WFD

	pattrib->last_txcmdsz = pattrib->pktlen;
	

	dump_mgntframe(padapter, pmgntframe);
	
	return;

}

void issue_probereq_p2p(_adapter *padapter)
{
	struct xmit_frame		*pmgntframe;
	struct pkt_attrib		*pattrib;
	unsigned char			*pframe;
	struct ieee80211_hdr	*pwlanhdr;
	unsigned short		*fctrl;
	unsigned char			*mac;
	unsigned char			bssrate[NumRates];
	struct xmit_priv		*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	int	bssrate_len = 0;
	u8	bc_addr[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	struct wifidirect_info	*pwdinfo = &(padapter->wdinfo);	
	u8					wpsie[255] = { 0x00 }, p2pie[ 255 ] = { 0x00 };
	u16					wpsielen = 0, p2pielen = 0;
#ifdef CONFIG_WFD
	u32					wfdielen = 0;
#endif //CONFIG_WFD

	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		return;
	}

	//update attribute
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);


	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct ieee80211_hdr *)pframe;

	mac = myid(&(padapter->eeprompriv));

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	//	broadcast probe request frame
	_rtw_memcpy(pwlanhdr->addr1, bc_addr, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, bc_addr, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, mac, ETH_ALEN);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_PROBEREQ);

	pframe += sizeof (struct ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof (struct ieee80211_hdr_3addr);

	if ( P2P_STATE_TX_PROVISION_DIS_REQ == pwdinfo->p2p_state )
	{
		pframe = rtw_set_ie(pframe, _SSID_IE_, pwdinfo->tx_prov_disc_info.ssid.SsidLength, pwdinfo->tx_prov_disc_info.ssid.Ssid, &(pattrib->pktlen));
	}
	else
	{
		pframe = rtw_set_ie(pframe, _SSID_IE_, P2P_WILDCARD_SSID_LEN, pwdinfo->p2p_wildcard_ssid, &(pattrib->pktlen));
	}	
	//	Use the OFDM rate in the P2P probe request frame. ( 6(B), 9(B), 12(B), 24(B), 36, 48, 54 )
	pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_, 8, pwdinfo->support_rate, &pattrib->pktlen);

	//	WPS IE
	//	Noted by Albert 20110221
	//	According to the WPS specification, all the WPS attribute is presented by Big Endian.

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

	//	Device Name
	//	Type:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_DEVICE_NAME );
	wpsielen += 2;

	//	Length:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( pwdinfo->device_name_len );
	wpsielen += 2;

	//	Value:
	_rtw_memcpy( wpsie + wpsielen, pwdinfo->device_name, pwdinfo->device_name_len );
	wpsielen += pwdinfo->device_name_len;

	//	Primary Device Type
	//	Type:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_PRIMARY_DEV_TYPE );
	wpsielen += 2;

	//	Length:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( 0x0008 );
	wpsielen += 2;

	//	Value:
	//	Category ID
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_PDT_CID_RTK_WIDI );
	wpsielen += 2;

	//	OUI
	*(u32*) ( wpsie + wpsielen ) = cpu_to_be32( WPSOUI );
	wpsielen += 4;

	//	Sub Category ID
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_PDT_SCID_RTK_DMP );
	wpsielen += 2;	

	pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, wpsielen, (unsigned char *) wpsie, &pattrib->pktlen );
	
	//	P2P OUI
	p2pielen = 0;
	p2pie[ p2pielen++ ] = 0x50;
	p2pie[ p2pielen++ ] = 0x6F;
	p2pie[ p2pielen++ ] = 0x9A;
	p2pie[ p2pielen++ ] = 0x09;	//	WFA P2P v1.0

	//	Commented by Albert 20110221
	//	According to the P2P Specification, the probe request frame should contain 5 P2P attributes
	//	1. P2P Capability
	//	2. P2P Device ID if this probe request wants to find the specific P2P device
	//	3. Listen Channel
	//	4. Extended Listen Timing
	//	5. Operating Channel if this WiFi is working as the group owner now

	//	P2P Capability
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_CAPABILITY;

	//	Length:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x0002 );
	p2pielen += 2;

	//	Value:
	//	Device Capability Bitmap, 1 byte
	//	Be able to participate in additional P2P Groups and
	//	support the P2P Invitation Procedure
	p2pie[ p2pielen++ ] = P2P_DEVCAP_INVITATION_PROC;
	
	//	Group Capability Bitmap, 1 byte
	p2pie[ p2pielen++ ] = 0x00;

	//	Listen Channel
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_LISTEN_CH;

	//	Length:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x0005 );
	p2pielen += 2;

	//	Value:
	//	Country String
	p2pie[ p2pielen++ ] = 'U';
	p2pie[ p2pielen++ ] = 'S';
	
	//	The third byte should be set to 0x04.
	//	Described in the "Operating Channel Attribute" section.
	p2pie[ p2pielen++ ] = 0x04;

	//	Operating Class
	p2pie[ p2pielen++ ] = 0x51;	//	Copy from SD7
	
	//	Channel Number
	p2pie[ p2pielen++ ] = pwdinfo->listen_channel;	//	listen channel
	

	//	Extended Listen Timing
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_EX_LISTEN_TIMING;

	//	Length:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x0004 );
	p2pielen += 2;

	//	Value:
	//	Availability Period
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0xFFFF );
	p2pielen += 2;

	//	Availability Interval
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0xFFFF );
	p2pielen += 2;

	if ( pwdinfo->role == P2P_ROLE_GO )
	{
		//	Operating Channel (if this WiFi is working as the group owner now)
		//	Type:
		p2pie[ p2pielen++ ] = P2P_ATTR_OPERATING_CH;

		//	Length:
		*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x0005 );
		p2pielen += 2;

		//	Value:
		//	Country String
		p2pie[ p2pielen++ ] = 'U';
		p2pie[ p2pielen++ ] = 'S';
	
		//	The third byte should be set to 0x04.
		//	Described in the "Operating Channel Attribute" section.
		p2pie[ p2pielen++ ] = 0x04;

		//	Operating Class
		p2pie[ p2pielen++ ] = 0x51;	//	Copy from SD7
	
		//	Channel Number
		p2pie[ p2pielen++ ] = pwdinfo->operating_channel;	//	operating channel number
		
	}
	
	pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, p2pielen, (unsigned char *) p2pie, &pattrib->pktlen );

#ifdef CONFIG_WFD
	wfdielen = build_probe_req_wfd_ie(pwdinfo, pframe);
	pframe += wfdielen;
	pattrib->pktlen += wfdielen;
#endif //CONFIG_WFD	

	pattrib->last_txcmdsz = pattrib->pktlen;

	RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("issuing probe_req, tx_len=%d\n", pattrib->last_txcmdsz));

	dump_mgntframe(padapter, pmgntframe);

	return;
}

#endif //CONFIG_P2P

unsigned int OnAction_public(_adapter *padapter, union recv_frame *precv_frame)
{
	unsigned char		*frame_body;
	unsigned char		category, action;
	u8 *pframe = precv_frame->u.hdr.rx_data;
	uint len = precv_frame->u.hdr.len;
#ifdef CONFIG_P2P
	u8	p2p_ie[ 255 ];
	u32	p2p_ielen, wps_ielen;
	struct	wifidirect_info	*pwdinfo = &( padapter->wdinfo );
	u8	result = P2P_STATUS_SUCCESS;	
#endif //CONFIG_P2P

	//check RA matches or not
	if (!_rtw_memcmp(myid(&(padapter->eeprompriv)), GetAddr1Ptr(pframe), ETH_ALEN))//for if1, sta/ap mode
		return _SUCCESS;

	frame_body = (unsigned char *)(pframe + sizeof(struct ieee80211_hdr_3addr));

	category = frame_body[0];
	if(category != WLAN_CATEGORY_PUBLIC)
		return _SUCCESS;

	action = frame_body[ 1 ];
	if ( action == ACT_PUBLIC_P2P )	//	IEEE 802.11 P2P Public Action usage.
	{		
#ifdef CONFIG_P2P
		//	Do nothing if the driver doesn't enable the P2P function.
		if ( ( pwdinfo->p2p_state == P2P_STATE_NONE ) || ( pwdinfo->p2p_state == P2P_STATE_IDLE ) )
			return _SUCCESS;

		//	Commented by Albert 20100908
		//	Low byte -> High byte is 0x50, 0x6F, 0x9A, 0x09 for P2P OUI.
		//	But the P2POUT is defined as 0x506F9A09 -> should use the cpu_to_be32
		if ( cpu_to_be32( *( ( u32* ) ( frame_body + 2 ) ) ) == P2POUI )
		{
			_rtw_memset( p2p_ie, 0x00, 255 );
			p2p_ielen = 0;
			
			switch( frame_body[ 6 ] )//OUI Subtype
			{
				case P2P_GO_NEGO_REQ:
				{
					DBG_8192C( "[%s] Got GO Nego Req Frame\n", __FUNCTION__);
					
					if ( pwdinfo->p2p_state == P2P_STATE_GONEGO_FAIL )
					{
						//	Commented by Albert 20110526
						//	In this case, this means the previous nego fail doesn't be reset yet.
						_cancel_timer_ex( &pwdinfo->restore_p2p_state_timer );
						//	Restore the previous p2p state
						pwdinfo->p2p_state = pwdinfo->pre_p2p_state;
						printk( "[%s] Restore the previous p2p state to %d\n", __FUNCTION__, pwdinfo->p2p_state );						
					}					

					//20110902 Kurt
					//Add if statement to avoid receiving duplicate prov disc req. such that pre_p2p_state would be covered.
					if(pwdinfo->p2p_state != P2P_STATE_GONEGO_ING)										
						pwdinfo->pre_p2p_state = pwdinfo->p2p_state;

					result = process_p2p_group_negotation_req( pwdinfo, frame_body, len );
					issue_p2p_GO_response( padapter, GetAddr2Ptr(pframe), frame_body, len, result );
					//	Commented by Albert 20110718
					//	No matter negotiating or negotiation failure, the driver should set up the restore P2P state timer.
					_set_timer( &pwdinfo->restore_p2p_state_timer, 5000 );
					break;					
				}
				case P2P_GO_NEGO_RESP:
				{
					DBG_8192C( "[%s] Got GO Nego Resp Frame\n", __FUNCTION__);

					if ( pwdinfo->p2p_state == P2P_STATE_GONEGO_ING )
					{
						//	Commented by Albert 20110425
						//	The restore timer is enabled when issuing the nego request frame of rtw_p2p_connect function.
						_cancel_timer_ex( &pwdinfo->restore_p2p_state_timer );
						result = process_p2p_group_negotation_resp( pwdinfo, frame_body, len);

						issue_p2p_GO_confirm( pwdinfo->padapter, GetAddr2Ptr(pframe), result);

						//	Reset the dialog token for group negotiation frames.
						pwdinfo->negotiation_dialog_token = 1;

						if( pwdinfo->p2p_state == P2P_STATE_GONEGO_FAIL )
						{
							_set_timer( &pwdinfo->restore_p2p_state_timer, 5000 );
						}
					}
					else
					{
						DBG_8192C( "[%s] Skipped GO Nego Resp Frame (p2p_state != P2P_STATE_GONEGO_ING)\n", __FUNCTION__);
					}
					
					break;
				}
				case P2P_GO_NEGO_CONF:
				{
					DBG_8192C( "[%s] Got GO Nego Confirm Frame\n", __FUNCTION__);
					process_p2p_group_negotation_confirm( pwdinfo, frame_body, len);
					break;
				}
				case P2P_INVIT_REQ:
				{
					//	Added by Albert 2010/10/05
					//	Received the P2P Invite Request frame.
					
					DBG_8192C( "[%s] Got invite request frame!\n", __FUNCTION__ );
					if ( rtw_get_p2p_ie( frame_body + _PUBLIC_ACTION_IE_OFFSET_, len - _PUBLIC_ACTION_IE_OFFSET_, p2p_ie, &p2p_ielen ) )
					{
						//	Parse the necessary information from the P2P Invitation Request frame.
						//	For example: The MAC address of sending this P2P Invitation Request frame.
						u8	groupid[ 38 ] = { 0x00 };
						u32	attr_contentlen = 0;
						u8	match_result = 0;						

						rtw_get_p2p_attr_content( p2p_ie, p2p_ielen, P2P_ATTR_GROUP_ID, groupid, &attr_contentlen);
						_rtw_memcpy( pwdinfo->p2p_peer_interface_addr, groupid, ETH_ALEN );
						pwdinfo->p2p_state = P2P_STATE_RECV_INVITE_REQ;
						DBG_8192C( "[%s] peer address %.2X %.2X %.2X %.2X %.2X %.2X\n", __FUNCTION__,
								groupid[0], groupid[1], groupid[2], groupid[3], groupid[4], groupid[5] );

						if ( is_matched_in_profilelist( pwdinfo->p2p_peer_interface_addr, &pwdinfo->profileinfo[ 0 ] ) )
						{
							match_result = 1;
						}
						else
						{
							match_result = 0;
						}

						DBG_8192C( "[%s] match_result = %d\n", __FUNCTION__, match_result );
						
						pwdinfo->inviteresp_info.token = frame_body[ 7 ];
						issue_p2p_invitation_response( padapter, pwdinfo->p2p_peer_interface_addr, pwdinfo->inviteresp_info.token, match_result );
					}

					break;
				}
				case P2P_INVIT_RESP:
				{
					u8	attr_content = 0x00;
					u32	attr_contentlen = 0;
					
					DBG_8192C( "[%s] Got invite response frame!\n", __FUNCTION__ );
					if ( rtw_get_p2p_ie( frame_body + _PUBLIC_ACTION_IE_OFFSET_, len - _PUBLIC_ACTION_IE_OFFSET_, p2p_ie, &p2p_ielen ) )
					{
						rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_STATUS, &attr_content, &attr_contentlen);
										
						if ( attr_contentlen == 1 )
						{
							DBG_8192C( "[%s] Status = %d\n", __FUNCTION__, attr_content );
							if ( attr_content == P2P_STATUS_SUCCESS )
							{
								pwdinfo->role = P2P_ROLE_CLIENT;
							}
							else
							{
								pwdinfo->role = P2P_ROLE_DEVICE;
							}
						}
						else
						{
								pwdinfo->role = P2P_ROLE_DEVICE;
						}
					}
					break;
				}
				case P2P_DEVDISC_REQ:

					process_p2p_devdisc_req(pwdinfo, pframe, len);

					break;

				case P2P_DEVDISC_RESP:

					process_p2p_devdisc_resp(pwdinfo, pframe, len);

					break;

				case P2P_PROVISION_DISC_REQ:
					DBG_8192C( "[%s] Got Provisioning Discovery Request Frame\n", __FUNCTION__ );
					process_p2p_provdisc_req(pwdinfo, pframe, len);
					_rtw_memcpy(pwdinfo->rx_prov_disc_info.peerDevAddr, GetAddr2Ptr(pframe), ETH_ALEN);

					//20110902 Kurt
					//Add if statement to avoid receiving duplicate prov disc req. such that pre_p2p_state would be covered.
					if(pwdinfo->p2p_state != P2P_STATE_RX_PROVISION_DIS_REQ)
						pwdinfo->pre_p2p_state = pwdinfo->p2p_state;
					pwdinfo->p2p_state = P2P_STATE_RX_PROVISION_DIS_REQ;
					_set_timer( &pwdinfo->restore_p2p_state_timer, P2P_PROVISION_TIMEOUT );
					break;

				case P2P_PROVISION_DISC_RESP:
					//	Commented by Albert 20110707
					//	Should we check the pwdinfo->tx_prov_disc_info.bsent flag here??
					DBG_8192C( "[%s] Got Provisioning Discovery Response Frame\n", __FUNCTION__ );
					//	Commented by Albert 20110426
					//	The restore timer is enabled when issuing the provisioing request frame in rtw_p2p_prov_disc function.
					_cancel_timer_ex( &pwdinfo->restore_p2p_state_timer );
					pwdinfo->p2p_state = P2P_STATE_RX_PROVISION_DIS_RSP;
					process_p2p_provdisc_resp(pwdinfo, pframe);
					_set_timer( &pwdinfo->restore_p2p_state_timer, P2P_PROVISION_TIMEOUT );
					break;

			}

		}		
#endif //CONFIG_P2P
	}
	
	return _SUCCESS;
}

unsigned int OnAction_ht(_adapter *padapter, union recv_frame *precv_frame)
{
	return _SUCCESS;
}

unsigned int OnAction_wmm(_adapter *padapter, union recv_frame *precv_frame)
{
	return _SUCCESS;
}

unsigned int OnAction_p2p(_adapter *padapter, union recv_frame *precv_frame)
{
#ifdef CONFIG_P2P
	u8 *frame_body;
	u8 category, OUI_Subtype, dialogToken=0;
	u8 *pframe = precv_frame->u.hdr.rx_data;
	uint len = precv_frame->u.hdr.len;
	struct	wifidirect_info	*pwdinfo = &( padapter->wdinfo );
	

	DBG_871X("%s\n", __FUNCTION__);
	
	//check RA matches or not
	if (!_rtw_memcmp(myid(&(padapter->eeprompriv)), GetAddr1Ptr(pframe), ETH_ALEN))//for if1, sta/ap mode
		return _SUCCESS;

	frame_body = (unsigned char *)(pframe + sizeof(struct ieee80211_hdr_3addr));

	category = frame_body[0];
	if(category != WLAN_CATEGORY_P2P)
		return _SUCCESS;

	if ( cpu_to_be32( *( ( u32* ) ( frame_body + 1 ) ) ) != P2POUI )
		return _SUCCESS;

	OUI_Subtype = frame_body[5];
	dialogToken = frame_body[6];

	switch(OUI_Subtype)
	{
		case P2P_NOTICE_OF_ABSENCE:
			
			break;
			
		case P2P_PRESENCE_REQUEST:

			process_p2p_presence_req(pwdinfo, pframe, len);			
			
			break;
			
		case P2P_PRESENCE_RESPONSE:
			
			break;
			
		case P2P_GO_DISC_REQUEST:
			
			break;
			
		default:
			break;
			
	}	

#endif //CONFIG_P2P

	return _SUCCESS;

}

unsigned int OnAction(_adapter *padapter, union recv_frame *precv_frame)
{
	int i;
	unsigned char	category;
	struct action_handler *ptable;
	unsigned char	*frame_body;
	u8 *pframe = precv_frame->u.hdr.rx_data; 

	frame_body = (unsigned char *)(pframe + sizeof(struct ieee80211_hdr_3addr));
	
	category = frame_body[0];
	
	for(i = 0; i < sizeof(OnAction_tbl)/sizeof(struct action_handler); i++)	
	{
		ptable = &OnAction_tbl[i];
		
		if(category == ptable->num)
			ptable->func(padapter, precv_frame);
	
	}

	return _SUCCESS;

}

unsigned int DoReserved(_adapter *padapter, union recv_frame *precv_frame)
{
	u8 *pframe = precv_frame->u.hdr.rx_data;
	uint len = precv_frame->u.hdr.len;

	//DBG_871X("rcvd mgt frame(%x, %x)\n", (GetFrameSubType(pframe) >> 4), *(unsigned int *)GetAddr1Ptr(pframe));
	return _SUCCESS;
}

struct xmit_frame *alloc_mgtxmitframe(struct xmit_priv *pxmitpriv)
{
	struct xmit_frame			*pmgntframe;
	struct xmit_buf				*pxmitbuf;

	if ((pmgntframe = rtw_alloc_xmitframe(pxmitpriv)) == NULL)
	{
		return NULL;
	}

	if ((pxmitbuf = rtw_alloc_xmitbuf_ext(pxmitpriv)) == NULL)
	{
		rtw_free_xmitframe_ex(pxmitpriv, pmgntframe);
		return NULL;
	}

	pmgntframe->frame_tag = MGNT_FRAMETAG;

	pmgntframe->pxmitbuf = pxmitbuf;

	pmgntframe->buf_addr = pxmitbuf->pbuf;

	pxmitbuf->priv_data = pmgntframe;

	return pmgntframe;

}


/****************************************************************************

Following are some TX fuctions for WiFi MLME

*****************************************************************************/

void update_mgntframe_attrib(_adapter *padapter, struct pkt_attrib *pattrib)
{
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);

	_rtw_memset((u8 *)(pattrib), 0, sizeof(struct pkt_attrib));

	pattrib->hdrlen = 24;
	pattrib->nr_frags = 1;
	pattrib->priority = 7;
	pattrib->mac_id = 0;
	pattrib->qsel = 0x12;

	pattrib->pktlen = 0;

	if(pmlmeext->cur_wireless_mode & WIRELESS_11B)
		pattrib->raid = 6;//b mode
	else
		pattrib->raid = 5;//a/g mode

	pattrib->encrypt = _NO_PRIVACY_;
	pattrib->bswenc = _FALSE;	

	pattrib->qos_en = _FALSE;
	pattrib->ht_en = _FALSE;
	pattrib->bwmode = HT_CHANNEL_WIDTH_20;
	pattrib->ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
	pattrib->sgi = _FALSE;

	pattrib->seqnum = pmlmeext->mgnt_seq;

}

void dump_mgntframe(_adapter *padapter, struct xmit_frame *pmgntframe)
{
	if(padapter->bSurpriseRemoved == _TRUE ||
		padapter->bDriverStopped == _TRUE)
		return;
	
	padapter->HalFunc.mgnt_xmit(padapter, pmgntframe);
}

//Commented by Kurt
#ifdef CONFIG_TDLS
void issue_nulldata_to_TDLS_peer_STA(_adapter *padapter, struct sta_info *ptdls_sta, unsigned int power_mode)
{
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	unsigned char					*pframe;
	struct ieee80211_hdr	*pwlanhdr;
	unsigned short				*fctrl;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		return;
	}

	//update attribute
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);

	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;
//	SetToDs(fctrl);
	if (power_mode)
	{
		SetPwrMgt(fctrl);
	}

	_rtw_memcpy(pwlanhdr->addr1, ptdls_sta->hwaddr, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, myid(&(padapter->eeprompriv)), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);

	ptdls_sta->sta_xmitpriv.txseq_tid[pattrib->priority]++;
	ptdls_sta->sta_xmitpriv.txseq_tid[pattrib->priority] &= 0xFFF;
	pattrib->seqnum = ptdls_sta->sta_xmitpriv.txseq_tid[pattrib->priority];
	SetSeqNum(pwlanhdr, pattrib->seqnum);

	SetFrameSubType(pframe, WIFI_DATA_NULL);

	pframe += sizeof(struct ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct ieee80211_hdr_3addr);

	pattrib->last_txcmdsz = pattrib->pktlen;
	dump_mgntframe(padapter, pmgntframe);

	return;
}

s32 update_tdls_attrib(_adapter *padapter, struct pkt_attrib *pattrib)
{

	struct sta_info *psta = NULL;
	struct sta_priv		*pstapriv = &padapter->stapriv;
	struct security_priv	*psecuritypriv = &padapter->securitypriv;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct qos_priv		*pqospriv= &pmlmepriv->qospriv;

	s32 res=_SUCCESS;
	sint bmcast;

	bmcast = IS_MCAST(pattrib->ra);
	
	psta = rtw_get_stainfo(pstapriv, pattrib->ra);
	if (psta == NULL)	{ 
		res =_FAIL;
		goto exit;
	}

	pattrib->mac_id = psta->mac_id;
				
	pattrib->psta = psta;

	pattrib->ack_policy = 0;
	// get ether_hdr_len
	pattrib->pkt_hdrlen = ETH_HLEN;//(pattrib->ether_type == 0x8100) ? (14 + 4 ): 14; //vlan tag

	if (pqospriv->qos_option &&  psta->qos_option) {
		if(pattrib->priority==0)
			pattrib->priority = 1;	//tdls management frame should be AC_BK
		pattrib->hdrlen = WLAN_HDR_A3_QOS_LEN;
		pattrib->subtype = WIFI_QOS_DATA_TYPE;
	} else {
		pattrib->hdrlen = WLAN_HDR_A3_LEN;
		pattrib->subtype = WIFI_DATA_TYPE;	
		pattrib->priority = 0;
	}

	if (psta->ieee8021x_blocked == _TRUE)
	{
		pattrib->encrypt = 0;
	}
	else
	{
		GET_ENCRY_ALGO(psecuritypriv, psta, pattrib->encrypt, bmcast);

		switch(psecuritypriv->dot11AuthAlgrthm)
		{
			case dot11AuthAlgrthm_Open:
			case dot11AuthAlgrthm_Shared:
			case dot11AuthAlgrthm_Auto:				
				pattrib->key_idx = (u8)psecuritypriv->dot11PrivacyKeyIndex;
				break;
			case dot11AuthAlgrthm_8021X:
				pattrib->key_idx = 0;
				break;
			default:
				pattrib->key_idx = 0;
				break;
		}
	}

	switch (pattrib->encrypt)
	{
		case _WEP40_:
		case _WEP104_:
			pattrib->iv_len = 4;
			pattrib->icv_len = 4;
			break;
		case _TKIP_:
			pattrib->iv_len = 8;
			pattrib->icv_len = 4;
			if(padapter->securitypriv.busetkipkey==_FAIL)
			{
				res =_FAIL;
				goto exit;
			}
			break;			
		case _AES_:
			pattrib->iv_len = 8;
			pattrib->icv_len = 8;
			break;
		default:
			pattrib->iv_len = 0;
			pattrib->icv_len = 0;
			break;
	}

	if (pattrib->encrypt &&
	    ((padapter->securitypriv.sw_encrypt == _TRUE) || (psecuritypriv->hw_decrypted == _FALSE)))
	{
		pattrib->bswenc = _TRUE;
	} else {
		pattrib->bswenc = _FALSE;
	}

	//qos_en, ht_en, init rate, ,bw, ch_offset, sgi
	pattrib->qos_en = psta->qos_option;
	pattrib->ht_en = psta->htpriv.ht_option;
	pattrib->raid = psta->raid;
	pattrib->bwmode = psta->htpriv.bwmode;
	pattrib->ch_offset = psta->htpriv.ch_offset;
	pattrib->sgi= psta->htpriv.sgi;
	pattrib->ampdu_en = _FALSE;
	
	if(pattrib->ht_en && psta->htpriv.ampdu_enable)
	{
		if(psta->htpriv.agg_enable_bitmap & BIT(pattrib->priority))
			pattrib->ampdu_en = _TRUE;
	}	

exit:

	return res;
}

void issue_tdls_setup_req(_adapter *padapter, u8 *mac_addr)
{
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
   	struct sta_priv *pstapriv = &padapter->stapriv;
	struct sta_info *ptdls_sta= NULL;
	_irqL irqL;
	static u8 dialogtoken = 0;
	u32 timeout_interval= TPK_RESEND_COUNT * 1000;	//retry timer should set at least 301 sec, using TPK_count counting 301 times.

	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		return;
	}
	
	//update attribute
	pattrib = &pmgntframe->attrib;

	pmgntframe->frame_tag = DATA_FRAMETAG;
	pattrib->ether_type = 0x890d;
	pattrib->pctrl =0;		

	_rtw_memcpy(pattrib->dst, mac_addr, ETH_ALEN);
	_rtw_memcpy(pattrib->src, myid(&(padapter->eeprompriv)), ETH_ALEN);

	_rtw_memcpy(pattrib->ra, get_bssid(pmlmepriv), ETH_ALEN);
	_rtw_memcpy(pattrib->ta, pattrib->src, ETH_ALEN);

	//for tdls; pattrib->nr_frags is used to fill dialogtoken
	pattrib->nr_frags = dialogtoken;		
	dialogtoken = (dialogtoken+1)%256;

	update_tdls_attrib(padapter, pattrib);

	//init peer sta_info
	ptdls_sta = rtw_get_stainfo(pstapriv, mac_addr);
	if(ptdls_sta==NULL){
		ptdls_sta = rtw_alloc_stainfo(pstapriv, mac_addr);
	}
	
	if(ptdls_sta){	
		ptdls_sta->state |= TDLS_RESPONDER_STATE;
		//for tdls; ptdls_sta->aid is used to fill dialogtoken
		ptdls_sta->aid = pattrib->nr_frags;		
		ptdls_sta->TDLS_PeerKey_Lifetime = timeout_interval;
		_enter_critical_bh(&(pstapriv->sta_hash_lock), &irqL);	
		pmlmeinfo->tdls_sta_cnt++;
		_exit_critical_bh(&(pstapriv->sta_hash_lock), &irqL);	
	}
	else	{
		rtw_free_xmitbuf(pxmitpriv,pmgntframe->pxmitbuf);
		rtw_free_xmitframe_ex(pxmitpriv, pmgntframe);
		goto exit;
	}

	pattrib->qsel=pattrib->priority;
	if(xmit_tdls_coalesce(padapter, pmgntframe, TDLS_SETUP_REQUEST) !=_SUCCESS ){
		rtw_free_xmitbuf(pxmitpriv,pmgntframe->pxmitbuf);
		rtw_free_xmitframe_ex(pxmitpriv, pmgntframe);
	}
	rtw_dump_xframe(padapter, pmgntframe);

exit:

	return;
}

void issue_tdls_teardown(_adapter *padapter, u8 *mac_addr)
{
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct sta_info	*ptdls_sta=NULL;
	_irqL irqL;

	ptdls_sta = rtw_get_stainfo(pstapriv, mac_addr);
	if(ptdls_sta==NULL){
		DBG_8192C("issue tdls teardown unsuccessful\n");
		return;
	}else{
		ptdls_sta->state=UN_TDLS_STATE;
	}

	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		return;
	}
	
	//update attribute
	pattrib = &pmgntframe->attrib;

	pmgntframe->frame_tag = DATA_FRAMETAG;
	pattrib->ether_type = 0x890d;
	pattrib->pctrl =0;

	_rtw_memcpy(pattrib->dst, mac_addr, ETH_ALEN);
	_rtw_memcpy(pattrib->src, myid(&(padapter->eeprompriv)), ETH_ALEN);

	_rtw_memcpy(pattrib->ra, get_bssid(pmlmepriv), ETH_ALEN);
	_rtw_memcpy(pattrib->ta, pattrib->src, ETH_ALEN);

	update_tdls_attrib(padapter, pattrib);
	pattrib->qsel=pattrib->priority;
	if (xmit_tdls_coalesce(padapter, pmgntframe, TDLS_TEARDOWN) != _SUCCESS) {
		rtw_free_xmitbuf(pxmitpriv, pmgntframe->pxmitbuf);
		rtw_free_xmitframe_ex(pxmitpriv, pmgntframe);
	}
	rtw_dump_xframe(padapter, pmgntframe);

	if(ptdls_sta->state & TDLS_CH_SWITCH_ON_STATE){
		ptdls_sta->option =3;
		_set_workitem(&ptdls_sta->option_workitem);
	}
	
	//free peer sta_info
	DBG_8192C("tdls teardown, free sta_info\n");
	_enter_critical_bh(&(pstapriv->sta_hash_lock), &irqL);		
	pmlmeinfo->tdls_sta_cnt--;
	_exit_critical_bh(&(pstapriv->sta_hash_lock), &irqL);
	//ready to clear cam
	if(ptdls_sta->cam_entry!=0){
		pmlmeinfo->tdls_cam_entry_to_clear=ptdls_sta->cam_entry;
		rtw_setstakey_cmd(padapter, (u8 *)ptdls_sta, _TRUE);
	}
	_set_workitem(&pmlmeext->TDLS_restore_workitem);
	rtw_free_stainfo(padapter,  ptdls_sta);
	if(pmlmeinfo->tdls_sta_cnt==0)
		pmlmeinfo->tdls_setup_state=UN_TDLS_STATE;		//tdls;kurt			

exit:

	return;
}

void issue_tdls_dis_req(_adapter *padapter)
{
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);

	static u8 dialogtoken=0;
	u8 mac_addr[ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff}; 
	
	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		return;
	}
	
	//update attribute
	pattrib = &pmgntframe->attrib;

	pmgntframe->frame_tag = DATA_FRAMETAG;
	pattrib->ether_type = 0x890d;
	pattrib->pctrl =0;

	_rtw_memcpy(pattrib->dst, mac_addr, ETH_ALEN);
	_rtw_memcpy(pattrib->src, myid(&(padapter->eeprompriv)), ETH_ALEN);

	_rtw_memcpy(pattrib->ra, get_bssid(pmlmepriv), ETH_ALEN);
	_rtw_memcpy(pattrib->ta, pattrib->src, ETH_ALEN);

	//for tdls; pattrib->nr_frags is used to fill dialogtoken
	pattrib->nr_frags = dialogtoken;
	dialogtoken = (dialogtoken+1)%256;
	//for tdls; pattrib->type is used to fill status
	pattrib->type = 0;

	update_tdls_attrib(padapter, pattrib);
	pattrib->qsel=pattrib->priority;
	if (xmit_tdls_coalesce(padapter, pmgntframe, TDLS_DISCOVERY_REQUEST) != _SUCCESS) {
		rtw_free_xmitbuf(pxmitpriv, pmgntframe->pxmitbuf);
		rtw_free_xmitframe_ex(pxmitpriv, pmgntframe);
	}
	rtw_dump_xframe(padapter, pmgntframe);
	DBG_8192C("issue tdls dis req\n");

exit:

	return;
}

void issue_tdls_setup_rsp(_adapter *padapter, union recv_frame *precv_frame)
{
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct sta_info	*ptdls_sta=NULL;
	_irqL irqL;

	struct rx_pkt_attrib	*rx_pkt_pattrib = &precv_frame->u.hdr.attrib;

	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		return;
	}
	
	//update attribute
	pattrib = &pmgntframe->attrib;

	pmgntframe->frame_tag = DATA_FRAMETAG;
	pattrib->ether_type = 0x890d;
	pattrib->pctrl =0;

	_rtw_memcpy(pattrib->dst, rx_pkt_pattrib->src, ETH_ALEN);
	_rtw_memcpy(pattrib->src, myid(&(padapter->eeprompriv)), ETH_ALEN);

	_rtw_memcpy(pattrib->ra, rx_pkt_pattrib->bssid, ETH_ALEN);
	_rtw_memcpy(pattrib->ta, pattrib->src, ETH_ALEN);

	//pattrib->nr_frags is used to fill dialogtoken
	pattrib->nr_frags = rx_pkt_pattrib->frag_num;

	update_tdls_attrib(padapter, pattrib);
	pattrib->qsel=pattrib->priority;
	if (xmit_tdls_coalesce(padapter, pmgntframe, TDLS_SETUP_RESPONSE) != _SUCCESS) {
		rtw_free_xmitbuf(pxmitpriv, pmgntframe->pxmitbuf);
		rtw_free_xmitframe_ex(pxmitpriv, pmgntframe);
	}
	rtw_dump_xframe(padapter, pmgntframe);

	ptdls_sta = rtw_get_stainfo(pstapriv, pattrib->dst);
	//status code!=0 ; setup unsuccess
	if(ptdls_sta->stat_code!=0){
		_enter_critical_bh(&(pstapriv->sta_hash_lock), &irqL);		
		pmlmeinfo->tdls_sta_cnt--;
		_exit_critical_bh(&(pstapriv->sta_hash_lock), &irqL);
		rtw_free_stainfo(padapter,  ptdls_sta);		
		if(pmlmeinfo->tdls_sta_cnt==0)
			pmlmeinfo->tdls_setup_state=UN_TDLS_STATE;
	}
	
exit:

	return;

}

void issue_tdls_setup_cfm(_adapter *padapter, union recv_frame *precv_frame)
{
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct sta_priv		*pstapriv = &padapter->stapriv;
	struct sta_info		*ptdls_sta=NULL;
	_irqL irqL;

	struct rx_pkt_attrib	*rx_pkt_pattrib = & precv_frame->u.hdr.attrib;
	
	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		return;
	}
	
	//update attribute
	pattrib = &pmgntframe->attrib;

	pmgntframe->frame_tag = DATA_FRAMETAG;
	pattrib->ether_type = 0x890d;
	pattrib->pctrl =0;

	_rtw_memcpy(pattrib->dst, rx_pkt_pattrib->src, ETH_ALEN);
	_rtw_memcpy(pattrib->src, myid(&(padapter->eeprompriv)), ETH_ALEN);

	_rtw_memcpy(pattrib->ra, rx_pkt_pattrib->bssid, ETH_ALEN);
	_rtw_memcpy(pattrib->ta, pattrib->src, ETH_ALEN);

	//pattrib->nr_frags is used to fill dialogtoken
	pattrib->nr_frags = rx_pkt_pattrib->frag_num;		

	update_tdls_attrib(padapter, pattrib);
	pattrib->qsel=pattrib->priority;
	if (xmit_tdls_coalesce(padapter, pmgntframe, TDLS_SETUP_CONFIRM) != _SUCCESS) {
		rtw_free_xmitbuf(pxmitpriv, pmgntframe->pxmitbuf);
		rtw_free_xmitframe_ex(pxmitpriv, pmgntframe);
	}
	pmlmeinfo->tdls_setup_state=TDLS_LINKED_STATE;

	rtw_dump_xframe(padapter, pmgntframe);

	ptdls_sta=rtw_get_stainfo(pstapriv, pattrib->dst);

	ptdls_sta->option=1;
	_set_workitem(&ptdls_sta->option_workitem);
	//status code!=0 ; setup unsuccess
	if(ptdls_sta->stat_code!=0){
		_enter_critical_bh(&(pstapriv->sta_hash_lock), &irqL);
		pmlmeinfo->tdls_sta_cnt--;
		_exit_critical_bh(&(pstapriv->sta_hash_lock), &irqL);
		//ready to clear cam
		if(ptdls_sta->cam_entry!=0){
			pmlmeinfo->tdls_cam_entry_to_clear=ptdls_sta->cam_entry;
			rtw_setstakey_cmd(padapter, (u8 *)ptdls_sta, _TRUE);
		}
		rtw_free_stainfo(padapter,  ptdls_sta);
		if(pmlmeinfo->tdls_sta_cnt==0)
			pmlmeinfo->tdls_setup_state=UN_TDLS_STATE;
	}
		
exit:

	return;

}

//TDLS Discovery Response frame is a management action frame
void issue_tdls_dis_rsp(_adapter *padapter, union recv_frame *precv_frame)
{
	struct xmit_frame		*pmgntframe;
	struct pkt_attrib		*pattrib;
	unsigned char			*pframe;
	struct ieee80211_hdr	*pwlanhdr;
	unsigned short		*fctrl;
	struct xmit_priv		*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	struct rx_pkt_attrib	*rx_pkt_pattrib = &precv_frame->u.hdr.attrib;

	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		return;
	}

	//update attribute
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);

	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	//	unicast probe request frame
	_rtw_memcpy(pwlanhdr->addr1, rx_pkt_pattrib->src, ETH_ALEN);
	_rtw_memcpy(pattrib->dst, pwlanhdr->addr1, ETH_ALEN);
	
	_rtw_memcpy(pwlanhdr->addr2, myid(&(padapter->eeprompriv)), ETH_ALEN);
	_rtw_memcpy(pattrib->src, pwlanhdr->addr2, ETH_ALEN);

	_rtw_memcpy(pwlanhdr->addr3, rx_pkt_pattrib->bssid, ETH_ALEN);
	_rtw_memcpy(pattrib->ra, pwlanhdr->addr3, ETH_ALEN);
	
	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_ACTION);

	pframe += sizeof (struct ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof (struct ieee80211_hdr_3addr);
	//tdls; pattrib->nr_frags is used to fill dialogtoken
	pattrib->nr_frags = rx_pkt_pattrib->frag_num;
	
	fill_tdls_dis_rsp_frbody(padapter, pmgntframe, pframe);

	pattrib->nr_frags = 1;
	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe(padapter, pmgntframe);

	return;
}

void issue_tdls_peer_traffic_indication(_adapter *padapter, struct sta_info *psta)
{
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);

	static u8 dialogtoken=0;
	
	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		return;
	}
	
	//update attribute
	pattrib = &pmgntframe->attrib;

	pmgntframe->frame_tag = DATA_FRAMETAG;
	pattrib->ether_type = 0x890d;
	pattrib->pctrl =0;

	_rtw_memcpy(pattrib->dst, psta->hwaddr, ETH_ALEN);
	_rtw_memcpy(pattrib->src, myid(&(padapter->eeprompriv)), ETH_ALEN);

	_rtw_memcpy(pattrib->ra, get_bssid(pmlmepriv), ETH_ALEN);
	_rtw_memcpy(pattrib->ta, pattrib->src, ETH_ALEN);

	//for tdls; pattrib->nr_frags is used to fill dialogtoken
	pattrib->nr_frags = dialogtoken;
	dialogtoken = (dialogtoken+1)%256;
	//for tdls; pattrib->type is used to fill status
	pattrib->type = 0;
	//PTI frame's priority should be AC_VO
	pattrib->priority = 7; 

	update_tdls_attrib(padapter, pattrib);
	pattrib->qsel=pattrib->priority;
	if (xmit_tdls_coalesce(padapter, pmgntframe, TDLS_PEER_TRAFFIC_INDICATION) != _SUCCESS) {
		rtw_free_xmitbuf(pxmitpriv, pmgntframe->pxmitbuf);
		rtw_free_xmitframe_ex(pxmitpriv, pmgntframe);
	}
	rtw_dump_xframe(padapter, pmgntframe);
	
exit:

	return;
}

void issue_tdls_ch_switch_req(_adapter *padapter, u8 *mac_addr)
{
	struct xmit_frame	*pmgntframe;
	struct pkt_attrib	*pattrib;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct xmit_priv	*pxmitpriv = &(padapter->xmitpriv);

	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		return;
	}
	
	//update attribute
	pattrib = &pmgntframe->attrib;

	pmgntframe->frame_tag = DATA_FRAMETAG;
	pattrib->ether_type = 0x890d;
	pattrib->pctrl =0;		

	_rtw_memcpy(pattrib->dst, mac_addr, ETH_ALEN);
	_rtw_memcpy(pattrib->src, myid(&(padapter->eeprompriv)), ETH_ALEN);

	_rtw_memcpy(pattrib->ra, get_bssid(pmlmepriv), ETH_ALEN);
	_rtw_memcpy(pattrib->ta, pattrib->src, ETH_ALEN);

	update_tdls_attrib(padapter, pattrib);

	pattrib->qsel=pattrib->priority;
	if(xmit_tdls_coalesce(padapter, pmgntframe, TDLS_CHANNEL_SWITCH_REQUEST) !=_SUCCESS ){
		rtw_free_xmitbuf(pxmitpriv,pmgntframe->pxmitbuf);
		rtw_free_xmitframe_ex(pxmitpriv, pmgntframe);
	}
	rtw_dump_xframe(padapter, pmgntframe);

exit:

	return;
}

void issue_tdls_ch_switch_rsp(_adapter *padapter, u8 *mac_addr)
{
	struct xmit_frame	*pmgntframe;
	struct pkt_attrib	*pattrib;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct xmit_priv	*pxmitpriv = &(padapter->xmitpriv);

        _irqL irqL;	
		
	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		return;
	}
	
	//update attribute
	pattrib = &pmgntframe->attrib;

	pmgntframe->frame_tag = DATA_FRAMETAG;
	pattrib->ether_type = 0x890d;
	pattrib->pctrl =0;		

	_rtw_memcpy(pattrib->dst, mac_addr, ETH_ALEN);
	_rtw_memcpy(pattrib->src, myid(&(padapter->eeprompriv)), ETH_ALEN);

	_rtw_memcpy(pattrib->ra, get_bssid(pmlmepriv), ETH_ALEN);
	_rtw_memcpy(pattrib->ta, pattrib->src, ETH_ALEN);

	update_tdls_attrib(padapter, pattrib);

	pattrib->qsel=pattrib->priority;
/*
	_enter_critical_bh(&pxmitpriv->lock, &irqL);
	if(xmit_tdls_enqueue_for_sleeping_sta(padapter, pmgntframe)==_TRUE){
		_exit_critical_bh(&pxmitpriv->lock, &irqL);
		return _FALSE;
	}
*/
	if(xmit_tdls_coalesce(padapter, pmgntframe, TDLS_CHANNEL_SWITCH_RESPONSE) !=_SUCCESS ){
		rtw_free_xmitbuf(pxmitpriv,pmgntframe->pxmitbuf);
		rtw_free_xmitframe_ex(pxmitpriv, pmgntframe);
	}
	rtw_dump_xframe(padapter, pmgntframe);

exit:

	return;
}

sint On_TDLS_Dis_Rsp(_adapter *adapter, union recv_frame *precv_frame)
{
	struct mlme_ext_priv *pmlmeext = &adapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	if(pmlmeinfo->tdls_dis_req==1 || pmlmeinfo->tdls_dis_req==2){
		report_survey_event(adapter, precv_frame);
	}

	return _FAIL;
}

u8 collect_tdls_info(_adapter *padapter, union recv_frame *precv_frame, WLAN_BSSID_EX *bssid)
{
	int FIXED_IE=5;
	int				i;
	unsigned int		len;
	unsigned char		*p;
	u8 *pframe = precv_frame->u.hdr.rx_data;
	uint packet_len = precv_frame->u.hdr.len;
	struct rx_pkt_attrib	*pattrib = &precv_frame->u.hdr.attrib;

//	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(padapter);
//	struct registry_priv *pregistrypriv = &padapter->registrypriv;

	//[-24]: mgt_frame hdrlen
	len = packet_len - 24 ;

	if (len > MAX_IE_SZ)
	{
		DBG_8192C("TDLS dis rsp IE too long for survey event\n");
		return _FAIL;
	}

	_rtw_memset(bssid, 0, sizeof(WLAN_BSSID_EX));

	//represent tdls peer
	bssid->Reserved[1] = 'T';
		
	bssid->Length = sizeof(WLAN_BSSID_EX) - MAX_IE_SZ + len;

	//below is to copy the information element
	bssid->IELength = len;
	_rtw_memcpy(bssid->IEs, (pframe + 24), bssid->IELength);

	//get the signal strength
	//bssid->Rssi = precv_frame->u.hdr.attrib.signal_strength; // 0-100 index.	
	bssid->Rssi = precv_frame->u.hdr.attrib.RecvSignalPower; // in dBM.raw data	
	bssid->PhyInfo.SignalQuality = precv_frame->u.hdr.attrib.signal_qual;//in percentage 
	bssid->PhyInfo.SignalStrength = precv_frame->u.hdr.attrib.signal_strength;//in percentage
#ifdef CONFIG_ANTENNA_DIVERSITY
	//padapter->HalFunc.GetHwRegHandler(padapter, HW_VAR_CURRENT_ANTENNA, (u8 *)(&bssid->PhyInfo.Optimum_antenna));
	padapter->HalFunc.GetHalDefVarHandler(padapter, HAL_DEF_CURRENT_ANTENNA,  &bssid->PhyInfo.Optimum_antenna);
#endif

	_rtw_memset(bssid->SupportedRates, 0, NDIS_802_11_LENGTH_RATES_EX);

	//checking rate info...
	i = 0;
	p = rtw_get_ie(bssid->IEs + FIXED_IE, _SUPPORTEDRATES_IE_, &len, bssid->IELength - FIXED_IE);
	if (p != NULL)
	{
		_rtw_memcpy(bssid->SupportedRates, (p + 2), len);
		i = len;
	}

	p = rtw_get_ie(bssid->IEs + FIXED_IE, _EXT_SUPPORTEDRATES_IE_, &len, bssid->IELength - FIXED_IE);
	if (p != NULL)
	{
		_rtw_memcpy(bssid->SupportedRates + i, (p + 2), len);
	}

	//pframe+10 would be src_addr
	_rtw_memcpy(bssid->MacAddress, (pframe+10), ETH_ALEN);
	return _SUCCESS;

}

#endif

void issue_beacon(_adapter *padapter)
{
	_irqL irqL;		
	struct xmit_frame	*pmgntframe;
	struct pkt_attrib	*pattrib;
	unsigned char	*pframe;
	struct ieee80211_hdr *pwlanhdr;
	unsigned short *fctrl;
	unsigned int	rate_len;
	struct xmit_priv	*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX 		*cur_network = &(pmlmeinfo->network);
	u8	bc_addr[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
#ifdef CONFIG_P2P
	struct wifidirect_info	*pwdinfo = &(padapter->wdinfo);
#endif //CONFIG_P2P


	//DBG_871X("%s\n", __FUNCTION__);

	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		DBG_871X("%s, alloc mgnt frame fail\n", __FUNCTION__);
		return;
	}

	_enter_critical_bh(&pmlmepriv->bcn_update_lock, &irqL);			

	//update attribute
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);
	pattrib->qsel = 0x10;
	
	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);
		
	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct ieee80211_hdr *)pframe;	
	
	
	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;
	
	_rtw_memcpy(pwlanhdr->addr1, bc_addr, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, myid(&(padapter->eeprompriv)), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, get_my_bssid(cur_network), ETH_ALEN);

	SetSeqNum(pwlanhdr, 0/*pmlmeext->mgnt_seq*/);
	//pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_BEACON);
	
	pframe += sizeof(struct ieee80211_hdr_3addr);	
	pattrib->pktlen = sizeof (struct ieee80211_hdr_3addr);
	
	if( (pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE)
	{
		//DBG_871X("ie len=%d\n", cur_network->IELength);
#ifdef CONFIG_P2P
		// for P2P : Primary Device Type & Device Name
		u32 wpsielen=0, insert_len=0;
		u8 *wpsie=NULL;		
		wpsie = rtw_get_wps_ie(cur_network->IEs, cur_network->IELength, NULL, &wpsielen);
		
		if(pwdinfo->role == P2P_ROLE_GO && wpsie && wpsielen>0)
		{
			uint wps_offset, remainder_ielen;
			u8 *premainder_ie, *pframe_wscie;		
	
			wps_offset = (uint)(wpsie - cur_network->IEs);

			premainder_ie = wpsie + wpsielen;

			remainder_ielen = cur_network->IELength - wps_offset - wpsielen;

			pframe_wscie = pframe + wps_offset;
			_rtw_memcpy(pframe, cur_network->IEs, wps_offset+wpsielen);			
			pframe += (wps_offset + wpsielen);		
			pattrib->pktlen += (wps_offset + wpsielen);

			//now pframe is end of wsc ie, insert Primary Device Type & Device Name
			//	Primary Device Type
			//	Type:
			*(u16*) ( pframe + insert_len) = cpu_to_be16( WPS_ATTR_PRIMARY_DEV_TYPE );
			insert_len += 2;
			
			//	Length:
			*(u16*) ( pframe + insert_len ) = cpu_to_be16( 0x0008 );
			insert_len += 2;
			
			//	Value:
			//	Category ID
			*(u16*) ( pframe + insert_len ) = cpu_to_be16( WPS_PDT_CID_RTK_WIDI );
			insert_len += 2;

			//	OUI
			*(u32*) ( pframe + insert_len ) = cpu_to_be32( WPSOUI );
			insert_len += 4;

			//	Sub Category ID
			*(u16*) ( pframe + insert_len ) = cpu_to_be16( WPS_PDT_SCID_RTK_DMP );
			insert_len += 2;


			//	Device Name
			//	Type:
			*(u16*) ( pframe + insert_len ) = cpu_to_be16( WPS_ATTR_DEVICE_NAME );
			insert_len += 2;

			//	Length:
			*(u16*) ( pframe + insert_len ) = cpu_to_be16( pwdinfo->device_name_len );
			insert_len += 2;

			//	Value:
			_rtw_memcpy( pframe + insert_len, pwdinfo->device_name, pwdinfo->device_name_len );
			insert_len += pwdinfo->device_name_len;


			//update wsc ie length
			*(pframe_wscie+1) = (wpsielen -2) + insert_len;

			//pframe move to end
			pframe+=insert_len;
			pattrib->pktlen += insert_len;

			//copy remainder_ie to pframe
			_rtw_memcpy(pframe, premainder_ie, remainder_ielen);
			pframe += remainder_ielen;		
			pattrib->pktlen += remainder_ielen;	
							
		}
		else
#endif //CONFIG_P2P
		{
			_rtw_memcpy(pframe, cur_network->IEs, cur_network->IELength);
			pframe += cur_network->IELength;
			pattrib->pktlen += cur_network->IELength;
		}

#ifdef CONFIG_P2P
		if(pwdinfo->role == P2P_ROLE_GO)
		{
			u32 len;
		
			len = build_beacon_p2p_ie(pwdinfo, pframe);
		
			pframe += len;
			pattrib->pktlen += len;
#ifdef CONFIG_WFD
			len = build_beacon_wfd_ie( pwdinfo, pframe );
			pframe += len;
			pattrib->pktlen += len;
#endif //CONFIG_WFD
		}
#endif //CONFIG_P2P

		goto _issue_bcn;

	}

	//below for ad-hoc mode

	//timestamp will be inserted by hardware
	pframe += 8;
	pattrib->pktlen += 8;

	// beacon interval: 2 bytes

	_rtw_memcpy(pframe, (unsigned char *)(rtw_get_beacon_interval_from_ie(cur_network->IEs)), 2); 

	pframe += 2;
	pattrib->pktlen += 2;

	// capability info: 2 bytes

	_rtw_memcpy(pframe, (unsigned char *)(rtw_get_capability_from_ie(cur_network->IEs)), 2);

	pframe += 2;
	pattrib->pktlen += 2;

	// SSID
	pframe = rtw_set_ie(pframe, _SSID_IE_, cur_network->Ssid.SsidLength, cur_network->Ssid.Ssid, &pattrib->pktlen);

	// supported rates...
	rate_len = rtw_get_rateset_len(cur_network->SupportedRates);
	pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_, ((rate_len > 8)? 8: rate_len), cur_network->SupportedRates, &pattrib->pktlen);

	// DS parameter set
	pframe = rtw_set_ie(pframe, _DSSET_IE_, 1, (unsigned char *)&(cur_network->Configuration.DSConfig), &pattrib->pktlen);

	//if( (pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE)
	{
		u8 erpinfo=0;
		u32 ATIMWindow;
		// IBSS Parameter Set...
		//ATIMWindow = cur->Configuration.ATIMWindow;
		ATIMWindow = 0;
		pframe = rtw_set_ie(pframe, _IBSS_PARA_IE_, 2, (unsigned char *)(&ATIMWindow), &pattrib->pktlen);

		//ERP IE
		pframe = rtw_set_ie(pframe, _ERPINFO_IE_, 1, &erpinfo, &pattrib->pktlen);
	}	


	// EXTERNDED SUPPORTED RATE
	if (rate_len > 8)
	{
		pframe = rtw_set_ie(pframe, _EXT_SUPPORTEDRATES_IE_, (rate_len - 8), (cur_network->SupportedRates + 8), &pattrib->pktlen);
	}


	//todo:HT for adhoc

_issue_bcn:

	pmlmepriv->update_bcn = _FALSE;
	
	_exit_critical_bh(&pmlmepriv->bcn_update_lock, &irqL);	

	if ((pattrib->pktlen + TXDESC_SIZE) > 512)
	{
		DBG_871X("beacon frame too large\n");
		return;
	}
	
	pattrib->last_txcmdsz = pattrib->pktlen;

	//DBG_871X("issue bcn_sz=%d\n", pattrib->last_txcmdsz);

	dump_mgntframe(padapter, pmgntframe);

}

void issue_probersp(_adapter *padapter, unsigned char *da, u8 is_valid_p2p_probereq)
{
	u8 *pwps_ie;
	uint wps_ielen;
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	unsigned char					*pframe;
	struct ieee80211_hdr	*pwlanhdr;
	unsigned short				*fctrl;	
	unsigned char					*mac, *bssid;
	struct xmit_priv	*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX 		*cur_network = &(pmlmeinfo->network);
	unsigned int	rate_len;
#ifdef CONFIG_P2P
	struct wifidirect_info	*pwdinfo = &(padapter->wdinfo);	
#endif //CONFIG_P2P

	//DBG_871X("%s\n", __FUNCTION__);

	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		DBG_871X("%s, alloc mgnt frame fail\n", __FUNCTION__);
		return;
	}


	//update attribute
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);	
	
	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);
		
	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct ieee80211_hdr *)pframe;	
	
	mac = myid(&(padapter->eeprompriv));
	bssid = cur_network->MacAddress;
	
	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;
	_rtw_memcpy(pwlanhdr->addr1, da, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, mac, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, bssid, ETH_ALEN);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(fctrl, WIFI_PROBERSP);
	
	pattrib->hdrlen = sizeof(struct ieee80211_hdr_3addr);
	pattrib->pktlen = pattrib->hdrlen;
	pframe += pattrib->hdrlen;


	if(cur_network->IELength>MAX_IE_SZ)
		return;
	
#if defined (CONFIG_AP_MODE) && defined (CONFIG_NATIVEAP_MLME)
	if( (pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE)
	{
		pwps_ie = rtw_get_wps_ie(cur_network->IEs, cur_network->IELength, NULL, &wps_ielen);
	
		//inerset & update wps_probe_resp_ie
		if((pmlmepriv->wps_probe_resp_ie!=NULL) && pwps_ie && (wps_ielen>0))
		{
			uint wps_offset, remainder_ielen;
			u8 *premainder_ie;		
	
			wps_offset = (uint)(pwps_ie - cur_network->IEs);

			premainder_ie = pwps_ie + wps_ielen;

			remainder_ielen = cur_network->IELength - wps_offset - wps_ielen;

			_rtw_memcpy(pframe, cur_network->IEs, wps_offset);		
			pframe += wps_offset;		
			pattrib->pktlen += wps_offset;		

			wps_ielen = (uint)pmlmepriv->wps_probe_resp_ie[1];//to get ie data len
			if((wps_offset+wps_ielen+2)<=MAX_IE_SZ)
			{
				_rtw_memcpy(pframe, pmlmepriv->wps_probe_resp_ie, wps_ielen+2);
				pframe += wps_ielen+2;		
				pattrib->pktlen += wps_ielen+2;	
			}

			if((wps_offset+wps_ielen+2+remainder_ielen)<=MAX_IE_SZ)
			{
				_rtw_memcpy(pframe, premainder_ie, remainder_ielen);
				pframe += remainder_ielen;		
				pattrib->pktlen += remainder_ielen;	
			}
		}
		else
		{
			_rtw_memcpy(pframe, cur_network->IEs, cur_network->IELength);
			pframe += cur_network->IELength;
			pattrib->pktlen += cur_network->IELength;
		}
		
	}	
	else		
#endif		
	{
	
		//timestamp will be inserted by hardware
		pframe += 8;
		pattrib->pktlen += 8;

		// beacon interval: 2 bytes

		_rtw_memcpy(pframe, (unsigned char *)(rtw_get_beacon_interval_from_ie(cur_network->IEs)), 2); 

		pframe += 2;
		pattrib->pktlen += 2;

		// capability info: 2 bytes

		_rtw_memcpy(pframe, (unsigned char *)(rtw_get_capability_from_ie(cur_network->IEs)), 2);

		pframe += 2;
		pattrib->pktlen += 2;

		//below for ad-hoc mode

		// SSID
		pframe = rtw_set_ie(pframe, _SSID_IE_, cur_network->Ssid.SsidLength, cur_network->Ssid.Ssid, &pattrib->pktlen);

		// supported rates...
		rate_len = rtw_get_rateset_len(cur_network->SupportedRates);
		pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_, ((rate_len > 8)? 8: rate_len), cur_network->SupportedRates, &pattrib->pktlen);

		// DS parameter set
		pframe =rtw_set_ie(pframe, _DSSET_IE_, 1, (unsigned char *)&(cur_network->Configuration.DSConfig), &pattrib->pktlen);

		if( (pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE)
		{
			u8 erpinfo=0;
			u32 ATIMWindow;
			// IBSS Parameter Set...
			//ATIMWindow = cur->Configuration.ATIMWindow;
			ATIMWindow = 0;
			pframe = rtw_set_ie(pframe, _IBSS_PARA_IE_, 2, (unsigned char *)(&ATIMWindow), &pattrib->pktlen);

			//ERP IE
			pframe = rtw_set_ie(pframe, _ERPINFO_IE_, 1, &erpinfo, &pattrib->pktlen);
		}

		
		// EXTERNDED SUPPORTED RATE
		if (rate_len > 8)
		{
			pframe = rtw_set_ie(pframe, _EXT_SUPPORTEDRATES_IE_, (rate_len - 8), (cur_network->SupportedRates + 8), &pattrib->pktlen);
		}


		//todo:HT for adhoc

	}	

#ifdef CONFIG_P2P
	if(pwdinfo->role == P2P_ROLE_GO && is_valid_p2p_probereq)
	{
		u32 len;
		
		len = build_probe_resp_p2p_ie(pwdinfo, pframe);
		
		pframe += len;
		pattrib->pktlen += len;
	}
#endif //CONFIG_P2P


	pattrib->last_txcmdsz = pattrib->pktlen;
	

	dump_mgntframe(padapter, pmgntframe);
	
	return;

}

void issue_probereq(_adapter *padapter, u8 blnbc)
{
	struct xmit_frame		*pmgntframe;
	struct pkt_attrib		*pattrib;
	unsigned char			*pframe;
	struct ieee80211_hdr	*pwlanhdr;
	unsigned short		*fctrl;
	unsigned char			*mac;
	unsigned char			bssrate[NumRates];
	struct xmit_priv		*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	int	bssrate_len = 0;
	u8	bc_addr[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

	RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("+issue_probereq\n"));

	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		return;
	}

	//update attribute
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);


	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct ieee80211_hdr *)pframe;

	mac = myid(&(padapter->eeprompriv));

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	if ( 0 == blnbc )
	{
		//	unicast probe request frame
		_rtw_memcpy(pwlanhdr->addr1, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);
		_rtw_memcpy(pwlanhdr->addr3, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);
	}
	else
	{
		//	broadcast probe request frame
		_rtw_memcpy(pwlanhdr->addr1, bc_addr, ETH_ALEN);
		_rtw_memcpy(pwlanhdr->addr3, bc_addr, ETH_ALEN);
	}

	_rtw_memcpy(pwlanhdr->addr2, mac, ETH_ALEN);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_PROBEREQ);

	pframe += sizeof (struct ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof (struct ieee80211_hdr_3addr);

	pframe = rtw_set_ie(pframe, _SSID_IE_, pmlmeext->sitesurvey_res.ss_ssidlen, pmlmeext->sitesurvey_res.ss_ssid, &(pattrib->pktlen));

	get_rate_set(padapter, bssrate, &bssrate_len);

	if (bssrate_len > 8)
	{
		pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_ , 8, bssrate, &(pattrib->pktlen));
		pframe = rtw_set_ie(pframe, _EXT_SUPPORTEDRATES_IE_ , (bssrate_len - 8), (bssrate + 8), &(pattrib->pktlen));
	}
	else
	{
		pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_ , bssrate_len , bssrate, &(pattrib->pktlen));
	}


	//add wps_ie for wps2.0
	if(pmlmepriv->probereq_wpsie_len>0 && pmlmepriv->probereq_wpsie_len<MAX_WPS_IE_LEN)
	{
		_rtw_memcpy(pframe, pmlmepriv->probereq_wpsie, pmlmepriv->probereq_wpsie_len);
		pframe += pmlmepriv->probereq_wpsie_len;
		pattrib->pktlen += pmlmepriv->probereq_wpsie_len;
		//pmlmepriv->probereq_wpsie_len = 0 ;//reset to zero		
	}	


	pattrib->last_txcmdsz = pattrib->pktlen;

	RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("issuing probe_req, tx_len=%d\n", pattrib->last_txcmdsz));

	dump_mgntframe(padapter, pmgntframe);

	return;
}

// if psta == NULL, indiate we are station(client) now...
void issue_auth(_adapter *padapter, struct sta_info *psta, unsigned short status)
{
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	unsigned char					*pframe;
	struct ieee80211_hdr	*pwlanhdr;
	unsigned short				*fctrl;
	unsigned int					val32;
	unsigned short				val16;
	int use_shared_key = 0;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	/*
	 * Send a deauth message before auth. (meitu's disconnect issue)
	 */
	issue_deauth( padapter, (&(pmlmeinfo->network))->MacAddress, WLAN_REASON_DEAUTH_LEAVING);

	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		return;
	}

	//update attribute
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);

	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_AUTH);

	pframe += sizeof(struct ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct ieee80211_hdr_3addr);


	if(psta)// for AP mode
	{
#ifdef CONFIG_NATIVEAP_MLME

		_rtw_memcpy(pwlanhdr->addr1, psta->hwaddr, ETH_ALEN);		
		_rtw_memcpy(pwlanhdr->addr2, myid(&(padapter->eeprompriv)), ETH_ALEN);
		_rtw_memcpy(pwlanhdr->addr3, myid(&(padapter->eeprompriv)), ETH_ALEN);

	
		// setting auth algo number	
		val16 = (u16)psta->authalg;

		if(status != _STATS_SUCCESSFUL_)
			val16 = 0;

		if (val16)	{
			val16 = cpu_to_le16(val16);	
			use_shared_key = 1;
		}

		pframe = rtw_set_fixed_ie(pframe, _AUTH_ALGM_NUM_, (unsigned char *)&val16, &(pattrib->pktlen));

		// setting auth seq number
		val16 =(u16)psta->auth_seq;
		val16 = cpu_to_le16(val16);	
		pframe = rtw_set_fixed_ie(pframe, _AUTH_SEQ_NUM_, (unsigned char *)&val16, &(pattrib->pktlen));

		// setting status code...
		val16 = status;
		val16 = cpu_to_le16(val16);	
		pframe = rtw_set_fixed_ie(pframe, _STATUS_CODE_, (unsigned char *)&val16, &(pattrib->pktlen));

		// added challenging text...
		if ((psta->auth_seq == 2) && (psta->state & WIFI_FW_AUTH_STATE) && (use_shared_key==1))
		{
			pframe = rtw_set_ie(pframe, _CHLGETXT_IE_, 128, psta->chg_txt, &(pattrib->pktlen));			
		}
#endif
	}
	else
	{		
		_rtw_memcpy(pwlanhdr->addr1, get_my_bssid(&pmlmeinfo->network), ETH_ALEN);
		_rtw_memcpy(pwlanhdr->addr2, myid(&padapter->eeprompriv), ETH_ALEN);
		_rtw_memcpy(pwlanhdr->addr3, get_my_bssid(&pmlmeinfo->network), ETH_ALEN);
	
		// setting auth algo number		
		val16 = (pmlmeinfo->auth_algo == dot11AuthAlgrthm_Shared)? 1: 0;// 0:OPEN System, 1:Shared key
		if (val16)	{
			val16 = cpu_to_le16(val16);	
			use_shared_key = 1;
		}	
		//DBG_8192C("%s auth_algo= %s auth_seq=%d\n",__FUNCTION__,(pmlmeinfo->auth_algo==0)?"OPEN":"SHARED",pmlmeinfo->auth_seq);
		
		//setting IV for auth seq #3
		if ((pmlmeinfo->auth_seq == 3) && (pmlmeinfo->state & WIFI_FW_AUTH_STATE) && (use_shared_key==1))
		{
			//DBG_8192C("==> iv(%d),key_index(%d)\n",pmlmeinfo->iv,pmlmeinfo->key_index);
			val32 = ((pmlmeinfo->iv++) | (pmlmeinfo->key_index << 30));
			val32 = cpu_to_le32(val32);
			pframe = rtw_set_fixed_ie(pframe, 4, (unsigned char *)&val32, &(pattrib->pktlen));

			pattrib->iv_len = 4;
		}

		pframe = rtw_set_fixed_ie(pframe, _AUTH_ALGM_NUM_, (unsigned char *)&val16, &(pattrib->pktlen));
		
		// setting auth seq number
		val16 = pmlmeinfo->auth_seq;
		val16 = cpu_to_le16(val16);	
		pframe = rtw_set_fixed_ie(pframe, _AUTH_SEQ_NUM_, (unsigned char *)&val16, &(pattrib->pktlen));

		
		// setting status code...
		val16 = status;
		val16 = cpu_to_le16(val16);	
		pframe = rtw_set_fixed_ie(pframe, _STATUS_CODE_, (unsigned char *)&val16, &(pattrib->pktlen));

		// then checking to see if sending challenging text...
		if ((pmlmeinfo->auth_seq == 3) && (pmlmeinfo->state & WIFI_FW_AUTH_STATE) && (use_shared_key==1))
		{
			pframe = rtw_set_ie(pframe, _CHLGETXT_IE_, 128, pmlmeinfo->chg_txt, &(pattrib->pktlen));

			SetPrivacy(fctrl);
			
			pattrib->hdrlen = sizeof(struct ieee80211_hdr_3addr);			
			
			pattrib->encrypt = _WEP40_;

			pattrib->icv_len = 4;
			
			pattrib->pktlen += pattrib->icv_len;			
			
		}
		
	}

	pattrib->last_txcmdsz = pattrib->pktlen;

	rtw_wep_encrypt(padapter, (u8 *)pmgntframe);

	dump_mgntframe(padapter, pmgntframe);

	return;
}


void issue_asocrsp(_adapter *padapter, unsigned short status, struct sta_info *pstat, int pkt_type)
{
#ifdef CONFIG_AP_MODE
	struct xmit_frame	*pmgntframe;
	struct ieee80211_hdr	*pwlanhdr;
	struct pkt_attrib *pattrib;
	unsigned char	*pbuf, *pframe;
	unsigned short val;		
	unsigned short *fctrl;
	struct xmit_priv *pxmitpriv = &(padapter->xmitpriv);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);	
	WLAN_BSSID_EX *pnetwork = &(pmlmeinfo->network);
	u8 *ie = pnetwork->IEs; 
#ifdef CONFIG_P2P
	struct wifidirect_info	*pwdinfo = &(padapter->wdinfo);
#endif //CONFIG_P2P

	DBG_871X("%s\n", __FUNCTION__);

	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		return;
	}

	//update attribute
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);


	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	_rtw_memcpy((void *)GetAddr1Ptr(pwlanhdr), pstat->hwaddr, ETH_ALEN);
	_rtw_memcpy((void *)GetAddr2Ptr(pwlanhdr), myid(&(padapter->eeprompriv)), ETH_ALEN);
	_rtw_memcpy((void *)GetAddr3Ptr(pwlanhdr), get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);


	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	if ((pkt_type == WIFI_ASSOCRSP) || (pkt_type == WIFI_REASSOCRSP))
		SetFrameSubType(pwlanhdr, pkt_type);		
	else
		return;

	pattrib->hdrlen = sizeof(struct ieee80211_hdr_3addr);
	pattrib->pktlen += pattrib->hdrlen;
	pframe += pattrib->hdrlen;

	//capability
	val = *(unsigned short *)rtw_get_capability_from_ie(ie);

	pframe = rtw_set_fixed_ie(pframe, _CAPABILITY_ , (unsigned char *)&val, &(pattrib->pktlen));

	status = cpu_to_le16(status);
	pframe = rtw_set_fixed_ie(pframe , _STATUS_CODE_ , (unsigned char *)&status, &(pattrib->pktlen));
	
	val = cpu_to_le16(pstat->aid | BIT(14) | BIT(15));
	pframe = rtw_set_fixed_ie(pframe, _ASOC_ID_ , (unsigned char *)&val, &(pattrib->pktlen));

	if (pstat->bssratelen <= 8)
	{
		pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_, pstat->bssratelen, pstat->bssrateset, &(pattrib->pktlen));
	}	
	else 
	{
		pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_, 8, pstat->bssrateset, &(pattrib->pktlen));
		pframe = rtw_set_ie(pframe, _EXT_SUPPORTEDRATES_IE_, (pstat->bssratelen-8), pstat->bssrateset+8, &(pattrib->pktlen));
	}

#ifdef CONFIG_80211N_HT
	if ((pstat->flags & WLAN_STA_HT) && (pmlmepriv->htpriv.ht_option))
	{
		uint ie_len=0;
		
		//FILL HT CAP INFO IE
		//p = hostapd_eid_ht_capabilities_info(hapd, p);
		pbuf = rtw_get_ie(ie + _BEACON_IE_OFFSET_, _HT_CAPABILITY_IE_, &ie_len, (pnetwork->IELength - _BEACON_IE_OFFSET_));
		if(pbuf && ie_len>0)
		{
			_rtw_memcpy(pframe, pbuf, ie_len+2);
			pframe += (ie_len+2);
			pattrib->pktlen +=(ie_len+2);
		}

		//FILL HT ADD INFO IE
		//p = hostapd_eid_ht_operation(hapd, p);
		pbuf = rtw_get_ie(ie + _BEACON_IE_OFFSET_, _HT_ADD_INFO_IE_, &ie_len, (pnetwork->IELength - _BEACON_IE_OFFSET_));
		if(pbuf && ie_len>0)
		{
			_rtw_memcpy(pframe, pbuf, ie_len+2);
			pframe += (ie_len+2);
			pattrib->pktlen +=(ie_len+2);
		}
		
	}	
#endif

	//FILL WMM IE
	if ((pstat->flags & WLAN_STA_WME) && (pmlmepriv->qospriv.qos_option))
	{
		uint ie_len=0;
		unsigned char WMM_PARA_IE[] = {0x00, 0x50, 0xf2, 0x02, 0x01, 0x01};	
		
		for (pbuf = ie + _BEACON_IE_OFFSET_; ;pbuf+= (ie_len + 2))
		{			
			pbuf = rtw_get_ie(pbuf, _VENDOR_SPECIFIC_IE_, &ie_len, (pnetwork->IELength - _BEACON_IE_OFFSET_ - (ie_len + 2)));	
			if(pbuf && _rtw_memcmp(pbuf+2, WMM_PARA_IE, 6)) 
			{				
				_rtw_memcpy(pframe, pbuf, ie_len+2);
				pframe += (ie_len+2);
				pattrib->pktlen +=(ie_len+2);
				
				break;				
			}
			
			if ((pbuf == NULL) || (ie_len == 0))
			{
				break;
			}			
		}
		
	}


	if (pmlmeinfo->assoc_AP_vendor == realtekAP)
	{
		pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, 6 , REALTEK_96B_IE, &(pattrib->pktlen));
	}

	//add WPS IE ie for wps 2.0
	if(pmlmepriv->wps_assoc_resp_ie && pmlmepriv->wps_assoc_resp_ie_len>0)
	{
		_rtw_memcpy(pframe, pmlmepriv->wps_assoc_resp_ie, pmlmepriv->wps_assoc_resp_ie_len);
		
		pframe += pmlmepriv->wps_assoc_resp_ie_len;
		pattrib->pktlen += pmlmepriv->wps_assoc_resp_ie_len;
	}

#ifdef CONFIG_P2P
	if((pwdinfo->role == P2P_ROLE_GO) && (pstat->is_p2p_device == _TRUE))
	{
		u32 len;

		len = build_assoc_resp_p2p_ie(pwdinfo, pframe, pstat->p2p_status_code);

		pframe += len;
		pattrib->pktlen += len;
	}
#endif //CONFIG_P2P

	pattrib->last_txcmdsz = pattrib->pktlen;
	
	dump_mgntframe(padapter, pmgntframe);
	
#endif
}

void issue_assocreq(_adapter *padapter)
{
	struct xmit_frame				*pmgntframe;
	struct pkt_attrib				*pattrib;
	unsigned char					*pframe, *p;
	struct ieee80211_hdr			*pwlanhdr;
	unsigned short				*fctrl;
	unsigned short				val16;
	unsigned int					i, j, ie_len, index=0;
	unsigned char					rf_type, bssrate[NumRates], sta_bssrate[NumRates];
	PNDIS_802_11_VARIABLE_IEs	pIE;
	struct registry_priv	*pregpriv = &padapter->registrypriv;
	struct xmit_priv		*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	int	bssrate_len = 0, sta_bssrate_len = 0;
#ifdef CONFIG_P2P
	struct wifidirect_info	*pwdinfo = &(padapter->wdinfo);
	u8					p2pie[ 255 ] = { 0x00 };
	u16					p2pielen = 0;	
#endif //CONFIG_P2P

#ifdef CONFIG_DFS
	u16	cap;
#endif //CONFIG_DFS

	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		return;
	}

	//update attribute
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);


	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;
	_rtw_memcpy(pwlanhdr->addr1, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, myid(&(padapter->eeprompriv)), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_ASSOCREQ);

	pframe += sizeof(struct ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct ieee80211_hdr_3addr);

	//caps
#ifdef CONFIG_DFS
	_rtw_memcpy(&cap, rtw_get_capability_from_ie(pmlmeinfo->network.IEs), 2);
	cap |= BIT(8);
	_rtw_memcpy(pframe, &cap, 2);
#else
	_rtw_memcpy(pframe, rtw_get_capability_from_ie(pmlmeinfo->network.IEs), 2);
#endif //CONFIG_DFS

	pframe += 2;
	pattrib->pktlen += 2;

	//listen interval
	//todo: listen interval for power saving
	val16 = cpu_to_le16(3);
	_rtw_memcpy(pframe ,(unsigned char *)&val16, 2);
	pframe += 2;
	pattrib->pktlen += 2;

	//SSID
	pframe = rtw_set_ie(pframe, _SSID_IE_,  pmlmeinfo->network.Ssid.SsidLength, pmlmeinfo->network.Ssid.Ssid, &(pattrib->pktlen));

	//supported rate & extended supported rate
#if 1	// Check if the AP's supported rates are also supported by STA.
	get_rate_set(padapter, sta_bssrate, &sta_bssrate_len);
	//DBG_871X("sta_bssrate_len=%d\n", sta_bssrate_len);
				
	//for (i = 0; i < sta_bssrate_len; i++) {
	//	DBG_871X("sta_bssrate[%d]=%02X\n", i, sta_bssrate[i]);
	//}
	for (i = 0; i < NDIS_802_11_LENGTH_RATES_EX; i++) {
		if (pmlmeinfo->network.SupportedRates[i] == 0) break;
			DBG_871X("network.SupportedRates[%d]=%02X\n", i, pmlmeinfo->network.SupportedRates[i]);
	}
											
	for (i = 0; i < NDIS_802_11_LENGTH_RATES_EX; i++) {
		if (pmlmeinfo->network.SupportedRates[i] == 0) break;
						
		// Check if the AP's supported rates are also supported by STA.
		for (j=0; j < sta_bssrate_len; j++) {
		// Avoid the proprietary data rate (22Mbps) of Handlink WSG-4000 AP
			if ( (pmlmeinfo->network.SupportedRates[i]|IEEE80211_BASIC_RATE_MASK) 
				== (sta_bssrate[j]|IEEE80211_BASIC_RATE_MASK)) {
		//DBG_871X("match i = %d, j=%d\n", i, j);
				break;
			} else {
		//DBG_871X("not match: %02X != %02X\n", (pmlmeinfo->network.SupportedRates[i]|IEEE80211_BASIC_RATE_MASK), (sta_bssrate[j]|IEEE80211_BASIC_RATE_MASK));
			}
		}
		if (j == sta_bssrate_len) {
			// the rate is not supported by STA
			DBG_871X("%s(): the rate[%d]=%02X is not supported by STA!\n",__FUNCTION__, i, pmlmeinfo->network.SupportedRates[i]);
		} else {
		// the rate is supported by STA
			bssrate[index++] = pmlmeinfo->network.SupportedRates[i];
		}
	}
	bssrate_len = index;
	DBG_871X("bssrate_len = %d\n", bssrate_len);
#else	// Check if the AP's supported rates are also supported by STA.
#if 0
	get_rate_set(padapter, bssrate, &bssrate_len);
#else
	for (bssrate_len = 0; bssrate_len < NumRates; bssrate_len++) {
		if (pmlmeinfo->network.SupportedRates[bssrate_len] == 0) break;
		
		if (pmlmeinfo->network.SupportedRates[bssrate_len] == 0x2C) // Avoid the proprietary data rate (22Mbps) of Handlink WSG-4000 AP
			break;
		
		bssrate[bssrate_len] = pmlmeinfo->network.SupportedRates[bssrate_len];
	}
#endif
#endif	// Check if the AP's supported rates are also supported by STA.
	if (bssrate_len > 8)
	{
		pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_ , 8, bssrate, &(pattrib->pktlen));
		pframe = rtw_set_ie(pframe, _EXT_SUPPORTEDRATES_IE_ , (bssrate_len - 8), (bssrate + 8), &(pattrib->pktlen));
	}
	else
	{
		pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_ , bssrate_len , bssrate, &(pattrib->pktlen));
	}

	//RSN
	p = rtw_get_ie((pmlmeinfo->network.IEs + sizeof(NDIS_802_11_FIXED_IEs)), _RSN_IE_2_, &ie_len, (pmlmeinfo->network.IELength - sizeof(NDIS_802_11_FIXED_IEs)));
	if (p != NULL)
	{
		pframe = rtw_set_ie(pframe, _RSN_IE_2_, ie_len, (p + 2), &(pattrib->pktlen));
	}

#ifdef CONFIG_80211N_HT
	//HT caps
	if(padapter->mlmepriv.htpriv.ht_option==_TRUE)
	{
		p = rtw_get_ie((pmlmeinfo->network.IEs + sizeof(NDIS_802_11_FIXED_IEs)), _HT_CAPABILITY_IE_, &ie_len, (pmlmeinfo->network.IELength - sizeof(NDIS_802_11_FIXED_IEs)));
		if ((p != NULL) && (!(is_ap_in_tkip(padapter))))
		{
			_rtw_memcpy(&(pmlmeinfo->HT_caps), (p + 2), sizeof(struct HT_caps_element));

			//to disable 40M Hz support while gd_bw_40MHz_en = 0
			if (pregpriv->cbw40_enable == 0)
			{
				pmlmeinfo->HT_caps.HT_cap_element.HT_caps_info &= (~(BIT(6) | BIT(1)));
			}
			else
			{
				pmlmeinfo->HT_caps.HT_cap_element.HT_caps_info |= BIT(1);
			}

			//todo: disable SM power save mode
			pmlmeinfo->HT_caps.HT_cap_element.HT_caps_info |= 0x000c;

			padapter->HalFunc.GetHwRegHandler(padapter, HW_VAR_RF_TYPE, (u8 *)(&rf_type));
			//switch (pregpriv->rf_config)
			switch(rf_type)
			{
				case RF_1T1R:
					
                    //if(pregpriv->rx_stbc)
					       //pmlmeinfo->HT_caps.HT_cap_element.HT_caps_info |= cpu_to_le16(0x0100);//RX STBC One spatial stream
					
                    _rtw_memcpy(pmlmeinfo->HT_caps.HT_cap_element.MCS_rate, MCS_rate_1R, 16);
					break;

				case RF_2T2R:
				case RF_1T2R:
				default:
					
					if(pregpriv->rx_stbc)	
						pmlmeinfo->HT_caps.HT_cap_element.HT_caps_info |= cpu_to_le16(0x0100);//RX STBC One spatial stream

					_rtw_memcpy(pmlmeinfo->HT_caps.HT_cap_element.MCS_rate, MCS_rate_2R, 16);
					break;
			}
			#ifdef RTL8192C_RECONFIG_TO_1T1R
			{
				//if(pregpriv->rx_stbc)
					//pmlmeinfo->HT_caps.u.HT_cap_element.HT_caps_info |= cpu_to_le16(0x0100);//RX STBC One spatial stream
			
				_rtw_memcpy(pmlmeinfo->HT_caps.u.HT_cap_element.MCS_rate, MCS_rate_1R, 16);
			}
			#endif
			pmlmeinfo->HT_caps.HT_cap_element.HT_caps_info = cpu_to_le16(pmlmeinfo->HT_caps.HT_cap_element.HT_caps_info);
			pframe = rtw_set_ie(pframe, _HT_CAPABILITY_IE_, ie_len , (u8 *)(&(pmlmeinfo->HT_caps)), &(pattrib->pktlen));

		}
	}
#endif

	//vendor specific IE, such as WPA, WMM, WPS
	for (i = sizeof(NDIS_802_11_FIXED_IEs); i < pmlmeinfo->network.IELength;)
	{
		pIE = (PNDIS_802_11_VARIABLE_IEs)(pmlmeinfo->network.IEs + i);

		switch (pIE->ElementID)
		{
			case _VENDOR_SPECIFIC_IE_:
				if ((_rtw_memcmp(pIE->data, WPA_OUI, 4)) ||
						(_rtw_memcmp(pIE->data, WMM_OUI, 4)) ||
						(_rtw_memcmp(pIE->data, WPS_OUI, 4)))
				{
					//Commented by Kurt 20110629
					//In some older APs, WPS handshake
					//would be fail if we append vender extensions informations to AP
					if(_rtw_memcmp(pIE->data, WPS_OUI, 4)){
						pIE->Length=14;
					}
					
					pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, pIE->Length, pIE->data, &(pattrib->pktlen));
				}
				break;

			default:
				break;
		}

		i += (pIE->Length + 2);
	}

	if (pmlmeinfo->assoc_AP_vendor == realtekAP)
	{
		pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, 6 , REALTEK_96B_IE, &(pattrib->pktlen));
	}

#ifdef CONFIG_P2P
	if ( ( pwdinfo->p2p_state != P2P_STATE_NONE ) && ( pwdinfo->p2p_state != P2P_STATE_IDLE ) )
	{
		//	Should add the P2P IE in the association request frame.	
		//	P2P OUI
		
		p2pielen = 0;
		p2pie[ p2pielen++ ] = 0x50;
		p2pie[ p2pielen++ ] = 0x6F;
		p2pie[ p2pielen++ ] = 0x9A;
		p2pie[ p2pielen++ ] = 0x09;	//	WFA P2P v1.0

		//	Commented by Albert 20101109
		//	According to the P2P Specification, the association request frame should contain 3 P2P attributes
		//	1. P2P Capability
		//	2. Extended Listen Timing
		//	3. Device Info
		//	Commented by Albert 20110516
		//	4. P2P Interface

		//	P2P Capability
		//	Type:
		p2pie[ p2pielen++ ] = P2P_ATTR_CAPABILITY;

		//	Length:
		*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x0002 );
		p2pielen += 2;

		//	Value:
		//	Device Capability Bitmap, 1 byte
		//	Be able to participate in additional P2P Groups and
		//	support the P2P Invitation Procedure
		p2pie[ p2pielen++ ] = P2P_DEVCAP_INVITATION_PROC;
	
		//	Group Capability Bitmap, 1 byte
		p2pie[ p2pielen++ ] = 0x00;

		//	Extended Listen Timing
		//	Type:
		p2pie[ p2pielen++ ] = P2P_ATTR_EX_LISTEN_TIMING;

		//	Length:
		*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x0004 );
		p2pielen += 2;

		//	Value:
		//	Availability Period
		*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0xFFFF );
		p2pielen += 2;

		//	Availability Interval
		*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0xFFFF );
		p2pielen += 2;

		//	Device Info
		//	Type:
		p2pie[ p2pielen++ ] = P2P_ATTR_DEVICE_INFO;

		//	Length:
		//	21 -> P2P Device Address (6bytes) + Config Methods (2bytes) + Primary Device Type (8bytes) 
		//	+ NumofSecondDevType (1byte) + WPS Device Name ID field (2bytes) + WPS Device Name Len field (2bytes)
		*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 21 + pwdinfo->device_name_len );
		p2pielen += 2;

		//	Value:
		//	P2P Device Address
		_rtw_memcpy( p2pie + p2pielen, myid( &padapter->eeprompriv ), ETH_ALEN );
		p2pielen += ETH_ALEN;

		//	Config Method
		//	This field should be big endian. Noted by P2P specification.
		if ( ( pwdinfo->ui_got_wps_info == P2P_GOT_WPSINFO_PEER_DISPLAY_PIN ) ||
			( pwdinfo->ui_got_wps_info == P2P_GOT_WPSINFO_SELF_DISPLAY_PIN ) )
		{
			*(u16*) ( p2pie + p2pielen ) = cpu_to_be16( WPS_CONFIG_METHOD_DISPLAY );
		}
		else
		{
			*(u16*) ( p2pie + p2pielen ) = cpu_to_be16( WPS_CONFIG_METHOD_PBC );
		}

		p2pielen += 2;

		//	Primary Device Type
		//	Category ID
		*(u16*) ( p2pie + p2pielen ) = cpu_to_be16( WPS_PDT_CID_RTK_WIDI );
		p2pielen += 2;

		//	OUI
		*(u32*) ( p2pie + p2pielen ) = cpu_to_be32( WPSOUI );
		p2pielen += 4;

		//	Sub Category ID
		*(u16*) ( p2pie + p2pielen ) = cpu_to_be16( WPS_PDT_SCID_RTK_DMP );
		p2pielen += 2;

		//	Number of Secondary Device Types
		p2pie[ p2pielen++ ] = 0x00;	//	No Secondary Device Type List

		//	Device Name
		//	Type:
		*(u16*) ( p2pie + p2pielen ) = cpu_to_be16( WPS_ATTR_DEVICE_NAME );
		p2pielen += 2;

		//	Length:
		*(u16*) ( p2pie + p2pielen ) = cpu_to_be16( pwdinfo->device_name_len );
		p2pielen += 2;

		//	Value:
		_rtw_memcpy( p2pie + p2pielen, pwdinfo->device_name, pwdinfo->device_name_len );
		p2pielen += pwdinfo->device_name_len;
	
		//	P2P Interface
		//	Type:
		p2pie[ p2pielen++ ] = P2P_ATTR_INTERFACE;
		
		//	Length:
		*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x000D );
		p2pielen += 2;
		
		//	Value:
		_rtw_memcpy( p2pie + p2pielen, pwdinfo->device_addr, ETH_ALEN );	//	P2P Device Address
		p2pielen += ETH_ALEN;

		p2pie[ p2pielen++ ] = 1;	//	P2P Interface Address Count
		
		_rtw_memcpy( p2pie + p2pielen, pwdinfo->device_addr, ETH_ALEN );	//	P2P Interface Address List
		p2pielen += ETH_ALEN;
	
		pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, p2pielen, (unsigned char *) p2pie, &pattrib->pktlen );
		
	}
#endif //CONFIG_P2P

	pattrib->last_txcmdsz = pattrib->pktlen;
	dump_mgntframe(padapter, pmgntframe);

	return;
}

void issue_nulldata(_adapter *padapter, unsigned int power_mode)
{
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	unsigned char					*pframe;
	struct ieee80211_hdr	*pwlanhdr;
	unsigned short				*fctrl;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	//DBG_871X("%s:%d\n", __FUNCTION__, power_mode);

	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		return;
	}

	//update attribute
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);

	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	if((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE)
	{
		SetFrDs(fctrl);
	}
	else if((pmlmeinfo->state&0x03) == WIFI_FW_STATION_STATE)
	{
		SetToDs(fctrl);
	}
	
	if (power_mode)
	{
		SetPwrMgt(fctrl);
	}

	_rtw_memcpy(pwlanhdr->addr1, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, myid(&(padapter->eeprompriv)), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_DATA_NULL);

	pframe += sizeof(struct ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct ieee80211_hdr_3addr);

	pattrib->last_txcmdsz = pattrib->pktlen;
	dump_mgntframe(padapter, pmgntframe);

	return;
}


void issue_qos_nulldata(_adapter *padapter, unsigned char *da, u16 tid)
{
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	unsigned char					*pframe;
	struct ieee80211_hdr	*pwlanhdr;
	unsigned short				*fctrl, *qc;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	DBG_871X("%s\n", __FUNCTION__);

	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		return;
	}

	//update attribute
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);

	pattrib->hdrlen +=2;
	pattrib->qos_en = _TRUE;
	pattrib->eosp = 1;
	pattrib->ack_policy = 0;
	pattrib->mdata = 0;

	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	if((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE)
	{
		SetFrDs(fctrl);
	}
	else if((pmlmeinfo->state&0x03) == WIFI_FW_STATION_STATE)
	{
		SetToDs(fctrl);
	}

	if(pattrib->mdata)
		SetMData(fctrl);

	qc = (unsigned short *)(pframe + pattrib->hdrlen - 2);
	
	SetPriority(qc, tid);

	SetEOSP(qc, pattrib->eosp);

	SetAckpolicy(qc, pattrib->ack_policy);

	_rtw_memcpy(pwlanhdr->addr1, da, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, myid(&(padapter->eeprompriv)), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_QOS_DATA_NULL);

	pframe += sizeof(struct ieee80211_hdr_3addr_qos);
	pattrib->pktlen = sizeof(struct ieee80211_hdr_3addr_qos);

	pattrib->last_txcmdsz = pattrib->pktlen;
	dump_mgntframe(padapter, pmgntframe);
	
}

void issue_deauth(_adapter *padapter, unsigned char *da, unsigned short reason)
{
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	unsigned char					*pframe;
	struct ieee80211_hdr	*pwlanhdr;
	unsigned short				*fctrl;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		return;
	}

	//update attribute
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);

	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	_rtw_memcpy(pwlanhdr->addr1, da, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, myid(&(padapter->eeprompriv)), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_DEAUTH);

	pframe += sizeof(struct ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct ieee80211_hdr_3addr);

	reason = cpu_to_le16(reason);
	pframe = rtw_set_fixed_ie(pframe, _RSON_CODE_ , (unsigned char *)&reason, &(pattrib->pktlen));

	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe(padapter, pmgntframe);
}

void issue_action_BA(_adapter *padapter, unsigned char *raddr, unsigned char action, unsigned short status)
{
	u8	category = WLAN_CATEGORY_BACK;
	u16	start_seq;
	u16	BA_para_set;
	u16	reason_code;
	u16	BA_timeout_value;
	u16	BA_starting_seqctrl;
	struct xmit_frame		*pmgntframe;
	struct pkt_attrib		*pattrib;
	u8					*pframe;
	struct ieee80211_hdr	*pwlanhdr;
	u16					*fctrl;
	struct xmit_priv		*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct sta_info		*psta;
	struct sta_priv		*pstapriv = &padapter->stapriv;
	struct registry_priv	 	*pregpriv = &padapter->registrypriv;


	DBG_871X("%s, category=%d, action=%d, status=%d\n", __FUNCTION__, category, action, status);

	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		return;
	}

	//update attribute
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);

	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	//_rtw_memcpy(pwlanhdr->addr1, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr1, raddr, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, myid(&(padapter->eeprompriv)), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_ACTION);

	pframe += sizeof(struct ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct ieee80211_hdr_3addr);

	pframe = rtw_set_fixed_ie(pframe, 1, &(category), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(action), &(pattrib->pktlen));

      status = cpu_to_le16(status);
	

	if (category == 3)
	{
		switch (action)
		{
			case 0: //ADDBA req
				do {
					pmlmeinfo->dialogToken++;
				} while (pmlmeinfo->dialogToken == 0);
				pframe = rtw_set_fixed_ie(pframe, 1, &(pmlmeinfo->dialogToken), &(pattrib->pktlen));

				BA_para_set = (0x1002 | ((status & 0xf) << 2)); //immediate ack & 64 buffer size
				//sys_mib.BA_para_set = 0x0802; //immediate ack & 32 buffer size
				BA_para_set = cpu_to_le16(BA_para_set);
				pframe = rtw_set_fixed_ie(pframe, 2, (unsigned char *)(&(BA_para_set)), &(pattrib->pktlen));

				//BA_timeout_value = 0xffff;//max: 65535 TUs(~ 65 ms)
				BA_timeout_value = 5000;//~ 5ms
				BA_timeout_value = cpu_to_le16(BA_timeout_value);
				pframe = rtw_set_fixed_ie(pframe, 2, (unsigned char *)(&(BA_timeout_value)), &(pattrib->pktlen));

				//if ((psta = rtw_get_stainfo(pstapriv, pmlmeinfo->network.MacAddress)) != NULL)
				if ((psta = rtw_get_stainfo(pstapriv, raddr)) != NULL)
				{
					start_seq = (psta->sta_xmitpriv.txseq_tid[status & 0x07]&0xfff) + 1;

					DBG_871X("BA_starting_seqctrl = %d for TID=%d\n", start_seq, status & 0x07);
					
					psta->BA_starting_seqctrl[status & 0x07] = start_seq;
					
					BA_starting_seqctrl = start_seq << 4;
				}
				
				BA_starting_seqctrl = cpu_to_le16(BA_starting_seqctrl);
				pframe = rtw_set_fixed_ie(pframe, 2, (unsigned char *)(&(BA_starting_seqctrl)), &(pattrib->pktlen));
				break;

			case 1: //ADDBA rsp
				pframe = rtw_set_fixed_ie(pframe, 1, &(pmlmeinfo->ADDBA_req.dialog_token), &(pattrib->pktlen));
				pframe = rtw_set_fixed_ie(pframe, 2, (unsigned char *)(&status), &(pattrib->pktlen));

				//BA_para_set = cpu_to_le16((le16_to_cpu(pmlmeinfo->ADDBA_req.BA_para_set) & 0x3f) | 0x1000); //64 buffer size
				BA_para_set = ((le16_to_cpu(pmlmeinfo->ADDBA_req.BA_para_set) & 0x3f) | 0x1000); //64 buffer size

				if(pregpriv->ampdu_amsdu==0)//disabled
					BA_para_set = cpu_to_le16(BA_para_set & ~BIT(0));
				else if(pregpriv->ampdu_amsdu==1)//enabled
					BA_para_set = cpu_to_le16(BA_para_set | BIT(0));
				else //auto
					BA_para_set = cpu_to_le16(BA_para_set);
				
				pframe = rtw_set_fixed_ie(pframe, 2, (unsigned char *)(&(BA_para_set)), &(pattrib->pktlen));
				pframe = rtw_set_fixed_ie(pframe, 2, (unsigned char *)(&(pmlmeinfo->ADDBA_req.BA_timeout_value)), &(pattrib->pktlen));
				break;
			case 2://DELBA
				BA_para_set = (status & 0x1F) << 3;
				BA_para_set = cpu_to_le16(BA_para_set);				
				pframe = rtw_set_fixed_ie(pframe, 2, (unsigned char *)(&(BA_para_set)), &(pattrib->pktlen));

				reason_code = 37;//Requested from peer STA as it does not want to use the mechanism
				reason_code = cpu_to_le16(reason_code);
				pframe = rtw_set_fixed_ie(pframe, 2, (unsigned char *)(&(reason_code)), &(pattrib->pktlen));
				break;
			default:
				break;
		}
	}

	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe(padapter, pmgntframe);
}

static void issue_action_BSSCoexistPacket(_adapter *padapter)
{	
	_irqL	irqL;
	_list		*plist, *phead;
	unsigned char category, action;
	struct xmit_frame			*pmgntframe;
	struct pkt_attrib			*pattrib;
	unsigned char				*pframe;
	struct ieee80211_hdr	*pwlanhdr;
	unsigned short			*fctrl;
	struct	wlan_network	*pnetwork = NULL;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	_queue		*queue	= &(pmlmepriv->scanned_queue);
	u8 InfoContent[16] = {0};
	u8 ICS[8][15];
	
	if((pmlmepriv->num_FortyMHzIntolerant==0) || (pmlmepriv->num_sta_no_ht==0))
		return;

	if(_TRUE == pmlmeinfo->bwmode_updated)
		return;
	

	DBG_871X("%s\n", __FUNCTION__);


	category = WLAN_CATEGORY_PUBLIC;
	action = ACT_PUBLIC_BSSCOEXIST;

	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		return;
	}

	//update attribute
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);

	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	_rtw_memcpy(pwlanhdr->addr1, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, myid(&(padapter->eeprompriv)), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);

	SetSeqNum(pwlanhdr, pmlmeext->mgnt_seq);
	pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_ACTION);

	pframe += sizeof(struct ieee80211_hdr_3addr);
	pattrib->pktlen = sizeof(struct ieee80211_hdr_3addr);

	pframe = rtw_set_fixed_ie(pframe, 1, &(category), &(pattrib->pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(action), &(pattrib->pktlen));


	//
	if(pmlmepriv->num_FortyMHzIntolerant>0)
	{
		u8 iedata=0;
		
		iedata |= BIT(2);//20 MHz BSS Width Request

		pframe = rtw_set_ie(pframe, EID_BSSCoexistence,  1, &iedata, &(pattrib->pktlen));
		
	}
	

	//
	_rtw_memset(ICS, 0, sizeof(ICS));
	if(pmlmepriv->num_sta_no_ht>0)
	{	
		int i;
	
		_enter_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);

		phead = get_list_head(queue);
		plist = get_next(phead);
       
		while(1)
		{
			int len;
			u8 *p;
			WLAN_BSSID_EX *pbss_network;
	
			if (rtw_end_of_queue_search(phead,plist)== _TRUE)
				break;		

			pnetwork = LIST_CONTAINOR(plist, struct wlan_network, list);      
		
			plist = get_next(plist);

			pbss_network = (WLAN_BSSID_EX *)&pnetwork->network;

			p = rtw_get_ie(pbss_network->IEs + _FIXED_IE_LENGTH_, _HT_CAPABILITY_IE_, &len, pbss_network->IELength - _FIXED_IE_LENGTH_);
			if((p==NULL) || (len==0))//non-HT
			{
				if((pbss_network->Configuration.DSConfig<=0) || (pbss_network->Configuration.DSConfig>14))
					continue;
				
				ICS[0][pbss_network->Configuration.DSConfig]=1;
				
				if(ICS[0][0] == 0)
					ICS[0][0] = 1;		
			}		
	
		}        

		_exit_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);


		for(i= 0;i<8;i++)
		{
			if(ICS[i][0] == 1)
			{
				int j, k = 0;
				
				InfoContent[k] = i;				
				//SET_BSS_INTOLERANT_ELE_REG_CLASS(InfoContent,i);
				k++;
				
				for(j=1;j<=14;j++)
				{
					if(ICS[i][j]==1)
					{
						if(k<16)
						{
							InfoContent[k] = j; //channel number
							//SET_BSS_INTOLERANT_ELE_CHANNEL(InfoContent+k, j);
							k++;
						}	
					}	
				}	

				pframe = rtw_set_ie(pframe, EID_BSSIntolerantChlReport, k, InfoContent, &(pattrib->pktlen));
				
			}
			
		}
		

	}
		

	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe(padapter, pmgntframe);

}

unsigned int send_delba(_adapter *padapter, u8 initiator, u8 *addr)
{
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct sta_info *psta = NULL;
	//struct recv_reorder_ctrl *preorder_ctrl;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	u16 tid;

	if((pmlmeinfo->state&0x03) != WIFI_FW_AP_STATE)	
		if (!(pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS))
			return _SUCCESS;
	
	psta = rtw_get_stainfo(pstapriv, addr);
	if(psta==NULL)
		return _SUCCESS;

	//DBG_8192C("%s:%s\n", __FUNCTION__, (initiator==0)?"RX_DIR":"TX_DIR");
	
	if(initiator==0) // recipient
	{
		for(tid = 0;tid<MAXTID;tid++)
		{
			if(psta->recvreorder_ctrl[tid].enable == _TRUE)
			{
				DBG_8192C("rx agg disable tid(%d)\n",tid);
				issue_action_BA(padapter, addr, WLAN_ACTION_DELBA, (((tid <<1) |initiator)&0x1F));
				psta->recvreorder_ctrl[tid].enable = _FALSE;
				psta->recvreorder_ctrl[tid].indicate_seq = 0xffff;
				#ifdef DBG_RX_SEQ
				DBG_871X("DBG_RX_SEQ %s:%d indicate_seq:%u \n", __FUNCTION__, __LINE__,
					psta->recvreorder_ctrl[tid].indicate_seq);
				#endif
			}		
		}
	}
	else if(initiator == 1)// originator
	{
		//DBG_8192C("tx agg_enable_bitmap(0x%08x)\n", psta->htpriv.agg_enable_bitmap);
		for(tid = 0;tid<MAXTID;tid++)
		{
			if(psta->htpriv.agg_enable_bitmap & BIT(tid))
			{
				DBG_8192C("tx agg disable tid(%d)\n",tid);
				issue_action_BA(padapter, addr, WLAN_ACTION_DELBA, (((tid <<1) |initiator)&0x1F) );
				psta->htpriv.agg_enable_bitmap &= ~BIT(tid);
				psta->htpriv.candidate_tid_bitmap &= ~BIT(tid);
				
			}			
		}
	}
	
	return _SUCCESS;
	
}

unsigned int send_beacon(_adapter *padapter)
{
	u8	bxmitok = _FALSE;
	int	retry=0;

#ifdef CONFIG_PCI_HCI

	//DBG_871X("%s\n", __FUNCTION__);

	issue_beacon(padapter);

	return _SUCCESS;

#endif

#ifdef CONFIG_USB_HCI
	do{

		issue_beacon(padapter);

		padapter->HalFunc.GetHwRegHandler(padapter, HW_VAR_TX_BCN_DONE, (u8 *)(&bxmitok));

	}while((_FALSE == bxmitok) &&((retry++)<100 ));

	if(retry == 100)
	{
		DBG_871X("send_beacon, fail!\n");
		return _FAIL;
	}
	else
	{
		return _SUCCESS;
	}
#endif	
	
}

/****************************************************************************

Following are some utitity fuctions for WiFi MLME

*****************************************************************************/

BOOLEAN IsLegal5GChannel(
	IN PADAPTER			Adapter,
	IN u8			channel)
{
	
	int i=0;
	u8 Channel_5G[45] = {36,38,40,42,44,46,48,50,52,54,56,58,
		60,62,64,100,102,104,106,108,110,112,114,116,118,120,122,
		124,126,128,130,132,134,136,138,140,149,151,153,155,157,159,
		161,163,165};
	for(i=0;i<sizeof(Channel_5G);i++)
		if(channel == Channel_5G[i])
			return _TRUE;
	return _FALSE;
}

void site_survey(_adapter *padapter)
{
	unsigned char		survey_channel, val8;
	RT_SCAN_TYPE	ScanType;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
#ifdef CONFIG_P2P
	struct wifidirect_info *pwdinfo= &(padapter->wdinfo);
	static unsigned char  prev_survey_channel = 0;
	static unsigned int p2p_scan_count = 0;
#endif //CONFIG_P2P
	u32 initialgain = 0;


#ifdef CONFIG_TDLS
	u32 v, bit_6=1<<6;
	if(pmlmeinfo->tdls_dis_req==1){ 
		SelectChannel(padapter, pmlmeext->cur_channel);

		val8 = 0;
		padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_MLME_SITESURVEY, (u8 *)(&val8));

		//when we already set at a direct link and also want to dicovery TDLS STAs
		//it is used to avoiding RCR DATA BIT on
		v = rtw_read32(padapter, 0x0608);
		v &= ~(bit_6);
		rtw_write32(padapter, 0x0608, v);

		issue_tdls_dis_req(padapter);

		set_survey_timer(pmlmeext, 300);

		pmlmeinfo->tdls_dis_req=2;
		return;
	}
	else if(pmlmeinfo->tdls_dis_req==2){
		pmlmeinfo->tdls_dis_req=3;

		//config MSR
		Set_NETYPE0_MSR(padapter, (pmlmeinfo->state & 0x3));

		initialgain = 0xff; //restore RX GAIN
		padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_INITIAL_GAIN, (u8 *)(&initialgain));	
		
		//turn on dynamic functions
		Restore_DM_Func_Flag(padapter);

		report_surveydone_event(padapter);
		
		pmlmeext->chan_scan_time = SURVEY_TO;
		pmlmeext->sitesurvey_res.state = SCAN_DISABLE;
		return;
	}
#endif

#ifdef CONFIG_P2P
	survey_channel = 0;
	ScanType = SCAN_PASSIVE;
	
	if ( pwdinfo->p2p_state == P2P_STATE_FIND_PHASE_SEARCH )
	{
		if ( pwdinfo->find_phase_state_exchange_cnt != 0 )
		{
			//	Commented by Albert 2011/06/03
			//	The driver is in the find phase, it should go through the social channel.
			survey_channel = pwdinfo->social_chan[pmlmeext->sitesurvey_res.channel_idx];
			ScanType = SCAN_ACTIVE;
		}
		else
		{
			//	Commented by Albert 2011/06/03
			//	The driver is in the scan phase, it should go through all the channel.
			survey_channel = pmlmeext->channel_set[pmlmeext->sitesurvey_res.channel_idx].ChannelNum;
			ScanType = pmlmeext->channel_set[pmlmeext->sitesurvey_res.channel_idx].ScanType;
		}
	}
	else if ( ( P2P_STATE_NONE == pwdinfo->p2p_state ) || ( P2P_STATE_IDLE == pwdinfo->p2p_state ) )
	{
		//	Commented by Albert 20110805
		//	The following code will be executed only when the P2P is disable.
		survey_channel = pmlmeext->channel_set[pmlmeext->sitesurvey_res.channel_idx].ChannelNum;
		ScanType = pmlmeext->channel_set[pmlmeext->sitesurvey_res.channel_idx].ScanType;
	}
#else
	{
		survey_channel = pmlmeext->channel_set[pmlmeext->sitesurvey_res.channel_idx].ChannelNum;
		ScanType = pmlmeext->channel_set[pmlmeext->sitesurvey_res.channel_idx].ScanType;
	}
#endif //CONFIG_P2P

	if(survey_channel != 0)
	{

		//DBG_871X("switching to channel:%d at %dms\n", 
		//	survey_channel, rtw_get_passing_time_ms(padapter->mlmepriv.scan_start_time)
		//);
		//PAUSE 4-AC Queue when site_survey
		//padapter->HalFunc.GetHwRegHandler(padapter, HW_VAR_TXPAUSE, (u8 *)(&val8));
		//val8 |= 0x0f;
		//padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_TXPAUSE, (u8 *)(&val8));

		if(pmlmeext->sitesurvey_res.channel_idx == 0)
		{
			set_channel_bwmode(padapter, survey_channel, HAL_PRIME_CHNL_OFFSET_DONT_CARE, HT_CHANNEL_WIDTH_20);
		}
		else
		{
			SelectChannel(padapter, survey_channel);
		}

#ifdef CONFIG_TDLS
		if(pmlmeinfo->tdls_ch_sensing==1)
			pmlmeinfo->tdls_cur_channel =survey_channel;
#endif

		//DBG_871X("%s scan_mode:%d, ScanType:%d\n", __FUNCTION__, pmlmeext->sitesurvey_res.scan_mode, ScanType);
		if((pmlmeext->sitesurvey_res.scan_mode == SCAN_ACTIVE) && (ScanType == SCAN_ACTIVE))
		{
		
#ifdef CONFIG_P2P
			if ( ( pwdinfo->p2p_state == P2P_STATE_SCAN ) || 
				( pwdinfo->p2p_state == P2P_STATE_FIND_PHASE_SEARCH )
			   )
			{
				issue_probereq_p2p(padapter);
				issue_probereq_p2p(padapter);
				issue_probereq_p2p(padapter);
			}
			else
#endif //CONFIG_P2P
			{
				//todo: to issue two probe req???
				issue_probereq(padapter, 1);
				//rtw_msleep_os(SURVEY_TO>>1);
				issue_probereq(padapter, 1);
			}
		}

		set_survey_timer(pmlmeext, pmlmeext->chan_scan_time);

	}
	else
	{

		//	channel number is 0 or this channel is not valid.
#ifdef CONFIG_P2P
		if ( ( pwdinfo->p2p_state == P2P_STATE_SCAN ) || ( pwdinfo->p2p_state == P2P_STATE_FIND_PHASE_SEARCH ) )
		{
			DBG_8192C( "[%s] find phase exchange cnt = %d\n", __FUNCTION__, pwdinfo->find_phase_state_exchange_cnt );
		}
		
		if ( ( ( pwdinfo->p2p_state == P2P_STATE_SCAN ) || ( pwdinfo->p2p_state == P2P_STATE_FIND_PHASE_SEARCH ) ) && 
			( pwdinfo->find_phase_state_exchange_cnt < P2P_FINDPHASE_EX_CNT ) )
		{
			//	Set the P2P State to the listen state of find phase and set the current channel to the listen channel
			pwdinfo->p2p_state = P2P_STATE_FIND_PHASE_LISTEN;
			pmlmeext->sitesurvey_res.state = SCAN_DISABLE;
			set_channel_bwmode(padapter, pwdinfo->listen_channel, HAL_PRIME_CHNL_OFFSET_DONT_CARE, HT_CHANNEL_WIDTH_20);
			_set_timer( &pwdinfo->find_phase_timer, ( u32 ) ( ( u32 ) ( pwdinfo->listen_dwell ) * 100 ) );
		}
		else
#endif //CONFIG_P2P

		{

#ifdef CONFIG_ANTENNA_DIVERSITY
			// 20100721:Interrupt scan operation here.
			// For SW antenna diversity before link, it needs to switch to another antenna and scan again.
			// It compares the scan result and select beter one to do connection.
			if(padapter->HalFunc.SwAntDivBeforeLinkHandler(padapter))
			{				
				pmlmeext->sitesurvey_res.bss_cnt = 0;
				pmlmeext->sitesurvey_res.channel_idx = -1;
				pmlmeext->chan_scan_time = SURVEY_TO /2;			
				set_survey_timer(pmlmeext, pmlmeext->chan_scan_time);
				return;
			}
#endif

#ifdef CONFIG_P2P
			if ( ( pwdinfo->p2p_state == P2P_STATE_SCAN ) || ( pwdinfo->p2p_state == P2P_STATE_FIND_PHASE_SEARCH ) )
			{
				pwdinfo->p2p_state = P2P_STATE_LISTEN;
			}
#endif //CONFIG_P2P
			
			pmlmeext->sitesurvey_res.state = SCAN_COMPLETE;

			//switch back to the original channel
			//SelectChannel(padapter, pmlmeext->cur_channel, pmlmeext->cur_ch_offset);

#ifdef CONFIG_P2P
			if ( pwdinfo->p2p_state == P2P_STATE_GONEGO_OK )
			{
				DBG_8192C( "[%s] In P2P WPS mode, stay in the peer operating channel = %d\n", __FUNCTION__,  pwdinfo->peer_operating_ch );
				set_channel_bwmode(padapter, pwdinfo->peer_operating_ch, pmlmeext->cur_ch_offset, pmlmeext->cur_bwmode);
			}
			else
#endif //CONFIG_P2P
			{
				set_channel_bwmode(padapter, pmlmeext->cur_channel, pmlmeext->cur_ch_offset, pmlmeext->cur_bwmode);
			}

			//flush 4-AC Queue after site_survey
			//val8 = 0;
			//padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_TXPAUSE, (u8 *)(&val8));

			val8 = 0;
			padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_MLME_SITESURVEY, (u8 *)(&val8));

			//config MSR
			Set_NETYPE0_MSR(padapter, (pmlmeinfo->state & 0x3));

			initialgain = 0xff; //restore RX GAIN
			padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_INITIAL_GAIN, (u8 *)(&initialgain));	

			//turn on dynamic functions
			Restore_DM_Func_Flag(padapter);
			//Switch_DM_Func(padapter, DYNAMIC_FUNC_DIG|DYNAMIC_FUNC_HP|DYNAMIC_FUNC_SS, _TRUE);

			if (is_client_associated_to_ap(padapter) == _TRUE)
			{
				//issue null data 
				issue_nulldata(padapter, 0);
			}

#ifdef CONFIG_TDLS
			if(pmlmeinfo->tdls_ch_sensing==1){
				u8 i, min;
				pmlmeinfo->tdls_ch_sensing=0;
				pmlmeinfo->tdls_cur_channel=1;
				min=pmlmeinfo->tdls_collect_pkt_num[0];
				for(i=1; i<14-1; i++){
					if(min > pmlmeinfo->tdls_collect_pkt_num[i]){
						pmlmeinfo->tdls_cur_channel=i+1;
						min=pmlmeinfo->tdls_collect_pkt_num[i];
					}
					pmlmeinfo->tdls_collect_pkt_num[i]=0;
				}
				pmlmeinfo->tdls_collect_pkt_num[0]=0;
				pmlmeinfo->tdls_candidate_ch=pmlmeinfo->tdls_cur_channel;
				DBG_8192C("TDLS channel sensing done, candidate channel: %02x\n", pmlmeinfo->tdls_candidate_ch);
				pmlmeinfo->tdls_cur_channel=0;

				// If we support TDLS, then when we finished site survey,
				// we stil turn RCR_CBSSID_DATA off,
				// such we can receive all kinds of data frames.
				v = rtw_read32(padapter, 0x0608);
				bit_6=1<<6;
				v &= ~(bit_6);
				rtw_write32(padapter, 0x0608, v);
			}
#else
			report_surveydone_event(padapter);
#endif
			pmlmeext->chan_scan_time = SURVEY_TO;
			pmlmeext->sitesurvey_res.state = SCAN_DISABLE;

			issue_action_BSSCoexistPacket(padapter);
			issue_action_BSSCoexistPacket(padapter);
			issue_action_BSSCoexistPacket(padapter);

		}

	}

	return;

}

//collect bss info from Beacon and Probe response frames.
u8 collect_bss_info(_adapter *padapter, union recv_frame *precv_frame, WLAN_BSSID_EX *bssid)
{
	int	i;
	u32	len;
	u8	*p;
	u16	val16, subtype;
	u8	*pframe = precv_frame->u.hdr.rx_data;
	u32	packet_len = precv_frame->u.hdr.len;
	struct registry_priv 	*pregistrypriv = &padapter->registrypriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	len = packet_len - sizeof(struct ieee80211_hdr_3addr);

	if (len > MAX_IE_SZ)
	{
		//DBG_8192C("IE too long for survey event\n");
		return _FAIL;
	}

	_rtw_memset(bssid, 0, sizeof(WLAN_BSSID_EX));

	subtype = GetFrameSubType(pframe) >> 4;

	if(subtype==WIFI_BEACON)
		bssid->Reserved[0] = 1;
		
	bssid->Length = sizeof(WLAN_BSSID_EX) - MAX_IE_SZ + len;

	//below is to copy the information element
	bssid->IELength = len;
	_rtw_memcpy(bssid->IEs, (pframe + sizeof(struct ieee80211_hdr_3addr)), bssid->IELength);

	//get the signal strength
	//bssid->Rssi = precv_frame->u.hdr.attrib.signal_strength; // 0-100 index.
	bssid->Rssi = precv_frame->u.hdr.attrib.RecvSignalPower; // in dBM.raw data	
	bssid->PhyInfo.SignalQuality = precv_frame->u.hdr.attrib.signal_qual;//in percentage 
	bssid->PhyInfo.SignalStrength = precv_frame->u.hdr.attrib.signal_strength;//in percentage
#ifdef CONFIG_ANTENNA_DIVERSITY
	//padapter->HalFunc.GetHwRegHandler(padapter, HW_VAR_CURRENT_ANTENNA, (u8 *)(&bssid->PhyInfo.Optimum_antenna));
	padapter->HalFunc.GetHalDefVarHandler(padapter, HAL_DEF_CURRENT_ANTENNA,  &bssid->PhyInfo.Optimum_antenna);
#endif

	// checking SSID
	if ((p = rtw_get_ie(bssid->IEs + _FIXED_IE_LENGTH_, _SSID_IE_, &len, bssid->IELength - _FIXED_IE_LENGTH_)) == NULL)
	{
		DBG_871X("marc: cannot find SSID for survey event\n");
		return _FAIL;
	}

	if (*(p + 1))
	{
		_rtw_memcpy(bssid->Ssid.Ssid, (p + 2), *(p + 1));
		bssid->Ssid.SsidLength = *(p + 1);
	}
	else
	{
		bssid->Ssid.SsidLength = 0;
	}

	_rtw_memset(bssid->SupportedRates, 0, NDIS_802_11_LENGTH_RATES_EX);

	//checking rate info...
	i = 0;
	p = rtw_get_ie(bssid->IEs + _FIXED_IE_LENGTH_, _SUPPORTEDRATES_IE_, &len, bssid->IELength - _FIXED_IE_LENGTH_);
	if (p != NULL)
	{
		_rtw_memcpy(bssid->SupportedRates, (p + 2), len);
		i = len;
	}

	p = rtw_get_ie(bssid->IEs + _FIXED_IE_LENGTH_, _EXT_SUPPORTEDRATES_IE_, &len, bssid->IELength - _FIXED_IE_LENGTH_);
	if (p != NULL)
	{
		_rtw_memcpy(bssid->SupportedRates + i, (p + 2), len);
	}

	//todo:
#if 0
	if (judge_network_type(bssid->SupportedRates, (len + i)) == WIRELESS_11B)
	{
		bssid->NetworkTypeInUse = Ndis802_11DS;
	}
	else
#endif
	{
		bssid->NetworkTypeInUse = Ndis802_11OFDM24;
	}

	// Checking for DSConfig
	p = rtw_get_ie(bssid->IEs + _FIXED_IE_LENGTH_, _DSSET_IE_, &len, bssid->IELength - _FIXED_IE_LENGTH_);

	bssid->Configuration.DSConfig = 0;
	bssid->Configuration.Length = 0;

	if (p)
	{
		bssid->Configuration.DSConfig = *(p + 2);
	}
	else
	{// In 5G, some ap do not have DSSET IE
		// checking HT info for channel
		p = rtw_get_ie(bssid->IEs + _FIXED_IE_LENGTH_, _HT_ADD_INFO_IE_, &len, bssid->IELength - _FIXED_IE_LENGTH_);
		if(p)
		{
			struct HT_info_element *HT_info = (struct HT_info_element *)(p + 2);
			bssid->Configuration.DSConfig = HT_info->primary_channel;
		}
		else
		{ // use current channel
			if (padapter->mlmeextpriv.sitesurvey_res.state == SCAN_PROCESS)
				bssid->Configuration.DSConfig = padapter->mlmeextpriv.channel_set[padapter->mlmeextpriv.sitesurvey_res.channel_idx].ChannelNum;
			else
				bssid->Configuration.DSConfig = padapter->mlmeextpriv.cur_channel;
		}
	}

	_rtw_memcpy(&bssid->Configuration.BeaconPeriod, rtw_get_beacon_interval_from_ie(bssid->IEs), 2);


	bssid->Configuration.BeaconPeriod = le32_to_cpu(bssid->Configuration.BeaconPeriod);

	val16 = rtw_get_capability((WLAN_BSSID_EX *)bssid);

	if (val16 & BIT(0))
	{
		bssid->InfrastructureMode = Ndis802_11Infrastructure;
		_rtw_memcpy(bssid->MacAddress, GetAddr2Ptr(pframe), ETH_ALEN);
	}
	else
	{
		bssid->InfrastructureMode = Ndis802_11IBSS;
		_rtw_memcpy(bssid->MacAddress, GetAddr3Ptr(pframe), ETH_ALEN);
	}

	if (val16 & BIT(4))
		bssid->Privacy = 1;
	else
		bssid->Privacy = 0;

	bssid->Configuration.ATIMWindow = 0;

	//20/40 BSS Coexistence check
	if((pregistrypriv->wifi_spec==1) && (_FALSE == pmlmeinfo->bwmode_updated))
	{	
		struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
		
		p = rtw_get_ie(bssid->IEs + _FIXED_IE_LENGTH_, _HT_CAPABILITY_IE_, &len, bssid->IELength - _FIXED_IE_LENGTH_);
		if(p && len>0)
		{
			struct HT_caps_element	*pHT_caps;
			pHT_caps = (struct HT_caps_element	*)(p + 2);
			
			if(pHT_caps->HT_cap_element.HT_caps_info&BIT(14))
			{				
				pmlmepriv->num_FortyMHzIntolerant++;
			}
		}
		else
		{
			pmlmepriv->num_sta_no_ht++;
		}
		
	}

	return _SUCCESS;

}

void start_create_ibss(_adapter* padapter)
{
	unsigned short	caps;
	u32	val32;
	u8	val8;
	u8	join_type;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX		*pnetwork = (WLAN_BSSID_EX*)(&(pmlmeinfo->network));
	pmlmeext->cur_channel = (u8)pnetwork->Configuration.DSConfig;
	pmlmeinfo->bcn_interval = get_beacon_interval(pnetwork);

	//update wireless mode
	update_wireless_mode(padapter);

	//udpate capability
	caps = rtw_get_capability((WLAN_BSSID_EX *)pnetwork);
	update_capinfo(padapter, caps);
	if(caps&cap_IBSS)//adhoc master
	{
		//set_opmode_cmd(padapter, adhoc);//removed

		val8 = 0xcf;
		padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_SEC_CFG, (u8 *)(&val8));

		//switch channel
		//SelectChannel(padapter, pmlmeext->cur_channel, HAL_PRIME_CHNL_OFFSET_DONT_CARE);
		set_channel_bwmode(padapter, pmlmeext->cur_channel, HAL_PRIME_CHNL_OFFSET_DONT_CARE, HT_CHANNEL_WIDTH_20);

		beacon_timing_control(padapter);

		//set msr to WIFI_FW_ADHOC_STATE
		pmlmeinfo->state = WIFI_FW_ADHOC_STATE;
		Set_NETYPE0_MSR(padapter, (pmlmeinfo->state & 0x3));

		//issue beacon
		if(send_beacon(padapter)==_FAIL)
		{
			RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("issuing beacon frame fail....\n"));

			report_join_res(padapter, -1);
			pmlmeinfo->state = WIFI_FW_NULL_STATE;
		}
		else
		{			
			padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_BSSID, padapter->registrypriv.dev_network.MacAddress);
			join_type = 0;
			padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_MLME_JOIN, (u8 *)(&join_type));
	
			report_join_res(padapter, 1);
			pmlmeinfo->state |= WIFI_FW_ASSOC_SUCCESS;
		}
	}
	else
	{
		DBG_871X("start_create_ibss, invalid cap:%x\n", caps);
		return;
	}

}

void start_clnt_join(_adapter* padapter)
{
	unsigned short	caps;
	u8	val8;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX		*pnetwork = (WLAN_BSSID_EX*)(&(pmlmeinfo->network));


	pmlmeext->cur_channel = (u8)pnetwork->Configuration.DSConfig;
	pmlmeinfo->bcn_interval = get_beacon_interval(pnetwork);

	//update wireless mode
	update_wireless_mode(padapter);

	//udpate capability
	caps = rtw_get_capability((WLAN_BSSID_EX *)pnetwork);
	update_capinfo(padapter, caps);
	if (caps&cap_ESS)
	{
		Set_NETYPE0_MSR(padapter, WIFI_FW_STATION_STATE);

		val8 = (pmlmeinfo->auth_algo == dot11AuthAlgrthm_8021X)? 0xcc: 0xcf;
		padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_SEC_CFG, (u8 *)(&val8));

		//switch channel
		set_channel_bwmode(padapter, pmlmeext->cur_channel, pmlmeext->cur_ch_offset, pmlmeext->cur_bwmode);

		//here wait for receiving the beacon to start auth
		//and enable a timer
		set_link_timer(pmlmeext, decide_wait_for_beacon_timeout(pmlmeinfo->bcn_interval));

		pmlmeinfo->state = WIFI_FW_AUTH_NULL | WIFI_FW_STATION_STATE;
	}
	else if (caps&cap_IBSS) //adhoc client
	{
		Set_NETYPE0_MSR(padapter, WIFI_FW_ADHOC_STATE);

		val8 = 0xcf;
		padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_SEC_CFG, (u8 *)(&val8));

		//switch channel
		set_channel_bwmode(padapter, pmlmeext->cur_channel, pmlmeext->cur_ch_offset, pmlmeext->cur_bwmode);

		beacon_timing_control(padapter);

		pmlmeinfo->state = WIFI_FW_ADHOC_STATE;

		report_join_res(padapter, 1);
	}
	else
	{
		//DBG_8192C("marc: invalid cap:%x\n", caps);
		return;
	}

}

void start_clnt_auth(_adapter* padapter)
{
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	_cancel_timer_ex(&pmlmeext->link_timer);

	pmlmeinfo->state &= (~WIFI_FW_AUTH_NULL);
	pmlmeinfo->state |= WIFI_FW_AUTH_STATE;

	pmlmeinfo->auth_seq = 1;
	pmlmeinfo->reauth_count = 0;
	pmlmeinfo->reassoc_count = 0;
	pmlmeinfo->link_count = 0;

	issue_auth(padapter, NULL, 0);

	set_link_timer(pmlmeext, REAUTH_TO);

}


void start_clnt_assoc(_adapter* padapter)
{
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	_cancel_timer_ex(&pmlmeext->link_timer);

	pmlmeinfo->state &= (~(WIFI_FW_AUTH_NULL | WIFI_FW_AUTH_STATE));
	pmlmeinfo->state |= (WIFI_FW_AUTH_SUCCESS | WIFI_FW_ASSOC_STATE);

	issue_assocreq(padapter);

	set_link_timer(pmlmeext, REASSOC_TO);
}

unsigned int receive_disconnect(_adapter *padapter, unsigned char *MacAddr, unsigned short reason)
{
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	//check A3
	if (!(_rtw_memcmp(MacAddr, get_my_bssid(&pmlmeinfo->network), ETH_ALEN)))
		return _SUCCESS;

	DBG_871X("%s\n", __FUNCTION__);

	if((pmlmeinfo->state&0x03) == WIFI_FW_STATION_STATE)
	{
		if (pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS)
		{
			pmlmeinfo->state = WIFI_FW_NULL_STATE;
			report_del_sta_event(padapter, MacAddr, reason);
		}
		else if (pmlmeinfo->state & WIFI_FW_LINKING_STATE)
		{
			pmlmeinfo->state = WIFI_FW_NULL_STATE;
			report_join_res(padapter, -2);
		}
	}

	return _SUCCESS;
}

/****************************************************************************

Following are the functions to report events

*****************************************************************************/

void report_survey_event(_adapter *padapter, union recv_frame *precv_frame)
{
	struct cmd_obj *pcmd_obj;
	u8	*pevtcmd;
	u32 cmdsz;
	struct survey_event	*psurvey_evt;
	struct C2HEvent_Header *pc2h_evt_hdr;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;
	//u8 *pframe = precv_frame->u.hdr.rx_data;
	//uint len = precv_frame->u.hdr.len;
#ifdef CONFIG_TDLS
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	u8 *pframe = get_recvframe_data(precv_frame);
#endif
	if ((pcmd_obj = (struct cmd_obj*)rtw_zmalloc(sizeof(struct cmd_obj))) == NULL)
	{
		return;
	}

	cmdsz = (sizeof(struct survey_event) + sizeof(struct C2HEvent_Header));
	if ((pevtcmd = (u8*)rtw_zmalloc(cmdsz)) == NULL)
	{
		rtw_mfree((u8 *)pcmd_obj, sizeof(struct cmd_obj));
		return;
	}

	_rtw_init_listhead(&pcmd_obj->list);

	pcmd_obj->cmdcode = GEN_CMD_CODE(_Set_MLME_EVT);
	pcmd_obj->cmdsz = cmdsz;
	pcmd_obj->parmbuf = pevtcmd;

	pcmd_obj->rsp = NULL;
	pcmd_obj->rspsz  = 0;

	pc2h_evt_hdr = (struct C2HEvent_Header*)(pevtcmd);
	pc2h_evt_hdr->len = sizeof(struct survey_event);
	pc2h_evt_hdr->ID = GEN_EVT_CODE(_Survey);
	pc2h_evt_hdr->seq = ATOMIC_INC_RETURN(&pmlmeext->event_seq);

	psurvey_evt = (struct survey_event*)(pevtcmd + sizeof(struct C2HEvent_Header));

#ifdef CONFIG_TDLS
	//when tdls_dis_req is on, it would only report STAs who respond TDLS discovery response frame
	if((pmlmeinfo->tdls_dis_req==1 || pmlmeinfo->tdls_dis_req==2)){
		if(*(pframe+24)==0x04 && *(pframe+25)==TDLS_DISCOVERY_RESPONSE){
			if (collect_tdls_info(padapter, precv_frame, (WLAN_BSSID_EX *)&psurvey_evt->bss) == _FAIL){
				rtw_mfree((u8 *)pcmd_obj, sizeof(struct cmd_obj));
				rtw_mfree((u8 *)pevtcmd, cmdsz);
				return;
			}
		}
	}else 
#endif
	if (collect_bss_info(padapter, precv_frame, (WLAN_BSSID_EX *)&psurvey_evt->bss) == _FAIL)
	{
		rtw_mfree((u8 *)pcmd_obj, sizeof(struct cmd_obj));
		rtw_mfree((u8 *)pevtcmd, cmdsz);
		return;
	}

	rtw_enqueue_cmd(pcmdpriv, pcmd_obj);

	pmlmeext->sitesurvey_res.bss_cnt++;

	return;

}

void report_surveydone_event(_adapter *padapter)
{
	struct cmd_obj *pcmd_obj;
	u8	*pevtcmd;
	u32 cmdsz;
	struct surveydone_event *psurveydone_evt;
	struct C2HEvent_Header	*pc2h_evt_hdr;
	struct mlme_ext_priv		*pmlmeext = &padapter->mlmeextpriv;
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;

	if ((pcmd_obj = (struct cmd_obj*)rtw_zmalloc(sizeof(struct cmd_obj))) == NULL)
	{
		return;
	}

	cmdsz = (sizeof(struct surveydone_event) + sizeof(struct C2HEvent_Header));
	if ((pevtcmd = (u8*)rtw_zmalloc(cmdsz)) == NULL)
	{
		rtw_mfree((u8 *)pcmd_obj, sizeof(struct cmd_obj));
		return;
	}

	_rtw_init_listhead(&pcmd_obj->list);

	pcmd_obj->cmdcode = GEN_CMD_CODE(_Set_MLME_EVT);
	pcmd_obj->cmdsz = cmdsz;
	pcmd_obj->parmbuf = pevtcmd;

	pcmd_obj->rsp = NULL;
	pcmd_obj->rspsz  = 0;

	pc2h_evt_hdr = (struct C2HEvent_Header*)(pevtcmd);
	pc2h_evt_hdr->len = sizeof(struct surveydone_event);
	pc2h_evt_hdr->ID = GEN_EVT_CODE(_SurveyDone);
	pc2h_evt_hdr->seq = ATOMIC_INC_RETURN(&pmlmeext->event_seq);

	psurveydone_evt = (struct surveydone_event*)(pevtcmd + sizeof(struct C2HEvent_Header));
	psurveydone_evt->bss_cnt = pmlmeext->sitesurvey_res.bss_cnt;

	DBG_871X("survey done event(%x)\n", psurveydone_evt->bss_cnt);

	rtw_enqueue_cmd(pcmdpriv, pcmd_obj);

	return;

}

void report_join_res(_adapter *padapter, int res)
{
	struct cmd_obj *pcmd_obj;
	u8	*pevtcmd;
	u32 cmdsz;
	struct joinbss_event		*pjoinbss_evt;
	struct C2HEvent_Header	*pc2h_evt_hdr;
	struct mlme_ext_priv		*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;

	if ((pcmd_obj = (struct cmd_obj*)rtw_zmalloc(sizeof(struct cmd_obj))) == NULL)
	{
		return;
	}

	cmdsz = (sizeof(struct joinbss_event) + sizeof(struct C2HEvent_Header));
	if ((pevtcmd = (u8*)rtw_zmalloc(cmdsz)) == NULL)
	{
		rtw_mfree((u8 *)pcmd_obj, sizeof(struct cmd_obj));
		return;
	}

	_rtw_init_listhead(&pcmd_obj->list);

	pcmd_obj->cmdcode = GEN_CMD_CODE(_Set_MLME_EVT);
	pcmd_obj->cmdsz = cmdsz;
	pcmd_obj->parmbuf = pevtcmd;

	pcmd_obj->rsp = NULL;
	pcmd_obj->rspsz  = 0;

	pc2h_evt_hdr = (struct C2HEvent_Header*)(pevtcmd);
	pc2h_evt_hdr->len = sizeof(struct joinbss_event);
	pc2h_evt_hdr->ID = GEN_EVT_CODE(_JoinBss);
	pc2h_evt_hdr->seq = ATOMIC_INC_RETURN(&pmlmeext->event_seq);

	pjoinbss_evt = (struct joinbss_event*)(pevtcmd + sizeof(struct C2HEvent_Header));
	_rtw_memcpy((unsigned char *)(&(pjoinbss_evt->network.network)), &(pmlmeinfo->network), sizeof(WLAN_BSSID_EX));
	pjoinbss_evt->network.join_res 	= pjoinbss_evt->network.aid = res;

	DBG_871X("report_join_res(%d)\n", res);

	#ifdef CONFIG_HANDLE_JOINBSS_ON_ASSOC_RSP
	joinbss_event_prehandle(padapter, (u8 *)&pjoinbss_evt->network);
	#endif //CONFIG_HANDLE_JOINBSS_ON_ASSOC_RSP

	rtw_enqueue_cmd(pcmdpriv, pcmd_obj);

	return;

}

void report_del_sta_event(_adapter *padapter, unsigned char* MacAddr, unsigned short reason)
{
	struct cmd_obj *pcmd_obj;
	u8	*pevtcmd;
	u32 cmdsz;
	struct stadel_event			*pdel_sta_evt;
	struct C2HEvent_Header	*pc2h_evt_hdr;
	struct mlme_ext_priv		*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;

	if ((pcmd_obj = (struct cmd_obj*)rtw_zmalloc(sizeof(struct cmd_obj))) == NULL)
	{
		return;
	}

	cmdsz = (sizeof(struct stadel_event) + sizeof(struct C2HEvent_Header));
	if ((pevtcmd = (u8*)rtw_zmalloc(cmdsz)) == NULL)
	{
		rtw_mfree((u8 *)pcmd_obj, sizeof(struct cmd_obj));
		return;
	}

	_rtw_init_listhead(&pcmd_obj->list);

	pcmd_obj->cmdcode = GEN_CMD_CODE(_Set_MLME_EVT);
	pcmd_obj->cmdsz = cmdsz;
	pcmd_obj->parmbuf = pevtcmd;

	pcmd_obj->rsp = NULL;
	pcmd_obj->rspsz  = 0;

	pc2h_evt_hdr = (struct C2HEvent_Header*)(pevtcmd);
	pc2h_evt_hdr->len = sizeof(struct stadel_event);
	pc2h_evt_hdr->ID = GEN_EVT_CODE(_DelSTA);
	pc2h_evt_hdr->seq = ATOMIC_INC_RETURN(&pmlmeext->event_seq);

	pdel_sta_evt = (struct stadel_event*)(pevtcmd + sizeof(struct C2HEvent_Header));
	_rtw_memcpy((unsigned char *)(&(pdel_sta_evt->macaddr)), MacAddr, ETH_ALEN);
	_rtw_memcpy((unsigned char *)(pdel_sta_evt->rsvd),(unsigned char *)(&reason),2);

	DBG_871X("rtl8192: delete STA\n");

	rtw_enqueue_cmd(pcmdpriv, pcmd_obj);

	return;
}

void report_add_sta_event(_adapter *padapter, unsigned char* MacAddr, int cam_idx)
{
	struct cmd_obj *pcmd_obj;
	u8	*pevtcmd;
	u32 cmdsz;
	struct stassoc_event		*padd_sta_evt;
	struct C2HEvent_Header	*pc2h_evt_hdr;
	struct mlme_ext_priv		*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct cmd_priv *pcmdpriv = &padapter->cmdpriv;

	if ((pcmd_obj = (struct cmd_obj*)rtw_zmalloc(sizeof(struct cmd_obj))) == NULL)
	{
		return;
	}

	cmdsz = (sizeof(struct stassoc_event) + sizeof(struct C2HEvent_Header));
	if ((pevtcmd = (u8*)rtw_zmalloc(cmdsz)) == NULL)
	{
		rtw_mfree((u8 *)pcmd_obj, sizeof(struct cmd_obj));
		return;
	}

	_rtw_init_listhead(&pcmd_obj->list);

	pcmd_obj->cmdcode = GEN_CMD_CODE(_Set_MLME_EVT);
	pcmd_obj->cmdsz = cmdsz;
	pcmd_obj->parmbuf = pevtcmd;

	pcmd_obj->rsp = NULL;
	pcmd_obj->rspsz  = 0;

	pc2h_evt_hdr = (struct C2HEvent_Header*)(pevtcmd);
	pc2h_evt_hdr->len = sizeof(struct stassoc_event);
	pc2h_evt_hdr->ID = GEN_EVT_CODE(_AddSTA);
	pc2h_evt_hdr->seq = ATOMIC_INC_RETURN(&pmlmeext->event_seq);

	padd_sta_evt = (struct stassoc_event*)(pevtcmd + sizeof(struct C2HEvent_Header));
	_rtw_memcpy((unsigned char *)(&(padd_sta_evt->macaddr)), MacAddr, ETH_ALEN);
	padd_sta_evt->cam_id = cam_idx;

	DBG_871X("report_add_sta_event: add STA\n");

	rtw_enqueue_cmd(pcmdpriv, pcmd_obj);

	return;
}


/****************************************************************************

Following are the event callback functions

*****************************************************************************/

//for sta/adhoc mode
static void update_sta_info(_adapter *padapter, struct sta_info *psta)
{
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	//ERP
	VCS_update(padapter, psta);


	//HT
	if(pmlmepriv->htpriv.ht_option)
	{
		psta->htpriv.ht_option = _TRUE;

		psta->htpriv.ampdu_enable = pmlmepriv->htpriv.ampdu_enable;

		if (support_short_GI(padapter, &(pmlmeinfo->HT_caps)))
			psta->htpriv.sgi = _TRUE;

		psta->qos_option = _TRUE;
		
	}
	else
	{
		psta->htpriv.ht_option = _FALSE;

		psta->htpriv.ampdu_enable = _FALSE;
		
		psta->htpriv.sgi = _FALSE;

		psta->qos_option = _FALSE;//?

	}
	
	psta->htpriv.bwmode = pmlmeext->cur_bwmode;
	psta->htpriv.ch_offset = pmlmeext->cur_ch_offset;
	
	psta->htpriv.agg_enable_bitmap = 0x0;//reset
	psta->htpriv.candidate_tid_bitmap = 0x0;//reset
	

	//QoS
	if(pmlmepriv->qospriv.qos_option)
		psta->qos_option = _TRUE;
	

	psta->state = _FW_LINKED;

}

u8	null_addr[ETH_ALEN]= {0,0,0,0,0,0};

void mlmeext_joinbss_event_callback(_adapter *padapter, int join_res)
{
	struct sta_info		*psta, *psta_bmc;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX 		*cur_network = &(pmlmeinfo->network);
	struct sta_priv		*pstapriv = &padapter->stapriv;
	u8	join_type, init_rts_rate;

	if(join_res < 0)
	{
		join_type = 1;
		padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_MLME_JOIN, (u8 *)(&join_type));
		padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_BSSID, null_addr);
		return;
	}

	if((pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE)
	{
		//for bc/mc
		psta_bmc = rtw_get_bcmc_stainfo(padapter);
		if(psta_bmc)
		{
			pmlmeinfo->FW_sta_info[psta_bmc->mac_id].psta = psta_bmc;
			update_bmc_sta_support_rate(padapter, psta_bmc->mac_id);
			Update_RA_Entry(padapter, psta_bmc->mac_id);
		}
	}

	psta = rtw_get_stainfo(pstapriv, cur_network->MacAddress);
	if (psta)//only for infra. mode
	{
		pmlmeinfo->FW_sta_info[psta->mac_id].psta = psta;
	}

	//turn on dynamic functions
	Switch_DM_Func(padapter, DYNAMIC_FUNC_DIG|DYNAMIC_FUNC_HP|DYNAMIC_FUNC_SS, _TRUE);

	// update IOT-releated issue
	update_IOT_info(padapter);

	padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_BASIC_RATE, cur_network->SupportedRates);

	//BCN interval
	padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_BEACON_INTERVAL, (u8 *)(&pmlmeinfo->bcn_interval));

	//udpate capability
	update_capinfo(padapter, pmlmeinfo->capability);

	//WMM, Update EDCA param
	WMMOnAssocRsp(padapter);

	//HT
	HTOnAssocRsp(padapter);

        //update sta_info
	if (psta) //only for infra. mode
	{
		//DBG_871X("set_sta_rate & update_sta_info\n");
	
		//set per sta rate after updating HT cap.
		set_sta_rate(padapter, psta);
		
		update_sta_info(padapter, psta);
	}

	join_type = 2;
	padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_MLME_JOIN, (u8 *)(&join_type));

	if((pmlmeinfo->state&0x03) == WIFI_FW_STATION_STATE)
	{
		// correcting TSF
		correct_TSF(padapter, pmlmeext);
	
		//set_link_timer(pmlmeext, DISCONNECT_TO);
		pmlmeext->linked_to = LINKED_TO;
	}

#ifdef CONFIG_LPS
	rtw_lps_ctrl_wk_cmd(padapter, LPS_CTRL_CONNECT, 0);
#endif
	
	DBG_871X("=>%s\n", __FUNCTION__);

}

void mlmeext_sta_add_event_callback(_adapter *padapter, struct sta_info *psta)
{
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	u8	join_type;

	DBG_871X("%s\n", __FUNCTION__);

	if((pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE)
	{
		if(pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS)//adhoc master or sta_count>1
		{
			//nothing to do
		}
		else//adhoc client
		{
			//update TSF Value
			//update_TSF(pmlmeext, pframe, len);			

			// correcting TSF
			correct_TSF(padapter, pmlmeext);

			//start beacon
			if(send_beacon(padapter)==_FAIL)
			{
				pmlmeinfo->FW_sta_info[psta->mac_id].status = 0;

				pmlmeinfo->state ^= WIFI_FW_ADHOC_STATE;

				return;
			}

			pmlmeinfo->state |= WIFI_FW_ASSOC_SUCCESS;
				
		}

		join_type = 2;
		padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_MLME_JOIN, (u8 *)(&join_type));
	}

	pmlmeinfo->FW_sta_info[psta->mac_id].psta = psta;

	//rate radaptive
	Update_RA_Entry(padapter, psta->mac_id);

	//update adhoc sta_info
	update_sta_info(padapter, psta);

	pmlmeext->linked_to = LINKED_TO;

}

void mlmeext_sta_del_event_callback(_adapter *padapter)
{
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	if (is_client_associated_to_ap(padapter) || is_IBSS_empty(padapter))
	{
		//set_opmode_cmd(padapter, infra_client_with_mlme);

		//switch to the 20M Hz mode after disconnect
		pmlmeext->cur_bwmode = HT_CHANNEL_WIDTH_20;
		pmlmeext->cur_ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;

		padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_MLME_DISCONNECT, 0);
		padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_BSSID, null_addr);

		//SelectChannel(padapter, pmlmeext->cur_channel, pmlmeext->cur_ch_offset);
		set_channel_bwmode(padapter, pmlmeext->cur_channel, pmlmeext->cur_ch_offset, pmlmeext->cur_bwmode);
		flush_all_cam_entry(padapter);

		pmlmeinfo->state = WIFI_FW_NULL_STATE;

		//set MSR to no link state
		Set_NETYPE0_MSR(padapter, _HW_STATE_NOLINK_);

		pmlmeext->linked_to = 0;
		_cancel_timer_ex(&pmlmeext->link_timer);

	}

}

/****************************************************************************

Following are the functions for the timer handlers

*****************************************************************************/

void _linked_rx_signal_strehgth_display(_adapter *padapter)
{
	int	UndecoratedSmoothedPWDB;
	DBG_8192C("============ linked status check ===================\n");
	DBG_8192C("pathA Rx SNRdb:%d\n",padapter->recvpriv.RxSNRdB[0]);
	DBG_8192C("pathA Rx PWDB:%d\n",padapter->recvpriv.rxpwdb);		
	padapter->HalFunc.GetHalDefVarHandler(padapter, HAL_DEF_UNDERCORATEDSMOOTHEDPWDB, &UndecoratedSmoothedPWDB);
	DBG_8192C("UndecoratedSmoothedPWDB:%d\n",UndecoratedSmoothedPWDB);
	DBG_8192C("Rx RSSI:%d\n",padapter->recvpriv.rssi);
	DBG_8192C("Rx Signal_strength:%d\n",padapter->recvpriv.signal_strength);
	DBG_8192C("Rx Signal_qual:%d \n",padapter->recvpriv.signal_qual);
	DBG_8192C("============ linked status check ===================\n");
	DBG_8192C(" DIG PATH-A(0x%02x), PATH-B(0x%02x)\n",rtw_read8(padapter,0xc50),rtw_read8(padapter,0xc58));
	DBG_8192C(" OFDM -Alarm DA2(0x%04x),DA4(0x%04x),DA6(0x%04x),DA8(0x%04x)\n",
		rtw_read16(padapter,0xDA2),rtw_read16(padapter,0xDA4),rtw_read16(padapter,0xDA6),rtw_read16(padapter,0xDA8));

	DBG_8192C(" CCK -Alarm A5B(0x%02x),A5C(0x%02x)\n",rtw_read8(padapter,0xA5B),rtw_read8(padapter,0xA5C));
	
}

void linked_status_chk(_adapter *padapter)
{
	u32	i;
	struct sta_info		*psta;
	struct xmit_priv		*pxmitpriv = &(padapter->xmitpriv);
	struct recv_priv		*precvpriv = &(padapter->recvpriv);
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct sta_priv		*pstapriv = &padapter->stapriv;
	struct registry_priv	*pregistrypriv = &padapter->registrypriv;

	if (is_client_associated_to_ap(padapter))
	{
		if(padapter->bRxRSSIDisplay)
		 	_linked_rx_signal_strehgth_display(padapter);

		//linked infrastructure client mode
		if ((psta = rtw_get_stainfo(pstapriv, pmlmeinfo->network.MacAddress)) != NULL)
		{
			/*to monitor whether the AP is alive or not*/
			if (psta->sta_stats.last_rx_pkts == psta->sta_stats.rx_pkts)
			{
				//	Commented by Albert 2010/07/21
				//	In this case, there is no any rx packet received by driver.

				#ifdef DBG_ROAMING_TEST
				if(pmlmeext->retry<1)
				#else
				if(pmlmeext->retry<8)// Alter the retry limit to 8
				#endif
				{
					if(pmlmeext->retry==0)
					{
						_rtw_memcpy(pmlmeext->sitesurvey_res.ss_ssid, pmlmeinfo->network.Ssid.Ssid, pmlmeinfo->network.Ssid.SsidLength);
						pmlmeext->sitesurvey_res.ss_ssidlen = pmlmeinfo->network.Ssid.SsidLength;
						pmlmeext->sitesurvey_res.scan_mode = SCAN_ACTIVE;
						pmlmeext->sitesurvey_res.state = SCAN_DISABLE;
						#ifdef SILENT_RESET_FOR_SPECIFIC_PLATFOM	
						if(padapter->HalFunc.sreset_linked_status_check)
							padapter->HalFunc.sreset_linked_status_check(padapter);						
						#endif

						//DBG_871X("issue_probereq to check if ap alive, retry=%d\n", pmlmeext->retry);
					
						//	In order to know the AP's current state, try to send the probe request 
						//	to trigger the AP to send the probe response.
						issue_probereq(padapter, 0);
						issue_probereq(padapter, 0);
						issue_probereq(padapter, 0);
					}
					
					pmlmeext->retry++;
					pmlmeext->linked_to = LINKED_TO;
				}
				else
				{
					pmlmeext->retry = 0;
                                	DBG_871X("no beacon to call receive_disconnect()\n");
					receive_disconnect(padapter, pmlmeinfo->network.MacAddress
						, 65535// indicate disconnect caused by no rx
					);
					pmlmeinfo->link_count = 0;
					return;
				}
			}
			else
			{
				pmlmeext->retry = 0;
				psta->sta_stats.last_rx_pkts = psta->sta_stats.rx_pkts;
				//set_link_timer(pmlmeext, DISCONNECT_TO);
				pmlmeext->linked_to = LINKED_TO;
			}

			/*to send the AP a nulldata if no frame is xmitted in order to keep alive*/
			if (pmlmeinfo->link_count++ == 0)
			{
				pxmitpriv->last_tx_pkts = pxmitpriv->tx_pkts;
			}
			else if ((pmlmeinfo->link_count & 0xf) == 0)
			{
				if ( pxmitpriv->last_tx_pkts == pxmitpriv->tx_pkts)
				{
					//DBG_871X("(Interface %d)issue nulldata to keep alive\n",padapter->dvobjpriv.InterfaceNumber);
					issue_nulldata(padapter, 0);
				}

				pxmitpriv->last_tx_pkts = pxmitpriv->tx_pkts;
			}

		} //end of if ((psta = rtw_get_stainfo(pstapriv, passoc_res->network.MacAddress)) != NULL)
	}
	else if (is_client_associated_to_ibss(padapter))
	{
		//linked IBSS mode
		//for each assoc list entry to check the rx pkt counter
		for (i = IBSS_START_MAC_ID; i < NUM_STA; i++)
		{
			if (pmlmeinfo->FW_sta_info[i].status == 1)
			{
				psta = pmlmeinfo->FW_sta_info[i].psta;

				if(NULL==psta) continue;

				if (pmlmeinfo->FW_sta_info[i].rx_pkt == psta->sta_stats.rx_pkts)
				{

					if(pmlmeinfo->FW_sta_info[i].retry<3)
					{
						pmlmeinfo->FW_sta_info[i].retry++;
					}
					else
					{
						pmlmeinfo->FW_sta_info[i].retry = 0;
						pmlmeinfo->FW_sta_info[i].status = 0;
						report_del_sta_event(padapter, psta->hwaddr
							, 65535// indicate disconnect caused by no rx
						);
					}	
				}
				else
				{
					pmlmeinfo->FW_sta_info[i].retry = 0;
					pmlmeinfo->FW_sta_info[i].rx_pkt = (u32)psta->sta_stats.rx_pkts;
				}
			}
		}

		//set_link_timer(pmlmeext, DISCONNECT_TO);
		pmlmeext->linked_to = LINKED_TO;

	}

}

void survey_timer_hdl(_adapter *padapter)
{
	struct cmd_obj	*ph2c;
	struct sitesurvey_parm	*psurveyPara;
	struct cmd_priv					*pcmdpriv=&padapter->cmdpriv;
	struct mlme_priv				*pmlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv 		*pmlmeext = &padapter->mlmeextpriv;

	//DBG_8192C("marc: survey timer\n");

	//issue rtw_sitesurvey_cmd
	if (pmlmeext->sitesurvey_res.state > SCAN_START)
	{
		if(pmlmeext->sitesurvey_res.state ==  SCAN_PROCESS)
			pmlmeext->sitesurvey_res.channel_idx++;

		if ((ph2c = (struct cmd_obj*)rtw_zmalloc(sizeof(struct cmd_obj))) == NULL)
		{
			goto exit_survey_timer_hdl;
		}

		if ((psurveyPara = (struct sitesurvey_parm*)rtw_zmalloc(sizeof(struct sitesurvey_parm))) == NULL)
		{
			rtw_mfree((unsigned char *)ph2c, sizeof(struct cmd_obj));
			goto exit_survey_timer_hdl;
		}

		init_h2fwcmd_w_parm_no_rsp(ph2c, psurveyPara, GEN_CMD_CODE(_SiteSurvey));
		rtw_enqueue_cmd(pcmdpriv, ph2c);
	}


exit_survey_timer_hdl:

	return;
}

void link_timer_hdl(_adapter *padapter)
{
	static unsigned int		rx_pkt = 0;
	static u64				tx_cnt = 0;
	struct xmit_priv		*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct sta_priv		*pstapriv = &padapter->stapriv;

	if (pmlmeinfo->state & WIFI_FW_AUTH_NULL)
	{
		DBG_871X("link_timer_hdl:no beacon while connecting\n");
		pmlmeinfo->state = WIFI_FW_NULL_STATE;
		report_join_res(padapter, -3);
	}
	else if (pmlmeinfo->state & WIFI_FW_AUTH_STATE)
	{
		//re-auth timer
		if (++pmlmeinfo->reauth_count > REAUTH_LIMIT)
		{
			if (pmlmeinfo->auth_algo != dot11AuthAlgrthm_Auto)
			{
				pmlmeinfo->state = 0;
				report_join_res(padapter, -1);
				return;
			}
			else
			{
				pmlmeinfo->auth_algo = dot11AuthAlgrthm_Shared;
				pmlmeinfo->reauth_count = 0;
			}
		}

		DBG_871X("link_timer_hdl: auth timeout and try again\n");
		pmlmeinfo->auth_seq = 1;
		issue_auth(padapter, NULL, 0);
		set_link_timer(pmlmeext, REAUTH_TO);
	}
	else if (pmlmeinfo->state & WIFI_FW_ASSOC_STATE)
	{
		//re-assoc timer
		if (++pmlmeinfo->reassoc_count > REASSOC_LIMIT)
		{
			pmlmeinfo->state = WIFI_FW_NULL_STATE;
			report_join_res(padapter, -2);
			return;
		}

		DBG_871X("link_timer_hdl: assoc timeout and try again\n");
		issue_assocreq(padapter);
		set_link_timer(pmlmeext, REASSOC_TO);
	}
#if 0
	else if (is_client_associated_to_ap(padapter))
	{
		//linked infrastructure client mode
		if ((psta = rtw_get_stainfo(pstapriv, pmlmeinfo->network.MacAddress)) != NULL)
		{
			/*to monitor whether the AP is alive or not*/
			if (rx_pkt == psta->sta_stats.rx_pkts)
			{
				receive_disconnect(padapter, pmlmeinfo->network.MacAddress);
				return;
			}
			else
			{
				rx_pkt = psta->sta_stats.rx_pkts;
				set_link_timer(pmlmeext, DISCONNECT_TO);
			}

			//update the EDCA paramter according to the Tx/RX mode
			update_EDCA_param(padapter);

			/*to send the AP a nulldata if no frame is xmitted in order to keep alive*/
			if (pmlmeinfo->link_count++ == 0)
			{
				tx_cnt = pxmitpriv->tx_pkts;
			}
			else if ((pmlmeinfo->link_count & 0xf) == 0)
			{
				if (tx_cnt == pxmitpriv->tx_pkts)
				{
					issue_nulldata(padapter, 0);
				}

				tx_cnt = pxmitpriv->tx_pkts;
			}
		} //end of if ((psta = rtw_get_stainfo(pstapriv, passoc_res->network.MacAddress)) != NULL)
	}
	else if (is_client_associated_to_ibss(padapter))
	{
		//linked IBSS mode
		//for each assoc list entry to check the rx pkt counter
		for (i = IBSS_START_MAC_ID; i < NUM_STA; i++)
		{
			if (pmlmeinfo->FW_sta_info[i].status == 1)
			{
				psta = pmlmeinfo->FW_sta_info[i].psta;

				if (pmlmeinfo->FW_sta_info[i].rx_pkt == psta->sta_stats.rx_pkts)
				{
					pmlmeinfo->FW_sta_info[i].status = 0;
					report_del_sta_event(padapter, psta->hwaddr);
				}
				else
				{
					pmlmeinfo->FW_sta_info[i].rx_pkt = psta->sta_stats.rx_pkts;
				}
			}
		}

		set_link_timer(pmlmeext, DISCONNECT_TO);
	}
#endif

	return;
}

void addba_timer_hdl(struct sta_info *psta)
{
	u8 bitmap;
	u16 tid;
	struct ht_priv	*phtpriv;

	if(!psta)
		return;
	
	phtpriv = &psta->htpriv;

	if((phtpriv->ht_option==_TRUE) && (phtpriv->ampdu_enable==_TRUE)) 
	{
		if(phtpriv->candidate_tid_bitmap)
			phtpriv->candidate_tid_bitmap=0x0;
		
	}
	
}

u8 NULL_hdl(_adapter *padapter, u8 *pbuf)
{
	return H2C_SUCCESS;
}

u8 setopmode_hdl(_adapter *padapter, u8 *pbuf)
{
	u8	type;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct setopmode_parm *psetop = (struct setopmode_parm *)pbuf;

	if(psetop->mode == Ndis802_11APMode)
	{
		pmlmeinfo->state = WIFI_FW_AP_STATE;
		type = _HW_STATE_AP_;
#ifdef CONFIG_NATIVEAP_MLME
		//start_ap_mode(padapter);
#endif
	}
	else if(psetop->mode == Ndis802_11Infrastructure)
	{
		type = _HW_STATE_STATION_;
	}
	else if(psetop->mode == Ndis802_11IBSS)
	{
		type = _HW_STATE_ADHOC_;
	}
	else
	{
		type = _HW_STATE_NOLINK_;
	}

	padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_SET_OPMODE, (u8 *)(&type));
	//Set_NETYPE0_MSR(padapter, type);

	return H2C_SUCCESS;
	
}

u8 createbss_hdl(_adapter *padapter, u8 *pbuf)
{
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX	*pnetwork = (WLAN_BSSID_EX*)(&(pmlmeinfo->network));
	struct joinbss_parm *pparm = (struct joinbss_parm *)pbuf;
	u32	initialgain;

	
	if(pparm->network.InfrastructureMode == Ndis802_11APMode)
	{
#ifdef CONFIG_AP_MODE
	
		if(pmlmeinfo->state == WIFI_FW_AP_STATE)
		{		
			//todo:
			return H2C_SUCCESS;		
		}		
#endif
	}

	//below is for ad-hoc master
	if(pparm->network.InfrastructureMode == Ndis802_11IBSS)
	{
		rtw_joinbss_reset(padapter);

		pmlmeext->linked_to = 0;
	
		pmlmeext->cur_bwmode = HT_CHANNEL_WIDTH_20;
		pmlmeext->cur_ch_offset= HAL_PRIME_CHNL_OFFSET_DONT_CARE;	
		pmlmeinfo->ERP_enable = 0;
		pmlmeinfo->WMM_enable = 0;
		pmlmeinfo->HT_enable = 0;
		pmlmeinfo->HT_caps_enable = 0;
		pmlmeinfo->HT_info_enable = 0;
		pmlmeinfo->agg_enable_bitmap = 0;
		pmlmeinfo->candidate_tid_bitmap = 0;

		//disable dynamic functions, such as high power, DIG
		Save_DM_Func_Flag(padapter);
		Switch_DM_Func(padapter, DYNAMIC_FUNC_DISABLE, _FALSE);

		//config the initial gain under linking, need to write the BB registers
		//initialgain = 0x30;
		//padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_INITIAL_GAIN, (u8 *)(&initialgain));

		initialgain = 0x1E;
		padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_INITIAL_GAIN, (u8 *)(&initialgain));

		//cancel link timer 
		_cancel_timer_ex(&pmlmeext->link_timer);

		//clear CAM
		flush_all_cam_entry(padapter);	

		_rtw_memcpy(pnetwork, pbuf, FIELD_OFFSET(WLAN_BSSID_EX, IELength)); 
		pnetwork->IELength = ((WLAN_BSSID_EX *)pbuf)->IELength;

		if(pnetwork->IELength>MAX_IE_SZ)//Check pbuf->IELength
			return H2C_PARAMETERS_ERROR;

		_rtw_memcpy(pnetwork->IEs, ((WLAN_BSSID_EX *)pbuf)->IEs, pnetwork->IELength);
	
		start_create_ibss(padapter);

	}	

	return H2C_SUCCESS;

}

u8 join_cmd_hdl(_adapter *padapter, u8 *pbuf)
{
	u8	join_type;
	PNDIS_802_11_VARIABLE_IEs	pIE;
	struct registry_priv	*pregpriv = &padapter->registrypriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX		*pnetwork = (WLAN_BSSID_EX*)(&(pmlmeinfo->network));
	struct joinbss_parm	*pparm = (struct joinbss_parm *)pbuf;
	u32	acparm, initialgain, i;

	//check already connecting to AP or not
	if (pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS)
	{
		if (pmlmeinfo->state & WIFI_FW_STATION_STATE)
		{
			issue_deauth(padapter, pnetwork->MacAddress, WLAN_REASON_DEAUTH_LEAVING);
		}

		pmlmeinfo->state = WIFI_FW_NULL_STATE;
		
		//clear CAM
		flush_all_cam_entry(padapter);		
		
		_cancel_timer_ex(&pmlmeext->link_timer);
		
		//set MSR to nolink		
		Set_NETYPE0_MSR(padapter, _HW_STATE_NOLINK_);	

		padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_MLME_DISCONNECT, 0);
	}

#ifdef CONFIG_ANTENNA_DIVERSITY
	rtw_antenna_select_cmd(padapter, pparm->network.PhyInfo.Optimum_antenna, _FALSE);
#endif

	rtw_joinbss_reset(padapter);

	pmlmeext->linked_to = 0;
	
	pmlmeext->cur_bwmode = HT_CHANNEL_WIDTH_20;
	pmlmeext->cur_ch_offset= HAL_PRIME_CHNL_OFFSET_DONT_CARE;	
	pmlmeinfo->ERP_enable = 0;
	pmlmeinfo->WMM_enable = 0;
	pmlmeinfo->HT_enable = 0;
	pmlmeinfo->HT_caps_enable = 0;
	pmlmeinfo->HT_info_enable = 0;
	pmlmeinfo->agg_enable_bitmap = 0;
	pmlmeinfo->candidate_tid_bitmap = 0;
	pmlmeinfo->bwmode_updated = _FALSE;
	//pmlmeinfo->assoc_AP_vendor = maxAP;

	_rtw_memcpy(pnetwork, pbuf, FIELD_OFFSET(WLAN_BSSID_EX, IELength)); 
	pnetwork->IELength = ((WLAN_BSSID_EX *)pbuf)->IELength;
	
	if(pnetwork->IELength>MAX_IE_SZ)//Check pbuf->IELength
		return H2C_PARAMETERS_ERROR;	
		
	_rtw_memcpy(pnetwork->IEs, ((WLAN_BSSID_EX *)pbuf)->IEs, pnetwork->IELength); 

	//Check AP vendor
	pmlmeinfo->assoc_AP_vendor = check_assoc_AP(pnetwork->IEs, pnetwork->IELength);

	for (i = sizeof(NDIS_802_11_FIXED_IEs); i < pnetwork->IELength;)
	{
		pIE = (PNDIS_802_11_VARIABLE_IEs)(pnetwork->IEs + i);

		switch (pIE->ElementID)
		{
			case _VENDOR_SPECIFIC_IE_://Get WMM IE.
				if ( _rtw_memcmp(pIE->data, WMM_OUI, 4) )
				{
					pmlmeinfo->WMM_enable = 1;
				}
				break;

			case _HT_CAPABILITY_IE_:	//Get HT Cap IE.
				pmlmeinfo->HT_caps_enable = 1;
				break;

			case _HT_EXTRA_INFO_IE_:	//Get HT Info IE.
				pmlmeinfo->HT_info_enable = 1;

				//spec case only for cisco's ap because cisco's ap issue assoc rsp using mcs rate @40MHz or @20MHz	
				if(pmlmeinfo->assoc_AP_vendor == ciscoAP)
				{
					struct HT_info_element *pht_info = (struct HT_info_element *)(pIE->data);
							
					if ((pregpriv->cbw40_enable) &&	 (pht_info->infos[0] & BIT(2)))
					{
						//switch to the 40M Hz mode according to the AP
						pmlmeext->cur_bwmode = HT_CHANNEL_WIDTH_40;
						switch (pht_info->infos[0] & 0x3)
						{
							case 1:
								pmlmeext->cur_ch_offset = HAL_PRIME_CHNL_OFFSET_LOWER;
								break;
			
							case 3:
								pmlmeext->cur_ch_offset = HAL_PRIME_CHNL_OFFSET_UPPER;
								break;
				
							default:
								pmlmeext->cur_ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
								break;
						}

						DBG_871X("set ch/bw for cisco's ap before connected\n");
					}
				}
				break;

			default:
				break;
		}

		i += (pIE->Length + 2);
	}
#if 0
	if (padapter->registrypriv.wifi_spec) {
		// for WiFi test, follow WMM test plan spec
		acparm = 0x002F431C; // VO
		padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_AC_PARAM_VO, (u8 *)(&acparm));
		acparm = 0x005E541C; // VI
		padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_AC_PARAM_VI, (u8 *)(&acparm));
		acparm = 0x0000A525; // BE
		padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_AC_PARAM_BE, (u8 *)(&acparm));
		acparm = 0x0000A549; // BK
		padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_AC_PARAM_BK, (u8 *)(&acparm));
	
		// for WiFi test, mixed mode with intel STA under bg mode throughput issue
		if (padapter->mlmepriv.htpriv.ht_option == _FALSE){
			acparm = 0x00004320;
			padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_AC_PARAM_BE, (u8 *)(&acparm));
		}
	}
	else {
		acparm = 0x002F3217; // VO
		padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_AC_PARAM_VO, (u8 *)(&acparm));
		acparm = 0x005E4317; // VI
		padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_AC_PARAM_VI, (u8 *)(&acparm));
		acparm = 0x00105320; // BE
		padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_AC_PARAM_BE, (u8 *)(&acparm));
		acparm = 0x0000A444; // BK
		padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_AC_PARAM_BK, (u8 *)(&acparm));
	}
#endif
	//disable dynamic functions, such as high power, DIG
	//Switch_DM_Func(padapter, DYNAMIC_FUNC_DISABLE, _FALSE);

	//config the initial gain under linking, need to write the BB registers
	//initialgain = 0x32;
	//padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_INITIAL_GAIN, (u8 *)(&initialgain));

	initialgain = 0x1E;
	padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_INITIAL_GAIN, (u8 *)(&initialgain));

	padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_BSSID, pmlmeinfo->network.MacAddress);
	join_type = 0;
	padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_MLME_JOIN, (u8 *)(&join_type));

	//cancel link timer 
	_cancel_timer_ex(&pmlmeext->link_timer);
	
	start_clnt_join(padapter);
	
	return H2C_SUCCESS;
	
}

u8 disconnect_hdl(_adapter *padapter, unsigned char *pbuf)
{
	struct disconnect_parm	*pparm = (struct disconnect_parm *)pbuf;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX		*pnetwork = (WLAN_BSSID_EX*)(&(pmlmeinfo->network));
	u8	val8;
	
	if (is_client_associated_to_ap(padapter))
	{
		issue_deauth(padapter, pnetwork->MacAddress, WLAN_REASON_DEAUTH_LEAVING);
	}

	//set_opmode_cmd(padapter, infra_client_with_mlme);

	pmlmeinfo->state = WIFI_FW_NULL_STATE;
	
	//switch to the 20M Hz mode after disconnect
	pmlmeext->cur_bwmode = HT_CHANNEL_WIDTH_20;
	pmlmeext->cur_ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
		
	//set MSR to no link state
	Set_NETYPE0_MSR(padapter, _HW_STATE_NOLINK_);

	padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_MLME_DISCONNECT, 0);
	padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_BSSID, null_addr);
	
	if(((pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE) || ((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE))
	{
		//Stop BCN
		val8 = 0;
		padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_BCN_FUNC, (u8 *)(&val8));
	}

	pmlmeinfo->state = WIFI_FW_NULL_STATE;
	
	set_channel_bwmode(padapter, pmlmeext->cur_channel, pmlmeext->cur_ch_offset, pmlmeext->cur_bwmode);

	flush_all_cam_entry(padapter);
		
	_cancel_timer_ex(&pmlmeext->link_timer);
	pmlmeext->linked_to = 0;
	
	return 	H2C_SUCCESS;
}

u8 sitesurvey_cmd_hdl(_adapter *padapter, u8 *pbuf)
{
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct sitesurvey_parm	*pparm = (struct sitesurvey_parm *)pbuf;
	u8	val8;
	u32	initialgain;
	u32 i;
#ifdef CONFIG_TDLS
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
#endif

#ifdef CONFIG_P2P
	struct wifidirect_info*	pwdinfo = &padapter->wdinfo;
#endif

	if (pmlmeext->sitesurvey_res.state == SCAN_DISABLE)
	{
		//for first time sitesurvey_cmd
		pmlmeext->sitesurvey_res.state = SCAN_START;
		pmlmeext->sitesurvey_res.bss_cnt = 0;
		pmlmeext->sitesurvey_res.channel_idx = 0;
		
		if (le32_to_cpu(pparm->ss_ssidlen))
		{
			_rtw_memcpy(pmlmeext->sitesurvey_res.ss_ssid, pparm->ss_ssid, le32_to_cpu(pparm->ss_ssidlen));
		}	
		else
		{
			_rtw_memset(pmlmeext->sitesurvey_res.ss_ssid, 0, (IW_ESSID_MAX_SIZE + 1));
		}	
		
		pmlmeext->sitesurvey_res.ss_ssidlen = le32_to_cpu(pparm->ss_ssidlen);

#ifdef CONFIG_TDLS
		if(pmlmeinfo->tdls_ch_sensing==1)
			pmlmeext->sitesurvey_res.scan_mode=SCAN_PASSIVE;
		else
			pmlmeext->sitesurvey_res.scan_mode = le32_to_cpu(pparm->scan_mode);			
#else
		pmlmeext->sitesurvey_res.scan_mode = le32_to_cpu(pparm->scan_mode);
#endif

		//issue null data if associating to the AP
		if (is_client_associated_to_ap(padapter) == _TRUE)
		{
			pmlmeext->sitesurvey_res.state = SCAN_TXNULL;

			issue_nulldata(padapter, 1);
			issue_nulldata(padapter, 1);

			//delay 50ms to protect nulldata(1).
			set_survey_timer(pmlmeext, 50);

			return H2C_SUCCESS;
		}
	}

	if ((pmlmeext->sitesurvey_res.state == SCAN_START) || (pmlmeext->sitesurvey_res.state == SCAN_TXNULL))
	{
#ifdef CONFIG_FIND_BEST_CHANNEL
#if 0
		for (i=0; pmlmeext->channel_set[i].ChannelNum !=0; i++) {
			pmlmeext->channel_set[i].rx_count = 0;				
		}
#endif
#endif /* CONFIG_FIND_BEST_CHANNEL */

		//disable dynamic functions, such as high power, DIG
		Save_DM_Func_Flag(padapter);
		Switch_DM_Func(padapter, DYNAMIC_FUNC_DISABLE, _FALSE);

		//config the initial gain under scaning, need to write the BB registers
		//initialgain = 0x20;
		//padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_INITIAL_GAIN, (u8 *)(&initialgain));		
		initialgain = 0x17;
		padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_INITIAL_GAIN, (u8 *)(&initialgain));		

		//set MSR to no link state
		Set_NETYPE0_MSR(padapter, _HW_STATE_NOLINK_);

		val8 = 1; //before site survey
		padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_MLME_SITESURVEY, (u8 *)(&val8));

		//	Commented by Albert 2011/08/05
		//	The pre_tx_scan_timer_process will issue the scan H2C command.
		//	However, the driver should NOT enter the scanning mode at that time.
		pmlmeext->sitesurvey_res.state = SCAN_PROCESS;
	}

#ifdef CONFIG_TDLS
		if(pmlmeinfo->tdls_ch_sensing==1){
			rtw_write16(padapter, 0x06A4,0xffff);
		}
#endif

	site_survey(padapter);

	return H2C_SUCCESS;
	
}

u8 setauth_hdl(_adapter *padapter, unsigned char *pbuf)
{
	struct setauth_parm		*pparm = (struct setauth_parm *)pbuf;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	
	if (pparm->mode < 4)
	{
		pmlmeinfo->auth_algo = pparm->mode;
	}

	return 	H2C_SUCCESS;
}

u8 setkey_hdl(_adapter *padapter, u8 *pbuf)
{
	unsigned short				ctrl;
	struct setkey_parm		*pparm = (struct setkey_parm *)pbuf;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	unsigned char					null_sta[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

	//main tx key for wep.
	if(pparm->set_tx)
		pmlmeinfo->key_index = pparm->keyid;
	
	//write cam
	ctrl = BIT(15) | ((pparm->algorithm) << 2) | pparm->keyid;	

	write_cam(padapter, pparm->keyid, ctrl, null_sta, pparm->key);
	
	return H2C_SUCCESS;
}

u8 set_stakey_hdl(_adapter *padapter, u8 *pbuf)
{
	unsigned short ctrl=0;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct set_stakey_parm	*pparm = (struct set_stakey_parm *)pbuf;
#ifdef CONFIG_TDLS
	struct sta_priv	*pstapriv = &padapter->stapriv;
	struct sta_info	*psta;
#endif

	if((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE)
	{
		unsigned char cam_id;//cam_entry
		struct sta_info *psta;
		struct sta_priv *pstapriv = &padapter->stapriv;
		
		psta = rtw_get_stainfo(pstapriv, pparm->addr);
		if(psta)
		{			
			ctrl = (BIT(15) | ((pparm->algorithm) << 2));

			DBG_8192C("r871x_set_stakey_hdl(): enc_algorithm=%d\n", pparm->algorithm);

			if((psta->mac_id<1) || (psta->mac_id>(NUM_STA-4)))
			{
				DBG_8192C("r871x_set_stakey_hdl():set_stakey failed, mac_id(aid)=%d\n", psta->mac_id);
				return H2C_REJECTED;
			}	
				 
			cam_id = (psta->mac_id + 3);//0~3 for default key, cmd_id=macid + 3, macid=aid+1;

			DBG_8192C("Write CAM, mac_addr=%x:%x:%x:%x:%x:%x, cam_entry=%d\n", pparm->addr[0], 
						pparm->addr[1], pparm->addr[2], pparm->addr[3], pparm->addr[4],
						pparm->addr[5], cam_id);

			write_cam(padapter, cam_id, ctrl, pparm->addr, pparm->key);
	
			return H2C_SUCCESS_RSP;
		
		}
		else
		{
			DBG_8192C("r871x_set_stakey_hdl(): sta has been free\n");
			return H2C_REJECTED;
		}
		
	}

	//below for sta mode
	
	ctrl = BIT(15) | ((pparm->algorithm) << 2);	
	
#ifdef CONFIG_TDLS
	if(pmlmeinfo->tdls_cam_entry_to_clear!=0){
		clear_cam_entry(padapter, pmlmeinfo->tdls_cam_entry_to_clear);
		pmlmeinfo->tdls_cam_entry_to_clear=0;

		return H2C_SUCCESS;
	}

	psta = rtw_get_stainfo(pstapriv, pparm->addr);//Get TDLS Peer STA
	if((psta->state&TDLS_LINKED_STATE)==TDLS_LINKED_STATE){
		write_cam(padapter, psta->cam_entry, ctrl, pparm->addr, pparm->key);
	}
	else
#endif
	write_cam(padapter, 5, ctrl, pparm->addr, pparm->key);

	pmlmeinfo->enc_algo = pparm->algorithm;
	
	return H2C_SUCCESS;
}

u8 add_ba_hdl(_adapter *padapter, unsigned char *pbuf)
{
	struct addBaReq_parm 	*pparm = (struct addBaReq_parm *)pbuf;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	struct sta_info *psta = rtw_get_stainfo(&padapter->stapriv, pparm->addr);
	
	if(!psta)
		return 	H2C_SUCCESS;
		

	if (((pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS) && (pmlmeinfo->HT_enable)) ||
		((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE))
	{
		//pmlmeinfo->ADDBA_retry_count = 0;
		//pmlmeinfo->candidate_tid_bitmap |= (0x1 << pparm->tid);		
		//psta->htpriv.candidate_tid_bitmap |= BIT(pparm->tid);
		issue_action_BA(padapter, pparm->addr, WLAN_ACTION_ADDBA_REQ, (u16)pparm->tid);		
		//_set_timer(&pmlmeext->ADDBA_timer, ADDBA_TO);
		_set_timer(&psta->addba_retry_timer, ADDBA_TO);
	}
	else
	{		
		psta->htpriv.candidate_tid_bitmap &= ~BIT(pparm->tid);		
	}
	
	return 	H2C_SUCCESS;
}

u8 set_tx_beacon_cmd(_adapter* padapter)
{
	struct cmd_obj	*ph2c;
	struct Tx_Beacon_param 	*ptxBeacon_parm;	
	struct cmd_priv	*pcmdpriv = &(padapter->cmdpriv);
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	u8	res = _SUCCESS;
	
_func_enter_;	
	
	if ((ph2c = (struct cmd_obj*)rtw_zmalloc(sizeof(struct cmd_obj))) == NULL)
	{
		res= _FAIL;
		goto exit;
	}
	
	if ((ptxBeacon_parm = (struct Tx_Beacon_param *)rtw_zmalloc(sizeof(struct Tx_Beacon_param))) == NULL)
	{
		rtw_mfree((unsigned char *)ph2c, sizeof(struct	cmd_obj));
		res= _FAIL;
		goto exit;
	}

	_rtw_memcpy(&(ptxBeacon_parm->network), &(pmlmeinfo->network), sizeof(WLAN_BSSID_EX));
	init_h2fwcmd_w_parm_no_rsp(ph2c, ptxBeacon_parm, GEN_CMD_CODE(_TX_Beacon));

	res = rtw_enqueue_cmd(pcmdpriv, ph2c);

	
exit:
	
_func_exit_;

	return res;
}


u8 mlme_evt_hdl(_adapter *padapter, unsigned char *pbuf)
{
	u8 evt_code, evt_seq;
	u16 evt_sz;
	uint 	*peventbuf;
	void (*event_callback)(_adapter *dev, u8 *pbuf);
	struct evt_priv *pevt_priv = &(padapter->evtpriv);

	peventbuf = (uint*)pbuf;
	evt_sz = (u16)(*peventbuf&0xffff);
	evt_seq = (u8)((*peventbuf>>24)&0x7f);
	evt_code = (u8)((*peventbuf>>16)&0xff);
	
		
	#ifdef CHECK_EVENT_SEQ
	// checking event sequence...		
	if (evt_seq != (ATOMIC_READ(&pevt_priv->event_seq) & 0x7f) )
	{
		RT_TRACE(_module_rtl871x_cmd_c_,_drv_info_,("Evetn Seq Error! %d vs %d\n", (evt_seq & 0x7f), (ATOMIC_READ(&pevt_priv->event_seq) & 0x7f)));
	
		pevt_priv->event_seq = (evt_seq+1)&0x7f;

		goto _abort_event_;
	}
	#endif

	// checking if event code is valid
	if (evt_code >= MAX_C2HEVT)
	{
		RT_TRACE(_module_rtl871x_cmd_c_,_drv_err_,("\nEvent Code(%d) mismatch!\n", evt_code));
		goto _abort_event_;
	}

	// checking if event size match the event parm size	
	if ((wlanevents[evt_code].parmsize != 0) && 
			(wlanevents[evt_code].parmsize != evt_sz))
	{
			
		RT_TRACE(_module_rtl871x_cmd_c_,_drv_err_,("\nEvent(%d) Parm Size mismatch (%d vs %d)!\n", 
			evt_code, wlanevents[evt_code].parmsize, evt_sz));
		goto _abort_event_;	
			
	}

	ATOMIC_INC(&pevt_priv->event_seq);

	peventbuf += 2;
				
	if(peventbuf)
	{
		event_callback = wlanevents[evt_code].event_callback;
		event_callback(padapter, (u8*)peventbuf);

		pevt_priv->evt_done_cnt++;
	}


_abort_event_:


	return H2C_SUCCESS;
		
}

u8 h2c_msg_hdl(_adapter *padapter, unsigned char *pbuf)
{
	if(!pbuf)
		return H2C_PARAMETERS_ERROR;

	return H2C_SUCCESS;
}


u8 tx_beacon_hdl(_adapter *padapter, unsigned char *pbuf)
{
	if(send_beacon(padapter)==_FAIL)
	{
		DBG_871X("issue_beacon, fail!\n");
		return H2C_PARAMETERS_ERROR;
	}
#ifdef CONFIG_AP_MODE
	else //tx bc/mc frames after update TIM 
	{	
		_irqL irqL;
		struct sta_info *psta_bmc;
		_list	*xmitframe_plist, *xmitframe_phead;
		struct xmit_frame *pxmitframe=NULL;
		struct sta_priv  *pstapriv = &padapter->stapriv;
		
		//for BC/MC Frames
		psta_bmc = rtw_get_bcmc_stainfo(padapter);
		if(!psta_bmc)
			return H2C_SUCCESS;
	
		if((pstapriv->tim_bitmap&BIT(0)) && (psta_bmc->sleepq_len>0))
		{				

			rtw_msleep_os(10);// 10ms, ATIM(HIQ) Windows
		
			_enter_critical_bh(&psta_bmc->sleep_q.lock, &irqL);	

			xmitframe_phead = get_list_head(&psta_bmc->sleep_q);
			xmitframe_plist = get_next(xmitframe_phead);

			while ((rtw_end_of_queue_search(xmitframe_phead, xmitframe_plist)) == _FALSE)
			{			
				pxmitframe = LIST_CONTAINOR(xmitframe_plist, struct xmit_frame, list);

				xmitframe_plist = get_next(xmitframe_plist);

				rtw_list_delete(&pxmitframe->list);

				psta_bmc->sleepq_len--;
				if(psta_bmc->sleepq_len>0)
					pxmitframe->attrib.mdata = 1;
				else
					pxmitframe->attrib.mdata = 0;

				pxmitframe->attrib.triggered=1;

				if(padapter->HalFunc.hal_xmit(padapter, pxmitframe) == _TRUE)
				{		
					rtw_os_xmit_complete(padapter, pxmitframe);
				}

				//pstapriv->tim_bitmap &= ~BIT(0);				
		
			}	
	
			_exit_critical_bh(&psta_bmc->sleep_q.lock, &irqL);	

		}

	}
#endif

	return H2C_SUCCESS;
	
}

#ifdef CONFIG_AP_MODE

void init_mlme_ap_info(_adapter *padapter)
{
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);

	_rtw_spinlock_init(&pmlmepriv->bcn_update_lock);	

	//pmlmeext->bstart_bss = _FALSE;

	start_ap_mode(padapter);
}

void free_mlme_ap_info(_adapter *padapter)
{
	_irqL irqL;
	struct sta_info *psta=NULL;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	//stop_ap_mode(padapter);

	pmlmepriv->update_bcn = _FALSE;
	pmlmeext->bstart_bss = _FALSE;	
	
	rtw_sta_flush(padapter);

	pmlmeinfo->state = _HW_STATE_NOLINK_;

	//free_assoc_sta_resources
	rtw_free_all_stainfo(padapter);

	//free bc/mc sta_info
	psta = rtw_get_bcmc_stainfo(padapter);	
	_enter_critical_bh(&(pstapriv->sta_hash_lock), &irqL);		
	rtw_free_stainfo(padapter, psta);
	_exit_critical_bh(&(pstapriv->sta_hash_lock), &irqL);
	

	_rtw_spinlock_free(&pmlmepriv->bcn_update_lock);
	
}

static void update_BCNTIM(_adapter *padapter)
{
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	WLAN_BSSID_EX *pnetwork = (WLAN_BSSID_EX *)&pmlmepriv->cur_network.network;
	struct mlme_ext_priv *pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX *pnetwork_mlmeext = &(pmlmeinfo->network);
	unsigned char *src_ie = pnetwork->IEs;
	unsigned int src_ielen = pnetwork->IELength;
	unsigned char *dst_ie = pnetwork_mlmeext->IEs;
	
	
	//update TIM IE
	//if(pstapriv->tim_bitmap)
	if(_TRUE)
	{
		u8 *p, ie_len;
		u16 tim_bitmap_le;
		u32 tmp_len, head_len=0;

		tim_bitmap_le = cpu_to_le16(pstapriv->tim_bitmap);
	
		//calucate head_len		
		head_len = _FIXED_IE_LENGTH_;
		head_len += pnetwork->Ssid.SsidLength + 2;

		// get supported rates len
		p = rtw_get_ie(src_ie + _BEACON_IE_OFFSET_, _SUPPORTEDRATES_IE_, &tmp_len, (pnetwork->IELength - _BEACON_IE_OFFSET_));	
		if (p !=  NULL) 
		{			
			head_len += tmp_len+2;
		}

		//DS Parameter Set IE, len=3	
		head_len += 3;

		//copy head offset
		_rtw_memcpy(dst_ie, src_ie, head_len);
		

		//append TIM IE from head_len offset
		dst_ie+=head_len;

		*dst_ie++=_TIM_IE_;

		if((pstapriv->tim_bitmap&0xff00) && (pstapriv->tim_bitmap&0x00fc))			
			ie_len = 5;
		else
			ie_len = 4;

		*dst_ie++= ie_len;
		
		*dst_ie++=0;//DTIM count
		*dst_ie++=1;//DTIM peroid
		
		if(pstapriv->tim_bitmap&BIT(0))//for bc/mc frames
			*dst_ie++ = BIT(0);//bitmap ctrl 
		else
			*dst_ie++ = 0;

		if(ie_len==4)
		{
			*dst_ie++ = *(u8*)&tim_bitmap_le;
		}	
		else if(ie_len==5)
		{
			_rtw_memcpy(dst_ie, &tim_bitmap_le, 2);
			dst_ie+=2;				
		}	
		
		//copy remainder IE
		_rtw_memcpy(dst_ie, src_ie+head_len, src_ielen-head_len);

		//pnetwork_mlmeext->Length += ie_len+2;
		//pnetwork_mlmeext->IELength += ie_len+2;
		pnetwork_mlmeext->Length = pnetwork->Length+ie_len+2;
		pnetwork_mlmeext->IELength = src_ielen+ie_len+2;
		
	}
	else
	{
		_rtw_memcpy(dst_ie, src_ie, src_ielen);
		pnetwork_mlmeext->Length = pnetwork->Length;
		pnetwork_mlmeext->IELength = src_ielen;
	}

#ifdef CONFIG_USB_HCI
	set_tx_beacon_cmd(padapter);
#endif


/*
	if(send_beacon(padapter)==_FAIL)
	{
		DBG_871X("issue_beacon, fail!\n");
	}
*/

}

u8 chk_sta_is_alive(struct sta_info *psta)
{	
	struct stainfo_stats *pstats;

	pstats = &psta->sta_stats;

	if(pstats->rx_pkts == pstats->last_rx_pkts)
	{
		if(psta->state&WIFI_SLEEP_STATE)
			return _TRUE;
		else
			return _FALSE;
	}
	else
	{
		pstats->last_rx_pkts = pstats->rx_pkts;
		
		return _TRUE;
	}
	
}

void	expire_timeout_chk(_adapter *padapter)
{
	_list	*phead, *plist;
	struct sta_info *psta=NULL;	
	struct sta_priv *pstapriv = &padapter->stapriv;	

	phead = &pstapriv->auth_list;
	plist = get_next(phead);

	//check auth_queue
	while ((rtw_end_of_queue_search(phead, plist)) == _FALSE)	
	{
		psta = LIST_CONTAINOR(plist, struct sta_info, auth_list);
		
		plist = get_next(plist);
	
		if(psta->expire_to>0)
		{
			psta->expire_to--;
			if (psta->expire_to == 0)
			{				
				_irqL irqL;
				
				rtw_list_delete(&psta->auth_list);
				
				DBG_871X("auth expire %02X%02X%02X%02X%02X%02X\n",
					psta->hwaddr[0],psta->hwaddr[1],psta->hwaddr[2],psta->hwaddr[3],psta->hwaddr[4],psta->hwaddr[5]);
				
				_enter_critical_bh(&(pstapriv->sta_hash_lock), &irqL);	
				rtw_free_stainfo(padapter, psta);
				_exit_critical_bh(&(pstapriv->sta_hash_lock), &irqL);	
			}	
		}	
		
	}


	psta = NULL;
	
	phead = &pstapriv->asoc_list;
	plist = get_next(phead);

	//check asoc_queue
	while ((rtw_end_of_queue_search(phead, plist)) == _FALSE)	
	{
		psta = LIST_CONTAINOR(plist, struct sta_info, asoc_list);
		
		plist = get_next(plist);
	
		if(chk_sta_is_alive(psta))
		{
			psta->expire_to = pstapriv->expire_to;
		}		
	
		if(psta->expire_to>0)
		{
			psta->expire_to--;
			if (psta->expire_to == 0)
			{				
				_irqL irqL;
			
				rtw_list_delete(&psta->asoc_list);
				
				DBG_871X("asoc expire %02X%02X%02X%02X%02X%02X\n",
					psta->hwaddr[0],psta->hwaddr[1],psta->hwaddr[2],psta->hwaddr[3],psta->hwaddr[4],psta->hwaddr[5]);
#if 0
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
#endif
				ap_free_sta(padapter, psta);
				
			}	
		}	
		
	}

}


static void add_RATid(_adapter *padapter, struct sta_info *psta)
{	
	int i;
	u8 rf_type;
	u32 init_rate=0;
	unsigned char sta_band = 0, raid, shortGIrate = _FALSE;
	unsigned char limit;	
	unsigned int tx_ra_bitmap=0;
	struct ht_priv	*psta_ht = NULL;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	WLAN_BSSID_EX *pcur_network = (WLAN_BSSID_EX *)&pmlmepriv->cur_network.network;	

	
	if(psta)
		psta_ht = &psta->htpriv;
	else
		return;
	
	//b/g mode ra_bitmap  
	for (i=0; i<sizeof(psta->bssrateset); i++)
	{
		if (psta->bssrateset[i])
			tx_ra_bitmap |= rtw_get_bit_value_from_ieee_value(psta->bssrateset[i]&0x7f);
	}

	//n mode ra_bitmap
	if(psta_ht->ht_option) 
	{
		padapter->HalFunc.GetHwRegHandler(padapter, HW_VAR_RF_TYPE, (u8 *)(&rf_type));
		if(rf_type == RF_2T2R)
			limit=16;// 2R
		else
			limit=8;//  1R

		for (i=0; i<limit; i++) {
			if (psta_ht->ht_cap.supp_mcs_set[i/8] & BIT(i%8))
				tx_ra_bitmap |= BIT(i+12);
		}

		//max short GI rate
		shortGIrate = psta_ht->sgi;
	}


#if 0//gtest
	if(get_rf_mimo_mode(padapter) == RTL8712_RF_2T2R)
	{
		//is this a 2r STA?
		if((pstat->tx_ra_bitmap & 0x0ff00000) != 0 && !(priv->pshare->has_2r_sta & BIT(pstat->aid)))
		{
			priv->pshare->has_2r_sta |= BIT(pstat->aid);
			if(rtw_read16(padapter, 0x102501f6) != 0xffff)
			{
				rtw_write16(padapter, 0x102501f6, 0xffff);
				reset_1r_sta_RA(priv, 0xffff);
				Switch_1SS_Antenna(priv, 3);
			}
		}
		else// bg or 1R STA? 
		{ 
			if((priv->pmib->dot11BssType.net_work_type & WIRELESS_11N) && pstat->ht_cap_len && priv->pshare->has_2r_sta == 0)
			{
				if(rtw_read16(padapter, 0x102501f6) != 0x7777)
				{ // MCS7 SGI
					rtw_write16(padapter, 0x102501f6,0x7777);
					reset_1r_sta_RA(priv, 0x7777);
					Switch_1SS_Antenna(priv, 2);
				}
			}
		}
		
	}

	if ((pstat->rssi_level < 1) || (pstat->rssi_level > 3)) 
	{
		if (pstat->rssi >= priv->pshare->rf_ft_var.raGoDownUpper)
			pstat->rssi_level = 1;
		else if ((pstat->rssi >= priv->pshare->rf_ft_var.raGoDown20MLower) ||
			((priv->pshare->is_40m_bw) && (pstat->ht_cap_len) &&
			(pstat->rssi >= priv->pshare->rf_ft_var.raGoDown40MLower) &&
			(pstat->ht_cap_buf.ht_cap_info & cpu_to_le16(_HTCAP_SUPPORT_CH_WDTH_))))
			pstat->rssi_level = 2;
		else
			pstat->rssi_level = 3;
	}

	// rate adaptive by rssi
	if ((priv->pmib->dot11BssType.net_work_type & WIRELESS_11N) && pstat->ht_cap_len)
	{
		if ((get_rf_mimo_mode(priv) == MIMO_1T2R) || (get_rf_mimo_mode(priv) == MIMO_1T1R))
		{
			switch (pstat->rssi_level) {
				case 1:
					pstat->tx_ra_bitmap &= 0x100f0000;
					break;
				case 2:
					pstat->tx_ra_bitmap &= 0x100ff000;
					break;
				case 3:
					if (priv->pshare->is_40m_bw)
						pstat->tx_ra_bitmap &= 0x100ff005;
					else
						pstat->tx_ra_bitmap &= 0x100ff001;

					break;
			}
		}
		else 
		{
			switch (pstat->rssi_level) {
				case 1:
					pstat->tx_ra_bitmap &= 0x1f0f0000;
					break;
				case 2:
					pstat->tx_ra_bitmap &= 0x1f0ff000;
					break;
				case 3:
					if (priv->pshare->is_40m_bw)
						pstat->tx_ra_bitmap &= 0x000ff005;
					else
						pstat->tx_ra_bitmap &= 0x000ff001;

					break;
			}

			// Don't need to mask high rates due to new rate adaptive parameters
			//if (pstat->is_broadcom_sta)		// use MCS12 as the highest rate vs. Broadcom sta
			//	pstat->tx_ra_bitmap &= 0x81ffffff;

			// NIC driver will report not supporting MCS15 and MCS14 in asoc req
			//if (pstat->is_rtl8190_sta && !pstat->is_2t_mimo_sta)
			//	pstat->tx_ra_bitmap &= 0x83ffffff;		// if Realtek 1x2 sta, don't use MCS15 and MCS14
		}
	}
	else if ((priv->pmib->dot11BssType.net_work_type & WIRELESS_11G) && isErpSta(pstat))
	{
		switch (pstat->rssi_level) {
			case 1:
				pstat->tx_ra_bitmap &= 0x00000f00;
				break;
			case 2:
				pstat->tx_ra_bitmap &= 0x00000ff0;
				break;
			case 3:
				pstat->tx_ra_bitmap &= 0x00000ff5;
				break;
		}
	}
	else 
	{
		pstat->tx_ra_bitmap &= 0x0000000d;
	}

	// disable tx short GI when station cannot rx MCS15(AP is 2T2R)
	// disable tx short GI when station cannot rx MCS7 (AP is 1T2R or 1T1R)
	// if there is only 1r STA and we are 2T2R, DO NOT mask SGI rate
	if ((!(pstat->tx_ra_bitmap & 0x8000000) && (priv->pshare->has_2r_sta > 0) && (get_rf_mimo_mode(padapter) == RTL8712_RF_2T2R)) ||
		 (!(pstat->tx_ra_bitmap & 0x80000) && (get_rf_mimo_mode(padapter) != RTL8712_RF_2T2R)))
	{
		pstat->tx_ra_bitmap &= ~BIT(28);	
	}
#endif

	if ( pcur_network->Configuration.DSConfig > 14 ) {
		// 5G band
		if (tx_ra_bitmap & 0xffff000)
			sta_band |= WIRELESS_11_5N | WIRELESS_11A;
		else
			sta_band |= WIRELESS_11A;
	} else {
		if (tx_ra_bitmap & 0xffff000)
			sta_band |= WIRELESS_11_24N | WIRELESS_11G | WIRELESS_11B;
		else if (tx_ra_bitmap & 0xff0)
			sta_band |= WIRELESS_11G |WIRELESS_11B;
		else
			sta_band |= WIRELESS_11B;
	}

	raid = networktype_to_raid(sta_band);	
	init_rate = get_highest_rate_idx(tx_ra_bitmap&0x0fffffff)&0x3f;
	
	if (psta->aid < NUM_STA) 
	{
		u8 arg = 0;

		arg = psta->mac_id&0x1f;
		
		arg |= BIT(7);
		
		if (shortGIrate==_TRUE)
			arg |= BIT(5);

		tx_ra_bitmap |= ((raid<<28)&0xf0000000);

		DBG_871X("update raid entry, bitmap=0x%x, arg=0x%x\n", tx_ra_bitmap, arg);

		//bitmap[0:27] = tx_rate_bitmap
		//bitmap[28:31]= Rate Adaptive id
		//arg[0:4] = macid
		//arg[5] = Short GI
		padapter->HalFunc.Add_RateATid(padapter, tx_ra_bitmap, arg);

		if (shortGIrate==_TRUE)
			init_rate |= BIT(6);
		
		//set ra_id, init_rate
		psta->raid = raid;
		psta->init_rate = init_rate;
		
	}
	else 
	{
		DBG_871X("station aid %d exceed the max number\n", psta->aid);
	}

}

static void update_bmc_sta(_adapter *padapter)
{
	_irqL	irqL;
	u32 init_rate=0;
	unsigned char	network_type, raid;
	unsigned short para16;
	int i, supportRateNum = 0;	
	unsigned int tx_ra_bitmap=0;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	WLAN_BSSID_EX *pcur_network = (WLAN_BSSID_EX *)&pmlmepriv->cur_network.network;	
	struct sta_info *psta = rtw_get_bcmc_stainfo(padapter);

	if(psta)
	{
		psta->aid = 0;//default set to 0
		//psta->mac_id = psta->aid+4;	
		psta->mac_id = psta->aid + 1;

		psta->qos_option = 0;		
		psta->htpriv.ht_option = _FALSE;

		psta->ieee8021x_blocked = 0;

		_rtw_memset((void*)&psta->sta_stats, 0, sizeof(struct stainfo_stats));

		//psta->dot118021XPrivacy = _NO_PRIVACY_;//!!! remove it, because it has been set before this.



		//prepare for add_RATid		
		supportRateNum = rtw_get_rateset_len((u8*)&pcur_network->SupportedRates);
		network_type = rtw_check_network_type((u8*)&pcur_network->SupportedRates, supportRateNum, 1);
		
		_rtw_memcpy(psta->bssrateset, &pcur_network->SupportedRates, supportRateNum);
		psta->bssratelen = supportRateNum;

		//b/g mode ra_bitmap  
		for (i=0; i<supportRateNum; i++)
		{	
			if (psta->bssrateset[i])
				tx_ra_bitmap |= rtw_get_bit_value_from_ieee_value(psta->bssrateset[i]&0x7f);
		}

		if ( pcur_network->Configuration.DSConfig > 14 ) {
			//force to A mode. 5G doesn't support CCK rates
			network_type = WIRELESS_11A;
			tx_ra_bitmap = 0x150; // 6, 12, 24 Mbps		
		} else {
			//force to b mode 
			network_type = WIRELESS_11B;
			tx_ra_bitmap = 0xf;		
		}

		//tx_ra_bitmap = update_basic_rate(pcur_network->SupportedRates, supportRateNum);

		raid = networktype_to_raid(network_type);
		init_rate = get_highest_rate_idx(tx_ra_bitmap&0x0fffffff)&0x3f;
				
		//DBG_871X("Add id %d val %08x to ratr for bmc sta\n", psta->aid, tx_ra_bitmap);

		//if(pHalData->fw_ractrl == _TRUE)
		{
			u8 arg = 0;

			arg = psta->mac_id&0x1f;
		
			arg |= BIT(7);
		
			//if (shortGIrate==_TRUE)
			//	arg |= BIT(5);
			
			tx_ra_bitmap |= ((raid<<28)&0xf0000000);			

			DBG_871X("update_bmc_sta, mask=0x%x, arg=0x%x\n", tx_ra_bitmap, arg);

			//bitmap[0:27] = tx_rate_bitmap
			//bitmap[28:31]= Rate Adaptive id
			//arg[0:4] = macid
			//arg[5] = Short GI
			padapter->HalFunc.Add_RateATid(padapter, tx_ra_bitmap, arg);			
		
		}

		//set ra_id, init_rate
		psta->raid = raid;
		psta->init_rate = init_rate;
	 
		_enter_critical_bh(&psta->lock, &irqL);
		psta->state = _FW_LINKED;
		_exit_critical_bh(&psta->lock, &irqL);

	}
	else
	{
		DBG_871X("add_RATid_bmc_sta error!\n");
	}
		
}

//notes:
//AID: 1~MAX for sta and 0 for bc/mc in ap/adhoc mode 
//MAC_ID = AID+1 for sta in ap/adhoc mode 
//MAC_ID = 1 for bc/mc for sta/ap/adhoc
//MAC_ID = 0 for bssid for sta/ap/adhoc
//CAM_ID = //0~3 for default key, cmd_id=macid + 3, macid=aid+1;

void update_sta_info_apmode(_adapter *padapter, struct sta_info *psta)
{	
	_irqL	irqL;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct ht_priv	*phtpriv_ap = &pmlmepriv->htpriv;
	struct ht_priv	*phtpriv_sta = &psta->htpriv;

	//set intf_tag to if1
	//psta->intf_tag = 0;

	//psta->mac_id = psta->aid+4;
	psta->mac_id = psta->aid+1; 
	
	if(psecuritypriv->dot11AuthAlgrthm==dot11AuthAlgrthm_8021X)
		psta->ieee8021x_blocked = _TRUE;
	else
		psta->ieee8021x_blocked = _FALSE;
	

	//update sta's cap
	
	//ERP
	VCS_update(padapter, psta);
		
	//HT related cap
	if(phtpriv_sta->ht_option)
	{
		//check if sta supports rx ampdu
		phtpriv_sta->ampdu_enable = phtpriv_ap->ampdu_enable;

		//check if sta support s Short GI
		if((phtpriv_sta->ht_cap.cap_info & phtpriv_ap->ht_cap.cap_info) & cpu_to_le16(IEEE80211_HT_CAP_SGI_20|IEEE80211_HT_CAP_SGI_40))
		{
			phtpriv_sta->sgi = _TRUE;
		}

		// bwmode
		if((phtpriv_sta->ht_cap.cap_info & phtpriv_ap->ht_cap.cap_info) & cpu_to_le16(IEEE80211_HT_CAP_SUP_WIDTH))
		{
			//phtpriv_sta->bwmode = HT_CHANNEL_WIDTH_40;
			phtpriv_sta->bwmode = pmlmeext->cur_bwmode;
			phtpriv_sta->ch_offset = pmlmeext->cur_ch_offset;
			
		}		

		psta->qos_option = _TRUE;
		
	}
	else
	{
		phtpriv_sta->ampdu_enable = _FALSE;
		
		phtpriv_sta->sgi = _FALSE;
		phtpriv_sta->bwmode = HT_CHANNEL_WIDTH_20;
		phtpriv_sta->ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
	}

	//Rx AMPDU
	send_delba(padapter, 0, psta->hwaddr);// recipient
	
	//TX AMPDU
	send_delba(padapter, 1, psta->hwaddr);// // originator
	phtpriv_sta->agg_enable_bitmap = 0x0;//reset
	phtpriv_sta->candidate_tid_bitmap = 0x0;//reset
	

	//todo: init other variables
	
	_rtw_memset((void*)&psta->sta_stats, 0, sizeof(struct stainfo_stats));


	//add ratid
	add_RATid(padapter, psta);


	_enter_critical_bh(&psta->lock, &irqL);
	psta->state |= _FW_LINKED;
	_exit_critical_bh(&psta->lock, &irqL);
	

}

static void start_bss_network(_adapter *padapter, u8 *pbuf)
{
	u8 *p;
	u8 val8, cur_channel, cur_bwmode, cur_ch_offset;
	u16 bcn_interval;
	u32	acparm;	
	int	ie_len;	
	struct registry_priv	 *pregpriv = &padapter->registrypriv;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct security_priv* psecuritypriv=&(padapter->securitypriv);	
	WLAN_BSSID_EX *pnetwork = (WLAN_BSSID_EX *)&pmlmepriv->cur_network.network;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX *pnetwork_mlmeext = &(pmlmeinfo->network);
#ifdef CONFIG_P2P
	struct wifidirect_info	*pwdinfo = &(padapter->wdinfo);
#endif //CONFIG_P2P
	
	
	//DBG_8192C("%s\n", __FUNCTION__);

	bcn_interval = (u16)pnetwork->Configuration.BeaconPeriod;	
	cur_channel = pnetwork->Configuration.DSConfig;
	cur_bwmode = HT_CHANNEL_WIDTH_20;;
	cur_ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
	

	//check if there is wps ie, 
	//if there is wpsie in beacon, the hostapd will update beacon twice when stating hostapd,
	//and at first time the security ie ( RSN/WPA IE) will not include in beacon.
	if(NULL == rtw_get_wps_ie(pnetwork->IEs, pnetwork->IELength, NULL, &ie_len))
	{
		pmlmeext->bstart_bss = _TRUE;
	}
	
	//todo: update wmm, ht cap
	//pmlmeinfo->WMM_enable;
	//pmlmeinfo->HT_enable;
	if(pmlmepriv->qospriv.qos_option)
		pmlmeinfo->WMM_enable = _TRUE;

	if(pmlmepriv->htpriv.ht_option)
	{
		pmlmeinfo->WMM_enable = _TRUE;
		pmlmeinfo->HT_enable = _TRUE;
	}
	

	if(pmlmepriv->cur_network.join_res != _TRUE) //setting only at  first time
	{		
		flush_all_cam_entry(padapter);	//clear CAM
	}	

	//set MSR to AP_Mode		
	Set_NETYPE0_MSR(padapter, _HW_STATE_AP_);	
		
	//Set BSSID REG
	padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_BSSID, pnetwork->MacAddress);

	//Set EDCA param reg
	acparm = 0x002F3217; // VO
	padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_AC_PARAM_VO, (u8 *)(&acparm));
	acparm = 0x005E4317; // VI
	padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_AC_PARAM_VI, (u8 *)(&acparm));
	//acparm = 0x00105320; // BE
	acparm = 0x005ea42b;
	padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_AC_PARAM_BE, (u8 *)(&acparm));
	acparm = 0x0000A444; // BK
	padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_AC_PARAM_BK, (u8 *)(&acparm));

	//Set Security
	val8 = (psecuritypriv->dot11AuthAlgrthm == dot11AuthAlgrthm_8021X)? 0xcc: 0xcf;
	padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_SEC_CFG, (u8 *)(&val8));

	//Beacon Control related register
	padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_BEACON_INTERVAL, (u8 *)(&bcn_interval));

	padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_BASIC_RATE, pnetwork->SupportedRates);

	if(pmlmepriv->cur_network.join_res != _TRUE) //setting only at  first time
	{
		u32 initialgain;

		//disable dynamic functions, such as high power, DIG
		//Save_DM_Func_Flag(padapter);
		//Switch_DM_Func(padapter, DYNAMIC_FUNC_DISABLE, _FALSE);
		
		//turn on dynamic functions	
		Switch_DM_Func(padapter, DYNAMIC_FUNC_DIG|DYNAMIC_FUNC_HP|DYNAMIC_FUNC_SS, _TRUE);

		initialgain = 0x30;
		//padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_INITIAL_GAIN, (u8 *)(&initialgain));
	
	}

	//set channel, bwmode	
	p = rtw_get_ie((pnetwork->IEs + sizeof(NDIS_802_11_FIXED_IEs)), _HT_ADD_INFO_IE_, &ie_len, (pnetwork->IELength - sizeof(NDIS_802_11_FIXED_IEs)));
	if( p && ie_len)
	{
		struct HT_info_element *pht_info = (struct HT_info_element *)(p+2);
					
		if ((pregpriv->cbw40_enable) &&	 (pht_info->infos[0] & BIT(2)))
		{
			//switch to the 40M Hz mode
			//pmlmeext->cur_bwmode = HT_CHANNEL_WIDTH_40;
			cur_bwmode = HT_CHANNEL_WIDTH_40;
			switch (pht_info->infos[0] & 0x3)
			{
				case 1:
					//pmlmeext->cur_ch_offset = HAL_PRIME_CHNL_OFFSET_LOWER;
					cur_ch_offset = HAL_PRIME_CHNL_OFFSET_LOWER;
					break;
			
				case 3:
					//pmlmeext->cur_ch_offset = HAL_PRIME_CHNL_OFFSET_UPPER;
					cur_ch_offset = HAL_PRIME_CHNL_OFFSET_UPPER;					
					break;
				
				default:
					//pmlmeext->cur_ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
					cur_ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
					break;
			}		
						
		}
					
	}

	//TODO: need to judge the phy parameters on concurrent mode for single phy
	//set_channel_bwmode(padapter, pmlmeext->cur_channel, pmlmeext->cur_ch_offset, pmlmeext->cur_bwmode);
	set_channel_bwmode(padapter, cur_channel, cur_ch_offset, cur_bwmode);

	DBG_871X("CH=%d, BW=%d, offset=%d\n", cur_channel, cur_bwmode, cur_ch_offset);

	//
	pmlmeext->cur_channel = cur_channel;	
	pmlmeext->cur_bwmode = cur_bwmode;
	pmlmeext->cur_ch_offset = cur_ch_offset;	
	pmlmeext->cur_wireless_mode = pmlmepriv->cur_network.network_type;

	//let pnetwork_mlmeext == pnetwork_mlme.
	_rtw_memcpy(pnetwork_mlmeext, pnetwork, pnetwork->Length);

#ifdef CONFIG_P2P
	_rtw_memcpy(pwdinfo->p2p_group_ssid, pnetwork->Ssid.Ssid, pnetwork->Ssid.SsidLength);	
	pwdinfo->p2p_group_ssid_len = pnetwork->Ssid.SsidLength;
#endif //CONFIG_P2P

	
	if(_TRUE == pmlmeext->bstart_bss)
	{
       	        update_beacon(padapter, _TIM_IE_, NULL, _FALSE);

		//issue beacon frame
		if(send_beacon(padapter)==_FAIL)
		{
			DBG_871X("issue_beacon, fail!\n");
		}
	}


	//update bc/mc sta_info
	update_bmc_sta(padapter);
	
	//pmlmeext->bstart_bss = _TRUE;
	
}

int rtw_check_beacon_data(_adapter *padapter, u8 *pbuf,  int len)
{
	int ret=_SUCCESS;
	u8 *p;
	struct sta_info *psta = NULL;
	u16 cap, ht_cap=_FALSE;
	uint ie_len = 0;
	int group_cipher, pairwise_cipher;	
	u8	channel, network_type, supportRate[NDIS_802_11_LENGTH_RATES_EX];
	int supportRateNum = 0;
	u8 OUI1[] = {0x00, 0x50, 0xf2,0x01};
	u8 wps_oui[4]={0x0,0x50,0xf2,0x04};
	u8 WMM_PARA_IE[] = {0x00, 0x50, 0xf2, 0x02, 0x01, 0x01};	
	struct registry_priv *pregistrypriv = &padapter->registrypriv;	
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	WLAN_BSSID_EX *pbss_network = (WLAN_BSSID_EX *)&pmlmepriv->cur_network.network;	
	struct sta_priv *pstapriv = &padapter->stapriv;
	u8 *ie = pbss_network->IEs;
	

	/* SSID */
	/* Supported rates */
	/* DS Params */
	/* WLAN_EID_COUNTRY */
	/* ERP Information element */
	/* Extended supported rates */
	/* WPA/WPA2 */
	/* Wi-Fi Wireless Multimedia Extensions */
	/* ht_capab, ht_oper */
	/* WPS IE */

	DBG_8192C("%s, len=%d\n", __FUNCTION__, len);

	if(check_fwstate(pmlmepriv, WIFI_AP_STATE) != _TRUE)
		return _FAIL;


	if(len>MAX_IE_SZ)
		return _FAIL;
	
	pbss_network->IELength = len;

	_rtw_memset(ie, 0, MAX_IE_SZ);
	
	_rtw_memcpy(ie, pbuf, pbss_network->IELength);


	if(pbss_network->InfrastructureMode!=Ndis802_11APMode)
		return _FAIL;

	pbss_network->Rssi = 0;

	_rtw_memcpy(pbss_network->MacAddress, myid(&(padapter->eeprompriv)), ETH_ALEN);
	
	//beacon interval
	p = rtw_get_beacon_interval_from_ie(ie);//ie + 8;	// 8: TimeStamp, 2: Beacon Interval 2:Capability
	//pbss_network->Configuration.BeaconPeriod = le16_to_cpu(*(unsigned short*)p);
	pbss_network->Configuration.BeaconPeriod = RTW_GET_LE16(p);
	
	//capability
	//cap = *(unsigned short *)rtw_get_capability_from_ie(ie);
	//cap = le16_to_cpu(cap);
	cap = RTW_GET_LE16(ie);

	//SSID
	p = rtw_get_ie(ie + _BEACON_IE_OFFSET_, _SSID_IE_, &ie_len, (pbss_network->IELength -_BEACON_IE_OFFSET_));
	if(p && ie_len>0)
	{
		_rtw_memset(&pbss_network->Ssid, 0, sizeof(NDIS_802_11_SSID));
		_rtw_memcpy(pbss_network->Ssid.Ssid, (p + 2), ie_len);
		pbss_network->Ssid.SsidLength = ie_len;
	}	

	//chnnel
	channel = 0;
	pbss_network->Configuration.Length = 0;
	p = rtw_get_ie(ie + _BEACON_IE_OFFSET_, _DSSET_IE_, &ie_len, (pbss_network->IELength - _BEACON_IE_OFFSET_));
	if(p && ie_len>0)
		channel = *(p + 2);

	pbss_network->Configuration.DSConfig = channel;

	
	_rtw_memset(supportRate, 0, NDIS_802_11_LENGTH_RATES_EX);
	// get supported rates
	p = rtw_get_ie(ie + _BEACON_IE_OFFSET_, _SUPPORTEDRATES_IE_, &ie_len, (pbss_network->IELength - _BEACON_IE_OFFSET_));	
	if (p !=  NULL) 
	{
		_rtw_memcpy(supportRate, p+2, ie_len);	
		supportRateNum = ie_len;
	}
	
	//get ext_supported rates
	p = rtw_get_ie(ie + _BEACON_IE_OFFSET_, _EXT_SUPPORTEDRATES_IE_, &ie_len, pbss_network->IELength - _BEACON_IE_OFFSET_);	
	if (p !=  NULL)
	{
		_rtw_memcpy(supportRate+supportRateNum, p+2, ie_len);
		supportRateNum += ie_len;
	
	}

	network_type = rtw_check_network_type(supportRate, supportRateNum, channel);

	rtw_set_supported_rate(pbss_network->SupportedRates, network_type);


	//parsing ERP_IE
	p = rtw_get_ie(ie + _BEACON_IE_OFFSET_, _ERPINFO_IE_, &ie_len, (pbss_network->IELength - _BEACON_IE_OFFSET_));
	if(p && ie_len>0)
	{
		ERP_IE_handler(padapter, (PNDIS_802_11_VARIABLE_IEs)p);
	}

	//update privacy/security
	if (cap & BIT(4))
		pbss_network->Privacy = 1;
	else
		pbss_network->Privacy = 0;

	psecuritypriv->wpa_psk = 0;

	//wpa2
	group_cipher = 0; pairwise_cipher = 0;
	psecuritypriv->wpa2_group_cipher = _NO_PRIVACY_;
	psecuritypriv->wpa2_pairwise_cipher = _NO_PRIVACY_;	
	p = rtw_get_ie(ie + _BEACON_IE_OFFSET_, _RSN_IE_2_, &ie_len, (pbss_network->IELength - _BEACON_IE_OFFSET_));		
	if(p && ie_len>0)
	{
		if(rtw_parse_wpa2_ie(p, ie_len+2, &group_cipher, &pairwise_cipher) == _SUCCESS)
		{
			psecuritypriv->dot11AuthAlgrthm= dot11AuthAlgrthm_8021X;
			
			psecuritypriv->dot8021xalg = 1;//psk,  todo:802.1x
			psecuritypriv->wpa_psk |= BIT(1);

			psecuritypriv->wpa2_group_cipher = group_cipher;
			psecuritypriv->wpa2_pairwise_cipher = pairwise_cipher;
#if 0
			switch(group_cipher)
			{
				case WPA_CIPHER_NONE:				
				psecuritypriv->wpa2_group_cipher = _NO_PRIVACY_;
				break;
				case WPA_CIPHER_WEP40:				
				psecuritypriv->wpa2_group_cipher = _WEP40_;
				break;
				case WPA_CIPHER_TKIP:				
				psecuritypriv->wpa2_group_cipher = _TKIP_;
				break;
				case WPA_CIPHER_CCMP:				
				psecuritypriv->wpa2_group_cipher = _AES_;				
				break;
				case WPA_CIPHER_WEP104:					
				psecuritypriv->wpa2_group_cipher = _WEP104_;
				break;
			}

			switch(pairwise_cipher)
			{
				case WPA_CIPHER_NONE:			
				psecuritypriv->wpa2_pairwise_cipher = _NO_PRIVACY_;
				break;
				case WPA_CIPHER_WEP40:			
				psecuritypriv->wpa2_pairwise_cipher = _WEP40_;
				break;
				case WPA_CIPHER_TKIP:				
				psecuritypriv->wpa2_pairwise_cipher = _TKIP_;
				break;
				case WPA_CIPHER_CCMP:			
				psecuritypriv->wpa2_pairwise_cipher = _AES_;
				break;
				case WPA_CIPHER_WEP104:					
				psecuritypriv->wpa2_pairwise_cipher = _WEP104_;
				break;
			}
#endif			
		}
		
	}

	//wpa
	ie_len = 0;
	group_cipher = 0; pairwise_cipher = 0;
	psecuritypriv->wpa_group_cipher = _NO_PRIVACY_;
	psecuritypriv->wpa_pairwise_cipher = _NO_PRIVACY_;	
	for (p = ie + _BEACON_IE_OFFSET_; ;p += (ie_len + 2))
	{
		p = rtw_get_ie(p, _SSN_IE_1_, &ie_len, (pbss_network->IELength - _BEACON_IE_OFFSET_ - (ie_len + 2)));		
		if ((p) && (_rtw_memcmp(p+2, OUI1, 4)))
		{
			if(rtw_parse_wpa_ie(p, ie_len+2, &group_cipher, &pairwise_cipher) == _SUCCESS)
			{
				psecuritypriv->dot11AuthAlgrthm= dot11AuthAlgrthm_8021X;
				
				psecuritypriv->dot8021xalg = 1;//psk,  todo:802.1x

				psecuritypriv->wpa_psk |= BIT(0);

				psecuritypriv->wpa_group_cipher = group_cipher;
				psecuritypriv->wpa_pairwise_cipher = pairwise_cipher;

#if 0
				switch(group_cipher)
				{
					case WPA_CIPHER_NONE:					
					psecuritypriv->wpa_group_cipher = _NO_PRIVACY_;
					break;
					case WPA_CIPHER_WEP40:					
					psecuritypriv->wpa_group_cipher = _WEP40_;
					break;
					case WPA_CIPHER_TKIP:					
					psecuritypriv->wpa_group_cipher = _TKIP_;
					break;
					case WPA_CIPHER_CCMP:					
					psecuritypriv->wpa_group_cipher = _AES_;				
					break;
					case WPA_CIPHER_WEP104:					
					psecuritypriv->wpa_group_cipher = _WEP104_;
					break;
				}

				switch(pairwise_cipher)
				{
					case WPA_CIPHER_NONE:					
					psecuritypriv->wpa_pairwise_cipher = _NO_PRIVACY_;
					break;
					case WPA_CIPHER_WEP40:					
					psecuritypriv->wpa_pairwise_cipher = _WEP40_;
					break;
					case WPA_CIPHER_TKIP:					
					psecuritypriv->wpa_pairwise_cipher = _TKIP_;
					break;
					case WPA_CIPHER_CCMP:					
					psecuritypriv->wpa_pairwise_cipher = _AES_;
					break;
					case WPA_CIPHER_WEP104:					
					psecuritypriv->wpa_pairwise_cipher = _WEP104_;
					break;
				}
#endif				
			}

			break;
			
		}
			
		if ((p == NULL) || (ie_len == 0))
		{
				break;
		}
		
	}

	//wmm
	ie_len = 0;
	pmlmepriv->qospriv.qos_option = 0;
	if(pregistrypriv->wmm_enable)
	{
		for (p = ie + _BEACON_IE_OFFSET_; ;p += (ie_len + 2))
		{			
			p = rtw_get_ie(p, _VENDOR_SPECIFIC_IE_, &ie_len, (pbss_network->IELength - _BEACON_IE_OFFSET_ - (ie_len + 2)));	
			if((p) && _rtw_memcmp(p+2, WMM_PARA_IE, 6)) 
			{
				pmlmepriv->qospriv.qos_option = 1;	

				*(p+8) |= BIT(7);//QoS Info, support U-APSD
				
				break;				
			}
			
			if ((p == NULL) || (ie_len == 0))
			{
				break;
			}			
		}		
	}

	//parsing HT_CAP_IE
	p = rtw_get_ie(ie + _BEACON_IE_OFFSET_, _HT_CAPABILITY_IE_, &ie_len, (pbss_network->IELength - _BEACON_IE_OFFSET_));
	if(p && ie_len>0)
	{
		u8 rf_type;

		struct ieee80211_ht_cap *pht_cap = (struct ieee80211_ht_cap *)(p+2);

		
		ht_cap = _TRUE;
		network_type |= WIRELESS_11_24N;

	
		padapter->HalFunc.GetHwRegHandler(padapter, HW_VAR_RF_TYPE, (u8 *)(&rf_type));

		if((psecuritypriv->wpa_pairwise_cipher & WPA_CIPHER_CCMP) ||
			(psecuritypriv->wpa2_pairwise_cipher & WPA_CIPHER_CCMP))
		{
			pht_cap->ampdu_params_info |= (IEEE80211_HT_CAP_AMPDU_DENSITY&(0x07<<2)); 
		}	
		else
		{
			pht_cap->ampdu_params_info |= (IEEE80211_HT_CAP_AMPDU_DENSITY&0x00);	
		}	
		

		if(rf_type == RF_1T1R)
		{			
			pht_cap->supp_mcs_set[0] = 0xff;
			pht_cap->supp_mcs_set[1] = 0x0;				
		}

		_rtw_memcpy(&pmlmepriv->htpriv.ht_cap, p+2, ie_len);		
		
	}

	switch(network_type)
	{
		case WIRELESS_11B:
			pbss_network->NetworkTypeInUse = Ndis802_11DS;
			break;	
		case WIRELESS_11G:
		case WIRELESS_11BG:
             case WIRELESS_11G_24N:
		case WIRELESS_11BG_24N:
			pbss_network->NetworkTypeInUse = Ndis802_11OFDM24;
			break;
		case WIRELESS_11A:
			pbss_network->NetworkTypeInUse = Ndis802_11OFDM5;
			break;
		default :
			pbss_network->NetworkTypeInUse = Ndis802_11OFDM24;
			break;
	}
	
	pmlmepriv->cur_network.network_type = network_type;


	pmlmepriv->htpriv.ht_option = _FALSE;
#ifdef CONFIG_80211N_HT
	if( (psecuritypriv->wpa2_pairwise_cipher&WPA_CIPHER_TKIP) ||
		      (psecuritypriv->wpa_pairwise_cipher&WPA_CIPHER_TKIP))
	{	
		//todo:
		//ht_cap = _FALSE;
	}
		      
	//ht_cap	
	if(pregistrypriv->ht_enable && ht_cap==_TRUE)
	{		
		pmlmepriv->htpriv.ht_option = _TRUE;
		pmlmepriv->qospriv.qos_option = 1;

		if(pregistrypriv->ampdu_enable==1)
		{
			pmlmepriv->htpriv.ampdu_enable = _TRUE;
		}
	}
#endif


	pbss_network->Length = get_WLAN_BSSID_EX_sz((WLAN_BSSID_EX  *)pbss_network);

	//issue beacon to start bss network
	start_bss_network(padapter, (u8*)pbss_network);
			

	//alloc sta_info for ap itself
	psta = rtw_get_stainfo(&padapter->stapriv, pbss_network->MacAddress);
	if(!psta)
	{
		psta = rtw_alloc_stainfo(&padapter->stapriv, pbss_network->MacAddress);
		if (psta == NULL) 
		{ 
			return _FAIL;
		}	
	}	
			
	rtw_indicate_connect( padapter);

	pmlmepriv->cur_network.join_res = _TRUE;//for check if already set beacon
		
	//update bc/mc sta_info
	//update_bmc_sta(padapter);

	return ret;

}

#ifdef CONFIG_NATIVEAP_MLME

static void update_bcn_fixed_ie(_adapter *padapter)
{
	DBG_871X("%s\n", __FUNCTION__);

}

static void update_bcn_erpinfo_ie(_adapter *padapter)
{
	DBG_871X("%s\n", __FUNCTION__);

}

static void update_bcn_htcap_ie(_adapter *padapter)
{
	DBG_871X("%s\n", __FUNCTION__);

}

static void update_bcn_htinfo_ie(_adapter *padapter)
{	
	DBG_871X("%s\n", __FUNCTION__);

}

static void update_bcn_rsn_ie(_adapter *padapter)
{
	DBG_871X("%s\n", __FUNCTION__);

}

static void update_bcn_wpa_ie(_adapter *padapter)
{
	DBG_871X("%s\n", __FUNCTION__);

}

static void update_bcn_wmm_ie(_adapter *padapter)
{
	DBG_871X("%s\n", __FUNCTION__);
	
}

static void update_bcn_wps_ie(_adapter *padapter)
{
	int match;
	u8 *pwps_ie=NULL, *pwps_ie_src, *premainder_ie, *pbackup_remainder_ie=NULL;
	uint wps_ielen=0, wps_offset, remainder_ielen;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX *pnetwork = &(pmlmeinfo->network);
	unsigned char *ie = pnetwork->IEs;
	u32 ielen = pnetwork->IELength;


	DBG_871X("%s\n", __FUNCTION__);

	pwps_ie = rtw_get_wps_ie(ie, ielen, NULL, &wps_ielen);
	
	if(pwps_ie==NULL || wps_ielen==0)
		return;

	wps_offset = (uint)(pwps_ie-ie);

	premainder_ie = pwps_ie + wps_ielen;

	remainder_ielen = ielen - wps_offset - wps_ielen;

	if(remainder_ielen>0)
	{
		pbackup_remainder_ie = rtw_malloc(remainder_ielen);
		if(pbackup_remainder_ie)
			_rtw_memcpy(pbackup_remainder_ie, premainder_ie, remainder_ielen);
	}

	
	pwps_ie_src = pmlmepriv->wps_beacon_ie;
	if(pwps_ie_src == NULL)
		return;


	wps_ielen = (uint)pwps_ie_src[1];//to get ie data len
	if((wps_offset+wps_ielen+2+remainder_ielen)<=MAX_IE_SZ)
	{
		_rtw_memcpy(pwps_ie, pwps_ie_src, wps_ielen+2);
		pwps_ie += (wps_ielen+2);

		if(pbackup_remainder_ie)
			_rtw_memcpy(pwps_ie, pbackup_remainder_ie, remainder_ielen);

		//update IELength
		pnetwork->IELength = wps_offset + (wps_ielen+2) + remainder_ielen;
	}

	if(pbackup_remainder_ie)
		rtw_mfree(pbackup_remainder_ie, remainder_ielen);

}

static void update_bcn_vendor_spec_ie(_adapter *padapter, u8*oui)
{
	DBG_871X("%s\n", __FUNCTION__);

	if(_rtw_memcmp(WPA_OUI, oui, 4))
	{
		update_bcn_wpa_ie(padapter);
	}
	else if(_rtw_memcmp(WMM_OUI, oui, 4))
	{
		update_bcn_wmm_ie(padapter);
	}
	else if(_rtw_memcmp(WPS_OUI, oui, 4))
	{
		update_bcn_wps_ie(padapter);
	}
	else
	{
		DBG_871X("unknown OUI type!\n");
 	}
	
	
}

void update_beacon(_adapter *padapter, u8 ie_id, u8 *oui, u8 tx)
{
	_irqL irqL;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	
	//DBG_871X("%s\n", __FUNCTION__);

	if(_FALSE == pmlmeext->bstart_bss)
		return;

	_enter_critical_bh(&pmlmepriv->bcn_update_lock, &irqL);

	switch(ie_id)
	{
		case 0xFF:

			update_bcn_fixed_ie(padapter);//8: TimeStamp, 2: Beacon Interval 2:Capability
			
			break;
	
		case _TIM_IE_:
			
			update_BCNTIM(padapter);
			
			break;

		case _ERPINFO_IE_:

			update_bcn_erpinfo_ie(padapter);

			break;

		case _HT_CAPABILITY_IE_:

			update_bcn_htcap_ie(padapter);
			
			break;

		case _RSN_IE_2_:

			update_bcn_rsn_ie(padapter);

			break;
			
		case _HT_ADD_INFO_IE_:

			update_bcn_htinfo_ie(padapter);
			
			break;
	
		case _VENDOR_SPECIFIC_IE_:

			update_bcn_vendor_spec_ie(padapter, oui);
			
			break;
			
		default:
			break;
	}

	pmlmepriv->update_bcn = _TRUE;
	
	_exit_critical_bh(&pmlmepriv->bcn_update_lock, &irqL);		

#ifdef CONFIG_USB_HCI
	if(tx)
	{
		//send_beacon(padapter);//send_beacon must execute on TSR level
		set_tx_beacon_cmd(padapter);
	}
#else
	{	
		//PCI will issue beacon when BCN interrupt occurs.		
	}
#endif
	
}

#ifdef CONFIG_80211N_HT

/*
op_mode
Set to 0 (HT pure) under the followign conditions
	- all STAs in the BSS are 20/40 MHz HT in 20/40 MHz BSS or
	- all STAs in the BSS are 20 MHz HT in 20 MHz BSS
Set to 1 (HT non-member protection) if there may be non-HT STAs
	in both the primary and the secondary channel
Set to 2 if only HT STAs are associated in BSS,
	however and at least one 20 MHz HT STA is associated
Set to 3 (HT mixed mode) when one or more non-HT STAs are associated
	(currently non-GF HT station is considered as non-HT STA also)
*/
static int rtw_ht_operation_update(_adapter *padapter)
{
	u16 cur_op_mode, new_op_mode;
	int op_mode_changes = 0;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct ht_priv	*phtpriv_ap = &pmlmepriv->htpriv;

	if(pmlmepriv->htpriv.ht_option == _TRUE) 
		return 0;
	
	//if (!iface->conf->ieee80211n || iface->conf->ht_op_mode_fixed)
	//	return 0;

	DBG_871X("%s current operation mode=0x%X\n",
		   __FUNCTION__, pmlmepriv->ht_op_mode);

	if (!(pmlmepriv->ht_op_mode & HT_INFO_OPERATION_MODE_NON_GF_DEVS_PRESENT)
	    && pmlmepriv->num_sta_ht_no_gf) {
		pmlmepriv->ht_op_mode |=
			HT_INFO_OPERATION_MODE_NON_GF_DEVS_PRESENT;
		op_mode_changes++;
	} else if ((pmlmepriv->ht_op_mode &
		    HT_INFO_OPERATION_MODE_NON_GF_DEVS_PRESENT) &&
		   pmlmepriv->num_sta_ht_no_gf == 0) {
		pmlmepriv->ht_op_mode &=
			~HT_INFO_OPERATION_MODE_NON_GF_DEVS_PRESENT;
		op_mode_changes++;
	}

	if (!(pmlmepriv->ht_op_mode & HT_INFO_OPERATION_MODE_NON_HT_STA_PRESENT) &&
	    (pmlmepriv->num_sta_no_ht || pmlmepriv->olbc_ht)) {
		pmlmepriv->ht_op_mode |= HT_INFO_OPERATION_MODE_NON_HT_STA_PRESENT;
		op_mode_changes++;
	} else if ((pmlmepriv->ht_op_mode &
		    HT_INFO_OPERATION_MODE_NON_HT_STA_PRESENT) &&
		   (pmlmepriv->num_sta_no_ht == 0 && !pmlmepriv->olbc_ht)) {
		pmlmepriv->ht_op_mode &=
			~HT_INFO_OPERATION_MODE_NON_HT_STA_PRESENT;
		op_mode_changes++;
	}

	/* Note: currently we switch to the MIXED op mode if HT non-greenfield
	 * station is associated. Probably it's a theoretical case, since
	 * it looks like all known HT STAs support greenfield.
	 */
	new_op_mode = 0;
	if (pmlmepriv->num_sta_no_ht ||
	    (pmlmepriv->ht_op_mode & HT_INFO_OPERATION_MODE_NON_GF_DEVS_PRESENT))
		new_op_mode = OP_MODE_MIXED;
	else if ((phtpriv_ap->ht_cap.cap_info & IEEE80211_HT_CAP_SUP_WIDTH)
		 && pmlmepriv->num_sta_ht_20mhz)
		new_op_mode = OP_MODE_20MHZ_HT_STA_ASSOCED;
	else if (pmlmepriv->olbc_ht)
		new_op_mode = OP_MODE_MAY_BE_LEGACY_STAS;
	else
		new_op_mode = OP_MODE_PURE;

	cur_op_mode = pmlmepriv->ht_op_mode & HT_INFO_OPERATION_MODE_OP_MODE_MASK;
	if (cur_op_mode != new_op_mode) {
		pmlmepriv->ht_op_mode &= ~HT_INFO_OPERATION_MODE_OP_MODE_MASK;
		pmlmepriv->ht_op_mode |= new_op_mode;
		op_mode_changes++;
	}

	DBG_871X("%s new operation mode=0x%X changes=%d\n",
		   __FUNCTION__, pmlmepriv->ht_op_mode, op_mode_changes);

	return op_mode_changes;
	
}

#endif /* CONFIG_80211N_HT */


void bss_cap_update(_adapter *padapter, struct sta_info *psta)
{
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);

#if 0
	if (psta->flags & WLAN_STA_NONERP && !psta->nonerp_set) {
		psta->nonerp_set = 1;
		pmlmepriv->num_sta_non_erp++;
		if (pmlmepriv->num_sta_non_erp == 1)
			ieee802_11_set_beacons(hapd->iface);
	}
#endif

	if(psta->flags & WLAN_STA_NONERP)
	{
		if(!psta->nonerp_set)
		{
			psta->nonerp_set = 1;
			
			pmlmepriv->num_sta_non_erp++;
			
			if (pmlmepriv->num_sta_non_erp == 1)
				update_beacon(padapter, _ERPINFO_IE_, NULL, _TRUE);
		}
		
	}
	else
	{
		if(psta->nonerp_set)
		{
			psta->nonerp_set = 0;
			
			pmlmepriv->num_sta_non_erp--;
			
			if (pmlmepriv->num_sta_non_erp == 0)
				update_beacon(padapter, _ERPINFO_IE_, NULL, _TRUE);
		}
		
	}


#if 0
	if (!(psta->capability & WLAN_CAPABILITY_SHORT_SLOT) &&
	    !psta->no_short_slot_time_set) {
		psta->no_short_slot_time_set = 1;
		pmlmepriv->num_sta_no_short_slot_time++;
		if ((pmlmeext->cur_wireless_mode > WIRELESS_11B) &&
		    (pmlmepriv->num_sta_no_short_slot_time == 1))
			ieee802_11_set_beacons(hapd->iface);
	}
#endif

	if(!(psta->capability & WLAN_CAPABILITY_SHORT_SLOT))
	{
		if(!psta->no_short_slot_time_set)
		{
			psta->no_short_slot_time_set = 1;
			
			pmlmepriv->num_sta_no_short_slot_time++;
			
			if ((pmlmeext->cur_wireless_mode > WIRELESS_11B) &&
		   		 (pmlmepriv->num_sta_no_short_slot_time == 1))
						update_beacon(padapter, 0xFF, NULL, _TRUE);
			
		}
	}
	else
	{
		if(psta->no_short_slot_time_set)
		{
			psta->no_short_slot_time_set = 0;
			
			pmlmepriv->num_sta_no_short_slot_time--;
			
			if ((pmlmeext->cur_wireless_mode > WIRELESS_11B) &&
		   		 (pmlmepriv->num_sta_no_short_slot_time == 0))
						update_beacon(padapter, 0xFF, NULL, _TRUE);
		}
	}
		
	
#if 0
	if (!(psta->capability & WLAN_CAPABILITY_SHORT_PREAMBLE) &&
	    !psta->no_short_preamble_set) {
		psta->no_short_preamble_set = 1;
		pmlmepriv->num_sta_no_short_preamble++;
		if ((pmlmeext->cur_wireless_mode > WIRELESS_11B) && 
		     (pmlmepriv->num_sta_no_short_preamble == 1))
			ieee802_11_set_beacons(hapd->iface);
	}
#endif


	if(!(psta->flags & WLAN_STA_SHORT_PREAMBLE))	
	{
		if(!psta->no_short_preamble_set)
		{
			psta->no_short_preamble_set = 1;
			
			pmlmepriv->num_sta_no_short_preamble++;
			
			if ((pmlmeext->cur_wireless_mode > WIRELESS_11B) && 
		     		(pmlmepriv->num_sta_no_short_preamble == 1))
					update_beacon(padapter, 0xFF, NULL, _TRUE);
			
		}
	}
	else
	{
		if(psta->no_short_preamble_set)
		{
			psta->no_short_preamble_set = 0;
			
			pmlmepriv->num_sta_no_short_preamble--;
			
			if ((pmlmeext->cur_wireless_mode > WIRELESS_11B) && 
		     		(pmlmepriv->num_sta_no_short_preamble == 0))
					update_beacon(padapter, 0xFF, NULL, _TRUE);
			
		}
	}


#ifdef CONFIG_80211N_HT

	if (psta->flags & WLAN_STA_HT) 
	{
		u16 ht_capab = le16_to_cpu(psta->htpriv.ht_cap.cap_info);
			
		DBG_871X("HT: STA " MAC_FMT " HT Capabilities "
			   "Info: 0x%04x\n", MAC_ARG(psta->hwaddr), ht_capab);

		if (psta->no_ht_set) {
			psta->no_ht_set = 0;
			pmlmepriv->num_sta_no_ht--;
		}
		
		if ((ht_capab & IEEE80211_HT_CAP_GRN_FLD) == 0) {
			if (!psta->no_ht_gf_set) {
				psta->no_ht_gf_set = 1;
				pmlmepriv->num_sta_ht_no_gf++;
			}
			DBG_871X("%s STA " MAC_FMT " - no "
				   "greenfield, num of non-gf stations %d\n",
				   __FUNCTION__, MAC_ARG(psta->hwaddr),
				   pmlmepriv->num_sta_ht_no_gf);
		}
		
		if ((ht_capab & IEEE80211_HT_CAP_SUP_WIDTH) == 0) {
			if (!psta->ht_20mhz_set) {
				psta->ht_20mhz_set = 1;
				pmlmepriv->num_sta_ht_20mhz++;
			}
			DBG_871X("%s STA " MAC_FMT " - 20 MHz HT, "
				   "num of 20MHz HT STAs %d\n",
				   __FUNCTION__, MAC_ARG(psta->hwaddr),
				   pmlmepriv->num_sta_ht_20mhz);
		}
		
	} 
	else 
	{
		if (!psta->no_ht_set) {
			psta->no_ht_set = 1;
			pmlmepriv->num_sta_no_ht++;
		}
		if(pmlmepriv->htpriv.ht_option == _TRUE) {		
			DBG_871X("%s STA " MAC_FMT
				   " - no HT, num of non-HT stations %d\n",
				   __FUNCTION__, MAC_ARG(psta->hwaddr),
				   pmlmepriv->num_sta_no_ht);
		}
	}

	if (rtw_ht_operation_update(padapter) > 0)
	{
		update_beacon(padapter, _HT_CAPABILITY_IE_, NULL, _FALSE);
		update_beacon(padapter, _HT_ADD_INFO_IE_, NULL, _TRUE);
	}	
	
#endif /* CONFIG_80211N_HT */

}

void ap_free_sta(_adapter *padapter, struct sta_info *psta)
{
	_irqL irqL;	
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct sta_priv *pstapriv = &padapter->stapriv;

	if(!psta)
		return;


	if (psta->nonerp_set) {
		psta->nonerp_set = 0;		
		pmlmepriv->num_sta_non_erp--;
		if (pmlmepriv->num_sta_non_erp == 0)
			update_beacon(padapter, _ERPINFO_IE_, NULL, _TRUE);
	}

	if (psta->no_short_slot_time_set) {
		psta->no_short_slot_time_set = 0;
		pmlmepriv->num_sta_no_short_slot_time--;
		if (pmlmeext->cur_wireless_mode > WIRELESS_11B
		    && pmlmepriv->num_sta_no_short_slot_time == 0)
			update_beacon(padapter, 0xFF, NULL, _TRUE);
	}

	if (psta->no_short_preamble_set) {
		psta->no_short_preamble_set = 0;
		pmlmepriv->num_sta_no_short_preamble--;
		if (pmlmeext->cur_wireless_mode > WIRELESS_11B
		    && pmlmepriv->num_sta_no_short_preamble == 0)
			update_beacon(padapter, 0xFF, NULL, _TRUE);
	}
	
#ifdef CONFIG_80211N_HT

	if (psta->no_ht_gf_set) {
		psta->no_ht_gf_set = 0;
		pmlmepriv->num_sta_ht_no_gf--;
	}

	if (psta->no_ht_set) {
		psta->no_ht_set = 0;
		pmlmepriv->num_sta_no_ht--;
	}

	if (psta->ht_20mhz_set) {
		psta->ht_20mhz_set = 0;
		pmlmepriv->num_sta_ht_20mhz--;
	}

	if (rtw_ht_operation_update(padapter) > 0)
	{
		update_beacon(padapter, _HT_CAPABILITY_IE_, NULL, _FALSE);
		update_beacon(padapter, _HT_ADD_INFO_IE_, NULL, _TRUE);
	}
	
#endif /* CONFIG_80211N_HT */
		
	
	//tear down Rx AMPDU
	send_delba(padapter, 0, psta->hwaddr);// recipient
	
	//tear down TX AMPDU
	send_delba(padapter, 1, psta->hwaddr);// // originator
	psta->htpriv.agg_enable_bitmap = 0x0;//reset
	psta->htpriv.candidate_tid_bitmap = 0x0;//reset


	issue_deauth(padapter, psta->hwaddr, WLAN_REASON_DEAUTH_LEAVING);

	//report_del_sta_event(padapter, psta->hwaddr);

	//clear key
	//clear_cam_entry(padapter, (psta->mac_id + 3));

	_enter_critical_bh(&(pstapriv->sta_hash_lock), &irqL);					
	rtw_free_stainfo(padapter, psta);
	_exit_critical_bh(&(pstapriv->sta_hash_lock), &irqL);
	

}

int rtw_sta_flush(_adapter *padapter)
{
	_list	*phead, *plist;
	int ret=0;	
	struct sta_info *psta = NULL;	
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	u8 bc_addr[ETH_ALEN] = {0xff,0xff,0xff,0xff,0xff,0xff};

	DBG_871X("%s\n", __FUNCTION__);

	if((pmlmeinfo->state&0x03) != WIFI_FW_AP_STATE)
		return ret;

	phead = &pstapriv->asoc_list;
	plist = get_next(phead);
	
	//free sta asoc_queue
	while ((rtw_end_of_queue_search(phead, plist)) == _FALSE)	
	{		
		psta = LIST_CONTAINOR(plist, struct sta_info, asoc_list);
		
		plist = get_next(plist);

		rtw_list_delete(&psta->asoc_list);		

		ap_free_sta(padapter, psta);
		
	}


	issue_deauth(padapter, bc_addr, WLAN_REASON_DEAUTH_LEAVING);

	return ret;

}

void sta_info_update(_adapter *padapter, struct sta_info *psta)
{	
	int flags = psta->flags;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	
				
	//update wmm cap.
	if(WLAN_STA_WME&flags)
		psta->qos_option = 1;
	else
		psta->qos_option = 0;

	if(pmlmepriv->qospriv.qos_option == 0)	
		psta->qos_option = 0;

		
#ifdef CONFIG_80211N_HT		
	//update 802.11n ht cap.
	if(WLAN_STA_HT&flags)
	{
		psta->htpriv.ht_option = _TRUE;
		psta->qos_option = 1;	
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

void start_ap_mode(_adapter *padapter)
{
	int i;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	
	pmlmepriv->update_bcn = _FALSE;
	
	//init_mlme_ap_info(padapter);
	pmlmeext->bstart_bss = _FALSE;

	pmlmepriv->num_sta_non_erp = 0;

	pmlmepriv->num_sta_no_short_slot_time = 0;

	pmlmepriv->num_sta_no_short_preamble = 0;

	pmlmepriv->num_sta_ht_no_gf = 0;

	pmlmepriv->num_sta_no_ht = 0;
	
	pmlmepriv->num_sta_ht_20mhz = 0;

	pmlmepriv->olbc = _FALSE;

	pmlmepriv->olbc_ht = _FALSE;
	
#ifdef CONFIG_80211N_HT
	pmlmepriv->ht_op_mode = 0;
#endif

	for(i=0; i<NUM_STA; i++)
		pstapriv->sta_aid[i] = NULL;

	pmlmepriv->wps_beacon_ie = NULL;	
	pmlmepriv->wps_probe_resp_ie = NULL;
	pmlmepriv->wps_assoc_resp_ie = NULL;
	

}

void stop_ap_mode(_adapter *padapter)
{
	_irqL irqL;
	//_list	*phead, *plist;
	struct sta_info *psta=NULL;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;

	pmlmepriv->update_bcn = _FALSE;
	pmlmeext->bstart_bss = _FALSE;
	//_rtw_spinlock_free(&pmlmepriv->bcn_update_lock);

	//phead = &pstapriv->asoc_list;
	//plist = get_next(phead);
	
	rtw_sta_flush(padapter);

#if 0	
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

	//free_assoc_sta_resources	
	rtw_free_all_stainfo(padapter);
	
	psta = rtw_get_bcmc_stainfo(padapter);
	_enter_critical_bh(&(pstapriv->sta_hash_lock), &irqL);		
	rtw_free_stainfo(padapter, psta);
	_exit_critical_bh(&(pstapriv->sta_hash_lock), &irqL);
	
	rtw_init_bcmc_stainfo(padapter);	


	if(pmlmepriv->wps_beacon_ie)
	{
		rtw_mfree(pmlmepriv->wps_beacon_ie, pmlmepriv->wps_beacon_ie_len);
		pmlmepriv->wps_beacon_ie = NULL;
	}	

	if(pmlmepriv->wps_probe_resp_ie)
	{
		rtw_mfree(pmlmepriv->wps_probe_resp_ie, pmlmepriv->wps_probe_resp_ie_len);
		pmlmepriv->wps_probe_resp_ie = NULL;
	}	

	if(pmlmepriv->wps_assoc_resp_ie)
	{
		rtw_mfree(pmlmepriv->wps_assoc_resp_ie, pmlmepriv->wps_assoc_resp_ie_len);
		pmlmepriv->wps_assoc_resp_ie = NULL;
	}

}


#endif

#endif

u8 set_chplan_hdl(_adapter *padapter, unsigned char *pbuf)
{
	struct SetChannelPlan_param *setChannelPlan_param;
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	//struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	if(!pbuf)
		return H2C_PARAMETERS_ERROR;

	setChannelPlan_param = (struct SetChannelPlan_param *)pbuf;

	//Jeff: We use mlmepriv->ChannelPlan to indicate channel plan,
	//the setChannelPlan_param is useless now...

	pmlmeext->max_chan_nums = init_channel_set(padapter, pmlmepriv->ChannelPlan,pmlmeext->channel_set);

	return 	H2C_SUCCESS;
}

