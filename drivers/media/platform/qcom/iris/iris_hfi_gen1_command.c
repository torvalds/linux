// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "iris_hfi_gen1.h"
#include "iris_hfi_gen1_defines.h"
#include "iris_instance.h"

static int iris_hfi_gen1_sys_init(struct iris_core *core)
{
	struct hfi_sys_init_pkt sys_init_pkt;

	sys_init_pkt.hdr.size = sizeof(sys_init_pkt);
	sys_init_pkt.hdr.pkt_type = HFI_CMD_SYS_INIT;
	sys_init_pkt.arch_type = HFI_VIDEO_ARCH_OX;

	return iris_hfi_queue_cmd_write_locked(core, &sys_init_pkt, sys_init_pkt.hdr.size);
}

static int iris_hfi_gen1_sys_image_version(struct iris_core *core)
{
	struct hfi_sys_get_property_pkt packet;

	packet.hdr.size = sizeof(packet);
	packet.hdr.pkt_type = HFI_CMD_SYS_GET_PROPERTY;
	packet.num_properties = 1;
	packet.data = HFI_PROPERTY_SYS_IMAGE_VERSION;

	return iris_hfi_queue_cmd_write_locked(core, &packet, packet.hdr.size);
}

static int iris_hfi_gen1_sys_interframe_powercollapse(struct iris_core *core)
{
	struct hfi_sys_set_property_pkt *pkt;
	struct hfi_enable *hfi;
	u32 packet_size;
	int ret;

	packet_size = struct_size(pkt, data, 1) + sizeof(*hfi);
	pkt = kzalloc(packet_size, GFP_KERNEL);
	if (!pkt)
		return -ENOMEM;

	hfi = (struct hfi_enable *)&pkt->data[1];

	pkt->hdr.size = packet_size;
	pkt->hdr.pkt_type = HFI_CMD_SYS_SET_PROPERTY;
	pkt->num_properties = 1;
	pkt->data[0] = HFI_PROPERTY_SYS_CODEC_POWER_PLANE_CTRL;
	hfi->enable = true;

	ret = iris_hfi_queue_cmd_write_locked(core, pkt, pkt->hdr.size);
	kfree(pkt);

	return ret;
}

static int iris_hfi_gen1_sys_pc_prep(struct iris_core *core)
{
	struct hfi_sys_pc_prep_pkt pkt;

	pkt.hdr.size = sizeof(struct hfi_sys_pc_prep_pkt);
	pkt.hdr.pkt_type = HFI_CMD_SYS_PC_PREP;

	return iris_hfi_queue_cmd_write_locked(core, &pkt, pkt.hdr.size);
}

static int iris_hfi_gen1_session_open(struct iris_inst *inst)
{
	struct hfi_session_open_pkt packet;
	int ret;

	if (inst->state != IRIS_INST_DEINIT)
		return -EALREADY;

	packet.shdr.hdr.size = sizeof(struct hfi_session_open_pkt);
	packet.shdr.hdr.pkt_type = HFI_CMD_SYS_SESSION_INIT;
	packet.shdr.session_id = inst->session_id;
	packet.session_domain = HFI_SESSION_TYPE_DEC;
	packet.session_codec = HFI_VIDEO_CODEC_H264;

	reinit_completion(&inst->completion);

	ret = iris_hfi_queue_cmd_write(inst->core, &packet, packet.shdr.hdr.size);
	if (ret)
		return ret;

	return iris_wait_for_session_response(inst, false);
}

static void iris_hfi_gen1_packet_session_cmd(struct iris_inst *inst,
					     struct hfi_session_pkt *packet,
					     u32 ptype)
{
	packet->shdr.hdr.size = sizeof(*packet);
	packet->shdr.hdr.pkt_type = ptype;
	packet->shdr.session_id = inst->session_id;
}

static int iris_hfi_gen1_session_close(struct iris_inst *inst)
{
	struct hfi_session_pkt packet;

	iris_hfi_gen1_packet_session_cmd(inst, &packet, HFI_CMD_SYS_SESSION_END);

	return iris_hfi_queue_cmd_write(inst->core, &packet, packet.shdr.hdr.size);
}

static int iris_hfi_gen1_session_start(struct iris_inst *inst, u32 plane)
{
	struct iris_core *core = inst->core;
	struct hfi_session_pkt packet;
	int ret;

	if (!V4L2_TYPE_IS_OUTPUT(plane))
		return 0;

	reinit_completion(&inst->completion);
	iris_hfi_gen1_packet_session_cmd(inst, &packet, HFI_CMD_SESSION_LOAD_RESOURCES);

	ret = iris_hfi_queue_cmd_write(core, &packet, packet.shdr.hdr.size);
	if (ret)
		return ret;

	ret = iris_wait_for_session_response(inst, false);
	if (ret)
		return ret;

	reinit_completion(&inst->completion);
	iris_hfi_gen1_packet_session_cmd(inst, &packet, HFI_CMD_SESSION_START);

	ret = iris_hfi_queue_cmd_write(core, &packet, packet.shdr.hdr.size);
	if (ret)
		return ret;

	return iris_wait_for_session_response(inst, false);
}

static int iris_hfi_gen1_session_stop(struct iris_inst *inst, u32 plane)
{
	struct hfi_session_flush_pkt flush_pkt;
	struct iris_core *core = inst->core;
	struct hfi_session_pkt pkt;
	u32 flush_type = 0;
	int ret = 0;

	if ((V4L2_TYPE_IS_OUTPUT(plane) &&
	     inst->state == IRIS_INST_INPUT_STREAMING) ||
	    (V4L2_TYPE_IS_CAPTURE(plane) &&
	     inst->state == IRIS_INST_OUTPUT_STREAMING) ||
	    inst->state == IRIS_INST_ERROR) {
		reinit_completion(&inst->completion);
		iris_hfi_gen1_packet_session_cmd(inst, &pkt, HFI_CMD_SESSION_STOP);
		ret = iris_hfi_queue_cmd_write(core, &pkt, pkt.shdr.hdr.size);
		if (!ret)
			ret = iris_wait_for_session_response(inst, false);

		reinit_completion(&inst->completion);
		iris_hfi_gen1_packet_session_cmd(inst, &pkt, HFI_CMD_SESSION_RELEASE_RESOURCES);
		ret = iris_hfi_queue_cmd_write(core, &pkt, pkt.shdr.hdr.size);
		if (!ret)
			ret = iris_wait_for_session_response(inst, false);
	} else if (inst->state == IRIS_INST_STREAMING) {
		if (V4L2_TYPE_IS_OUTPUT(plane))
			flush_type = HFI_FLUSH_ALL;
		else if (V4L2_TYPE_IS_CAPTURE(plane))
			flush_type = HFI_FLUSH_OUTPUT;

		reinit_completion(&inst->flush_completion);

		flush_pkt.shdr.hdr.size = sizeof(struct hfi_session_flush_pkt);
		flush_pkt.shdr.hdr.pkt_type = HFI_CMD_SESSION_FLUSH;
		flush_pkt.shdr.session_id = inst->session_id;
		flush_pkt.flush_type = flush_type;

		ret = iris_hfi_queue_cmd_write(core, &flush_pkt, flush_pkt.shdr.hdr.size);
		if (!ret)
			ret = iris_wait_for_session_response(inst, true);
	}

	return ret;
}

static const struct iris_hfi_command_ops iris_hfi_gen1_command_ops = {
	.sys_init = iris_hfi_gen1_sys_init,
	.sys_image_version = iris_hfi_gen1_sys_image_version,
	.sys_interframe_powercollapse = iris_hfi_gen1_sys_interframe_powercollapse,
	.sys_pc_prep = iris_hfi_gen1_sys_pc_prep,
	.session_open = iris_hfi_gen1_session_open,
	.session_start = iris_hfi_gen1_session_start,
	.session_stop = iris_hfi_gen1_session_stop,
	.session_close = iris_hfi_gen1_session_close,
};

void iris_hfi_gen1_command_ops_init(struct iris_core *core)
{
	core->hfi_ops = &iris_hfi_gen1_command_ops;
}

struct iris_inst *iris_hfi_gen1_get_instance(void)
{
	return kzalloc(sizeof(struct iris_inst), GFP_KERNEL);
}
