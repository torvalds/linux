// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  HID driver for Kye/Genius devices not fully compliant with HID standard
 *
 *  Copyright (c) 2009 Jiri Kosina
 *  Copyright (c) 2009 Tomas Hanak
 *  Copyright (c) 2012 Nikolai Kondrashov
 *  Copyright (c) 2023 David Yang
 */

#include <asm-generic/unaligned.h>
#include <linux/device.h>
#include <linux/hid.h>
#include <linux/module.h>

#include "hid-ids.h"

/* Data gathered from Database/VID0458_PID????/Vista/TBoard/default.xml in ioTablet driver
 *
 * TODO:
 *   - Add battery and sleep support for EasyPen M406W and MousePen M508WX
 *   - Investigate ScrollZ.MiceFMT buttons of EasyPen M406
 */

static const __u8 easypen_m406_control_rdesc[] = {
	0x05, 0x0C,        /*  Usage Page (Consumer),    */
	0x09, 0x01,        /*  Usage (Consumer Control), */
	0xA1, 0x01,        /*  Collection (Application), */
	0x85, 0x12,        /*    Report ID (18),         */
	0x0A, 0x45, 0x02,  /*    Usage (AC Rotate),      */
	0x09, 0x40,        /*    Usage (Menu),           */
	0x0A, 0x2F, 0x02,  /*    Usage (AC Zoom),        */
	0x0A, 0x46, 0x02,  /*    Usage (AC Resize),      */
	0x0A, 0x1A, 0x02,  /*    Usage (AC Undo),        */
	0x0A, 0x6A, 0x02,  /*    Usage (AC Delete),      */
	0x0A, 0x24, 0x02,  /*    Usage (AC Back),        */
	0x0A, 0x25, 0x02,  /*    Usage (AC Forward),     */
	0x14,              /*    Logical Minimum (0),    */
	0x25, 0x01,        /*    Logical Maximum (1),    */
	0x75, 0x01,        /*    Report Size (1),        */
	0x95, 0x08,        /*    Report Count (8),       */
	0x81, 0x02,        /*    Input (Variable),       */
	0x95, 0x30,        /*    Report Count (48),      */
	0x81, 0x01,        /*    Input (Constant),       */
	0xC0               /*  End Collection            */
};

static const __u8 easypen_m506_control_rdesc[] = {
	0x05, 0x0C,        /*  Usage Page (Consumer),    */
	0x09, 0x01,        /*  Usage (Consumer Control), */
	0xA1, 0x01,        /*  Collection (Application), */
	0x85, 0x12,        /*    Report ID (18),         */
	0x0A, 0x6A, 0x02,  /*    Usage (AC Delete),      */
	0x0A, 0x1A, 0x02,  /*    Usage (AC Undo),        */
	0x0A, 0x2D, 0x02,  /*    Usage (AC Zoom In),     */
	0x0A, 0x2E, 0x02,  /*    Usage (AC Zoom Out),    */
	0x14,              /*    Logical Minimum (0),    */
	0x25, 0x01,        /*    Logical Maximum (1),    */
	0x75, 0x01,        /*    Report Size (1),        */
	0x95, 0x04,        /*    Report Count (4),       */
	0x81, 0x02,        /*    Input (Variable),       */
	0x95, 0x34,        /*    Report Count (52),      */
	0x81, 0x01,        /*    Input (Constant),       */
	0xC0               /*  End Collection            */
};

static const __u8 easypen_m406w_control_rdesc[] = {
	0x05, 0x0C,        /*  Usage Page (Consumer),    */
	0x09, 0x01,        /*  Usage (Consumer Control), */
	0xA1, 0x01,        /*  Collection (Application), */
	0x85, 0x12,        /*    Report ID (18),         */
	0x0A, 0x6A, 0x02,  /*    Usage (AC Delete),      */
	0x0A, 0x1A, 0x02,  /*    Usage (AC Undo),        */
	0x0A, 0x01, 0x02,  /*    Usage (AC New),         */
	0x09, 0x40,        /*    Usage (Menu),           */
	0x14,              /*    Logical Minimum (0),    */
	0x25, 0x01,        /*    Logical Maximum (1),    */
	0x75, 0x01,        /*    Report Size (1),        */
	0x95, 0x04,        /*    Report Count (4),       */
	0x81, 0x02,        /*    Input (Variable),       */
	0x95, 0x34,        /*    Report Count (52),      */
	0x81, 0x01,        /*    Input (Constant),       */
	0xC0               /*  End Collection            */
};

static const __u8 easypen_m610x_control_rdesc[] = {
	0x05, 0x0C,        /*  Usage Page (Consumer),       */
	0x09, 0x01,        /*  Usage (Consumer Control),    */
	0xA1, 0x01,        /*  Collection (Application),    */
	0x85, 0x12,        /*    Report ID (18),            */
	0x0A, 0x1A, 0x02,  /*    Usage (AC Undo),           */
	0x0A, 0x79, 0x02,  /*    Usage (AC Redo Or Repeat), */
	0x0A, 0x2D, 0x02,  /*    Usage (AC Zoom In),        */
	0x0A, 0x2E, 0x02,  /*    Usage (AC Zoom Out),       */
	0x14,              /*    Logical Minimum (0),       */
	0x25, 0x01,        /*    Logical Maximum (1),       */
	0x75, 0x01,        /*    Report Size (1),           */
	0x95, 0x04,        /*    Report Count (4),          */
	0x81, 0x02,        /*    Input (Variable),          */
	0x95, 0x34,        /*    Report Count (52),         */
	0x81, 0x01,        /*    Input (Constant),          */
	0xC0               /*  End Collection               */
};

static const __u8 pensketch_m912_control_rdesc[] = {
	0x05, 0x0C,        /*  Usage Page (Consumer),        */
	0x09, 0x01,        /*  Usage (Consumer Control),     */
	0xA1, 0x01,        /*  Collection (Application),     */
	0x85, 0x12,        /*    Report ID (18),             */
	0x14,              /*    Logical Minimum (0),        */
	0x25, 0x01,        /*    Logical Maximum (1),        */
	0x75, 0x01,        /*    Report Size (1),            */
	0x95, 0x08,        /*    Report Count (8),           */
	0x05, 0x0C,        /*    Usage Page (Consumer),      */
	0x0A, 0x6A, 0x02,  /*    Usage (AC Delete),          */
	0x0A, 0x1A, 0x02,  /*    Usage (AC Undo),            */
	0x0A, 0x01, 0x02,  /*    Usage (AC New),             */
	0x0A, 0x2F, 0x02,  /*    Usage (AC Zoom),            */
	0x0A, 0x25, 0x02,  /*    Usage (AC Forward),         */
	0x0A, 0x24, 0x02,  /*    Usage (AC Back),            */
	0x0A, 0x2D, 0x02,  /*    Usage (AC Zoom In),         */
	0x0A, 0x2E, 0x02,  /*    Usage (AC Zoom Out),        */
	0x81, 0x02,        /*    Input (Variable),           */
	0x95, 0x30,        /*    Report Count (48),          */
	0x81, 0x03,        /*    Input (Constant, Variable), */
	0xC0               /*  End Collection                */
};

static const __u8 mousepen_m508wx_control_rdesc[] = {
	0x05, 0x0C,        /*  Usage Page (Consumer),    */
	0x09, 0x01,        /*  Usage (Consumer Control), */
	0xA1, 0x01,        /*  Collection (Application), */
	0x85, 0x12,        /*    Report ID (18),         */
	0x0A, 0x1A, 0x02,  /*    Usage (AC Undo),        */
	0x0A, 0x6A, 0x02,  /*    Usage (AC Delete),      */
	0x0A, 0x2D, 0x02,  /*    Usage (AC Zoom In),     */
	0x0A, 0x2E, 0x02,  /*    Usage (AC Zoom Out),    */
	0x14,              /*    Logical Minimum (0),    */
	0x25, 0x01,        /*    Logical Maximum (1),    */
	0x75, 0x01,        /*    Report Size (1),        */
	0x95, 0x04,        /*    Report Count (4),       */
	0x81, 0x02,        /*    Input (Variable),       */
	0x95, 0x34,        /*    Report Count (52),      */
	0x81, 0x01,        /*    Input (Constant),       */
	0xC0               /*  End Collection            */
};

static const __u8 mousepen_m508x_control_rdesc[] = {
	0x05, 0x0C,        /*  Usage Page (Consumer),        */
	0x09, 0x01,        /*  Usage (Consumer Control),     */
	0xA1, 0x01,        /*  Collection (Application),     */
	0x85, 0x12,        /*    Report ID (18),             */
	0x0A, 0x01, 0x02,  /*    Usage (AC New),             */
	0x09, 0x40,        /*    Usage (Menu),               */
	0x0A, 0x6A, 0x02,  /*    Usage (AC Delete),          */
	0x0A, 0x1A, 0x02,  /*    Usage (AC Undo),            */
	0x14,              /*    Logical Minimum (0),        */
	0x25, 0x01,        /*    Logical Maximum (1),        */
	0x75, 0x01,        /*    Report Size (1),            */
	0x95, 0x04,        /*    Report Count (4),           */
	0x81, 0x02,        /*    Input (Variable),           */
	0x81, 0x01,        /*    Input (Constant),           */
	0x15, 0xFF,        /*    Logical Minimum (-1),       */
	0x95, 0x10,        /*    Report Count (16),          */
	0x81, 0x01,        /*    Input (Constant),           */
	0x0A, 0x35, 0x02,  /*    Usage (AC Scroll),          */
	0x0A, 0x2F, 0x02,  /*    Usage (AC Zoom),            */
	0x0A, 0x38, 0x02,  /*    Usage (AC Pan),             */
	0x75, 0x08,        /*    Report Size (8),            */
	0x95, 0x03,        /*    Report Count (3),           */
	0x81, 0x06,        /*    Input (Variable, Relative), */
	0x95, 0x01,        /*    Report Count (1),           */
	0x81, 0x01,        /*    Input (Constant),           */
	0xC0               /*  End Collection                */
};

static const __u8 easypen_m406xe_control_rdesc[] = {
	0x05, 0x0C,        /*  Usage Page (Consumer),          */
	0x09, 0x01,        /*  Usage (Consumer Control),       */
	0xA1, 0x01,        /*  Collection (Application),       */
	0x85, 0x12,        /*      Report ID (18),             */
	0x14,              /*      Logical Minimum (0),        */
	0x25, 0x01,        /*      Logical Maximum (1),        */
	0x75, 0x01,        /*      Report Size (1),            */
	0x95, 0x04,        /*      Report Count (4),           */
	0x0A, 0x79, 0x02,  /*      Usage (AC Redo Or Repeat),  */
	0x0A, 0x1A, 0x02,  /*      Usage (AC Undo),            */
	0x0A, 0x2D, 0x02,  /*      Usage (AC Zoom In),         */
	0x0A, 0x2E, 0x02,  /*      Usage (AC Zoom Out),        */
	0x81, 0x02,        /*      Input (Variable),           */
	0x95, 0x34,        /*      Report Count (52),          */
	0x81, 0x03,        /*      Input (Constant, Variable), */
	0xC0               /*  End Collection                  */
};

static const __u8 pensketch_t609a_control_rdesc[] = {
	0x05, 0x0C,        /*  Usage Page (Consumer),    */
	0x09, 0x01,        /*  Usage (Consumer Control), */
	0xA1, 0x01,        /*  Collection (Application), */
	0x85, 0x12,        /*    Report ID (18),         */
	0x0A, 0x6A, 0x02,  /*    Usage (AC Delete),      */
	0x14,              /*    Logical Minimum (0),    */
	0x25, 0x01,        /*    Logical Maximum (1),    */
	0x75, 0x01,        /*    Report Size (1),        */
	0x95, 0x08,        /*    Report Count (8),       */
	0x81, 0x02,        /*    Input (Variable),       */
	0x95, 0x37,        /*    Report Count (55),      */
	0x81, 0x01,        /*    Input (Constant),       */
	0xC0               /*  End Collection            */
};

/* Fix indexes in kye_tablet_fixup if you change this */
static const __u8 kye_tablet_rdesc[] = {
	0x06, 0x00, 0xFF,             /*  Usage Page (FF00h),             */
	0x09, 0x01,                   /*  Usage (01h),                    */
	0xA1, 0x01,                   /*  Collection (Application),       */
	0x85, 0x05,                   /*    Report ID (5),                */
	0x09, 0x01,                   /*    Usage (01h),                  */
	0x15, 0x81,                   /*    Logical Minimum (-127),       */
	0x25, 0x7F,                   /*    Logical Maximum (127),        */
	0x75, 0x08,                   /*    Report Size (8),              */
	0x95, 0x07,                   /*    Report Count (7),             */
	0xB1, 0x02,                   /*    Feature (Variable),           */
	0xC0,                         /*  End Collection,                 */
	0x05, 0x0D,                   /*  Usage Page (Digitizer),         */
	0x09, 0x01,                   /*  Usage (Digitizer),              */
	0xA1, 0x01,                   /*  Collection (Application),       */
	0x85, 0x10,                   /*    Report ID (16),               */
	0x09, 0x20,                   /*    Usage (Stylus),               */
	0xA0,                         /*    Collection (Physical),        */
	0x09, 0x42,                   /*      Usage (Tip Switch),         */
	0x09, 0x44,                   /*      Usage (Barrel Switch),      */
	0x09, 0x46,                   /*      Usage (Tablet Pick),        */
	0x14,                         /*      Logical Minimum (0),        */
	0x25, 0x01,                   /*      Logical Maximum (1),        */
	0x75, 0x01,                   /*      Report Size (1),            */
	0x95, 0x03,                   /*      Report Count (3),           */
	0x81, 0x02,                   /*      Input (Variable),           */
	0x95, 0x04,                   /*      Report Count (4),           */
	0x81, 0x01,                   /*      Input (Constant),           */
	0x09, 0x32,                   /*      Usage (In Range),           */
	0x95, 0x01,                   /*      Report Count (1),           */
	0x81, 0x02,                   /*      Input (Variable),           */
	0x75, 0x10,                   /*      Report Size (16),           */
	0xA4,                         /*      Push,                       */
	0x05, 0x01,                   /*      Usage Page (Desktop),       */
	0x09, 0x30,                   /*      Usage (X),                  */
	0x27, 0xFF, 0x7F, 0x00, 0x00, /*      Logical Maximum (32767),    */
	0x34,                         /*      Physical Minimum (0),       */
	0x47, 0x00, 0x00, 0x00, 0x00, /*      Physical Maximum (0),       */
	0x65, 0x11,                   /*      Unit (Centimeter),          */
	0x55, 0x00,                   /*      Unit Exponent (0),          */
	0x75, 0x10,                   /*      Report Size (16),           */
	0x81, 0x02,                   /*      Input (Variable),           */
	0x09, 0x31,                   /*      Usage (Y),                  */
	0x27, 0xFF, 0x7F, 0x00, 0x00, /*      Logical Maximum (32767),    */
	0x47, 0x00, 0x00, 0x00, 0x00, /*      Physical Maximum (0),       */
	0x81, 0x02,                   /*      Input (Variable),           */
	0xB4,                         /*      Pop,                        */
	0x05, 0x0D,                   /*      Usage Page (Digitizer),     */
	0x09, 0x30,                   /*      Usage (Tip Pressure),       */
	0x27, 0xFF, 0x07, 0x00, 0x00, /*      Logical Maximum (2047),     */
	0x81, 0x02,                   /*      Input (Variable),           */
	0xC0,                         /*    End Collection,               */
	0xC0,                         /*  End Collection,                 */
	0x05, 0x0D,                   /*  Usage Page (Digitizer),         */
	0x09, 0x21,                   /*  Usage (Puck),                   */
	0xA1, 0x01,                   /*  Collection (Application),       */
	0x85, 0x11,                   /*    Report ID (17),               */
	0x09, 0x21,                   /*    Usage (Puck),                 */
	0xA0,                         /*    Collection (Physical),        */
	0x05, 0x09,                   /*      Usage Page (Button),        */
	0x19, 0x01,                   /*      Usage Minimum (01h),        */
	0x29, 0x03,                   /*      Usage Maximum (03h),        */
	0x14,                         /*      Logical Minimum (0),        */
	0x25, 0x01,                   /*      Logical Maximum (1),        */
	0x75, 0x01,                   /*      Report Size (1),            */
	0x95, 0x03,                   /*      Report Count (3),           */
	0x81, 0x02,                   /*      Input (Variable),           */
	0x95, 0x04,                   /*      Report Count (4),           */
	0x81, 0x01,                   /*      Input (Constant),           */
	0x05, 0x0D,                   /*      Usage Page (Digitizer),     */
	0x09, 0x32,                   /*      Usage (In Range),           */
	0x95, 0x01,                   /*      Report Count (1),           */
	0x81, 0x02,                   /*      Input (Variable),           */
	0x05, 0x01,                   /*      Usage Page (Desktop),       */
	0xA4,                         /*      Push,                       */
	0x09, 0x30,                   /*      Usage (X),                  */
	0x27, 0xFF, 0x7F, 0x00, 0x00, /*      Logical Maximum (32767),    */
	0x34,                         /*      Physical Minimum (0),       */
	0x47, 0x00, 0x00, 0x00, 0x00, /*      Physical Maximum (0),       */
	0x65, 0x11,                   /*      Unit (Centimeter),          */
	0x55, 0x00,                   /*      Unit Exponent (0),          */
	0x75, 0x10,                   /*      Report Size (16),           */
	0x81, 0x02,                   /*      Input (Variable),           */
	0x09, 0x31,                   /*      Usage (Y),                  */
	0x27, 0xFF, 0x7F, 0x00, 0x00, /*      Logical Maximum (32767),    */
	0x47, 0x00, 0x00, 0x00, 0x00, /*      Physical Maximum (0),       */
	0x81, 0x02,                   /*      Input (Variable),           */
	0xB4,                         /*      Pop,                        */
	0x09, 0x38,                   /*      Usage (Wheel),              */
	0x15, 0xFF,                   /*      Logical Minimum (-1),       */
	0x75, 0x08,                   /*      Report Size (8),            */
	0x95, 0x01,                   /*      Report Count (1),           */
	0x81, 0x06,                   /*      Input (Variable, Relative), */
	0x81, 0x01,                   /*      Input (Constant),           */
	0xC0,                         /*    End Collection,               */
	0xC0                          /*  End Collection                  */
};

static const struct kye_tablet_info {
	__u32 product;
	__s32 x_logical_maximum;
	__s32 y_logical_maximum;
	__s32 pressure_logical_maximum;
	__s32 x_physical_maximum;
	__s32 y_physical_maximum;
	__s8 unit_exponent;
	__s8 unit;
	bool has_punk;
	unsigned int control_rsize;
	const __u8 *control_rdesc;
} kye_tablets_info[] = {
	{USB_DEVICE_ID_KYE_EASYPEN_M406,  /* 0x5005 */
		15360, 10240, 1023,    6,   4,  0, 0x13, false,
		sizeof(easypen_m406_control_rdesc), easypen_m406_control_rdesc},
	{USB_DEVICE_ID_KYE_EASYPEN_M506,  /* 0x500F */
		24576, 20480, 1023,    6,   5,  0, 0x13, false,
		sizeof(easypen_m506_control_rdesc), easypen_m506_control_rdesc},
	{USB_DEVICE_ID_KYE_EASYPEN_I405X,  /* 0x5010 */
		14080, 10240, 1023,   55,  40, -1, 0x13, false},
	{USB_DEVICE_ID_KYE_MOUSEPEN_I608X,  /* 0x5011 */
		20480, 15360, 2047,    8,   6,  0, 0x13,  true},
	{USB_DEVICE_ID_KYE_EASYPEN_M406W,  /* 0x5012 */
		15360, 10240, 1023,    6,   4,  0, 0x13, false,
		sizeof(easypen_m406w_control_rdesc), easypen_m406w_control_rdesc},
	{USB_DEVICE_ID_KYE_EASYPEN_M610X,  /* 0x5013 */
		40960, 25600, 1023, 1000, 625, -2, 0x13, false,
		sizeof(easypen_m610x_control_rdesc), easypen_m610x_control_rdesc},
	{USB_DEVICE_ID_KYE_EASYPEN_340,  /* 0x5014 */
		10240,  7680, 1023,    4,   3,  0, 0x13, false},
	{USB_DEVICE_ID_KYE_PENSKETCH_M912,  /* 0x5015 */
		61440, 46080, 2047,   12,   9,  0, 0x13,  true,
		sizeof(pensketch_m912_control_rdesc), pensketch_m912_control_rdesc},
	{USB_DEVICE_ID_KYE_MOUSEPEN_M508WX,  /* 0x5016 */
		40960, 25600, 2047,    8,   5,  0, 0x13,  true,
		sizeof(mousepen_m508wx_control_rdesc), mousepen_m508wx_control_rdesc},
	{USB_DEVICE_ID_KYE_MOUSEPEN_M508X,  /* 0x5017 */
		40960, 25600, 2047,    8,   5,  0, 0x13,  true,
		sizeof(mousepen_m508x_control_rdesc), mousepen_m508x_control_rdesc},
	{USB_DEVICE_ID_KYE_EASYPEN_M406XE,  /* 0x5019 */
		15360, 10240, 1023,    6,   4,  0, 0x13, false,
		sizeof(easypen_m406xe_control_rdesc), easypen_m406xe_control_rdesc},
	{USB_DEVICE_ID_KYE_MOUSEPEN_I608X_V2,  /* 0x501A */
		40960, 30720, 2047,    8,   6,  0, 0x13,  true},
	{USB_DEVICE_ID_KYE_PENSKETCH_T609A,  /* 0x501B */
		43520, 28160, 1023,   85,  55, -1, 0x13, false,
		sizeof(pensketch_t609a_control_rdesc), pensketch_t609a_control_rdesc},
	{}
};

static __u8 *kye_consumer_control_fixup(struct hid_device *hdev, __u8 *rdesc,
		unsigned int *rsize, int offset, const char *device_name)
{
	/*
	 * the fixup that need to be done:
	 *   - change Usage Maximum in the Consumer Control
	 *     (report ID 3) to a reasonable value
	 */
	if (*rsize >= offset + 31 &&
	    /* Usage Page (Consumer Devices) */
	    rdesc[offset] == 0x05 && rdesc[offset + 1] == 0x0c &&
	    /* Usage (Consumer Control) */
	    rdesc[offset + 2] == 0x09 && rdesc[offset + 3] == 0x01 &&
	    /*   Usage Maximum > 12287 */
	    rdesc[offset + 10] == 0x2a && rdesc[offset + 12] > 0x2f) {
		hid_info(hdev, "fixing up %s report descriptor\n", device_name);
		rdesc[offset + 12] = 0x2f;
	}
	return rdesc;
}

/*
 * Fix tablet descriptor of so-called "DataFormat 2".
 *
 * Though we may achieve a usable descriptor from original vendor-defined one,
 * some problems exist:
 *  - Their Logical Maximum never exceed 32767 (7F FF), though device do report
 *    values greater than that;
 *  - Physical Maximums are arbitrarily filled (always equal to Logical
 *    Maximum);
 *  - Detail for control buttons are not provided (a vendor-defined Usage Page
 *    with fixed content).
 *
 * Thus we use a pre-defined parameter table rather than digging it from
 * original descriptor.
 *
 * We may as well write a fallback routine for unrecognized kye tablet, but it's
 * clear kye are unlikely to produce new models in the foreseeable future, so we
 * simply enumerate all possible models.
 */
static __u8 *kye_tablet_fixup(struct hid_device *hdev, __u8 *rdesc, unsigned int *rsize)
{
	const struct kye_tablet_info *info;
	unsigned int newsize;

	if (*rsize < sizeof(kye_tablet_rdesc)) {
		hid_warn(hdev,
			 "tablet report size too small, or kye_tablet_rdesc unexpectedly large\n");
		return rdesc;
	}

	for (info = kye_tablets_info; info->product; info++) {
		if (hdev->product == info->product)
			break;
	}

	if (!info->product) {
		hid_err(hdev, "tablet unknown, someone forget to add kye_tablet_info entry?\n");
		return rdesc;
	}

	newsize = info->has_punk ? sizeof(kye_tablet_rdesc) : 112;
	memcpy(rdesc, kye_tablet_rdesc, newsize);

	put_unaligned_le32(info->x_logical_maximum, rdesc + 66);
	put_unaligned_le32(info->x_physical_maximum, rdesc + 72);
	rdesc[77] = info->unit;
	rdesc[79] = info->unit_exponent;
	put_unaligned_le32(info->y_logical_maximum, rdesc + 87);
	put_unaligned_le32(info->y_physical_maximum, rdesc + 92);
	put_unaligned_le32(info->pressure_logical_maximum, rdesc + 104);

	if (info->has_punk) {
		put_unaligned_le32(info->x_logical_maximum, rdesc + 156);
		put_unaligned_le32(info->x_physical_maximum, rdesc + 162);
		rdesc[167] = info->unit;
		rdesc[169] = info->unit_exponent;
		put_unaligned_le32(info->y_logical_maximum, rdesc + 177);
		put_unaligned_le32(info->y_physical_maximum, rdesc + 182);
	}

	if (info->control_rsize) {
		if (newsize + info->control_rsize > *rsize)
			hid_err(hdev, "control rdesc unexpectedly large");
		else {
			memcpy(rdesc + newsize, info->control_rdesc, info->control_rsize);
			newsize += info->control_rsize;
		}
	}

	*rsize = newsize;
	return rdesc;
}

static __u8 *kye_report_fixup(struct hid_device *hdev, __u8 *rdesc,
		unsigned int *rsize)
{
	switch (hdev->product) {
	case USB_DEVICE_ID_KYE_ERGO_525V:
		/* the fixups that need to be done:
		 *   - change led usage page to button for extra buttons
		 *   - report size 8 count 1 must be size 1 count 8 for button
		 *     bitfield
		 *   - change the button usage range to 4-7 for the extra
		 *     buttons
		 */
		if (*rsize >= 75 &&
			rdesc[61] == 0x05 && rdesc[62] == 0x08 &&
			rdesc[63] == 0x19 && rdesc[64] == 0x08 &&
			rdesc[65] == 0x29 && rdesc[66] == 0x0f &&
			rdesc[71] == 0x75 && rdesc[72] == 0x08 &&
			rdesc[73] == 0x95 && rdesc[74] == 0x01) {
			hid_info(hdev,
				 "fixing up Kye/Genius Ergo Mouse "
				 "report descriptor\n");
			rdesc[62] = 0x09;
			rdesc[64] = 0x04;
			rdesc[66] = 0x07;
			rdesc[72] = 0x01;
			rdesc[74] = 0x08;
		}
		break;
	case USB_DEVICE_ID_GENIUS_GILA_GAMING_MOUSE:
		rdesc = kye_consumer_control_fixup(hdev, rdesc, rsize, 104,
					"Genius Gila Gaming Mouse");
		break;
	case USB_DEVICE_ID_GENIUS_MANTICORE:
		rdesc = kye_consumer_control_fixup(hdev, rdesc, rsize, 104,
					"Genius Manticore Keyboard");
		break;
	case USB_DEVICE_ID_GENIUS_GX_IMPERATOR:
		rdesc = kye_consumer_control_fixup(hdev, rdesc, rsize, 83,
					"Genius Gx Imperator Keyboard");
		break;
	case USB_DEVICE_ID_KYE_EASYPEN_M406:
	case USB_DEVICE_ID_KYE_EASYPEN_M506:
	case USB_DEVICE_ID_KYE_EASYPEN_I405X:
	case USB_DEVICE_ID_KYE_MOUSEPEN_I608X:
	case USB_DEVICE_ID_KYE_EASYPEN_M406W:
	case USB_DEVICE_ID_KYE_EASYPEN_M610X:
	case USB_DEVICE_ID_KYE_EASYPEN_340:
	case USB_DEVICE_ID_KYE_PENSKETCH_M912:
	case USB_DEVICE_ID_KYE_MOUSEPEN_M508WX:
	case USB_DEVICE_ID_KYE_MOUSEPEN_M508X:
	case USB_DEVICE_ID_KYE_EASYPEN_M406XE:
	case USB_DEVICE_ID_KYE_MOUSEPEN_I608X_V2:
	case USB_DEVICE_ID_KYE_PENSKETCH_T609A:
		rdesc = kye_tablet_fixup(hdev, rdesc, rsize);
		break;
	}
	return rdesc;
}

static int kye_tablet_enable(struct hid_device *hdev)
{
	struct list_head *list;
	struct list_head *head;
	struct hid_report *report;
	__s32 *value;

	list = &hdev->report_enum[HID_FEATURE_REPORT].report_list;
	list_for_each(head, list) {
		report = list_entry(head, struct hid_report, list);
		if (report->id == 5)
			break;
	}

	if (head == list) {
		hid_err(hdev, "tablet-enabling feature report not found\n");
		return -ENODEV;
	}

	if (report->maxfield < 1 || report->field[0]->report_count < 7) {
		hid_err(hdev, "invalid tablet-enabling feature report\n");
		return -ENODEV;
	}

	value = report->field[0]->value;

	/*
	 * The code is for DataFormat 2 of config xml. They have no obvious
	 * meaning (at least not configurable in Windows driver) except enabling
	 * fully-functional tablet mode (absolute positioning). Otherwise, the
	 * tablet acts like a relative mouse.
	 *
	 * Though there're magic codes for DataFormat 3 and 4, no devices use
	 * these DataFormats.
	 */
	value[0] = 0x12;
	value[1] = 0x10;
	value[2] = 0x11;
	value[3] = 0x12;
	value[4] = 0x00;
	value[5] = 0x00;
	value[6] = 0x00;
	hid_hw_request(hdev, report, HID_REQ_SET_REPORT);

	return 0;
}

static int kye_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	int ret;

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "parse failed\n");
		goto err;
	}

	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
	if (ret) {
		hid_err(hdev, "hw start failed\n");
		goto err;
	}

	switch (id->product) {
	case USB_DEVICE_ID_GENIUS_MANTICORE:
		/*
		 * The manticore keyboard needs to have all the interfaces
		 * opened at least once to be fully functional.
		 */
		if (hid_hw_open(hdev))
			hid_hw_close(hdev);
		break;
	case USB_DEVICE_ID_KYE_EASYPEN_M406:
	case USB_DEVICE_ID_KYE_EASYPEN_M506:
	case USB_DEVICE_ID_KYE_EASYPEN_I405X:
	case USB_DEVICE_ID_KYE_MOUSEPEN_I608X:
	case USB_DEVICE_ID_KYE_EASYPEN_M406W:
	case USB_DEVICE_ID_KYE_EASYPEN_M610X:
	case USB_DEVICE_ID_KYE_EASYPEN_340:
	case USB_DEVICE_ID_KYE_PENSKETCH_M912:
	case USB_DEVICE_ID_KYE_MOUSEPEN_M508WX:
	case USB_DEVICE_ID_KYE_MOUSEPEN_M508X:
	case USB_DEVICE_ID_KYE_EASYPEN_M406XE:
	case USB_DEVICE_ID_KYE_MOUSEPEN_I608X_V2:
	case USB_DEVICE_ID_KYE_PENSKETCH_T609A:
		ret = kye_tablet_enable(hdev);
		if (ret) {
			hid_err(hdev, "tablet enabling failed\n");
			goto enabling_err;
		}
		break;
	}

	return 0;
enabling_err:
	hid_hw_stop(hdev);
err:
	return ret;
}

static const struct hid_device_id kye_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_KYE, USB_DEVICE_ID_KYE_ERGO_525V) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_KYE,
				USB_DEVICE_ID_GENIUS_GILA_GAMING_MOUSE) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_KYE,
				USB_DEVICE_ID_GENIUS_MANTICORE) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_KYE,
				USB_DEVICE_ID_GENIUS_GX_IMPERATOR) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_KYE,
				USB_DEVICE_ID_KYE_EASYPEN_M406) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_KYE,
				USB_DEVICE_ID_KYE_EASYPEN_M506) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_KYE,
				USB_DEVICE_ID_KYE_EASYPEN_I405X) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_KYE,
				USB_DEVICE_ID_KYE_MOUSEPEN_I608X) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_KYE,
				USB_DEVICE_ID_KYE_EASYPEN_M406W) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_KYE,
				USB_DEVICE_ID_KYE_EASYPEN_M610X) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_KYE,
				USB_DEVICE_ID_KYE_EASYPEN_340) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_KYE,
				USB_DEVICE_ID_KYE_PENSKETCH_M912) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_KYE,
				USB_DEVICE_ID_KYE_MOUSEPEN_M508WX) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_KYE,
				USB_DEVICE_ID_KYE_MOUSEPEN_M508X) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_KYE,
				USB_DEVICE_ID_KYE_EASYPEN_M406XE) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_KYE,
				USB_DEVICE_ID_KYE_MOUSEPEN_I608X_V2) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_KYE,
				USB_DEVICE_ID_KYE_PENSKETCH_T609A) },
	{ }
};
MODULE_DEVICE_TABLE(hid, kye_devices);

static struct hid_driver kye_driver = {
	.name = "kye",
	.id_table = kye_devices,
	.probe = kye_probe,
	.report_fixup = kye_report_fixup,
};
module_hid_driver(kye_driver);

MODULE_LICENSE("GPL");
