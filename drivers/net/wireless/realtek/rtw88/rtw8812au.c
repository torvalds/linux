// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2024  Realtek Corporation
 */

#include <linux/module.h>
#include <linux/usb.h>
#include "main.h"
#include "rtw8812a.h"
#include "usb.h"

static const struct usb_device_id rtw_8812au_id_table[] = {
	{ USB_DEVICE_AND_INTERFACE_INFO(0x2604, 0x0012, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8812a_hw_spec) },
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
