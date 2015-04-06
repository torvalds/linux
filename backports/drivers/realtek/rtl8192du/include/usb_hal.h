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
 *
 ******************************************************************************/
#ifndef __USB_HAL_H__
#define __USB_HAL_H__


void rtl8192cu_set_hal_ops(struct rtw_adapter * padapter);

void rtl8192du_set_hal_ops(struct rtw_adapter * padapter);
#ifdef CONFIG_WOWLAN
#ifdef CONFIG_WOWLAN_MANUAL
extern int rtw_suspend_toshiba(struct rtw_adapter * adapter);
extern int rtw_resume_toshiba(struct rtw_adapter * adapter);
#endif /*  CONFIG_WOWLAN_MANUAL */
#endif /* CONFIG_WOWLAN */

#endif /* __USB_HAL_H__ */
