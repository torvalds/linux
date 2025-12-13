// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2025  Realtek Corporation
 */

#include <linux/module.h>
#include <linux/usb.h>
#include "rtw8851b.h"
#include "usb.h"

static const struct rtw89_driver_info rtw89_8851bu_info = {
	.chip = &rtw8851b_chip_info,
	.variant = NULL,
	.quirks = NULL,
};

static const struct usb_device_id rtw_8851bu_id_table[] = {
	{ USB_DEVICE_AND_INTERFACE_INFO(0x0bda, 0xb851, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&rtw89_8851bu_info },
	/* D-Link AX9U rev. A1 */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x2001, 0x332a, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&rtw89_8851bu_info },
	/* TP-Link Archer TX10UB Nano */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x3625, 0x010b, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&rtw89_8851bu_info },
	/* Edimax EW-7611UXB */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x7392, 0xe611, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&rtw89_8851bu_info },
	{},
};
MODULE_DEVICE_TABLE(usb, rtw_8851bu_id_table);

static struct usb_driver rtw_8851bu_driver = {
	.name = KBUILD_MODNAME,
	.id_table = rtw_8851bu_id_table,
	.probe = rtw89_usb_probe,
	.disconnect = rtw89_usb_disconnect,
};
module_usb_driver(rtw_8851bu_driver);

MODULE_AUTHOR("Bitterblue Smith <rtl8821cerfe2@gmail.com>");
MODULE_DESCRIPTION("Realtek 802.11ax wireless 8851BU driver");
MODULE_LICENSE("Dual BSD/GPL");
