// SPDX-License-Identifier: GPL-2.0-or-later
/* Copyright (c) 2025 TUXEDO Computers GmbH
 */

#include "vmlinux.h"
#include "hid_bpf.h"
#include "hid_bpf_helpers.h"
#include <bpf/bpf_tracing.h>

HID_BPF_CONFIG(
	HID_DEVICE(BUS_USB, HID_GROUP_GENERIC, 0x048D, 0x8910)
);

SEC(HID_BPF_DEVICE_EVENT)
int BPF_PROG(ignore_key_fix_event, struct hid_bpf_ctx *hid_ctx)
{
	const int expected_length = 37;
	const int expected_report_id = 1;
	__u8 *data;
	int i;

	if (hid_ctx->size < expected_length)
		return 0;

	data = hid_bpf_get_data(hid_ctx, 0, expected_length);
	if (!data || data[0] != expected_report_id)
		return 0;

	// Zero out F13 (HID usage ID: 0x68) key press.
	// The first 6 parallel key presses (excluding modifier keys) are
	// encoded in an array containing usage IDs.
	for (i = 3; i < 9; ++i)
		if (data[i] == 0x68)
			data[i] = 0x00;
	// Additional parallel key presses starting with the 7th (excluding
	// modifier keys) are encoded as a bit flag with the offset being
	// the usage ID.
	data[22] &= 0xfe;

	return 0;
}

HID_BPF_OPS(ignore_button) = {
	.hid_device_event = (void *)ignore_key_fix_event,
};

char _license[] SEC("license") = "GPL";
