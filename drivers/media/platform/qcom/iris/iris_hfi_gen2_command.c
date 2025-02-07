// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "iris_hfi_gen2.h"
#include "iris_hfi_gen2_packet.h"

#define NUM_SYS_INIT_PACKETS 8

#define SYS_INIT_PKT_SIZE (sizeof(struct iris_hfi_header) + \
	NUM_SYS_INIT_PACKETS * (sizeof(struct iris_hfi_packet) + sizeof(u32)))

#define SYS_IFPC_PKT_SIZE (sizeof(struct iris_hfi_header) + \
	sizeof(struct iris_hfi_packet) + sizeof(u32))

#define SYS_NO_PAYLOAD_PKT_SIZE (sizeof(struct iris_hfi_header) + \
	sizeof(struct iris_hfi_packet))

static int iris_hfi_gen2_sys_init(struct iris_core *core)
{
	struct iris_hfi_header *hdr;
	int ret;

	hdr = kzalloc(SYS_INIT_PKT_SIZE, GFP_KERNEL);
	if (!hdr)
		return -ENOMEM;

	iris_hfi_gen2_packet_sys_init(core, hdr);
	ret = iris_hfi_queue_cmd_write_locked(core, hdr, hdr->size);

	kfree(hdr);

	return ret;
}

static int iris_hfi_gen2_sys_image_version(struct iris_core *core)
{
	struct iris_hfi_header *hdr;
	int ret;

	hdr = kzalloc(SYS_NO_PAYLOAD_PKT_SIZE, GFP_KERNEL);
	if (!hdr)
		return -ENOMEM;

	iris_hfi_gen2_packet_image_version(core, hdr);
	ret = iris_hfi_queue_cmd_write_locked(core, hdr, hdr->size);

	kfree(hdr);

	return ret;
}

static int iris_hfi_gen2_sys_interframe_powercollapse(struct iris_core *core)
{
	struct iris_hfi_header *hdr;
	int ret;

	hdr = kzalloc(SYS_IFPC_PKT_SIZE, GFP_KERNEL);
	if (!hdr)
		return -ENOMEM;

	iris_hfi_gen2_packet_sys_interframe_powercollapse(core, hdr);
	ret = iris_hfi_queue_cmd_write_locked(core, hdr, hdr->size);

	kfree(hdr);

	return ret;
}

static int iris_hfi_gen2_sys_pc_prep(struct iris_core *core)
{
	struct iris_hfi_header *hdr;
	int ret;

	hdr = kzalloc(SYS_NO_PAYLOAD_PKT_SIZE, GFP_KERNEL);
	if (!hdr)
		return -ENOMEM;

	iris_hfi_gen2_packet_sys_pc_prep(core, hdr);
	ret = iris_hfi_queue_cmd_write_locked(core, hdr, hdr->size);

	kfree(hdr);

	return ret;
}

static const struct iris_hfi_command_ops iris_hfi_gen2_command_ops = {
	.sys_init = iris_hfi_gen2_sys_init,
	.sys_image_version = iris_hfi_gen2_sys_image_version,
	.sys_interframe_powercollapse = iris_hfi_gen2_sys_interframe_powercollapse,
	.sys_pc_prep = iris_hfi_gen2_sys_pc_prep,
};

void iris_hfi_gen2_command_ops_init(struct iris_core *core)
{
	core->hfi_ops = &iris_hfi_gen2_command_ops;
}

struct iris_inst *iris_hfi_gen2_get_instance(void)
{
	return kzalloc(sizeof(struct iris_inst_hfi_gen2), GFP_KERNEL);
}
