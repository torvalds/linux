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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 ******************************************************************************/
#define _USB_OPS_LINUX_C_

#include <drv_types.h>
#include <usb_ops_linux.h>
#include <rtw_sreset.h>

unsigned int ffaddr2pipehdl(struct dvobj_priv *pdvobj, u32 addr)
{
	unsigned int pipe = 0, ep_num = 0;
	struct usb_device *pusbd = pdvobj->pusbdev;

	if (addr == RECV_BULK_IN_ADDR) {
		pipe = usb_rcvbulkpipe(pusbd, pdvobj->RtInPipe[0]);
	} else if (addr == RECV_INT_IN_ADDR) {
		pipe = usb_rcvbulkpipe(pusbd, pdvobj->RtInPipe[1]);
	} else if (addr < HW_QUEUE_ENTRY) {
		ep_num = pdvobj->Queue2Pipe[addr];
		pipe = usb_sndbulkpipe(pusbd, ep_num);
	}

	return pipe;
}

struct zero_bulkout_context {
	void *pbuf;
	void *purb;
	void *pirp;
	void *padapter;
};

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
	struct adapter	*padapter = pintfhdl->padapter;
	precvbuf = (struct recv_buf *)padapter->recvpriv.precv_buf;

	DBG_88E("%s\n", __func__);

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
	struct xmit_buf *pxmitbuf = (struct xmit_buf *)purb->context;
	struct adapter	*padapter = pxmitbuf->padapter;
	struct xmit_priv	*pxmitpriv = &padapter->xmitpriv;
	struct hal_data_8188e	*haldata;

_func_enter_;

	switch (pxmitbuf->flags) {
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
#ifdef CONFIG_88EU_AP_MODE
		rtw_chk_hi_queue_cmd(padapter);
#endif
		break;
	default:
		break;
	}

	if (padapter->bSurpriseRemoved || padapter->bDriverStopped ||
	    padapter->bWritePortCancel) {
		RT_TRACE(_module_hci_ops_os_c_, _drv_err_,
			 ("usb_write_port_complete:bDriverStopped(%d) OR bSurpriseRemoved(%d)",
			 padapter->bDriverStopped, padapter->bSurpriseRemoved));
		DBG_88E("%s(): TX Warning! bDriverStopped(%d) OR bSurpriseRemoved(%d) bWritePortCancel(%d) pxmitbuf->ext_tag(%x)\n",
			__func__, padapter->bDriverStopped,
			padapter->bSurpriseRemoved, padapter->bReadPortCancel,
			pxmitbuf->ext_tag);

		goto check_completion;
	}

	if (purb->status) {
		RT_TRACE(_module_hci_ops_os_c_, _drv_err_, ("usb_write_port_complete : purb->status(%d) != 0\n", purb->status));
		DBG_88E("###=> urb_write_port_complete status(%d)\n", purb->status);
		if ((purb->status == -EPIPE) || (purb->status == -EPROTO)) {
			sreset_set_wifi_error_status(padapter, USB_WRITE_PORT_FAIL);
		} else if (purb->status == -EINPROGRESS) {
			RT_TRACE(_module_hci_ops_os_c_, _drv_err_, ("usb_write_port_complete: EINPROGESS\n"));
			goto check_completion;
		} else if (purb->status == -ENOENT) {
			DBG_88E("%s: -ENOENT\n", __func__);
			goto check_completion;
		} else if (purb->status == -ECONNRESET) {
			DBG_88E("%s: -ECONNRESET\n", __func__);
			goto check_completion;
		} else if (purb->status == -ESHUTDOWN) {
			RT_TRACE(_module_hci_ops_os_c_, _drv_err_, ("usb_write_port_complete: ESHUTDOWN\n"));
			padapter->bDriverStopped = true;
			RT_TRACE(_module_hci_ops_os_c_, _drv_err_, ("usb_write_port_complete:bDriverStopped = true\n"));
			goto check_completion;
		} else {
			padapter->bSurpriseRemoved = true;
			DBG_88E("bSurpriseRemoved = true\n");
			RT_TRACE(_module_hci_ops_os_c_, _drv_err_, ("usb_write_port_complete:bSurpriseRemoved = true\n"));

			goto check_completion;
		}
	}

	haldata = GET_HAL_DATA(padapter);
	haldata->srestpriv.last_tx_complete_time = jiffies;

check_completion:
	rtw_sctx_done_err(&pxmitbuf->sctx,
			  purb->status ? RTW_SCTX_DONE_WRITE_PORT_ERR :
			  RTW_SCTX_DONE_SUCCESS);

	rtw_free_xmitbuf(pxmitpriv, pxmitbuf);

	tasklet_hi_schedule(&pxmitpriv->xmit_tasklet);

_func_exit_;
}

u32 usb_write_port(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *wmem)
{
	unsigned long irqL;
	unsigned int pipe;
	int status;
	u32 ret = _FAIL;
	struct urb *purb = NULL;
	struct adapter *padapter = (struct adapter *)pintfhdl->padapter;
	struct dvobj_priv	*pdvobj = adapter_to_dvobj(padapter);
	struct xmit_priv	*pxmitpriv = &padapter->xmitpriv;
	struct xmit_buf *pxmitbuf = (struct xmit_buf *)wmem;
	struct xmit_frame *pxmitframe = (struct xmit_frame *)pxmitbuf->priv_data;
	struct usb_device *pusbd = pdvobj->pusbdev;

_func_enter_;

	RT_TRACE(_module_hci_ops_os_c_, _drv_err_, ("+usb_write_port\n"));

	if ((padapter->bDriverStopped) || (padapter->bSurpriseRemoved) ||
	    (padapter->pwrctrlpriv.pnp_bstop_trx)) {
		RT_TRACE(_module_hci_ops_os_c_, _drv_err_,
			 ("usb_write_port:( padapter->bDriverStopped ||padapter->bSurpriseRemoved ||adapter->pwrctrlpriv.pnp_bstop_trx)!!!\n"));
		rtw_sctx_done_err(&pxmitbuf->sctx, RTW_SCTX_DONE_TX_DENY);
		goto exit;
	}

	spin_lock_irqsave(&pxmitpriv->lock, irqL);

	switch (addr) {
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

	usb_fill_bulk_urb(purb, pusbd, pipe,
			  pxmitframe->buf_addr, /*  pxmitbuf->pbuf */
			  cnt,
			  usb_write_port_complete,
			  pxmitbuf);/* context is pxmitbuf */

	status = usb_submit_urb(purb, GFP_ATOMIC);
	if (!status) {
		struct hal_data_8188e	*haldata = GET_HAL_DATA(padapter);

		haldata->srestpriv.last_tx_time = jiffies;
	} else {
		rtw_sctx_done_err(&pxmitbuf->sctx, RTW_SCTX_DONE_WRITE_PORT_ERR);
		DBG_88E("usb_write_port, status =%d\n", status);
		RT_TRACE(_module_hci_ops_os_c_, _drv_err_, ("usb_write_port(): usb_submit_urb, status =%x\n", status));

		switch (status) {
		case -ENODEV:
			padapter->bDriverStopped = true;
			break;
		default:
			break;
		}
		goto exit;
	}

	ret = _SUCCESS;

/*    We add the URB_ZERO_PACKET flag to urb so that the host will send the zero packet automatically. */

	RT_TRACE(_module_hci_ops_os_c_, _drv_err_, ("-usb_write_port\n"));

exit:
	if (ret != _SUCCESS)
		rtw_free_xmitbuf(pxmitpriv, pxmitbuf);
_func_exit_;
	return ret;
}

void usb_write_port_cancel(struct intf_hdl *pintfhdl)
{
	int i, j;
	struct adapter	*padapter = pintfhdl->padapter;
	struct xmit_buf *pxmitbuf = (struct xmit_buf *)padapter->xmitpriv.pxmitbuf;

	DBG_88E("%s\n", __func__);

	padapter->bWritePortCancel = true;

	for (i = 0; i < NR_XMITBUFF; i++) {
		for (j = 0; j < 8; j++) {
			if (pxmitbuf->pxmit_urb[j])
				usb_kill_urb(pxmitbuf->pxmit_urb[j]);
		}
		pxmitbuf++;
	}

	pxmitbuf = (struct xmit_buf *)padapter->xmitpriv.pxmit_extbuf;
	for (i = 0; i < NR_XMIT_EXTBUFF; i++) {
		for (j = 0; j < 8; j++) {
			if (pxmitbuf->pxmit_urb[j])
				usb_kill_urb(pxmitbuf->pxmit_urb[j]);
		}
		pxmitbuf++;
	}
}
