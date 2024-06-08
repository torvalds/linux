/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2022 Benjamin Tissoires
 */

#ifndef ____HID_BPF__H
#define ____HID_BPF__H

#define HID_BPF_DEVICE_EVENT "fmod_ret/hid_bpf_device_event"
#define HID_BPF_RDESC_FIXUP  "fmod_ret/hid_bpf_rdesc_fixup"

struct hid_bpf_probe_args {
	unsigned int hid;
	unsigned int rdesc_size;
	unsigned char rdesc[4096];
	int retval;
};

#endif /* ____HID_BPF__H */
