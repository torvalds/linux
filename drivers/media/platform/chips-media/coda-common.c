// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Coda multi-standard codec IP
 *
 * Copyright (C) 2012 Vista Silicon S.L.
 *    Javier Martin, <javier.martin@vista-silicon.com>
 *    Xavier Duret
 */

#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/gcd.h>
#include <linux/genalloc.h>
#include <linux/idr.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/kfifo.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/videodev2.h>
#include <linux/of.h>
#include <linux/ratelimit.h>
#include <linux/reset.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-dma-contig.h>
#include <media/videobuf2-vmalloc.h>

#include "coda.h"
#include "imx-vdoa.h"

#define CODA_NAME		"coda"

#define CODADX6_MAX_INSTANCES	4
#define CODA_MAX_FORMATS	5

#define CODA_ISRAM_SIZE	(2048 * 2)

#define MIN_W 48
#define MIN_H 16

#define S_ALIGN		1 /* multiple of 2 */
#define W_ALIGN		1 /* multiple of 2 */
#define H_ALIGN		1 /* multiple of 2 */

#define fh_to_ctx(__fh)	container_of(__fh, struct coda_ctx, fh)

int coda_debug;
module_param(coda_debug, int, 0644);
MODULE_PARM_DESC(coda_debug, "Debug level (0-2)");

static int disable_tiling;
module_param(disable_tiling, int, 0644);
MODULE_PARM_DESC(disable_tiling, "Disable tiled frame buffers");

static int disable_vdoa;
module_param(disable_vdoa, int, 0644);
MODULE_PARM_DESC(disable_vdoa, "Disable Video Data Order Adapter tiled to raster-scan conversion");

static int enable_bwb = 0;
module_param(enable_bwb, int, 0644);
MODULE_PARM_DESC(enable_bwb, "Enable BWB unit for decoding, may crash on certain streams");

void coda_write(struct coda_dev *dev, u32 data, u32 reg)
{
	v4l2_dbg(3, coda_debug, &dev->v4l2_dev,
		 "%s: data=0x%x, reg=0x%x\n", __func__, data, reg);
	writel(data, dev->regs_base + reg);
}

unsigned int coda_read(struct coda_dev *dev, u32 reg)
{
	u32 data;

	data = readl(dev->regs_base + reg);
	v4l2_dbg(3, coda_debug, &dev->v4l2_dev,
		 "%s: data=0x%x, reg=0x%x\n", __func__, data, reg);
	return data;
}

void coda_write_base(struct coda_ctx *ctx, struct coda_q_data *q_data,
		     struct vb2_v4l2_buffer *buf, unsigned int reg_y)
{
	u32 base_y = vb2_dma_contig_plane_dma_addr(&buf->vb2_buf, 0);
	u32 base_cb, base_cr;

	switch (q_data->fourcc) {
	case V4L2_PIX_FMT_YUYV:
		/* Fallthrough: IN -H264-> CODA -NV12 MB-> VDOA -YUYV-> OUT */
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_YUV420:
	default:
		base_cb = base_y + q_data->bytesperline * q_data->height;
		base_cr = base_cb + q_data->bytesperline * q_data->height / 4;
		break;
	case V4L2_PIX_FMT_YVU420:
		/* Switch Cb and Cr for YVU420 format */
		base_cr = base_y + q_data->bytesperline * q_data->height;
		base_cb = base_cr + q_data->bytesperline * q_data->height / 4;
		break;
	case V4L2_PIX_FMT_YUV422P:
		base_cb = base_y + q_data->bytesperline * q_data->height;
		base_cr = base_cb + q_data->bytesperline * q_data->height / 2;
	}

	coda_write(ctx->dev, base_y, reg_y);
	coda_write(ctx->dev, base_cb, reg_y + 4);
	coda_write(ctx->dev, base_cr, reg_y + 8);
}

#define CODA_CODEC(mode, src_fourcc, dst_fourcc, max_w, max_h) \
	{ mode, src_fourcc, dst_fourcc, max_w, max_h }

/*
 * Arrays of codecs supported by each given version of Coda:
 *  i.MX27 -> codadx6
 *  i.MX51 -> codahx4
 *  i.MX53 -> coda7
 *  i.MX6  -> coda960
 * Use V4L2_PIX_FMT_YUV420 as placeholder for all supported YUV 4:2:0 variants
 */
static const struct coda_codec codadx6_codecs[] = {
	CODA_CODEC(CODADX6_MODE_ENCODE_H264, V4L2_PIX_FMT_YUV420, V4L2_PIX_FMT_H264,  720, 576),
	CODA_CODEC(CODADX6_MODE_ENCODE_MP4,  V4L2_PIX_FMT_YUV420, V4L2_PIX_FMT_MPEG4, 720, 576),
};

static const struct coda_codec codahx4_codecs[] = {
	CODA_CODEC(CODA7_MODE_ENCODE_H264, V4L2_PIX_FMT_YUV420, V4L2_PIX_FMT_H264,   720, 576),
	CODA_CODEC(CODA7_MODE_DECODE_H264, V4L2_PIX_FMT_H264,   V4L2_PIX_FMT_YUV420, 1920, 1088),
	CODA_CODEC(CODA7_MODE_DECODE_MP2,  V4L2_PIX_FMT_MPEG2,  V4L2_PIX_FMT_YUV420, 1920, 1088),
	CODA_CODEC(CODA7_MODE_DECODE_MP4,  V4L2_PIX_FMT_MPEG4,  V4L2_PIX_FMT_YUV420, 1280, 720),
};

static const struct coda_codec coda7_codecs[] = {
	CODA_CODEC(CODA7_MODE_ENCODE_H264, V4L2_PIX_FMT_YUV420, V4L2_PIX_FMT_H264,   1280, 720),
	CODA_CODEC(CODA7_MODE_ENCODE_MP4,  V4L2_PIX_FMT_YUV420, V4L2_PIX_FMT_MPEG4,  1280, 720),
	CODA_CODEC(CODA7_MODE_ENCODE_MJPG, V4L2_PIX_FMT_YUV420, V4L2_PIX_FMT_JPEG,   8192, 8192),
	CODA_CODEC(CODA7_MODE_DECODE_H264, V4L2_PIX_FMT_H264,   V4L2_PIX_FMT_YUV420, 1920, 1088),
	CODA_CODEC(CODA7_MODE_DECODE_MP2,  V4L2_PIX_FMT_MPEG2,  V4L2_PIX_FMT_YUV420, 1920, 1088),
	CODA_CODEC(CODA7_MODE_DECODE_MP4,  V4L2_PIX_FMT_MPEG4,  V4L2_PIX_FMT_YUV420, 1920, 1088),
	CODA_CODEC(CODA7_MODE_DECODE_MJPG, V4L2_PIX_FMT_JPEG,   V4L2_PIX_FMT_YUV420, 8192, 8192),
};

static const struct coda_codec coda9_codecs[] = {
	CODA_CODEC(CODA9_MODE_ENCODE_H264, V4L2_PIX_FMT_YUV420, V4L2_PIX_FMT_H264,   1920, 1088),
	CODA_CODEC(CODA9_MODE_ENCODE_MP4,  V4L2_PIX_FMT_YUV420, V4L2_PIX_FMT_MPEG4,  1920, 1088),
	CODA_CODEC(CODA9_MODE_ENCODE_MJPG, V4L2_PIX_FMT_YUV420, V4L2_PIX_FMT_JPEG,   8192, 8192),
	CODA_CODEC(CODA9_MODE_DECODE_H264, V4L2_PIX_FMT_H264,   V4L2_PIX_FMT_YUV420, 1920, 1088),
	CODA_CODEC(CODA9_MODE_DECODE_MP2,  V4L2_PIX_FMT_MPEG2,  V4L2_PIX_FMT_YUV420, 1920, 1088),
	CODA_CODEC(CODA9_MODE_DECODE_MP4,  V4L2_PIX_FMT_MPEG4,  V4L2_PIX_FMT_YUV420, 1920, 1088),
	CODA_CODEC(CODA9_MODE_DECODE_MJPG, V4L2_PIX_FMT_JPEG,	V4L2_PIX_FMT_YUV420, 8192, 8192),
};

struct coda_video_device {
	const char *name;
	enum coda_inst_type type;
	const struct coda_context_ops *ops;
	bool direct;
	u32 src_formats[CODA_MAX_FORMATS];
	u32 dst_formats[CODA_MAX_FORMATS];
};

static const struct coda_video_device coda_bit_encoder = {
	.name = "coda-video-encoder",
	.type = CODA_INST_ENCODER,
	.ops = &coda_bit_encode_ops,
	.src_formats = {
		V4L2_PIX_FMT_NV12,
		V4L2_PIX_FMT_YUV420,
		V4L2_PIX_FMT_YVU420,
	},
	.dst_formats = {
		V4L2_PIX_FMT_H264,
		V4L2_PIX_FMT_MPEG4,
	},
};

static const struct coda_video_device coda_bit_jpeg_encoder = {
	.name = "coda-jpeg-encoder",
	.type = CODA_INST_ENCODER,
	.ops = &coda_bit_encode_ops,
	.src_formats = {
		V4L2_PIX_FMT_NV12,
		V4L2_PIX_FMT_YUV420,
		V4L2_PIX_FMT_YVU420,
		V4L2_PIX_FMT_YUV422P,
	},
	.dst_formats = {
		V4L2_PIX_FMT_JPEG,
	},
};

static const struct coda_video_device coda_bit_decoder = {
	.name = "coda-video-decoder",
	.type = CODA_INST_DECODER,
	.ops = &coda_bit_decode_ops,
	.src_formats = {
		V4L2_PIX_FMT_H264,
		V4L2_PIX_FMT_MPEG2,
		V4L2_PIX_FMT_MPEG4,
	},
	.dst_formats = {
		V4L2_PIX_FMT_NV12,
		V4L2_PIX_FMT_YUV420,
		V4L2_PIX_FMT_YVU420,
		/*
		 * If V4L2_PIX_FMT_YUYV should be default,
		 * set_default_params() must be adjusted.
		 */
		V4L2_PIX_FMT_YUYV,
	},
};

static const struct coda_video_device coda_bit_jpeg_decoder = {
	.name = "coda-jpeg-decoder",
	.type = CODA_INST_DECODER,
	.ops = &coda_bit_decode_ops,
	.src_formats = {
		V4L2_PIX_FMT_JPEG,
	},
	.dst_formats = {
		V4L2_PIX_FMT_NV12,
		V4L2_PIX_FMT_YUV420,
		V4L2_PIX_FMT_YVU420,
		V4L2_PIX_FMT_YUV422P,
	},
};

static const struct coda_video_device coda9_jpeg_encoder = {
	.name = "coda-jpeg-encoder",
	.type = CODA_INST_ENCODER,
	.ops = &coda9_jpeg_encode_ops,
	.direct = true,
	.src_formats = {
		V4L2_PIX_FMT_NV12,
		V4L2_PIX_FMT_YUV420,
		V4L2_PIX_FMT_YVU420,
		V4L2_PIX_FMT_YUV422P,
		V4L2_PIX_FMT_GREY,
	},
	.dst_formats = {
		V4L2_PIX_FMT_JPEG,
	},
};

static const struct coda_video_device coda9_jpeg_decoder = {
	.name = "coda-jpeg-decoder",
	.type = CODA_INST_DECODER,
	.ops = &coda9_jpeg_decode_ops,
	.direct = true,
	.src_formats = {
		V4L2_PIX_FMT_JPEG,
	},
	.dst_formats = {
		V4L2_PIX_FMT_NV12,
		V4L2_PIX_FMT_YUV420,
		V4L2_PIX_FMT_YVU420,
		V4L2_PIX_FMT_YUV422P,
	},
};

static const struct coda_video_device *codadx6_video_devices[] = {
	&coda_bit_encoder,
};

static const struct coda_video_device *codahx4_video_devices[] = {
	&coda_bit_encoder,
	&coda_bit_decoder,
};

static const struct coda_video_device *coda7_video_devices[] = {
	&coda_bit_jpeg_encoder,
	&coda_bit_jpeg_decoder,
	&coda_bit_encoder,
	&coda_bit_decoder,
};

static const struct coda_video_device *coda9_video_devices[] = {
	&coda9_jpeg_encoder,
	&coda9_jpeg_decoder,
	&coda_bit_encoder,
	&coda_bit_decoder,
};

/*
 * Normalize all supported YUV 4:2:0 formats to the value used in the codec
 * tables.
 */
static u32 coda_format_normalize_yuv(u32 fourcc)
{
	switch (fourcc) {
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_YVU420:
	case V4L2_PIX_FMT_YUV422P:
	case V4L2_PIX_FMT_YUYV:
		return V4L2_PIX_FMT_YUV420;
	default:
		return fourcc;
	}
}

static const struct coda_codec *coda_find_codec(struct coda_dev *dev,
						int src_fourcc, int dst_fourcc)
{
	const struct coda_codec *codecs = dev->devtype->codecs;
	int num_codecs = dev->devtype->num_codecs;
	int k;

	src_fourcc = coda_format_normalize_yuv(src_fourcc);
	dst_fourcc = coda_format_normalize_yuv(dst_fourcc);
	if (src_fourcc == dst_fourcc)
		return NULL;

	for (k = 0; k < num_codecs; k++) {
		if (codecs[k].src_fourcc == src_fourcc &&
		    codecs[k].dst_fourcc == dst_fourcc)
			break;
	}

	if (k == num_codecs)
		return NULL;

	return &codecs[k];
}

static void coda_get_max_dimensions(struct coda_dev *dev,
				    const struct coda_codec *codec,
				    int *max_w, int *max_h)
{
	const struct coda_codec *codecs = dev->devtype->codecs;
	int num_codecs = dev->devtype->num_codecs;
	unsigned int w, h;
	int k;

	if (codec) {
		w = codec->max_w;
		h = codec->max_h;
	} else {
		for (k = 0, w = 0, h = 0; k < num_codecs; k++) {
			w = max(w, codecs[k].max_w);
			h = max(h, codecs[k].max_h);
		}
	}

	if (max_w)
		*max_w = w;
	if (max_h)
		*max_h = h;
}

static const struct coda_video_device *to_coda_video_device(struct video_device
							    *vdev)
{
	struct coda_dev *dev = video_get_drvdata(vdev);
	unsigned int i = vdev - dev->vfd;

	if (i >= dev->devtype->num_vdevs)
		return NULL;

	return dev->devtype->vdevs[i];
}

const char *coda_product_name(int product)
{
	static char buf[9];

	switch (product) {
	case CODA_DX6:
		return "CodaDx6";
	case CODA_HX4:
		return "CodaHx4";
	case CODA_7541:
		return "CODA7541";
	case CODA_960:
		return "CODA960";
	default:
		snprintf(buf, sizeof(buf), "(0x%04x)", product);
		return buf;
	}
}

static struct vdoa_data *coda_get_vdoa_data(void)
{
	struct device_node *vdoa_node;
	struct platform_device *vdoa_pdev;
	struct vdoa_data *vdoa_data = NULL;

	vdoa_node = of_find_compatible_node(NULL, NULL, "fsl,imx6q-vdoa");
	if (!vdoa_node)
		return NULL;

	vdoa_pdev = of_find_device_by_node(vdoa_node);
	if (!vdoa_pdev)
		goto out;

	vdoa_data = platform_get_drvdata(vdoa_pdev);
	if (!vdoa_data)
		vdoa_data = ERR_PTR(-EPROBE_DEFER);

	put_device(&vdoa_pdev->dev);
out:
	of_node_put(vdoa_node);

	return vdoa_data;
}

/*
 * V4L2 ioctl() operations.
 */
static int coda_querycap(struct file *file, void *priv,
			 struct v4l2_capability *cap)
{
	struct coda_ctx *ctx = fh_to_ctx(priv);

	strscpy(cap->driver, CODA_NAME, sizeof(cap->driver));
	strscpy(cap->card, coda_product_name(ctx->dev->devtype->product),
		sizeof(cap->card));
	strscpy(cap->bus_info, "platform:" CODA_NAME, sizeof(cap->bus_info));
	return 0;
}

static const u32 coda_formats_420[CODA_MAX_FORMATS] = {
		V4L2_PIX_FMT_NV12,
		V4L2_PIX_FMT_YUV420,
		V4L2_PIX_FMT_YVU420,
};

static int coda_enum_fmt(struct file *file, void *priv,
			 struct v4l2_fmtdesc *f)
{
	struct video_device *vdev = video_devdata(file);
	const struct coda_video_device *cvd = to_coda_video_device(vdev);
	struct coda_ctx *ctx = fh_to_ctx(priv);
	const u32 *formats;

	if (f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT)
		formats = cvd->src_formats;
	else if (f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		struct coda_q_data *q_data_src;
		struct vb2_queue *src_vq;

		formats = cvd->dst_formats;

		/*
		 * If the source format is already fixed, only allow the same
		 * chroma subsampling.
		 */
		q_data_src = get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT);
		src_vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx,
					 V4L2_BUF_TYPE_VIDEO_OUTPUT);
		if (q_data_src->fourcc == V4L2_PIX_FMT_JPEG &&
		    vb2_is_streaming(src_vq)) {
			if (ctx->params.jpeg_chroma_subsampling ==
			    V4L2_JPEG_CHROMA_SUBSAMPLING_420) {
				formats = coda_formats_420;
			} else if (ctx->params.jpeg_chroma_subsampling ==
				   V4L2_JPEG_CHROMA_SUBSAMPLING_422) {
				f->pixelformat = V4L2_PIX_FMT_YUV422P;
				return f->index ? -EINVAL : 0;
			}
		}
	} else {
		return -EINVAL;
	}

	if (f->index >= CODA_MAX_FORMATS || formats[f->index] == 0)
		return -EINVAL;

	/* Skip YUYV if the vdoa is not available */
	if (!ctx->vdoa && f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE &&
	    formats[f->index] == V4L2_PIX_FMT_YUYV)
		return -EINVAL;

	f->pixelformat = formats[f->index];

	return 0;
}

static int coda_g_fmt(struct file *file, void *priv,
		      struct v4l2_format *f)
{
	struct coda_q_data *q_data;
	struct coda_ctx *ctx = fh_to_ctx(priv);

	q_data = get_q_data(ctx, f->type);
	if (!q_data)
		return -EINVAL;

	f->fmt.pix.field	= V4L2_FIELD_NONE;
	f->fmt.pix.pixelformat	= q_data->fourcc;
	f->fmt.pix.width	= q_data->width;
	f->fmt.pix.height	= q_data->height;
	f->fmt.pix.bytesperline = q_data->bytesperline;

	f->fmt.pix.sizeimage	= q_data->sizeimage;
	f->fmt.pix.colorspace	= ctx->colorspace;
	f->fmt.pix.xfer_func	= ctx->xfer_func;
	f->fmt.pix.ycbcr_enc	= ctx->ycbcr_enc;
	f->fmt.pix.quantization	= ctx->quantization;

	return 0;
}

static int coda_try_pixelformat(struct coda_ctx *ctx, struct v4l2_format *f)
{
	struct coda_q_data *q_data;
	const u32 *formats;
	int i;

	if (f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT)
		formats = ctx->cvd->src_formats;
	else if (f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE)
		formats = ctx->cvd->dst_formats;
	else
		return -EINVAL;

	for (i = 0; i < CODA_MAX_FORMATS; i++) {
		/* Skip YUYV if the vdoa is not available */
		if (!ctx->vdoa && f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE &&
		    formats[i] == V4L2_PIX_FMT_YUYV)
			continue;

		if (formats[i] == f->fmt.pix.pixelformat) {
			f->fmt.pix.pixelformat = formats[i];
			return 0;
		}
	}

	/* Fall back to currently set pixelformat */
	q_data = get_q_data(ctx, f->type);
	f->fmt.pix.pixelformat = q_data->fourcc;

	return 0;
}

static int coda_try_fmt_vdoa(struct coda_ctx *ctx, struct v4l2_format *f,
			     bool *use_vdoa)
{
	int err;

	if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	if (!use_vdoa)
		return -EINVAL;

	if (!ctx->vdoa) {
		*use_vdoa = false;
		return 0;
	}

	err = vdoa_context_configure(NULL, round_up(f->fmt.pix.width, 16),
				     f->fmt.pix.height, f->fmt.pix.pixelformat);
	if (err) {
		*use_vdoa = false;
		return 0;
	}

	*use_vdoa = true;
	return 0;
}

static unsigned int coda_estimate_sizeimage(struct coda_ctx *ctx, u32 sizeimage,
					    u32 width, u32 height)
{
	/*
	 * This is a rough estimate for sensible compressed buffer
	 * sizes (between 1 and 16 bits per pixel). This could be
	 * improved by better format specific worst case estimates.
	 */
	return round_up(clamp(sizeimage, width * height / 8,
					 width * height * 2), PAGE_SIZE);
}

static int coda_try_fmt(struct coda_ctx *ctx, const struct coda_codec *codec,
			struct v4l2_format *f)
{
	struct coda_dev *dev = ctx->dev;
	unsigned int max_w, max_h;
	enum v4l2_field field;

	field = f->fmt.pix.field;
	if (field == V4L2_FIELD_ANY)
		field = V4L2_FIELD_NONE;
	else if (V4L2_FIELD_NONE != field)
		return -EINVAL;

	/* V4L2 specification suggests the driver corrects the format struct
	 * if any of the dimensions is unsupported */
	f->fmt.pix.field = field;

	coda_get_max_dimensions(dev, codec, &max_w, &max_h);
	v4l_bound_align_image(&f->fmt.pix.width, MIN_W, max_w, W_ALIGN,
			      &f->fmt.pix.height, MIN_H, max_h, H_ALIGN,
			      S_ALIGN);

	switch (f->fmt.pix.pixelformat) {
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_YVU420:
		/*
		 * Frame stride must be at least multiple of 8,
		 * but multiple of 16 for h.264 or JPEG 4:2:x
		 */
		f->fmt.pix.bytesperline = round_up(f->fmt.pix.width, 16);
		f->fmt.pix.sizeimage = f->fmt.pix.bytesperline *
					f->fmt.pix.height * 3 / 2;
		break;
	case V4L2_PIX_FMT_YUYV:
		f->fmt.pix.bytesperline = round_up(f->fmt.pix.width, 16) * 2;
		f->fmt.pix.sizeimage = f->fmt.pix.bytesperline *
					f->fmt.pix.height;
		break;
	case V4L2_PIX_FMT_YUV422P:
		f->fmt.pix.bytesperline = round_up(f->fmt.pix.width, 16);
		f->fmt.pix.sizeimage = f->fmt.pix.bytesperline *
					f->fmt.pix.height * 2;
		break;
	case V4L2_PIX_FMT_GREY:
		/* keep 16 pixel alignment of 8-bit pixel data */
		f->fmt.pix.bytesperline = round_up(f->fmt.pix.width, 16);
		f->fmt.pix.sizeimage = f->fmt.pix.bytesperline * f->fmt.pix.height;
		break;
	case V4L2_PIX_FMT_JPEG:
	case V4L2_PIX_FMT_H264:
	case V4L2_PIX_FMT_MPEG4:
	case V4L2_PIX_FMT_MPEG2:
		f->fmt.pix.bytesperline = 0;
		f->fmt.pix.sizeimage = coda_estimate_sizeimage(ctx,
							f->fmt.pix.sizeimage,
							f->fmt.pix.width,
							f->fmt.pix.height);
		break;
	default:
		BUG();
	}

	return 0;
}

static int coda_try_fmt_vid_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct coda_ctx *ctx = fh_to_ctx(priv);
	const struct coda_q_data *q_data_src;
	const struct coda_codec *codec;
	struct vb2_queue *src_vq;
	int ret;
	bool use_vdoa;

	ret = coda_try_pixelformat(ctx, f);
	if (ret < 0)
		return ret;

	q_data_src = get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT);

	/*
	 * If the source format is already fixed, only allow the same output
	 * resolution. When decoding JPEG images, we also have to make sure to
	 * use the same chroma subsampling.
	 */
	src_vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT);
	if (vb2_is_streaming(src_vq)) {
		f->fmt.pix.width = q_data_src->width;
		f->fmt.pix.height = q_data_src->height;

		if (q_data_src->fourcc == V4L2_PIX_FMT_JPEG) {
			if (ctx->params.jpeg_chroma_subsampling ==
			    V4L2_JPEG_CHROMA_SUBSAMPLING_420 &&
			    f->fmt.pix.pixelformat == V4L2_PIX_FMT_YUV422P)
				f->fmt.pix.pixelformat = V4L2_PIX_FMT_NV12;
			else if (ctx->params.jpeg_chroma_subsampling ==
				 V4L2_JPEG_CHROMA_SUBSAMPLING_422)
				f->fmt.pix.pixelformat = V4L2_PIX_FMT_YUV422P;
		}
	}

	f->fmt.pix.colorspace = ctx->colorspace;
	f->fmt.pix.xfer_func = ctx->xfer_func;
	f->fmt.pix.ycbcr_enc = ctx->ycbcr_enc;
	f->fmt.pix.quantization = ctx->quantization;

	q_data_src = get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT);
	codec = coda_find_codec(ctx->dev, q_data_src->fourcc,
				f->fmt.pix.pixelformat);
	if (!codec)
		return -EINVAL;

	ret = coda_try_fmt(ctx, codec, f);
	if (ret < 0)
		return ret;

	/* The decoders always write complete macroblocks or MCUs */
	if (ctx->inst_type == CODA_INST_DECODER) {
		f->fmt.pix.bytesperline = round_up(f->fmt.pix.width, 16);
		f->fmt.pix.height = round_up(f->fmt.pix.height, 16);
		if (codec->src_fourcc == V4L2_PIX_FMT_JPEG &&
		    f->fmt.pix.pixelformat == V4L2_PIX_FMT_YUV422P) {
			f->fmt.pix.sizeimage = f->fmt.pix.bytesperline *
					       f->fmt.pix.height * 2;
		} else {
			f->fmt.pix.sizeimage = f->fmt.pix.bytesperline *
					       f->fmt.pix.height * 3 / 2;
		}

		ret = coda_try_fmt_vdoa(ctx, f, &use_vdoa);
		if (ret < 0)
			return ret;

		if (f->fmt.pix.pixelformat == V4L2_PIX_FMT_YUYV) {
			if (!use_vdoa)
				return -EINVAL;

			f->fmt.pix.bytesperline = round_up(f->fmt.pix.width, 16) * 2;
			f->fmt.pix.sizeimage = f->fmt.pix.bytesperline *
				f->fmt.pix.height;
		}
	}

	return 0;
}

static void coda_set_default_colorspace(struct v4l2_pix_format *fmt)
{
	enum v4l2_colorspace colorspace;

	if (fmt->pixelformat == V4L2_PIX_FMT_JPEG)
		colorspace = V4L2_COLORSPACE_JPEG;
	else if (fmt->width <= 720 && fmt->height <= 576)
		colorspace = V4L2_COLORSPACE_SMPTE170M;
	else
		colorspace = V4L2_COLORSPACE_REC709;

	fmt->colorspace = colorspace;
	fmt->xfer_func = V4L2_XFER_FUNC_DEFAULT;
	fmt->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	fmt->quantization = V4L2_QUANTIZATION_DEFAULT;
}

static int coda_try_fmt_vid_out(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct coda_ctx *ctx = fh_to_ctx(priv);
	struct coda_dev *dev = ctx->dev;
	const struct coda_q_data *q_data_dst;
	const struct coda_codec *codec;
	int ret;

	ret = coda_try_pixelformat(ctx, f);
	if (ret < 0)
		return ret;

	if (f->fmt.pix.colorspace == V4L2_COLORSPACE_DEFAULT)
		coda_set_default_colorspace(&f->fmt.pix);

	q_data_dst = get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE);
	codec = coda_find_codec(dev, f->fmt.pix.pixelformat, q_data_dst->fourcc);

	return coda_try_fmt(ctx, codec, f);
}

static int coda_s_fmt(struct coda_ctx *ctx, struct v4l2_format *f,
		      struct v4l2_rect *r)
{
	struct coda_q_data *q_data;
	struct vb2_queue *vq;

	vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, f->type);
	if (!vq)
		return -EINVAL;

	q_data = get_q_data(ctx, f->type);
	if (!q_data)
		return -EINVAL;

	if (vb2_is_busy(vq)) {
		v4l2_err(&ctx->dev->v4l2_dev, "%s: %s queue busy: %d\n",
			 __func__, v4l2_type_names[f->type], vq->num_buffers);
		return -EBUSY;
	}

	q_data->fourcc = f->fmt.pix.pixelformat;
	q_data->width = f->fmt.pix.width;
	q_data->height = f->fmt.pix.height;
	q_data->bytesperline = f->fmt.pix.bytesperline;
	q_data->sizeimage = f->fmt.pix.sizeimage;
	if (r) {
		q_data->rect = *r;
	} else {
		q_data->rect.left = 0;
		q_data->rect.top = 0;
		q_data->rect.width = f->fmt.pix.width;
		q_data->rect.height = f->fmt.pix.height;
	}

	switch (f->fmt.pix.pixelformat) {
	case V4L2_PIX_FMT_YUYV:
		ctx->tiled_map_type = GDI_TILED_FRAME_MB_RASTER_MAP;
		break;
	case V4L2_PIX_FMT_NV12:
		if (!disable_tiling && ctx->use_bit &&
		    ctx->dev->devtype->product == CODA_960) {
			ctx->tiled_map_type = GDI_TILED_FRAME_MB_RASTER_MAP;
			break;
		}
		fallthrough;
	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_YVU420:
	case V4L2_PIX_FMT_YUV422P:
		ctx->tiled_map_type = GDI_LINEAR_FRAME_MAP;
		break;
	default:
		break;
	}

	if (ctx->tiled_map_type == GDI_TILED_FRAME_MB_RASTER_MAP &&
	    !coda_try_fmt_vdoa(ctx, f, &ctx->use_vdoa) &&
	    ctx->use_vdoa)
		vdoa_context_configure(ctx->vdoa,
				       round_up(f->fmt.pix.width, 16),
				       f->fmt.pix.height,
				       f->fmt.pix.pixelformat);
	else
		ctx->use_vdoa = false;

	coda_dbg(1, ctx, "Setting %s format, wxh: %dx%d, fmt: %4.4s %c\n",
		 v4l2_type_names[f->type], q_data->width, q_data->height,
		 (char *)&q_data->fourcc,
		 (ctx->tiled_map_type == GDI_LINEAR_FRAME_MAP) ? 'L' : 'T');

	return 0;
}

static int coda_s_fmt_vid_cap(struct file *file, void *priv,
			      struct v4l2_format *f)
{
	struct coda_ctx *ctx = fh_to_ctx(priv);
	struct coda_q_data *q_data_src;
	const struct coda_codec *codec;
	struct v4l2_rect r;
	int ret;

	ret = coda_try_fmt_vid_cap(file, priv, f);
	if (ret)
		return ret;

	q_data_src = get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT);
	r.left = 0;
	r.top = 0;
	r.width = q_data_src->width;
	r.height = q_data_src->height;

	ret = coda_s_fmt(ctx, f, &r);
	if (ret)
		return ret;

	if (ctx->inst_type != CODA_INST_ENCODER)
		return 0;

	/* Setting the coded format determines the selected codec */
	codec = coda_find_codec(ctx->dev, q_data_src->fourcc,
				f->fmt.pix.pixelformat);
	if (!codec) {
		v4l2_err(&ctx->dev->v4l2_dev, "failed to determine codec\n");
		return -EINVAL;
	}
	ctx->codec = codec;

	ctx->colorspace = f->fmt.pix.colorspace;
	ctx->xfer_func = f->fmt.pix.xfer_func;
	ctx->ycbcr_enc = f->fmt.pix.ycbcr_enc;
	ctx->quantization = f->fmt.pix.quantization;

	return 0;
}

static int coda_s_fmt_vid_out(struct file *file, void *priv,
			      struct v4l2_format *f)
{
	struct coda_ctx *ctx = fh_to_ctx(priv);
	const struct coda_codec *codec;
	struct v4l2_format f_cap;
	struct vb2_queue *dst_vq;
	int ret;

	ret = coda_try_fmt_vid_out(file, priv, f);
	if (ret)
		return ret;

	ret = coda_s_fmt(ctx, f, NULL);
	if (ret)
		return ret;

	ctx->colorspace = f->fmt.pix.colorspace;
	ctx->xfer_func = f->fmt.pix.xfer_func;
	ctx->ycbcr_enc = f->fmt.pix.ycbcr_enc;
	ctx->quantization = f->fmt.pix.quantization;

	if (ctx->inst_type != CODA_INST_DECODER)
		return 0;

	/* Setting the coded format determines the selected codec */
	codec = coda_find_codec(ctx->dev, f->fmt.pix.pixelformat,
				V4L2_PIX_FMT_YUV420);
	if (!codec) {
		v4l2_err(&ctx->dev->v4l2_dev, "failed to determine codec\n");
		return -EINVAL;
	}
	ctx->codec = codec;

	dst_vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE);
	if (!dst_vq)
		return -EINVAL;

	/*
	 * Setting the capture queue format is not possible while the capture
	 * queue is still busy. This is not an error, but the user will have to
	 * make sure themselves that the capture format is set correctly before
	 * starting the output queue again.
	 */
	if (vb2_is_busy(dst_vq))
		return 0;

	memset(&f_cap, 0, sizeof(f_cap));
	f_cap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	coda_g_fmt(file, priv, &f_cap);
	f_cap.fmt.pix.width = f->fmt.pix.width;
	f_cap.fmt.pix.height = f->fmt.pix.height;

	return coda_s_fmt_vid_cap(file, priv, &f_cap);
}

static int coda_reqbufs(struct file *file, void *priv,
			struct v4l2_requestbuffers *rb)
{
	struct coda_ctx *ctx = fh_to_ctx(priv);
	int ret;

	ret = v4l2_m2m_reqbufs(file, ctx->fh.m2m_ctx, rb);
	if (ret)
		return ret;

	/*
	 * Allow to allocate instance specific per-context buffers, such as
	 * bitstream ringbuffer, slice buffer, work buffer, etc. if needed.
	 */
	if (rb->type == V4L2_BUF_TYPE_VIDEO_OUTPUT && ctx->ops->reqbufs)
		return ctx->ops->reqbufs(ctx, rb);

	return 0;
}

static int coda_qbuf(struct file *file, void *priv,
		     struct v4l2_buffer *buf)
{
	struct coda_ctx *ctx = fh_to_ctx(priv);

	if (ctx->inst_type == CODA_INST_DECODER &&
	    buf->type == V4L2_BUF_TYPE_VIDEO_OUTPUT)
		buf->flags &= ~V4L2_BUF_FLAG_LAST;

	return v4l2_m2m_qbuf(file, ctx->fh.m2m_ctx, buf);
}

static int coda_dqbuf(struct file *file, void *priv, struct v4l2_buffer *buf)
{
	struct coda_ctx *ctx = fh_to_ctx(priv);
	int ret;

	ret = v4l2_m2m_dqbuf(file, ctx->fh.m2m_ctx, buf);

	if (ctx->inst_type == CODA_INST_DECODER &&
	    buf->type == V4L2_BUF_TYPE_VIDEO_OUTPUT)
		buf->flags &= ~V4L2_BUF_FLAG_LAST;

	return ret;
}

void coda_m2m_buf_done(struct coda_ctx *ctx, struct vb2_v4l2_buffer *buf,
		       enum vb2_buffer_state state)
{
	const struct v4l2_event eos_event = {
		.type = V4L2_EVENT_EOS
	};

	if (buf->flags & V4L2_BUF_FLAG_LAST)
		v4l2_event_queue_fh(&ctx->fh, &eos_event);

	v4l2_m2m_buf_done(buf, state);
}

static int coda_g_selection(struct file *file, void *fh,
			    struct v4l2_selection *s)
{
	struct coda_ctx *ctx = fh_to_ctx(fh);
	struct coda_q_data *q_data;
	struct v4l2_rect r, *rsel;

	q_data = get_q_data(ctx, s->type);
	if (!q_data)
		return -EINVAL;

	r.left = 0;
	r.top = 0;
	r.width = q_data->width;
	r.height = q_data->height;
	rsel = &q_data->rect;

	switch (s->target) {
	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		rsel = &r;
		fallthrough;
	case V4L2_SEL_TGT_CROP:
		if (s->type != V4L2_BUF_TYPE_VIDEO_OUTPUT ||
		    ctx->inst_type == CODA_INST_DECODER)
			return -EINVAL;
		break;
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
	case V4L2_SEL_TGT_COMPOSE_PADDED:
		rsel = &r;
		fallthrough;
	case V4L2_SEL_TGT_COMPOSE:
	case V4L2_SEL_TGT_COMPOSE_DEFAULT:
		if (s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE ||
		    ctx->inst_type == CODA_INST_ENCODER)
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	s->r = *rsel;

	return 0;
}

static int coda_s_selection(struct file *file, void *fh,
			    struct v4l2_selection *s)
{
	struct coda_ctx *ctx = fh_to_ctx(fh);
	struct coda_q_data *q_data;

	switch (s->target) {
	case V4L2_SEL_TGT_CROP:
		if (ctx->inst_type == CODA_INST_ENCODER &&
		    s->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
			q_data = get_q_data(ctx, s->type);
			if (!q_data)
				return -EINVAL;

			s->r.left = 0;
			s->r.top = 0;
			s->r.width = clamp(s->r.width, 2U, q_data->width);
			s->r.height = clamp(s->r.height, 2U, q_data->height);

			if (s->flags & V4L2_SEL_FLAG_LE) {
				s->r.width = round_up(s->r.width, 2);
				s->r.height = round_up(s->r.height, 2);
			} else {
				s->r.width = round_down(s->r.width, 2);
				s->r.height = round_down(s->r.height, 2);
			}

			q_data->rect = s->r;

			coda_dbg(1, ctx, "Setting crop rectangle: %dx%d\n",
				 s->r.width, s->r.height);

			return 0;
		}
		fallthrough;
	case V4L2_SEL_TGT_NATIVE_SIZE:
	case V4L2_SEL_TGT_COMPOSE:
		return coda_g_selection(file, fh, s);
	default:
		/* v4l2-compliance expects this to fail for read-only targets */
		return -EINVAL;
	}
}

static int coda_try_encoder_cmd(struct file *file, void *fh,
				struct v4l2_encoder_cmd *ec)
{
	struct coda_ctx *ctx = fh_to_ctx(fh);

	if (ctx->inst_type != CODA_INST_ENCODER)
		return -ENOTTY;

	return v4l2_m2m_ioctl_try_encoder_cmd(file, fh, ec);
}

static void coda_wake_up_capture_queue(struct coda_ctx *ctx)
{
	struct vb2_queue *dst_vq;

	coda_dbg(1, ctx, "waking up capture queue\n");

	dst_vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE);
	dst_vq->last_buffer_dequeued = true;
	wake_up(&dst_vq->done_wq);
}

static int coda_encoder_cmd(struct file *file, void *fh,
			    struct v4l2_encoder_cmd *ec)
{
	struct coda_ctx *ctx = fh_to_ctx(fh);
	struct vb2_v4l2_buffer *buf;
	int ret;

	ret = coda_try_encoder_cmd(file, fh, ec);
	if (ret < 0)
		return ret;

	mutex_lock(&ctx->wakeup_mutex);
	buf = v4l2_m2m_last_src_buf(ctx->fh.m2m_ctx);
	if (buf) {
		/*
		 * If the last output buffer is still on the queue, make sure
		 * that decoder finish_run will see the last flag and report it
		 * to userspace.
		 */
		buf->flags |= V4L2_BUF_FLAG_LAST;
	} else {
		/* Set the stream-end flag on this context */
		ctx->bit_stream_param |= CODA_BIT_STREAM_END_FLAG;

		/*
		 * If the last output buffer has already been taken from the
		 * queue, wake up the capture queue and signal end of stream
		 * via the -EPIPE mechanism.
		 */
		coda_wake_up_capture_queue(ctx);
	}
	mutex_unlock(&ctx->wakeup_mutex);

	return 0;
}

static int coda_try_decoder_cmd(struct file *file, void *fh,
				struct v4l2_decoder_cmd *dc)
{
	struct coda_ctx *ctx = fh_to_ctx(fh);

	if (ctx->inst_type != CODA_INST_DECODER)
		return -ENOTTY;

	return v4l2_m2m_ioctl_try_decoder_cmd(file, fh, dc);
}

static bool coda_mark_last_meta(struct coda_ctx *ctx)
{
	struct coda_buffer_meta *meta;

	coda_dbg(1, ctx, "marking last meta\n");

	spin_lock(&ctx->buffer_meta_lock);
	if (list_empty(&ctx->buffer_meta_list)) {
		spin_unlock(&ctx->buffer_meta_lock);
		return false;
	}

	meta = list_last_entry(&ctx->buffer_meta_list, struct coda_buffer_meta,
			       list);
	meta->last = true;

	spin_unlock(&ctx->buffer_meta_lock);
	return true;
}

static bool coda_mark_last_dst_buf(struct coda_ctx *ctx)
{
	struct vb2_v4l2_buffer *buf;
	struct vb2_buffer *dst_vb;
	struct vb2_queue *dst_vq;
	unsigned long flags;

	coda_dbg(1, ctx, "marking last capture buffer\n");

	dst_vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE);
	spin_lock_irqsave(&dst_vq->done_lock, flags);
	if (list_empty(&dst_vq->done_list)) {
		spin_unlock_irqrestore(&dst_vq->done_lock, flags);
		return false;
	}

	dst_vb = list_last_entry(&dst_vq->done_list, struct vb2_buffer,
				 done_entry);
	buf = to_vb2_v4l2_buffer(dst_vb);
	buf->flags |= V4L2_BUF_FLAG_LAST;

	spin_unlock_irqrestore(&dst_vq->done_lock, flags);
	return true;
}

static int coda_decoder_cmd(struct file *file, void *fh,
			    struct v4l2_decoder_cmd *dc)
{
	struct coda_ctx *ctx = fh_to_ctx(fh);
	struct coda_dev *dev = ctx->dev;
	struct vb2_v4l2_buffer *buf;
	struct vb2_queue *dst_vq;
	bool stream_end;
	bool wakeup;
	int ret;

	ret = coda_try_decoder_cmd(file, fh, dc);
	if (ret < 0)
		return ret;

	switch (dc->cmd) {
	case V4L2_DEC_CMD_START:
		mutex_lock(&dev->coda_mutex);
		mutex_lock(&ctx->bitstream_mutex);
		coda_bitstream_flush(ctx);
		dst_vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx,
					 V4L2_BUF_TYPE_VIDEO_CAPTURE);
		vb2_clear_last_buffer_dequeued(dst_vq);
		ctx->bit_stream_param &= ~CODA_BIT_STREAM_END_FLAG;
		coda_fill_bitstream(ctx, NULL);
		mutex_unlock(&ctx->bitstream_mutex);
		mutex_unlock(&dev->coda_mutex);
		break;
	case V4L2_DEC_CMD_STOP:
		stream_end = false;
		wakeup = false;

		mutex_lock(&ctx->wakeup_mutex);

		buf = v4l2_m2m_last_src_buf(ctx->fh.m2m_ctx);
		if (buf) {
			coda_dbg(1, ctx, "marking last pending buffer\n");

			/* Mark last buffer */
			buf->flags |= V4L2_BUF_FLAG_LAST;

			if (v4l2_m2m_num_src_bufs_ready(ctx->fh.m2m_ctx) == 0) {
				coda_dbg(1, ctx, "all remaining buffers queued\n");
				stream_end = true;
			}
		} else {
			if (ctx->use_bit)
				if (coda_mark_last_meta(ctx))
					stream_end = true;
				else
					wakeup = true;
			else
				if (!coda_mark_last_dst_buf(ctx))
					wakeup = true;
		}

		if (stream_end) {
			coda_dbg(1, ctx, "all remaining buffers queued\n");

			/* Set the stream-end flag on this context */
			coda_bit_stream_end_flag(ctx);
			ctx->hold = false;
			v4l2_m2m_try_schedule(ctx->fh.m2m_ctx);
		}

		if (wakeup) {
			/* If there is no buffer in flight, wake up */
			coda_wake_up_capture_queue(ctx);
		}

		mutex_unlock(&ctx->wakeup_mutex);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int coda_enum_framesizes(struct file *file, void *fh,
				struct v4l2_frmsizeenum *fsize)
{
	struct coda_ctx *ctx = fh_to_ctx(fh);
	struct coda_q_data *q_data_dst;
	const struct coda_codec *codec;

	if (ctx->inst_type != CODA_INST_ENCODER)
		return -ENOTTY;

	if (fsize->index)
		return -EINVAL;

	if (coda_format_normalize_yuv(fsize->pixel_format) ==
	    V4L2_PIX_FMT_YUV420) {
		q_data_dst = get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE);
		codec = coda_find_codec(ctx->dev, fsize->pixel_format,
					q_data_dst->fourcc);
	} else {
		codec = coda_find_codec(ctx->dev, V4L2_PIX_FMT_YUV420,
					fsize->pixel_format);
	}
	if (!codec)
		return -EINVAL;

	fsize->type = V4L2_FRMSIZE_TYPE_CONTINUOUS;
	fsize->stepwise.min_width = MIN_W;
	fsize->stepwise.max_width = codec->max_w;
	fsize->stepwise.step_width = 1;
	fsize->stepwise.min_height = MIN_H;
	fsize->stepwise.max_height = codec->max_h;
	fsize->stepwise.step_height = 1;

	return 0;
}

static int coda_enum_frameintervals(struct file *file, void *fh,
				    struct v4l2_frmivalenum *f)
{
	struct coda_ctx *ctx = fh_to_ctx(fh);
	int i;

	if (f->index)
		return -EINVAL;

	/* Disallow YUYV if the vdoa is not available */
	if (!ctx->vdoa && f->pixel_format == V4L2_PIX_FMT_YUYV)
		return -EINVAL;

	for (i = 0; i < CODA_MAX_FORMATS; i++) {
		if (f->pixel_format == ctx->cvd->src_formats[i] ||
		    f->pixel_format == ctx->cvd->dst_formats[i])
			break;
	}
	if (i == CODA_MAX_FORMATS)
		return -EINVAL;

	f->type = V4L2_FRMIVAL_TYPE_CONTINUOUS;
	f->stepwise.min.numerator = 1;
	f->stepwise.min.denominator = 65535;
	f->stepwise.max.numerator = 65536;
	f->stepwise.max.denominator = 1;
	f->stepwise.step.numerator = 1;
	f->stepwise.step.denominator = 1;

	return 0;
}

static int coda_g_parm(struct file *file, void *fh, struct v4l2_streamparm *a)
{
	struct coda_ctx *ctx = fh_to_ctx(fh);
	struct v4l2_fract *tpf;

	if (a->type != V4L2_BUF_TYPE_VIDEO_OUTPUT)
		return -EINVAL;

	a->parm.output.capability = V4L2_CAP_TIMEPERFRAME;
	tpf = &a->parm.output.timeperframe;
	tpf->denominator = ctx->params.framerate & CODA_FRATE_RES_MASK;
	tpf->numerator = 1 + (ctx->params.framerate >>
			      CODA_FRATE_DIV_OFFSET);

	return 0;
}

/*
 * Approximate timeperframe v4l2_fract with values that can be written
 * into the 16-bit CODA_FRATE_DIV and CODA_FRATE_RES fields.
 */
static void coda_approximate_timeperframe(struct v4l2_fract *timeperframe)
{
	struct v4l2_fract s = *timeperframe;
	struct v4l2_fract f0;
	struct v4l2_fract f1 = { 1, 0 };
	struct v4l2_fract f2 = { 0, 1 };
	unsigned int i, div, s_denominator;

	/* Lower bound is 1/65535 */
	if (s.numerator == 0 || s.denominator / s.numerator > 65535) {
		timeperframe->numerator = 1;
		timeperframe->denominator = 65535;
		return;
	}

	/* Upper bound is 65536/1 */
	if (s.denominator == 0 || s.numerator / s.denominator > 65536) {
		timeperframe->numerator = 65536;
		timeperframe->denominator = 1;
		return;
	}

	/* Reduce fraction to lowest terms */
	div = gcd(s.numerator, s.denominator);
	if (div > 1) {
		s.numerator /= div;
		s.denominator /= div;
	}

	if (s.numerator <= 65536 && s.denominator < 65536) {
		*timeperframe = s;
		return;
	}

	/* Find successive convergents from continued fraction expansion */
	while (f2.numerator <= 65536 && f2.denominator < 65536) {
		f0 = f1;
		f1 = f2;

		/* Stop when f2 exactly equals timeperframe */
		if (s.numerator == 0)
			break;

		i = s.denominator / s.numerator;

		f2.numerator = f0.numerator + i * f1.numerator;
		f2.denominator = f0.denominator + i * f2.denominator;

		s_denominator = s.numerator;
		s.numerator = s.denominator % s.numerator;
		s.denominator = s_denominator;
	}

	*timeperframe = f1;
}

static uint32_t coda_timeperframe_to_frate(struct v4l2_fract *timeperframe)
{
	return ((timeperframe->numerator - 1) << CODA_FRATE_DIV_OFFSET) |
		timeperframe->denominator;
}

static int coda_s_parm(struct file *file, void *fh, struct v4l2_streamparm *a)
{
	struct coda_ctx *ctx = fh_to_ctx(fh);
	struct v4l2_fract *tpf;

	if (a->type != V4L2_BUF_TYPE_VIDEO_OUTPUT)
		return -EINVAL;

	a->parm.output.capability = V4L2_CAP_TIMEPERFRAME;
	tpf = &a->parm.output.timeperframe;
	coda_approximate_timeperframe(tpf);
	ctx->params.framerate = coda_timeperframe_to_frate(tpf);
	ctx->params.framerate_changed = true;

	return 0;
}

static int coda_subscribe_event(struct v4l2_fh *fh,
				const struct v4l2_event_subscription *sub)
{
	struct coda_ctx *ctx = fh_to_ctx(fh);

	switch (sub->type) {
	case V4L2_EVENT_EOS:
		return v4l2_event_subscribe(fh, sub, 0, NULL);
	case V4L2_EVENT_SOURCE_CHANGE:
		if (ctx->inst_type == CODA_INST_DECODER)
			return v4l2_event_subscribe(fh, sub, 0, NULL);
		else
			return -EINVAL;
	default:
		return v4l2_ctrl_subscribe_event(fh, sub);
	}
}

static const struct v4l2_ioctl_ops coda_ioctl_ops = {
	.vidioc_querycap	= coda_querycap,

	.vidioc_enum_fmt_vid_cap = coda_enum_fmt,
	.vidioc_g_fmt_vid_cap	= coda_g_fmt,
	.vidioc_try_fmt_vid_cap	= coda_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap	= coda_s_fmt_vid_cap,

	.vidioc_enum_fmt_vid_out = coda_enum_fmt,
	.vidioc_g_fmt_vid_out	= coda_g_fmt,
	.vidioc_try_fmt_vid_out	= coda_try_fmt_vid_out,
	.vidioc_s_fmt_vid_out	= coda_s_fmt_vid_out,

	.vidioc_reqbufs		= coda_reqbufs,
	.vidioc_querybuf	= v4l2_m2m_ioctl_querybuf,

	.vidioc_qbuf		= coda_qbuf,
	.vidioc_expbuf		= v4l2_m2m_ioctl_expbuf,
	.vidioc_dqbuf		= coda_dqbuf,
	.vidioc_create_bufs	= v4l2_m2m_ioctl_create_bufs,
	.vidioc_prepare_buf	= v4l2_m2m_ioctl_prepare_buf,

	.vidioc_streamon	= v4l2_m2m_ioctl_streamon,
	.vidioc_streamoff	= v4l2_m2m_ioctl_streamoff,

	.vidioc_g_selection	= coda_g_selection,
	.vidioc_s_selection	= coda_s_selection,

	.vidioc_try_encoder_cmd	= coda_try_encoder_cmd,
	.vidioc_encoder_cmd	= coda_encoder_cmd,
	.vidioc_try_decoder_cmd	= coda_try_decoder_cmd,
	.vidioc_decoder_cmd	= coda_decoder_cmd,

	.vidioc_g_parm		= coda_g_parm,
	.vidioc_s_parm		= coda_s_parm,

	.vidioc_enum_framesizes	= coda_enum_framesizes,
	.vidioc_enum_frameintervals = coda_enum_frameintervals,

	.vidioc_subscribe_event = coda_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

/*
 * Mem-to-mem operations.
 */

static void coda_device_run(void *m2m_priv)
{
	struct coda_ctx *ctx = m2m_priv;
	struct coda_dev *dev = ctx->dev;

	queue_work(dev->workqueue, &ctx->pic_run_work);
}

static void coda_pic_run_work(struct work_struct *work)
{
	struct coda_ctx *ctx = container_of(work, struct coda_ctx, pic_run_work);
	struct coda_dev *dev = ctx->dev;
	int ret;

	mutex_lock(&ctx->buffer_mutex);
	mutex_lock(&dev->coda_mutex);

	ret = ctx->ops->prepare_run(ctx);
	if (ret < 0 && ctx->inst_type == CODA_INST_DECODER) {
		mutex_unlock(&dev->coda_mutex);
		mutex_unlock(&ctx->buffer_mutex);
		/* job_finish scheduled by prepare_decode */
		return;
	}

	if (!wait_for_completion_timeout(&ctx->completion,
					 msecs_to_jiffies(1000))) {
		if (ctx->use_bit) {
			dev_err(dev->dev, "CODA PIC_RUN timeout\n");

			ctx->hold = true;

			coda_hw_reset(ctx);
		}

		if (ctx->ops->run_timeout)
			ctx->ops->run_timeout(ctx);
	} else {
		ctx->ops->finish_run(ctx);
	}

	if ((ctx->aborting || (!ctx->streamon_cap && !ctx->streamon_out)) &&
	    ctx->ops->seq_end_work)
		queue_work(dev->workqueue, &ctx->seq_end_work);

	mutex_unlock(&dev->coda_mutex);
	mutex_unlock(&ctx->buffer_mutex);

	v4l2_m2m_job_finish(ctx->dev->m2m_dev, ctx->fh.m2m_ctx);
}

static int coda_job_ready(void *m2m_priv)
{
	struct coda_ctx *ctx = m2m_priv;
	int src_bufs = v4l2_m2m_num_src_bufs_ready(ctx->fh.m2m_ctx);

	/*
	 * For both 'P' and 'key' frame cases 1 picture
	 * and 1 frame are needed. In the decoder case,
	 * the compressed frame can be in the bitstream.
	 */
	if (!src_bufs && ctx->inst_type != CODA_INST_DECODER) {
		coda_dbg(1, ctx, "not ready: not enough vid-out buffers.\n");
		return 0;
	}

	if (!v4l2_m2m_num_dst_bufs_ready(ctx->fh.m2m_ctx)) {
		coda_dbg(1, ctx, "not ready: not enough vid-cap buffers.\n");
		return 0;
	}

	if (ctx->inst_type == CODA_INST_DECODER && ctx->use_bit) {
		bool stream_end = ctx->bit_stream_param &
				  CODA_BIT_STREAM_END_FLAG;
		int num_metas = ctx->num_metas;
		struct coda_buffer_meta *meta;
		unsigned int count;

		count = hweight32(ctx->frm_dis_flg);
		if (ctx->use_vdoa && count >= (ctx->num_internal_frames - 1)) {
			coda_dbg(1, ctx,
				 "not ready: all internal buffers in use: %d/%d (0x%x)",
				 count, ctx->num_internal_frames,
				 ctx->frm_dis_flg);
			return 0;
		}

		if (ctx->hold && !src_bufs) {
			coda_dbg(1, ctx,
				 "not ready: on hold for more buffers.\n");
			return 0;
		}

		if (!stream_end && (num_metas + src_bufs) < 2) {
			coda_dbg(1, ctx,
				 "not ready: need 2 buffers available (queue:%d + bitstream:%d)\n",
				 num_metas, src_bufs);
			return 0;
		}

		meta = list_first_entry(&ctx->buffer_meta_list,
					struct coda_buffer_meta, list);
		if (!coda_bitstream_can_fetch_past(ctx, meta->end) &&
		    !stream_end) {
			coda_dbg(1, ctx,
				 "not ready: not enough bitstream data to read past %u (%u)\n",
				 meta->end, ctx->bitstream_fifo.kfifo.in);
			return 0;
		}
	}

	if (ctx->aborting) {
		coda_dbg(1, ctx, "not ready: aborting\n");
		return 0;
	}

	coda_dbg(2, ctx, "job ready\n");

	return 1;
}

static void coda_job_abort(void *priv)
{
	struct coda_ctx *ctx = priv;

	ctx->aborting = 1;

	coda_dbg(1, ctx, "job abort\n");
}

static const struct v4l2_m2m_ops coda_m2m_ops = {
	.device_run	= coda_device_run,
	.job_ready	= coda_job_ready,
	.job_abort	= coda_job_abort,
};

static void set_default_params(struct coda_ctx *ctx)
{
	unsigned int max_w, max_h, usize, csize;

	ctx->codec = coda_find_codec(ctx->dev, ctx->cvd->src_formats[0],
				     ctx->cvd->dst_formats[0]);
	max_w = min(ctx->codec->max_w, 1920U);
	max_h = min(ctx->codec->max_h, 1088U);
	usize = max_w * max_h * 3 / 2;
	csize = coda_estimate_sizeimage(ctx, usize, max_w, max_h);

	ctx->params.codec_mode = ctx->codec->mode;
	if (ctx->cvd->src_formats[0] == V4L2_PIX_FMT_JPEG)
		ctx->colorspace = V4L2_COLORSPACE_JPEG;
	else
		ctx->colorspace = V4L2_COLORSPACE_REC709;
	ctx->xfer_func = V4L2_XFER_FUNC_DEFAULT;
	ctx->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	ctx->quantization = V4L2_QUANTIZATION_DEFAULT;
	ctx->params.framerate = 30;

	/* Default formats for output and input queues */
	ctx->q_data[V4L2_M2M_SRC].fourcc = ctx->cvd->src_formats[0];
	ctx->q_data[V4L2_M2M_DST].fourcc = ctx->cvd->dst_formats[0];
	ctx->q_data[V4L2_M2M_SRC].width = max_w;
	ctx->q_data[V4L2_M2M_SRC].height = max_h;
	ctx->q_data[V4L2_M2M_DST].width = max_w;
	ctx->q_data[V4L2_M2M_DST].height = max_h;
	if (ctx->codec->src_fourcc == V4L2_PIX_FMT_YUV420) {
		ctx->q_data[V4L2_M2M_SRC].bytesperline = max_w;
		ctx->q_data[V4L2_M2M_SRC].sizeimage = usize;
		ctx->q_data[V4L2_M2M_DST].bytesperline = 0;
		ctx->q_data[V4L2_M2M_DST].sizeimage = csize;
	} else {
		ctx->q_data[V4L2_M2M_SRC].bytesperline = 0;
		ctx->q_data[V4L2_M2M_SRC].sizeimage = csize;
		ctx->q_data[V4L2_M2M_DST].bytesperline = max_w;
		ctx->q_data[V4L2_M2M_DST].sizeimage = usize;
	}
	ctx->q_data[V4L2_M2M_SRC].rect.width = max_w;
	ctx->q_data[V4L2_M2M_SRC].rect.height = max_h;
	ctx->q_data[V4L2_M2M_DST].rect.width = max_w;
	ctx->q_data[V4L2_M2M_DST].rect.height = max_h;

	/*
	 * Since the RBC2AXI logic only supports a single chroma plane,
	 * macroblock tiling only works for to NV12 pixel format.
	 */
	ctx->tiled_map_type = GDI_LINEAR_FRAME_MAP;
}

/*
 * Queue operations
 */
static int coda_queue_setup(struct vb2_queue *vq,
				unsigned int *nbuffers, unsigned int *nplanes,
				unsigned int sizes[], struct device *alloc_devs[])
{
	struct coda_ctx *ctx = vb2_get_drv_priv(vq);
	struct coda_q_data *q_data;
	unsigned int size;

	q_data = get_q_data(ctx, vq->type);
	size = q_data->sizeimage;

	if (*nplanes)
		return sizes[0] < size ? -EINVAL : 0;

	*nplanes = 1;
	sizes[0] = size;

	coda_dbg(1, ctx, "get %d buffer(s) of size %d each.\n", *nbuffers,
		 size);

	return 0;
}

static int coda_buf_prepare(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct coda_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct coda_q_data *q_data;

	q_data = get_q_data(ctx, vb->vb2_queue->type);
	if (V4L2_TYPE_IS_OUTPUT(vb->vb2_queue->type)) {
		if (vbuf->field == V4L2_FIELD_ANY)
			vbuf->field = V4L2_FIELD_NONE;
		if (vbuf->field != V4L2_FIELD_NONE) {
			v4l2_warn(&ctx->dev->v4l2_dev,
				  "%s field isn't supported\n", __func__);
			return -EINVAL;
		}
	}

	if (vb2_plane_size(vb, 0) < q_data->sizeimage) {
		v4l2_warn(&ctx->dev->v4l2_dev,
			  "%s data will not fit into plane (%lu < %lu)\n",
			  __func__, vb2_plane_size(vb, 0),
			  (long)q_data->sizeimage);
		return -EINVAL;
	}

	return 0;
}

static void coda_update_menu_ctrl(struct v4l2_ctrl *ctrl, int value)
{
	if (!ctrl)
		return;

	v4l2_ctrl_lock(ctrl);

	/*
	 * Extend the control range if the parsed stream contains a known but
	 * unsupported value or level.
	 */
	if (value > ctrl->maximum) {
		__v4l2_ctrl_modify_range(ctrl, ctrl->minimum, value,
			ctrl->menu_skip_mask & ~(1 << value),
			ctrl->default_value);
	} else if (value < ctrl->minimum) {
		__v4l2_ctrl_modify_range(ctrl, value, ctrl->maximum,
			ctrl->menu_skip_mask & ~(1 << value),
			ctrl->default_value);
	}

	__v4l2_ctrl_s_ctrl(ctrl, value);

	v4l2_ctrl_unlock(ctrl);
}

void coda_update_profile_level_ctrls(struct coda_ctx *ctx, u8 profile_idc,
				     u8 level_idc)
{
	const char * const *profile_names;
	const char * const *level_names;
	struct v4l2_ctrl *profile_ctrl;
	struct v4l2_ctrl *level_ctrl;
	const char *codec_name;
	u32 profile_cid;
	u32 level_cid;
	int profile;
	int level;

	switch (ctx->codec->src_fourcc) {
	case V4L2_PIX_FMT_H264:
		codec_name = "H264";
		profile_cid = V4L2_CID_MPEG_VIDEO_H264_PROFILE;
		level_cid = V4L2_CID_MPEG_VIDEO_H264_LEVEL;
		profile_ctrl = ctx->h264_profile_ctrl;
		level_ctrl = ctx->h264_level_ctrl;
		profile = coda_h264_profile(profile_idc);
		level = coda_h264_level(level_idc);
		break;
	case V4L2_PIX_FMT_MPEG2:
		codec_name = "MPEG-2";
		profile_cid = V4L2_CID_MPEG_VIDEO_MPEG2_PROFILE;
		level_cid = V4L2_CID_MPEG_VIDEO_MPEG2_LEVEL;
		profile_ctrl = ctx->mpeg2_profile_ctrl;
		level_ctrl = ctx->mpeg2_level_ctrl;
		profile = coda_mpeg2_profile(profile_idc);
		level = coda_mpeg2_level(level_idc);
		break;
	case V4L2_PIX_FMT_MPEG4:
		codec_name = "MPEG-4";
		profile_cid = V4L2_CID_MPEG_VIDEO_MPEG4_PROFILE;
		level_cid = V4L2_CID_MPEG_VIDEO_MPEG4_LEVEL;
		profile_ctrl = ctx->mpeg4_profile_ctrl;
		level_ctrl = ctx->mpeg4_level_ctrl;
		profile = coda_mpeg4_profile(profile_idc);
		level = coda_mpeg4_level(level_idc);
		break;
	default:
		return;
	}

	profile_names = v4l2_ctrl_get_menu(profile_cid);
	level_names = v4l2_ctrl_get_menu(level_cid);

	if (profile < 0) {
		v4l2_warn(&ctx->dev->v4l2_dev, "Invalid %s profile: %u\n",
			  codec_name, profile_idc);
	} else {
		coda_dbg(1, ctx, "Parsed %s profile: %s\n", codec_name,
			 profile_names[profile]);
		coda_update_menu_ctrl(profile_ctrl, profile);
	}

	if (level < 0) {
		v4l2_warn(&ctx->dev->v4l2_dev, "Invalid %s level: %u\n",
			  codec_name, level_idc);
	} else {
		coda_dbg(1, ctx, "Parsed %s level: %s\n", codec_name,
			 level_names[level]);
		coda_update_menu_ctrl(level_ctrl, level);
	}
}

static void coda_queue_source_change_event(struct coda_ctx *ctx)
{
	static const struct v4l2_event source_change_event = {
		.type = V4L2_EVENT_SOURCE_CHANGE,
		.u.src_change.changes = V4L2_EVENT_SRC_CH_RESOLUTION,
	};

	v4l2_event_queue_fh(&ctx->fh, &source_change_event);
}

static void coda_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct coda_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_queue *vq = vb->vb2_queue;
	struct coda_q_data *q_data;

	q_data = get_q_data(ctx, vb->vb2_queue->type);

	/*
	 * In the decoder case, immediately try to copy the buffer into the
	 * bitstream ringbuffer and mark it as ready to be dequeued.
	 */
	if (ctx->bitstream.size && vq->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
		/*
		 * For backwards compatibility, queuing an empty buffer marks
		 * the stream end
		 */
		if (vb2_get_plane_payload(vb, 0) == 0)
			coda_bit_stream_end_flag(ctx);

		if (q_data->fourcc == V4L2_PIX_FMT_H264) {
			/*
			 * Unless already done, try to obtain profile_idc and
			 * level_idc from the SPS header. This allows to decide
			 * whether to enable reordering during sequence
			 * initialization.
			 */
			if (!ctx->params.h264_profile_idc) {
				coda_sps_parse_profile(ctx, vb);
				coda_update_profile_level_ctrls(ctx,
						ctx->params.h264_profile_idc,
						ctx->params.h264_level_idc);
			}
		}

		mutex_lock(&ctx->bitstream_mutex);
		v4l2_m2m_buf_queue(ctx->fh.m2m_ctx, vbuf);
		if (vb2_is_streaming(vb->vb2_queue))
			/* This set buf->sequence = ctx->qsequence++ */
			coda_fill_bitstream(ctx, NULL);
		mutex_unlock(&ctx->bitstream_mutex);

		if (!ctx->initialized) {
			/*
			 * Run sequence initialization in case the queued
			 * buffer contained headers.
			 */
			if (vb2_is_streaming(vb->vb2_queue) &&
			    ctx->ops->seq_init_work) {
				queue_work(ctx->dev->workqueue,
					   &ctx->seq_init_work);
				flush_work(&ctx->seq_init_work);
			}

			if (ctx->initialized)
				coda_queue_source_change_event(ctx);
		}
	} else {
		if ((ctx->inst_type == CODA_INST_ENCODER || !ctx->use_bit) &&
		    vq->type == V4L2_BUF_TYPE_VIDEO_OUTPUT)
			vbuf->sequence = ctx->qsequence++;
		v4l2_m2m_buf_queue(ctx->fh.m2m_ctx, vbuf);
	}
}

int coda_alloc_aux_buf(struct coda_dev *dev, struct coda_aux_buf *buf,
		       size_t size, const char *name, struct dentry *parent)
{
	buf->vaddr = dma_alloc_coherent(dev->dev, size, &buf->paddr,
					GFP_KERNEL);
	if (!buf->vaddr) {
		v4l2_err(&dev->v4l2_dev,
			 "Failed to allocate %s buffer of size %zu\n",
			 name, size);
		return -ENOMEM;
	}

	buf->size = size;

	if (name && parent) {
		buf->blob.data = buf->vaddr;
		buf->blob.size = size;
		buf->dentry = debugfs_create_blob(name, 0444, parent,
						  &buf->blob);
	}

	return 0;
}

void coda_free_aux_buf(struct coda_dev *dev,
		       struct coda_aux_buf *buf)
{
	if (buf->vaddr) {
		dma_free_coherent(dev->dev, buf->size, buf->vaddr, buf->paddr);
		buf->vaddr = NULL;
		buf->size = 0;
		debugfs_remove(buf->dentry);
		buf->dentry = NULL;
	}
}

static int coda_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct coda_ctx *ctx = vb2_get_drv_priv(q);
	struct v4l2_device *v4l2_dev = &ctx->dev->v4l2_dev;
	struct coda_q_data *q_data_src, *q_data_dst;
	struct v4l2_m2m_buffer *m2m_buf, *tmp;
	struct vb2_v4l2_buffer *buf;
	struct list_head list;
	int ret = 0;

	if (count < 1)
		return -EINVAL;

	coda_dbg(1, ctx, "start streaming %s\n", v4l2_type_names[q->type]);

	INIT_LIST_HEAD(&list);

	q_data_src = get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT);
	if (q->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
		if (ctx->inst_type == CODA_INST_DECODER && ctx->use_bit) {
			/* copy the buffers that were queued before streamon */
			mutex_lock(&ctx->bitstream_mutex);
			coda_fill_bitstream(ctx, &list);
			mutex_unlock(&ctx->bitstream_mutex);

			if (ctx->dev->devtype->product != CODA_960 &&
			    coda_get_bitstream_payload(ctx) < 512) {
				v4l2_err(v4l2_dev, "start payload < 512\n");
				ret = -EINVAL;
				goto err;
			}

			if (!ctx->initialized) {
				/* Run sequence initialization */
				if (ctx->ops->seq_init_work) {
					queue_work(ctx->dev->workqueue,
						   &ctx->seq_init_work);
					flush_work(&ctx->seq_init_work);
				}
			}
		}

		/*
		 * Check the first input JPEG buffer to determine chroma
		 * subsampling.
		 */
		if (q_data_src->fourcc == V4L2_PIX_FMT_JPEG) {
			buf = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);
			ret = coda_jpeg_decode_header(ctx, &buf->vb2_buf);
			if (ret < 0) {
				v4l2_err(v4l2_dev,
					 "failed to decode JPEG header: %d\n",
					 ret);
				goto err;
			}

			q_data_dst = get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE);
			q_data_dst->width = round_up(q_data_src->width, 16);
			q_data_dst->height = round_up(q_data_src->height, 16);
			q_data_dst->bytesperline = q_data_dst->width;
			if (ctx->params.jpeg_chroma_subsampling ==
			    V4L2_JPEG_CHROMA_SUBSAMPLING_420) {
				q_data_dst->sizeimage =
						q_data_dst->bytesperline *
						q_data_dst->height * 3 / 2;
				if (q_data_dst->fourcc != V4L2_PIX_FMT_YUV420)
					q_data_dst->fourcc = V4L2_PIX_FMT_NV12;
			} else {
				q_data_dst->sizeimage =
						q_data_dst->bytesperline *
						q_data_dst->height * 2;
				q_data_dst->fourcc = V4L2_PIX_FMT_YUV422P;
			}
			q_data_dst->rect.left = 0;
			q_data_dst->rect.top = 0;
			q_data_dst->rect.width = q_data_src->width;
			q_data_dst->rect.height = q_data_src->height;
		}
		ctx->streamon_out = 1;
	} else {
		ctx->streamon_cap = 1;
	}

	/* Don't start the coda unless both queues are on */
	if (!(ctx->streamon_out && ctx->streamon_cap))
		goto out;

	q_data_dst = get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE);
	if ((q_data_src->rect.width != q_data_dst->width &&
	     round_up(q_data_src->rect.width, 16) != q_data_dst->width) ||
	    (q_data_src->rect.height != q_data_dst->height &&
	     round_up(q_data_src->rect.height, 16) != q_data_dst->height)) {
		v4l2_err(v4l2_dev, "can't convert %dx%d to %dx%d\n",
			 q_data_src->rect.width, q_data_src->rect.height,
			 q_data_dst->width, q_data_dst->height);
		ret = -EINVAL;
		goto err;
	}

	/* Allow BIT decoder device_run with no new buffers queued */
	if (ctx->inst_type == CODA_INST_DECODER && ctx->use_bit)
		v4l2_m2m_set_src_buffered(ctx->fh.m2m_ctx, true);

	ctx->gopcounter = ctx->params.gop_size - 1;

	if (q_data_dst->fourcc == V4L2_PIX_FMT_JPEG)
		ctx->params.gop_size = 1;
	ctx->gopcounter = ctx->params.gop_size - 1;
	/* Only decoders have this control */
	if (ctx->mb_err_cnt_ctrl)
		v4l2_ctrl_s_ctrl(ctx->mb_err_cnt_ctrl, 0);

	ret = ctx->ops->start_streaming(ctx);
	if (ctx->inst_type == CODA_INST_DECODER) {
		if (ret == -EAGAIN)
			goto out;
	}
	if (ret < 0)
		goto err;

out:
	if (q->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
		list_for_each_entry_safe(m2m_buf, tmp, &list, list) {
			list_del(&m2m_buf->list);
			v4l2_m2m_buf_done(&m2m_buf->vb, VB2_BUF_STATE_DONE);
		}
	}
	return 0;

err:
	if (q->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
		list_for_each_entry_safe(m2m_buf, tmp, &list, list) {
			list_del(&m2m_buf->list);
			v4l2_m2m_buf_done(&m2m_buf->vb, VB2_BUF_STATE_QUEUED);
		}
		while ((buf = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx)))
			v4l2_m2m_buf_done(buf, VB2_BUF_STATE_QUEUED);
	} else {
		while ((buf = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx)))
			v4l2_m2m_buf_done(buf, VB2_BUF_STATE_QUEUED);
	}
	return ret;
}

static void coda_stop_streaming(struct vb2_queue *q)
{
	struct coda_ctx *ctx = vb2_get_drv_priv(q);
	struct coda_dev *dev = ctx->dev;
	struct vb2_v4l2_buffer *buf;
	bool stop;

	stop = ctx->streamon_out && ctx->streamon_cap;

	coda_dbg(1, ctx, "stop streaming %s\n", v4l2_type_names[q->type]);

	if (q->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
		ctx->streamon_out = 0;

		coda_bit_stream_end_flag(ctx);

		ctx->qsequence = 0;

		while ((buf = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx)))
			v4l2_m2m_buf_done(buf, VB2_BUF_STATE_ERROR);
	} else {
		ctx->streamon_cap = 0;

		ctx->osequence = 0;
		ctx->sequence_offset = 0;

		while ((buf = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx)))
			v4l2_m2m_buf_done(buf, VB2_BUF_STATE_ERROR);
	}

	if (stop) {
		struct coda_buffer_meta *meta;

		if (ctx->ops->seq_end_work) {
			queue_work(dev->workqueue, &ctx->seq_end_work);
			flush_work(&ctx->seq_end_work);
		}
		spin_lock(&ctx->buffer_meta_lock);
		while (!list_empty(&ctx->buffer_meta_list)) {
			meta = list_first_entry(&ctx->buffer_meta_list,
						struct coda_buffer_meta, list);
			list_del(&meta->list);
			kfree(meta);
		}
		ctx->num_metas = 0;
		spin_unlock(&ctx->buffer_meta_lock);
		kfifo_init(&ctx->bitstream_fifo,
			ctx->bitstream.vaddr, ctx->bitstream.size);
		ctx->runcounter = 0;
		ctx->aborting = 0;
		ctx->hold = false;
	}

	if (!ctx->streamon_out && !ctx->streamon_cap)
		ctx->bit_stream_param &= ~CODA_BIT_STREAM_END_FLAG;
}

static const struct vb2_ops coda_qops = {
	.queue_setup		= coda_queue_setup,
	.buf_prepare		= coda_buf_prepare,
	.buf_queue		= coda_buf_queue,
	.start_streaming	= coda_start_streaming,
	.stop_streaming		= coda_stop_streaming,
	.wait_prepare		= vb2_ops_wait_prepare,
	.wait_finish		= vb2_ops_wait_finish,
};

static int coda_s_ctrl(struct v4l2_ctrl *ctrl)
{
	const char * const *val_names = v4l2_ctrl_get_menu(ctrl->id);
	struct coda_ctx *ctx =
			container_of(ctrl->handler, struct coda_ctx, ctrls);

	if (val_names)
		coda_dbg(2, ctx, "s_ctrl: id = 0x%x, name = \"%s\", val = %d (\"%s\")\n",
			 ctrl->id, ctrl->name, ctrl->val, val_names[ctrl->val]);
	else
		coda_dbg(2, ctx, "s_ctrl: id = 0x%x, name = \"%s\", val = %d\n",
			 ctrl->id, ctrl->name, ctrl->val);

	switch (ctrl->id) {
	case V4L2_CID_HFLIP:
		if (ctrl->val)
			ctx->params.rot_mode |= CODA_MIR_HOR;
		else
			ctx->params.rot_mode &= ~CODA_MIR_HOR;
		break;
	case V4L2_CID_VFLIP:
		if (ctrl->val)
			ctx->params.rot_mode |= CODA_MIR_VER;
		else
			ctx->params.rot_mode &= ~CODA_MIR_VER;
		break;
	case V4L2_CID_MPEG_VIDEO_BITRATE:
		ctx->params.bitrate = ctrl->val / 1000;
		ctx->params.bitrate_changed = true;
		break;
	case V4L2_CID_MPEG_VIDEO_GOP_SIZE:
		ctx->params.gop_size = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_I_FRAME_QP:
		ctx->params.h264_intra_qp = ctrl->val;
		ctx->params.h264_intra_qp_changed = true;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_P_FRAME_QP:
		ctx->params.h264_inter_qp = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_MIN_QP:
		ctx->params.h264_min_qp = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_MAX_QP:
		ctx->params.h264_max_qp = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_LOOP_FILTER_ALPHA:
		ctx->params.h264_slice_alpha_c0_offset_div2 = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_LOOP_FILTER_BETA:
		ctx->params.h264_slice_beta_offset_div2 = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_LOOP_FILTER_MODE:
		ctx->params.h264_disable_deblocking_filter_idc = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_CONSTRAINED_INTRA_PREDICTION:
		ctx->params.h264_constrained_intra_pred_flag = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_FRAME_RC_ENABLE:
		ctx->params.frame_rc_enable = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_MB_RC_ENABLE:
		ctx->params.mb_rc_enable = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_CHROMA_QP_INDEX_OFFSET:
		ctx->params.h264_chroma_qp_index_offset = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_PROFILE:
		/* TODO: switch between baseline and constrained baseline */
		if (ctx->inst_type == CODA_INST_ENCODER)
			ctx->params.h264_profile_idc = 66;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_LEVEL:
		/* nothing to do, this is set by the encoder */
		break;
	case V4L2_CID_MPEG_VIDEO_MPEG4_I_FRAME_QP:
		ctx->params.mpeg4_intra_qp = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_MPEG4_P_FRAME_QP:
		ctx->params.mpeg4_inter_qp = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_MPEG2_PROFILE:
	case V4L2_CID_MPEG_VIDEO_MPEG2_LEVEL:
	case V4L2_CID_MPEG_VIDEO_MPEG4_PROFILE:
	case V4L2_CID_MPEG_VIDEO_MPEG4_LEVEL:
		/* nothing to do, these are fixed */
		break;
	case V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MODE:
		ctx->params.slice_mode = ctrl->val;
		ctx->params.slice_mode_changed = true;
		break;
	case V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MAX_MB:
		ctx->params.slice_max_mb = ctrl->val;
		ctx->params.slice_mode_changed = true;
		break;
	case V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MAX_BYTES:
		ctx->params.slice_max_bits = ctrl->val * 8;
		ctx->params.slice_mode_changed = true;
		break;
	case V4L2_CID_MPEG_VIDEO_HEADER_MODE:
		break;
	case V4L2_CID_MPEG_VIDEO_CYCLIC_INTRA_REFRESH_MB:
		ctx->params.intra_refresh = ctrl->val;
		ctx->params.intra_refresh_changed = true;
		break;
	case V4L2_CID_MPEG_VIDEO_FORCE_KEY_FRAME:
		ctx->params.force_ipicture = true;
		break;
	case V4L2_CID_JPEG_COMPRESSION_QUALITY:
		coda_set_jpeg_compression_quality(ctx, ctrl->val);
		break;
	case V4L2_CID_JPEG_RESTART_INTERVAL:
		ctx->params.jpeg_restart_interval = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_VBV_DELAY:
		ctx->params.vbv_delay = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_VBV_SIZE:
		ctx->params.vbv_size = min(ctrl->val * 8192, 0x7fffffff);
		break;
	default:
		coda_dbg(1, ctx, "Invalid control, id=%d, val=%d\n",
			 ctrl->id, ctrl->val);
		return -EINVAL;
	}

	return 0;
}

static const struct v4l2_ctrl_ops coda_ctrl_ops = {
	.s_ctrl = coda_s_ctrl,
};

static void coda_encode_ctrls(struct coda_ctx *ctx)
{
	int max_gop_size = (ctx->dev->devtype->product == CODA_DX6) ? 60 : 99;

	v4l2_ctrl_new_std(&ctx->ctrls, &coda_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_BITRATE, 0, 32767000, 1000, 0);
	v4l2_ctrl_new_std(&ctx->ctrls, &coda_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_GOP_SIZE, 0, max_gop_size, 1, 16);
	v4l2_ctrl_new_std(&ctx->ctrls, &coda_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_H264_I_FRAME_QP, 0, 51, 1, 25);
	v4l2_ctrl_new_std(&ctx->ctrls, &coda_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_H264_P_FRAME_QP, 0, 51, 1, 25);
	if (ctx->dev->devtype->product != CODA_960) {
		v4l2_ctrl_new_std(&ctx->ctrls, &coda_ctrl_ops,
			V4L2_CID_MPEG_VIDEO_H264_MIN_QP, 0, 51, 1, 12);
	}
	v4l2_ctrl_new_std(&ctx->ctrls, &coda_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_H264_MAX_QP, 0, 51, 1, 51);
	v4l2_ctrl_new_std(&ctx->ctrls, &coda_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_H264_LOOP_FILTER_ALPHA, -6, 6, 1, 0);
	v4l2_ctrl_new_std(&ctx->ctrls, &coda_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_H264_LOOP_FILTER_BETA, -6, 6, 1, 0);
	v4l2_ctrl_new_std_menu(&ctx->ctrls, &coda_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_H264_LOOP_FILTER_MODE,
		V4L2_MPEG_VIDEO_H264_LOOP_FILTER_MODE_DISABLED_AT_SLICE_BOUNDARY,
		0x0, V4L2_MPEG_VIDEO_H264_LOOP_FILTER_MODE_ENABLED);
	v4l2_ctrl_new_std(&ctx->ctrls, &coda_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_H264_CONSTRAINED_INTRA_PREDICTION, 0, 1, 1,
		0);
	v4l2_ctrl_new_std(&ctx->ctrls, &coda_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_FRAME_RC_ENABLE, 0, 1, 1, 1);
	v4l2_ctrl_new_std(&ctx->ctrls, &coda_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_MB_RC_ENABLE, 0, 1, 1, 1);
	v4l2_ctrl_new_std(&ctx->ctrls, &coda_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_H264_CHROMA_QP_INDEX_OFFSET, -12, 12, 1, 0);
	v4l2_ctrl_new_std_menu(&ctx->ctrls, &coda_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_H264_PROFILE,
		V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE, 0x0,
		V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE);
	if (ctx->dev->devtype->product == CODA_HX4 ||
	    ctx->dev->devtype->product == CODA_7541) {
		v4l2_ctrl_new_std_menu(&ctx->ctrls, &coda_ctrl_ops,
			V4L2_CID_MPEG_VIDEO_H264_LEVEL,
			V4L2_MPEG_VIDEO_H264_LEVEL_3_1,
			~((1 << V4L2_MPEG_VIDEO_H264_LEVEL_2_0) |
			  (1 << V4L2_MPEG_VIDEO_H264_LEVEL_3_0) |
			  (1 << V4L2_MPEG_VIDEO_H264_LEVEL_3_1)),
			V4L2_MPEG_VIDEO_H264_LEVEL_3_1);
	}
	if (ctx->dev->devtype->product == CODA_960) {
		v4l2_ctrl_new_std_menu(&ctx->ctrls, &coda_ctrl_ops,
			V4L2_CID_MPEG_VIDEO_H264_LEVEL,
			V4L2_MPEG_VIDEO_H264_LEVEL_4_0,
			~((1 << V4L2_MPEG_VIDEO_H264_LEVEL_2_0) |
			  (1 << V4L2_MPEG_VIDEO_H264_LEVEL_3_0) |
			  (1 << V4L2_MPEG_VIDEO_H264_LEVEL_3_1) |
			  (1 << V4L2_MPEG_VIDEO_H264_LEVEL_3_2) |
			  (1 << V4L2_MPEG_VIDEO_H264_LEVEL_4_0)),
			V4L2_MPEG_VIDEO_H264_LEVEL_4_0);
	}
	v4l2_ctrl_new_std(&ctx->ctrls, &coda_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_MPEG4_I_FRAME_QP, 1, 31, 1, 2);
	v4l2_ctrl_new_std(&ctx->ctrls, &coda_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_MPEG4_P_FRAME_QP, 1, 31, 1, 2);
	v4l2_ctrl_new_std_menu(&ctx->ctrls, &coda_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_MPEG4_PROFILE,
		V4L2_MPEG_VIDEO_MPEG4_PROFILE_SIMPLE, 0x0,
		V4L2_MPEG_VIDEO_MPEG4_PROFILE_SIMPLE);
	if (ctx->dev->devtype->product == CODA_HX4 ||
	    ctx->dev->devtype->product == CODA_7541 ||
	    ctx->dev->devtype->product == CODA_960) {
		v4l2_ctrl_new_std_menu(&ctx->ctrls, &coda_ctrl_ops,
			V4L2_CID_MPEG_VIDEO_MPEG4_LEVEL,
			V4L2_MPEG_VIDEO_MPEG4_LEVEL_5,
			~(1 << V4L2_MPEG_VIDEO_MPEG4_LEVEL_5),
			V4L2_MPEG_VIDEO_MPEG4_LEVEL_5);
	}
	v4l2_ctrl_new_std_menu(&ctx->ctrls, &coda_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MODE,
		V4L2_MPEG_VIDEO_MULTI_SLICE_MODE_MAX_BYTES, 0x0,
		V4L2_MPEG_VIDEO_MULTI_SLICE_MODE_SINGLE);
	v4l2_ctrl_new_std(&ctx->ctrls, &coda_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MAX_MB, 1, 0x3fffffff, 1, 1);
	v4l2_ctrl_new_std(&ctx->ctrls, &coda_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MAX_BYTES, 1, 0x3fffffff, 1,
		500);
	v4l2_ctrl_new_std_menu(&ctx->ctrls, &coda_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_HEADER_MODE,
		V4L2_MPEG_VIDEO_HEADER_MODE_JOINED_WITH_1ST_FRAME,
		(1 << V4L2_MPEG_VIDEO_HEADER_MODE_SEPARATE),
		V4L2_MPEG_VIDEO_HEADER_MODE_JOINED_WITH_1ST_FRAME);
	v4l2_ctrl_new_std(&ctx->ctrls, &coda_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_CYCLIC_INTRA_REFRESH_MB, 0,
		1920 * 1088 / 256, 1, 0);
	v4l2_ctrl_new_std(&ctx->ctrls, &coda_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_VBV_DELAY, 0, 0x7fff, 1, 0);
	/*
	 * The maximum VBV size value is 0x7fffffff bits,
	 * one bit less than 262144 KiB
	 */
	v4l2_ctrl_new_std(&ctx->ctrls, &coda_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_VBV_SIZE, 0, 262144, 1, 0);
}

static void coda_jpeg_encode_ctrls(struct coda_ctx *ctx)
{
	v4l2_ctrl_new_std(&ctx->ctrls, &coda_ctrl_ops,
		V4L2_CID_JPEG_COMPRESSION_QUALITY, 5, 100, 1, 50);
	v4l2_ctrl_new_std(&ctx->ctrls, &coda_ctrl_ops,
		V4L2_CID_JPEG_RESTART_INTERVAL, 0, 100, 1, 0);
}

static void coda_decode_ctrls(struct coda_ctx *ctx)
{
	u8 max;

	ctx->h264_profile_ctrl = v4l2_ctrl_new_std_menu(&ctx->ctrls,
		&coda_ctrl_ops, V4L2_CID_MPEG_VIDEO_H264_PROFILE,
		V4L2_MPEG_VIDEO_H264_PROFILE_HIGH,
		~((1 << V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE) |
		  (1 << V4L2_MPEG_VIDEO_H264_PROFILE_MAIN) |
		  (1 << V4L2_MPEG_VIDEO_H264_PROFILE_HIGH)),
		V4L2_MPEG_VIDEO_H264_PROFILE_HIGH);
	if (ctx->h264_profile_ctrl)
		ctx->h264_profile_ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	if (ctx->dev->devtype->product == CODA_HX4 ||
	    ctx->dev->devtype->product == CODA_7541)
		max = V4L2_MPEG_VIDEO_H264_LEVEL_4_0;
	else if (ctx->dev->devtype->product == CODA_960)
		max = V4L2_MPEG_VIDEO_H264_LEVEL_4_1;
	else
		return;
	ctx->h264_level_ctrl = v4l2_ctrl_new_std_menu(&ctx->ctrls,
		&coda_ctrl_ops, V4L2_CID_MPEG_VIDEO_H264_LEVEL, max, 0, max);
	if (ctx->h264_level_ctrl)
		ctx->h264_level_ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	ctx->mpeg2_profile_ctrl = v4l2_ctrl_new_std_menu(&ctx->ctrls,
		&coda_ctrl_ops, V4L2_CID_MPEG_VIDEO_MPEG2_PROFILE,
		V4L2_MPEG_VIDEO_MPEG2_PROFILE_HIGH, 0,
		V4L2_MPEG_VIDEO_MPEG2_PROFILE_HIGH);
	if (ctx->mpeg2_profile_ctrl)
		ctx->mpeg2_profile_ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	ctx->mpeg2_level_ctrl = v4l2_ctrl_new_std_menu(&ctx->ctrls,
		&coda_ctrl_ops, V4L2_CID_MPEG_VIDEO_MPEG2_LEVEL,
		V4L2_MPEG_VIDEO_MPEG2_LEVEL_HIGH, 0,
		V4L2_MPEG_VIDEO_MPEG2_LEVEL_HIGH);
	if (ctx->mpeg2_level_ctrl)
		ctx->mpeg2_level_ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	ctx->mpeg4_profile_ctrl = v4l2_ctrl_new_std_menu(&ctx->ctrls,
		&coda_ctrl_ops, V4L2_CID_MPEG_VIDEO_MPEG4_PROFILE,
		V4L2_MPEG_VIDEO_MPEG4_PROFILE_ADVANCED_CODING_EFFICIENCY, 0,
		V4L2_MPEG_VIDEO_MPEG4_PROFILE_ADVANCED_CODING_EFFICIENCY);
	if (ctx->mpeg4_profile_ctrl)
		ctx->mpeg4_profile_ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	ctx->mpeg4_level_ctrl = v4l2_ctrl_new_std_menu(&ctx->ctrls,
		&coda_ctrl_ops, V4L2_CID_MPEG_VIDEO_MPEG4_LEVEL,
		V4L2_MPEG_VIDEO_MPEG4_LEVEL_5, 0,
		V4L2_MPEG_VIDEO_MPEG4_LEVEL_5);
	if (ctx->mpeg4_level_ctrl)
		ctx->mpeg4_level_ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;
}

static const struct v4l2_ctrl_config coda_mb_err_cnt_ctrl_config = {
	.id	= V4L2_CID_CODA_MB_ERR_CNT,
	.name	= "Macroblocks Error Count",
	.type	= V4L2_CTRL_TYPE_INTEGER,
	.min	= 0,
	.max	= 0x7fffffff,
	.step	= 1,
};

static int coda_ctrls_setup(struct coda_ctx *ctx)
{
	v4l2_ctrl_handler_init(&ctx->ctrls, 2);

	v4l2_ctrl_new_std(&ctx->ctrls, &coda_ctrl_ops,
		V4L2_CID_HFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(&ctx->ctrls, &coda_ctrl_ops,
		V4L2_CID_VFLIP, 0, 1, 1, 0);
	if (ctx->inst_type == CODA_INST_ENCODER) {
		v4l2_ctrl_new_std(&ctx->ctrls, &coda_ctrl_ops,
				  V4L2_CID_MIN_BUFFERS_FOR_OUTPUT,
				  1, 1, 1, 1);
		if (ctx->cvd->dst_formats[0] == V4L2_PIX_FMT_JPEG)
			coda_jpeg_encode_ctrls(ctx);
		else
			coda_encode_ctrls(ctx);
	} else {
		v4l2_ctrl_new_std(&ctx->ctrls, &coda_ctrl_ops,
				  V4L2_CID_MIN_BUFFERS_FOR_CAPTURE,
				  1, 1, 1, 1);
		if (ctx->cvd->src_formats[0] == V4L2_PIX_FMT_H264)
			coda_decode_ctrls(ctx);

		ctx->mb_err_cnt_ctrl = v4l2_ctrl_new_custom(&ctx->ctrls,
						&coda_mb_err_cnt_ctrl_config,
						NULL);
		if (ctx->mb_err_cnt_ctrl)
			ctx->mb_err_cnt_ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;
	}

	if (ctx->ctrls.error) {
		v4l2_err(&ctx->dev->v4l2_dev,
			"control initialization error (%d)",
			ctx->ctrls.error);
		return -EINVAL;
	}

	return v4l2_ctrl_handler_setup(&ctx->ctrls);
}

static int coda_queue_init(struct coda_ctx *ctx, struct vb2_queue *vq)
{
	vq->drv_priv = ctx;
	vq->ops = &coda_qops;
	vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	vq->lock = &ctx->dev->dev_mutex;
	/* One way to indicate end-of-stream for coda is to set the
	 * bytesused == 0. However by default videobuf2 handles bytesused
	 * equal to 0 as a special case and changes its value to the size
	 * of the buffer. Set the allow_zero_bytesused flag, so
	 * that videobuf2 will keep the value of bytesused intact.
	 */
	vq->allow_zero_bytesused = 1;
	/*
	 * We might be fine with no buffers on some of the queues, but that
	 * would need to be reflected in job_ready(). Currently we expect all
	 * queues to have at least one buffer queued.
	 */
	vq->min_buffers_needed = 1;
	vq->dev = ctx->dev->dev;

	return vb2_queue_init(vq);
}

int coda_encoder_queue_init(void *priv, struct vb2_queue *src_vq,
			    struct vb2_queue *dst_vq)
{
	int ret;

	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	src_vq->io_modes = VB2_DMABUF | VB2_MMAP;
	src_vq->mem_ops = &vb2_dma_contig_memops;

	ret = coda_queue_init(priv, src_vq);
	if (ret)
		return ret;

	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	dst_vq->io_modes = VB2_DMABUF | VB2_MMAP;
	dst_vq->mem_ops = &vb2_dma_contig_memops;

	return coda_queue_init(priv, dst_vq);
}

int coda_decoder_queue_init(void *priv, struct vb2_queue *src_vq,
			    struct vb2_queue *dst_vq)
{
	int ret;

	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	src_vq->io_modes = VB2_DMABUF | VB2_MMAP | VB2_USERPTR;
	src_vq->mem_ops = &vb2_vmalloc_memops;

	ret = coda_queue_init(priv, src_vq);
	if (ret)
		return ret;

	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	dst_vq->io_modes = VB2_DMABUF | VB2_MMAP;
	dst_vq->dma_attrs = DMA_ATTR_NO_KERNEL_MAPPING;
	dst_vq->mem_ops = &vb2_dma_contig_memops;

	return coda_queue_init(priv, dst_vq);
}

/*
 * File operations
 */

static int coda_open(struct file *file)
{
	struct video_device *vdev = video_devdata(file);
	struct coda_dev *dev = video_get_drvdata(vdev);
	struct coda_ctx *ctx;
	unsigned int max = ~0;
	char *name;
	int ret;
	int idx;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	if (dev->devtype->product == CODA_DX6)
		max = CODADX6_MAX_INSTANCES - 1;
	idx = ida_alloc_max(&dev->ida, max, GFP_KERNEL);
	if (idx < 0) {
		ret = idx;
		goto err_coda_max;
	}

	name = kasprintf(GFP_KERNEL, "context%d", idx);
	if (!name) {
		ret = -ENOMEM;
		goto err_coda_name_init;
	}

	ctx->debugfs_entry = debugfs_create_dir(name, dev->debugfs_root);
	kfree(name);

	ctx->cvd = to_coda_video_device(vdev);
	ctx->inst_type = ctx->cvd->type;
	ctx->ops = ctx->cvd->ops;
	ctx->use_bit = !ctx->cvd->direct;
	init_completion(&ctx->completion);
	INIT_WORK(&ctx->pic_run_work, coda_pic_run_work);
	if (ctx->ops->seq_init_work)
		INIT_WORK(&ctx->seq_init_work, ctx->ops->seq_init_work);
	if (ctx->ops->seq_end_work)
		INIT_WORK(&ctx->seq_end_work, ctx->ops->seq_end_work);
	v4l2_fh_init(&ctx->fh, video_devdata(file));
	file->private_data = &ctx->fh;
	v4l2_fh_add(&ctx->fh);
	ctx->dev = dev;
	ctx->idx = idx;

	coda_dbg(1, ctx, "open instance (%p)\n", ctx);

	switch (dev->devtype->product) {
	case CODA_960:
		/*
		 * Enabling the BWB when decoding can hang the firmware with
		 * certain streams. The issue was tracked as ENGR00293425 by
		 * Freescale. As a workaround, disable BWB for all decoders.
		 * The enable_bwb module parameter allows to override this.
		 */
		if (enable_bwb || ctx->inst_type == CODA_INST_ENCODER)
			ctx->frame_mem_ctrl = CODA9_FRAME_ENABLE_BWB;
		fallthrough;
	case CODA_HX4:
	case CODA_7541:
		ctx->reg_idx = 0;
		break;
	default:
		ctx->reg_idx = idx;
	}
	if (ctx->dev->vdoa && !disable_vdoa) {
		ctx->vdoa = vdoa_context_create(dev->vdoa);
		if (!ctx->vdoa)
			v4l2_warn(&dev->v4l2_dev,
				  "Failed to create vdoa context: not using vdoa");
	}
	ctx->use_vdoa = false;

	/* Power up and upload firmware if necessary */
	ret = pm_runtime_resume_and_get(dev->dev);
	if (ret < 0) {
		v4l2_err(&dev->v4l2_dev, "failed to power up: %d\n", ret);
		goto err_pm_get;
	}

	ret = clk_prepare_enable(dev->clk_per);
	if (ret)
		goto err_clk_enable;

	ret = clk_prepare_enable(dev->clk_ahb);
	if (ret)
		goto err_clk_ahb;

	set_default_params(ctx);
	ctx->fh.m2m_ctx = v4l2_m2m_ctx_init(dev->m2m_dev, ctx,
					    ctx->ops->queue_init);
	if (IS_ERR(ctx->fh.m2m_ctx)) {
		ret = PTR_ERR(ctx->fh.m2m_ctx);

		v4l2_err(&dev->v4l2_dev, "%s return error (%d)\n",
			 __func__, ret);
		goto err_ctx_init;
	}

	ret = coda_ctrls_setup(ctx);
	if (ret) {
		v4l2_err(&dev->v4l2_dev, "failed to setup coda controls\n");
		goto err_ctrls_setup;
	}

	ctx->fh.ctrl_handler = &ctx->ctrls;

	mutex_init(&ctx->bitstream_mutex);
	mutex_init(&ctx->buffer_mutex);
	mutex_init(&ctx->wakeup_mutex);
	INIT_LIST_HEAD(&ctx->buffer_meta_list);
	spin_lock_init(&ctx->buffer_meta_lock);

	return 0;

err_ctrls_setup:
	v4l2_m2m_ctx_release(ctx->fh.m2m_ctx);
err_ctx_init:
	clk_disable_unprepare(dev->clk_ahb);
err_clk_ahb:
	clk_disable_unprepare(dev->clk_per);
err_clk_enable:
	pm_runtime_put_sync(dev->dev);
err_pm_get:
	v4l2_fh_del(&ctx->fh);
	v4l2_fh_exit(&ctx->fh);
err_coda_name_init:
	ida_free(&dev->ida, ctx->idx);
err_coda_max:
	kfree(ctx);
	return ret;
}

static int coda_release(struct file *file)
{
	struct coda_dev *dev = video_drvdata(file);
	struct coda_ctx *ctx = fh_to_ctx(file->private_data);

	coda_dbg(1, ctx, "release instance (%p)\n", ctx);

	if (ctx->inst_type == CODA_INST_DECODER && ctx->use_bit)
		coda_bit_stream_end_flag(ctx);

	/* If this instance is running, call .job_abort and wait for it to end */
	v4l2_m2m_ctx_release(ctx->fh.m2m_ctx);

	if (ctx->vdoa)
		vdoa_context_destroy(ctx->vdoa);

	/* In case the instance was not running, we still need to call SEQ_END */
	if (ctx->ops->seq_end_work) {
		queue_work(dev->workqueue, &ctx->seq_end_work);
		flush_work(&ctx->seq_end_work);
	}

	if (ctx->dev->devtype->product == CODA_DX6)
		coda_free_aux_buf(dev, &ctx->workbuf);

	v4l2_ctrl_handler_free(&ctx->ctrls);
	clk_disable_unprepare(dev->clk_ahb);
	clk_disable_unprepare(dev->clk_per);
	pm_runtime_put_sync(dev->dev);
	v4l2_fh_del(&ctx->fh);
	v4l2_fh_exit(&ctx->fh);
	ida_free(&dev->ida, ctx->idx);
	if (ctx->ops->release)
		ctx->ops->release(ctx);
	debugfs_remove_recursive(ctx->debugfs_entry);
	kfree(ctx);

	return 0;
}

static const struct v4l2_file_operations coda_fops = {
	.owner		= THIS_MODULE,
	.open		= coda_open,
	.release	= coda_release,
	.poll		= v4l2_m2m_fop_poll,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= v4l2_m2m_fop_mmap,
};

static int coda_hw_init(struct coda_dev *dev)
{
	u32 data;
	u16 *p;
	int i, ret;

	ret = clk_prepare_enable(dev->clk_per);
	if (ret)
		goto err_clk_per;

	ret = clk_prepare_enable(dev->clk_ahb);
	if (ret)
		goto err_clk_ahb;

	reset_control_reset(dev->rstc);

	/*
	 * Copy the first CODA_ISRAM_SIZE in the internal SRAM.
	 * The 16-bit chars in the code buffer are in memory access
	 * order, re-sort them to CODA order for register download.
	 * Data in this SRAM survives a reboot.
	 */
	p = (u16 *)dev->codebuf.vaddr;
	if (dev->devtype->product == CODA_DX6) {
		for (i = 0; i < (CODA_ISRAM_SIZE / 2); i++)  {
			data = CODA_DOWN_ADDRESS_SET(i) |
				CODA_DOWN_DATA_SET(p[i ^ 1]);
			coda_write(dev, data, CODA_REG_BIT_CODE_DOWN);
		}
	} else {
		for (i = 0; i < (CODA_ISRAM_SIZE / 2); i++) {
			data = CODA_DOWN_ADDRESS_SET(i) |
				CODA_DOWN_DATA_SET(p[round_down(i, 4) +
							3 - (i % 4)]);
			coda_write(dev, data, CODA_REG_BIT_CODE_DOWN);
		}
	}

	/* Clear registers */
	for (i = 0; i < 64; i++)
		coda_write(dev, 0, CODA_REG_BIT_CODE_BUF_ADDR + i * 4);

	/* Tell the BIT where to find everything it needs */
	if (dev->devtype->product == CODA_960 ||
	    dev->devtype->product == CODA_7541 ||
	    dev->devtype->product == CODA_HX4) {
		coda_write(dev, dev->tempbuf.paddr,
				CODA_REG_BIT_TEMP_BUF_ADDR);
		coda_write(dev, 0, CODA_REG_BIT_BIT_STREAM_PARAM);
	} else {
		coda_write(dev, dev->workbuf.paddr,
			      CODA_REG_BIT_WORK_BUF_ADDR);
	}
	coda_write(dev, dev->codebuf.paddr,
		      CODA_REG_BIT_CODE_BUF_ADDR);
	coda_write(dev, 0, CODA_REG_BIT_CODE_RUN);

	/* Set default values */
	switch (dev->devtype->product) {
	case CODA_DX6:
		coda_write(dev, CODADX6_STREAM_BUF_PIC_FLUSH,
			   CODA_REG_BIT_STREAM_CTRL);
		break;
	default:
		coda_write(dev, CODA7_STREAM_BUF_PIC_FLUSH,
			   CODA_REG_BIT_STREAM_CTRL);
	}
	if (dev->devtype->product == CODA_960)
		coda_write(dev, CODA9_FRAME_ENABLE_BWB,
				CODA_REG_BIT_FRAME_MEM_CTRL);
	else
		coda_write(dev, 0, CODA_REG_BIT_FRAME_MEM_CTRL);

	if (dev->devtype->product != CODA_DX6)
		coda_write(dev, 0, CODA7_REG_BIT_AXI_SRAM_USE);

	coda_write(dev, CODA_INT_INTERRUPT_ENABLE,
		      CODA_REG_BIT_INT_ENABLE);

	/* Reset VPU and start processor */
	data = coda_read(dev, CODA_REG_BIT_CODE_RESET);
	data |= CODA_REG_RESET_ENABLE;
	coda_write(dev, data, CODA_REG_BIT_CODE_RESET);
	udelay(10);
	data &= ~CODA_REG_RESET_ENABLE;
	coda_write(dev, data, CODA_REG_BIT_CODE_RESET);
	coda_write(dev, CODA_REG_RUN_ENABLE, CODA_REG_BIT_CODE_RUN);

	clk_disable_unprepare(dev->clk_ahb);
	clk_disable_unprepare(dev->clk_per);

	return 0;

err_clk_ahb:
	clk_disable_unprepare(dev->clk_per);
err_clk_per:
	return ret;
}

static int coda_register_device(struct coda_dev *dev, int i)
{
	struct video_device *vfd = &dev->vfd[i];
	const char *name;
	int ret;

	if (i >= dev->devtype->num_vdevs)
		return -EINVAL;
	name = dev->devtype->vdevs[i]->name;

	strscpy(vfd->name, dev->devtype->vdevs[i]->name, sizeof(vfd->name));
	vfd->fops	= &coda_fops;
	vfd->ioctl_ops	= &coda_ioctl_ops;
	vfd->release	= video_device_release_empty;
	vfd->lock	= &dev->dev_mutex;
	vfd->v4l2_dev	= &dev->v4l2_dev;
	vfd->vfl_dir	= VFL_DIR_M2M;
	vfd->device_caps = V4L2_CAP_VIDEO_M2M | V4L2_CAP_STREAMING;
	video_set_drvdata(vfd, dev);

	/* Not applicable, use the selection API instead */
	v4l2_disable_ioctl(vfd, VIDIOC_CROPCAP);
	v4l2_disable_ioctl(vfd, VIDIOC_G_CROP);
	v4l2_disable_ioctl(vfd, VIDIOC_S_CROP);

	ret = video_register_device(vfd, VFL_TYPE_VIDEO, 0);
	if (!ret)
		v4l2_info(&dev->v4l2_dev, "%s registered as %s\n",
			  name, video_device_node_name(vfd));
	return ret;
}

static void coda_copy_firmware(struct coda_dev *dev, const u8 * const buf,
			       size_t size)
{
	u32 *src = (u32 *)buf;

	/* Check if the firmware has a 16-byte Freescale header, skip it */
	if (buf[0] == 'M' && buf[1] == 'X')
		src += 4;
	/*
	 * Check whether the firmware is in native order or pre-reordered for
	 * memory access. The first instruction opcode always is 0xe40e.
	 */
	if (__le16_to_cpup((__le16 *)src) == 0xe40e) {
		u32 *dst = dev->codebuf.vaddr;
		int i;

		/* Firmware in native order, reorder while copying */
		if (dev->devtype->product == CODA_DX6) {
			for (i = 0; i < (size - 16) / 4; i++)
				dst[i] = (src[i] << 16) | (src[i] >> 16);
		} else {
			for (i = 0; i < (size - 16) / 4; i += 2) {
				dst[i] = (src[i + 1] << 16) | (src[i + 1] >> 16);
				dst[i + 1] = (src[i] << 16) | (src[i] >> 16);
			}
		}
	} else {
		/* Copy the already reordered firmware image */
		memcpy(dev->codebuf.vaddr, src, size);
	}
}

static void coda_fw_callback(const struct firmware *fw, void *context);

static int coda_firmware_request(struct coda_dev *dev)
{
	char *fw;

	if (dev->firmware >= ARRAY_SIZE(dev->devtype->firmware))
		return -EINVAL;

	fw = dev->devtype->firmware[dev->firmware];

	dev_dbg(dev->dev, "requesting firmware '%s' for %s\n", fw,
		coda_product_name(dev->devtype->product));

	return request_firmware_nowait(THIS_MODULE, true, fw, dev->dev,
				       GFP_KERNEL, dev, coda_fw_callback);
}

static void coda_fw_callback(const struct firmware *fw, void *context)
{
	struct coda_dev *dev = context;
	int i, ret;

	if (!fw) {
		dev->firmware++;
		ret = coda_firmware_request(dev);
		if (ret < 0) {
			v4l2_err(&dev->v4l2_dev, "firmware request failed\n");
			goto put_pm;
		}
		return;
	}
	if (dev->firmware > 0) {
		/*
		 * Since we can't suppress warnings for failed asynchronous
		 * firmware requests, report that the fallback firmware was
		 * found.
		 */
		dev_info(dev->dev, "Using fallback firmware %s\n",
			 dev->devtype->firmware[dev->firmware]);
	}

	/* allocate auxiliary per-device code buffer for the BIT processor */
	ret = coda_alloc_aux_buf(dev, &dev->codebuf, fw->size, "codebuf",
				 dev->debugfs_root);
	if (ret < 0)
		goto put_pm;

	coda_copy_firmware(dev, fw->data, fw->size);
	release_firmware(fw);

	ret = coda_hw_init(dev);
	if (ret < 0) {
		v4l2_err(&dev->v4l2_dev, "HW initialization failed\n");
		goto put_pm;
	}

	ret = coda_check_firmware(dev);
	if (ret < 0)
		goto put_pm;

	dev->m2m_dev = v4l2_m2m_init(&coda_m2m_ops);
	if (IS_ERR(dev->m2m_dev)) {
		v4l2_err(&dev->v4l2_dev, "Failed to init mem2mem device\n");
		goto put_pm;
	}

	for (i = 0; i < dev->devtype->num_vdevs; i++) {
		ret = coda_register_device(dev, i);
		if (ret) {
			v4l2_err(&dev->v4l2_dev,
				 "Failed to register %s video device: %d\n",
				 dev->devtype->vdevs[i]->name, ret);
			goto rel_vfd;
		}
	}

	pm_runtime_put_sync(dev->dev);
	return;

rel_vfd:
	while (--i >= 0)
		video_unregister_device(&dev->vfd[i]);
	v4l2_m2m_release(dev->m2m_dev);
put_pm:
	pm_runtime_put_sync(dev->dev);
}

enum coda_platform {
	CODA_IMX27,
	CODA_IMX51,
	CODA_IMX53,
	CODA_IMX6Q,
	CODA_IMX6DL,
};

static const struct coda_devtype coda_devdata[] = {
	[CODA_IMX27] = {
		.firmware     = {
			"vpu_fw_imx27_TO2.bin",
			"vpu/vpu_fw_imx27_TO2.bin",
			"v4l-codadx6-imx27.bin"
		},
		.product      = CODA_DX6,
		.codecs       = codadx6_codecs,
		.num_codecs   = ARRAY_SIZE(codadx6_codecs),
		.vdevs        = codadx6_video_devices,
		.num_vdevs    = ARRAY_SIZE(codadx6_video_devices),
		.workbuf_size = 288 * 1024 + FMO_SLICE_SAVE_BUF_SIZE * 8 * 1024,
		.iram_size    = 0xb000,
	},
	[CODA_IMX51] = {
		.firmware     = {
			"vpu_fw_imx51.bin",
			"vpu/vpu_fw_imx51.bin",
			"v4l-codahx4-imx51.bin"
		},
		.product      = CODA_HX4,
		.codecs       = codahx4_codecs,
		.num_codecs   = ARRAY_SIZE(codahx4_codecs),
		.vdevs        = codahx4_video_devices,
		.num_vdevs    = ARRAY_SIZE(codahx4_video_devices),
		.workbuf_size = 128 * 1024,
		.tempbuf_size = 304 * 1024,
		.iram_size    = 0x14000,
	},
	[CODA_IMX53] = {
		.firmware     = {
			"vpu_fw_imx53.bin",
			"vpu/vpu_fw_imx53.bin",
			"v4l-coda7541-imx53.bin"
		},
		.product      = CODA_7541,
		.codecs       = coda7_codecs,
		.num_codecs   = ARRAY_SIZE(coda7_codecs),
		.vdevs        = coda7_video_devices,
		.num_vdevs    = ARRAY_SIZE(coda7_video_devices),
		.workbuf_size = 128 * 1024,
		.tempbuf_size = 304 * 1024,
		.iram_size    = 0x14000,
	},
	[CODA_IMX6Q] = {
		.firmware     = {
			"vpu_fw_imx6q.bin",
			"vpu/vpu_fw_imx6q.bin",
			"v4l-coda960-imx6q.bin"
		},
		.product      = CODA_960,
		.codecs       = coda9_codecs,
		.num_codecs   = ARRAY_SIZE(coda9_codecs),
		.vdevs        = coda9_video_devices,
		.num_vdevs    = ARRAY_SIZE(coda9_video_devices),
		.workbuf_size = 80 * 1024,
		.tempbuf_size = 204 * 1024,
		.iram_size    = 0x21000,
	},
	[CODA_IMX6DL] = {
		.firmware     = {
			"vpu_fw_imx6d.bin",
			"vpu/vpu_fw_imx6d.bin",
			"v4l-coda960-imx6dl.bin"
		},
		.product      = CODA_960,
		.codecs       = coda9_codecs,
		.num_codecs   = ARRAY_SIZE(coda9_codecs),
		.vdevs        = coda9_video_devices,
		.num_vdevs    = ARRAY_SIZE(coda9_video_devices),
		.workbuf_size = 80 * 1024,
		.tempbuf_size = 204 * 1024,
		.iram_size    = 0x1f000, /* leave 4k for suspend code */
	},
};

static const struct of_device_id coda_dt_ids[] = {
	{ .compatible = "fsl,imx27-vpu", .data = &coda_devdata[CODA_IMX27] },
	{ .compatible = "fsl,imx51-vpu", .data = &coda_devdata[CODA_IMX51] },
	{ .compatible = "fsl,imx53-vpu", .data = &coda_devdata[CODA_IMX53] },
	{ .compatible = "fsl,imx6q-vpu", .data = &coda_devdata[CODA_IMX6Q] },
	{ .compatible = "fsl,imx6dl-vpu", .data = &coda_devdata[CODA_IMX6DL] },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, coda_dt_ids);

static int coda_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct gen_pool *pool;
	struct coda_dev *dev;
	int ret, irq;

	dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	dev->devtype = of_device_get_match_data(&pdev->dev);

	dev->dev = &pdev->dev;
	dev->clk_per = devm_clk_get(&pdev->dev, "per");
	if (IS_ERR(dev->clk_per)) {
		dev_err(&pdev->dev, "Could not get per clock\n");
		return PTR_ERR(dev->clk_per);
	}

	dev->clk_ahb = devm_clk_get(&pdev->dev, "ahb");
	if (IS_ERR(dev->clk_ahb)) {
		dev_err(&pdev->dev, "Could not get ahb clock\n");
		return PTR_ERR(dev->clk_ahb);
	}

	/* Get  memory for physical registers */
	dev->regs_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(dev->regs_base))
		return PTR_ERR(dev->regs_base);

	/* IRQ */
	irq = platform_get_irq_byname(pdev, "bit");
	if (irq < 0)
		irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = devm_request_irq(&pdev->dev, irq, coda_irq_handler, 0,
			       CODA_NAME "-video", dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to request irq: %d\n", ret);
		return ret;
	}

	/* JPEG IRQ */
	if (dev->devtype->product == CODA_960) {
		irq = platform_get_irq_byname(pdev, "jpeg");
		if (irq < 0)
			return irq;

		ret = devm_request_threaded_irq(&pdev->dev, irq, NULL,
						coda9_jpeg_irq_handler,
						IRQF_ONESHOT, CODA_NAME "-jpeg",
						dev);
		if (ret < 0) {
			dev_err(&pdev->dev, "failed to request jpeg irq\n");
			return ret;
		}
	}

	dev->rstc = devm_reset_control_get_optional_exclusive(&pdev->dev,
							      NULL);
	if (IS_ERR(dev->rstc)) {
		ret = PTR_ERR(dev->rstc);
		dev_err(&pdev->dev, "failed get reset control: %d\n", ret);
		return ret;
	}

	/* Get IRAM pool from device tree */
	pool = of_gen_pool_get(np, "iram", 0);
	if (!pool) {
		dev_err(&pdev->dev, "iram pool not available\n");
		return -ENOMEM;
	}
	dev->iram_pool = pool;

	/* Get vdoa_data if supported by the platform */
	dev->vdoa = coda_get_vdoa_data();
	if (PTR_ERR(dev->vdoa) == -EPROBE_DEFER)
		return -EPROBE_DEFER;

	ret = v4l2_device_register(&pdev->dev, &dev->v4l2_dev);
	if (ret)
		return ret;

	ratelimit_default_init(&dev->mb_err_rs);
	mutex_init(&dev->dev_mutex);
	mutex_init(&dev->coda_mutex);
	ida_init(&dev->ida);

	dev->debugfs_root = debugfs_create_dir("coda", NULL);

	/* allocate auxiliary per-device buffers for the BIT processor */
	if (dev->devtype->product == CODA_DX6) {
		ret = coda_alloc_aux_buf(dev, &dev->workbuf,
					 dev->devtype->workbuf_size, "workbuf",
					 dev->debugfs_root);
		if (ret < 0)
			goto err_v4l2_register;
	}

	if (dev->devtype->tempbuf_size) {
		ret = coda_alloc_aux_buf(dev, &dev->tempbuf,
					 dev->devtype->tempbuf_size, "tempbuf",
					 dev->debugfs_root);
		if (ret < 0)
			goto err_v4l2_register;
	}

	dev->iram.size = dev->devtype->iram_size;
	dev->iram.vaddr = gen_pool_dma_alloc(dev->iram_pool, dev->iram.size,
					     &dev->iram.paddr);
	if (!dev->iram.vaddr) {
		dev_warn(&pdev->dev, "unable to alloc iram\n");
	} else {
		memset(dev->iram.vaddr, 0, dev->iram.size);
		dev->iram.blob.data = dev->iram.vaddr;
		dev->iram.blob.size = dev->iram.size;
		dev->iram.dentry = debugfs_create_blob("iram", 0444,
						       dev->debugfs_root,
						       &dev->iram.blob);
	}

	dev->workqueue = alloc_workqueue("coda", WQ_UNBOUND | WQ_MEM_RECLAIM, 1);
	if (!dev->workqueue) {
		dev_err(&pdev->dev, "unable to alloc workqueue\n");
		ret = -ENOMEM;
		goto err_v4l2_register;
	}

	platform_set_drvdata(pdev, dev);

	/*
	 * Start activated so we can directly call coda_hw_init in
	 * coda_fw_callback regardless of whether CONFIG_PM is
	 * enabled or whether the device is associated with a PM domain.
	 */
	pm_runtime_get_noresume(&pdev->dev);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	ret = coda_firmware_request(dev);
	if (ret)
		goto err_alloc_workqueue;
	return 0;

err_alloc_workqueue:
	pm_runtime_disable(&pdev->dev);
	pm_runtime_put_noidle(&pdev->dev);
	destroy_workqueue(dev->workqueue);
err_v4l2_register:
	v4l2_device_unregister(&dev->v4l2_dev);
	return ret;
}

static int coda_remove(struct platform_device *pdev)
{
	struct coda_dev *dev = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < ARRAY_SIZE(dev->vfd); i++) {
		if (video_get_drvdata(&dev->vfd[i]))
			video_unregister_device(&dev->vfd[i]);
	}
	if (dev->m2m_dev)
		v4l2_m2m_release(dev->m2m_dev);
	pm_runtime_disable(&pdev->dev);
	v4l2_device_unregister(&dev->v4l2_dev);
	destroy_workqueue(dev->workqueue);
	if (dev->iram.vaddr)
		gen_pool_free(dev->iram_pool, (unsigned long)dev->iram.vaddr,
			      dev->iram.size);
	coda_free_aux_buf(dev, &dev->codebuf);
	coda_free_aux_buf(dev, &dev->tempbuf);
	coda_free_aux_buf(dev, &dev->workbuf);
	debugfs_remove_recursive(dev->debugfs_root);
	ida_destroy(&dev->ida);
	return 0;
}

#ifdef CONFIG_PM
static int coda_runtime_resume(struct device *dev)
{
	struct coda_dev *cdev = dev_get_drvdata(dev);
	int ret = 0;

	if (dev->pm_domain && cdev->codebuf.vaddr) {
		ret = coda_hw_init(cdev);
		if (ret)
			v4l2_err(&cdev->v4l2_dev, "HW initialization failed\n");
	}

	return ret;
}
#endif

static const struct dev_pm_ops coda_pm_ops = {
	SET_RUNTIME_PM_OPS(NULL, coda_runtime_resume, NULL)
};

static struct platform_driver coda_driver = {
	.probe	= coda_probe,
	.remove	= coda_remove,
	.driver	= {
		.name	= CODA_NAME,
		.of_match_table = coda_dt_ids,
		.pm	= &coda_pm_ops,
	},
};

module_platform_driver(coda_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Javier Martin <javier.martin@vista-silicon.com>");
MODULE_DESCRIPTION("Coda multi-standard codec V4L2 driver");
