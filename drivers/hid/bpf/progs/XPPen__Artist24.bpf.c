// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2023 Benjamin Tissoires
 */

#include "vmlinux.h"
#include "hid_bpf.h"
#include "hid_bpf_helpers.h"
#include <bpf/bpf_tracing.h>

#define VID_UGEE 0x28BD /* VID is shared with SinoWealth and Glorious and prob others */
#define PID_ARTIST_24 0x093A
#define PID_ARTIST_24_PRO 0x092D

HID_BPF_CONFIG(
	HID_DEVICE(BUS_USB, HID_GROUP_GENERIC, VID_UGEE, PID_ARTIST_24),
	HID_DEVICE(BUS_USB, HID_GROUP_GENERIC, VID_UGEE, PID_ARTIST_24_PRO)
);

/*
 * We need to amend the report descriptor for the following:
 * - the device reports Eraser instead of using Secondary Barrel Switch
 * - the pen doesn't have a rubber tail, so basically we are removing any
 *   eraser/invert bits
 */
static const __u8 fixed_rdesc[] = {
	0x05, 0x0d,                    // Usage Page (Digitizers)             0
	0x09, 0x02,                    // Usage (Pen)                         2
	0xa1, 0x01,                    // Collection (Application)            4
	0x85, 0x07,                    //  Report ID (7)                      6
	0x09, 0x20,                    //  Usage (Stylus)                     8
	0xa1, 0x00,                    //  Collection (Physical)              10
	0x09, 0x42,                    //   Usage (Tip Switch)                12
	0x09, 0x44,                    //   Usage (Barrel Switch)             14
	0x09, 0x5a,                    //   Usage (Secondary Barrel Switch)   16  /* changed from 0x45 (Eraser) to 0x5a (Secondary Barrel Switch) */
	0x15, 0x00,                    //   Logical Minimum (0)               18
	0x25, 0x01,                    //   Logical Maximum (1)               20
	0x75, 0x01,                    //   Report Size (1)                   22
	0x95, 0x03,                    //   Report Count (3)                  24
	0x81, 0x02,                    //   Input (Data,Var,Abs)              26
	0x95, 0x02,                    //   Report Count (2)                  28
	0x81, 0x03,                    //   Input (Cnst,Var,Abs)              30
	0x09, 0x32,                    //   Usage (In Range)                  32
	0x95, 0x01,                    //   Report Count (1)                  34
	0x81, 0x02,                    //   Input (Data,Var,Abs)              36
	0x95, 0x02,                    //   Report Count (2)                  38
	0x81, 0x03,                    //   Input (Cnst,Var,Abs)              40
	0x75, 0x10,                    //   Report Size (16)                  42
	0x95, 0x01,                    //   Report Count (1)                  44
	0x35, 0x00,                    //   Physical Minimum (0)              46
	0xa4,                          //   Push                              48
	0x05, 0x01,                    //   Usage Page (Generic Desktop)      49
	0x09, 0x30,                    //   Usage (X)                         51
	0x65, 0x13,                    //   Unit (EnglishLinear: in)          53
	0x55, 0x0d,                    //   Unit Exponent (-3)                55
	0x46, 0xf0, 0x50,              //   Physical Maximum (20720)          57
	0x26, 0xff, 0x7f,              //   Logical Maximum (32767)           60
	0x81, 0x02,                    //   Input (Data,Var,Abs)              63
	0x09, 0x31,                    //   Usage (Y)                         65
	0x46, 0x91, 0x2d,              //   Physical Maximum (11665)          67
	0x26, 0xff, 0x7f,              //   Logical Maximum (32767)           70
	0x81, 0x02,                    //   Input (Data,Var,Abs)              73
	0xb4,                          //   Pop                               75
	0x09, 0x30,                    //   Usage (Tip Pressure)              76
	0x45, 0x00,                    //   Physical Maximum (0)              78
	0x26, 0xff, 0x1f,              //   Logical Maximum (8191)            80
	0x81, 0x42,                    //   Input (Data,Var,Abs,Null)         83
	0x09, 0x3d,                    //   Usage (X Tilt)                    85
	0x15, 0x81,                    //   Logical Minimum (-127)            87
	0x25, 0x7f,                    //   Logical Maximum (127)             89
	0x75, 0x08,                    //   Report Size (8)                   91
	0x95, 0x01,                    //   Report Count (1)                  93
	0x81, 0x02,                    //   Input (Data,Var,Abs)              95
	0x09, 0x3e,                    //   Usage (Y Tilt)                    97
	0x15, 0x81,                    //   Logical Minimum (-127)            99
	0x25, 0x7f,                    //   Logical Maximum (127)             101
	0x81, 0x02,                    //   Input (Data,Var,Abs)              103
	0xc0,                          //  End Collection                     105
	0xc0,                          // End Collection                      106
};

#define TIP_SWITCH		BIT(0)
#define BARREL_SWITCH		BIT(1)
#define ERASER			BIT(2)
/* padding			BIT(3) */
/* padding			BIT(4) */
#define IN_RANGE		BIT(5)
/* padding			BIT(6) */
/* padding			BIT(7) */

#define U16(index) (data[index] | (data[index + 1] << 8))

SEC(HID_BPF_RDESC_FIXUP)
int BPF_PROG(hid_fix_rdesc_xppen_artist24, struct hid_bpf_ctx *hctx)
{
	__u8 *data = hid_bpf_get_data(hctx, 0 /* offset */, 4096 /* size */);

	if (!data)
		return 0; /* EPERM check */

	__builtin_memcpy(data, fixed_rdesc, sizeof(fixed_rdesc));

	return sizeof(fixed_rdesc);
}

static __u8 prev_state = 0;

/*
 * There are a few cases where the device is sending wrong event
 * sequences, all related to the second button (the pen doesn't
 * have an eraser switch on the tail end):
 *
 *   whenever the second button gets pressed or released, an
 *   out-of-proximity event is generated and then the firmware
 *   compensate for the missing state (and the firmware uses
 *   eraser for that button):
 *
 *   - if the pen is in range, an extra out-of-range is sent
 *     when the second button is pressed/released:
 *     // Pen is in range
 *     E:                               InRange
 *
 *     // Second button is pressed
 *     E:
 *     E:                        Eraser InRange
 *
 *     // Second button is released
 *     E:
 *     E:                               InRange
 *
 *     This case is ignored by this filter, it's "valid"
 *     and userspace knows how to deal with it, there are just
 *     a few out-of-prox events generated, but the user doesn´t
 *     see them.
 *
 *   - if the pen is in contact, 2 extra events are added when
 *     the second button is pressed/released: an out of range
 *     and an in range:
 *
 *     // Pen is in contact
 *     E: TipSwitch                     InRange
 *
 *     // Second button is pressed
 *     E:                                         <- false release, needs to be filtered out
 *     E:                        Eraser InRange   <- false release, needs to be filtered out
 *     E: TipSwitch              Eraser InRange
 *
 *     // Second button is released
 *     E:                                         <- false release, needs to be filtered out
 *     E:                               InRange   <- false release, needs to be filtered out
 *     E: TipSwitch                     InRange
 *
 */
SEC(HID_BPF_DEVICE_EVENT)
int BPF_PROG(xppen_24_fix_eraser, struct hid_bpf_ctx *hctx)
{
	__u8 *data = hid_bpf_get_data(hctx, 0 /* offset */, 10 /* size */);
	__u8 current_state, changed_state;
	bool prev_tip;

	if (!data)
		return 0; /* EPERM check */

	current_state = data[1];

	/* if the state is identical to previously, early return */
	if (current_state == prev_state)
		return 0;

	prev_tip = !!(prev_state & TIP_SWITCH);

	/*
	 * Illegal transition: pen is in range with the tip pressed, and
	 * it goes into out of proximity.
	 *
	 * Ideally we should hold the event, start a timer and deliver it
	 * only if the timer ends, but we are not capable of that now.
	 *
	 * And it doesn't matter because when we are in such cases, this
	 * means we are detecting a false release.
	 */
	if ((current_state & IN_RANGE) == 0) {
		if (prev_tip)
			return HID_IGNORE_EVENT;
		return 0;
	}

	/*
	 * XOR to only set the bits that have changed between
	 * previous and current state
	 */
	changed_state = prev_state ^ current_state;

	/* Store the new state for future processing */
	prev_state = current_state;

	/*
	 * We get both a tipswitch and eraser change in the same HID report:
	 * this is not an authorized transition and is unlikely to happen
	 * in real life.
	 * This is likely to be added by the firmware to emulate the
	 * eraser mode so we can skip the event.
	 */
	if ((changed_state & (TIP_SWITCH | ERASER)) == (TIP_SWITCH | ERASER)) /* we get both a tipswitch and eraser change at the same time */
		return HID_IGNORE_EVENT;

	return 0;
}

HID_BPF_OPS(xppen_artist_24) = {
	.hid_rdesc_fixup = (void *)hid_fix_rdesc_xppen_artist24,
	.hid_device_event = (void *)xppen_24_fix_eraser,
};

SEC("syscall")
int probe(struct hid_bpf_probe_args *ctx)
{
	/*
	 * The device exports 3 interfaces.
	 */
	ctx->retval = ctx->rdesc_size != 107;
	if (ctx->retval)
		ctx->retval = -EINVAL;

	/* ensure the kernel isn't fixed already */
	if (ctx->rdesc[17] != 0x45) /* Eraser */
		ctx->retval = -EINVAL;

	return 0;
}

char _license[] SEC("license") = "GPL";
