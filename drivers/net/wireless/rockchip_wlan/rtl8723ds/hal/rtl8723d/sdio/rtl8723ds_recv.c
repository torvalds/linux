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
#define _RTL8723DS_RECV_C_

#include <rtl8723d_hal.h>


static s32 initrecvbuf(struct recv_buf *precvbuf, PADAPTER padapter)
{
	_rtw_init_listhead(&precvbuf->list);
	_rtw_spinlock_init(&precvbuf->recvbuf_lock);

	precvbuf->adapter = padapter;

	return _SUCCESS;
}

static void freerecvbuf(struct recv_buf *precvbuf)
{
	_rtw_spinlock_free(&precvbuf->recvbuf_lock);
}

#ifdef CONFIG_SDIO_RX_COPY
s32 rtl8723ds_recv_hdl(_adapter *padapter)
{
	PHAL_DATA_TYPE		pHalData;
	struct recv_priv		*precvpriv;
	struct recv_buf	*precvbuf;
	union recv_frame		*precvframe;
	struct recv_frame_hdr	*phdr;
	struct rx_pkt_attrib	*pattrib;
	u8	*ptr;
	u32	pkt_len, pkt_offset;
	u8	rx_report_sz = 0;

	pHalData = GET_HAL_DATA(padapter);
	precvpriv = &padapter->recvpriv;

	do {
		precvbuf = rtw_dequeue_recvbuf(&precvpriv->recv_buf_pending_queue);
		if (NULL == precvbuf)
			break;

		ptr = precvbuf->pdata;

		while (ptr < precvbuf->ptail) {
			precvframe = rtw_alloc_recvframe(&precvpriv->free_recv_queue);
			if (precvframe == NULL) {
				RTW_INFO("%s: no enough recv frame!\n", __FUNCTION__);
				rtw_enqueue_recvbuf_to_head(precvbuf, &precvpriv->recv_buf_pending_queue);
				return RTW_RFRAME_UNAVAIL;
			}

			/* rx desc parsing */
			rtl8723d_query_rx_desc_status(precvframe, ptr);

			pattrib = &precvframe->u.hdr.attrib;

			/* fix Hardware RX data error, drop whole recv_buffer */
			if (!rtw_hal_rcr_check(padapter, RCR_ACRC32) && pattrib->crc_err) {
#if !(MP_DRIVER == 1)
				RTW_INFO("%s()-%d: RX Warning! rx CRC ERROR !!\n", __FUNCTION__, __LINE__);
#endif
				rtw_free_recvframe(precvframe, &precvpriv->free_recv_queue);
				break;
			}

			rx_report_sz = RXDESC_SIZE + pattrib->drvinfo_sz;
			pkt_offset = rx_report_sz + pattrib->shift_sz + pattrib->pkt_len;

			if ((ptr + pkt_offset) > precvbuf->ptail) {
				RTW_INFO("%s()-%d: : next pkt len(%p,%d) exceed ptail(%p)!\n", __FUNCTION__, __LINE__, ptr, pkt_offset, precvbuf->ptail);
				rtw_free_recvframe(precvframe, &precvpriv->free_recv_queue);
				break;
			}

			if ((pattrib->crc_err) || (pattrib->icv_err)) {
#ifdef CONFIG_MP_INCLUDED
				if (padapter->registrypriv.mp_mode == 1) {
					if (check_fwstate(&padapter->mlmepriv, WIFI_MP_STATE) == _TRUE) { /* &&(padapter->mppriv.check_mp_pkt == 0)) */
						if (pattrib->crc_err == 1)
							padapter->mppriv.rx_crcerrpktcount++;
					}
				} else
#endif
				{
					RTW_INFO("%s: crc_err=%d icv_err=%d, skip!\n", __FUNCTION__, pattrib->crc_err, pattrib->icv_err);
				}
				rtw_free_recvframe(precvframe, &precvpriv->free_recv_queue);
			} else {
#ifdef CONFIG_RX_PACKET_APPEND_FCS
				if (check_fwstate(&padapter->mlmepriv, WIFI_MONITOR_STATE) == _FALSE)
					if ((pattrib->pkt_rpt_type == NORMAL_RX) && rtw_hal_rcr_check(padapter, RCR_APPFCS))
						pattrib->pkt_len -= IEEE80211_FCS_LEN;
#endif

				if (rtw_os_alloc_recvframe(padapter, precvframe,
					(ptr + rx_report_sz + pattrib->shift_sz), precvbuf->pskb) == _FAIL) {
					rtw_free_recvframe(precvframe, &precvpriv->free_recv_queue);
					break;
				}

				recvframe_put(precvframe, pattrib->pkt_len);
				/* move to drv info position */
				ptr += RXDESC_SIZE;

				/* update drv info */
				if (rtw_hal_rcr_check(padapter, RCR_APP_BA_SSN)) {
					/* rtl8723s_update_bassn(padapter, pdrvinfo); */
					ptr += 4;
				}

				if (pattrib->pkt_rpt_type == NORMAL_RX) {
					/* skip the rx packet with abnormal length */
					if (pattrib->pkt_len < 14 || pattrib->pkt_len > 8192) {
						RTW_INFO("skip abnormal rx packet(%d)\n", pattrib->pkt_len);
						rtw_free_recvframe(precvframe, &precvpriv->free_recv_queue);
						break;
					}

					pre_recv_entry(precvframe, pattrib->physt ? ptr : NULL);

				} else {
#ifdef CONFIG_FW_C2H_PKT
					if (pattrib->pkt_rpt_type == C2H_PACKET)
						rtw_hal_c2h_pkt_pre_hdl(padapter, precvframe->u.hdr.rx_data, pattrib->pkt_len);
					else {
						RTW_INFO("%s: [WARNNING] RX type(%d) not be handled!\n",
							__FUNCTION__, pattrib->pkt_rpt_type);
					}
#endif
					rtw_free_recvframe(precvframe, &precvpriv->free_recv_queue);
				}
			}

			pkt_offset = _RND8(pkt_offset);
			precvbuf->pdata += pkt_offset;
			ptr = precvbuf->pdata;
			precvframe = NULL;
		}

		rtw_enqueue_recvbuf(precvbuf, &precvpriv->free_recv_buf_queue);
	} while (1);

#ifdef CONFIG_RTW_NAPI
#ifdef CONFIG_RTW_NAPI_V2
	if (padapter->registrypriv.en_napi) {
		struct dvobj_priv *d;
		struct _ADAPTER *a;
		u8 i;

		d = adapter_to_dvobj(padapter);
		for (i = 0; i < d->iface_nums; i++) {
			a = d->padapters[i];
			if (rtw_if_up(a) == _TRUE)
				napi_schedule(&a->napi);
	
		}
	}
#endif /* CONFIG_RTW_NAPI_V2 */
#endif /* CONFIG_RTW_NAPI */
	
	return _SUCCESS;
}

static void rtl8723ds_recv_tasklet(void *priv)
{
	_adapter *adapter = (_adapter *)priv;
	s32 ret;

	ret = rtl8723ds_recv_hdl(adapter);
	if (ret == RTW_RFRAME_UNAVAIL
		|| ret == RTW_RFRAME_PKT_UNAVAIL
	) {
		/* schedule again and hope recvframe/packet is available next time. */
		#ifdef PLATFORM_LINUX
		tasklet_schedule(&adapter->recvpriv.recv_tasklet);
		#endif
	}
}
#else
static void rtl8723ds_recv_tasklet(void *priv)
{
	PADAPTER				padapter;
	PHAL_DATA_TYPE			pHalData;
	struct recv_priv		*precvpriv;
	struct recv_buf		*precvbuf;
	union recv_frame		*precvframe;
	struct recv_frame_hdr	*phdr;
	struct rx_pkt_attrib	*pattrib;
	u8			*ptr;
	_pkt		*ppkt;
	u32		pkt_offset;

	padapter = (PADAPTER)priv;
	pHalData = GET_HAL_DATA(padapter);
	precvpriv = &padapter->recvpriv;

	do {
		precvbuf = rtw_dequeue_recvbuf(&precvpriv->recv_buf_pending_queue);
		if (NULL == precvbuf)
			break;

		ptr = precvbuf->pdata;

		while (ptr < precvbuf->ptail) {
			precvframe = rtw_alloc_recvframe(&precvpriv->free_recv_queue);
			if (precvframe == NULL) {
				rtw_enqueue_recvbuf_to_head(precvbuf, &precvpriv->recv_buf_pending_queue);

				/* The case of can't allocate recvframe should be temporary, */
				/* schedule again and hope recvframe is available next time. */
#ifdef PLATFORM_LINUX
				tasklet_schedule(&precvpriv->recv_tasklet);
#endif
				return;
			}

			phdr = &precvframe->u.hdr;
			pattrib = &phdr->attrib;

			rtl8723d_query_rx_desc_status(precvframe, ptr);

#if 0
			{
				int i, len = 64;
				u8 *pptr = ptr;

				if ((*(pptr + RXDESC_SIZE + pattrib->drvinfo_sz) != 0x80) && (*(pptr + RXDESC_SIZE + pattrib->drvinfo_sz) != 0x40)) {
					RTW_INFO("##############RxDESC###############\n");
					for (i = 0; i < 32; i = i + 16)
						RTW_INFO("%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:\n", *(pptr + i),
							*(pptr + i + 1), *(pptr + i + 2) , *(pptr + i + 3) , *(pptr + i + 4), *(pptr + i + 5), *(pptr + i + 6), *(pptr + i + 7), *(pptr + i + 8),
							*(pptr + i + 9), *(pptr + i + 10),
							*(pptr + i + 11), *(pptr + i + 12), *(pptr + i + 13), *(pptr + i + 14), *(pptr + i + 15));

					if (pattrib->pkt_len < 100)
						len = pattrib->pkt_len;
					pptr = ptr + RXDESC_SIZE + pattrib->drvinfo_sz;
					RTW_INFO("##############Len=%d###############\n", pattrib->pkt_len);
					for (i = 0; i < len; i = i + 16)
						RTW_INFO("%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X:\n", *(pptr + i),
							*(pptr + i + 1), *(pptr + i + 2) , *(pptr + i + 3) , *(pptr + i + 4), *(pptr + i + 5), *(pptr + i + 6), *(pptr + i + 7), *(pptr + i + 8),
							*(pptr + i + 9), *(pptr + i + 10),
							*(pptr + i + 11), *(pptr + i + 12), *(pptr + i + 13), *(pptr + i + 14), *(pptr + i + 15));
					RTW_INFO("#############################\n");
				}
			}
#endif

			/* fix Hardware RX data error, drop whole recv_buffer */
			if (!rtw_hal_rcr_check(padapter, RCR_ACRC32) && pattrib->crc_err) {
				RTW_INFO("%s()-%d: RX Warning! rx CRC ERROR !!\n", __FUNCTION__, __LINE__);
				rtw_free_recvframe(precvframe, &precvpriv->free_recv_queue);
				break;
			}

			pkt_offset = RXDESC_SIZE + pattrib->drvinfo_sz + pattrib->pkt_len;
#if 0 /* reduce check to speed up */
			if ((ptr + pkt_offset) > precvbuf->ptail) {
				rtw_free_recvframe(precvframe, &precvpriv->free_recv_queue);
				break;
			}
#endif

			if ((pattrib->crc_err) || (pattrib->icv_err)) {
#ifdef CONFIG_MP_INCLUDED
				if (padapter->registrypriv.mp_mode == 1) {
					if (check_fwstate(&padapter->mlmepriv, WIFI_MP_STATE) == _TRUE) { /* &&(padapter->mppriv.check_mp_pkt == 0)) */
						if (pattrib->crc_err == 1)
							padapter->mppriv.rx_crcerrpktcount++;
					}
				} else
#endif
				{
					RTW_INFO("%s: crc_err=%d icv_err=%d, skip!\n", __FUNCTION__, pattrib->crc_err, pattrib->icv_err);
				}
				rtw_free_recvframe(precvframe, &precvpriv->free_recv_queue);
			} else {
				ppkt = rtw_skb_clone(precvbuf->pskb);
				if (ppkt == NULL) {
					RTW_INFO("%s: no enough memory to allocate SKB!\n", __FUNCTION__);
					rtw_free_recvframe(precvframe, &precvpriv->free_recv_queue);
					rtw_enqueue_recvbuf_to_head(precvbuf, &precvpriv->recv_buf_pending_queue);

					/* The case of can't allocate skb is serious and may never be recovered, */
					/* once bDriverStopped is enable, this task should be stopped. */
					if (!rtw_is_drv_stopped(padapter)) {
#ifdef PLATFORM_LINUX
						tasklet_schedule(&precvpriv->recv_tasklet);
#endif
					}

					return;
				}

				phdr->pkt = ppkt;
				phdr->len = 0;
				phdr->rx_head = precvbuf->phead;
				phdr->rx_data = phdr->rx_tail = precvbuf->pdata;
				phdr->rx_end = precvbuf->pend;
				recvframe_put(precvframe, pkt_offset);
				recvframe_pull(precvframe, RXDESC_SIZE + pattrib->drvinfo_sz);
				skb_pull(ppkt, RXDESC_SIZE + pattrib->drvinfo_sz);

#ifdef CONFIG_RX_PACKET_APPEND_FCS
				if (check_fwstate(&padapter->mlmepriv, WIFI_MONITOR_STATE) == _FALSE) {
					if ((pattrib->pkt_rpt_type == NORMAL_RX) && rtw_hal_rcr_check(padapter, RCR_APPFCS)) {
						recvframe_pull_tail(precvframe, IEEE80211_FCS_LEN);
						pattrib->pkt_len -= IEEE80211_FCS_LEN;
						ppkt->len = pattrib->pkt_len;
					}
				}
#endif

				/* move to drv info position */
				ptr += RXDESC_SIZE;

				/* update drv info */
				if (rtw_hal_rcr_check(padapter, RCR_APP_BA_SSN)) {
					/* rtl8723s_update_bassn(padapter, pdrvinfo); */
					ptr += 4;
				}

				if (pattrib->pkt_rpt_type == NORMAL_RX)
					pre_recv_entry(precvframe, pattrib->physt ? ptr : NULL);
				else {
#ifdef CONFIG_FW_C2H_PKT
					if (pattrib->pkt_rpt_type == C2H_PACKET)
						rtw_hal_c2h_pkt_pre_hdl(padapter, precvframe->u.hdr.rx_data, pattrib->pkt_len);
					else {
						RTW_INFO("%s: [WARNNING] RX type(%d) not be handled!\n",
							__FUNCTION__, pattrib->pkt_rpt_type);
					}
#endif
					rtw_free_recvframe(precvframe, &precvpriv->free_recv_queue);
				}
			}

			pkt_offset = _RND8(pkt_offset);
			precvbuf->pdata += pkt_offset;
			ptr = precvbuf->pdata;
		}

		rtw_skb_free(precvbuf->pskb);
		precvbuf->pskb = NULL;
		rtw_enqueue_recvbuf(precvbuf, &precvpriv->free_recv_buf_queue);
	} while (1);
}
#endif

/*
 * Initialize recv private variable for hardware dependent
 * 1. recv buf
 * 2. recv tasklet
 *
 */
s32 rtl8723ds_init_recv_priv(PADAPTER padapter)
{
	s32			res;
	u32			i, n;
	struct recv_priv	*precvpriv;
	struct recv_buf		*precvbuf;


	res = _SUCCESS;
	precvpriv = &padapter->recvpriv;

	/* 3 1. init recv buffer */
	_rtw_init_queue(&precvpriv->free_recv_buf_queue);
	_rtw_init_queue(&precvpriv->recv_buf_pending_queue);

	n = NR_RECVBUFF * sizeof(struct recv_buf) + 4;
	precvpriv->pallocated_recv_buf = rtw_zmalloc(n);
	if (precvpriv->pallocated_recv_buf == NULL) {
		res = _FAIL;
		goto exit;
	}

	precvpriv->precv_buf = (u8 *)N_BYTE_ALIGMENT((SIZE_PTR)(precvpriv->pallocated_recv_buf), 4);

	/* init each recv buffer */
	precvbuf = (struct recv_buf *)precvpriv->precv_buf;
	for (i = 0; i < NR_RECVBUFF; i++) {
		res = initrecvbuf(precvbuf, padapter);
		if (res == _FAIL)
			break;

		res = rtw_os_recvbuf_resource_alloc(padapter, precvbuf);
		if (res == _FAIL) {
			freerecvbuf(precvbuf);
			break;
		}

#ifdef CONFIG_SDIO_RX_COPY
		if (precvbuf->pskb == NULL) {
			SIZE_PTR tmpaddr = 0;
			SIZE_PTR alignment = 0;

			precvbuf->pskb = rtw_skb_alloc(MAX_RECVBUF_SZ + RECVBUFF_ALIGN_SZ);

			if (precvbuf->pskb) {
				precvbuf->pskb->dev = padapter->pnetdev;

				tmpaddr = (SIZE_PTR)precvbuf->pskb->data;
				alignment = tmpaddr & (RECVBUFF_ALIGN_SZ - 1);
				skb_reserve(precvbuf->pskb, (RECVBUFF_ALIGN_SZ - alignment));
			}

			if (precvbuf->pskb == NULL)
				RTW_INFO("%s: alloc_skb fail!\n", __FUNCTION__);
		}
#endif

		rtw_list_insert_tail(&precvbuf->list, &precvpriv->free_recv_buf_queue.queue);

		precvbuf++;
	}
	precvpriv->free_recv_buf_queue_cnt = i;

	if (res == _FAIL)
		goto initbuferror;

	/* 3 2. init tasklet */
#ifdef PLATFORM_LINUX
	tasklet_init(&precvpriv->recv_tasklet,
		     (void(*)(unsigned long))rtl8723ds_recv_tasklet,
		     (unsigned long)padapter);
#endif

	goto exit;

initbuferror:
	precvbuf = (struct recv_buf *)precvpriv->precv_buf;
	if (precvbuf) {
		n = precvpriv->free_recv_buf_queue_cnt;
		precvpriv->free_recv_buf_queue_cnt = 0;
		for (i = 0; i < n ; i++) {
			rtw_list_delete(&precvbuf->list);
			rtw_os_recvbuf_resource_free(padapter, precvbuf);
			freerecvbuf(precvbuf);
			precvbuf++;
		}
		precvpriv->precv_buf = NULL;
	}

	if (precvpriv->pallocated_recv_buf) {
		n = NR_RECVBUFF * sizeof(struct recv_buf) + 4;
		rtw_mfree(precvpriv->pallocated_recv_buf, n);
		precvpriv->pallocated_recv_buf = NULL;
	}

exit:
	return res;
}

/*
 * Free recv private variable of hardware dependent
 * 1. recv buf
 * 2. recv tasklet
 *
 */
void rtl8723ds_free_recv_priv(PADAPTER padapter)
{
	u32			i, n;
	struct recv_priv	*precvpriv;
	struct recv_buf		*precvbuf;


	precvpriv = &padapter->recvpriv;

	/* 3 1. kill tasklet */
#ifdef PLATFORM_LINUX
	tasklet_kill(&precvpriv->recv_tasklet);
#endif

	/* 3 2. free all recv buffers */
	precvbuf = (struct recv_buf *)precvpriv->precv_buf;
	if (precvbuf) {
		n = NR_RECVBUFF;
		precvpriv->free_recv_buf_queue_cnt = 0;
		for (i = 0; i < n ; i++) {
			rtw_list_delete(&precvbuf->list);
			rtw_os_recvbuf_resource_free(padapter, precvbuf);
			freerecvbuf(precvbuf);
			precvbuf++;
		}
		precvpriv->precv_buf = NULL;
	}

	if (precvpriv->pallocated_recv_buf) {
		n = NR_RECVBUFF * sizeof(struct recv_buf) + 4;
		rtw_mfree(precvpriv->pallocated_recv_buf, n);
		precvpriv->pallocated_recv_buf = NULL;
	}
}
