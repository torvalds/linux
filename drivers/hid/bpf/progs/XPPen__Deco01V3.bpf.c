// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2025 Red Hat
 */

#include "vmlinux.h"
#include "hid_bpf.h"
#include "hid_bpf_helpers.h"
#include "hid_report_helpers.h"
#include <bpf/bpf_tracing.h>

#define VID_UGEE 0x28BD /* VID is shared with SinoWealth and Glorious and prob others */
#define PID_DECO_01_V3 0x0947

HID_BPF_CONFIG(
	HID_DEVICE(BUS_USB, HID_GROUP_GENERIC, VID_UGEE, PID_DECO_01_V3),
);

/*
 * Default report descriptor reports:
 * - a report descriptor for the pad buttons, reported as key sequences
 * - a report descriptor for the pen
 * - a vendor-specific report descriptor
 *
 * The Pad report descriptor, see
 * https://gitlab.freedesktop.org/libevdev/udev-hid-bpf/-/issues/54
 *
 * # Report descriptor length: 102 bytes
 * 0x05, 0x01,                    // Usage Page (Generic Desktop)              0
 * 0x09, 0x02,                    // Usage (Mouse)                             2
 * 0xa1, 0x01,                    // Collection (Application)                  4
 * 0x85, 0x09,                    //   Report ID (9)                           6
 * 0x09, 0x01,                    //   Usage (Pointer)                         8
 * 0xa1, 0x00,                    //   Collection (Physical)                   10
 * 0x05, 0x09,                    //     Usage Page (Button)                   12
 * 0x19, 0x01,                    //     UsageMinimum (1)                      14
 * 0x29, 0x03,                    //     UsageMaximum (3)                      16
 * 0x15, 0x00,                    //     Logical Minimum (0)                   18
 * 0x25, 0x01,                    //     Logical Maximum (1)                   20
 * 0x95, 0x03,                    //     Report Count (3)                      22
 * 0x75, 0x01,                    //     Report Size (1)                       24
 * 0x81, 0x02,                    //     Input (Data,Var,Abs)                  26
 * 0x95, 0x05,                    //     Report Count (5)                      28
 * 0x81, 0x01,                    //     Input (Cnst,Arr,Abs)                  30
 * 0x05, 0x01,                    //     Usage Page (Generic Desktop)          32
 * 0x09, 0x30,                    //     Usage (X)                             34
 * 0x09, 0x31,                    //     Usage (Y)                             36
 * 0x26, 0xff, 0x7f,              //     Logical Maximum (32767)               38
 * 0x95, 0x02,                    //     Report Count (2)                      41
 * 0x75, 0x10,                    //     Report Size (16)                      43
 * 0x81, 0x02,                    //     Input (Data,Var,Abs)                  45
 * 0x05, 0x0d,                    //     Usage Page (Digitizers)               47
 * 0x09, 0x30,                    //     Usage (Tip Pressure)                  49
 * 0x26, 0xff, 0x07,              //     Logical Maximum (2047)                51
 * 0x95, 0x01,                    //     Report Count (1)                      54
 * 0x75, 0x10,                    //     Report Size (16)                      56
 * 0x81, 0x02,                    //     Input (Data,Var,Abs)                  58
 * 0xc0,                          //   End Collection                          60
 * 0xc0,                          // End Collection                            61
 * 0x05, 0x01,                    // Usage Page (Generic Desktop)              62
 * 0x09, 0x06,                    // Usage (Keyboard)                          64
 * 0xa1, 0x01,                    // Collection (Application)                  66
 * 0x85, 0x06,                    //   Report ID (6)                           68
 * 0x05, 0x07,                    //   Usage Page (Keyboard/Keypad)            70
 * 0x19, 0xe0,                    //   UsageMinimum (224)                      72
 * 0x29, 0xe7,                    //   UsageMaximum (231)                      74
 * 0x15, 0x00,                    //   Logical Minimum (0)                     76
 * 0x25, 0x01,                    //   Logical Maximum (1)                     78
 * 0x75, 0x01,                    //   Report Size (1)                         80
 * 0x95, 0x08,                    //   Report Count (8)                        82
 * 0x81, 0x02,                    //   Input (Data,Var,Abs)                    84
 * 0x05, 0x07,                    //   Usage Page (Keyboard/Keypad)            86
 * 0x19, 0x00,                    //   UsageMinimum (0)                        88
 * 0x29, 0xff,                    //   UsageMaximum (255)                      90
 * 0x26, 0xff, 0x00,              //   Logical Maximum (255)                   92
 * 0x75, 0x08,                    //   Report Size (8)                         95
 * 0x95, 0x06,                    //   Report Count (6)                        97
 * 0x81, 0x00,                    //   Input (Data,Arr,Abs)                    99
 * 0xc0,                          // End Collection                            101
 *
 * And key events for buttons top->bottom are:
 * Buttons released: 06 00 00 00 00 00 00 00
 * Button1:          06 00 05 00 00 00 00 00 -> b
 * Button2:          06 00 08 00 00 00 00 00 -> e
 * Button3:          06 04 00 00 00 00 00 00 -> LAlt
 * Button4:          06 00 2c 00 00 00 00 00 -> Space
 * Button5:          06 01 16 00 00 00 00 00 -> LControl + s
 * Button6:          06 01 1d 00 00 00 00 00 -> LControl + z
 * Button7:          06 01 57 00 00 00 00 00 -> LControl + Keypad Plus
 * Button8:          06 01 56 00 00 00 00 00 -> LControl + Keypad Dash
 *
 * When multiple buttons are pressed at the same time, the values used to
 * identify the buttons are identical, but they appear in different bytes of the
 * record. For example, when button 2 (0x08) and button 1 (0x05) are pressed,
 * this is the report:
 *
 *   Buttons 2 and 1:  06 00 08 05 00 00 00 00 -> e + b
 *
 * Buttons 1, 2, 4, 5 and 6 can be matched by finding their values in the
 * report.
 *
 * Button 3 is pressed when the 3rd bit is 1. For example, pressing buttons 3
 * and 5 generates this report:
 *
 *   Buttons 3 and 5:  06 05 16 00 00 00 00 00 -> LControl + LAlt + s
 *                        -- --
 *                         |  |
 *                         |  `- Button 5 (0x16)
 *                         `- 0x05 = 0101. Button 3 is pressed
 *                                    ^
 *
 * pad_buttons contains a list of buttons that can be matched in
 * HID_BPF_DEVICE_EVENT. Button 3 as it has a dedicated bit.
 *
 *
 * The Pen report descriptor announces a wrong tilt range:
 *
 * Report descriptor length: 109 bytes
 * 0x05, 0x0d,                    // Usage Page (Digitizers)                   0
 * 0x09, 0x02,                    // Usage (Pen)                               2
 * 0xa1, 0x01,                    // Collection (Application)                  4
 * 0x85, 0x07,                    //   Report ID (7)                           6
 * 0x09, 0x20,                    //   Usage (Stylus)                          8
 * 0xa1, 0x01,                    //   Collection (Application)                10
 * 0x09, 0x42,                    //     Usage (Tip Switch)                    12
 * 0x09, 0x44,                    //     Usage (Barrel Switch)                 14
 * 0x09, 0x45,                    //     Usage (Eraser)                        16
 * 0x09, 0x3c,                    //     Usage (Invert)                        18
 * 0x15, 0x00,                    //     Logical Minimum (0)                   20
 * 0x25, 0x01,                    //     Logical Maximum (1)                   22
 * 0x75, 0x01,                    //     Report Size (1)                       24
 * 0x95, 0x04,                    //     Report Count (4)                      26
 * 0x81, 0x02,                    //     Input (Data,Var,Abs)                  28
 * 0x95, 0x01,                    //     Report Count (1)                      30
 * 0x81, 0x03,                    //     Input (Cnst,Var,Abs)                  32
 * 0x09, 0x32,                    //     Usage (In Range)                      34
 * 0x95, 0x01,                    //     Report Count (1)                      36
 * 0x81, 0x02,                    //     Input (Data,Var,Abs)                  38
 * 0x95, 0x02,                    //     Report Count (2)                      40
 * 0x81, 0x03,                    //     Input (Cnst,Var,Abs)                  42
 * 0x75, 0x10,                    //     Report Size (16)                      44
 * 0x95, 0x01,                    //     Report Count (1)                      46
 * 0x35, 0x00,                    //     Physical Minimum (0)                  48
 * 0xa4,                          //     Push                                  50
 * 0x05, 0x01,                    //     Usage Page (Generic Desktop)          51
 * 0x09, 0x30,                    //     Usage (X)                             53
 * 0x65, 0x13,                    //     Unit (EnglishLinear: in)              55
 * 0x55, 0x0d,                    //     Unit Exponent (-3)                    57
 * 0x46, 0x10, 0x27,              //     Physical Maximum (10000)              59
 * 0x26, 0xff, 0x7f,              //     Logical Maximum (32767)               62
 * 0x81, 0x02,                    //     Input (Data,Var,Abs)                  65
 * 0x09, 0x31,                    //     Usage (Y)                             67
 * 0x46, 0x6a, 0x18,              //     Physical Maximum (6250)               69
 * 0x26, 0xff, 0x7f,              //     Logical Maximum (32767)               72
 * 0x81, 0x02,                    //     Input (Data,Var,Abs)                  75
 * 0xb4,                          //     Pop                                   77
 * 0x09, 0x30,                    //     Usage (X)                             78
 * 0x45, 0x00,                    //     Physical Maximum (0)                  80
 * 0x26, 0xff, 0x3f,              //     Logical Maximum (16383)               82
 * 0x81, 0x42,                    //     Input (Data,Var,Abs,Null)             85
 * 0x09, 0x3d,                    //     Usage (Start)                         87
 * 0x15, 0x81,                    //     Logical Minimum (-127)                89  <- Change from -127 to -60
 * 0x25, 0x7f,                    //     Logical Maximum (127)                 91  <- Change from 127 to 60
 * 0x75, 0x08,                    //     Report Size (8)                       93
 * 0x95, 0x01,                    //     Report Count (1)                      95
 * 0x81, 0x02,                    //     Input (Data,Var,Abs)                  97
 * 0x09, 0x3e,                    //     Usage (Select)                        99
 * 0x15, 0x81,                    //     Logical Minimum (-127)                101  <- Change from -127 to -60
 * 0x25, 0x7f,                    //     Logical Maximum (127)                 103  <- Change from 127 to 60
 * 0x81, 0x02,                    //     Input (Data,Var,Abs)                  105
 * 0xc0,                          //   End Collection                          107
 * 0xc0,                          // End Collection                            108
 */

#define PEN_REPORT_DESCRIPTOR_LENGTH 109
#define PAD_REPORT_DESCRIPTOR_LENGTH 102
#define PAD_REPORT_LENGTH 8
#define PAD_REPORT_ID 6
#define PAD_NUM_BUTTONS 8

static const __u8 fixed_rdesc_pad[] = {
	UsagePage_GenericDesktop
	Usage_GD_Keypad
	CollectionApplication(
		// Byte 0 in report is the report ID
		ReportId(PAD_REPORT_ID)
		ReportCount(1)
		ReportSize(8)
		UsagePage_Digitizers
		Usage_Dig_TabletFunctionKeys
		CollectionPhysical(
			// Byte 1 is the button state
			UsagePage_Button
			UsageMinimum_i8(0x01)
			UsageMaximum_i8(PAD_NUM_BUTTONS)
			LogicalMinimum_i8(0x0)
			LogicalMaximum_i8(0x1)
			ReportCount(PAD_NUM_BUTTONS)
			ReportSize(1)
			Input(Var|Abs)
			// Byte 2 in report - just exists so we get to be a tablet pad
			UsagePage_Digitizers
			Usage_Dig_BarrelSwitch // BTN_STYLUS
			ReportCount(1)
			ReportSize(1)
			Input(Var|Abs)
			ReportCount(7) // padding
			Input(Const)
			// Bytes 3/4 in report - just exists so we get to be a tablet pad
			UsagePage_GenericDesktop
			Usage_GD_X
			Usage_GD_Y
			ReportCount(2)
			ReportSize(8)
			Input(Var|Abs)
			// Byte 5-7 are padding so we match the original report lengtth
			ReportCount(3)
			ReportSize(8)
			Input(Const)
		)
	)
};

SEC(HID_BPF_RDESC_FIXUP)
int BPF_PROG(xppen_deco01v3_rdesc_fixup, struct hid_bpf_ctx *hctx)
{
	__u8 *data = hid_bpf_get_data(hctx, 0 /* offset */, HID_MAX_DESCRIPTOR_SIZE /* size */);

	const __u8 wrong_logical_range[] = {0x15, 0x81, 0x25, 0x7f};
	const __u8 correct_logical_range[] = {0x15, 0xc4, 0x25, 0x3c};

	if (!data)
		return 0; /* EPERM check */

	switch (hctx->size) {
	case PAD_REPORT_DESCRIPTOR_LENGTH:
		__builtin_memcpy(data, fixed_rdesc_pad, sizeof(fixed_rdesc_pad));
		return sizeof(fixed_rdesc_pad);
	case PEN_REPORT_DESCRIPTOR_LENGTH:
		if (__builtin_memcmp(&data[89], wrong_logical_range,
				     sizeof(wrong_logical_range)) == 0)
			__builtin_memcpy(&data[89], correct_logical_range,
					 sizeof(correct_logical_range));
		if (__builtin_memcmp(&data[101], wrong_logical_range,
				     sizeof(wrong_logical_range)) == 0)
			__builtin_memcpy(&data[101], correct_logical_range,
					 sizeof(correct_logical_range));
		break;
	}

	return 0;
}

SEC(HID_BPF_DEVICE_EVENT)
int BPF_PROG(xppen_deco01v3_device_event, struct hid_bpf_ctx *hctx)
{
	static const __u8 pad_buttons[] = { 0x05, 0x08, 0x00, 0x2c, 0x16, 0x1d, 0x57, 0x56 };
	__u8 *data = hid_bpf_get_data(hctx, 0 /* offset */, PAD_REPORT_LENGTH /* size */);

	if (!data)
		return 0; /* EPERM check */

	if (data[0] == PAD_REPORT_ID) {
		__u8 button_mask = 0;
		size_t d, b;

		/* data[1] stores the status of BTN_2 in the 3rd bit*/
		if (data[1] & BIT(2))
			button_mask |= BIT(2);

		/* The rest of the descriptor stores the buttons as in pad_buttons */
		for (d = 2; d < 8; d++) {
			for (b = 0; b < sizeof(pad_buttons); b++) {
				if (data[d] != 0 && data[d] == pad_buttons[b])
					button_mask |= BIT(b);
			}
		}

		__u8 report[8] = {PAD_REPORT_ID, button_mask, 0x00};

		__builtin_memcpy(data, report, sizeof(report));
	}
	return 0;
}

HID_BPF_OPS(xppen_deco01v3) = {
	.hid_rdesc_fixup = (void *)xppen_deco01v3_rdesc_fixup,
	.hid_device_event = (void *)xppen_deco01v3_device_event,
};

SEC("syscall")
int probe(struct hid_bpf_probe_args *ctx)
{
	switch (ctx->rdesc_size) {
	case PAD_REPORT_DESCRIPTOR_LENGTH:
	case PEN_REPORT_DESCRIPTOR_LENGTH:
		ctx->retval = 0;
		break;
	default:
		ctx->retval = -EINVAL;
	}

	return 0;
}

char _license[] SEC("license") = "GPL";
