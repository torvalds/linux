/*
 * Copyright (c) 2012-2016, The Linux Foundation. All rights reserved.
 * Copyright (C) 2017 Linaro Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/clk.h>
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

static const unsigned int intbuf_types[] = {
	HFI_BUFFER_INTERNAL_SCRATCH,
	HFI_BUFFER_INTERNAL_SCRATCH_1,
	HFI_BUFFER_INTERNAL_SCRATCH_2,
	HFI_BUFFER_INTERNAL_PERSIST,
	HFI_BUFFER_INTERNAL_PERSIST_1,
};

static int intbufs_alloc(struct venus_inst *inst)
{
	unsigned int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(intbuf_types); i++) {
		ret = intbufs_set_buffer(inst, intbuf_types[i]);
		if (ret)
			goto error;
	}

	return 0;

error:
	intbufs_unset_buffers(inst);
	return ret;
}

static int intbufs_free(struct venus_inst *inst)
{
	return intbufs_unset_buffers(inst);
}

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

static int load_scale_clocks(struct venus_core *core)
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

	if (core->res->hfi_version == HFI_VERSION_3XX) {
		ret = clk_set_rate(clk, freq);
		ret |= clk_set_rate(core->core0_clk, freq);
		ret |= clk_set_rate(core->core1_clk, freq);
	} else {
		ret = clk_set_rate(clk, freq);
	}

	if (ret) {
		dev_err(dev, "failed to set clock rate %lu (%d)\n", freq, ret);
		return ret;
	}

	return 0;
}

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

	if (!fdata.timestamp)
		fdata.flags |= HFI_BUFFERFLAG_TIMESTAMPINVALID;

	if (type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		fdata.buffer_type = HFI_BUFFER_INPUT;
		fdata.filled_len = vb2_get_plane_payload(vb, 0);
		fdata.offset = vb->planes[0].data_offset;

		if (vbuf->flags & V4L2_BUF_FLAG_LAST || !fdata.filled_len)
			fdata.flags |= HFI_BUFFERFLAG_EOS;
	} else if (type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		fdata.buffer_type = HFI_BUFFER_OUTPUT;
		fdata.filled_len = 0;
		fdata.offset = 0;
	}

	ret = hfi_session_process_buf(inst, &fdata);
	if (ret)
		return ret;

	return 0;
}

static inline int is_reg_unreg_needed(struct venus_inst *inst)
{
	if (inst->session_type == VIDC_SESSION_TYPE_DEC &&
	    inst->core->res->hfi_version == HFI_VERSION_3XX)
		return 0;

	if (inst->session_type == VIDC_SESSION_TYPE_DEC &&
	    inst->cap_bufs_mode_dynamic &&
	    inst->core->res->hfi_version == HFI_VERSION_1XX)
		return 0;

	return 1;
}

static int session_unregister_bufs(struct venus_inst *inst)
{
	struct venus_buffer *buf, *n;
	struct hfi_buffer_desc bd;
	int ret = 0;

	if (!is_reg_unreg_needed(inst))
		return 0;

	list_for_each_entry_safe(buf, n, &inst->registeredbufs, reg_list) {
		fill_buffer_desc(buf, &bd, true);
		ret = hfi_session_unset_buffers(inst, &bd);
		list_del_init(&buf->reg_list);
	}

	return ret;
}

static int session_register_bufs(struct venus_inst *inst)
{
	struct venus_core *core = inst->core;
	struct device *dev = core->dev;
	struct hfi_buffer_desc bd;
	struct venus_buffer *buf;
	int ret = 0;

	if (!is_reg_unreg_needed(inst))
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
				       unsigned int width, unsigned int height)
{
	u32 ptype = HFI_PROPERTY_PARAM_FRAME_SIZE;
	struct hfi_framesize fs;

	fs.buffer_type = HFI_BUFFER_OUTPUT;
	fs.width = width;
	fs.height = height;

	return hfi_session_set_property(inst, ptype, &fs);
}
EXPORT_SYMBOL_GPL(venus_helper_set_output_resolution);

int venus_helper_set_num_bufs(struct venus_inst *inst, unsigned int input_bufs,
			      unsigned int output_bufs)
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

	return hfi_session_set_property(inst, ptype, &buf_count);
}
EXPORT_SYMBOL_GPL(venus_helper_set_num_bufs);

int venus_helper_set_color_format(struct venus_inst *inst, u32 pixfmt)
{
	struct hfi_uncompressed_format_select fmt;
	u32 ptype = HFI_PROPERTY_PARAM_UNCOMPRESSED_FORMAT_SELECT;
	int ret;

	if (inst->session_type == VIDC_SESSION_TYPE_DEC)
		fmt.buffer_type = HFI_BUFFER_OUTPUT;
	else if (inst->session_type == VIDC_SESSION_TYPE_ENC)
		fmt.buffer_type = HFI_BUFFER_INPUT;
	else
		return -EINVAL;

	switch (pixfmt) {
	case V4L2_PIX_FMT_NV12:
		fmt.format = HFI_COLOR_FORMAT_NV12;
		break;
	case V4L2_PIX_FMT_NV21:
		fmt.format = HFI_COLOR_FORMAT_NV21;
		break;
	default:
		return -EINVAL;
	}

	ret = hfi_session_set_property(inst, ptype, &fmt);
	if (ret)
		return ret;

	return 0;
}
EXPORT_SYMBOL_GPL(venus_helper_set_color_format);

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

	if (vb->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE &&
	    vb2_plane_size(vb, 0) < inst->output_buf_size)
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

	if (!(inst->streamon_out & inst->streamon_cap))
		goto unlock;

	ret = is_buf_refed(inst, vbuf);
	if (ret)
		goto unlock;

	ret = session_process_buf(inst, vbuf);
	if (ret)
		return_buf_error(inst, vbuf);

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
		ret |= session_unregister_bufs(inst);
		ret |= intbufs_free(inst);
		ret |= hfi_session_deinit(inst);

		if (inst->session_error || core->sys_error)
			ret = -EIO;

		if (ret)
			hfi_session_abort(inst);

		load_scale_clocks(core);
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

int venus_helper_vb2_start_streaming(struct venus_inst *inst)
{
	struct venus_core *core = inst->core;
	int ret;

	ret = intbufs_alloc(inst);
	if (ret)
		return ret;

	ret = session_register_bufs(inst);
	if (ret)
		goto err_bufs_free;

	load_scale_clocks(core);

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
	session_unregister_bufs(inst);
err_bufs_free:
	intbufs_free(inst);
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
