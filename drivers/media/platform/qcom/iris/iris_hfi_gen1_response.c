// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/bitfield.h>
#include <media/v4l2-mem2mem.h>

#include "iris_hfi_gen1.h"
#include "iris_hfi_gen1_defines.h"
#include "iris_instance.h"
#include "iris_vdec.h"
#include "iris_vpu_buffer.h"

static void iris_hfi_gen1_read_changed_params(struct iris_inst *inst,
					      struct hfi_msg_event_notify_pkt *pkt)
{
	struct v4l2_pix_format_mplane *pixmp_ip = &inst->fmt_src->fmt.pix_mp;
	struct v4l2_pix_format_mplane *pixmp_op = &inst->fmt_dst->fmt.pix_mp;
	u32 num_properties_changed = pkt->event_data2;
	u8 *data_ptr = (u8 *)&pkt->ext_event_data[0];
	u32 primaries, matrix_coeff, transfer_char;
	struct hfi_dpb_counts *iris_vpu_dpb_count;
	struct hfi_profile_level *profile_level;
	struct hfi_buffer_requirements *bufreq;
	struct hfi_extradata_input_crop *crop;
	struct hfi_colour_space *colour_info;
	struct iris_core *core = inst->core;
	u32 colour_description_present_flag;
	u32 video_signal_type_present_flag;
	struct hfi_event_data event = {0};
	struct hfi_bit_depth *pixel_depth;
	struct hfi_pic_struct *pic_struct;
	struct hfi_framesize *frame_sz;
	struct vb2_queue *dst_q;
	struct v4l2_ctrl *ctrl;
	u32 full_range, ptype;

	do {
		ptype = *((u32 *)data_ptr);
		switch (ptype) {
		case HFI_PROPERTY_PARAM_FRAME_SIZE:
			data_ptr += sizeof(u32);
			frame_sz = (struct hfi_framesize *)data_ptr;
			event.width = frame_sz->width;
			event.height = frame_sz->height;
			data_ptr += sizeof(*frame_sz);
			break;
		case HFI_PROPERTY_PARAM_PROFILE_LEVEL_CURRENT:
			data_ptr += sizeof(u32);
			profile_level = (struct hfi_profile_level *)data_ptr;
			event.profile = profile_level->profile;
			event.level = profile_level->level;
			data_ptr += sizeof(*profile_level);
			break;
		case HFI_PROPERTY_PARAM_VDEC_PIXEL_BITDEPTH:
			data_ptr += sizeof(u32);
			pixel_depth = (struct hfi_bit_depth *)data_ptr;
			event.bit_depth = pixel_depth->bit_depth;
			data_ptr += sizeof(*pixel_depth);
			break;
		case HFI_PROPERTY_PARAM_VDEC_PIC_STRUCT:
			data_ptr += sizeof(u32);
			pic_struct = (struct hfi_pic_struct *)data_ptr;
			event.pic_struct = pic_struct->progressive_only;
			data_ptr += sizeof(*pic_struct);
			break;
		case HFI_PROPERTY_PARAM_VDEC_COLOUR_SPACE:
			data_ptr += sizeof(u32);
			colour_info = (struct hfi_colour_space *)data_ptr;
			event.colour_space = colour_info->colour_space;
			data_ptr += sizeof(*colour_info);
			break;
		case HFI_PROPERTY_CONFIG_VDEC_ENTROPY:
			data_ptr += sizeof(u32);
			event.entropy_mode = *(u32 *)data_ptr;
			data_ptr += sizeof(u32);
			break;
		case HFI_PROPERTY_CONFIG_BUFFER_REQUIREMENTS:
			data_ptr += sizeof(u32);
			bufreq = (struct hfi_buffer_requirements *)data_ptr;
			event.buf_count = bufreq->count_min;
			data_ptr += sizeof(*bufreq);
			break;
		case HFI_INDEX_EXTRADATA_INPUT_CROP:
			data_ptr += sizeof(u32);
			crop = (struct hfi_extradata_input_crop *)data_ptr;
			event.input_crop.left = crop->left;
			event.input_crop.top = crop->top;
			event.input_crop.width = crop->width;
			event.input_crop.height = crop->height;
			data_ptr += sizeof(*crop);
			break;
		case HFI_PROPERTY_PARAM_VDEC_DPB_COUNTS:
			data_ptr += sizeof(u32);
			iris_vpu_dpb_count = (struct hfi_dpb_counts *)data_ptr;
			event.buf_count = iris_vpu_dpb_count->fw_min_count;
			data_ptr += sizeof(*iris_vpu_dpb_count);
			break;
		default:
			break;
		}
		num_properties_changed--;
	} while (num_properties_changed > 0);

	pixmp_ip->width = event.width;
	pixmp_ip->height = event.height;

	pixmp_op->width = ALIGN(event.width, 128);
	pixmp_op->height = ALIGN(event.height, 32);
	pixmp_op->plane_fmt[0].bytesperline = ALIGN(event.width, 128);
	pixmp_op->plane_fmt[0].sizeimage = iris_get_buffer_size(inst, BUF_OUTPUT);

	matrix_coeff =  FIELD_GET(GENMASK(7, 0), event.colour_space);
	transfer_char = FIELD_GET(GENMASK(15, 8), event.colour_space);
	primaries = FIELD_GET(GENMASK(23, 16), event.colour_space);
	colour_description_present_flag = FIELD_GET(GENMASK(24, 24), event.colour_space);
	full_range = FIELD_GET(GENMASK(25, 25), event.colour_space);
	video_signal_type_present_flag = FIELD_GET(GENMASK(29, 29), event.colour_space);

	pixmp_op->colorspace = V4L2_COLORSPACE_DEFAULT;
	pixmp_op->xfer_func = V4L2_XFER_FUNC_DEFAULT;
	pixmp_op->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	pixmp_op->quantization = V4L2_QUANTIZATION_DEFAULT;

	if (video_signal_type_present_flag) {
		pixmp_op->quantization =
			full_range ?
			V4L2_QUANTIZATION_FULL_RANGE :
			V4L2_QUANTIZATION_LIM_RANGE;
		if (colour_description_present_flag) {
			pixmp_op->colorspace =
				iris_hfi_get_v4l2_color_primaries(primaries);
			pixmp_op->xfer_func =
				iris_hfi_get_v4l2_transfer_char(transfer_char);
			pixmp_op->ycbcr_enc =
				iris_hfi_get_v4l2_matrix_coefficients(matrix_coeff);
		}
	}

	pixmp_ip->colorspace = pixmp_op->colorspace;
	pixmp_ip->xfer_func = pixmp_op->xfer_func;
	pixmp_ip->ycbcr_enc = pixmp_op->ycbcr_enc;
	pixmp_ip->quantization = pixmp_op->quantization;

	if (event.input_crop.width > 0 && event.input_crop.height > 0) {
		inst->crop.left = event.input_crop.left;
		inst->crop.top = event.input_crop.top;
		inst->crop.width = event.input_crop.width;
		inst->crop.height = event.input_crop.height;
	} else {
		inst->crop.left = 0;
		inst->crop.top = 0;
		inst->crop.width = event.width;
		inst->crop.height = event.height;
	}

	inst->fw_min_count = event.buf_count;
	inst->buffers[BUF_OUTPUT].min_count = iris_vpu_buf_count(inst, BUF_OUTPUT);
	inst->buffers[BUF_OUTPUT].size = pixmp_op->plane_fmt[0].sizeimage;
	ctrl = v4l2_ctrl_find(&inst->ctrl_handler, V4L2_CID_MIN_BUFFERS_FOR_CAPTURE);
	if (ctrl)
		v4l2_ctrl_s_ctrl(ctrl, inst->buffers[BUF_OUTPUT].min_count);

	dst_q = v4l2_m2m_get_dst_vq(inst->m2m_ctx);
	dst_q->min_reqbufs_allocation = inst->buffers[BUF_OUTPUT].min_count;

	if (event.bit_depth || !event.pic_struct) {
		dev_err(core->dev, "unsupported content, bit depth: %x, pic_struct = %x\n",
			event.bit_depth, event.pic_struct);
		iris_inst_change_state(inst, IRIS_INST_ERROR);
	}
}

static void iris_hfi_gen1_event_seq_changed(struct iris_inst *inst,
					    struct hfi_msg_event_notify_pkt *pkt)
{
	struct hfi_session_flush_pkt flush_pkt;
	u32 num_properties_changed;
	int ret;

	ret = iris_inst_sub_state_change_drc(inst);
	if (ret)
		return;

	switch (pkt->event_data1) {
	case HFI_EVENT_DATA_SEQUENCE_CHANGED_SUFFICIENT_BUF_RESOURCES:
	case HFI_EVENT_DATA_SEQUENCE_CHANGED_INSUFFICIENT_BUF_RESOURCES:
		break;
	default:
		iris_inst_change_state(inst, IRIS_INST_ERROR);
		return;
	}

	num_properties_changed = pkt->event_data2;
	if (!num_properties_changed) {
		iris_inst_change_state(inst, IRIS_INST_ERROR);
		return;
	}

	iris_hfi_gen1_read_changed_params(inst, pkt);

	if (inst->state != IRIS_INST_ERROR) {
		reinit_completion(&inst->flush_completion);

		flush_pkt.shdr.hdr.size = sizeof(struct hfi_session_flush_pkt);
		flush_pkt.shdr.hdr.pkt_type = HFI_CMD_SESSION_FLUSH;
		flush_pkt.shdr.session_id = inst->session_id;
		flush_pkt.flush_type = HFI_FLUSH_OUTPUT;
		iris_hfi_queue_cmd_write(inst->core, &flush_pkt, flush_pkt.shdr.hdr.size);
	}

	iris_vdec_src_change(inst);
	iris_inst_sub_state_change_drc_last(inst);
}

static void
iris_hfi_gen1_sys_event_notify(struct iris_core *core, void *packet)
{
	struct hfi_msg_event_notify_pkt *pkt = packet;
	struct iris_inst *instance;

	if (pkt->event_id == HFI_EVENT_SYS_ERROR)
		dev_err(core->dev, "sys error (type: %x, session id:%x, data1:%x, data2:%x)\n",
			pkt->event_id, pkt->shdr.session_id, pkt->event_data1,
			pkt->event_data2);

	core->state = IRIS_CORE_ERROR;

	mutex_lock(&core->lock);
	list_for_each_entry(instance, &core->instances, list)
		iris_inst_change_state(instance, IRIS_INST_ERROR);
	mutex_unlock(&core->lock);

	schedule_delayed_work(&core->sys_error_handler, msecs_to_jiffies(10));
}

static void
iris_hfi_gen1_event_session_error(struct iris_inst *inst, struct hfi_msg_event_notify_pkt *pkt)
{
	switch (pkt->event_data1) {
	/* non fatal session errors */
	case HFI_ERR_SESSION_INVALID_SCALE_FACTOR:
	case HFI_ERR_SESSION_UNSUPPORT_BUFFERTYPE:
	case HFI_ERR_SESSION_UNSUPPORTED_SETTING:
	case HFI_ERR_SESSION_UPSCALE_NOT_SUPPORTED:
		dev_dbg(inst->core->dev, "session error: event id:%x, session id:%x\n",
			pkt->event_data1, pkt->shdr.session_id);
		break;
	/* fatal session errors */
	default:
		/*
		 * firmware fills event_data2 as an additional information about the
		 * hfi command for which session error has ouccured.
		 */
		dev_err(inst->core->dev,
			"session error for command: %x, event id:%x, session id:%x\n",
			pkt->event_data2, pkt->event_data1,
			pkt->shdr.session_id);
		iris_vb2_queue_error(inst);
		iris_inst_change_state(inst, IRIS_INST_ERROR);
		break;
	}
}

static void iris_hfi_gen1_session_event_notify(struct iris_inst *inst, void *packet)
{
	struct hfi_msg_event_notify_pkt *pkt = packet;

	switch (pkt->event_id) {
	case HFI_EVENT_SESSION_ERROR:
		iris_hfi_gen1_event_session_error(inst, pkt);
		break;
	case HFI_EVENT_SESSION_SEQUENCE_CHANGED:
		iris_hfi_gen1_event_seq_changed(inst, pkt);
		break;
	default:
		break;
	}
}

static void iris_hfi_gen1_sys_init_done(struct iris_core *core, void *packet)
{
	struct hfi_msg_sys_init_done_pkt *pkt = packet;

	if (pkt->error_type != HFI_ERR_NONE) {
		core->state = IRIS_CORE_ERROR;
		return;
	}

	complete(&core->core_init_done);
}

static void
iris_hfi_gen1_sys_get_prop_image_version(struct iris_core *core,
					 struct hfi_msg_sys_property_info_pkt *pkt)
{
	int req_bytes = pkt->hdr.size - sizeof(*pkt);
	char fw_version[IRIS_FW_VERSION_LENGTH];
	u8 *str_image_version;
	u32 i;

	if (req_bytes < IRIS_FW_VERSION_LENGTH - 1 || !pkt->data[0] || pkt->num_properties > 1) {
		dev_err(core->dev, "bad packet\n");
		return;
	}

	str_image_version = pkt->data;
	if (!str_image_version) {
		dev_err(core->dev, "firmware version not available\n");
		return;
	}

	for (i = 0; i < IRIS_FW_VERSION_LENGTH - 1; i++) {
		if (str_image_version[i] != '\0')
			fw_version[i] = str_image_version[i];
		else
			fw_version[i] = ' ';
	}
	fw_version[i] = '\0';
	dev_dbg(core->dev, "firmware version: %s\n", fw_version);
}

static void iris_hfi_gen1_sys_property_info(struct iris_core *core, void *packet)
{
	struct hfi_msg_sys_property_info_pkt *pkt = packet;

	if (!pkt->num_properties) {
		dev_dbg(core->dev, "no properties\n");
		return;
	}

	switch (pkt->property) {
	case HFI_PROPERTY_SYS_IMAGE_VERSION:
		iris_hfi_gen1_sys_get_prop_image_version(core, pkt);
		break;
	default:
		dev_dbg(core->dev, "unknown property data\n");
		break;
	}
}

static void iris_hfi_gen1_session_etb_done(struct iris_inst *inst, void *packet)
{
	struct hfi_msg_session_empty_buffer_done_pkt *pkt = packet;
	struct v4l2_m2m_ctx *m2m_ctx = inst->m2m_ctx;
	struct v4l2_m2m_buffer *m2m_buffer, *n;
	struct iris_buffer *buf = NULL;
	bool found = false;

	v4l2_m2m_for_each_src_buf_safe(m2m_ctx, m2m_buffer, n) {
		buf = to_iris_buffer(&m2m_buffer->vb);
		if (buf->index == pkt->input_tag) {
			found = true;
			break;
		}
	}
	if (!found)
		goto error;

	if (pkt->shdr.error_type == HFI_ERR_SESSION_UNSUPPORTED_STREAM) {
		buf->flags = V4L2_BUF_FLAG_ERROR;
		iris_vb2_queue_error(inst);
		iris_inst_change_state(inst, IRIS_INST_ERROR);
	}

	if (!(buf->attr & BUF_ATTR_QUEUED))
		return;

	buf->attr &= ~BUF_ATTR_QUEUED;

	if (!(buf->attr & BUF_ATTR_BUFFER_DONE)) {
		buf->attr |= BUF_ATTR_BUFFER_DONE;
		iris_vb2_buffer_done(inst, buf);
	}

	return;

error:
	iris_inst_change_state(inst, IRIS_INST_ERROR);
	dev_err(inst->core->dev, "error in etb done\n");
}

static void iris_hfi_gen1_session_ftb_done(struct iris_inst *inst, void *packet)
{
	struct hfi_msg_session_fbd_uncompressed_plane0_pkt *pkt = packet;
	struct v4l2_m2m_ctx *m2m_ctx = inst->m2m_ctx;
	struct v4l2_m2m_buffer *m2m_buffer, *n;
	struct hfi_session_flush_pkt flush_pkt;
	u32 timestamp_hi = pkt->time_stamp_hi;
	u32 timestamp_lo = pkt->time_stamp_lo;
	struct iris_core *core = inst->core;
	u32 filled_len = pkt->filled_len;
	u32 pic_type = pkt->picture_type;
	u32 output_tag = pkt->output_tag;
	struct iris_buffer *buf, *iter;
	struct iris_buffers *buffers;
	u32 hfi_flags = pkt->flags;
	u32 offset = pkt->offset;
	u64 timestamp_us = 0;
	bool found = false;
	u32 flags = 0;

	if ((hfi_flags & HFI_BUFFERFLAG_EOS) && !filled_len) {
		reinit_completion(&inst->flush_completion);

		flush_pkt.shdr.hdr.size = sizeof(struct hfi_session_flush_pkt);
		flush_pkt.shdr.hdr.pkt_type = HFI_CMD_SESSION_FLUSH;
		flush_pkt.shdr.session_id = inst->session_id;
		flush_pkt.flush_type = HFI_FLUSH_OUTPUT;
		iris_hfi_queue_cmd_write(core, &flush_pkt, flush_pkt.shdr.hdr.size);
		iris_inst_sub_state_change_drain_last(inst);

		return;
	}

	if (iris_split_mode_enabled(inst) && pkt->stream_id == 0) {
		buffers = &inst->buffers[BUF_DPB];
		if (!buffers)
			goto error;

		found = false;
		list_for_each_entry(iter, &buffers->list, list) {
			if (!(iter->attr & BUF_ATTR_QUEUED))
				continue;

			found = (iter->index == output_tag &&
				iter->data_offset == offset);

			if (found) {
				buf = iter;
				break;
			}
		}
	} else {
		v4l2_m2m_for_each_dst_buf_safe(m2m_ctx, m2m_buffer, n) {
			buf = to_iris_buffer(&m2m_buffer->vb);
			if (!(buf->attr & BUF_ATTR_QUEUED))
				continue;

			found = (buf->index == output_tag &&
				 buf->data_offset == offset);

			if (found)
				break;
		}
	}
	if (!found)
		goto error;

	buf->data_offset = offset;
	buf->data_size = filled_len;

	if (filled_len) {
		timestamp_us = timestamp_hi;
		timestamp_us = (timestamp_us << 32) | timestamp_lo;
	} else {
		flags |= V4L2_BUF_FLAG_LAST;
	}
	buf->timestamp = timestamp_us;

	switch (pic_type) {
	case HFI_PICTURE_IDR:
	case HFI_PICTURE_I:
		flags |= V4L2_BUF_FLAG_KEYFRAME;
		break;
	case HFI_PICTURE_P:
		flags |= V4L2_BUF_FLAG_PFRAME;
		break;
	case HFI_PICTURE_B:
		flags |= V4L2_BUF_FLAG_BFRAME;
		break;
	case HFI_FRAME_NOTCODED:
	case HFI_UNUSED_PICT:
	case HFI_FRAME_YUV:
	default:
		break;
	}

	buf->attr &= ~BUF_ATTR_QUEUED;
	buf->attr |= BUF_ATTR_DEQUEUED;
	buf->attr |= BUF_ATTR_BUFFER_DONE;

	buf->flags |= flags;

	iris_vb2_buffer_done(inst, buf);

	return;

error:
	iris_inst_change_state(inst, IRIS_INST_ERROR);
	dev_err(core->dev, "error in ftb done\n");
}

struct iris_hfi_gen1_response_pkt_info {
	u32 pkt;
	u32 pkt_sz;
};

static const struct iris_hfi_gen1_response_pkt_info pkt_infos[] = {
	{
	 .pkt = HFI_MSG_EVENT_NOTIFY,
	 .pkt_sz = sizeof(struct hfi_msg_event_notify_pkt),
	},
	{
	 .pkt = HFI_MSG_SYS_INIT,
	 .pkt_sz = sizeof(struct hfi_msg_sys_init_done_pkt),
	},
	{
	 .pkt = HFI_MSG_SYS_PROPERTY_INFO,
	 .pkt_sz = sizeof(struct hfi_msg_sys_property_info_pkt),
	},
	{
	 .pkt = HFI_MSG_SYS_SESSION_INIT,
	 .pkt_sz = sizeof(struct hfi_msg_session_init_done_pkt),
	},
	{
	 .pkt = HFI_MSG_SYS_SESSION_END,
	 .pkt_sz = sizeof(struct hfi_msg_session_hdr_pkt),
	},
	{
	 .pkt = HFI_MSG_SESSION_LOAD_RESOURCES,
	 .pkt_sz = sizeof(struct hfi_msg_session_hdr_pkt),
	},
	{
	 .pkt = HFI_MSG_SESSION_START,
	 .pkt_sz = sizeof(struct hfi_msg_session_hdr_pkt),
	},
	{
	 .pkt = HFI_MSG_SESSION_STOP,
	 .pkt_sz = sizeof(struct hfi_msg_session_hdr_pkt),
	},
	{
	 .pkt = HFI_MSG_SESSION_EMPTY_BUFFER,
	 .pkt_sz = sizeof(struct hfi_msg_session_empty_buffer_done_pkt),
	},
	{
	 .pkt = HFI_MSG_SESSION_FILL_BUFFER,
	 .pkt_sz = sizeof(struct hfi_msg_session_fbd_uncompressed_plane0_pkt),
	},
	{
	 .pkt = HFI_MSG_SESSION_FLUSH,
	 .pkt_sz = sizeof(struct hfi_msg_session_flush_done_pkt),
	},
	{
	 .pkt = HFI_MSG_SESSION_RELEASE_RESOURCES,
	 .pkt_sz = sizeof(struct hfi_msg_session_hdr_pkt),
	},
	{
	 .pkt = HFI_MSG_SESSION_RELEASE_BUFFERS,
	 .pkt_sz = sizeof(struct hfi_msg_session_release_buffers_done_pkt),
	},
};

static void iris_hfi_gen1_handle_response(struct iris_core *core, void *response)
{
	struct hfi_pkt_hdr *hdr = (struct hfi_pkt_hdr *)response;
	const struct iris_hfi_gen1_response_pkt_info *pkt_info;
	struct device *dev = core->dev;
	struct hfi_session_pkt *pkt;
	struct completion *done;
	struct iris_inst *inst;
	bool found = false;
	u32 i;

	for (i = 0; i < ARRAY_SIZE(pkt_infos); i++) {
		pkt_info = &pkt_infos[i];
		if (pkt_info->pkt != hdr->pkt_type)
			continue;
		found = true;
		break;
	}

	if (!found || hdr->size < pkt_info->pkt_sz) {
		dev_err(dev, "bad packet size (%d should be %d, pkt type:%x, found %d)\n",
			hdr->size, pkt_info->pkt_sz, hdr->pkt_type, found);

		return;
	}

	switch (hdr->pkt_type) {
	case HFI_MSG_SYS_INIT:
		iris_hfi_gen1_sys_init_done(core, hdr);
		break;
	case HFI_MSG_SYS_PROPERTY_INFO:
		iris_hfi_gen1_sys_property_info(core, hdr);
		break;
	case HFI_MSG_EVENT_NOTIFY:
		pkt = (struct hfi_session_pkt *)hdr;
		inst = iris_get_instance(core, pkt->shdr.session_id);
		if (inst) {
			mutex_lock(&inst->lock);
			iris_hfi_gen1_session_event_notify(inst, hdr);
			mutex_unlock(&inst->lock);
		} else {
			iris_hfi_gen1_sys_event_notify(core, hdr);
		}

		break;
	default:
		pkt = (struct hfi_session_pkt *)hdr;
		inst = iris_get_instance(core, pkt->shdr.session_id);
		if (!inst) {
			dev_warn(dev, "no valid instance(pkt session_id:%x, pkt:%x)\n",
				 pkt->shdr.session_id,
				 pkt_info ? pkt_info->pkt : 0);
			return;
		}

		mutex_lock(&inst->lock);
		if (hdr->pkt_type == HFI_MSG_SESSION_EMPTY_BUFFER) {
			iris_hfi_gen1_session_etb_done(inst, hdr);
		} else if (hdr->pkt_type == HFI_MSG_SESSION_FILL_BUFFER) {
			iris_hfi_gen1_session_ftb_done(inst, hdr);
		} else {
			struct hfi_msg_session_hdr_pkt *shdr;

			shdr = (struct hfi_msg_session_hdr_pkt *)hdr;
			if (shdr->error_type != HFI_ERR_NONE)
				iris_inst_change_state(inst, IRIS_INST_ERROR);

			done = pkt_info->pkt == HFI_MSG_SESSION_FLUSH ?
				&inst->flush_completion : &inst->completion;
			complete(done);
		}
		mutex_unlock(&inst->lock);

		break;
	}
}

static void iris_hfi_gen1_flush_debug_queue(struct iris_core *core, u8 *packet)
{
	struct hfi_msg_sys_coverage_pkt *pkt;

	while (!iris_hfi_queue_dbg_read(core, packet)) {
		pkt = (struct hfi_msg_sys_coverage_pkt *)packet;

		if (pkt->hdr.pkt_type != HFI_MSG_SYS_COV) {
			struct hfi_msg_sys_debug_pkt *pkt =
				(struct hfi_msg_sys_debug_pkt *)packet;

			dev_dbg(core->dev, "%s", pkt->msg_data);
		}
	}
}

static void iris_hfi_gen1_response_handler(struct iris_core *core)
{
	memset(core->response_packet, 0, sizeof(struct hfi_pkt_hdr));
	while (!iris_hfi_queue_msg_read(core, core->response_packet)) {
		iris_hfi_gen1_handle_response(core, core->response_packet);
		memset(core->response_packet, 0, sizeof(struct hfi_pkt_hdr));
	}

	iris_hfi_gen1_flush_debug_queue(core, core->response_packet);
}

static const struct iris_hfi_response_ops iris_hfi_gen1_response_ops = {
	.hfi_response_handler = iris_hfi_gen1_response_handler,
};

void iris_hfi_gen1_response_ops_init(struct iris_core *core)
{
	core->hfi_response_ops = &iris_hfi_gen1_response_ops;
}
