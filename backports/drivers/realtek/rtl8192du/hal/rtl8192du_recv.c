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
#define _RTL8192DU_RECV_C_
#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <recv_osdep.h>
#include <mlme_osdep.h>
#include <linux/ip.h>
#include <linux/if_ether.h>
#include <ethernet.h>
#include <usb_ops.h>
#include <wifi.h>
#include <rtl8192d_hal.h>

void rtl8192du_init_recvbuf(struct rtw_adapter *padapter, struct recv_buf *precvbuf)
{

	precvbuf->transfer_len = 0;

	precvbuf->len = 0;

	precvbuf->ref_cnt = 0;

	if (precvbuf->pbuf)
	{
		precvbuf->pdata = precvbuf->phead = precvbuf->ptail = precvbuf->pbuf;
		precvbuf->pend = precvbuf->pdata + MAX_RECVBUF_SZ;
	}
}

int	rtl8192du_init_recv_priv(struct rtw_adapter *padapter)
{
	struct recv_priv	*precvpriv = &padapter->recvpriv;
	int	i, res = _SUCCESS;
	struct recv_buf *precvbuf;

	tasklet_init(&precvpriv->recv_tasklet,
	     (void(*)(unsigned long))rtl8192du_recv_tasklet,
	     (unsigned long)padapter);

	/* init recv_buf */
	_rtw_init_queue(&precvpriv->free_recv_buf_queue);

	precvpriv->pallocated_recv_buf = kzalloc(NR_RECVBUFF *sizeof(struct recv_buf) + 4, GFP_KERNEL);
	if (precvpriv->pallocated_recv_buf==NULL) {
		res= _FAIL;
		RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("alloc recv_buf fail!\n"));
		goto exit;
	}

	precvpriv->precv_buf = (u8 *)N_BYTE_ALIGMENT((SIZE_PTR)(precvpriv->pallocated_recv_buf), 4);

	precvbuf = (struct recv_buf*)precvpriv->precv_buf;

	for (i=0; i < NR_RECVBUFF ; i++)
	{
		INIT_LIST_HEAD(&precvbuf->list);

		_rtw_spinlock_init(&precvbuf->recvbuf_lock);

		precvbuf->alloc_sz = MAX_RECVBUF_SZ;

		res = rtw_os_recvbuf_resource_alloc(padapter, precvbuf);
		if (res==_FAIL)
			break;

		precvbuf->ref_cnt = 0;
		precvbuf->adapter =padapter;
		precvbuf++;

	}

	precvpriv->free_recv_buf_queue_cnt = NR_RECVBUFF;
	skb_queue_head_init(&precvpriv->rx_skb_queue);

#ifdef CONFIG_PREALLOC_RECV_SKB
	{
		int i;
		SIZE_PTR tmpaddr=0;
		SIZE_PTR alignment=0;
		struct sk_buff *pskb=NULL;

		skb_queue_head_init(&precvpriv->free_recv_skb_queue);

		for (i=0; i<NR_PREALLOC_RECV_SKB; i++)
		{
	#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18)) /*  http://www.mail-archive.com/netdev@vger.kernel.org/msg17214.html */
			pskb = dev_alloc_skb(MAX_RECVBUF_SZ + RECVBUFF_ALIGN_SZ);
	#else
			pskb = netdev_alloc_skb(padapter->pnetdev, MAX_RECVBUF_SZ + RECVBUFF_ALIGN_SZ);
	#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18)) */

			if (pskb)
			{
				pskb->dev = padapter->pnetdev;

				tmpaddr = (SIZE_PTR)pskb->data;
				alignment = tmpaddr & (RECVBUFF_ALIGN_SZ-1);
				skb_reserve(pskb, (RECVBUFF_ALIGN_SZ - alignment));

				skb_queue_tail(&precvpriv->free_recv_skb_queue, pskb);
			}

			pskb=NULL;

		}
	}
#endif

exit:

	return res;
}

void rtl8192du_free_recv_priv (struct rtw_adapter *padapter)
{
	int i;
	struct recv_buf *precvbuf;
	struct recv_priv	*precvpriv = &padapter->recvpriv;

	precvbuf = (struct recv_buf *)precvpriv->precv_buf;

	for (i = 0; i < NR_RECVBUFF; i++) {
		rtw_os_recvbuf_resource_free(padapter, precvbuf);
		precvbuf++;
	}

	kfree(precvpriv->pallocated_recv_buf);

	if (skb_queue_len(&precvpriv->rx_skb_queue))
		DBG_8192D(KERN_WARNING "rx_skb_queue not empty\n");

	skb_queue_purge(&precvpriv->rx_skb_queue);

#ifdef CONFIG_PREALLOC_RECV_SKB

	if (skb_queue_len(&precvpriv->free_recv_skb_queue)) {
		DBG_8192D(KERN_WARNING "free_recv_skb_queue not empty, %d\n", skb_queue_len(&precvpriv->free_recv_skb_queue));
	}

	skb_queue_purge(&precvpriv->free_recv_skb_queue);
#endif
}
