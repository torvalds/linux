/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _BPF_HID_BPF_DISPATCH_H
#define _BPF_HID_BPF_DISPATCH_H

#include <linux/hid.h>

struct hid_bpf_ctx_kern {
	struct hid_bpf_ctx ctx;
	u8 *data;
};

struct hid_device *hid_get_device(unsigned int hid_id);
void hid_put_device(struct hid_device *hid);
int hid_bpf_allocate_event_data(struct hid_device *hdev);
int hid_bpf_preload_skel(void);
void hid_bpf_free_links_and_skel(void);
int hid_bpf_get_prog_attach_type(struct bpf_prog *prog);
int __hid_bpf_attach_prog(struct hid_device *hdev, enum hid_bpf_prog_type prog_type, int prog_fd,
			  struct bpf_prog *prog, __u32 flags);
void __hid_bpf_destroy_device(struct hid_device *hdev);
void __hid_bpf_ops_destroy_device(struct hid_device *hdev);
int hid_bpf_prog_run(struct hid_device *hdev, enum hid_bpf_prog_type type,
		     struct hid_bpf_ctx_kern *ctx_kern);
int hid_bpf_reconnect(struct hid_device *hdev);

struct bpf_prog;

#endif
