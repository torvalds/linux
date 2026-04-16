// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2024 Red Hat, Inc
 */

#include "vmlinux.h"
#include "hid_bpf.h"
#include "hid_bpf_helpers.h"
#include "hid_report_helpers.h"
#include <bpf/bpf_tracing.h>

#define VID_HUION 0x256C
#define PID_KEYDIAL_K20_BLUETOOTH 0x8251

HID_BPF_CONFIG(
	HID_DEVICE(BUS_BLUETOOTH, HID_GROUP_GENERIC, VID_HUION, PID_KEYDIAL_K20_BLUETOOTH),
);

/* This is the same device as in 0010-Huion__KeydialK20 but connected via Bluetooth.
 * It does not need (to support?) switching to a vendor mode so we just modify the
 * existing mode.
 *
 * By default it exports two hidraw nodes, only the second one sends events.
 *
 * This is the first hidraw node which we disable:
 *
 * # Keydial mini-050
 * # Report descriptor length: 114 bytes
 * #   Bytes                          // Field Name                              Offset
 * # ----------------------------------------------------------------------------------
 * # 🮥 0x05, 0x01,                    // Usage Page (Generic Desktop)              0
 * # 🭬 0x09, 0x0e,                    // Usage (System Multi-Axis Controller)      2
 * #   0xa1, 0x01,                    // Collection (Application)                  4
 * # ┅ 0x85, 0x03,                    //   Report ID (3)                           6
 * # 🮥 0x05, 0x0d,                    //   Usage Page (Digitizers)                 8
 * #   0x75, 0x08,                    //   Report Size (8)                         10
 * #   0x95, 0x01,                    //   Report Count (1)                        12
 * # ┇ 0x81, 0x01,                    //   Input (Cnst,Arr,Abs)                    14
 * # 🭬 0x09, 0x21,                    //   Usage (Puck)                            16
 * #   0xa1, 0x02,                    //   Collection (Logical)                    18
 * #   0x15, 0x00,                    //     Logical Minimum (0)                   20
 * #   0x25, 0x01,                    //     Logical Maximum (1)                   22
 * #   0x75, 0x01,                    //     Report Size (1)                       24
 * #   0x95, 0x01,                    //     Report Count (1)                      26
 * #   0xa1, 0x00,                    //     Collection (Physical)                 28
 * # 🮥 0x05, 0x09,                    //       Usage Page (Button)                 30
 * # 🭬 0x09, 0x01,                    //       Usage (Button 1)                    32
 * # ┇ 0x81, 0x02,                    //       Input (Data,Var,Abs)                34
 * # 🮥 0x05, 0x0d,                    //       Usage Page (Digitizers)             36
 * # 🭬 0x09, 0x33,                    //       Usage (Touch)                       38
 * # ┇ 0x81, 0x02,                    //       Input (Data,Var,Abs)                40
 * #   0x95, 0x06,                    //       Report Count (6)                    42
 * # ┇ 0x81, 0x03,                    //       Input (Cnst,Var,Abs)                44
 * #   0xa1, 0x02,                    //       Collection (Logical)                46
 * # 🮥 0x05, 0x01,                    //         Usage Page (Generic Desktop)      48
 * # 🭬 0x09, 0x37,                    //         Usage (Dial)                      50
 * #   0x16, 0x00, 0x80,              //         Logical Minimum (32768)           52
 * #   0x26, 0xff, 0x7f,              //         Logical Maximum (32767)           55
 * #   0x75, 0x10,                    //         Report Size (16)                  58
 * #   0x95, 0x01,                    //         Report Count (1)                  60
 * # ┇ 0x81, 0x06,                    //         Input (Data,Var,Rel)              62
 * #   0x35, 0x00,                    //         Physical Minimum (0)              64
 * #   0x46, 0x10, 0x0e,              //         Physical Maximum (3600)           66
 * #   0x15, 0x00,                    //         Logical Minimum (0)               69
 * #   0x26, 0x10, 0x0e,              //         Logical Maximum (3600)            71
 * # 🭬 0x09, 0x48,                    //         Usage (Resolution Multiplier)     74
 * # ║ 0xb1, 0x02,                    //         Feature (Data,Var,Abs)            76
 * #   0x45, 0x00,                    //         Physical Maximum (0)              78
 * #   0xc0,                          //       End Collection                      80
 * #   0x75, 0x08,                    //       Report Size (8)                     81
 * #   0x95, 0x01,                    //       Report Count (1)                    83
 * # ┇ 0x81, 0x01,                    //       Input (Cnst,Arr,Abs)                85
 * #   0x75, 0x08,                    //       Report Size (8)                     87
 * #   0x95, 0x01,                    //       Report Count (1)                    89
 * # ┇ 0x81, 0x01,                    //       Input (Cnst,Arr,Abs)                91
 * #   0x75, 0x08,                    //       Report Size (8)                     93
 * #   0x95, 0x01,                    //       Report Count (1)                    95
 * # ┇ 0x81, 0x01,                    //       Input (Cnst,Arr,Abs)                97
 * #   0x75, 0x08,                    //       Report Size (8)                     99
 * #   0x95, 0x01,                    //       Report Count (1)                    101
 * # ┇ 0x81, 0x01,                    //       Input (Cnst,Arr,Abs)                103
 * #   0x75, 0x08,                    //       Report Size (8)                     105
 * #   0x95, 0x01,                    //       Report Count (1)                    107
 * # ┇ 0x81, 0x01,                    //       Input (Cnst,Arr,Abs)                109
 * #   0xc0,                          //     End Collection                        111
 * #   0xc0,                          //   End Collection                          112
 * #   0xc0,                          // End Collection                            113
 * R: 114 05 01 09 0e a1 01 85 03 05 0d 75 08 95 01 81 01 09 21 a1 02 15 00 25 01 75 01 95 01 a1 00 05 09 09 01 81 02 05 0d 09 33 81 02 95 06 81 03 a1 02 05 01 09 37 16 00 80 26 ff 7f 75 10 95 01 81 06 35 00 46 10 0e 15 00 26 10 0e 09 48 b1 02 45 00 c0 75 08 95 01 81 01 75 08 95 01 81 01 75 08 95 01 81 01 75 08 95 01 81 01 75 08 95 01 81 01 c0 c0 c0
 * N: Keydial mini-050
 * I: 5 256c 8251
 *
 * The second hidraw node is what sends events:
 *
 * # Keydial mini-050
 * # Report descriptor length: 160 bytes
 * #   Bytes                          // Field Name                              Offset
 * # ----------------------------------------------------------------------------------
 * # 🮥 0x05, 0x01,                    // Usage Page (Generic Desktop)              0
 * # 🭬 0x09, 0x06,                    // Usage (Keyboard)                          2
 * #   0xa1, 0x01,                    // Collection (Application)                  4
 * # ┅ 0x85, 0x01,                    //   Report ID (1)                           6
 * # 🮥 0x05, 0x07,                    //   Usage Page (Keyboard/Keypad)            8
 * # 🭬 0x19, 0xe0,                    //   Usage Minimum (224)                     10
 * # 🭬 0x29, 0xe7,                    //   Usage Maximum (231)                     12
 * #   0x15, 0x00,                    //   Logical Minimum (0)                     14
 * #   0x25, 0x01,                    //   Logical Maximum (1)                     16
 * #   0x75, 0x01,                    //   Report Size (1)                         18
 * #   0x95, 0x08,                    //   Report Count (8)                        20
 * # ┇ 0x81, 0x02,                    //   Input (Data,Var,Abs)                    22
 * #   0x95, 0x01,                    //   Report Count (1)                        24
 * #   0x75, 0x08,                    //   Report Size (8)                         26
 * # ┇ 0x81, 0x01,                    //   Input (Cnst,Arr,Abs)                    28
 * #   0x95, 0x05,                    //   Report Count (5)                        30
 * #   0x75, 0x01,                    //   Report Size (1)                         32
 * # 🮥 0x05, 0x08,                    //   Usage Page (LED)                        34
 * # 🭬 0x19, 0x01,                    //   Usage Minimum (1)                       36
 * # 🭬 0x29, 0x05,                    //   Usage Maximum (5)                       38
 * # ┊ 0x91, 0x02,                    //   Output (Data,Var,Abs)                   40
 * #   0x95, 0x01,                    //   Report Count (1)                        42
 * #   0x75, 0x03,                    //   Report Size (3)                         44
 * # ┊ 0x91, 0x01,                    //   Output (Cnst,Arr,Abs)                   46
 * #   0x95, 0x06,                    //   Report Count (6)                        48
 * #   0x75, 0x08,                    //   Report Size (8)                         50
 * #   0x15, 0x00,                    //   Logical Minimum (0)                     52
 * #   0x25, 0xf1,                    //   Logical Maximum (241)                   54
 * # 🮥 0x05, 0x07,                    //   Usage Page (Keyboard/Keypad)            56
 * # 🭬 0x19, 0x00,                    //   Usage Minimum (0)                       58
 * # 🭬 0x29, 0xf1,                    //   Usage Maximum (241)                     60
 * # ┇ 0x81, 0x00,                    //   Input (Data,Arr,Abs)                    62
 * #   0xc0,                          // End Collection                            64
 * # 🮥 0x05, 0x0c,                    // Usage Page (Consumer)                     65
 * # 🭬 0x09, 0x01,                    // Usage (Consumer Control)                  67
 * #   0xa1, 0x01,                    // Collection (Application)                  69
 * # ┅ 0x85, 0x02,                    //   Report ID (2)                           71
 * # 🮥 0x05, 0x0c,                    //   Usage Page (Consumer)                   73
 * # 🭬 0x19, 0x00,                    //   Usage Minimum (0)                       75
 * # 🭬 0x2a, 0x80, 0x03,              //   Usage Maximum (896)                     77
 * #   0x15, 0x00,                    //   Logical Minimum (0)                     80
 * #   0x26, 0x80, 0x03,              //   Logical Maximum (896)                   82
 * #   0x75, 0x10,                    //   Report Size (16)                        85
 * #   0x95, 0x01,                    //   Report Count (1)                        87
 * # ┇ 0x81, 0x00,                    //   Input (Data,Arr,Abs)                    89
 * #   0xc0,                          // End Collection                            91
 * # 🮥 0x05, 0x01,                    // Usage Page (Generic Desktop)              92
 * # 🭬 0x09, 0x02,                    // Usage (Mouse)                             94
 * #   0xa1, 0x01,                    // Collection (Application)                  96
 * # 🭬 0x09, 0x01,                    //   Usage (Pointer)                         98
 * # ┅ 0x85, 0x05,                    //   Report ID (5)                           100
 * #   0xa1, 0x00,                    //   Collection (Physical)                   102
 * # 🮥 0x05, 0x09,                    //     Usage Page (Button)                   104
 * # 🭬 0x19, 0x01,                    //     Usage Minimum (1)                     106
 * # 🭬 0x29, 0x05,                    //     Usage Maximum (5)                     108
 * #   0x15, 0x00,                    //     Logical Minimum (0)                   110
 * #   0x25, 0x01,                    //     Logical Maximum (1)                   112
 * #   0x95, 0x05,                    //     Report Count (5)                      114
 * #   0x75, 0x01,                    //     Report Size (1)                       116
 * # ┇ 0x81, 0x02,                    //     Input (Data,Var,Abs)                  118
 * #   0x95, 0x01,                    //     Report Count (1)                      120
 * #   0x75, 0x03,                    //     Report Size (3)                       122
 * # ┇ 0x81, 0x01,                    //     Input (Cnst,Arr,Abs)                  124
 * # 🮥 0x05, 0x01,                    //     Usage Page (Generic Desktop)          126
 * # 🭬 0x09, 0x30,                    //     Usage (X)                             128
 * # 🭬 0x09, 0x31,                    //     Usage (Y)                             130
 * #   0x16, 0x01, 0x80,              //     Logical Minimum (32769)               132
 * #   0x26, 0xff, 0x7f,              //     Logical Maximum (32767)               135
 * #   0x75, 0x10,                    //     Report Size (16)                      138
 * #   0x95, 0x02,                    //     Report Count (2)                      140
 * # ┇ 0x81, 0x06,                    //     Input (Data,Var,Rel)                  142
 * # 🮥 0x05, 0x01,                    //     Usage Page (Generic Desktop)          144
 * # 🭬 0x09, 0x38,                    //     Usage (Wheel)                         146
 * #   0x15, 0x81,                    //     Logical Minimum (129)                 148
 * #   0x25, 0x7f,                    //     Logical Maximum (127)                 150
 * #   0x95, 0x01,                    //     Report Count (1)                      152
 * #   0x75, 0x08,                    //     Report Size (8)                       154
 * # ┇ 0x81, 0x06,                    //     Input (Data,Var,Rel)                  156
 * #   0xc0,                          //   End Collection                          158
 * #   0xc0,                          // End Collection                            159
 * R: 160 05 01 09 06 a1 01 85 01 05 07 19 e0 29 e7 15 00 25 01 75 01 95 08 81 02 95 01 75 08 81 01 95 05 75 01 05 08 19 01 29 05 91 02 95 01 75 03 91 01 95 06 75 08 15 00 25 f1 05 07 19 00 29 f1 81 00 c0 05 0c 09 01 a1 01 85 02 05 0c 19 00 2a 80 03 15 00 26 80 03 75 10 95 01 81 00 c0 05 01 09 02 a1 01 09 01 85 05 a1 00 05 09 19 01 29 05 15 00 25 01 95 05 75 01 81 02 95 01 75 03 81 01 05 01 09 30 09 31 16 01 80 26 ff 7f 75 10 95 02 81 06 05 01 09 38 15 81 25 7f 95 01 75 08 81 06 c0 c0
 * N: Keydial mini-050
 * I: 5 256c 8251
 * # Report descriptor:
 * # ------- Input Report -------
 * # ░ Report ID: 1
 * # ░  | Report size: 72 bits
 * # ░ Bit:    8       Usage: 0007/00e0: Keyboard/Keypad / Keyboard LeftControl      Logical Range:     0..=1
 * # ░ Bit:    9       Usage: 0007/00e1: Keyboard/Keypad / Keyboard LeftShift        Logical Range:     0..=1
 * # ░ Bit:   10       Usage: 0007/00e2: Keyboard/Keypad / Keyboard LeftAlt          Logical Range:     0..=1
 * # ░ Bit:   11       Usage: 0007/00e3: Keyboard/Keypad / Keyboard Left GUI         Logical Range:     0..=1
 * # ░ Bit:   12       Usage: 0007/00e4: Keyboard/Keypad / Keyboard RightControl     Logical Range:     0..=1
 * # ░ Bit:   13       Usage: 0007/00e5: Keyboard/Keypad / Keyboard RightShift       Logical Range:     0..=1
 * # ░ Bit:   14       Usage: 0007/00e6: Keyboard/Keypad / Keyboard RightAlt         Logical Range:     0..=1
 * # ░ Bit:   15       Usage: 0007/00e7: Keyboard/Keypad / Keyboard Right GUI        Logical Range:     0..=1
 * # ░ Bits:  16..=23  ######### Padding
 * # ░ Bits:  24..=71  Usages:                                                       Logical Range:     0..=241
 * # ░                 0007/0000: <unknown>
 * # ░                 0007/0001: Keyboard/Keypad / ErrorRollOver
 * # ░                 0007/0002: Keyboard/Keypad / POSTFail
 * # ░                 0007/0003: Keyboard/Keypad / ErrorUndefined
 * # ░                 0007/0004: Keyboard/Keypad / Keyboard A
 * # ░                 ... use --full to see all usages
 * # ------- Input Report -------
 * # ▒ Report ID: 2
 * # ▒  | Report size: 24 bits
 * # ▒ Bits:   8..=23  Usages:                                                Logical Range:     0..=896
 * # ▒                 000c/0000: <unknown>
 * # ▒                 000c/0001: Consumer / Consumer Control
 * # ▒                 000c/0002: Consumer / Numeric Key Pad
 * # ▒                 000c/0003: Consumer / Programmable Buttons
 * # ▒                 000c/0004: Consumer / Microphone
 * # ▒                 ... use --full to see all usages
 * # ------- Input Report -------
 * # ▞ Report ID: 5
 * # ▞  | Report size: 56 bits
 * # ▞ Bit:    8       Usage: 0009/0001: Button / Button 1                           Logical Range:     0..=1
 * # ▞ Bit:    9       Usage: 0009/0002: Button / Button 2                           Logical Range:     0..=1
 * # ▞ Bit:   10       Usage: 0009/0003: Button / Button 3                           Logical Range:     0..=1
 * # ▞ Bit:   11       Usage: 0009/0004: Button / Button 4                           Logical Range:     0..=1
 * # ▞ Bit:   12       Usage: 0009/0005: Button / Button 5                           Logical Range:     0..=1
 * # ▞ Bits:  13..=15  ######### Padding
 * # ▞ Bits:  16..=31  Usage: 0001/0030: Generic Desktop / X                         Logical Range: 32769..=32767
 * # ▞ Bits:  32..=47  Usage: 0001/0031: Generic Desktop / Y                         Logical Range: 32769..=32767
 * # ▞ Bits:  48..=55  Usage: 0001/0038: Generic Desktop / Wheel                     Logical Range:   129..=127
 * # ------- Output Report -------
 * # ░ Report ID: 1
 * # ░  | Report size: 16 bits
 * # ░ Bit:    8       Usage: 0008/0001: LED / Num Lock                              Logical Range:     0..=1
 * # ░ Bit:    9       Usage: 0008/0002: LED / Caps Lock                             Logical Range:     0..=1
 * # ░ Bit:   10       Usage: 0008/0003: LED / Scroll Lock                           Logical Range:     0..=1
 * # ░ Bit:   11       Usage: 0008/0004: LED / Compose                               Logical Range:     0..=1
 * # ░ Bit:   12       Usage: 0008/0005: LED / Kana                                  Logical Range:     0..=1
 * # ░ Bits:  13..=15  ######### Padding
 * ##############################################################################
 * # Event nodes:
 * # - /dev/input/event12: "Keydial mini-050 Keyboard"
 * # - /dev/input/event14: "Keydial mini-050 Mouse"
 * ##############################################################################
 * # Recorded events below in format:
 * # E: <seconds>.<microseconds> <length-in-bytes> [bytes ...]
 * #
 *
 * - Report ID 1 sends keyboard shortcuts when pressing the buttons, e.g.
 *
 * # ░  Report ID: 1 /
 * # ░               Keyboard LeftControl:     0 |Keyboard LeftShift:     0 |Keyboard LeftAlt:     0 |Keyboard Left GUI:     0 |Keyboard RightControl:     0 |Keyboard RightShift:     0 |Keyboard RightAlt:     0 |Keyboard Right GUI:     0 |<8 bits padding> |0007/0000:     0| Keyboard K:    14| 0007/0000:     0| 0007/0000:     0| 0007/0000:     0| 0007/0000:     0
 * E: 000000.000292 9 01 00 00 00 0e 00 00 00 00
 *
 * - Report ID 2 sends the button inside the wheel/dial thing
 * # ▒  Report ID: 2 /
 * # ▒               Play/Pause:   205
 * E: 000134.347845 3 02 cd 00
 * # ▒  Report ID: 2 /
 * # ▒               000c/0000:     0
 * E: 000134.444965 3 02 00 00
 *
 * - Report ID 5 sends the wheel relative events (always a double-event with the second as zero)
 * # ▞  Report ID: 5 /
 * # ▞               Button 1:     0 |Button 2:     0 |Button 3:     0 |Button 4:     0 |Button 5:     0 |<3 bits padding> |X:     0 |Y:     0 |Wheel:   255
 * E: 000064.859915 7 05 00 00 00 00 00 ff
 * # ▞  Report ID: 5 /
 * # ▞               Button 1:     0 |Button 2:     0 |Button 3:     0 |Button 4:     0 |Button 5:     0 |<3 bits padding> |X:     0 |Y:     0 |Wheel:     0
 * E: 000064.882009 7 05 00 00 00 00 00 00
 */

#define BT_PAD_REPORT_DESCRIPTOR_LENGTH 160
#define BT_PUCK_REPORT_DESCRIPTOR_LENGTH 114  // This one doesn't send events
#define BT_PAD_KBD_REPORT_ID 1
#define BT_PAD_CC_REPORT_ID 2
#define BT_PAD_MOUSE_REPORT_ID 5
#define BT_PAD_KBD_REPORT_LENGTH 9
#define BT_PAD_CC_REPORT_LENGTH 3
#define BT_PAD_MOUSE_REPORT_LENGTH 7
#define OUR_REPORT_ID 11 /* "randomly" picked report ID for our reports */

__u32 last_button_state = 0;

static const __u8 disabled_rdesc_puck[] = {
	FixedSizeVendorReport(BT_PUCK_REPORT_DESCRIPTOR_LENGTH)
};

static const __u8 fixed_rdesc_pad[] = {
	UsagePage_GenericDesktop
	Usage_GD_Keypad
	CollectionApplication(
		// Byte 0
		ReportId(OUR_REPORT_ID)
		UsagePage_Digitizers
		Usage_Dig_TabletFunctionKeys
		CollectionPhysical(
			// Byte 1 is a button so we look like a tablet
			Usage_Dig_BarrelSwitch	 // BTN_STYLUS, needed so we get to be a tablet pad
			ReportCount(1)
			ReportSize(1)
			Input(Var|Abs)
			ReportCount(7) // Padding
			Input(Const)
			// Bytes 2/3 - x/y just exist so we get to be a tablet pad
			UsagePage_GenericDesktop
			Usage_GD_X
			Usage_GD_Y
			LogicalMinimum_i8(0x0)
			LogicalMaximum_i8(0x1)
			ReportCount(2)
			ReportSize(8)
			Input(Var|Abs)
			// Bytes 4-7 are the button state for 19 buttons + pad out to u32
			// We send the first 10 buttons as buttons 1-10 which is BTN_0 -> BTN_9
			UsagePage_Button
			UsageMinimum_i8(1)
			UsageMaximum_i8(10)
			LogicalMinimum_i8(0x0)
			LogicalMaximum_i8(0x1)
			ReportCount(10)
			ReportSize(1)
			Input(Var|Abs)
			// We send the other 9 buttons as buttons 0x31 and above -> BTN_A - BTN_TL2
			UsageMinimum_i8(0x31)
			UsageMaximum_i8(0x3a)
			ReportCount(9)
			ReportSize(1)
			Input(Var|Abs)
			ReportCount(13)
			ReportSize(1)
			Input(Const) // padding
			// Byte 8 is the wheel
			UsagePage_GenericDesktop
			Usage_GD_Wheel
			LogicalMinimum_i8(-1)
			LogicalMaximum_i8(1)
			ReportCount(1)
			ReportSize(8)
			Input(Var|Rel)
		)
		// Make sure we match our original report length
		FixedSizeVendorReport(BT_PAD_KBD_REPORT_LENGTH)
	)
};

SEC(HID_BPF_RDESC_FIXUP)
int BPF_PROG(k20_bt_fix_rdesc, struct hid_bpf_ctx *hctx)
{
	__u8 *data = hid_bpf_get_data(hctx, 0 /* offset */, HID_MAX_DESCRIPTOR_SIZE /* size */);
	__s32 rdesc_size = hctx->size;

	if (!data)
		return 0; /* EPERM check */

	if (rdesc_size == BT_PAD_REPORT_DESCRIPTOR_LENGTH) {
		__builtin_memcpy(data, fixed_rdesc_pad, sizeof(fixed_rdesc_pad));
		return sizeof(fixed_rdesc_pad);
	}
	if (rdesc_size == BT_PUCK_REPORT_DESCRIPTOR_LENGTH) {
		// This hidraw node doesn't send anything and can be ignored
		__builtin_memcpy(data, disabled_rdesc_puck, sizeof(disabled_rdesc_puck));
		return sizeof(disabled_rdesc_puck);
	}

	return 0;
}

SEC(HID_BPF_DEVICE_EVENT)
int BPF_PROG(k20_bt_fix_events, struct hid_bpf_ctx *hctx)
{
	__u8 *data = hid_bpf_get_data(hctx, 0 /* offset */, 12 /* size */);
	struct pad_report {
		__u8 report_id;
		__u8 btn_stylus:1;
		__u8 pad:7;
		__u8 x;
		__u8 y;
		__u32 buttons;
		__u8 wheel;
	} __packed * pad_report = (struct pad_report *)data;

	if (!data)
		return 0; /* EPERM check */

	/* Report ID 1 - Keyboard events (button presses) */
	if (data[0] == BT_PAD_KBD_REPORT_ID) {
		const __u8 button_mapping[] = {
			0x0e, /* Button 1:  K */
			0x0a, /* Button 2:  G */
			0x0f, /* Button 3:  L */
			0x4c, /* Button 4:  Delete */
			0x0c, /* Button 5:  I */
			0x07, /* Button 6:  D */
			0x05, /* Button 7:  B */
			0x08, /* Button 8:  E */
			0x16, /* Button 9:  S */
			0x1d, /* Button 10: Z */
			0x06, /* Button 11: C */
			0x19, /* Button 12: V */
			0xff, /* Button 13: LeftControl */
			0xff, /* Button 14: LeftAlt */
			0xff, /* Button 15: LeftShift */
			0x28, /* Button 16: Return Enter */
			0x2c, /* Button 17: Spacebar */
			0x11, /* Button 18: N */
		};

		__u8 modifiers = data[1];
		__u32 buttons = 0;

		if (modifiers & 0x01) { /* Control */
			buttons |= BIT(12);
		}
		if (modifiers & 0x02) { /* Shift */
			buttons |= BIT(14);
		}
		if (modifiers & 0x04) { /* Alt */
			buttons |= BIT(13);
		}

		for (int i = 4; i < BT_PAD_KBD_REPORT_LENGTH; i++) {
			if (!data[i])
				break;

			for (size_t b = 0; b < ARRAY_SIZE(button_mapping); b++) {
				if (data[i] != 0xff && data[i] == button_mapping[b]) {
					buttons |= BIT(b);
					break;
				}
			}
		}

		last_button_state = buttons;

		pad_report->report_id = OUR_REPORT_ID;
		pad_report->btn_stylus = 0;
		pad_report->x = 0;
		pad_report->y = 0;
		pad_report->buttons = buttons;
		pad_report->wheel = 0;

		return sizeof(struct pad_report);
	}

	/* Report ID 2 - Consumer control events (the button inside the wheel) */
	if (data[0] == BT_PAD_CC_REPORT_ID) {
		const __u8 PlayPause = 0xcd;

		if (data[1] == PlayPause)
			last_button_state |= BIT(18);
		else
			last_button_state &= ~BIT(18);

		pad_report->report_id = OUR_REPORT_ID;
		pad_report->btn_stylus = 0;
		pad_report->x = 0;
		pad_report->y = 0;
		pad_report->buttons = last_button_state;
		pad_report->wheel = 0;

		return sizeof(struct pad_report);
	}

	/* Report ID 5 - Mouse events (wheel rotation) */
	if (data[0] == BT_PAD_MOUSE_REPORT_ID) {
		__u8 wheel_delta = data[6];

		pad_report->report_id = OUR_REPORT_ID;
		pad_report->btn_stylus = 0;
		pad_report->x = 0;
		pad_report->y = 0;
		pad_report->buttons = last_button_state;
		pad_report->wheel = wheel_delta;

		return sizeof(struct pad_report);
	}

	return 0;
}

HID_BPF_OPS(keydial_k20_bluetooth) = {
	.hid_device_event = (void *)k20_bt_fix_events,
	.hid_rdesc_fixup = (void *)k20_bt_fix_rdesc,
};

SEC("syscall")
int probe(struct hid_bpf_probe_args *ctx)
{
	switch (ctx->rdesc_size) {
	case BT_PAD_REPORT_DESCRIPTOR_LENGTH:
	case BT_PUCK_REPORT_DESCRIPTOR_LENGTH:
		ctx->retval = 0;
		break;
	default:
		ctx->retval = -EINVAL;
	}

	return 0;
}

char _license[] SEC("license") = "GPL";
