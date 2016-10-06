/******************************************************************************
 * rtl871x_mlme.c
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
 * Linux device driver for RTL8192SU
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * Modifications for inclusion into the Linux staging tree are
 * Copyright(c) 2010 Larry Finger. All rights reserved.
 *
 * Contact information:
 * WLAN FAE <wlanfae@realtek.com>
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 ******************************************************************************/

#define _RTL871X_MLME_C_

#include <linux/etherdevice.h>

#include "osdep_service.h"
#include "drv_types.h"
#include "recv_osdep.h"
#include "xmit_osdep.h"
#include "mlme_osdep.h"
#include "sta_info.h"
#include "wifi.h"
#include "wlan_bssdef.h"

static void update_ht_cap(struct _adapter *padapter, u8 *pie, uint ie_len);

static sint _init_mlme_priv(struct _adapter *padapter)
{
	sint	i;
	u8	*pbuf;
	struct wlan_network	*pnetwork;
	struct	mlme_priv *pmlmepriv = &padapter->mlmepriv;

	memset((u8 *)pmlmepriv, 0, sizeof(struct mlme_priv));
	pmlmepriv->nic_hdl = (u8 *)padapter;
	pmlmepriv->pscanned = NULL;
	pmlmepriv->fw_state = 0;
	pmlmepriv->cur_network.network.InfrastructureMode =
				 Ndis802_11AutoUnknown;
	/* Maybe someday we should rename this variable to "active_mode"(Jeff)*/
	pmlmepriv->passive_mode = 1; /* 1: active, 0: passive. */
	spin_lock_init(&(pmlmepriv->lock));
	spin_lock_init(&(pmlmepriv->lock2));
	_init_queue(&(pmlmepriv->free_bss_pool));
	_init_queue(&(pmlmepriv->scanned_queue));
	set_scanned_network_val(pmlmepriv, 0);
	memset(&pmlmepriv->assoc_ssid, 0, sizeof(struct ndis_802_11_ssid));
	pbuf = kmalloc_array(MAX_BSS_CNT, sizeof(struct wlan_network),
			     GFP_ATOMIC);
	if (!pbuf)
		return _FAIL;
	pmlmepriv->free_bss_buf = pbuf;
	pnetwork = (struct wlan_network *)pbuf;
	for (i = 0; i < MAX_BSS_CNT; i++) {
		INIT_LIST_HEAD(&(pnetwork->list));
		list_add_tail(&(pnetwork->list),
				 &(pmlmepriv->free_bss_pool.queue));
		pnetwork++;
	}
	pmlmepriv->sitesurveyctrl.last_rx_pkts = 0;
	pmlmepriv->sitesurveyctrl.last_tx_pkts = 0;
	pmlmepriv->sitesurveyctrl.traffic_busy = false;
	/* allocate DMA-able/Non-Page memory for cmd_buf and rsp_buf */
	r8712_init_mlme_timer(padapter);
	return _SUCCESS;
}

struct wlan_network *_r8712_alloc_network(struct mlme_priv *pmlmepriv)
{
	unsigned long irqL;
	struct wlan_network *pnetwork;
	struct  __queue *free_queue = &pmlmepriv->free_bss_pool;

	spin_lock_irqsave(&free_queue->lock, irqL);
	pnetwork = list_first_entry_or_null(&free_queue->queue,
					    struct wlan_network, list);
	if (pnetwork) {
		list_del_init(&pnetwork->list);
		pnetwork->last_scanned = jiffies;
		pmlmepriv->num_of_scanned++;
	}
	spin_unlock_irqrestore(&free_queue->lock, irqL);
	return pnetwork;
}

static void _free_network(struct mlme_priv *pmlmepriv,
			  struct wlan_network *pnetwork)
{
	u32 curr_time, delta_time;
	unsigned long irqL;
	struct  __queue *free_queue = &(pmlmepriv->free_bss_pool);

	if (pnetwork == NULL)
		return;
	if (pnetwork->fixed)
		return;
	curr_time = jiffies;
	delta_time = (curr_time - (u32)pnetwork->last_scanned) / HZ;
	if (delta_time < SCANQUEUE_LIFETIME)
		return;
	spin_lock_irqsave(&free_queue->lock, irqL);
	list_del_init(&pnetwork->list);
	list_add_tail(&pnetwork->list, &free_queue->queue);
	pmlmepriv->num_of_scanned--;
	spin_unlock_irqrestore(&free_queue->lock, irqL);
}

static void free_network_nolock(struct mlme_priv *pmlmepriv,
			  struct wlan_network *pnetwork)
{
	struct  __queue *free_queue = &pmlmepriv->free_bss_pool;

	if (pnetwork == NULL)
		return;
	if (pnetwork->fixed)
		return;
	list_del_init(&pnetwork->list);
	list_add_tail(&pnetwork->list, &free_queue->queue);
	pmlmepriv->num_of_scanned--;
}


/*
	return the wlan_network with the matching addr
	Shall be called under atomic context...
	to avoid possible racing condition...
*/
static struct wlan_network *_r8712_find_network(struct  __queue *scanned_queue,
					 u8 *addr)
{
	unsigned long irqL;
	struct list_head *phead, *plist;
	struct wlan_network *pnetwork = NULL;

	if (is_zero_ether_addr(addr))
		return NULL;
	spin_lock_irqsave(&scanned_queue->lock, irqL);
	phead = &scanned_queue->queue;
	plist = phead->next;
	while (plist != phead) {
		pnetwork = container_of(plist, struct wlan_network, list);
		plist = plist->next;
		if (!memcmp(addr, pnetwork->network.MacAddress, ETH_ALEN))
			break;
	}
	spin_unlock_irqrestore(&scanned_queue->lock, irqL);
	return pnetwork;
}

static void _free_network_queue(struct _adapter *padapter)
{
	unsigned long irqL;
	struct list_head *phead, *plist;
	struct wlan_network *pnetwork;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct  __queue *scanned_queue = &pmlmepriv->scanned_queue;

	spin_lock_irqsave(&scanned_queue->lock, irqL);
	phead = &scanned_queue->queue;
	plist = phead->next;
	while (!end_of_queue_search(phead, plist)) {
		pnetwork = container_of(plist, struct wlan_network, list);
		plist = plist->next;
		_free_network(pmlmepriv, pnetwork);
	}
	spin_unlock_irqrestore(&scanned_queue->lock, irqL);
}

sint r8712_if_up(struct _adapter *padapter)
{
	sint res;

	if (padapter->bDriverStopped || padapter->bSurpriseRemoved ||
	    !check_fwstate(&padapter->mlmepriv, _FW_LINKED)) {
		res = false;
	} else {
		res = true;
	}
	return res;
}

void r8712_generate_random_ibss(u8 *pibss)
{
	u32 curtime = jiffies;

	pibss[0] = 0x02; /*in ad-hoc mode bit1 must set to 1 */
	pibss[1] = 0x11;
	pibss[2] = 0x87;
	pibss[3] = (u8)(curtime & 0xff);
	pibss[4] = (u8)((curtime >> 8) & 0xff);
	pibss[5] = (u8)((curtime >> 16) & 0xff);
}

uint r8712_get_wlan_bssid_ex_sz(struct wlan_bssid_ex *bss)
{
	return sizeof(*bss) + bss->IELength - MAX_IE_SZ;
}

u8 *r8712_get_capability_from_ie(u8 *ie)
{
	return ie + 8 + 2;
}

int r8712_init_mlme_priv(struct _adapter *padapter)
{
	return _init_mlme_priv(padapter);
}

void r8712_free_mlme_priv(struct mlme_priv *pmlmepriv)
{
	kfree(pmlmepriv->free_bss_buf);
}

static struct	wlan_network *alloc_network(struct mlme_priv *pmlmepriv)
{
	return _r8712_alloc_network(pmlmepriv);
}

void r8712_free_network_queue(struct _adapter *dev)
{
	_free_network_queue(dev);
}

/*
	return the wlan_network with the matching addr

	Shall be called under atomic context...
	to avoid possible racing condition...
*/
static struct wlan_network *r8712_find_network(struct  __queue *scanned_queue,
					       u8 *addr)
{
	struct wlan_network *pnetwork = _r8712_find_network(scanned_queue,
							    addr);

	return pnetwork;
}

int r8712_is_same_ibss(struct _adapter *adapter, struct wlan_network *pnetwork)
{
	int ret = true;
	struct security_priv *psecuritypriv = &adapter->securitypriv;

	if ((psecuritypriv->PrivacyAlgrthm != _NO_PRIVACY_) &&
		    (pnetwork->network.Privacy == 0))
		ret = false;
	else if ((psecuritypriv->PrivacyAlgrthm == _NO_PRIVACY_) &&
		 (pnetwork->network.Privacy == 1))
		ret = false;
	else
		ret = true;
	return ret;

}

static int is_same_network(struct wlan_bssid_ex *src,
			   struct wlan_bssid_ex *dst)
{
	 u16 s_cap, d_cap;

	memcpy((u8 *)&s_cap, r8712_get_capability_from_ie(src->IEs), 2);
	memcpy((u8 *)&d_cap, r8712_get_capability_from_ie(dst->IEs), 2);
	return (src->Ssid.SsidLength == dst->Ssid.SsidLength) &&
			(src->Configuration.DSConfig ==
			dst->Configuration.DSConfig) &&
			((!memcmp(src->MacAddress, dst->MacAddress,
			ETH_ALEN))) &&
			((!memcmp(src->Ssid.Ssid,
			  dst->Ssid.Ssid,
			  src->Ssid.SsidLength))) &&
			((s_cap & WLAN_CAPABILITY_IBSS) ==
			(d_cap & WLAN_CAPABILITY_IBSS)) &&
			((s_cap & WLAN_CAPABILITY_BSS) ==
			(d_cap & WLAN_CAPABILITY_BSS));

}

struct	wlan_network *r8712_get_oldest_wlan_network(
				struct  __queue *scanned_queue)
{
	struct list_head *plist, *phead;
	struct	wlan_network	*pwlan = NULL;
	struct	wlan_network	*oldest = NULL;

	phead = &scanned_queue->queue;
	plist = phead->next;
	while (1) {
		if (end_of_queue_search(phead, plist) ==  true)
			break;
		pwlan = container_of(plist, struct wlan_network, list);
		if (pwlan->fixed != true) {
			if (oldest == NULL ||
			    time_after((unsigned long)oldest->last_scanned,
			    (unsigned long)pwlan->last_scanned))
				oldest = pwlan;
		}
		plist = plist->next;
	}
	return oldest;
}

static void update_network(struct wlan_bssid_ex *dst,
			   struct wlan_bssid_ex *src,
			   struct _adapter *padapter)
{
	u32 last_evm = 0, tmpVal;

	if (check_fwstate(&padapter->mlmepriv, _FW_LINKED) &&
	    is_same_network(&(padapter->mlmepriv.cur_network.network), src)) {
		if (padapter->recvpriv.signal_qual_data.total_num++ >=
		    PHY_LINKQUALITY_SLID_WIN_MAX) {
			padapter->recvpriv.signal_qual_data.total_num =
				   PHY_LINKQUALITY_SLID_WIN_MAX;
			last_evm = padapter->recvpriv.signal_qual_data.
				   elements[padapter->recvpriv.
				   signal_qual_data.index];
			padapter->recvpriv.signal_qual_data.total_val -=
				 last_evm;
		}
		padapter->recvpriv.signal_qual_data.total_val += src->Rssi;

		padapter->recvpriv.signal_qual_data.
			  elements[padapter->recvpriv.signal_qual_data.
			  index++] = src->Rssi;
		if (padapter->recvpriv.signal_qual_data.index >=
		    PHY_LINKQUALITY_SLID_WIN_MAX)
			padapter->recvpriv.signal_qual_data.index = 0;
		/* <1> Showed on UI for user, in percentage. */
		tmpVal = padapter->recvpriv.signal_qual_data.total_val /
			 padapter->recvpriv.signal_qual_data.total_num;
		padapter->recvpriv.signal = (u8)tmpVal;

		src->Rssi = padapter->recvpriv.signal;
	} else {
		src->Rssi = (src->Rssi + dst->Rssi) / 2;
	}
	memcpy((u8 *)dst, (u8 *)src, r8712_get_wlan_bssid_ex_sz(src));
}

static void update_current_network(struct _adapter *adapter,
				   struct wlan_bssid_ex *pnetwork)
{
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;

	if (is_same_network(&(pmlmepriv->cur_network.network), pnetwork)) {
		update_network(&(pmlmepriv->cur_network.network),
			       pnetwork, adapter);
		r8712_update_protection(adapter,
			       (pmlmepriv->cur_network.network.IEs) +
			       sizeof(struct NDIS_802_11_FIXED_IEs),
			       pmlmepriv->cur_network.network.IELength);
	}
}

/*
Caller must hold pmlmepriv->lock first.
*/
static void update_scanned_network(struct _adapter *adapter,
			    struct wlan_bssid_ex *target)
{
	struct list_head *plist, *phead;

	u32 bssid_ex_sz;
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	struct  __queue *queue = &pmlmepriv->scanned_queue;
	struct wlan_network *pnetwork = NULL;
	struct wlan_network *oldest = NULL;

	phead = &queue->queue;
	plist = phead->next;

	while (1) {
		if (end_of_queue_search(phead, plist))
			break;

		pnetwork = container_of(plist, struct wlan_network, list);
		if (is_same_network(&pnetwork->network, target))
			break;
		if ((oldest == ((struct wlan_network *)0)) ||
		    time_after((unsigned long)oldest->last_scanned,
				(unsigned long)pnetwork->last_scanned))
			oldest = pnetwork;

		plist = plist->next;
	}


	/* If we didn't find a match, then get a new network slot to initialize
	 * with this beacon's information
	 */
	if (end_of_queue_search(phead, plist)) {
		if (list_empty(&pmlmepriv->free_bss_pool.queue)) {
			/* If there are no more slots, expire the oldest */
			pnetwork = oldest;
			target->Rssi = (pnetwork->network.Rssi +
					target->Rssi) / 2;
			memcpy(&pnetwork->network, target,
				r8712_get_wlan_bssid_ex_sz(target));
			pnetwork->last_scanned = jiffies;
		} else {
			/* Otherwise just pull from the free list */
			/* update scan_time */
			pnetwork = alloc_network(pmlmepriv);
			if (pnetwork == NULL)
				return;
			bssid_ex_sz = r8712_get_wlan_bssid_ex_sz(target);
			target->Length = bssid_ex_sz;
			memcpy(&pnetwork->network, target, bssid_ex_sz);
			list_add_tail(&pnetwork->list, &queue->queue);
		}
	} else {
		/* we have an entry and we are going to update it. But
		 * this entry may be already expired. In this case we
		 * do the same as we found a new net and call the new_net
		 * handler
		 */
		update_network(&pnetwork->network, target, adapter);
		pnetwork->last_scanned = jiffies;
	}
}

static void rtl8711_add_network(struct _adapter *adapter,
			 struct wlan_bssid_ex *pnetwork)
{
	unsigned long irqL;
	struct mlme_priv *pmlmepriv = &(((struct _adapter *)adapter)->mlmepriv);
	struct  __queue *queue = &pmlmepriv->scanned_queue;

	spin_lock_irqsave(&queue->lock, irqL);
	update_current_network(adapter, pnetwork);
	update_scanned_network(adapter, pnetwork);
	spin_unlock_irqrestore(&queue->lock, irqL);
}

/*select the desired network based on the capability of the (i)bss.
 * check items:		(1) security
 *			(2) network_type
 *			(3) WMM
 *			(4) HT
 *			(5) others
 */
static int is_desired_network(struct _adapter *adapter,
				struct wlan_network *pnetwork)
{
	u8 wps_ie[512];
	uint wps_ielen;
	int bselected = true;
	struct	security_priv *psecuritypriv = &adapter->securitypriv;

	if (psecuritypriv->wps_phase) {
		if (r8712_get_wps_ie(pnetwork->network.IEs,
		    pnetwork->network.IELength, wps_ie,
		    &wps_ielen))
			return true;
		return false;
	}
	if ((psecuritypriv->PrivacyAlgrthm != _NO_PRIVACY_) &&
		    (pnetwork->network.Privacy == 0))
		bselected = false;
	if (check_fwstate(&adapter->mlmepriv, WIFI_ADHOC_STATE)) {
		if (pnetwork->network.InfrastructureMode !=
			adapter->mlmepriv.cur_network.network.
			InfrastructureMode)
			bselected = false;
	}
	return bselected;
}

/* TODO: Perry : For Power Management */
void r8712_atimdone_event_callback(struct _adapter *adapter, u8 *pbuf)
{
}

void r8712_survey_event_callback(struct _adapter *adapter, u8 *pbuf)
{
	unsigned long flags;
	u32 len;
	struct wlan_bssid_ex *pnetwork;
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;

	pnetwork = (struct wlan_bssid_ex *)pbuf;
#ifdef __BIG_ENDIAN
	/* endian_convert */
	pnetwork->Length = le32_to_cpu(pnetwork->Length);
	pnetwork->Ssid.SsidLength = le32_to_cpu(pnetwork->Ssid.SsidLength);
	pnetwork->Privacy = le32_to_cpu(pnetwork->Privacy);
	pnetwork->Rssi = le32_to_cpu(pnetwork->Rssi);
	pnetwork->NetworkTypeInUse = le32_to_cpu(pnetwork->NetworkTypeInUse);
	pnetwork->Configuration.ATIMWindow =
		 le32_to_cpu(pnetwork->Configuration.ATIMWindow);
	pnetwork->Configuration.BeaconPeriod =
		 le32_to_cpu(pnetwork->Configuration.BeaconPeriod);
	pnetwork->Configuration.DSConfig =
		 le32_to_cpu(pnetwork->Configuration.DSConfig);
	pnetwork->Configuration.FHConfig.DwellTime =
		 le32_to_cpu(pnetwork->Configuration.FHConfig.DwellTime);
	pnetwork->Configuration.FHConfig.HopPattern =
		 le32_to_cpu(pnetwork->Configuration.FHConfig.HopPattern);
	pnetwork->Configuration.FHConfig.HopSet =
		 le32_to_cpu(pnetwork->Configuration.FHConfig.HopSet);
	pnetwork->Configuration.FHConfig.Length =
		 le32_to_cpu(pnetwork->Configuration.FHConfig.Length);
	pnetwork->Configuration.Length =
		 le32_to_cpu(pnetwork->Configuration.Length);
	pnetwork->InfrastructureMode =
		 le32_to_cpu(pnetwork->InfrastructureMode);
	pnetwork->IELength = le32_to_cpu(pnetwork->IELength);
#endif
	len = r8712_get_wlan_bssid_ex_sz(pnetwork);
	if (len > sizeof(struct wlan_bssid_ex))
		return;
	spin_lock_irqsave(&pmlmepriv->lock2, flags);
	/* update IBSS_network 's timestamp */
	if (check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE)) {
		if (!memcmp(&(pmlmepriv->cur_network.network.MacAddress),
		    pnetwork->MacAddress, ETH_ALEN)) {
			struct wlan_network *ibss_wlan = NULL;

			memcpy(pmlmepriv->cur_network.network.IEs,
				pnetwork->IEs, 8);
			ibss_wlan = r8712_find_network(
						&pmlmepriv->scanned_queue,
						pnetwork->MacAddress);
			if (ibss_wlan) {
				memcpy(ibss_wlan->network.IEs,
					pnetwork->IEs, 8);
				goto exit;
			}
		}
	}
	/* lock pmlmepriv->lock when you accessing network_q */
	if (!check_fwstate(pmlmepriv, _FW_UNDER_LINKING)) {
		if (pnetwork->Ssid.Ssid[0] != 0) {
			rtl8711_add_network(adapter, pnetwork);
		} else {
			pnetwork->Ssid.SsidLength = 8;
			memcpy(pnetwork->Ssid.Ssid, "<hidden>", 8);
			rtl8711_add_network(adapter, pnetwork);
		}
	}
exit:
	spin_unlock_irqrestore(&pmlmepriv->lock2, flags);
}

void r8712_surveydone_event_callback(struct _adapter *adapter, u8 *pbuf)
{
	unsigned long irqL;
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;

	spin_lock_irqsave(&pmlmepriv->lock, irqL);

	if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY)) {
		del_timer(&pmlmepriv->scan_to_timer);

		_clr_fwstate_(pmlmepriv, _FW_UNDER_SURVEY);
	}

	if (pmlmepriv->to_join) {
		if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE)) {
			if (!check_fwstate(pmlmepriv, _FW_LINKED)) {
				set_fwstate(pmlmepriv, _FW_UNDER_LINKING);

				if (r8712_select_and_join_from_scan(pmlmepriv)
				    == _SUCCESS)
					mod_timer(&pmlmepriv->assoc_timer, jiffies +
						  msecs_to_jiffies(MAX_JOIN_TIMEOUT));
				else {
					struct wlan_bssid_ex *pdev_network =
					  &(adapter->registrypriv.dev_network);
					u8 *pibss =
						 adapter->registrypriv.
							dev_network.MacAddress;
					pmlmepriv->fw_state ^= _FW_UNDER_SURVEY;
					memcpy(&pdev_network->Ssid,
						&pmlmepriv->assoc_ssid,
						sizeof(struct
							 ndis_802_11_ssid));
					r8712_update_registrypriv_dev_network
						(adapter);
					r8712_generate_random_ibss(pibss);
					pmlmepriv->fw_state =
						 WIFI_ADHOC_MASTER_STATE;
					pmlmepriv->to_join = false;
				}
			}
		} else {
			pmlmepriv->to_join = false;
			set_fwstate(pmlmepriv, _FW_UNDER_LINKING);
			if (r8712_select_and_join_from_scan(pmlmepriv) ==
			    _SUCCESS)
				mod_timer(&pmlmepriv->assoc_timer, jiffies +
					  msecs_to_jiffies(MAX_JOIN_TIMEOUT));
			else
				_clr_fwstate_(pmlmepriv, _FW_UNDER_LINKING);
		}
	}
	spin_unlock_irqrestore(&pmlmepriv->lock, irqL);
}

/*
 *r8712_free_assoc_resources: the caller has to lock pmlmepriv->lock
 */
void r8712_free_assoc_resources(struct _adapter *adapter)
{
	unsigned long irqL;
	struct wlan_network *pwlan = NULL;
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	struct sta_priv *pstapriv = &adapter->stapriv;
	struct wlan_network *tgt_network = &pmlmepriv->cur_network;

	pwlan = r8712_find_network(&pmlmepriv->scanned_queue,
				   tgt_network->network.MacAddress);

	if (check_fwstate(pmlmepriv, WIFI_STATION_STATE | WIFI_AP_STATE)) {
		struct sta_info *psta;

		psta = r8712_get_stainfo(&adapter->stapriv,
					 tgt_network->network.MacAddress);

		spin_lock_irqsave(&pstapriv->sta_hash_lock, irqL);
		r8712_free_stainfo(adapter,  psta);
		spin_unlock_irqrestore(&pstapriv->sta_hash_lock, irqL);
	}

	if (check_fwstate(pmlmepriv,
	    WIFI_ADHOC_STATE | WIFI_ADHOC_MASTER_STATE | WIFI_AP_STATE))
		r8712_free_all_stainfo(adapter);
	if (pwlan)
		pwlan->fixed = false;

	if (((check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE)) &&
	     (adapter->stapriv.asoc_sta_count == 1)))
		free_network_nolock(pmlmepriv, pwlan);
}

/*
*r8712_indicate_connect: the caller has to lock pmlmepriv->lock
*/
void r8712_indicate_connect(struct _adapter *padapter)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

	pmlmepriv->to_join = false;
	set_fwstate(pmlmepriv, _FW_LINKED);
	padapter->ledpriv.LedControlHandler(padapter, LED_CTL_LINK);
	r8712_os_indicate_connect(padapter);
	if (padapter->registrypriv.power_mgnt > PS_MODE_ACTIVE)
		mod_timer(&pmlmepriv->dhcp_timer,
			  jiffies + msecs_to_jiffies(60000));
}


/*
*r8712_ind_disconnect: the caller has to lock pmlmepriv->lock
*/
void r8712_ind_disconnect(struct _adapter *padapter)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

	if (check_fwstate(pmlmepriv, _FW_LINKED)) {
		_clr_fwstate_(pmlmepriv, _FW_LINKED);
		padapter->ledpriv.LedControlHandler(padapter, LED_CTL_NO_LINK);
		r8712_os_indicate_disconnect(padapter);
	}
	if (padapter->pwrctrlpriv.pwr_mode !=
	    padapter->registrypriv.power_mgnt) {
		del_timer(&pmlmepriv->dhcp_timer);
		r8712_set_ps_mode(padapter, padapter->registrypriv.power_mgnt,
				  padapter->registrypriv.smart_ps);
	}
}

/*Notes:
 *pnetwork : returns from r8712_joinbss_event_callback
 *ptarget_wlan: found from scanned_queue
 *if join_res > 0, for (fw_state==WIFI_STATION_STATE), we check if
 *  "ptarget_sta" & "ptarget_wlan" exist.
 *if join_res > 0, for (fw_state==WIFI_ADHOC_STATE), we only check
 * if "ptarget_wlan" exist.
 *if join_res > 0, update "cur_network->network" from
 * "pnetwork->network" if (ptarget_wlan !=NULL).
 */
void r8712_joinbss_event_callback(struct _adapter *adapter, u8 *pbuf)
{
	unsigned long irqL = 0, irqL2;
	struct sta_info	*ptarget_sta = NULL, *pcur_sta = NULL;
	struct sta_priv	*pstapriv = &adapter->stapriv;
	struct mlme_priv	*pmlmepriv = &adapter->mlmepriv;
	struct wlan_network	*cur_network = &pmlmepriv->cur_network;
	struct wlan_network	*pcur_wlan = NULL, *ptarget_wlan = NULL;
	unsigned int		the_same_macaddr = false;
	struct wlan_network *pnetwork;

	if (sizeof(struct list_head) == 4 * sizeof(u32)) {
		pnetwork = kmalloc(sizeof(struct wlan_network), GFP_ATOMIC);
		if (!pnetwork)
			return;
		memcpy((u8 *)pnetwork + 16, (u8 *)pbuf + 8,
		       sizeof(struct wlan_network) - 16);
	} else {
		pnetwork = (struct wlan_network *)pbuf;
	}

#ifdef __BIG_ENDIAN
	/* endian_convert */
	pnetwork->join_res = le32_to_cpu(pnetwork->join_res);
	pnetwork->network_type = le32_to_cpu(pnetwork->network_type);
	pnetwork->network.Length = le32_to_cpu(pnetwork->network.Length);
	pnetwork->network.Ssid.SsidLength =
		 le32_to_cpu(pnetwork->network.Ssid.SsidLength);
	pnetwork->network.Privacy = le32_to_cpu(pnetwork->network.Privacy);
	pnetwork->network.Rssi = le32_to_cpu(pnetwork->network.Rssi);
	pnetwork->network.NetworkTypeInUse =
		 le32_to_cpu(pnetwork->network.NetworkTypeInUse);
	pnetwork->network.Configuration.ATIMWindow =
		 le32_to_cpu(pnetwork->network.Configuration.ATIMWindow);
	pnetwork->network.Configuration.BeaconPeriod =
		 le32_to_cpu(pnetwork->network.Configuration.BeaconPeriod);
	pnetwork->network.Configuration.DSConfig =
		 le32_to_cpu(pnetwork->network.Configuration.DSConfig);
	pnetwork->network.Configuration.FHConfig.DwellTime =
		 le32_to_cpu(pnetwork->network.Configuration.FHConfig.
			     DwellTime);
	pnetwork->network.Configuration.FHConfig.HopPattern =
		 le32_to_cpu(pnetwork->network.Configuration.
			     FHConfig.HopPattern);
	pnetwork->network.Configuration.FHConfig.HopSet =
		 le32_to_cpu(pnetwork->network.Configuration.FHConfig.HopSet);
	pnetwork->network.Configuration.FHConfig.Length =
		 le32_to_cpu(pnetwork->network.Configuration.FHConfig.Length);
	pnetwork->network.Configuration.Length =
		 le32_to_cpu(pnetwork->network.Configuration.Length);
	pnetwork->network.InfrastructureMode =
		 le32_to_cpu(pnetwork->network.InfrastructureMode);
	pnetwork->network.IELength = le32_to_cpu(pnetwork->network.IELength);
#endif

	the_same_macaddr = !memcmp(pnetwork->network.MacAddress,
				   cur_network->network.MacAddress, ETH_ALEN);
	pnetwork->network.Length =
		 r8712_get_wlan_bssid_ex_sz(&pnetwork->network);
	spin_lock_irqsave(&pmlmepriv->lock, irqL);
	if (pnetwork->network.Length > sizeof(struct wlan_bssid_ex))
		goto ignore_joinbss_callback;
	if (pnetwork->join_res > 0) {
		if (check_fwstate(pmlmepriv, _FW_UNDER_LINKING)) {
			/*s1. find ptarget_wlan*/
			if (check_fwstate(pmlmepriv, _FW_LINKED)) {
				if (the_same_macaddr) {
					ptarget_wlan =
					    r8712_find_network(&pmlmepriv->
					    scanned_queue,
					    cur_network->network.MacAddress);
				} else {
					pcur_wlan =
					     r8712_find_network(&pmlmepriv->
					     scanned_queue,
					     cur_network->network.MacAddress);
					pcur_wlan->fixed = false;

					pcur_sta = r8712_get_stainfo(pstapriv,
					     cur_network->network.MacAddress);
					spin_lock_irqsave(&pstapriv->
						sta_hash_lock, irqL2);
					r8712_free_stainfo(adapter, pcur_sta);
					spin_unlock_irqrestore(&(pstapriv->
						sta_hash_lock), irqL2);

					ptarget_wlan =
						 r8712_find_network(&pmlmepriv->
						 scanned_queue,
						 pnetwork->network.
						 MacAddress);
					if (ptarget_wlan)
						ptarget_wlan->fixed = true;
				}
			} else {
				ptarget_wlan = r8712_find_network(&pmlmepriv->
						scanned_queue,
						pnetwork->network.MacAddress);
				if (ptarget_wlan)
					ptarget_wlan->fixed = true;
			}

			if (ptarget_wlan == NULL) {
				if (check_fwstate(pmlmepriv,
					_FW_UNDER_LINKING))
					pmlmepriv->fw_state ^=
						 _FW_UNDER_LINKING;
				goto ignore_joinbss_callback;
			}

			/*s2. find ptarget_sta & update ptarget_sta*/
			if (check_fwstate(pmlmepriv, WIFI_STATION_STATE)) {
				if (the_same_macaddr) {
					ptarget_sta =
						 r8712_get_stainfo(pstapriv,
						 pnetwork->network.MacAddress);
					if (ptarget_sta == NULL)
						ptarget_sta =
						 r8712_alloc_stainfo(pstapriv,
						 pnetwork->network.MacAddress);
				} else {
					ptarget_sta =
						 r8712_alloc_stainfo(pstapriv,
						 pnetwork->network.MacAddress);
				}
				if (ptarget_sta) /*update ptarget_sta*/ {
					ptarget_sta->aid = pnetwork->join_res;
					ptarget_sta->qos_option = 1;
					ptarget_sta->mac_id = 5;
					if (adapter->securitypriv.
					    AuthAlgrthm == 2) {
						adapter->securitypriv.
							binstallGrpkey =
							 false;
						adapter->securitypriv.
							busetkipkey =
							 false;
						adapter->securitypriv.
							bgrpkey_handshake =
							 false;
						ptarget_sta->ieee8021x_blocked
							 = true;
						ptarget_sta->XPrivacy =
							 adapter->securitypriv.
							 PrivacyAlgrthm;
						memset((u8 *)&ptarget_sta->
							 x_UncstKey,
							 0,
							 sizeof(union Keytype));
						memset((u8 *)&ptarget_sta->
							 tkiprxmickey,
							 0,
							 sizeof(union Keytype));
						memset((u8 *)&ptarget_sta->
							 tkiptxmickey,
							 0,
							 sizeof(union Keytype));
						memset((u8 *)&ptarget_sta->
							 txpn, 0,
							 sizeof(union pn48));
						memset((u8 *)&ptarget_sta->
							 rxpn, 0,
							 sizeof(union pn48));
					}
				} else {
					if (check_fwstate(pmlmepriv,
					    _FW_UNDER_LINKING))
						pmlmepriv->fw_state ^=
							 _FW_UNDER_LINKING;
					goto ignore_joinbss_callback;
				}
			}

			/*s3. update cur_network & indicate connect*/
			memcpy(&cur_network->network, &pnetwork->network,
				pnetwork->network.Length);
			cur_network->aid = pnetwork->join_res;
			/*update fw_state will clr _FW_UNDER_LINKING*/
			switch (pnetwork->network.InfrastructureMode) {
			case Ndis802_11Infrastructure:
				pmlmepriv->fw_state = WIFI_STATION_STATE;
				break;
			case Ndis802_11IBSS:
				pmlmepriv->fw_state = WIFI_ADHOC_STATE;
				break;
			default:
				pmlmepriv->fw_state = WIFI_NULL_STATE;
				break;
			}
			r8712_update_protection(adapter,
					  (cur_network->network.IEs) +
					  sizeof(struct NDIS_802_11_FIXED_IEs),
					  (cur_network->network.IELength));
			/*TODO: update HT_Capability*/
			update_ht_cap(adapter, cur_network->network.IEs,
				      cur_network->network.IELength);
			/*indicate connect*/
			if (check_fwstate(pmlmepriv, WIFI_STATION_STATE))
				r8712_indicate_connect(adapter);
			del_timer(&pmlmepriv->assoc_timer);
		} else {
			goto ignore_joinbss_callback;
		}
	} else {
		if (check_fwstate(pmlmepriv, _FW_UNDER_LINKING)) {
			mod_timer(&pmlmepriv->assoc_timer,
				  jiffies + msecs_to_jiffies(1));
			_clr_fwstate_(pmlmepriv, _FW_UNDER_LINKING);
		}
	}
ignore_joinbss_callback:
	spin_unlock_irqrestore(&pmlmepriv->lock, irqL);
	if (sizeof(struct list_head) == 4 * sizeof(u32))
		kfree(pnetwork);
}

void r8712_stassoc_event_callback(struct _adapter *adapter, u8 *pbuf)
{
	unsigned long irqL;
	struct sta_info *psta;
	struct mlme_priv *pmlmepriv = &(adapter->mlmepriv);
	struct stassoc_event *pstassoc	= (struct stassoc_event *)pbuf;

	/* to do: */
	if (!r8712_access_ctrl(&adapter->acl_list, pstassoc->macaddr))
		return;
	psta = r8712_get_stainfo(&adapter->stapriv, pstassoc->macaddr);
	if (psta != NULL) {
		/*the sta have been in sta_info_queue => do nothing
		 *(between drv has received this event before and
		 * fw have not yet to set key to CAM_ENTRY)
		 */
		return;
	}

	psta = r8712_alloc_stainfo(&adapter->stapriv, pstassoc->macaddr);
	if (psta == NULL)
		return;
	/* to do : init sta_info variable */
	psta->qos_option = 0;
	psta->mac_id = le32_to_cpu((uint)pstassoc->cam_id);
	/* psta->aid = (uint)pstassoc->cam_id; */

	if (adapter->securitypriv.AuthAlgrthm == 2)
		psta->XPrivacy = adapter->securitypriv.PrivacyAlgrthm;
	psta->ieee8021x_blocked = false;
	spin_lock_irqsave(&pmlmepriv->lock, irqL);
	if (check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) ||
	    check_fwstate(pmlmepriv, WIFI_ADHOC_STATE)) {
		if (adapter->stapriv.asoc_sta_count == 2) {
			/* a sta + bc/mc_stainfo (not Ibss_stainfo) */
			r8712_indicate_connect(adapter);
		}
	}
	spin_unlock_irqrestore(&pmlmepriv->lock, irqL);
}

void r8712_stadel_event_callback(struct _adapter *adapter, u8 *pbuf)
{
	unsigned long irqL, irqL2;
	struct sta_info *psta;
	struct wlan_network *pwlan = NULL;
	struct wlan_bssid_ex *pdev_network = NULL;
	u8 *pibss = NULL;
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	struct stadel_event *pstadel = (struct stadel_event *)pbuf;
	struct sta_priv *pstapriv = &adapter->stapriv;
	struct wlan_network *tgt_network = &pmlmepriv->cur_network;

	spin_lock_irqsave(&pmlmepriv->lock, irqL2);
	if (check_fwstate(pmlmepriv, WIFI_STATION_STATE)) {
		r8712_ind_disconnect(adapter);
		r8712_free_assoc_resources(adapter);
	}
	if (check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE |
	    WIFI_ADHOC_STATE)) {
		psta = r8712_get_stainfo(&adapter->stapriv, pstadel->macaddr);
		spin_lock_irqsave(&pstapriv->sta_hash_lock, irqL);
		r8712_free_stainfo(adapter, psta);
		spin_unlock_irqrestore(&pstapriv->sta_hash_lock, irqL);
		if (adapter->stapriv.asoc_sta_count == 1) {
			/*a sta + bc/mc_stainfo (not Ibss_stainfo) */
			pwlan = r8712_find_network(&pmlmepriv->scanned_queue,
				tgt_network->network.MacAddress);
			if (pwlan) {
				pwlan->fixed = false;
				free_network_nolock(pmlmepriv, pwlan);
			}
			/*re-create ibss*/
			pdev_network = &(adapter->registrypriv.dev_network);
			pibss = adapter->registrypriv.dev_network.MacAddress;
			memcpy(pdev_network, &tgt_network->network,
				r8712_get_wlan_bssid_ex_sz(&tgt_network->
							network));
			memcpy(&pdev_network->Ssid,
				&pmlmepriv->assoc_ssid,
				sizeof(struct ndis_802_11_ssid));
			r8712_update_registrypriv_dev_network(adapter);
			r8712_generate_random_ibss(pibss);
			if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE)) {
				_clr_fwstate_(pmlmepriv, WIFI_ADHOC_STATE);
				set_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE);
			}
		}
	}
	spin_unlock_irqrestore(&pmlmepriv->lock, irqL2);
}

void r8712_cpwm_event_callback(struct _adapter *adapter, u8 *pbuf)
{
	struct reportpwrstate_parm *preportpwrstate =
			 (struct reportpwrstate_parm *)pbuf;

	preportpwrstate->state |= (u8)(adapter->pwrctrlpriv.cpwm_tog + 0x80);
	r8712_cpwm_int_hdl(adapter, preportpwrstate);
}

/*	When the Netgear 3500 AP is with WPA2PSK-AES mode, it will send
 *	 the ADDBA req frame with start seq control = 0 to wifi client after
 *	 the WPA handshake and the seqence number of following data packet
 *	will be 0. In this case, the Rx reorder sequence is not longer than 0
 *	 and the WiFi client will drop the data with seq number 0.
 *	So, the 8712 firmware has to inform driver with receiving the
 *	 ADDBA-Req frame so that the driver can reset the
 *	sequence value of Rx reorder control.
 */
void r8712_got_addbareq_event_callback(struct _adapter *adapter, u8 *pbuf)
{
	struct	ADDBA_Req_Report_parm *pAddbareq_pram =
			 (struct ADDBA_Req_Report_parm *)pbuf;
	struct	sta_info *psta;
	struct	sta_priv *pstapriv = &adapter->stapriv;
	struct	recv_reorder_ctrl *precvreorder_ctrl = NULL;

	psta = r8712_get_stainfo(pstapriv, pAddbareq_pram->MacAddress);
	if (psta) {
		precvreorder_ctrl =
			 &psta->recvreorder_ctrl[pAddbareq_pram->tid];
		/* set the indicate_seq to 0xffff so that the rx reorder
		 * can store any following data packet.
		 */
		precvreorder_ctrl->indicate_seq = 0xffff;
	}
}

void r8712_wpspbc_event_callback(struct _adapter *adapter, u8 *pbuf)
{
	if (!adapter->securitypriv.wps_hw_pbc_pressed)
		adapter->securitypriv.wps_hw_pbc_pressed = true;
}

void _r8712_sitesurvey_ctrl_handler(struct _adapter *adapter)
{
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	struct sitesurvey_ctrl	*psitesurveyctrl = &pmlmepriv->sitesurveyctrl;
	struct registry_priv	*pregistrypriv = &adapter->registrypriv;
	u64 current_tx_pkts;
	uint current_rx_pkts;

	current_tx_pkts = (adapter->xmitpriv.tx_pkts) -
			  (psitesurveyctrl->last_tx_pkts);
	current_rx_pkts = (adapter->recvpriv.rx_pkts) -
			  (psitesurveyctrl->last_rx_pkts);
	psitesurveyctrl->last_tx_pkts = adapter->xmitpriv.tx_pkts;
	psitesurveyctrl->last_rx_pkts = adapter->recvpriv.rx_pkts;
	if ((current_tx_pkts > pregistrypriv->busy_thresh) ||
	    (current_rx_pkts > pregistrypriv->busy_thresh))
		psitesurveyctrl->traffic_busy = true;
	else
		psitesurveyctrl->traffic_busy = false;
}

void _r8712_join_timeout_handler(struct _adapter *adapter)
{
	unsigned long irqL;
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;

	if (adapter->bDriverStopped || adapter->bSurpriseRemoved)
		return;
	spin_lock_irqsave(&pmlmepriv->lock, irqL);
	_clr_fwstate_(pmlmepriv, _FW_UNDER_LINKING);
	pmlmepriv->to_join = false;
	if (check_fwstate(pmlmepriv, _FW_LINKED)) {
		r8712_os_indicate_disconnect(adapter);
		_clr_fwstate_(pmlmepriv, _FW_LINKED);
	}
	if (adapter->pwrctrlpriv.pwr_mode != adapter->registrypriv.power_mgnt) {
		r8712_set_ps_mode(adapter, adapter->registrypriv.power_mgnt,
				  adapter->registrypriv.smart_ps);
	}
	spin_unlock_irqrestore(&pmlmepriv->lock, irqL);
}

void r8712_scan_timeout_handler (struct _adapter *adapter)
{
	unsigned long irqL;
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;

	spin_lock_irqsave(&pmlmepriv->lock, irqL);
	_clr_fwstate_(pmlmepriv, _FW_UNDER_SURVEY);
	pmlmepriv->to_join = false;	/* scan fail, so clear to_join flag */
	spin_unlock_irqrestore(&pmlmepriv->lock, irqL);
}

void _r8712_dhcp_timeout_handler (struct _adapter *adapter)
{
	if (adapter->bDriverStopped || adapter->bSurpriseRemoved)
		return;
	if (adapter->pwrctrlpriv.pwr_mode != adapter->registrypriv.power_mgnt)
		r8712_set_ps_mode(adapter, adapter->registrypriv.power_mgnt,
			    adapter->registrypriv.smart_ps);
}

void _r8712_wdg_timeout_handler(struct _adapter *adapter)
{
	r8712_wdg_wk_cmd(adapter);
}

int r8712_select_and_join_from_scan(struct mlme_priv *pmlmepriv)
{
	struct list_head *phead;
	unsigned char *dst_ssid, *src_ssid;
	struct _adapter *adapter;
	struct  __queue *queue = NULL;
	struct wlan_network *pnetwork = NULL;
	struct wlan_network *pnetwork_max_rssi = NULL;

	adapter = (struct _adapter *)pmlmepriv->nic_hdl;
	queue = &pmlmepriv->scanned_queue;
	phead = &queue->queue;
	pmlmepriv->pscanned = phead->next;
	while (1) {
		if (end_of_queue_search(phead, pmlmepriv->pscanned)) {
			if ((pmlmepriv->assoc_by_rssi) &&
			    (pnetwork_max_rssi != NULL)) {
				pnetwork = pnetwork_max_rssi;
				goto ask_for_joinbss;
			}
			return _FAIL;
		}
		pnetwork = container_of(pmlmepriv->pscanned,
					struct wlan_network, list);
		if (pnetwork == NULL)
			return _FAIL;
		pmlmepriv->pscanned = pmlmepriv->pscanned->next;
		if (pmlmepriv->assoc_by_bssid) {
			dst_ssid = pnetwork->network.MacAddress;
			src_ssid = pmlmepriv->assoc_bssid;
			if (!memcmp(dst_ssid, src_ssid, ETH_ALEN)) {
				if (check_fwstate(pmlmepriv, _FW_LINKED)) {
					if (is_same_network(&pmlmepriv->
					    cur_network.network,
					    &pnetwork->network)) {
						_clr_fwstate_(pmlmepriv,
							_FW_UNDER_LINKING);
						/*r8712_indicate_connect again*/
						r8712_indicate_connect(adapter);
						return 2;
					}
					r8712_disassoc_cmd(adapter);
					r8712_ind_disconnect(adapter);
					r8712_free_assoc_resources(adapter);
				}
				goto ask_for_joinbss;
			}
		} else if (pmlmepriv->assoc_ssid.SsidLength == 0) {
			goto ask_for_joinbss;
		}
		dst_ssid = pnetwork->network.Ssid.Ssid;
		src_ssid = pmlmepriv->assoc_ssid.Ssid;
		if ((pnetwork->network.Ssid.SsidLength ==
		    pmlmepriv->assoc_ssid.SsidLength) &&
		    (!memcmp(dst_ssid, src_ssid,
		     pmlmepriv->assoc_ssid.SsidLength))) {
			if (pmlmepriv->assoc_by_rssi) {
				/* if the ssid is the same, select the bss
				 * which has the max rssi
				 */
				if (pnetwork_max_rssi) {
					if (pnetwork->network.Rssi >
					    pnetwork_max_rssi->network.Rssi)
						pnetwork_max_rssi = pnetwork;
				} else {
					pnetwork_max_rssi = pnetwork;
				}
			} else if (is_desired_network(adapter, pnetwork)) {
				if (check_fwstate(pmlmepriv, _FW_LINKED)) {
					r8712_disassoc_cmd(adapter);
					r8712_free_assoc_resources(adapter);
				}
				goto ask_for_joinbss;
			}
		}
	}

ask_for_joinbss:
	return r8712_joinbss_cmd(adapter, pnetwork);
}

sint r8712_set_auth(struct _adapter *adapter,
		    struct security_priv *psecuritypriv)
{
	struct cmd_priv	*pcmdpriv = &adapter->cmdpriv;
	struct cmd_obj *pcmd;
	struct setauth_parm *psetauthparm;

	pcmd = kmalloc(sizeof(*pcmd), GFP_ATOMIC);
	if (!pcmd)
		return _FAIL;

	psetauthparm = kzalloc(sizeof(*psetauthparm), GFP_ATOMIC);
	if (!psetauthparm) {
		kfree(pcmd);
		return _FAIL;
	}
	psetauthparm->mode = (u8)psecuritypriv->AuthAlgrthm;
	pcmd->cmdcode = _SetAuth_CMD_;
	pcmd->parmbuf = (unsigned char *)psetauthparm;
	pcmd->cmdsz = sizeof(struct setauth_parm);
	pcmd->rsp = NULL;
	pcmd->rspsz = 0;
	INIT_LIST_HEAD(&pcmd->list);
	r8712_enqueue_cmd(pcmdpriv, pcmd);
	return _SUCCESS;
}

sint r8712_set_key(struct _adapter *adapter,
		   struct security_priv *psecuritypriv,
	     sint keyid)
{
	struct cmd_priv *pcmdpriv = &adapter->cmdpriv;
	struct cmd_obj *pcmd;
	struct setkey_parm *psetkeyparm;
	u8 keylen;
	sint ret = _SUCCESS;

	pcmd = kmalloc(sizeof(*pcmd), GFP_ATOMIC);
	if (!pcmd)
		return _FAIL;
	psetkeyparm = kzalloc(sizeof(*psetkeyparm), GFP_ATOMIC);
	if (!psetkeyparm) {
		ret = _FAIL;
		goto err_free_cmd;
	}
	if (psecuritypriv->AuthAlgrthm == 2) { /* 802.1X */
		psetkeyparm->algorithm =
			 (u8)psecuritypriv->XGrpPrivacy;
	} else { /* WEP */
		psetkeyparm->algorithm =
			 (u8)psecuritypriv->PrivacyAlgrthm;
	}
	psetkeyparm->keyid = (u8)keyid;

	switch (psetkeyparm->algorithm) {
	case _WEP40_:
		keylen = 5;
		memcpy(psetkeyparm->key,
			psecuritypriv->DefKey[keyid].skey, keylen);
		break;
	case _WEP104_:
		keylen = 13;
		memcpy(psetkeyparm->key,
			psecuritypriv->DefKey[keyid].skey, keylen);
		break;
	case _TKIP_:
		if (keyid < 1 || keyid > 2) {
			ret = _FAIL;
			goto err_free_parm;
		}
		keylen = 16;
		memcpy(psetkeyparm->key,
			&psecuritypriv->XGrpKey[keyid - 1], keylen);
		psetkeyparm->grpkey = 1;
		break;
	case _AES_:
		if (keyid < 1 || keyid > 2) {
			ret = _FAIL;
			goto err_free_parm;
		}
		keylen = 16;
		memcpy(psetkeyparm->key,
			&psecuritypriv->XGrpKey[keyid - 1], keylen);
		psetkeyparm->grpkey = 1;
		break;
	default:
		ret = _FAIL;
		goto err_free_parm;
	}
	pcmd->cmdcode = _SetKey_CMD_;
	pcmd->parmbuf = (u8 *)psetkeyparm;
	pcmd->cmdsz =  (sizeof(struct setkey_parm));
	pcmd->rsp = NULL;
	pcmd->rspsz = 0;
	INIT_LIST_HEAD(&pcmd->list);
	r8712_enqueue_cmd(pcmdpriv, pcmd);
	return ret;

err_free_parm:
	kfree(psetkeyparm);
err_free_cmd:
	kfree(pcmd);
	return ret;
}

/* adjust IEs for r8712_joinbss_cmd in WMM */
int r8712_restruct_wmm_ie(struct _adapter *adapter, u8 *in_ie, u8 *out_ie,
		    uint in_len, uint initial_out_len)
{
	unsigned int ielength = 0;
	unsigned int i, j;

	i = 12; /* after the fixed IE */
	while (i < in_len) {
		ielength = initial_out_len;
		if (in_ie[i] == 0xDD && in_ie[i + 2] == 0x00 &&
		    in_ie[i + 3] == 0x50 && in_ie[i + 4] == 0xF2 &&
		    in_ie[i + 5] == 0x02 && i + 5 < in_len) {
			/*WMM element ID and OUI*/
			for (j = i; j < i + 9; j++) {
				out_ie[ielength] = in_ie[j];
				ielength++;
			}
			out_ie[initial_out_len + 1] = 0x07;
			out_ie[initial_out_len + 6] = 0x00;
			out_ie[initial_out_len + 8] = 0x00;
			break;
		}
		i += (in_ie[i + 1] + 2); /* to the next IE element */
	}
	return ielength;
}

/*
 * Ported from 8185: IsInPreAuthKeyList().
 *
 * Search by BSSID,
 * Return Value:
 *	-1		:if there is no pre-auth key in the  table
 *	>=0		:if there is pre-auth key, and   return the entry id
 */
static int SecIsInPMKIDList(struct _adapter *Adapter, u8 *bssid)
{
	struct security_priv *psecuritypriv = &Adapter->securitypriv;
	int i = 0;

	do {
		if (psecuritypriv->PMKIDList[i].bUsed &&
		   (!memcmp(psecuritypriv->PMKIDList[i].Bssid,
			    bssid, ETH_ALEN)))
			break;
		else
			i++;
	} while (i < NUM_PMKID_CACHE);

	if (i == NUM_PMKID_CACHE) {
		i = -1; /* Could not find. */
	} else {
		; /* There is one Pre-Authentication Key for the
		   * specific BSSID.
		   */
	}
	return i;
}

sint r8712_restruct_sec_ie(struct _adapter *adapter, u8 *in_ie,
		     u8 *out_ie, uint in_len)
{
	u8 authmode = 0, match;
	u8 sec_ie[255], uncst_oui[4], bkup_ie[255];
	u8 wpa_oui[4] = {0x0, 0x50, 0xf2, 0x01};
	uint ielength, cnt, remove_cnt;
	int iEntry;
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	struct security_priv *psecuritypriv = &adapter->securitypriv;
	uint ndisauthmode = psecuritypriv->ndisauthtype;
	uint ndissecuritytype = psecuritypriv->ndisencryptstatus;

	if ((ndisauthmode == Ndis802_11AuthModeWPA) ||
	    (ndisauthmode == Ndis802_11AuthModeWPAPSK)) {
		authmode = _WPA_IE_ID_;
		uncst_oui[0] = 0x0;
		uncst_oui[1] = 0x50;
		uncst_oui[2] = 0xf2;
	}
	if ((ndisauthmode == Ndis802_11AuthModeWPA2) ||
	    (ndisauthmode == Ndis802_11AuthModeWPA2PSK)) {
		authmode = _WPA2_IE_ID_;
		uncst_oui[0] = 0x0;
		uncst_oui[1] = 0x0f;
		uncst_oui[2] = 0xac;
	}
	switch (ndissecuritytype) {
	case Ndis802_11Encryption1Enabled:
	case Ndis802_11Encryption1KeyAbsent:
		uncst_oui[3] = 0x1;
		break;
	case Ndis802_11Encryption2Enabled:
	case Ndis802_11Encryption2KeyAbsent:
		uncst_oui[3] = 0x2;
		break;
	case Ndis802_11Encryption3Enabled:
	case Ndis802_11Encryption3KeyAbsent:
		uncst_oui[3] = 0x4;
		break;
	default:
		break;
	}
	/*Search required WPA or WPA2 IE and copy to sec_ie[] */
	cnt = 12;
	match = false;
	while (cnt < in_len) {
		if (in_ie[cnt] == authmode) {
			if ((authmode == _WPA_IE_ID_) &&
			    (!memcmp(&in_ie[cnt + 2], &wpa_oui[0], 4))) {
				memcpy(&sec_ie[0], &in_ie[cnt],
					in_ie[cnt + 1] + 2);
				match = true;
				break;
			}
			if (authmode == _WPA2_IE_ID_) {
				memcpy(&sec_ie[0], &in_ie[cnt],
					in_ie[cnt + 1] + 2);
				match = true;
				break;
			}
			if (((authmode == _WPA_IE_ID_) &&
			     (!memcmp(&in_ie[cnt + 2], &wpa_oui[0], 4))) ||
			     (authmode == _WPA2_IE_ID_))
				memcpy(&bkup_ie[0], &in_ie[cnt],
					in_ie[cnt + 1] + 2);
		}
		cnt += in_ie[cnt + 1] + 2; /*get next*/
	}
	/*restruct WPA IE or WPA2 IE in sec_ie[] */
	if (match) {
		if (sec_ie[0] == _WPA_IE_ID_) {
			/* parsing SSN IE to select required encryption
			 * algorithm, and set the bc/mc encryption algorithm
			 */
			while (true) {
				/*check wpa_oui tag*/
				if (memcmp(&sec_ie[2], &wpa_oui[0], 4)) {
					match = false;
					break;
				}
				if ((sec_ie[6] != 0x01) || (sec_ie[7] != 0x0)) {
					/*IE Ver error*/
					match = false;
					break;
				}
				if (!memcmp(&sec_ie[8], &wpa_oui[0], 3)) {
					/* get bc/mc encryption type (group
					 * key type)
					 */
					switch (sec_ie[11]) {
					case 0x0: /*none*/
						psecuritypriv->XGrpPrivacy =
								_NO_PRIVACY_;
						break;
					case 0x1: /*WEP_40*/
						psecuritypriv->XGrpPrivacy =
								_WEP40_;
						break;
					case 0x2: /*TKIP*/
						psecuritypriv->XGrpPrivacy =
								_TKIP_;
						break;
					case 0x3: /*AESCCMP*/
					case 0x4:
						psecuritypriv->XGrpPrivacy =
								_AES_;
						break;
					case 0x5: /*WEP_104*/
						psecuritypriv->XGrpPrivacy =
								_WEP104_;
						break;
					}
				} else {
					match = false;
					break;
				}
				if (sec_ie[12] == 0x01) {
					/*check the unicast encryption type*/
					if (memcmp(&sec_ie[14],
					    &uncst_oui[0], 4)) {
						match = false;
						break;

					} /*else the uncst_oui is match*/
				} else { /*mixed mode, unicast_enc_type > 1*/
					/*select the uncst_oui and remove
					 * the other uncst_oui
					 */
					cnt = sec_ie[12];
					remove_cnt = (cnt - 1) * 4;
					sec_ie[12] = 0x01;
					memcpy(&sec_ie[14], &uncst_oui[0], 4);
					/*remove the other unicast suit*/
					memcpy(&sec_ie[18],
						&sec_ie[18 + remove_cnt],
						sec_ie[1] - 18 + 2 -
						remove_cnt);
					sec_ie[1] = sec_ie[1] - remove_cnt;
				}
				break;
			}
		}
		if (authmode == _WPA2_IE_ID_) {
			/* parsing RSN IE to select required encryption
			 * algorithm, and set the bc/mc encryption algorithm
			 */
			while (true) {
				if ((sec_ie[2] != 0x01) || (sec_ie[3] != 0x0)) {
					/*IE Ver error*/
					match = false;
					break;
				}
				if (!memcmp(&sec_ie[4], &uncst_oui[0], 3)) {
					/*get bc/mc encryption type*/
					switch (sec_ie[7]) {
					case 0x1: /*WEP_40*/
						psecuritypriv->XGrpPrivacy =
								_WEP40_;
						break;
					case 0x2: /*TKIP*/
						psecuritypriv->XGrpPrivacy =
								_TKIP_;
						break;
					case 0x4: /*AESWRAP*/
						psecuritypriv->XGrpPrivacy =
								_AES_;
						break;
					case 0x5: /*WEP_104*/
						psecuritypriv->XGrpPrivacy =
								_WEP104_;
						break;
					default: /*one*/
						psecuritypriv->XGrpPrivacy =
								_NO_PRIVACY_;
						break;
					}
				} else {
					match = false;
					break;
				}
				if (sec_ie[8] == 0x01) {
					/*check the unicast encryption type*/
					if (memcmp(&sec_ie[10],
						     &uncst_oui[0], 4)) {
						match = false;
						break;
					} /*else the uncst_oui is match*/
				} else { /*mixed mode, unicast_enc_type > 1*/
					/*select the uncst_oui and remove the
					 * other uncst_oui
					 */
					cnt = sec_ie[8];
					remove_cnt = (cnt - 1) * 4;
					sec_ie[8] = 0x01;
					memcpy(&sec_ie[10], &uncst_oui[0], 4);
					/*remove the other unicast suit*/
					memcpy(&sec_ie[14],
						&sec_ie[14 + remove_cnt],
						(sec_ie[1] - 14 + 2 -
						remove_cnt));
					sec_ie[1] = sec_ie[1] - remove_cnt;
				}
				break;
			}
		}
	}
	if ((authmode == _WPA_IE_ID_) || (authmode == _WPA2_IE_ID_)) {
		/*copy fixed ie*/
		memcpy(out_ie, in_ie, 12);
		ielength = 12;
		/*copy RSN or SSN*/
		if (match) {
			memcpy(&out_ie[ielength], &sec_ie[0], sec_ie[1] + 2);
			ielength += sec_ie[1] + 2;
			if (authmode == _WPA2_IE_ID_) {
				/*the Pre-Authentication bit should be zero*/
				out_ie[ielength - 1] = 0;
				out_ie[ielength - 2] = 0;
			}
			r8712_report_sec_ie(adapter, authmode, sec_ie);
		}
	} else {
		/*copy fixed ie only*/
		memcpy(out_ie, in_ie, 12);
		ielength = 12;
		if (psecuritypriv->wps_phase) {
			memcpy(out_ie + ielength, psecuritypriv->wps_ie,
			       psecuritypriv->wps_ie_len);
			ielength += psecuritypriv->wps_ie_len;
		}
	}
	iEntry = SecIsInPMKIDList(adapter, pmlmepriv->assoc_bssid);
	if (iEntry < 0)
		return ielength;
	if (authmode == _WPA2_IE_ID_) {
		out_ie[ielength] = 1;
		ielength++;
		out_ie[ielength] = 0;	/*PMKID count = 0x0100*/
		ielength++;
		memcpy(&out_ie[ielength],
			&psecuritypriv->PMKIDList[iEntry].PMKID, 16);
		ielength += 16;
		out_ie[13] += 18;/*PMKID length = 2+16*/
	}
	return ielength;
}

void r8712_init_registrypriv_dev_network(struct _adapter *adapter)
{
	struct registry_priv *pregistrypriv = &adapter->registrypriv;
	struct eeprom_priv *peepriv = &adapter->eeprompriv;
	struct wlan_bssid_ex *pdev_network = &pregistrypriv->dev_network;
	u8 *myhwaddr = myid(peepriv);

	memcpy(pdev_network->MacAddress, myhwaddr, ETH_ALEN);
	memcpy(&pdev_network->Ssid, &pregistrypriv->ssid,
		sizeof(struct ndis_802_11_ssid));
	pdev_network->Configuration.Length =
			 sizeof(struct NDIS_802_11_CONFIGURATION);
	pdev_network->Configuration.BeaconPeriod = 100;
	pdev_network->Configuration.FHConfig.Length = 0;
	pdev_network->Configuration.FHConfig.HopPattern = 0;
	pdev_network->Configuration.FHConfig.HopSet = 0;
	pdev_network->Configuration.FHConfig.DwellTime = 0;
}

void r8712_update_registrypriv_dev_network(struct _adapter *adapter)
{
	int sz = 0;
	struct registry_priv	*pregistrypriv = &adapter->registrypriv;
	struct wlan_bssid_ex	*pdev_network = &pregistrypriv->dev_network;
	struct security_priv	*psecuritypriv = &adapter->securitypriv;
	struct wlan_network	*cur_network = &adapter->mlmepriv.cur_network;

	pdev_network->Privacy = cpu_to_le32(psecuritypriv->PrivacyAlgrthm
					    > 0 ? 1 : 0); /* adhoc no 802.1x */
	pdev_network->Rssi = 0;
	switch (pregistrypriv->wireless_mode) {
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
	default:
		/* TODO */
		break;
	}
	pdev_network->Configuration.DSConfig = cpu_to_le32(
					       pregistrypriv->channel);
	if (cur_network->network.InfrastructureMode == Ndis802_11IBSS)
		pdev_network->Configuration.ATIMWindow = cpu_to_le32(3);
	pdev_network->InfrastructureMode = cpu_to_le32(
				cur_network->network.InfrastructureMode);
	/* 1. Supported rates
	 * 2. IE
	 */
	sz = r8712_generate_ie(pregistrypriv);
	pdev_network->IELength = sz;
	pdev_network->Length = r8712_get_wlan_bssid_ex_sz(pdev_network);
}

/*the function is at passive_level*/
void r8712_joinbss_reset(struct _adapter *padapter)
{
	int i;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct ht_priv		*phtpriv = &pmlmepriv->htpriv;

	/* todo: if you want to do something io/reg/hw setting before join_bss,
	 * please add code here
	 */
	phtpriv->ampdu_enable = false;/*reset to disabled*/
	for (i = 0; i < 16; i++)
		phtpriv->baddbareq_issued[i] = false;/*reset it*/
	if (phtpriv->ht_option) {
		/* validate  usb rx aggregation */
		r8712_write8(padapter, 0x102500D9, 48);/*TH = 48 pages, 6k*/
	} else {
		/* invalidate  usb rx aggregation */
		/* TH=1 => means that invalidate usb rx aggregation */
		r8712_write8(padapter, 0x102500D9, 1);
	}
}

/*the function is >= passive_level*/
unsigned int r8712_restructure_ht_ie(struct _adapter *padapter, u8 *in_ie,
				     u8 *out_ie, uint in_len, uint *pout_len)
{
	u32 ielen, out_len;
	unsigned char *p;
	struct ieee80211_ht_cap ht_capie;
	unsigned char WMM_IE[] = {0x00, 0x50, 0xf2, 0x02, 0x00, 0x01, 0x00};
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct qos_priv *pqospriv = &pmlmepriv->qospriv;
	struct ht_priv *phtpriv = &pmlmepriv->htpriv;

	phtpriv->ht_option = 0;
	p = r8712_get_ie(in_ie + 12, _HT_CAPABILITY_IE_, &ielen, in_len - 12);
	if (p && (ielen > 0)) {
		if (pqospriv->qos_option == 0) {
			out_len = *pout_len;
			r8712_set_ie(out_ie + out_len, _VENDOR_SPECIFIC_IE_,
				     _WMM_IE_Length_, WMM_IE, pout_len);
			pqospriv->qos_option = 1;
		}
		out_len = *pout_len;
		memset(&ht_capie, 0, sizeof(struct ieee80211_ht_cap));
		ht_capie.cap_info = IEEE80211_HT_CAP_SUP_WIDTH |
				    IEEE80211_HT_CAP_SGI_20 |
				    IEEE80211_HT_CAP_SGI_40 |
				    IEEE80211_HT_CAP_TX_STBC |
				    IEEE80211_HT_CAP_MAX_AMSDU |
				    IEEE80211_HT_CAP_DSSSCCK40;
		ht_capie.ampdu_params_info = (IEEE80211_HT_CAP_AMPDU_FACTOR &
				0x03) | (IEEE80211_HT_CAP_AMPDU_DENSITY & 0x00);
		r8712_set_ie(out_ie + out_len, _HT_CAPABILITY_IE_,
			     sizeof(struct ieee80211_ht_cap),
			     (unsigned char *)&ht_capie, pout_len);
		phtpriv->ht_option = 1;
	}
	return phtpriv->ht_option;
}

/* the function is > passive_level (in critical_section) */
static void update_ht_cap(struct _adapter *padapter, u8 *pie, uint ie_len)
{
	u8 *p, max_ampdu_sz;
	int i, len;
	struct sta_info *bmc_sta, *psta;
	struct ieee80211_ht_cap *pht_capie;
	struct recv_reorder_ctrl *preorder_ctrl;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct ht_priv *phtpriv = &pmlmepriv->htpriv;
	struct registry_priv *pregistrypriv = &padapter->registrypriv;
	struct wlan_network *pcur_network = &(pmlmepriv->cur_network);

	if (!phtpriv->ht_option)
		return;
	/* maybe needs check if ap supports rx ampdu. */
	if (!phtpriv->ampdu_enable &&
	    (pregistrypriv->ampdu_enable == 1))
		phtpriv->ampdu_enable = true;
	/*check Max Rx A-MPDU Size*/
	len = 0;
	p = r8712_get_ie(pie + sizeof(struct NDIS_802_11_FIXED_IEs),
				_HT_CAPABILITY_IE_,
				&len, ie_len -
				sizeof(struct NDIS_802_11_FIXED_IEs));
	if (p && len > 0) {
		pht_capie = (struct ieee80211_ht_cap *)(p + 2);
		max_ampdu_sz = (pht_capie->ampdu_params_info &
				IEEE80211_HT_CAP_AMPDU_FACTOR);
		/* max_ampdu_sz (kbytes); */
		max_ampdu_sz = 1 << (max_ampdu_sz + 3);
		phtpriv->rx_ampdu_maxlen = max_ampdu_sz;
	}
	/* for A-MPDU Rx reordering buffer control for bmc_sta & sta_info
	 * if A-MPDU Rx is enabled, resetting rx_ordering_ctrl
	 * wstart_b(indicate_seq) to default value=0xffff
	 * todo: check if AP can send A-MPDU packets
	 */
	bmc_sta = r8712_get_bcmc_stainfo(padapter);
	if (bmc_sta) {
		for (i = 0; i < 16; i++) {
			preorder_ctrl = &bmc_sta->recvreorder_ctrl[i];
			preorder_ctrl->indicate_seq = 0xffff;
			preorder_ctrl->wend_b = 0xffff;
		}
	}
	psta = r8712_get_stainfo(&padapter->stapriv,
				 pcur_network->network.MacAddress);
	if (psta) {
		for (i = 0; i < 16; i++) {
			preorder_ctrl = &psta->recvreorder_ctrl[i];
			preorder_ctrl->indicate_seq = 0xffff;
			preorder_ctrl->wend_b = 0xffff;
		}
	}
	len = 0;
	p = r8712_get_ie(pie + sizeof(struct NDIS_802_11_FIXED_IEs),
		   _HT_ADD_INFO_IE_, &len,
		   ie_len - sizeof(struct NDIS_802_11_FIXED_IEs));
}

void r8712_issue_addbareq_cmd(struct _adapter *padapter, int priority)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct ht_priv	 *phtpriv = &pmlmepriv->htpriv;

	if ((phtpriv->ht_option == 1) && (phtpriv->ampdu_enable)) {
		if (!phtpriv->baddbareq_issued[priority]) {
			r8712_addbareq_cmd(padapter, (u8)priority);
			phtpriv->baddbareq_issued[priority] = true;
		}
	}
}
