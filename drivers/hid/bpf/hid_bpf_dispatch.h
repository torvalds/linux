/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _BPF_HID_BPF_DISPATCH_H
#define _BPF_HID_BPF_DISPATCH_H

#include <linux/hid.h>

struct hid_bpf_ctx_kern {
	struct hid_bpf_ctx ctx;
	u8 *data;
};

int hid_bpf_preload_skel(void);
void hid_bpf_free_links_and_skel(void);
int hid_bpf_get_prog_attach_type(int prog_fd);
int __hid_bpf_attach_prog(struct hid_device *hdev, enum hid_bpf_prog_type prog_type, int prog_fd,
			  __u32 flags);
void __hid_bpf_destroy_device(struct hid_device *hdev);
int hid_bpf_prog_run(struct hid_device *hdev, enum hid_bpf_prog_type type,
		     struct hid_bpf_ctx_kern *ctx_kern);
int hid_bpf_reconnect(struct hid_device *hdev);

struct bpf_prog;

#endif
