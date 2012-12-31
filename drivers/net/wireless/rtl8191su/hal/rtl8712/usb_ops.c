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
#define _HCI_OPS_C_

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <osdep_intf.h>
#include <usb_ops.h>
#include <recv_osdep.h>

#if defined (PLATFORM_LINUX) && defined (PLATFORM_WINDOWS)
	#error "Shall be Linux or Windows, but not both!\n"
#endif

#ifndef CONFIG_USB_HCI
	#error "CONFIG_USB_HCI shall be on!\n"
#endif


#ifdef PLATFORM_LINUX
#endif

#ifdef PLATFORM_WINDOWS
#ifdef PLATFORM_OS_XP
#include <usb.h>
#include <usbdlib.h>
#include <usbioctl.h>
#endif
#endif

#include <rtl871x_byteorder.h>

u8 usb_read8(struct intf_hdl *pintfhdl, u32 addr)
{
	u8 request;
	u8 requesttype;
	u16 wvalue;
	u16 index;
	u16 len;
	u32 data;
	struct intf_priv *pintfpriv = pintfhdl->pintfpriv;
	
	_func_enter_;

	request = 0x05;
	requesttype = 0x01;//read_in
	index = 0;//n/a

	wvalue = (u16)(addr&0x0000ffff);
	len = 1;	
	
	usbctrl_vendorreq(pintfpriv, request, wvalue, index, &data, len, requesttype);

	_func_exit_;

	return (u8)(le32_to_cpu(data)&0x0ff);
		
}
u16 usb_read16(struct intf_hdl *pintfhdl, u32 addr)
{       
	u8 request;
	u8 requesttype;
	u16 wvalue;
	u16 index;
	u16 len;
	u32 data;
	struct intf_priv *pintfpriv = pintfhdl->pintfpriv;
	
	_func_enter_;

	request = 0x05;
	requesttype = 0x01;//read_in
	index = 0;//n/a

	wvalue = (u16)(addr&0x0000ffff);
	len = 2;	
	
	usbctrl_vendorreq(pintfpriv, request, wvalue, index, &data, len, requesttype);

	_func_exit_;

	return (u16)(le32_to_cpu(data)&0xffff);
	
}
u32 usb_read32(struct intf_hdl *pintfhdl, u32 addr)
{
	u8 request;
	u8 requesttype;
	u16 wvalue;
	u16 index;
	u16 len;
	u32 data;
	struct intf_priv *pintfpriv = pintfhdl->pintfpriv;
	
	_func_enter_;

	request = 0x05;
	requesttype = 0x01;//read_in
	index = 0;//n/a

	wvalue = (u16)(addr&0x0000ffff);
	len = 4;	
	
	usbctrl_vendorreq(pintfpriv, request, wvalue, index, &data, len, requesttype);

	_func_exit_;

	return le32_to_cpu(data);
	
}
void usb_write8(struct intf_hdl *pintfhdl, u32 addr, u8 val)
{
	u8 request;
	u8 requesttype;
	u16 wvalue;
	u16 index;
	u16 len;
	u32 data;
	struct intf_priv *pintfpriv = pintfhdl->pintfpriv;
	
	_func_enter_;

	request = 0x05;
	requesttype = 0x00;//write_out
	index = 0;//n/a

	wvalue = (u16)(addr&0x0000ffff);
	len = 1;
	
	data = val;
	data = cpu_to_le32(data&0x000000ff);
	
	usbctrl_vendorreq(pintfpriv, request, wvalue, index, &data, len, requesttype);
	
	_func_exit_;
	
}
void usb_write16(struct intf_hdl *pintfhdl, u32 addr, u16 val)
{	
	u8 request;
	u8 requesttype;
	u16 wvalue;
	u16 index;
	u16 len;
	u32 data;
	struct intf_priv *pintfpriv = pintfhdl->pintfpriv;
	
	_func_enter_;

	request = 0x05;
	requesttype = 0x00;//write_out
	index = 0;//n/a

	wvalue = (u16)(addr&0x0000ffff);
	len = 2;
	
	data = val;
	data = cpu_to_le32(data&0x0000ffff);
	
	usbctrl_vendorreq(pintfpriv, request, wvalue, index, &data, len, requesttype);
	
	_func_exit_;
	
}

void usb_write32(struct intf_hdl *pintfhdl, u32 addr, u32 val)
{
	u8 request;
	u8 requesttype;
	u16 wvalue;
	u16 index;
	u16 len;
	u32 data;
	struct intf_priv *pintfpriv = pintfhdl->pintfpriv;
	
	_func_enter_;

	request = 0x05;
	requesttype = 0x00;//write_out
	index = 0;//n/a

	wvalue = (u16)(addr&0x0000ffff);
	len = 4;
	data = cpu_to_le32(val);	
	
	usbctrl_vendorreq(pintfpriv, request, wvalue, index, &data, len, requesttype);
	
	_func_exit_;
	
}

void usb_set_intf_option(u32 *poption)
{
	
	*poption = ((*poption) | _INTF_ASYNC_);
	_func_enter_;
	_func_exit_;
	
}

void usb_intf_hdl_init(u8 *priv)
{
	_func_enter_;
	_func_exit_;
	
	
}

void usb_intf_hdl_unload(u8 *priv)
{
	
	_func_enter_;
	_func_exit_;
	
}
void usb_intf_hdl_open(u8 *priv)
{
	_func_enter_;
	_func_exit_;
	
	
}

void usb_intf_hdl_close(u8 *priv)
{
	
	_func_enter_;
	_func_exit_;
	
	
}


void usb_set_intf_funs(struct intf_hdl *pintf_hdl)
{
	
	pintf_hdl->intf_hdl_init = &usb_intf_hdl_init;
	pintf_hdl->intf_hdl_unload = &usb_intf_hdl_unload;
	pintf_hdl->intf_hdl_open = &usb_intf_hdl_open;
	pintf_hdl->intf_hdl_close = &usb_intf_hdl_close;
	
	_func_enter_;

	_func_exit_;
}

void usb_set_intf_ops(struct _io_ops	*pops)
{
	_func_enter_;
	
	_memset((u8 *)pops, 0, sizeof(struct _io_ops));	

	pops->_read8 = &usb_read8;
	pops->_read16 = &usb_read16;
	pops->_read32 = &usb_read32;
	pops->_read_mem = &usb_read_mem;
	pops->_read_port = &usb_read_port;	
	
	pops->_write8 = &usb_write8;
	pops->_write16 = &usb_write16;
	pops->_write32 = &usb_write32;
	pops->_write_mem = &usb_write_mem;
	pops->_write_port = &usb_write_port;	   

	_func_exit_;

}

#ifdef PLATFORM_WINDOWS
void io_irp_timeout_handler (
	IN	PVOID					SystemSpecific1,
	IN	PVOID					FunctionContext,
	IN	PVOID					SystemSpecific2,
	IN	PVOID					SystemSpecific3
	)
#endif
#ifdef PLATFORM_LINUX
void io_irp_timeout_handler(void *FunctionContext)
#endif
{
       struct intf_priv *pintfpriv= ( struct intf_priv *)FunctionContext;


_func_enter_;		
	RT_TRACE(_module_hci_ops_c_,_drv_err_,("^^^io_irp_timeout_handler ^^^\n"));
	
#ifdef PLATFORM_LINUX
	//pintfpriv->bio_irp_timeout=_TRUE;
	usb_kill_urb(pintfpriv->piorw_urb);
#endif		
_func_exit_;	

}

