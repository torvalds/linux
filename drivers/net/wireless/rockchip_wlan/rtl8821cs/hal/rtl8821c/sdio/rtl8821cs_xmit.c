/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2016 - 2017 Realtek Corporation.
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
#define _RTL8821CS_XMIT_C_

#include <drv_types.h>		/* PADAPTER, rtw_xmit.h and etc. */
#include <hal_data.h>		/* HAL_DATA_TYPE */
#include "../../hal_halmac.h"	/* rtw_halmac_sdio_tx_allowed() and etc. */
#include "../rtl8821c.h"	/* rtl8821c_update_txdesc() */

#ifdef CONFIG_SDIO_TX_ENABLE_AVAL_INT
u8 HalQueryTxBufferStatus8821CSdio(PADAPTER padapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	u32 NumOfFreePage;
	/*_irqL irql;*/


	pHalData = GET_HAL_DATA(padapter);

	NumOfFreePage = SdioLocalCmd53Read4Byte(padapter, SDIO_REG_FREE_TXPG);

	/*_enter_critical_bh(&pHalData->SdioTxFIFOFreePageLock, &irql);*/
	_rtw_memcpy(pHalData->SdioTxFIFOFreePage, &NumOfFreePage, 4);
	/*_exit_critical_bh(&pHalData->SdioTxFIFOFreePageLock, &irql);*/

	return _TRUE;
}
#endif

s32 _dequeue_writeport(PADAPTER adapter)
{
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	struct xmit_priv *pxmitpriv = &adapter->xmitpriv;
	struct dvobj_priv *pdvobjpriv = adapter_to_dvobj(adapter);
	struct xmit_buf *pxmitbuf;
	u32 polling_num = 0;

#ifdef CONFIG_SDIO_TX_ENABLE_AVAL_INT

#endif
	pxmitbuf = select_and_dequeue_pending_xmitbuf(adapter);

	if (pxmitbuf == NULL)
		return _TRUE;

#ifdef CONFIG_SDIO_TX_ENABLE_AVAL_INT
query_free_page:

	/* Total number of page is NOT available, so update current FIFO status*/
	u8	bUpdatePageNum = _FALSE;

	if (!bUpdatePageNum) {
		HalQueryTxBufferStatus8821CSdio(adapter);
		bUpdatePageNum = _TRUE;
		goto query_free_page;
	} else {
		bUpdatePageNum = _FALSE;
		enqueue_pending_xmitbuf_to_head(pxmitpriv, pxmitbuf);
		return _TRUE;
	}
#endif
	if (_TRUE == rtw_is_xmit_blocked(adapter)) {
		enqueue_pending_xmitbuf_to_head(pxmitpriv, pxmitbuf);
		/*rtw_msleep_os(1);*/
		return _FALSE;
	}

	/* check if hardware tx fifo page is enough */
	while (rtw_halmac_sdio_tx_allowed(pdvobjpriv, pxmitbuf->pdata, pxmitbuf->len)) {
		if (RTW_CANNOT_RUN(adapter)) {
			RTW_INFO("%s: bSurpriseRemoved(write port)\n", __func__);
			goto free_xmitbuf;
		}

		polling_num++;
		/* Only polling (0x7F / 10) times here, since rtw_halmac_sdio_tx_allowed() has polled 10 times within */
		if ((polling_num % 6) == 0) {
			enqueue_pending_xmitbuf_to_head(pxmitpriv, pxmitbuf);
			rtw_msleep_os(1);
			return _FALSE;
		}
	}

#ifdef CONFIG_CHECK_LEAVE_LPS
	#ifdef CONFIG_LPS_CHK_BY_TP
	if (!adapter_to_pwrctl(adapter)->lps_chk_by_tp)
	#endif
		traffic_check_for_leave_lps(adapter, _TRUE, pxmitbuf->agg_num);
#endif

	if (_TRUE == rtw_is_xmit_blocked(adapter)) {
		enqueue_pending_xmitbuf_to_head(pxmitpriv, pxmitbuf);
		/*rtw_msleep_os(1);*/
		return _FALSE;
	}

	rtw_write_port(adapter, 0, pxmitbuf->len, (u8 *)pxmitbuf);

free_xmitbuf:
	rtw_free_xmitbuf(pxmitpriv, pxmitbuf);

#ifdef CONFIG_SDIO_TX_TASKLET
	tasklet_hi_schedule(&pxmitpriv->xmit_tasklet);
#endif

	return _FALSE;
}

/*
 * Description
 *	Transmit xmitbuf to hardware tx fifo
 *
 * Return
 *	_SUCCESS	ok
 *	_FAIL		something error
 */
s32 rtl8821cs_xmit_buf_handler(PADAPTER adapter)
{
	struct xmit_priv *pxmitpriv;
	u8 queue_empty, queue_pending;
	s32 ret;


	pxmitpriv = &adapter->xmitpriv;

	ret = _rtw_down_sema(&pxmitpriv->xmit_sema);
	if (_FAIL == ret) {
		RTW_ERR("%s: down SdioXmitBufSema fail!\n", __FUNCTION__);
		return _FAIL;
	}

	if (RTW_CANNOT_RUN(adapter)) {
		RTW_DBG(FUNC_ADPT_FMT "- bDriverStopped(%s) bSurpriseRemoved(%s)\n",
			 FUNC_ADPT_ARG(adapter),
			 rtw_is_drv_stopped(adapter) ? "True" : "False",
			 rtw_is_surprise_removed(adapter) ? "True" : "False");
		return _FAIL;
	}

	if (rtw_mi_check_pending_xmitbuf(adapter) == 0)
		return _SUCCESS;
#ifdef CONFIG_LPS_LCLK
	ret = rtw_register_tx_alive(adapter);
	if (ret != _SUCCESS)
		return _SUCCESS;
#endif

	do {
		queue_empty = rtw_mi_dequeue_writeport(adapter);

	} while (!queue_empty);

#ifdef CONFIG_LPS_LCLK
	rtw_unregister_tx_alive(adapter);
#endif

	return _SUCCESS;
}

#ifdef XMIT_BUF_SIZE
int rtl8821cs_get_tx_max_length(struct dvobj_priv *dvobj, u8 queue, u32 *size)
{
	_adapter *padapter = dvobj_get_primary_adapter(dvobj);
	PHAL_DATA_TYPE hal_data = GET_HAL_DATA(padapter);
	u32 fifo_size = 0;

	switch (queue) {
	case VO_QUEUE_INX:
	case VI_QUEUE_INX:
		*size = (hal_data->max_xmit_size_vovi > MAX_XMITBUF_SZ) ? MAX_XMITBUF_SZ : hal_data->max_xmit_size_vovi;
		break;
	case BE_QUEUE_INX:
	case BK_QUEUE_INX:
		*size = (hal_data->max_xmit_size_bebk > MAX_XMITBUF_SZ) ? MAX_XMITBUF_SZ : hal_data->max_xmit_size_bebk;
		break;
	case BCN_QUEUE_INX:
	case MGT_QUEUE_INX:
	case HIGH_QUEUE_INX:
	case TXCMD_QUEUE_INX:
	default:
		rtw_halmac_get_tx_fifo_size(dvobj, &fifo_size);
		*size = (fifo_size / 3);
		rtw_warn_on(1);
		break;
	}
#ifdef DBG_DUMP_RQPN
	RTW_INFO("%s => max_xmit_size:%d\n", __func__, *size);
#endif
	return 0;
}
#endif

u16 rtl8821cs_get_tx_max_page(struct dvobj_priv *dvobj, u8 queue)
{
	_adapter *padapter = dvobj_get_primary_adapter(dvobj);
	PHAL_DATA_TYPE hal_data = GET_HAL_DATA(padapter);
	u16 page_num;

	if (VO_QUEUE_INX == queue)
		page_num = (hal_data->max_xmit_page_vo > hal_data->max_xmit_page) ? hal_data->max_xmit_page : hal_data->max_xmit_page_vo;
	else if (VI_QUEUE_INX == queue)
		page_num = (hal_data->max_xmit_page_vi > hal_data->max_xmit_page) ? hal_data->max_xmit_page : hal_data->max_xmit_page_vi;
	else if (BE_QUEUE_INX == queue)
		page_num = (hal_data->max_xmit_page_be > hal_data->max_xmit_page) ? hal_data->max_xmit_page : hal_data->max_xmit_page_be;
	else if (BK_QUEUE_INX == queue)
		page_num = (hal_data->max_xmit_page_bk > hal_data->max_xmit_page) ? hal_data->max_xmit_page : hal_data->max_xmit_page_bk;
	else
		page_num = hal_data->max_xmit_page;

#ifdef DBG_DUMP_RQPN
	RTW_INFO("%s => max_xmit_page:%d\n", __func__, page_num);
#endif
	return page_num;
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
static s32 xmit_xmitframes(PADAPTER adapter, struct xmit_priv *pxmitpriv)
{
	s32 err, ret;
	u32 agg_num = 0;
	struct hw_xmit *hwxmits, *phwxmit;
	u8 no_res, idx, hwentry;
	_irqL irql;
	struct tx_servq *ptxservq;
	_list *sta_plist, *sta_phead, *frame_plist, *frame_phead;
	struct xmit_frame *pxmitframe;
	_queue *pframe_queue;
	struct xmit_buf *pxmitbuf;
	u32 txlen;
	#ifdef XMIT_BUF_SIZE
	u32 max_xmit_len;
	#endif
	u16 max_xmit_page;
	u32 txdesc_size = 0;
	u32 page_size = 0;
	int inx[4];
	u8 pre_qsel = 0xFF, next_qsel = 0xFF;
	u8 single_sta_in_queue = _FALSE;
	HAL_DATA_TYPE	*hal_data = GET_HAL_DATA(adapter);


	err = 0;
	no_res = _FALSE;
	hwxmits = pxmitpriv->hwxmits;
	hwentry = pxmitpriv->hwxmit_entry;
	ptxservq = NULL;
	pxmitframe = NULL;
	pframe_queue = NULL;
	pxmitbuf = NULL;
	rtw_halmac_get_tx_desc_size(adapter_to_dvobj(adapter), &txdesc_size);
	rtw_hal_get_def_var(adapter, HAL_DEF_TX_PAGE_SIZE, &page_size);

	if (adapter->registrypriv.wifi_spec == 1) {
		for (idx = 0; idx < 4; idx++)
			inx[idx] = pxmitpriv->wmm_para_seq[idx];
	} else {
		inx[0] = 0;
		inx[1] = 1;
		inx[2] = 2;
		inx[3] = 3;
	}

	/* 0(VO), 1(VI), 2(BE), 3(BK)*/
	for (idx = 0; idx < hwentry; idx++) {
		phwxmit = hwxmits + inx[idx];

		if ((check_pending_xmitbuf(pxmitpriv) == _TRUE)
		    && (adapter->mlmepriv.LinkDetectInfo.bHigherBusyTxTraffic == _TRUE)) {
			if ((phwxmit->accnt > 0) && (phwxmit->accnt < 5)) {
				err = -2;
				break;
			}
		}

		max_xmit_page = rtl8821cs_get_tx_max_page(adapter_to_dvobj(adapter), inx[idx]);
		#ifdef XMIT_BUF_SIZE
		rtl8821cs_get_tx_max_length(adapter_to_dvobj(adapter), inx[idx], &max_xmit_len);
		if (0) {
			if (max_xmit_page != PageNum(max_xmit_len, page_size)) {
				RTW_ERR("%s => max_xmit_page_1:%d != max_xmit_page_2:%d\n", __func__, max_xmit_page, PageNum(max_xmit_len, page_size));
				rtw_warn_on(1);
			}
		}
		#endif

		_enter_critical_bh(&pxmitpriv->lock, &irql);

		sta_phead = get_list_head(phwxmit->sta_queue);
		sta_plist = get_next(sta_phead);
		/*
		 * Because stop_sta_xmit may delete sta_plist at any time,
		 * so we should add lock here, or while loop can not exit
		 */

		single_sta_in_queue = rtw_end_of_queue_search(sta_phead, get_next(sta_plist));

		while (rtw_end_of_queue_search(sta_phead, sta_plist) == _FALSE) {
			ptxservq = LIST_CONTAINOR(sta_plist, struct tx_servq, tx_pending);
			sta_plist = get_next(sta_plist);

#ifdef DBG_XMIT_BUF
			RTW_INFO("%s idx:%d hwxmit_pkt_num:%d ptxservq_pkt_num:%d\n", __FUNCTION__, idx, phwxmit->accnt, ptxservq->qcnt);
			RTW_INFO("%s free_xmit_extbuf_cnt=%d free_xmitbuf_cnt=%d free_xmitframe_cnt=%d\n",
				__FUNCTION__, pxmitpriv->free_xmit_extbuf_cnt, pxmitpriv->free_xmitbuf_cnt,
				 pxmitpriv->free_xmitframe_cnt);
#endif
			pframe_queue = &ptxservq->sta_pending;

			frame_phead = get_list_head(pframe_queue);

			while (rtw_is_list_empty(frame_phead) == _FALSE) {
				frame_plist = get_next(frame_phead);
				pxmitframe = LIST_CONTAINOR(frame_plist, struct xmit_frame, list);

				/* check xmit_buf size enough or not */
				txlen = txdesc_size + rtw_wlan_pkt_size(pxmitframe);
				next_qsel = pxmitframe->attrib.qsel;
				if ((NULL == pxmitbuf)				    
					#ifdef XMIT_BUF_SIZE
				    || ((pxmitbuf->pg_num + PageNum(txlen, page_size)) > PageNum(max_xmit_len, page_size))
					#else
				    || ((pxmitbuf->pg_num + PageNum(txlen, page_size)) > max_xmit_page)
					#endif
				    || (agg_num >= hal_data->max_oqt_size)
				#ifdef SDIO_TX_AGG_MAX
					|| (agg_num >= SDIO_TX_AGG_MAX)
				#endif
				    || ((agg_num != 0) && (_FAIL == rtw_hal_busagg_qsel_check(adapter, pre_qsel, next_qsel)))) {
					if (pxmitbuf) {
						if (pxmitbuf->len > 0 && pxmitbuf->priv_data) {
							struct xmit_frame *pframe;

							pframe = (struct xmit_frame *)pxmitbuf->priv_data;
							pframe->agg_num = agg_num;
							pxmitbuf->agg_num = agg_num;
							rtl8821c_update_txdesc(pframe, pframe->buf_addr);
							rtw_free_xmitframe(pxmitpriv, pframe);
							pxmitbuf->priv_data = NULL;
							enqueue_pending_xmitbuf(pxmitpriv, pxmitbuf);

							if (single_sta_in_queue == _FALSE) {
								/* break the loop in case there is more than one sta in this ac queue */
								pxmitbuf = NULL;
								err = -3;
								break;
							}
						} else
							rtw_free_xmitbuf(pxmitpriv, pxmitbuf);
					}

					pxmitbuf = rtw_alloc_xmitbuf(pxmitpriv);
					if (pxmitbuf == NULL) {
						err = -2;
#ifdef CONFIG_SDIO_TX_ENABLE_AVAL_INT
						_rtw_up_sema(&(GET_PRIMARY_ADAPTER(adapter)->xmitpriv.xmit_sema));
#endif
						break;
					}
					agg_num = 0;
				}

				/* ok to send, remove frame from queue */
#ifdef CONFIG_AP_MODE
				if (MLME_IS_AP(adapter) || MLME_IS_MESH(adapter)) {
					if ((pxmitframe->attrib.psta->state & WIFI_SLEEP_STATE)
					    && (pxmitframe->attrib.triggered == 0)) {
						RTW_INFO("%s: one not triggered pkt in queue when this STA sleep,"
							" break and goto next sta\n", __FUNCTION__);
						break;
					}
				}
#endif
				rtw_list_delete(&pxmitframe->list);
				ptxservq->qcnt--;
				phwxmit->accnt--;

				if (agg_num == 0) {
					pxmitbuf->ff_hwaddr = rtw_get_ff_hwaddr(pxmitframe);
					pxmitbuf->priv_data = (u8 *)pxmitframe;
				}

				/* coalesce the xmitframe to xmitbuf */
				pxmitframe->pxmitbuf = pxmitbuf;
				pxmitframe->buf_addr = pxmitbuf->ptail;

				ret = rtw_xmitframe_coalesce(adapter, pxmitframe->pkt, pxmitframe);
				if (ret == _FAIL) {
					RTW_INFO("%s: coalesce FAIL!", __FUNCTION__);
					/* Todo: error handler */
				} else {
					agg_num++;
					if (agg_num != 1)
						rtl8821c_update_txdesc(pxmitframe, pxmitframe->buf_addr);
					rtw_count_tx_stats(adapter, pxmitframe, pxmitframe->attrib.last_txcmdsz);
					pre_qsel = pxmitframe->attrib.qsel;
					txlen = txdesc_size + pxmitframe->attrib.last_txcmdsz;
					pxmitframe->pg_num = PageNum(txlen, page_size);
					pxmitbuf->pg_num += PageNum(txlen, page_size);
					pxmitbuf->ptail += _RND(txlen, 8); /* round to 8 bytes alignment */
					pxmitbuf->len = _RND(pxmitbuf->len, 8) + txlen;
				}

				if (agg_num != 1)
					rtw_free_xmitframe(pxmitpriv, pxmitframe);
				pxmitframe = NULL;
			}
#if 0
			/* dump xmit_buf to hw tx fifo */
			if (pxmitbuf && (pxmitbuf->len > 0)) {
				struct xmit_frame *pframe;

				RTW_INFO("STA pxmitbuf->len=%d enqueue\n", pxmitbuf->len);

				pframe = (struct xmit_frame *)pxmitbuf->priv_data;
				pframe->agg_num = k;
				pxmitbuf->agg_num = k;
				rtl8821c_update_txdesc(pframe, pframe->buf_addr);
				rtw_free_xmitframe(pxmitpriv, pframe);
				pxmitbuf->priv_data = NULL;
				enqueue_pending_xmitbuf(pxmitpriv, pxmitbuf);

				pxmitbuf = NULL;
			}
#endif
			if (_rtw_queue_empty(pframe_queue) == _TRUE)
				rtw_list_delete(&ptxservq->tx_pending);
			else if (err == -3) {
				/* Re-arrange the order of stations in this ac queue to balance the service for these stations */
				rtw_list_delete(&ptxservq->tx_pending);
				rtw_list_insert_tail(&ptxservq->tx_pending, get_list_head(phwxmit->sta_queue));
				err = 0;
			}

			if (err)
				break;
		}
		_exit_critical_bh(&pxmitpriv->lock, &irql);

		/* dump xmit_buf to hw tx fifo */
		if (pxmitbuf) {

			if (pxmitbuf->len > 0) {
				struct xmit_frame *pframe;

				pframe = (struct xmit_frame *)pxmitbuf->priv_data;
				pframe->agg_num = agg_num;
				pxmitbuf->agg_num = agg_num;
				rtl8821c_update_txdesc(pframe, pframe->buf_addr);
				rtw_free_xmitframe(pxmitpriv, pframe);
				pxmitbuf->priv_data = NULL;
				enqueue_pending_xmitbuf(pxmitpriv, pxmitbuf);
				rtw_yield_os();
			} else
				rtw_free_xmitbuf(pxmitpriv, pxmitbuf);
			pxmitbuf = NULL;
		}

		if (err == -2)
			break;
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
static s32 rtl8821cs_xmit_handler(PADAPTER adapter)
{
	struct xmit_priv *pxmitpriv = &adapter->xmitpriv;
	s32 ret;
	_irqL irql;

	ret = _rtw_down_sema(&pxmitpriv->SdioXmitSema);
	if (_FAIL == ret) {
		RTW_ERR("%s: down sema fail!\n", __FUNCTION__);
		return _FAIL;
	}

next:
	if (RTW_CANNOT_RUN(adapter)) {
		RTW_DBG(FUNC_ADPT_FMT "- bDriverStopped(%s) bSurpriseRemoved(%s)\n",
			 FUNC_ADPT_ARG(adapter),
			 rtw_is_drv_stopped(adapter) ? "True" : "False",
			 rtw_is_surprise_removed(adapter) ? "True" : "False");
		return _FAIL;
	}

	_enter_critical_bh(&pxmitpriv->lock, &irql);
	ret = rtw_txframes_pending(adapter);
	_exit_critical_bh(&pxmitpriv->lock, &irql);
	/* All queues are empty! */
	if (ret == 0)
		return _SUCCESS;

	/* Dequeue frame and agg-tx then enqueue pending xmitbuf-queue */
	ret = xmit_xmitframes(adapter, pxmitpriv);
	if (ret == -2) {
		/* here sleep 1ms will cause big TP loss of TX */
		/* from 50+ to 40+ */
		if (adapter->registrypriv.wifi_spec)
		rtw_msleep_os(1);
		else
			#ifdef CONFIG_REDUCE_TX_CPU_LOADING
			rtw_msleep_os(1);
			#else
			rtw_yield_os();
			#endif

		goto next;
	}
	_enter_critical_bh(&pxmitpriv->lock, &irql);
	ret = rtw_txframes_pending(adapter);
	_exit_critical_bh(&pxmitpriv->lock, &irql);
	if (ret == 1)
		goto next;

	return _SUCCESS;
}

thread_return rtl8821cs_xmit_thread(thread_context context)
{
	s32 ret;
	PADAPTER adapter;
	struct xmit_priv *pxmitpriv;
	u8 thread_name[20] = {0};


	ret = _SUCCESS;
	adapter = (PADAPTER)context;
	pxmitpriv = &adapter->xmitpriv;

	rtw_sprintf(thread_name, 20, "RTWHALXT-"ADPT_FMT, ADPT_ARG(adapter));
	thread_enter(thread_name);

	RTW_INFO("start "FUNC_ADPT_FMT"\n", FUNC_ADPT_ARG(adapter));

	do {
		ret = rtl8821cs_xmit_handler(adapter);
		flush_signals_thread();
	} while (_SUCCESS == ret);

	RTW_INFO(FUNC_ADPT_FMT " Exit\n", FUNC_ADPT_ARG(adapter));

	rtw_thread_wait_stop();

	return 0;
}

/*
 * Description:
 *	Transmit manage frame
 *
 * Return:
 *	_SUCCESS	ok or enqueue
 *	_FAIL		fail
 */
s32 rtl8821cs_mgnt_xmit(PADAPTER adapter, struct xmit_frame *pmgntframe)
{
	s32 ret = _SUCCESS;
	struct dvobj_priv *pdvobjpriv;
	struct xmit_priv *pxmitpriv;
	struct pkt_attrib *pattrib;
	struct xmit_buf *pxmitbuf;
	u32 txdesc_size;
	u32 page_size;
	u8 *pframe;


	pdvobjpriv = adapter_to_dvobj(adapter);
	pxmitpriv = &adapter->xmitpriv;
	pattrib = &pmgntframe->attrib;
	pxmitbuf = pmgntframe->pxmitbuf;
	rtw_halmac_get_tx_desc_size(pdvobjpriv, &txdesc_size);
	rtw_hal_get_def_var(adapter, HAL_DEF_TX_PAGE_SIZE, &page_size);

	rtl8821c_update_txdesc(pmgntframe, pmgntframe->buf_addr);

	pxmitbuf->len = txdesc_size + pattrib->last_txcmdsz;
	pxmitbuf->pg_num = PageNum(pxmitbuf->len, page_size);
	pxmitbuf->ptail = pmgntframe->buf_addr + pxmitbuf->len;

	pframe = pmgntframe->buf_addr + txdesc_size;

	rtw_count_tx_stats(adapter, pmgntframe, pattrib->last_txcmdsz);

	rtw_free_xmitframe(pxmitpriv, pmgntframe);
	pxmitbuf->priv_data = NULL;

	if ((pattrib->subtype == WIFI_BEACON) || (get_frame_sub_type(pframe) == WIFI_BEACON)) {
		/* dump beacon directly */
		ret = rtw_write_port(adapter, 0, pxmitbuf->len, (u8 *)pxmitbuf);
		if (ret != _SUCCESS)
			rtw_sctx_done_err(&pxmitbuf->sctx, RTW_SCTX_DONE_WRITE_PORT_ERR);

		rtw_free_xmitbuf(pxmitpriv, pxmitbuf);
	} else
		enqueue_pending_xmitbuf(pxmitpriv, pxmitbuf);
	return ret;
}

/*
 * Description:
 *	Enqueue xmitframe
 *
 * Return:
 *	_TRUE	enqueue ok
 *	_FALSE	fail
 */
s32 rtl8821cs_hal_xmit_enqueue(PADAPTER adapter, struct xmit_frame *pxmitframe)
{
	struct xmit_priv *pxmitpriv;
	s32 ret;


	pxmitpriv = &adapter->xmitpriv;

	ret = rtw_xmitframe_enqueue(adapter, pxmitframe);
	if (ret != _SUCCESS) {
		rtw_free_xmitframe(pxmitpriv, pxmitframe);
		pxmitpriv->tx_drop++;
		return _FALSE;
	}

#ifdef CONFIG_SDIO_TX_TASKLET
	tasklet_hi_schedule(&pxmitpriv->xmit_tasklet);
#else /* !CONFIG_SDIO_TX_TASKLET */
	_rtw_up_sema(&pxmitpriv->SdioXmitSema);
#endif /* !CONFIG_SDIO_TX_TASKLET */

	return _TRUE;
}

/*
 * Description:
 *	Handle xmitframe(packet) come from rtw_xmit()
 *
 * Return:
 *	_TRUE	handle packet directly, maybe ok or drop
 *	_FALSE	enqueue, temporary can't transmit packets to hardware
 */
s32 rtl8821cs_hal_xmit(PADAPTER adapter, struct xmit_frame *pxmitframe)
{
	struct xmit_priv *pxmitpriv;
	_irqL irql;
	s32 err;


	pxmitframe->attrib.qsel = pxmitframe->attrib.priority;
	pxmitpriv = &adapter->xmitpriv;

#ifdef CONFIG_80211N_HT
	if ((pxmitframe->frame_tag == DATA_FRAMETAG)
	    && (pxmitframe->attrib.ether_type != 0x0806)
	    && (pxmitframe->attrib.ether_type != 0x888e)
	    && (pxmitframe->attrib.dhcp_pkt != 1)) {
		rtw_issue_addbareq_cmd(adapter, pxmitframe, _TRUE);
	}
#endif /* CONFIG_80211N_HT */

	_enter_critical_bh(&pxmitpriv->lock, &irql);
	err = rtw_xmitframe_enqueue(adapter, pxmitframe);
	_exit_critical_bh(&pxmitpriv->lock, &irql);
	if (err != _SUCCESS) {
		rtw_free_xmitframe(pxmitpriv, pxmitframe);

		pxmitpriv->tx_drop++;
		return _TRUE;
	}

#ifdef CONFIG_SDIO_TX_TASKLET
	tasklet_hi_schedule(&pxmitpriv->xmit_tasklet);
#else
	_rtw_up_sema(&pxmitpriv->SdioXmitSema);
#endif

	return _FALSE;
}

/*
 * Return
 *	_SUCCESS	start thread ok
 *	_FAIL		start thread fail
 *
 */
s32 rtl8821cs_init_xmit_priv(PADAPTER adapter)
{
	struct xmit_priv *xmitpriv = &adapter->xmitpriv;

	_rtw_init_sema(&xmitpriv->SdioXmitSema, 0);
	rtl8821c_init_xmit_priv(adapter);

	return _SUCCESS;
}

void rtl8821cs_free_xmit_priv(PADAPTER adapter)
{
	struct xmit_priv *pxmitpriv;
	struct xmit_buf *pxmitbuf;
	_queue *pqueue;
	_list *plist, *phead;
	_list tmplist;
	_irqL irql;


	pxmitpriv = &adapter->xmitpriv;
	pqueue = &pxmitpriv->pending_xmitbuf_queue;
	phead = get_list_head(pqueue);
	_rtw_init_listhead(&tmplist);

	_enter_critical_bh(&pqueue->lock, &irql);
	if (_rtw_queue_empty(pqueue) == _FALSE) {
		/*
		 * Insert tmplist to end of queue, and delete phead
		 * then tmplist become head of queue.
		 */
		rtw_list_insert_tail(&tmplist, phead);
		rtw_list_delete(phead);
	}
	_exit_critical_bh(&pqueue->lock, &irql);

	phead = &tmplist;
	while (rtw_is_list_empty(phead) == _FALSE) {
		plist = get_next(phead);
		rtw_list_delete(plist);

		pxmitbuf = LIST_CONTAINOR(plist, struct xmit_buf, list);
		rtw_free_xmitframe(pxmitpriv, (struct xmit_frame *)pxmitbuf->priv_data);
		pxmitbuf->priv_data = NULL;
		rtw_free_xmitbuf(pxmitpriv, pxmitbuf);
	}
}
