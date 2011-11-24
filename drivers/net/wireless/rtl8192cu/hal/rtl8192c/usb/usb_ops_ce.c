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
#define _HCI_OPS_OS_C_

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <osdep_intf.h>

#ifndef PLATFORM_OS_CE
    #error "PLATFORM_OS_CE shall be set \n"
#endif

#ifndef CONFIG_USB_HCI
	#error "CONFIG_USB_HCI shall be on!\n"
#endif

#include <usb_ops.h>
#include <recv_osdep.h>

#include <circ_buf.h>


struct zero_bulkout_context
{
	void *pbuf;
	void *purb;
	void *pirp;
	void *padapter;
};



#define PUSB_ERROR LPDWORD
#define USBD_HALTED(Status) ((ULONG)(Status) >> 30 == 3)


USB_PIPE ffaddr2pipehdl(struct dvobj_priv *pNdisCEDvice, u32 addr);


static NTSTATUS usb_async_interrupt_in_complete( LPVOID Context );
static NTSTATUS usb_async_interrupt_out_complete( LPVOID Context );

DWORD usb_write_port_complete( LPVOID Context );
DWORD usb_read_port_complete( LPVOID Context );

void usb_read_mem(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *rmem)
{
_func_enter_;
	RT_TRACE( _module_hci_ops_os_c_, _drv_info_, ("%s(%u)\n",__FUNCTION__, __LINE__));
_func_exit_;
}



BOOL
CloseTransferHandle(
    LPCUSB_FUNCS   pUsbFuncs,
    USB_TRANSFER   hTransfer
    )
{
    BOOL bRc = TRUE;

    // This assert may fail on suprise remove,
    // but should pass during normal I/O.
    // ASSERT( pUsbFuncs->lpIsTransferComplete(hTransfer) ); 

    // CloseTransfer aborts any pending transfers
    if ( !pUsbFuncs->lpCloseTransfer(hTransfer) ) {
     
	RT_TRACE( _module_hci_ops_os_c_, _drv_err_, ("*** CloseTransfer ERROR:%d ***\n", GetLastError()));	
        bRc = FALSE;
    }

    return bRc;
}


BOOL
GetTransferStatus(
    LPCUSB_FUNCS   pUsbFuncs,
    USB_TRANSFER   hTransfer,
    LPDWORD        pBytesTransferred , // OPTIONAL returns number of bytes transferred
    PUSB_ERROR     pUsbError  	 // returns USB error code
    )
{

    BOOL bRc = TRUE;

    if ( pUsbFuncs->lpGetTransferStatus(hTransfer, pBytesTransferred, pUsbError) ) {
        if ( USB_NO_ERROR != *pUsbError ) {
		RT_TRACE( _module_hci_ops_os_c_, _drv_err_, ("*** CloseTransfer ERROR:%d ***\n", GetLastError()));		
            RT_TRACE( _module_hci_ops_os_c_, _drv_err_, ("GetTransferStatus (BytesTransferred:%d, UsbError:0x%x)\n", pBytesTransferred?*pBytesTransferred:-1, pUsbError?*pUsbError:-1 )); 
        }
    } else {
        RT_TRACE( _module_hci_ops_os_c_, _drv_err_,("*** GetTransferStatus ERROR:%d ***\n", GetLastError())); 
        *pUsbError = USB_CANCELED_ERROR;
        bRc = FALSE;
    }

    return bRc;
}


// The driver should never read RxCmd register. We have to set
// RCR CMDHAT0 (bit6) to append Rx status before the Rx frame.
//
// |<--------  pBulkUrb->TransferBufferLength  ------------>|
// +------------------+-------------------+------------+
//  | Rx status (16 bytes)  | Rx frame .....             | CRC(4 bytes) |
// +------------------+-------------------+------------+
// ^
// ^pRfd->Buffer.VirtualAddress
//
/*! \brief USB RX IRP Complete Routine.
	@param Context pointer of RT_RFD
*/
u32 usb_read_port(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *rmem)
{
    struct intf_priv	*pintfpriv		= pintfhdl->pintfpriv;
    struct dvobj_priv	*pdvobj_priv	= (struct dvobj_priv*)pintfpriv->intf_dev;
    _adapter			*adapter		= (_adapter *)pdvobj_priv->padapter;

	struct recv_priv	*precvpriv	= &adapter->recvpriv;

	struct recv_buf		*precvbuf	= (struct recv_buf *)rmem;
	DWORD dwErr = ERROR_SUCCESS ;
	DWORD dwBytesTransferred = 0 ;
	USB_TRANSFER hTransfer = NULL;
	USB_PIPE	hPipe;
	LPCUSB_FUNCS usb_funcs_vp = pdvobj_priv->usb_extension._lpUsbFuncs;

_func_enter_;
	RT_TRACE( _module_hci_ops_os_c_, _drv_info_, ("usb_read_port(%u)\n", __LINE__));

#if (CONFIG_PWRCTRL == 1)
    if (adapter->pwrctrlpriv.pnp_bstop_trx)
	{
        return _FALSE;
    }
#endif

	if(adapter->bDriverStopped || adapter->bSurpriseRemoved) 
	{
		RT_TRACE(_module_hci_ops_os_c_, _drv_info_,("usb_read_port:( padapter->bDriverStopped ||padapter->bSurpriseRemoved)!!!\n"));
		return _FALSE;
	}

	if(precvbuf !=NULL)
	{

		// get a recv buffer
		rtl8192cu_init_recvbuf(adapter, precvbuf);
	


		_rtw_spinlock(&precvpriv->lock);
		precvpriv->rx_pending_cnt++;
		precvbuf->irp_pending = _TRUE;
		_rtw_spinunlock(&precvpriv->lock);


		//translate DMA FIFO addr to pipehandle
		hPipe = ffaddr2pipehdl(pdvobj_priv, addr);
		

		RT_TRACE( _module_hci_ops_os_c_, _drv_info_, ("usb_read_port(%u)\n", __LINE__));

		precvbuf->usb_transfer_read_port = (*usb_funcs_vp->lpIssueBulkTransfer)(
			hPipe,
			usb_read_port_complete,
			precvbuf,
			USB_IN_TRANSFER|USB_SHORT_TRANSFER_OK,
			MAX_RECVBUF_SZ,
			precvbuf->pbuf,
			0);


		if(precvbuf->usb_transfer_read_port)
		{
			
		//	  GetTransferStatus(usb_funcs_vp, hTransfer, &dwBytesTransferred,&UsbRc);

		//	  CloseTransferHandle(usb_funcs_vp, hTransfer);

		}
		else
		{

			dwErr = GetLastError();
			//RT_TRACE( _module_hci_ops_os_c_, _drv_err_, ("usb_read_port ERROR : %d\n", dwErr));		 

		}

//	 	if (  USB_NO_ERROR != UsbRc && ERROR_SUCCESS == dwErr) {
//	        dwErr = ERROR_GEN_FAILURE;
//	    }


		if ( ERROR_SUCCESS != dwErr ) {

			SetLastError(dwErr);
			RT_TRACE( _module_hci_ops_os_c_, _drv_err_, ("usb_read_port ERROR : %d\n", dwErr));	
		}
	
		RT_TRACE( _module_hci_ops_os_c_, _drv_info_, ("-usb_read_port(%u)\n", __LINE__));

	}
	else // if(precvbuf !=NULL)
	{
		
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port:precv_frame ==NULL\n"));
	}

	return _TRUE;

}

DWORD usb_read_port_complete( PVOID context )
{
	struct recv_buf 	*precvbuf	= (struct recv_buf *)context;
	_adapter			*adapter	= (_adapter *)precvbuf->adapter;
	struct recv_priv	*precvpriv	= &adapter->recvpriv;


	struct intf_hdl		*pintfhdl = &adapter->pio_queue->intf;
    struct intf_priv	*pintfpriv    = pintfhdl->pintfpriv;
    struct dvobj_priv	*pdvobj_priv  = (struct dvobj_priv*)pintfpriv->intf_dev;


	LPCUSB_FUNCS usb_funcs_vp = pdvobj_priv->usb_extension._lpUsbFuncs;

    DWORD dwBytesTransferred    = 0;
    DWORD dwErr                 = USB_CANCELED_ERROR;

	uint isevt, *pbuf;
	int fComplete =_FALSE;


	RT_TRACE( _module_hci_ops_os_c_, _drv_info_, ("usb_read_port_complete(%u)\n", __LINE__));

_func_enter_;


	_rtw_spinlock_ex(&precvpriv->lock);
	precvbuf->irp_pending=_FALSE;
	precvpriv->rx_pending_cnt --;
	_rtw_spinunlock_ex(&precvpriv->lock);	


#if 1
		
	(*usb_funcs_vp->lpGetTransferStatus)(precvbuf->usb_transfer_read_port, &dwBytesTransferred, &dwErr);
	fComplete = (*usb_funcs_vp->lpIsTransferComplete)(precvbuf->usb_transfer_read_port);
	if(fComplete!=_TRUE)
	{
		RT_TRACE( _module_hci_ops_os_c_, _drv_err_, ("usb_read_port_complete CloseTransfer before complete\n"));
	}
	(*usb_funcs_vp->lpCloseTransfer)(precvbuf->usb_transfer_read_port);
	
	
#endif


	if(USB_NO_ERROR != dwErr)
		RT_TRACE( _module_hci_ops_os_c_, _drv_err_, ("usb_read_port_complete Fail :%d\n",dwErr));
	
	{

		if ( dwBytesTransferred > MAX_RECVBUF_SZ || dwBytesTransferred < RXDESC_SIZE )
		{
			RT_TRACE(_module_hci_ops_os_c_,_drv_err_,
				("\n usb_read_port_complete: (pbulkurb->TransferBufferLength > MAX_RECVBUF_SZ) || (pbulkurb->TransferBufferLength < RXDESC_SIZE): %d\n",dwBytesTransferred));
			rtw_read_port(adapter, precvpriv->ff_hwaddr, 0, (unsigned char *)precvbuf);

    	    //usb_read_port(pintfhdl, 0, 0, (unsigned char *)precvframe);
    	}
		else
		{
			precvbuf->transfer_len = dwBytesTransferred;

			pbuf = (uint*)precvbuf->pbuf;

			if((isevt = *(pbuf+1)&0x1ff) == 0x1ff)
			{
				RT_TRACE(_module_hci_ops_os_c_,_drv_info_,
					("\n usb_read_port_complete: get a event\n"));
				rxcmd_event_hdl(adapter, pbuf);//rx c2h events

				rtw_read_port(adapter, precvpriv->ff_hwaddr, 0, (unsigned char *)precvbuf);
			}
			else
			{
				if(recvbuf2recvframe(adapter, precvbuf)==_FAIL)//rx packets
				{
					//precvbuf->reuse = _TRUE;		
					rtw_read_port(adapter, precvpriv->ff_hwaddr, 0, (unsigned char *)precvbuf);
				}
			}
	    }
	}

	RT_TRACE( _module_hci_ops_os_c_, _drv_info_, ("-usb_read_port_complete(%u)\n", __LINE__));

_func_exit_;
	return ERROR_SUCCESS;
//	return STATUS_MORE_PROCESSING_REQUIRED;
}

void usb_read_port_cancel(_adapter *padapter){
	RT_TRACE( _module_hci_ops_os_c_, _drv_info_, ("usb_read_port_cancel(%u)\n",__FUNCTION__, __LINE__));
}

DWORD usb_write_mem_complete( LPVOID Context )
{
	int fComplete =_FALSE;
	DWORD	dwBytes 	= 0;
	DWORD	dwErr		= USB_CANCELED_ERROR;

	_irqL irqL;
	_list	*head;
	_list *plist;
	struct io_req	*pio_req;	
	struct io_queue *pio_q = (struct io_queue *) Context;
	struct intf_hdl *pintf = &(pio_q->intf);	
	struct intf_priv *pintfpriv = pintf->pintfpriv;	
	_adapter *padapter = (_adapter *)pintf->adapter;
	NTSTATUS status = STATUS_SUCCESS;
    struct xmit_priv * pxmitpriv	= &padapter->xmitpriv;

	struct dvobj_priv * pdvobj_priv	= (struct dvobj_priv*)pintfpriv->intf_dev;

    USB_HANDLE		usbHandle		= pdvobj_priv->usb_extension._hDevice;
    LPCUSB_FUNCS	usb_funcs_vp	= pdvobj_priv->usb_extension._lpUsbFuncs;

	// get the head from the processing io_queue
	head = &(pio_q->processing);
	
_func_enter_;
	RT_TRACE( _module_hci_ops_os_c_, _drv_info_, ("+usb_write_mem_complete %p\n", Context));

#if 1
	_enter_critical_bh(&(pio_q->lock), &irqL);
	

	//free irp in processing list...	
	while(rtw_is_list_empty(head) != _TRUE)
	{
		plist = get_next(head);	
		rtw_list_delete(plist);
		pio_req = LIST_CONTAINOR(plist, struct io_req, list);
		_rtw_up_sema(&pio_req->sema);
	}

	_exit_critical_bh(&(pio_q->lock), &irqL);
#endif


#if 1
		
	(*usb_funcs_vp->lpGetTransferStatus)(pio_req->usb_transfer_write_mem , &dwBytes, &dwErr);
	fComplete = (*usb_funcs_vp->lpIsTransferComplete)(pio_req->usb_transfer_write_mem);
	if(fComplete!=_TRUE)
	{
		RT_TRACE( _module_hci_ops_os_c_, _drv_err_, ("usb_write_mem_complete CloseTransfer before complete\n"));
	}
	(*usb_funcs_vp->lpCloseTransfer)(pio_req->usb_transfer_write_mem );
	
#endif
	
	RT_TRACE( _module_hci_ops_os_c_, _drv_info_, ("-usb_write_mem_complete\n"));

_func_exit_;


	return STATUS_MORE_PROCESSING_REQUIRED;

}


void usb_write_mem(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *wmem)
{

	NTSTATUS NtStatus = STATUS_SUCCESS;
    USB_PIPE	hPipe;
	_irqL	irqL;

	int 	fComplete	= _FALSE;
	DWORD	dwBytes 	= 0;
	DWORD	dwErr		= USB_CANCELED_ERROR;


	struct io_req 		*pio_req;

	_adapter 			*adapter 	= (_adapter *)pintfhdl->adapter;
	struct intf_priv 	*pintfpriv	= pintfhdl->pintfpriv;
	struct dvobj_priv   * pdvobj_priv   = (struct dvobj_priv*)pintfpriv->intf_dev;

	 
    struct xmit_priv	*pxmitpriv	= &adapter->xmitpriv;
	struct io_queue 	*pio_queue 	= (struct io_queue *)adapter->pio_queue; 

	LPCUSB_FUNCS usb_funcs_vp = pdvobj_priv->usb_extension._lpUsbFuncs;


_func_enter_;
	RT_TRACE( _module_hci_ops_os_c_, _drv_info_, ("usb_write_mem(%u) pintfhdl %p wmem %p\n", __LINE__, pintfhdl, wmem));

	// fetch a io_request from the io_queue
	pio_req = alloc_ioreq(pio_queue);
		
	if ((pio_req == NULL)||(adapter->bSurpriseRemoved))
	{
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("async_irp_write32 : pio_req =0x%x adapter->bSurpriseRemoved=0x%x",pio_req,adapter->bSurpriseRemoved ));
		goto exit;
	}	

	_enter_critical_bh(&(pio_queue->lock), &irqL);


	// insert the io_request into processing io_queue
	rtw_list_insert_tail(&(pio_req->list),&(pio_queue->processing));
	
	
	if((adapter->bDriverStopped) || (adapter->bSurpriseRemoved) ||(adapter->pwrctrlpriv.pnp_bstop_trx)) 
	{
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("\npadapter->pwrctrlpriv.pnp_bstop_trx==_TRUE\n"));
		goto exit;
	}
	
	//translate DMA FIFO addr to pipehandle
	hPipe = ffaddr2pipehdl(pdvobj_priv, addr);	

	RT_TRACE( _module_hci_ops_os_c_, _drv_info_,("usb_write_mem(%u)\n",__LINE__));

	pio_req->usb_transfer_write_mem = (*usb_funcs_vp->lpIssueBulkTransfer)(
		hPipe,
		usb_write_mem_complete, 
		pio_queue,
		USB_OUT_TRANSFER,
		cnt,
		wmem,
		0);

#if 0

	(*usb_funcs_vp->lpGetTransferStatus)(pio_req->usb_transfer_write_mem , &dwBytes, &dwErr);

	while( fComplete != _TRUE)
	{
		fComplete = (*usb_funcs_vp->lpIsTransferComplete)(pio_req->usb_transfer_write_mem);
		if(fComplete==_TRUE)
		{
			(*usb_funcs_vp->lpCloseTransfer)(pio_req->usb_transfer_write_mem );
			RT_TRACE( _module_hci_ops_os_c_, _drv_err_, ("usb_write_mem finished\n"));
			break;
		}
		else
		{
			RT_TRACE( _module_hci_ops_os_c_, _drv_err_, 
				("usb_write_mem not yet finished %X\n", 
				pio_req->usb_transfer_write_mem));
			rtw_msleep_os(10);
		}
		
	}

#endif


//	_rtw_down_sema(&pio_req->sema);	

	RT_TRACE( _module_hci_ops_os_c_, _drv_info_, ("-usb_write_mem(%X)\n",pio_req->usb_transfer_write_mem));

	_exit_critical_bh(&(pio_queue->lock), &irqL);

	_rtw_down_sema(&pio_req->sema); 
	free_ioreq(pio_req, pio_queue);

exit:
_func_exit_;
	return;
}

u32 usb_write_cnt=0;
u32 usb_complete_cnt=0;

USB_PIPE ffaddr2pipehdl(struct dvobj_priv *pNdisCEDvice, u32 addr)
{
	USB_PIPE	PipeHandle = NULL;
	_adapter	*padapter = pNdisCEDvice->padapter;

	
	if(pNdisCEDvice->nr_endpoint == 11)
	{		
		switch(addr)
		{	    
	     		case RTL8712_DMA_BEQ:
		 		PipeHandle= padapter->halpriv.pipehdls_r8712[3] ; 
				break;
	     		case RTL8712_DMA_BKQ:
			 	PipeHandle=  padapter->halpriv.pipehdls_r8712[4]; 
				break;
	     		case RTL8712_DMA_VIQ:
		 		PipeHandle=  padapter->halpriv.pipehdls_r8712[2]; 
				break;
	    		case RTL8712_DMA_VOQ:
				PipeHandle=  padapter->halpriv.pipehdls_r8712[1]; 
				break;					
                     case RTL8712_DMA_BCNQ:	
				PipeHandle=  padapter->halpriv.pipehdls_r8712[6]; 
				break;	 	
			case RTL8712_DMA_BMCQ:	//HI Queue
				PipeHandle=  padapter->halpriv.pipehdls_r8712[7]; 
				break;	
			case RTL8712_DMA_MGTQ:				
		 		PipeHandle=  padapter->halpriv.pipehdls_r8712[8]; 
				break;
                     case RTL8712_DMA_RX0FF:
				PipeHandle=  padapter->halpriv.pipehdls_r8712[0]; 
				break;	 	
			case RTL8712_DMA_C2HCMD:		 	
				PipeHandle=  padapter->halpriv.pipehdls_r8712[5]; 
				break;
			case RTL8712_DMA_H2CCMD:
				PipeHandle=  padapter->halpriv.pipehdls_r8712[9]; 
				break;	
				
		}

	}
	else if(pNdisCEDvice->nr_endpoint == 6)
	{
		switch(addr)
		{	    
			case RTL8712_DMA_BEQ:
		 		PipeHandle=  padapter->halpriv.pipehdls_r8712[3]; 
				break;
	     	case RTL8712_DMA_BKQ:
			 	PipeHandle=  padapter->halpriv.pipehdls_r8712[4]; 
				break;
	     	case RTL8712_DMA_VIQ:
		 		PipeHandle=  padapter->halpriv.pipehdls_r8712[2]; 
				break;
	    	case RTL8712_DMA_VOQ:                   		
		 		PipeHandle=  padapter->halpriv.pipehdls_r8712[1]; 
				break;
			case RTL8712_DMA_RX0FF:
			case RTL8712_DMA_C2HCMD:		 	
				PipeHandle=  padapter->halpriv.pipehdls_r8712[0]; 
				break;
			case RTL8712_DMA_H2CCMD:
			case RTL8712_DMA_BCNQ:					
			case RTL8712_DMA_BMCQ:	
			case RTL8712_DMA_MGTQ:			
				PipeHandle=  padapter->halpriv.pipehdls_r8712[5]; 
				break;	
				
		}

	}
	else if(pNdisCEDvice->nr_endpoint == 4)
	{
		switch(addr)
		{	    
	     	case RTL8712_DMA_BEQ:
		 	case RTL8712_DMA_BKQ:
			 	PipeHandle=  padapter->halpriv.pipehdls_r8712[2]; 
				break;
     		case RTL8712_DMA_VIQ:
		 	case RTL8712_DMA_VOQ:					
		 		PipeHandle=  padapter->halpriv.pipehdls_r8712[1]; 
				break;
			case RTL8712_DMA_RX0FF:
			case RTL8712_DMA_C2HCMD:		 	
				PipeHandle=  padapter->halpriv.pipehdls_r8712[0]; 
				break;
			case RTL8712_DMA_H2CCMD:	
			case RTL8712_DMA_BCNQ:					
			case RTL8712_DMA_BMCQ:	
			case RTL8712_DMA_MGTQ:				
				PipeHandle=  padapter->halpriv.pipehdls_r8712[3]; 
				break;	
		}
	
	}
	else
	{
	   RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("ffaddr2pipehdl():nr_endpoint=%d error!\n", pNdisCEDvice->nr_endpoint));	   
	}
		
	return PipeHandle;

}

DWORD usb_bulkout_zero_complete( LPVOID pZeroContext )
{
	struct zero_bulkout_context *pcontext = (struct zero_bulkout_context *)pZeroContext;
	_adapter * padapter = pcontext->padapter;
    struct dvobj_priv *	pdvobj_priv = (struct dvobj_priv *)&padapter->dvobjpriv;
	LPCUSB_FUNCS usb_funcs_vp = pdvobj_priv->usb_extension._lpUsbFuncs;
	struct xmit_priv  * pxmitpriv	= &padapter->xmitpriv;

	int 	fComplete			=_FALSE;
	DWORD	dwBytesTransferred	= 0;
	DWORD	dwErr				= USB_CANCELED_ERROR;

_func_enter_;

#if 1
				
	(*usb_funcs_vp->lpGetTransferStatus)(pxmitpriv->usb_transfer_write_port, &dwBytesTransferred, &dwErr);
	fComplete = (*usb_funcs_vp->lpIsTransferComplete)(pxmitpriv->usb_transfer_write_port);
	if(fComplete!=_TRUE)
	{
		RT_TRACE( _module_hci_ops_os_c_, _drv_err_, ("usb_bulkout_zero_complete CloseTransfer before complete\n"));
	}
	(*usb_funcs_vp->lpCloseTransfer)(pxmitpriv->usb_transfer_write_port);
	
#endif

	if(pcontext)
	{
		if(pcontext->pbuf)
		{			
			rtw_mfree(pcontext->pbuf, sizeof(int));	
		}	

		rtw_mfree((u8*)pcontext, sizeof(struct zero_bulkout_context));	
	}	

_func_exit_;

	return ERROR_SUCCESS;
	

}

u32 usb_bulkout_zero(struct intf_hdl *pintfhdl, u32 addr)
{	
	struct zero_bulkout_context *pcontext;
	unsigned char *pbuf;
	u8 len = 0 ;
	_adapter *padapter = (_adapter *)pintfhdl->adapter;
	struct dvobj_priv	*pdvobj = (struct dvobj_priv *)&padapter->dvobjpriv;	
	struct xmit_priv	* pxmitpriv     = &padapter->xmitpriv;
     

	LPCUSB_FUNCS usb_funcs_vp = pdvobj->usb_extension._lpUsbFuncs;

	USB_PIPE	hPipe;

_func_enter_;

	if((padapter->bDriverStopped) || (padapter->bSurpriseRemoved) ||(padapter->pwrctrlpriv.pnp_bstop_trx))
	{		
		return _FAIL;
	}


	pcontext = (struct zero_bulkout_context *)rtw_zmalloc(sizeof(struct zero_bulkout_context));

	pbuf = (unsigned char *)rtw_zmalloc(sizeof(int));	

	len = 0;
	
	pcontext->pbuf = pbuf;
	pcontext->purb = NULL;
	pcontext->pirp = NULL;
	pcontext->padapter = padapter;
                    

//translate DMA FIFO addr to pipehandle
	hPipe = ffaddr2pipehdl(pdvobj, addr);




	pxmitpriv->usb_transfer_write_port = (*usb_funcs_vp->lpIssueBulkTransfer)(
						        hPipe, usb_bulkout_zero_complete, 
						        pcontext, USB_OUT_TRANSFER,
					    	    len, pbuf, 0);

	
_func_exit_;
	
	return _SUCCESS;

}

u32 usb_write_port(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *wmem)
{

    u32	i, bwritezero = _FALSE;
	u32	ac_tag = addr;

    u8*	ptr;

	struct intf_priv    * pintfpriv     = pintfhdl->pintfpriv;
	struct dvobj_priv   * pdvobj_priv   = (struct dvobj_priv*)pintfpriv->intf_dev;
	_adapter            * padapter      = pdvobj_priv->padapter;

    struct xmit_priv	* pxmitpriv     = &padapter->xmitpriv;
    struct xmit_frame   * pxmitframe    = (struct xmit_frame *)wmem;

	LPCUSB_FUNCS usb_funcs_vp = pdvobj_priv->usb_extension._lpUsbFuncs;

    USB_PIPE	hPipe;

	u32			bResult = _FALSE;

_func_enter_;
	RT_TRACE( _module_hci_ops_os_c_, _drv_info_, ("+usb_write_port\n"));

#if (CONFIG_PWRCTRL == 1)
    if(padapter->pwrctrlpriv.pnp_bstop_trx==_TRUE){
       	RT_TRACE( _module_hci_ops_os_c_, _drv_err_, ("\npadapter->pwrctrlpriv.pnp_bstop_trx==_TRUE\n"));

    }
#endif

	if((padapter->bDriverStopped) || (padapter->bSurpriseRemoved) ||(padapter->pwrctrlpriv.pnp_bstop_trx))
	{
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_write_port:( padapter->bDriverStopped ||padapter->bSurpriseRemoved ||adapter->pwrctrlpriv.pnp_bstop_trx)!!!\n"));
		bResult = _FALSE;
		goto exit;
	}

	RT_TRACE( _module_hci_ops_os_c_, _drv_info_, ("usb_write_port(%u)\n", __LINE__));

	for(i=0; i<8; i++)
	{
		if(pxmitframe->bpending[i] == _FALSE)
		{
			_rtw_spinlock(&pxmitpriv->lock);	
			pxmitpriv->txirp_cnt++;
			pxmitframe->bpending[i]  = _TRUE;
			_rtw_spinunlock(&pxmitpriv->lock);
			
			pxmitframe->sz[i] = cnt;
			pxmitframe->ac_tag[i] = ac_tag;

			break;
		}
	}	


	//TODO:
	if (pdvobj_priv->ishighspeed)
	{
		if(cnt> 0 && cnt%512 == 0)
		{
			RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("ishighspeed, cnt=%d\n", cnt));
		//	cnt=cnt+1;
			bwritezero = _TRUE;

		}	
	}
	else
	{
		if(cnt > 0 && cnt%64 == 0)
		{
			RT_TRACE(_module_hci_ops_os_c_,_drv_info_,("cnt=%d\n", cnt));
		//	cnt=cnt+1;
			bwritezero = _TRUE;

		}	
	}

	RT_TRACE( _module_hci_ops_os_c_, _drv_info_, ("usb_write_port: pipe handle convert\n"));

	//translate DMA FIFO addr to pipehandle
	hPipe = ffaddr2pipehdl(pdvobj_priv, addr);


#if 0
	// for tx fifo, the maximum payload number is 8,
	// we workaround this issue here by separate whole fifo into 8 segments.
	if (cnt <= 500)
		cnt = 500;
#endif

	RT_TRACE( _module_hci_ops_os_c_, _drv_info_,
		("usb_write_port(%u): pxmitframe %X  pxmitframe->padapter %X\n",__LINE__, pxmitframe, pxmitframe->padapter));

	pxmitpriv->usb_transfer_write_port = (*usb_funcs_vp->lpIssueBulkTransfer)(
						        hPipe, usb_write_port_complete, 
						        pxmitframe, USB_OUT_TRANSFER,
					    	    cnt, pxmitframe->mem_addr, 0);

	RT_TRACE( _module_hci_ops_os_c_, _drv_info_, ("%s(%u)\n",__FUNCTION__, __LINE__));

	ptr=(u8 *)&pxmitframe->mem;

#if 0
	if (pdvobj_priv->ishighspeed)
	{
		ptr=ptr+512;
	}
	else
	{
		ptr=ptr+64;

	}
#endif
	if(bwritezero == _TRUE)
	{
		usb_bulkout_zero(pintfhdl, addr);
	}

//	if (!pxmitframe->usb_transfer_xmit)
//	    padapter->bSurpriseRemoved=_TRUE;

	RT_TRACE( _module_hci_ops_os_c_, _drv_info_, ("%s(%u)\n",__FUNCTION__, __LINE__));
	bResult = _SUCCESS;

exit:
_func_exit_;
	return bResult;
}

DWORD usb_write_port_complete( LPVOID Context )
{

//    u8 *ptr;

    struct xmit_frame *	pxmitframe  = (struct xmit_frame *) Context;
    _adapter          * padapter    = pxmitframe->padapter;
    struct dvobj_priv *	pdvobj_priv = (struct dvobj_priv *)&padapter->dvobjpriv;
	struct xmit_priv  * pxmitpriv   = &padapter->xmitpriv;
	struct xmit_buf *pxmitbuf = pxmitframe->pxmitbuf;
	LPCUSB_FUNCS usb_funcs_vp = pdvobj_priv->usb_extension._lpUsbFuncs;

	int		fComplete			=_FALSE;
	DWORD	dwBytesTransferred	= 0;
	DWORD	dwErr				= USB_CANCELED_ERROR;

	RT_TRACE( _module_hci_ops_os_c_, _drv_info_, ("%s(%u), pxmitframe %X\n",__FUNCTION__, __LINE__, Context));

_func_enter_;

	RT_TRACE(_module_hci_ops_os_c_,_drv_info_,("+usb_write_port_complete\n"));

	_rtw_spinlock_ex(&pxmitpriv->lock);	
	pxmitpriv->txirp_cnt--;
	_rtw_spinunlock_ex(&pxmitpriv->lock);

	if(pxmitpriv->txirp_cnt==0){
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_write_port_complete: txirp_cnt== 0, set allrxreturnevt!\n"));		
		_rtw_up_sema(&(pxmitpriv->tx_retevt));
	}


	//not to consider tx fragment
	rtw_free_xmitframe_ex(pxmitpriv, pxmitframe);		


#if 1
			
	(*usb_funcs_vp->lpGetTransferStatus)(pxmitpriv->usb_transfer_write_port, &dwBytesTransferred, &dwErr);
	fComplete = (*usb_funcs_vp->lpIsTransferComplete)(pxmitpriv->usb_transfer_write_port);
	if(fComplete!=_TRUE)
	{
		RT_TRACE( _module_hci_ops_os_c_, _drv_err_, ("usb_write_port_complete CloseTransfer before complete\n"));
	}
	(*usb_funcs_vp->lpCloseTransfer)(pxmitpriv->usb_transfer_write_port);

#else

	if((*usb_funcs_vp->lpIsTransferComplete)(pxmitpriv->usb_transfer_write_port))
	{
		(*usb_funcs_vp->lpCloseTransfer)(pxmitpriv->usb_transfer_write_port);
	}

#endif

	RT_TRACE( _module_hci_ops_os_c_, _drv_info_, 
		("%s(%u): pxmitpriv %X pxmitpriv->free_xmitframe_cnt %X pxmitframe->padapter %X pxmitframe->padapter %X\n", 
		__LINE__, pxmitpriv, pxmitpriv->free_xmitframe_cnt, pxmitframe->padapter));

    rtl8192cu_xmitframe_complete(padapter, pxmitpriv, pxmitbuf);

_func_exit_;

    return STATUS_SUCCESS;
}

DWORD usb_write_scsi_complete(LPVOID pTxContext) 
{
#ifndef PLATFORM_OS_CE
	struct SCSI_BUFFER_ENTRY *psb_entry = (struct SCSI_BUFFER_ENTRY *)pTxContext;
	_adapter 				 *padapter 	= psb_entry->padapter;
	struct SCSI_BUFFER 		 *psb 		= padapter->pscsi_buf;
	struct xmit_priv 		 *pxmitpriv = &(padapter->xmitpriv);
    struct dvobj_priv 		*pdvobj_priv = (struct dvobj_priv *)&padapter->dvobjpriv;
	LPCUSB_FUNCS 		  	 lpUsbFuncs = pdvobj_priv->pUsbExtension->_lpUsbFuncs;

	int 	fComplete			=_FALSE;
	DWORD	dwBytesTransferred	= 0;
	DWORD	dwErr				= USB_CANCELED_ERROR;

_func_enter_;
	RT_TRACE( _module_hci_ops_os_c_, _drv_info_, ("%s(%u): circ_space = %d\n",__FUNCTION__, __LINE__, CIRC_SPACE( psb->head,psb->tail,  SCSI_BUFFER_NUMBER)));

#if 1
				
	(*lpUsbFuncs->lpGetTransferStatus)(psb_entry->usb_transfer_scsi_txcmd, &dwBytesTransferred, &dwErr);
	fComplete = (*lpUsbFuncs->lpIsTransferComplete)(psb_entry->usb_transfer_scsi_txcmd);
	if(fComplete!=_TRUE)
	{
		RT_TRACE( _module_hci_ops_os_c_, _drv_err_, ("usb_write_scsi_complete CloseTransfer before complete\n"));
	}
	(*lpUsbFuncs->lpCloseTransfer)(psb_entry->usb_transfer_scsi_txcmd);
	
#else

	if((*lpUsbFuncs->lpIsTransferComplete)(psb_entry->usb_transfer_scsi_txcmd))
		(*lpUsbFuncs->lpCloseTransfer)(psb_entry->usb_transfer_scsi_txcmd);
#endif

	memset(psb_entry->entry_memory, 0, 8);

	RT_TRACE( _module_hci_ops_os_c_, _drv_info_, ("%s(%u)\n",__FUNCTION__, __LINE__));
	if((psb->tail+1)==SCSI_BUFFER_NUMBER)
		psb->tail=0;
	else 
		psb->tail++;

	RT_TRACE( _module_hci_ops_os_c_, _drv_info_, ("%s(%u)\n",__FUNCTION__, __LINE__));
	if(CIRC_CNT(psb->head,psb->tail,SCSI_BUFFER_NUMBER)==0){
		RT_TRACE( _module_hci_ops_os_c_, _drv_info_, ("write_txcmd_scsififo_callback: up_sema\n"));
		_rtw_up_sema(&pxmitpriv->xmit_sema);
	}

	RT_TRACE( _module_hci_ops_os_c_, _drv_info_, ("%s(%u)\n",__FUNCTION__, __LINE__));
	if(padapter->bSurpriseRemoved) {
		return STATUS_MORE_PROCESSING_REQUIRED;
	}

_func_exit_;
#endif
	RT_TRACE( _module_hci_ops_os_c_, _drv_info_, ("%s(%u)\n",__FUNCTION__, __LINE__));
	return STATUS_MORE_PROCESSING_REQUIRED;
}

uint usb_write_scsi(struct intf_hdl *pintfhdl, u32 cnt, u8 *wmem)
{

#ifndef PLATFORM_OS_CE

	_adapter 		  *padapter = (_adapter *)pintfhdl->adapter;
	struct dvobj_priv *pdev 	= (struct dvobj_priv*)&padapter->dvobjpriv;

	struct SCSI_BUFFER       *psb      =padapter->pscsi_buf;
	struct SCSI_BUFFER_ENTRY *psb_entry=LIST_CONTAINOR(wmem,struct SCSI_BUFFER_ENTRY,entry_memory);

_func_enter_;
	if(padapter->bSurpriseRemoved||padapter->bDriverStopped)
		return 0;
	
	RT_TRACE( _module_hci_ops_os_c_, _drv_info_, ("%s(%u)\n",__FUNCTION__, __LINE__));
	psb_entry->usb_transfer_scsi_txcmd=pdev->pUsbExtension->_lpUsbFuncs->lpIssueBulkTransfer(
			pdev->scsi_out_pipehandle,
			usb_write_scsi_complete,
			psb_entry,
			USB_OUT_TRANSFER,
			cnt,
			wmem,
			0);
	
_func_exit_;
#endif

   return _SUCCESS;  
}


/*
 */
uint usb_init_intf_priv(struct intf_priv *pintfpriv)
{
	// get the dvobj_priv object
	struct dvobj_priv * pNdisCEDvice = (struct dvobj_priv *) pintfpriv->intf_dev;

	RT_TRACE( _module_hci_ops_os_c_, _drv_info_, ("%s(%u)\n",__FUNCTION__, __LINE__));
	// set init intf_priv init status as _IOREADY
	pintfpriv->intf_status = _IOREADY;

	//  determine the max io size by dvobj_priv.ishighspeed
	if(pNdisCEDvice->ishighspeed)
		pintfpriv->max_iosz =  128;
	else
		pintfpriv->max_iosz =  64;

	//  read/write size set as 0
	pintfpriv->io_wsz = 0;
	pintfpriv->io_rsz = 0;

	//  init io_rwmem buffer
	pintfpriv->allocated_io_rwmem = rtw_zmalloc(pintfpriv->max_iosz +4);
	if (pintfpriv->allocated_io_rwmem == NULL)
	{
		rtw_mfree((u8 *)(pintfpriv->allocated_io_rwmem), pintfpriv->max_iosz +4);
		return _FAIL;
	}
	else
	{
		// word align the io_rwmem
		pintfpriv->io_rwmem = pintfpriv->allocated_io_rwmem + 4 - ( (u32)(pintfpriv->allocated_io_rwmem) & 3);
	}

#ifndef PLATFORM_OS_CE

	//  init io_r_mem buffer
	pintfpriv->allocated_io_r_mem = rtw_zmalloc(pintfpriv->max_iosz +4);
	if (pintfpriv->allocated_io_r_mem == NULL)
	{
		rtw_mfree((u8 *)(pintfpriv->allocated_io_r_mem), pintfpriv->max_iosz +4);
		return _FAIL;
	}
	else
	{
		// word align the io_rwmem
		pintfpriv->io_r_mem = pintfpriv->allocated_io_r_mem + 4 - ( (u32)(pintfpriv->allocated_io_r_mem) & 3);
	}
#endif

	return _SUCCESS;
}

void usb_unload_intf_priv(struct intf_priv *pintfpriv)
{
#ifndef PLATFORM_OS_CE

	rtw_mfree((u8 *)(pintfpriv->allocated_io_rwmem), pintfpriv->max_iosz+4);
	rtw_mfree((u8 *)(pintfpriv->allocated_io_r_mem), pintfpriv->max_iosz+4);
#endif

	RT_TRACE( _module_hci_ops_os_c_, _drv_info_, ("%s(%u)\n",__FUNCTION__, __LINE__));
}



void usb_write_port_cancel(_adapter *padapter)
{

	sint i,j;
	struct dvobj_priv   *pdev = &padapter->dvobjpriv;
	struct xmit_priv *pxmitpriv=&padapter->xmitpriv;
	struct xmit_frame *pxmitframe;

	_rtw_spinlock(&pxmitpriv->lock);
	pxmitpriv->txirp_cnt--; //decrease 1 for Initialize ++
	_rtw_spinunlock(&pxmitpriv->lock);
	
	if (pxmitpriv->txirp_cnt) 
	{
		// Canceling Pending Recv Irp
		pxmitframe= (struct xmit_frame *)pxmitpriv->pxmit_frame_buf;
		
		for( i = 0; i < NR_XMITFRAME; i++ )
		{
			for(j=0;j<8;j++)
			{
				if (pxmitframe->bpending[j]==_TRUE)
				{			

					RT_TRACE(_module_hci_ops_os_c_,_drv_err_,(" usb_write_port_cancel() :IoCancelIrp\n"));

				}
			}
			
			pxmitframe++;
		}

		_rtw_down_sema(&(pxmitpriv->tx_retevt));
		
	}

}

DWORD usbctrl_vendorreq_complete(LPVOID lpvNotifyParameter)
{
	struct dvobj_priv  *pdvobjpriv = (struct dvobj_priv*)lpvNotifyParameter;

	RT_TRACE(_module_hci_ops_os_c_,_drv_debug_,("+usbctrl_vendorreq_complete\n"));
	
    return STATUS_SUCCESS;
}


int usbctrl_vendorreq(struct intf_priv *pintfpriv, u8 request, u16 value, u16 index, void *pdata, u16 len, u8 requesttype)
{
	u8			ret=_TRUE;
//	NTSTATUS	ntstatus;
//	int 		fComplete;
//	LPCUSB_DEVICE		lpDeviceInfo;

	struct dvobj_priv  *pdvobjpriv = (struct dvobj_priv  *)pintfpriv->intf_dev;   

    USB_TRANSFER        usbTrans;
    USB_DEVICE_REQUEST  usb_device_req;
    USB_HANDLE      usbHandle   = pdvobjpriv->usb_extension._hDevice;
    LPCUSB_FUNCS    usbFuncs    = pdvobjpriv->usb_extension._lpUsbFuncs;

	u32	transfer_flags = 0;

	_func_enter_;

    memset( &usb_device_req, 0, sizeof( USB_DEVICE_REQUEST ) );

    if( 0x01 == requesttype )
	{
        usb_device_req.bmRequestType = USB_REQUEST_DEVICE_TO_HOST | USB_REQUEST_VENDOR | USB_REQUEST_FOR_DEVICE;
    }
	else
    {
        usb_device_req.bmRequestType = USB_REQUEST_HOST_TO_DEVICE | USB_REQUEST_VENDOR | USB_REQUEST_FOR_DEVICE;
    }

	usb_device_req.bRequest 		= request;
	usb_device_req.wValue 			= value;
	usb_device_req.wIndex 	    	= index;
	usb_device_req.wLength 	    	= len;    

	if (requesttype == 0x01)
	{
		transfer_flags = USB_IN_TRANSFER;//read_in
	}
	else
	{
		transfer_flags= USB_OUT_TRANSFER;//write_out
	}

	RT_TRACE(_module_hci_ops_os_c_,_drv_debug_,("+usbctrl_vendorreq\n",__FUNCTION__,__LINE__));

#if 0
	// Remember to add callback for sync
	usbTrans = (*usbFuncs->lpIssueVendorTransfer)(usbHandle, 
							usbctrl_vendorreq_complete, pdvobjpriv, 
							transfer_flags, &usb_device_req, pdata, 0);
#else
	// Remember to add callback for sync
	usbTrans = (*usbFuncs->lpIssueVendorTransfer)(usbHandle, 
							NULL, 0, 
							transfer_flags, &usb_device_req, pdata, 0);
#endif

//	rtw_usleep_os(10);

	if ( usbTrans )
	{
		DWORD	dwBytes 	= 0;
		DWORD	dwErr		= USB_CANCELED_ERROR;
		int 	fComplete;

		(*usbFuncs->lpGetTransferStatus)(usbTrans, &dwBytes, &dwErr);

		fComplete = (*usbFuncs->lpIsTransferComplete)(usbTrans);

		if (fComplete== _TRUE)
		{
			(*usbFuncs->lpCloseTransfer)(usbTrans);
			RT_TRACE(_module_hci_ops_os_c_,_drv_debug_,("usbctrl_vendorreq lpCloseTransfer\n"));
		}

		if ( dwErr != USB_NO_ERROR || fComplete != _TRUE)
		{
			RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usbctrl_vendorreq lpCloseTransfer without complete\n"));
			ret = _FALSE;
			goto exit;
		}
	}
	else
	{
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usbctrl_vendorreq without usbTrans\n"));
		ret = _FALSE;
		goto exit;

	}

exit:
	RT_TRACE(_module_hci_ops_os_c_,_drv_debug_,("-usbctrl_vendorreq\n"));
_func_exit_;
	
	return ret;	

}


