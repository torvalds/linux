// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2018-2019  Realtek Corporation
 */

#include <linux/module.h>
#include <linux/usb.h>
#include "main.h"
#include "rtw8821c.h"
#include "usb.h"

static const struct usb_device_id rtw_8821cu_id_table[] = {
	{ USB_DEVICE_AND_INTERFACE_INFO(RTW_USB_VENDOR_ID_REALTEK, 0x2006, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8821c_hw_spec) },
	{ USB_DEVICE_AND_INTERFACE_INFO(RTW_USB_VENDOR_ID_REALTEK, 0x8731, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8821c_hw_spec) },
	{ USB_DEVICE_AND_INTERFACE_INFO(RTW_USB_VENDOR_ID_REALTEK, 0x8811, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8821c_hw_spec) },
	{ USB_DEVICE_AND_INTERFACE_INFO(RTW_USB_VENDOR_ID_REALTEK, 0xb820, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8821c_hw_spec) },
	{ USB_DEVICE_AND_INTERFACE_INFO(RTW_USB_VENDOR_ID_REALTEK, 0xb82b, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8821c_hw_spec) },
	{ USB_DEVICE_AND_INTERFACE_INFO(RTW_USB_VENDOR_ID_REALTEK, 0xc80c, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8821c_hw_spec) },
	{ USB_DEVICE_AND_INTERFACE_INFO(RTW_USB_VENDOR_ID_REALTEK, 0xc811, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8821c_hw_spec) },
	{ USB_DEVICE_AND_INTERFACE_INFO(RTW_USB_VENDOR_ID_REALTEK, 0xc820, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8821c_hw_spec) },
	{ USB_DEVICE_AND_INTERFACE_INFO(RTW_USB_VENDOR_ID_REALTEK, 0xc821, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8821c_hw_spec) },
	{ USB_DEVICE_AND_INTERFACE_INFO(RTW_USB_VENDOR_ID_REALTEK, 0xc82a, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8821c_hw_spec) },
	{ USB_DEVICE_AND_INTERFACE_INFO(RTW_USB_VENDOR_ID_REALTEK, 0xc82b, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8821c_hw_spec) },
	{ USB_DEVICE_AND_INTERFACE_INFO(0x2001, 0x331d, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8821c_hw_spec) }, /* D-Link */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x7392, 0xc811, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8821c_hw_spec) }, /* Edimax */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x7392, 0xd811, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8821c_hw_spec) }, /* Edimax */
	{},
};
MODULE_DEVICE_TABLE(usb, rtw_8821cu_id_table);

static int rtw_8821cu_probe(struct usb_interface *intf,
			    const struct usb_device_id *id)
{
	return rtw_usb_probe(intf, id);
}

static struct usb_driver rtw_8821cu_driver = {
	.name = "rtw_8821cu",
	.id_table = rtw_8821cu_id_table,
	.probe = rtw_8821cu_probe,
	.disconnect = rtw_usb_disconnect,
};
module_usb_driver(rtw_8821cu_driver);

MODULE_AUTHOR("Hans Ulli Kroll <linux@ulli-kroll.de>");
MODULE_DESCRIPTION("Realtek 802.11ac wireless 8821cu driver");
MODULE_LICENSE("Dual BSD/GPL");
