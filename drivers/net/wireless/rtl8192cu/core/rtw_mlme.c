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
#include <hal_intf.h>
#include <mlme_osdep.h>
#include <sta_info.h>
#include <wifi.h>
#include <wlan_bssdef.h>
#include <rtw_ioctl_set.h>

extern void indicate_wx_scan_complete_event(_adapter *padapter);
extern u8 rtw_do_join(_adapter * padapter);

#ifdef CONFIG_DISABLE_MCS13TO15
extern unsigned char	MCS_rate_2R_MCS13TO15_OFF[16];
extern unsigned char	MCS_rate_2R[16];
#else //CONFIG_DISABLE_MCS13TO15
extern unsigned char	MCS_rate_2R[16];
#endif //CONFIG_DISABLE_MCS13TO15
extern unsigned char	MCS_rate_1R[16];

sint	_rtw_init_mlme_priv (_adapter* padapter)
{
	sint	i;
	u8	*pbuf;
	struct wlan_network	*pnetwork;
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	sint	res = _SUCCESS;

_func_enter_;

	// We don't need to memset padapter->XXX to zero, because adapter is allocated by rtw_zvmalloc().
	//_rtw_memset((u8 *)pmlmepriv, 0, sizeof(struct mlme_priv));

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

	rtw_clear_scan_deny(padapter);

	rtw_init_mlme_timer(padapter);

exit:

_func_exit_;

	return res;
}	

void rtw_mfree_mlme_priv_lock (struct mlme_priv *pmlmepriv);
void rtw_mfree_mlme_priv_lock (struct mlme_priv *pmlmepriv)
{
	_rtw_spinlock_free(&pmlmepriv->lock);
	_rtw_spinlock_free(&(pmlmepriv->free_bss_pool.lock));
	_rtw_spinlock_free(&(pmlmepriv->scanned_queue.lock));
}

static void rtw_free_mlme_ie_data(u8 **ppie, u32 *plen)
{
	if(*ppie)
	{		
		rtw_mfree(*ppie, *plen);
		*plen = 0;
		*ppie=NULL;
	}	
}

void rtw_free_mlme_priv_ie_data(struct mlme_priv *pmlmepriv)
{
#if defined (CONFIG_AP_MODE) && defined (CONFIG_NATIVEAP_MLME)
	rtw_buf_free(&pmlmepriv->assoc_req, &pmlmepriv->assoc_req_len);
	rtw_buf_free(&pmlmepriv->assoc_rsp, &pmlmepriv->assoc_rsp_len);
	rtw_free_mlme_ie_data(&pmlmepriv->wps_beacon_ie, &pmlmepriv->wps_beacon_ie_len);
	rtw_free_mlme_ie_data(&pmlmepriv->wps_probe_req_ie, &pmlmepriv->wps_probe_req_ie_len);
	rtw_free_mlme_ie_data(&pmlmepriv->wps_probe_resp_ie, &pmlmepriv->wps_probe_resp_ie_len);
	rtw_free_mlme_ie_data(&pmlmepriv->wps_assoc_resp_ie, &pmlmepriv->wps_assoc_resp_ie_len);
	
	rtw_free_mlme_ie_data(&pmlmepriv->p2p_beacon_ie, &pmlmepriv->p2p_beacon_ie_len);
	rtw_free_mlme_ie_data(&pmlmepriv->p2p_probe_req_ie, &pmlmepriv->p2p_probe_req_ie_len);
	rtw_free_mlme_ie_data(&pmlmepriv->p2p_probe_resp_ie, &pmlmepriv->p2p_probe_resp_ie_len);
	rtw_free_mlme_ie_data(&pmlmepriv->p2p_go_probe_resp_ie, &pmlmepriv->p2p_go_probe_resp_ie_len);
	rtw_free_mlme_ie_data(&pmlmepriv->p2p_assoc_req_ie, &pmlmepriv->p2p_assoc_req_ie_len);
#endif

#if defined(CONFIG_WFD) && defined(CONFIG_IOCTL_CFG80211)	
	rtw_free_mlme_ie_data(&pmlmepriv->wfd_beacon_ie, &pmlmepriv->wfd_beacon_ie_len);
	rtw_free_mlme_ie_data(&pmlmepriv->wfd_probe_req_ie, &pmlmepriv->wfd_probe_req_ie_len);
	rtw_free_mlme_ie_data(&pmlmepriv->wfd_probe_resp_ie, &pmlmepriv->wfd_probe_resp_ie_len);
	rtw_free_mlme_ie_data(&pmlmepriv->wfd_go_probe_resp_ie, &pmlmepriv->wfd_go_probe_resp_ie_len);
	rtw_free_mlme_ie_data(&pmlmepriv->wfd_assoc_req_ie, &pmlmepriv->wfd_assoc_req_ie_len);
#endif

}

void _rtw_free_mlme_priv (struct mlme_priv *pmlmepriv)
{
_func_enter_;

	rtw_free_mlme_priv_ie_data(pmlmepriv);

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

#ifdef PLATFORM_FREEBSD
        //i think needs to check again
		delta_time = (curr_time -pnetwork->last_scanned)/hz;	

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

	//_irqL irqL;
	_list	*phead, *plist;
	struct	wlan_network *pnetwork = NULL;
	u8 zero_addr[ETH_ALEN] = {0,0,0,0,0,0};
	
_func_enter_;	

	if(_rtw_memcmp(zero_addr, addr, ETH_ALEN)){
		pnetwork=NULL;
		goto exit;
	}
	
	//_enter_critical_bh(&scanned_queue->lock, &irqL);
	
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

	//_exit_critical_bh(&scanned_queue->lock, &irqL);
	
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

int	rtw_enqueue_network(_queue *queue, struct wlan_network *pnetwork);
int	rtw_enqueue_network(_queue *queue, struct wlan_network *pnetwork)
{
	int	res;
_func_enter_;		
	res = _rtw_enqueue_network(queue, pnetwork);
_func_exit_;		
	return res;
}


#ifndef PLATFORM_FREEBSD //Baron
static struct	wlan_network *rtw_dequeue_network(_queue *queue)
{
	struct wlan_network *pnetwork;
_func_enter_;		
	pnetwork = _rtw_dequeue_network(queue);
_func_exit_;		
	return pnetwork;
}
#endif //PLATFORM_FREEBSD

struct	wlan_network *rtw_alloc_network(struct	mlme_priv *pmlmepriv );
struct	wlan_network *rtw_alloc_network(struct	mlme_priv *pmlmepriv )//(_queue	*free_queue)
{
	struct	wlan_network	*pnetwork;
_func_enter_;			
	pnetwork = _rtw_alloc_network(pmlmepriv);
_func_exit_;			
	return pnetwork;
}

void rtw_free_network(struct mlme_priv *pmlmepriv, struct	wlan_network *pnetwork, u8 is_freeall);
void rtw_free_network(struct mlme_priv *pmlmepriv, struct	wlan_network *pnetwork, u8 is_freeall)//(struct	wlan_network *pnetwork, _queue	*free_queue)
{
_func_enter_;		
	RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("rtw_free_network==> ssid = %s \n\n" , pnetwork->network.Ssid.Ssid));
	_rtw_free_network(pmlmepriv, pnetwork, is_freeall);
_func_exit_;		
}

void rtw_free_network_nolock(struct mlme_priv *pmlmepriv, struct wlan_network *pnetwork );
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

inline int is_same_ess(WLAN_BSSID_EX *a, WLAN_BSSID_EX *b);
inline int is_same_ess(WLAN_BSSID_EX *a, WLAN_BSSID_EX *b)
{
	//RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("(%s,%d)(%s,%d)\n",
	//		a->Ssid.Ssid,a->Ssid.SsidLength,b->Ssid.Ssid,b->Ssid.SsidLength));
	return (a->Ssid.SsidLength == b->Ssid.SsidLength) 
		&&  _rtw_memcmp(a->Ssid.Ssid, b->Ssid.Ssid, a->Ssid.SsidLength)==_TRUE;
}

int is_same_network(WLAN_BSSID_EX *src, WLAN_BSSID_EX *dst)
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
		//	(src->Configuration.DSConfig == dst->Configuration.DSConfig) &&
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

static void update_network(WLAN_BSSID_EX *dst, WLAN_BSSID_EX *src,
	_adapter * padapter, bool update_ie)
{
	u8 ss_ori = dst->PhyInfo.SignalStrength;
	u8 sq_ori = dst->PhyInfo.SignalQuality;
	long rssi_ori = dst->Rssi;

	u8 ss_smp = src->PhyInfo.SignalStrength;
	u8 sq_smp = src->PhyInfo.SignalQuality;
	long rssi_smp = src->Rssi;

	u8 ss_final;
	u8 sq_final;
	long rssi_final;

_func_enter_;		

#ifdef CONFIG_ANTENNA_DIVERSITY
	rtw_hal_antdiv_rssi_compared(padapter, dst, src); //this will update src.Rssi, need consider again
#endif

	#if defined(DBG_RX_SIGNAL_DISPLAY_SSID_MONITORED) && 1
	if(strcmp(dst->Ssid.Ssid, DBG_RX_SIGNAL_DISPLAY_SSID_MONITORED) == 0) {
		DBG_871X(FUNC_ADPT_FMT" %s("MAC_FMT", ch%u) ss_ori:%3u, sq_ori:%3u, rssi_ori:%3ld, ss_smp:%3u, sq_smp:%3u, rssi_smp:%3ld\n"
			, FUNC_ADPT_ARG(padapter)
			, src->Ssid.Ssid, MAC_ARG(src->MacAddress), src->Configuration.DSConfig
			,ss_ori, sq_ori, rssi_ori
			,ss_smp, sq_smp, rssi_smp
		);
	}
	#endif

	/* The rule below is 1/5 for sample value, 4/5 for history value */
	if (check_fwstate(&padapter->mlmepriv, _FW_LINKED) && is_same_network(&(padapter->mlmepriv.cur_network.network), src)) {
		/* Take the recvpriv's value for the connected AP*/
		ss_final = padapter->recvpriv.signal_strength;
		sq_final = padapter->recvpriv.signal_qual;
		/* the rssi value here is undecorated, and will be used for antenna diversity */
		if(sq_smp != 101) /* from the right channel */
			rssi_final = (src->Rssi+dst->Rssi*4)/5;
		else
			rssi_final = rssi_ori;
	}
	else {
		if(sq_smp != 101) { /* from the right channel */
			ss_final = ((u32)(src->PhyInfo.SignalStrength)+(u32)(dst->PhyInfo.SignalStrength)*4)/5;
			sq_final = ((u32)(src->PhyInfo.SignalQuality)+(u32)(dst->PhyInfo.SignalQuality)*4)/5;
			rssi_final = (src->Rssi+dst->Rssi*4)/5;
		} else {
			/* bss info not receving from the right channel, use the original RX signal infos */
			ss_final = dst->PhyInfo.SignalStrength;
			sq_final = dst->PhyInfo.SignalQuality;
			rssi_final = dst->Rssi;
		}
		
	}

	if (update_ie)
		_rtw_memcpy((u8 *)dst, (u8 *)src, get_WLAN_BSSID_EX_sz(src));

	dst->PhyInfo.SignalStrength = ss_final;
	dst->PhyInfo.SignalQuality = sq_final;
	dst->Rssi = rssi_final;

	#if defined(DBG_RX_SIGNAL_DISPLAY_SSID_MONITORED) && 1
	if(strcmp(dst->Ssid.Ssid, DBG_RX_SIGNAL_DISPLAY_SSID_MONITORED) == 0) {
		DBG_871X(FUNC_ADPT_FMT" %s("MAC_FMT"), SignalStrength:%u, SignalQuality:%u, RawRSSI:%ld\n"
			, FUNC_ADPT_ARG(padapter)
			, dst->Ssid.Ssid, MAC_ARG(dst->MacAddress), dst->PhyInfo.SignalStrength, dst->PhyInfo.SignalQuality, dst->Rssi);
	}
	#endif

#if 0 // old codes, may be useful one day...
//	DBG_871X("update_network: rssi=0x%lx dst->Rssi=%d ,dst->Rssi=0x%lx , src->Rssi=0x%lx",(dst->Rssi+src->Rssi)/2,dst->Rssi,dst->Rssi,src->Rssi);
	if (check_fwstate(&padapter->mlmepriv, _FW_LINKED) && is_same_network(&(padapter->mlmepriv.cur_network.network), src))
	{
	
		//DBG_871X("b:ssid=%s update_network: src->rssi=0x%d padapter->recvpriv.ui_rssi=%d\n",src->Ssid.Ssid,src->Rssi,padapter->recvpriv.signal);
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

		//DBG_871X("Total SQ=%d  pattrib->signal_qual= %d\n", padapter->recvpriv.signal_qual_data.total_val, src->Rssi);

		// <1> Showed on UI for user,in percentage.
		tmpVal = padapter->recvpriv.signal_qual_data.total_val/padapter->recvpriv.signal_qual_data.total_num;
                padapter->recvpriv.signal=(u8)tmpVal;//Link quality

		src->Rssi= translate_percentage_to_dbm(padapter->recvpriv.signal) ;
	}
	else{
//	DBG_871X("ELSE:ssid=%s update_network: src->rssi=0x%d dst->rssi=%d\n",src->Ssid.Ssid,src->Rssi,dst->Rssi);
		src->Rssi=(src->Rssi +dst->Rssi)/2;//dBM
	}	

//	DBG_871X("a:update_network: src->rssi=0x%d padapter->recvpriv.ui_rssi=%d\n",src->Rssi,padapter->recvpriv.signal);

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
			update_network(&(pmlmepriv->cur_network.network), pnetwork,adapter, _TRUE);
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
	_irqL irqL;
	_list	*plist, *phead;
	ULONG	bssid_ex_sz;
	struct mlme_priv	*pmlmepriv = &(adapter->mlmepriv);
	_queue	*queue	= &(pmlmepriv->scanned_queue);
	struct wlan_network	*pnetwork = NULL;
	struct wlan_network	*oldest = NULL;

_func_enter_;

	_enter_critical_bh(&queue->lock, &irqL);
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
			rtw_hal_get_def_var(adapter, HAL_DEF_CURRENT_ANTENNA, &(target->PhyInfo.Optimum_antenna));
#endif
			_rtw_memcpy(&(pnetwork->network), target,  get_WLAN_BSSID_EX_sz(target));
			//pnetwork->last_scanned = rtw_get_current_time();
			// variable initialize
			pnetwork->fixed = _FALSE;
			pnetwork->last_scanned = rtw_get_current_time();

			pnetwork->network_type = 0;	
			pnetwork->aid=0;		
			pnetwork->join_res=0;

			/* bss info not receving from the right channel */
			if (pnetwork->network.PhyInfo.SignalQuality == 101)
				pnetwork->network.PhyInfo.SignalQuality = 0;
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
			rtw_hal_get_def_var(adapter, HAL_DEF_CURRENT_ANTENNA, &(target->PhyInfo.Optimum_antenna));
#endif
			_rtw_memcpy(&(pnetwork->network), target, bssid_ex_sz );

			pnetwork->last_scanned = rtw_get_current_time();

			/* bss info not receving from the right channel */
			if (pnetwork->network.PhyInfo.SignalQuality == 101)
				pnetwork->network.PhyInfo.SignalQuality = 0;

			rtw_list_insert_tail(&(pnetwork->list),&(queue->queue)); 

		}
	}
	else {
		/* we have an entry and we are going to update it. But this entry may
		 * be already expired. In this case we do the same as we found a new 
		 * net and call the new_net handler
		 */
		bool update_ie = _TRUE;

		pnetwork->last_scanned = rtw_get_current_time();

		//target.Reserved[0]==1, means that scaned network is a bcn frame.
		if((pnetwork->network.IELength>target->IELength) && (target->Reserved[0]==1))
			update_ie = _FALSE;

		update_network(&(pnetwork->network), target,adapter, update_ie);
	}

exit:
	_exit_critical_bh(&queue->lock, &irqL);

_func_exit_;
}

void rtw_add_network(_adapter *adapter, WLAN_BSSID_EX *pnetwork);
void rtw_add_network(_adapter *adapter, WLAN_BSSID_EX *pnetwork)
{
	_irqL irqL;
	struct	mlme_priv	*pmlmepriv = &(((_adapter *)adapter)->mlmepriv);
	//_queue	*queue	= &(pmlmepriv->scanned_queue);

_func_enter_;		

	//_enter_critical_bh(&queue->lock, &irqL);

	#if defined(CONFIG_P2P) && defined(CONFIG_P2P_REMOVE_GROUP_INFO)
	rtw_WLAN_BSSID_EX_remove_p2p_attr(pnetwork, P2P_ATTR_GROUP_INFO);
	#endif
	
	update_current_network(adapter, pnetwork);
	
	rtw_update_scanned_network(adapter, pnetwork);

	//_exit_critical_bh(&queue->lock, &irqL);
	
_func_exit_;		
}

//select the desired network based on the capability of the (i)bss.
// check items: (1) security
//			   (2) network_type
//			   (3) WMM
//			   (4) HT
//                     (5) others
int rtw_is_desired_network(_adapter *adapter, struct wlan_network *pnetwork);
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

	if(check_fwstate(pmlmepriv, WIFI_UNDER_WPS))
	{
		if(rtw_get_wps_ie(pnetwork->network.IEs+_FIXED_IE_LENGTH_, pnetwork->network.IELength-_FIXED_IE_LENGTH_, NULL, &wps_ielen)!=NULL)
		{
			return _TRUE;
		}
		else
		{	
			return _FALSE;
		}	
	}
	if (adapter->registrypriv.wifi_spec == 1) //for  correct flow of 8021X  to do....
	{
		u8 *p=NULL;
		uint ie_len=0;

		if ((desired_encmode == Ndis802_11EncryptionDisabled) && (privacy != 0))
	            bselected = _FALSE;

		if ( psecuritypriv->ndisauthtype == Ndis802_11AuthModeWPA2PSK) {
			p = rtw_get_ie(pnetwork->network.IEs + _BEACON_IE_OFFSET_, _RSN_IE_2_, &ie_len, (pnetwork->network.IELength - _BEACON_IE_OFFSET_));
			if (p && ie_len>0) {
				bselected = _TRUE;
			} else {
				bselected = _FALSE;
			}
		}
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
		return;
	}


	_enter_critical_bh(&pmlmepriv->lock, &irqL);

	// update IBSS_network 's timestamp
	if ((check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE)) == _TRUE)
	{
		//RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,"rtw_survey_event_callback : WIFI_ADHOC_MASTER_STATE \n\n");
		if(_rtw_memcmp(&(pmlmepriv->cur_network.network.MacAddress), pnetwork->MacAddress, ETH_ALEN))
		{
			struct wlan_network* ibss_wlan = NULL;
			_irqL	irqL;
			
			_rtw_memcpy(pmlmepriv->cur_network.network.IEs, pnetwork->IEs, 8);
			_enter_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);
			ibss_wlan = rtw_find_network(&pmlmepriv->scanned_queue,  pnetwork->MacAddress);
			if(ibss_wlan)
			{
				_rtw_memcpy(ibss_wlan->network.IEs , pnetwork->IEs, 8);			
				_exit_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);		
				goto exit;
			}
			_exit_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);
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
	u8 timer_cancelled = _FALSE;
	struct	mlme_priv	*pmlmepriv = &(adapter->mlmepriv);
	
#ifdef CONFIG_MLME_EXT	

	mlmeext_surveydone_event_callback(adapter);

#endif

_func_enter_;			

	_enter_critical_bh(&pmlmepriv->lock, &irqL);
	if(pmlmepriv->wps_probe_req_ie)
	{
		u32 free_len = pmlmepriv->wps_probe_req_ie_len;
		pmlmepriv->wps_probe_req_ie_len = 0;
		rtw_mfree(pmlmepriv->wps_probe_req_ie, free_len);
		pmlmepriv->wps_probe_req_ie = NULL;			
	}
	
	RT_TRACE(_module_rtl871x_mlme_c_,_drv_info_,("rtw_surveydone_event_callback: fw_state:%x\n\n", get_fwstate(pmlmepriv)));
	
	if (check_fwstate(pmlmepriv,_FW_UNDER_SURVEY))
	{
		//u8 timer_cancelled;

		timer_cancelled = _TRUE;
		//_cancel_timer(&pmlmepriv->scan_to_timer, &timer_cancelled);
		
		_clr_fwstate_(pmlmepriv, _FW_UNDER_SURVEY);
	}
	else {
	
		RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("nic status =%x, survey done event comes too late!\n", get_fwstate(pmlmepriv)));	
	}
	_exit_critical_bh(&pmlmepriv->lock, &irqL);

	if(timer_cancelled)
		_cancel_timer(&pmlmepriv->scan_to_timer, &timer_cancelled);


	_enter_critical_bh(&pmlmepriv->lock, &irqL);

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
			int s_ret;
			set_fwstate(pmlmepriv, _FW_UNDER_LINKING);
			pmlmepriv->to_join = _FALSE;
			if(_SUCCESS == (s_ret=rtw_select_and_join_from_scanned_queue(pmlmepriv)))
			{
	     		     _set_timer(&pmlmepriv->assoc_timer, MAX_JOIN_TIMEOUT);	 
			}
			else if(s_ret == 2)//there is no need to wait for join
			{
				_clr_fwstate_(pmlmepriv, _FW_UNDER_LINKING);
				rtw_indicate_connect(adapter);
			}
			else
			{
				DBG_871X("try_to_join, but select scanning queue fail, to_roaming:%d\n", rtw_to_roaming(adapter));
				#ifdef CONFIG_LAYER2_ROAMING
				if (rtw_to_roaming(adapter) != 0) {
					if( --pmlmepriv->to_roaming == 0
						|| _SUCCESS != rtw_sitesurvey_cmd(adapter, &pmlmepriv->assoc_ssid, 1, NULL, 0)
					) {
						rtw_set_roaming(adapter, 0);
#ifdef CONFIG_INTEL_WIDI
						if(adapter->mlmepriv.widi_state == INTEL_WIDI_STATE_ROAMING)
						{
							_rtw_memset(pmlmepriv->sa_ext, 0x00, L2SDTA_SERVICE_VE_LEN);
							intel_widi_wk_cmd(adapter, INTEL_WIDI_LISTEN_WK, NULL);
							DBG_871X("change to widi listen\n");
						}
#endif // CONFIG_INTEL_WIDI
						rtw_free_assoc_resources(adapter, 1);
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

	indicate_wx_scan_complete_event(adapter);
	//DBG_871X("scan complete in %dms\n",rtw_get_passing_time_ms(pmlmepriv->scan_start_time));

	_exit_critical_bh(&pmlmepriv->lock, &irqL);

#ifdef CONFIG_P2P_PS
	if (check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE) {
		p2p_ps_wk_cmd(adapter, P2P_PS_SCAN_DONE, 0);
	}
#endif // CONFIG_P2P_PS

	rtw_os_xmit_schedule(adapter);
#ifdef CONFIG_CONCURRENT_MODE
	rtw_os_xmit_schedule(adapter->pbuddy_adapter);
#endif
#ifdef CONFIG_DUALMAC_CONCURRENT
	dc_resume_xmit(adapter);
#endif

#ifdef CONFIG_DRVEXT_MODULE_WSC
	drvext_surveydone_callback(&adapter->drvextpriv);
#endif

#ifdef DBG_CONFIG_ERROR_DETECT
#ifdef CONFIG_INTEL_WIDI
	if (adapter->mlmepriv.widi_state == INTEL_WIDI_STATE_NONE)
#endif
	{
		struct mlme_ext_priv *pmlmeext = &adapter->mlmeextpriv;		
		if(pmlmeext->sitesurvey_res.bss_cnt == 0){
			rtw_hal_sreset_reset(adapter);
		}
	}
#endif

#ifdef CONFIG_IOCTL_CFG80211
	rtw_cfg80211_surveydone_event_callback(adapter);
#endif //CONFIG_IOCTL_CFG80211

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
	_irqL irqL, irqL0;
	_queue *free_queue = &pmlmepriv->free_bss_pool;
	_queue *scan_queue = &pmlmepriv->scanned_queue;
	_list	*plist, *phead, *ptemp;
	
_func_enter_;		
	
	RT_TRACE(_module_rtl871x_mlme_c_, _drv_notice_, ("+free_scanqueue\n"));
	_enter_critical_bh(&scan_queue->lock, &irqL0);
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
	_exit_critical_bh(&scan_queue->lock, &irqL0);
	
_func_exit_;
}
	
/*
*rtw_free_assoc_resources: the caller has to lock pmlmepriv->lock
*/
void rtw_free_assoc_resources(_adapter *adapter, int lock_scanned_queue)
{
	_irqL irqL;
	struct wlan_network* pwlan = NULL;
     	struct	mlme_priv *pmlmepriv = &adapter->mlmepriv;
   	struct	sta_priv *pstapriv = &adapter->stapriv;
	struct wlan_network *tgt_network = &pmlmepriv->cur_network;
	
#ifdef CONFIG_TDLS
	struct tdls_info *ptdlsinfo = &adapter->tdlsinfo;
#endif //CONFIG_TDLS	
_func_enter_;			

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_notice_, ("+rtw_free_assoc_resources\n"));
	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_, ("tgt_network->network.MacAddress="MAC_FMT" ssid=%s\n",
		MAC_ARG(tgt_network->network.MacAddress), tgt_network->network.Ssid.Ssid));

	if(check_fwstate( pmlmepriv, WIFI_STATION_STATE|WIFI_AP_STATE))
	{
		struct sta_info* psta;
		
		psta = rtw_get_stainfo(&adapter->stapriv, tgt_network->network.MacAddress);

#ifdef CONFIG_TDLS
		if(ptdlsinfo->setup_state != TDLS_STATE_NONE)
		{
			rtw_tdls_cmd(adapter, myid(&(adapter->eeprompriv)), TDLS_RS_RCR);
			rtw_reset_tdls_info(adapter);
			rtw_free_all_stainfo(adapter);
			_enter_critical_bh(&(pstapriv->sta_hash_lock), &irqL);
		}
		else
#endif //CONFIG_TDLS
		{
			_enter_critical_bh(&(pstapriv->sta_hash_lock), &irqL);
			rtw_free_stainfo(adapter,  psta);
		}

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

	if(lock_scanned_queue)
		_enter_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);
	
	pwlan = rtw_find_network(&pmlmepriv->scanned_queue, tgt_network->network.MacAddress);
	if(pwlan)
	{
		pwlan->fixed = _FALSE;
#ifdef CONFIG_P2P
		if(!rtw_p2p_chk_state(&adapter->wdinfo, P2P_STATE_NONE))
		{
			u32 p2p_ielen=0;
			u8  *p2p_ie;
			//u16 capability;
			u8 *pcap = NULL;
			u32 capability_len=0;

			//DBG_871X("free disconnecting network\n");
			//rtw_free_network_nolock(pmlmepriv, pwlan);

			if((p2p_ie=rtw_get_p2p_ie(pwlan->network.IEs+_FIXED_IE_LENGTH_, pwlan->network.IELength-_FIXED_IE_LENGTH_, NULL, &p2p_ielen)))
			{
				pcap = rtw_get_p2p_attr_content(p2p_ie, p2p_ielen, P2P_ATTR_CAPABILITY, NULL, &capability_len);
				if(pcap && capability_len==2)
				{
					u16 cap = *(u16*)pcap ;
					*(u16*)pcap = cap&0x00ff;//clear group capability when free this network
				}
			}

			rtw_set_scan_deny(adapter, 2000);
			//rtw_clear_scan_deny(adapter);
		}
#endif //CONFIG_P2P
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

	if(lock_scanned_queue)
		_exit_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);
	
	adapter->securitypriv.key_mask = 0;

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

	if(!check_fwstate(&padapter->mlmepriv, _FW_LINKED)) 
	{

#ifdef CONFIG_SW_ANTENNA_DIVERSITY
	rtw_hal_set_hwreg(padapter, HW_VAR_ANTENNA_DIVERSITY_LINK, 0);
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

	}

	rtw_set_roaming(padapter, 0);

#ifdef CONFIG_INTEL_WIDI
	if(padapter->mlmepriv.widi_state == INTEL_WIDI_STATE_ROAMING)
	{
		_rtw_memset(pmlmepriv->sa_ext, 0x00, L2SDTA_SERVICE_VE_LEN);
		intel_widi_wk_cmd(padapter, INTEL_WIDI_LISTEN_WK, NULL);
		DBG_871X("change to widi listen\n");
	}
#endif // CONFIG_INTEL_WIDI

	rtw_set_scan_deny(padapter, 3000);

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_, ("-rtw_indicate_connect: fw_state=0x%08x\n", get_fwstate(pmlmepriv)));
 
_func_exit_;

}


/*
*rtw_indicate_disconnect: the caller has to lock pmlmepriv->lock
*/
void rtw_indicate_disconnect( _adapter *padapter )
{
	struct	mlme_priv *pmlmepriv = &padapter->mlmepriv;	

_func_enter_;	
	
	RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_, ("+rtw_indicate_disconnect\n"));

	_clr_fwstate_(pmlmepriv, _FW_UNDER_LINKING|WIFI_UNDER_WPS);

	if(rtw_to_roaming(padapter) > 0)
		_clr_fwstate_(pmlmepriv, _FW_LINKED);

	if(check_fwstate(&padapter->mlmepriv, _FW_LINKED) 
		|| (rtw_to_roaming(padapter) <= 0)
	)
	{
		rtw_os_indicate_disconnect(padapter);

		//set ips_deny_time to avoid enter IPS before LPS leave
		padapter->pwrctrlpriv.ips_deny_time = rtw_get_current_time() + rtw_ms_to_systime(3000);

	      _clr_fwstate_(pmlmepriv, _FW_LINKED);

		rtw_led_control(padapter, LED_CTL_NO_LINK);

		rtw_clear_scan_deny(padapter);

	}

#ifdef CONFIG_P2P_PS
	p2p_ps_wk_cmd(padapter, P2P_PS_DISABLE, 1);
#endif // CONFIG_P2P_PS

#ifdef CONFIG_LPS
#ifdef CONFIG_WOWLAN
	if(padapter->pwrctrlpriv.wowlan_mode==_FALSE)
#endif //CONFIG_WOWLAN
	rtw_lps_ctrl_wk_cmd(padapter, LPS_CTRL_DISCONNECT, 1);

#endif

_func_exit_;	
}

inline void rtw_indicate_scan_done( _adapter *padapter, bool aborted)
{
	rtw_os_indicate_scan_done(padapter, aborted);
}

void rtw_scan_abort(_adapter *adapter)
{
	u32 cnt=0;
	u32 start;
	struct mlme_priv	*pmlmepriv = &(adapter->mlmepriv);
	struct mlme_ext_priv	*pmlmeext = &(adapter->mlmeextpriv);

	start = rtw_get_current_time();
	pmlmeext->scan_abort = _TRUE;
	while (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY)
		&& rtw_get_passing_time_ms(start) <= 200) {

		if (adapter->bDriverStopped || adapter->bSurpriseRemoved)
			break;

		DBG_871X(FUNC_NDEV_FMT"fw_state=_FW_UNDER_SURVEY!\n", FUNC_NDEV_ARG(adapter->pnetdev));
		rtw_msleep_os(20);
	}

	if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY)) {
		if (!adapter->bDriverStopped && !adapter->bSurpriseRemoved)
			DBG_871X(FUNC_NDEV_FMT"waiting for scan_abort time out!\n", FUNC_NDEV_ARG(adapter->pnetdev));
		#ifdef CONFIG_PLATFORM_MSTAR
		//_clr_fwstate_(pmlmepriv, _FW_UNDER_SURVEY);
		set_survey_timer(pmlmeext, 0);
		_set_timer(&pmlmepriv->scan_to_timer, 50);
		#endif
		rtw_indicate_scan_done(adapter, _TRUE);
	}
	pmlmeext->scan_abort = _FALSE;
}

static struct sta_info *rtw_joinbss_update_stainfo(_adapter *padapter, struct wlan_network *pnetwork)
{
	int i;
	struct sta_info *bmc_sta, *psta=NULL;
	struct recv_reorder_ctrl *preorder_ctrl;
	struct sta_priv *pstapriv = &padapter->stapriv;	
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;

	psta = rtw_get_stainfo(pstapriv, pnetwork->network.MacAddress);
	if(psta==NULL) {
		psta = rtw_alloc_stainfo(pstapriv, pnetwork->network.MacAddress);
	}

	if(psta) //update ptarget_sta
	{
		DBG_871X("%s\n", __FUNCTION__);
	
		psta->aid  = pnetwork->join_res;
#ifdef CONFIG_CONCURRENT_MODE	

		if(PRIMARY_ADAPTER == padapter->adapter_type)
			psta->mac_id=0;
		else
			psta->mac_id=2;
#else
		psta->mac_id=0;
#endif

		psta->raid = networktype_to_raid(pmlmeext->cur_wireless_mode);

		//security related
		if(padapter->securitypriv.dot11AuthAlgrthm== dot11AuthAlgrthm_8021X)
		{						
			padapter->securitypriv.binstallGrpkey=_FALSE;
			padapter->securitypriv.busetkipkey=_FALSE;						
			padapter->securitypriv.bgrpkey_handshake=_FALSE;

			psta->ieee8021x_blocked=_TRUE;
			psta->dot118021XPrivacy=padapter->securitypriv.dot11PrivacyAlgrthm;
						
			_rtw_memset((u8 *)&psta->dot118021x_UncstKey, 0, sizeof (union Keytype));
						
			_rtw_memset((u8 *)&psta->dot11tkiprxmickey, 0, sizeof (union Keytype));
			_rtw_memset((u8 *)&psta->dot11tkiptxmickey, 0, sizeof (union Keytype));
						
			_rtw_memset((u8 *)&psta->dot11txpn, 0, sizeof (union pn48));
#ifdef CONFIG_IEEE80211W
			_rtw_memset((u8 *)&psta->dot11wtxpn, 0, sizeof (union pn48));
#endif //CONFIG_IEEE80211W
			_rtw_memset((u8 *)&psta->dot11rxpn, 0, sizeof (union pn48));	
		}

		//	Commented by Albert 2012/07/21
		//	When doing the WPS, the wps_ie_len won't equal to 0
		//	And the Wi-Fi driver shouldn't allow the data packet to be tramsmitted.
		if ( padapter->securitypriv.wps_ie_len != 0 )
		{
			psta->ieee8021x_blocked=_TRUE;
			padapter->securitypriv.wps_ie_len = 0;
		}


		//for A-MPDU Rx reordering buffer control for bmc_sta & sta_info
		//if A-MPDU Rx is enabled, reseting  rx_ordering_ctrl wstart_b(indicate_seq) to default value=0xffff
		//todo: check if AP can send A-MPDU packets
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

	
		//misc.
		update_sta_info(padapter, psta);
		
	}
					
	return psta;
	
}

//pnetwork : returns from rtw_joinbss_event_callback
//ptarget_wlan: found from scanned_queue
static void rtw_joinbss_update_network(_adapter *padapter, struct wlan_network *ptarget_wlan, struct wlan_network  *pnetwork)
{
	struct mlme_priv	*pmlmepriv = &(padapter->mlmepriv);	
	struct wlan_network  *cur_network = &(pmlmepriv->cur_network);

	DBG_871X("%s\n", __FUNCTION__);
	
	RT_TRACE(_module_rtl871x_mlme_c_,_drv_info_,("\nfw_state:%x, BSSID:"MAC_FMT"\n"
		,get_fwstate(pmlmepriv), MAC_ARG(pnetwork->network.MacAddress)));

				
	// why not use ptarget_wlan??
	_rtw_memcpy(&cur_network->network, &pnetwork->network, pnetwork->network.Length);

	cur_network->aid = pnetwork->join_res;

				
#ifdef CONFIG_NEW_SIGNAL_STAT_PROCESS
	rtw_set_signal_stat_timer(&padapter->recvpriv);
#endif
	padapter->recvpriv.signal_strength = ptarget_wlan->network.PhyInfo.SignalStrength;
	padapter->recvpriv.signal_qual = ptarget_wlan->network.PhyInfo.SignalQuality;
	//the ptarget_wlan->network.Rssi is raw data, we use ptarget_wlan->network.PhyInfo.SignalStrength instead (has scaled)
	padapter->recvpriv.rssi = translate_percentage_to_dbm(ptarget_wlan->network.PhyInfo.SignalStrength);
	#if defined(DBG_RX_SIGNAL_DISPLAY_PROCESSING) && 1
		DBG_871X(FUNC_ADPT_FMT" signal_strength:%3u, rssi:%3d, signal_qual:%3u"
			"\n"
			, FUNC_ADPT_ARG(padapter)
			, padapter->recvpriv.signal_strength
			, padapter->recvpriv.rssi
			, padapter->recvpriv.signal_qual
	);
	#endif
#ifdef CONFIG_NEW_SIGNAL_STAT_PROCESS
	rtw_set_signal_stat_timer(&padapter->recvpriv);
#endif
				
	//update fw_state //will clr _FW_UNDER_LINKING here indirectly
	switch(pnetwork->network.InfrastructureMode)
	{	
		case Ndis802_11Infrastructure:						
			
				if(pmlmepriv->fw_state&WIFI_UNDER_WPS)
					pmlmepriv->fw_state = WIFI_STATION_STATE|WIFI_UNDER_WPS;
				else
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

	rtw_update_protection(padapter, (cur_network->network.IEs) + sizeof (NDIS_802_11_FIXED_IEs), 
									(cur_network->network.IELength));
			
#ifdef CONFIG_80211N_HT			
	rtw_update_ht_cap(padapter, cur_network->network.IEs, cur_network->network.IELength, (u8) cur_network->network.Configuration.DSConfig);
#endif


}

//Notes: the fucntion could be > passive_level (the same context as Rx tasklet)
//pnetwork : returns from rtw_joinbss_event_callback
//ptarget_wlan: found from scanned_queue
//if join_res > 0, for (fw_state==WIFI_STATION_STATE), we check if  "ptarget_sta" & "ptarget_wlan" exist.	
//if join_res > 0, for (fw_state==WIFI_ADHOC_STATE), we only check if "ptarget_wlan" exist.
//if join_res > 0, update "cur_network->network" from "pnetwork->network" if (ptarget_wlan !=NULL).
//
//#define REJOIN
void rtw_joinbss_event_prehandle(_adapter *adapter, u8 *pbuf)
{
	_irqL irqL,irqL2;
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
		_enter_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);
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
		
			//s2. update cur_network 
			if(ptarget_wlan)
			{			
				rtw_joinbss_update_network(adapter, ptarget_wlan, pnetwork);
			}
			else
			{
				RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("Can't find ptarget_wlan when joinbss_event callback\n"));
				_exit_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);
				goto ignore_joinbss_callback;
			}
			
			
			//s3. find ptarget_sta & update ptarget_sta after update cur_network only for station mode 
			if(check_fwstate(pmlmepriv, WIFI_STATION_STATE) == _TRUE)
			{
				ptarget_sta = rtw_joinbss_update_stainfo(adapter, pnetwork);
				if(ptarget_sta==NULL)
				{
					RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("Can't update stainfo when joinbss_event callback\n"));
					_exit_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);
					goto ignore_joinbss_callback;
				}
			}	
					
			//s4. indicate connect			
			if(check_fwstate(pmlmepriv, WIFI_STATION_STATE) == _TRUE)
			{
				rtw_indicate_connect(adapter);
			}
			else
			{
				//adhoc mode will rtw_indicate_connect when rtw_stassoc_event_callback
				RT_TRACE(_module_rtl871x_mlme_c_,_drv_info_,("adhoc mode, fw_state:%x", get_fwstate(pmlmepriv)));
			}


			//s5. Cancle assoc_timer					
			_cancel_timer(&pmlmepriv->assoc_timer, &timer_cancelled);
			
			RT_TRACE(_module_rtl871x_mlme_c_,_drv_info_,("Cancle assoc_timer\n"));
				
		}
		else
		{
			RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("rtw_joinbss_event_callback err: fw_state:%x", get_fwstate(pmlmepriv)));	
			_exit_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);
			goto ignore_joinbss_callback;
		}
		
		_exit_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);
				
	}
	else if(pnetwork->join_res == -4) 
	{
		rtw_reset_securitypriv(adapter);
		_set_timer(&pmlmepriv->assoc_timer, 1);					

		//rtw_free_assoc_resources(adapter, 1);

		if((check_fwstate(pmlmepriv, _FW_UNDER_LINKING)) == _TRUE)
		{		
			RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("fail! clear _FW_UNDER_LINKING ^^^fw_state=%x\n", get_fwstate(pmlmepriv)));
			_clr_fwstate_(pmlmepriv, _FW_UNDER_LINKING);
		}	
		
	}
	else //if join_res < 0 (join fails), then try again
	{
	
		#ifdef REJOIN
		res = _FAIL;
		if(retry < 2) {
			res = rtw_select_and_join_from_scanned_queue(pmlmepriv);
			RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("rtw_select_and_join_from_scanned_queue again! res:%d\n",res));
		}

		 if(res == _SUCCESS)
		{
			//extend time of assoc_timer
			_set_timer(&pmlmepriv->assoc_timer, MAX_JOIN_TIMEOUT);
			retry++;
		}
		else if(res == 2)//there is no need to wait for join
		{
			_clr_fwstate_(pmlmepriv, _FW_UNDER_LINKING);
			rtw_indicate_connect(adapter);
		}	
		else
		{
			RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("Set Assoc_Timer = 1; can't find match ssid in scanned_q \n"));
		#endif
			
			_set_timer(&pmlmepriv->assoc_timer, 1);
			//rtw_free_assoc_resources(adapter, 1);
			_clr_fwstate_(pmlmepriv, _FW_UNDER_LINKING);
			
		#ifdef REJOIN
			retry = 0;	
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

_func_enter_;

	mlmeext_joinbss_event_callback(adapter, pnetwork->join_res);

	rtw_os_xmit_schedule(adapter);

#ifdef CONFIG_CONCURRENT_MODE	
	rtw_os_xmit_schedule(adapter->pbuddy_adapter);
#endif

#ifdef CONFIG_DUALMAC_CONCURRENT
	dc_resume_xmit(adapter);
#endif

_func_exit_;
}

void rtw_stassoc_event_callback(_adapter *adapter, u8 *pbuf)
{
	_irqL irqL;	
	struct sta_info *psta;
	struct mlme_priv *pmlmepriv = &(adapter->mlmepriv);
	struct stassoc_event	*pstassoc	= (struct stassoc_event*)pbuf;
	struct wlan_network 	*cur_network = &(pmlmepriv->cur_network);
	struct wlan_network	*ptarget_wlan = NULL;

_func_enter_;	
	
	if(rtw_access_ctrl(adapter, pstassoc->macaddr) == _FALSE)
		return;

#if defined (CONFIG_AP_MODE) && defined (CONFIG_NATIVEAP_MLME)
	if(check_fwstate(pmlmepriv, WIFI_AP_STATE))
	{
		psta = rtw_get_stainfo(&adapter->stapriv, pstassoc->macaddr);	
		if(psta)
		{
#ifdef CONFIG_IOCTL_CFG80211
#ifdef COMPAT_KERNEL_RELEASE

#elif (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,37)) || defined(CONFIG_CFG80211_FORCE_COMPATIBLE_2_6_37_UNDER)
			u8 *passoc_req = NULL;
			u32 assoc_req_len;

			_enter_critical_bh(&psta->lock, &irqL);
			if(psta->passoc_req && psta->assoc_req_len>0)
			{				
				passoc_req = rtw_zmalloc(psta->assoc_req_len);
				if(passoc_req)
				{
					assoc_req_len = psta->assoc_req_len;
					_rtw_memcpy(passoc_req, psta->passoc_req, assoc_req_len);
					
					rtw_mfree(psta->passoc_req , psta->assoc_req_len);
					psta->passoc_req = NULL;
					psta->assoc_req_len = 0;
				}
			}			
			_exit_critical_bh(&psta->lock, &irqL);

			if(passoc_req && assoc_req_len>0)
			{
				rtw_cfg80211_indicate_sta_assoc(adapter, passoc_req, assoc_req_len);

				rtw_mfree(passoc_req, assoc_req_len);
			}
#endif //(LINUX_VERSION_CODE < KERNEL_VERSION(2,6,37)) || defined(CONFIG_CFG80211_FORCE_COMPATIBLE_2_6_37_UNDER)
#endif //CONFIG_IOCTL_CFG80211	

			//bss_cap_update_on_sta_join(adapter, psta);
			//sta_info_update(adapter, psta);
			ap_sta_info_defer_update(adapter, psta);
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
	psta->mac_id = (uint)pstassoc->cam_id;
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
			_enter_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);
			ptarget_wlan = rtw_find_network(&pmlmepriv->scanned_queue, cur_network->network.MacAddress);
			if(ptarget_wlan)	ptarget_wlan->fixed = _TRUE;
			_exit_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);
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

        if(check_fwstate(pmlmepriv, WIFI_AP_STATE))
        {
#ifdef CONFIG_IOCTL_CFG80211
#ifdef COMPAT_KERNEL_RELEASE

#elif (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,37)) || defined(CONFIG_CFG80211_FORCE_COMPATIBLE_2_6_37_UNDER)
		rtw_cfg80211_indicate_sta_disassoc(adapter, pstadel->macaddr, *(u16*)pstadel->rsvd);
#endif //(LINUX_VERSION_CODE < KERNEL_VERSION(2,6,37)) || defined(CONFIG_CFG80211_FORCE_COMPATIBLE_2_6_37_UNDER)
#endif //CONFIG_IOCTL_CFG80211

		return;
        }


	mlmeext_sta_del_event_callback(adapter);

	_enter_critical_bh(&pmlmepriv->lock, &irqL2);

	if(check_fwstate(pmlmepriv, WIFI_STATION_STATE) )
	{
		#ifdef CONFIG_LAYER2_ROAMING
		if (rtw_to_roaming(adapter) > 0)
			pmlmepriv->to_roaming--; /* this stadel_event is caused by roaming, decrease to_roaming */
		else if (rtw_to_roaming(adapter) == 0)
			rtw_set_roaming(adapter, adapter->registrypriv.max_roaming_times);
#ifdef CONFIG_INTEL_WIDI
		if(adapter->mlmepriv.widi_state != INTEL_WIDI_STATE_CONNECTED)
#endif // CONFIG_INTEL_WIDI
		if(*((unsigned short *)(pstadel->rsvd)) != WLAN_REASON_EXPIRATION_CHK)
			rtw_set_roaming(adapter, 0); /* don't roam */
		#endif

		rtw_free_uc_swdec_pending_queue(adapter);

		rtw_free_assoc_resources(adapter, 1);
		rtw_indicate_disconnect(adapter);
		_enter_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);
		// remove the network entry in scanned_queue
		pwlan = rtw_find_network(&pmlmepriv->scanned_queue, tgt_network->network.MacAddress);	
		if (pwlan) {			
			pwlan->fixed = _FALSE;
			rtw_free_network_nolock(pmlmepriv, pwlan);
		}
		_exit_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);

		_rtw_roaming(adapter, tgt_network);
		
#ifdef CONFIG_INTEL_WIDI
		if (!rtw_to_roaming(adapter))
			process_intel_widi_disconnect(adapter, 1);
#endif // CONFIG_INTEL_WIDI
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
			_enter_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);
			//free old ibss network
			//pwlan = rtw_find_network(&pmlmepriv->scanned_queue, pstadel->macaddr);
			pwlan = rtw_find_network(&pmlmepriv->scanned_queue, tgt_network->network.MacAddress);
			if(pwlan)	
			{
				pwlan->fixed = _FALSE;
				rtw_free_network_nolock(pmlmepriv, pwlan); 
			}
			_exit_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);
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


void rtw_cpwm_event_callback(PADAPTER padapter, u8 *pbuf)
{
#ifdef CONFIG_LPS_LCLK
	struct reportpwrstate_parm *preportpwrstate;
#endif

_func_enter_;

	RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("rtw_cpwm_event_callback !!!\n"));
#ifdef CONFIG_LPS_LCLK
	preportpwrstate = (struct reportpwrstate_parm*)pbuf;
	preportpwrstate->state |= (u8)(padapter->pwrctrlpriv.cpwm_tog + 0x80);
	cpwm_int_hdl(padapter, preportpwrstate);
#endif

_func_exit_;

}

/*
* _rtw_join_timeout_handler - Timeout/faliure handler for CMD JoinBss
* @adapter: pointer to _adapter structure
*/
void _rtw_join_timeout_handler (_adapter *adapter)
{
	_irqL irqL;
	struct	mlme_priv *pmlmepriv = &adapter->mlmepriv;
#ifdef CONFIG_LAYER2_ROAMING
	int do_join_r;
#endif //CONFIG_LAYER2_ROAMING

#if 0
	if (adapter->bDriverStopped == _TRUE){
		_rtw_up_sema(&pmlmepriv->assoc_terminate);
		return;
	}
#endif	

_func_enter_;		
#ifdef PLATFORM_FREEBSD
		rtw_mtx_lock(NULL);
		 if (callout_pending(&adapter->mlmepriv.assoc_timer.callout)) {
			 /* callout was reset */
			 //mtx_unlock(&sc->sc_mtx);
			 rtw_mtx_unlock(NULL);
			 return;
		 }
		 if (!callout_active(&adapter->mlmepriv.assoc_timer.callout)) {
			 /* callout was stopped */
			 //mtx_unlock(&sc->sc_mtx);
			 rtw_mtx_unlock(NULL);
			 return;
		 }
		 callout_deactivate(&adapter->mlmepriv.assoc_timer.callout);
	
	
#endif

	DBG_871X("%s, fw_state=%x\n", __FUNCTION__, get_fwstate(pmlmepriv));
	
	if(adapter->bDriverStopped ||adapter->bSurpriseRemoved)
		return;

	
	_enter_critical_bh(&pmlmepriv->lock, &irqL);

	#ifdef CONFIG_LAYER2_ROAMING
	if (rtw_to_roaming(adapter) > 0) { /* join timeout caused by roaming */
		while(1) {
			pmlmepriv->to_roaming--;
			if (rtw_to_roaming(adapter) != 0) { /* try another */
				DBG_871X("%s try another roaming\n", __FUNCTION__);
				if( _SUCCESS!=(do_join_r=rtw_do_join(adapter)) ) {
					DBG_871X("%s roaming do_join return %d\n", __FUNCTION__ ,do_join_r);
					continue;
				}
				break;
			} else {
#ifdef CONFIG_INTEL_WIDI
				if(adapter->mlmepriv.widi_state == INTEL_WIDI_STATE_ROAMING)
				{
					_rtw_memset(pmlmepriv->sa_ext, 0x00, L2SDTA_SERVICE_VE_LEN);
					intel_widi_wk_cmd(adapter, INTEL_WIDI_LISTEN_WK, NULL);
					DBG_871X("change to widi listen\n");
				}
#endif // CONFIG_INTEL_WIDI
				DBG_871X("%s We've try roaming but fail\n", __FUNCTION__);
				rtw_indicate_disconnect(adapter);
				break;
			}
		}
		
	} else 
	#endif
	{
		rtw_indicate_disconnect(adapter);
		free_scanqueue(pmlmepriv);//???

#ifdef CONFIG_IOCTL_CFG80211
		//indicate disconnect for the case that join_timeout and check_fwstate != FW_LINKED
		rtw_cfg80211_indicate_disconnect(adapter);
#endif //CONFIG_IOCTL_CFG80211

 	}

	_exit_critical_bh(&pmlmepriv->lock, &irqL);
	

#ifdef CONFIG_DRVEXT_MODULE_WSC	
	drvext_assoc_fail_indicate(&adapter->drvextpriv);	
#endif	
#ifdef PLATFORM_FREEBSD
		rtw_mtx_unlock(NULL);
#endif
	
_func_exit_;

}

/*
* rtw_scan_timeout_handler - Timeout/Faliure handler for CMD SiteSurvey
* @adapter: pointer to _adapter structure
*/
void rtw_scan_timeout_handler (_adapter *adapter)
{	
	_irqL irqL;
	struct	mlme_priv *pmlmepriv = &adapter->mlmepriv;
	
	DBG_871X(FUNC_ADPT_FMT" fw_state=%x\n", FUNC_ADPT_ARG(adapter), get_fwstate(pmlmepriv));

	_enter_critical_bh(&pmlmepriv->lock, &irqL);
	
	_clr_fwstate_(pmlmepriv, _FW_UNDER_SURVEY);

	_exit_critical_bh(&pmlmepriv->lock, &irqL);

	rtw_indicate_scan_done(adapter, _TRUE);

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
/*		
			if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY|_FW_UNDER_LINKING) == _TRUE) 
			{
				DBG_871X("exit %s when _FW_UNDER_SURVEY|_FW_UNDER_LINKING -> \n", __FUNCTION__);
				return;
			}
			
			if(pmlmepriv->sitesurveyctrl.traffic_busy == _TRUE)
			{
				DBG_871X("%s exit cause traffic_busy(%x)\n",__FUNCTION__, pmlmepriv->sitesurveyctrl.traffic_busy);
				return;
			}
*/

#ifdef CONFIG_CONCURRENT_MODE
			if (rtw_buddy_adapter_up(padapter))
			{
				if ((check_buddy_fwstate(padapter, _FW_UNDER_SURVEY|_FW_UNDER_LINKING) == _TRUE) ||
					(padapter->pbuddy_adapter->mlmepriv.LinkDetectInfo.bBusyTraffic == _TRUE))
				{		
					DBG_871X("%s, but buddy_intf is under scanning or linking or BusyTraffic\n", __FUNCTION__);
					return;
				}
			}
#endif

			DBG_871X("%s\n", __FUNCTION__);

			rtw_set_802_11_bssid_list_scan(padapter, NULL, 0);			
			
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
#ifdef CONFIG_CONCURRENT_MODE	
	PADAPTER pbuddy_adapter = adapter->pbuddy_adapter;	
#endif

	if(!adapter)
		return;	

	if(adapter->hw_init_completed == _FALSE)
		return;

	if ((adapter->bDriverStopped == _TRUE)||(adapter->bSurpriseRemoved== _TRUE))
		return;


#ifdef CONFIG_CONCURRENT_MODE
	if(pbuddy_adapter)
	{
		if(adapter->net_closed == _TRUE && pbuddy_adapter->net_closed == _TRUE)
		{
			return;
		}		
	}
	else
#endif //CONFIG_CONCURRENT_MODE
	if(adapter->net_closed == _TRUE)
	{
		return;
	}	

	rtw_dynamic_chk_wk_cmd(adapter);

	if(pregistrypriv->wifi_spec==1)
	{	
#ifdef CONFIG_P2P
		struct wifidirect_info *pwdinfo = &adapter->wdinfo;
		if(rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE))
#endif	
		{
			//auto site survey
			rtw_auto_scan_handler(adapter);
		}	
	}

#ifndef CONFIG_ACTIVE_KEEP_ALIVE_CHECK
#ifdef CONFIG_AP_MODE
	if(check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE)
	{
		expire_timeout_chk(adapter);
	}	
#endif
#endif //!CONFIG_ACTIVE_KEEP_ALIVE_CHECK

#ifdef CONFIG_BR_EXT

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 35))
	rcu_read_lock();
#endif	// (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 35))

#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 35)) 
	if( adapter->pnetdev->br_port 
#else	// (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 35))
	if( rcu_dereference(adapter->pnetdev->rx_handler_data)
#endif	// (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 35))
		&& (check_fwstate(pmlmepriv, WIFI_STATION_STATE|WIFI_ADHOC_STATE) == _TRUE) )
	{
		// expire NAT2.5 entry
		void nat25_db_expire(_adapter *priv);
		nat25_db_expire(adapter);

		if (adapter->pppoe_connection_in_progress > 0) {
			adapter->pppoe_connection_in_progress--;
		}
		
		// due to rtw_dynamic_check_timer_handlder() is called every 2 seconds
		if (adapter->pppoe_connection_in_progress > 0) {
			adapter->pppoe_connection_in_progress--;
		}
	}

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 35))
	rcu_read_unlock();
#endif	// (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 35))

#endif	// CONFIG_BR_EXT
	
}


#ifdef CONFIG_SET_SCAN_DENY_TIMER
inline bool rtw_is_scan_deny(_adapter *adapter)
{
	struct mlme_priv *mlmepriv = &adapter->mlmepriv;
	return (ATOMIC_READ(&mlmepriv->set_scan_deny) != 0) ? _TRUE : _FALSE;
}

inline void rtw_clear_scan_deny(_adapter *adapter)
{
	struct mlme_priv *mlmepriv = &adapter->mlmepriv;
	ATOMIC_SET(&mlmepriv->set_scan_deny, 0);
	if (0)
	DBG_871X(FUNC_ADPT_FMT"\n", FUNC_ADPT_ARG(adapter));
}

void rtw_set_scan_deny_timer_hdl(_adapter *adapter)
{
	rtw_clear_scan_deny(adapter);
}

void rtw_set_scan_deny(_adapter *adapter, u32 ms)
{
	struct mlme_priv *mlmepriv = &adapter->mlmepriv;
#ifdef CONFIG_CONCURRENT_MODE
	struct mlme_priv *b_mlmepriv;
#endif

	if (0)
	DBG_871X(FUNC_ADPT_FMT"\n", FUNC_ADPT_ARG(adapter));
	ATOMIC_SET(&mlmepriv->set_scan_deny, 1);
	_set_timer(&mlmepriv->set_scan_deny_timer, ms);
	
#ifdef CONFIG_CONCURRENT_MODE
	if (!adapter->pbuddy_adapter)
		return;

	if (0)
	DBG_871X(FUNC_ADPT_FMT"\n", FUNC_ADPT_ARG(adapter->pbuddy_adapter));
 	b_mlmepriv = &adapter->pbuddy_adapter->mlmepriv;
	ATOMIC_SET(&b_mlmepriv->set_scan_deny, 1);
	_set_timer(&b_mlmepriv->set_scan_deny_timer, ms);	
#endif
	
}
#endif

#if defined(IEEE80211_SCAN_RESULT_EXPIRE)
#define RTW_SCAN_RESULT_EXPIRE IEEE80211_SCAN_RESULT_EXPIRE/HZ*1000 -1000 //3000 -1000
#else
#define RTW_SCAN_RESULT_EXPIRE 2000
#endif

#ifndef PLATFORM_FREEBSD
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


	//check bssid, if needed
	if(pmlmepriv->assoc_by_bssid==_TRUE) {
		if(_rtw_memcmp(competitor->network.MacAddress, pmlmepriv->assoc_bssid, ETH_ALEN) ==_FALSE)
			goto exit;
	}

	//check ssid, if needed
	if(pmlmepriv->assoc_ssid.Ssid && pmlmepriv->assoc_ssid.SsidLength) {
		if(	competitor->network.Ssid.SsidLength != pmlmepriv->assoc_ssid.SsidLength
			|| _rtw_memcmp(competitor->network.Ssid.Ssid, pmlmepriv->assoc_ssid.Ssid, pmlmepriv->assoc_ssid.SsidLength) == _FALSE
		)
			goto exit;
	}

	if(rtw_is_desired_network(adapter, competitor)  == _FALSE)
		goto exit;

#ifdef  CONFIG_LAYER2_ROAMING
	if(rtw_to_roaming(adapter) > 0) {
		if(	rtw_get_passing_time_ms((u32)competitor->last_scanned) >= RTW_SCAN_RESULT_EXPIRE
			|| is_same_ess(&competitor->network, &pmlmepriv->cur_network.network) == _FALSE
		)
			goto exit;
	}
#endif
	
	if(*candidate == NULL ||(*candidate)->network.Rssi<competitor->network.Rssi )
	{
		*candidate = competitor;
		updated = _TRUE;
	}

#if 0
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
	if(rtw_to_roaming(adapter)) { // roaming
		if(	(*candidate == NULL ||(*candidate)->network.Rssi<competitor->network.Rssi )
			&& is_same_ess(&competitor->network, &pmlmepriv->cur_network.network) 
			//&&(!is_same_network(&competitor->network, &pmlmepriv->cur_network.network))
			&& rtw_get_passing_time_ms((u32)competitor->last_scanned) < RTW_SCAN_RESULT_EXPIRE
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
#endif

	if(updated){
		DBG_871X("[by_bssid:%u][assoc_ssid:%s]"
			#ifdef  CONFIG_LAYER2_ROAMING
			"[to_roaming:%u] "
			#endif
			"new candidate: %s("MAC_FMT", ch%u) rssi:%d\n",
			pmlmepriv->assoc_by_bssid,
			pmlmepriv->assoc_ssid.Ssid,
			#ifdef  CONFIG_LAYER2_ROAMING
			rtw_to_roaming(adapter),
			#endif
			(*candidate)->network.Ssid.Ssid,
			MAC_ARG((*candidate)->network.MacAddress),
			(*candidate)->network.Configuration.DSConfig,
			(int)(*candidate)->network.Rssi
		);
	}

exit:
	return updated;
}

/*
Calling context:
The caller of the sub-routine will be in critical section...

The caller must hold the following spinlock

pmlmepriv->lock


*/

int rtw_select_and_join_from_scanned_queue(struct mlme_priv *pmlmepriv )
{
	_irqL	irqL;
	int ret;
	_list	*phead;
	_adapter *adapter;	
	_queue	*queue	= &(pmlmepriv->scanned_queue);
	struct	wlan_network	*pnetwork = NULL;
	struct	wlan_network	*candidate = NULL;
	u8 		bSupportAntDiv = _FALSE;

_func_enter_;

	_enter_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);
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
		DBG_871X("%s: candidate: %s("MAC_FMT", ch:%u)\n", __FUNCTION__,
			candidate->network.Ssid.Ssid, MAC_ARG(candidate->network.MacAddress),
			candidate->network.Configuration.DSConfig);
	}
	

	// check for situation of  _FW_LINKED 
	if (check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE)
	{
		DBG_871X("%s: _FW_LINKED while ask_for_joinbss!!!\n", __FUNCTION__);

		#if 0 // for WPA/WPA2 authentication, wpa_supplicant will expect authentication from AP, it is needed to reconnect AP...
		if(is_same_network(&pmlmepriv->cur_network.network, &candidate->network))
		{
			DBG_871X("%s: _FW_LINKED and is same network, it needn't join again\n", __FUNCTION__);

			rtw_indicate_connect(adapter);//rtw_indicate_connect again
				
			ret = 2;
			goto exit;
		}
		else
		#endif
		{
			rtw_disassoc_cmd(adapter, 0, _TRUE);
			rtw_indicate_disconnect(adapter);
			rtw_free_assoc_resources(adapter, 0);
		}
	}
	
	#ifdef CONFIG_ANTENNA_DIVERSITY
	rtw_hal_get_def_var(adapter, HAL_DEF_IS_SUPPORT_ANT_DIV, &(bSupportAntDiv));
	if(_TRUE == bSupportAntDiv)	
	{
		u8 CurrentAntenna;
		rtw_hal_get_def_var(adapter, HAL_DEF_CURRENT_ANTENNA, &(CurrentAntenna));			
		DBG_871X("#### Opt_Ant_(%s) , cur_Ant(%s)\n",
			(2==candidate->network.PhyInfo.Optimum_antenna)?"A":"B",
			(2==CurrentAntenna)?"A":"B"
		);
	}
	#endif
	set_fwstate(pmlmepriv, _FW_UNDER_LINKING);
	ret = rtw_joinbss_cmd(adapter, candidate);
	
exit:
	_exit_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);

_func_exit_;

	return ret;
}
#else
int rtw_select_and_join_from_scanned_queue(struct mlme_priv *pmlmepriv )
{	
	_irqL	irqL;
	_list	*phead;
#ifdef CONFIG_ANTENNA_DIVERSITY
	u8 CurrentAntenna;
#endif
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
	_enter_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);
	phead = get_list_head(queue);		
	adapter = (_adapter *)pmlmepriv->nic_hdl;

	pmlmepriv->pscanned = get_next( phead );

	while (!rtw_end_of_queue_search(phead, pmlmepriv->pscanned)) {

		pnetwork = LIST_CONTAINOR(pmlmepriv->pscanned, struct wlan_network, list);
		if(pnetwork==NULL){
			RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("(2)rtw_select_and_join_from_scanned_queue return _FAIL:(pnetwork==NULL)\n"));
			_exit_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);
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
						_exit_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);	
						return 2;
					}
					else
					{
						rtw_disassoc_cmd(adapter, 0, _TRUE);
						rtw_indicate_disconnect(adapter);
						rtw_free_assoc_resources(adapter, 0);
						_exit_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);
						goto ask_for_joinbss;
						
					}
				}
				else
				{
					_exit_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);
					goto ask_for_joinbss;
				}
							
			}
			
		} else if (pmlmepriv->assoc_ssid.SsidLength == 0) {
			_exit_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);
			goto ask_for_joinbss;//anyway, join first selected(dequeued) pnetwork if ssid_len=0				
	
		#ifdef CONFIG_LAYER2_ROAMING
		} else if (rtw_to_roaming(adapter) > 0) {
		
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
			rtw_hal_get_def_var(adapter, HAL_DEF_CURRENT_ANTENNA, &(CurrentAntenna));			
			DBG_871X("#### dst_ssid=(%s) Opt_Ant_(%s) , cur_Ant(%s)\n", dst_ssid,
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
						rtw_disassoc_cmd(adapter, 0, _TRUE);
						//rtw_indicate_disconnect(adapter);//
						rtw_free_assoc_resources(adapter, 0);
						_exit_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);
						goto ask_for_joinbss;						
					}
				}
				else
				{
					_exit_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);
					goto ask_for_joinbss;
				}				

			}
		
			
		}
 	
 	}
	_exit_critical_bh(&(pmlmepriv->scanned_queue.lock), &irqL);
	#ifdef CONFIG_LAYER2_ROAMING
	if(rtw_to_roaming(adapter) > 0 && roaming_candidate ){
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
#endif //PLATFORM_FREEBSD


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
	if (is_wep_enc(psetkeyparm->algorithm))
		psecuritypriv->key_mask |= BIT(psetkeyparm->keyid);

	DBG_871X("==> rtw_set_key algorithm(%x),keyid(%x),key_mask(%x)\n",psetkeyparm->algorithm,psetkeyparm->keyid, psecuritypriv->key_mask);
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

//
// Check the RSN IE length
// If the RSN IE length <= 20, the RSN IE didn't include the PMKID information
// 0-11th element in the array are the fixed IE
// 12th element in the array is the IE
// 13th element in the array is the IE length  
//

static int rtw_append_pmkid(_adapter *Adapter,int iEntry, u8 *ie, uint ie_len)
{
	struct security_priv *psecuritypriv=&Adapter->securitypriv;

	if(ie[13]<=20){	
		// The RSN IE didn't include the PMK ID, append the PMK information 
			ie[ie_len]=1;
			ie_len++;
			ie[ie_len]=0;	//PMKID count = 0x0100
			ie_len++;
			_rtw_memcpy(	&ie[ie_len], &psecuritypriv->PMKIDList[iEntry].PMKID, 16);
		
			ie_len+=16;
			ie[13]+=18;//PMKID length = 2+16

	}
	return (ie_len);
	
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
	
	//copy fixed ie only
	_rtw_memcpy(out_ie, in_ie,12);
	ielength=12;
	if((ndisauthmode==Ndis802_11AuthModeWPA)||(ndisauthmode==Ndis802_11AuthModeWPAPSK))
			authmode=_WPA_IE_ID_;
	if((ndisauthmode==Ndis802_11AuthModeWPA2)||(ndisauthmode==Ndis802_11AuthModeWPA2PSK))
			authmode=_WPA2_IE_ID_;

	if(check_fwstate(pmlmepriv, WIFI_UNDER_WPS))
	{
		_rtw_memcpy(out_ie+ielength, psecuritypriv->wps_ie, psecuritypriv->wps_ie_len);
		
		ielength += psecuritypriv->wps_ie_len;
	}
	else if((authmode==_WPA_IE_ID_)||(authmode==_WPA2_IE_ID_))
	{		
		//copy RSN or SSN		
		_rtw_memcpy(&out_ie[ielength], &psecuritypriv->supplicant_ie[0], psecuritypriv->supplicant_ie[1]+2);
		/* debug for CONFIG_IEEE80211W
		{
			int jj;
			printk("supplicant_ie_length=%d &&&&&&&&&&&&&&&&&&&\n", psecuritypriv->supplicant_ie[1]+2);
			for(jj=0; jj < psecuritypriv->supplicant_ie[1]+2; jj++)
				printk(" %02x ", psecuritypriv->supplicant_ie[jj]);
			printk("\n");
		}*/
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
			ielength=rtw_append_pmkid(adapter, iEntry, out_ie, ielength);
		}
	}

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
	//struct	xmit_priv	*pxmitpriv = &adapter->xmitpriv;

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
	// TH=1 => means that invalidate usb rx aggregation
	// TH=0 => means that validate usb rx aggregation, use init value.
	if(phtpriv->ht_option)
	{
		if(padapter->registrypriv.wifi_spec==1)		
			threshold = 1;
		else
			threshold = 0;
		rtw_hal_set_hwreg(padapter, HW_VAR_RXDMA_AGG_PG_TH, (u8 *)(&threshold));
	}
	else
	{
		threshold = 1;
		rtw_hal_set_hwreg(padapter, HW_VAR_RXDMA_AGG_PG_TH, (u8 *)(&threshold));
	}
#endif

#endif	

}


#ifdef CONFIG_80211N_HT

//the fucntion is >= passive_level 
unsigned int rtw_restructure_ht_ie(_adapter *padapter, u8 *in_ie, u8 *out_ie, uint in_len, uint *pout_len, u8 channel)
{
	u32 ielen, out_len;
	unsigned char *p, *pframe;
	struct rtw_ieee80211_ht_cap ht_capie;
	unsigned char WMM_IE[] = {0x00, 0x50, 0xf2, 0x02, 0x00, 0x01, 0x00};
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct qos_priv   	*pqospriv= &pmlmepriv->qospriv;
	struct ht_priv		*phtpriv = &pmlmepriv->htpriv;
	struct registry_priv	*pregpriv = &padapter->registrypriv;
	u8 cbw40_enable = 0;

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

		_rtw_memset(&ht_capie, 0, sizeof(struct rtw_ieee80211_ht_cap));

		ht_capie.cap_info =  IEEE80211_HT_CAP_SGI_20 | IEEE80211_HT_CAP_TX_STBC |
							IEEE80211_HT_CAP_DSSSCCK40;
		//if insert module set only support 20MHZ, don't add the 40MHZ and SGI_40
		if( channel > 14 )
		{
			if( pregpriv->cbw40_enable & BIT(1) )
				cbw40_enable = 1;
		}
		else
			if( pregpriv->cbw40_enable & BIT(0) )
				cbw40_enable = 1;

		if ( cbw40_enable != 0 )
			ht_capie.cap_info |= IEEE80211_HT_CAP_SUP_WIDTH | IEEE80211_HT_CAP_SGI_40;
				


		{
			u32 rx_packet_offset, max_recvbuf_sz;
			rtw_hal_get_def_var(padapter, HAL_DEF_RX_PACKET_OFFSET, &rx_packet_offset);
			rtw_hal_get_def_var(padapter, HAL_DEF_MAX_RECVBUF_SZ, &max_recvbuf_sz);
			//if(max_recvbuf_sz-rx_packet_offset>(8191-256)) {
			//	DBG_871X("%s IEEE80211_HT_CAP_MAX_AMSDU is set\n", __FUNCTION__);
			//	ht_capie.cap_info = ht_capie.cap_info |IEEE80211_HT_CAP_MAX_AMSDU;
			//}
		}
		
		ht_capie.ampdu_params_info = (IEEE80211_HT_CAP_AMPDU_FACTOR&0x03);

		if(padapter->securitypriv.dot11PrivacyAlgrthm == _AES_ )
			ht_capie.ampdu_params_info |= (IEEE80211_HT_CAP_AMPDU_DENSITY&(0x07<<2));
		else
			ht_capie.ampdu_params_info |= (IEEE80211_HT_CAP_AMPDU_DENSITY&0x00);										

		
		pframe = rtw_set_ie(out_ie+out_len, _HT_CAPABILITY_IE_, 
							sizeof(struct rtw_ieee80211_ht_cap), (unsigned char*)&ht_capie, pout_len);


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
void rtw_update_ht_cap(_adapter *padapter, u8 *pie, uint ie_len, u8 channel)
{	
	u8 *p, max_ampdu_sz;
	int len;		
	//struct sta_info *bmc_sta, *psta;	
	struct rtw_ieee80211_ht_cap *pht_capie;
	struct ieee80211_ht_addt_info *pht_addtinfo;
	//struct recv_reorder_ctrl *preorder_ctrl;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct ht_priv		*phtpriv = &pmlmepriv->htpriv;
	//struct recv_priv *precvpriv = &padapter->recvpriv;
	struct registry_priv *pregistrypriv = &padapter->registrypriv;
	//struct wlan_network *pcur_network = &(pmlmepriv->cur_network);;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);	
	u8 cbw40_enable=0;

	if(!phtpriv->ht_option)
		return;

	if ((!pmlmeinfo->HT_info_enable) || (!pmlmeinfo->HT_caps_enable))
		return;

	DBG_871X("+rtw_update_ht_cap()\n");

	//maybe needs check if ap supports rx ampdu.
	if((phtpriv->ampdu_enable==_FALSE) &&(pregistrypriv->ampdu_enable==1))
	{
		//In the wifi cert. test, the test Lab should turn off the AP's RX AMPDU. client doen't need to close the TX AMPDU
		/*if(pregistrypriv->wifi_spec==1)
		{
			phtpriv->ampdu_enable = _FALSE;
		}
		else*/
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
		pht_capie = (struct rtw_ieee80211_ht_cap *)(p+2);
		max_ampdu_sz = (pht_capie->ampdu_params_info & IEEE80211_HT_CAP_AMPDU_FACTOR);
		max_ampdu_sz = 1 << (max_ampdu_sz+3); // max_ampdu_sz (kbytes);
		
		//DBG_871X("rtw_update_ht_cap(): max_ampdu_sz=%d\n", max_ampdu_sz);
		phtpriv->rx_ampdu_maxlen = max_ampdu_sz;
		
	}


	len=0;
	p = rtw_get_ie(pie+sizeof (NDIS_802_11_FIXED_IEs), _HT_ADD_INFO_IE_, &len, ie_len-sizeof (NDIS_802_11_FIXED_IEs));
	if(p && len>0)	
	{
		pht_addtinfo = (struct ieee80211_ht_addt_info *)(p+2);
		//todo:
	}

	if( channel > 14 )
	{
		if( pregistrypriv->cbw40_enable & BIT(1) )
			cbw40_enable = 1;
	}
	else
		if( pregistrypriv->cbw40_enable & BIT(0) )
			cbw40_enable = 1;


	//update cur_bwmode & cur_ch_offset
	if ((cbw40_enable) &&
		(pmlmeinfo->HT_caps.u.HT_cap_element.HT_caps_info & BIT(1)) && 
		(pmlmeinfo->HT_info.infos[0] & BIT(2)))
	{
		int i;
		u8	rf_type;

		rtw_hal_get_hwreg(padapter, HW_VAR_RF_TYPE, (u8 *)(&rf_type));

		//update the MCS rates
		for (i = 0; i < 16; i++)
		{
			if((rf_type == RF_1T1R) || (rf_type == RF_1T2R))
			{
				pmlmeinfo->HT_caps.u.HT_cap_element.MCS_rate[i] &= MCS_rate_1R[i];
			}
			else
			{
				#ifdef CONFIG_DISABLE_MCS13TO15
				if(pmlmeext->cur_bwmode == HT_CHANNEL_WIDTH_40 && pregistrypriv->wifi_spec != 1 )
				{
					pmlmeinfo->HT_caps.u.HT_cap_element.MCS_rate[i] &= MCS_rate_2R_MCS13TO15_OFF[i];
				}
				else
					pmlmeinfo->HT_caps.u.HT_cap_element.MCS_rate[i] &= MCS_rate_2R[i];
				#else
				pmlmeinfo->HT_caps.u.HT_cap_element.MCS_rate[i] &= MCS_rate_2R[i];
				#endif //CONFIG_DISABLE_MCS13TO15
			}
			#ifdef RTL8192C_RECONFIG_TO_1T1R
			{
				pmlmeinfo->HT_caps.u.HT_cap_element.MCS_rate[i] &= MCS_rate_1R[i];
			}
			#endif

			if(pregistrypriv->special_rf_path)
				pmlmeinfo->HT_caps.u.HT_cap_element.MCS_rate[i] &= MCS_rate_1R[i];

		}
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
	}

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

	
	
#if 0 //move to rtw_update_sta_info_client()
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
#endif	

}

void rtw_issue_addbareq_cmd(_adapter *padapter, struct xmit_frame *pxmitframe)
{
	u8 issued;
	int priority;
	struct sta_info *psta=NULL;
	struct ht_priv	*phtpriv;
	struct pkt_attrib *pattrib =&pxmitframe->attrib;
	s32 bmcst = IS_MCAST(pattrib->ra);

	if(bmcst || (padapter->mlmepriv.LinkDetectInfo.bTxBusyTraffic == _FALSE))
		return;
	
	priority = pattrib->priority;

	if (pattrib->psta)
		psta = pattrib->psta;
	else
	{
		DBG_871X("%s, call rtw_get_stainfo()\n", __func__);
		psta = rtw_get_stainfo(&padapter->stapriv, pattrib->ra);
	}	
	
	if(psta==NULL)
	{
		DBG_871X("%s, psta==NUL\n", __func__);
		return;
	}

	if(!(psta->state &_FW_LINKED))
	{
		DBG_871X("%s, psta->state(0x%x) != _FW_LINKED\n", __func__, psta->state);
		return;
	}	

	
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
inline void rtw_set_roaming(_adapter *adapter, u8 to_roaming)
{
	if (to_roaming == 0)
		adapter->mlmepriv.to_join = _FALSE;
	adapter->mlmepriv.to_roaming = to_roaming;
}

inline u8 rtw_to_roaming(_adapter *adapter)
{
	return adapter->mlmepriv.to_roaming;
}

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
	
	if(0 < rtw_to_roaming(padapter)) {
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
				
				if(0< rtw_to_roaming(padapter)) {
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

#ifdef CONFIG_CONCURRENT_MODE
sint rtw_buddy_adapter_up(_adapter *padapter)
{	
	sint res = _FALSE;
	
	if(padapter == NULL)
		return res;


	if(padapter->pbuddy_adapter == NULL)
	{
		res = _FALSE;
	}
	else if( (padapter->pbuddy_adapter->bDriverStopped) || (padapter->pbuddy_adapter->bSurpriseRemoved) ||
		(padapter->pbuddy_adapter->bup == _FALSE) || (padapter->pbuddy_adapter->hw_init_completed == _FALSE))
	{		
		res = _FALSE;
	}
	else
	{
		res = _TRUE;
	}	

	return res;	

}

sint check_buddy_fwstate(_adapter *padapter, sint state)
{
	if(padapter == NULL)
		return _FALSE;	
	
	if(padapter->pbuddy_adapter == NULL)
		return _FALSE;	
		
	if ((state == WIFI_FW_NULL_STATE) && 
		(padapter->pbuddy_adapter->mlmepriv.fw_state == WIFI_FW_NULL_STATE))
		return _TRUE;
	
	if (padapter->pbuddy_adapter->mlmepriv.fw_state & state)
		return _TRUE;

	return _FALSE;
}
#endif //CONFIG_CONCURRENT_MODE

