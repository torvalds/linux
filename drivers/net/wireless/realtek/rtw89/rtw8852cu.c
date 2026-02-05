// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2025  Realtek Corporation
 */

#include <linux/module.h>
#include <linux/usb.h>
#include "rtw8852c.h"
#include "reg.h"
#include "usb.h"

static const struct rtw89_usb_info rtw8852c_usb_info = {
	.usb_host_request_2		= R_AX_USB_HOST_REQUEST_2_V1,
	.usb_wlan0_1			= R_AX_USB_WLAN0_1_V1,
	.hci_func_en			= R_AX_HCI_FUNC_EN_V1,
	.usb3_mac_npi_config_intf_0	= R_AX_USB3_MAC_NPI_CONFIG_INTF_0_V1,
	.usb_endpoint_0			= R_AX_USB_ENDPOINT_0_V1,
	.usb_endpoint_2			= R_AX_USB_ENDPOINT_2_V1,
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

static const struct rtw89_driver_info rtw89_8852cu_info = {
	.chip = &rtw8852c_chip_info,
	.variant = NULL,
	.quirks = NULL,
	.bus = {
		.usb = &rtw8852c_usb_info,
	},
};

static const struct usb_device_id rtw_8852cu_id_table[] = {
	{ USB_DEVICE_AND_INTERFACE_INFO(0x0bda, 0xc832, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&rtw89_8852cu_info },
	{ USB_DEVICE_AND_INTERFACE_INFO(0x0bda, 0xc85a, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&rtw89_8852cu_info },
	{ USB_DEVICE_AND_INTERFACE_INFO(0x0bda, 0xc85d, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&rtw89_8852cu_info },
	{ USB_DEVICE_AND_INTERFACE_INFO(0x0db0, 0x991d, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&rtw89_8852cu_info },
	{ USB_DEVICE_AND_INTERFACE_INFO(0x28de, 0x2432, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&rtw89_8852cu_info },
	{ USB_DEVICE_AND_INTERFACE_INFO(0x35b2, 0x0502, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&rtw89_8852cu_info },
	{ USB_DEVICE_AND_INTERFACE_INFO(0x35bc, 0x0101, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&rtw89_8852cu_info },
	{ USB_DEVICE_AND_INTERFACE_INFO(0x35bc, 0x0102, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&rtw89_8852cu_info },
	{},
};
MODULE_DEVICE_TABLE(usb, rtw_8852cu_id_table);

static struct usb_driver rtw_8852cu_driver = {
	.name = KBUILD_MODNAME,
	.id_table = rtw_8852cu_id_table,
	.probe = rtw89_usb_probe,
	.disconnect = rtw89_usb_disconnect,
};
module_usb_driver(rtw_8852cu_driver);

MODULE_AUTHOR("Bitterblue Smith <rtl8821cerfe2@gmail.com>");
MODULE_DESCRIPTION("Realtek 802.11ax wireless 8852CU driver");
MODULE_LICENSE("Dual BSD/GPL");
