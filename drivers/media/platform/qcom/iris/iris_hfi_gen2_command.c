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

static u32 iris_hfi_gen2_get_port(u32 plane)
{
	switch (plane) {
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		return HFI_PORT_BITSTREAM;
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		return HFI_PORT_RAW;
	default:
		return HFI_PORT_NONE;
	}
}

static int iris_hfi_gen2_session_set_codec(struct iris_inst *inst)
{
	struct iris_inst_hfi_gen2 *inst_hfi_gen2 = to_iris_inst_hfi_gen2(inst);
	u32 codec = HFI_CODEC_DECODE_AVC;

	iris_hfi_gen2_packet_session_property(inst,
					      HFI_PROP_CODEC,
					      HFI_HOST_FLAGS_NONE,
					      HFI_PORT_NONE,
					      HFI_PAYLOAD_U32_ENUM,
					      &codec,
					      sizeof(u32));

	return iris_hfi_queue_cmd_write(inst->core, inst_hfi_gen2->packet,
					inst_hfi_gen2->packet->size);
}

static int iris_hfi_gen2_session_set_default_header(struct iris_inst *inst)
{
	struct iris_inst_hfi_gen2 *inst_hfi_gen2 = to_iris_inst_hfi_gen2(inst);
	u32 default_header = false;

	iris_hfi_gen2_packet_session_property(inst,
					      HFI_PROP_DEC_DEFAULT_HEADER,
					      HFI_HOST_FLAGS_NONE,
					      HFI_PORT_BITSTREAM,
					      HFI_PAYLOAD_U32,
					      &default_header,
					      sizeof(u32));

	return iris_hfi_queue_cmd_write(inst->core, inst_hfi_gen2->packet,
					inst_hfi_gen2->packet->size);
}

static int iris_hfi_gen2_session_open(struct iris_inst *inst)
{
	struct iris_inst_hfi_gen2 *inst_hfi_gen2 = to_iris_inst_hfi_gen2(inst);
	int ret;

	if (inst->state != IRIS_INST_DEINIT)
		return -EALREADY;

	inst_hfi_gen2->packet = kzalloc(4096, GFP_KERNEL);
	if (!inst_hfi_gen2->packet)
		return -ENOMEM;

	iris_hfi_gen2_packet_session_command(inst,
					     HFI_CMD_OPEN,
					     HFI_HOST_FLAGS_RESPONSE_REQUIRED |
					     HFI_HOST_FLAGS_INTR_REQUIRED,
					     HFI_PORT_NONE,
					     0,
					     HFI_PAYLOAD_U32,
					     &inst->session_id,
					     sizeof(u32));

	ret = iris_hfi_queue_cmd_write(inst->core, inst_hfi_gen2->packet,
				       inst_hfi_gen2->packet->size);
	if (ret)
		goto fail_free_packet;

	ret = iris_hfi_gen2_session_set_codec(inst);
	if (ret)
		goto fail_free_packet;

	ret = iris_hfi_gen2_session_set_default_header(inst);
	if (ret)
		goto fail_free_packet;

	return 0;

fail_free_packet:
	kfree(inst_hfi_gen2->packet);
	inst_hfi_gen2->packet = NULL;

	return ret;
}

static int iris_hfi_gen2_session_close(struct iris_inst *inst)
{
	struct iris_inst_hfi_gen2 *inst_hfi_gen2 = to_iris_inst_hfi_gen2(inst);
	int ret;

	if (!inst_hfi_gen2->packet)
		return -EINVAL;

	iris_hfi_gen2_packet_session_command(inst,
					     HFI_CMD_CLOSE,
					     (HFI_HOST_FLAGS_RESPONSE_REQUIRED |
					     HFI_HOST_FLAGS_INTR_REQUIRED |
					     HFI_HOST_FLAGS_NON_DISCARDABLE),
					     HFI_PORT_NONE,
					     inst->session_id,
					     HFI_PAYLOAD_NONE,
					     NULL,
					     0);

	ret = iris_hfi_queue_cmd_write(inst->core, inst_hfi_gen2->packet,
				       inst_hfi_gen2->packet->size);

	kfree(inst_hfi_gen2->packet);
	inst_hfi_gen2->packet = NULL;

	return ret;
}

static int iris_hfi_gen2_session_start(struct iris_inst *inst, u32 plane)
{
	struct iris_inst_hfi_gen2 *inst_hfi_gen2 = to_iris_inst_hfi_gen2(inst);

	iris_hfi_gen2_packet_session_command(inst,
					     HFI_CMD_START,
					     (HFI_HOST_FLAGS_RESPONSE_REQUIRED |
					     HFI_HOST_FLAGS_INTR_REQUIRED),
					     iris_hfi_gen2_get_port(plane),
					     inst->session_id,
					     HFI_PAYLOAD_NONE,
					     NULL,
					     0);

	return iris_hfi_queue_cmd_write(inst->core, inst_hfi_gen2->packet,
					inst_hfi_gen2->packet->size);
}

static int iris_hfi_gen2_session_stop(struct iris_inst *inst, u32 plane)
{
	struct iris_inst_hfi_gen2 *inst_hfi_gen2 = to_iris_inst_hfi_gen2(inst);
	int ret = 0;

	reinit_completion(&inst->completion);

	iris_hfi_gen2_packet_session_command(inst,
					     HFI_CMD_STOP,
					     (HFI_HOST_FLAGS_RESPONSE_REQUIRED |
					     HFI_HOST_FLAGS_INTR_REQUIRED |
					     HFI_HOST_FLAGS_NON_DISCARDABLE),
					     iris_hfi_gen2_get_port(plane),
					     inst->session_id,
					     HFI_PAYLOAD_NONE,
					     NULL,
					     0);

	ret = iris_hfi_queue_cmd_write(inst->core, inst_hfi_gen2->packet,
				       inst_hfi_gen2->packet->size);
	if (ret)
		return ret;

	return iris_wait_for_session_response(inst, false);
}

static const struct iris_hfi_command_ops iris_hfi_gen2_command_ops = {
	.sys_init = iris_hfi_gen2_sys_init,
	.sys_image_version = iris_hfi_gen2_sys_image_version,
	.sys_interframe_powercollapse = iris_hfi_gen2_sys_interframe_powercollapse,
	.sys_pc_prep = iris_hfi_gen2_sys_pc_prep,
	.session_open = iris_hfi_gen2_session_open,
	.session_start = iris_hfi_gen2_session_start,
	.session_stop = iris_hfi_gen2_session_stop,
	.session_close = iris_hfi_gen2_session_close,
};

void iris_hfi_gen2_command_ops_init(struct iris_core *core)
{
	core->hfi_ops = &iris_hfi_gen2_command_ops;
}

struct iris_inst *iris_hfi_gen2_get_instance(void)
{
	return kzalloc(sizeof(struct iris_inst_hfi_gen2), GFP_KERNEL);
}
