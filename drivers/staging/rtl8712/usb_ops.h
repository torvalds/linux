/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
 *
 * Modifications for inclusion into the Linux staging tree are
 * Copyright(c) 2010 Larry Finger. All rights reserved.
 *
 * Contact information:
 * WLAN FAE <wlanfae@realtek.com>
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 ******************************************************************************/
#ifndef __USB_OPS_H_
#define __USB_OPS_H_

#include "osdep_service.h"
#include "drv_types.h"
#include "osdep_intf.h"

void r8712_usb_write_mem(struct intf_hdl *pintfhdl, u32 addr,
			 u32 cnt, u8 *wmem);
u32 r8712_usb_write_port(struct intf_hdl *pintfhdl, u32 addr,
			 u32 cnt, u8 *wmem);
u32 r8712_usb_read_port(struct intf_hdl *pintfhdl, u32 addr,
			u32 cnt, u8 *rmem);
void r8712_usb_set_intf_option(u32 *poption);
void r8712_usb_set_intf_funs(struct intf_hdl *pintf_hdl);
uint r8712_usb_init_intf_priv(struct intf_priv *pintfpriv);
void r8712_usb_unload_intf_priv(struct intf_priv *pintfpriv);
void r8712_usb_set_intf_ops(struct _io_ops *pops);
void r8712_usb_read_port_cancel(struct _adapter *padapter);
void r8712_usb_write_port_cancel(struct _adapter *padapter);
int r8712_usbctrl_vendorreq(struct intf_priv *pintfpriv, u8 request, u16 value,
			    u16 index, void *pdata, u16 len, u8 requesttype);

#endif

