// SPDX-License-Identifier: GPL-2.0-only
/* Fix for Trust Philips SPK6327 (145f:024b)
 * Modifier keys report as Array (0x00) instead of Variable (0x02)
 * causing LCtrl, LAlt, Super etc. to all act as LShift
 */
#include "vmlinux.h"
#include "hid_bpf.h"
#include "hid_bpf_helpers.h"
#include <bpf/bpf_tracing.h>

#define VID_TRUST 0x145F
#define PID_SPK6327 0x024B

HID_BPF_CONFIG(
	HID_DEVICE(BUS_USB, HID_GROUP_GENERIC, VID_TRUST, PID_SPK6327)
);

SEC(HID_BPF_RDESC_FIXUP)
int BPF_PROG(hid_fix_rdesc, struct hid_bpf_ctx *hctx)
{
	__u8 *data = hid_bpf_get_data(hctx, 0, 4096);

	if (!data)
		return 0;

	/* Fix modifier keys: Input Array (0x00) -> Input Variable (0x02) */
	if (data[101] == 0x00)
		data[101] = 0x02;

	return 0;
}

HID_BPF_OPS(trust_spk6327) = {
	.hid_rdesc_fixup = (void *)hid_fix_rdesc,
};

SEC("syscall")
int probe(struct hid_bpf_probe_args *ctx)
{
	/* Only apply to interface 1 (169 bytes) not interface 0 (62 bytes) */
	if (ctx->rdesc_size == 169)
		ctx->retval = 0;
	else
		ctx->retval = -EINVAL;

	return 0;
}

char _license[] SEC("license") = "GPL";
