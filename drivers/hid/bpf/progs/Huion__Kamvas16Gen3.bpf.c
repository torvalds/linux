// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2025 Nicholas LaPointe
 * Copyright (c) 2025 Higgins Dragon
 */

#include "vmlinux.h"
#include "hid_bpf.h"
#include "hid_bpf_helpers.h"
#include "hid_report_helpers.h"
#include <bpf/bpf_tracing.h>

#define VID_HUION 0x256c
#define PID_KAMVAS16_GEN3 0x2009

#define VENDOR_DESCRIPTOR_LENGTH 36
#define TABLET_DESCRIPTOR_LENGTH 328
#define WHEEL_DESCRIPTOR_LENGTH 200

#define VENDOR_REPORT_ID 8
#define VENDOR_REPORT_LENGTH 14

#define VENDOR_REPORT_SUBTYPE_PEN 0x08
#define VENDOR_REPORT_SUBTYPE_PEN_OUT 0x00
#define VENDOR_REPORT_SUBTYPE_BUTTONS 0x0e
#define VENDOR_REPORT_SUBTYPE_WHEELS 0x0f

/* For the reports that we create ourselves */
#define CUSTOM_PAD_REPORT_ID 9

HID_BPF_CONFIG(
	HID_DEVICE(BUS_USB, HID_GROUP_ANY, VID_HUION, PID_KAMVAS16_GEN3),
);

/*
 * This tablet can send reports using one of two different data formats,
 * depending on what "mode" the tablet is in.
 *
 * By default, the tablet will send reports that can be decoded using its
 * included HID descriptors (descriptors 1 and 2, shown below).
 * This mode will be called "firmware mode" throughout this file.
 *
 * The HID descriptor that describes pen events in firmware mode (descriptor 1)
 * has multiple bugs:
 *	* "Secondary Tip Switch" instead of "Secondary Barrel Switch"
 *	* "Invert" instead of (or potentially shared with) third barrel button
 *	* Specified tablet area of 2048 in³ instead of 293.8 x 165.2mm
 *	* Specified tilt range of -90 to +90 instead of -60 to +60
 *
 * While these can be easily patched up by editing the descriptor, a larger
 * problem with the firmware mode exists: it is impossible to tell which of the
 * two wheels are being rotated (or having their central button pressed).
 *
 *
 * By using a tool such as huion-switcher (https://github.com/whot/huion-switcher),
 * the tablet can be made to send reports using a proprietary format that is not
 * adequately described by its relevant descriptor (descriptor 0, shown below).
 * This mode will be called "vendor mode" throughout this file.
 *
 * The reports sent while in vendor mode allow for proper decoding of the wheels.
 *
 * For simplicity and maximum functionality, this BPF focuses strictly on
 * enabling one to make use of the vendor mode.
 */

/*
 * DESCRIPTORS
 *	DESCRIPTOR 0
 *		# 0x06, 0x00, 0xff,              // Usage Page (Vendor Defined Page 1)  0
 *		# 0x09, 0x01,                    // Usage (Vendor Usage 1)              3
 *		# 0xa1, 0x01,                    // Collection (Application)            5
 *		# 0x85, 0x08,                    //  Report ID (8)                      7
 *		# 0x75, 0x68,                    //  Report Size (104)                  9
 *		# 0x95, 0x01,                    //  Report Count (1)                   11
 *		# 0x09, 0x01,                    //  Usage (Vendor Usage 1)             13
 *		# 0x81, 0x02,                    //  Input (Data,Var,Abs)               15
 *		# 0xc0,                          // End Collection                      17
 *		# 0x06, 0x00, 0xff,              // Usage Page (Vendor Defined Page 1)  18
 *		# 0x09, 0x01,                    // Usage (Vendor Usage 1)              21
 *		# 0xa1, 0x01,                    // Collection (Application)            23
 *		# 0x85, 0x16,                    //  Report ID (22)                     25
 *		# 0x75, 0x08,                    //  Report Size (8)                    27
 *		# 0x95, 0x07,                    //  Report Count (7)                   29
 *		# 0x09, 0x01,                    //  Usage (Vendor Usage 1)             31
 *		# 0xb1, 0x02,                    //  Feature (Data,Var,Abs)             33
 *		# 0xc0,                          // End Collection                      35
 *		#
 *		R: 36 06 00 ff 09 01 a1 01 85 08 75 68 95 01 09 01 81 02 c0 06 00 ff 09 01 a1 01 85 16 75 08 95 07 09 01 b1 02 c0
 *		N: HUION Huion Tablet_GS1563
 *		I: 3 256c 2009
 *
 *
 *	DESCRIPTOR 1
 *		# 0x05, 0x0d,                    // Usage Page (Digitizers)             0
 *		# 0x09, 0x02,                    // Usage (Pen)                         2
 *		# 0xa1, 0x01,                    // Collection (Application)            4
 *		# 0x85, 0x0a,                    //  Report ID (10)                     6
 *		# 0x09, 0x20,                    //  Usage (Stylus)                     8
 *		# 0xa1, 0x01,                    //  Collection (Application)           10
 *		# 0x09, 0x42,                    //   Usage (Tip Switch)                12
 *		# 0x09, 0x44,                    //   Usage (Barrel Switch)             14
 *		# 0x09, 0x43,                    //   Usage (Secondary Tip Switch)      16
 *		# 0x09, 0x3c,                    //   Usage (Invert)                    18
 *		# 0x09, 0x45,                    //   Usage (Eraser)                    20
 *		# 0x15, 0x00,                    //   Logical Minimum (0)               22
 *		# 0x25, 0x01,                    //   Logical Maximum (1)               24
 *		# 0x75, 0x01,                    //   Report Size (1)                   26
 *		# 0x95, 0x06,                    //   Report Count (6)                  28
 *		# 0x81, 0x02,                    //   Input (Data,Var,Abs)              30
 *		# 0x09, 0x32,                    //   Usage (In Range)                  32
 *		# 0x75, 0x01,                    //   Report Size (1)                   34
 *		# 0x95, 0x01,                    //   Report Count (1)                  36
 *		# 0x81, 0x02,                    //   Input (Data,Var,Abs)              38
 *		# 0x81, 0x03,                    //   Input (Cnst,Var,Abs)              40
 *		# 0x05, 0x01,                    //   Usage Page (Generic Desktop)      42
 *		# 0x09, 0x30,                    //   Usage (X)                         44
 *		# 0x09, 0x31,                    //   Usage (Y)                         46
 *		# 0x55, 0x0d,                    //   Unit Exponent (-3)                48
 *		# 0x65, 0x33,                    //   Unit (EnglishLinear: in³)         50
 *		# 0x26, 0xff, 0x7f,              //   Logical Maximum (32767)           52
 *		# 0x35, 0x00,                    //   Physical Minimum (0)              55
 *		# 0x46, 0x00, 0x08,              //   Physical Maximum (2048)           57
 *		# 0x75, 0x10,                    //   Report Size (16)                  60
 *		# 0x95, 0x02,                    //   Report Count (2)                  62
 *		# 0x81, 0x02,                    //   Input (Data,Var,Abs)              64
 *		# 0x05, 0x0d,                    //   Usage Page (Digitizers)           66
 *		# 0x09, 0x30,                    //   Usage (Tip Pressure)              68
 *		# 0x26, 0xff, 0x3f,              //   Logical Maximum (16383)           70
 *		# 0x75, 0x10,                    //   Report Size (16)                  73
 *		# 0x95, 0x01,                    //   Report Count (1)                  75
 *		# 0x81, 0x02,                    //   Input (Data,Var,Abs)              77
 *		# 0x09, 0x3d,                    //   Usage (X Tilt)                    79
 *		# 0x09, 0x3e,                    //   Usage (Y Tilt)                    81
 *		# 0x15, 0xa6,                    //   Logical Minimum (-90)             83
 *		# 0x25, 0x5a,                    //   Logical Maximum (90)              85
 *		# 0x75, 0x08,                    //   Report Size (8)                   87
 *		# 0x95, 0x02,                    //   Report Count (2)                  89
 *		# 0x81, 0x02,                    //   Input (Data,Var,Abs)              91
 *		# 0xc0,                          //  End Collection                     93
 *		# 0xc0,                          // End Collection                      94
 *		# 0x05, 0x0d,                    // Usage Page (Digitizers)             95
 *		# 0x09, 0x04,                    // Usage (Touch Screen)                97
 *		# 0xa1, 0x01,                    // Collection (Application)            99
 *		# 0x85, 0x04,                    //  Report ID (4)                      101
 *		# 0x09, 0x22,                    //  Usage (Finger)                     103
 *		# 0xa1, 0x02,                    //  Collection (Logical)               105
 *		# 0x05, 0x0d,                    //   Usage Page (Digitizers)           107
 *		# 0x95, 0x01,                    //   Report Count (1)                  109
 *		# 0x75, 0x06,                    //   Report Size (6)                   111
 *		# 0x09, 0x51,                    //   Usage (Contact Id)                113
 *		# 0x15, 0x00,                    //   Logical Minimum (0)               115
 *		# 0x25, 0x3f,                    //   Logical Maximum (63)              117
 *		# 0x81, 0x02,                    //   Input (Data,Var,Abs)              119
 *		# 0x09, 0x42,                    //   Usage (Tip Switch)                121
 *		# 0x25, 0x01,                    //   Logical Maximum (1)               123
 *		# 0x75, 0x01,                    //   Report Size (1)                   125
 *		# 0x95, 0x01,                    //   Report Count (1)                  127
 *		# 0x81, 0x02,                    //   Input (Data,Var,Abs)              129
 *		# 0x75, 0x01,                    //   Report Size (1)                   131
 *		# 0x95, 0x01,                    //   Report Count (1)                  133
 *		# 0x81, 0x03,                    //   Input (Cnst,Var,Abs)              135
 *		# 0x05, 0x01,                    //   Usage Page (Generic Desktop)      137
 *		# 0x75, 0x10,                    //   Report Size (16)                  139
 *		# 0x55, 0x0e,                    //   Unit Exponent (-2)                141
 *		# 0x65, 0x11,                    //   Unit (SILinear: cm)               143
 *		# 0x09, 0x30,                    //   Usage (X)                         145
 *		# 0x26, 0xff, 0x7f,              //   Logical Maximum (32767)           147
 *		# 0x35, 0x00,                    //   Physical Minimum (0)              150
 *		# 0x46, 0x15, 0x0c,              //   Physical Maximum (3093)           152
 *		# 0x81, 0x42,                    //   Input (Data,Var,Abs,Null)         155
 *		# 0x09, 0x31,                    //   Usage (Y)                         157
 *		# 0x26, 0xff, 0x7f,              //   Logical Maximum (32767)           159
 *		# 0x46, 0xcb, 0x06,              //   Physical Maximum (1739)           162
 *		# 0x81, 0x42,                    //   Input (Data,Var,Abs,Null)         165
 *		# 0x05, 0x0d,                    //   Usage Page (Digitizers)           167
 *		# 0x09, 0x30,                    //   Usage (Tip Pressure)              169
 *		# 0x26, 0xff, 0x1f,              //   Logical Maximum (8191)            171
 *		# 0x75, 0x10,                    //   Report Size (16)                  174
 *		# 0x95, 0x01,                    //   Report Count (1)                  176
 *		# 0x81, 0x02,                    //   Input (Data,Var,Abs)              178
 *		# 0xc0,                          //  End Collection                     180
 *		# 0x05, 0x0d,                    //  Usage Page (Digitizers)            181
 *		# 0x09, 0x22,                    //  Usage (Finger)                     183
 *		# 0xa1, 0x02,                    //  Collection (Logical)               185
 *		# 0x05, 0x0d,                    //   Usage Page (Digitizers)           187
 *		# 0x95, 0x01,                    //   Report Count (1)                  189
 *		# 0x75, 0x06,                    //   Report Size (6)                   191
 *		# 0x09, 0x51,                    //   Usage (Contact Id)                193
 *		# 0x15, 0x00,                    //   Logical Minimum (0)               195
 *		# 0x25, 0x3f,                    //   Logical Maximum (63)              197
 *		# 0x81, 0x02,                    //   Input (Data,Var,Abs)              199
 *		# 0x09, 0x42,                    //   Usage (Tip Switch)                201
 *		# 0x25, 0x01,                    //   Logical Maximum (1)               203
 *		# 0x75, 0x01,                    //   Report Size (1)                   205
 *		# 0x95, 0x01,                    //   Report Count (1)                  207
 *		# 0x81, 0x02,                    //   Input (Data,Var,Abs)              209
 *		# 0x75, 0x01,                    //   Report Size (1)                   211
 *		# 0x95, 0x01,                    //   Report Count (1)                  213
 *		# 0x81, 0x03,                    //   Input (Cnst,Var,Abs)              215
 *		# 0x05, 0x01,                    //   Usage Page (Generic Desktop)      217
 *		# 0x75, 0x10,                    //   Report Size (16)                  219
 *		# 0x55, 0x0e,                    //   Unit Exponent (-2)                221
 *		# 0x65, 0x11,                    //   Unit (SILinear: cm)               223
 *		# 0x09, 0x30,                    //   Usage (X)                         225
 *		# 0x26, 0xff, 0x7f,              //   Logical Maximum (32767)           227
 *		# 0x35, 0x00,                    //   Physical Minimum (0)              230
 *		# 0x46, 0x15, 0x0c,              //   Physical Maximum (3093)           232
 *		# 0x81, 0x42,                    //   Input (Data,Var,Abs,Null)         235
 *		# 0x09, 0x31,                    //   Usage (Y)                         237
 *		# 0x26, 0xff, 0x7f,              //   Logical Maximum (32767)           239
 *		# 0x46, 0xcb, 0x06,              //   Physical Maximum (1739)           242
 *		# 0x81, 0x42,                    //   Input (Data,Var,Abs,Null)         245
 *		# 0x05, 0x0d,                    //   Usage Page (Digitizers)           247
 *		# 0x09, 0x30,                    //   Usage (Tip Pressure)              249
 *		# 0x26, 0xff, 0x1f,              //   Logical Maximum (8191)            251
 *		# 0x75, 0x10,                    //   Report Size (16)                  254
 *		# 0x95, 0x01,                    //   Report Count (1)                  256
 *		# 0x81, 0x02,                    //   Input (Data,Var,Abs)              258
 *		# 0xc0,                          //  End Collection                     260
 *		# 0x05, 0x0d,                    //  Usage Page (Digitizers)            261
 *		# 0x09, 0x56,                    //  Usage (Scan Time)                  263
 *		# 0x55, 0x00,                    //  Unit Exponent (0)                  265
 *		# 0x65, 0x00,                    //  Unit (None)                        267
 *		# 0x27, 0xff, 0xff, 0xff, 0x7f,  //  Logical Maximum (2147483647)       269
 *		# 0x95, 0x01,                    //  Report Count (1)                   274
 *		# 0x75, 0x20,                    //  Report Size (32)                   276
 *		# 0x81, 0x02,                    //  Input (Data,Var,Abs)               278
 *		# 0x09, 0x54,                    //  Usage (Contact Count)              280
 *		# 0x25, 0x7f,                    //  Logical Maximum (127)              282
 *		# 0x95, 0x01,                    //  Report Count (1)                   284
 *		# 0x75, 0x08,                    //  Report Size (8)                    286
 *		# 0x81, 0x02,                    //  Input (Data,Var,Abs)               288
 *		# 0x75, 0x08,                    //  Report Size (8)                    290
 *		# 0x95, 0x08,                    //  Report Count (8)                   292
 *		# 0x81, 0x03,                    //  Input (Cnst,Var,Abs)               294
 *		# 0x85, 0x05,                    //  Report ID (5)                      296
 *		# 0x09, 0x55,                    //  Usage (Contact Max)                298
 *		# 0x25, 0x0a,                    //  Logical Maximum (10)               300
 *		# 0x75, 0x08,                    //  Report Size (8)                    302
 *		# 0x95, 0x01,                    //  Report Count (1)                   304
 *		# 0xb1, 0x02,                    //  Feature (Data,Var,Abs)             306
 *		# 0x06, 0x00, 0xff,              //  Usage Page (Vendor Defined Page 1) 308
 *		# 0x09, 0xc5,                    //  Usage (Vendor Usage 0xc5)          311
 *		# 0x85, 0x06,                    //  Report ID (6)                      313
 *		# 0x15, 0x00,                    //  Logical Minimum (0)                315
 *		# 0x26, 0xff, 0x00,              //  Logical Maximum (255)              317
 *		# 0x75, 0x08,                    //  Report Size (8)                    320
 *		# 0x96, 0x00, 0x01,              //  Report Count (256)                 322
 *		# 0xb1, 0x02,                    //  Feature (Data,Var,Abs)             325
 *		# 0xc0,                          // End Collection                      327
 *		#
 *		R: 328 05 0d 09 02 a1 01 85 0a 09 20 a1 01 09 42 09 44 09 43 09 3c 09 45 15 00 25 01 75 01 95 06 81 02 09 32 75 01 95 01 81 02 81 03 05 01 09 30 09 31 55 0d 65 33 26 ff 7f 35 00 46 00 08 75 10 95 02 81 02 05 0d 09 30 26 ff 3f 75 10 95 01 81 02 09 3d 09 3e 15 a6 25 5a 75 08 95 02 81 02 c0 c0 05 0d 09 04 a1 01 85 04 09 22 a1 02 05 0d 95 01 75 06 09 51 15 00 25 3f 81 02 09 42 25 01 75 01 95 01 81 02 75 01 95 01 81 03 05 01 75 10 55 0e 65 11 09 30 26 ff 7f 35 00 46 15 0c 81 42 09 31 26 ff 7f 46 cb 06 81 42 05 0d 09 30 26 ff 1f 75 10 95 01 81 02 c0 05 0d 09 22 a1 02 05 0d 95 01 75 06 09 51 15 00 25 3f 81 02 09 42 25 01 75 01 95 01 81 02 75 01 95 01 81 03 05 01 75 10 55 0e 65 11 09 30 26 ff 7f 35 00 46 15 0c 81 42 09 31 26 ff 7f 46 cb 06 81 42 05 0d 09 30 26 ff 1f 75 10 95 01 81 02 c0 05 0d 09 56 55 00 65 00 27 ff ff ff 7f 95 01 75 20 81 02 09 54 25 7f 95 01 75 08 81 02 75 08 95 08 81 03 85 05 09 55 25 0a 75 08 95 01 b1 02 06 00 ff 09 c5 85 06 15 00 26 ff 00 75 08 96 00 01 b1 02 c0
 *		N: HUION Huion Tablet_GS1563
 *		I: 3 256c 2009
 *
 *	DESCRIPTOR 2
 *		# 0x05, 0x01,                    // Usage Page (Generic Desktop)        0
 *		# 0x09, 0x0e,                    // Usage (System Multi-Axis Controller) 2
 *		# 0xa1, 0x01,                    // Collection (Application)            4
 *		# 0x85, 0x11,                    //  Report ID (17)                     6
 *		# 0x05, 0x0d,                    //  Usage Page (Digitizers)            8
 *		# 0x09, 0x21,                    //  Usage (Puck)                       10
 *		# 0xa1, 0x02,                    //  Collection (Logical)               12
 *		# 0x15, 0x00,                    //   Logical Minimum (0)               14
 *		# 0x25, 0x01,                    //   Logical Maximum (1)               16
 *		# 0x75, 0x01,                    //   Report Size (1)                   18
 *		# 0x95, 0x01,                    //   Report Count (1)                  20
 *		# 0xa1, 0x00,                    //   Collection (Physical)             22
 *		# 0x05, 0x09,                    //    Usage Page (Button)              24
 *		# 0x09, 0x01,                    //    Usage (Vendor Usage 0x01)        26
 *		# 0x81, 0x02,                    //    Input (Data,Var,Abs)             28
 *		# 0x05, 0x0d,                    //    Usage Page (Digitizers)          30
 *		# 0x09, 0x33,                    //    Usage (Touch)                    32
 *		# 0x81, 0x02,                    //    Input (Data,Var,Abs)             34
 *		# 0x95, 0x06,                    //    Report Count (6)                 36
 *		# 0x81, 0x03,                    //    Input (Cnst,Var,Abs)             38
 *		# 0xa1, 0x02,                    //    Collection (Logical)             40
 *		# 0x05, 0x01,                    //     Usage Page (Generic Desktop)    42
 *		# 0x09, 0x37,                    //     Usage (Dial)                    44
 *		# 0x16, 0x00, 0x80,              //     Logical Minimum (-32768)        46
 *		# 0x26, 0xff, 0x7f,              //     Logical Maximum (32767)         49
 *		# 0x75, 0x10,                    //     Report Size (16)                52
 *		# 0x95, 0x01,                    //     Report Count (1)                54
 *		# 0x81, 0x06,                    //     Input (Data,Var,Rel)            56
 *		# 0x35, 0x00,                    //     Physical Minimum (0)            58
 *		# 0x46, 0x10, 0x0e,              //     Physical Maximum (3600)         60
 *		# 0x15, 0x00,                    //     Logical Minimum (0)             63
 *		# 0x26, 0x10, 0x0e,              //     Logical Maximum (3600)          65
 *		# 0x09, 0x48,                    //     Usage (Resolution Multiplier)   68
 *		# 0xb1, 0x02,                    //     Feature (Data,Var,Abs)          70
 *		# 0x45, 0x00,                    //     Physical Maximum (0)            72
 *		# 0xc0,                          //    End Collection                   74
 *		# 0x75, 0x08,                    //    Report Size (8)                  75
 *		# 0x95, 0x01,                    //    Report Count (1)                 77
 *		# 0x81, 0x01,                    //    Input (Cnst,Arr,Abs)             79
 *		# 0x75, 0x08,                    //    Report Size (8)                  81
 *		# 0x95, 0x01,                    //    Report Count (1)                 83
 *		# 0x81, 0x01,                    //    Input (Cnst,Arr,Abs)             85
 *		# 0x75, 0x08,                    //    Report Size (8)                  87
 *		# 0x95, 0x01,                    //    Report Count (1)                 89
 *		# 0x81, 0x01,                    //    Input (Cnst,Arr,Abs)             91
 *		# 0x75, 0x08,                    //    Report Size (8)                  93
 *		# 0x95, 0x01,                    //    Report Count (1)                 95
 *		# 0x81, 0x01,                    //    Input (Cnst,Arr,Abs)             97
 *		# 0x75, 0x08,                    //    Report Size (8)                  99
 *		# 0x95, 0x01,                    //    Report Count (1)                 101
 *		# 0x81, 0x01,                    //    Input (Cnst,Arr,Abs)             103
 *		# 0xc0,                          //   End Collection                    105
 *		# 0xc0,                          //  End Collection                     106
 *		# 0xc0,                          // End Collection                      107
 *		# 0x05, 0x01,                    // Usage Page (Generic Desktop)        108
 *		# 0x09, 0x06,                    // Usage (Keyboard)                    110
 *		# 0xa1, 0x01,                    // Collection (Application)            112
 *		# 0x85, 0x03,                    //  Report ID (3)                      114
 *		# 0x05, 0x07,                    //  Usage Page (Keyboard)              116
 *		# 0x19, 0xe0,                    //  Usage Minimum (224)                118
 *		# 0x29, 0xe7,                    //  Usage Maximum (231)                120
 *		# 0x15, 0x00,                    //  Logical Minimum (0)                122
 *		# 0x25, 0x01,                    //  Logical Maximum (1)                124
 *		# 0x75, 0x01,                    //  Report Size (1)                    126
 *		# 0x95, 0x08,                    //  Report Count (8)                   128
 *		# 0x81, 0x02,                    //  Input (Data,Var,Abs)               130
 *		# 0x05, 0x07,                    //  Usage Page (Keyboard)              132
 *		# 0x19, 0x00,                    //  Usage Minimum (0)                  134
 *		# 0x29, 0xff,                    //  Usage Maximum (255)                136
 *		# 0x26, 0xff, 0x00,              //  Logical Maximum (255)              138
 *		# 0x75, 0x08,                    //  Report Size (8)                    141
 *		# 0x95, 0x06,                    //  Report Count (6)                   143
 *		# 0x81, 0x00,                    //  Input (Data,Arr,Abs)               145
 *		# 0xc0,                          // End Collection                      147
 *		# 0x05, 0x0c,                    // Usage Page (Consumer Devices)       148
 *		# 0x09, 0x01,                    // Usage (Consumer Control)            150
 *		# 0xa1, 0x01,                    // Collection (Application)            152
 *		# 0x85, 0x04,                    //  Report ID (4)                      154
 *		# 0x19, 0x01,                    //  Usage Minimum (1)                  156
 *		# 0x2a, 0x9c, 0x02,              //  Usage Maximum (668)                158
 *		# 0x15, 0x01,                    //  Logical Minimum (1)                161
 *		# 0x26, 0x9c, 0x02,              //  Logical Maximum (668)              163
 *		# 0x95, 0x01,                    //  Report Count (1)                   166
 *		# 0x75, 0x10,                    //  Report Size (16)                   168
 *		# 0x81, 0x00,                    //  Input (Data,Arr,Abs)               170
 *		# 0xc0,                          // End Collection                      172
 *		# 0x05, 0x01,                    // Usage Page (Generic Desktop)        173
 *		# 0x09, 0x80,                    // Usage (System Control)              175
 *		# 0xa1, 0x01,                    // Collection (Application)            177
 *		# 0x85, 0x05,                    //  Report ID (5)                      179
 *		# 0x19, 0x81,                    //  Usage Minimum (129)                181
 *		# 0x29, 0x83,                    //  Usage Maximum (131)                183
 *		# 0x15, 0x00,                    //  Logical Minimum (0)                185
 *		# 0x25, 0x01,                    //  Logical Maximum (1)                187
 *		# 0x75, 0x01,                    //  Report Size (1)                    189
 *		# 0x95, 0x03,                    //  Report Count (3)                   191
 *		# 0x81, 0x02,                    //  Input (Data,Var,Abs)               193
 *		# 0x95, 0x05,                    //  Report Count (5)                   195
 *		# 0x81, 0x01,                    //  Input (Cnst,Arr,Abs)               197
 *		# 0xc0,                          // End Collection                      199
 *		#
 *		R: 200 05 01 09 0e a1 01 85 11 05 0d 09 21 a1 02 15 00 25 01 75 01 95 01 a1 00 05 09 09 01 81 02 05 0d 09 33 81 02 95 06 81 03 a1 02 05 01 09 37 16 00 80 26 ff 7f 75 10 95 01 81 06 35 00 46 10 0e 15 00 26 10 0e 09 48 b1 02 45 00 c0 75 08 95 01 81 01 75 08 95 01 81 01 75 08 95 01 81 01 75 08 95 01 81 01 75 08 95 01 81 01 c0 c0 c0 05 01 09 06 a1 01 85 03 05 07 19 e0 29 e7 15 00 25 01 75 01 95 08 81 02 05 07 19 00 29 ff 26 ff 00 75 08 95 06 81 00 c0 05 0c 09 01 a1 01 85 04 19 01 2a 9c 02 15 01 26 9c 02 95 01 75 10 81 00 c0 05 01 09 80 a1 01 85 05 19 81 29 83 15 00 25 01 75 01 95 03 81 02 95 05 81 01 c0
 *		N: HUION Huion Tablet_GS1563
 *		I: 3 256c 2009
 *
 *
 *
 * VENDOR MODE
 *	HUION_FIRMWARE_ID="HUION_M22d_241101"
 *	HUION_MAGIC_BYTES="1403201101ac9900ff3fd81305080080083c4010"
 *
 *	MAGIC BYTES
 *	          [LogicalMaximum, X   ] [LogicalMaximum, Y   ] [LogicalMaximum, Pressure] [  LPI]
 *	    14 03 [            20 11 01] [            ac 99 00] [                   ff 3f] [d8 13] 05 08 00 80 08 3c 40 10
 *
 * See Huion__Kamvas13Gen3.bpf.c for more details on detailed button/dial reports and caveats. It's very
 * similar to the Kamvas 16 Gen 3.
 */


/* Filled in by udev-hid-bpf */
char UDEV_PROP_HUION_FIRMWARE_ID[64];

char EXPECTED_FIRMWARE_ID[] = "HUION_M22d_";

__u8 last_button_state;

static const __u8 disabled_rdesc_tablet[] = {
	FixedSizeVendorReport(28)	/* Input report 4 */
};

static const __u8 disabled_rdesc_wheel[] = {
	FixedSizeVendorReport(9)	/* Input report 17 */
};

static const __u8 fixed_rdesc_vendor[] = {
	UsagePage_Digitizers
	Usage_Dig_Pen
	CollectionApplication(
		ReportId(VENDOR_REPORT_ID)
		UsagePage_Digitizers
		Usage_Dig_Pen
		CollectionPhysical(
			/*
			 * I have only examined the tablet's behavior while using
			 * the PW600L pen, which does not have an eraser.
			 * Because of this, I don't know where the Eraser and Invert
			 * bits will go, or if they work as one would expect.
			 *
			 * For the time being, there is no expectation that a pen
			 * with an eraser will work without modifications here.
			 */
			ReportSize(1)
			LogicalMinimum_i8(0)
			LogicalMaximum_i8(1)
			ReportCount(3)
			Usage_Dig_TipSwitch
			Usage_Dig_BarrelSwitch
			Usage_Dig_SecondaryBarrelSwitch
			Input(Var|Abs)
			PushPop(
				ReportCount(1)
				UsagePage_Button
				Usage_i8(0x4a)	/* (BTN_STYLUS3 + 1) & 0xff */
				Input(Var|Abs)
			)
			ReportCount(3)
			Input(Const)
			ReportCount(1)
			Usage_Dig_InRange
			Input(Var|Abs)
			ReportSize(16)
			ReportCount(1)
			PushPop(
				UsagePage_GenericDesktop
				Unit(cm)
				UnitExponent(-2)
				LogicalMinimum_i16(0)
				PhysicalMinimum_i16(0)
				/*
				 * The tablet has a logical maximum of 69920 x 39340
				 * and a claimed resolution of 5080 LPI (200 L/mm)
				 * This works out to a physical maximum of
				 * 349.6 x 196.7mm, which matches Huion's advertised
				 * (rounded) active area dimensions from
				 * https://www.huion.com/products/pen_display/Kamvas/kamvas-16-gen-3.html
				 *
				 * The Kamvas uses data[8] for the 3rd byte of the X-axis, and adding
				 * that after data[2] and data[3] makes a contiguous little-endian
				 * 24-bit value. (See BPF_PROG below)
				 */
				ReportSize(24)
				LogicalMaximum_i32(69920)
				PhysicalMaximum_i16(3496)
				Usage_GD_X
				Input(Var|Abs)
				ReportSize(16)
				LogicalMaximum_i16(39340)
				PhysicalMaximum_i16(1967)
				Usage_GD_Y
				Input(Var|Abs)
			)
			ReportSize(16)
			LogicalMinimum_i16(0)
			LogicalMaximum_i16(16383)
			Usage_Dig_TipPressure
			Input(Var|Abs)
			ReportSize(8)
			ReportCount(1)
			Input(Const)
			ReportCount(2)
			PushPop(
				Unit(deg)
				UnitExponent(0)
				LogicalMinimum_i8(-60)
				PhysicalMinimum_i8(-60)
				LogicalMaximum_i8(60)
				PhysicalMaximum_i8(60)
				Usage_Dig_XTilt
				Usage_Dig_YTilt
				Input(Var|Abs)
			)
		)
	)
	UsagePage_GenericDesktop
	Usage_GD_Keypad
	CollectionApplication(
		ReportId(CUSTOM_PAD_REPORT_ID)
		LogicalMinimum_i8(0)
		LogicalMaximum_i8(1)
		UsagePage_Digitizers
		Usage_Dig_TabletFunctionKeys
		CollectionPhysical(
			/*
			 * The first 3 bytes are somewhat vestigial and will
			 * always be set to zero. Their presence here is needed
			 * to ensure that this device will be detected as a
			 * tablet pad by software that otherwise wouldn't know
			 * any better.
			 */
			/* (data[1] & 0x01)	barrel switch */
			ReportSize(1)
			ReportCount(1)
			Usage_Dig_BarrelSwitch
			Input(Var|Abs)
			ReportCount(7)
			Input(Const)
			/* data[2]	X */
			/* data[3]	Y */
			ReportSize(8)
			ReportCount(2)
			UsagePage_GenericDesktop
			Usage_GD_X
			Usage_GD_Y
			Input(Var|Abs)
			/*
			 * (data[4] & 0x01)	button 1
			 * (data[4] & 0x02)	button 2
			 * (data[4] & 0x04)	button 3
			 * (data[4] & 0x08)	button 4
			 * (data[4] & 0x10)	button 5
			 * (data[4] & 0x20)	button 6
			 * (data[4] & 0x40)	button 7 (top wheel button)
			 * (data[4] & 0x80)	button 8 (bottom wheel button)
			 */
			ReportSize(1)
			ReportCount(8)
			UsagePage_Button
			UsageMinimum_i8(1)
			UsageMaximum_i8(8)
			Input(Var|Abs)
			/* data[5]	top wheel (signed, positive clockwise) */
			ReportSize(8)
			ReportCount(1)
			UsagePage_GenericDesktop
			Usage_GD_Wheel
			LogicalMinimum_i8(-1)
			LogicalMaximum_i8(1)
			Input(Var|Rel)
			/* data[6]	bottom wheel (signed, positive clockwise) */
			UsagePage_Consumer
			Usage_Con_ACPan
			Input(Var|Rel)
		)
		/*
		 * The kernel will drop reports that are bigger than the
		 * largest report specified in the HID descriptor.
		 * Therefore, our modified descriptor needs to have at least one
		 * HID report that is as long as, or longer than, the largest
		 * report in the original descriptor.
		 *
		 * This macro expands to a no-op report that is padded to the
		 * provided length.
		 */
		FixedSizeVendorReport(VENDOR_REPORT_LENGTH)
	)
};

SEC(HID_BPF_RDESC_FIXUP)
int BPF_PROG(hid_fix_rdesc_huion_kamvas16_gen3, struct hid_bpf_ctx *hid_ctx)
{
	__u8 *data = hid_bpf_get_data(hid_ctx, 0 /* offset */, HID_MAX_DESCRIPTOR_SIZE /* size */);
	__s32 rdesc_size = hid_ctx->size;
	__u8 have_fw_id;

	if (!data)
		return 0; /* EPERM check */

	have_fw_id = __builtin_memcmp(UDEV_PROP_HUION_FIRMWARE_ID,
					EXPECTED_FIRMWARE_ID,
					sizeof(EXPECTED_FIRMWARE_ID) - 1) == 0;

	if (have_fw_id) {
		/*
		 * Tablet should be in vendor mode.
		 * Disable the unused devices
		 */
		if (rdesc_size == TABLET_DESCRIPTOR_LENGTH) {
			__builtin_memcpy(data, disabled_rdesc_tablet,
					 sizeof(disabled_rdesc_tablet));
			return sizeof(disabled_rdesc_tablet);
		}

		if (rdesc_size == WHEEL_DESCRIPTOR_LENGTH) {
			__builtin_memcpy(data, disabled_rdesc_wheel,
					 sizeof(disabled_rdesc_wheel));
			return sizeof(disabled_rdesc_wheel);
		}
	}

	/*
	 * Regardless of which mode the tablet is in, always fix the vendor
	 * descriptor in case the udev property just happened to not be set
	 */
	if (rdesc_size == VENDOR_DESCRIPTOR_LENGTH) {
		__builtin_memcpy(data, fixed_rdesc_vendor, sizeof(fixed_rdesc_vendor));
		return sizeof(fixed_rdesc_vendor);
	}

	return 0;
}

SEC(HID_BPF_DEVICE_EVENT)
int BPF_PROG(hid_fix_event_huion_kamvas16_gen3, struct hid_bpf_ctx *hid_ctx)
{
	__u8 *data = hid_bpf_get_data(hid_ctx, 0 /* offset */, VENDOR_REPORT_LENGTH /* size */);

	if (!data)
		return 0; /* EPERM check */

	/* Handle vendor reports only */
	if (hid_ctx->size != VENDOR_REPORT_LENGTH)
		return 0;
	if (data[0] != VENDOR_REPORT_ID)
		return 0;

	__u8 report_subtype = (data[1] >> 4) & 0x0f;

	if (report_subtype == VENDOR_REPORT_SUBTYPE_PEN ||
	    report_subtype == VENDOR_REPORT_SUBTYPE_PEN_OUT) {
		/* Invert Y tilt */
		data[11] = -data[11];

		/*
		 * Rearrange the bytes of the report so that
		 * [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13]
		 * will be arranged as
		 * [0, 1, 2, 3, 8, 4, 5, 6, 7, 9, 10, 11, 12, 13]
		 */
		__u8 x_24 = data[8];

		data[8] = data[7];
		data[7] = data[6];
		data[6] = data[5];
		data[5] = data[4];

		data[4] = x_24;

	} else if (report_subtype == VENDOR_REPORT_SUBTYPE_BUTTONS ||
		   report_subtype == VENDOR_REPORT_SUBTYPE_WHEELS) {
		struct pad_report {
			__u8 report_id;
			__u8 btn_stylus:1;
			__u8 padding:7;
			__u8 x;
			__u8 y;
			__u8 buttons;
			__s8 top_wheel;
			__s8 bottom_wheel;
		} __attribute__((packed)) *pad_report;

		__s8 top_wheel = 0;
		__s8 bottom_wheel = 0;

		switch (report_subtype) {
		case VENDOR_REPORT_SUBTYPE_WHEELS:
			/*
			 * The wheel direction byte is 1 for clockwise rotation
			 * and 2 for counter-clockwise.
			 * Change it to 1 and -1, respectively.
			 */
			switch (data[3]) {
			case 1:
				top_wheel = (data[5] == 1) ? 1 : -1;
				break;
			case 2:
				bottom_wheel = (data[5] == 1) ? 1 : -1;
				break;
			}
			break;

		case VENDOR_REPORT_SUBTYPE_BUTTONS:
			/*
			 * If a button is already being held, ignore any new
			 * button event unless it's a release.
			 *
			 * The tablet only cleanly handles one button being held
			 * at a time, and trying to hold multiple buttons
			 * (particularly wheel+pad buttons) can result in sequences
			 * of reports that look like imaginary presses and releases.
			 *
			 * This is an imperfect way to filter out some of these
			 * reports.
			 */
			if (last_button_state != 0x00 && data[4] != 0x00)
				break;

			last_button_state = data[4];
			break;
		}

		pad_report = (struct pad_report *)data;

		pad_report->report_id = CUSTOM_PAD_REPORT_ID;
		pad_report->btn_stylus = 0;
		pad_report->x = 0;
		pad_report->y = 0;
		pad_report->buttons = last_button_state;
		pad_report->top_wheel = top_wheel;
		pad_report->bottom_wheel = bottom_wheel;

		return sizeof(struct pad_report);
	}

	return 0;
}

HID_BPF_OPS(huion_kamvas16_gen3) = {
	.hid_device_event = (void *)hid_fix_event_huion_kamvas16_gen3,
	.hid_rdesc_fixup = (void *)hid_fix_rdesc_huion_kamvas16_gen3,
};

SEC("syscall")
int probe(struct hid_bpf_probe_args *ctx)
{
	switch (ctx->rdesc_size) {
	case VENDOR_DESCRIPTOR_LENGTH:
	case TABLET_DESCRIPTOR_LENGTH:
	case WHEEL_DESCRIPTOR_LENGTH:
		ctx->retval = 0;
		break;
	default:
		ctx->retval = -EINVAL;
	}

	return 0;
}

char _license[] SEC("license") = "GPL";
