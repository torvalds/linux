// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#include <drv_types.h>
#include <linux/jiffies.h>
#include <rtw_recv.h>
#include <net/cfg80211.h>
#include <asm/unaligned.h>

static u8 SNAP_ETH_TYPE_IPX[2] = {0x81, 0x37};
static u8 SNAP_ETH_TYPE_APPLETALK_AARP[2] = {0x80, 0xf3};

static void rtw_signal_stat_timer_hdl(struct timer_list *t);

void _rtw_init_sta_recv_priv(struct sta_recv_priv *psta_recvpriv)
{
	memset((u8 *)psta_recvpriv, 0, sizeof(struct sta_recv_priv));

	spin_lock_init(&psta_recvpriv->lock);

	/* for (i = 0; i<MAX_RX_NUMBLKS; i++) */
	/* _rtw_init_queue(&psta_recvpriv->blk_strms[i]); */

	INIT_LIST_HEAD(&psta_recvpriv->defrag_q.queue);
	spin_lock_init(&psta_recvpriv->defrag_q.lock);
}

signed int _rtw_init_recv_priv(struct recv_priv *precvpriv, struct adapter *padapter)
{
	signed int i;
	union recv_frame *precvframe;
	signed int	res = _SUCCESS;

	spin_lock_init(&precvpriv->lock);

	INIT_LIST_HEAD(&precvpriv->free_recv_queue.queue);
	spin_lock_init(&precvpriv->free_recv_queue.lock);
	INIT_LIST_HEAD(&precvpriv->recv_pending_queue.queue);
	spin_lock_init(&precvpriv->recv_pending_queue.lock);
	INIT_LIST_HEAD(&precvpriv->uc_swdec_pending_queue.queue);
	spin_lock_init(&precvpriv->uc_swdec_pending_queue.lock);

	precvpriv->adapter = padapter;

	precvpriv->free_recvframe_cnt = NR_RECVFRAME;

	precvpriv->pallocated_frame_buf = vzalloc(NR_RECVFRAME * sizeof(union recv_frame) + RXFRAME_ALIGN_SZ);

	if (!precvpriv->pallocated_frame_buf) {
		res = _FAIL;
		goto exit;
	}

	precvpriv->precv_frame_buf = (u8 *)N_BYTE_ALIGMENT((SIZE_PTR)(precvpriv->pallocated_frame_buf), RXFRAME_ALIGN_SZ);
	/* precvpriv->precv_frame_buf = precvpriv->pallocated_frame_buf + RXFRAME_ALIGN_SZ - */
	/* ((SIZE_PTR) (precvpriv->pallocated_frame_buf) &(RXFRAME_ALIGN_SZ-1)); */

	precvframe = (union recv_frame *) precvpriv->precv_frame_buf;


	for (i = 0; i < NR_RECVFRAME; i++) {
		INIT_LIST_HEAD(&(precvframe->u.list));

		list_add_tail(&(precvframe->u.list), &(precvpriv->free_recv_queue.queue));

		rtw_os_recv_resource_alloc(padapter, precvframe);

		precvframe->u.hdr.len = 0;

		precvframe->u.hdr.adapter = padapter;
		precvframe++;

	}

	res = rtw_hal_init_recv_priv(padapter);

	timer_setup(&precvpriv->signal_stat_timer, rtw_signal_stat_timer_hdl,
		    0);

	precvpriv->signal_stat_sampling_interval = 2000; /* ms */

	rtw_set_signal_stat_timer(precvpriv);

exit:
	return res;
}

void _rtw_free_recv_priv(struct recv_priv *precvpriv)
{
	struct adapter	*padapter = precvpriv->adapter;

	rtw_free_uc_swdec_pending_queue(padapter);

	rtw_os_recv_resource_free(precvpriv);

	vfree(precvpriv->pallocated_frame_buf);

	rtw_hal_free_recv_priv(padapter);
}

union recv_frame *_rtw_alloc_recvframe(struct __queue *pfree_recv_queue)
{

	union recv_frame  *precvframe;
	struct list_head	*plist, *phead;
	struct adapter *padapter;
	struct recv_priv *precvpriv;

	if (list_empty(&pfree_recv_queue->queue))
		precvframe = NULL;
	else {
		phead = get_list_head(pfree_recv_queue);

		plist = get_next(phead);

		precvframe = (union recv_frame *)plist;

		list_del_init(&precvframe->u.hdr.list);
		padapter = precvframe->u.hdr.adapter;
		if (padapter) {
			precvpriv = &padapter->recvpriv;
			if (pfree_recv_queue == &precvpriv->free_recv_queue)
				precvpriv->free_recvframe_cnt--;
		}
	}
	return precvframe;
}

union recv_frame *rtw_alloc_recvframe(struct __queue *pfree_recv_queue)
{
	union recv_frame  *precvframe;

	spin_lock_bh(&pfree_recv_queue->lock);

	precvframe = _rtw_alloc_recvframe(pfree_recv_queue);

	spin_unlock_bh(&pfree_recv_queue->lock);

	return precvframe;
}

int rtw_free_recvframe(union recv_frame *precvframe, struct __queue *pfree_recv_queue)
{
	struct adapter *padapter = precvframe->u.hdr.adapter;
	struct recv_priv *precvpriv = &padapter->recvpriv;

	rtw_os_free_recvframe(precvframe);


	spin_lock_bh(&pfree_recv_queue->lock);

	list_del_init(&(precvframe->u.hdr.list));

	precvframe->u.hdr.len = 0;

	list_add_tail(&(precvframe->u.hdr.list), get_list_head(pfree_recv_queue));

	if (padapter) {
		if (pfree_recv_queue == &precvpriv->free_recv_queue)
			precvpriv->free_recvframe_cnt++;
	}
	spin_unlock_bh(&pfree_recv_queue->lock);
	return _SUCCESS;
}




signed int _rtw_enqueue_recvframe(union recv_frame *precvframe, struct __queue *queue)
{

	struct adapter *padapter = precvframe->u.hdr.adapter;
	struct recv_priv *precvpriv = &padapter->recvpriv;

	/* INIT_LIST_HEAD(&(precvframe->u.hdr.list)); */
	list_del_init(&(precvframe->u.hdr.list));


	list_add_tail(&(precvframe->u.hdr.list), get_list_head(queue));

	if (padapter)
		if (queue == &precvpriv->free_recv_queue)
			precvpriv->free_recvframe_cnt++;

	return _SUCCESS;
}

signed int rtw_enqueue_recvframe(union recv_frame *precvframe, struct __queue *queue)
{
	signed int ret;

	/* _spinlock(&pfree_recv_queue->lock); */
	spin_lock_bh(&queue->lock);
	ret = _rtw_enqueue_recvframe(precvframe, queue);
	/* spin_unlock(&pfree_recv_queue->lock); */
	spin_unlock_bh(&queue->lock);

	return ret;
}

/*
 * caller : defrag ; recvframe_chk_defrag in recv_thread  (passive)
 * pframequeue: defrag_queue : will be accessed in recv_thread  (passive)
 *
 * using spinlock to protect
 *
 */

void rtw_free_recvframe_queue(struct __queue *pframequeue,  struct __queue *pfree_recv_queue)
{
	union	recv_frame	*precvframe;
	struct list_head	*plist, *phead;

	spin_lock(&pframequeue->lock);

	phead = get_list_head(pframequeue);
	plist = get_next(phead);

	while (phead != plist) {
		precvframe = (union recv_frame *)plist;

		plist = get_next(plist);

		rtw_free_recvframe(precvframe, pfree_recv_queue);
	}

	spin_unlock(&pframequeue->lock);
}

u32 rtw_free_uc_swdec_pending_queue(struct adapter *adapter)
{
	u32 cnt = 0;
	union recv_frame *pending_frame;

	while ((pending_frame = rtw_alloc_recvframe(&adapter->recvpriv.uc_swdec_pending_queue))) {
		rtw_free_recvframe(pending_frame, &adapter->recvpriv.free_recv_queue);
		cnt++;
	}

	return cnt;
}


signed int rtw_enqueue_recvbuf_to_head(struct recv_buf *precvbuf, struct __queue *queue)
{
	spin_lock_bh(&queue->lock);

	list_del_init(&precvbuf->list);
	list_add(&precvbuf->list, get_list_head(queue));

	spin_unlock_bh(&queue->lock);

	return _SUCCESS;
}

signed int rtw_enqueue_recvbuf(struct recv_buf *precvbuf, struct __queue *queue)
{
	spin_lock_bh(&queue->lock);

	list_del_init(&precvbuf->list);

	list_add_tail(&precvbuf->list, get_list_head(queue));
	spin_unlock_bh(&queue->lock);
	return _SUCCESS;

}

struct recv_buf *rtw_dequeue_recvbuf(struct __queue *queue)
{
	struct recv_buf *precvbuf;
	struct list_head	*plist, *phead;

	spin_lock_bh(&queue->lock);

	if (list_empty(&queue->queue))
		precvbuf = NULL;
	else {
		phead = get_list_head(queue);

		plist = get_next(phead);

		precvbuf = container_of(plist, struct recv_buf, list);

		list_del_init(&precvbuf->list);

	}

	spin_unlock_bh(&queue->lock);

	return precvbuf;

}

static signed int recvframe_chkmic(struct adapter *adapter,  union recv_frame *precvframe)
{

	signed int	i, res = _SUCCESS;
	u32 datalen;
	u8 miccode[8];
	u8 bmic_err = false, brpt_micerror = true;
	u8 *pframe, *payload, *pframemic;
	u8 *mickey;
	/* u8 *iv, rxdata_key_idx = 0; */
	struct sta_info *stainfo;
	struct rx_pkt_attrib *prxattrib = &precvframe->u.hdr.attrib;
	struct security_priv *psecuritypriv = &adapter->securitypriv;

	struct mlme_ext_priv *pmlmeext = &adapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);

	stainfo = rtw_get_stainfo(&adapter->stapriv, &prxattrib->ta[0]);

	if (prxattrib->encrypt == _TKIP_) {
		/* calculate mic code */
		if (stainfo) {
			if (is_multicast_ether_addr(prxattrib->ra)) {
				/* mickey =&psecuritypriv->dot118021XGrprxmickey.skey[0]; */
				/* iv = precvframe->u.hdr.rx_data+prxattrib->hdrlen; */
				/* rxdata_key_idx =(((iv[3])>>6)&0x3) ; */
				mickey = &psecuritypriv->dot118021XGrprxmickey[prxattrib->key_index].skey[0];

				/* psecuritypriv->dot118021XGrpKeyid, pmlmeinfo->key_index, rxdata_key_idx); */

				if (psecuritypriv->binstallGrpkey == false) {
					res = _FAIL;
					goto exit;
				}
			} else {
				mickey = &stainfo->dot11tkiprxmickey.skey[0];
			}

			datalen = precvframe->u.hdr.len-prxattrib->hdrlen-prxattrib->iv_len-prxattrib->icv_len-8;/* icv_len included the mic code */
			pframe = precvframe->u.hdr.rx_data;
			payload = pframe+prxattrib->hdrlen+prxattrib->iv_len;

			rtw_seccalctkipmic(mickey, pframe, payload, datalen, &miccode[0], (unsigned char)prxattrib->priority); /* care the length of the data */

			pframemic = payload+datalen;

			bmic_err = false;

			for (i = 0; i < 8; i++) {
				if (miccode[i] != *(pframemic + i))
					bmic_err = true;
			}


			if (bmic_err == true) {
				/*  double check key_index for some timing issue , */
				/*  cannot compare with psecuritypriv->dot118021XGrpKeyid also cause timing issue */
				if ((is_multicast_ether_addr(prxattrib->ra) == true)  && (prxattrib->key_index != pmlmeinfo->key_index))
					brpt_micerror = false;

				if (prxattrib->bdecrypted && brpt_micerror)
					rtw_handle_tkip_mic_err(adapter, (u8)is_multicast_ether_addr(prxattrib->ra));

				res = _FAIL;

			} else {
				/* mic checked ok */
				if (!psecuritypriv->bcheck_grpkey &&
				    is_multicast_ether_addr(prxattrib->ra))
					psecuritypriv->bcheck_grpkey = true;
			}
		}

		recvframe_pull_tail(precvframe, 8);

	}

exit:
	return res;

}

/* decrypt and set the ivlen, icvlen of the recv_frame */
static union recv_frame *decryptor(struct adapter *padapter, union recv_frame *precv_frame)
{

	struct rx_pkt_attrib *prxattrib = &precv_frame->u.hdr.attrib;
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	union recv_frame *return_packet = precv_frame;
	u32  res = _SUCCESS;

	if (prxattrib->encrypt > 0) {
		u8 *iv = precv_frame->u.hdr.rx_data+prxattrib->hdrlen;

		prxattrib->key_index = (((iv[3])>>6)&0x3);

		if (prxattrib->key_index > WEP_KEYS) {
			switch (prxattrib->encrypt) {
			case _WEP40_:
			case _WEP104_:
				prxattrib->key_index = psecuritypriv->dot11PrivacyKeyIndex;
				break;
			case _TKIP_:
			case _AES_:
			default:
				prxattrib->key_index = psecuritypriv->dot118021XGrpKeyid;
				break;
			}
		}
	}

	if ((prxattrib->encrypt > 0) && ((prxattrib->bdecrypted == 0) || (psecuritypriv->sw_decrypt == true))) {
		psecuritypriv->hw_decrypted = false;

		switch (prxattrib->encrypt) {
		case _WEP40_:
		case _WEP104_:
			rtw_wep_decrypt(padapter, (u8 *)precv_frame);
			break;
		case _TKIP_:
			res = rtw_tkip_decrypt(padapter, (u8 *)precv_frame);
			break;
		case _AES_:
			res = rtw_aes_decrypt(padapter, (u8 *)precv_frame);
			break;
		default:
				break;
		}
	} else if (prxattrib->bdecrypted == 1 && prxattrib->encrypt > 0 &&
		   (psecuritypriv->busetkipkey == 1 || prxattrib->encrypt != _TKIP_)
		) {
		psecuritypriv->hw_decrypted = true;
	} else {
	}

	if (res == _FAIL) {
		rtw_free_recvframe(return_packet, &padapter->recvpriv.free_recv_queue);
		return_packet = NULL;
	} else
		prxattrib->bdecrypted = true;

	return return_packet;
}

/* set the security information in the recv_frame */
static union recv_frame *portctrl(struct adapter *adapter, union recv_frame *precv_frame)
{
	u8 *psta_addr = NULL;
	u8 *ptr;
	uint  auth_alg;
	struct recv_frame_hdr *pfhdr;
	struct sta_info *psta;
	struct sta_priv *pstapriv;
	union recv_frame *prtnframe;
	u16 ether_type = 0;
	u16  eapol_type = 0x888e;/* for Funia BD's WPA issue */
	struct rx_pkt_attrib *pattrib;

	pstapriv = &adapter->stapriv;

	auth_alg = adapter->securitypriv.dot11AuthAlgrthm;

	ptr = precv_frame->u.hdr.rx_data;
	pfhdr = &precv_frame->u.hdr;
	pattrib = &pfhdr->attrib;
	psta_addr = pattrib->ta;

	prtnframe = NULL;

	psta = rtw_get_stainfo(pstapriv, psta_addr);

	if (auth_alg == 2) {
		if ((psta) && (psta->ieee8021x_blocked)) {
			__be16 be_tmp;

			/* blocked */
			/* only accept EAPOL frame */

			prtnframe = precv_frame;

			/* get ether_type */
			ptr = ptr + pfhdr->attrib.hdrlen + pfhdr->attrib.iv_len + LLC_HEADER_LENGTH;
			memcpy(&be_tmp, ptr, 2);
			ether_type = ntohs(be_tmp);

			if (ether_type == eapol_type)
				prtnframe = precv_frame;
			else {
				/* free this frame */
				rtw_free_recvframe(precv_frame, &adapter->recvpriv.free_recv_queue);
				prtnframe = NULL;
			}
		} else {
			/* allowed */
			/* check decryption status, and decrypt the frame if needed */

			prtnframe = precv_frame;
			/* check is the EAPOL frame or not (Rekey) */
			/* if (ether_type == eapol_type) { */
				/* check Rekey */

			/* prtnframe =precv_frame; */
			/*  */
			/* else { */
			/*  */
		}
	} else
		prtnframe = precv_frame;

	return prtnframe;
}

static signed int recv_decache(union recv_frame *precv_frame, u8 bretry, struct stainfo_rxcache *prxcache)
{
	signed int tid = precv_frame->u.hdr.attrib.priority;

	u16 seq_ctrl = ((precv_frame->u.hdr.attrib.seq_num&0xffff) << 4) |
		(precv_frame->u.hdr.attrib.frag_num & 0xf);

	if (tid > 15)
		return _FAIL;

	if (1) { /* if (bretry) */
		if (seq_ctrl == prxcache->tid_rxseq[tid])
			return _FAIL;
	}

	prxcache->tid_rxseq[tid] = seq_ctrl;

	return _SUCCESS;

}

static void process_pwrbit_data(struct adapter *padapter, union recv_frame *precv_frame)
{
	unsigned char pwrbit;
	u8 *ptr = precv_frame->u.hdr.rx_data;
	struct rx_pkt_attrib *pattrib = &precv_frame->u.hdr.attrib;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct sta_info *psta = NULL;

	psta = rtw_get_stainfo(pstapriv, pattrib->src);

	pwrbit = GetPwrMgt(ptr);

	if (psta) {
		if (pwrbit) {
			if (!(psta->state & WIFI_SLEEP_STATE)) {
				/* psta->state |= WIFI_SLEEP_STATE; */
				/* pstapriv->sta_dz_bitmap |= BIT(psta->aid); */

				stop_sta_xmit(padapter, psta);

			}
		} else {
			if (psta->state & WIFI_SLEEP_STATE) {
				/* psta->state ^= WIFI_SLEEP_STATE; */
				/* pstapriv->sta_dz_bitmap &= ~BIT(psta->aid); */

				wakeup_sta_to_xmit(padapter, psta);
			}
		}

	}
}

static void process_wmmps_data(struct adapter *padapter, union recv_frame *precv_frame)
{
	struct rx_pkt_attrib *pattrib = &precv_frame->u.hdr.attrib;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct sta_info *psta = NULL;

	psta = rtw_get_stainfo(pstapriv, pattrib->src);

	if (!psta)
		return;

	if (!psta->qos_option)
		return;

	if (!(psta->qos_info&0xf))
		return;

	if (psta->state&WIFI_SLEEP_STATE) {
		u8 wmmps_ac = 0;

		switch (pattrib->priority) {
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

		if (wmmps_ac) {
			if (psta->sleepq_ac_len > 0)
				/* process received triggered frame */
				xmit_delivery_enabled_frames(padapter, psta);
			else
				/* issue one qos null frame with More data bit = 0 and the EOSP bit set (= 1) */
				issue_qos_nulldata(padapter, psta->hwaddr, (u16)pattrib->priority, 0, 0);
		}
	}
}

static void count_rx_stats(struct adapter *padapter, union recv_frame *prframe, struct sta_info *sta)
{
	int sz;
	struct sta_info *psta = NULL;
	struct stainfo_stats *pstats = NULL;
	struct rx_pkt_attrib *pattrib = &prframe->u.hdr.attrib;
	struct recv_priv *precvpriv = &padapter->recvpriv;

	sz = get_recvframe_len(prframe);
	precvpriv->rx_bytes += sz;

	padapter->mlmepriv.LinkDetectInfo.NumRxOkInPeriod++;

	if ((!is_broadcast_ether_addr(pattrib->dst)) && (!is_multicast_ether_addr(pattrib->dst)))
		padapter->mlmepriv.LinkDetectInfo.NumRxUnicastOkInPeriod++;

	if (sta)
		psta = sta;
	else
		psta = prframe->u.hdr.psta;

	if (psta) {
		pstats = &psta->sta_stats;

		pstats->rx_data_pkts++;
		pstats->rx_bytes += sz;
	}

	traffic_check_for_leave_lps(padapter, false, 0);
}

static signed int sta2sta_data_frame(struct adapter *adapter, union recv_frame *precv_frame,
			struct sta_info **psta)
{
	u8 *ptr = precv_frame->u.hdr.rx_data;
	signed int ret = _SUCCESS;
	struct rx_pkt_attrib *pattrib = &precv_frame->u.hdr.attrib;
	struct sta_priv *pstapriv = &adapter->stapriv;
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	u8 *mybssid  = get_bssid(pmlmepriv);
	u8 *myhwaddr = myid(&adapter->eeprompriv);
	u8 *sta_addr = NULL;
	signed int bmcast = is_multicast_ether_addr(pattrib->dst);

	if ((check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == true) ||
		(check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == true)) {

		/*  filter packets that SA is myself or multicast or broadcast */
		if (!memcmp(myhwaddr, pattrib->src, ETH_ALEN)) {
			ret = _FAIL;
			goto exit;
		}

		if ((memcmp(myhwaddr, pattrib->dst, ETH_ALEN))	&& (!bmcast)) {
			ret = _FAIL;
			goto exit;
		}

		if (is_zero_ether_addr(pattrib->bssid) ||
		    is_zero_ether_addr(mybssid) ||
		    (memcmp(pattrib->bssid, mybssid, ETH_ALEN))) {
			ret = _FAIL;
			goto exit;
		}

		sta_addr = pattrib->src;

	} else if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) == true) {
		/*  For Station mode, sa and bssid should always be BSSID, and DA is my mac-address */
		if (memcmp(pattrib->bssid, pattrib->src, ETH_ALEN)) {
			ret = _FAIL;
			goto exit;
		}

		sta_addr = pattrib->bssid;
	} else if (check_fwstate(pmlmepriv, WIFI_AP_STATE) == true) {
		if (bmcast) {
			/*  For AP mode, if DA == MCAST, then BSSID should be also MCAST */
			if (!is_multicast_ether_addr(pattrib->bssid)) {
				ret = _FAIL;
				goto exit;
			}
		} else { /*  not mc-frame */
			/*  For AP mode, if DA is non-MCAST, then it must be BSSID, and bssid == BSSID */
			if (memcmp(pattrib->bssid, pattrib->dst, ETH_ALEN)) {
				ret = _FAIL;
				goto exit;
			}

			sta_addr = pattrib->src;
		}

	} else if (check_fwstate(pmlmepriv, WIFI_MP_STATE) == true) {
		memcpy(pattrib->dst, GetAddr1Ptr(ptr), ETH_ALEN);
		memcpy(pattrib->src, GetAddr2Ptr(ptr), ETH_ALEN);
		memcpy(pattrib->bssid, GetAddr3Ptr(ptr), ETH_ALEN);
		memcpy(pattrib->ra, pattrib->dst, ETH_ALEN);
		memcpy(pattrib->ta, pattrib->src, ETH_ALEN);

		sta_addr = mybssid;
	} else
		ret  = _FAIL;



	if (bmcast)
		*psta = rtw_get_bcmc_stainfo(adapter);
	else
		*psta = rtw_get_stainfo(pstapriv, sta_addr); /*  get ap_info */

	if (!*psta) {
		ret = _FAIL;
		goto exit;
	}

exit:
	return ret;
}

static signed int ap2sta_data_frame(struct adapter *adapter, union recv_frame *precv_frame,
		       struct sta_info **psta)
{
	u8 *ptr = precv_frame->u.hdr.rx_data;
	struct rx_pkt_attrib *pattrib = &precv_frame->u.hdr.attrib;
	signed int ret = _SUCCESS;
	struct sta_priv *pstapriv = &adapter->stapriv;
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	u8 *mybssid  = get_bssid(pmlmepriv);
	u8 *myhwaddr = myid(&adapter->eeprompriv);
	signed int bmcast = is_multicast_ether_addr(pattrib->dst);

	if ((check_fwstate(pmlmepriv, WIFI_STATION_STATE) == true) &&
	    (check_fwstate(pmlmepriv, _FW_LINKED) == true ||
	     check_fwstate(pmlmepriv, _FW_UNDER_LINKING) == true)
		) {

		/*  filter packets that SA is myself or multicast or broadcast */
		if (!memcmp(myhwaddr, pattrib->src, ETH_ALEN)) {
			ret = _FAIL;
			goto exit;
		}

		/*  da should be for me */
		if ((memcmp(myhwaddr, pattrib->dst, ETH_ALEN)) && (!bmcast)) {
			ret = _FAIL;
			goto exit;
		}


		/*  check BSSID */
		if (is_zero_ether_addr(pattrib->bssid) ||
		    is_zero_ether_addr(mybssid) ||
		    (memcmp(pattrib->bssid, mybssid, ETH_ALEN))) {

			if (!bmcast)
				issue_deauth(adapter, pattrib->bssid, WLAN_REASON_CLASS3_FRAME_FROM_NONASSOC_STA);

			ret = _FAIL;
			goto exit;
		}

		if (bmcast)
			*psta = rtw_get_bcmc_stainfo(adapter);
		else
			*psta = rtw_get_stainfo(pstapriv, pattrib->bssid); /*  get ap_info */

		if (!*psta) {
			ret = _FAIL;
			goto exit;
		}

		if (GetFrameSubType(ptr) & BIT(6)) {
			/* No data, will not indicate to upper layer, temporily count it here */
			count_rx_stats(adapter, precv_frame, *psta);
			ret = RTW_RX_HANDLED;
			goto exit;
		}

	} else if ((check_fwstate(pmlmepriv, WIFI_MP_STATE) == true) &&
		     (check_fwstate(pmlmepriv, _FW_LINKED) == true)) {
		memcpy(pattrib->dst, GetAddr1Ptr(ptr), ETH_ALEN);
		memcpy(pattrib->src, GetAddr2Ptr(ptr), ETH_ALEN);
		memcpy(pattrib->bssid, GetAddr3Ptr(ptr), ETH_ALEN);
		memcpy(pattrib->ra, pattrib->dst, ETH_ALEN);
		memcpy(pattrib->ta, pattrib->src, ETH_ALEN);

		/*  */
		memcpy(pattrib->bssid,  mybssid, ETH_ALEN);


		*psta = rtw_get_stainfo(pstapriv, pattrib->bssid); /*  get sta_info */
		if (!*psta) {
			ret = _FAIL;
			goto exit;
		}


	} else if (check_fwstate(pmlmepriv, WIFI_AP_STATE) == true) {
		/* Special case */
		ret = RTW_RX_HANDLED;
		goto exit;
	} else {
		if (!memcmp(myhwaddr, pattrib->dst, ETH_ALEN) && (!bmcast)) {
			*psta = rtw_get_stainfo(pstapriv, pattrib->bssid); /*  get sta_info */
			if (!*psta) {

				/* for AP multicast issue , modify by yiwei */
				static unsigned long send_issue_deauth_time;

				if (jiffies_to_msecs(jiffies - send_issue_deauth_time) > 10000 || send_issue_deauth_time == 0) {
					send_issue_deauth_time = jiffies;

					issue_deauth(adapter, pattrib->bssid, WLAN_REASON_CLASS3_FRAME_FROM_NONASSOC_STA);
				}
			}
		}

		ret = _FAIL;
	}

exit:
	return ret;
}

static signed int sta2ap_data_frame(struct adapter *adapter, union recv_frame *precv_frame,
		       struct sta_info **psta)
{
	u8 *ptr = precv_frame->u.hdr.rx_data;
	struct rx_pkt_attrib *pattrib = &precv_frame->u.hdr.attrib;
	struct sta_priv *pstapriv = &adapter->stapriv;
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	unsigned char *mybssid  = get_bssid(pmlmepriv);
	signed int ret = _SUCCESS;

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE) == true) {
		/* For AP mode, RA =BSSID, TX =STA(SRC_ADDR), A3 =DST_ADDR */
		if (memcmp(pattrib->bssid, mybssid, ETH_ALEN)) {
			ret = _FAIL;
			goto exit;
		}

		*psta = rtw_get_stainfo(pstapriv, pattrib->src);
		if (!*psta) {
			issue_deauth(adapter, pattrib->src, WLAN_REASON_CLASS3_FRAME_FROM_NONASSOC_STA);

			ret = RTW_RX_HANDLED;
			goto exit;
		}

		process_pwrbit_data(adapter, precv_frame);

		if ((GetFrameSubType(ptr) & WIFI_QOS_DATA_TYPE) == WIFI_QOS_DATA_TYPE)
			process_wmmps_data(adapter, precv_frame);

		if (GetFrameSubType(ptr) & BIT(6)) {
			/* No data, will not indicate to upper layer, temporily count it here */
			count_rx_stats(adapter, precv_frame, *psta);
			ret = RTW_RX_HANDLED;
			goto exit;
		}
	} else {
		u8 *myhwaddr = myid(&adapter->eeprompriv);

		if (memcmp(pattrib->ra, myhwaddr, ETH_ALEN)) {
			ret = RTW_RX_HANDLED;
			goto exit;
		}
		issue_deauth(adapter, pattrib->src, WLAN_REASON_CLASS3_FRAME_FROM_NONASSOC_STA);
		ret = RTW_RX_HANDLED;
		goto exit;
	}

exit:
	return ret;
}

static signed int validate_recv_ctrl_frame(struct adapter *padapter, union recv_frame *precv_frame)
{
	struct rx_pkt_attrib *pattrib = &precv_frame->u.hdr.attrib;
	struct sta_priv *pstapriv = &padapter->stapriv;
	u8 *pframe = precv_frame->u.hdr.rx_data;
	struct sta_info *psta = NULL;
	/* uint len = precv_frame->u.hdr.len; */

	if (GetFrameType(pframe) != WIFI_CTRL_TYPE)
		return _FAIL;

	/* receive the frames that ra(a1) is my address */
	if (memcmp(GetAddr1Ptr(pframe), myid(&padapter->eeprompriv), ETH_ALEN))
		return _FAIL;

	psta = rtw_get_stainfo(pstapriv, GetAddr2Ptr(pframe));
	if (!psta)
		return _FAIL;

	/* for rx pkt statistics */
	psta->sta_stats.rx_ctrl_pkts++;

	/* only handle ps-poll */
	if (GetFrameSubType(pframe) == WIFI_PSPOLL) {
		u16 aid;
		u8 wmmps_ac = 0;

		aid = GetAid(pframe);
		if (psta->aid != aid)
			return _FAIL;

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
			return _FAIL;

		if (psta->state & WIFI_STA_ALIVE_CHK_STATE) {
			psta->expire_to = pstapriv->expire_to;
			psta->state ^= WIFI_STA_ALIVE_CHK_STATE;
		}

		if ((psta->state&WIFI_SLEEP_STATE) && (pstapriv->sta_dz_bitmap&BIT(psta->aid))) {
			struct list_head	*xmitframe_plist, *xmitframe_phead;
			struct xmit_frame *pxmitframe = NULL;
			struct xmit_priv *pxmitpriv = &padapter->xmitpriv;

			/* spin_lock_bh(&psta->sleep_q.lock); */
			spin_lock_bh(&pxmitpriv->lock);

			xmitframe_phead = get_list_head(&psta->sleep_q);
			xmitframe_plist = get_next(xmitframe_phead);

			if (xmitframe_phead != xmitframe_plist) {
				pxmitframe = container_of(xmitframe_plist, struct xmit_frame, list);

				xmitframe_plist = get_next(xmitframe_plist);

				list_del_init(&pxmitframe->list);

				psta->sleepq_len--;

				if (psta->sleepq_len > 0)
					pxmitframe->attrib.mdata = 1;
				else
					pxmitframe->attrib.mdata = 0;

				pxmitframe->attrib.triggered = 1;

				rtw_hal_xmitframe_enqueue(padapter, pxmitframe);

				if (psta->sleepq_len == 0) {
					pstapriv->tim_bitmap &= ~BIT(psta->aid);

					/* update BCN for TIM IE */
					/* update_BCNTIM(padapter); */
					update_beacon(padapter, WLAN_EID_TIM, NULL, true);
				}

				/* spin_unlock_bh(&psta->sleep_q.lock); */
				spin_unlock_bh(&pxmitpriv->lock);

			} else {
				/* spin_unlock_bh(&psta->sleep_q.lock); */
				spin_unlock_bh(&pxmitpriv->lock);

				if (pstapriv->tim_bitmap&BIT(psta->aid)) {
					if (psta->sleepq_len == 0) {
						/* issue nulldata with More data bit = 0 to indicate we have no buffered packets */
						issue_nulldata_in_interrupt(padapter, psta->hwaddr);
					} else {
						psta->sleepq_len = 0;
					}

					pstapriv->tim_bitmap &= ~BIT(psta->aid);

					/* update BCN for TIM IE */
					/* update_BCNTIM(padapter); */
					update_beacon(padapter, WLAN_EID_TIM, NULL, true);
				}
			}
		}
	}

	return _FAIL;

}

/* perform defrag */
static union recv_frame *recvframe_defrag(struct adapter *adapter,
					  struct __queue *defrag_q)
{
	struct list_head	 *plist, *phead;
	u8  wlanhdr_offset;
	u8 curfragnum;
	struct recv_frame_hdr *pfhdr, *pnfhdr;
	union recv_frame *prframe, *pnextrframe;
	struct __queue	*pfree_recv_queue;

	curfragnum = 0;
	pfree_recv_queue = &adapter->recvpriv.free_recv_queue;

	phead = get_list_head(defrag_q);
	plist = get_next(phead);
	prframe = (union recv_frame *)plist;
	pfhdr = &prframe->u.hdr;
	list_del_init(&(prframe->u.list));

	if (curfragnum != pfhdr->attrib.frag_num) {
		/* the first fragment number must be 0 */
		/* free the whole queue */
		rtw_free_recvframe(prframe, pfree_recv_queue);
		rtw_free_recvframe_queue(defrag_q, pfree_recv_queue);

		return NULL;
	}

	curfragnum++;

	plist = get_list_head(defrag_q);

	plist = get_next(plist);

	while (phead != plist) {
		pnextrframe = (union recv_frame *)plist;
		pnfhdr = &pnextrframe->u.hdr;


		/* check the fragment sequence  (2nd ~n fragment frame) */

		if (curfragnum != pnfhdr->attrib.frag_num) {
			/* the fragment number must be increasing  (after decache) */
			/* release the defrag_q & prframe */
			rtw_free_recvframe(prframe, pfree_recv_queue);
			rtw_free_recvframe_queue(defrag_q, pfree_recv_queue);
			return NULL;
		}

		curfragnum++;

		/* copy the 2nd~n fragment frame's payload to the first fragment */
		/* get the 2nd~last fragment frame's payload */

		wlanhdr_offset = pnfhdr->attrib.hdrlen + pnfhdr->attrib.iv_len;

		recvframe_pull(pnextrframe, wlanhdr_offset);

		/* append  to first fragment frame's tail (if privacy frame, pull the ICV) */
		recvframe_pull_tail(prframe, pfhdr->attrib.icv_len);

		/* memcpy */
		memcpy(pfhdr->rx_tail, pnfhdr->rx_data, pnfhdr->len);

		recvframe_put(prframe, pnfhdr->len);

		pfhdr->attrib.icv_len = pnfhdr->attrib.icv_len;
		plist = get_next(plist);

	}

	/* free the defrag_q queue and return the prframe */
	rtw_free_recvframe_queue(defrag_q, pfree_recv_queue);

	return prframe;
}

/* check if need to defrag, if needed queue the frame to defrag_q */
static union recv_frame *recvframe_chk_defrag(struct adapter *padapter, union recv_frame *precv_frame)
{
	u8 ismfrag;
	u8 fragnum;
	u8 *psta_addr;
	struct recv_frame_hdr *pfhdr;
	struct sta_info *psta;
	struct sta_priv *pstapriv;
	struct list_head *phead;
	union recv_frame *prtnframe = NULL;
	struct __queue *pfree_recv_queue, *pdefrag_q;

	pstapriv = &padapter->stapriv;

	pfhdr = &precv_frame->u.hdr;

	pfree_recv_queue = &padapter->recvpriv.free_recv_queue;

	/* need to define struct of wlan header frame ctrl */
	ismfrag = pfhdr->attrib.mfrag;
	fragnum = pfhdr->attrib.frag_num;

	psta_addr = pfhdr->attrib.ta;
	psta = rtw_get_stainfo(pstapriv, psta_addr);
	if (!psta) {
		u8 type = GetFrameType(pfhdr->rx_data);

		if (type != WIFI_DATA_TYPE) {
			psta = rtw_get_bcmc_stainfo(padapter);
			pdefrag_q = &psta->sta_recvpriv.defrag_q;
		} else
			pdefrag_q = NULL;
	} else
		pdefrag_q = &psta->sta_recvpriv.defrag_q;

	if ((ismfrag == 0) && (fragnum == 0))
		prtnframe = precv_frame;/* isn't a fragment frame */

	if (ismfrag == 1) {
		/* 0~(n-1) fragment frame */
		/* enqueue to defraf_g */
		if (pdefrag_q) {
			if (fragnum == 0)
				/* the first fragment */
				if (!list_empty(&pdefrag_q->queue))
					/* free current defrag_q */
					rtw_free_recvframe_queue(pdefrag_q, pfree_recv_queue);


			/* Then enqueue the 0~(n-1) fragment into the defrag_q */

			/* spin_lock(&pdefrag_q->lock); */
			phead = get_list_head(pdefrag_q);
			list_add_tail(&pfhdr->list, phead);
			/* spin_unlock(&pdefrag_q->lock); */

			prtnframe = NULL;

		} else {
			/* can't find this ta's defrag_queue, so free this recv_frame */
			rtw_free_recvframe(precv_frame, pfree_recv_queue);
			prtnframe = NULL;
		}

	}

	if ((ismfrag == 0) && (fragnum != 0)) {
		/* the last fragment frame */
		/* enqueue the last fragment */
		if (pdefrag_q) {
			/* spin_lock(&pdefrag_q->lock); */
			phead = get_list_head(pdefrag_q);
			list_add_tail(&pfhdr->list, phead);
			/* spin_unlock(&pdefrag_q->lock); */

			/* call recvframe_defrag to defrag */
			precv_frame = recvframe_defrag(padapter, pdefrag_q);
			prtnframe = precv_frame;

		} else {
			/* can't find this ta's defrag_queue, so free this recv_frame */
			rtw_free_recvframe(precv_frame, pfree_recv_queue);
			prtnframe = NULL;
		}

	}


	if ((prtnframe) && (prtnframe->u.hdr.attrib.privacy)) {
		/* after defrag we must check tkip mic code */
		if (recvframe_chkmic(padapter,  prtnframe) == _FAIL) {
			rtw_free_recvframe(prtnframe, pfree_recv_queue);
			prtnframe = NULL;
		}
	}
	return prtnframe;
}

static signed int validate_recv_mgnt_frame(struct adapter *padapter, union recv_frame *precv_frame)
{
	/* struct mlme_priv *pmlmepriv = &adapter->mlmepriv; */

	precv_frame = recvframe_chk_defrag(padapter, precv_frame);
	if (!precv_frame)
		return _SUCCESS;

	{
		/* for rx pkt statistics */
		struct sta_info *psta = rtw_get_stainfo(&padapter->stapriv, GetAddr2Ptr(precv_frame->u.hdr.rx_data));

		if (psta) {
			psta->sta_stats.rx_mgnt_pkts++;
			if (GetFrameSubType(precv_frame->u.hdr.rx_data) == WIFI_BEACON)
				psta->sta_stats.rx_beacon_pkts++;
			else if (GetFrameSubType(precv_frame->u.hdr.rx_data) == WIFI_PROBEREQ)
				psta->sta_stats.rx_probereq_pkts++;
			else if (GetFrameSubType(precv_frame->u.hdr.rx_data) == WIFI_PROBERSP) {
				if (!memcmp(padapter->eeprompriv.mac_addr, GetAddr1Ptr(precv_frame->u.hdr.rx_data), ETH_ALEN))
					psta->sta_stats.rx_probersp_pkts++;
				else if (is_broadcast_mac_addr(GetAddr1Ptr(precv_frame->u.hdr.rx_data)) ||
					 is_multicast_mac_addr(GetAddr1Ptr(precv_frame->u.hdr.rx_data)))
					psta->sta_stats.rx_probersp_bm_pkts++;
				else
					psta->sta_stats.rx_probersp_uo_pkts++;
			}
		}
	}

	mgt_dispatcher(padapter, precv_frame);

	return _SUCCESS;

}

static signed int validate_recv_data_frame(struct adapter *adapter, union recv_frame *precv_frame)
{
	u8 bretry;
	u8 *psa, *pda, *pbssid;
	struct sta_info *psta = NULL;
	u8 *ptr = precv_frame->u.hdr.rx_data;
	struct rx_pkt_attrib	*pattrib = &precv_frame->u.hdr.attrib;
	struct security_priv *psecuritypriv = &adapter->securitypriv;
	signed int ret = _SUCCESS;

	bretry = GetRetry(ptr);
	pda = get_da(ptr);
	psa = get_sa(ptr);
	pbssid = get_hdr_bssid(ptr);

	if (!pbssid) {
		ret = _FAIL;
		goto exit;
	}

	memcpy(pattrib->dst, pda, ETH_ALEN);
	memcpy(pattrib->src, psa, ETH_ALEN);

	memcpy(pattrib->bssid, pbssid, ETH_ALEN);

	switch (pattrib->to_fr_ds) {
	case 0:
		memcpy(pattrib->ra, pda, ETH_ALEN);
		memcpy(pattrib->ta, psa, ETH_ALEN);
		ret = sta2sta_data_frame(adapter, precv_frame, &psta);
		break;

	case 1:
		memcpy(pattrib->ra, pda, ETH_ALEN);
		memcpy(pattrib->ta, pbssid, ETH_ALEN);
		ret = ap2sta_data_frame(adapter, precv_frame, &psta);
		break;

	case 2:
		memcpy(pattrib->ra, pbssid, ETH_ALEN);
		memcpy(pattrib->ta, psa, ETH_ALEN);
		ret = sta2ap_data_frame(adapter, precv_frame, &psta);
		break;

	case 3:
		memcpy(pattrib->ra, GetAddr1Ptr(ptr), ETH_ALEN);
		memcpy(pattrib->ta, GetAddr2Ptr(ptr), ETH_ALEN);
		ret = _FAIL;
		break;

	default:
		ret = _FAIL;
		break;

	}

	if (ret == _FAIL) {
		goto exit;
	} else if (ret == RTW_RX_HANDLED) {
		goto exit;
	}


	if (!psta) {
		ret = _FAIL;
		goto exit;
	}

	/* psta->rssi = prxcmd->rssi; */
	/* psta->signal_quality = prxcmd->sq; */
	precv_frame->u.hdr.psta = psta;


	pattrib->amsdu = 0;
	pattrib->ack_policy = 0;
	/* parsing QC field */
	if (pattrib->qos == 1) {
		pattrib->priority = GetPriority((ptr + 24));
		pattrib->ack_policy = GetAckpolicy((ptr + 24));
		pattrib->amsdu = GetAMsdu((ptr + 24));
		pattrib->hdrlen = pattrib->to_fr_ds == 3 ? 32 : 26;

		if (pattrib->priority != 0 && pattrib->priority != 3)
			adapter->recvpriv.bIsAnyNonBEPkts = true;

	} else {
		pattrib->priority = 0;
		pattrib->hdrlen = pattrib->to_fr_ds == 3 ? 30 : 24;
	}


	if (pattrib->order)/* HT-CTRL 11n */
		pattrib->hdrlen += 4;

	precv_frame->u.hdr.preorder_ctrl = &psta->recvreorder_ctrl[pattrib->priority];

	/*  decache, drop duplicate recv packets */
	if (recv_decache(precv_frame, bretry, &psta->sta_recvpriv.rxcache) == _FAIL) {
		ret = _FAIL;
		goto exit;
	}

	if (pattrib->privacy) {
		GET_ENCRY_ALGO(psecuritypriv, psta, pattrib->encrypt, is_multicast_ether_addr(pattrib->ra));

		SET_ICE_IV_LEN(pattrib->iv_len, pattrib->icv_len, pattrib->encrypt);
	} else {
		pattrib->encrypt = 0;
		pattrib->iv_len = pattrib->icv_len = 0;
	}

exit:
	return ret;
}

static signed int validate_80211w_mgmt(struct adapter *adapter, union recv_frame *precv_frame)
{
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	struct rx_pkt_attrib *pattrib = &precv_frame->u.hdr.attrib;
	u8 *ptr = precv_frame->u.hdr.rx_data;
	u8 subtype;

	subtype = GetFrameSubType(ptr); /* bit(7)~bit(2) */

	/* only support station mode */
	if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) && check_fwstate(pmlmepriv, _FW_LINKED) &&
	    adapter->securitypriv.binstallBIPkey == true) {
		/* unicast management frame decrypt */
		if (pattrib->privacy && !(is_multicast_ether_addr(GetAddr1Ptr(ptr))) &&
			(subtype == WIFI_DEAUTH || subtype == WIFI_DISASSOC || subtype == WIFI_ACTION)) {
			u8 *mgmt_DATA;
			u32 data_len = 0;

			pattrib->bdecrypted = 0;
			pattrib->encrypt = _AES_;
			pattrib->hdrlen = sizeof(struct ieee80211_hdr_3addr);
			/* set iv and icv length */
			SET_ICE_IV_LEN(pattrib->iv_len, pattrib->icv_len, pattrib->encrypt);
			memcpy(pattrib->ra, GetAddr1Ptr(ptr), ETH_ALEN);
			memcpy(pattrib->ta, GetAddr2Ptr(ptr), ETH_ALEN);
			/* actual management data frame body */
			data_len = pattrib->pkt_len - pattrib->hdrlen - pattrib->iv_len - pattrib->icv_len;
			mgmt_DATA = rtw_zmalloc(data_len);
			if (!mgmt_DATA)
				goto validate_80211w_fail;
			precv_frame = decryptor(adapter, precv_frame);
			/* save actual management data frame body */
			memcpy(mgmt_DATA, ptr+pattrib->hdrlen+pattrib->iv_len, data_len);
			/* overwrite the iv field */
			memcpy(ptr+pattrib->hdrlen, mgmt_DATA, data_len);
			/* remove the iv and icv length */
			pattrib->pkt_len = pattrib->pkt_len - pattrib->iv_len - pattrib->icv_len;
			kfree(mgmt_DATA);
			if (!precv_frame)
				goto validate_80211w_fail;
		} else if (is_multicast_ether_addr(GetAddr1Ptr(ptr)) &&
			(subtype == WIFI_DEAUTH || subtype == WIFI_DISASSOC)) {
			signed int BIP_ret = _SUCCESS;
			/* verify BIP MME IE of broadcast/multicast de-auth/disassoc packet */
			BIP_ret = rtw_BIP_verify(adapter, (u8 *)precv_frame);
			if (BIP_ret == _FAIL) {
				goto validate_80211w_fail;
			} else if (BIP_ret == RTW_RX_HANDLED) {
				/* issue sa query request */
				issue_action_SA_Query(adapter, NULL, 0, 0);
				goto validate_80211w_fail;
			}
		} else { /* 802.11w protect */
			if (subtype == WIFI_ACTION) {
				/* according 802.11-2012 standard, these five types are not robust types */
				if (ptr[WLAN_HDR_A3_LEN] != RTW_WLAN_CATEGORY_PUBLIC          &&
					ptr[WLAN_HDR_A3_LEN] != RTW_WLAN_CATEGORY_HT              &&
					ptr[WLAN_HDR_A3_LEN] != RTW_WLAN_CATEGORY_UNPROTECTED_WNM &&
					ptr[WLAN_HDR_A3_LEN] != RTW_WLAN_CATEGORY_SELF_PROTECTED  &&
					ptr[WLAN_HDR_A3_LEN] != RTW_WLAN_CATEGORY_P2P) {
					goto validate_80211w_fail;
				}
			} else if (subtype == WIFI_DEAUTH || subtype == WIFI_DISASSOC) {
				/* issue sa query request */
				issue_action_SA_Query(adapter, NULL, 0, 0);
				goto validate_80211w_fail;
			}
		}
	}
	return _SUCCESS;

validate_80211w_fail:
	return _FAIL;

}

static signed int validate_recv_frame(struct adapter *adapter, union recv_frame *precv_frame)
{
	/* shall check frame subtype, to / from ds, da, bssid */

	/* then call check if rx seq/frag. duplicated. */

	u8 type;
	u8 subtype;
	signed int retval = _SUCCESS;
	u8 bDumpRxPkt;

	struct rx_pkt_attrib *pattrib = &precv_frame->u.hdr.attrib;

	u8 *ptr = precv_frame->u.hdr.rx_data;
	u8  ver = (unsigned char) (*ptr)&0x3;

	/* add version chk */
	if (ver != 0) {
		retval = _FAIL;
		goto exit;
	}

	type =  GetFrameType(ptr);
	subtype = GetFrameSubType(ptr); /* bit(7)~bit(2) */

	pattrib->to_fr_ds = get_tofr_ds(ptr);

	pattrib->frag_num = GetFragNum(ptr);
	pattrib->seq_num = GetSequence(ptr);

	pattrib->pw_save = GetPwrMgt(ptr);
	pattrib->mfrag = GetMFrag(ptr);
	pattrib->mdata = GetMData(ptr);
	pattrib->privacy = GetPrivacy(ptr);
	pattrib->order = GetOrder(ptr);
	rtw_hal_get_def_var(adapter, HAL_DEF_DBG_DUMP_RXPKT, &(bDumpRxPkt));

	switch (type) {
	case WIFI_MGT_TYPE: /* mgnt */
		if (validate_80211w_mgmt(adapter, precv_frame) == _FAIL) {
			retval = _FAIL;
			break;
		}

		retval = validate_recv_mgnt_frame(adapter, precv_frame);
		retval = _FAIL; /*  only data frame return _SUCCESS */
		break;
	case WIFI_CTRL_TYPE: /* ctrl */
		retval = validate_recv_ctrl_frame(adapter, precv_frame);
		retval = _FAIL; /*  only data frame return _SUCCESS */
		break;
	case WIFI_DATA_TYPE: /* data */
		pattrib->qos = (subtype & BIT(7)) ? 1:0;
		retval = validate_recv_data_frame(adapter, precv_frame);
		if (retval == _FAIL) {
			struct recv_priv *precvpriv = &adapter->recvpriv;

			precvpriv->rx_drop++;
		} else if (retval == _SUCCESS) {
#ifdef DBG_RX_DUMP_EAP
			u8 bDumpRxPkt;
			u16 eth_type;

			/*  dump eapol */
			rtw_hal_get_def_var(adapter, HAL_DEF_DBG_DUMP_RXPKT, &(bDumpRxPkt));
			/*  get ether_type */
			memcpy(&eth_type, ptr + pattrib->hdrlen + pattrib->iv_len + LLC_HEADER_LENGTH, 2);
			eth_type = ntohs((unsigned short) eth_type);
#endif
		}
		break;
	default:
		retval = _FAIL;
		break;
	}

exit:
	return retval;
}

/* remove the wlanhdr and add the eth_hdr */
static signed int wlanhdr_to_ethhdr(union recv_frame *precvframe)
{
	signed int	rmv_len;
	u16 eth_type, len;
	u8 bsnaphdr;
	u8 *psnap_type;
	struct ieee80211_snap_hdr	*psnap;
	__be16 be_tmp;
	struct adapter			*adapter = precvframe->u.hdr.adapter;
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	u8 *ptr = precvframe->u.hdr.rx_data; /*  point to frame_ctrl field */
	struct rx_pkt_attrib *pattrib = &precvframe->u.hdr.attrib;

	if (pattrib->encrypt)
		recvframe_pull_tail(precvframe, pattrib->icv_len);

	psnap = (struct ieee80211_snap_hdr	*)(ptr+pattrib->hdrlen + pattrib->iv_len);
	psnap_type = ptr+pattrib->hdrlen + pattrib->iv_len+SNAP_SIZE;
	/* convert hdr + possible LLC headers into Ethernet header */
	/* eth_type = (psnap_type[0] << 8) | psnap_type[1]; */
	if ((!memcmp(psnap, rfc1042_header, SNAP_SIZE) &&
		(memcmp(psnap_type, SNAP_ETH_TYPE_IPX, 2)) &&
		(memcmp(psnap_type, SNAP_ETH_TYPE_APPLETALK_AARP, 2))) ||
		/* eth_type != ETH_P_AARP && eth_type != ETH_P_IPX) || */
		 !memcmp(psnap, bridge_tunnel_header, SNAP_SIZE)) {
		/* remove RFC1042 or Bridge-Tunnel encapsulation and replace EtherType */
		bsnaphdr = true;
	} else
		/* Leave Ethernet header part of hdr and full payload */
		bsnaphdr = false;

	rmv_len = pattrib->hdrlen + pattrib->iv_len + (bsnaphdr?SNAP_SIZE:0);
	len = precvframe->u.hdr.len - rmv_len;

	memcpy(&be_tmp, ptr+rmv_len, 2);
	eth_type = ntohs(be_tmp); /* pattrib->ether_type */
	pattrib->eth_type = eth_type;

	if ((check_fwstate(pmlmepriv, WIFI_MP_STATE) == true)) {
		ptr += rmv_len;
		*ptr = 0x87;
		*(ptr+1) = 0x12;

		eth_type = 0x8712;
		/*  append rx status for mp test packets */
		ptr = recvframe_pull(precvframe, (rmv_len-sizeof(struct ethhdr)+2)-24);
		if (!ptr)
			return _FAIL;
		memcpy(ptr, get_rxmem(precvframe), 24);
		ptr += 24;
	} else {
		ptr = recvframe_pull(precvframe, (rmv_len-sizeof(struct ethhdr) + (bsnaphdr?2:0)));
		if (!ptr)
			return _FAIL;
	}

	memcpy(ptr, pattrib->dst, ETH_ALEN);
	memcpy(ptr+ETH_ALEN, pattrib->src, ETH_ALEN);

	if (!bsnaphdr) {
		be_tmp = htons(len);
		memcpy(ptr+12, &be_tmp, 2);
	}

	return _SUCCESS;
}

static int amsdu_to_msdu(struct adapter *padapter, union recv_frame *prframe)
{
	int	a_len, padding_len;
	u16 nSubframe_Length;
	u8 nr_subframes, i;
	u8 *pdata;
	struct sk_buff *sub_pkt, *subframes[MAX_SUBFRAME_COUNT];
	struct recv_priv *precvpriv = &padapter->recvpriv;
	struct __queue *pfree_recv_queue = &(precvpriv->free_recv_queue);

	nr_subframes = 0;

	recvframe_pull(prframe, prframe->u.hdr.attrib.hdrlen);

	if (prframe->u.hdr.attrib.iv_len > 0)
		recvframe_pull(prframe, prframe->u.hdr.attrib.iv_len);

	a_len = prframe->u.hdr.len;

	pdata = prframe->u.hdr.rx_data;

	while (a_len > ETH_HLEN) {

		/* Offset 12 denote 2 mac address */
		nSubframe_Length = get_unaligned_be16(pdata + 12);

		if (a_len < ETH_HLEN + nSubframe_Length)
			break;

		sub_pkt = rtw_os_alloc_msdu_pkt(prframe, nSubframe_Length, pdata);
		if (!sub_pkt)
			break;

		/* move the data point to data content */
		pdata += ETH_HLEN;
		a_len -= ETH_HLEN;

		subframes[nr_subframes++] = sub_pkt;

		if (nr_subframes >= MAX_SUBFRAME_COUNT)
			break;

		pdata += nSubframe_Length;
		a_len -= nSubframe_Length;
		if (a_len != 0) {
			padding_len = 4 - ((nSubframe_Length + ETH_HLEN) & (4-1));
			if (padding_len == 4)
				padding_len = 0;

			if (a_len < padding_len)
				break;

			pdata += padding_len;
			a_len -= padding_len;
		}
	}

	for (i = 0; i < nr_subframes; i++) {
		sub_pkt = subframes[i];

		/* Indicate the packets to upper layer */
		if (sub_pkt)
			rtw_os_recv_indicate_pkt(padapter, sub_pkt, &prframe->u.hdr.attrib);
	}

	prframe->u.hdr.len = 0;
	rtw_free_recvframe(prframe, pfree_recv_queue);/* free this recv_frame */

	return  _SUCCESS;
}

static int check_indicate_seq(struct recv_reorder_ctrl *preorder_ctrl, u16 seq_num)
{
	struct adapter *padapter = preorder_ctrl->padapter;
	struct dvobj_priv *psdpriv = padapter->dvobj;
	struct debug_priv *pdbgpriv = &psdpriv->drv_dbg;
	u8 wsize = preorder_ctrl->wsize_b;
	u16 wend = (preorder_ctrl->indicate_seq + wsize - 1) & 0xFFF;/*  4096; */

	/*  Rx Reorder initialize condition. */
	if (preorder_ctrl->indicate_seq == 0xFFFF)
		preorder_ctrl->indicate_seq = seq_num;

	/*  Drop out the packet which SeqNum is smaller than WinStart */
	if (SN_LESS(seq_num, preorder_ctrl->indicate_seq))
		return false;

	/*  */
	/*  Sliding window manipulation. Conditions includes: */
	/*  1. Incoming SeqNum is equal to WinStart =>Window shift 1 */
	/*  2. Incoming SeqNum is larger than the WinEnd => Window shift N */
	/*  */
	if (SN_EQUAL(seq_num, preorder_ctrl->indicate_seq)) {
		preorder_ctrl->indicate_seq = (preorder_ctrl->indicate_seq + 1) & 0xFFF;

	} else if (SN_LESS(wend, seq_num)) {
		/*  boundary situation, when seq_num cross 0xFFF */
		if (seq_num >= (wsize - 1))
			preorder_ctrl->indicate_seq = seq_num + 1 - wsize;
		else
			preorder_ctrl->indicate_seq = 0xFFF - (wsize - (seq_num + 1)) + 1;
		pdbgpriv->dbg_rx_ampdu_window_shift_cnt++;
	}

	return true;
}

static int enqueue_reorder_recvframe(struct recv_reorder_ctrl *preorder_ctrl, union recv_frame *prframe)
{
	struct rx_pkt_attrib *pattrib = &prframe->u.hdr.attrib;
	struct __queue *ppending_recvframe_queue = &preorder_ctrl->pending_recvframe_queue;
	struct list_head	*phead, *plist;
	union recv_frame *pnextrframe;
	struct rx_pkt_attrib *pnextattrib;

	/* spin_lock_irqsave(&ppending_recvframe_queue->lock, irql); */
	/* spin_lock(&ppending_recvframe_queue->lock); */


	phead = get_list_head(ppending_recvframe_queue);
	plist = get_next(phead);

	while (phead != plist) {
		pnextrframe = (union recv_frame *)plist;
		pnextattrib = &pnextrframe->u.hdr.attrib;

		if (SN_LESS(pnextattrib->seq_num, pattrib->seq_num))
			plist = get_next(plist);
		else if (SN_EQUAL(pnextattrib->seq_num, pattrib->seq_num))
			/* Duplicate entry is found!! Do not insert current entry. */
			/* spin_unlock_irqrestore(&ppending_recvframe_queue->lock, irql); */
			return false;
		else
			break;

	}


	/* spin_lock_irqsave(&ppending_recvframe_queue->lock, irql); */
	/* spin_lock(&ppending_recvframe_queue->lock); */

	list_del_init(&(prframe->u.hdr.list));

	list_add_tail(&(prframe->u.hdr.list), plist);

	/* spin_unlock(&ppending_recvframe_queue->lock); */
	/* spin_unlock_irqrestore(&ppending_recvframe_queue->lock, irql); */

	return true;

}

static void recv_indicatepkts_pkt_loss_cnt(struct debug_priv *pdbgpriv, u64 prev_seq, u64 current_seq)
{
	if (current_seq < prev_seq)
		pdbgpriv->dbg_rx_ampdu_loss_count += (4096 + current_seq - prev_seq);
	else
		pdbgpriv->dbg_rx_ampdu_loss_count += (current_seq - prev_seq);

}

static int recv_indicatepkts_in_order(struct adapter *padapter, struct recv_reorder_ctrl *preorder_ctrl, int bforced)
{
	struct list_head	*phead, *plist;
	union recv_frame *prframe;
	struct rx_pkt_attrib *pattrib;
	/* u8 index = 0; */
	int bPktInBuf = false;
	struct recv_priv *precvpriv = &padapter->recvpriv;
	struct __queue *ppending_recvframe_queue = &preorder_ctrl->pending_recvframe_queue;
	struct dvobj_priv *psdpriv = padapter->dvobj;
	struct debug_priv *pdbgpriv = &psdpriv->drv_dbg;

	/* spin_lock_irqsave(&ppending_recvframe_queue->lock, irql); */
	/* spin_lock(&ppending_recvframe_queue->lock); */

	phead =		get_list_head(ppending_recvframe_queue);
	plist = get_next(phead);

	/*  Handling some condition for forced indicate case. */
	if (bforced == true) {
		pdbgpriv->dbg_rx_ampdu_forced_indicate_count++;
		if (list_empty(phead)) {
			/*  spin_unlock_irqrestore(&ppending_recvframe_queue->lock, irql); */
			/* spin_unlock(&ppending_recvframe_queue->lock); */
			return true;
		}

		prframe = (union recv_frame *)plist;
		pattrib = &prframe->u.hdr.attrib;

		recv_indicatepkts_pkt_loss_cnt(pdbgpriv, preorder_ctrl->indicate_seq, pattrib->seq_num);
		preorder_ctrl->indicate_seq = pattrib->seq_num;

	}

	/*  Prepare indication list and indication. */
	/*  Check if there is any packet need indicate. */
	while (!list_empty(phead)) {

		prframe = (union recv_frame *)plist;
		pattrib = &prframe->u.hdr.attrib;

		if (!SN_LESS(preorder_ctrl->indicate_seq, pattrib->seq_num)) {
			plist = get_next(plist);
			list_del_init(&(prframe->u.hdr.list));

			if (SN_EQUAL(preorder_ctrl->indicate_seq, pattrib->seq_num))
				preorder_ctrl->indicate_seq = (preorder_ctrl->indicate_seq + 1) & 0xFFF;

			/* Set this as a lock to make sure that only one thread is indicating packet. */
			/* pTS->RxIndicateState = RXTS_INDICATE_PROCESSING; */

			/*  Indicate packets */

			/* indicate this recv_frame */
			if (!pattrib->amsdu) {
				if ((padapter->bDriverStopped == false) &&
				    (padapter->bSurpriseRemoved == false))
					rtw_recv_indicatepkt(padapter, prframe);/* indicate this recv_frame */

			} else if (pattrib->amsdu == 1) {
				if (amsdu_to_msdu(padapter, prframe) != _SUCCESS)
					rtw_free_recvframe(prframe, &precvpriv->free_recv_queue);

			} else {
				/* error condition; */
			}


			/* Update local variables. */
			bPktInBuf = false;

		} else {
			bPktInBuf = true;
			break;
		}

	}

	/* spin_unlock(&ppending_recvframe_queue->lock); */
	/* spin_unlock_irqrestore(&ppending_recvframe_queue->lock, irql); */

	return bPktInBuf;
}

static int recv_indicatepkt_reorder(struct adapter *padapter, union recv_frame *prframe)
{
	int retval = _SUCCESS;
	struct rx_pkt_attrib *pattrib = &prframe->u.hdr.attrib;
	struct recv_reorder_ctrl *preorder_ctrl = prframe->u.hdr.preorder_ctrl;
	struct __queue *ppending_recvframe_queue = &preorder_ctrl->pending_recvframe_queue;
	struct dvobj_priv *psdpriv = padapter->dvobj;
	struct debug_priv *pdbgpriv = &psdpriv->drv_dbg;

	if (!pattrib->amsdu) {
		/* s1. */
		wlanhdr_to_ethhdr(prframe);

		if (pattrib->qos != 1) {
			if ((padapter->bDriverStopped == false) &&
			    (padapter->bSurpriseRemoved == false)) {
				rtw_recv_indicatepkt(padapter, prframe);
				return _SUCCESS;

			}

			return _FAIL;

		}

		if (preorder_ctrl->enable == false) {
			/* indicate this recv_frame */
			preorder_ctrl->indicate_seq = pattrib->seq_num;

			rtw_recv_indicatepkt(padapter, prframe);

			preorder_ctrl->indicate_seq = (preorder_ctrl->indicate_seq + 1)%4096;

			return _SUCCESS;
		}
	} else if (pattrib->amsdu == 1) { /* temp filter -> means didn't support A-MSDUs in a A-MPDU */
		if (preorder_ctrl->enable == false) {
			preorder_ctrl->indicate_seq = pattrib->seq_num;

			retval = amsdu_to_msdu(padapter, prframe);

			preorder_ctrl->indicate_seq = (preorder_ctrl->indicate_seq + 1)%4096;

			if (retval != _SUCCESS) {
			}

			return retval;
		}
	}

	spin_lock_bh(&ppending_recvframe_queue->lock);

	/* s2. check if winstart_b(indicate_seq) needs to been updated */
	if (!check_indicate_seq(preorder_ctrl, pattrib->seq_num)) {
		pdbgpriv->dbg_rx_ampdu_drop_count++;
		goto _err_exit;
	}


	/* s3. Insert all packet into Reorder Queue to maintain its ordering. */
	if (!enqueue_reorder_recvframe(preorder_ctrl, prframe)) {
		/* spin_unlock_irqrestore(&ppending_recvframe_queue->lock, irql); */
		/* return _FAIL; */
		goto _err_exit;
	}


	/* s4. */
	/*  Indication process. */
	/*  After Packet dropping and Sliding Window shifting as above, we can now just indicate the packets */
	/*  with the SeqNum smaller than latest WinStart and buffer other packets. */
	/*  */
	/*  For Rx Reorder condition: */
	/*  1. All packets with SeqNum smaller than WinStart => Indicate */
	/*  2. All packets with SeqNum larger than or equal to WinStart => Buffer it. */
	/*  */

	/* recv_indicatepkts_in_order(padapter, preorder_ctrl, true); */
	if (recv_indicatepkts_in_order(padapter, preorder_ctrl, false) == true) {
		_set_timer(&preorder_ctrl->reordering_ctrl_timer, REORDER_WAIT_TIME);
		spin_unlock_bh(&ppending_recvframe_queue->lock);
	} else {
		spin_unlock_bh(&ppending_recvframe_queue->lock);
		del_timer_sync(&preorder_ctrl->reordering_ctrl_timer);
	}

	return _SUCCESS;

_err_exit:
	spin_unlock_bh(&ppending_recvframe_queue->lock);

	return _FAIL;
}


void rtw_reordering_ctrl_timeout_handler(struct timer_list *t)
{
	struct recv_reorder_ctrl *preorder_ctrl =
		from_timer(preorder_ctrl, t, reordering_ctrl_timer);
	struct adapter *padapter = preorder_ctrl->padapter;
	struct __queue *ppending_recvframe_queue = &preorder_ctrl->pending_recvframe_queue;


	if (padapter->bDriverStopped || padapter->bSurpriseRemoved)
		return;

	spin_lock_bh(&ppending_recvframe_queue->lock);

	if (recv_indicatepkts_in_order(padapter, preorder_ctrl, true) == true)
		_set_timer(&preorder_ctrl->reordering_ctrl_timer, REORDER_WAIT_TIME);

	spin_unlock_bh(&ppending_recvframe_queue->lock);

}

static int process_recv_indicatepkts(struct adapter *padapter, union recv_frame *prframe)
{
	int retval = _SUCCESS;
	/* struct recv_priv *precvpriv = &padapter->recvpriv; */
	/* struct rx_pkt_attrib *pattrib = &prframe->u.hdr.attrib; */
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct ht_priv *phtpriv = &pmlmepriv->htpriv;

	if (phtpriv->ht_option == true) { /* B/G/N Mode */
		/* prframe->u.hdr.preorder_ctrl = &precvpriv->recvreorder_ctrl[pattrib->priority]; */

		if (recv_indicatepkt_reorder(padapter, prframe) != _SUCCESS) { /*  including perform A-MPDU Rx Ordering Buffer Control */

			if ((padapter->bDriverStopped == false) &&
			    (padapter->bSurpriseRemoved == false)) {
				retval = _FAIL;
				return retval;
			}
		}
	} else { /* B/G mode */
		retval = wlanhdr_to_ethhdr(prframe);
		if (retval != _SUCCESS)
			return retval;

		if ((padapter->bDriverStopped == false) && (padapter->bSurpriseRemoved == false)) {
			/* indicate this recv_frame */
			rtw_recv_indicatepkt(padapter, prframe);
		} else {
			retval = _FAIL;
			return retval;
		}

	}

	return retval;

}

static int recv_func_prehandle(struct adapter *padapter, union recv_frame *rframe)
{
	int ret = _SUCCESS;
	struct __queue *pfree_recv_queue = &padapter->recvpriv.free_recv_queue;

	/* check the frame crtl field and decache */
	ret = validate_recv_frame(padapter, rframe);
	if (ret != _SUCCESS) {
		rtw_free_recvframe(rframe, pfree_recv_queue);/* free this recv_frame */
		goto exit;
	}

exit:
	return ret;
}

static int recv_func_posthandle(struct adapter *padapter, union recv_frame *prframe)
{
	int ret = _SUCCESS;
	union recv_frame *orig_prframe = prframe;
	struct recv_priv *precvpriv = &padapter->recvpriv;
	struct __queue *pfree_recv_queue = &padapter->recvpriv.free_recv_queue;

	prframe = decryptor(padapter, prframe);
	if (!prframe) {
		ret = _FAIL;
		goto _recv_data_drop;
	}

	prframe = recvframe_chk_defrag(padapter, prframe);
	if (!prframe)
		goto _recv_data_drop;

	prframe = portctrl(padapter, prframe);
	if (!prframe) {
		ret = _FAIL;
		goto _recv_data_drop;
	}

	count_rx_stats(padapter, prframe, NULL);

	ret = process_recv_indicatepkts(padapter, prframe);
	if (ret != _SUCCESS) {
		rtw_free_recvframe(orig_prframe, pfree_recv_queue);/* free this recv_frame */
		goto _recv_data_drop;
	}

_recv_data_drop:
	precvpriv->rx_drop++;
	return ret;
}

static int recv_func(struct adapter *padapter, union recv_frame *rframe)
{
	int ret;
	struct rx_pkt_attrib *prxattrib = &rframe->u.hdr.attrib;
	struct recv_priv *recvpriv = &padapter->recvpriv;
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	struct mlme_priv *mlmepriv = &padapter->mlmepriv;

	/* check if need to handle uc_swdec_pending_queue*/
	if (check_fwstate(mlmepriv, WIFI_STATION_STATE) && psecuritypriv->busetkipkey) {
		union recv_frame *pending_frame;

		while ((pending_frame = rtw_alloc_recvframe(&padapter->recvpriv.uc_swdec_pending_queue)))
			recv_func_posthandle(padapter, pending_frame);
	}

	ret = recv_func_prehandle(padapter, rframe);

	if (ret == _SUCCESS) {

		/* check if need to enqueue into uc_swdec_pending_queue*/
		if (check_fwstate(mlmepriv, WIFI_STATION_STATE) &&
			!is_multicast_ether_addr(prxattrib->ra) && prxattrib->encrypt > 0 &&
			(prxattrib->bdecrypted == 0 || psecuritypriv->sw_decrypt == true) &&
			psecuritypriv->ndisauthtype == Ndis802_11AuthModeWPAPSK &&
			!psecuritypriv->busetkipkey) {
			rtw_enqueue_recvframe(rframe, &padapter->recvpriv.uc_swdec_pending_queue);

			if (recvpriv->free_recvframe_cnt < NR_RECVFRAME/4) {
				/* to prevent from recvframe starvation, get recvframe from uc_swdec_pending_queue to free_recvframe_cnt  */
				rframe = rtw_alloc_recvframe(&padapter->recvpriv.uc_swdec_pending_queue);
				if (rframe)
					goto do_posthandle;
			}
			goto exit;
		}

do_posthandle:
		ret = recv_func_posthandle(padapter, rframe);
	}

exit:
	return ret;
}


s32 rtw_recv_entry(union recv_frame *precvframe)
{
	struct adapter *padapter;
	struct recv_priv *precvpriv;
	s32 ret = _SUCCESS;

	padapter = precvframe->u.hdr.adapter;

	precvpriv = &padapter->recvpriv;

	ret = recv_func(padapter, precvframe);
	if (ret == _FAIL)
		goto _recv_entry_drop;

	precvpriv->rx_pkts++;

	return ret;

_recv_entry_drop:

	return ret;
}

static void rtw_signal_stat_timer_hdl(struct timer_list *t)
{
	struct adapter *adapter =
		from_timer(adapter, t, recvpriv.signal_stat_timer);
	struct recv_priv *recvpriv = &adapter->recvpriv;

	u32 tmp_s, tmp_q;
	u8 avg_signal_strength = 0;
	u8 avg_signal_qual = 0;
	u32 num_signal_strength = 0;
	u32 __maybe_unused num_signal_qual = 0;
	u8 _alpha = 5; /*  this value is based on converging_constant = 5000 and sampling_interval = 1000 */

	if (adapter->recvpriv.is_signal_dbg) {
		/* update the user specific value, signal_strength_dbg, to signal_strength, rssi */
		adapter->recvpriv.signal_strength = adapter->recvpriv.signal_strength_dbg;
		adapter->recvpriv.rssi = (s8)translate_percentage_to_dbm((u8)adapter->recvpriv.signal_strength_dbg);
	} else {

		if (recvpriv->signal_strength_data.update_req == 0) {/*  update_req is clear, means we got rx */
			avg_signal_strength = recvpriv->signal_strength_data.avg_val;
			num_signal_strength = recvpriv->signal_strength_data.total_num;
			/*  after avg_vals are acquired, we can re-stat the signal values */
			recvpriv->signal_strength_data.update_req = 1;
		}

		if (recvpriv->signal_qual_data.update_req == 0) {/*  update_req is clear, means we got rx */
			avg_signal_qual = recvpriv->signal_qual_data.avg_val;
			num_signal_qual = recvpriv->signal_qual_data.total_num;
			/*  after avg_vals are acquired, we can re-stat the signal values */
			recvpriv->signal_qual_data.update_req = 1;
		}

		if (num_signal_strength == 0) {
			if (rtw_get_on_cur_ch_time(adapter) == 0 ||
			    jiffies_to_msecs(jiffies - rtw_get_on_cur_ch_time(adapter)) < 2 * adapter->mlmeextpriv.mlmext_info.bcn_interval
			) {
				goto set_timer;
			}
		}

		if (check_fwstate(&adapter->mlmepriv, _FW_UNDER_SURVEY) == true ||
		    check_fwstate(&adapter->mlmepriv, _FW_LINKED) == false
		) {
			goto set_timer;
		}

		/* update value of signal_strength, rssi, signal_qual */
		tmp_s = (avg_signal_strength+(_alpha-1)*recvpriv->signal_strength);
		if (tmp_s % _alpha)
			tmp_s = tmp_s/_alpha + 1;
		else
			tmp_s = tmp_s/_alpha;
		if (tmp_s > 100)
			tmp_s = 100;

		tmp_q = (avg_signal_qual+(_alpha-1)*recvpriv->signal_qual);
		if (tmp_q % _alpha)
			tmp_q = tmp_q/_alpha + 1;
		else
			tmp_q = tmp_q/_alpha;
		if (tmp_q > 100)
			tmp_q = 100;

		recvpriv->signal_strength = tmp_s;
		recvpriv->rssi = (s8)translate_percentage_to_dbm(tmp_s);
		recvpriv->signal_qual = tmp_q;
	}

set_timer:
	rtw_set_signal_stat_timer(recvpriv);

}
