// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2024  Realtek Corporation
 */

#include <linux/module.h>
#include <linux/usb.h>
#include "main.h"
#include "rtw8821a.h"
#include "usb.h"

static const struct usb_device_id rtw_8821au_id_table[] = {
	{ USB_DEVICE_AND_INTERFACE_INFO(RTW_USB_VENDOR_ID_REALTEK, 0x0811, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8821a_hw_spec) },
	{ USB_DEVICE_AND_INTERFACE_INFO(RTW_USB_VENDOR_ID_REALTEK, 0x0820, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8821a_hw_spec) },
	{ USB_DEVICE_AND_INTERFACE_INFO(RTW_USB_VENDOR_ID_REALTEK, 0x0821, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8821a_hw_spec) },
	{ USB_DEVICE_AND_INTERFACE_INFO(RTW_USB_VENDOR_ID_REALTEK, 0x8822, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8821a_hw_spec) },
	{ USB_DEVICE_AND_INTERFACE_INFO(RTW_USB_VENDOR_ID_REALTEK, 0x0823, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8821a_hw_spec) },
	{ USB_DEVICE_AND_INTERFACE_INFO(RTW_USB_VENDOR_ID_REALTEK, 0xa811, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8821a_hw_spec) },
	{ USB_DEVICE_AND_INTERFACE_INFO(0x0411, 0x0242, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8821a_hw_spec) }, /* Buffalo */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x0411, 0x029b, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8821a_hw_spec) }, /* Buffalo */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x04bb, 0x0953, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8821a_hw_spec) }, /* I-O DATA */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x056e, 0x4007, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8821a_hw_spec) }, /* ELECOM */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x056e, 0x400e, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8821a_hw_spec) }, /* ELECOM */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x056e, 0x400f, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8821a_hw_spec) }, /* ELECOM */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x0846, 0x9052, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8821a_hw_spec) }, /* Netgear */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x0e66, 0x0023, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8821a_hw_spec) }, /* HAWKING */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x2001, 0x3314, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8821a_hw_spec) }, /* D-Link */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x2001, 0x3318, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8821a_hw_spec) }, /* D-Link */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x2019, 0xab32, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8821a_hw_spec) }, /* Planex */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x20f4, 0x804b, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8821a_hw_spec) }, /* TRENDnet */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x2357, 0x011e, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8821a_hw_spec) }, /* TP Link */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x2357, 0x011f, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8821a_hw_spec) }, /* TP Link */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x2357, 0x0120, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8821a_hw_spec) }, /* TP Link */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x3823, 0x6249, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8821a_hw_spec) }, /* Obihai */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x7392, 0xa811, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8821a_hw_spec) }, /* Edimax */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x7392, 0xa812, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8821a_hw_spec) }, /* Edimax */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x7392, 0xa813, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8821a_hw_spec) }, /* Edimax */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x7392, 0xb611, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8821a_hw_spec) }, /* Edimax */
	{},
};
MODULE_DEVICE_TABLE(usb, rtw_8821au_id_table);

static struct usb_driver rtw_8821au_driver = {
	.name = KBUILD_MODNAME,
	.id_table = rtw_8821au_id_table,
	.probe = rtw_usb_probe,
	.disconnect = rtw_usb_disconnect,
};
module_usb_driver(rtw_8821au_driver);

MODULE_AUTHOR("Bitterblue Smith <rtl8821cerfe2@gmail.com>");
MODULE_DESCRIPTION("Realtek 802.11ac wireless 8821au/8811au driver");
MODULE_LICENSE("Dual BSD/GPL");
