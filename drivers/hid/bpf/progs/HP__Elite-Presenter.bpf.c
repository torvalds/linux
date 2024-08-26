// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2023 Benjamin Tissoires
 */

#include "vmlinux.h"
#include "hid_bpf.h"
#include "hid_bpf_helpers.h"
#include <bpf/bpf_tracing.h>

#define VID_HP 0x03F0
#define PID_ELITE_PRESENTER 0x464A

HID_BPF_CONFIG(
	HID_DEVICE(BUS_BLUETOOTH, HID_GROUP_GENERIC, VID_HP, PID_ELITE_PRESENTER)
);

/*
 * Already fixed as of commit 0db117359e47 ("HID: add quirk for 03f0:464a
 * HP Elite Presenter Mouse") in the kernel, but this is a slightly better
 * fix.
 *
 * The HP Elite Presenter Mouse HID Record Descriptor shows
 * two mice (Report ID 0x1 and 0x2), one keypad (Report ID 0x5),
 * two Consumer Controls (Report IDs 0x6 and 0x3).
 * Prior to these fixes it registers one mouse, one keypad
 * and one Consumer Control, and it was usable only as a
 * digital laser pointer (one of the two mouses).
 * We replace the second mouse collection with a pointer collection,
 * allowing to use the device both as a mouse and a digital laser
 * pointer.
 */

SEC(HID_BPF_RDESC_FIXUP)
int BPF_PROG(hid_fix_rdesc, struct hid_bpf_ctx *hctx)
{
	__u8 *data = hid_bpf_get_data(hctx, 0 /* offset */, 4096 /* size */);

	if (!data)
		return 0; /* EPERM check */

	/* replace application mouse by application pointer on the second collection */
	if (data[79] == 0x02)
		data[79] = 0x01;

	return 0;
}

HID_BPF_OPS(hp_elite_presenter) = {
	.hid_rdesc_fixup = (void *)hid_fix_rdesc,
};

SEC("syscall")
int probe(struct hid_bpf_probe_args *ctx)
{
	ctx->retval = ctx->rdesc_size != 264;
	if (ctx->retval)
		ctx->retval = -EINVAL;

	return 0;
}

char _license[] SEC("license") = "GPL";
