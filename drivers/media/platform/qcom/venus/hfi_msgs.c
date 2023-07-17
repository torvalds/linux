// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012-2016, The Linux Foundation. All rights reserved.
 * Copyright (C) 2017 Linaro Ltd.
 */
#include <linux/hash.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/soc/qcom/smem.h>
#include <media/videobuf2-v4l2.h>

#include "core.h"
#include "hfi.h"
#include "hfi_helper.h"
#include "hfi_msgs.h"
#include "hfi_parser.h"

#define SMEM_IMG_VER_TBL	469
#define VER_STR_SZ		128
#define SMEM_IMG_OFFSET_VENUS	(14 * 128)

static void event_seq_changed(struct venus_core *core, struct venus_inst *inst,
			      struct hfi_msg_event_notify_pkt *pkt)
{
	enum hfi_version ver = core->res->hfi_version;
	struct hfi_event_data event = {0};
	int num_properties_changed;
	struct hfi_framesize *frame_sz;
	struct hfi_profile_level *profile_level;
	struct hfi_bit_depth *pixel_depth;
	struct hfi_pic_struct *pic_struct;
	struct hfi_colour_space *colour_info;
	struct hfi_buffer_requirements *bufreq;
	struct hfi_extradata_input_crop *crop;
	struct hfi_dpb_counts *dpb_count;
	u8 *data_ptr;
	u32 ptype;

	inst->error = HFI_ERR_NONE;

	switch (pkt->event_data1) {
	case HFI_EVENT_DATA_SEQUENCE_CHANGED_SUFFICIENT_BUF_RESOURCES:
	case HFI_EVENT_DATA_SEQUENCE_CHANGED_INSUFFICIENT_BUF_RESOURCES:
		break;
	default:
		inst->error = HFI_ERR_SESSION_INVALID_PARAMETER;
		goto done;
	}

	event.event_type = pkt->event_data1;

	num_properties_changed = pkt->event_data2;
	if (!num_properties_changed) {
		inst->error = HFI_ERR_SESSION_INSUFFICIENT_RESOURCES;
		goto done;
	}

	data_ptr = (u8 *)&pkt->ext_event_data[0];
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
			event.buf_count = HFI_BUFREQ_COUNT_MIN(bufreq, ver);
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
			dpb_count = (struct hfi_dpb_counts *)data_ptr;
			event.buf_count = dpb_count->fw_min_cnt;
			data_ptr += sizeof(*dpb_count);
			break;
		default:
			break;
		}
		num_properties_changed--;
	} while (num_properties_changed > 0);

done:
	inst->ops->event_notify(inst, EVT_SYS_EVENT_CHANGE, &event);
}

static void event_release_buffer_ref(struct venus_core *core,
				     struct venus_inst *inst,
				     struct hfi_msg_event_notify_pkt *pkt)
{
	struct hfi_event_data event = {0};
	struct hfi_msg_event_release_buffer_ref_pkt *data;

	data = (struct hfi_msg_event_release_buffer_ref_pkt *)
		pkt->ext_event_data;

	event.event_type = HFI_EVENT_RELEASE_BUFFER_REFERENCE;
	event.packet_buffer = data->packet_buffer;
	event.extradata_buffer = data->extradata_buffer;
	event.tag = data->output_tag;

	inst->error = HFI_ERR_NONE;
	inst->ops->event_notify(inst, EVT_SYS_EVENT_CHANGE, &event);
}

static void event_sys_error(struct venus_core *core, u32 event,
			    struct hfi_msg_event_notify_pkt *pkt)
{
	if (pkt)
		dev_dbg(core->dev, VDBGH
			"sys error (session id:%x, data1:%x, data2:%x)\n",
			pkt->shdr.session_id, pkt->event_data1,
			pkt->event_data2);

	core->core_ops->event_notify(core, event);
}

static void
event_session_error(struct venus_core *core, struct venus_inst *inst,
		    struct hfi_msg_event_notify_pkt *pkt)
{
	struct device *dev = core->dev;

	dev_dbg(dev, VDBGH "session error: event id:%x, session id:%x\n",
		pkt->event_data1, pkt->shdr.session_id);

	if (!inst)
		return;

	switch (pkt->event_data1) {
	/* non fatal session errors */
	case HFI_ERR_SESSION_INVALID_SCALE_FACTOR:
	case HFI_ERR_SESSION_UNSUPPORT_BUFFERTYPE:
	case HFI_ERR_SESSION_UNSUPPORTED_SETTING:
	case HFI_ERR_SESSION_UPSCALE_NOT_SUPPORTED:
		inst->error = HFI_ERR_NONE;
		break;
	default:
		dev_err(dev, "session error: event id:%x (%x), session id:%x\n",
			pkt->event_data1, pkt->event_data2,
			pkt->shdr.session_id);

		inst->error = pkt->event_data1;
		inst->ops->event_notify(inst, EVT_SESSION_ERROR, NULL);
		break;
	}
}

static void hfi_event_notify(struct venus_core *core, struct venus_inst *inst,
			     void *packet)
{
	struct hfi_msg_event_notify_pkt *pkt = packet;

	if (!packet)
		return;

	switch (pkt->event_id) {
	case HFI_EVENT_SYS_ERROR:
		event_sys_error(core, EVT_SYS_ERROR, pkt);
		break;
	case HFI_EVENT_SESSION_ERROR:
		event_session_error(core, inst, pkt);
		break;
	case HFI_EVENT_SESSION_SEQUENCE_CHANGED:
		event_seq_changed(core, inst, pkt);
		break;
	case HFI_EVENT_RELEASE_BUFFER_REFERENCE:
		event_release_buffer_ref(core, inst, pkt);
		break;
	case HFI_EVENT_SESSION_PROPERTY_CHANGED:
		break;
	default:
		break;
	}
}

static void hfi_sys_init_done(struct venus_core *core, struct venus_inst *inst,
			      void *packet)
{
	struct hfi_msg_sys_init_done_pkt *pkt = packet;
	int rem_bytes;
	u32 error;

	error = pkt->error_type;
	if (error != HFI_ERR_NONE)
		goto done;

	if (!pkt->num_properties) {
		error = HFI_ERR_SYS_INVALID_PARAMETER;
		goto done;
	}

	rem_bytes = pkt->hdr.size - sizeof(*pkt);
	if (rem_bytes <= 0) {
		/* missing property data */
		error = HFI_ERR_SYS_INSUFFICIENT_RESOURCES;
		goto done;
	}

	error = hfi_parser(core, inst, pkt->data, rem_bytes);

done:
	core->error = error;
	complete(&core->done);
}

static void
sys_get_prop_image_version(struct venus_core *core,
			   struct hfi_msg_sys_property_info_pkt *pkt)
{
	struct device *dev = core->dev;
	u8 *smem_tbl_ptr;
	u8 *img_ver;
	int req_bytes;
	size_t smem_blk_sz;
	int ret;

	req_bytes = pkt->hdr.size - sizeof(*pkt);

	if (req_bytes < VER_STR_SZ || !pkt->data[0] || pkt->num_properties > 1)
		/* bad packet */
		return;

	img_ver = pkt->data;
	if (!img_ver)
		return;

	ret = sscanf(img_ver, "14:video-firmware.%u.%u-%u",
		     &core->venus_ver.major, &core->venus_ver.minor, &core->venus_ver.rev);
	if (ret)
		goto done;

	ret = sscanf(img_ver, "14:VIDEO.VPU.%u.%u-%u",
		     &core->venus_ver.major, &core->venus_ver.minor, &core->venus_ver.rev);
	if (ret)
		goto done;

	ret = sscanf(img_ver, "14:VIDEO.VE.%u.%u-%u",
		     &core->venus_ver.major, &core->venus_ver.minor, &core->venus_ver.rev);
	if (ret)
		goto done;

	dev_err(dev, VDBGL "error reading F/W version\n");
	return;

done:
	dev_dbg(dev, VDBGL "F/W version: %s, major %u, minor %u, revision %u\n",
		img_ver, core->venus_ver.major, core->venus_ver.minor, core->venus_ver.rev);

	smem_tbl_ptr = qcom_smem_get(QCOM_SMEM_HOST_ANY,
		SMEM_IMG_VER_TBL, &smem_blk_sz);
	if (!IS_ERR(smem_tbl_ptr) && smem_blk_sz >= SMEM_IMG_OFFSET_VENUS + VER_STR_SZ)
		memcpy(smem_tbl_ptr + SMEM_IMG_OFFSET_VENUS,
		       img_ver, VER_STR_SZ);
}

static void hfi_sys_property_info(struct venus_core *core,
				  struct venus_inst *inst, void *packet)
{
	struct hfi_msg_sys_property_info_pkt *pkt = packet;
	struct device *dev = core->dev;

	if (!pkt->num_properties) {
		dev_dbg(dev, VDBGL "no properties\n");
		return;
	}

	switch (pkt->property) {
	case HFI_PROPERTY_SYS_IMAGE_VERSION:
		sys_get_prop_image_version(core, pkt);
		break;
	default:
		dev_dbg(dev, VDBGL "unknown property data\n");
		break;
	}
}

static void hfi_sys_rel_resource_done(struct venus_core *core,
				      struct venus_inst *inst,
				      void *packet)
{
	struct hfi_msg_sys_release_resource_done_pkt *pkt = packet;

	core->error = pkt->error_type;
	complete(&core->done);
}

static void hfi_sys_ping_done(struct venus_core *core, struct venus_inst *inst,
			      void *packet)
{
	struct hfi_msg_sys_ping_ack_pkt *pkt = packet;

	core->error = HFI_ERR_NONE;

	if (pkt->client_data != 0xbeef)
		core->error = HFI_ERR_SYS_FATAL;

	complete(&core->done);
}

static void hfi_sys_idle_done(struct venus_core *core, struct venus_inst *inst,
			      void *packet)
{
	dev_dbg(core->dev, VDBGL "sys idle\n");
}

static void hfi_sys_pc_prepare_done(struct venus_core *core,
				    struct venus_inst *inst, void *packet)
{
	struct hfi_msg_sys_pc_prep_done_pkt *pkt = packet;

	dev_dbg(core->dev, VDBGL "pc prepare done (error %x)\n",
		pkt->error_type);
}

static unsigned int
session_get_prop_profile_level(struct hfi_msg_session_property_info_pkt *pkt,
			       struct hfi_profile_level *profile_level)
{
	struct hfi_profile_level *hfi;
	u32 req_bytes;

	req_bytes = pkt->shdr.hdr.size - sizeof(*pkt);

	if (!req_bytes || req_bytes % sizeof(struct hfi_profile_level))
		/* bad packet */
		return HFI_ERR_SESSION_INVALID_PARAMETER;

	hfi = (struct hfi_profile_level *)&pkt->data[0];
	profile_level->profile = hfi->profile;
	profile_level->level = hfi->level;

	return HFI_ERR_NONE;
}

static unsigned int
session_get_prop_buf_req(struct hfi_msg_session_property_info_pkt *pkt,
			 struct hfi_buffer_requirements *bufreq)
{
	struct hfi_buffer_requirements *buf_req;
	u32 req_bytes;
	unsigned int idx = 0;

	req_bytes = pkt->shdr.hdr.size - sizeof(*pkt);

	if (!req_bytes || req_bytes % sizeof(*buf_req) || !pkt->data[0])
		/* bad packet */
		return HFI_ERR_SESSION_INVALID_PARAMETER;

	buf_req = (struct hfi_buffer_requirements *)&pkt->data[0];
	if (!buf_req)
		return HFI_ERR_SESSION_INVALID_PARAMETER;

	while (req_bytes) {
		memcpy(&bufreq[idx], buf_req, sizeof(*bufreq));
		idx++;

		if (idx > HFI_BUFFER_TYPE_MAX)
			return HFI_ERR_SESSION_INVALID_PARAMETER;

		req_bytes -= sizeof(struct hfi_buffer_requirements);
		buf_req++;
	}

	return HFI_ERR_NONE;
}

static void hfi_session_prop_info(struct venus_core *core,
				  struct venus_inst *inst, void *packet)
{
	struct hfi_msg_session_property_info_pkt *pkt = packet;
	struct device *dev = core->dev;
	union hfi_get_property *hprop = &inst->hprop;
	unsigned int error = HFI_ERR_NONE;

	if (!pkt->num_properties) {
		error = HFI_ERR_SESSION_INVALID_PARAMETER;
		dev_err(dev, "%s: no properties\n", __func__);
		goto done;
	}

	switch (pkt->property) {
	case HFI_PROPERTY_CONFIG_BUFFER_REQUIREMENTS:
		memset(hprop->bufreq, 0, sizeof(hprop->bufreq));
		error = session_get_prop_buf_req(pkt, hprop->bufreq);
		break;
	case HFI_PROPERTY_PARAM_PROFILE_LEVEL_CURRENT:
		memset(&hprop->profile_level, 0, sizeof(hprop->profile_level));
		error = session_get_prop_profile_level(pkt,
						       &hprop->profile_level);
		break;
	case HFI_PROPERTY_CONFIG_VDEC_ENTROPY:
		break;
	default:
		dev_dbg(dev, VDBGM "unknown property id:%x\n", pkt->property);
		return;
	}

done:
	inst->error = error;
	complete(&inst->done);
}

static void hfi_session_init_done(struct venus_core *core,
				  struct venus_inst *inst, void *packet)
{
	struct hfi_msg_session_init_done_pkt *pkt = packet;
	int rem_bytes;
	u32 error;

	error = pkt->error_type;
	if (error != HFI_ERR_NONE)
		goto done;

	if (!IS_V1(core))
		goto done;

	rem_bytes = pkt->shdr.hdr.size - sizeof(*pkt);
	if (rem_bytes <= 0) {
		error = HFI_ERR_SESSION_INSUFFICIENT_RESOURCES;
		goto done;
	}

	error = hfi_parser(core, inst, pkt->data, rem_bytes);
done:
	inst->error = error;
	complete(&inst->done);
}

static void hfi_session_load_res_done(struct venus_core *core,
				      struct venus_inst *inst, void *packet)
{
	struct hfi_msg_session_load_resources_done_pkt *pkt = packet;

	inst->error = pkt->error_type;
	complete(&inst->done);
}

static void hfi_session_flush_done(struct venus_core *core,
				   struct venus_inst *inst, void *packet)
{
	struct hfi_msg_session_flush_done_pkt *pkt = packet;

	inst->error = pkt->error_type;
	complete(&inst->done);
	if (inst->ops->flush_done)
		inst->ops->flush_done(inst);
}

static void hfi_session_etb_done(struct venus_core *core,
				 struct venus_inst *inst, void *packet)
{
	struct hfi_msg_session_empty_buffer_done_pkt *pkt = packet;

	inst->error = pkt->error_type;
	inst->ops->buf_done(inst, HFI_BUFFER_INPUT, pkt->input_tag,
			    pkt->filled_len, pkt->offset, 0, 0, 0);
}

static void hfi_session_ftb_done(struct venus_core *core,
				 struct venus_inst *inst, void *packet)
{
	u32 session_type = inst->session_type;
	u64 timestamp_us = 0;
	u32 timestamp_hi = 0, timestamp_lo = 0;
	unsigned int error;
	u32 flags = 0, hfi_flags = 0, offset = 0, filled_len = 0;
	u32 pic_type = 0, buffer_type = 0, output_tag = -1;

	if (session_type == VIDC_SESSION_TYPE_ENC) {
		struct hfi_msg_session_fbd_compressed_pkt *pkt = packet;

		timestamp_hi = pkt->time_stamp_hi;
		timestamp_lo = pkt->time_stamp_lo;
		hfi_flags = pkt->flags;
		offset = pkt->offset;
		filled_len = pkt->filled_len;
		pic_type = pkt->picture_type;
		output_tag = pkt->output_tag;
		buffer_type = HFI_BUFFER_OUTPUT;

		error = pkt->error_type;
	} else if (session_type == VIDC_SESSION_TYPE_DEC) {
		struct hfi_msg_session_fbd_uncompressed_plane0_pkt *pkt =
			packet;

		timestamp_hi = pkt->time_stamp_hi;
		timestamp_lo = pkt->time_stamp_lo;
		hfi_flags = pkt->flags;
		offset = pkt->offset;
		filled_len = pkt->filled_len;
		pic_type = pkt->picture_type;
		output_tag = pkt->output_tag;

		if (pkt->stream_id == 0)
			buffer_type = HFI_BUFFER_OUTPUT;
		else if (pkt->stream_id == 1)
			buffer_type = HFI_BUFFER_OUTPUT2;

		error = pkt->error_type;
	} else {
		error = HFI_ERR_SESSION_INVALID_PARAMETER;
	}

	if (buffer_type != HFI_BUFFER_OUTPUT &&
	    buffer_type != HFI_BUFFER_OUTPUT2)
		goto done;

	if (hfi_flags & HFI_BUFFERFLAG_EOS)
		flags |= V4L2_BUF_FLAG_LAST;

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

	if (!(hfi_flags & HFI_BUFFERFLAG_TIMESTAMPINVALID) && filled_len) {
		timestamp_us = timestamp_hi;
		timestamp_us = (timestamp_us << 32) | timestamp_lo;
	}

done:
	inst->error = error;
	inst->ops->buf_done(inst, buffer_type, output_tag, filled_len,
			    offset, flags, hfi_flags, timestamp_us);
}

static void hfi_session_start_done(struct venus_core *core,
				   struct venus_inst *inst, void *packet)
{
	struct hfi_msg_session_start_done_pkt *pkt = packet;

	inst->error = pkt->error_type;
	complete(&inst->done);
}

static void hfi_session_stop_done(struct venus_core *core,
				  struct venus_inst *inst, void *packet)
{
	struct hfi_msg_session_stop_done_pkt *pkt = packet;

	inst->error = pkt->error_type;
	complete(&inst->done);
}

static void hfi_session_rel_res_done(struct venus_core *core,
				     struct venus_inst *inst, void *packet)
{
	struct hfi_msg_session_release_resources_done_pkt *pkt = packet;

	inst->error = pkt->error_type;
	complete(&inst->done);
}

static void hfi_session_rel_buf_done(struct venus_core *core,
				     struct venus_inst *inst, void *packet)
{
	struct hfi_msg_session_release_buffers_done_pkt *pkt = packet;

	inst->error = pkt->error_type;
	complete(&inst->done);
}

static void hfi_session_end_done(struct venus_core *core,
				 struct venus_inst *inst, void *packet)
{
	struct hfi_msg_session_end_done_pkt *pkt = packet;

	inst->error = pkt->error_type;
	complete(&inst->done);
}

static void hfi_session_abort_done(struct venus_core *core,
				   struct venus_inst *inst, void *packet)
{
	struct hfi_msg_sys_session_abort_done_pkt *pkt = packet;

	inst->error = pkt->error_type;
	complete(&inst->done);
}

static void hfi_session_get_seq_hdr_done(struct venus_core *core,
					 struct venus_inst *inst, void *packet)
{
	struct hfi_msg_session_get_sequence_hdr_done_pkt *pkt = packet;

	inst->error = pkt->error_type;
	complete(&inst->done);
}

struct hfi_done_handler {
	u32 pkt;
	u32 pkt_sz;
	u32 pkt_sz2;
	void (*done)(struct venus_core *, struct venus_inst *, void *);
	bool is_sys_pkt;
};

static const struct hfi_done_handler handlers[] = {
	{.pkt = HFI_MSG_EVENT_NOTIFY,
	 .pkt_sz = sizeof(struct hfi_msg_event_notify_pkt),
	 .done = hfi_event_notify,
	},
	{.pkt = HFI_MSG_SYS_INIT,
	 .pkt_sz = sizeof(struct hfi_msg_sys_init_done_pkt),
	 .done = hfi_sys_init_done,
	 .is_sys_pkt = true,
	},
	{.pkt = HFI_MSG_SYS_PROPERTY_INFO,
	 .pkt_sz = sizeof(struct hfi_msg_sys_property_info_pkt),
	 .done = hfi_sys_property_info,
	 .is_sys_pkt = true,
	},
	{.pkt = HFI_MSG_SYS_RELEASE_RESOURCE,
	 .pkt_sz = sizeof(struct hfi_msg_sys_release_resource_done_pkt),
	 .done = hfi_sys_rel_resource_done,
	 .is_sys_pkt = true,
	},
	{.pkt = HFI_MSG_SYS_PING_ACK,
	 .pkt_sz = sizeof(struct hfi_msg_sys_ping_ack_pkt),
	 .done = hfi_sys_ping_done,
	 .is_sys_pkt = true,
	},
	{.pkt = HFI_MSG_SYS_IDLE,
	 .pkt_sz = sizeof(struct hfi_msg_sys_idle_pkt),
	 .done = hfi_sys_idle_done,
	 .is_sys_pkt = true,
	},
	{.pkt = HFI_MSG_SYS_PC_PREP,
	 .pkt_sz = sizeof(struct hfi_msg_sys_pc_prep_done_pkt),
	 .done = hfi_sys_pc_prepare_done,
	 .is_sys_pkt = true,
	},
	{.pkt = HFI_MSG_SYS_SESSION_INIT,
	 .pkt_sz = sizeof(struct hfi_msg_session_init_done_pkt),
	 .done = hfi_session_init_done,
	},
	{.pkt = HFI_MSG_SYS_SESSION_END,
	 .pkt_sz = sizeof(struct hfi_msg_session_end_done_pkt),
	 .done = hfi_session_end_done,
	},
	{.pkt = HFI_MSG_SESSION_LOAD_RESOURCES,
	 .pkt_sz = sizeof(struct hfi_msg_session_load_resources_done_pkt),
	 .done = hfi_session_load_res_done,
	},
	{.pkt = HFI_MSG_SESSION_START,
	 .pkt_sz = sizeof(struct hfi_msg_session_start_done_pkt),
	 .done = hfi_session_start_done,
	},
	{.pkt = HFI_MSG_SESSION_STOP,
	 .pkt_sz = sizeof(struct hfi_msg_session_stop_done_pkt),
	 .done = hfi_session_stop_done,
	},
	{.pkt = HFI_MSG_SYS_SESSION_ABORT,
	 .pkt_sz = sizeof(struct hfi_msg_sys_session_abort_done_pkt),
	 .done = hfi_session_abort_done,
	},
	{.pkt = HFI_MSG_SESSION_EMPTY_BUFFER,
	 .pkt_sz = sizeof(struct hfi_msg_session_empty_buffer_done_pkt),
	 .done = hfi_session_etb_done,
	},
	{.pkt = HFI_MSG_SESSION_FILL_BUFFER,
	 .pkt_sz = sizeof(struct hfi_msg_session_fbd_uncompressed_plane0_pkt),
	 .pkt_sz2 = sizeof(struct hfi_msg_session_fbd_compressed_pkt),
	 .done = hfi_session_ftb_done,
	},
	{.pkt = HFI_MSG_SESSION_FLUSH,
	 .pkt_sz = sizeof(struct hfi_msg_session_flush_done_pkt),
	 .done = hfi_session_flush_done,
	},
	{.pkt = HFI_MSG_SESSION_PROPERTY_INFO,
	 .pkt_sz = sizeof(struct hfi_msg_session_property_info_pkt),
	 .done = hfi_session_prop_info,
	},
	{.pkt = HFI_MSG_SESSION_RELEASE_RESOURCES,
	 .pkt_sz = sizeof(struct hfi_msg_session_release_resources_done_pkt),
	 .done = hfi_session_rel_res_done,
	},
	{.pkt = HFI_MSG_SESSION_GET_SEQUENCE_HEADER,
	 .pkt_sz = sizeof(struct hfi_msg_session_get_sequence_hdr_done_pkt),
	 .done = hfi_session_get_seq_hdr_done,
	},
	{.pkt = HFI_MSG_SESSION_RELEASE_BUFFERS,
	 .pkt_sz = sizeof(struct hfi_msg_session_release_buffers_done_pkt),
	 .done = hfi_session_rel_buf_done,
	},
};

void hfi_process_watchdog_timeout(struct venus_core *core)
{
	event_sys_error(core, EVT_SYS_WATCHDOG_TIMEOUT, NULL);
}

static struct venus_inst *to_instance(struct venus_core *core, u32 session_id)
{
	struct venus_inst *inst;

	mutex_lock(&core->lock);
	list_for_each_entry(inst, &core->instances, list)
		if (hash32_ptr(inst) == session_id) {
			mutex_unlock(&core->lock);
			return inst;
		}
	mutex_unlock(&core->lock);

	return NULL;
}

u32 hfi_process_msg_packet(struct venus_core *core, struct hfi_pkt_hdr *hdr)
{
	const struct hfi_done_handler *handler;
	struct device *dev = core->dev;
	struct venus_inst *inst;
	bool found = false;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(handlers); i++) {
		handler = &handlers[i];
		if (handler->pkt != hdr->pkt_type)
			continue;
		found = true;
		break;
	}

	if (!found)
		return hdr->pkt_type;

	if (hdr->size && hdr->size < handler->pkt_sz &&
	    hdr->size < handler->pkt_sz2) {
		dev_err(dev, "bad packet size (%d should be %d, pkt type:%x)\n",
			hdr->size, handler->pkt_sz, hdr->pkt_type);

		return hdr->pkt_type;
	}

	if (handler->is_sys_pkt) {
		inst = NULL;
	} else {
		struct hfi_session_pkt *pkt;

		pkt = (struct hfi_session_pkt *)hdr;
		inst = to_instance(core, pkt->shdr.session_id);

		if (!inst)
			dev_warn(dev, "no valid instance(pkt session_id:%x, pkt:%x)\n",
				 pkt->shdr.session_id,
				 handler ? handler->pkt : 0);

		/*
		 * Event of type HFI_EVENT_SYS_ERROR will not have any session
		 * associated with it
		 */
		if (!inst && hdr->pkt_type != HFI_MSG_EVENT_NOTIFY) {
			dev_err(dev, "got invalid session id:%x\n",
				pkt->shdr.session_id);
			goto invalid_session;
		}
	}

	handler->done(core, inst, hdr);

invalid_session:
	return hdr->pkt_type;
}
