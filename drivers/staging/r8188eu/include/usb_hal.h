/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#ifndef __USB_HAL_H__
#define __USB_HAL_H__

void rtl8188eu_set_hal_ops(struct adapter *padapter);
#define hal_set_hal_ops	rtl8188eu_set_hal_ops

#endif /* __USB_HAL_H__ */
