// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <media/v4l2-event.h>
#include <media/v4l2-mem2mem.h>

#include "iris_buffer.h"
#include "iris_instance.h"
#include "iris_power.h"
#include "iris_vpu_buffer.h"

#define PIXELS_4K 4096
#define MAX_WIDTH 4096
#define MAX_HEIGHT 2304
#define Y_STRIDE_ALIGN 128
#define UV_STRIDE_ALIGN 128
#define Y_SCANLINE_ALIGN 32
#define UV_SCANLINE_ALIGN 16
#define UV_SCANLINE_ALIGN_QC08C 32
#define META_STRIDE_ALIGNED 64
#define META_SCANLINE_ALIGNED 16
#define NUM_MBS_4K (DIV_ROUND_UP(MAX_WIDTH, 16) * DIV_ROUND_UP(MAX_HEIGHT, 16))

/*
 * NV12:
 * YUV 4:2:0 image with a plane of 8 bit Y samples followed
 * by an interleaved U/V plane containing 8 bit 2x2 subsampled
 * colour difference samples.
 *
 * <-Y/UV_Stride (aligned to 128)->
 * <------- Width ------->
 * Y Y Y Y Y Y Y Y Y Y Y Y . . . .  ^           ^
 * Y Y Y Y Y Y Y Y Y Y Y Y . . . .  |           |
 * Y Y Y Y Y Y Y Y Y Y Y Y . . . .  Height      |
 * Y Y Y Y Y Y Y Y Y Y Y Y . . . .  |          y_scanlines (aligned to 32)
 * Y Y Y Y Y Y Y Y Y Y Y Y . . . .  |           |
 * Y Y Y Y Y Y Y Y Y Y Y Y . . . .  |           |
 * Y Y Y Y Y Y Y Y Y Y Y Y . . . .  |           |
 * Y Y Y Y Y Y Y Y Y Y Y Y . . . .  V           |
 * . . . . . . . . . . . . . . . .              |
 * . . . . . . . . . . . . . . . .              |
 * . . . . . . . . . . . . . . . .              |
 * . . . . . . . . . . . . . . . .              V
 * U V U V U V U V U V U V . . . .  ^
 * U V U V U V U V U V U V . . . .  |
 * U V U V U V U V U V U V . . . .  |
 * U V U V U V U V U V U V . . . .  uv_scanlines (aligned to 16)
 * . . . . . . . . . . . . . . . .  |
 * . . . . . . . . . . . . . . . .  V
 * . . . . . . . . . . . . . . . .  --> Buffer size aligned to 4K
 *
 * y_stride : Width aligned to 128
 * uv_stride : Width aligned to 128
 * y_scanlines: Height aligned to 32
 * uv_scanlines: Height/2 aligned to 16
 * Total size = align((y_stride * y_scanlines
 *          + uv_stride * uv_scanlines , 4096)
 *
 * Note: All the alignments are hardware requirements.
 */
static u32 iris_yuv_buffer_size_nv12(struct iris_inst *inst)
{
	u32 y_plane, uv_plane, y_stride, uv_stride, y_scanlines, uv_scanlines;
	struct v4l2_format *f = inst->fmt_dst;

	y_stride = ALIGN(f->fmt.pix_mp.width, Y_STRIDE_ALIGN);
	uv_stride = ALIGN(f->fmt.pix_mp.width, UV_STRIDE_ALIGN);
	y_scanlines = ALIGN(f->fmt.pix_mp.height, Y_SCANLINE_ALIGN);
	uv_scanlines = ALIGN((f->fmt.pix_mp.height + 1) >> 1, UV_SCANLINE_ALIGN);
	y_plane = y_stride * y_scanlines;
	uv_plane = uv_stride * uv_scanlines;

	return ALIGN(y_plane + uv_plane, PIXELS_4K);
}

/*
 * QC08C:
 * Compressed Macro-tile format for NV12.
 * Contains 4 planes in the following order -
 * (A) Y_Meta_Plane
 * (B) Y_UBWC_Plane
 * (C) UV_Meta_Plane
 * (D) UV_UBWC_Plane
 *
 * Y_Meta_Plane consists of meta information to decode compressed
 * tile data in Y_UBWC_Plane.
 * Y_UBWC_Plane consists of Y data in compressed macro-tile format.
 * UBWC decoder block will use the Y_Meta_Plane data together with
 * Y_UBWC_Plane data to produce loss-less uncompressed 8 bit Y samples.
 *
 * UV_Meta_Plane consists of meta information to decode compressed
 * tile data in UV_UBWC_Plane.
 * UV_UBWC_Plane consists of UV data in compressed macro-tile format.
 * UBWC decoder block will use UV_Meta_Plane data together with
 * UV_UBWC_Plane data to produce loss-less uncompressed 8 bit 2x2
 * subsampled color difference samples.
 *
 * Each tile in Y_UBWC_Plane/UV_UBWC_Plane is independently decodable
 * and randomly accessible. There is no dependency between tiles.
 *
 * <----- y_meta_stride ----> (aligned to 64)
 * <-------- Width ------>
 * M M M M M M M M M M M M . .      ^           ^
 * M M M M M M M M M M M M . .      |           |
 * M M M M M M M M M M M M . .      Height      |
 * M M M M M M M M M M M M . .      |         y_meta_scanlines  (aligned to 16)
 * M M M M M M M M M M M M . .      |           |
 * M M M M M M M M M M M M . .      |           |
 * M M M M M M M M M M M M . .      |           |
 * M M M M M M M M M M M M . .      V           |
 * . . . . . . . . . . . . . .                  |
 * . . . . . . . . . . . . . .                  |
 * . . . . . . . . . . . . . .      -------> Buffer size aligned to 4k
 * . . . . . . . . . . . . . .                  V
 * <--Compressed tile y_stride---> (aligned to 128)
 * <------- Width ------->
 * Y* Y* Y* Y* Y* Y* Y* Y* . . . .  ^           ^
 * Y* Y* Y* Y* Y* Y* Y* Y* . . . .  |           |
 * Y* Y* Y* Y* Y* Y* Y* Y* . . . .  Height      |
 * Y* Y* Y* Y* Y* Y* Y* Y* . . . .  |        Macro_tile y_scanlines (aligned to 32)
 * Y* Y* Y* Y* Y* Y* Y* Y* . . . .  |           |
 * Y* Y* Y* Y* Y* Y* Y* Y* . . . .  |           |
 * Y* Y* Y* Y* Y* Y* Y* Y* . . . .  |           |
 * Y* Y* Y* Y* Y* Y* Y* Y* . . . .  V           |
 * . . . . . . . . . . . . . . . .              |
 * . . . . . . . . . . . . . . . .              |
 * . . . . . . . . . . . . . . . .  -------> Buffer size aligned to 4k
 * . . . . . . . . . . . . . . . .              V
 * <----- uv_meta_stride ---->  (aligned to 64)
 * M M M M M M M M M M M M . .      ^
 * M M M M M M M M M M M M . .      |
 * M M M M M M M M M M M M . .      |
 * M M M M M M M M M M M M . .      uv_meta_scanlines (aligned to 16)
 * . . . . . . . . . . . . . .      |
 * . . . . . . . . . . . . . .      V
 * . . . . . . . . . . . . . .      -------> Buffer size aligned to 4k
 * <--Compressed tile uv_stride---> (aligned to 128)
 * U* V* U* V* U* V* U* V* . . . .  ^
 * U* V* U* V* U* V* U* V* . . . .  |
 * U* V* U* V* U* V* U* V* . . . .  |
 * U* V* U* V* U* V* U* V* . . . .  uv_scanlines (aligned to 32)
 * . . . . . . . . . . . . . . . .  |
 * . . . . . . . . . . . . . . . .  V
 * . . . . . . . . . . . . . . . .  -------> Buffer size aligned to 4k
 *
 * y_stride: width aligned to 128
 * uv_stride: width aligned to 128
 * y_scanlines: height aligned to 32
 * uv_scanlines: height aligned to 32
 * y_plane: buffer size aligned to 4096
 * uv_plane: buffer size aligned to 4096
 * y_meta_stride: width aligned to 64
 * y_meta_scanlines: height aligned to 16
 * y_meta_plane: buffer size aligned to 4096
 * uv_meta_stride: width aligned to 64
 * uv_meta_scanlines: height aligned to 16
 * uv_meta_plane: buffer size aligned to 4096
 *
 * Total size = align( y_plane + uv_plane +
 *           y_meta_plane + uv_meta_plane, 4096)
 *
 * Note: All the alignments are hardware requirements.
 */
static u32 iris_yuv_buffer_size_qc08c(struct iris_inst *inst)
{
	u32 y_plane, uv_plane, y_stride, uv_stride;
	struct v4l2_format *f = inst->fmt_dst;
	u32 uv_meta_stride, uv_meta_plane;
	u32 y_meta_stride, y_meta_plane;

	y_meta_stride = ALIGN(DIV_ROUND_UP(f->fmt.pix_mp.width, META_STRIDE_ALIGNED >> 1),
			      META_STRIDE_ALIGNED);
	y_meta_plane = y_meta_stride * ALIGN(DIV_ROUND_UP(f->fmt.pix_mp.height,
							  META_SCANLINE_ALIGNED >> 1),
					     META_SCANLINE_ALIGNED);
	y_meta_plane = ALIGN(y_meta_plane, PIXELS_4K);

	y_stride = ALIGN(f->fmt.pix_mp.width, Y_STRIDE_ALIGN);
	y_plane = ALIGN(y_stride * ALIGN(f->fmt.pix_mp.height, Y_SCANLINE_ALIGN), PIXELS_4K);

	uv_meta_stride = ALIGN(DIV_ROUND_UP(f->fmt.pix_mp.width / 2, META_STRIDE_ALIGNED >> 2),
			       META_STRIDE_ALIGNED);
	uv_meta_plane = uv_meta_stride * ALIGN(DIV_ROUND_UP(f->fmt.pix_mp.height / 2,
							    META_SCANLINE_ALIGNED >> 1),
					       META_SCANLINE_ALIGNED);
	uv_meta_plane = ALIGN(uv_meta_plane, PIXELS_4K);

	uv_stride = ALIGN(f->fmt.pix_mp.width, UV_STRIDE_ALIGN);
	uv_plane = ALIGN(uv_stride * ALIGN(f->fmt.pix_mp.height / 2, UV_SCANLINE_ALIGN_QC08C),
			 PIXELS_4K);

	return ALIGN(y_meta_plane + y_plane + uv_meta_plane + uv_plane, PIXELS_4K);
}

static u32 iris_bitstream_buffer_size(struct iris_inst *inst)
{
	struct platform_inst_caps *caps = inst->core->iris_platform_data->inst_caps;
	u32 base_res_mbs = NUM_MBS_4K;
	u32 frame_size, num_mbs;
	u32 div_factor = 2;

	num_mbs = iris_get_mbpf(inst);
	if (num_mbs > NUM_MBS_4K) {
		div_factor = 4;
		base_res_mbs = caps->max_mbpf;
	}

	/*
	 * frame_size = YUVsize / div_factor
	 * where YUVsize = resolution_in_MBs * MBs_in_pixel * 3 / 2
	 */
	frame_size = base_res_mbs * (16 * 16) * 3 / 2 / div_factor;

	return ALIGN(frame_size, PIXELS_4K);
}

int iris_get_buffer_size(struct iris_inst *inst,
			 enum iris_buffer_type buffer_type)
{
	switch (buffer_type) {
	case BUF_INPUT:
		return iris_bitstream_buffer_size(inst);
	case BUF_OUTPUT:
		return iris_yuv_buffer_size_nv12(inst);
	case BUF_DPB:
		return iris_yuv_buffer_size_qc08c(inst);
	default:
		return 0;
	}
}

static void iris_fill_internal_buf_info(struct iris_inst *inst,
					enum iris_buffer_type buffer_type)
{
	struct iris_buffers *buffers = &inst->buffers[buffer_type];

	buffers->size = iris_vpu_buf_size(inst, buffer_type);
	buffers->min_count = iris_vpu_buf_count(inst, buffer_type);
}

void iris_get_internal_buffers(struct iris_inst *inst, u32 plane)
{
	const struct iris_platform_data *platform_data = inst->core->iris_platform_data;
	const u32 *internal_buf_type;
	u32 internal_buffer_count, i;

	if (V4L2_TYPE_IS_OUTPUT(plane)) {
		internal_buf_type = platform_data->dec_ip_int_buf_tbl;
		internal_buffer_count = platform_data->dec_ip_int_buf_tbl_size;
		for (i = 0; i < internal_buffer_count; i++)
			iris_fill_internal_buf_info(inst, internal_buf_type[i]);
	} else {
		internal_buf_type = platform_data->dec_op_int_buf_tbl;
		internal_buffer_count = platform_data->dec_op_int_buf_tbl_size;
		for (i = 0; i < internal_buffer_count; i++)
			iris_fill_internal_buf_info(inst, internal_buf_type[i]);
	}
}

static int iris_create_internal_buffer(struct iris_inst *inst,
				       enum iris_buffer_type buffer_type, u32 index)
{
	struct iris_buffers *buffers = &inst->buffers[buffer_type];
	struct iris_core *core = inst->core;
	struct iris_buffer *buffer;

	if (!buffers->size)
		return 0;

	buffer = kzalloc(sizeof(*buffer), GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	INIT_LIST_HEAD(&buffer->list);
	buffer->type = buffer_type;
	buffer->index = index;
	buffer->buffer_size = buffers->size;
	buffer->dma_attrs = DMA_ATTR_WRITE_COMBINE | DMA_ATTR_NO_KERNEL_MAPPING;
	list_add_tail(&buffer->list, &buffers->list);

	buffer->kvaddr = dma_alloc_attrs(core->dev, buffer->buffer_size,
					 &buffer->device_addr, GFP_KERNEL, buffer->dma_attrs);
	if (!buffer->kvaddr)
		return -ENOMEM;

	return 0;
}

int iris_create_internal_buffers(struct iris_inst *inst, u32 plane)
{
	const struct iris_platform_data *platform_data = inst->core->iris_platform_data;
	u32 internal_buffer_count, i, j;
	struct iris_buffers *buffers;
	const u32 *internal_buf_type;
	int ret;

	if (V4L2_TYPE_IS_OUTPUT(plane)) {
		internal_buf_type = platform_data->dec_ip_int_buf_tbl;
		internal_buffer_count = platform_data->dec_ip_int_buf_tbl_size;
	} else {
		internal_buf_type = platform_data->dec_op_int_buf_tbl;
		internal_buffer_count = platform_data->dec_op_int_buf_tbl_size;
	}

	for (i = 0; i < internal_buffer_count; i++) {
		buffers = &inst->buffers[internal_buf_type[i]];
		for (j = 0; j < buffers->min_count; j++) {
			ret = iris_create_internal_buffer(inst, internal_buf_type[i], j);
			if (ret)
				return ret;
		}
	}

	return 0;
}

int iris_queue_buffer(struct iris_inst *inst, struct iris_buffer *buf)
{
	const struct iris_hfi_command_ops *hfi_ops = inst->core->hfi_ops;
	int ret;

	ret = hfi_ops->session_queue_buf(inst, buf);
	if (ret)
		return ret;

	buf->attr &= ~BUF_ATTR_DEFERRED;
	buf->attr |= BUF_ATTR_QUEUED;

	return 0;
}

int iris_queue_internal_buffers(struct iris_inst *inst, u32 plane)
{
	const struct iris_platform_data *platform_data = inst->core->iris_platform_data;
	struct iris_buffer *buffer, *next;
	struct iris_buffers *buffers;
	const u32 *internal_buf_type;
	u32 internal_buffer_count, i;
	int ret;

	if (V4L2_TYPE_IS_OUTPUT(plane)) {
		internal_buf_type = platform_data->dec_ip_int_buf_tbl;
		internal_buffer_count = platform_data->dec_ip_int_buf_tbl_size;
	} else {
		internal_buf_type = platform_data->dec_op_int_buf_tbl;
		internal_buffer_count = platform_data->dec_op_int_buf_tbl_size;
	}

	for (i = 0; i < internal_buffer_count; i++) {
		buffers = &inst->buffers[internal_buf_type[i]];
		list_for_each_entry_safe(buffer, next, &buffers->list, list) {
			if (buffer->attr & BUF_ATTR_PENDING_RELEASE)
				continue;
			if (buffer->attr & BUF_ATTR_QUEUED)
				continue;
			ret = iris_queue_buffer(inst, buffer);
			if (ret)
				return ret;
		}
	}

	return 0;
}

int iris_destroy_internal_buffer(struct iris_inst *inst, struct iris_buffer *buffer)
{
	struct iris_core *core = inst->core;

	list_del(&buffer->list);
	dma_free_attrs(core->dev, buffer->buffer_size, buffer->kvaddr,
		       buffer->device_addr, buffer->dma_attrs);
	kfree(buffer);

	return 0;
}

int iris_destroy_internal_buffers(struct iris_inst *inst, u32 plane)
{
	const struct iris_platform_data *platform_data = inst->core->iris_platform_data;
	struct iris_buffer *buf, *next;
	struct iris_buffers *buffers;
	const u32 *internal_buf_type;
	u32 i, len;
	int ret;

	if (V4L2_TYPE_IS_OUTPUT(plane)) {
		internal_buf_type = platform_data->dec_ip_int_buf_tbl;
		len = platform_data->dec_ip_int_buf_tbl_size;
	} else {
		internal_buf_type = platform_data->dec_op_int_buf_tbl;
		len = platform_data->dec_op_int_buf_tbl_size;
	}

	for (i = 0; i < len; i++) {
		buffers = &inst->buffers[internal_buf_type[i]];
		list_for_each_entry_safe(buf, next, &buffers->list, list) {
			ret = iris_destroy_internal_buffer(inst, buf);
			if (ret)
				return ret;
		}
	}

	return 0;
}

static int iris_release_internal_buffers(struct iris_inst *inst,
					 enum iris_buffer_type buffer_type)
{
	const struct iris_hfi_command_ops *hfi_ops = inst->core->hfi_ops;
	struct iris_buffers *buffers = &inst->buffers[buffer_type];
	struct iris_buffer *buffer, *next;
	int ret;

	list_for_each_entry_safe(buffer, next, &buffers->list, list) {
		if (buffer->attr & BUF_ATTR_PENDING_RELEASE)
			continue;
		if (!(buffer->attr & BUF_ATTR_QUEUED))
			continue;
		ret = hfi_ops->session_release_buf(inst, buffer);
		if (ret)
			return ret;
		buffer->attr |= BUF_ATTR_PENDING_RELEASE;
	}

	return 0;
}

static int iris_release_input_internal_buffers(struct iris_inst *inst)
{
	const struct iris_platform_data *platform_data = inst->core->iris_platform_data;
	const u32 *internal_buf_type;
	u32 internal_buffer_count, i;
	int ret;

	internal_buf_type = platform_data->dec_ip_int_buf_tbl;
	internal_buffer_count = platform_data->dec_ip_int_buf_tbl_size;

	for (i = 0; i < internal_buffer_count; i++) {
		ret = iris_release_internal_buffers(inst, internal_buf_type[i]);
		if (ret)
			return ret;
	}

	return 0;
}

int iris_alloc_and_queue_persist_bufs(struct iris_inst *inst)
{
	struct iris_buffers *buffers = &inst->buffers[BUF_PERSIST];
	struct iris_buffer *buffer, *next;
	int ret;
	u32 i;

	if (!list_empty(&buffers->list))
		return 0;

	iris_fill_internal_buf_info(inst, BUF_PERSIST);

	for (i = 0; i < buffers->min_count; i++) {
		ret = iris_create_internal_buffer(inst, BUF_PERSIST, i);
		if (ret)
			return ret;
	}

	list_for_each_entry_safe(buffer, next, &buffers->list, list) {
		if (buffer->attr & BUF_ATTR_PENDING_RELEASE)
			continue;
		if (buffer->attr & BUF_ATTR_QUEUED)
			continue;
		ret = iris_queue_buffer(inst, buffer);
		if (ret)
			return ret;
	}

	return 0;
}

int iris_alloc_and_queue_input_int_bufs(struct iris_inst *inst)
{
	int ret;

	iris_get_internal_buffers(inst, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);

	ret = iris_release_input_internal_buffers(inst);
	if (ret)
		return ret;

	ret = iris_create_internal_buffers(inst, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
	if (ret)
		return ret;

	return iris_queue_internal_buffers(inst, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
}

int iris_queue_deferred_buffers(struct iris_inst *inst, enum iris_buffer_type buf_type)
{
	struct v4l2_m2m_ctx *m2m_ctx = inst->m2m_ctx;
	struct v4l2_m2m_buffer *buffer, *n;
	struct iris_buffer *buf;
	int ret;

	iris_scale_power(inst);

	if (buf_type == BUF_INPUT) {
		v4l2_m2m_for_each_src_buf_safe(m2m_ctx, buffer, n) {
			buf = to_iris_buffer(&buffer->vb);
			if (!(buf->attr & BUF_ATTR_DEFERRED))
				continue;
			ret = iris_queue_buffer(inst, buf);
			if (ret)
				return ret;
		}
	} else {
		v4l2_m2m_for_each_dst_buf_safe(m2m_ctx, buffer, n) {
			buf = to_iris_buffer(&buffer->vb);
			if (!(buf->attr & BUF_ATTR_DEFERRED))
				continue;
			ret = iris_queue_buffer(inst, buf);
			if (ret)
				return ret;
		}
	}

	return 0;
}

void iris_vb2_queue_error(struct iris_inst *inst)
{
	struct v4l2_m2m_ctx *m2m_ctx = inst->m2m_ctx;
	struct vb2_queue *q;

	q = v4l2_m2m_get_src_vq(m2m_ctx);
	vb2_queue_error(q);
	q = v4l2_m2m_get_dst_vq(m2m_ctx);
	vb2_queue_error(q);
}

static struct vb2_v4l2_buffer *
iris_helper_find_buf(struct iris_inst *inst, u32 type, u32 idx)
{
	struct v4l2_m2m_ctx *m2m_ctx = inst->m2m_ctx;

	if (V4L2_TYPE_IS_OUTPUT(type))
		return v4l2_m2m_src_buf_remove_by_idx(m2m_ctx, idx);
	else
		return v4l2_m2m_dst_buf_remove_by_idx(m2m_ctx, idx);
}

static void iris_get_ts_metadata(struct iris_inst *inst, u64 timestamp_ns,
				 struct vb2_v4l2_buffer *vbuf)
{
	u32 mask = V4L2_BUF_FLAG_TIMECODE | V4L2_BUF_FLAG_TSTAMP_SRC_MASK;
	u32 i;

	for (i = 0; i < ARRAY_SIZE(inst->tss); ++i) {
		if (inst->tss[i].ts_ns != timestamp_ns)
			continue;

		vbuf->flags &= ~mask;
		vbuf->flags |= inst->tss[i].flags;
		vbuf->timecode = inst->tss[i].tc;
		return;
	}

	vbuf->flags &= ~mask;
	vbuf->flags |= inst->tss[inst->metadata_idx].flags;
	vbuf->timecode = inst->tss[inst->metadata_idx].tc;
}

int iris_vb2_buffer_done(struct iris_inst *inst, struct iris_buffer *buf)
{
	struct v4l2_m2m_ctx *m2m_ctx = inst->m2m_ctx;
	struct vb2_v4l2_buffer *vbuf;
	struct vb2_buffer *vb2;
	u32 type, state;

	switch (buf->type) {
	case BUF_INPUT:
		type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
		break;
	case BUF_OUTPUT:
		type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		break;
	default:
		return 0; /* Internal DPB Buffers */
	}

	vbuf = iris_helper_find_buf(inst, type, buf->index);
	if (!vbuf)
		return -EINVAL;

	vb2 = &vbuf->vb2_buf;

	if (buf->flags & V4L2_BUF_FLAG_ERROR)
		state = VB2_BUF_STATE_ERROR;
	else
		state = VB2_BUF_STATE_DONE;

	vbuf->flags |= buf->flags;

	if (V4L2_TYPE_IS_CAPTURE(type)) {
		vb2_set_plane_payload(vb2, 0, buf->data_size);
		vbuf->sequence = inst->sequence_cap++;
		iris_get_ts_metadata(inst, buf->timestamp, vbuf);
	} else {
		vbuf->sequence = inst->sequence_out++;
	}

	if (vbuf->flags & V4L2_BUF_FLAG_LAST) {
		if (!v4l2_m2m_has_stopped(m2m_ctx)) {
			const struct v4l2_event ev = { .type = V4L2_EVENT_EOS };

			v4l2_event_queue_fh(&inst->fh, &ev);
			v4l2_m2m_mark_stopped(m2m_ctx);
		}
	}
	vb2->timestamp = buf->timestamp;
	v4l2_m2m_buf_done(vbuf, state);

	return 0;
}
