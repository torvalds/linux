// SPDX-License-Identifier: GPL-2.0+
/*
 *  HID driver for UC-Logic devices not fully compliant with HID standard
 *  - original and fixed report descriptors
 *
 *  Copyright (c) 2010-2017 Nikolai Kondrashov
 *  Copyright (c) 2013 Martin Rusko
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include "hid-uclogic-rdesc.h"
#include <linux/slab.h>
#include <linux/unaligned.h>
#include <kunit/visibility.h>

/* Fixed WP4030U report descriptor */
const __u8 uclogic_rdesc_wp4030u_fixed_arr[] = {
	0x05, 0x0D,         /*  Usage Page (Digitizer),             */
	0x09, 0x01,         /*  Usage (Digitizer),                  */
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

const size_t uclogic_rdesc_wp4030u_fixed_size =
			sizeof(uclogic_rdesc_wp4030u_fixed_arr);

/* Fixed WP5540U report descriptor */
const __u8 uclogic_rdesc_wp5540u_fixed_arr[] = {
	0x05, 0x0D,         /*  Usage Page (Digitizer),             */
	0x09, 0x01,         /*  Usage (Digitizer),                  */
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

const size_t uclogic_rdesc_wp5540u_fixed_size =
			sizeof(uclogic_rdesc_wp5540u_fixed_arr);

/* Fixed WP8060U report descriptor */
const __u8 uclogic_rdesc_wp8060u_fixed_arr[] = {
	0x05, 0x0D,         /*  Usage Page (Digitizer),             */
	0x09, 0x01,         /*  Usage (Digitizer),                  */
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

const size_t uclogic_rdesc_wp8060u_fixed_size =
			sizeof(uclogic_rdesc_wp8060u_fixed_arr);

/* Fixed WP1062 report descriptor */
const __u8 uclogic_rdesc_wp1062_fixed_arr[] = {
	0x05, 0x0D,         /*  Usage Page (Digitizer),             */
	0x09, 0x01,         /*  Usage (Digitizer),                  */
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

const size_t uclogic_rdesc_wp1062_fixed_size =
			sizeof(uclogic_rdesc_wp1062_fixed_arr);

/* Fixed PF1209 report descriptor */
const __u8 uclogic_rdesc_pf1209_fixed_arr[] = {
	0x05, 0x0D,         /*  Usage Page (Digitizer),             */
	0x09, 0x01,         /*  Usage (Digitizer),                  */
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

const size_t uclogic_rdesc_pf1209_fixed_size =
			sizeof(uclogic_rdesc_pf1209_fixed_arr);

/* Fixed PID 0522 tablet report descriptor, interface 0 (stylus) */
const __u8 uclogic_rdesc_twhl850_fixed0_arr[] = {
	0x05, 0x0D,         /*  Usage Page (Digitizer),             */
	0x09, 0x01,         /*  Usage (Digitizer),                  */
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

const size_t uclogic_rdesc_twhl850_fixed0_size =
			sizeof(uclogic_rdesc_twhl850_fixed0_arr);

/* Fixed PID 0522 tablet report descriptor, interface 1 (mouse) */
const __u8 uclogic_rdesc_twhl850_fixed1_arr[] = {
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

const size_t uclogic_rdesc_twhl850_fixed1_size =
			sizeof(uclogic_rdesc_twhl850_fixed1_arr);

/* Fixed PID 0522 tablet report descriptor, interface 2 (frame buttons) */
const __u8 uclogic_rdesc_twhl850_fixed2_arr[] = {
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

const size_t uclogic_rdesc_twhl850_fixed2_size =
			sizeof(uclogic_rdesc_twhl850_fixed2_arr);

/* Fixed TWHA60 report descriptor, interface 0 (stylus) */
const __u8 uclogic_rdesc_twha60_fixed0_arr[] = {
	0x05, 0x0D,         /*  Usage Page (Digitizer),             */
	0x09, 0x01,         /*  Usage (Digitizer),                  */
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

const size_t uclogic_rdesc_twha60_fixed0_size =
			sizeof(uclogic_rdesc_twha60_fixed0_arr);

/* Fixed TWHA60 report descriptor, interface 1 (frame buttons) */
const __u8 uclogic_rdesc_twha60_fixed1_arr[] = {
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

const size_t uclogic_rdesc_twha60_fixed1_size =
			sizeof(uclogic_rdesc_twha60_fixed1_arr);

/* Fixed report descriptor template for (tweaked) v1 pen reports */
const __u8 uclogic_rdesc_v1_pen_template_arr[] = {
	0x05, 0x0D,             /*  Usage Page (Digitizer),                 */
	0x09, 0x01,             /*  Usage (Digitizer),                      */
	0xA1, 0x01,             /*  Collection (Application),               */
	0x85, 0x07,             /*      Report ID (7),                      */
	0x09, 0x20,             /*      Usage (Stylus),                     */
	0xA0,                   /*      Collection (Physical),              */
	0x14,                   /*          Logical Minimum (0),            */
	0x25, 0x01,             /*          Logical Maximum (1),            */
	0x75, 0x01,             /*          Report Size (1),                */
	0x09, 0x42,             /*          Usage (Tip Switch),             */
	0x09, 0x44,             /*          Usage (Barrel Switch),          */
	0x09, 0x46,             /*          Usage (Tablet Pick),            */
	0x95, 0x03,             /*          Report Count (3),               */
	0x81, 0x02,             /*          Input (Variable),               */
	0x95, 0x03,             /*          Report Count (3),               */
	0x81, 0x03,             /*          Input (Constant, Variable),     */
	0x09, 0x32,             /*          Usage (In Range),               */
	0x95, 0x01,             /*          Report Count (1),               */
	0x81, 0x02,             /*          Input (Variable),               */
	0x95, 0x01,             /*          Report Count (1),               */
	0x81, 0x03,             /*          Input (Constant, Variable),     */
	0x75, 0x10,             /*          Report Size (16),               */
	0x95, 0x01,             /*          Report Count (1),               */
	0xA4,                   /*          Push,                           */
	0x05, 0x01,             /*          Usage Page (Desktop),           */
	0x65, 0x13,             /*          Unit (Inch),                    */
	0x55, 0xFD,             /*          Unit Exponent (-3),             */
	0x34,                   /*          Physical Minimum (0),           */
	0x09, 0x30,             /*          Usage (X),                      */
	0x27, UCLOGIC_RDESC_PEN_PH(X_LM),
				/*          Logical Maximum (PLACEHOLDER),  */
	0x47, UCLOGIC_RDESC_PEN_PH(X_PM),
				/*          Physical Maximum (PLACEHOLDER), */
	0x81, 0x02,             /*          Input (Variable),               */
	0x09, 0x31,             /*          Usage (Y),                      */
	0x27, UCLOGIC_RDESC_PEN_PH(Y_LM),
				/*          Logical Maximum (PLACEHOLDER),  */
	0x47, UCLOGIC_RDESC_PEN_PH(Y_PM),
				/*          Physical Maximum (PLACEHOLDER), */
	0x81, 0x02,             /*          Input (Variable),               */
	0xB4,                   /*          Pop,                            */
	0x09, 0x30,             /*          Usage (Tip Pressure),           */
	0x27, UCLOGIC_RDESC_PEN_PH(PRESSURE_LM),
				/*          Logical Maximum (PLACEHOLDER),  */
	0x81, 0x02,             /*          Input (Variable),               */
	0xC0,                   /*      End Collection,                     */
	0xC0                    /*  End Collection                          */
};

const size_t uclogic_rdesc_v1_pen_template_size =
			sizeof(uclogic_rdesc_v1_pen_template_arr);

/* Fixed report descriptor template for (tweaked) v2 pen reports */
const __u8 uclogic_rdesc_v2_pen_template_arr[] = {
	0x05, 0x0D,             /*  Usage Page (Digitizer),                 */
	0x09, 0x01,             /*  Usage (Digitizer),                      */
	0xA1, 0x01,             /*  Collection (Application),               */
	0x85, 0x08,             /*      Report ID (8),                      */
	0x09, 0x20,             /*      Usage (Stylus),                     */
	0xA0,                   /*      Collection (Physical),              */
	0x14,                   /*          Logical Minimum (0),            */
	0x25, 0x01,             /*          Logical Maximum (1),            */
	0x75, 0x01,             /*          Report Size (1),                */
	0x09, 0x42,             /*          Usage (Tip Switch),             */
	0x09, 0x44,             /*          Usage (Barrel Switch),          */
	0x09, 0x46,             /*          Usage (Tablet Pick),            */
	0x95, 0x03,             /*          Report Count (3),               */
	0x81, 0x02,             /*          Input (Variable),               */
	0x95, 0x03,             /*          Report Count (3),               */
	0x81, 0x03,             /*          Input (Constant, Variable),     */
	0x09, 0x32,             /*          Usage (In Range),               */
	0x95, 0x01,             /*          Report Count (1),               */
	0x81, 0x02,             /*          Input (Variable),               */
	0x95, 0x01,             /*          Report Count (1),               */
	0x81, 0x03,             /*          Input (Constant, Variable),     */
	0x95, 0x01,             /*          Report Count (1),               */
	0xA4,                   /*          Push,                           */
	0x05, 0x01,             /*          Usage Page (Desktop),           */
	0x65, 0x13,             /*          Unit (Inch),                    */
	0x55, 0xFD,             /*          Unit Exponent (-3),             */
	0x75, 0x18,             /*          Report Size (24),               */
	0x34,                   /*          Physical Minimum (0),           */
	0x09, 0x30,             /*          Usage (X),                      */
	0x27, UCLOGIC_RDESC_PEN_PH(X_LM),
				/*          Logical Maximum (PLACEHOLDER),  */
	0x47, UCLOGIC_RDESC_PEN_PH(X_PM),
				/*          Physical Maximum (PLACEHOLDER), */
	0x81, 0x02,             /*          Input (Variable),               */
	0x09, 0x31,             /*          Usage (Y),                      */
	0x27, UCLOGIC_RDESC_PEN_PH(Y_LM),
				/*          Logical Maximum (PLACEHOLDER),  */
	0x47, UCLOGIC_RDESC_PEN_PH(Y_PM),
				/*          Physical Maximum (PLACEHOLDER), */
	0x81, 0x02,             /*          Input (Variable),               */
	0xB4,                   /*          Pop,                            */
	0x09, 0x30,             /*          Usage (Tip Pressure),           */
	0x75, 0x10,             /*          Report Size (16),               */
	0x27, UCLOGIC_RDESC_PEN_PH(PRESSURE_LM),
				/*          Logical Maximum (PLACEHOLDER),  */
	0x81, 0x02,             /*          Input (Variable),               */
	0x54,                   /*          Unit Exponent (0),              */
	0x65, 0x14,             /*          Unit (Degrees),                 */
	0x35, 0xC4,             /*          Physical Minimum (-60),         */
	0x45, 0x3C,             /*          Physical Maximum (60),          */
	0x15, 0xC4,             /*          Logical Minimum (-60),          */
	0x25, 0x3C,             /*          Logical Maximum (60),           */
	0x75, 0x08,             /*          Report Size (8),                */
	0x95, 0x02,             /*          Report Count (2),               */
	0x09, 0x3D,             /*          Usage (X Tilt),                 */
	0x09, 0x3E,             /*          Usage (Y Tilt),                 */
	0x81, 0x02,             /*          Input (Variable),               */
	0xC0,                   /*      End Collection,                     */
	0xC0                    /*  End Collection                          */
};

const size_t uclogic_rdesc_v2_pen_template_size =
			sizeof(uclogic_rdesc_v2_pen_template_arr);

/*
 * Expand to the contents of a generic frame buttons report descriptor.
 *
 * @_id:	The report ID to use.
 * @_size:	Size of the report to pad to, including report ID, bytes.
 */
#define UCLOGIC_RDESC_FRAME_BUTTONS_BYTES(_id, _size) \
	0x05, 0x01,     /*  Usage Page (Desktop),               */ \
	0x09, 0x07,     /*  Usage (Keypad),                     */ \
	0xA1, 0x01,     /*  Collection (Application),           */ \
	0x85, (_id),    /*      Report ID (_id),                */ \
	0x14,           /*      Logical Minimum (0),            */ \
	0x25, 0x01,     /*      Logical Maximum (1),            */ \
	0x75, 0x01,     /*      Report Size (1),                */ \
	0x05, 0x0D,     /*      Usage Page (Digitizer),         */ \
	0x09, 0x39,     /*      Usage (Tablet Function Keys),   */ \
	0xA0,           /*      Collection (Physical),          */ \
	0x09, 0x44,     /*          Usage (Barrel Switch),      */ \
	0x95, 0x01,     /*          Report Count (1),           */ \
	0x81, 0x02,     /*          Input (Variable),           */ \
	0x05, 0x01,     /*          Usage Page (Desktop),       */ \
	0x09, 0x30,     /*          Usage (X),                  */ \
	0x09, 0x31,     /*          Usage (Y),                  */ \
	0x95, 0x02,     /*          Report Count (2),           */ \
	0x81, 0x02,     /*          Input (Variable),           */ \
	0x95, 0x15,     /*          Report Count (21),          */ \
	0x81, 0x01,     /*          Input (Constant),           */ \
	0x05, 0x09,     /*          Usage Page (Button),        */ \
	0x19, 0x01,     /*          Usage Minimum (01h),        */ \
	0x29, 0x0A,     /*          Usage Maximum (0Ah),        */ \
	0x95, 0x0A,     /*          Report Count (10),          */ \
	0x81, 0x02,     /*          Input (Variable),           */ \
	0xC0,           /*      End Collection,                 */ \
	0x05, 0x01,     /*      Usage Page (Desktop),           */ \
	0x09, 0x05,     /*      Usage (Gamepad),                */ \
	0xA0,           /*      Collection (Physical),          */ \
	0x05, 0x09,     /*          Usage Page (Button),        */ \
	0x19, 0x01,     /*          Usage Minimum (01h),        */ \
	0x29, 0x0A,     /*          Usage Maximum (0Ah),        */ \
	0x95, 0x0A,     /*          Report Count (10),          */ \
	0x81, 0x02,     /*          Input (Variable),           */ \
	0x95, ((_size) * 8 - 52),                                  \
			/*          Report Count (padding),     */ \
	0x81, 0x01,     /*          Input (Constant),           */ \
	0xC0,           /*      End Collection,                 */ \
	0xC0            /*  End Collection                      */

/* Fixed report descriptor for (tweaked) v1 frame reports */
const __u8 uclogic_rdesc_v1_frame_arr[] = {
	UCLOGIC_RDESC_FRAME_BUTTONS_BYTES(UCLOGIC_RDESC_V1_FRAME_ID, 8)
};
const size_t uclogic_rdesc_v1_frame_size =
			sizeof(uclogic_rdesc_v1_frame_arr);

/* Fixed report descriptor for (tweaked) v2 frame button reports */
const __u8 uclogic_rdesc_v2_frame_buttons_arr[] = {
	UCLOGIC_RDESC_FRAME_BUTTONS_BYTES(UCLOGIC_RDESC_V2_FRAME_BUTTONS_ID,
					  12)
};
const size_t uclogic_rdesc_v2_frame_buttons_size =
			sizeof(uclogic_rdesc_v2_frame_buttons_arr);

/* Fixed report descriptor for (tweaked) v2 frame touch ring reports */
const __u8 uclogic_rdesc_v2_frame_touch_ring_arr[] = {
	0x05, 0x01,         /*  Usage Page (Desktop),               */
	0x09, 0x07,         /*  Usage (Keypad),                     */
	0xA1, 0x01,         /*  Collection (Application),           */
	0x85, UCLOGIC_RDESC_V2_FRAME_TOUCH_ID,
			    /*      Report ID (TOUCH_ID),           */
	0x14,               /*      Logical Minimum (0),            */
	0x05, 0x0D,         /*      Usage Page (Digitizer),         */
	0x09, 0x39,         /*      Usage (Tablet Function Keys),   */
	0xA0,               /*      Collection (Physical),          */
	0x25, 0x01,         /*          Logical Maximum (1),        */
	0x75, 0x01,         /*          Report Size (1),            */
	0x05, 0x09,         /*          Usage Page (Button),        */
	0x09, 0x01,         /*          Usage (01h),                */
	0x95, 0x01,         /*          Report Count (1),           */
	0x81, 0x02,         /*          Input (Variable),           */
	0x95, 0x07,         /*          Report Count (7),           */
	0x81, 0x01,         /*          Input (Constant),           */
	0x75, 0x08,         /*          Report Size (8),            */
	0x95, 0x02,         /*          Report Count (2),           */
	0x81, 0x01,         /*          Input (Constant),           */
	0x05, 0x0D,         /*          Usage Page (Digitizer),     */
	0x0A, 0xFF, 0xFF,   /*          Usage (FFFFh),              */
	0x26, 0xFF, 0x00,   /*          Logical Maximum (255),      */
	0x95, 0x01,         /*          Report Count (1),           */
	0x81, 0x02,         /*          Input (Variable),           */
	0x05, 0x01,         /*          Usage Page (Desktop),       */
	0x09, 0x38,         /*          Usage (Wheel),              */
	0x95, 0x01,         /*          Report Count (1),           */
	0x15, 0x00,         /*          Logical Minimum (0),        */
	0x25, 0x0B,         /*          Logical Maximum (11),       */
	0x81, 0x02,         /*          Input (Variable),           */
	0x09, 0x30,         /*          Usage (X),                  */
	0x09, 0x31,         /*          Usage (Y),                  */
	0x14,               /*          Logical Minimum (0),        */
	0x25, 0x01,         /*          Logical Maximum (1),        */
	0x75, 0x01,         /*          Report Size (1),            */
	0x95, 0x02,         /*          Report Count (2),           */
	0x81, 0x02,         /*          Input (Variable),           */
	0x95, 0x2E,         /*          Report Count (46),          */
	0x81, 0x01,         /*          Input (Constant),           */
	0xC0,               /*      End Collection,                 */
	0xC0                /*  End Collection                      */
};
const size_t uclogic_rdesc_v2_frame_touch_ring_size =
			sizeof(uclogic_rdesc_v2_frame_touch_ring_arr);

/* Fixed report descriptor for (tweaked) v2 frame touch strip reports */
const __u8 uclogic_rdesc_v2_frame_touch_strip_arr[] = {
	0x05, 0x01,         /*  Usage Page (Desktop),               */
	0x09, 0x07,         /*  Usage (Keypad),                     */
	0xA1, 0x01,         /*  Collection (Application),           */
	0x85, UCLOGIC_RDESC_V2_FRAME_TOUCH_ID,
			    /*      Report ID (TOUCH_ID),           */
	0x14,               /*      Logical Minimum (0),            */
	0x05, 0x0D,         /*      Usage Page (Digitizer),         */
	0x09, 0x39,         /*      Usage (Tablet Function Keys),   */
	0xA0,               /*      Collection (Physical),          */
	0x25, 0x01,         /*          Logical Maximum (1),        */
	0x75, 0x01,         /*          Report Size (1),            */
	0x05, 0x09,         /*          Usage Page (Button),        */
	0x09, 0x01,         /*          Usage (01h),                */
	0x95, 0x01,         /*          Report Count (1),           */
	0x81, 0x02,         /*          Input (Variable),           */
	0x95, 0x07,         /*          Report Count (7),           */
	0x81, 0x01,         /*          Input (Constant),           */
	0x75, 0x08,         /*          Report Size (8),            */
	0x95, 0x02,         /*          Report Count (2),           */
	0x81, 0x01,         /*          Input (Constant),           */
	0x05, 0x0D,         /*          Usage Page (Digitizer),     */
	0x0A, 0xFF, 0xFF,   /*          Usage (FFFFh),              */
	0x26, 0xFF, 0x00,   /*          Logical Maximum (255),      */
	0x95, 0x01,         /*          Report Count (1),           */
	0x81, 0x02,         /*          Input (Variable),           */
	0x05, 0x01,         /*          Usage Page (Desktop),       */
	0x09, 0x33,         /*          Usage (Rx),                 */
	0x09, 0x34,         /*          Usage (Ry),                 */
	0x95, 0x01,         /*          Report Count (1),           */
	0x15, 0x00,         /*          Logical Minimum (0),        */
	0x25, 0x07,         /*          Logical Maximum (7),        */
	0x81, 0x02,         /*          Input (Variable),           */
	0x09, 0x30,         /*          Usage (X),                  */
	0x09, 0x31,         /*          Usage (Y),                  */
	0x14,               /*          Logical Minimum (0),        */
	0x25, 0x01,         /*          Logical Maximum (1),        */
	0x75, 0x01,         /*          Report Size (1),            */
	0x95, 0x02,         /*          Report Count (2),           */
	0x81, 0x02,         /*          Input (Variable),           */
	0x95, 0x2E,         /*          Report Count (46),          */
	0x81, 0x01,         /*          Input (Constant),           */
	0xC0,               /*      End Collection,                 */
	0xC0                /*  End Collection                      */
};
const size_t uclogic_rdesc_v2_frame_touch_strip_size =
			sizeof(uclogic_rdesc_v2_frame_touch_strip_arr);

/* Fixed report descriptor for (tweaked) v2 frame dial reports */
const __u8 uclogic_rdesc_v2_frame_dial_arr[] = {
	0x05, 0x01,         /*  Usage Page (Desktop),               */
	0x09, 0x07,         /*  Usage (Keypad),                     */
	0xA1, 0x01,         /*  Collection (Application),           */
	0x85, UCLOGIC_RDESC_V2_FRAME_DIAL_ID,
			    /*      Report ID (DIAL_ID),            */
	0x14,               /*      Logical Minimum (0),            */
	0x05, 0x0D,         /*      Usage Page (Digitizer),         */
	0x09, 0x39,         /*      Usage (Tablet Function Keys),   */
	0xA0,               /*      Collection (Physical),          */
	0x25, 0x01,         /*          Logical Maximum (1),        */
	0x75, 0x01,         /*          Report Size (1),            */
	0x95, 0x01,         /*          Report Count (1),           */
	0x81, 0x01,         /*          Input (Constant),           */
	0x05, 0x09,         /*          Usage Page (Button),        */
	0x09, 0x01,         /*          Usage (01h),                */
	0x95, 0x01,         /*          Report Count (1),           */
	0x81, 0x02,         /*          Input (Variable),           */
	0x95, 0x06,         /*          Report Count (6),           */
	0x81, 0x01,         /*          Input (Constant),           */
	0x75, 0x08,         /*          Report Size (8),            */
	0x95, 0x02,         /*          Report Count (2),           */
	0x81, 0x01,         /*          Input (Constant),           */
	0x05, 0x0D,         /*          Usage Page (Digitizer),     */
	0x0A, 0xFF, 0xFF,   /*          Usage (FFFFh),              */
	0x26, 0xFF, 0x00,   /*          Logical Maximum (255),      */
	0x95, 0x01,         /*          Report Count (1),           */
	0x81, 0x02,         /*          Input (Variable),           */
	0x05, 0x01,         /*          Usage Page (Desktop),       */
	0x09, 0x38,         /*          Usage (Wheel),              */
	0x95, 0x01,         /*          Report Count (1),           */
	0x15, 0xFF,         /*          Logical Minimum (-1),       */
	0x25, 0x01,         /*          Logical Maximum (1),        */
	0x81, 0x06,         /*          Input (Variable, Relative), */
	0x09, 0x30,         /*          Usage (X),                  */
	0x09, 0x31,         /*          Usage (Y),                  */
	0x14,               /*          Logical Minimum (0),        */
	0x25, 0x01,         /*          Logical Maximum (1),        */
	0x75, 0x01,         /*          Report Size (1),            */
	0x95, 0x02,         /*          Report Count (2),           */
	0x81, 0x02,         /*          Input (Variable),           */
	0x95, 0x2E,         /*          Report Count (46),          */
	0x81, 0x01,         /*          Input (Constant),           */
	0xC0,               /*      End Collection,                 */
	0xC0                /*  End Collection                      */
};
const size_t uclogic_rdesc_v2_frame_dial_size =
			sizeof(uclogic_rdesc_v2_frame_dial_arr);

const __u8 uclogic_ugee_v2_probe_arr[] = {
	0x02, 0xb0, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
const size_t uclogic_ugee_v2_probe_size = sizeof(uclogic_ugee_v2_probe_arr);
const int uclogic_ugee_v2_probe_endpoint = 0x03;

/* Fixed report descriptor template for UGEE v2 pen reports */
const __u8 uclogic_rdesc_ugee_v2_pen_template_arr[] = {
	0x05, 0x0d,         /*  Usage Page (Digitizers),                */
	0x09, 0x01,         /*  Usage (Digitizer),                      */
	0xa1, 0x01,         /*  Collection (Application),               */
	0x85, 0x02,         /*      Report ID (2),                      */
	0x09, 0x20,         /*      Usage (Stylus),                     */
	0xa1, 0x00,         /*      Collection (Physical),              */
	0x09, 0x42,         /*          Usage (Tip Switch),             */
	0x09, 0x44,         /*          Usage (Barrel Switch),          */
	0x09, 0x46,         /*          Usage (Tablet Pick),            */
	0x75, 0x01,         /*          Report Size (1),                */
	0x95, 0x03,         /*          Report Count (3),               */
	0x14,               /*          Logical Minimum (0),            */
	0x25, 0x01,         /*          Logical Maximum (1),            */
	0x81, 0x02,         /*          Input (Variable),               */
	0x95, 0x02,         /*          Report Count (2),               */
	0x81, 0x03,         /*          Input (Constant, Variable),     */
	0x09, 0x32,         /*          Usage (In Range),               */
	0x95, 0x01,         /*          Report Count (1),               */
	0x81, 0x02,         /*          Input (Variable),               */
	0x95, 0x02,         /*          Report Count (2),               */
	0x81, 0x03,         /*          Input (Constant, Variable),     */
	0x75, 0x10,         /*          Report Size (16),               */
	0x95, 0x01,         /*          Report Count (1),               */
	0x35, 0x00,         /*          Physical Minimum (0),           */
	0xa4,               /*          Push,                           */
	0x05, 0x01,         /*          Usage Page (Desktop),           */
	0x09, 0x30,         /*          Usage (X),                      */
	0x65, 0x13,         /*          Unit (Inch),                    */
	0x55, 0x0d,         /*          Unit Exponent (-3),             */
	0x27, UCLOGIC_RDESC_PEN_PH(X_LM),
			    /*          Logical Maximum (PLACEHOLDER),  */
	0x47, UCLOGIC_RDESC_PEN_PH(X_PM),
			    /*          Physical Maximum (PLACEHOLDER), */
	0x81, 0x02,         /*          Input (Variable),               */
	0x09, 0x31,         /*          Usage (Y),                      */
	0x27, UCLOGIC_RDESC_PEN_PH(Y_LM),
			    /*          Logical Maximum (PLACEHOLDER),  */
	0x47, UCLOGIC_RDESC_PEN_PH(Y_PM),
			    /*          Physical Maximum (PLACEHOLDER), */
	0x81, 0x02,         /*          Input (Variable),               */
	0xb4,               /*          Pop,                            */
	0x09, 0x30,         /*          Usage (Tip Pressure),           */
	0x45, 0x00,         /*          Physical Maximum (0),           */
	0x27, UCLOGIC_RDESC_PEN_PH(PRESSURE_LM),
			    /*          Logical Maximum (PLACEHOLDER),  */
	0x75, 0x0D,         /*          Report Size (13),               */
	0x95, 0x01,         /*          Report Count (1),               */
	0x81, 0x02,         /*          Input (Variable),               */
	0x75, 0x01,         /*          Report Size (1),                */
	0x95, 0x03,         /*          Report Count (3),               */
	0x81, 0x01,         /*          Input (Constant),               */
	0x09, 0x3d,         /*          Usage (X Tilt),                 */
	0x35, 0xC3,         /*          Physical Minimum (-61),         */
	0x45, 0x3C,         /*          Physical Maximum (60),          */
	0x15, 0xC3,         /*          Logical Minimum (-61),          */
	0x25, 0x3C,         /*          Logical Maximum (60),           */
	0x75, 0x08,         /*          Report Size (8),                */
	0x95, 0x01,         /*          Report Count (1),               */
	0x81, 0x02,         /*          Input (Variable),               */
	0x09, 0x3e,         /*          Usage (Y Tilt),                 */
	0x35, 0xC3,         /*          Physical Minimum (-61),         */
	0x45, 0x3C,         /*          Physical Maximum (60),          */
	0x15, 0xC3,         /*          Logical Minimum (-61),          */
	0x25, 0x3C,         /*          Logical Maximum (60),           */
	0x81, 0x02,         /*          Input (Variable),               */
	0xc0,               /*      End Collection,                     */
	0xc0,               /*  End Collection                          */
};
const size_t uclogic_rdesc_ugee_v2_pen_template_size =
			sizeof(uclogic_rdesc_ugee_v2_pen_template_arr);

/* Fixed report descriptor template for UGEE v2 frame reports (buttons only) */
const __u8 uclogic_rdesc_ugee_v2_frame_btn_template_arr[] = {
	0x05, 0x01,         /*  Usage Page (Desktop),                   */
	0x09, 0x07,         /*  Usage (Keypad),                         */
	0xA1, 0x01,         /*  Collection (Application),               */
	0x85, UCLOGIC_RDESC_V1_FRAME_ID,
			    /*      Report ID,                          */
	0x05, 0x0D,         /*      Usage Page (Digitizer),             */
	0x09, 0x39,         /*      Usage (Tablet Function Keys),       */
	0xA0,               /*      Collection (Physical),              */
	0x75, 0x01,         /*          Report Size (1),                */
	0x95, 0x08,         /*          Report Count (8),               */
	0x81, 0x01,         /*          Input (Constant),               */
	0x05, 0x09,         /*          Usage Page (Button),            */
	0x19, 0x01,         /*          Usage Minimum (01h),            */
	UCLOGIC_RDESC_FRAME_PH_BTN,
			    /*          Usage Maximum (PLACEHOLDER),    */
	0x95, 0x0A,         /*          Report Count (10),              */
	0x14,               /*          Logical Minimum (0),            */
	0x25, 0x01,         /*          Logical Maximum (1),            */
	0x81, 0x02,         /*          Input (Variable),               */
	0x95, 0x46,         /*          Report Count (70),              */
	0x81, 0x01,         /*          Input (Constant),               */
	0xC0,               /*      End Collection,                     */
	0xC0                /*  End Collection                          */
};
const size_t uclogic_rdesc_ugee_v2_frame_btn_template_size =
			sizeof(uclogic_rdesc_ugee_v2_frame_btn_template_arr);

/* Fixed report descriptor template for UGEE v2 frame reports (dial) */
const __u8 uclogic_rdesc_ugee_v2_frame_dial_template_arr[] = {
	0x05, 0x01,         /*  Usage Page (Desktop),                   */
	0x09, 0x07,         /*  Usage (Keypad),                         */
	0xA1, 0x01,         /*  Collection (Application),               */
	0x85, UCLOGIC_RDESC_V1_FRAME_ID,
			    /*      Report ID,                          */
	0x05, 0x0D,         /*      Usage Page (Digitizer),             */
	0x09, 0x39,         /*      Usage (Tablet Function Keys),       */
	0xA0,               /*      Collection (Physical),              */
	0x75, 0x01,         /*          Report Size (1),                */
	0x95, 0x08,         /*          Report Count (8),               */
	0x81, 0x01,         /*          Input (Constant),               */
	0x05, 0x09,         /*          Usage Page (Button),            */
	0x19, 0x01,         /*          Usage Minimum (01h),            */
	UCLOGIC_RDESC_FRAME_PH_BTN,
			    /*          Usage Maximum (PLACEHOLDER),    */
	0x95, 0x0A,         /*          Report Count (10),              */
	0x14,               /*          Logical Minimum (0),            */
	0x25, 0x01,         /*          Logical Maximum (1),            */
	0x81, 0x02,         /*          Input (Variable),               */
	0x95, 0x06,         /*          Report Count (6),               */
	0x81, 0x01,         /*          Input (Constant),               */
	0x75, 0x08,         /*          Report Size (8),                */
	0x95, 0x03,         /*          Report Count (3),               */
	0x81, 0x01,         /*          Input (Constant),               */
	0x05, 0x01,         /*          Usage Page (Desktop),           */
	0x09, 0x38,         /*          Usage (Wheel),                  */
	0x95, 0x01,         /*          Report Count (1),               */
	0x15, 0xFF,         /*          Logical Minimum (-1),           */
	0x25, 0x01,         /*          Logical Maximum (1),            */
	0x81, 0x06,         /*          Input (Variable, Relative),     */
	0x95, 0x02,         /*          Report Count (2),               */
	0x81, 0x01,         /*          Input (Constant),               */
	0xC0,               /*      End Collection,                     */
	0xC0                /*  End Collection                          */
};
const size_t uclogic_rdesc_ugee_v2_frame_dial_template_size =
			sizeof(uclogic_rdesc_ugee_v2_frame_dial_template_arr);

/* Fixed report descriptor template for UGEE v2 frame reports (mouse) */
const __u8 uclogic_rdesc_ugee_v2_frame_mouse_template_arr[] = {
	0x05, 0x01,         /*  Usage Page (Desktop),                   */
	0x09, 0x02,         /*  Usage (Mouse),                          */
	0xA1, 0x01,         /*  Collection (Application),               */
	0x85, 0x01,         /*      Report ID (1),                      */
	0x05, 0x01,         /*      Usage Page (Pointer),               */
	0xA0,               /*      Collection (Physical),              */
	0x75, 0x01,         /*          Report Size (1),                */
	0x95, 0x02,         /*          Report Count (2),               */
	0x05, 0x09,         /*          Usage Page (Button),            */
	0x19, 0x01,         /*          Usage Minimum (01h),            */
	0x29, 0x02,         /*          Usage Maximum (02h),            */
	0x14,               /*          Logical Minimum (0),            */
	0x25, 0x01,         /*          Logical Maximum (1),            */
	0x81, 0x02,         /*          Input (Variable),               */
	0x95, 0x06,         /*          Report Count (6),               */
	0x81, 0x01,         /*          Input (Constant),               */
	0x05, 0x01,         /*          Usage Page (Generic Desktop),   */
	0x09, 0x30,         /*          Usage (X),                      */
	0x09, 0x31,         /*          Usage (Y),                      */
	0x75, 0x10,         /*          Report Size (16),               */
	0x95, 0x02,         /*          Report Count (2),               */
	0x16, 0x00, 0x80,   /*          Logical Minimum (-32768),       */
	0x26, 0xFF, 0x7F,   /*          Logical Maximum (32767),        */
	0x81, 0x06,         /*          Input (Variable, Relative),     */
	0x95, 0x01,         /*          Report Count (1),               */
	0x81, 0x01,         /*          Input (Constant),               */
	0xC0,               /*      End Collection,                     */
	0xC0                /*  End Collection                          */
};
const size_t uclogic_rdesc_ugee_v2_frame_mouse_template_size =
			sizeof(uclogic_rdesc_ugee_v2_frame_mouse_template_arr);

/* Fixed report descriptor template for UGEE v2 battery reports */
const __u8 uclogic_rdesc_ugee_v2_battery_template_arr[] = {
	0x05, 0x01,         /*  Usage Page (Desktop),                   */
	0x09, 0x07,         /*  Usage (Keypad),                         */
	0xA1, 0x01,         /*  Collection (Application),               */
	0x85, UCLOGIC_RDESC_UGEE_V2_BATTERY_ID,
			    /*      Report ID,                          */
	0x75, 0x08,         /*      Report Size (8),                    */
	0x95, 0x02,         /*      Report Count (2),                   */
	0x81, 0x01,         /*      Input (Constant),                   */
	0x05, 0x84,         /*      Usage Page (Power Device),          */
	0x05, 0x85,         /*      Usage Page (Battery System),        */
	0x09, 0x65,         /*      Usage Page (AbsoluteStateOfCharge), */
	0x75, 0x08,         /*      Report Size (8),                    */
	0x95, 0x01,         /*      Report Count (1),                   */
	0x15, 0x00,         /*      Logical Minimum (0),                */
	0x26, 0xff, 0x00,   /*      Logical Maximum (255),              */
	0x81, 0x02,         /*      Input (Variable),                   */
	0x75, 0x01,         /*      Report Size (1),                    */
	0x95, 0x01,         /*      Report Count (1),                   */
	0x15, 0x00,         /*      Logical Minimum (0),                */
	0x25, 0x01,         /*      Logical Maximum (1),                */
	0x09, 0x44,         /*      Usage Page (Charging),              */
	0x81, 0x02,         /*      Input (Variable),                   */
	0x95, 0x07,         /*      Report Count (7),                   */
	0x81, 0x01,         /*      Input (Constant),                   */
	0x75, 0x08,         /*      Report Size (8),                    */
	0x95, 0x07,         /*      Report Count (7),                   */
	0x81, 0x01,         /*      Input (Constant),                   */
	0xC0                /*  End Collection                          */
};
const size_t uclogic_rdesc_ugee_v2_battery_template_size =
			sizeof(uclogic_rdesc_ugee_v2_battery_template_arr);

/* Fixed report descriptor for Ugee EX07 frame */
const __u8 uclogic_rdesc_ugee_ex07_frame_arr[] = {
	0x05, 0x01,             /*  Usage Page (Desktop),                   */
	0x09, 0x07,             /*  Usage (Keypad),                         */
	0xA1, 0x01,             /*  Collection (Application),               */
	0x85, 0x06,             /*      Report ID (6),                      */
	0x05, 0x0D,             /*      Usage Page (Digitizer),             */
	0x09, 0x39,             /*      Usage (Tablet Function Keys),       */
	0xA0,                   /*      Collection (Physical),              */
	0x05, 0x09,             /*          Usage Page (Button),            */
	0x75, 0x01,             /*          Report Size (1),                */
	0x19, 0x03,             /*          Usage Minimum (03h),            */
	0x29, 0x06,             /*          Usage Maximum (06h),            */
	0x95, 0x04,             /*          Report Count (4),               */
	0x81, 0x02,             /*          Input (Variable),               */
	0x95, 0x1A,             /*          Report Count (26),              */
	0x81, 0x03,             /*          Input (Constant, Variable),     */
	0x19, 0x01,             /*          Usage Minimum (01h),            */
	0x29, 0x02,             /*          Usage Maximum (02h),            */
	0x95, 0x02,             /*          Report Count (2),               */
	0x81, 0x02,             /*          Input (Variable),               */
	0xC0,                   /*      End Collection,                     */
	0xC0                    /*  End Collection                          */
};
const size_t uclogic_rdesc_ugee_ex07_frame_size =
			sizeof(uclogic_rdesc_ugee_ex07_frame_arr);

/* Fixed report descriptor for Ugee G5 frame controls */
const __u8 uclogic_rdesc_ugee_g5_frame_arr[] = {
	0x05, 0x01,         /*  Usage Page (Desktop),               */
	0x09, 0x07,         /*  Usage (Keypad),                     */
	0xA1, 0x01,         /*  Collection (Application),           */
	0x85, 0x06,         /*      Report ID (6),                  */
	0x05, 0x0D,         /*      Usage Page (Digitizer),         */
	0x09, 0x39,         /*      Usage (Tablet Function Keys),   */
	0xA0,               /*      Collection (Physical),          */
	0x14,               /*          Logical Minimum (0),        */
	0x25, 0x01,         /*          Logical Maximum (1),        */
	0x05, 0x01,         /*          Usage Page (Desktop),       */
	0x05, 0x09,         /*          Usage Page (Button),        */
	0x19, 0x01,         /*          Usage Minimum (01h),        */
	0x29, 0x05,         /*          Usage Maximum (05h),        */
	0x75, 0x01,         /*          Report Size (1),            */
	0x95, 0x05,         /*          Report Count (5),           */
	0x81, 0x02,         /*          Input (Variable),           */
	0x75, 0x01,         /*          Report Size (1),            */
	0x95, 0x03,         /*          Report Count (3),           */
	0x81, 0x01,         /*          Input (Constant),           */
	0x05, 0x0D,         /*          Usage Page (Digitizer),     */
	0x0A, 0xFF, 0xFF,   /*          Usage (FFFFh),              */
	0x26, 0xFF, 0x00,   /*          Logical Maximum (255),      */
	0x75, 0x08,         /*          Report Size (8),            */
	0x95, 0x01,         /*          Report Count (1),           */
	0x81, 0x02,         /*          Input (Variable),           */
	0x25, 0x01,         /*          Logical Maximum (1),        */
	0x09, 0x44,         /*          Usage (Barrel Switch),      */
	0x75, 0x01,         /*          Report Size (1),            */
	0x95, 0x01,         /*          Report Count (1),           */
	0x81, 0x02,         /*          Input (Variable),           */
	0x05, 0x01,         /*          Usage Page (Desktop),       */
	0x09, 0x30,         /*          Usage (X),                  */
	0x09, 0x31,         /*          Usage (Y),                  */
	0x75, 0x01,         /*          Report Size (1),            */
	0x95, 0x02,         /*          Report Count (2),           */
	0x81, 0x02,         /*          Input (Variable),           */
	0x75, 0x01,         /*          Report Size (1),            */
	0x95, 0x0B,         /*          Report Count (11),          */
	0x81, 0x01,         /*          Input (Constant),           */
	0x05, 0x01,         /*          Usage Page (Desktop),       */
	0x09, 0x38,         /*          Usage (Wheel),              */
	0x15, 0xFF,         /*          Logical Minimum (-1),       */
	0x25, 0x01,         /*          Logical Maximum (1),        */
	0x75, 0x02,         /*          Report Size (2),            */
	0x95, 0x01,         /*          Report Count (1),           */
	0x81, 0x06,         /*          Input (Variable, Relative), */
	0xC0,               /*      End Collection,                 */
	0xC0                /*  End Collection                      */
};
const size_t uclogic_rdesc_ugee_g5_frame_size =
			sizeof(uclogic_rdesc_ugee_g5_frame_arr);

/* Fixed report descriptor for XP-Pen Deco 01 frame controls */
const __u8 uclogic_rdesc_xppen_deco01_frame_arr[] = {
	0x05, 0x01, /*  Usage Page (Desktop),               */
	0x09, 0x07, /*  Usage (Keypad),                     */
	0xA1, 0x01, /*  Collection (Application),           */
	0x85, 0x06, /*      Report ID (6),                  */
	0x14,       /*      Logical Minimum (0),            */
	0x25, 0x01, /*      Logical Maximum (1),            */
	0x75, 0x01, /*      Report Size (1),                */
	0x05, 0x0D, /*      Usage Page (Digitizer),         */
	0x09, 0x39, /*      Usage (Tablet Function Keys),   */
	0xA0,       /*      Collection (Physical),          */
	0x05, 0x09, /*          Usage Page (Button),        */
	0x19, 0x01, /*          Usage Minimum (01h),        */
	0x29, 0x08, /*          Usage Maximum (08h),        */
	0x95, 0x08, /*          Report Count (8),           */
	0x81, 0x02, /*          Input (Variable),           */
	0x05, 0x0D, /*          Usage Page (Digitizer),     */
	0x09, 0x44, /*          Usage (Barrel Switch),      */
	0x95, 0x01, /*          Report Count (1),           */
	0x81, 0x02, /*          Input (Variable),           */
	0x05, 0x01, /*          Usage Page (Desktop),       */
	0x09, 0x30, /*          Usage (X),                  */
	0x09, 0x31, /*          Usage (Y),                  */
	0x95, 0x02, /*          Report Count (2),           */
	0x81, 0x02, /*          Input (Variable),           */
	0x95, 0x15, /*          Report Count (21),          */
	0x81, 0x01, /*          Input (Constant),           */
	0xC0,       /*      End Collection,                 */
	0xC0        /*  End Collection                      */
};

const size_t uclogic_rdesc_xppen_deco01_frame_size =
			sizeof(uclogic_rdesc_xppen_deco01_frame_arr);

/* Fixed report descriptor for XP-Pen Arist 22R Pro frame */
const __u8 uclogic_rdesc_xppen_artist_22r_pro_frame_arr[] = {
	0x05, 0x01,         /*  Usage Page (Desktop),                       */
	0x09, 0x07,         /*  Usage (Keypad),                             */
	0xA1, 0x01,         /*  Collection (Application),                   */
	0x85, UCLOGIC_RDESC_V1_FRAME_ID,
	/*      Report ID (Virtual report),             */
	0x05, 0x0D,         /*     Usage Page (Digitizer),                  */
	0x09, 0x39,         /*      Usage (Tablet Function Keys),           */
	0xA0,               /*      Collection (Physical),                  */
	0x14,               /*          Logical Minimum (0),                */
	0x25, 0x01,         /*          Logical Maximum (1),                */
	0x75, 0x01,         /*          Report Size (1),                    */
	0x95, 0x08,         /*          Report Count (8),                   */
	0x81, 0x01,         /*          Input (Constant),                   */
	0x05, 0x09,         /*          Usage Page (Button),                */
	0x19, 0x01,         /*          Usage Minimum (01h),                */
	0x29, 0x14,         /*          Usage Maximum (14h),                */
	0x95, 0x14,         /*          Report Count (20),                  */
	0x81, 0x02,         /*          Input (Variable),                   */
	0x95, 0x14,         /*          Report Count (20),                  */
	0x81, 0x01,         /*          Input (Constant),                   */
	0x05, 0x01,         /*          Usage Page (Desktop),               */
	0x09, 0x38,         /*          Usage (Wheel),                      */
	0x75, 0x08,         /*          Report Size (8),                    */
	0x95, 0x01,         /*          Report Count (1),                   */
	0x15, 0xFF,         /*          Logical Minimum (-1),               */
	0x25, 0x08,         /*          Logical Maximum (8),                */
	0x81, 0x06,         /*          Input (Variable, Relative),         */
	0x05, 0x0C,         /*          Usage Page (Consumer Devices),      */
	0x0A, 0x38, 0x02,   /*          Usage (AC PAN),                     */
	0x95, 0x01,         /*          Report Count (1),                   */
	0x81, 0x06,         /*          Input (Variable, Relative),         */
	0x26, 0xFF, 0x00,   /*          Logical Maximum (255),              */
	0x75, 0x08,         /*          Report Size (8),                    */
	0x95, 0x01,         /*          Report Count (1),                   */
	0x81, 0x02,         /*          Input (Variable),                   */
	0xC0,               /*      End Collection                          */
	0xC0,               /*  End Collection                              */
};

const size_t uclogic_rdesc_xppen_artist_22r_pro_frame_size =
				sizeof(uclogic_rdesc_xppen_artist_22r_pro_frame_arr);

/**
 * uclogic_rdesc_template_apply() - apply report descriptor parameters to a
 * report descriptor template, creating a report descriptor. Copies the
 * template over to the new report descriptor and replaces every occurrence of
 * the template placeholders, followed by an index byte, with the value from the
 * parameter list at that index.
 *
 * @template_ptr:	Pointer to the template buffer.
 * @template_size:	Size of the template buffer.
 * @param_list:		List of template parameters.
 * @param_num:		Number of parameters in the list.
 *
 * Returns:
 *	Kmalloc-allocated pointer to the created report descriptor,
 *	or NULL if allocation failed.
 */
__u8 *uclogic_rdesc_template_apply(const __u8 *template_ptr,
				   size_t template_size,
				   const s32 *param_list,
				   size_t param_num)
{
	static const __u8 btn_head[] = {UCLOGIC_RDESC_FRAME_PH_BTN_HEAD};
	static const __u8 pen_head[] = {UCLOGIC_RDESC_PEN_PH_HEAD};
	__u8 *rdesc_ptr;
	__u8 *p;
	s32 v;

	rdesc_ptr = kmemdup(template_ptr, template_size, GFP_KERNEL);
	if (rdesc_ptr == NULL)
		return NULL;

	for (p = rdesc_ptr; p + sizeof(btn_head) < rdesc_ptr + template_size;) {
		if (p + sizeof(pen_head) < rdesc_ptr + template_size &&
		    memcmp(p, pen_head, sizeof(pen_head)) == 0 &&
		    p[sizeof(pen_head)] < param_num) {
			v = param_list[p[sizeof(pen_head)]];
			put_unaligned((__force u32)cpu_to_le32(v), (s32 *)p);
			p += sizeof(pen_head) + 1;
		} else if (memcmp(p, btn_head, sizeof(btn_head)) == 0 &&
			   p[sizeof(btn_head)] < param_num) {
			v = param_list[p[sizeof(btn_head)]];
			put_unaligned((__u8)0x2A, p); /* Usage Maximum */
			put_unaligned((__force u16)cpu_to_le16(v), (s16 *)(p + 1));
			p += sizeof(btn_head) + 1;
		} else {
			p++;
		}
	}

	return rdesc_ptr;
}
EXPORT_SYMBOL_IF_KUNIT(uclogic_rdesc_template_apply);
