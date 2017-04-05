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
#define _XMIT_OSDEP_C_

#include <drv_types.h>

#define DBG_DUMP_OS_QUEUE_CTL 0

uint rtw_remainder_len(struct pkt_file *pfile)
{
	return pfile->buf_len - ((SIZE_PTR)(pfile->cur_addr) - (SIZE_PTR)(pfile->buf_start));
}

void _rtw_open_pktfile(_pkt *pktptr, struct pkt_file *pfile)
{

	pfile->pkt = pktptr;
	pfile->cur_addr = pfile->buf_start = pktptr->data;
	pfile->pkt_len = pfile->buf_len = pktptr->len;

	pfile->cur_buffer = pfile->buf_start ;

}

uint _rtw_pktfile_read(struct pkt_file *pfile, u8 *rmem, uint rlen)
{
	uint	len = 0;


	len =  rtw_remainder_len(pfile);
	len = (rlen > len) ? len : rlen;

	if (rmem)
		skb_copy_bits(pfile->pkt, pfile->buf_len - pfile->pkt_len, rmem, len);

	pfile->cur_addr += len;
	pfile->pkt_len -= len;


	return len;
}

sint rtw_endofpktfile(struct pkt_file *pfile)
{

	if (pfile->pkt_len == 0) {
		return _TRUE;
	}


	return _FALSE;
}

void rtw_set_tx_chksum_offload(_pkt *pkt, struct pkt_attrib *pattrib)
{

#ifdef CONFIG_TCP_CSUM_OFFLOAD_TX
	struct sk_buff *skb = (struct sk_buff *)pkt;
	pattrib->hw_tcp_csum = 0;

	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		if (skb_shinfo(skb)->nr_frags == 0) {
			const struct iphdr *ip = ip_hdr(skb);
			if (ip->protocol == IPPROTO_TCP) {
				/* TCP checksum offload by HW */
				RTW_INFO("CHECKSUM_PARTIAL TCP\n");
				pattrib->hw_tcp_csum = 1;
				/* skb_checksum_help(skb); */
			} else if (ip->protocol == IPPROTO_UDP) {
				/* RTW_INFO("CHECKSUM_PARTIAL UDP\n"); */
#if 1
				skb_checksum_help(skb);
#else
				/* Set UDP checksum = 0 to skip checksum check */
				struct udphdr *udp = skb_transport_header(skb);
				udp->check = 0;
#endif
			} else {
				RTW_INFO("%s-%d TCP CSUM offload Error!!\n", __FUNCTION__, __LINE__);
				WARN_ON(1);     /* we need a WARN() */
			}
		} else { /* IP fragmentation case */
			RTW_INFO("%s-%d nr_frags != 0, using skb_checksum_help(skb);!!\n", __FUNCTION__, __LINE__);
			skb_checksum_help(skb);
		}
	}
#endif

}

int rtw_os_xmit_resource_alloc(_adapter *padapter, struct xmit_buf *pxmitbuf, u32 alloc_sz, u8 flag)
{
	if (alloc_sz > 0) {
#ifdef CONFIG_USE_USB_BUFFER_ALLOC_TX
		struct dvobj_priv	*pdvobjpriv = adapter_to_dvobj(padapter);
		struct usb_device	*pusbd = pdvobjpriv->pusbdev;

		pxmitbuf->pallocated_buf = rtw_usb_buffer_alloc(pusbd, (size_t)alloc_sz, &pxmitbuf->dma_transfer_addr);
		pxmitbuf->pbuf = pxmitbuf->pallocated_buf;
		if (pxmitbuf->pallocated_buf == NULL)
			return _FAIL;
#else /* CONFIG_USE_USB_BUFFER_ALLOC_TX */

		pxmitbuf->pallocated_buf = rtw_zmalloc(alloc_sz);
		if (pxmitbuf->pallocated_buf == NULL)
			return _FAIL;

		pxmitbuf->pbuf = (u8 *)N_BYTE_ALIGMENT((SIZE_PTR)(pxmitbuf->pallocated_buf), XMITBUF_ALIGN_SZ);

#endif /* CONFIG_USE_USB_BUFFER_ALLOC_TX */
	}

	if (flag) {
#ifdef CONFIG_USB_HCI
		int i;
		for (i = 0; i < 8; i++) {
			pxmitbuf->pxmit_urb[i] = usb_alloc_urb(0, GFP_KERNEL);
			if (pxmitbuf->pxmit_urb[i] == NULL) {
				RTW_INFO("pxmitbuf->pxmit_urb[i]==NULL");
				return _FAIL;
			}
		}
#endif
	}

	return _SUCCESS;
}

void rtw_os_xmit_resource_free(_adapter *padapter, struct xmit_buf *pxmitbuf, u32 free_sz, u8 flag)
{
	if (flag) {
#ifdef CONFIG_USB_HCI
		int i;

		for (i = 0; i < 8; i++) {
			if (pxmitbuf->pxmit_urb[i]) {
				/* usb_kill_urb(pxmitbuf->pxmit_urb[i]); */
				usb_free_urb(pxmitbuf->pxmit_urb[i]);
			}
		}
#endif
	}

	if (free_sz > 0) {
#ifdef CONFIG_USE_USB_BUFFER_ALLOC_TX
		struct dvobj_priv	*pdvobjpriv = adapter_to_dvobj(padapter);
		struct usb_device	*pusbd = pdvobjpriv->pusbdev;

		rtw_usb_buffer_free(pusbd, (size_t)free_sz, pxmitbuf->pallocated_buf, pxmitbuf->dma_transfer_addr);
		pxmitbuf->pallocated_buf =  NULL;
		pxmitbuf->dma_transfer_addr = 0;
#else	/* CONFIG_USE_USB_BUFFER_ALLOC_TX */
		if (pxmitbuf->pallocated_buf)
			rtw_mfree(pxmitbuf->pallocated_buf, free_sz);
#endif /* CONFIG_USE_USB_BUFFER_ALLOC_TX */
	}
}

void dump_os_queue(void *sel, _adapter *padapter)
{
	struct net_device *ndev = padapter->pnetdev;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35))
	int i;

	for (i = 0; i < 4; i++) {
		RTW_PRINT_SEL(sel, "os_queue[%d]:%s\n"
			, i, __netif_subqueue_stopped(ndev, i) ? "stopped" : "waked");
	}
#else
	RTW_PRINT_SEL(sel, "os_queue:%s\n"
		      , netif_queue_stopped(ndev) ? "stopped" : "waked");
#endif
}

#define WMM_XMIT_THRESHOLD	(NR_XMITFRAME*2/5)

static inline bool rtw_os_need_wake_queue(_adapter *padapter, u16 qidx)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35))
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;

	if (padapter->registrypriv.wifi_spec) {
		if (pxmitpriv->hwxmits[qidx].accnt < WMM_XMIT_THRESHOLD)
			return _TRUE;
	} else {
#ifdef CONFIG_MCC_MODE
		if (MCC_EN(padapter)) {
			if (rtw_hal_check_mcc_status(padapter, MCC_STATUS_DOING_MCC)
			    && MCC_STOP(padapter))
				return _FALSE;
		}
#endif /* CONFIG_MCC_MODE */
		return _TRUE;
	}
	return _FALSE;
#else
#ifdef CONFIG_MCC_MODE
	if (MCC_EN(padapter)) {
		if (rtw_hal_check_mcc_status(padapter, MCC_STATUS_DOING_MCC)
		    && MCC_STOP(padapter))
			return _FALSE;
	}
#endif /* CONFIG_MCC_MODE */
	return _TRUE;
#endif
}

static inline bool rtw_os_need_stop_queue(_adapter *padapter, u16 qidx)
{
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35))
	if (padapter->registrypriv.wifi_spec) {
		/* No free space for Tx, tx_worker is too slow */
		if (pxmitpriv->hwxmits[qidx].accnt > WMM_XMIT_THRESHOLD)
			return _TRUE;
	} else {
		if (pxmitpriv->free_xmitframe_cnt <= 4)
			return _TRUE;
	}
#else
	if (pxmitpriv->free_xmitframe_cnt <= 4)
		return _TRUE;
#endif
	return _FALSE;
}

void rtw_os_pkt_complete(_adapter *padapter, _pkt *pkt)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35))
	u16	qidx;

	qidx = skb_get_queue_mapping(pkt);
	if (rtw_os_need_wake_queue(padapter, qidx)) {
		if (DBG_DUMP_OS_QUEUE_CTL)
			RTW_INFO(FUNC_ADPT_FMT": netif_wake_subqueue[%d]\n", FUNC_ADPT_ARG(padapter), qidx);
		netif_wake_subqueue(padapter->pnetdev, qidx);
	}
#else
	if (rtw_os_need_wake_queue(padapter, 0)) {
		if (DBG_DUMP_OS_QUEUE_CTL)
			RTW_INFO(FUNC_ADPT_FMT": netif_wake_queue\n", FUNC_ADPT_ARG(padapter));
		netif_wake_queue(padapter->pnetdev);
	}
#endif

	rtw_skb_free(pkt);
}

void rtw_os_xmit_complete(_adapter *padapter, struct xmit_frame *pxframe)
{
	if (pxframe->pkt)
		rtw_os_pkt_complete(padapter, pxframe->pkt);

	pxframe->pkt = NULL;
}

void rtw_os_xmit_schedule(_adapter *padapter)
{
#if defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
	_adapter *pri_adapter = GET_PRIMARY_ADAPTER(padapter);

	if (!padapter)
		return;

	if (_rtw_queue_empty(&padapter->xmitpriv.pending_xmitbuf_queue) == _FALSE)
		_rtw_up_sema(&pri_adapter->xmitpriv.xmit_sema);


#else
	_irqL  irqL;
	struct xmit_priv *pxmitpriv;

	if (!padapter)
		return;

	pxmitpriv = &padapter->xmitpriv;

	_enter_critical_bh(&pxmitpriv->lock, &irqL);

	if (rtw_txframes_pending(padapter))
		tasklet_hi_schedule(&pxmitpriv->xmit_tasklet);

	_exit_critical_bh(&pxmitpriv->lock, &irqL);
	
#if defined(CONFIG_PCI_HCI) && defined(CONFIG_XMIT_THREAD_MODE)
	if (_rtw_queue_empty(&padapter->xmitpriv.pending_xmitbuf_queue) == _FALSE)
		_rtw_up_sema(&padapter->xmitpriv.xmit_sema);
#endif
	

#endif
}

static bool rtw_check_xmit_resource(_adapter *padapter, _pkt *pkt)
{
	bool busy = _FALSE;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35))
	u16	qidx;

	qidx = skb_get_queue_mapping(pkt);
	if (rtw_os_need_stop_queue(padapter, qidx)) {
		if (DBG_DUMP_OS_QUEUE_CTL)
			RTW_INFO(FUNC_ADPT_FMT": netif_stop_subqueue[%d]\n", FUNC_ADPT_ARG(padapter), qidx);
		netif_stop_subqueue(padapter->pnetdev, qidx);
		busy = _TRUE;
	}
#else
	if (rtw_os_need_stop_queue(padapter, 0)) {
		if (DBG_DUMP_OS_QUEUE_CTL)
			RTW_INFO(FUNC_ADPT_FMT": netif_stop_queue\n", FUNC_ADPT_ARG(padapter));
		rtw_netif_stop_queue(padapter->pnetdev);
		busy = _TRUE;
	}
#endif
	return busy;
}

void rtw_os_wake_queue_at_free_stainfo(_adapter *padapter, int *qcnt_freed)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35))
	int i;

	for (i = 0; i < 4; i++) {
		if (qcnt_freed[i] == 0)
			continue;

		if (rtw_os_need_wake_queue(padapter, i)) {
			if (DBG_DUMP_OS_QUEUE_CTL)
				RTW_INFO(FUNC_ADPT_FMT": netif_wake_subqueue[%d]\n", FUNC_ADPT_ARG(padapter), i);
			netif_wake_subqueue(padapter->pnetdev, i);
		}
	}
#else
	if (qcnt_freed[0] || qcnt_freed[1] || qcnt_freed[2] || qcnt_freed[3]) {
		if (rtw_os_need_wake_queue(padapter, 0)) {
			if (DBG_DUMP_OS_QUEUE_CTL)
				RTW_INFO(FUNC_ADPT_FMT": netif_wake_queue\n", FUNC_ADPT_ARG(padapter));
			netif_wake_queue(padapter->pnetdev);
		}
	}
#endif
}

#ifdef CONFIG_TX_MCAST2UNI
int rtw_mlcst2unicst(_adapter *padapter, struct sk_buff *skb)
{
	struct	sta_priv *pstapriv = &padapter->stapriv;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	_irqL	irqL;
	_list	*phead, *plist;
	struct sk_buff *newskb;
	struct sta_info *psta = NULL;
	u8 chk_alive_num = 0;
	char chk_alive_list[NUM_STA];
	u8 bc_addr[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	u8 null_addr[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

	int i;
	s32	res;

	DBG_COUNTER(padapter->tx_logs.os_tx_m2u);

	_enter_critical_bh(&pstapriv->asoc_list_lock, &irqL);
	phead = &pstapriv->asoc_list;
	plist = get_next(phead);

	/* free sta asoc_queue */
	while ((rtw_end_of_queue_search(phead, plist)) == _FALSE) {
		int stainfo_offset;
		psta = LIST_CONTAINOR(plist, struct sta_info, asoc_list);
		plist = get_next(plist);

		stainfo_offset = rtw_stainfo_offset(pstapriv, psta);
		if (stainfo_offset_valid(stainfo_offset))
			chk_alive_list[chk_alive_num++] = stainfo_offset;
	}
	_exit_critical_bh(&pstapriv->asoc_list_lock, &irqL);

	for (i = 0; i < chk_alive_num; i++) {
		psta = rtw_get_stainfo_by_offset(pstapriv, chk_alive_list[i]);
		if (!(psta->state & _FW_LINKED)) {
			DBG_COUNTER(padapter->tx_logs.os_tx_m2u_ignore_fw_linked);
			continue;
		}

		/* avoid come from STA1 and send back STA1 */
		if (_rtw_memcmp(psta->hwaddr, &skb->data[6], 6) == _TRUE
			|| _rtw_memcmp(psta->hwaddr, null_addr, 6) == _TRUE
			|| _rtw_memcmp(psta->hwaddr, bc_addr, 6) == _TRUE
		) {
			DBG_COUNTER(padapter->tx_logs.os_tx_m2u_ignore_self);
			continue;
		}

		DBG_COUNTER(padapter->tx_logs.os_tx_m2u_entry);

		newskb = rtw_skb_copy(skb);

		if (newskb) {
			_rtw_memcpy(newskb->data, psta->hwaddr, 6);
			res = rtw_xmit(padapter, &newskb);
			if (res < 0) {
				DBG_COUNTER(padapter->tx_logs.os_tx_m2u_entry_err_xmit);
				RTW_INFO("%s()-%d: rtw_xmit() return error! res=%d\n", __FUNCTION__, __LINE__, res);
				pxmitpriv->tx_drop++;
				rtw_skb_free(newskb);
			}
		} else {
			DBG_COUNTER(padapter->tx_logs.os_tx_m2u_entry_err_skb);
			RTW_INFO("%s-%d: rtw_skb_copy() failed!\n", __FUNCTION__, __LINE__);
			pxmitpriv->tx_drop++;
			/* rtw_skb_free(skb); */
			return _FALSE;	/* Caller shall tx this multicast frame via normal way. */
		}
	}

	rtw_skb_free(skb);
	return _TRUE;
}
#endif /* CONFIG_TX_MCAST2UNI */


int _rtw_xmit_entry(_pkt *pkt, _nic_hdl pnetdev)
{
	_adapter *padapter = (_adapter *)rtw_netdev_priv(pnetdev);
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
#ifdef CONFIG_TX_MCAST2UNI
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	extern int rtw_mc2u_disable;
#endif /* CONFIG_TX_MCAST2UNI	 */
	s32 res = 0;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35))
	u16 queue;
#endif


	if (padapter->registrypriv.mp_mode) {
		RTW_INFO("MP_TX_DROP_OS_FRAME\n");
		goto drop_packet;
	}
	DBG_COUNTER(padapter->tx_logs.os_tx);

	if (rtw_if_up(padapter) == _FALSE) {
		DBG_COUNTER(padapter->tx_logs.os_tx_err_up);
		#ifdef DBG_TX_DROP_FRAME
		RTW_INFO("DBG_TX_DROP_FRAME %s if_up fail\n", __FUNCTION__);
		#endif
		goto drop_packet;
	}

	rtw_check_xmit_resource(padapter, pkt);

#ifdef CONFIG_TX_MCAST2UNI
	if (!rtw_mc2u_disable
		&& check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE
		&& (IP_MCAST_MAC(pkt->data)
			|| ICMPV6_MCAST_MAC(pkt->data)
			#ifdef CONFIG_TX_BCAST2UNI
			|| is_broadcast_mac_addr(pkt->data)
			#endif
			)
		&& (padapter->registrypriv.wifi_spec == 0)
	) {
		if (pxmitpriv->free_xmitframe_cnt > (NR_XMITFRAME / 4)) {
			res = rtw_mlcst2unicst(padapter, pkt);
			if (res == _TRUE)
				goto exit;
		} else {
			/* RTW_INFO("Stop M2U(%d, %d)! ", pxmitpriv->free_xmitframe_cnt, pxmitpriv->free_xmitbuf_cnt); */
			/* RTW_INFO("!m2u ); */
			DBG_COUNTER(padapter->tx_logs.os_tx_m2u_stop);
		}
	}
#endif /* CONFIG_TX_MCAST2UNI	 */

	res = rtw_xmit(padapter, &pkt);
	if (res < 0) {
		#ifdef DBG_TX_DROP_FRAME
		RTW_INFO("DBG_TX_DROP_FRAME %s rtw_xmit fail\n", __FUNCTION__);
		#endif
		goto drop_packet;
	}

	goto exit;

drop_packet:
	pxmitpriv->tx_drop++;
	rtw_os_pkt_complete(padapter, pkt);

exit:


	return 0;
}

int rtw_xmit_entry(_pkt *pkt, _nic_hdl pnetdev)
{
	_adapter *padapter = (_adapter *)rtw_netdev_priv(pnetdev);
	struct	mlme_priv	*pmlmepriv = &(padapter->mlmepriv);
	int ret = 0;

	if (pkt) {
		if (check_fwstate(pmlmepriv, WIFI_MONITOR_STATE) == _TRUE) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 24))
			rtw_monitor_xmit_entry((struct sk_buff *)pkt, pnetdev);
#endif
		}
		else {
			rtw_mstat_update(MSTAT_TYPE_SKB, MSTAT_ALLOC_SUCCESS, pkt->truesize);
			ret = _rtw_xmit_entry(pkt, pnetdev);
		}

	}

	return ret;
}
