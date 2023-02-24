// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2007 - 2012 Realtek Corporation. */

#define _USB_OPS_LINUX_C_

#include "../include/drv_types.h"
#include "../include/rtl8188e_recv.h"

static unsigned int ffaddr2pipehdl(struct dvobj_priv *pdvobj, u32 addr)
{
	unsigned int pipe = 0, ep_num = 0;
	struct usb_device *pusbd = pdvobj->pusbdev;

	if (addr < HW_QUEUE_ENTRY) {
		ep_num = pdvobj->Queue2Pipe[addr];
		pipe = usb_sndbulkpipe(pusbd, ep_num);
	}

	return pipe;
}

void rtw_read_port_cancel(struct adapter *padapter)
{
	int i;
	struct recv_buf *precvbuf = (struct recv_buf *)padapter->recvpriv.precv_buf;

	padapter->bReadPortCancel = true;

	for (i = 0; i < NR_RECVBUFF; i++) {
		precvbuf->reuse = true;
		usb_kill_urb(precvbuf->purb);
		precvbuf++;
	}
}

static void usb_write_port_complete(struct urb *purb)
{
	struct xmit_buf *pxmitbuf = (struct xmit_buf *)purb->context;
	struct adapter *padapter = pxmitbuf->padapter;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;

	if (pxmitbuf->high_queue)
		rtw_chk_hi_queue_cmd(padapter);

	switch (purb->status) {
	case 0:
	case -EINPROGRESS:
	case -ENOENT:
	case -ECONNRESET:
	case -EPIPE:
	case -EPROTO:
		break;
	case -ESHUTDOWN:
		padapter->bDriverStopped = true;
		break;
	default:
		padapter->bSurpriseRemoved = true;
		break;
	}

	rtw_sctx_done_err(&pxmitbuf->sctx,
			  purb->status ? RTW_SCTX_DONE_WRITE_PORT_ERR : RTW_SCTX_DONE_SUCCESS);
	rtw_free_xmitbuf(pxmitpriv, pxmitbuf);
	tasklet_hi_schedule(&pxmitpriv->xmit_tasklet);
}

u32 rtw_write_port(struct adapter *padapter, u32 addr, u32 cnt, u8 *wmem)
{
	unsigned long irqL;
	unsigned int pipe;
	int status;
	u32 ret = _FAIL;
	struct urb *purb = NULL;
	struct dvobj_priv	*pdvobj = adapter_to_dvobj(padapter);
	struct xmit_priv	*pxmitpriv = &padapter->xmitpriv;
	struct xmit_buf *pxmitbuf = (struct xmit_buf *)wmem;
	struct xmit_frame *pxmitframe = (struct xmit_frame *)pxmitbuf->priv_data;
	struct usb_device *pusbd = pdvobj->pusbdev;

	if (padapter->bDriverStopped || padapter->bSurpriseRemoved) {
		rtw_sctx_done_err(&pxmitbuf->sctx, RTW_SCTX_DONE_TX_DENY);
		goto exit;
	}

	spin_lock_irqsave(&pxmitpriv->lock, irqL);
	pxmitbuf->high_queue = (addr == HIGH_QUEUE_INX);
	spin_unlock_irqrestore(&pxmitpriv->lock, irqL);

	purb	= pxmitbuf->pxmit_urb;

	/* translate DMA FIFO addr to pipehandle */
	pipe = ffaddr2pipehdl(pdvobj, addr);

	usb_fill_bulk_urb(purb, pusbd, pipe,
			  pxmitframe->buf_addr, /*  pxmitbuf->pbuf */
			  cnt,
			  usb_write_port_complete,
			  pxmitbuf);/* context is pxmitbuf */

	status = usb_submit_urb(purb, GFP_ATOMIC);
	if (status) {
		rtw_sctx_done_err(&pxmitbuf->sctx, RTW_SCTX_DONE_WRITE_PORT_ERR);
		if (status == -ENODEV)
			padapter->bDriverStopped = true;
		goto exit;
	}

	ret = _SUCCESS;

/*    We add the URB_ZERO_PACKET flag to urb so that the host will send the zero packet automatically. */

exit:
	if (ret != _SUCCESS)
		rtw_free_xmitbuf(pxmitpriv, pxmitbuf);

	return ret;
}

void rtw_write_port_cancel(struct adapter *padapter)
{
	int i;
	struct xmit_buf *pxmitbuf = (struct xmit_buf *)padapter->xmitpriv.pxmitbuf;

	padapter->bWritePortCancel = true;

	for (i = 0; i < NR_XMITBUFF; i++) {
		usb_kill_urb(pxmitbuf->pxmit_urb);
		pxmitbuf++;
	}

	pxmitbuf = (struct xmit_buf *)padapter->xmitpriv.pxmit_extbuf;
	for (i = 0; i < NR_XMIT_EXTBUFF; i++) {
		usb_kill_urb(pxmitbuf->pxmit_urb);
		pxmitbuf++;
	}
}
