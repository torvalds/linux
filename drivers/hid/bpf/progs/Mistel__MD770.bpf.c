// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Tatsuyuki Ishi
 */

#include "vmlinux.h"
#include "hid_bpf.h"
#include "hid_bpf_helpers.h"
#include <bpf/bpf_tracing.h>

#define VID_HOLTEK	0x04D9
#define PID_MD770	0x0339
#define RDESC_SIZE	203

HID_BPF_CONFIG(
	HID_DEVICE(BUS_USB, HID_GROUP_GENERIC, VID_HOLTEK, PID_MD770)
);

/*
 * The Mistel MD770 keyboard reports the first 6 simultaneous key presses
 * through the first interface, and anything beyond that through a second
 * interface. Unfortunately, the second interface's report descriptor has an
 * error, causing events to be malformed and ignored. This HID-BPF driver
 * fixes the descriptor to allow NKRO to work again.
 *
 * For reference, this is the original report descriptor:
 *
 * 0x05, 0x01,        // Usage Page (Generic Desktop)        0
 * 0x09, 0x80,        // Usage (System Control)              2
 * 0xa1, 0x01,        // Collection (Application)            4
 * 0x85, 0x01,        //  Report ID (1)                      6
 * 0x19, 0x81,        //  Usage Minimum (129)                8
 * 0x29, 0x83,        //  Usage Maximum (131)                10
 * 0x15, 0x00,        //  Logical Minimum (0)                12
 * 0x25, 0x01,        //  Logical Maximum (1)                14
 * 0x95, 0x03,        //  Report Count (3)                   16
 * 0x75, 0x01,        //  Report Size (1)                    18
 * 0x81, 0x02,        //  Input (Data,Var,Abs)               20
 * 0x95, 0x01,        //  Report Count (1)                   22
 * 0x75, 0x05,        //  Report Size (5)                    24
 * 0x81, 0x01,        //  Input (Cnst,Arr,Abs)               26
 * 0xc0,              // End Collection                      28
 * 0x05, 0x0c,        // Usage Page (Consumer Devices)       29
 * 0x09, 0x01,        // Usage (Consumer Control)            31
 * 0xa1, 0x01,        // Collection (Application)            33
 * 0x85, 0x02,        //  Report ID (2)                      35
 * 0x15, 0x00,        //  Logical Minimum (0)                37
 * 0x25, 0x01,        //  Logical Maximum (1)                39
 * 0x95, 0x12,        //  Report Count (18)                  41
 * 0x75, 0x01,        //  Report Size (1)                    43
 * 0x0a, 0x83, 0x01,  //  Usage (AL Consumer Control Config) 45
 * 0x0a, 0x8a, 0x01,  //  Usage (AL Email Reader)            48
 * 0x0a, 0x92, 0x01,  //  Usage (AL Calculator)              51
 * 0x0a, 0x94, 0x01,  //  Usage (AL Local Machine Browser)   54
 * 0x09, 0xcd,        //  Usage (Play/Pause)                 57
 * 0x09, 0xb7,        //  Usage (Stop)                       59
 * 0x09, 0xb6,        //  Usage (Scan Previous Track)        61
 * 0x09, 0xb5,        //  Usage (Scan Next Track)            63
 * 0x09, 0xe2,        //  Usage (Mute)                       65
 * 0x09, 0xea,        //  Usage (Volume Down)                67
 * 0x09, 0xe9,        //  Usage (Volume Up)                  69
 * 0x0a, 0x21, 0x02,  //  Usage (AC Search)                  71
 * 0x0a, 0x23, 0x02,  //  Usage (AC Home)                    74
 * 0x0a, 0x24, 0x02,  //  Usage (AC Back)                    77
 * 0x0a, 0x25, 0x02,  //  Usage (AC Forward)                 80
 * 0x0a, 0x26, 0x02,  //  Usage (AC Stop)                    83
 * 0x0a, 0x27, 0x02,  //  Usage (AC Refresh)                 86
 * 0x0a, 0x2a, 0x02,  //  Usage (AC Bookmarks)               89
 * 0x81, 0x02,        //  Input (Data,Var,Abs)               92
 * 0x95, 0x01,        //  Report Count (1)                   94
 * 0x75, 0x0e,        //  Report Size (14)                   96
 * 0x81, 0x01,        //  Input (Cnst,Arr,Abs)               98
 * 0xc0,              // End Collection                      100
 * 0x05, 0x01,        // Usage Page (Generic Desktop)        101
 * 0x09, 0x02,        // Usage (Mouse)                       103
 * 0xa1, 0x01,        // Collection (Application)            105
 * 0x09, 0x01,        //  Usage (Pointer)                    107
 * 0xa1, 0x00,        //  Collection (Physical)              109
 * 0x85, 0x03,        //   Report ID (3)                     111
 * 0x05, 0x09,        //   Usage Page (Button)               113
 * 0x19, 0x01,        //   Usage Minimum (1)                 115
 * 0x29, 0x08,        //   Usage Maximum (8)                 117
 * 0x15, 0x00,        //   Logical Minimum (0)               119
 * 0x25, 0x01,        //   Logical Maximum (1)               121
 * 0x75, 0x01,        //   Report Size (1)                   123
 * 0x95, 0x08,        //   Report Count (8)                  125
 * 0x81, 0x02,        //   Input (Data,Var,Abs)              127
 * 0x05, 0x01,        //   Usage Page (Generic Desktop)      129
 * 0x09, 0x30,        //   Usage (X)                         131
 * 0x09, 0x31,        //   Usage (Y)                         133
 * 0x16, 0x01, 0x80,  //   Logical Minimum (-32767)          135
 * 0x26, 0xff, 0x7f,  //   Logical Maximum (32767)           138
 * 0x75, 0x10,        //   Report Size (16)                  141
 * 0x95, 0x02,        //   Report Count (2)                  143
 * 0x81, 0x06,        //   Input (Data,Var,Rel)              145
 * 0x09, 0x38,        //   Usage (Wheel)                     147
 * 0x15, 0x81,        //   Logical Minimum (-127)            149
 * 0x25, 0x7f,        //   Logical Maximum (127)             151
 * 0x75, 0x08,        //   Report Size (8)                   153
 * 0x95, 0x01,        //   Report Count (1)                  155
 * 0x81, 0x06,        //   Input (Data,Var,Rel)              157
 * 0x05, 0x0c,        //   Usage Page (Consumer Devices)     159
 * 0x0a, 0x38, 0x02,  //   Usage (AC Pan)                    161
 * 0x95, 0x01,        //   Report Count (1)                  164
 * 0x81, 0x06,        //   Input (Data,Var,Rel)              166
 * 0xc0,              //  End Collection                     168
 * 0xc0,              // End Collection                      169
 * 0x05, 0x01,        // Usage Page (Generic Desktop)        170
 * 0x09, 0x06,        // Usage (Keyboard)                    172
 * 0xa1, 0x01,        // Collection (Application)            174
 * 0x85, 0x04,        //  Report ID (4)                      176
 * 0x05, 0x07,        //  Usage Page (Keyboard)              178
 * 0x95, 0x01,        //  Report Count (1)                   180
 * 0x75, 0x08,        //  Report Size (8)                    182
 * 0x81, 0x03,        //  Input (Cnst,Var,Abs)               184
 * 0x95, 0xe8,        //  Report Count (232)                 186
 * 0x75, 0x01,        //  Report Size (1)                    188
 * 0x15, 0x00,        //  Logical Minimum (0)                190
 * 0x25, 0x01,        //  Logical Maximum (1)                192
 * 0x05, 0x07,        //  Usage Page (Keyboard)              194
 * 0x19, 0x00,        //  Usage Minimum (0)                  196
 * 0x29, 0xe7,        //  Usage Maximum (231)                198
 * 0x81, 0x00,        //  Input (Data,Arr,Abs)               200  <- change to 0x81, 0x02 (Data,Var,Abs)
 * 0xc0,              // End Collection                      202
 */

SEC(HID_BPF_RDESC_FIXUP)
int BPF_PROG(hid_rdesc_fixup_mistel_md770, struct hid_bpf_ctx *hctx)
{
	__u8 *data = hid_bpf_get_data(hctx, 0, HID_MAX_DESCRIPTOR_SIZE);

	if (!data)
		return 0; /* EPERM check */

	if (data[201] == 0x00)
		data[201] = 0x02;

	return 0;
}

HID_BPF_OPS(mistel_md770) = {
	.hid_rdesc_fixup = (void *)hid_rdesc_fixup_mistel_md770,
};

SEC("syscall")
int probe(struct hid_bpf_probe_args *ctx)
{
	ctx->retval = ctx->rdesc_size != RDESC_SIZE;
	if (ctx->retval)
		ctx->retval = -EINVAL;

	return 0;
}

char _license[] SEC("license") = "GPL";
