// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/

#include <drv_types.h>
#include <rtl8723b_hal.h>

static u8 rtw_sdio_wait_enough_TxOQT_space(struct adapter *padapter, u8 agg_num)
{
	u32 n = 0;
	struct hal_com_data *pHalData = GET_HAL_DATA(padapter);

	while (pHalData->SdioTxOQTFreeSpace < agg_num) {
		if (
			(padapter->bSurpriseRemoved) ||
			(padapter->bDriverStopped)
		)
			return false;

		HalQueryTxOQTBufferStatus8723BSdio(padapter);

		if ((++n % 60) == 0) {
			msleep(1);
			/* yield(); */
		}
	}

	pHalData->SdioTxOQTFreeSpace -= agg_num;

	return true;
}

static s32 rtl8723_dequeue_writeport(struct adapter *padapter)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct dvobj_priv *pdvobjpriv = adapter_to_dvobj(padapter);
	struct xmit_buf *pxmitbuf;
	struct adapter *pri_padapter = padapter;
	s32 ret = 0;
	u8 PageIdx = 0;
	u32 deviceId;
	u8 bUpdatePageNum = false;

	ret = ret || check_fwstate(pmlmepriv, _FW_UNDER_SURVEY);

	if (ret)
		pxmitbuf = dequeue_pending_xmitbuf_under_survey(pxmitpriv);
	else
		pxmitbuf = dequeue_pending_xmitbuf(pxmitpriv);

	if (!pxmitbuf)
		return true;

	deviceId = ffaddr2deviceId(pdvobjpriv, pxmitbuf->ff_hwaddr);

	/*  translate fifo addr to queue index */
	switch (deviceId) {
	case WLAN_TX_HIQ_DEVICE_ID:
		PageIdx = HI_QUEUE_IDX;
		break;

	case WLAN_TX_MIQ_DEVICE_ID:
		PageIdx = MID_QUEUE_IDX;
		break;

	case WLAN_TX_LOQ_DEVICE_ID:
		PageIdx = LOW_QUEUE_IDX;
		break;
	}

query_free_page:
	/*  check if hardware tx fifo page is enough */
	if (!rtw_hal_sdio_query_tx_freepage(pri_padapter, PageIdx, pxmitbuf->pg_num)) {
		if (!bUpdatePageNum) {
			/*  Total number of page is NOT available, so update current FIFO status */
			HalQueryTxBufferStatus8723BSdio(padapter);
			bUpdatePageNum = true;
			goto query_free_page;
		} else {
			bUpdatePageNum = false;
			enqueue_pending_xmitbuf_to_head(pxmitpriv, pxmitbuf);
			return true;
		}
	}

	if (
		(padapter->bSurpriseRemoved) ||
		(padapter->bDriverStopped)
	)
		goto free_xmitbuf;

	if (rtw_sdio_wait_enough_TxOQT_space(padapter, pxmitbuf->agg_num) == false)
		goto free_xmitbuf;

	traffic_check_for_leave_lps(padapter, true, pxmitbuf->agg_num);

	rtw_write_port(padapter, deviceId, pxmitbuf->len, (u8 *)pxmitbuf);

	rtw_hal_sdio_update_tx_freepage(pri_padapter, PageIdx, pxmitbuf->pg_num);

free_xmitbuf:
	/* rtw_free_xmitframe(pxmitpriv, pframe); */
	/* pxmitbuf->priv_data = NULL; */
	rtw_free_xmitbuf(pxmitpriv, pxmitbuf);

	return _FAIL;
}

/*
 * Description
 *Transmit xmitbuf to hardware tx fifo
 *
 * Return
 *_SUCCESS	ok
 *_FAIL		something error
 */
s32 rtl8723bs_xmit_buf_handler(struct adapter *padapter)
{
	struct xmit_priv *pxmitpriv;
	u8 queue_empty, queue_pending;
	s32 ret;


	pxmitpriv = &padapter->xmitpriv;

	if (wait_for_completion_interruptible(&pxmitpriv->xmit_comp)) {
		netdev_emerg(padapter->pnetdev,
			     "%s: down SdioXmitBufSema fail!\n", __func__);
		return _FAIL;
	}

	ret = (padapter->bDriverStopped) || (padapter->bSurpriseRemoved);
	if (ret)
		return _FAIL;

	queue_pending = check_pending_xmitbuf(pxmitpriv);

	if (!queue_pending)
		return _SUCCESS;

	ret = rtw_register_tx_alive(padapter);
	if (ret != _SUCCESS) {
		return _SUCCESS;
	}

	do {
		queue_empty = rtl8723_dequeue_writeport(padapter);
/* 	dump secondary adapter xmitbuf */
	} while (!queue_empty);

	rtw_unregister_tx_alive(padapter);

	return _SUCCESS;
}

/*
 * Description:
 *Aggregation packets and send to hardware
 *
 * Return:
 *0	Success
 *-1	Hardware resource(TX FIFO) not ready
 *-2	Software resource(xmitbuf) not ready
 */
static s32 xmit_xmitframes(struct adapter *padapter, struct xmit_priv *pxmitpriv)
{
	s32 err, ret;
	u32 k = 0;
	struct hw_xmit *hwxmits, *phwxmit;
	u8 idx, hwentry;
	struct tx_servq *ptxservq;
	struct list_head *sta_plist, *sta_phead, *frame_plist, *frame_phead, *tmp;
	struct xmit_frame *pxmitframe;
	struct __queue *pframe_queue;
	struct xmit_buf *pxmitbuf;
	u32 txlen, max_xmit_len;
	u8 txdesc_size = TXDESC_SIZE;
	int inx[4];

	err = 0;
	hwxmits = pxmitpriv->hwxmits;
	hwentry = pxmitpriv->hwxmit_entry;
	ptxservq = NULL;
	pxmitframe = NULL;
	pframe_queue = NULL;
	pxmitbuf = NULL;

	if (padapter->registrypriv.wifi_spec == 1) {
		for (idx = 0; idx < 4; idx++)
			inx[idx] = pxmitpriv->wmm_para_seq[idx];
	} else {
		inx[0] = 0;
		inx[1] = 1;
		inx[2] = 2;
		inx[3] = 3;
	}

	/*  0(VO), 1(VI), 2(BE), 3(BK) */
	for (idx = 0; idx < hwentry; idx++) {
		phwxmit = hwxmits + inx[idx];

		if (
			(check_pending_xmitbuf(pxmitpriv)) &&
			(padapter->mlmepriv.LinkDetectInfo.bHigherBusyTxTraffic)
		) {
			if ((phwxmit->accnt > 0) && (phwxmit->accnt < 5)) {
				err = -2;
				break;
			}
		}

		max_xmit_len = rtw_hal_get_sdio_tx_max_length(padapter, inx[idx]);

		spin_lock_bh(&pxmitpriv->lock);

		sta_phead = get_list_head(phwxmit->sta_queue);
		/* because stop_sta_xmit may delete sta_plist at any time */
		/* so we should add lock here, or while loop can not exit */
		list_for_each_safe(sta_plist, tmp, sta_phead) {
			ptxservq = list_entry(sta_plist, struct tx_servq,
					      tx_pending);

			pframe_queue = &ptxservq->sta_pending;

			frame_phead = get_list_head(pframe_queue);

			while (list_empty(frame_phead) == false) {
				frame_plist = get_next(frame_phead);
				pxmitframe = container_of(frame_plist, struct xmit_frame, list);

				/*  check xmit_buf size enough or not */
				txlen = txdesc_size + rtw_wlan_pkt_size(pxmitframe);
				if (!pxmitbuf ||
					((_RND(pxmitbuf->len, 8) + txlen) > max_xmit_len) ||
					(k >= (rtw_hal_sdio_max_txoqt_free_space(padapter) - 1))
				) {
					if (pxmitbuf) {
						/* pxmitbuf->priv_data will be NULL, and will crash here */
						if (pxmitbuf->len > 0 &&
						    pxmitbuf->priv_data) {
							struct xmit_frame *pframe;
							pframe = (struct xmit_frame *)pxmitbuf->priv_data;
							pframe->agg_num = k;
							pxmitbuf->agg_num = k;
							rtl8723b_update_txdesc(pframe, pframe->buf_addr);
							rtw_free_xmitframe(pxmitpriv, pframe);
							pxmitbuf->priv_data = NULL;
							enqueue_pending_xmitbuf(pxmitpriv, pxmitbuf);
							/* can not yield under lock */
							/* yield(); */
						} else
							rtw_free_xmitbuf(pxmitpriv, pxmitbuf);
					}

					pxmitbuf = rtw_alloc_xmitbuf(pxmitpriv);
					if (!pxmitbuf) {
#ifdef DBG_XMIT_BUF
						netdev_err(padapter->pnetdev,
							   "%s: xmit_buf is not enough!\n",
							   __func__);
#endif
						err = -2;
						complete(&(pxmitpriv->xmit_comp));
						break;
					}
					k = 0;
				}

				/*  ok to send, remove frame from queue */
				if (check_fwstate(&padapter->mlmepriv, WIFI_AP_STATE) == true)
					if (
						(pxmitframe->attrib.psta->state & WIFI_SLEEP_STATE) &&
						(pxmitframe->attrib.triggered == 0)
					)
						break;

				list_del_init(&pxmitframe->list);
				ptxservq->qcnt--;
				phwxmit->accnt--;

				if (k == 0) {
					pxmitbuf->ff_hwaddr = rtw_get_ff_hwaddr(pxmitframe);
					pxmitbuf->priv_data = (u8 *)pxmitframe;
				}

				/*  coalesce the xmitframe to xmitbuf */
				pxmitframe->pxmitbuf = pxmitbuf;
				pxmitframe->buf_addr = pxmitbuf->ptail;

				ret = rtw_xmitframe_coalesce(padapter, pxmitframe->pkt, pxmitframe);
				if (ret == _FAIL) {
					netdev_err(padapter->pnetdev,
						   "%s: coalesce FAIL!",
						   __func__);
					/*  Todo: error handler */
				} else {
					k++;
					if (k != 1)
						rtl8723b_update_txdesc(pxmitframe, pxmitframe->buf_addr);
					rtw_count_tx_stats(padapter, pxmitframe, pxmitframe->attrib.last_txcmdsz);

					txlen = txdesc_size + pxmitframe->attrib.last_txcmdsz;
					pxmitframe->pg_num = (txlen + 127) / 128;
					pxmitbuf->pg_num += (txlen + 127) / 128;
					pxmitbuf->ptail += _RND(txlen, 8); /*  round to 8 bytes alignment */
					pxmitbuf->len = _RND(pxmitbuf->len, 8) + txlen;
				}

				if (k != 1)
					rtw_free_xmitframe(pxmitpriv, pxmitframe);
				pxmitframe = NULL;
			}

			if (list_empty(&pframe_queue->queue))
				list_del_init(&ptxservq->tx_pending);

			if (err)
				break;
		}
		spin_unlock_bh(&pxmitpriv->lock);

		/*  dump xmit_buf to hw tx fifo */
		if (pxmitbuf) {
			if (pxmitbuf->len > 0) {
				struct xmit_frame *pframe;
				pframe = (struct xmit_frame *)pxmitbuf->priv_data;
				pframe->agg_num = k;
				pxmitbuf->agg_num = k;
				rtl8723b_update_txdesc(pframe, pframe->buf_addr);
				rtw_free_xmitframe(pxmitpriv, pframe);
				pxmitbuf->priv_data = NULL;
				enqueue_pending_xmitbuf(pxmitpriv, pxmitbuf);
				yield();
			} else
				rtw_free_xmitbuf(pxmitpriv, pxmitbuf);
			pxmitbuf = NULL;
		}

		if (err)
			break;
	}

	return err;
}

/*
 * Description
 *Transmit xmitframe from queue
 *
 * Return
 *_SUCCESS	ok
 *_FAIL		something error
 */
static s32 rtl8723bs_xmit_handler(struct adapter *padapter)
{
	struct xmit_priv *pxmitpriv;
	s32 ret;


	pxmitpriv = &padapter->xmitpriv;

	if (wait_for_completion_interruptible(&pxmitpriv->SdioXmitStart)) {
		netdev_emerg(padapter->pnetdev, "%s: SdioXmitStart fail!\n",
			     __func__);
		return _FAIL;
	}

next:
	if (
		(padapter->bDriverStopped) ||
		(padapter->bSurpriseRemoved)
	)
		return _FAIL;

	spin_lock_bh(&pxmitpriv->lock);
	ret = rtw_txframes_pending(padapter);
	spin_unlock_bh(&pxmitpriv->lock);
	if (ret == 0) {
		return _SUCCESS;
	}

	/*  dequeue frame and write to hardware */

	ret = xmit_xmitframes(padapter, pxmitpriv);
	if (ret == -2) {
		/* here sleep 1ms will cause big TP loss of TX */
		/* from 50+ to 40+ */
		if (padapter->registrypriv.wifi_spec)
			msleep(1);
		else
			yield();
		goto next;
	}

	spin_lock_bh(&pxmitpriv->lock);
	ret = rtw_txframes_pending(padapter);
	spin_unlock_bh(&pxmitpriv->lock);
	if (ret == 1) {
		goto next;
	}

	return _SUCCESS;
}

int rtl8723bs_xmit_thread(void *context)
{
	s32 ret;
	struct adapter *padapter;
	struct xmit_priv *pxmitpriv;

	ret = _SUCCESS;
	padapter = context;
	pxmitpriv = &padapter->xmitpriv;

	allow_signal(SIGTERM);

	do {
		ret = rtl8723bs_xmit_handler(padapter);
		if (signal_pending(current)) {
			flush_signals(current);
		}
	} while (_SUCCESS == ret);

	complete(&pxmitpriv->SdioXmitTerminate);

	return 0;
}

s32 rtl8723bs_mgnt_xmit(
	struct adapter *padapter, struct xmit_frame *pmgntframe
)
{
	s32 ret = _SUCCESS;
	struct pkt_attrib *pattrib;
	struct xmit_buf *pxmitbuf;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct dvobj_priv *pdvobjpriv = adapter_to_dvobj(padapter);
	u8 *pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	u8 txdesc_size = TXDESC_SIZE;

	pattrib = &pmgntframe->attrib;
	pxmitbuf = pmgntframe->pxmitbuf;

	rtl8723b_update_txdesc(pmgntframe, pmgntframe->buf_addr);

	pxmitbuf->len = txdesc_size + pattrib->last_txcmdsz;
	pxmitbuf->pg_num = (pxmitbuf->len + 127) / 128; /*  128 is tx page size */
	pxmitbuf->ptail = pmgntframe->buf_addr + pxmitbuf->len;
	pxmitbuf->ff_hwaddr = rtw_get_ff_hwaddr(pmgntframe);

	rtw_count_tx_stats(padapter, pmgntframe, pattrib->last_txcmdsz);

	rtw_free_xmitframe(pxmitpriv, pmgntframe);

	pxmitbuf->priv_data = NULL;

	if (GetFrameSubType(pframe) == WIFI_BEACON) { /* dump beacon directly */
		ret = rtw_write_port(padapter, pdvobjpriv->Queue2Pipe[pxmitbuf->ff_hwaddr], pxmitbuf->len, (u8 *)pxmitbuf);
		if (ret != _SUCCESS)
			rtw_sctx_done_err(&pxmitbuf->sctx, RTW_SCTX_DONE_WRITE_PORT_ERR);

		rtw_free_xmitbuf(pxmitpriv, pxmitbuf);
	} else
		enqueue_pending_xmitbuf(pxmitpriv, pxmitbuf);

	return ret;
}

/*
 * Description:
 *Handle xmitframe(packet) come from rtw_xmit()
 *
 * Return:
 *true	dump packet directly ok
 *false	enqueue, temporary can't transmit packets to hardware
 */
s32 rtl8723bs_hal_xmit(
	struct adapter *padapter, struct xmit_frame *pxmitframe
)
{
	struct xmit_priv *pxmitpriv;
	s32 err;


	pxmitframe->attrib.qsel = pxmitframe->attrib.priority;
	pxmitpriv = &padapter->xmitpriv;

	if (
		(pxmitframe->frame_tag == DATA_FRAMETAG) &&
		(pxmitframe->attrib.ether_type != 0x0806) &&
		(pxmitframe->attrib.ether_type != 0x888e) &&
		(pxmitframe->attrib.dhcp_pkt != 1)
	) {
		if (padapter->mlmepriv.LinkDetectInfo.bBusyTraffic)
			rtw_issue_addbareq_cmd(padapter, pxmitframe);
	}

	spin_lock_bh(&pxmitpriv->lock);
	err = rtw_xmitframe_enqueue(padapter, pxmitframe);
	spin_unlock_bh(&pxmitpriv->lock);
	if (err != _SUCCESS) {
		rtw_free_xmitframe(pxmitpriv, pxmitframe);

		pxmitpriv->tx_drop++;
		return true;
	}

	complete(&pxmitpriv->SdioXmitStart);

	return false;
}

s32	rtl8723bs_hal_xmitframe_enqueue(
	struct adapter *padapter, struct xmit_frame *pxmitframe
)
{
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	s32 err;

	err = rtw_xmitframe_enqueue(padapter, pxmitframe);
	if (err != _SUCCESS) {
		rtw_free_xmitframe(pxmitpriv, pxmitframe);

		pxmitpriv->tx_drop++;
	} else {
		complete(&pxmitpriv->SdioXmitStart);
	}

	return err;

}

/*
 * Return
 *_SUCCESS	start thread ok
 *_FAIL		start thread fail
 *
 */
s32 rtl8723bs_init_xmit_priv(struct adapter *padapter)
{
	struct xmit_priv *xmitpriv = &padapter->xmitpriv;
	struct hal_com_data *phal;


	phal = GET_HAL_DATA(padapter);

	spin_lock_init(&phal->SdioTxFIFOFreePageLock);
	init_completion(&xmitpriv->SdioXmitStart);
	init_completion(&xmitpriv->SdioXmitTerminate);

	return _SUCCESS;
}

void rtl8723bs_free_xmit_priv(struct adapter *padapter)
{
	struct xmit_priv *pxmitpriv;
	struct xmit_buf *pxmitbuf;
	struct __queue *pqueue;
	struct list_head *plist, *phead;
	struct list_head tmplist;


	pxmitpriv = &padapter->xmitpriv;
	pqueue = &pxmitpriv->pending_xmitbuf_queue;
	phead = get_list_head(pqueue);
	INIT_LIST_HEAD(&tmplist);

	spin_lock_bh(&pqueue->lock);
	if (!list_empty(&pqueue->queue)) {
		/*  Insert tmplist to end of queue, and delete phead */
		/*  then tmplist become head of queue. */
		list_add_tail(&tmplist, phead);
		list_del_init(phead);
	}
	spin_unlock_bh(&pqueue->lock);

	phead = &tmplist;
	while (list_empty(phead) == false) {
		plist = get_next(phead);
		list_del_init(plist);

		pxmitbuf = container_of(plist, struct xmit_buf, list);
		rtw_free_xmitframe(pxmitpriv, (struct xmit_frame *)pxmitbuf->priv_data);
		pxmitbuf->priv_data = NULL;
		rtw_free_xmitbuf(pxmitpriv, pxmitbuf);
	}
}
