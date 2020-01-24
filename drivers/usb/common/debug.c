// SPDX-License-Identifier: GPL-2.0
/**
 * Common USB debugging functions
 *
 * Copyright (C) 2010-2011 Texas Instruments Incorporated - http://www.ti.com
 *
 * Authors: Felipe Balbi <balbi@ti.com>,
 *	    Sebastian Andrzej Siewior <bigeasy@linutronix.de>
 */

#include <linux/usb/ch9.h>

static void usb_decode_get_status(__u8 bRequestType, __u16 wIndex,
				  __u16 wLength, char *str, size_t size)
{
	switch (bRequestType & USB_RECIP_MASK) {
	case USB_RECIP_DEVICE:
		snprintf(str, size, "Get Device Status(Length = %d)", wLength);
		break;
	case USB_RECIP_INTERFACE:
		snprintf(str, size,
			 "Get Interface Status(Intf = %d, Length = %d)",
			 wIndex, wLength);
		break;
	case USB_RECIP_ENDPOINT:
		snprintf(str, size, "Get Endpoint Status(ep%d%s)",
			 wIndex & ~USB_DIR_IN,
			 wIndex & USB_DIR_IN ? "in" : "out");
		break;
	}
}

static const char *usb_decode_device_feature(u16 wValue)
{
	switch (wValue) {
	case USB_DEVICE_SELF_POWERED:
		return "Self Powered";
	case USB_DEVICE_REMOTE_WAKEUP:
		return "Remote Wakeup";
	case USB_DEVICE_TEST_MODE:
		return "Test Mode";
	case USB_DEVICE_U1_ENABLE:
		return "U1 Enable";
	case USB_DEVICE_U2_ENABLE:
		return "U2 Enable";
	case USB_DEVICE_LTM_ENABLE:
		return "LTM Enable";
	default:
		return "UNKNOWN";
	}
}

static const char *usb_decode_test_mode(u16 wIndex)
{
	switch (wIndex) {
	case TEST_J:
		return ": TEST_J";
	case TEST_K:
		return ": TEST_K";
	case TEST_SE0_NAK:
		return ": TEST_SE0_NAK";
	case TEST_PACKET:
		return ": TEST_PACKET";
	case TEST_FORCE_EN:
		return ": TEST_FORCE_EN";
	default:
		return ": UNKNOWN";
	}
}

static void usb_decode_set_clear_feature(__u8 bRequestType,
					 __u8 bRequest, __u16 wValue,
					 __u16 wIndex, char *str, size_t size)
{
	switch (bRequestType & USB_RECIP_MASK) {
	case USB_RECIP_DEVICE:
		snprintf(str, size, "%s Device Feature(%s%s)",
			 bRequest == USB_REQ_CLEAR_FEATURE ? "Clear" : "Set",
			 usb_decode_device_feature(wValue),
			 wValue == USB_DEVICE_TEST_MODE ?
			 usb_decode_test_mode(wIndex) : "");
		break;
	case USB_RECIP_INTERFACE:
		snprintf(str, size, "%s Interface Feature(%s)",
			 bRequest == USB_REQ_CLEAR_FEATURE ? "Clear" : "Set",
			 wValue == USB_INTRF_FUNC_SUSPEND ?
			 "Function Suspend" : "UNKNOWN");
		break;
	case USB_RECIP_ENDPOINT:
		snprintf(str, size, "%s Endpoint Feature(%s ep%d%s)",
			 bRequest == USB_REQ_CLEAR_FEATURE ? "Clear" : "Set",
			 wValue == USB_ENDPOINT_HALT ? "Halt" : "UNKNOWN",
			 wIndex & ~USB_DIR_IN,
			 wIndex & USB_DIR_IN ? "in" : "out");
		break;
	}
}

static void usb_decode_set_address(__u16 wValue, char *str, size_t size)
{
	snprintf(str, size, "Set Address(Addr = %02x)", wValue);
}

static void usb_decode_get_set_descriptor(__u8 bRequestType, __u8 bRequest,
					  __u16 wValue, __u16 wIndex,
					  __u16 wLength, char *str, size_t size)
{
	char *s;

	switch (wValue >> 8) {
	case USB_DT_DEVICE:
		s = "Device";
		break;
	case USB_DT_CONFIG:
		s = "Configuration";
		break;
	case USB_DT_STRING:
		s = "String";
		break;
	case USB_DT_INTERFACE:
		s = "Interface";
		break;
	case USB_DT_ENDPOINT:
		s = "Endpoint";
		break;
	case USB_DT_DEVICE_QUALIFIER:
		s = "Device Qualifier";
		break;
	case USB_DT_OTHER_SPEED_CONFIG:
		s = "Other Speed Config";
		break;
	case USB_DT_INTERFACE_POWER:
		s = "Interface Power";
		break;
	case USB_DT_OTG:
		s = "OTG";
		break;
	case USB_DT_DEBUG:
		s = "Debug";
		break;
	case USB_DT_INTERFACE_ASSOCIATION:
		s = "Interface Association";
		break;
	case USB_DT_BOS:
		s = "BOS";
		break;
	case USB_DT_DEVICE_CAPABILITY:
		s = "Device Capability";
		break;
	case USB_DT_PIPE_USAGE:
		s = "Pipe Usage";
		break;
	case USB_DT_SS_ENDPOINT_COMP:
		s = "SS Endpoint Companion";
		break;
	case USB_DT_SSP_ISOC_ENDPOINT_COMP:
		s = "SSP Isochronous Endpoint Companion";
		break;
	default:
		s = "UNKNOWN";
		break;
	}

	snprintf(str, size, "%s %s Descriptor(Index = %d, Length = %d)",
		bRequest == USB_REQ_GET_DESCRIPTOR ? "Get" : "Set",
		s, wValue & 0xff, wLength);
}

static void usb_decode_get_configuration(__u16 wLength, char *str, size_t size)
{
	snprintf(str, size, "Get Configuration(Length = %d)", wLength);
}

static void usb_decode_set_configuration(__u8 wValue, char *str, size_t size)
{
	snprintf(str, size, "Set Configuration(Config = %d)", wValue);
}

static void usb_decode_get_intf(__u16 wIndex, __u16 wLength, char *str,
				size_t size)
{
	snprintf(str, size, "Get Interface(Intf = %d, Length = %d)",
		 wIndex, wLength);
}

static void usb_decode_set_intf(__u8 wValue, __u16 wIndex, char *str,
				size_t size)
{
	snprintf(str, size, "Set Interface(Intf = %d, Alt.Setting = %d)",
		 wIndex, wValue);
}

static void usb_decode_synch_frame(__u16 wIndex, __u16 wLength,
				   char *str, size_t size)
{
	snprintf(str, size, "Synch Frame(Endpoint = %d, Length = %d)",
		 wIndex, wLength);
}

static void usb_decode_set_sel(__u16 wLength, char *str, size_t size)
{
	snprintf(str, size, "Set SEL(Length = %d)", wLength);
}

static void usb_decode_set_isoch_delay(__u8 wValue, char *str, size_t size)
{
	snprintf(str, size, "Set Isochronous Delay(Delay = %d ns)", wValue);
}

/**
 * usb_decode_ctrl - returns a string representation of ctrl request
 */
const char *usb_decode_ctrl(char *str, size_t size, __u8 bRequestType,
			    __u8 bRequest, __u16 wValue, __u16 wIndex,
			    __u16 wLength)
{
	switch (bRequest) {
	case USB_REQ_GET_STATUS:
		usb_decode_get_status(bRequestType, wIndex, wLength, str, size);
		break;
	case USB_REQ_CLEAR_FEATURE:
	case USB_REQ_SET_FEATURE:
		usb_decode_set_clear_feature(bRequestType, bRequest, wValue,
					     wIndex, str, size);
		break;
	case USB_REQ_SET_ADDRESS:
		usb_decode_set_address(wValue, str, size);
		break;
	case USB_REQ_GET_DESCRIPTOR:
	case USB_REQ_SET_DESCRIPTOR:
		usb_decode_get_set_descriptor(bRequestType, bRequest, wValue,
					      wIndex, wLength, str, size);
		break;
	case USB_REQ_GET_CONFIGURATION:
		usb_decode_get_configuration(wLength, str, size);
		break;
	case USB_REQ_SET_CONFIGURATION:
		usb_decode_set_configuration(wValue, str, size);
		break;
	case USB_REQ_GET_INTERFACE:
		usb_decode_get_intf(wIndex, wLength, str, size);
		break;
	case USB_REQ_SET_INTERFACE:
		usb_decode_set_intf(wValue, wIndex, str, size);
		break;
	case USB_REQ_SYNCH_FRAME:
		usb_decode_synch_frame(wIndex, wLength, str, size);
		break;
	case USB_REQ_SET_SEL:
		usb_decode_set_sel(wLength, str, size);
		break;
	case USB_REQ_SET_ISOCH_DELAY:
		usb_decode_set_isoch_delay(wValue, str, size);
		break;
	default:
		snprintf(str, size, "%02x %02x %02x %02x %02x %02x %02x %02x",
			 bRequestType, bRequest,
			 (u8)(cpu_to_le16(wValue) & 0xff),
			 (u8)(cpu_to_le16(wValue) >> 8),
			 (u8)(cpu_to_le16(wIndex) & 0xff),
			 (u8)(cpu_to_le16(wIndex) >> 8),
			 (u8)(cpu_to_le16(wLength) & 0xff),
			 (u8)(cpu_to_le16(wLength) >> 8));
	}

	return str;
}
EXPORT_SYMBOL_GPL(usb_decode_ctrl);
