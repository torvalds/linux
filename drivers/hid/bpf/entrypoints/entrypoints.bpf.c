// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Benjamin Tissoires */

#include ".output/vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

#define HID_BPF_MAX_PROGS 1024

extern bool call_hid_bpf_prog_release(u64 prog, int table_cnt) __ksym;

struct {
	__uint(type, BPF_MAP_TYPE_PROG_ARRAY);
	__uint(max_entries, HID_BPF_MAX_PROGS);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, sizeof(__u32));
} hid_jmp_table SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, HID_BPF_MAX_PROGS * HID_BPF_PROG_TYPE_MAX);
	__type(key, void *);
	__type(value, __u8);
} progs_map SEC(".maps");

SEC("fmod_ret/__hid_bpf_tail_call")
int BPF_PROG(hid_tail_call, struct hid_bpf_ctx *hctx)
{
	bpf_tail_call(ctx, &hid_jmp_table, hctx->index);

	return 0;
}

static void release_prog(u64 prog)
{
	u8 *value;

	value = bpf_map_lookup_elem(&progs_map, &prog);
	if (!value)
		return;

	if (call_hid_bpf_prog_release(prog, *value))
		bpf_map_delete_elem(&progs_map, &prog);
}

SEC("fexit/bpf_prog_release")
int BPF_PROG(hid_prog_release, struct inode *inode, struct file *filp)
{
	u64 prog = (u64)filp->private_data;

	release_prog(prog);

	return 0;
}

SEC("fexit/bpf_free_inode")
int BPF_PROG(hid_free_inode, struct inode *inode)
{
	u64 prog = (u64)inode->i_private;

	release_prog(prog);

	return 0;
}

char LICENSE[] SEC("license") = "GPL";
