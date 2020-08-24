/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2019 Realtek Corporation.
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
#define _RTW_XMIT_C_

#include <drv_types.h>
#include <hal_data.h>

static u8 P802_1H_OUI[P80211_OUI_LEN] = { 0x00, 0x00, 0xf8 };
static u8 RFC1042_OUI[P80211_OUI_LEN] = { 0x00, 0x00, 0x00 };

static void _init_txservq(struct tx_servq *ptxservq)
{
	_rtw_init_listhead(&ptxservq->tx_pending);
	_rtw_init_queue(&ptxservq->sta_pending);
	ptxservq->qcnt = 0;
}


void	_rtw_init_sta_xmit_priv(struct sta_xmit_priv *psta_xmitpriv)
{


	_rtw_memset((unsigned char *)psta_xmitpriv, 0, sizeof(struct sta_xmit_priv));

	_rtw_spinlock_init(&psta_xmitpriv->lock);

	/* for(i = 0 ; i < MAX_NUMBLKS; i++) */
	/*	_init_txservq(&(psta_xmitpriv->blk_q[i])); */

	_init_txservq(&psta_xmitpriv->be_q);
	_init_txservq(&psta_xmitpriv->bk_q);
	_init_txservq(&psta_xmitpriv->vi_q);
	_init_txservq(&psta_xmitpriv->vo_q);
	_rtw_init_listhead(&psta_xmitpriv->legacy_dz);
	_rtw_init_listhead(&psta_xmitpriv->apsd);


}

void rtw_init_xmit_block(_adapter *padapter)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);

	_rtw_spinlock_init(&dvobj->xmit_block_lock);
	dvobj->xmit_block = XMIT_BLOCK_NONE;

}
void rtw_free_xmit_block(_adapter *padapter)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);

	_rtw_spinlock_free(&dvobj->xmit_block_lock);
}

s32	_rtw_init_xmit_priv(struct xmit_priv *pxmitpriv, _adapter *padapter)
{
	int i;
	struct xmit_buf *pxmitbuf;
	struct xmit_frame *pxframe;
	sint	res = _SUCCESS;


	/* We don't need to memset padapter->XXX to zero, because adapter is allocated by rtw_zvmalloc(). */
	/* _rtw_memset((unsigned char *)pxmitpriv, 0, sizeof(struct xmit_priv)); */

	_rtw_spinlock_init(&pxmitpriv->lock);
	_rtw_spinlock_init(&pxmitpriv->lock_sctx);
	_rtw_init_sema(&pxmitpriv->xmit_sema, 0);

	/*
	Please insert all the queue initializaiton using _rtw_init_queue below
	*/

	pxmitpriv->adapter = padapter;

	/* for(i = 0 ; i < MAX_NUMBLKS; i++) */
	/*	_rtw_init_queue(&pxmitpriv->blk_strms[i]); */

	_rtw_init_queue(&pxmitpriv->be_pending);
	_rtw_init_queue(&pxmitpriv->bk_pending);
	_rtw_init_queue(&pxmitpriv->vi_pending);
	_rtw_init_queue(&pxmitpriv->vo_pending);
	_rtw_init_queue(&pxmitpriv->bm_pending);

	/* _rtw_init_queue(&pxmitpriv->legacy_dz_queue); */
	/* _rtw_init_queue(&pxmitpriv->apsd_queue); */

	_rtw_init_queue(&pxmitpriv->free_xmit_queue);

	/*
	Please allocate memory with the sz = (struct xmit_frame) * NR_XMITFRAME,
	and initialize free_xmit_frame below.
	Please also apply  free_txobj to link_up all the xmit_frames...
	*/

	pxmitpriv->pallocated_frame_buf = rtw_zvmalloc(NR_XMITFRAME * sizeof(struct xmit_frame) + 4);

	if (pxmitpriv->pallocated_frame_buf  == NULL) {
		pxmitpriv->pxmit_frame_buf = NULL;
		res = _FAIL;
		goto exit;
	}
	pxmitpriv->pxmit_frame_buf = (u8 *)N_BYTE_ALIGMENT((SIZE_PTR)(pxmitpriv->pallocated_frame_buf), 4);
	/* pxmitpriv->pxmit_frame_buf = pxmitpriv->pallocated_frame_buf + 4 - */
	/*						((SIZE_PTR) (pxmitpriv->pallocated_frame_buf) &3); */

	pxframe = (struct xmit_frame *) pxmitpriv->pxmit_frame_buf;

	for (i = 0; i < NR_XMITFRAME; i++) {
		_rtw_init_listhead(&(pxframe->list));

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

	pxmitpriv->pallocated_xmitbuf = rtw_zvmalloc(NR_XMITBUFF * sizeof(struct xmit_buf) + 4);

	if (pxmitpriv->pallocated_xmitbuf  == NULL) {
		res = _FAIL;
		goto exit;
	}

	pxmitpriv->pxmitbuf = (u8 *)N_BYTE_ALIGMENT((SIZE_PTR)(pxmitpriv->pallocated_xmitbuf), 4);
	/* pxmitpriv->pxmitbuf = pxmitpriv->pallocated_xmitbuf + 4 - */
	/*						((SIZE_PTR) (pxmitpriv->pallocated_xmitbuf) &3); */

	pxmitbuf = (struct xmit_buf *)pxmitpriv->pxmitbuf;

	for (i = 0; i < NR_XMITBUFF; i++) {
		_rtw_init_listhead(&pxmitbuf->list);

		pxmitbuf->priv_data = NULL;
		pxmitbuf->padapter = padapter;
		pxmitbuf->buf_tag = XMITBUF_DATA;

		/* Tx buf allocation may fail sometimes, so sleep and retry. */
		res = rtw_os_xmit_resource_alloc(padapter, pxmitbuf, (MAX_XMITBUF_SZ + XMITBUF_ALIGN_SZ), _TRUE);
		if (res == _FAIL) {
			rtw_msleep_os(10);
			res = rtw_os_xmit_resource_alloc(padapter, pxmitbuf, (MAX_XMITBUF_SZ + XMITBUF_ALIGN_SZ), _TRUE);
			if (res == _FAIL)
				goto exit;
		}

#if defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
		pxmitbuf->phead = pxmitbuf->pbuf;
		pxmitbuf->pend = pxmitbuf->pbuf + MAX_XMITBUF_SZ;
		pxmitbuf->len = 0;
		pxmitbuf->pdata = pxmitbuf->ptail = pxmitbuf->phead;
#endif

		pxmitbuf->flags = XMIT_VO_QUEUE;

		rtw_list_insert_tail(&pxmitbuf->list, &(pxmitpriv->free_xmitbuf_queue.queue));
#ifdef DBG_XMIT_BUF
		pxmitbuf->no = i;
#endif

		pxmitbuf++;

	}

	pxmitpriv->free_xmitbuf_cnt = NR_XMITBUFF;

	/* init xframe_ext queue,  the same count as extbuf */
	_rtw_init_queue(&pxmitpriv->free_xframe_ext_queue);

	pxmitpriv->xframe_ext_alloc_addr = rtw_zvmalloc(NR_XMIT_EXTBUFF * sizeof(struct xmit_frame) + 4);

	if (pxmitpriv->xframe_ext_alloc_addr  == NULL) {
		pxmitpriv->xframe_ext = NULL;
		res = _FAIL;
		goto exit;
	}
	pxmitpriv->xframe_ext = (u8 *)N_BYTE_ALIGMENT((SIZE_PTR)(pxmitpriv->xframe_ext_alloc_addr), 4);
	pxframe = (struct xmit_frame *)pxmitpriv->xframe_ext;

	for (i = 0; i < NR_XMIT_EXTBUFF; i++) {
		_rtw_init_listhead(&(pxframe->list));

		pxframe->padapter = padapter;
		pxframe->frame_tag = NULL_FRAMETAG;

		pxframe->pkt = NULL;

		pxframe->buf_addr = NULL;
		pxframe->pxmitbuf = NULL;

		pxframe->ext_tag = 1;

		rtw_list_insert_tail(&(pxframe->list), &(pxmitpriv->free_xframe_ext_queue.queue));

		pxframe++;
	}
	pxmitpriv->free_xframe_ext_cnt = NR_XMIT_EXTBUFF;

	/* Init xmit extension buff */
	_rtw_init_queue(&pxmitpriv->free_xmit_extbuf_queue);

	pxmitpriv->pallocated_xmit_extbuf = rtw_zvmalloc(NR_XMIT_EXTBUFF * sizeof(struct xmit_buf) + 4);

	if (pxmitpriv->pallocated_xmit_extbuf  == NULL) {
		res = _FAIL;
		goto exit;
	}

	pxmitpriv->pxmit_extbuf = (u8 *)N_BYTE_ALIGMENT((SIZE_PTR)(pxmitpriv->pallocated_xmit_extbuf), 4);

	pxmitbuf = (struct xmit_buf *)pxmitpriv->pxmit_extbuf;

	for (i = 0; i < NR_XMIT_EXTBUFF; i++) {
		_rtw_init_listhead(&pxmitbuf->list);

		pxmitbuf->priv_data = NULL;
		pxmitbuf->padapter = padapter;
		pxmitbuf->buf_tag = XMITBUF_MGNT;

		res = rtw_os_xmit_resource_alloc(padapter, pxmitbuf, MAX_XMIT_EXTBUF_SZ + XMITBUF_ALIGN_SZ, _TRUE);
		if (res == _FAIL) {
			res = _FAIL;
			goto exit;
		}

#if defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
		pxmitbuf->phead = pxmitbuf->pbuf;
		pxmitbuf->pend = pxmitbuf->pbuf + MAX_XMIT_EXTBUF_SZ;
		pxmitbuf->len = 0;
		pxmitbuf->pdata = pxmitbuf->ptail = pxmitbuf->phead;
#endif

		rtw_list_insert_tail(&pxmitbuf->list, &(pxmitpriv->free_xmit_extbuf_queue.queue));
#ifdef DBG_XMIT_BUF_EXT
		pxmitbuf->no = i;
#endif
		pxmitbuf++;

	}

	pxmitpriv->free_xmit_extbuf_cnt = NR_XMIT_EXTBUFF;

	for (i = 0; i < CMDBUF_MAX; i++) {
		pxmitbuf = &pxmitpriv->pcmd_xmitbuf[i];
		if (pxmitbuf) {
			_rtw_init_listhead(&pxmitbuf->list);

			pxmitbuf->priv_data = NULL;
			pxmitbuf->padapter = padapter;
			pxmitbuf->buf_tag = XMITBUF_CMD;

			res = rtw_os_xmit_resource_alloc(padapter, pxmitbuf, MAX_CMDBUF_SZ + XMITBUF_ALIGN_SZ, _TRUE);
			if (res == _FAIL) {
				res = _FAIL;
				goto exit;
			}

#if defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
			pxmitbuf->phead = pxmitbuf->pbuf;
			pxmitbuf->pend = pxmitbuf->pbuf + MAX_CMDBUF_SZ;
			pxmitbuf->len = 0;
			pxmitbuf->pdata = pxmitbuf->ptail = pxmitbuf->phead;
#endif
			pxmitbuf->alloc_sz = MAX_CMDBUF_SZ + XMITBUF_ALIGN_SZ;
		}
	}

	rtw_alloc_hwxmits(padapter);
	rtw_init_hwxmits(pxmitpriv->hwxmits, pxmitpriv->hwxmit_entry);

	for (i = 0; i < 4; i++)
		pxmitpriv->wmm_para_seq[i] = i;

#ifdef CONFIG_USB_HCI
	pxmitpriv->txirp_cnt = 1;

	_rtw_init_sema(&(pxmitpriv->tx_retevt), 0);

	/* per AC pending irp */
	pxmitpriv->beq_cnt = 0;
	pxmitpriv->bkq_cnt = 0;
	pxmitpriv->viq_cnt = 0;
	pxmitpriv->voq_cnt = 0;
#endif


#ifdef CONFIG_XMIT_ACK
	pxmitpriv->ack_tx = _FALSE;
	_rtw_mutex_init(&pxmitpriv->ack_tx_mutex);
	rtw_sctx_init(&pxmitpriv->ack_tx_ops, 0);
#endif

#ifdef CONFIG_TX_AMSDU
	rtw_init_timer(&(pxmitpriv->amsdu_vo_timer), padapter,
		rtw_amsdu_vo_timeout_handler, padapter);
	pxmitpriv->amsdu_vo_timeout = RTW_AMSDU_TIMER_UNSET;

	rtw_init_timer(&(pxmitpriv->amsdu_vi_timer), padapter,
		rtw_amsdu_vi_timeout_handler, padapter);
	pxmitpriv->amsdu_vi_timeout = RTW_AMSDU_TIMER_UNSET;

	rtw_init_timer(&(pxmitpriv->amsdu_be_timer), padapter,
		rtw_amsdu_be_timeout_handler, padapter);
	pxmitpriv->amsdu_be_timeout = RTW_AMSDU_TIMER_UNSET;

	rtw_init_timer(&(pxmitpriv->amsdu_bk_timer), padapter,
		rtw_amsdu_bk_timeout_handler, padapter);
	pxmitpriv->amsdu_bk_timeout = RTW_AMSDU_TIMER_UNSET;

	pxmitpriv->amsdu_debug_set_timer = 0;
	pxmitpriv->amsdu_debug_timeout = 0;
	pxmitpriv->amsdu_debug_coalesce_one = 0;
	pxmitpriv->amsdu_debug_coalesce_two = 0;
#endif
#ifdef DBG_TXBD_DESC_DUMP
	pxmitpriv->dump_txbd_desc = 0;
#endif
	rtw_init_xmit_block(padapter);
	rtw_hal_init_xmit_priv(padapter);

exit:


	return res;
}

void  rtw_mfree_xmit_priv_lock(struct xmit_priv *pxmitpriv);
void  rtw_mfree_xmit_priv_lock(struct xmit_priv *pxmitpriv)
{
	_rtw_spinlock_free(&pxmitpriv->lock);
	_rtw_free_sema(&pxmitpriv->xmit_sema);

	_rtw_spinlock_free(&pxmitpriv->be_pending.lock);
	_rtw_spinlock_free(&pxmitpriv->bk_pending.lock);
	_rtw_spinlock_free(&pxmitpriv->vi_pending.lock);
	_rtw_spinlock_free(&pxmitpriv->vo_pending.lock);
	_rtw_spinlock_free(&pxmitpriv->bm_pending.lock);

	/* _rtw_spinlock_free(&pxmitpriv->legacy_dz_queue.lock); */
	/* _rtw_spinlock_free(&pxmitpriv->apsd_queue.lock); */

	_rtw_spinlock_free(&pxmitpriv->free_xmit_queue.lock);
	_rtw_spinlock_free(&pxmitpriv->free_xmitbuf_queue.lock);
	_rtw_spinlock_free(&pxmitpriv->pending_xmitbuf_queue.lock);
}


void _rtw_free_xmit_priv(struct xmit_priv *pxmitpriv)
{
	int i;
	_adapter *padapter = pxmitpriv->adapter;
	struct xmit_frame	*pxmitframe = (struct xmit_frame *) pxmitpriv->pxmit_frame_buf;
	struct xmit_buf *pxmitbuf = (struct xmit_buf *)pxmitpriv->pxmitbuf;


	rtw_hal_free_xmit_priv(padapter);

	rtw_mfree_xmit_priv_lock(pxmitpriv);

	if (pxmitpriv->pxmit_frame_buf == NULL)
		goto out;

	for (i = 0; i < NR_XMITFRAME; i++) {
		rtw_os_xmit_complete(padapter, pxmitframe);

		pxmitframe++;
	}

	for (i = 0; i < NR_XMITBUFF; i++) {
		rtw_os_xmit_resource_free(padapter, pxmitbuf, (MAX_XMITBUF_SZ + XMITBUF_ALIGN_SZ), _TRUE);

		pxmitbuf++;
	}

	if (pxmitpriv->pallocated_frame_buf)
		rtw_vmfree(pxmitpriv->pallocated_frame_buf, NR_XMITFRAME * sizeof(struct xmit_frame) + 4);


	if (pxmitpriv->pallocated_xmitbuf)
		rtw_vmfree(pxmitpriv->pallocated_xmitbuf, NR_XMITBUFF * sizeof(struct xmit_buf) + 4);

	/* free xframe_ext queue,  the same count as extbuf */
	if ((pxmitframe = (struct xmit_frame *)pxmitpriv->xframe_ext)) {
		for (i = 0; i < NR_XMIT_EXTBUFF; i++) {
			rtw_os_xmit_complete(padapter, pxmitframe);
			pxmitframe++;
		}
	}
	if (pxmitpriv->xframe_ext_alloc_addr)
		rtw_vmfree(pxmitpriv->xframe_ext_alloc_addr, NR_XMIT_EXTBUFF * sizeof(struct xmit_frame) + 4);
	_rtw_spinlock_free(&pxmitpriv->free_xframe_ext_queue.lock);

	/* free xmit extension buff */
	_rtw_spinlock_free(&pxmitpriv->free_xmit_extbuf_queue.lock);

	pxmitbuf = (struct xmit_buf *)pxmitpriv->pxmit_extbuf;
	for (i = 0; i < NR_XMIT_EXTBUFF; i++) {
		rtw_os_xmit_resource_free(padapter, pxmitbuf, (MAX_XMIT_EXTBUF_SZ + XMITBUF_ALIGN_SZ), _TRUE);

		pxmitbuf++;
	}

	if (pxmitpriv->pallocated_xmit_extbuf)
		rtw_vmfree(pxmitpriv->pallocated_xmit_extbuf, NR_XMIT_EXTBUFF * sizeof(struct xmit_buf) + 4);

	for (i = 0; i < CMDBUF_MAX; i++) {
		pxmitbuf = &pxmitpriv->pcmd_xmitbuf[i];
		if (pxmitbuf != NULL)
			rtw_os_xmit_resource_free(padapter, pxmitbuf, MAX_CMDBUF_SZ + XMITBUF_ALIGN_SZ , _TRUE);
	}

	rtw_free_hwxmits(padapter);

#ifdef CONFIG_XMIT_ACK
	_rtw_mutex_free(&pxmitpriv->ack_tx_mutex);
#endif
	rtw_free_xmit_block(padapter);
out:
	return;
}

u8 rtw_get_tx_bw_mode(_adapter *adapter, struct sta_info *sta)
{
	u8 bw;

	bw = sta->cmn.bw_mode;
	if (MLME_STATE(adapter) & WIFI_ASOC_STATE) {
		if (adapter->mlmeextpriv.cur_channel <= 14)
			bw = rtw_min(bw, ADAPTER_TX_BW_2G(adapter));
		else
			bw = rtw_min(bw, ADAPTER_TX_BW_5G(adapter));
	}

	return bw;
}

void rtw_get_adapter_tx_rate_bmp_by_bw(_adapter *adapter, u8 bw, u16 *r_bmp_cck_ofdm, u32 *r_bmp_ht, u32 *r_bmp_vht)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct macid_ctl_t *macid_ctl = dvobj_to_macidctl(dvobj);
	u8 fix_bw = 0xFF;
	u16 bmp_cck_ofdm = 0;
	u32 bmp_ht = 0;
	u32 bmp_vht = 0;
	int i;

	if (adapter->fix_rate != 0xFF && adapter->fix_bw != 0xFF)
		fix_bw = adapter->fix_bw;

	/* TODO: adapter->fix_rate */

	for (i = 0; i < macid_ctl->num; i++) {
		if (!rtw_macid_is_used(macid_ctl, i))
			continue;
		if (!rtw_macid_is_iface_specific(macid_ctl, i, adapter))
			continue;

		if (bw == CHANNEL_WIDTH_20) /* CCK, OFDM always 20MHz */
			bmp_cck_ofdm |= macid_ctl->rate_bmp0[i] & 0x00000FFF;

		/* bypass mismatch bandwidth for HT, VHT */
		if ((fix_bw != 0xFF && fix_bw != bw) || (fix_bw == 0xFF && macid_ctl->bw[i] != bw))
			continue;

		if (macid_ctl->vht_en[i])
			bmp_vht |= (macid_ctl->rate_bmp0[i] >> 12) | (macid_ctl->rate_bmp1[i] << 20);
		else
			bmp_ht |= (macid_ctl->rate_bmp0[i] >> 12) | (macid_ctl->rate_bmp1[i] << 20);
	}

	/* TODO: mlmeext->tx_rate*/

	if (r_bmp_cck_ofdm)
		*r_bmp_cck_ofdm = bmp_cck_ofdm;
	if (r_bmp_ht)
		*r_bmp_ht = bmp_ht;
	if (r_bmp_vht)
		*r_bmp_vht = bmp_vht;
}

void rtw_get_shared_macid_tx_rate_bmp_by_bw(struct dvobj_priv *dvobj, u8 bw, u16 *r_bmp_cck_ofdm, u32 *r_bmp_ht, u32 *r_bmp_vht)
{
	struct macid_ctl_t *macid_ctl = dvobj_to_macidctl(dvobj);
	u16 bmp_cck_ofdm = 0;
	u32 bmp_ht = 0;
	u32 bmp_vht = 0;
	int i;

	for (i = 0; i < macid_ctl->num; i++) {
		if (!rtw_macid_is_used(macid_ctl, i))
			continue;
		if (!rtw_macid_is_iface_shared(macid_ctl, i))
			continue;

		if (bw == CHANNEL_WIDTH_20) /* CCK, OFDM always 20MHz */
			bmp_cck_ofdm |= macid_ctl->rate_bmp0[i] & 0x00000FFF;

		/* bypass mismatch bandwidth for HT, VHT */
		if (macid_ctl->bw[i] != bw)
			continue;

		if (macid_ctl->vht_en[i])
			bmp_vht |= (macid_ctl->rate_bmp0[i] >> 12) | (macid_ctl->rate_bmp1[i] << 20);
		else
			bmp_ht |= (macid_ctl->rate_bmp0[i] >> 12) | (macid_ctl->rate_bmp1[i] << 20);
	}

	if (r_bmp_cck_ofdm)
		*r_bmp_cck_ofdm = bmp_cck_ofdm;
	if (r_bmp_ht)
		*r_bmp_ht = bmp_ht;
	if (r_bmp_vht)
		*r_bmp_vht = bmp_vht;
}

void rtw_update_tx_rate_bmp(struct dvobj_priv *dvobj)
{
	struct rf_ctl_t *rf_ctl = dvobj_to_rfctl(dvobj);
	_adapter *adapter = dvobj_get_primary_adapter(dvobj);
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	u8 bw;
	u16 bmp_cck_ofdm, tmp_cck_ofdm;
	u32 bmp_ht, tmp_ht, ori_bmp_ht[2];
	u8 ori_highest_ht_rate_bw_bmp;
	u32 bmp_vht, tmp_vht, ori_bmp_vht[4];
	u8 ori_highest_vht_rate_bw_bmp;
	int i;

	/* backup the original ht & vht highest bw bmp */
	ori_highest_ht_rate_bw_bmp = rf_ctl->highest_ht_rate_bw_bmp;
	ori_highest_vht_rate_bw_bmp = rf_ctl->highest_vht_rate_bw_bmp;

	for (bw = CHANNEL_WIDTH_20; bw <= CHANNEL_WIDTH_160; bw++) {
		/* backup the original ht & vht bmp */
		if (bw <= CHANNEL_WIDTH_40)
			ori_bmp_ht[bw] = rf_ctl->rate_bmp_ht_by_bw[bw];
		if (bw <= CHANNEL_WIDTH_160)
			ori_bmp_vht[bw] = rf_ctl->rate_bmp_vht_by_bw[bw];

		bmp_cck_ofdm = bmp_ht = bmp_vht = 0;
		if (hal_is_bw_support(dvobj_get_primary_adapter(dvobj), bw)) {
			for (i = 0; i < dvobj->iface_nums; i++) {
				if (!dvobj->padapters[i])
					continue;
				rtw_get_adapter_tx_rate_bmp_by_bw(dvobj->padapters[i], bw, &tmp_cck_ofdm, &tmp_ht, &tmp_vht);
				bmp_cck_ofdm |= tmp_cck_ofdm;
				bmp_ht |= tmp_ht;
				bmp_vht |= tmp_vht;
			}
			rtw_get_shared_macid_tx_rate_bmp_by_bw(dvobj, bw, &tmp_cck_ofdm, &tmp_ht, &tmp_vht);
			bmp_cck_ofdm |= tmp_cck_ofdm;
			bmp_ht |= tmp_ht;
			bmp_vht |= tmp_vht;
		}
		if (bw == CHANNEL_WIDTH_20)
			rf_ctl->rate_bmp_cck_ofdm = bmp_cck_ofdm;
		if (bw <= CHANNEL_WIDTH_40)
			rf_ctl->rate_bmp_ht_by_bw[bw] = bmp_ht;
		if (bw <= CHANNEL_WIDTH_160)
			rf_ctl->rate_bmp_vht_by_bw[bw] = bmp_vht;
	}

#ifndef DBG_HIGHEST_RATE_BMP_BW_CHANGE
#define DBG_HIGHEST_RATE_BMP_BW_CHANGE 0
#endif

	{
		u8 highest_rate_bw;
		u8 highest_rate_bw_bmp;
		u8 update_ht_rs = _FALSE;
		u8 update_vht_rs = _FALSE;

		highest_rate_bw_bmp = BW_CAP_20M;
		highest_rate_bw = CHANNEL_WIDTH_20;
		for (bw = CHANNEL_WIDTH_20; bw <= CHANNEL_WIDTH_40; bw++) {
			if (rf_ctl->rate_bmp_ht_by_bw[highest_rate_bw] < rf_ctl->rate_bmp_ht_by_bw[bw]) {
				highest_rate_bw_bmp = ch_width_to_bw_cap(bw);
				highest_rate_bw = bw;
			} else if (rf_ctl->rate_bmp_ht_by_bw[highest_rate_bw] == rf_ctl->rate_bmp_ht_by_bw[bw])
				highest_rate_bw_bmp |= ch_width_to_bw_cap(bw);
		}
		rf_ctl->highest_ht_rate_bw_bmp = highest_rate_bw_bmp;

		if (ori_highest_ht_rate_bw_bmp != rf_ctl->highest_ht_rate_bw_bmp
			|| largest_bit(ori_bmp_ht[highest_rate_bw]) != largest_bit(rf_ctl->rate_bmp_ht_by_bw[highest_rate_bw])
		) {
			if (DBG_HIGHEST_RATE_BMP_BW_CHANGE) {
				RTW_INFO("highest_ht_rate_bw_bmp:0x%02x=>0x%02x\n", ori_highest_ht_rate_bw_bmp, rf_ctl->highest_ht_rate_bw_bmp);
				RTW_INFO("rate_bmp_ht_by_bw[%u]:0x%08x=>0x%08x\n", highest_rate_bw, ori_bmp_ht[highest_rate_bw], rf_ctl->rate_bmp_ht_by_bw[highest_rate_bw]);
			}
			update_ht_rs = _TRUE;
		}

		highest_rate_bw_bmp = BW_CAP_20M;
		highest_rate_bw = CHANNEL_WIDTH_20;
		for (bw = CHANNEL_WIDTH_20; bw <= CHANNEL_WIDTH_160; bw++) {
			if (rf_ctl->rate_bmp_vht_by_bw[highest_rate_bw] < rf_ctl->rate_bmp_vht_by_bw[bw]) {
				highest_rate_bw_bmp = ch_width_to_bw_cap(bw);
				highest_rate_bw = bw;
			} else if (rf_ctl->rate_bmp_vht_by_bw[highest_rate_bw] == rf_ctl->rate_bmp_vht_by_bw[bw])
				highest_rate_bw_bmp |= ch_width_to_bw_cap(bw);
		}
		rf_ctl->highest_vht_rate_bw_bmp = highest_rate_bw_bmp;

		if (ori_highest_vht_rate_bw_bmp != rf_ctl->highest_vht_rate_bw_bmp
			|| largest_bit(ori_bmp_vht[highest_rate_bw]) != largest_bit(rf_ctl->rate_bmp_vht_by_bw[highest_rate_bw])
		) {
			if (DBG_HIGHEST_RATE_BMP_BW_CHANGE) {
				RTW_INFO("highest_vht_rate_bw_bmp:0x%02x=>0x%02x\n", ori_highest_vht_rate_bw_bmp, rf_ctl->highest_vht_rate_bw_bmp);
				RTW_INFO("rate_bmp_vht_by_bw[%u]:0x%08x=>0x%08x\n", highest_rate_bw, ori_bmp_vht[highest_rate_bw], rf_ctl->rate_bmp_vht_by_bw[highest_rate_bw]);
			}
			update_vht_rs = _TRUE;
		}

		/* TODO: per rfpath and rate section handling? */
		if (update_ht_rs == _TRUE || update_vht_rs == _TRUE)
			rtw_hal_update_txpwr_level(adapter);
	}
}

inline u16 rtw_get_tx_rate_bmp_cck_ofdm(struct dvobj_priv *dvobj)
{
	struct rf_ctl_t *rf_ctl = dvobj_to_rfctl(dvobj);

	return rf_ctl->rate_bmp_cck_ofdm;
}

inline u32 rtw_get_tx_rate_bmp_ht_by_bw(struct dvobj_priv *dvobj, u8 bw)
{
	struct rf_ctl_t *rf_ctl = dvobj_to_rfctl(dvobj);

	return rf_ctl->rate_bmp_ht_by_bw[bw];
}

inline u32 rtw_get_tx_rate_bmp_vht_by_bw(struct dvobj_priv *dvobj, u8 bw)
{
	struct rf_ctl_t *rf_ctl = dvobj_to_rfctl(dvobj);

	return rf_ctl->rate_bmp_vht_by_bw[bw];
}

u8 rtw_get_tx_bw_bmp_of_ht_rate(struct dvobj_priv *dvobj, u8 rate, u8 max_bw)
{
	struct rf_ctl_t *rf_ctl = dvobj_to_rfctl(dvobj);
	u8 bw;
	u8 bw_bmp = 0;
	u32 rate_bmp;

	if (!IS_HT_RATE(rate)) {
		rtw_warn_on(1);
		goto exit;
	}

	rate_bmp = 1 << (rate - MGN_MCS0);

	if (max_bw > CHANNEL_WIDTH_40)
		max_bw = CHANNEL_WIDTH_40;

	for (bw = CHANNEL_WIDTH_20; bw <= max_bw; bw++) {
		/* RA may use lower rate for retry */
		if (rf_ctl->rate_bmp_ht_by_bw[bw] >= rate_bmp)
			bw_bmp |= ch_width_to_bw_cap(bw);
	}

exit:
	return bw_bmp;
}

u8 rtw_get_tx_bw_bmp_of_vht_rate(struct dvobj_priv *dvobj, u8 rate, u8 max_bw)
{
	struct rf_ctl_t *rf_ctl = dvobj_to_rfctl(dvobj);
	u8 bw;
	u8 bw_bmp = 0;
	u32 rate_bmp;

	if (!IS_VHT_RATE(rate)) {
		rtw_warn_on(1);
		goto exit;
	}

	rate_bmp = 1 << (rate - MGN_VHT1SS_MCS0);

	if (max_bw > CHANNEL_WIDTH_160)
		max_bw = CHANNEL_WIDTH_160;

	for (bw = CHANNEL_WIDTH_20; bw <= max_bw; bw++) {
		/* RA may use lower rate for retry */
		if (rf_ctl->rate_bmp_vht_by_bw[bw] >= rate_bmp)
			bw_bmp |= ch_width_to_bw_cap(bw);
	}

exit:
	return bw_bmp;
}

u8 query_ra_short_GI(struct sta_info *psta, u8 bw)
{
	u8	sgi = _FALSE, sgi_20m = _FALSE, sgi_40m = _FALSE, sgi_80m = _FALSE;

#ifdef CONFIG_80211N_HT
#ifdef CONFIG_80211AC_VHT
	if (psta->vhtpriv.vht_option)
		sgi_80m = psta->vhtpriv.sgi_80m;
#endif
	sgi_20m = psta->htpriv.sgi_20m;
	sgi_40m = psta->htpriv.sgi_40m;
#endif

	switch (bw) {
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

static void update_attrib_vcs_info(_adapter *padapter, struct xmit_frame *pxmitframe)
{
	u32	sz;
	struct pkt_attrib	*pattrib = &pxmitframe->attrib;
	/* struct sta_info	*psta = pattrib->psta; */
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	/*
		if(pattrib->psta)
		{
			psta = pattrib->psta;
		}
		else
		{
			RTW_INFO("%s, call rtw_get_stainfo()\n", __func__);
			psta=rtw_get_stainfo(&padapter->stapriv ,&pattrib->ra[0] );
		}

		if(psta==NULL)
		{
			RTW_INFO("%s, psta==NUL\n", __func__);
			return;
		}

		if(!(psta->state &_FW_LINKED))
		{
			RTW_INFO("%s, psta->state(0x%x) != _FW_LINKED\n", __func__, psta->state);
			return;
		}
	*/

	if (pattrib->nr_frags != 1)
		sz = padapter->xmitpriv.frag_len;
	else /* no frag */
		sz = pattrib->last_txcmdsz;

	/* (1) RTS_Threshold is compared to the MPDU, not MSDU. */
	/* (2) If there are more than one frag in  this MSDU, only the first frag uses protection frame. */
	/*		Other fragments are protected by previous fragment. */
	/*		So we only need to check the length of first fragment. */
	if (pmlmeext->cur_wireless_mode < WIRELESS_11_24N  || padapter->registrypriv.wifi_spec) {
		if (sz > padapter->registrypriv.rts_thresh)
			pattrib->vcs_mode = RTS_CTS;
		else {
			if (pattrib->rtsen)
				pattrib->vcs_mode = RTS_CTS;
			else if (pattrib->cts2self)
				pattrib->vcs_mode = CTS_TO_SELF;
			else
				pattrib->vcs_mode = NONE_VCS;
		}
	} else {
		while (_TRUE) {
#if 0 /* Todo */
			/* check IOT action */
			if (pHTInfo->IOTAction & HT_IOT_ACT_FORCED_CTS2SELF) {
				pattrib->vcs_mode = CTS_TO_SELF;
				pattrib->rts_rate = MGN_24M;
				break;
			} else if (pHTInfo->IOTAction & (HT_IOT_ACT_FORCED_RTS | HT_IOT_ACT_PURE_N_MODE)) {
				pattrib->vcs_mode = RTS_CTS;
				pattrib->rts_rate = MGN_24M;
				break;
			}
#endif

			/* IOT action */
			if ((pmlmeinfo->assoc_AP_vendor == HT_IOT_PEER_ATHEROS) && (pattrib->ampdu_en == _TRUE) &&
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
			if ((pattrib->ampdu_en == _TRUE) && (!IS_HARDWARE_TYPE_8812(padapter))) {
				pattrib->vcs_mode = RTS_CTS;
				break;
			}

			pattrib->vcs_mode = NONE_VCS;
			break;
		}
	}

	/* for debug : force driver control vrtl_carrier_sense. */
	if (padapter->driver_vcs_en == 1) {
		/* u8 driver_vcs_en; */ /* Enable=1, Disable=0 driver control vrtl_carrier_sense. */
		/* u8 driver_vcs_type; */ /* force 0:disable VCS, 1:RTS-CTS, 2:CTS-to-self when vcs_en=1. */
		pattrib->vcs_mode = padapter->driver_vcs_type;
	}

}

#ifdef CONFIG_WMMPS_STA
/*
 * update_attrib_trigger_frame_info
 * For Station mode, if a specific TID of driver setting and an AP support uapsd function, the data 
 * frame with corresponding TID will be a trigger frame when driver is in wmm power saving mode.
 * 
 * Arguments:
 * @padapter: _adapter pointer.
 * @pattrib: pkt_attrib pointer.
 *
 * Auther: Arvin Liu
 * Date: 2017/06/05
 */
static void update_attrib_trigger_frame_info(_adapter *padapter, struct pkt_attrib *pattrib) {
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct pwrctrl_priv 	*pwrpriv = adapter_to_pwrctl(padapter); 
	struct qos_priv 	*pqospriv = &pmlmepriv->qospriv;
	u8 trigger_frame_en = 0;

	if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) == _TRUE) {
		if ((pwrpriv->pwr_mode == PS_MODE_MIN) || (pwrpriv->pwr_mode == PS_MODE_MAX)) {
			if((pqospriv->uapsd_ap_supported) && ((pqospriv->uapsd_tid & BIT(pattrib->priority)) == _TRUE)) {
				trigger_frame_en = 1;
				RTW_INFO("[WMMPS]"FUNC_ADPT_FMT": This is a Trigger Frame\n", FUNC_ADPT_ARG(padapter));
			}
		}
	}

	pattrib->trigger_frame = trigger_frame_en;
}
#endif /* CONFIG_WMMPS_STA */

static void update_attrib_phy_info(_adapter *padapter, struct pkt_attrib *pattrib, struct sta_info *psta)
{
	struct mlme_ext_priv *mlmeext = &padapter->mlmeextpriv;
	u8 bw;

	pattrib->rtsen = psta->rtsen;
	pattrib->cts2self = psta->cts2self;

	pattrib->mdata = 0;
	pattrib->eosp = 0;
	pattrib->triggered = 0;
	pattrib->ampdu_spacing = 0;

	/* ht_en, init rate, ,bw, ch_offset, sgi */

	pattrib->raid = psta->cmn.ra_info.rate_id;

	bw = rtw_get_tx_bw_mode(padapter, psta);
	pattrib->bwmode = rtw_min(bw, mlmeext->cur_bwmode);
	pattrib->sgi = query_ra_short_GI(psta, pattrib->bwmode);

	pattrib->ldpc = psta->cmn.ldpc_en;
	pattrib->stbc = psta->cmn.stbc_en;

#ifdef CONFIG_80211N_HT
	if(padapter->registrypriv.ht_enable &&
		is_supported_ht(padapter->registrypriv.wireless_mode)) {
		pattrib->ht_en = psta->htpriv.ht_option;
		pattrib->ch_offset = psta->htpriv.ch_offset;
		pattrib->ampdu_en = _FALSE;

		if (padapter->driver_ampdu_spacing != 0xFF) /* driver control AMPDU Density for peer sta's rx */
			pattrib->ampdu_spacing = padapter->driver_ampdu_spacing;
		else
			pattrib->ampdu_spacing = psta->htpriv.rx_ampdu_min_spacing;

		/* check if enable ampdu */
		if (pattrib->ht_en && psta->htpriv.ampdu_enable) {
			if (psta->htpriv.agg_enable_bitmap & BIT(pattrib->priority)) {
				pattrib->ampdu_en = _TRUE;
				if (psta->htpriv.tx_amsdu_enable == _TRUE)
					pattrib->amsdu_ampdu_en = _TRUE;
				else
					pattrib->amsdu_ampdu_en = _FALSE;
			}
		}
	}
#endif /* CONFIG_80211N_HT */
	/* if(pattrib->ht_en && psta->htpriv.ampdu_enable) */
	/* { */
	/*	if(psta->htpriv.agg_enable_bitmap & BIT(pattrib->priority)) */
	/*		pattrib->ampdu_en = _TRUE; */
	/* }	 */

#ifdef CONFIG_TDLS
	if (pattrib->direct_link == _TRUE) {
		psta = pattrib->ptdls_sta;

		pattrib->raid = psta->cmn.ra_info.rate_id;
#ifdef CONFIG_80211N_HT
	if(padapter->registrypriv.ht_enable &&
		is_supported_ht(padapter->registrypriv.wireless_mode)) {
			pattrib->bwmode = rtw_get_tx_bw_mode(padapter, psta);
			pattrib->ht_en = psta->htpriv.ht_option;
			pattrib->ch_offset = psta->htpriv.ch_offset;
			pattrib->sgi = query_ra_short_GI(psta, pattrib->bwmode);
	}
#endif /* CONFIG_80211N_HT */
	}
#endif /* CONFIG_TDLS */

	pattrib->retry_ctrl = _FALSE;
}

static s32 update_attrib_sec_info(_adapter *padapter, struct pkt_attrib *pattrib, struct sta_info *psta)
{
	sint res = _SUCCESS;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	sint bmcast = IS_MCAST(pattrib->ra);

	_rtw_memset(pattrib->dot118021x_UncstKey.skey,  0, 16);
	_rtw_memset(pattrib->dot11tkiptxmickey.skey,  0, 16);
	pattrib->mac_id = psta->cmn.mac_id;

	if (psta->ieee8021x_blocked == _TRUE) {

		pattrib->encrypt = 0;

		if ((pattrib->ether_type != 0x888e) && (check_fwstate(pmlmepriv, WIFI_MP_STATE) == _FALSE)) {
#ifdef DBG_TX_DROP_FRAME
			RTW_INFO("DBG_TX_DROP_FRAME %s psta->ieee8021x_blocked == _TRUE,  pattrib->ether_type(%04x) != 0x888e\n", __FUNCTION__, pattrib->ether_type);
#endif
			res = _FAIL;
			goto exit;
		}
	} else {
		GET_ENCRY_ALGO(psecuritypriv, psta, pattrib->encrypt, bmcast);

#ifdef CONFIG_WAPI_SUPPORT
		if (pattrib->ether_type == 0x88B4)
			pattrib->encrypt = _NO_PRIVACY_;
#endif

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

#ifdef CONFIG_TDLS
	if (pattrib->direct_link == _TRUE) {
		if (pattrib->encrypt > 0)
			pattrib->encrypt = _AES_;
	}
#endif

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
#ifdef DBG_TX_DROP_FRAME
			RTW_INFO("DBG_TX_DROP_FRAME %s psecuritypriv->busetkipkey(%d)==_FAIL drop packet\n", __FUNCTION__, psecuritypriv->busetkipkey);
#endif
			res = _FAIL;
			goto exit;
		}

		if (bmcast)
			TKIP_IV(pattrib->iv, psta->dot11txpn, pattrib->key_idx);
		else
			TKIP_IV(pattrib->iv, psta->dot11txpn, 0);


		_rtw_memcpy(pattrib->dot11tkiptxmickey.skey, psta->dot11tkiptxmickey.skey, 16);

		break;

	case _AES_:

		pattrib->iv_len = 8;
		pattrib->icv_len = 8;

		if (bmcast)
			AES_IV(pattrib->iv, psta->dot11txpn, pattrib->key_idx);
		else
			AES_IV(pattrib->iv, psta->dot11txpn, 0);

		break;

#ifdef CONFIG_WAPI_SUPPORT
	case _SMS4_:
		pattrib->iv_len = 18;
		pattrib->icv_len = 16;
		rtw_wapi_get_iv(padapter, pattrib->ra, pattrib->iv);
		break;
#endif
	default:
		pattrib->iv_len = 0;
		pattrib->icv_len = 0;
		break;
	}

	if (pattrib->encrypt > 0)
		_rtw_memcpy(pattrib->dot118021x_UncstKey.skey, psta->dot118021x_UncstKey.skey, 16);


	if (pattrib->encrypt &&
	    ((padapter->securitypriv.sw_encrypt == _TRUE) || (psecuritypriv->hw_decrypted == _FALSE))) {
		pattrib->bswenc = _TRUE;
	} else {
		pattrib->bswenc = _FALSE;
	}

#if defined(CONFIG_CONCURRENT_MODE)
	pattrib->bmc_camid = padapter->securitypriv.dot118021x_bmc_cam_id;
#endif

	if (pattrib->encrypt && bmcast && _rtw_camctl_chk_flags(padapter, SEC_STATUS_STA_PK_GK_CONFLICT_DIS_BMC_SEARCH))
		pattrib->bswenc = _TRUE;

#ifdef CONFIG_WAPI_SUPPORT
	if (pattrib->encrypt == _SMS4_)
		pattrib->bswenc = _FALSE;
#endif

exit:

	return res;

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
		RTW_INFO("qos_acm(): invalid pattrib->priority: %d!!!\n", priority);
		break;
	}

	return change_priority;
}

#ifdef CONFIG_USER_PRIORITY_COMPLY_RFC4594_DSCP
/* refer to IEEE802.11-2016 Table R-3; Comply with Table R-2 (IETF RFC4594) */
static u8 dscp_to_up_ac(u8 tos)
{
	u8 up = 0;
	u8 dscp;

	dscp = (tos >> 2);

	if ( dscp == 0 )
		up = 0;
	else if ( dscp >= 1 && dscp <= 9)
		up = 1;
	else if ( dscp >= 10 && dscp <= 16)
		up = 2;
	else if ( dscp >= 17 && dscp <= 23)
		up = 3;
	else if ( dscp >= 24 && dscp <= 31)
		up = 4;
	else if ( dscp >= 33 && dscp <= 40)
		up = 5;
	else if ((dscp >= 41 && dscp <= 47) || (dscp == 32))
		up = 6;
	else if ( dscp >= 48 && dscp <= 63)
		up = 7;

	return up;
}
#endif

static void set_qos(struct pkt_file *ppktfile, struct pkt_attrib *pattrib)
{
	struct ethhdr etherhdr;
	struct iphdr ip_hdr;
	s32 UserPriority = 0;


	_rtw_open_pktfile(ppktfile->pkt, ppktfile);
	_rtw_pktfile_read(ppktfile, (unsigned char *)&etherhdr, ETH_HLEN);

	/* get UserPriority from IP hdr */
	if (pattrib->ether_type == 0x0800) {
		_rtw_pktfile_read(ppktfile, (u8 *)&ip_hdr, sizeof(ip_hdr));
		/*		UserPriority = (ntohs(ip_hdr.tos) >> 5) & 0x3; */
#ifdef CONFIG_USER_PRIORITY_COMPLY_RFC4594_DSCP
		UserPriority = dscp_to_up_ac(ip_hdr.tos);
#else
		UserPriority = ip_hdr.tos >> 5;
#endif
	}
	/*
		else if (pattrib->ether_type == 0x888e) {


			UserPriority = 7;
		}
	*/

	#ifdef CONFIG_ICMP_VOQ
	if(pattrib->icmp_pkt==1)/*use VO queue to send icmp packet*/
		UserPriority = 7;
	#endif
	#ifdef CONFIG_IP_R_MONITOR
	if (pattrib->ether_type == ETH_P_ARP)
		UserPriority = 7;
	#endif/*CONFIG_IP_R_MONITOR*/
	pattrib->priority = UserPriority;
	pattrib->hdrlen = WLAN_HDR_A3_QOS_LEN;
	pattrib->subtype = WIFI_QOS_DATA_TYPE;
}

#ifdef CONFIG_TDLS
u8 rtw_check_tdls_established(_adapter *padapter, struct pkt_attrib *pattrib)
{
	pattrib->ptdls_sta = NULL;

	pattrib->direct_link = _FALSE;
	if (padapter->tdlsinfo.link_established == _TRUE) {
		pattrib->ptdls_sta = rtw_get_stainfo(&padapter->stapriv, pattrib->dst);
#if 1
		if ((pattrib->ptdls_sta != NULL) &&
		    (pattrib->ptdls_sta->tdls_sta_state & TDLS_LINKED_STATE) &&
		    (pattrib->ether_type != 0x0806)) {
			pattrib->direct_link = _TRUE;
			/* RTW_INFO("send ptk to "MAC_FMT" using direct link\n", MAC_ARG(pattrib->dst)); */
		}
#else
		if (pattrib->ptdls_sta != NULL &&
		    pattrib->ptdls_sta->tdls_sta_state & TDLS_LINKED_STATE) {
			pattrib->direct_link = _TRUE;
#if 0
			RTW_INFO("send ptk to "MAC_FMT" using direct link\n", MAC_ARG(pattrib->dst));
#endif
		}

		/* ARP frame may be helped by AP*/
		if (pattrib->ether_type != 0x0806)
			pattrib->direct_link = _FALSE;
#endif
	}

	return pattrib->direct_link;
}

s32 update_tdls_attrib(_adapter *padapter, struct pkt_attrib *pattrib)
{

	struct sta_info *psta = NULL;
	struct sta_priv		*pstapriv = &padapter->stapriv;
	struct security_priv	*psecuritypriv = &padapter->securitypriv;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct qos_priv		*pqospriv = &pmlmepriv->qospriv;

	s32 res = _SUCCESS;

	psta = rtw_get_stainfo(pstapriv, pattrib->ra);
	if (psta == NULL)	{
		res = _FAIL;
		goto exit;
	}

	pattrib->mac_id = psta->cmn.mac_id;
	pattrib->psta = psta;
	pattrib->ack_policy = 0;
	/* get ether_hdr_len */
	pattrib->pkt_hdrlen = ETH_HLEN;

	pattrib->qos_en = psta->qos_option;

	/* [TDLS] TODO: setup req/rsp should be AC_BK */
	if (pqospriv->qos_option &&  psta->qos_option) {
		pattrib->priority = 4;	/* tdls management frame should be AC_VI */
		pattrib->hdrlen = WLAN_HDR_A3_QOS_LEN;
		pattrib->subtype = WIFI_QOS_DATA_TYPE;
	} else {
		pattrib->priority = 0;
		pattrib->hdrlen = WLAN_HDR_A3_LEN;
		pattrib->subtype = WIFI_DATA_TYPE;
	}

	/* TODO:_lock */
	if (update_attrib_sec_info(padapter, pattrib, psta) == _FAIL) {
		res = _FAIL;
		goto exit;
	}

	update_attrib_phy_info(padapter, pattrib, psta);


exit:

	return res;
}

#endif /* CONFIG_TDLS */

/*get non-qos hw_ssn control register,mapping to REG_HW_SEQ 0,1,2,3*/
inline u8 rtw_get_hwseq_no(_adapter *padapter)
{
	u8 hwseq_num = 0;

#ifdef CONFIG_CONCURRENT_MODE
	#if defined(CONFIG_RTL8822B) || defined(CONFIG_RTL8821C) || defined(CONFIG_RTL8822C)
	hwseq_num = padapter->iface_id;
	if (hwseq_num > 3)
		hwseq_num = 3;
	#else
	if (!is_primary_adapter(padapter))
		hwseq_num = 1;
	#endif
#endif /* CONFIG_CONCURRENT_MODE */
	return hwseq_num;
}
#ifdef CONFIG_LPS
#define LPS_PT_NORMAL	0
#define LPS_PT_SP		1/* only DHCP packets is as SPECIAL_PACKET*/
#define LPS_PT_ICMP		2

/*If EAPOL , ARP , OR DHCP packet, driver must be in active mode.*/
static u8 _rtw_lps_chk_packet_type(struct pkt_attrib *pattrib)
{
	u8 pkt_type = LPS_PT_NORMAL; /*normal data frame*/

	#ifdef CONFIG_WAPI_SUPPORT
	if ((pattrib->ether_type == 0x88B4) || (pattrib->ether_type == 0x0806) || (pattrib->ether_type == 0x888e) || (pattrib->dhcp_pkt == 1))
		pkt_type = LPS_PT_SP;
	#else /* !CONFIG_WAPI_SUPPORT */

	#ifndef CONFIG_LPS_NOT_LEAVE_FOR_ICMP
	if (pattrib->icmp_pkt == 1)
		pkt_type = LPS_PT_ICMP;
	else
	#endif
		if (pattrib->dhcp_pkt == 1)
			pkt_type = LPS_PT_SP;
	#endif
	return pkt_type;
}
#endif
static s32 update_attrib(_adapter *padapter, _pkt *pkt, struct pkt_attrib *pattrib)
{
	uint i;
	struct pkt_file pktfile;
	struct sta_info *psta = NULL;
	struct ethhdr etherhdr;

	sint bmcast;
	struct sta_priv		*pstapriv = &padapter->stapriv;
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	struct qos_priv		*pqospriv = &pmlmepriv->qospriv;
	struct xmit_priv		*pxmitpriv = &padapter->xmitpriv;
	sint res = _SUCCESS;
#ifdef CONFIG_LPS
	u8 pkt_type = 0;
#endif

	DBG_COUNTER(padapter->tx_logs.core_tx_upd_attrib);

	_rtw_open_pktfile(pkt, &pktfile);
	i = _rtw_pktfile_read(&pktfile, (u8 *)&etherhdr, ETH_HLEN);

	pattrib->ether_type = ntohs(etherhdr.h_proto);

	if (MLME_IS_MESH(padapter)) /* address resolve is done for mesh */
		goto get_sta_info;

	_rtw_memcpy(pattrib->dst, &etherhdr.h_dest, ETH_ALEN);
	_rtw_memcpy(pattrib->src, &etherhdr.h_source, ETH_ALEN);

	if ((check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == _TRUE) ||
	    (check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == _TRUE)) {
		_rtw_memcpy(pattrib->ra, pattrib->dst, ETH_ALEN);
		_rtw_memcpy(pattrib->ta, adapter_mac_addr(padapter), ETH_ALEN);
		DBG_COUNTER(padapter->tx_logs.core_tx_upd_attrib_adhoc);
	} else if (check_fwstate(pmlmepriv, WIFI_STATION_STATE)) {
#ifdef CONFIG_TDLS
		if (rtw_check_tdls_established(padapter, pattrib) == _TRUE)
			_rtw_memcpy(pattrib->ra, pattrib->dst, ETH_ALEN);	/* For TDLS direct link Tx, set ra to be same to dst */
		else
#endif
			_rtw_memcpy(pattrib->ra, get_bssid(pmlmepriv), ETH_ALEN);
		_rtw_memcpy(pattrib->ta, adapter_mac_addr(padapter), ETH_ALEN);
		DBG_COUNTER(padapter->tx_logs.core_tx_upd_attrib_sta);
	} else if (check_fwstate(pmlmepriv, WIFI_AP_STATE)) {
		_rtw_memcpy(pattrib->ra, pattrib->dst, ETH_ALEN);
		_rtw_memcpy(pattrib->ta, get_bssid(pmlmepriv), ETH_ALEN);
		DBG_COUNTER(padapter->tx_logs.core_tx_upd_attrib_ap);
	} else
		DBG_COUNTER(padapter->tx_logs.core_tx_upd_attrib_unknown);

get_sta_info:
	bmcast = IS_MCAST(pattrib->ra);
	if (bmcast) {
		psta = rtw_get_bcmc_stainfo(padapter);
		if (psta == NULL) { /* if we cannot get psta => drop the pkt */
			DBG_COUNTER(padapter->tx_logs.core_tx_upd_attrib_err_sta);
			#ifdef DBG_TX_DROP_FRAME
			RTW_INFO("DBG_TX_DROP_FRAME %s get sta_info fail, ra:" MAC_FMT"\n", __func__, MAC_ARG(pattrib->ra));
			#endif
			res = _FAIL;
			goto exit;
		}
	} else {
		psta = rtw_get_stainfo(pstapriv, pattrib->ra);
		if (psta == NULL) { /* if we cannot get psta => drop the pkt */
			DBG_COUNTER(padapter->tx_logs.core_tx_upd_attrib_err_ucast_sta);
			#ifdef DBG_TX_DROP_FRAME
			RTW_INFO("DBG_TX_DROP_FRAME %s get sta_info fail, ra:" MAC_FMT"\n", __func__, MAC_ARG(pattrib->ra));
			#endif
			res = _FAIL;
			goto exit;
		} else if (check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE && !(psta->state & _FW_LINKED)) {
			DBG_COUNTER(padapter->tx_logs.core_tx_upd_attrib_err_ucast_ap_link);
			res = _FAIL;
			goto exit;
		}
	}

	if (!(psta->state & _FW_LINKED)) {
		DBG_COUNTER(padapter->tx_logs.core_tx_upd_attrib_err_link);
		RTW_INFO("%s-"ADPT_FMT" psta("MAC_FMT")->state(0x%x) != _FW_LINKED\n",
			__func__, ADPT_ARG(padapter), MAC_ARG(psta->cmn.mac_addr), psta->state);
		res = _FAIL;
		goto exit;
	}

	pattrib->pktlen = pktfile.pkt_len;

	/* TODO: 802.1Q VLAN header */
	/* TODO: IPV6 */

	if (ETH_P_IP == pattrib->ether_type) {
		u8 ip[20];

		_rtw_pktfile_read(&pktfile, ip, 20);

		if (GET_IPV4_IHL(ip) * 4 > 20)
			_rtw_pktfile_read(&pktfile, NULL, GET_IPV4_IHL(ip) - 20);

		pattrib->icmp_pkt = 0;
		pattrib->dhcp_pkt = 0;

		if (GET_IPV4_PROTOCOL(ip) == 0x01) { /* ICMP */
			pattrib->icmp_pkt = 1;
			DBG_COUNTER(padapter->tx_logs.core_tx_upd_attrib_icmp);

		} else if (GET_IPV4_PROTOCOL(ip) == 0x11) { /* UDP */
			u8 udp[8];

			_rtw_pktfile_read(&pktfile, udp, 8);

			if ((GET_UDP_SRC(udp) == 68 && GET_UDP_DST(udp) == 67)
				|| (GET_UDP_SRC(udp) == 67 && GET_UDP_DST(udp) == 68)
			) {
				/* 67 : UDP BOOTP server, 68 : UDP BOOTP client */
				if (pattrib->pktlen > 282) { /* MINIMUM_DHCP_PACKET_SIZE */
					pattrib->dhcp_pkt = 1;
					DBG_COUNTER(padapter->tx_logs.core_tx_upd_attrib_dhcp);
					if (0)
						RTW_INFO("send DHCP packet\n");
				}
			}

		} else if (GET_IPV4_PROTOCOL(ip) == 0x06 /* TCP */
			&& rtw_st_ctl_chk_reg_s_proto(&psta->st_ctl, 0x06) == _TRUE
		) {
			u8 tcp[20];

			_rtw_pktfile_read(&pktfile, tcp, 20);

			if (rtw_st_ctl_chk_reg_rule(&psta->st_ctl, padapter, IPV4_SRC(ip), TCP_SRC(tcp), IPV4_DST(ip), TCP_DST(tcp)) == _TRUE) {
				if (GET_TCP_SYN(tcp) && GET_TCP_ACK(tcp)) {
					session_tracker_add_cmd(padapter, psta
						, IPV4_SRC(ip), TCP_SRC(tcp)
						, IPV4_SRC(ip), TCP_DST(tcp));
					if (DBG_SESSION_TRACKER)
						RTW_INFO(FUNC_ADPT_FMT" local:"IP_FMT":"PORT_FMT", remote:"IP_FMT":"PORT_FMT" SYN-ACK\n"
							, FUNC_ADPT_ARG(padapter)
							, IP_ARG(IPV4_SRC(ip)), PORT_ARG(TCP_SRC(tcp))
							, IP_ARG(IPV4_DST(ip)), PORT_ARG(TCP_DST(tcp)));
				}
				if (GET_TCP_FIN(tcp)) {
					session_tracker_del_cmd(padapter, psta
						, IPV4_SRC(ip), TCP_SRC(tcp)
						, IPV4_SRC(ip), TCP_DST(tcp));
					if (DBG_SESSION_TRACKER)
						RTW_INFO(FUNC_ADPT_FMT" local:"IP_FMT":"PORT_FMT", remote:"IP_FMT":"PORT_FMT" FIN\n"
							, FUNC_ADPT_ARG(padapter)
							, IP_ARG(IPV4_SRC(ip)), PORT_ARG(TCP_SRC(tcp))
							, IP_ARG(IPV4_DST(ip)), PORT_ARG(TCP_DST(tcp)));
				}
			}
		}

	} else if (0x888e == pattrib->ether_type)
		parsing_eapol_packet(padapter, pktfile.cur_addr, psta, 1);
#if defined (DBG_ARP_DUMP) || defined (DBG_IP_R_MONITOR)
	else if (pattrib->ether_type == ETH_P_ARP) {
		u8 arp[28] = {0};

		_rtw_pktfile_read(&pktfile, arp, 28);
		dump_arp_pkt(RTW_DBGDUMP, etherhdr.h_dest, etherhdr.h_source, arp, 1);
	}
#endif

	if ((pattrib->ether_type == 0x888e) || (pattrib->dhcp_pkt == 1))
		rtw_mi_set_scan_deny(padapter, 3000);

	if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) &&
		pattrib->ether_type == ETH_P_ARP &&
		!IS_MCAST(pattrib->dst)) {
		rtw_mi_set_scan_deny(padapter, 1000);
		rtw_mi_scan_abort(padapter, _FALSE); /*rtw_scan_abort_no_wait*/
	}

#ifdef CONFIG_LPS
	pkt_type = _rtw_lps_chk_packet_type(pattrib);

	if (pkt_type == LPS_PT_SP) {/*packet is as SPECIAL_PACKET*/
		DBG_COUNTER(padapter->tx_logs.core_tx_upd_attrib_active);
		rtw_lps_ctrl_wk_cmd(padapter, LPS_CTRL_SPECIAL_PACKET, 0);
	} else if (pkt_type == LPS_PT_ICMP)
		rtw_lps_ctrl_wk_cmd(padapter, LPS_CTRL_LEAVE, 0);
#endif /* CONFIG_LPS */

#ifdef CONFIG_BEAMFORMING
	update_attrib_txbf_info(padapter, pattrib, psta);
#endif

	/* TODO:_lock */
	if (update_attrib_sec_info(padapter, pattrib, psta) == _FAIL) {
		DBG_COUNTER(padapter->tx_logs.core_tx_upd_attrib_err_sec);
		res = _FAIL;
		goto exit;
	}

	/* get ether_hdr_len */
	pattrib->pkt_hdrlen = ETH_HLEN;/* (pattrib->ether_type == 0x8100) ? (14 + 4 ): 14; */ /* vlan tag */

	pattrib->hdrlen = WLAN_HDR_A3_LEN;
	pattrib->subtype = WIFI_DATA_TYPE;
	pattrib->qos_en = psta->qos_option;
	pattrib->priority = 0;

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE | WIFI_MESH_STATE
		| WIFI_ADHOC_STATE | WIFI_ADHOC_MASTER_STATE)
	) {
		if (pattrib->qos_en) {
			set_qos(&pktfile, pattrib);
			#ifdef CONFIG_RTW_MESH
			if (MLME_IS_MESH(padapter))
				rtw_mesh_tx_set_whdr_mctrl_len(pattrib->mesh_frame_mode, pattrib);
			#endif
		}
	} else {
#ifdef CONFIG_TDLS
		if (pattrib->direct_link == _TRUE) {
			if (pattrib->qos_en)
				set_qos(&pktfile, pattrib);
		} else
#endif
		{
			if (pqospriv->qos_option) {
				set_qos(&pktfile, pattrib);

				if (pmlmepriv->acm_mask != 0)
					pattrib->priority = qos_acm(pmlmepriv->acm_mask, pattrib->priority);
			}
		}
	}
	
	update_attrib_phy_info(padapter, pattrib, psta);

	/* RTW_INFO("%s ==> mac_id(%d)\n",__FUNCTION__,pattrib->mac_id ); */

	pattrib->psta = psta;
	/* TODO:_unlock */

#ifdef CONFIG_AUTO_AP_MODE
	if (psta->isrc && psta->pid > 0)
		pattrib->pctrl = _TRUE;
	else
#endif
		pattrib->pctrl = 0;

	pattrib->ack_policy = 0;

	if (bmcast)
		pattrib->rate = psta->init_rate;


#ifdef CONFIG_WMMPS_STA
	update_attrib_trigger_frame_info(padapter, pattrib);
#endif /* CONFIG_WMMPS_STA */	

	/* pattrib->priority = 5; */ /* force to used VI queue, for testing */
	pattrib->hw_ssn_sel = pxmitpriv->hw_ssn_seq_no;
	rtw_set_tx_chksum_offload(pkt, pattrib);

exit:


	return res;
}

static s32 xmitframe_addmic(_adapter *padapter, struct xmit_frame *pxmitframe)
{
	sint			curfragnum, length;
	u8	*pframe, *payload, mic[8];
	struct	mic_data		micdata;
	/* struct	sta_info		*stainfo; */
	struct	pkt_attrib	*pattrib = &pxmitframe->attrib;
	struct	security_priv	*psecuritypriv = &padapter->securitypriv;
	struct	xmit_priv		*pxmitpriv = &padapter->xmitpriv;
	u8 priority[4] = {0x0, 0x0, 0x0, 0x0};
	u8 hw_hdr_offset = 0;
	sint bmcst = IS_MCAST(pattrib->ra);

	/*
		if(pattrib->psta)
		{
			stainfo = pattrib->psta;
		}
		else
		{
			RTW_INFO("%s, call rtw_get_stainfo()\n", __func__);
			stainfo=rtw_get_stainfo(&padapter->stapriv ,&pattrib->ra[0]);
		}

		if(stainfo==NULL)
		{
			RTW_INFO("%s, psta==NUL\n", __func__);
			return _FAIL;
		}

		if(!(stainfo->state &_FW_LINKED))
		{
			RTW_INFO("%s, psta->state(0x%x) != _FW_LINKED\n", __func__, stainfo->state);
			return _FAIL;
		}
	*/


#ifdef CONFIG_USB_TX_AGGREGATION
	hw_hdr_offset = TXDESC_SIZE + (pxmitframe->pkt_offset * PACKET_OFFSET_SZ);;
#else
#ifdef CONFIG_TX_EARLY_MODE
	hw_hdr_offset = TXDESC_OFFSET + EARLY_MODE_INFO_SIZE;
#else
	hw_hdr_offset = TXDESC_OFFSET;
#endif
#endif

	if (pattrib->encrypt == _TKIP_) { /* if(psecuritypriv->dot11PrivacyAlgrthm==_TKIP_PRIVACY_) */
		/* encode mic code */
		/* if(stainfo!= NULL) */
		{
			u8 null_key[16] = {0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0};

			pframe = pxmitframe->buf_addr + hw_hdr_offset;

			if (bmcst) {
				if (_rtw_memcmp(psecuritypriv->dot118021XGrptxmickey[psecuritypriv->dot118021XGrpKeyid].skey, null_key, 16) == _TRUE) {
					/* DbgPrint("\nxmitframe_addmic:stainfo->dot11tkiptxmickey==0\n"); */
					/* rtw_msleep_os(10); */
					return _FAIL;
				}
				/* start to calculate the mic code */
				rtw_secmicsetkey(&micdata, psecuritypriv->dot118021XGrptxmickey[psecuritypriv->dot118021XGrpKeyid].skey);
			} else {
				if (_rtw_memcmp(&pattrib->dot11tkiptxmickey.skey[0], null_key, 16) == _TRUE) {
					/* DbgPrint("\nxmitframe_addmic:stainfo->dot11tkiptxmickey==0\n"); */
					/* rtw_msleep_os(10); */
					return _FAIL;
				}
				/* start to calculate the mic code */
				rtw_secmicsetkey(&micdata, &pattrib->dot11tkiptxmickey.skey[0]);
			}

			if (pframe[1] & 1) { /* ToDS==1 */
				rtw_secmicappend(&micdata, &pframe[16], 6);  /* DA */
				if (pframe[1] & 2) /* From Ds==1 */
					rtw_secmicappend(&micdata, &pframe[24], 6);
				else
					rtw_secmicappend(&micdata, &pframe[10], 6);
			} else {	/* ToDS==0 */
				rtw_secmicappend(&micdata, &pframe[4], 6);   /* DA */
				if (pframe[1] & 2) /* From Ds==1 */
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

				payload = payload + pattrib->hdrlen + pattrib->iv_len;
				if ((curfragnum + 1) == pattrib->nr_frags) {
					length = pattrib->last_txcmdsz - pattrib->hdrlen - pattrib->iv_len - ((pattrib->bswenc) ? pattrib->icv_len : 0);
					rtw_secmicappend(&micdata, payload, length);
					payload = payload + length;
				} else {
					length = pxmitpriv->frag_len - pattrib->hdrlen - pattrib->iv_len - ((pattrib->bswenc) ? pattrib->icv_len : 0);
					rtw_secmicappend(&micdata, payload, length);
					payload = payload + length + pattrib->icv_len;
				}
			}
			rtw_secgetmic(&micdata, &(mic[0]));
			/* add mic code  and add the mic code length in last_txcmdsz */

			_rtw_memcpy(payload, &(mic[0]), 8);
			pattrib->last_txcmdsz += 8;

			payload = payload - pattrib->last_txcmdsz + 8;
		}
	}


	return _SUCCESS;
}

/*#define DBG_TX_SW_ENCRYPTOR*/

static s32 xmitframe_swencrypt(_adapter *padapter, struct xmit_frame *pxmitframe)
{

	struct	pkt_attrib	*pattrib = &pxmitframe->attrib;
	/* struct 	security_priv	*psecuritypriv=&padapter->securitypriv; */


	/* if((psecuritypriv->sw_encrypt)||(pattrib->bswenc))	 */
	if (pattrib->bswenc) {
#ifdef DBG_TX_SW_ENCRYPTOR
		RTW_INFO(ADPT_FMT" - sec_type:%s DO SW encryption\n",
			ADPT_ARG(padapter), security_type_str(pattrib->encrypt));
#endif

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
#ifdef CONFIG_WAPI_SUPPORT
		case _SMS4_:
			rtw_sms4_encrypt(padapter, (u8 *)pxmitframe);
#endif
		default:
			break;
		}

	}


	return _SUCCESS;
}

s32 rtw_make_wlanhdr(_adapter *padapter , u8 *hdr, struct pkt_attrib *pattrib)
{
	u16 *qc;

	struct rtw_ieee80211_hdr *pwlanhdr = (struct rtw_ieee80211_hdr *)hdr;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct qos_priv *pqospriv = &pmlmepriv->qospriv;
	u8 qos_option = _FALSE;
	sint res = _SUCCESS;
	u16 *fctrl = &pwlanhdr->frame_ctl;

	/* struct sta_info *psta; */

	/* sint bmcst = IS_MCAST(pattrib->ra); */


	/*
		psta = rtw_get_stainfo(&padapter->stapriv, pattrib->ra);
		if(pattrib->psta != psta)
		{
			RTW_INFO("%s, pattrib->psta(%p) != psta(%p)\n", __func__, pattrib->psta, psta);
			return;
		}

		if(psta==NULL)
		{
			RTW_INFO("%s, psta==NUL\n", __func__);
			return _FAIL;
		}

		if(!(psta->state &_FW_LINKED))
		{
			RTW_INFO("%s, psta->state(0x%x) != _FW_LINKED\n", __func__, psta->state);
			return _FAIL;
		}
	*/

	_rtw_memset(hdr, 0, WLANHDR_OFFSET);

	set_frame_sub_type(fctrl, pattrib->subtype);

	if (pattrib->subtype & WIFI_DATA_TYPE) {
		if ((check_fwstate(pmlmepriv,  WIFI_STATION_STATE) == _TRUE)) {
#ifdef CONFIG_TDLS
			if (pattrib->direct_link == _TRUE) {
				/* TDLS data transfer, ToDS=0, FrDs=0 */
				_rtw_memcpy(pwlanhdr->addr1, pattrib->dst, ETH_ALEN);
				_rtw_memcpy(pwlanhdr->addr2, pattrib->src, ETH_ALEN);
				_rtw_memcpy(pwlanhdr->addr3, get_bssid(pmlmepriv), ETH_ALEN);

				if (pattrib->qos_en)
					qos_option = _TRUE;
			} else
#endif /* CONFIG_TDLS */
			{
				/* to_ds = 1, fr_ds = 0; */
				/* 1.Data transfer to AP */
				/* 2.Arp pkt will relayed by AP */
				SetToDs(fctrl);
				_rtw_memcpy(pwlanhdr->addr1, get_bssid(pmlmepriv), ETH_ALEN);
				_rtw_memcpy(pwlanhdr->addr2, pattrib->ta, ETH_ALEN);
				_rtw_memcpy(pwlanhdr->addr3, pattrib->dst, ETH_ALEN);

				if (pqospriv->qos_option)
					qos_option = _TRUE;
			}
		} else if ((check_fwstate(pmlmepriv,  WIFI_AP_STATE) == _TRUE)) {
			/* to_ds = 0, fr_ds = 1; */
			SetFrDs(fctrl);
			_rtw_memcpy(pwlanhdr->addr1, pattrib->dst, ETH_ALEN);
			_rtw_memcpy(pwlanhdr->addr2, get_bssid(pmlmepriv), ETH_ALEN);
			_rtw_memcpy(pwlanhdr->addr3, pattrib->src, ETH_ALEN);

			if (pattrib->qos_en)
				qos_option = _TRUE;
		} else if ((check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == _TRUE) ||
			(check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == _TRUE)) {
			_rtw_memcpy(pwlanhdr->addr1, pattrib->dst, ETH_ALEN);
			_rtw_memcpy(pwlanhdr->addr2, pattrib->ta, ETH_ALEN);
			_rtw_memcpy(pwlanhdr->addr3, get_bssid(pmlmepriv), ETH_ALEN);

			if (pattrib->qos_en)
				qos_option = _TRUE;
#ifdef CONFIG_RTW_MESH
		} else if (check_fwstate(pmlmepriv, WIFI_MESH_STATE) == _TRUE) {
			rtw_mesh_tx_build_whdr(padapter, pattrib, fctrl, pwlanhdr);
			if (pattrib->qos_en)
				qos_option = _TRUE;
			else {
				RTW_WARN("[%s] !qos_en in Mesh\n", __FUNCTION__);
				res = _FAIL;
				goto exit;
			}
#endif
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

			if(pattrib->amsdu)
				SetAMsdu(qc, pattrib->amsdu);
#ifdef CONFIG_RTW_MESH
			if (MLME_IS_MESH(padapter)) {
				/* active: don't care, light sleep: 0, deep sleep: 1*/
				set_mps_lv(qc, 0); //TBD

				/* TBD: temporary set (rspi, eosp) = (0, 1) which means End MPSP */
				set_rspi(qc, 0);
				SetEOSP(qc, 1);
				
				set_mctrl_present(qc, 1);
			}
#endif
		}

		/* TODO: fill HT Control Field */

		/* Update Seq Num will be handled by f/w */
		{
			struct sta_info *psta;
			psta = rtw_get_stainfo(&padapter->stapriv, pattrib->ra);
			if (pattrib->psta != psta) {
				RTW_INFO("%s, pattrib->psta(%p) != psta(%p)\n", __func__, pattrib->psta, psta);
				return _FAIL;
			}

			if (psta == NULL) {
				RTW_INFO("%s, psta==NUL\n", __func__);
				return _FAIL;
			}

			if (!(psta->state & _FW_LINKED)) {
				RTW_INFO("%s, psta->state(0x%x) != _FW_LINKED\n", __func__, psta->state);
				return _FAIL;
			}


			if (psta) {
				psta->sta_xmitpriv.txseq_tid[pattrib->priority]++;
				psta->sta_xmitpriv.txseq_tid[pattrib->priority] &= 0xFFF;
				pattrib->seqnum = psta->sta_xmitpriv.txseq_tid[pattrib->priority];

				SetSeqNum(hdr, pattrib->seqnum);

#ifdef CONFIG_80211N_HT
#if 0 /* move into update_attrib_phy_info(). */
				/* check if enable ampdu */
				if (pattrib->ht_en && psta->htpriv.ampdu_enable) {
					if (psta->htpriv.agg_enable_bitmap & BIT(pattrib->priority))
						pattrib->ampdu_en = _TRUE;
				}
#endif
				/* re-check if enable ampdu by BA_starting_seqctrl */
				if (pattrib->ampdu_en == _TRUE) {
					u16 tx_seq;

					tx_seq = psta->BA_starting_seqctrl[pattrib->priority & 0x0f];

					/* check BA_starting_seqctrl */
					if (SN_LESS(pattrib->seqnum, tx_seq)) {
						/* RTW_INFO("tx ampdu seqnum(%d) < tx_seq(%d)\n", pattrib->seqnum, tx_seq); */
						pattrib->ampdu_en = _FALSE;/* AGG BK */
					} else if (SN_EQUAL(pattrib->seqnum, tx_seq)) {
						psta->BA_starting_seqctrl[pattrib->priority & 0x0f] = (tx_seq + 1) & 0xfff;

						pattrib->ampdu_en = _TRUE;/* AGG EN */
					} else {
						/* RTW_INFO("tx ampdu over run\n"); */
						psta->BA_starting_seqctrl[pattrib->priority & 0x0f] = (pattrib->seqnum + 1) & 0xfff;
						pattrib->ampdu_en = _TRUE;/* AGG EN */
					}

				}
#endif /* CONFIG_80211N_HT */
			}
		}

	} else {

	}

exit:


	return res;
}

s32 rtw_txframes_pending(_adapter *padapter)
{
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;

	return ((_rtw_queue_empty(&pxmitpriv->be_pending) == _FALSE) ||
		(_rtw_queue_empty(&pxmitpriv->bk_pending) == _FALSE) ||
		(_rtw_queue_empty(&pxmitpriv->vi_pending) == _FALSE) ||
		(_rtw_queue_empty(&pxmitpriv->vo_pending) == _FALSE));
}

s32 rtw_txframes_sta_ac_pending(_adapter *padapter, struct pkt_attrib *pattrib)
{
	struct sta_info *psta;
	struct tx_servq *ptxservq;
	int priority = pattrib->priority;
	/*
		if(pattrib->psta)
		{
			psta = pattrib->psta;
		}
		else
		{
			RTW_INFO("%s, call rtw_get_stainfo()\n", __func__);
			psta=rtw_get_stainfo(&padapter->stapriv ,&pattrib->ra[0]);
		}
	*/
	psta = rtw_get_stainfo(&padapter->stapriv, pattrib->ra);
	if (pattrib->psta != psta) {
		RTW_INFO("%s, pattrib->psta(%p) != psta(%p)\n", __func__, pattrib->psta, psta);
		return 0;
	}

	if (psta == NULL) {
		RTW_INFO("%s, psta==NUL\n", __func__);
		return 0;
	}

	if (!(psta->state & _FW_LINKED)) {
		RTW_INFO("%s, psta->state(0x%x) != _FW_LINKED\n", __func__, psta->state);
		return 0;
	}

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

	return ptxservq->qcnt;
}

#ifdef CONFIG_TDLS

int rtw_build_tdls_ies(_adapter *padapter, struct xmit_frame *pxmitframe, u8 *pframe, struct tdls_txmgmt *ptxmgmt)
{
	struct pkt_attrib *pattrib = &pxmitframe->attrib;
	struct sta_info *ptdls_sta = NULL;
	int res = _SUCCESS;

	ptdls_sta = rtw_get_stainfo((&padapter->stapriv), pattrib->dst);
	if (ptdls_sta == NULL) {
		switch (ptxmgmt->action_code) {
		case TDLS_DISCOVERY_REQUEST:
		case TUNNELED_PROBE_REQ:
		case TUNNELED_PROBE_RSP:
			break;
		default:
			RTW_INFO("[TDLS] %s - Direct Link Peer = "MAC_FMT" not found for action = %d\n", __func__, MAC_ARG(pattrib->dst), ptxmgmt->action_code);
			res = _FAIL;
			goto exit;
		}
	}

	switch (ptxmgmt->action_code) {
	case TDLS_SETUP_REQUEST:
		rtw_build_tdls_setup_req_ies(padapter, pxmitframe, pframe, ptxmgmt, ptdls_sta);
		break;
	case TDLS_SETUP_RESPONSE:
		rtw_build_tdls_setup_rsp_ies(padapter, pxmitframe, pframe, ptxmgmt, ptdls_sta);
		break;
	case TDLS_SETUP_CONFIRM:
		rtw_build_tdls_setup_cfm_ies(padapter, pxmitframe, pframe, ptxmgmt, ptdls_sta);
		break;
	case TDLS_TEARDOWN:
		rtw_build_tdls_teardown_ies(padapter, pxmitframe, pframe, ptxmgmt, ptdls_sta);
		break;
	case TDLS_DISCOVERY_REQUEST:
		rtw_build_tdls_dis_req_ies(padapter, pxmitframe, pframe, ptxmgmt);
		break;
	case TDLS_PEER_TRAFFIC_INDICATION:
		rtw_build_tdls_peer_traffic_indication_ies(padapter, pxmitframe, pframe, ptxmgmt, ptdls_sta);
		break;
#ifdef CONFIG_TDLS_CH_SW
	case TDLS_CHANNEL_SWITCH_REQUEST:
		rtw_build_tdls_ch_switch_req_ies(padapter, pxmitframe, pframe, ptxmgmt, ptdls_sta);
		break;
	case TDLS_CHANNEL_SWITCH_RESPONSE:
		rtw_build_tdls_ch_switch_rsp_ies(padapter, pxmitframe, pframe, ptxmgmt, ptdls_sta);
		break;
#endif
	case TDLS_PEER_TRAFFIC_RESPONSE:
		rtw_build_tdls_peer_traffic_rsp_ies(padapter, pxmitframe, pframe, ptxmgmt, ptdls_sta);
		break;
#ifdef CONFIG_WFD
	case TUNNELED_PROBE_REQ:
		rtw_build_tunneled_probe_req_ies(padapter, pxmitframe, pframe);
		break;
	case TUNNELED_PROBE_RSP:
		rtw_build_tunneled_probe_rsp_ies(padapter, pxmitframe, pframe);
		break;
#endif /* CONFIG_WFD */
	default:
		res = _FAIL;
		break;
	}

exit:
	return res;
}

s32 rtw_make_tdls_wlanhdr(_adapter *padapter , u8 *hdr, struct pkt_attrib *pattrib, struct tdls_txmgmt *ptxmgmt)
{
	u16 *qc;
	struct rtw_ieee80211_hdr *pwlanhdr = (struct rtw_ieee80211_hdr *)hdr;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct qos_priv *pqospriv = &pmlmepriv->qospriv;
	struct sta_priv	*pstapriv = &padapter->stapriv;
	struct sta_info *psta = NULL, *ptdls_sta = NULL;
	u8 tdls_seq = 0, baddr[ETH_ALEN] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

	sint res = _SUCCESS;
	u16 *fctrl = &pwlanhdr->frame_ctl;


	_rtw_memset(hdr, 0, WLANHDR_OFFSET);

	set_frame_sub_type(fctrl, pattrib->subtype);

	switch (ptxmgmt->action_code) {
	case TDLS_SETUP_REQUEST:
	case TDLS_SETUP_RESPONSE:
	case TDLS_SETUP_CONFIRM:
	case TDLS_PEER_TRAFFIC_INDICATION:
	case TDLS_PEER_PSM_REQUEST:
	case TUNNELED_PROBE_REQ:
	case TUNNELED_PROBE_RSP:
	case TDLS_DISCOVERY_REQUEST:
		SetToDs(fctrl);
		_rtw_memcpy(pwlanhdr->addr1, get_bssid(pmlmepriv), ETH_ALEN);
		_rtw_memcpy(pwlanhdr->addr2, pattrib->src, ETH_ALEN);
		_rtw_memcpy(pwlanhdr->addr3, pattrib->dst, ETH_ALEN);
		break;
	case TDLS_CHANNEL_SWITCH_REQUEST:
	case TDLS_CHANNEL_SWITCH_RESPONSE:
	case TDLS_PEER_PSM_RESPONSE:
	case TDLS_PEER_TRAFFIC_RESPONSE:
		_rtw_memcpy(pwlanhdr->addr1, pattrib->dst, ETH_ALEN);
		_rtw_memcpy(pwlanhdr->addr2, pattrib->src, ETH_ALEN);
		_rtw_memcpy(pwlanhdr->addr3, get_bssid(pmlmepriv), ETH_ALEN);
		tdls_seq = 1;
		break;
	case TDLS_TEARDOWN:
		if (ptxmgmt->status_code == _RSON_TDLS_TEAR_UN_RSN_) {
			_rtw_memcpy(pwlanhdr->addr1, pattrib->dst, ETH_ALEN);
			_rtw_memcpy(pwlanhdr->addr2, pattrib->src, ETH_ALEN);
			_rtw_memcpy(pwlanhdr->addr3, get_bssid(pmlmepriv), ETH_ALEN);
			tdls_seq = 1;
		} else {
			SetToDs(fctrl);
			_rtw_memcpy(pwlanhdr->addr1, get_bssid(pmlmepriv), ETH_ALEN);
			_rtw_memcpy(pwlanhdr->addr2, pattrib->src, ETH_ALEN);
			_rtw_memcpy(pwlanhdr->addr3, pattrib->dst, ETH_ALEN);
		}
		break;
	}

	if (pattrib->encrypt)
		SetPrivacy(fctrl);

	if (ptxmgmt->action_code == TDLS_PEER_TRAFFIC_RESPONSE)
		SetPwrMgt(fctrl);

	if (pqospriv->qos_option) {
		qc = (unsigned short *)(hdr + pattrib->hdrlen - 2);
		if (pattrib->priority)
			SetPriority(qc, pattrib->priority);
		SetAckpolicy(qc, pattrib->ack_policy);
	}

	psta = pattrib->psta;

	/* 1. update seq_num per link by sta_info */
	/* 2. rewrite encrypt to _AES_, also rewrite iv_len, icv_len */
	if (tdls_seq == 1) {
		ptdls_sta = rtw_get_stainfo(pstapriv, pattrib->dst);
		if (ptdls_sta) {
			ptdls_sta->sta_xmitpriv.txseq_tid[pattrib->priority]++;
			ptdls_sta->sta_xmitpriv.txseq_tid[pattrib->priority] &= 0xFFF;
			pattrib->seqnum = ptdls_sta->sta_xmitpriv.txseq_tid[pattrib->priority];
			SetSeqNum(hdr, pattrib->seqnum);

			if (pattrib->encrypt) {
				pattrib->encrypt = _AES_;
				pattrib->iv_len = 8;
				pattrib->icv_len = 8;
				pattrib->bswenc = _FALSE;
			}
			pattrib->mac_id = ptdls_sta->cmn.mac_id;
		} else {
			res = _FAIL;
			goto exit;
		}
	} else if (psta) {
		psta->sta_xmitpriv.txseq_tid[pattrib->priority]++;
		psta->sta_xmitpriv.txseq_tid[pattrib->priority] &= 0xFFF;
		pattrib->seqnum = psta->sta_xmitpriv.txseq_tid[pattrib->priority];
		SetSeqNum(hdr, pattrib->seqnum);
	}


exit:


	return res;
}

s32 rtw_xmit_tdls_coalesce(_adapter *padapter, struct xmit_frame *pxmitframe, struct tdls_txmgmt *ptxmgmt)
{
	s32 llc_sz;

	u8 *pframe, *mem_start;

	struct sta_info		*psta;
	struct sta_priv		*pstapriv = &padapter->stapriv;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct pkt_attrib	*pattrib = &pxmitframe->attrib;
	u8 *pbuf_start;
	s32 bmcst = IS_MCAST(pattrib->ra);
	s32 res = _SUCCESS;


	if (pattrib->psta)
		psta = pattrib->psta;
	else {
		if (bmcst)
			psta = rtw_get_bcmc_stainfo(padapter);
		else
			psta = rtw_get_stainfo(&padapter->stapriv, pattrib->ra);
	}

	if (psta == NULL) {
		res = _FAIL;
		goto exit;
	}

	if (pxmitframe->buf_addr == NULL) {
		res = _FAIL;
		goto exit;
	}

	pbuf_start = pxmitframe->buf_addr;
	mem_start = pbuf_start + TXDESC_OFFSET;

	if (rtw_make_tdls_wlanhdr(padapter, mem_start, pattrib, ptxmgmt) == _FAIL) {
		res = _FAIL;
		goto exit;
	}

	pframe = mem_start;
	pframe += pattrib->hdrlen;

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

		_rtw_memcpy(pframe, pattrib->iv, pattrib->iv_len);
		pframe += pattrib->iv_len;

	}

	llc_sz = rtw_put_snap(pframe, pattrib->ether_type);
	pframe += llc_sz;

	/* pattrib->pktlen will be counted in rtw_build_tdls_ies */
	pattrib->pktlen = 0;

	rtw_build_tdls_ies(padapter, pxmitframe, pframe, ptxmgmt);

	if ((pattrib->icv_len > 0) && (pattrib->bswenc)) {
		pframe += pattrib->pktlen;
		_rtw_memcpy(pframe, pattrib->icv, pattrib->icv_len);
		pframe += pattrib->icv_len;
	}

	pattrib->nr_frags = 1;
	pattrib->last_txcmdsz = pattrib->hdrlen + pattrib->iv_len + llc_sz +
		((pattrib->bswenc) ? pattrib->icv_len : 0) + pattrib->pktlen;

	if (xmitframe_addmic(padapter, pxmitframe) == _FAIL) {
		res = _FAIL;
		goto exit;
	}

	xmitframe_swencrypt(padapter, pxmitframe);

	update_attrib_vcs_info(padapter, pxmitframe);

exit:


	return res;
}
#endif /* CONFIG_TDLS */

/*
 * Calculate wlan 802.11 packet MAX size from pkt_attrib
 * This function doesn't consider fragment case
 */
u32 rtw_calculate_wlan_pkt_size_by_attribue(struct pkt_attrib *pattrib)
{
	u32	len = 0;

	len = pattrib->hdrlen /* WLAN Header */
		+ pattrib->iv_len /* IV */
		+ XATTRIB_GET_MCTRL_LEN(pattrib)
		+ SNAP_SIZE + sizeof(u16) /* LLC */
		+ pattrib->pktlen
		+ (pattrib->encrypt == _TKIP_ ? 8 : 0) /* MIC */
		+ (pattrib->bswenc ? pattrib->icv_len : 0) /* ICV */
		;

	return len;
}

#ifdef CONFIG_TX_AMSDU
s32 check_amsdu(struct xmit_frame *pxmitframe)
{
	struct pkt_attrib *pattrib;
	s32 ret = _TRUE;

	if (!pxmitframe)
		ret = _FALSE;

	pattrib = &pxmitframe->attrib;

	if (IS_MCAST(pattrib->ra))
		ret = _FALSE;

	if ((pattrib->ether_type == 0x888e) ||
		(pattrib->ether_type == 0x0806) ||
		(pattrib->ether_type == 0x88b4) ||
		(pattrib->dhcp_pkt == 1))
		ret = _FALSE;

	if ((pattrib->encrypt == _WEP40_) ||
	    (pattrib->encrypt == _WEP104_) ||
	    (pattrib->encrypt == _TKIP_))
		ret = _FALSE;

	if (!pattrib->qos_en)
		ret = _FALSE;

	if (IS_AMSDU_AMPDU_NOT_VALID(pattrib))
		ret = _FALSE;

	return ret;
}

s32 check_amsdu_tx_support(_adapter *padapter)
{
	struct dvobj_priv *pdvobjpriv;
	int tx_amsdu;
	int tx_amsdu_rate;
	int current_tx_rate;
	s32 ret = _FALSE;

	pdvobjpriv = adapter_to_dvobj(padapter);
	tx_amsdu = padapter->tx_amsdu;
	tx_amsdu_rate = padapter->tx_amsdu_rate;
	current_tx_rate = pdvobjpriv->traffic_stat.cur_tx_tp;

	if (tx_amsdu == 1)
		ret = _TRUE;
	else if (tx_amsdu == 2 && (tx_amsdu_rate == 0 || current_tx_rate > tx_amsdu_rate))
		ret = _TRUE;
	else
		ret = _FALSE;

	return ret;
}

s32 rtw_xmitframe_coalesce_amsdu(_adapter *padapter, struct xmit_frame *pxmitframe, struct xmit_frame *pxmitframe_queue)
{

	struct pkt_file pktfile;
	struct pkt_attrib *pattrib;
	_pkt *pkt;

	struct pkt_file pktfile_queue;
	struct pkt_attrib *pattrib_queue;
	_pkt *pkt_queue;

	s32 llc_sz, mem_sz;

	s32 padding = 0;

	u8 *pframe, *mem_start;
	u8 hw_hdr_offset;

	u16* len;
	u8 *pbuf_start;
	s32 res = _SUCCESS;

	if (pxmitframe->buf_addr == NULL) {
		RTW_INFO("==> %s buf_addr==NULL\n", __FUNCTION__);
		return _FAIL;
	}


	pbuf_start = pxmitframe->buf_addr;

#ifdef CONFIG_USB_TX_AGGREGATION
	hw_hdr_offset =  TXDESC_SIZE + (pxmitframe->pkt_offset * PACKET_OFFSET_SZ);
#else
#ifdef CONFIG_TX_EARLY_MODE /* for SDIO && Tx Agg */
	hw_hdr_offset = TXDESC_OFFSET + EARLY_MODE_INFO_SIZE;
#else
	hw_hdr_offset = TXDESC_OFFSET;
#endif
#endif

	mem_start = pbuf_start + hw_hdr_offset; //for DMA

	pattrib = &pxmitframe->attrib;

	pattrib->amsdu = 1;

	if (rtw_make_wlanhdr(padapter, mem_start, pattrib) == _FAIL) {
		RTW_INFO("rtw_xmitframe_coalesce: rtw_make_wlanhdr fail; drop pkt\n");
		res = _FAIL;
		goto exit;
	}

	llc_sz = 0;

	pframe = mem_start;

	//SetMFrag(mem_start);
	ClearMFrag(mem_start);

	pframe += pattrib->hdrlen;

	/* adding icv, if necessary... */
	if (pattrib->iv_len) {
		_rtw_memcpy(pframe, pattrib->iv, pattrib->iv_len); // queue or new?

		RTW_DBG("rtw_xmitframe_coalesce: keyid=%d pattrib->iv[3]=%.2x pframe=%.2x %.2x %.2x %.2x\n",
			padapter->securitypriv.dot11PrivacyKeyIndex, pattrib->iv[3], *pframe, *(pframe + 1), *(pframe + 2), *(pframe + 3));

		pframe += pattrib->iv_len;
	}

	pattrib->last_txcmdsz = pattrib->hdrlen + pattrib->iv_len;

	if(pxmitframe_queue)
	{
		pattrib_queue = &pxmitframe_queue->attrib;
		pkt_queue = pxmitframe_queue->pkt;

		_rtw_open_pktfile(pkt_queue, &pktfile_queue);
		_rtw_pktfile_read(&pktfile_queue, NULL, pattrib_queue->pkt_hdrlen);

		#ifdef CONFIG_RTW_MESH
		if (MLME_IS_MESH(padapter)) {
			/* mDA(6), mSA(6), len(2), mctrl */
			_rtw_memcpy(pframe, pattrib_queue->mda, ETH_ALEN);
			pframe += ETH_ALEN;
			_rtw_memcpy(pframe, pattrib_queue->msa, ETH_ALEN);
			pframe += ETH_ALEN;
			len = (u16*)pframe;
			pframe += 2;
			rtw_mesh_tx_build_mctrl(padapter, pattrib_queue, pframe);
			pframe += XATTRIB_GET_MCTRL_LEN(pattrib_queue);
		} else
		#endif
		{
			/* 802.3 MAC Header DA(6)  SA(6)  Len(2)*/
			_rtw_memcpy(pframe, pattrib_queue->dst, ETH_ALEN);
			pframe += ETH_ALEN;
			_rtw_memcpy(pframe, pattrib_queue->src, ETH_ALEN);
			pframe += ETH_ALEN;
			len = (u16*)pframe;
			pframe += 2;
		}

		llc_sz = rtw_put_snap(pframe, pattrib_queue->ether_type);
		pframe += llc_sz;

		mem_sz = _rtw_pktfile_read(&pktfile_queue, pframe, pattrib_queue->pktlen);
		pframe += mem_sz;

		*len = htons(XATTRIB_GET_MCTRL_LEN(pattrib_queue) + llc_sz + mem_sz);

		//calc padding
		padding = 4 - ((ETH_HLEN + XATTRIB_GET_MCTRL_LEN(pattrib_queue) + llc_sz + mem_sz) & (4-1));
		if(padding == 4)
			padding = 0;

		//_rtw_memset(pframe,0xaa, padding);
		pframe += padding;

		pattrib->last_txcmdsz += ETH_HLEN + XATTRIB_GET_MCTRL_LEN(pattrib_queue) + llc_sz + mem_sz + padding ;
	}

	//2nd mpdu

	pkt = pxmitframe->pkt;
	_rtw_open_pktfile(pkt, &pktfile);
	_rtw_pktfile_read(&pktfile, NULL, pattrib->pkt_hdrlen);

#ifdef CONFIG_RTW_MESH
	if (MLME_IS_MESH(padapter)) {
		/* mDA(6), mSA(6), len(2), mctrl */
		_rtw_memcpy(pframe, pattrib->mda, ETH_ALEN);
		pframe += ETH_ALEN;
		_rtw_memcpy(pframe, pattrib->msa, ETH_ALEN);
		pframe += ETH_ALEN;
		len = (u16*)pframe;
		pframe += 2;
		rtw_mesh_tx_build_mctrl(padapter, pattrib, pframe);
		pframe += XATTRIB_GET_MCTRL_LEN(pattrib);
	} else
#endif
	{
		/* 802.3 MAC Header  DA(6)  SA(6)  Len(2) */
		_rtw_memcpy(pframe, pattrib->dst, ETH_ALEN);
		pframe += ETH_ALEN;
		_rtw_memcpy(pframe, pattrib->src, ETH_ALEN);
		pframe += ETH_ALEN;
		len = (u16*)pframe;
		pframe += 2;
	}

	llc_sz = rtw_put_snap(pframe, pattrib->ether_type);
	pframe += llc_sz;

	mem_sz = _rtw_pktfile_read(&pktfile, pframe, pattrib->pktlen);

	pframe += mem_sz;

	*len = htons(XATTRIB_GET_MCTRL_LEN(pattrib) + llc_sz + mem_sz);

	//the last ampdu has no padding
	padding = 0;

	pattrib->nr_frags = 1;

	pattrib->last_txcmdsz += ETH_HLEN + XATTRIB_GET_MCTRL_LEN(pattrib) + llc_sz + mem_sz + padding +
		((pattrib->bswenc) ? pattrib->icv_len : 0) ;

	if ((pattrib->icv_len > 0) && (pattrib->bswenc)) {
		_rtw_memcpy(pframe, pattrib->icv, pattrib->icv_len);
		pframe += pattrib->icv_len;
	}

	if (xmitframe_addmic(padapter, pxmitframe) == _FAIL) {
		RTW_INFO("xmitframe_addmic(padapter, pxmitframe)==_FAIL\n");
		res = _FAIL;
		goto exit;
	}

	xmitframe_swencrypt(padapter, pxmitframe);

	update_attrib_vcs_info(padapter, pxmitframe);

exit:
	return res;
}
#endif /* CONFIG_TX_AMSDU */

/*

This sub-routine will perform all the following:

1. remove 802.3 header.
2. create wlan_header, based on the info in pxmitframe
3. append sta's iv/ext-iv
4. append LLC
5. move frag chunk from pframe to pxmitframe->mem
6. apply sw-encrypt, if necessary.

*/
s32 rtw_xmitframe_coalesce(_adapter *padapter, _pkt *pkt, struct xmit_frame *pxmitframe)
{
	struct pkt_file pktfile;

	s32 frg_inx, frg_len, mpdu_len, llc_sz, mem_sz;

	SIZE_PTR addr;

	u8 *pframe, *mem_start;
	u8 hw_hdr_offset;

	/* struct sta_info		*psta; */
	/* struct sta_priv		*pstapriv = &padapter->stapriv; */
	/* struct mlme_priv	*pmlmepriv = &padapter->mlmepriv; */
	struct xmit_priv	*pxmitpriv = &padapter->xmitpriv;

	struct pkt_attrib	*pattrib = &pxmitframe->attrib;

	u8 *pbuf_start;

	s32 bmcst = IS_MCAST(pattrib->ra);
	s32 res = _SUCCESS;


	/*
		if (pattrib->psta)
		{
			psta = pattrib->psta;
		} else
		{
			RTW_INFO("%s, call rtw_get_stainfo()\n", __func__);
			psta = rtw_get_stainfo(&padapter->stapriv, pattrib->ra);
		}

		if(psta==NULL)
		{

			RTW_INFO("%s, psta==NUL\n", __func__);
			return _FAIL;
		}


		if(!(psta->state &_FW_LINKED))
		{
			RTW_INFO("%s, psta->state(0x%x) != _FW_LINKED\n", __func__, psta->state);
			return _FAIL;
		}
	*/
	if (pxmitframe->buf_addr == NULL) {
		RTW_INFO("==> %s buf_addr==NULL\n", __FUNCTION__);
		return _FAIL;
	}

	pbuf_start = pxmitframe->buf_addr;

#ifdef CONFIG_USB_TX_AGGREGATION
	hw_hdr_offset =  TXDESC_SIZE + (pxmitframe->pkt_offset * PACKET_OFFSET_SZ);
#else
#ifdef CONFIG_TX_EARLY_MODE /* for SDIO && Tx Agg */
	hw_hdr_offset = TXDESC_OFFSET + EARLY_MODE_INFO_SIZE;
#else
	hw_hdr_offset = TXDESC_OFFSET;
#endif
#endif

	mem_start = pbuf_start +	hw_hdr_offset;

	if (rtw_make_wlanhdr(padapter, mem_start, pattrib) == _FAIL) {
		RTW_INFO("rtw_xmitframe_coalesce: rtw_make_wlanhdr fail; drop pkt\n");
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
#if 0
			/* if (check_fwstate(pmlmepriv, WIFI_MP_STATE)) */
			/*	psta = rtw_get_stainfo(pstapriv, get_bssid(pmlmepriv)); */
			/* else */
			/*	psta = rtw_get_stainfo(pstapriv, pattrib->ra); */

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
#ifdef CONFIG_WAPI_SUPPORT
				case _SMS4_:
					rtw_wapi_get_iv(padapter, pattrib->ra, pattrib->iv);
					break;
#endif
				}
			}
#endif
			_rtw_memcpy(pframe, pattrib->iv, pattrib->iv_len);


			pframe += pattrib->iv_len;

			mpdu_len -= pattrib->iv_len;
		}

		if (frg_inx == 0) {
			#ifdef CONFIG_RTW_MESH
			if (MLME_IS_MESH(padapter)) {
				rtw_mesh_tx_build_mctrl(padapter, pattrib, pframe);
				pframe += XATTRIB_GET_MCTRL_LEN(pattrib);
				mpdu_len -= XATTRIB_GET_MCTRL_LEN(pattrib);
			}
			#endif

			llc_sz = rtw_put_snap(pframe, pattrib->ether_type);
			pframe += llc_sz;
			mpdu_len -= llc_sz;
		}

		if ((pattrib->icv_len > 0) && (pattrib->bswenc))
			mpdu_len -= pattrib->icv_len;


		if (bmcst) {
			/* don't do fragment to broadcat/multicast packets */
			mem_sz = _rtw_pktfile_read(&pktfile, pframe, pattrib->pktlen);
		} else
			mem_sz = _rtw_pktfile_read(&pktfile, pframe, mpdu_len);

		pframe += mem_sz;

		if ((pattrib->icv_len > 0) && (pattrib->bswenc)) {
			_rtw_memcpy(pframe, pattrib->icv, pattrib->icv_len);
			pframe += pattrib->icv_len;
		}

		frg_inx++;

		if (bmcst || (rtw_endofpktfile(&pktfile) == _TRUE)) {
			pattrib->nr_frags = frg_inx;

			pattrib->last_txcmdsz = pattrib->hdrlen + pattrib->iv_len +
				((pattrib->nr_frags == 1) ? (XATTRIB_GET_MCTRL_LEN(pattrib) + llc_sz) : 0) +
				((pattrib->bswenc) ? pattrib->icv_len : 0) + mem_sz;

			ClearMFrag(mem_start);

			break;
		}

		addr = (SIZE_PTR)(pframe);

		mem_start = (unsigned char *)RND4(addr) + hw_hdr_offset;
		_rtw_memcpy(mem_start, pbuf_start + hw_hdr_offset, pattrib->hdrlen);

	}

	if (xmitframe_addmic(padapter, pxmitframe) == _FAIL) {
		RTW_INFO("xmitframe_addmic(padapter, pxmitframe)==_FAIL\n");
		res = _FAIL;
		goto exit;
	}

	xmitframe_swencrypt(padapter, pxmitframe);

	if (bmcst == _FALSE)
		update_attrib_vcs_info(padapter, pxmitframe);
	else
		pattrib->vcs_mode = NONE_VCS;

exit:


	return res;
}

#if defined(CONFIG_IEEE80211W) || defined(CONFIG_RTW_MESH)
/*
 * CCMP encryption for unicast robust mgmt frame and broadcast group privicy action
 * BIP for broadcast robust mgmt frame
 */
s32 rtw_mgmt_xmitframe_coalesce(_adapter *padapter, _pkt *pkt, struct xmit_frame *pxmitframe)
{
#define DBG_MGMT_XMIT_COALESEC_DUMP 0
#define DBG_MGMT_XMIT_BIP_DUMP 0
#define DBG_MGMT_XMIT_ENC_DUMP 0

	struct pkt_file pktfile;
	s32 frg_inx, frg_len, mpdu_len, llc_sz, mem_sz;
	SIZE_PTR addr;
	u8 *pframe, *mem_start = NULL, *tmp_buf = NULL;
	u8 hw_hdr_offset, subtype ;
	u8 category = 0xFF;
	struct sta_info		*psta = NULL;
	struct xmit_priv	*pxmitpriv = &padapter->xmitpriv;
	struct pkt_attrib	*pattrib = &pxmitframe->attrib;
	u8 *pbuf_start;
	s32 bmcst = IS_MCAST(pattrib->ra);
	s32 res = _FAIL;
	u8 *BIP_AAD = NULL;
	u8 *MGMT_body = NULL;

	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct rtw_ieee80211_hdr	*pwlanhdr;
	u8 MME[_MME_IE_LENGTH_];

	_irqL irqL;
	u32	ori_len;
	union pn48 *pn = NULL;
	u8 kid;

	if (pxmitframe->buf_addr == NULL) {
		RTW_WARN(FUNC_ADPT_FMT" pxmitframe->buf_addr\n"
			, FUNC_ADPT_ARG(padapter));
		return _FAIL;
	}

	mem_start = pframe = (u8 *)(pxmitframe->buf_addr) + TXDESC_OFFSET;
	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;
	subtype = get_frame_sub_type(pframe); /* bit(7)~bit(2) */

	/* check if robust mgmt frame */
	if (subtype != WIFI_DEAUTH && subtype != WIFI_DISASSOC && subtype != WIFI_ACTION)
		return _SUCCESS;
	if (subtype == WIFI_ACTION) {
		category = *(pframe + sizeof(struct rtw_ieee80211_hdr_3addr));
		if (CATEGORY_IS_NON_ROBUST(category))
			return _SUCCESS;
	}
	if (!bmcst) {
		if (pattrib->psta)
			psta = pattrib->psta;
		else
			pattrib->psta = psta = rtw_get_stainfo(&padapter->stapriv, pattrib->ra);
		if (psta == NULL) {
			RTW_INFO(FUNC_ADPT_FMT" unicast sta == NULL\n", FUNC_ADPT_ARG(padapter));
			return _FAIL;
		}
		if (!(psta->flags & WLAN_STA_MFP)) {
			/* peer is not MFP capable, no need to encrypt */
			return _SUCCESS;
		}
		if (psta->bpairwise_key_installed != _TRUE) {
			RTW_INFO(FUNC_ADPT_FMT" PTK is not installed\n"
				, FUNC_ADPT_ARG(padapter));
			return _FAIL;
		}
	}

	ori_len = BIP_AAD_SIZE + pattrib->pktlen;
	tmp_buf = BIP_AAD = rtw_zmalloc(ori_len);
	if (BIP_AAD == NULL)
		return _FAIL;

	_enter_critical_bh(&padapter->security_key_mutex, &irqL);

	if (bmcst) {
		if (subtype == WIFI_ACTION && CATEGORY_IS_GROUP_PRIVACY(category)) {
			/* broadcast group privacy action frame */
			#if DBG_MGMT_XMIT_COALESEC_DUMP
			RTW_INFO(FUNC_ADPT_FMT" broadcast gp action(%u)\n"
				, FUNC_ADPT_ARG(padapter), category);
			#endif

			if (pattrib->psta)
				psta = pattrib->psta;
			else
				pattrib->psta = psta = rtw_get_bcmc_stainfo(padapter);
			if (psta == NULL) {
				RTW_INFO(FUNC_ADPT_FMT" broadcast sta == NULL\n"
					, FUNC_ADPT_ARG(padapter));
				goto xmitframe_coalesce_fail;
			}
			if (padapter->securitypriv.binstallGrpkey != _TRUE) {
				RTW_INFO(FUNC_ADPT_FMT" GTK is not installed\n"
					, FUNC_ADPT_ARG(padapter));
				goto xmitframe_coalesce_fail;
			}

			pn = &psta->dot11txpn;
			kid = padapter->securitypriv.dot118021XGrpKeyid;
		} else {
			#ifdef CONFIG_IEEE80211W
			/* broadcast robust mgmt frame, using BIP */
			int frame_body_len;
			u8 mic[16];

			/* IGTK key is not install ex: mesh MFP without IGTK */
			if (SEC_IS_BIP_KEY_INSTALLED(&padapter->securitypriv) != _TRUE)
				goto xmitframe_coalesce_success;

			#if DBG_MGMT_XMIT_COALESEC_DUMP
			if (subtype == WIFI_DEAUTH)
				RTW_INFO(FUNC_ADPT_FMT" braodcast deauth\n", FUNC_ADPT_ARG(padapter));
			else if (subtype == WIFI_DISASSOC)
				RTW_INFO(FUNC_ADPT_FMT" braodcast disassoc\n", FUNC_ADPT_ARG(padapter));
			else if (subtype == WIFI_ACTION) {
				RTW_INFO(FUNC_ADPT_FMT" braodcast action(%u)\n"
					, FUNC_ADPT_ARG(padapter), category);
			}
			#endif

			_rtw_memset(MME, 0, _MME_IE_LENGTH_);

			MGMT_body = pframe + sizeof(struct rtw_ieee80211_hdr_3addr);
			pframe += pattrib->pktlen;

			/* octent 0 and 1 is key index ,BIP keyid is 4 or 5, LSB only need octent 0 */
			MME[0] = padapter->securitypriv.dot11wBIPKeyid;
			/* increase PN and apply to packet */
			padapter->securitypriv.dot11wBIPtxpn.val++;
			RTW_PUT_LE64(&MME[2], padapter->securitypriv.dot11wBIPtxpn.val);

			/* add MME IE with MIC all zero, MME string doesn't include element id and length */
			pframe = rtw_set_ie(pframe, _MME_IE_ , 16 , MME, &(pattrib->pktlen));
			pattrib->last_txcmdsz = pattrib->pktlen;
			/* total frame length - header length */
			frame_body_len = pattrib->pktlen - sizeof(struct rtw_ieee80211_hdr_3addr);

			/* conscruct AAD, copy frame control field */
			_rtw_memcpy(BIP_AAD, &pwlanhdr->frame_ctl, 2);
			ClearRetry(BIP_AAD);
			ClearPwrMgt(BIP_AAD);
			ClearMData(BIP_AAD);
			/* conscruct AAD, copy address 1 to address 3 */
			_rtw_memcpy(BIP_AAD + 2, pwlanhdr->addr1, 18);
			/* copy management fram body */
			_rtw_memcpy(BIP_AAD + BIP_AAD_SIZE, MGMT_body, frame_body_len);

			#if DBG_MGMT_XMIT_BIP_DUMP
			/* dump total packet include MME with zero MIC */
			{
				int i;
				printk("Total packet: ");
				for (i = 0; i < BIP_AAD_SIZE + frame_body_len; i++)
					printk(" %02x ", BIP_AAD[i]);
				printk("\n");
			}
			#endif

			/* calculate mic */
			if (omac1_aes_128(padapter->securitypriv.dot11wBIPKey[padapter->securitypriv.dot11wBIPKeyid].skey
				  , BIP_AAD, BIP_AAD_SIZE + frame_body_len, mic))
				goto xmitframe_coalesce_fail;

			#if DBG_MGMT_XMIT_BIP_DUMP
			/* dump calculated mic result */
			{
				int i;
				printk("Calculated mic result: ");
				for (i = 0; i < 16; i++)
					printk(" %02x ", mic[i]);
				printk("\n");
			}
			#endif

			/* copy right BIP mic value, total is 128bits, we use the 0~63 bits */
			_rtw_memcpy(pframe - 8, mic, 8);

			#if DBG_MGMT_XMIT_BIP_DUMP
			/*dump all packet after mic ok */
			{
				int pp;
				printk("pattrib->pktlen = %d\n", pattrib->pktlen);
				for(pp=0;pp< pattrib->pktlen; pp++)
					printk(" %02x ", mem_start[pp]);
				printk("\n");
			}
			#endif

			#endif /* CONFIG_IEEE80211W */

			goto xmitframe_coalesce_success;
		}
	}
	else {
		/* unicast robust mgmt frame */
		#if DBG_MGMT_XMIT_COALESEC_DUMP
		if (subtype == WIFI_DEAUTH) {
			RTW_INFO(FUNC_ADPT_FMT" unicast deauth to "MAC_FMT"\n"
				, FUNC_ADPT_ARG(padapter), MAC_ARG(pattrib->ra));
		} else if (subtype == WIFI_DISASSOC) {
			RTW_INFO(FUNC_ADPT_FMT" unicast disassoc to "MAC_FMT"\n"
				, FUNC_ADPT_ARG(padapter), MAC_ARG(pattrib->ra));
		} else if (subtype == WIFI_ACTION) {
			RTW_INFO(FUNC_ADPT_FMT" unicast action(%u) to "MAC_FMT"\n"
				, FUNC_ADPT_ARG(padapter), category, MAC_ARG(pattrib->ra));
		}
		#endif

		_rtw_memcpy(pattrib->dot118021x_UncstKey.skey, psta->dot118021x_UncstKey.skey, 16);

		/* To use wrong key */
		if (pattrib->key_type == IEEE80211W_WRONG_KEY) {
			RTW_INFO("use wrong key\n");
			pattrib->dot118021x_UncstKey.skey[0] = 0xff;
		}

		pn = &psta->dot11txpn;
		kid = 0;
	}

	#if DBG_MGMT_XMIT_ENC_DUMP
	/* before encrypt dump the management packet content */
	{
		int i;
		printk("Management pkt: ");
		for(i=0; i<pattrib->pktlen; i++)
		printk(" %02x ", pframe[i]);
		printk("=======\n");
	}
	#endif

	/* bakeup original management packet */
	_rtw_memcpy(tmp_buf, pframe, pattrib->pktlen);
	/* move to data portion */
	pframe += pattrib->hdrlen;

	/* 802.11w encrypted management packet must be _AES_ */
	if (pattrib->key_type != IEEE80211W_NO_KEY) {
		pattrib->encrypt = _AES_;
		pattrib->bswenc = _TRUE;
	}

	pattrib->iv_len = 8;
	/* it's MIC of AES */
	pattrib->icv_len = 8;

	switch (pattrib->encrypt) {
	case _AES_:
		/* set AES IV header */
		AES_IV(pattrib->iv, (*pn), kid);
		break;
	default:
		goto xmitframe_coalesce_fail;
	}

	/* insert iv header into management frame */
	_rtw_memcpy(pframe, pattrib->iv, pattrib->iv_len);
	pframe += pattrib->iv_len;
	/* copy mgmt data portion after CCMP header */
	_rtw_memcpy(pframe, tmp_buf + pattrib->hdrlen, pattrib->pktlen - pattrib->hdrlen);
	/* move pframe to end of mgmt pkt */
	pframe += pattrib->pktlen - pattrib->hdrlen;
	/* add 8 bytes CCMP IV header to length */
	pattrib->pktlen += pattrib->iv_len;

	#if DBG_MGMT_XMIT_ENC_DUMP
	/* dump management packet include AES IV header */
	{
		int i;
		printk("Management pkt + IV: ");
		/* for(i=0; i<pattrib->pktlen; i++) */

		printk("@@@@@@@@@@@@@\n");
	}
	#endif

	if ((pattrib->icv_len > 0) && (pattrib->bswenc)) {
		_rtw_memcpy(pframe, pattrib->icv, pattrib->icv_len);
		pframe += pattrib->icv_len;
	}
	/* add 8 bytes MIC */
	pattrib->pktlen += pattrib->icv_len;
	/* set final tx command size */
	pattrib->last_txcmdsz = pattrib->pktlen;

	/* set protected bit must be beofre SW encrypt */
	SetPrivacy(mem_start);

	#if DBG_MGMT_XMIT_ENC_DUMP
	/* dump management packet include AES header */
	{
		int i;
		printk("prepare to enc Management pkt + IV: ");
		for (i = 0; i < pattrib->pktlen; i++)
			printk(" %02x ", mem_start[i]);
		printk("@@@@@@@@@@@@@\n");
	}
	#endif

	/* software encrypt */
	xmitframe_swencrypt(padapter, pxmitframe);

xmitframe_coalesce_success:
	_exit_critical_bh(&padapter->security_key_mutex, &irqL);
	rtw_mfree(BIP_AAD, ori_len);
	return _SUCCESS;

xmitframe_coalesce_fail:
	_exit_critical_bh(&padapter->security_key_mutex, &irqL);
	rtw_mfree(BIP_AAD, ori_len);

	return _FAIL;
}
#endif /* defined(CONFIG_IEEE80211W) || defined(CONFIG_RTW_MESH) */

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

	*(u16 *)(data + SNAP_SIZE) = htons(h_proto);


	return SNAP_SIZE + sizeof(u16);
}

void rtw_update_protection(_adapter *padapter, u8 *ie, uint ie_len)
{

	uint	protection;
	u8	*perp;
	sint	 erp_len;
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
		if (perp == NULL)
			pxmitpriv->vcs = NONE_VCS;
		else {
			protection = (*(perp + 2)) & BIT(1);
			if (protection) {
				if (pregistrypriv->vcs_type == RTS_CTS)
					pxmitpriv->vcs = RTS_CTS;
				else
					pxmitpriv->vcs = CTS_TO_SELF;
			} else
				pxmitpriv->vcs = NONE_VCS;
		}

		break;

	}


}

void rtw_count_tx_stats(PADAPTER padapter, struct xmit_frame *pxmitframe, int sz)
{
	struct sta_info *psta = NULL;
	struct stainfo_stats *pstats = NULL;
	struct xmit_priv	*pxmitpriv = &padapter->xmitpriv;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	u8	pkt_num = 1;

	if ((pxmitframe->frame_tag & 0x0f) == DATA_FRAMETAG) {
#if defined(CONFIG_USB_TX_AGGREGATION) || defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
		pkt_num = pxmitframe->agg_num;
#endif
		pmlmepriv->LinkDetectInfo.NumTxOkInPeriod += pkt_num;

		pxmitpriv->tx_pkts += pkt_num;

		pxmitpriv->tx_bytes += sz;

		psta = pxmitframe->attrib.psta;
		if (psta) {
			pstats = &psta->sta_stats;

			pstats->tx_pkts += pkt_num;

			pstats->tx_bytes += sz;
			#if defined(CONFIG_CHECK_LEAVE_LPS) && defined(CONFIG_LPS_CHK_BY_TP)
			if (adapter_to_pwrctl(padapter)->lps_chk_by_tp)
				traffic_check_for_leave_lps_by_tp(padapter, _TRUE, psta);
			#endif /* CONFIG_LPS */
		}

#ifdef CONFIG_CHECK_LEAVE_LPS
		/* traffic_check_for_leave_lps(padapter, _TRUE); */
#endif /* CONFIG_CHECK_LEAVE_LPS */

	}
}

static struct xmit_buf *__rtw_alloc_cmd_xmitbuf(struct xmit_priv *pxmitpriv,
		enum cmdbuf_type buf_type)
{
	struct xmit_buf *pxmitbuf =  NULL;


	pxmitbuf = &pxmitpriv->pcmd_xmitbuf[buf_type];
	if (pxmitbuf !=  NULL) {
		pxmitbuf->priv_data = NULL;

#if defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
		pxmitbuf->len = 0;
		pxmitbuf->pdata = pxmitbuf->ptail = pxmitbuf->phead;
		pxmitbuf->agg_num = 0;
		pxmitbuf->pg_num = 0;
#endif
#ifdef CONFIG_PCI_HCI
		pxmitbuf->len = 0;
#ifdef CONFIG_TRX_BD_ARCH
		/*pxmitbuf->buf_desc = NULL;*/
#else
		pxmitbuf->desc = NULL;
#endif
#endif

		if (pxmitbuf->sctx) {
			RTW_INFO("%s pxmitbuf->sctx is not NULL\n", __func__);
			rtw_sctx_done_err(&pxmitbuf->sctx, RTW_SCTX_DONE_BUF_ALLOC);
		}
	} else
		RTW_INFO("%s fail, no xmitbuf available !!!\n", __func__);

	return pxmitbuf;
}

struct xmit_frame *__rtw_alloc_cmdxmitframe(struct xmit_priv *pxmitpriv,
		enum cmdbuf_type buf_type)
{
	struct xmit_frame		*pcmdframe;
	struct xmit_buf		*pxmitbuf;

	pcmdframe = rtw_alloc_xmitframe(pxmitpriv);
	if (pcmdframe == NULL) {
		RTW_INFO("%s, alloc xmitframe fail\n", __FUNCTION__);
		return NULL;
	}

	pxmitbuf = __rtw_alloc_cmd_xmitbuf(pxmitpriv, buf_type);
	if (pxmitbuf == NULL) {
		RTW_INFO("%s, alloc xmitbuf fail\n", __FUNCTION__);
		rtw_free_xmitframe(pxmitpriv, pcmdframe);
		return NULL;
	}

	pcmdframe->frame_tag = MGNT_FRAMETAG;

	pcmdframe->pxmitbuf = pxmitbuf;

	pcmdframe->buf_addr = pxmitbuf->pbuf;

	/* initial memory to zero */
	_rtw_memset(pcmdframe->buf_addr, 0, MAX_CMDBUF_SZ);

	pxmitbuf->priv_data = pcmdframe;

	return pcmdframe;

}

struct xmit_buf *rtw_alloc_xmitbuf_ext(struct xmit_priv *pxmitpriv)
{
	_irqL irqL;
	struct xmit_buf *pxmitbuf =  NULL;
	_list *plist, *phead;
	_queue *pfree_queue = &pxmitpriv->free_xmit_extbuf_queue;


	_enter_critical(&pfree_queue->lock, &irqL);

	if (_rtw_queue_empty(pfree_queue) == _TRUE)
		pxmitbuf = NULL;
	else {

		phead = get_list_head(pfree_queue);

		plist = get_next(phead);

		pxmitbuf = LIST_CONTAINOR(plist, struct xmit_buf, list);

		rtw_list_delete(&(pxmitbuf->list));
	}

	if (pxmitbuf !=  NULL) {
		pxmitpriv->free_xmit_extbuf_cnt--;
#ifdef DBG_XMIT_BUF_EXT
		RTW_INFO("DBG_XMIT_BUF_EXT ALLOC no=%d,  free_xmit_extbuf_cnt=%d\n", pxmitbuf->no, pxmitpriv->free_xmit_extbuf_cnt);
#endif


		pxmitbuf->priv_data = NULL;

#if defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
		pxmitbuf->len = 0;
		pxmitbuf->pdata = pxmitbuf->ptail = pxmitbuf->phead;
		pxmitbuf->agg_num = 1;
#endif
#ifdef CONFIG_PCI_HCI
		pxmitbuf->len = 0;
#ifdef CONFIG_TRX_BD_ARCH
		/*pxmitbuf->buf_desc = NULL;*/
#else
		pxmitbuf->desc = NULL;
#endif
#endif

		if (pxmitbuf->sctx) {
			RTW_INFO("%s pxmitbuf->sctx is not NULL\n", __func__);
			rtw_sctx_done_err(&pxmitbuf->sctx, RTW_SCTX_DONE_BUF_ALLOC);
		}

	}

	_exit_critical(&pfree_queue->lock, &irqL);


	return pxmitbuf;
}

s32 rtw_free_xmitbuf_ext(struct xmit_priv *pxmitpriv, struct xmit_buf *pxmitbuf)
{
	_irqL irqL;
	_queue *pfree_queue = &pxmitpriv->free_xmit_extbuf_queue;


	if (pxmitbuf == NULL)
		return _FAIL;

	_enter_critical(&pfree_queue->lock, &irqL);

	rtw_list_delete(&pxmitbuf->list);

	rtw_list_insert_tail(&(pxmitbuf->list), get_list_head(pfree_queue));
	pxmitpriv->free_xmit_extbuf_cnt++;
#ifdef DBG_XMIT_BUF_EXT
	RTW_INFO("DBG_XMIT_BUF_EXT FREE no=%d, free_xmit_extbuf_cnt=%d\n", pxmitbuf->no , pxmitpriv->free_xmit_extbuf_cnt);
#endif

	_exit_critical(&pfree_queue->lock, &irqL);


	return _SUCCESS;
}

struct xmit_buf *rtw_alloc_xmitbuf(struct xmit_priv *pxmitpriv)
{
	_irqL irqL;
	struct xmit_buf *pxmitbuf =  NULL;
	_list *plist, *phead;
	_queue *pfree_xmitbuf_queue = &pxmitpriv->free_xmitbuf_queue;


	/* RTW_INFO("+rtw_alloc_xmitbuf\n"); */

	_enter_critical(&pfree_xmitbuf_queue->lock, &irqL);

	if (_rtw_queue_empty(pfree_xmitbuf_queue) == _TRUE)
		pxmitbuf = NULL;
	else {

		phead = get_list_head(pfree_xmitbuf_queue);

		plist = get_next(phead);

		pxmitbuf = LIST_CONTAINOR(plist, struct xmit_buf, list);

		rtw_list_delete(&(pxmitbuf->list));
	}

	if (pxmitbuf !=  NULL) {
		pxmitpriv->free_xmitbuf_cnt--;
#ifdef DBG_XMIT_BUF
		RTW_INFO("DBG_XMIT_BUF ALLOC no=%d,  free_xmitbuf_cnt=%d\n", pxmitbuf->no, pxmitpriv->free_xmitbuf_cnt);
#endif
		/* RTW_INFO("alloc, free_xmitbuf_cnt=%d\n", pxmitpriv->free_xmitbuf_cnt); */

		pxmitbuf->priv_data = NULL;

#if defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
		pxmitbuf->len = 0;
		pxmitbuf->pdata = pxmitbuf->ptail = pxmitbuf->phead;
		pxmitbuf->agg_num = 0;
		pxmitbuf->pg_num = 0;
#endif
#ifdef CONFIG_PCI_HCI
		pxmitbuf->len = 0;
#ifdef CONFIG_TRX_BD_ARCH
		/*pxmitbuf->buf_desc = NULL;*/
#else
		pxmitbuf->desc = NULL;
#endif
#endif

		if (pxmitbuf->sctx) {
			RTW_INFO("%s pxmitbuf->sctx is not NULL\n", __func__);
			rtw_sctx_done_err(&pxmitbuf->sctx, RTW_SCTX_DONE_BUF_ALLOC);
		}
	}
#ifdef DBG_XMIT_BUF
	else
		RTW_INFO("DBG_XMIT_BUF rtw_alloc_xmitbuf return NULL\n");
#endif

	_exit_critical(&pfree_xmitbuf_queue->lock, &irqL);


	return pxmitbuf;
}

s32 rtw_free_xmitbuf(struct xmit_priv *pxmitpriv, struct xmit_buf *pxmitbuf)
{
	_irqL irqL;
	_queue *pfree_xmitbuf_queue = &pxmitpriv->free_xmitbuf_queue;


	/* RTW_INFO("+rtw_free_xmitbuf\n"); */

	if (pxmitbuf == NULL)
		return _FAIL;

	if (pxmitbuf->sctx) {
		RTW_INFO("%s pxmitbuf->sctx is not NULL\n", __func__);
		rtw_sctx_done_err(&pxmitbuf->sctx, RTW_SCTX_DONE_BUF_FREE);
	}

	if (pxmitbuf->buf_tag == XMITBUF_CMD) {
	} else if (pxmitbuf->buf_tag == XMITBUF_MGNT)
		rtw_free_xmitbuf_ext(pxmitpriv, pxmitbuf);
	else {
		_enter_critical(&pfree_xmitbuf_queue->lock, &irqL);

		rtw_list_delete(&pxmitbuf->list);

		rtw_list_insert_tail(&(pxmitbuf->list), get_list_head(pfree_xmitbuf_queue));

		pxmitpriv->free_xmitbuf_cnt++;
		/* RTW_INFO("FREE, free_xmitbuf_cnt=%d\n", pxmitpriv->free_xmitbuf_cnt); */
#ifdef DBG_XMIT_BUF
		RTW_INFO("DBG_XMIT_BUF FREE no=%d, free_xmitbuf_cnt=%d\n", pxmitbuf->no , pxmitpriv->free_xmitbuf_cnt);
#endif
		_exit_critical(&pfree_xmitbuf_queue->lock, &irqL);
	}


	return _SUCCESS;
}

void rtw_init_xmitframe(struct xmit_frame *pxframe)
{
	if (pxframe !=  NULL) { /* default value setting */
		pxframe->buf_addr = NULL;
		pxframe->pxmitbuf = NULL;

		_rtw_memset(&pxframe->attrib, 0, sizeof(struct pkt_attrib));
		/* pxframe->attrib.psta = NULL; */

		pxframe->frame_tag = DATA_FRAMETAG;

#ifdef CONFIG_USB_HCI
		pxframe->pkt = NULL;
#ifdef USB_PACKET_OFFSET_SZ
		pxframe->pkt_offset = (PACKET_OFFSET_SZ / 8);
#else
		pxframe->pkt_offset = 1;/* default use pkt_offset to fill tx desc */
#endif

#ifdef CONFIG_USB_TX_AGGREGATION
		pxframe->agg_num = 1;
#endif

#endif /* #ifdef CONFIG_USB_HCI */

#if defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
		pxframe->pg_num = 1;
		pxframe->agg_num = 1;
#endif

#ifdef CONFIG_XMIT_ACK
		pxframe->ack_report = 0;
#endif

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
struct xmit_frame *rtw_alloc_xmitframe(struct xmit_priv *pxmitpriv)/* (_queue *pfree_xmit_queue) */
{
	/*
		Please remember to use all the osdep_service api,
		and lock/unlock or _enter/_exit critical to protect
		pfree_xmit_queue
	*/

	_irqL irqL;
	struct xmit_frame *pxframe = NULL;
	_list *plist, *phead;
	_queue *pfree_xmit_queue = &pxmitpriv->free_xmit_queue;


	_enter_critical_bh(&pfree_xmit_queue->lock, &irqL);

	if (_rtw_queue_empty(pfree_xmit_queue) == _TRUE) {
		pxframe =  NULL;
	} else {
		phead = get_list_head(pfree_xmit_queue);

		plist = get_next(phead);

		pxframe = LIST_CONTAINOR(plist, struct xmit_frame, list);

		rtw_list_delete(&(pxframe->list));
		pxmitpriv->free_xmitframe_cnt--;
	}

	_exit_critical_bh(&pfree_xmit_queue->lock, &irqL);

	rtw_init_xmitframe(pxframe);


	return pxframe;
}

struct xmit_frame *rtw_alloc_xmitframe_ext(struct xmit_priv *pxmitpriv)
{
	_irqL irqL;
	struct xmit_frame *pxframe = NULL;
	_list *plist, *phead;
	_queue *queue = &pxmitpriv->free_xframe_ext_queue;


	_enter_critical_bh(&queue->lock, &irqL);

	if (_rtw_queue_empty(queue) == _TRUE) {
		pxframe =  NULL;
	} else {
		phead = get_list_head(queue);
		plist = get_next(phead);
		pxframe = LIST_CONTAINOR(plist, struct xmit_frame, list);

		rtw_list_delete(&(pxframe->list));
		pxmitpriv->free_xframe_ext_cnt--;
	}

	_exit_critical_bh(&queue->lock, &irqL);

	rtw_init_xmitframe(pxframe);


	return pxframe;
}

struct xmit_frame *rtw_alloc_xmitframe_once(struct xmit_priv *pxmitpriv)
{
	struct xmit_frame *pxframe = NULL;
	u8 *alloc_addr;

	alloc_addr = rtw_zmalloc(sizeof(struct xmit_frame) + 4);

	if (alloc_addr == NULL)
		goto exit;

	pxframe = (struct xmit_frame *)N_BYTE_ALIGMENT((SIZE_PTR)(alloc_addr), 4);
	pxframe->alloc_addr = alloc_addr;

	pxframe->padapter = pxmitpriv->adapter;
	pxframe->frame_tag = NULL_FRAMETAG;

	pxframe->pkt = NULL;

	pxframe->buf_addr = NULL;
	pxframe->pxmitbuf = NULL;

	rtw_init_xmitframe(pxframe);

	RTW_INFO("################## %s ##################\n", __func__);

exit:
	return pxframe;
}

s32 rtw_free_xmitframe(struct xmit_priv *pxmitpriv, struct xmit_frame *pxmitframe)
{
	_irqL irqL;
	_queue *queue = NULL;
	_adapter *padapter = pxmitpriv->adapter;
	_pkt *pndis_pkt = NULL;


	if (pxmitframe == NULL) {
		goto exit;
	}

	if (pxmitframe->pkt) {
		pndis_pkt = pxmitframe->pkt;
		pxmitframe->pkt = NULL;
	}

	if (pxmitframe->alloc_addr) {
		RTW_INFO("################## %s with alloc_addr ##################\n", __func__);
		rtw_mfree(pxmitframe->alloc_addr, sizeof(struct xmit_frame) + 4);
		goto check_pkt_complete;
	}

	if (pxmitframe->ext_tag == 0)
		queue = &pxmitpriv->free_xmit_queue;
	else if (pxmitframe->ext_tag == 1)
		queue = &pxmitpriv->free_xframe_ext_queue;
	else
		rtw_warn_on(1);

	_enter_critical_bh(&queue->lock, &irqL);

	rtw_list_delete(&pxmitframe->list);
	rtw_list_insert_tail(&pxmitframe->list, get_list_head(queue));
	if (pxmitframe->ext_tag == 0) {
		pxmitpriv->free_xmitframe_cnt++;
	} else if (pxmitframe->ext_tag == 1) {
		pxmitpriv->free_xframe_ext_cnt++;
	} else {
	}

	_exit_critical_bh(&queue->lock, &irqL);

check_pkt_complete:

	if (pndis_pkt)
		rtw_os_pkt_complete(padapter, pndis_pkt);

exit:


	return _SUCCESS;
}

void rtw_free_xmitframe_queue(struct xmit_priv *pxmitpriv, _queue *pframequeue)
{
	_irqL irqL;
	_list	*plist, *phead;
	struct	xmit_frame	*pxmitframe;


	_enter_critical_bh(&(pframequeue->lock), &irqL);

	phead = get_list_head(pframequeue);
	plist = get_next(phead);

	while (rtw_end_of_queue_search(phead, plist) == _FALSE) {

		pxmitframe = LIST_CONTAINOR(plist, struct xmit_frame, list);

		plist = get_next(plist);

		rtw_free_xmitframe(pxmitpriv, pxmitframe);

	}
	_exit_critical_bh(&(pframequeue->lock), &irqL);

}

s32 rtw_xmitframe_enqueue(_adapter *padapter, struct xmit_frame *pxmitframe)
{
	DBG_COUNTER(padapter->tx_logs.core_tx_enqueue);
	if (rtw_xmit_classifier(padapter, pxmitframe) == _FAIL) {
		/*		pxmitframe->pkt = NULL; */
		return _FAIL;
	}

	return _SUCCESS;
}

static struct xmit_frame *dequeue_one_xmitframe(struct xmit_priv *pxmitpriv, struct hw_xmit *phwxmit, struct tx_servq *ptxservq, _queue *pframe_queue)
{
	_list	*xmitframe_plist, *xmitframe_phead;
	struct	xmit_frame	*pxmitframe = NULL;

	xmitframe_phead = get_list_head(pframe_queue);
	xmitframe_plist = get_next(xmitframe_phead);

	while ((rtw_end_of_queue_search(xmitframe_phead, xmitframe_plist)) == _FALSE) {
		pxmitframe = LIST_CONTAINOR(xmitframe_plist, struct xmit_frame, list);

		/* xmitframe_plist = get_next(xmitframe_plist); */

		/*#ifdef RTK_DMP_PLATFORM
		#ifdef CONFIG_USB_TX_AGGREGATION
				if((ptxservq->qcnt>0) && (ptxservq->qcnt<=2))
				{
					pxmitframe = NULL;

					tasklet_schedule(&pxmitpriv->xmit_tasklet);

					break;
				}
		#endif
		#endif*/
		rtw_list_delete(&pxmitframe->list);

		ptxservq->qcnt--;

		/* rtw_list_insert_tail(&pxmitframe->list, &phwxmit->pending); */

		/* ptxservq->qcnt--; */

		break;

		/* pxmitframe = NULL; */

	}

	return pxmitframe;
}

static struct xmit_frame *get_one_xmitframe(struct xmit_priv *pxmitpriv, struct hw_xmit *phwxmit, struct tx_servq *ptxservq, _queue *pframe_queue)
{
	_list	*xmitframe_plist, *xmitframe_phead;
	struct	xmit_frame	*pxmitframe = NULL;

	xmitframe_phead = get_list_head(pframe_queue);
	xmitframe_plist = get_next(xmitframe_phead);

	while ((rtw_end_of_queue_search(xmitframe_phead, xmitframe_plist)) == _FALSE) {
		pxmitframe = LIST_CONTAINOR(xmitframe_plist, struct xmit_frame, list);
		break;
	}

	return pxmitframe;
}

struct xmit_frame *rtw_get_xframe(struct xmit_priv *pxmitpriv, int *num_frame)
{
	_irqL irqL0;
	_list *sta_plist, *sta_phead;
	struct hw_xmit *phwxmit_i = pxmitpriv->hwxmits;
	sint entry =  pxmitpriv->hwxmit_entry;

	struct hw_xmit *phwxmit;
	struct tx_servq *ptxservq = NULL;
	_queue *pframe_queue = NULL;
	struct xmit_frame *pxmitframe = NULL;
	_adapter *padapter = pxmitpriv->adapter;
	struct registry_priv	*pregpriv = &padapter->registrypriv;
	int i, inx[4];

	inx[0] = 0;
	inx[1] = 1;
	inx[2] = 2;
	inx[3] = 3;

	*num_frame = 0;

	/*No amsdu when wifi_spec on*/
	if (pregpriv->wifi_spec == 1) {
		return NULL;
	}

	_enter_critical_bh(&pxmitpriv->lock, &irqL0);

	for (i = 0; i < entry; i++) {
		phwxmit = phwxmit_i + inx[i];

		sta_phead = get_list_head(phwxmit->sta_queue);
		sta_plist = get_next(sta_phead);

		while ((rtw_end_of_queue_search(sta_phead, sta_plist)) == _FALSE) {

			ptxservq = LIST_CONTAINOR(sta_plist, struct tx_servq, tx_pending);
			pframe_queue = &ptxservq->sta_pending;

			if(ptxservq->qcnt)
			{
				*num_frame = ptxservq->qcnt;
				pxmitframe = get_one_xmitframe(pxmitpriv, phwxmit, ptxservq, pframe_queue);
				goto exit;
			}
			sta_plist = get_next(sta_plist);
		}
	}

exit:

	_exit_critical_bh(&pxmitpriv->lock, &irqL0);

	return pxmitframe;
}


struct xmit_frame *rtw_dequeue_xframe(struct xmit_priv *pxmitpriv, struct hw_xmit *phwxmit_i, sint entry)
{
	_irqL irqL0;
	_list *sta_plist, *sta_phead;
	struct hw_xmit *phwxmit;
	struct tx_servq *ptxservq = NULL;
	_queue *pframe_queue = NULL;
	struct xmit_frame *pxmitframe = NULL;
	_adapter *padapter = pxmitpriv->adapter;
	struct registry_priv	*pregpriv = &padapter->registrypriv;
	int i, inx[4];

	inx[0] = 0;
	inx[1] = 1;
	inx[2] = 2;
	inx[3] = 3;

	if (pregpriv->wifi_spec == 1) {
		int j;
#if 0
		if (flags < XMIT_QUEUE_ENTRY) {
			/* priority exchange according to the completed xmitbuf flags. */
			inx[flags] = 0;
			inx[0] = flags;
		}
#endif

#if defined(CONFIG_USB_HCI) || defined(CONFIG_SDIO_HCI) || defined(CONFIG_PCI_HCI)
		for (j = 0; j < 4; j++)
			inx[j] = pxmitpriv->wmm_para_seq[j];
#endif
	}

	_enter_critical_bh(&pxmitpriv->lock, &irqL0);

	for (i = 0; i < entry; i++) {
		phwxmit = phwxmit_i + inx[i];

		/* _enter_critical_ex(&phwxmit->sta_queue->lock, &irqL0); */

		sta_phead = get_list_head(phwxmit->sta_queue);
		sta_plist = get_next(sta_phead);

		while ((rtw_end_of_queue_search(sta_phead, sta_plist)) == _FALSE) {

			ptxservq = LIST_CONTAINOR(sta_plist, struct tx_servq, tx_pending);

			pframe_queue = &ptxservq->sta_pending;

			pxmitframe = dequeue_one_xmitframe(pxmitpriv, phwxmit, ptxservq, pframe_queue);

			if (pxmitframe) {
				phwxmit->accnt--;

				/* Remove sta node when there is no pending packets. */
				if (_rtw_queue_empty(pframe_queue)) /* must be done after get_next and before break */
					rtw_list_delete(&ptxservq->tx_pending);

				/* _exit_critical_ex(&phwxmit->sta_queue->lock, &irqL0); */

				goto exit;
			}

			sta_plist = get_next(sta_plist);

		}

		/* _exit_critical_ex(&phwxmit->sta_queue->lock, &irqL0); */

	}

exit:

	_exit_critical_bh(&pxmitpriv->lock, &irqL0);

	return pxmitframe;
}

#if 1
struct tx_servq *rtw_get_sta_pending(_adapter *padapter, struct sta_info *psta, sint up, u8 *ac)
{
	struct tx_servq *ptxservq = NULL;


	switch (up) {
	case 1:
	case 2:
		ptxservq = &(psta->sta_xmitpriv.bk_q);
		*(ac) = 3;
		break;

	case 4:
	case 5:
		ptxservq = &(psta->sta_xmitpriv.vi_q);
		*(ac) = 1;
		break;

	case 6:
	case 7:
		ptxservq = &(psta->sta_xmitpriv.vo_q);
		*(ac) = 0;
		break;

	case 0:
	case 3:
	default:
		ptxservq = &(psta->sta_xmitpriv.be_q);
		*(ac) = 2;
		break;

	}


	return ptxservq;
}
#else
__inline static struct tx_servq *rtw_get_sta_pending
(_adapter *padapter, _queue **ppstapending, struct sta_info *psta, sint up)
{
	struct tx_servq *ptxservq;
	struct hw_xmit *phwxmits =  padapter->xmitpriv.hwxmits;


#ifdef CONFIG_RTL8711

	if (IS_MCAST(psta->cmn.mac_addr)) {
		ptxservq = &(psta->sta_xmitpriv.be_q); /* we will use be_q to queue bc/mc frames in BCMC_stainfo */
		*ppstapending = &padapter->xmitpriv.bm_pending;
	} else
#endif
	{
		switch (up) {
		case 1:
		case 2:
			ptxservq = &(psta->sta_xmitpriv.bk_q);
			*ppstapending = &padapter->xmitpriv.bk_pending;
			(phwxmits + 3)->accnt++;
			break;

		case 4:
		case 5:
			ptxservq = &(psta->sta_xmitpriv.vi_q);
			*ppstapending = &padapter->xmitpriv.vi_pending;
			(phwxmits + 1)->accnt++;
			break;

		case 6:
		case 7:
			ptxservq = &(psta->sta_xmitpriv.vo_q);
			*ppstapending = &padapter->xmitpriv.vo_pending;
			(phwxmits + 0)->accnt++;
			break;

		case 0:
		case 3:
		default:
			ptxservq = &(psta->sta_xmitpriv.be_q);
			*ppstapending = &padapter->xmitpriv.be_pending;
			(phwxmits + 2)->accnt++;
			break;

		}

	}


	return ptxservq;
}
#endif

/*
 * Will enqueue pxmitframe to the proper queue,
 * and indicate it to xx_pending list.....
 */
s32 rtw_xmit_classifier(_adapter *padapter, struct xmit_frame *pxmitframe)
{
	/* _irqL irqL0; */
	u8	ac_index;
	struct sta_info	*psta;
	struct tx_servq	*ptxservq;
	struct pkt_attrib	*pattrib = &pxmitframe->attrib;
	struct hw_xmit	*phwxmits =  padapter->xmitpriv.hwxmits;
	sint res = _SUCCESS;


	DBG_COUNTER(padapter->tx_logs.core_tx_enqueue_class);

	/*
		if (pattrib->psta) {
			psta = pattrib->psta;
		} else {
			RTW_INFO("%s, call rtw_get_stainfo()\n", __func__);
			psta = rtw_get_stainfo(pstapriv, pattrib->ra);
		}
	*/

	psta = rtw_get_stainfo(&padapter->stapriv, pattrib->ra);
	if (pattrib->psta != psta) {
		DBG_COUNTER(padapter->tx_logs.core_tx_enqueue_class_err_sta);
		RTW_INFO("%s, pattrib->psta(%p) != psta(%p)\n", __func__, pattrib->psta, psta);
		return _FAIL;
	}

	if (psta == NULL) {
		DBG_COUNTER(padapter->tx_logs.core_tx_enqueue_class_err_nosta);
		res = _FAIL;
		RTW_INFO("rtw_xmit_classifier: psta == NULL\n");
		goto exit;
	}

	if (!(psta->state & _FW_LINKED)) {
		DBG_COUNTER(padapter->tx_logs.core_tx_enqueue_class_err_fwlink);
		RTW_INFO("%s, psta->state(0x%x) != _FW_LINKED\n", __func__, psta->state);
		return _FAIL;
	}

	ptxservq = rtw_get_sta_pending(padapter, psta, pattrib->priority, (u8 *)(&ac_index));

	/* _enter_critical(&pstapending->lock, &irqL0); */

	if (rtw_is_list_empty(&ptxservq->tx_pending))
		rtw_list_insert_tail(&ptxservq->tx_pending, get_list_head(phwxmits[ac_index].sta_queue));

	/* _enter_critical(&ptxservq->sta_pending.lock, &irqL1); */

	rtw_list_insert_tail(&pxmitframe->list, get_list_head(&ptxservq->sta_pending));
	ptxservq->qcnt++;
	phwxmits[ac_index].accnt++;

	/* _exit_critical(&ptxservq->sta_pending.lock, &irqL1); */

	/* _exit_critical(&pstapending->lock, &irqL0); */

exit:


	return res;
}

void rtw_alloc_hwxmits(_adapter *padapter)
{
	struct hw_xmit *hwxmits;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;

	pxmitpriv->hwxmit_entry = HWXMIT_ENTRY;

	pxmitpriv->hwxmits = NULL;

	pxmitpriv->hwxmits = (struct hw_xmit *)rtw_zmalloc(sizeof(struct hw_xmit) * pxmitpriv->hwxmit_entry);

	if (pxmitpriv->hwxmits == NULL) {
		RTW_INFO("alloc hwxmits fail!...\n");
		return;
	}

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

void rtw_free_hwxmits(_adapter *padapter)
{
	struct hw_xmit *hwxmits;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;

	hwxmits = pxmitpriv->hwxmits;
	if (hwxmits)
		rtw_mfree((u8 *)hwxmits, (sizeof(struct hw_xmit) * pxmitpriv->hwxmit_entry));
}

void rtw_init_hwxmits(struct hw_xmit *phwxmit, sint entry)
{
	sint i;
	for (i = 0; i < entry; i++, phwxmit++) {
		/* _rtw_spinlock_init(&phwxmit->xmit_lock); */
		/* _rtw_init_listhead(&phwxmit->pending);		 */
		/* phwxmit->txcmdcnt = 0; */
		phwxmit->accnt = 0;
	}
}

#ifdef CONFIG_BR_EXT
int rtw_br_client_tx(_adapter *padapter, struct sk_buff **pskb)
{
	struct sk_buff *skb = *pskb;
	_irqL irqL;
	/* if(check_fwstate(pmlmepriv, WIFI_STATION_STATE|WIFI_ADHOC_STATE) == _TRUE) */
	{
		void dhcp_flag_bcast(_adapter *priv, struct sk_buff *skb);
		int res, is_vlan_tag = 0, i, do_nat25 = 1;
		unsigned short vlan_hdr = 0;
		void *br_port = NULL;

		/* mac_clone_handle_frame(priv, skb); */

#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 35))
		br_port = padapter->pnetdev->br_port;
#else   /* (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 35)) */
		rcu_read_lock();
		br_port = rcu_dereference(padapter->pnetdev->rx_handler_data);
		rcu_read_unlock();
#endif /* (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 35)) */
		_enter_critical_bh(&padapter->br_ext_lock, &irqL);
		if (!(skb->data[0] & 1) &&
		    br_port &&
		    memcmp(skb->data + MACADDRLEN, padapter->br_mac, MACADDRLEN) &&
		    *((unsigned short *)(skb->data + MACADDRLEN * 2)) != __constant_htons(ETH_P_8021Q) &&
		    *((unsigned short *)(skb->data + MACADDRLEN * 2)) == __constant_htons(ETH_P_IP) &&
		    !memcmp(padapter->scdb_mac, skb->data + MACADDRLEN, MACADDRLEN) && padapter->scdb_entry) {
			memcpy(skb->data + MACADDRLEN, GET_MY_HWADDR(padapter), MACADDRLEN);
			padapter->scdb_entry->ageing_timer = jiffies;
			_exit_critical_bh(&padapter->br_ext_lock, &irqL);
		} else
			/* if (!priv->pmib->ethBrExtInfo.nat25_disable)		 */
		{
			/*			if (priv->dev->br_port &&
			 *				 !memcmp(skb->data+MACADDRLEN, priv->br_mac, MACADDRLEN)) { */
#if 1
			if (*((unsigned short *)(skb->data + MACADDRLEN * 2)) == __constant_htons(ETH_P_8021Q)) {
				is_vlan_tag = 1;
				vlan_hdr = *((unsigned short *)(skb->data + MACADDRLEN * 2 + 2));
				for (i = 0; i < 6; i++)
					*((unsigned short *)(skb->data + MACADDRLEN * 2 + 2 - i * 2)) = *((unsigned short *)(skb->data + MACADDRLEN * 2 - 2 - i * 2));
				skb_pull(skb, 4);
			}
			/* if SA == br_mac && skb== IP  => copy SIP to br_ip ?? why */
			if (!memcmp(skb->data + MACADDRLEN, padapter->br_mac, MACADDRLEN) &&
			    (*((unsigned short *)(skb->data + MACADDRLEN * 2)) == __constant_htons(ETH_P_IP)))
				memcpy(padapter->br_ip, skb->data + WLAN_ETHHDR_LEN + 12, 4);

			if (*((unsigned short *)(skb->data + MACADDRLEN * 2)) == __constant_htons(ETH_P_IP)) {
				if (memcmp(padapter->scdb_mac, skb->data + MACADDRLEN, MACADDRLEN)) {
					void *scdb_findEntry(_adapter *priv, unsigned char *macAddr, unsigned char *ipAddr);

					padapter->scdb_entry = (struct nat25_network_db_entry *)scdb_findEntry(padapter,
						skb->data + MACADDRLEN, skb->data + WLAN_ETHHDR_LEN + 12);
					if (padapter->scdb_entry != NULL) {
						memcpy(padapter->scdb_mac, skb->data + MACADDRLEN, MACADDRLEN);
						memcpy(padapter->scdb_ip, skb->data + WLAN_ETHHDR_LEN + 12, 4);
						padapter->scdb_entry->ageing_timer = jiffies;
						do_nat25 = 0;
					}
				} else {
					if (padapter->scdb_entry) {
						padapter->scdb_entry->ageing_timer = jiffies;
						do_nat25 = 0;
					} else {
						memset(padapter->scdb_mac, 0, MACADDRLEN);
						memset(padapter->scdb_ip, 0, 4);
					}
				}
			}
			_exit_critical_bh(&padapter->br_ext_lock, &irqL);
#endif /* 1 */
			if (do_nat25) {
				int nat25_db_handle(_adapter *priv, struct sk_buff *skb, int method);
				if (nat25_db_handle(padapter, skb, NAT25_CHECK) == 0) {
					struct sk_buff *newskb;

					if (is_vlan_tag) {
						skb_push(skb, 4);
						for (i = 0; i < 6; i++)
							*((unsigned short *)(skb->data + i * 2)) = *((unsigned short *)(skb->data + 4 + i * 2));
						*((unsigned short *)(skb->data + MACADDRLEN * 2)) = __constant_htons(ETH_P_8021Q);
						*((unsigned short *)(skb->data + MACADDRLEN * 2 + 2)) = vlan_hdr;
					}

					newskb = rtw_skb_copy(skb);
					if (newskb == NULL) {
						/* priv->ext_stats.tx_drops++; */
						DEBUG_ERR("TX DROP: rtw_skb_copy fail!\n");
						/* goto stop_proc; */
						return -1;
					}
					rtw_skb_free(skb);

					*pskb = skb = newskb;
					if (is_vlan_tag) {
						vlan_hdr = *((unsigned short *)(skb->data + MACADDRLEN * 2 + 2));
						for (i = 0; i < 6; i++)
							*((unsigned short *)(skb->data + MACADDRLEN * 2 + 2 - i * 2)) = *((unsigned short *)(skb->data + MACADDRLEN * 2 - 2 - i * 2));
						skb_pull(skb, 4);
					}
				}

				if (skb_is_nonlinear(skb))
					DEBUG_ERR("%s(): skb_is_nonlinear!!\n", __FUNCTION__);


#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 18))
				res = skb_linearize(skb, GFP_ATOMIC);
#else	/* (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 18)) */
				res = skb_linearize(skb);
#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 18)) */
				if (res < 0) {
					DEBUG_ERR("TX DROP: skb_linearize fail!\n");
					/* goto free_and_stop; */
					return -1;
				}

				res = nat25_db_handle(padapter, skb, NAT25_INSERT);
				if (res < 0) {
					if (res == -2) {
						/* priv->ext_stats.tx_drops++; */
						DEBUG_ERR("TX DROP: nat25_db_handle fail!\n");
						/* goto free_and_stop; */
						return -1;

					}
					/* we just print warning message and let it go */
					/* DEBUG_WARN("%s()-%d: nat25_db_handle INSERT Warning!\n", __FUNCTION__, __LINE__); */
					/* return -1; */ /* return -1 will cause system crash on 2011/08/30! */
					return 0;
				}
			}

			memcpy(skb->data + MACADDRLEN, GET_MY_HWADDR(padapter), MACADDRLEN);

			dhcp_flag_bcast(padapter, skb);

			if (is_vlan_tag) {
				skb_push(skb, 4);
				for (i = 0; i < 6; i++)
					*((unsigned short *)(skb->data + i * 2)) = *((unsigned short *)(skb->data + 4 + i * 2));
				*((unsigned short *)(skb->data + MACADDRLEN * 2)) = __constant_htons(ETH_P_8021Q);
				*((unsigned short *)(skb->data + MACADDRLEN * 2 + 2)) = vlan_hdr;
			}
		}
#if 0
		else {
			if (*((unsigned short *)(skb->data + MACADDRLEN * 2)) == __constant_htons(ETH_P_8021Q))
				is_vlan_tag = 1;

			if (is_vlan_tag) {
				if (ICMPV6_MCAST_MAC(skb->data) && ICMPV6_PROTO1A_VALN(skb->data))
					memcpy(skb->data + MACADDRLEN, GET_MY_HWADDR(padapter), MACADDRLEN);
			} else {
				if (ICMPV6_MCAST_MAC(skb->data) && ICMPV6_PROTO1A(skb->data))
					memcpy(skb->data + MACADDRLEN, GET_MY_HWADDR(padapter), MACADDRLEN);
			}
		}
#endif /* 0 */

		/* check if SA is equal to our MAC */
		if (memcmp(skb->data + MACADDRLEN, GET_MY_HWADDR(padapter), MACADDRLEN)) {
			/* priv->ext_stats.tx_drops++; */
			DEBUG_ERR("TX DROP: untransformed frame SA:%02X%02X%02X%02X%02X%02X!\n",
				skb->data[6], skb->data[7], skb->data[8], skb->data[9], skb->data[10], skb->data[11]);
			/* goto free_and_stop; */
			return -1;
		}
	}
	return 0;
}
#endif /* CONFIG_BR_EXT */

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
	case 0x11: /* BC/MC in PS (HIQ) */
		addr = HIGH_QUEUE_INX;
		break;
	case 0x13:
		addr = TXCMD_QUEUE_INX;
		break;
	case 0x12:
	default:
		addr = MGT_QUEUE_INX;
		break;

	}

	return addr;

}

static void do_queue_select(_adapter	*padapter, struct pkt_attrib *pattrib)
{
	u8 qsel;

	qsel = pattrib->priority;

#ifdef CONFIG_MCC_MODE
	if (MCC_EN(padapter)) {
		/* Under MCC */
		if (rtw_hal_check_mcc_status(padapter, MCC_STATUS_NEED_MCC)) {
			if (padapter->mcc_adapterpriv.role == MCC_ROLE_GO
			    || padapter->mcc_adapterpriv.role == MCC_ROLE_AP) {
				pattrib->qsel = QSLT_VO; /* AP interface VO queue */
				pattrib->priority  = QSLT_VO;
			} else {
				pattrib->qsel = QSLT_BE; /* STA interface BE queue */
				pattrib->priority  = QSLT_BE;
			}
		} else
			/* Not Under MCC */
			pattrib->qsel = qsel;
	} else
		/* Not enable MCC */
		pattrib->qsel = qsel;
#else /* !CONFIG_MCC_MODE */
	pattrib->qsel = qsel;
#endif /* CONFIG_MCC_MODE */
}

/*
 * The main transmit(tx) entry
 *
 * Return
 *	1	enqueue
 *	0	success, hardware will handle this xmit frame(packet)
 *	<0	fail
 */
 #if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 24))
s32 rtw_monitor_xmit_entry(struct sk_buff *skb, struct net_device *ndev)
{
	u16 frame_ctl;
	struct ieee80211_radiotap_header rtap_hdr;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(ndev);
	struct pkt_file pktfile;
	struct rtw_ieee80211_hdr *pwlanhdr;
	struct pkt_attrib	*pattrib;
	struct xmit_frame		*pmgntframe;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct xmit_priv	*pxmitpriv = &(padapter->xmitpriv);
	unsigned char	*pframe;
	u8 dummybuf[32];
	int len = skb->len, rtap_len;


	rtw_mstat_update(MSTAT_TYPE_SKB, MSTAT_ALLOC_SUCCESS, skb->truesize);

#ifndef CONFIG_CUSTOMER_ALIBABA_GENERAL
	if (unlikely(skb->len < sizeof(struct ieee80211_radiotap_header)))
		goto fail;

	_rtw_open_pktfile((_pkt *)skb, &pktfile);
	_rtw_pktfile_read(&pktfile, (u8 *)(&rtap_hdr), sizeof(struct ieee80211_radiotap_header));
	rtap_len = ieee80211_get_radiotap_len((u8 *)(&rtap_hdr));
	if (unlikely(rtap_hdr.it_version))
		goto fail;

	if (unlikely(skb->len < rtap_len))
		goto fail;

	if (rtap_len != 12) {
		RTW_INFO("radiotap len (should be 14): %d\n", rtap_len);
		goto fail;
	}
	_rtw_pktfile_read(&pktfile, dummybuf, rtap_len-sizeof(struct ieee80211_radiotap_header));
	len = len - rtap_len;
#endif
	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (pmgntframe == NULL) {
		rtw_udelay_os(500);
		goto fail;
	}

	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);
	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
//	_rtw_memcpy(pframe, (void *)checking, len);
	_rtw_pktfile_read(&pktfile, pframe, len);


	/* Check DATA/MGNT frames */
	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;
	frame_ctl = le16_to_cpu(pwlanhdr->frame_ctl);
	if ((frame_ctl & RTW_IEEE80211_FCTL_FTYPE) == RTW_IEEE80211_FTYPE_DATA) {

		pattrib = &pmgntframe->attrib;
		update_monitor_frame_attrib(padapter, pattrib);

		if (is_broadcast_mac_addr(pwlanhdr->addr3) || is_broadcast_mac_addr(pwlanhdr->addr1))
			pattrib->rate = MGN_24M;

	} else {

		pattrib = &pmgntframe->attrib;
		update_mgntframe_attrib(padapter, pattrib);

	}
	pattrib->retry_ctrl = _FALSE;
	pattrib->pktlen = len;
	pmlmeext->mgnt_seq = GetSequence(pwlanhdr);
	pattrib->seqnum = pmlmeext->mgnt_seq;
	pmlmeext->mgnt_seq++;
	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe(padapter, pmgntframe);

fail:
	rtw_skb_free(skb);
	return 0;
}
#endif

/*
 *
 * Return _TRUE when frame has been put to queue, otherwise return _FALSE.
 */
static u8 xmit_enqueue(struct _ADAPTER *a, struct xmit_frame *frame)
{
	struct sta_info *sta = NULL;
	struct pkt_attrib *attrib = NULL;
	_irqL irqL;
	_list *head;
	u8 ret = _TRUE;


	attrib = &frame->attrib;
	sta = attrib->psta;
	if (!sta)
		return _FALSE;

	_enter_critical_bh(&sta->tx_queue.lock, &irqL);

	head = get_list_head(&sta->tx_queue);

	if ((rtw_is_list_empty(head) == _TRUE) && (!sta->tx_q_enable)) {
		ret = _FALSE;
		goto exit;
	}

	rtw_list_insert_tail(&frame->list, head);
	RTW_INFO(FUNC_ADPT_FMT ": en-queue tx pkt for macid=%d\n",
		 FUNC_ADPT_ARG(a), sta->cmn.mac_id);

exit:
	_exit_critical_bh(&sta->tx_queue.lock, &irqL);

	return ret;
}

static void xmit_dequeue(struct sta_info *sta)
{
	struct _ADAPTER *a;
	_irqL irqL;
	_list *head, *list;
	struct xmit_frame *frame;


	a = sta->padapter;

	_enter_critical_bh(&sta->tx_queue.lock, &irqL);

	head = get_list_head(&sta->tx_queue);

	do {
		if (rtw_is_list_empty(head) == _TRUE)
			break;

		list = get_next(head);
		rtw_list_delete(list);
		frame = LIST_CONTAINOR(list, struct xmit_frame, list);
		RTW_INFO(FUNC_ADPT_FMT ": de-queue tx frame of macid=%d\n",
			 FUNC_ADPT_ARG(a), sta->cmn.mac_id);

		rtw_hal_xmit(a, frame);
	} while (1);

	_exit_critical_bh(&sta->tx_queue.lock, &irqL);
}

void rtw_xmit_dequeue_callback(_workitem *work)
{
	struct sta_info *sta;


	sta = container_of(work, struct sta_info, tx_q_work);
	xmit_dequeue(sta);
}

void rtw_xmit_queue_set(struct sta_info *sta)
{
	_irqL irqL;

	_enter_critical_bh(&sta->tx_queue.lock, &irqL);

	if (sta->tx_q_enable) {
		RTW_WARN(FUNC_ADPT_FMT ": duplicated set!\n",
			 FUNC_ADPT_ARG(sta->padapter));
		goto exit;
	}
	sta->tx_q_enable = 1;
	RTW_INFO(FUNC_ADPT_FMT ": enable queue TX for macid=%d\n",
		 FUNC_ADPT_ARG(sta->padapter), sta->cmn.mac_id);

exit:
	_exit_critical_bh(&sta->tx_queue.lock, &irqL);
}

void rtw_xmit_queue_clear(struct sta_info *sta)
{
	_irqL irqL;

	_enter_critical_bh(&sta->tx_queue.lock, &irqL);

	if (!sta->tx_q_enable) {
		RTW_WARN(FUNC_ADPT_FMT ": tx queue for macid=%d "
			 "not be enabled!\n",
			 FUNC_ADPT_ARG(sta->padapter), sta->cmn.mac_id);
		goto exit;
	}

	sta->tx_q_enable = 0;
	RTW_INFO(FUNC_ADPT_FMT ": disable queue TX for macid=%d\n",
		 FUNC_ADPT_ARG(sta->padapter), sta->cmn.mac_id);

	_set_workitem(&sta->tx_q_work);

exit:
	_exit_critical_bh(&sta->tx_queue.lock, &irqL);
}

/*
 * The main transmit(tx) entry post handle
 *
 * Return
 *	1	enqueue
 *	0	success, hardware will handle this xmit frame(packet)
 *	<0	fail
 */
s32 rtw_xmit_posthandle(_adapter *padapter, struct xmit_frame *pxmitframe, _pkt *pkt)
{
#ifdef CONFIG_AP_MODE
	_irqL irqL0;
#endif
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	s32 res;

	res = update_attrib(padapter, pkt, &pxmitframe->attrib);

#ifdef CONFIG_MCC_MODE
	/* record data kernel TX to driver to check MCC concurrent TX */
	rtw_hal_mcc_calc_tx_bytes_from_kernel(padapter, pxmitframe->attrib.pktlen);
#endif /* CONFIG_MCC_MODE */

#ifdef CONFIG_WAPI_SUPPORT
	if (pxmitframe->attrib.ether_type != 0x88B4) {
		if (rtw_wapi_drop_for_key_absent(padapter, pxmitframe->attrib.ra)) {
			WAPI_TRACE(WAPI_RX, "drop for key absend when tx\n");
			res = _FAIL;
		}
	}
#endif
	if (res == _FAIL) {
		/*RTW_INFO("%s-"ADPT_FMT" update attrib fail\n", __func__, ADPT_ARG(padapter));*/
#ifdef DBG_TX_DROP_FRAME
		RTW_INFO("DBG_TX_DROP_FRAME %s update attrib fail\n", __FUNCTION__);
#endif
		rtw_free_xmitframe(pxmitpriv, pxmitframe);
		return -1;
	}
	pxmitframe->pkt = pkt;

	rtw_led_tx_control(padapter, pxmitframe->attrib.dst);

	do_queue_select(padapter, &pxmitframe->attrib);

#ifdef CONFIG_AP_MODE
	_enter_critical_bh(&pxmitpriv->lock, &irqL0);
	if (xmitframe_enqueue_for_sleeping_sta(padapter, pxmitframe) == _TRUE) {
		_exit_critical_bh(&pxmitpriv->lock, &irqL0);
		DBG_COUNTER(padapter->tx_logs.core_tx_ap_enqueue);
		return 1;
	}
	_exit_critical_bh(&pxmitpriv->lock, &irqL0);
#endif

	if (xmit_enqueue(padapter, pxmitframe) == _TRUE)
		return 1;

	/* pre_xmitframe */
	if (rtw_hal_xmit(padapter, pxmitframe) == _FALSE)
		return 1;

	return 0;
}

/*
 * The main transmit(tx) entry
 *
 * Return
 *	1	enqueue
 *	0	success, hardware will handle this xmit frame(packet)
 *	<0	fail
 */
s32 rtw_xmit(_adapter *padapter, _pkt **ppkt)
{
	static systime start = 0;
	static u32 drop_cnt = 0;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct xmit_frame *pxmitframe = NULL;
	s32 res;

	DBG_COUNTER(padapter->tx_logs.core_tx);

	if (IS_CH_WAITING(adapter_to_rfctl(padapter)))
		return -1;

	if (rtw_linked_check(padapter) == _FALSE)
		return -1;

	if (start == 0)
		start = rtw_get_current_time();

	pxmitframe = rtw_alloc_xmitframe(pxmitpriv);

	if (rtw_get_passing_time_ms(start) > 2000) {
		if (drop_cnt)
			RTW_INFO("DBG_TX_DROP_FRAME %s no more pxmitframe, drop_cnt:%u\n", __FUNCTION__, drop_cnt);
		start = rtw_get_current_time();
		drop_cnt = 0;
	}

	if (pxmitframe == NULL) {
		drop_cnt++;
		/*RTW_INFO("%s-"ADPT_FMT" no more xmitframe\n", __func__, ADPT_ARG(padapter));*/
		DBG_COUNTER(padapter->tx_logs.core_tx_err_pxmitframe);
		return -1;
	}

#ifdef CONFIG_BR_EXT
	if (check_fwstate(&padapter->mlmepriv, WIFI_STATION_STATE | WIFI_ADHOC_STATE) == _TRUE) {
		void *br_port = NULL;

		#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 35))
		br_port = padapter->pnetdev->br_port;
		#else
		rcu_read_lock();
		br_port = rcu_dereference(padapter->pnetdev->rx_handler_data);
		rcu_read_unlock();
		#endif

		if (br_port) {
			res = rtw_br_client_tx(padapter, ppkt);
			if (res == -1) {
				rtw_free_xmitframe(pxmitpriv, pxmitframe);
				DBG_COUNTER(padapter->tx_logs.core_tx_err_brtx);
				return -1;
			}
		}
	}
#endif /* CONFIG_BR_EXT */

#ifdef CONFIG_RTW_MESH
	if (MLME_IS_MESH(padapter)) {
		_list b2u_list;

		res = rtw_mesh_addr_resolve(padapter, pxmitframe, *ppkt, &b2u_list);
		if (res == RTW_RA_RESOLVING)
			return 1;
		if (res == _FAIL)
			return -1;

		#if CONFIG_RTW_MESH_DATA_BMC_TO_UC
		if (!rtw_is_list_empty(&b2u_list)) {
			_list *list = get_next(&b2u_list);
			struct xmit_frame *b2uframe;

			while ((rtw_end_of_queue_search(&b2u_list, list)) == _FALSE) {
				b2uframe = LIST_CONTAINOR(list, struct xmit_frame, list);
				list = get_next(list);
				rtw_list_delete(&b2uframe->list);

				b2uframe->pkt = rtw_os_pkt_copy(*ppkt);
				if (!b2uframe->pkt) {
					if (res == RTW_BMC_NO_NEED)
						res = _SUCCESS;
					rtw_free_xmitframe(pxmitpriv, b2uframe);
					continue;
				}

				rtw_xmit_posthandle(padapter, b2uframe, b2uframe->pkt);
			}
		}
		#endif /* CONFIG_RTW_MESH_DATA_BMC_TO_UC */

		if (res == RTW_BMC_NO_NEED) {
			rtw_free_xmitframe(&padapter->xmitpriv, pxmitframe);
			return 0;
		}
	}
#endif /* CONFIG_RTW_MESH */

	pxmitframe->pkt = NULL; /* let rtw_xmit_posthandle not to free pkt inside */
	res = rtw_xmit_posthandle(padapter, pxmitframe, *ppkt);

	return res;
}

#ifdef CONFIG_TDLS
sint xmitframe_enqueue_for_tdls_sleeping_sta(_adapter *padapter, struct xmit_frame *pxmitframe)
{
	sint ret = _FALSE;

	_irqL irqL;
	struct sta_info *ptdls_sta = NULL;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct pkt_attrib *pattrib = &pxmitframe->attrib;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	int i;

	ptdls_sta = rtw_get_stainfo(pstapriv, pattrib->dst);
	if (ptdls_sta == NULL)
		return ret;
	else if (ptdls_sta->tdls_sta_state & TDLS_LINKED_STATE) {

		if (pattrib->triggered == 1) {
			ret = _TRUE;
			return ret;
		}

		_enter_critical_bh(&ptdls_sta->sleep_q.lock, &irqL);

		if (ptdls_sta->state & WIFI_SLEEP_STATE) {
			rtw_list_delete(&pxmitframe->list);

			/* _enter_critical_bh(&psta->sleep_q.lock, &irqL);	 */

			rtw_list_insert_tail(&pxmitframe->list, get_list_head(&ptdls_sta->sleep_q));

			ptdls_sta->sleepq_len++;
			ptdls_sta->sleepq_ac_len++;

			/* indicate 4-AC queue bit in TDLS peer traffic indication */
			switch (pattrib->priority) {
			case 1:
			case 2:
				ptdls_sta->uapsd_bk |= BIT(1);
				break;
			case 4:
			case 5:
				ptdls_sta->uapsd_vi |= BIT(1);
				break;
			case 6:
			case 7:
				ptdls_sta->uapsd_vo |= BIT(1);
				break;
			case 0:
			case 3:
			default:
				ptdls_sta->uapsd_be |= BIT(1);
				break;
			}

			/* Transmit TDLS PTI via AP */
			if (ptdls_sta->sleepq_len == 1)
				rtw_tdls_cmd(padapter, ptdls_sta->cmn.mac_addr, TDLS_ISSUE_PTI);

			ret = _TRUE;
		}

		_exit_critical_bh(&ptdls_sta->sleep_q.lock, &irqL);
	}

	return ret;

}
#endif /* CONFIG_TDLS */

#define RTW_HIQ_FILTER_ALLOW_ALL 0
#define RTW_HIQ_FILTER_ALLOW_SPECIAL 1
#define RTW_HIQ_FILTER_DENY_ALL 2

inline bool xmitframe_hiq_filter(struct xmit_frame *xmitframe)
{
	bool allow = _FALSE;
	_adapter *adapter = xmitframe->padapter;
	struct registry_priv *registry = &adapter->registrypriv;

	if (adapter->registrypriv.wifi_spec == 1)
		allow = _TRUE;
	else if (registry->hiq_filter == RTW_HIQ_FILTER_ALLOW_SPECIAL) {

		struct pkt_attrib *attrib = &xmitframe->attrib;

		if (attrib->ether_type == 0x0806
		    || attrib->ether_type == 0x888e
#ifdef CONFIG_WAPI_SUPPORT
		    || attrib->ether_type == 0x88B4
#endif
		    || attrib->dhcp_pkt
		   ) {
			if (0)
				RTW_INFO(FUNC_ADPT_FMT" ether_type:0x%04x%s\n", FUNC_ADPT_ARG(xmitframe->padapter)
					, attrib->ether_type, attrib->dhcp_pkt ? " DHCP" : "");
			allow = _TRUE;
		}
	} else if (registry->hiq_filter == RTW_HIQ_FILTER_ALLOW_ALL)
		allow = _TRUE;
	else if (registry->hiq_filter == RTW_HIQ_FILTER_DENY_ALL)
		allow = _FALSE;
	else
		rtw_warn_on(1);

	return allow;
}

#if defined(CONFIG_AP_MODE) || defined(CONFIG_TDLS)

sint xmitframe_enqueue_for_sleeping_sta(_adapter *padapter, struct xmit_frame *pxmitframe)
{
	_irqL irqL;
	sint ret = _FALSE;
	struct sta_info *psta = NULL;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct pkt_attrib *pattrib = &pxmitframe->attrib;
	sint bmcst = IS_MCAST(pattrib->ra);
	bool update_tim = _FALSE;
#ifdef CONFIG_TDLS

	if (padapter->tdlsinfo.link_established == _TRUE)
		ret = xmitframe_enqueue_for_tdls_sleeping_sta(padapter, pxmitframe);
#endif /* CONFIG_TDLS */

	if (!MLME_IS_AP(padapter) && !MLME_IS_MESH(padapter)) {
		DBG_COUNTER(padapter->tx_logs.core_tx_ap_enqueue_warn_fwstate);
		return ret;
	}
	/*
		if(pattrib->psta)
		{
			psta = pattrib->psta;
		}
		else
		{
			RTW_INFO("%s, call rtw_get_stainfo()\n", __func__);
			psta=rtw_get_stainfo(pstapriv, pattrib->ra);
		}
	*/
	psta = rtw_get_stainfo(&padapter->stapriv, pattrib->ra);
	if (pattrib->psta != psta) {
		DBG_COUNTER(padapter->tx_logs.core_tx_ap_enqueue_warn_sta);
		RTW_INFO("%s, pattrib->psta(%p) != psta(%p)\n", __func__, pattrib->psta, psta);
		return _FALSE;
	}

	if (psta == NULL) {
		DBG_COUNTER(padapter->tx_logs.core_tx_ap_enqueue_warn_nosta);
		RTW_INFO("%s, psta==NUL\n", __func__);
		return _FALSE;
	}

	if (!(psta->state & _FW_LINKED)) {
		DBG_COUNTER(padapter->tx_logs.core_tx_ap_enqueue_warn_link);
		RTW_INFO("%s, psta->state(0x%x) != _FW_LINKED\n", __func__, psta->state);
		return _FALSE;
	}

	if (pattrib->triggered == 1) {
		DBG_COUNTER(padapter->tx_logs.core_tx_ap_enqueue_warn_trigger);
		/* RTW_INFO("directly xmit pspoll_triggered packet\n"); */

		/* pattrib->triggered=0; */
		if (bmcst && xmitframe_hiq_filter(pxmitframe) == _TRUE)
			pattrib->qsel = QSLT_HIGH;/* HIQ */

		return ret;
	}


	if (bmcst) {
		_enter_critical_bh(&psta->sleep_q.lock, &irqL);

		if (rtw_tim_map_anyone_be_set(padapter, pstapriv->sta_dz_bitmap)) { /* if anyone sta is in ps mode */
			/* pattrib->qsel = QSLT_HIGH; */ /* HIQ */

			rtw_list_delete(&pxmitframe->list);

			/*_enter_critical_bh(&psta->sleep_q.lock, &irqL);*/

			rtw_list_insert_tail(&pxmitframe->list, get_list_head(&psta->sleep_q));

			psta->sleepq_len++;

			if (!(rtw_tim_map_is_set(padapter, pstapriv->tim_bitmap, 0)))
				update_tim = _TRUE;

			rtw_tim_map_set(padapter, pstapriv->tim_bitmap, 0);
			rtw_tim_map_set(padapter, pstapriv->sta_dz_bitmap, 0);

			/* RTW_INFO("enqueue, sq_len=%d\n", psta->sleepq_len); */
			/* RTW_INFO_DUMP("enqueue, tim=", pstapriv->tim_bitmap, pstapriv->aid_bmp_len); */
			if (update_tim == _TRUE) {
				if (is_broadcast_mac_addr(pattrib->ra))
					_update_beacon(padapter, _TIM_IE_, NULL, _TRUE, "buffer BC");
				else
					_update_beacon(padapter, _TIM_IE_, NULL, _TRUE, "buffer MC");
			} else
				chk_bmc_sleepq_cmd(padapter);

			/*_exit_critical_bh(&psta->sleep_q.lock, &irqL);*/

			ret = _TRUE;

			DBG_COUNTER(padapter->tx_logs.core_tx_ap_enqueue_mcast);
		}

		_exit_critical_bh(&psta->sleep_q.lock, &irqL);

		return ret;

	}


	_enter_critical_bh(&psta->sleep_q.lock, &irqL);

	if (psta->state & WIFI_SLEEP_STATE) {
		u8 wmmps_ac = 0;

		if (rtw_tim_map_is_set(padapter, pstapriv->sta_dz_bitmap, psta->cmn.aid)) {
			rtw_list_delete(&pxmitframe->list);

			/* _enter_critical_bh(&psta->sleep_q.lock, &irqL);	 */

			rtw_list_insert_tail(&pxmitframe->list, get_list_head(&psta->sleep_q));

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

			if (((psta->has_legacy_ac) && (!wmmps_ac)) || ((!psta->has_legacy_ac) && (wmmps_ac))) {
				if (!(rtw_tim_map_is_set(padapter, pstapriv->tim_bitmap, psta->cmn.aid)))
					update_tim = _TRUE;

				rtw_tim_map_set(padapter, pstapriv->tim_bitmap, psta->cmn.aid);

				/* RTW_INFO("enqueue, sq_len=%d\n", psta->sleepq_len); */
				/* RTW_INFO_DUMP("enqueue, tim=", pstapriv->tim_bitmap, pstapriv->aid_bmp_len); */

				if (update_tim == _TRUE) {
					/* RTW_INFO("sleepq_len==1, update BCNTIM\n"); */
					/* upate BCN for TIM IE */
					_update_beacon(padapter, _TIM_IE_, NULL, _TRUE, "buffer UC");
				}
			}

			/* _exit_critical_bh(&psta->sleep_q.lock, &irqL);			 */

			/* if(psta->sleepq_len > (NR_XMITFRAME>>3)) */
			/* { */
			/*	wakeup_sta_to_xmit(padapter, psta); */
			/* }	 */

			ret = _TRUE;

			DBG_COUNTER(padapter->tx_logs.core_tx_ap_enqueue_ucast);
		}

	}

	_exit_critical_bh(&psta->sleep_q.lock, &irqL);

	return ret;

}

static void dequeue_xmitframes_to_sleeping_queue(_adapter *padapter, struct sta_info *psta, _queue *pframequeue)
{
	sint ret;
	_list	*plist, *phead;
	u8	ac_index;
	struct tx_servq	*ptxservq;
	struct pkt_attrib	*pattrib;
	struct xmit_frame	*pxmitframe;
	struct hw_xmit *phwxmits =  padapter->xmitpriv.hwxmits;

	phead = get_list_head(pframequeue);
	plist = get_next(phead);

	while (rtw_end_of_queue_search(phead, plist) == _FALSE) {
		pxmitframe = LIST_CONTAINOR(plist, struct xmit_frame, list);

		plist = get_next(plist);

		pattrib = &pxmitframe->attrib;

		pattrib->triggered = 0;

		ret = xmitframe_enqueue_for_sleeping_sta(padapter, pxmitframe);

		if (_TRUE == ret) {
			ptxservq = rtw_get_sta_pending(padapter, psta, pattrib->priority, (u8 *)(&ac_index));

			ptxservq->qcnt--;
			phwxmits[ac_index].accnt--;
		} else {
			/* RTW_INFO("xmitframe_enqueue_for_sleeping_sta return _FALSE\n"); */
		}

	}

}

void stop_sta_xmit(_adapter *padapter, struct sta_info *psta)
{
	_irqL irqL0;
	struct sta_info *psta_bmc;
	struct sta_xmit_priv *pstaxmitpriv;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;

	pstaxmitpriv = &psta->sta_xmitpriv;

	/* for BC/MC Frames */
	psta_bmc = rtw_get_bcmc_stainfo(padapter);


	_enter_critical_bh(&pxmitpriv->lock, &irqL0);

	psta->state |= WIFI_SLEEP_STATE;

#ifdef CONFIG_TDLS
	if (!(psta->tdls_sta_state & TDLS_LINKED_STATE))
#endif /* CONFIG_TDLS */
		rtw_tim_map_set(padapter, pstapriv->sta_dz_bitmap, psta->cmn.aid);

	dequeue_xmitframes_to_sleeping_queue(padapter, psta, &pstaxmitpriv->vo_q.sta_pending);
	rtw_list_delete(&(pstaxmitpriv->vo_q.tx_pending));
	dequeue_xmitframes_to_sleeping_queue(padapter, psta, &pstaxmitpriv->vi_q.sta_pending);
	rtw_list_delete(&(pstaxmitpriv->vi_q.tx_pending));
	dequeue_xmitframes_to_sleeping_queue(padapter, psta, &pstaxmitpriv->be_q.sta_pending);
	rtw_list_delete(&(pstaxmitpriv->be_q.tx_pending));
	dequeue_xmitframes_to_sleeping_queue(padapter, psta, &pstaxmitpriv->bk_q.sta_pending);
	rtw_list_delete(&(pstaxmitpriv->bk_q.tx_pending));

#ifdef CONFIG_TDLS
	if (!(psta->tdls_sta_state & TDLS_LINKED_STATE) && (psta_bmc != NULL)) {
#endif /* CONFIG_TDLS */

		/* for BC/MC Frames */
		pstaxmitpriv = &psta_bmc->sta_xmitpriv;
		dequeue_xmitframes_to_sleeping_queue(padapter, psta_bmc, &pstaxmitpriv->vo_q.sta_pending);
		rtw_list_delete(&(pstaxmitpriv->vo_q.tx_pending));
		dequeue_xmitframes_to_sleeping_queue(padapter, psta_bmc, &pstaxmitpriv->vi_q.sta_pending);
		rtw_list_delete(&(pstaxmitpriv->vi_q.tx_pending));
		dequeue_xmitframes_to_sleeping_queue(padapter, psta_bmc, &pstaxmitpriv->be_q.sta_pending);
		rtw_list_delete(&(pstaxmitpriv->be_q.tx_pending));
		dequeue_xmitframes_to_sleeping_queue(padapter, psta_bmc, &pstaxmitpriv->bk_q.sta_pending);
		rtw_list_delete(&(pstaxmitpriv->bk_q.tx_pending));

#ifdef CONFIG_TDLS
	}
#endif /* CONFIG_TDLS	 */
	_exit_critical_bh(&pxmitpriv->lock, &irqL0);


}

void wakeup_sta_to_xmit(_adapter *padapter, struct sta_info *psta)
{
	_irqL irqL;
	u8 update_mask = 0, wmmps_ac = 0;
	struct sta_info *psta_bmc;
	_list	*xmitframe_plist, *xmitframe_phead;
	struct xmit_frame *pxmitframe = NULL;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;

	psta_bmc = rtw_get_bcmc_stainfo(padapter);


	/* _enter_critical_bh(&psta->sleep_q.lock, &irqL); */
	_enter_critical_bh(&pxmitpriv->lock, &irqL);

	xmitframe_phead = get_list_head(&psta->sleep_q);
	xmitframe_plist = get_next(xmitframe_phead);

	while ((rtw_end_of_queue_search(xmitframe_phead, xmitframe_plist)) == _FALSE) {
		pxmitframe = LIST_CONTAINOR(xmitframe_plist, struct xmit_frame, list);

		xmitframe_plist = get_next(xmitframe_plist);

		rtw_list_delete(&pxmitframe->list);

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

		/*
				_exit_critical_bh(&psta->sleep_q.lock, &irqL);
				if(rtw_hal_xmit(padapter, pxmitframe) == _TRUE)
				{
					rtw_os_xmit_complete(padapter, pxmitframe);
				}
				_enter_critical_bh(&psta->sleep_q.lock, &irqL);
		*/
		rtw_hal_xmitframe_enqueue(padapter, pxmitframe);


	}

	if (psta->sleepq_len == 0) {
#ifdef CONFIG_TDLS
		if (psta->tdls_sta_state & TDLS_LINKED_STATE) {
			if (psta->state & WIFI_SLEEP_STATE)
				psta->state ^= WIFI_SLEEP_STATE;

			_exit_critical_bh(&pxmitpriv->lock, &irqL);
			return;
		}
#endif /* CONFIG_TDLS */

		if (rtw_tim_map_is_set(padapter, pstapriv->tim_bitmap, psta->cmn.aid)) {
			/* RTW_INFO("wakeup to xmit, qlen==0\n"); */
			/* RTW_INFO_DUMP("update_BCNTIM, tim=", pstapriv->tim_bitmap, pstapriv->aid_bmp_len); */
			/* upate BCN for TIM IE */
			/* update_BCNTIM(padapter); */
			update_mask = BIT(0);
		}

		rtw_tim_map_clear(padapter, pstapriv->tim_bitmap, psta->cmn.aid);

		if (psta->state & WIFI_SLEEP_STATE)
			psta->state ^= WIFI_SLEEP_STATE;

		if (psta->state & WIFI_STA_ALIVE_CHK_STATE) {
			RTW_INFO("%s alive check\n", __func__);
			psta->expire_to = pstapriv->expire_to;
			psta->state ^= WIFI_STA_ALIVE_CHK_STATE;
		}

		rtw_tim_map_clear(padapter, pstapriv->sta_dz_bitmap, psta->cmn.aid);
	}

	/* for BC/MC Frames */
	if (!psta_bmc)
		goto _exit;

	if (!(rtw_tim_map_anyone_be_set_exclude_aid0(padapter, pstapriv->sta_dz_bitmap))) { /* no any sta in ps mode */
		xmitframe_phead = get_list_head(&psta_bmc->sleep_q);
		xmitframe_plist = get_next(xmitframe_phead);

		while ((rtw_end_of_queue_search(xmitframe_phead, xmitframe_plist)) == _FALSE) {
			pxmitframe = LIST_CONTAINOR(xmitframe_plist, struct xmit_frame, list);

			xmitframe_plist = get_next(xmitframe_plist);

			rtw_list_delete(&pxmitframe->list);

			psta_bmc->sleepq_len--;
			if (psta_bmc->sleepq_len > 0)
				pxmitframe->attrib.mdata = 1;
			else
				pxmitframe->attrib.mdata = 0;


			pxmitframe->attrib.triggered = 1;
			/*
						_exit_critical_bh(&psta_bmc->sleep_q.lock, &irqL);
						if(rtw_hal_xmit(padapter, pxmitframe) == _TRUE)
						{
							rtw_os_xmit_complete(padapter, pxmitframe);
						}
						_enter_critical_bh(&psta_bmc->sleep_q.lock, &irqL);

			*/
			rtw_hal_xmitframe_enqueue(padapter, pxmitframe);

		}

		if (psta_bmc->sleepq_len == 0) {
			if (rtw_tim_map_is_set(padapter, pstapriv->tim_bitmap, 0)) {
				/* RTW_INFO("wakeup to xmit, qlen==0\n"); */
				/* RTW_INFO_DUMP("update_BCNTIM, tim=", pstapriv->tim_bitmap, pstapriv->aid_bmp_len); */
				/* upate BCN for TIM IE */
				/* update_BCNTIM(padapter); */
				update_mask |= BIT(1);
			}
			rtw_tim_map_clear(padapter, pstapriv->tim_bitmap, 0);
			rtw_tim_map_clear(padapter, pstapriv->sta_dz_bitmap, 0);
		}

	}

_exit:

	/* _exit_critical_bh(&psta_bmc->sleep_q.lock, &irqL);	 */
	_exit_critical_bh(&pxmitpriv->lock, &irqL);

	if (update_mask) {
		/* update_BCNTIM(padapter); */
		if ((update_mask & (BIT(0) | BIT(1))) == (BIT(0) | BIT(1)))
			_update_beacon(padapter, _TIM_IE_, NULL, _TRUE, "clear UC&BMC");
		else if ((update_mask & BIT(1)) == BIT(1))
			_update_beacon(padapter, _TIM_IE_, NULL, _TRUE, "clear BMC");
		else
			_update_beacon(padapter, _TIM_IE_, NULL, _TRUE, "clear UC");
	}

}

void xmit_delivery_enabled_frames(_adapter *padapter, struct sta_info *psta)
{
	_irqL irqL;
	u8 wmmps_ac = 0;
	_list	*xmitframe_plist, *xmitframe_phead;
	struct xmit_frame *pxmitframe = NULL;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;


	/* _enter_critical_bh(&psta->sleep_q.lock, &irqL); */
	_enter_critical_bh(&pxmitpriv->lock, &irqL);

	xmitframe_phead = get_list_head(&psta->sleep_q);
	xmitframe_plist = get_next(xmitframe_phead);

	while ((rtw_end_of_queue_search(xmitframe_phead, xmitframe_plist)) == _FALSE) {
		pxmitframe = LIST_CONTAINOR(xmitframe_plist, struct xmit_frame, list);

		xmitframe_plist = get_next(xmitframe_plist);

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

		rtw_list_delete(&pxmitframe->list);

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
#ifdef CONFIG_TDLS
			if (psta->tdls_sta_state & TDLS_LINKED_STATE) {
				/* _exit_critical_bh(&psta->sleep_q.lock, &irqL); */
				goto exit;
			}
#endif /* CONFIG_TDLS */
			rtw_tim_map_clear(padapter, pstapriv->tim_bitmap, psta->cmn.aid);

			/* RTW_INFO("wakeup to xmit, qlen==0\n"); */
			/* RTW_INFO_DUMP("update_BCNTIM, tim=", pstapriv->tim_bitmap, pstapriv->aid_bmp_len); */
			/* upate BCN for TIM IE */
			/* update_BCNTIM(padapter); */
			update_beacon(padapter, _TIM_IE_, NULL, _TRUE);
			/* update_mask = BIT(0); */
		}

	}

#ifdef CONFIG_TDLS
exit:
#endif
	/* _exit_critical_bh(&psta->sleep_q.lock, &irqL);	 */
	_exit_critical_bh(&pxmitpriv->lock, &irqL);

	return;
}

#endif /* defined(CONFIG_AP_MODE) || defined(CONFIG_TDLS) */

#ifdef CONFIG_XMIT_THREAD_MODE
void enqueue_pending_xmitbuf(
	struct xmit_priv *pxmitpriv,
	struct xmit_buf *pxmitbuf)
{
	_irqL irql;
	_queue *pqueue;
	_adapter *pri_adapter = pxmitpriv->adapter;

	pqueue = &pxmitpriv->pending_xmitbuf_queue;

	_enter_critical_bh(&pqueue->lock, &irql);
	rtw_list_delete(&pxmitbuf->list);
	rtw_list_insert_tail(&pxmitbuf->list, get_list_head(pqueue));
	_exit_critical_bh(&pqueue->lock, &irql);

#if defined(CONFIG_SDIO_HCI) && defined(CONFIG_CONCURRENT_MODE)
	pri_adapter = GET_PRIMARY_ADAPTER(pri_adapter);
#endif /*SDIO_HCI + CONCURRENT*/
	_rtw_up_sema(&(pri_adapter->xmitpriv.xmit_sema));
}

void enqueue_pending_xmitbuf_to_head(
	struct xmit_priv *pxmitpriv,
	struct xmit_buf *pxmitbuf)
{
	_irqL irql;
	_queue *pqueue = &pxmitpriv->pending_xmitbuf_queue;

	_enter_critical_bh(&pqueue->lock, &irql);
	rtw_list_delete(&pxmitbuf->list);
	rtw_list_insert_head(&pxmitbuf->list, get_list_head(pqueue));
	_exit_critical_bh(&pqueue->lock, &irql);
}

struct xmit_buf *dequeue_pending_xmitbuf(
	struct xmit_priv *pxmitpriv)
{
	_irqL irql;
	struct xmit_buf *pxmitbuf;
	_queue *pqueue;


	pxmitbuf = NULL;
	pqueue = &pxmitpriv->pending_xmitbuf_queue;

	_enter_critical_bh(&pqueue->lock, &irql);

	if (_rtw_queue_empty(pqueue) == _FALSE) {
		_list *plist, *phead;

		phead = get_list_head(pqueue);
		plist = get_next(phead);
		pxmitbuf = LIST_CONTAINOR(plist, struct xmit_buf, list);
		rtw_list_delete(&pxmitbuf->list);
	}

	_exit_critical_bh(&pqueue->lock, &irql);

	return pxmitbuf;
}

static struct xmit_buf *dequeue_pending_xmitbuf_ext(
	struct xmit_priv *pxmitpriv)
{
	_irqL irql;
	struct xmit_buf *pxmitbuf;
	_queue *pqueue;

	pxmitbuf = NULL;
	pqueue = &pxmitpriv->pending_xmitbuf_queue;

	_enter_critical_bh(&pqueue->lock, &irql);

	if (_rtw_queue_empty(pqueue) == _FALSE) {
		_list *plist, *phead;
		u8 type = 0;

		phead = get_list_head(pqueue);
		plist = phead;
		do {
			plist = get_next(plist);
			if (plist == phead)
				break;

			pxmitbuf = LIST_CONTAINOR(plist, struct xmit_buf, list);

			if (pxmitbuf->buf_tag == XMITBUF_MGNT) {
				rtw_list_delete(&pxmitbuf->list);
				break;
			}
			pxmitbuf = NULL;
		} while (1);
	}

	_exit_critical_bh(&pqueue->lock, &irql);

	return pxmitbuf;
}

struct xmit_buf *select_and_dequeue_pending_xmitbuf(_adapter *padapter)
{
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct xmit_buf *pxmitbuf = NULL;

	if (_TRUE == rtw_is_xmit_blocked(padapter))
		return pxmitbuf;

	pxmitbuf = dequeue_pending_xmitbuf_ext(pxmitpriv);
	if (pxmitbuf == NULL && rtw_xmit_ac_blocked(padapter) != _TRUE)
		pxmitbuf = dequeue_pending_xmitbuf(pxmitpriv);

	return pxmitbuf;
}

sint check_pending_xmitbuf(
	struct xmit_priv *pxmitpriv)
{
	_irqL irql;
	_queue *pqueue;
	sint	ret = _FALSE;

	pqueue = &pxmitpriv->pending_xmitbuf_queue;

	_enter_critical_bh(&pqueue->lock, &irql);

	if (_rtw_queue_empty(pqueue) == _FALSE)
		ret = _TRUE;

	_exit_critical_bh(&pqueue->lock, &irql);

	return ret;
}

thread_return rtw_xmit_thread(thread_context context)
{
	s32 err;
	PADAPTER padapter;
#ifdef RTW_XMIT_THREAD_HIGH_PRIORITY
#ifdef PLATFORM_LINUX
	struct sched_param param = { .sched_priority = 1 };

	sched_setscheduler(current, SCHED_FIFO, &param);
#endif /* PLATFORM_LINUX */
#endif /* RTW_XMIT_THREAD_HIGH_PRIORITY */

	err = _SUCCESS;
	padapter = (PADAPTER)context;

	thread_enter("RTW_XMIT_THREAD");

	do {
		err = rtw_hal_xmit_thread_handler(padapter);
		flush_signals_thread();
	} while (_SUCCESS == err);

	RTW_INFO(FUNC_ADPT_FMT " Exit\n", FUNC_ADPT_ARG(padapter));

	rtw_thread_wait_stop();

	return 0;
}
#endif

#ifdef DBG_XMIT_BLOCK
void dump_xmit_block(void *sel, _adapter *padapter)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);

	RTW_PRINT_SEL(sel, "[XMIT-BLOCK] xmit_block :0x%02x\n", dvobj->xmit_block);
	if (dvobj->xmit_block & XMIT_BLOCK_REDLMEM)
		RTW_PRINT_SEL(sel, "Reason:%s\n", "XMIT_BLOCK_REDLMEM");
	if (dvobj->xmit_block & XMIT_BLOCK_SUSPEND)
		RTW_PRINT_SEL(sel, "Reason:%s\n", "XMIT_BLOCK_SUSPEND");
	if (dvobj->xmit_block == XMIT_BLOCK_NONE)
		RTW_PRINT_SEL(sel, "Reason:%s\n", "XMIT_BLOCK_NONE");
}
void dump_xmit_block_info(void *sel, const char *fun_name, _adapter *padapter)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);

	RTW_INFO("\n"ADPT_FMT" call %s\n", ADPT_ARG(padapter), fun_name);
	dump_xmit_block(sel, padapter);
}
#define DBG_XMIT_BLOCK_DUMP(adapter)	dump_xmit_block_info(RTW_DBGDUMP, __func__, adapter)
#endif

void rtw_set_xmit_block(_adapter *padapter, enum XMIT_BLOCK_REASON reason)
{
	_irqL irqL;
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);

	_enter_critical_bh(&dvobj->xmit_block_lock, &irqL);
	dvobj->xmit_block |= reason;
	_exit_critical_bh(&dvobj->xmit_block_lock, &irqL);

	#ifdef DBG_XMIT_BLOCK
	DBG_XMIT_BLOCK_DUMP(padapter);
	#endif
}

void rtw_clr_xmit_block(_adapter *padapter, enum XMIT_BLOCK_REASON reason)
{
	_irqL irqL;
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);

	_enter_critical_bh(&dvobj->xmit_block_lock, &irqL);
	dvobj->xmit_block &= ~reason;
	_exit_critical_bh(&dvobj->xmit_block_lock, &irqL);

	#ifdef DBG_XMIT_BLOCK
	DBG_XMIT_BLOCK_DUMP(padapter);
	#endif
}
bool rtw_is_xmit_blocked(_adapter *padapter)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);

	#ifdef DBG_XMIT_BLOCK
	DBG_XMIT_BLOCK_DUMP(padapter);
	#endif
	return ((dvobj->xmit_block) ? _TRUE : _FALSE);
}

bool rtw_xmit_ac_blocked(_adapter *adapter)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct rf_ctl_t *rfctl = adapter_to_rfctl(adapter);
	_adapter *iface;
	struct mlme_ext_priv *mlmeext;
	bool blocked = _FALSE;
	int i;
#ifdef DBG_CONFIG_ERROR_DETECT
#ifdef DBG_CONFIG_ERROR_RESET
#ifdef CONFIG_USB_HCI
	if (rtw_hal_sreset_inprogress(adapter) == _TRUE) {
		blocked = _TRUE;
		goto exit;
	}
#endif/* #ifdef CONFIG_USB_HCI */
#endif/* #ifdef DBG_CONFIG_ERROR_RESET */
#endif/* #ifdef DBG_CONFIG_ERROR_DETECT */

	if (rfctl->offch_state != OFFCHS_NONE
		#ifdef CONFIG_DFS
		|| IS_RADAR_DETECTED(rfctl) || rfctl->csa_ch
		#endif
	) {
		blocked = _TRUE;
		goto exit;
	}

	for (i = 0; i < dvobj->iface_nums; i++) {
		iface = dvobj->padapters[i];
		mlmeext = &iface->mlmeextpriv;

		/* check scan state */
		if (mlmeext_scan_state(mlmeext) != SCAN_DISABLE
			&& mlmeext_scan_state(mlmeext) != SCAN_BACK_OP
		) {
			blocked = _TRUE;
			goto exit;
		}

		if (mlmeext_scan_state(mlmeext) == SCAN_BACK_OP
			&& !mlmeext_chk_scan_backop_flags(mlmeext, SS_BACKOP_TX_RESUME)
		) {
			blocked = _TRUE;
			goto exit;
		}
	}

#ifdef CONFIG_MCC_MODE
	if (MCC_EN(adapter)) {
		if (rtw_hal_check_mcc_status(adapter, MCC_STATUS_DOING_MCC)) {
			if (MCC_STOP(adapter)) {
				blocked = _TRUE;
				goto exit;
			}
		}
	}
#endif /*  CONFIG_MCC_MODE */

exit:
	return blocked;
}

#ifdef CONFIG_TX_AMSDU
void rtw_amsdu_vo_timeout_handler(void *FunctionContext)
{
	_adapter *adapter = (_adapter *)FunctionContext;

	adapter->xmitpriv.amsdu_vo_timeout = RTW_AMSDU_TIMER_TIMEOUT;

	tasklet_hi_schedule(&adapter->xmitpriv.xmit_tasklet);
}

void rtw_amsdu_vi_timeout_handler(void *FunctionContext)
{
	_adapter *adapter = (_adapter *)FunctionContext;

	adapter->xmitpriv.amsdu_vi_timeout = RTW_AMSDU_TIMER_TIMEOUT;

	tasklet_hi_schedule(&adapter->xmitpriv.xmit_tasklet);
}

void rtw_amsdu_be_timeout_handler(void *FunctionContext)
{
	_adapter *adapter = (_adapter *)FunctionContext;

	adapter->xmitpriv.amsdu_be_timeout = RTW_AMSDU_TIMER_TIMEOUT;

	if (printk_ratelimit())
		RTW_DBG("%s Timeout!\n",__FUNCTION__);

	tasklet_hi_schedule(&adapter->xmitpriv.xmit_tasklet);
}

void rtw_amsdu_bk_timeout_handler(void *FunctionContext)
{
	_adapter *adapter = (_adapter *)FunctionContext;

	adapter->xmitpriv.amsdu_bk_timeout = RTW_AMSDU_TIMER_TIMEOUT;

	tasklet_hi_schedule(&adapter->xmitpriv.xmit_tasklet);
}

u8 rtw_amsdu_get_timer_status(_adapter *padapter, u8 priority)
{
	struct xmit_priv        *pxmitpriv = &padapter->xmitpriv;

	u8 status =  RTW_AMSDU_TIMER_UNSET;

	switch(priority)
	{
		case 1:
		case 2:
			status = pxmitpriv->amsdu_bk_timeout;
			break;
		case 4:
		case 5:
			status = pxmitpriv->amsdu_vi_timeout;
			break;
		case 6:
		case 7:
			status = pxmitpriv->amsdu_vo_timeout;
			break;
		case 0:
		case 3:
		default:
			status = pxmitpriv->amsdu_be_timeout;
			break;
	}
	return status;
}

void rtw_amsdu_set_timer_status(_adapter *padapter, u8 priority, u8 status)
{
	struct xmit_priv        *pxmitpriv = &padapter->xmitpriv;

	switch(priority)
	{
		case 1:
		case 2:
			pxmitpriv->amsdu_bk_timeout = status;
			break;
		case 4:
		case 5:
			pxmitpriv->amsdu_vi_timeout = status;
			break;
		case 6:
		case 7:
			pxmitpriv->amsdu_vo_timeout = status;
			break;
		case 0:
		case 3:
		default:
			pxmitpriv->amsdu_be_timeout = status;
			break;
	}
}

void rtw_amsdu_set_timer(_adapter *padapter, u8 priority)
{
	struct xmit_priv        *pxmitpriv = &padapter->xmitpriv;

	_timer* amsdu_timer = NULL;

	switch(priority)
	{
		case 1:
		case 2:
			amsdu_timer = &pxmitpriv->amsdu_bk_timer;
			break;
		case 4:
		case 5:
			amsdu_timer = &pxmitpriv->amsdu_vi_timer;
			break;
		case 6:
		case 7:
			amsdu_timer = &pxmitpriv->amsdu_vo_timer;
			break;
		case 0:
		case 3:
		default:
			amsdu_timer = &pxmitpriv->amsdu_be_timer;
			break;
	}
	_set_timer(amsdu_timer, 1);
}

void rtw_amsdu_cancel_timer(_adapter *padapter, u8 priority)
{
	struct xmit_priv        *pxmitpriv = &padapter->xmitpriv;
	_timer* amsdu_timer = NULL;

	switch(priority)
	{
		case 1:
		case 2:
			amsdu_timer = &pxmitpriv->amsdu_bk_timer;
			break;
		case 4:
		case 5:
			amsdu_timer = &pxmitpriv->amsdu_vi_timer;
			break;
		case 6:
		case 7:
			amsdu_timer = &pxmitpriv->amsdu_vo_timer;
			break;
		case 0:
		case 3:
		default:
			amsdu_timer = &pxmitpriv->amsdu_be_timer;
			break;
	}
	_cancel_timer_ex(amsdu_timer);
}
#endif /* CONFIG_TX_AMSDU */

#ifdef DBG_TXBD_DESC_DUMP
static struct rtw_tx_desc_backup tx_backup[HW_QUEUE_ENTRY][TX_BAK_FRMAE_CNT];
static u8 backup_idx[HW_QUEUE_ENTRY];

void rtw_tx_desc_backup(_adapter *padapter, struct xmit_frame *pxmitframe, u8 desc_size, u8 hwq)
{
	u32 tmp32;
	u8 *pxmit_buf;

	if (rtw_get_hw_init_completed(padapter) == _FALSE)
		return;

	pxmit_buf = pxmitframe->pxmitbuf->pbuf;

	_rtw_memcpy(tx_backup[hwq][backup_idx[hwq]].tx_bak_desc, pxmit_buf, desc_size);
	_rtw_memcpy(tx_backup[hwq][backup_idx[hwq]].tx_bak_data_hdr, pxmit_buf+desc_size, TX_BAK_DATA_LEN);

	tmp32 = rtw_read32(padapter, get_txbd_rw_reg(hwq));

	tx_backup[hwq][backup_idx[hwq]].tx_bak_rp = (tmp32>>16)&0xfff;
	tx_backup[hwq][backup_idx[hwq]].tx_bak_wp = tmp32&0xfff;

	tx_backup[hwq][backup_idx[hwq]].tx_desc_size = desc_size;

	backup_idx[hwq] = (backup_idx[hwq] + 1) % TX_BAK_FRMAE_CNT;
}

void rtw_tx_desc_backup_reset(void)
{
	int i, j;

	for (i = 0; i < HW_QUEUE_ENTRY; i++) {
		for (j = 0; j < TX_BAK_FRMAE_CNT; j++)
			_rtw_memset(&tx_backup[i][j], 0, sizeof(struct rtw_tx_desc_backup));

		backup_idx[i] = 0;
	}
}

u8 rtw_get_tx_desc_backup(_adapter *padapter, u8 hwq, struct rtw_tx_desc_backup **pbak)
{
	*pbak = &tx_backup[hwq][0];

	return backup_idx[hwq];
}
#endif

#ifdef CONFIG_PCI_TX_POLLING
void rtw_tx_poll_init(_adapter *padapter)
{
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	_timer* timer = &pxmitpriv->tx_poll_timer;

	if (!is_primary_adapter(padapter))
		return;

	if (timer->function != NULL) {
		RTW_INFO("tx polling timer has been init.\n");
		return;
	}

	rtw_init_timer(timer, padapter, rtw_tx_poll_timeout_handler, padapter);
	rtw_tx_poll_timer_set(padapter, 1);
	RTW_INFO("Tx poll timer init!\n");
}

void rtw_tx_poll_timeout_handler(void *FunctionContext)
{
	_adapter *adapter = (_adapter *)FunctionContext;

	rtw_tx_poll_timer_set(adapter, 1);

	if (adapter->hal_func.tx_poll_handler)
		adapter->hal_func.tx_poll_handler(adapter);
	else
		RTW_WARN("hal ops: tx_poll_handler is NULL\n");
}

void rtw_tx_poll_timer_set(_adapter *padapter, u32 delay)
{
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	_timer* timer = NULL;

	timer = &pxmitpriv->tx_poll_timer;
	_set_timer(timer, delay);
}

void rtw_tx_poll_timer_cancel(_adapter *padapter)
{
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	_timer* timer = NULL;

	if (!is_primary_adapter(padapter))
		return;

	timer = &pxmitpriv->tx_poll_timer;
	_cancel_timer_ex(timer);
	timer->function = NULL;
	RTW_INFO("Tx poll timer cancel !\n");
}
#endif /* CONFIG_PCI_TX_POLLING */

void rtw_sctx_init(struct submit_ctx *sctx, int timeout_ms)
{
	sctx->timeout_ms = timeout_ms;
	sctx->submit_time = rtw_get_current_time();
#ifdef PLATFORM_LINUX /* TODO: add condition wating interface for other os */
	init_completion(&sctx->done);
#endif
	sctx->status = RTW_SCTX_SUBMITTED;
}

int rtw_sctx_wait(struct submit_ctx *sctx, const char *msg)
{
	int ret = _FAIL;
	unsigned long expire;
	int status = 0;

#ifdef PLATFORM_LINUX
	expire = sctx->timeout_ms ? msecs_to_jiffies(sctx->timeout_ms) : MAX_SCHEDULE_TIMEOUT;
	if (!wait_for_completion_timeout(&sctx->done, expire)) {
		/* timeout, do something?? */
		status = RTW_SCTX_DONE_TIMEOUT;
		RTW_INFO("%s timeout: %s\n", __func__, msg);
	} else
		status = sctx->status;
#endif

	if (status == RTW_SCTX_DONE_SUCCESS)
		ret = _SUCCESS;

	return ret;
}

bool rtw_sctx_chk_waring_status(int status)
{
	switch (status) {
	case RTW_SCTX_DONE_UNKNOWN:
	case RTW_SCTX_DONE_BUF_ALLOC:
	case RTW_SCTX_DONE_BUF_FREE:

	case RTW_SCTX_DONE_DRV_STOP:
	case RTW_SCTX_DONE_DEV_REMOVE:
		return _TRUE;
	default:
		return _FALSE;
	}
}

void rtw_sctx_done_err(struct submit_ctx **sctx, int status)
{
	if (*sctx) {
		if (rtw_sctx_chk_waring_status(status))
			RTW_INFO("%s status:%d\n", __func__, status);
		(*sctx)->status = status;
#ifdef PLATFORM_LINUX
		complete(&((*sctx)->done));
#endif
		*sctx = NULL;
	}
}

void rtw_sctx_done(struct submit_ctx **sctx)
{
	rtw_sctx_done_err(sctx, RTW_SCTX_DONE_SUCCESS);
}

#ifdef CONFIG_XMIT_ACK
int rtw_ack_tx_wait(struct xmit_priv *pxmitpriv, u32 timeout_ms)
{
	struct submit_ctx *pack_tx_ops = &pxmitpriv->ack_tx_ops;

	pack_tx_ops->submit_time = rtw_get_current_time();
	pack_tx_ops->timeout_ms = timeout_ms;
	pack_tx_ops->status = RTW_SCTX_SUBMITTED;

	return rtw_sctx_wait(pack_tx_ops, __func__);
}

void rtw_ack_tx_done(struct xmit_priv *pxmitpriv, int status)
{
	struct submit_ctx *pack_tx_ops = &pxmitpriv->ack_tx_ops;

	if (pxmitpriv->ack_tx)
		rtw_sctx_done_err(&pack_tx_ops, status);
	else
		RTW_INFO("%s ack_tx not set\n", __func__);
}
#endif /* CONFIG_XMIT_ACK */
