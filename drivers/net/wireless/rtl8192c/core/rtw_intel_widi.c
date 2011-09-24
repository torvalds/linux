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
#define _RTW_INTEL_WIDI_C_

#include <drv_types.h>
#include <wifi.h>

#ifdef CONFIG_INTEL_WIDI
#include <rtw_intel_widi.h>

void intel_widi_listen_timer_hdl(void *FunctionContext);

void rtw_init_intel_widi( _adapter *padapter)
{
	struct mlme_priv		*pmlmepriv = &(padapter->mlmepriv);

	pmlmepriv->widi_state = INTEL_WIDI_STATE_NONE;

	_init_timer(&pmlmepriv->listen_timer, padapter->pnetdev, intel_widi_listen_timer_hdl, padapter);
}

void rtw_free_intel_widi( _adapter *padapter)
{
	struct mlme_priv		*pmlmepriv = &(padapter->mlmepriv);

	if (padapter->bDriverStopped == _TRUE)
	{
		_cancel_timer_ex(&pmlmepriv->listen_timer);
	}
}

void issue_probereq_widi(_adapter *padapter, l2_msg_t *l2_msg)
{
	u8	wpsie[256];
	u16	wpsielen = 0;
	struct mlme_priv		*pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	_rtw_memcpy(get_my_bssid(&(pmlmeinfo->network)), l2_msg->u.l2sd_service_msg.dst_mac, ETH_ALEN);

	//add wps_ie for WiDi
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

	//	Request Type
	//	Type:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_REQUEST_TYPE );
	wpsielen += 2;

	//	Length:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( 0x0001 );
	wpsielen += 2;

	//	Value:
	wpsie[wpsielen++] = WPS_REQ_TYPE_ENROLLEE_INFO_ONLY;

	//	Config Method
	//	Type:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_CONF_METHOD );
	wpsielen += 2;

	//	Length:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( 0x0002 );
	wpsielen += 2;

	//	Value:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_CONFIG_METHOD_PBC | WPS_CONFIG_METHOD_DISPLAY| WPS_CONFIG_METHOD_LABEL );
	wpsielen += 2;

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

	//	Primary Device Type
	//	Type:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_PRIMARY_DEV_TYPE );
	wpsielen += 2;

	//	Length:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( L2SDTA_PRIMARY_DEV_LEN );
	wpsielen += 2;

	//	Value:
	_rtw_memcpy( wpsie + wpsielen, l2_msg->u.l2sd_service_msg.sa_primary_dev, L2SDTA_PRIMARY_DEV_LEN );
	wpsielen += L2SDTA_PRIMARY_DEV_LEN;

	//	RF Bands
	//	Type:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_RF_BANDS );
	wpsielen += 2;

	//	Length:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( 0x0001 );
	wpsielen += 2;

	//	Value:
	wpsie[wpsielen++] = WPS_RF_BANDS_2_4_GHZ; // 2.4GHz

	//	Association State
	//	Type:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_ASSOCIATION_STATE );
	wpsielen += 2;

	//	Length:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( 0x0002 );
	wpsielen += 2;

	//	Value:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ASSOC_STATE_NOT_ASSOCIATED);
	wpsielen += 2;

	//	Configuration Error
	//	Type:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_CONFIG_ERROR);
	wpsielen += 2;

	//	Length:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( 0x0002 );
	wpsielen += 2;

	//	Value:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( 0x0000);
	wpsielen += 2;

	//	Device Password ID
	//	Type:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_DEVICE_PWID);
	wpsielen += 2;

	//	Length:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( 0x0002 );
	wpsielen += 2;

	//	Value:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_DPID_PIN);
	wpsielen += 2;

	//	Device Name
	//	Type:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_DEVICE_NAME );
	wpsielen += 2;

	//	Length:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( L2SDTA_DEVICE_NAME_LEN );
	wpsielen += 2;

	//	Value:
	_rtw_memcpy( wpsie + wpsielen, l2_msg->u.l2sd_service_msg.sa_device_name, L2SDTA_DEVICE_NAME_LEN );
	wpsielen += L2SDTA_DEVICE_NAME_LEN;

	//	Vendor Extension
	_rtw_memcpy( wpsie + wpsielen, l2_msg->u.l2sd_service_msg.sa_ext, L2SDTA_SERVICE_VE_LEN );
	wpsielen += L2SDTA_SERVICE_VE_LEN;

	pmlmepriv->probereq_wpsie[0] = 0xdd;
	pmlmepriv->probereq_wpsie[1] = wpsielen;
	pmlmepriv->probereq_wpsie_len = wpsielen + 2;
	_rtw_memcpy(&pmlmepriv->probereq_wpsie[2], wpsie, wpsielen);

	//DBG_871X("WiDi wps ie length = %d\n",wpsielen);

	// driver should issue probe request in the right channel
	if( l2_msg->u.l2sd_service_msg.channel == pmlmeext->channel_set[pmlmepriv->channel_idx].ChannelNum)
	{
		issue_probereq(padapter, 0);
	}

	//clear wps ie length after send probe request.
	pmlmepriv->probereq_wpsie_len = 0;
}

static void indicate_widi_msg(_adapter *padapter, _pkt *skb)
{
	skb->dev = padapter->pnetdev;
	skb->ip_summed = CHECKSUM_NONE;
	skb->protocol = eth_type_trans(skb, padapter->pnetdev);
	skb->protocol = htons(ETH_P_WIDI_NOTIF);
	netif_rx(skb);
}

void process_intel_widi_assoc_status(_adapter *padapter, u8 assoc_status)
{
	_pkt *skb;
	l2_msg_t *l2_notif;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX 		*cur_network = &(pmlmeinfo->network);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18)) // http://www.mail-archive.com/netdev@vger.kernel.org/msg17214.html
	skb = dev_alloc_skb(sizeof(l2_msg_t));
#else			
	skb = netdev_alloc_skb(padapter->pnetdev, sizeof(l2_msg_t));
#endif
	if(skb != NULL)
	{
		l2_notif = (l2_msg_t *)skb->data;
		_rtw_memset(l2_notif, 0, sizeof(l2_msg_t));
		skb_put(skb, sizeof(struct l2sd_assoc_msg_t)+1);
		l2_notif->msg_type = L2SDTA_MSG_TYPE_ASSOC_STATUS;
		_rtw_memcpy(l2_notif->u.l2sd_assoc_msg.peer_mac, get_my_bssid(cur_network), ETH_ALEN);
		l2_notif->u.l2sd_assoc_msg.assoc_stat = assoc_status;

		DBG_871X("indicate_widi_assoc_status %d\n",assoc_status);
		indicate_widi_msg(padapter, skb);
	}
}

void process_intel_widi_disconnect(_adapter *padapter, u8 bdisassoc)
{
	if(padapter->mlmepriv.widi_state == INTEL_WIDI_STATE_CONNECTED)
	{
		padapter->mlmepriv.widi_state == INTEL_WIDI_STATE_DISASSOCIATED;
		process_intel_widi_assoc_status(padapter, L2SDTA_DISASSOCIATED);
	}
}

void process_intel_widi_wps_status(_adapter *padapter, u8 wps_status)
{
	if ( wps_status == 2 ) // WPS Stop because of wps success
	{
		DBG_871X("Intel WiDi WPS Success with RDS Source\n");
		process_intel_widi_assoc_status(padapter, L2SDTA_WPS_STATUS_SUCCESS);
	}
	else if ( wps_status == 3 ) // WPS Stop because of wps fail
	{
		DBG_871X("Intel WiDi WPS Fail with RDS Source\n");
		process_intel_widi_assoc_status(padapter, L2SDTA_WPS_STATUS_FAIL);
	}
}

int	process_intel_widi_query_or_tigger(_adapter *padapter, WLAN_BSSID_EX *bssid)
{
	u8	*wps_ie, *wps_attr;
	u32	wps_len, wps_attr_len;
	_pkt	*skb;
	l2_msg_t *l2_notif;
	struct l2sd_wps_attrib_hdr_t	*l2sd_attr;

	if(padapter->mlmepriv.widi_state != INTEL_WIDI_STATE_LISTEN)
		return 0;

	wps_ie = rtw_get_wps_ie(bssid->IEs, bssid->IELength, NULL, &wps_len);
	if(wps_ie && wps_len>0)
	{
		wps_attr = rtw_get_wps_attr_ie(wps_ie, wps_len, WPS_ATTR_VENDOR_EXT, NULL, &wps_attr_len);
		if(wps_attr && wps_attr_len>0)
		{
			l2sd_attr = (struct l2sd_wps_attrib_hdr_t *)wps_attr;
			if(IS_INTEL_SMI(l2sd_attr->smi_intel))	
			{
				DBG_871X("Get WPS Vendor extension IE from Intel, length = %d\n",wps_attr_len);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18)) // http://www.mail-archive.com/netdev@vger.kernel.org/msg17214.html
				skb = dev_alloc_skb(sizeof(l2_msg_t));
#else			
				skb = netdev_alloc_skb(padapter->pnetdev, sizeof(l2_msg_t));
#endif	
				if(skb != NULL)
				{
					l2_notif = (l2_msg_t *)skb->data;
					_rtw_memset(l2_notif, 0, sizeof(l2_msg_t));
					skb_put(skb, sizeof(struct l2sd_query_or_trig_msg_t)+1);
					l2_notif->msg_type = L2SDTA_MSG_TYPE_QUERY_OR_TRIGGER;
					_rtw_memcpy(l2_notif->u.l2sd_query_or_trig_msg.src_mac, bssid->MacAddress, ETH_ALEN);
					l2_notif->u.l2sd_query_or_trig_msg.channel = bssid->Configuration.DSConfig;
					l2_notif->u.l2sd_query_or_trig_msg.ssid_len = bssid->Ssid.SsidLength;
					_rtw_memcpy(l2_notif->u.l2sd_query_or_trig_msg.ssid, bssid->Ssid.Ssid, bssid->Ssid.SsidLength);
					_rtw_memcpy(l2_notif->u.l2sd_query_or_trig_msg.qa_ta_ext, wps_attr, wps_attr_len+4);

					DBG_871X("Recvive WIDI Query or Trigger Msg\n");
					indicate_widi_msg(padapter, skb);
				}
			}
		}
	}

	return 1;
}

void process_intel_widi_cmd(_adapter*padapter, u8 *cmd)
{
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);

	DBG_8192C( "[%s] cmd = %s\n", __FUNCTION__, cmd );

	if( _rtw_memcmp( cmd, "enable", 6 ) )
	{
		DBG_871X("Intel WiDi Enable\n");
	}
	else if ( _rtw_memcmp( cmd, "disable", 7 ) )
	{
		DBG_871X("Intel WiDi Disable\n");
		pmlmepriv->widi_state = INTEL_WIDI_STATE_NONE;
		intel_widi_wk_cmd(padapter, INTEL_WIDI_LISTEN_STOP_WK);
	}
	else if ( _rtw_memcmp( cmd, "listen", 6 ) )
	{
		DBG_871X("Intel WiDi start listening for RDS Source\n");
		pmlmepriv->widi_state = INTEL_WIDI_STATE_LISTEN;
		intel_widi_wk_cmd(padapter, INTEL_WIDI_LISTEN_WK);
	}
	else if ( _rtw_memcmp( cmd, "wps_start", 9 ) )
	{
		DBG_871X("Intel WiDi start WPS with RDS Source\n");
		pmlmepriv->widi_state = INTEL_WIDI_STATE_WPS;
		intel_widi_wk_cmd(padapter, INTEL_WIDI_LISTEN_STOP_WK);
	}
	else if ( _rtw_memcmp( cmd, "associate", 9 ) )
	{
		DBG_871X("Intel WiDi is ready to associate with RDS Source\n");
		pmlmepriv->widi_state = INTEL_WIDI_STATE_ASSOICATE;
		intel_widi_wk_cmd(padapter, INTEL_WIDI_LISTEN_STOP_WK);
	}
	else if ( _rtw_memcmp( cmd, "connected", 9 ) )
	{
		DBG_871X("Intel WiDi is connected with RDS Source\n");
		pmlmepriv->widi_state = INTEL_WIDI_STATE_CONNECTED;
		process_intel_widi_assoc_status(padapter, L2SDTA_ASSOCIATED);
	}
}

void intel_widi_listen_timer_hdl(void *FunctionContext)
{
	_adapter *padapter = (_adapter *)FunctionContext;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);

	if(pmlmepriv->listen_state == INTEL_WIDI_LISTEN_PROCESS)
	{
		pmlmepriv->channel_idx++;
	}

	intel_widi_wk_cmd(padapter, INTEL_WIDI_LISTEN_WK);
}

void intel_widi_listen_stop_handler(_adapter *padapter)
{
	u8	val8;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	if(pmlmepriv->listen_state == INTEL_WIDI_LISTEN_STOP)
		return;

	DBG_871X("Leave WiDi Listen Mode\n");

	_cancel_timer_ex(&(pmlmepriv->listen_timer));

	val8 = 0;
	padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_MLME_SITESURVEY, (u8 *)(&val8));

	//config MSR
	Set_MSR(padapter, (pmlmeinfo->state & 0x3));

	//turn on dynamic functions
	Restore_DM_Func_Flag(padapter);

	pmlmepriv->listen_state = INTEL_WIDI_LISTEN_STOP;
	pmlmeext->sitesurvey_res.state = SCAN_DISABLE;
}

void intel_widi_listen_handler(_adapter *padapter)
{
	u8	listen_channel, val8;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;


	if(pmlmepriv->widi_state != INTEL_WIDI_STATE_LISTEN)
	{
		if(pmlmepriv->listen_state != INTEL_WIDI_LISTEN_STOP)
		{
			intel_widi_listen_stop_handler(padapter);
		}
		return;
	}

	if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY) == _TRUE)
	{
		_set_timer(&(pmlmepriv->listen_timer), 1000);
		return;
	}

	if(pmlmepriv->listen_state == INTEL_WIDI_LISTEN_STOP)
	{
		pmlmepriv->channel_idx = 0;

		_rtw_memset(pmlmeext->sitesurvey_res.ss_ssid, 0, (IW_ESSID_MAX_SIZE + 1));
		pmlmeext->sitesurvey_res.ss_ssidlen = 0;
	
		//disable dynamic functions, such as high power, DIG
		Save_DM_Func_Flag(padapter);
		Switch_DM_Func(padapter, DYNAMIC_FUNC_DISABLE, _FALSE);

		//set MSR to no link state
		Set_MSR(padapter, _HW_STATE_NOLINK_);

		val8 = 1; //before site survey
		padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_MLME_SITESURVEY, (u8 *)(&val8));
	
		pmlmepriv->listen_state = INTEL_WIDI_LISTEN_PROCESS;
		pmlmeext->sitesurvey_res.state = SCAN_PROCESS;
	}

	listen_channel = pmlmeext->channel_set[pmlmepriv->channel_idx].ChannelNum;

	if(listen_channel == 0)
	{
		// Get Back to first channel
		pmlmepriv->channel_idx = 0;
		listen_channel = pmlmeext->channel_set[pmlmepriv->channel_idx].ChannelNum;
	}

	if(pmlmepriv->channel_idx == 0)
	{
		set_channel_bwmode(padapter, listen_channel, HAL_PRIME_CHNL_OFFSET_DONT_CARE, HT_CHANNEL_WIDTH_20);
	}
	else
	{
		SelectChannel(padapter, listen_channel);
	}

	_set_timer(&(pmlmepriv->listen_timer), SURVEY_TO);
}

void intel_widi_wk_hdl(_adapter *padapter, u8 intel_widi_state)
{	
_func_enter_;

	switch(intel_widi_state)
	{
		case INTEL_WIDI_LISTEN_WK:
			intel_widi_listen_handler(padapter);
			break;
		case INTEL_WIDI_LISTEN_STOP_WK:
			intel_widi_listen_stop_handler(padapter);
			break;
		default:
			break;
	}

_func_exit_;
}

u8 intel_widi_wk_cmd(_adapter*padapter, u8 intel_widi_state)
{
	struct cmd_obj	*ph2c;
	struct drvextra_cmd_parm	*pdrvextra_cmd_parm;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct cmd_priv	*pcmdpriv = &padapter->cmdpriv;
	u8	res = _SUCCESS;
	
_func_enter_;

	ph2c = (struct cmd_obj*)rtw_zmalloc(sizeof(struct cmd_obj));	
	if(ph2c==NULL){
		res= _FAIL;
		goto exit;
	}
	
	pdrvextra_cmd_parm = (struct drvextra_cmd_parm*)rtw_zmalloc(sizeof(struct drvextra_cmd_parm)); 
	if(pdrvextra_cmd_parm==NULL){
		rtw_mfree((unsigned char *)ph2c, sizeof(struct cmd_obj));
		res= _FAIL;
		goto exit;
	}

	pdrvextra_cmd_parm->ec_id = INTEl_WIDI_WK_CID;
	pdrvextra_cmd_parm->type_size = intel_widi_state;
	pdrvextra_cmd_parm->pbuf = NULL;

	init_h2fwcmd_w_parm_no_rsp(ph2c, pdrvextra_cmd_parm, GEN_CMD_CODE(_Set_Drv_Extra));

	res = rtw_enqueue_cmd(pcmdpriv, ph2c);
	
exit:
	
_func_exit_;

	return res;

}
#endif //CONFIG_INTEL_WIDI

