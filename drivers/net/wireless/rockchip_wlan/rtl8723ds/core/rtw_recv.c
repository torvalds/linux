/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
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
 *****************************************************************************/
#define _RTW_RECV_C_

#include <drv_types.h>
#include <hal_data.h>

#if defined(PLATFORM_LINUX) && defined (PLATFORM_WINDOWS)

	#error "Shall be Linux or Windows, but not both!\n"

#endif


#ifdef CONFIG_NEW_SIGNAL_STAT_PROCESS
static void rtw_signal_stat_timer_hdl(void *ctx);

enum {
	SIGNAL_STAT_CALC_PROFILE_0 = 0,
	SIGNAL_STAT_CALC_PROFILE_1,
	SIGNAL_STAT_CALC_PROFILE_MAX
};

u8 signal_stat_calc_profile[SIGNAL_STAT_CALC_PROFILE_MAX][2] = {
	{4, 1},	/* Profile 0 => pre_stat : curr_stat = 4 : 1 */
	{3, 7}	/* Profile 1 => pre_stat : curr_stat = 3 : 7 */
};

#ifndef RTW_SIGNAL_STATE_CALC_PROFILE
	#define RTW_SIGNAL_STATE_CALC_PROFILE SIGNAL_STAT_CALC_PROFILE_1
#endif

#endif /* CONFIG_NEW_SIGNAL_STAT_PROCESS */

void _rtw_init_sta_recv_priv(struct sta_recv_priv *psta_recvpriv)
{



	_rtw_memset((u8 *)psta_recvpriv, 0, sizeof(struct sta_recv_priv));

	_rtw_spinlock_init(&psta_recvpriv->lock);

	/* for(i=0; i<MAX_RX_NUMBLKS; i++) */
	/*	_rtw_init_queue(&psta_recvpriv->blk_strms[i]); */

	_rtw_init_queue(&psta_recvpriv->defrag_q);


}

sint _rtw_init_recv_priv(struct recv_priv *precvpriv, _adapter *padapter)
{
	sint i;

	union recv_frame *precvframe;
	sint	res = _SUCCESS;


	/* We don't need to memset padapter->XXX to zero, because adapter is allocated by rtw_zvmalloc(). */
	/* _rtw_memset((unsigned char *)precvpriv, 0, sizeof (struct  recv_priv)); */

	_rtw_spinlock_init(&precvpriv->lock);

#ifdef CONFIG_RECV_THREAD_MODE
	_rtw_init_sema(&precvpriv->recv_sema, 0);

#endif

	_rtw_init_queue(&precvpriv->free_recv_queue);
	_rtw_init_queue(&precvpriv->recv_pending_queue);
	_rtw_init_queue(&precvpriv->uc_swdec_pending_queue);

	precvpriv->adapter = padapter;

	precvpriv->free_recvframe_cnt = NR_RECVFRAME;

	precvpriv->sink_udpport = 0;
	precvpriv->pre_rtp_rxseq = 0;
	precvpriv->cur_rtp_rxseq = 0;

#ifdef DBG_RX_SIGNAL_DISPLAY_RAW_DATA
	precvpriv->store_law_data_flag = 1;
#else
	precvpriv->store_law_data_flag = 0;
#endif

	rtw_os_recv_resource_init(precvpriv, padapter);

	precvpriv->pallocated_frame_buf = rtw_zvmalloc(NR_RECVFRAME * sizeof(union recv_frame) + RXFRAME_ALIGN_SZ);

	if (precvpriv->pallocated_frame_buf == NULL) {
		res = _FAIL;
		goto exit;
	}
	/* _rtw_memset(precvpriv->pallocated_frame_buf, 0, NR_RECVFRAME * sizeof(union recv_frame) + RXFRAME_ALIGN_SZ); */

	precvpriv->precv_frame_buf = (u8 *)N_BYTE_ALIGMENT((SIZE_PTR)(precvpriv->pallocated_frame_buf), RXFRAME_ALIGN_SZ);
	/* precvpriv->precv_frame_buf = precvpriv->pallocated_frame_buf + RXFRAME_ALIGN_SZ - */
	/*						((SIZE_PTR) (precvpriv->pallocated_frame_buf) &(RXFRAME_ALIGN_SZ-1)); */

	precvframe = (union recv_frame *) precvpriv->precv_frame_buf;


	for (i = 0; i < NR_RECVFRAME ; i++) {
		_rtw_init_listhead(&(precvframe->u.list));

		rtw_list_insert_tail(&(precvframe->u.list), &(precvpriv->free_recv_queue.queue));

		res = rtw_os_recv_resource_alloc(padapter, precvframe);

		precvframe->u.hdr.len = 0;

		precvframe->u.hdr.adapter = padapter;
		precvframe++;

	}

#ifdef CONFIG_USB_HCI

	ATOMIC_SET(&(precvpriv->rx_pending_cnt), 1);

	_rtw_init_sema(&precvpriv->allrxreturnevt, 0);

#endif

	res = rtw_hal_init_recv_priv(padapter);

#ifdef CONFIG_NEW_SIGNAL_STAT_PROCESS
	rtw_init_timer(&precvpriv->signal_stat_timer, padapter, rtw_signal_stat_timer_hdl, padapter);

	precvpriv->signal_stat_sampling_interval = 2000; /* ms */
	/* precvpriv->signal_stat_converging_constant = 5000; */ /* ms */

	rtw_set_signal_stat_timer(precvpriv);
#endif /* CONFIG_NEW_SIGNAL_STAT_PROCESS */

exit:


	return res;

}

void rtw_mfree_recv_priv_lock(struct recv_priv *precvpriv);
void rtw_mfree_recv_priv_lock(struct recv_priv *precvpriv)
{
	_rtw_spinlock_free(&precvpriv->lock);
#ifdef CONFIG_RECV_THREAD_MODE
	_rtw_free_sema(&precvpriv->recv_sema);
#endif

	_rtw_spinlock_free(&precvpriv->free_recv_queue.lock);
	_rtw_spinlock_free(&precvpriv->recv_pending_queue.lock);

	_rtw_spinlock_free(&precvpriv->free_recv_buf_queue.lock);

#ifdef CONFIG_USE_USB_BUFFER_ALLOC_RX
	_rtw_spinlock_free(&precvpriv->recv_buf_pending_queue.lock);
#endif /* CONFIG_USE_USB_BUFFER_ALLOC_RX */
}

void _rtw_free_recv_priv(struct recv_priv *precvpriv)
{
	_adapter	*padapter = precvpriv->adapter;


	rtw_free_uc_swdec_pending_queue(padapter);

	rtw_mfree_recv_priv_lock(precvpriv);

	rtw_os_recv_resource_free(precvpriv);

	if (precvpriv->pallocated_frame_buf)
		rtw_vmfree(precvpriv->pallocated_frame_buf, NR_RECVFRAME * sizeof(union recv_frame) + RXFRAME_ALIGN_SZ);

	rtw_hal_free_recv_priv(padapter);


}

bool rtw_rframe_del_wfd_ie(union recv_frame *rframe, u8 ies_offset)
{
#define DBG_RFRAME_DEL_WFD_IE 0
	u8 *ies = rframe->u.hdr.rx_data + sizeof(struct rtw_ieee80211_hdr_3addr) + ies_offset;
	uint ies_len_ori = rframe->u.hdr.len - (ies - rframe->u.hdr.rx_data);
	uint ies_len;

	ies_len = rtw_del_wfd_ie(ies, ies_len_ori, DBG_RFRAME_DEL_WFD_IE ? __func__ : NULL);
	rframe->u.hdr.len -= ies_len_ori - ies_len;

	return ies_len_ori != ies_len;
}

union recv_frame *_rtw_alloc_recvframe(_queue *pfree_recv_queue)
{

	union recv_frame  *precvframe;
	_list	*plist, *phead;
	_adapter *padapter;
	struct recv_priv *precvpriv;

	if (_rtw_queue_empty(pfree_recv_queue) == _TRUE)
		precvframe = NULL;
	else {
		phead = get_list_head(pfree_recv_queue);

		plist = get_next(phead);

		precvframe = LIST_CONTAINOR(plist, union recv_frame, u);

		rtw_list_delete(&precvframe->u.hdr.list);
		padapter = precvframe->u.hdr.adapter;
		if (padapter != NULL) {
			precvpriv = &padapter->recvpriv;
			if (pfree_recv_queue == &precvpriv->free_recv_queue)
				precvpriv->free_recvframe_cnt--;
		}
	}


	return precvframe;

}

union recv_frame *rtw_alloc_recvframe(_queue *pfree_recv_queue)
{
	_irqL irqL;
	union recv_frame  *precvframe;

	_enter_critical_bh(&pfree_recv_queue->lock, &irqL);

	precvframe = _rtw_alloc_recvframe(pfree_recv_queue);

	_exit_critical_bh(&pfree_recv_queue->lock, &irqL);

	return precvframe;
}

void rtw_init_recvframe(union recv_frame *precvframe, struct recv_priv *precvpriv)
{
	/* Perry: This can be removed */
	_rtw_init_listhead(&precvframe->u.hdr.list);

	precvframe->u.hdr.len = 0;
}

int rtw_free_recvframe(union recv_frame *precvframe, _queue *pfree_recv_queue)
{
	_irqL irqL;
	_adapter *padapter = precvframe->u.hdr.adapter;
	struct recv_priv *precvpriv = &padapter->recvpriv;


#ifdef CONFIG_CONCURRENT_MODE
	padapter = GET_PRIMARY_ADAPTER(padapter);
	precvpriv = &padapter->recvpriv;
	pfree_recv_queue = &precvpriv->free_recv_queue;
	precvframe->u.hdr.adapter = padapter;
#endif


	rtw_os_free_recvframe(precvframe);


	_enter_critical_bh(&pfree_recv_queue->lock, &irqL);

	rtw_list_delete(&(precvframe->u.hdr.list));

	precvframe->u.hdr.len = 0;

	rtw_list_insert_tail(&(precvframe->u.hdr.list), get_list_head(pfree_recv_queue));

	if (padapter != NULL) {
		if (pfree_recv_queue == &precvpriv->free_recv_queue)
			precvpriv->free_recvframe_cnt++;
	}

	_exit_critical_bh(&pfree_recv_queue->lock, &irqL);


	return _SUCCESS;

}




sint _rtw_enqueue_recvframe(union recv_frame *precvframe, _queue *queue)
{

	_adapter *padapter = precvframe->u.hdr.adapter;
	struct recv_priv *precvpriv = &padapter->recvpriv;


	/* _rtw_init_listhead(&(precvframe->u.hdr.list)); */
	rtw_list_delete(&(precvframe->u.hdr.list));


	rtw_list_insert_tail(&(precvframe->u.hdr.list), get_list_head(queue));

	if (padapter != NULL) {
		if (queue == &precvpriv->free_recv_queue)
			precvpriv->free_recvframe_cnt++;
	}


	return _SUCCESS;
}

sint rtw_enqueue_recvframe(union recv_frame *precvframe, _queue *queue)
{
	sint ret;
	_irqL irqL;

	/* _spinlock(&pfree_recv_queue->lock); */
	_enter_critical_bh(&queue->lock, &irqL);
	ret = _rtw_enqueue_recvframe(precvframe, queue);
	/* _rtw_spinunlock(&pfree_recv_queue->lock); */
	_exit_critical_bh(&queue->lock, &irqL);

	return ret;
}

/*
sint	rtw_enqueue_recvframe(union recv_frame *precvframe, _queue *queue)
{
	return rtw_free_recvframe(precvframe, queue);
}
*/




/*
caller : defrag ; recvframe_chk_defrag in recv_thread  (passive)
pframequeue: defrag_queue : will be accessed in recv_thread  (passive)

using spinlock to protect

*/

void rtw_free_recvframe_queue(_queue *pframequeue,  _queue *pfree_recv_queue)
{
	union	recv_frame	*precvframe;
	_list	*plist, *phead;

	_rtw_spinlock(&pframequeue->lock);

	phead = get_list_head(pframequeue);
	plist = get_next(phead);

	while (rtw_end_of_queue_search(phead, plist) == _FALSE) {
		precvframe = LIST_CONTAINOR(plist, union recv_frame, u);

		plist = get_next(plist);

		/* rtw_list_delete(&precvframe->u.hdr.list); */ /* will do this in rtw_free_recvframe() */

		rtw_free_recvframe(precvframe, pfree_recv_queue);
	}

	_rtw_spinunlock(&pframequeue->lock);


}

u32 rtw_free_uc_swdec_pending_queue(_adapter *adapter)
{
	u32 cnt = 0;
	union recv_frame *pending_frame;
	while ((pending_frame = rtw_alloc_recvframe(&adapter->recvpriv.uc_swdec_pending_queue))) {
		rtw_free_recvframe(pending_frame, &adapter->recvpriv.free_recv_queue);
		cnt++;
	}

	if (cnt)
		RTW_INFO(FUNC_ADPT_FMT" dequeue %d\n", FUNC_ADPT_ARG(adapter), cnt);

	return cnt;
}


sint rtw_enqueue_recvbuf_to_head(struct recv_buf *precvbuf, _queue *queue)
{
	_irqL irqL;

	_enter_critical_bh(&queue->lock, &irqL);

	rtw_list_delete(&precvbuf->list);
	rtw_list_insert_head(&precvbuf->list, get_list_head(queue));

	_exit_critical_bh(&queue->lock, &irqL);

	return _SUCCESS;
}

sint rtw_enqueue_recvbuf(struct recv_buf *precvbuf, _queue *queue)
{
	_irqL irqL;
#ifdef CONFIG_SDIO_HCI
	_enter_critical_bh(&queue->lock, &irqL);
#else
	_enter_critical_ex(&queue->lock, &irqL);
#endif/*#ifdef CONFIG_SDIO_HCI*/

	rtw_list_delete(&precvbuf->list);

	rtw_list_insert_tail(&precvbuf->list, get_list_head(queue));
#ifdef CONFIG_SDIO_HCI
	_exit_critical_bh(&queue->lock, &irqL);
#else
	_exit_critical_ex(&queue->lock, &irqL);
#endif/*#ifdef CONFIG_SDIO_HCI*/
	return _SUCCESS;

}

struct recv_buf *rtw_dequeue_recvbuf(_queue *queue)
{
	_irqL irqL;
	struct recv_buf *precvbuf;
	_list	*plist, *phead;

#ifdef CONFIG_SDIO_HCI
	_enter_critical_bh(&queue->lock, &irqL);
#else
	_enter_critical_ex(&queue->lock, &irqL);
#endif/*#ifdef CONFIG_SDIO_HCI*/

	if (_rtw_queue_empty(queue) == _TRUE)
		precvbuf = NULL;
	else {
		phead = get_list_head(queue);

		plist = get_next(phead);

		precvbuf = LIST_CONTAINOR(plist, struct recv_buf, list);

		rtw_list_delete(&precvbuf->list);

	}

#ifdef CONFIG_SDIO_HCI
	_exit_critical_bh(&queue->lock, &irqL);
#else
	_exit_critical_ex(&queue->lock, &irqL);
#endif/*#ifdef CONFIG_SDIO_HCI*/

	return precvbuf;

}

sint recvframe_chkmic(_adapter *adapter,  union recv_frame *precvframe);
sint recvframe_chkmic(_adapter *adapter,  union recv_frame *precvframe)
{

	sint	i, res = _SUCCESS;
	u32	datalen;
	u8	miccode[8];
	u8	bmic_err = _FALSE, brpt_micerror = _TRUE;
	u8	*pframe, *payload, *pframemic;
	u8	*mickey;
	/* u8	*iv,rxdata_key_idx=0; */
	struct	sta_info		*stainfo;
	struct	rx_pkt_attrib	*prxattrib = &precvframe->u.hdr.attrib;
	struct	security_priv	*psecuritypriv = &adapter->securitypriv;

	struct mlme_ext_priv	*pmlmeext = &adapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	stainfo = rtw_get_stainfo(&adapter->stapriv , &prxattrib->ta[0]);

	if (prxattrib->encrypt == _TKIP_) {

		/* calculate mic code */
		if (stainfo != NULL) {
			if (IS_MCAST(prxattrib->ra)) {
				/* mickey=&psecuritypriv->dot118021XGrprxmickey.skey[0]; */
				/* iv = precvframe->u.hdr.rx_data+prxattrib->hdrlen; */
				/* rxdata_key_idx =( ((iv[3])>>6)&0x3) ; */
				mickey = &psecuritypriv->dot118021XGrprxmickey[prxattrib->key_index].skey[0];

				/* RTW_INFO("\n recvframe_chkmic: bcmc key psecuritypriv->dot118021XGrpKeyid(%d),pmlmeinfo->key_index(%d) ,recv key_id(%d)\n", */
				/*								psecuritypriv->dot118021XGrpKeyid,pmlmeinfo->key_index,rxdata_key_idx); */

				if (psecuritypriv->binstallGrpkey == _FALSE) {
					res = _FAIL;
					RTW_INFO("\n recvframe_chkmic:didn't install group key!!!!!!!!!!\n");
					goto exit;
				}
			} else {
				mickey = &stainfo->dot11tkiprxmickey.skey[0];
			}

			datalen = precvframe->u.hdr.len - prxattrib->hdrlen - prxattrib->iv_len - prxattrib->icv_len - 8; /* icv_len included the mic code */
			pframe = precvframe->u.hdr.rx_data;
			payload = pframe + prxattrib->hdrlen + prxattrib->iv_len;


			/* rtw_seccalctkipmic(&stainfo->dot11tkiprxmickey.skey[0],pframe,payload, datalen ,&miccode[0],(unsigned char)prxattrib->priority); */ /* care the length of the data */

			rtw_seccalctkipmic(mickey, pframe, payload, datalen , &miccode[0], (unsigned char)prxattrib->priority); /* care the length of the data */

			pframemic = payload + datalen;

			bmic_err = _FALSE;

			for (i = 0; i < 8; i++) {
				if (miccode[i] != *(pframemic + i)) {
					bmic_err = _TRUE;
				}
			}


			if (bmic_err == _TRUE) {



				/* double check key_index for some timing issue , */
				/* cannot compare with psecuritypriv->dot118021XGrpKeyid also cause timing issue */
				if ((IS_MCAST(prxattrib->ra) == _TRUE)  && (prxattrib->key_index != pmlmeinfo->key_index))
					brpt_micerror = _FALSE;

				if ((prxattrib->bdecrypted == _TRUE) && (brpt_micerror == _TRUE)) {
					rtw_handle_tkip_mic_err(adapter, stainfo, (u8)IS_MCAST(prxattrib->ra));
					RTW_INFO(" mic error :prxattrib->bdecrypted=%d\n", prxattrib->bdecrypted);
				} else {
					RTW_INFO(" mic error :prxattrib->bdecrypted=%d\n", prxattrib->bdecrypted);
				}

				res = _FAIL;

			} else {
				/* mic checked ok */
				if ((psecuritypriv->bcheck_grpkey == _FALSE) && (IS_MCAST(prxattrib->ra) == _TRUE)) {
					psecuritypriv->bcheck_grpkey = _TRUE;
				}
			}

		}

		recvframe_pull_tail(precvframe, 8);

	}

exit:


	return res;

}

/*#define DBG_RX_SW_DECRYPTOR*/

/* decrypt and set the ivlen,icvlen of the recv_frame */
union recv_frame *decryptor(_adapter *padapter, union recv_frame *precv_frame);
union recv_frame *decryptor(_adapter *padapter, union recv_frame *precv_frame)
{

	struct rx_pkt_attrib *prxattrib = &precv_frame->u.hdr.attrib;
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	union recv_frame *return_packet = precv_frame;
	u32	 res = _SUCCESS;


	DBG_COUNTER(padapter->rx_logs.core_rx_post_decrypt);


	if (prxattrib->encrypt > 0) {
		u8 *iv = precv_frame->u.hdr.rx_data + prxattrib->hdrlen;
		prxattrib->key_index = (((iv[3]) >> 6) & 0x3) ;

		if (prxattrib->key_index > WEP_KEYS) {
			RTW_INFO("prxattrib->key_index(%d) > WEP_KEYS\n", prxattrib->key_index);

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

	if (prxattrib->encrypt && !prxattrib->bdecrypted) {

		#ifdef CONFIG_CONCURRENT_MODE
		if (!IS_MCAST(prxattrib->ra)) /* bc/mc packets may use sw decryption for concurrent mode */
		#endif
			psecuritypriv->hw_decrypted = _FALSE;

#ifdef DBG_RX_SW_DECRYPTOR
		RTW_INFO(ADPT_FMT" - sec_type:%s DO SW decryption\n",
			ADPT_ARG(padapter), security_type_str(prxattrib->encrypt));
#endif

#ifdef DBG_RX_DECRYPTOR
		RTW_INFO("[%s] %d:prxstat->bdecrypted:%d,  prxattrib->encrypt:%d,  Setting psecuritypriv->hw_decrypted = %d\n",
			 __FUNCTION__,
			 __LINE__,
			 prxattrib->bdecrypted,
			 prxattrib->encrypt,
			 psecuritypriv->hw_decrypted);
#endif

		switch (prxattrib->encrypt) {
		case _WEP40_:
		case _WEP104_:
			DBG_COUNTER(padapter->rx_logs.core_rx_post_decrypt_wep);
			rtw_wep_decrypt(padapter, (u8 *)precv_frame);
			break;
		case _TKIP_:
			DBG_COUNTER(padapter->rx_logs.core_rx_post_decrypt_tkip);
			res = rtw_tkip_decrypt(padapter, (u8 *)precv_frame);
			break;
		case _AES_:
			DBG_COUNTER(padapter->rx_logs.core_rx_post_decrypt_aes);
			res = rtw_aes_decrypt(padapter, (u8 *)precv_frame);
			break;
#ifdef CONFIG_WAPI_SUPPORT
		case _SMS4_:
			DBG_COUNTER(padapter->rx_logs.core_rx_post_decrypt_wapi);
			rtw_sms4_decrypt(padapter, (u8 *)precv_frame);
			break;
#endif
		default:
			break;
		}
	} else if (prxattrib->bdecrypted == 1
		   && prxattrib->encrypt > 0
		&& (psecuritypriv->busetkipkey == 1 || prxattrib->encrypt != _TKIP_)
		  ) {
#if 0
		if ((prxstat->icv == 1) && (prxattrib->encrypt != _AES_)) {
			psecuritypriv->hw_decrypted = _FALSE;


			rtw_free_recvframe(precv_frame, &padapter->recvpriv.free_recv_queue);

			return_packet = NULL;

		} else
#endif
		{
			DBG_COUNTER(padapter->rx_logs.core_rx_post_decrypt_hw);

			psecuritypriv->hw_decrypted = _TRUE;
#ifdef DBG_RX_DECRYPTOR
			RTW_INFO("[%s] %d:prxstat->bdecrypted:%d,  prxattrib->encrypt:%d,  Setting psecuritypriv->hw_decrypted = %d\n",
				 __FUNCTION__,
				 __LINE__,
				 prxattrib->bdecrypted,
				 prxattrib->encrypt,
				 psecuritypriv->hw_decrypted);

#endif
		}
	} else {
		DBG_COUNTER(padapter->rx_logs.core_rx_post_decrypt_unknown);
#ifdef DBG_RX_DECRYPTOR
		RTW_INFO("[%s] %d:prxstat->bdecrypted:%d,  prxattrib->encrypt:%d,  Setting psecuritypriv->hw_decrypted = %d\n",
			 __FUNCTION__,
			 __LINE__,
			 prxattrib->bdecrypted,
			 prxattrib->encrypt,
			 psecuritypriv->hw_decrypted);
#endif
	}

	if (res == _FAIL) {
		rtw_free_recvframe(return_packet, &padapter->recvpriv.free_recv_queue);
		return_packet = NULL;
	} else
		prxattrib->bdecrypted = _TRUE;
	/* recvframe_chkmic(adapter, precv_frame);   */ /* move to recvframme_defrag function */


	return return_packet;

}
/* ###set the security information in the recv_frame */
union recv_frame *portctrl(_adapter *adapter, union recv_frame *precv_frame);
union recv_frame *portctrl(_adapter *adapter, union recv_frame *precv_frame)
{
	u8 *psta_addr = NULL;
	u8 *ptr;
	uint  auth_alg;
	struct recv_frame_hdr *pfhdr;
	struct sta_info *psta;
	struct sta_priv *pstapriv ;
	union recv_frame *prtnframe;
	u16	ether_type = 0;
	u16  eapol_type = 0x888e;/* for Funia BD's WPA issue  */
	struct rx_pkt_attrib *pattrib;


	pstapriv = &adapter->stapriv;

	auth_alg = adapter->securitypriv.dot11AuthAlgrthm;

	ptr = get_recvframe_data(precv_frame);
	pfhdr = &precv_frame->u.hdr;
	pattrib = &pfhdr->attrib;
	psta_addr = pattrib->ta;

	prtnframe = NULL;

	psta = rtw_get_stainfo(pstapriv, psta_addr);


	if (auth_alg == dot11AuthAlgrthm_8021X) {
		if ((psta != NULL) && (psta->ieee8021x_blocked)) {
			/* blocked */
			/* only accept EAPOL frame */

			prtnframe = precv_frame;

			/* get ether_type */
			ptr = ptr + pfhdr->attrib.hdrlen + pfhdr->attrib.iv_len + LLC_HEADER_SIZE;
			_rtw_memcpy(&ether_type, ptr, 2);
			ether_type = ntohs((unsigned short)ether_type);

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
			/* if(ether_type == eapol_type){ */
			/* check Rekey */

			/*	prtnframe=precv_frame; */
			/* } */
		}
	} else
		prtnframe = precv_frame;


	return prtnframe;

}

sint recv_decache(union recv_frame *precv_frame, u8 bretry)
{
	struct sta_info *sta = precv_frame->u.hdr.psta;
	struct stainfo_rxcache *prxcache = &sta->sta_recvpriv.rxcache;

	sint tid = precv_frame->u.hdr.attrib.priority;
	u16 seq_ctrl = ((precv_frame->u.hdr.attrib.seq_num & 0xffff) << 4) |
		       (precv_frame->u.hdr.attrib.frag_num & 0xf);

	if (tid > 15)
		return _FAIL;

	if (1) { /* if(bretry) */
		if (seq_ctrl == prxcache->tid_rxseq[tid]) {
			/* for non-AMPDU case	*/
			sta->sta_stats.duplicate_cnt++;

			if (sta->sta_stats.duplicate_cnt % 100 == 0)
				RTW_INFO("%s: tid=%u seq=%d frag=%d\n", __func__
					, tid, precv_frame->u.hdr.attrib.seq_num
					, precv_frame->u.hdr.attrib.frag_num);

			return _FAIL;
		}
	}

	prxcache->tid_rxseq[tid] = seq_ctrl;


	return _SUCCESS;

}

/* VALID_PN_CHK
 * Return true when PN is legal, otherwise false.
 * Legal PN:
 *	1. If old PN is 0, any PN is legal
 *	2. PN > old PN
 */
#define PN_LESS_CHK(a, b)	(((a-b) & 0x800000000000) != 0)
#define VALID_PN_CHK(new, old)	(((old) == 0) || PN_LESS_CHK(old, new))
#define CCMPH_2_KEYID(ch)	(((ch) & 0x00000000c0000000) >> 30)
#define CCMPH_2_PN(ch)	((ch) & 0x000000000000ffff) \
				| (((ch) & 0xffffffff00000000) >> 16)
sint recv_ucast_pn_decache(union recv_frame *precv_frame);
sint recv_ucast_pn_decache(union recv_frame *precv_frame)
{
	_adapter *padapter = precv_frame->u.hdr.adapter;
	struct rx_pkt_attrib *pattrib = &precv_frame->u.hdr.attrib;
	struct sta_info *sta = precv_frame->u.hdr.psta;
	struct stainfo_rxcache *prxcache = &sta->sta_recvpriv.rxcache;
	u8 *pdata = precv_frame->u.hdr.rx_data;
	u32 data_len = precv_frame->u.hdr.len;
	sint tid = precv_frame->u.hdr.attrib.priority;
	u64 tmp_iv_hdr = 0;
	u64 curr_pn = 0, pkt_pn = 0;

	if (tid > 15)
		return _FAIL;

	if (pattrib->encrypt == _AES_) {
		tmp_iv_hdr = le64_to_cpu(*(u64*)(pdata + pattrib->hdrlen));
		pkt_pn = CCMPH_2_PN(tmp_iv_hdr);
	
		tmp_iv_hdr = le64_to_cpu(*(u64*)prxcache->iv[tid]);
		curr_pn = CCMPH_2_PN(tmp_iv_hdr);	

		if (!VALID_PN_CHK(pkt_pn, curr_pn)) {
			/* return _FAIL; */
		} else
			_rtw_memcpy(prxcache->iv[tid], (pdata + pattrib->hdrlen), sizeof(prxcache->iv[tid]));
	}

	return _SUCCESS;
}

sint recv_bcast_pn_decache(union recv_frame *precv_frame);
sint recv_bcast_pn_decache(union recv_frame *precv_frame)
{
	_adapter *padapter = precv_frame->u.hdr.adapter;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	struct rx_pkt_attrib *pattrib = &precv_frame->u.hdr.attrib;
	u8 *pdata = precv_frame->u.hdr.rx_data;
	u32 data_len = precv_frame->u.hdr.len;
	u64 tmp_iv_hdr = 0;
	u64 curr_pn = 0, pkt_pn = 0;
	u8 key_id;

	if ((pattrib->encrypt == _AES_) &&
		(check_fwstate(pmlmepriv, WIFI_STATION_STATE) == _TRUE)) {		

		tmp_iv_hdr = le64_to_cpu(*(u64*)(pdata + pattrib->hdrlen));
		key_id = CCMPH_2_KEYID(tmp_iv_hdr);
		pkt_pn = CCMPH_2_PN(tmp_iv_hdr);
	
		curr_pn = le64_to_cpu(*(u64*)psecuritypriv->iv_seq[key_id]);
		curr_pn &= 0x0000ffffffffffff;

		if (!VALID_PN_CHK(pkt_pn, curr_pn))
			return _FAIL;

		*(u64*)psecuritypriv->iv_seq[key_id] = cpu_to_le64(pkt_pn);
	}

	return _SUCCESS;
}

void process_pwrbit_data(_adapter *padapter, union recv_frame *precv_frame, struct sta_info *psta)
{
#ifdef CONFIG_AP_MODE
	unsigned char pwrbit;
	u8 *ptr = precv_frame->u.hdr.rx_data;
	struct rx_pkt_attrib *pattrib = &precv_frame->u.hdr.attrib;
	struct sta_priv *pstapriv = &padapter->stapriv;

	pwrbit = GetPwrMgt(ptr);

	if (pwrbit) {
		if (!(psta->state & WIFI_SLEEP_STATE)) {
			/* psta->state |= WIFI_SLEEP_STATE; */
			/* pstapriv->sta_dz_bitmap |= BIT(psta->cmn.aid); */

			stop_sta_xmit(padapter, psta);

			/* RTW_INFO("to sleep, sta_dz_bitmap=%x\n", pstapriv->sta_dz_bitmap); */
		}
	} else {
		if (psta->state & WIFI_SLEEP_STATE) {
			/* psta->state ^= WIFI_SLEEP_STATE; */
			/* pstapriv->sta_dz_bitmap &= ~BIT(psta->cmn.aid); */

			wakeup_sta_to_xmit(padapter, psta);

			/* RTW_INFO("to wakeup, sta_dz_bitmap=%x\n", pstapriv->sta_dz_bitmap); */
		}
	}
#endif
}

void process_wmmps_data(_adapter *padapter, union recv_frame *precv_frame, struct sta_info *psta)
{
#ifdef CONFIG_AP_MODE
	struct rx_pkt_attrib *pattrib = &precv_frame->u.hdr.attrib;
	struct sta_priv *pstapriv = &padapter->stapriv;

#ifdef CONFIG_TDLS
	if (!(psta->tdls_sta_state & TDLS_LINKED_STATE)) {
#endif /* CONFIG_TDLS */

		if (!psta->qos_option)
			return;

		if (!(psta->qos_info & 0xf))
			return;

#ifdef CONFIG_TDLS
	}
#endif /* CONFIG_TDLS		 */

	if (psta->state & WIFI_SLEEP_STATE) {
		u8 wmmps_ac = 0;

		switch (pattrib->priority) {
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

		if (wmmps_ac) {
			if (psta->sleepq_ac_len > 0) {
				/* process received triggered frame */
				xmit_delivery_enabled_frames(padapter, psta);
			} else {
				/* issue one qos null frame with More data bit = 0 and the EOSP bit set (=1) */
				issue_qos_nulldata(padapter, psta->cmn.mac_addr, (u16)pattrib->priority, 0, 0);
			}
		}

	}


#endif

}

#ifdef CONFIG_TDLS
sint OnTDLS(_adapter *adapter, union recv_frame *precv_frame)
{
	struct rx_pkt_attrib	*pattrib = &precv_frame->u.hdr.attrib;
	sint ret = _SUCCESS;
	u8 *paction = get_recvframe_data(precv_frame);
	u8 category_field = 1;
#ifdef CONFIG_WFD
	u8 WFA_OUI[3] = { 0x50, 0x6f, 0x9a };
#endif /* CONFIG_WFD */
	struct tdls_info *ptdlsinfo = &(adapter->tdlsinfo);
	u8 *ptr = precv_frame->u.hdr.rx_data;
	struct sta_priv *pstapriv = &(adapter->stapriv);
	struct sta_info *ptdls_sta = NULL;

	/* point to action field */
	paction += pattrib->hdrlen
		   + pattrib->iv_len
		   + SNAP_SIZE
		   + ETH_TYPE_LEN
		   + PAYLOAD_TYPE_LEN
		   + category_field;

	RTW_INFO("[TDLS] Recv %s from "MAC_FMT" with SeqNum = %d\n", rtw_tdls_action_txt(*paction), MAC_ARG(pattrib->src), GetSequence(get_recvframe_data(precv_frame)));

	if (hal_chk_wl_func(adapter, WL_FUNC_TDLS) == _FALSE) {
		RTW_INFO("Ignore tdls frame since hal doesn't support tdls\n");
		ret = _FAIL;
		return ret;
	}

	if (rtw_is_tdls_enabled(adapter) == _FALSE) {
		RTW_INFO("recv tdls frame, "
			 "but tdls haven't enabled\n");
		ret = _FAIL;
		return ret;
	}

	ptdls_sta = rtw_get_stainfo(pstapriv, get_sa(ptr));
	if (ptdls_sta == NULL) {
		switch (*paction) {
		case TDLS_SETUP_REQUEST:
		case TDLS_DISCOVERY_REQUEST:
			break;
		default:
			RTW_INFO("[TDLS] %s - Direct Link Peer = "MAC_FMT" not found for action = %d\n", __func__, MAC_ARG(get_sa(ptr)), *paction);
			ret = _FAIL;
			goto exit;
		}
	}

	switch (*paction) {
	case TDLS_SETUP_REQUEST:
		ret = On_TDLS_Setup_Req(adapter, precv_frame, ptdls_sta);
		break;
	case TDLS_SETUP_RESPONSE:
		ret = On_TDLS_Setup_Rsp(adapter, precv_frame, ptdls_sta);
		break;
	case TDLS_SETUP_CONFIRM:
		ret = On_TDLS_Setup_Cfm(adapter, precv_frame, ptdls_sta);
		break;
	case TDLS_TEARDOWN:
		ret = On_TDLS_Teardown(adapter, precv_frame, ptdls_sta);
		break;
	case TDLS_DISCOVERY_REQUEST:
		ret = On_TDLS_Dis_Req(adapter, precv_frame);
		break;
	case TDLS_PEER_TRAFFIC_INDICATION:
		ret = On_TDLS_Peer_Traffic_Indication(adapter, precv_frame, ptdls_sta);
		break;
	case TDLS_PEER_TRAFFIC_RESPONSE:
		ret = On_TDLS_Peer_Traffic_Rsp(adapter, precv_frame, ptdls_sta);
		break;
#ifdef CONFIG_TDLS_CH_SW
	case TDLS_CHANNEL_SWITCH_REQUEST:
		ret = On_TDLS_Ch_Switch_Req(adapter, precv_frame, ptdls_sta);
		break;
	case TDLS_CHANNEL_SWITCH_RESPONSE:
		ret = On_TDLS_Ch_Switch_Rsp(adapter, precv_frame, ptdls_sta);
		break;
#endif
#ifdef CONFIG_WFD
	/* First byte of WFA OUI */
	case 0x50:
		if (_rtw_memcmp(WFA_OUI, paction, 3)) {
			/* Probe request frame */
			if (*(paction + 3) == 0x04) {
				/* WFDTDLS: for sigma test, do not setup direct link automatically */
				ptdlsinfo->dev_discovered = _TRUE;
				RTW_INFO("recv tunneled probe request frame\n");
				issue_tunneled_probe_rsp(adapter, precv_frame);
			}
			/* Probe response frame */
			if (*(paction + 3) == 0x05) {
				/* WFDTDLS: for sigma test, do not setup direct link automatically */
				ptdlsinfo->dev_discovered = _TRUE;
				RTW_INFO("recv tunneled probe response frame\n");
			}
		}
		break;
#endif /* CONFIG_WFD */
	default:
		RTW_INFO("receive TDLS frame %d but not support\n", *paction);
		ret = _FAIL;
		break;
	}

exit:
	return ret;

}
#endif /* CONFIG_TDLS */

void count_rx_stats(_adapter *padapter, union recv_frame *prframe, struct sta_info *sta);
void count_rx_stats(_adapter *padapter, union recv_frame *prframe, struct sta_info *sta)
{
	int	sz;
	struct sta_info		*psta = NULL;
	struct stainfo_stats	*pstats = NULL;
	struct rx_pkt_attrib	*pattrib = &prframe->u.hdr.attrib;
	struct recv_priv		*precvpriv = &padapter->recvpriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	sz = get_recvframe_len(prframe);
	precvpriv->rx_bytes += sz;

	padapter->mlmepriv.LinkDetectInfo.NumRxOkInPeriod++;

	if ((!MacAddr_isBcst(pattrib->dst)) && (!IS_MCAST(pattrib->dst)))
		padapter->mlmepriv.LinkDetectInfo.NumRxUnicastOkInPeriod++;

	if (sta)
		psta = sta;
	else
		psta = prframe->u.hdr.psta;

	if (psta) {
		u8 is_ra_bmc = IS_MCAST(pattrib->ra);

		pstats = &psta->sta_stats;

		pstats->rx_data_pkts++;
		pstats->rx_bytes += sz;
		if (is_broadcast_mac_addr(pattrib->ra)) {
			pstats->rx_data_bc_pkts++;
			pstats->rx_bc_bytes += sz;
		} else if (is_ra_bmc) {
			pstats->rx_data_mc_pkts++;
			pstats->rx_mc_bytes += sz;
		}

		if (!is_ra_bmc) {
			pstats->rxratecnt[pattrib->data_rate]++;
			/*record rx packets for every tid*/
			pstats->rx_data_qos_pkts[pattrib->priority]++;
		}
	}

#ifdef CONFIG_CHECK_LEAVE_LPS
	traffic_check_for_leave_lps(padapter, _FALSE, 0);
#endif /* CONFIG_LPS */

}

sint sta2sta_data_frame(
	_adapter *adapter,
	union recv_frame *precv_frame,
	struct sta_info **psta
)
{
	u8 *ptr = precv_frame->u.hdr.rx_data;
	sint ret = _SUCCESS;
	struct rx_pkt_attrib *pattrib = &precv_frame->u.hdr.attrib;
	struct	sta_priv		*pstapriv = &adapter->stapriv;
	struct	mlme_priv	*pmlmepriv = &adapter->mlmepriv;
	u8 *mybssid  = get_bssid(pmlmepriv);
	u8 *myhwaddr = adapter_mac_addr(adapter);
	u8 *sta_addr = pattrib->ta;
	sint bmcast = IS_MCAST(pattrib->dst);

#ifdef CONFIG_TDLS
	struct tdls_info *ptdlsinfo = &adapter->tdlsinfo;
#ifdef CONFIG_TDLS_CH_SW
	struct tdls_ch_switch *pchsw_info = &ptdlsinfo->chsw_info;
#endif
	struct sta_info *ptdls_sta = NULL;
	u8 *psnap_type = ptr + pattrib->hdrlen + pattrib->iv_len + SNAP_SIZE;
	/* frame body located after [+2]: ether-type, [+1]: payload type */
	u8 *pframe_body = psnap_type + 2 + 1;
#endif


	/* RTW_INFO("[%s] %d, seqnum:%d\n", __FUNCTION__, __LINE__, pattrib->seq_num); */

	if ((check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == _TRUE) ||
	    (check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == _TRUE)) {

		/* filter packets that SA is myself or multicast or broadcast */
		if (_rtw_memcmp(myhwaddr, pattrib->src, ETH_ALEN)) {
			ret = _FAIL;
			goto exit;
		}

		if ((!_rtw_memcmp(myhwaddr, pattrib->dst, ETH_ALEN))	&& (!bmcast)) {
			ret = _FAIL;
			goto exit;
		}

		if (_rtw_memcmp(pattrib->bssid, "\x0\x0\x0\x0\x0\x0", ETH_ALEN) ||
		    _rtw_memcmp(mybssid, "\x0\x0\x0\x0\x0\x0", ETH_ALEN) ||
		    (!_rtw_memcmp(pattrib->bssid, mybssid, ETH_ALEN))) {
			ret = _FAIL;
			goto exit;
		}

	} else if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) == _TRUE) {
#ifdef CONFIG_TDLS

		/* direct link data transfer */
		if (ptdlsinfo->link_established == _TRUE) {
			*psta = ptdls_sta = rtw_get_stainfo(pstapriv, pattrib->ta);
			if (ptdls_sta == NULL) {
				ret = _FAIL;
				goto exit;
			} else if (ptdls_sta->tdls_sta_state & TDLS_LINKED_STATE) {
				/* filter packets that SA is myself or multicast or broadcast */
				if (_rtw_memcmp(myhwaddr, pattrib->src, ETH_ALEN)) {
					ret = _FAIL;
					goto exit;
				}
				/* da should be for me */
				if ((!_rtw_memcmp(myhwaddr, pattrib->dst, ETH_ALEN)) && (!bmcast)) {
					ret = _FAIL;
					goto exit;
				}
				/* check BSSID */
				if (_rtw_memcmp(pattrib->bssid, "\x0\x0\x0\x0\x0\x0", ETH_ALEN) ||
				    _rtw_memcmp(mybssid, "\x0\x0\x0\x0\x0\x0", ETH_ALEN) ||
				    (!_rtw_memcmp(pattrib->bssid, mybssid, ETH_ALEN))) {
					ret = _FAIL;
					goto exit;
				}

#ifdef CONFIG_TDLS_CH_SW
				if (ATOMIC_READ(&pchsw_info->chsw_on) == _TRUE) {
					if (adapter->mlmeextpriv.cur_channel != rtw_get_oper_ch(adapter)) {
						pchsw_info->ch_sw_state |= TDLS_PEER_AT_OFF_STATE;
						if (!(pchsw_info->ch_sw_state & TDLS_CH_SW_INITIATOR_STATE))
							_cancel_timer_ex(&ptdls_sta->ch_sw_timer);
						/* On_TDLS_Peer_Traffic_Rsp(adapter, precv_frame); */
					}
				}
#endif

				/* process UAPSD tdls sta */
				process_pwrbit_data(adapter, precv_frame, ptdls_sta);

				/* if NULL-frame, check pwrbit */
				if ((get_frame_sub_type(ptr) & WIFI_DATA_NULL) == WIFI_DATA_NULL) {
					/* NULL-frame with pwrbit=1, buffer_STA should buffer frames for sleep_STA */
					if (GetPwrMgt(ptr)) {
						/* it would be triggered when we are off channel and receiving NULL DATA */
						/* we can confirm that peer STA is at off channel */
						RTW_INFO("TDLS: recv peer null frame with pwr bit 1\n");
						/* ptdls_sta->tdls_sta_state|=TDLS_PEER_SLEEP_STATE; */
					}

					/* TODO: Updated BSSID's seq. */
					/* RTW_INFO("drop Null Data\n"); */
					ptdls_sta->tdls_sta_state &= ~(TDLS_WAIT_PTR_STATE);
					ret = _FAIL;
					goto exit;
				}

				/* receive some of all TDLS management frames, process it at ON_TDLS */
				if (_rtw_memcmp(psnap_type, SNAP_ETH_TYPE_TDLS, 2)) {
					ret = OnTDLS(adapter, precv_frame);
					goto exit;
				}

				if ((get_frame_sub_type(ptr) & WIFI_QOS_DATA_TYPE) == WIFI_QOS_DATA_TYPE)
					process_wmmps_data(adapter, precv_frame, ptdls_sta);

				ptdls_sta->tdls_sta_state &= ~(TDLS_WAIT_PTR_STATE);

			}
		} else
#endif /* CONFIG_TDLS */
		{
			/* For Station mode, sa and bssid should always be BSSID, and DA is my mac-address */
			if (!_rtw_memcmp(pattrib->bssid, pattrib->src, ETH_ALEN)) {
				ret = _FAIL;
				goto exit;
			}
		}

	} else if (check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE) {
		if (bmcast) {
			/* For AP mode, if DA == MCAST, then BSSID should be also MCAST */
			if (!IS_MCAST(pattrib->bssid)) {
				ret = _FAIL;
				goto exit;
			}
		} else { /* not mc-frame */
			/* For AP mode, if DA is non-MCAST, then it must be BSSID, and bssid == BSSID */
			if (!_rtw_memcmp(pattrib->bssid, pattrib->dst, ETH_ALEN)) {
				ret = _FAIL;
				goto exit;
			}
		}

	} else if (check_fwstate(pmlmepriv, WIFI_MP_STATE) == _TRUE) {
		_rtw_memcpy(pattrib->dst, GetAddr1Ptr(ptr), ETH_ALEN);
		_rtw_memcpy(pattrib->src, get_addr2_ptr(ptr), ETH_ALEN);
		_rtw_memcpy(pattrib->bssid, GetAddr3Ptr(ptr), ETH_ALEN);
		_rtw_memcpy(pattrib->ra, pattrib->dst, ETH_ALEN);
		_rtw_memcpy(pattrib->ta, pattrib->src, ETH_ALEN);

		sta_addr = mybssid;
	} else
		ret  = _FAIL;

#ifdef CONFIG_TDLS
	if (ptdls_sta == NULL)
#endif
		*psta = rtw_get_stainfo(pstapriv, sta_addr);

	if (*psta == NULL) {
#ifdef CONFIG_MP_INCLUDED
		if (adapter->registrypriv.mp_mode == 1) {
			if (check_fwstate(pmlmepriv, WIFI_MP_STATE) == _TRUE)
				adapter->mppriv.rx_pktloss++;
		}
#endif
		ret = _FAIL;
		goto exit;
	}

exit:
	return ret;

}

sint ap2sta_data_frame(
	_adapter *adapter,
	union recv_frame *precv_frame,
	struct sta_info **psta)
{
	u8 *ptr = precv_frame->u.hdr.rx_data;
	struct rx_pkt_attrib *pattrib = &precv_frame->u.hdr.attrib;
	sint ret = _SUCCESS;
	struct	sta_priv		*pstapriv = &adapter->stapriv;
	struct	mlme_priv	*pmlmepriv = &adapter->mlmepriv;
	u8 *mybssid  = get_bssid(pmlmepriv);
	u8 *myhwaddr = adapter_mac_addr(adapter);
	sint bmcast = IS_MCAST(pattrib->dst);


	if ((check_fwstate(pmlmepriv, WIFI_STATION_STATE) == _TRUE)
	    && (check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE
		|| check_fwstate(pmlmepriv, _FW_UNDER_LINKING) == _TRUE)
	   ) {

		/* filter packets that SA is myself or multicast or broadcast */
		if (_rtw_memcmp(myhwaddr, pattrib->src, ETH_ALEN)) {
			#ifdef DBG_RX_DROP_FRAME
			RTW_INFO("DBG_RX_DROP_FRAME "FUNC_ADPT_FMT" SA="MAC_FMT", myhwaddr="MAC_FMT"\n"
				, FUNC_ADPT_ARG(adapter), MAC_ARG(pattrib->src), MAC_ARG(myhwaddr));
			#endif
			ret = _FAIL;
			goto exit;
		}

		/* da should be for me */
		if ((!_rtw_memcmp(myhwaddr, pattrib->dst, ETH_ALEN)) && (!bmcast)) {
			#ifdef DBG_RX_DROP_FRAME
			RTW_INFO("DBG_RX_DROP_FRAME "FUNC_ADPT_FMT" DA="MAC_FMT"\n"
				, FUNC_ADPT_ARG(adapter), MAC_ARG(pattrib->dst));
			#endif
			ret = _FAIL;
			goto exit;
		}


		/* check BSSID */
		if (_rtw_memcmp(pattrib->bssid, "\x0\x0\x0\x0\x0\x0", ETH_ALEN) ||
		    _rtw_memcmp(mybssid, "\x0\x0\x0\x0\x0\x0", ETH_ALEN) ||
		    (!_rtw_memcmp(pattrib->bssid, mybssid, ETH_ALEN))) {
			#ifdef DBG_RX_DROP_FRAME
			RTW_INFO("DBG_RX_DROP_FRAME "FUNC_ADPT_FMT" BSSID="MAC_FMT", mybssid="MAC_FMT"\n"
				, FUNC_ADPT_ARG(adapter), MAC_ARG(pattrib->bssid), MAC_ARG(mybssid));
			#endif

			if (!bmcast) {
				RTW_INFO(ADPT_FMT" -issue_deauth to the nonassociated ap=" MAC_FMT " for the reason(7)\n", ADPT_ARG(adapter), MAC_ARG(pattrib->bssid));
				issue_deauth(adapter, pattrib->bssid, WLAN_REASON_CLASS3_FRAME_FROM_NONASSOC_STA);
			}

			ret = _FAIL;
			goto exit;
		}

		*psta = rtw_get_stainfo(pstapriv, pattrib->ta);
		if (*psta == NULL) {
			#ifdef DBG_RX_DROP_FRAME
			RTW_INFO("DBG_RX_DROP_FRAME "FUNC_ADPT_FMT" can't get psta under STATION_MODE ; drop pkt\n"
				, FUNC_ADPT_ARG(adapter));
			#endif
			ret = _FAIL;
			goto exit;
		}

		/*if ((get_frame_sub_type(ptr) & WIFI_QOS_DATA_TYPE) == WIFI_QOS_DATA_TYPE) {
		}
		*/

		if (get_frame_sub_type(ptr) & BIT(6)) {
			/* No data, will not indicate to upper layer, temporily count it here */
			count_rx_stats(adapter, precv_frame, *psta);
			ret = RTW_RX_HANDLED;
			goto exit;
		}

	} else if ((check_fwstate(pmlmepriv, WIFI_MP_STATE) == _TRUE) &&
		   (check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE)) {
		_rtw_memcpy(pattrib->dst, GetAddr1Ptr(ptr), ETH_ALEN);
		_rtw_memcpy(pattrib->src, get_addr2_ptr(ptr), ETH_ALEN);
		_rtw_memcpy(pattrib->bssid, GetAddr3Ptr(ptr), ETH_ALEN);
		_rtw_memcpy(pattrib->ra, pattrib->dst, ETH_ALEN);
		_rtw_memcpy(pattrib->ta, pattrib->src, ETH_ALEN);


		*psta = rtw_get_stainfo(pstapriv, pattrib->bssid); /* get sta_info */
		if (*psta == NULL) {
			#ifdef DBG_RX_DROP_FRAME
			RTW_INFO("DBG_RX_DROP_FRAME "FUNC_ADPT_FMT" can't get psta under WIFI_MP_STATE ; drop pkt\n"
				, FUNC_ADPT_ARG(adapter));
			#endif
			ret = _FAIL;
			goto exit;
		}


	} else if (check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE) {
		/* Special case */
		ret = RTW_RX_HANDLED;
		goto exit;
	} else {
		if (_rtw_memcmp(myhwaddr, pattrib->dst, ETH_ALEN) && (!bmcast)) {
			*psta = rtw_get_stainfo(pstapriv, pattrib->ta);
			if (*psta == NULL) {

				/* for AP multicast issue , modify by yiwei */
				static systime send_issue_deauth_time = 0;

				/* RTW_INFO("After send deauth , %u ms has elapsed.\n", rtw_get_passing_time_ms(send_issue_deauth_time)); */

				if (rtw_get_passing_time_ms(send_issue_deauth_time) > 10000 || send_issue_deauth_time == 0) {
					send_issue_deauth_time = rtw_get_current_time();

					RTW_INFO("issue_deauth to the ap=" MAC_FMT " for the reason(7)\n", MAC_ARG(pattrib->bssid));

					issue_deauth(adapter, pattrib->bssid, WLAN_REASON_CLASS3_FRAME_FROM_NONASSOC_STA);
				}
			}
		}

		ret = _FAIL;
		#ifdef DBG_RX_DROP_FRAME
		RTW_INFO("DBG_RX_DROP_FRAME "FUNC_ADPT_FMT" fw_state:0x%x\n"
			, FUNC_ADPT_ARG(adapter), get_fwstate(pmlmepriv));
		#endif
	}

exit:


	return ret;

}

sint sta2ap_data_frame(
	_adapter *adapter,
	union recv_frame *precv_frame,
	struct sta_info **psta)
{
	u8 *ptr = precv_frame->u.hdr.rx_data;
	struct rx_pkt_attrib *pattrib = &precv_frame->u.hdr.attrib;
	struct	sta_priv		*pstapriv = &adapter->stapriv;
	struct	mlme_priv	*pmlmepriv = &adapter->mlmepriv;
	unsigned char *mybssid  = get_bssid(pmlmepriv);
	sint ret = _SUCCESS;


	if (check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE) {
		/* For AP mode, RA=BSSID, TX=STA(SRC_ADDR), A3=DST_ADDR */
		if (!_rtw_memcmp(pattrib->bssid, mybssid, ETH_ALEN)) {
			ret = _FAIL;
			goto exit;
		}

		*psta = rtw_get_stainfo(pstapriv, pattrib->ta);
		if (*psta == NULL) {
			#ifdef CONFIG_DFS_MASTER
			struct rf_ctl_t *rfctl = adapter_to_rfctl(adapter);

			/* prevent RX tasklet blocks cmd_thread */
			if (rfctl->radar_detected == 1)
				goto bypass_deauth7;
			#endif

			RTW_INFO("issue_deauth to sta=" MAC_FMT " for the reason(7)\n", MAC_ARG(pattrib->src));

			issue_deauth(adapter, pattrib->src, WLAN_REASON_CLASS3_FRAME_FROM_NONASSOC_STA);

#ifdef CONFIG_DFS_MASTER
bypass_deauth7:
#endif
			ret = RTW_RX_HANDLED;
			goto exit;
		}

		process_pwrbit_data(adapter, precv_frame, *psta);

		if ((get_frame_sub_type(ptr) & WIFI_QOS_DATA_TYPE) == WIFI_QOS_DATA_TYPE)
			process_wmmps_data(adapter, precv_frame, *psta);

		if (get_frame_sub_type(ptr) & BIT(6)) {
			/* No data, will not indicate to upper layer, temporily count it here */
			count_rx_stats(adapter, precv_frame, *psta);
			ret = RTW_RX_HANDLED;
			goto exit;
		}
	} else if ((check_fwstate(pmlmepriv, WIFI_MP_STATE) == _TRUE) &&
		   (check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE)) {
		/* RTW_INFO("%s ,in WIFI_MP_STATE\n",__func__); */
		_rtw_memcpy(pattrib->dst, GetAddr1Ptr(ptr), ETH_ALEN);
		_rtw_memcpy(pattrib->src, get_addr2_ptr(ptr), ETH_ALEN);
		_rtw_memcpy(pattrib->bssid, GetAddr3Ptr(ptr), ETH_ALEN);
		_rtw_memcpy(pattrib->ra, pattrib->dst, ETH_ALEN);
		_rtw_memcpy(pattrib->ta, pattrib->src, ETH_ALEN);


		*psta = rtw_get_stainfo(pstapriv, pattrib->bssid); /* get sta_info */
		if (*psta == NULL) {
			#ifdef DBG_RX_DROP_FRAME
			RTW_INFO("DBG_RX_DROP_FRAME "FUNC_ADPT_FMT" can't get psta under WIFI_MP_STATE ; drop pkt\n"
				, FUNC_ADPT_ARG(adapter));
			#endif
			ret = _FAIL;
			goto exit;
		}

	} else {
		u8 *myhwaddr = adapter_mac_addr(adapter);
		if (!_rtw_memcmp(pattrib->ra, myhwaddr, ETH_ALEN)) {
			ret = RTW_RX_HANDLED;
			goto exit;
		}
		RTW_INFO("issue_deauth to sta=" MAC_FMT " for the reason(7)\n", MAC_ARG(pattrib->src));
		issue_deauth(adapter, pattrib->src, WLAN_REASON_CLASS3_FRAME_FROM_NONASSOC_STA);
		ret = RTW_RX_HANDLED;
		goto exit;
	}

exit:


	return ret;

}

sint validate_recv_ctrl_frame(_adapter *padapter, union recv_frame *precv_frame);
sint validate_recv_ctrl_frame(_adapter *padapter, union recv_frame *precv_frame)
{
	struct rx_pkt_attrib *pattrib = &precv_frame->u.hdr.attrib;
	struct sta_priv *pstapriv = &padapter->stapriv;
	u8 *pframe = precv_frame->u.hdr.rx_data;
	struct sta_info *psta = NULL;
	/* uint len = precv_frame->u.hdr.len; */

	/* RTW_INFO("+validate_recv_ctrl_frame\n"); */

	if (GetFrameType(pframe) != WIFI_CTRL_TYPE)
		return _FAIL;

	/* receive the frames that ra(a1) is my address */
	if (!_rtw_memcmp(GetAddr1Ptr(pframe), adapter_mac_addr(padapter), ETH_ALEN))
		return _FAIL;

	psta = rtw_get_stainfo(pstapriv, get_addr2_ptr(pframe));
	if (psta == NULL)
		return _FAIL;

	/* for rx pkt statistics */
	psta->sta_stats.rx_ctrl_pkts++;

	/* only handle ps-poll */
	if (get_frame_sub_type(pframe) == WIFI_PSPOLL) {
#ifdef CONFIG_AP_MODE
		u16 aid;
		u8 wmmps_ac = 0;

		aid = GetAid(pframe);
		if (psta->cmn.aid != aid)
			return _FAIL;

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
			return _FAIL;

		if (psta->state & WIFI_STA_ALIVE_CHK_STATE) {
			RTW_INFO("%s alive check-rx ps-poll\n", __func__);
			psta->expire_to = pstapriv->expire_to;
			psta->state ^= WIFI_STA_ALIVE_CHK_STATE;
		}

		if ((psta->state & WIFI_SLEEP_STATE) && (pstapriv->sta_dz_bitmap & BIT(psta->cmn.aid))) {
			_irqL irqL;
			_list	*xmitframe_plist, *xmitframe_phead;
			struct xmit_frame *pxmitframe = NULL;
			struct xmit_priv *pxmitpriv = &padapter->xmitpriv;

			/* _enter_critical_bh(&psta->sleep_q.lock, &irqL); */
			_enter_critical_bh(&pxmitpriv->lock, &irqL);

			xmitframe_phead = get_list_head(&psta->sleep_q);
			xmitframe_plist = get_next(xmitframe_phead);

			if ((rtw_end_of_queue_search(xmitframe_phead, xmitframe_plist)) == _FALSE) {
				pxmitframe = LIST_CONTAINOR(xmitframe_plist, struct xmit_frame, list);

				xmitframe_plist = get_next(xmitframe_plist);

				rtw_list_delete(&pxmitframe->list);

				psta->sleepq_len--;

				if (psta->sleepq_len > 0)
					pxmitframe->attrib.mdata = 1;
				else
					pxmitframe->attrib.mdata = 0;

				pxmitframe->attrib.triggered = 1;

				/* RTW_INFO("handling ps-poll, q_len=%d, tim=%x\n", psta->sleepq_len, pstapriv->tim_bitmap); */

#if 0
				_exit_critical_bh(&psta->sleep_q.lock, &irqL);
				if (rtw_hal_xmit(padapter, pxmitframe) == _TRUE)
					rtw_os_xmit_complete(padapter, pxmitframe);
				_enter_critical_bh(&psta->sleep_q.lock, &irqL);
#endif
				rtw_hal_xmitframe_enqueue(padapter, pxmitframe);

				if (psta->sleepq_len == 0) {
					pstapriv->tim_bitmap &= ~BIT(psta->cmn.aid);

					/* RTW_INFO("after handling ps-poll, tim=%x\n", pstapriv->tim_bitmap); */

					/* upate BCN for TIM IE */
					/* update_BCNTIM(padapter);		 */
					update_beacon(padapter, _TIM_IE_, NULL, _TRUE);
				}

				/* _exit_critical_bh(&psta->sleep_q.lock, &irqL); */
				_exit_critical_bh(&pxmitpriv->lock, &irqL);

			} else {
				/* _exit_critical_bh(&psta->sleep_q.lock, &irqL); */
				_exit_critical_bh(&pxmitpriv->lock, &irqL);

				/* RTW_INFO("no buffered packets to xmit\n"); */
				if (pstapriv->tim_bitmap & BIT(psta->cmn.aid)) {
					if (psta->sleepq_len == 0) {
						RTW_INFO("no buffered packets to xmit\n");

						/* issue nulldata with More data bit = 0 to indicate we have no buffered packets */
						issue_nulldata(padapter, psta->cmn.mac_addr, 0, 0, 0);
					} else {
						RTW_INFO("error!psta->sleepq_len=%d\n", psta->sleepq_len);
						psta->sleepq_len = 0;
					}

					pstapriv->tim_bitmap &= ~BIT(psta->cmn.aid);

					/* upate BCN for TIM IE */
					/* update_BCNTIM(padapter); */
					update_beacon(padapter, _TIM_IE_, NULL, _TRUE);
				}
			}
		}
#endif /* CONFIG_AP_MODE */
	} else if (get_frame_sub_type(pframe) == WIFI_NDPA) {
#ifdef CONFIG_BEAMFORMING
		rtw_beamforming_get_ndpa_frame(padapter, precv_frame);
#endif/*CONFIG_BEAMFORMING*/
	}

	return _FAIL;

}

union recv_frame *recvframe_chk_defrag(PADAPTER padapter, union recv_frame *precv_frame);
sint validate_recv_mgnt_frame(PADAPTER padapter, union recv_frame *precv_frame);
sint validate_recv_mgnt_frame(PADAPTER padapter, union recv_frame *precv_frame)
{
	/* struct mlme_priv *pmlmepriv = &adapter->mlmepriv; */

	precv_frame = recvframe_chk_defrag(padapter, precv_frame);
	if (precv_frame == NULL) {
		return _SUCCESS;
	}

	{
		/* for rx pkt statistics */
		struct sta_info *psta = rtw_get_stainfo(&padapter->stapriv, get_addr2_ptr(precv_frame->u.hdr.rx_data));
		if (psta) {
			psta->sta_stats.rx_mgnt_pkts++;
			if (get_frame_sub_type(precv_frame->u.hdr.rx_data) == WIFI_BEACON)
				psta->sta_stats.rx_beacon_pkts++;
			else if (get_frame_sub_type(precv_frame->u.hdr.rx_data) == WIFI_PROBEREQ)
				psta->sta_stats.rx_probereq_pkts++;
			else if (get_frame_sub_type(precv_frame->u.hdr.rx_data) == WIFI_PROBERSP) {
				if (_rtw_memcmp(adapter_mac_addr(padapter), GetAddr1Ptr(precv_frame->u.hdr.rx_data), ETH_ALEN) == _TRUE)
					psta->sta_stats.rx_probersp_pkts++;
				else if (is_broadcast_mac_addr(GetAddr1Ptr(precv_frame->u.hdr.rx_data))
					|| is_multicast_mac_addr(GetAddr1Ptr(precv_frame->u.hdr.rx_data)))
					psta->sta_stats.rx_probersp_bm_pkts++;
				else
					psta->sta_stats.rx_probersp_uo_pkts++;
			}
		}
	}

#ifdef CONFIG_INTEL_PROXIM
	if (padapter->proximity.proxim_on == _TRUE) {
		struct rx_pkt_attrib *pattrib = &precv_frame->u.hdr.attrib;
		struct recv_stat *prxstat = (struct recv_stat *)  precv_frame->u.hdr.rx_head ;
		u8 *pda, *psa, *pbssid, *ptr;
		ptr = precv_frame->u.hdr.rx_data;
		pda = get_da(ptr);
		psa = get_sa(ptr);
		pbssid = get_hdr_bssid(ptr);


		_rtw_memcpy(pattrib->dst, pda, ETH_ALEN);
		_rtw_memcpy(pattrib->src, psa, ETH_ALEN);

		_rtw_memcpy(pattrib->bssid, pbssid, ETH_ALEN);

		switch (pattrib->to_fr_ds) {
		case 0:
			_rtw_memcpy(pattrib->ra, pda, ETH_ALEN);
			_rtw_memcpy(pattrib->ta, psa, ETH_ALEN);
			break;

		case 1:
			_rtw_memcpy(pattrib->ra, pda, ETH_ALEN);
			_rtw_memcpy(pattrib->ta, pbssid, ETH_ALEN);
			break;

		case 2:
			_rtw_memcpy(pattrib->ra, pbssid, ETH_ALEN);
			_rtw_memcpy(pattrib->ta, psa, ETH_ALEN);
			break;

		case 3:
			_rtw_memcpy(pattrib->ra, GetAddr1Ptr(ptr), ETH_ALEN);
			_rtw_memcpy(pattrib->ta, get_addr2_ptr(ptr), ETH_ALEN);
			break;

		default:
			break;

		}
		pattrib->priority = 0;
		pattrib->hdrlen = pattrib->to_fr_ds == 3 ? 30 : 24;

		padapter->proximity.proxim_rx(padapter, precv_frame);
	}
#endif
	mgt_dispatcher(padapter, precv_frame);

	return _SUCCESS;

}

sint validate_recv_data_frame(_adapter *adapter, union recv_frame *precv_frame)
{
	u8 bretry;
	struct sta_info *psta = NULL;
	u8 *ptr = precv_frame->u.hdr.rx_data;
	struct rx_pkt_attrib	*pattrib = &precv_frame->u.hdr.attrib;
	struct sta_priv	*pstapriv = &adapter->stapriv;
	struct security_priv	*psecuritypriv = &adapter->securitypriv;
	sint ret = _SUCCESS;

	bretry = GetRetry(ptr);

	switch (pattrib->to_fr_ds) {
	case 0:
		_rtw_memcpy(pattrib->ra, GetAddr1Ptr(ptr), ETH_ALEN);
		_rtw_memcpy(pattrib->ta, get_addr2_ptr(ptr), ETH_ALEN);
		_rtw_memcpy(pattrib->dst, GetAddr1Ptr(ptr), ETH_ALEN);
		_rtw_memcpy(pattrib->src, get_addr2_ptr(ptr), ETH_ALEN);
		_rtw_memcpy(pattrib->bssid, GetAddr3Ptr(ptr), ETH_ALEN);
		ret = sta2sta_data_frame(adapter, precv_frame, &psta);
		break;

	case 1:
		_rtw_memcpy(pattrib->ra, GetAddr1Ptr(ptr), ETH_ALEN);
		_rtw_memcpy(pattrib->ta, get_addr2_ptr(ptr), ETH_ALEN);
		_rtw_memcpy(pattrib->dst, GetAddr1Ptr(ptr), ETH_ALEN);
		_rtw_memcpy(pattrib->src, GetAddr3Ptr(ptr), ETH_ALEN);
		_rtw_memcpy(pattrib->bssid, get_addr2_ptr(ptr), ETH_ALEN);
		ret = ap2sta_data_frame(adapter, precv_frame, &psta);
		break;

	case 2:
		_rtw_memcpy(pattrib->ra, GetAddr1Ptr(ptr), ETH_ALEN);
		_rtw_memcpy(pattrib->ta, get_addr2_ptr(ptr), ETH_ALEN);
		_rtw_memcpy(pattrib->dst, GetAddr3Ptr(ptr), ETH_ALEN);
		_rtw_memcpy(pattrib->src, get_addr2_ptr(ptr), ETH_ALEN);
		_rtw_memcpy(pattrib->bssid, GetAddr1Ptr(ptr), ETH_ALEN);
		ret = sta2ap_data_frame(adapter, precv_frame, &psta);
		break;

	case 3:
	default:
		/* WDS is not supported */
		ret = _FAIL;
		break;
	}

	if (ret == _FAIL) {
		#ifdef DBG_RX_DROP_FRAME
		RTW_INFO("DBG_RX_DROP_FRAME "FUNC_ADPT_FMT" case:%d, res:%d\n"
			, FUNC_ADPT_ARG(adapter), pattrib->to_fr_ds, ret);
		#endif
		goto exit;
	} else if (ret == RTW_RX_HANDLED)
		goto exit;


	if (psta == NULL) {
		#ifdef DBG_RX_DROP_FRAME
		RTW_INFO("DBG_RX_DROP_FRAME "FUNC_ADPT_FMT" psta == NULL, ra="MAC_FMT", ta="MAC_FMT"\n"
			, FUNC_ADPT_ARG(adapter), MAC_ARG(pattrib->ra), MAC_ARG(pattrib->ta));
		#endif
		ret = _FAIL;
		goto exit;
	}

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
			adapter->recvpriv.is_any_non_be_pkts = _TRUE;
		else
			adapter->recvpriv.is_any_non_be_pkts = _FALSE;
	} else {
		pattrib->priority = 0;
		pattrib->hdrlen = pattrib->to_fr_ds == 3 ? 30 : 24;
	}


	if (pattrib->order) /* HT-CTRL 11n */
		pattrib->hdrlen += 4;

	if (!IS_MCAST(pattrib->ra)) {
		precv_frame->u.hdr.preorder_ctrl = &psta->recvreorder_ctrl[pattrib->priority];

		/* decache, drop duplicate recv packets */
		if ((recv_decache(precv_frame, bretry) == _FAIL) ||
			(recv_ucast_pn_decache(precv_frame) == _FAIL)) {
			#ifdef DBG_RX_DROP_FRAME
			RTW_INFO("DBG_RX_DROP_FRAME "FUNC_ADPT_FMT" recv_decache return _FAIL for sta="MAC_FMT"\n"
				, FUNC_ADPT_ARG(adapter), MAC_ARG(psta->cmn.mac_addr));
			#endif
			ret = _FAIL;
			goto exit;
		}
	} else {
		if (recv_bcast_pn_decache(precv_frame) == _FAIL) {
			#ifdef DBG_RX_DROP_FRAME
			RTW_INFO("DBG_RX_DROP_FRAME "FUNC_ADPT_FMT" recv_bcast_pn_decache _FAIL for invalid PN!\n"
				, FUNC_ADPT_ARG(adapter));
			#endif
			ret = _FAIL;
			goto exit;
		}	

		precv_frame->u.hdr.preorder_ctrl = NULL;
	}

	if (pattrib->privacy) {
#ifdef CONFIG_TDLS
		if ((psta->tdls_sta_state & TDLS_LINKED_STATE) && (psta->dot118021XPrivacy == _AES_))
			pattrib->encrypt = psta->dot118021XPrivacy;
		else
#endif /* CONFIG_TDLS */
			GET_ENCRY_ALGO(psecuritypriv, psta, pattrib->encrypt, IS_MCAST(pattrib->ra));


		SET_ICE_IV_LEN(pattrib->iv_len, pattrib->icv_len, pattrib->encrypt);
	} else {
		pattrib->encrypt = 0;
		pattrib->iv_len = pattrib->icv_len = 0;
	}

exit:


	return ret;
}

#ifdef CONFIG_IEEE80211W
static sint validate_80211w_mgmt(_adapter *adapter, union recv_frame *precv_frame)
{
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	struct rx_pkt_attrib *pattrib = &precv_frame->u.hdr.attrib;
	u8 *ptr = precv_frame->u.hdr.rx_data;
	struct sta_info	*psta;
	struct sta_priv		*pstapriv = &adapter->stapriv;
	u8 type;
	u8 subtype;

	type =  GetFrameType(ptr);
	subtype = get_frame_sub_type(ptr); /* bit(7)~bit(2) */

	if (adapter->securitypriv.binstallBIPkey == _TRUE) {
		/* unicast management frame decrypt */
		if (pattrib->privacy && !(IS_MCAST(GetAddr1Ptr(ptr))) &&
		    (subtype == WIFI_DEAUTH || subtype == WIFI_DISASSOC || subtype == WIFI_ACTION)) {
			u8 *ppp, *mgmt_DATA;
			u32 data_len = 0;
			ppp = get_addr2_ptr(ptr);

			pattrib->bdecrypted = 0;
			pattrib->encrypt = _AES_;
			pattrib->hdrlen = sizeof(struct rtw_ieee80211_hdr_3addr);
			/* set iv and icv length */
			SET_ICE_IV_LEN(pattrib->iv_len, pattrib->icv_len, pattrib->encrypt);
			_rtw_memcpy(pattrib->ra, GetAddr1Ptr(ptr), ETH_ALEN);
			_rtw_memcpy(pattrib->ta, get_addr2_ptr(ptr), ETH_ALEN);
			/* actual management data frame body */
			data_len = pattrib->pkt_len - pattrib->hdrlen - pattrib->iv_len - pattrib->icv_len;
			mgmt_DATA = rtw_zmalloc(data_len);
			if (mgmt_DATA == NULL) {
				RTW_INFO("%s mgmt allocate fail  !!!!!!!!!\n", __FUNCTION__);
				goto validate_80211w_fail;
			}
#if 0
			/* dump the packet content before decrypt */
			{
				int pp;
				printk("pattrib->pktlen = %d =>", pattrib->pkt_len);
				for (pp = 0; pp < pattrib->pkt_len; pp++)
					printk(" %02x ", ptr[pp]);
				printk("\n");
			}
#endif

			precv_frame = decryptor(adapter, precv_frame);
			/* save actual management data frame body */
			_rtw_memcpy(mgmt_DATA, ptr + pattrib->hdrlen + pattrib->iv_len, data_len);
			/* overwrite the iv field */
			_rtw_memcpy(ptr + pattrib->hdrlen, mgmt_DATA, data_len);
			/* remove the iv and icv length */
			pattrib->pkt_len = pattrib->pkt_len - pattrib->iv_len - pattrib->icv_len;
			rtw_mfree(mgmt_DATA, data_len);
#if 0
			/* print packet content after decryption */
			{
				int pp;
				printk("after decryption pattrib->pktlen = %d @@=>", pattrib->pkt_len);
				for (pp = 0; pp < pattrib->pkt_len; pp++)
					printk(" %02x ", ptr[pp]);
				printk("\n");
			}
#endif
			if (!precv_frame) {
				RTW_INFO("%s mgmt descrypt fail  !!!!!!!!!\n", __FUNCTION__);
				goto validate_80211w_fail;
			}
		} else if (IS_MCAST(GetAddr1Ptr(ptr)) &&
			(subtype == WIFI_DEAUTH || subtype == WIFI_DISASSOC)) {
			sint BIP_ret = _SUCCESS;
			/* verify BIP MME IE of broadcast/multicast de-auth/disassoc packet */
			BIP_ret = rtw_BIP_verify(adapter, (u8 *)precv_frame);
			if (BIP_ret == _FAIL) {
				/* RTW_INFO("802.11w BIP verify fail\n"); */
				goto validate_80211w_fail;
			} else if (BIP_ret == RTW_RX_HANDLED) {
				RTW_INFO("802.11w recv none protected packet\n");
				/* drop pkt, don't issue sa query request */
				/* issue_action_SA_Query(adapter, NULL, 0, 0, 0); */
				goto validate_80211w_fail;
			}
		} /* 802.11w protect */
		else {
			psta = rtw_get_stainfo(pstapriv, get_addr2_ptr(ptr));

			if (subtype == WIFI_ACTION && psta && psta->bpairwise_key_installed == _TRUE) {
				/* according 802.11-2012 standard, these five types are not robust types */
				if (ptr[WLAN_HDR_A3_LEN] != RTW_WLAN_CATEGORY_PUBLIC          &&
				    ptr[WLAN_HDR_A3_LEN] != RTW_WLAN_CATEGORY_HT              &&
				    ptr[WLAN_HDR_A3_LEN] != RTW_WLAN_CATEGORY_UNPROTECTED_WNM &&
				    ptr[WLAN_HDR_A3_LEN] != RTW_WLAN_CATEGORY_SELF_PROTECTED  &&
				    ptr[WLAN_HDR_A3_LEN] != RTW_WLAN_CATEGORY_P2P) {
					RTW_INFO("action frame category=%d should robust\n", ptr[WLAN_HDR_A3_LEN]);
					goto validate_80211w_fail;
				}
			} else if (subtype == WIFI_DEAUTH || subtype == WIFI_DISASSOC) {
				unsigned short	reason;
				reason = le16_to_cpu(*(unsigned short *)(ptr + WLAN_HDR_A3_LEN));
				RTW_INFO("802.11w recv none protected packet, reason=%d\n", reason);
				if (reason == 6 || reason == 7) {
					/* issue sa query request */
					issue_action_SA_Query(adapter, NULL, 0, 0, IEEE80211W_RIGHT_KEY);
				}
				goto validate_80211w_fail;
			}
		}
	}
	return _SUCCESS;

validate_80211w_fail:
	return _FAIL;

}
#endif /* CONFIG_IEEE80211W */

static inline void dump_rx_packet(u8 *ptr)
{
	int i;

	RTW_INFO("#############################\n");
	for (i = 0; i < 64; i = i + 8)
		RTW_INFO("%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:\n", *(ptr + i),
			*(ptr + i + 1), *(ptr + i + 2) , *(ptr + i + 3) , *(ptr + i + 4), *(ptr + i + 5), *(ptr + i + 6), *(ptr + i + 7));
	RTW_INFO("#############################\n");
}

sint validate_recv_frame(_adapter *adapter, union recv_frame *precv_frame);
sint validate_recv_frame(_adapter *adapter, union recv_frame *precv_frame)
{
	/* shall check frame subtype, to / from ds, da, bssid */

	/* then call check if rx seq/frag. duplicated. */

	u8 type;
	u8 subtype;
	sint retval = _SUCCESS;

	struct rx_pkt_attrib *pattrib = &precv_frame->u.hdr.attrib;
	struct recv_priv  *precvpriv = &adapter->recvpriv;

	u8 *ptr = precv_frame->u.hdr.rx_data;
	u8  ver = (unsigned char)(*ptr) & 0x3 ;
#ifdef CONFIG_FIND_BEST_CHANNEL
	struct rf_ctl_t *rfctl = adapter_to_rfctl(adapter);
	struct mlme_ext_priv *pmlmeext = &adapter->mlmeextpriv;
#endif

#ifdef CONFIG_TDLS
	struct tdls_info *ptdlsinfo = &adapter->tdlsinfo;
#endif /* CONFIG_TDLS */
#ifdef CONFIG_WAPI_SUPPORT
	PRT_WAPI_T	pWapiInfo = &adapter->wapiInfo;
	struct recv_frame_hdr *phdr = &precv_frame->u.hdr;
	u8 wai_pkt = 0;
	u16 sc;
	u8	external_len = 0;
#endif


#ifdef CONFIG_FIND_BEST_CHANNEL
	if (pmlmeext->sitesurvey_res.state == SCAN_PROCESS) {
		int ch_set_idx = rtw_chset_search_ch(rfctl->channel_set, rtw_get_oper_ch(adapter));
		if (ch_set_idx >= 0)
			rfctl->channel_set[ch_set_idx].rx_count++;
	}
#endif

#ifdef CONFIG_TDLS
	if (ptdlsinfo->ch_sensing == 1 && ptdlsinfo->cur_channel != 0)
		ptdlsinfo->collect_pkt_num[ptdlsinfo->cur_channel - 1]++;
#endif /* CONFIG_TDLS */

#ifdef RTK_DMP_PLATFORM
	if (0) {
		RTW_INFO("++\n");
		{
			int i;
			for (i = 0; i < 64; i = i + 8)
				RTW_INFO("%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:", *(ptr + i),
					*(ptr + i + 1), *(ptr + i + 2) , *(ptr + i + 3) , *(ptr + i + 4), *(ptr + i + 5), *(ptr + i + 6), *(ptr + i + 7));

		}
		RTW_INFO("--\n");
	}
#endif /* RTK_DMP_PLATFORM */

	/* add version chk */
	if (ver != 0) {
		retval = _FAIL;
		DBG_COUNTER(adapter->rx_logs.core_rx_pre_ver_err);
		goto exit;
	}

	type =  GetFrameType(ptr);
	subtype = get_frame_sub_type(ptr); /* bit(7)~bit(2) */

	pattrib->to_fr_ds = get_tofr_ds(ptr);

	pattrib->frag_num = GetFragNum(ptr);
	pattrib->seq_num = GetSequence(ptr);

	pattrib->pw_save = GetPwrMgt(ptr);
	pattrib->mfrag = GetMFrag(ptr);
	pattrib->mdata = GetMData(ptr);
	pattrib->privacy = GetPrivacy(ptr);
	pattrib->order = GetOrder(ptr);
#ifdef CONFIG_WAPI_SUPPORT
	sc = (pattrib->seq_num << 4) | pattrib->frag_num;
#endif

#if 1 /* Dump rx packets */
	{
		u8 bDumpRxPkt = 0;

		rtw_hal_get_def_var(adapter, HAL_DEF_DBG_DUMP_RXPKT, &(bDumpRxPkt));
		if (bDumpRxPkt == 1) /* dump all rx packets */
			dump_rx_packet(ptr);
		else if ((bDumpRxPkt == 2) && (type == WIFI_MGT_TYPE))
			dump_rx_packet(ptr);
		else if ((bDumpRxPkt == 3) && (type == WIFI_DATA_TYPE))
			dump_rx_packet(ptr);
	}
#endif
	switch (type) {
	case WIFI_MGT_TYPE: /* mgnt */
		DBG_COUNTER(adapter->rx_logs.core_rx_pre_mgmt);
#ifdef CONFIG_IEEE80211W
		if (validate_80211w_mgmt(adapter, precv_frame) == _FAIL) {
			retval = _FAIL;
			DBG_COUNTER(adapter->rx_logs.core_rx_pre_mgmt_err_80211w);
			break;
		}
#endif /* CONFIG_IEEE80211W */

		retval = validate_recv_mgnt_frame(adapter, precv_frame);
		if (retval == _FAIL) {
			DBG_COUNTER(adapter->rx_logs.core_rx_pre_mgmt_err);
		}
		retval = _FAIL; /* only data frame return _SUCCESS */
		break;
	case WIFI_CTRL_TYPE: /* ctrl */
		DBG_COUNTER(adapter->rx_logs.core_rx_pre_ctrl);
		retval = validate_recv_ctrl_frame(adapter, precv_frame);
		if (retval == _FAIL) {
			DBG_COUNTER(adapter->rx_logs.core_rx_pre_ctrl_err);
		}
		retval = _FAIL; /* only data frame return _SUCCESS */
		break;
	case WIFI_DATA_TYPE: /* data */
		DBG_COUNTER(adapter->rx_logs.core_rx_pre_data);
#ifdef CONFIG_WAPI_SUPPORT
		if (pattrib->qos)
			external_len = 2;
		else
			external_len = 0;

		wai_pkt = rtw_wapi_is_wai_packet(adapter, ptr);

		phdr->bIsWaiPacket = wai_pkt;

		if (wai_pkt != 0) {
			if (sc != adapter->wapiInfo.wapiSeqnumAndFragNum)
				adapter->wapiInfo.wapiSeqnumAndFragNum = sc;
			else {
				retval = _FAIL;
				DBG_COUNTER(adapter->rx_logs.core_rx_pre_data_wapi_seq_err);
				break;
			}
		} else {

			if (rtw_wapi_drop_for_key_absent(adapter, get_addr2_ptr(ptr))) {
				retval = _FAIL;
				WAPI_TRACE(WAPI_RX, "drop for key absent for rx\n");
				DBG_COUNTER(adapter->rx_logs.core_rx_pre_data_wapi_key_err);
				break;
			}
		}

#endif

		pattrib->qos = (subtype & BIT(7)) ? 1 : 0;
		retval = validate_recv_data_frame(adapter, precv_frame);
		if (retval == _FAIL) {
			precvpriv->dbg_rx_drop_count++;
			DBG_COUNTER(adapter->rx_logs.core_rx_pre_data_err);
		} else if (retval == _SUCCESS) {
			#ifdef DBG_RX_DUMP_EAP
			if (!pattrib->encrypt || pattrib->bdecrypted) {
				u8 bDumpRxPkt;
				u16 eth_type;

				/* dump eapol */
				rtw_hal_get_def_var(adapter, HAL_DEF_DBG_DUMP_RXPKT, &(bDumpRxPkt));
				/* get ether_type */
				_rtw_memcpy(&eth_type, ptr + pattrib->hdrlen + pattrib->iv_len + LLC_HEADER_SIZE, 2);
				eth_type = ntohs((unsigned short) eth_type);
				if ((bDumpRxPkt == 4) && (eth_type == 0x888e))
					dump_rx_packet(ptr);
			}
			#endif
		} else
			DBG_COUNTER(adapter->rx_logs.core_rx_pre_data_handled);
		break;
	default:
		DBG_COUNTER(adapter->rx_logs.core_rx_pre_unknown);
		#ifdef DBG_RX_DROP_FRAME
		RTW_INFO("DBG_RX_DROP_FRAME "FUNC_ADPT_FMT" fail! type=0x%x\n"
			, FUNC_ADPT_ARG(adapter), type);
		#endif
		retval = _FAIL;
		break;
	}

exit:


	return retval;
}


/* remove the wlanhdr and add the eth_hdr */
#if 1
sint wlanhdr_to_ethhdr(union recv_frame *precvframe)
{
	sint	rmv_len;
	u16	eth_type, len;
	u8	bsnaphdr;
	u8	*psnap_type;
	struct ieee80211_snap_hdr	*psnap;

	sint ret = _SUCCESS;
	_adapter			*adapter = precvframe->u.hdr.adapter;
	struct mlme_priv	*pmlmepriv = &adapter->mlmepriv;

	u8	*ptr = get_recvframe_data(precvframe) ; /* point to frame_ctrl field */
	struct rx_pkt_attrib *pattrib = &precvframe->u.hdr.attrib;


	if (pattrib->encrypt)
		recvframe_pull_tail(precvframe, pattrib->icv_len);

	psnap = (struct ieee80211_snap_hdr *)(ptr + pattrib->hdrlen + pattrib->iv_len);
	psnap_type = ptr + pattrib->hdrlen + pattrib->iv_len + SNAP_SIZE;
	/* convert hdr + possible LLC headers into Ethernet header */
	/* eth_type = (psnap_type[0] << 8) | psnap_type[1]; */
	if ((_rtw_memcmp(psnap, rtw_rfc1042_header, SNAP_SIZE) &&
	     (_rtw_memcmp(psnap_type, SNAP_ETH_TYPE_IPX, 2) == _FALSE) &&
	     (_rtw_memcmp(psnap_type, SNAP_ETH_TYPE_APPLETALK_AARP, 2) == _FALSE)) ||
	    /* eth_type != ETH_P_AARP && eth_type != ETH_P_IPX) || */
	    _rtw_memcmp(psnap, rtw_bridge_tunnel_header, SNAP_SIZE)) {
		/* remove RFC1042 or Bridge-Tunnel encapsulation and replace EtherType */
		bsnaphdr = _TRUE;
	} else {
		/* Leave Ethernet header part of hdr and full payload */
		bsnaphdr = _FALSE;
	}

	rmv_len = pattrib->hdrlen + pattrib->iv_len + (bsnaphdr ? SNAP_SIZE : 0);
	len = precvframe->u.hdr.len - rmv_len;


	_rtw_memcpy(&eth_type, ptr + rmv_len, 2);
	eth_type = ntohs((unsigned short)eth_type); /* pattrib->ether_type */
	pattrib->eth_type = eth_type;


	if ((check_fwstate(pmlmepriv, WIFI_MP_STATE) == _TRUE)) {
		ptr += rmv_len ;
		*ptr = 0x87;
		*(ptr + 1) = 0x12;

		eth_type = 0x8712;
		/* append rx status for mp test packets */
		ptr = recvframe_pull(precvframe, (rmv_len - sizeof(struct ethhdr) + 2) - 24);
		if (!ptr) {
			ret = _FAIL;
			goto exiting;
		}
		_rtw_memcpy(ptr, get_rxmem(precvframe), 24);
		ptr += 24;
	} else {
		ptr = recvframe_pull(precvframe, (rmv_len - sizeof(struct ethhdr) + (bsnaphdr ? 2 : 0)));
		if (!ptr) {
			ret = _FAIL;
			goto exiting;
		}
	}

	if (ptr) {
		_rtw_memcpy(ptr, pattrib->dst, ETH_ALEN);
		_rtw_memcpy(ptr + ETH_ALEN, pattrib->src, ETH_ALEN);

		if (!bsnaphdr) {
			len = htons(len);
			_rtw_memcpy(ptr + 12, &len, 2);
		}
	}

exiting:
	return ret;

}

#else

sint wlanhdr_to_ethhdr(union recv_frame *precvframe)
{
	sint rmv_len;
	u16 eth_type;
	u8	bsnaphdr;
	u8	*psnap_type;
	struct ieee80211_snap_hdr	*psnap;

	sint ret = _SUCCESS;
	_adapter	*adapter = precvframe->u.hdr.adapter;
	struct	mlme_priv	*pmlmepriv = &adapter->mlmepriv;

	u8 *ptr = get_recvframe_data(precvframe) ; /* point to frame_ctrl field */
	struct rx_pkt_attrib *pattrib = &precvframe->u.hdr.attrib;
	struct _vlan *pvlan = NULL;


	psnap = (struct ieee80211_snap_hdr *)(ptr + pattrib->hdrlen + pattrib->iv_len);
	psnap_type = ptr + pattrib->hdrlen + pattrib->iv_len + SNAP_SIZE;
	if (psnap->dsap == 0xaa && psnap->ssap == 0xaa && psnap->ctrl == 0x03) {
		if (_rtw_memcmp(psnap->oui, oui_rfc1042, WLAN_IEEE_OUI_LEN))
			bsnaphdr = _TRUE; /* wlan_pkt_format = WLAN_PKT_FORMAT_SNAP_RFC1042;	 */
		else if (_rtw_memcmp(psnap->oui, SNAP_HDR_APPLETALK_DDP, WLAN_IEEE_OUI_LEN) &&
			_rtw_memcmp(psnap_type, SNAP_ETH_TYPE_APPLETALK_DDP, 2))
			bsnaphdr = _TRUE;	/* wlan_pkt_format = WLAN_PKT_FORMAT_APPLETALK; */
		else if (_rtw_memcmp(psnap->oui, oui_8021h, WLAN_IEEE_OUI_LEN))
			bsnaphdr = _TRUE;	/* wlan_pkt_format = WLAN_PKT_FORMAT_SNAP_TUNNEL; */
		else {
			ret = _FAIL;
			goto exit;
		}

	} else
		bsnaphdr = _FALSE; /* wlan_pkt_format = WLAN_PKT_FORMAT_OTHERS; */

	rmv_len = pattrib->hdrlen + pattrib->iv_len + (bsnaphdr ? SNAP_SIZE : 0);

	if (check_fwstate(pmlmepriv, WIFI_MP_STATE) == _TRUE) {
		ptr += rmv_len ;
		*ptr = 0x87;
		*(ptr + 1) = 0x12;

		/* back to original pointer */
		ptr -= rmv_len;
	}

	ptr += rmv_len ;

	_rtw_memcpy(&eth_type, ptr, 2);
	eth_type = ntohs((unsigned short)eth_type); /* pattrib->ether_type */
	ptr += 2;

	if (pattrib->encrypt)
		recvframe_pull_tail(precvframe, pattrib->icv_len);

	if (eth_type == 0x8100) { /* vlan */
		pvlan = (struct _vlan *) ptr;

		/* eth_type = get_vlan_encap_proto(pvlan); */
		/* eth_type = pvlan->h_vlan_encapsulated_proto; */ /* ? */
		rmv_len += 4;
		ptr += 4;
	}

	if (eth_type == 0x0800) { /* ip */
		/* struct iphdr*  piphdr = (struct iphdr*) ptr; */
		/* __u8 tos = (unsigned char)(pattrib->priority & 0xff); */

		/* piphdr->tos = tos; */

	} else if (eth_type == 0x8712) { /* append rx status for mp test packets */
		/* ptr -= 16; */
		/* _rtw_memcpy(ptr, get_rxmem(precvframe), 16); */
	} else {
#ifdef PLATFORM_OS_XP
		NDIS_PACKET_8021Q_INFO VlanPriInfo;
		UINT32 UserPriority = precvframe->u.hdr.attrib.priority;
		UINT32 VlanID = (pvlan != NULL ? get_vlan_id(pvlan) : 0);

		VlanPriInfo.Value =          /* Get current value. */
			NDIS_PER_PACKET_INFO_FROM_PACKET(precvframe->u.hdr.pkt, Ieee8021QInfo);

		VlanPriInfo.TagHeader.UserPriority = UserPriority;
		VlanPriInfo.TagHeader.VlanId =  VlanID ;

		VlanPriInfo.TagHeader.CanonicalFormatId = 0; /* Should be zero. */
		VlanPriInfo.TagHeader.Reserved = 0; /* Should be zero. */
		NDIS_PER_PACKET_INFO_FROM_PACKET(precvframe->u.hdr.pkt, Ieee8021QInfo) = VlanPriInfo.Value;
#endif
	}

	if (eth_type == 0x8712) { /* append rx status for mp test packets */
		ptr = recvframe_pull(precvframe, (rmv_len - sizeof(struct ethhdr) + 2) - 24);
		_rtw_memcpy(ptr, get_rxmem(precvframe), 24);
		ptr += 24;
	} else
		ptr = recvframe_pull(precvframe, (rmv_len - sizeof(struct ethhdr) + 2));

	_rtw_memcpy(ptr, pattrib->dst, ETH_ALEN);
	_rtw_memcpy(ptr + ETH_ALEN, pattrib->src, ETH_ALEN);

	eth_type = htons((unsigned short)eth_type) ;
	_rtw_memcpy(ptr + 12, &eth_type, 2);

exit:


	return ret;
}
#endif


#if defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
#ifdef PLATFORM_LINUX
static void recvframe_expand_pkt(
	PADAPTER padapter,
	union recv_frame *prframe)
{
	struct recv_frame_hdr *pfhdr;
	_pkt *ppkt;
	u8 shift_sz;
	u32 alloc_sz;
	u8 *ptr;


	pfhdr = &prframe->u.hdr;

	/*	6 is for IP header 8 bytes alignment in QoS packet case. */
	if (pfhdr->attrib.qos)
		shift_sz = 6;
	else
		shift_sz = 0;

	/* for first fragment packet, need to allocate */
	/* (1536 + RXDESC_SIZE + drvinfo_sz) to reassemble packet */
	/*	8 is for skb->data 8 bytes alignment.
	*	alloc_sz = _RND(1536 + RXDESC_SIZE + pfhdr->attrib.drvinfosize + shift_sz + 8, 128); */
	alloc_sz = 1664; /* round (1536 + 24 + 32 + shift_sz + 8) to 128 bytes alignment */

	/* 3 1. alloc new skb */
	/* prepare extra space for 4 bytes alignment */
	ppkt = rtw_skb_alloc(alloc_sz);

	if (!ppkt)
		return; /* no way to expand */

	/* 3 2. Prepare new skb to replace & release old skb */
	/* force ppkt->data at 8-byte alignment address */
	skb_reserve(ppkt, 8 - ((SIZE_PTR)ppkt->data & 7));
	/* force ip_hdr at 8-byte alignment address according to shift_sz */
	skb_reserve(ppkt, shift_sz);

	/* copy data to new pkt */
	ptr = skb_put(ppkt, pfhdr->len);
	if (ptr)
		_rtw_memcpy(ptr, pfhdr->rx_data, pfhdr->len);

	rtw_skb_free(pfhdr->pkt);

	/* attach new pkt to recvframe */
	pfhdr->pkt = ppkt;
	pfhdr->rx_head = ppkt->head;
	pfhdr->rx_data = ppkt->data;
	pfhdr->rx_tail = skb_tail_pointer(ppkt);
	pfhdr->rx_end = skb_end_pointer(ppkt);
}
#else
#warning "recvframe_expand_pkt not implement, defrag may crash system"
#endif
#endif

/* perform defrag */
union recv_frame *recvframe_defrag(_adapter *adapter, _queue *defrag_q);
union recv_frame *recvframe_defrag(_adapter *adapter, _queue *defrag_q)
{
	_list	*plist, *phead;
	u8	*data, wlanhdr_offset;
	u8	curfragnum;
	struct recv_frame_hdr *pfhdr, *pnfhdr;
	union recv_frame *prframe, *pnextrframe;
	_queue	*pfree_recv_queue;


	curfragnum = 0;
	pfree_recv_queue = &adapter->recvpriv.free_recv_queue;

	phead = get_list_head(defrag_q);
	plist = get_next(phead);
	prframe = LIST_CONTAINOR(plist, union recv_frame, u);
	pfhdr = &prframe->u.hdr;
	rtw_list_delete(&(prframe->u.list));

	if (curfragnum != pfhdr->attrib.frag_num) {
		/* the first fragment number must be 0 */
		/* free the whole queue */
		rtw_free_recvframe(prframe, pfree_recv_queue);
		rtw_free_recvframe_queue(defrag_q, pfree_recv_queue);

		return NULL;
	}

#if defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
#ifndef CONFIG_SDIO_RX_COPY
	recvframe_expand_pkt(adapter, prframe);
#endif
#endif

	curfragnum++;

	plist = get_list_head(defrag_q);

	plist = get_next(plist);

	data = get_recvframe_data(prframe);

	while (rtw_end_of_queue_search(phead, plist) == _FALSE) {
		pnextrframe = LIST_CONTAINOR(plist, union recv_frame , u);
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
		_rtw_memcpy(pfhdr->rx_tail, pnfhdr->rx_data, pnfhdr->len);

		recvframe_put(prframe, pnfhdr->len);

		pfhdr->attrib.icv_len = pnfhdr->attrib.icv_len;
		plist = get_next(plist);

	};

	/* free the defrag_q queue and return the prframe */
	rtw_free_recvframe_queue(defrag_q, pfree_recv_queue);



	return prframe;
}

/* check if need to defrag, if needed queue the frame to defrag_q */
union recv_frame *recvframe_chk_defrag(PADAPTER padapter, union recv_frame *precv_frame)
{
	u8	ismfrag;
	u8	fragnum;
	u8	*psta_addr;
	struct recv_frame_hdr *pfhdr;
	struct sta_info *psta;
	struct sta_priv *pstapriv;
	_list *phead;
	union recv_frame *prtnframe = NULL;
	_queue *pfree_recv_queue, *pdefrag_q;


	pstapriv = &padapter->stapriv;

	pfhdr = &precv_frame->u.hdr;

	pfree_recv_queue = &padapter->recvpriv.free_recv_queue;

	/* need to define struct of wlan header frame ctrl */
	ismfrag = pfhdr->attrib.mfrag;
	fragnum = pfhdr->attrib.frag_num;

	psta_addr = pfhdr->attrib.ta;
	psta = rtw_get_stainfo(pstapriv, psta_addr);
	if (psta == NULL) {
		u8 type = GetFrameType(pfhdr->rx_data);
		if (type != WIFI_DATA_TYPE) {
			psta = rtw_get_bcmc_stainfo(padapter);
			pdefrag_q = &psta->sta_recvpriv.defrag_q;
		} else
			pdefrag_q = NULL;
	} else
		pdefrag_q = &psta->sta_recvpriv.defrag_q;

	if ((ismfrag == 0) && (fragnum == 0)) {
		prtnframe = precv_frame;/* isn't a fragment frame */
	}

	if (ismfrag == 1) {
		/* 0~(n-1) fragment frame */
		/* enqueue to defraf_g */
		if (pdefrag_q != NULL) {
			if (fragnum == 0) {
				/* the first fragment */
				if (_rtw_queue_empty(pdefrag_q) == _FALSE) {
					/* free current defrag_q */
					rtw_free_recvframe_queue(pdefrag_q, pfree_recv_queue);
				}
			}


			/* Then enqueue the 0~(n-1) fragment into the defrag_q */

			/* _rtw_spinlock(&pdefrag_q->lock); */
			phead = get_list_head(pdefrag_q);
			rtw_list_insert_tail(&pfhdr->list, phead);
			/* _rtw_spinunlock(&pdefrag_q->lock); */


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
		if (pdefrag_q != NULL) {
			/* _rtw_spinlock(&pdefrag_q->lock); */
			phead = get_list_head(pdefrag_q);
			rtw_list_insert_tail(&pfhdr->list, phead);
			/* _rtw_spinunlock(&pdefrag_q->lock); */

			/* call recvframe_defrag to defrag */
			precv_frame = recvframe_defrag(padapter, pdefrag_q);
			prtnframe = precv_frame;

		} else {
			/* can't find this ta's defrag_queue, so free this recv_frame */
			rtw_free_recvframe(precv_frame, pfree_recv_queue);
			prtnframe = NULL;
		}

	}


	if ((prtnframe != NULL) && (prtnframe->u.hdr.attrib.privacy)) {
		/* after defrag we must check tkip mic code */
		if (recvframe_chkmic(padapter,  prtnframe) == _FAIL) {
			rtw_free_recvframe(prtnframe, pfree_recv_queue);
			prtnframe = NULL;
		}
	}


	return prtnframe;

}

static int rtw_recv_indicatepkt_check(union recv_frame *rframe, u8 *ehdr_pos, u32 pkt_len)
{
	_adapter *adapter = rframe->u.hdr.adapter;
	struct recv_priv *recvpriv = &adapter->recvpriv;
	struct ethhdr *ehdr = (struct ethhdr *)ehdr_pos;
	int ret = _FAIL;

#ifdef CONFIG_WAPI_SUPPORT
	if (rtw_wapi_check_for_drop(adapter, rframe, ehdr_pos)) {
		#ifdef DBG_RX_DROP_FRAME
		RTW_INFO("DBG_RX_DROP_FRAME "FUNC_ADPT_FMT" rtw_wapi_check_for_drop\n"
			, FUNC_ADPT_ARG(adapter));
		#endif
		goto exit;
	}
#endif

	if (rframe->u.hdr.psta)
		rtw_st_ctl_rx(rframe->u.hdr.psta, ehdr_pos);

	if (ntohs(ehdr->h_proto) == 0x888e)
		RTW_PRINT("recv eapol packet\n");

	if (recvpriv->sink_udpport > 0)
		rtw_sink_rtp_seq_dbg(adapter, ehdr_pos);

#ifdef DBG_UDP_PKT_LOSE_11AC
	#define PAYLOAD_LEN_LOC_OF_IP_HDR 0x10 /*ethernet payload length location of ip header (DA + SA+eth_type+(version&hdr_len)) */

	if (ntohs(ehdr->h_proto) == ETH_P_ARP) {
		/* ARP Payload length will be 42bytes or 42+18(tailer)=60bytes*/
		if (pkt_len != 42 && pkt_len != 60)
			RTW_INFO("Error !!%s,ARP Payload length %u not correct\n" , __func__ , pkt_len);
	} else if (ntohs(ehdr->h_proto) == ETH_P_IP) {
		if (be16_to_cpu(*((u16 *)(ehdr_pos + PAYLOAD_LEN_LOC_OF_IP_HDR))) != (pkt_len) - ETH_HLEN) {
			RTW_INFO("Error !!%s,Payload length not correct\n" , __func__);
			RTW_INFO("%s, IP header describe Total length=%u\n" , __func__ , be16_to_cpu(*((u16 *)(ehdr_pos + PAYLOAD_LEN_LOC_OF_IP_HDR))));
			RTW_INFO("%s, Pkt real length=%u\n" , __func__ , (pkt_len) - ETH_HLEN);
		}
	}
#endif

#ifdef CONFIG_AUTO_AP_MODE
	if (ntohs(ehdr->h_proto) == 0x8899)
		rtw_auto_ap_rx_msg_dump(adapter, rframe, ehdr_pos);
#endif

	ret = _SUCCESS;

exit:
	return ret;
}

int amsdu_to_msdu(_adapter *padapter, union recv_frame *prframe)
{
	struct rx_pkt_attrib *rattrib = &prframe->u.hdr.attrib;
	int	a_len, padding_len;
	u16	nSubframe_Length;
	u8	nr_subframes, i;
	u8	*pdata;
	_pkt *sub_pkt, *subframes[MAX_SUBFRAME_COUNT];
	struct recv_priv *precvpriv = &padapter->recvpriv;
	_queue *pfree_recv_queue = &(precvpriv->free_recv_queue);
	int	ret = _SUCCESS;

	nr_subframes = 0;

	recvframe_pull(prframe, rattrib->hdrlen);

	if (rattrib->iv_len > 0)
		recvframe_pull(prframe, rattrib->iv_len);

	a_len = prframe->u.hdr.len;
	pdata = prframe->u.hdr.rx_data;

	while (a_len > ETH_HLEN) {

		/* Offset 12 denote 2 mac address */
		nSubframe_Length = RTW_GET_BE16(pdata + 12);

		if (a_len < (ETHERNET_HEADER_SIZE + nSubframe_Length)) {
			RTW_INFO("nRemain_Length is %d and nSubframe_Length is : %d\n", a_len, nSubframe_Length);
			break;
		}

		sub_pkt = rtw_os_alloc_msdu_pkt(prframe, nSubframe_Length, pdata);
		if (sub_pkt == NULL) {
			RTW_INFO("%s(): allocate sub packet fail !!!\n", __FUNCTION__);
			break;
		}

		if (rtw_recv_indicatepkt_check(prframe, rtw_os_pkt_data(sub_pkt), rtw_os_pkt_len(sub_pkt)) == _SUCCESS)
			subframes[nr_subframes++] = sub_pkt;
		else
			rtw_os_pkt_free(sub_pkt);

		/* move the data point to data content */
		pdata += ETH_HLEN;
		a_len -= ETH_HLEN;

		if (nr_subframes >= MAX_SUBFRAME_COUNT) {
			RTW_WARN("ParseSubframe(): Too many Subframes! Packets dropped!\n");
			break;
		}

		pdata += nSubframe_Length;
		a_len -= nSubframe_Length;
		if (a_len != 0) {
			padding_len = 4 - ((nSubframe_Length + ETH_HLEN) & (4 - 1));
			if (padding_len == 4)
				padding_len = 0;

			if (a_len < padding_len) {
				RTW_INFO("ParseSubframe(): a_len < padding_len !\n");
				break;
			}
			pdata += padding_len;
			a_len -= padding_len;
		}
	}

	for (i = 0; i < nr_subframes; i++) {
		sub_pkt = subframes[i];

		/* Indicat the packets to upper layer */
		if (sub_pkt)
			rtw_os_recv_indicate_pkt(padapter, sub_pkt, prframe);
	}

	prframe->u.hdr.len = 0;
	rtw_free_recvframe(prframe, pfree_recv_queue);/* free this recv_frame */

	return ret;
}

static int recv_process_mpdu(_adapter *padapter, union recv_frame *prframe)
{
	struct recv_priv *precvpriv = &padapter->recvpriv;
	_queue *pfree_recv_queue = &padapter->recvpriv.free_recv_queue;
	struct rx_pkt_attrib *pattrib = &prframe->u.hdr.attrib;
	int ret;

	if (pattrib->amsdu) {
		ret = amsdu_to_msdu(padapter, prframe);
		if (ret != _SUCCESS) {
			#ifdef DBG_RX_DROP_FRAME
			RTW_INFO("DBG_RX_DROP_FRAME "FUNC_ADPT_FMT" amsdu_to_msdu fail\n"
				, FUNC_ADPT_ARG(padapter));
			#endif
			rtw_free_recvframe(prframe, pfree_recv_queue);
			goto exit;
		}
	} else {
		ret = wlanhdr_to_ethhdr(prframe);
		if (ret != _SUCCESS) {
			#ifdef DBG_RX_DROP_FRAME
			RTW_INFO("DBG_RX_DROP_FRAME "FUNC_ADPT_FMT" wlanhdr_to_ethhdr: drop pkt\n"
				, FUNC_ADPT_ARG(padapter));
			#endif
			rtw_free_recvframe(prframe, pfree_recv_queue);
			goto exit;
		}

		if (!RTW_CANNOT_RUN(padapter)) {
			ret = rtw_recv_indicatepkt_check(prframe
				, get_recvframe_data(prframe), get_recvframe_len(prframe));
			if (ret != _SUCCESS) {
				rtw_free_recvframe(prframe, pfree_recv_queue);
				goto exit;
			}

			/* indicate this recv_frame */
			ret = rtw_recv_indicatepkt(padapter, prframe);
			if (ret != _SUCCESS) {
				#ifdef DBG_RX_DROP_FRAME
				RTW_INFO("DBG_RX_DROP_FRAME "FUNC_ADPT_FMT" rtw_recv_indicatepkt fail!\n"
					, FUNC_ADPT_ARG(padapter));
				#endif
				goto exit;
			}
		} else {
			#ifdef DBG_RX_DROP_FRAME
			RTW_INFO("DBG_RX_DROP_FRAME "FUNC_ADPT_FMT" DS:%u SR:%u\n"
				, FUNC_ADPT_ARG(padapter)
				, rtw_is_drv_stopped(padapter)
				, rtw_is_surprise_removed(padapter));
			#endif
			ret = _SUCCESS; /* don't count as packet drop */
			rtw_free_recvframe(prframe, pfree_recv_queue);
		}
	}

exit:
	return ret;
}

#if defined(CONFIG_80211N_HT) && defined(CONFIG_RECV_REORDERING_CTRL)
static int check_indicate_seq(struct recv_reorder_ctrl *preorder_ctrl, u16 seq_num)
{
	PADAPTER padapter = preorder_ctrl->padapter;
	struct recv_priv  *precvpriv = &padapter->recvpriv;
	u8	wsize = preorder_ctrl->wsize_b;
	u16	wend = (preorder_ctrl->indicate_seq + wsize - 1) & 0xFFF; /* % 4096; */

	/* Rx Reorder initialize condition. */
	if (preorder_ctrl->indicate_seq == 0xFFFF) {
		preorder_ctrl->indicate_seq = seq_num;
		#ifdef DBG_RX_SEQ
		RTW_INFO("DBG_RX_SEQ "FUNC_ADPT_FMT" tid:%u SN_INIT indicate_seq:%d, seq_num:%d\n"
			, FUNC_ADPT_ARG(padapter), preorder_ctrl->tid, preorder_ctrl->indicate_seq, seq_num);
		#endif
	}

	/* Drop out the packet which SeqNum is smaller than WinStart */
	if (SN_LESS(seq_num, preorder_ctrl->indicate_seq)) {
		#ifdef DBG_RX_DROP_FRAME
		RTW_INFO(FUNC_ADPT_FMT" tid:%u indicate_seq:%d > seq_num:%d\n"
			, FUNC_ADPT_ARG(padapter), preorder_ctrl->tid, preorder_ctrl->indicate_seq, seq_num);
		#endif
		return _FALSE;
	}

	/*
	* Sliding window manipulation. Conditions includes:
	* 1. Incoming SeqNum is equal to WinStart =>Window shift 1
	* 2. Incoming SeqNum is larger than the WinEnd => Window shift N
	*/
	if (SN_EQUAL(seq_num, preorder_ctrl->indicate_seq)) {
		preorder_ctrl->indicate_seq = (preorder_ctrl->indicate_seq + 1) & 0xFFF;
		#ifdef DBG_RX_SEQ
		RTW_INFO("DBG_RX_SEQ "FUNC_ADPT_FMT" tid:%u SN_EQUAL indicate_seq:%d, seq_num:%d\n"
			, FUNC_ADPT_ARG(padapter), preorder_ctrl->tid, preorder_ctrl->indicate_seq, seq_num);
		#endif

	} else if (SN_LESS(wend, seq_num)) {
		/* boundary situation, when seq_num cross 0xFFF */
		if (seq_num >= (wsize - 1))
			preorder_ctrl->indicate_seq = seq_num + 1 - wsize;
		else
			preorder_ctrl->indicate_seq = 0xFFF - (wsize - (seq_num + 1)) + 1;

		precvpriv->dbg_rx_ampdu_window_shift_cnt++;
		#ifdef DBG_RX_SEQ
		RTW_INFO("DBG_RX_SEQ "FUNC_ADPT_FMT" tid:%u SN_LESS(wend, seq_num) indicate_seq:%d, seq_num:%d\n"
			, FUNC_ADPT_ARG(padapter), preorder_ctrl->tid, preorder_ctrl->indicate_seq, seq_num);
		#endif
	}

	return _TRUE;
}

static int enqueue_reorder_recvframe(struct recv_reorder_ctrl *preorder_ctrl, union recv_frame *prframe)
{
	struct rx_pkt_attrib *pattrib = &prframe->u.hdr.attrib;
	_queue *ppending_recvframe_queue = &preorder_ctrl->pending_recvframe_queue;
	_list	*phead, *plist;
	union recv_frame *pnextrframe;
	struct rx_pkt_attrib *pnextattrib;

	/* DbgPrint("+enqueue_reorder_recvframe()\n"); */

	/* _enter_critical_ex(&ppending_recvframe_queue->lock, &irql); */
	/* _rtw_spinlock_ex(&ppending_recvframe_queue->lock); */


	phead = get_list_head(ppending_recvframe_queue);
	plist = get_next(phead);

	while (rtw_end_of_queue_search(phead, plist) == _FALSE) {
		pnextrframe = LIST_CONTAINOR(plist, union recv_frame, u);
		pnextattrib = &pnextrframe->u.hdr.attrib;

		if (SN_LESS(pnextattrib->seq_num, pattrib->seq_num))
			plist = get_next(plist);
		else if (SN_EQUAL(pnextattrib->seq_num, pattrib->seq_num)) {
			/* Duplicate entry is found!! Do not insert current entry. */

			/* _exit_critical_ex(&ppending_recvframe_queue->lock, &irql); */

			return _FALSE;
		} else
			break;

		/* DbgPrint("enqueue_reorder_recvframe():while\n"); */

	}


	/* _enter_critical_ex(&ppending_recvframe_queue->lock, &irql); */
	/* _rtw_spinlock_ex(&ppending_recvframe_queue->lock); */

	rtw_list_delete(&(prframe->u.hdr.list));

	rtw_list_insert_tail(&(prframe->u.hdr.list), plist);

	/* _rtw_spinunlock_ex(&ppending_recvframe_queue->lock); */
	/* _exit_critical_ex(&ppending_recvframe_queue->lock, &irql); */


	return _TRUE;

}

static void recv_indicatepkts_pkt_loss_cnt(_adapter *padapter, u64 prev_seq, u64 current_seq)
{
	struct recv_priv *precvpriv = &padapter->recvpriv;

	if (current_seq < prev_seq) {
		precvpriv->dbg_rx_ampdu_loss_count += (4096 + current_seq - prev_seq);
		precvpriv->rx_drop += (4096 + current_seq - prev_seq);
	} else {
		precvpriv->dbg_rx_ampdu_loss_count += (current_seq - prev_seq);
		precvpriv->rx_drop += (current_seq - prev_seq);
	}
}

static int recv_indicatepkts_in_order(_adapter *padapter, struct recv_reorder_ctrl *preorder_ctrl, int bforced)
{
	/* _irqL irql; */
	_list	*phead, *plist;
	union recv_frame *prframe;
	struct rx_pkt_attrib *pattrib;
	/* u8 index = 0; */
	int bPktInBuf = _FALSE;
	struct recv_priv *precvpriv = &padapter->recvpriv;
	_queue *ppending_recvframe_queue = &preorder_ctrl->pending_recvframe_queue;

	DBG_COUNTER(padapter->rx_logs.core_rx_post_indicate_in_oder);

	/* DbgPrint("+recv_indicatepkts_in_order\n"); */

	/* _enter_critical_ex(&ppending_recvframe_queue->lock, &irql); */
	/* _rtw_spinlock_ex(&ppending_recvframe_queue->lock); */

	phead =	get_list_head(ppending_recvframe_queue);
	plist = get_next(phead);

#if 0
	/* Check if there is any other indication thread running. */
	if (pTS->RxIndicateState == RXTS_INDICATE_PROCESSING)
		return;
#endif

	/* Handling some condition for forced indicate case. */
	if (bforced == _TRUE) {
		precvpriv->dbg_rx_ampdu_forced_indicate_count++;
		if (rtw_is_list_empty(phead)) {
			/* _exit_critical_ex(&ppending_recvframe_queue->lock, &irql); */
			/* _rtw_spinunlock_ex(&ppending_recvframe_queue->lock); */
			return _TRUE;
		}

		prframe = LIST_CONTAINOR(plist, union recv_frame, u);
		pattrib = &prframe->u.hdr.attrib;

		#ifdef DBG_RX_SEQ
		RTW_INFO("DBG_RX_SEQ "FUNC_ADPT_FMT" tid:%u FORCE indicate_seq:%d, seq_num:%d\n"
			, FUNC_ADPT_ARG(padapter), preorder_ctrl->tid, preorder_ctrl->indicate_seq, pattrib->seq_num);
		#endif
		recv_indicatepkts_pkt_loss_cnt(padapter, preorder_ctrl->indicate_seq, pattrib->seq_num);
		preorder_ctrl->indicate_seq = pattrib->seq_num;
	}

	/* Prepare indication list and indication. */
	/* Check if there is any packet need indicate. */
	while (!rtw_is_list_empty(phead)) {

		prframe = LIST_CONTAINOR(plist, union recv_frame, u);
		pattrib = &prframe->u.hdr.attrib;

		if (!SN_LESS(preorder_ctrl->indicate_seq, pattrib->seq_num)) {

#if 0
			/* This protect buffer from overflow. */
			if (index >= REORDER_WIN_SIZE) {
				RT_ASSERT(FALSE, ("IndicateRxReorderList(): Buffer overflow!!\n"));
				bPktInBuf = TRUE;
				break;
			}
#endif

			plist = get_next(plist);
			rtw_list_delete(&(prframe->u.hdr.list));

			if (SN_EQUAL(preorder_ctrl->indicate_seq, pattrib->seq_num)) {
				preorder_ctrl->indicate_seq = (preorder_ctrl->indicate_seq + 1) & 0xFFF;
				#ifdef DBG_RX_SEQ
				RTW_INFO("DBG_RX_SEQ "FUNC_ADPT_FMT" tid:%u SN_EQUAL indicate_seq:%d, seq_num:%d\n"
					, FUNC_ADPT_ARG(padapter), preorder_ctrl->tid, preorder_ctrl->indicate_seq, pattrib->seq_num);
				#endif
			}

#if 0
			index++;
			if (index == 1) {
				/* Cancel previous pending timer. */
				/* PlatformCancelTimer(Adapter, &pTS->RxPktPendingTimer); */
				if (bforced != _TRUE) {
					/* RTW_INFO("_cancel_timer_ex(&preorder_ctrl->reordering_ctrl_timer);\n"); */
					_cancel_timer_ex(&preorder_ctrl->reordering_ctrl_timer);
				}
			}
#endif

			/* Set this as a lock to make sure that only one thread is indicating packet. */
			/* pTS->RxIndicateState = RXTS_INDICATE_PROCESSING; */

			/* Indicate packets */
			/* RT_ASSERT((index<=REORDER_WIN_SIZE), ("RxReorderIndicatePacket(): Rx Reorder buffer full!!\n")); */


			/* indicate this recv_frame */
			/* DbgPrint("recv_indicatepkts_in_order, indicate_seq=%d, seq_num=%d\n", precvpriv->indicate_seq, pattrib->seq_num); */
			if (recv_process_mpdu(padapter, prframe) != _SUCCESS)
				precvpriv->dbg_rx_drop_count++;

			/* Update local variables. */
			bPktInBuf = _FALSE;

		} else {
			bPktInBuf = _TRUE;
			break;
		}

		/* DbgPrint("recv_indicatepkts_in_order():while\n"); */

	}

	/* _rtw_spinunlock_ex(&ppending_recvframe_queue->lock); */
	/* _exit_critical_ex(&ppending_recvframe_queue->lock, &irql); */

#if 0
	/* Release the indication lock and set to new indication step. */
	if (bPktInBuf) {
		/*  Set new pending timer. */
		/* pTS->RxIndicateState = RXTS_INDICATE_REORDER; */
		/* PlatformSetTimer(Adapter, &pTS->RxPktPendingTimer, pHTInfo->RxReorderPendingTime); */

		_set_timer(&preorder_ctrl->reordering_ctrl_timer, REORDER_WAIT_TIME);
	} else {
		/* pTS->RxIndicateState = RXTS_INDICATE_IDLE; */
	}
#endif
	/* _exit_critical_ex(&ppending_recvframe_queue->lock, &irql); */

	/* return _TRUE; */
	return bPktInBuf;

}

static int recv_indicatepkt_reorder(_adapter *padapter, union recv_frame *prframe)
{
	_irqL irql;
	int retval = _SUCCESS;
	struct rx_pkt_attrib *pattrib = &prframe->u.hdr.attrib;
	struct recv_reorder_ctrl *preorder_ctrl = prframe->u.hdr.preorder_ctrl;
	_queue *ppending_recvframe_queue = preorder_ctrl ? &preorder_ctrl->pending_recvframe_queue : NULL;
	struct recv_priv  *precvpriv = &padapter->recvpriv;

	if (!pattrib->qos || !preorder_ctrl || preorder_ctrl->enable == _FALSE)
		goto _success_exit;

	DBG_COUNTER(padapter->rx_logs.core_rx_post_indicate_reoder);

	_enter_critical_bh(&ppending_recvframe_queue->lock, &irql);

	/* s2. check if winstart_b(indicate_seq) needs to been updated */
	if (!check_indicate_seq(preorder_ctrl, pattrib->seq_num)) {
		precvpriv->dbg_rx_ampdu_drop_count++;
		/* pHTInfo->RxReorderDropCounter++; */
		/* ReturnRFDList(Adapter, pRfd); */
		/* _exit_critical_ex(&ppending_recvframe_queue->lock, &irql); */
		/* return _FAIL; */

		#ifdef DBG_RX_DROP_FRAME
		RTW_INFO("DBG_RX_DROP_FRAME "FUNC_ADPT_FMT" check_indicate_seq fail\n"
			, FUNC_ADPT_ARG(padapter));
		#endif
#if 0
		rtw_recv_indicatepkt(padapter, prframe);

		_exit_critical_bh(&ppending_recvframe_queue->lock, &irql);

		goto _success_exit;
#else
		goto _err_exit;
#endif
	}


	/* s3. Insert all packet into Reorder Queue to maintain its ordering. */
	if (!enqueue_reorder_recvframe(preorder_ctrl, prframe)) {
		/* DbgPrint("recv_indicatepkt_reorder, enqueue_reorder_recvframe fail!\n"); */
		/* _exit_critical_ex(&ppending_recvframe_queue->lock, &irql); */
		/* return _FAIL; */
		#ifdef DBG_RX_DROP_FRAME
		RTW_INFO("DBG_RX_DROP_FRAME "FUNC_ADPT_FMT" enqueue_reorder_recvframe fail\n"
			, FUNC_ADPT_ARG(padapter));
		#endif
		goto _err_exit;
	}


	/* s4. */
	/* Indication process. */
	/* After Packet dropping and Sliding Window shifting as above, we can now just indicate the packets */
	/* with the SeqNum smaller than latest WinStart and buffer other packets. */
	/*  */
	/* For Rx Reorder condition: */
	/* 1. All packets with SeqNum smaller than WinStart => Indicate */
	/* 2. All packets with SeqNum larger than or equal to WinStart => Buffer it. */
	/*  */

	/* recv_indicatepkts_in_order(padapter, preorder_ctrl, _TRUE); */
	if (recv_indicatepkts_in_order(padapter, preorder_ctrl, _FALSE) == _TRUE) {
		if (!preorder_ctrl->bReorderWaiting) {
			preorder_ctrl->bReorderWaiting = _TRUE;
			_set_timer(&preorder_ctrl->reordering_ctrl_timer, REORDER_WAIT_TIME);
		}
		_exit_critical_bh(&ppending_recvframe_queue->lock, &irql);
	} else {
		preorder_ctrl->bReorderWaiting = _FALSE;
		_exit_critical_bh(&ppending_recvframe_queue->lock, &irql);
		_cancel_timer_ex(&preorder_ctrl->reordering_ctrl_timer);
	}

	return RTW_RX_HANDLED;

_success_exit:

	return _SUCCESS;

_err_exit:

	_exit_critical_bh(&ppending_recvframe_queue->lock, &irql);

	return _FAIL;
}


void rtw_reordering_ctrl_timeout_handler(void *pcontext)
{
	_irqL irql;
	struct recv_reorder_ctrl *preorder_ctrl = (struct recv_reorder_ctrl *)pcontext;
	_adapter *padapter = preorder_ctrl->padapter;
	_queue *ppending_recvframe_queue = &preorder_ctrl->pending_recvframe_queue;


	if (RTW_CANNOT_RUN(padapter))
		return;

	/* RTW_INFO("+rtw_reordering_ctrl_timeout_handler()=>\n"); */

	_enter_critical_bh(&ppending_recvframe_queue->lock, &irql);

	if (preorder_ctrl)
		preorder_ctrl->bReorderWaiting = _FALSE;

	if (recv_indicatepkts_in_order(padapter, preorder_ctrl, _TRUE) == _TRUE)
		_set_timer(&preorder_ctrl->reordering_ctrl_timer, REORDER_WAIT_TIME);

	_exit_critical_bh(&ppending_recvframe_queue->lock, &irql);

}
#endif /* defined(CONFIG_80211N_HT) && defined(CONFIG_RECV_REORDERING_CTRL) */

static void recv_set_iseq_before_mpdu_process(union recv_frame *rframe, u16 seq_num, const char *caller)
{
#if defined(CONFIG_80211N_HT) && defined(CONFIG_RECV_REORDERING_CTRL)
	struct recv_reorder_ctrl *reorder_ctrl = rframe->u.hdr.preorder_ctrl;

	if (reorder_ctrl) {
		reorder_ctrl->indicate_seq = seq_num;
		#ifdef DBG_RX_SEQ
		RTW_INFO("DBG_RX_SEQ %s("ADPT_FMT")-B tid:%u indicate_seq:%d, seq_num:%d\n"
			, caller, ADPT_ARG(reorder_ctrl->padapter)
			, reorder_ctrl->tid, reorder_ctrl->indicate_seq, seq_num);
		#endif
	}
#endif
}

static void recv_set_iseq_after_mpdu_process(union recv_frame *rframe, u16 seq_num, const char *caller)
{
#if defined(CONFIG_80211N_HT) && defined(CONFIG_RECV_REORDERING_CTRL)
	struct recv_reorder_ctrl *reorder_ctrl = rframe->u.hdr.preorder_ctrl;

	if (reorder_ctrl) {
		reorder_ctrl->indicate_seq = (reorder_ctrl->indicate_seq + 1) % 4096;
		#ifdef DBG_RX_SEQ
		RTW_INFO("DBG_RX_SEQ %s("ADPT_FMT")-A tid:%u indicate_seq:%d, seq_num:%d\n"
			, caller, ADPT_ARG(reorder_ctrl->padapter)
			, reorder_ctrl->tid, reorder_ctrl->indicate_seq, seq_num);
		#endif
	}
#endif
}

#ifdef CONFIG_MP_INCLUDED
int validate_mp_recv_frame(_adapter *adapter, union recv_frame *precv_frame)
{
	int ret = _SUCCESS;
	u8 *ptr = precv_frame->u.hdr.rx_data;
	u8 type, subtype;
	struct mp_priv *pmppriv = &adapter->mppriv;
	struct mp_tx		*pmptx;
	unsigned char	*sa , *da, *bs;

	pmptx = &pmppriv->tx;

#if 0
	if (1) {
		u8 bDumpRxPkt;
		type =  GetFrameType(ptr);
		subtype = get_frame_sub_type(ptr); /* bit(7)~bit(2)	 */

		rtw_hal_get_def_var(adapter, HAL_DEF_DBG_DUMP_RXPKT, &(bDumpRxPkt));
		if (bDumpRxPkt == 1) { /* dump all rx packets */
			int i;
			RTW_INFO("############ type:0x%02x subtype:0x%02x #################\n", type, subtype);

			for (i = 0; i < 64; i = i + 8)
				RTW_INFO("%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:\n", *(ptr + i),
					*(ptr + i + 1), *(ptr + i + 2) , *(ptr + i + 3) , *(ptr + i + 4), *(ptr + i + 5), *(ptr + i + 6), *(ptr + i + 7));
			RTW_INFO("#############################\n");
		}
	}
#endif
	if (pmppriv->bloopback) {
		if (_rtw_memcmp(ptr + 24, pmptx->buf + 24, precv_frame->u.hdr.len - 24) == _FALSE) {
			RTW_INFO("Compare payload content Fail !!!\n");
			ret = _FAIL;
		}
	}
 	if (pmppriv->bSetRxBssid == _TRUE) {

		sa = get_addr2_ptr(ptr);
		da = GetAddr1Ptr(ptr);
		bs = GetAddr3Ptr(ptr);
		type =	GetFrameType(ptr);
		subtype = get_frame_sub_type(ptr); /* bit(7)~bit(2)  */

		if (_rtw_memcmp(bs, adapter->mppriv.network_macaddr, ETH_ALEN) == _FALSE)
			ret = _FAIL;

		RTW_DBG("############ type:0x%02x subtype:0x%02x #################\n", type, subtype);
		RTW_DBG("A2 sa %02X:%02X:%02X:%02X:%02X:%02X \n", *(sa) , *(sa + 1), *(sa+ 2), *(sa + 3), *(sa + 4), *(sa + 5));
		RTW_DBG("A1 da %02X:%02X:%02X:%02X:%02X:%02X \n", *(da) , *(da + 1), *(da+ 2), *(da + 3), *(da + 4), *(da + 5));
		RTW_DBG("A3 bs %02X:%02X:%02X:%02X:%02X:%02X \n --------------------------\n", *(bs) , *(bs + 1), *(bs+ 2), *(bs + 3), *(bs + 4), *(bs + 5));
	}

	if (!adapter->mppriv.bmac_filter)
		return ret;

	if (_rtw_memcmp(get_addr2_ptr(ptr), adapter->mppriv.mac_filter, ETH_ALEN) == _FALSE)
		ret = _FAIL;

	return ret;
}

static sint MPwlanhdr_to_ethhdr(union recv_frame *precvframe)
{
	sint	rmv_len;
	u16 eth_type, len;
	u8	bsnaphdr;
	u8	*psnap_type;
	u8 mcastheadermac[] = {0x01, 0x00, 0x5e};

	struct ieee80211_snap_hdr	*psnap;

	sint ret = _SUCCESS;
	_adapter			*adapter = precvframe->u.hdr.adapter;
	struct mlme_priv	*pmlmepriv = &adapter->mlmepriv;

	u8	*ptr = get_recvframe_data(precvframe) ; /* point to frame_ctrl field */
	struct rx_pkt_attrib *pattrib = &precvframe->u.hdr.attrib;


	if (pattrib->encrypt)
		recvframe_pull_tail(precvframe, pattrib->icv_len);

	psnap = (struct ieee80211_snap_hdr *)(ptr + pattrib->hdrlen + pattrib->iv_len);
	psnap_type = ptr + pattrib->hdrlen + pattrib->iv_len + SNAP_SIZE;
	/* convert hdr + possible LLC headers into Ethernet header */
	/* eth_type = (psnap_type[0] << 8) | psnap_type[1]; */
	if ((_rtw_memcmp(psnap, rtw_rfc1042_header, SNAP_SIZE) &&
	     (_rtw_memcmp(psnap_type, SNAP_ETH_TYPE_IPX, 2) == _FALSE) &&
	     (_rtw_memcmp(psnap_type, SNAP_ETH_TYPE_APPLETALK_AARP, 2) == _FALSE)) ||
	    /* eth_type != ETH_P_AARP && eth_type != ETH_P_IPX) || */
	    _rtw_memcmp(psnap, rtw_bridge_tunnel_header, SNAP_SIZE)) {
		/* remove RFC1042 or Bridge-Tunnel encapsulation and replace EtherType */
		bsnaphdr = _TRUE;
	} else {
		/* Leave Ethernet header part of hdr and full payload */
		bsnaphdr = _FALSE;
	}

	rmv_len = pattrib->hdrlen + pattrib->iv_len + (bsnaphdr ? SNAP_SIZE : 0);
	len = precvframe->u.hdr.len - rmv_len;


	_rtw_memcpy(&eth_type, ptr + rmv_len, 2);
	eth_type = ntohs((unsigned short)eth_type); /* pattrib->ether_type */
	pattrib->eth_type = eth_type;

	{
		ptr = recvframe_pull(precvframe, (rmv_len - sizeof(struct ethhdr) + (bsnaphdr ? 2 : 0)));
	}

	_rtw_memcpy(ptr, pattrib->dst, ETH_ALEN);
	_rtw_memcpy(ptr + ETH_ALEN, pattrib->src, ETH_ALEN);

	if (!bsnaphdr) {
		len = htons(len);
		_rtw_memcpy(ptr + 12, &len, 2);
	}


	len = htons(pattrib->seq_num);
	/* RTW_INFO("wlan seq = %d ,seq_num =%x\n",len,pattrib->seq_num); */
	_rtw_memcpy(ptr + 12, &len, 2);
	if (adapter->mppriv.bRTWSmbCfg == _TRUE) {
		/* if(_rtw_memcmp(mcastheadermac, pattrib->dst, 3) == _TRUE) */ /* SimpleConfig Dest. */
		/*			_rtw_memcpy(ptr+ETH_ALEN, pattrib->bssid, ETH_ALEN); */

		if (_rtw_memcmp(mcastheadermac, pattrib->bssid, 3) == _TRUE) /* SimpleConfig Dest. */
			_rtw_memcpy(ptr, pattrib->bssid, ETH_ALEN);

	}


	return ret;

}


int mp_recv_frame(_adapter *padapter, union recv_frame *rframe)
{
	int ret = _SUCCESS;
	struct rx_pkt_attrib *pattrib = &rframe->u.hdr.attrib;
	struct recv_priv *precvpriv = &padapter->recvpriv;
	_queue *pfree_recv_queue = &padapter->recvpriv.free_recv_queue;
#ifdef CONFIG_MP_INCLUDED
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct mp_priv *pmppriv = &padapter->mppriv;
#endif /* CONFIG_MP_INCLUDED */
	u8 type;
	u8 *ptr = rframe->u.hdr.rx_data;
	u8 *psa, *pda, *pbssid;
	struct sta_info *psta = NULL;
	DBG_COUNTER(padapter->rx_logs.core_rx_pre);

	if ((check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE)) { /* &&(padapter->mppriv.check_mp_pkt == 0)) */
		if (pattrib->crc_err == 1)
			padapter->mppriv.rx_crcerrpktcount++;
		else {
			if (_SUCCESS == validate_mp_recv_frame(padapter, rframe))
				padapter->mppriv.rx_pktcount++;
			else
				padapter->mppriv.rx_pktcount_filter_out++;
		}

		if (pmppriv->rx_bindicatePkt == _FALSE) {
			ret = _FAIL;
			rtw_free_recvframe(rframe, pfree_recv_queue);/* free this recv_frame */
			goto exit;
		} else {
			type =	GetFrameType(ptr);
			pattrib->to_fr_ds = get_tofr_ds(ptr);
			pattrib->frag_num = GetFragNum(ptr);
			pattrib->seq_num = GetSequence(ptr);
			pattrib->pw_save = GetPwrMgt(ptr);
			pattrib->mfrag = GetMFrag(ptr);
			pattrib->mdata = GetMData(ptr);
			pattrib->privacy = GetPrivacy(ptr);
			pattrib->order = GetOrder(ptr);

			if (type == WIFI_DATA_TYPE) {
				pda = get_da(ptr);
				psa = get_sa(ptr);
				pbssid = get_hdr_bssid(ptr);

				_rtw_memcpy(pattrib->dst, pda, ETH_ALEN);
				_rtw_memcpy(pattrib->src, psa, ETH_ALEN);
				_rtw_memcpy(pattrib->bssid, pbssid, ETH_ALEN);

				switch (pattrib->to_fr_ds) {
				case 0:
					_rtw_memcpy(pattrib->ra, pda, ETH_ALEN);
					_rtw_memcpy(pattrib->ta, psa, ETH_ALEN);
					ret = sta2sta_data_frame(padapter, rframe, &psta);
					break;

				case 1:

					_rtw_memcpy(pattrib->ra, pda, ETH_ALEN);
					_rtw_memcpy(pattrib->ta, pbssid, ETH_ALEN);
					ret = ap2sta_data_frame(padapter, rframe, &psta);

					break;

				case 2:
					_rtw_memcpy(pattrib->ra, pbssid, ETH_ALEN);
					_rtw_memcpy(pattrib->ta, psa, ETH_ALEN);
					ret = sta2ap_data_frame(padapter, rframe, &psta);
					break;

				case 3:
					_rtw_memcpy(pattrib->ra, GetAddr1Ptr(ptr), ETH_ALEN);
					_rtw_memcpy(pattrib->ta, get_addr2_ptr(ptr), ETH_ALEN);
					ret = _FAIL;
					break;

				default:
					ret = _FAIL;
					break;
				}

				ret = MPwlanhdr_to_ethhdr(rframe);

				if (ret != _SUCCESS) {
					#ifdef DBG_RX_DROP_FRAME
					RTW_INFO("DBG_RX_DROP_FRAME "FUNC_ADPT_FMT" wlanhdr_to_ethhdr: drop pkt\n"
						, FUNC_ADPT_ARG(padapter));
					#endif
					rtw_free_recvframe(rframe, pfree_recv_queue);/* free this recv_frame */
					ret = _FAIL;
					goto exit;
				}
				if (!RTW_CANNOT_RUN(padapter)) {
					/* indicate this recv_frame */
					ret = rtw_recv_indicatepkt(padapter, rframe);
					if (ret != _SUCCESS) {
						#ifdef DBG_RX_DROP_FRAME
						RTW_INFO("DBG_RX_DROP_FRAME "FUNC_ADPT_FMT" rtw_recv_indicatepkt fail!\n"
							, FUNC_ADPT_ARG(padapter));
						#endif
						rtw_free_recvframe(rframe, pfree_recv_queue);/* free this recv_frame */
						ret = _FAIL;

						goto exit;
					}
				} else {
					#ifdef DBG_RX_DROP_FRAME
					RTW_INFO("DBG_RX_DROP_FRAME "FUNC_ADPT_FMT" bDriverStopped(%s) OR bSurpriseRemoved(%s)\n"
						, FUNC_ADPT_ARG(padapter)
						, rtw_is_drv_stopped(padapter) ? "True" : "False"
						, rtw_is_surprise_removed(padapter) ? "True" : "False");
					#endif
					ret = _FAIL;
					rtw_free_recvframe(rframe, pfree_recv_queue);/* free this recv_frame */
					goto exit;
				}

			}
		}

	}

	rtw_free_recvframe(rframe, pfree_recv_queue);/* free this recv_frame */
	ret = _FAIL;

exit:
	return ret;

}
#endif

static sint fill_radiotap_hdr(_adapter *padapter, union recv_frame *precvframe, u8 *buf)
{
#define CHAN2FREQ(a) ((a < 14) ? (2407+5*a) : (5000+5*a))

#if 0
#define RTW_RX_RADIOTAP_PRESENT (\
				 (1 << IEEE80211_RADIOTAP_TSFT)              | \
				 (1 << IEEE80211_RADIOTAP_FLAGS)             | \
				 (1 << IEEE80211_RADIOTAP_RATE)              | \
				 (1 << IEEE80211_RADIOTAP_CHANNEL)           | \
				 (0 << IEEE80211_RADIOTAP_FHSS)              | \
				 (1 << IEEE80211_RADIOTAP_DBM_ANTSIGNAL)     | \
				 (1 << IEEE80211_RADIOTAP_DBM_ANTNOISE)      | \
				 (0 << IEEE80211_RADIOTAP_LOCK_QUALITY)      | \
				 (0 << IEEE80211_RADIOTAP_TX_ATTENUATION)    | \
				 (0 << IEEE80211_RADIOTAP_DB_TX_ATTENUATION) | \
				 (0 << IEEE80211_RADIOTAP_DBM_TX_POWER)      | \
				 (1 << IEEE80211_RADIOTAP_ANTENNA)           | \
				 (1 << IEEE80211_RADIOTAP_DB_ANTSIGNAL)      | \
				 (0 << IEEE80211_RADIOTAP_DB_ANTNOISE)       | \
				 (0 << IEEE80211_RADIOTAP_RX_FLAGS)          | \
				 (0 << IEEE80211_RADIOTAP_TX_FLAGS)          | \
				 (0 << IEEE80211_RADIOTAP_RTS_RETRIES)       | \
				 (0 << IEEE80211_RADIOTAP_DATA_RETRIES)      | \
				 (0 << IEEE80211_RADIOTAP_MCS)               | \
				 (0 << IEEE80211_RADIOTAP_RADIOTAP_NAMESPACE)| \
				 (0 << IEEE80211_RADIOTAP_VENDOR_NAMESPACE)  | \
				 (0 << IEEE80211_RADIOTAP_EXT)               | \
				 0)

	/* (0 << IEEE80211_RADIOTAP_AMPDU_STATUS)      | \ */
	/* (0 << IEEE80211_RADIOTAP_VHT)               | \ */
#endif

#ifndef IEEE80211_RADIOTAP_RX_FLAGS
#define IEEE80211_RADIOTAP_RX_FLAGS 14
#endif

#ifndef IEEE80211_RADIOTAP_MCS
#define IEEE80211_RADIOTAP_MCS 19
#endif
#ifndef IEEE80211_RADIOTAP_VHT
#define IEEE80211_RADIOTAP_VHT 21
#endif

#ifndef IEEE80211_RADIOTAP_F_BADFCS
#define IEEE80211_RADIOTAP_F_BADFCS 0x40 /* bad FCS */
#endif

	sint ret = _SUCCESS;
	_adapter			*adapter = precvframe->u.hdr.adapter;
	struct mlme_priv	*pmlmepriv = &adapter->mlmepriv;
	struct rx_pkt_attrib *pattrib = &precvframe->u.hdr.attrib;

	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(padapter);

	u16 tmp_16bit = 0;

	u8 data_rate[] = {
		2, 4, 11, 22, /* CCK */
		12, 18, 24, 36, 48, 72, 93, 108, /* OFDM */
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, /* HT MCS index */
		16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9, /* VHT Nss 1 */
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9, /* VHT Nss 2 */
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9, /* VHT Nss 3 */
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9, /* VHT Nss 4 */
	};

	_pkt *pskb = NULL;

	struct ieee80211_radiotap_header *rtap_hdr = NULL;
	u8 *ptr = NULL;

	u8 hdr_buf[64] = {0};
	u16 rt_len = 8;

	/* create header */
	rtap_hdr = (struct ieee80211_radiotap_header *)&hdr_buf[0];
	rtap_hdr->it_version = PKTHDR_RADIOTAP_VERSION;

	/* tsft */
	if (pattrib->tsfl) {
		u64 tmp_64bit;

		rtap_hdr->it_present |= (1 << IEEE80211_RADIOTAP_TSFT);
		tmp_64bit = cpu_to_le64(pattrib->tsfl);
		memcpy(&hdr_buf[rt_len], &tmp_64bit, 8);
		rt_len += 8;
	}

	/* flags */
	rtap_hdr->it_present |= (1 << IEEE80211_RADIOTAP_FLAGS);
	if (0)
		hdr_buf[rt_len] |= IEEE80211_RADIOTAP_F_CFP;

	if (0)
		hdr_buf[rt_len] |= IEEE80211_RADIOTAP_F_SHORTPRE;

	if ((pattrib->encrypt == 1) || (pattrib->encrypt == 5))
		hdr_buf[rt_len] |= IEEE80211_RADIOTAP_F_WEP;

	if (pattrib->mfrag)
		hdr_buf[rt_len] |= IEEE80211_RADIOTAP_F_FRAG;

	/* always append FCS */
	hdr_buf[rt_len] |= IEEE80211_RADIOTAP_F_FCS;


	if (0)
		hdr_buf[rt_len] |= IEEE80211_RADIOTAP_F_DATAPAD;

	if (pattrib->crc_err)
		hdr_buf[rt_len] |= IEEE80211_RADIOTAP_F_BADFCS;

	if (pattrib->sgi) {
		/* Currently unspecified but used */
		hdr_buf[rt_len] |= 0x80;
	}
	rt_len += 1;

	/* rate */
	if (pattrib->data_rate < 12) {
		rtap_hdr->it_present |= (1 << IEEE80211_RADIOTAP_RATE);
		if (pattrib->data_rate < 4) {
			/* CCK */
			hdr_buf[rt_len] = data_rate[pattrib->data_rate];
		} else {
			/* OFDM */
			hdr_buf[rt_len] = data_rate[pattrib->data_rate];
		}
	}
	rt_len += 1; /* force padding 1 byte for aligned */

	/* channel */
	tmp_16bit = 0;
	rtap_hdr->it_present |= (1 << IEEE80211_RADIOTAP_CHANNEL);
	tmp_16bit = CHAN2FREQ(rtw_get_oper_ch(padapter));
	/*tmp_16bit = CHAN2FREQ(pHalData->current_channel);*/
	memcpy(&hdr_buf[rt_len], &tmp_16bit, 2);
	rt_len += 2;

	/* channel flags */
	tmp_16bit = 0;
	if (pHalData->current_band_type == 0)
		tmp_16bit |= cpu_to_le16(IEEE80211_CHAN_2GHZ);
	else
		tmp_16bit |= cpu_to_le16(IEEE80211_CHAN_5GHZ);

	if (pattrib->data_rate < 12) {
		if (pattrib->data_rate < 4) {
			/* CCK */
			tmp_16bit |= cpu_to_le16(IEEE80211_CHAN_CCK);
		} else {
			/* OFDM */
			tmp_16bit |= cpu_to_le16(IEEE80211_CHAN_OFDM);
		}
	} else
		tmp_16bit |= cpu_to_le16(IEEE80211_CHAN_DYN);
	memcpy(&hdr_buf[rt_len], &tmp_16bit, 2);
	rt_len += 2;

	/* dBm Antenna Signal */
	rtap_hdr->it_present |= (1 << IEEE80211_RADIOTAP_DBM_ANTSIGNAL);
	hdr_buf[rt_len] = pattrib->phy_info.recv_signal_power;
	rt_len += 1;

#if 0
	/* dBm Antenna Noise */
	rtap_hdr->it_present |= (1 << IEEE80211_RADIOTAP_DBM_ANTNOISE);
	hdr_buf[rt_len] = 0;
	rt_len += 1;

	/* Signal Quality */
	rtap_hdr->it_present |= (1 << IEEE80211_RADIOTAP_LOCK_QUALITY);
	hdr_buf[rt_len] = pattrib->phy_info.signal_quality;
	rt_len += 1;
#endif

	/* Antenna */
	rtap_hdr->it_present |= (1 << IEEE80211_RADIOTAP_ANTENNA);
	hdr_buf[rt_len] = 0; /* pHalData->rf_type; */
	rt_len += 1;

	/* RX flags */
	rtap_hdr->it_present |= (1 << IEEE80211_RADIOTAP_RX_FLAGS);
#if 0
	tmp_16bit = cpu_to_le16(0);
	memcpy(ptr, &tmp_16bit, 1);
#endif
	rt_len += 2;

	/* MCS information */
	if (pattrib->data_rate >= 12 && pattrib->data_rate < 44) {
		rtap_hdr->it_present |= (1 << IEEE80211_RADIOTAP_MCS);
		/* known, flag */
		hdr_buf[rt_len] |= BIT1; /* MCS index known */

		/* bandwidth */
		hdr_buf[rt_len] |= BIT0;
		hdr_buf[rt_len + 1] |= (pattrib->bw & 0x03);

		/* guard interval */
		hdr_buf[rt_len] |= BIT2;
		hdr_buf[rt_len + 1] |= (pattrib->sgi & 0x01) << 2;

		/* STBC */
		hdr_buf[rt_len] |= BIT5;
		hdr_buf[rt_len + 1] |= (pattrib->stbc & 0x03) << 5;

		rt_len += 2;

		/* MCS rate index */
		hdr_buf[rt_len] = data_rate[pattrib->data_rate];
		rt_len += 1;
	}

	/* VHT */
	if (pattrib->data_rate >= 44 && pattrib->data_rate < 84) {
		rtap_hdr->it_present |= (1 << IEEE80211_RADIOTAP_VHT);

		/* known 16 bit, flag 8 bit */
		tmp_16bit = 0;

		/* Bandwidth */
		tmp_16bit |= BIT6;

		/* Group ID */
		tmp_16bit |= BIT7;

		/* Partial AID */
		tmp_16bit |= BIT8;

		/* STBC */
		tmp_16bit |= BIT0;
		hdr_buf[rt_len + 2] |= (pattrib->stbc & 0x01);

		/* Guard interval */
		tmp_16bit |= BIT2;
		hdr_buf[rt_len + 2] |= (pattrib->sgi & 0x01) << 2;

		/* LDPC extra OFDM symbol */
		tmp_16bit |= BIT4;
		hdr_buf[rt_len + 2] |= (pattrib->ldpc & 0x01) << 4;

		memcpy(&hdr_buf[rt_len], &tmp_16bit, 2);
		rt_len += 3;

		/* bandwidth */
		if (pattrib->bw == 0)
			hdr_buf[rt_len] |= 0;
		else if (pattrib->bw == 1)
			hdr_buf[rt_len] |= 1;
		else if (pattrib->bw == 2)
			hdr_buf[rt_len] |= 4;
		else if (pattrib->bw == 3)
			hdr_buf[rt_len] |= 11;
		rt_len += 1;

		/* mcs_nss */
		if (pattrib->data_rate >= 44 && pattrib->data_rate < 54) {
			hdr_buf[rt_len] |= 1;
			hdr_buf[rt_len] |= data_rate[pattrib->data_rate] << 4;
		} else if (pattrib->data_rate >= 54 && pattrib->data_rate < 64) {
			hdr_buf[rt_len + 1] |= 2;
			hdr_buf[rt_len + 1] |= data_rate[pattrib->data_rate] << 4;
		} else if (pattrib->data_rate >= 64 && pattrib->data_rate < 74) {
			hdr_buf[rt_len + 2] |= 3;
			hdr_buf[rt_len + 2] |= data_rate[pattrib->data_rate] << 4;
		} else if (pattrib->data_rate >= 74 && pattrib->data_rate < 84) {
			hdr_buf[rt_len + 3] |= 4;
			hdr_buf[rt_len + 3] |= data_rate[pattrib->data_rate] << 4;
		}
		rt_len += 4;

		/* coding */
		hdr_buf[rt_len] = 0;
		rt_len += 1;

		/* group_id */
		hdr_buf[rt_len] = 0;
		rt_len += 1;

		/* partial_aid */
		tmp_16bit = 0;
		memcpy(&hdr_buf[rt_len], &tmp_16bit, 2);
		rt_len += 2;
	}

	/* push to skb */
	pskb = (_pkt *)buf;
	if (skb_headroom(pskb) < rt_len) {
		RTW_INFO("%s:%d %s headroom is too small.\n", __FILE__, __LINE__, __func__);
		ret = _FAIL;
		return ret;
	}

	ptr = skb_push(pskb, rt_len);
	if (ptr) {
		rtap_hdr->it_len = cpu_to_le16(rt_len);
		memcpy(ptr, rtap_hdr, rt_len);
	} else
		ret = _FAIL;

	return ret;

}
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 24))
int recv_frame_monitor(_adapter *padapter, union recv_frame *rframe)
{
	int ret = _SUCCESS;
	struct rx_pkt_attrib *pattrib = &rframe->u.hdr.attrib;
	struct recv_priv *precvpriv = &padapter->recvpriv;
	_queue *pfree_recv_queue = &padapter->recvpriv.free_recv_queue;
	_pkt *pskb = NULL;

	/* read skb information from recv frame */
	pskb = rframe->u.hdr.pkt;
	pskb->len = rframe->u.hdr.len;
	pskb->data = rframe->u.hdr.rx_data;
	skb_set_tail_pointer(pskb, rframe->u.hdr.len);

	/* fill radiotap header */
	if (fill_radiotap_hdr(padapter, rframe, (u8 *)pskb) == _FAIL) {
		ret = _FAIL;
		rtw_free_recvframe(rframe, pfree_recv_queue); /* free this recv_frame */
		goto exit;
	}

	/* write skb information to recv frame */
	skb_reset_mac_header(pskb);
	rframe->u.hdr.len = pskb->len;
	rframe->u.hdr.rx_data = pskb->data;
	rframe->u.hdr.rx_head = pskb->head;
	rframe->u.hdr.rx_tail = skb_tail_pointer(pskb);
	rframe->u.hdr.rx_end = skb_end_pointer(pskb);

	if (!RTW_CANNOT_RUN(padapter)) {
		/* indicate this recv_frame */
		ret = rtw_recv_monitor(padapter, rframe);
		if (ret != _SUCCESS) {
			ret = _FAIL;
			rtw_free_recvframe(rframe, pfree_recv_queue); /* free this recv_frame */
			goto exit;
		}
	} else {
		ret = _FAIL;
		rtw_free_recvframe(rframe, pfree_recv_queue); /* free this recv_frame */
		goto exit;
	}

exit:
	return ret;
}
#endif
int recv_func_prehandle(_adapter *padapter, union recv_frame *rframe)
{
	int ret = _SUCCESS;
	struct rx_pkt_attrib *pattrib = &rframe->u.hdr.attrib;
	struct recv_priv *precvpriv = &padapter->recvpriv;
	_queue *pfree_recv_queue = &padapter->recvpriv.free_recv_queue;

#ifdef DBG_RX_COUNTER_DUMP
	if (padapter->dump_rx_cnt_mode & DUMP_DRV_RX_COUNTER) {
		if (pattrib->crc_err == 1)
			padapter->drv_rx_cnt_crcerror++;
		else
			padapter->drv_rx_cnt_ok++;
	}
#endif

#ifdef CONFIG_MP_INCLUDED
	if (padapter->registrypriv.mp_mode == 1 || padapter->mppriv.bRTWSmbCfg == _TRUE) {
		mp_recv_frame(padapter, rframe);
		ret = _FAIL;
		goto exit;
	} else
#endif
	{
		/* check the frame crtl field and decache */
		ret = validate_recv_frame(padapter, rframe);
		if (ret != _SUCCESS) {
			rtw_free_recvframe(rframe, pfree_recv_queue);/* free this recv_frame */
			goto exit;
		}
	}
exit:
	return ret;
}

/*#define DBG_RX_BMC_FRAME*/
int recv_func_posthandle(_adapter *padapter, union recv_frame *prframe)
{
	int ret = _SUCCESS;
	union recv_frame *orig_prframe = prframe;
	struct rx_pkt_attrib *pattrib = &prframe->u.hdr.attrib;
	struct recv_priv *precvpriv = &padapter->recvpriv;
	_queue *pfree_recv_queue = &padapter->recvpriv.free_recv_queue;
#ifdef CONFIG_TDLS
	u8 *psnap_type, *pcategory;
#endif /* CONFIG_TDLS */

	DBG_COUNTER(padapter->rx_logs.core_rx_post);

	/* DATA FRAME */
	rtw_led_control(padapter, LED_CTL_RX);

	prframe = decryptor(padapter, prframe);
	if (prframe == NULL) {
		#ifdef DBG_RX_DROP_FRAME
		RTW_INFO("DBG_RX_DROP_FRAME "FUNC_ADPT_FMT" decryptor: drop pkt\n"
			, FUNC_ADPT_ARG(padapter));
		#endif
		ret = _FAIL;
		DBG_COUNTER(padapter->rx_logs.core_rx_post_decrypt_err);
		goto _recv_data_drop;
	}

#ifdef DBG_RX_BMC_FRAME
	if (IS_MCAST(pattrib->ra))
		RTW_INFO("%s =>"ADPT_FMT" Rx BC/MC from "MAC_FMT"\n", __func__, ADPT_ARG(padapter), MAC_ARG(pattrib->ta));
#endif

#if 0
	if (padapter->adapter_type == PRIMARY_ADAPTER) {
		RTW_INFO("+++\n");
		{
			int i;
			u8	*ptr = get_recvframe_data(prframe);
			for (i = 0; i < 140; i = i + 8)
				RTW_INFO("%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:", *(ptr + i),
					*(ptr + i + 1), *(ptr + i + 2) , *(ptr + i + 3) , *(ptr + i + 4), *(ptr + i + 5), *(ptr + i + 6), *(ptr + i + 7));

		}
		RTW_INFO("---\n");
	}
#endif

#ifdef CONFIG_TDLS
	/* check TDLS frame */
	psnap_type = get_recvframe_data(orig_prframe) + pattrib->hdrlen + pattrib->iv_len + SNAP_SIZE;
	pcategory = psnap_type + ETH_TYPE_LEN + PAYLOAD_TYPE_LEN;

	if ((_rtw_memcmp(psnap_type, SNAP_ETH_TYPE_TDLS, ETH_TYPE_LEN)) &&
	    ((*pcategory == RTW_WLAN_CATEGORY_TDLS) || (*pcategory == RTW_WLAN_CATEGORY_P2P))) {
		ret = OnTDLS(padapter, prframe);
		if (ret == _FAIL)
			goto _exit_recv_func;
	}
#endif /* CONFIG_TDLS */

	prframe = recvframe_chk_defrag(padapter, prframe);
	if (prframe == NULL)	{
		#ifdef DBG_RX_DROP_FRAME
		RTW_INFO("DBG_RX_DROP_FRAME "FUNC_ADPT_FMT" recvframe_chk_defrag: drop pkt\n"
			, FUNC_ADPT_ARG(padapter));
		#endif
		DBG_COUNTER(padapter->rx_logs.core_rx_post_defrag_err);
		goto _recv_data_drop;
	}

	prframe = portctrl(padapter, prframe);
	if (prframe == NULL) {
		#ifdef DBG_RX_DROP_FRAME
		RTW_INFO("DBG_RX_DROP_FRAME "FUNC_ADPT_FMT" portctrl: drop pkt\n"
			, FUNC_ADPT_ARG(padapter));
		#endif
		ret = _FAIL;
		DBG_COUNTER(padapter->rx_logs.core_rx_post_portctrl_err);
		goto _recv_data_drop;
	}

	count_rx_stats(padapter, prframe, NULL);

#ifdef CONFIG_WAPI_SUPPORT
	rtw_wapi_update_info(padapter, prframe);
#endif

#if defined(CONFIG_80211N_HT) && defined(CONFIG_RECV_REORDERING_CTRL)
	/* including perform A-MPDU Rx Ordering Buffer Control */
	ret = recv_indicatepkt_reorder(padapter, prframe);
	if (ret == _FAIL) {
		rtw_free_recvframe(orig_prframe, pfree_recv_queue);
		goto _recv_data_drop;
	} else if (ret == RTW_RX_HANDLED) /* queued OR indicated in order */
		goto _exit_recv_func;
#endif

	recv_set_iseq_before_mpdu_process(prframe, pattrib->seq_num, __func__);
	ret = recv_process_mpdu(padapter, prframe);
	recv_set_iseq_after_mpdu_process(prframe, pattrib->seq_num, __func__);
	if (ret == _FAIL)
		goto _recv_data_drop;

_exit_recv_func:
	return ret;

_recv_data_drop:
	precvpriv->dbg_rx_drop_count++;
	return ret;
}

int recv_func(_adapter *padapter, union recv_frame *rframe)
{
	int ret;
	struct rx_pkt_attrib *prxattrib = &rframe->u.hdr.attrib;
	struct recv_priv *recvpriv = &padapter->recvpriv;
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	struct mlme_priv *mlmepriv = &padapter->mlmepriv;

	if (check_fwstate(mlmepriv, WIFI_MONITOR_STATE)) {
		/* monitor mode */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 24))
		recv_frame_monitor(padapter, rframe);
#endif
		ret = _SUCCESS;
		goto exit;
	} else

		/* check if need to handle uc_swdec_pending_queue*/
		if (check_fwstate(mlmepriv, WIFI_STATION_STATE) && psecuritypriv->busetkipkey) {
			union recv_frame *pending_frame;
			int cnt = 0;

			while ((pending_frame = rtw_alloc_recvframe(&padapter->recvpriv.uc_swdec_pending_queue))) {
				cnt++;
				DBG_COUNTER(padapter->rx_logs.core_rx_dequeue);
				recv_func_posthandle(padapter, pending_frame);
			}

			if (cnt)
				RTW_INFO(FUNC_ADPT_FMT" dequeue %d from uc_swdec_pending_queue\n",
					 FUNC_ADPT_ARG(padapter), cnt);
		}

	DBG_COUNTER(padapter->rx_logs.core_rx);
	ret = recv_func_prehandle(padapter, rframe);

	if (ret == _SUCCESS) {

		/* check if need to enqueue into uc_swdec_pending_queue*/
		if (check_fwstate(mlmepriv, WIFI_STATION_STATE) &&
		    !IS_MCAST(prxattrib->ra) && prxattrib->encrypt > 0 &&
		    (prxattrib->bdecrypted == 0 || psecuritypriv->sw_decrypt == _TRUE) &&
		    psecuritypriv->ndisauthtype == Ndis802_11AuthModeWPAPSK &&
		    !psecuritypriv->busetkipkey) {
			DBG_COUNTER(padapter->rx_logs.core_rx_enqueue);
			rtw_enqueue_recvframe(rframe, &padapter->recvpriv.uc_swdec_pending_queue);
			/* RTW_INFO("%s: no key, enqueue uc_swdec_pending_queue\n", __func__); */

			if (recvpriv->free_recvframe_cnt < NR_RECVFRAME / 4) {
				/* to prevent from recvframe starvation, get recvframe from uc_swdec_pending_queue to free_recvframe_cnt */
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
	_adapter *padapter;
	struct recv_priv *precvpriv;
	s32 ret = _SUCCESS;



	padapter = precvframe->u.hdr.adapter;

	precvpriv = &padapter->recvpriv;


	ret = recv_func(padapter, precvframe);
	if (ret == _FAIL) {
		goto _recv_entry_drop;
	}


	precvpriv->rx_pkts++;


	return ret;

_recv_entry_drop:

#ifdef CONFIG_MP_INCLUDED
	if (padapter->registrypriv.mp_mode == 1)
		padapter->mppriv.rx_pktloss = precvpriv->rx_drop;
#endif



	return ret;
}

#ifdef CONFIG_NEW_SIGNAL_STAT_PROCESS
static void rtw_signal_stat_timer_hdl(void *ctx)
{
	_adapter *adapter = (_adapter *)ctx;
	struct recv_priv *recvpriv = &adapter->recvpriv;

	u32 tmp_s, tmp_q;
	u8 avg_signal_strength = 0;
	u8 avg_signal_qual = 0;
	u32 num_signal_strength = 0;
	u32 num_signal_qual = 0;
	u8 ratio_pre_stat = 0, ratio_curr_stat = 0, ratio_total = 0, ratio_profile = SIGNAL_STAT_CALC_PROFILE_0;

	if (adapter->recvpriv.is_signal_dbg) {
		/* update the user specific value, signal_strength_dbg, to signal_strength, rssi */
		adapter->recvpriv.signal_strength = adapter->recvpriv.signal_strength_dbg;
		adapter->recvpriv.rssi = (s8)translate_percentage_to_dbm((u8)adapter->recvpriv.signal_strength_dbg);
	} else {

		if (recvpriv->signal_strength_data.update_req == 0) { /* update_req is clear, means we got rx */
			avg_signal_strength = recvpriv->signal_strength_data.avg_val;
			num_signal_strength = recvpriv->signal_strength_data.total_num;
			/* after avg_vals are accquired, we can re-stat the signal values */
			recvpriv->signal_strength_data.update_req = 1;
		}

		if (recvpriv->signal_qual_data.update_req == 0) { /* update_req is clear, means we got rx */
			avg_signal_qual = recvpriv->signal_qual_data.avg_val;
			num_signal_qual = recvpriv->signal_qual_data.total_num;
			/* after avg_vals are accquired, we can re-stat the signal values */
			recvpriv->signal_qual_data.update_req = 1;
		}

		if (num_signal_strength == 0) {
			if (rtw_get_on_cur_ch_time(adapter) == 0
			    || rtw_get_passing_time_ms(rtw_get_on_cur_ch_time(adapter)) < 2 * adapter->mlmeextpriv.mlmext_info.bcn_interval
			   )
				goto set_timer;
		}

		if (check_fwstate(&adapter->mlmepriv, _FW_UNDER_SURVEY) == _TRUE
		    || check_fwstate(&adapter->mlmepriv, _FW_LINKED) == _FALSE
		   )
			goto set_timer;

#ifdef CONFIG_CONCURRENT_MODE
		if (rtw_mi_buddy_check_fwstate(adapter, _FW_UNDER_SURVEY) == _TRUE)
			goto set_timer;
#endif

		if (RTW_SIGNAL_STATE_CALC_PROFILE < SIGNAL_STAT_CALC_PROFILE_MAX)
			ratio_profile = RTW_SIGNAL_STATE_CALC_PROFILE;

		ratio_pre_stat = signal_stat_calc_profile[ratio_profile][0];
		ratio_curr_stat = signal_stat_calc_profile[ratio_profile][1];
		ratio_total = ratio_pre_stat + ratio_curr_stat;

		/* update value of signal_strength, rssi, signal_qual */
		tmp_s = (ratio_curr_stat * avg_signal_strength + ratio_pre_stat * recvpriv->signal_strength);
		if (tmp_s % ratio_total)
			tmp_s = tmp_s / ratio_total + 1;
		else
			tmp_s = tmp_s / ratio_total;
		if (tmp_s > 100)
			tmp_s = 100;

		tmp_q = (ratio_curr_stat * avg_signal_qual + ratio_pre_stat * recvpriv->signal_qual);
		if (tmp_q % ratio_total)
			tmp_q = tmp_q / ratio_total + 1;
		else
			tmp_q = tmp_q / ratio_total;
		if (tmp_q > 100)
			tmp_q = 100;

		recvpriv->signal_strength = tmp_s;
		recvpriv->rssi = (s8)translate_percentage_to_dbm(tmp_s);
		recvpriv->signal_qual = tmp_q;

#if defined(DBG_RX_SIGNAL_DISPLAY_PROCESSING) && 1
		RTW_INFO(FUNC_ADPT_FMT" signal_strength:%3u, rssi:%3d, signal_qual:%3u"
			 ", num_signal_strength:%u, num_signal_qual:%u"
			 ", on_cur_ch_ms:%d"
			 "\n"
			 , FUNC_ADPT_ARG(adapter)
			 , recvpriv->signal_strength
			 , recvpriv->rssi
			 , recvpriv->signal_qual
			 , num_signal_strength, num_signal_qual
			, rtw_get_on_cur_ch_time(adapter) ? rtw_get_passing_time_ms(rtw_get_on_cur_ch_time(adapter)) : 0
			);
#endif
	}

set_timer:
	rtw_set_signal_stat_timer(recvpriv);

}
#endif /* CONFIG_NEW_SIGNAL_STAT_PROCESS */

static void rx_process_rssi(_adapter *padapter, union recv_frame *prframe)
{
	u32	last_rssi, tmp_val;
	struct rx_pkt_attrib *pattrib = &prframe->u.hdr.attrib;
#ifdef CONFIG_NEW_SIGNAL_STAT_PROCESS
	struct signal_stat *signal_stat = &padapter->recvpriv.signal_strength_data;
#endif /* CONFIG_NEW_SIGNAL_STAT_PROCESS */

	/* RTW_INFO("process_rssi=> pattrib->rssil(%d) signal_strength(%d)\n ",pattrib->recv_signal_power,pattrib->signal_strength); */
	/* if(pRfd->Status.bPacketToSelf || pRfd->Status.bPacketBeacon) */
	{
#ifdef CONFIG_NEW_SIGNAL_STAT_PROCESS
		if (signal_stat->update_req) {
			signal_stat->total_num = 0;
			signal_stat->total_val = 0;
			signal_stat->update_req = 0;
		}

		signal_stat->total_num++;
		signal_stat->total_val  += pattrib->phy_info.signal_strength;
		signal_stat->avg_val = signal_stat->total_val / signal_stat->total_num;
#else /* CONFIG_NEW_SIGNAL_STAT_PROCESS */

		/* Adapter->RxStats.RssiCalculateCnt++;	 */ /* For antenna Test */
		if (padapter->recvpriv.signal_strength_data.total_num++ >= PHY_RSSI_SLID_WIN_MAX) {
			padapter->recvpriv.signal_strength_data.total_num = PHY_RSSI_SLID_WIN_MAX;
			last_rssi = padapter->recvpriv.signal_strength_data.elements[padapter->recvpriv.signal_strength_data.index];
			padapter->recvpriv.signal_strength_data.total_val -= last_rssi;
		}
		padapter->recvpriv.signal_strength_data.total_val  += pattrib->phy_info.signal_strength;

		padapter->recvpriv.signal_strength_data.elements[padapter->recvpriv.signal_strength_data.index++] = pattrib->phy_info.signal_strength;
		if (padapter->recvpriv.signal_strength_data.index >= PHY_RSSI_SLID_WIN_MAX)
			padapter->recvpriv.signal_strength_data.index = 0;


		tmp_val = padapter->recvpriv.signal_strength_data.total_val / padapter->recvpriv.signal_strength_data.total_num;

		if (padapter->recvpriv.is_signal_dbg) {
			padapter->recvpriv.signal_strength = padapter->recvpriv.signal_strength_dbg;
			padapter->recvpriv.rssi = (s8)translate_percentage_to_dbm(padapter->recvpriv.signal_strength_dbg);
		} else {
			padapter->recvpriv.signal_strength = tmp_val;
			padapter->recvpriv.rssi = (s8)translate_percentage_to_dbm(tmp_val);
		}

#endif /* CONFIG_NEW_SIGNAL_STAT_PROCESS */
	}
}

static void rx_process_link_qual(_adapter *padapter, union recv_frame *prframe)
{
	u32	last_evm = 0, tmpVal;
	struct rx_pkt_attrib *pattrib;
#ifdef CONFIG_NEW_SIGNAL_STAT_PROCESS
	struct signal_stat *signal_stat;
#endif /* CONFIG_NEW_SIGNAL_STAT_PROCESS */

	if (prframe == NULL || padapter == NULL)
		return;

	pattrib = &prframe->u.hdr.attrib;
#ifdef CONFIG_NEW_SIGNAL_STAT_PROCESS
	signal_stat = &padapter->recvpriv.signal_qual_data;
#endif /* CONFIG_NEW_SIGNAL_STAT_PROCESS */

	/* RTW_INFO("process_link_qual=> pattrib->signal_qual(%d)\n ",pattrib->signal_qual); */

#ifdef CONFIG_NEW_SIGNAL_STAT_PROCESS
	if (signal_stat->update_req) {
		signal_stat->total_num = 0;
		signal_stat->total_val = 0;
		signal_stat->update_req = 0;
	}

	signal_stat->total_num++;
	signal_stat->total_val  += pattrib->phy_info.signal_quality;
	signal_stat->avg_val = signal_stat->total_val / signal_stat->total_num;

#else /* CONFIG_NEW_SIGNAL_STAT_PROCESS */
	if (pattrib->phy_info.signal_quality != 0) {
		/*  */
		/* 1. Record the general EVM to the sliding window. */
		/*  */
		if (padapter->recvpriv.signal_qual_data.total_num++ >= PHY_LINKQUALITY_SLID_WIN_MAX) {
			padapter->recvpriv.signal_qual_data.total_num = PHY_LINKQUALITY_SLID_WIN_MAX;
			last_evm = padapter->recvpriv.signal_qual_data.elements[padapter->recvpriv.signal_qual_data.index];
			padapter->recvpriv.signal_qual_data.total_val -= last_evm;
		}
		padapter->recvpriv.signal_qual_data.total_val += pattrib->phy_info.signal_quality;

		padapter->recvpriv.signal_qual_data.elements[padapter->recvpriv.signal_qual_data.index++] = pattrib->phy_info.signal_quality;
		if (padapter->recvpriv.signal_qual_data.index >= PHY_LINKQUALITY_SLID_WIN_MAX)
			padapter->recvpriv.signal_qual_data.index = 0;


		/* <1> Showed on UI for user, in percentage. */
		tmpVal = padapter->recvpriv.signal_qual_data.total_val / padapter->recvpriv.signal_qual_data.total_num;
		padapter->recvpriv.signal_qual = (u8)tmpVal;

	}
#endif /* CONFIG_NEW_SIGNAL_STAT_PROCESS */
}

void rx_process_phy_info(_adapter *padapter, union recv_frame *rframe)
{
	/* Check RSSI */
	rx_process_rssi(padapter, rframe);

	/* Check PWDB */
	/* process_PWDB(padapter, rframe); */

	/* UpdateRxSignalStatistics8192C(Adapter, pRfd); */

	/* Check EVM */
	rx_process_link_qual(padapter, rframe);
	rtw_store_phy_info(padapter, rframe);
}

void rx_query_phy_status(
	union recv_frame	*precvframe,
	u8 *pphy_status)
{
	PADAPTER			padapter = precvframe->u.hdr.adapter;
	struct rx_pkt_attrib	*pattrib = &precvframe->u.hdr.attrib;
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(padapter);
	struct phydm_phyinfo_struct *p_phy_info = &pattrib->phy_info;
	u8					*wlanhdr;
	struct phydm_perpkt_info_struct pkt_info;
	u8 *ta, *ra;
	u8 is_ra_bmc;
	struct sta_priv *pstapriv;
	struct sta_info *psta = NULL;
	struct recv_priv  *precvpriv = &padapter->recvpriv;
	/* _irqL		irqL; */

	pkt_info.is_packet_match_bssid = _FALSE;
	pkt_info.is_packet_to_self = _FALSE;
	pkt_info.is_packet_beacon = _FALSE;
	pkt_info.ppdu_cnt = pattrib->ppdu_cnt;
	pkt_info.station_id = 0xFF;

	wlanhdr = get_recvframe_data(precvframe);

	ta = get_ta(wlanhdr);
	ra = get_ra(wlanhdr);
	is_ra_bmc = IS_MCAST(ra);

	if (_rtw_memcmp(adapter_mac_addr(padapter), ta, ETH_ALEN) == _TRUE) {
		static systime start_time = 0;

#if 0 /*For debug */
		if (IsFrameTypeCtrl(wlanhdr)) {
			RTW_INFO("-->Control frame: Y\n");
			RTW_INFO("-->pkt_len: %d\n", pattrib->pkt_len);
			RTW_INFO("-->Sub Type = 0x%X\n", get_frame_sub_type(wlanhdr));
		}

		/* Dump first 40 bytes of header */
		int i = 0;

		for (i = 0; i < 40; i++)
			RTW_INFO("%d: %X\n", i, *((u8 *)wlanhdr + i));

		RTW_INFO("\n");
#endif

		if ((start_time == 0) || (rtw_get_passing_time_ms(start_time) > 5000)) {
			RTW_PRINT("Warning!!! %s: Confilc mac addr!!\n", __func__);
			start_time = rtw_get_current_time();
		}
		precvpriv->dbg_rx_conflic_mac_addr_cnt++;
	} else {
		pstapriv = &padapter->stapriv;
		psta = rtw_get_stainfo(pstapriv, ta);
		if (psta)
			pkt_info.station_id = psta->cmn.mac_id;
	}

	pkt_info.is_packet_match_bssid = (!IsFrameTypeCtrl(wlanhdr))
		&& (!pattrib->icv_err) && (!pattrib->crc_err)
		&& _rtw_memcmp(get_hdr_bssid(wlanhdr), get_bssid(&padapter->mlmepriv), ETH_ALEN);

	pkt_info.is_to_self = (!pattrib->icv_err) && (!pattrib->crc_err)
		&& _rtw_memcmp(ra, adapter_mac_addr(padapter), ETH_ALEN);

	pkt_info.is_packet_to_self = pkt_info.is_packet_match_bssid
		&& _rtw_memcmp(ra, adapter_mac_addr(padapter), ETH_ALEN);

	pkt_info.is_packet_beacon = pkt_info.is_packet_match_bssid
				 && (get_frame_sub_type(wlanhdr) == WIFI_BEACON);

	if (psta && IsFrameTypeData(wlanhdr)) {
		if (is_ra_bmc)
			psta->curr_rx_rate_bmc = pattrib->data_rate;
		else
			psta->curr_rx_rate = pattrib->data_rate;
	}
	pkt_info.data_rate = pattrib->data_rate;

	odm_phy_status_query(&pHalData->odmpriv, p_phy_info, pphy_status, &pkt_info);

	{
		precvframe->u.hdr.psta = NULL;
		if ((!MLME_IS_MESH(padapter) && pkt_info.is_packet_match_bssid)
			|| padapter->registrypriv.mp_mode == 1
		) {
			if (psta) {
				precvframe->u.hdr.psta = psta;
				rx_process_phy_info(padapter, precvframe);
			}
		} else if (pkt_info.is_packet_to_self || pkt_info.is_packet_beacon) {

			if (psta)
				precvframe->u.hdr.psta = psta;
			rx_process_phy_info(padapter, precvframe);
		}
	}

	rtw_odm_parse_rx_phy_status_chinfo(precvframe, pphy_status);
}
/*
* Increase and check if the continual_no_rx_packet of this @param pmlmepriv is larger than MAX_CONTINUAL_NORXPACKET_COUNT
* @return _TRUE:
* @return _FALSE:
*/
int rtw_inc_and_chk_continual_no_rx_packet(struct sta_info *sta, int tid_index)
{

	int ret = _FALSE;
	int value = ATOMIC_INC_RETURN(&sta->continual_no_rx_packet[tid_index]);

	if (value >= MAX_CONTINUAL_NORXPACKET_COUNT)
		ret = _TRUE;

	return ret;
}

/*
* Set the continual_no_rx_packet of this @param pmlmepriv to 0
*/
void rtw_reset_continual_no_rx_packet(struct sta_info *sta, int tid_index)
{
	ATOMIC_SET(&sta->continual_no_rx_packet[tid_index], 0);
}

u8 adapter_allow_bmc_data_rx(_adapter *adapter)
{
	if (check_fwstate(&adapter->mlmepriv, WIFI_MONITOR_STATE | WIFI_MP_STATE) == _TRUE)
		return 1;

	if (MLME_IS_AP(adapter))
		return 0;

	if (rtw_linked_check(adapter) == _FALSE)
		return 0;

	return 1;
}

s32 pre_recv_entry(union recv_frame *precvframe, u8 *pphy_status)
{
	s32 ret = _SUCCESS;
	u8 *pbuf = precvframe->u.hdr.rx_data;
	u8 *pda = get_ra(pbuf);
	u8 ra_is_bmc = IS_MCAST(pda);
#ifdef CONFIG_CONCURRENT_MODE
	_adapter *iface = NULL;
	_adapter *primary_padapter = precvframe->u.hdr.adapter;

	#ifdef CONFIG_MP_INCLUDED
	if (rtw_mp_mode_check(primary_padapter))
		goto bypass_concurrent_hdl;
	#endif

	if (ra_is_bmc == _FALSE) { /*unicast packets*/
		iface = rtw_get_iface_by_macddr(primary_padapter , pda);
		if (NULL == iface) {
			RTW_INFO("%s [WARN] Cannot find appropriate adapter - mac_addr : "MAC_FMT"\n", __func__, MAC_ARG(pda));
			/*rtw_warn_on(1);*/
		} else
			precvframe->u.hdr.adapter = iface;
	} else   /* Handle BC/MC Packets	*/
		rtw_mi_buddy_clone_bcmc_packet(primary_padapter, precvframe, pphy_status);
bypass_concurrent_hdl:
#endif /* CONFIG_CONCURRENT_MODE */

	/* skip unnecessary bmc data frame for primary adapter */
	if (ra_is_bmc == _TRUE && GetFrameType(pbuf) == WIFI_DATA_TYPE
		&& !adapter_allow_bmc_data_rx(precvframe->u.hdr.adapter)
	) {
		rtw_free_recvframe(precvframe, &precvframe->u.hdr.adapter->recvpriv.free_recv_queue);
		goto exit;
	}

	if (pphy_status)
		rx_query_phy_status(precvframe, pphy_status);
	ret = rtw_recv_entry(precvframe);

exit:
	return ret;
}

#ifdef CONFIG_RECV_THREAD_MODE
thread_return rtw_recv_thread(thread_context context)
{
	_adapter *adapter = (_adapter *)context;
	struct recv_priv *recvpriv = &adapter->recvpriv;
	s32 err = _SUCCESS;
#ifdef PLATFORM_LINUX
	struct sched_param param = { .sched_priority = 1 };

	sched_setscheduler(current, SCHED_FIFO, &param);
#endif /* PLATFORM_LINUX */
	thread_enter("RTW_RECV_THREAD");

	RTW_INFO(FUNC_ADPT_FMT" enter\n", FUNC_ADPT_ARG(adapter));

	do {
		err = _rtw_down_sema(&recvpriv->recv_sema);
		if (_FAIL == err) {
			RTW_ERR(FUNC_ADPT_FMT" down recv_sema fail!\n", FUNC_ADPT_ARG(adapter));
			goto exit;
		}

		if (RTW_CANNOT_RUN(adapter)) {
			RTW_DBG(FUNC_ADPT_FMT "- bDriverStopped(%s) bSurpriseRemoved(%s)\n",
				FUNC_ADPT_ARG(adapter),
				rtw_is_drv_stopped(adapter) ? "True" : "False",
				rtw_is_surprise_removed(adapter) ? "True" : "False");
			goto exit;
		}

		err = rtw_hal_recv_hdl(adapter);

		if (err == RTW_RFRAME_UNAVAIL
			|| err == RTW_RFRAME_PKT_UNAVAIL
		) {
			rtw_msleep_os(1);
			_rtw_up_sema(&recvpriv->recv_sema);
		}

		flush_signals_thread();

	} while (err != _FAIL);

exit:

	RTW_INFO(FUNC_ADPT_FMT " Exit\n", FUNC_ADPT_ARG(adapter));

	rtw_thread_wait_stop();

	return 0;
}
#endif /* CONFIG_RECV_THREAD_MODE */

#if DBG_RX_BH_TRACKING
void rx_bh_tk_set_stage(struct recv_priv *recv, u32 s)
{
	recv->rx_bh_stage = s;
}

void rx_bh_tk_set_buf(struct recv_priv *recv, void *buf, void *data, u32 dlen)
{
	if (recv->rx_bh_cbuf)
		recv->rx_bh_lbuf = recv->rx_bh_cbuf;
	recv->rx_bh_cbuf = buf;
	if (buf) {
		recv->rx_bh_cbuf_data = data;
		recv->rx_bh_cbuf_dlen = dlen;
		recv->rx_bh_buf_dq_cnt++;
	} else {
		recv->rx_bh_cbuf_data = NULL;
		recv->rx_bh_cbuf_dlen = 0;
	}
}

void rx_bh_tk_set_buf_pos(struct recv_priv *recv, void *pos)
{
	if (recv->rx_bh_cbuf) {
		recv->rx_bh_cbuf_pos = pos - recv->rx_bh_cbuf_data;
	} else {
		rtw_warn_on(1);
		recv->rx_bh_cbuf_pos = 0;
	}
}

void rx_bh_tk_set_frame(struct recv_priv *recv, void *frame)
{
	recv->rx_bh_cframe = frame;
}

void dump_rx_bh_tk(void *sel, struct recv_priv *recv)
{
	RTW_PRINT_SEL(sel, "[RXBHTK]s:%u, buf_dqc:%u, lbuf:%p, cbuf:%p, dlen:%u, pos:%u, cframe:%p\n"
		, recv->rx_bh_stage
		, recv->rx_bh_buf_dq_cnt
		, recv->rx_bh_lbuf
		, recv->rx_bh_cbuf
		, recv->rx_bh_cbuf_dlen
		, recv->rx_bh_cbuf_pos
		, recv->rx_bh_cframe
	);
}
#endif /* DBG_RX_BH_TRACKING */

