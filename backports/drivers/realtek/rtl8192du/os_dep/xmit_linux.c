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
 *
 ******************************************************************************/
#define _XMIT_OSDEP_C_

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>

#include <linux/if_ether.h>
#include <linux/ip.h>
#include <wifi.h>
#include <mlme_osdep.h>
#include <xmit_osdep.h>
#include <osdep_intf.h>
#include <usb_osintf.h>

void rtw_set_tx_chksum_offload(struct sk_buff *pkt, struct pkt_attrib *pattrib)
{

#ifdef CONFIG_TCP_CSUM_OFFLOAD_TX
	struct sk_buff *skb = (struct sk_buff *)pkt;
	pattrib->hw_tcp_csum = 0;

	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		if (skb_shinfo(skb)->nr_frags == 0)
		{
                        const struct iphdr *ip = ip_hdr(skb);
                        if (ip->protocol == IPPROTO_TCP) {
                                /*  TCP checksum offload by HW */
                                DBG_8192D("CHECKSUM_PARTIAL TCP\n");
                                pattrib->hw_tcp_csum = 1;
                                /* skb_checksum_help(skb); */
                        } else if (ip->protocol == IPPROTO_UDP) {
                                skb_checksum_help(skb);
                        } else {
				DBG_8192D("%s-%d TCP CSUM offload Error!!\n", __func__, __LINE__);
                                WARN_ON(1);     /* we need a WARN() */
			    }
		} else { /*  IP fragmentation case */
			DBG_8192D("%s-%d nr_frags != 0, using skb_checksum_help(skb);!!\n", __func__, __LINE__);
			skb_checksum_help(skb);
		}
	}
#endif
}

int rtw_os_xmit_resource_alloc(struct rtw_adapter *padapter, struct xmit_buf *pxmitbuf,u32 alloc_sz)
{
	int i;
	struct dvobj_priv	*pdvobjpriv = adapter_to_dvobj(padapter);
	struct usb_device	*pusbd = pdvobjpriv->pusbdev;

	pxmitbuf->pallocated_buf = kzalloc(alloc_sz, GFP_KERNEL);
	if (pxmitbuf->pallocated_buf == NULL)
		return _FAIL;

	pxmitbuf->pbuf = (u8 *)N_BYTE_ALIGMENT((SIZE_PTR)(pxmitbuf->pallocated_buf), XMITBUF_ALIGN_SZ);
	pxmitbuf->dma_transfer_addr = 0;

	for (i = 0; i < 8; i++) {
		pxmitbuf->pxmit_urb[i] = usb_alloc_urb(0, GFP_KERNEL);
		if (pxmitbuf->pxmit_urb[i] == NULL) {
			DBG_8192D("pxmitbuf->pxmit_urb[i]==NULL");
			return _FAIL;
		}

	}
	return _SUCCESS;
}

void rtw_os_xmit_resource_free(struct rtw_adapter *padapter, struct xmit_buf *pxmitbuf,u32 free_sz)
{
	struct dvobj_priv	*pdvobjpriv = adapter_to_dvobj(padapter);
	struct usb_device	*pusbd = pdvobjpriv->pusbdev;
	int i;

	for (i = 0; i < 8; i++) {
		if (pxmitbuf->pxmit_urb[i])
			usb_free_urb(pxmitbuf->pxmit_urb[i]);
	}
	kfree(pxmitbuf->pallocated_buf);
}

void rtw_os_pkt_complete(struct rtw_adapter *padapter, struct sk_buff *pkt)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,35))
	u16	queue;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;

	queue = skb_get_queue_mapping(pkt);
	if (__netif_subqueue_stopped(padapter->pnetdev, queue) &&
		(pxmitpriv->hwxmits[queue].accnt < NR_XMITFRAME/2))
	{
		netif_wake_subqueue(padapter->pnetdev, queue);
	}
#else
	if (netif_queue_stopped(padapter->pnetdev))
		netif_wake_queue(padapter->pnetdev);
#endif

	dev_kfree_skb_any(pkt);
}

void rtw_os_xmit_complete(struct rtw_adapter *padapter, struct xmit_frame *pxframe)
{
	if (pxframe->pkt)
	{

		rtw_os_pkt_complete(padapter, pxframe->pkt);

	}

	pxframe->pkt = NULL;
}

void rtw_os_xmit_schedule(struct rtw_adapter *padapter)
{
	struct xmit_priv *pxmitpriv;

	if (!padapter)
		return;

	pxmitpriv = &padapter->xmitpriv;

	spin_lock_bh(&pxmitpriv->lock);

	if (rtw_txframes_pending(padapter))
	{
		tasklet_hi_schedule(&pxmitpriv->xmit_tasklet);
	}

	spin_unlock_bh(&pxmitpriv->lock);
}

static int rtw_mlcst2unicst(struct rtw_adapter *padapter, struct sk_buff *skb)
{
	struct	sta_priv *pstapriv = &padapter->stapriv;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct list_head *phead, *plist;
	struct sk_buff *newskb;
	struct sta_info *psta = NULL;
	s32	res;

#ifdef CONFIG_92D_AP_MODE
	spin_lock_bh(&pstapriv->asoc_list_lock);
	phead = &pstapriv->asoc_list;
	plist = phead->next;
#endif
	/* free sta asoc_queue */
	while ((rtw_end_of_queue_search(phead, plist)) == false)
	{
		psta = container_of(plist, struct sta_info, asoc_list);

		plist = plist->next;

		/* avoid   come from STA1 and send back STA1 */
		if (!memcmp(psta->hwaddr, &skb->data[6], 6))
			continue;

		newskb = skb_copy(skb, GFP_ATOMIC);

		if (newskb) {
			memcpy(newskb->data, psta->hwaddr, 6);
			res = rtw_xmit(padapter, &newskb);
			if (res < 0) {
				DBG_8192D("%s()-%d: rtw_xmit() return error!\n", __func__, __LINE__);
				pxmitpriv->tx_drop++;
				dev_kfree_skb_any(newskb);
			} else
				pxmitpriv->tx_pkts++;
		} else {
			DBG_8192D("%s-%d: skb_copy() failed!\n", __func__, __LINE__);
			pxmitpriv->tx_drop++;

#ifdef CONFIG_92D_AP_MODE
			spin_unlock_bh(&pstapriv->asoc_list_lock);
#endif
			return false;	/*  Caller shall tx this multicast frame via normal way. */
		}
	}

#ifdef CONFIG_92D_AP_MODE
	spin_unlock_bh(&pstapriv->asoc_list_lock);
#endif
	dev_kfree_skb_any(skb);
	return true;
}

int rtw_xmit_entry(struct sk_buff *pkt, struct net_device *pnetdev)
{
	struct rtw_adapter *padapter = (struct rtw_adapter *)rtw_netdev_priv(pnetdev);
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	s32 res = 0;
#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,35))
	u16 queue;
#endif

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_, ("+xmit_enry\n"));

	if (rtw_if_up(padapter) == false) {
		RT_TRACE(_module_xmit_osdep_c_, _drv_err_, ("rtw_xmit_entry: rtw_if_up fail\n"));
		#ifdef DBG_TX_DROP_FRAME
		DBG_8192D("DBG_TX_DROP_FRAME %s if_up fail\n", __func__);
		#endif
		goto drop_packet;
	}

#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,35))
	queue = skb_get_queue_mapping(pkt);
	/* No free space for Tx, tx_worker is too slow */
	if (pxmitpriv->hwxmits[queue].accnt > NR_XMITFRAME/2) {
		netif_stop_subqueue(padapter->pnetdev, queue);
		return NETDEV_TX_BUSY;
	}
#endif

	if (!rtw_mc2u_disable
		&& check_fwstate(pmlmepriv, WIFI_AP_STATE) == true
		&& (IP_MCAST_MAC(pkt->data)
			|| ICMPV6_MCAST_MAC(pkt->data))
		&& (padapter->registrypriv.wifi_spec == 0)
		)
	{
		if (pxmitpriv->free_xmitframe_cnt > (NR_XMITFRAME/4)) {
			res = rtw_mlcst2unicst(padapter, pkt);
			if (res == true) {
				goto exit;
			}
		}
	}

	res = rtw_xmit(padapter, &pkt);
	if (res < 0) {
		#ifdef DBG_TX_DROP_FRAME
		DBG_8192D("DBG_TX_DROP_FRAME %s rtw_xmit fail\n", __func__);
		#endif
		goto drop_packet;
	}

	pxmitpriv->tx_pkts++;
	RT_TRACE(_module_xmit_osdep_c_, _drv_info_, ("rtw_xmit_entry: tx_pkts=%d\n", (u32)pxmitpriv->tx_pkts));
	goto exit;

drop_packet:
	pxmitpriv->tx_drop++;
	dev_kfree_skb_any(pkt);
	RT_TRACE(_module_xmit_osdep_c_, _drv_notice_, ("rtw_xmit_entry: drop, tx_drop=%d\n", (u32)pxmitpriv->tx_drop));

exit:

	return 0;
}
