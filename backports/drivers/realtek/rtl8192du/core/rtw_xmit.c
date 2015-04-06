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
 *
 ******************************************************************************/
#define _RTW_XMIT_C_

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <wifi.h>
#include <osdep_intf.h>
#include <linux/ip.h>
#include <usb_osintf.h>
#include <usb_ops.h>
#include <linux/vmalloc.h>

static u8 P802_1H_OUI[P80211_OUI_LEN] = { 0x00, 0x00, 0xf8 };
static u8 RFC1042_OUI[P80211_OUI_LEN] = { 0x00, 0x00, 0x00 };

static void _init_txservq(struct tx_servq *ptxservq)
{

	INIT_LIST_HEAD(&ptxservq->tx_pending);
	_rtw_init_queue(&ptxservq->sta_pending);
	ptxservq->qcnt = 0;

}

void	_rtw_init_sta_xmit_priv(struct sta_xmit_priv *psta_xmitpriv)
{

	memset((unsigned char *)psta_xmitpriv, 0, sizeof(struct sta_xmit_priv));

	_rtw_spinlock_init(&psta_xmitpriv->lock);

	/* for (i = 0 ; i < MAX_NUMBLKS; i++) */
	/*	_init_txservq(&(psta_xmitpriv->blk_q[i])); */

	_init_txservq(&psta_xmitpriv->be_q);
	_init_txservq(&psta_xmitpriv->bk_q);
	_init_txservq(&psta_xmitpriv->vi_q);
	_init_txservq(&psta_xmitpriv->vo_q);
	INIT_LIST_HEAD(&psta_xmitpriv->legacy_dz);
	INIT_LIST_HEAD(&psta_xmitpriv->apsd);

}

s32	_rtw_init_xmit_priv(struct xmit_priv *pxmitpriv, struct rtw_adapter *padapter)
{
	int i;
	struct xmit_buf *pxmitbuf;
	struct xmit_frame *pxframe;
	int	res = _SUCCESS;

	/*  We don't need to memset padapter->XXX to zero, because adapter is allocated by vzalloc(). */
	/* memset((unsigned char *)pxmitpriv, 0, sizeof(struct xmit_priv)); */

	_rtw_spinlock_init(&pxmitpriv->lock);
	_rtw_init_sema(&pxmitpriv->xmit_sema, 0);
	_rtw_init_sema(&pxmitpriv->terminate_xmitthread_sema, 0);

	/*
	Please insert all the queue initializaiton using _rtw_init_queue below
	*/

	pxmitpriv->adapter = padapter;

	_rtw_init_queue(&pxmitpriv->be_pending);
	_rtw_init_queue(&pxmitpriv->bk_pending);
	_rtw_init_queue(&pxmitpriv->vi_pending);
	_rtw_init_queue(&pxmitpriv->vo_pending);
	_rtw_init_queue(&pxmitpriv->bm_pending);

	_rtw_init_queue(&pxmitpriv->free_xmit_queue);

	/*
	Please allocate memory with the sz = (struct xmit_frame) * NR_XMITFRAME,
	and initialize free_xmit_frame below.
	Please also apply  free_txobj to link_up all the xmit_frames...
	*/

	pxmitpriv->pallocated_frame_buf = vzalloc(NR_XMITFRAME * sizeof(struct xmit_frame) + 4);

	if (pxmitpriv->pallocated_frame_buf  == NULL) {
		pxmitpriv->pxmit_frame_buf = NULL;
		RT_TRACE(_module_rtl871x_xmit_c_, _drv_err_, ("alloc xmit_frame fail!\n"));
		res = _FAIL;
		goto exit;
	}
	pxmitpriv->pxmit_frame_buf = (u8 *)N_BYTE_ALIGMENT((SIZE_PTR)(pxmitpriv->pallocated_frame_buf), 4);

	pxframe = (struct xmit_frame *)pxmitpriv->pxmit_frame_buf;

	for (i = 0; i < NR_XMITFRAME; i++) {
		INIT_LIST_HEAD(&(pxframe->list));

		pxframe->padapter = padapter;
		pxframe->frame_tag = NULL_FRAMETAG;

		pxframe->pkt = NULL;

		pxframe->buf_addr = NULL;
		pxframe->pxmitbuf = NULL;

		rtw_list_insert_tail(&(pxframe->list), &(pxmitpriv->free_xmit_queue.queue));

		pxframe++;
	}

	pxmitpriv->free_xmitframe_cnt = NR_XMITFRAME;

	pxmitpriv->frag_len = MAX_FRAG_THRESHOLD;

	/* init xmit_buf */
	_rtw_init_queue(&pxmitpriv->free_xmitbuf_queue);
	_rtw_init_queue(&pxmitpriv->pending_xmitbuf_queue);

	pxmitpriv->pallocated_xmitbuf = vzalloc(NR_XMITBUFF * sizeof(struct xmit_buf) + 4);

	if (pxmitpriv->pallocated_xmitbuf  == NULL) {
		RT_TRACE(_module_rtl871x_xmit_c_, _drv_err_, ("alloc xmit_buf fail!\n"));
		res = _FAIL;
		goto exit;
	}

	pxmitpriv->pxmitbuf = (u8 *)N_BYTE_ALIGMENT((SIZE_PTR)(pxmitpriv->pallocated_xmitbuf), 4);

	pxmitbuf = (struct xmit_buf *)pxmitpriv->pxmitbuf;

	for (i = 0; i < NR_XMITBUFF; i++) {
		INIT_LIST_HEAD(&pxmitbuf->list);

		pxmitbuf->priv_data = NULL;
		pxmitbuf->padapter = padapter;
		pxmitbuf->ext_tag = false;

		res = rtw_os_xmit_resource_alloc(padapter, pxmitbuf, (MAX_XMITBUF_SZ + XMITBUF_ALIGN_SZ));
		if (res == _FAIL)
			goto exit;

		pxmitbuf->flags = XMIT_VO_QUEUE;

		rtw_list_insert_tail(&pxmitbuf->list, &(pxmitpriv->free_xmitbuf_queue.queue));
		#ifdef DBG_XMIT_BUF
		pxmitbuf->no = i;
		#endif

		pxmitbuf++;
	}
	pxmitpriv->free_xmitbuf_cnt = NR_XMITBUFF;

	/*  Init xmit extension buff */
	_rtw_init_queue(&pxmitpriv->free_xmit_extbuf_queue);

	pxmitpriv->pallocated_xmit_extbuf = vzalloc(NR_XMIT_EXTBUFF *
						    sizeof(struct xmit_buf)+4);

	if (pxmitpriv->pallocated_xmit_extbuf  == NULL) {
		RT_TRACE(_module_rtl871x_xmit_c_, _drv_err_, ("alloc xmit_extbuf fail!\n"));
		res = _FAIL;
		goto exit;
	}

	pxmitpriv->pxmit_extbuf = (u8 *)N_BYTE_ALIGMENT((SIZE_PTR)(pxmitpriv->pallocated_xmit_extbuf), 4);

	pxmitbuf = (struct xmit_buf *)pxmitpriv->pxmit_extbuf;

	for (i = 0; i < NR_XMIT_EXTBUFF; i++) {
		INIT_LIST_HEAD(&pxmitbuf->list);

		pxmitbuf->priv_data = NULL;
		pxmitbuf->padapter = padapter;
		pxmitbuf->ext_tag = true;

		res = rtw_os_xmit_resource_alloc(padapter, pxmitbuf, MAX_XMIT_EXTBUF_SZ + XMITBUF_ALIGN_SZ);
		if (res == _FAIL)
			goto exit;

		rtw_list_insert_tail(&pxmitbuf->list, &(pxmitpriv->free_xmit_extbuf_queue.queue));
		#ifdef DBG_XMIT_BUF
		pxmitbuf->no = i;
		#endif
		pxmitbuf++;
	}

	pxmitpriv->free_xmit_extbuf_cnt = NR_XMIT_EXTBUFF;

	rtw_alloc_hwxmits(padapter);
	rtw_init_hwxmits(pxmitpriv->hwxmits, pxmitpriv->hwxmit_entry);

	pxmitpriv->txirp_cnt = 1;

	_rtw_init_sema(&(pxmitpriv->tx_retevt), 0);

	/* per AC pending irp */
	pxmitpriv->beq_cnt = 0;
	pxmitpriv->bkq_cnt = 0;
	pxmitpriv->viq_cnt = 0;
	pxmitpriv->voq_cnt = 0;

	pxmitpriv->ack_tx = false;
	_rtw_mutex_init(&pxmitpriv->ack_tx_mutex);
	rtw_sctx_init(&pxmitpriv->ack_tx_ops, 0);
	rtw_hal_init_xmit_priv(padapter);
exit:
	return res;
}

static void  rtw_mfree_xmit_priv_lock(struct xmit_priv *pxmitpriv)
{
	_rtw_spinlock_free(&pxmitpriv->lock);
	_rtw_free_sema(&pxmitpriv->xmit_sema);
	_rtw_free_sema(&pxmitpriv->terminate_xmitthread_sema);

	_rtw_spinlock_free(&pxmitpriv->be_pending.lock);
	_rtw_spinlock_free(&pxmitpriv->bk_pending.lock);
	_rtw_spinlock_free(&pxmitpriv->vi_pending.lock);
	_rtw_spinlock_free(&pxmitpriv->vo_pending.lock);
	_rtw_spinlock_free(&pxmitpriv->bm_pending.lock);

	_rtw_spinlock_free(&pxmitpriv->free_xmit_queue.lock);
	_rtw_spinlock_free(&pxmitpriv->free_xmitbuf_queue.lock);
	_rtw_spinlock_free(&pxmitpriv->pending_xmitbuf_queue.lock);
}

void _rtw_free_xmit_priv(struct xmit_priv *pxmitpriv)
{
	int i;
	struct rtw_adapter *padapter = pxmitpriv->adapter;
	struct xmit_frame	*pxmitframe = (struct xmit_frame *)pxmitpriv->pxmit_frame_buf;
	struct xmit_buf *pxmitbuf = (struct xmit_buf *)pxmitpriv->pxmitbuf;

	rtw_hal_free_xmit_priv(padapter);
	rtw_mfree_xmit_priv_lock(pxmitpriv);
	if (pxmitpriv->pxmit_frame_buf == NULL)
		return;

	for (i = 0; i < NR_XMITFRAME; i++) {
		rtw_os_xmit_complete(padapter, pxmitframe);
		pxmitframe++;
	}
	for (i = 0; i < NR_XMITBUFF; i++) {
		rtw_os_xmit_resource_free(padapter, pxmitbuf, (MAX_XMITBUF_SZ + XMITBUF_ALIGN_SZ));
		pxmitbuf++;
	}
	if (pxmitpriv->pallocated_frame_buf)
		vfree(pxmitpriv->pallocated_frame_buf);
	if (pxmitpriv->pallocated_xmitbuf)
		vfree(pxmitpriv->pallocated_xmitbuf);

	/*  free xmit extension buff */
	_rtw_spinlock_free(&pxmitpriv->free_xmit_extbuf_queue.lock);

	pxmitbuf = (struct xmit_buf *)pxmitpriv->pxmit_extbuf;
	for (i = 0; i < NR_XMIT_EXTBUFF; i++) {
		rtw_os_xmit_resource_free(padapter, pxmitbuf, (MAX_XMIT_EXTBUF_SZ + XMITBUF_ALIGN_SZ));

		pxmitbuf++;
	}

	if (pxmitpriv->pallocated_xmit_extbuf)
		vfree(pxmitpriv->pallocated_xmit_extbuf);

	rtw_free_hwxmits(padapter);

	_rtw_mutex_free(&pxmitpriv->ack_tx_mutex);
}

static void update_attrib_vcs_info(struct rtw_adapter *padapter, struct xmit_frame *pxmitframe)
{
	u32	sz;
	struct pkt_attrib	*pattrib = &pxmitframe->attrib;
	struct sta_info	*psta = pattrib->psta;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	if (pattrib->nr_frags != 1)
		sz = padapter->xmitpriv.frag_len;
	else /* no frag */
		sz = pattrib->last_txcmdsz;

	/*  (1) RTS_Threshold is compared to the MPDU, not MSDU. */
	/*  (2) If there are more than one frag in  this MSDU, only the first frag uses protection frame. */
	/*		Other fragments are protected by previous fragment. */
	/*		So we only need to check the length of first fragment. */
	if (pmlmeext->cur_wireless_mode < WIRELESS_11_24N  || padapter->registrypriv.wifi_spec) {
		if (sz > padapter->registrypriv.rts_thresh) {
			pattrib->vcs_mode = RTS_CTS;
		} else {
			if (psta->rtsen)
				pattrib->vcs_mode = RTS_CTS;
			else if (psta->cts2self)
				pattrib->vcs_mode = CTS_TO_SELF;
			else
				pattrib->vcs_mode = NONE_VCS;
		}
	} else {
		while (true) {
			/* IOT action */
			if ((pmlmeinfo->assoc_AP_vendor == atherosAP) && (pattrib->ampdu_en == true) &&
			    (padapter->securitypriv.dot11PrivacyAlgrthm == _AES_)) {
				pattrib->vcs_mode = CTS_TO_SELF;
				break;
			}

			/* check ERP protection */
			if (psta->rtsen || psta->cts2self) {
				if (psta->rtsen)
					pattrib->vcs_mode = RTS_CTS;
				else if (psta->cts2self)
					pattrib->vcs_mode = CTS_TO_SELF;

				break;
			}

			/* check HT op mode */
			if (pattrib->ht_en) {
				u8 ht_op_mode = pmlmeinfo->HT_protection;
				if ((pmlmeext->cur_bwmode && (ht_op_mode == 2 || ht_op_mode == 3)) ||
				    (!pmlmeext->cur_bwmode && ht_op_mode == 3)) {
					pattrib->vcs_mode = RTS_CTS;
					break;
				}
			}

			/* check rts */
			if (sz > padapter->registrypriv.rts_thresh) {
				pattrib->vcs_mode = RTS_CTS;
				break;
			}

			/* to do list: check MIMO power save condition. */

			/* check AMPDU aggregation for TXOP */
			if (pattrib->ampdu_en == true) {
				pattrib->vcs_mode = RTS_CTS;
				break;
			}

			pattrib->vcs_mode = NONE_VCS;
			break;
		}
	}
}

static void update_attrib_phy_info(struct pkt_attrib *pattrib, struct sta_info *psta)
{
	pattrib->mdata = 0;
	pattrib->eosp = 0;
	pattrib->triggered = 0;

	/* qos_en, ht_en, init rate, , bw, ch_offset, sgi */
	pattrib->qos_en = psta->qos_option;
	pattrib->ht_en = psta->htpriv.ht_option;
	pattrib->raid = psta->raid;
	pattrib->bwmode = psta->htpriv.bwmode;
	pattrib->ch_offset = psta->htpriv.ch_offset;
	pattrib->sgi = psta->htpriv.sgi;
	pattrib->ampdu_en = false;

	pattrib->retry_ctrl = false;
}

u8	qos_acm(u8 acm_mask, u8 priority)
{
	u8	change_priority = priority;

	switch (priority) {
	case 0:
	case 3:
		if (acm_mask & BIT(1))
			change_priority = 1;
		break;
	case 1:
	case 2:
		break;
	case 4:
	case 5:
		if (acm_mask & BIT(2))
			change_priority = 0;
		break;
	case 6:
	case 7:
		if (acm_mask & BIT(3))
			change_priority = 5;
		break;
	default:
		DBG_8192D("qos_acm(): invalid pattrib->priority: %d!!!\n", priority);
		break;
	}

	return change_priority;
}

static void set_qos(struct sk_buff *skb, struct pkt_attrib *pattrib)
{
	u8 *pframe = skb->data;
	struct iphdr *ip_hdr;
	u8 userpriority = 0;

	/*  get userpriority from IP hdr */
	if (pattrib->ether_type == ETH_P_IP) {
		ip_hdr = (struct iphdr *)(pframe + ETH_HLEN);
		userpriority = ip_hdr->tos >> 5;
	} else if (pattrib->ether_type == ETH_P_PAE) {
		/*  "When priority processing of data frames is supported, */
		/*  a STA's SME should send EAPOL-Key frames at the highest
		    priority." */
		userpriority = 7;
	}

	pattrib->priority = userpriority;
	pattrib->hdrlen = sizeof(struct ieee80211_qos_hdr);
	pattrib->subtype = WIFI_QOS_DATA_TYPE;
}

static int update_attrib(struct rtw_adapter *padapter,
			 struct sk_buff *skb, struct pkt_attrib *pattrib)
{
	struct sta_info *psta = NULL;
	int bmcast;
	struct sta_priv	*pstapriv = &padapter->stapriv;
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	int res = _SUCCESS;
	struct ethhdr *ehdr = (struct ethhdr *) skb->data;

	pattrib->ether_type = ntohs(ehdr->h_proto);

	ether_addr_copy(pattrib->dst, ehdr->h_dest);
	ether_addr_copy(pattrib->src, ehdr->h_source);

	pattrib->pctrl = 0;

	if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) ||
	    check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE)) {
		ether_addr_copy(pattrib->ra, pattrib->dst);
		ether_addr_copy(pattrib->ta, pattrib->src);
	} else if (check_fwstate(pmlmepriv, WIFI_STATION_STATE)) {
		ether_addr_copy(pattrib->ra, get_bssid(pmlmepriv));
		ether_addr_copy(pattrib->ta, pattrib->src);
	} else if (check_fwstate(pmlmepriv, WIFI_AP_STATE)) {
		ether_addr_copy(pattrib->ra, pattrib->dst);
		ether_addr_copy(pattrib->ta, get_bssid(pmlmepriv));
	}

	pattrib->pktlen = skb->len - ETH_HLEN;

	if (pattrib->ether_type == ETH_P_IP) {
		/*  The following is for DHCP and ARP packet, we use cck1M
		    to tx these packets and let LPS awake some time */
		/*  to prevent DHCP protocol fail */
		pattrib->dhcp_pkt = 0;
		/* MINIMUM_DHCP_PACKET_SIZE) { */
		if (pattrib->pktlen > 282 + 24) {
			if (pattrib->ether_type == ETH_P_IP) {/*  IP header */
				u8 *pframe = skb->data;
				pframe += ETH_HLEN;

				if ((pframe[21] == 68 && pframe[23] == 67) ||
				    (pframe[21] == 67 && pframe[23] == 68)) {
					/*  68 : UDP BOOTP client */
					/*  67 : UDP BOOTP server */
					RT_TRACE(_module_rtl871x_xmit_c_,
						 _drv_err_,
						 ("======================"
						  "update_attrib: get DHCP "
						  "Packet\n"));
					pattrib->dhcp_pkt = 1;
				}
			}
		}
	}

	if ((pattrib->ether_type == ETH_P_PAE) || (pattrib->dhcp_pkt == 1)) {
		rtw_set_scan_deny(padapter, 3000);
	}

#ifdef CONFIG_LPS
	/*  If EAPOL , ARP , OR DHCP packet, driver must be in active mode. */
	if ((pattrib->ether_type == ETH_P_ARP) ||
	    (pattrib->ether_type == ETH_P_PAE) || (pattrib->dhcp_pkt == 1)) {
		rtw_lps_ctrl_wk_cmd(padapter, LPS_CTRL_SPECIAL_PACKET, 1);
	}
#endif

	bmcast = is_multicast_ether_addr(pattrib->ra);

	/*  get sta_info */
	if (bmcast) {
		psta = rtw_get_bcmc_stainfo(padapter);
	} else {
		psta = rtw_get_stainfo(pstapriv, pattrib->ra);
		if (psta == NULL) { /*  if we cannot get psta => drop the pkt */
			RT_TRACE(_module_rtl871x_xmit_c_, _drv_alert_,
				 ("\nupdate_attrib => get sta_info fail, ra:%pM\n", pattrib->ra));
			res = _FAIL;
			goto exit;
		} else if (check_fwstate(pmlmepriv, WIFI_AP_STATE) &&
			   (!(psta->state & _FW_LINKED))) {
			res = _FAIL;
			goto exit;
		}
	}

	if (psta) {
		pattrib->mac_id = psta->mac_id;
		pattrib->psta = psta;
	} else {
		/*  if we cannot get psta => drop the pkt */
		RT_TRACE(_module_rtl871x_xmit_c_, _drv_alert_,
			 ("\nupdate_attrib => get sta_info fail, ra:%pM\n", pattrib->ra));
		res = _FAIL;
		goto exit;
	}

	pattrib->ack_policy = 0;
	/*  get ether_hdr_len */

	/* pattrib->ether_type == 0x8100) ? (14 + 4): 14; vlan tag */
	pattrib->pkt_hdrlen = ETH_HLEN;

	pattrib->hdrlen = sizeof(struct ieee80211_hdr_3addr);
	pattrib->subtype = WIFI_DATA_TYPE;
	pattrib->priority = 0;

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE | WIFI_ADHOC_STATE |
			  WIFI_ADHOC_MASTER_STATE)) {
		if (psta->qos_option)
			set_qos(skb, pattrib);
	} else {
		if (psta->qos_option) {
			set_qos(skb, pattrib);

			if (pmlmepriv->acm_mask != 0) {
				pattrib->priority = qos_acm(pmlmepriv->acm_mask,
							    pattrib->priority);
			}
		}
	}

	if (psta->ieee8021x_blocked == true) {
		RT_TRACE(_module_rtl871x_xmit_c_, _drv_err_,
			 ("\n psta->ieee8021x_blocked == true\n"));

		pattrib->encrypt = 0;

		if ((pattrib->ether_type != ETH_P_PAE) &&
		    !check_fwstate(pmlmepriv, WIFI_MP_STATE)) {
			RT_TRACE(_module_rtl871x_xmit_c_, _drv_err_,
				 ("\npsta->ieee8021x_blocked == true,  "
				  "pattrib->ether_type(%.4x) != 0x888e\n",
				  pattrib->ether_type));
			res = _FAIL;
			goto exit;
		}
	} else {
		GET_ENCRY_ALGO(psecuritypriv, psta, pattrib->encrypt, bmcast);

		switch (psecuritypriv->dot11AuthAlgrthm) {
		case dot11AuthAlgrthm_Open:
		case dot11AuthAlgrthm_Shared:
		case dot11AuthAlgrthm_Auto:
			pattrib->key_idx =
				(u8)psecuritypriv->dot11PrivacyKeyIndex;
			break;
		case dot11AuthAlgrthm_8021X:
			if (bmcast)
				pattrib->key_idx =
					(u8)psecuritypriv->dot118021XGrpKeyid;
			else
				pattrib->key_idx = 0;
			break;
		default:
			pattrib->key_idx = 0;
			break;
		}

	}

	switch (pattrib->encrypt) {
	case _WEP40_:
	case _WEP104_:
		pattrib->iv_len = IEEE80211_WEP_IV_LEN;
		pattrib->icv_len = IEEE80211_WEP_ICV_LEN;
		break;

	case _TKIP_:
		pattrib->iv_len = IEEE80211_TKIP_IV_LEN;
		pattrib->icv_len = IEEE80211_TKIP_ICV_LEN;

		if (!padapter->securitypriv.busetkipkey) {
			RT_TRACE(_module_rtl871x_xmit_c_, _drv_err_,
				 ("\npadapter->securitypriv.busetkip"
				  "key(%d) == false drop packet\n",
				  padapter->securitypriv.busetkipkey));
			res = _FAIL;
			goto exit;
		}
		break;
	case _AES_:
		RT_TRACE(_module_rtl871x_xmit_c_, _drv_info_,
			 ("pattrib->encrypt =%d (WLAN_CIPHER_SUITE_CCMP)\n",
			  pattrib->encrypt));
		pattrib->iv_len = IEEE80211_CCMP_HDR_LEN;
		pattrib->icv_len = IEEE80211_CCMP_MIC_LEN;
		break;
	default:
		pattrib->iv_len = 0;
		pattrib->icv_len = 0;
		break;
	}

	RT_TRACE(_module_rtl871x_xmit_c_, _drv_info_,
		 ("update_attrib: encrypt =%d\n", pattrib->encrypt));

	if (pattrib->encrypt && !psecuritypriv->hw_decrypted) {
		pattrib->bswenc = true;
		RT_TRACE(_module_rtl871x_xmit_c_, _drv_err_,
			 ("update_attrib: encrypt =%d bswenc = true\n",
			  pattrib->encrypt));
	} else {
		pattrib->bswenc = false;
		RT_TRACE(_module_rtl871x_xmit_c_, _drv_info_,
			 ("update_attrib: bswenc = false\n"));
	}
#ifdef CONFIG_CONCURRENT_MODE
	if ((pattrib->encrypt && bmcast) || (pattrib->encrypt == _WEP40_) || (pattrib->encrypt == _WEP104_))
		pattrib->bswenc = true;/* force using sw enc. */
#endif

	rtw_set_tx_chksum_offload(skb, pattrib);

	update_attrib_phy_info(pattrib, psta);

exit:
	return res;
}

static s32 xmitframe_addmic(struct rtw_adapter *padapter,
			    struct xmit_frame *pxmitframe)
{
	int curfragnum, length;
	u8 *pframe, *payload, mic[8];
	struct	mic_data micdata;
	struct	sta_info *stainfo;
	struct	qos_priv *pqospriv = &(padapter->mlmepriv.qospriv);
	struct	pkt_attrib *pattrib = &pxmitframe->attrib;
	struct	security_priv *psecuritypriv = &padapter->securitypriv;
	struct	xmit_priv *pxmitpriv = &padapter->xmitpriv;
	u8 priority[4] = {0x0, 0x0, 0x0, 0x0};
	int bmcst = IS_MCAST(pattrib->ra);

	if (pattrib->psta)
		stainfo = pattrib->psta;
	else
		stainfo = rtw_get_stainfo(&padapter->stapriv, &pattrib->ra[0]);

	if (pattrib->encrypt == _TKIP_) {
		/* encode mic code */
		if (stainfo != NULL) {
			u8 null_key[16] = {0};

			pframe = pxmitframe->buf_addr + TXDESC_SIZE +
				 (pxmitframe->pkt_offset * PACKET_OFFSET_SZ);

			if (bmcst) {
				if (_rtw_memcmp(psecuritypriv->dot118021XGrptxmickey[psecuritypriv->dot118021XGrpKeyid].skey, null_key, 16) == true) {
					return _FAIL;
				}
				/* start to calculate the mic code */
				rtw_secmicsetkey(&micdata, psecuritypriv->dot118021XGrptxmickey[psecuritypriv->dot118021XGrpKeyid].skey);
			} else {
				if (_rtw_memcmp(&stainfo->dot11tkiptxmickey.skey[0], null_key, 16) == true) {
					/* DbgPrint("\nxmitframe_addmic:stainfo->dot11tkiptxmickey == 0\n"); */
					/* rtw_msleep_os(10); */
					return _FAIL;
				}
				/* start to calculate the mic code */
				rtw_secmicsetkey(&micdata, &stainfo->dot11tkiptxmickey.skey[0]);
			}

			if (pframe[1]&1) {   /* ToDS == 1 */
				rtw_secmicappend(&micdata, &pframe[16], 6);  /* DA */
				if (pframe[1]&2)  /* From Ds == 1 */
					rtw_secmicappend(&micdata, &pframe[24], 6);
				else
				rtw_secmicappend(&micdata, &pframe[10], 6);
			} else {	/* ToDS == 0 */
				rtw_secmicappend(&micdata, &pframe[4], 6);   /* DA */
				if (pframe[1]&2)  /* From Ds == 1 */
					rtw_secmicappend(&micdata, &pframe[16], 6);
				else
					rtw_secmicappend(&micdata, &pframe[10], 6);
			}
			if (pattrib->qos_en)
				priority[0] = (u8)pxmitframe->attrib.priority;
			rtw_secmicappend(&micdata, &priority[0], 4);

			payload = pframe;

			for (curfragnum = 0; curfragnum < pattrib->nr_frags; curfragnum++) {
				payload = (u8 *)RND4((SIZE_PTR)(payload));
				RT_TRACE(_module_rtl871x_xmit_c_, _drv_err_,
					 ("=== curfragnum =%d, pframe = 0x%.2x, 0x%.2x, 0x%.2x, 0x%.2x, 0x%.2x, 0x%.2x, 0x%.2x, 0x%.2x,!!!\n",
					 curfragnum, *payload, *(payload+1),
					 *(payload+2), *(payload+3),
					 *(payload+4), *(payload+5),
					 *(payload+6), *(payload+7)));

				payload = payload+pattrib->hdrlen+pattrib->iv_len;
				RT_TRACE(_module_rtl871x_xmit_c_, _drv_err_,
					 ("curfragnum =%d pattrib->hdrlen =%d pattrib->iv_len =%d",
					 curfragnum, pattrib->hdrlen,
					 pattrib->iv_len));
				if ((curfragnum+1) == pattrib->nr_frags) {
					length = pattrib->last_txcmdsz-pattrib->hdrlen-pattrib->iv_len-((pattrib->bswenc) ? pattrib->icv_len : 0);
					rtw_secmicappend(&micdata, payload, length);
					payload = payload+length;
				} else {
					length = pxmitpriv->frag_len-pattrib->hdrlen-pattrib->iv_len-((pattrib->bswenc) ? pattrib->icv_len : 0);
					rtw_secmicappend(&micdata, payload, length);
					payload = payload+length+pattrib->icv_len;
					RT_TRACE(_module_rtl871x_xmit_c_, _drv_err_,
						 ("curfragnum =%d length =%d pattrib->icv_len =%d",
						 curfragnum, length, pattrib->icv_len));
				}
			}
			rtw_secgetmic(&micdata, &(mic[0]));
			RT_TRACE(_module_rtl871x_xmit_c_, _drv_err_,
				 ("xmitframe_addmic: before add mic code!!!\n"));
			RT_TRACE(_module_rtl871x_xmit_c_, _drv_err_,
				 ("xmitframe_addmic: pattrib->last_txcmdsz =%d!!!\n",
				 pattrib->last_txcmdsz));
			RT_TRACE(_module_rtl871x_xmit_c_, _drv_err_,
				 ("xmitframe_addmic: mic[0]= 0x%.2x , mic[1]= 0x%.2x , mic[2]= 0x%.2x , mic[3]= 0x%.2x\nmic[4]= 0x%.2x , mic[5]= 0x%.2x , mic[6]= 0x%.2x , mic[7]= 0x%.2x !!!!\n",
				 mic[0], mic[1], mic[2], mic[3], mic[4], mic[5],
				 mic[6], mic[7]));
			/* add mic code  and add the mic code length in last_txcmdsz */

			memcpy(payload, &(mic[0]), 8);
			pattrib->last_txcmdsz += 8;

			RT_TRACE(_module_rtl871x_xmit_c_, _drv_info_, ("\n ======== last pkt ========\n"));
			payload = payload-pattrib->last_txcmdsz+8;
			for (curfragnum = 0; curfragnum < pattrib->last_txcmdsz; curfragnum += 8)
				RT_TRACE(_module_rtl871x_xmit_c_, _drv_info_,
					 (" %.2x,  %.2x,  %.2x,  %.2x,  %.2x,  %.2x,  %.2x,  %.2x ",
					 *(payload+curfragnum),
					 *(payload+curfragnum+1),
					 *(payload+curfragnum+2),
					 *(payload+curfragnum+3),
					 *(payload+curfragnum+4),
					 *(payload+curfragnum+5),
					 *(payload+curfragnum+6),
					 *(payload+curfragnum+7)));
		} else {
			RT_TRACE(_module_rtl871x_xmit_c_, _drv_err_, ("xmitframe_addmic: rtw_get_stainfo == NULL!!!\n"));
		}
	}

	return _SUCCESS;
}

static s32 xmitframe_swencrypt(struct rtw_adapter *padapter, struct xmit_frame *pxmitframe)
{
	struct	pkt_attrib	 *pattrib = &pxmitframe->attrib;

	if (pattrib->bswenc) {
		RT_TRACE(_module_rtl871x_xmit_c_, _drv_alert_, ("### xmitframe_swencrypt\n"));
		switch (pattrib->encrypt) {
		case _WEP40_:
		case _WEP104_:
			rtw_wep_encrypt(padapter, (u8 *)pxmitframe);
			break;
		case _TKIP_:
			rtw_tkip_encrypt(padapter, (u8 *)pxmitframe);
			break;
		case _AES_:
			rtw_aes_encrypt(padapter, (u8 *)pxmitframe);
			break;
		default:
				break;
		}

	} else {
		RT_TRACE(_module_rtl871x_xmit_c_, _drv_notice_, ("### xmitframe_hwencrypt\n"));
	}

	return _SUCCESS;
}

s32 rtw_make_wlanhdr (struct rtw_adapter *padapter , u8 *hdr, struct pkt_attrib *pattrib)
{
	u16 *qc;
	struct rtw_ieee80211_hdr *pwlanhdr = (struct rtw_ieee80211_hdr *)hdr;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct qos_priv *pqospriv = &pmlmepriv->qospriv;
	u8 qos_option = false;
	int res = _SUCCESS;
	__le16 *fctrl = &pwlanhdr->frame_ctl;
	struct sta_info *psta;
	int bmcst = IS_MCAST(pattrib->ra);

	if (pattrib->psta) {
		psta = pattrib->psta;
	} else {
		if (bmcst)
			psta = rtw_get_bcmc_stainfo(padapter);
		else
			psta = rtw_get_stainfo(&padapter->stapriv, pattrib->ra);
	}

	memset(hdr, 0, WLANHDR_OFFSET);

	SetFrameSubType(fctrl, pattrib->subtype);

	if (pattrib->subtype & WIFI_DATA_TYPE) {
		if ((check_fwstate(pmlmepriv,  WIFI_STATION_STATE) == true)) {
			/* to_ds = 1, fr_ds = 0; */
			/* Data transfer to AP */
			SetToDs(fctrl);
			memcpy(pwlanhdr->addr1, get_bssid(pmlmepriv), ETH_ALEN);
			memcpy(pwlanhdr->addr2, pattrib->src, ETH_ALEN);
			memcpy(pwlanhdr->addr3, pattrib->dst, ETH_ALEN);

			if (pqospriv->qos_option)
				qos_option = true;
		} else if ((check_fwstate(pmlmepriv,  WIFI_AP_STATE) == true)) {
			/* to_ds = 0, fr_ds = 1; */
			SetFrDs(fctrl);
			memcpy(pwlanhdr->addr1, pattrib->dst, ETH_ALEN);
			memcpy(pwlanhdr->addr2, get_bssid(pmlmepriv), ETH_ALEN);
			memcpy(pwlanhdr->addr3, pattrib->src, ETH_ALEN);

			if (psta->qos_option)
				qos_option = true;
		} else if ((check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == true) ||
		(check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == true)) {
			memcpy(pwlanhdr->addr1, pattrib->dst, ETH_ALEN);
			memcpy(pwlanhdr->addr2, pattrib->src, ETH_ALEN);
			memcpy(pwlanhdr->addr3, get_bssid(pmlmepriv), ETH_ALEN);

			if (psta->qos_option)
				qos_option = true;
		} else {
			RT_TRACE(_module_rtl871x_xmit_c_, _drv_err_, ("fw_state:%x is not allowed to xmit frame\n", get_fwstate(pmlmepriv)));
			res = _FAIL;
			goto exit;
		}

		if (pattrib->mdata)
			SetMData(fctrl);

		if (pattrib->encrypt)
			SetPrivacy(fctrl);

		if (qos_option) {
			qc = (unsigned short *)(hdr + pattrib->hdrlen - 2);

			if (pattrib->priority)
				SetPriority(qc, pattrib->priority);

			SetEOSP(qc, pattrib->eosp);

			SetAckpolicy(qc, pattrib->ack_policy);
		}

		/* TODO: fill HT Control Field */

		/* Update Seq Num will be handled by f/w */
		if (psta) {
			psta->sta_xmitpriv.txseq_tid[pattrib->priority]++;
			psta->sta_xmitpriv.txseq_tid[pattrib->priority] &= 0xFFF;

			pattrib->seqnum = psta->sta_xmitpriv.txseq_tid[pattrib->priority];

			SetSeqNum(hdr, pattrib->seqnum);

			/* check if enable ampdu */
			if (pattrib->ht_en && psta->htpriv.ampdu_enable) {
				if (psta->htpriv.agg_enable_bitmap & BIT(pattrib->priority))
					pattrib->ampdu_en = true;
			}

			/* re-check if enable ampdu by BA_starting_seqctrl */
			if (pattrib->ampdu_en == true) {
				u16 tx_seq;

				tx_seq = psta->BA_starting_seqctrl[pattrib->priority & 0x0f];

				/* check BA_starting_seqctrl */
				if (SN_LESS(pattrib->seqnum, tx_seq)) {
					/* DBG_8192D("tx ampdu seqnum(%d) < tx_seq(%d)\n", pattrib->seqnum, tx_seq); */
					pattrib->ampdu_en = false;/* AGG BK */
				} else if (SN_EQUAL(pattrib->seqnum, tx_seq)) {
					psta->BA_starting_seqctrl[pattrib->priority & 0x0f] = (tx_seq+1)&0xfff;

					pattrib->ampdu_en = true;/* AGG EN */
				} else {
					/* DBG_8192D("tx ampdu over run\n"); */
					psta->BA_starting_seqctrl[pattrib->priority & 0x0f] = (pattrib->seqnum+1)&0xfff;
					pattrib->ampdu_en = true;/* AGG EN */
				}
			}
		}
	}

exit:

	return res;
}

s32 rtw_txframes_pending(struct rtw_adapter *padapter)
{
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;

	return ((_rtw_queue_empty(&pxmitpriv->be_pending) == false) ||
			 (_rtw_queue_empty(&pxmitpriv->bk_pending) == false) ||
			 (_rtw_queue_empty(&pxmitpriv->vi_pending) == false) ||
			 (_rtw_queue_empty(&pxmitpriv->vo_pending) == false));
}

s32 rtw_txframes_sta_ac_pending(struct rtw_adapter *padapter, struct pkt_attrib *pattrib)
{
	struct sta_info *psta;
	struct tx_servq *ptxservq;
	int priority = pattrib->priority;

	psta = pattrib->psta;

	switch (priority) {
	case 1:
	case 2:
		ptxservq = &(psta->sta_xmitpriv.bk_q);
		break;
	case 4:
	case 5:
		ptxservq = &(psta->sta_xmitpriv.vi_q);
		break;
	case 6:
	case 7:
		ptxservq = &(psta->sta_xmitpriv.vo_q);
		break;
	case 0:
	case 3:
	default:
		ptxservq = &(psta->sta_xmitpriv.be_q);
	break;
	}

	if (!ptxservq) {
		pr_err("ptxservq is NULL for priority %d\n", priority);
		return 0;
	}
	return ptxservq->qcnt;
}

/*
 * Calculate wlan 802.11 packet MAX size from pkt_attrib
 * This function doesn't consider fragment case
 */
u32 rtw_calculate_wlan_pkt_size_by_attribue(struct pkt_attrib *pattrib)
{
	u32	len = 0;

	len = pattrib->hdrlen + pattrib->iv_len; /*  WLAN Header and IV */
	len += SNAP_SIZE + sizeof(u16); /*  LLC */
	len += pattrib->pktlen;
	if (pattrib->encrypt == _TKIP_)
		len += 8; /*  MIC */
	len += pattrib->icv_len; /*  ICV */

	return len;
}

/*

This sub-routine will perform all the following:

1. remove 802.3 header.
2. create wlan_header, based on the info in pxmitframe
3. append sta's iv/ext-iv
4. append LLC
5. move frag chunk from pframe to pxmitframe->mem
6. apply sw-encrypt, if necessary.

*/
s32 rtw_xmitframe_coalesce(struct rtw_adapter *padapter, struct sk_buff *skb,
			   struct xmit_frame *pxmitframe)
{
	struct sta_info *psta;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct pkt_attrib *pattrib = &pxmitframe->attrib;
	struct ieee80211_hdr *hdr;
	s32 frg_inx, frg_len, mpdu_len, llc_sz, mem_sz;
	u8 *pframe, *mem_start;
	u8 hw_hdr_offset;
	u8 *pbuf_start;
	u8 *pdata = skb->data;
	int data_len = skb->len;
	s32 bmcst = is_multicast_ether_addr(pattrib->ra);
	s32 res = _SUCCESS;

	if (pattrib->psta)
		psta = pattrib->psta;
	else
		psta = rtw_get_stainfo(&padapter->stapriv, pattrib->ra);

	if (!psta)
		return _FAIL;

	if (!pxmitframe->buf_addr)
		return _FAIL;

	pbuf_start = pxmitframe->buf_addr;

	mem_start = pbuf_start + TXDESC_SIZE + (pxmitframe->pkt_offset * PACKET_OFFSET_SZ);

	if (rtw_make_wlanhdr(padapter, mem_start, pattrib) == _FAIL) {
		RT_TRACE(_module_rtl871x_xmit_c_, _drv_err_, ("rtw_xmitframe_coalesce: rtw_make_wlanhdr fail; drop pkt\n"));
		res = _FAIL;
		goto exit;
	}

	pdata += pattrib->pkt_hdrlen;
	data_len -= pattrib->pkt_hdrlen;

	frg_inx = 0;
	frg_len = pxmitpriv->frag_len - 4;/* 2346-4 = 2342 */

	while (1) {
		llc_sz = 0;

		mpdu_len = frg_len;

		pframe = mem_start;

		hdr = (struct ieee80211_hdr *)mem_start;

		pframe += pattrib->hdrlen;
		mpdu_len -= pattrib->hdrlen;

		/* adding icv, if necessary... */
		if (pattrib->iv_len) {
			if (psta) {
				switch (pattrib->encrypt) {
				case _WEP40_:
				case _WEP104_:
					WEP_IV(pattrib->iv, psta->dot11txpn, pattrib->key_idx);
					break;
				case _TKIP_:
					if (bmcst)
						TKIP_IV(pattrib->iv, psta->dot11txpn, pattrib->key_idx);
					else
						TKIP_IV(pattrib->iv, psta->dot11txpn, 0);
					break;
				case _AES_:
					if (bmcst)
						AES_IV(pattrib->iv, psta->dot11txpn, pattrib->key_idx);
					else
						AES_IV(pattrib->iv, psta->dot11txpn, 0);
					break;
				}
			}

			memcpy(pframe, pattrib->iv, pattrib->iv_len);

			RT_TRACE(_module_rtl871x_xmit_c_, _drv_notice_,
				 ("rtw_xmitframe_coalesce: keyid =%d pattrib->iv[3]=%.2x pframe =%.2x %.2x %.2x %.2x\n",
				  padapter->securitypriv.dot11PrivacyKeyIndex, pattrib->iv[3], *pframe, *(pframe+1), *(pframe+2), *(pframe+3)));

			pframe += pattrib->iv_len;

			mpdu_len -= pattrib->iv_len;
		}

		if (frg_inx == 0) {
			llc_sz = rtw_put_snap(pframe, pattrib->ether_type);
			pframe += llc_sz;
			mpdu_len -= llc_sz;
		}

		if ((pattrib->icv_len > 0) && (pattrib->bswenc))
			mpdu_len -= pattrib->icv_len;

		if (bmcst) {
			/*  don't do fragment to broadcat/multicast packets */
			mem_sz = min_t(s32, data_len, pattrib->pktlen);
		} else {
			mem_sz = min_t(s32, data_len, mpdu_len);
		}

		memcpy(pframe, pdata, mem_sz);

		pframe += mem_sz;
		pdata += mem_sz;
		data_len -= mem_sz;

		if ((pattrib->icv_len > 0) && (pattrib->bswenc)) {
			memcpy(pframe, pattrib->icv, pattrib->icv_len);
			pframe += pattrib->icv_len;
		}

		frg_inx++;

		if (bmcst || data_len <= 0) {
			pattrib->nr_frags = frg_inx;

			pattrib->last_txcmdsz = pattrib->hdrlen + pattrib->iv_len + ((pattrib->nr_frags == 1) ? llc_sz : 0) +
					((pattrib->bswenc) ? pattrib->icv_len : 0) + mem_sz;

			hdr->frame_control &=
				~cpu_to_le16(IEEE80211_FCTL_MOREFRAGS);

			break;
		} else {
			RT_TRACE(_module_rtl871x_xmit_c_, _drv_err_, ("%s: There're still something in packet!\n", __func__));
		}
		hdr->frame_control |= cpu_to_le16(IEEE80211_FCTL_MOREFRAGS);

		mem_start = PTR_ALIGN(pframe, 4) + hw_hdr_offset;
		memcpy(mem_start, pbuf_start + hw_hdr_offset, pattrib->hdrlen);
	}

	if (xmitframe_addmic(padapter, pxmitframe) == _FAIL) {
		RT_TRACE(_module_rtl871x_xmit_c_, _drv_err_, ("xmitframe_addmic(padapter, pxmitframe) == _FAIL\n"));
		res = _FAIL;
		goto exit;
	}

	xmitframe_swencrypt(padapter, pxmitframe);

	if (bmcst == false)
		update_attrib_vcs_info(padapter, pxmitframe);
	else
		pattrib->vcs_mode = NONE_VCS;
exit:
	return res;
}

/* Logical Link Control(LLC) SubNetwork Attachment Point(SNAP) header
 * IEEE LLC/SNAP header contains 8 octets
 * First 3 octets comprise the LLC portion
 * SNAP portion, 5 octets, is divided into two fields:
 *	Organizationally Unique Identifier(OUI), 3 octets,
 *	type, defined by that organization, 2 octets.
 */
s32 rtw_put_snap(u8 *data, u16 h_proto)
{
	struct ieee80211_snap_hdr *snap;
	u8 *oui;

	snap = (struct ieee80211_snap_hdr *)data;
	snap->dsap = 0xaa;
	snap->ssap = 0xaa;
	snap->ctrl = 0x03;

	if (h_proto == 0x8137 || h_proto == 0x80f3)
		oui = P802_1H_OUI;
	else
		oui = RFC1042_OUI;

	snap->oui[0] = oui[0];
	snap->oui[1] = oui[1];
	snap->oui[2] = oui[2];

	*(__be16 *)(data + SNAP_SIZE) = htons(h_proto);

	return SNAP_SIZE + sizeof(u16);
}

void rtw_update_protection(struct rtw_adapter *padapter, u8 *ie, uint ie_len)
{
	uint	protection;
	u8	*perp;
	int	 erp_len;
	struct	xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct	registry_priv *pregistrypriv = &padapter->registrypriv;

	switch (pxmitpriv->vcs_setting) {
	case DISABLE_VCS:
		pxmitpriv->vcs = NONE_VCS;
		break;
	case ENABLE_VCS:
		break;
	case AUTO_VCS:
	default:
		perp = rtw_get_ie(ie, _ERPINFO_IE_, &erp_len, ie_len);
		if (perp == NULL) {
			pxmitpriv->vcs = NONE_VCS;
		} else {
			protection = (*(perp + 2)) & BIT(1);
			if (protection) {
				if (pregistrypriv->vcs_type == RTS_CTS)
					pxmitpriv->vcs = RTS_CTS;
				else
					pxmitpriv->vcs = CTS_TO_SELF;
			} else {
				pxmitpriv->vcs = NONE_VCS;
			}
		}
		break;
	}

}

void rtw_count_tx_stats(struct rtw_adapter *padapter, struct xmit_frame *pxmitframe, int sz)
{
	struct sta_info *psta = NULL;
	struct stainfo_stats *pstats = NULL;
	struct xmit_priv	*pxmitpriv = &padapter->xmitpriv;
	struct mlme_priv	*pmlmepriv = &(padapter->mlmepriv);

	if ((pxmitframe->frame_tag&0x0f) == DATA_FRAMETAG) {
		pxmitpriv->tx_bytes += sz;
		pmlmepriv->LinkDetectInfo.NumTxOkInPeriod += pxmitframe->agg_num;

		psta = pxmitframe->attrib.psta;

		if (psta) {
			pstats = &psta->sta_stats;
			pstats->tx_pkts += pxmitframe->agg_num;
			pstats->tx_bytes += sz;
		}
	}
}

struct xmit_buf *rtw_alloc_xmitbuf_ext(struct xmit_priv *pxmitpriv)
{
	long unsigned int flags;
	struct xmit_buf *pxmitbuf =  NULL;
	struct list_head *plist, *phead;
	struct __queue *pfree_queue = &pxmitpriv->free_xmit_extbuf_queue;

	spin_lock_irqsave(&pfree_queue->lock, flags);

	if (_rtw_queue_empty(pfree_queue) == true) {
		pxmitbuf = NULL;
	} else {
		phead = get_list_head(pfree_queue);

		plist = phead->next;

		pxmitbuf = container_of(plist, struct xmit_buf, list);

		list_del_init(&(pxmitbuf->list));
	}
	if (pxmitbuf !=  NULL) {
		pxmitpriv->free_xmit_extbuf_cnt--;
		#ifdef DBG_XMIT_BUF
		DBG_8192D("DBG_XMIT_BUF ALLOC no =%d,  free_xmit_extbuf_cnt =%d\n", pxmitbuf->no, pxmitpriv->free_xmit_extbuf_cnt);
		#endif
		pxmitbuf->priv_data = NULL;
		if (pxmitbuf->sctx) {
			DBG_8192D("%s pxmitbuf->sctx is not NULL\n", __func__);
			rtw_sctx_done_err(&pxmitbuf->sctx, RTW_SCTX_DONE_BUF_ALLOC);
		}
	}
	spin_unlock_irqrestore(&pfree_queue->lock, flags);

	return pxmitbuf;
}

s32 rtw_free_xmitbuf_ext(struct xmit_priv *pxmitpriv, struct xmit_buf *pxmitbuf)
{
	long unsigned int flags;
	struct __queue *pfree_queue = &pxmitpriv->free_xmit_extbuf_queue;

	if (pxmitbuf == NULL)
		return _FAIL;

	spin_lock_irqsave(&pfree_queue->lock, flags);

	list_del_init(&pxmitbuf->list);

	rtw_list_insert_tail(&(pxmitbuf->list), get_list_head(pfree_queue));
	pxmitpriv->free_xmit_extbuf_cnt++;
	#ifdef DBG_XMIT_BUF
	DBG_8192D("DBG_XMIT_BUF FREE no =%d, free_xmit_extbuf_cnt =%d\n", pxmitbuf->no , pxmitpriv->free_xmit_extbuf_cnt);
	#endif

	spin_unlock_irqrestore(&pfree_queue->lock, flags);

	return _SUCCESS;
}

struct xmit_buf *rtw_alloc_xmitbuf(struct xmit_priv *pxmitpriv)
{
	long unsigned int flags;
	struct xmit_buf *pxmitbuf =  NULL;
	struct list_head *plist, *phead;
	struct __queue *pfree_xmitbuf_queue = &pxmitpriv->free_xmitbuf_queue;

	/* DBG_8192D("+rtw_alloc_xmitbuf\n"); */

	spin_lock_irqsave(&pfree_xmitbuf_queue->lock, flags);

	if (_rtw_queue_empty(pfree_xmitbuf_queue) == true) {
		pxmitbuf = NULL;
	} else {
		phead = get_list_head(pfree_xmitbuf_queue);

		plist = phead->next;

		pxmitbuf = container_of(plist, struct xmit_buf, list);

		list_del_init(&(pxmitbuf->list));
	}

	if (pxmitbuf !=  NULL) {
		pxmitpriv->free_xmitbuf_cnt--;
		#ifdef DBG_XMIT_BUF
		DBG_8192D("DBG_XMIT_BUF ALLOC no =%d,  free_xmitbuf_cnt =%d\n", pxmitbuf->no, pxmitpriv->free_xmitbuf_cnt);
		#endif

		pxmitbuf->priv_data = NULL;

		if (pxmitbuf->sctx) {
			DBG_8192D("%s pxmitbuf->sctx is not NULL\n", __func__);
			rtw_sctx_done_err(&pxmitbuf->sctx, RTW_SCTX_DONE_BUF_ALLOC);
		}
	}
	#ifdef DBG_XMIT_BUF
	else {
		DBG_8192D("DBG_XMIT_BUF rtw_alloc_xmitbuf return NULL\n");
	}
	#endif

	spin_unlock_irqrestore(&pfree_xmitbuf_queue->lock, flags);

	return pxmitbuf;
}

s32 rtw_free_xmitbuf(struct xmit_priv *pxmitpriv, struct xmit_buf *pxmitbuf)
{
	long unsigned int flags;
	struct __queue *pfree_xmitbuf_queue = &pxmitpriv->free_xmitbuf_queue;

	if (pxmitbuf == NULL)
		return _FAIL;

	if (pxmitbuf->sctx) {
		DBG_8192D("%s pxmitbuf->sctx is not NULL\n", __func__);
		rtw_sctx_done_err(&pxmitbuf->sctx, RTW_SCTX_DONE_BUF_FREE);
	}

	if (pxmitbuf->ext_tag) {
		rtw_free_xmitbuf_ext(pxmitpriv, pxmitbuf);
	} else {
		spin_lock_irqsave(&pfree_xmitbuf_queue->lock, flags);

		list_del_init(&pxmitbuf->list);

		rtw_list_insert_tail(&(pxmitbuf->list), get_list_head(pfree_xmitbuf_queue));

		pxmitpriv->free_xmitbuf_cnt++;
		/* DBG_8192D("FREE, free_xmitbuf_cnt =%d\n", pxmitpriv->free_xmitbuf_cnt); */
		#ifdef DBG_XMIT_BUF
		DBG_8192D("DBG_XMIT_BUF FREE no =%d, free_xmitbuf_cnt =%d\n", pxmitbuf->no , pxmitpriv->free_xmitbuf_cnt);
		#endif
		spin_unlock_irqrestore(&pfree_xmitbuf_queue->lock, flags);
	}

	return _SUCCESS;
}

/*
Calling context:
1. OS_TXENTRY
2. RXENTRY (rx_thread or RX_ISR/RX_CallBack)

If we turn on USE_RXTHREAD, then, no need for critical section.
Otherwise, we must use _enter/_exit critical to protect free_xmit_queue...

Must be very very cautious...

*/

struct xmit_frame *rtw_alloc_xmitframe(struct xmit_priv *pxmitpriv)
{
	/*
		Please remember to use all the osdep_service api,
		and lock/unlock or _enter/_exit critical to protect
		pfree_xmit_queue
	*/

	struct xmit_frame *pxframe = NULL;
	struct list_head *plist, *phead;
	struct __queue *pfree_xmit_queue = &pxmitpriv->free_xmit_queue;
	struct rtw_adapter *padapter = pxmitpriv->adapter;

	spin_lock_bh(&pfree_xmit_queue->lock);

	if (_rtw_queue_empty(pfree_xmit_queue) == true) {
		RT_TRACE(_module_rtl871x_xmit_c_, _drv_info_, ("rtw_alloc_xmitframe:%d\n", pxmitpriv->free_xmitframe_cnt));
		pxframe =  NULL;
	} else {
		phead = get_list_head(pfree_xmit_queue);

		plist = phead->next;

		pxframe = container_of(plist, struct xmit_frame, list);

		list_del_init(&(pxframe->list));
	}

	if (pxframe !=  NULL) {
		pxmitpriv->free_xmitframe_cnt--;

		RT_TRACE(_module_rtl871x_xmit_c_, _drv_info_, ("rtw_alloc_xmitframe():free_xmitframe_cnt =%d\n", pxmitpriv->free_xmitframe_cnt));

		pxframe->buf_addr = NULL;
		pxframe->pxmitbuf = NULL;

		memset(&pxframe->attrib, 0, sizeof(struct pkt_attrib));
		/* pxframe->attrib.psta = NULL; */

		pxframe->frame_tag = DATA_FRAMETAG;

		pxframe->pkt = NULL;
		pxframe->pkt_offset = 1;/* default use pkt_offset to fill tx desc */

		pxframe->agg_num = 1;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 35))
		if (pxmitpriv->free_xmitframe_cnt == 1) {
			if (!rtw_netif_queue_stopped(padapter->pnetdev))
				rtw_netif_stop_queue(padapter->pnetdev);
		}
#endif
		pxframe->ack_report = 0;
	}
	spin_unlock_bh(&pfree_xmit_queue->lock);

	return pxframe;
}

s32 rtw_free_xmitframe(struct xmit_priv *pxmitpriv, struct xmit_frame *pxmitframe)
{
	struct __queue *pfree_xmit_queue = &pxmitpriv->free_xmit_queue;
	struct rtw_adapter *padapter = pxmitpriv->adapter;
	struct sk_buff *pndis_pkt = NULL;

	if (pxmitframe == NULL) {
		RT_TRACE(_module_rtl871x_xmit_c_, _drv_err_, ("====== rtw_free_xmitframe():pxmitframe == NULL!!!!!!!!!!\n"));
		goto exit;
	}

	spin_lock_bh(&pfree_xmit_queue->lock);

	list_del_init(&pxmitframe->list);

	if (pxmitframe->pkt) {
		pndis_pkt = pxmitframe->pkt;
		pxmitframe->pkt = NULL;
	}

	rtw_list_insert_tail(&pxmitframe->list, get_list_head(pfree_xmit_queue));

	pxmitpriv->free_xmitframe_cnt++;
	RT_TRACE(_module_rtl871x_xmit_c_, _drv_debug_, ("rtw_free_xmitframe():free_xmitframe_cnt =%d\n", pxmitpriv->free_xmitframe_cnt));

	spin_unlock_bh(&pfree_xmit_queue->lock);

	if (pndis_pkt)
		rtw_os_pkt_complete(padapter, pndis_pkt);

exit:

	return _SUCCESS;
}

void rtw_free_xmitframe_queue(struct xmit_priv *pxmitpriv, struct __queue *pframequeue)
{
	struct list_head *plist, *phead;
	struct	xmit_frame	*pxmitframe;

	spin_lock_bh(&(pframequeue->lock));

	phead = get_list_head(pframequeue);
	plist = phead->next;

	while (rtw_end_of_queue_search(phead, plist) == false) {
		pxmitframe = container_of(plist, struct xmit_frame, list);
		plist = plist->next;
		rtw_free_xmitframe(pxmitpriv, pxmitframe);
	}
	spin_unlock_bh(&(pframequeue->lock));

}

s32 rtw_xmitframe_enqueue(struct rtw_adapter *padapter, struct xmit_frame *pxmitframe)
{
	if (rtw_xmit_classifier(padapter, pxmitframe) == _FAIL) {
		RT_TRACE(_module_rtl871x_xmit_c_, _drv_err_,
			 ("rtw_xmitframe_enqueue: drop xmit pkt for classifier fail\n"));
		return _FAIL;
	}

	return _SUCCESS;
}

static struct xmit_frame *dequeue_one_xmitframe(struct xmit_priv *pxmitpriv,
						struct hw_xmit *phwxmit,
						struct tx_servq *ptxservq,
						struct __queue *pframe_queue)
{
	struct list_head *xmitframe_plist, *xmitframe_phead;
	struct	xmit_frame	*pxmitframe = NULL;

	xmitframe_phead = get_list_head(pframe_queue);
	xmitframe_plist = xmitframe_phead->next;

	if ((rtw_end_of_queue_search(xmitframe_phead, xmitframe_plist)) == false) {
		pxmitframe = container_of(xmitframe_plist, struct xmit_frame, list);

		xmitframe_plist = xmitframe_plist->next;

		list_del_init(&pxmitframe->list);

		ptxservq->qcnt--;
	}
	return pxmitframe;
}

struct xmit_frame *rtw_dequeue_xframe(struct xmit_priv *pxmitpriv, struct hw_xmit *phwxmit_i, int entry)
{
	struct list_head *sta_plist, *sta_phead;
	struct hw_xmit *phwxmit;
	struct tx_servq *ptxservq = NULL;
	struct __queue *pframe_queue = NULL;
	struct xmit_frame *pxmitframe = NULL;
	struct rtw_adapter *padapter = pxmitpriv->adapter;
	struct registry_priv	*pregpriv = &padapter->registrypriv;
	int i, inx[4];

	inx[0] = 0; inx[1] = 1; inx[2] = 2; inx[3] = 3;

	if (pregpriv->wifi_spec == 1) {
		int j, tmp, acirp_cnt[4];
		/* entry indx: 0->vo, 1->vi, 2->be, 3->bk. */
		acirp_cnt[0] = pxmitpriv->voq_cnt;
		acirp_cnt[1] = pxmitpriv->viq_cnt;
		acirp_cnt[2] = pxmitpriv->beq_cnt;
		acirp_cnt[3] = pxmitpriv->bkq_cnt;

		for (i = 0; i < 4; i++) {
			for (j = i+1; j < 4; j++) {
				if (acirp_cnt[j] < acirp_cnt[i]) {
					tmp = acirp_cnt[i];
					acirp_cnt[i] = acirp_cnt[j];
					acirp_cnt[j] = tmp;

					tmp = inx[i];
					inx[i] = inx[j];
					inx[j] = tmp;
				}
			}
		}
	}

	spin_lock_bh(&pxmitpriv->lock);

	for (i = 0; i < entry; i++) {
		phwxmit = phwxmit_i + inx[i];

		sta_phead = get_list_head(phwxmit->sta_queue);
		sta_plist = sta_phead->next;

		while ((rtw_end_of_queue_search(sta_phead, sta_plist)) == false) {
			ptxservq = container_of(sta_plist, struct tx_servq, tx_pending);

			pframe_queue = &ptxservq->sta_pending;

			pxmitframe = dequeue_one_xmitframe(pxmitpriv, phwxmit, ptxservq, pframe_queue);

			if (pxmitframe) {
				phwxmit->accnt--;

				/* Remove sta node when there is no pending packets. */
				if (_rtw_queue_empty(pframe_queue)) /* must be done after and before break */
					list_del_init((&ptxservq->tx_pending)->next);
				goto exit;
			}
			sta_plist = sta_plist->next;
		}
	}

exit:

	spin_unlock_bh(&pxmitpriv->lock);

	return pxmitframe;
}

struct tx_servq *rtw_get_sta_pending(struct rtw_adapter *padapter, struct sta_info *psta, int up, u8 *ac)
{
	struct tx_servq *ptxservq;

	switch (up) {
	case 1:
	case 2:
		ptxservq = &(psta->sta_xmitpriv.bk_q);
		*(ac) = 3;
		RT_TRACE(_module_rtl871x_xmit_c_, _drv_info_, ("rtw_get_sta_pending : BK\n"));
		break;
	case 4:
	case 5:
		ptxservq = &(psta->sta_xmitpriv.vi_q);
		*(ac) = 1;
		RT_TRACE(_module_rtl871x_xmit_c_, _drv_info_, ("rtw_get_sta_pending : VI\n"));
		break;
	case 6:
	case 7:
		ptxservq = &(psta->sta_xmitpriv.vo_q);
		*(ac) = 0;
		RT_TRACE(_module_rtl871x_xmit_c_, _drv_info_, ("rtw_get_sta_pending : VO\n"));
		break;
	case 0:
	case 3:
	default:
		ptxservq = &(psta->sta_xmitpriv.be_q);
		*(ac) = 2;
		RT_TRACE(_module_rtl871x_xmit_c_, _drv_info_, ("rtw_get_sta_pending : BE\n"));
	break;
	}

	return ptxservq;
}

/*
 * Will enqueue pxmitframe to the proper queue,
 * and indicate it to xx_pending list.....
 */
s32 rtw_xmit_classifier(struct rtw_adapter *padapter, struct xmit_frame *pxmitframe)
{
	u8	ac_index;
	struct sta_info	*psta;
	struct tx_servq	*ptxservq;
	struct pkt_attrib	*pattrib = &pxmitframe->attrib;
	struct sta_priv	*pstapriv = &padapter->stapriv;
	struct hw_xmit	*phwxmits =  padapter->xmitpriv.hwxmits;
	int res = _SUCCESS;

	if (pattrib->psta)
		psta = pattrib->psta;
	else
		psta = rtw_get_stainfo(pstapriv, pattrib->ra);

	if (psta == NULL) {
		res = _FAIL;
		RT_TRACE(_module_rtl871x_xmit_c_, _drv_err_, ("rtw_xmit_classifier: psta == NULL\n"));
		goto exit;
	}

	ptxservq = rtw_get_sta_pending(padapter, psta, pattrib->priority, (u8 *)(&ac_index));

	if (rtw_is_list_empty(&ptxservq->tx_pending)) {
		rtw_list_insert_tail(&ptxservq->tx_pending, get_list_head(phwxmits[ac_index].sta_queue));
	}

	rtw_list_insert_tail(&pxmitframe->list, get_list_head(&ptxservq->sta_pending));
	ptxservq->qcnt++;
	phwxmits[ac_index].accnt++;

exit:

	return res;
}

void rtw_alloc_hwxmits(struct rtw_adapter *padapter)
{
	struct hw_xmit *hwxmits;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;

	pxmitpriv->hwxmit_entry = HWXMIT_ENTRY;

	pxmitpriv->hwxmits = (struct hw_xmit *)kzalloc(sizeof(struct hw_xmit) * pxmitpriv->hwxmit_entry, GFP_KERNEL);

	hwxmits = pxmitpriv->hwxmits;

	if (pxmitpriv->hwxmit_entry == 5) {
		hwxmits[0] .sta_queue = &pxmitpriv->bm_pending;
		hwxmits[1] .sta_queue = &pxmitpriv->vo_pending;
		hwxmits[2] .sta_queue = &pxmitpriv->vi_pending;
		hwxmits[3] .sta_queue = &pxmitpriv->bk_pending;
		hwxmits[4] .sta_queue = &pxmitpriv->be_pending;
	} else if (pxmitpriv->hwxmit_entry == 4) {
		hwxmits[0] .sta_queue = &pxmitpriv->vo_pending;
		hwxmits[1] .sta_queue = &pxmitpriv->vi_pending;
		hwxmits[2] .sta_queue = &pxmitpriv->be_pending;
		hwxmits[3] .sta_queue = &pxmitpriv->bk_pending;
	}
}

void rtw_free_hwxmits(struct rtw_adapter *padapter)
{
	struct hw_xmit *hwxmits;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;

	hwxmits = pxmitpriv->hwxmits;
	kfree(hwxmits);
}

void rtw_init_hwxmits(struct hw_xmit *phwxmit, int entry)
{
	int i;

	for (i = 0; i < entry; i++, phwxmit++)
		phwxmit->accnt = 0;

}

static void do_queue_select(struct rtw_adapter	*padapter, struct pkt_attrib *pattrib)
{
	u8 qsel;

	qsel = pattrib->priority;
	RT_TRACE(_module_rtl871x_xmit_c_, _drv_info_, ("### do_queue_select priority =%d , qsel = %d\n", pattrib->priority , qsel));

	pattrib->qsel = qsel;
}

/*
 * The main transmit(tx) entry
 *
 * Return
 *	1	enqueue
 *	0	success, hardware will handle this xmit frame(packet)
 *	<0	fail
 */
s32 rtw_xmit(struct rtw_adapter *padapter, struct sk_buff **ppkt)
{
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct xmit_frame *pxmitframe = NULL;
	s32 res;

	pxmitframe = rtw_alloc_xmitframe(pxmitpriv);
	if (pxmitframe == NULL) {
		RT_TRACE(_module_xmit_osdep_c_, _drv_err_, ("rtw_xmit: no more pxmitframe\n"));
		#ifdef DBG_TX_DROP_FRAME
		DBG_8192D("DBG_TX_DROP_FRAME %s no more pxmitframe\n", __func__);
		#endif
		return -1;
	}

	res = update_attrib(padapter, *ppkt, &pxmitframe->attrib);
	if (res == _FAIL) {
		RT_TRACE(_module_xmit_osdep_c_, _drv_err_, ("rtw_xmit: update attrib fail\n"));
		#ifdef DBG_TX_DROP_FRAME
		DBG_8192D("DBG_TX_DROP_FRAME %s update attrib fail\n", __func__);
		#endif
		rtw_free_xmitframe(pxmitpriv, pxmitframe);
		return -1;
	}
	pxmitframe->pkt = *ppkt;

	rtw_led_control(padapter, LED_CTL_TX);

	do_queue_select(padapter, &pxmitframe->attrib);

#if defined(CONFIG_92D_AP_MODE)
	spin_lock_bh(&pxmitpriv->lock);
	if (xmitframe_enqueue_for_sleeping_sta(padapter, pxmitframe) == true) {
		spin_unlock_bh(&pxmitpriv->lock);
		return 1;
	}
	spin_unlock_bh(&pxmitpriv->lock);
#endif

	if (rtw_hal_xmit(padapter, pxmitframe) == false)
		return 1;
	return 0;
}

#if defined(CONFIG_92D_AP_MODE)

int xmitframe_enqueue_for_sleeping_sta(struct rtw_adapter *padapter, struct xmit_frame *pxmitframe)
{
	int ret = false;
	struct sta_info *psta = NULL;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct pkt_attrib *pattrib = &pxmitframe->attrib;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	int bmcst = IS_MCAST(pattrib->ra);

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE) == false)
	    return ret;

	if (pattrib->psta)
		psta = pattrib->psta;
	else
		psta = rtw_get_stainfo(pstapriv, pattrib->ra);

	if (psta == NULL)
		return ret;

	if (pattrib->triggered == 1) {
		if (bmcst)
			pattrib->qsel = 0x11;/* HIQ */
		return ret;
	}
	if (bmcst) {
		spin_lock_bh(&psta->sleep_q.lock);

		if (pstapriv->sta_dz_bitmap) {/* if anyone sta is in ps mode */
			list_del_init(&pxmitframe->list);

			rtw_list_insert_tail(&pxmitframe->list, get_list_head(&psta->sleep_q));

			psta->sleepq_len++;

			pstapriv->tim_bitmap |= BIT(0);/*  */
			pstapriv->sta_dz_bitmap |= BIT(0);

			update_beacon(padapter, _TIM_IE_, NULL, false);/* tx bc/mc packets after upate bcn */

			ret = true;
		}
		spin_unlock_bh(&psta->sleep_q.lock);
		return ret;
	}
	spin_lock_bh(&psta->sleep_q.lock);
	if (psta->state&WIFI_SLEEP_STATE) {
		u8 wmmps_ac = 0;

		if (pstapriv->sta_dz_bitmap&BIT(psta->aid)) {
			list_del_init(&pxmitframe->list);

			rtw_list_insert_tail(&pxmitframe->list, get_list_head(&psta->sleep_q));

			psta->sleepq_len++;

			switch (pattrib->priority) {
			case 1:
			case 2:
				wmmps_ac = psta->uapsd_bk&BIT(0);
				break;
			case 4:
			case 5:
				wmmps_ac = psta->uapsd_vi&BIT(0);
				break;
			case 6:
			case 7:
				wmmps_ac = psta->uapsd_vo&BIT(0);
				break;
			case 0:
			case 3:
			default:
				wmmps_ac = psta->uapsd_be&BIT(0);
				break;
			}

			if (wmmps_ac)
				psta->sleepq_ac_len++;

			if (((psta->has_legacy_ac) && (!wmmps_ac)) || ((!psta->has_legacy_ac) && (wmmps_ac))) {
				pstapriv->tim_bitmap |= BIT(psta->aid);

				if (psta->sleepq_len == 1)
					update_beacon(padapter, _TIM_IE_, NULL, false);
			}

			ret = true;
		}
	}
	spin_unlock_bh(&psta->sleep_q.lock);
	return ret;
}

static void dequeue_xmitframes_to_sleeping_queue(struct rtw_adapter *padapter, struct sta_info *psta, struct __queue *pframequeue)
{
	struct list_head *plist, *phead;
	u8	ac_index;
	struct tx_servq	*ptxservq;
	struct pkt_attrib	*pattrib;
	struct xmit_frame	*pxmitframe;
	struct hw_xmit *phwxmits =  padapter->xmitpriv.hwxmits;

	phead = get_list_head(pframequeue);
	plist = phead->next;

	while (rtw_end_of_queue_search(phead, plist) == false) {
		pxmitframe = container_of(plist, struct xmit_frame, list);

		plist = plist->next;

		xmitframe_enqueue_for_sleeping_sta(padapter, pxmitframe);

		pattrib = &pxmitframe->attrib;

		ptxservq = rtw_get_sta_pending(padapter, psta, pattrib->priority, (u8 *)(&ac_index));

		ptxservq->qcnt--;
		phwxmits[ac_index].accnt--;
	}
}

void stop_sta_xmit(struct rtw_adapter *padapter, struct sta_info *psta)
{
	struct sta_info *psta_bmc;
	struct sta_xmit_priv *pstaxmitpriv;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;

	pstaxmitpriv = &psta->sta_xmitpriv;

	/* for BC/MC Frames */
	psta_bmc = rtw_get_bcmc_stainfo(padapter);

	spin_lock_bh(&pxmitpriv->lock);

	psta->state |= WIFI_SLEEP_STATE;

	pstapriv->sta_dz_bitmap |= BIT(psta->aid);
	dequeue_xmitframes_to_sleeping_queue(padapter, psta, &pstaxmitpriv->vo_q.sta_pending);
	list_del_init(&(pstaxmitpriv->vo_q.tx_pending));
	dequeue_xmitframes_to_sleeping_queue(padapter, psta, &pstaxmitpriv->vi_q.sta_pending);
	list_del_init(&(pstaxmitpriv->vi_q.tx_pending));
	dequeue_xmitframes_to_sleeping_queue(padapter, psta, &pstaxmitpriv->be_q.sta_pending);
	list_del_init(&(pstaxmitpriv->be_q.tx_pending));
	dequeue_xmitframes_to_sleeping_queue(padapter, psta, &pstaxmitpriv->bk_q.sta_pending);
	list_del_init(&(pstaxmitpriv->bk_q.tx_pending));

	/* for BC/MC Frames */
	pstaxmitpriv = &psta_bmc->sta_xmitpriv;
	dequeue_xmitframes_to_sleeping_queue(padapter, psta_bmc, &pstaxmitpriv->be_q.sta_pending);
	list_del_init(&(pstaxmitpriv->be_q.tx_pending));
	spin_unlock_bh(&pxmitpriv->lock);
}

void wakeup_sta_to_xmit(struct rtw_adapter *padapter, struct sta_info *psta)
{
	u8 update_mask = 0, wmmps_ac = 0;
	struct sta_info *psta_bmc;
	struct list_head *xmitframe_plist, *xmitframe_phead;
	struct xmit_frame *pxmitframe = NULL;
	struct sta_priv *pstapriv = &padapter->stapriv;

	spin_lock_bh(&psta->sleep_q.lock);

	xmitframe_phead = get_list_head(&psta->sleep_q);
	xmitframe_plist = xmitframe_phead->next;

	while ((rtw_end_of_queue_search(xmitframe_phead, xmitframe_plist)) == false) {
		pxmitframe = container_of(xmitframe_plist, struct xmit_frame, list);

		xmitframe_plist = xmitframe_plist->next;

		list_del_init(&pxmitframe->list);

		switch (pxmitframe->attrib.priority) {
		case 1:
		case 2:
			wmmps_ac = psta->uapsd_bk&BIT(1);
			break;
		case 4:
		case 5:
			wmmps_ac = psta->uapsd_vi&BIT(1);
			break;
		case 6:
		case 7:
			wmmps_ac = psta->uapsd_vo&BIT(1);
			break;
		case 0:
		case 3:
		default:
			wmmps_ac = psta->uapsd_be&BIT(1);
			break;
		}

		psta->sleepq_len--;
		if (psta->sleepq_len > 0)
			pxmitframe->attrib.mdata = 1;
		else
			pxmitframe->attrib.mdata = 0;

		if (wmmps_ac) {
			psta->sleepq_ac_len--;
			if (psta->sleepq_ac_len > 0) {
				pxmitframe->attrib.mdata = 1;
				pxmitframe->attrib.eosp = 0;
			} else {
				pxmitframe->attrib.mdata = 0;
				pxmitframe->attrib.eosp = 1;
			}
		}

		pxmitframe->attrib.triggered = 1;

		spin_unlock_bh(&psta->sleep_q.lock);
		if (rtw_hal_xmit(padapter, pxmitframe) == true)
			rtw_os_xmit_complete(padapter, pxmitframe);
		spin_lock_bh(&psta->sleep_q.lock);
	}
	if (psta->sleepq_len == 0) {
		pstapriv->tim_bitmap &= ~BIT(psta->aid);

		update_mask = BIT(0);

		if (psta->state&WIFI_SLEEP_STATE)
			psta->state ^= WIFI_SLEEP_STATE;

		if (psta->state & WIFI_STA_ALIVE_CHK_STATE) {
			psta->expire_to = pstapriv->expire_to;
			psta->state ^= WIFI_STA_ALIVE_CHK_STATE;
		}

		pstapriv->sta_dz_bitmap &= ~BIT(psta->aid);
	}

	spin_unlock_bh(&psta->sleep_q.lock);

	/* for BC/MC Frames */
	psta_bmc = rtw_get_bcmc_stainfo(padapter);
	if (!psta_bmc)
		return;

	if ((pstapriv->sta_dz_bitmap&0xfffe) == 0x0) { /* no any sta in ps mode */
		spin_lock_bh(&psta_bmc->sleep_q.lock);

		xmitframe_phead = get_list_head(&psta_bmc->sleep_q);
		xmitframe_plist = xmitframe_phead->next;

		while ((rtw_end_of_queue_search(xmitframe_phead, xmitframe_plist)) == false) {
			pxmitframe = container_of(xmitframe_plist, struct xmit_frame, list);

			xmitframe_plist = xmitframe_plist->next;

			list_del_init(&pxmitframe->list);

			psta_bmc->sleepq_len--;
			if (psta_bmc->sleepq_len > 0)
				pxmitframe->attrib.mdata = 1;
			else
				pxmitframe->attrib.mdata = 0;
			pxmitframe->attrib.triggered = 1;
			spin_unlock_bh(&psta_bmc->sleep_q.lock);
			if (rtw_hal_xmit(padapter, pxmitframe) == true)
				rtw_os_xmit_complete(padapter, pxmitframe);
			spin_lock_bh(&psta_bmc->sleep_q.lock);
		}
		if (psta_bmc->sleepq_len == 0) {
			pstapriv->tim_bitmap &= ~BIT(0);
			pstapriv->sta_dz_bitmap &= ~BIT(0);
			update_mask |= BIT(1);
		}
		spin_unlock_bh(&psta_bmc->sleep_q.lock);
	}
	if (update_mask)
		update_beacon(padapter, _TIM_IE_, NULL, false);
}

void xmit_delivery_enabled_frames(struct rtw_adapter *padapter, struct sta_info *psta)
{
	u8 wmmps_ac = 0;
	struct list_head *xmitframe_plist, *xmitframe_phead;
	struct xmit_frame *pxmitframe = NULL;
	struct sta_priv *pstapriv = &padapter->stapriv;

	spin_lock_bh(&psta->sleep_q.lock);
	xmitframe_phead = get_list_head(&psta->sleep_q);
	xmitframe_plist = xmitframe_phead->next;

	while ((rtw_end_of_queue_search(xmitframe_phead, xmitframe_plist)) == false) {
		pxmitframe = container_of(xmitframe_plist, struct xmit_frame, list);

		xmitframe_plist = xmitframe_plist->next;

		switch (pxmitframe->attrib.priority) {
		case 1:
		case 2:
			wmmps_ac = psta->uapsd_bk&BIT(1);
			break;
		case 4:
		case 5:
			wmmps_ac = psta->uapsd_vi&BIT(1);
			break;
		case 6:
		case 7:
			wmmps_ac = psta->uapsd_vo&BIT(1);
			break;
		case 0:
		case 3:
		default:
			wmmps_ac = psta->uapsd_be&BIT(1);
			break;
		}
		if (!wmmps_ac)
			continue;
		list_del_init(&pxmitframe->list);
		psta->sleepq_len--;
		psta->sleepq_ac_len--;
		if (psta->sleepq_ac_len > 0) {
			pxmitframe->attrib.mdata = 1;
			pxmitframe->attrib.eosp = 0;
		} else {
			pxmitframe->attrib.mdata = 0;
			pxmitframe->attrib.eosp = 1;
		}

		pxmitframe->attrib.triggered = 1;

		if (rtw_hal_xmit(padapter, pxmitframe) == true)
			rtw_os_xmit_complete(padapter, pxmitframe);

		if ((psta->sleepq_ac_len == 0) && (!psta->has_legacy_ac) && (wmmps_ac)) {
			pstapriv->tim_bitmap &= ~BIT(psta->aid);

			/* upate BCN for TIM IE */
			update_beacon(padapter, _TIM_IE_, NULL, false);
		}
	}
	spin_unlock_bh(&psta->sleep_q.lock);
}
#endif

void rtw_sctx_init(struct submit_ctx *sctx, int timeout_ms)
{
	sctx->timeout_ms = timeout_ms;
	sctx->submit_time = rtw_get_current_time();
	init_completion(&sctx->done);
	sctx->status = RTW_SCTX_SUBMITTED;
}

int rtw_sctx_wait(struct submit_ctx *sctx)
{
	int ret = _FAIL;
	unsigned long expire;
	int status = 0;

	expire = sctx->timeout_ms ? msecs_to_jiffies(sctx->timeout_ms) : MAX_SCHEDULE_TIMEOUT;
	if (!wait_for_completion_timeout(&sctx->done, expire)) {
		/* timeout, do something?? */
		status = RTW_SCTX_DONE_TIMEOUT;
		DBG_8192D("%s timeout\n", __func__);
	} else {
		status = sctx->status;
	}

	if (status == RTW_SCTX_DONE_SUCCESS)
		ret = _SUCCESS;
	return ret;
}

static bool rtw_sctx_chk_waring_status(int status)
{
	switch (status) {
	case RTW_SCTX_DONE_UNKNOWN:
	case RTW_SCTX_DONE_BUF_ALLOC:
	case RTW_SCTX_DONE_BUF_FREE:
	case RTW_SCTX_DONE_DRV_STOP:
	case RTW_SCTX_DONE_DEV_REMOVE:
		return true;
	default:
		return false;
	}
}

void rtw_sctx_done_err(struct submit_ctx **sctx, int status)
{
	if (*sctx) {
		if (rtw_sctx_chk_waring_status(status))
			DBG_8192D("%s status:%d\n", __func__, status);
		(*sctx)->status = status;
		complete(&((*sctx)->done));
		*sctx = NULL;
	}
}

void rtw_sctx_done(struct submit_ctx **sctx)
{
	rtw_sctx_done_err(sctx, RTW_SCTX_DONE_SUCCESS);
}

/**
 * rtw_ack_tx_polling -
 * @pxmitpriv: xmit_priv to address ack_tx_ops
 * @timeout_ms: timeout msec
 *
 * Init ack_tx_ops and then do c2h_evt_hdl() and polling ack_tx_ops repeatedly
 * till tx report or timeout
 * Returns: _SUCCESS if TX report ok, _FAIL for others
 */
static int rtw_ack_tx_polling(struct xmit_priv *pxmitpriv, u32 timeout_ms)
{
	int ret = _FAIL;
	struct submit_ctx *pack_tx_ops = &pxmitpriv->ack_tx_ops;
	struct rtw_adapter *adapter = container_of(pxmitpriv, struct rtw_adapter, xmitpriv);

	pack_tx_ops->submit_time = rtw_get_current_time();
	pack_tx_ops->timeout_ms = timeout_ms;
	pack_tx_ops->status = RTW_SCTX_SUBMITTED;

	do {
		c2h_evt_hdl(adapter, NULL, rtw_hal_c2h_id_filter_ccx(adapter));
		if (pack_tx_ops->status != RTW_SCTX_SUBMITTED)
			break;
		if (adapter->bDriverStopped) {
			pack_tx_ops->status = RTW_SCTX_DONE_DRV_STOP;
			break;
		}
		if (adapter->bSurpriseRemoved) {
			pack_tx_ops->status = RTW_SCTX_DONE_DEV_REMOVE;
			break;
		}

		rtw_msleep_os(10);
	} while (rtw_get_passing_time_ms(pack_tx_ops->submit_time) < timeout_ms);

	if (pack_tx_ops->status == RTW_SCTX_SUBMITTED) {
		pack_tx_ops->status = RTW_SCTX_DONE_TIMEOUT;
		DBG_8192D("%s timeout\n", __func__);
	}

	if (pack_tx_ops->status == RTW_SCTX_DONE_SUCCESS)
		ret = _SUCCESS;

	return ret;
}

int rtw_ack_tx_wait(struct xmit_priv *pxmitpriv, u32 timeout_ms)
{
	return rtw_ack_tx_polling(pxmitpriv, timeout_ms);
}

void rtw_ack_tx_done(struct xmit_priv *pxmitpriv, int status)
{
	struct submit_ctx *pack_tx_ops = &pxmitpriv->ack_tx_ops;

	if (pxmitpriv->ack_tx)
		rtw_sctx_done_err(&pack_tx_ops, status);
	else
		DBG_8192D("%s ack_tx not set\n", __func__);
}
