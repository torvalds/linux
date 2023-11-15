// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2018-2019  Realtek Corporation
 */

#include <linux/module.h>
#include <linux/usb.h>
#include "main.h"
#include "rtw8822c.h"
#include "usb.h"

static const struct usb_device_id rtw_8822cu_id_table[] = {
	{ USB_DEVICE_AND_INTERFACE_INFO(RTW_USB_VENDOR_ID_REALTEK, 0xc82c, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8822c_hw_spec) },
	{ USB_DEVICE_AND_INTERFACE_INFO(RTW_USB_VENDOR_ID_REALTEK, 0xc812, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8822c_hw_spec) },
	{ USB_DEVICE_AND_INTERFACE_INFO(RTW_USB_VENDOR_ID_REALTEK, 0xc82e, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8822c_hw_spec) },
	{ USB_DEVICE_AND_INTERFACE_INFO(RTW_USB_VENDOR_ID_REALTEK, 0xd820, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8822c_hw_spec) },
	{ USB_DEVICE_AND_INTERFACE_INFO(RTW_USB_VENDOR_ID_REALTEK, 0xd82b, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8822c_hw_spec) },
	{ USB_DEVICE_AND_INTERFACE_INFO(0x13b1, 0x0043, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8822c_hw_spec) }, /* Alpha - Alpha */
	{},
};
MODULE_DEVICE_TABLE(usb, rtw_8822cu_id_table);

static int rtw8822cu_probe(struct usb_interface *intf,
			   const struct usb_device_id *id)
{
	return rtw_usb_probe(intf, id);
}

static struct usb_driver rtw_8822cu_driver = {
	.name = "rtw_8822cu",
	.id_table = rtw_8822cu_id_table,
	.probe = rtw8822cu_probe,
	.disconnect = rtw_usb_disconnect,
};
module_usb_driver(rtw_8822cu_driver);

MODULE_AUTHOR("Realtek Corporation");
MODULE_DESCRIPTION("Realtek 802.11ac wireless 8822cu driver");
MODULE_LICENSE("Dual BSD/GPL");
