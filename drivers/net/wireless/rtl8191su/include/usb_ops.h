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
#ifndef __USB_OPS_H_
#define __USB_OPS_H_

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <osdep_intf.h>


extern void usb_read_mem(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *rmem);
extern void usb_write_mem(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *wmem);

extern u32 usb_write_port(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *wmem);
extern u32 usb_read_port(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *rmem);

extern void usb_set_intf_option(u32 *poption);

extern void usb_set_intf_funs(struct intf_hdl *pintf_hdl);

extern uint usb_init_intf_priv(struct intf_priv *pintfpriv);

extern void usb_unload_intf_priv(struct intf_priv *pintfpriv);

extern void usb_intf_hdl_init(u8 *priv);

extern void usb_intf_hdl_unload(u8 *priv);

extern void usb_intf_hdl_open(u8 *priv);

extern void usb_intf_hdl_close(u8 *priv);

extern void usb_set_intf_ops(struct _io_ops *pops);	


void usb_read_port_cancel(_adapter *padapter);
void usb_write_port_cancel(_adapter *padapter);

void _async_protocol_read(struct io_queue *pio_q);
void _async_protocol_write(struct io_queue *pio_q);

void async_rd_io_callback(_adapter *padapter, struct io_req *pio_req, u8 *cnxt);
void usb_cancel_io_irp(_adapter *padapter);
uint usb_write_scsi(struct intf_hdl *pintfhdl, u32 cnt, u8 *wmem);


int usbctrl_vendorreq(struct intf_priv *pintfpriv, u8 request, u16 value, u16 index, void *pdata, u16 len, u8 requesttype);


#ifdef PLATFORM_WINDOWS
void io_irp_timeout_handler (	IN	PVOID	SystemSpecific1,IN	PVOID	FunctionContext,IN	PVOID	SystemSpecific2,IN	PVOID	SystemSpecific3);
#endif
#ifdef PLATFORM_LINUX
void io_irp_timeout_handler(void* FunctionContext);
#endif
#endif

