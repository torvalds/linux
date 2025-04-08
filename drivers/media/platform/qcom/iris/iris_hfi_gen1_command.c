// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "iris_hfi_gen1.h"
#include "iris_hfi_gen1_defines.h"
#include "iris_instance.h"
#include "iris_vpu_buffer.h"

static u32 iris_hfi_gen1_buf_type_from_driver(enum iris_buffer_type buffer_type)
{
	switch (buffer_type) {
	case BUF_INPUT:
		return HFI_BUFFER_INPUT;
	case BUF_OUTPUT:
		return HFI_BUFFER_OUTPUT;
	case BUF_PERSIST:
		return HFI_BUFFER_INTERNAL_PERSIST_1;
	case BUF_BIN:
		return HFI_BUFFER_INTERNAL_SCRATCH;
	case BUF_SCRATCH_1:
		return HFI_BUFFER_INTERNAL_SCRATCH_1;
	default:
		return -EINVAL;
	}
}

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

	if (inst->sub_state & IRIS_INST_SUB_LOAD_RESOURCES)
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

	ret = iris_wait_for_session_response(inst, false);
	if (ret)
		return ret;

	return iris_inst_change_sub_state(inst, 0, IRIS_INST_SUB_LOAD_RESOURCES);
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

		iris_inst_change_sub_state(inst, IRIS_INST_SUB_LOAD_RESOURCES, 0);

		iris_helper_buffers_done(inst, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
					 VB2_BUF_STATE_ERROR);
		iris_helper_buffers_done(inst, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
					 VB2_BUF_STATE_ERROR);
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

static int iris_hfi_gen1_session_continue(struct iris_inst *inst, u32 plane)
{
	struct hfi_session_pkt packet;

	iris_hfi_gen1_packet_session_cmd(inst, &packet, HFI_CMD_SESSION_CONTINUE);

	return iris_hfi_queue_cmd_write(inst->core, &packet, packet.shdr.hdr.size);
}

static int iris_hfi_gen1_queue_input_buffer(struct iris_inst *inst, struct iris_buffer *buf)
{
	struct hfi_session_empty_buffer_compressed_pkt ip_pkt;

	ip_pkt.shdr.hdr.size = sizeof(struct hfi_session_empty_buffer_compressed_pkt);
	ip_pkt.shdr.hdr.pkt_type = HFI_CMD_SESSION_EMPTY_BUFFER;
	ip_pkt.shdr.session_id = inst->session_id;
	ip_pkt.time_stamp_hi = upper_32_bits(buf->timestamp);
	ip_pkt.time_stamp_lo = lower_32_bits(buf->timestamp);
	ip_pkt.flags = buf->flags;
	ip_pkt.mark_target = 0;
	ip_pkt.mark_data = 0;
	ip_pkt.offset = buf->data_offset;
	ip_pkt.alloc_len = buf->buffer_size;
	ip_pkt.filled_len = buf->data_size;
	ip_pkt.input_tag = buf->index;
	ip_pkt.packet_buffer = buf->device_addr;

	return iris_hfi_queue_cmd_write(inst->core, &ip_pkt, ip_pkt.shdr.hdr.size);
}

static int iris_hfi_gen1_queue_output_buffer(struct iris_inst *inst, struct iris_buffer *buf)
{
	struct hfi_session_fill_buffer_pkt op_pkt;

	op_pkt.shdr.hdr.size = sizeof(struct hfi_session_fill_buffer_pkt);
	op_pkt.shdr.hdr.pkt_type = HFI_CMD_SESSION_FILL_BUFFER;
	op_pkt.shdr.session_id = inst->session_id;
	op_pkt.output_tag = buf->index;
	op_pkt.packet_buffer = buf->device_addr;
	op_pkt.extradata_buffer = 0;
	op_pkt.alloc_len = buf->buffer_size;
	op_pkt.filled_len = buf->data_size;
	op_pkt.offset = buf->data_offset;
	op_pkt.data = 0;

	if (buf->type == BUF_OUTPUT && iris_split_mode_enabled(inst))
		op_pkt.stream_id = 1;
	else
		op_pkt.stream_id = 0;

	return iris_hfi_queue_cmd_write(inst->core, &op_pkt, op_pkt.shdr.hdr.size);
}

static int iris_hfi_gen1_queue_internal_buffer(struct iris_inst *inst, struct iris_buffer *buf)
{
	struct hfi_session_set_buffers_pkt *int_pkt;
	u32 buffer_type, i;
	u32 packet_size;
	int ret;

	packet_size = struct_size(int_pkt, buffer_info, 1);
	int_pkt = kzalloc(packet_size, GFP_KERNEL);
	if (!int_pkt)
		return -ENOMEM;

	int_pkt->shdr.hdr.pkt_type = HFI_CMD_SESSION_SET_BUFFERS;
	int_pkt->shdr.session_id = inst->session_id;
	int_pkt->buffer_size = buf->buffer_size;
	int_pkt->min_buffer_size = buf->buffer_size;
	int_pkt->num_buffers = 1;
	int_pkt->extradata_size = 0;
	int_pkt->shdr.hdr.size = packet_size;
	for (i = 0; i < int_pkt->num_buffers; i++)
		int_pkt->buffer_info[i] = buf->device_addr;
	buffer_type = iris_hfi_gen1_buf_type_from_driver(buf->type);
	if (buffer_type == -EINVAL) {
		ret = -EINVAL;
		goto exit;
	}

	int_pkt->buffer_type = buffer_type;
	ret = iris_hfi_queue_cmd_write(inst->core, int_pkt, int_pkt->shdr.hdr.size);

exit:
	kfree(int_pkt);

	return ret;
}

static int iris_hfi_gen1_session_queue_buffer(struct iris_inst *inst, struct iris_buffer *buf)
{
	switch (buf->type) {
	case BUF_INPUT:
		return iris_hfi_gen1_queue_input_buffer(inst, buf);
	case BUF_OUTPUT:
	case BUF_DPB:
		return iris_hfi_gen1_queue_output_buffer(inst, buf);
	case BUF_PERSIST:
	case BUF_BIN:
	case BUF_SCRATCH_1:
		return iris_hfi_gen1_queue_internal_buffer(inst, buf);
	default:
		return -EINVAL;
	}
}

static int iris_hfi_gen1_session_unset_buffers(struct iris_inst *inst, struct iris_buffer *buf)
{
	struct hfi_session_release_buffer_pkt *pkt;
	u32 packet_size, buffer_type, i;
	int ret;

	buffer_type = iris_hfi_gen1_buf_type_from_driver(buf->type);
	if (buffer_type == -EINVAL)
		return -EINVAL;

	if (buffer_type == HFI_BUFFER_INPUT)
		return 0;

	packet_size = sizeof(*pkt) + sizeof(struct hfi_buffer_info);
	pkt = kzalloc(packet_size, GFP_KERNEL);
	if (!pkt)
		return -ENOMEM;

	pkt->shdr.hdr.pkt_type = HFI_CMD_SESSION_RELEASE_BUFFERS;
	pkt->shdr.session_id = inst->session_id;
	pkt->buffer_size = buf->buffer_size;
	pkt->num_buffers = 1;

	if (buffer_type == HFI_BUFFER_OUTPUT ||
	    buffer_type == HFI_BUFFER_OUTPUT2) {
		struct hfi_buffer_info *bi;

		bi = (struct hfi_buffer_info *)pkt->buffer_info;
		for (i = 0; i < pkt->num_buffers; i++) {
			bi->buffer_addr = buf->device_addr;
			bi->extradata_addr = 0;
		}
		pkt->shdr.hdr.size = packet_size;
	} else {
		for (i = 0; i < pkt->num_buffers; i++)
			pkt->buffer_info[i] = buf->device_addr;
		pkt->extradata_size = 0;
		pkt->shdr.hdr.size =
				sizeof(struct hfi_session_set_buffers_pkt) +
				((pkt->num_buffers) * sizeof(u32));
	}

	pkt->response_req = true;
	pkt->buffer_type = buffer_type;

	ret = iris_hfi_queue_cmd_write(inst->core, pkt, pkt->shdr.hdr.size);
	if (ret)
		goto exit;

	ret = iris_wait_for_session_response(inst, false);

exit:
	kfree(pkt);

	return ret;
}

static int iris_hfi_gen1_session_drain(struct iris_inst *inst, u32 plane)
{
	struct hfi_session_empty_buffer_compressed_pkt ip_pkt = {0};

	ip_pkt.shdr.hdr.size = sizeof(struct hfi_session_empty_buffer_compressed_pkt);
	ip_pkt.shdr.hdr.pkt_type = HFI_CMD_SESSION_EMPTY_BUFFER;
	ip_pkt.shdr.session_id = inst->session_id;
	ip_pkt.flags = HFI_BUFFERFLAG_EOS;

	return iris_hfi_queue_cmd_write(inst->core, &ip_pkt, ip_pkt.shdr.hdr.size);
}

static int
iris_hfi_gen1_packet_session_set_property(struct hfi_session_set_property_pkt *packet,
					  struct iris_inst *inst, u32 ptype, void *pdata)
{
	void *prop_data = &packet->data[1];

	packet->shdr.hdr.size = sizeof(*packet);
	packet->shdr.hdr.pkt_type = HFI_CMD_SESSION_SET_PROPERTY;
	packet->shdr.session_id = inst->session_id;
	packet->num_properties = 1;
	packet->data[0] = ptype;

	switch (ptype) {
	case HFI_PROPERTY_PARAM_FRAME_SIZE: {
		struct hfi_framesize *in = pdata, *fsize = prop_data;

		fsize->buffer_type = in->buffer_type;
		fsize->height = in->height;
		fsize->width = in->width;
		packet->shdr.hdr.size += sizeof(u32) + sizeof(*fsize);
		break;
	}
	case HFI_PROPERTY_CONFIG_VIDEOCORES_USAGE: {
		struct hfi_videocores_usage_type *in = pdata, *cu = prop_data;

		cu->video_core_enable_mask = in->video_core_enable_mask;
		packet->shdr.hdr.size += sizeof(u32) + sizeof(*cu);
		break;
	}
	case HFI_PROPERTY_PARAM_UNCOMPRESSED_FORMAT_SELECT: {
		struct hfi_uncompressed_format_select *in = pdata;
		struct hfi_uncompressed_format_select *hfi = prop_data;

		hfi->buffer_type = in->buffer_type;
		hfi->format = in->format;
		packet->shdr.hdr.size += sizeof(u32) + sizeof(*hfi);
		break;
	}
	case HFI_PROPERTY_PARAM_UNCOMPRESSED_PLANE_ACTUAL_CONSTRAINTS_INFO: {
		struct hfi_uncompressed_plane_actual_constraints_info *info = prop_data;

		info->buffer_type = HFI_BUFFER_OUTPUT2;
		info->num_planes = 2;
		info->plane_format[0].stride_multiples = 128;
		info->plane_format[0].max_stride = 8192;
		info->plane_format[0].min_plane_buffer_height_multiple = 32;
		info->plane_format[0].buffer_alignment = 256;
		if (info->num_planes > 1) {
			info->plane_format[1].stride_multiples = 128;
			info->plane_format[1].max_stride = 8192;
			info->plane_format[1].min_plane_buffer_height_multiple = 16;
			info->plane_format[1].buffer_alignment = 256;
		}

		packet->shdr.hdr.size += sizeof(u32) + sizeof(*info);
		break;
	}
	case HFI_PROPERTY_PARAM_BUFFER_COUNT_ACTUAL: {
		struct hfi_buffer_count_actual *in = pdata;
		struct hfi_buffer_count_actual *count = prop_data;

		count->type = in->type;
		count->count_actual = in->count_actual;
		count->count_min_host = in->count_min_host;
		packet->shdr.hdr.size += sizeof(u32) + sizeof(*count);
		break;
	}
	case HFI_PROPERTY_PARAM_VDEC_MULTI_STREAM: {
		struct hfi_multi_stream *in = pdata;
		struct hfi_multi_stream *multi = prop_data;

		multi->buffer_type = in->buffer_type;
		multi->enable = in->enable;
		packet->shdr.hdr.size += sizeof(u32) + sizeof(*multi);
		break;
	}
	case HFI_PROPERTY_PARAM_BUFFER_SIZE_ACTUAL: {
		struct hfi_buffer_size_actual *in = pdata, *sz = prop_data;

		sz->size = in->size;
		sz->type = in->type;
		packet->shdr.hdr.size += sizeof(u32) + sizeof(*sz);
		break;
	}
	case HFI_PROPERTY_PARAM_WORK_ROUTE: {
		struct hfi_video_work_route *wr = prop_data;
		u32 *in = pdata;

		wr->video_work_route = *in;
		packet->shdr.hdr.size += sizeof(u32) + sizeof(*wr);
		break;
	}
	case HFI_PROPERTY_PARAM_WORK_MODE: {
		struct hfi_video_work_mode *wm = prop_data;
		u32 *in = pdata;

		wm->video_work_mode = *in;
		packet->shdr.hdr.size += sizeof(u32) + sizeof(*wm);
		break;
	}
	case HFI_PROPERTY_CONFIG_VDEC_POST_LOOP_DEBLOCKER: {
		struct hfi_enable *en = prop_data;
		u32 *in = pdata;

		en->enable = *in;
		packet->shdr.hdr.size += sizeof(u32) + sizeof(*en);
		break;
	}
	default:
		return -EINVAL;
	}

	return 0;
}

static int hfi_gen1_set_property(struct iris_inst *inst, u32 packet_type,
				 void *payload, u32 payload_size)
{
	struct hfi_session_set_property_pkt *pkt;
	u32 packet_size;
	int ret;

	packet_size = sizeof(*pkt) + sizeof(u32) + payload_size;
	pkt = kzalloc(packet_size, GFP_KERNEL);
	if (!pkt)
		return -ENOMEM;

	ret = iris_hfi_gen1_packet_session_set_property(pkt, inst, packet_type, payload);
	if (ret == -EOPNOTSUPP) {
		ret = 0;
		goto exit;
	}
	if (ret)
		goto exit;

	ret = iris_hfi_queue_cmd_write(inst->core, pkt, pkt->shdr.hdr.size);

exit:
	kfree(pkt);

	return ret;
}

static int iris_hfi_gen1_session_set_property(struct iris_inst *inst, u32 packet_type,
					      u32 flag, u32 plane, u32 payload_type,
					      void *payload, u32 payload_size)
{
	return hfi_gen1_set_property(inst, packet_type, payload, payload_size);
}

static int iris_hfi_gen1_set_resolution(struct iris_inst *inst)
{
	u32 ptype = HFI_PROPERTY_PARAM_FRAME_SIZE;
	struct hfi_framesize fs;
	int ret;

	fs.buffer_type = HFI_BUFFER_INPUT;
	fs.width = inst->fmt_src->fmt.pix_mp.width;
	fs.height = inst->fmt_src->fmt.pix_mp.height;

	ret = hfi_gen1_set_property(inst, ptype, &fs, sizeof(fs));
	if (ret)
		return ret;

	fs.buffer_type = HFI_BUFFER_OUTPUT2;
	fs.width = inst->fmt_dst->fmt.pix_mp.width;
	fs.height = inst->fmt_dst->fmt.pix_mp.height;

	return hfi_gen1_set_property(inst, ptype, &fs, sizeof(fs));
}

static int iris_hfi_gen1_decide_core(struct iris_inst *inst)
{
	const u32 ptype = HFI_PROPERTY_CONFIG_VIDEOCORES_USAGE;
	struct hfi_videocores_usage_type cu;

	cu.video_core_enable_mask = HFI_CORE_ID_1;

	return hfi_gen1_set_property(inst, ptype, &cu, sizeof(cu));
}

static int iris_hfi_gen1_set_raw_format(struct iris_inst *inst)
{
	const u32 ptype = HFI_PROPERTY_PARAM_UNCOMPRESSED_FORMAT_SELECT;
	u32 pixelformat = inst->fmt_dst->fmt.pix_mp.pixelformat;
	struct hfi_uncompressed_format_select fmt;
	int ret;

	if (iris_split_mode_enabled(inst)) {
		fmt.buffer_type = HFI_BUFFER_OUTPUT;
		fmt.format = pixelformat == V4L2_PIX_FMT_NV12 ? HFI_COLOR_FORMAT_NV12_UBWC : 0;

		ret = hfi_gen1_set_property(inst, ptype, &fmt, sizeof(fmt));
		if (ret)
			return ret;

		fmt.buffer_type = HFI_BUFFER_OUTPUT2;
		fmt.format = pixelformat == V4L2_PIX_FMT_NV12 ? HFI_COLOR_FORMAT_NV12 : 0;

		ret = hfi_gen1_set_property(inst, ptype, &fmt, sizeof(fmt));
	} else {
		fmt.buffer_type = HFI_BUFFER_OUTPUT;
		fmt.format = pixelformat == V4L2_PIX_FMT_NV12 ? HFI_COLOR_FORMAT_NV12 : 0;

		ret = hfi_gen1_set_property(inst, ptype, &fmt, sizeof(fmt));
	}

	return ret;
}

static int iris_hfi_gen1_set_format_constraints(struct iris_inst *inst)
{
	const u32 ptype = HFI_PROPERTY_PARAM_UNCOMPRESSED_PLANE_ACTUAL_CONSTRAINTS_INFO;
	struct hfi_uncompressed_plane_actual_constraints_info pconstraint;

	pconstraint.buffer_type = HFI_BUFFER_OUTPUT2;
	pconstraint.num_planes = 2;
	pconstraint.plane_format[0].stride_multiples = 128;
	pconstraint.plane_format[0].max_stride = 8192;
	pconstraint.plane_format[0].min_plane_buffer_height_multiple = 32;
	pconstraint.plane_format[0].buffer_alignment = 256;

	pconstraint.plane_format[1].stride_multiples = 128;
	pconstraint.plane_format[1].max_stride = 8192;
	pconstraint.plane_format[1].min_plane_buffer_height_multiple = 16;
	pconstraint.plane_format[1].buffer_alignment = 256;

	return hfi_gen1_set_property(inst, ptype, &pconstraint, sizeof(pconstraint));
}

static int iris_hfi_gen1_set_num_bufs(struct iris_inst *inst)
{
	u32 ptype = HFI_PROPERTY_PARAM_BUFFER_COUNT_ACTUAL;
	struct hfi_buffer_count_actual buf_count;
	int ret;

	buf_count.type = HFI_BUFFER_INPUT;
	buf_count.count_actual = VIDEO_MAX_FRAME;
	buf_count.count_min_host = VIDEO_MAX_FRAME;

	ret = hfi_gen1_set_property(inst, ptype, &buf_count, sizeof(buf_count));
	if (ret)
		return ret;

	if (iris_split_mode_enabled(inst)) {
		buf_count.type = HFI_BUFFER_OUTPUT;
		buf_count.count_actual = VIDEO_MAX_FRAME;
		buf_count.count_min_host = VIDEO_MAX_FRAME;

		ret = hfi_gen1_set_property(inst, ptype, &buf_count, sizeof(buf_count));
		if (ret)
			return ret;

		buf_count.type = HFI_BUFFER_OUTPUT2;
		buf_count.count_actual = iris_vpu_buf_count(inst, BUF_DPB);
		buf_count.count_min_host = iris_vpu_buf_count(inst, BUF_DPB);

		ret = hfi_gen1_set_property(inst, ptype, &buf_count, sizeof(buf_count));
	} else {
		buf_count.type = HFI_BUFFER_OUTPUT;
		buf_count.count_actual = VIDEO_MAX_FRAME;
		buf_count.count_min_host = VIDEO_MAX_FRAME;

		ret = hfi_gen1_set_property(inst, ptype, &buf_count, sizeof(buf_count));
	}

	return ret;
}

static int iris_hfi_gen1_set_multistream(struct iris_inst *inst)
{
	u32 ptype = HFI_PROPERTY_PARAM_VDEC_MULTI_STREAM;
	struct hfi_multi_stream multi = {0};
	int ret;

	if (iris_split_mode_enabled(inst)) {
		multi.buffer_type = HFI_BUFFER_OUTPUT;
		multi.enable = 0;

		ret = hfi_gen1_set_property(inst, ptype, &multi, sizeof(multi));
		if (ret)
			return ret;

		multi.buffer_type = HFI_BUFFER_OUTPUT2;
		multi.enable = 1;

		ret = hfi_gen1_set_property(inst, ptype, &multi, sizeof(multi));
	} else {
		multi.buffer_type = HFI_BUFFER_OUTPUT;
		multi.enable = 1;

		ret = hfi_gen1_set_property(inst, ptype, &multi, sizeof(multi));
		if (ret)
			return ret;

		multi.buffer_type = HFI_BUFFER_OUTPUT2;
		multi.enable = 0;

		ret = hfi_gen1_set_property(inst, ptype, &multi, sizeof(multi));
	}

	return ret;
}

static int iris_hfi_gen1_set_bufsize(struct iris_inst *inst)
{
	const u32 ptype = HFI_PROPERTY_PARAM_BUFFER_SIZE_ACTUAL;
	struct hfi_buffer_size_actual bufsz;
	int ret;

	if (iris_split_mode_enabled(inst)) {
		bufsz.type = HFI_BUFFER_OUTPUT;
		bufsz.size = iris_vpu_buf_size(inst, BUF_DPB);

		ret = hfi_gen1_set_property(inst, ptype, &bufsz, sizeof(bufsz));
		if (ret)
			return ret;

		bufsz.type = HFI_BUFFER_OUTPUT2;
		bufsz.size = inst->buffers[BUF_OUTPUT].size;

		ret = hfi_gen1_set_property(inst, ptype, &bufsz, sizeof(bufsz));
	} else {
		bufsz.type = HFI_BUFFER_OUTPUT;
		bufsz.size = inst->buffers[BUF_OUTPUT].size;

		ret = hfi_gen1_set_property(inst, ptype, &bufsz, sizeof(bufsz));
		if (ret)
			return ret;

		bufsz.type = HFI_BUFFER_OUTPUT2;
		bufsz.size = 0;

		ret = hfi_gen1_set_property(inst, ptype, &bufsz, sizeof(bufsz));
	}

	return ret;
}

static int iris_hfi_gen1_session_set_config_params(struct iris_inst *inst, u32 plane)
{
	struct iris_core *core = inst->core;
	u32 config_params_size, i, j;
	const u32 *config_params;
	int ret;

	static const struct iris_hfi_prop_type_handle prop_type_handle_inp_arr[] = {
		{HFI_PROPERTY_PARAM_FRAME_SIZE,
			iris_hfi_gen1_set_resolution},
		{HFI_PROPERTY_CONFIG_VIDEOCORES_USAGE,
			iris_hfi_gen1_decide_core},
		{HFI_PROPERTY_PARAM_UNCOMPRESSED_FORMAT_SELECT,
			iris_hfi_gen1_set_raw_format},
		{HFI_PROPERTY_PARAM_UNCOMPRESSED_PLANE_ACTUAL_CONSTRAINTS_INFO,
			iris_hfi_gen1_set_format_constraints},
		{HFI_PROPERTY_PARAM_BUFFER_COUNT_ACTUAL,
			iris_hfi_gen1_set_num_bufs},
		{HFI_PROPERTY_PARAM_VDEC_MULTI_STREAM,
			iris_hfi_gen1_set_multistream},
		{HFI_PROPERTY_PARAM_BUFFER_SIZE_ACTUAL,
			iris_hfi_gen1_set_bufsize},
	};

	static const struct iris_hfi_prop_type_handle prop_type_handle_out_arr[] = {
		{HFI_PROPERTY_PARAM_FRAME_SIZE,
			iris_hfi_gen1_set_resolution},
		{HFI_PROPERTY_PARAM_UNCOMPRESSED_FORMAT_SELECT,
			iris_hfi_gen1_set_raw_format},
		{HFI_PROPERTY_PARAM_UNCOMPRESSED_PLANE_ACTUAL_CONSTRAINTS_INFO,
			iris_hfi_gen1_set_format_constraints},
		{HFI_PROPERTY_PARAM_BUFFER_COUNT_ACTUAL,
			iris_hfi_gen1_set_num_bufs},
		{HFI_PROPERTY_PARAM_VDEC_MULTI_STREAM,
			iris_hfi_gen1_set_multistream},
		{HFI_PROPERTY_PARAM_BUFFER_SIZE_ACTUAL,
			iris_hfi_gen1_set_bufsize},
	};

	config_params = core->iris_platform_data->input_config_params;
	config_params_size = core->iris_platform_data->input_config_params_size;

	if (V4L2_TYPE_IS_OUTPUT(plane)) {
		for (i = 0; i < config_params_size; i++) {
			for (j = 0; j < ARRAY_SIZE(prop_type_handle_inp_arr); j++) {
				if (prop_type_handle_inp_arr[j].type == config_params[i]) {
					ret = prop_type_handle_inp_arr[j].handle(inst);
					if (ret)
						return ret;
					break;
				}
			}
		}
	} else if (V4L2_TYPE_IS_CAPTURE(plane)) {
		for (i = 0; i < config_params_size; i++) {
			for (j = 0; j < ARRAY_SIZE(prop_type_handle_out_arr); j++) {
				if (prop_type_handle_out_arr[j].type == config_params[i]) {
					ret = prop_type_handle_out_arr[j].handle(inst);
					if (ret)
						return ret;
					break;
				}
			}
		}
	}

	return 0;
}

static const struct iris_hfi_command_ops iris_hfi_gen1_command_ops = {
	.sys_init = iris_hfi_gen1_sys_init,
	.sys_image_version = iris_hfi_gen1_sys_image_version,
	.sys_interframe_powercollapse = iris_hfi_gen1_sys_interframe_powercollapse,
	.sys_pc_prep = iris_hfi_gen1_sys_pc_prep,
	.session_open = iris_hfi_gen1_session_open,
	.session_set_config_params = iris_hfi_gen1_session_set_config_params,
	.session_set_property = iris_hfi_gen1_session_set_property,
	.session_start = iris_hfi_gen1_session_start,
	.session_queue_buf = iris_hfi_gen1_session_queue_buffer,
	.session_release_buf = iris_hfi_gen1_session_unset_buffers,
	.session_resume_drc = iris_hfi_gen1_session_continue,
	.session_stop = iris_hfi_gen1_session_stop,
	.session_drain = iris_hfi_gen1_session_drain,
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
