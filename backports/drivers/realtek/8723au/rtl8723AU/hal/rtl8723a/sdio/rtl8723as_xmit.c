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


s32 rtl8723_dequeue_writeport(PADAPTER padapter, u8 *freePage)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct dvobj_priv	*pdvobjpriv = adapter_to_dvobj(padapter);
	struct xmit_buf *pxmitbuf;
	//struct xmit_frame *pframe;
	PADAPTER pri_padapter = padapter;
	u32 deviceId;
	u32 requiredPage;
	u8 PageIdx;
	_irqL irql;
	u32 n;
	s32 ret = 0;
	//HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
#ifdef CONFIG_CONCURRENT_MODE
	s32 buddy_rm_stop = _FAIL;
#endif

#ifdef CONFIG_CONCURRENT_MODE
	if(rtw_buddy_adapter_up(padapter))
		ret = check_buddy_fwstate( padapter,  _FW_UNDER_SURVEY);
#endif

	ret = ret || check_fwstate(pmlmepriv, _FW_UNDER_SURVEY);

	if (_TRUE == ret)
		pxmitbuf = dequeue_pending_xmitbuf_under_survey(pxmitpriv);
	else
		pxmitbuf = dequeue_pending_xmitbuf(pxmitpriv);

	if (pxmitbuf == NULL) 
		return _TRUE;

	//pframe = (struct xmit_frame*)pxmitbuf->priv_data;
	//requiredPage = pframe->pg_num;
	requiredPage = pxmitbuf->pg_num;

	//translate queue index to sdio fifo addr
	deviceId = pdvobjpriv->Queue2Pipe[pxmitbuf->ff_hwaddr];

	// translate sdio fifo addr to tx fifo page index
	switch (deviceId)
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
//	_enter_critical_bh(&phal->SdioTxFIFOFreePageLock, &irql);
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
//		_exit_critical_bh(&phal->SdioTxFIFOFreePageLock, &irql);

		ret = (padapter->bDriverStopped == _TRUE) || (padapter->bSurpriseRemoved == _TRUE)
#ifdef CONFIG_CONCURRENT_MODE
		||((padapter->pbuddy_adapter) 
		&& ((padapter->pbuddy_adapter->bSurpriseRemoved) ||(padapter->pbuddy_adapter->bDriverStopped)))
#endif
		;

		if (ret) {
			RT_TRACE(_module_hal_xmit_c_, _drv_notice_,
				 ("%s: bSurpriseRemoved(update TX FIFO page)\n", __func__));
			goto free_xmitbuf;
		}

		n++;
		//if ((n & 0x3FF) == 0) 
		if ((n % 100) == 0) 
		{		
			if (n >= 5000) {
				u8 reg_value_1 = 0;
				u8 reg_value_2 = 0;
				u8 reg_value_3 = 0;
				
				DBG_8192C(KERN_NOTICE "%s: FIFO starvation!(%d) len=%d agg=%d page=(R)%d(A)%d\n",
					__func__, n, pxmitbuf->len, pxmitbuf->agg_num, pxmitbuf->pg_num, freePage[PageIdx] + freePage[PUBLIC_QUEUE_IDX]);

				//try to recover the transmission
				reg_value_1 = rtw_read8(padapter, REG_SYS_FUNC_EN);
				reg_value_2 = rtw_read8(padapter, REG_CR);
				reg_value_3 = rtw_read8(padapter, REG_TXPAUSE);
				DBG_871X("Before recovery: REG_SYS_FUNC_EN = 0x%X, REG_CR = 0x%X, REG_TXPAUSE = 0x%X\n", reg_value_1, reg_value_2, reg_value_3);

				rtw_write8(padapter, REG_SYS_FUNC_EN, reg_value_1 | 0x01);
				rtw_write8(padapter, REG_CR, reg_value_2 | 0xC0);
				rtw_write8(padapter, REG_TXPAUSE, 0);
				DBG_871X("After recovery: REG_SYS_FUNC_EN = 0x%X, REG_CR = 0x%X, REG_TXPAUSE = 0x%X\n", 
					rtw_read8(padapter, REG_SYS_FUNC_EN), rtw_read8(padapter, REG_CR), rtw_read8(padapter, REG_TXPAUSE));
			} else {
				//RT_TRACE(_module_hal_xmit_c_, _drv_notice_,
				//	("%s: FIFO starvation!(%d) len=%d agg=%d page=(R)%d(A)%d\n",
				//	__FUNCTION__, n, pxmitbuf->len, pxmitbuf->agg_num, pxmitbuf->pg_num, freePage[PageIdx] + freePage[PUBLIC_QUEUE_IDX]));
			}
			//rtw_yield_os();
			rtw_msleep_os(1);
		}

		// Total number of page is NOT available, so update current FIFO status
#ifdef CONFIG_CONCURRENT_MODE
		if (padapter->adapter_type > 0)
			pri_padapter = padapter->pbuddy_adapter;
#endif
		HalQueryTxBufferStatus8723ASdio(pri_padapter);

	} while (1);

	if ((padapter->bSurpriseRemoved == _TRUE)
#ifdef CONFIG_CONCURRENT_MODE
		||((padapter->pbuddy_adapter)&& (padapter->pbuddy_adapter->bSurpriseRemoved))
#endif
	){
		RT_TRACE(_module_hal_xmit_c_, _drv_notice_,
			 ("%s: bSurpriseRemoved(wirte port)\n", __FUNCTION__));
		goto free_xmitbuf;
	}
	rtw_write_port(padapter, deviceId, pxmitbuf->len, (u8 *)pxmitbuf);

free_xmitbuf:
	//rtw_free_xmitframe(pxmitpriv, pframe);
	//pxmitbuf->priv_data = NULL;
	rtw_free_xmitbuf(pxmitpriv, pxmitbuf);

	return _FAIL;
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
	struct dvobj_priv	*pdvobjpriv;
	struct xmit_buf *pxmitbuf;
	struct xmit_frame *pframe;
	u8 *freePage;
	u32 deviceId;
	u32 requiredPage;
	u8 PageIdx, queue_empty;
	_irqL irql;
	u32 n;
	s32 ret;


	phal = GET_HAL_DATA(padapter);
	pmlmepriv = &padapter->mlmepriv;
	pxmitpriv = &padapter->xmitpriv;
	pdvobjpriv = adapter_to_dvobj(padapter);
	freePage = phal->SdioTxFIFOFreePage;

	ret = _rtw_down_sema(&pxmitpriv->xmit_sema);
	if (_FAIL == ret) {
		RT_TRACE(_module_hal_xmit_c_, _drv_emerg_,
				 ("%s: down SdioXmitBufSema fail!\n", __FUNCTION__));
		return _FAIL;
	}

	ret = (padapter->bDriverStopped == _TRUE) || (padapter->bSurpriseRemoved == _TRUE);
	if (ret) {
		RT_TRACE(_module_hal_xmit_c_, _drv_err_,
				 ("%s: bDriverStopped(%d) bSurpriseRemoved(%d)!\n",
				  __FUNCTION__, padapter->bDriverStopped, padapter->bSurpriseRemoved));
		return _FAIL;
	}

#ifdef CONFIG_LPS_LCLK
	ret = rtw_register_tx_alive(padapter);
	if (ret != _SUCCESS) {
		return _SUCCESS;
	}
#endif

	do {
		queue_empty = rtl8723_dequeue_writeport(padapter, freePage);
//	dump secondary adapter xmitbuf 
#ifdef CONFIG_CONCURRENT_MODE
		if(rtw_buddy_adapter_up(padapter))
			queue_empty &= rtl8723_dequeue_writeport(padapter->pbuddy_adapter, freePage);
#endif
	} while ( !queue_empty);
	
#ifdef CONFIG_LPS_LCLK
	rtw_unregister_tx_alive(padapter);
#endif

	return _SUCCESS;
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
		_enter_critical_bh(&pxmitpriv->lock, &irql);

		sta_phead = get_list_head(hwxmits->sta_queue);
		sta_plist = get_next(sta_phead);

		while (rtw_end_of_queue_search(sta_phead, sta_plist) == _FALSE)
		{
			ptxservq = LIST_CONTAINOR(sta_plist, struct tx_servq, tx_pending);
			sta_plist = get_next(sta_plist);

			pframe_queue = &ptxservq->sta_pending;

//			_enter_critical(&pframe_queue->lock, &irqL1);
			//_enter_critical_bh(&pxmitpriv->lock, &irql);

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
						pxmitbuf->agg_num = k;
						rtl8723a_update_txdesc(pframe, pframe->buf_addr);
						rtw_free_xmitframe(pxmitpriv, pframe);
						pxmitbuf->priv_data = NULL;
						enqueue_pending_xmitbuf(pxmitpriv, pxmitbuf);
						//rtw_yield_os();
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
				//_enter_critical_bh(&pxmitpriv->lock, &irql);
#ifdef CONFIG_AP_MODE
				if (check_fwstate(&padapter->mlmepriv, WIFI_AP_STATE) == _TRUE)
				{
					if ((pxmitframe->attrib.psta->state & WIFI_SLEEP_STATE) &&
						(pxmitframe->attrib.triggered == 0))
					{
						//_exit_critical_bh(&pxmitpriv->lock, &irql);
						
						DBG_8192C("%s: one not triggered pkt in queue when STA sleep\n", __func__);
						break;
					}
				}
#endif
				rtw_list_delete(&pxmitframe->list);
				ptxservq->qcnt--;
				hwxmits->accnt--;
				

				if (k == 0) {
					pxmitbuf->ff_hwaddr = rtw_get_ff_hwaddr(pxmitframe);
					pxmitbuf->priv_data = (u8*)pxmitframe;
				}

				// coalesce the xmitframe to xmitbuf
				pxmitframe->pxmitbuf = pxmitbuf;
				pxmitframe->buf_addr = pxmitbuf->ptail;

				ret = rtw_xmitframe_coalesce(padapter, pxmitframe->pkt, pxmitframe);
				if (ret == _FAIL) {
					RT_TRACE(_module_hal_xmit_c_, _drv_err_, ("%s: coalesce FAIL!", __FUNCTION__));
					// Todo: error handler
					DBG_871X("%s: coalesce FAIL!", __FUNCTION__);
				} else {
					k++;
					if (k != 1)
						rtl8723a_update_txdesc(pxmitframe, pxmitframe->buf_addr);
					rtw_count_tx_stats(padapter, pxmitframe, pxmitframe->attrib.last_txcmdsz);

					txlen = TXDESC_SIZE + pxmitframe->attrib.last_txcmdsz;
					pxmitframe->pg_num = (txlen + 127)/128;
					pxmitbuf->pg_num += (txlen + 127)/128;					
					//if (k != 1)
					//	((struct xmit_frame*)pxmitbuf->priv_data)->pg_num += pxmitframe->pg_num;
					pxmitbuf->ptail += _RND(txlen, 8); // round to 8 bytes alignment
					pxmitbuf->len = _RND(pxmitbuf->len, 8) + txlen;
				}

				if (k != 1)
					rtw_free_xmitframe(pxmitpriv, pxmitframe);
				pxmitframe = NULL;
			}

			//_enter_critical_bh(&pxmitpriv->lock, &irql);
			if (_rtw_queue_empty(pframe_queue) == _TRUE)
				rtw_list_delete(&ptxservq->tx_pending);
			//_exit_critical_bh(&pxmitpriv->lock, &irql);

//			_exit_critical(&pframe_queue->lock, &irqL1);
			//_exit_critical_bh(&pxmitpriv->lock, &irql);

			if (err) break;
		}

//		_exit_critical(&hwxmits->sta_queue->lock, &irqL0);
		_exit_critical_bh(&pxmitpriv->lock, &irql);

		// dump xmit_buf to hw tx fifo
		if (pxmitbuf)
		{
			RT_TRACE(_module_hal_xmit_c_, _drv_notice_, ("pxmitbuf->len=%d enqueue\n",pxmitbuf->len));

			if (pxmitbuf->len > 0) {
				struct xmit_frame *pframe;
				pframe = (struct xmit_frame*)pxmitbuf->priv_data;
				pframe->agg_num = k;
				pxmitbuf->agg_num = k;
				rtl8723a_update_txdesc(pframe, pframe->buf_addr);
				rtw_free_xmitframe(pxmitpriv, pframe);
				pxmitbuf->priv_data = NULL;
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
	s32 ret;
	_irqL irql;


	pxmitpriv = &padapter->xmitpriv;

wait:
	ret = _rtw_down_sema(&pxmitpriv->SdioXmitSema);
	if (_FAIL == ret) {
		RT_TRACE(_module_hal_xmit_c_, _drv_emerg_, ("%s: down sema fail!\n", __FUNCTION__));
		return _FAIL;
	}

next:
	if ((padapter->bDriverStopped == _TRUE) ||
		(padapter->bSurpriseRemoved == _TRUE)) {
		RT_TRACE(_module_hal_xmit_c_, _drv_notice_,
				 ("%s: bDriverStopped(%d) bSurpriseRemoved(%d)\n",
				  __FUNCTION__, padapter->bDriverStopped, padapter->bSurpriseRemoved));
		return _FAIL;
	}

	_enter_critical_bh(&pxmitpriv->lock, &irql);
	ret = rtw_txframes_pending(padapter);
	_exit_critical_bh(&pxmitpriv->lock, &irql);
	if (ret == 0) {
		return _SUCCESS;
	}

	// dequeue frame and write to hardware

	ret = xmit_xmitframes(padapter, pxmitpriv);
	if (ret == -2) {
		rtw_msleep_os(1);
		goto next;
	}

	_enter_critical_bh(&pxmitpriv->lock, &irql);
	ret = rtw_txframes_pending(padapter);
	_exit_critical_bh(&pxmitpriv->lock, &irql);
	if (ret == 1) {
		rtw_msleep_os(1);
		goto next;
	}

	return _SUCCESS;
}

thread_return rtl8723as_xmit_thread(thread_context context)
{
	PADAPTER padapter;
	struct xmit_priv *pxmitpriv;
	s32 ret;


	padapter = (PADAPTER)context;
	pxmitpriv = &padapter->xmitpriv;
	ret = _SUCCESS;

	thread_enter("RTWHALXT");

	do {
		ret = rtl8723as_xmit_handler(padapter);
		if (signal_pending(current)) {
			flush_signals(current);
		}
	} while (_SUCCESS == ret);

	_rtw_up_sema(&pxmitpriv->SdioXmitTerminateSema);

	RT_TRACE(_module_hal_xmit_c_, _drv_notice_, ("-%s\n", __FUNCTION__));

	thread_exit();
}

s32 rtl8723as_mgnt_xmit(PADAPTER padapter, struct xmit_frame *pmgntframe)
{
	s32 ret = _SUCCESS;
	struct pkt_attrib *pattrib;
	struct xmit_buf *pxmitbuf;
        struct xmit_priv	*pxmitpriv = &padapter->xmitpriv;
        struct dvobj_priv	*pdvobjpriv = adapter_to_dvobj(padapter);
        u8 *pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;

	RT_TRACE(_module_hal_xmit_c_, _drv_info_, ("+%s\n", __FUNCTION__));

	pattrib = &pmgntframe->attrib;
	pxmitbuf = pmgntframe->pxmitbuf;

	rtl8723a_update_txdesc(pmgntframe, pmgntframe->buf_addr);

	pxmitbuf->len = TXDESC_SIZE + pattrib->last_txcmdsz;
	//pmgntframe->pg_num = (pxmitbuf->len + 127)/128; // 128 is tx page size
	pxmitbuf->pg_num = (pxmitbuf->len + 127)/128; // 128 is tx page size
	pxmitbuf->ptail = pmgntframe->buf_addr + pxmitbuf->len;
	pxmitbuf->ff_hwaddr = rtw_get_ff_hwaddr(pmgntframe);

        rtw_count_tx_stats(padapter, pmgntframe, pattrib->last_txcmdsz);

	rtw_free_xmitframe(pxmitpriv, pmgntframe);

	pxmitbuf->priv_data = NULL;

	if(GetFrameSubType(pframe)==WIFI_BEACON) //dump beacon directly
	{
		rtw_write_port(padapter, pdvobjpriv->Queue2Pipe[pxmitbuf->ff_hwaddr], pxmitbuf->len, (u8 *)pxmitbuf);
		
		rtw_free_xmitbuf(pxmitpriv, pxmitbuf);
	}
         else
	{
		enqueue_pending_xmitbuf(pxmitpriv, pxmitbuf);
	}	

	if  (ret != _SUCCESS)
		rtw_sctx_done_err(&pxmitbuf->sctx, RTW_SCTX_DONE_UNKNOWN);

	return ret;
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
	_irqL irql;
	s32 err;


	pxmitframe->attrib.qsel = pxmitframe->attrib.priority;
	pxmitpriv = &padapter->xmitpriv;

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
		rtw_free_xmitframe(pxmitpriv, pxmitframe);

		// Trick, make the statistics correct
		pxmitpriv->tx_pkts--;
		pxmitpriv->tx_drop++;
		return _TRUE;
	}

	_rtw_up_sema(&pxmitpriv->SdioXmitSema);

	return _FALSE;
}

s32	rtl8723as_hal_xmitframe_enqueue(_adapter *padapter, struct xmit_frame *pxmitframe)
{
	struct xmit_priv 	*pxmitpriv = &padapter->xmitpriv;
	s32 err;
	
	if ((err=rtw_xmitframe_enqueue(padapter, pxmitframe)) != _SUCCESS) 
	{
		rtw_free_xmitframe(pxmitpriv, pxmitframe);

		// Trick, make the statistics correct
		pxmitpriv->tx_pkts--;
		pxmitpriv->tx_drop++;					
	}
	else
	{
#ifdef CONFIG_SDIO_TX_TASKLET
		tasklet_hi_schedule(&pxmitpriv->xmit_tasklet);					
#else
		_rtw_up_sema(&pxmitpriv->SdioXmitSema);
#endif
	}
	
	return err;
	
}

/*
 * Return
 *	_SUCCESS	start thread ok
 *	_FAIL		start thread fail
 *
 */
s32 rtl8723as_init_xmit_priv(PADAPTER padapter)
{
	struct xmit_priv *xmitpriv = &padapter->xmitpriv;
	PHAL_DATA_TYPE phal;


	phal = GET_HAL_DATA(padapter);

	_rtw_spinlock_init(&phal->SdioTxFIFOFreePageLock);
	_rtw_init_sema(&xmitpriv->SdioXmitSema, 0);
	_rtw_init_sema(&xmitpriv->SdioXmitTerminateSema, 0);

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

	_rtw_spinlock_free(&phal->SdioTxFIFOFreePageLock);
}

