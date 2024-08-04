// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2024 Red Hat, Inc
 */

#include "vmlinux.h"
#include "hid_bpf.h"
#include "hid_bpf_helpers.h"
#include "hid_report_helpers.h"
#include <bpf/bpf_tracing.h>

#define VID_HUION 0x256C
#define PID_INSPIROY_2_S 0x0066

HID_BPF_CONFIG(
	HID_DEVICE(BUS_USB, HID_GROUP_GENERIC, VID_HUION, PID_INSPIROY_2_S),
);

/* Filled in by udev-hid-bpf */
char UDEV_PROP_HUION_FIRMWARE_ID[64];

/* The prefix of the firmware ID we expect for this device. The full firmware
 * string has a date suffix, e.g. HUION_T21j_221221
 */
char EXPECTED_FIRMWARE_ID[] = "HUION_T21j_";

/* How this BPF program works: the tablet has two modes, firmware mode and
 * tablet mode. In firmware mode (out of the box) the tablet sends button events
 * and the dial as keyboard combinations. In tablet mode it uses a vendor specific
 * hid report to report everything instead.
 * Depending on the mode some hid reports are never sent and the corresponding
 * devices are mute.
 *
 * To switch the tablet use e.g.  https://github.com/whot/huion-switcher
 * or one of the tools from the digimend project
 *
 * This BPF works for both modes. The huion-switcher tool sets the
 * HUION_FIRMWARE_ID udev property - if that is set then we disable the firmware
 * pad and pen reports (by making them vendor collections that are ignored).
 * If that property is not set we fix all hidraw nodes so the tablet works in
 * either mode though the drawback is that the device will show up twice if
 * you bind it to all event nodes
 *
 * Default report descriptor for the first exposed hidraw node:
 *
 * # HUION Huion Tablet_H641P
 * # Report descriptor length: 18 bytes
 * # 0x06, 0x00, 0xff,              // Usage Page (Vendor Defined Page 0xFF00)   0
 * # 0x09, 0x01,                    // Usage (Vendor Usage 0x01)                 3
 * # 0xa1, 0x01,                    // Collection (Application)                  5
 * # 0x85, 0x08,                    //   Report ID (8)                           7
 * # 0x75, 0x58,                    //   Report Size (88)                        9
 * # 0x95, 0x01,                    //   Report Count (1)                        11
 * # 0x09, 0x01,                    //   Usage (Vendor Usage 0x01)               13
 * # 0x81, 0x02,                    //   Input (Data,Var,Abs)                    15
 * # 0xc0,                          // End Collection                            17
 * R: 18 06 00 ff 09 01 a1 01 85 08 75 58 95 01 09 01 81 02 c0
 *
 * This rdesc does nothing until the tablet is switched to raw mode, see
 * https://github.com/whot/huion-switcher
 *
 *
 * Second hidraw node is the Pen. This one sends events until the tablet is
 * switched to raw mode, then it's mute.
 *
 * # Report descriptor length: 93 bytes
 * # 0x05, 0x0d,          // Usage Page (Digitizers)                   0
 * # 0x09, 0x02,          // Usage (Pen)                               2
 * # 0xa1, 0x01,          // Collection (Application)                  4
 * # 0x85, 0x0a,          //   Report ID (10)                          6
 * # 0x09, 0x20,          //   Usage (Stylus)                          8
 * # 0xa1, 0x01,          //   Collection (Application)                10
 * # 0x09, 0x42,          //     Usage (Tip Switch)                    12
 * # 0x09, 0x44,          //     Usage (Barrel Switch)                 14
 * # 0x09, 0x45,          //     Usage (Eraser)                        16
 * # 0x09, 0x3c,          //     Usage (Invert)                        18 <-- has no Invert eraser
 * # 0x15, 0x00,          //     Logical Minimum (0)                   20
 * # 0x25, 0x01,          //     Logical Maximum (1)                   22
 * # 0x75, 0x01,          //     Report Size (1)                       24
 * # 0x95, 0x06,          //     Report Count (6)                      26
 * # 0x81, 0x02,          //     Input (Data,Var,Abs)                  28
 * # 0x09, 0x32,          //     Usage (In Range)                      30
 * # 0x75, 0x01,          //     Report Size (1)                       32
 * # 0x95, 0x01,          //     Report Count (1)                      34
 * # 0x81, 0x02,          //     Input (Data,Var,Abs)                  36
 * # 0x81, 0x03,          //     Input (Cnst,Var,Abs)                  38
 * # 0x05, 0x01,          //     Usage Page (Generic Desktop)          40
 * # 0x09, 0x30,          //     Usage (X)                             42
 * # 0x09, 0x31,          //     Usage (Y)                             44
 * # 0x55, 0x0d,          //     Unit Exponent (-3)                    46 <-- change to -2
 * # 0x65, 0x33,          //     Unit (EnglishLinear: in³)             48 <-- change in³ to in
 * # 0x26, 0xff, 0x7f,    //     Logical Maximum (32767)               50
 * # 0x35, 0x00,          //     Physical Minimum (0)                  53
 * # 0x46, 0x00, 0x08,    //     Physical Maximum (2048)               55 <-- invalid size
 * # 0x75, 0x10,          //     Report Size (16)                      58
 * # 0x95, 0x02,          //     Report Count (2)                      60
 * # 0x81, 0x02,          //     Input (Data,Var,Abs)                  62
 * # 0x05, 0x0d,          //     Usage Page (Digitizers)               64
 * # 0x09, 0x30,          //     Usage (Tip Pressure)                  66
 * # 0x26, 0xff, 0x1f,    //     Logical Maximum (8191)                68
 * # 0x75, 0x10,          //     Report Size (16)                      71
 * # 0x95, 0x01,          //     Report Count (1)                      73
 * # 0x81, 0x02,          //     Input (Data,Var,Abs)                  75
 * # 0x09, 0x3d,          //     Usage (X Tilt)                        77 <-- No tilt reported
 * # 0x09, 0x3e,          //     Usage (Y Tilt)                        79
 * # 0x15, 0x81,          //     Logical Minimum (-127)                81
 * # 0x25, 0x7f,          //     Logical Maximum (127)                 83
 * # 0x75, 0x08,          //     Report Size (8)                       85
 * # 0x95, 0x02,          //     Report Count (2)                      87
 * # 0x81, 0x02,          //     Input (Data,Var,Abs)                  89
 * # 0xc0,                //   End Collection                          91
 * # 0xc0,                // End Collection                            92
 * R: 93 05 0d 09 02 a1 01 85 0a 09 20 a1 01 09 42 09 44 09 45 09 3c 15 00 25 01 7501 95 06 81 02 09 32 75 01 95 01 81 02 81 03 05 01 09 30 09 31 55 0d 65 33 26 ff7f 35 00 46 00 08 75 10 95 02 81 02 05 0d 09 30 26 ff 1f 75 10 95 01 81 02 09 3d09 3e 15 81 25 7f 75 08 95 02 81 02 c0 c0
 *
 * Third hidraw node is the pad which sends a combination of keyboard shortcuts until
 * the tablet is switched to raw mode, then it's mute:
 *
 * # Report descriptor length: 65 bytes
 * # 0x05, 0x01,          // Usage Page (Generic Desktop)              0
 * # 0x09, 0x06,          // Usage (Keyboard)                          2
 * # 0xa1, 0x01,          // Collection (Application)                  4
 * # 0x85, 0x03,          //   Report ID (3)                           6
 * # 0x05, 0x07,          //   Usage Page (Keyboard/Keypad)            8
 * # 0x19, 0xe0,          //   UsageMinimum (224)                      10
 * # 0x29, 0xe7,          //   UsageMaximum (231)                      12
 * # 0x15, 0x00,          //   Logical Minimum (0)                     14
 * # 0x25, 0x01,          //   Logical Maximum (1)                     16
 * # 0x75, 0x01,          //   Report Size (1)                         18
 * # 0x95, 0x08,          //   Report Count (8)                        20
 * # 0x81, 0x02,          //   Input (Data,Var,Abs)                    22
 * # 0x05, 0x07,          //   Usage Page (Keyboard/Keypad)            24
 * # 0x19, 0x00,          //   UsageMinimum (0)                        26
 * # 0x29, 0xff,          //   UsageMaximum (255)                      28
 * # 0x26, 0xff, 0x00,    //   Logical Maximum (255)                   30
 * # 0x75, 0x08,          //   Report Size (8)                         33
 * # 0x95, 0x06,          //   Report Count (6)                        35
 * # 0x81, 0x00,          //   Input (Data,Arr,Abs)                    37
 * # 0xc0,                // End Collection                            39
 * # 0x05, 0x0c,          // Usage Page (Consumer)                     40
 * # 0x09, 0x01,          // Usage (Consumer Control)                  42
 * # 0xa1, 0x01,          // Collection (Application)                  44
 * # 0x85, 0x04,          //   Report ID (4)                           46
 * # 0x19, 0x00,          //   UsageMinimum (0)                        48
 * # 0x2a, 0x3c, 0x02,    //   UsageMaximum (572)                      50
 * # 0x15, 0x00,          //   Logical Minimum (0)                     53
 * # 0x26, 0x3c, 0x02,    //   Logical Maximum (572)                   55
 * # 0x95, 0x01,          //   Report Count (1)                        58
 * # 0x75, 0x10,          //   Report Size (16)                        60
 * # 0x81, 0x00,          //   Input (Data,Arr,Abs)                    62
 * # 0xc0,                // End Collection                            64
 * R: 65 05 01 09 06 a1 01 85 03 05 07 19 e0 29 e7 15 00 25 01 75 01 95 08 81 02 0507 19 00 29 ff 26 ff 00 75 08 95 06 81 00 c0 05 0c 09 01 a1 01 85 04 19 00 2a 3c02 15 00 26 3c 02 95 01 75 10 81 00 c0
 * N: HUION Huion Tablet_H641P
 */

#define PAD_REPORT_DESCRIPTOR_LENGTH 65
#define PEN_REPORT_DESCRIPTOR_LENGTH 93
#define VENDOR_REPORT_DESCRIPTOR_LENGTH 18
#define PAD_REPORT_ID 3
#define PEN_REPORT_ID 10
#define VENDOR_REPORT_ID 8
#define PAD_REPORT_LENGTH 8
#define PEN_REPORT_LENGTH 10
#define VENDOR_REPORT_LENGTH 12


__u8 last_button_state;

static const __u8 fixed_rdesc_pad[] = {
	UsagePage_GenericDesktop
	Usage_GD_Keypad
	CollectionApplication(
		// -- Byte 0 in report
		ReportId(PAD_REPORT_ID)
		LogicalRange_i8(0, 1)
		UsagePage_Digitizers
		Usage_Dig_TabletFunctionKeys
		CollectionPhysical(
			// Byte 1 in report - just exists so we get to be a tablet pad
			Usage_Dig_BarrelSwitch // BTN_STYLUS
			ReportCount(1)
			ReportSize(1)
			Input(Var|Abs)
			ReportCount(7) // padding
			Input(Const)
			// Bytes 2/3 in report - just exists so we get to be a tablet pad
			UsagePage_GenericDesktop
			Usage_GD_X
			Usage_GD_Y
			ReportCount(2)
			ReportSize(8)
			Input(Var|Abs)
			// Byte 4 in report is the wheel
			Usage_GD_Wheel
			LogicalRange_i8(-1, 1)
			ReportCount(1)
			ReportSize(8)
			Input(Var|Rel)
			// Byte 5 is the button state
			UsagePage_Button
			UsageRange_i8(0x01, 0x6)
			LogicalRange_i8(0x01, 0x6)
			ReportCount(1)
			ReportSize(8)
			Input(Arr|Abs)
		)
		// Make sure we match our original report length
		FixedSizeVendorReport(PAD_REPORT_LENGTH)
	)
};

static const __u8 fixed_rdesc_pen[] = {
	UsagePage_Digitizers
	Usage_Dig_Pen
	CollectionApplication(
		// -- Byte 0 in report
		ReportId(PEN_REPORT_ID)
		Usage_Dig_Pen
		CollectionPhysical(
			// -- Byte 1 in report
			Usage_Dig_TipSwitch
			Usage_Dig_BarrelSwitch
			Usage_Dig_SecondaryBarrelSwitch // maps eraser to BTN_STYLUS2
			LogicalRange_i8(0, 1)
			ReportSize(1)
			ReportCount(3)
			Input(Var|Abs)
			ReportCount(4)  // Padding
			Input(Const)
			Usage_Dig_InRange
			ReportCount(1)
			Input(Var|Abs)
			ReportSize(16)
			ReportCount(1)
			PushPop(
				UsagePage_GenericDesktop
				Unit(cm)
				UnitExponent(-1)
				PhysicalRange_i16(0, 160)
				LogicalRange_i16(0, 32767)
				Usage_GD_X
				Input(Var|Abs) // Bytes 2+3
				PhysicalRange_i16(0, 100)
				LogicalRange_i16(0, 32767)
				Usage_GD_Y
				Input(Var|Abs) // Bytes 4+5
			)
			UsagePage_Digitizers
			Usage_Dig_TipPressure
			LogicalRange_i16(0, 8191)
			Input(Var|Abs) // Byte 6+7
			// Two bytes padding so we don't need to change the report at all
			ReportSize(8)
			ReportCount(2)
			Input(Const) // Byte 6+7
		)
	)
};

static const __u8 fixed_rdesc_vendor[] = {
	UsagePage_Digitizers
	Usage_Dig_Pen
	CollectionApplication(
		// Byte 0
		// We leave the pen on the vendor report ID
		ReportId(VENDOR_REPORT_ID)
		Usage_Dig_Pen
		CollectionPhysical(
			// Byte 1 are the buttons
			LogicalRange_i8(0, 1)
			ReportSize(1)
			Usage_Dig_TipSwitch
			Usage_Dig_BarrelSwitch
			Usage_Dig_SecondaryBarrelSwitch
			ReportCount(3)
			Input(Var|Abs)
			ReportCount(4) // Padding
			Input(Const)
			Usage_Dig_InRange
			ReportCount(1)
			Input(Var|Abs)
			ReportSize(16)
			ReportCount(1)
			PushPop(
				UsagePage_GenericDesktop
				Unit(cm)
				UnitExponent(-1)
				// Note: reported logical range differs
				// from the pen report ID for x and y
				LogicalRange_i16(0, 32000)
				PhysicalRange_i16(0, 160)
				// Bytes 2/3 in report
				Usage_GD_X
				Input(Var|Abs)
				LogicalRange_i16(0, 20000)
				PhysicalRange_i16(0, 100)
				// Bytes 4/5 in report
				Usage_GD_Y
				Input(Var|Abs)
			)
			// Bytes 6/7 in report
			LogicalRange_i16(0, 8192)
			Usage_Dig_TipPressure
			Input(Var|Abs)
		)
	)
	UsagePage_GenericDesktop
	Usage_GD_Keypad
	CollectionApplication(
		// Byte 0
		ReportId(PAD_REPORT_ID)
		LogicalRange_i8(0, 1)
		UsagePage_Digitizers
		Usage_Dig_TabletFunctionKeys
		CollectionPhysical(
			// Byte 1 are the buttons
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
			ReportCount(2)
			ReportSize(8)
			Input(Var|Abs)
			// Byte 4 is the button state
			UsagePage_Button
			UsageRange_i8(0x01, 0x6)
			LogicalRange_i8(0x0, 0x1)
			ReportCount(6)
			ReportSize(1)
			Input(Var|Abs)
			ReportCount(2)
			Input(Const)
			// Byte 5 is the wheel
			UsagePage_GenericDesktop
			Usage_GD_Wheel
			LogicalRange_i8(-1, 1)
			ReportCount(1)
			ReportSize(8)
			Input(Var|Rel)
		)
		// Make sure we match our original report length
		FixedSizeVendorReport(VENDOR_REPORT_LENGTH)
	)
};

static const __u8 disabled_rdesc_pen[] = {
	FixedSizeVendorReport(PEN_REPORT_LENGTH)
};

static const __u8 disabled_rdesc_pad[] = {
	FixedSizeVendorReport(PAD_REPORT_LENGTH)
};

SEC(HID_BPF_RDESC_FIXUP)
int BPF_PROG(hid_fix_rdesc, struct hid_bpf_ctx *hctx)
{
	__u8 *data = hid_bpf_get_data(hctx, 0 /* offset */, HID_MAX_DESCRIPTOR_SIZE /* size */);
	__s32 rdesc_size = hctx->size;
	__u8 have_fw_id;

	if (!data)
		return 0; /* EPERM check */

	/* If we have a firmware ID and it matches our expected prefix, we
	 * disable the default pad/pen nodes. They won't send events
	 * but cause duplicate devices.
	 */
	have_fw_id = __builtin_memcmp(UDEV_PROP_HUION_FIRMWARE_ID,
				      EXPECTED_FIRMWARE_ID,
				      sizeof(EXPECTED_FIRMWARE_ID) - 1) == 0;
	if (rdesc_size == PAD_REPORT_DESCRIPTOR_LENGTH) {
		if (have_fw_id) {
			__builtin_memcpy(data, disabled_rdesc_pad, sizeof(disabled_rdesc_pad));
			return sizeof(disabled_rdesc_pad);
		}

		__builtin_memcpy(data, fixed_rdesc_pad, sizeof(fixed_rdesc_pad));
		return sizeof(fixed_rdesc_pad);
	}
	if (rdesc_size == PEN_REPORT_DESCRIPTOR_LENGTH) {
		if (have_fw_id) {
			__builtin_memcpy(data, disabled_rdesc_pen, sizeof(disabled_rdesc_pen));
			return sizeof(disabled_rdesc_pen);
		}

		__builtin_memcpy(data, fixed_rdesc_pen, sizeof(fixed_rdesc_pen));
		return sizeof(fixed_rdesc_pen);
	}
	/* Always fix the vendor mode so the tablet will work even if nothing sets
	 * the udev property (e.g. huion-switcher run manually)
	 */
	if (rdesc_size == VENDOR_REPORT_DESCRIPTOR_LENGTH) {
		__builtin_memcpy(data, fixed_rdesc_vendor, sizeof(fixed_rdesc_vendor));
		return sizeof(fixed_rdesc_vendor);

	}
	return 0;
}

SEC(HID_BPF_DEVICE_EVENT)
int BPF_PROG(inspiroy_2_fix_events, struct hid_bpf_ctx *hctx)
{
	__u8 *data = hid_bpf_get_data(hctx, 0 /* offset */, 10 /* size */);

	if (!data)
		return 0; /* EPERM check */

	/* Only sent if tablet is in default mode */
	if (data[0] == PAD_REPORT_ID) {
		/* Nicely enough, this device only supports one button down at a time so
		 * the reports are easy to match. Buttons numbered from the top
		 *   Button released: 03 00 00 00 00 00 00 00
		 *   Button 1: 03 00 05 00 00 00 00 00 -> b
		 *   Button 2: 03 00 0c 00 00 00 00 00 -> i
		 *   Button 3: 03 00 08 00 00 00 00 00 -> e
		 *   Button 4: 03 01 16 00 00 00 00 00 -> Ctrl S
		 *   Button 5: 03 00 2c 00 00 00 00 00 -> space
		 *   Button 6: 03 05 1d 00 00 00 00 00 -> Ctrl Alt Z
		 *
		 *   Wheel down: 03 01 2d 00 00 00 00 00 -> Ctrl -
		 *   Wheel up:   03 01 2e 00 00 00 00 00 -> Ctrl =
		 */
		__u8 button = 0;
		__u8 wheel = 0;

		switch (data[1] << 8 | data[2]) {
		case 0x0000:
			break;
		case 0x0005:
			button = 1;
			break;
		case 0x000c:
			button = 2;
			break;
		case 0x0008:
			button = 3;
			break;
		case 0x0116:
			button = 4;
			break;
		case 0x002c:
			button = 5;
			break;
		case 0x051d:
			button = 6;
			break;
		case 0x012d:
			wheel = -1;
			break;
		case 0x012e:
			wheel = 1;
			break;

		}

		__u8 report[6] = {PAD_REPORT_ID, 0x0, 0x0, 0x0, wheel, button};

		__builtin_memcpy(data, report, sizeof(report));
		return sizeof(report);
	}

	/* Nothing to do for the PEN_REPORT_ID, it's already mapped */

	/* Only sent if tablet is in raw mode */
	if (data[0] == VENDOR_REPORT_ID) {
		/* Pad reports */
		if (data[1] & 0x20) {
			/* See fixed_rdesc_pad */
			struct pad_report {
				__u8 report_id;
				__u8 btn_stylus;
				__u8 x;
				__u8 y;
				__u8 buttons;
				__u8 wheel;
			} __attribute__((packed)) *pad_report;
			__u8 wheel = 0;

			/* Wheel report */
			if (data[1] == 0xf1) {
				if (data[5] == 2)
					wheel = 0xff;
				else
					wheel = data[5];
			} else {
				/* data[4] are the buttons, mapped correctly */
				last_button_state = data[4];
				wheel = 0; // wheel
			}

			pad_report = (struct pad_report *)data;

			pad_report->report_id = PAD_REPORT_ID;
			pad_report->btn_stylus = 0;
			pad_report->x = 0;
			pad_report->y = 0;
			pad_report->buttons = last_button_state;
			pad_report->wheel = wheel;

			return sizeof(struct pad_report);
		}

		/* Pen reports need nothing done */
	}

	return 0;
}

HID_BPF_OPS(inspiroy_2) = {
	.hid_device_event = (void *)inspiroy_2_fix_events,
	.hid_rdesc_fixup = (void *)hid_fix_rdesc,
};

SEC("syscall")
int probe(struct hid_bpf_probe_args *ctx)
{
	switch (ctx->rdesc_size) {
	case PAD_REPORT_DESCRIPTOR_LENGTH:
	case PEN_REPORT_DESCRIPTOR_LENGTH:
	case VENDOR_REPORT_DESCRIPTOR_LENGTH:
		ctx->retval = 0;
		break;
	default:
		ctx->retval = -EINVAL;
	}

	return 0;
}

char _license[] SEC("license") = "GPL";
