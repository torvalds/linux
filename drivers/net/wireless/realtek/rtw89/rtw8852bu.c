// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2025  Realtek Corporation
 */

#include <linux/module.h>
#include <linux/usb.h>
#include "rtw8852b.h"
#include "reg.h"
#include "usb.h"

static const struct rtw89_usb_info rtw8852b_usb_info = {
	.usb_host_request_2		= R_AX_USB_HOST_REQUEST_2,
	.usb_wlan0_1			= R_AX_USB_WLAN0_1,
	.hci_func_en			= R_AX_HCI_FUNC_EN,
	.usb3_mac_npi_config_intf_0	= R_AX_USB3_MAC_NPI_CONFIG_INTF_0,
	.usb_endpoint_0			= R_AX_USB_ENDPOINT_0,
	.usb_endpoint_2			= R_AX_USB_ENDPOINT_2,
	.bulkout_id = {
		[RTW89_DMA_ACH0] = 3,
		[RTW89_DMA_ACH1] = 4,
		[RTW89_DMA_ACH2] = 5,
		[RTW89_DMA_ACH3] = 6,
		[RTW89_DMA_B0MG] = 0,
		[RTW89_DMA_B0HI] = 1,
		[RTW89_DMA_H2C] = 2,
	},
};

static const struct rtw89_driver_info rtw89_8852bu_info = {
	.chip = &rtw8852b_chip_info,
	.variant = NULL,
	.quirks = NULL,
	.bus = {
		.usb = &rtw8852b_usb_info,
	}
};

static const struct usb_device_id rtw_8852bu_id_table[] = {
	{ USB_DEVICE_AND_INTERFACE_INFO(0x0bda, 0xb832, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&rtw89_8852bu_info },
	{ USB_DEVICE_AND_INTERFACE_INFO(0x0bda, 0xb83a, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&rtw89_8852bu_info },
	{ USB_DEVICE_AND_INTERFACE_INFO(0x0bda, 0xb852, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&rtw89_8852bu_info },
	{ USB_DEVICE_AND_INTERFACE_INFO(0x0bda, 0xb85a, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&rtw89_8852bu_info },
	{ USB_DEVICE_AND_INTERFACE_INFO(0x0bda, 0xa85b, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&rtw89_8852bu_info },
	{ USB_DEVICE_AND_INTERFACE_INFO(0x0586, 0x3428, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&rtw89_8852bu_info },
	{ USB_DEVICE_AND_INTERFACE_INFO(0x0b05, 0x1a62, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&rtw89_8852bu_info },
	{ USB_DEVICE_AND_INTERFACE_INFO(0x0b05, 0x1cb6, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&rtw89_8852bu_info },
	{ USB_DEVICE_AND_INTERFACE_INFO(0x0db0, 0x6931, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&rtw89_8852bu_info },
	{ USB_DEVICE_AND_INTERFACE_INFO(0x0db0, 0xf0c8, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&rtw89_8852bu_info },
	{ USB_DEVICE_AND_INTERFACE_INFO(0x2001, 0x3327, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&rtw89_8852bu_info },
	{ USB_DEVICE_AND_INTERFACE_INFO(0x3574, 0x6121, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&rtw89_8852bu_info },
	{ USB_DEVICE_AND_INTERFACE_INFO(0x35bc, 0x0100, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&rtw89_8852bu_info },
	{ USB_DEVICE_AND_INTERFACE_INFO(0x35bc, 0x0108, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&rtw89_8852bu_info },
	{ USB_DEVICE_AND_INTERFACE_INFO(0x7392, 0x6822, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&rtw89_8852bu_info },
	{},
};
MODULE_DEVICE_TABLE(usb, rtw_8852bu_id_table);

static struct usb_driver rtw_8852bu_driver = {
	.name = KBUILD_MODNAME,
	.id_table = rtw_8852bu_id_table,
	.probe = rtw89_usb_probe,
	.disconnect = rtw89_usb_disconnect,
};
module_usb_driver(rtw_8852bu_driver);

MODULE_AUTHOR("Bitterblue Smith <rtl8821cerfe2@gmail.com>");
MODULE_DESCRIPTION("Realtek 802.11ax wireless 8852BU driver");
MODULE_LICENSE("Dual BSD/GPL");
