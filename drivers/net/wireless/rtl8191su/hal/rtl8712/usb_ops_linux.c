/******************************************************************************
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
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
#include <usb_ops.h>
#include <circ_buf.h>

#if defined (PLATFORM_LINUX) && defined (PLATFORM_WINDOWS)

#error "Shall be Linux or Windows, but not both!\n"

#endif

#define	RTL871X_VENQT_READ	0xc0
#define	RTL871X_VENQT_WRITE	0x40

struct zero_bulkout_context
{
	void *pbuf;
	void *purb;
	void *pirp;
	void *padapter;
};


#define usb_write_cmd usb_write_mem 
//#define usb_read_cmd usb_read_mem
#define usb_write_cmd_complete usb_write_mem_complete
//#define usb_read_cmd_complete usb_read_mem_complete

#define RTW_USB_CONTROL_MSG_TIMEOUT		500
#define RTW_USB_CONTROL_RETRY_CNT		10

uint usb_init_intf_priv(struct intf_priv *pintfpriv)
{
_func_enter_;
	
	//pintfpriv->intf_status = _IOREADY;

	//_init_timer(&pintfpriv->io_timer, padapter->pnetdev, io_irp_timeout_handler, pintfpriv);
	
	pintfpriv->piorw_urb = usb_alloc_urb(0, GFP_ATOMIC);	
	if(pintfpriv->piorw_urb==NULL)
	{		
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("pintfpriv->piorw_urb==NULL!!!\n"));
		goto usb_init_intf_priv_fail;
	}	
	
	_init_sema(&(pintfpriv->io_retevt), 0);

_func_exit_;

	return _SUCCESS;

usb_init_intf_priv_fail:

	if(pintfpriv->piorw_urb)
	{
		usb_free_urb(pintfpriv->piorw_urb);
	}	

_func_exit_;

	return _FAIL;
		
}

void usb_unload_intf_priv(struct intf_priv *pintfpriv)
{

_func_enter_;
	
	RT_TRACE(_module_hci_ops_os_c_,_drv_info_,("+usb_unload_intf_priv\n"));

	if(pintfpriv->piorw_urb)
	{
		usb_kill_urb(pintfpriv->piorw_urb);
		usb_free_urb(pintfpriv->piorw_urb);
	}

	_free_sema(&(pintfpriv->io_retevt));

	RT_TRACE(_module_hci_ops_os_c_,_drv_info_,("-usb_unload_intf_priv\n"));

_func_exit_;
	
}

int ffaddr2pipehdl(struct dvobj_priv *pdvobj, u32 addr)
{
	int pipe=0;
	struct usb_device *pusbd = pdvobj->pusbdev;
	
	if(pdvobj->nr_endpoint == 11)
	{		
		switch(addr)
		{	    
			case RTL8712_DMA_BKQ:
			 	pipe=usb_sndbulkpipe(pusbd, 0x07);
				break;
	     		case RTL8712_DMA_BEQ:
		 		pipe=usb_sndbulkpipe(pusbd, 0x06);
				break;	     		
	     		case RTL8712_DMA_VIQ:
		 		pipe=usb_sndbulkpipe(pusbd, 0x05);
				break;
	    		case RTL8712_DMA_VOQ:
				pipe=usb_sndbulkpipe(pusbd, 0x04);
				break;					
                     case RTL8712_DMA_BCNQ:	
				pipe=usb_sndbulkpipe(pusbd, 0x0a);
				break;	 	
			case RTL8712_DMA_BMCQ:	//HI Queue
				pipe=usb_sndbulkpipe(pusbd, 0x0b);
				break;	
			case RTL8712_DMA_MGTQ:				
		 		pipe=usb_sndbulkpipe(pusbd, 0x0c);
				break;
                     case RTL8712_DMA_RX0FF:
				pipe=usb_rcvbulkpipe(pusbd, 0x03);//in
				break;	 	
			case RTL8712_DMA_C2HCMD:		 	
				pipe=usb_rcvbulkpipe(pusbd, 0x09);//in
				break;
			case RTL8712_DMA_H2CCMD:
				pipe=usb_sndbulkpipe(pusbd, 0x0d);
				break;	
				
		}

	}
	else if(pdvobj->nr_endpoint == 6)
	{
		switch(addr)
		{	    
	     		case RTL8712_DMA_BKQ:
			 	pipe=usb_sndbulkpipe(pusbd, 0x07);
				break;
	     		case RTL8712_DMA_BEQ:
		 		pipe=usb_sndbulkpipe(pusbd, 0x06);
				break;	     		
	     		case RTL8712_DMA_VIQ:
		 		pipe=usb_sndbulkpipe(pusbd, 0x05);
				break;
	    		case RTL8712_DMA_VOQ:
				pipe=usb_sndbulkpipe(pusbd, 0x04);
				break;					
                     case RTL8712_DMA_RX0FF:
			case RTL8712_DMA_C2HCMD:		 	
				pipe=usb_rcvbulkpipe(pusbd, 0x03);//in
				break;
			case RTL8712_DMA_H2CCMD:
			case RTL8712_DMA_BCNQ:					
			case RTL8712_DMA_BMCQ:	
			case RTL8712_DMA_MGTQ:			
				pipe=usb_sndbulkpipe(pusbd, 0x0d);
				break;	
				
		}

	}
	else if(pdvobj->nr_endpoint == 4)
	{
		switch(addr)
		{		        
	     		case RTL8712_DMA_BEQ:
		 	//case RTL8712_DMA_BKQ:
			 	//pipe=usb_sndbulkpipe(pusbd, 0x05);
			 	pipe=usb_sndbulkpipe(pusbd, 0x06);	
				break;		
	     		//case RTL8712_DMA_VIQ:
		 	case RTL8712_DMA_VOQ:					
		 		pipe=usb_sndbulkpipe(pusbd, 0x04);
				break;
			case RTL8712_DMA_RX0FF:
			case RTL8712_DMA_C2HCMD:		 	
				pipe=usb_rcvbulkpipe(pusbd, 0x03);//in
				break;
			case RTL8712_DMA_H2CCMD:	
			case RTL8712_DMA_BCNQ:					
			case RTL8712_DMA_BMCQ:	
			case RTL8712_DMA_MGTQ:				
				pipe=usb_sndbulkpipe(pusbd, 0x0d);
				break;	
		}
	
	}
	else
	{
	   RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("ffaddr2pipehdl():nr_endpoint=%d error!\n", pdvobj->nr_endpoint));
	   pipe = 0;
	}
		
	return pipe;

}


#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 18))
static void usb_bulkout_zero_complete(struct urb *purb)
#else
static void usb_bulkout_zero_complete(struct urb *purb, struct pt_regs *regs)
#endif
{	
	struct zero_bulkout_context *pcontext = (struct zero_bulkout_context *)purb->context;

	//printk("+usb_bulkout_zero_complete\n");
	
	if(pcontext)
	{
		if(pcontext->pbuf)
		{			
			_mfree(pcontext->pbuf, sizeof(int));	
		}	

		if(pcontext->purb && (pcontext->purb==purb))
		{
			usb_free_urb(pcontext->purb);
		}

	
		_mfree((u8*)pcontext, sizeof(struct zero_bulkout_context));	
	}	
	

}

u32 usb_bulkout_zero(struct intf_hdl *pintfhdl, u32 addr)
{	
	int pipe, status, len;
	u32 ret;
	unsigned char *pbuf;
	struct zero_bulkout_context *pcontext;
	PURB	purb = NULL;	
	_adapter *padapter = (_adapter *)pintfhdl->adapter;
	struct dvobj_priv *pdvobj = (struct dvobj_priv *)&padapter->dvobjpriv;	
	struct usb_device *pusbd = pdvobj->pusbdev;

	//printk("+usb_bulkout_zero\n");
	
		
	if((padapter->bDriverStopped) || (padapter->bSurpriseRemoved) ||(padapter->pwrctrlpriv.pnp_bstop_trx))
	{		
		return _FAIL;
	}
	

	pcontext = (struct zero_bulkout_context *)_malloc(sizeof(struct zero_bulkout_context));

	pbuf = (unsigned char *)_malloc(sizeof(int));	
    	purb = usb_alloc_urb(0, GFP_ATOMIC);
      	
	len = 0;
	pcontext->pbuf = pbuf;
	pcontext->purb = purb;
	pcontext->pirp = NULL;
	pcontext->padapter = padapter;

	
	//translate DMA FIFO addr to pipehandle
	pipe = ffaddr2pipehdl(pdvobj, addr);	

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
	_func_enter_;
	

	
	_func_exit_;
}

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 18))
static void usb_write_mem_complete(struct urb *purb)
#else
static void usb_write_mem_complete(struct urb *purb, struct pt_regs *regs)
#endif
{		
	_irqL irqL;
	_list	*head, *plist;
	struct io_queue *pio_q = (struct io_queue *)purb->context;
	struct intf_hdl *pintf = &(pio_q->intf);	
	struct intf_priv *pintfpriv = pintf->pintfpriv;	
	_adapter *padapter = (_adapter *)pintf->adapter;
	struct xmit_priv * pxmitpriv = &padapter->xmitpriv;
	struct dvobj_priv * pdvobjpriv = (struct dvobj_priv *)&padapter->dvobjpriv;
        struct usb_device       *pusbd = pdvobjpriv->pusbdev;
	
_func_enter_;

	RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("+usb_write_mem_complete\n"));

	if(padapter->bSurpriseRemoved || padapter->bDriverStopped)
	{
		RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("usb_write_mem_complete:bDriverStopped(%d) OR bSurpriseRemoved(%d)", padapter->bDriverStopped, padapter->bSurpriseRemoved));		
	}
	
	if(purb->status==0)
	{

	}
	else
	{
		printk("wm_comp: status:%d\n",purb->status);
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_write_mem_complete : purb->status(%d) != 0 \n", purb->status));
		
		if(purb->status == (-ESHUTDOWN))
		{
			RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_write_mem_complete: ESHUTDOWN\n"));
			
			padapter->bDriverStopped=_TRUE;
			
			RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_write_mem_complete:bDriverStopped=TRUE\n"));
			
		}
		else if(purb->status==-EPIPE||purb->status == -EPROTO)
		{

			//printk("wm_comp: work around for pipe error (%d)!\n", purb->status);

			//if(purb->pipe == usb_sndbulkpipe(pusbd, 0x04))
			//	_set_workitem(&pxmitpriv->xmit_pipe4_reset_wi);
			//if(purb->pipe == usb_sndbulkpipe(pusbd, 0x06))
			//	_set_workitem(&pxmitpriv->xmit_pipe6_reset_wi);			
			//if(purb->pipe == usb_sndbulkpipe(pusbd, 0x0d))
			//	_set_workitem(&pxmitpriv->xmit_piped_reset_wi);
		}
		else
		{			
			padapter->bSurpriseRemoved=_TRUE;
			
			RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_write_mem_complete:bSurpriseRemoved=TRUE\n"));
		}		

	}
	
	_up_sema(&pintfpriv->io_retevt);

	RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("-usb_write_mem_complete\n"));

_func_exit_;	

}
void usb_write_mem(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *wmem)
{
	_irqL	irqL;
	int status, pipe;
	struct io_req *pio_req=NULL;	
	_adapter *padapter = (_adapter *)pintfhdl->adapter;
	struct intf_priv *pintfpriv = pintfhdl->pintfpriv;
	struct io_queue *pio_queue = (struct io_queue *)padapter->pio_queue;
	struct dvobj_priv *pdvobj = (struct dvobj_priv *)pintfpriv->intf_dev;
	struct usb_device *pusbd = pdvobj->pusbdev;
	PURB piorw_urb = pintfpriv->piorw_urb;
	
_func_enter_;	

	RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("+usb_write_mem\n"));

	if((padapter->bDriverStopped) || (padapter->bSurpriseRemoved) ||(padapter->pwrctrlpriv.pnp_bstop_trx))
	{
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_write_mem:( padapter->bDriverStopped ||padapter->bSurpriseRemoved ||adapter->pwrctrlpriv.pnp_bstop_trx)!!!\n"));
		goto exit;
	}

	//translate DMA FIFO addr to pipehandle
	pipe = ffaddr2pipehdl(pdvobj, addr);
	if(pipe==0)
	{
	   RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_write_mem, pipe=%x\n", pipe));	
		goto exit;
	}
	
	usb_fill_bulk_urb(piorw_urb, pusbd, pipe, 
       			    wmem,
              		    cnt,
              		    usb_write_mem_complete,
              		    pio_queue);
	

        //piorw_urb->transfer_flags |= URB_ZERO_PACKET;
      
	status = usb_submit_urb(piorw_urb, GFP_ATOMIC);

	if (!status)
	{		
		
	}
	else
	{
		//TODO:		
		RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,("usb_write_mem(): usb_submit_urb err, status=%x\n", status));
	}

	_down_sema(&pintfpriv->io_retevt);
	
	RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("-usb_write_mem\n"));
	
exit:
	
_func_exit_;
	
}

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 18))
static void usb_read_port_complete(struct urb *purb)
#else
static void usb_read_port_complete(struct urb *purb, struct pt_regs *regs)
#endif
{
	_irqL irqL;
	uint isevt, *pbuf;
	struct recv_buf	*precvbuf = (struct recv_buf *)purb->context;	
	_adapter 			*padapter =(_adapter *)precvbuf->adapter;
	struct recv_priv	*precvpriv = &padapter->recvpriv;	
	
	RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port_complete!!!\n"));
	
	//Useless for linux usb driver.
	//2010-03-10 by Thomas
	//_enter_critical(&precvpriv->lock, &irqL);
	//precvbuf->irp_pending=_FALSE;
	//precvpriv->rx_pending_cnt --;
	//_exit_critical(&precvpriv->lock, &irqL);

	//if(precvpriv->rx_pending_cnt== 0)
	//{		
	//	RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port_complete: rx_pending_cnt== 0, set allrxreturnevt!\n"));
	//	_up_sema(&precvpriv->allrxreturnevt);
	//}

	if(padapter->bSurpriseRemoved ||padapter->bDriverStopped)
	{
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port_complete:bDriverStopped(%d) OR bSurpriseRemoved(%d)\n", padapter->bDriverStopped, padapter->bSurpriseRemoved));
		goto exit;
	}

	if(purb->status==0)//SUCCESS
	{
		if((purb->actual_length>(MAX_RECVBUF_SZ)) || (purb->actual_length < RXDESC_SIZE)) 
		{
			RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port_complete: (purb->actual_length > MAX_RECVBUF_SZ) || (purb->actual_length < RXDESC_SIZE)\n"));
			precvbuf->reuse = _TRUE;
			read_port(padapter, precvpriv->ff_hwaddr, 0, (unsigned char *)precvbuf);
		}
		else 
		{	
			precvbuf->transfer_len = purb->actual_length;

			pbuf = (uint*)precvbuf->pbuf;

			if((isevt = le32_to_cpu(*(pbuf+1))&0x1ff) == 0x1ff)
			{				
				//_irqL  irqL;
					
				//_enter_critical( &padapter->lockRxFF0Filter, &irqL );
				//if ( padapter->blnEnableRxFF0Filter )
				//{
				//	padapter->blnEnableRxFF0Filter = 0;
				//}
				//_exit_critical( &padapter->lockRxFF0Filter, &irqL );
					
				//MSG_8712("usb_read_port_complete():rxcmd_event_hdl\n");

				rxcmd_event_hdl(padapter, pbuf);//rx c2h events

				precvbuf->reuse = _TRUE;

				read_port(padapter, precvpriv->ff_hwaddr, 0, (unsigned char *)precvbuf);
			}
			else
			{
	#ifdef CONFIG_RECV_TASKLET
					
				_pkt *pskb = precvbuf->pskb;				

				skb_put(pskb, purb->actual_length);	
				skb_queue_tail(&precvpriv->rx_skb_queue, pskb);
					
				tasklet_hi_schedule(&precvpriv->recv_tasklet);

				precvbuf->pskb = NULL;
				precvbuf->reuse = _FALSE;
				read_port(padapter, precvpriv->ff_hwaddr, 0, (unsigned char *)precvbuf);
	#else
				if(recvbuf2recvframe(padapter, precvbuf)==_FAIL)//rx packets
				{
					precvbuf->reuse = _TRUE;		
					read_port(padapter, precvpriv->ff_hwaddr, 0, (unsigned char *)precvbuf);
				}
	#endif
			}
		}	
		
	}
	else
	{
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port_complete : purb->status(%d) != 0 \n", purb->status));
		printk( "[%s] purb->status(%d) != 0\n", __FUNCTION__, purb->status );
		switch(purb->status) {
			case -EINVAL:
			case -EPIPE:			
			case -ENODEV:
			case -ESHUTDOWN:
				//padapter->bSurpriseRemoved=_TRUE;
				RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port_complete:bSurpriseRemoved=TRUE\n"));
			case -ENOENT:
				padapter->bDriverStopped=_TRUE;
				RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port_complete:bDriverStopped=TRUE\n"));
				break;
			case -EPROTO:
				precvbuf->reuse = _TRUE;
				read_port(padapter, precvpriv->ff_hwaddr, 0, (unsigned char *)precvbuf);
				break;
			case -EINPROGRESS:
				printk("ERROR: URB IS IN PROGRESS!/n");
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
	int err, pipe;
	u32 tmpaddr=0;
        int alignment=0;
	u32 ret = _SUCCESS;
	PURB purb = NULL;	
	struct recv_buf	*precvbuf = (struct recv_buf *)rmem;
	struct intf_priv	*pintfpriv = pintfhdl->pintfpriv;
	struct dvobj_priv	*pdvobj = (struct dvobj_priv *)pintfpriv->intf_dev;
	_adapter			*adapter = (_adapter *)pdvobj->padapter;
	struct recv_priv	*precvpriv = &adapter->recvpriv;
	struct usb_device	*pusbd = pdvobj->pusbdev;
	

_func_enter_;
	
	if(adapter->bDriverStopped || adapter->bSurpriseRemoved ||adapter->pwrctrlpriv.pnp_bstop_trx)
	{
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port:( padapter->bDriverStopped ||padapter->bSurpriseRemoved ||adapter->pwrctrlpriv.pnp_bstop_trx)!!!\n"));
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
	

	if(precvbuf !=NULL)
	{	
		init_recvbuf(adapter, precvbuf);		

		//re-assign for linux based on skb
		if((precvbuf->reuse == _FALSE) || (precvbuf->pskb == NULL))
		{
			//precvbuf->pskb = alloc_skb(MAX_RECVBUF_SZ, GFP_ATOMIC);//don't use this after v2.6.25
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18)) // http://www.mail-archive.com/netdev@vger.kernel.org/msg17214.html
			precvbuf->pskb = dev_alloc_skb(MAX_RECVBUF_SZ + RECVBUFF_ALIGN_SZ);
#else			
			precvbuf->pskb = netdev_alloc_skb(adapter->pnetdev, MAX_RECVBUF_SZ + RECVBUFF_ALIGN_SZ);
#endif			
			if(precvbuf->pskb == NULL)		
			{
				RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("init_recvbuf(): alloc_skb fail!\n"));
				return _FAIL;
			}	

			tmpaddr = (u32)precvbuf->pskb->data;
	        	alignment = tmpaddr & (RECVBUFF_ALIGN_SZ-1);
	       	        skb_reserve(precvbuf->pskb, (RECVBUFF_ALIGN_SZ - alignment));			

			precvbuf->phead = precvbuf->pskb->head;
			precvbuf->pdata = precvbuf->pskb->data;
			precvbuf->ptail = precvbuf->pskb->tail;
			precvbuf->pend = precvbuf->pskb->end;

       		precvbuf->pbuf = precvbuf->pskb->data;		
		
		}	
		else//reuse skb
		{
			precvbuf->phead = precvbuf->pskb->head;
			precvbuf->pdata = precvbuf->pskb->data;
			precvbuf->ptail = precvbuf->pskb->tail;
			precvbuf->pend = precvbuf->pskb->end;

       		precvbuf->pbuf = precvbuf->pskb->data;
		
			precvbuf->reuse = _FALSE;
		}
	
		//Useless for linux usb driver.
		//2010-03-10 by Thomas
		//_enter_critical(&precvpriv->lock, &irqL);
		//precvpriv->rx_pending_cnt++;
		//precvbuf->irp_pending = _TRUE;
		//_exit_critical(&precvpriv->lock, &irqL);
		
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
			ret = _FAIL;
		}
	}
	else
	{
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port:precvbuf ==NULL\n"));
		ret = _FAIL;
	}

_func_exit_;
	
	return ret;
	
}

void usb_read_port_cancel(_adapter *padapter)
{
	int i;
	struct recv_buf *precvbuf;

	precvbuf = (struct recv_buf *)padapter->recvpriv.precv_buf;

	RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("+usb_read_port_cancel\n"));

	for(i=0; i < NR_RECVBUFF ; i++)
	{
		precvbuf->reuse == _TRUE;
		if(precvbuf->purb)
		{
			usb_kill_urb(precvbuf->purb);
		}
		precvbuf++;	
	}

	RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("-usb_read_port_cancel\n"));
}

void xmit_bh(void *priv)
{	
	int ret = _FALSE;
	_adapter *padapter = (_adapter*)priv;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	
	//while(1)
	//{
		if ((padapter->bDriverStopped == _TRUE)||(padapter->bSurpriseRemoved== _TRUE))
		{
			printk("xmit_bh => bDriverStopped or bSurpriseRemoved \n");
			return;
			//break;
		}

		ret = xmitframe_complete(padapter, pxmitpriv, NULL);

		if(ret==_FALSE)
			return;
			//break;
		else
			tasklet_hi_schedule(&pxmitpriv->xmit_tasklet);
	//}
}


#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 18))
static void usb_write_port_complete(struct urb *purb)
#else
static void usb_write_port_complete(struct urb *purb, struct pt_regs *regs)
#endif
{
	_irqL irqL;
	int i;
	struct xmit_frame	*pxmitframe = (struct xmit_frame *)purb->context;
	struct xmit_buf *pxmitbuf = pxmitframe->pxmitbuf;
	_adapter			*padapter = pxmitframe->padapter;		
	struct xmit_priv	*pxmitpriv = &padapter->xmitpriv;		
	struct pkt_attrib *pattrib = &pxmitframe->attrib;

	struct dvobj_priv * pdvobjpriv = (struct dvobj_priv *)&padapter->dvobjpriv;
	struct usb_device *pusbd=pdvobjpriv->pusbdev;	   

_func_enter_;

	RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("+usb_write_port_complete\n"));

	//_enter_critical(&pxmitpriv->lock, &irqL);
	
	switch(pattrib->priority) 
	{
		case 1:				
		case 2:
			pxmitpriv->bkq_cnt--;
			//printk("pxmitpriv->bkq_cnt=%d\n", pxmitpriv->bkq_cnt);
			break;
		case 4:
		case 5:
			pxmitpriv->viq_cnt--;
			//printk("pxmitpriv->viq_cnt=%d\n", pxmitpriv->viq_cnt);
			break;
		case 6:
		case 7:
			pxmitpriv->voq_cnt--;
			//printk("pxmitpriv->voq_cnt=%d\n", pxmitpriv->voq_cnt);
			break;
		case 0:
		case 3:			
		default:
			pxmitpriv->beq_cnt--;
			//printk("pxmitpriv->beq_cnt=%d\n", pxmitpriv->beq_cnt);
			break;
			
	}
	
	pxmitpriv->txirp_cnt--;

	for(i=0; i< 8; i++)
	{
            if(purb == pxmitframe->pxmit_urb[i])
            {
		    pxmitframe->bpending[i] = _FALSE;//		  
		    break;		  
            }
	}
	
	//_exit_critical(&pxmitpriv->lock, &irqL);
	
	//Useless for linux usb driver.
	//2010-03-10 by Thomas
	//if(pxmitpriv->txirp_cnt==0)
	//{
	//	RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_write_port_complete: txirp_cnt== 0, set allrxreturnevt!\n"));		
	//	_up_sema(&(pxmitpriv->tx_retevt));
	//}
	
	if(padapter->bSurpriseRemoved)
	{
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_write_port_complete:bDriverStopped(%d) OR bSurpriseRemoved(%d)", padapter->bDriverStopped, padapter->bSurpriseRemoved));
		goto exit;
	}

	if(purb->status==0||purb->status==-EPIPE||purb->status==-EPROTO)
	{
		if(purb->status == -EPIPE|| purb->status == -EPROTO)
		{
			printk("wp_comp: work around for pipe error (%d)!\n", purb->status);

			//if(purb->pipe == usb_sndbulkpipe(pusbd, 0x04))
			//	_set_workitem(&pxmitpriv->xmit_pipe4_reset_wi);
			//if(purb->pipe == usb_sndbulkpipe(pusbd, 0x06))
			//	_set_workitem(&pxmitpriv->xmit_pipe6_reset_wi);			
			//if(purb->pipe == usb_sndbulkpipe(pusbd, 0x0d))
			//	_set_workitem(&pxmitpriv->xmit_piped_reset_wi);
			
			//usb_clear_halt(pusbdev,purb->pipe);

			//int result;
			//result = usb_control_msg(pusbdev, purb->pipe,
          		//	USB_REQ_CLEAR_FEATURE, USB_RECIP_ENDPOINT,
			//	USB_ENDPOINT_HALT, usb_pipeendpoint(purb->pipe),
			//	NULL, 0, 10*HZ);
		
		        /* reset the endpoint toggle */
        		//if (result >= 0)
			//	usb_settoggle(pusbdev, usb_pipeendpoint(purb->pipe),
			//		usb_pipeout(purb->pipe), 0);	

			//msleep(10);
		}
	}
	else
	{
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_write_port_complete : purb->status(%d) != 0 \n", purb->status));
		/*if(purb->status == (-ESHUTDOWN))
		{
			RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_write_port_complete: ESHUTDOWN\n"));
			
			padapter->bDriverStopped=_TRUE;
			
			RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_write_port_complete:bDriverStopped=TRUE\n"));
			
		}
		else
		{			
			padapter->bSurpriseRemoved=_TRUE;
			
			RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_write_port_complete:bSurpriseRemoved=TRUE\n"));
		}		

		goto exit;*/
	}	


#if 0	
	pxmitframe->fragcnt--;
	if(pxmitframe->fragcnt == 0)// if((pxmitframe->fragcnt == 0) && (pxmitframe->irpcnt == 8)){
	{
		//RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("\n usb_write_port_complete:pxmitframe->fragcnt == 0\n"));
		free_xmitframe(pxmitpriv,pxmitframe);	          
      	}
#else	

	//not to consider tx fragment
	free_xmitframe_ex(pxmitpriv, pxmitframe);		

#endif	

#ifdef CONFIG_XMIT_BH
	free_xmitbuf(pxmitpriv, pxmitbuf);
	tasklet_hi_schedule(&pxmitpriv->xmit_tasklet);
#else
	xmitframe_complete(padapter, pxmitpriv, pxmitbuf);
#endif

	RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("-usb_write_port_complete\n"));

exit:

_func_exit_;	

}


u32 usb_write_port(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *wmem)
{    
	_irqL irqL;
	int i, pipe, status;
	u32 ret, bwritezero;
	PURB	purb = NULL;
	_adapter *padapter = (_adapter *)pintfhdl->adapter;
	struct dvobj_priv	*pdvobj = (struct dvobj_priv   *)&padapter->dvobjpriv;	
	struct xmit_priv	*pxmitpriv = &padapter->xmitpriv;
	struct xmit_frame *pxmitframe = (struct xmit_frame *)wmem;
	struct usb_device *pusbd = pdvobj->pusbdev;
	struct pkt_attrib *pattrib = &pxmitframe->attrib;
	
_func_enter_;	
	
	RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("+usb_write_port\n"));
	
	if((padapter->bDriverStopped) || (padapter->bSurpriseRemoved) ||(padapter->pwrctrlpriv.pnp_bstop_trx))
	{
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_write_port:( padapter->bDriverStopped ||padapter->bSurpriseRemoved ||adapter->pwrctrlpriv.pnp_bstop_trx)!!!\n"));
		return _FAIL;
	}
	
	for(i=0; i<8; i++)
       {
		if(pxmitframe->bpending[i] == _FALSE)
		{
			_enter_critical(&pxmitpriv->lock, &irqL);

			pxmitpriv->txirp_cnt++;
			pxmitframe->bpending[i]  = _TRUE;

			switch(pattrib->priority) 
			{
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

			_exit_critical(&pxmitpriv->lock, &irqL);
			
			pxmitframe->sz[i] = (u16)cnt;
			purb	= pxmitframe->pxmit_urb[i];		
			
			break;	 
		}
		
       }	

	bwritezero = _FALSE;
	if(pdvobj->ishighspeed)
	{
		if(cnt> 0 && cnt%512 == 0)
		{
			//printk("ishighspeed, cnt=%d\n", cnt);
			bwritezero = _TRUE;
		}	
	}
	else
	{
		if(cnt > 0 && cnt%64 == 0)
		{
			//printk("cnt=%d\n", cnt);
			bwritezero = _TRUE;
		}	
	}
	
	//translate DMA FIFO addr to pipehandle
	pipe = ffaddr2pipehdl(pdvobj, addr);	

#ifdef CONFIG_REDUCE_USB_TX_INT
	//if ( (pxmitpriv->free_xmitbuf_cnt%2  == 0))
	if ( pxmitpriv->free_xmitbuf_cnt%NR_XMITBUFF == 0 )
	{
		purb->transfer_flags  &=  (~URB_NO_INTERRUPT);
	} else {
		purb->transfer_flags  |=  URB_NO_INTERRUPT;
		//printk("URB_NO_INTERRUPT ");
	}
#endif

	if ( bwritezero )
	{
		cnt += 8;
	}
	   
	usb_fill_bulk_urb(purb, pusbd, pipe, 
       				pxmitframe->mem_addr,
              			cnt,
              			usb_write_port_complete,
              			pxmitframe);//context is xmit_frame

	status = usb_submit_urb(purb, GFP_ATOMIC);

	if (!status)
	{		
		ret= _SUCCESS;
	}
	else
	{
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_write_port(): usb_submit_urb, status=%x\n", status));
		ret= _FAIL;
	}
//   Commented by Albert 2009/10/13
//   We add the URB_ZERO_PACKET flag to urb so that the host will send the zero packet automatically.
/*	
	if(bwritezero == _TRUE)
	{
		usb_bulkout_zero(pintfhdl, addr);
	}
*/

_func_exit_;
	
	RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("-usb_write_port\n"));
	
	return ret;

}

void usb_write_port_cancel(_adapter *padapter)
{
	int i,j;
	struct xmit_buf	*pxmitbuf = (struct xmit_buf *)padapter->xmitpriv.pxmitbuf;

	for(i=0; i<NR_XMITBUFF; i++)
	{
		for(j=0; j<8; j++)
		{
		        if(pxmitbuf->pxmit_urb[j])
		        {
		                usb_kill_urb(pxmitbuf->pxmit_urb[j]);
		        }
		}
		
		pxmitbuf++;
	}
}

int usbctrl_vendorreq(struct intf_priv *pintfpriv, u8 request, u16 value, u16 index, void *pdata, u16 len, u8 requesttype)
{
	unsigned int pipe;
	int status, intretry;
	u8 reqtype;
	struct dvobj_priv *pdvobjpriv = ( struct dvobj_priv *) pintfpriv->intf_dev;
	struct usb_device *udev=pdvobjpriv->pusbdev;
		
	// Added by Albert 2010/02/09
	// For mstar platform, mstar suggests the address for USB IO should be 16 bytes alignment.
	// Trying to fix it here.

	u8 *palloc_buf, *pIo_buf;

	palloc_buf = _malloc( (u32) len + 16);
	
	if ( palloc_buf== NULL)
	{
		printk( "[%s] Can't alloc memory for vendor request\n", __FUNCTION__ );
		return(-1);
	}
	
	pIo_buf = palloc_buf + 16 -((uint)(palloc_buf) & 0x0f );
	
	if (requesttype == 0x01)
	{
		pipe = usb_rcvctrlpipe(udev, 0);//read_in
		reqtype =  RTL871X_VENQT_READ;		
	} 
	else 
	{
		pipe = usb_sndctrlpipe(udev, 0);//write_out
		reqtype =  RTL871X_VENQT_WRITE;		
		_memcpy( pIo_buf, pdata, len);
	}

	intretry = 0;
retry:
	status = usb_control_msg(udev, pipe, request, reqtype, value, index, pIo_buf, len, RTW_USB_CONTROL_MSG_TIMEOUT );
	
	if (status < 0)
       {
		printk("retry = %d, reg 0x%x, usb read/write TimeOut! status:%d value=0x%x\n", intretry, value, status, *(u32*)pdata);
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("reg 0x%x, usb_read8 TimeOut! status:0x%x value=0x%x\n", value, status, *(u32*)pdata));
		intretry++;
		if ( intretry < RTW_USB_CONTROL_RETRY_CNT )
		{
			goto retry;
		}
       }
	else if ( status > 0 )   // Success this control transfer.
	{
               if ( requesttype == 0x01 )
               {   // For Control read transfer, we have to copy the read data from pIo_buf to pdata.
                       _memcpy( pdata, pIo_buf,  status );
               }
	}

	_mfree( palloc_buf, (u32) len + 16 );

	return status;

}


