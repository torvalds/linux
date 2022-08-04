// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012-2016, The Linux Foundation. All rights reserved.
 * Copyright (C) 2017 Linaro Ltd.
 */
#include <linux/idr.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <media/videobuf2-dma-contig.h>
#include <media/v4l2-mem2mem.h>
#include <asm/div64.h>

#include "core.h"
#include "helpers.h"
#include "hfi_helper.h"
#include "pm_helpers.h"
#include "hfi_platform.h"
#include "hfi_parser.h"

#define NUM_MBS_720P	(((ALIGN(1280, 16)) >> 4) * ((ALIGN(736, 16)) >> 4))
#define NUM_MBS_4K	(((ALIGN(4096, 16)) >> 4) * ((ALIGN(2304, 16)) >> 4))

enum dpb_buf_owner {
	DRIVER,
	FIRMWARE,
};

struct intbuf {
	struct list_head list;
	u32 type;
	size_t size;
	void *va;
	dma_addr_t da;
	unsigned long attrs;
	enum dpb_buf_owner owned_by;
	u32 dpb_out_tag;
};

bool venus_helper_check_codec(struct venus_inst *inst, u32 v4l2_pixfmt)
{
	struct venus_core *core = inst->core;
	u32 session_type = inst->session_type;
	u32 codec;

	switch (v4l2_pixfmt) {
	case V4L2_PIX_FMT_H264:
		codec = HFI_VIDEO_CODEC_H264;
		break;
	case V4L2_PIX_FMT_H263:
		codec = HFI_VIDEO_CODEC_H263;
		break;
	case V4L2_PIX_FMT_MPEG1:
		codec = HFI_VIDEO_CODEC_MPEG1;
		break;
	case V4L2_PIX_FMT_MPEG2:
		codec = HFI_VIDEO_CODEC_MPEG2;
		break;
	case V4L2_PIX_FMT_MPEG4:
		codec = HFI_VIDEO_CODEC_MPEG4;
		break;
	case V4L2_PIX_FMT_VC1_ANNEX_G:
	case V4L2_PIX_FMT_VC1_ANNEX_L:
		codec = HFI_VIDEO_CODEC_VC1;
		break;
	case V4L2_PIX_FMT_VP8:
		codec = HFI_VIDEO_CODEC_VP8;
		break;
	case V4L2_PIX_FMT_VP9:
		codec = HFI_VIDEO_CODEC_VP9;
		break;
	case V4L2_PIX_FMT_XVID:
		codec = HFI_VIDEO_CODEC_DIVX;
		break;
	case V4L2_PIX_FMT_HEVC:
		codec = HFI_VIDEO_CODEC_HEVC;
		break;
	default:
		return false;
	}

	if (session_type == VIDC_SESSION_TYPE_ENC && core->enc_codecs & codec)
		return true;

	if (session_type == VIDC_SESSION_TYPE_DEC && core->dec_codecs & codec)
		return true;

	return false;
}
EXPORT_SYMBOL_GPL(venus_helper_check_codec);

static void free_dpb_buf(struct venus_inst *inst, struct intbuf *buf)
{
	ida_free(&inst->dpb_ids, buf->dpb_out_tag);

	list_del_init(&buf->list);
	dma_free_attrs(inst->core->dev, buf->size, buf->va, buf->da,
		       buf->attrs);
	kfree(buf);
}

int venus_helper_queue_dpb_bufs(struct venus_inst *inst)
{
	struct intbuf *buf, *next;
	unsigned int dpb_size = 0;
	int ret = 0;

	if (inst->dpb_buftype == HFI_BUFFER_OUTPUT)
		dpb_size = inst->output_buf_size;
	else if (inst->dpb_buftype == HFI_BUFFER_OUTPUT2)
		dpb_size = inst->output2_buf_size;

	list_for_each_entry_safe(buf, next, &inst->dpbbufs, list) {
		struct hfi_frame_data fdata;

		memset(&fdata, 0, sizeof(fdata));
		fdata.alloc_len = buf->size;
		fdata.device_addr = buf->da;
		fdata.buffer_type = buf->type;

		if (buf->owned_by == FIRMWARE)
			continue;

		/* free buffer from previous sequence which was released later */
		if (dpb_size > buf->size) {
			free_dpb_buf(inst, buf);
			continue;
		}

		fdata.clnt_data = buf->dpb_out_tag;

		ret = hfi_session_process_buf(inst, &fdata);
		if (ret)
			goto fail;

		buf->owned_by = FIRMWARE;
	}

fail:
	return ret;
}
EXPORT_SYMBOL_GPL(venus_helper_queue_dpb_bufs);

int venus_helper_free_dpb_bufs(struct venus_inst *inst)
{
	struct intbuf *buf, *n;

	list_for_each_entry_safe(buf, n, &inst->dpbbufs, list) {
		if (buf->owned_by == FIRMWARE)
			continue;
		free_dpb_buf(inst, buf);
	}

	if (list_empty(&inst->dpbbufs))
		INIT_LIST_HEAD(&inst->dpbbufs);

	return 0;
}
EXPORT_SYMBOL_GPL(venus_helper_free_dpb_bufs);

int venus_helper_alloc_dpb_bufs(struct venus_inst *inst)
{
	struct venus_core *core = inst->core;
	struct device *dev = core->dev;
	enum hfi_version ver = core->res->hfi_version;
	struct hfi_buffer_requirements bufreq;
	u32 buftype = inst->dpb_buftype;
	unsigned int dpb_size = 0;
	struct intbuf *buf;
	unsigned int i;
	u32 count;
	int ret;
	int id;

	/* no need to allocate dpb buffers */
	if (!inst->dpb_fmt)
		return 0;

	if (inst->dpb_buftype == HFI_BUFFER_OUTPUT)
		dpb_size = inst->output_buf_size;
	else if (inst->dpb_buftype == HFI_BUFFER_OUTPUT2)
		dpb_size = inst->output2_buf_size;

	if (!dpb_size)
		return 0;

	ret = venus_helper_get_bufreq(inst, buftype, &bufreq);
	if (ret)
		return ret;

	count = HFI_BUFREQ_COUNT_MIN(&bufreq, ver);

	for (i = 0; i < count; i++) {
		buf = kzalloc(sizeof(*buf), GFP_KERNEL);
		if (!buf) {
			ret = -ENOMEM;
			goto fail;
		}

		buf->type = buftype;
		buf->size = dpb_size;
		buf->attrs = DMA_ATTR_WRITE_COMBINE |
			     DMA_ATTR_NO_KERNEL_MAPPING;
		buf->va = dma_alloc_attrs(dev, buf->size, &buf->da, GFP_KERNEL,
					  buf->attrs);
		if (!buf->va) {
			ret = -ENOMEM;
			goto fail;
		}
		buf->owned_by = DRIVER;

		id = ida_alloc_min(&inst->dpb_ids, VB2_MAX_FRAME, GFP_KERNEL);
		if (id < 0) {
			ret = id;
			goto fail;
		}

		buf->dpb_out_tag = id;

		list_add_tail(&buf->list, &inst->dpbbufs);
	}

	return 0;

fail:
	kfree(buf);
	venus_helper_free_dpb_bufs(inst);
	return ret;
}
EXPORT_SYMBOL_GPL(venus_helper_alloc_dpb_bufs);

static int intbufs_set_buffer(struct venus_inst *inst, u32 type)
{
	struct venus_core *core = inst->core;
	struct device *dev = core->dev;
	struct hfi_buffer_requirements bufreq;
	struct hfi_buffer_desc bd;
	struct intbuf *buf;
	unsigned int i;
	int ret;

	ret = venus_helper_get_bufreq(inst, type, &bufreq);
	if (ret)
		return 0;

	if (!bufreq.size)
		return 0;

	for (i = 0; i < bufreq.count_actual; i++) {
		buf = kzalloc(sizeof(*buf), GFP_KERNEL);
		if (!buf) {
			ret = -ENOMEM;
			goto fail;
		}

		buf->type = bufreq.type;
		buf->size = bufreq.size;
		buf->attrs = DMA_ATTR_WRITE_COMBINE |
			     DMA_ATTR_NO_KERNEL_MAPPING;
		buf->va = dma_alloc_attrs(dev, buf->size, &buf->da, GFP_KERNEL,
					  buf->attrs);
		if (!buf->va) {
			ret = -ENOMEM;
			goto fail;
		}

		memset(&bd, 0, sizeof(bd));
		bd.buffer_size = buf->size;
		bd.buffer_type = buf->type;
		bd.num_buffers = 1;
		bd.device_addr = buf->da;

		ret = hfi_session_set_buffers(inst, &bd);
		if (ret) {
			dev_err(dev, "set session buffers failed\n");
			goto dma_free;
		}

		list_add_tail(&buf->list, &inst->internalbufs);
	}

	return 0;

dma_free:
	dma_free_attrs(dev, buf->size, buf->va, buf->da, buf->attrs);
fail:
	kfree(buf);
	return ret;
}

static int intbufs_unset_buffers(struct venus_inst *inst)
{
	struct hfi_buffer_desc bd = {0};
	struct intbuf *buf, *n;
	int ret = 0;

	list_for_each_entry_safe(buf, n, &inst->internalbufs, list) {
		bd.buffer_size = buf->size;
		bd.buffer_type = buf->type;
		bd.num_buffers = 1;
		bd.device_addr = buf->da;
		bd.response_required = true;

		ret = hfi_session_unset_buffers(inst, &bd);

		list_del_init(&buf->list);
		dma_free_attrs(inst->core->dev, buf->size, buf->va, buf->da,
			       buf->attrs);
		kfree(buf);
	}

	return ret;
}

static const unsigned int intbuf_types_1xx[] = {
	HFI_BUFFER_INTERNAL_SCRATCH(HFI_VERSION_1XX),
	HFI_BUFFER_INTERNAL_SCRATCH_1(HFI_VERSION_1XX),
	HFI_BUFFER_INTERNAL_SCRATCH_2(HFI_VERSION_1XX),
	HFI_BUFFER_INTERNAL_PERSIST,
	HFI_BUFFER_INTERNAL_PERSIST_1,
};

static const unsigned int intbuf_types_4xx[] = {
	HFI_BUFFER_INTERNAL_SCRATCH(HFI_VERSION_4XX),
	HFI_BUFFER_INTERNAL_SCRATCH_1(HFI_VERSION_4XX),
	HFI_BUFFER_INTERNAL_SCRATCH_2(HFI_VERSION_4XX),
	HFI_BUFFER_INTERNAL_PERSIST,
	HFI_BUFFER_INTERNAL_PERSIST_1,
};

static const unsigned int intbuf_types_6xx[] = {
	HFI_BUFFER_INTERNAL_SCRATCH(HFI_VERSION_6XX),
	HFI_BUFFER_INTERNAL_SCRATCH_1(HFI_VERSION_6XX),
	HFI_BUFFER_INTERNAL_SCRATCH_2(HFI_VERSION_6XX),
	HFI_BUFFER_INTERNAL_PERSIST,
	HFI_BUFFER_INTERNAL_PERSIST_1,
};

int venus_helper_intbufs_alloc(struct venus_inst *inst)
{
	const unsigned int *intbuf;
	size_t arr_sz, i;
	int ret;

	if (IS_V6(inst->core)) {
		arr_sz = ARRAY_SIZE(intbuf_types_6xx);
		intbuf = intbuf_types_6xx;
	} else if (IS_V4(inst->core)) {
		arr_sz = ARRAY_SIZE(intbuf_types_4xx);
		intbuf = intbuf_types_4xx;
	} else {
		arr_sz = ARRAY_SIZE(intbuf_types_1xx);
		intbuf = intbuf_types_1xx;
	}

	for (i = 0; i < arr_sz; i++) {
		ret = intbufs_set_buffer(inst, intbuf[i]);
		if (ret)
			goto error;
	}

	return 0;

error:
	intbufs_unset_buffers(inst);
	return ret;
}
EXPORT_SYMBOL_GPL(venus_helper_intbufs_alloc);

int venus_helper_intbufs_free(struct venus_inst *inst)
{
	return intbufs_unset_buffers(inst);
}
EXPORT_SYMBOL_GPL(venus_helper_intbufs_free);

int venus_helper_intbufs_realloc(struct venus_inst *inst)
{
	enum hfi_version ver = inst->core->res->hfi_version;
	struct hfi_buffer_desc bd;
	struct intbuf *buf, *n;
	int ret;

	list_for_each_entry_safe(buf, n, &inst->internalbufs, list) {
		if (buf->type == HFI_BUFFER_INTERNAL_PERSIST ||
		    buf->type == HFI_BUFFER_INTERNAL_PERSIST_1)
			continue;

		memset(&bd, 0, sizeof(bd));
		bd.buffer_size = buf->size;
		bd.buffer_type = buf->type;
		bd.num_buffers = 1;
		bd.device_addr = buf->da;
		bd.response_required = true;

		ret = hfi_session_unset_buffers(inst, &bd);

		dma_free_attrs(inst->core->dev, buf->size, buf->va, buf->da,
			       buf->attrs);

		list_del_init(&buf->list);
		kfree(buf);
	}

	ret = intbufs_set_buffer(inst, HFI_BUFFER_INTERNAL_SCRATCH(ver));
	if (ret)
		goto err;

	ret = intbufs_set_buffer(inst, HFI_BUFFER_INTERNAL_SCRATCH_1(ver));
	if (ret)
		goto err;

	ret = intbufs_set_buffer(inst, HFI_BUFFER_INTERNAL_SCRATCH_2(ver));
	if (ret)
		goto err;

	return 0;
err:
	return ret;
}
EXPORT_SYMBOL_GPL(venus_helper_intbufs_realloc);

static void fill_buffer_desc(const struct venus_buffer *buf,
			     struct hfi_buffer_desc *bd, bool response)
{
	memset(bd, 0, sizeof(*bd));
	bd->buffer_type = HFI_BUFFER_OUTPUT;
	bd->buffer_size = buf->size;
	bd->num_buffers = 1;
	bd->device_addr = buf->dma_addr;
	bd->response_required = response;
}

static void return_buf_error(struct venus_inst *inst,
			     struct vb2_v4l2_buffer *vbuf)
{
	struct v4l2_m2m_ctx *m2m_ctx = inst->m2m_ctx;

	if (vbuf->vb2_buf.type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		v4l2_m2m_src_buf_remove_by_buf(m2m_ctx, vbuf);
	else
		v4l2_m2m_dst_buf_remove_by_buf(m2m_ctx, vbuf);

	v4l2_m2m_buf_done(vbuf, VB2_BUF_STATE_ERROR);
}

static void
put_ts_metadata(struct venus_inst *inst, struct vb2_v4l2_buffer *vbuf)
{
	struct vb2_buffer *vb = &vbuf->vb2_buf;
	unsigned int i;
	int slot = -1;
	u64 ts_us = vb->timestamp;

	for (i = 0; i < ARRAY_SIZE(inst->tss); i++) {
		if (!inst->tss[i].used) {
			slot = i;
			break;
		}
	}

	if (slot == -1) {
		dev_dbg(inst->core->dev, VDBGL "no free slot\n");
		return;
	}

	do_div(ts_us, NSEC_PER_USEC);

	inst->tss[slot].used = true;
	inst->tss[slot].flags = vbuf->flags;
	inst->tss[slot].tc = vbuf->timecode;
	inst->tss[slot].ts_us = ts_us;
	inst->tss[slot].ts_ns = vb->timestamp;
}

void venus_helper_get_ts_metadata(struct venus_inst *inst, u64 timestamp_us,
				  struct vb2_v4l2_buffer *vbuf)
{
	struct vb2_buffer *vb = &vbuf->vb2_buf;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(inst->tss); ++i) {
		if (!inst->tss[i].used)
			continue;

		if (inst->tss[i].ts_us != timestamp_us)
			continue;

		inst->tss[i].used = false;
		vbuf->flags |= inst->tss[i].flags;
		vbuf->timecode = inst->tss[i].tc;
		vb->timestamp = inst->tss[i].ts_ns;
		break;
	}
}
EXPORT_SYMBOL_GPL(venus_helper_get_ts_metadata);

static int
session_process_buf(struct venus_inst *inst, struct vb2_v4l2_buffer *vbuf)
{
	struct venus_buffer *buf = to_venus_buffer(vbuf);
	struct vb2_buffer *vb = &vbuf->vb2_buf;
	unsigned int type = vb->type;
	struct hfi_frame_data fdata;
	int ret;

	memset(&fdata, 0, sizeof(fdata));
	fdata.alloc_len = buf->size;
	fdata.device_addr = buf->dma_addr;
	fdata.timestamp = vb->timestamp;
	do_div(fdata.timestamp, NSEC_PER_USEC);
	fdata.flags = 0;
	fdata.clnt_data = vbuf->vb2_buf.index;

	if (type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		fdata.buffer_type = HFI_BUFFER_INPUT;
		fdata.filled_len = vb2_get_plane_payload(vb, 0);
		fdata.offset = vb->planes[0].data_offset;

		if (vbuf->flags & V4L2_BUF_FLAG_LAST || !fdata.filled_len)
			fdata.flags |= HFI_BUFFERFLAG_EOS;

		if (inst->session_type == VIDC_SESSION_TYPE_DEC)
			put_ts_metadata(inst, vbuf);

		venus_pm_load_scale(inst);
	} else if (type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		if (inst->session_type == VIDC_SESSION_TYPE_ENC)
			fdata.buffer_type = HFI_BUFFER_OUTPUT;
		else
			fdata.buffer_type = inst->opb_buftype;
		fdata.filled_len = 0;
		fdata.offset = 0;
	}

	ret = hfi_session_process_buf(inst, &fdata);
	if (ret)
		return ret;

	return 0;
}

static bool is_dynamic_bufmode(struct venus_inst *inst)
{
	struct venus_core *core = inst->core;
	struct hfi_plat_caps *caps;

	/*
	 * v4 doesn't send BUFFER_ALLOC_MODE_SUPPORTED property and supports
	 * dynamic buffer mode by default for HFI_BUFFER_OUTPUT/OUTPUT2.
	 */
	if (IS_V4(core) || IS_V6(core))
		return true;

	caps = venus_caps_by_codec(core, inst->hfi_codec, inst->session_type);
	if (!caps)
		return false;

	return caps->cap_bufs_mode_dynamic;
}

int venus_helper_unregister_bufs(struct venus_inst *inst)
{
	struct venus_buffer *buf, *n;
	struct hfi_buffer_desc bd;
	int ret = 0;

	if (is_dynamic_bufmode(inst))
		return 0;

	list_for_each_entry_safe(buf, n, &inst->registeredbufs, reg_list) {
		fill_buffer_desc(buf, &bd, true);
		ret = hfi_session_unset_buffers(inst, &bd);
		list_del_init(&buf->reg_list);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(venus_helper_unregister_bufs);

static int session_register_bufs(struct venus_inst *inst)
{
	struct venus_core *core = inst->core;
	struct device *dev = core->dev;
	struct hfi_buffer_desc bd;
	struct venus_buffer *buf;
	int ret = 0;

	if (is_dynamic_bufmode(inst))
		return 0;

	list_for_each_entry(buf, &inst->registeredbufs, reg_list) {
		fill_buffer_desc(buf, &bd, false);
		ret = hfi_session_set_buffers(inst, &bd);
		if (ret) {
			dev_err(dev, "%s: set buffer failed\n", __func__);
			break;
		}
	}

	return ret;
}

static u32 to_hfi_raw_fmt(u32 v4l2_fmt)
{
	switch (v4l2_fmt) {
	case V4L2_PIX_FMT_NV12:
		return HFI_COLOR_FORMAT_NV12;
	case V4L2_PIX_FMT_NV21:
		return HFI_COLOR_FORMAT_NV21;
	case V4L2_PIX_FMT_QC08C:
		return HFI_COLOR_FORMAT_NV12_UBWC;
	case V4L2_PIX_FMT_QC10C:
		return HFI_COLOR_FORMAT_YUV420_TP10_UBWC;
	default:
		break;
	}

	return 0;
}

static int platform_get_bufreq(struct venus_inst *inst, u32 buftype,
			       struct hfi_buffer_requirements *req)
{
	enum hfi_version version = inst->core->res->hfi_version;
	const struct hfi_platform *hfi_plat;
	struct hfi_plat_buffers_params params;
	bool is_dec = inst->session_type == VIDC_SESSION_TYPE_DEC;
	struct venc_controls *enc_ctr = &inst->controls.enc;

	hfi_plat = hfi_platform_get(version);

	if (!hfi_plat || !hfi_plat->bufreq)
		return -EINVAL;

	params.version = version;
	params.num_vpp_pipes = inst->core->res->num_vpp_pipes;

	if (is_dec) {
		params.width = inst->width;
		params.height = inst->height;
		params.codec = inst->fmt_out->pixfmt;
		params.hfi_color_fmt = to_hfi_raw_fmt(inst->fmt_cap->pixfmt);
		params.dec.max_mbs_per_frame = mbs_per_frame_max(inst);
		params.dec.buffer_size_limit = 0;
		params.dec.is_secondary_output =
			inst->opb_buftype == HFI_BUFFER_OUTPUT2;
		params.dec.is_interlaced =
			inst->pic_struct != HFI_INTERLACE_FRAME_PROGRESSIVE;
	} else {
		params.width = inst->out_width;
		params.height = inst->out_height;
		params.codec = inst->fmt_cap->pixfmt;
		params.hfi_color_fmt = to_hfi_raw_fmt(inst->fmt_out->pixfmt);
		params.enc.work_mode = VIDC_WORK_MODE_2;
		params.enc.rc_type = HFI_RATE_CONTROL_OFF;
		if (enc_ctr->bitrate_mode == V4L2_MPEG_VIDEO_BITRATE_MODE_CQ)
			params.enc.rc_type = HFI_RATE_CONTROL_CQ;
		params.enc.num_b_frames = enc_ctr->num_b_frames;
		params.enc.is_tenbit = inst->bit_depth == VIDC_BITDEPTH_10;
	}

	return hfi_plat->bufreq(&params, inst->session_type, buftype, req);
}

int venus_helper_get_bufreq(struct venus_inst *inst, u32 type,
			    struct hfi_buffer_requirements *req)
{
	u32 ptype = HFI_PROPERTY_CONFIG_BUFFER_REQUIREMENTS;
	union hfi_get_property hprop;
	unsigned int i;
	int ret;

	if (req)
		memset(req, 0, sizeof(*req));

	if (type == HFI_BUFFER_OUTPUT || type == HFI_BUFFER_OUTPUT2)
		req->count_min = inst->fw_min_cnt;

	ret = platform_get_bufreq(inst, type, req);
	if (!ret) {
		if (type == HFI_BUFFER_OUTPUT || type == HFI_BUFFER_OUTPUT2)
			inst->fw_min_cnt = req->count_min;
		return 0;
	}

	ret = hfi_session_get_property(inst, ptype, &hprop);
	if (ret)
		return ret;

	ret = -EINVAL;

	for (i = 0; i < HFI_BUFFER_TYPE_MAX; i++) {
		if (hprop.bufreq[i].type != type)
			continue;

		if (req)
			memcpy(req, &hprop.bufreq[i], sizeof(*req));
		ret = 0;
		break;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(venus_helper_get_bufreq);

struct id_mapping {
	u32 hfi_id;
	u32 v4l2_id;
};

static const struct id_mapping mpeg4_profiles[] = {
	{ HFI_MPEG4_PROFILE_SIMPLE, V4L2_MPEG_VIDEO_MPEG4_PROFILE_SIMPLE },
	{ HFI_MPEG4_PROFILE_ADVANCEDSIMPLE, V4L2_MPEG_VIDEO_MPEG4_PROFILE_ADVANCED_SIMPLE },
};

static const struct id_mapping mpeg4_levels[] = {
	{ HFI_MPEG4_LEVEL_0, V4L2_MPEG_VIDEO_MPEG4_LEVEL_0 },
	{ HFI_MPEG4_LEVEL_0b, V4L2_MPEG_VIDEO_MPEG4_LEVEL_0B },
	{ HFI_MPEG4_LEVEL_1, V4L2_MPEG_VIDEO_MPEG4_LEVEL_1 },
	{ HFI_MPEG4_LEVEL_2, V4L2_MPEG_VIDEO_MPEG4_LEVEL_2 },
	{ HFI_MPEG4_LEVEL_3, V4L2_MPEG_VIDEO_MPEG4_LEVEL_3 },
	{ HFI_MPEG4_LEVEL_4, V4L2_MPEG_VIDEO_MPEG4_LEVEL_4 },
	{ HFI_MPEG4_LEVEL_5, V4L2_MPEG_VIDEO_MPEG4_LEVEL_5 },
};

static const struct id_mapping mpeg2_profiles[] = {
	{ HFI_MPEG2_PROFILE_SIMPLE, V4L2_MPEG_VIDEO_MPEG2_PROFILE_SIMPLE },
	{ HFI_MPEG2_PROFILE_MAIN, V4L2_MPEG_VIDEO_MPEG2_PROFILE_MAIN },
	{ HFI_MPEG2_PROFILE_SNR, V4L2_MPEG_VIDEO_MPEG2_PROFILE_SNR_SCALABLE },
	{ HFI_MPEG2_PROFILE_SPATIAL, V4L2_MPEG_VIDEO_MPEG2_PROFILE_SPATIALLY_SCALABLE },
	{ HFI_MPEG2_PROFILE_HIGH, V4L2_MPEG_VIDEO_MPEG2_PROFILE_HIGH },
};

static const struct id_mapping mpeg2_levels[] = {
	{ HFI_MPEG2_LEVEL_LL, V4L2_MPEG_VIDEO_MPEG2_LEVEL_LOW },
	{ HFI_MPEG2_LEVEL_ML, V4L2_MPEG_VIDEO_MPEG2_LEVEL_MAIN },
	{ HFI_MPEG2_LEVEL_H14, V4L2_MPEG_VIDEO_MPEG2_LEVEL_HIGH_1440 },
	{ HFI_MPEG2_LEVEL_HL, V4L2_MPEG_VIDEO_MPEG2_LEVEL_HIGH },
};

static const struct id_mapping h264_profiles[] = {
	{ HFI_H264_PROFILE_BASELINE, V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE },
	{ HFI_H264_PROFILE_MAIN, V4L2_MPEG_VIDEO_H264_PROFILE_MAIN },
	{ HFI_H264_PROFILE_HIGH, V4L2_MPEG_VIDEO_H264_PROFILE_HIGH },
	{ HFI_H264_PROFILE_STEREO_HIGH, V4L2_MPEG_VIDEO_H264_PROFILE_STEREO_HIGH },
	{ HFI_H264_PROFILE_MULTIVIEW_HIGH, V4L2_MPEG_VIDEO_H264_PROFILE_MULTIVIEW_HIGH },
	{ HFI_H264_PROFILE_CONSTRAINED_BASE, V4L2_MPEG_VIDEO_H264_PROFILE_CONSTRAINED_BASELINE },
	{ HFI_H264_PROFILE_CONSTRAINED_HIGH, V4L2_MPEG_VIDEO_H264_PROFILE_CONSTRAINED_HIGH },
};

static const struct id_mapping h264_levels[] = {
	{ HFI_H264_LEVEL_1, V4L2_MPEG_VIDEO_H264_LEVEL_1_0 },
	{ HFI_H264_LEVEL_1b, V4L2_MPEG_VIDEO_H264_LEVEL_1B },
	{ HFI_H264_LEVEL_11, V4L2_MPEG_VIDEO_H264_LEVEL_1_1 },
	{ HFI_H264_LEVEL_12, V4L2_MPEG_VIDEO_H264_LEVEL_1_2 },
	{ HFI_H264_LEVEL_13, V4L2_MPEG_VIDEO_H264_LEVEL_1_3 },
	{ HFI_H264_LEVEL_2, V4L2_MPEG_VIDEO_H264_LEVEL_2_0 },
	{ HFI_H264_LEVEL_21, V4L2_MPEG_VIDEO_H264_LEVEL_2_1 },
	{ HFI_H264_LEVEL_22, V4L2_MPEG_VIDEO_H264_LEVEL_2_2 },
	{ HFI_H264_LEVEL_3, V4L2_MPEG_VIDEO_H264_LEVEL_3_0 },
	{ HFI_H264_LEVEL_31, V4L2_MPEG_VIDEO_H264_LEVEL_3_1 },
	{ HFI_H264_LEVEL_32, V4L2_MPEG_VIDEO_H264_LEVEL_3_2 },
	{ HFI_H264_LEVEL_4, V4L2_MPEG_VIDEO_H264_LEVEL_4_0 },
	{ HFI_H264_LEVEL_41, V4L2_MPEG_VIDEO_H264_LEVEL_4_1 },
	{ HFI_H264_LEVEL_42, V4L2_MPEG_VIDEO_H264_LEVEL_4_2 },
	{ HFI_H264_LEVEL_5, V4L2_MPEG_VIDEO_H264_LEVEL_5_0 },
	{ HFI_H264_LEVEL_51, V4L2_MPEG_VIDEO_H264_LEVEL_5_1 },
	{ HFI_H264_LEVEL_52, V4L2_MPEG_VIDEO_H264_LEVEL_5_1 },
};

static const struct id_mapping hevc_profiles[] = {
	{ HFI_HEVC_PROFILE_MAIN, V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN },
	{ HFI_HEVC_PROFILE_MAIN_STILL_PIC, V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN_STILL_PICTURE },
	{ HFI_HEVC_PROFILE_MAIN10, V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN_10 },
};

static const struct id_mapping hevc_levels[] = {
	{ HFI_HEVC_LEVEL_1, V4L2_MPEG_VIDEO_HEVC_LEVEL_1 },
	{ HFI_HEVC_LEVEL_2, V4L2_MPEG_VIDEO_HEVC_LEVEL_2 },
	{ HFI_HEVC_LEVEL_21, V4L2_MPEG_VIDEO_HEVC_LEVEL_2_1 },
	{ HFI_HEVC_LEVEL_3, V4L2_MPEG_VIDEO_HEVC_LEVEL_3 },
	{ HFI_HEVC_LEVEL_31, V4L2_MPEG_VIDEO_HEVC_LEVEL_3_1 },
	{ HFI_HEVC_LEVEL_4, V4L2_MPEG_VIDEO_HEVC_LEVEL_4 },
	{ HFI_HEVC_LEVEL_41, V4L2_MPEG_VIDEO_HEVC_LEVEL_4_1 },
	{ HFI_HEVC_LEVEL_5, V4L2_MPEG_VIDEO_HEVC_LEVEL_5 },
	{ HFI_HEVC_LEVEL_51, V4L2_MPEG_VIDEO_HEVC_LEVEL_5_1 },
	{ HFI_HEVC_LEVEL_52, V4L2_MPEG_VIDEO_HEVC_LEVEL_5_2 },
	{ HFI_HEVC_LEVEL_6, V4L2_MPEG_VIDEO_HEVC_LEVEL_6 },
	{ HFI_HEVC_LEVEL_61, V4L2_MPEG_VIDEO_HEVC_LEVEL_6_1 },
	{ HFI_HEVC_LEVEL_62, V4L2_MPEG_VIDEO_HEVC_LEVEL_6_2 },
};

static const struct id_mapping vp8_profiles[] = {
	{ HFI_VPX_PROFILE_VERSION_0, V4L2_MPEG_VIDEO_VP8_PROFILE_0 },
	{ HFI_VPX_PROFILE_VERSION_1, V4L2_MPEG_VIDEO_VP8_PROFILE_1 },
	{ HFI_VPX_PROFILE_VERSION_2, V4L2_MPEG_VIDEO_VP8_PROFILE_2 },
	{ HFI_VPX_PROFILE_VERSION_3, V4L2_MPEG_VIDEO_VP8_PROFILE_3 },
};

static const struct id_mapping vp9_profiles[] = {
	{ HFI_VP9_PROFILE_P0, V4L2_MPEG_VIDEO_VP9_PROFILE_0 },
	{ HFI_VP9_PROFILE_P2_10B, V4L2_MPEG_VIDEO_VP9_PROFILE_2 },
};

static const struct id_mapping vp9_levels[] = {
	{ HFI_VP9_LEVEL_1, V4L2_MPEG_VIDEO_VP9_LEVEL_1_0 },
	{ HFI_VP9_LEVEL_11, V4L2_MPEG_VIDEO_VP9_LEVEL_1_1 },
	{ HFI_VP9_LEVEL_2, V4L2_MPEG_VIDEO_VP9_LEVEL_2_0},
	{ HFI_VP9_LEVEL_21, V4L2_MPEG_VIDEO_VP9_LEVEL_2_1 },
	{ HFI_VP9_LEVEL_3, V4L2_MPEG_VIDEO_VP9_LEVEL_3_0},
	{ HFI_VP9_LEVEL_31, V4L2_MPEG_VIDEO_VP9_LEVEL_3_1 },
	{ HFI_VP9_LEVEL_4, V4L2_MPEG_VIDEO_VP9_LEVEL_4_0 },
	{ HFI_VP9_LEVEL_41, V4L2_MPEG_VIDEO_VP9_LEVEL_4_1 },
	{ HFI_VP9_LEVEL_5, V4L2_MPEG_VIDEO_VP9_LEVEL_5_0 },
	{ HFI_VP9_LEVEL_51, V4L2_MPEG_VIDEO_VP9_LEVEL_5_1 },
	{ HFI_VP9_LEVEL_6, V4L2_MPEG_VIDEO_VP9_LEVEL_6_0 },
	{ HFI_VP9_LEVEL_61, V4L2_MPEG_VIDEO_VP9_LEVEL_6_1 },
};

static u32 find_v4l2_id(u32 hfi_id, const struct id_mapping *array, unsigned int array_sz)
{
	unsigned int i;

	if (!array || !array_sz)
		return 0;

	for (i = 0; i < array_sz; i++)
		if (hfi_id == array[i].hfi_id)
			return array[i].v4l2_id;

	return 0;
}

static u32 find_hfi_id(u32 v4l2_id, const struct id_mapping *array, unsigned int array_sz)
{
	unsigned int i;

	if (!array || !array_sz)
		return 0;

	for (i = 0; i < array_sz; i++)
		if (v4l2_id == array[i].v4l2_id)
			return array[i].hfi_id;

	return 0;
}

static void
v4l2_id_profile_level(u32 hfi_codec, struct hfi_profile_level *pl, u32 *profile, u32 *level)
{
	u32 hfi_pf = pl->profile;
	u32 hfi_lvl = pl->level;

	switch (hfi_codec) {
	case HFI_VIDEO_CODEC_H264:
		*profile = find_v4l2_id(hfi_pf, h264_profiles, ARRAY_SIZE(h264_profiles));
		*level = find_v4l2_id(hfi_lvl, h264_levels, ARRAY_SIZE(h264_levels));
		break;
	case HFI_VIDEO_CODEC_MPEG2:
		*profile = find_v4l2_id(hfi_pf, mpeg2_profiles, ARRAY_SIZE(mpeg2_profiles));
		*level = find_v4l2_id(hfi_lvl, mpeg2_levels, ARRAY_SIZE(mpeg2_levels));
		break;
	case HFI_VIDEO_CODEC_MPEG4:
		*profile = find_v4l2_id(hfi_pf, mpeg4_profiles, ARRAY_SIZE(mpeg4_profiles));
		*level = find_v4l2_id(hfi_lvl, mpeg4_levels, ARRAY_SIZE(mpeg4_levels));
		break;
	case HFI_VIDEO_CODEC_VP8:
		*profile = find_v4l2_id(hfi_pf, vp8_profiles, ARRAY_SIZE(vp8_profiles));
		*level = 0;
		break;
	case HFI_VIDEO_CODEC_VP9:
		*profile = find_v4l2_id(hfi_pf, vp9_profiles, ARRAY_SIZE(vp9_profiles));
		*level = find_v4l2_id(hfi_lvl, vp9_levels, ARRAY_SIZE(vp9_levels));
		break;
	case HFI_VIDEO_CODEC_HEVC:
		*profile = find_v4l2_id(hfi_pf, hevc_profiles, ARRAY_SIZE(hevc_profiles));
		*level = find_v4l2_id(hfi_lvl, hevc_levels, ARRAY_SIZE(hevc_levels));
		break;
	default:
		break;
	}
}

static void
hfi_id_profile_level(u32 hfi_codec, u32 v4l2_pf, u32 v4l2_lvl, struct hfi_profile_level *pl)
{
	switch (hfi_codec) {
	case HFI_VIDEO_CODEC_H264:
		pl->profile = find_hfi_id(v4l2_pf, h264_profiles, ARRAY_SIZE(h264_profiles));
		pl->level = find_hfi_id(v4l2_lvl, h264_levels, ARRAY_SIZE(h264_levels));
		break;
	case HFI_VIDEO_CODEC_MPEG2:
		pl->profile = find_hfi_id(v4l2_pf, mpeg2_profiles, ARRAY_SIZE(mpeg2_profiles));
		pl->level = find_hfi_id(v4l2_lvl, mpeg2_levels, ARRAY_SIZE(mpeg2_levels));
		break;
	case HFI_VIDEO_CODEC_MPEG4:
		pl->profile = find_hfi_id(v4l2_pf, mpeg4_profiles, ARRAY_SIZE(mpeg4_profiles));
		pl->level = find_hfi_id(v4l2_lvl, mpeg4_levels, ARRAY_SIZE(mpeg4_levels));
		break;
	case HFI_VIDEO_CODEC_VP8:
		pl->profile = find_hfi_id(v4l2_pf, vp8_profiles, ARRAY_SIZE(vp8_profiles));
		pl->level = 0;
		break;
	case HFI_VIDEO_CODEC_VP9:
		pl->profile = find_hfi_id(v4l2_pf, vp9_profiles, ARRAY_SIZE(vp9_profiles));
		pl->level = find_hfi_id(v4l2_lvl, vp9_levels, ARRAY_SIZE(vp9_levels));
		break;
	case HFI_VIDEO_CODEC_HEVC:
		pl->profile = find_hfi_id(v4l2_pf, hevc_profiles, ARRAY_SIZE(hevc_profiles));
		pl->level = find_hfi_id(v4l2_lvl, hevc_levels, ARRAY_SIZE(hevc_levels));
		break;
	default:
		break;
	}
}

int venus_helper_get_profile_level(struct venus_inst *inst, u32 *profile, u32 *level)
{
	const u32 ptype = HFI_PROPERTY_PARAM_PROFILE_LEVEL_CURRENT;
	union hfi_get_property hprop;
	int ret;

	ret = hfi_session_get_property(inst, ptype, &hprop);
	if (ret)
		return ret;

	v4l2_id_profile_level(inst->hfi_codec, &hprop.profile_level, profile, level);

	return 0;
}
EXPORT_SYMBOL_GPL(venus_helper_get_profile_level);

int venus_helper_set_profile_level(struct venus_inst *inst, u32 profile, u32 level)
{
	const u32 ptype = HFI_PROPERTY_PARAM_PROFILE_LEVEL_CURRENT;
	struct hfi_profile_level pl;

	hfi_id_profile_level(inst->hfi_codec, profile, level, &pl);

	return hfi_session_set_property(inst, ptype, &pl);
}
EXPORT_SYMBOL_GPL(venus_helper_set_profile_level);

static u32 get_framesize_raw_nv12(u32 width, u32 height)
{
	u32 y_stride, uv_stride, y_plane;
	u32 y_sclines, uv_sclines, uv_plane;
	u32 size;

	y_stride = ALIGN(width, 128);
	uv_stride = ALIGN(width, 128);
	y_sclines = ALIGN(height, 32);
	uv_sclines = ALIGN(((height + 1) >> 1), 16);

	y_plane = y_stride * y_sclines;
	uv_plane = uv_stride * uv_sclines + SZ_4K;
	size = y_plane + uv_plane + SZ_8K;

	return ALIGN(size, SZ_4K);
}

static u32 get_framesize_raw_nv12_ubwc(u32 width, u32 height)
{
	u32 y_meta_stride, y_meta_plane;
	u32 y_stride, y_plane;
	u32 uv_meta_stride, uv_meta_plane;
	u32 uv_stride, uv_plane;
	u32 extradata = SZ_16K;

	y_meta_stride = ALIGN(DIV_ROUND_UP(width, 32), 64);
	y_meta_plane = y_meta_stride * ALIGN(DIV_ROUND_UP(height, 8), 16);
	y_meta_plane = ALIGN(y_meta_plane, SZ_4K);

	y_stride = ALIGN(width, 128);
	y_plane = ALIGN(y_stride * ALIGN(height, 32), SZ_4K);

	uv_meta_stride = ALIGN(DIV_ROUND_UP(width / 2, 16), 64);
	uv_meta_plane = uv_meta_stride * ALIGN(DIV_ROUND_UP(height / 2, 8), 16);
	uv_meta_plane = ALIGN(uv_meta_plane, SZ_4K);

	uv_stride = ALIGN(width, 128);
	uv_plane = ALIGN(uv_stride * ALIGN(height / 2, 32), SZ_4K);

	return ALIGN(y_meta_plane + y_plane + uv_meta_plane + uv_plane +
		     max(extradata, y_stride * 48), SZ_4K);
}

static u32 get_framesize_raw_p010(u32 width, u32 height)
{
	u32 y_plane, uv_plane, y_stride, uv_stride, y_sclines, uv_sclines;

	y_stride = ALIGN(width * 2, 256);
	uv_stride = ALIGN(width * 2, 256);
	y_sclines = ALIGN(height, 32);
	uv_sclines = ALIGN((height + 1) >> 1, 16);
	y_plane = y_stride * y_sclines;
	uv_plane = uv_stride * uv_sclines;

	return ALIGN((y_plane + uv_plane), SZ_4K);
}

static u32 get_framesize_raw_p010_ubwc(u32 width, u32 height)
{
	u32 y_stride, uv_stride, y_sclines, uv_sclines;
	u32 y_ubwc_plane, uv_ubwc_plane;
	u32 y_meta_stride, y_meta_scanlines;
	u32 uv_meta_stride, uv_meta_scanlines;
	u32 y_meta_plane, uv_meta_plane;
	u32 size;

	y_stride = ALIGN(width * 2, 256);
	uv_stride = ALIGN(width * 2, 256);
	y_sclines = ALIGN(height, 16);
	uv_sclines = ALIGN((height + 1) >> 1, 16);

	y_ubwc_plane = ALIGN(y_stride * y_sclines, SZ_4K);
	uv_ubwc_plane = ALIGN(uv_stride * uv_sclines, SZ_4K);
	y_meta_stride = ALIGN(DIV_ROUND_UP(width, 32), 64);
	y_meta_scanlines = ALIGN(DIV_ROUND_UP(height, 4), 16);
	y_meta_plane = ALIGN(y_meta_stride * y_meta_scanlines, SZ_4K);
	uv_meta_stride = ALIGN(DIV_ROUND_UP((width + 1) >> 1, 16), 64);
	uv_meta_scanlines = ALIGN(DIV_ROUND_UP((height + 1) >> 1, 4), 16);
	uv_meta_plane = ALIGN(uv_meta_stride * uv_meta_scanlines, SZ_4K);

	size = y_ubwc_plane + uv_ubwc_plane + y_meta_plane + uv_meta_plane;

	return ALIGN(size, SZ_4K);
}

static u32 get_framesize_raw_yuv420_tp10_ubwc(u32 width, u32 height)
{
	u32 y_stride, uv_stride, y_sclines, uv_sclines;
	u32 y_ubwc_plane, uv_ubwc_plane;
	u32 y_meta_stride, y_meta_scanlines;
	u32 uv_meta_stride, uv_meta_scanlines;
	u32 y_meta_plane, uv_meta_plane;
	u32 extradata = SZ_16K;
	u32 size;

	y_stride = ALIGN(ALIGN(width, 192) * 4 / 3, 256);
	uv_stride = ALIGN(ALIGN(width, 192) * 4 / 3, 256);
	y_sclines = ALIGN(height, 16);
	uv_sclines = ALIGN((height + 1) >> 1, 16);

	y_ubwc_plane = ALIGN(y_stride * y_sclines, SZ_4K);
	uv_ubwc_plane = ALIGN(uv_stride * uv_sclines, SZ_4K);
	y_meta_stride = ALIGN(DIV_ROUND_UP(width, 48), 64);
	y_meta_scanlines = ALIGN(DIV_ROUND_UP(height, 4), 16);
	y_meta_plane = ALIGN(y_meta_stride * y_meta_scanlines, SZ_4K);
	uv_meta_stride = ALIGN(DIV_ROUND_UP((width + 1) >> 1, 24), 64);
	uv_meta_scanlines = ALIGN(DIV_ROUND_UP((height + 1) >> 1, 4), 16);
	uv_meta_plane = ALIGN(uv_meta_stride * uv_meta_scanlines, SZ_4K);

	size = y_ubwc_plane + uv_ubwc_plane + y_meta_plane + uv_meta_plane;
	size += max(extradata + SZ_8K, y_stride * 48);

	return ALIGN(size, SZ_4K);
}

u32 venus_helper_get_framesz_raw(u32 hfi_fmt, u32 width, u32 height)
{
	switch (hfi_fmt) {
	case HFI_COLOR_FORMAT_NV12:
	case HFI_COLOR_FORMAT_NV21:
		return get_framesize_raw_nv12(width, height);
	case HFI_COLOR_FORMAT_NV12_UBWC:
		return get_framesize_raw_nv12_ubwc(width, height);
	case HFI_COLOR_FORMAT_P010:
		return get_framesize_raw_p010(width, height);
	case HFI_COLOR_FORMAT_P010_UBWC:
		return get_framesize_raw_p010_ubwc(width, height);
	case HFI_COLOR_FORMAT_YUV420_TP10_UBWC:
		return get_framesize_raw_yuv420_tp10_ubwc(width, height);
	default:
		return 0;
	}
}
EXPORT_SYMBOL_GPL(venus_helper_get_framesz_raw);

u32 venus_helper_get_framesz(u32 v4l2_fmt, u32 width, u32 height)
{
	u32 hfi_fmt, sz;
	bool compressed;

	switch (v4l2_fmt) {
	case V4L2_PIX_FMT_MPEG:
	case V4L2_PIX_FMT_H264:
	case V4L2_PIX_FMT_H264_NO_SC:
	case V4L2_PIX_FMT_H264_MVC:
	case V4L2_PIX_FMT_H263:
	case V4L2_PIX_FMT_MPEG1:
	case V4L2_PIX_FMT_MPEG2:
	case V4L2_PIX_FMT_MPEG4:
	case V4L2_PIX_FMT_XVID:
	case V4L2_PIX_FMT_VC1_ANNEX_G:
	case V4L2_PIX_FMT_VC1_ANNEX_L:
	case V4L2_PIX_FMT_VP8:
	case V4L2_PIX_FMT_VP9:
	case V4L2_PIX_FMT_HEVC:
		compressed = true;
		break;
	default:
		compressed = false;
		break;
	}

	if (compressed) {
		sz = ALIGN(height, 32) * ALIGN(width, 32) * 3 / 2 / 2;
		if (width < 1280 || height < 720)
			sz *= 8;
		return ALIGN(sz, SZ_4K);
	}

	hfi_fmt = to_hfi_raw_fmt(v4l2_fmt);
	if (!hfi_fmt)
		return 0;

	return venus_helper_get_framesz_raw(hfi_fmt, width, height);
}
EXPORT_SYMBOL_GPL(venus_helper_get_framesz);

int venus_helper_set_input_resolution(struct venus_inst *inst,
				      unsigned int width, unsigned int height)
{
	u32 ptype = HFI_PROPERTY_PARAM_FRAME_SIZE;
	struct hfi_framesize fs;

	fs.buffer_type = HFI_BUFFER_INPUT;
	fs.width = width;
	fs.height = height;

	return hfi_session_set_property(inst, ptype, &fs);
}
EXPORT_SYMBOL_GPL(venus_helper_set_input_resolution);

int venus_helper_set_output_resolution(struct venus_inst *inst,
				       unsigned int width, unsigned int height,
				       u32 buftype)
{
	u32 ptype = HFI_PROPERTY_PARAM_FRAME_SIZE;
	struct hfi_framesize fs;

	fs.buffer_type = buftype;
	fs.width = width;
	fs.height = height;

	return hfi_session_set_property(inst, ptype, &fs);
}
EXPORT_SYMBOL_GPL(venus_helper_set_output_resolution);

static u32 venus_helper_get_work_mode(struct venus_inst *inst)
{
	u32 mode;
	u32 num_mbs;

	mode = VIDC_WORK_MODE_2;
	if (inst->session_type == VIDC_SESSION_TYPE_DEC) {
		num_mbs = (ALIGN(inst->height, 16) * ALIGN(inst->width, 16)) / 256;
		if (inst->hfi_codec == HFI_VIDEO_CODEC_MPEG2 ||
		    inst->pic_struct != HFI_INTERLACE_FRAME_PROGRESSIVE ||
		    num_mbs <= NUM_MBS_720P)
			mode = VIDC_WORK_MODE_1;
	} else {
		num_mbs = (ALIGN(inst->out_height, 16) * ALIGN(inst->out_width, 16)) / 256;
		if (inst->hfi_codec == HFI_VIDEO_CODEC_VP8 &&
		    num_mbs <= NUM_MBS_4K)
			mode = VIDC_WORK_MODE_1;
	}

	return mode;
}

int venus_helper_set_work_mode(struct venus_inst *inst)
{
	const u32 ptype = HFI_PROPERTY_PARAM_WORK_MODE;
	struct hfi_video_work_mode wm;
	u32 mode;

	if (!IS_V4(inst->core) && !IS_V6(inst->core))
		return 0;

	mode = venus_helper_get_work_mode(inst);
	wm.video_work_mode = mode;
	return hfi_session_set_property(inst, ptype, &wm);
}
EXPORT_SYMBOL_GPL(venus_helper_set_work_mode);

int venus_helper_set_format_constraints(struct venus_inst *inst)
{
	const u32 ptype = HFI_PROPERTY_PARAM_UNCOMPRESSED_PLANE_ACTUAL_CONSTRAINTS_INFO;
	struct hfi_uncompressed_plane_actual_constraints_info pconstraint;

	if (!IS_V6(inst->core))
		return 0;

	if (inst->opb_fmt == HFI_COLOR_FORMAT_NV12_UBWC ||
	    inst->opb_fmt == HFI_COLOR_FORMAT_YUV420_TP10_UBWC)
		return 0;

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

	return hfi_session_set_property(inst, ptype, &pconstraint);
}
EXPORT_SYMBOL_GPL(venus_helper_set_format_constraints);

int venus_helper_set_num_bufs(struct venus_inst *inst, unsigned int input_bufs,
			      unsigned int output_bufs,
			      unsigned int output2_bufs)
{
	u32 ptype = HFI_PROPERTY_PARAM_BUFFER_COUNT_ACTUAL;
	struct hfi_buffer_count_actual buf_count;
	int ret;

	buf_count.type = HFI_BUFFER_INPUT;
	buf_count.count_actual = input_bufs;

	ret = hfi_session_set_property(inst, ptype, &buf_count);
	if (ret)
		return ret;

	buf_count.type = HFI_BUFFER_OUTPUT;
	buf_count.count_actual = output_bufs;

	ret = hfi_session_set_property(inst, ptype, &buf_count);
	if (ret)
		return ret;

	if (output2_bufs) {
		buf_count.type = HFI_BUFFER_OUTPUT2;
		buf_count.count_actual = output2_bufs;

		ret = hfi_session_set_property(inst, ptype, &buf_count);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(venus_helper_set_num_bufs);

int venus_helper_set_raw_format(struct venus_inst *inst, u32 hfi_format,
				u32 buftype)
{
	const u32 ptype = HFI_PROPERTY_PARAM_UNCOMPRESSED_FORMAT_SELECT;
	struct hfi_uncompressed_format_select fmt;

	fmt.buffer_type = buftype;
	fmt.format = hfi_format;

	return hfi_session_set_property(inst, ptype, &fmt);
}
EXPORT_SYMBOL_GPL(venus_helper_set_raw_format);

int venus_helper_set_color_format(struct venus_inst *inst, u32 pixfmt)
{
	u32 hfi_format, buftype;

	if (inst->session_type == VIDC_SESSION_TYPE_DEC)
		buftype = HFI_BUFFER_OUTPUT;
	else if (inst->session_type == VIDC_SESSION_TYPE_ENC)
		buftype = HFI_BUFFER_INPUT;
	else
		return -EINVAL;

	hfi_format = to_hfi_raw_fmt(pixfmt);
	if (!hfi_format)
		return -EINVAL;

	return venus_helper_set_raw_format(inst, hfi_format, buftype);
}
EXPORT_SYMBOL_GPL(venus_helper_set_color_format);

int venus_helper_set_multistream(struct venus_inst *inst, bool out_en,
				 bool out2_en)
{
	struct hfi_multi_stream multi = {0};
	u32 ptype = HFI_PROPERTY_PARAM_VDEC_MULTI_STREAM;
	int ret;

	multi.buffer_type = HFI_BUFFER_OUTPUT;
	multi.enable = out_en;

	ret = hfi_session_set_property(inst, ptype, &multi);
	if (ret)
		return ret;

	multi.buffer_type = HFI_BUFFER_OUTPUT2;
	multi.enable = out2_en;

	return hfi_session_set_property(inst, ptype, &multi);
}
EXPORT_SYMBOL_GPL(venus_helper_set_multistream);

int venus_helper_set_dyn_bufmode(struct venus_inst *inst)
{
	const u32 ptype = HFI_PROPERTY_PARAM_BUFFER_ALLOC_MODE;
	struct hfi_buffer_alloc_mode mode;
	int ret;

	if (!is_dynamic_bufmode(inst))
		return 0;

	mode.type = HFI_BUFFER_OUTPUT;
	mode.mode = HFI_BUFFER_MODE_DYNAMIC;

	ret = hfi_session_set_property(inst, ptype, &mode);
	if (ret)
		return ret;

	mode.type = HFI_BUFFER_OUTPUT2;

	return hfi_session_set_property(inst, ptype, &mode);
}
EXPORT_SYMBOL_GPL(venus_helper_set_dyn_bufmode);

int venus_helper_set_bufsize(struct venus_inst *inst, u32 bufsize, u32 buftype)
{
	const u32 ptype = HFI_PROPERTY_PARAM_BUFFER_SIZE_ACTUAL;
	struct hfi_buffer_size_actual bufsz;

	bufsz.type = buftype;
	bufsz.size = bufsize;

	return hfi_session_set_property(inst, ptype, &bufsz);
}
EXPORT_SYMBOL_GPL(venus_helper_set_bufsize);

unsigned int venus_helper_get_opb_size(struct venus_inst *inst)
{
	/* the encoder has only one output */
	if (inst->session_type == VIDC_SESSION_TYPE_ENC)
		return inst->output_buf_size;

	if (inst->opb_buftype == HFI_BUFFER_OUTPUT)
		return inst->output_buf_size;
	else if (inst->opb_buftype == HFI_BUFFER_OUTPUT2)
		return inst->output2_buf_size;

	return 0;
}
EXPORT_SYMBOL_GPL(venus_helper_get_opb_size);

static void delayed_process_buf_func(struct work_struct *work)
{
	struct venus_buffer *buf, *n;
	struct venus_inst *inst;
	int ret;

	inst = container_of(work, struct venus_inst, delayed_process_work);

	mutex_lock(&inst->lock);

	if (!(inst->streamon_out & inst->streamon_cap))
		goto unlock;

	list_for_each_entry_safe(buf, n, &inst->delayed_process, ref_list) {
		if (buf->flags & HFI_BUFFERFLAG_READONLY)
			continue;

		ret = session_process_buf(inst, &buf->vb);
		if (ret)
			return_buf_error(inst, &buf->vb);

		list_del_init(&buf->ref_list);
	}
unlock:
	mutex_unlock(&inst->lock);
}

void venus_helper_release_buf_ref(struct venus_inst *inst, unsigned int idx)
{
	struct venus_buffer *buf;

	list_for_each_entry(buf, &inst->registeredbufs, reg_list) {
		if (buf->vb.vb2_buf.index == idx) {
			buf->flags &= ~HFI_BUFFERFLAG_READONLY;
			schedule_work(&inst->delayed_process_work);
			break;
		}
	}
}
EXPORT_SYMBOL_GPL(venus_helper_release_buf_ref);

void venus_helper_acquire_buf_ref(struct vb2_v4l2_buffer *vbuf)
{
	struct venus_buffer *buf = to_venus_buffer(vbuf);

	buf->flags |= HFI_BUFFERFLAG_READONLY;
}
EXPORT_SYMBOL_GPL(venus_helper_acquire_buf_ref);

static int is_buf_refed(struct venus_inst *inst, struct vb2_v4l2_buffer *vbuf)
{
	struct venus_buffer *buf = to_venus_buffer(vbuf);

	if (buf->flags & HFI_BUFFERFLAG_READONLY) {
		list_add_tail(&buf->ref_list, &inst->delayed_process);
		schedule_work(&inst->delayed_process_work);
		return 1;
	}

	return 0;
}

struct vb2_v4l2_buffer *
venus_helper_find_buf(struct venus_inst *inst, unsigned int type, u32 idx)
{
	struct v4l2_m2m_ctx *m2m_ctx = inst->m2m_ctx;

	if (type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		return v4l2_m2m_src_buf_remove_by_idx(m2m_ctx, idx);
	else
		return v4l2_m2m_dst_buf_remove_by_idx(m2m_ctx, idx);
}
EXPORT_SYMBOL_GPL(venus_helper_find_buf);

void venus_helper_change_dpb_owner(struct venus_inst *inst,
				   struct vb2_v4l2_buffer *vbuf, unsigned int type,
				   unsigned int buf_type, u32 tag)
{
	struct intbuf *dpb_buf;

	if (!V4L2_TYPE_IS_CAPTURE(type) ||
	    buf_type != inst->dpb_buftype)
		return;

	list_for_each_entry(dpb_buf, &inst->dpbbufs, list)
		if (dpb_buf->dpb_out_tag == tag) {
			dpb_buf->owned_by = DRIVER;
			break;
		}
}
EXPORT_SYMBOL_GPL(venus_helper_change_dpb_owner);

int venus_helper_vb2_buf_init(struct vb2_buffer *vb)
{
	struct venus_inst *inst = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct venus_buffer *buf = to_venus_buffer(vbuf);

	buf->size = vb2_plane_size(vb, 0);
	buf->dma_addr = vb2_dma_contig_plane_dma_addr(vb, 0);

	if (vb->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		list_add_tail(&buf->reg_list, &inst->registeredbufs);

	return 0;
}
EXPORT_SYMBOL_GPL(venus_helper_vb2_buf_init);

int venus_helper_vb2_buf_prepare(struct vb2_buffer *vb)
{
	struct venus_inst *inst = vb2_get_drv_priv(vb->vb2_queue);
	unsigned int out_buf_size = venus_helper_get_opb_size(inst);
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);

	if (V4L2_TYPE_IS_OUTPUT(vb->vb2_queue->type)) {
		if (vbuf->field == V4L2_FIELD_ANY)
			vbuf->field = V4L2_FIELD_NONE;
		if (vbuf->field != V4L2_FIELD_NONE) {
			dev_err(inst->core->dev, "%s field isn't supported\n",
				__func__);
			return -EINVAL;
		}
	}

	if (vb->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE &&
	    vb2_plane_size(vb, 0) < out_buf_size)
		return -EINVAL;
	if (vb->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE &&
	    vb2_plane_size(vb, 0) < inst->input_buf_size)
		return -EINVAL;

	return 0;
}
EXPORT_SYMBOL_GPL(venus_helper_vb2_buf_prepare);

static void cache_payload(struct venus_inst *inst, struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	unsigned int idx = vbuf->vb2_buf.index;

	if (vbuf->vb2_buf.type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		inst->payloads[idx] = vb2_get_plane_payload(vb, 0);
}

void venus_helper_vb2_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct venus_inst *inst = vb2_get_drv_priv(vb->vb2_queue);
	struct v4l2_m2m_ctx *m2m_ctx = inst->m2m_ctx;
	int ret;

	v4l2_m2m_buf_queue(m2m_ctx, vbuf);

	/* Skip processing queued capture buffers after LAST flag */
	if (inst->session_type == VIDC_SESSION_TYPE_DEC &&
	    V4L2_TYPE_IS_CAPTURE(vb->vb2_queue->type) &&
	    inst->codec_state == VENUS_DEC_STATE_DRC)
		return;

	cache_payload(inst, vb);

	if (inst->session_type == VIDC_SESSION_TYPE_ENC &&
	    !(inst->streamon_out && inst->streamon_cap))
		return;

	if (vb2_start_streaming_called(vb->vb2_queue)) {
		ret = is_buf_refed(inst, vbuf);
		if (ret)
			return;

		ret = session_process_buf(inst, vbuf);
		if (ret)
			return_buf_error(inst, vbuf);
	}
}
EXPORT_SYMBOL_GPL(venus_helper_vb2_buf_queue);

void venus_helper_buffers_done(struct venus_inst *inst, unsigned int type,
			       enum vb2_buffer_state state)
{
	struct vb2_v4l2_buffer *buf;

	if (type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		while ((buf = v4l2_m2m_src_buf_remove(inst->m2m_ctx)))
			v4l2_m2m_buf_done(buf, state);
	} else if (type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		while ((buf = v4l2_m2m_dst_buf_remove(inst->m2m_ctx)))
			v4l2_m2m_buf_done(buf, state);
	}
}
EXPORT_SYMBOL_GPL(venus_helper_buffers_done);

void venus_helper_vb2_stop_streaming(struct vb2_queue *q)
{
	struct venus_inst *inst = vb2_get_drv_priv(q);
	struct venus_core *core = inst->core;
	int ret;

	mutex_lock(&inst->lock);

	if (inst->streamon_out & inst->streamon_cap) {
		ret = hfi_session_stop(inst);
		ret |= hfi_session_unload_res(inst);
		ret |= venus_helper_unregister_bufs(inst);
		ret |= venus_helper_intbufs_free(inst);
		ret |= hfi_session_deinit(inst);

		if (inst->session_error || test_bit(0, &core->sys_error))
			ret = -EIO;

		if (ret)
			hfi_session_abort(inst);

		venus_helper_free_dpb_bufs(inst);

		venus_pm_load_scale(inst);
		INIT_LIST_HEAD(&inst->registeredbufs);
	}

	venus_helper_buffers_done(inst, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
				  VB2_BUF_STATE_ERROR);
	venus_helper_buffers_done(inst, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
				  VB2_BUF_STATE_ERROR);

	if (q->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		inst->streamon_out = 0;
	else
		inst->streamon_cap = 0;

	venus_pm_release_core(inst);

	inst->session_error = 0;

	mutex_unlock(&inst->lock);
}
EXPORT_SYMBOL_GPL(venus_helper_vb2_stop_streaming);

void venus_helper_vb2_queue_error(struct venus_inst *inst)
{
	struct v4l2_m2m_ctx *m2m_ctx = inst->m2m_ctx;
	struct vb2_queue *q;

	q = v4l2_m2m_get_src_vq(m2m_ctx);
	vb2_queue_error(q);
	q = v4l2_m2m_get_dst_vq(m2m_ctx);
	vb2_queue_error(q);
}
EXPORT_SYMBOL_GPL(venus_helper_vb2_queue_error);

int venus_helper_process_initial_cap_bufs(struct venus_inst *inst)
{
	struct v4l2_m2m_ctx *m2m_ctx = inst->m2m_ctx;
	struct v4l2_m2m_buffer *buf, *n;
	int ret;

	v4l2_m2m_for_each_dst_buf_safe(m2m_ctx, buf, n) {
		ret = session_process_buf(inst, &buf->vb);
		if (ret) {
			return_buf_error(inst, &buf->vb);
			return ret;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(venus_helper_process_initial_cap_bufs);

int venus_helper_process_initial_out_bufs(struct venus_inst *inst)
{
	struct v4l2_m2m_ctx *m2m_ctx = inst->m2m_ctx;
	struct v4l2_m2m_buffer *buf, *n;
	int ret;

	v4l2_m2m_for_each_src_buf_safe(m2m_ctx, buf, n) {
		ret = session_process_buf(inst, &buf->vb);
		if (ret) {
			return_buf_error(inst, &buf->vb);
			return ret;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(venus_helper_process_initial_out_bufs);

int venus_helper_vb2_start_streaming(struct venus_inst *inst)
{
	int ret;

	ret = venus_helper_intbufs_alloc(inst);
	if (ret)
		return ret;

	ret = session_register_bufs(inst);
	if (ret)
		goto err_bufs_free;

	venus_pm_load_scale(inst);

	ret = hfi_session_load_res(inst);
	if (ret)
		goto err_unreg_bufs;

	ret = hfi_session_start(inst);
	if (ret)
		goto err_unload_res;

	return 0;

err_unload_res:
	hfi_session_unload_res(inst);
err_unreg_bufs:
	venus_helper_unregister_bufs(inst);
err_bufs_free:
	venus_helper_intbufs_free(inst);
	return ret;
}
EXPORT_SYMBOL_GPL(venus_helper_vb2_start_streaming);

void venus_helper_m2m_device_run(void *priv)
{
	struct venus_inst *inst = priv;
	struct v4l2_m2m_ctx *m2m_ctx = inst->m2m_ctx;
	struct v4l2_m2m_buffer *buf, *n;
	int ret;

	mutex_lock(&inst->lock);

	v4l2_m2m_for_each_dst_buf_safe(m2m_ctx, buf, n) {
		ret = session_process_buf(inst, &buf->vb);
		if (ret)
			return_buf_error(inst, &buf->vb);
	}

	v4l2_m2m_for_each_src_buf_safe(m2m_ctx, buf, n) {
		ret = session_process_buf(inst, &buf->vb);
		if (ret)
			return_buf_error(inst, &buf->vb);
	}

	mutex_unlock(&inst->lock);
}
EXPORT_SYMBOL_GPL(venus_helper_m2m_device_run);

void venus_helper_m2m_job_abort(void *priv)
{
	struct venus_inst *inst = priv;

	v4l2_m2m_job_finish(inst->m2m_dev, inst->m2m_ctx);
}
EXPORT_SYMBOL_GPL(venus_helper_m2m_job_abort);

int venus_helper_session_init(struct venus_inst *inst)
{
	enum hfi_version version = inst->core->res->hfi_version;
	u32 session_type = inst->session_type;
	u32 codec;
	int ret;

	codec = inst->session_type == VIDC_SESSION_TYPE_DEC ?
			inst->fmt_out->pixfmt : inst->fmt_cap->pixfmt;

	ret = hfi_session_init(inst, codec);
	if (ret)
		return ret;

	inst->clk_data.vpp_freq = hfi_platform_get_codec_vpp_freq(version, codec,
								  session_type);
	inst->clk_data.vsp_freq = hfi_platform_get_codec_vsp_freq(version, codec,
								  session_type);
	inst->clk_data.low_power_freq = hfi_platform_get_codec_lp_freq(version, codec,
								       session_type);

	return 0;
}
EXPORT_SYMBOL_GPL(venus_helper_session_init);

void venus_helper_init_instance(struct venus_inst *inst)
{
	if (inst->session_type == VIDC_SESSION_TYPE_DEC) {
		INIT_LIST_HEAD(&inst->delayed_process);
		INIT_WORK(&inst->delayed_process_work,
			  delayed_process_buf_func);
	}
}
EXPORT_SYMBOL_GPL(venus_helper_init_instance);

static bool find_fmt_from_caps(struct hfi_plat_caps *caps, u32 buftype, u32 fmt)
{
	unsigned int i;

	for (i = 0; i < caps->num_fmts; i++) {
		if (caps->fmts[i].buftype == buftype &&
		    caps->fmts[i].fmt == fmt)
			return true;
	}

	return false;
}

int venus_helper_get_out_fmts(struct venus_inst *inst, u32 v4l2_fmt,
			      u32 *out_fmt, u32 *out2_fmt, bool ubwc)
{
	struct venus_core *core = inst->core;
	struct hfi_plat_caps *caps;
	u32 ubwc_fmt, fmt = to_hfi_raw_fmt(v4l2_fmt);
	bool found, found_ubwc;

	*out_fmt = *out2_fmt = 0;

	if (!fmt)
		return -EINVAL;

	caps = venus_caps_by_codec(core, inst->hfi_codec, inst->session_type);
	if (!caps)
		return -EINVAL;

	if (ubwc) {
		ubwc_fmt = fmt | HFI_COLOR_FORMAT_UBWC_BASE;
		found_ubwc = find_fmt_from_caps(caps, HFI_BUFFER_OUTPUT,
						ubwc_fmt);
		found = find_fmt_from_caps(caps, HFI_BUFFER_OUTPUT2, fmt);

		if (found_ubwc && found) {
			*out_fmt = ubwc_fmt;
			*out2_fmt = fmt;
			return 0;
		}
	}

	found = find_fmt_from_caps(caps, HFI_BUFFER_OUTPUT, fmt);
	if (found) {
		*out_fmt = fmt;
		*out2_fmt = 0;
		return 0;
	}

	found = find_fmt_from_caps(caps, HFI_BUFFER_OUTPUT2, fmt);
	if (found) {
		*out_fmt = 0;
		*out2_fmt = fmt;
		return 0;
	}

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(venus_helper_get_out_fmts);

bool venus_helper_check_format(struct venus_inst *inst, u32 v4l2_pixfmt)
{
	struct venus_core *core = inst->core;
	u32 fmt = to_hfi_raw_fmt(v4l2_pixfmt);
	struct hfi_plat_caps *caps;
	u32 buftype;

	if (!fmt)
		return false;

	caps = venus_caps_by_codec(core, inst->hfi_codec, inst->session_type);
	if (!caps)
		return false;

	if (inst->session_type == VIDC_SESSION_TYPE_DEC)
		buftype = HFI_BUFFER_OUTPUT2;
	else
		buftype = HFI_BUFFER_OUTPUT;

	return find_fmt_from_caps(caps, buftype, fmt);
}
EXPORT_SYMBOL_GPL(venus_helper_check_format);

int venus_helper_set_stride(struct venus_inst *inst,
			    unsigned int width, unsigned int height)
{
	const u32 ptype = HFI_PROPERTY_PARAM_UNCOMPRESSED_PLANE_ACTUAL_INFO;

	struct hfi_uncompressed_plane_actual_info plane_actual_info;

	plane_actual_info.buffer_type = HFI_BUFFER_INPUT;
	plane_actual_info.num_planes = 2;
	plane_actual_info.plane_format[0].actual_stride = width;
	plane_actual_info.plane_format[0].actual_plane_buffer_height = height;
	plane_actual_info.plane_format[1].actual_stride = width;
	plane_actual_info.plane_format[1].actual_plane_buffer_height = height / 2;

	return hfi_session_set_property(inst, ptype, &plane_actual_info);
}
EXPORT_SYMBOL_GPL(venus_helper_set_stride);
