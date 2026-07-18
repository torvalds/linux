// SPDX-License-Identifier: GPL-2.0-only
#include "vmlinux.h"
#include "hid_bpf.h"
#include "hid_bpf_helpers.h"
#include <bpf/bpf_tracing.h>

/*
 * Huion Inspiroy Frego M Pen Tablet
 * Model L610
 * 256c:8251 (Bluetooth)
 * 256c:2012 (USB)
 */
#define VID_HUION			0x256C
#define PID_INSPIROY_FREGO_M		0x8251
#define PID_L610			0x2012

#define PEN_RDESC_SIZE			125
#define SECONDARY_SWITCH_OFFSET		17

HID_BPF_CONFIG(
	HID_DEVICE(BUS_BLUETOOTH, HID_GROUP_GENERIC, VID_HUION, PID_INSPIROY_FREGO_M),
	HID_DEVICE(BUS_USB, HID_GROUP_GENERIC, VID_HUION, PID_L610)
);

/*
 * The pen descriptor reports the second side button as Secondary Tip Switch
 * instead of Secondary Barrel Switch.
 *
 * Relevant part of the original pen report descriptor:
 *
 * 0x09, 0x42,       // Usage (Tip Switch)                  12
 * 0x09, 0x44,       // Usage (Barrel Switch)               14
 * 0x09, 0x43,       // Usage (Secondary Tip Switch)        16 <- change to 0x5a
 * 0x09, 0x3c,       // Usage (Invert)                      18
 * 0x09, 0x45,       // Usage (Eraser)                      20
 * 0x15, 0x00,       // Logical Minimum (0)                 22
 * 0x25, 0x01,       // Logical Maximum (1)                 24
 */
SEC(HID_BPF_RDESC_FIXUP)
int BPF_PROG(fix_secondary_barrel_rdesc, struct hid_bpf_ctx *hctx)
{
	__u8 *data = hid_bpf_get_data(hctx, 0 /* offset */, HID_MAX_DESCRIPTOR_SIZE /* size */);

	if (!data)
		return 0; /* EPERM check */

	if (hctx->size != PEN_RDESC_SIZE)
		return 0;

	if (data[0] != 0x05 || data[1] != 0x0d || /* Usage Page (Digitizers) */
	    data[2] != 0x09 || data[3] != 0x02 || /* Usage (Pen) */
	    data[16] != 0x09 ||
	    data[SECONDARY_SWITCH_OFFSET] != 0x43) /* Secondary Tip Switch */
		return 0;

	data[SECONDARY_SWITCH_OFFSET] = 0x5a;

	return 0;
}

HID_BPF_OPS(fix_secondary_barrel) = {
	.hid_rdesc_fixup = (void *)fix_secondary_barrel_rdesc,
};

SEC("syscall")
int probe(struct hid_bpf_probe_args *ctx)
{
	ctx->retval = ctx->rdesc_size != PEN_RDESC_SIZE;
	if (ctx->retval) {
		ctx->retval = -EINVAL;
		return 0;
	}

	if (ctx->rdesc[0] != 0x05 || ctx->rdesc[1] != 0x0d || /* Usage Page (Digitizers) */
	    ctx->rdesc[2] != 0x09 || ctx->rdesc[3] != 0x02 || /* Usage (Pen) */
	    ctx->rdesc[16] != 0x09 ||
	    ctx->rdesc[SECONDARY_SWITCH_OFFSET] != 0x43) { /* Secondary Tip Switch */
		ctx->retval = -EINVAL;
		return 0;
	}

	ctx->retval = 0;

	return 0;
}

char _license[] SEC("license") = "GPL";
