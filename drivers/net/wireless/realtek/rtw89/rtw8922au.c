// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2026  Realtek Corporation
 */

#include <linux/module.h>
#include <linux/usb.h>
#include "rtw8922a.h"
#include "usb.h"

static const struct rtw89_usb_info rtw8922a_usb_info = {
	.usb_host_request_2		= 0,
	.usb_wlan0_1			= 0,
	.hci_func_en			= 0,
	.usb3_mac_npi_config_intf_0	= 0,
	.usb_endpoint_0			= 0,
	.usb_endpoint_2			= 0,
	.rx_agg_alignment		= 16,
	.bulkout_id = {
		[RTW89_DMA_ACH0] = 3,
		[RTW89_DMA_ACH2] = 5,
		[RTW89_DMA_ACH4] = 4,
		[RTW89_DMA_ACH6] = 6,
		[RTW89_DMA_B0MG] = 0,
		[RTW89_DMA_B0HI] = 0,
		[RTW89_DMA_B1MG] = 1,
		[RTW89_DMA_B1HI] = 1,
		[RTW89_DMA_H2C] = 2,
	},
};

static const struct rtw89_driver_info rtw89_8922au_info = {
	.chip = &rtw8922a_chip_info,
	.variant = NULL,
	.quirks = NULL,
	.dev_id_quirks = 0,
	.bus = {
		.usb = &rtw8922a_usb_info,
	},
};

static const struct usb_device_id rtw_8922au_id_table[] = {
	{ USB_DEVICE_AND_INTERFACE_INFO(0x0411, 0x03ef, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&rtw89_8922au_info },
	{ USB_DEVICE_AND_INTERFACE_INFO(0x0502, 0x76d7, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&rtw89_8922au_info },
	{ USB_DEVICE_AND_INTERFACE_INFO(0x056e, 0x4025, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&rtw89_8922au_info },
	{ USB_DEVICE_AND_INTERFACE_INFO(0x056e, 0x4026, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&rtw89_8922au_info },
	{ USB_DEVICE_AND_INTERFACE_INFO(0x0b05, 0x1bcf, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&rtw89_8922au_info },
	{ USB_DEVICE_AND_INTERFACE_INFO(0x0b05, 0x1bd2, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&rtw89_8922au_info },
	{ USB_DEVICE_AND_INTERFACE_INFO(0x0bda, 0x8912, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&rtw89_8922au_info },
	{ USB_DEVICE_AND_INTERFACE_INFO(0x0db0, 0xda0e, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&rtw89_8922au_info },
	{ USB_DEVICE_AND_INTERFACE_INFO(0x2001, 0x332b, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&rtw89_8922au_info },
	{ USB_DEVICE_AND_INTERFACE_INFO(0x3625, 0x010a, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&rtw89_8922au_info },
	{ USB_DEVICE_AND_INTERFACE_INFO(0x37ad, 0x0100, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&rtw89_8922au_info },
	{ USB_DEVICE_AND_INTERFACE_INFO(0x37ad, 0x0101, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&rtw89_8922au_info },
	{ USB_DEVICE_AND_INTERFACE_INFO(0x7392, 0x3822, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&rtw89_8922au_info },
	{ USB_DEVICE_AND_INTERFACE_INFO(0x7392, 0x4822, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&rtw89_8922au_info },
	{ USB_DEVICE_AND_INTERFACE_INFO(0x7392, 0x5822, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&rtw89_8922au_info },
	{},
};
MODULE_DEVICE_TABLE(usb, rtw_8922au_id_table);

static struct usb_driver rtw_8922au_driver = {
	.name = KBUILD_MODNAME,
	.id_table = rtw_8922au_id_table,
	.probe = rtw89_usb_probe,
	.disconnect = rtw89_usb_disconnect,
};
module_usb_driver(rtw_8922au_driver);

MODULE_AUTHOR("Bitterblue Smith <rtl8821cerfe2@gmail.com>");
MODULE_DESCRIPTION("Realtek 802.11be wireless 8922AU driver");
MODULE_LICENSE("Dual BSD/GPL");
