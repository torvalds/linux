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
 ******************************************************************************/
#ifndef __USB_OPS_LINUX_H__
#define __USB_OPS_LINUX_H__

#define VENDOR_CMD_MAX_DATA_LEN	254

#define RTW_USB_CONTROL_MSG_TIMEOUT	500/* ms */

#define MAX_USBCTRL_VENDORREQ_TIMES	10

int rtl8723au_read_port(struct rtw_adapter *adapter, u32 cnt,
			struct recv_buf *precvbuf);
void rtl8723au_read_port_cancel(struct rtw_adapter *padapter);
int rtl8723au_write_port(struct rtw_adapter *padapter, u32 addr, u32 cnt,
			 struct xmit_buf *pxmitbuf);
void rtl8723au_write_port_cancel(struct rtw_adapter *padapter);
int rtl8723au_read_interrupt(struct rtw_adapter *adapter);

u8 rtl8723au_read8(struct rtw_adapter *padapter, u16 addr);
u16 rtl8723au_read16(struct rtw_adapter *padapter, u16 addr);
u32 rtl8723au_read32(struct rtw_adapter *padapter, u16 addr);
int rtl8723au_write8(struct rtw_adapter *padapter, u16 addr, u8 val);
int rtl8723au_write16(struct rtw_adapter *padapter, u16 addr, u16 val);
int rtl8723au_write32(struct rtw_adapter *padapter, u16 addr, u32 val);
int rtl8723au_writeN(struct rtw_adapter *padapter,
		     u16 addr, u16 length, u8 *pdata);

#endif
