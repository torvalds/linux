// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2007 - 2012 Realtek Corporation. */

#define _XMIT_OSDEP_C_

#include "../include/osdep_service.h"
#include "../include/drv_types.h"
#include "../include/wifi.h"
#include "../include/mlme_osdep.h"
#include "../include/xmit_osdep.h"
#include "../include/osdep_intf.h"
#include "../include/usb_osintf.h"

uint rtw_remainder_len(struct pkt_file *pfile)
{
	return pfile->buf_len - ((size_t)(pfile->cur_addr) -
	       (size_t)(pfile->buf_start));
}

void _rtw_open_pktfile(struct sk_buff *pktptr, struct pkt_file *pfile)
{

	if (!pktptr) {
		pr_err("8188eu: pktptr is NULL\n");
		return;
	}
	if (!pfile) {
		pr_err("8188eu: pfile is NULL\n");
		return;
	}
	pfile->pkt = pktptr;
	pfile->cur_addr = pktptr->data;
	pfile->buf_start = pktptr->data;
	pfile->pkt_len = pktptr->len;
	pfile->buf_len = pktptr->len;

	pfile->cur_buffer = pfile->buf_start;

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

bool rtw_endofpktfile(struct pkt_file *pfile)
{

	if (pfile->pkt_len == 0) {

		return true;
	}

	return false;
}

int rtw_os_xmit_resource_alloc(struct adapter *padapter, struct xmit_buf *pxmitbuf, u32 alloc_sz)
{
	pxmitbuf->pallocated_buf = kzalloc(alloc_sz, GFP_KERNEL);
	if (!pxmitbuf->pallocated_buf)
		return _FAIL;

	pxmitbuf->pbuf = (u8 *)N_BYTE_ALIGMENT((size_t)(pxmitbuf->pallocated_buf), XMITBUF_ALIGN_SZ);
	pxmitbuf->dma_transfer_addr = 0;

	pxmitbuf->pxmit_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!pxmitbuf->pxmit_urb)
		return _FAIL;

	return _SUCCESS;
}

void rtw_os_xmit_resource_free(struct adapter *padapter,
			       struct xmit_buf *pxmitbuf, u32 free_sz)
{
	usb_free_urb(pxmitbuf->pxmit_urb);

	kfree(pxmitbuf->pallocated_buf);
}

#define WMM_XMIT_THRESHOLD	(NR_XMITFRAME * 2 / 5)

void rtw_os_pkt_complete(struct adapter *padapter, struct sk_buff *pkt)
{
	u16	queue;
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

static void rtw_check_xmit_resource(struct adapter *padapter, struct sk_buff *pkt)
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

static int rtw_mlcst2unicst(struct adapter *padapter, struct sk_buff *skb)
{
	struct	sta_priv *pstapriv = &padapter->stapriv;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct list_head *phead, *plist;
	struct sk_buff *newskb;
	struct sta_info *psta = NULL;
	s32	res;

	spin_lock_bh(&pstapriv->asoc_list_lock);
	phead = &pstapriv->asoc_list;
	plist = phead->next;

	/* free sta asoc_queue */
	while (phead != plist) {
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
				pxmitpriv->tx_drop++;
				dev_kfree_skb_any(newskb);
			} else {
				pxmitpriv->tx_pkts++;
			}
		} else {
			pxmitpriv->tx_drop++;

			spin_unlock_bh(&pstapriv->asoc_list_lock);
			return false;	/*  Caller shall tx this multicast frame via normal way. */
		}
	}

	spin_unlock_bh(&pstapriv->asoc_list_lock);
	dev_kfree_skb_any(skb);
	return true;
}

int rtw_xmit_entry(struct sk_buff *pkt, struct  net_device *pnetdev)
{
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(pnetdev);
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	s32 res = 0;

	if (!rtw_if_up(padapter))
		goto drop_packet;

	rtw_check_xmit_resource(padapter, pkt);

	if (!rtw_mc2u_disable && check_fwstate(pmlmepriv, WIFI_AP_STATE) &&
	    (IP_MCAST_MAC(pkt->data) || ICMPV6_MCAST_MAC(pkt->data)) &&
	    (padapter->registrypriv.wifi_spec == 0)) {
		if (pxmitpriv->free_xmitframe_cnt > (NR_XMITFRAME / 4)) {
			res = rtw_mlcst2unicst(padapter, pkt);
			if (res)
				goto exit;
		}
	}

	res = rtw_xmit(padapter, &pkt);
	if (res < 0)
		goto drop_packet;

	pxmitpriv->tx_pkts++;
	goto exit;

drop_packet:
	pxmitpriv->tx_drop++;
	dev_kfree_skb_any(pkt);

exit:

	return 0;
}
