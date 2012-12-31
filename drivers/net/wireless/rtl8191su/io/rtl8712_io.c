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

#define _RTL8712_IO_C_
#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <rtl871x_io.h>
#include <osdep_intf.h>

#if defined (PLATFORM_LINUX) && defined (PLATFORM_WINDOWS)
#error "Shall be Linux or Windows, but not both!\n"
#endif

#ifdef PLATFORM_LINUX
#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kref.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,0,0))
#include <linux/smp_lock.h>
#endif
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/usb.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20))
#include <linux/usb_ch9.h>
#else
#include <linux/usb/ch9.h>
#endif
#include <linux/circ_buf.h>
#include <asm/uaccess.h>
#include <asm/byteorder.h>
#include <asm/atomic.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,26))
#include <asm/semaphore.h>
#else
#include <linux/semaphore.h>
#endif
#endif


#ifdef CONFIG_SDIO_HCI
#include <sdio_ops.h>
#endif

#ifdef CONFIG_USB_HCI
#include <usb_ops.h>
#endif

u8 read8(_adapter *adapter, u32 addr)
{
	struct io_queue *pio_queue = (struct io_queue*)adapter->pio_queue;
	struct intf_hdl *pintfhdl = &(pio_queue->intf);

	u8 (*_read8)(struct intf_hdl *pintfhdl, u32 addr);
	u8 r_val;

_func_enter_;

	_read8 = pintfhdl->io_ops._read8;
	r_val = _read8(pintfhdl, addr);

_func_exit_;

	return r_val;
}

u16 read16(_adapter *adapter, u32 addr)
{
	struct io_queue *pio_queue = (struct io_queue*)adapter->pio_queue;
	struct intf_hdl *pintfhdl = &(pio_queue->intf);

	u16 (*_read16)(struct intf_hdl *pintfhdl, u32 addr);
	u16 r_val;

_func_enter_;

	_read16 = pintfhdl->io_ops._read16;
	r_val = _read16(pintfhdl, addr);

_func_exit_;

	return r_val;
}
	
u32 read32(_adapter *adapter, u32 addr)
{
	struct io_queue *pio_queue = (struct io_queue*)adapter->pio_queue;
	struct intf_hdl *pintfhdl = &(pio_queue->intf);

	u32 (*_read32)(struct intf_hdl *pintfhdl, u32 addr);
	u32 r_val;

_func_enter_;

	_read32 = pintfhdl->io_ops._read32;
	r_val = _read32(pintfhdl, addr);

_func_exit_;

	return r_val;
}

void write8(_adapter *adapter, u32 addr, u8 val)
{
	struct io_queue *pio_queue = (struct io_queue*)adapter->pio_queue;
	struct intf_hdl *pintfhdl = &(pio_queue->intf);

	void (*_write8)(struct intf_hdl *pintfhdl, u32 addr, u8 val);

_func_enter_;

	_write8 = pintfhdl->io_ops._write8;
	_write8(pintfhdl, addr, val);

_func_exit_;
}

void write16(_adapter *adapter, u32 addr, u16 val)
{
	struct io_queue *pio_queue = (struct io_queue*)adapter->pio_queue;
	struct intf_hdl *pintfhdl = &(pio_queue->intf);

	void (*_write16)(struct intf_hdl *pintfhdl, u32 addr, u16 val);

_func_enter_;

	_write16 = pintfhdl->io_ops._write16;
	_write16(pintfhdl, addr, val);

_func_exit_;
}

void write32(_adapter *adapter, u32 addr, u32 val)
{
	struct io_queue *pio_queue = (struct io_queue*)adapter->pio_queue;
	struct intf_hdl *pintfhdl = (struct intf_hdl*)(&(pio_queue->intf));

	void (*_write32)(struct intf_hdl *pintfhdl, u32 addr, u32 val);

_func_enter_;

	_write32 = pintfhdl->io_ops._write32;
	_write32(pintfhdl, addr, val);	

_func_exit_;
}

void read_mem(_adapter *adapter, u32 addr, u32 cnt, u8 *pmem)
{
	struct io_queue *pio_queue = (struct io_queue*)adapter->pio_queue;
	struct intf_hdl *pintfhdl = &(pio_queue->intf);

	void (*_read_mem)(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *pmem);	

_func_enter_;

	if( (adapter->bDriverStopped ==_TRUE) || (adapter->bSurpriseRemoved == _TRUE)) {
		RT_TRACE(_module_rtl871x_io_c_, _drv_info_, ("read_mem:bDriverStopped(%d) OR bSurpriseRemoved(%d)", adapter->bDriverStopped, adapter->bSurpriseRemoved));	    
		return;
	}

	_read_mem = pintfhdl->io_ops._read_mem;
	_read_mem(pintfhdl, addr, cnt, pmem);
	
_func_exit_;
}

void write_mem(_adapter *adapter, u32 addr, u32 cnt, u8 *pmem)
{	
	struct io_queue *pio_queue = (struct io_queue*)adapter->pio_queue;
	struct intf_hdl *pintfhdl = &(pio_queue->intf);

	void (*_write_mem)(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *pmem);	

_func_enter_;

	_write_mem = pintfhdl->io_ops._write_mem;
	_write_mem(pintfhdl, addr, cnt, pmem);

_func_exit_;
}

void read_port(_adapter *adapter, u32 addr, u32 cnt, u8 *pmem)
{	
	struct io_queue *pio_queue = (struct io_queue*)adapter->pio_queue;
	struct intf_hdl	*pintfhdl = &(pio_queue->intf);

	u32 (*_read_port)(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *pmem);	

_func_enter_;

	if ((adapter->bDriverStopped ==_TRUE) || (adapter->bSurpriseRemoved == _TRUE)) {
		RT_TRACE(_module_rtl871x_io_c_, _drv_info_, ("read_port:bDriverStopped(%d) OR bSurpriseRemoved(%d)", adapter->bDriverStopped, adapter->bSurpriseRemoved));	    
		return;
	}

	_read_port = pintfhdl->io_ops._read_port;
	_read_port(pintfhdl, addr, cnt, pmem);

_func_exit_;
}

void write_port(_adapter *adapter, u32 addr, u32 cnt, u8 *pmem)
{	
	struct io_queue *pio_queue = (struct io_queue*)adapter->pio_queue;
	struct intf_hdl *pintfhdl = &(pio_queue->intf);

	u32 (*_write_port)(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *pmem);

_func_enter_;	

	_write_port = pintfhdl->io_ops._write_port;
	_write_port(pintfhdl, addr, cnt, pmem);

 _func_exit_;
}

void attrib_read(_adapter *adapter, u32 addr, u32 cnt, u8 *pmem) {
#ifdef CONFIG_SDIO_HCI
	struct io_queue *pio_queue = (struct io_queue*)adapter->pio_queue;
	struct intf_hdl *pintfhdl = &(pio_queue->intf);

	void (*_attrib_read)(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *pmem);

_func_enter_;	

	_attrib_read = pintfhdl->io_ops._attrib_read;
	_attrib_read(pintfhdl, addr, cnt, pmem);
	
_func_exit_;
#endif
}
void attrib_write(_adapter *adapter, u32 addr, u32 cnt, u8 *pmem) {
#ifdef CONFIG_SDIO_HCI
	struct io_queue *pio_queue = (struct io_queue*)adapter->pio_queue;
	struct intf_hdl *pintfhdl = &(pio_queue->intf);

	void (*_attrib_write)(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *pmem);

_func_enter_;	

	_attrib_write= pintfhdl->io_ops._attrib_write;
	_attrib_write(pintfhdl, addr, cnt, pmem);

 _func_exit_;
#endif
}

