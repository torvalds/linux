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

#include "hid-ids.h"

/*
 * The original descriptors of WPXXXXU tablets have three report IDs, of
 * which only two are used (8 and 9), and the remaining (7) seems to have
 * the originally intended pen description which was abandoned for some
 * reason.  From this unused description it is possible to extract the
 * actual physical extents and resolution. All the models use the same
 * descriptor with different extents for the unused report ID.
 *
 * Here it is:
 *
 *  Usage Page (Digitizer),         ; Digitizer (0Dh)
 *  Usage (Pen),                    ; Pen (02h, application collection)
 *  Collection (Application),
 *    Report ID (7),
 *    Usage (Stylus),               ; Stylus (20h, logical collection)
 *    Collection (Physical),
 *      Usage (Tip Switch),         ; Tip switch (42h, momentary control)
 *      Usage (Barrel Switch),      ; Barrel switch (44h, momentary control)
 *      Usage (Eraser),             ; Eraser (45h, momentary control)
 *      Logical Minimum (0),
 *      Logical Maximum (1),
 *      Report Size (1),
 *      Report Count (3),
 *      Input (Variable),
 *      Report Count (3),
 *      Input (Constant, Variable),
 *      Usage (In Range),           ; In range (32h, momentary control)
 *      Report Count (1),
 *      Input (Variable),
 *      Report Count (1),
 *      Input (Constant, Variable),
 *      Usage Page (Desktop),       ; Generic desktop controls (01h)
 *      Usage (X),                  ; X (30h, dynamic value)
 *      Report Size (16),
 *      Report Count (1),
 *      Push,
 *      Unit Exponent (13),
 *      Unit (Inch^3),
 *      Physical Minimum (0),
 *      Physical Maximum (Xpm),
 *      Logical Maximum (Xlm),
 *      Input (Variable),
 *      Usage (Y),                  ; Y (31h, dynamic value)
 *      Physical Maximum (Ypm),
 *      Logical Maximum (Ylm),
 *      Input (Variable),
 *      Pop,
 *      Usage Page (Digitizer),     ; Digitizer (0Dh)
 *      Usage (Tip Pressure),       ; Tip pressure (30h, dynamic value)
 *      Logical Maximum (1023),
 *      Input (Variable),
 *      Report Size (16),
 *    End Collection,
 *  End Collection,
 *  Usage Page (Desktop),           ; Generic desktop controls (01h)
 *  Usage (Mouse),                  ; Mouse (02h, application collection)
 *  Collection (Application),
 *    Report ID (8),
 *    Usage (Pointer),              ; Pointer (01h, physical collection)
 *    Collection (Physical),
 *      Usage Page (Button),        ; Button (09h)
 *      Usage Minimum (01h),
 *      Usage Maximum (03h),
 *      Logical Minimum (0),
 *      Logical Maximum (1),
 *      Report Count (3),
 *      Report Size (1),
 *      Input (Variable),
 *      Report Count (5),
 *      Input (Constant),
 *      Usage Page (Desktop),       ; Generic desktop controls (01h)
 *      Usage (X),                  ; X (30h, dynamic value)
 *      Usage (Y),                  ; Y (31h, dynamic value)
 *      Usage (Wheel),              ; Wheel (38h, dynamic value)
 *      Usage (00h),
 *      Logical Minimum (-127),
 *      Logical Maximum (127),
 *      Report Size (8),
 *      Report Count (4),
 *      Input (Variable, Relative),
 *    End Collection,
 *  End Collection,
 *  Usage Page (Desktop),           ; Generic desktop controls (01h)
 *  Usage (Mouse),                  ; Mouse (02h, application collection)
 *  Collection (Application),
 *    Report ID (9),
 *    Usage (Pointer),              ; Pointer (01h, physical collection)
 *    Collection (Physical),
 *      Usage Page (Button),        ; Button (09h)
 *      Usage Minimum (01h),
 *      Usage Maximum (03h),
 *      Logical Minimum (0),
 *      Logical Maximum (1),
 *      Report Count (3),
 *      Report Size (1),
 *      Input (Variable),
 *      Report Count (5),
 *      Input (Constant),
 *      Usage Page (Desktop),       ; Generic desktop controls (01h)
 *      Usage (X),                  ; X (30h, dynamic value)
 *      Usage (Y),                  ; Y (31h, dynamic value)
 *      Logical Minimum (0),
 *      Logical Maximum (32767),
 *      Physical Minimum (0),
 *      Physical Maximum (32767),
 *      Report Count (2),
 *      Report Size (16),
 *      Input (Variable),
 *      Usage Page (Digitizer),     ; Digitizer (0Dh)
 *      Usage (Tip Pressure),       ; Tip pressure (30h, dynamic value)
 *      Logical Maximum (1023),
 *      Report Count (1),
 *      Report Size (16),
 *      Input (Variable),
 *    End Collection,
 *  End Collection
 *
 * Here are the extents values for the WPXXXXU models:
 *
 *              Xpm     Xlm     Ypm     Ylm
 *  WP4030U     4000    8000    3000    6000
 *  WP5540U     5500    11000   4000    8000
 *  WP8060U     8000    16000   6000    12000
 *
 * This suggests that all of them have 2000 LPI resolution, as advertised.
 */

/* Size of the original descriptor of WPXXXXU tablets */
#define WPXXXXU_RDESC_ORIG_SIZE	212

/*
 * Fixed WP4030U report descriptor.
 * Although the hardware might actually support it, the mouse description
 * has been removed, since there seems to be no devices having one and it
 * wouldn't make much sense because of the working area size.
 */
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
 * Original PF1209 report descriptor.
 *
 * The descriptor is similar to WPXXXXU descriptors, with an addition of a
 * feature report (ID 4) of unknown purpose.
 *
 * Although the advertised resolution is 4000 LPI the unused report ID
 * (taken from WPXXXXU, it seems) states 2000 LPI, but it is probably
 * incorrect and is a result of blind copying without understanding. Anyway
 * the real logical extents are always scaled to 0..32767, which IMHO spoils
 * the precision.
 *
 *  Usage Page (Digitizer),         ; Digitizer (0Dh)
 *  Usage (Pen),                    ; Pen (02h, application collection)
 *  Collection (Application),
 *    Report ID (7),
 *    Usage (Stylus),               ; Stylus (20h, logical collection)
 *    Collection (Physical),
 *      Usage (Tip Switch),         ; Tip switch (42h, momentary control)
 *      Usage (Barrel Switch),      ; Barrel switch (44h, momentary control)
 *      Usage (Eraser),             ; Eraser (45h, momentary control)
 *      Logical Minimum (0),
 *      Logical Maximum (1),
 *      Report Size (1),
 *      Report Count (3),
 *      Input (Variable),
 *      Report Count (3),
 *      Input (Constant, Variable),
 *      Usage (In Range),           ; In range (32h, momentary control)
 *      Report Count (1),
 *      Input (Variable),
 *      Report Count (1),
 *      Input (Constant, Variable),
 *      Usage Page (Desktop),       ; Generic desktop controls (01h)
 *      Usage (X),                  ; X (30h, dynamic value)
 *      Report Size (16),
 *      Report Count (1),
 *      Push,
 *      Unit Exponent (13),
 *      Unit (Inch^3),
 *      Physical Minimum (0),
 *      Physical Maximum (12000),
 *      Logical Maximum (24000),
 *      Input (Variable),
 *      Usage (Y),                  ; Y (31h, dynamic value)
 *      Physical Maximum (9000),
 *      Logical Maximum (18000),
 *      Input (Variable),
 *      Pop,
 *      Usage Page (Digitizer),     ; Digitizer (0Dh)
 *      Usage (Tip Pressure),       ; Tip pressure (30h, dynamic value)
 *      Logical Maximum (1023),
 *      Input (Variable),
 *      Report Size (16),
 *    End Collection,
 *  End Collection,
 *  Usage Page (Desktop),           ; Generic desktop controls (01h)
 *  Usage (Mouse),                  ; Mouse (02h, application collection)
 *  Collection (Application),
 *    Report ID (8),
 *    Usage (Pointer),              ; Pointer (01h, physical collection)
 *    Collection (Physical),
 *      Usage Page (Button),        ; Button (09h)
 *      Usage Minimum (01h),
 *      Usage Maximum (03h),
 *      Logical Minimum (0),
 *      Logical Maximum (1),
 *      Report Count (3),
 *      Report Size (1),
 *      Input (Variable),
 *      Report Count (5),
 *      Input (Constant),
 *      Usage Page (Desktop),       ; Generic desktop controls (01h)
 *      Usage (X),                  ; X (30h, dynamic value)
 *      Usage (Y),                  ; Y (31h, dynamic value)
 *      Usage (Wheel),              ; Wheel (38h, dynamic value)
 *      Usage (00h),
 *      Logical Minimum (-127),
 *      Logical Maximum (127),
 *      Report Size (8),
 *      Report Count (4),
 *      Input (Variable, Relative),
 *    End Collection,
 *  End Collection,
 *  Usage Page (Desktop),           ; Generic desktop controls (01h)
 *  Usage (Mouse),                  ; Mouse (02h, application collection)
 *  Collection (Application),
 *    Report ID (9),
 *    Usage (Pointer),              ; Pointer (01h, physical collection)
 *    Collection (Physical),
 *      Usage Page (Button),        ; Button (09h)
 *      Usage Minimum (01h),
 *      Usage Maximum (03h),
 *      Logical Minimum (0),
 *      Logical Maximum (1),
 *      Report Count (3),
 *      Report Size (1),
 *      Input (Variable),
 *      Report Count (5),
 *      Input (Constant),
 *      Usage Page (Desktop),       ; Generic desktop controls (01h)
 *      Usage (X),                  ; X (30h, dynamic value)
 *      Usage (Y),                  ; Y (31h, dynamic value)
 *      Logical Minimum (0),
 *      Logical Maximum (32767),
 *      Physical Minimum (0),
 *      Physical Maximum (32767),
 *      Report Count (2),
 *      Report Size (16),
 *      Input (Variable),
 *      Usage Page (Digitizer),     ; Digitizer (0Dh)
 *      Usage (Tip Pressure),       ; Tip pressure (30h, dynamic value)
 *      Logical Maximum (1023),
 *      Report Count (1),
 *      Report Size (16),
 *      Input (Variable),
 *    End Collection,
 *  End Collection,
 *  Usage Page (Desktop),           ; Generic desktop controls (01h)
 *  Usage (00h),
 *  Collection (Application),
 *    Report ID (4),
 *    Logical Minimum (0),
 *    Logical Maximum (255),
 *    Usage (00h),
 *    Report Size (8),
 *    Report Count (3),
 *    Feature (Variable),
 *  End Collection
 */

/* Size of the original descriptor of PF1209 tablet */
#define PF1209_RDESC_ORIG_SIZE	234

/*
 * Fixed PF1209 report descriptor
 *
 * The descriptor is fixed similarly to WP5540U and WP8060U, plus the
 * feature report is removed, because its purpose is unknown and it is of no
 * use to the generic HID driver anyway for now.
 */
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

static __u8 *uclogic_report_fixup(struct hid_device *hdev, __u8 *rdesc,
					unsigned int *rsize)
{
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
	{ }
};
MODULE_DEVICE_TABLE(hid, uclogic_devices);

static struct hid_driver uclogic_driver = {
	.name = "uclogic",
	.id_table = uclogic_devices,
	.report_fixup = uclogic_report_fixup,
};

static int __init uclogic_init(void)
{
	return hid_register_driver(&uclogic_driver);
}

static void __exit uclogic_exit(void)
{
	hid_unregister_driver(&uclogic_driver);
}

module_init(uclogic_init);
module_exit(uclogic_exit);
MODULE_LICENSE("GPL");
