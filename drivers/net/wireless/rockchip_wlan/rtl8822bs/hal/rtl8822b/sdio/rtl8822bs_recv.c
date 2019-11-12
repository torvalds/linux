/******************************************************************************
 *
 * Copyright(c) 2015 - 2019 Realtek Corporation.
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
#define _RTL8822BS_RECV_C_

#include <drv_types.h>		/* PADAPTER and etc. */
#include <hal_data.h>		/* HAL_DATA_TYPE */
#include "../../hal_halmac.h"	/* BIT_ACRC32_8822B and etc. */
#include "../rtl8822b.h"	/* rtl8822b_rxdesc2attribute(), rtl8822b_c2h_handler_no_io() */


static s32 initrecvbuf(struct recv_buf *precvbuf, PADAPTER adapter)
{
	_rtw_init_listhead(&precvbuf->list);
	_rtw_spinlock_init(&precvbuf->recvbuf_lock);

	precvbuf->adapter = adapter;

	return _SUCCESS;
}

static void freerecvbuf(struct recv_buf *precvbuf)
{
	_rtw_spinlock_free(&precvbuf->recvbuf_lock);
}

static void start_rx_handle(PADAPTER p)
{
#ifdef CONFIG_RECV_THREAD_MODE
	_rtw_up_sema(&p->recvpriv.recv_sema);
#else
	#ifdef PLATFORM_LINUX
	tasklet_schedule(&p->recvpriv.recv_tasklet);
	#endif
#endif
}

static void stop_rx_handle(PADAPTER p)
{
#ifdef CONFIG_RECV_THREAD_MODE
#else
	#ifdef PLATFORM_LINUX
	tasklet_kill(&p->recvpriv.recv_tasklet);
	#endif
#endif
}

/*
 * Return:
 *	Pointer of _pkt, otherwise NULL.
 */
static _pkt *alloc_recvbuf_skb(struct recv_buf *recvbuf, u32 size)
{
	_pkt *skb;

#ifdef CONFIG_PREALLOC_RX_SKB_BUFFER
	skb = rtw_alloc_skb_premem(size);
	if (!skb) {
		RTW_WARN("%s: Fail to get pre-alloc skb! size=%d\n",
			 __FUNCTION__, size);
		return NULL;
	}
#else /* !CONFIG_PREALLOC_RX_SKB_BUFFER */
	u32 alignsz = RECVBUFF_ALIGN_SZ;
#ifdef PLATFORM_LINUX
	SIZE_PTR tmpaddr = 0;
	SIZE_PTR alignment = 0;
#endif /* PLATFORM_LINUX */


	size += alignsz;
	skb = rtw_skb_alloc(size);
	if (!skb) {
		RTW_WARN("%s: alloc_skb fail! size=%d\n", __FUNCTION__, size);
		return NULL;
	}

#ifdef PLATFORM_LINUX
	tmpaddr = (SIZE_PTR)skb->data;
	alignment = tmpaddr & (alignsz - 1);
	skb_reserve(skb, alignsz - alignment);
#endif /* PLATFORM_LINUX */
#endif /* !CONFIG_PREALLOC_RX_SKB_BUFFER */

#ifdef PLATFORM_LINUX
	skb->dev = recvbuf->adapter->pnetdev;
#endif /* PLATFORM_LINUX */

	recvbuf->pskb = skb;

	return skb;
}

/*
 * Description:
 *	Allocate skb for recv_buf, the size is MAX_RECVBUF_SZ
 *
 * Parameters:
 *	recvbuf	pointer of struct recv_buf
 *	size	skb size, only valid when NOT define CONFIG_SDIO_RX_COPY.
 *		If CONFIG_SDIO_RX_COPY, size always be MAX_RECVBUF_SZ.
 *
 * Return:
 *	Pointer of _pkt, otherwise NULL.
 */
_pkt *rtl8822bs_alloc_recvbuf_skb(struct recv_buf *recvbuf, u32 size)
{
	_pkt *skb;


	skb = recvbuf->pskb;
#ifdef CONFIG_SDIO_RX_COPY
	if (skb) {
		skb_reset_tail_pointer(skb);
		skb->len = 0;
		return skb;
	}

	RTW_WARN("%s: skb not exist in recv_buf!\n", __FUNCTION__);
	size = MAX_RECVBUF_SZ;
#else /* !CONFIG_SDIO_RX_COPY */
	if (skb) {
		RTW_WARN("%s: skb already exist in recv_buf!\n", __FUNCTION__);
		rtl8822bs_free_recvbuf_skb(recvbuf);
	}
#endif /* !CONFIG_SDIO_RX_COPY */

	skb = alloc_recvbuf_skb(recvbuf, size);
	if (!skb)
		return NULL;

	return skb;
}

static void free_recvbuf_skb(struct recv_buf *recvbuf)
{
	_pkt *skb;


	skb = recvbuf->pskb;
	if (!skb)
		return;
	recvbuf->pskb = NULL;
#ifdef CONFIG_PREALLOC_RX_SKB_BUFFER
	rtw_free_skb_premem(skb);
#else /* !CONFIG_PREALLOC_RX_SKB_BUFFER */
	rtw_skb_free(skb);
#endif /* !CONFIG_PREALLOC_RX_SKB_BUFFER */
}

void rtl8822bs_free_recvbuf_skb(struct recv_buf *recvbuf)
{
#ifndef CONFIG_SDIO_RX_COPY
	free_recvbuf_skb(recvbuf);
#endif /* !CONFIG_SDIO_RX_COPY */
}

/*
 * Return:
 *	_SUCCESS	Allocate resource OK.
 *	_FAIL		Fail to allocate resource.
 */
static inline s32 os_recvbuf_resource_alloc(PADAPTER adapter, struct recv_buf *recvbuf)
{
	s32 ret = _SUCCESS;

#ifdef CONFIG_SDIO_RX_COPY
	alloc_recvbuf_skb(recvbuf, MAX_RECVBUF_SZ);
#endif /* CONFIG_SDIO_RX_COPY */

	return ret;
}

static inline void os_recvbuf_resource_free(PADAPTER adapter, struct recv_buf *recvbuf)
{
#ifdef CONFIG_SDIO_RX_COPY
	free_recvbuf_skb(recvbuf);
#endif /* CONFIG_SDIO_RX_COPY */
}
#if 0
static union recv_frame *copy_recvframe(union recv_frame *recvframe, PADAPTER adapter)
{
	PHAL_DATA_TYPE hal;
	struct recv_priv *precvpriv;
	_queue *pfree_recv_queue;
	struct rx_pkt_attrib *attrib = NULL;
	union recv_frame *copyframe = NULL;
	_pkt *copypkt = NULL;


	hal = GET_HAL_DATA(adapter);
	precvpriv = &adapter->recvpriv;
	pfree_recv_queue = &precvpriv->free_recv_queue;
	attrib = &recvframe->u.hdr.attrib;

	copyframe = rtw_alloc_recvframe(pfree_recv_queue);
	if (!copyframe) {
		RTW_INFO(FUNC_ADPT_FMT ": Alloc recvframe FAIL!\n",
			 FUNC_ADPT_ARG(adapter));
		return NULL;
	}
	copyframe->u.hdr.adapter = adapter;
	_rtw_memcpy(&copyframe->u.hdr.attrib, attrib, sizeof(struct rx_pkt_attrib));
#if 0
	/*
	 * driver need to set skb len for skb_copy().
	 * If skb->len is zero, skb_copy() will not copy data from original skb.
	 */
	skb_put(recvframe->u.hdr.pkt, attrib->pkt_len);
#else
	RTW_INFO(FUNC_ADPT_FMT ": skb len=%d!\n",
		 FUNC_ADPT_ARG(adapter), recvframe->u.hdr.pkt->len);
#endif

	copypkt = rtw_skb_copy(recvframe->u.hdr.pkt);
	if (!copypkt) {
		if ((attrib->mfrag == 1) && (attrib->frag_num == 0)) {
			RTW_ERR(FUNC_ADPT_FMT ": rtw_skb_copy fail for first fragment!\n",
				 FUNC_ADPT_ARG(adapter));
			rtw_free_recvframe(recvframe, &precvpriv->free_recv_queue);
			return NULL;
		}

		copypkt = rtw_skb_clone(recvframe->u.hdr.pkt);
		if (!copypkt) {
			RTW_ERR(FUNC_ADPT_FMT ": rtw_skb_clone fail, drop frame!\n",
				 FUNC_ADPT_ARG(adapter));
			rtw_free_recvframe(recvframe, &precvpriv->free_recv_queue);
			return NULL;
		}
	}
	copypkt->dev = adapter->pnetdev;

	copyframe->u.hdr.pkt = copypkt;
	copyframe->u.hdr.len = copypkt->len;
	copyframe->u.hdr.rx_head = copypkt->head;
	copyframe->u.hdr.rx_data = copypkt->data;
	copyframe->u.hdr.rx_tail = skb_tail_pointer(copypkt);
	copyframe->u.hdr.rx_end = skb_end_pointer(copypkt);

	return copyframe;
}
#endif
/*
 * Return:
 *	_SUCCESS	OK to send packet
 *	_FAIL		FAIL to send packet
 */
static s32 recv_entry(union recv_frame *recvframe, u8 *phy_status)
{
	s32 ret = _SUCCESS;
	PADAPTER adapter;
	struct rx_pkt_attrib *attrib = NULL;
#ifdef CONFIG_CONCURRENT_MODE
	struct dvobj_priv *d;
	u8 *addr1, *macaddr;
	u8 mcast, i;
	union recv_frame *copyframe = NULL;
#endif /* CONFIG_CONCURRENT_MODE */


	attrib = &recvframe->u.hdr.attrib;

#if 0
	d = adapter_to_dvobj(recvframe->u.hdr.adapter);
	addr1 = GetAddr1Ptr(recvframe->u.hdr.rx_data);
	mcast = IS_MCAST(addr1);
	if (_TRUE == mcast) {
		/* BC/MC packets */
		for (i = 1; i < d->iface_nums; i++) {
			adapter = d->adapters[i];

			if (rtw_if_up(adapter) == _FALSE)
				continue;

			copyframe = copy_recvframe(recvframe, adapter);
			if (!copyframe)
				break;

			if (attrib->physt)
				rx_query_phy_status(copyframe, phy_status);

			ret = rtw_recv_entry(copyframe);
		}
	} else {
		/* unicast packets */
		for (i = 0; i < d->iface_nums; i++) {
			adapter = d->adapters[i];

			if (rtw_if_up(adapter) == _FALSE)
				continue;

			macaddr = adapter_mac_addr(adapter);
			if (_rtw_memcmp(addr1, macaddr, ETH_ALEN) == _FALSE)
				continue;

			/* change to target interface */
			recvframe->u.hdr.adapter = adapter;
			recvframe->u.hdr.pkt->dev = adapter->pnetdev;
			break;
		}
	}
#else
	ret = pre_recv_entry(recvframe, attrib->physt ? phy_status : NULL);
#endif

	return ret;
}

/*
 * Return:
 *	_TRUE	Finish preparing recv_frame
 *	_FALSE	Something fail to prepare recv_frame
 */
static _pkt *prepare_recvframe_pkt(struct recv_buf *recvbuf, union recv_frame *recvframe)
{
	_pkt *pkt = NULL;
	struct rx_pkt_attrib *attrib;
	u32 desc_size;
	u32 skb_len;
	u8 *data;
#ifdef CONFIG_SDIO_RX_COPY
	u32 shift_sz, alloc_sz;
#endif /* CONFIG_SDIO_RX_COPY */


	pkt = recvframe->u.hdr.pkt;
	if (pkt) {
		RTW_WARN("%s: recvframe pkt already exist!\n", __FUNCTION__);
		return pkt;
	}

	desc_size = rtl8822b_get_rx_desc_size(recvbuf->adapter);

	attrib = &recvframe->u.hdr.attrib;
	skb_len = attrib->pkt_len;
	if (rtl8822b_rx_fcs_appended(recvbuf->adapter))
		skb_len -= IEEE80211_FCS_LEN;
	data = recvbuf->pdata + desc_size + attrib->drvinfo_sz;
#if 0
	data += attrib->shift_sz;
#endif

#ifdef CONFIG_SDIO_RX_COPY
	/* For 8 bytes IP header alignment. */
	if (attrib->qos)
		/* Qos data, wireless lan header length is 26 */
		shift_sz = 6;
	else
		shift_sz = 0;

	/*
	 * For first fragment packet, driver need allocate
	 * (1536 + drvinfo_sz + RXDESC_SIZE) to defrag packet.
	 * In 8822B, drvinfo_sz = 32, RXDESC_SIZE = 24, 1536 + 32 + 24 = 1592.
	 * And need 8 is for skb->data 8 bytes alignment.
	 * Round (1536 + 24 + 32 + shift_sz + 8) to 128 bytes alignment,
	 * and finally get 1664.
	 */
	if ((attrib->mfrag == 1) && (attrib->frag_num == 0)) {
		if (skb_len <= 1650)
			alloc_sz = 1664;
		else
			alloc_sz = skb_len + 14;
	} else {
		alloc_sz = skb_len;
		/*
		 * 6 is for IP header 8 bytes alignment in QoS packet case.
		 * 8 is for skb->data 4 bytes alignment.
		 */
		alloc_sz += 14;
	}

	pkt = rtw_skb_alloc(alloc_sz);
	if (pkt) {
		pkt->dev = recvframe->u.hdr.adapter->pnetdev;
		/* force pkt->data at 8-byte alignment address */
		skb_reserve(pkt, 8 - ((SIZE_PTR)pkt->data & 7));
		/* force ip_hdr at 8-byte alignment address according to shift_sz. */
		skb_reserve(pkt, shift_sz);
		_rtw_memcpy(skb_put(pkt, skb_len), data, skb_len);
	} else if ((attrib->mfrag == 1) && (attrib->frag_num == 0)) {
		RTW_ERR("%s: alloc_skb fail for first fragement\n", __FUNCTION__);
		return NULL;
	}
#endif /* CONFIG_SDIO_RX_COPY */

	if (!pkt) {
		pkt = rtw_skb_clone(recvbuf->pskb);
		if (!pkt) {
			RTW_ERR("%s: rtw_skb_clone fail\n", __FUNCTION__);
			return NULL;
		}
		pkt->data = data;
		skb_set_tail_pointer(pkt, skb_len);
		pkt->len = skb_len;
	}

	recvframe->u.hdr.pkt = pkt;
	recvframe->u.hdr.len = pkt->len;
	recvframe->u.hdr.rx_head = pkt->head;
	recvframe->u.hdr.rx_data = pkt->data;
	recvframe->u.hdr.rx_tail = skb_tail_pointer(pkt);
	recvframe->u.hdr.rx_end = skb_end_pointer(pkt);

	return pkt;
}

/*
 * Return:
 *	_SUCCESS	Finish processing recv_buf
 *	others		Something fail to process recv_buf
 */
static u8 recvbuf_handler(struct recv_buf *recvbuf)
{
	PADAPTER p;
	struct recv_priv *recvpriv;
	union recv_frame *recvframe;
	struct rx_pkt_attrib *attrib;
	u32 desc_size;
	_pkt *pkt;
	u32 rx_report_sz, pkt_offset, pkt_len;
	u8 *ptr;
	u8 ret = _SUCCESS;


	p = recvbuf->adapter;
	recvpriv = &p->recvpriv;
	ptr = recvbuf->pdata;
	desc_size = rtl8822b_get_rx_desc_size(p);

	while (ptr < recvbuf->ptail) {
		recvframe = rtw_alloc_recvframe(&recvpriv->free_recv_queue);
		if (!recvframe) {
			RTW_WARN("%s: no enough recv frame!\n", __FUNCTION__);
			ret = RTW_RFRAME_UNAVAIL;
			break;
		}

		/* rx desc parsing */
		attrib = &recvframe->u.hdr.attrib;
		rtl8822b_rxdesc2attribute(attrib, ptr);

		/* drop recvbuf if pkt_len of rx desc is too small */
		pkt_len = attrib->pkt_len;
		if (pkt_len && rtl8822b_rx_fcs_appended(recvbuf->adapter)) {
			if (pkt_len > IEEE80211_FCS_LEN)
				pkt_len -= IEEE80211_FCS_LEN;
			else
				pkt_len = 0;
		}
		if (pkt_len == 0) {
			RTW_WARN("%s: pkt len(%u) is too small, skip!\n",
				 __FUNCTION__, attrib->pkt_len);
			rtw_free_recvframe(recvframe, &recvpriv->free_recv_queue);
			break;
		}

		rx_report_sz = desc_size + attrib->drvinfo_sz;
		pkt_offset = rx_report_sz + attrib->shift_sz + attrib->pkt_len;

		if ((ptr + pkt_offset) > recvbuf->ptail) {
			RTW_WARN("%s: next pkt len(%p,%d) exceed ptail(%p)!\n",
				 __FUNCTION__, ptr, pkt_offset, recvbuf->ptail);
			rtw_free_recvframe(recvframe, &recvpriv->free_recv_queue);
			break;
		}

		/* fix Hardware RX data error, drop whole recv_buffer */
		if (!rtw_hal_rcr_check(p, BIT_ACRC32_8822B)
		    && attrib->crc_err) {
			RTW_WARN("%s: Received unexpected CRC error packet!!\n", __FUNCTION__);
			rtw_free_recvframe(recvframe, &recvpriv->free_recv_queue);
			break;
		}

		if ((attrib->crc_err) || (attrib->icv_err)) {
#ifdef CONFIG_MP_INCLUDED
			if (p->registrypriv.mp_mode == 1) {
				if (check_fwstate(&p->mlmepriv, WIFI_MP_STATE) == _TRUE) {
					if (attrib->crc_err == 1)
						p->mppriv.rx_crcerrpktcount++;
				}
			} else
#endif /* CONFIG_MP_INCLUDED */
			{
				RTW_INFO("%s: crc_err=%d icv_err=%d, skip!\n",
					__FUNCTION__, attrib->crc_err, attrib->icv_err);
			}
			rtw_free_recvframe(recvframe, &recvpriv->free_recv_queue);
		} else {
			pkt = prepare_recvframe_pkt(recvbuf, recvframe);
			if (!pkt) {
				rtw_free_recvframe(recvframe, &recvpriv->free_recv_queue);
				ret = RTW_RFRAME_PKT_UNAVAIL;
				break;
			}

			/* move to start of PHY_STATUS */
			ptr += desc_size;
			if (rtl8822b_rx_ba_ssn_appended(p))
				ptr += RTW_HALMAC_BA_SSN_RPT_SIZE;

			recv_entry(recvframe, ptr);
		}

		pkt_offset = _RND8(pkt_offset);
		recvbuf->pdata += pkt_offset;
		ptr = recvbuf->pdata;
	}

	return ret;
}

static struct recv_buf* c2h_hdl(struct _ADAPTER *a, struct recv_buf *recvbuf)
{
	u8 c2h = 0;


	c2h = GET_RX_DESC_C2H_8822B(recvbuf->pdata);
	if (c2h) {
		rtl8822b_c2h_handler_no_io(a, recvbuf->pdata, recvbuf->len);

		/* free recv_buf */
		rtl8822bs_free_recvbuf_skb(recvbuf);
		rtw_enqueue_recvbuf(recvbuf, &a->recvpriv.free_recv_buf_queue);
		recvbuf = NULL;
	}

	return recvbuf;
}

s32 rtl8822bs_recv_hdl(_adapter *adapter)
{
	struct recv_priv *recvpriv;
	struct recv_buf *recvbuf;
	s32 ret = _SUCCESS;

	recvpriv = &adapter->recvpriv;
	
	do {
		recvbuf = rtw_dequeue_recvbuf(&recvpriv->recv_buf_pending_queue);
		if (NULL == recvbuf)
			break;

#ifndef RTW_HANDLE_C2H_IN_ISR
		recvbuf = c2h_hdl(adapter, recvbuf);
		if (!recvbuf)
			continue;
#endif /* !RTW_HANDLE_C2H_IN_ISR */

		if (adapter_to_dvobj(adapter)->processing_dev_remove != _TRUE) {
			ret = recvbuf_handler(recvbuf);
			if (ret != _SUCCESS) {
				rtw_enqueue_recvbuf_to_head(recvbuf, &recvpriv->recv_buf_pending_queue);
				break;
			}
		} else {
			/* drop recv buffer */
			RTW_PRINT("%s: drop recv buffer during dev remove!\n", __func__);
		}

		/* free recv_buf */
		rtl8822bs_free_recvbuf_skb(recvbuf);
		rtw_enqueue_recvbuf(recvbuf, &recvpriv->free_recv_buf_queue);
	} while (1);

#ifdef CONFIG_RTW_NAPI
#ifdef CONFIG_RTW_NAPI_V2
	if (adapter->registrypriv.en_napi) {
		struct dvobj_priv *d;
		struct _ADAPTER *a;
		u8 i;

		d = adapter_to_dvobj(adapter);
		for (i = 0; i < d->iface_nums; i++) {
			a = d->padapters[i];
			recvpriv = &a->recvpriv;
			if ((rtw_if_up(a) == _TRUE)
			    && skb_queue_len(&recvpriv->rx_napi_skb_queue))
				napi_schedule(&a->napi);
		}
	}
#endif /* CONFIG_RTW_NAPI_V2 */
#endif /* CONFIG_RTW_NAPI */

	return ret;
}

static void recv_tasklet(void *priv)
{
	PADAPTER adapter;
	s32 ret;

	adapter = (PADAPTER)priv;

	ret = rtl8822bs_recv_hdl(adapter);
	if (ret == RTW_RFRAME_UNAVAIL
		|| ret == RTW_RFRAME_PKT_UNAVAIL)
		start_rx_handle(adapter);
}

/*
 * Initialize recv private variable for hardware dependent
 * 1. recv buf
 * 2. recv tasklet
 */
s32 rtl8822bs_init_recv_priv(PADAPTER adapter)
{
	s32 res;
	u32 i, n;
	struct recv_priv *precvpriv;
	struct recv_buf *precvbuf;


	res = _SUCCESS;
	precvpriv = &adapter->recvpriv;

	/* 1. init recv buffer */
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
		res = initrecvbuf(precvbuf, adapter);
		if (res == _FAIL)
			break;

		res = rtw_os_recvbuf_resource_alloc(adapter, precvbuf);
		if (res == _FAIL) {
			freerecvbuf(precvbuf);
			break;
		}

		res = os_recvbuf_resource_alloc(adapter, precvbuf);
		if (res == _FAIL) {
			freerecvbuf(precvbuf);
			break;
		}

		rtw_list_insert_tail(&precvbuf->list, &precvpriv->free_recv_buf_queue.queue);

		precvbuf++;
	}
	precvpriv->free_recv_buf_queue_cnt = i;

	if (res == _FAIL)
		goto initbuferror;

	/* 2. init tasklet */
#ifdef PLATFORM_LINUX
	tasklet_init(&precvpriv->recv_tasklet,
		     (void(*)(unsigned long))recv_tasklet,
		     (unsigned long)adapter);
#endif

	goto exit;

initbuferror:
	precvbuf = (struct recv_buf *)precvpriv->precv_buf;
	if (precvbuf) {
		n = precvpriv->free_recv_buf_queue_cnt;
		precvpriv->free_recv_buf_queue_cnt = 0;
		for (i = 0; i < n ; i++) {
			rtw_list_delete(&precvbuf->list);
			os_recvbuf_resource_free(adapter, precvbuf);
			rtw_os_recvbuf_resource_free(adapter, precvbuf);
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
 */
void rtl8822bs_free_recv_priv(PADAPTER adapter)
{
	u32 i, n;
	struct recv_priv *precvpriv;
	struct recv_buf *precvbuf;


	precvpriv = &adapter->recvpriv;

	/* 1. kill tasklet */
	stop_rx_handle(adapter);

	/* 2. free all recv buffers */
	precvbuf = (struct recv_buf *)precvpriv->precv_buf;
	if (precvbuf) {
		n = precvpriv->free_recv_buf_queue_cnt;
		precvpriv->free_recv_buf_queue_cnt = 0;
		for (i = 0; i < n ; i++) {
			rtw_list_delete(&precvbuf->list);
			os_recvbuf_resource_free(adapter, precvbuf);
			rtw_os_recvbuf_resource_free(adapter, precvbuf);
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

void rtl8822bs_rxhandler(PADAPTER adapter, struct recv_buf *recvbuf)
{
	struct recv_priv *recvpriv;
	_queue *pending_queue;


#ifdef RTW_HANDLE_C2H_IN_ISR
	recvbuf = c2h_hdl(adapter, recvbuf);
	if (!recvbuf)
		return;
#endif /* RTW_HANDLE_C2H_IN_ISR */

	recvpriv = &adapter->recvpriv;
	pending_queue = &recvpriv->recv_buf_pending_queue;

	/* 1. enqueue recvbuf */
	rtw_enqueue_recvbuf(recvbuf, pending_queue);

	/* 2. schedule tasklet */
	start_rx_handle(adapter);
}
