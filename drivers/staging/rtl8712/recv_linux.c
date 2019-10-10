// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 * recv_linux.c
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
 * Linux device driver for RTL8192SU
 *
 * Modifications for inclusion into the Linux staging tree are
 * Copyright(c) 2010 Larry Finger. All rights reserved.
 *
 * Contact information:
 * WLAN FAE <wlanfae@realtek.com>.
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 ******************************************************************************/

#define _RECV_OSDEP_C_

#include <linux/usb.h>

#include "osdep_service.h"
#include "drv_types.h"
#include "wifi.h"
#include "recv_osdep.h"
#include "osdep_intf.h"
#include "ethernet.h"
#include <linux/if_arp.h>
#include "usb_ops.h"

/*init os related resource in struct recv_priv*/
/*alloc os related resource in union recv_frame*/
void r8712_os_recv_resource_alloc(struct _adapter *padapter,
				  union recv_frame *precvframe)
{
	precvframe->u.hdr.pkt_newalloc = NULL;
	precvframe->u.hdr.pkt = NULL;
}

/*alloc os related resource in struct recv_buf*/
int r8712_os_recvbuf_resource_alloc(struct _adapter *padapter,
				    struct recv_buf *precvbuf)
{
	int res = 0;

	precvbuf->irp_pending = false;
	precvbuf->purb = usb_alloc_urb(0, GFP_KERNEL);
	if (!precvbuf->purb)
		res = -ENOMEM;
	precvbuf->pskb = NULL;
	precvbuf->pallocated_buf = NULL;
	precvbuf->pbuf = NULL;
	precvbuf->pdata = NULL;
	precvbuf->phead = NULL;
	precvbuf->ptail = NULL;
	precvbuf->pend = NULL;
	precvbuf->transfer_len = 0;
	precvbuf->len = 0;
	return res;
}

/*free os related resource in struct recv_buf*/
void r8712_os_recvbuf_resource_free(struct _adapter *padapter,
				    struct recv_buf *precvbuf)
{
	if (precvbuf->pskb)
		dev_kfree_skb_any(precvbuf->pskb);
	if (precvbuf->purb) {
		usb_kill_urb(precvbuf->purb);
		usb_free_urb(precvbuf->purb);
	}
}

void r8712_handle_tkip_mic_err(struct _adapter *adapter, u8 bgroup)
{
	union iwreq_data wrqu;
	struct iw_michaelmicfailure ev;
	struct mlme_priv *mlmepriv  = &adapter->mlmepriv;

	memset(&ev, 0x00, sizeof(ev));
	if (bgroup)
		ev.flags |= IW_MICFAILURE_GROUP;
	else
		ev.flags |= IW_MICFAILURE_PAIRWISE;
	ev.src_addr.sa_family = ARPHRD_ETHER;
	ether_addr_copy(ev.src_addr.sa_data, &mlmepriv->assoc_bssid[0]);
	memset(&wrqu, 0x00, sizeof(wrqu));
	wrqu.data.length = sizeof(ev);
	wireless_send_event(adapter->pnetdev, IWEVMICHAELMICFAILURE, &wrqu,
			    (char *)&ev);
}

void r8712_recv_indicatepkt(struct _adapter *adapter,
			    union recv_frame *recvframe)
{
	struct recv_priv *recvpriv;
	struct  __queue	*free_recv_queue;
	_pkt *skb;
	struct rx_pkt_attrib *attrib = &recvframe->u.hdr.attrib;

	recvpriv = &adapter->recvpriv;
	free_recv_queue = &recvpriv->free_recv_queue;
	skb = recvframe->u.hdr.pkt;
	if (!skb)
		goto _recv_indicatepkt_drop;
	skb->data = recvframe->u.hdr.rx_data;
	skb->len = recvframe->u.hdr.len;
	skb_set_tail_pointer(skb, skb->len);
	if ((attrib->tcpchk_valid == 1) && (attrib->tcp_chkrpt == 1))
		skb->ip_summed = CHECKSUM_UNNECESSARY;
	else
		skb->ip_summed = CHECKSUM_NONE;
	skb->dev = adapter->pnetdev;
	skb->protocol = eth_type_trans(skb, adapter->pnetdev);
	netif_rx(skb);
	recvframe->u.hdr.pkt = NULL; /* pointers to NULL before
				      * r8712_free_recvframe()
				      */
	r8712_free_recvframe(recvframe, free_recv_queue);
	return;
_recv_indicatepkt_drop:
	 /*enqueue back to free_recv_queue*/
	if (recvframe)
		r8712_free_recvframe(recvframe, free_recv_queue);
	recvpriv->rx_drop++;
}

static void _r8712_reordering_ctrl_timeout_handler (struct timer_list *t)
{
	struct recv_reorder_ctrl *reorder_ctrl =
			 from_timer(reorder_ctrl, t, reordering_ctrl_timer);

	r8712_reordering_ctrl_timeout_handler(reorder_ctrl);
}

void r8712_init_recv_timer(struct recv_reorder_ctrl *preorder_ctrl)
{
	timer_setup(&preorder_ctrl->reordering_ctrl_timer,
		    _r8712_reordering_ctrl_timeout_handler, 0);
}
