/*
 *  HID driver for UC-Logic devices not fully compliant with HID standard
 *
 *  Copyright (c) 2010 Nikolai Kondrashov
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
#include <linux/usb.h>

#include "hid-ids.h"

/*
 * See WPXXXXU model descriptions, device and HID report descriptors at
 * http://sf.net/apps/mediawiki/digimend/?title=UC-Logic_Tablet_WP4030U
 * http://sf.net/apps/mediawiki/digimend/?title=UC-Logic_Tablet_WP5540U
 * http://sf.net/apps/mediawiki/digimend/?title=UC-Logic_Tablet_WP8060U
 */

/* Size of the original descriptor of WPXXXXU tablets */
#define WPXXXXU_RDESC_ORIG_SIZE	212

/* Fixed WP4030U report descriptor */
static __u8 wp4030u_rdesc_fixed[] = {
	0x05, 0x0D,         /*  Usage Page (Digitizer),             */
	0x09, 0x02,         /*  Usage (Pen),                        */
	0xA1, 0x01,         /*  Collection (Application),           */
	0x85, 0x09,         /*      Report ID (9),                  */
	0x09, 0x20,         /*      Usage (Stylus),                 */
	0xA0,               /*      Collection (Physical),          */
	0x75, 0x01,         /*          Report Size (1),            */
	0x09, 0x42,         /*          Usage (Tip Switch),         */
	0x09, 0x44,         /*          Usage (Barrel Switch),      */
	0x09, 0x46,         /*          Usage (Tablet Pick),        */
	0x14,               /*          Logical Minimum (0),        */
	0x25, 0x01,         /*          Logical Maximum (1),        */
	0x95, 0x03,         /*          Report Count (3),           */
	0x81, 0x02,         /*          Input (Variable),           */
	0x95, 0x05,         /*          Report Count (5),           */
	0x81, 0x01,         /*          Input (Constant),           */
	0x75, 0x10,         /*          Report Size (16),           */
	0x95, 0x01,         /*          Report Count (1),           */
	0x14,               /*          Logical Minimum (0),        */
	0xA4,               /*          Push,                       */
	0x05, 0x01,         /*          Usage Page (Desktop),       */
	0x55, 0xFD,         /*          Unit Exponent (-3),         */
	0x65, 0x13,         /*          Unit (Inch),                */
	0x34,               /*          Physical Minimum (0),       */
	0x09, 0x30,         /*          Usage (X),                  */
	0x46, 0xA0, 0x0F,   /*          Physical Maximum (4000),    */
	0x26, 0xFF, 0x7F,   /*          Logical Maximum (32767),    */
	0x81, 0x02,         /*          Input (Variable),           */
	0x09, 0x31,         /*          Usage (Y),                  */
	0x46, 0xB8, 0x0B,   /*          Physical Maximum (3000),    */
	0x26, 0xFF, 0x7F,   /*          Logical Maximum (32767),    */
	0x81, 0x02,         /*          Input (Variable),           */
	0xB4,               /*          Pop,                        */
	0x09, 0x30,         /*          Usage (Tip Pressure),       */
	0x26, 0xFF, 0x03,   /*          Logical Maximum (1023),     */
	0x81, 0x02,         /*          Input (Variable),           */
	0xC0,               /*      End Collection,                 */
	0xC0                /*  End Collection                      */
};

/* Fixed WP5540U report descriptor */
static __u8 wp5540u_rdesc_fixed[] = {
	0x05, 0x0D,         /*  Usage Page (Digitizer),             */
	0x09, 0x02,         /*  Usage (Pen),                        */
	0xA1, 0x01,         /*  Collection (Application),           */
	0x85, 0x09,         /*      Report ID (9),                  */
	0x09, 0x20,         /*      Usage (Stylus),                 */
	0xA0,               /*      Collection (Physical),          */
	0x75, 0x01,         /*          Report Size (1),            */
	0x09, 0x42,         /*          Usage (Tip Switch),         */
	0x09, 0x44,         /*          Usage (Barrel Switch),      */
	0x09, 0x46,         /*          Usage (Tablet Pick),        */
	0x14,               /*          Logical Minimum (0),        */
	0x25, 0x01,         /*          Logical Maximum (1),        */
	0x95, 0x03,         /*          Report Count (3),           */
	0x81, 0x02,         /*          Input (Variable),           */
	0x95, 0x05,         /*          Report Count (5),           */
	0x81, 0x01,         /*          Input (Constant),           */
	0x75, 0x10,         /*          Report Size (16),           */
	0x95, 0x01,         /*          Report Count (1),           */
	0x14,               /*          Logical Minimum (0),        */
	0xA4,               /*          Push,                       */
	0x05, 0x01,         /*          Usage Page (Desktop),       */
	0x55, 0xFD,         /*          Unit Exponent (-3),         */
	0x65, 0x13,         /*          Unit (Inch),                */
	0x34,               /*          Physical Minimum (0),       */
	0x09, 0x30,         /*          Usage (X),                  */
	0x46, 0x7C, 0x15,   /*          Physical Maximum (5500),    */
	0x26, 0xFF, 0x7F,   /*          Logical Maximum (32767),    */
	0x81, 0x02,         /*          Input (Variable),           */
	0x09, 0x31,         /*          Usage (Y),                  */
	0x46, 0xA0, 0x0F,   /*          Physical Maximum (4000),    */
	0x26, 0xFF, 0x7F,   /*          Logical Maximum (32767),    */
	0x81, 0x02,         /*          Input (Variable),           */
	0xB4,               /*          Pop,                        */
	0x09, 0x30,         /*          Usage (Tip Pressure),       */
	0x26, 0xFF, 0x03,   /*          Logical Maximum (1023),     */
	0x81, 0x02,         /*          Input (Variable),           */
	0xC0,               /*      End Collection,                 */
	0xC0,               /*  End Collection,                     */
	0x05, 0x01,         /*  Usage Page (Desktop),               */
	0x09, 0x02,         /*  Usage (Mouse),                      */
	0xA1, 0x01,         /*  Collection (Application),           */
	0x85, 0x08,         /*      Report ID (8),                  */
	0x09, 0x01,         /*      Usage (Pointer),                */
	0xA0,               /*      Collection (Physical),          */
	0x75, 0x01,         /*          Report Size (1),            */
	0x05, 0x09,         /*          Usage Page (Button),        */
	0x19, 0x01,         /*          Usage Minimum (01h),        */
	0x29, 0x03,         /*          Usage Maximum (03h),        */
	0x14,               /*          Logical Minimum (0),        */
	0x25, 0x01,         /*          Logical Maximum (1),        */
	0x95, 0x03,         /*          Report Count (3),           */
	0x81, 0x02,         /*          Input (Variable),           */
	0x95, 0x05,         /*          Report Count (5),           */
	0x81, 0x01,         /*          Input (Constant),           */
	0x05, 0x01,         /*          Usage Page (Desktop),       */
	0x75, 0x08,         /*          Report Size (8),            */
	0x09, 0x30,         /*          Usage (X),                  */
	0x09, 0x31,         /*          Usage (Y),                  */
	0x15, 0x81,         /*          Logical Minimum (-127),     */
	0x25, 0x7F,         /*          Logical Maximum (127),      */
	0x95, 0x02,         /*          Report Count (2),           */
	0x81, 0x06,         /*          Input (Variable, Relative), */
	0x09, 0x38,         /*          Usage (Wheel),              */
	0x15, 0xFF,         /*          Logical Minimum (-1),       */
	0x25, 0x01,         /*          Logical Maximum (1),        */
	0x95, 0x01,         /*          Report Count (1),           */
	0x81, 0x06,         /*          Input (Variable, Relative), */
	0x81, 0x01,         /*          Input (Constant),           */
	0xC0,               /*      End Collection,                 */
	0xC0                /*  End Collection                      */
};

/* Fixed WP8060U report descriptor */
static __u8 wp8060u_rdesc_fixed[] = {
	0x05, 0x0D,         /*  Usage Page (Digitizer),             */
	0x09, 0x02,         /*  Usage (Pen),                        */
	0xA1, 0x01,         /*  Collection (Application),           */
	0x85, 0x09,         /*      Report ID (9),                  */
	0x09, 0x20,         /*      Usage (Stylus),                 */
	0xA0,               /*      Collection (Physical),          */
	0x75, 0x01,         /*          Report Size (1),            */
	0x09, 0x42,         /*          Usage (Tip Switch),         */
	0x09, 0x44,         /*          Usage (Barrel Switch),      */
	0x09, 0x46,         /*          Usage (Tablet Pick),        */
	0x14,               /*          Logical Minimum (0),        */
	0x25, 0x01,         /*          Logical Maximum (1),        */
	0x95, 0x03,         /*          Report Count (3),           */
	0x81, 0x02,         /*          Input (Variable),           */
	0x95, 0x05,         /*          Report Count (5),           */
	0x81, 0x01,         /*          Input (Constant),           */
	0x75, 0x10,         /*          Report Size (16),           */
	0x95, 0x01,         /*          Report Count (1),           */
	0x14,               /*          Logical Minimum (0),        */
	0xA4,               /*          Push,                       */
	0x05, 0x01,         /*          Usage Page (Desktop),       */
	0x55, 0xFD,         /*          Unit Exponent (-3),         */
	0x65, 0x13,         /*          Unit (Inch),                */
	0x34,               /*          Physical Minimum (0),       */
	0x09, 0x30,         /*          Usage (X),                  */
	0x46, 0x40, 0x1F,   /*          Physical Maximum (8000),    */
	0x26, 0xFF, 0x7F,   /*          Logical Maximum (32767),    */
	0x81, 0x02,         /*          Input (Variable),           */
	0x09, 0x31,         /*          Usage (Y),                  */
	0x46, 0x70, 0x17,   /*          Physical Maximum (6000),    */
	0x26, 0xFF, 0x7F,   /*          Logical Maximum (32767),    */
	0x81, 0x02,         /*          Input (Variable),           */
	0xB4,               /*          Pop,                        */
	0x09, 0x30,         /*          Usage (Tip Pressure),       */
	0x26, 0xFF, 0x03,   /*          Logical Maximum (1023),     */
	0x81, 0x02,         /*          Input (Variable),           */
	0xC0,               /*      End Collection,                 */
	0xC0,               /*  End Collection,                     */
	0x05, 0x01,         /*  Usage Page (Desktop),               */
	0x09, 0x02,         /*  Usage (Mouse),                      */
	0xA1, 0x01,         /*  Collection (Application),           */
	0x85, 0x08,         /*      Report ID (8),                  */
	0x09, 0x01,         /*      Usage (Pointer),                */
	0xA0,               /*      Collection (Physical),          */
	0x75, 0x01,         /*          Report Size (1),            */
	0x05, 0x09,         /*          Usage Page (Button),        */
	0x19, 0x01,         /*          Usage Minimum (01h),        */
	0x29, 0x03,         /*          Usage Maximum (03h),        */
	0x14,               /*          Logical Minimum (0),        */
	0x25, 0x01,         /*          Logical Maximum (1),        */
	0x95, 0x03,         /*          Report Count (3),           */
	0x81, 0x02,         /*          Input (Variable),           */
	0x95, 0x05,         /*          Report Count (5),           */
	0x81, 0x01,         /*          Input (Constant),           */
	0x05, 0x01,         /*          Usage Page (Desktop),       */
	0x75, 0x08,         /*          Report Size (8),            */
	0x09, 0x30,         /*          Usage (X),                  */
	0x09, 0x31,         /*          Usage (Y),                  */
	0x15, 0x81,         /*          Logical Minimum (-127),     */
	0x25, 0x7F,         /*          Logical Maximum (127),      */
	0x95, 0x02,         /*          Report Count (2),           */
	0x81, 0x06,         /*          Input (Variable, Relative), */
	0x09, 0x38,         /*          Usage (Wheel),              */
	0x15, 0xFF,         /*          Logical Minimum (-1),       */
	0x25, 0x01,         /*          Logical Maximum (1),        */
	0x95, 0x01,         /*          Report Count (1),           */
	0x81, 0x06,         /*          Input (Variable, Relative), */
	0x81, 0x01,         /*          Input (Constant),           */
	0xC0,               /*      End Collection,                 */
	0xC0                /*  End Collection                      */
};

/*
 * See WP1062 description, device and HID report descriptors at
 * http://sf.net/apps/mediawiki/digimend/?title=UC-Logic_Tablet_WP1062
 */

/* Size of the original descriptor of WP1062 tablet */
#define WP1062_RDESC_ORIG_SIZE	254

/* Fixed WP1062 report descriptor */
static __u8 wp1062_rdesc_fixed[] = {
	0x05, 0x0D,         /*  Usage Page (Digitizer),             */
	0x09, 0x02,         /*  Usage (Pen),                        */
	0xA1, 0x01,         /*  Collection (Application),           */
	0x85, 0x09,         /*      Report ID (9),                  */
	0x09, 0x20,         /*      Usage (Stylus),                 */
	0xA0,               /*      Collection (Physical),          */
	0x75, 0x01,         /*          Report Size (1),            */
	0x09, 0x42,         /*          Usage (Tip Switch),         */
	0x09, 0x44,         /*          Usage (Barrel Switch),      */
	0x09, 0x46,         /*          Usage (Tablet Pick),        */
	0x14,               /*          Logical Minimum (0),        */
	0x25, 0x01,         /*          Logical Maximum (1),        */
	0x95, 0x03,         /*          Report Count (3),           */
	0x81, 0x02,         /*          Input (Variable),           */
	0x95, 0x04,         /*          Report Count (4),           */
	0x81, 0x01,         /*          Input (Constant),           */
	0x09, 0x32,         /*          Usage (In Range),           */
	0x95, 0x01,         /*          Report Count (1),           */
	0x81, 0x02,         /*          Input (Variable),           */
	0x75, 0x10,         /*          Report Size (16),           */
	0x95, 0x01,         /*          Report Count (1),           */
	0x14,               /*          Logical Minimum (0),        */
	0xA4,               /*          Push,                       */
	0x05, 0x01,         /*          Usage Page (Desktop),       */
	0x55, 0xFD,         /*          Unit Exponent (-3),         */
	0x65, 0x13,         /*          Unit (Inch),                */
	0x34,               /*          Physical Minimum (0),       */
	0x09, 0x30,         /*          Usage (X),                  */
	0x46, 0x10, 0x27,   /*          Physical Maximum (10000),   */
	0x26, 0x20, 0x4E,   /*          Logical Maximum (20000),    */
	0x81, 0x02,         /*          Input (Variable),           */
	0x09, 0x31,         /*          Usage (Y),                  */
	0x46, 0xB7, 0x19,   /*          Physical Maximum (6583),    */
	0x26, 0x6E, 0x33,   /*          Logical Maximum (13166),    */
	0x81, 0x02,         /*          Input (Variable),           */
	0xB4,               /*          Pop,                        */
	0x09, 0x30,         /*          Usage (Tip Pressure),       */
	0x26, 0xFF, 0x03,   /*          Logical Maximum (1023),     */
	0x81, 0x02,         /*          Input (Variable),           */
	0xC0,               /*      End Collection,                 */
	0xC0                /*  End Collection                      */
};

/*
 * See PF1209 description, device and HID report descriptors at
 * http://sf.net/apps/mediawiki/digimend/?title=UC-Logic_Tablet_PF1209
 */

/* Size of the original descriptor of PF1209 tablet */
#define PF1209_RDESC_ORIG_SIZE	234

/* Fixed PF1209 report descriptor */
static __u8 pf1209_rdesc_fixed[] = {
	0x05, 0x0D,         /*  Usage Page (Digitizer),             */
	0x09, 0x02,         /*  Usage (Pen),                        */
	0xA1, 0x01,         /*  Collection (Application),           */
	0x85, 0x09,         /*      Report ID (9),                  */
	0x09, 0x20,         /*      Usage (Stylus),                 */
	0xA0,               /*      Collection (Physical),          */
	0x75, 0x01,         /*          Report Size (1),            */
	0x09, 0x42,         /*          Usage (Tip Switch),         */
	0x09, 0x44,         /*          Usage (Barrel Switch),      */
	0x09, 0x46,         /*          Usage (Tablet Pick),        */
	0x14,               /*          Logical Minimum (0),        */
	0x25, 0x01,         /*          Logical Maximum (1),        */
	0x95, 0x03,         /*          Report Count (3),           */
	0x81, 0x02,         /*          Input (Variable),           */
	0x95, 0x05,         /*          Report Count (5),           */
	0x81, 0x01,         /*          Input (Constant),           */
	0x75, 0x10,         /*          Report Size (16),           */
	0x95, 0x01,         /*          Report Count (1),           */
	0x14,               /*          Logical Minimum (0),        */
	0xA4,               /*          Push,                       */
	0x05, 0x01,         /*          Usage Page (Desktop),       */
	0x55, 0xFD,         /*          Unit Exponent (-3),         */
	0x65, 0x13,         /*          Unit (Inch),                */
	0x34,               /*          Physical Minimum (0),       */
	0x09, 0x30,         /*          Usage (X),                  */
	0x46, 0xE0, 0x2E,   /*          Physical Maximum (12000),   */
	0x26, 0xFF, 0x7F,   /*          Logical Maximum (32767),    */
	0x81, 0x02,         /*          Input (Variable),           */
	0x09, 0x31,         /*          Usage (Y),                  */
	0x46, 0x28, 0x23,   /*          Physical Maximum (9000),    */
	0x26, 0xFF, 0x7F,   /*          Logical Maximum (32767),    */
	0x81, 0x02,         /*          Input (Variable),           */
	0xB4,               /*          Pop,                        */
	0x09, 0x30,         /*          Usage (Tip Pressure),       */
	0x26, 0xFF, 0x03,   /*          Logical Maximum (1023),     */
	0x81, 0x02,         /*          Input (Variable),           */
	0xC0,               /*      End Collection,                 */
	0xC0,               /*  End Collection,                     */
	0x05, 0x01,         /*  Usage Page (Desktop),               */
	0x09, 0x02,         /*  Usage (Mouse),                      */
	0xA1, 0x01,         /*  Collection (Application),           */
	0x85, 0x08,         /*      Report ID (8),                  */
	0x09, 0x01,         /*      Usage (Pointer),                */
	0xA0,               /*      Collection (Physical),          */
	0x75, 0x01,         /*          Report Size (1),            */
	0x05, 0x09,         /*          Usage Page (Button),        */
	0x19, 0x01,         /*          Usage Minimum (01h),        */
	0x29, 0x03,         /*          Usage Maximum (03h),        */
	0x14,               /*          Logical Minimum (0),        */
	0x25, 0x01,         /*          Logical Maximum (1),        */
	0x95, 0x03,         /*          Report Count (3),           */
	0x81, 0x02,         /*          Input (Variable),           */
	0x95, 0x05,         /*          Report Count (5),           */
	0x81, 0x01,         /*          Input (Constant),           */
	0x05, 0x01,         /*          Usage Page (Desktop),       */
	0x75, 0x08,         /*          Report Size (8),            */
	0x09, 0x30,         /*          Usage (X),                  */
	0x09, 0x31,         /*          Usage (Y),                  */
	0x15, 0x81,         /*          Logical Minimum (-127),     */
	0x25, 0x7F,         /*          Logical Maximum (127),      */
	0x95, 0x02,         /*          Report Count (2),           */
	0x81, 0x06,         /*          Input (Variable, Relative), */
	0x09, 0x38,         /*          Usage (Wheel),              */
	0x15, 0xFF,         /*          Logical Minimum (-1),       */
	0x25, 0x01,         /*          Logical Maximum (1),        */
	0x95, 0x01,         /*          Report Count (1),           */
	0x81, 0x06,         /*          Input (Variable, Relative), */
	0x81, 0x01,         /*          Input (Constant),           */
	0xC0,               /*      End Collection,                 */
	0xC0                /*  End Collection                      */
};

/*
 * See TWHL850 description, device and HID report descriptors at
 * http://sf.net/apps/mediawiki/digimend/?title=UC-Logic_Wireless_Tablet_TWHL850
 */

/* Size of the original descriptors of TWHL850 tablet */
#define TWHL850_RDESC_ORIG_SIZE0	182
#define TWHL850_RDESC_ORIG_SIZE1	161
#define TWHL850_RDESC_ORIG_SIZE2	92

/* Fixed PID 0522 tablet report descriptor, interface 0 (stylus) */
static __u8 twhl850_rdesc_fixed0[] = {
	0x05, 0x0D,         /*  Usage Page (Digitizer),             */
	0x09, 0x02,         /*  Usage (Pen),                        */
	0xA1, 0x01,         /*  Collection (Application),           */
	0x85, 0x09,         /*      Report ID (9),                  */
	0x09, 0x20,         /*      Usage (Stylus),                 */
	0xA0,               /*      Collection (Physical),          */
	0x14,               /*          Logical Minimum (0),        */
	0x25, 0x01,         /*          Logical Maximum (1),        */
	0x75, 0x01,         /*          Report Size (1),            */
	0x95, 0x03,         /*          Report Count (3),           */
	0x09, 0x42,         /*          Usage (Tip Switch),         */
	0x09, 0x44,         /*          Usage (Barrel Switch),      */
	0x09, 0x46,         /*          Usage (Tablet Pick),        */
	0x81, 0x02,         /*          Input (Variable),           */
	0x81, 0x03,         /*          Input (Constant, Variable), */
	0x95, 0x01,         /*          Report Count (1),           */
	0x09, 0x32,         /*          Usage (In Range),           */
	0x81, 0x02,         /*          Input (Variable),           */
	0x81, 0x03,         /*          Input (Constant, Variable), */
	0x75, 0x10,         /*          Report Size (16),           */
	0xA4,               /*          Push,                       */
	0x05, 0x01,         /*          Usage Page (Desktop),       */
	0x65, 0x13,         /*          Unit (Inch),                */
	0x55, 0xFD,         /*          Unit Exponent (-3),         */
	0x34,               /*          Physical Minimum (0),       */
	0x09, 0x30,         /*          Usage (X),                  */
	0x46, 0x40, 0x1F,   /*          Physical Maximum (8000),    */
	0x26, 0x00, 0x7D,   /*          Logical Maximum (32000),    */
	0x81, 0x02,         /*          Input (Variable),           */
	0x09, 0x31,         /*          Usage (Y),                  */
	0x46, 0x88, 0x13,   /*          Physical Maximum (5000),    */
	0x26, 0x20, 0x4E,   /*          Logical Maximum (20000),    */
	0x81, 0x02,         /*          Input (Variable),           */
	0xB4,               /*          Pop,                        */
	0x09, 0x30,         /*          Usage (Tip Pressure),       */
	0x26, 0xFF, 0x03,   /*          Logical Maximum (1023),     */
	0x81, 0x02,         /*          Input (Variable),           */
	0xC0,               /*      End Collection,                 */
	0xC0                /*  End Collection                      */
};

/* Fixed PID 0522 tablet report descriptor, interface 1 (mouse) */
static __u8 twhl850_rdesc_fixed1[] = {
	0x05, 0x01,         /*  Usage Page (Desktop),               */
	0x09, 0x02,         /*  Usage (Mouse),                      */
	0xA1, 0x01,         /*  Collection (Application),           */
	0x85, 0x01,         /*      Report ID (1),                  */
	0x09, 0x01,         /*      Usage (Pointer),                */
	0xA0,               /*      Collection (Physical),          */
	0x05, 0x09,         /*          Usage Page (Button),        */
	0x75, 0x01,         /*          Report Size (1),            */
	0x95, 0x03,         /*          Report Count (3),           */
	0x19, 0x01,         /*          Usage Minimum (01h),        */
	0x29, 0x03,         /*          Usage Maximum (03h),        */
	0x14,               /*          Logical Minimum (0),        */
	0x25, 0x01,         /*          Logical Maximum (1),        */
	0x81, 0x02,         /*          Input (Variable),           */
	0x95, 0x05,         /*          Report Count (5),           */
	0x81, 0x03,         /*          Input (Constant, Variable), */
	0x05, 0x01,         /*          Usage Page (Desktop),       */
	0x09, 0x30,         /*          Usage (X),                  */
	0x09, 0x31,         /*          Usage (Y),                  */
	0x16, 0x00, 0x80,   /*          Logical Minimum (-32768),   */
	0x26, 0xFF, 0x7F,   /*          Logical Maximum (32767),    */
	0x75, 0x10,         /*          Report Size (16),           */
	0x95, 0x02,         /*          Report Count (2),           */
	0x81, 0x06,         /*          Input (Variable, Relative), */
	0x09, 0x38,         /*          Usage (Wheel),              */
	0x15, 0xFF,         /*          Logical Minimum (-1),       */
	0x25, 0x01,         /*          Logical Maximum (1),        */
	0x95, 0x01,         /*          Report Count (1),           */
	0x75, 0x08,         /*          Report Size (8),            */
	0x81, 0x06,         /*          Input (Variable, Relative), */
	0x81, 0x03,         /*          Input (Constant, Variable), */
	0xC0,               /*      End Collection,                 */
	0xC0                /*  End Collection                      */
};

/* Fixed PID 0522 tablet report descriptor, interface 2 (frame buttons) */
static __u8 twhl850_rdesc_fixed2[] = {
	0x05, 0x01,         /*  Usage Page (Desktop),               */
	0x09, 0x06,         /*  Usage (Keyboard),                   */
	0xA1, 0x01,         /*  Collection (Application),           */
	0x85, 0x03,         /*      Report ID (3),                  */
	0x05, 0x07,         /*      Usage Page (Keyboard),          */
	0x14,               /*      Logical Minimum (0),            */
	0x19, 0xE0,         /*      Usage Minimum (KB Leftcontrol), */
	0x29, 0xE7,         /*      Usage Maximum (KB Right GUI),   */
	0x25, 0x01,         /*      Logical Maximum (1),            */
	0x75, 0x01,         /*      Report Size (1),                */
	0x95, 0x08,         /*      Report Count (8),               */
	0x81, 0x02,         /*      Input (Variable),               */
	0x18,               /*      Usage Minimum (None),           */
	0x29, 0xFF,         /*      Usage Maximum (FFh),            */
	0x26, 0xFF, 0x00,   /*      Logical Maximum (255),          */
	0x75, 0x08,         /*      Report Size (8),                */
	0x95, 0x06,         /*      Report Count (6),               */
	0x80,               /*      Input,                          */
	0xC0                /*  End Collection                      */
};

/*
 * See TWHA60 description, device and HID report descriptors at
 * http://sf.net/apps/mediawiki/digimend/?title=UC-Logic_Tablet_TWHA60
 */

/* Size of the original descriptors of TWHA60 tablet */
#define TWHA60_RDESC_ORIG_SIZE0 254
#define TWHA60_RDESC_ORIG_SIZE1 139

/* Fixed TWHA60 report descriptor, interface 0 (stylus) */
static __u8 twha60_rdesc_fixed0[] = {
	0x05, 0x0D,         /*  Usage Page (Digitizer),             */
	0x09, 0x02,         /*  Usage (Pen),                        */
	0xA1, 0x01,         /*  Collection (Application),           */
	0x85, 0x09,         /*      Report ID (9),                  */
	0x09, 0x20,         /*      Usage (Stylus),                 */
	0xA0,               /*      Collection (Physical),          */
	0x75, 0x01,         /*          Report Size (1),            */
	0x09, 0x42,         /*          Usage (Tip Switch),         */
	0x09, 0x44,         /*          Usage (Barrel Switch),      */
	0x09, 0x46,         /*          Usage (Tablet Pick),        */
	0x14,               /*          Logical Minimum (0),        */
	0x25, 0x01,         /*          Logical Maximum (1),        */
	0x95, 0x03,         /*          Report Count (3),           */
	0x81, 0x02,         /*          Input (Variable),           */
	0x95, 0x04,         /*          Report Count (4),           */
	0x81, 0x01,         /*          Input (Constant),           */
	0x09, 0x32,         /*          Usage (In Range),           */
	0x95, 0x01,         /*          Report Count (1),           */
	0x81, 0x02,         /*          Input (Variable),           */
	0x75, 0x10,         /*          Report Size (16),           */
	0x95, 0x01,         /*          Report Count (1),           */
	0x14,               /*          Logical Minimum (0),        */
	0xA4,               /*          Push,                       */
	0x05, 0x01,         /*          Usage Page (Desktop),       */
	0x55, 0xFD,         /*          Unit Exponent (-3),         */
	0x65, 0x13,         /*          Unit (Inch),                */
	0x34,               /*          Physical Minimum (0),       */
	0x09, 0x30,         /*          Usage (X),                  */
	0x46, 0x10, 0x27,   /*          Physical Maximum (10000),   */
	0x27, 0x3F, 0x9C,
		0x00, 0x00, /*          Logical Maximum (39999),    */
	0x81, 0x02,         /*          Input (Variable),           */
	0x09, 0x31,         /*          Usage (Y),                  */
	0x46, 0x6A, 0x18,   /*          Physical Maximum (6250),    */
	0x26, 0xA7, 0x61,   /*          Logical Maximum (24999),    */
	0x81, 0x02,         /*          Input (Variable),           */
	0xB4,               /*          Pop,                        */
	0x09, 0x30,         /*          Usage (Tip Pressure),       */
	0x26, 0xFF, 0x03,   /*          Logical Maximum (1023),     */
	0x81, 0x02,         /*          Input (Variable),           */
	0xC0,               /*      End Collection,                 */
	0xC0                /*  End Collection                      */
};

/* Fixed TWHA60 report descriptor, interface 1 (frame buttons) */
static __u8 twha60_rdesc_fixed1[] = {
	0x05, 0x01, /*  Usage Page (Desktop),       */
	0x09, 0x06, /*  Usage (Keyboard),           */
	0xA1, 0x01, /*  Collection (Application),   */
	0x85, 0x05, /*      Report ID (5),          */
	0x05, 0x07, /*      Usage Page (Keyboard),  */
	0x14,       /*      Logical Minimum (0),    */
	0x25, 0x01, /*      Logical Maximum (1),    */
	0x75, 0x01, /*      Report Size (1),        */
	0x95, 0x08, /*      Report Count (8),       */
	0x81, 0x01, /*      Input (Constant),       */
	0x95, 0x0C, /*      Report Count (12),      */
	0x19, 0x3A, /*      Usage Minimum (KB F1),  */
	0x29, 0x45, /*      Usage Maximum (KB F12), */
	0x81, 0x02, /*      Input (Variable),       */
	0x95, 0x0C, /*      Report Count (12),      */
	0x19, 0x68, /*      Usage Minimum (KB F13), */
	0x29, 0x73, /*      Usage Maximum (KB F24), */
	0x81, 0x02, /*      Input (Variable),       */
	0x95, 0x08, /*      Report Count (8),       */
	0x81, 0x01, /*      Input (Constant),       */
	0xC0        /*  End Collection              */
};

static __u8 *uclogic_report_fixup(struct hid_device *hdev, __u8 *rdesc,
					unsigned int *rsize)
{
	struct usb_interface *iface = to_usb_interface(hdev->dev.parent);
	__u8 iface_num = iface->cur_altsetting->desc.bInterfaceNumber;

	switch (hdev->product) {
	case USB_DEVICE_ID_UCLOGIC_TABLET_PF1209:
		if (*rsize == PF1209_RDESC_ORIG_SIZE) {
			rdesc = pf1209_rdesc_fixed;
			*rsize = sizeof(pf1209_rdesc_fixed);
		}
		break;
	case USB_DEVICE_ID_UCLOGIC_TABLET_WP4030U:
		if (*rsize == WPXXXXU_RDESC_ORIG_SIZE) {
			rdesc = wp4030u_rdesc_fixed;
			*rsize = sizeof(wp4030u_rdesc_fixed);
		}
		break;
	case USB_DEVICE_ID_UCLOGIC_TABLET_WP5540U:
		if (*rsize == WPXXXXU_RDESC_ORIG_SIZE) {
			rdesc = wp5540u_rdesc_fixed;
			*rsize = sizeof(wp5540u_rdesc_fixed);
		}
		break;
	case USB_DEVICE_ID_UCLOGIC_TABLET_WP8060U:
		if (*rsize == WPXXXXU_RDESC_ORIG_SIZE) {
			rdesc = wp8060u_rdesc_fixed;
			*rsize = sizeof(wp8060u_rdesc_fixed);
		}
		break;
	case USB_DEVICE_ID_UCLOGIC_TABLET_WP1062:
		if (*rsize == WP1062_RDESC_ORIG_SIZE) {
			rdesc = wp1062_rdesc_fixed;
			*rsize = sizeof(wp1062_rdesc_fixed);
		}
		break;
	case USB_DEVICE_ID_UCLOGIC_WIRELESS_TABLET_TWHL850:
		switch (iface_num) {
		case 0:
			if (*rsize == TWHL850_RDESC_ORIG_SIZE0) {
				rdesc = twhl850_rdesc_fixed0;
				*rsize = sizeof(twhl850_rdesc_fixed0);
			}
			break;
		case 1:
			if (*rsize == TWHL850_RDESC_ORIG_SIZE1) {
				rdesc = twhl850_rdesc_fixed1;
				*rsize = sizeof(twhl850_rdesc_fixed1);
			}
			break;
		case 2:
			if (*rsize == TWHL850_RDESC_ORIG_SIZE2) {
				rdesc = twhl850_rdesc_fixed2;
				*rsize = sizeof(twhl850_rdesc_fixed2);
			}
			break;
		}
		break;
	case USB_DEVICE_ID_UCLOGIC_TABLET_TWHA60:
		switch (iface_num) {
		case 0:
			if (*rsize == TWHA60_RDESC_ORIG_SIZE0) {
				rdesc = twha60_rdesc_fixed0;
				*rsize = sizeof(twha60_rdesc_fixed0);
			}
			break;
		case 1:
			if (*rsize == TWHA60_RDESC_ORIG_SIZE1) {
				rdesc = twha60_rdesc_fixed1;
				*rsize = sizeof(twha60_rdesc_fixed1);
			}
			break;
		}
		break;
	}

	return rdesc;
}

static const struct hid_device_id uclogic_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_UCLOGIC,
				USB_DEVICE_ID_UCLOGIC_TABLET_PF1209) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_UCLOGIC,
				USB_DEVICE_ID_UCLOGIC_TABLET_WP4030U) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_UCLOGIC,
				USB_DEVICE_ID_UCLOGIC_TABLET_WP5540U) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_UCLOGIC,
				USB_DEVICE_ID_UCLOGIC_TABLET_WP8060U) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_UCLOGIC,
				USB_DEVICE_ID_UCLOGIC_TABLET_WP1062) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_UCLOGIC,
				USB_DEVICE_ID_UCLOGIC_WIRELESS_TABLET_TWHL850) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_UCLOGIC,
				USB_DEVICE_ID_UCLOGIC_TABLET_TWHA60) },
	{ }
};
MODULE_DEVICE_TABLE(hid, uclogic_devices);

static struct hid_driver uclogic_driver = {
	.name = "uclogic",
	.id_table = uclogic_devices,
	.report_fixup = uclogic_report_fixup,
};
module_hid_driver(uclogic_driver);

MODULE_LICENSE("GPL");
