// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#define _XMIT_OSDEP_C_

#include <osdep_service.h>
#include <drv_types.h>

#include <wifi.h>
#include <mlme_osdep.h>
#include <xmit_osdep.h>
#include <osdep_intf.h>

int rtw_os_xmit_resource_alloc(struct adapter *padapter,
			       struct xmit_buf *pxmitbuf, u32 alloc_sz)
{
	int i;

	pxmitbuf->pallocated_buf = kzalloc(alloc_sz, GFP_KERNEL);
	if (!pxmitbuf->pallocated_buf)
		return _FAIL;

	pxmitbuf->pbuf = PTR_ALIGN(pxmitbuf->pallocated_buf, XMITBUF_ALIGN_SZ);
	pxmitbuf->dma_transfer_addr = 0;

	for (i = 0; i < 8; i++) {
		pxmitbuf->pxmit_urb[i] = usb_alloc_urb(0, GFP_KERNEL);
		if (!pxmitbuf->pxmit_urb[i]) {
			DBG_88E("pxmitbuf->pxmit_urb[i]==NULL");
			return _FAIL;
		}
	}
	return _SUCCESS;
}

void rtw_os_xmit_resource_free(struct xmit_buf *pxmitbuf)
{
	int i;

	for (i = 0; i < 8; i++)
		usb_free_urb(pxmitbuf->pxmit_urb[i]);

	kfree(pxmitbuf->pallocated_buf);
}

#define WMM_XMIT_THRESHOLD	(NR_XMITFRAME * 2 / 5)

void rtw_os_pkt_complete(struct adapter *padapter, struct sk_buff *pkt)
{
	u16 queue;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;

	queue = skb_get_queue_mapping(pkt);
	if (padapter->registrypriv.wifi_spec) {
		if (__netif_subqueue_stopped(padapter->pnetdev, queue) &&
		    (pxmitpriv->hwxmits[queue].accnt < WMM_XMIT_THRESHOLD))
			netif_wake_subqueue(padapter->pnetdev, queue);
	} else {
		if (__netif_subqueue_stopped(padapter->pnetdev, queue))
			netif_wake_subqueue(padapter->pnetdev, queue);
	}

	dev_kfree_skb_any(pkt);
}

void rtw_os_xmit_complete(struct adapter *padapter, struct xmit_frame *pxframe)
{
	if (pxframe->pkt)
		rtw_os_pkt_complete(padapter, pxframe->pkt);
	pxframe->pkt = NULL;
}

void rtw_os_xmit_schedule(struct adapter *padapter)
{
	struct xmit_priv *pxmitpriv;

	if (!padapter)
		return;

	pxmitpriv = &padapter->xmitpriv;

	spin_lock_bh(&pxmitpriv->lock);

	if (rtw_txframes_pending(padapter))
		tasklet_hi_schedule(&pxmitpriv->xmit_tasklet);

	spin_unlock_bh(&pxmitpriv->lock);
}

static void rtw_check_xmit_resource(struct adapter *padapter,
				    struct sk_buff *pkt)
{
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	u16 queue;

	queue = skb_get_queue_mapping(pkt);
	if (padapter->registrypriv.wifi_spec) {
		/* No free space for Tx, tx_worker is too slow */
		if (pxmitpriv->hwxmits[queue].accnt > WMM_XMIT_THRESHOLD)
			netif_stop_subqueue(padapter->pnetdev, queue);
	} else {
		if (pxmitpriv->free_xmitframe_cnt <= 4) {
			if (!netif_tx_queue_stopped(netdev_get_tx_queue(padapter->pnetdev, queue)))
				netif_stop_subqueue(padapter->pnetdev, queue);
		}
	}
}

static int rtw_mlcst2unicst(struct adapter *padapter, struct sk_buff *skb)
{
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct list_head *phead, *plist;
	struct sk_buff *newskb;
	struct sta_info *psta = NULL;
	s32 res;

	spin_lock_bh(&pstapriv->asoc_list_lock);
	phead = &pstapriv->asoc_list;
	plist = phead->next;

	/* free sta asoc_queue */
	while (phead != plist) {
		psta = container_of(plist, struct sta_info, asoc_list);

		plist = plist->next;

		/* avoid come from STA1 and send back STA1 */
		if (!memcmp(psta->hwaddr, &skb->data[6], 6))
			continue;

		newskb = skb_copy(skb, GFP_ATOMIC);

		if (newskb) {
			memcpy(newskb->data, psta->hwaddr, 6);
			res = rtw_xmit(padapter, &newskb);
			if (res < 0) {
				DBG_88E("%s()-%d: rtw_xmit() return error!\n",
					__func__, __LINE__);
				pxmitpriv->tx_drop++;
				dev_kfree_skb_any(newskb);
			} else {
				pxmitpriv->tx_pkts++;
			}
		} else {
			DBG_88E("%s-%d: skb_copy() failed!\n",
				__func__, __LINE__);
			pxmitpriv->tx_drop++;

			spin_unlock_bh(&pstapriv->asoc_list_lock);

			/* Caller shall tx this multicast frame
			 * via normal way.
			 */
			return false;
		}
	}

	spin_unlock_bh(&pstapriv->asoc_list_lock);
	dev_kfree_skb_any(skb);
	return true;
}

int rtw_xmit_entry(struct sk_buff *pkt, struct net_device *pnetdev)
{
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(pnetdev);
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	s32 res = 0;

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_, ("+xmit_enry\n"));

	if (!rtw_if_up(padapter)) {
		RT_TRACE(_module_xmit_osdep_c_, _drv_err_,
			 ("%s: rtw_if_up fail\n", __func__));
		goto drop_packet;
	}

	rtw_check_xmit_resource(padapter, pkt);

	if (!rtw_mc2u_disable && check_fwstate(pmlmepriv, WIFI_AP_STATE) &&
	    (IP_MCAST_MAC(pkt->data) || ICMPV6_MCAST_MAC(pkt->data)) &&
	    (padapter->registrypriv.wifi_spec == 0)) {
		if (pxmitpriv->free_xmitframe_cnt > NR_XMITFRAME / 4) {
			res = rtw_mlcst2unicst(padapter, pkt);
			if (res)
				goto exit;
		}
	}

	res = rtw_xmit(padapter, &pkt);
	if (res < 0)
		goto drop_packet;

	pxmitpriv->tx_pkts++;
	RT_TRACE(_module_xmit_osdep_c_, _drv_info_,
		 ("%s: tx_pkts=%d\n", __func__, (u32)pxmitpriv->tx_pkts));
	goto exit;

drop_packet:
	pxmitpriv->tx_drop++;
	dev_kfree_skb_any(pkt);
	RT_TRACE(_module_xmit_osdep_c_, _drv_notice_,
		 ("%s: drop, tx_drop=%d\n", __func__, (u32)pxmitpriv->tx_drop));

exit:
	return 0;
}
