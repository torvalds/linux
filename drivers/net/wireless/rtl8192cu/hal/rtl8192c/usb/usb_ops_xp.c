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
#include <circ_buf.h>

#if defined (PLATFORM_LINUX) && defined (PLATFORM_WINDOWS)
	#error "Shall be Linux or Windows, but not both!\n"
#endif

#ifndef CONFIG_USB_HCI
	#error "CONFIG_USB_HCI shall be on!\n"
#endif


#include <usb.h>
#include <usbdlib.h>
#include <usbioctl.h>

#include <usb_ops.h>
#include <recv_osdep.h>

#include <usb_osintf.h>


struct zero_bulkout_context
{
	void *pbuf;
	void *purb;
	void *pirp;
	void *padapter;
};

#define usb_write_cmd usb_write_mem 
#define usb_read_cmd usb_read_mem
#define usb_write_cmd_complete usb_write_mem_complete
//#define usb_read_cmd_complete usb_read_mem_complete



uint usb_init_intf_priv(struct intf_priv *pintfpriv)
{
	        
	PURB	piorw_urb;
	u8		NextDeviceStackSize;
	struct dvobj_priv   *pdev = (struct dvobj_priv   *)pintfpriv->intf_dev;
	_adapter * padapter=pdev->padapter;

_func_enter_;
	
	RT_TRACE(_module_hci_ops_os_c_,_drv_info_,("\n +usb_init_intf_priv\n"));

	pintfpriv->intf_status = _IOREADY;

       if(pdev->ishighspeed) pintfpriv->max_iosz =  128;
	else pintfpriv->max_iosz =  64;	


	_init_timer(&pintfpriv->io_timer, padapter->hndis_adapter, io_irp_timeout_handler, pintfpriv);

	
	RT_TRACE(_module_hci_ops_os_c_,_drv_info_,("usb_init_intf_priv:pintfpriv->max_iosz:%d\n",pintfpriv->max_iosz));

	pintfpriv->io_wsz = 0;
	pintfpriv->io_rsz = 0;	
	
 	pintfpriv->allocated_io_rwmem = rtw_zmalloc(pintfpriv->max_iosz +4); 
	
   	if (pintfpriv->allocated_io_rwmem == NULL){
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("\n usb_init_intf_priv:pintfpriv->allocated_io_rwmem == NULL\n"));
    		goto usb_init_intf_priv_fail;
   	}

	pintfpriv->io_rwmem = pintfpriv->allocated_io_rwmem +  4 \
					-( (u32)(pintfpriv->allocated_io_rwmem) & 3);
	

     
     NextDeviceStackSize = (u8)pdev->nextdevstacksz;//pintfpriv->pUsbDevObj->StackSize + 1; 

      piorw_urb = (PURB)ExAllocatePool(NonPagedPool, sizeof(URB) ); 
      if(piorw_urb == NULL) 
	  goto usb_init_intf_priv_fail;
	  
      pintfpriv->piorw_urb = piorw_urb;

      pintfpriv->piorw_irp = IoAllocateIrp(NextDeviceStackSize , FALSE);	 
    

      pintfpriv->io_irp_cnt=1;
      pintfpriv->bio_irp_pending=_FALSE;
	 
     _rtw_init_sema(&(pintfpriv->io_retevt), 0);//NdisInitializeEvent(&pintfpriv->io_irp_return_evt);

_func_exit_;
	return _SUCCESS;

usb_init_intf_priv_fail:

	if (pintfpriv->allocated_io_rwmem)
		rtw_mfree((u8 *)(pintfpriv->allocated_io_rwmem), pintfpriv->max_iosz +4);
	
	if(piorw_urb)
		ExFreePool(piorw_urb);	

	RT_TRACE(_module_hci_ops_os_c_,_drv_info_,("\n -usb_init_intf_priv(usb_init_intf_priv_fail)\n"));

_func_exit_;	
	return _FAIL;
		
}

void usb_unload_intf_priv(struct intf_priv *pintfpriv)
{

_func_enter_;
	
	RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("\n+usb_unload_intf_priv\n"));
	
	rtw_mfree((u8 *)(pintfpriv->allocated_io_rwmem), pintfpriv->max_iosz+4);
	
#ifdef PLATFORM_WINDOWS
	if(pintfpriv->piorw_urb)
		ExFreePool(pintfpriv->piorw_urb);	

	if(pintfpriv->piorw_irp)
		IoFreeIrp(pintfpriv->piorw_irp);
#endif		


#ifdef PLATFORM_LINUX
	RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("\npintfpriv->io_irp_cnt=%d\n",pintfpriv->io_irp_cnt));
	pintfpriv->io_irp_cnt--;
	if(pintfpriv->io_irp_cnt){
		if(pintfpriv->bio_irp_pending==_TRUE){
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("\nkill iorw_urb\n"));
		usb_kill_urb(pintfpriv->piorw_urb);
		}
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("\n wait io_retevt\n"));
		_rtw_down_sema(&(pintfpriv->io_retevt));
	}
	RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("\n cancel io_urb ok\n"));
#endif

	RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("\n-usb_unload_intf_priv\n"));

_func_exit_;
	
}

void *ffaddr2pipehdl(struct dvobj_priv *pNdisCEDvice, u32 addr)
{
	HANDLE PipeHandle = NULL;
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
		 	//case RTL8712_DMA_BKQ:
			 	PipeHandle=  padapter->halpriv.pipehdls_r8712[2]; 
				break;
	     		//case RTL8712_DMA_VIQ:
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


NTSTATUS usb_bulkout_zero_complete(
	PDEVICE_OBJECT	pUsbDevObj, 
	PIRP		pIrp, void*	pZeroContext)
{	
	struct zero_bulkout_context *pcontext = (struct zero_bulkout_context *)pZeroContext;
			   
_func_enter_;

	if(pcontext)
	{
		if(pcontext->pbuf)
		{
			ExFreePool(pcontext->pbuf);	  
		}	

		if(pcontext->purb)
		{
			ExFreePool(pcontext->purb);	
		}

		if(pcontext->pirp && (pIrp ==pcontext->pirp))
		{			
			IoFreeIrp(pIrp);
		}

		ExFreePool(pcontext);	
	}	

_func_exit_;

	return STATUS_MORE_PROCESSING_REQUIRED;
	

}

u32 usb_bulkout_zero(struct intf_hdl *pintfhdl, u32 addr)
{	
	struct zero_bulkout_context *pcontext;
	unsigned char *pbuf;
	char NextDeviceStackSize, len;
	PIO_STACK_LOCATION	nextStack;
	USBD_STATUS		usbdstatus;
	HANDLE				PipeHandle;	
	PIRP					pirp = NULL;
	PURB				purb = NULL;	
	NDIS_STATUS			ndisStatus = NDIS_STATUS_SUCCESS;
	_adapter *padapter = (_adapter *)pintfhdl->adapter;
	struct dvobj_priv	*pdvobj = (struct dvobj_priv *)&padapter->dvobjpriv;	


_func_enter_;

	if((padapter->bDriverStopped) || (padapter->bSurpriseRemoved) ||(padapter->pwrctrlpriv.pnp_bstop_trx))
	{		
		return _FAIL;
	}

	len = 0;
	NextDeviceStackSize = (char)pdvobj->nextdevstacksz;

	pcontext = (struct zero_bulkout_context *)ExAllocatePool(NonPagedPool, sizeof(struct zero_bulkout_context));
	pbuf = (unsigned char *)ExAllocatePool(NonPagedPool, sizeof(int));	
    	purb = (PURB)ExAllocatePool(NonPagedPool, sizeof(URB));
      	pirp = IoAllocateIrp(NextDeviceStackSize, FALSE);

	pcontext->pbuf = pbuf;
	pcontext->purb = purb;
	pcontext->pirp = pirp;
	pcontext->padapter = padapter;
                    
	//translate DMA FIFO addr to pipehandle
	PipeHandle = ffaddr2pipehdl(pdvobj, addr);	


	// Build our URB for USBD
	UsbBuildInterruptOrBulkTransferRequest(
				purb,
				sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER),
				PipeHandle,
				pbuf, 
				NULL, 
				len, 
				0, 
				NULL);
	
	//
	// call the calss driver to perform the operation
	// pass the URB to the USB driver stack
	//
	nextStack = IoGetNextIrpStackLocation(pirp);
	nextStack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
	nextStack->Parameters.Others.Argument1 = purb;
	nextStack->Parameters.DeviceIoControl.IoControlCode = IOCTL_INTERNAL_USB_SUBMIT_URB;

	//Set Completion Routine
	IoSetCompletionRoutine(pirp,					// irp to use
				               usb_bulkout_zero_complete,	// callback routine
				               pcontext,				// context 
				               TRUE,					// call on success
				               TRUE,					// call on error
				               TRUE);					// call on cancel

	
	// Call IoCallDriver to send the irp to the usb bus driver
	//
	ndisStatus = IoCallDriver(pdvobj->pnextdevobj, pirp);
	usbdstatus = URB_STATUS(purb);

	if( USBD_HALTED(usbdstatus) )
	{		
		padapter->bDriverStopped=_TRUE;
		padapter->bSurpriseRemoved=_TRUE;
	}

	//
	// The usb bus driver should always return STATUS_PENDING when bulk out irp async
	//
	if ( ndisStatus != STATUS_PENDING )
	{		
		return _FAIL;
	} 	
	
_func_exit_;
	
	return _SUCCESS;

}

void usb_read_mem(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *rmem)
{
	_func_enter_;
	

	
	_func_exit_;
}

NTSTATUS usb_write_mem_complete(PDEVICE_OBJECT	pUsbDevObj, PIRP piowrite_irp, PVOID pusb_cnxt)
{
		
	_irqL irqL;
	_list	*head, *plist;
	struct io_req	*pio_req;	
	struct io_queue *pio_q = (struct io_queue *) pusb_cnxt;
	struct intf_hdl *pintf = &(pio_q->intf);	
	struct intf_priv *pintfpriv = pintf->pintfpriv;	
	_adapter *padapter = (_adapter *)pintf->adapter;
	NTSTATUS status = STATUS_SUCCESS;

	head = &(pio_q->processing);
	
	_func_enter_;
	
	_enter_critical_bh(&(pio_q->lock), &irqL);
	
	pintfpriv->io_irp_cnt--;
	if(pintfpriv->io_irp_cnt ==0){		
		_rtw_up_sema(&(pintfpriv->io_retevt));
	}	
	
	pintfpriv->bio_irp_pending=_FALSE;
	
	switch(piowrite_irp->IoStatus.Status)
	{		
		case STATUS_SUCCESS:
			break;
			
		default:
			padapter->bSurpriseRemoved=_TRUE;
			RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("\n usbAsynIntOutComplete:pioread_irp->IoStatus.Status !=STATUS_SUCCESS\n"));
			break;
	}				

	//free irp in processing list...	
	while(rtw_is_list_empty(head) != _TRUE)
	{
		plist = get_next(head);	
		rtw_list_delete(plist);
		pio_req = LIST_CONTAINOR(plist, struct io_req, list);
		_rtw_up_sema(&pio_req->sema);
	}	
						
	_exit_critical_bh(&(pio_q->lock), &irqL);

	_func_exit_;
	
	return STATUS_MORE_PROCESSING_REQUIRED;

}

void usb_write_mem(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *wmem)
{
	u32 bwritezero;
	_irqL	irqL;
	USBD_STATUS			usbdstatus;		
	PIO_STACK_LOCATION		nextStack;
	HANDLE				PipeHandle;	
	struct io_req *pio_req;
	
	_adapter *adapter = (_adapter *)pintfhdl->adapter;
	struct intf_priv *pintfpriv = pintfhdl->pintfpriv;
	struct dvobj_priv   *pdev = (struct dvobj_priv   *)pintfpriv->intf_dev;      
	PURB	piorw_urb = pintfpriv->piorw_urb;
	PIRP		piorw_irp  = pintfpriv->piorw_irp; 	
	struct io_queue	*pio_queue = (struct io_queue *)adapter->pio_queue;	
	NTSTATUS NtStatus = STATUS_SUCCESS;	
	
	_func_enter_;	

	pio_req = alloc_ioreq(pio_queue);
	
	if ((pio_req == NULL)||(adapter->bSurpriseRemoved)){
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("async_irp_write32 : pio_req =0x%x adapter->bSurpriseRemoved=0x%x",pio_req,adapter->bSurpriseRemoved ));
		goto exit;
	}	
	
	_enter_critical_bh(&(pio_queue->lock), &irqL);
	
	rtw_list_insert_tail(&(pio_req->list),&(pio_queue->processing));


#ifdef NDIS51_MINIPORT
	IoReuseIrp(piorw_irp, STATUS_SUCCESS);
#else
	piorw_irp->Cancel = _FALSE;
#endif

	if((adapter->bDriverStopped) || (adapter->bSurpriseRemoved) ||(adapter->pwrctrlpriv.pnp_bstop_trx))	
	{
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("\npadapter->pwrctrlpriv.pnp_bstop_trx==_TRUE\n"));
		_func_exit_;
		return;
	}

	//translate DMA FIFO addr to pipehandle
	PipeHandle = ffaddr2pipehdl(pdev, addr);	

	
	pintfpriv->io_irp_cnt++;
	pintfpriv->bio_irp_pending=_TRUE;	
	// Build our URB for USBD
	UsbBuildInterruptOrBulkTransferRequest(
				piorw_urb,
				sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER),
				PipeHandle,
				(PVOID)wmem,
				NULL, 
				cnt, 
				0, 
				NULL);  

	//
	// call the calss driver to perform the operation
	// pass the URB to the USB driver stack
	//
	nextStack = IoGetNextIrpStackLocation(piorw_irp);
	nextStack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
	nextStack->Parameters.Others.Argument1 = (PURB)piorw_urb;
	nextStack->Parameters.DeviceIoControl.IoControlCode = IOCTL_INTERNAL_USB_SUBMIT_URB;

	IoSetCompletionRoutine(
				piorw_irp,				// irp to use				
				usb_write_mem_complete,		// routine to call when irp is done
				pio_queue,				// context to pass routine
				TRUE,					// call on success
				TRUE,					// call on error
				TRUE);					// call on cancel
	
	// 
	// Call IoCallDriver to send the irp to the usb port
	//
	NtStatus	= IoCallDriver(pdev->pnextdevobj, piorw_irp);
	usbdstatus = URB_STATUS(piorw_urb);

	//
	// The USB driver should always return STATUS_PENDING when
	// it receives a write irp
	//
	if ((NtStatus != STATUS_PENDING) || USBD_HALTED(usbdstatus) ) {

		if( USBD_HALTED(usbdstatus) ) {

			RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_write_mem():USBD_HALTED(usbdstatus)=%X!\n",USBD_HALTED(usbdstatus)) );
		}
		_func_exit_;
		return;//STATUS_UNSUCCESSFUL;
	}

	_exit_critical_bh(&(pio_queue->lock), &irqL);
	
	_rtw_down_sema(&pio_req->sema);	
	free_ioreq(pio_req, pio_queue);
	

	bwritezero = _FALSE;
       if (pdev->ishighspeed)
	{
		if(cnt> 0 && cnt%512 == 0)
			bwritezero = _TRUE;
			
	}
	else
	{
		if(cnt > 0 && cnt%64 == 0)
			bwritezero = _TRUE;		
	}

	
	if(bwritezero == _TRUE)
	{
		usb_bulkout_zero(pintfhdl, addr);
	}
	
exit:
	
	_func_exit_;
	
}

NTSTATUS usb_read_port_complete(PDEVICE_OBJECT pUsbDevObj, PIRP pIrp, PVOID context)
{	
	uint isevt, *pbuf;
	struct _URB_BULK_OR_INTERRUPT_TRANSFER	*pbulkurb;
	USBD_STATUS		usbdstatus;	
	struct recv_buf		*precvbuf = (struct recv_buf *)context;	
	_adapter 				*adapter =(_adapter *)precvbuf->adapter;
	struct recv_priv		*precvpriv = &adapter->recvpriv;
	struct dvobj_priv   	*dev = (struct dvobj_priv   *)&adapter->dvobjpriv;
	PURB				purb = precvbuf->purb;
	struct intf_hdl 		*pintfhdl = &adapter->pio_queue->intf;
	
	//RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port_complete!!!\n"));

	usbdstatus = URB_STATUS(purb);
	
	_rtw_spinlock_ex(&precvpriv->lock);
	precvbuf->irp_pending=_FALSE;
	precvpriv->rx_pending_cnt --;
	_rtw_spinunlock_ex(&precvpriv->lock);	
	
	if(precvpriv->rx_pending_cnt== 0) {		
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port_complete: rx_pending_cnt== 0, set allrxreturnevt!\n"));
		_rtw_up_sema(&precvpriv->allrxreturnevt);	
	}


	if( pIrp->Cancel == _TRUE ) {
		
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port_complete: One IRP has been cancelled succesfully\n"));
		return STATUS_MORE_PROCESSING_REQUIRED;
	}
	if(adapter->bSurpriseRemoved) {

		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port_complete:bDriverStopped(%d) OR bSurpriseRemoved(%d)", adapter->bDriverStopped, adapter->bSurpriseRemoved));
		return STATUS_MORE_PROCESSING_REQUIRED;
	}

	switch(pIrp->IoStatus.Status) 
	{
		case STATUS_SUCCESS:
			
			pbulkurb = &(precvbuf->purb)->UrbBulkOrInterruptTransfer;
			if((pbulkurb->TransferBufferLength >(MAX_RECVBUF_SZ)) || (pbulkurb->TransferBufferLength < RXDESC_SIZE) ) 
			{								
				RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("\n usb_read_port_complete: (pbulkurb->TransferBufferLength > MAX_RECVBUF_SZ) || (pbulkurb->TransferBufferLength < RXDESC_SIZE)\n"));
				rtw_read_port(adapter, precvpriv->ff_hwaddr, 0, (unsigned char *)precvbuf);
			}
			else 
			{	
			       precvbuf->transfer_len = pbulkurb->TransferBufferLength;

				pbuf = (uint*)precvbuf->pbuf;

				if((isevt = *(pbuf+1)&0x1ff) == 0x1ff)
				{								
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
	
			break;
			
		default:
			
			if( !USBD_HALTED(usbdstatus) )
			{				
				RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("\n usb_read_port_complete():USBD_HALTED(usbdstatus)=%x  (need to handle ) \n",USBD_HALTED(usbdstatus)));				
			
			}
			else 
			{				
				RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("\n usb_read_port_complete(): USBD_HALTED(usbdstatus)=%x \n\n", USBD_HALTED(usbdstatus)) );
				adapter->bDriverStopped = _TRUE;
				adapter->bSurpriseRemoved = _TRUE;
				RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port_complete(): USBD_HALTED(usbdstatus)=%x \n\n", USBD_HALTED(usbdstatus))) ;
			}

		      break;
			  
	}

	return STATUS_MORE_PROCESSING_REQUIRED;
	
}

u32 usb_read_port(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *rmem)
{
	u8 					*pdata;
	u16					size;
	PURB				purb;
	PIRP					pirp;
	PIO_STACK_LOCATION	nextStack;
	NTSTATUS			ntstatus;
	USBD_STATUS		usbdstatus;
	HANDLE				PipeHandle;	
	struct recv_buf		*precvbuf = (struct recv_buf *)rmem;
	struct intf_priv		*pintfpriv = pintfhdl->pintfpriv;
	struct dvobj_priv		*pdev = (struct dvobj_priv   *)pintfpriv->intf_dev;
	_adapter				*adapter = (_adapter *)pdev->padapter;
	struct recv_priv		*precvpriv = &adapter->recvpriv;
	u32					bResult = _FALSE;

_func_enter_;
	
	if(adapter->bDriverStopped || adapter->bSurpriseRemoved ||adapter->pwrctrlpriv.pnp_bstop_trx) {
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port:( padapter->bDriverStopped ||padapter->bSurpriseRemoved ||adapter->pwrctrlpriv.pnp_bstop_trx)!!!\n"));
		return bResult;
	}

	if(precvbuf !=NULL)
	{

		rtl8192cu_init_recvbuf(adapter, precvbuf);
	
		_rtw_spinlock(&precvpriv->lock);
		precvpriv->rx_pending_cnt++;
		precvbuf->irp_pending = _TRUE;
		_rtw_spinunlock(&precvpriv->lock);

	       pdata = (u8*)precvbuf->pbuf;

		size	 = sizeof( struct _URB_BULK_OR_INTERRUPT_TRANSFER );
		purb = precvbuf->purb;	

		//translate DMA FIFO addr to pipehandle
		PipeHandle = ffaddr2pipehdl(pdev, addr);	
		 
		UsbBuildInterruptOrBulkTransferRequest(
			purb,
			(USHORT)size,
			PipeHandle,
			pdata,
			NULL, 
			MAX_RECVBUF_SZ,
			USBD_TRANSFER_DIRECTION_IN | USBD_SHORT_TRANSFER_OK, 
			NULL
			);

		pirp = precvbuf->pirp;

#if NDIS51_MINIPORT
		IoReuseIrp(pirp, STATUS_SUCCESS);
#else
		pirp->Cancel = _FALSE;
#endif

		// call the class driver to perform the operation
		// and pass the URB to the USB driver stack
		nextStack = IoGetNextIrpStackLocation(pirp);
		nextStack->Parameters.Others.Argument1 = purb;
		nextStack->Parameters.DeviceIoControl.IoControlCode = IOCTL_INTERNAL_USB_SUBMIT_URB;
		nextStack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;

		IoSetCompletionRoutine(
			pirp,					// irp to use
			usb_read_port_complete,	// routine to call when irp is done
			precvbuf,					// context to pass routine 
			TRUE,					// call on success
			TRUE,					// call on error
			TRUE);					// call on cancel

		//
		// The IoCallDriver routine  
		// sends an IRP to the driver associated with a specified device object.
		//
		ntstatus = IoCallDriver(pdev->pnextdevobj, pirp);
		usbdstatus = URB_STATUS(purb);

		if( USBD_HALTED(usbdstatus) ) {
			RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("\n usb_read_port(): USBD_HALTED(usbdstatus=0x%.8x)=%.8x  \n\n", usbdstatus, USBD_HALTED(usbdstatus)));
			pdev->padapter->bDriverStopped=_TRUE;
			pdev->padapter->bSurpriseRemoved=_TRUE;
		}

		if( ntstatus == STATUS_PENDING )
		{ 
			bResult = _TRUE;// The IRP is pended in USBD as we expected.
		}
		else {
			RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port(): IoCallDriver failed!!! IRP STATUS: %X\n", ntstatus));
			RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port(): IoCallDriver failed!!! USB STATUS: %X\n", usbdstatus));
		}

	}
	else{
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port:precv_frame ==NULL\n"));
	}

_func_exit_;
	
	return bResult;
	
}

void usb_read_port_cancel(_adapter *padapter)
{	
	struct recv_buf  *precvbuf;
	sint i;
	struct dvobj_priv   *pdev = &padapter->dvobjpriv;
	struct recv_priv *precvpriv=&padapter->recvpriv;	
		
	RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("\n ==>usb_read_port_cancel\n"));

	_rtw_spinlock(&precvpriv->lock);
	precvpriv->rx_pending_cnt--; //decrease 1 for Initialize ++ 
	_rtw_spinunlock(&precvpriv->lock);

	if (precvpriv->rx_pending_cnt)
	{
		// Canceling Pending Recv Irp
		precvbuf = (struct recv_buf  *)precvpriv->precv_buf;
		
		for( i = 0; i < NR_RECVBUFF; i++ )
		{
			if (precvbuf->irp_pending == _TRUE)
			{	
				IoCancelIrp(precvbuf->pirp);
				RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port_cancel() :IoCancelIrp\n"));
			}
			
			precvbuf++;
		}
		
		_rtw_down_sema(&precvpriv->allrxreturnevt);
		
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port_cancel:down sema\n"));

	}
	
	RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("<==usb_read_port_cancel\n"));
	
}

NTSTATUS usb_write_port_complete(
	PDEVICE_OBJECT	pUsbDevObj,
	PIRP				pIrp,
	PVOID			pTxContext
) 
{	
	u32	i, bIrpSuccess, sz;
	NTSTATUS	status = STATUS_SUCCESS;
	u8 *ptr;
	struct xmit_frame	*pxmitframe = (struct xmit_frame *) pTxContext;
        struct xmit_buf *pxmitbuf = pxmitframe->pxmitbuf;
	_adapter			*padapter = pxmitframe->padapter;
	struct dvobj_priv	*pdev =	(struct dvobj_priv *)&padapter->dvobjpriv;	
	struct io_queue	*pio_queue = (struct io_queue *)padapter->pio_queue;
	struct intf_hdl	*pintfhdl = &(pio_queue->intf);
       struct xmit_priv	*pxmitpriv = &padapter->xmitpriv;		
	   
_func_enter_;

	RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("+usb_write_port_complete\n"));

	_rtw_spinlock_ex(&pxmitpriv->lock);	
	pxmitpriv->txirp_cnt--;
	_rtw_spinunlock_ex(&pxmitpriv->lock);
	
	if(pxmitpriv->txirp_cnt==0){
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_write_port_complete: txirp_cnt== 0, set allrxreturnevt!\n"));		
		_rtw_up_sema(&(pxmitpriv->tx_retevt));
	}
	
	status = pIrp->IoStatus.Status;

	if( status == STATUS_SUCCESS ) 
		bIrpSuccess = _TRUE;	
	else	
		bIrpSuccess = _FALSE;
	
	if( pIrp->Cancel == _TRUE )
	{		
	    if(pxmitframe !=NULL)
	    {	       
	    		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("\n usb_write_port_complete:pIrp->Cancel == _TRUE,(pxmitframe !=NULL\n"));
			rtw_free_xmitframe_ex(pxmitpriv, pxmitframe);			
	    }
		  	 
	     return STATUS_MORE_PROCESSING_REQUIRED;
	}

	if(padapter->bSurpriseRemoved)
	{
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_write_port_complete:bDriverStopped(%d) OR bSurpriseRemoved(%d)", padapter->bDriverStopped, padapter->bSurpriseRemoved));
		return STATUS_MORE_PROCESSING_REQUIRED;
	}


	//
	// Send 0-byte here if necessary.
	//
	// <Note> 
	// 1. We MUST keep at most one IRP pending in each endpoint, otherwise USB host controler driver will hang.
	// Besides, even 0-byte IRP shall be count into #IRP sent down, so, we send 0-byte here instead of TxFillDescriptor8187().
	// 2. If we don't count 0-byte IRP into an #IRP sent down, Tx will stuck when we download files via BT and 
	// play online video on XP SP1 EHCU.
	// 2005.12.26, by rcnjko.
	//

	
	for(i=0; i< 8; i++)
	{
            if(pIrp == pxmitframe->pxmit_irp[i])
            {
		    pxmitframe->bpending[i] = _FALSE;//
		    //ac_tag = pxmitframe->ac_tag[i];
                  sz = pxmitframe->sz[i];
		    break;		  
            }
	}

#if 0	
	pxmitframe->fragcnt--;
	if(pxmitframe->fragcnt == 0)// if((pxmitframe->fragcnt == 0) && (pxmitframe->irpcnt == 8)){
	{
		//RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("\n usb_write_port_complete:pxmitframe->fragcnt == 0\n"));
		rtw_free_xmitframe(pxmitpriv,pxmitframe);	          
      	}
#else	

	//not to consider tx fragment
	rtw_free_xmitframe_ex(pxmitpriv, pxmitframe);		

#endif	

	rtl8192cu_xmitframe_complete(padapter, pxmitpriv, pxmitbuf);

_func_exit_;

	return STATUS_MORE_PROCESSING_REQUIRED;

}

u32 usb_write_port(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *wmem)
{
       u32 i, bwritezero;      	
	u8 *ptr;
	PIO_STACK_LOCATION	nextStack;
	USBD_STATUS		usbdstatus;
	HANDLE				PipeHandle;	
	PIRP					pirp = NULL;
	PURB				purb = NULL;	
	NDIS_STATUS			ndisStatus = NDIS_STATUS_SUCCESS;
	_adapter *padapter = (_adapter *)pintfhdl->adapter;
	struct dvobj_priv	*pNdisCEDvice = (struct dvobj_priv   *)&padapter->dvobjpriv;	
	struct xmit_priv	*pxmitpriv = &padapter->xmitpriv;
	struct xmit_frame *pxmitframe = (struct xmit_frame *)wmem;

_func_enter_;

	if((padapter->bDriverStopped) || (padapter->bSurpriseRemoved) ||(padapter->pwrctrlpriv.pnp_bstop_trx))
	{
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_write_port:( padapter->bDriverStopped ||padapter->bSurpriseRemoved ||adapter->pwrctrlpriv.pnp_bstop_trx)!!!\n"));
		return _FAIL;
	}
	

	for(i=0; i<8; i++)
       {
		if(pxmitframe->bpending[i] == _FALSE)
		{
			_rtw_spinlock(&pxmitpriv->lock);	
			pxmitpriv->txirp_cnt++;
			pxmitframe->bpending[i]  = _TRUE;
			_rtw_spinunlock(&pxmitpriv->lock);
			
			pxmitframe->sz[i] = cnt;
			purb	= pxmitframe->pxmit_urb[i];
			pirp	= pxmitframe->pxmit_irp[i];
			
			//pxmitframe->ac_tag[i] = ac_tag;
			
			break;	 
		}
       }	

	bwritezero = _FALSE;
       if (pNdisCEDvice->ishighspeed)
	{
		if(cnt> 0 && cnt%512 == 0)
		{
			RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("ishighspeed, cnt=%d\n", cnt));
			//cnt=cnt+1;
			bwritezero = _TRUE;
		}	
	}
	else
	{
		if(cnt > 0 && cnt%64 == 0)
		{
			RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("cnt=%d\n", cnt));
			//cnt=cnt+1;
			bwritezero = _TRUE;
		}	
	}
	

#ifdef NDIS51_MINIPORT
	IoReuseIrp(pirp, STATUS_SUCCESS);
#else
	pirp->Cancel = _FALSE;
#endif


	//translate DMA FIFO addr to pipehandle
	PipeHandle = ffaddr2pipehdl(pNdisCEDvice, addr);	


	// Build our URB for USBD
	UsbBuildInterruptOrBulkTransferRequest(
				purb,
				sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER),
				PipeHandle,
				pxmitframe->mem_addr, 
				NULL, 
				cnt, 
				0, 
				NULL);
	
	//
	// call the calss driver to perform the operation
	// pass the URB to the USB driver stack
	//
	nextStack = IoGetNextIrpStackLocation(pirp);
	nextStack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
	nextStack->Parameters.Others.Argument1 = purb;
	nextStack->Parameters.DeviceIoControl.IoControlCode = IOCTL_INTERNAL_USB_SUBMIT_URB;

	//Set Completion Routine
	IoSetCompletionRoutine(pirp,					// irp to use
				               usb_write_port_complete,	// callback routine
				               pxmitframe,				// context 
				               TRUE,					// call on success
				               TRUE,					// call on error
				               TRUE);					// call on cancel

	
	// Call IoCallDriver to send the irp to the usb bus driver
	//
	ndisStatus = IoCallDriver(pNdisCEDvice->pnextdevobj, pirp);
	usbdstatus = URB_STATUS(purb);

	if( USBD_HALTED(usbdstatus) )
	{
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("\n usb_write_port(): USBD_HALTED(usbdstatus)=%x set bDriverStopped TRUE!\n\n",USBD_HALTED(usbdstatus)) );
		padapter->bDriverStopped=_TRUE;
		padapter->bSurpriseRemoved=_TRUE;
	}

	//
	// The usb bus driver should always return STATUS_PENDING when bulk out irp async
	//
	if ( ndisStatus != STATUS_PENDING )
	{
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("\n usb_write_port(): ndisStatus(%x) != STATUS_PENDING!\n\n", ndisStatus));
		
		_func_exit_;
		
		return _FAIL;
	} 	
	
	if(bwritezero == _TRUE)
	{
		usb_bulkout_zero(pintfhdl, addr);
	}

	
_func_exit_;
	
	return _SUCCESS;

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
					IoCancelIrp(pxmitframe->pxmit_irp[j]);		
					RT_TRACE(_module_hci_ops_os_c_,_drv_err_,(" usb_write_port_cancel() :IoCancelIrp\n"));

				}
			}
			
			pxmitframe++;
		}

		_rtw_down_sema(&(pxmitpriv->tx_retevt));
		
	}

}


/*! \brief Wrap the pUrb to an IRP and send this IRP to Bus Driver. Then wait for this IRP completion.
	The Caller shall be at Passive Level.
*/
NTSTATUS  sync_callusbd(struct dvobj_priv *pdvobjpriv, PURB purb)
{

	KEVENT					kevent;
	PIRP						irp;
	IO_STATUS_BLOCK		iostatusblock;
	PIO_STACK_LOCATION		nextstack;
	USBD_STATUS			usbdstatus;
	LARGE_INTEGER			waittime;
	NTSTATUS ntstatus = STATUS_SUCCESS;
	_adapter *padapter = pdvobjpriv->padapter;
	

	_func_enter_;

//	if(padapter->bDriverStopped) {
//		goto exit;
//	}
	
	KeInitializeEvent(&kevent, NotificationEvent, _FALSE);
	irp = IoBuildDeviceIoControlRequest(
			IOCTL_INTERNAL_USB_SUBMIT_URB,
			pdvobjpriv->pphysdevobj,//CEdevice->pUsbDevObj, 
			NULL, 
			0, 
			NULL, 
			0, 
			_TRUE, 
			&kevent, 
			&iostatusblock);

	if(irp == NULL) {
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("SyncCallUSBD: memory alloc for irp failed\n"));
		ntstatus=STATUS_INSUFFICIENT_RESOURCES;
		goto exit;
	}
	
	nextstack = IoGetNextIrpStackLocation(irp);
	if(nextstack == NULL)	
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("IoGetNextIrpStackLocation fail\n"));

	nextstack->Parameters.Others.Argument1 = purb;
	
	// Issue an IRP for Sync IO.
	ntstatus = IoCallDriver(pdvobjpriv->pphysdevobj, irp);
	usbdstatus = URB_STATUS(purb);

	if(ntstatus == STATUS_PENDING)
	{		
		// Method 1
		waittime.QuadPart = -10000 * 50000;
		ntstatus = KeWaitForSingleObject(&kevent, Executive, KernelMode, _FALSE, &waittime); //8150 code
		
		// Method 2
		//ntStatus = KeWaitForSingleObject(&Kevent, Executive, KernelMode, FALSE, NULL); //DDK sample
		
		usbdstatus = URB_STATUS(purb);

		if(ntstatus == STATUS_TIMEOUT) 
		{			
			//usbdevice->nIoStuckCnt++;
			RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("SyncCallUSBD: TIMEOUT....5000ms\n"));

			// Method 2
			IoCancelIrp(irp);
			ntstatus = KeWaitForSingleObject(&kevent, Executive, KernelMode, _FALSE, NULL); //DDK sample
			usbdstatus = URB_STATUS(purb);

			usbdstatus = USBD_STATUS_SUCCESS;
		}
		
	}
	
exit:	
	
	_func_exit_;
	
	return ntstatus;
	
}
int usbctrl_vendorreq(struct intf_priv *pintfpriv, u8 request, u16 value, u16 index, void *pdata, u16 len, u8 requesttype)
{
	PURB			purb;
	u8				ret;
	unsigned long		transferflags;
	NTSTATUS		ntstatus;

	struct dvobj_priv  *pdvobjpriv = (struct dvobj_priv  *)pintfpriv->intf_dev;   
	
	_func_enter_;

	ret=_TRUE;
	purb = (PURB)ExAllocatePool(NonPagedPool, sizeof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST) );
	if(purb == NULL) {
		
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usbctrl_vendorreq(): Failed to allocate urb !!!\n"));
		ret =_FALSE;
		goto exit;
	}

	if (requesttype == 0x01) {
		transferflags = USBD_TRANSFER_DIRECTION_IN;//read_in
	} else {
		transferflags= 0;//write_out
	}

	UsbBuildVendorRequest(
			purb, 		//Pointer to an URB that is to be formatted as a vendor or class request. 
			URB_FUNCTION_VENDOR_DEVICE,	//Indicates the URB is a vendor-defined request for a USB device. 
			sizeof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST),  //Specifies the length, in bytes, of the URB. 
			transferflags, 	//TransferFlags 
			0,			//ReservedBits 
			request, 	//Request 
			value, 		//Value 
			index,		//Index
			pdata,		//TransferBuffer 
			NULL,		//TransferBufferMDL 
			len,			//TransferBufferLength 
			NULL		//Link 
	);

	ntstatus = sync_callusbd(pdvobjpriv, purb);
	if(!NT_SUCCESS(ntstatus))
	{
		ExFreePool(purb);
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,(" usbctrl_vendorreq() : SOMETHING WRONG\n") );
		ret = _FALSE;
		goto exit;
	}

	ExFreePool(purb);

exit:	
	_func_exit_;
	
	return ret;	

}

