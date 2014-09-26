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
 *******************************************************************************/
#define _USB_OPS_LINUX_C_

#include <drv_types.h>
#include <hal_data.h>
#include <rtw_sreset.h>

#ifdef CONFIG_RTL8192D
#include <Hal8192DPhyReg.h>
#endif


struct rtw_async_write_data {
	u8 data[VENDOR_CMD_MAX_DATA_LEN];
	struct usb_ctrlrequest dr;
};
	
int usbctrl_vendorreq(struct intf_hdl *pintfhdl, u8 request, u16 value, u16 index, void *pdata, u16 len, u8 requesttype)
{
	_adapter	*padapter = pintfhdl->padapter;
	struct dvobj_priv  *pdvobjpriv = adapter_to_dvobj(padapter);
	struct pwrctrl_priv *pwrctl = dvobj_to_pwrctl(pdvobjpriv);
	struct usb_device *udev=pdvobjpriv->pusbdev;

	unsigned int pipe;
	int status = 0;
	u32 tmp_buflen=0;
	u8 reqtype;
	u8 *pIo_buf;
	int vendorreq_times = 0;

	#ifdef CONFIG_USB_VENDOR_REQ_BUFFER_DYNAMIC_ALLOCATE
	u8 *tmp_buf;
	#else // use stack memory
	u8 tmp_buf[MAX_USB_IO_CTL_SIZE];
	#endif

#ifdef CONFIG_CONCURRENT_MODE
	if(padapter->adapter_type > PRIMARY_ADAPTER)
	{
		padapter = padapter->pbuddy_adapter;
		pdvobjpriv = adapter_to_dvobj(padapter);
		udev = pdvobjpriv->pusbdev;
	}
#endif

	//DBG_871X("%s %s:%d\n",__FUNCTION__, current->comm, current->pid);

	if (RTW_CANNOT_IO(padapter)){
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usbctrl_vendorreq:(RTW_CANNOT_IO)!!!\n"));
		status = -EPERM; 
		goto exit;
	}	

	if(len>MAX_VENDOR_REQ_CMD_SIZE){
		DBG_8192C( "[%s] Buffer len error ,vendor request failed\n", __FUNCTION__ );
		status = -EINVAL;
		goto exit;
	}	

	#ifdef CONFIG_USB_VENDOR_REQ_MUTEX
	_enter_critical_mutex(&pdvobjpriv->usb_vendor_req_mutex, NULL);
	#endif	

	
	// Acquire IO memory for vendorreq
#ifdef CONFIG_USB_VENDOR_REQ_BUFFER_PREALLOC
	pIo_buf = pdvobjpriv->usb_vendor_req_buf;
#else
	#ifdef CONFIG_USB_VENDOR_REQ_BUFFER_DYNAMIC_ALLOCATE
	tmp_buf = rtw_malloc( (u32) len + ALIGNMENT_UNIT);
	tmp_buflen =  (u32)len + ALIGNMENT_UNIT;
	#else // use stack memory
	tmp_buflen = MAX_USB_IO_CTL_SIZE;
	#endif

	// Added by Albert 2010/02/09
	// For mstar platform, mstar suggests the address for USB IO should be 16 bytes alignment.
	// Trying to fix it here.
	pIo_buf = (tmp_buf==NULL)?NULL:tmp_buf + ALIGNMENT_UNIT -((SIZE_PTR)(tmp_buf) & 0x0f );	
#endif

	if ( pIo_buf== NULL) {
		DBG_8192C( "[%s] pIo_buf == NULL \n", __FUNCTION__ );
		status = -ENOMEM;
		goto release_mutex;
	}
	
	while(++vendorreq_times<= MAX_USBCTRL_VENDORREQ_TIMES)
	{
		_rtw_memset(pIo_buf, 0, len);
		
		if (requesttype == 0x01)
		{
			pipe = usb_rcvctrlpipe(udev, 0);//read_in
			reqtype =  REALTEK_USB_VENQT_READ;		
		} 
		else 
		{
			pipe = usb_sndctrlpipe(udev, 0);//write_out
			reqtype =  REALTEK_USB_VENQT_WRITE;		
			_rtw_memcpy( pIo_buf, pdata, len);
		}		
	
		status = rtw_usb_control_msg(udev, pipe, request, reqtype, value, index, pIo_buf, len, RTW_USB_CONTROL_MSG_TIMEOUT);
		
		if ( status == len)   // Success this control transfer.
		{
			rtw_reset_continual_io_error(pdvobjpriv);
			if ( requesttype == 0x01 )
			{   // For Control read transfer, we have to copy the read data from pIo_buf to pdata.
				_rtw_memcpy( pdata, pIo_buf,  len );
			}
		}
		else { // error cases
			DBG_8192C("reg 0x%x, usb %s %u fail, status:%d value=0x%x, vendorreq_times:%d\n"
				, value,(requesttype == 0x01)?"read":"write" , len, status, *(u32*)pdata, vendorreq_times);
			
			if (status < 0) {
				if(status == (-ESHUTDOWN)	|| status == -ENODEV	)
				{			
					padapter->bSurpriseRemoved = _TRUE;
				} else {
					#ifdef DBG_CONFIG_ERROR_DETECT
					{
						HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
						pHalData->srestpriv.Wifi_Error_Status = USB_VEN_REQ_CMD_FAIL;
					}
					#endif
				}
			}
			else // status != len && status >= 0
			{
				if(status > 0) {
					if ( requesttype == 0x01 )
					{   // For Control read transfer, we have to copy the read data from pIo_buf to pdata.
						_rtw_memcpy( pdata, pIo_buf,  len );
					}
				}
			}

			if(rtw_inc_and_chk_continual_io_error(pdvobjpriv) == _TRUE ){
				padapter->bSurpriseRemoved = _TRUE;
				break;
			}
	
		}
	
		// firmware download is checksumed, don't retry
		if( (value >= FW_START_ADDRESS ) || status == len )
			break;
	
	}

	// release IO memory used by vendorreq
	#ifdef CONFIG_USB_VENDOR_REQ_BUFFER_DYNAMIC_ALLOCATE
	rtw_mfree(tmp_buf, tmp_buflen);
	#endif

release_mutex:
	#ifdef CONFIG_USB_VENDOR_REQ_MUTEX
	_exit_critical_mutex(&pdvobjpriv->usb_vendor_req_mutex, NULL);
	#endif
exit:
	return status;

}

#ifdef CONFIG_USB_SUPPORT_ASYNC_VDN_REQ
static void _usbctrl_vendorreq_async_callback(struct urb *urb, struct pt_regs *regs)
{
	if (urb) {
		if (urb->context) {
			rtw_mfree(urb->context, sizeof(struct rtw_async_write_data));
		}
		usb_free_urb(urb);
	}
}

int _usbctrl_vendorreq_async_write(struct usb_device *udev, u8 request,
	u16 value, u16 index, void *pdata, u16 len, u8 requesttype)
{
	int rc;
	unsigned int pipe;
	u8 reqtype;
	struct usb_ctrlrequest *dr;
	struct urb *urb;
	struct rtw_async_write_data *buf;


	if (requesttype == VENDOR_READ) {
		pipe = usb_rcvctrlpipe(udev, 0);//read_in
		reqtype =  REALTEK_USB_VENQT_READ;
	}
	else {
		pipe = usb_sndctrlpipe(udev, 0);//write_out
		reqtype =  REALTEK_USB_VENQT_WRITE;
	}
	
	buf = (struct rtl819x_async_write_data *)rtw_zmalloc(sizeof(*buf));
	if (!buf) {
		rc = -ENOMEM;
		goto exit;
	}

	urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (!urb) {
		rtw_mfree((u8*)buf, sizeof(*buf));
		rc = -ENOMEM;
		goto exit;
	}

	dr = &buf->dr;

	dr->bRequestType = reqtype;
	dr->bRequest = request;
	dr->wValue = cpu_to_le16(value);
	dr->wIndex = cpu_to_le16(index);
	dr->wLength = cpu_to_le16(len);

	_rtw_memcpy(buf, pdata, len);

	usb_fill_control_urb(urb, udev, pipe, (unsigned char *)dr, buf, len,
		_usbctrl_vendorreq_async_callback, buf);

	rc = usb_submit_urb(urb, GFP_ATOMIC);
	if (rc < 0) {
		rtw_mfree((u8*)buf, sizeof(*buf));
		usb_free_urb(urb);
	}

exit:
	return rc;
}


#endif /* CONFIG_USB_SUPPORT_ASYNC_VDN_REQ */

unsigned int ffaddr2pipehdl(struct dvobj_priv *pdvobj, u32 addr)
{
	unsigned int pipe=0, ep_num=0;
	struct usb_device *pusbd = pdvobj->pusbdev;	

	if (addr == RECV_BULK_IN_ADDR) {	
		pipe=usb_rcvbulkpipe(pusbd, pdvobj->RtInPipe[0]);
		
	} else if (addr == RECV_INT_IN_ADDR) {	
		pipe=usb_rcvintpipe(pusbd, pdvobj->RtInPipe[1]);
		
	} else if (addr < HW_QUEUE_ENTRY) {
		ep_num = pdvobj->Queue2Pipe[addr];
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

	//DBG_8192C("+usb_bulkout_zero_complete\n");
	
	if(pcontext)
	{
		if(pcontext->pbuf)
		{			
			rtw_mfree(pcontext->pbuf, sizeof(int));	
		}	

		if(pcontext->purb && (pcontext->purb==purb))
		{
			usb_free_urb(pcontext->purb);
		}

	
		rtw_mfree((u8*)pcontext, sizeof(struct zero_bulkout_context));	
	}	
	

}

static u32 usb_bulkout_zero(struct intf_hdl *pintfhdl, u32 addr)
{	
	int pipe, status, len;
	u32 ret;
	unsigned char *pbuf;
	struct zero_bulkout_context *pcontext;
	PURB	purb = NULL;	
	_adapter *padapter = (_adapter *)pintfhdl->padapter;
	struct dvobj_priv *pdvobj = adapter_to_dvobj(padapter);
	struct pwrctrl_priv *pwrctl = dvobj_to_pwrctl(pdvobj);
	struct usb_device *pusbd = pdvobj->pusbdev;

	//DBG_871X("%s\n", __func__);
	
		
	if (RTW_CANNOT_TX(padapter))
	{		
		return _FAIL;
	}
	

	pcontext = (struct zero_bulkout_context *)rtw_zmalloc(sizeof(struct zero_bulkout_context));
	if (pcontext == NULL) {
		return _FAIL;
	}

	pbuf = (unsigned char *)rtw_zmalloc(sizeof(int));	
    	purb = usb_alloc_urb(0, GFP_ATOMIC);

	//translate DMA FIFO addr to pipehandle
	pipe = ffaddr2pipehdl(pdvobj, addr);

	len = 0;
	pcontext->pbuf = pbuf;
	pcontext->purb = purb;
	pcontext->pirp = NULL;
	pcontext->padapter = padapter;

	
	//translate DMA FIFO addr to pipehandle
	//pipe = ffaddr2pipehdl(pdvobj, addr);	

	usb_fill_bulk_urb(purb, pusbd, pipe, 
       				pbuf,
              			len,
              			usb_bulkout_zero_complete,
              			pcontext);//context is pcontext

	status = usb_submit_urb(purb, GFP_ATOMIC);

	if (!status)
	{		
		ret= _SUCCESS;
	}
	else
	{
		ret= _FAIL;
	}
	
	
	return _SUCCESS;

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
	_adapter	*padapter = pintfhdl->padapter;
	precvbuf = (struct recv_buf *)padapter->recvpriv.precv_buf;

	DBG_871X("%s\n", __func__);

	for (i=0; i < NR_RECVBUFF ; i++) {
		
		precvbuf->reuse = _TRUE;	
		if (precvbuf->purb)	 {
			//DBG_8192C("usb_read_port_cancel : usb_kill_urb \n");
			usb_kill_urb(precvbuf->purb);
		}		
		precvbuf++;
	}

#ifdef CONFIG_USB_INTERRUPT_IN_PIPE
	usb_kill_urb(padapter->recvpriv.int_in_urb);
#endif
}

static void usb_write_port_complete(struct urb *purb, struct pt_regs *regs)
{
	_irqL irqL;
	int i;
	struct xmit_buf *pxmitbuf = (struct xmit_buf *)purb->context;
	//struct xmit_frame *pxmitframe = (struct xmit_frame *)pxmitbuf->priv_data;
	//_adapter			*padapter = pxmitframe->padapter;
	_adapter	*padapter = pxmitbuf->padapter;
       struct xmit_priv	*pxmitpriv = &padapter->xmitpriv;		
	//struct pkt_attrib *pattrib = &pxmitframe->attrib;
	   
_func_enter_;

	switch(pxmitbuf->flags)
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
		default:			
			break;
	}
		

/*
	_enter_critical(&pxmitpriv->lock, &irqL);

	pxmitpriv->txirp_cnt--;
	
	switch(pattrib->priority) 
	{
		case 1:				
		case 2:
			pxmitpriv->bkq_cnt--;
			//DBG_8192C("pxmitpriv->bkq_cnt=%d\n", pxmitpriv->bkq_cnt);
			break;
		case 4:
		case 5:
			pxmitpriv->viq_cnt--;
			//DBG_8192C("pxmitpriv->viq_cnt=%d\n", pxmitpriv->viq_cnt);
			break;
		case 6:
		case 7:
			pxmitpriv->voq_cnt--;
			//DBG_8192C("pxmitpriv->voq_cnt=%d\n", pxmitpriv->voq_cnt);
			break;
		case 0:
		case 3:			
		default:
			pxmitpriv->beq_cnt--;
			//DBG_8192C("pxmitpriv->beq_cnt=%d\n", pxmitpriv->beq_cnt);
			break;
			
	}	
	
	_exit_critical(&pxmitpriv->lock, &irqL);
	
	
	if(pxmitpriv->txirp_cnt==0)
	{
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_write_port_complete: txirp_cnt== 0, set allrxreturnevt!\n"));		
		_rtw_up_sema(&(pxmitpriv->tx_retevt));
	}
*/
        //rtw_free_xmitframe(pxmitpriv, pxmitframe);
	
	if (RTW_CANNOT_TX(padapter))
	{
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_write_port_complete:bDriverStopped(%d) OR bSurpriseRemoved(%d)", padapter->bDriverStopped, padapter->bSurpriseRemoved));
		DBG_8192C("%s(): TX Warning! bDriverStopped(%d) OR bSurpriseRemoved(%d) pxmitbuf->buf_tag(%x) \n", 
		__FUNCTION__,padapter->bDriverStopped, padapter->bSurpriseRemoved,pxmitbuf->buf_tag);	

		goto check_completion;
	}


	if (purb->status==0) {
	
	} else {
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_write_port_complete : purb->status(%d) != 0 \n", purb->status));
		DBG_871X("###=> urb_write_port_complete status(%d)\n",purb->status);
		if((purb->status==-EPIPE)||(purb->status==-EPROTO))
		{
			//usb_clear_halt(pusbdev, purb->pipe);	
			//msleep(10);
			sreset_set_wifi_error_status(padapter, USB_WRITE_PORT_FAIL);
		} else if (purb->status == -EINPROGRESS) {
			RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_write_port_complete: EINPROGESS\n"));
			goto check_completion;

		} else if (purb->status == -ENOENT) {
			DBG_871X("%s: -ENOENT\n", __func__);
			goto check_completion;
			
		} else if (purb->status == -ECONNRESET) {
			DBG_871X("%s: -ECONNRESET\n", __func__);
			goto check_completion;

		} else if (purb->status == -ESHUTDOWN) {
			RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_write_port_complete: ESHUTDOWN\n"));
			padapter->bDriverStopped=_TRUE;
			RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_write_port_complete:bDriverStopped=TRUE\n"));

			goto check_completion;
		}
		else
		{					
			padapter->bSurpriseRemoved=_TRUE;
			DBG_8192C("bSurpriseRemoved=TRUE\n");
			//rtl8192cu_trigger_gpio_0(padapter);
			RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_write_port_complete:bSurpriseRemoved=TRUE\n"));

			goto check_completion;
		}
	}

	#ifdef DBG_CONFIG_ERROR_DETECT
	{	
		HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
		pHalData->srestpriv.last_tx_complete_time = rtw_get_current_time();		
	}
	#endif

check_completion:
	_enter_critical(&pxmitpriv->lock_sctx, &irqL);
	rtw_sctx_done_err(&pxmitbuf->sctx,
	purb->status ? RTW_SCTX_DONE_WRITE_PORT_ERR : RTW_SCTX_DONE_SUCCESS);
	_exit_critical(&pxmitpriv->lock_sctx, &irqL);

	rtw_free_xmitbuf(pxmitpriv, pxmitbuf);

	//if(rtw_txframes_pending(padapter))	
	{
		tasklet_hi_schedule(&pxmitpriv->xmit_tasklet);
	}
	
_func_exit_;	

}

u32 usb_write_port(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *wmem)
{    
	_irqL irqL;
	unsigned int pipe;
	int status;
	u32 ret = _FAIL, bwritezero = _FALSE;
	PURB	purb = NULL;
	_adapter *padapter = (_adapter *)pintfhdl->padapter;
	struct dvobj_priv	*pdvobj = adapter_to_dvobj(padapter);
	struct pwrctrl_priv *pwrctl = dvobj_to_pwrctl(pdvobj);
	struct xmit_priv	*pxmitpriv = &padapter->xmitpriv;
	struct xmit_buf *pxmitbuf = (struct xmit_buf *)wmem;
	struct xmit_frame *pxmitframe = (struct xmit_frame *)pxmitbuf->priv_data;
	struct usb_device *pusbd = pdvobj->pusbdev;
	struct pkt_attrib *pattrib = &pxmitframe->attrib;
	
_func_enter_;	
	
	RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("+usb_write_port\n"));

	if (RTW_CANNOT_TX(padapter)) {
		#ifdef DBG_TX
		DBG_871X(" DBG_TX %s:%d bDriverStopped%d, bSurpriseRemoved:%d\n",__FUNCTION__, __LINE__
			,padapter->bDriverStopped, padapter->bSurpriseRemoved);
		#endif
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_write_port:( padapter->bDriverStopped ||padapter->bSurpriseRemoved )!!!\n"));
		rtw_sctx_done_err(&pxmitbuf->sctx, RTW_SCTX_DONE_TX_DENY);
		goto exit;
	}
	
	_enter_critical(&pxmitpriv->lock, &irqL);

	switch(addr)
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
		
	_exit_critical(&pxmitpriv->lock, &irqL);
		
	purb	= pxmitbuf->pxmit_urb[0];

	//translate DMA FIFO addr to pipehandle
	pipe = ffaddr2pipehdl(pdvobj, addr);	

#ifdef CONFIG_REDUCE_USB_TX_INT	
	if ( (pxmitpriv->free_xmitbuf_cnt%NR_XMITBUFF == 0)
		|| (pxmitbuf->buf_tag > XMITBUF_DATA) )
	{
		purb->transfer_flags  &=  (~URB_NO_INTERRUPT);
	} else {
		purb->transfer_flags  |=  URB_NO_INTERRUPT;
		//DBG_8192C("URB_NO_INTERRUPT ");
	}
#endif


	usb_fill_bulk_urb(purb, pusbd, pipe, 
       				pxmitframe->buf_addr, //= pxmitbuf->pbuf
              			cnt,
              			usb_write_port_complete,
              			pxmitbuf);//context is pxmitbuf
              			
#ifdef CONFIG_USE_USB_BUFFER_ALLOC_TX
	purb->transfer_dma = pxmitbuf->dma_transfer_addr;
	purb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	purb->transfer_flags |= URB_ZERO_PACKET;
#endif	// CONFIG_USE_USB_BUFFER_ALLOC_TX

#ifdef USB_PACKET_OFFSET_SZ
#if (USB_PACKET_OFFSET_SZ == 0)
	purb->transfer_flags |= URB_ZERO_PACKET;
#endif
#endif

#if 0
	if (bwritezero)
        {
            purb->transfer_flags |= URB_ZERO_PACKET;           
        }			
#endif

	status = usb_submit_urb(purb, GFP_ATOMIC);
	if (!status) {
		#ifdef DBG_CONFIG_ERROR_DETECT	
		{	
			HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
			pHalData->srestpriv.last_tx_time = rtw_get_current_time();		
		}
		#endif
	} else {
		rtw_sctx_done_err(&pxmitbuf->sctx, RTW_SCTX_DONE_WRITE_PORT_ERR);
		DBG_871X("usb_write_port, status=%d\n", status);
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_write_port(): usb_submit_urb, status=%x\n", status));
		
		switch (status) {
		case -ENODEV:
			padapter->bDriverStopped=_TRUE;
			break;
		default:
			break;
		}
		goto exit;
	}

	ret= _SUCCESS;

//   Commented by Albert 2009/10/13
//   We add the URB_ZERO_PACKET flag to urb so that the host will send the zero packet automatically.
/*	
	if(bwritezero == _TRUE)
	{
		usb_bulkout_zero(pintfhdl, addr);
	}
*/

	RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("-usb_write_port\n"));

exit:
	if (ret != _SUCCESS)
		rtw_free_xmitbuf(pxmitpriv, pxmitbuf);
_func_exit_;
	return ret;

}

void usb_write_port_cancel(struct intf_hdl *pintfhdl)
{
	int i, j;
	_adapter	*padapter = pintfhdl->padapter;
	struct xmit_buf *pxmitbuf = (struct xmit_buf *)padapter->xmitpriv.pxmitbuf;

	DBG_871X("%s \n", __func__);

	for (i=0; i<NR_XMITBUFF; i++) {
		for (j=0; j<8; j++) {
			if (pxmitbuf->pxmit_urb[j]) {
				usb_kill_urb(pxmitbuf->pxmit_urb[j]);
			}
		}
		pxmitbuf++;
	}
	
	pxmitbuf = (struct xmit_buf*)padapter->xmitpriv.pxmit_extbuf;
	for (i = 0; i < NR_XMIT_EXTBUFF ; i++) {	
		for (j=0; j<8; j++) {
			if(pxmitbuf->pxmit_urb[j]) {
				usb_kill_urb(pxmitbuf->pxmit_urb[j]);
			}
		}
		pxmitbuf++;
	}
}

void usb_init_recvbuf(_adapter *padapter, struct recv_buf *precvbuf)
{

	precvbuf->transfer_len = 0;

	precvbuf->len = 0;

	precvbuf->ref_cnt = 0;

	if(precvbuf->pbuf)
	{
		precvbuf->pdata = precvbuf->phead = precvbuf->ptail = precvbuf->pbuf;
		precvbuf->pend = precvbuf->pdata + MAX_RECVBUF_SZ;
	}

}

int recvbuf2recvframe(	_adapter *padapter, 
#ifdef CONFIG_USE_USB_BUFFER_ALLOC_RX
	struct recv_buf *precvbuf
#else
	_pkt *pskb
#endif
);

#ifdef CONFIG_USE_USB_BUFFER_ALLOC_RX
void usb_recv_tasklet(void *priv)
{	
	struct recv_buf *precvbuf = NULL;
	_adapter	*padapter = (_adapter*)priv;
	struct recv_priv	*precvpriv = &padapter->recvpriv;

	while (NULL != (precvbuf = rtw_dequeue_recvbuf(&precvpriv->recv_buf_pending_queue)))
	{
		if ((padapter->bDriverStopped == _TRUE)||(padapter->bSurpriseRemoved== _TRUE))
		{
			DBG_8192C("recv_tasklet => bDriverStopped or bSurpriseRemoved \n");
			
			break;
		}
		

		recvbuf2recvframe(padapter, precvbuf);

		rtw_read_port(padapter, precvpriv->ff_hwaddr, 0, (unsigned char *)precvbuf);
	}	
	
}

void usb_read_port_complete(struct urb *purb, struct pt_regs *regs)
{	
	struct recv_buf	*precvbuf = (struct recv_buf *)purb->context;	
	_adapter 			*padapter =(_adapter *)precvbuf->adapter;
	struct recv_priv	*precvpriv = &padapter->recvpriv;

	RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port_complete!!!\n"));
	
	ATOMIC_DEC(&(precvpriv->rx_pending_cnt));
		
	if (RTW_CANNOT_RX(padapter))
	{
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port_complete:bDriverStopped(%d) OR bSurpriseRemoved(%d)\n", padapter->bDriverStopped, padapter->bSurpriseRemoved));		
		DBG_8192C("%s() RX Warning! bDriverStopped(%d) OR bSurpriseRemoved(%d) \n", 
		__FUNCTION__,padapter->bDriverStopped, padapter->bSurpriseRemoved);
		goto exit;
	}

	if(purb->status==0)//SUCCESS
	{
		if ((purb->actual_length > MAX_RECVBUF_SZ) || (purb->actual_length < RXDESC_SIZE))
		{
			RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port_complete: (purb->actual_length > MAX_RECVBUF_SZ) || (purb->actual_length < RXDESC_SIZE)\n"));

			rtw_read_port(padapter, precvpriv->ff_hwaddr, 0, (unsigned char *)precvbuf);
		}
		else 
		{			
			rtw_reset_continual_io_error(adapter_to_dvobj(padapter));
			
			precvbuf->transfer_len = purb->actual_length;	

			//rtw_enqueue_rx_transfer_buffer(precvpriv, rx_transfer_buf);			
			rtw_enqueue_recvbuf(precvbuf, &precvpriv->recv_buf_pending_queue);

			tasklet_schedule(&precvpriv->recv_tasklet);			
		}		
	}
	else
	{
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port_complete : purb->status(%d) != 0 \n", purb->status));
	
		DBG_8192C("###=> usb_read_port_complete => urb status(%d)\n", purb->status);

		if(rtw_inc_and_chk_continual_io_error(adapter_to_dvobj(padapter)) == _TRUE ){
			padapter->bSurpriseRemoved = _TRUE;
		}

		switch(purb->status) {
			case -EINVAL:
			case -EPIPE:			
			case -ENODEV:
			case -ESHUTDOWN:
				//padapter->bSurpriseRemoved=_TRUE;
				//RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port_complete:bSurpriseRemoved=TRUE\n"));
			case -ENOENT:
				padapter->bDriverStopped=_TRUE;			
				RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port_complete:bDriverStopped=TRUE\n"));
				break;
			case -EPROTO:
			case -EILSEQ:
			case -ETIME:
			case -ECOMM:
			case -EOVERFLOW:
				#ifdef DBG_CONFIG_ERROR_DETECT	
				{	
					HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
					pHalData->srestpriv.Wifi_Error_Status = USB_READ_PORT_FAIL;			
				}
				#endif
				rtw_read_port(padapter, precvpriv->ff_hwaddr, 0, (unsigned char *)precvbuf);			
				break;
			case -EINPROGRESS:
				DBG_8192C("ERROR: URB IS IN PROGRESS!/n");
				break;
			default:
				break;				
		}
		
	}	

exit:	
	
_func_exit_;
}

u32 usb_read_port(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *rmem)
{		
	int err;
	unsigned int pipe;
	u32 ret = _SUCCESS;
	PURB purb = NULL;	
	struct recv_buf	*precvbuf = (struct recv_buf *)rmem;
	_adapter		*adapter = pintfhdl->padapter;
	struct dvobj_priv	*pdvobj = adapter_to_dvobj(adapter);
	struct pwrctrl_priv *pwrctl = dvobj_to_pwrctl(pdvobj);
	struct recv_priv	*precvpriv = &adapter->recvpriv;
	struct usb_device	*pusbd = pdvobj->pusbdev;

_func_enter_;
	
	if (RTW_CANNOT_RX(adapter) || (precvbuf == NULL))
	{
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port:( RTW_CANNOT_RX ) || precvbuf == NULL!!!\n"));
		return _FAIL;
	}

	usb_init_recvbuf(adapter, precvbuf);

	if(precvbuf->pbuf)
	{			
		ATOMIC_INC(&(precvpriv->rx_pending_cnt));
		purb = precvbuf->purb;		

		//translate DMA FIFO addr to pipehandle
		pipe = ffaddr2pipehdl(pdvobj, addr);	

		usb_fill_bulk_urb(purb, pusbd, pipe, 
					precvbuf->pbuf,
            				MAX_RECVBUF_SZ,
            				usb_read_port_complete,
            				precvbuf);//context is precvbuf

		purb->transfer_dma = precvbuf->dma_transfer_addr;
		purb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;								

		err = usb_submit_urb(purb, GFP_ATOMIC);	
		if((err) && (err != (-EPERM)))
		{
			RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("cannot submit rx in-token(err=0x%.8x), URB_STATUS =0x%.8x", err, purb->status));
			DBG_8192C("cannot submit rx in-token(err = 0x%08x),urb_status = %d\n",err,purb->status);
			ret = _FAIL;
		}
		
	}

_func_exit_;

	return ret;
}
#else	// CONFIG_USE_USB_BUFFER_ALLOC_RX

void usb_recv_tasklet(void *priv)
{
	_pkt			*pskb;
	_adapter		*padapter = (_adapter*)priv;
	struct recv_priv	*precvpriv = &padapter->recvpriv;
	struct recv_buf	*precvbuf = NULL;
	
	while (NULL != (pskb = skb_dequeue(&precvpriv->rx_skb_queue)))
	{
		if ((padapter->bDriverStopped == _TRUE)||(padapter->bSurpriseRemoved== _TRUE))
		{
			DBG_8192C("recv_tasklet => bDriverStopped or bSurpriseRemoved \n");
#ifdef CONFIG_PREALLOC_RX_SKB_BUFFER
			if(rtw_free_skb_premem(pskb)!=0)
#endif //CONFIG_PREALLOC_RX_SKB_BUFFER

			rtw_skb_free(pskb);
			break;
		}
	
		recvbuf2recvframe(padapter, pskb);

#ifdef CONFIG_PREALLOC_RECV_SKB

		skb_reset_tail_pointer(pskb);

		pskb->len = 0;
		
		skb_queue_tail(&precvpriv->free_recv_skb_queue, pskb);
		
#else
		rtw_skb_free(pskb);
#endif
		if (NULL != (precvbuf = rtw_dequeue_recvbuf(&precvpriv->recv_buf_pending_queue))) {
			precvbuf->pskb = NULL;
			precvbuf->reuse = _FALSE;
			rtw_read_port(padapter, precvpriv->ff_hwaddr, 0, (unsigned char *)precvbuf);
		}
	}
}


void usb_read_port_complete(struct urb *purb, struct pt_regs *regs)
{
	_irqL irqL;
	uint isevt, *pbuf;
	struct recv_buf	*precvbuf = (struct recv_buf *)purb->context;	
	_adapter 			*padapter =(_adapter *)precvbuf->adapter;
	struct recv_priv	*precvpriv = &padapter->recvpriv;	
	
	RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port_complete!!!\n"));
		
	ATOMIC_DEC(&(precvpriv->rx_pending_cnt));
	
	if(RTW_CANNOT_RX(padapter))
	{
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port_complete:bDriverStopped(%d) OR bSurpriseRemoved(%d)\n", padapter->bDriverStopped, padapter->bSurpriseRemoved));		
		
	#ifdef CONFIG_PREALLOC_RECV_SKB
		precvbuf->reuse = _TRUE;
	#else
		if(precvbuf->pskb){
			DBG_8192C("==> free skb(%p)\n",precvbuf->pskb);
			rtw_skb_free(precvbuf->pskb);
		}	
	#endif
		DBG_8192C("%s() RX Warning! bDriverStopped(%d) OR bSurpriseRemoved(%d) \n", 
		__FUNCTION__,padapter->bDriverStopped, padapter->bSurpriseRemoved);
		goto exit;
	}

	if(purb->status==0)//SUCCESS
	{
		if ((purb->actual_length > MAX_RECVBUF_SZ) || (purb->actual_length < RXDESC_SIZE))
		{
			RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port_complete: (purb->actual_length > MAX_RECVBUF_SZ) || (purb->actual_length < RXDESC_SIZE)\n"));
			precvbuf->reuse = _TRUE;
			rtw_read_port(padapter, precvpriv->ff_hwaddr, 0, (unsigned char *)precvbuf);
			DBG_8192C("%s()-%d: RX Warning!\n", __FUNCTION__, __LINE__);	
		}
		else 
		{	
			rtw_reset_continual_io_error(adapter_to_dvobj(padapter));
			
			precvbuf->transfer_len = purb->actual_length;			
			skb_put(precvbuf->pskb, purb->actual_length);	
			skb_queue_tail(&precvpriv->rx_skb_queue, precvbuf->pskb);
#ifndef CONFIG_FIX_NR_BULKIN_BUFFER
			if (skb_queue_len(&precvpriv->rx_skb_queue)<=1)
#endif
				tasklet_schedule(&precvpriv->recv_tasklet);

			precvbuf->pskb = NULL;
			precvbuf->reuse = _FALSE;
			rtw_read_port(padapter, precvpriv->ff_hwaddr, 0, (unsigned char *)precvbuf);			
		}		
	}
	else
	{
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port_complete : purb->status(%d) != 0 \n", purb->status));
	
		DBG_8192C("###=> usb_read_port_complete => urb status(%d)\n", purb->status);

		if(rtw_inc_and_chk_continual_io_error(adapter_to_dvobj(padapter)) == _TRUE ){
			padapter->bSurpriseRemoved = _TRUE;
		}

		switch(purb->status) {
			case -EINVAL:
			case -EPIPE:			
			case -ENODEV:
			case -ESHUTDOWN:
				//padapter->bSurpriseRemoved=_TRUE;
				//RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port_complete:bSurpriseRemoved=TRUE\n"));
			case -ENOENT:
				padapter->bDriverStopped=_TRUE;			
				RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port_complete:bDriverStopped=TRUE\n"));
				break;
			case -EPROTO:
			case -EILSEQ:
			case -ETIME:
			case -ECOMM:
			case -EOVERFLOW:
				#ifdef DBG_CONFIG_ERROR_DETECT	
				{	
					HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
					pHalData->srestpriv.Wifi_Error_Status = USB_READ_PORT_FAIL;			
				}
				#endif
				precvbuf->reuse = _TRUE;
				rtw_read_port(padapter, precvpriv->ff_hwaddr, 0, (unsigned char *)precvbuf);			
				break;
			case -EINPROGRESS:
				DBG_8192C("ERROR: URB IS IN PROGRESS!/n");
				break;
			default:
				break;				
		}
		
	}	

exit:	
	
_func_exit_;
	
}

u32 usb_read_port(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *rmem)
{
	_irqL irqL;
	int err;
	unsigned int pipe;
	SIZE_PTR tmpaddr=0;
	SIZE_PTR alignment=0;
	u32 ret = _SUCCESS;
	PURB purb = NULL;
	struct recv_buf	*precvbuf = (struct recv_buf *)rmem;
	_adapter		*adapter = pintfhdl->padapter;
	struct dvobj_priv	*pdvobj = adapter_to_dvobj(adapter);
	struct pwrctrl_priv *pwrctl = dvobj_to_pwrctl(pdvobj);
	struct recv_priv	*precvpriv = &adapter->recvpriv;
	struct usb_device	*pusbd = pdvobj->pusbdev;
	

_func_enter_;
	
	if (RTW_CANNOT_RX(adapter) || (precvbuf == NULL))
	{
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port:( RTW_CANNOT_RX ) || precvbuf == NULL!!!\n"));
		return _FAIL;
	}

#ifdef CONFIG_PREALLOC_RECV_SKB
	if((precvbuf->reuse == _FALSE) || (precvbuf->pskb == NULL))
	{
		if (NULL != (precvbuf->pskb = skb_dequeue(&precvpriv->free_recv_skb_queue)))
		{
			precvbuf->reuse = _TRUE;
		}
	}
#endif

	usb_init_recvbuf(adapter, precvbuf);		

	//re-assign for linux based on skb
	if((precvbuf->reuse == _FALSE) || (precvbuf->pskb == NULL))
	{
#ifndef CONFIG_FIX_NR_BULKIN_BUFFER
		precvbuf->pskb = rtw_skb_alloc(MAX_RECVBUF_SZ + RECVBUFF_ALIGN_SZ);
#endif
		if(precvbuf->pskb == NULL)		
		{
			if (0)
			DBG_8192C("usb_read_port() enqueue precvbuf=%p \n", precvbuf);
			//enqueue precvbuf and wait for free skb
			rtw_enqueue_recvbuf(precvbuf, &precvpriv->recv_buf_pending_queue);
			return _FAIL;
		}

		tmpaddr = (SIZE_PTR)precvbuf->pskb->data;
        	alignment = tmpaddr & (RECVBUFF_ALIGN_SZ-1);
		skb_reserve(precvbuf->pskb, (RECVBUFF_ALIGN_SZ - alignment));

		precvbuf->phead = precvbuf->pskb->head;
	   	precvbuf->pdata = precvbuf->pskb->data;
		precvbuf->ptail = skb_tail_pointer(precvbuf->pskb);
		precvbuf->pend = skb_end_pointer(precvbuf->pskb);
		precvbuf->pbuf = precvbuf->pskb->data;
	}	
	else//reuse skb
	{
		precvbuf->phead = precvbuf->pskb->head;
		precvbuf->pdata = precvbuf->pskb->data;
		precvbuf->ptail = skb_tail_pointer(precvbuf->pskb);
		precvbuf->pend = skb_end_pointer(precvbuf->pskb);
   		precvbuf->pbuf = precvbuf->pskb->data;

		precvbuf->reuse = _FALSE;
	}

	ATOMIC_INC(&(precvpriv->rx_pending_cnt));
	
	purb = precvbuf->purb;

	//translate DMA FIFO addr to pipehandle
	pipe = ffaddr2pipehdl(pdvobj, addr);

	usb_fill_bulk_urb(purb, pusbd, pipe, 
					precvbuf->pbuf,
            				MAX_RECVBUF_SZ,
            				usb_read_port_complete,
            				precvbuf);//context is precvbuf

	err = usb_submit_urb(purb, GFP_ATOMIC);
	if((err) && (err != (-EPERM)))
	{
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("cannot submit rx in-token(err=0x%.8x), URB_STATUS =0x%.8x", err, purb->status));
		DBG_8192C("cannot submit rx in-token(err = 0x%08x),urb_status = %d\n",err,purb->status);
		ret = _FAIL;
	}

_func_exit_;

	return ret;
}
#endif	// CONFIG_USE_USB_BUFFER_ALLOC_RX


