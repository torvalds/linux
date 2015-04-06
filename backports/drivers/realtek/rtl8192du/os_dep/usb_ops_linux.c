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
 *******************************************************************************/
#define _USB_OPS_LINUX_C_

#include <drv_types.h>
#include <usb_ops_linux.h>
#include <rtw_sreset.h>

unsigned int ffaddr2pipehdl(struct dvobj_priv *pdvobj, u32 addr)
{
	unsigned int pipe=0;
	int ep_num=0;
	struct rtw_adapter *padapter = pdvobj->if1;
	struct usb_device *pusbd = pdvobj->pusbdev;
	struct hal_data_8192du  *pHalData = GET_HAL_DATA(padapter);

	if (addr == RECV_BULK_IN_ADDR) {
		pipe=usb_rcvbulkpipe(pusbd, pHalData->RtBulkInPipe);

	} else if (addr == RECV_INT_IN_ADDR) {
		pipe=usb_rcvbulkpipe(pusbd, pHalData->RtIntInPipe);

	} else if (addr < HW_QUEUE_ENTRY) {
		ep_num = pHalData->Queue2EPNum[addr];
		pipe = usb_sndbulkpipe(pusbd, ep_num);
	}

	return pipe;
}

struct zero_bulkout_context{
	void *pbuf;
	void *purb;
	void *pirp;
	void *padapter;
};

static void usb_bulkout_zero_complete(struct urb *purb, struct pt_regs *regs)
{
	struct zero_bulkout_context *pcontext = (struct zero_bulkout_context *)purb->context;

	if (pcontext) {
		kfree(pcontext->pbuf);

		if (pcontext->purb && (pcontext->purb==purb))
			usb_free_urb(pcontext->purb);

		kfree(pcontext);
	}
}

static u32 usb_bulkout_zero(struct intf_hdl *pintfhdl, u32 addr)
{
	int pipe, status, len;
	u32 ret;
	unsigned char *pbuf;
	struct zero_bulkout_context *pcontext;
	struct urb *	purb = NULL;
	struct rtw_adapter *padapter = (struct rtw_adapter *)pintfhdl->padapter;
	struct dvobj_priv *pdvobj = adapter_to_dvobj(padapter);
	struct usb_device *pusbd = pdvobj->pusbdev;

	if ((padapter->bDriverStopped) || (padapter->bSurpriseRemoved) ||
	    (padapter->pwrctrlpriv.pnp_bstop_trx))
		return _FAIL;

	pcontext = (struct zero_bulkout_context *)kzalloc(sizeof(struct zero_bulkout_context), GFP_KERNEL);

	pbuf = (unsigned char *)kzalloc(sizeof(int), GFP_KERNEL);
	purb = usb_alloc_urb(0, GFP_ATOMIC);

	len = 0;
	pcontext->pbuf = pbuf;
	pcontext->purb = purb;
	pcontext->pirp = NULL;
	pcontext->padapter = padapter;

	/* translate DMA FIFO addr to pipehandle */

	usb_fill_bulk_urb(purb, pusbd, pipe, pbuf, len,
			  usb_bulkout_zero_complete, pcontext);

	status = usb_submit_urb(purb, GFP_ATOMIC);

	if (!status)
		ret= _SUCCESS;
	else
		ret= _FAIL;

	return ret;
}

void usb_read_mem(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *rmem)
{
}

void usb_write_mem(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *wmem)
{
}

void usb_read_port_cancel(struct intf_hdl *pintfhdl)
{
	int i;
	struct recv_buf *precvbuf;
	struct rtw_adapter	*padapter = pintfhdl->padapter;
	precvbuf = (struct recv_buf *)padapter->recvpriv.precv_buf;

	DBG_8192D("%s\n", __func__);

	padapter->bReadPortCancel = true;

	for (i = 0; i < NR_RECVBUFF; i++) {

		precvbuf->reuse = true;
		if (precvbuf->purb)
			usb_kill_urb(precvbuf->purb);
		precvbuf++;
	}
}

static void usb_write_port_complete(struct urb *purb, struct pt_regs *regs)
{
	long unsigned int irqL;
	int i;
	struct xmit_buf *pxmitbuf = (struct xmit_buf *)purb->context;
	struct rtw_adapter	*padapter = pxmitbuf->padapter;
       struct xmit_priv	*pxmitpriv = &padapter->xmitpriv;

	switch (pxmitbuf->flags)
	{
		case VO_QUEUE_INX:
			pxmitpriv->voq_cnt--;
			break;
		case VI_QUEUE_INX:
			pxmitpriv->viq_cnt--;
			break;
		case BE_QUEUE_INX:
			pxmitpriv->beq_cnt--;
			break;
		case BK_QUEUE_INX:
			pxmitpriv->bkq_cnt--;
			break;
		case HIGH_QUEUE_INX:
#ifdef CONFIG_92D_AP_MODE
			rtw_chk_hi_queue_cmd(padapter);
#endif
			break;
		default:
			break;
	}

	if (padapter->bSurpriseRemoved || padapter->bDriverStopped ||padapter->bWritePortCancel)
	{
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_write_port_complete:bDriverStopped(%d) OR bSurpriseRemoved(%d)", padapter->bDriverStopped, padapter->bSurpriseRemoved));
		DBG_8192D("%s(): TX Warning! bDriverStopped(%d) OR bSurpriseRemoved(%d) bWritePortCancel(%d) pxmitbuf->ext_tag(%x)\n",
		__func__,padapter->bDriverStopped, padapter->bSurpriseRemoved,padapter->bReadPortCancel,pxmitbuf->ext_tag);

		goto check_completion;
	}

	if (purb->status==0) {

	} else {
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_write_port_complete : purb->status(%d) != 0\n", purb->status));
		DBG_8192D("###=> urb_write_port_complete status(%d)\n",purb->status);
		if ((purb->status==-EPIPE)||(purb->status==-EPROTO))
		{
			sreset_set_wifi_error_status(padapter, USB_WRITE_PORT_FAIL);
		} else if (purb->status == -EINPROGRESS) {
			RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_write_port_complete: EINPROGESS\n"));
			goto check_completion;

		} else if (purb->status == -ENOENT) {
			DBG_8192D("%s: -ENOENT\n", __func__);
			goto check_completion;

		} else if (purb->status == -ECONNRESET) {
			DBG_8192D("%s: -ECONNRESET\n", __func__);
			goto check_completion;

		} else if (purb->status == -ESHUTDOWN) {
			RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_write_port_complete: ESHUTDOWN\n"));
			padapter->bDriverStopped=true;
			RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_write_port_complete:bDriverStopped=TRUE\n"));

			goto check_completion;
		}
		else
		{
			padapter->bSurpriseRemoved=true;
			DBG_8192D("bSurpriseRemoved=TRUE\n");
			RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_write_port_complete:bSurpriseRemoved=TRUE\n"));

			goto check_completion;
		}
	}

	#ifdef DBG_CONFIG_ERROR_DETECT
	{
		struct hal_data_8192du	*pHalData = GET_HAL_DATA(padapter);
		pHalData->srestpriv.last_tx_complete_time = rtw_get_current_time();
	}
	#endif

check_completion:
	rtw_sctx_done_err(&pxmitbuf->sctx,
		purb->status ? RTW_SCTX_DONE_WRITE_PORT_ERR : RTW_SCTX_DONE_SUCCESS);

	rtw_free_xmitbuf(pxmitpriv, pxmitbuf);

	{
		tasklet_hi_schedule(&pxmitpriv->xmit_tasklet);
	}

}

u32 usb_write_port(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *wmem)
{
	long unsigned int irqL;
	unsigned int pipe;
	int status;
	u32 ret = _FAIL, bwritezero = false;
	struct urb *purb = NULL;
	struct rtw_adapter *padapter = (struct rtw_adapter *)pintfhdl->padapter;
	struct dvobj_priv	*pdvobj = adapter_to_dvobj(padapter);
	struct xmit_priv	*pxmitpriv = &padapter->xmitpriv;
	struct xmit_buf *pxmitbuf = (struct xmit_buf *)wmem;
	struct xmit_frame *pxmitframe = (struct xmit_frame *)pxmitbuf->priv_data;
	struct usb_device *pusbd = pdvobj->pusbdev;
	struct pkt_attrib *pattrib = &pxmitframe->attrib;

	RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("+usb_write_port\n"));

	if ((padapter->bDriverStopped) || (padapter->bSurpriseRemoved) ||(padapter->pwrctrlpriv.pnp_bstop_trx)) {
		#ifdef DBG_TX
		DBG_8192D(" DBG_TX %s:%d bDriverStopped%d, bSurpriseRemoved:%d, pnp_bstop_trx:%d\n",__func__, __LINE__
			,padapter->bDriverStopped, padapter->bSurpriseRemoved, padapter->pwrctrlpriv.pnp_bstop_trx);
		#endif
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_write_port:(padapter->bDriverStopped ||padapter->bSurpriseRemoved ||adapter->pwrctrlpriv.pnp_bstop_trx)!!!\n"));
		rtw_sctx_done_err(&pxmitbuf->sctx, RTW_SCTX_DONE_TX_DENY);
		goto exit;
	}

	spin_lock_irqsave(&pxmitpriv->lock, irqL);

	switch (addr)
	{
		case VO_QUEUE_INX:
			pxmitpriv->voq_cnt++;
			pxmitbuf->flags = VO_QUEUE_INX;
			break;
		case VI_QUEUE_INX:
			pxmitpriv->viq_cnt++;
			pxmitbuf->flags = VI_QUEUE_INX;
			break;
		case BE_QUEUE_INX:
			pxmitpriv->beq_cnt++;
			pxmitbuf->flags = BE_QUEUE_INX;
			break;
		case BK_QUEUE_INX:
			pxmitpriv->bkq_cnt++;
			pxmitbuf->flags = BK_QUEUE_INX;
			break;
		case HIGH_QUEUE_INX:
			pxmitbuf->flags = HIGH_QUEUE_INX;
			break;
		default:
			pxmitbuf->flags = MGT_QUEUE_INX;
			break;
	}

	spin_unlock_irqrestore(&pxmitpriv->lock, irqL);

	purb	= pxmitbuf->pxmit_urb[0];

	/* translate DMA FIFO addr to pipehandle */
	pipe = ffaddr2pipehdl(pdvobj, addr);

#ifdef CONFIG_REDUCE_USB_TX_INT
	if ((pxmitpriv->free_xmitbuf_cnt%NR_XMITBUFF == 0) ||
	    (pxmitbuf->ext_tag == true))
		purb->transfer_flags  &=  (~URB_NO_INTERRUPT);
	else
		purb->transfer_flags  |=  URB_NO_INTERRUPT;
#endif

	usb_fill_bulk_urb(purb, pusbd, pipe,
				pxmitframe->buf_addr, /*  pxmitbuf->pbuf */
				cnt,
				usb_write_port_complete,
				pxmitbuf);/* context is pxmitbuf */

	status = usb_submit_urb(purb, GFP_ATOMIC);
	if (!status) {
		#ifdef DBG_CONFIG_ERROR_DETECT
		{
			struct hal_data_8192du	*pHalData = GET_HAL_DATA(padapter);
			pHalData->srestpriv.last_tx_time = rtw_get_current_time();
		}
		#endif
	} else {
		rtw_sctx_done_err(&pxmitbuf->sctx, RTW_SCTX_DONE_WRITE_PORT_ERR);
		DBG_8192D("usb_write_port, status=%d\n", status);
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_write_port(): usb_submit_urb, status=%x\n", status));

		switch (status) {
		case -ENODEV:
			padapter->bDriverStopped=true;
			break;
		default:
			break;
		}
		goto exit;
	}
	ret= _SUCCESS;
	RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("-usb_write_port\n"));

exit:
	if (ret != _SUCCESS)
		rtw_free_xmitbuf(pxmitpriv, pxmitbuf);

	return ret;
}

void usb_write_port_cancel(struct intf_hdl *pintfhdl)
{
	int i, j;
	struct rtw_adapter	*padapter = pintfhdl->padapter;
	struct xmit_buf *pxmitbuf = (struct xmit_buf *)padapter->xmitpriv.pxmitbuf;

	DBG_8192D("%s\n", __func__);

	padapter->bWritePortCancel = true;

	for (i=0; i<NR_XMITBUFF; i++) {
		for (j=0; j<8; j++) {
			if (pxmitbuf->pxmit_urb[j]) {
				usb_kill_urb(pxmitbuf->pxmit_urb[j]);
			}
		}
		pxmitbuf++;
	}

	pxmitbuf = (struct xmit_buf*)padapter->xmitpriv.pxmit_extbuf;
	for (i = 0; i < NR_XMIT_EXTBUFF; i++) {
		for (j=0; j<8; j++) {
			if (pxmitbuf->pxmit_urb[j]) {
				usb_kill_urb(pxmitbuf->pxmit_urb[j]);
			}
		}
		pxmitbuf++;
	}
}
