// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#define _RTW_XMIT_C_

#include <drv_types.h>
#include <rtw_debug.h>

static u8 P802_1H_OUI[P80211_OUI_LEN] = { 0x00, 0x00, 0xf8 };
static u8 RFC1042_OUI[P80211_OUI_LEN] = { 0x00, 0x00, 0x00 };

static void _init_txservq(struct tx_servq *ptxservq)
{
	INIT_LIST_HEAD(&ptxservq->tx_pending);
	_rtw_init_queue(&ptxservq->sta_pending);
	ptxservq->qcnt = 0;
}

void _rtw_init_sta_xmit_priv(struct sta_xmit_priv *psta_xmitpriv)
{
	memset((unsigned char *)psta_xmitpriv, 0, sizeof(struct sta_xmit_priv));

	spin_lock_init(&psta_xmitpriv->lock);

	_init_txservq(&psta_xmitpriv->be_q);
	_init_txservq(&psta_xmitpriv->bk_q);
	_init_txservq(&psta_xmitpriv->vi_q);
	_init_txservq(&psta_xmitpriv->vo_q);
	INIT_LIST_HEAD(&psta_xmitpriv->legacy_dz);
	INIT_LIST_HEAD(&psta_xmitpriv->apsd);
}

s32 _rtw_init_xmit_priv(struct xmit_priv *pxmitpriv, struct adapter *padapter)
{
	int i;
	struct xmit_buf *pxmitbuf;
	struct xmit_frame *pxframe;
	signed int	res = _SUCCESS;

	spin_lock_init(&pxmitpriv->lock);
	spin_lock_init(&pxmitpriv->lock_sctx);
	init_completion(&pxmitpriv->xmit_comp);
	init_completion(&pxmitpriv->terminate_xmitthread_comp);

	/*
	 * Please insert all the queue initializaiton using _rtw_init_queue below
	 */

	pxmitpriv->adapter = padapter;

	_rtw_init_queue(&pxmitpriv->be_pending);
	_rtw_init_queue(&pxmitpriv->bk_pending);
	_rtw_init_queue(&pxmitpriv->vi_pending);
	_rtw_init_queue(&pxmitpriv->vo_pending);
	_rtw_init_queue(&pxmitpriv->bm_pending);

	_rtw_init_queue(&pxmitpriv->free_xmit_queue);

	/*
	 * Please allocate memory with the sz = (struct xmit_frame) * NR_XMITFRAME,
	 * and initialize free_xmit_frame below.
	 * Please also apply  free_txobj to link_up all the xmit_frames...
	 */

	pxmitpriv->pallocated_frame_buf = vzalloc(NR_XMITFRAME * sizeof(struct xmit_frame) + 4);

	if (!pxmitpriv->pallocated_frame_buf) {
		pxmitpriv->pxmit_frame_buf = NULL;
		res = _FAIL;
		goto exit;
	}
	pxmitpriv->pxmit_frame_buf = (u8 *)N_BYTE_ALIGMENT((SIZE_PTR)(pxmitpriv->pallocated_frame_buf), 4);

	pxframe = (struct xmit_frame *) pxmitpriv->pxmit_frame_buf;

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
	_rtw_init_queue(&pxmitpriv->free_xmitbuf_queue);
	_rtw_init_queue(&pxmitpriv->pending_xmitbuf_queue);

	pxmitpriv->pallocated_xmitbuf = vzalloc(NR_XMITBUFF * sizeof(struct xmit_buf) + 4);

	if (!pxmitpriv->pallocated_xmitbuf) {
		res = _FAIL;
		goto exit;
	}

	pxmitpriv->pxmitbuf = (u8 *)N_BYTE_ALIGMENT((SIZE_PTR)(pxmitpriv->pallocated_xmitbuf), 4);

	pxmitbuf = (struct xmit_buf *)pxmitpriv->pxmitbuf;

	for (i = 0; i < NR_XMITBUFF; i++) {
		INIT_LIST_HEAD(&pxmitbuf->list);

		pxmitbuf->priv_data = NULL;
		pxmitbuf->padapter = padapter;
		pxmitbuf->buf_tag = XMITBUF_DATA;

		/* Tx buf allocation may fail sometimes, so sleep and retry. */
		res = rtw_os_xmit_resource_alloc(padapter, pxmitbuf, (MAX_XMITBUF_SZ + XMITBUF_ALIGN_SZ), true);
		if (res == _FAIL) {
			msleep(10);
			res = rtw_os_xmit_resource_alloc(padapter, pxmitbuf, (MAX_XMITBUF_SZ + XMITBUF_ALIGN_SZ), true);
			if (res == _FAIL)
				goto exit;
		}

		pxmitbuf->phead = pxmitbuf->pbuf;
		pxmitbuf->pend = pxmitbuf->pbuf + MAX_XMITBUF_SZ;
		pxmitbuf->len = 0;
		pxmitbuf->pdata = pxmitbuf->ptail = pxmitbuf->phead;

		pxmitbuf->flags = XMIT_VO_QUEUE;

		list_add_tail(&pxmitbuf->list,
			      &pxmitpriv->free_xmitbuf_queue.queue);
		#ifdef DBG_XMIT_BUF
		pxmitbuf->no = i;
		#endif

		pxmitbuf++;
	}

	pxmitpriv->free_xmitbuf_cnt = NR_XMITBUFF;

	/* init xframe_ext queue,  the same count as extbuf  */
	_rtw_init_queue(&pxmitpriv->free_xframe_ext_queue);

	pxmitpriv->xframe_ext_alloc_addr = vzalloc(NR_XMIT_EXTBUFF * sizeof(struct xmit_frame) + 4);

	if (!pxmitpriv->xframe_ext_alloc_addr) {
		pxmitpriv->xframe_ext = NULL;
		res = _FAIL;
		goto exit;
	}
	pxmitpriv->xframe_ext = (u8 *)N_BYTE_ALIGMENT((SIZE_PTR)(pxmitpriv->xframe_ext_alloc_addr), 4);
	pxframe = (struct xmit_frame *)pxmitpriv->xframe_ext;

	for (i = 0; i < NR_XMIT_EXTBUFF; i++) {
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
	pxmitpriv->free_xframe_ext_cnt = NR_XMIT_EXTBUFF;

	/*  Init xmit extension buff */
	_rtw_init_queue(&pxmitpriv->free_xmit_extbuf_queue);

	pxmitpriv->pallocated_xmit_extbuf = vzalloc(NR_XMIT_EXTBUFF * sizeof(struct xmit_buf) + 4);

	if (!pxmitpriv->pallocated_xmit_extbuf) {
		res = _FAIL;
		goto exit;
	}

	pxmitpriv->pxmit_extbuf = (u8 *)N_BYTE_ALIGMENT((SIZE_PTR)(pxmitpriv->pallocated_xmit_extbuf), 4);

	pxmitbuf = (struct xmit_buf *)pxmitpriv->pxmit_extbuf;

	for (i = 0; i < NR_XMIT_EXTBUFF; i++) {
		INIT_LIST_HEAD(&pxmitbuf->list);

		pxmitbuf->priv_data = NULL;
		pxmitbuf->padapter = padapter;
		pxmitbuf->buf_tag = XMITBUF_MGNT;

		res = rtw_os_xmit_resource_alloc(padapter, pxmitbuf, MAX_XMIT_EXTBUF_SZ + XMITBUF_ALIGN_SZ, true);
		if (res == _FAIL) {
			res = _FAIL;
			goto exit;
		}

		pxmitbuf->phead = pxmitbuf->pbuf;
		pxmitbuf->pend = pxmitbuf->pbuf + MAX_XMIT_EXTBUF_SZ;
		pxmitbuf->len = 0;
		pxmitbuf->pdata = pxmitbuf->ptail = pxmitbuf->phead;

		list_add_tail(&pxmitbuf->list,
			      &pxmitpriv->free_xmit_extbuf_queue.queue);
		#ifdef DBG_XMIT_BUF_EXT
		pxmitbuf->no = i;
		#endif
		pxmitbuf++;
	}

	pxmitpriv->free_xmit_extbuf_cnt = NR_XMIT_EXTBUFF;

	for (i = 0; i < CMDBUF_MAX; i++) {
		pxmitbuf = &pxmitpriv->pcmd_xmitbuf[i];
		if (pxmitbuf) {
			INIT_LIST_HEAD(&pxmitbuf->list);

			pxmitbuf->priv_data = NULL;
			pxmitbuf->padapter = padapter;
			pxmitbuf->buf_tag = XMITBUF_CMD;

			res = rtw_os_xmit_resource_alloc(padapter, pxmitbuf, MAX_CMDBUF_SZ+XMITBUF_ALIGN_SZ, true);
			if (res == _FAIL) {
				res = _FAIL;
				goto exit;
			}

			pxmitbuf->phead = pxmitbuf->pbuf;
			pxmitbuf->pend = pxmitbuf->pbuf + MAX_CMDBUF_SZ;
			pxmitbuf->len = 0;
			pxmitbuf->pdata = pxmitbuf->ptail = pxmitbuf->phead;
			pxmitbuf->alloc_sz = MAX_CMDBUF_SZ+XMITBUF_ALIGN_SZ;
		}
	}

	res = rtw_alloc_hwxmits(padapter);
	if (res == _FAIL)
		goto exit;
	rtw_init_hwxmits(pxmitpriv->hwxmits, pxmitpriv->hwxmit_entry);

	for (i = 0; i < 4; i++)
		pxmitpriv->wmm_para_seq[i] = i;

	pxmitpriv->ack_tx = false;
	mutex_init(&pxmitpriv->ack_tx_mutex);
	rtw_sctx_init(&pxmitpriv->ack_tx_ops, 0);

	rtw_hal_init_xmit_priv(padapter);

exit:
	return res;
}

void _rtw_free_xmit_priv(struct xmit_priv *pxmitpriv)
{
	int i;
	struct adapter *padapter = pxmitpriv->adapter;
	struct xmit_frame	*pxmitframe = (struct xmit_frame *) pxmitpriv->pxmit_frame_buf;
	struct xmit_buf *pxmitbuf = (struct xmit_buf *)pxmitpriv->pxmitbuf;

	rtw_hal_free_xmit_priv(padapter);

	if (!pxmitpriv->pxmit_frame_buf)
		return;

	for (i = 0; i < NR_XMITFRAME; i++) {
		rtw_os_xmit_complete(padapter, pxmitframe);

		pxmitframe++;
	}

	for (i = 0; i < NR_XMITBUFF; i++) {
		rtw_os_xmit_resource_free(padapter, pxmitbuf, (MAX_XMITBUF_SZ + XMITBUF_ALIGN_SZ), true);

		pxmitbuf++;
	}

	vfree(pxmitpriv->pallocated_frame_buf);
	vfree(pxmitpriv->pallocated_xmitbuf);

	/* free xframe_ext queue,  the same count as extbuf  */
	pxmitframe = (struct xmit_frame *)pxmitpriv->xframe_ext;
	if (pxmitframe) {
		for (i = 0; i < NR_XMIT_EXTBUFF; i++) {
			rtw_os_xmit_complete(padapter, pxmitframe);
			pxmitframe++;
		}
	}

	vfree(pxmitpriv->xframe_ext_alloc_addr);

	/*  free xmit extension buff */
	pxmitbuf = (struct xmit_buf *)pxmitpriv->pxmit_extbuf;
	for (i = 0; i < NR_XMIT_EXTBUFF; i++) {
		rtw_os_xmit_resource_free(padapter, pxmitbuf, (MAX_XMIT_EXTBUF_SZ + XMITBUF_ALIGN_SZ), true);

		pxmitbuf++;
	}

	vfree(pxmitpriv->pallocated_xmit_extbuf);

	for (i = 0; i < CMDBUF_MAX; i++) {
		pxmitbuf = &pxmitpriv->pcmd_xmitbuf[i];
		if (pxmitbuf)
			rtw_os_xmit_resource_free(padapter, pxmitbuf, MAX_CMDBUF_SZ+XMITBUF_ALIGN_SZ, true);
	}

	rtw_free_hwxmits(padapter);

	mutex_destroy(&pxmitpriv->ack_tx_mutex);
}

u8 query_ra_short_GI(struct sta_info *psta)
{
	u8 sgi = false, sgi_20m = false, sgi_40m = false, sgi_80m = false;

	sgi_20m = psta->htpriv.sgi_20m;
	sgi_40m = psta->htpriv.sgi_40m;

	switch (psta->bw_mode) {
	case CHANNEL_WIDTH_80:
		sgi = sgi_80m;
		break;
	case CHANNEL_WIDTH_40:
		sgi = sgi_40m;
		break;
	case CHANNEL_WIDTH_20:
	default:
		sgi = sgi_20m;
		break;
	}

	return sgi;
}

static void update_attrib_vcs_info(struct adapter *padapter, struct xmit_frame *pxmitframe)
{
	u32 sz;
	struct pkt_attrib	*pattrib = &pxmitframe->attrib;
	/* struct sta_info *psta = pattrib->psta; */
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;

	if (pattrib->nr_frags != 1)
		sz = padapter->xmitpriv.frag_len;
	else /* no frag */
		sz = pattrib->last_txcmdsz;

	/*  (1) RTS_Threshold is compared to the MPDU, not MSDU. */
	/*  (2) If there are more than one frag in  this MSDU, only the first frag uses protection frame. */
	/* Other fragments are protected by previous fragment. */
	/* So we only need to check the length of first fragment. */
	if (pmlmeext->cur_wireless_mode < WIRELESS_11_24N  || padapter->registrypriv.wifi_spec) {
		if (sz > padapter->registrypriv.rts_thresh) {
			pattrib->vcs_mode = RTS_CTS;
		} else {
			if (pattrib->rtsen)
				pattrib->vcs_mode = RTS_CTS;
			else if (pattrib->cts2self)
				pattrib->vcs_mode = CTS_TO_SELF;
			else
				pattrib->vcs_mode = NONE_VCS;
		}
	} else {
		while (true) {
			/* IOT action */
			if ((pmlmeinfo->assoc_AP_vendor == HT_IOT_PEER_ATHEROS) && (pattrib->ampdu_en == true) &&
				(padapter->securitypriv.dot11PrivacyAlgrthm == _AES_)) {
				pattrib->vcs_mode = CTS_TO_SELF;
				break;
			}

			/* check ERP protection */
			if (pattrib->rtsen || pattrib->cts2self) {
				if (pattrib->rtsen)
					pattrib->vcs_mode = RTS_CTS;
				else if (pattrib->cts2self)
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
			if (pattrib->ampdu_en == true) {
				pattrib->vcs_mode = RTS_CTS;
				break;
			}

			pattrib->vcs_mode = NONE_VCS;
			break;
		}
	}

	/* for debug : force driver control vrtl_carrier_sense. */
	if (padapter->driver_vcs_en == 1)
		pattrib->vcs_mode = padapter->driver_vcs_type;
}

static void update_attrib_phy_info(struct adapter *padapter, struct pkt_attrib *pattrib, struct sta_info *psta)
{
	struct mlme_ext_priv *mlmeext = &padapter->mlmeextpriv;

	pattrib->rtsen = psta->rtsen;
	pattrib->cts2self = psta->cts2self;

	pattrib->mdata = 0;
	pattrib->eosp = 0;
	pattrib->triggered = 0;
	pattrib->ampdu_spacing = 0;

	/* qos_en, ht_en, init rate, , bw, ch_offset, sgi */
	pattrib->qos_en = psta->qos_option;

	pattrib->raid = psta->raid;

	if (mlmeext->cur_bwmode < psta->bw_mode)
		pattrib->bwmode = mlmeext->cur_bwmode;
	else
		pattrib->bwmode = psta->bw_mode;

	pattrib->sgi = query_ra_short_GI(psta);

	pattrib->ldpc = psta->ldpc;
	pattrib->stbc = psta->stbc;

	pattrib->ht_en = psta->htpriv.ht_option;
	pattrib->ch_offset = psta->htpriv.ch_offset;
	pattrib->ampdu_en = false;

	if (padapter->driver_ampdu_spacing != 0xFF) /* driver control AMPDU Density for peer sta's rx */
		pattrib->ampdu_spacing = padapter->driver_ampdu_spacing;
	else
		pattrib->ampdu_spacing = psta->htpriv.rx_ampdu_min_spacing;

	pattrib->retry_ctrl = false;
}

static s32 update_attrib_sec_info(struct adapter *padapter, struct pkt_attrib *pattrib, struct sta_info *psta)
{
	signed int res = _SUCCESS;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	signed int bmcast = IS_MCAST(pattrib->ra);

	memset(pattrib->dot118021x_UncstKey.skey,  0, 16);
	memset(pattrib->dot11tkiptxmickey.skey,  0, 16);
	pattrib->mac_id = psta->mac_id;

	if (psta->ieee8021x_blocked == true) {
		pattrib->encrypt = 0;

		if ((pattrib->ether_type != 0x888e) && (check_fwstate(pmlmepriv, WIFI_MP_STATE) == false)) {
			res = _FAIL;
			goto exit;
		}
	} else {
		GET_ENCRY_ALGO(psecuritypriv, psta, pattrib->encrypt, bmcast);

		switch (psecuritypriv->dot11AuthAlgrthm) {
		case dot11AuthAlgrthm_Open:
		case dot11AuthAlgrthm_Shared:
		case dot11AuthAlgrthm_Auto:
			pattrib->key_idx = (u8)psecuritypriv->dot11PrivacyKeyIndex;
			break;
		case dot11AuthAlgrthm_8021X:
			if (bmcast)
				pattrib->key_idx = (u8)psecuritypriv->dot118021XGrpKeyid;
			else
				pattrib->key_idx = 0;
			break;
		default:
			pattrib->key_idx = 0;
			break;
		}

		/* For WPS 1.0 WEP, driver should not encrypt EAPOL Packet for WPS handshake. */
		if (((pattrib->encrypt == _WEP40_) || (pattrib->encrypt == _WEP104_)) && (pattrib->ether_type == 0x888e))
			pattrib->encrypt = _NO_PRIVACY_;
	}

	switch (pattrib->encrypt) {
	case _WEP40_:
	case _WEP104_:
		pattrib->iv_len = 4;
		pattrib->icv_len = 4;
		WEP_IV(pattrib->iv, psta->dot11txpn, pattrib->key_idx);
		break;

	case _TKIP_:
		pattrib->iv_len = 8;
		pattrib->icv_len = 4;

		if (psecuritypriv->busetkipkey == _FAIL) {
			res = _FAIL;
			goto exit;
		}

		if (bmcast)
			TKIP_IV(pattrib->iv, psta->dot11txpn, pattrib->key_idx);
		else
			TKIP_IV(pattrib->iv, psta->dot11txpn, 0);

		memcpy(pattrib->dot11tkiptxmickey.skey, psta->dot11tkiptxmickey.skey, 16);

		break;

	case _AES_:

		pattrib->iv_len = 8;
		pattrib->icv_len = 8;

		if (bmcast)
			AES_IV(pattrib->iv, psta->dot11txpn, pattrib->key_idx);
		else
			AES_IV(pattrib->iv, psta->dot11txpn, 0);

		break;

	default:
		pattrib->iv_len = 0;
		pattrib->icv_len = 0;
		break;
	}

	if (pattrib->encrypt > 0)
		memcpy(pattrib->dot118021x_UncstKey.skey, psta->dot118021x_UncstKey.skey, 16);

	if (pattrib->encrypt &&
		((padapter->securitypriv.sw_encrypt) || (!psecuritypriv->hw_decrypted)))
		pattrib->bswenc = true;
	else
		pattrib->bswenc = false;

exit:

	return res;
}

u8 qos_acm(u8 acm_mask, u8 priority)
{
	switch (priority) {
	case 0:
	case 3:
		if (acm_mask & BIT(1))
			priority = 1;
		break;
	case 1:
	case 2:
		break;
	case 4:
	case 5:
		if (acm_mask & BIT(2))
			priority = 0;
		break;
	case 6:
	case 7:
		if (acm_mask & BIT(3))
			priority = 5;
		break;
	default:
		break;
	}

	return priority;
}

static void set_qos(struct pkt_file *ppktfile, struct pkt_attrib *pattrib)
{
	struct ethhdr etherhdr;
	struct iphdr ip_hdr;
	s32 UserPriority = 0;

	_rtw_open_pktfile(ppktfile->pkt, ppktfile);
	_rtw_pktfile_read(ppktfile, (unsigned char *)&etherhdr, ETH_HLEN);

	/*  get UserPriority from IP hdr */
	if (pattrib->ether_type == 0x0800) {
		_rtw_pktfile_read(ppktfile, (u8 *)&ip_hdr, sizeof(ip_hdr));
		UserPriority = ip_hdr.tos >> 5;
	}
	pattrib->priority = UserPriority;
	pattrib->hdrlen = WLAN_HDR_A3_QOS_LEN;
	pattrib->subtype = WIFI_QOS_DATA_TYPE;
}

static s32 update_attrib(struct adapter *padapter, struct sk_buff *pkt, struct pkt_attrib *pattrib)
{
	struct pkt_file pktfile;
	struct sta_info *psta = NULL;
	struct ethhdr etherhdr;

	signed int bmcast;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct qos_priv *pqospriv = &pmlmepriv->qospriv;
	signed int res = _SUCCESS;

	_rtw_open_pktfile(pkt, &pktfile);
	_rtw_pktfile_read(&pktfile, (u8 *)&etherhdr, ETH_HLEN);

	pattrib->ether_type = ntohs(etherhdr.h_proto);

	memcpy(pattrib->dst, &etherhdr.h_dest, ETH_ALEN);
	memcpy(pattrib->src, &etherhdr.h_source, ETH_ALEN);

	if ((check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == true) ||
		(check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == true)) {
		memcpy(pattrib->ra, pattrib->dst, ETH_ALEN);
		memcpy(pattrib->ta, pattrib->src, ETH_ALEN);
	} else if (check_fwstate(pmlmepriv, WIFI_STATION_STATE)) {
		memcpy(pattrib->ra, get_bssid(pmlmepriv), ETH_ALEN);
		memcpy(pattrib->ta, pattrib->src, ETH_ALEN);
	} else if (check_fwstate(pmlmepriv, WIFI_AP_STATE)) {
		memcpy(pattrib->ra, pattrib->dst, ETH_ALEN);
		memcpy(pattrib->ta, get_bssid(pmlmepriv), ETH_ALEN);
	}

	pattrib->pktlen = pktfile.pkt_len;

	if (ETH_P_IP == pattrib->ether_type) {
		/*  The following is for DHCP and ARP packet, we use cck1M to tx these packets and let LPS awake some time */
		/*  to prevent DHCP protocol fail */

		u8 tmp[24];

		_rtw_pktfile_read(&pktfile, &tmp[0], 24);

		pattrib->dhcp_pkt = 0;
		if (pktfile.pkt_len > 282) {/* MINIMUM_DHCP_PACKET_SIZE) { */
			if (ETH_P_IP == pattrib->ether_type) {/*  IP header */
				if (((tmp[21] == 68) && (tmp[23] == 67)) ||
					((tmp[21] == 67) && (tmp[23] == 68))) {
					/*  68 : UDP BOOTP client */
					/*  67 : UDP BOOTP server */
					pattrib->dhcp_pkt = 1;
				}
			}
		}

		/* for parsing ICMP pakcets */
		{
			struct iphdr *piphdr = (struct iphdr *)tmp;

			pattrib->icmp_pkt = 0;
			if (piphdr->protocol == 0x1) /*  protocol type in ip header 0x1 is ICMP */
				pattrib->icmp_pkt = 1;
		}
	} else if (0x888e == pattrib->ether_type) {
		netdev_dbg(padapter->pnetdev, "send eapol packet\n");
	}

	if ((pattrib->ether_type == 0x888e) || (pattrib->dhcp_pkt == 1))
		rtw_set_scan_deny(padapter, 3000);

	/*  If EAPOL , ARP , OR DHCP packet, driver must be in active mode. */
	if (pattrib->icmp_pkt == 1)
		rtw_lps_ctrl_wk_cmd(padapter, LPS_CTRL_LEAVE, 1);
	else if (pattrib->dhcp_pkt == 1)
		rtw_lps_ctrl_wk_cmd(padapter, LPS_CTRL_SPECIAL_PACKET, 1);

	bmcast = IS_MCAST(pattrib->ra);

	/*  get sta_info */
	if (bmcast) {
		psta = rtw_get_bcmc_stainfo(padapter);
	} else {
		psta = rtw_get_stainfo(pstapriv, pattrib->ra);
		if (!psta)	{ /*  if we cannot get psta => drop the pkt */
			res = _FAIL;
			goto exit;
		} else if ((check_fwstate(pmlmepriv, WIFI_AP_STATE) == true) && (!(psta->state & _FW_LINKED))) {
			res = _FAIL;
			goto exit;
		}
	}

	if (!psta) {
		/*  if we cannot get psta => drop the pkt */
		res = _FAIL;
		goto exit;
	}

	if (!(psta->state & _FW_LINKED))
		return _FAIL;

	/* TODO:_lock */
	if (update_attrib_sec_info(padapter, pattrib, psta) == _FAIL) {
		res = _FAIL;
		goto exit;
	}

	update_attrib_phy_info(padapter, pattrib, psta);

	pattrib->psta = psta;
	/* TODO:_unlock */

	pattrib->pctrl = 0;

	pattrib->ack_policy = 0;
	/*  get ether_hdr_len */
	pattrib->pkt_hdrlen = ETH_HLEN;/* pattrib->ether_type == 0x8100) ? (14 + 4): 14; vlan tag */

	pattrib->hdrlen = WLAN_HDR_A3_LEN;
	pattrib->subtype = WIFI_DATA_TYPE;
	pattrib->priority = 0;

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE|WIFI_ADHOC_STATE|WIFI_ADHOC_MASTER_STATE)) {
		if (pattrib->qos_en)
			set_qos(&pktfile, pattrib);
	} else {
		if (pqospriv->qos_option) {
			set_qos(&pktfile, pattrib);

			if (pmlmepriv->acm_mask != 0)
				pattrib->priority = qos_acm(pmlmepriv->acm_mask, pattrib->priority);
		}
	}

	/* pattrib->priority = 5; force to used VI queue, for testing */

exit:
	return res;
}

static s32 xmitframe_addmic(struct adapter *padapter, struct xmit_frame *pxmitframe)
{
	signed int			curfragnum, length;
	u8 *pframe, *payload, mic[8];
	struct mic_data micdata;
	struct pkt_attrib *pattrib = &pxmitframe->attrib;
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	u8 priority[4] = {0x0, 0x0, 0x0, 0x0};
	u8 hw_hdr_offset = 0;
	signed int bmcst = IS_MCAST(pattrib->ra);

	hw_hdr_offset = TXDESC_OFFSET;

	if (pattrib->encrypt == _TKIP_) {
		/* encode mic code */
		{
			u8 null_key[16] = {0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0};

			pframe = pxmitframe->buf_addr + hw_hdr_offset;

			if (bmcst) {
				if (!memcmp(psecuritypriv->dot118021XGrptxmickey[psecuritypriv->dot118021XGrpKeyid].skey, null_key, 16))
					return _FAIL;
				/* start to calculate the mic code */
				rtw_secmicsetkey(&micdata, psecuritypriv->dot118021XGrptxmickey[psecuritypriv->dot118021XGrpKeyid].skey);
			} else {
				if (!memcmp(&pattrib->dot11tkiptxmickey.skey[0], null_key, 16))
					return _FAIL;
				/* start to calculate the mic code */
				rtw_secmicsetkey(&micdata, &pattrib->dot11tkiptxmickey.skey[0]);
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
				payload = (u8 *)round_up((SIZE_PTR)(payload), 4);
				payload = payload+pattrib->hdrlen+pattrib->iv_len;

				if ((curfragnum+1) == pattrib->nr_frags) {
					length = pattrib->last_txcmdsz-pattrib->hdrlen-pattrib->iv_len-((pattrib->bswenc) ? pattrib->icv_len : 0);
					rtw_secmicappend(&micdata, payload, length);
					payload = payload+length;
				} else {
					length = pxmitpriv->frag_len-pattrib->hdrlen-pattrib->iv_len-((pattrib->bswenc) ? pattrib->icv_len : 0);
					rtw_secmicappend(&micdata, payload, length);
					payload = payload+length+pattrib->icv_len;
				}
			}
			rtw_secgetmic(&micdata, &mic[0]);
			/* add mic code  and add the mic code length in last_txcmdsz */

			memcpy(payload, &mic[0], 8);
			pattrib->last_txcmdsz += 8;
			}
	}
	return _SUCCESS;
}

static s32 xmitframe_swencrypt(struct adapter *padapter, struct xmit_frame *pxmitframe)
{
	struct	pkt_attrib	 *pattrib = &pxmitframe->attrib;

	if (pattrib->bswenc) {
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
	}

	return _SUCCESS;
}

s32 rtw_make_wlanhdr(struct adapter *padapter, u8 *hdr, struct pkt_attrib *pattrib)
{
	u16 *qc;

	struct ieee80211_hdr *pwlanhdr = (struct ieee80211_hdr *)hdr;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct qos_priv *pqospriv = &pmlmepriv->qospriv;
	u8 qos_option = false;
	signed int res = _SUCCESS;
	__le16 *fctrl = &pwlanhdr->frame_control;

	memset(hdr, 0, WLANHDR_OFFSET);

	SetFrameSubType(fctrl, pattrib->subtype);

	if (pattrib->subtype & WIFI_DATA_TYPE) {
		if (check_fwstate(pmlmepriv,  WIFI_STATION_STATE) == true) {
			/* to_ds = 1, fr_ds = 0; */

			{
				/*  1.Data transfer to AP */
				/*  2.Arp pkt will relayed by AP */
				SetToDs(fctrl);
				memcpy(pwlanhdr->addr1, get_bssid(pmlmepriv), ETH_ALEN);
				memcpy(pwlanhdr->addr2, pattrib->src, ETH_ALEN);
				memcpy(pwlanhdr->addr3, pattrib->dst, ETH_ALEN);
			}

			if (pqospriv->qos_option)
				qos_option = true;
		} else if (check_fwstate(pmlmepriv,  WIFI_AP_STATE) == true) {
			/* to_ds = 0, fr_ds = 1; */
			SetFrDs(fctrl);
			memcpy(pwlanhdr->addr1, pattrib->dst, ETH_ALEN);
			memcpy(pwlanhdr->addr2, get_bssid(pmlmepriv), ETH_ALEN);
			memcpy(pwlanhdr->addr3, pattrib->src, ETH_ALEN);

			if (pattrib->qos_en)
				qos_option = true;
		} else if ((check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == true) ||
		(check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == true)) {
			memcpy(pwlanhdr->addr1, pattrib->dst, ETH_ALEN);
			memcpy(pwlanhdr->addr2, pattrib->src, ETH_ALEN);
			memcpy(pwlanhdr->addr3, get_bssid(pmlmepriv), ETH_ALEN);

			if (pattrib->qos_en)
				qos_option = true;
		} else {
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
		{
			struct sta_info *psta;

			psta = rtw_get_stainfo(&padapter->stapriv, pattrib->ra);
			if (pattrib->psta != psta)
				return _FAIL;

			if (!psta)
				return _FAIL;

			if (!(psta->state & _FW_LINKED))
				return _FAIL;

			if (psta) {
				psta->sta_xmitpriv.txseq_tid[pattrib->priority]++;
				psta->sta_xmitpriv.txseq_tid[pattrib->priority] &= 0xFFF;
				pattrib->seqnum = psta->sta_xmitpriv.txseq_tid[pattrib->priority];

				SetSeqNum(hdr, pattrib->seqnum);

				/* check if enable ampdu */
				if (pattrib->ht_en && psta->htpriv.ampdu_enable)
					if (psta->htpriv.agg_enable_bitmap & BIT(pattrib->priority))
						pattrib->ampdu_en = true;

				/* re-check if enable ampdu by BA_starting_seqctrl */
				if (pattrib->ampdu_en == true) {
					u16 tx_seq;

					tx_seq = psta->BA_starting_seqctrl[pattrib->priority & 0x0f];

					/* check BA_starting_seqctrl */
					if (SN_LESS(pattrib->seqnum, tx_seq)) {
						pattrib->ampdu_en = false;/* AGG BK */
					} else if (SN_EQUAL(pattrib->seqnum, tx_seq)) {
						psta->BA_starting_seqctrl[pattrib->priority & 0x0f] = (tx_seq+1)&0xfff;

						pattrib->ampdu_en = true;/* AGG EN */
					} else {
						psta->BA_starting_seqctrl[pattrib->priority & 0x0f] = (pattrib->seqnum+1)&0xfff;
						pattrib->ampdu_en = true;/* AGG EN */
					}
				}
			}
		}
	} else {
	}

exit:
	return res;
}

s32 rtw_txframes_pending(struct adapter *padapter)
{
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;

	return ((!list_empty(&pxmitpriv->be_pending.queue)) ||
			 (!list_empty(&pxmitpriv->bk_pending.queue)) ||
			 (!list_empty(&pxmitpriv->vi_pending.queue)) ||
			 (!list_empty(&pxmitpriv->vo_pending.queue)));
}

/*
 * Calculate wlan 802.11 packet MAX size from pkt_attrib
 * This function doesn't consider fragment case
 */
u32 rtw_calculate_wlan_pkt_size_by_attribue(struct pkt_attrib *pattrib)
{
	u32 len = 0;

	len = pattrib->hdrlen + pattrib->iv_len; /*  WLAN Header and IV */
	len += SNAP_SIZE + sizeof(u16); /*  LLC */
	len += pattrib->pktlen;
	if (pattrib->encrypt == _TKIP_)
		len += 8; /*  MIC */
	len += ((pattrib->bswenc) ? pattrib->icv_len : 0); /*  ICV */

	return len;
}

/*
 * This sub-routine will perform all the following:
 * 1. remove 802.3 header.
 * 2. create wlan_header, based on the info in pxmitframe
 * 3. append sta's iv/ext-iv
 * 4. append LLC
 * 5. move frag chunk from pframe to pxmitframe->mem
 * 6. apply sw-encrypt, if necessary.
 */
s32 rtw_xmitframe_coalesce(struct adapter *padapter, struct sk_buff *pkt, struct xmit_frame *pxmitframe)
{
	struct pkt_file pktfile;

	s32 frg_inx, frg_len, mpdu_len, llc_sz, mem_sz;

	SIZE_PTR addr;

	u8 *pframe, *mem_start;
	u8 hw_hdr_offset;

	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;

	struct pkt_attrib	*pattrib = &pxmitframe->attrib;

	u8 *pbuf_start;

	s32 bmcst = IS_MCAST(pattrib->ra);
	s32 res = _SUCCESS;

	if (!pxmitframe->buf_addr)
		return _FAIL;

	pbuf_start = pxmitframe->buf_addr;

	hw_hdr_offset = TXDESC_OFFSET;
	mem_start = pbuf_start +	hw_hdr_offset;

	if (rtw_make_wlanhdr(padapter, mem_start, pattrib) == _FAIL) {
		res = _FAIL;
		goto exit;
	}

	_rtw_open_pktfile(pkt, &pktfile);
	_rtw_pktfile_read(&pktfile, NULL, pattrib->pkt_hdrlen);

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
			memcpy(pframe, pattrib->iv, pattrib->iv_len);

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
			/*  don't do fragment to broadcast/multicast packets */
			mem_sz = _rtw_pktfile_read(&pktfile, pframe, pattrib->pktlen);
		} else {
			mem_sz = _rtw_pktfile_read(&pktfile, pframe, mpdu_len);
		}

		pframe += mem_sz;

		if ((pattrib->icv_len > 0) && (pattrib->bswenc)) {
			memcpy(pframe, pattrib->icv, pattrib->icv_len);
			pframe += pattrib->icv_len;
		}

		frg_inx++;

		if (bmcst || (rtw_endofpktfile(&pktfile) == true)) {
			pattrib->nr_frags = frg_inx;

			pattrib->last_txcmdsz = pattrib->hdrlen + pattrib->iv_len + ((pattrib->nr_frags == 1) ? llc_sz:0) +
					((pattrib->bswenc) ? pattrib->icv_len : 0) + mem_sz;

			ClearMFrag(mem_start);

			break;
		}

		addr = (SIZE_PTR)(pframe);

		mem_start = (unsigned char *)round_up(addr, 4) + hw_hdr_offset;
		memcpy(mem_start, pbuf_start + hw_hdr_offset, pattrib->hdrlen);
	}

	if (xmitframe_addmic(padapter, pxmitframe) == _FAIL) {
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

/* broadcast or multicast management pkt use BIP, unicast management pkt use CCMP encryption */
s32 rtw_mgmt_xmitframe_coalesce(struct adapter *padapter, struct sk_buff *pkt, struct xmit_frame *pxmitframe)
{
	u8 *pframe, *mem_start = NULL, *tmp_buf = NULL;
	u8 subtype;
	struct sta_info *psta = NULL;
	struct pkt_attrib *pattrib = &pxmitframe->attrib;
	s32 bmcst = IS_MCAST(pattrib->ra);
	u8 *BIP_AAD = NULL;
	u8 *MGMT_body = NULL;

	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct ieee80211_hdr	*pwlanhdr;
	u8 MME[_MME_IE_LENGTH_];
	u32 ori_len;

	mem_start = pframe = (u8 *)(pxmitframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct ieee80211_hdr *)pframe;

	ori_len = BIP_AAD_SIZE+pattrib->pktlen;
	tmp_buf = BIP_AAD = rtw_zmalloc(ori_len);
	subtype = GetFrameSubType(pframe); /* bit(7)~bit(2) */

	if (!BIP_AAD)
		return _FAIL;

	spin_lock_bh(&padapter->security_key_mutex);

	/* only support station mode */
	if (!check_fwstate(pmlmepriv, WIFI_STATION_STATE) || !check_fwstate(pmlmepriv, _FW_LINKED))
		goto xmitframe_coalesce_success;

	/* IGTK key is not install, it may not support 802.11w */
	if (!padapter->securitypriv.binstallBIPkey)
		goto xmitframe_coalesce_success;

	/* station mode doesn't need TX BIP, just ready the code */
	if (bmcst) {
		int frame_body_len;
		u8 mic[16];

		memset(MME, 0, 18);

		/* other types doesn't need the BIP */
		if (GetFrameSubType(pframe) != WIFI_DEAUTH && GetFrameSubType(pframe) != WIFI_DISASSOC)
			goto xmitframe_coalesce_fail;

		MGMT_body = pframe + sizeof(struct ieee80211_hdr_3addr);
		pframe += pattrib->pktlen;

		/* octent 0 and 1 is key index , BIP keyid is 4 or 5, LSB only need octent 0 */
		MME[0] = padapter->securitypriv.dot11wBIPKeyid;
		/* copy packet number */
		memcpy(&MME[2], &pmlmeext->mgnt_80211w_IPN, 6);
		/* increase the packet number */
		pmlmeext->mgnt_80211w_IPN++;

		/* add MME IE with MIC all zero, MME string doesn't include element id and length */
		pframe = rtw_set_ie(pframe, WLAN_EID_MMIE, 16,
				    MME, &pattrib->pktlen);
		pattrib->last_txcmdsz = pattrib->pktlen;
		/*  total frame length - header length */
		frame_body_len = pattrib->pktlen - sizeof(struct ieee80211_hdr_3addr);

		/* conscruct AAD, copy frame control field */
		memcpy(BIP_AAD, &pwlanhdr->frame_control, 2);
		ClearRetry(BIP_AAD);
		ClearPwrMgt(BIP_AAD);
		ClearMData(BIP_AAD);
		/* conscruct AAD, copy address 1 to address 3 */
		memcpy(BIP_AAD+2, pwlanhdr->addr1, 18);
		/* copy management fram body */
		memcpy(BIP_AAD+BIP_AAD_SIZE, MGMT_body, frame_body_len);
		/* calculate mic */
		if (omac1_aes_128(padapter->securitypriv.dot11wBIPKey[padapter->securitypriv.dot11wBIPKeyid].skey
			, BIP_AAD, BIP_AAD_SIZE+frame_body_len, mic))
			goto xmitframe_coalesce_fail;

		/* copy right BIP mic value, total is 128bits, we use the 0~63 bits */
		memcpy(pframe-8, mic, 8);
	} else { /* unicast mgmt frame TX */
		/* start to encrypt mgmt frame */
		if (subtype == WIFI_DEAUTH || subtype == WIFI_DISASSOC ||
			subtype == WIFI_REASSOCREQ || subtype == WIFI_ACTION) {
			if (pattrib->psta)
				psta = pattrib->psta;
			else
				psta = rtw_get_stainfo(&padapter->stapriv, pattrib->ra);

			if (!psta)
				goto xmitframe_coalesce_fail;

			if (!(psta->state & _FW_LINKED) || !pxmitframe->buf_addr)
				goto xmitframe_coalesce_fail;

			/* according 802.11-2012 standard, these five types are not robust types */
			if (subtype == WIFI_ACTION &&
			(pframe[WLAN_HDR_A3_LEN] == RTW_WLAN_CATEGORY_PUBLIC ||
			pframe[WLAN_HDR_A3_LEN] == RTW_WLAN_CATEGORY_HT ||
			pframe[WLAN_HDR_A3_LEN] == RTW_WLAN_CATEGORY_UNPROTECTED_WNM ||
			pframe[WLAN_HDR_A3_LEN] == RTW_WLAN_CATEGORY_SELF_PROTECTED  ||
			pframe[WLAN_HDR_A3_LEN] == RTW_WLAN_CATEGORY_P2P))
				goto xmitframe_coalesce_fail;
			/* before encrypt dump the management packet content */
			if (pattrib->encrypt > 0)
				memcpy(pattrib->dot118021x_UncstKey.skey, psta->dot118021x_UncstKey.skey, 16);
			/* bakeup original management packet */
			memcpy(tmp_buf, pframe, pattrib->pktlen);
			/* move to data portion */
			pframe += pattrib->hdrlen;

			/* 802.11w unicast management packet must be _AES_ */
			pattrib->iv_len = 8;
			/* it's MIC of AES */
			pattrib->icv_len = 8;

			switch (pattrib->encrypt) {
			case _AES_:
					/* set AES IV header */
					AES_IV(pattrib->iv, psta->dot11wtxpn, 0);
				break;
			default:
				goto xmitframe_coalesce_fail;
			}
			/* insert iv header into management frame */
			memcpy(pframe, pattrib->iv, pattrib->iv_len);
			pframe += pattrib->iv_len;
			/* copy mgmt data portion after CCMP header */
			memcpy(pframe, tmp_buf+pattrib->hdrlen, pattrib->pktlen-pattrib->hdrlen);
			/* move pframe to end of mgmt pkt */
			pframe += pattrib->pktlen-pattrib->hdrlen;
			/* add 8 bytes CCMP IV header to length */
			pattrib->pktlen += pattrib->iv_len;
			if ((pattrib->icv_len > 0) && (pattrib->bswenc)) {
				memcpy(pframe, pattrib->icv, pattrib->icv_len);
				pframe += pattrib->icv_len;
			}
			/* add 8 bytes MIC */
			pattrib->pktlen += pattrib->icv_len;
			/* set final tx command size */
			pattrib->last_txcmdsz = pattrib->pktlen;

			/* set protected bit must be beofre SW encrypt */
			SetPrivacy(mem_start);
			/* software encrypt */
			xmitframe_swencrypt(padapter, pxmitframe);
		}
	}

xmitframe_coalesce_success:
	spin_unlock_bh(&padapter->security_key_mutex);
	kfree(BIP_AAD);
	return _SUCCESS;

xmitframe_coalesce_fail:
	spin_unlock_bh(&padapter->security_key_mutex);
	kfree(BIP_AAD);
	return _FAIL;
}

/* Logical Link Control(LLC) SubNetwork Attachment Point(SNAP) header
 * IEEE LLC/SNAP header contains 8 octets
 * First 3 octets comprise the LLC portion
 * SNAP portion, 5 octets, is divided into two fields:
 *Organizationally Unique Identifier(OUI), 3 octets,
 *type, defined by that organization, 2 octets.
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

void rtw_update_protection(struct adapter *padapter, u8 *ie, uint ie_len)
{
	uint	protection;
	u8 *perp;
	signed int	 erp_len;
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
		perp = rtw_get_ie(ie, WLAN_EID_ERP_INFO, &erp_len, ie_len);
		if (!perp) {
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

void rtw_count_tx_stats(struct adapter *padapter, struct xmit_frame *pxmitframe, int sz)
{
	struct sta_info *psta = NULL;
	struct stainfo_stats *pstats = NULL;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	u8 pkt_num = 1;

	if ((pxmitframe->frame_tag&0x0f) == DATA_FRAMETAG) {
		pkt_num = pxmitframe->agg_num;

		pmlmepriv->LinkDetectInfo.NumTxOkInPeriod += pkt_num;

		pxmitpriv->tx_pkts += pkt_num;

		pxmitpriv->tx_bytes += sz;

		psta = pxmitframe->attrib.psta;
		if (psta) {
			pstats = &psta->sta_stats;

			pstats->tx_pkts += pkt_num;

			pstats->tx_bytes += sz;
		}
	}
}

static struct xmit_buf *__rtw_alloc_cmd_xmitbuf(struct xmit_priv *pxmitpriv,
		enum cmdbuf_type buf_type)
{
	struct xmit_buf *pxmitbuf =  NULL;

	pxmitbuf = &pxmitpriv->pcmd_xmitbuf[buf_type];
	if (pxmitbuf) {
		pxmitbuf->priv_data = NULL;

		pxmitbuf->len = 0;
		pxmitbuf->pdata = pxmitbuf->ptail = pxmitbuf->phead;
		pxmitbuf->agg_num = 0;
		pxmitbuf->pg_num = 0;

		if (pxmitbuf->sctx)
			rtw_sctx_done_err(&pxmitbuf->sctx, RTW_SCTX_DONE_BUF_ALLOC);
	}

	return pxmitbuf;
}

struct xmit_frame *__rtw_alloc_cmdxmitframe(struct xmit_priv *pxmitpriv,
		enum cmdbuf_type buf_type)
{
	struct xmit_frame		*pcmdframe;
	struct xmit_buf		*pxmitbuf;

	pcmdframe = rtw_alloc_xmitframe(pxmitpriv);
	if (!pcmdframe)
		return NULL;

	pxmitbuf = __rtw_alloc_cmd_xmitbuf(pxmitpriv, buf_type);
	if (!pxmitbuf) {
		rtw_free_xmitframe(pxmitpriv, pcmdframe);
		return NULL;
	}

	pcmdframe->frame_tag = MGNT_FRAMETAG;

	pcmdframe->pxmitbuf = pxmitbuf;

	pcmdframe->buf_addr = pxmitbuf->pbuf;

	pxmitbuf->priv_data = pcmdframe;

	return pcmdframe;
}

struct xmit_buf *rtw_alloc_xmitbuf_ext(struct xmit_priv *pxmitpriv)
{
	unsigned long irqL;
	struct xmit_buf *pxmitbuf =  NULL;
	struct list_head *plist, *phead;
	struct __queue *pfree_queue = &pxmitpriv->free_xmit_extbuf_queue;

	spin_lock_irqsave(&pfree_queue->lock, irqL);

	if (list_empty(&pfree_queue->queue)) {
		pxmitbuf = NULL;
	} else {
		phead = get_list_head(pfree_queue);

		plist = get_next(phead);

		pxmitbuf = container_of(plist, struct xmit_buf, list);

		list_del_init(&pxmitbuf->list);
	}

	if (pxmitbuf) {
		pxmitpriv->free_xmit_extbuf_cnt--;

		pxmitbuf->priv_data = NULL;

		pxmitbuf->len = 0;
		pxmitbuf->pdata = pxmitbuf->ptail = pxmitbuf->phead;
		pxmitbuf->agg_num = 1;

		if (pxmitbuf->sctx)
			rtw_sctx_done_err(&pxmitbuf->sctx, RTW_SCTX_DONE_BUF_ALLOC);
	}

	spin_unlock_irqrestore(&pfree_queue->lock, irqL);

	return pxmitbuf;
}

s32 rtw_free_xmitbuf_ext(struct xmit_priv *pxmitpriv, struct xmit_buf *pxmitbuf)
{
	unsigned long irqL;
	struct __queue *pfree_queue = &pxmitpriv->free_xmit_extbuf_queue;

	if (!pxmitbuf)
		return _FAIL;

	spin_lock_irqsave(&pfree_queue->lock, irqL);

	list_del_init(&pxmitbuf->list);

	list_add_tail(&pxmitbuf->list, get_list_head(pfree_queue));
	pxmitpriv->free_xmit_extbuf_cnt++;

	spin_unlock_irqrestore(&pfree_queue->lock, irqL);

	return _SUCCESS;
}

struct xmit_buf *rtw_alloc_xmitbuf(struct xmit_priv *pxmitpriv)
{
	unsigned long irqL;
	struct xmit_buf *pxmitbuf =  NULL;
	struct list_head *plist, *phead;
	struct __queue *pfree_xmitbuf_queue = &pxmitpriv->free_xmitbuf_queue;

	spin_lock_irqsave(&pfree_xmitbuf_queue->lock, irqL);

	if (list_empty(&pfree_xmitbuf_queue->queue)) {
		pxmitbuf = NULL;
	} else {
		phead = get_list_head(pfree_xmitbuf_queue);

		plist = get_next(phead);

		pxmitbuf = container_of(plist, struct xmit_buf, list);

		list_del_init(&pxmitbuf->list);
	}

	if (pxmitbuf) {
		pxmitpriv->free_xmitbuf_cnt--;

		pxmitbuf->priv_data = NULL;

		pxmitbuf->len = 0;
		pxmitbuf->pdata = pxmitbuf->ptail = pxmitbuf->phead;
		pxmitbuf->agg_num = 0;
		pxmitbuf->pg_num = 0;

		if (pxmitbuf->sctx)
			rtw_sctx_done_err(&pxmitbuf->sctx, RTW_SCTX_DONE_BUF_ALLOC);
	}

	spin_unlock_irqrestore(&pfree_xmitbuf_queue->lock, irqL);

	return pxmitbuf;
}

s32 rtw_free_xmitbuf(struct xmit_priv *pxmitpriv, struct xmit_buf *pxmitbuf)
{
	unsigned long irqL;
	struct __queue *pfree_xmitbuf_queue = &pxmitpriv->free_xmitbuf_queue;

	if (!pxmitbuf)
		return _FAIL;

	if (pxmitbuf->sctx)
		rtw_sctx_done_err(&pxmitbuf->sctx, RTW_SCTX_DONE_BUF_FREE);

	if (pxmitbuf->buf_tag == XMITBUF_CMD) {
	} else if (pxmitbuf->buf_tag == XMITBUF_MGNT) {
		rtw_free_xmitbuf_ext(pxmitpriv, pxmitbuf);
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
	if (pxframe) { /* default value setting */
		pxframe->buf_addr = NULL;
		pxframe->pxmitbuf = NULL;

		memset(&pxframe->attrib, 0, sizeof(struct pkt_attrib));

		pxframe->frame_tag = DATA_FRAMETAG;

		pxframe->pg_num = 1;
		pxframe->agg_num = 1;
		pxframe->ack_report = 0;
	}
}

/*
 * Calling context:
 * 1. OS_TXENTRY
 * 2. RXENTRY (rx_thread or RX_ISR/RX_CallBack)
 *
 * If we turn on USE_RXTHREAD, then, no need for critical section.
 * Otherwise, we must use _enter/_exit critical to protect free_xmit_queue...
 *
 * Must be very, very cautious...
 */
struct xmit_frame *rtw_alloc_xmitframe(struct xmit_priv *pxmitpriv)/* _queue *pfree_xmit_queue) */
{
	/*
	 *	Please remember to use all the osdep_service api,
	 *	and lock/unlock or _enter/_exit critical to protect
	 *	pfree_xmit_queue
	 */

	struct xmit_frame *pxframe = NULL;
	struct list_head *plist, *phead;
	struct __queue *pfree_xmit_queue = &pxmitpriv->free_xmit_queue;

	spin_lock_bh(&pfree_xmit_queue->lock);

	if (list_empty(&pfree_xmit_queue->queue)) {
		pxframe =  NULL;
	} else {
		phead = get_list_head(pfree_xmit_queue);

		plist = get_next(phead);

		pxframe = container_of(plist, struct xmit_frame, list);

		list_del_init(&pxframe->list);
		pxmitpriv->free_xmitframe_cnt--;
	}

	spin_unlock_bh(&pfree_xmit_queue->lock);

	rtw_init_xmitframe(pxframe);
	return pxframe;
}

struct xmit_frame *rtw_alloc_xmitframe_ext(struct xmit_priv *pxmitpriv)
{
	struct xmit_frame *pxframe = NULL;
	struct list_head *plist, *phead;
	struct __queue *queue = &pxmitpriv->free_xframe_ext_queue;

	spin_lock_bh(&queue->lock);

	if (list_empty(&queue->queue)) {
		pxframe =  NULL;
	} else {
		phead = get_list_head(queue);
		plist = get_next(phead);
		pxframe = container_of(plist, struct xmit_frame, list);

		list_del_init(&pxframe->list);
		pxmitpriv->free_xframe_ext_cnt--;
	}

	spin_unlock_bh(&queue->lock);

	rtw_init_xmitframe(pxframe);

	return pxframe;
}

struct xmit_frame *rtw_alloc_xmitframe_once(struct xmit_priv *pxmitpriv)
{
	struct xmit_frame *pxframe = NULL;
	u8 *alloc_addr;

	alloc_addr = rtw_zmalloc(sizeof(struct xmit_frame) + 4);

	if (!alloc_addr)
		goto exit;

	pxframe = (struct xmit_frame *)N_BYTE_ALIGMENT((SIZE_PTR)(alloc_addr), 4);
	pxframe->alloc_addr = alloc_addr;

	pxframe->padapter = pxmitpriv->adapter;
	pxframe->frame_tag = NULL_FRAMETAG;

	pxframe->pkt = NULL;

	pxframe->buf_addr = NULL;
	pxframe->pxmitbuf = NULL;

	rtw_init_xmitframe(pxframe);

exit:
	return pxframe;
}

s32 rtw_free_xmitframe(struct xmit_priv *pxmitpriv, struct xmit_frame *pxmitframe)
{
	struct __queue *queue = NULL;
	struct adapter *padapter = pxmitpriv->adapter;
	struct sk_buff *pndis_pkt = NULL;

	if (!pxmitframe)
		goto exit;

	if (pxmitframe->pkt) {
		pndis_pkt = pxmitframe->pkt;
		pxmitframe->pkt = NULL;
	}

	if (pxmitframe->alloc_addr) {
		kfree(pxmitframe->alloc_addr);
		goto check_pkt_complete;
	}

	if (pxmitframe->ext_tag == 0)
		queue = &pxmitpriv->free_xmit_queue;
	else if (pxmitframe->ext_tag == 1)
		queue = &pxmitpriv->free_xframe_ext_queue;
	else {
	}

	spin_lock_bh(&queue->lock);

	list_del_init(&pxmitframe->list);
	list_add_tail(&pxmitframe->list, get_list_head(queue));
	if (pxmitframe->ext_tag == 0)
		pxmitpriv->free_xmitframe_cnt++;
	else if (pxmitframe->ext_tag == 1)
		pxmitpriv->free_xframe_ext_cnt++;

	spin_unlock_bh(&queue->lock);

check_pkt_complete:

	if (pndis_pkt)
		rtw_os_pkt_complete(padapter, pndis_pkt);

exit:
	return _SUCCESS;
}

void rtw_free_xmitframe_queue(struct xmit_priv *pxmitpriv, struct __queue *pframequeue)
{
	struct list_head	*plist, *phead;
	struct	xmit_frame	*pxmitframe;

	spin_lock_bh(&pframequeue->lock);

	phead = get_list_head(pframequeue);
	plist = get_next(phead);

	while (phead != plist) {
		pxmitframe = container_of(plist, struct xmit_frame, list);

		plist = get_next(plist);

		rtw_free_xmitframe(pxmitpriv, pxmitframe);
	}
	spin_unlock_bh(&pframequeue->lock);
}

s32 rtw_xmitframe_enqueue(struct adapter *padapter, struct xmit_frame *pxmitframe)
{
	if (rtw_xmit_classifier(padapter, pxmitframe) == _FAIL)
		return _FAIL;

	return _SUCCESS;
}

struct tx_servq *rtw_get_sta_pending(struct adapter *padapter, struct sta_info *psta, signed int up, u8 *ac)
{
	struct tx_servq *ptxservq = NULL;

	switch (up) {
	case 1:
	case 2:
		ptxservq = &psta->sta_xmitpriv.bk_q;
		*(ac) = 3;
		break;

	case 4:
	case 5:
		ptxservq = &psta->sta_xmitpriv.vi_q;
		*(ac) = 1;
		break;

	case 6:
	case 7:
		ptxservq = &psta->sta_xmitpriv.vo_q;
		*(ac) = 0;
		break;

	case 0:
	case 3:
	default:
		ptxservq = &psta->sta_xmitpriv.be_q;
		*(ac) = 2;
	break;
	}

	return ptxservq;
}

/*
 * Will enqueue pxmitframe to the proper queue,
 * and indicate it to xx_pending list.....
 */
s32 rtw_xmit_classifier(struct adapter *padapter, struct xmit_frame *pxmitframe)
{
	u8 ac_index;
	struct sta_info *psta;
	struct tx_servq	*ptxservq;
	struct pkt_attrib	*pattrib = &pxmitframe->attrib;
	struct hw_xmit	*phwxmits =  padapter->xmitpriv.hwxmits;
	signed int res = _SUCCESS;

	psta = rtw_get_stainfo(&padapter->stapriv, pattrib->ra);
	if (pattrib->psta != psta)
		return _FAIL;

	if (!psta) {
		res = _FAIL;
		goto exit;
	}

	if (!(psta->state & _FW_LINKED))
		return _FAIL;

	ptxservq = rtw_get_sta_pending(padapter, psta, pattrib->priority, (u8 *)(&ac_index));

	if (list_empty(&ptxservq->tx_pending))
		list_add_tail(&ptxservq->tx_pending, get_list_head(phwxmits[ac_index].sta_queue));

	list_add_tail(&pxmitframe->list, get_list_head(&ptxservq->sta_pending));
	ptxservq->qcnt++;
	phwxmits[ac_index].accnt++;

exit:

	return res;
}

s32 rtw_alloc_hwxmits(struct adapter *padapter)
{
	struct hw_xmit *hwxmits;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;

	pxmitpriv->hwxmit_entry = HWXMIT_ENTRY;

	pxmitpriv->hwxmits = NULL;

	pxmitpriv->hwxmits = rtw_zmalloc(sizeof(struct hw_xmit) * pxmitpriv->hwxmit_entry);

	if (!pxmitpriv->hwxmits)
		return _FAIL;

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
	} else {
	}

	return _SUCCESS;
}

void rtw_free_hwxmits(struct adapter *padapter)
{
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;

	kfree(pxmitpriv->hwxmits);
}

void rtw_init_hwxmits(struct hw_xmit *phwxmit, signed int entry)
{
	signed int i;

	for (i = 0; i < entry; i++, phwxmit++)
		phwxmit->accnt = 0;
}

u32 rtw_get_ff_hwaddr(struct xmit_frame *pxmitframe)
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

static void do_queue_select(struct adapter	*padapter, struct pkt_attrib *pattrib)
{
	u8 qsel;

	qsel = pattrib->priority;

	pattrib->qsel = qsel;
}

/*
 * The main transmit(tx) entry
 *
 * Return
 *1	enqueue
 *0	success, hardware will handle this xmit frame(packet)
 *<0	fail
 */
s32 rtw_xmit(struct adapter *padapter, struct sk_buff **ppkt)
{
	static unsigned long start;
	static u32 drop_cnt;

	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct xmit_frame *pxmitframe = NULL;

	s32 res;

	if (start == 0)
		start = jiffies;

	pxmitframe = rtw_alloc_xmitframe(pxmitpriv);

	if (jiffies_to_msecs(jiffies - start) > 2000) {
		start = jiffies;
		drop_cnt = 0;
	}

	if (!pxmitframe) {
		drop_cnt++;
		return -1;
	}

	res = update_attrib(padapter, *ppkt, &pxmitframe->attrib);

	if (res == _FAIL) {
		rtw_free_xmitframe(pxmitpriv, pxmitframe);
		return -1;
	}
	pxmitframe->pkt = *ppkt;

	do_queue_select(padapter, &pxmitframe->attrib);

	spin_lock_bh(&pxmitpriv->lock);
	if (xmitframe_enqueue_for_sleeping_sta(padapter, pxmitframe) == true) {
		spin_unlock_bh(&pxmitpriv->lock);
		return 1;
	}
	spin_unlock_bh(&pxmitpriv->lock);

	/* pre_xmitframe */
	if (rtw_hal_xmit(padapter, pxmitframe) == false)
		return 1;

	return 0;
}

#define RTW_HIQ_FILTER_ALLOW_ALL 0
#define RTW_HIQ_FILTER_ALLOW_SPECIAL 1
#define RTW_HIQ_FILTER_DENY_ALL 2

inline bool xmitframe_hiq_filter(struct xmit_frame *xmitframe)
{
	bool allow = false;
	struct adapter *adapter = xmitframe->padapter;
	struct registry_priv *registry = &adapter->registrypriv;

	if (registry->hiq_filter == RTW_HIQ_FILTER_ALLOW_SPECIAL) {
		struct pkt_attrib *attrib = &xmitframe->attrib;

		if (attrib->ether_type == 0x0806 ||
		    attrib->ether_type == 0x888e ||
		    attrib->dhcp_pkt
		)
			allow = true;

	} else if (registry->hiq_filter == RTW_HIQ_FILTER_ALLOW_ALL)
		allow = true;
	else if (registry->hiq_filter == RTW_HIQ_FILTER_DENY_ALL) {
	} else
		rtw_warn_on(1);

	return allow;
}

signed int xmitframe_enqueue_for_sleeping_sta(struct adapter *padapter, struct xmit_frame *pxmitframe)
{
	signed int ret = false;
	struct sta_info *psta = NULL;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct pkt_attrib *pattrib = &pxmitframe->attrib;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	signed int bmcst = IS_MCAST(pattrib->ra);
	bool update_tim = false;

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE) == false)
		return ret;
	psta = rtw_get_stainfo(&padapter->stapriv, pattrib->ra);
	if (pattrib->psta != psta)
		return false;

	if (!psta)
		return false;

	if (!(psta->state & _FW_LINKED))
		return false;

	if (pattrib->triggered == 1) {
		if (bmcst && xmitframe_hiq_filter(pxmitframe))
			pattrib->qsel = 0x11;/* HIQ */

		return ret;
	}

	if (bmcst) {
		spin_lock_bh(&psta->sleep_q.lock);

		if (pstapriv->sta_dz_bitmap) { /* if anyone sta is in ps mode */
			/* pattrib->qsel = 0x11;HIQ */

			list_del_init(&pxmitframe->list);

			list_add_tail(&pxmitframe->list, get_list_head(&psta->sleep_q));

			psta->sleepq_len++;

			if (!(pstapriv->tim_bitmap & BIT(0)))
				update_tim = true;

			pstapriv->tim_bitmap |= BIT(0);
			pstapriv->sta_dz_bitmap |= BIT(0);

			if (update_tim)
				update_beacon(padapter, WLAN_EID_TIM, NULL, true);
			else
				chk_bmc_sleepq_cmd(padapter);

			ret = true;
		}

		spin_unlock_bh(&psta->sleep_q.lock);

		return ret;
	}

	spin_lock_bh(&psta->sleep_q.lock);

	if (psta->state&WIFI_SLEEP_STATE) {
		u8 wmmps_ac = 0;

		if (pstapriv->sta_dz_bitmap & BIT(psta->aid)) {
			list_del_init(&pxmitframe->list);

			list_add_tail(&pxmitframe->list, get_list_head(&psta->sleep_q));

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
				if (!(pstapriv->tim_bitmap & BIT(psta->aid)))
					update_tim = true;

				pstapriv->tim_bitmap |= BIT(psta->aid);

				if (update_tim)
					/* update BCN for TIM IE */
					update_beacon(padapter, WLAN_EID_TIM, NULL, true);
			}

			ret = true;
		}
	}

	spin_unlock_bh(&psta->sleep_q.lock);

	return ret;
}

static void dequeue_xmitframes_to_sleeping_queue(struct adapter *padapter, struct sta_info *psta, struct __queue *pframequeue)
{
	signed int ret;
	struct list_head	*plist, *phead;
	u8 ac_index;
	struct tx_servq	*ptxservq;
	struct pkt_attrib	*pattrib;
	struct xmit_frame	*pxmitframe;
	struct hw_xmit *phwxmits =  padapter->xmitpriv.hwxmits;

	phead = get_list_head(pframequeue);
	plist = get_next(phead);

	while (phead != plist) {
		pxmitframe = container_of(plist, struct xmit_frame, list);

		plist = get_next(plist);

		pattrib = &pxmitframe->attrib;

		pattrib->triggered = 0;

		ret = xmitframe_enqueue_for_sleeping_sta(padapter, pxmitframe);

		if (true == ret) {
			ptxservq = rtw_get_sta_pending(padapter, psta, pattrib->priority, (u8 *)(&ac_index));

			ptxservq->qcnt--;
			phwxmits[ac_index].accnt--;
		} else {
		}
	}
}

void stop_sta_xmit(struct adapter *padapter, struct sta_info *psta)
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
	list_del_init(&pstaxmitpriv->vo_q.tx_pending);

	dequeue_xmitframes_to_sleeping_queue(padapter, psta, &pstaxmitpriv->vi_q.sta_pending);
	list_del_init(&pstaxmitpriv->vi_q.tx_pending);

	dequeue_xmitframes_to_sleeping_queue(padapter, psta, &pstaxmitpriv->be_q.sta_pending);
	list_del_init(&pstaxmitpriv->be_q.tx_pending);

	dequeue_xmitframes_to_sleeping_queue(padapter, psta, &pstaxmitpriv->bk_q.sta_pending);
	list_del_init(&pstaxmitpriv->bk_q.tx_pending);

	/* for BC/MC Frames */
	pstaxmitpriv = &psta_bmc->sta_xmitpriv;
	dequeue_xmitframes_to_sleeping_queue(padapter, psta_bmc, &pstaxmitpriv->be_q.sta_pending);
	list_del_init(&pstaxmitpriv->be_q.tx_pending);

	spin_unlock_bh(&pxmitpriv->lock);
}

void wakeup_sta_to_xmit(struct adapter *padapter, struct sta_info *psta)
{
	u8 update_mask = 0, wmmps_ac = 0;
	struct sta_info *psta_bmc;
	struct list_head	*xmitframe_plist, *xmitframe_phead;
	struct xmit_frame *pxmitframe = NULL;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;

	psta_bmc = rtw_get_bcmc_stainfo(padapter);

	spin_lock_bh(&pxmitpriv->lock);

	xmitframe_phead = get_list_head(&psta->sleep_q);
	xmitframe_plist = get_next(xmitframe_phead);

	while (xmitframe_phead != xmitframe_plist) {
		pxmitframe = container_of(xmitframe_plist, struct xmit_frame, list);

		xmitframe_plist = get_next(xmitframe_plist);

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

		rtw_hal_xmitframe_enqueue(padapter, pxmitframe);
	}

	if (psta->sleepq_len == 0) {
		if (pstapriv->tim_bitmap & BIT(psta->aid))
			update_mask = BIT(0);

		pstapriv->tim_bitmap &= ~BIT(psta->aid);

		if (psta->state&WIFI_SLEEP_STATE)
			psta->state ^= WIFI_SLEEP_STATE;

		if (psta->state & WIFI_STA_ALIVE_CHK_STATE) {
			psta->expire_to = pstapriv->expire_to;
			psta->state ^= WIFI_STA_ALIVE_CHK_STATE;
		}

		pstapriv->sta_dz_bitmap &= ~BIT(psta->aid);
	}

	/* for BC/MC Frames */
	if (!psta_bmc)
		goto _exit;

	if ((pstapriv->sta_dz_bitmap&0xfffe) == 0x0) { /* no any sta in ps mode */
		xmitframe_phead = get_list_head(&psta_bmc->sleep_q);
		xmitframe_plist = get_next(xmitframe_phead);

		while (xmitframe_phead != xmitframe_plist) {
			pxmitframe = container_of(xmitframe_plist, struct xmit_frame, list);

			xmitframe_plist = get_next(xmitframe_plist);

			list_del_init(&pxmitframe->list);

			psta_bmc->sleepq_len--;
			if (psta_bmc->sleepq_len > 0)
				pxmitframe->attrib.mdata = 1;
			else
				pxmitframe->attrib.mdata = 0;

			pxmitframe->attrib.triggered = 1;
			rtw_hal_xmitframe_enqueue(padapter, pxmitframe);
		}

		if (psta_bmc->sleepq_len == 0) {
			if (pstapriv->tim_bitmap & BIT(0))
				update_mask |= BIT(1);

			pstapriv->tim_bitmap &= ~BIT(0);
			pstapriv->sta_dz_bitmap &= ~BIT(0);
		}
	}

_exit:

	spin_unlock_bh(&pxmitpriv->lock);

	if (update_mask)
		update_beacon(padapter, WLAN_EID_TIM, NULL, true);
}

void xmit_delivery_enabled_frames(struct adapter *padapter, struct sta_info *psta)
{
	u8 wmmps_ac = 0;
	struct list_head	*xmitframe_plist, *xmitframe_phead;
	struct xmit_frame *pxmitframe = NULL;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;

	spin_lock_bh(&pxmitpriv->lock);

	xmitframe_phead = get_list_head(&psta->sleep_q);
	xmitframe_plist = get_next(xmitframe_phead);

	while (xmitframe_phead != xmitframe_plist) {
		pxmitframe = container_of(xmitframe_plist, struct xmit_frame, list);

		xmitframe_plist = get_next(xmitframe_plist);

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
		rtw_hal_xmitframe_enqueue(padapter, pxmitframe);

		if ((psta->sleepq_ac_len == 0) && (!psta->has_legacy_ac) && (wmmps_ac)) {
			pstapriv->tim_bitmap &= ~BIT(psta->aid);

			update_beacon(padapter, WLAN_EID_TIM, NULL, true);
		}
	}

	spin_unlock_bh(&pxmitpriv->lock);
}

void enqueue_pending_xmitbuf(struct xmit_priv *pxmitpriv, struct xmit_buf *pxmitbuf)
{
	struct __queue *pqueue;
	struct adapter *pri_adapter = pxmitpriv->adapter;

	pqueue = &pxmitpriv->pending_xmitbuf_queue;

	spin_lock_bh(&pqueue->lock);
	list_del_init(&pxmitbuf->list);
	list_add_tail(&pxmitbuf->list, get_list_head(pqueue));
	spin_unlock_bh(&pqueue->lock);

	complete(&pri_adapter->xmitpriv.xmit_comp);
}

void enqueue_pending_xmitbuf_to_head(struct xmit_priv *pxmitpriv, struct xmit_buf *pxmitbuf)
{
	struct __queue *pqueue;

	pqueue = &pxmitpriv->pending_xmitbuf_queue;

	spin_lock_bh(&pqueue->lock);
	list_del_init(&pxmitbuf->list);
	list_add(&pxmitbuf->list, get_list_head(pqueue));
	spin_unlock_bh(&pqueue->lock);
}

struct xmit_buf *dequeue_pending_xmitbuf(struct xmit_priv *pxmitpriv)
{
	struct xmit_buf *pxmitbuf;
	struct __queue *pqueue;

	pxmitbuf = NULL;
	pqueue = &pxmitpriv->pending_xmitbuf_queue;

	spin_lock_bh(&pqueue->lock);

	if (!list_empty(&pqueue->queue)) {
		struct list_head *plist, *phead;

		phead = get_list_head(pqueue);
		plist = get_next(phead);
		pxmitbuf = container_of(plist, struct xmit_buf, list);
		list_del_init(&pxmitbuf->list);
	}

	spin_unlock_bh(&pqueue->lock);

	return pxmitbuf;
}

struct xmit_buf *dequeue_pending_xmitbuf_under_survey(struct xmit_priv *pxmitpriv)
{
	struct xmit_buf *pxmitbuf;
	struct __queue *pqueue;

	pxmitbuf = NULL;
	pqueue = &pxmitpriv->pending_xmitbuf_queue;

	spin_lock_bh(&pqueue->lock);

	if (!list_empty(&pqueue->queue)) {
		struct list_head *plist, *phead;
		u8 type;

		phead = get_list_head(pqueue);
		plist = phead;
		do {
			plist = get_next(plist);
			if (plist == phead)
				break;

			pxmitbuf = container_of(plist, struct xmit_buf, list);

			type = GetFrameSubType(pxmitbuf->pbuf + TXDESC_OFFSET);

			if ((type == WIFI_PROBEREQ) ||
				(type == WIFI_DATA_NULL) ||
				(type == WIFI_QOS_DATA_NULL)) {
				list_del_init(&pxmitbuf->list);
				break;
			}
			pxmitbuf = NULL;
		} while (1);
	}

	spin_unlock_bh(&pqueue->lock);

	return pxmitbuf;
}

signed int check_pending_xmitbuf(struct xmit_priv *pxmitpriv)
{
	struct __queue *pqueue;
	signed int	ret = false;

	pqueue = &pxmitpriv->pending_xmitbuf_queue;

	spin_lock_bh(&pqueue->lock);

	if (!list_empty(&pqueue->queue))
		ret = true;

	spin_unlock_bh(&pqueue->lock);

	return ret;
}

int rtw_xmit_thread(void *context)
{
	s32 err;
	struct adapter *padapter;

	err = _SUCCESS;
	padapter = context;

	thread_enter("RTW_XMIT_THREAD");

	do {
		err = rtw_hal_xmit_thread_handler(padapter);
		flush_signals_thread();
	} while (err == _SUCCESS);

	complete(&padapter->xmitpriv.terminate_xmitthread_comp);

	thread_exit();
}

void rtw_sctx_init(struct submit_ctx *sctx, int timeout_ms)
{
	sctx->timeout_ms = timeout_ms;
	sctx->submit_time = jiffies;
	init_completion(&sctx->done);
	sctx->status = RTW_SCTX_SUBMITTED;
}

int rtw_sctx_wait(struct submit_ctx *sctx, const char *msg)
{
	int ret = _FAIL;
	unsigned long expire;
	int status = 0;

	expire = sctx->timeout_ms ? msecs_to_jiffies(sctx->timeout_ms) : MAX_SCHEDULE_TIMEOUT;
	if (!wait_for_completion_timeout(&sctx->done, expire))
		/* timeout, do something?? */
		status = RTW_SCTX_DONE_TIMEOUT;
	else
		status = sctx->status;

	if (status == RTW_SCTX_DONE_SUCCESS)
		ret = _SUCCESS;

	return ret;
}

void rtw_sctx_done_err(struct submit_ctx **sctx, int status)
{
	if (*sctx) {
		(*sctx)->status = status;
		complete(&((*sctx)->done));
		*sctx = NULL;
	}
}

void rtw_sctx_done(struct submit_ctx **sctx)
{
	rtw_sctx_done_err(sctx, RTW_SCTX_DONE_SUCCESS);
}

int rtw_ack_tx_wait(struct xmit_priv *pxmitpriv, u32 timeout_ms)
{
	struct submit_ctx *pack_tx_ops = &pxmitpriv->ack_tx_ops;

	pack_tx_ops->submit_time = jiffies;
	pack_tx_ops->timeout_ms = timeout_ms;
	pack_tx_ops->status = RTW_SCTX_SUBMITTED;

	return rtw_sctx_wait(pack_tx_ops, __func__);
}

void rtw_ack_tx_done(struct xmit_priv *pxmitpriv, int status)
{
	struct submit_ctx *pack_tx_ops = &pxmitpriv->ack_tx_ops;

	if (pxmitpriv->ack_tx)
		rtw_sctx_done_err(&pack_tx_ops, status);
}
