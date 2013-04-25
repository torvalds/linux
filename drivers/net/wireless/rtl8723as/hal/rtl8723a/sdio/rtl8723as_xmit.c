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
#define _RTL8723AS_XMIT_C_

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <sdio_ops.h>
#include <rtl8723a_hal.h>

#define SDIO_TX_AGG_MAX	5

static void enqueue_pending_xmitbuf(
	struct xmit_priv *pxmitpriv,
	struct xmit_buf *pxmitbuf)
{
	_irqL irql;
	_queue *pqueue;


	pqueue = &pxmitpriv->pending_xmitbuf_queue;

	_enter_critical_bh(&pqueue->lock, &irql);
	rtw_list_delete(&pxmitbuf->list);
	rtw_list_insert_tail(&pxmitbuf->list, get_list_head(pqueue));
	_exit_critical_bh(&pqueue->lock, &irql);

	_rtw_up_sema(&pxmitpriv->xmit_sema);
}

static struct xmit_buf* dequeue_pending_xmitbuf(
	struct xmit_priv *pxmitpriv)
{
	_irqL irql;
	struct xmit_buf *pxmitbuf;
	_queue *pqueue;


	pxmitbuf = NULL;
	pqueue = &pxmitpriv->pending_xmitbuf_queue;

	_enter_critical_bh(&pqueue->lock, &irql);

	if (_rtw_queue_empty(pqueue) == _FALSE)
	{
		_list *plist, *phead;

		phead = get_list_head(pqueue);
		plist = get_next(phead);
		pxmitbuf = LIST_CONTAINOR(plist, struct xmit_buf, list);
		rtw_list_delete(&pxmitbuf->list);
	}

	_exit_critical_bh(&pqueue->lock, &irql);

	return pxmitbuf;
}

static struct xmit_buf* dequeue_pending_xmitbuf_under_survey(
	struct xmit_priv *pxmitpriv)
{
	_irqL irql;
	struct xmit_buf *pxmitbuf;
	struct xmit_frame *pxmitframe;
	_queue *pqueue;


	pxmitbuf = NULL;
	pqueue = &pxmitpriv->pending_xmitbuf_queue;

	_enter_critical_bh(&pqueue->lock, &irql);

	if (_rtw_queue_empty(pqueue) == _FALSE)
	{
		_list *plist, *phead;
		u8 type;

		phead = get_list_head(pqueue);
		plist = phead;
		do {
			plist = get_next(plist);
			if (plist == phead) break;
			pxmitbuf = LIST_CONTAINOR(plist, struct xmit_buf, list);
			pxmitframe = (struct xmit_frame*)pxmitbuf->priv_data;
			type = GetFrameSubType(pxmitframe->buf_addr + TXDESC_OFFSET);
			if ((type == WIFI_PROBEREQ) ||
				(type == WIFI_DATA_NULL) ||
				(type == WIFI_QOS_DATA_NULL))
			{
				rtw_list_delete(&pxmitbuf->list);
				break;
			}
			pxmitbuf = NULL;
		} while (1);
	}

	_exit_critical_bh(&pqueue->lock, &irql);

	return pxmitbuf;
}

static void freequeue_pending_xmitbuf(struct xmit_priv *pxmitpriv)
{
	struct xmit_buf *pxmitbuf;
	struct xmit_frame *pframe;


	do {
		pxmitbuf = dequeue_pending_xmitbuf(pxmitpriv);
		if (pxmitbuf == NULL) break;
		pframe = (struct xmit_frame*)pxmitbuf->priv_data;
		rtw_free_xmitframe(pxmitpriv, pframe);
		pxmitbuf->priv_data = NULL;
		rtw_free_xmitbuf(pxmitpriv, pxmitbuf);
	} while (1);
}

/*
 * Description
 *	Transmit xmitbuf to hardware tx fifo
 *
 * Return
 *	_SUCCESS	ok
 *	_FAIL		something error
 */
s32 rtl8723as_xmit_buf_handler(PADAPTER padapter)
{
	PHAL_DATA_TYPE phal;
	struct mlme_priv *pmlmepriv;
	struct xmit_priv *pxmitpriv;
	struct xmit_buf *pxmitbuf;
	struct xmit_frame *pframe;
	u8 *freePage;
	u32 requiredPage;
	u8 PageIdx;
	_irqL irql;
	u32 n;
	s32 ret;


	phal = GET_HAL_DATA(padapter);
	pmlmepriv = &padapter->mlmepriv;
	pxmitpriv = &padapter->xmitpriv;
	freePage = phal->SdioTxFIFOFreePage;

	ret = _rtw_down_sema(&pxmitpriv->xmit_sema);
	if (_FAIL == ret) {
		RT_TRACE(_module_hal_xmit_c_, _drv_emerg_,
				 ("%s: down SdioXmitBufSema fail!\n", __FUNCTION__));
		freequeue_pending_xmitbuf(pxmitpriv);
		return _FAIL;
	}

	ret = (padapter->bDriverStopped == _TRUE) || (padapter->bSurpriseRemoved == _TRUE);
	if (ret) {
		RT_TRACE(_module_hal_xmit_c_, _drv_notice_,
				 ("%s: bDriverStopped(%d) bSurpriseRemoved(%d)!\n",
				  __FUNCTION__, padapter->bDriverStopped, padapter->bSurpriseRemoved));
		freequeue_pending_xmitbuf(pxmitpriv);
		return _FAIL;
	}

#ifdef CONFIG_LPS_LCLK
	ret = rtw_register_tx_alive(padapter);
	if (ret != _SUCCESS) {
		RT_TRACE(_module_hal_xmit_c_, _drv_notice_,
				 ("%s: wait to leave LPS_LCLK\n", __FUNCTION__));
		return _SUCCESS;
	}
#endif

	do {
		ret = check_fwstate(pmlmepriv, _FW_UNDER_SURVEY);
		if (_TRUE == ret)
			pxmitbuf = dequeue_pending_xmitbuf_under_survey(pxmitpriv);
		else
			pxmitbuf = dequeue_pending_xmitbuf(pxmitpriv);
		if (pxmitbuf == NULL) break;
		pframe = (struct xmit_frame*)pxmitbuf->priv_data;
		requiredPage = pframe->pg_num;

		// translate fifo addr to queue index
		switch (pxmitbuf->ff_hwaddr)
		{
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

		// check if hardware tx fifo page is enough
		n = 0;
//		_enter_critical_bh(&phal->SdioTxFIFOFreePageLock, &irql);
		do {
			if (requiredPage <= freePage[PageIdx]) {
				freePage[PageIdx] -= requiredPage;
				break;
			}

			// The number of page which public page included is available.
			if ((freePage[PageIdx] + freePage[PUBLIC_QUEUE_IDX]) > (requiredPage + 1))
			{
				u8 requiredPublicPage;

				requiredPublicPage = requiredPage - freePage[PageIdx];
				freePage[PageIdx] = 0;
				freePage[PUBLIC_QUEUE_IDX] -= requiredPublicPage;
				break;
			}
//			_exit_critical_bh(&phal->SdioTxFIFOFreePageLock, &irql);

			ret = (padapter->bDriverStopped == _TRUE) || (padapter->bSurpriseRemoved == _TRUE);
			if (ret) {
				RT_TRACE(_module_hal_xmit_c_, _drv_notice_,
						 ("%s: updating TX FIFO page, bDriverStopped(%d) bSurpriseRemoved(%d)!\n",
						  __FUNCTION__, padapter->bDriverStopped, padapter->bSurpriseRemoved));
				goto free_xmitbuf;
			}

			n++;
			if ((n & 0x3FF) == 0) {
				if (n > 5000) {
					printk(KERN_NOTICE "%s: FIFO starvation!(%d) len=%d agg=%d page=(R)%d(A)%d\n",
						__func__, n, pxmitbuf->len, pframe->agg_num, pframe->pg_num, freePage[PageIdx] + freePage[PUBLIC_QUEUE_IDX]);
				} else {
					RT_TRACE(_module_hal_xmit_c_, _drv_notice_,
						("%s: FIFO starvation!(%d) len=%d agg=%d page=(R)%d(A)%d\n",
						__FUNCTION__, n, pxmitbuf->len, pframe->agg_num, pframe->pg_num, freePage[PageIdx] + freePage[PUBLIC_QUEUE_IDX]));
				}
				rtw_yield_os();
			}

			// Total number of page is NOT available, so update current FIFO status
			HalQueryTxBufferStatus8723ASdio(padapter);

//			_enter_critical_bh(&phal->SdioTxFIFOFreePageLock, &irql);
		} while (1);
//		_exit_critical_bh(&phal->SdioTxFIFOFreePageLock, &irql);

		if (padapter->bSurpriseRemoved == _TRUE) {
			RT_TRACE(_module_hal_xmit_c_, _drv_notice_,
				 ("%s: bSurpriseRemoved(wirte port)\n", __FUNCTION__));
			goto free_xmitbuf;
		}
		rtw_write_port(padapter, pxmitbuf->ff_hwaddr, pxmitbuf->len, pxmitbuf->pdata);

free_xmitbuf:
		rtw_free_xmitframe(pxmitpriv, pframe);
		pxmitbuf->priv_data = NULL;
		rtw_free_xmitbuf(pxmitpriv, pxmitbuf);
	} while (1);

#ifdef CONFIG_LPS_LCLK
	rtw_unregister_tx_alive(padapter);
#endif

	return _SUCCESS;
}

static void freequeue_pending_xmitframe(struct xmit_priv *pxmitpriv)
{
	struct hw_xmit *hwxmits;
	u8 idx, hwentry;
	_irqL irql;
//	_irqL irqL0, irqL1;
	struct tx_servq *ptxservq;
	_list *sta_plist, *sta_phead, *frame_plist, *frame_phead;
	struct xmit_frame *pxmitframe;
	_queue *pframe_queue;


	_enter_critical_bh(&pxmitpriv->lock, &irql);

	hwxmits = pxmitpriv->hwxmits;
	hwentry = pxmitpriv->hwxmit_entry;
	ptxservq = NULL;
	pxmitframe = NULL;
	pframe_queue = NULL;

	// 0(VO), 1(VI), 2(BE), 3(BK)
	for (idx = 0; idx < hwentry; idx++, hwxmits++)
	{
//		_enter_critical(&hwxmits->sta_queue->lock, &irqL0);

		sta_phead = get_list_head(hwxmits->sta_queue);
		sta_plist = get_next(sta_phead);

		while (rtw_end_of_queue_search(sta_phead, sta_plist) == _FALSE)
		{
			ptxservq = LIST_CONTAINOR(sta_plist, struct tx_servq, tx_pending);
			sta_plist = get_next(sta_plist);

			pframe_queue = &ptxservq->sta_pending;

//			_enter_critical(&pframe_queue->lock, &irqL1);

			frame_phead = get_list_head(pframe_queue);

			while (rtw_is_list_empty(frame_phead) == _FALSE)
			{
				frame_plist = get_next(frame_phead);
				pxmitframe = LIST_CONTAINOR(frame_plist, struct xmit_frame, list);
				rtw_list_delete(&pxmitframe->list);
				ptxservq->qcnt--;
				hwxmits->accnt--;
				rtw_free_xmitframe(pxmitpriv, pxmitframe);
			}

			if (_rtw_queue_empty(pframe_queue) == _TRUE)
				rtw_list_delete(&ptxservq->tx_pending);

//			_exit_critical(&pframe_queue->lock, &irqL1);
		}
//		_exit_critical(&hwxmits->sta_queue->lock, &irqL0);
	}

	_exit_critical_bh(&pxmitpriv->lock, &irql);
}

/*
 *	Description:
 *		Translate QSEL to hardware tx FIFO address
 */
#ifndef CONFIG_MAC_LOOPBACK_DRIVER
static
#endif
u32 get_txfifo_hwaddr(struct xmit_frame *pxmitframe)
{
	u32 addr;
	struct pkt_attrib *pattrib;
	struct registry_priv *pregistrypriv;


	pattrib = &pxmitframe->attrib;
	switch (pattrib->qsel)
	{
		case 0:
		case 3:
			addr = WLAN_TX_LOQ_DEVICE_ID;
		 	break;
		case 1:
		case 2:
			pregistrypriv = &pxmitframe->padapter->registrypriv;
			if (!pregistrypriv->wifi_spec)
				addr = WLAN_TX_LOQ_DEVICE_ID;
			else
				addr = WLAN_TX_MIQ_DEVICE_ID;
			break;
		case 4:
		case 5:
			addr = WLAN_TX_MIQ_DEVICE_ID;
			break;
		case 6:
		case 7:
		case 0x10:
		case 0x11://BC/MC in PS (HIQ)
		case 0x12:
			addr = WLAN_TX_HIQ_DEVICE_ID;
			break;
		default:
			addr = WLAN_TX_LOQ_DEVICE_ID;
			break;
	}

	return addr;
}

/*
 * Description:
 *	Aggregation packets and send to hardware
 *
 * Return:
 *	0	Success
 *	-1	Hardware resource(TX FIFO) not ready
 *	-2	Software resource(xmitbuf) not ready
 */
static s32 xmit_xmitframes(PADAPTER padapter, struct xmit_priv *pxmitpriv)
{
	s32 err, ret;
	u32 k;
	struct hw_xmit *hwxmits;
	u8 no_res, idx, hwentry;
	_irqL irql;
//	_irqL irqL0, irqL1;
	struct tx_servq *ptxservq;
	_list *sta_plist, *sta_phead, *frame_plist, *frame_phead;
	struct xmit_frame *pxmitframe;
	_queue *pframe_queue;
	struct xmit_buf *pxmitbuf;
	u32 txlen;


	err = 0;
	no_res = _FALSE;
	hwxmits = pxmitpriv->hwxmits;
	hwentry = pxmitpriv->hwxmit_entry;
	ptxservq = NULL;
	pxmitframe = NULL;
	pframe_queue = NULL;
	pxmitbuf = NULL;

	// 0(VO), 1(VI), 2(BE), 3(BK)
	for (idx = 0; idx < hwentry; idx++, hwxmits++)
	{
//		_enter_critical(&hwxmits->sta_queue->lock, &irqL0);

		sta_phead = get_list_head(hwxmits->sta_queue);
		sta_plist = get_next(sta_phead);

		while (rtw_end_of_queue_search(sta_phead, sta_plist) == _FALSE)
		{
			ptxservq = LIST_CONTAINOR(sta_plist, struct tx_servq, tx_pending);
			sta_plist = get_next(sta_plist);

			pframe_queue = &ptxservq->sta_pending;

//			_enter_critical(&pframe_queue->lock, &irqL1);

			frame_phead = get_list_head(pframe_queue);

			while (rtw_is_list_empty(frame_phead) == _FALSE)
			{
				frame_plist = get_next(frame_phead);
				pxmitframe = LIST_CONTAINOR(frame_plist, struct xmit_frame, list);

				// check xmit_buf size enough or not
				txlen = TXDESC_SIZE + rtw_wlan_pkt_size(pxmitframe);
				if ((NULL == pxmitbuf) ||
					((pxmitbuf->ptail + txlen) > pxmitbuf->pend)
#ifdef SDIO_TX_AGG_MAX
					|| (k >= SDIO_TX_AGG_MAX)
#endif
					)
				{
					if (pxmitbuf) {
						struct xmit_frame *pframe;
						pframe = (struct xmit_frame*)pxmitbuf->priv_data;
						pframe->agg_num = k;
						rtl8723a_update_txdesc(pframe, pframe->buf_addr);
						enqueue_pending_xmitbuf(pxmitpriv, pxmitbuf);
						rtw_yield_os();
					}

					pxmitbuf = rtw_alloc_xmitbuf(pxmitpriv);
					if (pxmitbuf == NULL) {
						RT_TRACE(_module_hal_xmit_c_, _drv_err_, ("%s: xmit_buf is not enough!\n", __FUNCTION__));
						err = -2;
						break;
					}
					k = 0;
				}

				// ok to send, remove frame from queue
				_enter_critical_bh(&pxmitpriv->lock, &irql);
#ifdef CONFIG_AP_MODE
				if (check_fwstate(&padapter->mlmepriv, WIFI_AP_STATE) == _TRUE)
				{
					if ((pxmitframe->attrib.psta->state & WIFI_SLEEP_STATE) &&
						(pxmitframe->attrib.triggered == 0))
					{
						_exit_critical_bh(&pxmitpriv->lock, &irql);
#ifdef PLATFORM_LINUX
						printk("%s: one not triggered pkt in queue when STA sleep\n", __func__);
#endif
						break;
					}
				}
#endif
				rtw_list_delete(&pxmitframe->list);
				ptxservq->qcnt--;
				hwxmits->accnt--;
				_exit_critical_bh(&pxmitpriv->lock, &irql);

				if (k == 0) {
					pxmitbuf->ff_hwaddr = get_txfifo_hwaddr(pxmitframe);
					pxmitbuf->priv_data = (u8*)pxmitframe;
				}

				// coalesce the xmitframe to xmitbuf
				pxmitframe->pxmitbuf = pxmitbuf;
				pxmitframe->buf_addr = pxmitbuf->ptail;

				ret = rtw_xmitframe_coalesce(padapter, pxmitframe->pkt, pxmitframe);
				if (ret == _FAIL) {
					RT_TRACE(_module_hal_xmit_c_, _drv_err_, ("%s: coalesce FAIL!", __FUNCTION__));
					// Todo: error handler
				} else {
					k++;
					if (k != 1)
						rtl8723a_update_txdesc(pxmitframe, pxmitframe->buf_addr);
					rtw_count_tx_stats(padapter, pxmitframe, pxmitframe->attrib.last_txcmdsz);

					txlen = TXDESC_SIZE + pxmitframe->attrib.last_txcmdsz;
					pxmitframe->pg_num = (txlen + 127)/128;
					if (k != 1)
						((struct xmit_frame*)pxmitbuf->priv_data)->pg_num += pxmitframe->pg_num;
					pxmitbuf->ptail += _RND(txlen, 8); // round to 8 bytes alignment
					pxmitbuf->len = _RND(pxmitbuf->len, 8) + txlen;
				}

				if (k != 1)
					rtw_free_xmitframe(pxmitpriv, pxmitframe);
				pxmitframe = NULL;
			}

			_enter_critical_bh(&pxmitpriv->lock, &irql);
			if (_rtw_queue_empty(pframe_queue) == _TRUE)
				rtw_list_delete(&ptxservq->tx_pending);
			_exit_critical_bh(&pxmitpriv->lock, &irql);

//			_exit_critical(&pframe_queue->lock, &irqL1);

			if (err) break;
		}

//		_exit_critical(&hwxmits->sta_queue->lock, &irqL0);

		// dump xmit_buf to hw tx fifo
		if (pxmitbuf)
		{
			RT_TRACE(_module_hal_xmit_c_, _drv_notice_, ("pxmitbuf->len=%d enqueue\n",pxmitbuf->len));

			if (pxmitbuf->len > 0) {
				struct xmit_frame *pframe;
				pframe = (struct xmit_frame*)pxmitbuf->priv_data;
				pframe->agg_num = k;
				rtl8723a_update_txdesc(pframe, pframe->buf_addr);
				enqueue_pending_xmitbuf(pxmitpriv, pxmitbuf);
				rtw_yield_os();
			}
			else
				rtw_free_xmitbuf(pxmitpriv, pxmitbuf);
			pxmitbuf = NULL;
		}

		if (err) break;
	}

	return err;
}

/*
 * Description
 *	Transmit xmitframe from queue
 *
 * Return
 *	_SUCCESS	ok
 *	_FAIL		something error
 */
s32 rtl8723as_xmit_handler(PADAPTER padapter)
{
	struct xmit_priv *pxmitpriv;
	PHAL_DATA_TYPE phal;
	_irqL irql;
	s32 ret, err;


	pxmitpriv = &padapter->xmitpriv;
	phal = GET_HAL_DATA(padapter);

	ret = _rtw_down_sema(&phal->SdioXmitSema);
	if (_FAIL == ret) {
		RT_TRACE(_module_hal_xmit_c_, _drv_emerg_, ("%s: down sema fail!\n", __FUNCTION__));
		return _FAIL;
	}

	do {
		if ((padapter->bDriverStopped == _TRUE) ||
			(padapter->bSurpriseRemoved == _TRUE))
		{
			RT_TRACE(_module_hal_xmit_c_, _drv_notice_,
					 ("%s: bDriverStopped(%d) bSurpriseRemoved(%d)\n",
					  __FUNCTION__, padapter->bDriverStopped, padapter->bSurpriseRemoved));
			ret = _FAIL;
			break;
		}

		_enter_critical_bh(&pxmitpriv->lock, &irql);
		ret = rtw_txframes_pending(padapter);
		_exit_critical_bh(&pxmitpriv->lock, &irql);
		if (ret == 0) {
			ret = _SUCCESS;
			break;
		}

		// dequeue frame and write to hardware
		err = xmit_xmitframes(padapter, pxmitpriv);
		if (err == -2) {
			rtw_msleep_os(1);
		}
	} while (1);

	return ret;
}

thread_return rtl8723as_xmit_thread(thread_context context)
{
	PADAPTER padapter;
	struct xmit_priv *pxmitpriv;
	PHAL_DATA_TYPE phal;
	s32 ret;


	padapter = (PADAPTER)context;
	pxmitpriv = &padapter->xmitpriv;
	phal = GET_HAL_DATA(padapter);
	ret = _SUCCESS;

#if 0
	thread_enter(padapter->pnetdev);
#else
//	daemonize("%s", padapter->pnetdev->name);
	daemonize("%s", "RTWHALXT");
	allow_signal(SIGTERM);
#endif

	do {
		ret = rtl8723as_xmit_handler(padapter);
		if (signal_pending(current)) {
			flush_signals(current);
		}
	} while (_SUCCESS == ret);

	freequeue_pending_xmitframe(pxmitpriv);
	
	_rtw_up_sema(&phal->SdioXmitTerminateSema);

	RT_TRACE(_module_hal_xmit_c_, _drv_notice_, ("-%s\n", __FUNCTION__));

	thread_exit();
}

void rtl8723as_mgnt_xmit(PADAPTER padapter, struct xmit_frame *pmgntframe)
{
	struct pkt_attrib *pattrib;
	struct xmit_buf *pxmitbuf;


	RT_TRACE(_module_hal_xmit_c_, _drv_info_, ("+%s\n", __FUNCTION__));

	pattrib = &pmgntframe->attrib;
	pxmitbuf = pmgntframe->pxmitbuf;

	rtl8723a_update_txdesc(pmgntframe, pmgntframe->buf_addr);

	pxmitbuf->len = TXDESC_SIZE + pattrib->last_txcmdsz;
	pmgntframe->pg_num = (pxmitbuf->len + 127)/128; // 128 is tx page size
	pxmitbuf->ptail = pmgntframe->buf_addr + pxmitbuf->len;
	pxmitbuf->ff_hwaddr = get_txfifo_hwaddr(pmgntframe);

	enqueue_pending_xmitbuf(&padapter->xmitpriv, pxmitbuf);

	rtw_count_tx_stats(padapter, pmgntframe, pattrib->last_txcmdsz);
}

/*
 * Description:
 *	Handle xmitframe(packet) come from rtw_xmit()
 *
 * Return:
 *	_TRUE	dump packet directly ok
 *	_FALSE	enqueue, temporary can't transmit packets to hardware
 */
s32 rtl8723as_hal_xmit(PADAPTER padapter, struct xmit_frame *pxmitframe)
{
	struct xmit_priv *pxmitpriv;
	PHAL_DATA_TYPE phal;
	_irqL irql;
	s32 err;


	pxmitframe->attrib.qsel = pxmitframe->attrib.priority;
	pxmitpriv = &padapter->xmitpriv;
	phal = GET_HAL_DATA(padapter);

#ifdef CONFIG_80211N_HT
	if ((pxmitframe->frame_tag == DATA_FRAMETAG) &&
		(pxmitframe->attrib.ether_type != 0x0806) &&
		(pxmitframe->attrib.ether_type != 0x888e) &&
		(pxmitframe->attrib.dhcp_pkt != 1))
	{
		if (padapter->mlmepriv.LinkDetectInfo.bBusyTraffic == _TRUE)
			rtw_issue_addbareq_cmd(padapter, pxmitframe);
	}
#endif

	_enter_critical_bh(&pxmitpriv->lock, &irql);
	err = rtw_xmitframe_enqueue(padapter, pxmitframe);
	_exit_critical_bh(&pxmitpriv->lock, &irql);
	if (err != _SUCCESS) {
		RT_TRACE(_module_hal_xmit_c_, _drv_err_, ("rtl8723as_hal_xmit: enqueue xmitframe fail\n"));
		rtw_free_xmitframe_ex(pxmitpriv, pxmitframe);

		// Trick, make the statistics correct
		pxmitpriv->tx_pkts--;
		pxmitpriv->tx_drop++;
		return _TRUE;
	}

	_rtw_up_sema(&phal->SdioXmitSema);

	return _FALSE;
}

/*
 * Return
 *	_SUCCESS	start thread ok
 *	_FAIL		start thread fail
 *
 */
s32 rtl8723as_init_xmit_priv(PADAPTER padapter)
{
	PHAL_DATA_TYPE phal;


	phal = GET_HAL_DATA(padapter);

	_rtw_spinlock_init(&phal->SdioTxFIFOFreePageLock);
	_rtw_init_sema(&phal->SdioXmitSema, 0);
	_rtw_init_sema(&phal->SdioXmitTerminateSema, 0);
#ifdef PLATFORM_LINUX
	phal->SdioXmitThread = kernel_thread(rtl8723as_xmit_thread, padapter, CLONE_FS|CLONE_FILES);
	if (phal->SdioXmitThread < 0) {
		RT_TRACE(_module_hal_xmit_c_, _drv_err_, ("%s: start rtl8723as_xmit_buf_thread FAIL!!\n", __FUNCTION__));
		return _FAIL;
	}
#else
#error "can not create SdioXmitThread!\n"
#endif

	return _SUCCESS;
}

void rtl8723as_free_xmit_priv(PADAPTER padapter)
{
	PHAL_DATA_TYPE phal;
	struct xmit_priv *pxmitpriv;
	struct xmit_buf *pxmitbuf;
	_queue *pqueue;
	_list *plist, *phead;
	_list tmplist;
	_irqL irql;


	phal = GET_HAL_DATA(padapter);
	pxmitpriv = &padapter->xmitpriv;
	pqueue = &pxmitpriv->pending_xmitbuf_queue;
	phead = get_list_head(pqueue);
	_rtw_init_listhead(&tmplist);

	_enter_critical_bh(&pqueue->lock, &irql);
	if (_rtw_queue_empty(pqueue) == _FALSE)
	{
		// Insert tmplist to end of queue, and delete phead
		// then tmplist become head of queue.
		rtw_list_insert_tail(&tmplist, phead);
		rtw_list_delete(phead);
	}
	_exit_critical_bh(&pqueue->lock, &irql);

	phead = &tmplist;
	while (rtw_is_list_empty(phead) == _FALSE)
	{
		plist = get_next(phead);
		rtw_list_delete(plist);

		pxmitbuf = LIST_CONTAINOR(plist, struct xmit_buf, list);
		rtw_free_xmitframe(pxmitpriv, (struct xmit_frame*)pxmitbuf->priv_data);
		pxmitbuf->priv_data = NULL;
		rtw_free_xmitbuf(pxmitpriv, pxmitbuf);
	}

	// stop xmit_buf_thread
	if (phal->SdioXmitThread >= 0) {
		_rtw_up_sema(&phal->SdioXmitSema);
		_rtw_down_sema(&phal->SdioXmitTerminateSema);
		phal->SdioXmitThread = -1;
	}

	_rtw_spinlock_free(&phal->SdioTxFIFOFreePageLock);
}

