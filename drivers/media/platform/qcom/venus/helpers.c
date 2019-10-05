// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012-2016, The Linux Foundation. All rights reserved.
 * Copyright (C) 2017 Linaro Ltd.
 */
#include <linux/clk.h>
#include <linux/iopoll.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <media/videobuf2-dma-sg.h>
#include <media/v4l2-mem2mem.h>
#include <asm/div64.h>

#include "core.h"
#include "helpers.h"
#include "hfi_helper.h"
#include "hfi_venus_io.h"

struct intbuf {
	struct list_head list;
	u32 type;
	size_t size;
	void *va;
	dma_addr_t da;
	unsigned long attrs;
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

int venus_helper_queue_dpb_bufs(struct venus_inst *inst)
{
	struct intbuf *buf;
	int ret = 0;

	list_for_each_entry(buf, &inst->dpbbufs, list) {
		struct hfi_frame_data fdata;

		memset(&fdata, 0, sizeof(fdata));
		fdata.alloc_len = buf->size;
		fdata.device_addr = buf->da;
		fdata.buffer_type = buf->type;

		ret = hfi_session_process_buf(inst, &fdata);
		if (ret)
			goto fail;
	}

fail:
	return ret;
}
EXPORT_SYMBOL_GPL(venus_helper_queue_dpb_bufs);

int venus_helper_free_dpb_bufs(struct venus_inst *inst)
{
	struct intbuf *buf, *n;

	list_for_each_entry_safe(buf, n, &inst->dpbbufs, list) {
		list_del_init(&buf->list);
		dma_free_attrs(inst->core->dev, buf->size, buf->va, buf->da,
			       buf->attrs);
		kfree(buf);
	}

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
			kfree(buf);
			ret = -ENOMEM;
			goto fail;
		}

		list_add_tail(&buf->list, &inst->dpbbufs);
	}

	return 0;

fail:
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

int venus_helper_intbufs_alloc(struct venus_inst *inst)
{
	const unsigned int *intbuf;
	size_t arr_sz, i;
	int ret;

	if (IS_V4(inst->core)) {
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

static u32 load_per_instance(struct venus_inst *inst)
{
	u32 mbs;

	if (!inst || !(inst->state >= INST_INIT && inst->state < INST_STOP))
		return 0;

	mbs = (ALIGN(inst->width, 16) / 16) * (ALIGN(inst->height, 16) / 16);

	return mbs * inst->fps;
}

static u32 load_per_type(struct venus_core *core, u32 session_type)
{
	struct venus_inst *inst = NULL;
	u32 mbs_per_sec = 0;

	mutex_lock(&core->lock);
	list_for_each_entry(inst, &core->instances, list) {
		if (inst->session_type != session_type)
			continue;

		mbs_per_sec += load_per_instance(inst);
	}
	mutex_unlock(&core->lock);

	return mbs_per_sec;
}

int venus_helper_load_scale_clocks(struct venus_core *core)
{
	const struct freq_tbl *table = core->res->freq_tbl;
	unsigned int num_rows = core->res->freq_tbl_size;
	unsigned long freq = table[0].freq;
	struct clk *clk = core->clks[0];
	struct device *dev = core->dev;
	u32 mbs_per_sec;
	unsigned int i;
	int ret;

	mbs_per_sec = load_per_type(core, VIDC_SESSION_TYPE_ENC) +
		      load_per_type(core, VIDC_SESSION_TYPE_DEC);

	if (mbs_per_sec > core->res->max_load)
		dev_warn(dev, "HW is overloaded, needed: %d max: %d\n",
			 mbs_per_sec, core->res->max_load);

	if (!mbs_per_sec && num_rows > 1) {
		freq = table[num_rows - 1].freq;
		goto set_freq;
	}

	for (i = 0; i < num_rows; i++) {
		if (mbs_per_sec > table[i].load)
			break;
		freq = table[i].freq;
	}

set_freq:

	ret = clk_set_rate(clk, freq);
	if (ret)
		goto err;

	ret = clk_set_rate(core->core0_clk, freq);
	if (ret)
		goto err;

	ret = clk_set_rate(core->core1_clk, freq);
	if (ret)
		goto err;

	return 0;

err:
	dev_err(dev, "failed to set clock rate %lu (%d)\n", freq, ret);
	return ret;
}
EXPORT_SYMBOL_GPL(venus_helper_load_scale_clocks);

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
		dev_dbg(inst->core->dev, "%s: no free slot\n", __func__);
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
	struct venus_caps *caps;

	/*
	 * v4 doesn't send BUFFER_ALLOC_MODE_SUPPORTED property and supports
	 * dynamic buffer mode by default for HFI_BUFFER_OUTPUT/OUTPUT2.
	 */
	if (IS_V4(core))
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
	default:
		break;
	}

	return 0;
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

u32 venus_helper_get_framesz_raw(u32 hfi_fmt, u32 width, u32 height)
{
	switch (hfi_fmt) {
	case HFI_COLOR_FORMAT_NV12:
	case HFI_COLOR_FORMAT_NV21:
		return get_framesize_raw_nv12(width, height);
	case HFI_COLOR_FORMAT_NV12_UBWC:
		return get_framesize_raw_nv12_ubwc(width, height);
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

int venus_helper_set_work_mode(struct venus_inst *inst, u32 mode)
{
	const u32 ptype = HFI_PROPERTY_PARAM_WORK_MODE;
	struct hfi_video_work_mode wm;

	if (!IS_V4(inst->core))
		return 0;

	wm.video_work_mode = mode;

	return hfi_session_set_property(inst, ptype, &wm);
}
EXPORT_SYMBOL_GPL(venus_helper_set_work_mode);

int venus_helper_set_core_usage(struct venus_inst *inst, u32 usage)
{
	const u32 ptype = HFI_PROPERTY_CONFIG_VIDEOCORES_USAGE;
	struct hfi_videocores_usage_type cu;

	if (!IS_V4(inst->core))
		return 0;

	cu.video_core_enable_mask = usage;

	return hfi_session_set_property(inst, ptype, &cu);
}
EXPORT_SYMBOL_GPL(venus_helper_set_core_usage);

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

int venus_helper_vb2_buf_init(struct vb2_buffer *vb)
{
	struct venus_inst *inst = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct venus_buffer *buf = to_venus_buffer(vbuf);
	struct sg_table *sgt;

	sgt = vb2_dma_sg_plane_desc(vb, 0);
	if (!sgt)
		return -EFAULT;

	buf->size = vb2_plane_size(vb, 0);
	buf->dma_addr = sg_dma_address(sgt->sgl);

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

void venus_helper_vb2_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct venus_inst *inst = vb2_get_drv_priv(vb->vb2_queue);
	struct v4l2_m2m_ctx *m2m_ctx = inst->m2m_ctx;
	int ret;

	mutex_lock(&inst->lock);

	v4l2_m2m_buf_queue(m2m_ctx, vbuf);

	if (inst->session_type == VIDC_SESSION_TYPE_ENC &&
	    !(inst->streamon_out && inst->streamon_cap))
		goto unlock;

	if (vb2_start_streaming_called(vb->vb2_queue)) {
		ret = is_buf_refed(inst, vbuf);
		if (ret)
			goto unlock;

		ret = session_process_buf(inst, vbuf);
		if (ret)
			return_buf_error(inst, vbuf);
	}

unlock:
	mutex_unlock(&inst->lock);
}
EXPORT_SYMBOL_GPL(venus_helper_vb2_buf_queue);

void venus_helper_buffers_done(struct venus_inst *inst,
			       enum vb2_buffer_state state)
{
	struct vb2_v4l2_buffer *buf;

	while ((buf = v4l2_m2m_src_buf_remove(inst->m2m_ctx)))
		v4l2_m2m_buf_done(buf, state);
	while ((buf = v4l2_m2m_dst_buf_remove(inst->m2m_ctx)))
		v4l2_m2m_buf_done(buf, state);
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

		if (inst->session_error || core->sys_error)
			ret = -EIO;

		if (ret)
			hfi_session_abort(inst);

		venus_helper_free_dpb_bufs(inst);

		venus_helper_load_scale_clocks(core);
		INIT_LIST_HEAD(&inst->registeredbufs);
	}

	venus_helper_buffers_done(inst, VB2_BUF_STATE_ERROR);

	if (q->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		inst->streamon_out = 0;
	else
		inst->streamon_cap = 0;

	mutex_unlock(&inst->lock);
}
EXPORT_SYMBOL_GPL(venus_helper_vb2_stop_streaming);

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
	struct venus_core *core = inst->core;
	int ret;

	ret = venus_helper_intbufs_alloc(inst);
	if (ret)
		return ret;

	ret = session_register_bufs(inst);
	if (ret)
		goto err_bufs_free;

	venus_helper_load_scale_clocks(core);

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

void venus_helper_init_instance(struct venus_inst *inst)
{
	if (inst->session_type == VIDC_SESSION_TYPE_DEC) {
		INIT_LIST_HEAD(&inst->delayed_process);
		INIT_WORK(&inst->delayed_process_work,
			  delayed_process_buf_func);
	}
}
EXPORT_SYMBOL_GPL(venus_helper_init_instance);

static bool find_fmt_from_caps(struct venus_caps *caps, u32 buftype, u32 fmt)
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
	struct venus_caps *caps;
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

int venus_helper_power_enable(struct venus_core *core, u32 session_type,
			      bool enable)
{
	void __iomem *ctrl, *stat;
	u32 val;
	int ret;

	if (!IS_V3(core) && !IS_V4(core))
		return 0;

	if (IS_V3(core)) {
		if (session_type == VIDC_SESSION_TYPE_DEC)
			ctrl = core->base + WRAPPER_VDEC_VCODEC_POWER_CONTROL;
		else
			ctrl = core->base + WRAPPER_VENC_VCODEC_POWER_CONTROL;
		if (enable)
			writel(0, ctrl);
		else
			writel(1, ctrl);

		return 0;
	}

	if (session_type == VIDC_SESSION_TYPE_DEC) {
		ctrl = core->base + WRAPPER_VCODEC0_MMCC_POWER_CONTROL;
		stat = core->base + WRAPPER_VCODEC0_MMCC_POWER_STATUS;
	} else {
		ctrl = core->base + WRAPPER_VCODEC1_MMCC_POWER_CONTROL;
		stat = core->base + WRAPPER_VCODEC1_MMCC_POWER_STATUS;
	}

	if (enable) {
		writel(0, ctrl);

		ret = readl_poll_timeout(stat, val, val & BIT(1), 1, 100);
		if (ret)
			return ret;
	} else {
		writel(1, ctrl);

		ret = readl_poll_timeout(stat, val, !(val & BIT(1)), 1, 100);
		if (ret)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(venus_helper_power_enable);
