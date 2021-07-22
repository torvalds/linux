// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012-2016, The Linux Foundation. All rights reserved.
 * Copyright (C) 2017 Linaro Ltd.
 */
#include <linux/overflow.h>
#include <linux/errno.h>
#include <linux/hash.h>

#include "hfi_cmds.h"

static enum hfi_version hfi_ver;

void pkt_sys_init(struct hfi_sys_init_pkt *pkt, u32 arch_type)
{
	pkt->hdr.size = sizeof(*pkt);
	pkt->hdr.pkt_type = HFI_CMD_SYS_INIT;
	pkt->arch_type = arch_type;
}

void pkt_sys_pc_prep(struct hfi_sys_pc_prep_pkt *pkt)
{
	pkt->hdr.size = sizeof(*pkt);
	pkt->hdr.pkt_type = HFI_CMD_SYS_PC_PREP;
}

void pkt_sys_idle_indicator(struct hfi_sys_set_property_pkt *pkt, u32 enable)
{
	struct hfi_enable *hfi = (struct hfi_enable *)&pkt->data[1];

	pkt->hdr.size = struct_size(pkt, data, 1) + sizeof(*hfi);
	pkt->hdr.pkt_type = HFI_CMD_SYS_SET_PROPERTY;
	pkt->num_properties = 1;
	pkt->data[0] = HFI_PROPERTY_SYS_IDLE_INDICATOR;
	hfi->enable = enable;
}

void pkt_sys_debug_config(struct hfi_sys_set_property_pkt *pkt, u32 mode,
			  u32 config)
{
	struct hfi_debug_config *hfi;

	pkt->hdr.size = struct_size(pkt, data, 1) + sizeof(*hfi);
	pkt->hdr.pkt_type = HFI_CMD_SYS_SET_PROPERTY;
	pkt->num_properties = 1;
	pkt->data[0] = HFI_PROPERTY_SYS_DEBUG_CONFIG;
	hfi = (struct hfi_debug_config *)&pkt->data[1];
	hfi->config = config;
	hfi->mode = mode;
}

void pkt_sys_coverage_config(struct hfi_sys_set_property_pkt *pkt, u32 mode)
{
	pkt->hdr.size = struct_size(pkt, data, 2);
	pkt->hdr.pkt_type = HFI_CMD_SYS_SET_PROPERTY;
	pkt->num_properties = 1;
	pkt->data[0] = HFI_PROPERTY_SYS_CONFIG_COVERAGE;
	pkt->data[1] = mode;
}

int pkt_sys_set_resource(struct hfi_sys_set_resource_pkt *pkt, u32 id, u32 size,
			 u32 addr, void *cookie)
{
	pkt->hdr.size = sizeof(*pkt);
	pkt->hdr.pkt_type = HFI_CMD_SYS_SET_RESOURCE;
	pkt->resource_handle = hash32_ptr(cookie);

	switch (id) {
	case VIDC_RESOURCE_OCMEM:
	case VIDC_RESOURCE_VMEM: {
		struct hfi_resource_ocmem *res =
			(struct hfi_resource_ocmem *)&pkt->resource_data[0];

		res->size = size;
		res->mem = addr;
		pkt->resource_type = HFI_RESOURCE_OCMEM;
		pkt->hdr.size += sizeof(*res) - sizeof(u32);
		break;
	}
	case VIDC_RESOURCE_NONE:
	default:
		return -ENOTSUPP;
	}

	return 0;
}

int pkt_sys_unset_resource(struct hfi_sys_release_resource_pkt *pkt, u32 id,
			   u32 size, void *cookie)
{
	pkt->hdr.size = sizeof(*pkt);
	pkt->hdr.pkt_type = HFI_CMD_SYS_RELEASE_RESOURCE;
	pkt->resource_handle = hash32_ptr(cookie);

	switch (id) {
	case VIDC_RESOURCE_OCMEM:
	case VIDC_RESOURCE_VMEM:
		pkt->resource_type = HFI_RESOURCE_OCMEM;
		break;
	case VIDC_RESOURCE_NONE:
		break;
	default:
		return -ENOTSUPP;
	}

	return 0;
}

void pkt_sys_ping(struct hfi_sys_ping_pkt *pkt, u32 cookie)
{
	pkt->hdr.size = sizeof(*pkt);
	pkt->hdr.pkt_type = HFI_CMD_SYS_PING;
	pkt->client_data = cookie;
}

void pkt_sys_power_control(struct hfi_sys_set_property_pkt *pkt, u32 enable)
{
	struct hfi_enable *hfi = (struct hfi_enable *)&pkt->data[1];

	pkt->hdr.size = struct_size(pkt, data, 1) + sizeof(*hfi);
	pkt->hdr.pkt_type = HFI_CMD_SYS_SET_PROPERTY;
	pkt->num_properties = 1;
	pkt->data[0] = HFI_PROPERTY_SYS_CODEC_POWER_PLANE_CTRL;
	hfi->enable = enable;
}

int pkt_sys_ssr_cmd(struct hfi_sys_test_ssr_pkt *pkt, u32 trigger_type)
{
	switch (trigger_type) {
	case HFI_TEST_SSR_SW_ERR_FATAL:
	case HFI_TEST_SSR_SW_DIV_BY_ZERO:
	case HFI_TEST_SSR_HW_WDOG_IRQ:
		break;
	default:
		return -EINVAL;
	}

	pkt->hdr.size = sizeof(*pkt);
	pkt->hdr.pkt_type = HFI_CMD_SYS_TEST_SSR;
	pkt->trigger_type = trigger_type;

	return 0;
}

void pkt_sys_image_version(struct hfi_sys_get_property_pkt *pkt)
{
	pkt->hdr.size = sizeof(*pkt);
	pkt->hdr.pkt_type = HFI_CMD_SYS_GET_PROPERTY;
	pkt->num_properties = 1;
	pkt->data[0] = HFI_PROPERTY_SYS_IMAGE_VERSION;
}

int pkt_session_init(struct hfi_session_init_pkt *pkt, void *cookie,
		     u32 session_type, u32 codec)
{
	if (!pkt || !cookie || !codec)
		return -EINVAL;

	pkt->shdr.hdr.size = sizeof(*pkt);
	pkt->shdr.hdr.pkt_type = HFI_CMD_SYS_SESSION_INIT;
	pkt->shdr.session_id = hash32_ptr(cookie);
	pkt->session_domain = session_type;
	pkt->session_codec = codec;

	return 0;
}

void pkt_session_cmd(struct hfi_session_pkt *pkt, u32 pkt_type, void *cookie)
{
	pkt->shdr.hdr.size = sizeof(*pkt);
	pkt->shdr.hdr.pkt_type = pkt_type;
	pkt->shdr.session_id = hash32_ptr(cookie);
}

int pkt_session_set_buffers(struct hfi_session_set_buffers_pkt *pkt,
			    void *cookie, struct hfi_buffer_desc *bd)
{
	unsigned int i;

	if (!cookie || !pkt || !bd)
		return -EINVAL;

	pkt->shdr.hdr.pkt_type = HFI_CMD_SESSION_SET_BUFFERS;
	pkt->shdr.session_id = hash32_ptr(cookie);
	pkt->buffer_size = bd->buffer_size;
	pkt->min_buffer_size = bd->buffer_size;
	pkt->num_buffers = bd->num_buffers;

	if (bd->buffer_type == HFI_BUFFER_OUTPUT ||
	    bd->buffer_type == HFI_BUFFER_OUTPUT2) {
		struct hfi_buffer_info *bi;

		pkt->extradata_size = bd->extradata_size;
		pkt->shdr.hdr.size = sizeof(*pkt) - sizeof(u32) +
			(bd->num_buffers * sizeof(*bi));
		bi = (struct hfi_buffer_info *)pkt->buffer_info;
		for (i = 0; i < pkt->num_buffers; i++) {
			bi->buffer_addr = bd->device_addr;
			bi->extradata_addr = bd->extradata_addr;
		}
	} else {
		pkt->extradata_size = 0;
		pkt->shdr.hdr.size = sizeof(*pkt) +
			((bd->num_buffers - 1) * sizeof(u32));
		for (i = 0; i < pkt->num_buffers; i++)
			pkt->buffer_info[i] = bd->device_addr;
	}

	pkt->buffer_type = bd->buffer_type;

	return 0;
}

int pkt_session_unset_buffers(struct hfi_session_release_buffer_pkt *pkt,
			      void *cookie, struct hfi_buffer_desc *bd)
{
	unsigned int i;

	if (!cookie || !pkt || !bd)
		return -EINVAL;

	pkt->shdr.hdr.pkt_type = HFI_CMD_SESSION_RELEASE_BUFFERS;
	pkt->shdr.session_id = hash32_ptr(cookie);
	pkt->buffer_size = bd->buffer_size;
	pkt->num_buffers = bd->num_buffers;

	if (bd->buffer_type == HFI_BUFFER_OUTPUT ||
	    bd->buffer_type == HFI_BUFFER_OUTPUT2) {
		struct hfi_buffer_info *bi;

		bi = (struct hfi_buffer_info *)pkt->buffer_info;
		for (i = 0; i < pkt->num_buffers; i++) {
			bi->buffer_addr = bd->device_addr;
			bi->extradata_addr = bd->extradata_addr;
		}
		pkt->shdr.hdr.size =
				sizeof(struct hfi_session_set_buffers_pkt) -
				sizeof(u32) + (bd->num_buffers * sizeof(*bi));
	} else {
		for (i = 0; i < pkt->num_buffers; i++)
			pkt->buffer_info[i] = bd->device_addr;

		pkt->extradata_size = 0;
		pkt->shdr.hdr.size =
				sizeof(struct hfi_session_set_buffers_pkt) +
				((bd->num_buffers - 1) * sizeof(u32));
	}

	pkt->response_req = bd->response_required;
	pkt->buffer_type = bd->buffer_type;

	return 0;
}

int pkt_session_etb_decoder(struct hfi_session_empty_buffer_compressed_pkt *pkt,
			    void *cookie, struct hfi_frame_data *in_frame)
{
	if (!cookie)
		return -EINVAL;

	pkt->shdr.hdr.size = sizeof(*pkt);
	pkt->shdr.hdr.pkt_type = HFI_CMD_SESSION_EMPTY_BUFFER;
	pkt->shdr.session_id = hash32_ptr(cookie);
	pkt->time_stamp_hi = upper_32_bits(in_frame->timestamp);
	pkt->time_stamp_lo = lower_32_bits(in_frame->timestamp);
	pkt->flags = in_frame->flags;
	pkt->mark_target = in_frame->mark_target;
	pkt->mark_data = in_frame->mark_data;
	pkt->offset = in_frame->offset;
	pkt->alloc_len = in_frame->alloc_len;
	pkt->filled_len = in_frame->filled_len;
	pkt->input_tag = in_frame->clnt_data;
	pkt->packet_buffer = in_frame->device_addr;

	return 0;
}

int pkt_session_etb_encoder(
		struct hfi_session_empty_buffer_uncompressed_plane0_pkt *pkt,
		void *cookie, struct hfi_frame_data *in_frame)
{
	if (!cookie || !in_frame->device_addr)
		return -EINVAL;

	pkt->shdr.hdr.size = sizeof(*pkt);
	pkt->shdr.hdr.pkt_type = HFI_CMD_SESSION_EMPTY_BUFFER;
	pkt->shdr.session_id = hash32_ptr(cookie);
	pkt->view_id = 0;
	pkt->time_stamp_hi = upper_32_bits(in_frame->timestamp);
	pkt->time_stamp_lo = lower_32_bits(in_frame->timestamp);
	pkt->flags = in_frame->flags;
	pkt->mark_target = in_frame->mark_target;
	pkt->mark_data = in_frame->mark_data;
	pkt->offset = in_frame->offset;
	pkt->alloc_len = in_frame->alloc_len;
	pkt->filled_len = in_frame->filled_len;
	pkt->input_tag = in_frame->clnt_data;
	pkt->packet_buffer = in_frame->device_addr;
	pkt->extradata_buffer = in_frame->extradata_addr;

	return 0;
}

int pkt_session_ftb(struct hfi_session_fill_buffer_pkt *pkt, void *cookie,
		    struct hfi_frame_data *out_frame)
{
	if (!cookie || !out_frame || !out_frame->device_addr)
		return -EINVAL;

	pkt->shdr.hdr.size = sizeof(*pkt);
	pkt->shdr.hdr.pkt_type = HFI_CMD_SESSION_FILL_BUFFER;
	pkt->shdr.session_id = hash32_ptr(cookie);

	if (out_frame->buffer_type == HFI_BUFFER_OUTPUT)
		pkt->stream_id = 0;
	else if (out_frame->buffer_type == HFI_BUFFER_OUTPUT2)
		pkt->stream_id = 1;

	pkt->output_tag = out_frame->clnt_data;
	pkt->packet_buffer = out_frame->device_addr;
	pkt->extradata_buffer = out_frame->extradata_addr;
	pkt->alloc_len = out_frame->alloc_len;
	pkt->filled_len = out_frame->filled_len;
	pkt->offset = out_frame->offset;
	pkt->data[0] = out_frame->extradata_size;

	return 0;
}

int pkt_session_parse_seq_header(
		struct hfi_session_parse_sequence_header_pkt *pkt,
		void *cookie, u32 seq_hdr, u32 seq_hdr_len)
{
	if (!cookie || !seq_hdr || !seq_hdr_len)
		return -EINVAL;

	pkt->shdr.hdr.size = sizeof(*pkt);
	pkt->shdr.hdr.pkt_type = HFI_CMD_SESSION_PARSE_SEQUENCE_HEADER;
	pkt->shdr.session_id = hash32_ptr(cookie);
	pkt->header_len = seq_hdr_len;
	pkt->packet_buffer = seq_hdr;

	return 0;
}

int pkt_session_get_seq_hdr(struct hfi_session_get_sequence_header_pkt *pkt,
			    void *cookie, u32 seq_hdr, u32 seq_hdr_len)
{
	if (!cookie || !seq_hdr || !seq_hdr_len)
		return -EINVAL;

	pkt->shdr.hdr.size = sizeof(*pkt);
	pkt->shdr.hdr.pkt_type = HFI_CMD_SESSION_GET_SEQUENCE_HEADER;
	pkt->shdr.session_id = hash32_ptr(cookie);
	pkt->buffer_len = seq_hdr_len;
	pkt->packet_buffer = seq_hdr;

	return 0;
}

int pkt_session_flush(struct hfi_session_flush_pkt *pkt, void *cookie, u32 type)
{
	switch (type) {
	case HFI_FLUSH_INPUT:
	case HFI_FLUSH_OUTPUT:
	case HFI_FLUSH_OUTPUT2:
	case HFI_FLUSH_ALL:
		break;
	default:
		return -EINVAL;
	}

	pkt->shdr.hdr.size = sizeof(*pkt);
	pkt->shdr.hdr.pkt_type = HFI_CMD_SESSION_FLUSH;
	pkt->shdr.session_id = hash32_ptr(cookie);
	pkt->flush_type = type;

	return 0;
}

static int pkt_session_get_property_1x(struct hfi_session_get_property_pkt *pkt,
				       void *cookie, u32 ptype)
{
	switch (ptype) {
	case HFI_PROPERTY_PARAM_PROFILE_LEVEL_CURRENT:
	case HFI_PROPERTY_CONFIG_BUFFER_REQUIREMENTS:
		break;
	default:
		return -EINVAL;
	}

	pkt->shdr.hdr.size = sizeof(*pkt);
	pkt->shdr.hdr.pkt_type = HFI_CMD_SESSION_GET_PROPERTY;
	pkt->shdr.session_id = hash32_ptr(cookie);
	pkt->num_properties = 1;
	pkt->data[0] = ptype;

	return 0;
}

static int pkt_session_set_property_1x(struct hfi_session_set_property_pkt *pkt,
				       void *cookie, u32 ptype, void *pdata)
{
	void *prop_data;
	int ret = 0;

	if (!pkt || !cookie || !pdata)
		return -EINVAL;

	prop_data = &pkt->data[1];

	pkt->shdr.hdr.size = sizeof(*pkt);
	pkt->shdr.hdr.pkt_type = HFI_CMD_SESSION_SET_PROPERTY;
	pkt->shdr.session_id = hash32_ptr(cookie);
	pkt->num_properties = 1;
	pkt->data[0] = ptype;

	switch (ptype) {
	case HFI_PROPERTY_CONFIG_FRAME_RATE: {
		struct hfi_framerate *in = pdata, *frate = prop_data;

		frate->buffer_type = in->buffer_type;
		frate->framerate = in->framerate;
		pkt->shdr.hdr.size += sizeof(u32) + sizeof(*frate);
		break;
	}
	case HFI_PROPERTY_PARAM_UNCOMPRESSED_FORMAT_SELECT: {
		struct hfi_uncompressed_format_select *in = pdata;
		struct hfi_uncompressed_format_select *hfi = prop_data;

		hfi->buffer_type = in->buffer_type;
		hfi->format = in->format;
		pkt->shdr.hdr.size += sizeof(u32) + sizeof(*hfi);
		break;
	}
	case HFI_PROPERTY_PARAM_FRAME_SIZE: {
		struct hfi_framesize *in = pdata, *fsize = prop_data;

		fsize->buffer_type = in->buffer_type;
		fsize->height = in->height;
		fsize->width = in->width;
		pkt->shdr.hdr.size += sizeof(u32) + sizeof(*fsize);
		break;
	}
	case HFI_PROPERTY_CONFIG_REALTIME: {
		struct hfi_enable *in = pdata, *en = prop_data;

		en->enable = in->enable;
		pkt->shdr.hdr.size += sizeof(u32) * 2;
		break;
	}
	case HFI_PROPERTY_PARAM_BUFFER_COUNT_ACTUAL: {
		struct hfi_buffer_count_actual *in = pdata, *count = prop_data;

		count->count_actual = in->count_actual;
		count->type = in->type;
		pkt->shdr.hdr.size += sizeof(u32) + sizeof(*count);
		break;
	}
	case HFI_PROPERTY_PARAM_BUFFER_SIZE_ACTUAL: {
		struct hfi_buffer_size_actual *in = pdata, *sz = prop_data;

		sz->size = in->size;
		sz->type = in->type;
		pkt->shdr.hdr.size += sizeof(u32) + sizeof(*sz);
		break;
	}
	case HFI_PROPERTY_PARAM_BUFFER_DISPLAY_HOLD_COUNT_ACTUAL: {
		struct hfi_buffer_display_hold_count_actual *in = pdata;
		struct hfi_buffer_display_hold_count_actual *count = prop_data;

		count->hold_count = in->hold_count;
		count->type = in->type;
		pkt->shdr.hdr.size += sizeof(u32) + sizeof(*count);
		break;
	}
	case HFI_PROPERTY_PARAM_NAL_STREAM_FORMAT_SELECT: {
		struct hfi_nal_stream_format_select *in = pdata;
		struct hfi_nal_stream_format_select *fmt = prop_data;

		fmt->format = in->format;
		pkt->shdr.hdr.size += sizeof(u32) + sizeof(*fmt);
		break;
	}
	case HFI_PROPERTY_PARAM_VDEC_OUTPUT_ORDER: {
		u32 *in = pdata;

		switch (*in) {
		case HFI_OUTPUT_ORDER_DECODE:
		case HFI_OUTPUT_ORDER_DISPLAY:
			break;
		default:
			ret = -EINVAL;
			break;
		}

		pkt->data[1] = *in;
		pkt->shdr.hdr.size += sizeof(u32) * 2;
		break;
	}
	case HFI_PROPERTY_PARAM_VDEC_PICTURE_TYPE_DECODE: {
		struct hfi_enable_picture *in = pdata, *en = prop_data;

		en->picture_type = in->picture_type;
		pkt->shdr.hdr.size += sizeof(u32) + sizeof(*en);
		break;
	}
	case HFI_PROPERTY_PARAM_VDEC_OUTPUT2_KEEP_ASPECT_RATIO: {
		struct hfi_enable *in = pdata, *en = prop_data;

		en->enable = in->enable;
		pkt->shdr.hdr.size += sizeof(u32) + sizeof(*en);
		break;
	}
	case HFI_PROPERTY_CONFIG_VDEC_POST_LOOP_DEBLOCKER: {
		struct hfi_enable *in = pdata;
		struct hfi_enable *en = prop_data;

		en->enable = in->enable;
		pkt->shdr.hdr.size += sizeof(u32) + sizeof(*en);
		break;
	}
	case HFI_PROPERTY_PARAM_VDEC_MULTI_STREAM: {
		struct hfi_multi_stream *in = pdata, *multi = prop_data;

		multi->buffer_type = in->buffer_type;
		multi->enable = in->enable;
		multi->width = in->width;
		multi->height = in->height;
		pkt->shdr.hdr.size += sizeof(u32) + sizeof(*multi);
		break;
	}
	case HFI_PROPERTY_PARAM_VDEC_DISPLAY_PICTURE_BUFFER_COUNT: {
		struct hfi_display_picture_buffer_count *in = pdata;
		struct hfi_display_picture_buffer_count *count = prop_data;

		count->count = in->count;
		count->enable = in->enable;
		pkt->shdr.hdr.size += sizeof(u32) + sizeof(*count);
		break;
	}
	case HFI_PROPERTY_PARAM_DIVX_FORMAT: {
		u32 *in = pdata;

		switch (*in) {
		case HFI_DIVX_FORMAT_4:
		case HFI_DIVX_FORMAT_5:
		case HFI_DIVX_FORMAT_6:
			break;
		default:
			ret = -EINVAL;
			break;
		}

		pkt->data[1] = *in;
		pkt->shdr.hdr.size += sizeof(u32) * 2;
		break;
	}
	case HFI_PROPERTY_CONFIG_VDEC_MB_ERROR_MAP_REPORTING: {
		struct hfi_enable *in = pdata, *en = prop_data;

		en->enable = in->enable;
		pkt->shdr.hdr.size += sizeof(u32) + sizeof(*en);
		break;
	}
	case HFI_PROPERTY_PARAM_VDEC_CONTINUE_DATA_TRANSFER: {
		struct hfi_enable *in = pdata, *en = prop_data;

		en->enable = in->enable;
		pkt->shdr.hdr.size += sizeof(u32) + sizeof(*en);
		break;
	}
	case HFI_PROPERTY_PARAM_VDEC_THUMBNAIL_MODE: {
		struct hfi_enable *in = pdata, *en = prop_data;

		en->enable = in->enable;
		pkt->shdr.hdr.size += sizeof(u32) + sizeof(*en);
		break;
	}
	case HFI_PROPERTY_CONFIG_VENC_SYNC_FRAME_SEQUENCE_HEADER: {
		struct hfi_enable *in = pdata, *en = prop_data;

		en->enable = in->enable;
		pkt->shdr.hdr.size += sizeof(u32) + sizeof(*en);
		break;
	}
	case HFI_PROPERTY_CONFIG_VENC_REQUEST_SYNC_FRAME:
		pkt->shdr.hdr.size += sizeof(u32);
		break;
	case HFI_PROPERTY_PARAM_VENC_MPEG4_SHORT_HEADER:
		break;
	case HFI_PROPERTY_PARAM_VENC_MPEG4_AC_PREDICTION:
		break;
	case HFI_PROPERTY_CONFIG_VENC_TARGET_BITRATE: {
		struct hfi_bitrate *in = pdata, *brate = prop_data;

		brate->bitrate = in->bitrate;
		brate->layer_id = in->layer_id;
		pkt->shdr.hdr.size += sizeof(u32) + sizeof(*brate);
		break;
	}
	case HFI_PROPERTY_CONFIG_VENC_MAX_BITRATE: {
		struct hfi_bitrate *in = pdata, *hfi = prop_data;

		hfi->bitrate = in->bitrate;
		hfi->layer_id = in->layer_id;
		pkt->shdr.hdr.size += sizeof(u32) + sizeof(*hfi);
		break;
	}
	case HFI_PROPERTY_PARAM_PROFILE_LEVEL_CURRENT: {
		struct hfi_profile_level *in = pdata, *pl = prop_data;

		pl->level = in->level;
		pl->profile = in->profile;
		if (pl->profile <= 0)
			/* Profile not supported, falling back to high */
			pl->profile = HFI_H264_PROFILE_HIGH;

		if (!pl->level)
			/* Level not supported, falling back to 1 */
			pl->level = 1;

		pkt->shdr.hdr.size += sizeof(u32) + sizeof(*pl);
		break;
	}
	case HFI_PROPERTY_PARAM_VENC_H264_ENTROPY_CONTROL: {
		struct hfi_h264_entropy_control *in = pdata, *hfi = prop_data;

		hfi->entropy_mode = in->entropy_mode;
		if (hfi->entropy_mode == HFI_H264_ENTROPY_CABAC)
			hfi->cabac_model = in->cabac_model;
		pkt->shdr.hdr.size += sizeof(u32) + sizeof(*hfi);
		break;
	}
	case HFI_PROPERTY_PARAM_VENC_RATE_CONTROL: {
		u32 *in = pdata;

		switch (*in) {
		case HFI_RATE_CONTROL_OFF:
		case HFI_RATE_CONTROL_CBR_CFR:
		case HFI_RATE_CONTROL_CBR_VFR:
		case HFI_RATE_CONTROL_VBR_CFR:
		case HFI_RATE_CONTROL_VBR_VFR:
		case HFI_RATE_CONTROL_CQ:
			break;
		default:
			ret = -EINVAL;
			break;
		}

		pkt->data[1] = *in;
		pkt->shdr.hdr.size += sizeof(u32) * 2;
		break;
	}
	case HFI_PROPERTY_PARAM_VENC_MPEG4_TIME_RESOLUTION: {
		struct hfi_mpeg4_time_resolution *in = pdata, *res = prop_data;

		res->time_increment_resolution = in->time_increment_resolution;
		pkt->shdr.hdr.size += sizeof(u32) + sizeof(*res);
		break;
	}
	case HFI_PROPERTY_PARAM_VENC_MPEG4_HEADER_EXTENSION: {
		struct hfi_mpeg4_header_extension *in = pdata, *ext = prop_data;

		ext->header_extension = in->header_extension;
		pkt->shdr.hdr.size += sizeof(u32) + sizeof(*ext);
		break;
	}
	case HFI_PROPERTY_PARAM_VENC_H264_DEBLOCK_CONTROL: {
		struct hfi_h264_db_control *in = pdata, *db = prop_data;

		switch (in->mode) {
		case HFI_H264_DB_MODE_DISABLE:
		case HFI_H264_DB_MODE_SKIP_SLICE_BOUNDARY:
		case HFI_H264_DB_MODE_ALL_BOUNDARY:
			break;
		default:
			ret = -EINVAL;
			break;
		}

		db->mode = in->mode;
		db->slice_alpha_offset = in->slice_alpha_offset;
		db->slice_beta_offset = in->slice_beta_offset;
		pkt->shdr.hdr.size += sizeof(u32) + sizeof(*db);
		break;
	}
	case HFI_PROPERTY_PARAM_VENC_SESSION_QP: {
		struct hfi_quantization *in = pdata, *quant = prop_data;

		quant->qp_i = in->qp_i;
		quant->qp_p = in->qp_p;
		quant->qp_b = in->qp_b;
		quant->layer_id = in->layer_id;
		pkt->shdr.hdr.size += sizeof(u32) + sizeof(*quant);
		break;
	}
	case HFI_PROPERTY_PARAM_VENC_SESSION_QP_RANGE: {
		struct hfi_quantization_range *in = pdata, *range = prop_data;
		u32 min_qp, max_qp;

		min_qp = in->min_qp;
		max_qp = in->max_qp;

		/* We'll be packing in the qp, so make sure we
		 * won't be losing data when masking
		 */
		if (min_qp > 0xff || max_qp > 0xff) {
			ret = -ERANGE;
			break;
		}

		/* When creating the packet, pack the qp value as
		 * 0xiippbb, where ii = qp range for I-frames,
		 * pp = qp range for P-frames, etc.
		 */
		range->min_qp = min_qp | min_qp << 8 | min_qp << 16;
		range->max_qp = max_qp | max_qp << 8 | max_qp << 16;
		range->layer_id = in->layer_id;

		pkt->shdr.hdr.size += sizeof(u32) + sizeof(*range);
		break;
	}
	case HFI_PROPERTY_PARAM_VENC_VC1_PERF_CFG: {
		struct hfi_vc1e_perf_cfg_type *in = pdata, *perf = prop_data;

		memcpy(perf->search_range_x_subsampled,
		       in->search_range_x_subsampled,
		       sizeof(perf->search_range_x_subsampled));
		memcpy(perf->search_range_y_subsampled,
		       in->search_range_y_subsampled,
		       sizeof(perf->search_range_y_subsampled));

		pkt->shdr.hdr.size += sizeof(u32) + sizeof(*perf);
		break;
	}
	case HFI_PROPERTY_PARAM_VENC_MAX_NUM_B_FRAMES: {
		struct hfi_max_num_b_frames *bframes = prop_data;
		u32 *in = pdata;

		bframes->max_num_b_frames = *in;
		pkt->shdr.hdr.size += sizeof(u32) + sizeof(*bframes);
		break;
	}
	case HFI_PROPERTY_CONFIG_VENC_INTRA_PERIOD: {
		struct hfi_intra_period *in = pdata, *intra = prop_data;

		intra->pframes = in->pframes;
		intra->bframes = in->bframes;
		pkt->shdr.hdr.size += sizeof(u32) + sizeof(*intra);
		break;
	}
	case HFI_PROPERTY_CONFIG_VENC_IDR_PERIOD: {
		struct hfi_idr_period *in = pdata, *idr = prop_data;

		idr->idr_period = in->idr_period;
		pkt->shdr.hdr.size += sizeof(u32) + sizeof(*idr);
		break;
	}
	case HFI_PROPERTY_PARAM_VDEC_CONCEAL_COLOR: {
		struct hfi_conceal_color *color = prop_data;
		u32 *in = pdata;

		color->conceal_color = *in & 0xff;
		color->conceal_color |= ((*in >> 10) & 0xff) << 8;
		color->conceal_color |= ((*in >> 20) & 0xff) << 16;
		pkt->shdr.hdr.size += sizeof(u32) + sizeof(*color);
		break;
	}
	case HFI_PROPERTY_CONFIG_VPE_OPERATIONS: {
		struct hfi_operations_type *in = pdata, *ops = prop_data;

		switch (in->rotation) {
		case HFI_ROTATE_NONE:
		case HFI_ROTATE_90:
		case HFI_ROTATE_180:
		case HFI_ROTATE_270:
			break;
		default:
			ret = -EINVAL;
			break;
		}

		switch (in->flip) {
		case HFI_FLIP_NONE:
		case HFI_FLIP_HORIZONTAL:
		case HFI_FLIP_VERTICAL:
			break;
		default:
			ret = -EINVAL;
			break;
		}

		ops->rotation = in->rotation;
		ops->flip = in->flip;
		pkt->shdr.hdr.size += sizeof(u32) + sizeof(*ops);
		break;
	}
	case HFI_PROPERTY_PARAM_VENC_INTRA_REFRESH: {
		struct hfi_intra_refresh *in = pdata, *intra = prop_data;

		switch (in->mode) {
		case HFI_INTRA_REFRESH_NONE:
		case HFI_INTRA_REFRESH_ADAPTIVE:
		case HFI_INTRA_REFRESH_CYCLIC:
		case HFI_INTRA_REFRESH_CYCLIC_ADAPTIVE:
		case HFI_INTRA_REFRESH_RANDOM:
			break;
		default:
			ret = -EINVAL;
			break;
		}

		intra->mode = in->mode;
		intra->air_mbs = in->air_mbs;
		intra->air_ref = in->air_ref;
		intra->cir_mbs = in->cir_mbs;
		pkt->shdr.hdr.size += sizeof(u32) + sizeof(*intra);
		break;
	}
	case HFI_PROPERTY_PARAM_VENC_MULTI_SLICE_CONTROL: {
		struct hfi_multi_slice_control *in = pdata, *multi = prop_data;

		switch (in->multi_slice) {
		case HFI_MULTI_SLICE_OFF:
		case HFI_MULTI_SLICE_GOB:
		case HFI_MULTI_SLICE_BY_MB_COUNT:
		case HFI_MULTI_SLICE_BY_BYTE_COUNT:
			break;
		default:
			ret = -EINVAL;
			break;
		}

		multi->multi_slice = in->multi_slice;
		multi->slice_size = in->slice_size;
		pkt->shdr.hdr.size += sizeof(u32) + sizeof(*multi);
		break;
	}
	case HFI_PROPERTY_PARAM_VENC_SLICE_DELIVERY_MODE: {
		struct hfi_enable *in = pdata, *en = prop_data;

		en->enable = in->enable;
		pkt->shdr.hdr.size += sizeof(u32) + sizeof(*en);
		break;
	}
	case HFI_PROPERTY_PARAM_VENC_H264_VUI_TIMING_INFO: {
		struct hfi_h264_vui_timing_info *in = pdata, *vui = prop_data;

		vui->enable = in->enable;
		vui->fixed_framerate = in->fixed_framerate;
		vui->time_scale = in->time_scale;
		pkt->shdr.hdr.size += sizeof(u32) + sizeof(*vui);
		break;
	}
	case HFI_PROPERTY_CONFIG_VPE_DEINTERLACE: {
		struct hfi_enable *in = pdata, *en = prop_data;

		en->enable = in->enable;
		pkt->shdr.hdr.size += sizeof(u32) + sizeof(*en);
		break;
	}
	case HFI_PROPERTY_PARAM_VENC_H264_GENERATE_AUDNAL: {
		struct hfi_enable *in = pdata, *en = prop_data;

		en->enable = in->enable;
		pkt->shdr.hdr.size += sizeof(u32) + sizeof(*en);
		break;
	}
	case HFI_PROPERTY_PARAM_BUFFER_ALLOC_MODE: {
		struct hfi_buffer_alloc_mode *in = pdata, *mode = prop_data;

		mode->type = in->type;
		mode->mode = in->mode;
		pkt->shdr.hdr.size += sizeof(u32) + sizeof(*mode);
		break;
	}
	case HFI_PROPERTY_PARAM_VDEC_FRAME_ASSEMBLY: {
		struct hfi_enable *in = pdata, *en = prop_data;

		en->enable = in->enable;
		pkt->shdr.hdr.size += sizeof(u32) + sizeof(*en);
		break;
	}
	case HFI_PROPERTY_PARAM_VENC_H264_VUI_BITSTREAM_RESTRC: {
		struct hfi_enable *in = pdata, *en = prop_data;

		en->enable = in->enable;
		pkt->shdr.hdr.size += sizeof(u32) + sizeof(*en);
		break;
	}
	case HFI_PROPERTY_PARAM_VENC_PRESERVE_TEXT_QUALITY: {
		struct hfi_enable *in = pdata, *en = prop_data;

		en->enable = in->enable;
		pkt->shdr.hdr.size += sizeof(u32) + sizeof(*en);
		break;
	}
	case HFI_PROPERTY_PARAM_VDEC_SCS_THRESHOLD: {
		struct hfi_scs_threshold *thres = prop_data;
		u32 *in = pdata;

		thres->threshold_value = *in;
		pkt->shdr.hdr.size += sizeof(u32) + sizeof(*thres);
		break;
	}
	case HFI_PROPERTY_PARAM_MVC_BUFFER_LAYOUT: {
		struct hfi_mvc_buffer_layout_descp_type *in = pdata;
		struct hfi_mvc_buffer_layout_descp_type *mvc = prop_data;

		switch (in->layout_type) {
		case HFI_MVC_BUFFER_LAYOUT_TOP_BOTTOM:
		case HFI_MVC_BUFFER_LAYOUT_SEQ:
			break;
		default:
			ret = -EINVAL;
			break;
		}

		mvc->layout_type = in->layout_type;
		mvc->bright_view_first = in->bright_view_first;
		mvc->ngap = in->ngap;
		pkt->shdr.hdr.size += sizeof(u32) + sizeof(*mvc);
		break;
	}
	case HFI_PROPERTY_PARAM_VENC_LTRMODE: {
		struct hfi_ltr_mode *in = pdata, *ltr = prop_data;

		switch (in->ltr_mode) {
		case HFI_LTR_MODE_DISABLE:
		case HFI_LTR_MODE_MANUAL:
		case HFI_LTR_MODE_PERIODIC:
			break;
		default:
			ret = -EINVAL;
			break;
		}

		ltr->ltr_mode = in->ltr_mode;
		ltr->ltr_count = in->ltr_count;
		ltr->trust_mode = in->trust_mode;
		pkt->shdr.hdr.size += sizeof(u32) + sizeof(*ltr);
		break;
	}
	case HFI_PROPERTY_CONFIG_VENC_USELTRFRAME: {
		struct hfi_ltr_use *in = pdata, *ltr_use = prop_data;

		ltr_use->frames = in->frames;
		ltr_use->ref_ltr = in->ref_ltr;
		ltr_use->use_constrnt = in->use_constrnt;
		pkt->shdr.hdr.size += sizeof(u32) + sizeof(*ltr_use);
		break;
	}
	case HFI_PROPERTY_CONFIG_VENC_MARKLTRFRAME: {
		struct hfi_ltr_mark *in = pdata, *ltr_mark = prop_data;

		ltr_mark->mark_frame = in->mark_frame;
		pkt->shdr.hdr.size += sizeof(u32) + sizeof(*ltr_mark);
		break;
	}
	case HFI_PROPERTY_PARAM_VENC_HIER_P_MAX_NUM_ENH_LAYER: {
		u32 *in = pdata;

		pkt->data[1] = *in;
		pkt->shdr.hdr.size += sizeof(u32) * 2;
		break;
	}
	case HFI_PROPERTY_CONFIG_VENC_HIER_P_ENH_LAYER: {
		u32 *in = pdata;

		pkt->data[1] = *in;
		pkt->shdr.hdr.size += sizeof(u32) * 2;
		break;
	}
	case HFI_PROPERTY_PARAM_VENC_DISABLE_RC_TIMESTAMP: {
		struct hfi_enable *in = pdata, *en = prop_data;

		en->enable = in->enable;
		pkt->shdr.hdr.size += sizeof(u32) + sizeof(*en);
		break;
	}
	case HFI_PROPERTY_PARAM_VENC_INITIAL_QP: {
		struct hfi_initial_quantization *in = pdata, *quant = prop_data;

		quant->init_qp_enable = in->init_qp_enable;
		quant->qp_i = in->qp_i;
		quant->qp_p = in->qp_p;
		quant->qp_b = in->qp_b;
		pkt->shdr.hdr.size += sizeof(u32) + sizeof(*quant);
		break;
	}
	case HFI_PROPERTY_PARAM_VPE_COLOR_SPACE_CONVERSION: {
		struct hfi_vpe_color_space_conversion *in = pdata;
		struct hfi_vpe_color_space_conversion *csc = prop_data;

		memcpy(csc->csc_matrix, in->csc_matrix,
		       sizeof(csc->csc_matrix));
		memcpy(csc->csc_bias, in->csc_bias, sizeof(csc->csc_bias));
		memcpy(csc->csc_limit, in->csc_limit, sizeof(csc->csc_limit));
		pkt->shdr.hdr.size += sizeof(u32) + sizeof(*csc);
		break;
	}
	case HFI_PROPERTY_PARAM_VENC_VPX_ERROR_RESILIENCE_MODE: {
		struct hfi_enable *in = pdata, *en = prop_data;

		en->enable = in->enable;
		pkt->shdr.hdr.size += sizeof(u32) + sizeof(*en);
		break;
	}
	case HFI_PROPERTY_PARAM_VENC_H264_NAL_SVC_EXT: {
		struct hfi_enable *in = pdata, *en = prop_data;

		en->enable = in->enable;
		pkt->shdr.hdr.size += sizeof(u32) + sizeof(*en);
		break;
	}
	case HFI_PROPERTY_CONFIG_VENC_PERF_MODE: {
		u32 *in = pdata;

		pkt->data[1] = *in;
		pkt->shdr.hdr.size += sizeof(u32) * 2;
		break;
	}
	case HFI_PROPERTY_PARAM_VENC_HIER_B_MAX_NUM_ENH_LAYER: {
		u32 *in = pdata;

		pkt->data[1] = *in;
		pkt->shdr.hdr.size += sizeof(u32) * 2;
		break;
	}
	case HFI_PROPERTY_PARAM_VDEC_NONCP_OUTPUT2: {
		struct hfi_enable *in = pdata, *en = prop_data;

		en->enable = in->enable;
		pkt->shdr.hdr.size += sizeof(u32) + sizeof(*en);
		break;
	}
	case HFI_PROPERTY_PARAM_VENC_HIER_P_HYBRID_MODE: {
		struct hfi_hybrid_hierp *in = pdata, *hierp = prop_data;

		hierp->layers = in->layers;
		pkt->shdr.hdr.size += sizeof(u32) + sizeof(*hierp);
		break;
	}
	case HFI_PROPERTY_PARAM_UNCOMPRESSED_PLANE_ACTUAL_INFO: {
		struct hfi_uncompressed_plane_actual_info *in = pdata;
		struct hfi_uncompressed_plane_actual_info *info = prop_data;

		info->buffer_type = in->buffer_type;
		info->num_planes = in->num_planes;
		info->plane_format[0] = in->plane_format[0];
		if (in->num_planes > 1)
			info->plane_format[1] = in->plane_format[1];
		pkt->shdr.hdr.size += sizeof(u32) + sizeof(*info);
		break;
	}

	/* FOLLOWING PROPERTIES ARE NOT IMPLEMENTED IN CORE YET */
	case HFI_PROPERTY_CONFIG_BUFFER_REQUIREMENTS:
	case HFI_PROPERTY_CONFIG_PRIORITY:
	case HFI_PROPERTY_CONFIG_BATCH_INFO:
	case HFI_PROPERTY_SYS_IDLE_INDICATOR:
	case HFI_PROPERTY_PARAM_UNCOMPRESSED_FORMAT_SUPPORTED:
	case HFI_PROPERTY_PARAM_INTERLACE_FORMAT_SUPPORTED:
	case HFI_PROPERTY_PARAM_CHROMA_SITE:
	case HFI_PROPERTY_PARAM_PROPERTIES_SUPPORTED:
	case HFI_PROPERTY_PARAM_PROFILE_LEVEL_SUPPORTED:
	case HFI_PROPERTY_PARAM_CAPABILITY_SUPPORTED:
	case HFI_PROPERTY_PARAM_NAL_STREAM_FORMAT_SUPPORTED:
	case HFI_PROPERTY_PARAM_MULTI_VIEW_FORMAT:
	case HFI_PROPERTY_PARAM_MAX_SEQUENCE_HEADER_SIZE:
	case HFI_PROPERTY_PARAM_CODEC_SUPPORTED:
	case HFI_PROPERTY_PARAM_VDEC_MULTI_VIEW_SELECT:
	case HFI_PROPERTY_PARAM_VDEC_MB_QUANTIZATION:
	case HFI_PROPERTY_PARAM_VDEC_NUM_CONCEALED_MB:
	case HFI_PROPERTY_PARAM_VDEC_H264_ENTROPY_SWITCHING:
	case HFI_PROPERTY_PARAM_VENC_MULTI_SLICE_INFO:
	default:
		return -EINVAL;
	}

	return ret;
}

static int
pkt_session_get_property_3xx(struct hfi_session_get_property_pkt *pkt,
			     void *cookie, u32 ptype)
{
	int ret = 0;

	if (!pkt || !cookie)
		return -EINVAL;

	pkt->shdr.hdr.size = sizeof(struct hfi_session_get_property_pkt);
	pkt->shdr.hdr.pkt_type = HFI_CMD_SESSION_GET_PROPERTY;
	pkt->shdr.session_id = hash32_ptr(cookie);
	pkt->num_properties = 1;

	switch (ptype) {
	case HFI_PROPERTY_CONFIG_VDEC_ENTROPY:
		pkt->data[0] = HFI_PROPERTY_CONFIG_VDEC_ENTROPY;
		break;
	default:
		ret = pkt_session_get_property_1x(pkt, cookie, ptype);
		break;
	}

	return ret;
}

static int
pkt_session_set_property_3xx(struct hfi_session_set_property_pkt *pkt,
			     void *cookie, u32 ptype, void *pdata)
{
	void *prop_data;
	int ret = 0;

	if (!pkt || !cookie || !pdata)
		return -EINVAL;

	prop_data = &pkt->data[1];

	pkt->shdr.hdr.size = sizeof(*pkt);
	pkt->shdr.hdr.pkt_type = HFI_CMD_SESSION_SET_PROPERTY;
	pkt->shdr.session_id = hash32_ptr(cookie);
	pkt->num_properties = 1;
	pkt->data[0] = ptype;

	/*
	 * Any session set property which is different in 3XX packetization
	 * should be added as a new case below. All unchanged session set
	 * properties will be handled in the default case.
	 */
	switch (ptype) {
	case HFI_PROPERTY_PARAM_VDEC_MULTI_STREAM: {
		struct hfi_multi_stream *in = pdata;
		struct hfi_multi_stream_3x *multi = prop_data;

		multi->buffer_type = in->buffer_type;
		multi->enable = in->enable;
		pkt->shdr.hdr.size += sizeof(u32) + sizeof(*multi);
		break;
	}
	case HFI_PROPERTY_PARAM_VENC_INTRA_REFRESH: {
		struct hfi_intra_refresh *in = pdata;
		struct hfi_intra_refresh_3x *intra = prop_data;

		switch (in->mode) {
		case HFI_INTRA_REFRESH_NONE:
		case HFI_INTRA_REFRESH_ADAPTIVE:
		case HFI_INTRA_REFRESH_CYCLIC:
		case HFI_INTRA_REFRESH_CYCLIC_ADAPTIVE:
		case HFI_INTRA_REFRESH_RANDOM:
			break;
		default:
			ret = -EINVAL;
			break;
		}

		intra->mode = in->mode;
		intra->mbs = in->cir_mbs;
		pkt->shdr.hdr.size += sizeof(u32) + sizeof(*intra);
		break;
	}
	case HFI_PROPERTY_PARAM_VDEC_CONTINUE_DATA_TRANSFER:
		/* for 3xx fw version session_continue is used */
		break;
	default:
		ret = pkt_session_set_property_1x(pkt, cookie, ptype, pdata);
		break;
	}

	return ret;
}

static int
pkt_session_set_property_4xx(struct hfi_session_set_property_pkt *pkt,
			     void *cookie, u32 ptype, void *pdata)
{
	void *prop_data;

	if (!pkt || !cookie || !pdata)
		return -EINVAL;

	prop_data = &pkt->data[1];

	pkt->shdr.hdr.size = sizeof(*pkt);
	pkt->shdr.hdr.pkt_type = HFI_CMD_SESSION_SET_PROPERTY;
	pkt->shdr.session_id = hash32_ptr(cookie);
	pkt->num_properties = 1;
	pkt->data[0] = ptype;

	/*
	 * Any session set property which is different in 3XX packetization
	 * should be added as a new case below. All unchanged session set
	 * properties will be handled in the default case.
	 */
	switch (ptype) {
	case HFI_PROPERTY_PARAM_BUFFER_COUNT_ACTUAL: {
		struct hfi_buffer_count_actual *in = pdata;
		struct hfi_buffer_count_actual_4xx *count = prop_data;

		count->count_actual = in->count_actual;
		count->type = in->type;
		count->count_min_host = in->count_actual;
		pkt->shdr.hdr.size += sizeof(u32) + sizeof(*count);
		break;
	}
	case HFI_PROPERTY_PARAM_WORK_MODE: {
		struct hfi_video_work_mode *in = pdata, *wm = prop_data;

		wm->video_work_mode = in->video_work_mode;
		pkt->shdr.hdr.size += sizeof(u32) + sizeof(*wm);
		break;
	}
	case HFI_PROPERTY_CONFIG_VIDEOCORES_USAGE: {
		struct hfi_videocores_usage_type *in = pdata, *cu = prop_data;

		cu->video_core_enable_mask = in->video_core_enable_mask;
		pkt->shdr.hdr.size += sizeof(u32) + sizeof(*cu);
		break;
	}
	case HFI_PROPERTY_PARAM_VENC_HDR10_PQ_SEI: {
		struct hfi_hdr10_pq_sei *in = pdata, *hdr10 = prop_data;

		memcpy(hdr10, in, sizeof(*hdr10));
		pkt->shdr.hdr.size += sizeof(u32) + sizeof(*hdr10);
		break;
	}
	case HFI_PROPERTY_PARAM_VDEC_CONCEAL_COLOR: {
		struct hfi_conceal_color_v4 *color = prop_data;
		u32 *in = pdata;

		color->conceal_color_8bit = *in & 0xff;
		color->conceal_color_8bit |= ((*in >> 10) & 0xff) << 8;
		color->conceal_color_8bit |= ((*in >> 20) & 0xff) << 16;
		color->conceal_color_10bit = *in;
		pkt->shdr.hdr.size += sizeof(u32) + sizeof(*color);
		break;
	}

	case HFI_PROPERTY_CONFIG_VENC_MAX_BITRATE:
	case HFI_PROPERTY_CONFIG_VDEC_POST_LOOP_DEBLOCKER:
	case HFI_PROPERTY_PARAM_BUFFER_ALLOC_MODE:
	case HFI_PROPERTY_PARAM_VENC_SESSION_QP:
	case HFI_PROPERTY_PARAM_VENC_SESSION_QP_RANGE:
		/* not implemented on Venus 4xx */
		return -ENOTSUPP;
	default:
		return pkt_session_set_property_3xx(pkt, cookie, ptype, pdata);
	}

	return 0;
}

static int
pkt_session_set_property_6xx(struct hfi_session_set_property_pkt *pkt,
			     void *cookie, u32 ptype, void *pdata)
{
	void *prop_data;

	if (!pkt || !cookie || !pdata)
		return -EINVAL;

	prop_data = &pkt->data[1];

	pkt->shdr.hdr.size = sizeof(*pkt);
	pkt->shdr.hdr.pkt_type = HFI_CMD_SESSION_SET_PROPERTY;
	pkt->shdr.session_id = hash32_ptr(cookie);
	pkt->num_properties = 1;
	pkt->data[0] = ptype;

	switch (ptype) {
	case HFI_PROPERTY_PARAM_UNCOMPRESSED_PLANE_ACTUAL_CONSTRAINTS_INFO: {
		struct hfi_uncompressed_plane_actual_constraints_info *in = pdata;
		struct hfi_uncompressed_plane_actual_constraints_info *info = prop_data;

		info->buffer_type = in->buffer_type;
		info->num_planes = in->num_planes;
		info->plane_format[0] = in->plane_format[0];
		if (in->num_planes > 1)
			info->plane_format[1] = in->plane_format[1];

		pkt->shdr.hdr.size += sizeof(u32) + sizeof(*info);
		break;
	}
	case HFI_PROPERTY_CONFIG_HEIC_FRAME_QUALITY: {
		struct hfi_heic_frame_quality *in = pdata, *cq = prop_data;

		cq->frame_quality = in->frame_quality;
		pkt->shdr.hdr.size += sizeof(u32) + sizeof(*cq);
		break;
	}
	default:
		return pkt_session_set_property_4xx(pkt, cookie, ptype, pdata);
	}

	return 0;
}

int pkt_session_get_property(struct hfi_session_get_property_pkt *pkt,
			     void *cookie, u32 ptype)
{
	if (hfi_ver == HFI_VERSION_1XX)
		return pkt_session_get_property_1x(pkt, cookie, ptype);

	return pkt_session_get_property_3xx(pkt, cookie, ptype);
}

int pkt_session_set_property(struct hfi_session_set_property_pkt *pkt,
			     void *cookie, u32 ptype, void *pdata)
{
	if (hfi_ver == HFI_VERSION_1XX)
		return pkt_session_set_property_1x(pkt, cookie, ptype, pdata);

	if (hfi_ver == HFI_VERSION_3XX)
		return pkt_session_set_property_3xx(pkt, cookie, ptype, pdata);

	if (hfi_ver == HFI_VERSION_4XX)
		return pkt_session_set_property_4xx(pkt, cookie, ptype, pdata);

	return pkt_session_set_property_6xx(pkt, cookie, ptype, pdata);
}

void pkt_set_version(enum hfi_version version)
{
	hfi_ver = version;
}
