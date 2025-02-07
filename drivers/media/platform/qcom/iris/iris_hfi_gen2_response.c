// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <media/v4l2-mem2mem.h>

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

static u32 iris_hfi_gen2_buf_type_to_driver(enum hfi_buffer_type buf_type)
{
	switch (buf_type) {
	case HFI_BUFFER_BITSTREAM:
		return BUF_INPUT;
	case HFI_BUFFER_RAW:
		return BUF_OUTPUT;
	case HFI_BUFFER_BIN:
		return BUF_BIN;
	case HFI_BUFFER_ARP:
		return BUF_ARP;
	case HFI_BUFFER_COMV:
		return BUF_COMV;
	case HFI_BUFFER_NON_COMV:
		return BUF_NON_COMV;
	case HFI_BUFFER_LINE:
		return BUF_LINE;
	case HFI_BUFFER_DPB:
		return BUF_DPB;
	case HFI_BUFFER_PERSIST:
		return BUF_PERSIST;
	default:
		return 0;
	}
}

static bool iris_hfi_gen2_is_valid_hfi_buffer_type(u32 buffer_type)
{
	switch (buffer_type) {
	case HFI_BUFFER_BITSTREAM:
	case HFI_BUFFER_RAW:
	case HFI_BUFFER_BIN:
	case HFI_BUFFER_ARP:
	case HFI_BUFFER_COMV:
	case HFI_BUFFER_NON_COMV:
	case HFI_BUFFER_LINE:
	case HFI_BUFFER_DPB:
	case HFI_BUFFER_PERSIST:
	case HFI_BUFFER_VPSS:
		return true;
	default:
		return false;
	}
}

static bool iris_hfi_gen2_is_valid_hfi_port(u32 port, u32 buffer_type)
{
	if (port == HFI_PORT_NONE && buffer_type != HFI_BUFFER_PERSIST)
		return false;

	if (port != HFI_PORT_BITSTREAM && port != HFI_PORT_RAW)
		return false;

	return true;
}

static int iris_hfi_gen2_get_driver_buffer_flags(struct iris_inst *inst, u32 hfi_flags)
{
	u32 keyframe = HFI_PICTURE_IDR | HFI_PICTURE_I | HFI_PICTURE_CRA | HFI_PICTURE_BLA;
	struct iris_inst_hfi_gen2 *inst_hfi_gen2 = to_iris_inst_hfi_gen2(inst);
	u32 driver_flags = 0;

	if (inst_hfi_gen2->hfi_frame_info.picture_type & keyframe)
		driver_flags |= V4L2_BUF_FLAG_KEYFRAME;
	else if (inst_hfi_gen2->hfi_frame_info.picture_type & HFI_PICTURE_P)
		driver_flags |= V4L2_BUF_FLAG_PFRAME;
	else if (inst_hfi_gen2->hfi_frame_info.picture_type & HFI_PICTURE_B)
		driver_flags |= V4L2_BUF_FLAG_BFRAME;

	if (inst_hfi_gen2->hfi_frame_info.data_corrupt || inst_hfi_gen2->hfi_frame_info.overflow)
		driver_flags |= V4L2_BUF_FLAG_ERROR;

	if (hfi_flags & HFI_BUF_FW_FLAG_LAST ||
	    hfi_flags & HFI_BUF_FW_FLAG_PSC_LAST)
		driver_flags |= V4L2_BUF_FLAG_LAST;

	return driver_flags;
}

static bool iris_hfi_gen2_validate_packet_payload(struct iris_hfi_packet *pkt)
{
	u32 payload_size = 0;

	switch (pkt->payload_info) {
	case HFI_PAYLOAD_U32:
	case HFI_PAYLOAD_S32:
	case HFI_PAYLOAD_Q16:
	case HFI_PAYLOAD_U32_ENUM:
	case HFI_PAYLOAD_32_PACKED:
		payload_size = 4;
		break;
	case HFI_PAYLOAD_U64:
	case HFI_PAYLOAD_S64:
	case HFI_PAYLOAD_64_PACKED:
		payload_size = 8;
		break;
	case HFI_PAYLOAD_STRUCTURE:
		if (pkt->type == HFI_CMD_BUFFER)
			payload_size = sizeof(struct iris_hfi_buffer);
		break;
	default:
		payload_size = 0;
		break;
	}

	if (pkt->size < sizeof(struct iris_hfi_packet) + payload_size)
		return false;

	return true;
}

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

static int iris_hfi_gen2_handle_session_info(struct iris_inst *inst,
					     struct iris_hfi_packet *pkt)
{
	struct iris_inst_hfi_gen2 *inst_hfi_gen2 = to_iris_inst_hfi_gen2(inst);
	struct iris_core *core = inst->core;
	int ret = 0;
	char *info;

	switch (pkt->type) {
	case HFI_INFO_UNSUPPORTED:
		info = "unsupported";
		break;
	case HFI_INFO_DATA_CORRUPT:
		info = "data corrupt";
		inst_hfi_gen2->hfi_frame_info.data_corrupt = 1;
		break;
	case HFI_INFO_BUFFER_OVERFLOW:
		info = "buffer overflow";
		inst_hfi_gen2->hfi_frame_info.overflow = 1;
		break;
	default:
		info = "unknown";
		break;
	}

	dev_dbg(core->dev, "session info received %#x: %s\n",
		pkt->type, info);

	return ret;
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
	iris_inst_change_state(inst, IRIS_INST_ERROR);

	return 0;
}

static int iris_hfi_gen2_handle_system_error(struct iris_core *core,
					     struct iris_hfi_packet *pkt)
{
	struct iris_inst *instance;

	dev_err(core->dev, "received system error of type %#x\n", pkt->type);

	core->state = IRIS_CORE_ERROR;

	mutex_lock(&core->lock);
	list_for_each_entry(instance, &core->instances, list)
		iris_inst_change_state(instance, IRIS_INST_ERROR);
	mutex_unlock(&core->lock);

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

static void iris_hfi_gen2_handle_session_close(struct iris_inst *inst,
					       struct iris_hfi_packet *pkt)
{
	if (!(pkt->flags & HFI_FW_FLAGS_SUCCESS)) {
		iris_inst_change_state(inst, IRIS_INST_ERROR);
		return;
	}

	complete(&inst->completion);
}

static int iris_hfi_gen2_handle_input_buffer(struct iris_inst *inst,
					     struct iris_hfi_buffer *buffer)
{
	struct v4l2_m2m_ctx *m2m_ctx = inst->m2m_ctx;
	struct v4l2_m2m_buffer *m2m_buffer, *n;
	struct iris_buffer *buf;
	bool found = false;

	v4l2_m2m_for_each_src_buf_safe(m2m_ctx, m2m_buffer, n) {
		buf = to_iris_buffer(&m2m_buffer->vb);
		if (buf->index == buffer->index) {
			found = true;
			break;
		}
	}
	if (!found)
		return -EINVAL;

	if (!(buf->attr & BUF_ATTR_QUEUED))
		return -EINVAL;

	buf->attr &= ~BUF_ATTR_QUEUED;
	buf->attr |= BUF_ATTR_DEQUEUED;

	buf->flags = iris_hfi_gen2_get_driver_buffer_flags(inst, buffer->flags);

	return 0;
}

static int iris_hfi_gen2_handle_output_buffer(struct iris_inst *inst,
					      struct iris_hfi_buffer *hfi_buffer)
{
	struct v4l2_m2m_ctx *m2m_ctx = inst->m2m_ctx;
	struct v4l2_m2m_buffer *m2m_buffer, *n;
	struct iris_buffer *buf;
	bool found = false;

	v4l2_m2m_for_each_dst_buf_safe(m2m_ctx, m2m_buffer, n) {
		buf = to_iris_buffer(&m2m_buffer->vb);
		if (buf->index == hfi_buffer->index &&
		    buf->device_addr == hfi_buffer->base_address &&
		    buf->data_offset == hfi_buffer->data_offset) {
			found = true;
			break;
		}
	}
	if (!found)
		return -EINVAL;

	if (!(buf->attr & BUF_ATTR_QUEUED))
		return -EINVAL;

	buf->data_offset = hfi_buffer->data_offset;
	buf->data_size = hfi_buffer->data_size;
	buf->timestamp = hfi_buffer->timestamp;

	buf->attr &= ~BUF_ATTR_QUEUED;
	buf->attr |= BUF_ATTR_DEQUEUED;

	buf->flags = iris_hfi_gen2_get_driver_buffer_flags(inst, hfi_buffer->flags);

	return 0;
}

static void iris_hfi_gen2_handle_dequeue_buffers(struct iris_inst *inst)
{
	struct v4l2_m2m_ctx *m2m_ctx = inst->m2m_ctx;
	struct v4l2_m2m_buffer *buffer, *n;
	struct iris_buffer *buf = NULL;

	v4l2_m2m_for_each_src_buf_safe(m2m_ctx, buffer, n) {
		buf = to_iris_buffer(&buffer->vb);
		if (buf->attr & BUF_ATTR_DEQUEUED) {
			buf->attr &= ~BUF_ATTR_DEQUEUED;
			if (!(buf->attr & BUF_ATTR_BUFFER_DONE)) {
				buf->attr |= BUF_ATTR_BUFFER_DONE;
				iris_vb2_buffer_done(inst, buf);
			}
		}
	}

	v4l2_m2m_for_each_dst_buf_safe(m2m_ctx, buffer, n) {
		buf = to_iris_buffer(&buffer->vb);
		if (buf->attr & BUF_ATTR_DEQUEUED) {
			buf->attr &= ~BUF_ATTR_DEQUEUED;
			if (!(buf->attr & BUF_ATTR_BUFFER_DONE)) {
				buf->attr |= BUF_ATTR_BUFFER_DONE;
				iris_vb2_buffer_done(inst, buf);
			}
		}
	}
}

static int iris_hfi_gen2_handle_release_internal_buffer(struct iris_inst *inst,
							struct iris_hfi_buffer *buffer)
{
	u32 buf_type = iris_hfi_gen2_buf_type_to_driver(buffer->type);
	struct iris_buffers *buffers = &inst->buffers[buf_type];
	struct iris_buffer *buf, *iter;
	bool found = false;
	int ret = 0;

	list_for_each_entry(iter, &buffers->list, list) {
		if (iter->device_addr == buffer->base_address) {
			found = true;
			buf = iter;
			break;
		}
	}
	if (!found)
		return -EINVAL;

	buf->attr &= ~BUF_ATTR_QUEUED;
	if (buf->attr & BUF_ATTR_PENDING_RELEASE)
		ret = iris_destroy_internal_buffer(inst, buf);

	return ret;
}

static int iris_hfi_gen2_handle_session_buffer(struct iris_inst *inst,
					       struct iris_hfi_packet *pkt)
{
	struct iris_hfi_buffer *buffer;

	if (pkt->payload_info == HFI_PAYLOAD_NONE)
		return 0;

	if (!iris_hfi_gen2_validate_packet_payload(pkt)) {
		iris_inst_change_state(inst, IRIS_INST_ERROR);
		return 0;
	}

	buffer = (struct iris_hfi_buffer *)((u8 *)pkt + sizeof(*pkt));
	if (!iris_hfi_gen2_is_valid_hfi_buffer_type(buffer->type))
		return 0;

	if (!iris_hfi_gen2_is_valid_hfi_port(pkt->port, buffer->type))
		return 0;

	if (buffer->type == HFI_BUFFER_BITSTREAM)
		return iris_hfi_gen2_handle_input_buffer(inst, buffer);
	else if (buffer->type == HFI_BUFFER_RAW)
		return iris_hfi_gen2_handle_output_buffer(inst, buffer);
	else
		return iris_hfi_gen2_handle_release_internal_buffer(inst, buffer);
}

static int iris_hfi_gen2_handle_session_command(struct iris_inst *inst,
						struct iris_hfi_packet *pkt)
{
	int ret = 0;

	switch (pkt->type) {
	case HFI_CMD_CLOSE:
		iris_hfi_gen2_handle_session_close(inst, pkt);
		break;
	case HFI_CMD_STOP:
		complete(&inst->completion);
		break;
	case HFI_CMD_BUFFER:
		ret = iris_hfi_gen2_handle_session_buffer(inst, pkt);
		break;
	default:
		break;
	}

	return ret;
}

static int iris_hfi_gen2_handle_session_property(struct iris_inst *inst,
						 struct iris_hfi_packet *pkt)
{
	struct iris_inst_hfi_gen2 *inst_hfi_gen2 = to_iris_inst_hfi_gen2(inst);

	if (pkt->port != HFI_PORT_BITSTREAM)
		return 0;

	if (pkt->flags & HFI_FW_FLAGS_INFORMATION)
		return 0;

	switch (pkt->type) {
	case HFI_PROP_BITSTREAM_RESOLUTION:
		inst_hfi_gen2->src_subcr_params.bitstream_resolution = pkt->payload[0];
		break;
	case HFI_PROP_CROP_OFFSETS:
		inst_hfi_gen2->src_subcr_params.crop_offsets[0] = pkt->payload[0];
		inst_hfi_gen2->src_subcr_params.crop_offsets[1] = pkt->payload[1];
		break;
	case HFI_PROP_CODED_FRAMES:
		inst_hfi_gen2->src_subcr_params.coded_frames = pkt->payload[0];
		break;
	case HFI_PROP_BUFFER_FW_MIN_OUTPUT_COUNT:
		inst_hfi_gen2->src_subcr_params.fw_min_count = pkt->payload[0];
		break;
	case HFI_PROP_PIC_ORDER_CNT_TYPE:
		inst_hfi_gen2->src_subcr_params.pic_order_cnt = pkt->payload[0];
		break;
	case HFI_PROP_SIGNAL_COLOR_INFO:
		inst_hfi_gen2->src_subcr_params.color_info = pkt->payload[0];
		break;
	case HFI_PROP_PROFILE:
		inst_hfi_gen2->src_subcr_params.profile = pkt->payload[0];
		break;
	case HFI_PROP_LEVEL:
		inst_hfi_gen2->src_subcr_params.level = pkt->payload[0];
		break;
	case HFI_PROP_PICTURE_TYPE:
		inst_hfi_gen2->hfi_frame_info.picture_type = pkt->payload[0];
		break;
	case HFI_PROP_NO_OUTPUT:
		inst_hfi_gen2->hfi_frame_info.no_output = 1;
		break;
	case HFI_PROP_QUALITY_MODE:
	case HFI_PROP_STAGE:
	case HFI_PROP_PIPE:
	default:
		break;
	}

	return 0;
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
	struct iris_inst_hfi_gen2 *inst_hfi_gen2;
	struct iris_hfi_packet *packet;
	struct iris_inst *inst;
	bool dequeue = false;
	int ret = 0;
	u32 i, j;
	u8 *pkt;
	static const struct iris_hfi_gen2_inst_hfi_range range[] = {
		{HFI_SESSION_ERROR_BEGIN, HFI_SESSION_ERROR_END,
		 iris_hfi_gen2_handle_session_error},
		{HFI_INFORMATION_BEGIN, HFI_INFORMATION_END,
		 iris_hfi_gen2_handle_session_info},
		{HFI_PROP_BEGIN, HFI_PROP_END,
		 iris_hfi_gen2_handle_session_property},
		{HFI_CMD_BEGIN, HFI_CMD_END,
		 iris_hfi_gen2_handle_session_command },
	};

	inst = iris_get_instance(core, hdr->session_id);
	if (!inst)
		return -EINVAL;

	mutex_lock(&inst->lock);
	inst_hfi_gen2 = to_iris_inst_hfi_gen2(inst);
	memset(&inst_hfi_gen2->hfi_frame_info, 0, sizeof(struct iris_hfi_frame_info));

	pkt = (u8 *)((u8 *)hdr + sizeof(*hdr));
	for (i = 0; i < ARRAY_SIZE(range); i++) {
		pkt = (u8 *)((u8 *)hdr + sizeof(*hdr));
		for (j = 0; j < hdr->num_packets; j++) {
			packet = (struct iris_hfi_packet *)pkt;
			if (packet->flags & HFI_FW_FLAGS_SESSION_ERROR)
				iris_hfi_gen2_handle_session_error(inst, packet);

			if (packet->type > range[i].begin && packet->type < range[i].end) {
				dequeue |= (packet->type == HFI_CMD_BUFFER);
				ret = range[i].handle(inst, packet);
				if (ret)
					iris_inst_change_state(inst, IRIS_INST_ERROR);
			}
			pkt += packet->size;
		}
	}

	if (dequeue)
		iris_hfi_gen2_handle_dequeue_buffers(inst);

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
