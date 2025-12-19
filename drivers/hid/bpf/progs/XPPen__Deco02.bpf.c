// SPDX-License-Identifier: GPL-2.0-only

#include "vmlinux.h"
#include "hid_bpf.h"
#include "hid_bpf_helpers.h"
#include "hid_report_helpers.h"
#include <bpf/bpf_tracing.h>

#define VID_UGEE 0x28BD
#define PID_DECO_02 0x0803

HID_BPF_CONFIG(
	HID_DEVICE(BUS_USB, HID_GROUP_GENERIC, VID_UGEE, PID_DECO_02),
);

/*
 * Devices are:
 * - Pad input, including pen (This is the only one we are interested in)
 * - Pen input as mouse
 * - Vendor
 *
 * Descriptors on main device are:
 * - 7: Pen
 * - 6: Vendor settings? Unclear
 * - 3: Keyboard (This is what we want to modify)
 * - 5: Feature report
 *
 * This creates three event nodes:
 * - XP-PEN DECO 02 Stylus
 * - XP-PEN DECO 02
 * - XP-PEN DECO 02 Keyboard (Again, what we want to modify)
 *
 * # Report descriptor length: 188 bytes
 * # 0x05, 0x0d,                    // Usage Page (Digitizers)             0
 * # 0x09, 0x02,                    // Usage (Pen)                         2
 * # 0xa1, 0x01,                    // Collection (Application)            4
 * # 0x85, 0x07,                    //  Report ID (7)                      6
 * # 0x09, 0x20,                    //  Usage (Stylus)                     8
 * # 0xa1, 0x00,                    //  Collection (Physical)              10
 * # 0x09, 0x42,                    //   Usage (Tip Switch)                12
 * # 0x09, 0x44,                    //   Usage (Barrel Switch)             14
 * # 0x09, 0x45,                    //   Usage (Eraser)                    16
 * # 0x09, 0x3c,                    //   Usage (Invert)                    18
 * # 0x09, 0x32,                    //   Usage (In Range)                  20
 * # 0x15, 0x00,                    //   Logical Minimum (0)               22
 * # 0x25, 0x01,                    //   Logical Maximum (1)               24
 * # 0x75, 0x01,                    //   Report Size (1)                   26
 * # 0x95, 0x05,                    //   Report Count (5)                  28
 * # 0x81, 0x02,                    //   Input (Data,Var,Abs)              30
 * # 0x95, 0x03,                    //   Report Count (3)                  32
 * # 0x81, 0x03,                    //   Input (Cnst,Var,Abs)              34
 * # 0x05, 0x01,                    //   Usage Page (Generic Desktop)      36
 * # 0x09, 0x30,                    //   Usage (X)                         38
 * # 0x15, 0x00,                    //   Logical Minimum (0)               40
 * # 0x26, 0x50, 0x57,              //   Logical Maximum (22352)           42
 * # 0x55, 0x0d,                    //   Unit Exponent (-3)                45
 * # 0x65, 0x13,                    //   Unit (EnglishLinear: in)          47
 * # 0x35, 0x00,                    //   Physical Minimum (0)              49
 * # 0x46, 0x50, 0x57,              //   Physical Maximum (22352)          51
 * # 0x75, 0x10,                    //   Report Size (16)                  54
 * # 0x95, 0x01,                    //   Report Count (1)                  56
 * # 0x81, 0x02,                    //   Input (Data,Var,Abs)              58
 * # 0x09, 0x31,                    //   Usage (Y)                         60
 * # 0x15, 0x00,                    //   Logical Minimum (0)               62
 * # 0x26, 0x92, 0x36,              //   Logical Maximum (13970)           64
 * # 0x55, 0x0d,                    //   Unit Exponent (-3)                67
 * # 0x65, 0x13,                    //   Unit (EnglishLinear: in)          69
 * # 0x35, 0x00,                    //   Physical Minimum (0)              71
 * # 0x46, 0x92, 0x36,              //   Physical Maximum (13970)          73
 * # 0x75, 0x10,                    //   Report Size (16)                  76
 * # 0x95, 0x01,                    //   Report Count (1)                  78
 * # 0x81, 0x02,                    //   Input (Data,Var,Abs)              80
 * # 0x05, 0x0d,                    //   Usage Page (Digitizers)           82
 * # 0x09, 0x30,                    //   Usage (Tip Pressure)              84
 * # 0x15, 0x00,                    //   Logical Minimum (0)               86
 * # 0x26, 0xff, 0x1f,              //   Logical Maximum (8191)            88
 * # 0x75, 0x10,                    //   Report Size (16)                  91
 * # 0x95, 0x01,                    //   Report Count (1)                  93
 * # 0x81, 0x02,                    //   Input (Data,Var,Abs)              95
 * # 0xc0,                          //  End Collection                     97
 * # 0xc0,                          // End Collection                      98
 * # 0x09, 0x0e,                    // Usage (Device Configuration)        99
 * # 0xa1, 0x01,                    // Collection (Application)            101
 * # 0x85, 0x05,                    //  Report ID (5)                      103
 * # 0x09, 0x23,                    //  Usage (Device Settings)            105
 * # 0xa1, 0x02,                    //  Collection (Logical)               107
 * # 0x09, 0x52,                    //   Usage (Inputmode)                 109
 * # 0x09, 0x53,                    //   Usage (Device Index)              111
 * # 0x25, 0x0a,                    //   Logical Maximum (10)              113
 * # 0x75, 0x08,                    //   Report Size (8)                   115
 * # 0x95, 0x02,                    //   Report Count (2)                  117
 * # 0xb1, 0x02,                    //   Feature (Data,Var,Abs)            119
 * # 0xc0,                          //  End Collection                     121
 * # 0xc0,                          // End Collection                      122
 * # 0x05, 0x0c,                    // Usage Page (Consumer Devices)       123
 * # 0x09, 0x36,                    // Usage (Function Buttons)            125
 * # 0xa1, 0x00,                    // Collection (Physical)               127
 * # 0x85, 0x06,                    //  Report ID (6)                      129
 * # 0x05, 0x09,                    //  Usage Page (Button)                131
 * # 0x19, 0x01,                    //  Usage Minimum (1)                  133
 * # 0x29, 0x20,                    //  Usage Maximum (32)                 135
 * # 0x15, 0x00,                    //  Logical Minimum (0)                137
 * # 0x25, 0x01,                    //  Logical Maximum (1)                139
 * # 0x95, 0x20,                    //  Report Count (32)                  141
 * # 0x75, 0x01,                    //  Report Size (1)                    143
 * # 0x81, 0x02,                    //  Input (Data,Var,Abs)               145
 * # 0xc0,                          // End Collection                      147
 * # 0x05, 0x01,                    // Usage Page (Generic Desktop)        148
 * # 0x09, 0x06,                    // Usage (Keyboard)                    150
 * # 0xa1, 0x01,                    // Collection (Application)            152
 * # 0x85, 0x03,                    //  Report ID (3)                      154
 * # 0x05, 0x07,                    //  Usage Page (Keyboard)              156
 * # 0x19, 0xe0,                    //  Usage Minimum (224)                158
 * # 0x29, 0xe7,                    //  Usage Maximum (231)                160
 * # 0x15, 0x00,                    //  Logical Minimum (0)                162
 * # 0x25, 0x01,                    //  Logical Maximum (1)                164
 * # 0x75, 0x01,                    //  Report Size (1)                    166
 * # 0x95, 0x08,                    //  Report Count (8)                   168
 * # 0x81, 0x02,                    //  Input (Data,Var,Abs)               170
 * # 0x05, 0x07,                    //  Usage Page (Keyboard)              172
 * # 0x19, 0x00,                    //  Usage Minimum (0)                  174
 * # 0x29, 0xff,                    //  Usage Maximum (255)                176
 * # 0x26, 0xff, 0x00,              //  Logical Maximum (255)              178
 * # 0x75, 0x08,                    //  Report Size (8)                    181
 * # 0x95, 0x06,                    //  Report Count (6)                   183
 * # 0x81, 0x00,                    //  Input (Data,Arr,Abs)               185
 * # 0xc0,                          // End Collection                      187
 *
 * Key events; top to bottom:
 * Buttons released: 03 00 00 00 00 00 00 00
 * Button1:          03 00 05 00 00 00 00 00 -> 'b and B'
 * Button2:          03 00 2c 00 00 00 00 00 -> 'Spacebar'
 * Button3:          03 00 08 00 00 00 00 00 -> 'e and E'
 * Button4:          03 00 0c 00 00 00 00 00 -> 'i and I'
 * Button5:          03 05 1d 00 00 00 00 00 -> LeftControl + LeftAlt + 'z and Z'
 * Button6:          03 01 16 00 00 00 00 00 -> LeftControl + 's and S'
 *
 * Dial Events:
 * Clockwise:	     03 01 2e 00 00 00 00 00 -> LeftControl + '= and +'
 * Anticlockwise:    03 01 2d 00 00 00 00 00 -> LeftControl + '- and (underscore)'
 *
 * NOTE: Input event descriptions begin at byte 2, and progressively build
 * towards byte 7 as each new key is pressed maintaining the press order.
 * For example:
 *	BTN1 followed by BTN2 is 03 00 05 2c 00 00 00 00
 *	BTN2 followed by BTN1 is 03 00 2c 05 00 00 00 00
 *
 * Releasing a button causes its byte to be freed, and the next item in the list
 * is pushed forwards. Dial events are released immediately after an event is
 * registered (i.e. after each "click"), so will continually appear pushed
 * backwards in the report.
 *
 * When a button with a modifier key is pressed, the button identifier stacks in
 * an abnormal way, where the highest modifier byte always supersedes others.
 * In these cases, the button with the higher modifier is always last.
 * For example:
 *	BTN6 followed by BTN5 is 03 05 1d 16 00 00 00 00
 *	BTN5 followed by BTN6 is 03 05 1d 16 00 00 00 00
 *	BTN5 followed by BTN1 is 03 05 05 1d 00 00 00 00
 *
 * For three button presses in order, demonstrating strictly above rules:
 *	BTN6, BTN1, BTN5 is 03 05 05 1d 16 00 00 00
 *	BTN5, BTN1, BTN6 is 03 05 05 1d 16 00 00 00
 *
 * In short, when BTN5/6 are pressed, the order of operations is lost, as they
 * will always float to the end when pressed in combination with others.
 *
 * Fortunately, all states are recorded in the same way, with no overlaps.
 * Byte 1 can be used as a spare for the wheel, since this is for mod keys.
 */

#define RDESC_SIZE_PAD 188
#define REPORT_SIZE_PAD 8
#define REPORT_ID_BUTTONS 3
#define PAD_BUTTON_COUNT 6
#define RDESC_KEYBOARD_OFFSET 148

static const __u8 fixed_rdesc_pad[] = {
	/* Copy of pen descriptor to avoid losing functionality */
	UsagePage_Digitizers
	Usage_Dig_Pen
	CollectionApplication(
		ReportId(7)
		Usage_Dig_Stylus
		CollectionPhysical(
			Usage_Dig_TipSwitch
			Usage_Dig_BarrelSwitch
			Usage_Dig_Eraser
			Usage_Dig_Invert
			Usage_Dig_InRange
			LogicalMinimum_i8(0)
			LogicalMaximum_i8(1)
			ReportSize(1)
			ReportCount(5)
			Input(Var|Abs)
			ReportCount(3)
			Input(Const) /* Input (Const, Var, Abs) */
			UsagePage_GenericDesktop
			Usage_GD_X
			LogicalMinimum_i16(0)
			LogicalMaximum_i16(22352)
			UnitExponent(-3)
			Unit(in) /* (EnglishLinear: in) */
			PhysicalMinimum_i16(0)
			PhysicalMaximum_i16(22352)
			ReportSize(16)
			ReportCount(1)
			Input(Var|Abs)
			Usage_GD_Y
			LogicalMinimum_i16(0)
			LogicalMaximum_i16(13970)
			UnitExponent(-3)
			Unit(in) /* (EnglishLinear: in) */
			PhysicalMinimum_i16(0)
			PhysicalMaximum_i16(13970)
			ReportSize(16)
			ReportCount(1)
			Input(Var|Abs)
			UsagePage_Digitizers
			Usage_Dig_TipPressure
			LogicalMinimum_i16(0)
			LogicalMaximum_i16(8191)
			ReportSize(16)
			ReportCount(1)
			Input(Var|Abs)
		)
	)

	/* FIXES BEGIN */
	UsagePage_GenericDesktop
	Usage_GD_Keypad
	CollectionApplication(
		ReportId(REPORT_ID_BUTTONS) /* Retain original ID on byte 0 */
		ReportCount(1)
		ReportSize(REPORT_SIZE_PAD)
		UsagePage_Digitizers
		Usage_Dig_TabletFunctionKeys
		CollectionPhysical(
			/* Byte 1: Dial state */
			UsagePage_GenericDesktop
			Usage_GD_Dial
			LogicalMinimum_i8(-1)
			LogicalMaximum_i8(1)
			ReportCount(1)
			ReportSize(REPORT_SIZE_PAD)
			Input(Var|Rel)
			/* Byte 2: Button state */
			UsagePage_Button
			ReportSize(1)
			ReportCount(PAD_BUTTON_COUNT)
			UsageMinimum_i8(0x01)
			UsageMaximum_i8(PAD_BUTTON_COUNT) /* Number of buttons */
			LogicalMinimum_i8(0x0)
			LogicalMaximum_i8(0x1)
			Input(Var|Abs)
			/* Byte 3: Exists to be tablet pad */
			UsagePage_Digitizers
			Usage_Dig_BarrelSwitch
			ReportCount(1)
			ReportSize(1)
			Input(Var|Abs)
			ReportCount(7) /* Padding, to fill full report space */
			Input(Const)
			/* Byte 4/5: Exists to be a tablet pad */
			UsagePage_GenericDesktop
			Usage_GD_X
			Usage_GD_Y
			ReportCount(2)
			ReportSize(8)
			Input(Var|Abs)
			/* Bytes 6/7: Padding, to match original length */
			ReportCount(2)
			ReportSize(8)
			Input(Const)
		)
		FixedSizeVendorReport(RDESC_SIZE_PAD)
	)
};

SEC(HID_BPF_RDESC_FIXUP)
int BPF_PROG(xppen_deco02_rdesc_fixup, struct hid_bpf_ctx *hctx)
{
	__u8 *data = hid_bpf_get_data(hctx, 0, HID_MAX_DESCRIPTOR_SIZE);

	if (!data)
		return 0; /* EPERM Check */

	if (hctx->size == RDESC_SIZE_PAD) {
		__builtin_memcpy(data, fixed_rdesc_pad, sizeof(fixed_rdesc_pad));
		return sizeof(fixed_rdesc_pad);
	}

	return 0;
}

SEC(HID_BPF_DEVICE_EVENT)
int BPF_PROG(xppen_deco02_device_event, struct hid_bpf_ctx *hctx)
{
	__u8 *data = hid_bpf_get_data(hctx, 0, REPORT_SIZE_PAD);

	if (!data || data[0] != REPORT_ID_BUTTONS)
		return 0; /* EPERM or wrong report */

	__u8 dial_code = 0;
	__u8 button_mask = 0;
	size_t d;

	/* Start from 2; 0 is report ID, 1 is modifier keys, replaced by dial */
	for (d = 2; d < 8; d++) {
		switch (data[d]) {
		case 0x2e:
			dial_code = 1;
			break;
		case 0x2d:
			dial_code = -1;
			break;
		/* below are buttons, top to bottom */
		case 0x05:
			button_mask |= BIT(0);
			break;
		case 0x2c:
			button_mask |= BIT(1);
			break;
		case 0x08:
			button_mask |= BIT(2);
			break;
		case 0x0c:
			button_mask |= BIT(3);
			break;
		case 0x1d:
			button_mask |= BIT(4);
			break;
		case 0x16:
			button_mask |= BIT(05);
			break;
		default:
			break;
		}
	}

	__u8 report[8] = { REPORT_ID_BUTTONS, dial_code, button_mask, 0x00 };

	__builtin_memcpy(data, report, sizeof(report));
	return 0;
}

HID_BPF_OPS(xppen_deco02) = {
	.hid_rdesc_fixup = (void *)xppen_deco02_rdesc_fixup,
	.hid_device_event = (void *)xppen_deco02_device_event,
};

SEC("syscall")
int probe(struct hid_bpf_probe_args *ctx)
{
	ctx->retval = ctx->rdesc_size != RDESC_SIZE_PAD ? -EINVAL : 0;
	return 0;
}

char _license[] SEC("license") = "GPL";
