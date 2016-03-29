/******************************************************************************
 * recv_linux.c
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
 * Linux device driver for RTL8192SU
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
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
int r8712_os_recv_resource_alloc(struct _adapter *padapter,
				 union recv_frame *precvframe)
{
	precvframe->u.hdr.pkt_newalloc = NULL;
	precvframe->u.hdr.pkt = NULL;
	return _SUCCESS;
}

/*alloc os related resource in struct recv_buf*/
int r8712_os_recvbuf_resource_alloc(struct _adapter *padapter,
				    struct recv_buf *precvbuf)
{
	int res = _SUCCESS;

	precvbuf->irp_pending = false;
	precvbuf->purb = usb_alloc_urb(0, GFP_KERNEL);
	if (!precvbuf->purb)
		res = _FAIL;
	precvbuf->pskb = NULL;
	precvbuf->reuse = false;
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
int r8712_os_recvbuf_resource_free(struct _adapter *padapter,
			     struct recv_buf *precvbuf)
{
	if (precvbuf->pskb)
		dev_kfree_skb_any(precvbuf->pskb);
	if (precvbuf->purb) {
		usb_kill_urb(precvbuf->purb);
		usb_free_urb(precvbuf->purb);
	}
	return _SUCCESS;
}

void r8712_handle_tkip_mic_err(struct _adapter *padapter, u8 bgroup)
{
	union iwreq_data wrqu;
	struct iw_michaelmicfailure ev;
	struct mlme_priv *pmlmepriv  = &padapter->mlmepriv;

	memset(&ev, 0x00, sizeof(ev));
	if (bgroup)
		ev.flags |= IW_MICFAILURE_GROUP;
	else
		ev.flags |= IW_MICFAILURE_PAIRWISE;
	ev.src_addr.sa_family = ARPHRD_ETHER;
	ether_addr_copy(ev.src_addr.sa_data, &pmlmepriv->assoc_bssid[0]);
	memset(&wrqu, 0x00, sizeof(wrqu));
	wrqu.data.length = sizeof(ev);
	wireless_send_event(padapter->pnetdev, IWEVMICHAELMICFAILURE, &wrqu,
			    (char *)&ev);
}

void r8712_recv_indicatepkt(struct _adapter *padapter,
			    union recv_frame *precv_frame)
{
	struct recv_priv *precvpriv;
	struct  __queue	*pfree_recv_queue;
	_pkt *skb;
	struct rx_pkt_attrib *pattrib = &precv_frame->u.hdr.attrib;

	precvpriv = &(padapter->recvpriv);
	pfree_recv_queue = &(precvpriv->free_recv_queue);
	skb = precv_frame->u.hdr.pkt;
	if (!skb)
		goto _recv_indicatepkt_drop;
	skb->data = precv_frame->u.hdr.rx_data;
	skb->len = precv_frame->u.hdr.len;
	skb_set_tail_pointer(skb, skb->len);
	if ((pattrib->tcpchk_valid == 1) && (pattrib->tcp_chkrpt == 1))
		skb->ip_summed = CHECKSUM_UNNECESSARY;
	else
		skb->ip_summed = CHECKSUM_NONE;
	skb->dev = padapter->pnetdev;
	skb->protocol = eth_type_trans(skb, padapter->pnetdev);
	netif_rx(skb);
	precv_frame->u.hdr.pkt = NULL; /* pointers to NULL before
					* r8712_free_recvframe()
					*/
	r8712_free_recvframe(precv_frame, pfree_recv_queue);
	return;
_recv_indicatepkt_drop:
	 /*enqueue back to free_recv_queue*/
	if (precv_frame)
		r8712_free_recvframe(precv_frame, pfree_recv_queue);
	precvpriv->rx_drop++;
}

static void _r8712_reordering_ctrl_timeout_handler (unsigned long data)
{
	struct recv_reorder_ctrl *preorder_ctrl =
			 (struct recv_reorder_ctrl *)data;

	r8712_reordering_ctrl_timeout_handler(preorder_ctrl);
}

void r8712_init_recv_timer(struct recv_reorder_ctrl *preorder_ctrl)
{
	setup_timer(&preorder_ctrl->reordering_ctrl_timer,
		     _r8712_reordering_ctrl_timeout_handler,
		     (unsigned long)preorder_ctrl);
}
