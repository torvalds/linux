/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2022 Benjamin Tissoires
 */

#ifndef ____HID_BPF__H
#define ____HID_BPF__H

#define HID_BPF_DEVICE_EVENT "struct_ops/hid_device_event"
#define HID_BPF_RDESC_FIXUP  "struct_ops/hid_rdesc_fixup"
#define HID_BPF_OPS(name) SEC(".struct_ops.link") \
	struct hid_bpf_ops name
#define hid_set_name(_hdev, _name) __builtin_memcpy(_hdev->name, _name, sizeof(_name))

struct hid_bpf_probe_args {
	unsigned int hid;
	unsigned int rdesc_size;
	unsigned char rdesc[4096];
	int retval;
};

#endif /* ____HID_BPF__H */
