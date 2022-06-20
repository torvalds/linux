// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 * usb_ops_linux.c
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
 * Linux device driver for RTL8192SU
 *
 * Modifications for inclusion into the Linux staging tree are
 * Copyright(c) 2010 Larry Finger. All rights reserved.
 *
 * Contact information:
 * WLAN FAE <wlanfae@realtek.com>
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 ******************************************************************************/

#define _HCI_OPS_OS_C_

#include <linux/usb.h>

#include "osdep_service.h"
#include "drv_types.h"
#include "osdep_intf.h"
#include "usb_ops.h"

#define	RTL871X_VENQT_READ	0xc0
#define	RTL871X_VENQT_WRITE	0x40

struct zero_bulkout_context {
	void *pbuf;
	void *purb;
	void *pirp;
	void *padapter;
};

uint r8712_usb_init_intf_priv(struct intf_priv *pintfpriv)
{
	pintfpriv->piorw_urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (!pintfpriv->piorw_urb)
		return _FAIL;
	init_completion(&pintfpriv->io_retevt_comp);
	return _SUCCESS;
}

void r8712_usb_unload_intf_priv(struct intf_priv *pintfpriv)
{
	if (pintfpriv->piorw_urb) {
		usb_kill_urb(pintfpriv->piorw_urb);
		usb_free_urb(pintfpriv->piorw_urb);
	}
}

static unsigned int ffaddr2pipehdl(struct dvobj_priv *pdvobj, u32 addr)
{
	unsigned int pipe = 0;
	struct usb_device *pusbd = pdvobj->pusbdev;

	if (pdvobj->nr_endpoint == 11) {
		switch (addr) {
		case RTL8712_DMA_BKQ:
			pipe = usb_sndbulkpipe(pusbd, 0x07);
			break;
		case RTL8712_DMA_BEQ:
			pipe = usb_sndbulkpipe(pusbd, 0x06);
			break;
		case RTL8712_DMA_VIQ:
			pipe = usb_sndbulkpipe(pusbd, 0x05);
			break;
		case RTL8712_DMA_VOQ:
			pipe = usb_sndbulkpipe(pusbd, 0x04);
			break;
		case RTL8712_DMA_BCNQ:
			pipe = usb_sndbulkpipe(pusbd, 0x0a);
			break;
		case RTL8712_DMA_BMCQ:	/* HI Queue */
			pipe = usb_sndbulkpipe(pusbd, 0x0b);
			break;
		case RTL8712_DMA_MGTQ:
			pipe = usb_sndbulkpipe(pusbd, 0x0c);
			break;
		case RTL8712_DMA_RX0FF:
			pipe = usb_rcvbulkpipe(pusbd, 0x03); /* in */
			break;
		case RTL8712_DMA_C2HCMD:
			pipe = usb_rcvbulkpipe(pusbd, 0x09); /* in */
			break;
		case RTL8712_DMA_H2CCMD:
			pipe = usb_sndbulkpipe(pusbd, 0x0d);
			break;
		}
	} else if (pdvobj->nr_endpoint == 6) {
		switch (addr) {
		case RTL8712_DMA_BKQ:
			pipe = usb_sndbulkpipe(pusbd, 0x07);
			break;
		case RTL8712_DMA_BEQ:
			pipe = usb_sndbulkpipe(pusbd, 0x06);
			break;
		case RTL8712_DMA_VIQ:
			pipe = usb_sndbulkpipe(pusbd, 0x05);
			break;
		case RTL8712_DMA_VOQ:
			pipe = usb_sndbulkpipe(pusbd, 0x04);
			break;
		case RTL8712_DMA_RX0FF:
		case RTL8712_DMA_C2HCMD:
			pipe = usb_rcvbulkpipe(pusbd, 0x03); /* in */
			break;
		case RTL8712_DMA_H2CCMD:
		case RTL8712_DMA_BCNQ:
		case RTL8712_DMA_BMCQ:
		case RTL8712_DMA_MGTQ:
			pipe = usb_sndbulkpipe(pusbd, 0x0d);
			break;
		}
	} else if (pdvobj->nr_endpoint == 4) {
		switch (addr) {
		case RTL8712_DMA_BEQ:
			pipe = usb_sndbulkpipe(pusbd, 0x06);
			break;
		case RTL8712_DMA_VOQ:
			pipe = usb_sndbulkpipe(pusbd, 0x04);
			break;
		case RTL8712_DMA_RX0FF:
		case RTL8712_DMA_C2HCMD:
			pipe = usb_rcvbulkpipe(pusbd, 0x03); /* in */
			break;
		case RTL8712_DMA_H2CCMD:
		case RTL8712_DMA_BCNQ:
		case RTL8712_DMA_BMCQ:
		case RTL8712_DMA_MGTQ:
			pipe = usb_sndbulkpipe(pusbd, 0x0d);
			break;
		}
	} else {
		pipe = 0;
	}
	return pipe;
}

static void usb_write_mem_complete(struct urb *purb)
{
	struct io_queue *pio_q = (struct io_queue *)purb->context;
	struct intf_hdl *pintf = &(pio_q->intf);
	struct intf_priv *pintfpriv = pintf->pintfpriv;
	struct _adapter *padapter = (struct _adapter *)pintf->adapter;

	if (purb->status != 0) {
		if (purb->status == (-ESHUTDOWN))
			padapter->driver_stopped = true;
		else
			padapter->surprise_removed = true;
	}
	complete(&pintfpriv->io_retevt_comp);
}

void r8712_usb_write_mem(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *wmem)
{
	unsigned int pipe;
	struct _adapter *padapter = (struct _adapter *)pintfhdl->adapter;
	struct intf_priv *pintfpriv = pintfhdl->pintfpriv;
	struct io_queue *pio_queue = padapter->pio_queue;
	struct dvobj_priv *pdvobj = (struct dvobj_priv *)pintfpriv->intf_dev;
	struct usb_device *pusbd = pdvobj->pusbdev;
	struct urb *piorw_urb = pintfpriv->piorw_urb;

	if ((padapter->driver_stopped) || (padapter->surprise_removed) ||
	    (padapter->pwrctrlpriv.pnp_bstop_trx))
		return;
	/* translate DMA FIFO addr to pipehandle */
	pipe = ffaddr2pipehdl(pdvobj, addr);
	if (pipe == 0)
		return;
	usb_fill_bulk_urb(piorw_urb, pusbd, pipe,
			  wmem, cnt, usb_write_mem_complete,
			  pio_queue);
	usb_submit_urb(piorw_urb, GFP_ATOMIC);
	wait_for_completion_interruptible(&pintfpriv->io_retevt_comp);
}

static void r8712_usb_read_port_complete(struct urb *purb)
{
	uint isevt;
	__le32 *pbuf;
	struct recv_buf	*precvbuf = (struct recv_buf *)purb->context;
	struct _adapter *padapter = (struct _adapter *)precvbuf->adapter;
	struct recv_priv *precvpriv = &padapter->recvpriv;

	if (padapter->surprise_removed || padapter->driver_stopped)
		return;
	if (purb->status == 0) { /* SUCCESS */
		if ((purb->actual_length > (MAX_RECVBUF_SZ)) ||
		    (purb->actual_length < RXDESC_SIZE)) {
			r8712_read_port(padapter, precvpriv->ff_hwaddr, 0,
				  (unsigned char *)precvbuf);
		} else {
			_pkt *pskb = precvbuf->pskb;

			precvbuf->transfer_len = purb->actual_length;
			pbuf = (__le32 *)precvbuf->pbuf;
			isevt = le32_to_cpu(*(pbuf + 1)) & 0x1ff;
			if ((isevt & 0x1ff) == 0x1ff) {
				r8712_rxcmd_event_hdl(padapter, pbuf);
				skb_queue_tail(&precvpriv->rx_skb_queue, pskb);
				r8712_read_port(padapter, precvpriv->ff_hwaddr,
						0, (unsigned char *)precvbuf);
			} else {
				skb_put(pskb, purb->actual_length);
				skb_queue_tail(&precvpriv->rx_skb_queue, pskb);
				tasklet_hi_schedule(&precvpriv->recv_tasklet);
				r8712_read_port(padapter, precvpriv->ff_hwaddr,
						0, (unsigned char *)precvbuf);
			}
		}
	} else {
		switch (purb->status) {
		case -EINVAL:
		case -EPIPE:
		case -ENODEV:
		case -ESHUTDOWN:
			padapter->driver_stopped = true;
			break;
		case -ENOENT:
			if (!padapter->suspended) {
				padapter->driver_stopped = true;
				break;
			}
			fallthrough;
		case -EPROTO:
			r8712_read_port(padapter, precvpriv->ff_hwaddr, 0,
				  (unsigned char *)precvbuf);
			break;
		case -EINPROGRESS:
			netdev_err(padapter->pnetdev, "ERROR: URB IS IN PROGRESS!\n");
			break;
		default:
			break;
		}
	}
}

u32 r8712_usb_read_port(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *rmem)
{
	unsigned int pipe;
	int err;
	u32 tmpaddr = 0;
	int alignment = 0;
	u32 ret = _SUCCESS;
	struct urb *purb = NULL;
	struct recv_buf	*precvbuf = (struct recv_buf *)rmem;
	struct intf_priv *pintfpriv = pintfhdl->pintfpriv;
	struct dvobj_priv *pdvobj = (struct dvobj_priv *)pintfpriv->intf_dev;
	struct _adapter *adapter = pdvobj->padapter;
	struct recv_priv *precvpriv = &adapter->recvpriv;
	struct usb_device *pusbd = pdvobj->pusbdev;

	if (adapter->driver_stopped || adapter->surprise_removed ||
	    adapter->pwrctrlpriv.pnp_bstop_trx || !precvbuf)
		return _FAIL;
	r8712_init_recvbuf(adapter, precvbuf);
	/* Try to use skb from the free queue */
	precvbuf->pskb = skb_dequeue(&precvpriv->free_recv_skb_queue);

	if (!precvbuf->pskb) {
		precvbuf->pskb = netdev_alloc_skb(adapter->pnetdev,
				 MAX_RECVBUF_SZ + RECVBUFF_ALIGN_SZ);
		if (!precvbuf->pskb)
			return _FAIL;
		tmpaddr = (addr_t)precvbuf->pskb->data;
		alignment = tmpaddr & (RECVBUFF_ALIGN_SZ - 1);
		skb_reserve(precvbuf->pskb,
			    (RECVBUFF_ALIGN_SZ - alignment));
		precvbuf->phead = precvbuf->pskb->head;
		precvbuf->pdata = precvbuf->pskb->data;
		precvbuf->ptail = skb_tail_pointer(precvbuf->pskb);
		precvbuf->pend = skb_end_pointer(precvbuf->pskb);
		precvbuf->pbuf = precvbuf->pskb->data;
	} else { /* skb is reused */
		precvbuf->phead = precvbuf->pskb->head;
		precvbuf->pdata = precvbuf->pskb->data;
		precvbuf->ptail = skb_tail_pointer(precvbuf->pskb);
		precvbuf->pend = skb_end_pointer(precvbuf->pskb);
		precvbuf->pbuf = precvbuf->pskb->data;
	}
	purb = precvbuf->purb;
	/* translate DMA FIFO addr to pipehandle */
	pipe = ffaddr2pipehdl(pdvobj, addr);
	usb_fill_bulk_urb(purb, pusbd, pipe,
			  precvbuf->pbuf, MAX_RECVBUF_SZ,
			  r8712_usb_read_port_complete,
			  precvbuf);
	err = usb_submit_urb(purb, GFP_ATOMIC);
	if ((err) && (err != (-EPERM)))
		ret = _FAIL;
	return ret;
}

void r8712_usb_read_port_cancel(struct _adapter *padapter)
{
	int i;
	struct recv_buf *precvbuf;

	precvbuf = (struct recv_buf *)padapter->recvpriv.precv_buf;
	for (i = 0; i < NR_RECVBUFF; i++) {
		if (precvbuf->purb)
			usb_kill_urb(precvbuf->purb);
		precvbuf++;
	}
}

void r8712_xmit_bh(struct tasklet_struct *t)
{
	int ret = false;
	struct _adapter *padapter = from_tasklet(padapter, t,
						 xmitpriv.xmit_tasklet);
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;

	if (padapter->driver_stopped ||
	    padapter->surprise_removed) {
		netdev_err(padapter->pnetdev, "xmit_bh => driver_stopped or surprise_removed\n");
		return;
	}
	ret = r8712_xmitframe_complete(padapter, pxmitpriv, NULL);
	if (!ret)
		return;
	tasklet_hi_schedule(&pxmitpriv->xmit_tasklet);
}

static void usb_write_port_complete(struct urb *purb)
{
	int i;
	struct xmit_frame *pxmitframe = (struct xmit_frame *)purb->context;
	struct xmit_buf *pxmitbuf = pxmitframe->pxmitbuf;
	struct _adapter *padapter = pxmitframe->padapter;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct pkt_attrib *pattrib = &pxmitframe->attrib;

	switch (pattrib->priority) {
	case 1:
	case 2:
		pxmitpriv->bkq_cnt--;
		break;
	case 4:
	case 5:
		pxmitpriv->viq_cnt--;
		break;
	case 6:
	case 7:
		pxmitpriv->voq_cnt--;
		break;
	case 0:
	case 3:
	default:
		pxmitpriv->beq_cnt--;
		break;
	}
	pxmitpriv->txirp_cnt--;
	for (i = 0; i < 8; i++) {
		if (purb == pxmitframe->pxmit_urb[i]) {
			pxmitframe->bpending[i] = false;
			break;
		}
	}
	if (padapter->surprise_removed)
		return;
	switch (purb->status) {
	case 0:
		break;
	default:
		netdev_warn(padapter->pnetdev,
				"r8712u: pipe error: (%d)\n", purb->status);
		break;
	}
	/* not to consider tx fragment */
	r8712_free_xmitframe_ex(pxmitpriv, pxmitframe);
	r8712_free_xmitbuf(pxmitpriv, pxmitbuf);
	tasklet_hi_schedule(&pxmitpriv->xmit_tasklet);
}

u32 r8712_usb_write_port(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *wmem)
{
	unsigned long irqL;
	int i, status;
	unsigned int pipe;
	u32 ret, bwritezero;
	struct urb *purb = NULL;
	struct _adapter *padapter = (struct _adapter *)pintfhdl->adapter;
	struct dvobj_priv *pdvobj = &padapter->dvobjpriv;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct xmit_frame *pxmitframe = (struct xmit_frame *)wmem;
	struct usb_device *pusbd = pdvobj->pusbdev;
	struct pkt_attrib *pattrib = &pxmitframe->attrib;

	if ((padapter->driver_stopped) || (padapter->surprise_removed) ||
	    (padapter->pwrctrlpriv.pnp_bstop_trx))
		return _FAIL;
	for (i = 0; i < 8; i++) {
		if (!pxmitframe->bpending[i]) {
			spin_lock_irqsave(&pxmitpriv->lock, irqL);
			pxmitpriv->txirp_cnt++;
			pxmitframe->bpending[i]  = true;
			switch (pattrib->priority) {
			case 1:
			case 2:
				pxmitpriv->bkq_cnt++;
				break;
			case 4:
			case 5:
				pxmitpriv->viq_cnt++;
				break;
			case 6:
			case 7:
				pxmitpriv->voq_cnt++;
				break;
			case 0:
			case 3:
			default:
				pxmitpriv->beq_cnt++;
				break;
			}
			spin_unlock_irqrestore(&pxmitpriv->lock, irqL);
			pxmitframe->sz[i] = (u16)cnt;
			purb = pxmitframe->pxmit_urb[i];
			break;
		}
	}
	bwritezero = false;
	if (pdvobj->ishighspeed) {
		if (cnt > 0 && cnt % 512 == 0)
			bwritezero = true;
	} else {
		if (cnt > 0 && cnt % 64 == 0)
			bwritezero = true;
	}
	/* translate DMA FIFO addr to pipehandle */
	pipe = ffaddr2pipehdl(pdvobj, addr);
	if (pxmitpriv->free_xmitbuf_cnt % NR_XMITBUFF == 0)
		purb->transfer_flags  &=  (~URB_NO_INTERRUPT);
	else
		purb->transfer_flags  |=  URB_NO_INTERRUPT;
	if (bwritezero)
		cnt += 8;
	usb_fill_bulk_urb(purb, pusbd, pipe,
			  pxmitframe->mem_addr,
			  cnt, usb_write_port_complete,
			  pxmitframe); /* context is xmit_frame */
	status = usb_submit_urb(purb, GFP_ATOMIC);
	if (!status)
		ret = _SUCCESS;
	else
		ret = _FAIL;
	return ret;
}

void r8712_usb_write_port_cancel(struct _adapter *padapter)
{
	int i, j;
	struct xmit_buf	*pxmitbuf = (struct xmit_buf *)
				     padapter->xmitpriv.pxmitbuf;

	for (i = 0; i < NR_XMITBUFF; i++) {
		for (j = 0; j < 8; j++) {
			if (pxmitbuf->pxmit_urb[j])
				usb_kill_urb(pxmitbuf->pxmit_urb[j]);
		}
		pxmitbuf++;
	}
}

int r8712_usbctrl_vendorreq(struct intf_priv *pintfpriv, u8 request, u16 value,
		      u16 index, void *pdata, u16 len, u8 requesttype)
{
	unsigned int pipe;
	int status;
	u8 reqtype;
	struct dvobj_priv *pdvobjpriv = (struct dvobj_priv *)
					 pintfpriv->intf_dev;
	struct usb_device *udev = pdvobjpriv->pusbdev;
	/* For mstar platform, mstar suggests the address for USB IO
	 * should be 16 bytes alignment. Trying to fix it here.
	 */
	u8 *palloc_buf, *pIo_buf;

	palloc_buf = kmalloc((u32)len + 16, GFP_ATOMIC);
	if (!palloc_buf)
		return -ENOMEM;
	pIo_buf = palloc_buf + 16 - ((addr_t)(palloc_buf) & 0x0f);
	if (requesttype == 0x01) {
		pipe = usb_rcvctrlpipe(udev, 0); /* read_in */
		reqtype =  RTL871X_VENQT_READ;
	} else {
		pipe = usb_sndctrlpipe(udev, 0); /* write_out */
		reqtype =  RTL871X_VENQT_WRITE;
		memcpy(pIo_buf, pdata, len);
	}
	status = usb_control_msg(udev, pipe, request, reqtype, value, index,
				 pIo_buf, len, 500);
	if (status < 0)
		goto free;
	if (status != len) {
		status = -EREMOTEIO;
		goto free;
	}
	/* Success this control transfer. */
	if (requesttype == 0x01) {
		/* For Control read transfer, we have to copy the read
		 * data from pIo_buf to pdata.
		 */
		memcpy(pdata, pIo_buf, status);
	}

free:
	kfree(palloc_buf);
	return status;
}
