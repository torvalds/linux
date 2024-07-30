// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2024 Kumar Swarnam Iyer (kumar.s.iyer65@gmail.com)
 */

#include "vmlinux.h"
#include "hid_bpf.h"
#include "hid_bpf_helpers.h"
#include <bpf/bpf_tracing.h>

#define VID_THRUSTMASTER 0x044F
#define PID_TCA_YOKE_BOEING 0x0409

HID_BPF_CONFIG(
	HID_DEVICE(BUS_USB, HID_GROUP_GENERIC, VID_THRUSTMASTER, PID_TCA_YOKE_BOEING)
);

/*  The original HID descriptor of the Thrustmaster TCA Yoke Boeing joystick contains
 *  an Input field that shows up as an axis, ABS_MISC in Linux. But it is not possible
 *  to assign an actual physical control to this axis as they're all taken up. There
 *  are 2 vendor-defined inputs where the Input type appears to be defined wrongly.
 *  This bpf attempts to fix this by changing the Inputs so that it doesn't show up in
 *  Linux at all.
 *  This version is the short version fix that only changes 2 fields in the descriptor
 *  instead of the whole report descriptor.
 *  For reference, this is the original report descriptor:
 *
 *  0x05, 0x01,                    // Usage Page (Generic Desktop)        0
 *  0x09, 0x04,                    // Usage (Joystick)                    2
 *  0xa1, 0x01,                    // Collection (Application)            4
 *  0x85, 0x01,                    //  Report ID (1)                      6
 *  0x09, 0x39,                    //  Usage (Hat switch)                 8
 *  0x15, 0x00,                    //  Logical Minimum (0)                10
 *  0x25, 0x07,                    //  Logical Maximum (7)                12
 *  0x35, 0x00,                    //  Physical Minimum (0)               14
 *  0x46, 0x3b, 0x01,              //  Physical Maximum (315)             16
 *  0x65, 0x14,                    //  Unit (EnglishRotation: deg)        19
 *  0x75, 0x04,                    //  Report Size (4)                    21
 *  0x95, 0x01,                    //  Report Count (1)                   23
 *  0x81, 0x42,                    //  Input (Data,Var,Abs,Null)          25
 *  0x65, 0x00,                    //  Unit (None)                        27
 *  0x05, 0x09,                    //  Usage Page (Button)                29
 *  0x19, 0x01,                    //  Usage Minimum (1)                  31
 *  0x29, 0x12,                    //  Usage Maximum (18)                 33
 *  0x15, 0x00,                    //  Logical Minimum (0)                35
 *  0x25, 0x01,                    //  Logical Maximum (1)                37
 *  0x75, 0x01,                    //  Report Size (1)                    39
 *  0x95, 0x12,                    //  Report Count (18)                  41
 *  0x81, 0x02,                    //  Input (Data,Var,Abs)               43
 *  0x95, 0x02,                    //  Report Count (2)                   45
 *  0x81, 0x03,                    //  Input (Cnst,Var,Abs)               47
 *  0x05, 0x01,                    //  Usage Page (Generic Desktop)       49
 *  0x09, 0x31,                    //  Usage (Y)                          51
 *  0x09, 0x30,                    //  Usage (X)                          53
 *  0x09, 0x32,                    //  Usage (Z)                          55
 *  0x09, 0x34,                    //  Usage (Ry)                         57
 *  0x09, 0x33,                    //  Usage (Rx)                         59
 *  0x09, 0x35,                    //  Usage (Rz)                         61
 *  0x15, 0x00,                    //  Logical Minimum (0)                63
 *  0x27, 0xff, 0xff, 0x00, 0x00,  //  Logical Maximum (65535)            65
 *  0x75, 0x10,                    //  Report Size (16)                   70
 *  0x95, 0x06,                    //  Report Count (6)                   72
 *  0x81, 0x02,                    //  Input (Data,Var,Abs)               74
 *  0x06, 0xf0, 0xff,              //  Usage Page (Vendor Usage Page 0xfff0) 76
 *  0x09, 0x59,                    //  Usage (Vendor Usage 0x59)          79
 *  0x15, 0x00,                    //  Logical Minimum (0)                81
 *  0x26, 0xff, 0x00,              //  Logical Maximum (255)              83
 *  0x75, 0x08,                    //  Report Size (8)                    86
 *  0x95, 0x01,                    //  Report Count (1)                   88
 *  0x81, 0x02,                    //  Input (Data,Var,Abs)               90 --> Needs to be changed
 *  0x09, 0x51,                    //  Usage (Vendor Usage 0x51)          92
 *  0x15, 0x00,                    //  Logical Minimum (0)                94
 *  0x26, 0xff, 0x00,              //  Logical Maximum (255)              96
 *  0x75, 0x08,                    //  Report Size (8)                    99
 *  0x95, 0x20,                    //  Report Count (32)                  101 --> Needs to be changed
 *  0x81, 0x02,                    //  Input (Data,Var,Abs)               103
 *  0x09, 0x50,                    //  Usage (Vendor Usage 0x50)          105
 *  0x15, 0x00,                    //  Logical Minimum (0)                107
 *  0x26, 0xff, 0x00,              //  Logical Maximum (255)              109
 *  0x75, 0x08,                    //  Report Size (8)                    112
 *  0x95, 0x0f,                    //  Report Count (15)                  114
 *  0x81, 0x03,                    //  Input (Cnst,Var,Abs)               116
 *  0x09, 0x47,                    //  Usage (Vendor Usage 0x47)          118
 *  0x85, 0xf2,                    //  Report ID (242)                    120
 *  0x15, 0x00,                    //  Logical Minimum (0)                122
 *  0x26, 0xff, 0x00,              //  Logical Maximum (255)              124
 *  0x75, 0x08,                    //  Report Size (8)                    127
 *  0x95, 0x3f,                    //  Report Count (63)                  129
 *  0xb1, 0x02,                    //  Feature (Data,Var,Abs)             131
 *  0x09, 0x48,                    //  Usage (Vendor Usage 0x48)          133
 *  0x85, 0xf3,                    //  Report ID (243)                    135
 *  0x15, 0x00,                    //  Logical Minimum (0)                137
 *  0x26, 0xff, 0x00,              //  Logical Maximum (255)              139
 *  0x75, 0x08,                    //  Report Size (8)                    142
 *  0x95, 0x3f,                    //  Report Count (63)                  144
 *  0xb1, 0x02,                    //  Feature (Data,Var,Abs)             146
 *  0xc0,                          // End Collection                      148
 */

SEC(HID_BPF_RDESC_FIXUP)
int BPF_PROG(hid_fix_rdesc_tca_yoke, struct hid_bpf_ctx *hctx)
{
	const int expected_length = 148;

	if (hctx->size != expected_length)
		return 0;

	__u8 *data = hid_bpf_get_data(hctx, 0 /* offset */, HID_MAX_DESCRIPTOR_SIZE /* size */);

	if (!data)
		return 0; /* EPERM */

	/* Safety check, our probe() should take care of this though */
	if (data[1] != 0x01 /* Generic Desktop */ || data[3] != 0x04 /* Joystick */)
		return 0;

	/* The report descriptor sets incorrect Input items in 2 places, resulting in a
	 * non-existing axis showing up.
	 * This change sets the correct Input which prevents the axis from showing up in Linux.
	 */

	if (data[90] == 0x81 && /* Input */
	    data[103] == 0x81) { /* Input */
		data[91] = 0x03; /* Input set to 0x03 Constant, Variable Absolute */
		data[104] = 0x03; /* Input set to 0X03 Constant, Variable Absolute */
	}

	return 0;
}

HID_BPF_OPS(tca_yoke) = {
	.hid_rdesc_fixup = (void *)hid_fix_rdesc_tca_yoke,
};

SEC("syscall")
int probe(struct hid_bpf_probe_args *ctx)
{
	/* ensure the kernel isn't fixed already */
	if (ctx->rdesc[91] != 0x02) /* Input for 0x59 Usage type has changed */
		ctx->retval = -EINVAL;

	return 0;
}

char _license[] SEC("license") = "GPL";
