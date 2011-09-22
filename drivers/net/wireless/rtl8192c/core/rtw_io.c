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
/*

The purpose of rtw_io.c

a. provides the API 

b. provides the protocol engine

c. provides the software interface between caller and the hardware interface


Compiler Flag Option:

1. CONFIG_SDIO_HCI:
    a. USE_SYNC_IRP:  Only sync operations are provided.
    b. USE_ASYNC_IRP:Both sync/async operations are provided.

2. CONFIG_USB_HCI:
   a. USE_ASYNC_IRP: Both sync/async operations are provided.

3. CONFIG_CFIO_HCI:
   b. USE_SYNC_IRP: Only sync operations are provided.


Only sync read/rtw_write_mem operations are provided.

jackson@realtek.com.tw

*/

#define _RTW_IO_C_
#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <rtw_io.h>
#include <osdep_intf.h>

#if defined (PLATFORM_LINUX) && defined (PLATFORM_WINDOWS)
#error "Shall be Linux or Windows, but not both!\n"
#endif

#ifdef CONFIG_SDIO_HCI
#include <sdio_ops.h>
#endif

#ifdef CONFIG_USB_HCI
#include <usb_ops.h>
#endif

#ifdef CONFIG_PCI_HCI
#include <pci_ops.h>
#endif


u8 rtw_read8(_adapter *adapter, u32 addr)
{
	u8 r_val;
	//struct	io_queue  	*pio_queue = (struct io_queue *)adapter->pio_queue;
	struct io_priv *pio_priv = &adapter->iopriv;
	struct	intf_hdl		*pintfhdl = &(pio_priv->intf);
	u8 (*_read8)(struct intf_hdl *pintfhdl, u32 addr);
	_func_enter_;
	_read8 = pintfhdl->io_ops._read8;

	r_val = _read8(pintfhdl, addr);
	_func_exit_;
	return r_val;
}

u16 rtw_read16(_adapter *adapter, u32 addr)
{
	u16 r_val;
	//struct	io_queue  	*pio_queue = (struct io_queue *)adapter->pio_queue;
	struct io_priv *pio_priv = &adapter->iopriv;
	struct	intf_hdl		*pintfhdl = &(pio_priv->intf);
	u16 	(*_read16)(struct intf_hdl *pintfhdl, u32 addr);
	_func_enter_;
	_read16 = pintfhdl->io_ops._read16;

	r_val = _read16(pintfhdl, addr);
	_func_exit_;
	return r_val;
}
	
u32 rtw_read32(_adapter *adapter, u32 addr)
{
	u32 r_val;
	//struct	io_queue  	*pio_queue = (struct io_queue *)adapter->pio_queue;
	struct io_priv *pio_priv = &adapter->iopriv;
	struct	intf_hdl		*pintfhdl = &(pio_priv->intf);
	u32 	(*_read32)(struct intf_hdl *pintfhdl, u32 addr);
	_func_enter_;
	_read32 = pintfhdl->io_ops._read32;

	r_val = _read32(pintfhdl, addr);
	_func_exit_;
	return r_val;	

}

void _rtw_write8(_adapter *adapter, u32 addr, u8 val)
{
	//struct	io_queue  	*pio_queue = (struct io_queue *)adapter->pio_queue;
	struct io_priv *pio_priv = &adapter->iopriv;
	struct	intf_hdl		*pintfhdl = &(pio_priv->intf);
	void (*_write8)(struct intf_hdl *pintfhdl, u32 addr, u8 val);
	_func_enter_;
	_write8 = pintfhdl->io_ops._write8;

	_write8(pintfhdl, addr, val);
}
void _rtw_write16(_adapter *adapter, u32 addr, u16 val)
{
	//struct	io_queue  	*pio_queue = (struct io_queue *)adapter->pio_queue;
	struct io_priv *pio_priv = &adapter->iopriv;
	struct	intf_hdl		*pintfhdl = &(pio_priv->intf);
	void (*_write16)(struct intf_hdl *pintfhdl, u32 addr, u16 val);
	_func_enter_;
	_write16 = pintfhdl->io_ops._write16;
	
	_write16(pintfhdl, addr, val);
	_func_exit_;

}
void _rtw_write32(_adapter *adapter, u32 addr, u32 val)
{
	//struct	io_queue  	*pio_queue = (struct io_queue *)adapter->pio_queue;
	struct io_priv *pio_priv = &adapter->iopriv;
	struct	intf_hdl		*pintfhdl = &(pio_priv->intf);
	void (*_write32)(struct intf_hdl *pintfhdl, u32 addr, u32 val);
	_func_enter_;
	_write32 = pintfhdl->io_ops._write32;

	_write32(pintfhdl, addr, val);
	_func_exit_;

}

void rtw_writeN(_adapter *adapter, u32 addr ,u32 length , u8 *pdata)
{
	//struct	io_queue  	*pio_queue = (struct io_queue *)adapter->pio_queue;
	struct io_priv *pio_priv = &adapter->iopriv;
        struct	intf_hdl	*pintfhdl = (struct intf_hdl*)(&(pio_priv->intf));
	void (*_writeN)(struct intf_hdl *pintfhdl, u32 addr,u32 length, u8 *pdata);
	_func_enter_;
	_writeN = pintfhdl->io_ops._writeN;

	_writeN(pintfhdl, addr,length,pdata);	
	_func_exit_;

}
void rtw_write8_async(_adapter *adapter, u32 addr, u8 val)
{
	//struct	io_queue  	*pio_queue = (struct io_queue *)adapter->pio_queue;
	struct io_priv *pio_priv = &adapter->iopriv;
	struct	intf_hdl		*pintfhdl = &(pio_priv->intf);
	void (*_write8_async)(struct intf_hdl *pintfhdl, u32 addr, u8 val);
	_func_enter_;
	_write8_async = pintfhdl->io_ops._write8_async;
	
	_write8_async(pintfhdl, addr, val);	
	_func_exit_;

}
void rtw_write16_async(_adapter *adapter, u32 addr, u16 val)
{
	//struct	io_queue  	*pio_queue = (struct io_queue *)adapter->pio_queue;
	struct io_priv *pio_priv = &adapter->iopriv;
	struct	intf_hdl		*pintfhdl = &(pio_priv->intf);
	void (*_write16_async)(struct intf_hdl *pintfhdl, u32 addr, u16 val);
	_func_enter_;
	_write16_async = pintfhdl->io_ops._write16_async;
	
	_write16_async(pintfhdl, addr, val);	
	_func_exit_;

}
void rtw_write32_async(_adapter *adapter, u32 addr, u32 val)
{
	//struct	io_queue  	*pio_queue = (struct io_queue *)adapter->pio_queue;
	struct io_priv *pio_priv = &adapter->iopriv;
	struct	intf_hdl		*pintfhdl = &(pio_priv->intf);
	void (*_write32_async)(struct intf_hdl *pintfhdl, u32 addr, u32 val);
	_func_enter_;
	_write32_async = pintfhdl->io_ops._write32_async;
	
	_write32_async(pintfhdl, addr, val);	
	_func_exit_;

}
void rtw_read_mem(_adapter *adapter, u32 addr, u32 cnt, u8 *pmem)
{
	void (*_read_mem)(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *pmem);	
	//struct	io_queue  	*pio_queue = (struct io_queue *)adapter->pio_queue;
	struct io_priv *pio_priv = &adapter->iopriv;
	struct	intf_hdl		*pintfhdl = &(pio_priv->intf);
	
	_func_enter_;

	if( (adapter->bDriverStopped ==_TRUE) || (adapter->bSurpriseRemoved == _TRUE))
	{
	     RT_TRACE(_module_rtl871x_io_c_, _drv_info_, ("rtw_read_mem:bDriverStopped(%d) OR bSurpriseRemoved(%d)", adapter->bDriverStopped, adapter->bSurpriseRemoved));	    
	     return;
	}	
	
	_read_mem = pintfhdl->io_ops._read_mem;
	
	_read_mem(pintfhdl, addr, cnt, pmem);
	
	_func_exit_;
	
}

void rtw_write_mem(_adapter *adapter, u32 addr, u32 cnt, u8 *pmem)
{	
	void (*_write_mem)(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *pmem);	
	//struct	io_queue  	*pio_queue = (struct io_queue *)adapter->pio_queue;
	struct io_priv *pio_priv = &adapter->iopriv;
	struct	intf_hdl		*pintfhdl = &(pio_priv->intf);

	_func_enter_;
	
	_write_mem = pintfhdl->io_ops._write_mem;
	
	_write_mem(pintfhdl, addr, cnt, pmem);
	
	_func_exit_;	
	
}

void rtw_read_port(_adapter *adapter, u32 addr, u32 cnt, u8 *pmem)
{	
	u32 (*_read_port)(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *pmem);	
	//struct	io_queue  	*pio_queue = (struct io_queue *)adapter->pio_queue;
	struct io_priv *pio_priv = &adapter->iopriv;
	struct	intf_hdl		*pintfhdl = &(pio_priv->intf);
	
	_func_enter_;	

	if( (adapter->bDriverStopped ==_TRUE) || (adapter->bSurpriseRemoved == _TRUE))
	{
	     RT_TRACE(_module_rtl871x_io_c_, _drv_info_, ("rtw_read_port:bDriverStopped(%d) OR bSurpriseRemoved(%d)", adapter->bDriverStopped, adapter->bSurpriseRemoved));	    
	     return;
	}	

	_read_port = pintfhdl->io_ops._read_port;
	
	_read_port(pintfhdl, addr, cnt, pmem);
	 
	_func_exit_;

}

void read_port_cancel(_adapter *adapter)
{
	void (*_read_port_cancel)(struct intf_hdl *pintfhdl);
	struct io_priv *pio_priv = &adapter->iopriv;
	struct intf_hdl *pintfhdl = &(pio_priv->intf);
	
	_read_port_cancel = pintfhdl->io_ops._read_port_cancel;

	if(_read_port_cancel)
		_read_port_cancel(pintfhdl);	
			
}

void rtw_write_port(_adapter *adapter, u32 addr, u32 cnt, u8 *pmem)
{	
	u32 (*_write_port)(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *pmem);
	//struct	io_queue  	*pio_queue = (struct io_queue *)adapter->pio_queue;
	struct io_priv *pio_priv = &adapter->iopriv;
	struct	intf_hdl		*pintfhdl = &(pio_priv->intf);
	
	_func_enter_;	
	
	_write_port = pintfhdl->io_ops._write_port;
	
	_write_port(pintfhdl, addr, cnt, pmem);
	
	 _func_exit_;
	 
}

void write_port_cancel(_adapter *adapter)
{
	void (*_write_port_cancel)(struct intf_hdl *pintfhdl);
	struct io_priv *pio_priv = &adapter->iopriv;
	struct intf_hdl *pintfhdl = &(pio_priv->intf);
	
	_write_port_cancel = pintfhdl->io_ops._write_port_cancel;

	if(_write_port_cancel)
		_write_port_cancel(pintfhdl);	

}


void rtw_attrib_read(_adapter *adapter, u32 addr, u32 cnt, u8 *pmem){
#ifdef CONFIG_SDIO_HCI
	void (*_attrib_read)(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *pmem);
	
	//struct	io_queue  	*pio_queue = (struct io_queue *)adapter->pio_queue;
	struct io_priv *pio_priv = &adapter->iopriv;
	struct	intf_hdl		*pintfhdl = &(pio_priv->intf);
	
	_func_enter_;	
	
	_attrib_read= pintfhdl->io_ops._attrib_read;
	
	_attrib_read(pintfhdl, addr, cnt, pmem);
	
	 _func_exit_;
#endif
}

void rtw_attrib_write(_adapter *adapter, u32 addr, u32 cnt, u8 *pmem){
#ifdef CONFIG_SDIO_HCI
	void (*_attrib_write)(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *pmem);
	
	//struct	io_queue  	*pio_queue = (struct io_queue *)adapter->pio_queue;
	struct io_priv *pio_priv = &adapter->iopriv;
	struct	intf_hdl		*pintfhdl = &(pio_priv->intf);
	
	_func_enter_;	
	
	_attrib_write= pintfhdl->io_ops._attrib_write;
	
	_attrib_write(pintfhdl, addr, cnt, pmem);
	
	 _func_exit_;

#endif
}

int init_io_priv(_adapter *padapter)
{	
	void (*set_intf_ops)(struct _io_ops	*pops);
	struct io_priv	*piopriv = &padapter->iopriv;
	struct intf_hdl *pintf = &piopriv->intf;

	piopriv->padapter = padapter;
	pintf->padapter = padapter;
	pintf->pintf_dev = &padapter->dvobjpriv;

	
#ifdef CONFIG_SDIO_HCI	
	set_intf_ops = &sdio_set_intf_ops;	
#endif //END OF CONFIG_SDIO_HCI


#ifdef CONFIG_USB_HCI	

	if(padapter->chip_type == RTL8188C_8192C)
	{
#ifdef CONFIG_RTL8192C
		set_intf_ops = &rtl8192cu_set_intf_ops;
#endif
	}
	else if(padapter->chip_type == RTL8192D)
	{
#ifdef CONFIG_RTL8192D
		set_intf_ops = &rtl8192du_set_intf_ops;
#endif		
	}
	else
	{
		set_intf_ops = NULL;		
	}
#endif //END OF CONFIG_USB_HCI

#ifdef CONFIG_PCI_HCI

	if(padapter->chip_type == RTL8188C_8192C)
	{
#ifdef CONFIG_RTL8192C
		set_intf_ops = &rtl8192ce_set_intf_ops;
#endif
	}
	else if(padapter->chip_type == RTL8192D)
	{
#ifdef CONFIG_RTL8192D
		set_intf_ops = &rtl8192de_set_intf_ops;
#endif		
	}
	else
	{
		set_intf_ops = NULL;		
	}
#endif //END OF CONFIG_PCI_HCI



	if(set_intf_ops==NULL)
		return _FAIL;


	set_intf_ops(&pintf->io_ops);

	return _SUCCESS;

}

