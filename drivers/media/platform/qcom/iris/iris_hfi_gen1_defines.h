/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __IRIS_HFI_GEN1_DEFINES_H__
#define __IRIS_HFI_GEN1_DEFINES_H__

#include <linux/types.h>

#define HFI_VIDEO_ARCH_OX				0x1
#define HFI_ERR_NONE					0x0

#define HFI_CMD_SYS_INIT				0x10001
#define HFI_CMD_SYS_SET_PROPERTY			0x10005
#define HFI_CMD_SYS_GET_PROPERTY			0x10006

#define HFI_PROPERTY_SYS_CODEC_POWER_PLANE_CTRL		0x5
#define HFI_PROPERTY_SYS_IMAGE_VERSION			0x6

#define HFI_EVENT_SYS_ERROR				0x1

#define HFI_MSG_SYS_INIT				0x20001
#define HFI_MSG_SYS_COV					0x20009
#define HFI_MSG_SYS_PROPERTY_INFO			0x2000a

#define HFI_MSG_EVENT_NOTIFY				0x21001

struct hfi_pkt_hdr {
	u32 size;
	u32 pkt_type;
};

struct hfi_sys_init_pkt {
	struct hfi_pkt_hdr hdr;
	u32 arch_type;
};

struct hfi_sys_set_property_pkt {
	struct hfi_pkt_hdr hdr;
	u32 num_properties;
	u32 data[];
};

struct hfi_sys_get_property_pkt {
	struct hfi_pkt_hdr hdr;
	u32 num_properties;
	u32 data;
};

struct hfi_msg_event_notify_pkt {
	struct hfi_pkt_hdr hdr;
	u32 event_id;
	u32 event_data1;
	u32 event_data2;
	u32 ext_event_data[];
};

struct hfi_msg_sys_init_done_pkt {
	struct hfi_pkt_hdr hdr;
	u32 error_type;
	u32 num_properties;
	u32 data[];
};

struct hfi_msg_sys_property_info_pkt {
	struct hfi_pkt_hdr hdr;
	u32 num_properties;
	u32 property;
	u8 data[];
};

struct hfi_enable {
	u32 enable;
};

struct hfi_msg_sys_debug_pkt {
	struct hfi_pkt_hdr hdr;
	u32 msg_type;
	u32 msg_size;
	u32 time_stamp_hi;
	u32 time_stamp_lo;
	u8 msg_data[];
};

struct hfi_msg_sys_coverage_pkt {
	struct hfi_pkt_hdr hdr;
	u32 msg_size;
	u32 time_stamp_hi;
	u32 time_stamp_lo;
	u8 msg_data[];
};

#endif
