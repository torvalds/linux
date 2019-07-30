// SPDX-License-Identifier: GPL-2.0-only
/*
 * A virtual codec example device.
 *
 * Copyright 2018 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 *
 * This is a virtual codec device driver for testing the codec framework.
 * It simulates a device that uses memory buffers for both source and
 * destination and encodes or decodes the data.
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include <linux/platform_device.h>
#include <media/v4l2-mem2mem.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-event.h>
#include <media/videobuf2-vmalloc.h>

#include "codec-v4l2-fwht.h"

MODULE_DESCRIPTION("Virtual codec device");
MODULE_AUTHOR("Hans Verkuil <hans.verkuil@cisco.com>");
MODULE_LICENSE("GPL v2");

static bool multiplanar;
module_param(multiplanar, bool, 0444);
MODULE_PARM_DESC(multiplanar,
		 " use multi-planar API instead of single-planar API");

static unsigned int debug;
module_param(debug, uint, 0644);
MODULE_PARM_DESC(debug, " activates debug info");

#define VICODEC_NAME		"vicodec"
#define MAX_WIDTH		4096U
#define MIN_WIDTH		640U
#define MAX_HEIGHT		2160U
#define MIN_HEIGHT		360U

#define dprintk(dev, fmt, arg...) \
	v4l2_dbg(1, debug, &dev->v4l2_dev, "%s: " fmt, __func__, ## arg)


struct pixfmt_info {
	u32 id;
	unsigned int bytesperline_mult;
	unsigned int sizeimage_mult;
	unsigned int sizeimage_div;
	unsigned int luma_step;
	unsigned int chroma_step;
	/* Chroma plane subsampling */
	unsigned int width_div;
	unsigned int height_div;
};

static const struct v4l2_fwht_pixfmt_info pixfmt_fwht = {
	V4L2_PIX_FMT_FWHT, 0, 3, 1, 1, 1, 1, 1, 0, 1
};

static void vicodec_dev_release(struct device *dev)
{
}

static struct platform_device vicodec_pdev = {
	.name		= VICODEC_NAME,
	.dev.release	= vicodec_dev_release,
};

/* Per-queue, driver-specific private data */
struct vicodec_q_data {
	unsigned int		coded_width;
	unsigned int		coded_height;
	unsigned int		visible_width;
	unsigned int		visible_height;
	unsigned int		sizeimage;
	unsigned int		sequence;
	const struct v4l2_fwht_pixfmt_info *info;
};

enum {
	V4L2_M2M_SRC = 0,
	V4L2_M2M_DST = 1,
};

struct vicodec_dev {
	struct v4l2_device	v4l2_dev;
	struct video_device	enc_vfd;
	struct video_device	dec_vfd;
#ifdef CONFIG_MEDIA_CONTROLLER
	struct media_device	mdev;
#endif

	struct mutex		enc_mutex;
	struct mutex		dec_mutex;
	spinlock_t		enc_lock;
	spinlock_t		dec_lock;

	struct v4l2_m2m_dev	*enc_dev;
	struct v4l2_m2m_dev	*dec_dev;
};

struct vicodec_ctx {
	struct v4l2_fh		fh;
	struct vicodec_dev	*dev;
	bool			is_enc;
	spinlock_t		*lock;

	struct v4l2_ctrl_handler hdl;

	struct vb2_v4l2_buffer *last_src_buf;
	struct vb2_v4l2_buffer *last_dst_buf;

	/* Source and destination queue data */
	struct vicodec_q_data   q_data[2];
	struct v4l2_fwht_state	state;

	u32			cur_buf_offset;
	u32			comp_max_size;
	u32			comp_size;
	u32			header_size;
	u32			comp_magic_cnt;
	bool			comp_has_frame;
	bool			comp_has_next_frame;
	bool			first_source_change_sent;
	bool			source_changed;
};

static inline struct vicodec_ctx *file2ctx(struct file *file)
{
	return container_of(file->private_data, struct vicodec_ctx, fh);
}

static struct vicodec_q_data *get_q_data(struct vicodec_ctx *ctx,
					 enum v4l2_buf_type type)
{
	switch (type) {
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		return &ctx->q_data[V4L2_M2M_SRC];
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		return &ctx->q_data[V4L2_M2M_DST];
	default:
		WARN_ON(1);
		break;
	}
	return NULL;
}

static int device_process(struct vicodec_ctx *ctx,
			  struct vb2_v4l2_buffer *src_vb,
			  struct vb2_v4l2_buffer *dst_vb)
{
	struct vicodec_dev *dev = ctx->dev;
	struct vicodec_q_data *q_dst;
	struct v4l2_fwht_state *state = &ctx->state;
	u8 *p_src, *p_dst;
	int ret;

	q_dst = get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE);
	if (ctx->is_enc)
		p_src = vb2_plane_vaddr(&src_vb->vb2_buf, 0);
	else
		p_src = state->compressed_frame;
	p_dst = vb2_plane_vaddr(&dst_vb->vb2_buf, 0);
	if (!p_src || !p_dst) {
		v4l2_err(&dev->v4l2_dev,
			 "Acquiring kernel pointers to buffers failed\n");
		return -EFAULT;
	}

	if (ctx->is_enc) {
		struct vicodec_q_data *q_src;

		q_src = get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT);
		state->info = q_src->info;
		ret = v4l2_fwht_encode(state, p_src, p_dst);
		if (ret < 0)
			return ret;
		vb2_set_plane_payload(&dst_vb->vb2_buf, 0, ret);
	} else {
		unsigned int comp_frame_size = ntohl(ctx->state.header.size);

		if (comp_frame_size > ctx->comp_max_size)
			return -EINVAL;
		state->info = q_dst->info;
		ret = v4l2_fwht_decode(state, p_src, p_dst);
		if (ret < 0)
			return ret;
		vb2_set_plane_payload(&dst_vb->vb2_buf, 0, q_dst->sizeimage);
	}

	dst_vb->sequence = q_dst->sequence++;
	dst_vb->flags &= ~V4L2_BUF_FLAG_LAST;
	v4l2_m2m_buf_copy_metadata(src_vb, dst_vb, !ctx->is_enc);

	return 0;
}

/*
 * mem2mem callbacks
 */
static enum vb2_buffer_state get_next_header(struct vicodec_ctx *ctx,
					     u8 **pp, u32 sz)
{
	static const u8 magic[] = {
		0x4f, 0x4f, 0x4f, 0x4f, 0xff, 0xff, 0xff, 0xff
	};
	u8 *p = *pp;
	u32 state;
	u8 *header = (u8 *)&ctx->state.header;

	state = VB2_BUF_STATE_DONE;

	if (!ctx->header_size) {
		state = VB2_BUF_STATE_ERROR;
		for (; p < *pp + sz; p++) {
			u32 copy;

			p = memchr(p, magic[ctx->comp_magic_cnt],
				   *pp + sz - p);
			if (!p) {
				ctx->comp_magic_cnt = 0;
				p = *pp + sz;
				break;
			}
			copy = sizeof(magic) - ctx->comp_magic_cnt;
			if (*pp + sz - p < copy)
				copy = *pp + sz - p;

			memcpy(header + ctx->comp_magic_cnt, p, copy);
			ctx->comp_magic_cnt += copy;
			if (!memcmp(header, magic, ctx->comp_magic_cnt)) {
				p += copy;
				state = VB2_BUF_STATE_DONE;
				break;
			}
			ctx->comp_magic_cnt = 0;
		}
		if (ctx->comp_magic_cnt < sizeof(magic)) {
			*pp = p;
			return state;
		}
		ctx->header_size = sizeof(magic);
	}

	if (ctx->header_size < sizeof(struct fwht_cframe_hdr)) {
		u32 copy = sizeof(struct fwht_cframe_hdr) - ctx->header_size;

		if (*pp + sz - p < copy)
			copy = *pp + sz - p;

		memcpy(header + ctx->header_size, p, copy);
		p += copy;
		ctx->header_size += copy;
	}
	*pp = p;
	return state;
}

/* device_run() - prepares and starts the device */
static void device_run(void *priv)
{
	static const struct v4l2_event eos_event = {
		.type = V4L2_EVENT_EOS
	};
	struct vicodec_ctx *ctx = priv;
	struct vicodec_dev *dev = ctx->dev;
	struct vb2_v4l2_buffer *src_buf, *dst_buf;
	struct vicodec_q_data *q_src;
	u32 state;

	src_buf = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);
	dst_buf = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);
	q_src = get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT);

	state = VB2_BUF_STATE_DONE;
	if (device_process(ctx, src_buf, dst_buf))
		state = VB2_BUF_STATE_ERROR;
	ctx->last_dst_buf = dst_buf;

	spin_lock(ctx->lock);
	if (!ctx->comp_has_next_frame && src_buf == ctx->last_src_buf) {
		dst_buf->flags |= V4L2_BUF_FLAG_LAST;
		v4l2_event_queue_fh(&ctx->fh, &eos_event);
	}
	if (ctx->is_enc) {
		src_buf->sequence = q_src->sequence++;
		src_buf = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
		v4l2_m2m_buf_done(src_buf, state);
	} else if (vb2_get_plane_payload(&src_buf->vb2_buf, 0) == ctx->cur_buf_offset) {
		src_buf->sequence = q_src->sequence++;
		src_buf = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
		v4l2_m2m_buf_done(src_buf, state);
		ctx->cur_buf_offset = 0;
		ctx->comp_has_next_frame = false;
	}
	v4l2_m2m_buf_done(dst_buf, state);
	ctx->comp_size = 0;
	ctx->header_size = 0;
	ctx->comp_magic_cnt = 0;
	ctx->comp_has_frame = false;
	spin_unlock(ctx->lock);

	if (ctx->is_enc)
		v4l2_m2m_job_finish(dev->enc_dev, ctx->fh.m2m_ctx);
	else
		v4l2_m2m_job_finish(dev->dec_dev, ctx->fh.m2m_ctx);
}

static void job_remove_src_buf(struct vicodec_ctx *ctx, u32 state)
{
	struct vb2_v4l2_buffer *src_buf;
	struct vicodec_q_data *q_src;

	q_src = get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT);
	spin_lock(ctx->lock);
	src_buf = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
	src_buf->sequence = q_src->sequence++;
	v4l2_m2m_buf_done(src_buf, state);
	ctx->cur_buf_offset = 0;
	spin_unlock(ctx->lock);
}

static const struct v4l2_fwht_pixfmt_info *
info_from_header(const struct fwht_cframe_hdr *p_hdr)
{
	unsigned int flags = ntohl(p_hdr->flags);
	unsigned int width_div = (flags & FWHT_FL_CHROMA_FULL_WIDTH) ? 1 : 2;
	unsigned int height_div = (flags & FWHT_FL_CHROMA_FULL_HEIGHT) ? 1 : 2;
	unsigned int components_num = 3;
	unsigned int pixenc = 0;
	unsigned int version = ntohl(p_hdr->version);

	if (version >= 2) {
		components_num = 1 + ((flags & FWHT_FL_COMPONENTS_NUM_MSK) >>
				FWHT_FL_COMPONENTS_NUM_OFFSET);
		pixenc = (flags & FWHT_FL_PIXENC_MSK);
	}
	return v4l2_fwht_default_fmt(width_div, height_div,
				     components_num, pixenc, 0);
}

static bool is_header_valid(const struct fwht_cframe_hdr *p_hdr)
{
	const struct v4l2_fwht_pixfmt_info *info;
	unsigned int w = ntohl(p_hdr->width);
	unsigned int h = ntohl(p_hdr->height);
	unsigned int version = ntohl(p_hdr->version);
	unsigned int flags = ntohl(p_hdr->flags);

	if (!version || version > FWHT_VERSION)
		return false;

	if (w < MIN_WIDTH || w > MAX_WIDTH || h < MIN_HEIGHT || h > MAX_HEIGHT)
		return false;

	if (version >= 2) {
		unsigned int components_num = 1 +
			((flags & FWHT_FL_COMPONENTS_NUM_MSK) >>
			FWHT_FL_COMPONENTS_NUM_OFFSET);
		unsigned int pixenc = flags & FWHT_FL_PIXENC_MSK;

		if (components_num == 0 || components_num > 4 || !pixenc)
			return false;
	}

	info = info_from_header(p_hdr);
	if (!info)
		return false;
	return true;
}

static void update_capture_data_from_header(struct vicodec_ctx *ctx)
{
	struct vicodec_q_data *q_dst = get_q_data(ctx,
						  V4L2_BUF_TYPE_VIDEO_CAPTURE);
	const struct fwht_cframe_hdr *p_hdr = &ctx->state.header;
	const struct v4l2_fwht_pixfmt_info *info = info_from_header(p_hdr);
	unsigned int flags = ntohl(p_hdr->flags);
	unsigned int hdr_width_div = (flags & FWHT_FL_CHROMA_FULL_WIDTH) ? 1 : 2;
	unsigned int hdr_height_div = (flags & FWHT_FL_CHROMA_FULL_HEIGHT) ? 1 : 2;

	q_dst->info = info;
	q_dst->visible_width = ntohl(p_hdr->width);
	q_dst->visible_height = ntohl(p_hdr->height);
	q_dst->coded_width = vic_round_dim(q_dst->visible_width, hdr_width_div);
	q_dst->coded_height = vic_round_dim(q_dst->visible_height,
					    hdr_height_div);

	q_dst->sizeimage = q_dst->coded_width * q_dst->coded_height *
		q_dst->info->sizeimage_mult / q_dst->info->sizeimage_div;
	ctx->state.colorspace = ntohl(p_hdr->colorspace);

	ctx->state.xfer_func = ntohl(p_hdr->xfer_func);
	ctx->state.ycbcr_enc = ntohl(p_hdr->ycbcr_enc);
	ctx->state.quantization = ntohl(p_hdr->quantization);
}

static void set_last_buffer(struct vb2_v4l2_buffer *dst_buf,
			    const struct vb2_v4l2_buffer *src_buf,
			    struct vicodec_ctx *ctx)
{
	struct vicodec_q_data *q_dst = get_q_data(ctx,
						  V4L2_BUF_TYPE_VIDEO_CAPTURE);

	vb2_set_plane_payload(&dst_buf->vb2_buf, 0, 0);
	dst_buf->sequence = q_dst->sequence++;

	v4l2_m2m_buf_copy_metadata(src_buf, dst_buf, !ctx->is_enc);
	dst_buf->flags |= V4L2_BUF_FLAG_LAST;
	v4l2_m2m_buf_done(dst_buf, VB2_BUF_STATE_DONE);
}

static int job_ready(void *priv)
{
	static const u8 magic[] = {
		0x4f, 0x4f, 0x4f, 0x4f, 0xff, 0xff, 0xff, 0xff
	};
	struct vicodec_ctx *ctx = priv;
	struct vb2_v4l2_buffer *src_buf;
	u8 *p_src;
	u8 *p;
	u32 sz;
	u32 state;
	struct vicodec_q_data *q_dst = get_q_data(ctx,
						  V4L2_BUF_TYPE_VIDEO_CAPTURE);
	unsigned int flags;
	unsigned int hdr_width_div;
	unsigned int hdr_height_div;
	unsigned int max_to_copy;
	unsigned int comp_frame_size;

	if (ctx->source_changed)
		return 0;
	if (ctx->is_enc || ctx->comp_has_frame)
		return 1;

restart:
	ctx->comp_has_next_frame = false;
	src_buf = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);
	if (!src_buf)
		return 0;
	p_src = vb2_plane_vaddr(&src_buf->vb2_buf, 0);
	sz = vb2_get_plane_payload(&src_buf->vb2_buf, 0);
	p = p_src + ctx->cur_buf_offset;

	state = VB2_BUF_STATE_DONE;

	if (ctx->header_size < sizeof(struct fwht_cframe_hdr)) {
		state = get_next_header(ctx, &p, p_src + sz - p);
		if (ctx->header_size < sizeof(struct fwht_cframe_hdr)) {
			job_remove_src_buf(ctx, state);
			goto restart;
		}
	}

	comp_frame_size = ntohl(ctx->state.header.size);

	/*
	 * The current scanned frame might be the first frame of a new
	 * resolution so its size might be larger than ctx->comp_max_size.
	 * In that case it is copied up to the current buffer capacity and
	 * the copy will continue after allocating new large enough buffer
	 * when restreaming
	 */
	max_to_copy = min(comp_frame_size, ctx->comp_max_size);

	if (ctx->comp_size < max_to_copy) {
		u32 copy = max_to_copy - ctx->comp_size;

		if (copy > p_src + sz - p)
			copy = p_src + sz - p;

		memcpy(ctx->state.compressed_frame + ctx->comp_size,
		       p, copy);
		p += copy;
		ctx->comp_size += copy;
		if (ctx->comp_size < max_to_copy) {
			job_remove_src_buf(ctx, state);
			goto restart;
		}
	}
	ctx->cur_buf_offset = p - p_src;
	if (ctx->comp_size == comp_frame_size)
		ctx->comp_has_frame = true;
	ctx->comp_has_next_frame = false;
	if (ctx->comp_has_frame && sz - ctx->cur_buf_offset >=
			sizeof(struct fwht_cframe_hdr)) {
		struct fwht_cframe_hdr *p_hdr = (struct fwht_cframe_hdr *)p;
		u32 frame_size = ntohl(p_hdr->size);
		u32 remaining = sz - ctx->cur_buf_offset - sizeof(*p_hdr);

		if (!memcmp(p, magic, sizeof(magic)))
			ctx->comp_has_next_frame = remaining >= frame_size;
	}
	/*
	 * if the header is invalid the device_run will just drop the frame
	 * with an error
	 */
	if (!is_header_valid(&ctx->state.header) && ctx->comp_has_frame)
		return 1;
	flags = ntohl(ctx->state.header.flags);
	hdr_width_div = (flags & FWHT_FL_CHROMA_FULL_WIDTH) ? 1 : 2;
	hdr_height_div = (flags & FWHT_FL_CHROMA_FULL_HEIGHT) ? 1 : 2;

	if (ntohl(ctx->state.header.width) != q_dst->visible_width ||
	    ntohl(ctx->state.header.height) != q_dst->visible_height ||
	    !q_dst->info ||
	    hdr_width_div != q_dst->info->width_div ||
	    hdr_height_div != q_dst->info->height_div) {
		static const struct v4l2_event rs_event = {
			.type = V4L2_EVENT_SOURCE_CHANGE,
			.u.src_change.changes = V4L2_EVENT_SRC_CH_RESOLUTION,
		};

		struct vb2_v4l2_buffer *dst_buf =
			v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);

		update_capture_data_from_header(ctx);
		ctx->first_source_change_sent = true;
		v4l2_event_queue_fh(&ctx->fh, &rs_event);
		set_last_buffer(dst_buf, src_buf, ctx);
		ctx->source_changed = true;
		return 0;
	}
	return 1;
}

/*
 * video ioctls
 */

static const struct v4l2_fwht_pixfmt_info *find_fmt(u32 fmt)
{
	const struct v4l2_fwht_pixfmt_info *info =
		v4l2_fwht_find_pixfmt(fmt);

	if (!info)
		info = v4l2_fwht_get_pixfmt(0);
	return info;
}

static int vidioc_querycap(struct file *file, void *priv,
			   struct v4l2_capability *cap)
{
	strncpy(cap->driver, VICODEC_NAME, sizeof(cap->driver) - 1);
	strncpy(cap->card, VICODEC_NAME, sizeof(cap->card) - 1);
	snprintf(cap->bus_info, sizeof(cap->bus_info),
			"platform:%s", VICODEC_NAME);
	return 0;
}

static int enum_fmt(struct v4l2_fmtdesc *f, struct vicodec_ctx *ctx,
		    bool is_out)
{
	bool is_uncomp = (ctx->is_enc && is_out) || (!ctx->is_enc && !is_out);

	if (V4L2_TYPE_IS_MULTIPLANAR(f->type) && !multiplanar)
		return -EINVAL;
	if (!V4L2_TYPE_IS_MULTIPLANAR(f->type) && multiplanar)
		return -EINVAL;

	if (is_uncomp) {
		const struct v4l2_fwht_pixfmt_info *info =
					get_q_data(ctx, f->type)->info;

		if (!info || ctx->is_enc)
			info = v4l2_fwht_get_pixfmt(f->index);
		else
			info = v4l2_fwht_default_fmt(info->width_div,
						     info->height_div,
						     info->components_num,
						     info->pixenc,
						     f->index);
		if (!info)
			return -EINVAL;
		f->pixelformat = info->id;
	} else {
		if (f->index)
			return -EINVAL;
		f->pixelformat = V4L2_PIX_FMT_FWHT;
	}
	return 0;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void *priv,
				   struct v4l2_fmtdesc *f)
{
	struct vicodec_ctx *ctx = file2ctx(file);

	return enum_fmt(f, ctx, false);
}

static int vidioc_enum_fmt_vid_out(struct file *file, void *priv,
				   struct v4l2_fmtdesc *f)
{
	struct vicodec_ctx *ctx = file2ctx(file);

	return enum_fmt(f, ctx, true);
}

static int vidioc_g_fmt(struct vicodec_ctx *ctx, struct v4l2_format *f)
{
	struct vb2_queue *vq;
	struct vicodec_q_data *q_data;
	struct v4l2_pix_format_mplane *pix_mp;
	struct v4l2_pix_format *pix;
	const struct v4l2_fwht_pixfmt_info *info;

	vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, f->type);
	if (!vq)
		return -EINVAL;

	q_data = get_q_data(ctx, f->type);
	info = q_data->info;

	if (!info)
		info = v4l2_fwht_get_pixfmt(0);

	switch (f->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
		if (multiplanar)
			return -EINVAL;
		pix = &f->fmt.pix;
		pix->width = q_data->coded_width;
		pix->height = q_data->coded_height;
		pix->field = V4L2_FIELD_NONE;
		pix->pixelformat = info->id;
		pix->bytesperline = q_data->coded_width *
					info->bytesperline_mult;
		pix->sizeimage = q_data->sizeimage;
		pix->colorspace = ctx->state.colorspace;
		pix->xfer_func = ctx->state.xfer_func;
		pix->ycbcr_enc = ctx->state.ycbcr_enc;
		pix->quantization = ctx->state.quantization;
		break;

	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		if (!multiplanar)
			return -EINVAL;
		pix_mp = &f->fmt.pix_mp;
		pix_mp->width = q_data->coded_width;
		pix_mp->height = q_data->coded_height;
		pix_mp->field = V4L2_FIELD_NONE;
		pix_mp->pixelformat = info->id;
		pix_mp->num_planes = 1;
		pix_mp->plane_fmt[0].bytesperline =
				q_data->coded_width * info->bytesperline_mult;
		pix_mp->plane_fmt[0].sizeimage = q_data->sizeimage;
		pix_mp->colorspace = ctx->state.colorspace;
		pix_mp->xfer_func = ctx->state.xfer_func;
		pix_mp->ycbcr_enc = ctx->state.ycbcr_enc;
		pix_mp->quantization = ctx->state.quantization;
		memset(pix_mp->reserved, 0, sizeof(pix_mp->reserved));
		memset(pix_mp->plane_fmt[0].reserved, 0,
		       sizeof(pix_mp->plane_fmt[0].reserved));
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int vidioc_g_fmt_vid_out(struct file *file, void *priv,
				struct v4l2_format *f)
{
	return vidioc_g_fmt(file2ctx(file), f);
}

static int vidioc_g_fmt_vid_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	return vidioc_g_fmt(file2ctx(file), f);
}

static int vidioc_try_fmt(struct vicodec_ctx *ctx, struct v4l2_format *f)
{
	struct v4l2_pix_format_mplane *pix_mp;
	struct v4l2_pix_format *pix;
	struct v4l2_plane_pix_format *plane;
	const struct v4l2_fwht_pixfmt_info *info = &pixfmt_fwht;

	switch (f->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
		pix = &f->fmt.pix;
		if (pix->pixelformat != V4L2_PIX_FMT_FWHT)
			info = find_fmt(pix->pixelformat);

		pix->width = clamp(pix->width, MIN_WIDTH, MAX_WIDTH);
		pix->width = vic_round_dim(pix->width, info->width_div);

		pix->height = clamp(pix->height, MIN_HEIGHT, MAX_HEIGHT);
		pix->height = vic_round_dim(pix->height, info->height_div);

		pix->field = V4L2_FIELD_NONE;
		pix->bytesperline =
			pix->width * info->bytesperline_mult;
		pix->sizeimage = pix->width * pix->height *
			info->sizeimage_mult / info->sizeimage_div;
		if (pix->pixelformat == V4L2_PIX_FMT_FWHT)
			pix->sizeimage += sizeof(struct fwht_cframe_hdr);
		break;
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		pix_mp = &f->fmt.pix_mp;
		plane = pix_mp->plane_fmt;
		if (pix_mp->pixelformat != V4L2_PIX_FMT_FWHT)
			info = find_fmt(pix_mp->pixelformat);
		pix_mp->num_planes = 1;

		pix_mp->width = clamp(pix_mp->width, MIN_WIDTH, MAX_WIDTH);
		pix_mp->width = vic_round_dim(pix_mp->width, info->width_div);

		pix_mp->height = clamp(pix_mp->height, MIN_HEIGHT, MAX_HEIGHT);
		pix_mp->height = vic_round_dim(pix_mp->height,
					       info->height_div);

		pix_mp->field = V4L2_FIELD_NONE;
		plane->bytesperline =
			pix_mp->width * info->bytesperline_mult;
		plane->sizeimage = pix_mp->width * pix_mp->height *
			info->sizeimage_mult / info->sizeimage_div;
		if (pix_mp->pixelformat == V4L2_PIX_FMT_FWHT)
			plane->sizeimage += sizeof(struct fwht_cframe_hdr);
		memset(pix_mp->reserved, 0, sizeof(pix_mp->reserved));
		memset(plane->reserved, 0, sizeof(plane->reserved));
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int vidioc_try_fmt_vid_cap(struct file *file, void *priv,
				  struct v4l2_format *f)
{
	struct vicodec_ctx *ctx = file2ctx(file);
	struct v4l2_pix_format_mplane *pix_mp;
	struct v4l2_pix_format *pix;

	switch (f->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		if (multiplanar)
			return -EINVAL;
		pix = &f->fmt.pix;
		pix->pixelformat = ctx->is_enc ? V4L2_PIX_FMT_FWHT :
				   find_fmt(f->fmt.pix.pixelformat)->id;
		pix->colorspace = ctx->state.colorspace;
		pix->xfer_func = ctx->state.xfer_func;
		pix->ycbcr_enc = ctx->state.ycbcr_enc;
		pix->quantization = ctx->state.quantization;
		break;
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		if (!multiplanar)
			return -EINVAL;
		pix_mp = &f->fmt.pix_mp;
		pix_mp->pixelformat = ctx->is_enc ? V4L2_PIX_FMT_FWHT :
				      find_fmt(pix_mp->pixelformat)->id;
		pix_mp->colorspace = ctx->state.colorspace;
		pix_mp->xfer_func = ctx->state.xfer_func;
		pix_mp->ycbcr_enc = ctx->state.ycbcr_enc;
		pix_mp->quantization = ctx->state.quantization;
		break;
	default:
		return -EINVAL;
	}

	return vidioc_try_fmt(ctx, f);
}

static int vidioc_try_fmt_vid_out(struct file *file, void *priv,
				  struct v4l2_format *f)
{
	struct vicodec_ctx *ctx = file2ctx(file);
	struct v4l2_pix_format_mplane *pix_mp;
	struct v4l2_pix_format *pix;

	switch (f->type) {
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
		if (multiplanar)
			return -EINVAL;
		pix = &f->fmt.pix;
		pix->pixelformat = !ctx->is_enc ? V4L2_PIX_FMT_FWHT :
				   find_fmt(pix->pixelformat)->id;
		if (!pix->colorspace)
			pix->colorspace = V4L2_COLORSPACE_REC709;
		break;
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		if (!multiplanar)
			return -EINVAL;
		pix_mp = &f->fmt.pix_mp;
		pix_mp->pixelformat = !ctx->is_enc ? V4L2_PIX_FMT_FWHT :
				      find_fmt(pix_mp->pixelformat)->id;
		if (!pix_mp->colorspace)
			pix_mp->colorspace = V4L2_COLORSPACE_REC709;
		break;
	default:
		return -EINVAL;
	}

	return vidioc_try_fmt(ctx, f);
}

static int vidioc_s_fmt(struct vicodec_ctx *ctx, struct v4l2_format *f)
{
	struct vicodec_q_data *q_data;
	struct vb2_queue *vq;
	bool fmt_changed = true;
	struct v4l2_pix_format_mplane *pix_mp;
	struct v4l2_pix_format *pix;

	vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, f->type);
	if (!vq)
		return -EINVAL;

	q_data = get_q_data(ctx, f->type);
	if (!q_data)
		return -EINVAL;

	switch (f->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
		pix = &f->fmt.pix;
		if (ctx->is_enc && V4L2_TYPE_IS_OUTPUT(f->type))
			fmt_changed =
				!q_data->info ||
				q_data->info->id != pix->pixelformat ||
				q_data->coded_width != pix->width ||
				q_data->coded_height != pix->height;

		if (vb2_is_busy(vq) && fmt_changed)
			return -EBUSY;

		if (pix->pixelformat == V4L2_PIX_FMT_FWHT)
			q_data->info = &pixfmt_fwht;
		else
			q_data->info = find_fmt(pix->pixelformat);
		q_data->coded_width = pix->width;
		q_data->coded_height = pix->height;
		q_data->sizeimage = pix->sizeimage;
		break;
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		pix_mp = &f->fmt.pix_mp;
		if (ctx->is_enc && V4L2_TYPE_IS_OUTPUT(f->type))
			fmt_changed =
				!q_data->info ||
				q_data->info->id != pix_mp->pixelformat ||
				q_data->coded_width != pix_mp->width ||
				q_data->coded_height != pix_mp->height;

		if (vb2_is_busy(vq) && fmt_changed)
			return -EBUSY;

		if (pix_mp->pixelformat == V4L2_PIX_FMT_FWHT)
			q_data->info = &pixfmt_fwht;
		else
			q_data->info = find_fmt(pix_mp->pixelformat);
		q_data->coded_width = pix_mp->width;
		q_data->coded_height = pix_mp->height;
		q_data->sizeimage = pix_mp->plane_fmt[0].sizeimage;
		break;
	default:
		return -EINVAL;
	}
	if (q_data->visible_width > q_data->coded_width)
		q_data->visible_width = q_data->coded_width;
	if (q_data->visible_height > q_data->coded_height)
		q_data->visible_height = q_data->coded_height;


	dprintk(ctx->dev,
		"Setting format for type %d, coded wxh: %dx%d, visible wxh: %dx%d, fourcc: %08x\n",
		f->type, q_data->coded_width, q_data->coded_height,
		q_data->visible_width, q_data->visible_height,
		q_data->info->id);

	return 0;
}

static int vidioc_s_fmt_vid_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	int ret;

	ret = vidioc_try_fmt_vid_cap(file, priv, f);
	if (ret)
		return ret;

	return vidioc_s_fmt(file2ctx(file), f);
}

static int vidioc_s_fmt_vid_out(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct vicodec_ctx *ctx = file2ctx(file);
	struct v4l2_pix_format_mplane *pix_mp;
	struct v4l2_pix_format *pix;
	int ret;

	ret = vidioc_try_fmt_vid_out(file, priv, f);
	if (ret)
		return ret;

	ret = vidioc_s_fmt(file2ctx(file), f);
	if (!ret) {
		switch (f->type) {
		case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		case V4L2_BUF_TYPE_VIDEO_OUTPUT:
			pix = &f->fmt.pix;
			ctx->state.colorspace = pix->colorspace;
			ctx->state.xfer_func = pix->xfer_func;
			ctx->state.ycbcr_enc = pix->ycbcr_enc;
			ctx->state.quantization = pix->quantization;
			break;
		case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
			pix_mp = &f->fmt.pix_mp;
			ctx->state.colorspace = pix_mp->colorspace;
			ctx->state.xfer_func = pix_mp->xfer_func;
			ctx->state.ycbcr_enc = pix_mp->ycbcr_enc;
			ctx->state.quantization = pix_mp->quantization;
			break;
		default:
			break;
		}
	}
	return ret;
}

static int vidioc_g_selection(struct file *file, void *priv,
			      struct v4l2_selection *s)
{
	struct vicodec_ctx *ctx = file2ctx(file);
	struct vicodec_q_data *q_data;
	enum v4l2_buf_type valid_cap_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	enum v4l2_buf_type valid_out_type = V4L2_BUF_TYPE_VIDEO_OUTPUT;

	if (multiplanar) {
		valid_cap_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		valid_out_type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	}

	if (s->type != valid_cap_type && s->type != valid_out_type)
		return -EINVAL;

	q_data = get_q_data(ctx, s->type);
	if (!q_data)
		return -EINVAL;
	/*
	 * encoder supports only cropping on the OUTPUT buffer
	 * decoder supports only composing on the CAPTURE buffer
	 */
	if ((ctx->is_enc && s->type == valid_out_type) ||
	    (!ctx->is_enc && s->type == valid_cap_type)) {
		switch (s->target) {
		case V4L2_SEL_TGT_COMPOSE:
		case V4L2_SEL_TGT_CROP:
			s->r.left = 0;
			s->r.top = 0;
			s->r.width = q_data->visible_width;
			s->r.height = q_data->visible_height;
			return 0;
		case V4L2_SEL_TGT_COMPOSE_DEFAULT:
		case V4L2_SEL_TGT_COMPOSE_BOUNDS:
		case V4L2_SEL_TGT_CROP_DEFAULT:
		case V4L2_SEL_TGT_CROP_BOUNDS:
			s->r.left = 0;
			s->r.top = 0;
			s->r.width = q_data->coded_width;
			s->r.height = q_data->coded_height;
			return 0;
		}
	}
	return -EINVAL;
}

static int vidioc_s_selection(struct file *file, void *priv,
			      struct v4l2_selection *s)
{
	struct vicodec_ctx *ctx = file2ctx(file);
	struct vicodec_q_data *q_data;
	enum v4l2_buf_type out_type = V4L2_BUF_TYPE_VIDEO_OUTPUT;

	if (multiplanar)
		out_type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;

	if (s->type != out_type)
		return -EINVAL;

	q_data = get_q_data(ctx, s->type);
	if (!q_data)
		return -EINVAL;

	if (!ctx->is_enc || s->target != V4L2_SEL_TGT_CROP)
		return -EINVAL;

	s->r.left = 0;
	s->r.top = 0;
	q_data->visible_width = clamp(s->r.width, MIN_WIDTH,
				      q_data->coded_width);
	s->r.width = q_data->visible_width;
	q_data->visible_height = clamp(s->r.height, MIN_HEIGHT,
				       q_data->coded_height);
	s->r.height = q_data->visible_height;
	return 0;
}

static void vicodec_mark_last_buf(struct vicodec_ctx *ctx)
{
	static const struct v4l2_event eos_event = {
		.type = V4L2_EVENT_EOS
	};

	spin_lock(ctx->lock);
	ctx->last_src_buf = v4l2_m2m_last_src_buf(ctx->fh.m2m_ctx);
	if (!ctx->last_src_buf && ctx->last_dst_buf) {
		ctx->last_dst_buf->flags |= V4L2_BUF_FLAG_LAST;
		v4l2_event_queue_fh(&ctx->fh, &eos_event);
	}
	spin_unlock(ctx->lock);
}

static int vicodec_try_encoder_cmd(struct file *file, void *fh,
				struct v4l2_encoder_cmd *ec)
{
	if (ec->cmd != V4L2_ENC_CMD_STOP)
		return -EINVAL;

	if (ec->flags & V4L2_ENC_CMD_STOP_AT_GOP_END)
		return -EINVAL;

	return 0;
}

static int vicodec_encoder_cmd(struct file *file, void *fh,
			    struct v4l2_encoder_cmd *ec)
{
	struct vicodec_ctx *ctx = file2ctx(file);
	int ret;

	ret = vicodec_try_encoder_cmd(file, fh, ec);
	if (ret < 0)
		return ret;

	vicodec_mark_last_buf(ctx);
	return 0;
}

static int vicodec_try_decoder_cmd(struct file *file, void *fh,
				struct v4l2_decoder_cmd *dc)
{
	if (dc->cmd != V4L2_DEC_CMD_STOP)
		return -EINVAL;

	if (dc->flags & V4L2_DEC_CMD_STOP_TO_BLACK)
		return -EINVAL;

	if (!(dc->flags & V4L2_DEC_CMD_STOP_IMMEDIATELY) && (dc->stop.pts != 0))
		return -EINVAL;

	return 0;
}

static int vicodec_decoder_cmd(struct file *file, void *fh,
			    struct v4l2_decoder_cmd *dc)
{
	struct vicodec_ctx *ctx = file2ctx(file);
	int ret;

	ret = vicodec_try_decoder_cmd(file, fh, dc);
	if (ret < 0)
		return ret;

	vicodec_mark_last_buf(ctx);
	return 0;
}

static int vicodec_enum_framesizes(struct file *file, void *fh,
				   struct v4l2_frmsizeenum *fsize)
{
	switch (fsize->pixel_format) {
	case V4L2_PIX_FMT_FWHT:
		break;
	default:
		if (find_fmt(fsize->pixel_format)->id == fsize->pixel_format)
			break;
		return -EINVAL;
	}

	if (fsize->index)
		return -EINVAL;

	fsize->type = V4L2_FRMSIZE_TYPE_STEPWISE;

	fsize->stepwise.min_width = MIN_WIDTH;
	fsize->stepwise.max_width = MAX_WIDTH;
	fsize->stepwise.step_width = 8;
	fsize->stepwise.min_height = MIN_HEIGHT;
	fsize->stepwise.max_height = MAX_HEIGHT;
	fsize->stepwise.step_height = 8;

	return 0;
}

static int vicodec_subscribe_event(struct v4l2_fh *fh,
				const struct v4l2_event_subscription *sub)
{
	struct vicodec_ctx *ctx = container_of(fh, struct vicodec_ctx, fh);

	switch (sub->type) {
	case V4L2_EVENT_SOURCE_CHANGE:
		if (ctx->is_enc)
			return -EINVAL;
		/* fall through */
	case V4L2_EVENT_EOS:
		return v4l2_event_subscribe(fh, sub, 0, NULL);
	default:
		return v4l2_ctrl_subscribe_event(fh, sub);
	}
}

static const struct v4l2_ioctl_ops vicodec_ioctl_ops = {
	.vidioc_querycap	= vidioc_querycap,

	.vidioc_enum_fmt_vid_cap = vidioc_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap	= vidioc_g_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap	= vidioc_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap	= vidioc_s_fmt_vid_cap,

	.vidioc_enum_fmt_vid_cap_mplane = vidioc_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap_mplane	= vidioc_g_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap_mplane	= vidioc_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap_mplane	= vidioc_s_fmt_vid_cap,

	.vidioc_enum_fmt_vid_out = vidioc_enum_fmt_vid_out,
	.vidioc_g_fmt_vid_out	= vidioc_g_fmt_vid_out,
	.vidioc_try_fmt_vid_out	= vidioc_try_fmt_vid_out,
	.vidioc_s_fmt_vid_out	= vidioc_s_fmt_vid_out,

	.vidioc_enum_fmt_vid_out_mplane = vidioc_enum_fmt_vid_out,
	.vidioc_g_fmt_vid_out_mplane	= vidioc_g_fmt_vid_out,
	.vidioc_try_fmt_vid_out_mplane	= vidioc_try_fmt_vid_out,
	.vidioc_s_fmt_vid_out_mplane	= vidioc_s_fmt_vid_out,

	.vidioc_reqbufs		= v4l2_m2m_ioctl_reqbufs,
	.vidioc_querybuf	= v4l2_m2m_ioctl_querybuf,
	.vidioc_qbuf		= v4l2_m2m_ioctl_qbuf,
	.vidioc_dqbuf		= v4l2_m2m_ioctl_dqbuf,
	.vidioc_prepare_buf	= v4l2_m2m_ioctl_prepare_buf,
	.vidioc_create_bufs	= v4l2_m2m_ioctl_create_bufs,
	.vidioc_expbuf		= v4l2_m2m_ioctl_expbuf,

	.vidioc_streamon	= v4l2_m2m_ioctl_streamon,
	.vidioc_streamoff	= v4l2_m2m_ioctl_streamoff,

	.vidioc_g_selection	= vidioc_g_selection,
	.vidioc_s_selection	= vidioc_s_selection,

	.vidioc_try_encoder_cmd	= vicodec_try_encoder_cmd,
	.vidioc_encoder_cmd	= vicodec_encoder_cmd,
	.vidioc_try_decoder_cmd	= vicodec_try_decoder_cmd,
	.vidioc_decoder_cmd	= vicodec_decoder_cmd,
	.vidioc_enum_framesizes = vicodec_enum_framesizes,

	.vidioc_subscribe_event = vicodec_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};


/*
 * Queue operations
 */

static int vicodec_queue_setup(struct vb2_queue *vq, unsigned int *nbuffers,
			       unsigned int *nplanes, unsigned int sizes[],
			       struct device *alloc_devs[])
{
	struct vicodec_ctx *ctx = vb2_get_drv_priv(vq);
	struct vicodec_q_data *q_data = get_q_data(ctx, vq->type);
	unsigned int size = q_data->sizeimage;

	if (*nplanes)
		return sizes[0] < size ? -EINVAL : 0;

	*nplanes = 1;
	sizes[0] = size;
	return 0;
}

static int vicodec_buf_prepare(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct vicodec_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct vicodec_q_data *q_data;

	dprintk(ctx->dev, "type: %d\n", vb->vb2_queue->type);

	q_data = get_q_data(ctx, vb->vb2_queue->type);
	if (V4L2_TYPE_IS_OUTPUT(vb->vb2_queue->type)) {
		if (vbuf->field == V4L2_FIELD_ANY)
			vbuf->field = V4L2_FIELD_NONE;
		if (vbuf->field != V4L2_FIELD_NONE) {
			dprintk(ctx->dev, "%s field isn't supported\n",
					__func__);
			return -EINVAL;
		}
	}

	if (vb2_plane_size(vb, 0) < q_data->sizeimage) {
		dprintk(ctx->dev,
			"%s data will not fit into plane (%lu < %lu)\n",
			__func__, vb2_plane_size(vb, 0),
			(long)q_data->sizeimage);
		return -EINVAL;
	}

	return 0;
}

static void vicodec_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct vicodec_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	unsigned int sz = vb2_get_plane_payload(&vbuf->vb2_buf, 0);
	u8 *p_src = vb2_plane_vaddr(&vbuf->vb2_buf, 0);
	u8 *p = p_src;
	struct vb2_queue *vq_out = v4l2_m2m_get_vq(ctx->fh.m2m_ctx,
						   V4L2_BUF_TYPE_VIDEO_OUTPUT);
	struct vb2_queue *vq_cap = v4l2_m2m_get_vq(ctx->fh.m2m_ctx,
						   V4L2_BUF_TYPE_VIDEO_CAPTURE);
	bool header_valid = false;
	static const struct v4l2_event rs_event = {
		.type = V4L2_EVENT_SOURCE_CHANGE,
		.u.src_change.changes = V4L2_EVENT_SRC_CH_RESOLUTION,
	};

	/* buf_queue handles only the first source change event */
	if (ctx->first_source_change_sent) {
		v4l2_m2m_buf_queue(ctx->fh.m2m_ctx, vbuf);
		return;
	}

	/*
	 * if both queues are streaming, the source change event is
	 * handled in job_ready
	 */
	if (vb2_is_streaming(vq_cap) && vb2_is_streaming(vq_out)) {
		v4l2_m2m_buf_queue(ctx->fh.m2m_ctx, vbuf);
		return;
	}

	/*
	 * source change event is relevant only for the decoder
	 * in the compressed stream
	 */
	if (ctx->is_enc || !V4L2_TYPE_IS_OUTPUT(vb->vb2_queue->type)) {
		v4l2_m2m_buf_queue(ctx->fh.m2m_ctx, vbuf);
		return;
	}

	do {
		enum vb2_buffer_state state =
			get_next_header(ctx, &p, p_src + sz - p);

		if (ctx->header_size < sizeof(struct fwht_cframe_hdr)) {
			v4l2_m2m_buf_done(vbuf, state);
			return;
		}
		header_valid = is_header_valid(&ctx->state.header);
		/*
		 * p points right after the end of the header in the
		 * buffer. If the header is invalid we set p to point
		 * to the next byte after the start of the header
		 */
		if (!header_valid) {
			p = p - sizeof(struct fwht_cframe_hdr) + 1;
			if (p < p_src)
				p = p_src;
			ctx->header_size = 0;
			ctx->comp_magic_cnt = 0;
		}

	} while (!header_valid);

	ctx->cur_buf_offset = p - p_src;
	update_capture_data_from_header(ctx);
	ctx->first_source_change_sent = true;
	v4l2_event_queue_fh(&ctx->fh, &rs_event);
	v4l2_m2m_buf_queue(ctx->fh.m2m_ctx, vbuf);
}

static void vicodec_return_bufs(struct vb2_queue *q, u32 state)
{
	struct vicodec_ctx *ctx = vb2_get_drv_priv(q);
	struct vb2_v4l2_buffer *vbuf;

	for (;;) {
		if (V4L2_TYPE_IS_OUTPUT(q->type))
			vbuf = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
		else
			vbuf = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);
		if (vbuf == NULL)
			return;
		spin_lock(ctx->lock);
		v4l2_m2m_buf_done(vbuf, state);
		spin_unlock(ctx->lock);
	}
}

static int vicodec_start_streaming(struct vb2_queue *q,
				   unsigned int count)
{
	struct vicodec_ctx *ctx = vb2_get_drv_priv(q);
	struct vicodec_q_data *q_data = get_q_data(ctx, q->type);
	struct v4l2_fwht_state *state = &ctx->state;
	const struct v4l2_fwht_pixfmt_info *info = q_data->info;
	unsigned int size = q_data->coded_width * q_data->coded_height;
	unsigned int chroma_div;
	unsigned int total_planes_size;
	u8 *new_comp_frame;

	if (!info)
		return -EINVAL;

	chroma_div = info->width_div * info->height_div;
	q_data->sequence = 0;

	ctx->last_src_buf = NULL;
	ctx->last_dst_buf = NULL;
	state->gop_cnt = 0;

	if ((V4L2_TYPE_IS_OUTPUT(q->type) && !ctx->is_enc) ||
	    (!V4L2_TYPE_IS_OUTPUT(q->type) && ctx->is_enc))
		return 0;

	if (info->id == V4L2_PIX_FMT_FWHT) {
		vicodec_return_bufs(q, VB2_BUF_STATE_QUEUED);
		return -EINVAL;
	}
	if (info->components_num == 4)
		total_planes_size = 2 * size + 2 * (size / chroma_div);
	else if (info->components_num == 3)
		total_planes_size = size + 2 * (size / chroma_div);
	else
		total_planes_size = size;

	state->visible_width = q_data->visible_width;
	state->visible_height = q_data->visible_height;
	state->coded_width = q_data->coded_width;
	state->coded_height = q_data->coded_height;
	state->stride = q_data->coded_width *
				info->bytesperline_mult;

	state->ref_frame.luma = kvmalloc(total_planes_size, GFP_KERNEL);
	ctx->comp_max_size = total_planes_size;
	new_comp_frame = kvmalloc(ctx->comp_max_size, GFP_KERNEL);

	if (!state->ref_frame.luma || !new_comp_frame) {
		kvfree(state->ref_frame.luma);
		kvfree(new_comp_frame);
		vicodec_return_bufs(q, VB2_BUF_STATE_QUEUED);
		return -ENOMEM;
	}
	/*
	 * if state->compressed_frame was already allocated then
	 * it contain data of the first frame of the new resolution
	 */
	if (state->compressed_frame) {
		if (ctx->comp_size > ctx->comp_max_size)
			ctx->comp_size = ctx->comp_max_size;

		memcpy(new_comp_frame,
		       state->compressed_frame, ctx->comp_size);
	}

	kvfree(state->compressed_frame);
	state->compressed_frame = new_comp_frame;

	if (info->components_num >= 3) {
		state->ref_frame.cb = state->ref_frame.luma + size;
		state->ref_frame.cr = state->ref_frame.cb + size / chroma_div;
	} else {
		state->ref_frame.cb = NULL;
		state->ref_frame.cr = NULL;
	}

	if (info->components_num == 4)
		state->ref_frame.alpha =
			state->ref_frame.cr + size / chroma_div;
	else
		state->ref_frame.alpha = NULL;
	return 0;
}

static void vicodec_stop_streaming(struct vb2_queue *q)
{
	struct vicodec_ctx *ctx = vb2_get_drv_priv(q);

	vicodec_return_bufs(q, VB2_BUF_STATE_ERROR);

	if ((!V4L2_TYPE_IS_OUTPUT(q->type) && !ctx->is_enc) ||
	    (V4L2_TYPE_IS_OUTPUT(q->type) && ctx->is_enc)) {
		kvfree(ctx->state.ref_frame.luma);
		ctx->comp_max_size = 0;
		ctx->source_changed = false;
	}
	if (V4L2_TYPE_IS_OUTPUT(q->type) && !ctx->is_enc) {
		ctx->cur_buf_offset = 0;
		ctx->comp_size = 0;
		ctx->header_size = 0;
		ctx->comp_magic_cnt = 0;
		ctx->comp_has_frame = 0;
		ctx->comp_has_next_frame = 0;
	}
}

static const struct vb2_ops vicodec_qops = {
	.queue_setup	 = vicodec_queue_setup,
	.buf_prepare	 = vicodec_buf_prepare,
	.buf_queue	 = vicodec_buf_queue,
	.start_streaming = vicodec_start_streaming,
	.stop_streaming  = vicodec_stop_streaming,
	.wait_prepare	 = vb2_ops_wait_prepare,
	.wait_finish	 = vb2_ops_wait_finish,
};

static int queue_init(void *priv, struct vb2_queue *src_vq,
		      struct vb2_queue *dst_vq)
{
	struct vicodec_ctx *ctx = priv;
	int ret;

	src_vq->type = (multiplanar ?
			V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE :
			V4L2_BUF_TYPE_VIDEO_OUTPUT);
	src_vq->io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF;
	src_vq->drv_priv = ctx;
	src_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	src_vq->ops = &vicodec_qops;
	src_vq->mem_ops = &vb2_vmalloc_memops;
	src_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	src_vq->lock = ctx->is_enc ? &ctx->dev->enc_mutex :
		&ctx->dev->dec_mutex;

	ret = vb2_queue_init(src_vq);
	if (ret)
		return ret;

	dst_vq->type = (multiplanar ?
			V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE :
			V4L2_BUF_TYPE_VIDEO_CAPTURE);
	dst_vq->io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF;
	dst_vq->drv_priv = ctx;
	dst_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	dst_vq->ops = &vicodec_qops;
	dst_vq->mem_ops = &vb2_vmalloc_memops;
	dst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	dst_vq->lock = src_vq->lock;

	return vb2_queue_init(dst_vq);
}

#define VICODEC_CID_CUSTOM_BASE		(V4L2_CID_MPEG_BASE | 0xf000)
#define VICODEC_CID_I_FRAME_QP		(VICODEC_CID_CUSTOM_BASE + 0)
#define VICODEC_CID_P_FRAME_QP		(VICODEC_CID_CUSTOM_BASE + 1)

static int vicodec_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct vicodec_ctx *ctx = container_of(ctrl->handler,
					       struct vicodec_ctx, hdl);

	switch (ctrl->id) {
	case V4L2_CID_MPEG_VIDEO_GOP_SIZE:
		ctx->state.gop_size = ctrl->val;
		return 0;
	case VICODEC_CID_I_FRAME_QP:
		ctx->state.i_frame_qp = ctrl->val;
		return 0;
	case VICODEC_CID_P_FRAME_QP:
		ctx->state.p_frame_qp = ctrl->val;
		return 0;
	}
	return -EINVAL;
}

static const struct v4l2_ctrl_ops vicodec_ctrl_ops = {
	.s_ctrl = vicodec_s_ctrl,
};

static const struct v4l2_ctrl_config vicodec_ctrl_i_frame = {
	.ops = &vicodec_ctrl_ops,
	.id = VICODEC_CID_I_FRAME_QP,
	.name = "FWHT I-Frame QP Value",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = 1,
	.max = 31,
	.def = 20,
	.step = 1,
};

static const struct v4l2_ctrl_config vicodec_ctrl_p_frame = {
	.ops = &vicodec_ctrl_ops,
	.id = VICODEC_CID_P_FRAME_QP,
	.name = "FWHT P-Frame QP Value",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = 1,
	.max = 31,
	.def = 20,
	.step = 1,
};

/*
 * File operations
 */
static int vicodec_open(struct file *file)
{
	struct video_device *vfd = video_devdata(file);
	struct vicodec_dev *dev = video_drvdata(file);
	struct vicodec_ctx *ctx = NULL;
	struct v4l2_ctrl_handler *hdl;
	unsigned int size;
	int rc = 0;

	if (mutex_lock_interruptible(vfd->lock))
		return -ERESTARTSYS;
	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx) {
		rc = -ENOMEM;
		goto open_unlock;
	}

	if (vfd == &dev->enc_vfd)
		ctx->is_enc = true;

	v4l2_fh_init(&ctx->fh, video_devdata(file));
	file->private_data = &ctx->fh;
	ctx->dev = dev;
	hdl = &ctx->hdl;
	v4l2_ctrl_handler_init(hdl, 4);
	v4l2_ctrl_new_std(hdl, &vicodec_ctrl_ops, V4L2_CID_MPEG_VIDEO_GOP_SIZE,
			  1, 16, 1, 10);
	v4l2_ctrl_new_custom(hdl, &vicodec_ctrl_i_frame, NULL);
	v4l2_ctrl_new_custom(hdl, &vicodec_ctrl_p_frame, NULL);
	if (hdl->error) {
		rc = hdl->error;
		v4l2_ctrl_handler_free(hdl);
		kfree(ctx);
		goto open_unlock;
	}
	ctx->fh.ctrl_handler = hdl;
	v4l2_ctrl_handler_setup(hdl);

	ctx->q_data[V4L2_M2M_SRC].info =
		ctx->is_enc ? v4l2_fwht_get_pixfmt(0) : &pixfmt_fwht;
	ctx->q_data[V4L2_M2M_SRC].coded_width = 1280;
	ctx->q_data[V4L2_M2M_SRC].coded_height = 720;
	ctx->q_data[V4L2_M2M_SRC].visible_width = 1280;
	ctx->q_data[V4L2_M2M_SRC].visible_height = 720;
	size = 1280 * 720 * ctx->q_data[V4L2_M2M_SRC].info->sizeimage_mult /
		ctx->q_data[V4L2_M2M_SRC].info->sizeimage_div;
	if (ctx->is_enc)
		ctx->q_data[V4L2_M2M_SRC].sizeimage = size;
	else
		ctx->q_data[V4L2_M2M_SRC].sizeimage =
			size + sizeof(struct fwht_cframe_hdr);
	if (ctx->is_enc) {
		ctx->q_data[V4L2_M2M_DST] = ctx->q_data[V4L2_M2M_SRC];
		ctx->q_data[V4L2_M2M_DST].info = &pixfmt_fwht;
		ctx->q_data[V4L2_M2M_DST].sizeimage = 1280 * 720 *
			ctx->q_data[V4L2_M2M_DST].info->sizeimage_mult /
			ctx->q_data[V4L2_M2M_DST].info->sizeimage_div +
			sizeof(struct fwht_cframe_hdr);
	} else {
		ctx->q_data[V4L2_M2M_DST].info = NULL;
	}

	ctx->state.colorspace = V4L2_COLORSPACE_REC709;

	if (ctx->is_enc) {
		ctx->fh.m2m_ctx = v4l2_m2m_ctx_init(dev->enc_dev, ctx,
						    &queue_init);
		ctx->lock = &dev->enc_lock;
	} else {
		ctx->fh.m2m_ctx = v4l2_m2m_ctx_init(dev->dec_dev, ctx,
						    &queue_init);
		ctx->lock = &dev->dec_lock;
	}

	if (IS_ERR(ctx->fh.m2m_ctx)) {
		rc = PTR_ERR(ctx->fh.m2m_ctx);

		v4l2_ctrl_handler_free(hdl);
		v4l2_fh_exit(&ctx->fh);
		kfree(ctx);
		goto open_unlock;
	}

	v4l2_fh_add(&ctx->fh);

open_unlock:
	mutex_unlock(vfd->lock);
	return rc;
}

static int vicodec_release(struct file *file)
{
	struct video_device *vfd = video_devdata(file);
	struct vicodec_ctx *ctx = file2ctx(file);

	v4l2_fh_del(&ctx->fh);
	v4l2_fh_exit(&ctx->fh);
	v4l2_ctrl_handler_free(&ctx->hdl);
	mutex_lock(vfd->lock);
	v4l2_m2m_ctx_release(ctx->fh.m2m_ctx);
	mutex_unlock(vfd->lock);
	kfree(ctx);

	return 0;
}

static const struct v4l2_file_operations vicodec_fops = {
	.owner		= THIS_MODULE,
	.open		= vicodec_open,
	.release	= vicodec_release,
	.poll		= v4l2_m2m_fop_poll,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= v4l2_m2m_fop_mmap,
};

static const struct video_device vicodec_videodev = {
	.name		= VICODEC_NAME,
	.vfl_dir	= VFL_DIR_M2M,
	.fops		= &vicodec_fops,
	.ioctl_ops	= &vicodec_ioctl_ops,
	.minor		= -1,
	.release	= video_device_release_empty,
};

static const struct v4l2_m2m_ops m2m_ops = {
	.device_run	= device_run,
	.job_ready	= job_ready,
};

static int vicodec_probe(struct platform_device *pdev)
{
	struct vicodec_dev *dev;
	struct video_device *vfd;
	int ret;

	dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	spin_lock_init(&dev->enc_lock);
	spin_lock_init(&dev->dec_lock);

	ret = v4l2_device_register(&pdev->dev, &dev->v4l2_dev);
	if (ret)
		return ret;

#ifdef CONFIG_MEDIA_CONTROLLER
	dev->mdev.dev = &pdev->dev;
	strscpy(dev->mdev.model, "vicodec", sizeof(dev->mdev.model));
	strscpy(dev->mdev.bus_info, "platform:vicodec",
		sizeof(dev->mdev.bus_info));
	media_device_init(&dev->mdev);
	dev->v4l2_dev.mdev = &dev->mdev;
#endif

	mutex_init(&dev->enc_mutex);
	mutex_init(&dev->dec_mutex);

	platform_set_drvdata(pdev, dev);

	dev->enc_dev = v4l2_m2m_init(&m2m_ops);
	if (IS_ERR(dev->enc_dev)) {
		v4l2_err(&dev->v4l2_dev, "Failed to init vicodec device\n");
		ret = PTR_ERR(dev->enc_dev);
		goto unreg_dev;
	}

	dev->dec_dev = v4l2_m2m_init(&m2m_ops);
	if (IS_ERR(dev->dec_dev)) {
		v4l2_err(&dev->v4l2_dev, "Failed to init vicodec device\n");
		ret = PTR_ERR(dev->dec_dev);
		goto err_enc_m2m;
	}

	dev->enc_vfd = vicodec_videodev;
	vfd = &dev->enc_vfd;
	vfd->lock = &dev->enc_mutex;
	vfd->v4l2_dev = &dev->v4l2_dev;
	strscpy(vfd->name, "vicodec-enc", sizeof(vfd->name));
	vfd->device_caps = V4L2_CAP_STREAMING |
		(multiplanar ? V4L2_CAP_VIDEO_M2M_MPLANE : V4L2_CAP_VIDEO_M2M);
	v4l2_disable_ioctl(vfd, VIDIOC_DECODER_CMD);
	v4l2_disable_ioctl(vfd, VIDIOC_TRY_DECODER_CMD);
	video_set_drvdata(vfd, dev);

	ret = video_register_device(vfd, VFL_TYPE_GRABBER, 0);
	if (ret) {
		v4l2_err(&dev->v4l2_dev, "Failed to register video device\n");
		goto err_dec_m2m;
	}
	v4l2_info(&dev->v4l2_dev,
			"Device registered as /dev/video%d\n", vfd->num);

	dev->dec_vfd = vicodec_videodev;
	vfd = &dev->dec_vfd;
	vfd->lock = &dev->dec_mutex;
	vfd->v4l2_dev = &dev->v4l2_dev;
	vfd->device_caps = V4L2_CAP_STREAMING |
		(multiplanar ? V4L2_CAP_VIDEO_M2M_MPLANE : V4L2_CAP_VIDEO_M2M);
	strscpy(vfd->name, "vicodec-dec", sizeof(vfd->name));
	v4l2_disable_ioctl(vfd, VIDIOC_ENCODER_CMD);
	v4l2_disable_ioctl(vfd, VIDIOC_TRY_ENCODER_CMD);
	video_set_drvdata(vfd, dev);

	ret = video_register_device(vfd, VFL_TYPE_GRABBER, 0);
	if (ret) {
		v4l2_err(&dev->v4l2_dev, "Failed to register video device\n");
		goto unreg_enc;
	}
	v4l2_info(&dev->v4l2_dev,
			"Device registered as /dev/video%d\n", vfd->num);

#ifdef CONFIG_MEDIA_CONTROLLER
	ret = v4l2_m2m_register_media_controller(dev->enc_dev,
			&dev->enc_vfd, MEDIA_ENT_F_PROC_VIDEO_ENCODER);
	if (ret) {
		v4l2_err(&dev->v4l2_dev, "Failed to init mem2mem media controller\n");
		goto unreg_m2m;
	}

	ret = v4l2_m2m_register_media_controller(dev->dec_dev,
			&dev->dec_vfd, MEDIA_ENT_F_PROC_VIDEO_DECODER);
	if (ret) {
		v4l2_err(&dev->v4l2_dev, "Failed to init mem2mem media controller\n");
		goto unreg_m2m_enc_mc;
	}

	ret = media_device_register(&dev->mdev);
	if (ret) {
		v4l2_err(&dev->v4l2_dev, "Failed to register mem2mem media device\n");
		goto unreg_m2m_dec_mc;
	}
#endif
	return 0;

#ifdef CONFIG_MEDIA_CONTROLLER
unreg_m2m_dec_mc:
	v4l2_m2m_unregister_media_controller(dev->dec_dev);
unreg_m2m_enc_mc:
	v4l2_m2m_unregister_media_controller(dev->enc_dev);
unreg_m2m:
	video_unregister_device(&dev->dec_vfd);
#endif
unreg_enc:
	video_unregister_device(&dev->enc_vfd);
err_dec_m2m:
	v4l2_m2m_release(dev->dec_dev);
err_enc_m2m:
	v4l2_m2m_release(dev->enc_dev);
unreg_dev:
	v4l2_device_unregister(&dev->v4l2_dev);

	return ret;
}

static int vicodec_remove(struct platform_device *pdev)
{
	struct vicodec_dev *dev = platform_get_drvdata(pdev);

	v4l2_info(&dev->v4l2_dev, "Removing " VICODEC_NAME);

#ifdef CONFIG_MEDIA_CONTROLLER
	media_device_unregister(&dev->mdev);
	v4l2_m2m_unregister_media_controller(dev->enc_dev);
	v4l2_m2m_unregister_media_controller(dev->dec_dev);
	media_device_cleanup(&dev->mdev);
#endif

	v4l2_m2m_release(dev->enc_dev);
	v4l2_m2m_release(dev->dec_dev);
	video_unregister_device(&dev->enc_vfd);
	video_unregister_device(&dev->dec_vfd);
	v4l2_device_unregister(&dev->v4l2_dev);

	return 0;
}

static struct platform_driver vicodec_pdrv = {
	.probe		= vicodec_probe,
	.remove		= vicodec_remove,
	.driver		= {
		.name	= VICODEC_NAME,
	},
};

static void __exit vicodec_exit(void)
{
	platform_driver_unregister(&vicodec_pdrv);
	platform_device_unregister(&vicodec_pdev);
}

static int __init vicodec_init(void)
{
	int ret;

	ret = platform_device_register(&vicodec_pdev);
	if (ret)
		return ret;

	ret = platform_driver_register(&vicodec_pdrv);
	if (ret)
		platform_device_unregister(&vicodec_pdev);

	return ret;
}

module_init(vicodec_init);
module_exit(vicodec_exit);
