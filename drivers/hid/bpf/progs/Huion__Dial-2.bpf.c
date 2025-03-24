// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2024 Red Hat, Inc
 */

#include "vmlinux.h"
#include "hid_bpf.h"
#include "hid_bpf_helpers.h"
#include "hid_report_helpers.h"
#include <bpf/bpf_tracing.h>

#define VID_HUION 0x256C
#define PID_DIAL_2 0x0060


HID_BPF_CONFIG(
	HID_DEVICE(BUS_USB, HID_GROUP_GENERIC, VID_HUION, PID_DIAL_2),
);

/* Filled in by udev-hid-bpf */
char UDEV_PROP_HUION_FIRMWARE_ID[64];

/* The prefix of the firmware ID we expect for this device. The full firmware
 * string has a date suffix, e.g. HUION_T21j_221221
 */
char EXPECTED_FIRMWARE_ID[] = "HUION_T216_";

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
 * # HUION Huion Tablet_Q630M
 * # 0x06, 0x00, 0xff,              // Usage Page (Vendor Defined Page 1)  0
 * # 0x09, 0x01,                    // Usage (Vendor Usage 1)              3
 * # 0xa1, 0x01,                    // Collection (Application)            5
 * # 0x85, 0x08,                    //  Report ID (8)                      7
 * # 0x75, 0x58,                    //  Report Size (88)                   9
 * # 0x95, 0x01,                    //  Report Count (1)                   11
 * # 0x09, 0x01,                    //  Usage (Vendor Usage 1)             13
 * # 0x81, 0x02,                    //  Input (Data,Var,Abs)               15
 * # 0xc0,                          // End Collection                      17
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
 * # HUION Huion Tablet_Q630M
 * # 0x05, 0x0d,                    // Usage Page (Digitizers)             0
 * # 0x09, 0x02,                    // Usage (Pen)                         2
 * # 0xa1, 0x01,                    // Collection (Application)            4
 * # 0x85, 0x0a,                    //  Report ID (10)                     6
 * # 0x09, 0x20,                    //  Usage (Stylus)                     8
 * # 0xa1, 0x01,                    //  Collection (Application)           10
 * # 0x09, 0x42,                    //   Usage (Tip Switch)                12
 * # 0x09, 0x44,                    //   Usage (Barrel Switch)             14
 * # 0x09, 0x45,                    //   Usage (Eraser)                    16
 * # 0x09, 0x3c,                    //   Usage (Invert)                    18
 * # 0x15, 0x00,                    //   Logical Minimum (0)               20
 * # 0x25, 0x01,                    //   Logical Maximum (1)               22
 * # 0x75, 0x01,                    //   Report Size (1)                   24
 * # 0x95, 0x06,                    //   Report Count (6)                  26
 * # 0x81, 0x02,                    //   Input (Data,Var,Abs)              28
 * # 0x09, 0x32,                    //   Usage (In Range)                  30
 * # 0x75, 0x01,                    //   Report Size (1)                   32
 * # 0x95, 0x01,                    //   Report Count (1)                  34
 * # 0x81, 0x02,                    //   Input (Data,Var,Abs)              36
 * # 0x81, 0x03,                    //   Input (Cnst,Var,Abs)              38
 * # 0x05, 0x01,                    //   Usage Page (Generic Desktop)      40
 * # 0x09, 0x30,                    //   Usage (X)                         42
 * # 0x09, 0x31,                    //   Usage (Y)                         44
 * # 0x55, 0x0d,                    //   Unit Exponent (-3)                46
 * # 0x65, 0x33,                    //   Unit (EnglishLinear: in³)         48
 * # 0x26, 0xff, 0x7f,              //   Logical Maximum (32767)           50
 * # 0x35, 0x00,                    //   Physical Minimum (0)              53
 * # 0x46, 0x00, 0x08,              //   Physical Maximum (2048)           55
 * # 0x75, 0x10,                    //   Report Size (16)                  58
 * # 0x95, 0x02,                    //   Report Count (2)                  60
 * # 0x81, 0x02,                    //   Input (Data,Var,Abs)              62
 * # 0x05, 0x0d,                    //   Usage Page (Digitizers)           64
 * # 0x09, 0x30,                    //   Usage (Tip Pressure)              66
 * # 0x26, 0xff, 0x1f,              //   Logical Maximum (8191)            68
 * # 0x75, 0x10,                    //   Report Size (16)                  71
 * # 0x95, 0x01,                    //   Report Count (1)                  73
 * # 0x81, 0x02,                    //   Input (Data,Var,Abs)              75
 * # 0x09, 0x3d,                    //   Usage (X Tilt)                    77
 * # 0x09, 0x3e,                    //   Usage (Y Tilt)                    79
 * # 0x15, 0x81,                    //   Logical Minimum (-127)            81
 * # 0x25, 0x7f,                    //   Logical Maximum (127)             83
 * # 0x75, 0x08,                    //   Report Size (8)                   85
 * # 0x95, 0x02,                    //   Report Count (2)                  87
 * # 0x81, 0x02,                    //   Input (Data,Var,Abs)              89
 * # 0xc0,                          //  End Collection                     91
 * # 0xc0,                          // End Collection                      92
 * R: 93 05 0d 09 02 a1 01 85 0a 09 20 a1 01 09 42 09 44 09 45 09 3c 15 00 25 01 75 01 95 06 81 02 09 32 75 01 95 01 81 02 81 03 05 01 09 30 09 31 55 0d 65 33 26 ff 7f 35 00 46 00 08 75 10 95 02 81 02 05 0d 09 30 26 ff 1f 75 10 95 01 81 02 09 3d 09 3e 15 81 25 7f 75 08 95 02 81 02 c0 c0
 *
 * Third hidraw node is the pad which sends a combination of keyboard shortcuts until
 * the tablet is switched to raw mode, then it's mute:
 *
 * # Report descriptor length: 148 bytes
 * # HUION Huion Tablet_Q630M
 * # 0x05, 0x01,                    // Usage Page (Generic Desktop)        0
 * # 0x09, 0x0e,                    // Usage (System Multi-Axis Controller) 2
 * # 0xa1, 0x01,                    // Collection (Application)            4
 * # 0x85, 0x11,                    //  Report ID (17)                     6
 * # 0x05, 0x0d,                    //  Usage Page (Digitizers)            8
 * # 0x09, 0x21,                    //  Usage (Puck)                       10
 * # 0xa1, 0x02,                    //  Collection (Logical)               12
 * # 0x15, 0x00,                    //   Logical Minimum (0)               14
 * # 0x25, 0x01,                    //   Logical Maximum (1)               16
 * # 0x75, 0x01,                    //   Report Size (1)                   18
 * # 0x95, 0x01,                    //   Report Count (1)                  20
 * # 0xa1, 0x00,                    //   Collection (Physical)             22
 * # 0x05, 0x09,                    //    Usage Page (Button)              24
 * # 0x09, 0x01,                    //    Usage (Vendor Usage 0x01)        26
 * # 0x81, 0x02,                    //    Input (Data,Var,Abs)             28
 * # 0x05, 0x0d,                    //    Usage Page (Digitizers)          30
 * # 0x09, 0x33,                    //    Usage (Touch)                    32
 * # 0x81, 0x02,                    //    Input (Data,Var,Abs)             34
 * # 0x95, 0x06,                    //    Report Count (6)                 36
 * # 0x81, 0x03,                    //    Input (Cnst,Var,Abs)             38
 * # 0xa1, 0x02,                    //    Collection (Logical)             40
 * # 0x05, 0x01,                    //     Usage Page (Generic Desktop)    42
 * # 0x09, 0x37,                    //     Usage (Dial)                    44
 * # 0x16, 0x00, 0x80,              //     Logical Minimum (-32768)        46
 * # 0x26, 0xff, 0x7f,              //     Logical Maximum (32767)         49
 * # 0x75, 0x10,                    //     Report Size (16)                52
 * # 0x95, 0x01,                    //     Report Count (1)                54
 * # 0x81, 0x06,                    //     Input (Data,Var,Rel)            56
 * # 0x35, 0x00,                    //     Physical Minimum (0)            58
 * # 0x46, 0x10, 0x0e,              //     Physical Maximum (3600)         60
 * # 0x15, 0x00,                    //     Logical Minimum (0)             63
 * # 0x26, 0x10, 0x0e,              //     Logical Maximum (3600)          65
 * # 0x09, 0x48,                    //     Usage (Resolution Multiplier)   68
 * # 0xb1, 0x02,                    //     Feature (Data,Var,Abs)          70
 * # 0x45, 0x00,                    //     Physical Maximum (0)            72
 * # 0xc0,                          //    End Collection                   74
 * # 0x75, 0x08,                    //    Report Size (8)                  75
 * # 0x95, 0x01,                    //    Report Count (1)                 77
 * # 0x81, 0x01,                    //    Input (Cnst,Arr,Abs)             79
 * # 0x75, 0x08,                    //    Report Size (8)                  81
 * # 0x95, 0x01,                    //    Report Count (1)                 83
 * # 0x81, 0x01,                    //    Input (Cnst,Arr,Abs)             85
 * # 0x75, 0x08,                    //    Report Size (8)                  87
 * # 0x95, 0x01,                    //    Report Count (1)                 89
 * # 0x81, 0x01,                    //    Input (Cnst,Arr,Abs)             91
 * # 0x75, 0x08,                    //    Report Size (8)                  93
 * # 0x95, 0x01,                    //    Report Count (1)                 95
 * # 0x81, 0x01,                    //    Input (Cnst,Arr,Abs)             97
 * # 0x75, 0x08,                    //    Report Size (8)                  99
 * # 0x95, 0x01,                    //    Report Count (1)                 101
 * # 0x81, 0x01,                    //    Input (Cnst,Arr,Abs)             103
 * # 0xc0,                          //   End Collection                    105
 * # 0xc0,                          //  End Collection                     106
 * # 0xc0,                          // End Collection                      107
 * # 0x05, 0x01,                    // Usage Page (Generic Desktop)        108
 * # 0x09, 0x06,                    // Usage (Keyboard)                    110
 * # 0xa1, 0x01,                    // Collection (Application)            112
 * # 0x85, 0x03,                    //  Report ID (3)                      114
 * # 0x05, 0x07,                    //  Usage Page (Keyboard)              116
 * # 0x19, 0xe0,                    //  Usage Minimum (224)                118
 * # 0x29, 0xe7,                    //  Usage Maximum (231)                120
 * # 0x15, 0x00,                    //  Logical Minimum (0)                122
 * # 0x25, 0x01,                    //  Logical Maximum (1)                124
 * # 0x75, 0x01,                    //  Report Size (1)                    126
 * # 0x95, 0x08,                    //  Report Count (8)                   128
 * # 0x81, 0x02,                    //  Input (Data,Var,Abs)               130
 * # 0x05, 0x07,                    //  Usage Page (Keyboard)              132
 * # 0x19, 0x00,                    //  Usage Minimum (0)                  134
 * # 0x29, 0xff,                    //  Usage Maximum (255)                136
 * # 0x26, 0xff, 0x00,              //  Logical Maximum (255)              138
 * # 0x75, 0x08,                    //  Report Size (8)                    141
 * # 0x95, 0x06,                    //  Report Count (6)                   143
 * # 0x81, 0x00,                    //  Input (Data,Arr,Abs)               145
 * # 0xc0,                          // End Collection                      147
 * R: 148 05 01 09 0e a1 01 85 11 05 0d 09 21 a1 02 15 00 25 01 75 01 95 01 a1 00 05 09 09 01 81 02 05 0d 09 33 81 02 95 06 81 03 a1 02 05 01 09 37 16 00 80 26 ff 7f 75 10 95 01 81 06 35 00 46 10 0e 15 00 26 10 0e 09 48 b1 02 45 00 c0 75 08 95 01 81 01 75 08 95 01 81 01 75 08 95 01 81 01 75 08 95 01 81 01 75 08 95 01 81 01 c0 c0 c0 05 01 09 06 a1 01 85 03 05 07 19 e0 29 e7 15 00 25 01 75 01 95 08 81 02 05 07 19 00 29 ff 26 ff 00 75 08 95 06 81 00 c0
 */

#define PAD_REPORT_DESCRIPTOR_LENGTH 148
#define PEN_REPORT_DESCRIPTOR_LENGTH 93
#define VENDOR_REPORT_DESCRIPTOR_LENGTH 18
#define PAD_REPORT_ID 3
#define DIAL_REPORT_ID 17
#define PEN_REPORT_ID 10
#define VENDOR_REPORT_ID 8
#define PAD_REPORT_LENGTH 9
#define PEN_REPORT_LENGTH 10
#define VENDOR_REPORT_LENGTH 12


__u8 last_button_state;

static const __u8 fixed_rdesc_pad[] = {
	UsagePage_GenericDesktop
	Usage_GD_Keypad
	CollectionApplication(
		// -- Byte 0 in report
		ReportId(PAD_REPORT_ID)
		LogicalMaximum_i8(0)
		LogicalMaximum_i8(1)
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
			// Byte 4 in report is the dial
			Usage_GD_Wheel
			LogicalMinimum_i8(-1)
			LogicalMaximum_i8(1)
			ReportCount(1)
			ReportSize(8)
			Input(Var|Rel)
			// Byte 5 is the button state
			UsagePage_Button
			UsageMinimum_i8(0x01)
			UsageMaximum_i8(0x08)
			LogicalMinimum_i8(0x0)
			LogicalMaximum_i8(0x1)
			ReportCount(7)
			ReportSize(1)
			Input(Var|Abs)
			ReportCount(1) // padding
			Input(Const)
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
			LogicalMinimum_i8(0)
			LogicalMaximum_i8(1)
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
				PhysicalMinimum_i16(0)
				PhysicalMaximum_i16(266)
				LogicalMinimum_i16(0)
				LogicalMaximum_i16(32767)
				Usage_GD_X
				Input(Var|Abs) // Bytes 2+3
				PhysicalMinimum_i16(0)
				PhysicalMaximum_i16(166)
				LogicalMinimum_i16(0)
				LogicalMaximum_i16(32767)
				Usage_GD_Y
				Input(Var|Abs) // Bytes 4+5
			)
			UsagePage_Digitizers
			Usage_Dig_TipPressure
			LogicalMinimum_i16(0)
			LogicalMaximum_i16(8191)
			Input(Var|Abs) // Byte 6+7
			ReportSize(8)
			ReportCount(2)
			LogicalMinimum_i8(-60)
			LogicalMaximum_i8(60)
			Usage_Dig_XTilt
			Usage_Dig_YTilt
			Input(Var|Abs) // Byte 8+9
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
			LogicalMinimum_i8(0)
			LogicalMaximum_i8(1)
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
				LogicalMinimum_i16(0)
				LogicalMaximum_i16(53340)
				PhysicalMinimum_i16(0)
				PhysicalMaximum_i16(266)
				// Bytes 2/3 in report
				Usage_GD_X
				Input(Var|Abs)
				LogicalMinimum_i16(0)
				LogicalMaximum_i16(33340)
				PhysicalMinimum_i16(0)
				PhysicalMaximum_i16(166)
				// Bytes 4/5 in report
				Usage_GD_Y
				Input(Var|Abs)
			)
			// Bytes 6/7 in report
			LogicalMinimum_i16(0)
			LogicalMaximum_i16(8191)
			Usage_Dig_TipPressure
			Input(Var|Abs)
			// Bytes 8/9 in report
			ReportCount(1) // Padding
			Input(Const)
			LogicalMinimum_i8(-60)
			LogicalMaximum_i8(60)
			// Byte 10 in report
			Usage_Dig_XTilt
			// Byte 11 in report
			Usage_Dig_YTilt
			ReportSize(8)
			ReportCount(2)
			Input(Var|Abs)
		)
	)
	UsagePage_GenericDesktop
	Usage_GD_Keypad
	CollectionApplication(
		// Byte 0
		ReportId(PAD_REPORT_ID)
		LogicalMinimum_i8(0)
		LogicalMaximum_i8(1)
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
			UsageMinimum_i8(0x1)
			UsageMaximum_i8(0x8)
			LogicalMinimum_i8(0x0)
			LogicalMaximum_i8(0x1)
			ReportCount(8)
			ReportSize(1)
			Input(Var|Abs)
			// Byte 5 is the top dial
			UsagePage_GenericDesktop
			Usage_GD_Wheel
			LogicalMinimum_i8(-1)
			LogicalMaximum_i8(1)
			ReportCount(1)
			ReportSize(8)
			Input(Var|Rel)
			// Byte 6 is the bottom dial
			UsagePage_Consumer
			Usage_Con_ACPan
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
int BPF_PROG(dial_2_fix_rdesc, struct hid_bpf_ctx *hctx)
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
int BPF_PROG(dial_2_fix_events, struct hid_bpf_ctx *hctx)
{
	__u8 *data = hid_bpf_get_data(hctx, 0 /* offset */, 16 /* size */);
	static __u8 button;

	if (!data)
		return 0; /* EPERM check */

	/* Only sent if tablet is in default mode */
	if (data[0] == PAD_REPORT_ID) {
		/* Nicely enough, this device only supports one button down at a time so
		 * the reports are easy to match. Buttons numbered from the top
		 *   Button released: 03 00 00 00 00 00 00 00
		 *   Button 1: 03 00 05 00 00 00 00 00 -> b
		 *   Button 2: 03 00 08 00 00 00 00 00 -> e
		 *   Button 3: 03 00 0c 00 00 00 00 00 -> i
		 *   Button 4: 03 00 e0 16 00 00 00 00 -> Ctrl S
		 *   Button 5: 03 00 2c 00 00 00 00 00 -> space
		 *   Button 6: 03 00 e0 e2 1d 00 00 00 -> Ctrl Alt Z
		 */
		button &= 0xc0;

		switch ((data[2] << 16) | (data[3] << 8) | data[4]) {
		case 0x000000:
			break;
		case 0x050000:
			button |= BIT(0);
			break;
		case 0x080000:
			button |= BIT(1);
			break;
		case 0x0c0000:
			button |= BIT(2);
			break;
		case 0xe01600:
			button |= BIT(3);
			break;
		case 0x2c0000:
			button |= BIT(4);
			break;
		case 0xe0e21d:
			button |= BIT(5);
			break;
		}

		__u8 report[8] = {PAD_REPORT_ID, 0x0, 0x0, 0x0, 0x00, button};

		__builtin_memcpy(data, report, sizeof(report));
		return sizeof(report);
	}

	/* Only sent if tablet is in default mode */
	if (data[0] == DIAL_REPORT_ID) {
		/*
		 * In default mode, both dials are merged together:
		 *
		 *   Dial down: 11 00 ff ff 00 00 00 00 00 -> Dial -1
		 *   Dial up:   11 00 01 00 00 00 00 00 00 -> Dial 1
		 */
		__u16 dial = data[3] << 8 | data[2];

		button &= 0x3f;
		button |= !!data[1] << 6;

		__u8 report[] = {PAD_REPORT_ID, 0x0, 0x0, 0x0, dial, button};

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
				__u8 dial_1;
				__u8 dial_2;
			} __attribute__((packed)) *pad_report;
			__u8 dial_1 = 0, dial_2 = 0;

			/* Dial report */
			if (data[1] == 0xf1) {
				__u8 d = 0;

				if (data[5] == 2)
					d = 0xff;
				else
					d = data[5];

				if (data[3] == 1)
					dial_1 = d;
				else
					dial_2 = d;
			} else {
				/* data[4] are the buttons, mapped correctly */
				last_button_state = data[4];
				dial_1 = 0; // dial
				dial_2 = 0;
			}

			pad_report = (struct pad_report *)data;

			pad_report->report_id = PAD_REPORT_ID;
			pad_report->btn_stylus = 0;
			pad_report->x = 0;
			pad_report->y = 0;
			pad_report->buttons = last_button_state;
			pad_report->dial_1 = dial_1;
			pad_report->dial_2 = dial_2;

			return sizeof(struct pad_report);
		}

		/* Pen reports need nothing done */
	}

	return 0;
}

HID_BPF_OPS(inspiroy_dial2) = {
	.hid_device_event = (void *)dial_2_fix_events,
	.hid_rdesc_fixup = (void *)dial_2_fix_rdesc,
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
