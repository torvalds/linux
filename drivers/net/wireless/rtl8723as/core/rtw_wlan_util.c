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
#define _RTW_WLAN_UTIL_C_

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <wifi.h>


unsigned char ARTHEROS_OUI1[] = {0x00, 0x03, 0x7f};
unsigned char ARTHEROS_OUI2[] = {0x00, 0x13, 0x74};

unsigned char BROADCOM_OUI1[] = {0x00, 0x10, 0x18};
unsigned char BROADCOM_OUI2[] = {0x00, 0x0a, 0xf7};
unsigned char BROADCOM_OUI3[] = {0x00, 0x05, 0xb5};

unsigned char CISCO_OUI[] = {0x00, 0x40, 0x96};
unsigned char MARVELL_OUI[] = {0x00, 0x50, 0x43};
unsigned char RALINK_OUI[] = {0x00, 0x0c, 0x43};
unsigned char REALTEK_OUI[] = {0x00, 0xe0, 0x4c};
unsigned char AIRGOCAP_OUI[] = {0x00, 0x0a, 0xf5};

unsigned char REALTEK_96B_IE[] = {0x00, 0xe0, 0x4c, 0x02, 0x01, 0x20};

extern unsigned char	MCS_rate_2R[16];
extern unsigned char	MCS_rate_1R[16];
extern unsigned char RTW_WPA_OUI[];
extern unsigned char WPA_TKIP_CIPHER[4];
extern unsigned char RSN_TKIP_CIPHER[4];

#define R2T_PHY_DELAY	(0)

//#define WAIT_FOR_BCN_TO_MIN	(3000)
#define WAIT_FOR_BCN_TO_MIN	(6000)
#define WAIT_FOR_BCN_TO_MAX	(20000)


int cckrates_included(unsigned char *rate, int ratelen)
{
	int	i;
	
	for(i = 0; i < ratelen; i++)
	{
		if  (  (((rate[i]) & 0x7f) == 2)	|| (((rate[i]) & 0x7f) == 4) ||
			   (((rate[i]) & 0x7f) == 11)  || (((rate[i]) & 0x7f) == 22) )
		return _TRUE;	
	}

	return _FALSE;

}

int cckratesonly_included(unsigned char *rate, int ratelen)
{
	int	i;
	
	for(i = 0; i < ratelen; i++)
	{
		if  ( (((rate[i]) & 0x7f) != 2) && (((rate[i]) & 0x7f) != 4) &&
			   (((rate[i]) & 0x7f) != 11)  && (((rate[i]) & 0x7f) != 22) )
		return _FALSE;	
	}
	
	return _TRUE;
}

unsigned char networktype_to_raid(unsigned char network_type)
{
	unsigned char raid;

	switch(network_type)
	{
		case WIRELESS_11B:
			raid = 6;
			break;
		case WIRELESS_11A:
		case WIRELESS_11G:
			raid = 5;
			break;
		case WIRELESS_11BG:
			raid = 4;
			break;
		case WIRELESS_11_24N:
		case WIRELESS_11_5N:
			raid = 3;
			break;
		case WIRELESS_11A_5N:
		case WIRELESS_11G_24N:
			raid = 1;
			break;
		case WIRELESS_11BG_24N:
			raid = 0;
			break;
		default:
			raid = 4;
			break;	

	}

	return raid;
	
}

int judge_network_type(_adapter *padapter, unsigned char *rate, int ratelen)
{
	int network_type = 0;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	

	if(pmlmeext->cur_channel > 14)
	{
		if (pmlmeinfo->HT_enable)
		{
			network_type = WIRELESS_11_5N;
		}
	
		network_type |= WIRELESS_11A;
	}
	else
	{
		if (pmlmeinfo->HT_enable)
		{
			network_type = WIRELESS_11_24N;
		}

		if ((cckratesonly_included(rate, ratelen)) == _TRUE)
		{
			network_type |= WIRELESS_11B;
		}
		else if((cckrates_included(rate, ratelen)) == _TRUE)
		{
			network_type |= WIRELESS_11BG;
		}
		else
		{
			network_type |= WIRELESS_11G;
		}
	}
		
	return 	network_type;
}

unsigned char ratetbl_val_2wifirate(unsigned char rate);
unsigned char ratetbl_val_2wifirate(unsigned char rate)
{
	unsigned char val = 0;

	switch (rate & 0x7f) 
	{
		case 0:
			val = IEEE80211_CCK_RATE_1MB;
			break;

		case 1:
			val = IEEE80211_CCK_RATE_2MB;
			break;

		case 2:
			val = IEEE80211_CCK_RATE_5MB;
			break;

		case 3:
			val = IEEE80211_CCK_RATE_11MB;
			break;
			
		case 4:
			val = IEEE80211_OFDM_RATE_6MB;
			break;

		case 5:
			val = IEEE80211_OFDM_RATE_9MB;
			break;

		case 6:
			val = IEEE80211_OFDM_RATE_12MB;
			break;
			
		case 7:
			val = IEEE80211_OFDM_RATE_18MB;
			break;

		case 8:
			val = IEEE80211_OFDM_RATE_24MB;
			break;
			
		case 9:
			val = IEEE80211_OFDM_RATE_36MB;
			break;

		case 10:
			val = IEEE80211_OFDM_RATE_48MB;
			break;
		
		case 11:
			val = IEEE80211_OFDM_RATE_54MB;
			break;

	}

	return val;

}

int is_basicrate(_adapter *padapter, unsigned char rate);
int is_basicrate(_adapter *padapter, unsigned char rate)
{
	int i;
	unsigned char val;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	
	for(i = 0; i < NumRates; i++)
	{
		val = pmlmeext->basicrate[i];

		if ((val != 0xff) && (val != 0xfe))
		{
			if (rate == ratetbl_val_2wifirate(val))
			{
				return _TRUE;
			}
		}
	}
	
	return _FALSE;
}

unsigned int ratetbl2rateset(_adapter *padapter, unsigned char *rateset);
unsigned int ratetbl2rateset(_adapter *padapter, unsigned char *rateset)
{
	int i;
	unsigned char rate;
	unsigned int	len = 0;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;

	for (i = 0; i < NumRates; i++)
	{
		rate = pmlmeext->datarate[i];

		switch (rate)
		{
			case 0xff:
				return len;
				
			case 0xfe:
				continue;
				
			default:
				rate = ratetbl_val_2wifirate(rate);

				if (is_basicrate(padapter, rate) == _TRUE)
				{
					rate |= IEEE80211_BASIC_RATE_MASK;
				}
				
				rateset[len] = rate;
				len++;
				break;
		}
	}
	return len;
}


void get_rate_set(_adapter *padapter, unsigned char *pbssrate, int *bssrate_len)
{
	unsigned char supportedrates[NumRates];

	_rtw_memset(supportedrates, 0, NumRates);
	*bssrate_len = ratetbl2rateset(padapter, supportedrates);
	_rtw_memcpy(pbssrate, supportedrates, *bssrate_len);
}

void Save_DM_Func_Flag(_adapter *padapter)
{
	u8	bSaveFlag = _TRUE;

#ifdef CONFIG_CONCURRENT_MODE	
	_adapter *pbuddy_adapter = padapter->pbuddy_adapter;
	if(pbuddy_adapter)
	pbuddy_adapter->HalFunc.SetHwRegHandler(pbuddy_adapter, HW_VAR_DM_FUNC_OP, (u8 *)(&bSaveFlag));
#endif

	padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_DM_FUNC_OP, (u8 *)(&bSaveFlag));

}

void Restore_DM_Func_Flag(_adapter *padapter)
{
	u8	bSaveFlag = _FALSE;
#ifdef CONFIG_CONCURRENT_MODE	
	_adapter *pbuddy_adapter = padapter->pbuddy_adapter;
	if(pbuddy_adapter)
	pbuddy_adapter->HalFunc.SetHwRegHandler(pbuddy_adapter, HW_VAR_DM_FUNC_OP, (u8 *)(&bSaveFlag));
#endif
	padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_DM_FUNC_OP, (u8 *)(&bSaveFlag));
}

void Switch_DM_Func(_adapter *padapter, u32 mode, u8 enable)
{
#ifdef CONFIG_CONCURRENT_MODE	
	_adapter *pbuddy_adapter = padapter->pbuddy_adapter;
#endif

	if(enable == _TRUE)
	{
#ifdef CONFIG_CONCURRENT_MODE
		if(pbuddy_adapter)
		pbuddy_adapter->HalFunc.SetHwRegHandler(pbuddy_adapter, HW_VAR_DM_FUNC_SET, (u8 *)(&mode));
#endif
		padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_DM_FUNC_SET, (u8 *)(&mode));
	}
	else
	{
#ifdef CONFIG_CONCURRENT_MODE
		if(pbuddy_adapter)
		pbuddy_adapter->HalFunc.SetHwRegHandler(pbuddy_adapter, HW_VAR_DM_FUNC_CLR, (u8 *)(&mode));
#endif
		padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_DM_FUNC_CLR, (u8 *)(&mode));
	}

#if 0
	u8 val8;

	val8 = rtw_read8(padapter, FW_DYNAMIC_FUN_SWITCH);

	if(enable == _TRUE)
	{
		rtw_write8(padapter, FW_DYNAMIC_FUN_SWITCH, (val8 | mode));
	}
	else
	{
		rtw_write8(padapter, FW_DYNAMIC_FUN_SWITCH, (val8 & mode));
	}
#endif

}

static void Set_NETYPE1_MSR(_adapter *padapter, u8 type)
{
	padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_MEDIA_STATUS1, (u8 *)(&type));
}

static void Set_NETYPE0_MSR(_adapter *padapter, u8 type)
{
	padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_MEDIA_STATUS, (u8 *)(&type));
}

void Set_MSR(_adapter *padapter, u8 type)
{
#ifdef CONFIG_CONCURRENT_MODE
	if(padapter->iface_type == IFACE_PORT1)
	{
		Set_NETYPE1_MSR(padapter, type);
	}
	else
#endif
	{
		Set_NETYPE0_MSR(padapter, type);
	}
}

void SelectChannel(_adapter *padapter, unsigned char channel)
{
#ifdef CONFIG_DUALMAC_CONCURRENT
	dc_SelectChannel(padapter, channel);
#else

	unsigned int scanMode;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	
#ifdef CONFIG_CONCURRENT_MODE
	_enter_critical_mutex(padapter->psetch_mutex, NULL);
#endif
	
	scanMode = (pmlmeext->sitesurvey_res.scan_mode == SCAN_ACTIVE)? 1: 0;//todo:

	if(padapter->HalFunc.set_channel_handler)
		padapter->HalFunc.set_channel_handler(padapter, channel);
	

#ifdef CONFIG_CONCURRENT_MODE
	_exit_critical_mutex(padapter->psetch_mutex, NULL);
#endif
		
#endif // CONFIG_DUALMAC_CONCURRENT
}

void SetBWMode(_adapter *padapter, unsigned short bwmode, unsigned char channel_offset)
{
#ifdef CONFIG_DUALMAC_CONCURRENT
	dc_SetBWMode(padapter, bwmode, channel_offset);
#else

#ifdef CONFIG_CONCURRENT_MODE
	_enter_critical_mutex(padapter->psetbw_mutex, NULL);
#endif

	if(padapter->HalFunc.set_bwmode_handler)
		padapter->HalFunc.set_bwmode_handler(padapter, (HT_CHANNEL_WIDTH)bwmode, channel_offset);

#ifdef CONFIG_CONCURRENT_MODE
	_exit_critical_mutex(padapter->psetbw_mutex, NULL);
#endif

#endif // CONFIG_DUALMAC_CONCURRENT
}

void set_channel_bwmode(_adapter *padapter, unsigned char channel, unsigned char channel_offset, unsigned short bwmode)
{
	if((bwmode == HT_CHANNEL_WIDTH_20)||(channel_offset == HAL_PRIME_CHNL_OFFSET_DONT_CARE))
	{
		SelectChannel(padapter, channel);
	}
	else		
	{
		//switch to the proper channel
		if (channel_offset == HAL_PRIME_CHNL_OFFSET_LOWER)
		{
			SelectChannel(padapter, channel + 2);
		}
		else
		{
			SelectChannel(padapter, channel - 2);
		}
	}	

	
	SetBWMode(padapter, bwmode, channel_offset);
	
}

int get_bsstype(unsigned short capability)
{
	if (capability & BIT(0))
	{
		return WIFI_FW_AP_STATE;
	}
	else if (capability & BIT(1))
	{
		return WIFI_FW_ADHOC_STATE;
	}
	else
	{
		return 0;		
	}
}

__inline u8 *get_my_bssid(WLAN_BSSID_EX *pnetwork)
{	
	return (pnetwork->MacAddress); 
}

u16 get_beacon_interval(WLAN_BSSID_EX *bss)
{
	unsigned short val;
	_rtw_memcpy((unsigned char *)&val, rtw_get_beacon_interval_from_ie(bss->IEs), 2);

	return le16_to_cpu(val);	

}

int is_client_associated_to_ap(_adapter *padapter)
{
	struct mlme_ext_priv	*pmlmeext;
	struct mlme_ext_info	*pmlmeinfo;

	if(!padapter)
		return _FAIL;

	pmlmeext = &padapter->mlmeextpriv;
	pmlmeinfo = &(pmlmeext->mlmext_info);
	
	if ((pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS) && ((pmlmeinfo->state&0x03) == WIFI_FW_STATION_STATE))
	{
		return _TRUE;
	}
	else
	{
		return _FAIL;
	}
}

int is_client_associated_to_ibss(_adapter *padapter)
{
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	
	if ((pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS) && ((pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE))
	{
		return _TRUE;
	}
	else
	{
		return _FAIL;
	}
}

int is_IBSS_empty(_adapter *padapter)
{
	unsigned int i;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	
	for (i = IBSS_START_MAC_ID; i < NUM_STA; i++)
	{
		if (pmlmeinfo->FW_sta_info[i].status == 1)
		{
			return _FAIL;
		}
	}
	
	return _TRUE;
	
}

unsigned int decide_wait_for_beacon_timeout(unsigned int bcn_interval)
{
	if ((bcn_interval << 2) < WAIT_FOR_BCN_TO_MIN)
	{
		return WAIT_FOR_BCN_TO_MIN;
	} 
	else if ((bcn_interval << 2) > WAIT_FOR_BCN_TO_MAX)
	{
		return WAIT_FOR_BCN_TO_MAX;
	}	
	else
	{
		return ((bcn_interval << 2));
	}
}

void CAM_empty_entry(
	PADAPTER     	Adapter,	
	u8 			ucIndex
)
{
	Adapter->HalFunc.SetHwRegHandler(Adapter, HW_VAR_CAM_EMPTY_ENTRY, (u8 *)(&ucIndex));
}

void invalidate_cam_all(_adapter *padapter)
{
	padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_CAM_INVALID_ALL, 0);
}
#if 0
static u32 _ReadCAM(_adapter *padapter ,u32 addr)
{
	u32 count = 0, cmd;
	cmd = CAM_POLLINIG |addr ;
	rtw_write32(padapter, RWCAM, cmd);

	do{
		if(0 == (rtw_read32(padapter,REG_CAMCMD) & CAM_POLLINIG)){
			break;
		}
	}while(count++ < 100);		

	return rtw_read32(padapter,REG_CAMREAD);	
}
void read_cam(_adapter *padapter ,u8 entry)
{
	u32	j,count = 0, addr, cmd;
	addr = entry << 3;

	printk("********* DUMP CAM Entry_#%02d***************\n",entry);
	for (j = 0; j < 6; j++)
	{	
		cmd = _ReadCAM(padapter ,addr+j);
		printk("offset:0x%02x => 0x%08x \n",addr+j,cmd);
	}
	printk("*********************************\n");
}
#endif

void write_cam(_adapter *padapter, u8 entry, u16 ctrl, u8 *mac, u8 *key)
{
	unsigned int	i, val, addr;
	//unsigned int    cmd;
	int j;
	u32	cam_val[2];

	addr = entry << 3;

	for (j = 5; j >= 0; j--)
	{	
		switch (j)
		{
			case 0:
				val = (ctrl | (mac[0] << 16) | (mac[1] << 24) );
				break;
				
			case 1:
				val = (mac[2] | ( mac[3] << 8) | (mac[4] << 16) | (mac[5] << 24));
				break;
			
			default:
				i = (j - 2) << 2;
				val = (key[i] | (key[i+1] << 8) | (key[i+2] << 16) | (key[i+3] << 24));
				break;
				
		}

		cam_val[0] = val;
		cam_val[1] = addr + (unsigned int)j;

		padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_CAM_WRITE, (u8 *)cam_val);
		
		//rtw_write32(padapter, WCAMI, val);
		
		//cmd = CAM_POLLINIG | CAM_WRITE | (addr + j);
		//rtw_write32(padapter, RWCAM, cmd);
		
		//DBG_871X("%s=> cam write: %x, %x\n",__FUNCTION__, cmd, val);
		
	}

}

void clear_cam_entry(_adapter *padapter, u8 entry)
{	
#if 0
	u32	addr, val=0;
	u32	cam_val[2];

	addr = entry << 3;
	

	cam_val[0] = val;
	cam_val[1] = addr + (unsigned int)0;

	padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_CAM_WRITE, (u8 *)cam_val);



	cam_val[0] = val;
	cam_val[1] = addr + (unsigned int)1;

	padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_CAM_WRITE, (u8 *)cam_val);
#else

	unsigned char null_sta[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

	unsigned char null_key[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,0x00, 0x00, 0x00, 0x00,0x00, 0x00, 0x00, 0x00};

	write_cam(padapter, entry, 0, null_sta, null_key);

#endif
}

int allocate_fw_sta_entry(_adapter *padapter)
{
	unsigned int mac_id;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	
	for (mac_id = IBSS_START_MAC_ID; mac_id < NUM_STA; mac_id++)
	{
		if (pmlmeinfo->FW_sta_info[mac_id].status == 0)
		{
			pmlmeinfo->FW_sta_info[mac_id].status = 1;
			pmlmeinfo->FW_sta_info[mac_id].retry = 0;
			break;
		}
	}
	
	return mac_id;
}

void flush_all_cam_entry(_adapter *padapter)
{
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

#ifdef CONFIG_CONCURRENT_MODE

	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);

	//if(check_buddy_mlmeinfo_state(padapter, _HW_STATE_NOLINK_))	
	if(check_buddy_fwstate(padapter, _FW_LINKED) == _FALSE)
	{
		padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_CAM_INVALID_ALL, 0);		
	}
	else
	{
		if(check_fwstate(pmlmepriv, WIFI_STATION_STATE) == _TRUE)
		{
			struct sta_priv	*pstapriv = &padapter->stapriv;
			struct sta_info	*psta;
			u8 cam_id;//cam_entry

			psta = rtw_get_stainfo(pstapriv, pmlmeinfo->network.MacAddress);

			if(psta && psta->mac_id==2)
			{
				cam_id = 5;
			}
			else
			{
				cam_id = 4;
			}

			//clear_cam_entry(padapter, cam_id);
			rtw_clearstakey_cmd(padapter, (u8*)psta, cam_id, _FALSE);

		}
		else if(check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE)
		{
			//clear cam when ap free per sta_info 
		}			
	}
#else //CONFIG_CONCURRENT_MODE

	padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_CAM_INVALID_ALL, 0);	

#endif //CONFIG_CONCURRENT_MODE

	_rtw_memset((u8 *)(pmlmeinfo->FW_sta_info), 0, sizeof(pmlmeinfo->FW_sta_info));
	
}

#ifdef CONFIG_WFD
int WFD_info_handler(_adapter *padapter, PNDIS_802_11_VARIABLE_IEs	pIE)
{
	struct registry_priv	*pregpriv = &padapter->registrypriv;
	struct mlme_priv	*pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wifidirect_info	*pwdinfo;	
	u8	wfd_ie[ 128 ] = { 0x00 };
	u32	wfd_ielen = 0;


	pwdinfo = &padapter->wdinfo;
	if ( rtw_get_wfd_ie( ( u8* ) pIE, pIE->Length, wfd_ie, &wfd_ielen ) )
	{
		u8	attr_content[ 10 ] = { 0x00 };
		u32	attr_contentlen = 0;
			
		DBG_871X( "[%s] Found WFD IE\n", __FUNCTION__ );
		rtw_get_wfd_attr_content( wfd_ie, wfd_ielen, WFD_ATTR_DEVICE_INFO, attr_content, &attr_contentlen);
		if ( attr_contentlen )
		{
			pwdinfo->wfd_info.peer_rtsp_ctrlport = RTW_GET_BE16( attr_content + 2 );
			DBG_8192C( "[%s] Peer PORT NUM = %d\n", __FUNCTION__, pwdinfo->wfd_info.peer_rtsp_ctrlport );
			return( _TRUE );
		}		
	}
	else
	{
		DBG_871X( "[%s] NO WFD IE\n", __FUNCTION__ );

	}
	return( _FAIL );
}
#endif

int WMM_param_handler(_adapter *padapter, PNDIS_802_11_VARIABLE_IEs	pIE)
{
	//struct registry_priv	*pregpriv = &padapter->registrypriv;
	struct mlme_priv	*pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);	
	
	if(pmlmepriv->qospriv.qos_option==0)
	{
		pmlmeinfo->WMM_enable = 0;
		return _FAIL;
	}	

	pmlmeinfo->WMM_enable = 1;
	_rtw_memcpy(&(pmlmeinfo->WMM_param), (pIE->data + 6), sizeof(struct WMM_para_element));
	return _TRUE;

	/*if (pregpriv->wifi_spec == 1)
	{
		if (pmlmeinfo->WMM_enable == 1)
		{
			//todo: compare the parameter set count & decide wheher to update or not
			return _FAIL;
		}
		else
		{
			pmlmeinfo->WMM_enable = 1;
			_rtw_rtw_memcpy(&(pmlmeinfo->WMM_param), (pIE->data + 6), sizeof(struct WMM_para_element));
			return _TRUE;
		}
	}
	else
	{
		pmlmeinfo->WMM_enable = 0;
		return _FAIL;
	}*/
	
}

void WMMOnAssocRsp(_adapter *padapter)
{
	u8	ACI, ACM, AIFS, ECWMin, ECWMax, aSifsTime;
	u8	acm_mask;
	u16	TXOP;
	u32	acParm, i;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	if (pmlmeinfo->WMM_enable == 0)
	{
		padapter->mlmepriv.acm_mask = 0;
		return;
	}

	acm_mask = 0;

	if( pmlmeext->cur_wireless_mode == WIRELESS_11B)
		aSifsTime = 10;
	else
		aSifsTime = 16;

	for (i = 0; i < 4; i++)  
	{
		ACI = (pmlmeinfo->WMM_param.ac_param[i].ACI_AIFSN >> 5) & 0x03;
		ACM = (pmlmeinfo->WMM_param.ac_param[i].ACI_AIFSN >> 4) & 0x01;

		//AIFS = AIFSN * slot time + SIFS - r2t phy delay
		AIFS = (pmlmeinfo->WMM_param.ac_param[i].ACI_AIFSN & 0x0f) * pmlmeinfo->slotTime + aSifsTime;

		ECWMin = (pmlmeinfo->WMM_param.ac_param[i].CW & 0x0f);
		ECWMax = (pmlmeinfo->WMM_param.ac_param[i].CW & 0xf0) >> 4;
		TXOP = le16_to_cpu(pmlmeinfo->WMM_param.ac_param[i].TXOP_limit);

		acParm = AIFS | (ECWMin << 8) | (ECWMax << 12) | (TXOP << 16);

		switch (ACI)
		{
			case 0x0:
				padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_AC_PARAM_BE, (u8 *)(&acParm));
				acm_mask |= (ACM? BIT(1):0);
				break;

			case 0x1:
				padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_AC_PARAM_BK, (u8 *)(&acParm));
				//acm_mask |= (ACM? BIT(0):0);
				break;

			case 0x2:
				padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_AC_PARAM_VI, (u8 *)(&acParm));
				acm_mask |= (ACM? BIT(2):0);
				break;

			case 0x3:
				padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_AC_PARAM_VO, (u8 *)(&acParm));
				acm_mask |= (ACM? BIT(3):0);
				break;							
		}

		DBG_871X("WMM(%x): %x, %x\n", ACI, ACM, acParm);
	}

	if(padapter->registrypriv.acm_method == 1)
		padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_ACM_CTRL, (u8 *)(&acm_mask));
	else
		padapter->mlmepriv.acm_mask = acm_mask;

	return;	
}

static void bwmode_update_check(_adapter *padapter, PNDIS_802_11_VARIABLE_IEs pIE)
{
	unsigned char	 new_bwmode;
	unsigned char  new_ch_offset;
	struct HT_info_element	 *pHT_info;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	

	if(!pIE)
		return;

	pHT_info = (struct HT_info_element *)pIE->data;

	if(pHT_info->infos[0] & BIT(2))
	{
		new_bwmode = HT_CHANNEL_WIDTH_40;
		switch (pHT_info->infos[0] & 0x3)
		{
			case 1:
				new_ch_offset = HAL_PRIME_CHNL_OFFSET_LOWER;
				break;
			
			case 3:
				new_ch_offset = HAL_PRIME_CHNL_OFFSET_UPPER;
				break;
				
			default:
				new_ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
				break;
		}
	}
	else
	{
		new_bwmode = HT_CHANNEL_WIDTH_20;
		new_ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
	}	

	
	if((new_bwmode!= pmlmeext->cur_bwmode) || (new_ch_offset!=pmlmeext->cur_ch_offset))
	{
		pmlmeinfo->bwmode_updated = _TRUE;
		
		pmlmeext->cur_bwmode = new_bwmode;
		pmlmeext->cur_ch_offset = new_ch_offset;
	}
	else
	{
		pmlmeinfo->bwmode_updated = _FALSE;
	}
		

	if(_TRUE == pmlmeinfo->bwmode_updated)
	{
		struct sta_info *psta;
		WLAN_BSSID_EX 	*cur_network = &(pmlmeinfo->network);
		struct sta_priv	*pstapriv = &padapter->stapriv;
	
		//set_channel_bwmode(padapter, pmlmeext->cur_channel, pmlmeext->cur_ch_offset, pmlmeext->cur_bwmode);

		
		//update ap's stainfo
		psta = rtw_get_stainfo(pstapriv, cur_network->MacAddress);
		if(psta)
		{
			struct ht_priv	*phtpriv_sta = &psta->htpriv;
			
			if(phtpriv_sta->ht_option)
			{				
				// bwmode				
				phtpriv_sta->bwmode = pmlmeext->cur_bwmode;
				phtpriv_sta->ch_offset = pmlmeext->cur_ch_offset;		
			}
			else
			{
				phtpriv_sta->bwmode = HT_CHANNEL_WIDTH_20;
				phtpriv_sta->ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
			}
			
		}

		//pmlmeinfo->bwmode_updated = _FALSE;//bwmode_updated done, reset it!
		
	}	

}

void HT_caps_handler(_adapter *padapter, PNDIS_802_11_VARIABLE_IEs pIE)
{
	unsigned int	i;
	u8	rf_type;
	u8	max_AMPDU_len, min_MPDU_spacing;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct mlme_priv 		*pmlmepriv = &padapter->mlmepriv;	
	struct ht_priv			*phtpriv = &pmlmepriv->htpriv;

	if(pIE==NULL) return;
	
	if(phtpriv->ht_option == _FALSE)	return;

	pmlmeinfo->HT_caps_enable = 1;
	
	for (i = 0; i < (pIE->Length); i++)
	{
		if (i != 2)
		{
			//	Commented by Albert 2010/07/12
			//	Got the endian issue here.
			pmlmeinfo->HT_caps.u.HT_cap[i] &= (pIE->data[i]);
		}
		else
		{
			//modify from  fw by Thomas 2010/11/17
			if ((pmlmeinfo->HT_caps.u.HT_cap_element.AMPDU_para & 0x3) > (pIE->data[i] & 0x3))
			{
				max_AMPDU_len = (pIE->data[i] & 0x3);
			}
			else
			{
				max_AMPDU_len = (pmlmeinfo->HT_caps.u.HT_cap_element.AMPDU_para & 0x3);
			}
			
			if ((pmlmeinfo->HT_caps.u.HT_cap_element.AMPDU_para & 0x1c) > (pIE->data[i] & 0x1c))
			{
				min_MPDU_spacing = (pmlmeinfo->HT_caps.u.HT_cap_element.AMPDU_para & 0x1c);
			}
			else
			{
				min_MPDU_spacing = (pIE->data[i] & 0x1c);
			}

			pmlmeinfo->HT_caps.u.HT_cap_element.AMPDU_para = max_AMPDU_len | min_MPDU_spacing;
		}
	}

	//	Commented by Albert 2010/07/12
	//	Have to handle the endian issue after copying.
	//	HT_ext_caps didn't be used yet.	
	pmlmeinfo->HT_caps.u.HT_cap_element.HT_caps_info = le16_to_cpu( pmlmeinfo->HT_caps.u.HT_cap_element.HT_caps_info );
	pmlmeinfo->HT_caps.u.HT_cap_element.HT_ext_caps = le16_to_cpu( pmlmeinfo->HT_caps.u.HT_cap_element.HT_ext_caps );

	padapter->HalFunc.GetHwRegHandler(padapter, HW_VAR_RF_TYPE, (u8 *)(&rf_type));

	//update the MCS rates
	for (i = 0; i < 16; i++)
	{
		if((rf_type == RF_1T1R) || (rf_type == RF_1T2R))
		{
			pmlmeinfo->HT_caps.u.HT_cap_element.MCS_rate[i] &= MCS_rate_1R[i];
		}
		else
		{
			pmlmeinfo->HT_caps.u.HT_cap_element.MCS_rate[i] &= MCS_rate_2R[i];
		}
	        #ifdef RTL8192C_RECONFIG_TO_1T1R
		{
			pmlmeinfo->HT_caps.HT_cap_element.MCS_rate[i] &= MCS_rate_1R[i];
		}
		#endif
	}
	
	return;
}

void HT_info_handler(_adapter *padapter, PNDIS_802_11_VARIABLE_IEs pIE)
{
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct mlme_priv 		*pmlmepriv = &padapter->mlmepriv;	
	struct ht_priv			*phtpriv = &pmlmepriv->htpriv;

	if(pIE==NULL) return;

	if(phtpriv->ht_option == _FALSE)	return;


	if(pIE->Length > sizeof(struct HT_info_element))
		return;
	
	pmlmeinfo->HT_info_enable = 1;
	_rtw_memcpy(&(pmlmeinfo->HT_info), pIE->data, pIE->Length);
	
	return;
}

void HTOnAssocRsp(_adapter *padapter)
{
	unsigned char		max_AMPDU_len;
	unsigned char		min_MPDU_spacing;
	//struct registry_priv	 *pregpriv = &padapter->registrypriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	
	DBG_871X("%s\n", __FUNCTION__);

	if ((pmlmeinfo->HT_info_enable) && (pmlmeinfo->HT_caps_enable))
	{
		pmlmeinfo->HT_enable = 1;
	}
	else
	{
		pmlmeinfo->HT_enable = 0;
		//set_channel_bwmode(padapter, pmlmeext->cur_channel, pmlmeext->cur_ch_offset, pmlmeext->cur_bwmode);
		return;
	}
	
	//handle A-MPDU parameter field
	/* 	
		AMPDU_para [1:0]:Max AMPDU Len => 0:8k , 1:16k, 2:32k, 3:64k
		AMPDU_para [4:2]:Min MPDU Start Spacing	
	*/
	max_AMPDU_len = pmlmeinfo->HT_caps.u.HT_cap_element.AMPDU_para & 0x03;	
	
	min_MPDU_spacing = (pmlmeinfo->HT_caps.u.HT_cap_element.AMPDU_para & 0x1c) >> 2;	

	padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_AMPDU_MIN_SPACE, (u8 *)(&min_MPDU_spacing));

	padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_AMPDU_FACTOR, (u8 *)(&max_AMPDU_len));

#if 0 //move to rtw_update_ht_cap()
	if ((pregpriv->cbw40_enable) &&
		(pmlmeinfo->HT_caps.u.HT_cap_element.HT_caps_info & BIT(1)) && 
		(pmlmeinfo->HT_info.infos[0] & BIT(2)))
	{
		//switch to the 40M Hz mode accoring to the AP
		pmlmeext->cur_bwmode = HT_CHANNEL_WIDTH_40;
		switch ((pmlmeinfo->HT_info.infos[0] & 0x3))
		{
			case HT_EXTCHNL_OFFSET_UPPER:
				pmlmeext->cur_ch_offset = HAL_PRIME_CHNL_OFFSET_LOWER;
				break;
			
			case HT_EXTCHNL_OFFSET_LOWER:
				pmlmeext->cur_ch_offset = HAL_PRIME_CHNL_OFFSET_UPPER;
				break;
				
			default:
				pmlmeext->cur_ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
				break;
		}
		
		//SelectChannel(padapter, pmlmeext->cur_channel, pmlmeext->cur_ch_offset);
	}
#endif

	//set_channel_bwmode(padapter, pmlmeext->cur_channel, pmlmeext->cur_ch_offset, pmlmeext->cur_bwmode);

#if 0 //move to rtw_update_ht_cap()
	//
	// Config SM Power Save setting
	//
	pmlmeinfo->SM_PS = (pmlmeinfo->HT_caps.u.HT_cap_element.HT_caps_info & 0x0C) >> 2;
	if(pmlmeinfo->SM_PS == WLAN_HT_CAP_SM_PS_STATIC)
	{
		/*u8 i;
		//update the MCS rates
		for (i = 0; i < 16; i++)
		{
			pmlmeinfo->HT_caps.HT_cap_element.MCS_rate[i] &= MCS_rate_1R[i];
		}*/
		DBG_871X("%s(): WLAN_HT_CAP_SM_PS_STATIC\n",__FUNCTION__);
	}

	//
	// Config current HT Protection mode.
	//
	pmlmeinfo->HT_protection = pmlmeinfo->HT_info.infos[1] & 0x3;
#endif
	
}

void ERP_IE_handler(_adapter *padapter, PNDIS_802_11_VARIABLE_IEs pIE)
{
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	if(pIE->Length>1)
		return;
	
	pmlmeinfo->ERP_enable = 1;
	_rtw_memcpy(&(pmlmeinfo->ERP_IE), pIE->data, pIE->Length);
}

void VCS_update(_adapter *padapter, struct sta_info *psta)
{
	struct registry_priv	 *pregpriv = &padapter->registrypriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	switch (pregpriv->vrtl_carrier_sense)/* 0:off 1:on 2:auto */
	{
		case 0: //off
			psta->rtsen = 0;
			psta->cts2self = 0;
			break;
			
		case 1: //on
			if (pregpriv->vcs_type == 1) /* 1:RTS/CTS 2:CTS to self */
			{
				psta->rtsen = 1;
				psta->cts2self = 0;
			}
			else
			{
				psta->rtsen = 0;
				psta->cts2self = 1;
			}
			break;
			
		case 2: //auto
		default:
			if ((pmlmeinfo->ERP_enable) && (pmlmeinfo->ERP_IE & BIT(1)))
			{
				if (pregpriv->vcs_type == 1)
				{
					psta->rtsen = 1;
					psta->cts2self = 0;
				}
				else
				{
					psta->rtsen = 0;
					psta->cts2self = 1;
				}
			}
			else
			{
				psta->rtsen = 0;
				psta->cts2self = 0;
			}	
			break;
	}
}

void update_beacon_info(_adapter *padapter, u8 *pframe, uint pkt_len, struct sta_info *psta)
{
	unsigned int i;
	unsigned int len;
	PNDIS_802_11_VARIABLE_IEs	pIE;
		
#ifdef CONFIG_TDLS
	struct tdls_info *ptdlsinfo = &padapter->tdlsinfo;
	u8 tdls_prohibited[] = { 0x00, 0x00, 0x00, 0x00, 0x10 }; //bit(38): TDLS_prohibited
#endif //CONFIG_TDLS
		
	len = pkt_len - (_BEACON_IE_OFFSET_ + WLAN_HDR_A3_LEN);

	for (i = 0; i < len;)
	{
		pIE = (PNDIS_802_11_VARIABLE_IEs)(pframe + (_BEACON_IE_OFFSET_ + WLAN_HDR_A3_LEN) + i);
		
		switch (pIE->ElementID)
		{
#if 0			
			case _VENDOR_SPECIFIC_IE_:		
				//todo: to update WMM paramter set while receiving beacon			
				if (_rtw_memcmp(pIE->data, WMM_PARA_OUI, 6))	//WMM
				{
					(WMM_param_handler(padapter, pIE))? WMMOnAssocRsp(padapter): 0;
				}				
				break;
#endif

			case _HT_EXTRA_INFO_IE_:	//HT info				
				//HT_info_handler(padapter, pIE);
				bwmode_update_check(padapter, pIE);
				break;

			case _ERPINFO_IE_:
				ERP_IE_handler(padapter, pIE);
				VCS_update(padapter, psta);
				break;

#ifdef CONFIG_TDLS
			case _EXT_CAP_IE_:
				if( _rtw_memcmp(pIE->data, tdls_prohibited, 5) == _TRUE )
					ptdlsinfo->ap_prohibited = _TRUE;
				break;
#endif //CONFIG_TDLS
			default:
				break;
		}
		
		i += (pIE->Length + 2);
	}
}

#ifdef CONFIG_DFS
void process_csa_ie(_adapter *padapter, u8 *pframe, uint pkt_len)
{
	unsigned int i;
	unsigned int len;
	PNDIS_802_11_VARIABLE_IEs	pIE;
	u8 new_ch_no = 0; 
		
	len = pkt_len - (_BEACON_IE_OFFSET_ + WLAN_HDR_A3_LEN);

	for (i = 0; i < len;)
	{
		pIE = (PNDIS_802_11_VARIABLE_IEs)(pframe + (_BEACON_IE_OFFSET_ + WLAN_HDR_A3_LEN) + i);
		
		switch (pIE->ElementID)
		{
			case _CH_SWTICH_ANNOUNCE_:
				_rtw_memcpy(&new_ch_no, pIE->data+1, 1);
				rtw_set_csa_cmd(padapter, new_ch_no);
				break;

			default:
				break;
		}
		
		i += (pIE->Length + 2);
	}
}
#endif //CONFIG_DFS

unsigned int is_ap_in_tkip(_adapter *padapter)
{
	u32 i;
	PNDIS_802_11_VARIABLE_IEs	pIE;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX 		*cur_network = &(pmlmeinfo->network);

	if (rtw_get_capability((WLAN_BSSID_EX *)cur_network) & WLAN_CAPABILITY_PRIVACY)
	{
		for (i = sizeof(NDIS_802_11_FIXED_IEs); i < pmlmeinfo->network.IELength;)
		{
			pIE = (PNDIS_802_11_VARIABLE_IEs)(pmlmeinfo->network.IEs + i);
		
			switch (pIE->ElementID)
			{
				case _VENDOR_SPECIFIC_IE_:
					if ((_rtw_memcmp(pIE->data, RTW_WPA_OUI, 4)) && (_rtw_memcmp((pIE->data + 12), WPA_TKIP_CIPHER, 4))) 
					{
						return _TRUE;
					}
					break;
				
				case _RSN_IE_2_:
					if (_rtw_memcmp((pIE->data + 8), RSN_TKIP_CIPHER, 4)) 
					{
						return _TRUE;
					}
					
				default:
					break;
			}
		
			i += (pIE->Length + 2);
		}
		
		return _FALSE;
	}
	else
	{
		return _FALSE;
	}
	
}

int wifirate2_ratetbl_inx(unsigned char rate);
int wifirate2_ratetbl_inx(unsigned char rate)
{
	int	inx = 0;
	rate = rate & 0x7f;

	switch (rate) 
	{
		case 54*2:
			inx = 11;
			break;

		case 48*2:
			inx = 10;
			break;

		case 36*2:
			inx = 9;
			break;

		case 24*2:
			inx = 8;
			break;
			
		case 18*2:
			inx = 7;
			break;

		case 12*2:
			inx = 6;
			break;

		case 9*2:
			inx = 5;
			break;
			
		case 6*2:
			inx = 4;
			break;

		case 11*2:
			inx = 3;
			break;
		case 11:
			inx = 2;
			break;

		case 2*2:
			inx = 1;
			break;
		
		case 1*2:
			inx = 0;
			break;

	}
	return inx;	
}

unsigned int update_basic_rate(unsigned char *ptn, unsigned int ptn_sz)
{
	unsigned int i, num_of_rate;
	unsigned int mask = 0;
	
	num_of_rate = (ptn_sz > NumRates)? NumRates: ptn_sz;
		
	for (i = 0; i < num_of_rate; i++)
	{
		if ((*(ptn + i)) & 0x80)
		{
			mask |= 0x1 << wifirate2_ratetbl_inx(*(ptn + i));
		}
	}
	return mask;
}

unsigned int update_supported_rate(unsigned char *ptn, unsigned int ptn_sz)
{
	unsigned int i, num_of_rate;
	unsigned int mask = 0;
	
	num_of_rate = (ptn_sz > NumRates)? NumRates: ptn_sz;
		
	for (i = 0; i < num_of_rate; i++)
	{
		mask |= 0x1 << wifirate2_ratetbl_inx(*(ptn + i));
	}

	return mask;
}

unsigned int update_MSC_rate(struct HT_caps_element *pHT_caps)
{
	unsigned int mask = 0;
	
	mask = ((pHT_caps->u.HT_cap_element.MCS_rate[0] << 12) | (pHT_caps->u.HT_cap_element.MCS_rate[1] << 20));
						
	return mask;
}

int support_short_GI(_adapter *padapter, struct HT_caps_element *pHT_caps)
{
	unsigned char					bit_offset;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	
	if (!(pmlmeinfo->HT_enable))
		return _FAIL;
	
	if ((pmlmeinfo->assoc_AP_vendor == HT_IOT_PEER_RALINK))
		return _FAIL;
		
	bit_offset = (pmlmeext->cur_bwmode & HT_CHANNEL_WIDTH_40)? 6: 5;
	
	if (pHT_caps->u.HT_cap_element.HT_caps_info & (0x1 << bit_offset))
	{
		return _SUCCESS;
	}
	else
	{
		return _FAIL;
	}		
}

unsigned char get_highest_rate_idx(u32 mask)
{
	int i;
	unsigned char rate_idx=0;

	for(i=27; i>=0; i--)
	{
		if(mask & BIT(i))
		{
			rate_idx = i;
			break;
		}
	}

	return rate_idx;
}

unsigned char get_highest_mcs_rate(struct HT_caps_element *pHT_caps);
unsigned char get_highest_mcs_rate(struct HT_caps_element *pHT_caps)
{
	int i, mcs_rate;
	
	mcs_rate = (pHT_caps->u.HT_cap_element.MCS_rate[0] | (pHT_caps->u.HT_cap_element.MCS_rate[1] << 8));
	
	for (i = 15; i >= 0; i--)
	{
		if (mcs_rate & (0x1 << i))
		{
			break;
		}
	}
	
	return i;
}

void Update_RA_Entry(_adapter *padapter, u32 mac_id)
{
	padapter->HalFunc.UpdateRAMaskHandler(padapter, mac_id);
}

void enable_rate_adaptive(_adapter *padapter, u32 mac_id);
void enable_rate_adaptive(_adapter *padapter, u32 mac_id)
{
	Update_RA_Entry(padapter, mac_id);
}

void set_sta_rate(_adapter *padapter, struct sta_info *psta)
{
	//rate adaptive	
	enable_rate_adaptive(padapter, psta->mac_id);
}

unsigned char check_assoc_AP(u8 *pframe, uint len)
{
	unsigned int	i;
	PNDIS_802_11_VARIABLE_IEs	pIE;

	for (i = sizeof(NDIS_802_11_FIXED_IEs); i < len;)
	{
		pIE = (PNDIS_802_11_VARIABLE_IEs)(pframe + i);
		
		switch (pIE->ElementID)
		{
			case _VENDOR_SPECIFIC_IE_:
				if ((_rtw_memcmp(pIE->data, ARTHEROS_OUI1, 3)) || (_rtw_memcmp(pIE->data, ARTHEROS_OUI2, 3)))
				{
					DBG_871X("link to Artheros AP\n");
					return HT_IOT_PEER_ATHEROS;
				}
				else if ((_rtw_memcmp(pIE->data, BROADCOM_OUI1, 3))
							|| (_rtw_memcmp(pIE->data, BROADCOM_OUI2, 3))
							|| (_rtw_memcmp(pIE->data, BROADCOM_OUI2, 3)))
				{
					DBG_871X("link to Broadcom AP\n");
					return HT_IOT_PEER_BROADCOM;
				}
				else if (_rtw_memcmp(pIE->data, MARVELL_OUI, 3))
				{
					DBG_871X("link to Marvell AP\n");
					return HT_IOT_PEER_MARVELL;
				}
				else if (_rtw_memcmp(pIE->data, RALINK_OUI, 3))
				{
					DBG_871X("link to Ralink AP\n");
					return HT_IOT_PEER_RALINK;
				}
				else if (_rtw_memcmp(pIE->data, CISCO_OUI, 3))
				{
					DBG_871X("link to Cisco AP\n");
					return HT_IOT_PEER_CISCO;
				}
				else if (_rtw_memcmp(pIE->data, REALTEK_OUI, 3))
				{
					DBG_871X("link to Realtek 96B\n");
					return HT_IOT_PEER_REALTEK;
				}
				else if (_rtw_memcmp(pIE->data, AIRGOCAP_OUI,3))
				{
					DBG_871X("link to Airgo Cap\n");
					return HT_IOT_PEER_AIRGO;
				}
				else
				{
					break;
				}
						
			default:
				break;
		}
				
		i += (pIE->Length + 2);
	}
	
	DBG_871X("link to new AP\n");
	return HT_IOT_PEER_UNKNOWN;
}

void update_IOT_info(_adapter *padapter)
{
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	
	switch (pmlmeinfo->assoc_AP_vendor)
	{
		case HT_IOT_PEER_MARVELL:
			pmlmeinfo->turboMode_cts2self = 1;
			pmlmeinfo->turboMode_rtsen = 0;
			break;
		
		case HT_IOT_PEER_RALINK:
			pmlmeinfo->turboMode_cts2self = 0;
			pmlmeinfo->turboMode_rtsen = 1;
			//disable high power			
			Switch_DM_Func(padapter, (~DYNAMIC_BB_DYNAMIC_TXPWR), _FALSE);
			break;
		case HT_IOT_PEER_REALTEK:
			//rtw_write16(padapter, 0x4cc, 0xffff);
			//rtw_write16(padapter, 0x546, 0x01c0);
			//disable high power			
			Switch_DM_Func(padapter, (~DYNAMIC_BB_DYNAMIC_TXPWR), _FALSE);
			break;
		default:
			pmlmeinfo->turboMode_cts2self = 0;
			pmlmeinfo->turboMode_rtsen = 1;
			break;	
	}
	
}

void update_capinfo(PADAPTER Adapter, u16 updateCap)
{
	struct mlme_ext_priv	*pmlmeext = &Adapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	BOOLEAN		ShortPreamble;

	// Check preamble mode, 2005.01.06, by rcnjko.
	// Mark to update preamble value forever, 2008.03.18 by lanhsin
	//if( pMgntInfo->RegPreambleMode == PREAMBLE_AUTO )
	{
			
		if(updateCap & cShortPreamble)
		{ // Short Preamble
			if(pmlmeinfo->preamble_mode != PREAMBLE_SHORT) // PREAMBLE_LONG or PREAMBLE_AUTO
			{
				ShortPreamble = _TRUE;
				pmlmeinfo->preamble_mode = PREAMBLE_SHORT;
				Adapter->HalFunc.SetHwRegHandler( Adapter, HW_VAR_ACK_PREAMBLE, (u8 *)&ShortPreamble );
			}
		}
		else
		{ // Long Preamble
			if(pmlmeinfo->preamble_mode != PREAMBLE_LONG)  // PREAMBLE_SHORT or PREAMBLE_AUTO
			{
				ShortPreamble = _FALSE;
				pmlmeinfo->preamble_mode = PREAMBLE_LONG;
				Adapter->HalFunc.SetHwRegHandler( Adapter, HW_VAR_ACK_PREAMBLE, (u8 *)&ShortPreamble );
			}
		}
	}

	if ( updateCap & cIBSS ) {
		//Filen: See 802.11-2007 p.91
		pmlmeinfo->slotTime = NON_SHORT_SLOT_TIME;
	}
	else
	{
		//Filen: See 802.11-2007 p.90
		if( pmlmeext->cur_wireless_mode & (WIRELESS_11G | WIRELESS_11_24N))
		{
			if( (updateCap & cShortSlotTime) /* && (!(pMgntInfo->pHTInfo->RT2RT_HT_Mode & RT_HT_CAP_USE_LONG_PREAMBLE)) */)
			{ // Short Slot Time
				if(pmlmeinfo->slotTime != SHORT_SLOT_TIME)
				{
					pmlmeinfo->slotTime = SHORT_SLOT_TIME;
				}
			}
			else
			{ // Long Slot Time
				if(pmlmeinfo->slotTime != NON_SHORT_SLOT_TIME)
				{
					pmlmeinfo->slotTime = NON_SHORT_SLOT_TIME;
				}
			}
		}
		else if( pmlmeext->cur_wireless_mode & (WIRELESS_11A | WIRELESS_11_5N))
		{
			pmlmeinfo->slotTime = SHORT_SLOT_TIME;
		}
		else
		{
			//B Mode
			pmlmeinfo->slotTime = NON_SHORT_SLOT_TIME;
		}
 	}
 
	Adapter->HalFunc.SetHwRegHandler( Adapter, HW_VAR_SLOT_TIME, &pmlmeinfo->slotTime );

}

void update_wireless_mode(_adapter *padapter)
{
	int ratelen, network_type = 0;
	u16 SIFS_Timer;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX 		*cur_network = &(pmlmeinfo->network);
	unsigned char			*rate = cur_network->SupportedRates;

	ratelen = rtw_get_rateset_len(cur_network->SupportedRates);

	if ((pmlmeinfo->HT_info_enable) && (pmlmeinfo->HT_caps_enable))
	{
		pmlmeinfo->HT_enable = 1;
	}

	if(pmlmeext->cur_channel > 14)
	{
		if (pmlmeinfo->HT_enable)
		{
			network_type = WIRELESS_11_5N;
		}

		network_type |= WIRELESS_11A;
	}
	else
	{
		if (pmlmeinfo->HT_enable)
		{
			network_type = WIRELESS_11_24N;
		}
	
		if ((cckratesonly_included(rate, ratelen)) == _TRUE)
		{
			network_type |= WIRELESS_11B;
		}
		else if((cckrates_included(rate, ratelen)) == _TRUE)
		{
			network_type |= WIRELESS_11BG;
		}
		else
		{
			network_type |= WIRELESS_11G;
		}
	}

	pmlmeext->cur_wireless_mode = network_type & padapter->registrypriv.wireless_mode;
	if((pmlmeext->cur_wireless_mode==WIRELESS_11G) ||
		(pmlmeext->cur_wireless_mode==WIRELESS_11BG))//WIRELESS_MODE_G)
		SIFS_Timer = 0x0a0a;
	else
		SIFS_Timer = 0x0e0e;//pHalData->SifsTime;
	padapter->HalFunc.SetHwRegHandler( padapter, HW_VAR_SIFS,  (u8 *)&SIFS_Timer);
	
}

void fire_write_MAC_cmd(_adapter *padapter, unsigned int addr, unsigned int value);
void fire_write_MAC_cmd(_adapter *padapter, unsigned int addr, unsigned int value)
{
#if 0
	struct cmd_obj					*ph2c;
	struct reg_rw_parm			*pwriteMacPara;
	struct cmd_priv					*pcmdpriv = &(padapter->cmdpriv);

	if ((ph2c = (struct cmd_obj*)rtw_zmalloc(sizeof(struct cmd_obj))) == NULL)
	{
		return;
	}	

	if ((pwriteMacPara = (struct reg_rw_parm*)rtw_malloc(sizeof(struct reg_rw_parm))) == NULL) 
	{		
		rtw_mfree((unsigned char *)ph2c, sizeof(struct cmd_obj));
		return;
	}
	
	pwriteMacPara->rw = 1;
	pwriteMacPara->addr = addr;
	pwriteMacPara->value = value;
	
	init_h2fwcmd_w_parm_no_rsp(ph2c, pwriteMacPara, GEN_CMD_CODE(_Write_MACREG));
	rtw_enqueue_cmd(pcmdpriv, ph2c);
#endif	
}

u8 bmc_support_rate_ofdm[4] = 
	{IEEE80211_OFDM_RATE_6MB|IEEE80211_BASIC_RATE_MASK, IEEE80211_OFDM_RATE_12MB|IEEE80211_BASIC_RATE_MASK,
	IEEE80211_OFDM_RATE_18MB|IEEE80211_BASIC_RATE_MASK, IEEE80211_OFDM_RATE_24MB|IEEE80211_BASIC_RATE_MASK};
u8 bmc_support_rate_cck[4] =
	{IEEE80211_CCK_RATE_1MB|IEEE80211_BASIC_RATE_MASK, IEEE80211_CCK_RATE_2MB|IEEE80211_BASIC_RATE_MASK,
	IEEE80211_CCK_RATE_5MB|IEEE80211_BASIC_RATE_MASK, IEEE80211_CCK_RATE_11MB|IEEE80211_BASIC_RATE_MASK};

void update_bmc_sta_support_rate(_adapter *padapter, u32 mac_id)
{
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	if(pmlmeext->cur_wireless_mode & WIRELESS_11B)
	{
		// Only B, B/G, and B/G/N AP could use CCK rate
		_rtw_memcpy((pmlmeinfo->FW_sta_info[mac_id].SupportedRates), bmc_support_rate_cck, 4);
	}
	else
	{
		_rtw_memcpy((pmlmeinfo->FW_sta_info[mac_id].SupportedRates), bmc_support_rate_ofdm, 4);
	}
}

int update_sta_support_rate(_adapter *padapter, u8* pvar_ie, uint var_ie_len, int cam_idx)
{
	unsigned int	ie_len;
	PNDIS_802_11_VARIABLE_IEs	pIE;
	int	supportRateNum = 0;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	
	pIE = (PNDIS_802_11_VARIABLE_IEs)rtw_get_ie(pvar_ie, _SUPPORTEDRATES_IE_, &ie_len, var_ie_len);
	if (pIE == NULL)
	{
		return _FAIL;
	}
	
	_rtw_memcpy(pmlmeinfo->FW_sta_info[cam_idx].SupportedRates, pIE->data, ie_len);
	supportRateNum = ie_len;
				
	pIE = (PNDIS_802_11_VARIABLE_IEs)rtw_get_ie(pvar_ie, _EXT_SUPPORTEDRATES_IE_, &ie_len, var_ie_len);
	if (pIE)
	{
		_rtw_memcpy((pmlmeinfo->FW_sta_info[cam_idx].SupportedRates + supportRateNum), pIE->data, ie_len);
	}

	return _SUCCESS;
	
}

void process_addba_req(_adapter *padapter, u8 *paddba_req, u8 *addr)
{
	struct sta_info *psta;
	u16 tid, start_seq, param;	
	struct recv_reorder_ctrl *preorder_ctrl;
	struct sta_priv *pstapriv = &padapter->stapriv;	
	struct ADDBA_request	*preq = (struct ADDBA_request*)paddba_req;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	psta = rtw_get_stainfo(pstapriv, addr);

	if(psta)
	{
		start_seq = le16_to_cpu(preq->BA_starting_seqctrl) >> 4;
			
		param = le16_to_cpu(preq->BA_para_set);
		tid = (param>>2)&0x0f;
		
		preorder_ctrl = &psta->recvreorder_ctrl[tid];

		#ifdef CONFIG_UPDATE_INDICATE_SEQ_WHILE_PROCESS_ADDBA_REQ
		preorder_ctrl->indicate_seq = start_seq;
		#ifdef DBG_RX_SEQ
		DBG_871X("DBG_RX_SEQ %s:%d IndicateSeq: %d, start_seq: %d\n", __FUNCTION__, __LINE__,
			preorder_ctrl->indicate_seq, start_seq);
		#endif
		#else
		preorder_ctrl->indicate_seq = 0xffff;
		#endif
		
		preorder_ctrl->enable =(pmlmeinfo->bAcceptAddbaReq == _TRUE)? _TRUE :_FALSE;
	}

}

void update_TSF(struct mlme_ext_priv *pmlmeext, u8 *pframe, uint len)
{	
	u8* pIE;
	u32 *pbuf;
		
	pIE = pframe + sizeof(struct rtw_ieee80211_hdr_3addr);
	pbuf = (u32*)pIE;

	pmlmeext->TSFValue = le32_to_cpu(*(pbuf+1));
	
	pmlmeext->TSFValue = pmlmeext->TSFValue << 32;

	pmlmeext->TSFValue |= le32_to_cpu(*pbuf);
}

void correct_TSF(_adapter *padapter, struct mlme_ext_priv *pmlmeext)
{
	padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_CORRECT_TSF, 0);
}

void beacon_timing_control(_adapter *padapter)
{
	padapter->HalFunc.SetBeaconRelatedRegistersHandler(padapter);
}

#if 0
unsigned int setup_beacon_frame(_adapter *padapter, unsigned char *beacon_frame)
{
	unsigned short				ATIMWindow;
	unsigned char					*pframe;
	struct tx_desc 				*ptxdesc;
	struct rtw_ieee80211_hdr 	*pwlanhdr;
	unsigned short				*fctrl;
	unsigned int					rate_len, len = 0;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX 		*cur_network = &(pmlmeinfo->network);
	u8	bc_addr[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	
	_rtw_memset(beacon_frame, 0, 256);
	
	pframe = beacon_frame + TXDESC_SIZE;
	
	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;	
	
	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;
	
	_rtw_memcpy(pwlanhdr->addr1, bc_addr, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, myid(&(padapter->eeprompriv)), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, get_my_bssid(cur_network), ETH_ALEN);
	
	SetFrameSubType(pframe, WIFI_BEACON);
	
	pframe += sizeof(struct rtw_ieee80211_hdr_3addr);	
	len = sizeof(struct rtw_ieee80211_hdr_3addr);

	//timestamp will be inserted by hardware
	pframe += 8;
	len += 8;

	// beacon interval: 2 bytes
	_rtw_memcpy(pframe, (unsigned char *)(rtw_get_beacon_interval_from_ie(cur_network->IEs)), 2); 

	pframe += 2;
	len += 2;

	// capability info: 2 bytes
	_rtw_memcpy(pframe, (unsigned char *)(rtw_get_capability_from_ie(cur_network->IEs)), 2);

	pframe += 2;
	len += 2;

	// SSID
	pframe = rtw_set_ie(pframe, _SSID_IE_, cur_network->Ssid.SsidLength, cur_network->Ssid.Ssid, &len);

	// supported rates...
	rate_len = rtw_get_rateset_len(cur_network->SupportedRates);
	pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_, ((rate_len > 8)? 8: rate_len), cur_network->SupportedRates, &len);

	// DS parameter set
	pframe = rtw_set_ie(pframe, _DSSET_IE_, 1, (unsigned char *)&(cur_network->Configuration.DSConfig), &len);

	// IBSS Parameter Set...
	//ATIMWindow = cur->Configuration.ATIMWindow;
	ATIMWindow = 0;
	pframe = rtw_set_ie(pframe, _IBSS_PARA_IE_, 2, (unsigned char *)(&ATIMWindow), &len);

	//todo: ERP IE
	
	// EXTERNDED SUPPORTED RATE
	if (rate_len > 8)
	{
		pframe = rtw_set_ie(pframe, _EXT_SUPPORTEDRATES_IE_, (rate_len - 8), (cur_network->SupportedRates + 8), &len);
	}

	if ((len + TXDESC_SIZE) > 256)
	{
		//DBG_871X("marc: beacon frame too large\n");
		return 0;
	}

	//fill the tx descriptor
	ptxdesc = (struct tx_desc *)beacon_frame;
	
	//offset 0	
	ptxdesc->txdw0 |= cpu_to_le32(len & 0x0000ffff); 
	ptxdesc->txdw0 |= cpu_to_le32(((TXDESC_SIZE + OFFSET_SZ) << OFFSET_SHT) & 0x00ff0000); //default = 32 bytes for TX Desc
	
	//offset 4	
	ptxdesc->txdw1 |= cpu_to_le32((0x10 << QSEL_SHT) & 0x00001f00);
	
	//offset 8		
	ptxdesc->txdw2 |= cpu_to_le32(BMC);
	ptxdesc->txdw2 |= cpu_to_le32(BK);

	//offset 16		
	ptxdesc->txdw4 = 0x80000000;
	
	//offset 20
	ptxdesc->txdw5 = 0x00000000; //1M	
	
	return (len + TXDESC_SIZE);
}
#endif

