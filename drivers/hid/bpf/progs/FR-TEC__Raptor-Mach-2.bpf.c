// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2024 Benjamin Tissoires
 */

#include "vmlinux.h"
#include "hid_bpf.h"
#include "hid_bpf_helpers.h"
#include <bpf/bpf_tracing.h>

#define VID_BETOP_2185PC        0x11C0
#define PID_RAPTOR_MACH_2 0x5606

HID_BPF_CONFIG(
	HID_DEVICE(BUS_USB, HID_GROUP_GENERIC, VID_BETOP_2185PC, PID_RAPTOR_MACH_2),
);

/*
 * For reference, this is the fixed report descriptor
 *
 * static const __u8 fixed_rdesc[] = {
 *     0x05, 0x01,                    // Usage Page (Generic Desktop)        0
 *     0x09, 0x04,                    // Usage (Joystick)                    2
 *     0xa1, 0x01,                    // Collection (Application)            4
 *     0x05, 0x01,                    //  Usage Page (Generic Desktop)       6
 *     0x85, 0x01,                    //  Report ID (1)                      8
 *     0x05, 0x01,                    //  Usage Page (Generic Desktop)       10
 *     0x09, 0x30,                    //  Usage (X)                          12
 *     0x75, 0x10,                    //  Report Size (16)                   14
 *     0x95, 0x01,                    //  Report Count (1)                   16
 *     0x15, 0x00,                    //  Logical Minimum (0)                18
 *     0x26, 0xff, 0x07,              //  Logical Maximum (2047)             20
 *     0x46, 0xff, 0x07,              //  Physical Maximum (2047)            23
 *     0x81, 0x02,                    //  Input (Data,Var,Abs)               26
 *     0x05, 0x01,                    //  Usage Page (Generic Desktop)       28
 *     0x09, 0x31,                    //  Usage (Y)                          30
 *     0x75, 0x10,                    //  Report Size (16)                   32
 *     0x95, 0x01,                    //  Report Count (1)                   34
 *     0x15, 0x00,                    //  Logical Minimum (0)                36
 *     0x26, 0xff, 0x07,              //  Logical Maximum (2047)             38
 *     0x46, 0xff, 0x07,              //  Physical Maximum (2047)            41
 *     0x81, 0x02,                    //  Input (Data,Var,Abs)               44
 *     0x05, 0x01,                    //  Usage Page (Generic Desktop)       46
 *     0x09, 0x33,                    //  Usage (Rx)                         48
 *     0x75, 0x10,                    //  Report Size (16)                   50
 *     0x95, 0x01,                    //  Report Count (1)                   52
 *     0x15, 0x00,                    //  Logical Minimum (0)                54
 *     0x26, 0xff, 0x03,              //  Logical Maximum (1023)             56
 *     0x46, 0xff, 0x03,              //  Physical Maximum (1023)            59
 *     0x81, 0x02,                    //  Input (Data,Var,Abs)               62
 *     0x05, 0x00,                    //  Usage Page (Undefined)             64
 *     0x09, 0x00,                    //  Usage (Undefined)                  66
 *     0x75, 0x10,                    //  Report Size (16)                   68
 *     0x95, 0x01,                    //  Report Count (1)                   70
 *     0x15, 0x00,                    //  Logical Minimum (0)                72
 *     0x26, 0xff, 0x03,              //  Logical Maximum (1023)             74
 *     0x46, 0xff, 0x03,              //  Physical Maximum (1023)            77
 *     0x81, 0x02,                    //  Input (Data,Var,Abs)               80
 *     0x05, 0x01,                    //  Usage Page (Generic Desktop)       82
 *     0x09, 0x32,                    //  Usage (Z)                          84
 *     0x75, 0x10,                    //  Report Size (16)                   86
 *     0x95, 0x01,                    //  Report Count (1)                   88
 *     0x15, 0x00,                    //  Logical Minimum (0)                90
 *     0x26, 0xff, 0x03,              //  Logical Maximum (1023)             92
 *     0x46, 0xff, 0x03,              //  Physical Maximum (1023)            95
 *     0x81, 0x02,                    //  Input (Data,Var,Abs)               98
 *     0x05, 0x01,                    //  Usage Page (Generic Desktop)       100
 *     0x09, 0x35,                    //  Usage (Rz)                         102
 *     0x75, 0x10,                    //  Report Size (16)                   104
 *     0x95, 0x01,                    //  Report Count (1)                   106
 *     0x15, 0x00,                    //  Logical Minimum (0)                108
 *     0x26, 0xff, 0x03,              //  Logical Maximum (1023)             110
 *     0x46, 0xff, 0x03,              //  Physical Maximum (1023)            113
 *     0x81, 0x02,                    //  Input (Data,Var,Abs)               116
 *     0x05, 0x01,                    //  Usage Page (Generic Desktop)       118
 *     0x09, 0x34,                    //  Usage (Ry)                         120
 *     0x75, 0x10,                    //  Report Size (16)                   122
 *     0x95, 0x01,                    //  Report Count (1)                   124
 *     0x15, 0x00,                    //  Logical Minimum (0)                126
 *     0x26, 0xff, 0x07,              //  Logical Maximum (2047)             128
 *     0x46, 0xff, 0x07,              //  Physical Maximum (2047)            131
 *     0x81, 0x02,                    //  Input (Data,Var,Abs)               134
 *     0x05, 0x01,                    //  Usage Page (Generic Desktop)       136
 *     0x09, 0x36,                    //  Usage (Slider)                     138
 *     0x75, 0x10,                    //  Report Size (16)                   140
 *     0x95, 0x01,                    //  Report Count (1)                   142
 *     0x15, 0x00,                    //  Logical Minimum (0)                144
 *     0x26, 0xff, 0x03,              //  Logical Maximum (1023)             146
 *     0x46, 0xff, 0x03,              //  Physical Maximum (1023)            149
 *     0x81, 0x02,                    //  Input (Data,Var,Abs)               152
 *     0x05, 0x09,                    //  Usage Page (Button)                154
 *     0x19, 0x01,                    //  Usage Minimum (1)                  156
 *     0x2a, 0x1d, 0x00,              //  Usage Maximum (29)                 158
 *     0x15, 0x00,                    //  Logical Minimum (0)                161
 *     0x25, 0x01,                    //  Logical Maximum (1)                163
 *     0x75, 0x01,                    //  Report Size (1)                    165
 *     0x96, 0x80, 0x00,              //  Report Count (128)                 167
 *     0x81, 0x02,                    //  Input (Data,Var,Abs)               170
 *     0x05, 0x01,                    //  Usage Page (Generic Desktop)       172
 *     0x09, 0x39,                    //  Usage (Hat switch)                 174
 *     0x26, 0x07, 0x00,              //  Logical Maximum (7)                176 // changed (was 239)
 *     0x46, 0x68, 0x01,              //  Physical Maximum (360)             179
 *     0x65, 0x14,                    //  Unit (EnglishRotation: deg)        182
 *     0x75, 0x10,                    //  Report Size (16)                   184
 *     0x95, 0x01,                    //  Report Count (1)                   186
 *     0x81, 0x42,                    //  Input (Data,Var,Abs,Null)          188
 *     0x05, 0x01,                    //  Usage Page (Generic Desktop)       190
 *     0x09, 0x00,                    //  Usage (Undefined)                  192
 *     0x75, 0x08,                    //  Report Size (8)                    194
 *     0x95, 0x1d,                    //  Report Count (29)                  196
 *     0x81, 0x01,                    //  Input (Cnst,Arr,Abs)               198
 *     0x15, 0x00,                    //  Logical Minimum (0)                200
 *     0x26, 0xef, 0x00,              //  Logical Maximum (239)              202
 *     0x85, 0x58,                    //  Report ID (88)                     205
 *     0x26, 0xff, 0x00,              //  Logical Maximum (255)              207
 *     0x46, 0xff, 0x00,              //  Physical Maximum (255)             210
 *     0x75, 0x08,                    //  Report Size (8)                    213
 *     0x95, 0x3f,                    //  Report Count (63)                  215
 *     0x09, 0x00,                    //  Usage (Undefined)                  217
 *     0x91, 0x02,                    //  Output (Data,Var,Abs)              219
 *     0x85, 0x59,                    //  Report ID (89)                     221
 *     0x75, 0x08,                    //  Report Size (8)                    223
 *     0x95, 0x80,                    //  Report Count (128)                 225
 *     0x09, 0x00,                    //  Usage (Undefined)                  227
 *     0xb1, 0x02,                    //  Feature (Data,Var,Abs)             229
 *     0xc0,                          // End Collection                      231
 * };
 */

/*
 * We need to amend the report descriptor for the following:
 * - the joystick sends its hat_switch data between 0 and 239 but
 *   the kernel expects the logical max to stick into a signed 8 bits
 *   integer. We thus divide it by 30 to match what other joysticks are
 *   doing
 */
SEC("fmod_ret/hid_bpf_rdesc_fixup")
int BPF_PROG(hid_fix_rdesc_raptor_mach_2, struct hid_bpf_ctx *hctx)
{
	__u8 *data = hid_bpf_get_data(hctx, 0 /* offset */, HID_MAX_DESCRIPTOR_SIZE /* size */);

	if (!data)
		return 0; /* EPERM check */

	data[177] = 0x07;

	return 0;
}

/*
 * The hat_switch value at offsets 33 and 34 (16 bits) needs
 * to be reduced to a single 8 bit signed integer. So we
 * divide it by 30.
 * Byte 34 is always null, so it is ignored.
 */
SEC("fmod_ret/hid_bpf_device_event")
int BPF_PROG(raptor_mach_2_fix_hat_switch, struct hid_bpf_ctx *hctx)
{
	__u8 *data = hid_bpf_get_data(hctx, 0 /* offset */, 64 /* size */);

	if (!data)
		return 0; /* EPERM check */

	if (data[0] != 0x01) /* not the joystick report ID */
		return 0;

	data[33] /= 30;

	return 0;
}

SEC("syscall")
int probe(struct hid_bpf_probe_args *ctx)
{
	ctx->retval = ctx->rdesc_size != 232;
	if (ctx->retval)
		ctx->retval = -EINVAL;

	/* ensure the kernel isn't fixed already */
	if (ctx->rdesc[177] != 0xef) /* Logical Max of 239 */
		ctx->retval = -EINVAL;

	return 0;
}

char _license[] SEC("license") = "GPL";
