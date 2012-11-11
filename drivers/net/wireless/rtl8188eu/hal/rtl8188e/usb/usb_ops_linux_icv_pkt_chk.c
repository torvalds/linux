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
#include <usb_ops.h>
#include <circ_buf.h>
#include <recv_osdep.h>
#include <rtl8188e_hal.h>

#if defined (PLATFORM_LINUX) && defined (PLATFORM_WINDOWS)

#error "Shall be Linux or Windows, but not both!\n"

#endif

struct zero_bulkout_context{
	void *pbuf;
	void *purb;
	void *pirp;
	void *padapter;
};

#define REALTEK_USB_VENQT_MAX_BUF_SIZE	254

#define RTW_USB_CONTROL_MSG_TIMEOUT_TEST	10	//ms
#define RTW_USB_CONTROL_MSG_TIMEOUT	500		//ms
//#define RTW_USB_CONTROL_MSG_TIMEOUT	5000	//ms

#if defined(CONFIG_VENDOR_REQ_RETRY) && defined(CONFIG_USB_VENDOR_REQ_MUTEX)
//vendor req retry should be in the situation when each vendor req is atomically submitted from others
#define MAX_USBCTRL_VENDORREQ_TIMES 10
#else
#define MAX_USBCTRL_VENDORREQ_TIMES 1
#endif

#define RTW_USB_BULKOUT_TIMEOUT 5000 //ms

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)) || (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,18))
#define _usbctrl_vendorreq_async_callback(urb, regs)	_usbctrl_vendorreq_async_callback(urb)
#define usb_bulkout_zero_complete(purb, regs)	usb_bulkout_zero_complete(purb)
#define usb_write_mem_complete(purb, regs)	usb_write_mem_complete(purb)
#define usb_write_port_complete(purb, regs)	usb_write_port_complete(purb)
#define usb_read_port_complete(purb, regs)	usb_read_port_complete(purb)
#define usb_read_interrupt_complete(purb, regs)	usb_read_interrupt_complete(purb)
#endif

static int usbctrl_vendorreq(struct dvobj_priv  *pdvobjpriv, u8 request, u16 value, u16 index, void *pdata, u16 len, u8 requesttype)
{
	_adapter		*padapter = pdvobjpriv->padapter ; 
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
		pdvobjpriv = &padapter->dvobjpriv;
		udev = pdvobjpriv->pusbdev;
	}
#endif

	//DBG_871X("%s %s:%d\n",__FUNCTION__, current->comm, current->pid);

	if((padapter->bSurpriseRemoved) ||(padapter->pwrctrlpriv.pnp_bstop_trx)){
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usbctrl_vendorreq:(padapter->bSurpriseRemoved ||adapter->pwrctrlpriv.pnp_bstop_trx)!!!\n"));
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
	
		#if 0
		//timeout test for firmware downloading
		status = rtw_usb_control_msg(udev, pipe, request, reqtype, value, index, pIo_buf, len
			, (value == FW_8188E_START_ADDRESS) ?RTW_USB_CONTROL_MSG_TIMEOUT_TEST : RTW_USB_CONTROL_MSG_TIMEOUT
		);
		#else
		status = rtw_usb_control_msg(udev, pipe, request, reqtype, value, index, pIo_buf, len, RTW_USB_CONTROL_MSG_TIMEOUT);
		#endif
	
		if ( status == len)   // Success this control transfer.
		{
			rtw_reset_continual_urb_error(&padapter->dvobjpriv);
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

			if(rtw_inc_and_chk_continual_urb_error(&padapter->dvobjpriv) == _TRUE ){
				padapter->bSurpriseRemoved = _TRUE;
				break;
			}
	
		}
	
		// firmware download is checksumed, don't retry
		if( (value >= FW_8188E_START_ADDRESS && value <= FW_8188E_END_ADDRESS) || status == len )
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

static u8 usb_read8(struct intf_hdl *pintfhdl, u32 addr)
{
	u8 request;
	u8 requesttype;
	u16 wvalue;
	u16 index;
	u16 len;
	u8 data=0;	
	struct dvobj_priv  *pdvobjpriv = (struct dvobj_priv  *)pintfhdl->pintf_dev;   
	
	_func_enter_;

	request = 0x05;
	requesttype = 0x01;//read_in
	index = 0;//n/a

	wvalue = (u16)(addr&0x0000ffff);
	len = 1;	
	
	usbctrl_vendorreq(pdvobjpriv, request, wvalue, index, &data, len, requesttype);

	_func_exit_;

	return data;
		
}

static u16 usb_read16(struct intf_hdl *pintfhdl, u32 addr)
{       
	u8 request;
	u8 requesttype;
	u16 wvalue;
	u16 index;
	u16 len;
	u16 data=0;
	struct dvobj_priv  *pdvobjpriv = (struct dvobj_priv  *)pintfhdl->pintf_dev;   
	
	_func_enter_;

	request = 0x05;
	requesttype = 0x01;//read_in
	index = 0;//n/a

	wvalue = (u16)(addr&0x0000ffff);
	len = 2;	
	
	usbctrl_vendorreq(pdvobjpriv, request, wvalue, index, &data, len, requesttype);

	_func_exit_;

	return data;
	
}

static u32 usb_read32(struct intf_hdl *pintfhdl, u32 addr)
{
	u8 request;
	u8 requesttype;
	u16 wvalue;
	u16 index;
	u16 len;
	u32 data=0;
	struct dvobj_priv  *pdvobjpriv = (struct dvobj_priv  *)pintfhdl->pintf_dev;  
	
	_func_enter_;

	request = 0x05;
	requesttype = 0x01;//read_in
	index = 0;//n/a

	wvalue = (u16)(addr&0x0000ffff);
	len = 4;	
	
	usbctrl_vendorreq(pdvobjpriv, request, wvalue, index, &data, len, requesttype);

	_func_exit_;

	return data;
	
}

static int usb_write8(struct intf_hdl *pintfhdl, u32 addr, u8 val)
{
	u8 request;
	u8 requesttype;
	u16 wvalue;
	u16 index;
	u16 len;
	u8 data;
	int ret;
	struct dvobj_priv  *pdvobjpriv = (struct dvobj_priv  *)pintfhdl->pintf_dev;   
	
	_func_enter_;

	request = 0x05;
	requesttype = 0x00;//write_out
	index = 0;//n/a

	wvalue = (u16)(addr&0x0000ffff);
	len = 1;
	
	data = val;	
	
	 ret = usbctrl_vendorreq(pdvobjpriv, request, wvalue, index, &data, len, requesttype);
	
	_func_exit_;
	
	return ret;
	
}

static int usb_write16(struct intf_hdl *pintfhdl, u32 addr, u16 val)
{	
	u8 request;
	u8 requesttype;
	u16 wvalue;
	u16 index;
	u16 len;
	u16 data;
	int ret;
	struct dvobj_priv  *pdvobjpriv = (struct dvobj_priv  *)pintfhdl->pintf_dev;   
	
	_func_enter_;

	request = 0x05;
	requesttype = 0x00;//write_out
	index = 0;//n/a

	wvalue = (u16)(addr&0x0000ffff);
	len = 2;
	
	data = val;
		
	ret = usbctrl_vendorreq(pdvobjpriv, request, wvalue, index, &data, len, requesttype);
	
	_func_exit_;
	
	return ret;
	
}

static int usb_write32(struct intf_hdl *pintfhdl, u32 addr, u32 val)
{
	u8 request;
	u8 requesttype;
	u16 wvalue;
	u16 index;
	u16 len;
	u32 data;
	int ret;
	struct dvobj_priv  *pdvobjpriv = (struct dvobj_priv  *)pintfhdl->pintf_dev;   
	
	_func_enter_;

	request = 0x05;
	requesttype = 0x00;//write_out
	index = 0;//n/a

	wvalue = (u16)(addr&0x0000ffff);
	len = 4;
	data =val;		

	ret =usbctrl_vendorreq(pdvobjpriv, request, wvalue, index, &data, len, requesttype);
	
	_func_exit_;
	
	return ret;
	
}
#define VENDOR_CMD_MAX_DATA_LEN	254
static int usb_writeN(struct intf_hdl *pintfhdl, u32 addr, u32 length, u8 *pdata)
{
	u8 request;
	u8 requesttype;
	u16 wvalue;
	u16 index;
	u16 len;
	u8 buf[VENDOR_CMD_MAX_DATA_LEN]={0};
	int ret;
	struct dvobj_priv  *pdvobjpriv = (struct dvobj_priv  *)pintfhdl->pintf_dev;  
	
	_func_enter_;

	request = 0x05;
	requesttype = 0x00;//write_out
	index = 0;//n/a

	wvalue = (u16)(addr&0x0000ffff);
	len = length;
	 _rtw_memcpy(buf, pdata, len );
	
	ret = usbctrl_vendorreq(pdvobjpriv, request, wvalue, index, buf, len, requesttype);
	
	_func_exit_;
	
	return ret;
	
}

#ifdef CONFIG_USB_SUPPORT_ASYNC_VDN_REQ
static void _usbctrl_vendorreq_async_callback(struct urb *urb, struct pt_regs *regs)
{
	if(urb){
		if(urb->context){
			kfree(urb->context);
		}
		usb_free_urb(urb);
	}
}

static int _usbctrl_vendorreq_async_write(struct usb_device *udev, u8 request, u16 value, u16 index, void *pdata, u16 len, u8 requesttype)
{

	int rc;
	unsigned int pipe;	
	u8 reqtype;
	struct usb_ctrlrequest *dr;
	struct urb *urb;
	struct rtl819x_async_write_data {
		u8 data[REALTEK_USB_VENQT_MAX_BUF_SIZE];
		struct usb_ctrlrequest dr;
	} *buf;	
				
	if (requesttype == VENDOR_READ){
		pipe = usb_rcvctrlpipe(udev, 0);//read_in
		reqtype =  REALTEK_USB_VENQT_READ;		
	} 
	else {
		pipe = usb_sndctrlpipe(udev, 0);//write_out
		reqtype =  REALTEK_USB_VENQT_WRITE;		
	}		
	
	//buf = kmalloc(sizeof(*buf), GFP_ATOMIC);
	buf = (struct rtl819x_async_write_data *)rtw_zmalloc(sizeof(*buf));
	if (!buf) {
		rc = -ENOMEM;
		goto exit;
	}

	urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (!urb) {
		rtw_mfree((u8*)buf,sizeof(*buf));
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

	usb_fill_control_urb(urb, udev, pipe,
			     (unsigned char *)dr, buf, len,
			     _usbctrl_vendorreq_async_callback, buf);

	rc = usb_submit_urb(urb, GFP_ATOMIC);
	if (rc < 0) {
		rtw_mfree((u8*)buf,sizeof(*buf));
		usb_free_urb(urb);
	}
	
exit:
	return rc;

}

static int usb_write_async(struct usb_device *udev, u32 addr, void *pdata, u16 len)
{
	u8 request;
	u8 requesttype;
	u16 wvalue;
	u16 index;	
	int ret;
	
	requesttype = VENDOR_WRITE;//write_out	
	request = REALTEK_USB_VENQT_CMD_REQ;
	index = REALTEK_USB_VENQT_CMD_IDX;//n/a

	wvalue = (u16)(addr&0x0000ffff);
	
	
	ret = _usbctrl_vendorreq_async_write(udev, request, wvalue, index, pdata, len, requesttype);

	return ret;
}
static int usb_async_write8(struct intf_hdl *pintfhdl, u32 addr, u8 val)
{	
	u8 data;
	int ret;
	struct dvobj_priv  *pdvobjpriv = (struct dvobj_priv  *)pintfhdl->pintf_dev;   
	struct usb_device *udev=pdvobjpriv->pusbdev;

	_func_enter_;
	data = val;	
	ret = usb_write_async(udev, addr, &data, 1);
	_func_exit_;	

	return ret;
}

static int usb_async_write16(struct intf_hdl *pintfhdl, u32 addr, u16 val)
{	
	u16 data;
	int ret;
	struct dvobj_priv  *pdvobjpriv = (struct dvobj_priv  *)pintfhdl->pintf_dev;   
	struct usb_device *udev=pdvobjpriv->pusbdev;

	_func_enter_;
	data = val;	
	ret = usb_write_async(udev, addr, &data, 2);
	_func_exit_;	

	return ret;
}
static int usb_async_write32(struct intf_hdl *pintfhdl, u32 addr, u32 val)
{	
	u32 data;
	int ret;
	struct dvobj_priv  *pdvobjpriv = (struct dvobj_priv  *)pintfhdl->pintf_dev;   
	struct usb_device *udev=pdvobjpriv->pusbdev;

	_func_enter_;
	data = val;	
	ret = usb_write_async(udev, addr, &data, 4);
	_func_exit_;	

	return ret;
}
#endif


static unsigned int ffaddr2pipehdl(struct dvobj_priv *pdvobj, u32 addr)
{
	unsigned int pipe=0;
	int ep_num=0;
	_adapter *padapter = pdvobj->padapter;
	struct usb_device *pusbd = pdvobj->pusbdev;	
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(padapter);

	if(addr == RECV_BULK_IN_ADDR)
	{	
		pipe=usb_rcvbulkpipe(pusbd, pHalData->RtBulkInPipe);

		return pipe;
	}

	if(addr == RECV_INT_IN_ADDR)
	{	
		pipe=usb_rcvbulkpipe(pusbd, pHalData->RtIntInPipe);

		return pipe;
	}

	if(addr < HW_QUEUE_ENTRY) 
	{
		ep_num = (pHalData->Queue2EPNum[(u8)addr] & 0x0f);
		
		pipe = usb_sndbulkpipe(pusbd, ep_num);

		return pipe;
	}

	return pipe;

}

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
	struct dvobj_priv *pdvobj = (struct dvobj_priv *)&padapter->dvobjpriv;	
	struct usb_device *pusbd = pdvobj->pusbdev;

	//DBG_8192C("+usb_bulkout_zero\n");
	
		
	if((padapter->bDriverStopped) || (padapter->bSurpriseRemoved) ||(padapter->pwrctrlpriv.pnp_bstop_trx))
	{		
		return _FAIL;
	}
	

	pcontext = (struct zero_bulkout_context *)rtw_zmalloc(sizeof(struct zero_bulkout_context));

	pbuf = (unsigned char *)rtw_zmalloc(sizeof(int));	
    	purb = usb_alloc_urb(0, GFP_ATOMIC);
      	
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

static void usb_read_mem(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *rmem)
{
	
}

static void usb_write_mem(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *wmem)
{
	
}
#ifdef CONFIG_SUPPORT_USB_INT
void interrupt_handler_8188eu(_adapter *padapter,u16 pkt_len,u8 *pbuf)
{	
	HAL_DATA_TYPE	*pHalData=GET_HAL_DATA(padapter);
	struct reportpwrstate_parm pwr_rpt;
	
	if ( pkt_len != INTERRUPT_MSG_FORMAT_LEN )
	{
		DBG_8192C("%s Invalid interrupt content length (%d)!\n", __FUNCTION__, pkt_len);
		return ;
	}

	// HISR 
	_rtw_memcpy(&(pHalData->IntArray[0]), &(pbuf[USB_INTR_CONTENT_HISR_OFFSET]), 4);
	_rtw_memcpy(&(pHalData->IntArray[1]), &(pbuf[USB_INTR_CONTENT_HISRE_OFFSET]), 4);

	#if 0 //DBG
	{
		u32 hisr=0 ,hisr_ex=0;
		_rtw_memcpy(&hisr,&(pHalData->IntArray[0]),4);
		hisr = le32_to_cpu(hisr);	
		
		_rtw_memcpy(&hisr_ex,&(pHalData->IntArray[1]),4);
		hisr_ex = le32_to_cpu(hisr_ex);
		
		if((hisr != 0) || (hisr_ex!=0))
			DBG_871X("===> %s hisr:0x%08x ,hisr_ex:0x%08x \n",__FUNCTION__,hisr,hisr_ex);
	}
	#endif


#ifdef CONFIG_LPS_LCLK
	if(  pHalData->IntArray[0]  & IMR_CPWM_88E )
	{
		_rtw_memcpy(&pwr_rpt.state, &(pbuf[USB_INTR_CONTENT_CPWM1_OFFSET]), 1);
		//_rtw_memcpy(&pwr_rpt.state2, &(pbuf[USB_INTR_CONTENT_CPWM2_OFFSET]), 1);

		//88e's cpwm value only change BIT0, so driver need to add PS_STATE_S2 for LPS flow.		
		pwr_rpt.state |= PS_STATE_S2;		
		_set_workitem(&padapter->pwrctrlpriv.cpwm_event);
	}
#endif//CONFIG_LPS_LCLK

#ifdef CONFIG_INTERRUPT_BASED_TXBCN

	#ifdef  CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT
	if (pHalData->IntArray[0] & IMR_BCNDMAINT0_88E)
	#endif
	#ifdef  CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR
	if (pHalData->IntArray[0] & (IMR_TBDER_88E|IMR_TBDOK_88E))
	#endif	
	{		
		struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
		#if 0
		if(pHalData->IntArray[0] & IMR_BCNDMAINT0_88E)
			printk("%s: HISR_BCNERLY_INT\n", __func__);
		if(pHalData->IntArray[0] & IMR_TBDOK_88E)
			printk("%s: HISR_TXBCNOK\n", __func__);		
		if(pHalData->IntArray[0] & IMR_TBDER_88E)
			printk("%s: HISR_TXBCNERR\n", __func__);		
		#endif
		

		if(check_fwstate(pmlmepriv, WIFI_AP_STATE))
		{
			//send_beacon(padapter);
			if(pmlmepriv->update_bcn == _TRUE)
			{
				//tx_beacon_hdl(padapter, NULL);
				set_tx_beacon_cmd(padapter);
			}
		}
#ifdef CONFIG_CONCURRENT_MODE
		if(check_buddy_fwstate(padapter, WIFI_AP_STATE))
		{
			//send_beacon(padapter);
			if(padapter->pbuddy_adapter->mlmepriv.update_bcn == _TRUE)
			{
				//tx_beacon_hdl(padapter, NULL);
				set_tx_beacon_cmd(padapter->pbuddy_adapter);
			}
		}
#endif
		
	}
#endif //CONFIG_INTERRUPT_BASED_TXBCN




#ifdef DBG_CONFIG_ERROR_DETECT_INT
	if(  pHalData->IntArray[1]  & IMR_TXERR_88E )
		DBG_871X("===> %s Tx Error Flag Interrupt Status \n",__FUNCTION__);
	if(  pHalData->IntArray[1]  & IMR_RXERR_88E )
		DBG_871X("===> %s Rx Error Flag INT Status \n",__FUNCTION__);
	if(  pHalData->IntArray[1]  & IMR_TXFOVW_88E )
		DBG_871X("===> %s Transmit FIFO Overflow \n",__FUNCTION__);
	if(  pHalData->IntArray[1]  & IMR_RXFOVW_88E )
		DBG_871X("===> %s Receive FIFO Overflow \n",__FUNCTION__);	
#endif//DBG_CONFIG_ERROR_DETECT_INT


	// C2H Event 
	if(pbuf[0]!= 0){
		_rtw_memcpy(&(pHalData->C2hArray[0]), &(pbuf[USB_INTR_CONTENT_C2H_OFFSET]), 16);		
		//rtw_c2h_wk_cmd(padapter); to do..
	}		
		
}
#endif
				
#ifdef CONFIG_USB_INTERRUPT_IN_PIPE
static void usb_read_interrupt_complete(struct urb *purb, struct pt_regs *regs)
{
	int	err;
	_adapter		*padapter = (_adapter	 *)purb->context;

	if(padapter->bSurpriseRemoved || padapter->bDriverStopped||padapter->bReadPortCancel)
	{
		DBG_8192C("%s() RX Warning! bDriverStopped(%d) OR bSurpriseRemoved(%d) bReadPortCancel(%d)\n", 
		__FUNCTION__,padapter->bDriverStopped, padapter->bSurpriseRemoved,padapter->bReadPortCancel);

		return;
	}
		
	if(purb->status==0)//SUCCESS
	{
		if (purb->actual_length > INTERRUPT_MSG_FORMAT_LEN)
		{
			DBG_8192C("usb_read_interrupt_complete: purb->actual_length > INTERRUPT_MSG_FORMAT_LEN(%d)\n",INTERRUPT_MSG_FORMAT_LEN);			
		}

		interrupt_handler_8188eu(padapter, purb->actual_length,purb->transfer_buffer );
		
		err = usb_submit_urb(purb, GFP_ATOMIC);
		if((err) && (err != (-EPERM)))
		{
			DBG_8192C("cannot submit interrupt in-token(err = 0x%08x),urb_status = %d\n",err, purb->status);
		}
	}
	else
	{
		DBG_8192C("###=> usb_read_interrupt_complete => urb status(%d)\n", purb->status);

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
				break;
			case -EINPROGRESS:
				DBG_8192C("ERROR: URB IS IN PROGRESS!/n");
				break;
			default:
				break;				
		}
	}	

}

static u32 usb_read_interrupt(struct intf_hdl *pintfhdl, u32 addr)
{
	int	err;
	unsigned int pipe;
	u32	ret = _SUCCESS;
	struct dvobj_priv	*pdvobj = (struct dvobj_priv *)pintfhdl->pintf_dev;
	_adapter			*adapter = (_adapter *)pdvobj->padapter;
	struct recv_priv	*precvpriv = &adapter->recvpriv;
	struct usb_device	*pusbd = pdvobj->pusbdev;

_func_enter_;

	//translate DMA FIFO addr to pipehandle
	pipe = ffaddr2pipehdl(pdvobj, addr);

	usb_fill_int_urb(precvpriv->int_in_urb, pusbd, pipe, 
					precvpriv->int_in_buf,
            				INTERRUPT_MSG_FORMAT_LEN,
            				usb_read_interrupt_complete,
            				adapter,
            				1);

	err = usb_submit_urb(precvpriv->int_in_urb, GFP_ATOMIC);
	if((err) && (err != (-EPERM)))
	{
		DBG_8192C("cannot submit interrupt in-token(err = 0x%08x),urb_status = %d\n",err, precvpriv->int_in_urb->status);
		ret = _FAIL;
	}

_func_exit_;

	return ret;
}
#endif

static s32 pre_recv_entry(union recv_frame *precvframe, struct recv_stat *prxstat, struct phy_stat *pphy_status)
{	
	s32 ret=_SUCCESS;
#ifdef CONFIG_CONCURRENT_MODE	
	u8 *primary_myid, *secondary_myid, *paddr1;
	union recv_frame	*precvframe_if2 = NULL;
	_adapter *primary_padapter = precvframe->u.hdr.adapter;
	_adapter *secondary_padapter = primary_padapter->pbuddy_adapter;
	struct recv_priv *precvpriv = &primary_padapter->recvpriv;
	_queue *pfree_recv_queue = &precvpriv->free_recv_queue;
	u8	*pbuf = precvframe->u.hdr.rx_head;
	
	if(!secondary_padapter)
		return ret;
	
	paddr1 = GetAddr1Ptr(precvframe->u.hdr.rx_data);		

	if(IS_MCAST(paddr1) == _FALSE)//unicast packets
	{
		//primary_myid = myid(&primary_padapter->eeprompriv);
		secondary_myid = myid(&secondary_padapter->eeprompriv);

		if(_rtw_memcmp(paddr1, secondary_myid, ETH_ALEN))
		{			
			//change to secondary interface
			precvframe->u.hdr.adapter = secondary_padapter;
		}	

		//ret = recv_entry(precvframe);

	}
	else // Handle BC/MC Packets	
	{
		
		u8 clone = _TRUE;
#if 0		
		u8 type, subtype, *paddr2, *paddr3;
	
		type =  GetFrameType(pbuf);
		subtype = GetFrameSubType(pbuf); //bit(7)~bit(2)
		
		switch (type)
		{
			case WIFI_MGT_TYPE: //Handle BC/MC mgnt Packets
				if(subtype == WIFI_BEACON)
				{
					paddr3 = GetAddr3Ptr(precvframe->u.hdr.rx_data);
				
					if (check_fwstate(&secondary_padapter->mlmepriv, _FW_LINKED) &&
						_rtw_memcmp(paddr3, get_bssid(&secondary_padapter->mlmepriv), ETH_ALEN))
					{
						//change to secondary interface
						precvframe->u.hdr.adapter = secondary_padapter;
						clone = _FALSE;
					}

					if(check_fwstate(&primary_padapter->mlmepriv, _FW_LINKED) &&
						_rtw_memcmp(paddr3, get_bssid(&primary_padapter->mlmepriv), ETH_ALEN))
					{
						if(clone==_FALSE)
						{
							clone = _TRUE;									
						}	
						else
						{
							clone = _FALSE;
						}

						precvframe->u.hdr.adapter = primary_padapter;	
					}

					if(check_fwstate(&primary_padapter->mlmepriv, _FW_UNDER_SURVEY|_FW_UNDER_LINKING) ||
						check_fwstate(&secondary_padapter->mlmepriv, _FW_UNDER_SURVEY|_FW_UNDER_LINKING))
					{
						clone = _TRUE;
						precvframe->u.hdr.adapter = primary_padapter;	
					}
				
				}
				else if(subtype == WIFI_PROBEREQ)
				{
					//probe req frame is only for interface2
					//change to secondary interface
					precvframe->u.hdr.adapter = secondary_padapter;
					clone = _FALSE;
				}			
				break;
			case WIFI_CTRL_TYPE: // Handle BC/MC ctrl Packets
			
				break;
			case WIFI_DATA_TYPE: //Handle BC/MC data Packets
					//Notes: AP MODE never rx BC/MC data packets
			
				paddr2 = GetAddr2Ptr(precvframe->u.hdr.rx_data);

				if(_rtw_memcmp(paddr2, get_bssid(&secondary_padapter->mlmepriv), ETH_ALEN))
				{
					//change to secondary interface
					precvframe->u.hdr.adapter = secondary_padapter;
					clone = _FALSE;
				}

				break;
			default:
			
				break;			
		}
#endif

		if(_TRUE == clone)
		{
			//clone/copy to if2
			u8 frag, mf, shift_sz = 0;
			u16 drvinfo_sz;
			u32 pkt_len, pkt_offset, alloc_sz, skb_len;		
			_pkt	 *pkt_copy = NULL;
			struct rx_pkt_attrib *pattrib = NULL;
		
			precvframe_if2 = rtw_alloc_recvframe(pfree_recv_queue);
			if(precvframe_if2)
			{
				precvframe_if2->u.hdr.adapter = secondary_padapter;
		
				_rtw_init_listhead(&precvframe_if2->u.hdr.list);	
				precvframe_if2->u.hdr.precvbuf = NULL;	//can't access the precvbuf for new arch.
				precvframe_if2->u.hdr.len=0;

				_rtw_memcpy(&precvframe_if2->u.hdr.attrib, &precvframe->u.hdr.attrib, sizeof(struct rx_pkt_attrib));

				pattrib = &precvframe_if2->u.hdr.attrib;

				pkt_offset = pattrib->pkt_len + pattrib->drvinfo_sz + RXDESC_SIZE;

				//	Modified by Albert 20101213
				//	For 8 bytes IP header alignment.
				if (pattrib->qos)	//	Qos data, wireless lan header length is 26
				{
					shift_sz = 6;
				}
				else
				{
					shift_sz = 0;
				}

				skb_len = pattrib->pkt_len;

				// for first fragment packet, driver need allocate 1536+drvinfo_sz+RXDESC_SIZE to defrag packet.
				// modify alloc_sz for recvive crc error packet by thomas 2011-06-02
				if((pattrib->mfrag == 1)&&(pattrib->frag_num == 0)){
					//alloc_sz = 1664;	//1664 is 128 alignment.
					if(skb_len <= 1650)
						alloc_sz = 1664;
					else
						alloc_sz = skb_len + 14;
				}
				else {
					alloc_sz = skb_len;
					//	6 is for IP header 8 bytes alignment in QoS packet case.
					//	8 is for skb->data 4 bytes alignment.
					alloc_sz += 14;
				}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18)) // http://www.mail-archive.com/netdev@vger.kernel.org/msg17214.html
				pkt_copy = dev_alloc_skb(alloc_sz);
#else			
				pkt_copy = netdev_alloc_skb(secondary_padapter->pnetdev, alloc_sz);
#endif		
				if(pkt_copy)
				{
					pkt_copy->dev = secondary_padapter->pnetdev;
					precvframe_if2->u.hdr.pkt = pkt_copy;
					skb_reserve( pkt_copy, 8 - ((SIZE_PTR)( pkt_copy->data ) & 7 ));//force pkt_copy->data at 8-byte alignment address
					skb_reserve( pkt_copy, shift_sz );//force ip_hdr at 8-byte alignment address according to shift_sz.
					_rtw_memcpy(pkt_copy->data, pbuf, skb_len);
					precvframe_if2->u.hdr.rx_head = precvframe_if2->u.hdr.rx_data = precvframe_if2->u.hdr.rx_tail = pkt_copy->data;
					precvframe_if2->u.hdr.rx_end = pkt_copy->data + alloc_sz;
			
					recvframe_put(precvframe_if2, skb_len);

					update_recvframe_phyinfo_88e(precvframe_if2, (struct phy_stat*)pphy_status);
					ret = rtw_recv_entry(precvframe_if2);			

				} 
				else {
					rtw_free_recvframe(precvframe_if2, pfree_recv_queue);
					DBG_8192C("%s()-%d: alloc_skb() failed!\n", __FUNCTION__, __LINE__);	
				}

			}
			
		}
		
	}
	update_recvframe_phyinfo_88e(precvframe, (struct phy_stat*)pphy_status);	
	ret = rtw_recv_entry(precvframe);
#endif

	return ret;

}

#ifdef CONFIG_USE_USB_BUFFER_ALLOC_RX
static int recvbuf2recvframe(_adapter *padapter, struct recv_buf *precvbuf)
{
	u8	*pbuf;
	u8	frag, mf, shift_sz = 0;
	u16	pkt_cnt, drvinfo_sz;
	u32	pkt_len, pkt_offset, skb_len, alloc_sz;
	s32	transfer_len;
	struct recv_stat	*prxstat;
	struct phy_stat	*pphy_status = NULL;
	_pkt				*pkt_copy = NULL;
	union recv_frame	*precvframe = NULL;
	struct rx_pkt_attrib	*pattrib = NULL;
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(padapter);
	struct recv_priv	*precvpriv = &padapter->recvpriv;
	_queue			*pfree_recv_queue = &precvpriv->free_recv_queue;


	transfer_len = (s32)precvbuf->transfer_len;	
	pbuf = precvbuf->pbuf;

	prxstat = (struct recv_stat *)pbuf;	
	pkt_cnt = (le32_to_cpu(prxstat->rxdw2)>>16) & 0xff;

#if 0 //temp remove when disable usb rx aggregation
	if((pkt_cnt > 10) || (pkt_cnt < 1) || (transfer_len<RXDESC_SIZE) ||(pkt_len<=0))
	{		
		return _FAIL;
	}
#endif
	
	do{		
		RT_TRACE(_module_rtl871x_recv_c_, _drv_info_,
			 ("recvbuf2recvframe: rxdesc=offsset 0:0x%08x, 4:0x%08x, 8:0x%08x, C:0x%08x\n",
			  prxstat->rxdw0, prxstat->rxdw1, prxstat->rxdw2, prxstat->rxdw4));

		prxstat = (struct recv_stat *)pbuf;

		precvframe = rtw_alloc_recvframe(pfree_recv_queue);
		if(precvframe==NULL)
		{
			RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("recvbuf2recvframe: precvframe==NULL\n"));
			DBG_8192C("%s()-%d: rtw_alloc_recvframe() failed! RX Drop!\n", __FUNCTION__, __LINE__);	
			goto _exit_recvbuf2recvframe;
		}

		_rtw_init_listhead(&precvframe->u.hdr.list);	
		precvframe->u.hdr.precvbuf = NULL;	//can't access the precvbuf for new arch.
		precvframe->u.hdr.len=0;

		//rtl8192c_query_rx_desc_status(precvframe, prxstat);
		update_recvframe_attrib_88e(precvframe, prxstat);

		pattrib = &precvframe->u.hdr.attrib;
		
		if(pattrib->crc_err){
			DBG_8192C("%s()-%d: RX Warning! rx CRC ERROR !!\n", __FUNCTION__, __LINE__);	
			rtw_free_recvframe(precvframe, pfree_recv_queue);
			goto _exit_recvbuf2recvframe;
		}			
		
		if( (pattrib->physt) && (pattrib->pkt_rpt_type == NORMAL_RX))
		{
			pphy_status = (struct phy_stat *)(pbuf + RXDESC_OFFSET + pattrib->shift_sz);
		}

		pkt_offset = pattrib->pkt_len + pattrib->drvinfo_sz + RXDESC_SIZE;

		if((pattrib->pkt_len<=0) || (pkt_offset>transfer_len))
		{	
			RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("recvbuf2recvframe: pkt_len<=0\n"));
			DBG_8192C("%s()-%d: RX Warning!\n", __FUNCTION__, __LINE__);	
			rtw_free_recvframe(precvframe, pfree_recv_queue);
			goto _exit_recvbuf2recvframe;
		}

		//	Modified by Albert 20101213
		//	For 8 bytes IP header alignment.
		if (pattrib->qos)	//	Qos data, wireless lan header length is 26
		{
			shift_sz = 6;
		}
		else
		{
			shift_sz = 0;
		}

		skb_len = pattrib->pkt_len;

		// for first fragment packet, driver need allocate 1536+drvinfo_sz+RXDESC_SIZE to defrag packet.
		// modify alloc_sz for recvive crc error packet by thomas 2011-06-02
		if((pattrib->mfrag == 1)&&(pattrib->frag_num == 0)){
			//alloc_sz = 1664;	//1664 is 128 alignment.
			if(skb_len <= 1650)
				alloc_sz = 1664;
			else
				alloc_sz = skb_len + 14;
		}
		else {
			alloc_sz = skb_len;
			//	6 is for IP header 8 bytes alignment in QoS packet case.
			//	8 is for skb->data 4 bytes alignment.
			alloc_sz += 14;
		}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18)) // http://www.mail-archive.com/netdev@vger.kernel.org/msg17214.html
		pkt_copy = dev_alloc_skb(alloc_sz);
#else			
		pkt_copy = netdev_alloc_skb(padapter->pnetdev, alloc_sz);
#endif		
		if(pkt_copy)
		{
			pkt_copy->dev = padapter->pnetdev;
			precvframe->u.hdr.pkt = pkt_copy;
			skb_reserve( pkt_copy, 8 - ((SIZE_PTR)( pkt_copy->data ) & 7 ));//force pkt_copy->data at 8-byte alignment address
			skb_reserve( pkt_copy, shift_sz );//force ip_hdr at 8-byte alignment address according to shift_sz.
			_rtw_memcpy(pkt_copy->data, (pbuf + pattrib->drvinfo_sz + RXDESC_SIZE), skb_len);
			precvframe->u.hdr.rx_head = precvframe->u.hdr.rx_data = precvframe->u.hdr.rx_tail = pkt_copy->data;
			precvframe->u.hdr.rx_end = pkt_copy->data + alloc_sz;
		}
		else
		{
			DBG_8192C("recvbuf2recvframe:can not allocate memory for skb copy\n");
			//precvframe->u.hdr.pkt = skb_clone(pskb, GFP_ATOMIC);
			//precvframe->u.hdr.rx_head = precvframe->u.hdr.rx_data = precvframe->u.hdr.rx_tail = pbuf;
			//precvframe->u.hdr.rx_end = pbuf + (pkt_offset>1612?pkt_offset:1612);

			precvframe->u.hdr.pkt = NULL;
			rtw_free_recvframe(precvframe, pfree_recv_queue);

			goto _exit_recvbuf2recvframe;
		}

		recvframe_put(precvframe, skb_len);
		//recvframe_pull(precvframe, drvinfo_sz + RXDESC_SIZE);	

#ifdef CONFIG_USB_RX_AGGREGATION
		switch(pHalData->UsbRxAggMode)
		{
			case USB_RX_AGG_DMA:
			case USB_RX_AGG_MIX:
				pkt_offset = (u16)_RND128(pkt_offset);
				break;
				case USB_RX_AGG_USB:
				pkt_offset = (u16)_RND4(pkt_offset);
				break;
			case USB_RX_AGG_DISABLE:			
			default:				
				break;
		}
#endif

		if(pattrib->pkt_rpt_type == NORMAL_RX)//Normal rx packet
		{
#ifdef CONFIG_CONCURRENT_MODE
			if(rtw_buddy_adapter_up(padapter))
			{
				if(pre_recv_entry(precvframe, prxstat, pphy_status) != _SUCCESS)
				{
					RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,
						("recvbuf2recvframe: recv_entry(precvframe) != _SUCCESS\n"));
				}
			}
			else
#endif				
			{
				if (pattrib->physt) 
					update_recvframe_phyinfo_88e(precvframe, (struct phy_stat*)pphy_status);
				if(rtw_recv_entry(precvframe) != _SUCCESS)
				{
					RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,
						("recvbuf2recvframe: rtw_recv_entry(precvframe) != _SUCCESS\n"));
				}
			}

		}
		else{ // pkt_rpt_type == TX_REPORT1-CCX, TX_REPORT2-TX RTP,HIS_REPORT-USB HISR RTP
				
			//enqueue recvframe to txrtp queue
			if(pattrib->pkt_rpt_type == TX_REPORT1){
				printk("rx CCX \n");
			}
			else if(pattrib->pkt_rpt_type == TX_REPORT2){
				//printk("rx TX RPT \n");
				ODM_RA_TxRPT2Handle_8188E(
							&pHalData->odmpriv,
							precvframe->u.hdr.rx_data,
							pattrib->pkt_len,
							pattrib->MacIDValidEntry[0],
							pattrib->MacIDValidEntry[1]
							);
				
			}
			else if(pattrib->pkt_rpt_type == HIS_REPORT)
			{
				//printk("%s , rx USB HISR \n",__FUNCTION__);
				#ifdef CONFIG_SUPPORT_USB_INT
				interrupt_handler_8188eu(padapter,pattrib->pkt_len,precvframe->u.hdr.rx_data);
				#endif
			}					
			rtw_free_recvframe(precvframe, pfree_recv_queue);				
			
		}

		pkt_cnt--;
		transfer_len -= pkt_offset;
		pbuf += pkt_offset;	
		precvframe = NULL;
		pkt_copy = NULL;

		if(transfer_len>0 && pkt_cnt==0)
			pkt_cnt = (le32_to_cpu(prxstat->rxdw2)>>16) & 0xff;

	}while((transfer_len>0) && (pkt_cnt>0));

_exit_recvbuf2recvframe:

	return _SUCCESS;
}

void rtl8188eu_recv_tasklet(void *priv)
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

static void usb_read_port_complete(struct urb *purb, struct pt_regs *regs)
{	
	struct recv_buf	*precvbuf = (struct recv_buf *)purb->context;	
	_adapter 			*padapter =(_adapter *)precvbuf->adapter;
	struct recv_priv	*precvpriv = &padapter->recvpriv;

	RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port_complete!!!\n"));
	
	precvpriv->rx_pending_cnt --;
		
	if(padapter->bSurpriseRemoved || padapter->bDriverStopped||padapter->bReadPortCancel)
	{
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port_complete:bDriverStopped(%d) OR bSurpriseRemoved(%d)\n", padapter->bDriverStopped, padapter->bSurpriseRemoved));		

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
			rtw_reset_continual_urb_error(&padapter->dvobjpriv);
			
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

		if(rtw_inc_and_chk_continual_urb_error(&padapter->dvobjpriv) == _TRUE ){
			padapter->bSurpriseRemoved = _TRUE;
		}

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

static u32 usb_read_port(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *rmem)
{		
	int err;
	unsigned int pipe;
	u32 ret = _SUCCESS;
	PURB purb = NULL;	
	struct recv_buf	*precvbuf = (struct recv_buf *)rmem;
	struct dvobj_priv	*pdvobj = (struct dvobj_priv *)pintfhdl->pintf_dev;
	_adapter		*adapter = (_adapter *)pdvobj->padapter;
	struct recv_priv	*precvpriv = &adapter->recvpriv;
	struct usb_device	*pusbd = pdvobj->pusbdev;

_func_enter_;
	
	if(adapter->bDriverStopped || adapter->bSurpriseRemoved ||adapter->pwrctrlpriv.pnp_bstop_trx)
	{
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port:( padapter->bDriverStopped ||padapter->bSurpriseRemoved ||adapter->pwrctrlpriv.pnp_bstop_trx)!!!\n"));
		return _FAIL;
	}

	if(precvbuf !=NULL)
	{	
		rtl8188eu_init_recvbuf(adapter, precvbuf);

		if(precvbuf->pbuf)
		{			
			precvpriv->rx_pending_cnt++;
		
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
			
	}
	else
	{
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port:precvbuf ==NULL\n"));
		ret = _FAIL;
	}

_func_exit_;

	return ret;
}
#else	// CONFIG_USE_USB_BUFFER_ALLOC_RX
static int recvbuf2recvframe(_adapter *padapter, _pkt *pskb)
{
	u8	*pbuf;
	u8	shift_sz = 0;
	u16	pkt_cnt;
	u32	pkt_offset, skb_len, alloc_sz;
	s32	transfer_len;
	struct recv_stat	*prxstat;
	struct phy_stat	*pphy_status = NULL;
	_pkt				*pkt_copy = NULL;
	union recv_frame	*precvframe = NULL;
	struct rx_pkt_attrib	*pattrib = NULL;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	struct recv_priv	*precvpriv = &padapter->recvpriv;
	_queue			*pfree_recv_queue = &precvpriv->free_recv_queue;


	transfer_len = (s32)pskb->len;	
	pbuf = pskb->data;

	prxstat = (struct recv_stat *)pbuf;	
	pkt_cnt = (le32_to_cpu(prxstat->rxdw2)>>16) & 0xff;

#if 0 //temp remove when disable usb rx aggregation
	if((pkt_cnt > 10) || (pkt_cnt < 1) || (transfer_len<RXDESC_SIZE) ||(pkt_len<=0))
	{		
		return _FAIL;
	}
#endif
	do{		
		RT_TRACE(_module_rtl871x_recv_c_, _drv_info_,
			 ("recvbuf2recvframe: rxdesc=offsset 0:0x%08x, 4:0x%08x, 8:0x%08x, C:0x%08x\n",
			  prxstat->rxdw0, prxstat->rxdw1, prxstat->rxdw2, prxstat->rxdw4));

		prxstat = (struct recv_stat *)pbuf;

		precvframe = rtw_alloc_recvframe(pfree_recv_queue);
		if(precvframe==NULL)
		{
			RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("recvbuf2recvframe: precvframe==NULL\n"));
			DBG_8192C("%s()-%d: rtw_alloc_recvframe() failed! RX Drop!\n", __FUNCTION__, __LINE__);	
			goto _exit_recvbuf2recvframe;
		}

		_rtw_init_listhead(&precvframe->u.hdr.list);	
		precvframe->u.hdr.precvbuf = NULL;	//can't access the precvbuf for new arch.
		precvframe->u.hdr.len=0;

		//rtl8192c_query_rx_desc_status(precvframe, prxstat);
		update_recvframe_attrib_88e(precvframe, prxstat);

		pattrib = &precvframe->u.hdr.attrib;
	
		if ((pattrib->crc_err) )//|| (pattrib->icv_err))
		{
			DBG_8192C("%s: RX Warning! crc_err=%d icv_err=%d, skip!\n", __FUNCTION__, pattrib->crc_err, pattrib->icv_err);
			rtw_free_recvframe(precvframe, pfree_recv_queue);
			goto _exit_recvbuf2recvframe;
		}

		if( (pattrib->physt) && (pattrib->pkt_rpt_type == NORMAL_RX))
		{
			pphy_status = (struct phy_stat *)(pbuf + RXDESC_OFFSET + pattrib->shift_sz);
		}

		pkt_offset = pattrib->pkt_len + pattrib->drvinfo_sz + RXDESC_SIZE;

		if((pattrib->pkt_len<=0) || (pkt_offset>transfer_len))
		{	
			RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("recvbuf2recvframe: pkt_len<=0\n"));
			DBG_8192C("%s()-%d: RX Warning!,pkt_len<=0 or pkt_offset> transfoer_len \n", __FUNCTION__, __LINE__);	
			rtw_free_recvframe(precvframe, pfree_recv_queue);
			goto _exit_recvbuf2recvframe;
		}
	
		//	Modified by Albert 20101213
		//	For 8 bytes IP header alignment.
		if (pattrib->qos)	//	Qos data, wireless lan header length is 26
		{
			shift_sz = 6;
		}
		else
		{
			shift_sz = 0;
		}

		skb_len = pattrib->pkt_len;

		// for first fragment packet, driver need allocate 1536+drvinfo_sz+RXDESC_SIZE to defrag packet.
		// modify alloc_sz for recvive crc error packet by thomas 2011-06-02
		if((pattrib->mfrag == 1)&&(pattrib->frag_num == 0)){
			//alloc_sz = 1664;	//1664 is 128 alignment.
			if(skb_len <= 1650)
				alloc_sz = 1664;
			else
				alloc_sz = skb_len + 14;
		}
		else {
			alloc_sz = skb_len;
			//	6 is for IP header 8 bytes alignment in QoS packet case.
			//	8 is for skb->data 4 bytes alignment.
			alloc_sz += 14;
		}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18)) // http://www.mail-archive.com/netdev@vger.kernel.org/msg17214.html
		pkt_copy = dev_alloc_skb(alloc_sz);
#else			
		pkt_copy = netdev_alloc_skb(padapter->pnetdev, alloc_sz);
#endif		
		if(pkt_copy)
		{
			pkt_copy->dev = padapter->pnetdev;
			precvframe->u.hdr.pkt = pkt_copy;
			skb_reserve( pkt_copy, 8 - ((SIZE_PTR)( pkt_copy->data ) & 7 ));//force pkt_copy->data at 8-byte alignment address
			skb_reserve( pkt_copy, shift_sz );//force ip_hdr at 8-byte alignment address according to shift_sz.
			_rtw_memcpy(pkt_copy->data, (pbuf + pattrib->drvinfo_sz + RXDESC_SIZE), skb_len);
			precvframe->u.hdr.rx_head = precvframe->u.hdr.rx_data = precvframe->u.hdr.rx_tail = pkt_copy->data;
			precvframe->u.hdr.rx_end = pkt_copy->data + alloc_sz;
		}
		else
		{
			if((pattrib->mfrag == 1)&&(pattrib->frag_num == 0))
			{				
				DBG_8192C("recvbuf2recvframe: alloc_skb fail , drop frag frame \n");
				rtw_free_recvframe(precvframe, pfree_recv_queue);
				goto _exit_recvbuf2recvframe;
			}
			
			precvframe->u.hdr.pkt = skb_clone(pskb, GFP_ATOMIC);
			if(precvframe->u.hdr.pkt)
			{
				precvframe->u.hdr.rx_head = precvframe->u.hdr.rx_data = precvframe->u.hdr.rx_tail 
					= pbuf+ pattrib->drvinfo_sz + RXDESC_SIZE;
				precvframe->u.hdr.rx_end =  pbuf +pattrib->drvinfo_sz + RXDESC_SIZE+ alloc_sz;
			}
			else
			{
				DBG_8192C("recvbuf2recvframe: skb_clone fail\n");
				rtw_free_recvframe(precvframe, pfree_recv_queue);
				goto _exit_recvbuf2recvframe;
			}
			
		}

		recvframe_put(precvframe, skb_len);
		//recvframe_pull(precvframe, drvinfo_sz + RXDESC_SIZE);	

#ifdef CONFIG_USB_RX_AGGREGATION
		switch(pHalData->UsbRxAggMode)
		{
			case USB_RX_AGG_DMA:
			case USB_RX_AGG_MIX:
				pkt_offset = (u16)_RND128(pkt_offset);
				break;
				case USB_RX_AGG_USB:
				pkt_offset = (u16)_RND4(pkt_offset);
				break;
			case USB_RX_AGG_DISABLE:			
			default:				
				break;
		}
#endif

		if(pattrib->pkt_rpt_type == NORMAL_RX)//Normal rx packet
		{
#ifdef CONFIG_CONCURRENT_MODE
			if(rtw_buddy_adapter_up(padapter))
			{
				if(pre_recv_entry(precvframe, prxstat, pphy_status) != _SUCCESS)
				{
					RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,
						("recvbuf2recvframe: recv_entry(precvframe) != _SUCCESS\n"));
				}
			}
			else
#endif
			{
				if (pattrib->physt) 
					update_recvframe_phyinfo_88e(precvframe, (struct phy_stat*)pphy_status);
//==============================================================================
#if 1
				if ((pattrib->crc_err==0) && (pattrib->icv_err==1))
				{								
				
					u8	*wlanhdr;				
					u8 i;
					u8 *sa,*da,*bssid;
					u32 type;
					
					wlanhdr = get_recvframe_data(precvframe);
					
			DBG_8192C("%s: RX Warning! crc_err=%d icv_err=%d, skip!\n", __FUNCTION__, pattrib->crc_err, pattrib->icv_err);
	                        /*
					pkt_info.bPacketMatchBSSID = ((!IsFrameTypeCtrl(wlanhdr)) &&					
						_rtw_memcmp(get_hdr_bssid(wlanhdr), get_bssid(&padapter->mlmepriv), ETH_ALEN));

					pkt_info.bPacketToSelf = pkt_info.bPacketMatchBSSID && (_rtw_memcmp(get_da(wlanhdr), myid(&padapter->eeprompriv), ETH_ALEN));

					pkt_info.bPacketBeacon = pkt_info.bPacketMatchBSSID && (GetFrameSubType(wlanhdr) == WIFI_BEACON);

					if(pkt_info.bPacketBeacon){
						if(check_fwstate(&padapter->mlmepriv, WIFI_STATION_STATE) == _TRUE){				
							sa = padapter->mlmepriv.cur_network.network.MacAddress;
							#if 0
							{					
								printk("==> rx beacon from AP[%02x:%02x:%02x:%02x:%02x:%02x]\n",
									sa[0],sa[1],sa[2],sa[3],sa[4],sa[5]);					
							}
							#endif
						}
						
					}
					else{
						sa = get_sa(wlanhdr);
						printk("==> sa[%02x:%02x:%02x:%02x:%02x:%02x]\n",
									sa[0],sa[1],sa[2],sa[3],sa[4],sa[5]);					
						
					}	
					
					sa = get_sa(wlanhdr);
					printk("==> sa[%02x:%02x:%02x:%02x:%02x:%02x]\n",
									sa[0],sa[1],sa[2],sa[3],sa[4],sa[5]);		
					da =get_da(wlanhdr);
					printk("==> da[%02x:%02x:%02x:%02x:%02x:%02x]\n",
									da[0],da[1],da[2],da[3],da[4],da[5]);	

					//bssid = get_bssid(&padapter->mlmepriv);
					//printk("==> bssid[%02x:%02x:%02x:%02x:%02x:%02x]\n",
					//				bssid[0],bssid[1],bssid[2],bssid[3],bssid[4],bssid[5]);	
					type = GetFrameSubType(wlanhdr);
					printk("==>frame type:0x%08x\n",type);
					*/
					printk("\n===========================\n [");
					for(i=0;i<26;i++)
					{
						printk(" 0x%02x ",wlanhdr[i]);
						if(i%16==0)
							printk("\n");
					}
					printk("] \n===========================\n");
							
					
				}
#endif
//==============================================================================
			
				if(rtw_recv_entry(precvframe) != _SUCCESS)
				{
					RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,
						("recvbuf2recvframe: rtw_recv_entry(precvframe) != _SUCCESS\n"));
				}
			}
		}
		else{ // pkt_rpt_type == TX_REPORT1-CCX, TX_REPORT2-TX RTP,HIS_REPORT-USB HISR RTP
				
			//enqueue recvframe to txrtp queue
			if(pattrib->pkt_rpt_type == TX_REPORT1){
				printk("rx CCX \n");
			}
			else if(pattrib->pkt_rpt_type == TX_REPORT2){
				//printk("rx TX RPT \n");
				ODM_RA_TxRPT2Handle_8188E(
							&pHalData->odmpriv,
							precvframe->u.hdr.rx_data,
							pattrib->pkt_len,
							pattrib->MacIDValidEntry[0],
							pattrib->MacIDValidEntry[1]
							);
				
			}
			else if(pattrib->pkt_rpt_type == HIS_REPORT)
			{
				//printk("%s , rx USB HISR \n",__FUNCTION__);
				#ifdef CONFIG_SUPPORT_USB_INT
				interrupt_handler_8188eu(padapter,pattrib->pkt_len,precvframe->u.hdr.rx_data);
				#endif
			}				
			rtw_free_recvframe(precvframe, pfree_recv_queue);			
			
		}
	
		pkt_cnt--;
		transfer_len -= pkt_offset;
		pbuf += pkt_offset;	
		precvframe = NULL;
		pkt_copy = NULL;

		if(transfer_len>0 && pkt_cnt==0)
			pkt_cnt = (le32_to_cpu(prxstat->rxdw2)>>16) & 0xff;

	}while((transfer_len>0) && (pkt_cnt>0));

_exit_recvbuf2recvframe:

	return _SUCCESS;	
}

void rtl8188eu_recv_tasklet(void *priv)
{
	_pkt			*pskb;
	_adapter		*padapter = (_adapter*)priv;
	struct recv_priv	*precvpriv = &padapter->recvpriv;
	
	while (NULL != (pskb = skb_dequeue(&precvpriv->rx_skb_queue)))
	{
		if ((padapter->bDriverStopped == _TRUE)||(padapter->bSurpriseRemoved== _TRUE))
		{
			DBG_8192C("recv_tasklet => bDriverStopped or bSurpriseRemoved \n");
			dev_kfree_skb_any(pskb);
			break;
		}
	
		recvbuf2recvframe(padapter, pskb);

#ifdef CONFIG_PREALLOC_RECV_SKB

#ifdef NET_SKBUFF_DATA_USES_OFFSET			
		skb_reset_tail_pointer(pskb);
#else
		pskb->tail = pskb->data;
#endif
		pskb->len = 0;
		
		skb_queue_tail(&precvpriv->free_recv_skb_queue, pskb);
		
#else
		dev_kfree_skb_any(pskb);
#endif
				
	}
	
}


static void usb_read_port_complete(struct urb *purb, struct pt_regs *regs)
{
	_irqL irqL;
	uint isevt, *pbuf;
	struct recv_buf	*precvbuf = (struct recv_buf *)purb->context;	
	_adapter 			*padapter =(_adapter *)precvbuf->adapter;
	struct recv_priv	*precvpriv = &padapter->recvpriv;	
	
	RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port_complete!!!\n"));
	
	//_enter_critical(&precvpriv->lock, &irqL);
	//precvbuf->irp_pending=_FALSE;
	//precvpriv->rx_pending_cnt --;
	//_exit_critical(&precvpriv->lock, &irqL);
		
	precvpriv->rx_pending_cnt --;
		
	//if(precvpriv->rx_pending_cnt== 0)
	//{		
	//	RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port_complete: rx_pending_cnt== 0, set allrxreturnevt!\n"));
	//	_rtw_up_sema(&precvpriv->allrxreturnevt);	
	//}

	if(padapter->bSurpriseRemoved || padapter->bDriverStopped||padapter->bReadPortCancel)
	{
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port_complete:bDriverStopped(%d) OR bSurpriseRemoved(%d)\n", padapter->bDriverStopped, padapter->bSurpriseRemoved));		
		
	#ifdef CONFIG_PREALLOC_RECV_SKB
		precvbuf->reuse = _TRUE;
	#else
		if(precvbuf->pskb){
			DBG_8192C("==> free skb(%p)\n",precvbuf->pskb);
			dev_kfree_skb_any(precvbuf->pskb);				
		}	
	#endif
		DBG_8192C("%s() RX Warning! bDriverStopped(%d) OR bSurpriseRemoved(%d) bReadPortCancel(%d)\n", 
		__FUNCTION__,padapter->bDriverStopped, padapter->bSurpriseRemoved,padapter->bReadPortCancel);	
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
			rtw_reset_continual_urb_error(&padapter->dvobjpriv);
			
			precvbuf->transfer_len = purb->actual_length;			
			skb_put(precvbuf->pskb, purb->actual_length);	
			skb_queue_tail(&precvpriv->rx_skb_queue, precvbuf->pskb);

			if (skb_queue_len(&precvpriv->rx_skb_queue)<=1)
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

		if(rtw_inc_and_chk_continual_urb_error(&padapter->dvobjpriv) == _TRUE ){
			padapter->bSurpriseRemoved = _TRUE;
		}

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

static u32 usb_read_port(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *rmem)
{	
	_irqL irqL;
	int err;
	unsigned int pipe;
	SIZE_PTR tmpaddr=0;
	SIZE_PTR alignment=0;
	u32 ret = _SUCCESS;
	PURB purb = NULL;
	struct recv_buf	*precvbuf = (struct recv_buf *)rmem;
	struct dvobj_priv	*pdvobj = (struct dvobj_priv *)pintfhdl->pintf_dev;
	_adapter		*adapter = (_adapter *)pdvobj->padapter;
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
		rtl8188eu_init_recvbuf(adapter, precvbuf);		

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
				DBG_8192C("#### usb_read_port() alloc_skb fail!#####\n");
				return _FAIL;
			}	

			tmpaddr = (SIZE_PTR)precvbuf->pskb->data;
	        	alignment = tmpaddr & (RECVBUFF_ALIGN_SZ-1);
			skb_reserve(precvbuf->pskb, (RECVBUFF_ALIGN_SZ - alignment));

			precvbuf->phead = precvbuf->pskb->head;
		   	precvbuf->pdata = precvbuf->pskb->data;

#ifdef NET_SKBUFF_DATA_USES_OFFSET
			precvbuf->ptail = precvbuf->pskb->head + precvbuf->pskb->tail;		
			precvbuf->pend = precvbuf->ptail + (MAX_RECVBUF_SZ + RECVBUFF_ALIGN_SZ);
#else
			precvbuf->ptail = precvbuf->pskb->tail;
			precvbuf->pend = precvbuf->pskb->end;
#endif
			precvbuf->pbuf = precvbuf->pskb->data;
		}	
		else//reuse skb
		{
			precvbuf->phead = precvbuf->pskb->head;
			precvbuf->pdata = precvbuf->pskb->data;

#ifdef NET_SKBUFF_DATA_USES_OFFSET
			precvbuf->ptail = precvbuf->pskb->head + precvbuf->pskb->tail;
			precvbuf->pend = precvbuf->ptail + (MAX_RECVBUF_SZ + RECVBUFF_ALIGN_SZ);
#else
			precvbuf->ptail = precvbuf->pskb->tail;
			precvbuf->pend = precvbuf->pskb->end;
#endif
       		precvbuf->pbuf = precvbuf->pskb->data;

			precvbuf->reuse = _FALSE;
		}

		//_enter_critical(&precvpriv->lock, &irqL);
		//precvpriv->rx_pending_cnt++;
		//precvbuf->irp_pending = _TRUE;
		//_exit_critical(&precvpriv->lock, &irqL);

		precvpriv->rx_pending_cnt++;

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
	}
	else
	{
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port:precvbuf ==NULL\n"));
		ret = _FAIL;
	}

_func_exit_;

	return ret;
}
#endif	// CONFIG_USE_USB_BUFFER_ALLOC_RX

static void usb_read_port_cancel(struct intf_hdl *pintfhdl)
{
	int i;	
	struct recv_buf *precvbuf;	
	_adapter	*padapter = pintfhdl->padapter;
	precvbuf = (struct recv_buf *)padapter->recvpriv.precv_buf;	

	DBG_8192C("usb_read_port_cancel \n");

	padapter->bReadPortCancel = _TRUE;

	for(i=0; i < NR_RECVBUFF ; i++)	
	{
		precvbuf->reuse = _TRUE;	
		if(precvbuf->purb)	
		{
			//DBG_8192C("usb_read_port_cancel : usb_kill_urb \n");
			usb_kill_urb(precvbuf->purb);
		}		

		precvbuf++;
	}

#ifdef CONFIG_USB_INTERRUPT_IN_PIPE
	usb_kill_urb(padapter->recvpriv.int_in_urb);
#endif
}

void rtl8188eu_xmit_tasklet(void *priv)
{	
	int ret = _FALSE;
	_adapter *padapter = (_adapter*)priv;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;

	if(check_fwstate(&padapter->mlmepriv, _FW_UNDER_SURVEY) == _TRUE)
		return;

	while(1)
	{
		if ((padapter->bDriverStopped == _TRUE)||(padapter->bSurpriseRemoved== _TRUE) || (padapter->bWritePortCancel == _TRUE))
		{
			DBG_8192C("xmit_tasklet => bDriverStopped or bSurpriseRemoved or bWritePortCancel\n");
			break;
		}

		ret = rtl8188eu_xmitframe_complete(padapter, pxmitpriv, NULL);

		if(ret==_FALSE)
			break;
		
	}
	
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

	RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("+usb_write_port_complete\n"));
	//printk("====> usb_write_port_complete\n");

	switch(pxmitbuf->flags)
	{
		case XMIT_VO_QUEUE:
			pxmitpriv->voq_cnt--;			
			break;
		case XMIT_VI_QUEUE:
			pxmitpriv->viq_cnt--;		
			break;
		case XMIT_BE_QUEUE:
			pxmitpriv->beq_cnt--;			
			break;
		case XMIT_BK_QUEUE:
			pxmitpriv->bkq_cnt--;			
			break;
		case HIGH_QUEUE_INX:
#ifdef CONFIG_AP_MODE			
			rtw_chk_hi_queue_cmd(padapter);
#endif
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
        //rtw_free_xmitframe_ex(pxmitpriv, pxmitframe);
	
	if(padapter->bSurpriseRemoved || padapter->bDriverStopped ||padapter->bWritePortCancel)
	{
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_write_port_complete:bDriverStopped(%d) OR bSurpriseRemoved(%d)", padapter->bDriverStopped, padapter->bSurpriseRemoved));
		DBG_8192C("%s(): TX Warning! bDriverStopped(%d) OR bSurpriseRemoved(%d) bWritePortCancel(%d) pxmitbuf->ext_tag(%x) \n", 
		__FUNCTION__,padapter->bDriverStopped, padapter->bSurpriseRemoved,padapter->bReadPortCancel,pxmitbuf->ext_tag);	

		
		
		goto check_completion;
	}


	if(purb->status==0)
	{
	
	}
	else
	{
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_write_port_complete : purb->status(%d) != 0 \n", purb->status));
		DBG_8192C("###=> urb_write_port_complete status(%d)\n",purb->status);
		if((purb->status==-EPIPE)||(purb->status==-EPROTO))
		{
			//usb_clear_halt(pusbdev, purb->pipe);	
			//msleep(10);
	             #ifdef DBG_CONFIG_ERROR_DETECT
			{	
				HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
				pHalData->srestpriv.Wifi_Error_Status = USB_WRITE_PORT_FAIL;			
	             }
			#endif
		}		
		else if(purb->status == -EINPROGRESS)
		{
			RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_write_port_complete: EINPROGESS\n"));
		}
		else if(purb->status == (-ESHUTDOWN))
		{
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
	if(pxmitbuf->isSync) {
		pxmitbuf->status = purb->status;
		complete(&pxmitbuf->done);
	}

	rtw_free_xmitbuf(pxmitpriv, pxmitbuf);

	//if(rtw_txframes_pending(padapter))	
	{
		tasklet_hi_schedule(&pxmitpriv->xmit_tasklet);
	}
	

	RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("-usb_write_port_complete\n"));
_func_exit_;	

}

static u32 usb_write_port(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *wmem, int timeout_ms)
{    
	_irqL irqL;
	unsigned int pipe;
	int status;
	u32 ret, bwritezero = _FALSE;
	PURB	purb = NULL;
	_adapter *padapter = (_adapter *)pintfhdl->padapter;
	struct dvobj_priv	*pdvobj = (struct dvobj_priv   *)&padapter->dvobjpriv;	
	struct xmit_priv	*pxmitpriv = &padapter->xmitpriv;
	struct xmit_buf *pxmitbuf = (struct xmit_buf *)wmem;
	struct xmit_frame *pxmitframe = (struct xmit_frame *)pxmitbuf->priv_data;
	struct usb_device *pusbd = pdvobj->pusbdev;
	struct pkt_attrib *pattrib = &pxmitframe->attrib;
	
_func_enter_;	
	
	RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("+usb_write_port\n"));
	//printk("+usb_write_port\n");
	if((padapter->bDriverStopped) || (padapter->bSurpriseRemoved) ||(padapter->pwrctrlpriv.pnp_bstop_trx))
	{
		#ifdef DBG_TX
		DBG_871X(" DBG_TX %s:%d bDriverStopped%d, bSurpriseRemoved:%d, pnp_bstop_trx:%d\n",__FUNCTION__, __LINE__
		,padapter->bDriverStopped, padapter->bSurpriseRemoved, padapter->pwrctrlpriv.pnp_bstop_trx );
		#endif
		
		rtw_free_xmitbuf(pxmitpriv, pxmitbuf);
		
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_write_port:( padapter->bDriverStopped ||padapter->bSurpriseRemoved ||adapter->pwrctrlpriv.pnp_bstop_trx)!!!\n"));
		return _FAIL;
	}
	//printk("====> usb_write_port\n");
	_enter_critical(&pxmitpriv->lock, &irqL);

	switch(addr)
	{
		case VO_QUEUE_INX:
			pxmitpriv->voq_cnt++;
			pxmitbuf->flags = XMIT_VO_QUEUE;
			break;
		case VI_QUEUE_INX:
			pxmitpriv->viq_cnt++;
			pxmitbuf->flags = XMIT_VI_QUEUE;
			break;
		case BE_QUEUE_INX:
			pxmitpriv->beq_cnt++;
			pxmitbuf->flags = XMIT_BE_QUEUE;
			break;
		case BK_QUEUE_INX:
			pxmitpriv->bkq_cnt++;
			pxmitbuf->flags = XMIT_BK_QUEUE;
			break;
		case HIGH_QUEUE_INX:
			pxmitbuf->flags = HIGH_QUEUE_INX;
			break;
		default:
			pxmitbuf->flags = XMIT_VO_QUEUE;
			break;
	}
		
	_exit_critical(&pxmitpriv->lock, &irqL);
		
/*
	_enter_critical(&pxmitpriv->lock, &irqL);

	//total irp 
	pxmitpriv->txirp_cnt++;
	
	//per ac irp
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
*/

	purb	= pxmitbuf->pxmit_urb[0];

#if 0
	if(pdvobj->ishighspeed)
	{
		if(cnt> 0 && cnt%512 == 0)
		{
			//DBG_8192C("ishighspeed, cnt=%d\n", cnt);
			bwritezero = _TRUE;			
		}	
	}
	else
	{
		if(cnt > 0 && cnt%64 == 0)
		{
			//DBG_8192C("cnt=%d\n", cnt);
			bwritezero = _TRUE;			
		}	
	}
#endif

	//translate DMA FIFO addr to pipehandle
	pipe = ffaddr2pipehdl(pdvobj, addr);	

#ifdef CONFIG_REDUCE_USB_TX_INT	
	if ( (pxmitpriv->free_xmitbuf_cnt%NR_XMITBUFF == 0)
		|| (pxmitbuf->ext_tag == _TRUE) )
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
              			
#if 0
	if (bwritezero)
        {
            purb->transfer_flags |= URB_ZERO_PACKET;           
        }			
#endif

	status = usb_submit_urb(purb, GFP_ATOMIC);

	if (!status)
	{		
		ret= _SUCCESS;
		#ifdef DBG_CONFIG_ERROR_DETECT	
		{	
			HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
			pHalData->srestpriv.last_tx_time = rtw_get_current_time();		
		}
		#endif
	}
	else
	{
		DBG_8192C("usb_write_port, status=%d\n", status);
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

	// synchronous write handling
	if(timeout_ms >= 0) {
		unsigned long expire = timeout_ms ? msecs_to_jiffies(timeout_ms) : MAX_SCHEDULE_TIMEOUT;
		int status;
		init_completion(&pxmitbuf->done);
		pxmitbuf->isSync = _TRUE;
		pxmitbuf->status = 0;

		if (!wait_for_completion_timeout(&pxmitbuf->done, expire)) {
			usb_kill_urb(purb);
			status = (pxmitbuf->status == -ENOENT ? -ETIMEDOUT : pxmitbuf->status);
		} else
			status = pxmitbuf->status;

		if (!status) {
			ret= _SUCCESS;
		} else {
			DBG_8192C("usb_write_port sync, status=%d\n", status);
			ret = _FAIL;
		}		
	}


	RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("-usb_write_port\n"));

_func_exit_;
	
	return ret;

}

static inline u32 usb_write_port_async(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *wmem)
{
	return usb_write_port(pintfhdl, addr, cnt, wmem, -1);
}

static inline int usb_write_port_sync(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *wmem)
{
	return usb_write_port(pintfhdl, addr, cnt, wmem, RTW_USB_BULKOUT_TIMEOUT);
}

static void usb_write_port_cancel(struct intf_hdl *pintfhdl)
{
	int i, j;
	_adapter	*padapter = pintfhdl->padapter;
	struct xmit_buf *pxmitbuf = (struct xmit_buf *)padapter->xmitpriv.pxmitbuf;

	DBG_8192C("usb_write_port_cancel \n");
	
	padapter->bWritePortCancel = _TRUE;	
	
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
	pxmitbuf = (struct xmit_buf*)padapter->xmitpriv.pxmit_extbuf;

	for (i = 0; i < NR_XMIT_EXTBUFF; i++)
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

void rtl8188eu_set_intf_ops(struct _io_ops	*pops)
{		
	_func_enter_;		

	_rtw_memset((u8 *)pops, 0, sizeof(struct _io_ops));	

	pops->_read8 = &usb_read8;
	pops->_read16 = &usb_read16;
	pops->_read32 = &usb_read32;
	pops->_read_mem = &usb_read_mem;
	pops->_read_port = &usb_read_port;	
	
	pops->_write8 = &usb_write8;
	pops->_write16 = &usb_write16;
	pops->_write32 = &usb_write32;
	pops->_writeN = &usb_writeN;
	
#ifdef CONFIG_USB_SUPPORT_ASYNC_VDN_REQ	
	pops->_write8_async= &usb_async_write8;
	pops->_write16_async = &usb_async_write16;
	pops->_write32_async = &usb_async_write32;
#endif	
	pops->_write_mem = &usb_write_mem;
	pops->_write_port = &usb_write_port_async;
	pops->_write_port_sync = &usb_write_port_sync;

	pops->_read_port_cancel = &usb_read_port_cancel;
	pops->_write_port_cancel = &usb_write_port_cancel;

#ifdef CONFIG_USB_INTERRUPT_IN_PIPE
	pops->_read_interrupt = &usb_read_interrupt;
#endif

	_func_exit_;

}

void rtl8188eu_set_hw_type(_adapter *padapter)
{
	padapter->chip_type = RTL8188E;
	padapter->HardwareType = HARDWARE_TYPE_RTL8188EU;
	DBG_871X("CHIP TYPE: RTL8188E\n");
}

