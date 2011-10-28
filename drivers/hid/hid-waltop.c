/*
 *  HID driver for Waltop devices not fully compliant with HID standard
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
 * There exists an official driver on the manufacturer's website, which
 * wasn't submitted to the kernel, for some reason. The official driver
 * doesn't seem to support extra features of some tablets, like wheels.
 *
 * It shows that the feature report ID 2 could be used to control any waltop
 * tablet input mode, switching it between "default", "tablet" and "ink".
 *
 * This driver only uses "default" mode for all the supported tablets. This
 * mode tries to be HID-compatible (not very successfully), but cripples the
 * resolution of some tablets.
 *
 * The "tablet" mode uses some proprietary, yet decipherable protocol, which
 * represents the correct resolution, but is possibly HID-incompatible (i.e.
 * indescribable by a report descriptor).
 *
 * The purpose of the "ink" mode is unknown.
 *
 * The feature reports needed for switching to each mode are these:
 *
 * 02 16 00     default
 * 02 16 01     tablet
 * 02 16 02     ink
 */

/*
 * Original Slim Tablet 5.8 inch report descriptor.
 *
 * All the reports except the report with ID 16 (the stylus) are unused,
 * possibly because the tablet is not configured to, or because they were
 * just copied from a more capable model. The full purpose of features
 * described for report ID 2 is unknown.
 *
 * The stylus buttons are described as three bit fields, whereas actually
 * it's an "array", i.e. they're reported as button numbers (1, 2 and 3).
 * The "eraser" field is not used. There is also a "push" without a "pop" in
 * the stylus description.
 *
 *  Usage Page (Desktop),           ; Generic desktop controls (01h)
 *  Usage (Mouse),                  ; Mouse (02h, application collection)
 *  Collection (Application),
 *    Report ID (1),
 *    Usage (Pointer),              ; Pointer (01h, physical collection)
 *    Collection (Physical),
 *      Usage Page (Button),        ; Button (09h)
 *      Usage Minimum (01h),
 *      Usage Maximum (05h),
 *      Logical Minimum (0),
 *      Logical Maximum (1),
 *      Report Size (1),
 *      Report Count (5),
 *      Input (Variable),
 *      Report Size (3),
 *      Report Count (1),
 *      Input (Constant, Variable),
 *      Usage Page (Desktop),       ; Generic desktop controls (01h)
 *      Usage (X),                  ; X (30h, dynamic value)
 *      Usage (Y),                  ; Y (31h, dynamic value)
 *      Usage (Wheel),              ; Wheel (38h, dynamic value)
 *      Logical Minimum (-127),
 *      Logical Maximum (127),
 *      Report Size (8),
 *      Report Count (3),
 *      Input (Variable, Relative),
 *    End Collection,
 *  End Collection,
 *  Usage Page (Digitizer),         ; Digitizer (0Dh)
 *  Usage (Pen),                    ; Pen (02h, application collection)
 *  Collection (Application),
 *    Report ID (2),
 *    Usage (Stylus),               ; Stylus (20h, logical collection)
 *    Collection (Physical),
 *      Usage (00h),
 *      Logical Minimum (0),
 *      Logical Maximum (255),
 *      Report Size (8),
 *      Report Count (7),
 *      Input (Variable),
 *      Usage (Azimuth),            ; Azimuth (3Fh, dynamic value)
 *      Usage (Altitude),           ; Altitude (40h, dynamic value)
 *      Logical Minimum (0),
 *      Logical Maximum (255),
 *      Report Size (8),
 *      Report Count (2),
 *      Feature (Variable),
 *    End Collection,
 *    Report ID (5),
 *    Usage Page (Digitizer),       ; Digitizer (0Dh)
 *    Usage (Stylus),               ; Stylus (20h, logical collection)
 *    Collection (Physical),
 *      Usage (00h),
 *      Logical Minimum (0),
 *      Logical Maximum (255),
 *      Report Size (8),
 *      Report Count (7),
 *      Input (Variable),
 *    End Collection,
 *    Report ID (10),
 *    Usage Page (Digitizer),       ; Digitizer (0Dh)
 *    Usage (Stylus),               ; Stylus (20h, logical collection)
 *    Collection (Physical),
 *      Usage (00h),
 *      Logical Minimum (0),
 *      Logical Maximum (255),
 *      Report Size (8),
 *      Report Count (3),
 *      Input (Variable),
 *    End Collection,
 *    Report ID (16),
 *    Usage (Stylus),               ; Stylus (20h, logical collection)
 *    Collection (Physical),
 *      Usage (Tip Switch),         ; Tip switch (42h, momentary control)
 *      Usage (Barrel Switch),      ; Barrel switch (44h, momentary control)
 *      Usage (Invert),             ; Invert (3Ch, momentary control)
 *      Usage (Eraser),             ; Eraser (45h, momentary control)
 *      Usage (In Range),           ; In range (32h, momentary control)
 *      Logical Minimum (0),
 *      Logical Maximum (1),
 *      Report Size (1),
 *      Report Count (5),
 *      Input (Variable),
 *      Report Count (3),
 *      Input (Constant, Variable),
 *      Usage Page (Desktop),       ; Generic desktop controls (01h)
 *      Usage (X),                  ; X (30h, dynamic value)
 *      Report Size (16),
 *      Report Count (1),
 *      Push,
 *      Unit Exponent (13),
 *      Unit (Inch^3),
 *      Logical Minimum (0),
 *      Logical Maximum (10000),
 *      Physical Minimum (0),
 *      Physical Maximum (10000),
 *      Input (Variable),
 *      Usage (Y),                  ; Y (31h, dynamic value)
 *      Logical Maximum (6000),
 *      Physical Maximum (6000),
 *      Input (Variable),
 *      Usage Page (Digitizer),     ; Digitizer (0Dh)
 *      Usage (Tip Pressure),       ; Tip pressure (30h, dynamic value)
 *      Logical Minimum (0),
 *      Logical Maximum (1023),
 *      Physical Minimum (0),
 *      Physical Maximum (1023),
 *      Input (Variable),
 *    End Collection,
 *  End Collection
 */

/* Size of the original report descriptor of Slim Tablet 5.8 inch */
#define SLIM_TABLET_5_8_INCH_RDESC_ORIG_SIZE	222

/*
 * Fixed Slim Tablet 5.8 inch descriptor.
 *
 * All the reports except the stylus report (ID 16) were removed as unused.
 * The stylus buttons description was fixed.
 */
static __u8 slim_tablet_5_8_inch_rdesc_fixed[] = {
	0x05, 0x0D,         /*  Usage Page (Digitizer),             */
	0x09, 0x02,         /*  Usage (Pen),                        */
	0xA1, 0x01,         /*  Collection (Application),           */
	0x85, 0x10,         /*      Report ID (16),                 */
	0x09, 0x20,         /*      Usage (Stylus),                 */
	0xA0,               /*      Collection (Physical),          */
	0x09, 0x42,         /*          Usage (Tip Switch),         */
	0x09, 0x44,         /*          Usage (Barrel Switch),      */
	0x09, 0x46,         /*          Usage (Tablet Pick),        */
	0x15, 0x01,         /*          Logical Minimum (1),        */
	0x25, 0x03,         /*          Logical Maximum (3),        */
	0x75, 0x04,         /*          Report Size (4),            */
	0x95, 0x01,         /*          Report Count (1),           */
	0x80,               /*          Input,                      */
	0x09, 0x32,         /*          Usage (In Range),           */
	0x14,               /*          Logical Minimum (0),        */
	0x25, 0x01,         /*          Logical Maximum (1),        */
	0x75, 0x01,         /*          Report Size (1),            */
	0x95, 0x01,         /*          Report Count (1),           */
	0x81, 0x02,         /*          Input (Variable),           */
	0x95, 0x03,         /*          Report Count (3),           */
	0x81, 0x03,         /*          Input (Constant, Variable), */
	0x75, 0x10,         /*          Report Size (16),           */
	0x95, 0x01,         /*          Report Count (1),           */
	0x14,               /*          Logical Minimum (0),        */
	0xA4,               /*          Push,                       */
	0x05, 0x01,         /*          Usage Page (Desktop),       */
	0x65, 0x13,         /*          Unit (Inch),                */
	0x55, 0xFD,         /*          Unit Exponent (-3),         */
	0x34,               /*          Physical Minimum (0),       */
	0x09, 0x30,         /*          Usage (X),                  */
	0x46, 0x88, 0x13,   /*          Physical Maximum (5000),    */
	0x26, 0x10, 0x27,   /*          Logical Maximum (10000),    */
	0x81, 0x02,         /*          Input (Variable),           */
	0x09, 0x31,         /*          Usage (Y),                  */
	0x46, 0xB8, 0x0B,   /*          Physical Maximum (3000),    */
	0x26, 0x70, 0x17,   /*          Logical Maximum (6000),     */
	0x81, 0x02,         /*          Input (Variable),           */
	0xB4,               /*          Pop,                        */
	0x09, 0x30,         /*          Usage (Tip Pressure),       */
	0x26, 0xFF, 0x03,   /*          Logical Maximum (1023),     */
	0x81, 0x02,         /*          Input (Variable),           */
	0xC0,               /*      End Collection,                 */
	0xC0                /*  End Collection                      */
};

/*
 * Original Slim Tablet 12.1 inch report descriptor.
 *
 * The descriptor is similar to the Slim Tablet 5.8 inch descriptor with the
 * addition of a keyboard report, seemingly unused. It may have get here
 * from a Media Tablet - probably an unimplemented feature.
 *
 *  Usage Page (Desktop),             ; Generic desktop controls (01h)
 *  Usage (Mouse),                    ; Mouse (02h, application collection)
 *  Collection (Application),
 *    Report ID (1),
 *    Usage (Pointer),                ; Pointer (01h, physical collection)
 *    Collection (Physical),
 *      Usage Page (Button),          ; Button (09h)
 *      Usage Minimum (01h),
 *      Usage Maximum (05h),
 *      Logical Minimum (0),
 *      Logical Maximum (1),
 *      Report Size (1),
 *      Report Count (5),
 *      Input (Variable),
 *      Report Size (3),
 *      Report Count (1),
 *      Input (Constant, Variable),
 *      Usage Page (Desktop),         ; Generic desktop controls (01h)
 *      Usage (X),                    ; X (30h, dynamic value)
 *      Usage (Y),                    ; Y (31h, dynamic value)
 *      Usage (Wheel),                ; Wheel (38h, dynamic value)
 *      Logical Minimum (-127),
 *      Logical Maximum (127),
 *      Report Size (8),
 *      Report Count (3),
 *      Input (Variable, Relative),
 *    End Collection,
 *  End Collection,
 *  Usage Page (Digitizer),           ; Digitizer (0Dh)
 *  Usage (Pen),                      ; Pen (02h, application collection)
 *  Collection (Application),
 *    Report ID (2),
 *    Usage (Stylus),                 ; Stylus (20h, logical collection)
 *    Collection (Physical),
 *      Usage (00h),
 *      Logical Minimum (0),
 *      Logical Maximum (255),
 *      Report Size (8),
 *      Report Count (7),
 *      Input (Variable),
 *      Usage (Azimuth),              ; Azimuth (3Fh, dynamic value)
 *      Usage (Altitude),             ; Altitude (40h, dynamic value)
 *      Logical Minimum (0),
 *      Logical Maximum (255),
 *      Report Size (8),
 *      Report Count (2),
 *      Feature (Variable),
 *    End Collection,
 *    Report ID (5),
 *    Usage Page (Digitizer),         ; Digitizer (0Dh)
 *    Usage (Stylus),                 ; Stylus (20h, logical collection)
 *    Collection (Physical),
 *      Usage (00h),
 *      Logical Minimum (0),
 *      Logical Maximum (255),
 *      Report Size (8),
 *      Report Count (7),
 *      Input (Variable),
 *    End Collection,
 *    Report ID (10),
 *    Usage Page (Digitizer),         ; Digitizer (0Dh)
 *    Usage (Stylus),                 ; Stylus (20h, logical collection)
 *    Collection (Physical),
 *      Usage (00h),
 *      Logical Minimum (0),
 *      Logical Maximum (255),
 *      Report Size (8),
 *      Report Count (3),
 *      Input (Variable),
 *    End Collection,
 *    Report ID (16),
 *    Usage (Stylus),                 ; Stylus (20h, logical collection)
 *    Collection (Physical),
 *      Usage (Tip Switch),           ; Tip switch (42h, momentary control)
 *      Usage (Barrel Switch),        ; Barrel switch (44h, momentary control)
 *      Usage (Invert),               ; Invert (3Ch, momentary control)
 *      Usage (Eraser),               ; Eraser (45h, momentary control)
 *      Usage (In Range),             ; In range (32h, momentary control)
 *      Logical Minimum (0),
 *      Logical Maximum (1),
 *      Report Size (1),
 *      Report Count (5),
 *      Input (Variable),
 *      Report Count (3),
 *      Input (Constant, Variable),
 *      Usage Page (Desktop),         ; Generic desktop controls (01h)
 *      Usage (X),                    ; X (30h, dynamic value)
 *      Report Size (16),
 *      Report Count (1),
 *      Push,
 *      Unit Exponent (13),
 *      Unit (Inch^3),
 *      Logical Minimum (0),
 *      Logical Maximum (20000),
 *      Physical Minimum (0),
 *      Physical Maximum (20000),
 *      Input (Variable),
 *      Usage (Y),                    ; Y (31h, dynamic value)
 *      Logical Maximum (12500),
 *      Physical Maximum (12500),
 *      Input (Variable),
 *      Usage Page (Digitizer),       ; Digitizer (0Dh)
 *      Usage (Tip Pressure),         ; Tip pressure (30h, dynamic value)
 *      Logical Minimum (0),
 *      Logical Maximum (1023),
 *      Physical Minimum (0),
 *      Physical Maximum (1023),
 *      Input (Variable),
 *    End Collection,
 *  End Collection,
 *  Usage Page (Desktop),             ; Generic desktop controls (01h)
 *  Usage (Keyboard),                 ; Keyboard (06h, application collection)
 *  Collection (Application),
 *    Report ID (13),
 *    Usage Page (Keyboard),          ; Keyboard/keypad (07h)
 *    Usage Minimum (KB Leftcontrol), ; Keyboard left control
 *                                    ; (E0h, dynamic value)
 *    Usage Maximum (KB Right GUI),   ; Keyboard right GUI (E7h, dynamic value)
 *    Logical Minimum (0),
 *    Logical Maximum (1),
 *    Report Size (1),
 *    Report Count (8),
 *    Input (Variable),
 *    Report Size (8),
 *    Report Count (1),
 *    Input (Constant),
 *    Usage Page (Keyboard),          ; Keyboard/keypad (07h)
 *    Usage Minimum (None),           ; No event (00h, selector)
 *    Usage Maximum (KB Application), ; Keyboard Application (65h, selector)
 *    Logical Minimum (0),
 *    Logical Maximum (101),
 *    Report Size (8),
 *    Report Count (5),
 *    Input,
 *  End Collection
 */

/* Size of the original report descriptor of Slim Tablet 12.1 inch */
#define SLIM_TABLET_12_1_INCH_RDESC_ORIG_SIZE	269

/*
 * Fixed Slim Tablet 12.1 inch descriptor.
 *
 * All the reports except the stylus report (ID 16) were removed as unused.
 * The stylus buttons description was fixed.
 */
static __u8 slim_tablet_12_1_inch_rdesc_fixed[] = {
	0x05, 0x0D,         /*  Usage Page (Digitizer),             */
	0x09, 0x02,         /*  Usage (Pen),                        */
	0xA1, 0x01,         /*  Collection (Application),           */
	0x85, 0x10,         /*      Report ID (16),                 */
	0x09, 0x20,         /*      Usage (Stylus),                 */
	0xA0,               /*      Collection (Physical),          */
	0x09, 0x42,         /*          Usage (Tip Switch),         */
	0x09, 0x44,         /*          Usage (Barrel Switch),      */
	0x09, 0x46,         /*          Usage (Tablet Pick),        */
	0x15, 0x01,         /*          Logical Minimum (1),        */
	0x25, 0x03,         /*          Logical Maximum (3),        */
	0x75, 0x04,         /*          Report Size (4),            */
	0x95, 0x01,         /*          Report Count (1),           */
	0x80,               /*          Input,                      */
	0x09, 0x32,         /*          Usage (In Range),           */
	0x14,               /*          Logical Minimum (0),        */
	0x25, 0x01,         /*          Logical Maximum (1),        */
	0x75, 0x01,         /*          Report Size (1),            */
	0x95, 0x01,         /*          Report Count (1),           */
	0x81, 0x02,         /*          Input (Variable),           */
	0x95, 0x03,         /*          Report Count (3),           */
	0x81, 0x03,         /*          Input (Constant, Variable), */
	0x75, 0x10,         /*          Report Size (16),           */
	0x95, 0x01,         /*          Report Count (1),           */
	0x14,               /*          Logical Minimum (0),        */
	0xA4,               /*          Push,                       */
	0x05, 0x01,         /*          Usage Page (Desktop),       */
	0x65, 0x13,         /*          Unit (Inch),                */
	0x55, 0xFD,         /*          Unit Exponent (-3),         */
	0x34,               /*          Physical Minimum (0),       */
	0x09, 0x30,         /*          Usage (X),                  */
	0x46, 0x10, 0x27,   /*          Physical Maximum (10000),   */
	0x26, 0x20, 0x4E,   /*          Logical Maximum (20000),    */
	0x81, 0x02,         /*          Input (Variable),           */
	0x09, 0x31,         /*          Usage (Y),                  */
	0x46, 0x6A, 0x18,   /*          Physical Maximum (6250),    */
	0x26, 0xD4, 0x30,   /*          Logical Maximum (12500),    */
	0x81, 0x02,         /*          Input (Variable),           */
	0xB4,               /*          Pop,                        */
	0x09, 0x30,         /*          Usage (Tip Pressure),       */
	0x26, 0xFF, 0x03,   /*          Logical Maximum (1023),     */
	0x81, 0x02,         /*          Input (Variable),           */
	0xC0,               /*      End Collection,                 */
	0xC0                /*  End Collection                      */
};

/*
 * Original Media Tablet 10.6 inch report descriptor.
 *
 * There are at least two versions of this model in the wild. They are
 * represented by Genius G-Pen M609 (older version) and Genius G-Pen M609X
 * (newer version).
 *
 * Both versions have the usual pen with two barrel buttons and two
 * identical wheels with center buttons in the top corners of the tablet
 * base. They also have buttons on the top, between the wheels, for
 * selecting the wheels' functions and wide/standard mode. In the wide mode
 * the whole working surface is sensed, in the standard mode a narrower area
 * is sensed, but the logical report extents remain the same. These modes
 * correspond roughly to 16:9 and 4:3 aspect ratios respectively.
 *
 * The older version has three wheel function buttons ("scroll", "zoom" and
 * "volume") and two separate buttons for wide and standard mode. The newer
 * version has four wheel function buttons (plus "brush") and only one
 * button is used for selecting wide/standard mode. So, the total number of
 * buttons remains the same, but one of the mode buttons is repurposed as a
 * wheels' function button in the newer version.
 *
 * The wheel functions are:
 * scroll   - the wheels act as scroll wheels, the center buttons switch
 *            between vertical and horizontal scrolling;
 * zoom     - the wheels zoom in/out, the buttons supposedly reset to 100%;
 * volume   - the wheels control the sound volume, the buttons mute;
 * brush    - the wheels are supposed to control brush width in a graphics
 *            editor, the buttons do nothing.
 *
 * Below is the newer version's report descriptor. It may very well be that
 * the older version's descriptor is different and thus it won't be
 * supported.
 *
 * The mouse report (ID 1) only uses the wheel field for reporting the tablet
 * wheels' scroll mode. The keyboard report (ID 13) is used to report the
 * wheels' zoom and brush control functions as key presses. The report ID 12
 * is used to report the wheels' volume control functions. The stylus report
 * (ID 16) has the same problems as the Slim Tablet 5.8 inch report has.
 *
 * The rest of the reports are unused, at least in the default configuration.
 * The purpose of the features is unknown.
 *
 *  Usage Page (Desktop),
 *  Usage (Mouse),
 *  Collection (Application),
 *    Report ID (1),
 *    Usage (Pointer),
 *    Collection (Physical),
 *      Usage Page (Button),
 *      Usage Minimum (01h),
 *      Usage Maximum (05h),
 *      Logical Minimum (0),
 *      Logical Maximum (1),
 *      Report Size (1),
 *      Report Count (5),
 *      Input (Variable),
 *      Report Size (3),
 *      Report Count (1),
 *      Input (Constant, Variable),
 *      Usage Page (Desktop),
 *      Usage (X),
 *      Usage (Y),
 *      Usage (Wheel),
 *      Logical Minimum (-127),
 *      Logical Maximum (127),
 *      Report Size (8),
 *      Report Count (3),
 *      Input (Variable, Relative),
 *    End Collection,
 *  End Collection,
 *  Usage Page (Digitizer),
 *  Usage (Pen),
 *  Collection (Application),
 *    Report ID (2),
 *    Usage (Stylus),
 *    Collection (Physical),
 *      Usage (00h),
 *      Logical Minimum (0),
 *      Logical Maximum (255),
 *      Report Size (8),
 *      Report Count (7),
 *      Input (Variable),
 *      Usage (Azimuth),
 *      Usage (Altitude),
 *      Logical Minimum (0),
 *      Logical Maximum (255),
 *      Report Size (8),
 *      Report Count (2),
 *      Feature (Variable),
 *    End Collection,
 *    Report ID (5),
 *    Usage Page (Digitizer),
 *    Usage (Stylus),
 *    Collection (Physical),
 *      Usage (00h),
 *      Logical Minimum (0),
 *      Logical Maximum (255),
 *      Report Size (8),
 *      Report Count (7),
 *      Input (Variable),
 *    End Collection,
 *    Report ID (10),
 *    Usage Page (Digitizer),
 *    Usage (Stylus),
 *    Collection (Physical),
 *      Usage (00h),
 *      Logical Minimum (0),
 *      Logical Maximum (255),
 *      Report Size (8),
 *      Report Count (7),
 *      Input (Variable),
 *    End Collection,
 *    Report ID (16),
 *    Usage (Stylus),
 *    Collection (Physical),
 *      Usage (Tip Switch),
 *      Usage (Barrel Switch),
 *      Usage (Invert),
 *      Usage (Eraser),
 *      Usage (In Range),
 *      Logical Minimum (0),
 *      Logical Maximum (1),
 *      Report Size (1),
 *      Report Count (5),
 *      Input (Variable),
 *      Report Count (3),
 *      Input (Constant, Variable),
 *      Usage Page (Desktop),
 *      Usage (X),
 *      Report Size (16),
 *      Report Count (1),
 *      Push,
 *      Unit Exponent (13),
 *      Unit (Inch^3),
 *      Logical Minimum (0),
 *      Logical Maximum (18000),
 *      Physical Minimum (0),
 *      Physical Maximum (18000),
 *      Input (Variable),
 *      Usage (Y),
 *      Logical Maximum (11000),
 *      Physical Maximum (11000),
 *      Input (Variable),
 *      Usage Page (Digitizer),
 *      Usage (Tip Pressure),
 *      Logical Minimum (0),
 *      Logical Maximum (1023),
 *      Physical Minimum (0),
 *      Physical Maximum (1023),
 *      Input (Variable),
 *    End Collection,
 *  End Collection,
 *  Usage Page (Desktop),
 *  Usage (Keyboard),
 *  Collection (Application),
 *    Report ID (13),
 *    Usage Page (Keyboard),
 *    Usage Minimum (KB Leftcontrol),
 *    Usage Maximum (KB Right GUI),
 *    Logical Minimum (0),
 *    Logical Maximum (1),
 *    Report Size (1),
 *    Report Count (8),
 *    Input (Variable),
 *    Report Size (8),
 *    Report Count (1),
 *    Input (Constant),
 *    Usage Page (Keyboard),
 *    Usage Minimum (None),
 *    Usage Maximum (KB Application),
 *    Logical Minimum (0),
 *    Logical Maximum (101),
 *    Report Size (8),
 *    Report Count (5),
 *    Input,
 *  End Collection,
 *  Usage Page (Consumer),
 *  Usage (Consumer Control),
 *  Collection (Application),
 *    Report ID (12),
 *    Usage (Volume Inc),
 *    Usage (Volume Dec),
 *    Usage (Mute),
 *    Logical Minimum (0),
 *    Logical Maximum (1),
 *    Report Size (1),
 *    Report Count (3),
 *    Input (Variable, Relative),
 *    Report Size (5),
 *    Report Count (1),
 *    Input (Constant, Variable, Relative),
 *  End Collection
 */

/* Size of the original report descriptor of Media Tablet 10.6 inch */
#define MEDIA_TABLET_10_6_INCH_RDESC_ORIG_SIZE	300

/*
 * Fixed Media Tablet 10.6 inch descriptor.
 *
 * The descriptions of reports unused in the default configuration are
 * removed. The stylus report (ID 16) is fixed similarly to Slim Tablet 5.8
 * inch.  The unused mouse report (ID 1) fields are replaced with constant
 * padding.
 *
 * The keyboard report (ID 13) is hacked to instead have an "array" field
 * reporting consumer page controls, and all the unused bits are masked out
 * with constant padding. The "brush" wheels' function is represented as "Scan
 * Previous/Next Track" controls due to the lack of brush controls in the
 * usage tables specification.
 */
static __u8 media_tablet_10_6_inch_rdesc_fixed[] = {
	0x05, 0x0D,         /*  Usage Page (Digitizer),             */
	0x09, 0x02,         /*  Usage (Pen),                        */
	0xA1, 0x01,         /*  Collection (Application),           */
	0x85, 0x10,         /*      Report ID (16),                 */
	0x09, 0x20,         /*      Usage (Stylus),                 */
	0xA0,               /*      Collection (Physical),          */
	0x09, 0x42,         /*          Usage (Tip Switch),         */
	0x09, 0x44,         /*          Usage (Barrel Switch),      */
	0x09, 0x46,         /*          Usage (Tablet Pick),        */
	0x15, 0x01,         /*          Logical Minimum (1),        */
	0x25, 0x03,         /*          Logical Maximum (3),        */
	0x75, 0x04,         /*          Report Size (4),            */
	0x95, 0x01,         /*          Report Count (1),           */
	0x80,               /*          Input,                      */
	0x75, 0x01,         /*          Report Size (1),            */
	0x09, 0x32,         /*          Usage (In Range),           */
	0x14,               /*          Logical Minimum (0),        */
	0x25, 0x01,         /*          Logical Maximum (1),        */
	0x95, 0x01,         /*          Report Count (1),           */
	0x81, 0x02,         /*          Input (Variable),           */
	0x95, 0x03,         /*          Report Count (3),           */
	0x81, 0x03,         /*          Input (Constant, Variable), */
	0x75, 0x10,         /*          Report Size (16),           */
	0x95, 0x01,         /*          Report Count (1),           */
	0x14,               /*          Logical Minimum (0),        */
	0xA4,               /*          Push,                       */
	0x05, 0x01,         /*          Usage Page (Desktop),       */
	0x65, 0x13,         /*          Unit (Inch),                */
	0x55, 0xFD,         /*          Unit Exponent (-3),         */
	0x34,               /*          Physical Minimum (0),       */
	0x09, 0x30,         /*          Usage (X),                  */
	0x46, 0x28, 0x23,   /*          Physical Maximum (9000),    */
	0x26, 0x50, 0x46,   /*          Logical Maximum (18000),    */
	0x81, 0x02,         /*          Input (Variable),           */
	0x09, 0x31,         /*          Usage (Y),                  */
	0x46, 0x7C, 0x15,   /*          Physical Maximum (5500),    */
	0x26, 0xF8, 0x2A,   /*          Logical Maximum (11000),    */
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
	0x85, 0x01,         /*      Report ID (1),                  */
	0x09, 0x01,         /*      Usage (Pointer),                */
	0xA0,               /*      Collection (Physical),          */
	0x75, 0x08,         /*          Report Size (8),            */
	0x95, 0x03,         /*          Report Count (3),           */
	0x81, 0x03,         /*          Input (Constant, Variable), */
	0x95, 0x02,         /*          Report Count (2),           */
	0x15, 0xFF,         /*          Logical Minimum (-1),       */
	0x25, 0x01,         /*          Logical Maximum (1),        */
	0x09, 0x38,         /*          Usage (Wheel),              */
	0x0B, 0x38, 0x02,   /*          Usage (Consumer AC Pan),    */
		0x0C, 0x00,
	0x81, 0x06,         /*          Input (Variable, Relative), */
	0x95, 0x02,         /*          Report Count (2),           */
	0x81, 0x03,         /*          Input (Constant, Variable), */
	0xC0,               /*      End Collection,                 */
	0xC0,               /*  End Collection,                     */
	0x05, 0x0C,         /*  Usage Page (Consumer),              */
	0x09, 0x01,         /*  Usage (Consumer Control),           */
	0xA1, 0x01,         /*  Collection (Application),           */
	0x85, 0x0D,         /*      Report ID (13),                 */
	0x95, 0x01,         /*      Report Count (1),               */
	0x75, 0x10,         /*      Report Size (16),               */
	0x81, 0x03,         /*      Input (Constant, Variable),     */
	0x0A, 0x2F, 0x02,   /*      Usage (AC Zoom),                */
	0x0A, 0x2E, 0x02,   /*      Usage (AC Zoom Out),            */
	0x0A, 0x2D, 0x02,   /*      Usage (AC Zoom In),             */
	0x09, 0xB6,         /*      Usage (Scan Previous Track),    */
	0x09, 0xB5,         /*      Usage (Scan Next Track),        */
	0x08,               /*      Usage (00h),                    */
	0x08,               /*      Usage (00h),                    */
	0x08,               /*      Usage (00h),                    */
	0x08,               /*      Usage (00h),                    */
	0x08,               /*      Usage (00h),                    */
	0x0A, 0x2E, 0x02,   /*      Usage (AC Zoom Out),            */
	0x0A, 0x2D, 0x02,   /*      Usage (AC Zoom In),             */
	0x15, 0x0C,         /*      Logical Minimum (12),           */
	0x25, 0x17,         /*      Logical Maximum (23),           */
	0x75, 0x05,         /*      Report Size (5),                */
	0x80,               /*      Input,                          */
	0x75, 0x03,         /*      Report Size (3),                */
	0x81, 0x03,         /*      Input (Constant, Variable),     */
	0x75, 0x20,         /*      Report Size (32),               */
	0x81, 0x03,         /*      Input (Constant, Variable),     */
	0xC0,               /*  End Collection,                     */
	0x09, 0x01,         /*  Usage (Consumer Control),           */
	0xA1, 0x01,         /*  Collection (Application),           */
	0x85, 0x0C,         /*      Report ID (12),                 */
	0x75, 0x01,         /*      Report Size (1),                */
	0x09, 0xE9,         /*      Usage (Volume Inc),             */
	0x09, 0xEA,         /*      Usage (Volume Dec),             */
	0x09, 0xE2,         /*      Usage (Mute),                   */
	0x14,               /*      Logical Minimum (0),            */
	0x25, 0x01,         /*      Logical Maximum (1),            */
	0x95, 0x03,         /*      Report Count (3),               */
	0x81, 0x06,         /*      Input (Variable, Relative),     */
	0x95, 0x35,         /*      Report Count (53),              */
	0x81, 0x03,         /*      Input (Constant, Variable),     */
	0xC0                /*  End Collection                      */
};

/*
 * Original Media Tablet 14.1 inch report descriptor.
 *
 * There are at least two versions of this model in the wild. They are
 * represented by Genius G-Pen M712 (older version) and Genius G-Pen M712X
 * (newer version). The hardware difference between these versions is the same
 * as between older and newer versions of Media Tablet 10.6 inch. The report
 * descriptors are identical for both versions.
 *
 * The function, behavior and report descriptor of this tablet is similar to
 * that of Media Tablet 10.6 inch. However, there is one more field (with
 * Consumer AC Pan usage) in the mouse description. Then the tablet X and Y
 * logical extents both get scaled to 0..16383 range (a hardware limit?),
 * which kind of defeats the advertised 4000 LPI resolution, considering the
 * physical extents of 12x7.25 inches. Plus, reports 5, 10 and 255 are used
 * sometimes (while moving the pen) with unknown purpose. Also, the key codes
 * generated for zoom in/out are different.
 *
 *  Usage Page (Desktop),
 *  Usage (Mouse),
 *  Collection (Application),
 *    Report ID (1),
 *    Usage (Pointer),
 *    Collection (Physical),
 *      Usage Page (Button),
 *      Usage Minimum (01h),
 *      Usage Maximum (05h),
 *      Logical Minimum (0),
 *      Logical Maximum (1),
 *      Report Size (1),
 *      Report Count (5),
 *      Input (Variable),
 *      Report Size (3),
 *      Report Count (1),
 *      Input (Constant, Variable),
 *      Usage Page (Desktop),
 *      Usage (X),
 *      Usage (Y),
 *      Usage (Wheel),
 *      Logical Minimum (-127),
 *      Logical Maximum (127),
 *      Report Size (8),
 *      Report Count (3),
 *      Input (Variable, Relative),
 *      Usage Page (Consumer),
 *      Logical Minimum (-127),
 *      Logical Maximum (127),
 *      Report Size (8),
 *      Report Count (1),
 *      Usage (AC Pan),
 *      Input (Variable, Relative),
 *    End Collection,
 *  End Collection,
 *  Usage Page (Digitizer),
 *  Usage (Pen),
 *  Collection (Application),
 *    Report ID (2),
 *    Usage (Stylus),
 *    Collection (Physical),
 *      Usage (00h),
 *      Logical Minimum (0),
 *      Logical Maximum (255),
 *      Report Size (8),
 *      Report Count (7),
 *      Input (Variable),
 *      Usage (Azimuth),
 *      Usage (Altitude),
 *      Logical Minimum (0),
 *      Logical Maximum (255),
 *      Report Size (8),
 *      Report Count (2),
 *      Feature (Variable),
 *    End Collection,
 *    Report ID (5),
 *    Usage Page (Digitizer),
 *    Usage (Stylus),
 *    Collection (Physical),
 *      Usage (00h),
 *      Logical Minimum (0),
 *      Logical Maximum (255),
 *      Report Size (8),
 *      Report Count (7),
 *      Input (Variable),
 *    End Collection,
 *    Report ID (10),
 *    Usage Page (Digitizer),
 *    Usage (Stylus),
 *    Collection (Physical),
 *      Usage (00h),
 *      Logical Minimum (0),
 *      Logical Maximum (255),
 *      Report Size (8),
 *      Report Count (7),
 *      Input (Variable),
 *    End Collection,
 *    Report ID (16),
 *    Usage (Stylus),
 *    Collection (Physical),
 *      Usage (Tip Switch),
 *      Usage (Barrel Switch),
 *      Usage (Invert),
 *      Usage (Eraser),
 *      Usage (In Range),
 *      Logical Minimum (0),
 *      Logical Maximum (1),
 *      Report Size (1),
 *      Report Count (5),
 *      Input (Variable),
 *      Report Count (3),
 *      Input (Constant, Variable),
 *      Usage Page (Desktop),
 *      Usage (X),
 *      Report Size (16),
 *      Report Count (1),
 *      Push,
 *      Unit Exponent (13),
 *      Unit (Inch^3),
 *      Logical Minimum (0),
 *      Logical Maximum (16383),
 *      Physical Minimum (0),
 *      Physical Maximum (16383),
 *      Input (Variable),
 *      Usage (Y),
 *      Input (Variable),
 *      Usage Page (Digitizer),
 *      Usage (Tip Pressure),
 *      Logical Minimum (0),
 *      Logical Maximum (1023),
 *      Physical Minimum (0),
 *      Physical Maximum (1023),
 *      Input (Variable),
 *    End Collection,
 *  End Collection,
 *  Usage Page (Desktop),
 *  Usage (Keyboard),
 *  Collection (Application),
 *    Report ID (13),
 *    Usage Page (Keyboard),
 *    Usage Minimum (KB Leftcontrol),
 *    Usage Maximum (KB Right GUI),
 *    Logical Minimum (0),
 *    Logical Maximum (1),
 *    Report Size (1),
 *    Report Count (8),
 *    Input (Variable),
 *    Report Size (8),
 *    Report Count (1),
 *    Input (Constant),
 *    Usage Page (Keyboard),
 *    Usage Minimum (None),
 *    Usage Maximum (KB Application),
 *    Logical Minimum (0),
 *    Logical Maximum (101),
 *    Report Size (8),
 *    Report Count (5),
 *    Input,
 *  End Collection,
 *  Usage Page (Consumer),
 *  Usage (Consumer Control),
 *  Collection (Application),
 *    Report ID (12),
 *    Usage (Volume Inc),
 *    Usage (Volume Dec),
 *    Usage (Mute),
 *    Logical Minimum (0),
 *    Logical Maximum (1),
 *    Report Size (1),
 *    Report Count (3),
 *    Input (Variable, Relative),
 *    Report Size (5),
 *    Report Count (1),
 *    Input (Constant, Variable, Relative),
 *  End Collection
 */

/* Size of the original report descriptor of Media Tablet 14.1 inch */
#define MEDIA_TABLET_14_1_INCH_RDESC_ORIG_SIZE	309

/*
 * Fixed Media Tablet 14.1 inch descriptor.
 * It is fixed similarly to the Media Tablet 10.6 inch descriptor.
 */
static __u8 media_tablet_14_1_inch_rdesc_fixed[] = {
	0x05, 0x0D,         /*  Usage Page (Digitizer),             */
	0x09, 0x02,         /*  Usage (Pen),                        */
	0xA1, 0x01,         /*  Collection (Application),           */
	0x85, 0x10,         /*      Report ID (16),                 */
	0x09, 0x20,         /*      Usage (Stylus),                 */
	0xA0,               /*      Collection (Physical),          */
	0x09, 0x42,         /*          Usage (Tip Switch),         */
	0x09, 0x44,         /*          Usage (Barrel Switch),      */
	0x09, 0x46,         /*          Usage (Tablet Pick),        */
	0x15, 0x01,         /*          Logical Minimum (1),        */
	0x25, 0x03,         /*          Logical Maximum (3),        */
	0x75, 0x04,         /*          Report Size (4),            */
	0x95, 0x01,         /*          Report Count (1),           */
	0x80,               /*          Input,                      */
	0x75, 0x01,         /*          Report Size (1),            */
	0x09, 0x32,         /*          Usage (In Range),           */
	0x14,               /*          Logical Minimum (0),        */
	0x25, 0x01,         /*          Logical Maximum (1),        */
	0x95, 0x01,         /*          Report Count (1),           */
	0x81, 0x02,         /*          Input (Variable),           */
	0x95, 0x03,         /*          Report Count (3),           */
	0x81, 0x03,         /*          Input (Constant, Variable), */
	0x75, 0x10,         /*          Report Size (16),           */
	0x95, 0x01,         /*          Report Count (1),           */
	0x14,               /*          Logical Minimum (0),        */
	0xA4,               /*          Push,                       */
	0x05, 0x01,         /*          Usage Page (Desktop),       */
	0x65, 0x13,         /*          Unit (Inch),                */
	0x55, 0xFD,         /*          Unit Exponent (-3),         */
	0x34,               /*          Physical Minimum (0),       */
	0x09, 0x30,         /*          Usage (X),                  */
	0x46, 0xE0, 0x2E,   /*          Physical Maximum (12000),   */
	0x26, 0xFF, 0x3F,   /*          Logical Maximum (16383),    */
	0x81, 0x02,         /*          Input (Variable),           */
	0x09, 0x31,         /*          Usage (Y),                  */
	0x46, 0x52, 0x1C,   /*          Physical Maximum (7250),    */
	0x26, 0xFF, 0x3F,   /*          Logical Maximum (16383),    */
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
	0x85, 0x01,         /*      Report ID (1),                  */
	0x09, 0x01,         /*      Usage (Pointer),                */
	0xA0,               /*      Collection (Physical),          */
	0x75, 0x08,         /*          Report Size (8),            */
	0x95, 0x03,         /*          Report Count (3),           */
	0x81, 0x03,         /*          Input (Constant, Variable), */
	0x95, 0x02,         /*          Report Count (2),           */
	0x15, 0xFF,         /*          Logical Minimum (-1),       */
	0x25, 0x01,         /*          Logical Maximum (1),        */
	0x09, 0x38,         /*          Usage (Wheel),              */
	0x0B, 0x38, 0x02,   /*          Usage (Consumer AC Pan),    */
		0x0C, 0x00,
	0x81, 0x06,         /*          Input (Variable, Relative), */
	0xC0,               /*      End Collection,                 */
	0xC0,               /*  End Collection,                     */
	0x05, 0x0C,         /*  Usage Page (Consumer),              */
	0x09, 0x01,         /*  Usage (Consumer Control),           */
	0xA1, 0x01,         /*  Collection (Application),           */
	0x85, 0x0D,         /*      Report ID (13),                 */
	0x95, 0x01,         /*      Report Count (1),               */
	0x75, 0x10,         /*      Report Size (16),               */
	0x81, 0x03,         /*      Input (Constant, Variable),     */
	0x0A, 0x2F, 0x02,   /*      Usage (AC Zoom),                */
	0x0A, 0x2E, 0x02,   /*      Usage (AC Zoom Out),            */
	0x0A, 0x2D, 0x02,   /*      Usage (AC Zoom In),             */
	0x09, 0xB6,         /*      Usage (Scan Previous Track),    */
	0x09, 0xB5,         /*      Usage (Scan Next Track),        */
	0x08,               /*      Usage (00h),                    */
	0x08,               /*      Usage (00h),                    */
	0x08,               /*      Usage (00h),                    */
	0x08,               /*      Usage (00h),                    */
	0x08,               /*      Usage (00h),                    */
	0x0A, 0x2E, 0x02,   /*      Usage (AC Zoom Out),            */
	0x0A, 0x2D, 0x02,   /*      Usage (AC Zoom In),             */
	0x15, 0x0C,         /*      Logical Minimum (12),           */
	0x25, 0x17,         /*      Logical Maximum (23),           */
	0x75, 0x05,         /*      Report Size (5),                */
	0x80,               /*      Input,                          */
	0x75, 0x03,         /*      Report Size (3),                */
	0x81, 0x03,         /*      Input (Constant, Variable),     */
	0x75, 0x20,         /*      Report Size (32),               */
	0x81, 0x03,         /*      Input (Constant, Variable),     */
	0xC0,               /*  End Collection,                     */
	0x09, 0x01,         /*  Usage (Consumer Control),           */
	0xA1, 0x01,         /*  Collection (Application),           */
	0x85, 0x0C,         /*      Report ID (12),                 */
	0x75, 0x01,         /*      Report Size (1),                */
	0x09, 0xE9,         /*      Usage (Volume Inc),             */
	0x09, 0xEA,         /*      Usage (Volume Dec),             */
	0x09, 0xE2,         /*      Usage (Mute),                   */
	0x14,               /*      Logical Minimum (0),            */
	0x25, 0x01,         /*      Logical Maximum (1),            */
	0x95, 0x03,         /*      Report Count (3),               */
	0x81, 0x06,         /*      Input (Variable, Relative),     */
	0x75, 0x05,         /*      Report Size (5),                */
	0x81, 0x03,         /*      Input (Constant, Variable),     */
	0xC0                /*  End Collection                      */
};

static __u8 *waltop_report_fixup(struct hid_device *hdev, __u8 *rdesc,
		unsigned int *rsize)
{
	switch (hdev->product) {
	case USB_DEVICE_ID_WALTOP_SLIM_TABLET_5_8_INCH:
		if (*rsize == SLIM_TABLET_5_8_INCH_RDESC_ORIG_SIZE) {
			rdesc = slim_tablet_5_8_inch_rdesc_fixed;
			*rsize = sizeof(slim_tablet_5_8_inch_rdesc_fixed);
		}
		break;
	case USB_DEVICE_ID_WALTOP_SLIM_TABLET_12_1_INCH:
		if (*rsize == SLIM_TABLET_12_1_INCH_RDESC_ORIG_SIZE) {
			rdesc = slim_tablet_12_1_inch_rdesc_fixed;
			*rsize = sizeof(slim_tablet_12_1_inch_rdesc_fixed);
		}
		break;
	case USB_DEVICE_ID_WALTOP_MEDIA_TABLET_10_6_INCH:
		if (*rsize == MEDIA_TABLET_10_6_INCH_RDESC_ORIG_SIZE) {
			rdesc = media_tablet_10_6_inch_rdesc_fixed;
			*rsize = sizeof(media_tablet_10_6_inch_rdesc_fixed);
		}
		break;
	case USB_DEVICE_ID_WALTOP_MEDIA_TABLET_14_1_INCH:
		if (*rsize == MEDIA_TABLET_14_1_INCH_RDESC_ORIG_SIZE) {
			rdesc = media_tablet_14_1_inch_rdesc_fixed;
			*rsize = sizeof(media_tablet_14_1_inch_rdesc_fixed);
		}
		break;
	}
	return rdesc;
}

static const struct hid_device_id waltop_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_WALTOP,
				USB_DEVICE_ID_WALTOP_SLIM_TABLET_5_8_INCH) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_WALTOP,
				USB_DEVICE_ID_WALTOP_SLIM_TABLET_12_1_INCH) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_WALTOP,
				USB_DEVICE_ID_WALTOP_MEDIA_TABLET_10_6_INCH) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_WALTOP,
				USB_DEVICE_ID_WALTOP_MEDIA_TABLET_14_1_INCH) },
	{ }
};
MODULE_DEVICE_TABLE(hid, waltop_devices);

static struct hid_driver waltop_driver = {
	.name = "waltop",
	.id_table = waltop_devices,
	.report_fixup = waltop_report_fixup,
};

static int __init waltop_init(void)
{
	return hid_register_driver(&waltop_driver);
}

static void __exit waltop_exit(void)
{
	hid_unregister_driver(&waltop_driver);
}

module_init(waltop_init);
module_exit(waltop_exit);
MODULE_LICENSE("GPL");
