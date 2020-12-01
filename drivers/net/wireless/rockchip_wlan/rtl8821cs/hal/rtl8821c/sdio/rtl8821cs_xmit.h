/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2016 - 2017 Realtek Corporation.
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
#ifndef _RTL8821CS_XMIT_H_
#define _RTL8821CS_XMIT_H_

#include <drv_types.h>		/* PADAPTER, rtw_xmit.h and etc. */

s32 rtl8821cs_init_xmit_priv(PADAPTER);
void rtl8821cs_free_xmit_priv(PADAPTER);
#ifdef CONFIG_RTW_MGMT_QUEUE
s32 rtl8821cs_hal_mgmt_xmit_enqueue(PADAPTER, struct xmit_frame *);
#endif
s32 rtl8821cs_hal_xmit_enqueue(PADAPTER, struct xmit_frame *);
s32 rtl8821cs_hal_xmit(PADAPTER, struct xmit_frame *);
s32 rtl8821cs_mgnt_xmit(PADAPTER, struct xmit_frame *);

s32 rtl8821cs_xmit_buf_handler(PADAPTER);
thread_return rtl8821cs_xmit_thread(thread_context);

#endif /* _RTL8821CS_XMIT_H_ */
