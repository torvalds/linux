// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Benjamin Tissoires */

#include ".output/vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

#define HID_BPF_MAX_PROGS 1024

struct {
	__uint(type, BPF_MAP_TYPE_PROG_ARRAY);
	__uint(max_entries, HID_BPF_MAX_PROGS);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, sizeof(__u32));
} hid_jmp_table SEC(".maps");

SEC("fmod_ret/__hid_bpf_tail_call")
int BPF_PROG(hid_tail_call, struct hid_bpf_ctx *hctx)
{
	bpf_tail_call(ctx, &hid_jmp_table, hctx->index);

	return 0;
}

char LICENSE[] SEC("license") = "GPL";
