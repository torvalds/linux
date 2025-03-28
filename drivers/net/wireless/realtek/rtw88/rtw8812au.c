// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2024  Realtek Corporation
 */

#include <linux/module.h>
#include <linux/usb.h>
#include "main.h"
#include "rtw8812a.h"
#include "usb.h"

static const struct usb_device_id rtw_8812au_id_table[] = {
	{ USB_DEVICE_AND_INTERFACE_INFO(RTW_USB_VENDOR_ID_REALTEK, 0x8812, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8812a_hw_spec) },
	{ USB_DEVICE_AND_INTERFACE_INFO(RTW_USB_VENDOR_ID_REALTEK, 0x881a, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8812a_hw_spec) },
	{ USB_DEVICE_AND_INTERFACE_INFO(RTW_USB_VENDOR_ID_REALTEK, 0x881b, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8812a_hw_spec) },
	{ USB_DEVICE_AND_INTERFACE_INFO(RTW_USB_VENDOR_ID_REALTEK, 0x881c, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8812a_hw_spec) },
	{ USB_DEVICE_AND_INTERFACE_INFO(0x0409, 0x0408, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8812a_hw_spec) }, /* NEC */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x0411, 0x025d, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8812a_hw_spec) }, /* Buffalo */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x04bb, 0x0952, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8812a_hw_spec) }, /* I-O DATA */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x050d, 0x1106, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8812a_hw_spec) }, /* Belkin */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x050d, 0x1109, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8812a_hw_spec) }, /* Belkin */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x0586, 0x3426, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8812a_hw_spec) }, /* ZyXEL */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x0789, 0x016e, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8812a_hw_spec) }, /* Logitec */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x07b8, 0x8812, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8812a_hw_spec) }, /* Abocom */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x0846, 0x9051, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8812a_hw_spec) }, /* Netgear */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x0b05, 0x17d2, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8812a_hw_spec) }, /* ASUS */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x0df6, 0x0074, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8812a_hw_spec) }, /* Sitecom */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x0e66, 0x0022, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8812a_hw_spec) }, /* Hawking */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x1058, 0x0632, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8812a_hw_spec) }, /* WD */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x13b1, 0x003f, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8812a_hw_spec) }, /* Linksys */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x148f, 0x9097, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8812a_hw_spec) }, /* Amped Wireless */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x1740, 0x0100, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8812a_hw_spec) }, /* EnGenius */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x2001, 0x330e, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8812a_hw_spec) }, /* D-Link */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x2001, 0x3313, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8812a_hw_spec) }, /* D-Link */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x2001, 0x3315, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8812a_hw_spec) }, /* D-Link */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x2001, 0x3316, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8812a_hw_spec) }, /* D-Link */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x2019, 0xab30, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8812a_hw_spec) }, /* Planex */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x20f4, 0x805b, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8812a_hw_spec) }, /* TRENDnet */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x2357, 0x0101, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8812a_hw_spec) }, /* TP-Link */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x2357, 0x0103, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8812a_hw_spec) }, /* TP-Link */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x2357, 0x010d, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8812a_hw_spec) }, /* TP-Link */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x2357, 0x010e, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8812a_hw_spec) }, /* TP-Link */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x2357, 0x010f, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8812a_hw_spec) }, /* TP-Link */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x2357, 0x0122, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8812a_hw_spec) }, /* TP-Link */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x2604, 0x0012, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8812a_hw_spec) }, /* Tenda */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x7392, 0xa822, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8812a_hw_spec) }, /* Edimax */
	{},
};
MODULE_DEVICE_TABLE(usb, rtw_8812au_id_table);

static struct usb_driver rtw_8812au_driver = {
	.name = "rtw_8812au",
	.id_table = rtw_8812au_id_table,
	.probe = rtw_usb_probe,
	.disconnect = rtw_usb_disconnect,
};
module_usb_driver(rtw_8812au_driver);

MODULE_AUTHOR("Bitterblue Smith <rtl8821cerfe2@gmail.com>");
MODULE_DESCRIPTION("Realtek 802.11ac wireless 8812au driver");
MODULE_LICENSE("Dual BSD/GPL");
