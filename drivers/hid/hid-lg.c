/*
 *  HID driver for some logitech "special" devices
 *
 *  Copyright (c) 1999 Andreas Gal
 *  Copyright (c) 2000-2005 Vojtech Pavlik <vojtech@suse.cz>
 *  Copyright (c) 2005 Michael Haboustak <mike-@cinci.rr.com> for Concept2, Inc
 *  Copyright (c) 2006-2007 Jiri Kosina
 *  Copyright (c) 2008 Jiri Slaby
 *  Copyright (c) 2010 Hendrik Iben
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <linux/device.h>
#include <linux/hid.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/sched.h>
#include <linux/usb.h>
#include <linux/wait.h>

#include "usbhid/usbhid.h"
#include "hid-ids.h"
#include "hid-lg.h"
#include "hid-lg4ff.h"

#define LG_RDESC		0x001
#define LG_BAD_RELATIVE_KEYS	0x002
#define LG_DUPLICATE_USAGES	0x004
#define LG_EXPANDED_KEYMAP	0x010
#define LG_IGNORE_DOUBLED_WHEEL	0x020
#define LG_WIRELESS		0x040
#define LG_INVERT_HWHEEL	0x080
#define LG_NOGET		0x100
#define LG_FF			0x200
#define LG_FF2			0x400
#define LG_RDESC_REL_ABS	0x800
#define LG_FF3			0x1000
#define LG_FF4			0x2000

/* Size of the original descriptors of the Driving Force (and Pro) wheels */
#define DF_RDESC_ORIG_SIZE	130
#define DFP_RDESC_ORIG_SIZE	97
#define FV_RDESC_ORIG_SIZE	130
#define MOMO_RDESC_ORIG_SIZE	87
#define MOMO2_RDESC_ORIG_SIZE	87

/* Fixed report descriptors for Logitech Driving Force (and Pro)
 * wheel controllers
 *
 * The original descriptors hide the separate throttle and brake axes in
 * a custom vendor usage page, providing only a combined value as
 * GenericDesktop.Y.
 * These descriptors remove the combined Y axis and instead report
 * separate throttle (Y) and brake (RZ).
 */
static __u8 df_rdesc_fixed[] = {
0x05, 0x01,         /*  Usage Page (Desktop),                   */
0x09, 0x04,         /*  Usage (Joystik),                        */
0xA1, 0x01,         /*  Collection (Application),               */
0xA1, 0x02,         /*      Collection (Logical),               */
0x95, 0x01,         /*          Report Count (1),               */
0x75, 0x0A,         /*          Report Size (10),               */
0x14,               /*          Logical Minimum (0),            */
0x26, 0xFF, 0x03,   /*          Logical Maximum (1023),         */
0x34,               /*          Physical Minimum (0),           */
0x46, 0xFF, 0x03,   /*          Physical Maximum (1023),        */
0x09, 0x30,         /*          Usage (X),                      */
0x81, 0x02,         /*          Input (Variable),               */
0x95, 0x0C,         /*          Report Count (12),              */
0x75, 0x01,         /*          Report Size (1),                */
0x25, 0x01,         /*          Logical Maximum (1),            */
0x45, 0x01,         /*          Physical Maximum (1),           */
0x05, 0x09,         /*          Usage (Buttons),                */
0x19, 0x01,         /*          Usage Minimum (1),              */
0x29, 0x0c,         /*          Usage Maximum (12),             */
0x81, 0x02,         /*          Input (Variable),               */
0x95, 0x02,         /*          Report Count (2),               */
0x06, 0x00, 0xFF,   /*          Usage Page (Vendor: 65280),     */
0x09, 0x01,         /*          Usage (?: 1),                   */
0x81, 0x02,         /*          Input (Variable),               */
0x05, 0x01,         /*          Usage Page (Desktop),           */
0x26, 0xFF, 0x00,   /*          Logical Maximum (255),          */
0x46, 0xFF, 0x00,   /*          Physical Maximum (255),         */
0x95, 0x01,         /*          Report Count (1),               */
0x75, 0x08,         /*          Report Size (8),                */
0x81, 0x02,         /*          Input (Variable),               */
0x25, 0x07,         /*          Logical Maximum (7),            */
0x46, 0x3B, 0x01,   /*          Physical Maximum (315),         */
0x75, 0x04,         /*          Report Size (4),                */
0x65, 0x14,         /*          Unit (Degrees),                 */
0x09, 0x39,         /*          Usage (Hat Switch),             */
0x81, 0x42,         /*          Input (Variable, Null State),   */
0x75, 0x01,         /*          Report Size (1),                */
0x95, 0x04,         /*          Report Count (4),               */
0x65, 0x00,         /*          Unit (none),                    */
0x06, 0x00, 0xFF,   /*          Usage Page (Vendor: 65280),     */
0x09, 0x01,         /*          Usage (?: 1),                   */
0x25, 0x01,         /*          Logical Maximum (1),            */
0x45, 0x01,         /*          Physical Maximum (1),           */
0x81, 0x02,         /*          Input (Variable),               */
0x05, 0x01,         /*          Usage Page (Desktop),           */
0x95, 0x01,         /*          Report Count (1),               */
0x75, 0x08,         /*          Report Size (8),                */
0x26, 0xFF, 0x00,   /*          Logical Maximum (255),          */
0x46, 0xFF, 0x00,   /*          Physical Maximum (255),         */
0x09, 0x31,         /*          Usage (Y),                      */
0x81, 0x02,         /*          Input (Variable),               */
0x09, 0x35,         /*          Usage (Rz),                     */
0x81, 0x02,         /*          Input (Variable),               */
0xC0,               /*      End Collection,                     */
0xA1, 0x02,         /*      Collection (Logical),               */
0x26, 0xFF, 0x00,   /*          Logical Maximum (255),          */
0x46, 0xFF, 0x00,   /*          Physical Maximum (255),         */
0x95, 0x07,         /*          Report Count (7),               */
0x75, 0x08,         /*          Report Size (8),                */
0x09, 0x03,         /*          Usage (?: 3),                   */
0x91, 0x02,         /*          Output (Variable),              */
0xC0,               /*      End Collection,                     */
0xC0                /*  End Collection                          */
};

static __u8 dfp_rdesc_fixed[] = {
0x05, 0x01,         /*  Usage Page (Desktop),                   */
0x09, 0x04,         /*  Usage (Joystik),                        */
0xA1, 0x01,         /*  Collection (Application),               */
0xA1, 0x02,         /*      Collection (Logical),               */
0x95, 0x01,         /*          Report Count (1),               */
0x75, 0x0E,         /*          Report Size (14),               */
0x14,               /*          Logical Minimum (0),            */
0x26, 0xFF, 0x3F,   /*          Logical Maximum (16383),        */
0x34,               /*          Physical Minimum (0),           */
0x46, 0xFF, 0x3F,   /*          Physical Maximum (16383),       */
0x09, 0x30,         /*          Usage (X),                      */
0x81, 0x02,         /*          Input (Variable),               */
0x95, 0x0E,         /*          Report Count (14),              */
0x75, 0x01,         /*          Report Size (1),                */
0x25, 0x01,         /*          Logical Maximum (1),            */
0x45, 0x01,         /*          Physical Maximum (1),           */
0x05, 0x09,         /*          Usage Page (Button),            */
0x19, 0x01,         /*          Usage Minimum (01h),            */
0x29, 0x0E,         /*          Usage Maximum (0Eh),            */
0x81, 0x02,         /*          Input (Variable),               */
0x05, 0x01,         /*          Usage Page (Desktop),           */
0x95, 0x01,         /*          Report Count (1),               */
0x75, 0x04,         /*          Report Size (4),                */
0x25, 0x07,         /*          Logical Maximum (7),            */
0x46, 0x3B, 0x01,   /*          Physical Maximum (315),         */
0x65, 0x14,         /*          Unit (Degrees),                 */
0x09, 0x39,         /*          Usage (Hat Switch),             */
0x81, 0x42,         /*          Input (Variable, Nullstate),    */
0x65, 0x00,         /*          Unit,                           */
0x26, 0xFF, 0x00,   /*          Logical Maximum (255),          */
0x46, 0xFF, 0x00,   /*          Physical Maximum (255),         */
0x75, 0x08,         /*          Report Size (8),                */
0x81, 0x01,         /*          Input (Constant),               */
0x09, 0x31,         /*          Usage (Y),                      */
0x81, 0x02,         /*          Input (Variable),               */
0x09, 0x35,         /*          Usage (Rz),                     */
0x81, 0x02,         /*          Input (Variable),               */
0x81, 0x01,         /*          Input (Constant),               */
0xC0,               /*      End Collection,                     */
0xA1, 0x02,         /*      Collection (Logical),               */
0x09, 0x02,         /*          Usage (02h),                    */
0x95, 0x07,         /*          Report Count (7),               */
0x91, 0x02,         /*          Output (Variable),              */
0xC0,               /*      End Collection,                     */
0xC0                /*  End Collection                          */
};

static __u8 fv_rdesc_fixed[] = {
0x05, 0x01,         /*  Usage Page (Desktop),                   */
0x09, 0x04,         /*  Usage (Joystik),                        */
0xA1, 0x01,         /*  Collection (Application),               */
0xA1, 0x02,         /*      Collection (Logical),               */
0x95, 0x01,         /*          Report Count (1),               */
0x75, 0x0A,         /*          Report Size (10),               */
0x15, 0x00,         /*          Logical Minimum (0),            */
0x26, 0xFF, 0x03,   /*          Logical Maximum (1023),         */
0x35, 0x00,         /*          Physical Minimum (0),           */
0x46, 0xFF, 0x03,   /*          Physical Maximum (1023),        */
0x09, 0x30,         /*          Usage (X),                      */
0x81, 0x02,         /*          Input (Variable),               */
0x95, 0x0C,         /*          Report Count (12),              */
0x75, 0x01,         /*          Report Size (1),                */
0x25, 0x01,         /*          Logical Maximum (1),            */
0x45, 0x01,         /*          Physical Maximum (1),           */
0x05, 0x09,         /*          Usage Page (Button),            */
0x19, 0x01,         /*          Usage Minimum (01h),            */
0x29, 0x0C,         /*          Usage Maximum (0Ch),            */
0x81, 0x02,         /*          Input (Variable),               */
0x95, 0x02,         /*          Report Count (2),               */
0x06, 0x00, 0xFF,   /*          Usage Page (FF00h),             */
0x09, 0x01,         /*          Usage (01h),                    */
0x81, 0x02,         /*          Input (Variable),               */
0x09, 0x02,         /*          Usage (02h),                    */
0x26, 0xFF, 0x00,   /*          Logical Maximum (255),          */
0x46, 0xFF, 0x00,   /*          Physical Maximum (255),         */
0x95, 0x01,         /*          Report Count (1),               */
0x75, 0x08,         /*          Report Size (8),                */
0x81, 0x02,         /*          Input (Variable),               */
0x05, 0x01,         /*          Usage Page (Desktop),           */
0x25, 0x07,         /*          Logical Maximum (7),            */
0x46, 0x3B, 0x01,   /*          Physical Maximum (315),         */
0x75, 0x04,         /*          Report Size (4),                */
0x65, 0x14,         /*          Unit (Degrees),                 */
0x09, 0x39,         /*          Usage (Hat Switch),             */
0x81, 0x42,         /*          Input (Variable, Null State),   */
0x75, 0x01,         /*          Report Size (1),                */
0x95, 0x04,         /*          Report Count (4),               */
0x65, 0x00,         /*          Unit,                           */
0x06, 0x00, 0xFF,   /*          Usage Page (FF00h),             */
0x09, 0x01,         /*          Usage (01h),                    */
0x25, 0x01,         /*          Logical Maximum (1),            */
0x45, 0x01,         /*          Physical Maximum (1),           */
0x81, 0x02,         /*          Input (Variable),               */
0x05, 0x01,         /*          Usage Page (Desktop),           */
0x95, 0x01,         /*          Report Count (1),               */
0x75, 0x08,         /*          Report Size (8),                */
0x26, 0xFF, 0x00,   /*          Logical Maximum (255),          */
0x46, 0xFF, 0x00,   /*          Physical Maximum (255),         */
0x09, 0x31,         /*          Usage (Y),                      */
0x81, 0x02,         /*          Input (Variable),               */
0x09, 0x32,         /*          Usage (Z),                      */
0x81, 0x02,         /*          Input (Variable),               */
0xC0,               /*      End Collection,                     */
0xA1, 0x02,         /*      Collection (Logical),               */
0x26, 0xFF, 0x00,   /*          Logical Maximum (255),          */
0x46, 0xFF, 0x00,   /*          Physical Maximum (255),         */
0x95, 0x07,         /*          Report Count (7),               */
0x75, 0x08,         /*          Report Size (8),                */
0x09, 0x03,         /*          Usage (03h),                    */
0x91, 0x02,         /*          Output (Variable),              */
0xC0,               /*      End Collection,                     */
0xC0                /*  End Collection                          */
};

static __u8 momo_rdesc_fixed[] = {
0x05, 0x01,         /*  Usage Page (Desktop),               */
0x09, 0x04,         /*  Usage (Joystik),                    */
0xA1, 0x01,         /*  Collection (Application),           */
0xA1, 0x02,         /*      Collection (Logical),           */
0x95, 0x01,         /*          Report Count (1),           */
0x75, 0x0A,         /*          Report Size (10),           */
0x15, 0x00,         /*          Logical Minimum (0),        */
0x26, 0xFF, 0x03,   /*          Logical Maximum (1023),     */
0x35, 0x00,         /*          Physical Minimum (0),       */
0x46, 0xFF, 0x03,   /*          Physical Maximum (1023),    */
0x09, 0x30,         /*          Usage (X),                  */
0x81, 0x02,         /*          Input (Variable),           */
0x95, 0x08,         /*          Report Count (8),           */
0x75, 0x01,         /*          Report Size (1),            */
0x25, 0x01,         /*          Logical Maximum (1),        */
0x45, 0x01,         /*          Physical Maximum (1),       */
0x05, 0x09,         /*          Usage Page (Button),        */
0x19, 0x01,         /*          Usage Minimum (01h),        */
0x29, 0x08,         /*          Usage Maximum (08h),        */
0x81, 0x02,         /*          Input (Variable),           */
0x06, 0x00, 0xFF,   /*          Usage Page (FF00h),         */
0x75, 0x0E,         /*          Report Size (14),           */
0x95, 0x01,         /*          Report Count (1),           */
0x26, 0xFF, 0x00,   /*          Logical Maximum (255),      */
0x46, 0xFF, 0x00,   /*          Physical Maximum (255),     */
0x09, 0x00,         /*          Usage (00h),                */
0x81, 0x02,         /*          Input (Variable),           */
0x05, 0x01,         /*          Usage Page (Desktop),       */
0x75, 0x08,         /*          Report Size (8),            */
0x09, 0x31,         /*          Usage (Y),                  */
0x81, 0x02,         /*          Input (Variable),           */
0x09, 0x32,         /*          Usage (Z),                  */
0x81, 0x02,         /*          Input (Variable),           */
0x06, 0x00, 0xFF,   /*          Usage Page (FF00h),         */
0x09, 0x01,         /*          Usage (01h),                */
0x81, 0x02,         /*          Input (Variable),           */
0xC0,               /*      End Collection,                 */
0xA1, 0x02,         /*      Collection (Logical),           */
0x09, 0x02,         /*          Usage (02h),                */
0x95, 0x07,         /*          Report Count (7),           */
0x91, 0x02,         /*          Output (Variable),          */
0xC0,               /*      End Collection,                 */
0xC0                /*  End Collection                      */
};

static __u8 momo2_rdesc_fixed[] = {
0x05, 0x01,         /*  Usage Page (Desktop),               */
0x09, 0x04,         /*  Usage (Joystik),                    */
0xA1, 0x01,         /*  Collection (Application),           */
0xA1, 0x02,         /*      Collection (Logical),           */
0x95, 0x01,         /*          Report Count (1),           */
0x75, 0x0A,         /*          Report Size (10),           */
0x15, 0x00,         /*          Logical Minimum (0),        */
0x26, 0xFF, 0x03,   /*          Logical Maximum (1023),     */
0x35, 0x00,         /*          Physical Minimum (0),       */
0x46, 0xFF, 0x03,   /*          Physical Maximum (1023),    */
0x09, 0x30,         /*          Usage (X),                  */
0x81, 0x02,         /*          Input (Variable),           */
0x95, 0x0A,         /*          Report Count (10),          */
0x75, 0x01,         /*          Report Size (1),            */
0x25, 0x01,         /*          Logical Maximum (1),        */
0x45, 0x01,         /*          Physical Maximum (1),       */
0x05, 0x09,         /*          Usage Page (Button),        */
0x19, 0x01,         /*          Usage Minimum (01h),        */
0x29, 0x0A,         /*          Usage Maximum (0Ah),        */
0x81, 0x02,         /*          Input (Variable),           */
0x06, 0x00, 0xFF,   /*          Usage Page (FF00h),         */
0x09, 0x00,         /*          Usage (00h),                */
0x95, 0x04,         /*          Report Count (4),           */
0x81, 0x02,         /*          Input (Variable),           */
0x95, 0x01,         /*          Report Count (1),           */
0x75, 0x08,         /*          Report Size (8),            */
0x26, 0xFF, 0x00,   /*          Logical Maximum (255),      */
0x46, 0xFF, 0x00,   /*          Physical Maximum (255),     */
0x09, 0x01,         /*          Usage (01h),                */
0x81, 0x02,         /*          Input (Variable),           */
0x05, 0x01,         /*          Usage Page (Desktop),       */
0x09, 0x31,         /*          Usage (Y),                  */
0x81, 0x02,         /*          Input (Variable),           */
0x09, 0x32,         /*          Usage (Z),                  */
0x81, 0x02,         /*          Input (Variable),           */
0x06, 0x00, 0xFF,   /*          Usage Page (FF00h),         */
0x09, 0x00,         /*          Usage (00h),                */
0x81, 0x02,         /*          Input (Variable),           */
0xC0,               /*      End Collection,                 */
0xA1, 0x02,         /*      Collection (Logical),           */
0x09, 0x02,         /*          Usage (02h),                */
0x95, 0x07,         /*          Report Count (7),           */
0x91, 0x02,         /*          Output (Variable),          */
0xC0,               /*      End Collection,                 */
0xC0                /*  End Collection                      */
};

/*
 * Certain Logitech keyboards send in report #3 keys which are far
 * above the logical maximum described in descriptor. This extends
 * the original value of 0x28c of logical maximum to 0x104d
 */
static __u8 *lg_report_fixup(struct hid_device *hdev, __u8 *rdesc,
		unsigned int *rsize)
{
	struct lg_drv_data *drv_data = hid_get_drvdata(hdev);
	struct usb_device_descriptor *udesc;
	__u16 bcdDevice, rev_maj, rev_min;

	if ((drv_data->quirks & LG_RDESC) && *rsize >= 91 && rdesc[83] == 0x26 &&
			rdesc[84] == 0x8c && rdesc[85] == 0x02) {
		hid_info(hdev,
			 "fixing up Logitech keyboard report descriptor\n");
		rdesc[84] = rdesc[89] = 0x4d;
		rdesc[85] = rdesc[90] = 0x10;
	}
	if ((drv_data->quirks & LG_RDESC_REL_ABS) && *rsize >= 51 &&
			rdesc[32] == 0x81 && rdesc[33] == 0x06 &&
			rdesc[49] == 0x81 && rdesc[50] == 0x06) {
		hid_info(hdev,
			 "fixing up rel/abs in Logitech report descriptor\n");
		rdesc[33] = rdesc[50] = 0x02;
	}

	switch (hdev->product) {

	/* Several wheels report as this id when operating in emulation mode. */
	case USB_DEVICE_ID_LOGITECH_WHEEL:
		udesc = &(hid_to_usb_dev(hdev)->descriptor);
		if (!udesc) {
			hid_err(hdev, "NULL USB device descriptor\n");
			break;
		}
		bcdDevice = le16_to_cpu(udesc->bcdDevice);
		rev_maj = bcdDevice >> 8;
		rev_min = bcdDevice & 0xff;

		/* Update the report descriptor for only the Driving Force wheel */
		if (rev_maj == 1 && rev_min == 2 &&
				*rsize == DF_RDESC_ORIG_SIZE) {
			hid_info(hdev,
				"fixing up Logitech Driving Force report descriptor\n");
			rdesc = df_rdesc_fixed;
			*rsize = sizeof(df_rdesc_fixed);
		}
		break;

	case USB_DEVICE_ID_LOGITECH_MOMO_WHEEL:
		if (*rsize == MOMO_RDESC_ORIG_SIZE) {
			hid_info(hdev,
				"fixing up Logitech Momo Force (Red) report descriptor\n");
			rdesc = momo_rdesc_fixed;
			*rsize = sizeof(momo_rdesc_fixed);
		}
		break;

	case USB_DEVICE_ID_LOGITECH_MOMO_WHEEL2:
		if (*rsize == MOMO2_RDESC_ORIG_SIZE) {
			hid_info(hdev,
				"fixing up Logitech Momo Racing Force (Black) report descriptor\n");
			rdesc = momo2_rdesc_fixed;
			*rsize = sizeof(momo2_rdesc_fixed);
		}
		break;

	case USB_DEVICE_ID_LOGITECH_VIBRATION_WHEEL:
		if (*rsize == FV_RDESC_ORIG_SIZE) {
			hid_info(hdev,
				"fixing up Logitech Formula Vibration report descriptor\n");
			rdesc = fv_rdesc_fixed;
			*rsize = sizeof(fv_rdesc_fixed);
		}
		break;

	case USB_DEVICE_ID_LOGITECH_DFP_WHEEL:
		if (*rsize == DFP_RDESC_ORIG_SIZE) {
			hid_info(hdev,
				"fixing up Logitech Driving Force Pro report descriptor\n");
			rdesc = dfp_rdesc_fixed;
			*rsize = sizeof(dfp_rdesc_fixed);
		}
		break;

	case USB_DEVICE_ID_LOGITECH_WII_WHEEL:
		if (*rsize >= 101 && rdesc[41] == 0x95 && rdesc[42] == 0x0B &&
				rdesc[47] == 0x05 && rdesc[48] == 0x09) {
			hid_info(hdev, "fixing up Logitech Speed Force Wireless report descriptor\n");
			rdesc[41] = 0x05;
			rdesc[42] = 0x09;
			rdesc[47] = 0x95;
			rdesc[48] = 0x0B;
		}
		break;
	}

	return rdesc;
}

#define lg_map_key_clear(c)	hid_map_usage_clear(hi, usage, bit, max, \
		EV_KEY, (c))

static int lg_ultrax_remote_mapping(struct hid_input *hi,
		struct hid_usage *usage, unsigned long **bit, int *max)
{
	if ((usage->hid & HID_USAGE_PAGE) != HID_UP_LOGIVENDOR)
		return 0;

	set_bit(EV_REP, hi->input->evbit);
	switch (usage->hid & HID_USAGE) {
	/* Reported on Logitech Ultra X Media Remote */
	case 0x004: lg_map_key_clear(KEY_AGAIN);	break;
	case 0x00d: lg_map_key_clear(KEY_HOME);		break;
	case 0x024: lg_map_key_clear(KEY_SHUFFLE);	break;
	case 0x025: lg_map_key_clear(KEY_TV);		break;
	case 0x026: lg_map_key_clear(KEY_MENU);		break;
	case 0x031: lg_map_key_clear(KEY_AUDIO);	break;
	case 0x032: lg_map_key_clear(KEY_TEXT);		break;
	case 0x033: lg_map_key_clear(KEY_LAST);		break;
	case 0x047: lg_map_key_clear(KEY_MP3);		break;
	case 0x048: lg_map_key_clear(KEY_DVD);		break;
	case 0x049: lg_map_key_clear(KEY_MEDIA);	break;
	case 0x04a: lg_map_key_clear(KEY_VIDEO);	break;
	case 0x04b: lg_map_key_clear(KEY_ANGLE);	break;
	case 0x04c: lg_map_key_clear(KEY_LANGUAGE);	break;
	case 0x04d: lg_map_key_clear(KEY_SUBTITLE);	break;
	case 0x051: lg_map_key_clear(KEY_RED);		break;
	case 0x052: lg_map_key_clear(KEY_CLOSE);	break;

	default:
		return 0;
	}
	return 1;
}

static int lg_dinovo_mapping(struct hid_input *hi, struct hid_usage *usage,
		unsigned long **bit, int *max)
{
	if ((usage->hid & HID_USAGE_PAGE) != HID_UP_LOGIVENDOR)
		return 0;

	switch (usage->hid & HID_USAGE) {

	case 0x00d: lg_map_key_clear(KEY_MEDIA);	break;
	default:
		return 0;

	}
	return 1;
}

static int lg_wireless_mapping(struct hid_input *hi, struct hid_usage *usage,
		unsigned long **bit, int *max)
{
	if ((usage->hid & HID_USAGE_PAGE) != HID_UP_CONSUMER)
		return 0;

	switch (usage->hid & HID_USAGE) {
	case 0x1001: lg_map_key_clear(KEY_MESSENGER);		break;
	case 0x1003: lg_map_key_clear(KEY_SOUND);		break;
	case 0x1004: lg_map_key_clear(KEY_VIDEO);		break;
	case 0x1005: lg_map_key_clear(KEY_AUDIO);		break;
	case 0x100a: lg_map_key_clear(KEY_DOCUMENTS);		break;
	/* The following two entries are Playlist 1 and 2 on the MX3200 */
	case 0x100f: lg_map_key_clear(KEY_FN_1);		break;
	case 0x1010: lg_map_key_clear(KEY_FN_2);		break;
	case 0x1011: lg_map_key_clear(KEY_PREVIOUSSONG);	break;
	case 0x1012: lg_map_key_clear(KEY_NEXTSONG);		break;
	case 0x1013: lg_map_key_clear(KEY_CAMERA);		break;
	case 0x1014: lg_map_key_clear(KEY_MESSENGER);		break;
	case 0x1015: lg_map_key_clear(KEY_RECORD);		break;
	case 0x1016: lg_map_key_clear(KEY_PLAYER);		break;
	case 0x1017: lg_map_key_clear(KEY_EJECTCD);		break;
	case 0x1018: lg_map_key_clear(KEY_MEDIA);		break;
	case 0x1019: lg_map_key_clear(KEY_PROG1);		break;
	case 0x101a: lg_map_key_clear(KEY_PROG2);		break;
	case 0x101b: lg_map_key_clear(KEY_PROG3);		break;
	case 0x101c: lg_map_key_clear(KEY_CYCLEWINDOWS);	break;
	case 0x101f: lg_map_key_clear(KEY_ZOOMIN);		break;
	case 0x1020: lg_map_key_clear(KEY_ZOOMOUT);		break;
	case 0x1021: lg_map_key_clear(KEY_ZOOMRESET);		break;
	case 0x1023: lg_map_key_clear(KEY_CLOSE);		break;
	case 0x1027: lg_map_key_clear(KEY_MENU);		break;
	/* this one is marked as 'Rotate' */
	case 0x1028: lg_map_key_clear(KEY_ANGLE);		break;
	case 0x1029: lg_map_key_clear(KEY_SHUFFLE);		break;
	case 0x102a: lg_map_key_clear(KEY_BACK);		break;
	case 0x102b: lg_map_key_clear(KEY_CYCLEWINDOWS);	break;
	case 0x102d: lg_map_key_clear(KEY_WWW);			break;
	/* The following two are 'Start/answer call' and 'End/reject call'
	   on the MX3200 */
	case 0x1031: lg_map_key_clear(KEY_OK);			break;
	case 0x1032: lg_map_key_clear(KEY_CANCEL);		break;
	case 0x1041: lg_map_key_clear(KEY_BATTERY);		break;
	case 0x1042: lg_map_key_clear(KEY_WORDPROCESSOR);	break;
	case 0x1043: lg_map_key_clear(KEY_SPREADSHEET);		break;
	case 0x1044: lg_map_key_clear(KEY_PRESENTATION);	break;
	case 0x1045: lg_map_key_clear(KEY_UNDO);		break;
	case 0x1046: lg_map_key_clear(KEY_REDO);		break;
	case 0x1047: lg_map_key_clear(KEY_PRINT);		break;
	case 0x1048: lg_map_key_clear(KEY_SAVE);		break;
	case 0x1049: lg_map_key_clear(KEY_PROG1);		break;
	case 0x104a: lg_map_key_clear(KEY_PROG2);		break;
	case 0x104b: lg_map_key_clear(KEY_PROG3);		break;
	case 0x104c: lg_map_key_clear(KEY_PROG4);		break;

	default:
		return 0;
	}
	return 1;
}

static int lg_input_mapping(struct hid_device *hdev, struct hid_input *hi,
		struct hid_field *field, struct hid_usage *usage,
		unsigned long **bit, int *max)
{
	/* extended mapping for certain Logitech hardware (Logitech cordless
	   desktop LX500) */
	static const u8 e_keymap[] = {
		  0,216,  0,213,175,156,  0,  0,  0,  0,
		144,  0,  0,  0,  0,  0,  0,  0,  0,212,
		174,167,152,161,112,  0,  0,  0,154,  0,
		  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
		  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
		  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
		  0,  0,  0,  0,  0,183,184,185,186,187,
		188,189,190,191,192,193,194,  0,  0,  0
	};
	struct lg_drv_data *drv_data = hid_get_drvdata(hdev);
	unsigned int hid = usage->hid;

	if (hdev->product == USB_DEVICE_ID_LOGITECH_RECEIVER &&
			lg_ultrax_remote_mapping(hi, usage, bit, max))
		return 1;

	if (hdev->product == USB_DEVICE_ID_DINOVO_MINI &&
			lg_dinovo_mapping(hi, usage, bit, max))
		return 1;

	if ((drv_data->quirks & LG_WIRELESS) && lg_wireless_mapping(hi, usage, bit, max))
		return 1;

	if ((hid & HID_USAGE_PAGE) != HID_UP_BUTTON)
		return 0;

	hid &= HID_USAGE;

	/* Special handling for Logitech Cordless Desktop */
	if (field->application == HID_GD_MOUSE) {
		if ((drv_data->quirks & LG_IGNORE_DOUBLED_WHEEL) &&
				(hid == 7 || hid == 8))
			return -1;
	} else {
		if ((drv_data->quirks & LG_EXPANDED_KEYMAP) &&
				hid < ARRAY_SIZE(e_keymap) &&
				e_keymap[hid] != 0) {
			hid_map_usage(hi, usage, bit, max, EV_KEY,
					e_keymap[hid]);
			return 1;
		}
	}

	return 0;
}

static int lg_input_mapped(struct hid_device *hdev, struct hid_input *hi,
		struct hid_field *field, struct hid_usage *usage,
		unsigned long **bit, int *max)
{
	struct lg_drv_data *drv_data = hid_get_drvdata(hdev);

	if ((drv_data->quirks & LG_BAD_RELATIVE_KEYS) && usage->type == EV_KEY &&
			(field->flags & HID_MAIN_ITEM_RELATIVE))
		field->flags &= ~HID_MAIN_ITEM_RELATIVE;

	if ((drv_data->quirks & LG_DUPLICATE_USAGES) && (usage->type == EV_KEY ||
			 usage->type == EV_REL || usage->type == EV_ABS))
		clear_bit(usage->code, *bit);

	/* Ensure that Logitech wheels are not given a default fuzz/flat value */
	if (usage->type == EV_ABS && (usage->code == ABS_X ||
			usage->code == ABS_Y || usage->code == ABS_Z ||
			usage->code == ABS_RZ)) {
		switch (hdev->product) {
		case USB_DEVICE_ID_LOGITECH_WHEEL:
		case USB_DEVICE_ID_LOGITECH_MOMO_WHEEL:
		case USB_DEVICE_ID_LOGITECH_DFP_WHEEL:
		case USB_DEVICE_ID_LOGITECH_G25_WHEEL:
		case USB_DEVICE_ID_LOGITECH_DFGT_WHEEL:
		case USB_DEVICE_ID_LOGITECH_G27_WHEEL:
		case USB_DEVICE_ID_LOGITECH_WII_WHEEL:
		case USB_DEVICE_ID_LOGITECH_MOMO_WHEEL2:
		case USB_DEVICE_ID_LOGITECH_VIBRATION_WHEEL:
			field->application = HID_GD_MULTIAXIS;
			break;
		default:
			break;
		}
	}

	return 0;
}

static int lg_event(struct hid_device *hdev, struct hid_field *field,
		struct hid_usage *usage, __s32 value)
{
	struct lg_drv_data *drv_data = hid_get_drvdata(hdev);

	if ((drv_data->quirks & LG_INVERT_HWHEEL) && usage->code == REL_HWHEEL) {
		input_event(field->hidinput->input, usage->type, usage->code,
				-value);
		return 1;
	}
	if (drv_data->quirks & LG_FF4) {
		return lg4ff_adjust_input_event(hdev, field, usage, value, drv_data);
	}

	return 0;
}

static int lg_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	unsigned int connect_mask = HID_CONNECT_DEFAULT;
	struct lg_drv_data *drv_data;
	int ret;

	drv_data = kzalloc(sizeof(struct lg_drv_data), GFP_KERNEL);
	if (!drv_data) {
		hid_err(hdev, "Insufficient memory, cannot allocate driver data\n");
		return -ENOMEM;
	}
	drv_data->quirks = id->driver_data;

	hid_set_drvdata(hdev, (void *)drv_data);

	if (drv_data->quirks & LG_NOGET)
		hdev->quirks |= HID_QUIRK_NOGET;

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "parse failed\n");
		goto err_free;
	}

	if (drv_data->quirks & (LG_FF | LG_FF2 | LG_FF3 | LG_FF4))
		connect_mask &= ~HID_CONNECT_FF;

	ret = hid_hw_start(hdev, connect_mask);
	if (ret) {
		hid_err(hdev, "hw start failed\n");
		goto err_free;
	}

	/* Setup wireless link with Logitech Wii wheel */
	if (hdev->product == USB_DEVICE_ID_LOGITECH_WII_WHEEL) {
		unsigned char buf[] = { 0x00, 0xAF,  0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

		ret = hid_hw_raw_request(hdev, buf[0], buf, sizeof(buf),
					HID_FEATURE_REPORT, HID_REQ_SET_REPORT);

		if (ret >= 0) {
			/* insert a little delay of 10 jiffies ~ 40ms */
			wait_queue_head_t wait;
			init_waitqueue_head (&wait);
			wait_event_interruptible_timeout(wait, 0,
							 msecs_to_jiffies(40));

			/* Select random Address */
			buf[1] = 0xB2;
			get_random_bytes(&buf[2], 2);

			ret = hid_hw_raw_request(hdev, buf[0], buf, sizeof(buf),
					HID_FEATURE_REPORT, HID_REQ_SET_REPORT);
		}
	}

	if (drv_data->quirks & LG_FF)
		ret = lgff_init(hdev);
	else if (drv_data->quirks & LG_FF2)
		ret = lg2ff_init(hdev);
	else if (drv_data->quirks & LG_FF3)
		ret = lg3ff_init(hdev);
	else if (drv_data->quirks & LG_FF4)
		ret = lg4ff_init(hdev);

	if (ret)
		goto err_free;

	return 0;
err_free:
	kfree(drv_data);
	return ret;
}

static void lg_remove(struct hid_device *hdev)
{
	struct lg_drv_data *drv_data = hid_get_drvdata(hdev);
	if (drv_data->quirks & LG_FF4)
		lg4ff_deinit(hdev);
	else
		hid_hw_stop(hdev);
	kfree(drv_data);
}

static const struct hid_device_id lg_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_MX3000_RECEIVER),
		.driver_data = LG_RDESC | LG_WIRELESS },
	{ HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_S510_RECEIVER),
		.driver_data = LG_RDESC | LG_WIRELESS },
	{ HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_S510_RECEIVER_2),
		.driver_data = LG_RDESC | LG_WIRELESS },

	{ HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_LOGITECH_RECEIVER),
		.driver_data = LG_BAD_RELATIVE_KEYS },

	{ HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_DINOVO_DESKTOP),
		.driver_data = LG_DUPLICATE_USAGES },
	{ HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_DINOVO_EDGE),
		.driver_data = LG_DUPLICATE_USAGES },
	{ HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_DINOVO_MINI),
		.driver_data = LG_DUPLICATE_USAGES },

	{ HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_LOGITECH_ELITE_KBD),
		.driver_data = LG_IGNORE_DOUBLED_WHEEL | LG_EXPANDED_KEYMAP },
	{ HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_LOGITECH_CORDLESS_DESKTOP_LX500),
		.driver_data = LG_IGNORE_DOUBLED_WHEEL | LG_EXPANDED_KEYMAP },

	{ HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_LOGITECH_EXTREME_3D),
		.driver_data = LG_NOGET },
	{ HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_LOGITECH_DUAL_ACTION),
		.driver_data = LG_NOGET },
	{ HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_LOGITECH_WHEEL),
		.driver_data = LG_NOGET | LG_FF4 },

	{ HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_LOGITECH_RUMBLEPAD_CORD),
		.driver_data = LG_FF2 },
	{ HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_LOGITECH_RUMBLEPAD),
		.driver_data = LG_FF },
	{ HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_LOGITECH_RUMBLEPAD2_2),
		.driver_data = LG_FF },
	{ HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_LOGITECH_WINGMAN_F3D),
		.driver_data = LG_FF },
	{ HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_LOGITECH_FORCE3D_PRO),
		.driver_data = LG_FF },
	{ HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_LOGITECH_MOMO_WHEEL),
		.driver_data = LG_NOGET | LG_FF4 },
	{ HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_LOGITECH_MOMO_WHEEL2),
		.driver_data = LG_FF4 },
	{ HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_LOGITECH_VIBRATION_WHEEL),
		.driver_data = LG_FF2 },
	{ HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_LOGITECH_G25_WHEEL),
		.driver_data = LG_FF4 },
	{ HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_LOGITECH_DFGT_WHEEL),
		.driver_data = LG_FF4 },
	{ HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_LOGITECH_G27_WHEEL),
		.driver_data = LG_FF4 },
	{ HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_LOGITECH_DFP_WHEEL),
		.driver_data = LG_NOGET | LG_FF4 },
	{ HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_LOGITECH_WII_WHEEL),
		.driver_data = LG_FF4 },
	{ HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_LOGITECH_WINGMAN_FFG),
		.driver_data = LG_FF },
	{ HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_LOGITECH_RUMBLEPAD2),
		.driver_data = LG_FF2 },
	{ HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_LOGITECH_FLIGHT_SYSTEM_G940),
		.driver_data = LG_FF3 },
	{ HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_SPACENAVIGATOR),
		.driver_data = LG_RDESC_REL_ABS },
	{ HID_USB_DEVICE(USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_SPACETRAVELLER),
		.driver_data = LG_RDESC_REL_ABS },
	{ }
};

MODULE_DEVICE_TABLE(hid, lg_devices);

static struct hid_driver lg_driver = {
	.name = "logitech",
	.id_table = lg_devices,
	.report_fixup = lg_report_fixup,
	.input_mapping = lg_input_mapping,
	.input_mapped = lg_input_mapped,
	.event = lg_event,
	.probe = lg_probe,
	.remove = lg_remove,
};
module_hid_driver(lg_driver);

#ifdef CONFIG_LOGIWHEELS_FF
int lg4ff_no_autoswitch = 0;
module_param_named(lg4ff_no_autoswitch, lg4ff_no_autoswitch, int, S_IRUGO);
MODULE_PARM_DESC(lg4ff_no_autoswitch, "Do not switch multimode wheels to their native mode automatically");
#endif

MODULE_LICENSE("GPL");
