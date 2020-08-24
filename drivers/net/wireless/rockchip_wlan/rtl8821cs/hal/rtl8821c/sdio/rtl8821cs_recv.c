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
#define _RTL8821CS_RECV_C_

#include <drv_types.h>		/* PADAPTER and etc. */
#include <hal_data.h>		/* HAL_DATA_TYPE */
#include "../../hal_halmac.h"	/* BIT_ACRC32_8821C and etc. */
#include "../rtl8821c.h"	/* rtl8821c_rxdesc2attribute(), rtl8821c_c2h_handler_no_io() */
#include "rtl8821cs_recv.h"	/* MAX_RECVBUF_SZ */


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

#if 0
/*
 * Return:
 *	Pointer of _pkt, otherwise NULL.
 */
static _pkt *alloc_recvbuf_skb(struct recv_buf *recvbuf, u32 size)
{
	_pkt *skb;
	u32 alignsz = RECVBUFF_ALIGN_SZ;
#ifdef PLATFORM_LINUX
	SIZE_PTR tmpaddr = 0;
	SIZE_PTR alignment = 0;
#endif /* PLATFORM_LINUX */


	size += alignsz;
	skb = rtw_skb_alloc(size);
	if (!skb) {
		RTW_INFO("%s: <WARN> alloc_skb fail! size=%d\n", __FUNCTION__, size);
		return NULL;
	}

#ifdef PLATFORM_LINUX
	skb->dev = recvbuf->adapter->pnetdev;

	tmpaddr = (SIZE_PTR)skb->data;
	alignment = tmpaddr & (alignsz - 1);
	skb_reserve(skb, alignsz - alignment);
#endif /* PLATFORM_LINUX */

	recvbuf->pskb = skb;

	return skb;
}

/*
 * Description:
 *	Allocate skb for recv_buf, the size is MAX_RECVBUF_SZ_8821C (24KB)
 *
 * Parameters:
 *	recvbuf	pointer of struct recv_buf
 *	size	skb size, only valid when NOT define CONFIG_SDIO_RX_COPY.
 *		If CONFIG_SDIO_RX_COPY, size always be MAX_RECVBUF_SZ_8821C.
 *
 * Return:
 *	Pointer of _pkt, otherwise NULL.
 */
_pkt *rtl8821cs_alloc_recvbuf_skb(struct recv_buf *recvbuf, u32 size)
{
	_pkt *skb;


	skb = recvbuf->pskb;
#ifdef CONFIG_SDIO_RX_COPY
	if (skb) {
		skb_reset_tail_pointer(skb);
		skb->len = 0;
		return skb;
	}

	RTW_INFO("%s: <WARN> skb not exist in recv_buf!\n", __FUNCTION__);
	size = MAX_RECVBUF_SZ_8821C;
#else /* !CONFIG_SDIO_RX_COPY */
	if (skb) {
		RTW_INFO("%s: <WARN> skb already exist in recv_buf!\n", __FUNCTION__);
		rtl8821cs_free_recvbuf_skb(recvbuf);
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
	rtw_skb_free(skb);
}

void rtl8821cs_free_recvbuf_skb(struct recv_buf *recvbuf)
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
	alloc_recvbuf_skb(recvbuf, MAX_RECVBUF_SZ_8821C);
#endif /* CONFIG_SDIO_RX_COPY */

	return ret;
}

static inline void os_recvbuf_resource_free(PADAPTER adapter, struct recv_buf *recvbuf)
{
#ifdef CONFIG_SDIO_RX_COPY
	free_recvbuf_skb(recvbuf);
#endif /* CONFIG_SDIO_RX_COPY */
}

static union recv_frame *copy_recvframe(union recv_frame *recvframe, PADAPTER adapter)
{
	PHAL_DATA_TYPE pHalData;
	struct recv_priv *precvpriv;
	_queue *pfree_recv_queue;
	struct rx_pkt_attrib *attrib = NULL;
	union recv_frame *copyframe = NULL;
	_pkt *copypkt = NULL;


	pHalData = GET_HAL_DATA(adapter);
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
			RTW_INFO(FUNC_ADPT_FMT ": <ERR> rtw_skb_copy fail for first fragment!\n",
				 FUNC_ADPT_ARG(adapter));
			rtw_free_recvframe(recvframe, &precvpriv->free_recv_queue);
			return NULL;
		}

		copypkt = rtw_skb_clone(recvframe->u.hdr.pkt);
		if (!copypkt) {
			RTW_INFO(FUNC_ADPT_FMT ": <ERR> rtw_skb_clone fail, drop frame!\n",
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

#ifdef CONFIG_CONCURRENT_MODE
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
#endif /* CONFIG_CONCURRENT_MODE */

	if (attrib->physt)
		rx_query_phy_status(recvframe, phy_status);

	ret = rtw_recv_entry(recvframe);

	return ret;
}

/*
 * Return:
 *	_TRUE	Finish preparing recv_frame
 *	_FALSE	Something fail to prepare recv_frame
 */
static _pkt *prepare_recvframe_pkt(struct recv_buf *recvbuf, union recv_frame *recvframe)
{
	u32 desc_size;
	_pkt *pkt = NULL;
	struct rx_pkt_attrib *attrib;
	u32 skb_len;
	u8 *data;
#ifdef CONFIG_SDIO_RX_COPY
	u32 shift_sz, alloc_sz;
#endif /* CONFIG_SDIO_RX_COPY */

	rtw_halmac_get_rx_desc_size(adapter_to_dvobj(recvbuf->adapter), &desc_size);

	pkt = recvframe->u.hdr.pkt;
	if (pkt) {
		RTW_INFO("%s: <WARN> recvframe pkt already exist!\n", __FUNCTION__);
		return pkt;
	}

	attrib = &recvframe->u.hdr.attrib;
	skb_len = attrib->pkt_len;
	if (rtl8821c_rx_fcs_appended(recvbuf->adapter))
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
	 * In 8821C, drvinfo_sz = 32, RXDESC_SIZE = 24, 1536 + 32 + 24 = 1592.
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
		RTW_INFO("%s: <ERR> alloc_skb fail for first fragement\n", __FUNCTION__);
		return NULL;
	}
#endif /* CONFIG_SDIO_RX_COPY */

	if (!pkt) {
		pkt = rtw_skb_clone(recvbuf->pskb);
		if (!pkt) {
			RTW_INFO("%s: <ERR> rtw_skb_clone fail\n", __FUNCTION__);
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
 * process Normal Rx packets
 * Return:
 *	_TRUE	Finish processing recv_buf
 *	_FALSE	Something fail to process recv_buf
 */
static u8 recvbuf_handler(struct recv_buf *recvbuf)
{
	PADAPTER p;
	struct recv_priv *recvpriv;
	union recv_frame *recvframe;
	struct rx_pkt_attrib *attrib;
	_pkt *pkt;
	u32 desc_size;
	u32 rx_report_sz, pkt_offset;
	u8 *ptr;
	u8 ret = _TRUE;


	p = recvbuf->adapter;
	recvpriv = &p->recvpriv;
	ptr = recvbuf->pdata;
	rtw_halmac_get_rx_desc_size(adapter_to_dvobj(p), &desc_size);

	while (ptr < recvbuf->ptail) {
		recvframe = rtw_alloc_recvframe(&recvpriv->free_recv_queue);
		if (!recvframe) {
			RTW_INFO("%s: <WARN> no enough recv frame!\n", __FUNCTION__);
			ret = _FALSE;
			break;
		}

		/* rx desc parsing */
		attrib = &recvframe->u.hdr.attrib;
		rtl8821c_rxdesc2attribute(attrib, ptr);

		rx_report_sz = desc_size + attrib->drvinfo_sz;
		pkt_offset = rx_report_sz + attrib->shift_sz + attrib->pkt_len;

		if ((ptr + pkt_offset) > recvbuf->ptail) {
			RTW_INFO("%s: <WARN> next pkt len(%p,%d) exceed ptail(%p)!\n",
				 __FUNCTION__, ptr, pkt_offset, recvbuf->ptail);
			rtw_free_recvframe(recvframe, &recvpriv->free_recv_queue);
			break;
		}

		/* fix Hardware RX data error, drop whole recv_buffer */
		if (!rtw_hal_rcr_check(p, BIT_ACRC32_8821C)
		    && attrib->crc_err) {
			RTW_INFO("%s: <WARN> Received unexpected CRC error packet!!\n", __FUNCTION__);
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
				ret = _FALSE;
				break;
			}

			/* move to start of PHY_STATUS */
			ptr += desc_size;
			if (rtl8821c_rx_ba_ssn_appended(p))
				ptr += RTW_HALMAC_BA_SSN_RPT_SIZE;

			recv_entry(recvframe, ptr);
		}

		pkt_offset = _RND8(pkt_offset);
		recvbuf->pdata += pkt_offset;
		ptr = recvbuf->pdata;
	}

	return ret;
}

static void rtl8821c_recv_tasklet(void *priv)
{
	PADAPTER adapter;
	struct recv_priv *recvpriv;
	struct recv_buf *recvbuf;
	u8 ret = _TRUE;


	adapter = (PADAPTER)priv;
	recvpriv = &adapter->recvpriv;

	do {
		recvbuf = rtw_dequeue_recvbuf(&recvpriv->recv_buf_pending_queue);
		if (NULL == recvbuf)
			break;


		if (GET_RX_DESC_C2H_8821C(recvbuf->pdata)) {
			if (GET_RX_DESC_DRV_INFO_SIZE_8821C(recvbuf->pdata)) {
				RTW_INFO("%s [WARN] DRV_INFO_SIZE != 0\n", __func__);
				rtw_warn_on(1);
			}

			c2h_pre_handler_rtl8821c(adapter, recvbuf->pdata, recvbuf->len);
		} else
			ret = recvbuf_handler(recvbuf);

		if (_FALSE == ret) {
			rtw_enqueue_recvbuf_to_head(recvbuf, &recvpriv->recv_buf_pending_queue);
			rtw_msleep_os(5);
			start_rx_handle(adapter);
			break;
		}

		/* free recv_buf */
		rtl8821cs_free_recvbuf_skb(recvbuf);
		rtw_enqueue_recvbuf(recvbuf, &recvpriv->free_recv_buf_queue);
	} while (1);
}
#endif

#ifdef CONFIG_SDIO_RX_COPY
s32 rtl8821cs_recv_hdl(_adapter *padapter)
{
	PHAL_DATA_TYPE		pHalData;
	struct recv_priv		*precvpriv;
	struct recv_buf		*precvbuf;
	union recv_frame		*precvframe;
	struct recv_frame_hdr	*phdr;
	struct rx_pkt_attrib	*pattrib;
	u8		*ptr;
	u32		desc_size;
	u32		pkt_offset;
	s32		transfer_len;
	u8		*pphy_status = NULL;
	u8		rx_report_sz = 0;

	pHalData = GET_HAL_DATA(padapter);
	precvpriv = &padapter->recvpriv;

	rtw_halmac_get_rx_desc_size(adapter_to_dvobj(padapter), &desc_size);

	do {
		if (RTW_CANNOT_RUN(padapter)) {
			RTW_INFO("%s => bDriverStopped or bSurpriseRemoved\n", __func__);
			break;
		}

		precvbuf = rtw_dequeue_recvbuf(&precvpriv->recv_buf_pending_queue);
		if (NULL == precvbuf)
			break;

		transfer_len = (s32)precvbuf->len;
		ptr = precvbuf->pdata;

		do {
			precvframe = rtw_alloc_recvframe(&precvpriv->free_recv_queue);
			if (precvframe == NULL) {
				rtw_enqueue_recvbuf_to_head(precvbuf, &precvpriv->recv_buf_pending_queue);
				return RTW_RFRAME_UNAVAIL;
			}

			/*rx desc parsing*/
			pattrib = &precvframe->u.hdr.attrib;
			rtl8821c_rxdesc2attribute(pattrib, ptr);

			/* fix Hardware RX data error, drop whole recv_buffer*/
			if (!rtw_hal_rcr_check(padapter, BIT_ACRC32_8821C) && pattrib->crc_err) {

				if (padapter->registrypriv.mp_mode == 0)
					RTW_INFO("%s()-%d: RX Warning! rx CRC ERROR !!\n", __func__, __LINE__);

				rtw_free_recvframe(precvframe, &precvpriv->free_recv_queue);
				break;
			}

			/*if (rtl8821c_rx_ba_ssn_appended(p))*/
			if (rtw_hal_rcr_check(padapter, BIT_APP_BASSN_8821C))
				rx_report_sz = desc_size + RTW_HALMAC_BA_SSN_RPT_SIZE + pattrib->drvinfo_sz;
			else
				rx_report_sz = desc_size + pattrib->drvinfo_sz;

			pkt_offset = rx_report_sz + pattrib->shift_sz + pattrib->pkt_len;

			if ((pattrib->pkt_len == 0) || (pkt_offset > transfer_len)) {
				RTW_INFO("%s()-%d: RX Warning!, pkt_len==0 or pkt_offset(%d)> transfoer_len(%d)\n", __func__, __LINE__, pkt_offset, transfer_len);
				rtw_free_recvframe(precvframe, &precvpriv->free_recv_queue);
				break;
			}

			if ((pattrib->crc_err) || (pattrib->icv_err)) {
#ifdef CONFIG_MP_INCLUDED
				if (padapter->registrypriv.mp_mode == 1) {
					if (check_fwstate(&padapter->mlmepriv, WIFI_MP_STATE)) { /*&&(padapter->mppriv.check_mp_pkt == 0))*/
						if (pattrib->crc_err == 1)
							padapter->mppriv.rx_crcerrpktcount++;
					}
				}
#endif

				RTW_INFO("%s: crc_err=%d icv_err=%d, skip!\n", __func__, pattrib->crc_err, pattrib->icv_err);
				rtw_free_recvframe(precvframe, &precvpriv->free_recv_queue);
			} else {

				if (pattrib->pkt_rpt_type == NORMAL_RX) { /*Normal rx packet*/

#ifdef CONFIG_RX_PACKET_APPEND_FCS
				if (check_fwstate(&padapter->mlmepriv, WIFI_MONITOR_STATE) == _FALSE)
					if ((pattrib->pkt_rpt_type == NORMAL_RX) && rtw_hal_rcr_check(padapter, BIT_APP_FCS_8821C))
						pattrib->pkt_len -= IEEE80211_FCS_LEN;
#endif

				if (rtw_os_alloc_recvframe(padapter, precvframe,
					(ptr + rx_report_sz + pattrib->shift_sz), precvbuf->pskb) == _FAIL) {
					rtw_free_recvframe(precvframe, &precvpriv->free_recv_queue);
					break;
				}

				recvframe_put(precvframe, pattrib->pkt_len);
				/*recvframe_pull(precvframe, drvinfo_sz + RXDESC_SIZE);*/


				/* update drv info*/
#if 0
				if (rtw_hal_rcr_check(padapter, BIT_APP_BASSN_8821C)) {
					/*rtl8821c_update_bassn(padapter, (ptr + RXDESC_SIZE));*/
				}
#endif

					pre_recv_entry(precvframe, pattrib->physt ? (ptr + rx_report_sz - pattrib->drvinfo_sz) : NULL);

				} else { /* C2H_PACKET */

					c2h_pre_handler_rtl8821c(padapter, ptr, transfer_len);
					rtw_free_recvframe(precvframe, &precvpriv->free_recv_queue);

				}
			}

			/* Page size of receive package is 128 bytes alignment =>DMA AGG*/

			pkt_offset = _RND8(pkt_offset);
			transfer_len -= pkt_offset;
			ptr += pkt_offset;
			precvframe = NULL;

		} while (transfer_len > 0);

		precvbuf->len = 0;

		rtw_enqueue_recvbuf(precvbuf, &precvpriv->free_recv_buf_queue);
	} while (1);

#ifdef CONFIG_RTW_NAPI
#ifdef CONFIG_RTW_NAPI_V2
	if (padapter->registrypriv.en_napi) {
		struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
		struct _ADAPTER *iface;
		u8 i;

		for (i = 0; i < dvobj->iface_nums; i++) {
			iface = dvobj->padapters[i];
			precvpriv = &iface->recvpriv; 
			if (rtw_if_up(iface) == _TRUE
				&& skb_queue_len(&precvpriv->rx_napi_skb_queue))
				napi_schedule(&iface->napi);
		}
	}
#endif /* CONFIG_RTW_NAPI_V2 */
#endif /* CONFIG_RTW_NAPI */

	return _SUCCESS;

}

static void rtl8821c_recv_tasklet(void *priv)
{
	_adapter *adapter = (_adapter *)priv;
	s32 ret;

	ret = rtl8821cs_recv_hdl(adapter);
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

static void rtl8821c_recv_tasklet(void *priv)
{
	PADAPTER				padapter;
	PHAL_DATA_TYPE			pHalData;
	struct recv_priv		*precvpriv;
	struct recv_buf			*precvbuf;
	union recv_frame		*precvframe;
	struct recv_frame_hdr	*phdr;
	struct rx_pkt_attrib	*pattrib;
	u8		*ptr;
	_pkt		*ppkt;
	u32		desc_size;
	u32		pkt_offset;

	padapter = (PADAPTER)priv;
	pHalData = GET_HAL_DATA(padapter);
	precvpriv = &padapter->recvpriv;
	rtw_halmac_get_rx_desc_size(adapter_to_dvobj(padapter), &desc_size);

	do {
		precvbuf = rtw_dequeue_recvbuf(&precvpriv->recv_buf_pending_queue);
		if (NULL == precvbuf)
			break;

		ptr = precvbuf->pdata;

		while (ptr < precvbuf->ptail) {
			precvframe = rtw_alloc_recvframe(&precvpriv->free_recv_queue);
			if (precvframe == NULL) {
				RTW_ERR("%s: no enough recv frame!\n", __FUNCTION__);
				rtw_enqueue_recvbuf_to_head(precvbuf, &precvpriv->recv_buf_pending_queue);

				/* The case of can't allocate recvframe should be temporary,
				schedule again and hope recvframe is available next time.*/
				tasklet_schedule(&precvpriv->recv_tasklet);

				return;
			}

			phdr = &precvframe->u.hdr;
			pattrib = &phdr->attrib;

			/*rx desc parsing*/
			rtl8821c_rxdesc2attribute(pattrib, ptr);

			/* fix Hardware RX data error, drop whole recv_buffer*/
			if (!rtw_hal_rcr_check(padapter, BIT_ACRC32_8821C) && pattrib->crc_err) {
				/*#if !(MP_DRIVER==1)*/
				if (padapter->registrypriv.mp_mode == 0)
					RTW_INFO("%s()-%d: RX Warning! rx CRC ERROR !!\n", __FUNCTION__, __LINE__);
				/*#endif*/
				rtw_free_recvframe(precvframe, &precvpriv->free_recv_queue);
				break;
			}

			pkt_offset = desc_size + pattrib->drvinfo_sz + pattrib->pkt_len;

			if ((ptr + pkt_offset) > precvbuf->ptail) {
				RTW_INFO("%s()-%d: : next pkt len(%p,%d) exceed ptail(%p)!\n", __FUNCTION__, __LINE__, ptr, pkt_offset, precvbuf->ptail);
				rtw_free_recvframe(precvframe, &precvpriv->free_recv_queue);
				break;
			}

			if ((pattrib->crc_err) || (pattrib->icv_err)) {
#ifdef CONFIG_MP_INCLUDED
				if (padapter->registrypriv.mp_mode == 1) {
					if (check_fwstate(&padapter->mlmepriv, WIFI_MP_STATE)) { /*&&(padapter->mppriv.check_mp_pkt == 0))*/
						if (pattrib->crc_err == 1)
							padapter->mppriv.rx_crcerrpktcount++;
					}
				}
#endif

				RTW_INFO("%s: crc_err=%d icv_err=%d, skip!\n", __func__, pattrib->crc_err, pattrib->icv_err);
				rtw_free_recvframe(precvframe, &precvpriv->free_recv_queue);
			} else {
				ppkt = rtw_skb_clone(precvbuf->pskb);
				if (ppkt == NULL) {
					rtw_free_recvframe(precvframe, &precvpriv->free_recv_queue);
					rtw_enqueue_recvbuf_to_head(precvbuf, &precvpriv->recv_buf_pending_queue);

					/* The case of can't allocate skb is serious and may never be recovered,
					 once bDriverStopped is enable, this task should be stopped.*/
					if (!rtw_is_drv_stopped(padapter))
						tasklet_schedule(&precvpriv->recv_tasklet);

					return;
				}

				phdr->pkt = ppkt;
				phdr->len = 0;
				phdr->rx_head = precvbuf->phead;
				phdr->rx_data = phdr->rx_tail = precvbuf->pdata;
				phdr->rx_end = precvbuf->pend;

				recvframe_put(precvframe, pkt_offset);
				recvframe_pull(precvframe, desc_size + pattrib->drvinfo_sz);
				skb_pull(ppkt, desc_size + pattrib->drvinfo_sz);

#ifdef CONFIG_RX_PACKET_APPEND_FCS
				if (check_fwstate(&padapter->mlmepriv, WIFI_MONITOR_STATE) == _FALSE) {
					if ((pattrib->pkt_rpt_type == NORMAL_RX) && rtw_hal_rcr_check(padapter, BIT_APP_FCS_8821C)) {
						recvframe_pull_tail(precvframe, IEEE80211_FCS_LEN);
						pattrib->pkt_len -= IEEE80211_FCS_LEN;
						ppkt->len = pattrib->pkt_len;
					}
				}
#endif

				/* move to drv info position*/
				ptr += desc_size;

				/* update drv info*/
				if (rtw_hal_rcr_check(padapter, BIT_APP_BASSN_8821C)) {
					/*rtl8821cs_update_bassn(padapter, pdrvinfo);*/
					ptr += RTW_HALMAC_BA_SSN_RPT_SIZE;
				}

				if (pattrib->pkt_rpt_type == NORMAL_RX) /*Normal rx packet*/
					pre_recv_entry(precvframe, pattrib->physt ? ptr : NULL);
				else { /* C2H_PACKET*/
					c2h_pre_handler_rtl8821c(padapter, ptr, transfer_len);
					rtw_free_recvframe(precvframe, &precvpriv->free_recv_queue);
				}
			}

			/* Page size of receive package is 128 bytes alignment =>DMA AGG*/

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
 */
s32 rtl8821cs_init_recv_priv(PADAPTER adapter)
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
#ifdef CONFIG_SDIO_RX_COPY
		if (precvbuf->pskb == NULL) {
			SIZE_PTR tmpaddr = 0;
			SIZE_PTR alignment = 0;

			precvbuf->pskb = rtw_skb_alloc(MAX_RECVBUF_SZ + RECVBUFF_ALIGN_SZ);

			if (precvbuf->pskb) {
				precvbuf->pskb->dev = adapter->pnetdev;

				tmpaddr = (SIZE_PTR)precvbuf->pskb->data;
				alignment = tmpaddr & (RECVBUFF_ALIGN_SZ - 1);
				skb_reserve(precvbuf->pskb, (RECVBUFF_ALIGN_SZ - alignment));

				precvbuf->phead = precvbuf->pskb->head;
				precvbuf->pdata = precvbuf->pskb->data;
				precvbuf->ptail = skb_tail_pointer(precvbuf->pskb);
				precvbuf->pend = skb_end_pointer(precvbuf->pskb);
				precvbuf->len = 0;
			}

			if (precvbuf->pskb == NULL)
				RTW_INFO("%s: alloc_skb fail!\n", __FUNCTION__);
		}
#endif

#if 0
		res = os_recvbuf_resource_alloc(adapter, precvbuf);
		if (res == _FAIL) {
			freerecvbuf(precvbuf);
			break;
		}
#endif
		rtw_list_insert_tail(&precvbuf->list, &precvpriv->free_recv_buf_queue.queue);

		precvbuf++;
	}
	precvpriv->free_recv_buf_queue_cnt = i;

	if (res == _FAIL)
		goto initbuferror;

	/* 2. init tasklet */
#ifdef PLATFORM_LINUX
	tasklet_init(&precvpriv->recv_tasklet,
		     (void(*)(unsigned long))rtl8821c_recv_tasklet,
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
void rtl8821cs_free_recv_priv(PADAPTER adapter)
{
	u32 i, n;
	struct recv_priv *precvpriv;
	struct recv_buf *precvbuf;


	precvpriv = &adapter->recvpriv;

	/* 1. kill tasklet */
#ifdef PLATFORM_LINUX
	tasklet_kill(&adapter->recvpriv.recv_tasklet);
#endif

	/* 2. free all recv buffers */
	precvbuf = (struct recv_buf *)precvpriv->precv_buf;
	if (precvbuf) {
		n = NR_RECVBUFF;
		precvpriv->free_recv_buf_queue_cnt = 0;
		for (i = 0; i < n ; i++) {
			rtw_list_delete(&precvbuf->list);
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

void rtl8821cs_rxhandler(PADAPTER adapter, struct recv_buf *recvbuf)
{
	struct recv_priv *recvpriv = &adapter->recvpriv;
	_queue *pending_queue = &recvpriv->recv_buf_pending_queue;


	/*enqueue recvbuf*/
	rtw_enqueue_recvbuf(recvbuf, pending_queue);

	/*schedule tasklet*/
#ifdef CONFIG_RECV_THREAD_MODE
	_rtw_up_sema(&recvpriv->recv_sema);
#else
#ifdef PLATFORM_LINUX
	tasklet_schedule(&recvpriv->recv_tasklet);
#endif
#endif
}
