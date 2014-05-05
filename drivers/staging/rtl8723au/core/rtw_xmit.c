/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
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
 ******************************************************************************/
#define _RTW_XMIT_C_

#include <osdep_service.h>
#include <drv_types.h>
#include <wifi.h>
#include <osdep_intf.h>
#include <linux/ip.h>
#include <usb_ops.h>

static u8 P802_1H_OUI[P80211_OUI_LEN] = { 0x00, 0x00, 0xf8 };
static u8 RFC1042_OUI[P80211_OUI_LEN] = { 0x00, 0x00, 0x00 };

static void _init_txservq(struct tx_servq *ptxservq)
{

	INIT_LIST_HEAD(&ptxservq->tx_pending);
	_rtw_init_queue23a(&ptxservq->sta_pending);
	ptxservq->qcnt = 0;

}

void	_rtw_init_sta_xmit_priv23a(struct sta_xmit_priv *psta_xmitpriv)
{

	spin_lock_init(&psta_xmitpriv->lock);

	/* for (i = 0 ; i < MAX_NUMBLKS; i++) */
	/*	_init_txservq(&psta_xmitpriv->blk_q[i]); */

	_init_txservq(&psta_xmitpriv->be_q);
	_init_txservq(&psta_xmitpriv->bk_q);
	_init_txservq(&psta_xmitpriv->vi_q);
	_init_txservq(&psta_xmitpriv->vo_q);
	INIT_LIST_HEAD(&psta_xmitpriv->legacy_dz);
	INIT_LIST_HEAD(&psta_xmitpriv->apsd);

}

s32	_rtw_init_xmit_priv23a(struct xmit_priv *pxmitpriv, struct rtw_adapter *padapter)
{
	int i;
	struct xmit_buf *pxmitbuf;
	struct xmit_frame *pxframe;
	int	res = _SUCCESS;
	u32 max_xmit_extbuf_size = MAX_XMIT_EXTBUF_SZ;
	u32 num_xmit_extbuf = NR_XMIT_EXTBUFF;

	/*  We don't need to memset padapter->XXX to zero, because adapter is allocated by rtw_zvmalloc(). */
	/* memset((unsigned char *)pxmitpriv, 0, sizeof(struct xmit_priv)); */

	spin_lock_init(&pxmitpriv->lock);
	spin_lock_init(&pxmitpriv->lock_sctx);
	sema_init(&pxmitpriv->xmit_sema, 0);
	sema_init(&pxmitpriv->terminate_xmitthread_sema, 0);

	/*
	Please insert all the queue initializaiton using _rtw_init_queue23a below
	*/

	pxmitpriv->adapter = padapter;

	_rtw_init_queue23a(&pxmitpriv->be_pending);
	_rtw_init_queue23a(&pxmitpriv->bk_pending);
	_rtw_init_queue23a(&pxmitpriv->vi_pending);
	_rtw_init_queue23a(&pxmitpriv->vo_pending);
	_rtw_init_queue23a(&pxmitpriv->bm_pending);

	_rtw_init_queue23a(&pxmitpriv->free_xmit_queue);

	/*
	Please allocate memory with the sz = (struct xmit_frame) * NR_XMITFRAME,
	and initialize free_xmit_frame below.
	Please also apply  free_txobj to link_up all the xmit_frames...
	*/

	pxmitpriv->pallocated_frame_buf = rtw_zvmalloc(NR_XMITFRAME * sizeof(struct xmit_frame) + 4);

	if (pxmitpriv->pallocated_frame_buf  == NULL) {
		pxmitpriv->pxmit_frame_buf = NULL;
		RT_TRACE(_module_rtl871x_xmit_c_, _drv_err_, ("alloc xmit_frame fail!\n"));
		res = _FAIL;
		goto exit;
	}
	pxmitpriv->pxmit_frame_buf = PTR_ALIGN(pxmitpriv->pallocated_frame_buf, 4);

	pxframe = (struct xmit_frame*) pxmitpriv->pxmit_frame_buf;

	for (i = 0; i < NR_XMITFRAME; i++) {
		INIT_LIST_HEAD(&pxframe->list);

		pxframe->padapter = padapter;
		pxframe->frame_tag = NULL_FRAMETAG;

		pxframe->pkt = NULL;

		pxframe->buf_addr = NULL;
		pxframe->pxmitbuf = NULL;

		list_add_tail(&pxframe->list,
			      &pxmitpriv->free_xmit_queue.queue);

		pxframe++;
	}

	pxmitpriv->free_xmitframe_cnt = NR_XMITFRAME;

	pxmitpriv->frag_len = MAX_FRAG_THRESHOLD;

	/* init xmit_buf */
	_rtw_init_queue23a(&pxmitpriv->free_xmitbuf_queue);
	INIT_LIST_HEAD(&pxmitpriv->xmitbuf_list);
	_rtw_init_queue23a(&pxmitpriv->pending_xmitbuf_queue);

	for (i = 0; i < NR_XMITBUFF; i++) {
		pxmitbuf = kzalloc(sizeof(struct xmit_buf), GFP_KERNEL);
		if (!pxmitbuf)
			goto fail;
		INIT_LIST_HEAD(&pxmitbuf->list);
		INIT_LIST_HEAD(&pxmitbuf->list2);

		pxmitbuf->padapter = padapter;

		/* Tx buf allocation may fail sometimes, so sleep and retry. */
		res = rtw_os_xmit_resource_alloc23a(padapter, pxmitbuf,
						 (MAX_XMITBUF_SZ + XMITBUF_ALIGN_SZ));
		if (res == _FAIL) {
			goto fail;
		}

		list_add_tail(&pxmitbuf->list,
			      &pxmitpriv->free_xmitbuf_queue.queue);
		list_add_tail(&pxmitbuf->list2,
			      &pxmitpriv->xmitbuf_list);
	}

	pxmitpriv->free_xmitbuf_cnt = NR_XMITBUFF;

	/* init xframe_ext queue,  the same count as extbuf  */
	_rtw_init_queue23a(&pxmitpriv->free_xframe_ext_queue);

	pxmitpriv->xframe_ext_alloc_addr = rtw_zvmalloc(num_xmit_extbuf * sizeof(struct xmit_frame) + 4);

	if (pxmitpriv->xframe_ext_alloc_addr  == NULL) {
		pxmitpriv->xframe_ext = NULL;
		RT_TRACE(_module_rtl871x_xmit_c_, _drv_err_, ("alloc xframe_ext fail!\n"));
		res = _FAIL;
		goto exit;
	}
	pxmitpriv->xframe_ext = PTR_ALIGN(pxmitpriv->xframe_ext_alloc_addr, 4);
	pxframe = (struct xmit_frame*)pxmitpriv->xframe_ext;

	for (i = 0; i < num_xmit_extbuf; i++) {
		INIT_LIST_HEAD(&pxframe->list);

		pxframe->padapter = padapter;
		pxframe->frame_tag = NULL_FRAMETAG;

		pxframe->pkt = NULL;

		pxframe->buf_addr = NULL;
		pxframe->pxmitbuf = NULL;

		pxframe->ext_tag = 1;

		list_add_tail(&pxframe->list,
			      &pxmitpriv->free_xframe_ext_queue.queue);

		pxframe++;
	}
	pxmitpriv->free_xframe_ext_cnt = num_xmit_extbuf;

	/*  Init xmit extension buff */
	_rtw_init_queue23a(&pxmitpriv->free_xmit_extbuf_queue);
	INIT_LIST_HEAD(&pxmitpriv->xmitextbuf_list);

	for (i = 0; i < num_xmit_extbuf; i++) {
		pxmitbuf = kzalloc(sizeof(struct xmit_buf), GFP_KERNEL);
		if (!pxmitbuf)
			goto fail;
		INIT_LIST_HEAD(&pxmitbuf->list);
		INIT_LIST_HEAD(&pxmitbuf->list2);

		pxmitbuf->padapter = padapter;

		/* Tx buf allocation may fail sometimes, so sleep and retry. */
		res = rtw_os_xmit_resource_alloc23a(padapter, pxmitbuf,
						 max_xmit_extbuf_size + XMITBUF_ALIGN_SZ);
		if (res == _FAIL) {
			goto exit;
		}

		list_add_tail(&pxmitbuf->list,
			      &pxmitpriv->free_xmit_extbuf_queue.queue);
		list_add_tail(&pxmitbuf->list2,
			      &pxmitpriv->xmitextbuf_list);
	}

	pxmitpriv->free_xmit_extbuf_cnt = num_xmit_extbuf;

	rtw_alloc_hwxmits23a(padapter);
	rtw_init_hwxmits23a(pxmitpriv->hwxmits, pxmitpriv->hwxmit_entry);

	for (i = 0; i < 4; i ++)
		pxmitpriv->wmm_para_seq[i] = i;

	pxmitpriv->txirp_cnt = 1;

	sema_init(&pxmitpriv->tx_retevt, 0);

	/* per AC pending irp */
	pxmitpriv->beq_cnt = 0;
	pxmitpriv->bkq_cnt = 0;
	pxmitpriv->viq_cnt = 0;
	pxmitpriv->voq_cnt = 0;

	pxmitpriv->ack_tx = false;
	mutex_init(&pxmitpriv->ack_tx_mutex);
	rtw_sctx_init23a(&pxmitpriv->ack_tx_ops, 0);
	rtw_hal_init23a_xmit_priv(padapter);

exit:

	return res;
fail:
	goto exit;
}

void _rtw_free_xmit_priv23a (struct xmit_priv *pxmitpriv)
{
	struct rtw_adapter *padapter = pxmitpriv->adapter;
	struct xmit_frame *pxmitframe = (struct xmit_frame*) pxmitpriv->pxmit_frame_buf;
	struct xmit_buf *pxmitbuf;
	struct list_head *plist, *ptmp;
	u32 num_xmit_extbuf = NR_XMIT_EXTBUFF;
	int i;

	rtw_hal_free_xmit_priv23a(padapter);

	if (pxmitpriv->pxmit_frame_buf == NULL)
		return;
	for (i = 0; i < NR_XMITFRAME; i++) {
		rtw_os_xmit_complete23a(padapter, pxmitframe);
		pxmitframe++;
	}

	list_for_each_safe(plist, ptmp, &pxmitpriv->xmitbuf_list) {
		pxmitbuf = container_of(plist, struct xmit_buf, list2);
		list_del_init(&pxmitbuf->list2);
		rtw_os_xmit_resource_free23a(padapter, pxmitbuf);
		kfree(pxmitbuf);
	}

	if (pxmitpriv->pallocated_frame_buf) {
		rtw_vmfree(pxmitpriv->pallocated_frame_buf, NR_XMITFRAME * sizeof(struct xmit_frame) + 4);
	}

	/* free xframe_ext queue,  the same count as extbuf  */
	if ((pxmitframe = (struct xmit_frame*)pxmitpriv->xframe_ext)) {
		for (i = 0; i<num_xmit_extbuf; i++) {
			rtw_os_xmit_complete23a(padapter, pxmitframe);
			pxmitframe++;
		}
	}
	if (pxmitpriv->xframe_ext_alloc_addr)
		rtw_vmfree(pxmitpriv->xframe_ext_alloc_addr, num_xmit_extbuf * sizeof(struct xmit_frame) + 4);

	/*  free xmit extension buff */
	list_for_each_safe(plist, ptmp, &pxmitpriv->xmitextbuf_list) {
		pxmitbuf = container_of(plist, struct xmit_buf, list2);
		list_del_init(&pxmitbuf->list2);
		rtw_os_xmit_resource_free23a(padapter, pxmitbuf);
		kfree(pxmitbuf);
	}

	rtw_free_hwxmits23a(padapter);
	mutex_destroy(&pxmitpriv->ack_tx_mutex);
}

static void update_attrib_vcs_info(struct rtw_adapter *padapter, struct xmit_frame *pxmitframe)
{
	u32	sz;
	struct pkt_attrib	*pattrib = &pxmitframe->attrib;
	struct sta_info	*psta = pattrib->psta;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;

        if (pattrib->psta) {
		psta = pattrib->psta;
	} else {
		DBG_8723A("%s, call rtw_get_stainfo23a()\n", __func__);
		psta = rtw_get_stainfo23a(&padapter->stapriv, &pattrib->ra[0]);
	}

        if (psta == NULL) {
		DBG_8723A("%s, psta == NUL\n", __func__);
		return;
	}

	if (!(psta->state &_FW_LINKED)) {
		DBG_8723A("%s, psta->state(0x%x) != _FW_LINKED\n", __func__, psta->state);
		return;
	}

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
			if ((pmlmeinfo->assoc_AP_vendor == HT_IOT_PEER_ATHEROS) &&
			    (pattrib->ampdu_en) &&
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
				u8 HTOpMode = pmlmeinfo->HT_protection;
				if ((pmlmeext->cur_bwmode && (HTOpMode == 2 || HTOpMode == 3)) ||
				    (!pmlmeext->cur_bwmode && HTOpMode == 3)) {
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
			if (pattrib->ampdu_en) {
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
	/*if (psta->rtsen)
		pattrib->vcs_mode = RTS_CTS;
	else if (psta->cts2self)
		pattrib->vcs_mode = CTS_TO_SELF;
	else
		pattrib->vcs_mode = NONE_VCS;*/

	pattrib->mdata = 0;
	pattrib->eosp = 0;
	pattrib->triggered = 0;

	/* qos_en, ht_en, init rate, , bw, ch_offset, sgi */
	pattrib->qos_en = psta->qos_option;

	pattrib->raid = psta->raid;
	pattrib->ht_en = psta->htpriv.ht_option;
	pattrib->bwmode = psta->htpriv.bwmode;
	pattrib->ch_offset = psta->htpriv.ch_offset;
	pattrib->sgi = psta->htpriv.sgi;
	pattrib->ampdu_en = false;

	pattrib->retry_ctrl = false;
}

u8 qos_acm23a(u8 acm_mask, u8 priority)
{
	u8 change_priority = priority;

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
		DBG_8723A("qos_acm23a(): invalid pattrib->priority: %d!!!\n",
			  priority);
		break;
	}

	return change_priority;
}

static void set_qos(struct pkt_file *ppktfile, struct pkt_attrib *pattrib)
{
	struct ethhdr etherhdr;
	struct iphdr ip_hdr;
	s32 UserPriority = 0;

	_rtw_open_pktfile23a(ppktfile->pkt, ppktfile);
	_rtw_pktfile_read23a(ppktfile, (unsigned char*)&etherhdr, ETH_HLEN);

	/*  get UserPriority from IP hdr */
	if (pattrib->ether_type == 0x0800) {
		_rtw_pktfile_read23a(ppktfile, (u8*)&ip_hdr, sizeof(ip_hdr));
/*		UserPriority = (ntohs(ip_hdr.tos) >> 5) & 0x3; */
		UserPriority = ip_hdr.tos >> 5;
	} else if (pattrib->ether_type == 0x888e) {
		/*  "When priority processing of data frames is supported, */
		/*  a STA's SME should send EAPOL-Key frames at the highest
		    priority." */
		UserPriority = 7;
	}

	pattrib->priority = UserPriority;
	pattrib->hdrlen = sizeof(struct ieee80211_qos_hdr);
	pattrib->subtype = WIFI_QOS_DATA_TYPE;
}

static s32 update_attrib(struct rtw_adapter *padapter,
			 struct sk_buff *pkt, struct pkt_attrib *pattrib)
{
	uint i;
	struct pkt_file pktfile;
	struct sta_info *psta = NULL;
	struct ethhdr etherhdr;

	int bmcast;
	struct sta_priv	*pstapriv = &padapter->stapriv;
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct qos_priv	*pqospriv = &pmlmepriv->qospriv;
	int res = _SUCCESS;

	_rtw_open_pktfile23a(pkt, &pktfile);
	i = _rtw_pktfile_read23a(&pktfile, (u8*)&etherhdr, ETH_HLEN);

	pattrib->ether_type = ntohs(etherhdr.h_proto);

	memcpy(pattrib->dst, &etherhdr.h_dest, ETH_ALEN);
	memcpy(pattrib->src, &etherhdr.h_source, ETH_ALEN);

	pattrib->pctrl = 0;

	if ((check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == true) ||
		(check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == true)) {
		memcpy(pattrib->ra, pattrib->dst, ETH_ALEN);
		memcpy(pattrib->ta, pattrib->src, ETH_ALEN);
	}
	else if (check_fwstate(pmlmepriv, WIFI_STATION_STATE)) {
		memcpy(pattrib->ra, get_bssid(pmlmepriv), ETH_ALEN);
		memcpy(pattrib->ta, pattrib->src, ETH_ALEN);
	}
	else if (check_fwstate(pmlmepriv, WIFI_AP_STATE)) {
		memcpy(pattrib->ra, pattrib->dst, ETH_ALEN);
		memcpy(pattrib->ta, get_bssid(pmlmepriv), ETH_ALEN);
	}

	pattrib->pktlen = pktfile.pkt_len;

	if (pattrib->ether_type == ETH_P_IP) {
		/*  The following is for DHCP and ARP packet, we use cck1M
		    to tx these packets and let LPS awake some time */
		/*  to prevent DHCP protocol fail */
		u8 tmp[24];
		_rtw_pktfile_read23a(&pktfile, &tmp[0], 24);
		pattrib->dhcp_pkt = 0;
		if (pktfile.pkt_len > 282) {/* MINIMUM_DHCP_PACKET_SIZE) { */
			if (ETH_P_IP == pattrib->ether_type) {/*  IP header */
				if (((tmp[21] == 68) && (tmp[23] == 67)) ||
				    ((tmp[21] == 67) && (tmp[23] == 68))) {
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
	} else if (0x888e == pattrib->ether_type) {
		DBG_8723A_LEVEL(_drv_always_, "send eapol packet\n");
	}

	if ((pattrib->ether_type == 0x888e) || (pattrib->dhcp_pkt == 1)) {
		rtw_set_scan_deny(padapter, 3000);
	}

	/*  If EAPOL , ARP , OR DHCP packet, driver must be in active mode. */
	if ((pattrib->ether_type == 0x0806) ||
	    (pattrib->ether_type == 0x888e) || (pattrib->dhcp_pkt == 1)) {
		rtw_lps_ctrl_wk_cmd23a(padapter, LPS_CTRL_SPECIAL_PACKET, 1);
	}

	bmcast = is_multicast_ether_addr(pattrib->ra);

	/*  get sta_info */
	if (bmcast) {
		psta = rtw_get_bcmc_stainfo23a(padapter);
	} else {
		psta = rtw_get_stainfo23a(pstapriv, pattrib->ra);
		if (psta == NULL) { /*  if we cannot get psta => drrp the pkt */
			RT_TRACE(_module_rtl871x_xmit_c_, _drv_alert_,
				 ("\nupdate_attrib => get sta_info fail, ra:"
				  MAC_FMT"\n", MAC_ARG(pattrib->ra)));
			res = _FAIL;
			goto exit;
		} else if ((check_fwstate(pmlmepriv, WIFI_AP_STATE) == true) &&
			   (!(psta->state & _FW_LINKED))) {
			res = _FAIL;
			goto exit;
		}
	}

	if (psta) {
		pattrib->mac_id = psta->mac_id;
		/* DBG_8723A("%s ==> mac_id(%d)\n", __func__, pattrib->mac_id); */
		pattrib->psta = psta;
	} else {
		/*  if we cannot get psta => drop the pkt */
		RT_TRACE(_module_rtl871x_xmit_c_, _drv_alert_,
			 ("\nupdate_attrib => get sta_info fail, ra:" MAC_FMT
			  "\n", MAC_ARG(pattrib->ra)));
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
			set_qos(&pktfile, pattrib);
	} else {
		if (pqospriv->qos_option) {
			set_qos(&pktfile, pattrib);

			if (pmlmepriv->acm_mask != 0) {
				pattrib->priority = qos_acm23a(pmlmepriv->acm_mask,
							    pattrib->priority);
			}
		}
	}

	if (psta->ieee8021x_blocked == true) {
		RT_TRACE(_module_rtl871x_xmit_c_, _drv_err_,
			 ("\n psta->ieee8021x_blocked == true\n"));

		pattrib->encrypt = 0;

		if ((pattrib->ether_type != 0x888e) &&
		    (check_fwstate(pmlmepriv, WIFI_MP_STATE) == false)) {
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
		pattrib->iv_len = 4;
		pattrib->icv_len = 4;
		break;

	case _TKIP_:
		pattrib->iv_len = 8;
		pattrib->icv_len = 4;

		if (padapter->securitypriv.busetkipkey == _FAIL) {
			RT_TRACE(_module_rtl871x_xmit_c_, _drv_err_,
				 ("\npadapter->securitypriv.busetkip"
				  "key(%d) == _FAIL drop packet\n",
				  padapter->securitypriv.busetkipkey));
			res = _FAIL;
			goto exit;
		}

		break;
	case _AES_:
		RT_TRACE(_module_rtl871x_xmit_c_, _drv_info_,
			 ("pattrib->encrypt =%d (_AES_)\n", pattrib->encrypt));
		pattrib->iv_len = 8;
		pattrib->icv_len = 8;
		break;

	default:
		pattrib->iv_len = 0;
		pattrib->icv_len = 0;
		break;
	}

	RT_TRACE(_module_rtl871x_xmit_c_, _drv_info_,
		 ("update_attrib: encrypt =%d\n", pattrib->encrypt));

	if (pattrib->encrypt && psecuritypriv->hw_decrypted == false) {
		pattrib->bswenc = true;
		RT_TRACE(_module_rtl871x_xmit_c_, _drv_err_,
			 ("update_attrib: encrypt =%d bswenc = true\n",
			  pattrib->encrypt));
	} else {
		pattrib->bswenc = false;
		RT_TRACE(_module_rtl871x_xmit_c_, _drv_info_,
			 ("update_attrib: bswenc = false\n"));
	}
	update_attrib_phy_info(pattrib, psta);

exit:

	return res;
}

static s32 xmitframe_addmic(struct rtw_adapter *padapter,
			    struct xmit_frame *pxmitframe) {
	struct mic_data micdata;
	struct sta_info *stainfo;
	struct pkt_attrib *pattrib = &pxmitframe->attrib;
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	int curfragnum, length;
	u8 *pframe, *payload, mic[8];
	u8 priority[4]= {0x0, 0x0, 0x0, 0x0};
	u8 hw_hdr_offset = 0;
	int bmcst = is_multicast_ether_addr(pattrib->ra);

	if (pattrib->psta) {
		stainfo = pattrib->psta;
	} else {
		DBG_8723A("%s, call rtw_get_stainfo23a()\n", __func__);
		stainfo = rtw_get_stainfo23a(&padapter->stapriv, &pattrib->ra[0]);
	}

	if (!stainfo) {
		DBG_8723A("%s, psta == NUL\n", __func__);
		return _FAIL;
	}

	if (!(stainfo->state &_FW_LINKED)) {
		DBG_8723A("%s, psta->state(0x%x) != _FW_LINKED\n",
			  __func__, stainfo->state);
		return _FAIL;
	}

	hw_hdr_offset = TXDESC_OFFSET;

	if (pattrib->encrypt == _TKIP_) {
		/* encode mic code */
		if (stainfo) {
			u8 null_key[16]={0x0, 0x0, 0x0, 0x0,
					 0x0, 0x0, 0x0, 0x0,
					 0x0, 0x0, 0x0, 0x0,
					 0x0, 0x0, 0x0, 0x0};

			pframe = pxmitframe->buf_addr + hw_hdr_offset;

			if (bmcst) {
				if (!memcmp(psecuritypriv->dot118021XGrptxmickey[psecuritypriv->dot118021XGrpKeyid].skey, null_key, 16)) {
					return _FAIL;
				}
				/* start to calculate the mic code */
				rtw_secmicsetkey23a(&micdata, psecuritypriv->dot118021XGrptxmickey[psecuritypriv->dot118021XGrpKeyid].skey);
			} else {
				if (!memcmp(&stainfo->dot11tkiptxmickey.skey[0],
					    null_key, 16)) {
					return _FAIL;
				}
				/* start to calculate the mic code */
				rtw_secmicsetkey23a(&micdata, &stainfo->dot11tkiptxmickey.skey[0]);
			}

			if (pframe[1] & 1) {   /* ToDS == 1 */
				/* DA */
				rtw_secmicappend23a(&micdata, &pframe[16], 6);
				if (pframe[1] & 2)  /* From Ds == 1 */
					rtw_secmicappend23a(&micdata,
							 &pframe[24], 6);
				else
					rtw_secmicappend23a(&micdata,
							 &pframe[10], 6);
			} else {	/* ToDS == 0 */
				/* DA */
				rtw_secmicappend23a(&micdata, &pframe[4], 6);
				if (pframe[1] & 2)  /* From Ds == 1 */
					rtw_secmicappend23a(&micdata,
							 &pframe[16], 6);
				else
					rtw_secmicappend23a(&micdata,
							 &pframe[10], 6);
			}

			/* if (pqospriv->qos_option == 1) */
			if (pattrib->qos_en)
				priority[0] = (u8)pxmitframe->attrib.priority;

			rtw_secmicappend23a(&micdata, &priority[0], 4);

			payload = pframe;

			for (curfragnum = 0; curfragnum < pattrib->nr_frags;
			     curfragnum++) {
				payload = PTR_ALIGN(payload, 4);
				RT_TRACE(_module_rtl871x_xmit_c_, _drv_err_,
					 ("=== curfragnum =%d, pframe = 0x%.2x, "
					  "0x%.2x, 0x%.2x, 0x%.2x, 0x%.2x, 0x"
					  "%.2x, 0x%.2x, 0x%.2x,!!!\n",
					  curfragnum, *payload, *(payload + 1),
					  *(payload + 2), *(payload + 3),
					  *(payload + 4), *(payload + 5),
					  *(payload + 6), *(payload + 7)));

				payload = payload + pattrib->hdrlen +
					pattrib->iv_len;
				RT_TRACE(_module_rtl871x_xmit_c_, _drv_err_,
					 ("curfragnum =%d pattrib->hdrlen =%d "
					  "pattrib->iv_len =%d", curfragnum,
					  pattrib->hdrlen, pattrib->iv_len));
				if ((curfragnum + 1) == pattrib->nr_frags) {
					length = pattrib->last_txcmdsz -
						pattrib->hdrlen -
						pattrib->iv_len -
						((pattrib->bswenc) ?
						 pattrib->icv_len : 0);
					rtw_secmicappend23a(&micdata, payload,
							 length);
					payload = payload + length;
				} else {
					length = pxmitpriv->frag_len -
						pattrib->hdrlen -
						pattrib->iv_len -
						((pattrib->bswenc) ?
						 pattrib->icv_len : 0);
					rtw_secmicappend23a(&micdata, payload,
							 length);
					payload = payload + length +
						pattrib->icv_len;
					RT_TRACE(_module_rtl871x_xmit_c_,
						 _drv_err_,
						 ("curfragnum =%d length =%d "
						  "pattrib->icv_len =%d",
						  curfragnum, length,
						  pattrib->icv_len));
				}
			}
			rtw_secgetmic23a(&micdata, &mic[0]);
			RT_TRACE(_module_rtl871x_xmit_c_, _drv_err_,
				 ("xmitframe_addmic: before add mic code!!\n"));
			RT_TRACE(_module_rtl871x_xmit_c_, _drv_err_,
				 ("xmitframe_addmic: pattrib->last_txcmdsz ="
				  "%d!!!\n", pattrib->last_txcmdsz));
			RT_TRACE(_module_rtl871x_xmit_c_, _drv_err_,
				 ("xmitframe_addmic: mic[0]= 0x%.2x , mic[1]="
				  "0x%.2x , mic[2]= 0x%.2x , mic[3]= 0x%.2x\n"
				  "mic[4]= 0x%.2x , mic[5]= 0x%.2x , mic[6]= 0x%.2x "
				  ", mic[7]= 0x%.2x !!!!\n", mic[0], mic[1],
				  mic[2], mic[3], mic[4], mic[5], mic[6],
				  mic[7]));
			/* add mic code  and add the mic code length
			   in last_txcmdsz */

			memcpy(payload, &mic[0], 8);
			pattrib->last_txcmdsz += 8;

			RT_TRACE(_module_rtl871x_xmit_c_, _drv_info_,
				 ("\n ======== last pkt ========\n"));
			payload = payload - pattrib->last_txcmdsz + 8;
			for (curfragnum = 0; curfragnum < pattrib->last_txcmdsz;
			     curfragnum = curfragnum + 8)
				RT_TRACE(_module_rtl871x_xmit_c_, _drv_info_,
					 (" %.2x,  %.2x,  %.2x,  %.2x,  %.2x, "
					  " %.2x,  %.2x,  %.2x ",
					  *(payload + curfragnum),
					  *(payload + curfragnum + 1),
					  *(payload + curfragnum + 2),
					  *(payload + curfragnum + 3),
					  *(payload + curfragnum + 4),
					  *(payload + curfragnum + 5),
					  *(payload + curfragnum + 6),
					  *(payload + curfragnum + 7)));
			} else {
				RT_TRACE(_module_rtl871x_xmit_c_, _drv_err_,
					 ("xmitframe_addmic: rtw_get_stainfo23a =="
					  "NULL!!!\n"));
		}
	}

	return _SUCCESS;
}

static s32 xmitframe_swencrypt(struct rtw_adapter *padapter,
			       struct xmit_frame *pxmitframe)
{
	struct pkt_attrib *pattrib = &pxmitframe->attrib;

	/* if ((psecuritypriv->sw_encrypt)||(pattrib->bswenc)) */
	if (pattrib->bswenc) {
		/* DBG_8723A("start xmitframe_swencrypt\n"); */
		RT_TRACE(_module_rtl871x_xmit_c_, _drv_alert_,
			 ("### xmitframe_swencrypt\n"));
		switch (pattrib->encrypt) {
		case _WEP40_:
		case _WEP104_:
			rtw_wep_encrypt23a(padapter, pxmitframe);
			break;
		case _TKIP_:
			rtw_tkip_encrypt23a(padapter, pxmitframe);
			break;
		case _AES_:
			rtw_aes_encrypt23a(padapter, pxmitframe);
			break;
		default:
				break;
		}

	} else {
		RT_TRACE(_module_rtl871x_xmit_c_, _drv_notice_,
			 ("### xmitframe_hwencrypt\n"));
	}

	return _SUCCESS;
}

s32 rtw_make_wlanhdr23a(struct rtw_adapter *padapter, u8 *hdr,
		        struct pkt_attrib *pattrib)
{
	u16 *qc;

	struct ieee80211_hdr *pwlanhdr = (struct ieee80211_hdr *)hdr;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct qos_priv *pqospriv = &pmlmepriv->qospriv;
	u8 qos_option = false;
	int res = _SUCCESS;
	u16 *fctrl = &pwlanhdr->frame_control;

	struct sta_info *psta;

	int bmcst = is_multicast_ether_addr(pattrib->ra);

	if (pattrib->psta) {
		psta = pattrib->psta;
	} else {
		DBG_8723A("%s, call rtw_get_stainfo23a()\n", __func__);
		if (bmcst) {
			psta = rtw_get_bcmc_stainfo23a(padapter);
		} else {
			psta = rtw_get_stainfo23a(&padapter->stapriv, pattrib->ra);
		}
	}

	if (psta == NULL) {
		DBG_8723A("%s, psta == NUL\n", __func__);
		return _FAIL;
	}

	if (!(psta->state &_FW_LINKED)) {
		DBG_8723A("%s, psta->state(0x%x) != _FW_LINKED\n", __func__, psta->state);
		return _FAIL;
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

		}
		else if ((check_fwstate(pmlmepriv,  WIFI_AP_STATE) == true)) {
			/* to_ds = 0, fr_ds = 1; */
			SetFrDs(fctrl);
			memcpy(pwlanhdr->addr1, pattrib->dst, ETH_ALEN);
			memcpy(pwlanhdr->addr2, get_bssid(pmlmepriv), ETH_ALEN);
			memcpy(pwlanhdr->addr3, pattrib->src, ETH_ALEN);

			if (psta->qos_option)
				qos_option = true;
		}
		else if ((check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == true) ||
		(check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == true)) {
			memcpy(pwlanhdr->addr1, pattrib->dst, ETH_ALEN);
			memcpy(pwlanhdr->addr2, pattrib->src, ETH_ALEN);
			memcpy(pwlanhdr->addr3, get_bssid(pmlmepriv), ETH_ALEN);

			if (psta->qos_option)
				qos_option = true;
		}
		else {
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
				if (psta->htpriv.agg_enable_bitmap & CHKBIT(pattrib->priority))
				pattrib->ampdu_en = true;
			}
			/* re-check if enable ampdu by BA_starting_seqctrl */
			if (pattrib->ampdu_en) {
				u16 tx_seq;

				tx_seq = psta->BA_starting_seqctrl[pattrib->priority & 0x0f];

				/* check BA_starting_seqctrl */
				if (SN_LESS(pattrib->seqnum, tx_seq)) {
					/* DBG_8723A("tx ampdu seqnum(%d) < tx_seq(%d)\n", pattrib->seqnum, tx_seq); */
					pattrib->ampdu_en = false;/* AGG BK */
				} else if (SN_EQUAL(pattrib->seqnum, tx_seq)) {
					psta->BA_starting_seqctrl[pattrib->priority & 0x0f] = (tx_seq+1)&0xfff;
					pattrib->ampdu_en = true;/* AGG EN */
				} else {
					/* DBG_8723A("tx ampdu over run\n"); */
					psta->BA_starting_seqctrl[pattrib->priority & 0x0f] = (pattrib->seqnum+1)&0xfff;
					pattrib->ampdu_en = true;/* AGG EN */
				}
			}
		}
	}
exit:
	return res;
}

s32 rtw_txframes_pending23a(struct rtw_adapter *padapter)
{
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;

	return (!_rtw_queue_empty23a(&pxmitpriv->be_pending)) ||
	       (!_rtw_queue_empty23a(&pxmitpriv->bk_pending)) ||
	       (!_rtw_queue_empty23a(&pxmitpriv->vi_pending)) ||
	       (!_rtw_queue_empty23a(&pxmitpriv->vo_pending));
}

s32 rtw_txframes_sta_ac_pending23a(struct rtw_adapter *padapter,
				struct pkt_attrib *pattrib)
{
	struct sta_info *psta;
	struct tx_servq *ptxservq;
	int priority = pattrib->priority;

	if (pattrib->psta) {
		psta = pattrib->psta;
	} else {
		DBG_8723A("%s, call rtw_get_stainfo23a()\n", __func__);
		psta = rtw_get_stainfo23a(&padapter->stapriv, &pattrib->ra[0]);
	}
	if (psta == NULL) {
		DBG_8723A("%s, psta == NUL\n", __func__);
		return 0;
	}
	if (!(psta->state &_FW_LINKED)) {
		DBG_8723A("%s, psta->state(0x%x) != _FW_LINKED\n", __func__,
			  psta->state);
		return 0;
	}
	switch (priority) {
	case 1:
	case 2:
		ptxservq = &psta->sta_xmitpriv.bk_q;
		break;
	case 4:
	case 5:
		ptxservq = &psta->sta_xmitpriv.vi_q;
		break;
	case 6:
	case 7:
		ptxservq = &psta->sta_xmitpriv.vo_q;
		break;
	case 0:
	case 3:
	default:
		ptxservq = &psta->sta_xmitpriv.be_q;
		break;
	}
	return ptxservq->qcnt;
}

/*
 * Calculate wlan 802.11 packet MAX size from pkt_attrib
 * This function doesn't consider fragment case
 */
u32 rtw_calculate_wlan_pkt_size_by_attribue23a(struct pkt_attrib *pattrib)
{
	u32	len = 0;

	len = pattrib->hdrlen + pattrib->iv_len; /*  WLAN Header and IV */
	len += SNAP_SIZE + sizeof(u16); /*  LLC */
	len += pattrib->pktlen;
	if (pattrib->encrypt == _TKIP_) len += 8; /*  MIC */
	len += ((pattrib->bswenc) ? pattrib->icv_len : 0); /*  ICV */

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
s32 rtw_xmitframe_coalesce23a(struct rtw_adapter *padapter, struct sk_buff *pkt,
			      struct xmit_frame *pxmitframe)
{
	struct pkt_file pktfile;
	struct sta_info		*psta;
	struct xmit_priv	*pxmitpriv = &padapter->xmitpriv;
	struct pkt_attrib	*pattrib = &pxmitframe->attrib;
	s32 frg_inx, frg_len, mpdu_len, llc_sz, mem_sz;
	u8 *pframe, *mem_start;
	u8 hw_hdr_offset;
	u8 *pbuf_start;

	s32 bmcst = is_multicast_ether_addr(pattrib->ra);
	s32 res = _SUCCESS;

	if (pattrib->psta) {
		psta = pattrib->psta;
	} else {
		DBG_8723A("%s, call rtw_get_stainfo23a()\n", __func__);
		psta = rtw_get_stainfo23a(&padapter->stapriv, pattrib->ra);
	}

	if (psta == NULL) {
		DBG_8723A("%s, psta == NUL\n", __func__);
		return _FAIL;
	}

	if (!(psta->state &_FW_LINKED)) {
		DBG_8723A("%s, psta->state(0x%x) != _FW_LINKED\n", __func__, psta->state);
		return _FAIL;
	}

	if (pxmitframe->buf_addr == NULL) {
		DBG_8723A("==> %s buf_addr == NULL\n", __func__);
		return _FAIL;
	}

	pbuf_start = pxmitframe->buf_addr;

	hw_hdr_offset = TXDESC_OFFSET;

	mem_start = pbuf_start +	hw_hdr_offset;

	if (rtw_make_wlanhdr23a(padapter, mem_start, pattrib) == _FAIL) {
		RT_TRACE(_module_rtl871x_xmit_c_, _drv_err_,
			 ("rtw_xmitframe_coalesce23a: rtw_make_wlanhdr23a "
			  "fail; drop pkt\n"));
		res = _FAIL;
		goto exit;
	}

	_rtw_open_pktfile23a(pkt, &pktfile);
	_rtw_pktfile_read23a(&pktfile, NULL, pattrib->pkt_hdrlen);

	frg_inx = 0;
	frg_len = pxmitpriv->frag_len - 4;/* 2346-4 = 2342 */

	while (1) {
		llc_sz = 0;

		mpdu_len = frg_len;

		pframe = mem_start;

		SetMFrag(mem_start);

		pframe += pattrib->hdrlen;
		mpdu_len -= pattrib->hdrlen;

		/* adding icv, if necessary... */
		if (pattrib->iv_len) {
			if (psta != NULL) {
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
				 ("rtw_xmiaframe_coalesce23a: keyid =%d pattrib"
				  "->iv[3]=%.2x pframe =%.2x %.2x %.2x %.2x\n",
				  padapter->securitypriv.dot11PrivacyKeyIndex,
				  pattrib->iv[3], *pframe, *(pframe+1),
				  *(pframe+2), *(pframe+3)));
			pframe += pattrib->iv_len;
			mpdu_len -= pattrib->iv_len;
		}
		if (frg_inx == 0) {
			llc_sz = rtw_put_snap23a(pframe, pattrib->ether_type);
			pframe += llc_sz;
			mpdu_len -= llc_sz;
		}

		if ((pattrib->icv_len >0) && (pattrib->bswenc))
			mpdu_len -= pattrib->icv_len;

		if (bmcst) {
			/*  don't do fragment to broadcat/multicast packets */
			mem_sz = _rtw_pktfile_read23a(&pktfile, pframe, pattrib->pktlen);
		} else {
			mem_sz = _rtw_pktfile_read23a(&pktfile, pframe, mpdu_len);
		}
		pframe += mem_sz;

		if ((pattrib->icv_len >0) && (pattrib->bswenc)) {
			memcpy(pframe, pattrib->icv, pattrib->icv_len);
			pframe += pattrib->icv_len;
		}

		frg_inx++;

		if (bmcst || (rtw_endofpktfile23a(&pktfile))) {
			pattrib->nr_frags = frg_inx;

			pattrib->last_txcmdsz = pattrib->hdrlen +
						pattrib->iv_len +
						((pattrib->nr_frags == 1) ?
						llc_sz : 0) +
						((pattrib->bswenc) ?
						pattrib->icv_len : 0) + mem_sz;

			ClearMFrag(mem_start);

			break;
		} else {
			RT_TRACE(_module_rtl871x_xmit_c_, _drv_err_, ("%s: There're still something in packet!\n", __func__));
		}

		mem_start = PTR_ALIGN(pframe, 4) + hw_hdr_offset;
		memcpy(mem_start, pbuf_start + hw_hdr_offset, pattrib->hdrlen);

	}

	if (xmitframe_addmic(padapter, pxmitframe) == _FAIL) {
		RT_TRACE(_module_rtl871x_xmit_c_, _drv_err_, ("xmitframe_addmic(padapter, pxmitframe) == _FAIL\n"));
		DBG_8723A("xmitframe_addmic(padapter, pxmitframe) == _FAIL\n");
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
s32 rtw_put_snap23a(u8 *data, u16 h_proto)
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
	*(u16 *)(data + SNAP_SIZE) = htons(h_proto);
	return SNAP_SIZE + sizeof(u16);
}

void rtw_update_protection23a(struct rtw_adapter *padapter, u8 *ie, uint ie_len)
{
	struct	xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct	registry_priv *pregistrypriv = &padapter->registrypriv;
	uint	protection;
	u8	*perp;
	int	 erp_len;

	switch (pxmitpriv->vcs_setting) {
	case DISABLE_VCS:
		pxmitpriv->vcs = NONE_VCS;
		break;
	case ENABLE_VCS:
		break;
	case AUTO_VCS:
	default:
		perp = rtw_get_ie23a(ie, _ERPINFO_IE_, &erp_len, ie_len);
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

void rtw_count_tx_stats23a(struct rtw_adapter *padapter, struct xmit_frame *pxmitframe, int sz)
{
	struct sta_info *psta = NULL;
	struct stainfo_stats *pstats = NULL;
	struct xmit_priv	*pxmitpriv = &padapter->xmitpriv;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;

	if ((pxmitframe->frame_tag&0x0f) == DATA_FRAMETAG) {
		pxmitpriv->tx_bytes += sz;
		pmlmepriv->LinkDetectInfo.NumTxOkInPeriod++;

		psta = pxmitframe->attrib.psta;
		if (psta) {
			pstats = &psta->sta_stats;
			pstats->tx_pkts++;
			pstats->tx_bytes += sz;
		}
	}
}

struct xmit_buf *rtw_alloc_xmitbuf23a_ext(struct xmit_priv *pxmitpriv)
{
	unsigned long irqL;
	struct xmit_buf *pxmitbuf =  NULL;
	struct list_head *phead;
	struct rtw_queue *pfree_queue = &pxmitpriv->free_xmit_extbuf_queue;

	spin_lock_irqsave(&pfree_queue->lock, irqL);

	phead = get_list_head(pfree_queue);

	if (!list_empty(phead)) {
		pxmitbuf = list_first_entry(phead, struct xmit_buf, list);

		list_del_init(&pxmitbuf->list);

		pxmitpriv->free_xmit_extbuf_cnt--;
		pxmitbuf->priv_data = NULL;
		pxmitbuf->ext_tag = true;

		if (pxmitbuf->sctx) {
			DBG_8723A("%s pxmitbuf->sctx is not NULL\n", __func__);
			rtw23a_sctx_done_err(&pxmitbuf->sctx, RTW_SCTX_DONE_BUF_ALLOC);
		}
	}

	spin_unlock_irqrestore(&pfree_queue->lock, irqL);

	return pxmitbuf;
}

s32 rtw_free_xmitbuf_ext23a(struct xmit_priv *pxmitpriv, struct xmit_buf *pxmitbuf)
{
	unsigned long irqL;
	struct rtw_queue *pfree_queue = &pxmitpriv->free_xmit_extbuf_queue;

	if (pxmitbuf == NULL)
		return _FAIL;

	spin_lock_irqsave(&pfree_queue->lock, irqL);

	list_del_init(&pxmitbuf->list);

	list_add_tail(&pxmitbuf->list, get_list_head(pfree_queue));
	pxmitpriv->free_xmit_extbuf_cnt++;

	spin_unlock_irqrestore(&pfree_queue->lock, irqL);

	return _SUCCESS;
}

struct xmit_buf *rtw_alloc_xmitbuf23a(struct xmit_priv *pxmitpriv)
{
	unsigned long irqL;
	struct xmit_buf *pxmitbuf =  NULL;
	struct list_head *phead;
	struct rtw_queue *pfree_xmitbuf_queue = &pxmitpriv->free_xmitbuf_queue;

	/* DBG_8723A("+rtw_alloc_xmitbuf23a\n"); */

	spin_lock_irqsave(&pfree_xmitbuf_queue->lock, irqL);

	phead = get_list_head(pfree_xmitbuf_queue);

	if (!list_empty(phead)) {
		pxmitbuf = list_first_entry(phead, struct xmit_buf, list);

		list_del_init(&pxmitbuf->list);

		pxmitpriv->free_xmitbuf_cnt--;
		pxmitbuf->priv_data = NULL;
		pxmitbuf->ext_tag = false;
		pxmitbuf->flags = XMIT_VO_QUEUE;

		if (pxmitbuf->sctx) {
			DBG_8723A("%s pxmitbuf->sctx is not NULL\n", __func__);
			rtw23a_sctx_done_err(&pxmitbuf->sctx, RTW_SCTX_DONE_BUF_ALLOC);
		}
	}

	spin_unlock_irqrestore(&pfree_xmitbuf_queue->lock, irqL);

	return pxmitbuf;
}

s32 rtw_free_xmitbuf23a(struct xmit_priv *pxmitpriv, struct xmit_buf *pxmitbuf)
{
	unsigned long irqL;
	struct rtw_queue *pfree_xmitbuf_queue = &pxmitpriv->free_xmitbuf_queue;

	/* DBG_8723A("+rtw_free_xmitbuf23a\n"); */

	if (pxmitbuf == NULL)
		return _FAIL;

	if (pxmitbuf->sctx) {
		DBG_8723A("%s pxmitbuf->sctx is not NULL\n", __func__);
		rtw23a_sctx_done_err(&pxmitbuf->sctx, RTW_SCTX_DONE_BUF_FREE);
	}

	if (pxmitbuf->ext_tag) {
		rtw_free_xmitbuf_ext23a(pxmitpriv, pxmitbuf);
	} else {
		spin_lock_irqsave(&pfree_xmitbuf_queue->lock, irqL);

		list_del_init(&pxmitbuf->list);

		list_add_tail(&pxmitbuf->list,
			      get_list_head(pfree_xmitbuf_queue));

		pxmitpriv->free_xmitbuf_cnt++;
		spin_unlock_irqrestore(&pfree_xmitbuf_queue->lock, irqL);
	}

	return _SUCCESS;
}

static void rtw_init_xmitframe(struct xmit_frame *pxframe)
{
	if (pxframe !=  NULL) {
		/* default value setting */
		pxframe->buf_addr = NULL;
		pxframe->pxmitbuf = NULL;

		memset(&pxframe->attrib, 0, sizeof(struct pkt_attrib));
		/* pxframe->attrib.psta = NULL; */

		pxframe->frame_tag = DATA_FRAMETAG;

		pxframe->pkt = NULL;
		pxframe->pkt_offset = 1;/* default use pkt_offset to fill tx desc */

		pxframe->ack_report = 0;
	}
}

/*
Calling context:
1. OS_TXENTRY
2. RXENTRY (rx_thread or RX_ISR/RX_CallBack)

If we turn on USE_RXTHREAD, then, no need for critical section.
Otherwise, we must use _enter/_exit critical to protect free_xmit_queue...

Must be very very cautious...

*/
struct xmit_frame *rtw_alloc_xmitframe23a(struct xmit_priv *pxmitpriv)/* _queue *pfree_xmit_queue) */
{
	/*
		Please remember to use all the osdep_service api,
		and lock/unlock or _enter/_exit critical to protect
		pfree_xmit_queue
	*/

	struct xmit_frame *pxframe = NULL;
	struct list_head *plist, *phead;
	struct rtw_queue *pfree_xmit_queue = &pxmitpriv->free_xmit_queue;

	spin_lock_bh(&pfree_xmit_queue->lock);

	if (_rtw_queue_empty23a(pfree_xmit_queue) == true) {
		RT_TRACE(_module_rtl871x_xmit_c_, _drv_info_, ("rtw_alloc_xmitframe23a:%d\n", pxmitpriv->free_xmitframe_cnt));
		pxframe =  NULL;
	} else {
		phead = get_list_head(pfree_xmit_queue);

		plist = phead->next;

		pxframe = container_of(plist, struct xmit_frame, list);

		list_del_init(&pxframe->list);
		pxmitpriv->free_xmitframe_cnt--;
		RT_TRACE(_module_rtl871x_xmit_c_, _drv_info_, ("rtw_alloc_xmitframe23a():free_xmitframe_cnt =%d\n", pxmitpriv->free_xmitframe_cnt));
	}

	spin_unlock_bh(&pfree_xmit_queue->lock);

	rtw_init_xmitframe(pxframe);

	return pxframe;
}

struct xmit_frame *rtw_alloc_xmitframe23a_ext(struct xmit_priv *pxmitpriv)
{
	struct xmit_frame *pxframe = NULL;
	struct list_head *plist, *phead;
	struct rtw_queue *queue = &pxmitpriv->free_xframe_ext_queue;

	spin_lock_bh(&queue->lock);

	if (_rtw_queue_empty23a(queue) == true) {
		RT_TRACE(_module_rtl871x_xmit_c_, _drv_info_, ("rtw_alloc_xmitframe23a_ext:%d\n", pxmitpriv->free_xframe_ext_cnt));
		pxframe =  NULL;
	} else {
		phead = get_list_head(queue);
		plist = phead->next;
		pxframe = container_of(plist, struct xmit_frame, list);

		list_del_init(&pxframe->list);
		pxmitpriv->free_xframe_ext_cnt--;
		RT_TRACE(_module_rtl871x_xmit_c_, _drv_info_, ("rtw_alloc_xmitframe23a_ext():free_xmitframe_cnt =%d\n", pxmitpriv->free_xframe_ext_cnt));
	}

	spin_unlock_bh(&queue->lock);

	rtw_init_xmitframe(pxframe);

	return pxframe;
}

s32 rtw_free_xmitframe23a(struct xmit_priv *pxmitpriv, struct xmit_frame *pxmitframe)
{
	struct rtw_queue *queue = NULL;
	struct rtw_adapter *padapter = pxmitpriv->adapter;
	struct sk_buff *pndis_pkt = NULL;

	if (pxmitframe == NULL) {
		RT_TRACE(_module_rtl871x_xmit_c_, _drv_err_, ("====== rtw_free_xmitframe23a():pxmitframe == NULL!!!!!!!!!!\n"));
		goto exit;
	}

	if (pxmitframe->pkt) {
		pndis_pkt = pxmitframe->pkt;
		pxmitframe->pkt = NULL;
	}

	if (pxmitframe->ext_tag == 0)
		queue = &pxmitpriv->free_xmit_queue;
	else if (pxmitframe->ext_tag == 1)
		queue = &pxmitpriv->free_xframe_ext_queue;

	if (!queue)
		goto check_pkt_complete;
	spin_lock_bh(&queue->lock);

	list_del_init(&pxmitframe->list);
	list_add_tail(&pxmitframe->list, get_list_head(queue));
	if (pxmitframe->ext_tag == 0) {
		pxmitpriv->free_xmitframe_cnt++;
		RT_TRACE(_module_rtl871x_xmit_c_, _drv_debug_, ("rtw_free_xmitframe23a():free_xmitframe_cnt =%d\n", pxmitpriv->free_xmitframe_cnt));
	} else if (pxmitframe->ext_tag == 1) {
		pxmitpriv->free_xframe_ext_cnt++;
		RT_TRACE(_module_rtl871x_xmit_c_, _drv_debug_, ("rtw_free_xmitframe23a():free_xframe_ext_cnt =%d\n", pxmitpriv->free_xframe_ext_cnt));
	}

	spin_unlock_bh(&queue->lock);

check_pkt_complete:

	if (pndis_pkt)
		rtw_os_pkt_complete23a(padapter, pndis_pkt);

exit:

	return _SUCCESS;
}

void rtw_free_xmitframe_queue23a(struct xmit_priv *pxmitpriv,
				 struct rtw_queue *pframequeue)
{
	struct list_head *plist, *phead, *ptmp;
	struct	xmit_frame *pxmitframe;

	spin_lock_bh(&pframequeue->lock);

	phead = get_list_head(pframequeue);

	list_for_each_safe(plist, ptmp, phead) {
		pxmitframe = container_of(plist, struct xmit_frame, list);

		rtw_free_xmitframe23a(pxmitpriv, pxmitframe);
	}
	spin_unlock_bh(&pframequeue->lock);

}

s32 rtw_xmitframe_enqueue23a(struct rtw_adapter *padapter,
			     struct xmit_frame *pxmitframe)
{
	if (rtw_xmit23a_classifier(padapter, pxmitframe) == _FAIL) {
		RT_TRACE(_module_rtl871x_xmit_c_, _drv_err_,
			 ("rtw_xmitframe_enqueue23a: drop xmit pkt for "
			  "classifier fail\n"));
		return _FAIL;
	}

	return _SUCCESS;
}

static struct xmit_frame *
dequeue_one_xmitframe(struct xmit_priv *pxmitpriv, struct hw_xmit *phwxmit,
		      struct tx_servq *ptxservq, struct rtw_queue *pframe_queue)
{
	struct list_head *phead;
	struct xmit_frame *pxmitframe = NULL;

	phead = get_list_head(pframe_queue);

	if (!list_empty(phead)) {
		pxmitframe = list_first_entry(phead, struct xmit_frame, list);
		list_del_init(&pxmitframe->list);
		ptxservq->qcnt--;
	}
	return pxmitframe;
}

struct xmit_frame *
rtw_dequeue_xframe23a(struct xmit_priv *pxmitpriv, struct hw_xmit *phwxmit_i,
		   int entry)
{
	struct list_head *sta_plist, *sta_phead, *ptmp;
	struct hw_xmit *phwxmit;
	struct tx_servq *ptxservq = NULL;
	struct rtw_queue *pframe_queue = NULL;
	struct xmit_frame *pxmitframe = NULL;
	struct rtw_adapter *padapter = pxmitpriv->adapter;
	struct registry_priv	*pregpriv = &padapter->registrypriv;
	int i, inx[4];

	inx[0] = 0;
	inx[1] = 1;
	inx[2] = 2;
	inx[3] = 3;
	if (pregpriv->wifi_spec == 1) {
		int j;

		for (j = 0; j < 4; j++)
			inx[j] = pxmitpriv->wmm_para_seq[j];
	}

	spin_lock_bh(&pxmitpriv->lock);

	for (i = 0; i < entry; i++) {
		phwxmit = phwxmit_i + inx[i];

		sta_phead = get_list_head(phwxmit->sta_queue);

		list_for_each_safe(sta_plist, ptmp, sta_phead) {
			ptxservq = container_of(sta_plist, struct tx_servq,
						tx_pending);

			pframe_queue = &ptxservq->sta_pending;

			pxmitframe = dequeue_one_xmitframe(pxmitpriv, phwxmit, ptxservq, pframe_queue);

			if (pxmitframe) {
				phwxmit->accnt--;

				/* Remove sta node when there is no pending packets. */
				if (_rtw_queue_empty23a(pframe_queue)) /* must be done after get_next and before break */
					list_del_init(&ptxservq->tx_pending);
				goto exit;
			}
		}
	}
exit:
	spin_unlock_bh(&pxmitpriv->lock);
	return pxmitframe;
}

struct tx_servq *rtw_get_sta_pending23a(struct rtw_adapter *padapter, struct sta_info *psta, int up, u8 *ac)
{
	struct tx_servq *ptxservq = NULL;

	switch (up) {
	case 1:
	case 2:
		ptxservq = &psta->sta_xmitpriv.bk_q;
		*(ac) = 3;
		RT_TRACE(_module_rtl871x_xmit_c_, _drv_info_, ("rtw_get_sta_pending23a : BK\n"));
		break;
	case 4:
	case 5:
		ptxservq = &psta->sta_xmitpriv.vi_q;
		*(ac) = 1;
		RT_TRACE(_module_rtl871x_xmit_c_, _drv_info_, ("rtw_get_sta_pending23a : VI\n"));
		break;
	case 6:
	case 7:
		ptxservq = &psta->sta_xmitpriv.vo_q;
		*(ac) = 0;
		RT_TRACE(_module_rtl871x_xmit_c_, _drv_info_, ("rtw_get_sta_pending23a : VO\n"));
		break;
	case 0:
	case 3:
	default:
		ptxservq = &psta->sta_xmitpriv.be_q;
		*(ac) = 2;
		RT_TRACE(_module_rtl871x_xmit_c_, _drv_info_, ("rtw_get_sta_pending23a : BE\n"));
		break;
	}
	return ptxservq;
}

/*
 * Will enqueue pxmitframe to the proper queue,
 * and indicate it to xx_pending list.....
 */
s32 rtw_xmit23a_classifier(struct rtw_adapter *padapter,
			struct xmit_frame *pxmitframe)
{
	struct sta_info	*psta;
	struct tx_servq	*ptxservq;
	struct pkt_attrib	*pattrib = &pxmitframe->attrib;
	struct sta_priv	*pstapriv = &padapter->stapriv;
	struct hw_xmit	*phwxmits =  padapter->xmitpriv.hwxmits;
	u8	ac_index;
	int res = _SUCCESS;

	if (pattrib->psta) {
		psta = pattrib->psta;
	} else {
		DBG_8723A("%s, call rtw_get_stainfo23a()\n", __func__);
		psta = rtw_get_stainfo23a(pstapriv, pattrib->ra);
	}
	if (psta == NULL) {
		res = _FAIL;
		DBG_8723A("rtw_xmit23a_classifier: psta == NULL\n");
		RT_TRACE(_module_rtl871x_xmit_c_, _drv_err_,
			 ("rtw_xmit23a_classifier: psta == NULL\n"));
		goto exit;
	}
	if (!(psta->state & _FW_LINKED)) {
		DBG_8723A("%s, psta->state(0x%x) != _FW_LINKED\n", __func__,
			  psta->state);
		return _FAIL;
	}
	ptxservq = rtw_get_sta_pending23a(padapter, psta, pattrib->priority,
				       (u8 *)(&ac_index));

	if (list_empty(&ptxservq->tx_pending)) {
		list_add_tail(&ptxservq->tx_pending,
			      get_list_head(phwxmits[ac_index].sta_queue));
	}

	list_add_tail(&pxmitframe->list, get_list_head(&ptxservq->sta_pending));
	ptxservq->qcnt++;
	phwxmits[ac_index].accnt++;
exit:
	return res;
}

void rtw_alloc_hwxmits23a(struct rtw_adapter *padapter)
{
	struct hw_xmit *hwxmits;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	int size;

	pxmitpriv->hwxmit_entry = HWXMIT_ENTRY;

	size = sizeof(struct hw_xmit) * (pxmitpriv->hwxmit_entry + 1);
	pxmitpriv->hwxmits = kzalloc(size, GFP_KERNEL);

	hwxmits = pxmitpriv->hwxmits;

	if (pxmitpriv->hwxmit_entry == 5) {
		/* pxmitpriv->bmc_txqueue.head = 0; */
		/* hwxmits[0] .phwtxqueue = &pxmitpriv->bmc_txqueue; */
		hwxmits[0] .sta_queue = &pxmitpriv->bm_pending;

		/* pxmitpriv->vo_txqueue.head = 0; */
		/* hwxmits[1] .phwtxqueue = &pxmitpriv->vo_txqueue; */
		hwxmits[1] .sta_queue = &pxmitpriv->vo_pending;

		/* pxmitpriv->vi_txqueue.head = 0; */
		/* hwxmits[2] .phwtxqueue = &pxmitpriv->vi_txqueue; */
		hwxmits[2] .sta_queue = &pxmitpriv->vi_pending;

		/* pxmitpriv->bk_txqueue.head = 0; */
		/* hwxmits[3] .phwtxqueue = &pxmitpriv->bk_txqueue; */
		hwxmits[3] .sta_queue = &pxmitpriv->bk_pending;

		/* pxmitpriv->be_txqueue.head = 0; */
		/* hwxmits[4] .phwtxqueue = &pxmitpriv->be_txqueue; */
		hwxmits[4] .sta_queue = &pxmitpriv->be_pending;

	} else if (pxmitpriv->hwxmit_entry == 4) {

		/* pxmitpriv->vo_txqueue.head = 0; */
		/* hwxmits[0] .phwtxqueue = &pxmitpriv->vo_txqueue; */
		hwxmits[0] .sta_queue = &pxmitpriv->vo_pending;

		/* pxmitpriv->vi_txqueue.head = 0; */
		/* hwxmits[1] .phwtxqueue = &pxmitpriv->vi_txqueue; */
		hwxmits[1] .sta_queue = &pxmitpriv->vi_pending;

		/* pxmitpriv->be_txqueue.head = 0; */
		/* hwxmits[2] .phwtxqueue = &pxmitpriv->be_txqueue; */
		hwxmits[2] .sta_queue = &pxmitpriv->be_pending;

		/* pxmitpriv->bk_txqueue.head = 0; */
		/* hwxmits[3] .phwtxqueue = &pxmitpriv->bk_txqueue; */
		hwxmits[3] .sta_queue = &pxmitpriv->bk_pending;
	} else {

	}
}

void rtw_free_hwxmits23a(struct rtw_adapter *padapter)
{
	struct hw_xmit *hwxmits;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;

	hwxmits = pxmitpriv->hwxmits;
	kfree(hwxmits);
}

void rtw_init_hwxmits23a(struct hw_xmit *phwxmit, int entry)
{
	int i;

	for (i = 0; i < entry; i++, phwxmit++)
		phwxmit->accnt = 0;
}

u32 rtw_get_ff_hwaddr23a(struct xmit_frame *pxmitframe)
{
	u32 addr;
	struct pkt_attrib *pattrib = &pxmitframe->attrib;

	switch (pattrib->qsel) {
	case 0:
	case 3:
		addr = BE_QUEUE_INX;
		break;
	case 1:
	case 2:
		addr = BK_QUEUE_INX;
		break;
	case 4:
	case 5:
		addr = VI_QUEUE_INX;
		break;
	case 6:
	case 7:
		addr = VO_QUEUE_INX;
		break;
	case 0x10:
		addr = BCN_QUEUE_INX;
		break;
	case 0x11:/* BC/MC in PS (HIQ) */
		addr = HIGH_QUEUE_INX;
		break;
	case 0x12:
	default:
		addr = MGT_QUEUE_INX;
		break;
	}

	return addr;
}

static void do_queue_select(struct rtw_adapter	*padapter, struct pkt_attrib *pattrib)
{
	u8 qsel;

	qsel = pattrib->priority;
	RT_TRACE(_module_rtl871x_xmit_c_, _drv_info_,
		 ("### do_queue_select priority =%d , qsel = %d\n",
		  pattrib->priority, qsel));

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
int rtw_xmit23a(struct rtw_adapter *padapter, struct sk_buff *skb)
{
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct xmit_frame *pxmitframe = NULL;
	s32 res;

	pxmitframe = rtw_alloc_xmitframe23a(pxmitpriv);

	if (pxmitframe == NULL) {
		RT_TRACE(_module_xmit_osdep_c_, _drv_err_,
			 ("rtw_xmit23a: no more pxmitframe\n"));
		return -1;
	}

	res = update_attrib(padapter, skb, &pxmitframe->attrib);

	if (res == _FAIL) {
		RT_TRACE(_module_xmit_osdep_c_, _drv_err_, ("rtw_xmit23a: update attrib fail\n"));
		rtw_free_xmitframe23a(pxmitpriv, pxmitframe);
		return -1;
	}
	pxmitframe->pkt = skb;

	rtw_led_control(padapter, LED_CTL_TX);

	do_queue_select(padapter, &pxmitframe->attrib);

#ifdef CONFIG_8723AU_AP_MODE
	spin_lock_bh(&pxmitpriv->lock);
	if (xmitframe_enqueue_for_sleeping_sta23a(padapter, pxmitframe)) {
		spin_unlock_bh(&pxmitpriv->lock);
		return 1;
	}
	spin_unlock_bh(&pxmitpriv->lock);
#endif

	if (rtw_hal_xmit23a(padapter, pxmitframe) == false)
		return 1;

	return 0;
}

#if defined(CONFIG_8723AU_AP_MODE)

int xmitframe_enqueue_for_sleeping_sta23a(struct rtw_adapter *padapter, struct xmit_frame *pxmitframe)
{
	int ret = false;
	struct sta_info *psta = NULL;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct pkt_attrib *pattrib = &pxmitframe->attrib;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	int bmcst = is_multicast_ether_addr(pattrib->ra);

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE) == false)
	    return ret;

	if (pattrib->psta) {
		psta = pattrib->psta;
	} else {
		DBG_8723A("%s, call rtw_get_stainfo23a()\n", __func__);
		psta = rtw_get_stainfo23a(pstapriv, pattrib->ra);
	}

	if (psta == NULL) {
		DBG_8723A("%s, psta == NUL\n", __func__);
		return false;
	}

	if (!(psta->state & _FW_LINKED)) {
		DBG_8723A("%s, psta->state(0x%x) != _FW_LINKED\n", __func__,
			  psta->state);
		return false;
	}

	if (pattrib->triggered == 1) {
		if (bmcst)
			pattrib->qsel = 0x11;/* HIQ */
		return ret;
	}

	if (bmcst) {
		spin_lock_bh(&psta->sleep_q.lock);

		if (pstapriv->sta_dz_bitmap) {
			/* if anyone sta is in ps mode */
			list_del_init(&pxmitframe->list);

			/* spin_lock_bh(&psta->sleep_q.lock); */

			list_add_tail(&pxmitframe->list, get_list_head(&psta->sleep_q));

			psta->sleepq_len++;

			pstapriv->tim_bitmap |= BIT(0);/*  */
			pstapriv->sta_dz_bitmap |= BIT(0);

			/* DBG_8723A("enqueue, sq_len =%d, tim =%x\n", psta->sleepq_len, pstapriv->tim_bitmap); */

			update_beacon23a(padapter, _TIM_IE_, NULL, false);/* tx bc/mc packets after upate bcn */

			/* spin_unlock_bh(&psta->sleep_q.lock); */

			ret = true;

		}

		spin_unlock_bh(&psta->sleep_q.lock);

		return ret;

	}

	spin_lock_bh(&psta->sleep_q.lock);

	if (psta->state&WIFI_SLEEP_STATE) {
		u8 wmmps_ac = 0;

		if (pstapriv->sta_dz_bitmap & CHKBIT(psta->aid)) {
			list_del_init(&pxmitframe->list);

			/* spin_lock_bh(&psta->sleep_q.lock); */

			list_add_tail(&pxmitframe->list, get_list_head(&psta->sleep_q));

			psta->sleepq_len++;

			switch (pattrib->priority) {
			case 1:
			case 2:
				wmmps_ac = psta->uapsd_bk & BIT(0);
				break;
			case 4:
			case 5:
				wmmps_ac = psta->uapsd_vi & BIT(0);
				break;
			case 6:
			case 7:
				wmmps_ac = psta->uapsd_vo & BIT(0);
				break;
			case 0:
			case 3:
			default:
				wmmps_ac = psta->uapsd_be & BIT(0);
				break;
			}

			if (wmmps_ac)
				psta->sleepq_ac_len++;

			if (((psta->has_legacy_ac) && (!wmmps_ac)) ||
			   ((!psta->has_legacy_ac) && (wmmps_ac))) {
				pstapriv->tim_bitmap |= CHKBIT(psta->aid);

				if (psta->sleepq_len == 1) {
					/* upate BCN for TIM IE */
					update_beacon23a(padapter, _TIM_IE_, NULL, false);
				}
			}

			/* spin_unlock_bh(&psta->sleep_q.lock); */

			/* if (psta->sleepq_len > (NR_XMITFRAME>>3)) */
			/*  */
			/*	wakeup_sta_to_xmit23a(padapter, psta); */
			/*  */

			ret = true;

		}

	}

	spin_unlock_bh(&psta->sleep_q.lock);

	return ret;
}

static void
dequeue_xmitframes_to_sleeping_queue(struct rtw_adapter *padapter,
				     struct sta_info *psta,
				     struct rtw_queue *pframequeue)
{
	int ret;
	struct list_head *plist, *phead, *ptmp;
	u8	ac_index;
	struct tx_servq	*ptxservq;
	struct pkt_attrib	*pattrib;
	struct xmit_frame	*pxmitframe;
	struct hw_xmit *phwxmits =  padapter->xmitpriv.hwxmits;

	phead = get_list_head(pframequeue);

	list_for_each_safe(plist, ptmp, phead) {
		pxmitframe = container_of(plist, struct xmit_frame, list);

		ret = xmitframe_enqueue_for_sleeping_sta23a(padapter, pxmitframe);

		if (ret == true) {
			pattrib = &pxmitframe->attrib;

			ptxservq = rtw_get_sta_pending23a(padapter, psta, pattrib->priority, (u8 *)(&ac_index));

			ptxservq->qcnt--;
			phwxmits[ac_index].accnt--;
		} else {
			/* DBG_8723A("xmitframe_enqueue_for_sleeping_sta23a return false\n"); */
		}
	}
}

void stop_sta_xmit23a(struct rtw_adapter *padapter, struct sta_info *psta)
{
	struct sta_info *psta_bmc;
	struct sta_xmit_priv *pstaxmitpriv;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;

	pstaxmitpriv = &psta->sta_xmitpriv;

	/* for BC/MC Frames */
	psta_bmc = rtw_get_bcmc_stainfo23a(padapter);

	spin_lock_bh(&pxmitpriv->lock);

	psta->state |= WIFI_SLEEP_STATE;

	pstapriv->sta_dz_bitmap |= CHKBIT(psta->aid);

	dequeue_xmitframes_to_sleeping_queue(padapter, psta, &pstaxmitpriv->vo_q.sta_pending);
	list_del_init(&pstaxmitpriv->vo_q.tx_pending);

	dequeue_xmitframes_to_sleeping_queue(padapter, psta, &pstaxmitpriv->vi_q.sta_pending);
	list_del_init(&pstaxmitpriv->vi_q.tx_pending);

	dequeue_xmitframes_to_sleeping_queue(padapter, psta,
					     &pstaxmitpriv->be_q.sta_pending);
	list_del_init(&pstaxmitpriv->be_q.tx_pending);

	dequeue_xmitframes_to_sleeping_queue(padapter, psta,
					     &pstaxmitpriv->bk_q.sta_pending);
	list_del_init(&pstaxmitpriv->bk_q.tx_pending);

	/* for BC/MC Frames */
	pstaxmitpriv = &psta_bmc->sta_xmitpriv;
	dequeue_xmitframes_to_sleeping_queue(padapter, psta_bmc,
					     &pstaxmitpriv->be_q.sta_pending);
	list_del_init(&pstaxmitpriv->be_q.tx_pending);

	spin_unlock_bh(&pxmitpriv->lock);
}

void wakeup_sta_to_xmit23a(struct rtw_adapter *padapter, struct sta_info *psta)
{
	u8 update_mask = 0, wmmps_ac = 0;
	struct sta_info *psta_bmc;
	struct list_head *plist, *phead, *ptmp;
	struct xmit_frame *pxmitframe = NULL;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;

	spin_lock_bh(&pxmitpriv->lock);

	phead = get_list_head(&psta->sleep_q);

	list_for_each_safe(plist, ptmp, phead) {
		pxmitframe = container_of(plist, struct xmit_frame, list);
		list_del_init(&pxmitframe->list);

		switch (pxmitframe->attrib.priority) {
		case 1:
		case 2:
			wmmps_ac = psta->uapsd_bk & BIT(1);
			break;
		case 4:
		case 5:
			wmmps_ac = psta->uapsd_vi & BIT(1);
			break;
		case 6:
		case 7:
			wmmps_ac = psta->uapsd_vo & BIT(1);
			break;
		case 0:
		case 3:
		default:
			wmmps_ac = psta->uapsd_be & BIT(1);
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
		rtw_hal_xmit23aframe_enqueue(padapter, pxmitframe);
	}

	if (psta->sleepq_len == 0) {
		pstapriv->tim_bitmap &= ~CHKBIT(psta->aid);

		/* upate BCN for TIM IE */
		update_mask = BIT(0);

		if (psta->state&WIFI_SLEEP_STATE)
			psta->state ^= WIFI_SLEEP_STATE;

		if (psta->state & WIFI_STA_ALIVE_CHK_STATE) {
			psta->expire_to = pstapriv->expire_to;
			psta->state ^= WIFI_STA_ALIVE_CHK_STATE;
		}

		pstapriv->sta_dz_bitmap &= ~CHKBIT(psta->aid);
	}

	/* spin_unlock_bh(&psta->sleep_q.lock); */
	spin_unlock_bh(&pxmitpriv->lock);

	/* for BC/MC Frames */
	psta_bmc = rtw_get_bcmc_stainfo23a(padapter);
	if (!psta_bmc)
		return;

	if ((pstapriv->sta_dz_bitmap&0xfffe) == 0x0) {
		/* no any sta in ps mode */
		spin_lock_bh(&pxmitpriv->lock);

		phead = get_list_head(&psta_bmc->sleep_q);

		list_for_each_safe(plist, ptmp, phead) {
			pxmitframe = container_of(plist, struct xmit_frame,
						  list);

			list_del_init(&pxmitframe->list);

			psta_bmc->sleepq_len--;
			if (psta_bmc->sleepq_len > 0)
				pxmitframe->attrib.mdata = 1;
			else
				pxmitframe->attrib.mdata = 0;

			pxmitframe->attrib.triggered = 1;
			rtw_hal_xmit23aframe_enqueue(padapter, pxmitframe);
		}
		if (psta_bmc->sleepq_len == 0) {
			pstapriv->tim_bitmap &= ~BIT(0);
			pstapriv->sta_dz_bitmap &= ~BIT(0);

			/* upate BCN for TIM IE */
			/* update_BCNTIM(padapter); */
			update_mask |= BIT(1);
		}

		/* spin_unlock_bh(&psta_bmc->sleep_q.lock); */
		spin_unlock_bh(&pxmitpriv->lock);
	}

	if (update_mask)
		update_beacon23a(padapter, _TIM_IE_, NULL, false);
}

void xmit_delivery_enabled_frames23a(struct rtw_adapter *padapter,
				  struct sta_info *psta)
{
	u8 wmmps_ac = 0;
	struct list_head *plist, *phead, *ptmp;
	struct xmit_frame *pxmitframe;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;

	/* spin_lock_bh(&psta->sleep_q.lock); */
	spin_lock_bh(&pxmitpriv->lock);

	phead = get_list_head(&psta->sleep_q);

	list_for_each_safe(plist, ptmp, phead) {
		pxmitframe = container_of(plist, struct xmit_frame, list);

		switch (pxmitframe->attrib.priority) {
		case 1:
		case 2:
			wmmps_ac = psta->uapsd_bk & BIT(1);
			break;
		case 4:
		case 5:
			wmmps_ac = psta->uapsd_vi & BIT(1);
			break;
		case 6:
		case 7:
			wmmps_ac = psta->uapsd_vo & BIT(1);
			break;
		case 0:
		case 3:
		default:
			wmmps_ac = psta->uapsd_be & BIT(1);
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

		rtw_hal_xmit23aframe_enqueue(padapter, pxmitframe);

		if ((psta->sleepq_ac_len == 0) && (!psta->has_legacy_ac) &&
		    (wmmps_ac)) {
			pstapriv->tim_bitmap &= ~CHKBIT(psta->aid);

			/* upate BCN for TIM IE */
			update_beacon23a(padapter, _TIM_IE_, NULL, false);
		}
	}
	spin_unlock_bh(&pxmitpriv->lock);
}

#endif

void rtw_sctx_init23a(struct submit_ctx *sctx, int timeout_ms)
{
	sctx->timeout_ms = timeout_ms;
	init_completion(&sctx->done);
	sctx->status = RTW_SCTX_SUBMITTED;
}

int rtw_sctx_wait23a(struct submit_ctx *sctx)
{
	int ret = _FAIL;
	unsigned long expire;
	int status = 0;

	expire = sctx->timeout_ms ? msecs_to_jiffies(sctx->timeout_ms) :
		 MAX_SCHEDULE_TIMEOUT;
	if (!wait_for_completion_timeout(&sctx->done, expire)) {
		/* timeout, do something?? */
		status = RTW_SCTX_DONE_TIMEOUT;
		DBG_8723A("%s timeout\n", __func__);
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

void rtw23a_sctx_done_err(struct submit_ctx **sctx, int status)
{
	if (*sctx) {
		if (rtw_sctx_chk_waring_status(status))
			DBG_8723A("%s status:%d\n", __func__, status);
		(*sctx)->status = status;
		complete(&(*sctx)->done);
		*sctx = NULL;
	}
}

void rtw_sctx_done23a(struct submit_ctx **sctx)
{
	rtw23a_sctx_done_err(sctx, RTW_SCTX_DONE_SUCCESS);
}

int rtw_ack_tx_wait23a(struct xmit_priv *pxmitpriv, u32 timeout_ms)
{
	struct submit_ctx *pack_tx_ops = &pxmitpriv->ack_tx_ops;

	pack_tx_ops->timeout_ms = timeout_ms;
	pack_tx_ops->status = RTW_SCTX_SUBMITTED;

	return rtw_sctx_wait23a(pack_tx_ops);
}

void rtw_ack_tx_done23a(struct xmit_priv *pxmitpriv, int status)
{
	struct submit_ctx *pack_tx_ops = &pxmitpriv->ack_tx_ops;

	if (pxmitpriv->ack_tx)
		rtw23a_sctx_done_err(&pack_tx_ops, status);
	else
		DBG_8723A("%s ack_tx not set\n", __func__);
}
