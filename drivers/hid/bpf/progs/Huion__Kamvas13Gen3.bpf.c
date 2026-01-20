// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2025 Nicholas LaPointe
 */

#include "vmlinux.h"
#include "hid_bpf.h"
#include "hid_bpf_helpers.h"
#include "hid_report_helpers.h"
#include <bpf/bpf_tracing.h>

#define VID_HUION 0x256c
#define PID_KAMVAS13_GEN3 0x2008

#define VENDOR_DESCRIPTOR_LENGTH 36
#define TABLET_DESCRIPTOR_LENGTH 368
#define WHEEL_DESCRIPTOR_LENGTH 108

#define VENDOR_REPORT_ID 8
#define VENDOR_REPORT_LENGTH 14

#define VENDOR_REPORT_SUBTYPE_PEN 0x08
#define VENDOR_REPORT_SUBTYPE_PEN_OUT 0x00
#define VENDOR_REPORT_SUBTYPE_BUTTONS 0x0e
#define VENDOR_REPORT_SUBTYPE_WHEELS 0x0f

/* For the reports that we create ourselves */
#define CUSTOM_PAD_REPORT_ID 9

HID_BPF_CONFIG(
	HID_DEVICE(BUS_USB, HID_GROUP_ANY, VID_HUION, PID_KAMVAS13_GEN3),
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
 *		#   0x06, 0x00, 0xff,              // Usage Page (Vendor Defined Page FF00)     0
 *		#   0x09, 0x01,                    // Usage (Vendor Usage 0x01)                 3
 *		#   0xa1, 0x01,                    // Collection (Application)                  5
 *		# ┅ 0x85, 0x08,                    //   Report ID (8)                           7
 *		#   0x75, 0x68,                    //   Report Size (104)                       9
 *		#   0x95, 0x01,                    //   Report Count (1)                        11
 *		#   0x09, 0x01,                    //   Usage (Vendor Usage 0x01)               13
 *		# ┇ 0x81, 0x02,                    //   Input (Data,Var,Abs)                    15
 *		#   0xc0,                          // End Collection                            17
 *		#   0x06, 0x00, 0xff,              // Usage Page (Vendor Defined Page FF00)     18
 *		#   0x09, 0x01,                    // Usage (Vendor Usage 0x01)                 21
 *		#   0xa1, 0x01,                    // Collection (Application)                  23
 *		# ┅ 0x85, 0x16,                    //   Report ID (22)                          25
 *		#   0x75, 0x08,                    //   Report Size (8)                         27
 *		#   0x95, 0x07,                    //   Report Count (7)                        29
 *		#   0x09, 0x01,                    //   Usage (Vendor Usage 0x01)               31
 *		# ║ 0xb1, 0x02,                    //   Feature (Data,Var,Abs)                  33
 *		#   0xc0,                          // End Collection                            35
 *		R: 36 06 00 ff 09 01 a1 01 85 08 75 68 95 01 09 01 81 02 c0 06 00 ff 09 01 a1 01 85 16 75 08 95 07 09 01 b1 02 c0
 *		N: HUION Huion Tablet_GS1333
 *		I: 3 256c 2008
 *
 *	DESCRIPTOR 1
 *		#   0x05, 0x0d,                    // Usage Page (Digitizers)                   0
 *		#   0x09, 0x02,                    // Usage (Pen)                               2
 *		#   0xa1, 0x01,                    // Collection (Application)                  4
 *		# ┅ 0x85, 0x0a,                    //   Report ID (10)                          6
 *		#   0x09, 0x20,                    //   Usage (Stylus)                          8
 *		#   0xa1, 0x01,                    //   Collection (Application)                10
 *		#   0x09, 0x42,                    //     Usage (Tip Switch)                    12
 *		#   0x09, 0x44,                    //     Usage (Barrel Switch)                 14
 *		#   0x09, 0x43,                    //     Usage (Secondary Tip Switch)          16
 *		#   0x09, 0x3c,                    //     Usage (Invert)                        18
 *		#   0x09, 0x45,                    //     Usage (Eraser)                        20
 *		#   0x15, 0x00,                    //     Logical Minimum (0)                   22
 *		#   0x25, 0x01,                    //     Logical Maximum (1)                   24
 *		#   0x75, 0x01,                    //     Report Size (1)                       26
 *		#   0x95, 0x06,                    //     Report Count (6)                      28
 *		# ┇ 0x81, 0x02,                    //     Input (Data,Var,Abs)                  30
 *		#   0x09, 0x32,                    //     Usage (In Range)                      32
 *		#   0x75, 0x01,                    //     Report Size (1)                       34
 *		#   0x95, 0x01,                    //     Report Count (1)                      36
 *		# ┇ 0x81, 0x02,                    //     Input (Data,Var,Abs)                  38
 *		# ┇ 0x81, 0x03,                    //     Input (Cnst,Var,Abs)                  40
 *		#   0x05, 0x01,                    //     Usage Page (Generic Desktop)          42
 *		#   0x09, 0x30,                    //     Usage (X)                             44
 *		#   0x09, 0x31,                    //     Usage (Y)                             46
 *		#   0x55, 0x0d,                    //     Unit Exponent (-3)                    48
 *		#   0x65, 0x33,                    //     Unit (EnglishLinear: in³)             50
 *		#   0x26, 0xff, 0x7f,              //     Logical Maximum (32767)               52
 *		#   0x35, 0x00,                    //     Physical Minimum (0)                  55
 *		#   0x46, 0x00, 0x08,              //     Physical Maximum (2048)               57
 *		#   0x75, 0x10,                    //     Report Size (16)                      60
 *		#   0x95, 0x02,                    //     Report Count (2)                      62
 *		# ┇ 0x81, 0x02,                    //     Input (Data,Var,Abs)                  64
 *		#   0x05, 0x0d,                    //     Usage Page (Digitizers)               66
 *		#   0x09, 0x30,                    //     Usage (Tip Pressure)                  68
 *		#   0x26, 0xff, 0x3f,              //     Logical Maximum (16383)               70
 *		#   0x75, 0x10,                    //     Report Size (16)                      73
 *		#   0x95, 0x01,                    //     Report Count (1)                      75
 *		# ┇ 0x81, 0x02,                    //     Input (Data,Var,Abs)                  77
 *		#   0x09, 0x3d,                    //     Usage (X Tilt)                        79
 *		#   0x09, 0x3e,                    //     Usage (Y Tilt)                        81
 *		#   0x15, 0xa6,                    //     Logical Minimum (-90)                 83
 *		#   0x25, 0x5a,                    //     Logical Maximum (90)                  85
 *		#   0x75, 0x08,                    //     Report Size (8)                       87
 *		#   0x95, 0x02,                    //     Report Count (2)                      89
 *		# ┇ 0x81, 0x02,                    //     Input (Data,Var,Abs)                  91
 *		#   0xc0,                          //   End Collection                          93
 *		#   0xc0,                          // End Collection                            94
 *		#   0x05, 0x0d,                    // Usage Page (Digitizers)                   95
 *		#   0x09, 0x04,                    // Usage (Touch Screen)                      97
 *		#   0xa1, 0x01,                    // Collection (Application)                  99
 *		# ┅ 0x85, 0x04,                    //   Report ID (4)                           101
 *		#   0x09, 0x22,                    //   Usage (Finger)                          103
 *		#   0xa1, 0x02,                    //   Collection (Logical)                    105
 *		#   0x05, 0x0d,                    //     Usage Page (Digitizers)               107
 *		#   0x95, 0x01,                    //     Report Count (1)                      109
 *		#   0x75, 0x06,                    //     Report Size (6)                       111
 *		#   0x09, 0x51,                    //     Usage (Contact Identifier)            113
 *		#   0x15, 0x00,                    //     Logical Minimum (0)                   115
 *		#   0x25, 0x3f,                    //     Logical Maximum (63)                  117
 *		# ┇ 0x81, 0x02,                    //     Input (Data,Var,Abs)                  119
 *		#   0x09, 0x42,                    //     Usage (Tip Switch)                    121
 *		#   0x25, 0x01,                    //     Logical Maximum (1)                   123
 *		#   0x75, 0x01,                    //     Report Size (1)                       125
 *		#   0x95, 0x01,                    //     Report Count (1)                      127
 *		# ┇ 0x81, 0x02,                    //     Input (Data,Var,Abs)                  129
 *		#   0x75, 0x01,                    //     Report Size (1)                       131
 *		#   0x95, 0x01,                    //     Report Count (1)                      133
 *		# ┇ 0x81, 0x03,                    //     Input (Cnst,Var,Abs)                  135
 *		#   0x05, 0x01,                    //     Usage Page (Generic Desktop)          137
 *		#   0x75, 0x10,                    //     Report Size (16)                      139
 *		#   0x55, 0x0e,                    //     Unit Exponent (-2)                    141
 *		#   0x65, 0x11,                    //     Unit (SILinear: cm)                   143
 *		#   0x09, 0x30,                    //     Usage (X)                             145
 *		#   0x26, 0xff, 0x7f,              //     Logical Maximum (32767)               147
 *		#   0x35, 0x00,                    //     Physical Minimum (0)                  150
 *		#   0x46, 0x15, 0x0c,              //     Physical Maximum (3093)               152
 *		# ┇ 0x81, 0x42,                    //     Input (Data,Var,Abs,Null)             155
 *		#   0x09, 0x31,                    //     Usage (Y)                             157
 *		#   0x26, 0xff, 0x7f,              //     Logical Maximum (32767)               159
 *		#   0x46, 0xcb, 0x06,              //     Physical Maximum (1739)               162
 *		# ┇ 0x81, 0x42,                    //     Input (Data,Var,Abs,Null)             165
 *		#   0x05, 0x0d,                    //     Usage Page (Digitizers)               167
 *		#   0x09, 0x30,                    //     Usage (Tip Pressure)                  169
 *		#   0x26, 0xff, 0x1f,              //     Logical Maximum (8191)                171
 *		#   0x75, 0x10,                    //     Report Size (16)                      174
 *		#   0x95, 0x01,                    //     Report Count (1)                      176
 *		# ┇ 0x81, 0x02,                    //     Input (Data,Var,Abs)                  178
 *		#   0xc0,                          //   End Collection                          180
 *		#   0x05, 0x0d,                    //   Usage Page (Digitizers)                 181
 *		#   0x09, 0x22,                    //   Usage (Finger)                          183
 *		#   0xa1, 0x02,                    //   Collection (Logical)                    185
 *		#   0x05, 0x0d,                    //     Usage Page (Digitizers)               187
 *		#   0x95, 0x01,                    //     Report Count (1)                      189
 *		#   0x75, 0x06,                    //     Report Size (6)                       191
 *		#   0x09, 0x51,                    //     Usage (Contact Identifier)            193
 *		#   0x15, 0x00,                    //     Logical Minimum (0)                   195
 *		#   0x25, 0x3f,                    //     Logical Maximum (63)                  197
 *		# ┇ 0x81, 0x02,                    //     Input (Data,Var,Abs)                  199
 *		#   0x09, 0x42,                    //     Usage (Tip Switch)                    201
 *		#   0x25, 0x01,                    //     Logical Maximum (1)                   203
 *		#   0x75, 0x01,                    //     Report Size (1)                       205
 *		#   0x95, 0x01,                    //     Report Count (1)                      207
 *		# ┇ 0x81, 0x02,                    //     Input (Data,Var,Abs)                  209
 *		#   0x75, 0x01,                    //     Report Size (1)                       211
 *		#   0x95, 0x01,                    //     Report Count (1)                      213
 *		# ┇ 0x81, 0x03,                    //     Input (Cnst,Var,Abs)                  215
 *		#   0x05, 0x01,                    //     Usage Page (Generic Desktop)          217
 *		#   0x75, 0x10,                    //     Report Size (16)                      219
 *		#   0x55, 0x0e,                    //     Unit Exponent (-2)                    221
 *		#   0x65, 0x11,                    //     Unit (SILinear: cm)                   223
 *		#   0x09, 0x30,                    //     Usage (X)                             225
 *		#   0x26, 0xff, 0x7f,              //     Logical Maximum (32767)               227
 *		#   0x35, 0x00,                    //     Physical Minimum (0)                  230
 *		#   0x46, 0x15, 0x0c,              //     Physical Maximum (3093)               232
 *		# ┇ 0x81, 0x42,                    //     Input (Data,Var,Abs,Null)             235
 *		#   0x09, 0x31,                    //     Usage (Y)                             237
 *		#   0x26, 0xff, 0x7f,              //     Logical Maximum (32767)               239
 *		#   0x46, 0xcb, 0x06,              //     Physical Maximum (1739)               242
 *		# ┇ 0x81, 0x42,                    //     Input (Data,Var,Abs,Null)             245
 *		#   0x05, 0x0d,                    //     Usage Page (Digitizers)               247
 *		#   0x09, 0x30,                    //     Usage (Tip Pressure)                  249
 *		#   0x26, 0xff, 0x1f,              //     Logical Maximum (8191)                251
 *		#   0x75, 0x10,                    //     Report Size (16)                      254
 *		#   0x95, 0x01,                    //     Report Count (1)                      256
 *		# ┇ 0x81, 0x02,                    //     Input (Data,Var,Abs)                  258
 *		#   0xc0,                          //   End Collection                          260
 *		#   0x05, 0x0d,                    //   Usage Page (Digitizers)                 261
 *		#   0x09, 0x56,                    //   Usage (Scan Time)                       263
 *		#   0x55, 0x00,                    //   Unit Exponent (0)                       265
 *		#   0x65, 0x00,                    //   Unit (None)                             267
 *		#   0x27, 0xff, 0xff, 0xff, 0x7f,  //   Logical Maximum (2147483647)            269
 *		#   0x95, 0x01,                    //   Report Count (1)                        274
 *		#   0x75, 0x20,                    //   Report Size (32)                        276
 *		# ┇ 0x81, 0x02,                    //   Input (Data,Var,Abs)                    278
 *		#   0x09, 0x54,                    //   Usage (Contact Count)                   280
 *		#   0x25, 0x7f,                    //   Logical Maximum (127)                   282
 *		#   0x95, 0x01,                    //   Report Count (1)                        284
 *		#   0x75, 0x08,                    //   Report Size (8)                         286
 *		# ┇ 0x81, 0x02,                    //   Input (Data,Var,Abs)                    288
 *		#   0x75, 0x08,                    //   Report Size (8)                         290
 *		#   0x95, 0x08,                    //   Report Count (8)                        292
 *		# ┇ 0x81, 0x03,                    //   Input (Cnst,Var,Abs)                    294
 *		# ┅ 0x85, 0x05,                    //   Report ID (5)                           296
 *		#   0x09, 0x55,                    //   Usage (Contact Count Maximum)           298
 *		#   0x25, 0x0a,                    //   Logical Maximum (10)                    300
 *		#   0x75, 0x08,                    //   Report Size (8)                         302
 *		#   0x95, 0x01,                    //   Report Count (1)                        304
 *		# ║ 0xb1, 0x02,                    //   Feature (Data,Var,Abs)                  306
 *		#   0x06, 0x00, 0xff,              //   Usage Page (Vendor Defined Page FF00)   308
 *		#   0x09, 0xc5,                    //   Usage (Vendor Usage 0xc5)               311
 *		# ┅ 0x85, 0x06,                    //   Report ID (6)                           313
 *		#   0x15, 0x00,                    //   Logical Minimum (0)                     315
 *		#   0x26, 0xff, 0x00,              //   Logical Maximum (255)                   317
 *		#   0x75, 0x08,                    //   Report Size (8)                         320
 *		#   0x96, 0x00, 0x01,              //   Report Count (256)                      322
 *		# ║ 0xb1, 0x02,                    //   Feature (Data,Var,Abs)                  325
 *		#   0xc0,                          // End Collection                            327
 *		#   0x05, 0x01,                    // Usage Page (Generic Desktop)              328
 *		#   0x09, 0x06,                    // Usage (Keyboard)                          330
 *		#   0xa1, 0x01,                    // Collection (Application)                  332
 *		# ┅ 0x85, 0x03,                    //   Report ID (3)                           334
 *		#   0x05, 0x07,                    //   Usage Page (Keyboard/Keypad)            336
 *		#   0x19, 0xe0,                    //   UsageMinimum (224)                      338
 *		#   0x29, 0xe7,                    //   UsageMaximum (231)                      340
 *		#   0x15, 0x00,                    //   Logical Minimum (0)                     342
 *		#   0x25, 0x01,                    //   Logical Maximum (1)                     344
 *		#   0x75, 0x01,                    //   Report Size (1)                         346
 *		#   0x95, 0x08,                    //   Report Count (8)                        348
 *		# ┇ 0x81, 0x02,                    //   Input (Data,Var,Abs)                    350
 *		#   0x05, 0x07,                    //   Usage Page (Keyboard/Keypad)            352
 *		#   0x19, 0x00,                    //   UsageMinimum (0)                        354
 *		#   0x29, 0xff,                    //   UsageMaximum (255)                      356
 *		#   0x26, 0xff, 0x00,              //   Logical Maximum (255)                   358
 *		#   0x75, 0x08,                    //   Report Size (8)                         361
 *		#   0x95, 0x06,                    //   Report Count (6)                        363
 *		# ┇ 0x81, 0x00,                    //   Input (Data,Arr,Abs)                    365
 *		#   0xc0,                          // End Collection                            367
 *		R: 368 05 0d 09 02 a1 01 85 0a 09 20 a1 01 09 42 09 44 09 43 09 3c 09 45 15 00 25 01 75 01 95 06 81 02 09 32 75 01 95 01 81 02 81 03 05 01 09 30 09 31 55 0d 65 33 26 ff 7f 35 00 46 00 08 75 10 95 02 81 02 05 0d 09 30 26 ff 3f 75 10 95 01 81 02 09 3d 09 3e 15 a6 25 5a 75 08 95 02 81 02 c0 c0 05 0d 09 04 a1 01 85 04 09 22 a1 02 05 0d 95 01 75 06 09 51 15 00 25 3f 81 02 09 42 25 01 75 01 95 01 81 02 75 01 95 01 81 03 05 01 75 10 55 0e 65 11 09 30 26 ff 7f 35 00 46 15 0c 81 42 09 31 26 ff 7f 46 cb 06 81 42 05 0d 09 30 26 ff 1f 75 10 95 01 81 02 c0 05 0d 09 22 a1 02 05 0d 95 01 75 06 09 51 15 00 25 3f 81 02 09 42 25 01 75 01 95 01 81 02 75 01 95 01 81 03 05 01 75 10 55 0e 65 11 09 30 26 ff 7f 35 00 46 15 0c 81 42 09 31 26 ff 7f 46 cb 06 81 42 05 0d 09 30 26 ff 1f 75 10 95 01 81 02 c0 05 0d 09 56 55 00 65 00 27 ff ff ff 7f 95 01 75 20 81 02 09 54 25 7f 95 01 75 08 81 02 75 08 95 08 81 03 85 05 09 55 25 0a 75 08 95 01 b1 02 06 00 ff 09 c5 85 06 15 00 26 ff 00 75 08 96 00 01 b1 02 c0 05 01 09 06 a1 01 85 03 05 07 19 e0 29 e7 15 00 25 01 75 01 95 08 81 02 05 07 19 00 29 ff 26 ff 00 75 08 95 06 81 00 c0
 *		N: HUION Huion Tablet_GS1333
 *		I: 3 256c 2008
 *
 *	DESCRIPTOR 2
 *		#   0x05, 0x01,                    // Usage Page (Generic Desktop)              0
 *		#   0x09, 0x0e,                    // Usage (System Multi-Axis Controller)      2
 *		#   0xa1, 0x01,                    // Collection (Application)                  4
 *		# ┅ 0x85, 0x11,                    //   Report ID (17)                          6
 *		#   0x05, 0x0d,                    //   Usage Page (Digitizers)                 8
 *		#   0x09, 0x21,                    //   Usage (Puck)                            10
 *		#   0xa1, 0x02,                    //   Collection (Logical)                    12
 *		#   0x15, 0x00,                    //     Logical Minimum (0)                   14
 *		#   0x25, 0x01,                    //     Logical Maximum (1)                   16
 *		#   0x75, 0x01,                    //     Report Size (1)                       18
 *		#   0x95, 0x01,                    //     Report Count (1)                      20
 *		#   0xa1, 0x00,                    //     Collection (Physical)                 22
 *		#   0x05, 0x09,                    //       Usage Page (Button)                 24
 *		#   0x09, 0x01,                    //       Usage (Button 1)                    26
 *		# ┇ 0x81, 0x02,                    //       Input (Data,Var,Abs)                28
 *		#   0x05, 0x0d,                    //       Usage Page (Digitizers)             30
 *		#   0x09, 0x33,                    //       Usage (Touch)                       32
 *		# ┇ 0x81, 0x02,                    //       Input (Data,Var,Abs)                34
 *		#   0x95, 0x06,                    //       Report Count (6)                    36
 *		# ┇ 0x81, 0x03,                    //       Input (Cnst,Var,Abs)                38
 *		#   0xa1, 0x02,                    //       Collection (Logical)                40
 *		#   0x05, 0x01,                    //         Usage Page (Generic Desktop)      42
 *		#   0x09, 0x37,                    //         Usage (Dial)                      44
 *		#   0x16, 0x00, 0x80,              //         Logical Minimum (-32768)          46
 *		#   0x26, 0xff, 0x7f,              //         Logical Maximum (32767)           49
 *		#   0x75, 0x10,                    //         Report Size (16)                  52
 *		#   0x95, 0x01,                    //         Report Count (1)                  54
 *		# ┇ 0x81, 0x06,                    //         Input (Data,Var,Rel)              56
 *		#   0x35, 0x00,                    //         Physical Minimum (0)              58
 *		#   0x46, 0x10, 0x0e,              //         Physical Maximum (3600)           60
 *		#   0x15, 0x00,                    //         Logical Minimum (0)               63
 *		#   0x26, 0x10, 0x0e,              //         Logical Maximum (3600)            65
 *		#   0x09, 0x48,                    //         Usage (Resolution Multiplier)     68
 *		# ║ 0xb1, 0x02,                    //         Feature (Data,Var,Abs)            70
 *		#   0x45, 0x00,                    //         Physical Maximum (0)              72
 *		#   0xc0,                          //       End Collection                      74
 *		#   0x75, 0x08,                    //       Report Size (8)                     75
 *		#   0x95, 0x01,                    //       Report Count (1)                    77
 *		# ┇ 0x81, 0x01,                    //       Input (Cnst,Arr,Abs)                79
 *		#   0x75, 0x08,                    //       Report Size (8)                     81
 *		#   0x95, 0x01,                    //       Report Count (1)                    83
 *		# ┇ 0x81, 0x01,                    //       Input (Cnst,Arr,Abs)                85
 *		#   0x75, 0x08,                    //       Report Size (8)                     87
 *		#   0x95, 0x01,                    //       Report Count (1)                    89
 *		# ┇ 0x81, 0x01,                    //       Input (Cnst,Arr,Abs)                91
 *		#   0x75, 0x08,                    //       Report Size (8)                     93
 *		#   0x95, 0x01,                    //       Report Count (1)                    95
 *		# ┇ 0x81, 0x01,                    //       Input (Cnst,Arr,Abs)                97
 *		#   0x75, 0x08,                    //       Report Size (8)                     99
 *		#   0x95, 0x01,                    //       Report Count (1)                    101
 *		# ┇ 0x81, 0x01,                    //       Input (Cnst,Arr,Abs)                103
 *		#   0xc0,                          //     End Collection                        105
 *		#   0xc0,                          //   End Collection                          106
 *		#   0xc0,                          // End Collection                            107
 *		R: 108 05 01 09 0e a1 01 85 11 05 0d 09 21 a1 02 15 00 25 01 75 01 95 01 a1 00 05 09 09 01 81 02 05 0d 09 33 81 02 95 06 81 03 a1 02 05 01 09 37 16 00 80 26 ff 7f 75 10 95 01 81 06 35 00 46 10 0e 15 00 26 10 0e 09 48 b1 02 45 00 c0 75 08 95 01 81 01 75 08 95 01 81 01 75 08 95 01 81 01 75 08 95 01 81 01 75 08 95 01 81 01 c0 c0 c0
 *		N: HUION Huion Tablet_GS1333
 *		I: 3 256c 2008
 *
 *
 *
 *
 *
 *
 *
 *
 * VENDOR MODE
 *	HUION_FIRMWARE_ID="HUION_M22c_240606"
 *	HUION_MAGIC_BYTES="140388e500108100ff3fd8130307008008004010"
 *
 *	MAGIC BYTES
 *	          [LogicalMaximum, X]    [LogicalMaximum, Y]    [LogicalMaximum, Pressure] [  LPI]
 *	    14 03 [            88 e5] 00 [            10 81] 00 [                   ff 3f] [d8 13] 03 07 00 80 08 00 40 10
 *
 *
 *	HIDRAW 0
 *		DESCRIPTIONS
 *			report_subtype = (data[1] >> 4) & 0x0f
 *
 *			REPORT SUBTYPES
 *				0x0e		Buttons
 *							(data[4] & 0x01)	button 1
 *							(data[4] & 0x02)	button 2
 *							(data[4] & 0x04)	button 3
 *							(data[4] & 0x08)	button 4
 *							(data[4] & 0x10)	button 5
 *							(data[4] & 0x20)	button 6 (top wheel button)
 *							(data[4] & 0x40)	button 7 (bottom wheel button)
 *
 *							All tablet buttons release with the same report:
 *								08 e0 01 01 00 00 00 00 00 00 00 00 00 00
 *
 *							Despite data[4] looking like a bit field, only one button
 *							can be unambiguously tracked at a time.
 *							(See NOTES ON SIMULTANEOUS BUTTON HOLDS at the end of this
 *							comment for examples of the confusion this can create.)
 *
 *							All buttons, with the exceptions of 6 and 7, will repeatedly
 *							report a press event approximately every 225ms while held.
 *
 *				0x0f		Wheels
 *							data[3] == 1: top wheel
 *							data[3] == 2: bottom wheel
 *							data[5] == 1: clockwise
 *							data[5] == 2: counter-clockwise
 *
 *				0x08/0x00	Pen
 *							report_subtype == 0x08: in-range
 *							report_subtype == 0x00: out-of-range
 *								For clarity, this is also equivalent to:
 *									(data[1] & 0x80)	in-range
 *
 *							Switches
 *								(data[1] & 0x01)	tip switch
 *								(data[1] & 0x02)	barrel switch
 *								(data[1] & 0x04)	secondary barrel switch
 *								(data[1] & 0x08)	third barrel switch
 *
 *								Unfortunately, I don't have a pen with an eraser, so I can't
 *								confirm where the invert and eraser bits reside.
 *								If we guess using the definitions from HID descriptor 1,
 *								then they might be...
 *									(data[1] & 0x08)	invert (conflicts with third barrel switch)
 *									(data[1] & 0x10)	eraser
 *
 *							data[2], data[3]	X (little-endian, maximum 0xe588)
 *
 *							data[4], data[5]	Y (little-endian, maximum 0x8110)
 *
 *							data[6], data[7]	Pressure (little-endian, maximum 0x3fff)
 *
 *							data[10]		X tilt	(signed, -60 to +60)
 *							data[11]		Y tilt	(signed, -60 to +60, inverted)
 *
 *
 *		EXAMPLE REPORTS
 *			Top wheel button, press, hold, then release
 *				E: 000000.000040 14 08 e0 01 01 20 00 00 00 00 00 00 00 00 00
 *				E: 000001.531559 14 08 e0 01 01 00 00 00 00 00 00 00 00 00 00
 *
 *			Bottom wheel button, press, hold, then release
 *				E: 000002.787603 14 08 e0 01 01 40 00 00 00 00 00 00 00 00 00
 *				E: 000004.215609 14 08 e0 01 01 00 00 00 00 00 00 00 00 00 00
 *
 *
 *			Top wheel rotation, one detent CW
 *				E: 000194.003899 14 08 f1 01 01 00 01 00 00 00 00 00 00 00 00
 *
 *			Top wheel rotation, one detent CCW
 *				E: 000194.997812 14 08 f1 01 01 00 02 00 00 00 00 00 00 00 00
 *
 *			Bottom wheel rotation, one detent CW
 *				E: 000196.693840 14 08 f1 01 02 00 01 00 00 00 00 00 00 00 00
 *
 *			Bottom wheel rotation, one detent CCW
 *				E: 000197.757895 14 08 f1 01 02 00 02 00 00 00 00 00 00 00 00
 *
 *
 *			Button 1, press, hold, then release
 *				E: 000000.000149 14 08 e0 01 01 01 00 00 00 00 00 00 00 00 00 < press
 *				E: 000000.447598 14 08 e0 01 01 01 00 00 00 00 00 00 00 00 00 < starting to auto-repeat, every ~225ms
 *				E: 000000.673586 14 08 e0 01 01 01 00 00 00 00 00 00 00 00 00
 *				E: 000000.900582 14 08 e0 01 01 01 00 00 00 00 00 00 00 00 00
 *				E: 000001.126703 14 08 e0 01 01 01 00 00 00 00 00 00 00 00 00
 *				E: 000001.347706 14 08 e0 01 01 01 00 00 00 00 00 00 00 00 00
 *				E: 000001.533721 14 08 e0 01 01 00 00 00 00 00 00 00 00 00 00 < release
 *
 *			Button 2, press, hold, then release
 *				E: 000003.304735 14 08 e0 01 01 02 00 00 00 00 00 00 00 00 00 < press
 *				E: 000003.746743 14 08 e0 01 01 02 00 00 00 00 00 00 00 00 00 < starting to auto-repeat, every ~225ms
 *				E: 000003.973741 14 08 e0 01 01 02 00 00 00 00 00 00 00 00 00
 *				E: 000004.199832 14 08 e0 01 01 02 00 00 00 00 00 00 00 00 00
 *				E: 000004.426732 14 08 e0 01 01 02 00 00 00 00 00 00 00 00 00
 *				E: 000004.647738 14 08 e0 01 01 02 00 00 00 00 00 00 00 00 00
 *				E: 000004.874733 14 08 e0 01 01 02 00 00 00 00 00 00 00 00 00
 *				E: 000004.930713 14 08 e0 01 01 00 00 00 00 00 00 00 00 00 00 < release
 *
 *			Button 3, press, hold, then release
 *				E: 000006.650346 14 08 e0 01 01 04 00 00 00 00 00 00 00 00 00 < press
 *				E: 000007.051782 14 08 e0 01 01 04 00 00 00 00 00 00 00 00 00 < starting to auto-repeat, every ~225ms
 *				E: 000007.273738 14 08 e0 01 01 04 00 00 00 00 00 00 00 00 00
 *				E: 000007.499794 14 08 e0 01 01 04 00 00 00 00 00 00 00 00 00
 *				E: 000007.726725 14 08 e0 01 01 04 00 00 00 00 00 00 00 00 00
 *				E: 000007.947765 14 08 e0 01 01 04 00 00 00 00 00 00 00 00 00
 *				E: 000008.174755 14 08 e0 01 01 04 00 00 00 00 00 00 00 00 00
 *				E: 000008.328786 14 08 e0 01 01 00 00 00 00 00 00 00 00 00 00 < release
 *
 *			Button 4, press, hold, then release
 *				E: 000009.893820 14 08 e0 01 01 08 00 00 00 00 00 00 00 00 00 < press
 *				E: 000010.274781 14 08 e0 01 01 08 00 00 00 00 00 00 00 00 00 < starting to auto-repeat, every ~225ms
 *				E: 000010.500931 14 08 e0 01 01 08 00 00 00 00 00 00 00 00 00
 *				E: 000010.722777 14 08 e0 01 01 08 00 00 00 00 00 00 00 00 00
 *				E: 000010.948778 14 08 e0 01 01 08 00 00 00 00 00 00 00 00 00
 *				E: 000011.175799 14 08 e0 01 01 08 00 00 00 00 00 00 00 00 00
 *				E: 000011.401153 14 08 e0 01 01 08 00 00 00 00 00 00 00 00 00
 *				E: 000011.432114 14 08 e0 01 01 00 00 00 00 00 00 00 00 00 00 < release
 *
 *			Button 5, press, hold, then release
 *				E: 000013.007778 14 08 e0 01 01 10 00 00 00 00 00 00 00 00 00 < press
 *				E: 000013.424741 14 08 e0 01 01 10 00 00 00 00 00 00 00 00 00 < starting to auto-repeat, every ~225ms
 *				E: 000013.651715 14 08 e0 01 01 10 00 00 00 00 00 00 00 00 00
 *				E: 000013.872763 14 08 e0 01 01 10 00 00 00 00 00 00 00 00 00
 *				E: 000014.099789 14 08 e0 01 01 10 00 00 00 00 00 00 00 00 00
 *				E: 000014.325734 14 08 e0 01 01 10 00 00 00 00 00 00 00 00 00
 *				E: 000014.438080 14 08 e0 01 01 00 00 00 00 00 00 00 00 00 00 < release
 *
 *
 *			Pen: Top-left, then out of range
 *				E: 000368.572184 14 08 80 00 00 00 00 00 00 00 00 fb ed 03 00
 *				E: 000368.573030 14 08 00 00 00 00 00 00 00 00 00 fb ed 03 00
 *
 *			Pen: Bottom-right, then out of range
 *				E: 000544.433185 14 08 80 88 e5 10 81 00 00 00 00 00 00 03 00
 *				E: 000544.434183 14 08 00 88 e5 10 81 00 00 00 00 00 00 03 00
 *
 *			Pen: Max Y tilt (tip of pen points down)
 *				E: 000002.231927 14 08 80 f5 5d 6c 36 00 00 00 00 09 3c 03 00
 *
 *			Pen: Min Y Tilt (tip of pen points up)
 *				E: 000657.593338 14 08 80 5f 69 fa 2c 00 00 00 00 fe c4 03 00
 *
 *			Pen: Max X tilt (tip of pen points left)
 *				E: 000742.246503 14 08 80 2a 4f c4 38 00 00 00 00 3c ed 03 00
 *
 *			Pen: Min X Tilt (tip of pen points right)
 *				E: 000776.404446 14 08 00 18 85 7c 3b 00 00 00 00 c4 ed 03 00
 *
 *			Pen: Tip switch, max pressure, then low pressure
 *				E: 001138.935675 14 08 81 d2 66 04 40 ff 3f 00 00 00 08 03 00
 *
 *				E: 001142.403715 14 08 81 9d 69 47 3e 82 04 00 00 00 07 03 00
 *
 *			Pen: Barrel switch
 *				E: 001210.645652 14 08 82 0d 72 ea 2b 00 00 00 00 db c4 03 00
 *
 *			Pen: Secondary barrel switch
 *				E: 001211.519729 14 08 84 2c 71 51 2b 00 00 00 00 da c4 03 00
 *
 *			Pen: Third switch
 *				E: 001212.443722 14 08 88 1d 72 df 2b 00 00 00 00 dc c4 03 00
 *
 *
 *	HIDRAW 1
 *		No reports
 *
 *
 *	HIDRAW 2
 *		No reports
 *
 *
 *
 *
 *
 *
 *
 *
 * FIRMWARE MODE
 *	HIDRAW 0
 *		No reports
 *
 *
 *	HIDRAW 1
 *		EXAMPLE REPORTS
 *			Top wheel button, *release*
 *				E: 000067.043739 8 03 00 00 00 00 00 00 00
 *
 *			Bottom wheel button, *release*
 *				E: 000068.219161 8 03 00 00 00 00 00 00 00
 *
 *
 *			Button 1, press, then release
 *				E: 000163.767870 8 03 00 05 00 00 00 00 00
 *				E: 000165.969193 8 03 00 00 00 00 00 00 00
 *
 *			Button 2, press, then release
 *				E: 000261.728935 8 03 05 11 00 00 00 00 00
 *				E: 000262.956220 8 03 00 00 00 00 00 00 00
 *
 *			Button 3, press, then release
 *				E: 000289.127881 8 03 01 16 00 00 00 00 00
 *				E: 000290.014594 8 03 00 00 00 00 00 00 00
 *
 *			Button 4, press, then release
 *				E: 000303.025839 8 03 00 2c 00 00 00 00 00
 *				E: 000303.994479 8 03 00 00 00 00 00 00 00
 *
 *			Button 5, press, then release
 *				E: 000315.500835 8 03 05 1d 00 00 00 00 00
 *				E: 000316.603274 8 03 00 00 00 00 00 00 00
 *
 *			BUTTON SUMMARY
 *					1	E: 000163.767870 8 03 00 05 00 00 00 00 00   Keyboard: B
 *					2	E: 000261.728935 8 03 05 11 00 00 00 00 00   Keyboard: LCtrl+LAlt N
 *					3	E: 000289.127881 8 03 01 16 00 00 00 00 00   Keyboard: LCtrl S
 *					4	E: 000303.025839 8 03 00 2c 00 00 00 00 00   Keyboard: Space
 *					5	E: 000315.500835 8 03 05 1d 00 00 00 00 00   Keyboard: LCtrl+LAlt
 *
 *					All buttons (including the wheel buttons) release the same way:
 *						03 00 00 00 00 00 00 00
 *
 *
 *			Pen: Top-left, then out of range
 *				E: 000063.196828 10 0a c0 00 00 00 00 00 00 00 02
 *				E: 000063.197762 10 0a 00 00 00 00 00 00 00 00 02
 *
 *			Pen: Bottom-right, then out of range
 *				E: 000197.123138 10 0a c0 ff 7f ff 7f 00 00 00 00
 *				E: 000197.124915 10 0a 00 ff 7f ff 7f 00 00 00 00
 *
 *			Pen: Max Y Tilt (tip of pen points up)
 *				E: 000291.399541 10 0a c0 19 32 0b 58 00 00 00 3c
 *
 *			Pen: Min Y tilt (tip of pen points down)
 *				E: 000340.888288 10 0a c0 85 40 89 6e 00 00 17 c4
 *
 *			Pen: Max X tilt (tip of pen points left)
 *				E: 000165.575115 10 0a c0 a7 34 99 42 00 00 3c f4
 *
 *			Pen: Min X Tilt (tip of pen points right)
 *				E: 000129.507883 10 0a c0 ea 4b 08 40 00 00 c4 1a
 *
 *			Pen: Tip switch, max pressure, then low pressure
 *				E: 000242.077160 10 0a c1 7e 3c 12 31 ff 3f 03 fd
 *
 *				E: 000339.139188 10 0a c1 ee 3a 9e 32 b5 00 06 f6
 *
 *			Pen: Barrel switch
 *				E: 000037.949777 10 0a c2 5c 28 47 2a 00 00 f6 3c
 *
 *			Pen: Secondary barrel switch
 *				E: 000038.320840 10 0a c4 e4 27 fd 29 00 00 f3 38
 *
 *			Pen: Third switch
 *				E: 000038.923822 10 0a c8 97 27 5f 29 00 00 f2 33
 *
 *
 *	HIDRAW 2
 *		EXAMPLE REPORTS
 *			Either wheel rotation, one detent CW
 *				E: 000097.276573 9 11 00 01 00 00 00 00 00 00
 *
 *			Either wheel rotation, one detent CCW
 *				E: 000153.416538 9 11 00 ff ff 00 00 00 00 00
 *
 *			Either wheel rotation, increasing rotation speed CW
 *				(Note that the wheels on my particular tablet may be
 *				 damaged, so the false rotation direction changes
 *				 that can be observed might not happen on other units.)
 *				E: 000210.514925 9 11 00 01 00 00 00 00 00 00
 *				E: 000210.725718 9 11 00 01 00 00 00 00 00 00
 *				E: 000210.924009 9 11 00 01 00 00 00 00 00 00
 *				E: 000211.205629 9 11 00 01 00 00 00 00 00 00
 *				E: 000211.280521 9 11 00 0b 00 00 00 00 00 00
 *				E: 000211.340121 9 11 00 0e 00 00 00 00 00 00
 *				E: 000211.404018 9 11 00 0d 00 00 00 00 00 00
 *				E: 000211.462060 9 11 00 0e 00 00 00 00 00 00
 *				E: 000211.544886 9 11 00 0a 00 00 00 00 00 00
 *				E: 000211.606130 9 11 00 0d 00 00 00 00 00 00
 *				E: 000211.674560 9 11 00 0c 00 00 00 00 00 00
 *				E: 000211.712039 9 11 00 16 00 00 00 00 00 00
 *				E: 000211.748076 9 11 00 17 00 00 00 00 00 00
 *				E: 000211.786016 9 11 00 17 00 00 00 00 00 00
 *				E: 000211.832960 9 11 00 11 00 00 00 00 00 00
 *				E: 000211.874081 9 11 00 14 00 00 00 00 00 00
 *				E: 000211.925094 9 11 00 10 00 00 00 00 00 00
 *				E: 000211.959048 9 11 00 18 00 00 00 00 00 00
 *				E: 000212.006937 9 11 00 11 00 00 00 00 00 00
 *				E: 000212.050055 9 11 00 13 00 00 00 00 00 00
 *				E: 000212.091947 9 11 00 14 00 00 00 00 00 00
 *				E: 000212.122989 9 11 00 1a 00 00 00 00 00 00
 *				E: 000212.160866 9 11 00 16 00 00 00 00 00 00
 *				E: 000212.194002 9 11 00 19 00 00 00 00 00 00
 *				E: 000212.242249 9 11 00 11 00 00 00 00 00 00
 *				E: 000212.278061 9 11 00 18 00 00 00 00 00 00
 *				E: 000212.328899 9 11 00 10 00 00 00 00 00 00
 *				E: 000212.354005 9 11 00 22 00 00 00 00 00 00
 *				E: 000212.398995 9 11 00 12 00 00 00 00 00 00
 *				E: 000212.432050 9 11 00 19 00 00 00 00 00 00
 *				E: 000212.471164 9 11 00 16 00 00 00 00 00 00
 *				E: 000212.507047 9 11 00 17 00 00 00 00 00 00
 *				E: 000212.540964 9 11 00 19 00 00 00 00 00 00
 *				E: 000212.567942 9 11 00 1f 00 00 00 00 00 00
 *				E: 000212.610007 9 11 00 14 00 00 00 00 00 00
 *				E: 000212.641101 9 11 00 1b 00 00 00 00 00 00
 *				E: 000212.674113 9 11 00 19 00 00 00 00 00 00
 *				E: 000212.674909 9 11 00 01 00 00 00 00 00 00
 *				E: 000212.677062 9 11 00 00 02 00 00 00 00 00
 *				E: 000212.679048 9 11 00 55 01 00 00 00 00 00
 *				E: 000212.682166 9 11 00 55 01 00 00 00 00 00
 *				E: 000212.682788 9 11 00 ff ff 00 00 00 00 00
 *				E: 000212.683899 9 11 00 01 00 00 00 00 00 00
 *				E: 000212.685827 9 11 00 67 fe 00 00 00 00 00
 *				E: 000212.686941 9 11 00 00 08 00 00 00 00 00
 *				E: 000212.727840 9 11 00 14 00 00 00 00 00 00
 *				E: 000212.772884 9 11 00 13 00 00 00 00 00 00
 *				E: 000212.810975 9 11 00 16 00 00 00 00 00 00
 *				E: 000212.811793 9 11 00 00 08 00 00 00 00 00
 *				E: 000212.812683 9 11 00 01 00 00 00 00 00 00
 *				E: 000212.813905 9 11 00 01 00 00 00 00 00 00
 *				E: 000212.814909 9 11 00 00 04 00 00 00 00 00
 *				E: 000212.816942 9 11 00 01 00 00 00 00 00 00
 *				E: 000212.817851 9 11 00 ff ff 00 00 00 00 00
 *				E: 000212.818752 9 11 00 01 00 00 00 00 00 00
 *				E: 000212.819910 9 11 00 56 fd 00 00 00 00 00
 *				E: 000212.820781 9 11 00 ff ff 00 00 00 00 00
 *				E: 000212.821811 9 11 00 00 04 00 00 00 00 00
 *				E: 000212.822920 9 11 00 00 08 00 00 00 00 00
 *				E: 000212.823861 9 11 00 00 02 00 00 00 00 00
 *				E: 000212.828781 9 11 00 ba 00 00 00 00 00 00
 *				E: 000212.874097 9 11 00 12 00 00 00 00 00 00
 *				E: 000212.874872 9 11 00 00 fc 00 00 00 00 00
 *				E: 000212.876136 9 11 00 00 fc 00 00 00 00 00
 *				E: 000212.877036 9 11 00 00 f8 00 00 00 00 00
 *				E: 000212.877993 9 11 00 00 f8 00 00 00 00 00
 *				E: 000212.879748 9 11 00 01 00 00 00 00 00 00
 *				E: 000212.880728 9 11 00 01 00 00 00 00 00 00
 *				E: 000212.881956 9 11 00 00 04 00 00 00 00 00
 *				E: 000212.885065 9 11 00 ff ff 00 00 00 00 00
 *				E: 000212.917060 9 11 00 1a 00 00 00 00 00 00
 *				E: 000212.936458 9 11 00 2d 00 00 00 00 00 00
 *				E: 000212.957860 9 11 00 25 00 00 00 00 00 00
 *				E: 000212.984019 9 11 00 20 00 00 00 00 00 00
 *				E: 000213.017915 9 11 00 19 00 00 00 00 00 00
 *				E: 000213.039973 9 11 00 27 00 00 00 00 00 00
 *				E: 000213.065933 9 11 00 21 00 00 00 00 00 00
 *				E: 000213.085807 9 11 00 28 00 00 00 00 00 00
 *				E: 000213.108888 9 11 00 25 00 00 00 00 00 00
 *				E: 000213.129726 9 11 00 29 00 00 00 00 00 00
 *				E: 000213.172043 9 11 00 14 00 00 00 00 00 00
 *				E: 000213.195873 9 11 00 23 00 00 00 00 00 00
 *				E: 000213.222884 9 11 00 20 00 00 00 00 00 00
 *				E: 000213.243220 9 11 00 2a 00 00 00 00 00 00
 *				E: 000213.266778 9 11 00 24 00 00 00 00 00 00
 *				E: 000213.285951 9 11 00 2b 00 00 00 00 00 00
 *				E: 000213.306045 9 11 00 2a 00 00 00 00 00 00
 *				E: 000213.306796 9 11 00 ff ff 00 00 00 00 00
 *				E: 000213.307755 9 11 00 ff ff 00 00 00 00 00
 *				E: 000213.308820 9 11 00 ff ff 00 00 00 00 00
 *				E: 000213.309971 9 11 00 ff ff 00 00 00 00 00
 *				E: 000213.310980 9 11 00 01 00 00 00 00 00 00
 *				E: 000213.311853 9 11 00 01 00 00 00 00 00 00
 *				E: 000213.312861 9 11 00 aa 02 00 00 00 00 00
 *				E: 000213.313884 9 11 00 00 f8 00 00 00 00 00
 *				E: 000213.315111 9 11 00 ff ff 00 00 00 00 00
 *				E: 000213.315992 9 11 00 01 00 00 00 00 00 00
 *				E: 000213.316955 9 11 00 00 08 00 00 00 00 00
 *				E: 000213.346065 9 11 00 1d 00 00 00 00 00 00
 *				E: 000213.346963 9 11 00 ff ff 00 00 00 00 00
 *				E: 000213.347874 9 11 00 00 08 00 00 00 00 00
 *				E: 000213.348736 9 11 00 00 08 00 00 00 00 00
 *				E: 000213.349795 9 11 00 00 04 00 00 00 00 00
 *				E: 000213.350791 9 11 00 01 00 00 00 00 00 00
 *				E: 000213.351791 9 11 00 01 00 00 00 00 00 00
 *				E: 000213.352729 9 11 00 00 f8 00 00 00 00 00
 *				E: 000213.353811 9 11 00 01 00 00 00 00 00 00
 *				E: 000213.354755 9 11 00 00 f8 00 00 00 00 00
 *				E: 000213.355795 9 11 00 00 f8 00 00 00 00 00
 *				E: 000213.356813 9 11 00 01 00 00 00 00 00 00
 *				E: 000213.357817 9 11 00 00 04 00 00 00 00 00
 *				E: 000213.393838 9 11 00 17 00 00 00 00 00 00
 *				E: 000213.394719 9 11 00 00 04 00 00 00 00 00
 *				E: 000213.395682 9 11 00 00 08 00 00 00 00 00
 *				E: 000213.396679 9 11 00 00 04 00 00 00 00 00
 *				E: 000213.397651 9 11 00 00 fc 00 00 00 00 00
 *				E: 000213.398661 9 11 00 ff ff 00 00 00 00 00
 *				E: 000213.400308 9 11 00 56 fd 00 00 00 00 00
 *				E: 000213.400909 9 11 00 00 f8 00 00 00 00 00
 *				E: 000213.401837 9 11 00 01 00 00 00 00 00 00
 *
 *			Either wheel rotation, increasing rotation speed CCW
 *				(Note that the wheels on my particular tablet may be
 *				 damaged, so the false rotation direction changes
 *				 that can be observed might not happen on other units.)
 *				E: 000040.527820 9 11 00 ff ff 00 00 00 00 00
 *				E: 000040.816644 9 11 00 ff ff 00 00 00 00 00
 *				E: 000040.880423 9 11 00 f3 ff 00 00 00 00 00
 *				E: 000040.882570 9 11 00 ff ff 00 00 00 00 00
 *				E: 000040.883381 9 11 00 ff ff 00 00 00 00 00
 *				E: 000040.885463 9 11 00 aa 02 00 00 00 00 00
 *				E: 000040.924106 9 11 00 ea ff 00 00 00 00 00
 *				E: 000041.006155 9 11 00 f6 ff 00 00 00 00 00
 *				E: 000041.085799 9 11 00 f6 ff 00 00 00 00 00
 *				E: 000041.168492 9 11 00 f6 ff 00 00 00 00 00
 *				E: 000041.233453 9 11 00 f3 ff 00 00 00 00 00
 *				E: 000041.296641 9 11 00 f3 ff 00 00 00 00 00
 *				E: 000041.370302 9 11 00 f5 ff 00 00 00 00 00
 *				E: 000041.437410 9 11 00 f4 ff 00 00 00 00 00
 *				E: 000041.474514 9 11 00 e9 ff 00 00 00 00 00
 *				E: 000041.522171 9 11 00 ef ff 00 00 00 00 00
 *				E: 000041.568160 9 11 00 ee ff 00 00 00 00 00
 *				E: 000041.608146 9 11 00 ec ff 00 00 00 00 00
 *				E: 000041.627132 9 11 00 d3 ff 00 00 00 00 00
 *				E: 000041.656151 9 11 00 e3 ff 00 00 00 00 00
 *				E: 000041.682264 9 11 00 e0 ff 00 00 00 00 00
 *				E: 000041.714186 9 11 00 e6 ff 00 00 00 00 00
 *				E: 000041.740339 9 11 00 e0 ff 00 00 00 00 00
 *				E: 000041.772087 9 11 00 e5 ff 00 00 00 00 00
 *				E: 000041.801093 9 11 00 e3 ff 00 00 00 00 00
 *				E: 000041.834051 9 11 00 e7 ff 00 00 00 00 00
 *				E: 000041.863094 9 11 00 e3 ff 00 00 00 00 00
 *				E: 000041.901016 9 11 00 ea ff 00 00 00 00 00
 *				E: 000041.901956 9 11 00 00 04 00 00 00 00 00
 *				E: 000041.902837 9 11 00 00 fe 00 00 00 00 00
 *				E: 000041.903927 9 11 00 01 00 00 00 00 00 00
 *				E: 000041.905066 9 11 00 01 00 00 00 00 00 00
 *				E: 000041.907214 9 11 00 00 fe 00 00 00 00 00
 *				E: 000041.909011 9 11 00 01 00 00 00 00 00 00
 *				E: 000041.909953 9 11 00 01 00 00 00 00 00 00
 *				E: 000041.910917 9 11 00 00 08 00 00 00 00 00
 *				E: 000041.913280 9 11 00 00 fe 00 00 00 00 00
 *				E: 000041.914121 9 11 00 56 fd 00 00 00 00 00
 *				E: 000041.915346 9 11 00 ff ff 00 00 00 00 00
 *				E: 000041.962101 9 11 00 ee ff 00 00 00 00 00
 *				E: 000041.964062 9 11 00 56 fd 00 00 00 00 00
 *				E: 000041.964978 9 11 00 00 fc 00 00 00 00 00
 *				E: 000041.968058 9 11 00 24 01 00 00 00 00 00
 *				E: 000041.968880 9 11 00 56 fd 00 00 00 00 00
 *				E: 000041.970977 9 11 00 aa 02 00 00 00 00 00
 *				E: 000041.971932 9 11 00 ff ff 00 00 00 00 00
 *				E: 000041.972943 9 11 00 01 00 00 00 00 00 00
 *				E: 000041.975291 9 11 00 ff ff 00 00 00 00 00
 *				E: 000041.978274 9 11 00 01 00 00 00 00 00 00
 *				E: 000042.035079 9 11 00 01 00 00 00 00 00 00
 *				E: 000042.041283 9 11 00 ff ff 00 00 00 00 00
 *				E: 000042.042057 9 11 00 00 04 00 00 00 00 00
 *				E: 000042.045169 9 11 00 ff ff 00 00 00 00 00
 *				E: 000042.051242 9 11 00 ff ff 00 00 00 00 00
 *				E: 000042.056099 9 11 00 63 ff 00 00 00 00 00
 *				E: 000042.106329 9 11 00 ef ff 00 00 00 00 00
 *				E: 000042.108601 9 11 00 ff ff 00 00 00 00 00
 *				E: 000042.116259 9 11 00 6b 00 00 00 00 00 00
 *				E: 000042.119140 9 11 00 55 01 00 00 00 00 00
 *				E: 000042.126101 9 11 00 88 ff 00 00 00 00 00
 *				E: 000042.158009 9 11 00 e6 ff 00 00 00 00 00
 *				E: 000042.172108 9 11 00 be ff 00 00 00 00 00
 *				E: 000042.207417 9 11 00 e8 ff 00 00 00 00 00
 *				E: 000042.223155 9 11 00 cc ff 00 00 00 00 00
 *				E: 000042.255185 9 11 00 e6 ff 00 00 00 00 00
 *				E: 000042.276280 9 11 00 d7 ff 00 00 00 00 00
 *				E: 000042.302128 9 11 00 e0 ff 00 00 00 00 00
 *				E: 000042.317423 9 11 00 c8 ff 00 00 00 00 00
 *				E: 000042.345226 9 11 00 e1 ff 00 00 00 00 00
 *				E: 000042.357243 9 11 00 bc ff 00 00 00 00 00
 *				E: 000042.381308 9 11 00 dc ff 00 00 00 00 00
 *				E: 000042.383180 9 11 00 dc fe 00 00 00 00 00
 *				E: 000042.412288 9 11 00 e3 ff 00 00 00 00 00
 *				E: 000042.451216 9 11 00 eb ff 00 00 00 00 00
 *				E: 000042.478372 9 11 00 e0 ff 00 00 00 00 00
 *				E: 000042.502116 9 11 00 dd ff 00 00 00 00 00
 *				E: 000042.520105 9 11 00 d3 ff 00 00 00 00 00
 *				E: 000042.540345 9 11 00 d6 ff 00 00 00 00 00
 *				E: 000042.541021 9 11 00 00 08 00 00 00 00 00
 *				E: 000042.542009 9 11 00 01 00 00 00 00 00 00
 *				E: 000042.543045 9 11 00 00 04 00 00 00 00 00
 *				E: 000042.544279 9 11 00 ff ff 00 00 00 00 00
 *				E: 000042.545097 9 11 00 ff ff 00 00 00 00 00
 *				E: 000042.546074 9 11 00 00 08 00 00 00 00 00
 *				E: 000042.547237 9 11 00 00 08 00 00 00 00 00
 *				E: 000042.548029 9 11 00 ff ff 00 00 00 00 00
 *				E: 000042.549304 9 11 00 00 f8 00 00 00 00 00
 *				E: 000042.553123 9 11 00 00 ff 00 00 00 00 00
 *				E: 000042.581186 9 11 00 e1 ff 00 00 00 00 00
 *				E: 000042.582238 9 11 00 00 f8 00 00 00 00 00
 *				E: 000042.583150 9 11 00 00 fc 00 00 00 00 00
 *				E: 000042.584273 9 11 00 00 f8 00 00 00 00 00
 *				E: 000042.585019 9 11 00 00 fc 00 00 00 00 00
 *				E: 000042.586059 9 11 00 01 00 00 00 00 00 00
 *				E: 000042.589012 9 11 00 67 fe 00 00 00 00 00
 *				E: 000042.590066 9 11 00 00 fc 00 00 00 00 00
 *				E: 000042.592916 9 11 00 dc fe 00 00 00 00 00
 *				E: 000042.621124 9 11 00 e1 ff 00 00 00 00 00
 *				E: 000042.622092 9 11 00 ff ff 00 00 00 00 00
 *				E: 000042.623069 9 11 00 01 00 00 00 00 00 00
 *				E: 000042.624030 9 11 00 ff ff 00 00 00 00 00
 *				E: 000042.625006 9 11 00 00 08 00 00 00 00 00
 *				E: 000042.626068 9 11 00 00 04 00 00 00 00 00
 *				E: 000042.626876 9 11 00 00 08 00 00 00 00 00
 *				E: 000042.628392 9 11 00 00 08 00 00 00 00 00
 *				E: 000042.628918 9 11 00 01 00 00 00 00 00 00
 *				E: 000042.630009 9 11 00 ff ff 00 00 00 00 00
 *				E: 000042.631934 9 11 00 00 fe 00 00 00 00 00
 *				E: 000042.656285 9 11 00 dd ff 00 00 00 00 00
 *				E: 000042.659870 9 11 00 cc 00 00 00 00 00 00
 *				E: 000042.666128 9 11 00 9d 00 00 00 00 00 00
 *				E: 000042.672458 9 11 00 80 ff 00 00 00 00 00
 *				E: 000042.696106 9 11 00 dc ff 00 00 00 00 00
 *				E: 000042.705129 9 11 00 61 00 00 00 00 00 00
 *				E: 000042.731303 9 11 00 e0 ff 00 00 00 00 00
 *				E: 000042.741278 9 11 00 ab ff 00 00 00 00 00
 *				E: 000042.788181 9 11 00 ee ff 00 00 00 00 00
 *				E: 000042.810441 9 11 00 db ff 00 00 00 00 00
 *				E: 000042.838073 9 11 00 e1 ff 00 00 00 00 00
 *				E: 000042.852235 9 11 00 c4 ff 00 00 00 00 00
 *				E: 000042.882290 9 11 00 e4 ff 00 00 00 00 00
 *
 *			Either wheel button, press, hold, then release
 *				E: 000202.084982 9 11 02 00 00 00 00 00 00 00
 *				E: 000202.090172 9 11 03 00 00 00 00 00 00 00
 *				E: 000202.094139 9 11 03 00 00 00 00 00 00 00
 *				E: 000202.099172 9 11 03 00 00 00 00 00 00 00
 *				E: 000202.105055 9 11 03 00 00 00 00 00 00 00
 *				E: 000202.109132 9 11 03 00 00 00 00 00 00 00
 *				E: 000202.114185 9 11 03 00 00 00 00 00 00 00
 *				E: 000202.119212 9 11 03 00 00 00 00 00 00 00
 *				E: 000202.124264 9 11 03 00 00 00 00 00 00 00
 *				E: 000202.130147 9 11 03 00 00 00 00 00 00 00
 *				E: 000202.135138 9 11 03 00 00 00 00 00 00 00
 *				E: 000202.140072 9 11 03 00 00 00 00 00 00 00
 *				E: 000202.145146 9 11 03 00 00 00 00 00 00 00
 *				E: 000202.150157 9 11 03 00 00 00 00 00 00 00
 *				E: 000202.155339 9 11 03 00 00 00 00 00 00 00
 *				E: 000202.160064 9 11 03 00 00 00 00 00 00 00
 *				E: 000202.165026 9 11 03 00 00 00 00 00 00 00
 *				E: 000202.170037 9 11 03 00 00 00 00 00 00 00
 *				E: 000202.175154 9 11 03 00 00 00 00 00 00 00
 *				E: 000202.180044 9 11 03 00 00 00 00 00 00 00
 *				E: 000202.186280 9 11 03 00 00 00 00 00 00 00
 *				E: 000202.191281 9 11 03 00 00 00 00 00 00 00
 *				E: 000202.196106 9 11 03 00 00 00 00 00 00 00
 *				E: 000202.201083 9 11 03 00 00 00 00 00 00 00
 *				E: 000202.206166 9 11 03 00 00 00 00 00 00 00
 *				E: 000202.211084 9 11 03 00 00 00 00 00 00 00
 *				E: 000202.216175 9 11 03 00 00 00 00 00 00 00
 *				E: 000202.221036 9 11 03 00 00 00 00 00 00 00
 *				E: 000202.226271 9 11 03 00 00 00 00 00 00 00
 *				E: 000202.231150 9 11 03 00 00 00 00 00 00 00
 *				E: 000202.235924 9 11 03 00 00 00 00 00 00 00
 *				E: 000202.242046 9 11 03 00 00 00 00 00 00 00
 *				E: 000202.247164 9 11 03 00 00 00 00 00 00 00
 *				E: 000202.252359 9 11 03 00 00 00 00 00 00 00
 *				E: 000202.257295 9 11 03 00 00 00 00 00 00 00
 *				E: 000202.262167 9 11 03 00 00 00 00 00 00 00
 *				E: 000202.267081 9 11 03 00 00 00 00 00 00 00
 *				E: 000202.272175 9 11 03 00 00 00 00 00 00 00
 *				E: 000202.277085 9 11 03 00 00 00 00 00 00 00
 *				E: 000202.282596 9 11 03 00 00 00 00 00 00 00
 *				E: 000202.287078 9 11 03 00 00 00 00 00 00 00
 *				E: 000202.292191 9 11 03 00 00 00 00 00 00 00
 *				E: 000202.298196 9 11 03 00 00 00 00 00 00 00
 *				E: 000202.303004 9 11 03 00 00 00 00 00 00 00
 *				E: 000202.308113 9 11 03 00 00 00 00 00 00 00
 *				E: 000202.313079 9 11 03 00 00 00 00 00 00 00
 *				E: 000202.318243 9 11 03 00 00 00 00 00 00 00
 *				E: 000202.323309 9 11 03 00 00 00 00 00 00 00
 *				E: 000202.328190 9 11 03 00 00 00 00 00 00 00
 *				E: 000202.333050 9 11 03 00 00 00 00 00 00 00
 *				E: 000202.338162 9 11 03 00 00 00 00 00 00 00
 *				E: 000202.343022 9 11 03 00 00 00 00 00 00 00
 *				E: 000202.348113 9 11 03 00 00 00 00 00 00 00
 *				E: 000202.354133 9 11 03 00 00 00 00 00 00 00
 *				E: 000202.359132 9 11 03 00 00 00 00 00 00 00
 *				E: 000202.364053 9 11 03 00 00 00 00 00 00 00
 *				E: 000202.369034 9 11 03 00 00 00 00 00 00 00
 *				E: 000202.374144 9 11 03 00 00 00 00 00 00 00
 *				E: 000202.379027 9 11 03 00 00 00 00 00 00 00
 *				E: 000202.384238 9 11 03 00 00 00 00 00 00 00
 *				E: 000202.389249 9 11 03 00 00 00 00 00 00 00
 *				E: 000202.394049 9 11 03 00 00 00 00 00 00 00
 *				E: 000202.398949 9 11 03 00 00 00 00 00 00 00
 *				E: 000202.404203 9 11 03 00 00 00 00 00 00 00
 *				E: 000202.410098 9 11 03 00 00 00 00 00 00 00
 *				E: 000202.415237 9 11 00 00 00 00 00 00 00 00
 *
 *
 *			Top wheel button press and release while holding bottom wheel button
 *				(The reverse action (a bottom wheel button press while holding the top wheel button) is invisible.)
 *				E: 000071.126966 9 11 03 00 00 00 00 00 00 00
 *				E: 000071.133117 9 11 03 00 00 00 00 00 00 00
 *				E: 000071.137481 9 11 03 00 00 00 00 00 00 00
 *				E: 000071.142036 9 11 03 00 00 00 00 00 00 00
 *				E: 000071.147027 9 11 03 00 00 00 00 00 00 00
 *				E: 000071.151988 9 11 03 00 00 00 00 00 00 00
 *				E: 000071.157945 9 11 03 00 00 00 00 00 00 00
 *				E: 000071.163657 9 11 03 00 00 00 00 00 00 00
 *				E: 000071.168240 9 11 03 00 00 00 00 00 00 00
 *				E: 000071.173109 9 11 02 00 00 00 00 00 00 00 < top wheel button press?
 *				E: 000071.178119 9 11 03 00 00 00 00 00 00 00
 *				E: 000071.183046 9 11 03 00 00 00 00 00 00 00
 *				E: 000071.187983 9 11 03 00 00 00 00 00 00 00
 *				E: 000071.192996 9 11 03 00 00 00 00 00 00 00
 *				E: 000071.198341 9 11 03 00 00 00 00 00 00 00
 *				E: 000071.203122 9 11 03 00 00 00 00 00 00 00
 *				E: 000071.208998 9 11 03 00 00 00 00 00 00 00
 *				E: 000071.214037 9 11 03 00 00 00 00 00 00 00
 *				E: 000071.218945 9 11 03 00 00 00 00 00 00 00
 *				E: 000071.223835 9 11 03 00 00 00 00 00 00 00
 *				E: 000071.228987 9 11 03 00 00 00 00 00 00 00
 *				E: 000071.234082 9 11 03 00 00 00 00 00 00 00
 *				E: 000071.239028 9 11 03 00 00 00 00 00 00 00
 *				E: 000071.244307 9 11 00 00 00 00 00 00 00 00 < top wheel button release?
 *				E: 000071.245867 9 11 03 00 00 00 00 00 00 00 < continued hold of bottom button
 *				E: 000071.249959 9 11 03 00 00 00 00 00 00 00
 *				E: 000071.255032 9 11 03 00 00 00 00 00 00 00
 *				E: 000071.259972 9 11 03 00 00 00 00 00 00 00
 *				E: 000071.265409 9 11 03 00 00 00 00 00 00 00
 *				E: 000071.270156 9 11 03 00 00 00 00 00 00 00
 *				E: 000071.275530 9 11 03 00 00 00 00 00 00 00
 *				E: 000071.279975 9 11 03 00 00 00 00 00 00 00
 *				E: 000071.285046 9 11 03 00 00 00 00 00 00 00
 *				E: 000071.290906 9 11 03 00 00 00 00 00 00 00
 *				E: 000071.296146 9 11 03 00 00 00 00 00 00 00
 *				E: 000071.301288 9 11 03 00 00 00 00 00 00 00
 *
 *			Top wheel button hold while top wheel rotate CCW
 *				(I did not test the other combinations of this)
 *				E: 000022.253144 9 11 03 00 00 00 00 00 00 00
 *				E: 000022.258157 9 11 03 00 00 00 00 00 00 00
 *				E: 000022.262011 9 11 00 ff ff 00 00 00 00 00
 *				E: 000022.264015 9 11 03 00 00 00 00 00 00 00
 *				E: 000022.268976 9 11 03 00 00 00 00 00 00 00
 *
 *
 *
 *
 *
 *
 *
 *
 * NOTES ON SIMULTANEOUS BUTTON HOLDS
 *	(applies to vendor mode only)
 *	Value replacements for ease of reading:
 *		.7 = 0x40 (button 7, a wheel button)
 *		.1 = 0x01 (button 1, a pad button)
 *		rr = 0x00 (no buttons pressed)
 *
 *	Press 7
 *	Press 1
 *	Release 7
 *	Release 1
 *		B: 000000.000152 42 08 e0 01 01    .7 00 00 00 00 00 00 00 00 00
 *		B: 000000.781784 42 08 e0 01 01    .1 00 00 00 00 00 00 00 00 00
 *		B: 000000.869845 42 08 e0 01 01    .1 00 00 00 00 00 00 00 00 00
 *		B: 000001.095688 42 08 e0 01 01    .1 00 00 00 00 00 00 00 00 00
 *		B: 000001.322635 42 08 e0 01 01    .1 00 00 00 00 00 00 00 00 00
 *		B: 000001.543643 42 08 e0 01 01    .1 00 00 00 00 00 00 00 00 00
 *		B: 000001.770652 42 08 e0 01 01    .1 00 00 00 00 00 00 00 00 00
 *		B: 000001.885659 42 08 e0 01 01    rr 00 00 00 00 00 00 00 00 00	release of 7
 *		B: 000001.993620 42 08 e0 01 01    .1 00 00 00 00 00 00 00 00 00
 *		B: 000002.220671 42 08 e0 01 01    .1 00 00 00 00 00 00 00 00 00
 *		B: 000002.446589 42 08 e0 01 01    .1 00 00 00 00 00 00 00 00 00
 *		B: 000002.672559 42 08 e0 01 01    .1 00 00 00 00 00 00 00 00 00
 *		B: 000002.765183 42 08 e0 01 01    rr 00 00 00 00 00 00 00 00 00	release of 1
 *
 *	Press 7
 *	Press 1
 *	Release 1
 *	Release 7
 *		B: 000017.071517 42 08 e0 01 01    .7 00 00 00 00 00 00 00 00 00
 *		B: 000018.270461 42 08 e0 01 01    .1 00 00 00 00 00 00 00 00 00
 *		B: 000018.419486 42 08 e0 01 01    .1 00 00 00 00 00 00 00 00 00
 *		B: 000018.646438 42 08 e0 01 01    .1 00 00 00 00 00 00 00 00 00
 *		B: 000018.872493 42 08 e0 01 01    .1 00 00 00 00 00 00 00 00 00
 *		B: 000019.094422 42 08 e0 01 01    .1 00 00 00 00 00 00 00 00 00
 *		B: 000019.320488 42 08 e0 01 01    .1 00 00 00 00 00 00 00 00 00
 *		B: 000020.360505 42 08 e0 01 01    rr 00 00 00 00 00 00 00 00 00	release of 1 is not reported until 7 is released, then both are rapidly reported
 *		B: 000020.361091 42 08 e0 01 01    rr 00 00 00 00 00 00 00 00 00
 *
 *	Press 1
 *	Press 7
 *	Release 7
 *	Release 1
 *		B: 000031.516315 42 08 e0 01 01    .1 00 00 00 00 00 00 00 00 00
 *		B: 000031.922299 42 08 e0 01 01    .1 00 00 00 00 00 00 00 00 00
 *		B: 000032.144165 42 08 e0 01 01    .1 00 00 00 00 00 00 00 00 00
 *		B: 000032.370262 42 08 e0 01 01    .1 00 00 00 00 00 00 00 00 00
 *		B: 000032.396242 42 08 e0 01 01    .7 00 00 00 00 00 00 00 00 00
 *		B: 000032.597270 42 08 e0 01 01    .1 00 00 00 00 00 00 00 00 00
 *		B: 000032.818187 42 08 e0 01 01    .1 00 00 00 00 00 00 00 00 00
 *		B: 000033.045143 42 08 e0 01 01    .1 00 00 00 00 00 00 00 00 00
 *		B: 000033.267535 42 08 e0 01 01    rr 00 00 00 00 00 00 00 00 00	release of 7
 *		B: 000033.272602 42 08 e0 01 01    .1 00 00 00 00 00 00 00 00 00
 *		B: 000033.494246 42 08 e0 01 01    .1 00 00 00 00 00 00 00 00 00
 *		B: 000033.721266 42 08 e0 01 01    .1 00 00 00 00 00 00 00 00 00
 *		B: 000033.947237 42 08 e0 01 01    .1 00 00 00 00 00 00 00 00 00
 *		B: 000034.169294 42 08 e0 01 01    .1 00 00 00 00 00 00 00 00 00
 *		B: 000034.183585 42 08 e0 01 01    rr 00 00 00 00 00 00 00 00 00	release of 1
 *
 *	Press 1
 *	Press 7
 *	Release 1
 *	Release 7
 *		B: 000056.628429 42 08 e0 01 01    .1 00 00 00 00 00 00 00 00 00
 *		B: 000057.046348 42 08 e0 01 01    .1 00 00 00 00 00 00 00 00 00
 *		B: 000057.272044 42 08 e0 01 01    .1 00 00 00 00 00 00 00 00 00
 *		B: 000057.494434 42 08 e0 01 01    .1 00 00 00 00 00 00 00 00 00
 *		B: 000057.601224 42 08 e0 01 01    .7 00 00 00 00 00 00 00 00 00
 *		B: 000057.719262 42 08 e0 01 01    .1 00 00 00 00 00 00 00 00 00
 *		B: 000057.946941 42 08 e0 01 01    .1 00 00 00 00 00 00 00 00 00
 *		B: 000058.172346 42 08 e0 01 01    .1 00 00 00 00 00 00 00 00 00
 *		B: 000058.393994 42 08 e0 01 01    .1 00 00 00 00 00 00 00 00 00
 *		B: 000059.434576 42 08 e0 01 01    rr 00 00 00 00 00 00 00 00 00	release of 1 is not reported until 7 is released, then both are rapidly reported
 *		B: 000059.435857 42 08 e0 01 01    rr 00 00 00 00 00 00 00 00 00
 */


/* Filled in by udev-hid-bpf */
char UDEV_PROP_HUION_FIRMWARE_ID[64];

char EXPECTED_FIRMWARE_ID[] = "HUION_M22c_";

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
				 * The tablet has a logical maximum of 58760 x 33040
				 * and a claimed resolution of 5080 LPI (200 L/mm)
				 * This works out to a physical maximum of
				 * 293.8 x 165.2mm, which matches Huion's advertised
				 * active area dimensions from
				 * https://www.huion.com/products/pen_display/Kamvas/kamvas-13-gen-3.html
				 */
				LogicalMaximum_i16(58760)
				PhysicalMaximum_i16(2938)
				Usage_GD_X
				Input(Var|Abs)
				LogicalMaximum_i16(33040)
				PhysicalMaximum_i16(1652)
				Usage_GD_Y
				Input(Var|Abs)
			)
			LogicalMinimum_i16(0)
			LogicalMaximum_i16(16383)
			Usage_Dig_TipPressure
			Input(Var|Abs)
			ReportCount(1)
			Input(Const)
			ReportSize(8)
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
			 * (data[4] & 0x20)	button 6 (top wheel button)
			 * (data[4] & 0x40)	button 7 (bottom wheel button)
			 */
			ReportSize(1)
			ReportCount(7)
			UsagePage_Button
			UsageMinimum_i8(1)
			UsageMaximum_i8(7)
			Input(Var|Abs)
			ReportCount(1)
			Input(Const)
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
int BPF_PROG(hid_fix_rdesc_huion_kamvas13_gen3, struct hid_bpf_ctx *hid_ctx)
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
int BPF_PROG(hid_fix_event_huion_kamvas13_gen3, struct hid_bpf_ctx *hid_ctx)
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

HID_BPF_OPS(huion_kamvas13_gen3) = {
	.hid_device_event = (void *)hid_fix_event_huion_kamvas13_gen3,
	.hid_rdesc_fixup = (void *)hid_fix_rdesc_huion_kamvas13_gen3,
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
