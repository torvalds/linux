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
 ******************************************************************************/
#define _RTL8188EU_RECV_C_
#include <linux/kmemleak.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <recv_osdep.h>
#include <mlme_osdep.h>

#include <usb_ops_linux.h>
#include <wifi.h>

#include <rtl8188e_hal.h>

int	rtw_hal_init_recv_priv(struct adapter *padapter)
{
	struct recv_priv	*precvpriv = &padapter->recvpriv;
	int	i, res = _SUCCESS;
	struct recv_buf *precvbuf;

	tasklet_init(&precvpriv->recv_tasklet,
		     (void(*)(unsigned long))rtl8188eu_recv_tasklet,
		     (unsigned long)padapter);

	/* init recv_buf */
	_rtw_init_queue(&precvpriv->free_recv_buf_queue);

	precvpriv->precv_buf =
		kcalloc(NR_RECVBUFF, sizeof(struct recv_buf), GFP_KERNEL);
	if (!precvpriv->precv_buf) {
		res = _FAIL;
		RT_TRACE(_module_rtl871x_recv_c_, _drv_err_,
				("alloc recv_buf fail!\n"));
		goto exit;
	}
	precvbuf = precvpriv->precv_buf;

	for (i = 0; i < NR_RECVBUFF; i++) {
		res = rtw_os_recvbuf_resource_alloc(padapter, precvbuf);
		if (res == _FAIL)
			break;
		precvbuf->adapter = padapter;
		precvbuf++;
	}
	skb_queue_head_init(&precvpriv->rx_skb_queue);
	{
		int i;
		struct sk_buff *pskb = NULL;

		skb_queue_head_init(&precvpriv->free_recv_skb_queue);

		for (i = 0; i < NR_PREALLOC_RECV_SKB; i++) {
			pskb = __netdev_alloc_skb(padapter->pnetdev,
					MAX_RECVBUF_SZ, GFP_KERNEL);
			if (pskb) {
				kmemleak_not_leak(pskb);
				skb_queue_tail(&precvpriv->free_recv_skb_queue,
						pskb);
			}
			pskb = NULL;
		}
	}
exit:
	return res;
}

void rtw_hal_free_recv_priv(struct adapter *padapter)
{
	int	i;
	struct recv_buf	*precvbuf;
	struct recv_priv	*precvpriv = &padapter->recvpriv;

	precvbuf = precvpriv->precv_buf;

	for (i = 0; i < NR_RECVBUFF; i++) {
		usb_free_urb(precvbuf->purb);
		precvbuf++;
	}

	kfree(precvpriv->precv_buf);

	if (skb_queue_len(&precvpriv->rx_skb_queue))
		DBG_88E(KERN_WARNING "rx_skb_queue not empty\n");
	skb_queue_purge(&precvpriv->rx_skb_queue);


	if (skb_queue_len(&precvpriv->free_recv_skb_queue))
		DBG_88E(KERN_WARNING "free_recv_skb_queue not empty, %d\n",
				skb_queue_len(&precvpriv->free_recv_skb_queue));

	skb_queue_purge(&precvpriv->free_recv_skb_queue);
}
