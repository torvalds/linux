/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
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
 *****************************************************************************/
#ifndef __USB_HAL_H__
#define __USB_HAL_H__

int usb_init_recv_priv(_adapter *padapter, u16 ini_in_buf_sz);
void usb_free_recv_priv(_adapter *padapter, u16 ini_in_buf_sz);
#ifdef CONFIG_FW_C2H_REG
void usb_c2h_hisr_hdl(_adapter *adapter, u8 *buf);
#endif

u8 rtw_set_hal_ops(_adapter *padapter);

#ifdef CONFIG_RTL8188E
void rtl8188eu_set_hal_ops(_adapter *padapter);
#endif

#if defined(CONFIG_RTL8812A) || defined(CONFIG_RTL8821A)
void rtl8812au_set_hal_ops(_adapter *padapter);
#endif

#ifdef CONFIG_RTL8192E
void rtl8192eu_set_hal_ops(_adapter *padapter);
#endif


#ifdef CONFIG_RTL8723B
void rtl8723bu_set_hal_ops(_adapter *padapter);
#endif

#ifdef CONFIG_RTL8814A
void rtl8814au_set_hal_ops(_adapter *padapter);
#endif /* CONFIG_RTL8814A */

#ifdef CONFIG_RTL8188F
void rtl8188fu_set_hal_ops(_adapter *padapter);
#endif

#ifdef CONFIG_RTL8188GTV
void rtl8188gtvu_set_hal_ops(_adapter *padapter);
#endif

#ifdef CONFIG_RTL8703B
void rtl8703bu_set_hal_ops(_adapter *padapter);
#endif

#ifdef CONFIG_RTL8723D
void rtl8723du_set_hal_ops(_adapter *padapter);
#endif

#ifdef CONFIG_RTL8710B
void rtl8710bu_set_hal_ops(_adapter *padapter);
#endif

#ifdef CONFIG_RTL8192F
void rtl8192fu_set_hal_ops(_adapter *padapter);
#endif /* CONFIG_RTL8192F */

#endif /* __USB_HAL_H__ */
