/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _BPF_HID_BPF_DISPATCH_H
#define _BPF_HID_BPF_DISPATCH_H

#include <linux/hid.h>

struct hid_bpf_ctx_kern {
	struct hid_bpf_ctx ctx;
	u8 *data;
	bool from_bpf;
};

struct hid_device *hid_get_device(unsigned int hid_id);
void hid_put_device(struct hid_device *hid);
int hid_bpf_allocate_event_data(struct hid_device *hdev);
void __hid_bpf_ops_destroy_device(struct hid_device *hdev);
int hid_bpf_reconnect(struct hid_device *hdev);

struct bpf_prog;

#endif
