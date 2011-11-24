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
#define _RTW_MLME_C_


#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <recv_osdep.h>
#include <xmit_osdep.h>
#include <hal_init.h>
#include <mlme_osdep.h>
#include <sta_info.h>
#include <wifi.h>
#include <wlan_bssdef.h>
#include <rtw_ioctl_set.h>

extern void indicate_wx_scan_complete_event(_adapter *padapter);
extern u8 rtw_do_join(_adapter * padapter);

sint	_rtw_init_mlme_priv (_adapter* padapter)
{
	sint	i;
	u8	*pbuf;
	struct wlan_network	*pnetwork;
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	sint	res = _SUCCESS;

_func_enter_;

	_rtw_memset((u8 *)pmlmepriv, 0, sizeof(struct mlme_priv));
	pmlmepriv->nic_hdl = (u8 *)padapter;

	pmlmepriv->pscanned = NULL;
	pmlmepriv->fw_state = 0;
	pmlmepriv->cur_network.network.InfrastructureMode = Ndis802_11AutoUnknown;
	pmlmepriv->scan_mode=SCAN_ACTIVE;// 1: active, 0: pasive. Maybe someday we should rename this varable to "active_mode" (Jeff)

	_rtw_spinlock_init(&(pmlmepriv->lock));	
	_rtw_init_queue(&(pmlmepriv->free_bss_pool));
	_rtw_init_queue(&(pmlmepriv->scanned_queue));

	set_scanned_network_val(pmlmepriv, 0);
	
	_rtw_memset(&pmlmepriv->assoc_ssid,0,sizeof(NDIS_802_11_SSID));

	pbuf = rtw_zvmalloc(MAX_BSS_CNT * (sizeof(struct wlan_network)));
	
	if (pbuf == NULL){
		res=_FAIL;
		goto exit;
	}
	pmlmepriv->free_bss_buf = pbuf;
		
	pnetwork = (struct wlan_network *)pbuf;
	
	for(i = 0; i < MAX_BSS_CNT; i++)
	{		
		_rtw_init_listhead(&(pnetwork->list));

		rtw_list_insert_tail(&(pnetwork->list), &(pmlmepriv->free_bss_pool.queue));

		pnetwork++;
	}

	//allocate DMA-able/Non-Page memory for cmd_buf and rsp_buf

	#ifdef CONFIG_SET_SCAN_DENY_TIMER
	ATOMIC_SET(&pmlmepriv->set_scan_deny, 0);
	#endif

	rtw_init_mlme_timer(padapter);

exit:

_func_exit_;

	return res;
}	

void rtw_mfree_mlme_priv_lock (struct mlme_priv *pmlmepriv)
{
	_rtw_spinlock_free(&pmlmepriv->lock);
	_rtw_spinlock_free(&(pmlmepriv->free_bss_pool.lock));
	_rtw_spinlock_free(&(pmlmepriv->scanned_queue.lock));
}

void _rtw_free_mlme_priv (struct mlme_priv *pmlmepriv)
{
_func_enter_;

	if(pmlmepriv){
		rtw_mfree_mlme_priv_lock (pmlmepriv);

		if (pmlmepriv->free_bss_buf) {
			rtw_vmfree(pmlmepriv->free_bss_buf, MAX_BSS_CNT * sizeof(struct wlan_network));
		}
	}
_func_exit_;	
}

sint	_rtw_enqueue_network(_queue *queue, struct wlan_network *pnetwork)
{
	_irqL irqL;

_func_enter_;	

	if (pnetwork == NULL)
		goto exit;
	
	_enter_critical_bh(&queue->lock, &irqL);

	rtw_list_insert_tail(&pnetwork->list, &queue->queue);

	_exit_critical_bh(&queue->lock, &irqL);

exit:	

_func_exit_;		

	return _SUCCESS;
}

struct	wlan_network *_rtw_dequeue_network(_queue *queue)
{
	_irqL irqL;

	struct wlan_network *pnetwork;

_func_enter_;	

	_enter_critical_bh(&queue->lock, &irqL);

	if (_rtw_queue_empty(queue) == _TRUE)

		pnetwork = NULL;
	
	else
	{
		pnetwork = LIST_CONTAINOR(get_next(&queue->queue), struct wlan_network, list);
		
		rtw_list_delete(&(pnetwork->list));
	}
	
	_exit_critical_bh(&queue->lock, &irqL);

_func_exit_;		

	return pnetwork;
}

struct	wlan_network *_rtw_alloc_network(struct	mlme_priv *pmlmepriv )//(_queue *free_queue)
{
	_irqL	irqL;
	struct	wlan_network	*pnetwork;	
	_queue *free_queue = &pmlmepriv->free_bss_pool;
	_list* plist = NULL;
	
_func_enter_;	

	_enter_critical_bh(&free_queue->lock, &irqL);
	
	if (_rtw_queue_empty(free_queue) == _TRUE) {
		pnetwork=NULL;
		goto exit;
	}
	plist = get_next(&(free_queue->queue));
	
	pnetwork = LIST_CONTAINOR(plist , struct wlan_network, list);
	
	rtw_list_delete(&pnetwork->list);
	
	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_, ("_rtw_alloc_network: ptr=%p\n", plist));
	pnetwork->network_type = 0;
	pnetwork->fixed = _FALSE;
	pnetwork->last_scanned = rtw_get_current_time();
	pnetwork->aid=0;	
	pnetwork->join_res=0;

	pmlmepriv->num_of_scanned ++;
	
exit:
	_exit_critical_bh(&free_queue->lock, &irqL);

_func_exit_;		

	return pnetwork;	
}

void _rtw_free_network(struct	mlme_priv *pmlmepriv ,struct wlan_network *pnetwork, u8 isfreeall)
{
	u32 curr_time, delta_time;
	u32 lifetime = SCANQUEUE_LIFETIME;
	_irqL irqL;	
	_queue *free_queue = &(pmlmepriv->free_bss_pool);
	
_func_enter_;		

	if (pnetwork == NULL)
		goto exit;

	if (pnetwork->fixed == _TRUE)
		goto exit;

	curr_time = rtw_get_current_time();	

	if ( (check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE)==_TRUE ) || 
		(check_fwstate(pmlmepriv, WIFI_ADHOC_STATE)==_TRUE ) )
		lifetime = 1;

	if(!isfreeall)
	{
#ifdef PLATFORM_WINDOWS

		delta_time = (curr_time -pnetwork->last_scanned)/10;

		if(delta_time  < lifetime*1000000)// unit:usec
		{
			goto exit;
		}

#endif

#ifdef PLATFORM_LINUX

		delta_time = (curr_time -pnetwork->last_scanned)/HZ;	

		if(delta_time < lifetime)// unit:sec
		{		
			goto exit;
		}

#endif
	}

	_enter_critical_bh(&free_queue->lock, &irqL);
	
	rtw_list_delete(&(pnetwork->list));

	rtw_list_insert_tail(&(pnetwork->list),&(free_queue->queue));
		
	pmlmepriv->num_of_scanned --;
	

	//DBG_871X("_rtw_free_network:SSID=%s\n", pnetwork->network.Ssid.Ssid);
	
	_exit_critical_bh(&free_queue->lock, &irqL);
	
exit:		
	
_func_exit_;			

}

void _rtw_free_network_nolock(struct	mlme_priv *pmlmepriv, struct wlan_network *pnetwork)
{

	_queue *free_queue = &(pmlmepriv->free_bss_pool);

_func_enter_;		

	if (pnetwork == NULL)
		goto exit;

	if (pnetwork->fixed == _TRUE)
		goto exit;

	//_enter_critical(&free_queue->lock, &irqL);
	
	rtw_list_delete(&(pnetwork->list));

	rtw_list_insert_tail(&(pnetwork->list), get_list_head(free_queue));
		
	pmlmepriv->num_of_scanned --;
	
	//_exit_critical(&free_queue->lock, &irqL);
	
exit:		

_func_exit_;			

}


/*
	return the wlan_network with the matching addr

	Shall be calle under atomic context... to avoid possible racing condition...
*/
struct wlan_network *_rtw_find_network(_queue *scanned_queue, u8 *addr)
{

	_irqL irqL;
	_list	*phead, *plist;
	struct	wlan_network *pnetwork = NULL;
	u8 zero_addr[ETH_ALEN] = {0,0,0,0,0,0};
	
_func_enter_;	

	if(_rtw_memcmp(zero_addr, addr, ETH_ALEN)){
		pnetwork=NULL;
		goto exit;
	}
	
	_enter_critical_bh(&scanned_queue->lock, &irqL);
	
	phead = get_list_head(scanned_queue);
	plist = get_next(phead);
	 
	while (plist != phead)
       {
                pnetwork = LIST_CONTAINOR(plist, struct wlan_network ,list);

		if (_rtw_memcmp(addr, pnetwork->network.MacAddress, ETH_ALEN) == _TRUE)
                        break;
		
		plist = get_next(plist);
        }

	if(plist == phead)
		pnetwork = NULL;

	_exit_critical_bh(&scanned_queue->lock, &irqL);
	
exit:		
	
_func_exit_;		

	return pnetwork;
	
}


void _rtw_free_network_queue(_adapter *padapter, u8 isfreeall)
{
	_irqL irqL;
	_list *phead, *plist;
	struct wlan_network *pnetwork;
	struct mlme_priv* pmlmepriv = &padapter->mlmepriv;
	_queue *scanned_queue = &pmlmepriv->scanned_queue;
	_queue	*free_queue = &pmlmepriv->free_bss_pool;
	u8 *mybssid = get_bssid(pmlmepriv);

_func_enter_;	
	

	_enter_critical_bh(&scanned_queue->lock, &irqL);

	phead = get_list_head(scanned_queue);
	plist = get_next(phead);

	while (rtw_end_of_queue_search(phead, plist) == _FALSE)
	{

		pnetwork = LIST_CONTAINOR(plist, struct wlan_network, list);

		plist = get_next(plist);

		_rtw_free_network(pmlmepriv,pnetwork, isfreeall);
		
	}

	_exit_critical_bh(&scanned_queue->lock, &irqL);
	
_func_exit_;		

}




sint rtw_if_up(_adapter *padapter)	{

	sint res;
_func_enter_;		

	if( padapter->bDriverStopped || padapter->bSurpriseRemoved ||
		(check_fwstate(&padapter->mlmepriv, _FW_LINKED)== _FALSE)){		
		RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_, ("rtw_if_up:bDriverStopped(%d) OR bSurpriseRemoved(%d)", padapter->bDriverStopped, padapter->bSurpriseRemoved));	
		res=_FALSE;
	}
	else
		res=  _TRUE;
	
_func_exit_;
	return res;
}


void rtw_generate_random_ibss(u8* pibss)
{
	u32	curtime = rtw_get_current_time();

_func_enter_;
	pibss[0] = 0x02;  //in ad-hoc mode bit1 must set to 1
	pibss[1] = 0x11;
	pibss[2] = 0x87;
	pibss[3] = (u8)(curtime & 0xff) ;//p[0];
	pibss[4] = (u8)((curtime>>8) & 0xff) ;//p[1];
	pibss[5] = (u8)((curtime>>16) & 0xff) ;//p[2];
_func_exit_;
	return;
}

u8 *rtw_get_capability_from_ie(u8 *ie)
{
	return (ie + 8 + 2);
}


u16 rtw_get_capability(WLAN_BSSID_EX *bss)
{
	u16	val;
_func_enter_;	

	_rtw_memcpy((u8 *)&val, rtw_get_capability_from_ie(bss->IEs), 2); 

_func_exit_;		
	return le16_to_cpu(val);
}

u8 *rtw_get_timestampe_from_ie(u8 *ie)
{
	return (ie + 0);	
}

u8 *rtw_get_beacon_interval_from_ie(u8 *ie)
{
	return (ie + 8);	
}


int	rtw_init_mlme_priv (_adapter *padapter)//(struct	mlme_priv *pmlmepriv)
{
	int	res;
_func_enter_;	
	res = _rtw_init_mlme_priv(padapter);// (pmlmepriv);
_func_exit_;	
	return res;
}

void rtw_free_mlme_priv (struct mlme_priv *pmlmepriv)
{
_func_enter_;
	RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("rtw_free_mlme_priv\n"));
	_rtw_free_mlme_priv (pmlmepriv);
_func_exit_;	
}

int	rtw_enqueue_network(_queue *queue, struct wlan_network *pnetwork)
{
	int	res;
_func_enter_;		
	res = _rtw_enqueue_network(queue, pnetwork);
_func_exit_;		
	return res;
}



static struct	wlan_network *rtw_dequeue_network(_queue *queue)
{
	struct wlan_network *pnetwork;
_func_enter_;		
	pnetwork = _rtw_dequeue_network(queue);
_func_exit_;		
	return pnetwork;
}


struct	wlan_network *rtw_alloc_network(struct	mlme_priv *pmlmepriv )//(_queue	*free_queue)
{
	struct	wlan_network	*pnetwork;
_func_enter_;			
	pnetwork = _rtw_alloc_network(pmlmepriv);
_func_exit_;			
	return pnetwork;
}

void rtw_free_network(struct mlme_priv *pmlmepriv, struct	wlan_network *pnetwork, u8 is_freeall)//(struct	wlan_network *pnetwork, _queue	*free_queue)
{
_func_enter_;		
	RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("rtw_free_network==> ssid = %s \n\n" , pnetwork->network.Ssid.Ssid));
	_rtw_free_network(pmlmepriv, pnetwork, is_freeall);
_func_exit_;		
}


void rtw_free_network_nolock(struct mlme_priv *pmlmepriv, struct wlan_network *pnetwork )
{
_func_enter_;		
	//RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("rtw_free_network==> ssid = %s \n\n" , pnetwork->network.Ssid.Ssid));
	_rtw_free_network_nolock(pmlmepriv, pnetwork);
_func_exit_;		
}


void rtw_free_network_queue(_adapter* dev, u8 isfreeall)
{
_func_enter_;		
	_rtw_free_network_queue(dev, isfreeall);
_func_exit_;			
}

/*
	return the wlan_network with the matching addr

	Shall be calle under atomic context... to avoid possible racing condition...
*/
struct	wlan_network *rtw_find_network(_queue *scanned_queue, u8 *addr)
{
	struct	wlan_network *pnetwork = _rtw_find_network(scanned_queue, addr);

	return pnetwork;
}

int rtw_is_same_ibss(_adapter *adapter, struct wlan_network *pnetwork)
{
	int ret=_TRUE;
	struct security_priv *psecuritypriv = &adapter->securitypriv;

	if ( (psecuritypriv->dot11PrivacyAlgrthm != _NO_PRIVACY_ ) &&
		    ( pnetwork->network.Privacy == 0 ) )
	{
		ret=_FALSE;
	}
	else if((psecuritypriv->dot11PrivacyAlgrthm == _NO_PRIVACY_ ) &&
		 ( pnetwork->network.Privacy == 1 ) )
	{
		ret=_FALSE;
	}
	else
	{
		ret=_TRUE;
	}
	
	return ret;
	
}

inline int is_same_ess(WLAN_BSSID_EX *a, WLAN_BSSID_EX *b)
{
	//RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("(%s,%d)(%s,%d)\n",
	//		a->Ssid.Ssid,a->Ssid.SsidLength,b->Ssid.Ssid,b->Ssid.SsidLength));
	return (a->Ssid.SsidLength == b->Ssid.SsidLength) 
		&&  _rtw_memcmp(a->Ssid.Ssid, b->Ssid.Ssid, a->Ssid.SsidLength)==_TRUE;
}

static int is_same_network(WLAN_BSSID_EX *src, WLAN_BSSID_EX *dst)
{
	 u16 s_cap, d_cap;
	 
_func_enter_;	

#ifdef PLATFORM_OS_XP
	 if ( ((uint)dst) <= 0x7fffffff || 
	 	((uint)src) <= 0x7fffffff ||
	 	((uint)&s_cap) <= 0x7fffffff ||
	 	((uint)&d_cap) <= 0x7fffffff)
	{
		RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("\n@@@@ error address of dst\n"));
			
		KeBugCheckEx(0x87110000, (ULONG_PTR)dst, (ULONG_PTR)src,(ULONG_PTR)&s_cap, (ULONG_PTR)&d_cap);

		return _FALSE;
	}
#endif


	_rtw_memcpy((u8 *)&s_cap, rtw_get_capability_from_ie(src->IEs), 2);
	_rtw_memcpy((u8 *)&d_cap, rtw_get_capability_from_ie(dst->IEs), 2);

	
	s_cap = le16_to_cpu(s_cap);
	d_cap = le16_to_cpu(d_cap);
	
_func_exit_;			

	return ((src->Ssid.SsidLength == dst->Ssid.SsidLength) &&
			(src->Configuration.DSConfig == dst->Configuration.DSConfig) &&
			( (_rtw_memcmp(src->MacAddress, dst->MacAddress, ETH_ALEN)) == _TRUE) &&
			( (_rtw_memcmp(src->Ssid.Ssid, dst->Ssid.Ssid, src->Ssid.SsidLength)) == _TRUE) &&
			((s_cap & WLAN_CAPABILITY_IBSS) == 
			(d_cap & WLAN_CAPABILITY_IBSS)) &&
			((s_cap & WLAN_CAPABILITY_BSS) == 
			(d_cap & WLAN_CAPABILITY_BSS)));
	
}

struct	wlan_network	* rtw_get_oldest_wlan_network(_queue *scanned_queue)
{
	_list	*plist, *phead;

	
	struct	wlan_network	*pwlan = NULL;
	struct	wlan_network	*oldest = NULL;
_func_enter_;		
	phead = get_list_head(scanned_queue);
	
	plist = get_next(phead);

	while(1)
	{
		
		if (rtw_end_of_queue_search(phead,plist)== _TRUE)
			break;
		
		pwlan= LIST_CONTAINOR(plist, struct wlan_network, list);

		if(pwlan->fixed!=_TRUE)
		{		
			if (oldest == NULL ||time_after(oldest->last_scanned, pwlan->last_scanned))
				oldest = pwlan;
		}
		
		plist = get_next(plist);
	}
_func_exit_;		
	return oldest;
	
}

static void update_network(WLAN_BSSID_EX *dst, WLAN_BSSID_EX *src,_adapter * padapter)
{
	u32 last_evm = 0, tmpVal;

_func_enter_;		

#ifdef CONFIG_ANTENNA_DIVERSITY
	padapter->HalFunc.SwAntDivCompareHandler(padapter, dst, src);
#endif

	
	//Update signal strength first. Alwlays using the newest value will cause large vibration of scan result's signal strength 
	if (check_fwstate(&padapter->mlmepriv, _FW_LINKED) && is_same_network(&(padapter->mlmepriv.cur_network.network), src)) {
		//Because we've process the rx phy info in rtl8192c_process_phy_info/rtl8192d_process_phy_info,
		//we can just take the recvpriv's value
		src->PhyInfo.SignalStrength = padapter->recvpriv.signal_strength;
		src->PhyInfo.SignalQuality = padapter->recvpriv.signal_qual;
		src->Rssi= translate_percentage_to_dbm(padapter->recvpriv.signal_strength);
	}
	else {
		src->PhyInfo.SignalStrength = (src->PhyInfo.SignalStrength+dst->PhyInfo.SignalStrength*4)/5;
		src->PhyInfo.SignalQuality = (src->PhyInfo.SignalQuality+dst->PhyInfo.SignalQuality*4)/5;
		src->Rssi=(src->Rssi+dst->Rssi*4)/5;
	}


	_rtw_memcpy((u8 *)dst, (u8 *)src, get_WLAN_BSSID_EX_sz(src));

	 #if 0
	 if(dst->Ssid.Ssid[0]=='j') {
		DBG_871X("%s %s("MAC_FMT"), SignalStrength:%u, SignalQuality:%u, rssi:%d\n", __FUNCTION__
			, dst->Ssid.Ssid, MAC_ARG(dst->MacAddress), dst->PhyInfo.SignalStrength, dst->PhyInfo.SignalQuality, (int)dst->Rssi);
	 }
	 #endif

#if 0 // old codes, may be useful one day...
//	DBG_8192C("update_network: rssi=0x%lx dst->Rssi=%d ,dst->Rssi=0x%lx , src->Rssi=0x%lx",(dst->Rssi+src->Rssi)/2,dst->Rssi,dst->Rssi,src->Rssi);
	if (check_fwstate(&padapter->mlmepriv, _FW_LINKED) && is_same_network(&(padapter->mlmepriv.cur_network.network), src))
	{
	
		//DBG_8192C("b:ssid=%s update_network: src->rssi=0x%d padapter->recvpriv.ui_rssi=%d\n",src->Ssid.Ssid,src->Rssi,padapter->recvpriv.signal);
		if(padapter->recvpriv.signal_qual_data.total_num++ >= PHY_LINKQUALITY_SLID_WIN_MAX)
	        {
	              padapter->recvpriv.signal_qual_data.total_num = PHY_LINKQUALITY_SLID_WIN_MAX;
	              last_evm = padapter->recvpriv.signal_qual_data.elements[padapter->recvpriv.signal_qual_data.index];
	              padapter->recvpriv.signal_qual_data.total_val -= last_evm;
	        }
               	padapter->recvpriv.signal_qual_data.total_val += query_rx_pwr_percentage(src->Rssi);

              	padapter->recvpriv.signal_qual_data.elements[padapter->recvpriv.signal_qual_data.index++] = query_rx_pwr_percentage(src->Rssi);
                if(padapter->recvpriv.signal_qual_data.index >= PHY_LINKQUALITY_SLID_WIN_MAX)
                       padapter->recvpriv.signal_qual_data.index = 0;

		//DBG_8192C("Total SQ=%d  pattrib->signal_qual= %d\n", padapter->recvpriv.signal_qual_data.total_val, src->Rssi);

		// <1> Showed on UI for user,in percentage.
		tmpVal = padapter->recvpriv.signal_qual_data.total_val/padapter->recvpriv.signal_qual_data.total_num;
                padapter->recvpriv.signal=(u8)tmpVal;//Link quality

		src->Rssi= translate_percentage_to_dbm(padapter->recvpriv.signal) ;
	}
	else{
//	DBG_8192C("ELSE:ssid=%s update_network: src->rssi=0x%d dst->rssi=%d\n",src->Ssid.Ssid,src->Rssi,dst->Rssi);
		src->Rssi=(src->Rssi +dst->Rssi)/2;//dBM
	}	

//	DBG_8192C("a:update_network: src->rssi=0x%d padapter->recvpriv.ui_rssi=%d\n",src->Rssi,padapter->recvpriv.signal);

#endif

_func_exit_;		
}

static void update_current_network(_adapter *adapter, WLAN_BSSID_EX *pnetwork)
{
	struct	mlme_priv	*pmlmepriv = &(adapter->mlmepriv);
	
_func_enter_;		

#ifdef PLATFORM_OS_XP
	if ((unsigned long)(&(pmlmepriv->cur_network.network)) < 0x7ffffff)
	{		
		KeBugCheckEx(0x87111c1c, (ULONG_PTR)(&(pmlmepriv->cur_network.network)), 0, 0,0);
	}
#endif

	if ( (check_fwstate(pmlmepriv, _FW_LINKED)== _TRUE) && (is_same_network(&(pmlmepriv->cur_network.network), pnetwork)))
	{
		//RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,"Same Network\n");

		//if(pmlmepriv->cur_network.network.IELength<= pnetwork->IELength)
		{
			update_network(&(pmlmepriv->cur_network.network), pnetwork,adapter);
			rtw_update_protection(adapter, (pmlmepriv->cur_network.network.IEs) + sizeof (NDIS_802_11_FIXED_IEs), 
									pmlmepriv->cur_network.network.IELength);
		}
	}

_func_exit_;			

}


/*

Caller must hold pmlmepriv->lock first.


*/
void rtw_update_scanned_network(_adapter *adapter, WLAN_BSSID_EX *target)
{
	_list	*plist, *phead;
	ULONG	bssid_ex_sz;
	struct mlme_priv	*pmlmepriv = &(adapter->mlmepriv);
	_queue	*queue	= &(pmlmepriv->scanned_queue);
	struct wlan_network	*pnetwork = NULL;
	struct wlan_network	*oldest = NULL;

_func_enter_;

	phead = get_list_head(queue);
	plist = get_next(phead);

	while(1)
	{
		if (rtw_end_of_queue_search(phead,plist)== _TRUE)
			break;

		pnetwork	= LIST_CONTAINOR(plist, struct wlan_network, list);

		if ((unsigned long)(pnetwork) < 0x7ffffff)
		{
#ifdef PLATFORM_OS_XP
			KeBugCheckEx(0x87111c1c, (ULONG_PTR)pnetwork, 0, 0,0);
#endif
		}

		if (is_same_network(&(pnetwork->network), target))
			break;

		if ((oldest == ((struct wlan_network *)0)) ||
		time_after(oldest->last_scanned, pnetwork->last_scanned))
			oldest = pnetwork;

		plist = get_next(plist);

	}
	
	
	/* If we didn't find a match, then get a new network slot to initialize
	 * with this beacon's information */
	if (rtw_end_of_queue_search(phead,plist)== _TRUE) {
		
		if (_rtw_queue_empty(&(pmlmepriv->free_bss_pool)) == _TRUE) {
			/* If there are no more slots, expire the oldest */
			//list_del_init(&oldest->list);
			pnetwork = oldest;

#ifdef CONFIG_ANTENNA_DIVERSITY
			//target->PhyInfo.Optimum_antenna = pHalData->CurAntenna;//optimum_antenna=>For antenna diversity
			adapter->HalFunc.GetHalDefVarHandler(adapter, HAL_DEF_CURRENT_ANTENNA, &(target->PhyInfo.Optimum_antenna));
#endif
			_rtw_memcpy(&(pnetwork->network), target,  get_WLAN_BSSID_EX_sz(target));
			pnetwork->last_scanned = rtw_get_current_time();
                        //variable initialize
			pnetwork->fixed = _FALSE;			
			pnetwork->last_scanned = rtw_get_current_time();

			pnetwork->network_type = 0;	
			pnetwork->aid=0;		
			pnetwork->join_res=0;
		}
		else {
			/* Otherwise just pull from the free list */

			pnetwork = rtw_alloc_network(pmlmepriv); // will update scan_time

			if(pnetwork==NULL){ 
				RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("\n\n\nsomething wrong here\n\n\n"));
				goto exit;
			}

			bssid_ex_sz = get_WLAN_BSSID_EX_sz(target);
			target->Length = bssid_ex_sz;
#ifdef CONFIG_ANTENNA_DIVERSITY
			//target->PhyInfo.Optimum_antenna = pHalData->CurAntenna;
			adapter->HalFunc.GetHalDefVarHandler(adapter, HAL_DEF_CURRENT_ANTENNA, &(target->PhyInfo.Optimum_antenna));
#endif
			_rtw_memcpy(&(pnetwork->network), target, bssid_ex_sz );

			rtw_list_insert_tail(&(pnetwork->list),&(queue->queue)); 

		}
	}
	else {
		/* we have an entry and we are going to update it. But this entry may
		 * be already expired. In this case we do the same as we found a new 
		 * net and call the new_net handler
		 */

		//target.Reserved[0]==1, means that scaned network is a bcn frame.
		if((pnetwork->network.IELength>target->IELength) && (target->Reserved[0]==1))
			goto exit;

		update_network(&(pnetwork->network),target,adapter);

		pnetwork->last_scanned = rtw_get_current_time();

	}

exit:

_func_exit_;

}


void rtw_add_network(_adapter *adapter, WLAN_BSSID_EX *pnetwork)
{
	_irqL irqL;
	struct	mlme_priv	*pmlmepriv = &(((_adapter *)adapter)->mlmepriv);
	_queue	*queue	= &(pmlmepriv->scanned_queue);

_func_enter_;		

	_enter_critical_bh(&queue->lock, &irqL);
	
	update_current_network(adapter, pnetwork);
	
	rtw_update_scanned_network(adapter, pnetwork);

	_exit_critical_bh(&queue->lock, &irqL);
	
_func_exit_;		
}

//select the desired network based on the capability of the (i)bss.
// check items: (1) security
//			   (2) network_type
//			   (3) WMM
//			   (4) HT
//                     (5) others
int rtw_is_desired_network(_adapter *adapter, struct wlan_network *pnetwork)
{
	struct security_priv *psecuritypriv = &adapter->securitypriv;
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	u32 desired_encmode;
	u32 privacy;

	//u8 wps_ie[512];
	uint wps_ielen;

	int bselected = _TRUE;
	
	desired_encmode = psecuritypriv->ndisencryptstatus;
	privacy = pnetwork->network.Privacy;

	if(psecuritypriv->wps_phase == _TRUE)
	{
		if(rtw_get_wps_ie(pnetwork->network.IEs, pnetwork->network.IELength, NULL, &wps_ielen)!=NULL)
		{
			//rtw_disassoc_cmd(adapter);			
			//rtw_indicate_disconnect(adapter);
			//rtw_free_assoc_resources(adapter);
			return _TRUE;
		}
		else
		{	
			return _FALSE;
		}	
	}
	if (adapter->registrypriv.wifi_spec == 1) //for  correct flow of 8021X  to do....
	{
		if ((desired_encmode == Ndis802_11EncryptionDisabled) && (privacy != 0))
	            bselected = _FALSE;
	}
	

 	if ((desired_encmode != Ndis802_11EncryptionDisabled) && (privacy == 0)) {
		DBG_871X("desired_encmode: %d, privacy: %d\n", desired_encmode, privacy);
		bselected = _FALSE;
 	}

	if(check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == _TRUE)
	{
		if(pnetwork->network.InfrastructureMode != pmlmepriv->cur_network.network.InfrastructureMode)
			bselected = _FALSE;
	}	
		

	return bselected;
}

/* TODO: Perry : For Power Management */
void rtw_atimdone_event_callback(_adapter	*adapter , u8 *pbuf)
{

_func_enter_;		
	RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("receive atimdone_evet\n"));	
_func_exit_;			
	return;	
}


void rtw_survey_event_callback(_adapter	*adapter, u8 *pbuf)
{
	_irqL  irqL;
	u32 len;
	WLAN_BSSID_EX *pnetwork;
	struct	mlme_priv	*pmlmepriv = &(adapter->mlmepriv);

_func_enter_;		

	pnetwork = (WLAN_BSSID_EX *)pbuf;

	RT_TRACE(_module_rtl871x_mlme_c_,_drv_info_,("rtw_survey_event_callback, ssid=%s\n",  pnetwork->Ssid.Ssid));

#ifdef CONFIG_RTL8712
        //endian_convert
 	pnetwork->Length = le32_to_cpu(pnetwork->Length);
  	pnetwork->Ssid.SsidLength = le32_to_cpu(pnetwork->Ssid.SsidLength);	
	pnetwork->Privacy =le32_to_cpu( pnetwork->Privacy);
	pnetwork->Rssi = le32_to_cpu(pnetwork->Rssi);
	pnetwork->NetworkTypeInUse =le32_to_cpu(pnetwork->NetworkTypeInUse);	
	pnetwork->Configuration.ATIMWindow = le32_to_cpu(pnetwork->Configuration.ATIMWindow);
	pnetwork->Configuration.BeaconPeriod = le32_to_cpu(pnetwork->Configuration.BeaconPeriod);
	pnetwork->Configuration.DSConfig =le32_to_cpu(pnetwork->Configuration.DSConfig);
	pnetwork->Configuration.FHConfig.DwellTime=le32_to_cpu(pnetwork->Configuration.FHConfig.DwellTime);
	pnetwork->Configuration.FHConfig.HopPattern=le32_to_cpu(pnetwork->Configuration.FHConfig.HopPattern);
	pnetwork->Configuration.FHConfig.HopSet=le32_to_cpu(pnetwork->Configuration.FHConfig.HopSet);
	pnetwork->Configuration.FHConfig.Length=le32_to_cpu(pnetwork->Configuration.FHConfig.Length);	
	pnetwork->Configuration.Length = le32_to_cpu(pnetwork->Configuration.Length);
	pnetwork->InfrastructureMode = le32_to_cpu(pnetwork->InfrastructureMode);
	pnetwork->IELength = le32_to_cpu(pnetwork->IELength);
#endif	

	len = get_WLAN_BSSID_EX_sz(pnetwork);
	if(len > (sizeof(WLAN_BSSID_EX)))
	{
		RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("\n ****rtw_survey_event_callback: return a wrong bss ***\n"));
		goto exit;
	}


	_enter_critical_bh(&pmlmepriv->lock, &irqL);

	// update IBSS_network 's timestamp
	if ((check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE)) == _TRUE)
	{
		//RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,"rtw_survey_event_callback : WIFI_ADHOC_MASTER_STATE \n\n");
		if(_rtw_memcmp(&(pmlmepriv->cur_network.network.MacAddress), pnetwork->MacAddress, ETH_ALEN))
		{
			struct wlan_network* ibss_wlan = NULL;
			
			_rtw_memcpy(pmlmepriv->cur_network.network.IEs, pnetwork->IEs, 8);

			ibss_wlan = rtw_find_network(&pmlmepriv->scanned_queue,  pnetwork->MacAddress);
			if(ibss_wlan)
			{
				_rtw_memcpy(ibss_wlan->network.IEs , pnetwork->IEs, 8);			
				goto exit;
			}
		}
	}

	// lock pmlmepriv->lock when you accessing network_q
	if ((check_fwstate(pmlmepriv, _FW_UNDER_LINKING)) == _FALSE)
	{		
   	        if( pnetwork->Ssid.Ssid[0] == 0 )
		{
			pnetwork->Ssid.SsidLength = 0;
		}	
		rtw_add_network(adapter, pnetwork);
	}	

exit:	
		
	_exit_critical_bh(&pmlmepriv->lock, &irqL);

_func_exit_;		

	return;	
}



void rtw_surveydone_event_callback(_adapter	*adapter, u8 *pbuf)
{
	_irqL  irqL;
	struct	mlme_priv	*pmlmepriv = &(adapter->mlmepriv);
	
#ifdef CONFIG_MLME_EXT	

	mlmeext_surveydone_event_callback(adapter);

#endif

_func_enter_;			

	_enter_critical_bh(&pmlmepriv->lock, &irqL);

	pmlmepriv->probereq_wpsie_len = 0 ;//reset to zero	
	
	RT_TRACE(_module_rtl871x_mlme_c_,_drv_info_,("rtw_surveydone_event_callback: fw_state:%x\n\n", get_fwstate(pmlmepriv)));
	
	if (check_fwstate(pmlmepriv,_FW_UNDER_SURVEY))
	{
		u8 timer_cancelled;
		
		_cancel_timer(&pmlmepriv->scan_to_timer, &timer_cancelled);
		
		_clr_fwstate_(pmlmepriv, _FW_UNDER_SURVEY);
	}
	else {
	
		RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("nic status =%x, survey done event comes too late!\n", get_fwstate(pmlmepriv)));	
	}	
	
	#ifdef CONFIG_NEW_SIGNAL_STAT_PROCESS
	rtw_set_signal_stat_timer(&adapter->recvpriv);
	#endif
	
	if(pmlmepriv->to_join == _TRUE)
	{
		if((check_fwstate(pmlmepriv, WIFI_ADHOC_STATE)==_TRUE) )
		{
			if(check_fwstate(pmlmepriv, _FW_LINKED)==_FALSE)
			{
				set_fwstate(pmlmepriv, _FW_UNDER_LINKING);	
				
		   		if(rtw_select_and_join_from_scanned_queue(pmlmepriv)==_SUCCESS)
		   		{
		       			_set_timer(&pmlmepriv->assoc_timer, MAX_JOIN_TIMEOUT );	 
                  		}
		   		else	
		  		{
					WLAN_BSSID_EX    *pdev_network = &(adapter->registrypriv.dev_network); 			
					u8 *pibss = adapter->registrypriv.dev_network.MacAddress;

					//pmlmepriv->fw_state ^= _FW_UNDER_SURVEY;//because don't set assoc_timer
					_clr_fwstate_(pmlmepriv, _FW_UNDER_SURVEY);

					RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("switching to adhoc master\n"));
				
					_rtw_memset(&pdev_network->Ssid, 0, sizeof(NDIS_802_11_SSID));
					_rtw_memcpy(&pdev_network->Ssid, &pmlmepriv->assoc_ssid, sizeof(NDIS_802_11_SSID));
	
					rtw_update_registrypriv_dev_network(adapter);
					rtw_generate_random_ibss(pibss);

                       			pmlmepriv->fw_state = WIFI_ADHOC_MASTER_STATE;
			
					if(rtw_createbss_cmd(adapter)!=_SUCCESS)
					{
	                     		RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("Error=>rtw_createbss_cmd status FAIL\n"));						
					}	

			     		pmlmepriv->to_join = _FALSE;
		   		}
		 	}
		}
		else
		{
			set_fwstate(pmlmepriv, _FW_UNDER_LINKING);
			pmlmepriv->to_join = _FALSE;
			if(rtw_select_and_join_from_scanned_queue(pmlmepriv)==_SUCCESS)
			{
	     		     _set_timer(&pmlmepriv->assoc_timer, MAX_JOIN_TIMEOUT);	 
			}
			else
			{
				#ifdef CONFIG_LAYER2_ROAMING
				DBG_871X("try_to_join, but select scanning queue fail, to_roaming:%d\n", pmlmepriv->to_roaming);
				#else
				DBG_871X("try_to_join, but select scanning queue fail\n");
				#endif

				#ifdef CONFIG_LAYER2_ROAMING
				if(pmlmepriv->to_roaming!=0) {
					if( --pmlmepriv->to_roaming == 0
						|| _SUCCESS != rtw_sitesurvey_cmd(adapter, &pmlmepriv->assoc_ssid)
					) {
						pmlmepriv->to_roaming = 0;
						rtw_free_assoc_resources(adapter);
						rtw_indicate_disconnect(adapter);
					} else {
						pmlmepriv->to_join = _TRUE;
					}
				}
				#endif
				_clr_fwstate_(pmlmepriv, _FW_UNDER_LINKING);
			}
		}
	} 
	//else
	{
		/*
		*  20110324 Commented by Jeff
		*  indicate the scan complete event when this scan isn't caused by join
		*/
		indicate_wx_scan_complete_event(adapter);

		//DBG_871X("scan complete in %dms\n",rtw_get_passing_time_ms(pmlmepriv->scan_start_time));

	}

	_exit_critical_bh(&pmlmepriv->lock, &irqL);

#ifdef CONFIG_P2P
	p2p_ps_wk_cmd(adapter, P2P_PS_SCAN_DONE, 0);
#endif //CONFIG_P2P

	rtw_os_xmit_schedule(adapter);

#ifdef CONFIG_DRVEXT_MODULE_WSC
	drvext_surveydone_callback(&adapter->drvextpriv);
#endif

#ifdef SILENT_RESET_FOR_SPECIFIC_PLATFOM
	{
		struct mlme_ext_priv *pmlmeext = &adapter->mlmeextpriv;		
		if(pmlmeext->sitesurvey_res.bss_cnt == 0){
			if(adapter->HalFunc.silentreset)
				adapter->HalFunc.silentreset(adapter);			
		}
	}
	#endif	
_func_exit_;	

}

void rtw_dummy_event_callback(_adapter *adapter , u8 *pbuf)
{

}

void rtw_fwdbg_event_callback(_adapter *adapter , u8 *pbuf)
{

}

static void free_scanqueue(struct	mlme_priv *pmlmepriv)
{
	_irqL irqL;
	_queue *free_queue = &pmlmepriv->free_bss_pool;
	_queue *scan_queue = &pmlmepriv->scanned_queue;
	_list	*plist, *phead, *ptemp;
	
_func_enter_;		
	
	RT_TRACE(_module_rtl871x_mlme_c_, _drv_notice_, ("+free_scanqueue\n"));

	_enter_critical_bh(&free_queue->lock, &irqL);

	phead = get_list_head(scan_queue);
	plist = get_next(phead);

	while (plist != phead)
       {
		ptemp = get_next(plist);
		rtw_list_delete(plist);
		rtw_list_insert_tail(plist, &free_queue->queue);
		plist =ptemp;
		pmlmepriv->num_of_scanned --;
        }
	
	_exit_critical_bh(&free_queue->lock, &irqL);
	
_func_exit_;
}
	
/*
*rtw_free_assoc_resources: the caller has to lock pmlmepriv->lock
*/
void rtw_free_assoc_resources(_adapter *adapter )
{
	_irqL irqL;
	struct wlan_network* pwlan = NULL;
     	struct	mlme_priv *pmlmepriv = &adapter->mlmepriv;
	struct 	mlme_ext_info *pmlmeinfo = &adapter->mlmeextpriv.mlmext_info;
   	struct	sta_priv *pstapriv = &adapter->stapriv;
	struct wlan_network *tgt_network = &pmlmepriv->cur_network;
	
_func_enter_;			

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_notice_, ("+rtw_free_assoc_resources\n"));

	pwlan = rtw_find_network(&pmlmepriv->scanned_queue, tgt_network->network.MacAddress);
	
	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_, ("tgt_network->network.MacAddress="MAC_FMT" ssid=%s\n",
		MAC_ARG(tgt_network->network.MacAddress), tgt_network->network.Ssid.Ssid));

	if(check_fwstate( pmlmepriv, WIFI_STATION_STATE|WIFI_AP_STATE))
	{
		struct sta_info* psta;
		
		psta = rtw_get_stainfo(&adapter->stapriv, tgt_network->network.MacAddress);

		_enter_critical_bh(&(pstapriv->sta_hash_lock), &irqL);
		rtw_free_stainfo(adapter,  psta);
		_exit_critical_bh(&(pstapriv->sta_hash_lock), &irqL);
		
	}

	if(check_fwstate( pmlmepriv, WIFI_ADHOC_STATE|WIFI_ADHOC_MASTER_STATE|WIFI_AP_STATE))
	{
		struct sta_info* psta;
	
		rtw_free_all_stainfo(adapter);

		psta = rtw_get_bcmc_stainfo(adapter);
		_enter_critical_bh(&(pstapriv->sta_hash_lock), &irqL);		
		rtw_free_stainfo(adapter, psta);
		_exit_critical_bh(&(pstapriv->sta_hash_lock), &irqL);		

		rtw_init_bcmc_stainfo(adapter);	
	}

	if(pwlan)		
	{
		pwlan->fixed = _FALSE;
	}	
	else
	{
		RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("rtw_free_assoc_resources : pwlan== NULL \n\n"));
	}


	if((check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) && (adapter->stapriv.asoc_sta_count== 1))
		/*||check_fwstate(pmlmepriv, WIFI_STATION_STATE)*/)
	{
		rtw_free_network_nolock(pmlmepriv, pwlan); 
	}
	pmlmepriv->key_mask = 0;

_func_exit_;	
	
}

/*
*rtw_indicate_connect: the caller has to lock pmlmepriv->lock
*/
void rtw_indicate_connect(_adapter *padapter)
{
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct xmit_priv	*pxmitpriv = &padapter->xmitpriv;
	
_func_enter_;

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_, ("+rtw_indicate_connect\n"));
 
	pmlmepriv->to_join = _FALSE;
#ifdef CONFIG_SW_ANTENNA_DIVERSITY
	padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_ANTENNA_DIVERSITY_LINK, 0);
#endif
	set_fwstate(pmlmepriv, _FW_LINKED);

	rtw_led_control(padapter, LED_CTL_LINK);	

#ifdef CONFIG_DRVEXT_MODULE
	if(padapter->drvextpriv.enable_wpa)
	{
		indicate_l2_connect(padapter);
	}
	else
#endif
	{
		rtw_os_indicate_connect(padapter);
	}

	#ifdef CONFIG_LAYER2_ROAMING
	pmlmepriv->to_roaming=0;
	#endif

	#ifdef CONFIG_SET_SCAN_DENY_TIMER
	rtw_set_scan_deny(pmlmepriv, 3000);
	#endif

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_, ("-rtw_indicate_connect: fw_state=0x%08x\n", get_fwstate(pmlmepriv)));
 
_func_exit_;

}


/*
*rtw_indicate_connect: the caller has to lock pmlmepriv->lock
*/
void rtw_indicate_disconnect( _adapter *padapter )
{
	struct	mlme_priv *pmlmepriv = &padapter->mlmepriv;	

_func_enter_;	
	
	RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_, ("+rtw_indicate_disconnect\n"));

	_clr_fwstate_(pmlmepriv, _FW_LINKED);

	rtw_led_control(padapter, LED_CTL_NO_LINK);

	#ifdef CONFIG_LAYER2_ROAMING
	if(pmlmepriv->to_roaming<=0)
	#endif
		rtw_os_indicate_disconnect(padapter);

#ifdef CONFIG_LPS
	rtw_lps_ctrl_wk_cmd(padapter, LPS_CTRL_DISCONNECT, 1);
#endif

#ifdef CONFIG_P2P
	p2p_ps_wk_cmd(padapter, P2P_PS_DISABLE, 1);
#endif //CONFIG_P2P

_func_exit_;	
}

//Notes:
//pnetwork : returns from rtw_joinbss_event_callback
//ptarget_wlan: found from scanned_queue
//if join_res > 0, for (fw_state==WIFI_STATION_STATE), we check if  "ptarget_sta" & "ptarget_wlan" exist.	
//if join_res > 0, for (fw_state==WIFI_ADHOC_STATE), we only check if "ptarget_wlan" exist.
//if join_res > 0, update "cur_network->network" from "pnetwork->network" if (ptarget_wlan !=NULL).
//


//#define REJOIN
#ifdef CONFIG_HANDLE_JOINBSS_ON_ASSOC_RSP
void joinbss_event_prehandle(_adapter *adapter, u8 *pbuf)
{
	_irqL irqL,irqL2;
	int	res;
	static u8 retry=0;
	u8 timer_cancelled;
	struct sta_info *ptarget_sta= NULL, *pcur_sta = NULL;
   	struct	sta_priv *pstapriv = &adapter->stapriv;
	struct	mlme_priv	*pmlmepriv = &(adapter->mlmepriv);
	struct wlan_network 	*pnetwork	= (struct wlan_network *)pbuf;
	struct wlan_network 	*cur_network = &(pmlmepriv->cur_network);
	struct wlan_network	*pcur_wlan = NULL, *ptarget_wlan = NULL;
	unsigned int 		the_same_macaddr = _FALSE;	

_func_enter_;	

#ifdef CONFIG_RTL8712
       //endian_convert
	pnetwork->join_res = le32_to_cpu(pnetwork->join_res);
	pnetwork->network_type = le32_to_cpu(pnetwork->network_type);
	pnetwork->network.Length = le32_to_cpu(pnetwork->network.Length);
	pnetwork->network.Ssid.SsidLength = le32_to_cpu(pnetwork->network.Ssid.SsidLength);
	pnetwork->network.Privacy =le32_to_cpu( pnetwork->network.Privacy);
	pnetwork->network.Rssi = le32_to_cpu(pnetwork->network.Rssi);
	pnetwork->network.NetworkTypeInUse =le32_to_cpu(pnetwork->network.NetworkTypeInUse) ;	
	pnetwork->network.Configuration.ATIMWindow = le32_to_cpu(pnetwork->network.Configuration.ATIMWindow);
	pnetwork->network.Configuration.BeaconPeriod = le32_to_cpu(pnetwork->network.Configuration.BeaconPeriod);
	pnetwork->network.Configuration.DSConfig = le32_to_cpu(pnetwork->network.Configuration.DSConfig);
	pnetwork->network.Configuration.FHConfig.DwellTime=le32_to_cpu(pnetwork->network.Configuration.FHConfig.DwellTime);
	pnetwork->network.Configuration.FHConfig.HopPattern=le32_to_cpu(pnetwork->network.Configuration.FHConfig.HopPattern);
	pnetwork->network.Configuration.FHConfig.HopSet=le32_to_cpu(pnetwork->network.Configuration.FHConfig.HopSet);
	pnetwork->network.Configuration.FHConfig.Length=le32_to_cpu(pnetwork->network.Configuration.FHConfig.Length);	
	pnetwork->network.Configuration.Length = le32_to_cpu(pnetwork->network.Configuration.Length);
	pnetwork->network.InfrastructureMode = le32_to_cpu(pnetwork->network.InfrastructureMode);
	pnetwork->network.IELength = le32_to_cpu(pnetwork->network.IELength );
#endif

	RT_TRACE(_module_rtl871x_mlme_c_,_drv_info_,("joinbss event call back received with res=%d\n", pnetwork->join_res));

	rtw_get_encrypt_decrypt_from_registrypriv(adapter);
	

	if (pmlmepriv->assoc_ssid.SsidLength == 0)
	{
		RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("@@@@@   joinbss event call back  for Any SSid\n"));		
	}
	else
	{
		RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("@@@@@   rtw_joinbss_event_callback for SSid:%s\n", pmlmepriv->assoc_ssid.Ssid));
	}
	
	the_same_macaddr = _rtw_memcmp(pnetwork->network.MacAddress, cur_network->network.MacAddress, ETH_ALEN);

	pnetwork->network.Length = get_WLAN_BSSID_EX_sz(&pnetwork->network);
	if(pnetwork->network.Length > sizeof(WLAN_BSSID_EX))
	{
		RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("\n\n ***joinbss_evt_callback return a wrong bss ***\n\n"));
		goto ignore_joinbss_callback;
	}
		
	_enter_critical_bh(&pmlmepriv->lock, &irqL);
	
	RT_TRACE(_module_rtl871x_mlme_c_,_drv_info_,("\n rtw_joinbss_event_callback !! _enter_critical \n"));

	if(pnetwork->join_res > 0)
	{
		retry = 0;
		if (check_fwstate(pmlmepriv,_FW_UNDER_LINKING) )
		{
			//s1. find ptarget_wlan
			if(check_fwstate(pmlmepriv, _FW_LINKED) )
			{
				if(the_same_macaddr == _TRUE)
				{
					ptarget_wlan = rtw_find_network(&pmlmepriv->scanned_queue, cur_network->network.MacAddress);					
				}
				else
				{
					pcur_wlan = rtw_find_network(&pmlmepriv->scanned_queue, cur_network->network.MacAddress);
					if(pcur_wlan)	pcur_wlan->fixed = _FALSE;

					pcur_sta = rtw_get_stainfo(pstapriv, cur_network->network.MacAddress);
					if(pcur_sta){
						_enter_critical_bh(&(pstapriv->sta_hash_lock), &irqL2);
						rtw_free_stainfo(adapter,  pcur_sta);
						_exit_critical_bh(&(pstapriv->sta_hash_lock), &irqL2);
					}

					ptarget_wlan = rtw_find_network(&pmlmepriv->scanned_queue, pnetwork->network.MacAddress);
					if(check_fwstate(pmlmepriv, WIFI_STATION_STATE) == _TRUE){
						if(ptarget_wlan)	ptarget_wlan->fixed = _TRUE;			
					}
				}

			}
			else
			{
				ptarget_wlan = rtw_find_network(&pmlmepriv->scanned_queue, pnetwork->network.MacAddress);
				if(check_fwstate(pmlmepriv, WIFI_STATION_STATE) == _TRUE){
					if(ptarget_wlan)	ptarget_wlan->fixed = _TRUE;			
				}
			}
		
			if(ptarget_wlan == NULL)
			{			
				RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("Can't find ptarget_wlan when joinbss_event callback\n"));
				goto ignore_joinbss_callback;
			}
					
			//s2. find ptarget_sta & update ptarget_sta
			if(check_fwstate(pmlmepriv, WIFI_STATION_STATE) == _TRUE)
			{ 
				ptarget_sta = rtw_get_stainfo(pstapriv, pnetwork->network.MacAddress);
				if(ptarget_sta==NULL) {
					ptarget_sta = rtw_alloc_stainfo(pstapriv, pnetwork->network.MacAddress);
				}

				if(ptarget_sta) //update ptarget_sta
				{
					ptarget_sta->aid  = pnetwork->join_res;					
					ptarget_sta->qos_option = 1;//? 
					ptarget_sta->mac_id=0;

					if(adapter->securitypriv.dot11AuthAlgrthm== dot11AuthAlgrthm_8021X)
					{						
						adapter->securitypriv.binstallGrpkey=_FALSE;
						adapter->securitypriv.busetkipkey=_FALSE;						
						adapter->securitypriv.bgrpkey_handshake=_FALSE;

						ptarget_sta->ieee8021x_blocked=_TRUE;
						ptarget_sta->dot118021XPrivacy=adapter->securitypriv.dot11PrivacyAlgrthm;
						
						_rtw_memset((u8 *)&ptarget_sta->dot118021x_UncstKey, 0, sizeof (union Keytype));
						
						_rtw_memset((u8 *)&ptarget_sta->dot11tkiprxmickey, 0, sizeof (union Keytype));
						_rtw_memset((u8 *)&ptarget_sta->dot11tkiptxmickey, 0, sizeof (union Keytype));
						
						_rtw_memset((u8 *)&ptarget_sta->dot11txpn, 0, sizeof (union pn48));	
						_rtw_memset((u8 *)&ptarget_sta->dot11rxpn, 0, sizeof (union pn48));	
					}		
					
				}
				else
				{
					RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("Can't allocate stainfo when joinbss_event callback\n"));
					goto ignore_joinbss_callback;
				}
				
			}
		
			//s3. update cur_network & indicate connect
			if(ptarget_wlan)
			{			

				RT_TRACE(_module_rtl871x_mlme_c_,_drv_info_,("\nfw_state:%x, BSSID:"MAC_FMT"\n"
					,get_fwstate(pmlmepriv), MAC_ARG(pnetwork->network.MacAddress)));

			
				// why not use ptarget_wlan??
				_rtw_memcpy(&cur_network->network, &pnetwork->network, pnetwork->network.Length);
				cur_network->aid = pnetwork->join_res;
				
				#ifdef CONFIG_NEW_SIGNAL_STAT_PROCESS
				rtw_set_signal_stat_timer(&adapter->recvpriv);
				#endif
				adapter->recvpriv.signal_strength = ptarget_wlan->network.PhyInfo.SignalStrength;
				adapter->recvpriv.signal_qual = ptarget_wlan->network.PhyInfo.SignalQuality;
				//the ptarget_wlan->network.Rssi is raw data, we use ptarget_wlan->network.PhyInfo.SignalStrength instead (has scaled)
				adapter->recvpriv.rssi = translate_percentage_to_dbm(ptarget_wlan->network.PhyInfo.SignalStrength);
				#ifdef CONFIG_NEW_SIGNAL_STAT_PROCESS
				rtw_set_signal_stat_timer(&adapter->recvpriv);
				#endif
				
				//update fw_state //will clr _FW_UNDER_LINKING here indirectly
				switch(pnetwork->network.InfrastructureMode)
				{
					case Ndis802_11Infrastructure:						
							pmlmepriv->fw_state = WIFI_STATION_STATE;
							break;
					case Ndis802_11IBSS:		
							pmlmepriv->fw_state = WIFI_ADHOC_STATE;
							break;
					default:
							pmlmepriv->fw_state = WIFI_NULL_STATE;
							RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("Invalid network_mode\n"));
							break;
				}

				rtw_update_protection(adapter, (cur_network->network.IEs) + sizeof (NDIS_802_11_FIXED_IEs), 
									(cur_network->network.IELength));
			
#ifdef CONFIG_80211N_HT			
				//TODO: update HT_Capability
				rtw_update_ht_cap(adapter, cur_network->network.IEs, cur_network->network.IELength);
#endif

				//indicate connect
				if(check_fwstate(pmlmepriv, WIFI_STATION_STATE) == _TRUE)
				{
					struct xmit_priv *pxmitpriv = &adapter->xmitpriv;
					//Set Value to 0 for prevent xmit data frame without hw setting done.
					ATOMIC_SET(&pxmitpriv->HwRdyXmitData, 0);
					rtw_indicate_connect(adapter);
				}
				else
				{
					//adhoc mode will rtw_indicate_connect when rtw_stassoc_event_callback
					RT_TRACE(_module_rtl871x_mlme_c_,_drv_info_,("adhoc mode, fw_state:%x", get_fwstate(pmlmepriv)));
				}

				
			}
		
			RT_TRACE(_module_rtl871x_mlme_c_,_drv_info_,("Cancle assoc_timer \n"));		
			_cancel_timer(&pmlmepriv->assoc_timer, &timer_cancelled);
	

		}
		else
		{
			RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("rtw_joinbss_event_callback err: fw_state:%x", get_fwstate(pmlmepriv)));	
			goto ignore_joinbss_callback;
		}
				
	}
	else if(pnetwork->join_res == -4) 
	{
		rtw_reset_securitypriv(adapter);
		_set_timer(&pmlmepriv->assoc_timer, 1);					

		//rtw_free_assoc_resources(adapter);

		if((check_fwstate(pmlmepriv, _FW_UNDER_LINKING)) == _TRUE)
		{		
			RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("fail! clear _FW_UNDER_LINKING ^^^fw_state=%x\n", get_fwstate(pmlmepriv)));
			_clr_fwstate_(pmlmepriv, _FW_UNDER_LINKING);
		}	
		
	}
	else //if join_res < 0 (join fails), then try again
	{
		#ifdef REJOIN
		res = rtw_select_and_join_from_scanned_queue(pmlmepriv);	
		RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("rtw_select_and_join_from_scanned_queue again! res:%d\n",res));
		if (res != _SUCCESS || retry>2)
		{
			RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("Set Assoc_Timer = 1; can't find match ssid in scanned_q \n"));
		#endif
	
			_set_timer(&pmlmepriv->assoc_timer, 1);					

			//rtw_free_assoc_resources(adapter);

			if((check_fwstate(pmlmepriv, _FW_UNDER_LINKING)) == _TRUE)
			{		
				RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("fail! clear _FW_UNDER_LINKING ^^^fw_state=%x\n", get_fwstate(pmlmepriv)));
				_clr_fwstate_(pmlmepriv, _FW_UNDER_LINKING);
			}						
		
		#ifdef REJOIN
			retry = 0;
			
		}
		else
		{
			//extend time of assoc_timer
			_set_timer(&pmlmepriv->assoc_timer, MAX_JOIN_TIMEOUT);

			retry++;
		}
		#endif
	}

ignore_joinbss_callback:

	_exit_critical_bh(&pmlmepriv->lock, &irqL);
	_func_exit_;	
}

void rtw_joinbss_event_callback(_adapter *adapter, u8 *pbuf)
{
	struct wlan_network 	*pnetwork	= (struct wlan_network *)pbuf;
	struct xmit_priv *pxmitpriv = &adapter->xmitpriv;

_func_enter_;

	mlmeext_joinbss_event_callback(adapter, pnetwork->join_res);

	//Set Value to 1 to xmit data frame.
	ATOMIC_SET(&pxmitpriv->HwRdyXmitData, 1);
	rtw_os_xmit_schedule(adapter);

_func_exit_;
}

#else //CONFIG_HANDLE_JOINBSS_ON_ASSOC_RSP
void rtw_joinbss_event_callback(_adapter *adapter, u8 *pbuf)
{
	_irqL irqL,irqL2;
	int	res;
	static u8 retry=0;
	u8 timer_cancelled;
	struct sta_info *ptarget_sta= NULL, *pcur_sta = NULL;
   	struct	sta_priv *pstapriv = &adapter->stapriv;
	struct	mlme_priv	*pmlmepriv = &(adapter->mlmepriv);
	struct wlan_network 	*pnetwork	= (struct wlan_network *)pbuf;
	struct wlan_network 	*cur_network = &(pmlmepriv->cur_network);
	struct wlan_network	*pcur_wlan = NULL, *ptarget_wlan = NULL;
	unsigned int 		the_same_macaddr = _FALSE;	

_func_enter_;	

#ifdef CONFIG_RTL8712
       //endian_convert
	pnetwork->join_res = le32_to_cpu(pnetwork->join_res);
	pnetwork->network_type = le32_to_cpu(pnetwork->network_type);
	pnetwork->network.Length = le32_to_cpu(pnetwork->network.Length);
	pnetwork->network.Ssid.SsidLength = le32_to_cpu(pnetwork->network.Ssid.SsidLength);
	pnetwork->network.Privacy =le32_to_cpu( pnetwork->network.Privacy);
	pnetwork->network.Rssi = le32_to_cpu(pnetwork->network.Rssi);
	pnetwork->network.NetworkTypeInUse =le32_to_cpu(pnetwork->network.NetworkTypeInUse) ;	
	pnetwork->network.Configuration.ATIMWindow = le32_to_cpu(pnetwork->network.Configuration.ATIMWindow);
	pnetwork->network.Configuration.BeaconPeriod = le32_to_cpu(pnetwork->network.Configuration.BeaconPeriod);
	pnetwork->network.Configuration.DSConfig = le32_to_cpu(pnetwork->network.Configuration.DSConfig);
	pnetwork->network.Configuration.FHConfig.DwellTime=le32_to_cpu(pnetwork->network.Configuration.FHConfig.DwellTime);
	pnetwork->network.Configuration.FHConfig.HopPattern=le32_to_cpu(pnetwork->network.Configuration.FHConfig.HopPattern);
	pnetwork->network.Configuration.FHConfig.HopSet=le32_to_cpu(pnetwork->network.Configuration.FHConfig.HopSet);
	pnetwork->network.Configuration.FHConfig.Length=le32_to_cpu(pnetwork->network.Configuration.FHConfig.Length);	
	pnetwork->network.Configuration.Length = le32_to_cpu(pnetwork->network.Configuration.Length);
	pnetwork->network.InfrastructureMode = le32_to_cpu(pnetwork->network.InfrastructureMode);
	pnetwork->network.IELength = le32_to_cpu(pnetwork->network.IELength );
#endif

	RT_TRACE(_module_rtl871x_mlme_c_,_drv_info_,("joinbss event call back received with res=%d\n", pnetwork->join_res));

	rtw_get_encrypt_decrypt_from_registrypriv(adapter);
	

	if (pmlmepriv->assoc_ssid.SsidLength == 0)
	{
		RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("@@@@@   joinbss event call back  for Any SSid\n"));		
	}
	else
	{
		RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("@@@@@   rtw_joinbss_event_callback for SSid:%s\n", pmlmepriv->assoc_ssid.Ssid));
	}
	
	the_same_macaddr = _rtw_memcmp(pnetwork->network.MacAddress, cur_network->network.MacAddress, ETH_ALEN);

	pnetwork->network.Length = get_WLAN_BSSID_EX_sz(&pnetwork->network);
	if(pnetwork->network.Length > sizeof(WLAN_BSSID_EX))
	{
		RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("\n\n ***joinbss_evt_callback return a wrong bss ***\n\n"));
		goto ignore_joinbss_callback;
	}
		
	_enter_critical_bh(&pmlmepriv->lock, &irqL);
	
	RT_TRACE(_module_rtl871x_mlme_c_,_drv_info_,("\n rtw_joinbss_event_callback !! _enter_critical \n"));

	if(pnetwork->join_res > 0)
	{
		retry = 0;
		if (check_fwstate(pmlmepriv, _FW_UNDER_LINKING) )
		{
			//s1. find ptarget_wlan
			if(check_fwstate(pmlmepriv, _FW_LINKED) )
			{
				if(the_same_macaddr == _TRUE)
				{
					ptarget_wlan = rtw_find_network(&pmlmepriv->scanned_queue, cur_network->network.MacAddress);					
				}
				else
				{
					pcur_wlan = rtw_find_network(&pmlmepriv->scanned_queue, cur_network->network.MacAddress);
					if(pcur_wlan)	pcur_wlan->fixed = _FALSE;

					pcur_sta = rtw_get_stainfo(pstapriv, cur_network->network.MacAddress);
					if(pcur_sta){
						_enter_critical_bh(&(pstapriv->sta_hash_lock), &irqL2);
						rtw_free_stainfo(adapter,  pcur_sta);
						_exit_critical_bh(&(pstapriv->sta_hash_lock), &irqL2);
					}

					ptarget_wlan = rtw_find_network(&pmlmepriv->scanned_queue, pnetwork->network.MacAddress);
					if(check_fwstate(pmlmepriv, WIFI_STATION_STATE) == _TRUE){
						if(ptarget_wlan)	ptarget_wlan->fixed = _TRUE;			
					}
				}	
			}
			else
			{
				ptarget_wlan = rtw_find_network(&pmlmepriv->scanned_queue, pnetwork->network.MacAddress);
				if(check_fwstate(pmlmepriv, WIFI_STATION_STATE) == _TRUE){
					if(ptarget_wlan)	ptarget_wlan->fixed = _TRUE;			
				}
			}
		
			if(ptarget_wlan == NULL)
			{			
				RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("Can't find ptarget_wlan when joinbss_event callback\n"));
				goto ignore_joinbss_callback;
			}
					
			//s2. find ptarget_sta & update ptarget_sta
			if(check_fwstate(pmlmepriv, WIFI_STATION_STATE) == _TRUE)
			{
				ptarget_sta = rtw_get_stainfo(pstapriv, pnetwork->network.MacAddress);
				if(ptarget_sta==NULL)
				{
					//DBG_8192C("==> %s #1, call rtw_alloc_stainfo\n",__FUNCTION__);
					ptarget_sta = rtw_alloc_stainfo(pstapriv, pnetwork->network.MacAddress);
				}

				if(ptarget_sta) //update ptarget_sta
				{
					ptarget_sta->aid  = pnetwork->join_res;					
					ptarget_sta->qos_option = 1;//? 
					ptarget_sta->mac_id=0;

					if(adapter->securitypriv.dot11AuthAlgrthm== dot11AuthAlgrthm_8021X)
					{						
						adapter->securitypriv.binstallGrpkey=_FALSE;
						adapter->securitypriv.busetkipkey=_FALSE;						
						adapter->securitypriv.bgrpkey_handshake=_FALSE;

						ptarget_sta->ieee8021x_blocked=_TRUE;
						ptarget_sta->dot118021XPrivacy=adapter->securitypriv.dot11PrivacyAlgrthm;
						
						_rtw_memset((u8 *)&ptarget_sta->dot118021x_UncstKey, 0, sizeof (union Keytype));
						
						_rtw_memset((u8 *)&ptarget_sta->dot11tkiprxmickey, 0, sizeof (union Keytype));
						_rtw_memset((u8 *)&ptarget_sta->dot11tkiptxmickey, 0, sizeof (union Keytype));
						
						_rtw_memset((u8 *)&ptarget_sta->dot11txpn, 0, sizeof (union pn48));	
						_rtw_memset((u8 *)&ptarget_sta->dot11rxpn, 0, sizeof (union pn48));	
					}		
					
				}
				else
				{
					RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("Can't allocate stainfo when joinbss_event callback\n"));
					goto ignore_joinbss_callback;
				}
				
			}
		
			//s3. update cur_network & indicate connect
			if(ptarget_wlan)
			{			

				RT_TRACE(_module_rtl871x_mlme_c_,_drv_info_,("\nfw_state:%x, BSSID:%x:%x:%x:%x:%x:%x\n",get_fwstate(pmlmepriv), 
						pnetwork->network.MacAddress[0], pnetwork->network.MacAddress[1],
						pnetwork->network.MacAddress[2], pnetwork->network.MacAddress[3],
						pnetwork->network.MacAddress[4], pnetwork->network.MacAddress[5]));

			
				_rtw_memcpy(&cur_network->network, &pnetwork->network, pnetwork->network.Length);
				cur_network->aid = pnetwork->join_res;
				
				#ifdef CONFIG_NEW_SIGNAL_STAT_PROCESS
				rtw_set_signal_stat_timer(&adapter->recvpriv);
				#endif
				adapter->recvpriv.signal_strength = ptarget_wlan->network.PhyInfo.SignalStrength;
				adapter->recvpriv.signal_qual = ptarget_wlan->network.PhyInfo.SignalQuality;
				//the ptarget_wlan->network.Rssi is raw data, we use ptarget_wlan->network.PhyInfo.SignalStrength instead (has scaled)
				adapter->recvpriv.rssi = translate_percentage_to_dbm(ptarget_wlan->network.PhyInfo.SignalStrength);
				#ifdef CONFIG_NEW_SIGNAL_STAT_PROCESS
				rtw_set_signal_stat_timer(&adapter->recvpriv);
				#endif
				
				//update fw_state //will clr _FW_UNDER_LINKING here indirectly
				switch(pnetwork->network.InfrastructureMode)
				{
					case Ndis802_11Infrastructure:						
							pmlmepriv->fw_state = WIFI_STATION_STATE;
							break;
					case Ndis802_11IBSS:		
							pmlmepriv->fw_state = WIFI_ADHOC_STATE;
							break;
					default:
							pmlmepriv->fw_state = WIFI_NULL_STATE;
							RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("Invalid network_mode\n"));
							break;
				}

				rtw_update_protection(adapter, (cur_network->network.IEs) + sizeof (NDIS_802_11_FIXED_IEs), 
									(cur_network->network.IELength));
			
#ifdef CONFIG_80211N_HT			
				//TODO: update HT_Capability
				rtw_update_ht_cap(adapter, cur_network->network.IEs, cur_network->network.IELength);
#endif

				//indicate connect
				if(check_fwstate(pmlmepriv, WIFI_STATION_STATE) == _TRUE)
				{
					rtw_indicate_connect(adapter);		
				}
				else
				{
					//adhoc mode will rtw_indicate_connect when rtw_stassoc_event_callback
					RT_TRACE(_module_rtl871x_mlme_c_,_drv_info_,("adhoc mode, fw_state:%x", get_fwstate(pmlmepriv)));
				}

				
			}
		
			RT_TRACE(_module_rtl871x_mlme_c_,_drv_info_,("Cancle assoc_timer \n"));		
			_cancel_timer(&pmlmepriv->assoc_timer, &timer_cancelled);
	

		}
		else
		{
			RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("rtw_joinbss_event_callback err: fw_state:%x", get_fwstate(pmlmepriv)));	
			goto ignore_joinbss_callback;
		}
				
	}
	else if(pnetwork->join_res == -4) 
	{
		rtw_reset_securitypriv(adapter);
		_set_timer(&pmlmepriv->assoc_timer, 1);					

		//rtw_free_assoc_resources(adapter);

		if((check_fwstate(pmlmepriv, _FW_UNDER_LINKING)) == _TRUE)
		{		
			RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("fail! clear _FW_UNDER_LINKING ^^^fw_state=%x\n", get_fwstate(pmlmepriv)));
			_clr_fwstate_(pmlmepriv, _FW_UNDER_LINKING);
		}	
		
	}
	else //if join_res < 0 (join fails), then try again
	{
		#ifdef REJOIN
		res = rtw_select_and_join_from_scanned_queue(pmlmepriv);	
		RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("rtw_select_and_join_from_scanned_queue again! res:%d\n",res));
		if (res != _SUCCESS || retry>2)
		{
			RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("Set Assoc_Timer = 1; can't find match ssid in scanned_q \n"));
			
		#endif
			_set_timer(&pmlmepriv->assoc_timer, 1);					

			//rtw_free_assoc_resources(adapter);

			if((check_fwstate(pmlmepriv, _FW_UNDER_LINKING)) == _TRUE)
			{		
				RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("fail! clear _FW_UNDER_LINKING ^^^fw_state=%x\n", get_fwstate(pmlmepriv)));
				_clr_fwstate_(pmlmepriv, _FW_UNDER_LINKING);
			}						

		#ifdef REJOIN
			retry = 0;
			
		}
		else
		{
			//extend time of assoc_timer
			_set_timer(&pmlmepriv->assoc_timer, MAX_JOIN_TIMEOUT);

			retry++;
		}
		#endif
		
	}

ignore_joinbss_callback:

	_exit_critical_bh(&pmlmepriv->lock, &irqL);

	mlmeext_joinbss_event_callback(adapter, pnetwork->join_res);

_func_exit_;	

}
#endif //CONFIG_HANDLE_JOINBSS_ON_ASSOC_RSP

void rtw_stassoc_event_callback(_adapter *adapter, u8 *pbuf)
{
	_irqL irqL;	
	struct sta_info *psta;
	struct mlme_priv *pmlmepriv = &(adapter->mlmepriv);
	struct stassoc_event	*pstassoc	= (struct stassoc_event*)pbuf;
	struct wlan_network 	*cur_network = &(pmlmepriv->cur_network);
	struct wlan_network	*ptarget_wlan = NULL;

_func_enter_;	
	
	// to do: 
	if(rtw_access_ctrl(&adapter->acl_list, pstassoc->macaddr) == _FALSE)
		return;

#if defined (CONFIG_AP_MODE) && defined (CONFIG_NATIVEAP_MLME)
	if(check_fwstate(pmlmepriv, WIFI_AP_STATE))
	{
		psta = rtw_get_stainfo(&adapter->stapriv, pstassoc->macaddr);	
		if(psta)
		{
			bss_cap_update(adapter, psta);
		
			sta_info_update(adapter, psta);
		}	
		
		goto exit;
	}	
#endif	

	psta = rtw_get_stainfo(&adapter->stapriv, pstassoc->macaddr);	
	if( psta != NULL)
	{
		//the sta have been in sta_info_queue => do nothing 
		
		RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("Error: rtw_stassoc_event_callback: sta has been in sta_hash_queue \n"));
		
		goto exit; //(between drv has received this event before and  fw have not yet to set key to CAM_ENTRY)
	}

	psta = rtw_alloc_stainfo(&adapter->stapriv, pstassoc->macaddr);	
	if (psta == NULL) {
		RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("Can't alloc sta_info when rtw_stassoc_event_callback\n"));
		goto exit;
	}	
	
	//to do : init sta_info variable
	psta->qos_option = 0;	
	psta->mac_id = le32_to_cpu((uint)pstassoc->cam_id);
	//psta->aid = (uint)pstassoc->cam_id;
	
	if(adapter->securitypriv.dot11AuthAlgrthm==dot11AuthAlgrthm_8021X)
		psta->dot118021XPrivacy = adapter->securitypriv.dot11PrivacyAlgrthm;

	psta->ieee8021x_blocked = _FALSE;		
	
	_enter_critical_bh(&pmlmepriv->lock, &irqL);

	if ( (check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE)==_TRUE ) || 
		(check_fwstate(pmlmepriv, WIFI_ADHOC_STATE)==_TRUE ) )
	{
		if(adapter->stapriv.asoc_sta_count== 2)
		{
			ptarget_wlan = rtw_find_network(&pmlmepriv->scanned_queue, cur_network->network.MacAddress);
			if(ptarget_wlan)	ptarget_wlan->fixed = _TRUE;
		
			// a sta + bc/mc_stainfo (not Ibss_stainfo)
			rtw_indicate_connect(adapter);
		}
	}

	_exit_critical_bh(&pmlmepriv->lock, &irqL);


	mlmeext_sta_add_event_callback(adapter, psta);
	
#ifdef CONFIG_RTL8711
	//submit SetStaKey_cmd to tell fw, fw will allocate an CAM entry for this sta	
	rtw_setstakey_cmd(adapter, (unsigned char*)psta, _FALSE);
#endif
		
exit:
	
_func_exit_;	

}

void rtw_stadel_event_callback(_adapter *adapter, u8 *pbuf)
{
	_irqL irqL,irqL2;	
	struct sta_info *psta;
	struct wlan_network* pwlan = NULL;
	WLAN_BSSID_EX    *pdev_network=NULL;
	u8* pibss = NULL;
	struct	mlme_priv	*pmlmepriv = &(adapter->mlmepriv);
	struct 	stadel_event *pstadel	= (struct stadel_event*)pbuf;
   	struct	sta_priv *pstapriv = &adapter->stapriv;
	struct wlan_network *tgt_network = &(pmlmepriv->cur_network);
	
_func_enter_;	

	mlmeext_sta_del_event_callback(adapter);

        if(check_fwstate(pmlmepriv, WIFI_AP_STATE))
		return;

	_enter_critical_bh(&pmlmepriv->lock, &irqL2);

	if(check_fwstate(pmlmepriv, WIFI_STATION_STATE) )
	{

		#ifdef CONFIG_LAYER2_ROAMING
		if(pmlmepriv->to_roaming > 0)
			pmlmepriv->to_roaming--; // this stadel_event is caused by roaming, decrease to_roaming
		else if(pmlmepriv->to_roaming ==0)
			pmlmepriv->to_roaming= adapter->registrypriv.max_roaming_times;

		if(*((unsigned short *)(pstadel->rsvd)) !=65535 ) //if stadel_event isn't caused by no rx
			pmlmepriv->to_roaming=0; // don't roam
		#endif //CONFIG_LAYER2_ROAMING


		rtw_free_assoc_resources(adapter);
		rtw_indicate_disconnect(adapter);

		// remove the network entry in scanned_queue
		pwlan = rtw_find_network(&pmlmepriv->scanned_queue, tgt_network->network.MacAddress);	
		if (pwlan) {			
			pwlan->fixed = _FALSE;
			rtw_free_network_nolock(pmlmepriv, pwlan);
		}

		#ifdef CONFIG_LAYER2_ROAMING
		_rtw_roaming(adapter, tgt_network);
		#endif //CONFIG_LAYER2_ROAMING
		
	}

	if ( check_fwstate(pmlmepriv,WIFI_ADHOC_MASTER_STATE) || 
	      check_fwstate(pmlmepriv,WIFI_ADHOC_STATE))
	{
		psta = rtw_get_stainfo(&adapter->stapriv, pstadel->macaddr);
		
		_enter_critical_bh(&(pstapriv->sta_hash_lock), &irqL);
		rtw_free_stainfo(adapter,  psta);
		_exit_critical_bh(&(pstapriv->sta_hash_lock), &irqL);
		
		if(adapter->stapriv.asoc_sta_count== 1) //a sta + bc/mc_stainfo (not Ibss_stainfo)
		{ 
			//rtw_indicate_disconnect(adapter);//removed@20091105
			
			//free old ibss network
			//pwlan = rtw_find_network(&pmlmepriv->scanned_queue, pstadel->macaddr);
			pwlan = rtw_find_network(&pmlmepriv->scanned_queue, tgt_network->network.MacAddress);
			if(pwlan)	
			{
				pwlan->fixed = _FALSE;
				rtw_free_network_nolock(pmlmepriv, pwlan); 
			}

			//re-create ibss
			pdev_network = &(adapter->registrypriv.dev_network);			
			pibss = adapter->registrypriv.dev_network.MacAddress;

			_rtw_memcpy(pdev_network, &tgt_network->network, get_WLAN_BSSID_EX_sz(&tgt_network->network));
			
			_rtw_memset(&pdev_network->Ssid, 0, sizeof(NDIS_802_11_SSID));
			_rtw_memcpy(&pdev_network->Ssid, &pmlmepriv->assoc_ssid, sizeof(NDIS_802_11_SSID));
	
			rtw_update_registrypriv_dev_network(adapter);			

			rtw_generate_random_ibss(pibss);
			
			if(check_fwstate(pmlmepriv,WIFI_ADHOC_STATE))
			{
				set_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE);
				_clr_fwstate_(pmlmepriv, WIFI_ADHOC_STATE);
			}

			if(rtw_createbss_cmd(adapter)!=_SUCCESS)
			{

				RT_TRACE(_module_rtl871x_ioctl_set_c_,_drv_err_,("***Error=>stadel_event_callback: rtw_createbss_cmd status FAIL*** \n "));										

			}

			
		}
		
	}
	
	_exit_critical_bh(&pmlmepriv->lock, &irqL2);
	
_func_exit_;	

}


void rtw_cpwm_event_callback(_adapter *adapter, u8 *pbuf)
{
	struct reportpwrstate_parm *preportpwrstate = (struct reportpwrstate_parm *)pbuf;

_func_enter_;

	RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("rtw_cpwm_event_callback !!!\n"));
#ifdef CONFIG_PWRCTRL
	preportpwrstate->state |= (u8)(adapter->pwrctrlpriv.cpwm_tog + 0x80);
	cpwm_int_hdl(adapter, preportpwrstate);
#endif

_func_exit_;

}

void _rtw_join_timeout_handler (_adapter *adapter)
{
	_irqL irqL;
	struct	mlme_priv *pmlmepriv = &adapter->mlmepriv;
	int do_join_r;

#if 0
	if (adapter->bDriverStopped == _TRUE){
		_rtw_up_sema(&pmlmepriv->assoc_terminate);
		return;
	}
#endif	

_func_enter_;		

	RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("\n^^^rtw_join_timeout_handler ^^^fw_state=%x\n", get_fwstate(pmlmepriv)));
	RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("+rtw_join_timeout_handler, fw_state=%x\n", get_fwstate(pmlmepriv)));	
	
	if(adapter->bDriverStopped ||adapter->bSurpriseRemoved)
		return;

	
	_enter_critical_bh(&pmlmepriv->lock, &irqL);

	#ifdef CONFIG_LAYER2_ROAMING
	if(pmlmepriv->to_roaming>0) { // join timeout caused by roaming
		while(1) {
			pmlmepriv->to_roaming--;
			if(pmlmepriv->to_roaming!=0) { //try another ,
				DBG_871X("%s try another roaming\n", __FUNCTION__);
				if( _SUCCESS!=(do_join_r=rtw_do_join(adapter)) ) {
					DBG_871X("%s roaming do_join return %d\n", __FUNCTION__ ,do_join_r);
					continue;
				}
				break;
			} else {
				DBG_871X("%s We've try roaming but fail\n", __FUNCTION__);
				_clr_fwstate_(pmlmepriv, _FW_UNDER_LINKING);
				rtw_indicate_disconnect(adapter);
				break;
			}
		}
		
	} else 
	#endif
	{
		if((check_fwstate(pmlmepriv, _FW_UNDER_LINKING)) == _TRUE)
		{
			RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("clear _FW_UNDER_LINKING ^^^fw_state=%x\n", get_fwstate(pmlmepriv)));
			_clr_fwstate_(pmlmepriv, _FW_UNDER_LINKING);              
		}

		//Jeff: Even if not linked, we still need to reset securitypriv
		//if((check_fwstate(pmlmepriv, _FW_LINKED)) == _TRUE)
		{
			rtw_os_indicate_disconnect(adapter);
			_clr_fwstate_(pmlmepriv, _FW_LINKED);
		}

		free_scanqueue(pmlmepriv);//???
		rtw_led_control(adapter, LED_CTL_NO_LINK);
 	}

	_exit_critical_bh(&pmlmepriv->lock, &irqL);
	

#ifdef CONFIG_DRVEXT_MODULE_WSC	
	drvext_assoc_fail_indicate(&adapter->drvextpriv);	
#endif	
	
_func_exit_;

}

void rtw_scan_timeout_handler (_adapter *adapter)
{	
	_irqL irqL;
	struct	mlme_priv *pmlmepriv = &adapter->mlmepriv;
	
	RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_, ("\n^^^rtw_scan_timeout_handler ^^^fw_state=%x\n", get_fwstate(pmlmepriv)));

	_enter_critical_bh(&pmlmepriv->lock, &irqL);
	
	if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY) )
	{		
		_clr_fwstate_(pmlmepriv, _FW_UNDER_SURVEY);
	}
	else 
	{	
		RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_, ("fw status =%x, rtw_scan_timeout_handler: survey done event comes too late!\n", get_fwstate(pmlmepriv)));	
	}	

	_exit_critical_bh(&pmlmepriv->lock, &irqL);
	
}

static void rtw_auto_scan_handler(_adapter *padapter)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct pwrctrl_priv *pwrctrlpriv = &padapter->pwrctrlpriv;

	//auto site survey per 60sec
	if(pmlmepriv->scan_interval >0)
	{
		pmlmepriv->scan_interval--;
		if(pmlmepriv->scan_interval==0)
		{
			if( pwrctrlpriv->power_mgnt != PS_MODE_ACTIVE )
				return;			

/*		
			if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY|_FW_UNDER_LINKING) == _TRUE) 
			{
				DBG_8192C("exit %s when _FW_UNDER_SURVEY|_FW_UNDER_LINKING -> \n", __FUNCTION__);
				return;
			}
			
			if(pmlmepriv->sitesurveyctrl.traffic_busy == _TRUE)
			{
				DBG_8192C("%s exit cause traffic_busy(%x)\n",__FUNCTION__, pmlmepriv->sitesurveyctrl.traffic_busy);
				return;
			}
*/

			DBG_871X("%s\n", __FUNCTION__);

			rtw_set_802_11_bssid_list_scan(padapter);			
			
			pmlmepriv->scan_interval = SCAN_INTERVAL;// 30*2 sec = 60sec
			
		}
		
	}

}

void rtw_dynamic_check_timer_handlder(_adapter *adapter)
{
#ifdef CONFIG_AP_MODE
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
#endif //CONFIG_AP_MODE
	struct registry_priv *pregistrypriv = &adapter->registrypriv;

	if(adapter->hw_init_completed == _FALSE)
		return;

	if ((adapter->bDriverStopped == _TRUE)||(adapter->bSurpriseRemoved== _TRUE))
		return;

	if(adapter->net_closed == _TRUE)
		return;

	rtw_dynamic_chk_wk_cmd(adapter);

	if(pregistrypriv->wifi_spec==1)
	{	
		//auto site survey
		rtw_auto_scan_handler(adapter);
	}

#ifdef CONFIG_AP_MODE
	if(check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE)
	{
		expire_timeout_chk(adapter);
	}	
#endif
	
}


#ifdef CONFIG_SET_SCAN_DENY_TIMER
void rtw_set_scan_deny_timer_hdl(_adapter *adapter)
{
	struct mlme_priv *mlmepriv = &adapter->mlmepriv;

	//allowed set scan
	ATOMIC_SET(&mlmepriv->set_scan_deny, 0);
}

void rtw_set_scan_deny(struct mlme_priv *mlmepriv, u32 ms)
{
	ATOMIC_SET(&mlmepriv->set_scan_deny, 1);
	_set_timer(&mlmepriv->set_scan_deny_timer, ms);
}
#endif


/*
* Select a new join candidate from the original @param candidate and @param competitor
* @return _TRUE: candidate is updated
* @return _FALSE: candidate is not updated
*/
static int rtw_check_join_candidate(struct mlme_priv *pmlmepriv
	, struct wlan_network **candidate, struct wlan_network *competitor)
{
	int updated = _FALSE;
	_adapter *adapter = container_of(pmlmepriv, _adapter, mlmepriv);

	if(pmlmepriv->assoc_by_bssid==_TRUE) { // associate with bssid
		if(	(*candidate == NULL ||(*candidate)->network.Rssi<competitor->network.Rssi )
			&& _rtw_memcmp(competitor->network.MacAddress, pmlmepriv->assoc_bssid, ETH_ALEN)==_TRUE
		) {
			*candidate = competitor;
			updated = _TRUE;
		}
	} else  if (pmlmepriv->assoc_ssid.SsidLength == 0 ) { // associate with ssid, but ssidlength is 0
		if(	(*candidate == NULL ||(*candidate)->network.Rssi<competitor->network.Rssi ) ) {
			*candidate = competitor;
			updated = _TRUE;
		}
	} else
#ifdef  CONFIG_LAYER2_ROAMING
	if(pmlmepriv->to_roaming) { // roaming
		if(	(*candidate == NULL ||(*candidate)->network.Rssi<competitor->network.Rssi )
			&& is_same_ess(&competitor->network, &pmlmepriv->cur_network.network) 
			//&&(!is_same_network(&competitor->network, &pmlmepriv->cur_network.network))
			&& rtw_get_passing_time_ms((u32)competitor->last_scanned) < 5000
			&& rtw_is_desired_network(adapter, competitor)
		) {
			*candidate = competitor;
			updated = _TRUE;
		}
		
	} else
#endif
	{ // associate with ssid
		if(	(*candidate == NULL ||(*candidate)->network.Rssi<competitor->network.Rssi )
			&& (competitor->network.Ssid.SsidLength==pmlmepriv->assoc_ssid.SsidLength)
			&&((_rtw_memcmp(competitor->network.Ssid.Ssid, pmlmepriv->assoc_ssid.Ssid, pmlmepriv->assoc_ssid.SsidLength)) == _TRUE)
			&& rtw_is_desired_network(adapter, competitor)
		) {
			*candidate = competitor;
			updated = _TRUE;
		}
	}

	if(updated){
		DBG_871X("new candidate: %s("MAC_FMT") rssi:%d\n",
			(*candidate)->network.Ssid.Ssid,
			MAC_ARG((*candidate)->network.MacAddress),
			(int)(*candidate)->network.Rssi
		);
	}
	
	return updated;
}


/*
Calling context:
The caller of the sub-routine will be in critical section...

The caller must hold the following spinlock

pmlmepriv->lock


*/
#if 1
int rtw_select_and_join_from_scanned_queue(struct mlme_priv *pmlmepriv )
{
	int ret;
	_list	*phead;
	_adapter *adapter;	
	_queue	*queue	= &(pmlmepriv->scanned_queue);
	struct	wlan_network	*pnetwork = NULL;
	struct	wlan_network	*candidate = NULL;
	u8 		bSupportAntDiv = _FALSE;
_func_enter_;

	phead = get_list_head(queue);		
	adapter = (_adapter *)pmlmepriv->nic_hdl;

	pmlmepriv->pscanned = get_next( phead );

	while (!rtw_end_of_queue_search(phead, pmlmepriv->pscanned)) {

		pnetwork = LIST_CONTAINOR(pmlmepriv->pscanned, struct wlan_network, list);
		if(pnetwork==NULL){
			RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("%s return _FAIL:(pnetwork==NULL)\n", __FUNCTION__));
			ret = _FAIL;
			goto exit;
		}
		
		pmlmepriv->pscanned = get_next(pmlmepriv->pscanned);

		#if 0
		DBG_871X("MacAddress:"MAC_FMT" ssid:%s\n", MAC_ARG(pnetwork->network.MacAddress), pnetwork->network.Ssid.Ssid);
		#endif

		rtw_check_join_candidate(pmlmepriv, &candidate, pnetwork);
 
 	}

	if(candidate == NULL) {
		DBG_871X("%s: return _FAIL(candidate == NULL)\n", __FUNCTION__);
		ret = _FAIL;
		goto exit;
	} else {
		DBG_871X("%s: candidate: %s("MAC_FMT")\n", __FUNCTION__,
			candidate->network.Ssid.Ssid, MAC_ARG(candidate->network.MacAddress));;
	}
	

	// check for situation of  _FW_LINKED 
	if (check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE)
	{
		DBG_871X("%s: _FW_LINKED while ask_for_joinbss!!!\n", __FUNCTION__);
	
		if(is_same_network(&pmlmepriv->cur_network.network, &candidate->network))
		{
			DBG_871X("%s: _FW_LINKED and is same network, it needn't join again\n", __FUNCTION__);

			rtw_indicate_connect(adapter);//rtw_indicate_connect again
				
			ret = 2;
			goto exit;
		}
		else
		{
			rtw_disassoc_cmd(adapter);
			rtw_indicate_disconnect(adapter);
			rtw_free_assoc_resources(adapter);
		}
	}
	
	#ifdef CONFIG_ANTENNA_DIVERSITY
	adapter->HalFunc.GetHalDefVarHandler(adapter, HAL_DEF_IS_SUPPORT_ANT_DIV, &(bSupportAntDiv));
	if(_TRUE == bSupportAntDiv)	
	{
		u8 CurrentAntenna;
		adapter->HalFunc.GetHalDefVarHandler(adapter, HAL_DEF_CURRENT_ANTENNA, &(CurrentAntenna));			
		DBG_8192C("#### Opt_Ant_(%s) , cur_Ant(%s)\n",
			(2==candidate->network.PhyInfo.Optimum_antenna)?"A":"B",
			(2==CurrentAntenna)?"A":"B"
		);
	}
	#endif

	ret = rtw_joinbss_cmd(adapter, candidate);
	
exit:
_func_exit_;

	return ret;

}
#else
int rtw_select_and_join_from_scanned_queue(struct mlme_priv *pmlmepriv )
{	
	_list	*phead;
	u8 CurrentAntenna;
	unsigned char *dst_ssid, *src_ssid;
	_adapter *adapter;	
	_queue	*queue	= &(pmlmepriv->scanned_queue);
	struct	wlan_network	*pnetwork = NULL;
	struct	wlan_network	*pnetwork_max_rssi = NULL;
	#ifdef CONFIG_LAYER2_ROAMING
	struct wlan_network * roaming_candidate=NULL;
	u32 cur_time=rtw_get_current_time();
	#endif

_func_enter_;

	phead = get_list_head(queue);		
	adapter = (_adapter *)pmlmepriv->nic_hdl;

	pmlmepriv->pscanned = get_next( phead );

	while (!rtw_end_of_queue_search(phead, pmlmepriv->pscanned)) {

		pnetwork = LIST_CONTAINOR(pmlmepriv->pscanned, struct wlan_network, list);
		if(pnetwork==NULL){
			RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("(2)rtw_select_and_join_from_scanned_queue return _FAIL:(pnetwork==NULL)\n"));
			return _FAIL;	
		}

		dst_ssid = pnetwork->network.Ssid.Ssid;
		src_ssid = pmlmepriv->assoc_ssid.Ssid;
		
		pmlmepriv->pscanned = get_next(pmlmepriv->pscanned);

		#if 0
		DBG_871X("MacAddress:"MAC_FMT" ssid:%s\n", MAC_ARG(pnetwork->network.MacAddress), pnetwork->network.Ssid.Ssid);
		#endif

		if(pmlmepriv->assoc_by_bssid==_TRUE)
		{
			if(_rtw_memcmp(pnetwork->network.MacAddress, pmlmepriv->assoc_bssid, ETH_ALEN)==_TRUE)
			{
				//remove the condition @ 20081125
				//if((pmlmepriv->cur_network.network.InfrastructureMode==Ndis802_11AutoUnknown)||
				//	pmlmepriv->cur_network.network.InfrastructureMode == pnetwork->network.InfrastructureMode)
				//		goto ask_for_joinbss;
				
				if (check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE)
				{
					if(is_same_network(&pmlmepriv->cur_network.network, &pnetwork->network))
					{
						//DBG_871X("select_and_join(1): _FW_LINKED and is same network, it needn't join again\n");

						rtw_indicate_connect(adapter);//rtw_indicate_connect again
							
						return 2;
					}
					else
					{
						rtw_disassoc_cmd(adapter);
						rtw_indicate_disconnect(adapter);
						rtw_free_assoc_resources(adapter);

						goto ask_for_joinbss;
						
					}
				}
				else
				{
					goto ask_for_joinbss;
				}
							
			}
			
		} else if (pmlmepriv->assoc_ssid.SsidLength == 0) {			
			goto ask_for_joinbss;//anyway, join first selected(dequeued) pnetwork if ssid_len=0				
	
		#ifdef CONFIG_LAYER2_ROAMING
		} else if(pmlmepriv->to_roaming>0) {
		
			if(	(roaming_candidate == NULL ||roaming_candidate->network.Rssi<pnetwork->network.Rssi )
				&& is_same_ess(&pnetwork->network, &pmlmepriv->cur_network.network) 
				//&&(!is_same_network(&pnetwork->network, &pmlmepriv->cur_network.network))
				&&  rtw_get_time_interval_ms((u32)pnetwork->last_scanned,cur_time) < 5000
				) {
				roaming_candidate = pnetwork;
				//RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,
				DBG_871X
					("roaming_candidate???: %s("MAC_FMT")\n",
					roaming_candidate->network.Ssid.Ssid, MAC_ARG(roaming_candidate->network.MacAddress) )
					//)
					;
			}
			continue;
		#endif

		} else if ( (pnetwork->network.Ssid.SsidLength==pmlmepriv->assoc_ssid.SsidLength)
			&&((_rtw_memcmp(dst_ssid, src_ssid, pmlmepriv->assoc_ssid.SsidLength)) == _TRUE)
			)
		{
			RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("dst_ssid=%s, src_ssid=%s \n", dst_ssid, src_ssid));
#ifdef CONFIG_ANTENNA_DIVERSITY
			adapter->HalFunc.GetHalDefVarHandler(adapter, HAL_DEF_CURRENT_ANTENNA, &(CurrentAntenna));			
			DBG_8192C("#### dst_ssid=(%s) Opt_Ant_(%s) , cur_Ant(%s)\n", dst_ssid,
				(2==pnetwork->network.PhyInfo.Optimum_antenna)?"A":"B",
				(2==CurrentAntenna)?"A":"B");
#endif
			//remove the condition @ 20081125
			//if((pmlmepriv->cur_network.network.InfrastructureMode==Ndis802_11AutoUnknown)||
			//	pmlmepriv->cur_network.network.InfrastructureMode == pnetwork->network.InfrastructureMode)
			//{
			//	_rtw_memcpy(pmlmepriv->assoc_bssid, pnetwork->network.MacAddress, ETH_ALEN);
			//	goto ask_for_joinbss;
			//}

			if(pmlmepriv->assoc_by_rssi==_TRUE)//if the ssid is the same, select the bss which has the max rssi
			{
				if( NULL==pnetwork_max_rssi|| pnetwork->network.Rssi > pnetwork_max_rssi->network.Rssi)
						pnetwork_max_rssi = pnetwork;					
			}
			else if(rtw_is_desired_network(adapter, pnetwork) == _TRUE)
			{
				if (check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE)
				{
#if 0				
					if(is_same_network(&pmlmepriv->cur_network.network, &pnetwork->network))
					{
						DBG_871X("select_and_join(2): _FW_LINKED and is same network, it needn't join again\n");
						
						rtw_indicate_connect(adapter);//rtw_indicate_connect again
						
						return 2;
					}
					else
#endif						
					{
						rtw_disassoc_cmd(adapter);
						//rtw_indicate_disconnect(adapter);//
						rtw_free_assoc_resources(adapter);

						goto ask_for_joinbss;						
					}
				}
				else
				{
					goto ask_for_joinbss;
				}				

			}
		
			
		}
 	
 	}

	#ifdef CONFIG_LAYER2_ROAMING
	if(pmlmepriv->to_roaming>0 && roaming_candidate ){
		pnetwork=roaming_candidate;
		DBG_871X("select_and_join_from_scanned_queue: roaming_candidate: %s("MAC_FMT")\n",
			pnetwork->network.Ssid.Ssid, MAC_ARG(pnetwork->network.MacAddress));
		goto ask_for_joinbss;
	}
	#endif

	if((pmlmepriv->assoc_by_rssi==_TRUE)  && (pnetwork_max_rssi!=NULL))
	{
		pnetwork = pnetwork_max_rssi;
		DBG_871X("select_and_join_from_scanned_queue: pnetwork_max_rssi: %s("MAC_FMT")\n",
			pnetwork->network.Ssid.Ssid, MAC_ARG(pnetwork->network.MacAddress));
		goto ask_for_joinbss;
	}

	DBG_871X("(1)rtw_select_and_join_from_scanned_queue return _FAIL\n");
	
_func_exit_;	

     return _FAIL;

ask_for_joinbss:
	
_func_exit_;

	return rtw_joinbss_cmd(adapter, pnetwork);

}
#endif


sint rtw_set_auth(_adapter * adapter,struct security_priv *psecuritypriv)
{
	struct	cmd_obj* pcmd;
	struct 	setauth_parm *psetauthparm;
	struct	cmd_priv	*pcmdpriv=&(adapter->cmdpriv);
	sint		res=_SUCCESS;
	
_func_enter_;	

	pcmd = (struct	cmd_obj*)rtw_zmalloc(sizeof(struct	cmd_obj));
	if(pcmd==NULL){
		res= _FAIL;  //try again
		goto exit;
	}
	
	psetauthparm=(struct setauth_parm*)rtw_zmalloc(sizeof(struct setauth_parm));
	if(psetauthparm==NULL){
		rtw_mfree((unsigned char *)pcmd, sizeof(struct	cmd_obj));
		res= _FAIL;
		goto exit;
	}

	_rtw_memset(psetauthparm, 0, sizeof(struct setauth_parm));
	psetauthparm->mode=(unsigned char)psecuritypriv->dot11AuthAlgrthm;
	
	pcmd->cmdcode = _SetAuth_CMD_;
	pcmd->parmbuf = (unsigned char *)psetauthparm;   
	pcmd->cmdsz =  (sizeof(struct setauth_parm));  
	pcmd->rsp = NULL;
	pcmd->rspsz = 0;


	_rtw_init_listhead(&pcmd->list);

	RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("after enqueue set_auth_cmd, auth_mode=%x\n", psecuritypriv->dot11AuthAlgrthm));

	res = rtw_enqueue_cmd(pcmdpriv, pcmd);

exit:

_func_exit_;

	return res;

}


sint rtw_set_key(_adapter * adapter,struct security_priv *psecuritypriv,sint keyid, u8 set_tx)
{
	u8	keylen;
	struct cmd_obj		*pcmd;
	struct setkey_parm	*psetkeyparm;
	struct cmd_priv		*pcmdpriv = &(adapter->cmdpriv);
	struct mlme_priv		*pmlmepriv = &(adapter->mlmepriv);
	sint	res=_SUCCESS;
	
_func_enter_;
	
	pcmd = (struct	cmd_obj*)rtw_zmalloc(sizeof(struct	cmd_obj));
	if(pcmd==NULL){
		res= _FAIL;  //try again
		goto exit;
	}
	psetkeyparm=(struct setkey_parm*)rtw_zmalloc(sizeof(struct setkey_parm));
	if(psetkeyparm==NULL){
		rtw_mfree((unsigned char *)pcmd, sizeof(struct	cmd_obj));
		res= _FAIL;
		goto exit;
	}

	_rtw_memset(psetkeyparm, 0, sizeof(struct setkey_parm));

	if(psecuritypriv->dot11AuthAlgrthm ==dot11AuthAlgrthm_8021X){		
		psetkeyparm->algorithm=(unsigned char)psecuritypriv->dot118021XGrpPrivacy;	
		RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("\n rtw_set_key: psetkeyparm->algorithm=(unsigned char)psecuritypriv->dot118021XGrpPrivacy=%d \n", psetkeyparm->algorithm));
	}	
	else{
		psetkeyparm->algorithm=(u8)psecuritypriv->dot11PrivacyAlgrthm;
		RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("\n rtw_set_key: psetkeyparm->algorithm=(u8)psecuritypriv->dot11PrivacyAlgrthm=%d \n", psetkeyparm->algorithm));

	}
	psetkeyparm->keyid = (u8)keyid;//0~3
	psetkeyparm->set_tx = set_tx;
	pmlmepriv->key_mask |= BIT(psetkeyparm->keyid);
#ifdef CONFIG_AUTOSUSPEND
	if( _TRUE  == adapter->pwrctrlpriv.bInternalAutoSuspend)
	{
		adapter->pwrctrlpriv.wepkeymask = pmlmepriv->key_mask;
		DBG_8192C("....AutoSuspend pwrctrlpriv.wepkeymask(%x)\n",adapter->pwrctrlpriv.wepkeymask);
	}
#endif
	DBG_8192C("==> rtw_set_key algorithm(%x),keyid(%x),key_mask(%x)\n",psetkeyparm->algorithm,psetkeyparm->keyid,pmlmepriv->key_mask);
	RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("\n rtw_set_key: psetkeyparm->algorithm=%d psetkeyparm->keyid=(u8)keyid=%d \n",psetkeyparm->algorithm, keyid));

	switch(psetkeyparm->algorithm){
		
		case _WEP40_:
			keylen=5;
			_rtw_memcpy(&(psetkeyparm->key[0]), &(psecuritypriv->dot11DefKey[keyid].skey[0]), keylen);
			break;
		case _WEP104_:
			keylen=13;
			_rtw_memcpy(&(psetkeyparm->key[0]), &(psecuritypriv->dot11DefKey[keyid].skey[0]), keylen);
			break;
		case _TKIP_:
			keylen=16;			
			_rtw_memcpy(&psetkeyparm->key, &psecuritypriv->dot118021XGrpKey[keyid], keylen);
			psetkeyparm->grpkey=1;
			break;
		case _AES_:
			keylen=16;			
			_rtw_memcpy(&psetkeyparm->key, &psecuritypriv->dot118021XGrpKey[keyid], keylen);
			psetkeyparm->grpkey=1;
			break;
		default:
			RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("\n rtw_set_key:psecuritypriv->dot11PrivacyAlgrthm = %x (must be 1 or 2 or 4 or 5)\n",psecuritypriv->dot11PrivacyAlgrthm));
			res= _FAIL;
			goto exit;
	}

	
	pcmd->cmdcode = _SetKey_CMD_;
	pcmd->parmbuf = (u8 *)psetkeyparm;   
	pcmd->cmdsz =  (sizeof(struct setkey_parm));  
	pcmd->rsp = NULL;
	pcmd->rspsz = 0;


	_rtw_init_listhead(&pcmd->list);

	//_rtw_init_sema(&(pcmd->cmd_sem), 0);

	res = rtw_enqueue_cmd(pcmdpriv, pcmd);

exit:
_func_exit_;
	return res;

}


//adjust IEs for rtw_joinbss_cmd in WMM
int rtw_restruct_wmm_ie(_adapter *adapter, u8 *in_ie, u8 *out_ie, uint in_len, uint initial_out_len)
{
	unsigned	int ielength=0;
	unsigned int i, j;

	i = 12; //after the fixed IE
	while(i<in_len)
	{
		ielength = initial_out_len;		
		
		if(in_ie[i] == 0xDD && in_ie[i+2] == 0x00 && in_ie[i+3] == 0x50  && in_ie[i+4] == 0xF2 && in_ie[i+5] == 0x02 && i+5 < in_len) //WMM element ID and OUI
		{

			//Append WMM IE to the last index of out_ie
			/*
			for(j=i; j< i+(in_ie[i+1]+2); j++)
			{
				out_ie[ielength] = in_ie[j];				
				ielength++;
			}
			out_ie[initial_out_len+8] = 0x00; //force the QoS Info Field to be zero
	                */
                       
                        for ( j = i; j < i + 9; j++ )
                        {
                            out_ie[ ielength] = in_ie[ j ];
                            ielength++;
                        } 
                        out_ie[ initial_out_len + 1 ] = 0x07;
                        out_ie[ initial_out_len + 6 ] = 0x00;
                        out_ie[ initial_out_len + 8 ] = 0x00;
	
			break;
		}

		i+=(in_ie[i+1]+2); // to the next IE element
	}
	
	return ielength;
	
}


//
// Ported from 8185: IsInPreAuthKeyList(). (Renamed from SecIsInPreAuthKeyList(), 2006-10-13.)
// Added by Annie, 2006-05-07.
//
// Search by BSSID,
// Return Value:
//		-1 		:if there is no pre-auth key in the  table
//		>=0		:if there is pre-auth key, and   return the entry id
//
//

static int SecIsInPMKIDList(_adapter *Adapter, u8 *bssid)
{
	struct security_priv *psecuritypriv=&Adapter->securitypriv;
	int i=0;

	do
	{
		if( ( psecuritypriv->PMKIDList[i].bUsed ) && 
                    (  _rtw_memcmp( psecuritypriv->PMKIDList[i].Bssid, bssid, ETH_ALEN ) == _TRUE ) )
		{
			break;
		}
		else
		{	
			i++;
			//continue;
		}
		
	}while(i<NUM_PMKID_CACHE);

	if( i == NUM_PMKID_CACHE )
	{ 
		i = -1;// Could not find.
	}
	else
	{ 
		// There is one Pre-Authentication Key for the specific BSSID.
	}

	return (i);
	
}

sint rtw_restruct_sec_ie(_adapter *adapter,u8 *in_ie, u8 *out_ie, uint in_len)
{
	u8 authmode, securitytype, match;
	u8 sec_ie[255], uncst_oui[4], bkup_ie[255];
	u8 wpa_oui[4]={0x0, 0x50, 0xf2, 0x01};
	uint 	ielength, cnt, remove_cnt;
	int iEntry;

	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	struct security_priv *psecuritypriv=&adapter->securitypriv;
	uint 	ndisauthmode=psecuritypriv->ndisauthtype;
	uint ndissecuritytype = psecuritypriv->ndisencryptstatus;
	
_func_enter_;

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_notice_,
		 ("+rtw_restruct_sec_ie: ndisauthmode=%d ndissecuritytype=%d\n",
		  ndisauthmode, ndissecuritytype));
#if 0	  
	authmode = 0xFF;//init

	if((ndisauthmode==Ndis802_11AuthModeWPA)||(ndisauthmode==Ndis802_11AuthModeWPAPSK))
	{
		authmode=_WPA_IE_ID_;
		uncst_oui[0]=0x0;
		uncst_oui[1]=0x50;
		uncst_oui[2]=0xf2;
	}
	if((ndisauthmode==Ndis802_11AuthModeWPA2)||(ndisauthmode==Ndis802_11AuthModeWPA2PSK))
	{	
		authmode=_WPA2_IE_ID_;
		uncst_oui[0]=0x0;
		uncst_oui[1]=0x0f;
		uncst_oui[2]=0xac;
	}
	
	switch(ndissecuritytype)
	{
		case Ndis802_11Encryption1Enabled:
		case Ndis802_11Encryption1KeyAbsent:
				securitytype=_WEP40_;
				uncst_oui[3]=0x1;
				break;
		case Ndis802_11Encryption2Enabled:
		case Ndis802_11Encryption2KeyAbsent:
				securitytype=_TKIP_;
				uncst_oui[3]=0x2;
				break;
		case Ndis802_11Encryption3Enabled:
		case Ndis802_11Encryption3KeyAbsent: 	
				securitytype=_AES_;
				uncst_oui[3]=0x4;
				break;
		default:
				securitytype=_NO_PRIVACY_;
				break;				
	}
		
	//Search required WPA or WPA2 IE and copy to sec_ie[ ]
	cnt=12;
	match=_FALSE;
	while(cnt<in_len)
	{
		if(in_ie[cnt]==authmode)
		{
			if((authmode==_WPA_IE_ID_)&&(_rtw_memcmp(&in_ie[cnt+2], &wpa_oui[0], 4)==_TRUE))
			{
				_rtw_memcpy(&sec_ie[0], &in_ie[cnt], in_ie[cnt+1]+2);
				match=_TRUE;
				RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("rtw_restruct_sec_ie: Get WPA IE from %d in_len=%d \n",cnt,in_len));
				break;
			}
			if(authmode==_WPA2_IE_ID_)
			{
				_rtw_memcpy(&sec_ie[0], &in_ie[cnt], in_ie[cnt+1]+2);
				match=_TRUE;
				RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("rtw_restruct_sec_ie: Get WPA2 IE from %d in_len=%d \n",cnt,in_len));
				break;
			}	
			if(((authmode==_WPA_IE_ID_)&&(_rtw_memcmp(&in_ie[cnt+2], &wpa_oui[0], 4)==_TRUE))||(authmode==_WPA2_IE_ID_))
			{
				_rtw_memcpy(&bkup_ie[0], &in_ie[cnt], in_ie[cnt+1]+2);
				RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("rtw_restruct_sec_ie: cnt=%d in_len=%d \n",cnt,in_len));
			}
		}
	
		cnt += in_ie[cnt+1] + 2; //get next
	}
	
	//restruct WPA IE or WPA2 IE in sec_ie[ ]
	if(match==_TRUE)
	{
		if(sec_ie[0]==_WPA_IE_ID_)
		{
			// parsing SSN IE to select required encryption algorithm, and set the bc/mc encryption algorithm
			while(_TRUE)
			{
				if(_rtw_memcmp(&sec_ie[2], &wpa_oui[0], 4) !=_TRUE)//check wpa_oui tag
				{  	 
					RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("\n SSN IE but doesn't has wpa_oui tag! \n"));
					match=_FALSE;
					break;
				}
				
				if((sec_ie[6]!=0x01) ||(sec_ie[7]!= 0x0))
				{ 	
					//IE Ver error
					RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("\n SSN IE :IE version error (%.2x %.2x != 01 00 )! \n",sec_ie[6],sec_ie[7]));
					match=_FALSE;
					break;
				}
				
				if(_rtw_memcmp(&sec_ie[8], &wpa_oui[0], 3) ==_TRUE)
				{ 
					//get bc/mc encryption type (group key tyep)
					switch(sec_ie[11])
					{
						case 0x0: //none
							psecuritypriv->dot118021XGrpPrivacy=_NO_PRIVACY_;
							break;
						case 0x1: //WEP_40
							psecuritypriv->dot118021XGrpPrivacy=_WEP40_;
							break;
						case 0x2: //TKIP
							psecuritypriv->dot118021XGrpPrivacy=_TKIP_;
							break;
						case 0x3: //AESCCMP
						case 0x4: 
							psecuritypriv->dot118021XGrpPrivacy=_AES_;
							break;
						case 0x5: //WEP_104	
							psecuritypriv->dot118021XGrpPrivacy=_WEP104_;
							break;
					}
					
				}
				else
				{
					RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("\n SSN IE :Multicast error (%.2x %.2x %.2x %.2x != 00 50 F2 xx )! \n",
									sec_ie[8],sec_ie[9],sec_ie[10],sec_ie[11]));
					match =_FALSE;
					break;
				}
				
				if(sec_ie[12]==0x01)
				{ 
					//check the unicast encryption type
					if(_rtw_memcmp(&sec_ie[14], &uncst_oui[0], 4) !=_TRUE)
					{
						RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("\n SSN IE :Unicast error (%.2x %.2x %.2x %.2x != 00 50 F2 %.2x )! \n",
										sec_ie[14],sec_ie[15],sec_ie[16],sec_ie[17],uncst_oui[3]));
						match =_FALSE;
						
						break;
						
					} //else the uncst_oui is match
				}
				else//mixed mode, unicast_enc_type > 1
				{
					//select the uncst_oui and remove the other uncst_oui
					cnt=sec_ie[12];
					remove_cnt=(cnt-1)*4;
					sec_ie[12]=0x01;
					_rtw_memcpy(&sec_ie[14], &uncst_oui[0], 4);
					
					//remove the other unicast suit
					_rtw_memcpy(&sec_ie[18], &sec_ie[18+remove_cnt],(sec_ie[1]-18+2-remove_cnt));
					sec_ie[1]=sec_ie[1]-remove_cnt;
				}
				
				break;				
			}			
		}

		if(authmode==_WPA2_IE_ID_)
		{
			// parsing RSN IE to select required encryption algorithm, and set the bc/mc encryption algorithm
			while(_TRUE)
			{
				if((sec_ie[2]!=0x01)||(sec_ie[3]!= 0x0))
				{ 
					//IE Ver error
					RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("\n RSN IE :IE version error (%.2x %.2x != 01 00 )! \n",sec_ie[2],sec_ie[3]));
					match=_FALSE;
					break;
				}
				
				if(_rtw_memcmp(&sec_ie[4], &uncst_oui[0], 3) ==_TRUE)
				{ 
					//get bc/mc encryption type
					switch(sec_ie[7])
					{
						case 0x1: //WEP_40
							psecuritypriv->dot118021XGrpPrivacy=_WEP40_;
							break;
						case 0x2: //TKIP
							psecuritypriv->dot118021XGrpPrivacy=_TKIP_;
							break;
						case 0x4: //AESWRAP
							psecuritypriv->dot118021XGrpPrivacy=_AES_;
							break;
						case 0x5: //WEP_104	
							psecuritypriv->dot118021XGrpPrivacy=_WEP104_;
							break;
						default: //none
							psecuritypriv->dot118021XGrpPrivacy=_NO_PRIVACY_;
							break;	
					}
				}
				else
				{
					RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("\n RSN IE :Multicast error (%.2x %.2x %.2x %.2x != 00 50 F2 xx )! \n",
								sec_ie[4],sec_ie[5],sec_ie[6],sec_ie[7]));
					match=_FALSE;
					break;
				}
				
				if(sec_ie[8]==0x01)
				{ 
					//check the unicast encryption type
					if(_rtw_memcmp(&sec_ie[10], &uncst_oui[0],4) !=_TRUE)
					{
						RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("\n SSN IE :Unicast error (%.2x %.2x %.2x %.2x != 00 50 F2 xx )! \n",
									sec_ie[10],sec_ie[11],sec_ie[12],sec_ie[13]));

						match =_FALSE;						
						break;
						
					} //else the uncst_oui is match
				}
				else //mixed mode, unicast_enc_type > 1
				{ 
					//select the uncst_oui and remove the other uncst_oui
					cnt=sec_ie[8];
					remove_cnt=(cnt-1)*4;
					sec_ie[8]=0x01;
					_rtw_memcpy( &sec_ie[10] , &uncst_oui[0],4 );
					
					//remove the other unicast suit
					_rtw_memcpy(&sec_ie[14],&sec_ie[14+remove_cnt],(sec_ie[1]-14+2-remove_cnt));
					sec_ie[1]=sec_ie[1]-remove_cnt;
				}

				break;				
			}			
		}

	}

	//copy fixed ie only
	_rtw_memcpy(out_ie, in_ie,12);
	ielength=12;

	if(psecuritypriv->wps_phase == _TRUE)
	{
		//DBG_871X("wps_phase == _TRUE\n");

		_rtw_memcpy(out_ie+ielength, psecuritypriv->wps_ie, psecuritypriv->wps_ie_len);
		
		ielength += psecuritypriv->wps_ie_len;
		psecuritypriv->wps_phase = _FALSE;
	
	}
	else if((authmode==_WPA_IE_ID_)||(authmode==_WPA2_IE_ID_))
	{		
		//copy RSN or SSN		
		if(match ==_TRUE)
		{
			_rtw_memcpy(&out_ie[ielength], &sec_ie[0], sec_ie[1]+2);
			ielength+=sec_ie[1]+2;
			
			if(authmode==_WPA2_IE_ID_)
			{
				//the Pre-Authentication bit should be zero, john
				out_ie[ielength-1]= 0;
				out_ie[ielength-2]= 0;
			}

			rtw_report_sec_ie(adapter, authmode, sec_ie);
	
#ifdef CONFIG_DRVEXT_MODULE
			drvext_report_sec_ie(&adapter->drvextpriv, authmode, sec_ie);	
#endif
			
		}
		
	}
	else
	{
	
	}
	
	iEntry = SecIsInPMKIDList(adapter, pmlmepriv->assoc_bssid);
	if(iEntry<0)
	{
		return ielength;
	}
	else
	{
		if(authmode == _WPA2_IE_ID_)
		{
			out_ie[ielength]=1;
			ielength++;
			out_ie[ielength]=0;	//PMKID count = 0x0100
			ielength++;
			_rtw_memcpy(	&out_ie[ielength], &psecuritypriv->PMKIDList[iEntry].PMKID, 16);
		
			ielength+=16;
			out_ie[13]+=18;//PMKID length = 2+16
		}
	}
	
	//report_sec_ie(adapter, authmode, sec_ie);
#else 
	
//copy fixed ie only
	_rtw_memcpy(out_ie, in_ie,12);
	ielength=12;

	if(psecuritypriv->wps_phase == _TRUE)
	{
		//DBG_871X("wps_phase == _TRUE\n");

		_rtw_memcpy(out_ie+ielength, psecuritypriv->wps_ie, psecuritypriv->wps_ie_len);
		
		ielength += psecuritypriv->wps_ie_len;
		psecuritypriv->wps_phase = _FALSE;
	
	}
	else if((ndisauthmode==Ndis802_11AuthModeWPA)||(ndisauthmode==Ndis802_11AuthModeWPAPSK)||(ndisauthmode==Ndis802_11AuthModeWPA2)||(ndisauthmode==Ndis802_11AuthModeWPA2PSK))
	{		
		//copy RSN or SSN		
			_rtw_memcpy(&out_ie[ielength], &psecuritypriv->supplicant_ie[0], psecuritypriv->supplicant_ie[1]+2);
			ielength+=psecuritypriv->supplicant_ie[1]+2;

			rtw_report_sec_ie(adapter, authmode, psecuritypriv->supplicant_ie);
	
#ifdef CONFIG_DRVEXT_MODULE
			drvext_report_sec_ie(&adapter->drvextpriv, authmode, sec_ie);	
#endif
			

		
	}
	iEntry = SecIsInPMKIDList(adapter, pmlmepriv->assoc_bssid);
	if(iEntry<0)
	{
		return ielength;
	}
	else
	{
		if(authmode == _WPA2_IE_ID_)
		{
			out_ie[ielength]=1;
			ielength++;
			out_ie[ielength]=0;	//PMKID count = 0x0100
			ielength++;
			_rtw_memcpy(	&out_ie[ielength], &psecuritypriv->PMKIDList[iEntry].PMKID, 16);
		
			ielength+=16;
			out_ie[13]+=18;//PMKID length = 2+16
		}
	}
#endif
_func_exit_;
	
	return ielength;	
}

void rtw_init_registrypriv_dev_network(	_adapter* adapter)
{
	struct registry_priv* pregistrypriv = &adapter->registrypriv;
	struct eeprom_priv* peepriv = &adapter->eeprompriv;
	WLAN_BSSID_EX    *pdev_network = &pregistrypriv->dev_network;
	u8 *myhwaddr = myid(peepriv);
	
_func_enter_;

	_rtw_memcpy(pdev_network->MacAddress, myhwaddr, ETH_ALEN);

	_rtw_memcpy(&pdev_network->Ssid, &pregistrypriv->ssid, sizeof(NDIS_802_11_SSID));
	
	pdev_network->Configuration.Length=sizeof(NDIS_802_11_CONFIGURATION);
	pdev_network->Configuration.BeaconPeriod = 100;	
	pdev_network->Configuration.FHConfig.Length = 0;
	pdev_network->Configuration.FHConfig.HopPattern = 0;
	pdev_network->Configuration.FHConfig.HopSet = 0;
	pdev_network->Configuration.FHConfig.DwellTime = 0;
	
	
_func_exit_;	
	
}

void rtw_update_registrypriv_dev_network(_adapter* adapter) 
{
	int sz=0;
	struct registry_priv* pregistrypriv = &adapter->registrypriv;	
	WLAN_BSSID_EX    *pdev_network = &pregistrypriv->dev_network;
	struct	security_priv*	psecuritypriv = &adapter->securitypriv;
	struct	wlan_network	*cur_network = &adapter->mlmepriv.cur_network;
	struct	xmit_priv	*pxmitpriv = &adapter->xmitpriv;

_func_enter_;

#if 0
	pxmitpriv->vcs_setting = pregistrypriv->vrtl_carrier_sense;
	pxmitpriv->vcs = pregistrypriv->vcs_type;
	pxmitpriv->vcs_type = pregistrypriv->vcs_type;
	//pxmitpriv->rts_thresh = pregistrypriv->rts_thresh;
	pxmitpriv->frag_len = pregistrypriv->frag_thresh;
	
	adapter->qospriv.qos_option = pregistrypriv->wmm_enable;
#endif	

	pdev_network->Privacy = (psecuritypriv->dot11PrivacyAlgrthm > 0 ? 1 : 0) ; // adhoc no 802.1x

	pdev_network->Rssi = 0;

	switch(pregistrypriv->wireless_mode)
	{
		case WIRELESS_11B:
			pdev_network->NetworkTypeInUse = (Ndis802_11DS);
			break;	
		case WIRELESS_11G:
		case WIRELESS_11BG:
		case WIRELESS_11_24N:
		case WIRELESS_11G_24N:
		case WIRELESS_11BG_24N:
			pdev_network->NetworkTypeInUse = (Ndis802_11OFDM24);
			break;
		case WIRELESS_11A:
		case WIRELESS_11A_5N:
			pdev_network->NetworkTypeInUse = (Ndis802_11OFDM5);
			break;
		case WIRELESS_11ABGN:
			if(pregistrypriv->channel > 14)
				pdev_network->NetworkTypeInUse = (Ndis802_11OFDM5);
			else
				pdev_network->NetworkTypeInUse = (Ndis802_11OFDM24);
			break;
		default :
			// TODO
			break;
	}
	
	pdev_network->Configuration.DSConfig = (pregistrypriv->channel);
	RT_TRACE(_module_rtl871x_mlme_c_,_drv_info_,("pregistrypriv->channel=%d, pdev_network->Configuration.DSConfig=0x%x\n", pregistrypriv->channel, pdev_network->Configuration.DSConfig));	

	if(cur_network->network.InfrastructureMode == Ndis802_11IBSS)
		pdev_network->Configuration.ATIMWindow = (0);

	pdev_network->InfrastructureMode = (cur_network->network.InfrastructureMode);

	// 1. Supported rates
	// 2. IE

	//rtw_set_supported_rate(pdev_network->SupportedRates, pregistrypriv->wireless_mode) ; // will be called in rtw_generate_ie
	sz = rtw_generate_ie(pregistrypriv);

	pdev_network->IELength = sz;

	pdev_network->Length = get_WLAN_BSSID_EX_sz((WLAN_BSSID_EX  *)pdev_network);

	//notes: translate IELength & Length after assign the Length to cmdsz in createbss_cmd();
	//pdev_network->IELength = cpu_to_le32(sz);
		
_func_exit_;	

}

void rtw_get_encrypt_decrypt_from_registrypriv(_adapter* adapter)
{
	u16	wpaconfig=0;
	struct registry_priv* pregistrypriv = &adapter->registrypriv;
	struct security_priv* psecuritypriv= &adapter->securitypriv;
_func_enter_;


_func_exit_;	
	
}

//the fucntion is at passive_level 
void rtw_joinbss_reset(_adapter *padapter)
{
	u8	threshold;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;

#ifdef CONFIG_80211N_HT	
	struct ht_priv		*phtpriv = &pmlmepriv->htpriv;
#endif

	//todo: if you want to do something io/reg/hw setting before join_bss, please add code here
	



#ifdef CONFIG_80211N_HT

	pmlmepriv->num_FortyMHzIntolerant = 0;

	pmlmepriv->num_sta_no_ht = 0;

	phtpriv->ampdu_enable = _FALSE;//reset to disabled

#ifdef CONFIG_USB_HCI
	if(phtpriv->ht_option)
	{
		//validate  usb rx aggregation
		//rtw_write8(padapter, 0x102500D9, 48);//TH = 48 pages, 6k
		threshold = 48;
		if(padapter->registrypriv.wifi_spec==1)		
                     threshold = 1;
		padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_RXDMA_AGG_PG_TH, (u8 *)(&threshold));
	}
	else
	{
		//invalidate  usb rx aggregation
		//rtw_write8(padapter, 0x102500D9, 1);// TH=1 => means that invalidate usb rx aggregation
		threshold = 1;
		padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_RXDMA_AGG_PG_TH, (u8 *)(&threshold));
	}
#endif

#endif	

}


#ifdef CONFIG_80211N_HT

//the fucntion is >= passive_level 
unsigned int rtw_restructure_ht_ie(_adapter *padapter, u8 *in_ie, u8 *out_ie, uint in_len, uint *pout_len)
{
	u32 ielen, out_len;
	unsigned char *p, *pframe;
	struct ieee80211_ht_cap ht_capie;
	unsigned char WMM_IE[] = {0x00, 0x50, 0xf2, 0x02, 0x00, 0x01, 0x00};
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct qos_priv   	*pqospriv= &pmlmepriv->qospriv;
	struct ht_priv		*phtpriv = &pmlmepriv->htpriv;


	phtpriv->ht_option = _FALSE;

	p = rtw_get_ie(in_ie+12, _HT_CAPABILITY_IE_, &ielen, in_len-12);

	if(p && ielen>0)
	{
		if(pqospriv->qos_option == 0)
		{
			out_len = *pout_len;
			pframe = rtw_set_ie(out_ie+out_len, _VENDOR_SPECIFIC_IE_, 
								_WMM_IE_Length_, WMM_IE, pout_len);

			pqospriv->qos_option = 1;
		}

		out_len = *pout_len;

		_rtw_memset(&ht_capie, 0, sizeof(struct ieee80211_ht_cap));

		ht_capie.cap_info = IEEE80211_HT_CAP_SUP_WIDTH |IEEE80211_HT_CAP_SGI_20 |
							IEEE80211_HT_CAP_SGI_40 | IEEE80211_HT_CAP_TX_STBC |
							IEEE80211_HT_CAP_DSSSCCK40;


		{
			u32 rx_packet_offset, max_recvbuf_sz;
			padapter->HalFunc.GetHalDefVarHandler(padapter, HAL_DEF_RX_PACKET_OFFSET, &rx_packet_offset);
			padapter->HalFunc.GetHalDefVarHandler(padapter, HAL_DEF_MAX_RECVBUF_SZ, &max_recvbuf_sz);
			if(max_recvbuf_sz-rx_packet_offset>(8191-256)) {
				DBG_871X("%s IEEE80211_HT_CAP_MAX_AMSDU is set\n", __FUNCTION__);
				ht_capie.cap_info = ht_capie.cap_info |IEEE80211_HT_CAP_MAX_AMSDU;
			}
		}
		
		ht_capie.ampdu_params_info = (IEEE80211_HT_CAP_AMPDU_FACTOR&0x03);

		if(padapter->securitypriv.dot11PrivacyAlgrthm == _AES_ )
			ht_capie.ampdu_params_info |= (IEEE80211_HT_CAP_AMPDU_DENSITY&(0x07<<2)); 
		else
			ht_capie.ampdu_params_info |= (IEEE80211_HT_CAP_AMPDU_DENSITY&0x00);										

		
		pframe = rtw_set_ie(out_ie+out_len, _HT_CAPABILITY_IE_, 
							sizeof(struct ieee80211_ht_cap), (unsigned char*)&ht_capie, pout_len);


		//_rtw_memcpy(out_ie+out_len, p, ielen+2);//gtest
		//*pout_len = *pout_len + (ielen+2);

							
		phtpriv->ht_option = _TRUE;

		p = rtw_get_ie(in_ie+12, _HT_ADD_INFO_IE_, &ielen, in_len-12);
		if(p && (ielen==sizeof(struct ieee80211_ht_addt_info)))
		{
			out_len = *pout_len;		
			pframe = rtw_set_ie(out_ie+out_len, _HT_ADD_INFO_IE_, ielen, p+2 , pout_len);
		}

	}
	
	return (phtpriv->ht_option);
	
}

//the fucntion is > passive_level (in critical_section)
void rtw_update_ht_cap(_adapter *padapter, u8 *pie, uint ie_len)
{	
	u8 *p, max_ampdu_sz;
	int i, len;		
	struct sta_info *bmc_sta, *psta;	
	struct ieee80211_ht_cap *pht_capie;
	struct ieee80211_ht_addt_info *pht_addtinfo;
	struct recv_reorder_ctrl *preorder_ctrl;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct ht_priv		*phtpriv = &pmlmepriv->htpriv;
	struct recv_priv *precvpriv = &padapter->recvpriv;
	struct registry_priv *pregistrypriv = &padapter->registrypriv;
	struct wlan_network *pcur_network = &(pmlmepriv->cur_network);
	

	if(!phtpriv->ht_option)
		return;


	//DBG_8192C("+rtw_update_ht_cap()\n");

	//maybe needs check if ap supports rx ampdu.
	if((phtpriv->ampdu_enable==_FALSE) &&(pregistrypriv->ampdu_enable==1))
	{
		if(pregistrypriv->wifi_spec==1)
		{
			phtpriv->ampdu_enable = _FALSE;
		}
		else
		{
			phtpriv->ampdu_enable = _TRUE;
		}
	}
	else if(pregistrypriv->ampdu_enable==2)
	{
		phtpriv->ampdu_enable = _TRUE;
	}

	
	//check Max Rx A-MPDU Size 
	len = 0;
	p = rtw_get_ie(pie+sizeof (NDIS_802_11_FIXED_IEs), _HT_CAPABILITY_IE_, &len, ie_len-sizeof (NDIS_802_11_FIXED_IEs));
	if(p && len>0)	
	{
		pht_capie = (struct ieee80211_ht_cap *)(p+2);
		max_ampdu_sz = (pht_capie->ampdu_params_info & IEEE80211_HT_CAP_AMPDU_FACTOR);
		max_ampdu_sz = 1 << (max_ampdu_sz+3); // max_ampdu_sz (kbytes);
		
		//DBG_8192C("rtw_update_ht_cap(): max_ampdu_sz=%d\n", max_ampdu_sz);
		phtpriv->rx_ampdu_maxlen = max_ampdu_sz;
		
	}

	//for A-MPDU Rx reordering buffer control for bmc_sta & sta_info
	//if A-MPDU Rx is enabled, reseting  rx_ordering_ctrl wstart_b(indicate_seq) to default value=0xffff
	//todo: check if AP can send A-MPDU packets
	bmc_sta = rtw_get_bcmc_stainfo(padapter);
	if(bmc_sta)
	{
		for(i=0; i < 16 ; i++)
		{
			//preorder_ctrl = &precvpriv->recvreorder_ctrl[i];
			preorder_ctrl = &bmc_sta->recvreorder_ctrl[i];
			preorder_ctrl->enable = _FALSE;
			preorder_ctrl->indicate_seq = 0xffff;
			#ifdef DBG_RX_SEQ
			DBG_871X("DBG_RX_SEQ %s:%d indicate_seq:%u \n", __FUNCTION__, __LINE__,
				preorder_ctrl->indicate_seq);
			#endif
			preorder_ctrl->wend_b= 0xffff;
			preorder_ctrl->wsize_b = 64;//max_ampdu_sz;//ex. 32(kbytes) -> wsize_b=32
		}
	}

	psta = rtw_get_stainfo(&padapter->stapriv, pcur_network->network.MacAddress);
	if(psta)
	{
		for(i=0; i < 16 ; i++)
		{
			//preorder_ctrl = &precvpriv->recvreorder_ctrl[i];
			preorder_ctrl = &psta->recvreorder_ctrl[i];
			preorder_ctrl->enable = _FALSE;
			preorder_ctrl->indicate_seq = 0xffff;
			#ifdef DBG_RX_SEQ
			DBG_871X("DBG_RX_SEQ %s:%d indicate_seq:%u \n", __FUNCTION__, __LINE__,
				preorder_ctrl->indicate_seq);
			#endif
			preorder_ctrl->wend_b= 0xffff;
			preorder_ctrl->wsize_b = 64;//max_ampdu_sz;//ex. 32(kbytes) -> wsize_b=32
		}
	}

	len=0;
	p = rtw_get_ie(pie+sizeof (NDIS_802_11_FIXED_IEs), _HT_ADD_INFO_IE_, &len, ie_len-sizeof (NDIS_802_11_FIXED_IEs));
	if(p && len>0)	
	{
		pht_addtinfo = (struct ieee80211_ht_addt_info *)(p+2);		
	}

}

void rtw_issue_addbareq_cmd(_adapter *padapter, struct xmit_frame *pxmitframe)
{
	u8 issued;
	int priority;
	struct sta_info *psta=NULL;
	struct ht_priv	*phtpriv;
	struct pkt_attrib *pattrib =&pxmitframe->attrib;
 	struct sta_priv *pstapriv = &padapter->stapriv;	
	s32 bmcst = IS_MCAST(pattrib->ra);

	if(bmcst)
		return;
	
	priority = pattrib->priority;

	if (pattrib->psta)
		psta = pattrib->psta;
	else
		psta = rtw_get_stainfo(&padapter->stapriv, pattrib->ra);
	
	if(psta==NULL)
		return;
	
	phtpriv = &psta->htpriv;

	if((phtpriv->ht_option==_TRUE) && (phtpriv->ampdu_enable==_TRUE)) 
	{
		issued = (phtpriv->agg_enable_bitmap>>priority)&0x1;
		issued |= (phtpriv->candidate_tid_bitmap>>priority)&0x1;

		if(0==issued)
		{
			DBG_871X("rtw_issue_addbareq_cmd, p=%d\n", priority);
			psta->htpriv.candidate_tid_bitmap |= BIT((u8)priority);
			rtw_addbareq_cmd(padapter,(u8) priority, pattrib->ra);
		}
	}
	
}

#endif

#ifdef CONFIG_LAYER2_ROAMING
void rtw_roaming(_adapter *padapter, struct wlan_network *tgt_network)
{
	_irqL irqL;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;

	_enter_critical_bh(&pmlmepriv->lock, &irqL);
	_rtw_roaming(padapter, tgt_network);
	_exit_critical_bh(&pmlmepriv->lock, &irqL);
}
void _rtw_roaming(_adapter *padapter, struct wlan_network *tgt_network)
{
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	int do_join_r;

	struct wlan_network *pnetwork;

	if(tgt_network != NULL)
		pnetwork = tgt_network;
	else
		pnetwork = &pmlmepriv->cur_network;
	
	if(0 < pmlmepriv->to_roaming) {
		DBG_871X("roaming from %s("MAC_FMT"), length:%d\n",
				pnetwork->network.Ssid.Ssid, MAC_ARG(pnetwork->network.MacAddress),
				pnetwork->network.Ssid.SsidLength);
		_rtw_memcpy(&pmlmepriv->assoc_ssid, &pnetwork->network.Ssid, sizeof(NDIS_802_11_SSID));

		pmlmepriv->assoc_by_bssid = _FALSE;

		while(1) {
			if( _SUCCESS==(do_join_r=rtw_do_join(padapter)) ) {
				break;
			} else {
				DBG_871X("roaming do_join return %d\n", do_join_r);
				pmlmepriv->to_roaming--;
				
				if(0< pmlmepriv->to_roaming) {
					continue;
				} else {
					DBG_871X("%s(%d) -to roaming fail, indicate_disconnect\n", __FUNCTION__,__LINE__);
					rtw_indicate_disconnect(padapter);
					break;
				}
			}
		}
	}
	
}
#endif

