/******************************************************************************
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
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
#define _RTL871X_MLME_C_


#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>


#ifdef PLATFORM_LINUX
#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kref.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,0,0))
#include <linux/smp_lock.h>
#endif
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/circ_buf.h>
#include <asm/uaccess.h>
#include <asm/byteorder.h>
#include <asm/atomic.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,26))
#include <asm/semaphore.h>
#else
#include <linux/semaphore.h>
#endif
#endif


#include <recv_osdep.h>
#include <xmit_osdep.h>
#include <hal_init.h>
#include <mlme_osdep.h>
#include <sta_info.h>
#include <wifi.h>
#include <wlan_bssdef.h>


extern void indicate_wx_scan_complete_event(_adapter *padapter);

sint _init_mlme_priv(_adapter* padapter)
{
	sint	i;
	u8	*pbuf;
	struct wlan_network	*pnetwork;
	struct	mlme_priv *pmlmepriv = &padapter->mlmepriv;
	sint	res=_SUCCESS;

_func_enter_;

	_memset((u8 *)pmlmepriv, 0, sizeof(struct mlme_priv));
	pmlmepriv->nic_hdl = (u8 *)padapter;

	pmlmepriv->pscanned = NULL;
	pmlmepriv->fw_state = 0;
	pmlmepriv->cur_network.network.InfrastructureMode = Ndis802_11AutoUnknown;
	pmlmepriv->passive_mode=1; // 1: active, 0: pasive. Maybe someday we should rename this varable to "active_mode" (Jeff)

	_spinlock_init(&(pmlmepriv->lock));
	_init_queue(&(pmlmepriv->free_bss_pool));
	_init_queue(&(pmlmepriv->scanned_queue));

	set_scanned_network_val(pmlmepriv, 0);

	_memset(&pmlmepriv->assoc_ssid,0,sizeof(NDIS_802_11_SSID));

	pbuf = _vmalloc(MAX_BSS_CNT * (sizeof(struct wlan_network)));

	if (pbuf == NULL) {
		res = _FAIL;
		goto exit;
	}
	pmlmepriv->free_bss_buf = pbuf;

	pnetwork = (struct wlan_network *)pbuf;

	for (i = 0; i < MAX_BSS_CNT; i++)
	{
		_init_listhead(&(pnetwork->list));

		list_insert_tail(&(pnetwork->list), &(pmlmepriv->free_bss_pool.queue));

		pnetwork++;
	}

	pmlmepriv->sitesurveyctrl.last_rx_pkts = 0;
	pmlmepriv->sitesurveyctrl.last_tx_pkts = 0;
	pmlmepriv->sitesurveyctrl.traffic_busy = _FALSE;

	//allocate DMA-able/Non-Page memory for cmd_buf and rsp_buf

	init_mlme_timer(padapter);

exit:

_func_exit_;

	return res;
}

void mfree_mlme_priv_lock (struct mlme_priv *pmlmepriv)
{
	_spinlock_free(&pmlmepriv->lock);
	_spinlock_free(&(pmlmepriv->free_bss_pool.lock));
	_spinlock_free(&(pmlmepriv->scanned_queue.lock));
}

void _free_mlme_priv (struct mlme_priv *pmlmepriv)
{
_func_enter_;

	if (pmlmepriv) {
		mfree_mlme_priv_lock (pmlmepriv);

		if (pmlmepriv->free_bss_buf)
			_vmfree(pmlmepriv->free_bss_buf, MAX_BSS_CNT * sizeof(struct wlan_network));
	}
_func_exit_;
}

sint _enqueue_network(_queue *queue, struct wlan_network *pnetwork)
{
	_irqL irqL;

_func_enter_;

	if (pnetwork == NULL)
		goto exit;

	_enter_critical(&queue->lock, &irqL);

	list_insert_tail(&pnetwork->list, &queue->queue);

	_exit_critical(&queue->lock, &irqL);

exit:

_func_exit_;

	return _SUCCESS;
}

struct wlan_network* _dequeue_network(_queue *queue)
{
	_irqL irqL;

	struct wlan_network *pnetwork;

_func_enter_;

	_enter_critical(&queue->lock, &irqL);

	if (_queue_empty(queue) == _TRUE)

		pnetwork = NULL;

	else
	{
		pnetwork = LIST_CONTAINOR(get_next(&queue->queue), struct wlan_network, list);

		list_delete(&(pnetwork->list));
	}

	_exit_critical(&queue->lock, &irqL);

_func_exit_;

	return pnetwork;
}

struct wlan_network* _alloc_network(struct mlme_priv *pmlmepriv)//(_queue *free_queue)
{
	_irqL irqL;
	struct wlan_network *pnetwork;
	_queue *free_queue = &pmlmepriv->free_bss_pool;
	_list *plist = NULL;

_func_enter_;

	_enter_critical(&free_queue->lock, &irqL);

	if (_queue_empty(free_queue) == _TRUE) {
		pnetwork = NULL;
		goto exit;
	}
	plist = get_next(&(free_queue->queue));

	pnetwork = LIST_CONTAINOR(plist , struct wlan_network, list);

	list_delete(&pnetwork->list);

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_, ("_alloc_network: ptr=%p\n", plist));

	pnetwork->last_scanned = get_current_time();
	pmlmepriv->num_of_scanned++;

exit:
	_exit_critical(&free_queue->lock, &irqL);

_func_exit_;

	return pnetwork;
}

void _free_network(struct mlme_priv *pmlmepriv ,struct wlan_network *pnetwork)
{
	u32 curr_time, delta_time;
	_irqL irqL;
	_queue *free_queue = &(pmlmepriv->free_bss_pool);

_func_enter_;

	if (pnetwork == NULL)
		goto exit;

	if (pnetwork->fixed == _TRUE)
		goto exit;

	curr_time = get_current_time();

#ifdef PLATFORM_WINDOWS

	delta_time = (curr_time - pnetwork->last_scanned) / 10;

	if (delta_time  < SCANQUEUE_LIFETIME*1000000)// unit:usec
	{
		goto exit;
	}

#endif

#ifdef PLATFORM_LINUX

	delta_time = (curr_time - pnetwork->last_scanned) / HZ;

	if (delta_time < SCANQUEUE_LIFETIME)// unit:sec
	{
		goto exit;
	}

#endif

	_enter_critical(&free_queue->lock, &irqL);

	list_delete(&pnetwork->list);

	list_insert_tail(&pnetwork->list, &free_queue->queue);

	pmlmepriv->num_of_scanned--;

	//DBG_8712("_free_network:SSID=%s\n", pnetwork->network.Ssid.Ssid);

	_exit_critical(&free_queue->lock, &irqL);

exit:

_func_exit_;

}

void _free_network_nolock(struct mlme_priv *pmlmepriv, struct wlan_network *pnetwork)
{
	_queue *free_queue = &pmlmepriv->free_bss_pool;

_func_enter_;

	if (pnetwork == NULL)
		goto exit;

	if (pnetwork->fixed == _TRUE)
		goto exit;

	//_enter_critical(&free_queue->lock, &irqL);

	list_delete(&pnetwork->list);

	list_insert_tail(&pnetwork->list, get_list_head(free_queue));

	pmlmepriv->num_of_scanned--;

	//_exit_critical(&free_queue->lock, &irqL);

exit:

_func_exit_;

}


/*
	return the wlan_network with the matching addr

	Shall be calle under atomic context... to avoid possible racing condition...
*/
struct wlan_network* _find_network(_queue *scanned_queue, u8 *addr)
{
	_irqL irqL;
	_list *phead, *plist;
	struct wlan_network *pnetwork = NULL;
	u8 zero_addr[ETH_ALEN] = {0,0,0,0,0,0};

_func_enter_;

	if (_memcmp(zero_addr, addr, ETH_ALEN)) {
		pnetwork = NULL;
		goto exit;
	}

	_enter_critical(&scanned_queue->lock, &irqL);

	phead = get_list_head(scanned_queue);
	plist = get_next(phead);

	while (plist != phead)
	{
		pnetwork = LIST_CONTAINOR(plist, struct wlan_network, list);
		plist = get_next(plist);
		if (_memcmp(addr, pnetwork->network.MacAddress, ETH_ALEN) == _TRUE)
			break;
	}

exit:
	_exit_critical(&scanned_queue->lock, &irqL);

_func_exit_;

	return pnetwork;

}

void _free_network_queue(_adapter *padapter)
{
	_irqL irqL;
	_list *phead, *plist;
	struct wlan_network *pnetwork;
	struct mlme_priv* pmlmepriv = &padapter->mlmepriv;
	_queue *scanned_queue = &pmlmepriv->scanned_queue;
	_queue	*free_queue = &pmlmepriv->free_bss_pool;
	u8 *mybssid = get_bssid(pmlmepriv);

_func_enter_;

	_enter_critical(&scanned_queue->lock, &irqL);

	phead = get_list_head(scanned_queue);
	plist = get_next(phead);

	while (end_of_queue_search(phead, plist) == _FALSE)
	{
		pnetwork = LIST_CONTAINOR(plist, struct wlan_network, list);

		plist = get_next(plist);

		_free_network(pmlmepriv, pnetwork);

	}

	_exit_critical(&scanned_queue->lock, &irqL);

_func_exit_;

}

sint if_up(_adapter *padapter)
{
	sint res;

_func_enter_;

	if (padapter->bDriverStopped || padapter->bSurpriseRemoved ||
	    (check_fwstate(&padapter->mlmepriv, _FW_LINKED) == _FALSE)) {
		RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_, ("if_up: bDriverStopped(%d) OR bSurpriseRemoved(%d)", padapter->bDriverStopped, padapter->bSurpriseRemoved));
		res = _FALSE;
	} else
		res = _TRUE;

_func_exit_;

	return res;
}

void generate_random_ibss(u8* pibss)
{
	u32 curtime = get_current_time();

_func_enter_;

	pibss[0] = 0x02; //in ad-hoc mode bit1 must set to 1
	pibss[1] = 0x11;
	pibss[2] = 0x87;
	pibss[3] = (u8)(curtime & 0xff) ;//p[0];
	pibss[4] = (u8)((curtime>>8) & 0xff) ;//p[1];
	pibss[5] = (u8)((curtime>>16) & 0xff) ;//p[2];

_func_exit_;

	return;
}

uint get_NDIS_WLAN_BSSID_EX_sz (NDIS_WLAN_BSSID_EX *bss)
{
	uint t_len;

_func_enter_;

	t_len = sizeof (ULONG) + sizeof (NDIS_802_11_MAC_ADDRESS) + 2 +
			sizeof (NDIS_802_11_SSID) + sizeof (ULONG) +
			sizeof (NDIS_802_11_RSSI) + sizeof (NDIS_802_11_NETWORK_TYPE) +
			sizeof (NDIS_802_11_CONFIGURATION) +
			sizeof (NDIS_802_11_NETWORK_INFRASTRUCTURE) +
			sizeof (NDIS_802_11_RATES_EX)+ sizeof (ULONG) + bss->IELength;

_func_exit_;

	return t_len;
}

u8 *get_capability_from_ie(u8 *ie)
{
	return (ie + 8 + 2);
}


u16 get_capability(NDIS_WLAN_BSSID_EX *bss)
{
	u16	val;
_func_enter_;
	_memcpy((u8 *)&val, get_capability_from_ie(bss->IEs), 2);
_func_exit_;
	return val;
}

u8 *get_timestampe_from_ie(u8 *ie)
{
	return (ie + 0);
}

u8 *get_beacon_interval_from_ie(u8 *ie)
{
	return (ie + 8);
}


int	init_mlme_priv (_adapter *padapter)//(struct	mlme_priv *pmlmepriv)
{
	int	res;
_func_enter_;
	res = _init_mlme_priv(padapter);// (pmlmepriv);
_func_exit_;
	return res;
}

void free_mlme_priv (struct mlme_priv *pmlmepriv)
{
_func_enter_;
	RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("free_mlme_priv\n"));
	_free_mlme_priv (pmlmepriv);
_func_exit_;
}

int	enqueue_network(_queue *queue, struct wlan_network *pnetwork)
{
	int	res;
_func_enter_;
	res = _enqueue_network(queue, pnetwork);
_func_exit_;
	return res;
}



struct	wlan_network *dequeue_network(_queue *queue)
{
	struct wlan_network *pnetwork;
_func_enter_;
	pnetwork = _dequeue_network(queue);
_func_exit_;
	return pnetwork;
}


struct	wlan_network *alloc_network(struct	mlme_priv *pmlmepriv )//(_queue	*free_queue)
{
	struct	wlan_network	*pnetwork;
_func_enter_;
	pnetwork = _alloc_network(pmlmepriv);
_func_exit_;
	return pnetwork;
}

void free_network(struct mlme_priv *pmlmepriv, struct	wlan_network *pnetwork )//(struct	wlan_network *pnetwork, _queue	*free_queue)
{
_func_enter_;
	RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("free_network==> ssid = %s \n\n" , pnetwork->network.Ssid.Ssid));
	_free_network(pmlmepriv, pnetwork);
_func_exit_;
}


void free_network_nolock(struct mlme_priv *pmlmepriv, struct wlan_network *pnetwork )
{
_func_enter_;
	//RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("free_network==> ssid = %s \n\n" , pnetwork->network.Ssid.Ssid));
	_free_network_nolock(pmlmepriv, pnetwork);
_func_exit_;
}


void free_network_queue(_adapter* dev)
{
_func_enter_;
	_free_network_queue(dev);
_func_exit_;
}

/*
	return the wlan_network with the matching addr

	Shall be calle under atomic context... to avoid possible racing condition...
*/
struct wlan_network* find_network(_queue *scanned_queue, u8 *addr)
{
	struct wlan_network *pnetwork = _find_network(scanned_queue, addr);

	return pnetwork;
}

int is_same_ibss(_adapter *adapter, struct wlan_network *pnetwork)
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

static int is_same_network(NDIS_WLAN_BSSID_EX *src, NDIS_WLAN_BSSID_EX *dst)
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

	_memcpy((u8 *)&s_cap, get_capability_from_ie(src->IEs), 2);
	_memcpy((u8 *)&d_cap, get_capability_from_ie(dst->IEs), 2);

_func_exit_;

	return ((src->Ssid.SsidLength == dst->Ssid.SsidLength) &&
			(src->Configuration.DSConfig == dst->Configuration.DSConfig) &&
			( (_memcmp(src->MacAddress, dst->MacAddress, ETH_ALEN)) == _TRUE) &&
			( (_memcmp(src->Ssid.Ssid, dst->Ssid.Ssid, src->Ssid.SsidLength)) == _TRUE) &&
			((s_cap & WLAN_CAPABILITY_IBSS) ==
			(d_cap & WLAN_CAPABILITY_IBSS)) &&
			((s_cap & WLAN_CAPABILITY_BSS) ==
			(d_cap & WLAN_CAPABILITY_BSS)));

}

struct	wlan_network	* get_oldest_wlan_network(_queue *scanned_queue)
{
	_list	*plist, *phead;


	struct	wlan_network	*pwlan = NULL;
	struct	wlan_network	*oldest = NULL;
_func_enter_;
	phead = get_list_head(scanned_queue);

	plist = get_next(phead);

	while(1)
	{

		if (end_of_queue_search(phead,plist)== _TRUE)
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

static void update_network(NDIS_WLAN_BSSID_EX *dst, NDIS_WLAN_BSSID_EX *src, _adapter *padapter)
{
	u32 last_evm = 0, tmpVal;

_func_enter_;

	//printk("update_network: rssi=0x%lx dst->Rssi=%d ,dst->Rssi=0x%lx , src->Rssi=0x%lx",(dst->Rssi+src->Rssi)/2,dst->Rssi,dst->Rssi,src->Rssi);
	if (check_fwstate(&padapter->mlmepriv, _FW_LINKED) && is_same_network(&(padapter->mlmepriv.cur_network.network), src))
	{
		//printk("b:ssid=%s update_network: src->rssi=0x%d padapter->recvpriv.ui_rssi=%d\n",src->Ssid.Ssid,src->Rssi,padapter->recvpriv.signal);
		if (padapter->recvpriv.signal_qual_data.total_num++ >= PHY_LINKQUALITY_SLID_WIN_MAX)
		{
			padapter->recvpriv.signal_qual_data.total_num = PHY_LINKQUALITY_SLID_WIN_MAX;
			last_evm = padapter->recvpriv.signal_qual_data.elements[padapter->recvpriv.signal_qual_data.index];
			padapter->recvpriv.signal_qual_data.total_val -= last_evm;
		}
		padapter->recvpriv.signal_qual_data.total_val += src->Rssi;

		padapter->recvpriv.signal_qual_data.elements[padapter->recvpriv.signal_qual_data.index++] = src->Rssi;
		if (padapter->recvpriv.signal_qual_data.index >= PHY_LINKQUALITY_SLID_WIN_MAX)
			padapter->recvpriv.signal_qual_data.index = 0;

		//printk("Total SQ=%d  pattrib->signal_qual= %d\n", padapter->recvpriv.signal_qual_data.total_val, src->Rssi);

		// <1> Showed on UI for user, in percentage.
		tmpVal = padapter->recvpriv.signal_qual_data.total_val / padapter->recvpriv.signal_qual_data.total_num;
		padapter->recvpriv.signal = (u8)tmpVal;

		src->Rssi = padapter->recvpriv.signal;
	}
	else {
		//printk("ELSE:ssid=%s update_network: src->rssi=0x%d dst->rssi=%d\n",src->Ssid.Ssid,src->Rssi,dst->Rssi);
		src->Rssi = (src->Rssi + dst->Rssi) / 2;
	}

	//printk("a:update_network: src->rssi=0x%d padapter->recvpriv.ui_rssi=%d\n",src->Rssi,padapter->recvpriv.signal);
	_memcpy((u8*)dst, (u8*)src, get_NDIS_WLAN_BSSID_EX_sz(src));

_func_exit_;
}

static void update_current_network(_adapter *adapter, NDIS_WLAN_BSSID_EX *pnetwork)
{
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;

_func_enter_;

#ifdef PLATFORM_OS_XP
	if ((unsigned long)(&(pmlmepriv->cur_network.network)) < 0x7ffffff)
	{
		KeBugCheckEx(0x87111c1c, (ULONG_PTR)(&(pmlmepriv->cur_network.network)), 0, 0,0);
	}
#endif

	if (is_same_network(&(pmlmepriv->cur_network.network), pnetwork))
	{
		//RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,"Same Network\n");
		update_network(&(pmlmepriv->cur_network.network), pnetwork,adapter);
		update_protection(adapter, (pmlmepriv->cur_network.network.IEs) + sizeof (NDIS_802_11_FIXED_IEs),
							pmlmepriv->cur_network.network.IELength);
	}

_func_exit_;

}

/*

Caller must hold pmlmepriv->lock first.


*/
void update_scanned_network(_adapter *adapter, NDIS_WLAN_BSSID_EX *target)
{
	_list	*plist, *phead;

	ULONG bssid_ex_sz;
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	_queue *queue = &pmlmepriv->scanned_queue;
	struct wlan_network *pnetwork = NULL;
	struct wlan_network *oldest = NULL;

_func_enter_;

	phead = get_list_head(queue);
	plist = get_next(phead);

	while(1)
	{
		if (end_of_queue_search(phead,plist) == _TRUE)
			break;

		pnetwork = LIST_CONTAINOR(plist, struct wlan_network, list);

		if ((unsigned long)(pnetwork) < 0x7ffffff)
		{
#ifdef PLATFORM_OS_XP
			KeBugCheckEx(0x87111c1c, (ULONG_PTR)pnetwork, 0, 0,0);
#endif
		}

		if (is_same_network(&pnetwork->network, target))
			break;

		if ((oldest == ((struct wlan_network *)0)) ||
		    time_after(oldest->last_scanned, pnetwork->last_scanned))
			oldest = pnetwork;

		plist = get_next(plist);
	}


	/* If we didn't find a match, then get a new network slot to initialize
	 * with this beacon's information */
	if (end_of_queue_search(phead,plist) == _TRUE)
	{
		if (_queue_empty(&pmlmepriv->free_bss_pool) == _TRUE) {
			/* If there are no more slots, expire the oldest */
			//list_del_init(&oldest->list);
			pnetwork = oldest;

			//printk("update_network: rssi=0x%lx ,pnetwork->network.Rssi=0x%lx , target->Rssi=0x%lx",(pnetwork->network.Rssi+target->Rssi)/2,pnetwork->network.Rssi,target->Rssi);
			target->Rssi = (pnetwork->network.Rssi + target->Rssi) / 2;
			_memcpy(&pnetwork->network, target, get_NDIS_WLAN_BSSID_EX_sz(target));
			pnetwork->last_scanned = get_current_time();
		} else {
			/* Otherwise just pull from the free list */

			pnetwork = alloc_network(pmlmepriv); // will update scan_time
			if (pnetwork == NULL) {
				RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("!update_scanned_network: something wrong here!!\n"));
				goto exit;
			}

			bssid_ex_sz = get_NDIS_WLAN_BSSID_EX_sz(target);
			target->Length = bssid_ex_sz;

			_memcpy(&pnetwork->network, target, bssid_ex_sz);

			list_insert_tail(&pnetwork->list, &queue->queue);
		}
	} else {
		/* we have an entry and we are going to update it. But this entry may
		 * be already expired. In this case we do the same as we found a new
		 * net and call the new_net handler
		 */
		update_network(&pnetwork->network, target, adapter);

		pnetwork->last_scanned = get_current_time();
	}

exit:

_func_exit_;

}

void rtl8711_add_network(_adapter *adapter, NDIS_WLAN_BSSID_EX *pnetwork)
{
	_irqL irqL;
	struct mlme_priv *pmlmepriv = &(((_adapter *)adapter)->mlmepriv);
	_queue *queue = &pmlmepriv->scanned_queue;

_func_enter_;

	_enter_critical(&queue->lock, &irqL);

	update_current_network(adapter, pnetwork);

	update_scanned_network(adapter, pnetwork);

	_exit_critical(&queue->lock, &irqL);


_func_exit_;
}

//select the desired network based on the capability of the (i)bss.
// check items: (1) security
//			   (2) network_type
//			   (3) WMM
//			   (4) HT
//			   (5) others
int is_desired_network(_adapter *adapter, struct wlan_network *pnetwork)
{
	u8 wps_ie[512];
	uint wps_ielen;
	int bselected = _TRUE;
	struct	security_priv*	 psecuritypriv = &adapter->securitypriv;


	if(psecuritypriv->wps_phase == _TRUE)
	{
		if(get_wps_ie(pnetwork->network.IEs+_FIXED_IE_LENGTH_, pnetwork->network.IELength-_FIXED_IE_LENGTH_, NULL, &wps_ielen)!=NULL)
		{
			return _TRUE;
		}
		else
		{
			return _FALSE;
		}
	}

/*	//for wep,  join bss before setting key

	if (( psecuritypriv->dot11PrivacyAlgrthm == _NO_PRIVACY_ ) &&
	    ( pnetwork->network.Privacy))
	{
		bselected = _FALSE;
	}
*/
	if ( (psecuritypriv->dot11PrivacyAlgrthm != _NO_PRIVACY_ ) &&
		    ( pnetwork->network.Privacy == 0 ) )
	{
		bselected = _FALSE;
	}

	if ( check_fwstate( &adapter->mlmepriv, WIFI_ADHOC_STATE ) == _TRUE )
	{
		if ( pnetwork->network.InfrastructureMode != adapter->mlmepriv.cur_network.network.InfrastructureMode )
			bselected = _FALSE;
	}

	return bselected;
}

/* TODO: Perry : For Power Management */
void atimdone_event_callback(_adapter *adapter , u8 *pbuf)
{
_func_enter_;
	RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("receive atimdone_evet\n"));
_func_exit_;
	return;
}

void survey_event_callback(_adapter *adapter, u8 *pbuf)
{
	_irqL irqL;
	u32 len;
	NDIS_WLAN_BSSID_EX *pnetwork;
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;

_func_enter_;

	pnetwork = (NDIS_WLAN_BSSID_EX *)pbuf;

	//endian_convert
	pnetwork->Length = le32_to_cpu(pnetwork->Length);
	pnetwork->Ssid.SsidLength = le32_to_cpu(pnetwork->Ssid.SsidLength);

	RT_TRACE(_module_rtl871x_mlme_c_,_drv_info_,("survey_event_callback, ssid=%s\n", pnetwork->Ssid.Ssid));

	pnetwork->Privacy = le32_to_cpu(pnetwork->Privacy);
	pnetwork->Rssi = le32_to_cpu(pnetwork->Rssi);
	pnetwork->NetworkTypeInUse = le32_to_cpu(pnetwork->NetworkTypeInUse);
	pnetwork->Configuration.ATIMWindow = le32_to_cpu(pnetwork->Configuration.ATIMWindow);
	pnetwork->Configuration.BeaconPeriod = le32_to_cpu(pnetwork->Configuration.BeaconPeriod);
	pnetwork->Configuration.DSConfig = le32_to_cpu(pnetwork->Configuration.DSConfig);
	pnetwork->Configuration.FHConfig.DwellTime = le32_to_cpu(pnetwork->Configuration.FHConfig.DwellTime);
	pnetwork->Configuration.FHConfig.HopPattern = le32_to_cpu(pnetwork->Configuration.FHConfig.HopPattern);
	pnetwork->Configuration.FHConfig.HopSet = le32_to_cpu(pnetwork->Configuration.FHConfig.HopSet);
	pnetwork->Configuration.FHConfig.Length = le32_to_cpu(pnetwork->Configuration.FHConfig.Length);
	pnetwork->Configuration.Length = le32_to_cpu(pnetwork->Configuration.Length);
	pnetwork->InfrastructureMode = le32_to_cpu(pnetwork->InfrastructureMode);
	pnetwork->IELength = le32_to_cpu(pnetwork->IELength);

	len = get_NDIS_WLAN_BSSID_EX_sz(pnetwork);
	if (len > sizeof(WLAN_BSSID_EX)) {
		RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("!survey_event_callback: return a wrong bss!\n"));
		goto exit;
	}

#ifdef CONFIG_DRVEXT_MODULE
	update_random_seed((void *)(adapter), pnetwork->IEs);
#endif

	_enter_critical(&pmlmepriv->lock, &irqL);

	// update IBSS_network 's timestamp
	if (check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == _TRUE)
	{
		//RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,"survey_event_callback : WIFI_ADHOC_MASTER_STATE \n\n");
		if(_memcmp(&(pmlmepriv->cur_network.network.MacAddress), pnetwork->MacAddress, ETH_ALEN))
		{
			struct wlan_network* ibss_wlan = NULL;

			_memcpy(pmlmepriv->cur_network.network.IEs, pnetwork->IEs, 8);

			ibss_wlan = find_network(&pmlmepriv->scanned_queue, pnetwork->MacAddress);
			if (!ibss_wlan) {
				_memcpy(ibss_wlan->network.IEs , pnetwork->IEs, 8);
				goto exit;
			}
		}
	}

	// lock pmlmepriv->lock when you accessing network_q
	if (check_fwstate(pmlmepriv, _FW_UNDER_LINKING) == _FALSE)
	{
		if (pnetwork->Ssid.Ssid[0] != 0) {
			rtl8711_add_network(adapter, pnetwork);
		} else {
			pnetwork->Ssid.SsidLength = 8;
			_memcpy(pnetwork->Ssid.Ssid, "<hidden>", 8);
			rtl8711_add_network(adapter, pnetwork);
		}
	}

exit:
	_exit_critical(&pmlmepriv->lock, &irqL);

_func_exit_;

	return;
}

void surveydone_event_callback(_adapter *adapter, u8 *pbuf)
{
	_irqL irqL;
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;

#ifdef CONFIG_MLME_EXT

	mlmeext_surveydone_event_callback(adapter);

#endif

_func_enter_;

	_enter_critical(&pmlmepriv->lock, &irqL);

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_notice_, ("+surveydone_event_callback: fw_state=0x%08x\n", pmlmepriv->fw_state));

	if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY) == _TRUE)
	{
		u8 timer_cancelled;

		_cancel_timer(&pmlmepriv->scan_to_timer, &timer_cancelled);

		_clr_fwstate_(pmlmepriv, _FW_UNDER_SURVEY);
	} else {
		RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_, ("!surveydone_event_callback: too late! fw_status=0x%08x\n", pmlmepriv->fw_state));
	}

	if (pmlmepriv->to_join == _TRUE)
	{
		RT_TRACE(_module_rtl871x_mlme_c_, _drv_notice_, ("+surveydone_event_callback: to_join == _TRUE\n"));

		if ((check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == _TRUE))
		{
			if (check_fwstate(pmlmepriv, _FW_LINKED) == _FALSE)
			{
				set_fwstate(pmlmepriv, _FW_UNDER_LINKING);

				if (select_and_join_from_scanned_queue(pmlmepriv)==_SUCCESS) {
					_set_timer(&pmlmepriv->assoc_timer, MAX_JOIN_TIMEOUT);
				}
				else
				{
					WLAN_BSSID_EX *pdev_network = &(adapter->registrypriv.dev_network);
					u8 *pibss = adapter->registrypriv.dev_network.MacAddress;

					pmlmepriv->fw_state ^= _FW_UNDER_SURVEY;//because don't set assoc_timer

					RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("switching to adhoc master\n"));

					_memset(&pdev_network->Ssid, 0, sizeof(NDIS_802_11_SSID));
					_memcpy(&pdev_network->Ssid, &pmlmepriv->assoc_ssid, sizeof(NDIS_802_11_SSID));

					update_registrypriv_dev_network(adapter);
					generate_random_ibss(pibss);

					pmlmepriv->fw_state = WIFI_ADHOC_MASTER_STATE;

					if(createbss_cmd(adapter)!=_SUCCESS)
					{
						RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("Error=>createbss_cmd status FAIL\n"));
					}

					pmlmepriv->to_join = _FALSE;
				}
			}
		}
		else
		{
			pmlmepriv->to_join = _FALSE;
			set_fwstate(pmlmepriv, _FW_UNDER_LINKING);
			if (select_and_join_from_scanned_queue(pmlmepriv) == _SUCCESS) {
				_set_timer(&pmlmepriv->assoc_timer, MAX_JOIN_TIMEOUT);
			} else {
				_clr_fwstate_(pmlmepriv, _FW_UNDER_LINKING);
				RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("try_to_join, but select scanning queue fail\n"));
			}
		}
	}

	_exit_critical(&pmlmepriv->lock, &irqL);
	indicate_wx_scan_complete_event(adapter);

#ifdef CONFIG_IOCTL_CFG80211
	rtw_cfg80211_surveydone_event_callback(adapter);
#endif //CONFIG_IOCTL_CFG80211

_func_exit_;
}

void free_scanqueue(struct mlme_priv *pmlmepriv)
{
	_irqL irqL;
	_queue *free_queue = &pmlmepriv->free_bss_pool;
	_queue *scan_queue = &pmlmepriv->scanned_queue;
	_list *plist, *phead, *ptemp;

_func_enter_;

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_notice_, ("+free_scanqueue\n"));

	_enter_critical(&free_queue->lock, &irqL);

	phead = get_list_head(scan_queue);
	plist = get_next(phead);

	while (plist != phead)
	{
		ptemp = get_next(plist);
		list_delete(plist);
		list_insert_tail(plist, &free_queue->queue);
		plist = ptemp;
		pmlmepriv->num_of_scanned--;
	}

	_exit_critical(&free_queue->lock, &irqL);

_func_exit_;
}

/*
 *free_assoc_resources: the caller has to lock pmlmepriv->lock
 */
void free_assoc_resources(_adapter *adapter)
{
	_irqL irqL;
	struct wlan_network *pwlan = NULL;
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	struct sta_priv *pstapriv = &adapter->stapriv;
	struct wlan_network *tgt_network = &pmlmepriv->cur_network;

_func_enter_;

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_notice_, ("+free_assoc_resources\n"));

	pwlan = find_network(&pmlmepriv->scanned_queue, tgt_network->network.MacAddress);

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_, ("tgt_network->network.MacAddress=%02x:%02x:%02x:%02x:%02x:%02x ssid=%s\n",
		tgt_network->network.MacAddress[0],tgt_network->network.MacAddress[1],
		tgt_network->network.MacAddress[2],tgt_network->network.MacAddress[3],
		tgt_network->network.MacAddress[4],tgt_network->network.MacAddress[5],
		tgt_network->network.Ssid.Ssid));

	if (check_fwstate(pmlmepriv, WIFI_STATION_STATE|WIFI_AP_STATE) == _TRUE)
	{
		struct sta_info* psta;

		psta = get_stainfo(&adapter->stapriv, tgt_network->network.MacAddress);

		_enter_critical(&pstapriv->sta_hash_lock, &irqL);
		free_stainfo(adapter,  psta);
		_exit_critical(&pstapriv->sta_hash_lock, &irqL);
	}

	if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE|WIFI_ADHOC_MASTER_STATE|WIFI_AP_STATE) == _TRUE)
	{
		free_all_stainfo(adapter);
	}

	if (pwlan) {
		pwlan->fixed = _FALSE;
	} else {
		RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("free_assoc_resources: pwlan== NULL\n"));
	}

	if (((check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == _TRUE) && (adapter->stapriv.asoc_sta_count== 1))
		/*|| (check_fwstate(pmlmepriv, WIFI_STATION_STATE) == _TRUE)*/)
	{
		free_network_nolock(pmlmepriv, pwlan);
	}

_func_exit_;
}

/*
*indicate_connect: the caller has to lock pmlmepriv->lock
*/
void indicate_connect(_adapter *padapter)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

_func_enter_;
	RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_, ("+indicate_connect\n"));

	pmlmepriv->to_join = _FALSE;

	set_fwstate(pmlmepriv, _FW_LINKED);

	padapter->ledpriv.LedControlHandler(padapter, LED_CTL_LINK);

	os_indicate_connect(padapter);

#ifdef CONFIG_PWRCTRL
	if (padapter->registrypriv.power_mgnt > PS_MODE_ACTIVE) {
		_set_timer(&pmlmepriv->dhcp_timer, 60000);
	}
#endif

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_, ("-indicate_connect: fw_state=0x%08x\n", get_fwstate(pmlmepriv)));

_func_exit_;
}


/*
*indicate_connect: the caller has to lock pmlmepriv->lock
*/
void indicate_disconnect( _adapter *padapter )
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

_func_enter_;

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_, ("+indicate_disconnect\n"));

	
	if(check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE)
	{
		_clr_fwstate_(pmlmepriv, _FW_LINKED);

		padapter->ledpriv.LedControlHandler(padapter, LED_CTL_NO_LINK);

		os_indicate_disconnect(padapter);
	}

	_cancel_timer_ex(&pmlmepriv->survey_timer);

#ifdef CONFIG_PWRCTRL
	if(padapter->pwrctrlpriv.pwr_mode != padapter->registrypriv.power_mgnt){
		_cancel_timer_ex(&pmlmepriv->dhcp_timer);
		set_ps_mode(padapter, padapter->registrypriv.power_mgnt, padapter->registrypriv.smart_ps);
	}
#endif

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_, ("-indicate_disconnect: fw_state=0x%08x\n", get_fwstate(pmlmepriv)));

_func_exit_;
}

inline void rtw_indicate_scan_done( _adapter *padapter, bool aborted)
{
	rtw_os_indicate_scan_done(padapter, aborted);
}

#if 0
void joinbss_event_callback(_adapter *adapter, u8 *pbuf)
{
	_irqL irqL,irqL2;
	int	res;
	u8 timer_cancelled;
	struct sta_info *ptarget_sta= NULL, *pcur_sta = NULL;
	struct	sta_priv *pstapriv = &adapter->stapriv;
	struct	mlme_priv	*pmlmepriv = &(adapter->mlmepriv);
	struct wlan_network 	*pnetwork	= (struct wlan_network *)pbuf;
	struct wlan_network 	*cur_network = &(pmlmepriv->cur_network);
	struct wlan_network	*pcur_wlan = NULL, *ptarget_wlan = NULL;
	unsigned int 		the_same_macaddr = _FALSE;

_func_enter_;

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
	pnetwork->network.Configuration.DSConfig =le32_to_cpu(pnetwork->network.Configuration.DSConfig);
	pnetwork->network.Configuration.FHConfig.DwellTime=le32_to_cpu(pnetwork->network.Configuration.FHConfig.DwellTime);
	pnetwork->network.Configuration.FHConfig.HopPattern=le32_to_cpu(pnetwork->network.Configuration.FHConfig.HopPattern);
	pnetwork->network.Configuration.FHConfig.HopSet=le32_to_cpu(pnetwork->network.Configuration.FHConfig.HopSet);
	pnetwork->network.Configuration.FHConfig.Length=le32_to_cpu(pnetwork->network.Configuration.FHConfig.Length);
	pnetwork->network.Configuration.Length = le32_to_cpu(pnetwork->network.Configuration.Length);
	pnetwork->network.InfrastructureMode = le32_to_cpu(pnetwork->network.InfrastructureMode);
	pnetwork->network.IELength = le32_to_cpu(pnetwork->network.IELength );

	RT_TRACE(_module_rtl871x_mlme_c_,_drv_info_,("joinbss event call back received with res=%d\n", pnetwork->join_res));

	get_encrypt_decrypt_from_registrypriv(adapter);

#ifdef CONFIG_MLME_EXT

	if(pnetwork->join_res > 0)
	{
		mlmeext_joinbss_event_callback(adapter, pnetwork);
	}

#endif


	if (pmlmepriv->assoc_ssid.SsidLength == 0)
	{
		RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("@@@@@   joinbss event call back  for Any SSid\n"));
	}
	else
	{
		RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("@@@@@   joinbss_event_callback for SSid:%s\n", pmlmepriv->assoc_ssid.Ssid));
	}

	the_same_macaddr = _memcmp(pnetwork->network.MacAddress, cur_network->network.MacAddress, ETH_ALEN);

	_enter_critical(&pmlmepriv->lock, &irqL);

	RT_TRACE(_module_rtl871x_mlme_c_,_drv_info_,("\n joinbss_event_callback !! remove spinlock \n"));

	if (pnetwork->join_res > 0)
	{
		cur_network->join_res = pnetwork->join_res;

		if ((pmlmepriv->fw_state) & _FW_UNDER_LINKING)
		{

			pnetwork->network.Length = get_NDIS_WLAN_BSSID_EX_sz(&pnetwork->network);
			if(pnetwork->network.Length > sizeof(WLAN_BSSID_EX))
			{
				RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("\n\n ***joinbss_evt_callback return a wrong bss ***\n\n"));
				goto ignore_joinbss_callback;
			}

			if((pmlmepriv->fw_state) & _FW_LINKED)
			{
				if(the_same_macaddr == _TRUE)
				{
					ptarget_wlan = find_network(&pmlmepriv->scanned_queue, cur_network->network.MacAddress);

					//update network in pscanned_q
					//_memcpy(&(ptarget_wlan->network), &pnetwork->network, pnetwork->network.Length);//removed
					ptarget_sta = get_stainfo(pstapriv, pnetwork->network.MacAddress);
				}
				else
				{
					pcur_wlan = find_network(&pmlmepriv->scanned_queue, cur_network->network.MacAddress);
					pcur_wlan->fixed = _FALSE;

					ptarget_wlan = find_network(&pmlmepriv->scanned_queue, pnetwork->network.MacAddress);

					// update network in pscanned_q
					//_memcpy(&(ptarget_wlan->network), &pnetwork->network, pnetwork->network.Length);//removed
					ptarget_wlan->fixed = _TRUE;

					pcur_sta = get_stainfo(pstapriv, cur_network->network.MacAddress);
					_enter_critical(&(pstapriv->sta_hash_lock), &irqL2);
					free_stainfo(adapter,  pcur_sta);
					_exit_critical(&(pstapriv->sta_hash_lock), &irqL2);

					ptarget_sta = alloc_stainfo(&adapter->stapriv, pnetwork->network.MacAddress);
				}

			}
			else
			{
				ptarget_wlan = find_network(&pmlmepriv->scanned_queue, pnetwork->network.MacAddress);
				if(ptarget_wlan)
				{
					RT_TRACE(_module_rtl871x_mlme_c_,_drv_info_,("\nfw_state:%x, BSSID:%x:%x:%x:%x:%x:%x (fw_state=%d)\n",
						pnetwork->network.MacAddress[0], pnetwork->network.MacAddress[1],
						pnetwork->network.MacAddress[2], pnetwork->network.MacAddress[3],
						pnetwork->network.MacAddress[4], pnetwork->network.MacAddress[5],
						pmlmepriv->fw_state));
				}

				//update network in pscanned_q
				ptarget_wlan->fixed = _TRUE;
				if(check_fwstate(pmlmepriv, WIFI_STATION_STATE) == _TRUE)
				{
					ptarget_sta = alloc_stainfo(&adapter->stapriv, pnetwork->network.MacAddress);
					if(ptarget_sta == NULL)
					{
						RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("#########Can't allocate  network when joinbss_event callback\n"));
						goto ignore_joinbss_callback;
					}

					if(adapter->securitypriv.dot11AuthAlgrthm== 2)
					{
						ptarget_sta->ieee8021x_blocked=_TRUE;
						ptarget_sta->dot118021XPrivacy=adapter->securitypriv.dot11PrivacyAlgrthm;
						adapter->securitypriv.binstallGrpkey=_FALSE;
						adapter->securitypriv.busetkipkey=_FALSE;

						RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("\n=====adapter->securitypriv.busetkipkey=_FALSE=====\n"));
						adapter->securitypriv.bgrpkey_handshake=_FALSE;
						_memset((u8 *)&ptarget_sta->dot118021x_UncstKey, 0, sizeof (union Keytype));
						_memset((u8 *)&ptarget_sta->dot11tkiprxmickey, 0, sizeof (union pn48));
						_memset((u8 *)&ptarget_sta->dot11tkiptxmickey, 0, sizeof (union pn48));
					}

				}
			}

			if(ptarget_sta)
			{
				ptarget_sta->aid  = pnetwork->join_res;

				ptarget_sta->qos_option = 1;//?

				if(check_fwstate( pmlmepriv, WIFI_STATION_STATE) == _TRUE)
					ptarget_sta->mac_id = 5;
			}
			else
			{
				RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("ptarget_sta==NULL\n\n"));
			}


			if(ptarget_wlan == NULL){
				RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("Can't allocate  network when joinbss_event callback\n"));
				goto ignore_joinbss_callback;
			}

			if(check_fwstate( pmlmepriv, WIFI_STATION_STATE) == _TRUE)
			{
				if(ptarget_sta == NULL)
				{
					RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("Can't allocate  stainfo when joinbss_event callback\n"));
					goto ignore_joinbss_callback;
				}
			}

			//update cur_network
			_memcpy(&cur_network->network, &pnetwork->network, pnetwork->network.Length);
			cur_network->aid = pnetwork->join_res;

			// update fw_state //will clr _FW_UNDER_LINKING here indirectly
			switch(pnetwork->network.InfrastructureMode)
			{
				case Ndis802_11Infrastructure:
					pmlmepriv->fw_state = WIFI_STATION_STATE;
					break;

				case Ndis802_11IBSS:
					pmlmepriv->fw_state = WIFI_ADHOC_STATE;
					break;

				default:
					RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("Invalid network_mode\n"));
					break;
			}


			RT_TRACE(_module_rtl871x_mlme_c_,_drv_info_,("before indicate connect fw_state:%x",pmlmepriv->fw_state));

			RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("\n BSSID=0x%02x:0x%2x:0x%2x:0x%2x:0x%2x:0x%2x\n",
						pmlmepriv->cur_network.network.MacAddress[0],pmlmepriv->cur_network.network.MacAddress[1],
						pmlmepriv->cur_network.network.MacAddress[2],pmlmepriv->cur_network.network.MacAddress[3],
						pmlmepriv->cur_network.network.MacAddress[4],pmlmepriv->cur_network.network.MacAddress[5]));

			if(check_fwstate( pmlmepriv, WIFI_STATION_STATE) == _TRUE)
			{
				RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("\n joinbss_event_callback:indicate_connect  \n"));

				RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("\n joinbss_event_callback:adapter->securitypriv.dot11AuthAlgrthm = %d adapter->securitypriv.ndisauthtype=%d\n",
							adapter->securitypriv.dot11AuthAlgrthm, adapter->securitypriv.ndisauthtype));

				if(adapter->securitypriv.dot11AuthAlgrthm== 2)
				{
					if(ptarget_sta!=NULL)
					{
						u8 null_key[16]={0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0};
						ptarget_sta->ieee8021x_blocked = _TRUE;
						ptarget_sta->dot118021XPrivacy =adapter->securitypriv.dot11PrivacyAlgrthm;
						_memcpy(&ptarget_sta->dot11tkiptxmickey.skey[0], null_key, 16);

					}

					adapter->securitypriv.binstallGrpkey=_FALSE;
					adapter->securitypriv.bgrpkey_handshake=_FALSE;
				}

				indicate_connect(adapter);

				update_protection(adapter, (cur_network->network.IEs) + sizeof (NDIS_802_11_FIXED_IEs),
									(cur_network->network.IELength));

				//TODO: update HT_Capability


			}

			//adhoc mode will indicate_connect when stassoc_event_callback

		}
		else
		{
			RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("joinbss_event_callback err: fw_state:%x", pmlmepriv->fw_state));
			goto ignore_joinbss_callback;
		}

		RT_TRACE(_module_rtl871x_mlme_c_,_drv_info_,("Cancle assoc_timer \n"));
		_cancel_timer(&pmlmepriv->assoc_timer, &timer_cancelled);


	}
	else //if join_res < 0 (join fails), then try again
	{
		res = select_and_join_from_scanned_queue(pmlmepriv);
		RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("select_and_join_from_scanned_queue again! res:%d\n",res));
		if (res != _SUCCESS)
		{
			RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("Set Assoc_Timer = 1; can't find match ssid in scanned_q \n"));

			_set_timer(&pmlmepriv->assoc_timer, 1);

			//free_assoc_resources(adapter);

			if((check_fwstate(pmlmepriv, _FW_UNDER_LINKING)) == _TRUE)
			{
				RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("fail! clear _FW_UNDER_LINKING ^^^fw_state=%x\n", pmlmepriv->fw_state));
				pmlmepriv->fw_state ^= _FW_UNDER_LINKING;
			}

		}
		else
		{
			//todo: extend time of assoc_timer
		}

	}

ignore_joinbss_callback:

	_exit_critical(&pmlmepriv->lock, &irqL);

_func_exit_;

}
#else
//Notes:
//pnetwork : returns from joinbss_event_callback
//ptarget_wlan: found from scanned_queue
//if join_res > 0, for (fw_state==WIFI_STATION_STATE), we check if  "ptarget_sta" & "ptarget_wlan" exist.
//if join_res > 0, for (fw_state==WIFI_ADHOC_STATE), we only check if "ptarget_wlan" exist.
//if join_res > 0, update "cur_network->network" from "pnetwork->network" if (ptarget_wlan !=NULL).
//
void joinbss_event_callback(_adapter *adapter, u8 *pbuf)
{
	_irqL irqL, irqL2;
	int res;
	u8 timer_cancelled;
	struct sta_info	*ptarget_sta = NULL, *pcur_sta = NULL;
	struct sta_priv	*pstapriv = &adapter->stapriv;
	struct mlme_priv	*pmlmepriv = &adapter->mlmepriv;
	struct wlan_network	*pnetwork = (struct wlan_network *)pbuf;
	struct wlan_network	*cur_network = &pmlmepriv->cur_network;
	struct wlan_network	*pcur_wlan = NULL, *ptarget_wlan = NULL;
	unsigned int		the_same_macaddr = _FALSE;
#ifdef CONFIG_DRVEXT_MODULE
	int enable_wpa = 0, enable_wsc = 0;
	struct drvext_priv *pdrvext = &adapter->drvextpriv;
#endif

_func_enter_;

	//endian_convert
	pnetwork->join_res = le32_to_cpu(pnetwork->join_res);
	pnetwork->network_type = le32_to_cpu(pnetwork->network_type);
	pnetwork->network.Length = le32_to_cpu(pnetwork->network.Length);
	pnetwork->network.Ssid.SsidLength = le32_to_cpu(pnetwork->network.Ssid.SsidLength);
	pnetwork->network.Privacy = le32_to_cpu(pnetwork->network.Privacy);
	pnetwork->network.Rssi = le32_to_cpu(pnetwork->network.Rssi);
	pnetwork->network.NetworkTypeInUse = le32_to_cpu(pnetwork->network.NetworkTypeInUse);
	pnetwork->network.Configuration.ATIMWindow = le32_to_cpu(pnetwork->network.Configuration.ATIMWindow);
	pnetwork->network.Configuration.BeaconPeriod = le32_to_cpu(pnetwork->network.Configuration.BeaconPeriod);
	pnetwork->network.Configuration.DSConfig = le32_to_cpu(pnetwork->network.Configuration.DSConfig);
	pnetwork->network.Configuration.FHConfig.DwellTime = le32_to_cpu(pnetwork->network.Configuration.FHConfig.DwellTime);
	pnetwork->network.Configuration.FHConfig.HopPattern = le32_to_cpu(pnetwork->network.Configuration.FHConfig.HopPattern);
	pnetwork->network.Configuration.FHConfig.HopSet = le32_to_cpu(pnetwork->network.Configuration.FHConfig.HopSet);
	pnetwork->network.Configuration.FHConfig.Length = le32_to_cpu(pnetwork->network.Configuration.FHConfig.Length);
	pnetwork->network.Configuration.Length = le32_to_cpu(pnetwork->network.Configuration.Length);
	pnetwork->network.InfrastructureMode = le32_to_cpu(pnetwork->network.InfrastructureMode);
	pnetwork->network.IELength = le32_to_cpu(pnetwork->network.IELength);

	RT_TRACE(_module_rtl871x_mlme_c_,_drv_info_,("+joinbss event call back: received with res=%d\n", pnetwork->join_res));

	get_encrypt_decrypt_from_registrypriv(adapter);

#ifdef CONFIG_MLME_EXT

	if (pnetwork->join_res > 0) {
		mlmeext_joinbss_event_callback(adapter, pnetwork);
	}

#endif

	if (pmlmepriv->assoc_ssid.SsidLength == 0) {
		RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("@@@@@   joinbss event call back for Any SSid\n"));
	} else {
		RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("@@@@@   joinbss_event_callback for SSid:%s\n", pmlmepriv->assoc_ssid.Ssid));
	}

	the_same_macaddr = _memcmp(pnetwork->network.MacAddress, cur_network->network.MacAddress, ETH_ALEN);

	pnetwork->network.Length = get_NDIS_WLAN_BSSID_EX_sz(&pnetwork->network);
	if (pnetwork->network.Length > sizeof(WLAN_BSSID_EX))
	{
		RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("***joinbss_evt_callback return a wrong bss ***\n"));
		goto ignore_joinbss_callback;
	}

	_enter_critical(&pmlmepriv->lock, &irqL);

	RT_TRACE(_module_rtl871x_mlme_c_,_drv_info_,("joinbss_event_callback: _enter_critical\n"));

	if (pnetwork->join_res > 0)
	{
		if (check_fwstate(pmlmepriv, _FW_UNDER_LINKING) == _TRUE)
		{
			//s1. find ptarget_wlan
			if (check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE)
			{
				if (the_same_macaddr == _TRUE) {
					ptarget_wlan = find_network(&pmlmepriv->scanned_queue, cur_network->network.MacAddress);
				} else {
					pcur_wlan = find_network(&pmlmepriv->scanned_queue, cur_network->network.MacAddress);
					pcur_wlan->fixed = _FALSE;

					pcur_sta = get_stainfo(pstapriv, cur_network->network.MacAddress);
					_enter_critical(&pstapriv->sta_hash_lock, &irqL2);
					free_stainfo(adapter, pcur_sta);
					_exit_critical(&(pstapriv->sta_hash_lock), &irqL2);

					ptarget_wlan = find_network(&pmlmepriv->scanned_queue, pnetwork->network.MacAddress);
					if (ptarget_wlan) ptarget_wlan->fixed = _TRUE;
				}
			} else {
				ptarget_wlan = find_network(&pmlmepriv->scanned_queue, pnetwork->network.MacAddress);
				if (ptarget_wlan) ptarget_wlan->fixed = _TRUE;
			}

			if (ptarget_wlan == NULL)
			{
				RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("Can't find ptarget_wlan when joinbss_event callback\n"));

				if (check_fwstate(pmlmepriv, _FW_UNDER_LINKING) == _TRUE) {
					RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_, ("joinbss_event_callback: clear _FW_UNDER_LINKING fw_state=%x\n", pmlmepriv->fw_state));
					pmlmepriv->fw_state ^= _FW_UNDER_LINKING;
				}

				goto ignore_joinbss_callback;
			}

			//s2. find ptarget_sta & update ptarget_sta
			if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) == _TRUE)
			{
				if (the_same_macaddr == _TRUE) {
					ptarget_sta = get_stainfo(pstapriv, pnetwork->network.MacAddress);
					if (ptarget_sta == NULL) {
						ptarget_sta = alloc_stainfo(pstapriv, pnetwork->network.MacAddress);
					}
				} else {
					ptarget_sta = alloc_stainfo(pstapriv, pnetwork->network.MacAddress);
				}

				if (ptarget_sta) //update ptarget_sta
				{
					ptarget_sta->aid = pnetwork->join_res;
					ptarget_sta->qos_option = 1;//?
					ptarget_sta->mac_id = 5;

					if (adapter->securitypriv.dot11AuthAlgrthm == 2)
					{
						adapter->securitypriv.binstallGrpkey = _FALSE;
						adapter->securitypriv.busetkipkey = _FALSE;
						adapter->securitypriv.bgrpkey_handshake = _FALSE;

						ptarget_sta->ieee8021x_blocked = _TRUE;
						ptarget_sta->dot118021XPrivacy = adapter->securitypriv.dot11PrivacyAlgrthm;

						_memset((u8 *)&ptarget_sta->dot118021x_UncstKey, 0, sizeof (union Keytype));

						_memset((u8 *)&ptarget_sta->dot11tkiprxmickey, 0, sizeof (union Keytype));
						_memset((u8 *)&ptarget_sta->dot11tkiptxmickey, 0, sizeof (union Keytype));

						_memset((u8 *)&ptarget_sta->dot11txpn, 0, sizeof (union pn48));
						_memset((u8 *)&ptarget_sta->dot11rxpn, 0, sizeof (union pn48));
					}
				}
				else
				{
					RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("Can't allocate stainfo when joinbss_event callback\n"));

					if (check_fwstate(pmlmepriv, _FW_UNDER_LINKING) == _TRUE) {
						RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_, ("joinbss_event_callback: clear _FW_UNDER_LINKING fw_state=%x\n", pmlmepriv->fw_state));
						pmlmepriv->fw_state ^= _FW_UNDER_LINKING;
					}

					goto ignore_joinbss_callback;
				}
			}

			//s3. update cur_network & indicate connect
			//if (ptarget_wlan)	// check above
			{
				RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_,
					 ("fw_state:%x, BSSID:%02x:%02x:%02x:%02x:%02x:%02x\n",
						pmlmepriv->fw_state,
						pnetwork->network.MacAddress[0], pnetwork->network.MacAddress[1],
						pnetwork->network.MacAddress[2], pnetwork->network.MacAddress[3],
						pnetwork->network.MacAddress[4], pnetwork->network.MacAddress[5]));

				_memcpy(&cur_network->network, &pnetwork->network, pnetwork->network.Length);
				cur_network->aid = pnetwork->join_res;

				//update fw_state //will clr _FW_UNDER_LINKING here indirectly
				switch (pnetwork->network.InfrastructureMode)
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

				update_protection(adapter, (cur_network->network.IEs) + sizeof (NDIS_802_11_FIXED_IEs),
									(cur_network->network.IELength));

#ifdef CONFIG_80211N_HT
				//TODO: update HT_Capability
				update_ht_cap(adapter, cur_network->network.IEs, cur_network->network.IELength);
#endif

				//indicate connect
				if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) == _TRUE)
				{
#ifdef CONFIG_DRVEXT_MODULE
					if (pdrvext->enable_wpa)
					{
						//Added by Albert 2008/10/16 for TKIP countermeasure.
						pdrvext->wpa_tkip_mic_error_occur_time = 0;
						pdrvext->wpa_tkip_countermeasure_enable = 0;
						_memset(pdrvext->wpa_tkip_countermeasure_blocked_bssid, 0x00, ETH_ALEN );

						res = drvext_l2_connect_callback(adapter);

						if (res == L2_CONNECTED)
						{
							indicate_connect(adapter);
						}
						else if (res == L2_DISCONNECTED)
						{
							goto select_and_join_new_bss;
						}
						else if (res == L2_PENDING)
						{
							//DEBUG_ERR(("Going for WPA module\n"));
							pdrvext->wpasm.rx_replay_counter_set = 0;
							enable_wpa = 1;
						}
						else if (res == L2_WSC_PENDING)
						{
							//DEBUG_ERR(("Going for WSC module\n"));
							enable_wsc = 1;
						}
					}
					else
#endif
					{
						indicate_connect(adapter);
					}
				}
				else
				{
					//adhoc mode will indicate_connect when stassoc_event_callback
					RT_TRACE(_module_rtl871x_mlme_c_,_drv_info_,("adhoc mode, fw_state:%x", pmlmepriv->fw_state));
				}
			}

#ifdef CONFIG_DRVEXT_MODULE
			if (enable_wpa)
			{
				_set_timer(&pmlmepriv->assoc_timer, MAX_JOIN_TIMEOUT);
				//DEBUG_ERR(("@@ Set Assoc Timer [%x] for WPA@@\n"));
			}
			else
#endif
			{
				RT_TRACE(_module_rtl871x_mlme_c_,_drv_info_,("Cancle assoc_timer\n"));
				_cancel_timer(&pmlmepriv->assoc_timer, &timer_cancelled);
			}
		}
		else
		{
			RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_,
				 ("joinbss_event_callback: ERROR! fw_state=0x%08x\n",
				  pmlmepriv->fw_state));
			goto ignore_joinbss_callback;
		}

	}
	else //if join_res < 0 (join fails)
	{

#ifdef CONFIG_DRVEXT_MODULE

select_and_join_new_bss:

		//drvext_assoc_fail_indicate(adapter);
#endif

		if (check_fwstate(pmlmepriv, _FW_UNDER_LINKING) == _TRUE)
		{
				_set_timer(&pmlmepriv->assoc_timer, 1);
				_clr_fwstate_(pmlmepriv, _FW_UNDER_LINKING);
		}
	}

ignore_joinbss_callback:

	_exit_critical(&pmlmepriv->lock, &irqL);

#ifdef CONFIG_DRVEXT_MODULE_WSC

	if (enable_wsc)
	{
		// here we start registration protocol
		start_reg_protocol(adapter);
	}

#endif

_func_exit_;

}
#endif

void stassoc_event_callback(_adapter *adapter, u8 *pbuf)
{
	_irqL irqL;
	struct sta_info *psta;
	struct mlme_priv *pmlmepriv = &(adapter->mlmepriv);
	struct stassoc_event *pstassoc	= (struct stassoc_event*)pbuf;

_func_enter_;

	// to do:
	if(access_ctrl(&adapter->acl_list, pstassoc->macaddr) == _FALSE)
		return;

	psta = get_stainfo(&adapter->stapriv, pstassoc->macaddr);
	if( psta != NULL)
	{
		//the sta have been in sta_info_queue => do nothing

		RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("Error: stassoc_event_callback: sta has been in sta_hash_queue \n"));

		goto exit; //(between drv has received this event before and  fw have not yet to set key to CAM_ENTRY)
	}

	psta = alloc_stainfo(&adapter->stapriv, pstassoc->macaddr);
	if (psta == NULL) {
		RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("Can't alloc sta_info when stassoc_event_callback\n"));
		goto exit;
	}

	//to do : init sta_info variable
	psta->qos_option = 0;
	psta->mac_id = le32_to_cpu((uint)pstassoc->cam_id);
	//psta->aid = (uint)pstassoc->cam_id;

	if(adapter->securitypriv.dot11AuthAlgrthm==2)
		psta->dot118021XPrivacy = adapter->securitypriv.dot11PrivacyAlgrthm;

	psta->ieee8021x_blocked = _FALSE;

	_enter_critical(&pmlmepriv->lock, &irqL);

	if ( (check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE)==_TRUE ) ||
		(check_fwstate(pmlmepriv, WIFI_ADHOC_STATE)==_TRUE ) )
	{
		if(adapter->stapriv.asoc_sta_count== 2)
		{
			// a sta + bc/mc_stainfo (not Ibss_stainfo)
			indicate_connect(adapter);
		}
	}

	_exit_critical(&pmlmepriv->lock, &irqL);

#ifdef CONFIG_RTL8711
	//submit SetStaKey_cmd to tell fw, fw will allocate an CAM entry for this sta
	setstakey_cmd(adapter, (unsigned char*)psta, _FALSE);
#endif

exit:

_func_exit_;

}

void stadel_event_callback(_adapter *adapter, u8 *pbuf)
{
	_irqL irqL,irqL2;
	struct sta_info *psta;
	struct wlan_network *pwlan = NULL;
	WLAN_BSSID_EX *pdev_network = NULL;
	u8 *pibss = NULL;
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	struct stadel_event *pstadel = (struct stadel_event*)pbuf;
	struct sta_priv *pstapriv = &adapter->stapriv;
	struct wlan_network *tgt_network = &pmlmepriv->cur_network;

_func_enter_;

	_enter_critical(&pmlmepriv->lock, &irqL2);

	if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) == _TRUE)
	{
		indicate_disconnect(adapter);
		free_assoc_resources(adapter);

		pwlan = find_network(&pmlmepriv->scanned_queue, tgt_network->network.MacAddress);
		if (pwlan)
		{
			pwlan->fixed = _FALSE;
			free_network_nolock(pmlmepriv, pwlan);
		}		
	}

	if (check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE|WIFI_ADHOC_STATE) == _TRUE)
	{
		psta = get_stainfo(&adapter->stapriv, pstadel->macaddr);

		_enter_critical(&pstapriv->sta_hash_lock, &irqL);

		free_stainfo(adapter, psta);

		_exit_critical(&pstapriv->sta_hash_lock, &irqL);

		if (adapter->stapriv.asoc_sta_count == 1) //a sta + bc/mc_stainfo (not Ibss_stainfo)
		{
			//indicate_disconnect(adapter);//removed@20091105

			//free old ibss network
			//pwlan = find_network(&pmlmepriv->scanned_queue, pstadel->macaddr);
			pwlan = find_network(&pmlmepriv->scanned_queue, tgt_network->network.MacAddress);
			if (pwlan)
			{
				pwlan->fixed = _FALSE;
				free_network_nolock(pmlmepriv, pwlan);
			}

			//re-create ibss
			pdev_network = &(adapter->registrypriv.dev_network);
			pibss = adapter->registrypriv.dev_network.MacAddress;

			_memcpy(pdev_network, &tgt_network->network, get_NDIS_WLAN_BSSID_EX_sz(&tgt_network->network));

			_memset(&pdev_network->Ssid, 0, sizeof(NDIS_802_11_SSID));
			_memcpy(&pdev_network->Ssid, &pmlmepriv->assoc_ssid, sizeof(NDIS_802_11_SSID));

			update_registrypriv_dev_network(adapter);

			generate_random_ibss(pibss);

			if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == _TRUE)
			{
				_clr_fwstate_(pmlmepriv, WIFI_ADHOC_STATE);
				set_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE);
			}

			if (createbss_cmd(adapter) != _SUCCESS)
			{
				RT_TRACE(_module_rtl871x_ioctl_set_c_, _drv_err_, ("***Error=>stadel_event_callback: createbss_cmd status FAIL*** \n "));
			}
		}
	}

	_exit_critical(&pmlmepriv->lock, &irqL2);

_func_exit_;

}


void cpwm_event_callback(_adapter *adapter, u8 *pbuf)
{
	struct reportpwrstate_parm *preportpwrstate = (struct reportpwrstate_parm *)pbuf;

_func_enter_;

	RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("cpwm_event_callback !!!\n"));
#ifdef CONFIG_PWRCTRL
	preportpwrstate->state |= (u8)(adapter->pwrctrlpriv.cpwm_tog + 0x80);
	cpwm_int_hdl(adapter, preportpwrstate);
#endif

_func_exit_;

}

//	Commented by Albert 20100527
//	When the Netgear 3500 AP is with WPA2PSK-AES mode, it will send the ADDBA req frame with 
//	start seq control = 0 to wifi client after the WPA handshake and the seqence number of following data packet will be 0. 
//	In this case, the Rx reorder sequence is not longer than 0 and the WiFi client will drop the data with seq number 0.
//	So, the 8712 firmware has to inform driver with receiving the ADDBA-Req frame so that the driver can reset the 
//	sequence value of Rx reorder contorl.

void got_addbareq_event_callback(_adapter *adapter, u8 *pbuf)
{
	struct		ADDBA_Req_Report_parm*	pAddbareq_pram = ( struct ADDBA_Req_Report_parm* ) pbuf;
	struct		sta_info*					psta;
	struct		sta_priv*					pstapriv = &adapter->stapriv;
	struct		recv_reorder_ctrl*			precvreorder_ctrl = NULL;
	struct registry_priv *pregistrypriv = &adapter->registrypriv;

_func_enter_;	
	RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("got_addbareq_event_callback!!!\n"));
	printk( "[%s] mac = %X %X %X %X %X %X, seq = %d, tid = %d\n", __FUNCTION__,
			pAddbareq_pram->MacAddress[0], pAddbareq_pram->MacAddress[1], pAddbareq_pram->MacAddress[2],
			pAddbareq_pram->MacAddress[3], pAddbareq_pram->MacAddress[4], pAddbareq_pram->MacAddress[5], 
			pAddbareq_pram->StartSeqNum, pAddbareq_pram->tid);
	
	psta = get_stainfo(pstapriv, pAddbareq_pram->MacAddress);
	if ( psta )
	{
		precvreorder_ctrl = &psta->recvreorder_ctrl[pAddbareq_pram->tid];	
		// set the indicate_seq to 0xffff so that the rx reorder can store any following data packet.
		if (pregistrypriv->wifi_test == 1)
		{
			precvreorder_ctrl->enable = _FALSE;
		}
		else
		{
			precvreorder_ctrl->enable = _TRUE;
		}
		precvreorder_ctrl->indicate_seq = 0xffff;
	}
_func_exit_;
}

void wpspbc_event_callback(_adapter *adapter, u8 *pbuf)
{
_func_enter_;

	RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("wpspbc_event_callback !!!\n"));

	if(adapter->securitypriv.wps_hw_pbc_pressed == _FALSE){

		RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("wpspbc_event_callback - PBC is pressed !!!\n"));

		adapter->securitypriv.wps_hw_pbc_pressed = _TRUE;
		//_set_workitem(&adapter->mlmepriv.hw_pbc_workitem);
	}

_func_exit_;
}

void survey_timer_event_callback(PADAPTER padapter, u8 *pbuf)
{
	struct survey_timer_event *ptimer = (struct survey_timer_event*)pbuf;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;


	pmlmepriv->survey_interval = ptimer->timeout * 1000;

	if (pmlmepriv->survey_interval == 0)
		_cancel_timer_ex(&pmlmepriv->survey_timer);
	else
		_set_timer(&pmlmepriv->survey_timer, pmlmepriv->survey_interval);
}

void _sitesurvey_ctrl_handler(_adapter *adapter)
{
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	struct sitesurvey_ctrl	*psitesurveyctrl = &pmlmepriv->sitesurveyctrl;
	struct registry_priv	*pregistrypriv = &adapter->registrypriv;
	u64 current_tx_pkts;
	uint current_rx_pkts;

_func_enter_;

	current_tx_pkts = (adapter->xmitpriv.tx_pkts)-(psitesurveyctrl->last_tx_pkts);
	current_rx_pkts = (adapter->recvpriv.rx_pkts)-(psitesurveyctrl->last_rx_pkts);

	psitesurveyctrl->last_tx_pkts = adapter->xmitpriv.tx_pkts;
	psitesurveyctrl->last_rx_pkts = adapter->recvpriv.rx_pkts;

	if ((current_tx_pkts > pregistrypriv->busy_thresh) ||
	    (current_rx_pkts > pregistrypriv->busy_thresh))
	{
		psitesurveyctrl->traffic_busy = _TRUE;
	}
	else
	{
		psitesurveyctrl->traffic_busy = _FALSE;
	}

_func_exit_;

}

void _join_timeout_handler(_adapter *adapter)
{
	_irqL irqL;
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;

#if 0
	if (adapter->bDriverStopped == _TRUE){
		_up_sema(&pmlmepriv->assoc_terminate);
		return;
	}
#endif

_func_enter_;

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_, ("+join_timeout_handler: fw_state=0x%08x\n", pmlmepriv->fw_state));

	if (adapter->bDriverStopped || adapter->bSurpriseRemoved)
		return;

	_enter_critical(&pmlmepriv->lock, &irqL);

	_clr_fwstate_(pmlmepriv, _FW_UNDER_LINKING);
	pmlmepriv->to_join = _FALSE;

//	if (check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE) {
		os_indicate_disconnect(adapter);
		_clr_fwstate_(pmlmepriv, _FW_LINKED);
//	}

//	free_scanqueue(pmlmepriv);// for join fail, don't join again

#ifdef CONFIG_PWRCTRL
	if (adapter->pwrctrlpriv.pwr_mode != adapter->registrypriv.power_mgnt) {
		set_ps_mode(adapter, adapter->registrypriv.power_mgnt, adapter->registrypriv.smart_ps);
	}
#endif

	_exit_critical(&pmlmepriv->lock, &irqL);

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_notice_, ("-join_timeout_handler: fw_state=0x%08x\n", pmlmepriv->fw_state));

_func_exit_;

}

void scan_timeout_handler (_adapter *adapter)
{
	_irqL irqL;
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_, ("+scan_timeout_handler: fw_state=0x%08x\n", pmlmepriv->fw_state));

	_enter_critical(&pmlmepriv->lock, &irqL);

	if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY) == _FALSE) {
		RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_, ("-scan_timeout_handler: too late! fw_status=0x%08x\n", pmlmepriv->fw_state));
	}
	_clr_fwstate_(pmlmepriv, _FW_UNDER_SURVEY);

	pmlmepriv->to_join = _FALSE;	// scan fail, so clear to_join flag

	_exit_critical(&pmlmepriv->lock, &irqL);
}

void _dhcp_timeout_handler (_adapter *adapter)
{
_func_enter_;

	RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("+_dhcp_timeout_handler\n"));
	if (adapter->bDriverStopped || adapter->bSurpriseRemoved) {
		return;
	}
#ifdef CONFIG_PWRCTRL
	if (adapter->pwrctrlpriv.pwr_mode != adapter->registrypriv.power_mgnt) {
		set_ps_mode(adapter, adapter->registrypriv.power_mgnt, adapter->registrypriv.smart_ps);
	}
#endif
_func_exit_;
}

void _regular_site_survey_handler (PADAPTER padapter)
{
	_irqL irqL;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;


	if ((padapter->bDriverStopped == _TRUE) ||
	    (padapter->bSurpriseRemoved == _TRUE) ||
	    (padapter->bup == _FALSE))
	{
		pmlmepriv->survey_interval = 0;
		return;
	}

	_enter_critical(&pmlmepriv->lock, &irqL);
	if ((check_fwstate(pmlmepriv, _FW_UNDER_LINKING) == _FALSE) &&
	    (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY) == _FALSE) &&
	    (pmlmepriv->sitesurveyctrl.traffic_busy == _FALSE))
	{
		sitesurvey_cmd(padapter, NULL);
	}
	_exit_critical(&pmlmepriv->lock, &irqL);

	if (pmlmepriv->survey_interval)
		_set_timer(&pmlmepriv->survey_timer, pmlmepriv->survey_interval);
}

void _wdg_timeout_handler(_adapter *adapter)
{
	wdg_wk_cmd(adapter);
}

/*void _hw_pbc_timeout_handler(_adapter *adapter)
{
	// cancel flag for next push button event
	adapter->securitypriv.wps_hw_pbc_pressed = _FALSE;
}*/

/*
Calling context:
The caller of the sub-routine will be in critical section...

The caller must hold the following spinlock

pmlmepriv->lock

*/
int select_and_join_from_scanned_queue(struct mlme_priv *pmlmepriv)
{
	_list *phead;
	unsigned char *dst_ssid, *src_ssid;
	_adapter *adapter;
	_queue *queue = NULL;
	struct wlan_network *pnetwork = NULL;
	struct wlan_network *pnetwork_max_rssi = NULL;

_func_enter_;

	adapter = (_adapter*)pmlmepriv->nic_hdl;
	queue = &pmlmepriv->scanned_queue;
	phead = get_list_head(queue);
	pmlmepriv->pscanned = get_next(phead);

	while (1)
	{
		if (end_of_queue_search(phead, pmlmepriv->pscanned) == _TRUE)
		{
			if ((pmlmepriv->assoc_by_rssi == _TRUE) &&
			    (pnetwork_max_rssi != NULL))
			{
				pnetwork = pnetwork_max_rssi;
				goto ask_for_joinbss;
			}

			RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("!select_and_join_from_scanned_queue: FAIL(end of queue)\n"));
			return _FAIL;
		}

		pnetwork = LIST_CONTAINOR(pmlmepriv->pscanned, struct wlan_network, list);
		if (pnetwork == NULL) {
			RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("select_and_join_from_scanned_queue: FAIL(pnetwork==NULL)\n"));
			return _FAIL;
		}

		pmlmepriv->pscanned = get_next(pmlmepriv->pscanned);

		if (pmlmepriv->assoc_by_bssid == _TRUE)
		{
			dst_ssid = pnetwork->network.MacAddress;
			src_ssid = pmlmepriv->assoc_bssid;

			if (_memcmp(dst_ssid, src_ssid, ETH_ALEN) == _TRUE)
			{
				//remove the condition @ 20081125
				//if((pmlmepriv->cur_network.network.InfrastructureMode==Ndis802_11AutoUnknown)||
				//	pmlmepriv->cur_network.network.InfrastructureMode == pnetwork->network.InfrastructureMode)
				//		goto ask_for_joinbss;

				if (check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE)
				{
					if (is_same_network(&pmlmepriv->cur_network.network, &pnetwork->network))
					{
						//DBG_8712("select_and_join(1): _FW_LINKED and is same network, it needn't join again\n");

						_clr_fwstate_(pmlmepriv, _FW_UNDER_LINKING);

						indicate_connect(adapter);//indicate_connect again

						return 2;
					}

					disassoc_cmd(adapter);
					indicate_disconnect(adapter);
					free_assoc_resources(adapter);
				}

				goto ask_for_joinbss;
			}
		}
		else if (pmlmepriv->assoc_ssid.SsidLength == 0) {
			goto ask_for_joinbss;//anyway, join first selected(dequeued) pnetwork if ssid_len=0
		}

		dst_ssid = pnetwork->network.Ssid.Ssid;
		src_ssid = pmlmepriv->assoc_ssid.Ssid;

		if ((pnetwork->network.Ssid.SsidLength == pmlmepriv->assoc_ssid.SsidLength) &&
		    (_memcmp(dst_ssid, src_ssid, pmlmepriv->assoc_ssid.SsidLength) == _TRUE))
		{
			RT_TRACE(_module_rtl871x_mlme_c_, _drv_notice_,
				 ("select_and_join_from_scanned_queue: dst=[%s] src=[%s]\n",
				  dst_ssid, src_ssid));

			//remove the condition @ 20081125
			//if((pmlmepriv->cur_network.network.InfrastructureMode==Ndis802_11AutoUnknown)||
			//	pmlmepriv->cur_network.network.InfrastructureMode == pnetwork->network.InfrastructureMode)
			//{
			//	_memcpy(pmlmepriv->assoc_bssid, pnetwork->network.MacAddress, ETH_ALEN);
			//	goto ask_for_joinbss;
			//}

			if (pmlmepriv->assoc_by_rssi == _TRUE)//if the ssid is the same, select the bss which has the max rssi
			{
				if (pnetwork_max_rssi) {
					if (pnetwork->network.Rssi > pnetwork_max_rssi->network.Rssi)
						pnetwork_max_rssi = pnetwork;
				} else {
					pnetwork_max_rssi = pnetwork;
				}
			}
			else if (is_desired_network(adapter, pnetwork) == _TRUE)
			{
				if (check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE)
				{
#if 0
					if(is_same_network(&pmlmepriv->cur_network.network, &pnetwork->network))
					{
						DBG_8712("select_and_join(2): _FW_LINKED and is same network, it needn't join again\n");

						indicate_connect(adapter);//indicate_connect again

						return 2;
					}
					else
#endif
					{
						disassoc_cmd(adapter);
						//indicate_disconnect(adapter);
						free_assoc_resources(adapter);
					}
				}

				goto ask_for_joinbss;
			}
		}
 	}

_func_exit_;

	return _FAIL;

ask_for_joinbss:

_func_exit_;
	return joinbss_cmd(adapter, pnetwork);
}

sint set_auth(_adapter *adapter, struct security_priv *psecuritypriv)
{
	struct cmd_priv	*pcmdpriv = &adapter->cmdpriv;
	struct cmd_obj *pcmd;
	struct setauth_parm *psetauthparm;
	sint ret = _SUCCESS;

_func_enter_;

	pcmd = (struct cmd_obj*)_malloc(sizeof(struct cmd_obj));
	if (pcmd == NULL) {
		ret = _FAIL;
		goto exit;
	}

	psetauthparm = (struct setauth_parm*)_malloc(sizeof(struct setauth_parm));
	if (psetauthparm == NULL) {
		_mfree((unsigned char *)pcmd, sizeof(struct cmd_obj));
		ret = _FAIL;
		goto exit;
	}

	_memset(psetauthparm, 0, sizeof(struct setauth_parm));
	psetauthparm->mode = (u8)psecuritypriv->dot11AuthAlgrthm;

	pcmd->cmdcode = _SetAuth_CMD_;
	pcmd->parmbuf = (unsigned char *)psetauthparm;
	pcmd->cmdsz = sizeof(struct setauth_parm);
	pcmd->rsp = NULL;
	pcmd->rspsz = 0;

	_init_listhead(&pcmd->list);

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_,
		 ("set_auth: after enqueue, auth_mode=%d\n", psecuritypriv->dot11AuthAlgrthm));

	enqueue_cmd(pcmdpriv, pcmd);

exit:

_func_exit_;

	return ret;
}

sint set_key(_adapter *adapter, struct security_priv *psecuritypriv, sint keyid)
{
	struct cmd_priv *pcmdpriv = &adapter->cmdpriv;
	struct cmd_obj *pcmd;
	struct setkey_parm *psetkeyparm;
	u8 keylen;
	sint res = _SUCCESS;

_func_enter_;

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_notice_,
		 ("+set_key: dot11AuthAlgrthm=%d dot11PrivacyAlgrthm=%d dot118021XGrpPrivacy=%d\n",
		  psecuritypriv->dot11AuthAlgrthm, psecuritypriv->dot11PrivacyAlgrthm, psecuritypriv->dot118021XGrpPrivacy));

	pcmd = (struct cmd_obj*)_malloc(sizeof(struct cmd_obj));
	if (pcmd == NULL) {
		res = _FAIL;
		goto exit;
	}

	psetkeyparm = (struct setkey_parm*)_malloc(sizeof(struct setkey_parm));
	if (psetkeyparm == NULL) {
		_mfree((unsigned char*)pcmd, sizeof(struct cmd_obj));
		res = _FAIL;
		goto exit;
	}
	_memset(psetkeyparm, 0, sizeof(struct setkey_parm));

	if (psecuritypriv->dot11AuthAlgrthm == 2) { // 802.1X
		psetkeyparm->algorithm = (u8)psecuritypriv->dot118021XGrpPrivacy;
		RT_TRACE(_module_rtl871x_mlme_c_, _drv_notice_, ("set_key: algorithm=dot118021XGrpPrivacy=%d\n", psetkeyparm->algorithm));
	} else { // WEP
		psetkeyparm->algorithm = (u8)psecuritypriv->dot11PrivacyAlgrthm;
		RT_TRACE(_module_rtl871x_mlme_c_, _drv_notice_, ("set_key: algorithm=dot11PrivacyAlgrthm=%d\n", psetkeyparm->algorithm));
	}
	psetkeyparm->keyid = (u8)keyid;
	RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_, ("set_key: algorithm=[%d](1(WEP)/2(TKIP)/4(AES)/5(WEP104) keyid=%d\n", psetkeyparm->algorithm, keyid));

	switch (psetkeyparm->algorithm)
	{
		case _WEP40_:
			keylen = 5;
			_memcpy(psetkeyparm->key, psecuritypriv->dot11DefKey[keyid].skey, keylen);
			break;
		case _WEP104_:
			keylen = 13;
			_memcpy(psetkeyparm->key, psecuritypriv->dot11DefKey[keyid].skey, keylen);
			break;
		case _TKIP_:
			keylen = 16;
			_memcpy(psetkeyparm->key, &psecuritypriv->dot118021XGrpKey[keyid-1], keylen);
			psetkeyparm->grpkey = 1;
			break;
		case _AES_:
			keylen = 16;
			_memcpy(psetkeyparm->key, &psecuritypriv->dot118021XGrpKey[keyid-1], keylen);
			psetkeyparm->grpkey = 1;
			break;
		default:
			RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("set_key: algoritm=%d Error!)\n", psetkeyparm->algorithm));
			res = _FAIL;
			goto exit;
	}

	pcmd->cmdcode = _SetKey_CMD_;
	pcmd->parmbuf = (u8 *)psetkeyparm;
	pcmd->cmdsz =  (sizeof(struct setkey_parm));
	pcmd->rsp = NULL;
	pcmd->rspsz = 0;

	_init_listhead(&pcmd->list);

	//_init_sema(&(pcmd->cmd_sem), 0);

	enqueue_cmd(pcmdpriv, pcmd);

exit:

_func_exit_;

	return _SUCCESS;
}

//adjust IEs for joinbss_cmd in WMM
int restruct_wmm_ie(_adapter *adapter, u8 *in_ie, u8 *out_ie, uint in_len, uint initial_out_len)
{
	unsigned int ielength = 0;
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
//		-1		:if there is no pre-auth key in the  table
//		>=0		:if there is pre-auth key, and   return the entry id
//
//
int SecIsInPMKIDList(_adapter *Adapter, u8 *bssid)
{
	struct security_priv *psecuritypriv = &Adapter->securitypriv;
	int i = 0;

	do {
		if (psecuritypriv->PMKIDList[i].bUsed &&
		    (_memcmp(psecuritypriv->PMKIDList[i].Bssid, bssid, ETH_ALEN) == _TRUE)) {
			break;
		} else {
			i++;
			//continue;
		}
	} while (i < NUM_PMKID_CACHE);

	if (i == NUM_PMKID_CACHE) {
		i = -1;// Could not find.
	} else {
		// There is one Pre-Authentication Key for the specific BSSID.
	}

	return i;
}

sint restruct_sec_ie(_adapter *adapter, u8 *in_ie, u8 *out_ie, uint in_len)
{
	u8 authmode = 0, securitytype, match;
	u8 sec_ie[255], uncst_oui[4], bkup_ie[255];
	u8 wpa_oui[4] = {0x0, 0x50, 0xf2, 0x01};
	uint ielength, cnt, remove_cnt;
	int iEntry;

	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	struct security_priv *psecuritypriv = &adapter->securitypriv;
	uint ndisauthmode = psecuritypriv->ndisauthtype;
	uint ndissecuritytype = psecuritypriv->ndisencryptstatus;

_func_enter_;

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_notice_,
		 ("+restruct_sec_ie: ndisauthmode=%d ndissecuritytype=%d\n",
		  ndisauthmode, ndissecuritytype));

	if ((ndisauthmode==Ndis802_11AuthModeWPA)||(ndisauthmode==Ndis802_11AuthModeWPAPSK))
	{
		authmode = _WPA_IE_ID_;
		uncst_oui[0] = 0x0;
		uncst_oui[1] = 0x50;
		uncst_oui[2] = 0xf2;
	}
	if ((ndisauthmode==Ndis802_11AuthModeWPA2)||(ndisauthmode==Ndis802_11AuthModeWPA2PSK))
	{
		authmode = _WPA2_IE_ID_;
		uncst_oui[0] = 0x0;
		uncst_oui[1] = 0x0f;
		uncst_oui[2] = 0xac;
	}

	switch (ndissecuritytype)
	{
		case Ndis802_11Encryption1Enabled:
		case Ndis802_11Encryption1KeyAbsent:
			securitytype = _WEP40_;
			uncst_oui[3] = 0x1;
			break;
		case Ndis802_11Encryption2Enabled:
		case Ndis802_11Encryption2KeyAbsent:
			securitytype = _TKIP_;
			uncst_oui[3] = 0x2;
			break;
		case Ndis802_11Encryption3Enabled:
		case Ndis802_11Encryption3KeyAbsent:
			securitytype = _AES_;
			uncst_oui[3] = 0x4;
			break;
		default:
			securitytype = _NO_PRIVACY_;
			break;
	}

	//Search required WPA or WPA2 IE and copy to sec_ie[ ]
	cnt = 12;
	match = _FALSE;
	while (cnt < in_len)
	{
		if (in_ie[cnt] == authmode)
		{
			if ((authmode==_WPA_IE_ID_)&&(_memcmp(&in_ie[cnt+2], &wpa_oui[0], 4)==_TRUE))
			{
				_memcpy(&sec_ie[0], &in_ie[cnt], in_ie[cnt+1]+2);
				match = _TRUE;
				RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("restruct_sec_ie: Get WPA IE from %d in_len=%d \n",cnt,in_len));
				break;
			}
			if (authmode == _WPA2_IE_ID_)
			{
				_memcpy(&sec_ie[0], &in_ie[cnt], in_ie[cnt+1]+2);
				match = _TRUE;
				RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("restruct_sec_ie: Get WPA2 IE from %d in_len=%d \n",cnt,in_len));
				break;
			}
			if (((authmode==_WPA_IE_ID_)&&(_memcmp(&in_ie[cnt+2], &wpa_oui[0], 4)==_TRUE))||(authmode==_WPA2_IE_ID_))
			{
				_memcpy(&bkup_ie[0], &in_ie[cnt], in_ie[cnt+1]+2);
				RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("restruct_sec_ie: cnt=%d in_len=%d \n",cnt,in_len));
			}
		}

		cnt += in_ie[cnt+1] + 2; //get next
	}

	//restruct WPA IE or WPA2 IE in sec_ie[ ]
	if (match == _TRUE)
	{
		if(sec_ie[0]==_WPA_IE_ID_)
		{
			// parsing SSN IE to select required encryption algorithm, and set the bc/mc encryption algorithm
			while (_TRUE)
			{
				if(_memcmp(&sec_ie[2], &wpa_oui[0], 4) !=_TRUE)//check wpa_oui tag
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

				if(_memcmp(&sec_ie[8], &wpa_oui[0], 3) ==_TRUE)
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
					if(_memcmp(&sec_ie[14], &uncst_oui[0], 4) !=_TRUE)
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
					_memcpy(&sec_ie[14], &uncst_oui[0], 4);

					//remove the other unicast suit
					_memcpy(&sec_ie[18], &sec_ie[18+remove_cnt],(sec_ie[1]-18+2-remove_cnt));
					sec_ie[1]=sec_ie[1]-remove_cnt;
				}

				break;
			}
		}

		if (authmode == _WPA2_IE_ID_)
		{
			// parsing RSN IE to select required encryption algorithm, and set the bc/mc encryption algorithm
			while (_TRUE)
			{
				if((sec_ie[2]!=0x01)||(sec_ie[3]!= 0x0))
				{
					//IE Ver error
					RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("\n RSN IE :IE version error (%.2x %.2x != 01 00 )! \n",sec_ie[2],sec_ie[3]));
					match=_FALSE;
					break;
				}

				if(_memcmp(&sec_ie[4], &uncst_oui[0], 3) ==_TRUE)
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
					if(_memcmp(&sec_ie[10], &uncst_oui[0],4) !=_TRUE)
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
					_memcpy( &sec_ie[10] , &uncst_oui[0],4 );

					//remove the other unicast suit
					_memcpy(&sec_ie[14],&sec_ie[14+remove_cnt],(sec_ie[1]-14+2-remove_cnt));
					sec_ie[1]=sec_ie[1]-remove_cnt;
				}

				break;
			}
		}

	}

	if(psecuritypriv->wps_phase == _TRUE)
	{
		//DBG_8712("wps_phase == _TRUE\n");
		_memcpy(out_ie, in_ie, 12);
		ielength=12;

		//Commented by Kurt 20110629
		//In some older APs, WPS handshake
		//would be failed if we append vender extensions informations to AP
		_memcpy(out_ie+ielength, psecuritypriv->wps_ie, 14+2);
		*( out_ie + ielength + 1 ) = 14;
		ielength += 14+2;

		psecuritypriv->wps_phase == _FALSE;
	}
	else if((authmode==_WPA_IE_ID_)||(authmode==_WPA2_IE_ID_))
	{
		//copy fixed ie
		_memcpy(out_ie, in_ie,12);
		ielength=12;

		//copy RSN or SSN
		if(match ==_TRUE)
		{
#ifdef CONFIG_IOCTL_CFG80211
			//Commented by Kurt 20120308
			//In nl80211, WPA IE length in EAPOL_KEY data would only be 22
			//So we tailored the last two byte when WPA IE length is 24
			if( ( authmode==_WPA_IE_ID_ ) && ( sec_ie[1] == 24 ) )
			{
				out_ie[ielength] = sec_ie[0];
				out_ie[ielength+1] = sec_ie[1]-2;
				_memcpy(&out_ie[ielength+2], &sec_ie[2], sec_ie[1]-2);
				ielength+=sec_ie[1];
			}
			else
#endif //CONFIG_IOCTL_CFG80211
			{
				_memcpy(&out_ie[ielength], &sec_ie[0], sec_ie[1]+2);
				ielength+=sec_ie[1]+2;

				if(authmode==_WPA2_IE_ID_)
				{
					//the Pre-Authentication bit should be zero, john
					out_ie[ielength-1]= 0;
					out_ie[ielength-2]= 0;
				}
			}

			report_sec_ie(adapter, authmode, sec_ie);

#ifdef CONFIG_DRVEXT_MODULE
			drvext_report_sec_ie(&adapter->drvextpriv, authmode, sec_ie);
#endif

		}

	}
	else
	{
		//_memcpy(&out_ie[0], &in_ie[0], in_len);
		//ielength=in_len;

		//copy fixed ie only
		_memcpy(out_ie, in_ie, 12);
		ielength=12;

		if(psecuritypriv->wps_phase == _TRUE)
		{
			//DBG_8712("wps_phase == _TRUE\n");

			_memcpy(out_ie+ielength, psecuritypriv->wps_ie, psecuritypriv->wps_ie_len);

			ielength += psecuritypriv->wps_ie_len;
		}
	}

	iEntry = SecIsInPMKIDList(adapter, pmlmepriv->assoc_bssid);
	if(iEntry<0)
	{
		return ielength;
	}
	else
	{
		if (authmode == _WPA2_IE_ID_)
		{
			out_ie[ielength]=1;
			ielength++;
			out_ie[ielength]=0;	//PMKID count = 0x0100
			ielength++;
			_memcpy(&out_ie[ielength], &psecuritypriv->PMKIDList[iEntry].PMKID, 16);

			ielength += 16;
			out_ie[13] += 18;//PMKID length = 2+16
		}
	}

	//report_sec_ie(adapter, authmode, sec_ie);

_func_exit_;

	return ielength;
}

void init_registrypriv_dev_network(_adapter *adapter)
{
	struct registry_priv *pregistrypriv = &adapter->registrypriv;
	struct eeprom_priv *peepriv = &adapter->eeprompriv;
	WLAN_BSSID_EX *pdev_network = &pregistrypriv->dev_network;
	u8 *myhwaddr = myid(peepriv);

_func_enter_;

	_memcpy(pdev_network->MacAddress, myhwaddr, ETH_ALEN);

	_memcpy(&pdev_network->Ssid, &pregistrypriv->ssid, sizeof(NDIS_802_11_SSID));

	pdev_network->Configuration.Length=sizeof(NDIS_802_11_CONFIGURATION);
	pdev_network->Configuration.BeaconPeriod = 100;
	pdev_network->Configuration.FHConfig.Length = 0;
	pdev_network->Configuration.FHConfig.HopPattern = 0;
	pdev_network->Configuration.FHConfig.HopSet = 0;
	pdev_network->Configuration.FHConfig.DwellTime = 0;


_func_exit_;

}

void update_registrypriv_dev_network(_adapter* adapter)
{
	int sz = 0;
	struct registry_priv	*pregistrypriv = &adapter->registrypriv;
	WLAN_BSSID_EX		*pdev_network = &pregistrypriv->dev_network;
	struct security_priv	*psecuritypriv = &adapter->securitypriv;
	struct wlan_network	*cur_network = &adapter->mlmepriv.cur_network;
	struct xmit_priv	*pxmitpriv = &adapter->xmitpriv;

_func_enter_;

#if 0
	pxmitpriv->vcs_setting = pregistrypriv->vrtl_carrier_sense;
	pxmitpriv->vcs = pregistrypriv->vcs_type;
	pxmitpriv->vcs_type = pregistrypriv->vcs_type;
	pxmitpriv->rts_thresh = pregistrypriv->rts_thresh;
	pxmitpriv->frag_len = pregistrypriv->frag_thresh;

	adapter->qospriv.qos_option = pregistrypriv->wmm_enable;
#endif

	pdev_network->Privacy = cpu_to_le32(psecuritypriv->dot11PrivacyAlgrthm > 0 ? 1 : 0) ; // adhoc no 802.1x

	pdev_network->Rssi = 0;

	switch (pregistrypriv->wireless_mode)
	{
		case WIRELESS_11B:
			pdev_network->NetworkTypeInUse = cpu_to_le32(Ndis802_11DS);
			break;
		case WIRELESS_11G:
		case WIRELESS_11BG:
			pdev_network->NetworkTypeInUse = cpu_to_le32(Ndis802_11OFDM24);
			break;
		case WIRELESS_11A:
			pdev_network->NetworkTypeInUse = cpu_to_le32(Ndis802_11OFDM5);
			break;
		default :
			// TODO
			break;
	}

	pdev_network->Configuration.DSConfig = cpu_to_le32(pregistrypriv->channel);
	RT_TRACE(_module_rtl871x_mlme_c_,_drv_info_,("pregistrypriv->channel=%d, pdev_network->Configuration.DSConfig=0x%x\n", pregistrypriv->channel, pdev_network->Configuration.DSConfig));

	if (cur_network->network.InfrastructureMode == Ndis802_11IBSS)
		pdev_network->Configuration.ATIMWindow = cpu_to_le32(3);

	pdev_network->InfrastructureMode = cpu_to_le32(cur_network->network.InfrastructureMode);

	// 1. Supported rates
	// 2. IE

	//set_supported_rate(pdev_network->SupportedRates, pregistrypriv->wireless_mode) ; // will be called in generate_ie
	sz = generate_ie(pregistrypriv);

	pdev_network->IELength = sz;

	pdev_network->Length = get_NDIS_WLAN_BSSID_EX_sz((NDIS_WLAN_BSSID_EX *)pdev_network);

	//notes: translate IELength & Length after assign the Length to cmdsz in createbss_cmd();
	//pdev_network->IELength = cpu_to_le32(sz);

_func_exit_;

}

void get_encrypt_decrypt_from_registrypriv(	_adapter* adapter)
{
	u16	wpaconfig=0;
	struct registry_priv* pregistrypriv = &adapter->registrypriv;
	struct security_priv* psecuritypriv= &adapter->securitypriv;
_func_enter_;


_func_exit_;

}

//the fucntion is at passive_level
void joinbss_reset(_adapter *padapter)
{
	int i;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct registry_priv *pregistrypriv = &padapter->registrypriv;

#ifdef CONFIG_80211N_HT
	struct ht_priv		*phtpriv = &pmlmepriv->htpriv;
#endif

	//todo: if you want to do something io/reg/hw setting before join_bss, please add code here




#ifdef CONFIG_80211N_HT

	phtpriv->ampdu_enable = _FALSE;//reset to disabled

	for(i=0; i<16; i++)
	{
		phtpriv->baddbareq_issued[i] = _FALSE;//reset it
	}

	if(phtpriv->ht_option)
	{

#ifdef CONFIG_USB_HCI
		//validate  usb rx aggregation
		//printk("joinbss_reset():validate  usb rx aggregation\n");
		if (pregistrypriv->wifi_test == 1)
		{
			write8(padapter, 0x102500D9, 1);// TH=1 => means that invalidate usb rx aggregation
		}
		else
		{
		write8(padapter, 0x102500D9, 48);//TH = 48 pages, 6k
		}
#endif

	}
	else
	{

#ifdef CONFIG_USB_HCI
	//invalidate  usb rx aggregation
	write8(padapter, 0x102500D9, 1);// TH=1 => means that invalidate usb rx aggregation
#endif

	}

#endif

}


#ifdef CONFIG_80211N_HT

//the fucntion is >= passive_level
unsigned int restructure_ht_ie(_adapter *padapter, u8 *in_ie, u8 *out_ie, uint in_len, uint *pout_len)
{
	u32 ielen, out_len;
	unsigned char *p, *pframe;
	struct rtw_ieee80211_ht_cap ht_capie;
	unsigned char WMM_IE[] = {0x00, 0x50, 0xf2, 0x02, 0x00, 0x01, 0x00};
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct qos_priv   	*pqospriv= &pmlmepriv->qospriv;
	struct ht_priv		*phtpriv = &pmlmepriv->htpriv;

	phtpriv->ht_option = 0;

	p = get_ie(in_ie+12, _HT_CAPABILITY_IE_, &ielen, in_len-12);

	if (p && (ielen>0))
	{
		if (pqospriv->qos_option == 0)
		{
			out_len = *pout_len;
			pframe = set_ie(out_ie+out_len, _VENDOR_SPECIFIC_IE_,
								_WMM_IE_Length_, WMM_IE, pout_len);

			pqospriv->qos_option = 1;
		}

		out_len = *pout_len;

		_memset(&ht_capie, 0, sizeof(struct rtw_ieee80211_ht_cap));

		ht_capie.cap_info = IEEE80211_HT_CAP_SUP_WIDTH |IEEE80211_HT_CAP_SGI_20 |
							IEEE80211_HT_CAP_SGI_40 | IEEE80211_HT_CAP_TX_STBC |
							IEEE80211_HT_CAP_MAX_AMSDU | IEEE80211_HT_CAP_DSSSCCK40;

		ht_capie.ampdu_params_info = (IEEE80211_HT_CAP_AMPDU_FACTOR&0x03) |
										(IEEE80211_HT_CAP_AMPDU_DENSITY&0x00) ;

		pframe = set_ie(out_ie+out_len, _HT_CAPABILITY_IE_,
							sizeof(struct rtw_ieee80211_ht_cap), (unsigned char*)&ht_capie, pout_len);

		//_memcpy(out_ie+out_len, p, ielen+2);//gtest
		//*pout_len = *pout_len + (ielen+2);

		phtpriv->ht_option = 1;
	}

	return (phtpriv->ht_option);
}

//the fucntion is > passive_level (in critical_section)
void update_ht_cap(_adapter *padapter, u8 *pie, uint ie_len)
{
	u8 *p, max_ampdu_sz;
	int i, len;
	struct sta_info *bmc_sta, *psta;
	struct rtw_ieee80211_ht_cap *pht_capie;
	struct ieee80211_ht_addt_info *pht_addtinfo;
	struct recv_reorder_ctrl *preorder_ctrl;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct ht_priv		*phtpriv = &pmlmepriv->htpriv;
	struct recv_priv *precvpriv = &padapter->recvpriv;
	struct registry_priv *pregistrypriv = &padapter->registrypriv;
	struct wlan_network *pcur_network = &(pmlmepriv->cur_network);


	if(!phtpriv->ht_option)
		return;


	//printk("+update_ht_cap()\n");

	//maybe needs check if ap supports rx ampdu.
	if((phtpriv->ampdu_enable==_FALSE) &&(pregistrypriv->ampdu_enable==1))
	{
		if (pregistrypriv->wifi_test == 1)
		{
			phtpriv->ampdu_enable = _FALSE;
		}
		else
		{	
			phtpriv->ampdu_enable = _TRUE;
		}
	}


	//check Max Rx A-MPDU Size
	len = 0;
	p = get_ie(pie+sizeof (NDIS_802_11_FIXED_IEs), _HT_CAPABILITY_IE_, &len, ie_len-sizeof (NDIS_802_11_FIXED_IEs));
	if(p && len>0)
	{
		pht_capie = (struct rtw_ieee80211_ht_cap *)(p+2);
		max_ampdu_sz = (pht_capie->ampdu_params_info & IEEE80211_HT_CAP_AMPDU_FACTOR);
		max_ampdu_sz = 1 << (max_ampdu_sz+3); // max_ampdu_sz (kbytes);

		//printk("update_ht_cap(): max_ampdu_sz=%d\n", max_ampdu_sz);
		phtpriv->rx_ampdu_maxlen = max_ampdu_sz;

	}

	//for A-MPDU Rx reordering buffer control for bmc_sta & sta_info
	//if A-MPDU Rx is enabled, reseting  rx_ordering_ctrl wstart_b(indicate_seq) to default value=0xffff
	//todo: check if AP can send A-MPDU packets
	bmc_sta = get_bcmc_stainfo(padapter);
	if(bmc_sta)
	{
		for(i=0; i < 16 ; i++)
		{
			//preorder_ctrl = &precvpriv->recvreorder_ctrl[i];
			preorder_ctrl = &bmc_sta->recvreorder_ctrl[i];
			preorder_ctrl->indicate_seq = 0xffff;
			preorder_ctrl->wend_b= 0xffff;
			preorder_ctrl->enable = _FALSE;
			//preorder_ctrl->wsize_b = max_ampdu_sz;//ex. 32(kbytes) -> wsize_b=32
		}
	}

	psta = get_stainfo(&padapter->stapriv, pcur_network->network.MacAddress);
	if(psta)
	{
		for(i=0; i < 16 ; i++)
		{
			//preorder_ctrl = &precvpriv->recvreorder_ctrl[i];
			preorder_ctrl = &psta->recvreorder_ctrl[i];
			preorder_ctrl->indicate_seq = 0xffff;
			preorder_ctrl->wend_b= 0xffff;
			preorder_ctrl->enable = _FALSE;
			//preorder_ctrl->wsize_b = max_ampdu_sz;//ex. 32(kbytes) -> wsize_b=32
		}
	}

	len=0;
	p = get_ie(pie+sizeof (NDIS_802_11_FIXED_IEs), _HT_ADD_INFO_IE_, &len, ie_len-sizeof (NDIS_802_11_FIXED_IEs));
	if(p && len>0)
	{
		pht_addtinfo = (struct ieee80211_ht_addt_info *)(p+2);
	}

}

void issue_addbareq_cmd(_adapter *padapter, int priority)
{
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct ht_priv	 *phtpriv = &pmlmepriv->htpriv;

	if((phtpriv->ht_option==1) && (phtpriv->ampdu_enable==_TRUE))
	{
		if(phtpriv->baddbareq_issued[priority] == _FALSE)
		{
			addbareq_cmd(padapter,(u8) priority);

			phtpriv->baddbareq_issued[priority] = _TRUE;
		}
	}

}

#endif

