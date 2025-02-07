/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __IRIS_HFI_COMMON_H__
#define __IRIS_HFI_COMMON_H__

#include <linux/types.h>
#include <media/v4l2-device.h>

struct iris_core;

enum hfi_packet_port_type {
	HFI_PORT_NONE		= 0x00000000,
	HFI_PORT_BITSTREAM	= 0x00000001,
	HFI_PORT_RAW		= 0x00000002,
};

enum hfi_packet_payload_info {
	HFI_PAYLOAD_NONE	= 0x00000000,
	HFI_PAYLOAD_U32		= 0x00000001,
	HFI_PAYLOAD_S32		= 0x00000002,
	HFI_PAYLOAD_U64		= 0x00000003,
	HFI_PAYLOAD_S64		= 0x00000004,
	HFI_PAYLOAD_STRUCTURE	= 0x00000005,
	HFI_PAYLOAD_BLOB	= 0x00000006,
	HFI_PAYLOAD_STRING	= 0x00000007,
	HFI_PAYLOAD_Q16		= 0x00000008,
	HFI_PAYLOAD_U32_ENUM	= 0x00000009,
	HFI_PAYLOAD_32_PACKED	= 0x0000000a,
	HFI_PAYLOAD_U32_ARRAY	= 0x0000000b,
	HFI_PAYLOAD_S32_ARRAY	= 0x0000000c,
	HFI_PAYLOAD_64_PACKED	= 0x0000000d,
};

enum hfi_packet_host_flags {
	HFI_HOST_FLAGS_NONE			= 0x00000000,
	HFI_HOST_FLAGS_INTR_REQUIRED		= 0x00000001,
	HFI_HOST_FLAGS_RESPONSE_REQUIRED	= 0x00000002,
	HFI_HOST_FLAGS_NON_DISCARDABLE		= 0x00000004,
	HFI_HOST_FLAGS_GET_PROPERTY		= 0x00000008,
};

struct iris_hfi_command_ops {
	int (*sys_init)(struct iris_core *core);
	int (*sys_image_version)(struct iris_core *core);
	int (*sys_interframe_powercollapse)(struct iris_core *core);
};

struct iris_hfi_response_ops {
	void (*hfi_response_handler)(struct iris_core *core);
};

int iris_hfi_core_init(struct iris_core *core);

irqreturn_t iris_hfi_isr(int irq, void *data);
irqreturn_t iris_hfi_isr_handler(int irq, void *data);

#endif
