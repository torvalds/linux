// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2025 Benjamin Tissoires
 */

#include "vmlinux.h"
#include "hid_bpf.h"
#include "hid_bpf_helpers.h"
#include "hid_report_helpers.h"
#include "hid_usages.h"
#include <bpf/bpf_tracing.h>

HID_BPF_CONFIG(
	HID_DEVICE(BUS_ANY, HID_GROUP_MULTITOUCH_WIN_8, HID_VID_ANY, HID_PID_ANY),
);

EXPORT_UDEV_PROP(HID_DIGITIZER_PAD_TYPE, 32);

__u8 hw_req_buf[1024];

/* to be filled by udev-hid-bpf */
struct hid_rdesc_descriptor HID_REPORT_DESCRIPTOR;

SEC("syscall")
int probe(struct hid_bpf_probe_args *ctx)
{
	struct hid_rdesc_report *pad_type_feature = NULL;
	struct hid_rdesc_field *pad_type = NULL;
	struct hid_rdesc_report *feature;
	struct hid_bpf_ctx *hid_ctx;
	char *pad_type_str = "";
	int ret;

	hid_bpf_for_each_feature_report(&HID_REPORT_DESCRIPTOR, feature) {
		struct hid_rdesc_field *field;

		hid_bpf_for_each_field(feature, field) {
			if (field->usage_page == HidUsagePage_Digitizers &&
			    field->usage_id == HidUsage_Dig_PadType) {
				pad_type = field;
				pad_type_feature = feature;
				break;
			}
		}
		if (pad_type)
			break;
	}

	if (!pad_type || !pad_type_feature) {
		ctx->retval = -EINVAL;
		return 0;
	}

	hid_ctx = hid_bpf_allocate_context(ctx->hid);

	if (!hid_ctx)
		return -1; /* EPERM check */

	hw_req_buf[0] = pad_type_feature->report_id;

	ret = hid_bpf_hw_request(hid_ctx, hw_req_buf, sizeof(hw_req_buf),
					HID_FEATURE_REPORT, HID_REQ_GET_REPORT);
	hid_bpf_release_context(hid_ctx);

	if (ret < 0) {
		ctx->retval = ret;
		return 0;
	}

	ctx->retval = 0;

	switch (EXTRACT_BITS(hw_req_buf, pad_type)) {
	case 0:
		pad_type_str = "Clickpad";
		break;
	case 1:
		pad_type_str = "Pressurepad";
		break;
	case 2:
		pad_type_str = "Discrete";
		break;
	default:
		pad_type_str = "Unknown";
	}

	UDEV_PROP_SPRINTF(HID_DIGITIZER_PAD_TYPE, "%s", pad_type_str);

	return 0;
}

char _license[] SEC("license") = "GPL";
