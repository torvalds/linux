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

#include "vicodec-codec.h"

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


static void vicodec_dev_release(struct device *dev)
{
}

static struct platform_device vicodec_pdev = {
	.name		= VICODEC_NAME,
	.dev.release	= vicodec_dev_release,
};

/* Per-queue, driver-specific private data */
struct vicodec_q_data {
	unsigned int		width;
	unsigned int		height;
	unsigned int		flags;
	unsigned int		sizeimage;
	unsigned int		sequence;
	u32			fourcc;
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
	struct v4l2_ctrl	*ctrl_gop_size;
	unsigned int		gop_size;
	unsigned int		gop_cnt;

	/* Abort requested by m2m */
	int			aborting;
	struct vb2_v4l2_buffer *last_src_buf;
	struct vb2_v4l2_buffer *last_dst_buf;

	enum v4l2_colorspace	colorspace;
	enum v4l2_ycbcr_encoding ycbcr_enc;
	enum v4l2_xfer_func	xfer_func;
	enum v4l2_quantization	quantization;

	/* Source and destination queue data */
	struct vicodec_q_data   q_data[2];
	struct raw_frame	ref_frame;
	u8			*compressed_frame;
	u32			cur_buf_offset;
	u32			comp_max_size;
	u32			comp_size;
	u32			comp_magic_cnt;
	u32			comp_frame_size;
	bool			comp_has_frame;
	bool			comp_has_next_frame;
};

static const u32 pixfmts_yuv[] = {
	V4L2_PIX_FMT_YUV420,
	V4L2_PIX_FMT_YVU420,
	V4L2_PIX_FMT_NV12,
	V4L2_PIX_FMT_NV21,
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

static void encode(struct vicodec_ctx *ctx,
		   struct vicodec_q_data *q_data,
		   u8 *p_in, u8 *p_out)
{
	unsigned int size = q_data->width * q_data->height;
	struct cframe_hdr *p_hdr;
	struct cframe cf;
	struct raw_frame rf;
	u32 encoding;

	rf.width = q_data->width;
	rf.height = q_data->height;
	rf.luma = p_in;

	switch (q_data->fourcc) {
	case V4L2_PIX_FMT_YUV420:
		rf.cb = rf.luma + size;
		rf.cr = rf.cb + size / 4;
		rf.chroma_step = 1;
		break;
	case V4L2_PIX_FMT_YVU420:
		rf.cr = rf.luma + size;
		rf.cb = rf.cr + size / 4;
		rf.chroma_step = 1;
		break;
	case V4L2_PIX_FMT_NV12:
		rf.cb = rf.luma + size;
		rf.cr = rf.cb + 1;
		rf.chroma_step = 2;
		break;
	case V4L2_PIX_FMT_NV21:
		rf.cr = rf.luma + size;
		rf.cb = rf.cr + 1;
		rf.chroma_step = 2;
		break;
	}

	cf.width = q_data->width;
	cf.height = q_data->height;
	cf.rlc_data = (__be16 *)(p_out + sizeof(*p_hdr));

	encoding = encode_frame(&rf, &ctx->ref_frame, &cf, !ctx->gop_cnt,
				ctx->gop_cnt == ctx->gop_size - 1);
	if (encoding != FRAME_PCODED)
		ctx->gop_cnt = 0;
	if (++ctx->gop_cnt == ctx->gop_size)
		ctx->gop_cnt = 0;

	p_hdr = (struct cframe_hdr *)p_out;
	p_hdr->magic1 = VICODEC_MAGIC1;
	p_hdr->magic2 = VICODEC_MAGIC2;
	p_hdr->version = htonl(VICODEC_VERSION);
	p_hdr->width = htonl(cf.width);
	p_hdr->height = htonl(cf.height);
	p_hdr->flags = htonl(q_data->flags);
	if (encoding & LUMA_UNENCODED)
		p_hdr->flags |= htonl(VICODEC_FL_LUMA_IS_UNCOMPRESSED);
	if (encoding & CB_UNENCODED)
		p_hdr->flags |= htonl(VICODEC_FL_CB_IS_UNCOMPRESSED);
	if (encoding & CR_UNENCODED)
		p_hdr->flags |= htonl(VICODEC_FL_CR_IS_UNCOMPRESSED);
	p_hdr->colorspace = htonl(ctx->colorspace);
	p_hdr->xfer_func = htonl(ctx->xfer_func);
	p_hdr->ycbcr_enc = htonl(ctx->ycbcr_enc);
	p_hdr->quantization = htonl(ctx->quantization);
	p_hdr->size = htonl(cf.size);
	ctx->ref_frame.width = cf.width;
	ctx->ref_frame.height = cf.height;
}

static int decode(struct vicodec_ctx *ctx,
		  struct vicodec_q_data *q_data,
		  u8 *p_in, u8 *p_out)
{
	unsigned int size = q_data->width * q_data->height;
	unsigned int i;
	struct cframe_hdr *p_hdr;
	struct cframe cf;
	u8 *p;

	p_hdr = (struct cframe_hdr *)p_in;
	cf.width = ntohl(p_hdr->width);
	cf.height = ntohl(p_hdr->height);
	q_data->flags = ntohl(p_hdr->flags);
	ctx->colorspace = ntohl(p_hdr->colorspace);
	ctx->xfer_func = ntohl(p_hdr->xfer_func);
	ctx->ycbcr_enc = ntohl(p_hdr->ycbcr_enc);
	ctx->quantization = ntohl(p_hdr->quantization);
	cf.rlc_data = (__be16 *)(p_in + sizeof(*p_hdr));

	if (p_hdr->magic1 != VICODEC_MAGIC1 ||
	    p_hdr->magic2 != VICODEC_MAGIC2 ||
	    ntohl(p_hdr->version) != VICODEC_VERSION ||
	    cf.width < VICODEC_MIN_WIDTH ||
	    cf.width > VICODEC_MAX_WIDTH ||
	    cf.height < VICODEC_MIN_HEIGHT ||
	    cf.height > VICODEC_MAX_HEIGHT ||
	    (cf.width & 7) || (cf.height & 7))
		return -EINVAL;

	/* TODO: support resolution changes */
	if (cf.width != q_data->width || cf.height != q_data->height)
		return -EINVAL;

	decode_frame(&cf, &ctx->ref_frame, q_data->flags);
	memcpy(p_out, ctx->ref_frame.luma, size);
	p_out += size;

	switch (q_data->fourcc) {
	case V4L2_PIX_FMT_YUV420:
		memcpy(p_out, ctx->ref_frame.cb, size / 4);
		p_out += size / 4;
		memcpy(p_out, ctx->ref_frame.cr, size / 4);
		break;
	case V4L2_PIX_FMT_YVU420:
		memcpy(p_out, ctx->ref_frame.cr, size / 4);
		p_out += size / 4;
		memcpy(p_out, ctx->ref_frame.cb, size / 4);
		break;
	case V4L2_PIX_FMT_NV12:
		for (i = 0, p = p_out; i < size / 4; i++, p += 2)
			*p = ctx->ref_frame.cb[i];
		for (i = 0, p = p_out + 1; i < size / 4; i++, p += 2)
			*p = ctx->ref_frame.cr[i];
		break;
	case V4L2_PIX_FMT_NV21:
		for (i = 0, p = p_out; i < size / 4; i++, p += 2)
			*p = ctx->ref_frame.cr[i];
		for (i = 0, p = p_out + 1; i < size / 4; i++, p += 2)
			*p = ctx->ref_frame.cb[i];
		break;
	}
	return 0;
}

static int device_process(struct vicodec_ctx *ctx,
			  struct vb2_v4l2_buffer *in_vb,
			  struct vb2_v4l2_buffer *out_vb)
{
	struct vicodec_dev *dev = ctx->dev;
	struct vicodec_q_data *q_out, *q_cap;
	u8 *p_in, *p_out;
	int ret;

	q_out = get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT);
	q_cap = get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE);
	if (ctx->is_enc)
		p_in = vb2_plane_vaddr(&in_vb->vb2_buf, 0);
	else
		p_in = ctx->compressed_frame;
	p_out = vb2_plane_vaddr(&out_vb->vb2_buf, 0);
	if (!p_in || !p_out) {
		v4l2_err(&dev->v4l2_dev,
			 "Acquiring kernel pointers to buffers failed\n");
		return -EFAULT;
	}

	if (ctx->is_enc) {
		struct cframe_hdr *p_hdr = (struct cframe_hdr *)p_out;

		encode(ctx, q_out, p_in, p_out);
		vb2_set_plane_payload(&out_vb->vb2_buf, 0,
				      sizeof(*p_hdr) + ntohl(p_hdr->size));
	} else {
		ret = decode(ctx, q_cap, p_in, p_out);
		if (ret)
			return ret;
		vb2_set_plane_payload(&out_vb->vb2_buf, 0,
				      q_cap->width * q_cap->height * 3 / 2);
	}

	out_vb->sequence = q_cap->sequence++;
	out_vb->vb2_buf.timestamp = in_vb->vb2_buf.timestamp;

	if (in_vb->flags & V4L2_BUF_FLAG_TIMECODE)
		out_vb->timecode = in_vb->timecode;
	out_vb->field = in_vb->field;
	out_vb->flags &= ~V4L2_BUF_FLAG_LAST;
	out_vb->flags |= in_vb->flags &
		(V4L2_BUF_FLAG_TIMECODE |
		 V4L2_BUF_FLAG_KEYFRAME |
		 V4L2_BUF_FLAG_PFRAME |
		 V4L2_BUF_FLAG_BFRAME |
		 V4L2_BUF_FLAG_TSTAMP_SRC_MASK);

	return 0;
}

/*
 * mem2mem callbacks
 */

/* device_run() - prepares and starts the device */
static void device_run(void *priv)
{
	static const struct v4l2_event eos_event = {
		.type = V4L2_EVENT_EOS
	};
	struct vicodec_ctx *ctx = priv;
	struct vicodec_dev *dev = ctx->dev;
	struct vb2_v4l2_buffer *src_buf, *dst_buf;
	struct vicodec_q_data *q_out;
	u32 state;

	src_buf = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);
	dst_buf = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);
	q_out = get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT);

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
		src_buf->sequence = q_out->sequence++;
		src_buf = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
		v4l2_m2m_buf_done(src_buf, state);
	} else if (vb2_get_plane_payload(&src_buf->vb2_buf, 0) == ctx->cur_buf_offset) {
		src_buf->sequence = q_out->sequence++;
		src_buf = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
		v4l2_m2m_buf_done(src_buf, state);
		ctx->cur_buf_offset = 0;
		ctx->comp_has_next_frame = false;
	}
	v4l2_m2m_buf_done(dst_buf, state);
	ctx->comp_size = 0;
	ctx->comp_magic_cnt = 0;
	ctx->comp_has_frame = false;
	spin_unlock(ctx->lock);

	if (ctx->is_enc)
		v4l2_m2m_job_finish(dev->enc_dev, ctx->fh.m2m_ctx);
	else
		v4l2_m2m_job_finish(dev->dec_dev, ctx->fh.m2m_ctx);
}

static void job_remove_out_buf(struct vicodec_ctx *ctx, u32 state)
{
	struct vb2_v4l2_buffer *src_buf;
	struct vicodec_q_data *q_out;

	q_out = get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT);
	spin_lock(ctx->lock);
	src_buf = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
	src_buf->sequence = q_out->sequence++;
	v4l2_m2m_buf_done(src_buf, state);
	ctx->cur_buf_offset = 0;
	spin_unlock(ctx->lock);
}

static int job_ready(void *priv)
{
	static const u8 magic[] = {
		0x4f, 0x4f, 0x4f, 0x4f, 0xff, 0xff, 0xff, 0xff
	};
	struct vicodec_ctx *ctx = priv;
	struct vb2_v4l2_buffer *src_buf;
	u8 *p_out;
	u8 *p;
	u32 sz;
	u32 state;

	if (ctx->is_enc || ctx->comp_has_frame)
		return 1;

restart:
	ctx->comp_has_next_frame = false;
	src_buf = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);
	if (!src_buf)
		return 0;
	p_out = vb2_plane_vaddr(&src_buf->vb2_buf, 0);
	sz = vb2_get_plane_payload(&src_buf->vb2_buf, 0);
	p = p_out + ctx->cur_buf_offset;

	state = VB2_BUF_STATE_DONE;

	if (!ctx->comp_size) {
		state = VB2_BUF_STATE_ERROR;
		for (; p < p_out + sz; p++) {
			u32 copy;

			p = memchr(p, magic[ctx->comp_magic_cnt],
				   p_out + sz - p);
			if (!p) {
				ctx->comp_magic_cnt = 0;
				break;
			}
			copy = sizeof(magic) - ctx->comp_magic_cnt;
			if (p_out + sz - p < copy)
				copy = p_out + sz - p;
			memcpy(ctx->compressed_frame + ctx->comp_magic_cnt,
			       p, copy);
			ctx->comp_magic_cnt += copy;
			if (!memcmp(ctx->compressed_frame, magic, ctx->comp_magic_cnt)) {
				p += copy;
				state = VB2_BUF_STATE_DONE;
				break;
			}
			ctx->comp_magic_cnt = 0;
		}
		if (ctx->comp_magic_cnt < sizeof(magic)) {
			job_remove_out_buf(ctx, state);
			goto restart;
		}
		ctx->comp_size = sizeof(magic);
	}
	if (ctx->comp_size < sizeof(struct cframe_hdr)) {
		struct cframe_hdr *p_hdr = (struct cframe_hdr *)ctx->compressed_frame;
		u32 copy = sizeof(struct cframe_hdr) - ctx->comp_size;

		if (copy > p_out + sz - p)
			copy = p_out + sz - p;
		memcpy(ctx->compressed_frame + ctx->comp_size,
		       p, copy);
		p += copy;
		ctx->comp_size += copy;
		if (ctx->comp_size < sizeof(struct cframe_hdr)) {
			job_remove_out_buf(ctx, state);
			goto restart;
		}
		ctx->comp_frame_size = ntohl(p_hdr->size) + sizeof(*p_hdr);
		if (ctx->comp_frame_size > ctx->comp_max_size)
			ctx->comp_frame_size = ctx->comp_max_size;
	}
	if (ctx->comp_size < ctx->comp_frame_size) {
		u32 copy = ctx->comp_frame_size - ctx->comp_size;

		if (copy > p_out + sz - p)
			copy = p_out + sz - p;
		memcpy(ctx->compressed_frame + ctx->comp_size,
		       p, copy);
		p += copy;
		ctx->comp_size += copy;
		if (ctx->comp_size < ctx->comp_frame_size) {
			job_remove_out_buf(ctx, state);
			goto restart;
		}
	}
	ctx->cur_buf_offset = p - p_out;
	ctx->comp_has_frame = true;
	ctx->comp_has_next_frame = false;
	if (sz - ctx->cur_buf_offset >= sizeof(struct cframe_hdr)) {
		struct cframe_hdr *p_hdr = (struct cframe_hdr *)p;
		u32 frame_size = ntohl(p_hdr->size);
		u32 remaining = sz - ctx->cur_buf_offset - sizeof(*p_hdr);

		if (!memcmp(p, magic, sizeof(magic)))
			ctx->comp_has_next_frame = remaining >= frame_size;
	}
	return 1;
}

static void job_abort(void *priv)
{
	struct vicodec_ctx *ctx = priv;

	/* Will cancel the transaction in the next interrupt handler */
	ctx->aborting = 1;
}

/*
 * video ioctls
 */

static u32 find_fmt(u32 fmt)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(pixfmts_yuv); i++)
		if (pixfmts_yuv[i] == fmt)
			return fmt;
	return pixfmts_yuv[0];
}

static int vidioc_querycap(struct file *file, void *priv,
			   struct v4l2_capability *cap)
{
	strncpy(cap->driver, VICODEC_NAME, sizeof(cap->driver) - 1);
	strncpy(cap->card, VICODEC_NAME, sizeof(cap->card) - 1);
	snprintf(cap->bus_info, sizeof(cap->bus_info),
			"platform:%s", VICODEC_NAME);
	cap->device_caps =  V4L2_CAP_STREAMING |
			    (multiplanar ?
			     V4L2_CAP_VIDEO_M2M_MPLANE :
			     V4L2_CAP_VIDEO_M2M);
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;
	return 0;
}

static int enum_fmt(struct v4l2_fmtdesc *f, bool is_enc, bool is_out)
{
	bool is_yuv = (is_enc && is_out) || (!is_enc && !is_out);

	if (V4L2_TYPE_IS_MULTIPLANAR(f->type) && !multiplanar)
		return -EINVAL;
	if (!V4L2_TYPE_IS_MULTIPLANAR(f->type) && multiplanar)
		return -EINVAL;
	if (f->index >= (is_yuv ? ARRAY_SIZE(pixfmts_yuv) : 1))
		return -EINVAL;

	if (is_yuv)
		f->pixelformat = pixfmts_yuv[f->index];
	else
		f->pixelformat = V4L2_PIX_FMT_FWHT;
	return 0;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void *priv,
				   struct v4l2_fmtdesc *f)
{
	struct vicodec_ctx *ctx = file2ctx(file);

	return enum_fmt(f, ctx->is_enc, false);
}

static int vidioc_enum_fmt_vid_out(struct file *file, void *priv,
				   struct v4l2_fmtdesc *f)
{
	struct vicodec_ctx *ctx = file2ctx(file);

	return enum_fmt(f, ctx->is_enc, true);
}

static int vidioc_g_fmt(struct vicodec_ctx *ctx, struct v4l2_format *f)
{
	struct vb2_queue *vq;
	struct vicodec_q_data *q_data;
	struct v4l2_pix_format_mplane *pix_mp;
	struct v4l2_pix_format *pix;

	vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, f->type);
	if (!vq)
		return -EINVAL;

	q_data = get_q_data(ctx, f->type);

	switch (f->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
		if (multiplanar)
			return -EINVAL;
		pix = &f->fmt.pix;
		pix->width = q_data->width;
		pix->height = q_data->height;
		pix->field = V4L2_FIELD_NONE;
		pix->pixelformat = q_data->fourcc;
		if (q_data->fourcc == V4L2_PIX_FMT_FWHT)
			pix->bytesperline = 0;
		else
			pix->bytesperline = q_data->width;
		pix->sizeimage = q_data->sizeimage;
		pix->colorspace = ctx->colorspace;
		pix->xfer_func = ctx->xfer_func;
		pix->ycbcr_enc = ctx->ycbcr_enc;
		pix->quantization = ctx->quantization;
		break;

	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		if (!multiplanar)
			return -EINVAL;
		pix_mp = &f->fmt.pix_mp;
		pix_mp->width = q_data->width;
		pix_mp->height = q_data->height;
		pix_mp->field = V4L2_FIELD_NONE;
		pix_mp->pixelformat = q_data->fourcc;
		pix_mp->num_planes = 1;
		if (q_data->fourcc == V4L2_PIX_FMT_FWHT)
			pix_mp->plane_fmt[0].bytesperline = 0;
		else
			pix_mp->plane_fmt[0].bytesperline = q_data->width;
		pix_mp->plane_fmt[0].sizeimage = q_data->sizeimage;
		pix_mp->colorspace = ctx->colorspace;
		pix_mp->xfer_func = ctx->xfer_func;
		pix_mp->ycbcr_enc = ctx->ycbcr_enc;
		pix_mp->quantization = ctx->quantization;
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

	switch (f->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
		pix = &f->fmt.pix;
		pix->width = clamp(pix->width, MIN_WIDTH, MAX_WIDTH) & ~7;
		pix->height = clamp(pix->height, MIN_HEIGHT, MAX_HEIGHT) & ~7;
		pix->bytesperline = pix->width;
		pix->sizeimage = pix->width * pix->height * 3 / 2;
		pix->field = V4L2_FIELD_NONE;
		if (pix->pixelformat == V4L2_PIX_FMT_FWHT) {
			pix->bytesperline = 0;
			pix->sizeimage += sizeof(struct cframe_hdr);
		}
		break;
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		pix_mp = &f->fmt.pix_mp;
		pix_mp->width = clamp(pix_mp->width, MIN_WIDTH, MAX_WIDTH) & ~7;
		pix_mp->height =
			clamp(pix_mp->height, MIN_HEIGHT, MAX_HEIGHT) & ~7;
		pix_mp->plane_fmt[0].bytesperline = pix_mp->width;
		pix_mp->plane_fmt[0].sizeimage =
			pix_mp->width * pix_mp->height * 3 / 2;
		pix_mp->field = V4L2_FIELD_NONE;
		pix_mp->num_planes = 1;
		if (pix_mp->pixelformat == V4L2_PIX_FMT_FWHT) {
			pix_mp->plane_fmt[0].bytesperline = 0;
			pix_mp->plane_fmt[0].sizeimage +=
					sizeof(struct cframe_hdr);
		}
		memset(pix_mp->reserved, 0, sizeof(pix_mp->reserved));
		memset(pix_mp->plane_fmt[0].reserved, 0,
		       sizeof(pix_mp->plane_fmt[0].reserved));
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
				   find_fmt(f->fmt.pix.pixelformat);
		pix->colorspace = ctx->colorspace;
		pix->xfer_func = ctx->xfer_func;
		pix->ycbcr_enc = ctx->ycbcr_enc;
		pix->quantization = ctx->quantization;
		break;
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		if (!multiplanar)
			return -EINVAL;
		pix_mp = &f->fmt.pix_mp;
		pix_mp->pixelformat = ctx->is_enc ? V4L2_PIX_FMT_FWHT :
				      find_fmt(pix_mp->pixelformat);
		pix_mp->colorspace = ctx->colorspace;
		pix_mp->xfer_func = ctx->xfer_func;
		pix_mp->ycbcr_enc = ctx->ycbcr_enc;
		pix_mp->quantization = ctx->quantization;
		memset(pix_mp->reserved, 0, sizeof(pix_mp->reserved));
		memset(pix_mp->plane_fmt[0].reserved, 0,
		       sizeof(pix_mp->plane_fmt[0].reserved));
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
				   find_fmt(pix->pixelformat);
		if (!pix->colorspace)
			pix->colorspace = V4L2_COLORSPACE_REC709;
		break;
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		if (!multiplanar)
			return -EINVAL;
		pix_mp = &f->fmt.pix_mp;
		pix_mp->pixelformat = !ctx->is_enc ? V4L2_PIX_FMT_FWHT :
				      find_fmt(pix_mp->pixelformat);
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
				q_data->fourcc != pix->pixelformat ||
				q_data->width != pix->width ||
				q_data->height != pix->height;

		if (vb2_is_busy(vq) && fmt_changed)
			return -EBUSY;

		q_data->fourcc = pix->pixelformat;
		q_data->width = pix->width;
		q_data->height = pix->height;
		q_data->sizeimage = pix->sizeimage;
		break;
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		pix_mp = &f->fmt.pix_mp;
		if (ctx->is_enc && V4L2_TYPE_IS_OUTPUT(f->type))
			fmt_changed =
				q_data->fourcc != pix_mp->pixelformat ||
				q_data->width != pix_mp->width ||
				q_data->height != pix_mp->height;

		if (vb2_is_busy(vq) && fmt_changed)
			return -EBUSY;

		q_data->fourcc = pix_mp->pixelformat;
		q_data->width = pix_mp->width;
		q_data->height = pix_mp->height;
		q_data->sizeimage = pix_mp->plane_fmt[0].sizeimage;
		break;
	default:
		return -EINVAL;
	}

	dprintk(ctx->dev,
		"Setting format for type %d, wxh: %dx%d, fourcc: %08x\n",
		f->type, q_data->width, q_data->height, q_data->fourcc);

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
			ctx->colorspace = pix->colorspace;
			ctx->xfer_func = pix->xfer_func;
			ctx->ycbcr_enc = pix->ycbcr_enc;
			ctx->quantization = pix->quantization;
			break;
		case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
			pix_mp = &f->fmt.pix_mp;
			ctx->colorspace = pix_mp->colorspace;
			ctx->xfer_func = pix_mp->xfer_func;
			ctx->ycbcr_enc = pix_mp->ycbcr_enc;
			ctx->quantization = pix_mp->quantization;
			break;
		default:
			break;
		}
	}
	return ret;
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
		if (find_fmt(fsize->pixel_format) == fsize->pixel_format)
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
	switch (sub->type) {
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
	unsigned int size = q_data->width * q_data->height;

	q_data->sequence = 0;

	if (!V4L2_TYPE_IS_OUTPUT(q->type))
		return 0;

	ctx->ref_frame.width = ctx->ref_frame.height = 0;
	ctx->ref_frame.luma = kvmalloc(size * 3 / 2, GFP_KERNEL);
	ctx->comp_max_size = size * 3 / 2 + sizeof(struct cframe_hdr);
	ctx->compressed_frame = kvmalloc(ctx->comp_max_size, GFP_KERNEL);
	if (!ctx->ref_frame.luma || !ctx->compressed_frame) {
		kvfree(ctx->ref_frame.luma);
		kvfree(ctx->compressed_frame);
		vicodec_return_bufs(q, VB2_BUF_STATE_QUEUED);
		return -ENOMEM;
	}
	ctx->ref_frame.cb = ctx->ref_frame.luma + size;
	ctx->ref_frame.cr = ctx->ref_frame.cb + size / 4;
	ctx->last_src_buf = NULL;
	ctx->last_dst_buf = NULL;
	v4l2_ctrl_grab(ctx->ctrl_gop_size, true);
	ctx->gop_size = v4l2_ctrl_g_ctrl(ctx->ctrl_gop_size);
	ctx->gop_cnt = 0;
	ctx->cur_buf_offset = 0;
	ctx->comp_size = 0;
	ctx->comp_magic_cnt = 0;
	ctx->comp_has_frame = false;

	return 0;
}

static void vicodec_stop_streaming(struct vb2_queue *q)
{
	struct vicodec_ctx *ctx = vb2_get_drv_priv(q);

	vicodec_return_bufs(q, VB2_BUF_STATE_ERROR);

	if (!V4L2_TYPE_IS_OUTPUT(q->type))
		return;

	kvfree(ctx->ref_frame.luma);
	kvfree(ctx->compressed_frame);
	v4l2_ctrl_grab(ctx->ctrl_gop_size, false);
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
	ctx->ctrl_gop_size = v4l2_ctrl_new_std(hdl, NULL,
					       V4L2_CID_MPEG_VIDEO_GOP_SIZE,
					       1, 16, 1, 10);
	if (hdl->error) {
		rc = hdl->error;
		v4l2_ctrl_handler_free(hdl);
		kfree(ctx);
		goto open_unlock;
	}
	ctx->fh.ctrl_handler = hdl;
	v4l2_ctrl_handler_setup(hdl);

	ctx->q_data[V4L2_M2M_SRC].fourcc =
		ctx->is_enc ? V4L2_PIX_FMT_YUV420 : V4L2_PIX_FMT_FWHT;
	ctx->q_data[V4L2_M2M_SRC].width = 1280;
	ctx->q_data[V4L2_M2M_SRC].height = 720;
	size = 1280 * 720 * 3 / 2;
	ctx->q_data[V4L2_M2M_SRC].sizeimage = size;
	ctx->q_data[V4L2_M2M_DST] = ctx->q_data[V4L2_M2M_SRC];
	ctx->q_data[V4L2_M2M_DST].fourcc =
		ctx->is_enc ? V4L2_PIX_FMT_FWHT : V4L2_PIX_FMT_YUV420;
	ctx->colorspace = V4L2_COLORSPACE_REC709;

	size += sizeof(struct cframe_hdr);
	if (ctx->is_enc) {
		ctx->q_data[V4L2_M2M_DST].sizeimage = size;
		ctx->fh.m2m_ctx = v4l2_m2m_ctx_init(dev->enc_dev, ctx,
						    &queue_init);
		ctx->lock = &dev->enc_lock;
	} else {
		ctx->q_data[V4L2_M2M_SRC].sizeimage = size;
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
	.job_abort	= job_abort,
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
	strlcpy(dev->mdev.model, "vicodec", sizeof(dev->mdev.model));
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
	strlcpy(vfd->name, "vicodec-enc", sizeof(vfd->name));
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
	strlcpy(vfd->name, "vicodec-dec", sizeof(vfd->name));
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
