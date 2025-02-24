// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2024 Red Hat, Inc
 */

#include "vmlinux.h"
#include "hid_bpf.h"
#include "hid_bpf_helpers.h"
#include "hid_report_helpers.h"
#include <bpf/bpf_tracing.h>

#define HID_BPF_ASYNC_MAX_CTX 1
#include "hid_bpf_async.h"

#define VID_UGEE		0x28BD
/* same PID whether connected directly or through the provided dongle: */
#define PID_ACK05_REMOTE	0x0202


HID_BPF_CONFIG(
	HID_DEVICE(BUS_USB, HID_GROUP_GENERIC, VID_UGEE, PID_ACK05_REMOTE),
);

/*
 * By default, the pad reports the buttons through a set of key sequences.
 *
 * The pad reports a classic keyboard report descriptor:
 * # HANVON UGEE Shortcut Remote
 * Report descriptor length: 102 bytes
 *  0x05, 0x01,                    // Usage Page (Generic Desktop)              0
 *  0x09, 0x02,                    // Usage (Mouse)                             2
 *  0xa1, 0x01,                    // Collection (Application)                  4
 *  0x85, 0x09,                    //   Report ID (9)                           6
 *  0x09, 0x01,                    //   Usage (Pointer)                         8
 *  0xa1, 0x00,                    //   Collection (Physical)                   10
 *  0x05, 0x09,                    //     Usage Page (Button)                   12
 *  0x19, 0x01,                    //     UsageMinimum (1)                      14
 *  0x29, 0x03,                    //     UsageMaximum (3)                      16
 *  0x15, 0x00,                    //     Logical Minimum (0)                   18
 *  0x25, 0x01,                    //     Logical Maximum (1)                   20
 *  0x95, 0x03,                    //     Report Count (3)                      22
 *  0x75, 0x01,                    //     Report Size (1)                       24
 *  0x81, 0x02,                    //     Input (Data,Var,Abs)                  26
 *  0x95, 0x05,                    //     Report Count (5)                      28
 *  0x81, 0x01,                    //     Input (Cnst,Arr,Abs)                  30
 *  0x05, 0x01,                    //     Usage Page (Generic Desktop)          32
 *  0x09, 0x30,                    //     Usage (X)                             34
 *  0x09, 0x31,                    //     Usage (Y)                             36
 *  0x26, 0xff, 0x7f,              //     Logical Maximum (32767)               38
 *  0x95, 0x02,                    //     Report Count (2)                      41
 *  0x75, 0x10,                    //     Report Size (16)                      43
 *  0x81, 0x02,                    //     Input (Data,Var,Abs)                  45
 *  0x05, 0x0d,                    //     Usage Page (Digitizers)               47
 *  0x09, 0x30,                    //     Usage (Tip Pressure)                  49
 *  0x26, 0xff, 0x07,              //     Logical Maximum (2047)                51
 *  0x95, 0x01,                    //     Report Count (1)                      54
 *  0x75, 0x10,                    //     Report Size (16)                      56
 *  0x81, 0x02,                    //     Input (Data,Var,Abs)                  58
 *  0xc0,                          //   End Collection                          60
 *  0xc0,                          // End Collection                            61
 *  0x05, 0x01,                    // Usage Page (Generic Desktop)              62
 *  0x09, 0x06,                    // Usage (Keyboard)                          64
 *  0xa1, 0x01,                    // Collection (Application)                  66
 *  0x85, 0x06,                    //   Report ID (6)                           68
 *  0x05, 0x07,                    //   Usage Page (Keyboard/Keypad)            70
 *  0x19, 0xe0,                    //   UsageMinimum (224)                      72
 *  0x29, 0xe7,                    //   UsageMaximum (231)                      74
 *  0x15, 0x00,                    //   Logical Minimum (0)                     76
 *  0x25, 0x01,                    //   Logical Maximum (1)                     78
 *  0x75, 0x01,                    //   Report Size (1)                         80
 *  0x95, 0x08,                    //   Report Count (8)                        82
 *  0x81, 0x02,                    //   Input (Data,Var,Abs)                    84
 *  0x05, 0x07,                    //   Usage Page (Keyboard/Keypad)            86
 *  0x19, 0x00,                    //   UsageMinimum (0)                        88
 *  0x29, 0xff,                    //   UsageMaximum (255)                      90
 *  0x26, 0xff, 0x00,              //   Logical Maximum (255)                   92
 *  0x75, 0x08,                    //   Report Size (8)                         95
 *  0x95, 0x06,                    //   Report Count (6)                        97
 *  0x81, 0x00,                    //   Input (Data,Arr,Abs)                    99
 *  0xc0,                          // End Collection                            101
 *
 * Each button gets assigned the following events:
 *
 *   Buttons released: 06 00 00 00 00 00 00 00
 *   Button 1:         06 01 12 00 00 00 00 00 -> LControl + o
 *   Button 2:         06 01 11 00 00 00 00 00 -> LControl + n
 *   Button 3:         06 00 3e 00 00 00 00 00 -> F5
 *   Button 4:         06 02 00 00 00 00 00 00 -> LShift
 *   Button 5:         06 01 00 00 00 00 00 00 -> LControl
 *   Button 6:         06 04 00 00 00 00 00 00 -> LAlt
 *   Button 7:         06 01 16 00 00 00 00 00 -> LControl + s
 *   Button 8:         06 01 1d 00 00 00 00 00 -> LControl + z
 *   Button 9:         06 00 2c 00 00 00 00 00 -> Space
 *   Button 10:        06 03 1d 00 00 00 00 00 -> LControl + LShift + z
 *   Wheel:            06 01 57 00 00 00 00 00 -> clockwise rotation (LControl + Keypad Plus)
 *   Wheel:            06 01 56 00 00 00 00 00 -> counter-clockwise rotation
 *						  (LControl + Keypad Minus)
 *
 * However, multiple buttons can be pressed at the same time, and when this happens,
 * each button gets assigned a new slot in the Input (Data,Arr,Abs):
 *
 *   Button 1 + 3:     06 01 12 3e 00 00 00 00 -> LControl + o + F5
 *
 * When a modifier is pressed (Button 4, 5, or 6), the assigned key is set to 00:
 *
 *   Button 5 + 7:     06 01 00 16 00 00 00 00 -> LControl + s
 *
 * This is mostly fine, but with Button 8 and Button 10 sharing the same
 * key value ("z"), there are cases where we can not know which is which.
 *
 */

#define PAD_WIRED_DESCRIPTOR_LENGTH 102
#define PAD_DONGLE_DESCRIPTOR_LENGTH 177
#define STYLUS_DESCRIPTOR_LENGTH 109
#define VENDOR_DESCRIPTOR_LENGTH 36
#define PAD_REPORT_ID 6
#define RAW_PAD_REPORT_ID 0xf0
#define RAW_BATTERY_REPORT_ID 0xf2
#define VENDOR_REPORT_ID 2
#define PAD_REPORT_LENGTH 8
#define VENDOR_REPORT_LENGTH 12

__u16 last_button_state;

static const __u8 disabled_rdesc[] = {
	// Make sure we match our original report length
	FixedSizeVendorReport(VENDOR_REPORT_LENGTH)
};

static const __u8 fixed_rdesc_vendor[] = {
	UsagePage_GenericDesktop
	Usage_GD_Keypad
	CollectionApplication(
		// -- Byte 0 in report
		ReportId(RAW_PAD_REPORT_ID)
		// Byte 1 in report - same than report ID
		ReportCount(1)
		ReportSize(8)
		Input(Const) // padding (internal report ID)
		LogicalMaximum_i8(0)
		LogicalMaximum_i8(1)
		UsagePage_Digitizers
		Usage_Dig_TabletFunctionKeys
		CollectionPhysical(
			// Byte 2-3 is the button state
			UsagePage_Button
			UsageMinimum_i8(0x01)
			UsageMaximum_i8(0x0a)
			LogicalMinimum_i8(0x0)
			LogicalMaximum_i8(0x1)
			ReportCount(10)
			ReportSize(1)
			Input(Var|Abs)
			Usage_i8(0x31) // will be mapped as BTN_A / BTN_SOUTH
			ReportCount(1)
			Input(Var|Abs)
			ReportCount(5) // padding
			Input(Const)
			// Byte 4 in report - just exists so we get to be a tablet pad
			Usage_Dig_BarrelSwitch // BTN_STYLUS
			ReportCount(1)
			ReportSize(1)
			Input(Var|Abs)
			ReportCount(7) // padding
			Input(Const)
			// Bytes 5/6 in report - just exists so we get to be a tablet pad
			UsagePage_GenericDesktop
			Usage_GD_X
			Usage_GD_Y
			ReportCount(2)
			ReportSize(8)
			Input(Var|Abs)
			// Byte 7 in report is the dial
			Usage_GD_Wheel
			LogicalMinimum_i8(-1)
			LogicalMaximum_i8(1)
			ReportCount(1)
			ReportSize(8)
			Input(Var|Rel)
		)
		// -- Byte 0 in report
		ReportId(RAW_BATTERY_REPORT_ID)
		// Byte 1 in report - same than report ID
		ReportCount(1)
		ReportSize(8)
		Input(Const) // padding (internal report ID)
		// Byte 2 in report - always 0x01
		Input(Const) // padding (internal report ID)
		UsagePage_Digitizers
		/*
		 * We represent the device as a stylus to force the kernel to not
		 * directly query its battery state. Instead the kernel will rely
		 * only on the provided events.
		 */
		Usage_Dig_Stylus
		CollectionPhysical(
			// Byte 3 in report - battery value
			UsagePage_BatterySystem
			Usage_BS_AbsoluteStateOfCharge
			LogicalMinimum_i8(0)
			LogicalMaximum_i8(100)
			ReportCount(1)
			ReportSize(8)
			Input(Var|Abs)
			// Byte 4 in report - charging state
			Usage_BS_Charging
			LogicalMinimum_i8(0)
			LogicalMaximum_i8(1)
			ReportCount(1)
			ReportSize(8)
			Input(Var|Abs)
		)
	)
};

SEC(HID_BPF_RDESC_FIXUP)
int BPF_PROG(ack05_fix_rdesc, struct hid_bpf_ctx *hctx)
{
	__u8 *data = hid_bpf_get_data(hctx, 0 /* offset */, HID_MAX_DESCRIPTOR_SIZE /* size */);
	__s32 rdesc_size = hctx->size;

	if (!data)
		return 0; /* EPERM check */

	if (rdesc_size == VENDOR_DESCRIPTOR_LENGTH) {
		/*
		 * The vendor fixed rdesc is appended after the current one,
		 * to keep the output reports working.
		 */
		__builtin_memcpy(data + rdesc_size, fixed_rdesc_vendor, sizeof(fixed_rdesc_vendor));
		return sizeof(fixed_rdesc_vendor) + rdesc_size;
	}

	hid_set_name(hctx->hid, "Disabled by HID-BPF Hanvon Ugee Shortcut Remote");

	__builtin_memcpy(data, disabled_rdesc, sizeof(disabled_rdesc));
	return sizeof(disabled_rdesc);
}

static int HID_BPF_ASYNC_FUN(switch_to_raw_mode)(struct hid_bpf_ctx *hid)
{
	static __u8 magic_0[32] = {0x02, 0xb0, 0x04, 0x00, 0x00};
	int err;

	/*
	 * The proprietary driver sends the 3 following packets after the
	 * above one.
	 * These don't seem to have any effect, so we don't send them to save
	 * some processing time.
	 *
	 * static __u8 magic_1[32] = {0x02, 0xb4, 0x01, 0x00, 0x01};
	 * static __u8 magic_2[32] = {0x02, 0xb4, 0x01, 0x00, 0xff};
	 * static __u8 magic_3[32] = {0x02, 0xb8, 0x04, 0x00, 0x00};
	 */

	err = hid_bpf_hw_output_report(hid, magic_0, sizeof(magic_0));
	if (err < 0)
		return err;

	return 0;
}

SEC(HID_BPF_DEVICE_EVENT)
int BPF_PROG(ack05_fix_events, struct hid_bpf_ctx *hctx)
{
	__u8 *data = hid_bpf_get_data(hctx, 0 /* offset */, PAD_REPORT_LENGTH);
	int ret = 0;

	if (!data)
		return 0; /* EPERM check */

	if (data[0] != VENDOR_REPORT_ID)
		return 0;

	/* reconnect event */
	if (data[1] == 0xf8 && data[2] == 02 && data[3] == 0x01)
		HID_BPF_ASYNC_DELAYED_CALL(switch_to_raw_mode, hctx, 10);

	/* button event */
	if (data[1] == RAW_PAD_REPORT_ID) {
		data[0] = data[1];
		if (data[7] == 0x02)
			data[7] = 0xff;
		ret = 8;
	} else if (data[1] == RAW_BATTERY_REPORT_ID) {
		data[0] = data[1];
		ret = 5;
	}

	return ret;
}

HID_BPF_OPS(xppen_ack05_remote) = {
	.hid_device_event = (void *)ack05_fix_events,
	.hid_rdesc_fixup = (void *)ack05_fix_rdesc,
};

SEC("syscall")
int probe(struct hid_bpf_probe_args *ctx)
{
	switch (ctx->rdesc_size) {
	case PAD_WIRED_DESCRIPTOR_LENGTH:
	case PAD_DONGLE_DESCRIPTOR_LENGTH:
	case STYLUS_DESCRIPTOR_LENGTH:
	case VENDOR_DESCRIPTOR_LENGTH:
		ctx->retval = 0;
		break;
	default:
		ctx->retval = -EINVAL;
		break;
	}

	if (ctx->rdesc_size == VENDOR_DESCRIPTOR_LENGTH) {
		struct hid_bpf_ctx *hctx = hid_bpf_allocate_context(ctx->hid);

		if (!hctx) {
			ctx->retval = -EINVAL;
			return 0;
		}

		ctx->retval = HID_BPF_ASYNC_INIT(switch_to_raw_mode) ||
			      switch_to_raw_mode(hctx);

		hid_bpf_release_context(hctx);
	}

	return 0;
}

char _license[] SEC("license") = "GPL";
