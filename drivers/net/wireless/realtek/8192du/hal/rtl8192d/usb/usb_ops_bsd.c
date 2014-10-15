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
#include <rtl8192d_hal.h>

#if defined (PLATFORM_LINUX) && defined (PLATFORM_FREEBSD)

#error "Shall be Linux or FreeBSD, but not both!\n"

#endif

struct zero_bulkout_context{
	void *pbuf;
	void *purb;
	void *pirp;
	void *padapter;
};

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,12))
#define USB_CONTROL_MSG_TIMEOUT	500		//ms
#else
#define USB_CONTROL_MSG_TIMEOUT	HZ/2	//jiffies
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)) || (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,18))
#define _usbctrl_vendorreq_async_callback(urb, regs)	_usbctrl_vendorreq_async_callback(urb)
#define usb_bulkout_zero_complete(purb, regs)	usb_bulkout_zero_complete(purb)
#define usb_write_mem_complete(purb, regs)	usb_write_mem_complete(purb)
#define usb_write_port_complete(purb, regs)	usb_write_port_complete(purb)
#define usb_read_port_complete(purb, regs)	usb_read_port_complete(purb)
#define usb_read_interrupt_complete(purb, regs)	usb_read_interrupt_complete(purb)
#endif

#ifdef CONFIG_USB_VENDOR_REQ_PREALLOC
static int usbctrl_vendorreq(struct dvobj_priv  *pdvobjpriv, u8 request, u16 value, u16 index, void *pdata, u16 len, u8 requesttype)
{
#ifdef PLATFORM_FREEBSD
	struct usb_host_endpoint *pipe;
#else /* PLATFORM_FREEBSD */
	unsigned int pipe;
#endif /* PLATFORM_FREEBSD */
	int status = 0;
	u32 tmp_buflen=0;
	u8 reqtype;
	u8 *pIo_buf;
	_adapter		*padapter = pdvobjpriv->padapter ; 
	struct usb_device *udev=pdvobjpriv->pusbdev;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pdvobjpriv->padapter);
	
	if( (padapter->bSurpriseRemoved) ||(padapter->pwrctrlpriv.pnp_bstop_trx))
	{
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usbctrl_vendorreq:( padapter->bDriverStopped ||padapter->bSurpriseRemoved ||adapter->pwrctrlpriv.pnp_bstop_trx)!!!\n"));
		return(-1);
	}	

	if(len>MAX_VENDOR_REQ_CMD_SIZE)
	{
		DBG_8192C( "[%s] Buffer len error ,vendor request failed\n", __FUNCTION__ );
		return(-1);
	}	

	if ( pdvobjpriv->usb_vendor_req_buf== NULL)
	{
		DBG_8192C( "[%s] usb_vendor_req_buf == NULL \n", __FUNCTION__ );
		return(-1);
	}
	
	_enter_critical_mutex(&pdvobjpriv->usb_vendor_req_mutex, NULL);
	
	pIo_buf = pdvobjpriv->usb_vendor_req_buf;
	_rtw_memset(pIo_buf, 0, MAX_VENDOR_REQ_CMD_SIZE);		
		
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

	if (requesttype == 0x01)
	{
		if (pHalData->interfaceIndex!=0)
		{
			if(value<0x1000)
				value|=0x4000;
			else if ((value&MAC1_ACCESS_PHY0) && !(value&0x8000))
				value &= 0xfff;
			index = 0;
		}

		pipe = usb_rcvctrlpipe(udev, 0);//read_in
		reqtype =  REALTEK_USB_VENQT_READ;		
	} 
	else 
	{
		if (pHalData->interfaceIndex!=0)
		{
			if(value<0x1000)
				value|=0x4000;
			else if((value&MAC1_ACCESS_PHY0) && !(value&0x8000))// MAC1 need to access PHY0
				value &= 0xFFF;
			index = 0;
		}

		pipe = usb_sndctrlpipe(udev, 0);//write_out
		reqtype =  REALTEK_USB_VENQT_WRITE;		
		_rtw_memcpy( pIo_buf, pdata, len);
	}		
	
	status = usb_control_msg(udev, pipe, request, reqtype, value, index, pIo_buf, len, USB_CONTROL_MSG_TIMEOUT);
	
	if (status < 0)
       {
       	if(status == (-ESHUTDOWN))
		{			
			DBG_8192C("reg 0x%x, usb %s  fail ,status:%d value=0x%x\n", value,(requesttype == 0x01)?"read":"write" , status, *(u32*)pdata);					
			padapter->bDriverStopped=_TRUE;					
		}
		else{
			DBG_8192C("reg 0x%x, usb %s  fail ,status:%d value=0x%x\n", value,(requesttype == 0x01)?"read":"write" , status, *(u32*)pdata);		
#ifdef DBG_CONFIG_ERROR_DETECT
			{
				_adapter *padapter =  pdvobjpriv->padapter;
				HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
				pHalData->srestpriv.Wifi_Error_Status = USB_VEN_REQ_CMD_FAIL;
			}
#endif
			RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("reg 0x%x, usb %s  fail ,status:%d value=0x%x\n", value, status, *(u32*)pdata));
		}
       }
	else if ( status > 0 )   // Success this control transfer.
	{
               if ( requesttype == 0x01 )
               {   // For Control read transfer, we have to copy the read data from pIo_buf to pdata.
                       _rtw_memcpy( pdata, pIo_buf,  status );
               }
	}
	_exit_critical_mutex(&pdvobjpriv->usb_vendor_req_mutex, NULL);
	return status;
}

#else
static int usbctrl_vendorreq(struct dvobj_priv  *pdvobjpriv, u8 request, u16 value, u16 index, void *pdata, u16 len, u8 requesttype)
{
#ifdef PLATFORM_FREEBSD
	struct usb_host_endpoint *pipe;
#else /* PLATFORM_FREEBSD */
	unsigned int	pipe;
#endif /* PLATFORM_FREEBSD */
	int	status;
	u8	reqtype;
	u32	tmp_buflen=0;
	_adapter		*padapter = pdvobjpriv->padapter ; 

#ifdef CONFIG_USE_USB_BUFFER_ALLOC
	dma_addr_t	dma_addr;
#else

#ifndef CONFIG_DYNAMIC_ALLOCIATE_VENDOR_CMD
	u8	tmp_buf[MAX_USB_IO_CTL_SIZE];
#endif

#endif
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pdvobjpriv->padapter);
	struct usb_device	*udev=pdvobjpriv->pusbdev;
		
	// Added by Albert 2010/02/09
	// For mstar platform, mstar suggests the address for USB IO should be 16 bytes alignment.
	// Trying to fix it here.

	u8 *palloc_buf, *pIo_buf;
	if( (padapter->bSurpriseRemoved) ||(padapter->pwrctrlpriv.pnp_bstop_trx))
	{
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usbctrl_vendorreq:( padapter->bDriverStopped ||padapter->bSurpriseRemoved ||adapter->pwrctrlpriv.pnp_bstop_trx)!!!\n"));
		return _FAIL;
	}

	if(len>MAX_VENDOR_REQ_CMD_SIZE)
	{
		DBG_8192C( "[%s] Buffer len error ,vendor request failed\n", __FUNCTION__ );
		return(-1);
	}

#ifdef CONFIG_USE_USB_BUFFER_ALLOC

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,35))
	pIo_buf = usb_alloc_coherent(udev, (size_t)len, GFP_ATOMIC, &dma_addr);
#else
        pIo_buf = rtw_usb_buffer_alloc(udev, (size_t)len, GFP_ATOMIC, &dma_addr);
#endif	

	if(pIo_buf == NULL)
	{
		DBG_8192C( "[%s] Can't alloc memory for vendor request\n", __FUNCTION__);
		return(-1);
	}

	_rtw_memset(pIo_buf, 0, len);

#else
	
#ifdef CONFIG_DYNAMIC_ALLOCIATE_VENDOR_CMD
	palloc_buf = rtw_malloc( (u32) len + ALIGNMENT_UNIT);
	tmp_buflen =  (u32)len + ALIGNMENT_UNIT;
#else
	palloc_buf = tmp_buf;
	tmp_buflen = MAX_USB_IO_CTL_SIZE;
#endif

	if ( palloc_buf== NULL)
	{
		DBG_8192C( "[%s] Can't alloc memory for vendor request\n", __FUNCTION__ );
		return(-1);
	}

	_rtw_memset(palloc_buf, 0, tmp_buflen);

	pIo_buf = (u8 *)N_BYTE_ALIGMENT((SIZE_PTR)(palloc_buf), ALIGNMENT_UNIT);

#endif

	if (requesttype == 0x01)
	{
		if((value & 0xff00) == 0xff00)
		{
			// Temply for pomelo read/write 0x00-0x100 ,will removed when the 0xffxx register eanble  zhiyuan 2009/10/23 
			value &= 0x00ff;
		}
		if (pHalData->interfaceIndex!=0)
		{
			if(value<0x1000)
				value|=0x4000;
			else if ((value&MAC1_ACCESS_PHY0) && !(value&0x8000))
				value &= 0xfff;
			index = 0;
		}

		pipe = usb_rcvctrlpipe(udev, 0);//read_in
		reqtype =  REALTEK_USB_VENQT_READ;		
	} 
	else 
	{
		if((value & 0xff00) == 0xff00)
		{
			// Temply for pomelo read/write 0x00-0x100 ,will removed when the 0xffxx register eanble  zhiyuan 2009/10/23 
			value &= 0x00ff;
		}
		if (pHalData->interfaceIndex!=0)
		{
			if(value<0x1000)
				value|=0x4000;
			else if((value&MAC1_ACCESS_PHY0) && !(value&0x8000))// MAC1 need to access PHY0
				value &= 0xFFF;
			index = 0;
		}

		pipe = usb_sndctrlpipe(udev, 0);//write_out
		reqtype =  REALTEK_USB_VENQT_WRITE;		
		_rtw_memcpy( pIo_buf, pdata, len);
	}		
	
        status = rtw_usb_control_msg(udev, pipe, request, reqtype, value, index, pIo_buf, len, USB_CONTROL_MSG_TIMEOUT);
	
	if (status < 0)
       {
		DBG_8192C("reg 0x%x, usb read/write TimeOut! status:%d value=0x%x\n", value, status, *(u32*)pdata);
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("reg 0x%x, usb_read8 TimeOut! status:0x%x value=0x%x\n", value, status, *(u32*)pdata));
       }
	else if ( status > 0 )   // Success this control transfer.
	{
               if ( requesttype == 0x01 )
               {   // For Control read transfer, we have to copy the read data from pIo_buf to pdata.
                       _rtw_memcpy( pdata, pIo_buf,  status );
               }
	}

#ifdef CONFIG_USE_USB_BUFFER_ALLOC

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,35))
	usb_free_coherent(udev, (size_t)len, pIo_buf, dma_addr);
#else
        rtw_usb_buffer_free(udev, (size_t)len, pIo_buf, dma_addr);
#endif

#else

#ifdef CONFIG_DYNAMIC_ALLOCIATE_VENDOR_CMD
	rtw_mfree( palloc_buf,tmp_buflen);
#endif

#endif

	return status;

}
#endif

static u8 usb_read8(struct intf_hdl *pintfhdl, u32 addr)
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
	len = 1;	
	
	usbctrl_vendorreq(pdvobjpriv, request, wvalue, index, &data, len, requesttype);

	_func_exit_;

	return (u8)(le32_to_cpu(data)&0x0ff);
		
}

static u16 usb_read16(struct intf_hdl *pintfhdl, u32 addr)
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
	len = 2;	
	
	usbctrl_vendorreq(pdvobjpriv, request, wvalue, index, &data, len, requesttype);

	_func_exit_;

	return (u16)(le32_to_cpu(data)&0xffff);
	
}

static void usb_read_rf_byfw(struct dvobj_priv  *pdvobjpriv, u16 byteCount, u32 registerIndex, PVOID buffer)
{
	u16	wPage = 0x0000, offset;
	PADAPTER	Adapter = pdvobjpriv->padapter;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u8	RFPath=0,nPHY=0;

	RFPath =(u8) ((registerIndex&0xff0000)>>16);
	
	if (pHalData->interfaceIndex!=0)
	{ 
		nPHY = 1; //MAC1
		if(registerIndex&MAC1_ACCESS_PHY0)// MAC1 need to access PHY0
			nPHY = 0;
	}
	else
	{
		if(registerIndex&MAC0_ACCESS_PHY1)
			nPHY = 1;
	}
	registerIndex &= 0xFF;
	wPage = ((nPHY<<7)|(RFPath<<5)|8)<<8;
	offset = (u16)registerIndex;
	
	//
	// IN a vendor request to read back MAC register.
	//
	usbctrl_vendorreq(pdvobjpriv, 0x05, offset, wPage, buffer, byteCount, 0x01);

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

	if((addr&0xff000000)>>24 == 0x66){
		usb_read_rf_byfw(pdvobjpriv, len, addr, &data);
	}
	else {
		usbctrl_vendorreq(pdvobjpriv, request, wvalue, index, &data, len, requesttype);
	}

	_func_exit_;

	return le32_to_cpu(data);
	
}

static void usb_write8(struct intf_hdl *pintfhdl, u32 addr, u8 val)
{
	u8 request;
	u8 requesttype;
	u16 wvalue;
	u16 index;
	u16 len;
	u32 data;
	struct dvobj_priv  *pdvobjpriv = (struct dvobj_priv  *)pintfhdl->pintf_dev;   
	
	_func_enter_;

	request = 0x05;
	requesttype = 0x00;//write_out
	index = 0;//n/a

	wvalue = (u16)(addr&0x0000ffff);
	len = 1;
	
	data = val;
	data = cpu_to_le32(data&0x000000ff);
	
	usbctrl_vendorreq(pdvobjpriv, request, wvalue, index, &data, len, requesttype);
	
	_func_exit_;
	
}

static void usb_write16(struct intf_hdl *pintfhdl, u32 addr, u16 val)
{	
	u8 request;
	u8 requesttype;
	u16 wvalue;
	u16 index;
	u16 len;
	u32 data;
	struct dvobj_priv  *pdvobjpriv = (struct dvobj_priv  *)pintfhdl->pintf_dev;   
	
	_func_enter_;

	request = 0x05;
	requesttype = 0x00;//write_out
	index = 0;//n/a

	wvalue = (u16)(addr&0x0000ffff);
	len = 2;
	
	data = val;
	data = cpu_to_le32(data&0x0000ffff);
	
	usbctrl_vendorreq(pdvobjpriv, request, wvalue, index, &data, len, requesttype);
	
	_func_exit_;
	
}

static void usb_write32(struct intf_hdl *pintfhdl, u32 addr, u32 val)
{
	u8 request;
	u8 requesttype;
	u16 wvalue;
	u16 index;
	u16 len;
	u32 data;
	struct dvobj_priv  *pdvobjpriv = (struct dvobj_priv  *)pintfhdl->pintf_dev;   
	
	_func_enter_;

	request = 0x05;
	requesttype = 0x00;//write_out
	index = 0;//n/a

	wvalue = (u16)(addr&0x0000ffff);
	len = 4;
	data = cpu_to_le32(val);	
	
	usbctrl_vendorreq(pdvobjpriv, request, wvalue, index, &data, len, requesttype);
	
	_func_exit_;
	
}
#define VENDOR_CMD_MAX_DATA_LEN	254
void usb_writeN(struct intf_hdl *pintfhdl, u32 addr, u32 length, u8 *pdata);
void usb_writeN(struct intf_hdl *pintfhdl, u32 addr, u32 length, u8 *pdata)
{
	u8 request;
	u8 requesttype;
	u16 wvalue;
	u16 index;
	u16 len;
	u8 buf[VENDOR_CMD_MAX_DATA_LEN]={0};
	struct dvobj_priv  *pdvobjpriv = (struct dvobj_priv  *)pintfhdl->pintf_dev;  
	
	_func_enter_;

	request = 0x05;
	requesttype = 0x00;//write_out
	index = 0;//n/a

	wvalue = (u16)(addr&0x0000ffff);
	len = length;
	 _rtw_memcpy(buf, pdata, len );
	
	usbctrl_vendorreq(pdvobjpriv, request, wvalue, index, buf, len, requesttype);
	
	_func_exit_;
	
}

#ifdef CONFIG_USB_SUPPORT_ASYNC_VDN_REQ
#ifndef PLATFORM_FREEBSD
static void _usbctrl_vendorreq_async_callback(struct urb *urb, struct pt_regs *regs)
{
	if(urb){
		if(urb->context){
			rtw_mfree(urb->context);
		}
                rtw_usb_free_urb(urb);
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
		u8 data[VENDOR_CMD_MAX_DATA_LEN];
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

	buf = (struct rtl819x_async_write_data *)rtw_zmalloc(sizeof(*buf));
	if (!buf)
		return -ENOMEM;

	urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (!urb) {
		rtw_mfree((u8*)buf, sizeof(*buf));
		return -ENOMEM;
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
	 	rtw_mfree((u8*)buf, sizeof(*buf));
		usb_free_urb(urb);
	}
	return rc;
}
#endif /* PLATFORM_FREEBSD */

static void usb_write_async(struct usb_device *udev, u32 addr, u32 val, u16 len)
{
#ifndef PLATFORM_FREEBSD
	u8 request;
	u8 requesttype;
	u16 wvalue;
	u16 index;
	u32 data;
	
	requesttype = VENDOR_WRITE;//write_out	
	request = REALTEK_USB_VENQT_CMD_REQ;
	index = REALTEK_USB_VENQT_CMD_IDX;//n/a

	wvalue = (u16)(addr&0x0000ffff);
	data = val & (0xffffffff >> ((4 - len) * 8));
	data = cpu_to_le32(data);
	
	_usbctrl_vendorreq_async_write(udev, request, wvalue, index, &data, len, requesttype);
#else /* PLATFORM_FREEBSD */
	DBG_8192C("*** %s() is not implemented! ***\n", __FUNCTION__);
#endif /* PLATFORM_FREEBSD */
}
static void usb_async_write8(struct intf_hdl *pintfhdl, u32 addr, u8 val)
{	
	u32 data;
	struct dvobj_priv  *pdvobjpriv = (struct dvobj_priv  *)pintfhdl->pintf_dev;   
	struct usb_device *udev=pdvobjpriv->pusbdev;

	_func_enter_;
	data = cpu_to_le32(val & 0xFF);	
	usb_write_async(udev, addr, val, 1);
	_func_exit_;	
}

static void usb_async_write16(struct intf_hdl *pintfhdl, u32 addr, u16 val)
{	
	u32 data;
	struct dvobj_priv  *pdvobjpriv = (struct dvobj_priv  *)pintfhdl->pintf_dev;   
	struct usb_device *udev=pdvobjpriv->pusbdev;

	_func_enter_;
	data = cpu_to_le32(val & 0xFFFF);	
	usb_write_async(udev, addr, val, 2);
	_func_exit_;	
}
static void usb_async_write32(struct intf_hdl *pintfhdl, u32 addr, u32 val)
{	
	u32 data;
	struct dvobj_priv  *pdvobjpriv = (struct dvobj_priv  *)pintfhdl->pintf_dev;   
	struct usb_device *udev=pdvobjpriv->pusbdev;

	_func_enter_;
	data = cpu_to_le32(val);	
	usb_write_async(udev, addr, val, 4);
	_func_exit_;	
}
#endif

static struct usb_host_endpoint * ffaddr2pipehdl(struct dvobj_priv *pdvobj, u32 addr)
{
	struct usb_host_endpoint *pipe = NULL;
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
		//ep_num = (pHalData->Queue2EPNum[(u8)addr] & 0x0f);
		ep_num = pHalData->Queue2EPNum[addr];
		
		pipe = usb_sndbulkpipe(pusbd, ep_num);

		return pipe;
	}

	return pipe;

}
#ifndef PLATFORM_FREEBSD
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
                        rtw_usb_free_urb(pcontext->purb);
		}

	
		rtw_mfree((u8*)pcontext, sizeof(struct zero_bulkout_context));	
	}	
	

}

static u32 usb_bulkout_zero(struct intf_hdl *pintfhdl, u32 addr)
{	
	int status, len;
	struct usb_host_endpoint *pipe;
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
	

	pcontext = (struct zero_bulkout_context *)rtw_malloc(sizeof(struct zero_bulkout_context));

	pbuf = (unsigned char *)rtw_malloc(sizeof(int));	
        purb = rtw_usb_alloc_urb(0, GFP_ATOMIC);
      	
	len = 0;
	pcontext->pbuf = pbuf;
	pcontext->purb = purb;
	pcontext->pirp = NULL;
	pcontext->padapter = padapter;

	
	//translate DMA FIFO addr to pipehandle
	//pipe = ffaddr2pipehdl(pdvobj, addr);	

        rtw_usb_fill_bulk_urb(purb, pusbd, pipe, 
       				pbuf,
              			len,
              			usb_bulkout_zero_complete,
              			pcontext);//context is pcontext

        status = rtw_usb_submit_urb(purb, GFP_ATOMIC);

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
#endif /* PLATFORM_FREEBSD */

static void usb_read_mem(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *rmem)
{
	
}

static void usb_write_mem(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *wmem)
{
	
}

#ifdef CONFIG_USB_INTERRUPT_IN_PIPE
static void usb_read_interrupt_complete(struct urb *purb, struct pt_regs *regs)
{
	int	err;
	_adapter		*padapter = (_adapter	 *)purb->context;

	padapter->recvpriv.int_cnt ++;
	if(purb->status==0)//SUCCESS
	{
		if (purb->actual_length > sizeof(INTERRUPT_MSG_FORMAT_EX))
		{
			DBG_8192C("usb_read_interrupt_complete: purb->actual_length > sizeof(INTERRUPT_MSG_FORMAT_EX) \n");
		}

                err = rtw_usb_submit_urb(purb, GFP_ATOMIC);
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
	struct usb_host_endpoint *pipe;
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
            				sizeof(INTERRUPT_MSG_FORMAT_EX),
            				usb_read_interrupt_complete,
            				adapter,
            				3);

        err = rtw_usb_submit_urb(precvpriv->int_in_urb, GFP_ATOMIC);
	if((err) && (err != (-EPERM)))
	{
		DBG_8192C("cannot submit interrupt in-token(err = 0x%08x),urb_status = %d\n",err, precvpriv->int_in_urb->status);
		ret = _FAIL;
	}

_func_exit_;

	return ret;
}
#endif

#ifdef CONFIG_USE_USB_BUFFER_ALLOC
static int recvbuf2recvframe(_adapter *padapter, struct recv_buf *precvbuf)
{
	u8	*pbuf;
	u8	qos, shift_sz = 0;
	u16	pkt_cnt, drvinfo_sz;
	u32	pkt_len, pkt_offset,  tmpaddr = 0;
	s32	transfer_len;
	int	alignment = 0;
	struct recv_stat	*prxstat;
	_pkt	*pkt_copy = NULL;	
	union recv_frame	*precvframe = NULL; 
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
		pkt_len =  le32_to_cpu(prxstat->rxdw0)&0x00003fff;	
		

		drvinfo_sz = (le32_to_cpu(prxstat->rxdw0) & 0x000f0000) >> 16;//uint 2^3 = 8 bytes
		drvinfo_sz = drvinfo_sz << 3;
		RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("recvbuf2recvframe: DRV_INFO_SIZE=%d\n", drvinfo_sz));

		pkt_offset = pkt_len + drvinfo_sz + RXDESC_SIZE;

		if((pkt_len<=0) || (pkt_offset>transfer_len))
		{	
			RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("recvbuf2recvframe: pkt_len<=0\n"));
			goto _exit_recvbuf2recvframe;
		}		
	
#if 0
		shift_sz = (le32_to_cpu(prxstat->rxdw0) & 0x03000000) >> 24;
#else
		//shift_sz deponds on qos bit
		qos = (le32_to_cpu(prxstat->rxdw0) & 0x00800000) >> 23;
		//	Modified by Albert 20101213
		//	For 8 bytes IP header alignment.
		shift_sz = (qos==1) ? 6:0;
#endif

		precvframe = rtw_alloc_recvframe(pfree_recv_queue);
		if(precvframe==NULL)
		{
			RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("recvbuf2recvframe: precvframe==NULL\n"));
			goto _exit_recvbuf2recvframe;
		}

		_rtw_init_listhead(&precvframe->u.hdr.list);	
		precvframe->u.hdr.precvbuf = NULL;	//can't access the precvbuf for new arch.
		precvframe->u.hdr.len=0;

		pkt_copy = rtw_skb_alloc((pkt_offset>1612?pkt_offset:1612) + shift_sz + 8);

		if(pkt_copy)
		{					
			tmpaddr = (u32)pkt_copy->data;	
			alignment = tmpaddr & (7);			
			skb_reserve(pkt_copy, (8 - alignment));//force pkt_copy->data at 8-byte alignment address
			
			skb_reserve(pkt_copy, shift_sz);//force ip_hdr at 8-byte alignment address according to shift_sz.
			
			//pkt_copy->dev = padapter->pnetdev;
			
			_rtw_memcpy(pkt_copy->data, pbuf, pkt_offset);
			precvframe->u.hdr.pkt = pkt_copy;
			precvframe->u.hdr.rx_head = precvframe->u.hdr.rx_data = precvframe->u.hdr.rx_tail = pkt_copy->data;
			precvframe->u.hdr.rx_end = pkt_copy->data + (pkt_offset>1612?pkt_offset:1612);
		}
		else
		{	
			DBG_8192C("recvbuf2recvframe:can not allocate memory for skb copy\n");				
			//precvframe->u.hdr.pkt = rtw_skb_clone(pskb);
			//precvframe->u.hdr.rx_head = precvframe->u.hdr.rx_data = precvframe->u.hdr.rx_tail = pbuf;
			//precvframe->u.hdr.rx_end = pbuf + (pkt_offset>1612?pkt_offset:1612);

			precvframe->u.hdr.pkt = NULL;
			rtw_free_recvframe(precvframe, pfree_recv_queue);

			goto _exit_recvbuf2recvframe;
		}

		recvframe_put(precvframe, pkt_len + drvinfo_sz + RXDESC_SIZE);
		recvframe_pull(precvframe, drvinfo_sz + RXDESC_SIZE);	

#if CONFIG_USB_RX_AGGREGATION	
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

		//because the endian issue, driver avoid reference to the rxstat after calling update_recvframe_attrib_from_recvstat();
		rtl8192cu_update_recvframe_attrib_from_recvstat(precvframe, prxstat);		
		
		if(rtw_recv_entry(precvframe) != _SUCCESS)
		{
			RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("recvbuf2recvframe: rtw_recv_entry(precvframe) != _SUCCESS\n"));
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

void rtl8192du_recv_tasklet(void *priv)
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

u32 usb_read_port(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *rmem);
u32 usb_read_port(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *rmem)
{		
	int err;
	struct usb_host_endpoint *pipe;	
	u32 ret = _SUCCESS;
	PURB purb = NULL;	
	struct recv_buf	*precvbuf = (struct recv_buf *)rmem;
	struct dvobj_priv	*pdvobj = (struct dvobj_priv *)pintfhdl->pintf_dev;
	_adapter		*adapter = (_adapter *)pdvobj->padapter;
	struct recv_priv	*precvpriv = &adapter->recvpriv;
	struct usb_device	*pusbd = pdvobj->pusbdev;

	if(adapter->bDriverStopped || adapter->bSurpriseRemoved ||adapter->pwrctrlpriv.pnp_bstop_trx)
	{
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port:( padapter->bDriverStopped ||padapter->bSurpriseRemoved ||adapter->pwrctrlpriv.pnp_bstop_trx)!!!\n"));
		return _FAIL;
	}

	if(precvbuf !=NULL)
	{	
		rtl8192du_init_recvbuf(adapter, precvbuf);

		if(precvbuf->pbuf)
		{			
			precvpriv->rx_pending_cnt++;
		
			purb = precvbuf->purb;		

			//translate DMA FIFO addr to pipehandle
			pipe = ffaddr2pipehdl(pdvobj, addr);	

                        rtw_usb_fill_bulk_urb(purb, pusbd, pipe, 
						precvbuf->pbuf,
                				MAX_RECVBUF_SZ,
                				usb_read_port_complete,
                				precvbuf);//context is precvbuf

			purb->transfer_dma = precvbuf->dma_transfer_addr;
			purb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;								

                        err = rtw_usb_submit_urb(purb, GFP_ATOMIC); 
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
			
	return ret;
	
}
#else // CONFIG_USE_USB_BUFFER_ALLOC


int recvbuf2recvframe(_adapter *padapter, struct sk_buff  *pskb);

int recvbuf2recvframe(_adapter *padapter, struct sk_buff  *pskb)
{
	u8	*pbuf;
	u8	frag, mf, shift_sz = 0;
	u16	pkt_cnt, drvinfo_sz;
	u32	pkt_len, pkt_offset, skb_len, alloc_sz;
	int	transfer_len;
	struct recv_stat	*prxstat;
#ifdef CONFIG_BSD_RX_USE_MBUF
	struct mbuf 		*pkt_copy = NULL;
#else // CONFIG_BSD_RX_USE_MBUF
	struct sk_buff		*pkt_copy = NULL;
#endif // CONFIG_BSD_RX_USE_MBUF
	union recv_frame	*precvframe = NULL; 
#ifdef CONFIG_USB_RX_AGGREGATION
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
#endif
	struct recv_priv	*precvpriv = &padapter->recvpriv;
	_queue			*pfree_recv_queue = &precvpriv->free_recv_queue;

	transfer_len = pskb->len;	
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
		pkt_len =  le32_to_cpu(prxstat->rxdw0)&0x00003fff;

		mf = (le32_to_cpu(prxstat->rxdw1) >> 27) & 0x1;//more fragment bit
		frag = (le32_to_cpu(prxstat->rxdw2) >> 12) & 0xf;//fragmentation number

		drvinfo_sz = (le32_to_cpu(prxstat->rxdw0) & 0x000f0000) >> 16;//uint 2^3 = 8 bytes
		drvinfo_sz = drvinfo_sz << 3;
		RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("recvbuf2recvframe: DRV_INFO_SIZE=%d\n", drvinfo_sz));

		pkt_offset = pkt_len + drvinfo_sz + RXDESC_SIZE;

		if((pkt_len<=0) || (pkt_len>transfer_len))
		{	
			RT_TRACE(_module_rtl871x_recv_c_,_drv_info_,("recvbuf2recvframe: pkt_len<=0\n"));
			goto _exit_recvbuf2recvframe;
		}		

		if ( ( le32_to_cpu( prxstat->rxdw0 ) >> 23 ) & 0x01 )	//	Qos data, wireless lan header length is 26, LLC is 8, total is 34
		{
			shift_sz = 6;
		}
		else
		{
			shift_sz = 0;
		}

		precvframe = rtw_alloc_recvframe(pfree_recv_queue);
		if(precvframe==NULL)
		{
			RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("recvbuf2recvframe: precvframe==NULL\n"));
			goto _exit_recvbuf2recvframe;
		}

		_rtw_init_listhead(&precvframe->u.hdr.list);	
		precvframe->u.hdr.precvbuf = NULL;	//can't access the precvbuf for new arch.
		precvframe->u.hdr.len=0;

		skb_len = pkt_len;

		// for first fragment packet, driver need allocate 1536+drvinfo_sz+RXDESC_SIZE to defrag packet.
		// modify alloc_sz for recvive crc error packet by thomas 2011-06-02
		if((mf ==1)&&(frag == 0)){
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

#ifdef CONFIG_BSD_RX_USE_MBUF

		pkt_copy = m_getjcl(M_DONTWAIT, MT_DATA, M_PKTHDR,
            		alloc_sz <= MCLBYTES ? MCLBYTES :
#if MJUMPAGESIZE != MCLBYTES
            		alloc_sz <= MJUMPAGESIZE ? MJUMPAGESIZE :
#endif
            		alloc_sz <= MJUM9BYTES ? MJUM9BYTES : MJUM16BYTES);
		//printf("%s()-%d: pkt_copy = %p, pkt_copy->head = %p\n", __FUNCTION__, __LINE__, pkt_copy, pkt_copy->head);
		if(pkt_copy)
		{		
#if 0
			if ((pkt_copy->m_flags & M_PKTHDR) == 0) {
				printf("%s()-%d: pkt_copy->m_flags = %08X\n", __FUNCTION__, __LINE__, pkt_copy->m_flags);
			}
#endif
			//pkt_copy->dev = padapter->pifp;
			pkt_copy->m_pkthdr.rcvif = padapter->pifp;
			pkt_copy->m_len = alloc_sz;
			

			precvframe->u.hdr.pkt = pkt_copy;
			//skb_reserve( pkt_copy, 8 - ((SIZE_PTR)( pkt_copy->data ) & 7 ));//force pkt_copy->data at 8-byte alignment address
			m_adj(pkt_copy, 8 - ((SIZE_PTR)( mtod(pkt_copy, caddr_t) ) & 7 ));
			//skb_reserve( pkt_copy, shift_sz );//force ip_hdr at 8-byte alignment address according to shift_sz.
			m_adj( pkt_copy, shift_sz );//force ip_hdr at 8-byte alignment address according to shift_sz.
									
			//_rtw_memcpy(mtod(pkt_copy, caddr_t), pbuf + (drvinfo_sz + RXDESC_SIZE), skb_len);
			m_copyback(pkt_copy, 0, skb_len, pbuf + (drvinfo_sz + RXDESC_SIZE));
			//printf("%s()-%d: mtod(pkt_copy) = %p\n", __FUNCTION__, __LINE__, mtod(pkt_copy, caddr_t));
			
			//precvframe->u.hdr.rx_head = precvframe->u.hdr.rx_data = precvframe->u.hdr.rx_tail = pkt_copy->data;
			precvframe->u.hdr.rx_head = precvframe->u.hdr.rx_data = precvframe->u.hdr.rx_tail = mtod(pkt_copy, caddr_t);
			//precvframe->u.hdr.rx_end = pkt_copy->data + alloc_sz;
			precvframe->u.hdr.rx_end = mtod(pkt_copy, caddr_t) + alloc_sz;
			
			
			//printf("%s()-%d: pkt = %p, head = %p\n", __FUNCTION__, __LINE__, precvframe->u.hdr.pkt, precvframe->u.hdr.pkt->head);
		}
		else
		{	
#ifdef PLATFORM_FREEBSD
			printf("%s(),LINE %d: allocate failure 881\n",__FUNCTION__,__LINE__);
			rtw_free_recvframe(precvframe, pfree_recv_queue);
			goto _exit_recvbuf2recvframe;
#else // PLATFORM_FREEBSD
			//DBG_8192C("recvbuf2recvframe:can not allocate memory for skb copy\n");				
			precvframe->u.hdr.pkt = rtw_skb_clone(pskb);
			precvframe->u.hdr.rx_head = precvframe->u.hdr.rx_data = precvframe->u.hdr.rx_tail = pbuf;
			precvframe->u.hdr.rx_end = pbuf + alloc_sz;
#endif // PLATFORM_FREEBSD
		}

#else // CONFIG_BSD_RX_USE_MBUF

		pkt_copy = rtw_skb_alloc(alloc_sz);

		//printf("%s()-%d: pkt_copy = %p, pkt_copy->head = %p\n", __FUNCTION__, __LINE__, pkt_copy, pkt_copy->head);
		if(pkt_copy)
		{					
			pkt_copy->dev = padapter->pifp;
			precvframe->u.hdr.pkt = pkt_copy;
			skb_reserve( pkt_copy, 8 - ((SIZE_PTR)( pkt_copy->data ) & 7 ));//force pkt_copy->data at 8-byte alignment address
			skb_reserve( pkt_copy, shift_sz );//force ip_hdr at 8-byte alignment address according to shift_sz.
			_rtw_memcpy(pkt_copy->data, pbuf + (drvinfo_sz + RXDESC_SIZE), skb_len);
			precvframe->u.hdr.rx_head = precvframe->u.hdr.rx_data = precvframe->u.hdr.rx_tail = pkt_copy->data;
			precvframe->u.hdr.rx_end = pkt_copy->data + alloc_sz;
		}
		else
		{	
			printf("%s(),LINE %d: allocate failure 881\n",__FUNCTION__,__LINE__);
			rtw_free_recvframe(precvframe, pfree_recv_queue);
			goto _exit_recvbuf2recvframe;
		}

#endif // CONFIG_BSD_RX_USE_MBUF

		recvframe_put(precvframe, skb_len);
		//recvframe_pull(precvframe, drvinfo_sz + RXDESC_SIZE);

#ifdef CONFIG_USB_RX_AGGREGATION
		switch(pHalData->UsbRxAggMode)
		{
			case USB_RX_AGG_DMA:
			case USB_RX_AGG_DMA_USB:
				pkt_offset = (u16)_RND128(pkt_offset);
				break;
				case USB_RX_AGG_USB:
				pkt_offset = (u16)_RND4(pkt_offset);
				break;
			case USB_RX_AGG_DISABLE:			
			default:				
				break;
		}
#endif  // CONFIG_USB_RX_AGGREGATION

		//because the endian issue, driver avoid reference to the rxstat after calling update_recvframe_attrib_from_recvstat();
		rtl8192du_update_recvframe_attrib_from_recvstat(precvframe, prxstat);
		
		if(rtw_recv_entry(precvframe) != _SUCCESS)
		{
			RT_TRACE(_module_rtl871x_recv_c_,_drv_err_,("recvbuf2recvframe: rtw_recv_entry(precvframe) != _SUCCESS\n"));
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
#endif // CONFIG_USE_USB_BUFFER_ALLOC

void rtl8192du_recv_tasklet(void *priv, int npending)
{
	struct sk_buff  *pskb;
	_adapter		*padapter = (_adapter*)priv;
	struct recv_priv	*precvpriv = &padapter->recvpriv;
	
	while (NULL != (pskb = skb_dequeue(&precvpriv->rx_skb_queue)))
	{
		if ((padapter->bDriverStopped == _TRUE)||(padapter->bSurpriseRemoved== _TRUE))
		{
			DBG_8192C("recv_tasklet => bDriverStopped or bSurpriseRemoved \n");
			rtw_skb_free(pskb);
			break;
		}
	
		recvbuf2recvframe(padapter, pskb);

#ifdef CONFIG_PREALLOC_RECV_SKB

		pskb->tail = pskb->data;
		pskb->len = 0;
		
		skb_queue_tail(&precvpriv->free_recv_skb_queue, pskb);
		
#else
		rtw_skb_free(pskb);
#endif
				
	}
	
}

#define CONFIG_CALL_RXBUF2FRAME_DIRECTLY	1

void usb_read_port_complete(struct urb *purb, struct pt_regs *regs);
void usb_read_port_complete(struct urb *purb, struct pt_regs *regs)
{
	//_irqL irqL;
	//uint isevt, *pbuf;
	struct recv_buf	*precvbuf = (struct recv_buf *)purb->context;	
	_adapter 			*padapter =(_adapter *)precvbuf->adapter;
	struct recv_priv	*precvpriv = &padapter->recvpriv;
#ifndef CONFIG_CALL_RXBUF2FRAME_DIRECTLY
	struct xmit_priv	*pxmitpriv = &padapter->xmitpriv;	
#endif	
	
	RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port_complete!!!\n"));

	precvpriv->rx_pending_cnt --;

	if(padapter->bSurpriseRemoved || padapter->bDriverStopped||padapter->bReadPortCancel)
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
	
		goto exit;
	}

	if(purb->status==0)//SUCCESS
	{
#if 0 // Had scheduled TX task after enqueue.
		//force tx to enqueue, in receving time to run the task
		taskqueue_enqueue_fast(taskqueue_fast, &pxmitpriv->xmit_tasklet);
#endif
		if ((purb->actual_length > MAX_RECVBUF_SZ) || (purb->actual_length < RXDESC_SIZE))
		{
			RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port_complete: (purb->actual_length > MAX_RECVBUF_SZ) || (purb->actual_length < RXDESC_SIZE)\n"));
			precvbuf->reuse = _TRUE;
			rtw_read_port(padapter, precvpriv->ff_hwaddr, 0, (unsigned char *)precvbuf);
		}
		else 
		{
			precvbuf->transfer_len = purb->actual_length;	

			skb_put(precvbuf->pskb, purb->actual_length);

#ifdef CONFIG_CALL_RXBUF2FRAME_DIRECTLY

			recvbuf2recvframe(padapter, precvbuf->pskb);

			precvbuf->pskb->tail = precvbuf->pskb->data;
			precvbuf->pskb->len = 0;
			
			precvbuf->reuse = _TRUE;


#else // CONFIG_CALL_RXBUF2FRAME_DIRECTLY

			skb_queue_tail(&precvpriv->rx_skb_queue, precvbuf->pskb);

#if 0 // 1: call rx task directly
			rtl8192du_recv_tasklet(padapter, 0);
			//printf("%s()-%d\n", __FUNCTION__, __LINE__);

#else // 1: call rx task directly
			if (skb_queue_len(&precvpriv->rx_skb_queue)<=1)
				taskqueue_enqueue_fast(taskqueue_fast, &precvpriv->recv_tasklet);
#endif // 1: call rx task directly

			precvbuf->pskb = NULL;
			precvbuf->reuse = _FALSE;
#endif // CONFIG_CALL_RXBUF2FRAME_DIRECTLY

			rtw_read_port(padapter, precvpriv->ff_hwaddr, 0, (unsigned char *)precvbuf);			

		}		
	}
	else
	{
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_read_port_complete : purb->status(%d) != 0 \n", purb->status));
	
		DBG_8192C("###=> usb_read_port_complete => urb status(%d)\n", purb->status);

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

u32 usb_read_port(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *rmem);
u32 usb_read_port(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *rmem)
{	
	//_irqL irqL;
	int err;
	struct usb_host_endpoint *pipe;
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
		rtl8192du_init_recvbuf(adapter, precvbuf);

		//re-assign for linux based on skb
		if((precvbuf->reuse == _FALSE) || (precvbuf->pskb == NULL))
		{
			precvbuf->pskb = rtw_skb_alloc(MAX_RECVBUF_SZ + RECVBUFF_ALIGN_SZ);

			if(precvbuf->pskb == NULL)	
			{
				RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("init_recvbuf(): alloc_skb fail!\n"));
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
	
		//_enter_critical(&precvpriv->lock, &irqL);
		//precvpriv->rx_pending_cnt++;
		//precvbuf->irp_pending = _TRUE;
		//_exit_critical(&precvpriv->lock, &irqL);
				
		precvpriv->rx_pending_cnt++;
		
		purb = precvbuf->purb;		

		//translate DMA FIFO addr to pipehandle
		pipe = ffaddr2pipehdl(pdvobj, addr);	

                rtw_usb_fill_bulk_urb(purb, pusbd, pipe, 
						precvbuf->pbuf,
                				MAX_RECVBUF_SZ,
                				usb_read_port_complete,
                				precvbuf);//context is precvbuf

                err = rtw_usb_submit_urb(purb, GFP_ATOMIC); 
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

void usb_read_port_cancel(struct intf_hdl *pintfhdl);
void usb_read_port_cancel(struct intf_hdl *pintfhdl)
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
                        //DBG_8192C("usb_read_port_cancel : rtw_usb_kill_urb \n");                  
                        rtw_usb_kill_urb(precvbuf->purb);           
		}		

		precvbuf++;		
	}

#ifdef CONFIG_USB_INTERRUPT_IN_PIPE
        rtw_usb_kill_urb(padapter->recvpriv.int_in_urb);
#endif
}

void rtl8192du_xmit_tasklet(void *priv)
{	
	int ret = _FALSE;
	_adapter *padapter = (_adapter*)priv;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;

	if(check_fwstate(&padapter->mlmepriv, _FW_UNDER_SURVEY) == _TRUE)
		return;

	while(1)
	{
		if ((padapter->bDriverStopped == _TRUE)||(padapter->bSurpriseRemoved== _TRUE))
		{
			DBG_8192C("xmit_tasklet => bDriverStopped or bSurpriseRemoved \n");
			break;
		}

		ret = rtl8192du_xmitframe_complete(padapter, pxmitpriv, NULL);

		if(ret==_FALSE)
			break;
		
	}

}

static void usb_write_port_complete(struct urb *purb, struct pt_regs *regs)
{
	 	
//	_irqL irqL;
//	int i;
	struct xmit_buf *pxmitbuf = (struct xmit_buf *)purb->context;
	//struct xmit_frame *pxmitframe = (struct xmit_frame *)pxmitbuf->priv_data;
	//_adapter			*padapter = pxmitframe->padapter;
	_adapter	*padapter = pxmitbuf->padapter;
       struct xmit_priv	*pxmitpriv = &padapter->xmitpriv;		
	//struct pkt_attrib *pattrib = &pxmitframe->attrib;
	   
_func_enter_;

	RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("+usb_write_port_complete\n"));

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

	rtw_free_xmitbuf(pxmitpriv, pxmitbuf);
	
	if(padapter->bSurpriseRemoved || padapter->bDriverStopped ||padapter->bWritePortCancel)
	{
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_write_port_complete:bDriverStopped(%d) OR bSurpriseRemoved(%d)", padapter->bDriverStopped, padapter->bSurpriseRemoved));
		goto exit;
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
                        //rtw_usb_clear_halt(pusbdev, purb->pipe);  
			//msleep(10);
		}		
		else if(purb->status == (-ESHUTDOWN))
		{
			RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_write_port_complete: ESHUTDOWN\n"));
						
			padapter->bDriverStopped=_TRUE;
			
			RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_write_port_complete:bDriverStopped=TRUE\n"));

			goto exit;
		}
		else
		{					
			padapter->bSurpriseRemoved=_TRUE;
			DBG_8192C("bSurpriseRemoved=TRUE\n");
			//rtl8192cu_trigger_gpio_0(padapter);
			RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_write_port_complete:bSurpriseRemoved=TRUE\n"));

			goto exit;
		}		

		

	}

	//if(rtw_txframes_pending(padapter))	
	{
				//printf("%s(),%d:\n",__FUNCTION__,__LINE__);
		//tasklet_hi_schedule(&pxmitpriv->xmit_tasklet);
		taskqueue_enqueue_fast(taskqueue_fast, &pxmitpriv->xmit_tasklet);
	}
	

	RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("-usb_write_port_complete\n"));

exit:

_func_exit_;	

}

static u32 usb_write_port(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *wmem)
{    
	   
//	_irqL irqL;
	int status;
	struct usb_host_endpoint *pipe;
	u32 ret;//, bwritezero = _FALSE;
	PURB	purb = NULL;
	_adapter *padapter = (_adapter *)pintfhdl->padapter;
	struct dvobj_priv	*pdvobj = (struct dvobj_priv   *)&padapter->dvobjpriv;	
//	struct xmit_priv	*pxmitpriv = &padapter->xmitpriv;
	struct xmit_buf *pxmitbuf = (struct xmit_buf *)wmem;
	struct xmit_frame *pxmitframe = (struct xmit_frame *)pxmitbuf->priv_data;
	struct usb_device *pusbd = pdvobj->pusbdev;
//	struct pkt_attrib *pattrib = &pxmitframe->attrib;
	
_func_enter_;	

	RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("+usb_write_port\n"));
	
	if((padapter->bDriverStopped) || (padapter->bSurpriseRemoved) ||(padapter->pwrctrlpriv.pnp_bstop_trx))
	{
		RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_write_port:( padapter->bDriverStopped ||padapter->bSurpriseRemoved ||adapter->pwrctrlpriv.pnp_bstop_trx)!!!\n"));	
		return _FAIL;
	}
	
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
	if ( pxmitpriv->free_xmitbuf_cnt%NR_XMITBUFF == 0 
		|| pxmitbuf->ext_tag )
	{
		purb->transfer_flags  &=  (~URB_NO_INTERRUPT);
	} else {
		purb->transfer_flags  |=  URB_NO_INTERRUPT;
		//DBG_8192C("URB_NO_INTERRUPT ");
	}
#endif


        rtw_usb_fill_bulk_urb(purb, pusbd, pipe, 
       				pxmitframe->buf_addr,
              			cnt,
              			usb_write_port_complete,
              			pxmitbuf);//context is pxmitbuf

#ifdef CONFIG_USE_USB_BUFFER_ALLOC
	purb->transfer_dma = pxmitbuf->dma_transfer_addr;
	purb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	purb->transfer_flags |= URB_ZERO_PACKET;
#endif

#if 0
	if (bwritezero)
        {
            purb->transfer_flags |= URB_ZERO_PACKET;           
        }			
#endif

        status = rtw_usb_submit_urb(purb, GFP_ATOMIC);

	if (!status)
	{		
		ret= _SUCCESS;
	}
	else
	{
		DBG_8192C("usb_write_port, status=%d\n", status);
                RT_TRACE(_module_hci_ops_os_c_,_drv_err_,("usb_write_port(): rtw_usb_submit_urb, status=%x\n", status));            
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
                                rtw_usb_kill_urb(pxmitbuf->pxmit_urb[j]);
		        }
		}
		
		pxmitbuf++;
	}
	
}


void rtl8192du_set_intf_ops(struct _io_ops	*pops)
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
	pops->_write_port = &usb_write_port;


	pops->_read_port_cancel = &usb_read_port_cancel;
	pops->_write_port_cancel = &usb_write_port_cancel;

#ifdef CONFIG_USB_INTERRUPT_IN_PIPE
	pops->_read_interrupt = &usb_read_interrupt;
#endif

	_func_exit_;

}

