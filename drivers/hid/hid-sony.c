/*
 *  HID driver for Sony / PS2 / PS3 BD devices.
 *
 *  Copyright (c) 1999 Andreas Gal
 *  Copyright (c) 2000-2005 Vojtech Pavlik <vojtech@suse.cz>
 *  Copyright (c) 2005 Michael Haboustak <mike-@cinci.rr.com> for Concept2, Inc
 *  Copyright (c) 2008 Jiri Slaby
 *  Copyright (c) 2012 David Dillow <dave@thedillows.org>
 *  Copyright (c) 2006-2013 Jiri Kosina
 *  Copyright (c) 2013 Colin Leitner <colin.leitner@gmail.com>
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

/*
 * NOTE: in order for the Sony PS3 BD Remote Control to be found by
 * a Bluetooth host, the key combination Start+Enter has to be kept pressed
 * for about 7 seconds with the Bluetooth Host Controller in discovering mode.
 *
 * There will be no PIN request from the device.
 */

#include <linux/device.h>
#include <linux/hid.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/leds.h>
#include <linux/power_supply.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/input/mt.h>

#include "hid-ids.h"

#define VAIO_RDESC_CONSTANT       BIT(0)
#define SIXAXIS_CONTROLLER_USB    BIT(1)
#define SIXAXIS_CONTROLLER_BT     BIT(2)
#define BUZZ_CONTROLLER           BIT(3)
#define PS3REMOTE                 BIT(4)
#define DUALSHOCK4_CONTROLLER_USB BIT(5)
#define DUALSHOCK4_CONTROLLER_BT  BIT(6)

#define SIXAXIS_CONTROLLER (SIXAXIS_CONTROLLER_USB | SIXAXIS_CONTROLLER_BT)
#define DUALSHOCK4_CONTROLLER (DUALSHOCK4_CONTROLLER_USB |\
				DUALSHOCK4_CONTROLLER_BT)
#define SONY_LED_SUPPORT (SIXAXIS_CONTROLLER | BUZZ_CONTROLLER |\
				DUALSHOCK4_CONTROLLER)
#define SONY_BATTERY_SUPPORT (SIXAXIS_CONTROLLER | DUALSHOCK4_CONTROLLER)
#define SONY_FF_SUPPORT (SIXAXIS_CONTROLLER | DUALSHOCK4_CONTROLLER)

#define MAX_LEDS 4

static const u8 sixaxis_rdesc_fixup[] = {
	0x95, 0x13, 0x09, 0x01, 0x81, 0x02, 0x95, 0x0C,
	0x81, 0x01, 0x75, 0x10, 0x95, 0x04, 0x26, 0xFF,
	0x03, 0x46, 0xFF, 0x03, 0x09, 0x01, 0x81, 0x02
};

static const u8 sixaxis_rdesc_fixup2[] = {
	0x05, 0x01, 0x09, 0x04, 0xa1, 0x01, 0xa1, 0x02,
	0x85, 0x01, 0x75, 0x08, 0x95, 0x01, 0x15, 0x00,
	0x26, 0xff, 0x00, 0x81, 0x03, 0x75, 0x01, 0x95,
	0x13, 0x15, 0x00, 0x25, 0x01, 0x35, 0x00, 0x45,
	0x01, 0x05, 0x09, 0x19, 0x01, 0x29, 0x13, 0x81,
	0x02, 0x75, 0x01, 0x95, 0x0d, 0x06, 0x00, 0xff,
	0x81, 0x03, 0x15, 0x00, 0x26, 0xff, 0x00, 0x05,
	0x01, 0x09, 0x01, 0xa1, 0x00, 0x75, 0x08, 0x95,
	0x04, 0x35, 0x00, 0x46, 0xff, 0x00, 0x09, 0x30,
	0x09, 0x31, 0x09, 0x32, 0x09, 0x35, 0x81, 0x02,
	0xc0, 0x05, 0x01, 0x95, 0x13, 0x09, 0x01, 0x81,
	0x02, 0x95, 0x0c, 0x81, 0x01, 0x75, 0x10, 0x95,
	0x04, 0x26, 0xff, 0x03, 0x46, 0xff, 0x03, 0x09,
	0x01, 0x81, 0x02, 0xc0, 0xa1, 0x02, 0x85, 0x02,
	0x75, 0x08, 0x95, 0x30, 0x09, 0x01, 0xb1, 0x02,
	0xc0, 0xa1, 0x02, 0x85, 0xee, 0x75, 0x08, 0x95,
	0x30, 0x09, 0x01, 0xb1, 0x02, 0xc0, 0xa1, 0x02,
	0x85, 0xef, 0x75, 0x08, 0x95, 0x30, 0x09, 0x01,
	0xb1, 0x02, 0xc0, 0xc0,
};

/*
 * The default descriptor doesn't provide mapping for the accelerometers
 * or orientation sensors.  This fixed descriptor maps the accelerometers
 * to usage values 0x40, 0x41 and 0x42 and maps the orientation sensors
 * to usage values 0x43, 0x44 and 0x45.
 */
static u8 dualshock4_usb_rdesc[] = {
	0x05, 0x01,         /*  Usage Page (Desktop),               */
	0x09, 0x05,         /*  Usage (Gamepad),                    */
	0xA1, 0x01,         /*  Collection (Application),           */
	0x85, 0x01,         /*      Report ID (1),                  */
	0x09, 0x30,         /*      Usage (X),                      */
	0x09, 0x31,         /*      Usage (Y),                      */
	0x09, 0x32,         /*      Usage (Z),                      */
	0x09, 0x35,         /*      Usage (Rz),                     */
	0x15, 0x00,         /*      Logical Minimum (0),            */
	0x26, 0xFF, 0x00,   /*      Logical Maximum (255),          */
	0x75, 0x08,         /*      Report Size (8),                */
	0x95, 0x04,         /*      Report Count (4),               */
	0x81, 0x02,         /*      Input (Variable),               */
	0x09, 0x39,         /*      Usage (Hat Switch),             */
	0x15, 0x00,         /*      Logical Minimum (0),            */
	0x25, 0x07,         /*      Logical Maximum (7),            */
	0x35, 0x00,         /*      Physical Minimum (0),           */
	0x46, 0x3B, 0x01,   /*      Physical Maximum (315),         */
	0x65, 0x14,         /*      Unit (Degrees),                 */
	0x75, 0x04,         /*      Report Size (4),                */
	0x95, 0x01,         /*      Report Count (1),               */
	0x81, 0x42,         /*      Input (Variable, Null State),   */
	0x65, 0x00,         /*      Unit,                           */
	0x05, 0x09,         /*      Usage Page (Button),            */
	0x19, 0x01,         /*      Usage Minimum (01h),            */
	0x29, 0x0E,         /*      Usage Maximum (0Eh),            */
	0x15, 0x00,         /*      Logical Minimum (0),            */
	0x25, 0x01,         /*      Logical Maximum (1),            */
	0x75, 0x01,         /*      Report Size (1),                */
	0x95, 0x0E,         /*      Report Count (14),              */
	0x81, 0x02,         /*      Input (Variable),               */
	0x06, 0x00, 0xFF,   /*      Usage Page (FF00h),             */
	0x09, 0x20,         /*      Usage (20h),                    */
	0x75, 0x06,         /*      Report Size (6),                */
	0x95, 0x01,         /*      Report Count (1),               */
	0x15, 0x00,         /*      Logical Minimum (0),            */
	0x25, 0x7F,         /*      Logical Maximum (127),          */
	0x81, 0x02,         /*      Input (Variable),               */
	0x05, 0x01,         /*      Usage Page (Desktop),           */
	0x09, 0x33,         /*      Usage (Rx),                     */
	0x09, 0x34,         /*      Usage (Ry),                     */
	0x15, 0x00,         /*      Logical Minimum (0),            */
	0x26, 0xFF, 0x00,   /*      Logical Maximum (255),          */
	0x75, 0x08,         /*      Report Size (8),                */
	0x95, 0x02,         /*      Report Count (2),               */
	0x81, 0x02,         /*      Input (Variable),               */
	0x06, 0x00, 0xFF,   /*      Usage Page (FF00h),             */
	0x09, 0x21,         /*      Usage (21h),                    */
	0x95, 0x03,         /*      Report Count (3),               */
	0x81, 0x02,         /*      Input (Variable),               */
	0x05, 0x01,         /*      Usage Page (Desktop),           */
	0x19, 0x40,         /*      Usage Minimum (40h),            */
	0x29, 0x42,         /*      Usage Maximum (42h),            */
	0x16, 0x00, 0x80,   /*      Logical Minimum (-32768),       */
	0x26, 0x00, 0x7F,   /*      Logical Maximum (32767),        */
	0x75, 0x10,         /*      Report Size (16),               */
	0x95, 0x03,         /*      Report Count (3),               */
	0x81, 0x02,         /*      Input (Variable),               */
	0x19, 0x43,         /*      Usage Minimum (43h),            */
	0x29, 0x45,         /*      Usage Maximum (45h),            */
	0x16, 0xFF, 0xBF,   /*      Logical Minimum (-16385),       */
	0x26, 0x00, 0x40,   /*      Logical Maximum (16384),        */
	0x95, 0x03,         /*      Report Count (3),               */
	0x81, 0x02,         /*      Input (Variable),               */
	0x06, 0x00, 0xFF,   /*      Usage Page (FF00h),             */
	0x09, 0x21,         /*      Usage (21h),                    */
	0x15, 0x00,         /*      Logical Minimum (0),            */
	0x25, 0xFF,         /*      Logical Maximum (255),          */
	0x75, 0x08,         /*      Report Size (8),                */
	0x95, 0x27,         /*      Report Count (39),              */
	0x81, 0x02,         /*      Input (Variable),               */
	0x85, 0x05,         /*      Report ID (5),                  */
	0x09, 0x22,         /*      Usage (22h),                    */
	0x95, 0x1F,         /*      Report Count (31),              */
	0x91, 0x02,         /*      Output (Variable),              */
	0x85, 0x04,         /*      Report ID (4),                  */
	0x09, 0x23,         /*      Usage (23h),                    */
	0x95, 0x24,         /*      Report Count (36),              */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0x02,         /*      Report ID (2),                  */
	0x09, 0x24,         /*      Usage (24h),                    */
	0x95, 0x24,         /*      Report Count (36),              */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0x08,         /*      Report ID (8),                  */
	0x09, 0x25,         /*      Usage (25h),                    */
	0x95, 0x03,         /*      Report Count (3),               */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0x10,         /*      Report ID (16),                 */
	0x09, 0x26,         /*      Usage (26h),                    */
	0x95, 0x04,         /*      Report Count (4),               */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0x11,         /*      Report ID (17),                 */
	0x09, 0x27,         /*      Usage (27h),                    */
	0x95, 0x02,         /*      Report Count (2),               */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0x12,         /*      Report ID (18),                 */
	0x06, 0x02, 0xFF,   /*      Usage Page (FF02h),             */
	0x09, 0x21,         /*      Usage (21h),                    */
	0x95, 0x0F,         /*      Report Count (15),              */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0x13,         /*      Report ID (19),                 */
	0x09, 0x22,         /*      Usage (22h),                    */
	0x95, 0x16,         /*      Report Count (22),              */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0x14,         /*      Report ID (20),                 */
	0x06, 0x05, 0xFF,   /*      Usage Page (FF05h),             */
	0x09, 0x20,         /*      Usage (20h),                    */
	0x95, 0x10,         /*      Report Count (16),              */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0x15,         /*      Report ID (21),                 */
	0x09, 0x21,         /*      Usage (21h),                    */
	0x95, 0x2C,         /*      Report Count (44),              */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x06, 0x80, 0xFF,   /*      Usage Page (FF80h),             */
	0x85, 0x80,         /*      Report ID (128),                */
	0x09, 0x20,         /*      Usage (20h),                    */
	0x95, 0x06,         /*      Report Count (6),               */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0x81,         /*      Report ID (129),                */
	0x09, 0x21,         /*      Usage (21h),                    */
	0x95, 0x06,         /*      Report Count (6),               */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0x82,         /*      Report ID (130),                */
	0x09, 0x22,         /*      Usage (22h),                    */
	0x95, 0x05,         /*      Report Count (5),               */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0x83,         /*      Report ID (131),                */
	0x09, 0x23,         /*      Usage (23h),                    */
	0x95, 0x01,         /*      Report Count (1),               */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0x84,         /*      Report ID (132),                */
	0x09, 0x24,         /*      Usage (24h),                    */
	0x95, 0x04,         /*      Report Count (4),               */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0x85,         /*      Report ID (133),                */
	0x09, 0x25,         /*      Usage (25h),                    */
	0x95, 0x06,         /*      Report Count (6),               */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0x86,         /*      Report ID (134),                */
	0x09, 0x26,         /*      Usage (26h),                    */
	0x95, 0x06,         /*      Report Count (6),               */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0x87,         /*      Report ID (135),                */
	0x09, 0x27,         /*      Usage (27h),                    */
	0x95, 0x23,         /*      Report Count (35),              */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0x88,         /*      Report ID (136),                */
	0x09, 0x28,         /*      Usage (28h),                    */
	0x95, 0x22,         /*      Report Count (34),              */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0x89,         /*      Report ID (137),                */
	0x09, 0x29,         /*      Usage (29h),                    */
	0x95, 0x02,         /*      Report Count (2),               */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0x90,         /*      Report ID (144),                */
	0x09, 0x30,         /*      Usage (30h),                    */
	0x95, 0x05,         /*      Report Count (5),               */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0x91,         /*      Report ID (145),                */
	0x09, 0x31,         /*      Usage (31h),                    */
	0x95, 0x03,         /*      Report Count (3),               */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0x92,         /*      Report ID (146),                */
	0x09, 0x32,         /*      Usage (32h),                    */
	0x95, 0x03,         /*      Report Count (3),               */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0x93,         /*      Report ID (147),                */
	0x09, 0x33,         /*      Usage (33h),                    */
	0x95, 0x0C,         /*      Report Count (12),              */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0xA0,         /*      Report ID (160),                */
	0x09, 0x40,         /*      Usage (40h),                    */
	0x95, 0x06,         /*      Report Count (6),               */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0xA1,         /*      Report ID (161),                */
	0x09, 0x41,         /*      Usage (41h),                    */
	0x95, 0x01,         /*      Report Count (1),               */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0xA2,         /*      Report ID (162),                */
	0x09, 0x42,         /*      Usage (42h),                    */
	0x95, 0x01,         /*      Report Count (1),               */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0xA3,         /*      Report ID (163),                */
	0x09, 0x43,         /*      Usage (43h),                    */
	0x95, 0x30,         /*      Report Count (48),              */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0xA4,         /*      Report ID (164),                */
	0x09, 0x44,         /*      Usage (44h),                    */
	0x95, 0x0D,         /*      Report Count (13),              */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0xA5,         /*      Report ID (165),                */
	0x09, 0x45,         /*      Usage (45h),                    */
	0x95, 0x15,         /*      Report Count (21),              */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0xA6,         /*      Report ID (166),                */
	0x09, 0x46,         /*      Usage (46h),                    */
	0x95, 0x15,         /*      Report Count (21),              */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0xF0,         /*      Report ID (240),                */
	0x09, 0x47,         /*      Usage (47h),                    */
	0x95, 0x3F,         /*      Report Count (63),              */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0xF1,         /*      Report ID (241),                */
	0x09, 0x48,         /*      Usage (48h),                    */
	0x95, 0x3F,         /*      Report Count (63),              */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0xF2,         /*      Report ID (242),                */
	0x09, 0x49,         /*      Usage (49h),                    */
	0x95, 0x0F,         /*      Report Count (15),              */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0xA7,         /*      Report ID (167),                */
	0x09, 0x4A,         /*      Usage (4Ah),                    */
	0x95, 0x01,         /*      Report Count (1),               */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0xA8,         /*      Report ID (168),                */
	0x09, 0x4B,         /*      Usage (4Bh),                    */
	0x95, 0x01,         /*      Report Count (1),               */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0xA9,         /*      Report ID (169),                */
	0x09, 0x4C,         /*      Usage (4Ch),                    */
	0x95, 0x08,         /*      Report Count (8),               */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0xAA,         /*      Report ID (170),                */
	0x09, 0x4E,         /*      Usage (4Eh),                    */
	0x95, 0x01,         /*      Report Count (1),               */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0xAB,         /*      Report ID (171),                */
	0x09, 0x4F,         /*      Usage (4Fh),                    */
	0x95, 0x39,         /*      Report Count (57),              */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0xAC,         /*      Report ID (172),                */
	0x09, 0x50,         /*      Usage (50h),                    */
	0x95, 0x39,         /*      Report Count (57),              */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0xAD,         /*      Report ID (173),                */
	0x09, 0x51,         /*      Usage (51h),                    */
	0x95, 0x0B,         /*      Report Count (11),              */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0xAE,         /*      Report ID (174),                */
	0x09, 0x52,         /*      Usage (52h),                    */
	0x95, 0x01,         /*      Report Count (1),               */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0xAF,         /*      Report ID (175),                */
	0x09, 0x53,         /*      Usage (53h),                    */
	0x95, 0x02,         /*      Report Count (2),               */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0xB0,         /*      Report ID (176),                */
	0x09, 0x54,         /*      Usage (54h),                    */
	0x95, 0x3F,         /*      Report Count (63),              */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0xC0                /*  End Collection                      */
};

/*
 * The default behavior of the Dualshock 4 is to send reports using report
 * type 1 when running over Bluetooth. However, as soon as it receives a
 * report of type 17 to set the LEDs or rumble it starts returning it's state
 * in report 17 instead of 1.  Since report 17 is undefined in the default HID
 * descriptor the button and axis definitions must be moved to report 17 or
 * the HID layer won't process the received input once a report is sent.
 */
static u8 dualshock4_bt_rdesc[] = {
	0x05, 0x01,         /*  Usage Page (Desktop),               */
	0x09, 0x05,         /*  Usage (Gamepad),                    */
	0xA1, 0x01,         /*  Collection (Application),           */
	0x85, 0x01,         /*      Report ID (1),                  */
	0x75, 0x08,         /*      Report Size (8),                */
	0x95, 0x0A,         /*      Report Count (9),               */
	0x81, 0x02,         /*      Input (Variable),               */
	0x06, 0x04, 0xFF,   /*      Usage Page (FF04h),             */
	0x85, 0x02,         /*      Report ID (2),                  */
	0x09, 0x24,         /*      Usage (24h),                    */
	0x95, 0x24,         /*      Report Count (36),              */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0xA3,         /*      Report ID (163),                */
	0x09, 0x25,         /*      Usage (25h),                    */
	0x95, 0x30,         /*      Report Count (48),              */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0x05,         /*      Report ID (5),                  */
	0x09, 0x26,         /*      Usage (26h),                    */
	0x95, 0x28,         /*      Report Count (40),              */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0x06,         /*      Report ID (6),                  */
	0x09, 0x27,         /*      Usage (27h),                    */
	0x95, 0x34,         /*      Report Count (52),              */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0x07,         /*      Report ID (7),                  */
	0x09, 0x28,         /*      Usage (28h),                    */
	0x95, 0x30,         /*      Report Count (48),              */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0x08,         /*      Report ID (8),                  */
	0x09, 0x29,         /*      Usage (29h),                    */
	0x95, 0x2F,         /*      Report Count (47),              */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x06, 0x03, 0xFF,   /*      Usage Page (FF03h),             */
	0x85, 0x03,         /*      Report ID (3),                  */
	0x09, 0x21,         /*      Usage (21h),                    */
	0x95, 0x26,         /*      Report Count (38),              */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0x04,         /*      Report ID (4),                  */
	0x09, 0x22,         /*      Usage (22h),                    */
	0x95, 0x2E,         /*      Report Count (46),              */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0xF0,         /*      Report ID (240),                */
	0x09, 0x47,         /*      Usage (47h),                    */
	0x95, 0x3F,         /*      Report Count (63),              */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0xF1,         /*      Report ID (241),                */
	0x09, 0x48,         /*      Usage (48h),                    */
	0x95, 0x3F,         /*      Report Count (63),              */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0xF2,         /*      Report ID (242),                */
	0x09, 0x49,         /*      Usage (49h),                    */
	0x95, 0x0F,         /*      Report Count (15),              */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0x11,         /*      Report ID (17),                 */
	0x06, 0x00, 0xFF,   /*      Usage Page (FF00h),             */
	0x09, 0x20,         /*      Usage (20h),                    */
	0x95, 0x02,         /*      Report Count (2),               */
	0x81, 0x02,         /*      Input (Variable),               */
	0x05, 0x01,         /*      Usage Page (Desktop),           */
	0x09, 0x30,         /*      Usage (X),                      */
	0x09, 0x31,         /*      Usage (Y),                      */
	0x09, 0x32,         /*      Usage (Z),                      */
	0x09, 0x35,         /*      Usage (Rz),                     */
	0x15, 0x00,         /*      Logical Minimum (0),            */
	0x26, 0xFF, 0x00,   /*      Logical Maximum (255),          */
	0x75, 0x08,         /*      Report Size (8),                */
	0x95, 0x04,         /*      Report Count (4),               */
	0x81, 0x02,         /*      Input (Variable),               */
	0x09, 0x39,         /*      Usage (Hat Switch),             */
	0x15, 0x00,         /*      Logical Minimum (0),            */
	0x25, 0x07,         /*      Logical Maximum (7),            */
	0x75, 0x04,         /*      Report Size (4),                */
	0x95, 0x01,         /*      Report Count (1),               */
	0x81, 0x42,         /*      Input (Variable, Null State),   */
	0x05, 0x09,         /*      Usage Page (Button),            */
	0x19, 0x01,         /*      Usage Minimum (01h),            */
	0x29, 0x0E,         /*      Usage Maximum (0Eh),            */
	0x15, 0x00,         /*      Logical Minimum (0),            */
	0x25, 0x01,         /*      Logical Maximum (1),            */
	0x75, 0x01,         /*      Report Size (1),                */
	0x95, 0x0E,         /*      Report Count (14),              */
	0x81, 0x02,         /*      Input (Variable),               */
	0x75, 0x06,         /*      Report Size (6),                */
	0x95, 0x01,         /*      Report Count (1),               */
	0x81, 0x01,         /*      Input (Constant),               */
	0x05, 0x01,         /*      Usage Page (Desktop),           */
	0x09, 0x33,         /*      Usage (Rx),                     */
	0x09, 0x34,         /*      Usage (Ry),                     */
	0x15, 0x00,         /*      Logical Minimum (0),            */
	0x26, 0xFF, 0x00,   /*      Logical Maximum (255),          */
	0x75, 0x08,         /*      Report Size (8),                */
	0x95, 0x02,         /*      Report Count (2),               */
	0x81, 0x02,         /*      Input (Variable),               */
	0x06, 0x00, 0xFF,   /*      Usage Page (FF00h),             */
	0x09, 0x20,         /*      Usage (20h),                    */
	0x95, 0x03,         /*      Report Count (3),               */
	0x81, 0x02,         /*      Input (Variable),               */
	0x05, 0x01,         /*      Usage Page (Desktop),           */
	0x19, 0x40,         /*      Usage Minimum (40h),            */
	0x29, 0x42,         /*      Usage Maximum (42h),            */
	0x16, 0x00, 0x80,   /*      Logical Minimum (-32768),       */
	0x26, 0x00, 0x7F,   /*      Logical Maximum (32767),        */
	0x75, 0x10,         /*      Report Size (16),               */
	0x95, 0x03,         /*      Report Count (3),               */
	0x81, 0x02,         /*      Input (Variable),               */
	0x19, 0x43,         /*      Usage Minimum (43h),            */
	0x29, 0x45,         /*      Usage Maximum (45h),            */
	0x16, 0xFF, 0xBF,   /*      Logical Minimum (-16385),       */
	0x26, 0x00, 0x40,   /*      Logical Maximum (16384),        */
	0x95, 0x03,         /*      Report Count (3),               */
	0x81, 0x02,         /*      Input (Variable),               */
	0x06, 0x00, 0xFF,   /*      Usage Page (FF00h),             */
	0x09, 0x20,         /*      Usage (20h),                    */
	0x15, 0x00,         /*      Logical Minimum (0),            */
	0x26, 0xFF, 0x00,   /*      Logical Maximum (255),          */
	0x75, 0x08,         /*      Report Size (8),                */
	0x95, 0x31,         /*      Report Count (51),              */
	0x81, 0x02,         /*      Input (Variable),               */
	0x09, 0x21,         /*      Usage (21h),                    */
	0x75, 0x08,         /*      Report Size (8),                */
	0x95, 0x4D,         /*      Report Count (77),              */
	0x91, 0x02,         /*      Output (Variable),              */
	0x85, 0x12,         /*      Report ID (18),                 */
	0x09, 0x22,         /*      Usage (22h),                    */
	0x95, 0x8D,         /*      Report Count (141),             */
	0x81, 0x02,         /*      Input (Variable),               */
	0x09, 0x23,         /*      Usage (23h),                    */
	0x91, 0x02,         /*      Output (Variable),              */
	0x85, 0x13,         /*      Report ID (19),                 */
	0x09, 0x24,         /*      Usage (24h),                    */
	0x95, 0xCD,         /*      Report Count (205),             */
	0x81, 0x02,         /*      Input (Variable),               */
	0x09, 0x25,         /*      Usage (25h),                    */
	0x91, 0x02,         /*      Output (Variable),              */
	0x85, 0x14,         /*      Report ID (20),                 */
	0x09, 0x26,         /*      Usage (26h),                    */
	0x96, 0x0D, 0x01,   /*      Report Count (269),             */
	0x81, 0x02,         /*      Input (Variable),               */
	0x09, 0x27,         /*      Usage (27h),                    */
	0x91, 0x02,         /*      Output (Variable),              */
	0x85, 0x15,         /*      Report ID (21),                 */
	0x09, 0x28,         /*      Usage (28h),                    */
	0x96, 0x4D, 0x01,   /*      Report Count (333),             */
	0x81, 0x02,         /*      Input (Variable),               */
	0x09, 0x29,         /*      Usage (29h),                    */
	0x91, 0x02,         /*      Output (Variable),              */
	0x85, 0x16,         /*      Report ID (22),                 */
	0x09, 0x2A,         /*      Usage (2Ah),                    */
	0x96, 0x8D, 0x01,   /*      Report Count (397),             */
	0x81, 0x02,         /*      Input (Variable),               */
	0x09, 0x2B,         /*      Usage (2Bh),                    */
	0x91, 0x02,         /*      Output (Variable),              */
	0x85, 0x17,         /*      Report ID (23),                 */
	0x09, 0x2C,         /*      Usage (2Ch),                    */
	0x96, 0xCD, 0x01,   /*      Report Count (461),             */
	0x81, 0x02,         /*      Input (Variable),               */
	0x09, 0x2D,         /*      Usage (2Dh),                    */
	0x91, 0x02,         /*      Output (Variable),              */
	0x85, 0x18,         /*      Report ID (24),                 */
	0x09, 0x2E,         /*      Usage (2Eh),                    */
	0x96, 0x0D, 0x02,   /*      Report Count (525),             */
	0x81, 0x02,         /*      Input (Variable),               */
	0x09, 0x2F,         /*      Usage (2Fh),                    */
	0x91, 0x02,         /*      Output (Variable),              */
	0x85, 0x19,         /*      Report ID (25),                 */
	0x09, 0x30,         /*      Usage (30h),                    */
	0x96, 0x22, 0x02,   /*      Report Count (546),             */
	0x81, 0x02,         /*      Input (Variable),               */
	0x09, 0x31,         /*      Usage (31h),                    */
	0x91, 0x02,         /*      Output (Variable),              */
	0x06, 0x80, 0xFF,   /*      Usage Page (FF80h),             */
	0x85, 0x82,         /*      Report ID (130),                */
	0x09, 0x22,         /*      Usage (22h),                    */
	0x95, 0x3F,         /*      Report Count (63),              */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0x83,         /*      Report ID (131),                */
	0x09, 0x23,         /*      Usage (23h),                    */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0x84,         /*      Report ID (132),                */
	0x09, 0x24,         /*      Usage (24h),                    */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0x90,         /*      Report ID (144),                */
	0x09, 0x30,         /*      Usage (30h),                    */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0x91,         /*      Report ID (145),                */
	0x09, 0x31,         /*      Usage (31h),                    */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0x92,         /*      Report ID (146),                */
	0x09, 0x32,         /*      Usage (32h),                    */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0x93,         /*      Report ID (147),                */
	0x09, 0x33,         /*      Usage (33h),                    */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0xA0,         /*      Report ID (160),                */
	0x09, 0x40,         /*      Usage (40h),                    */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0x85, 0xA4,         /*      Report ID (164),                */
	0x09, 0x44,         /*      Usage (44h),                    */
	0xB1, 0x02,         /*      Feature (Variable),             */
	0xC0                /*  End Collection                      */
};

static __u8 ps3remote_rdesc[] = {
	0x05, 0x01,          /* GUsagePage Generic Desktop */
	0x09, 0x05,          /* LUsage 0x05 [Game Pad] */
	0xA1, 0x01,          /* MCollection Application (mouse, keyboard) */

	 /* Use collection 1 for joypad buttons */
	 0xA1, 0x02,         /* MCollection Logical (interrelated data) */

	  /* Ignore the 1st byte, maybe it is used for a controller
	   * number but it's not needed for correct operation */
	  0x75, 0x08,        /* GReportSize 0x08 [8] */
	  0x95, 0x01,        /* GReportCount 0x01 [1] */
	  0x81, 0x01,        /* MInput 0x01 (Const[0] Arr[1] Abs[2]) */

	  /* Bytes from 2nd to 4th are a bitmap for joypad buttons, for these
	   * buttons multiple keypresses are allowed */
	  0x05, 0x09,        /* GUsagePage Button */
	  0x19, 0x01,        /* LUsageMinimum 0x01 [Button 1 (primary/trigger)] */
	  0x29, 0x18,        /* LUsageMaximum 0x18 [Button 24] */
	  0x14,              /* GLogicalMinimum [0] */
	  0x25, 0x01,        /* GLogicalMaximum 0x01 [1] */
	  0x75, 0x01,        /* GReportSize 0x01 [1] */
	  0x95, 0x18,        /* GReportCount 0x18 [24] */
	  0x81, 0x02,        /* MInput 0x02 (Data[0] Var[1] Abs[2]) */

	  0xC0,              /* MEndCollection */

	 /* Use collection 2 for remote control buttons */
	 0xA1, 0x02,         /* MCollection Logical (interrelated data) */

	  /* 5th byte is used for remote control buttons */
	  0x05, 0x09,        /* GUsagePage Button */
	  0x18,              /* LUsageMinimum [No button pressed] */
	  0x29, 0xFE,        /* LUsageMaximum 0xFE [Button 254] */
	  0x14,              /* GLogicalMinimum [0] */
	  0x26, 0xFE, 0x00,  /* GLogicalMaximum 0x00FE [254] */
	  0x75, 0x08,        /* GReportSize 0x08 [8] */
	  0x95, 0x01,        /* GReportCount 0x01 [1] */
	  0x80,              /* MInput  */

	  /* Ignore bytes from 6th to 11th, 6th to 10th are always constant at
	   * 0xff and 11th is for press indication */
	  0x75, 0x08,        /* GReportSize 0x08 [8] */
	  0x95, 0x06,        /* GReportCount 0x06 [6] */
	  0x81, 0x01,        /* MInput 0x01 (Const[0] Arr[1] Abs[2]) */

	  /* 12th byte is for battery strength */
	  0x05, 0x06,        /* GUsagePage Generic Device Controls */
	  0x09, 0x20,        /* LUsage 0x20 [Battery Strength] */
	  0x14,              /* GLogicalMinimum [0] */
	  0x25, 0x05,        /* GLogicalMaximum 0x05 [5] */
	  0x75, 0x08,        /* GReportSize 0x08 [8] */
	  0x95, 0x01,        /* GReportCount 0x01 [1] */
	  0x81, 0x02,        /* MInput 0x02 (Data[0] Var[1] Abs[2]) */

	  0xC0,              /* MEndCollection */

	 0xC0                /* MEndCollection [Game Pad] */
};

static const unsigned int ps3remote_keymap_joypad_buttons[] = {
	[0x01] = KEY_SELECT,
	[0x02] = BTN_THUMBL,		/* L3 */
	[0x03] = BTN_THUMBR,		/* R3 */
	[0x04] = BTN_START,
	[0x05] = KEY_UP,
	[0x06] = KEY_RIGHT,
	[0x07] = KEY_DOWN,
	[0x08] = KEY_LEFT,
	[0x09] = BTN_TL2,		/* L2 */
	[0x0a] = BTN_TR2,		/* R2 */
	[0x0b] = BTN_TL,		/* L1 */
	[0x0c] = BTN_TR,		/* R1 */
	[0x0d] = KEY_OPTION,		/* options/triangle */
	[0x0e] = KEY_BACK,		/* back/circle */
	[0x0f] = BTN_0,			/* cross */
	[0x10] = KEY_SCREEN,		/* view/square */
	[0x11] = KEY_HOMEPAGE,		/* PS button */
	[0x14] = KEY_ENTER,
};
static const unsigned int ps3remote_keymap_remote_buttons[] = {
	[0x00] = KEY_1,
	[0x01] = KEY_2,
	[0x02] = KEY_3,
	[0x03] = KEY_4,
	[0x04] = KEY_5,
	[0x05] = KEY_6,
	[0x06] = KEY_7,
	[0x07] = KEY_8,
	[0x08] = KEY_9,
	[0x09] = KEY_0,
	[0x0e] = KEY_ESC,		/* return */
	[0x0f] = KEY_CLEAR,
	[0x16] = KEY_EJECTCD,
	[0x1a] = KEY_MENU,		/* top menu */
	[0x28] = KEY_TIME,
	[0x30] = KEY_PREVIOUS,
	[0x31] = KEY_NEXT,
	[0x32] = KEY_PLAY,
	[0x33] = KEY_REWIND,		/* scan back */
	[0x34] = KEY_FORWARD,		/* scan forward */
	[0x38] = KEY_STOP,
	[0x39] = KEY_PAUSE,
	[0x40] = KEY_CONTEXT_MENU,	/* pop up/menu */
	[0x60] = KEY_FRAMEBACK,		/* slow/step back */
	[0x61] = KEY_FRAMEFORWARD,	/* slow/step forward */
	[0x63] = KEY_SUBTITLE,
	[0x64] = KEY_AUDIO,
	[0x65] = KEY_ANGLE,
	[0x70] = KEY_INFO,		/* display */
	[0x80] = KEY_BLUE,
	[0x81] = KEY_RED,
	[0x82] = KEY_GREEN,
	[0x83] = KEY_YELLOW,
};

static const unsigned int buzz_keymap[] = {
	/*
	 * The controller has 4 remote buzzers, each with one LED and 5
	 * buttons.
	 * 
	 * We use the mapping chosen by the controller, which is:
	 *
	 * Key          Offset
	 * -------------------
	 * Buzz              1
	 * Blue              5
	 * Orange            4
	 * Green             3
	 * Yellow            2
	 *
	 * So, for example, the orange button on the third buzzer is mapped to
	 * BTN_TRIGGER_HAPPY14
	 */
	[ 1] = BTN_TRIGGER_HAPPY1,
	[ 2] = BTN_TRIGGER_HAPPY2,
	[ 3] = BTN_TRIGGER_HAPPY3,
	[ 4] = BTN_TRIGGER_HAPPY4,
	[ 5] = BTN_TRIGGER_HAPPY5,
	[ 6] = BTN_TRIGGER_HAPPY6,
	[ 7] = BTN_TRIGGER_HAPPY7,
	[ 8] = BTN_TRIGGER_HAPPY8,
	[ 9] = BTN_TRIGGER_HAPPY9,
	[10] = BTN_TRIGGER_HAPPY10,
	[11] = BTN_TRIGGER_HAPPY11,
	[12] = BTN_TRIGGER_HAPPY12,
	[13] = BTN_TRIGGER_HAPPY13,
	[14] = BTN_TRIGGER_HAPPY14,
	[15] = BTN_TRIGGER_HAPPY15,
	[16] = BTN_TRIGGER_HAPPY16,
	[17] = BTN_TRIGGER_HAPPY17,
	[18] = BTN_TRIGGER_HAPPY18,
	[19] = BTN_TRIGGER_HAPPY19,
	[20] = BTN_TRIGGER_HAPPY20,
};

static enum power_supply_property sony_battery_props[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_SCOPE,
	POWER_SUPPLY_PROP_STATUS,
};

static spinlock_t sony_dev_list_lock;
static LIST_HEAD(sony_device_list);

struct sony_sc {
	spinlock_t lock;
	struct list_head list_node;
	struct hid_device *hdev;
	struct led_classdev *leds[MAX_LEDS];
	unsigned long quirks;
	struct work_struct state_worker;
	struct power_supply battery;

#ifdef CONFIG_SONY_FF
	__u8 left;
	__u8 right;
#endif

	__u8 mac_address[6];
	__u8 worker_initialized;
	__u8 cable_state;
	__u8 battery_charging;
	__u8 battery_capacity;
	__u8 led_state[MAX_LEDS];
	__u8 led_count;
};

static __u8 *ps3remote_fixup(struct hid_device *hdev, __u8 *rdesc,
			     unsigned int *rsize)
{
	*rsize = sizeof(ps3remote_rdesc);
	return ps3remote_rdesc;
}

static int ps3remote_mapping(struct hid_device *hdev, struct hid_input *hi,
			     struct hid_field *field, struct hid_usage *usage,
			     unsigned long **bit, int *max)
{
	unsigned int key = usage->hid & HID_USAGE;

	if ((usage->hid & HID_USAGE_PAGE) != HID_UP_BUTTON)
		return -1;

	switch (usage->collection_index) {
	case 1:
		if (key >= ARRAY_SIZE(ps3remote_keymap_joypad_buttons))
			return -1;

		key = ps3remote_keymap_joypad_buttons[key];
		if (!key)
			return -1;
		break;
	case 2:
		if (key >= ARRAY_SIZE(ps3remote_keymap_remote_buttons))
			return -1;

		key = ps3remote_keymap_remote_buttons[key];
		if (!key)
			return -1;
		break;
	default:
		return -1;
	}

	hid_map_usage_clear(hi, usage, bit, max, EV_KEY, key);
	return 1;
}


/* Sony Vaio VGX has wrongly mouse pointer declared as constant */
static __u8 *sony_report_fixup(struct hid_device *hdev, __u8 *rdesc,
		unsigned int *rsize)
{
	struct sony_sc *sc = hid_get_drvdata(hdev);

	/*
	 * Some Sony RF receivers wrongly declare the mouse pointer as a
	 * a constant non-data variable.
	 */
	if ((sc->quirks & VAIO_RDESC_CONSTANT) && *rsize >= 56 &&
	    /* usage page: generic desktop controls */
	    /* rdesc[0] == 0x05 && rdesc[1] == 0x01 && */
	    /* usage: mouse */
	    rdesc[2] == 0x09 && rdesc[3] == 0x02 &&
	    /* input (usage page for x,y axes): constant, variable, relative */
	    rdesc[54] == 0x81 && rdesc[55] == 0x07) {
		hid_info(hdev, "Fixing up Sony RF Receiver report descriptor\n");
		/* input: data, variable, relative */
		rdesc[55] = 0x06;
	}

	/*
	 * The default Dualshock 4 USB descriptor doesn't assign
	 * the gyroscope values to corresponding axes so we need a
	 * modified one.
	 */
	if ((sc->quirks & DUALSHOCK4_CONTROLLER_USB) && *rsize == 467) {
		hid_info(hdev, "Using modified Dualshock 4 report descriptor with gyroscope axes\n");
		rdesc = dualshock4_usb_rdesc;
		*rsize = sizeof(dualshock4_usb_rdesc);
	} else if ((sc->quirks & DUALSHOCK4_CONTROLLER_BT) && *rsize == 357) {
		hid_info(hdev, "Using modified Dualshock 4 Bluetooth report descriptor\n");
		rdesc = dualshock4_bt_rdesc;
		*rsize = sizeof(dualshock4_bt_rdesc);
	}

	/* The HID descriptor exposed over BT has a trailing zero byte */
	if ((((sc->quirks & SIXAXIS_CONTROLLER_USB) && *rsize == 148) ||
			((sc->quirks & SIXAXIS_CONTROLLER_BT) && *rsize == 149)) &&
			rdesc[83] == 0x75) {
		hid_info(hdev, "Fixing up Sony Sixaxis report descriptor\n");
		memcpy((void *)&rdesc[83], (void *)&sixaxis_rdesc_fixup,
			sizeof(sixaxis_rdesc_fixup));
	} else if (sc->quirks & SIXAXIS_CONTROLLER_USB &&
		   *rsize > sizeof(sixaxis_rdesc_fixup2)) {
		hid_info(hdev, "Sony Sixaxis clone detected. Using original report descriptor (size: %d clone; %d new)\n",
			 *rsize, (int)sizeof(sixaxis_rdesc_fixup2));
		*rsize = sizeof(sixaxis_rdesc_fixup2);
		memcpy(rdesc, &sixaxis_rdesc_fixup2, *rsize);
	}

	if (sc->quirks & PS3REMOTE)
		return ps3remote_fixup(hdev, rdesc, rsize);

	return rdesc;
}

static void sixaxis_parse_report(struct sony_sc *sc, __u8 *rd, int size)
{
	static const __u8 sixaxis_battery_capacity[] = { 0, 1, 25, 50, 75, 100 };
	unsigned long flags;
	__u8 cable_state, battery_capacity, battery_charging;

	/*
	 * The sixaxis is charging if the battery value is 0xee
	 * and it is fully charged if the value is 0xef.
	 * It does not report the actual level while charging so it
	 * is set to 100% while charging is in progress.
	 */
	if (rd[30] >= 0xee) {
		battery_capacity = 100;
		battery_charging = !(rd[30] & 0x01);
	} else {
		__u8 index = rd[30] <= 5 ? rd[30] : 5;
		battery_capacity = sixaxis_battery_capacity[index];
		battery_charging = 0;
	}
	cable_state = !(rd[31] & 0x04);

	spin_lock_irqsave(&sc->lock, flags);
	sc->cable_state = cable_state;
	sc->battery_capacity = battery_capacity;
	sc->battery_charging = battery_charging;
	spin_unlock_irqrestore(&sc->lock, flags);
}

static void dualshock4_parse_report(struct sony_sc *sc, __u8 *rd, int size)
{
	struct hid_input *hidinput = list_entry(sc->hdev->inputs.next,
						struct hid_input, list);
	struct input_dev *input_dev = hidinput->input;
	unsigned long flags;
	int n, offset;
	__u8 cable_state, battery_capacity, battery_charging;

	/*
	 * Battery and touchpad data starts at byte 30 in the USB report and
	 * 32 in Bluetooth report.
	 */
	offset = (sc->quirks & DUALSHOCK4_CONTROLLER_USB) ? 30 : 32;

	/*
	 * The lower 4 bits of byte 30 contain the battery level
	 * and the 5th bit contains the USB cable state.
	 */
	cable_state = (rd[offset] >> 4) & 0x01;
	battery_capacity = rd[offset] & 0x0F;

	/*
	 * When a USB power source is connected the battery level ranges from
	 * 0 to 10, and when running on battery power it ranges from 0 to 9.
	 * A battery level above 10 when plugged in means charge completed.
	 */
	if (!cable_state || battery_capacity > 10)
		battery_charging = 0;
	else
		battery_charging = 1;

	if (!cable_state)
		battery_capacity++;
	if (battery_capacity > 10)
		battery_capacity = 10;

	battery_capacity *= 10;

	spin_lock_irqsave(&sc->lock, flags);
	sc->cable_state = cable_state;
	sc->battery_capacity = battery_capacity;
	sc->battery_charging = battery_charging;
	spin_unlock_irqrestore(&sc->lock, flags);

	offset += 5;

	/*
	 * The Dualshock 4 multi-touch trackpad data starts at offset 35 on USB
	 * and 37 on Bluetooth.
	 * The first 7 bits of the first byte is a counter and bit 8 is a touch
	 * indicator that is 0 when pressed and 1 when not pressed.
	 * The next 3 bytes are two 12 bit touch coordinates, X and Y.
	 * The data for the second touch is in the same format and immediatly
	 * follows the data for the first.
	 */
	for (n = 0; n < 2; n++) {
		__u16 x, y;

		x = rd[offset+1] | ((rd[offset+2] & 0xF) << 8);
		y = ((rd[offset+2] & 0xF0) >> 4) | (rd[offset+3] << 4);

		input_mt_slot(input_dev, n);
		input_mt_report_slot_state(input_dev, MT_TOOL_FINGER,
					!(rd[offset] >> 7));
		input_report_abs(input_dev, ABS_MT_POSITION_X, x);
		input_report_abs(input_dev, ABS_MT_POSITION_Y, y);

		offset += 4;
	}
}

static int sony_raw_event(struct hid_device *hdev, struct hid_report *report,
		__u8 *rd, int size)
{
	struct sony_sc *sc = hid_get_drvdata(hdev);

	/*
	 * Sixaxis HID report has acclerometers/gyro with MSByte first, this
	 * has to be BYTE_SWAPPED before passing up to joystick interface
	 */
	if ((sc->quirks & SIXAXIS_CONTROLLER) && rd[0] == 0x01 && size == 49) {
		swap(rd[41], rd[42]);
		swap(rd[43], rd[44]);
		swap(rd[45], rd[46]);
		swap(rd[47], rd[48]);

		sixaxis_parse_report(sc, rd, size);
	} else if (((sc->quirks & DUALSHOCK4_CONTROLLER_USB) && rd[0] == 0x01 &&
			size == 64) || ((sc->quirks & DUALSHOCK4_CONTROLLER_BT)
			&& rd[0] == 0x11 && size == 78)) {
		dualshock4_parse_report(sc, rd, size);
	}

	return 0;
}

static int sony_mapping(struct hid_device *hdev, struct hid_input *hi,
			struct hid_field *field, struct hid_usage *usage,
			unsigned long **bit, int *max)
{
	struct sony_sc *sc = hid_get_drvdata(hdev);

	if (sc->quirks & BUZZ_CONTROLLER) {
		unsigned int key = usage->hid & HID_USAGE;

		if ((usage->hid & HID_USAGE_PAGE) != HID_UP_BUTTON)
			return -1;

		switch (usage->collection_index) {
		case 1:
			if (key >= ARRAY_SIZE(buzz_keymap))
				return -1;

			key = buzz_keymap[key];
			if (!key)
				return -1;
			break;
		default:
			return -1;
		}

		hid_map_usage_clear(hi, usage, bit, max, EV_KEY, key);
		return 1;
	}

	if (sc->quirks & PS3REMOTE)
		return ps3remote_mapping(hdev, hi, field, usage, bit, max);

	/* Let hid-core decide for the others */
	return 0;
}

/*
 * Sending HID_REQ_GET_REPORT changes the operation mode of the ps3 controller
 * to "operational".  Without this, the ps3 controller will not report any
 * events.
 */
static int sixaxis_set_operational_usb(struct hid_device *hdev)
{
	int ret;
	char *buf = kmalloc(18, GFP_KERNEL);

	if (!buf)
		return -ENOMEM;

	ret = hid_hw_raw_request(hdev, 0xf2, buf, 17, HID_FEATURE_REPORT,
				 HID_REQ_GET_REPORT);

	if (ret < 0)
		hid_err(hdev, "can't set operational mode\n");

	kfree(buf);

	return ret;
}

static int sixaxis_set_operational_bt(struct hid_device *hdev)
{
	unsigned char buf[] = { 0xf4,  0x42, 0x03, 0x00, 0x00 };
	return hid_hw_raw_request(hdev, buf[0], buf, sizeof(buf),
				  HID_FEATURE_REPORT, HID_REQ_SET_REPORT);
}

/*
 * Requesting feature report 0x02 in Bluetooth mode changes the state of the
 * controller so that it sends full input reports of type 0x11.
 */
static int dualshock4_set_operational_bt(struct hid_device *hdev)
{
	__u8 buf[37] = { 0 };

	return hid_hw_raw_request(hdev, 0x02, buf, sizeof(buf),
				HID_FEATURE_REPORT, HID_REQ_GET_REPORT);
}

static void buzz_set_leds(struct hid_device *hdev, const __u8 *leds)
{
	struct list_head *report_list =
		&hdev->report_enum[HID_OUTPUT_REPORT].report_list;
	struct hid_report *report = list_entry(report_list->next,
		struct hid_report, list);
	__s32 *value = report->field[0]->value;

	value[0] = 0x00;
	value[1] = leds[0] ? 0xff : 0x00;
	value[2] = leds[1] ? 0xff : 0x00;
	value[3] = leds[2] ? 0xff : 0x00;
	value[4] = leds[3] ? 0xff : 0x00;
	value[5] = 0x00;
	value[6] = 0x00;
	hid_hw_request(hdev, report, HID_REQ_SET_REPORT);
}

static void sony_set_leds(struct hid_device *hdev, const __u8 *leds, int count)
{
	struct sony_sc *drv_data = hid_get_drvdata(hdev);
	int n;

	BUG_ON(count > MAX_LEDS);

	if (drv_data->quirks & BUZZ_CONTROLLER && count == 4) {
		buzz_set_leds(hdev, leds);
	} else {
		for (n = 0; n < count; n++)
			drv_data->led_state[n] = leds[n];
		schedule_work(&drv_data->state_worker);
	}
}

static void sony_led_set_brightness(struct led_classdev *led,
				    enum led_brightness value)
{
	struct device *dev = led->dev->parent;
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct sony_sc *drv_data;

	int n;

	drv_data = hid_get_drvdata(hdev);
	if (!drv_data) {
		hid_err(hdev, "No device data\n");
		return;
	}

	for (n = 0; n < drv_data->led_count; n++) {
		if (led == drv_data->leds[n]) {
			if (value != drv_data->led_state[n]) {
				drv_data->led_state[n] = value;
				sony_set_leds(hdev, drv_data->led_state, drv_data->led_count);
			}
			break;
		}
	}
}

static enum led_brightness sony_led_get_brightness(struct led_classdev *led)
{
	struct device *dev = led->dev->parent;
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct sony_sc *drv_data;

	int n;

	drv_data = hid_get_drvdata(hdev);
	if (!drv_data) {
		hid_err(hdev, "No device data\n");
		return LED_OFF;
	}

	for (n = 0; n < drv_data->led_count; n++) {
		if (led == drv_data->leds[n])
			return drv_data->led_state[n];
	}

	return LED_OFF;
}

static void sony_leds_remove(struct hid_device *hdev)
{
	struct sony_sc *drv_data;
	struct led_classdev *led;
	int n;

	drv_data = hid_get_drvdata(hdev);
	BUG_ON(!(drv_data->quirks & SONY_LED_SUPPORT));

	for (n = 0; n < drv_data->led_count; n++) {
		led = drv_data->leds[n];
		drv_data->leds[n] = NULL;
		if (!led)
			continue;
		led_classdev_unregister(led);
		kfree(led);
	}

	drv_data->led_count = 0;
}

static int sony_leds_init(struct hid_device *hdev)
{
	struct sony_sc *drv_data;
	int n, ret = 0;
	int max_brightness;
	int use_colors;
	struct led_classdev *led;
	size_t name_sz;
	char *name;
	size_t name_len;
	const char *name_fmt;
	static const char * const color_str[] = { "red", "green", "blue" };
	static const __u8 initial_values[MAX_LEDS] = { 0x00, 0x00, 0x00, 0x00 };

	drv_data = hid_get_drvdata(hdev);
	BUG_ON(!(drv_data->quirks & SONY_LED_SUPPORT));

	if (drv_data->quirks & BUZZ_CONTROLLER) {
		drv_data->led_count = 4;
		max_brightness = 1;
		use_colors = 0;
		name_len = strlen("::buzz#");
		name_fmt = "%s::buzz%d";
		/* Validate expected report characteristics. */
		if (!hid_validate_values(hdev, HID_OUTPUT_REPORT, 0, 0, 7))
			return -ENODEV;
	} else if (drv_data->quirks & DUALSHOCK4_CONTROLLER) {
		drv_data->led_count = 3;
		max_brightness = 255;
		use_colors = 1;
		name_len = 0;
		name_fmt = "%s:%s";
	} else {
		drv_data->led_count = 4;
		max_brightness = 1;
		use_colors = 0;
		name_len = strlen("::sony#");
		name_fmt = "%s::sony%d";
	}

	/*
	 * Clear LEDs as we have no way of reading their initial state. This is
	 * only relevant if the driver is loaded after somebody actively set the
	 * LEDs to on
	 */
	sony_set_leds(hdev, initial_values, drv_data->led_count);

	name_sz = strlen(dev_name(&hdev->dev)) + name_len + 1;

	for (n = 0; n < drv_data->led_count; n++) {

		if (use_colors)
			name_sz = strlen(dev_name(&hdev->dev)) + strlen(color_str[n]) + 2;

		led = kzalloc(sizeof(struct led_classdev) + name_sz, GFP_KERNEL);
		if (!led) {
			hid_err(hdev, "Couldn't allocate memory for LED %d\n", n);
			ret = -ENOMEM;
			goto error_leds;
		}

		name = (void *)(&led[1]);
		if (use_colors)
			snprintf(name, name_sz, name_fmt, dev_name(&hdev->dev), color_str[n]);
		else
			snprintf(name, name_sz, name_fmt, dev_name(&hdev->dev), n + 1);
		led->name = name;
		led->brightness = 0;
		led->max_brightness = max_brightness;
		led->brightness_get = sony_led_get_brightness;
		led->brightness_set = sony_led_set_brightness;

		ret = led_classdev_register(&hdev->dev, led);
		if (ret) {
			hid_err(hdev, "Failed to register LED %d\n", n);
			kfree(led);
			goto error_leds;
		}

		drv_data->leds[n] = led;
	}

	return ret;

error_leds:
	sony_leds_remove(hdev);

	return ret;
}

static void sixaxis_state_worker(struct work_struct *work)
{
	struct sony_sc *sc = container_of(work, struct sony_sc, state_worker);
	unsigned char buf[] = {
		0x01,
		0x00, 0xff, 0x00, 0xff, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00,
		0xff, 0x27, 0x10, 0x00, 0x32,
		0xff, 0x27, 0x10, 0x00, 0x32,
		0xff, 0x27, 0x10, 0x00, 0x32,
		0xff, 0x27, 0x10, 0x00, 0x32,
		0x00, 0x00, 0x00, 0x00, 0x00
	};

#ifdef CONFIG_SONY_FF
	buf[3] = sc->right ? 1 : 0;
	buf[5] = sc->left;
#endif

	buf[10] |= sc->led_state[0] << 1;
	buf[10] |= sc->led_state[1] << 2;
	buf[10] |= sc->led_state[2] << 3;
	buf[10] |= sc->led_state[3] << 4;

	hid_hw_raw_request(sc->hdev, 0x01, buf, sizeof(buf), HID_OUTPUT_REPORT,
			HID_REQ_SET_REPORT);
}

static void dualshock4_state_worker(struct work_struct *work)
{
	struct sony_sc *sc = container_of(work, struct sony_sc, state_worker);
	struct hid_device *hdev = sc->hdev;
	int offset;

	__u8 buf[78] = { 0 };

	if (sc->quirks & DUALSHOCK4_CONTROLLER_USB) {
		buf[0] = 0x05;
		buf[1] = 0x03;
		offset = 4;
	} else {
		buf[0] = 0x11;
		buf[1] = 0xB0;
		buf[3] = 0x0F;
		offset = 6;
	}

#ifdef CONFIG_SONY_FF
	buf[offset++] = sc->right;
	buf[offset++] = sc->left;
#else
	offset += 2;
#endif

	buf[offset++] = sc->led_state[0];
	buf[offset++] = sc->led_state[1];
	buf[offset++] = sc->led_state[2];

	if (sc->quirks & DUALSHOCK4_CONTROLLER_USB)
		hid_hw_output_report(hdev, buf, 32);
	else
		hid_hw_raw_request(hdev, 0x11, buf, 78,
				HID_OUTPUT_REPORT, HID_REQ_SET_REPORT);
}

#ifdef CONFIG_SONY_FF
static int sony_play_effect(struct input_dev *dev, void *data,
			    struct ff_effect *effect)
{
	struct hid_device *hid = input_get_drvdata(dev);
	struct sony_sc *sc = hid_get_drvdata(hid);

	if (effect->type != FF_RUMBLE)
		return 0;

	sc->left = effect->u.rumble.strong_magnitude / 256;
	sc->right = effect->u.rumble.weak_magnitude / 256;

	schedule_work(&sc->state_worker);
	return 0;
}

static int sony_init_ff(struct hid_device *hdev)
{
	struct hid_input *hidinput = list_entry(hdev->inputs.next,
						struct hid_input, list);
	struct input_dev *input_dev = hidinput->input;

	input_set_capability(input_dev, EV_FF, FF_RUMBLE);
	return input_ff_create_memless(input_dev, NULL, sony_play_effect);
}

#else
static int sony_init_ff(struct hid_device *hdev)
{
	return 0;
}

#endif

static int sony_battery_get_property(struct power_supply *psy,
				     enum power_supply_property psp,
				     union power_supply_propval *val)
{
	struct sony_sc *sc = container_of(psy, struct sony_sc, battery);
	unsigned long flags;
	int ret = 0;
	u8 battery_charging, battery_capacity, cable_state;

	spin_lock_irqsave(&sc->lock, flags);
	battery_charging = sc->battery_charging;
	battery_capacity = sc->battery_capacity;
	cable_state = sc->cable_state;
	spin_unlock_irqrestore(&sc->lock, flags);

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = 1;
		break;
	case POWER_SUPPLY_PROP_SCOPE:
		val->intval = POWER_SUPPLY_SCOPE_DEVICE;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = battery_capacity;
		break;
	case POWER_SUPPLY_PROP_STATUS:
		if (battery_charging)
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
		else
			if (battery_capacity == 100 && cable_state)
				val->intval = POWER_SUPPLY_STATUS_FULL;
			else
				val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int sony_battery_probe(struct sony_sc *sc)
{
	static atomic_t power_id_seq = ATOMIC_INIT(0);
	unsigned long power_id;
	struct hid_device *hdev = sc->hdev;
	int ret;

	/*
	 * Set the default battery level to 100% to avoid low battery warnings
	 * if the battery is polled before the first device report is received.
	 */
	sc->battery_capacity = 100;

	power_id = (unsigned long)atomic_inc_return(&power_id_seq);

	sc->battery.properties = sony_battery_props;
	sc->battery.num_properties = ARRAY_SIZE(sony_battery_props);
	sc->battery.get_property = sony_battery_get_property;
	sc->battery.type = POWER_SUPPLY_TYPE_BATTERY;
	sc->battery.use_for_apm = 0;
	sc->battery.name = kasprintf(GFP_KERNEL, "sony_controller_battery_%lu",
				     power_id);
	if (!sc->battery.name)
		return -ENOMEM;

	ret = power_supply_register(&hdev->dev, &sc->battery);
	if (ret) {
		hid_err(hdev, "Unable to register battery device\n");
		goto err_free;
	}

	power_supply_powers(&sc->battery, &hdev->dev);
	return 0;

err_free:
	kfree(sc->battery.name);
	sc->battery.name = NULL;
	return ret;
}

static void sony_battery_remove(struct sony_sc *sc)
{
	if (!sc->battery.name)
		return;

	power_supply_unregister(&sc->battery);
	kfree(sc->battery.name);
	sc->battery.name = NULL;
}

static int sony_register_touchpad(struct sony_sc *sc, int touch_count,
					int w, int h)
{
	struct hid_input *hidinput = list_entry(sc->hdev->inputs.next,
						struct hid_input, list);
	struct input_dev *input_dev = hidinput->input;
	int ret;

	ret = input_mt_init_slots(input_dev, touch_count, 0);
	if (ret < 0) {
		hid_err(sc->hdev, "Unable to initialize multi-touch slots\n");
		return ret;
	}

	input_set_abs_params(input_dev, ABS_MT_POSITION_X, 0, w, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_POSITION_Y, 0, h, 0, 0);

	return 0;
}

/*
 * If a controller is plugged in via USB while already connected via Bluetooth
 * it will show up as two devices. A global list of connected controllers and
 * their MAC addresses is maintained to ensure that a device is only connected
 * once.
 */
static int sony_check_add_dev_list(struct sony_sc *sc)
{
	struct sony_sc *entry;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&sony_dev_list_lock, flags);

	list_for_each_entry(entry, &sony_device_list, list_node) {
		ret = memcmp(sc->mac_address, entry->mac_address,
				sizeof(sc->mac_address));
		if (!ret) {
			ret = -EEXIST;
			hid_info(sc->hdev, "controller with MAC address %pMR already connected\n",
				sc->mac_address);
			goto unlock;
		}
	}

	ret = 0;
	list_add(&(sc->list_node), &sony_device_list);

unlock:
	spin_unlock_irqrestore(&sony_dev_list_lock, flags);
	return ret;
}

static void sony_remove_dev_list(struct sony_sc *sc)
{
	unsigned long flags;

	if (sc->list_node.next) {
		spin_lock_irqsave(&sony_dev_list_lock, flags);
		list_del(&(sc->list_node));
		spin_unlock_irqrestore(&sony_dev_list_lock, flags);
	}
}

static int sony_get_bt_devaddr(struct sony_sc *sc)
{
	int ret;

	/* HIDP stores the device MAC address as a string in the uniq field. */
	ret = strlen(sc->hdev->uniq);
	if (ret != 17)
		return -EINVAL;

	ret = sscanf(sc->hdev->uniq,
		"%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
		&sc->mac_address[5], &sc->mac_address[4], &sc->mac_address[3],
		&sc->mac_address[2], &sc->mac_address[1], &sc->mac_address[0]);

	if (ret != 6)
		return -EINVAL;

	return 0;
}

static int sony_check_add(struct sony_sc *sc)
{
	int n, ret;

	if ((sc->quirks & DUALSHOCK4_CONTROLLER_BT) ||
	    (sc->quirks & SIXAXIS_CONTROLLER_BT)) {
		/*
		 * sony_get_bt_devaddr() attempts to parse the Bluetooth MAC
		 * address from the uniq string where HIDP stores it.
		 * As uniq cannot be guaranteed to be a MAC address in all cases
		 * a failure of this function should not prevent the connection.
		 */
		if (sony_get_bt_devaddr(sc) < 0) {
			hid_warn(sc->hdev, "UNIQ does not contain a MAC address; duplicate check skipped\n");
			return 0;
		}
	} else if (sc->quirks & DUALSHOCK4_CONTROLLER_USB) {
		__u8 buf[7];

		/*
		 * The MAC address of a DS4 controller connected via USB can be
		 * retrieved with feature report 0x81. The address begins at
		 * offset 1.
		 */
		ret = hid_hw_raw_request(sc->hdev, 0x81, buf, sizeof(buf),
				HID_FEATURE_REPORT, HID_REQ_GET_REPORT);

		if (ret != 7) {
			hid_err(sc->hdev, "failed to retrieve feature report 0x81 with the DualShock 4 MAC address\n");
			return ret < 0 ? ret : -EINVAL;
		}

		memcpy(sc->mac_address, &buf[1], sizeof(sc->mac_address));
	} else if (sc->quirks & SIXAXIS_CONTROLLER_USB) {
		__u8 buf[18];

		/*
		 * The MAC address of a Sixaxis controller connected via USB can
		 * be retrieved with feature report 0xf2. The address begins at
		 * offset 4.
		 */
		ret = hid_hw_raw_request(sc->hdev, 0xf2, buf, sizeof(buf),
				HID_FEATURE_REPORT, HID_REQ_GET_REPORT);

		if (ret != 18) {
			hid_err(sc->hdev, "failed to retrieve feature report 0xf2 with the Sixaxis MAC address\n");
			return ret < 0 ? ret : -EINVAL;
		}

		/*
		 * The Sixaxis device MAC in the report is big-endian and must
		 * be byte-swapped.
		 */
		for (n = 0; n < 6; n++)
			sc->mac_address[5-n] = buf[4+n];
	} else {
		return 0;
	}

	return sony_check_add_dev_list(sc);
}


static int sony_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	int ret;
	unsigned long quirks = id->driver_data;
	struct sony_sc *sc;
	unsigned int connect_mask = HID_CONNECT_DEFAULT;

	sc = devm_kzalloc(&hdev->dev, sizeof(*sc), GFP_KERNEL);
	if (sc == NULL) {
		hid_err(hdev, "can't alloc sony descriptor\n");
		return -ENOMEM;
	}

	sc->quirks = quirks;
	hid_set_drvdata(hdev, sc);
	sc->hdev = hdev;

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "parse failed\n");
		return ret;
	}

	if (sc->quirks & VAIO_RDESC_CONSTANT)
		connect_mask |= HID_CONNECT_HIDDEV_FORCE;
	else if (sc->quirks & SIXAXIS_CONTROLLER_USB)
		connect_mask |= HID_CONNECT_HIDDEV_FORCE;
	else if (sc->quirks & SIXAXIS_CONTROLLER_BT)
		connect_mask |= HID_CONNECT_HIDDEV_FORCE;

	ret = hid_hw_start(hdev, connect_mask);
	if (ret) {
		hid_err(hdev, "hw start failed\n");
		return ret;
	}

	if (sc->quirks & SIXAXIS_CONTROLLER_USB) {
		/*
		 * The Sony Sixaxis does not handle HID Output Reports on the
		 * Interrupt EP like it could, so we need to force HID Output
		 * Reports to use HID_REQ_SET_REPORT on the Control EP.
		 *
		 * There is also another issue about HID Output Reports via USB,
		 * the Sixaxis does not want the report_id as part of the data
		 * packet, so we have to discard buf[0] when sending the actual
		 * control message, even for numbered reports, humpf!
		 */
		hdev->quirks |= HID_QUIRK_NO_OUTPUT_REPORTS_ON_INTR_EP;
		hdev->quirks |= HID_QUIRK_SKIP_OUTPUT_REPORT_ID;
		ret = sixaxis_set_operational_usb(hdev);
		sc->worker_initialized = 1;
		INIT_WORK(&sc->state_worker, sixaxis_state_worker);
	} else if (sc->quirks & SIXAXIS_CONTROLLER_BT) {
		/*
		 * The Sixaxis wants output reports sent on the ctrl endpoint
		 * when connected via Bluetooth.
		 */
		hdev->quirks |= HID_QUIRK_NO_OUTPUT_REPORTS_ON_INTR_EP;
		ret = sixaxis_set_operational_bt(hdev);
		sc->worker_initialized = 1;
		INIT_WORK(&sc->state_worker, sixaxis_state_worker);
	} else if (sc->quirks & DUALSHOCK4_CONTROLLER) {
		if (sc->quirks & DUALSHOCK4_CONTROLLER_BT) {
			/*
			 * The DualShock 4 wants output reports sent on the ctrl
			 * endpoint when connected via Bluetooth.
			 */
			hdev->quirks |= HID_QUIRK_NO_OUTPUT_REPORTS_ON_INTR_EP;
			ret = dualshock4_set_operational_bt(hdev);
			if (ret < 0) {
				hid_err(hdev, "failed to set the Dualshock 4 operational mode\n");
				goto err_stop;
			}
		}
		/*
		 * The Dualshock 4 touchpad supports 2 touches and has a
		 * resolution of 1920x940.
		 */
		ret = sony_register_touchpad(sc, 2, 1920, 940);
		if (ret < 0)
			goto err_stop;

		sc->worker_initialized = 1;
		INIT_WORK(&sc->state_worker, dualshock4_state_worker);
	} else {
		ret = 0;
	}

	if (ret < 0)
		goto err_stop;

	ret = sony_check_add(sc);
	if (ret < 0)
		goto err_stop;

	if (sc->quirks & SONY_LED_SUPPORT) {
		ret = sony_leds_init(hdev);
		if (ret < 0)
			goto err_stop;
	}

	if (sc->quirks & SONY_BATTERY_SUPPORT) {
		ret = sony_battery_probe(sc);
		if (ret < 0)
			goto err_stop;

		/* Open the device to receive reports with battery info */
		ret = hid_hw_open(hdev);
		if (ret < 0) {
			hid_err(hdev, "hw open failed\n");
			goto err_stop;
		}
	}

	if (sc->quirks & SONY_FF_SUPPORT) {
		ret = sony_init_ff(hdev);
		if (ret < 0)
			goto err_close;
	}

	return 0;
err_close:
	hid_hw_close(hdev);
err_stop:
	if (sc->quirks & SONY_LED_SUPPORT)
		sony_leds_remove(hdev);
	if (sc->quirks & SONY_BATTERY_SUPPORT)
		sony_battery_remove(sc);
	if (sc->worker_initialized)
		cancel_work_sync(&sc->state_worker);
	sony_remove_dev_list(sc);
	hid_hw_stop(hdev);
	return ret;
}

static void sony_remove(struct hid_device *hdev)
{
	struct sony_sc *sc = hid_get_drvdata(hdev);

	if (sc->quirks & SONY_LED_SUPPORT)
		sony_leds_remove(hdev);

	if (sc->quirks & SONY_BATTERY_SUPPORT) {
		hid_hw_close(hdev);
		sony_battery_remove(sc);
	}

	if (sc->worker_initialized)
		cancel_work_sync(&sc->state_worker);

	sony_remove_dev_list(sc);

	hid_hw_stop(hdev);
}

static const struct hid_device_id sony_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_SONY, USB_DEVICE_ID_SONY_PS3_CONTROLLER),
		.driver_data = SIXAXIS_CONTROLLER_USB },
	{ HID_USB_DEVICE(USB_VENDOR_ID_SONY, USB_DEVICE_ID_SONY_NAVIGATION_CONTROLLER),
		.driver_data = SIXAXIS_CONTROLLER_USB },
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_SONY, USB_DEVICE_ID_SONY_PS3_CONTROLLER),
		.driver_data = SIXAXIS_CONTROLLER_BT },
	{ HID_USB_DEVICE(USB_VENDOR_ID_SONY, USB_DEVICE_ID_SONY_VAIO_VGX_MOUSE),
		.driver_data = VAIO_RDESC_CONSTANT },
	{ HID_USB_DEVICE(USB_VENDOR_ID_SONY, USB_DEVICE_ID_SONY_VAIO_VGP_MOUSE),
		.driver_data = VAIO_RDESC_CONSTANT },
	/* Wired Buzz Controller. Reported as Sony Hub from its USB ID and as
	 * Logitech joystick from the device descriptor. */
	{ HID_USB_DEVICE(USB_VENDOR_ID_SONY, USB_DEVICE_ID_SONY_BUZZ_CONTROLLER),
		.driver_data = BUZZ_CONTROLLER },
	{ HID_USB_DEVICE(USB_VENDOR_ID_SONY, USB_DEVICE_ID_SONY_WIRELESS_BUZZ_CONTROLLER),
		.driver_data = BUZZ_CONTROLLER },
	/* PS3 BD Remote Control */
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_SONY, USB_DEVICE_ID_SONY_PS3_BDREMOTE),
		.driver_data = PS3REMOTE },
	/* Logitech Harmony Adapter for PS3 */
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_LOGITECH_HARMONY_PS3),
		.driver_data = PS3REMOTE },
	/* Sony Dualshock 4 controllers for PS4 */
	{ HID_USB_DEVICE(USB_VENDOR_ID_SONY, USB_DEVICE_ID_SONY_PS4_CONTROLLER),
		.driver_data = DUALSHOCK4_CONTROLLER_USB },
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_SONY, USB_DEVICE_ID_SONY_PS4_CONTROLLER),
		.driver_data = DUALSHOCK4_CONTROLLER_BT },
	{ }
};
MODULE_DEVICE_TABLE(hid, sony_devices);

static struct hid_driver sony_driver = {
	.name          = "sony",
	.id_table      = sony_devices,
	.input_mapping = sony_mapping,
	.probe         = sony_probe,
	.remove        = sony_remove,
	.report_fixup  = sony_report_fixup,
	.raw_event     = sony_raw_event
};
module_hid_driver(sony_driver);

MODULE_LICENSE("GPL");
