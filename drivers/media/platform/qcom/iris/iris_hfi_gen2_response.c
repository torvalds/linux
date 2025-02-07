// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "iris_hfi_gen2.h"
#include "iris_hfi_gen2_defines.h"
#include "iris_hfi_gen2_packet.h"
#include "iris_vpu_common.h"

struct iris_hfi_gen2_core_hfi_range {
	u32 begin;
	u32 end;
	int (*handle)(struct iris_core *core, struct iris_hfi_packet *pkt);
};

struct iris_hfi_gen2_inst_hfi_range {
	u32 begin;
	u32 end;
	int (*handle)(struct iris_inst *inst, struct iris_hfi_packet *pkt);
};

struct iris_hfi_gen2_packet_handle {
	enum hfi_buffer_type type;
	int (*handle)(struct iris_inst *inst, struct iris_hfi_packet *pkt);
};

static int iris_hfi_gen2_validate_packet(u8 *response_pkt, u8 *core_resp_pkt)
{
	u8 *response_limit = core_resp_pkt + IFACEQ_CORE_PKT_SIZE;
	u32 response_pkt_size = *(u32 *)response_pkt;

	if (!response_pkt_size)
		return -EINVAL;

	if (response_pkt_size < sizeof(struct iris_hfi_packet))
		return -EINVAL;

	if (response_pkt + response_pkt_size > response_limit)
		return -EINVAL;

	return 0;
}

static int iris_hfi_gen2_validate_hdr_packet(struct iris_core *core, struct iris_hfi_header *hdr)
{
	struct iris_hfi_packet *packet;
	int ret;
	u8 *pkt;
	u32 i;

	if (hdr->size < sizeof(*hdr) + sizeof(*packet))
		return -EINVAL;

	pkt = (u8 *)((u8 *)hdr + sizeof(*hdr));

	for (i = 0; i < hdr->num_packets; i++) {
		packet = (struct iris_hfi_packet *)pkt;
		ret = iris_hfi_gen2_validate_packet(pkt, core->response_packet);
		if (ret)
			return ret;

		pkt += packet->size;
	}

	return 0;
}

static int iris_hfi_gen2_handle_session_error(struct iris_inst *inst,
					      struct iris_hfi_packet *pkt)
{
	struct iris_core *core = inst->core;
	char *error;

	switch (pkt->type) {
	case HFI_ERROR_MAX_SESSIONS:
		error = "exceeded max sessions";
		break;
	case HFI_ERROR_UNKNOWN_SESSION:
		error = "unknown session id";
		break;
	case HFI_ERROR_INVALID_STATE:
		error = "invalid operation for current state";
		break;
	case HFI_ERROR_INSUFFICIENT_RESOURCES:
		error = "insufficient resources";
		break;
	case HFI_ERROR_BUFFER_NOT_SET:
		error = "internal buffers not set";
		break;
	case HFI_ERROR_FATAL:
		error = "fatal error";
		break;
	case HFI_ERROR_STREAM_UNSUPPORTED:
		error = "unsupported stream";
		break;
	default:
		error = "unknown";
		break;
	}

	dev_err(core->dev, "session error received %#x: %s\n", pkt->type, error);
	iris_vb2_queue_error(inst);

	return 0;
}

static int iris_hfi_gen2_handle_system_error(struct iris_core *core,
					     struct iris_hfi_packet *pkt)
{
	dev_err(core->dev, "received system error of type %#x\n", pkt->type);

	core->state = IRIS_CORE_ERROR;
	schedule_delayed_work(&core->sys_error_handler, msecs_to_jiffies(10));

	return 0;
}

static int iris_hfi_gen2_handle_system_init(struct iris_core *core,
					    struct iris_hfi_packet *pkt)
{
	if (!(pkt->flags & HFI_FW_FLAGS_SUCCESS)) {
		core->state = IRIS_CORE_ERROR;
		return 0;
	}

	complete(&core->core_init_done);

	return 0;
}

static int iris_hfi_gen2_handle_session_command(struct iris_inst *inst,
						struct iris_hfi_packet *pkt)
{
	int ret = 0;

	switch (pkt->type) {
	case HFI_CMD_CLOSE:
		complete(&inst->completion);
		break;
	default:
		break;
	}

	return ret;
}

static int iris_hfi_gen2_handle_image_version_property(struct iris_core *core,
						       struct iris_hfi_packet *pkt)
{
	u8 *str_image_version = (u8 *)pkt + sizeof(*pkt);
	u32 req_bytes = pkt->size - sizeof(*pkt);
	char fw_version[IRIS_FW_VERSION_LENGTH];
	u32 i;

	if (req_bytes < IRIS_FW_VERSION_LENGTH - 1)
		return -EINVAL;

	for (i = 0; i < IRIS_FW_VERSION_LENGTH - 1; i++) {
		if (str_image_version[i] != '\0')
			fw_version[i] = str_image_version[i];
		else
			fw_version[i] = ' ';
	}
	fw_version[i] = '\0';
	dev_dbg(core->dev, "firmware version: %s\n", fw_version);

	return 0;
}

static int iris_hfi_gen2_handle_system_property(struct iris_core *core,
						struct iris_hfi_packet *pkt)
{
	switch (pkt->type) {
	case HFI_PROP_IMAGE_VERSION:
		return iris_hfi_gen2_handle_image_version_property(core, pkt);
	default:
		return 0;
	}
}

static int iris_hfi_gen2_handle_system_response(struct iris_core *core,
						struct iris_hfi_header *hdr)
{
	u8 *start_pkt = (u8 *)((u8 *)hdr + sizeof(*hdr));
	struct iris_hfi_packet *packet;
	u32 i, j;
	u8 *pkt;
	int ret;
	static const struct iris_hfi_gen2_core_hfi_range range[] = {
		{HFI_SYSTEM_ERROR_BEGIN, HFI_SYSTEM_ERROR_END, iris_hfi_gen2_handle_system_error },
		{HFI_PROP_BEGIN,         HFI_PROP_END, iris_hfi_gen2_handle_system_property },
		{HFI_CMD_BEGIN,          HFI_CMD_END, iris_hfi_gen2_handle_system_init },
	};

	for (i = 0; i < ARRAY_SIZE(range); i++) {
		pkt = start_pkt;
		for (j = 0; j < hdr->num_packets; j++) {
			packet = (struct iris_hfi_packet *)pkt;
			if (packet->flags & HFI_FW_FLAGS_SYSTEM_ERROR) {
				ret = iris_hfi_gen2_handle_system_error(core, packet);
				return ret;
			}

			if (packet->type > range[i].begin && packet->type < range[i].end) {
				ret = range[i].handle(core, packet);
				if (ret)
					return ret;

				if (packet->type >  HFI_SYSTEM_ERROR_BEGIN &&
				    packet->type < HFI_SYSTEM_ERROR_END)
					return 0;
			}
			pkt += packet->size;
		}
	}

	return 0;
}

static int iris_hfi_gen2_handle_session_response(struct iris_core *core,
						 struct iris_hfi_header *hdr)
{
	struct iris_hfi_packet *packet;
	struct iris_inst *inst;
	int ret = 0;
	u32 i, j;
	u8 *pkt;
	static const struct iris_hfi_gen2_inst_hfi_range range[] = {
		{HFI_SESSION_ERROR_BEGIN, HFI_SESSION_ERROR_END,
		 iris_hfi_gen2_handle_session_error},
		{HFI_CMD_BEGIN, HFI_CMD_END,
		 iris_hfi_gen2_handle_session_command },
	};

	inst = iris_get_instance(core, hdr->session_id);
	if (!inst)
		return -EINVAL;

	mutex_lock(&inst->lock);

	pkt = (u8 *)((u8 *)hdr + sizeof(*hdr));
	for (i = 0; i < ARRAY_SIZE(range); i++) {
		pkt = (u8 *)((u8 *)hdr + sizeof(*hdr));
		for (j = 0; j < hdr->num_packets; j++) {
			packet = (struct iris_hfi_packet *)pkt;
			if (packet->flags & HFI_FW_FLAGS_SESSION_ERROR)
				iris_hfi_gen2_handle_session_error(inst, packet);

			if (packet->type > range[i].begin && packet->type < range[i].end)
				ret = range[i].handle(inst, packet);
			pkt += packet->size;
		}
	}

	mutex_unlock(&inst->lock);

	return ret;
}

static int iris_hfi_gen2_handle_response(struct iris_core *core, void *response)
{
	struct iris_hfi_header *hdr = (struct iris_hfi_header *)response;
	int ret;

	ret = iris_hfi_gen2_validate_hdr_packet(core, hdr);
	if (ret)
		return iris_hfi_gen2_handle_system_error(core, NULL);

	if (!hdr->session_id)
		return iris_hfi_gen2_handle_system_response(core, hdr);
	else
		return iris_hfi_gen2_handle_session_response(core, hdr);
}

static void iris_hfi_gen2_flush_debug_queue(struct iris_core *core, u8 *packet)
{
	struct hfi_debug_header *pkt;
	u8 *log;

	while (!iris_hfi_queue_dbg_read(core, packet)) {
		pkt = (struct hfi_debug_header *)packet;

		if (pkt->size < sizeof(*pkt))
			continue;

		if (pkt->size >= IFACEQ_CORE_PKT_SIZE)
			continue;

		packet[pkt->size] = '\0';
		log = (u8 *)packet + sizeof(*pkt) + 1;
		dev_dbg(core->dev, "%s", log);
	}
}

static void iris_hfi_gen2_response_handler(struct iris_core *core)
{
	if (iris_vpu_watchdog(core, core->intr_status)) {
		struct iris_hfi_packet pkt = {.type = HFI_SYS_ERROR_WD_TIMEOUT};

		dev_err(core->dev, "cpu watchdog error received\n");
		core->state = IRIS_CORE_ERROR;
		iris_hfi_gen2_handle_system_error(core, &pkt);

		return;
	}

	memset(core->response_packet, 0, sizeof(struct iris_hfi_header));
	while (!iris_hfi_queue_msg_read(core, core->response_packet)) {
		iris_hfi_gen2_handle_response(core, core->response_packet);
		memset(core->response_packet, 0, sizeof(struct iris_hfi_header));
	}

	iris_hfi_gen2_flush_debug_queue(core, core->response_packet);
}

static const struct iris_hfi_response_ops iris_hfi_gen2_response_ops = {
	.hfi_response_handler = iris_hfi_gen2_response_handler,
};

void iris_hfi_gen2_response_ops_init(struct iris_core *core)
{
	core->hfi_response_ops = &iris_hfi_gen2_response_ops;
}
