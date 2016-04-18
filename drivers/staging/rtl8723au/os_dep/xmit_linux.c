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
 ******************************************************************************/
#define _XMIT_OSDEP_C_

#include <osdep_service.h>
#include <drv_types.h>

#include <linux/if_ether.h>
#include <linux/ip.h>
#include <wifi.h>
#include <mlme_osdep.h>
#include <xmit_osdep.h>
#include <osdep_intf.h>

int rtw_os_xmit_resource_alloc23a(struct rtw_adapter *padapter,
			       struct xmit_buf *pxmitbuf, u32 alloc_sz)
{
	int i;

	pxmitbuf->pallocated_buf = kzalloc(alloc_sz, GFP_KERNEL);
	if (pxmitbuf->pallocated_buf == NULL)
		return _FAIL;

	pxmitbuf->pbuf = PTR_ALIGN(pxmitbuf->pallocated_buf, XMITBUF_ALIGN_SZ);

	for (i = 0; i < 8; i++) {
		pxmitbuf->pxmit_urb[i] = usb_alloc_urb(0, GFP_KERNEL);
		if (!pxmitbuf->pxmit_urb[i]) {
			DBG_8723A("pxmitbuf->pxmit_urb[i]==NULL");
			return _FAIL;
		}
	}
	return _SUCCESS;
}

void rtw_os_xmit_resource_free23a(struct rtw_adapter *padapter,
			       struct xmit_buf *pxmitbuf)
{
	int i;

	for (i = 0; i < 8; i++)
		usb_free_urb(pxmitbuf->pxmit_urb[i]);
	kfree(pxmitbuf->pallocated_buf);
}

#define WMM_XMIT_THRESHOLD	(NR_XMITFRAME*2/5)

void rtw_os_pkt_complete23a(struct rtw_adapter *padapter, struct sk_buff *pkt)
{
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	u16	queue;

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

void rtw_os_xmit_complete23a(struct rtw_adapter *padapter,
			  struct xmit_frame *pxframe)
{
	if (pxframe->pkt)
		rtw_os_pkt_complete23a(padapter, pxframe->pkt);

	pxframe->pkt = NULL;
}

void rtw_os_xmit_schedule23a(struct rtw_adapter *padapter)
{
	struct xmit_priv *pxmitpriv;

	if (!padapter)
		return;
	pxmitpriv = &padapter->xmitpriv;

	spin_lock_bh(&pxmitpriv->lock);

	if (rtw_txframes_pending23a(padapter))
		tasklet_hi_schedule(&pxmitpriv->xmit_tasklet);
	spin_unlock_bh(&pxmitpriv->lock);
}

static void rtw_check_xmit_resource(struct rtw_adapter *padapter,
				    struct sk_buff *pkt)
{
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	u16	queue;

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

int rtw_xmit23a_entry23a(struct sk_buff *skb, struct net_device *pnetdev)
{
	struct rtw_adapter *padapter = netdev_priv(pnetdev);
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	int res = 0;

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_, "+xmit_enry\n");

	if (!rtw_if_up23a(padapter)) {
		RT_TRACE(_module_xmit_osdep_c_, _drv_err_,
			 "rtw_xmit23a_entry23a: rtw_if_up23a fail\n");
		goto drop_packet;
	}

	rtw_check_xmit_resource(padapter, skb);

	res = rtw_xmit23a(padapter, skb);
	if (res < 0)
		goto drop_packet;

	pxmitpriv->tx_pkts++;
	RT_TRACE(_module_xmit_osdep_c_, _drv_info_,
		 "rtw_xmit23a_entry23a: tx_pkts=%d\n",
		 (u32)pxmitpriv->tx_pkts);
	goto exit;

drop_packet:
	pxmitpriv->tx_drop++;
	dev_kfree_skb_any(skb);
	RT_TRACE(_module_xmit_osdep_c_, _drv_notice_,
		 "rtw_xmit23a_entry23a: drop, tx_drop=%d\n",
		 (u32)pxmitpriv->tx_drop);
exit:
	return 0;
}
