// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2018-2019  Realtek Corporation
 */

#include <linux/module.h>
#include <linux/usb.h>
#include "main.h"
#include "rtw8822b.h"
#include "usb.h"

static const struct usb_device_id rtw_8822bu_id_table[] = {
	{ USB_DEVICE_AND_INTERFACE_INFO(RTW_USB_VENDOR_ID_REALTEK, 0xb812, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8822b_hw_spec) },
	{ USB_DEVICE_AND_INTERFACE_INFO(RTW_USB_VENDOR_ID_REALTEK, 0xb82c, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8822b_hw_spec) },
	{ USB_DEVICE_AND_INTERFACE_INFO(RTW_USB_VENDOR_ID_REALTEK, 0x2102, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8822b_hw_spec) }, /* CCNC */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x7392, 0xb822, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8822b_hw_spec) }, /* Edimax EW-7822ULC */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x7392, 0xc822, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8822b_hw_spec) }, /* Edimax EW-7822UTC */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x7392, 0xd822, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8822b_hw_spec) }, /* Edimax */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x7392, 0xe822, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8822b_hw_spec) }, /* Edimax */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x7392, 0xf822, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8822b_hw_spec) }, /* Edimax EW-7822UAD */
	{ USB_DEVICE_AND_INTERFACE_INFO(RTW_USB_VENDOR_ID_REALTEK, 0xb81a, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8822b_hw_spec) }, /* Default ID */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x0b05, 0x1841, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8822b_hw_spec) }, /* ASUS AC1300 USB-AC55 B1 */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x0b05, 0x184c, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8822b_hw_spec) }, /* ASUS U2 */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x0B05, 0x19aa, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8822b_hw_spec) }, /* ASUS - USB-AC58 rev A1 */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x0B05, 0x1870, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8822b_hw_spec) }, /* ASUS */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x0B05, 0x1874, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8822b_hw_spec) }, /* ASUS */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x2001, 0x331e, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8822b_hw_spec) }, /* Dlink - DWA-181 */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x2001, 0x331c, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8822b_hw_spec) }, /* Dlink - DWA-182 - D1 */
	{USB_DEVICE_AND_INTERFACE_INFO(0x2001, 0x331f, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8822b_hw_spec)}, /* Dlink - DWA-183 D Ver */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x13b1, 0x0043, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8822b_hw_spec) }, /* Linksys WUSB6400M */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x13b1, 0x0045, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8822b_hw_spec) }, /* Linksys WUSB3600 v2 */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x2357, 0x012d, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8822b_hw_spec) }, /* TP-Link Archer T3U v1 */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x2357, 0x0138, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8822b_hw_spec) }, /* TP-Link Archer T3U Plus v1 */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x2357, 0x0115, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8822b_hw_spec) }, /* TP-Link Archer T4U V3 */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x2357, 0x012e, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8822b_hw_spec) }, /* TP-LINK */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x2357, 0x0116, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8822b_hw_spec) }, /* TP-LINK */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x2357, 0x0117, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8822b_hw_spec) }, /* TP-LINK */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x0846, 0x9055, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8822b_hw_spec) }, /* Netgear A6150 */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x0e66, 0x0025, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8822b_hw_spec) }, /* Hawking HW12ACU */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x04ca, 0x8602, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8822b_hw_spec) }, /* LiteOn */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x20f4, 0x808a, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8822b_hw_spec) }, /* TRENDnet TEW-808UBM */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x20f4, 0x805a, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8822b_hw_spec) }, /* TRENDnet TEW-805UBH */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x056e, 0x4011, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8822b_hw_spec) }, /* ELECOM WDB-867DU3S */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x2c4e, 0x0107, 0xff, 0xff, 0xff),
	  .driver_info = (kernel_ulong_t)&(rtw8822b_hw_spec) }, /* Mercusys MA30H */
	{},
};
MODULE_DEVICE_TABLE(usb, rtw_8822bu_id_table);

static int rtw8822bu_probe(struct usb_interface *intf,
			   const struct usb_device_id *id)
{
	return rtw_usb_probe(intf, id);
}

static struct usb_driver rtw_8822bu_driver = {
	.name = "rtw_8822bu",
	.id_table = rtw_8822bu_id_table,
	.probe = rtw8822bu_probe,
	.disconnect = rtw_usb_disconnect,
};
module_usb_driver(rtw_8822bu_driver);

MODULE_AUTHOR("Realtek Corporation");
MODULE_DESCRIPTION("Realtek 802.11ac wireless 8822bu driver");
MODULE_LICENSE("Dual BSD/GPL");
