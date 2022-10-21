// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 * usb_ops.c
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
 * Linux device driver for RTL8192SU
 *
 * Modifications for inclusion into the Linux staging tree are
 * Copyright(c) 2010 Larry Finger. All rights reserved.
 *
 * Contact information:
 * WLAN FAE <wlanfae@realtek.com>
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 ******************************************************************************/

#define _HCI_OPS_C_

#include "osdep_service.h"
#include "drv_types.h"
#include "osdep_intf.h"
#include "usb_ops.h"
#include "recv_osdep.h"

static u8 usb_read8(struct intf_hdl *intfhdl, u32 addr)
{
	u8 request;
	u8 requesttype;
	u16 wvalue;
	u16 index;
	u16 len;
	int status;
	__le32 data = 0;
	struct intf_priv *intfpriv = intfhdl->pintfpriv;

	request = 0x05;
	requesttype = 0x01; /* read_in */
	index = 0;
	wvalue = (u16)(addr & 0x0000ffff);
	len = 1;
	status = r8712_usbctrl_vendorreq(intfpriv, request, wvalue, index,
					 &data, len, requesttype);
	if (status < 0)
		return 0;
	return (u8)(le32_to_cpu(data) & 0x0ff);
}

static u16 usb_read16(struct intf_hdl *intfhdl, u32 addr)
{
	u8 request;
	u8 requesttype;
	u16 wvalue;
	u16 index;
	u16 len;
	int status;
	__le32 data = 0;
	struct intf_priv *intfpriv = intfhdl->pintfpriv;

	request = 0x05;
	requesttype = 0x01; /* read_in */
	index = 0;
	wvalue = (u16)(addr & 0x0000ffff);
	len = 2;
	status = r8712_usbctrl_vendorreq(intfpriv, request, wvalue, index,
					 &data, len, requesttype);
	if (status < 0)
		return 0;
	return (u16)(le32_to_cpu(data) & 0xffff);
}

static u32 usb_read32(struct intf_hdl *intfhdl, u32 addr)
{
	u8 request;
	u8 requesttype;
	u16 wvalue;
	u16 index;
	u16 len;
	int status;
	__le32 data = 0;
	struct intf_priv *intfpriv = intfhdl->pintfpriv;

	request = 0x05;
	requesttype = 0x01; /* read_in */
	index = 0;
	wvalue = (u16)(addr & 0x0000ffff);
	len = 4;
	status = r8712_usbctrl_vendorreq(intfpriv, request, wvalue, index,
					 &data, len, requesttype);
	if (status < 0)
		return 0;
	return le32_to_cpu(data);
}

static void usb_write8(struct intf_hdl *intfhdl, u32 addr, u8 val)
{
	u8 request;
	u8 requesttype;
	u16 wvalue;
	u16 index;
	u16 len;
	__le32 data;
	struct intf_priv *intfpriv = intfhdl->pintfpriv;

	request = 0x05;
	requesttype = 0x00; /* write_out */
	index = 0;
	wvalue = (u16)(addr & 0x0000ffff);
	len = 1;
	data = cpu_to_le32((u32)val & 0x000000ff);
	r8712_usbctrl_vendorreq(intfpriv, request, wvalue, index, &data, len,
				requesttype);
}

static void usb_write16(struct intf_hdl *intfhdl, u32 addr, u16 val)
{
	u8 request;
	u8 requesttype;
	u16 wvalue;
	u16 index;
	u16 len;
	__le32 data;
	struct intf_priv *intfpriv = intfhdl->pintfpriv;

	request = 0x05;
	requesttype = 0x00; /* write_out */
	index = 0;
	wvalue = (u16)(addr & 0x0000ffff);
	len = 2;
	data = cpu_to_le32((u32)val & 0x0000ffff);
	r8712_usbctrl_vendorreq(intfpriv, request, wvalue, index, &data, len,
				requesttype);
}

static void usb_write32(struct intf_hdl *intfhdl, u32 addr, u32 val)
{
	u8 request;
	u8 requesttype;
	u16 wvalue;
	u16 index;
	u16 len;
	__le32 data;
	struct intf_priv *intfpriv = intfhdl->pintfpriv;

	request = 0x05;
	requesttype = 0x00; /* write_out */
	index = 0;
	wvalue = (u16)(addr & 0x0000ffff);
	len = 4;
	data = cpu_to_le32(val);
	r8712_usbctrl_vendorreq(intfpriv, request, wvalue, index, &data, len,
				requesttype);
}

void r8712_usb_set_intf_option(u32 *option)
{
	*option = ((*option) | _INTF_ASYNC_);
}

static void usb_intf_hdl_init(u8 *priv)
{
}

static void usb_intf_hdl_unload(u8 *priv)
{
}

static void usb_intf_hdl_open(u8 *priv)
{
}

static void usb_intf_hdl_close(u8 *priv)
{
}

void r8712_usb_set_intf_funs(struct intf_hdl *intfhdl)
{
	intfhdl->intf_hdl_init = usb_intf_hdl_init;
	intfhdl->intf_hdl_unload = usb_intf_hdl_unload;
	intfhdl->intf_hdl_open = usb_intf_hdl_open;
	intfhdl->intf_hdl_close = usb_intf_hdl_close;
}

void r8712_usb_set_intf_ops(struct _io_ops *ops)
{
	memset((u8 *)ops, 0, sizeof(struct _io_ops));
	ops->_read8 = usb_read8;
	ops->_read16 = usb_read16;
	ops->_read32 = usb_read32;
	ops->_read_port = r8712_usb_read_port;
	ops->_write8 = usb_write8;
	ops->_write16 = usb_write16;
	ops->_write32 = usb_write32;
	ops->_write_mem = r8712_usb_write_mem;
	ops->_write_port = r8712_usb_write_port;
}
