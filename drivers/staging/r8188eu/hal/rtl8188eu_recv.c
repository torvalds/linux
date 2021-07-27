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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#define _RTL8188EU_RECV_C_
#include <osdep_service.h>
#include <drv_types.h>
#include <recv_osdep.h>
#include <mlme_osdep.h>
#include <ip.h>
#include <if_ether.h>
#include <ethernet.h>

#include <usb_ops.h>
#include <wifi.h>

#include <rtl8188e_hal.h>

void rtl8188eu_init_recvbuf(struct adapter *padapter, struct recv_buf *precvbuf)
{
	precvbuf->transfer_len = 0;

	precvbuf->len = 0;

	precvbuf->ref_cnt = 0;

	if (precvbuf->pbuf) {
		precvbuf->pdata = precvbuf->pbuf;
		precvbuf->phead = precvbuf->pbuf;
		precvbuf->ptail = precvbuf->pbuf;
		precvbuf->pend = precvbuf->pdata + MAX_RECVBUF_SZ;
	}
}

int	rtl8188eu_init_recv_priv(struct adapter *padapter)
{
	struct recv_priv	*precvpriv = &padapter->recvpriv;
	int	i, res = _SUCCESS;
	struct recv_buf *precvbuf;

	tasklet_init(&precvpriv->recv_tasklet,
		     (void(*)(unsigned long))rtl8188eu_recv_tasklet,
		     (unsigned long)padapter);

	/* init recv_buf */
	_rtw_init_queue(&precvpriv->free_recv_buf_queue);

	precvpriv->pallocated_recv_buf = rtw_zmalloc(NR_RECVBUFF * sizeof(struct recv_buf) + 4);
	if (precvpriv->pallocated_recv_buf == NULL) {
		res = _FAIL;
		RT_TRACE(_module_rtl871x_recv_c_, _drv_err_, ("alloc recv_buf fail!\n"));
		goto exit;
	}
	memset(precvpriv->pallocated_recv_buf, 0, NR_RECVBUFF * sizeof(struct recv_buf) + 4);

	precvpriv->precv_buf = (u8 *)N_BYTE_ALIGMENT((size_t)(precvpriv->pallocated_recv_buf), 4);

	precvbuf = (struct recv_buf *)precvpriv->precv_buf;

	for (i = 0; i < NR_RECVBUFF; i++) {
		INIT_LIST_HEAD(&precvbuf->list);
		spin_lock_init(&precvbuf->recvbuf_lock);
		precvbuf->alloc_sz = MAX_RECVBUF_SZ;
		res = rtw_os_recvbuf_resource_alloc(padapter, precvbuf);
		if (res == _FAIL)
			break;
		precvbuf->ref_cnt = 0;
		precvbuf->adapter = padapter;
		precvbuf++;
	}
	precvpriv->free_recv_buf_queue_cnt = NR_RECVBUFF;
	skb_queue_head_init(&precvpriv->rx_skb_queue);
	{
		int i;
		size_t tmpaddr = 0;
		size_t alignment = 0;
		struct sk_buff *pskb = NULL;

		skb_queue_head_init(&precvpriv->free_recv_skb_queue);

		for (i = 0; i < NR_PREALLOC_RECV_SKB; i++) {
			pskb = __netdev_alloc_skb(padapter->pnetdev, MAX_RECVBUF_SZ + RECVBUFF_ALIGN_SZ, GFP_KERNEL);
			if (pskb) {
				pskb->dev = padapter->pnetdev;
				tmpaddr = (size_t)pskb->data;
				alignment = tmpaddr & (RECVBUFF_ALIGN_SZ-1);
				skb_reserve(pskb, (RECVBUFF_ALIGN_SZ - alignment));

				skb_queue_tail(&precvpriv->free_recv_skb_queue, pskb);
			}
			pskb = NULL;
		}
	}
exit:
	return res;
}

void rtl8188eu_free_recv_priv(struct adapter *padapter)
{
	int	i;
	struct recv_buf	*precvbuf;
	struct recv_priv	*precvpriv = &padapter->recvpriv;

	precvbuf = (struct recv_buf *)precvpriv->precv_buf;

	for (i = 0; i < NR_RECVBUFF; i++) {
		rtw_os_recvbuf_resource_free(padapter, precvbuf);
		precvbuf++;
	}

	kfree(precvpriv->pallocated_recv_buf);

	if (skb_queue_len(&precvpriv->rx_skb_queue))
		DBG_88E(KERN_WARNING "rx_skb_queue not empty\n");
	skb_queue_purge(&precvpriv->rx_skb_queue);

	if (skb_queue_len(&precvpriv->free_recv_skb_queue))
		DBG_88E(KERN_WARNING "free_recv_skb_queue not empty, %d\n", skb_queue_len(&precvpriv->free_recv_skb_queue));

	skb_queue_purge(&precvpriv->free_recv_skb_queue);
}
