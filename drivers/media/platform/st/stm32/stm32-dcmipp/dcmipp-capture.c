// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for STM32 Digital Camera Memory Interface Pixel Processor
 *
 * Copyright (C) STMicroelectronics SA 2023
 * Authors: Hugues Fruchet <hugues.fruchet@foss.st.com>
 *          Alain Volmat <alain.volmat@foss.st.com>
 *          for STMicroelectronics.
 */

#include <linux/iopoll.h>
#include <linux/pm_runtime.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mc.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-dma-contig.h>

#include "dcmipp-common.h"

#define DCMIPP_PRSR		0x1f8
#define DCMIPP_CMIER		0x3f0
#define DCMIPP_CMIER_P0FRAMEIE	BIT(9)
#define DCMIPP_CMIER_P0VSYNCIE	BIT(10)
#define DCMIPP_CMIER_P0OVRIE	BIT(15)
#define DCMIPP_CMIER_P0ALL	(DCMIPP_CMIER_P0VSYNCIE |\
				 DCMIPP_CMIER_P0FRAMEIE |\
				 DCMIPP_CMIER_P0OVRIE)
#define DCMIPP_CMIER_P1FRAMEIE	BIT(17)
#define DCMIPP_CMIER_P1VSYNCIE	BIT(18)
#define DCMIPP_CMIER_P1OVRIE	BIT(23)
#define DCMIPP_CMIER_P1ALL	(DCMIPP_CMIER_P1VSYNCIE |\
				 DCMIPP_CMIER_P1FRAMEIE |\
				 DCMIPP_CMIER_P1OVRIE)
#define DCMIPP_CMIER_P2FRAMEIE	BIT(25)
#define DCMIPP_CMIER_P2VSYNCIE	BIT(26)
#define DCMIPP_CMIER_P2OVRIE	BIT(31)
#define DCMIPP_CMIER_P2ALL	(DCMIPP_CMIER_P2VSYNCIE |\
				 DCMIPP_CMIER_P2FRAMEIE |\
				 DCMIPP_CMIER_P2OVRIE)
#define DCMIPP_CMIER_PxALL(id)	(((id) == 0) ? DCMIPP_CMIER_P0ALL :	\
				 (((id) == 1) ? DCMIPP_CMIER_P1ALL :	\
						DCMIPP_CMIER_P2ALL))
#define DCMIPP_CMSR1		0x3f4
#define DCMIPP_CMSR2		0x3f8
#define DCMIPP_CMSR2_P0FRAMEF	BIT(9)
#define DCMIPP_CMSR2_P0VSYNCF	BIT(10)
#define DCMIPP_CMSR2_P0OVRF	BIT(15)
#define DCMIPP_CMSR2_P1FRAMEF	BIT(17)
#define DCMIPP_CMSR2_P1VSYNCF	BIT(18)
#define DCMIPP_CMSR2_P1OVRF	BIT(23)
#define DCMIPP_CMSR2_P2FRAMEF	BIT(25)
#define DCMIPP_CMSR2_P2VSYNCF	BIT(26)
#define DCMIPP_CMSR2_P2OVRF	BIT(31)
#define DCMIPP_CMSR2_PxFRAMEF(id)	(((id) == 0) ? DCMIPP_CMSR2_P0FRAMEF :\
					 (((id) == 1) ? DCMIPP_CMSR2_P1FRAMEF :\
						       DCMIPP_CMSR2_P2FRAMEF))
#define DCMIPP_CMSR2_PxVSYNCF(id)	(((id) == 0) ? DCMIPP_CMSR2_P0VSYNCF :\
					 (((id) == 1) ? DCMIPP_CMSR2_P1VSYNCF :\
						       DCMIPP_CMSR2_P2VSYNCF))
#define DCMIPP_CMSR2_PxOVRF(id)	(((id) == 0) ? DCMIPP_CMSR2_P0OVRF :\
				 (((id) == 1) ? DCMIPP_CMSR2_P1OVRF :\
					       DCMIPP_CMSR2_P2OVRF))
#define DCMIPP_CMFCR		0x3fc
#define DCMIPP_PxFSCR(id)	(0x404 + ((id) * 0x400))
#define DCMIPP_PxFSCR_PIPEN	BIT(31)
#define DCMIPP_PxFCTCR(id)	(0x500 + ((id) * 0x400))
#define DCMIPP_PxFCTCR_CPTREQ	BIT(3)
#define DCMIPP_P0DCCNTR		0x5b0
#define DCMIPP_P0DCLMTR		0x5b4
#define DCMIPP_P0DCLMTR_ENABLE	BIT(31)
#define DCMIPP_P0DCLMTR_LIMIT_MASK	GENMASK(23, 0)

#define DCMIPP_PxPPM0AR1(id)	(0x5c4 + ((id) * 0x400))
#define DCMIPP_PxPPM0PR(id)	(0x9cc + (((id) - 1) * 0x400))
#define DCMIPP_P1PPM1AR1	0x9d4
#define DCMIPP_P1PPM1PR		0x9dc
#define DCMIPP_P1PPM2AR1	0x9e4

#define DCMIPP_PxSR(id)		(0x5f8 + ((id) * 0x400))
#define DCMIPP_PxSR_CPTACT	BIT(23)

#define DCMIPP_PxPPCR(id)	(0x9c0 + (((id) - 1) * 0x400))
#define DCMIPP_PxPPCR_FORMAT_RGB888	0x0
#define DCMIPP_PxPPCR_FORMAT_RGB565	0x1
#define DCMIPP_PxPPCR_FORMAT_ARGB8888	0x2
#define DCMIPP_PxPPCR_FORMAT_RGBA8888	0x3
#define DCMIPP_PxPPCR_FORMAT_Y8		0x4
#define DCMIPP_PxPPCR_FORMAT_YUV444	0x5
#define DCMIPP_PxPPCR_FORMAT_YUYV	0x6
#define DCMIPP_P1PPCR_FORMAT_NV61	0x7
#define DCMIPP_P1PPCR_FORMAT_NV21	0x8
#define DCMIPP_P1PPCR_FORMAT_YV12	0x9
#define DCMIPP_PxPPCR_FORMAT_UYVY	0xa

#define DCMIPP_PxPPCR_SWAPRB		BIT(4)

struct dcmipp_capture_pix_map {
	unsigned int code;
	u32 pixelformat;
	u32 plane_nb;
	unsigned int ppcr_fmt;
	unsigned int swap_uv;
};

#define PIXMAP_MBUS_PFMT(mbus, fmt)			\
	{						\
		.code = MEDIA_BUS_FMT_##mbus,		\
		.pixelformat = V4L2_PIX_FMT_##fmt	\
	}

static const struct dcmipp_capture_pix_map dcmipp_capture_dump_pix_map_list[] = {
	PIXMAP_MBUS_PFMT(RGB565_2X8_LE, RGB565),
	PIXMAP_MBUS_PFMT(RGB565_1X16, RGB565),
	PIXMAP_MBUS_PFMT(RGB888_1X24, RGB24),
	PIXMAP_MBUS_PFMT(YUYV8_2X8, YUYV),
	PIXMAP_MBUS_PFMT(YUYV8_1X16, YUYV),
	PIXMAP_MBUS_PFMT(YVYU8_2X8, YVYU),
	PIXMAP_MBUS_PFMT(YVYU8_1X16, YVYU),
	PIXMAP_MBUS_PFMT(UYVY8_2X8, UYVY),
	PIXMAP_MBUS_PFMT(UYVY8_1X16, UYVY),
	PIXMAP_MBUS_PFMT(VYUY8_2X8, VYUY),
	PIXMAP_MBUS_PFMT(VYUY8_1X16, VYUY),
	PIXMAP_MBUS_PFMT(Y8_1X8, GREY),
	PIXMAP_MBUS_PFMT(Y10_1X10, Y10),
	PIXMAP_MBUS_PFMT(Y12_1X12, Y12),
	PIXMAP_MBUS_PFMT(Y14_1X14, Y14),
	PIXMAP_MBUS_PFMT(SBGGR8_1X8, SBGGR8),
	PIXMAP_MBUS_PFMT(SGBRG8_1X8, SGBRG8),
	PIXMAP_MBUS_PFMT(SGRBG8_1X8, SGRBG8),
	PIXMAP_MBUS_PFMT(SRGGB8_1X8, SRGGB8),
	PIXMAP_MBUS_PFMT(SBGGR10_1X10, SBGGR10),
	PIXMAP_MBUS_PFMT(SGBRG10_1X10, SGBRG10),
	PIXMAP_MBUS_PFMT(SGRBG10_1X10, SGRBG10),
	PIXMAP_MBUS_PFMT(SRGGB10_1X10, SRGGB10),
	PIXMAP_MBUS_PFMT(SBGGR12_1X12, SBGGR12),
	PIXMAP_MBUS_PFMT(SGBRG12_1X12, SGBRG12),
	PIXMAP_MBUS_PFMT(SGRBG12_1X12, SGRBG12),
	PIXMAP_MBUS_PFMT(SRGGB12_1X12, SRGGB12),
	PIXMAP_MBUS_PFMT(SBGGR14_1X14, SBGGR14),
	PIXMAP_MBUS_PFMT(SGBRG14_1X14, SGBRG14),
	PIXMAP_MBUS_PFMT(SGRBG14_1X14, SGRBG14),
	PIXMAP_MBUS_PFMT(SRGGB14_1X14, SRGGB14),
	PIXMAP_MBUS_PFMT(JPEG_1X8, JPEG),
};

#define PIXMAP_MBUS_PIXEL_PFMT(mbus, fmt, nb_plane, pp_code, swap)		\
	{						\
		.code = MEDIA_BUS_FMT_##mbus,		\
		.pixelformat = V4L2_PIX_FMT_##fmt,	\
		.plane_nb = nb_plane,			\
		.ppcr_fmt = pp_code,			\
		.swap_uv = swap,			\
	}

static const struct dcmipp_capture_pix_map dcmipp_capture_pixel_pix_map_list[] = {
	/* Coplanar formats are supported on main & aux pipe */
	PIXMAP_MBUS_PIXEL_PFMT(RGB888_1X24, RGB565, 1, DCMIPP_PxPPCR_FORMAT_RGB565, 0),
	PIXMAP_MBUS_PIXEL_PFMT(YUV8_1X24, YUYV, 1, DCMIPP_PxPPCR_FORMAT_YUYV, 0),
	PIXMAP_MBUS_PIXEL_PFMT(YUV8_1X24, YVYU, 1, DCMIPP_PxPPCR_FORMAT_YUYV, 1),
	PIXMAP_MBUS_PIXEL_PFMT(YUV8_1X24, UYVY, 1, DCMIPP_PxPPCR_FORMAT_UYVY, 0),
	PIXMAP_MBUS_PIXEL_PFMT(YUV8_1X24, VYUY, 1, DCMIPP_PxPPCR_FORMAT_UYVY, 1),
	PIXMAP_MBUS_PIXEL_PFMT(YUV8_1X24, GREY, 1, DCMIPP_PxPPCR_FORMAT_Y8, 0),
	PIXMAP_MBUS_PIXEL_PFMT(RGB888_1X24, RGB24, 1, DCMIPP_PxPPCR_FORMAT_RGB888, 1),
	PIXMAP_MBUS_PIXEL_PFMT(RGB888_1X24, BGR24, 1, DCMIPP_PxPPCR_FORMAT_RGB888, 0),
	PIXMAP_MBUS_PIXEL_PFMT(RGB888_1X24, ARGB32, 1, DCMIPP_PxPPCR_FORMAT_RGBA8888, 1),
	PIXMAP_MBUS_PIXEL_PFMT(RGB888_1X24, ABGR32, 1, DCMIPP_PxPPCR_FORMAT_ARGB8888, 0),
	PIXMAP_MBUS_PIXEL_PFMT(RGB888_1X24, RGBA32, 1, DCMIPP_PxPPCR_FORMAT_ARGB8888, 1),
	PIXMAP_MBUS_PIXEL_PFMT(RGB888_1X24, BGRA32, 1, DCMIPP_PxPPCR_FORMAT_RGBA8888, 0),

	/* Semiplanar & planar formats (plane_nb > 1) are only supported on main pipe */
	PIXMAP_MBUS_PIXEL_PFMT(YUV8_1X24, NV12, 2, DCMIPP_P1PPCR_FORMAT_NV21, 0),
	PIXMAP_MBUS_PIXEL_PFMT(YUV8_1X24, NV21, 2, DCMIPP_P1PPCR_FORMAT_NV21, 1),
	PIXMAP_MBUS_PIXEL_PFMT(YUV8_1X24, NV16, 2, DCMIPP_P1PPCR_FORMAT_NV61, 0),
	PIXMAP_MBUS_PIXEL_PFMT(YUV8_1X24, NV61, 2, DCMIPP_P1PPCR_FORMAT_NV61, 1),
	PIXMAP_MBUS_PIXEL_PFMT(YUV8_1X24, YUV420, 3, DCMIPP_P1PPCR_FORMAT_YV12, 0),
	PIXMAP_MBUS_PIXEL_PFMT(YUV8_1X24, YVU420, 3, DCMIPP_P1PPCR_FORMAT_YV12, 1),
};

struct dcmipp_buf {
	struct vb2_v4l2_buffer	vb;
	bool			prepared;
	dma_addr_t		addr;
	size_t			size;
	dma_addr_t		addrs[3];
	u32			strides[3];
	u64			sizes[3];
	struct list_head	list;
};

struct dcmipp_capture_device {
	struct dcmipp_ent_device ved;
	struct video_device vdev;
	struct device *dev;
	struct v4l2_pix_format format;
	struct vb2_queue queue;
	struct list_head buffers;
	/*
	 * Protects concurrent calls of buf queue / irq handler
	 * and buffer handling related variables / lists
	 */
	spinlock_t irqlock;
	/* mutex used as vdev and queue lock */
	struct mutex lock;
	u32 sequence;
	struct v4l2_subdev *s_subdev;
	u32 s_subdev_pad_nb;

	enum dcmipp_state state;

	/*
	 * DCMIPP driver is handling 2 buffers
	 * active: buffer into which DCMIPP is currently writing into
	 * next: buffer given to the DCMIPP and which will become
	 *       automatically active on next VSYNC
	 */
	struct dcmipp_buf *active, *next;

	void __iomem *regs;

	int pipe_id;

	const struct dcmipp_capture_pix_map *pix_map;
	unsigned int pix_map_array_size;

	u32 cmsr2;

	struct {
		u32 errors;
		u32 limit;
		u32 overrun;
		u32 buffers;
		u32 vsync;
		u32 frame;
		u32 it;
		u32 underrun;
		u32 nactive;
	} count;
};

static const struct dcmipp_capture_pix_map *
dcmipp_capture_pix_map_by_pixelformat(struct dcmipp_capture_device *vcap,
				      u32 pixelformat)
{
	for (unsigned int i = 0; i < vcap->pix_map_array_size; i++) {
		if (vcap->pix_map[i].pixelformat == pixelformat)
			return &vcap->pix_map[i];
	}

	return NULL;
}

static bool dcmipp_capture_is_format_valid(struct dcmipp_capture_device *vcap,
					   unsigned int pixelformat)
{
	const struct dcmipp_capture_pix_map *vpix =
		dcmipp_capture_pix_map_by_pixelformat(vcap, pixelformat);

	if (!vpix || (vpix->plane_nb > 1 && vcap->pipe_id != 1))
		return false;

	return true;
}

static const struct v4l2_pix_format fmt_default = {
	.width = DCMIPP_FMT_WIDTH_DEFAULT,
	.height = DCMIPP_FMT_HEIGHT_DEFAULT,
	.pixelformat = V4L2_PIX_FMT_RGB565,
	.field = V4L2_FIELD_NONE,
	.bytesperline = DCMIPP_FMT_WIDTH_DEFAULT * 2,
	.sizeimage = DCMIPP_FMT_WIDTH_DEFAULT * DCMIPP_FMT_HEIGHT_DEFAULT * 2,
	.colorspace = DCMIPP_COLORSPACE_DEFAULT,
	.ycbcr_enc = DCMIPP_YCBCR_ENC_DEFAULT,
	.quantization = DCMIPP_QUANTIZATION_DEFAULT,
	.xfer_func = DCMIPP_XFER_FUNC_DEFAULT,
};

static inline int hdw_pixel_alignment(u32 format)
{
	/* 16 bytes alignment required by hardware */
	switch (format) {
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV21:
	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_YVU420:
	case V4L2_PIX_FMT_NV16:
	case V4L2_PIX_FMT_NV61:
	case V4L2_PIX_FMT_GREY:
		return 4;/* 2^4 = 16 pixels = 16 bytes */
	case V4L2_PIX_FMT_RGB565:
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_YVYU:
	case V4L2_PIX_FMT_UYVY:
	case V4L2_PIX_FMT_VYUY:
		return 3;/* 2^3  = 8 pixels = 16 bytes */
	case V4L2_PIX_FMT_RGB24:
	case V4L2_PIX_FMT_BGR24:
		return 4;/* 2^4 = 16 pixels = 48 bytes */
	case V4L2_PIX_FMT_ARGB32:
	case V4L2_PIX_FMT_ABGR32:
	case V4L2_PIX_FMT_RGBA32:
	case V4L2_PIX_FMT_BGRA32:
		return 2;/* 2^2  = 4 pixels = 16 bytes */
	default:
		return 0;
	}
}

static inline int frame_planes(dma_addr_t base_addr, dma_addr_t addrs[],
			       u32 strides[], u64 sizes[],
			       u32 width, u32 height, u32 format)
{
	const struct v4l2_format_info *info;

	/* Only used by dump pipe hence addrs[0] is enough */
	if (format == V4L2_PIX_FMT_JPEG) {
		addrs[0] = base_addr;
		return 0;
	}

	info = v4l2_format_info(format);
	if (!info)
		return -EINVAL;

	/* Fill-in each plane information */
	addrs[0] = base_addr;
	strides[0] = width * info->bpp[0];
	sizes[0] = strides[0] * height;

	if (info->comp_planes > 1) {
		addrs[1] = addrs[0] + sizes[0];
		strides[1] = width * info->bpp[1] / info->hdiv;
		sizes[1] = strides[1] * height / info->vdiv;
	}

	if (info->comp_planes > 2) {
		addrs[2] = addrs[1] + sizes[1];
		strides[2] = width * info->bpp[2] / info->hdiv;
		sizes[2] = strides[2] * height / info->vdiv;
	}

	return 0;
}

static int dcmipp_capture_querycap(struct file *file, void *priv,
				   struct v4l2_capability *cap)
{
	strscpy(cap->driver, DCMIPP_PDEV_NAME, sizeof(cap->driver));
	strscpy(cap->card, KBUILD_MODNAME, sizeof(cap->card));

	return 0;
}

static int dcmipp_capture_g_fmt_vid_cap(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct dcmipp_capture_device *vcap = video_drvdata(file);

	f->fmt.pix = vcap->format;

	return 0;
}

static int dcmipp_capture_try_fmt_vid_cap(struct file *file, void *priv,
					  struct v4l2_format *f)
{
	struct dcmipp_capture_device *vcap = video_drvdata(file);
	struct v4l2_pix_format *format = &f->fmt.pix;
	u32 in_w, in_h;

	/* Don't accept a pixelformat that is not on the table */
	if (!dcmipp_capture_is_format_valid(vcap, format->pixelformat))
		format->pixelformat = fmt_default.pixelformat;

	/* Adjust width & height */
	in_w = format->width;
	in_h = format->height;
	format->width = clamp_t(u32, format->width, DCMIPP_FRAME_MIN_WIDTH,
				vcap->pipe_id != 0 ?
				DCMIPP_FRAME_MAX_WIDTH : DCMIPP_FRAME_MAX_WIDTH);
	if (vcap->pipe_id != 0)
		format->width = round_up(format->width,
					 1 << hdw_pixel_alignment(format->pixelformat));
	format->height = clamp_t(u32, format->height, DCMIPP_FRAME_MIN_HEIGHT,
				 vcap->pipe_id != 0 ?
				 DCMIPP_FRAME_MAX_HEIGHT : DCMIPP_FRAME_MAX_HEIGHT);
	if (format->width != in_w || format->height != in_h)
		dev_dbg(vcap->dev, "resolution updated: %dx%d -> %dx%d\n",
			in_w, in_h, format->width, format->height);

	if (format->pixelformat == V4L2_PIX_FMT_JPEG) {
		format->bytesperline = format->width;
		format->sizeimage = format->bytesperline * format->height;
	} else {
		v4l2_fill_pixfmt(format, format->pixelformat,
				 format->width, format->height);
	}

	if (format->field == V4L2_FIELD_ANY)
		format->field = fmt_default.field;

	dcmipp_colorimetry_clamp(format);

	return 0;
}

static int dcmipp_capture_s_fmt_vid_cap(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct dcmipp_capture_device *vcap = video_drvdata(file);
	int ret;

	/* Do not change the format while stream is on */
	if (vb2_is_busy(&vcap->queue))
		return -EBUSY;

	ret = dcmipp_capture_try_fmt_vid_cap(file, priv, f);
	if (ret)
		return ret;

	dev_dbg(vcap->dev, "%s: format update: old:%ux%u (0x%p4cc, %u, %u, %u, %u) new:%ux%d (0x%p4cc, %u, %u, %u, %u)\n",
		vcap->vdev.name,
		/* old */
		vcap->format.width, vcap->format.height,
		&vcap->format.pixelformat, vcap->format.colorspace,
		vcap->format.quantization, vcap->format.xfer_func,
		vcap->format.ycbcr_enc,
		/* new */
		f->fmt.pix.width, f->fmt.pix.height,
		&f->fmt.pix.pixelformat, f->fmt.pix.colorspace,
		f->fmt.pix.quantization, f->fmt.pix.xfer_func,
		f->fmt.pix.ycbcr_enc);

	vcap->format = f->fmt.pix;

	return 0;
}

static int dcmipp_capture_enum_fmt_vid_cap(struct file *file, void *priv,
					   struct v4l2_fmtdesc *f)
{
	struct dcmipp_capture_device *vcap = video_drvdata(file);
	unsigned int index = f->index;
	unsigned int i, prev_pixelformat = 0;

	/*
	 * List up all formats (or only ones matching f->mbus_code), taking
	 * care of removing duplicated entries (due to support of both
	 * parallel & csi 16 bits formats
	 */
	for (i = 0; i < vcap->pix_map_array_size; i++) {
		/* Only main pipe supports (Semi)-planar formats */
		if (vcap->pipe_id != 1 && vcap->pix_map[i].plane_nb > 1)
			continue;

		/* Skip formats not matching requested mbus code */
		if (f->mbus_code && vcap->pix_map[i].code != f->mbus_code)
			continue;

		/* Skip duplicated pixelformat */
		if (vcap->pix_map[i].pixelformat == prev_pixelformat)
			continue;

		prev_pixelformat = vcap->pix_map[i].pixelformat;

		if (index == 0)
			break;

		index--;
	}

	if (i == vcap->pix_map_array_size)
		return -EINVAL;

	f->pixelformat = vcap->pix_map[i].pixelformat;

	return 0;
}

static int dcmipp_capture_enum_framesizes(struct file *file, void *fh,
					  struct v4l2_frmsizeenum *fsize)
{
	struct dcmipp_capture_device *vcap = video_drvdata(file);


	if (fsize->index)
		return -EINVAL;

	/* Only accept code in the pix map table */
	if (!dcmipp_capture_is_format_valid(vcap, fsize->pixel_format))
		return -EINVAL;

	fsize->type = V4L2_FRMSIZE_TYPE_CONTINUOUS;
	fsize->stepwise.min_width = DCMIPP_FRAME_MIN_WIDTH;
	fsize->stepwise.max_width = DCMIPP_FRAME_MAX_WIDTH;
	fsize->stepwise.min_height = DCMIPP_FRAME_MIN_HEIGHT;
	fsize->stepwise.max_height = DCMIPP_FRAME_MAX_HEIGHT;
	fsize->stepwise.step_width = 1;
	fsize->stepwise.step_height = 1;

	return 0;
}

static const struct v4l2_file_operations dcmipp_capture_fops = {
	.owner		= THIS_MODULE,
	.open		= v4l2_fh_open,
	.release	= vb2_fop_release,
	.read           = vb2_fop_read,
	.poll		= vb2_fop_poll,
	.unlocked_ioctl = video_ioctl2,
	.mmap           = vb2_fop_mmap,
};

static const struct v4l2_ioctl_ops dcmipp_capture_ioctl_ops = {
	.vidioc_querycap = dcmipp_capture_querycap,

	.vidioc_g_fmt_vid_cap = dcmipp_capture_g_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap = dcmipp_capture_s_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap = dcmipp_capture_try_fmt_vid_cap,
	.vidioc_enum_fmt_vid_cap = dcmipp_capture_enum_fmt_vid_cap,
	.vidioc_enum_framesizes = dcmipp_capture_enum_framesizes,

	.vidioc_reqbufs = vb2_ioctl_reqbufs,
	.vidioc_create_bufs = vb2_ioctl_create_bufs,
	.vidioc_prepare_buf = vb2_ioctl_prepare_buf,
	.vidioc_querybuf = vb2_ioctl_querybuf,
	.vidioc_qbuf = vb2_ioctl_qbuf,
	.vidioc_dqbuf = vb2_ioctl_dqbuf,
	.vidioc_expbuf = vb2_ioctl_expbuf,
	.vidioc_streamon = vb2_ioctl_streamon,
	.vidioc_streamoff = vb2_ioctl_streamoff,
};

static void dcmipp_start_capture(struct dcmipp_capture_device *vcap,
				 struct dcmipp_buf *buf)
{
	/* Set buffer address */
	reg_write(vcap, DCMIPP_PxPPM0AR1(vcap->pipe_id), buf->addrs[0]);

	if (vcap->pipe_id == 0) {
		/* Set buffer size */
		reg_write(vcap, DCMIPP_P0DCLMTR, DCMIPP_P0DCLMTR_ENABLE |
			  ((buf->size / 4) & DCMIPP_P0DCLMTR_LIMIT_MASK));
	} else {
		reg_write(vcap, DCMIPP_PxPPM0PR(vcap->pipe_id),
			  buf->strides[0]);

		if (buf->addrs[1]) {
			reg_write(vcap, DCMIPP_P1PPM1AR1, buf->addrs[1]);
			reg_write(vcap, DCMIPP_P1PPM1PR, buf->strides[1]);
		}

		if (buf->addrs[2])
			reg_write(vcap, DCMIPP_P1PPM2AR1, buf->addrs[2]);
	}

	/* Capture request */
	reg_set(vcap, DCMIPP_PxFCTCR(vcap->pipe_id), DCMIPP_PxFCTCR_CPTREQ);
}

static void dcmipp_capture_all_buffers_done(struct dcmipp_capture_device *vcap,
					    enum vb2_buffer_state state)
{
	struct dcmipp_buf *buf, *node;

	list_for_each_entry_safe(buf, node, &vcap->buffers, list) {
		list_del_init(&buf->list);
		vb2_buffer_done(&buf->vb.vb2_buf, state);
	}
}

static int dcmipp_capture_start_streaming(struct vb2_queue *vq,
					  unsigned int count)
{
	struct dcmipp_capture_device *vcap = vb2_get_drv_priv(vq);
	struct media_entity *entity = &vcap->vdev.entity;
	struct dcmipp_buf *buf;
	struct media_pad *pad;
	int ret;

	vcap->sequence = 0;
	memset(&vcap->count, 0, sizeof(vcap->count));

	/*
	 * Get source subdev - since link is IMMUTABLE, pointer is cached
	 * within the dcmipp_capture_device structure
	 */
	if (!vcap->s_subdev) {
		pad = media_pad_remote_pad_first(&vcap->vdev.entity.pads[0]);
		if (!pad || !is_media_entity_v4l2_subdev(pad->entity)) {
			ret = -EINVAL;
			goto err_buffer_done;
		}
		vcap->s_subdev = media_entity_to_v4l2_subdev(pad->entity);
		vcap->s_subdev_pad_nb = pad->index;
	}

	ret = pm_runtime_resume_and_get(vcap->dev);
	if (ret < 0) {
		dev_err(vcap->dev, "%s: Failed to start streaming, cannot get sync (%d)\n",
			__func__, ret);
		goto err_buffer_done;
	}

	ret = media_pipeline_start(entity->pads, &vcap->ved.dcmipp->pipe);
	if (ret) {
		dev_dbg(vcap->dev, "%s: Failed to start streaming, media pipeline start error (%d)\n",
			__func__, ret);
		goto err_pm_put;
	}

	ret = v4l2_subdev_enable_streams(vcap->s_subdev,
					 vcap->s_subdev_pad_nb, BIT_ULL(0));
	if (ret)
		goto err_media_pipeline_stop;

	spin_lock_irq(&vcap->irqlock);

	if (vcap->pipe_id != 0) {
		const struct dcmipp_capture_pix_map *vpix =
			dcmipp_capture_pix_map_by_pixelformat(vcap, vcap->format.pixelformat);
		unsigned int ppcr = 0;

		/*
		 * Configure the Pixel Packer
		 * vpix is guaranteed to be valid since pixelformat is validated
		 * in dcmipp_pixelcap_s_fmt_vid_cap function before
		 */
		ppcr = vpix->ppcr_fmt;
		if (vpix->swap_uv)
			ppcr |= DCMIPP_PxPPCR_SWAPRB;

		reg_write(vcap, DCMIPP_PxPPCR(vcap->pipe_id), ppcr);
	}

	/* Enable pipe at the end of programming */
	reg_set(vcap, DCMIPP_PxFSCR(vcap->pipe_id), DCMIPP_PxFSCR_PIPEN);

	/*
	 * vb2 framework guarantee that we have at least 'min_queued_buffers'
	 * buffers in the list at this moment
	 */
	vcap->next = list_first_entry(&vcap->buffers, typeof(*buf), list);
	dev_dbg(vcap->dev, "Start with next [%d] %p phy=%pad\n",
		vcap->next->vb.vb2_buf.index, vcap->next, &vcap->next->addr);

	dcmipp_start_capture(vcap, vcap->next);

	/* Enable interruptions */
	spin_lock(&vcap->vdev.v4l2_dev->lock);
	reg_set(vcap, DCMIPP_CMIER, DCMIPP_CMIER_PxALL(vcap->pipe_id));
	spin_unlock(&vcap->vdev.v4l2_dev->lock);

	vcap->state = DCMIPP_RUNNING;

	spin_unlock_irq(&vcap->irqlock);

	return 0;

err_media_pipeline_stop:
	media_pipeline_stop(entity->pads);
err_pm_put:
	pm_runtime_put(vcap->dev);
err_buffer_done:
	spin_lock_irq(&vcap->irqlock);
	/*
	 * Return all buffers to vb2 in QUEUED state.
	 * This will give ownership back to userspace
	 */
	dcmipp_capture_all_buffers_done(vcap, VB2_BUF_STATE_QUEUED);
	vcap->active = NULL;
	spin_unlock_irq(&vcap->irqlock);

	return ret;
}

static void dcmipp_dump_status(struct dcmipp_capture_device *vcap)
{
	struct device *dev = vcap->dev;

	dev_dbg(dev, "[DCMIPP_PRSR]  =%#10.8x\n", reg_read(vcap, DCMIPP_PRSR));
	dev_dbg(dev, "[DCMIPP_P0SR] =%#10.8x\n", reg_read(vcap, DCMIPP_PxSR(0)));
	dev_dbg(dev, "[DCMIPP_P0DCCNTR]=%#10.8x\n",
		reg_read(vcap, DCMIPP_P0DCCNTR));
	dev_dbg(dev, "[DCMIPP_CMSR1] =%#10.8x\n", reg_read(vcap, DCMIPP_CMSR1));
	dev_dbg(dev, "[DCMIPP_CMSR2] =%#10.8x\n", reg_read(vcap, DCMIPP_CMSR2));
}

/*
 * Stop the stream engine. Any remaining buffers in the stream queue are
 * dequeued and passed on to the vb2 framework marked as STATE_ERROR.
 */
static void dcmipp_capture_stop_streaming(struct vb2_queue *vq)
{
	struct dcmipp_capture_device *vcap = vb2_get_drv_priv(vq);
	int ret;
	u32 status;

	ret = v4l2_subdev_disable_streams(vcap->s_subdev,
					  vcap->s_subdev_pad_nb, BIT_ULL(0));
	if (ret)
		dev_warn(vcap->dev, "Failed to disable stream\n");

	/* Stop the media pipeline */
	media_pipeline_stop(vcap->vdev.entity.pads);

	/* Disable interruptions */
	spin_lock(&vcap->vdev.v4l2_dev->lock);
	reg_clear(vcap, DCMIPP_CMIER, DCMIPP_CMIER_PxALL(vcap->pipe_id));
	spin_unlock(&vcap->vdev.v4l2_dev->lock);

	/* Stop capture */
	reg_clear(vcap, DCMIPP_PxFCTCR(vcap->pipe_id), DCMIPP_PxFCTCR_CPTREQ);

	/* Wait until CPTACT become 0 */
	ret = readl_relaxed_poll_timeout(vcap->regs + DCMIPP_PxSR(vcap->pipe_id),
					 status,
					 !(status & DCMIPP_PxSR_CPTACT),
					 20 * USEC_PER_MSEC,
					 1000 * USEC_PER_MSEC);
	if (ret)
		dev_warn(vcap->dev, "Timeout when stopping\n");

	/* Disable pipe */
	reg_clear(vcap, DCMIPP_PxFSCR(vcap->pipe_id), DCMIPP_PxFSCR_PIPEN);

	/* Clear any pending interrupts */
	reg_write(vcap, DCMIPP_CMFCR, DCMIPP_CMIER_PxALL(vcap->pipe_id));

	spin_lock_irq(&vcap->irqlock);

	/* Return all queued buffers to vb2 in ERROR state */
	dcmipp_capture_all_buffers_done(vcap, VB2_BUF_STATE_ERROR);
	INIT_LIST_HEAD(&vcap->buffers);

	vcap->active = NULL;
	vcap->state = DCMIPP_STOPPED;

	spin_unlock_irq(&vcap->irqlock);

	if (vcap->pipe_id == 0)
		dcmipp_dump_status(vcap);

	pm_runtime_put(vcap->dev);

	if (vcap->count.errors)
		dev_warn(vcap->dev, "Some errors found while streaming: errors=%d (overrun=%d, limit=%d, nactive=%d), underrun=%d, buffers=%d\n",
			 vcap->count.errors, vcap->count.overrun,
			 vcap->count.limit, vcap->count.nactive,
			 vcap->count.underrun, vcap->count.buffers);
}

static int dcmipp_capture_buf_prepare(struct vb2_buffer *vb)
{
	struct dcmipp_capture_device *vcap =  vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct dcmipp_buf *buf = container_of(vbuf, struct dcmipp_buf, vb);
	struct v4l2_pix_format *format = &vcap->format;
	unsigned long size;
	int ret;

	size = vcap->format.sizeimage;

	if (vb2_plane_size(vb, 0) < size) {
		dev_err(vcap->dev, "%s data will not fit into plane (%lu < %lu)\n",
			__func__, vb2_plane_size(vb, 0), size);
		return -EINVAL;
	}

	vb2_set_plane_payload(vb, 0, size);

	if (!buf->prepared) {
		/* Get memory addresses */
		buf->addr = vb2_dma_contig_plane_dma_addr(&buf->vb.vb2_buf, 0);
		buf->size = vb2_plane_size(&buf->vb.vb2_buf, 0);

		ret = frame_planes(buf->addr,
				   buf->addrs, buf->strides, buf->sizes,
				   format->width, format->height,
				   format->pixelformat);
		if (ret) {
			dev_err(vcap->dev, "%s: Unsupported pixel format (%x)\n",
				__func__, format->pixelformat);
			return ret;
		}

		/* Check for 16 bytes alignment required by hardware */
		WARN_ON(buf->addrs[0] & 15);
		if (vcap->pipe_id != 0) {
			WARN_ON(buf->strides[0] & 15);
			WARN_ON(buf->addrs[1] & 15);
			WARN_ON(buf->strides[1] & 15);
			WARN_ON(buf->addrs[2] & 15);
		}

		buf->prepared = true;

		vb2_set_plane_payload(&buf->vb.vb2_buf, 0, buf->size);

		dev_dbg(vcap->dev, "Setup [%d] phy=%pad size=%zu\n",
			vb->index, &buf->addr, buf->size);
	}

	return 0;
}

static void dcmipp_capture_buf_queue(struct vb2_buffer *vb2_buf)
{
	struct dcmipp_capture_device *vcap =
		vb2_get_drv_priv(vb2_buf->vb2_queue);
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb2_buf);
	struct dcmipp_buf *buf = container_of(vbuf, struct dcmipp_buf, vb);

	dev_dbg(vcap->dev, "Queue [%d] %p phy=%pad\n", buf->vb.vb2_buf.index,
		buf, &buf->addr);

	spin_lock_irq(&vcap->irqlock);
	list_add_tail(&buf->list, &vcap->buffers);

	if (vcap->state == DCMIPP_WAIT_FOR_BUFFER) {
		vcap->next = buf;
		dev_dbg(vcap->dev, "Restart with next [%d] %p phy=%pad\n",
			buf->vb.vb2_buf.index, buf, &buf->addr);

		dcmipp_start_capture(vcap, buf);

		vcap->state = DCMIPP_RUNNING;
	}

	spin_unlock_irq(&vcap->irqlock);
}

static int dcmipp_capture_queue_setup(struct vb2_queue *vq,
				      unsigned int *nbuffers,
				      unsigned int *nplanes,
				      unsigned int sizes[],
				      struct device *alloc_devs[])
{
	struct dcmipp_capture_device *vcap = vb2_get_drv_priv(vq);
	unsigned int size;

	size = vcap->format.sizeimage;

	/* Make sure the image size is large enough */
	if (*nplanes)
		return sizes[0] < vcap->format.sizeimage ? -EINVAL : 0;

	*nplanes = 1;
	sizes[0] = vcap->format.sizeimage;

	dev_dbg(vcap->dev, "Setup queue, count=%d, size=%d\n",
		*nbuffers, size);

	return 0;
}

static int dcmipp_capture_buf_init(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct dcmipp_buf *buf = container_of(vbuf, struct dcmipp_buf, vb);

	INIT_LIST_HEAD(&buf->list);

	return 0;
}

static const struct vb2_ops dcmipp_capture_qops = {
	.start_streaming	= dcmipp_capture_start_streaming,
	.stop_streaming		= dcmipp_capture_stop_streaming,
	.buf_init		= dcmipp_capture_buf_init,
	.buf_prepare		= dcmipp_capture_buf_prepare,
	.buf_queue		= dcmipp_capture_buf_queue,
	.queue_setup		= dcmipp_capture_queue_setup,
};

static void dcmipp_capture_release(struct video_device *vdev)
{
	struct dcmipp_capture_device *vcap =
		container_of(vdev, struct dcmipp_capture_device, vdev);

	dcmipp_pads_cleanup(vcap->ved.pads);
	mutex_destroy(&vcap->lock);

	kfree(vcap);
}

void dcmipp_capture_ent_release(struct dcmipp_ent_device *ved)
{
	struct dcmipp_capture_device *vcap =
		container_of(ved, struct dcmipp_capture_device, ved);

	media_entity_cleanup(ved->ent);
	vb2_video_unregister_device(&vcap->vdev);
}

static void dcmipp_buffer_done(struct dcmipp_capture_device *vcap,
			       struct dcmipp_buf *buf,
			       size_t bytesused,
			       int err)
{
	struct vb2_v4l2_buffer *vbuf;

	list_del_init(&buf->list);

	vbuf = &buf->vb;

	vbuf->sequence = vcap->sequence++;
	vbuf->field = V4L2_FIELD_NONE;
	vbuf->vb2_buf.timestamp = ktime_get_ns();
	vb2_set_plane_payload(&vbuf->vb2_buf, 0, bytesused);
	vb2_buffer_done(&vbuf->vb2_buf,
			err ? VB2_BUF_STATE_ERROR : VB2_BUF_STATE_DONE);
	dev_dbg(vcap->dev, "Done  [%d] %p phy=%pad\n", buf->vb.vb2_buf.index,
		buf, &buf->addr);
	vcap->count.buffers++;
}

/* irqlock must be held */
static void
dcmipp_capture_set_next_frame_or_stop(struct dcmipp_capture_device *vcap)
{
	if (!vcap->next && list_is_singular(&vcap->buffers)) {
		/*
		 * If there is no available buffer (none or a single one in the
		 * list while two are expected), stop the capture (effective
		 * for next frame). On-going frame capture will continue until
		 * FRAME END but no further capture will be done.
		 */
		reg_clear(vcap, DCMIPP_PxFCTCR(vcap->pipe_id), DCMIPP_PxFCTCR_CPTREQ);

		dev_dbg(vcap->dev, "Capture restart is deferred to next buffer queueing\n");
		vcap->next = NULL;
		vcap->state = DCMIPP_WAIT_FOR_BUFFER;
		return;
	}

	/* If we don't have buffer yet, pick the one after active */
	if (!vcap->next)
		vcap->next = list_next_entry(vcap->active, list);

	/*
	 * Set buffer address
	 * This register is shadowed and will be taken into
	 * account on next VSYNC (start of next frame)
	 */
	reg_write(vcap, DCMIPP_PxPPM0AR1(vcap->pipe_id), vcap->next->addrs[0]);
	if (vcap->pipe_id == 1) {
		if (vcap->next->addrs[1])
			reg_write(vcap, DCMIPP_P1PPM1AR1, vcap->next->addrs[1]);
		if (vcap->next->addrs[2])
			reg_write(vcap, DCMIPP_P1PPM2AR1, vcap->next->addrs[2]);
	}
	dev_dbg(vcap->dev, "Write [%d] %p phy=%pad\n",
		vcap->next->vb.vb2_buf.index, vcap->next, &vcap->next->addr);
}

/* irqlock must be held */
static void dcmipp_capture_process_frame(struct dcmipp_capture_device *vcap,
					 size_t bytesused)
{
	int err = 0;
	struct dcmipp_buf *buf = vcap->active;

	if (!buf) {
		vcap->count.nactive++;
		vcap->count.errors++;
		return;
	}

	if (bytesused > buf->size) {
		dev_dbg(vcap->dev, "frame larger than expected (%zu > %zu)\n",
			bytesused, buf->size);
		/* Clip to buffer size and return buffer to V4L2 in error */
		bytesused = buf->size;
		vcap->count.limit++;
		vcap->count.errors++;
		err = -EOVERFLOW;
	}

	dcmipp_buffer_done(vcap, buf, bytesused, err);
	vcap->active = NULL;
}

static irqreturn_t dcmipp_capture_irq_thread(int irq, void *arg)
{
	struct dcmipp_capture_device *vcap =
			container_of(arg, struct dcmipp_capture_device, ved);
	u32 cmsr2_pxframef;
	u32 cmsr2_pxvsyncf;
	u32 cmsr2_pxovrf;
	size_t bytesused = 0;

	spin_lock_irq(&vcap->irqlock);

	cmsr2_pxovrf = DCMIPP_CMSR2_PxOVRF(vcap->pipe_id);
	cmsr2_pxvsyncf = DCMIPP_CMSR2_PxVSYNCF(vcap->pipe_id);
	cmsr2_pxframef = DCMIPP_CMSR2_PxFRAMEF(vcap->pipe_id);

	/*
	 * If we have an overrun, a frame-end will probably not be generated,
	 * in that case the active buffer will be recycled as next buffer by
	 * the VSYNC handler
	 */
	if (vcap->cmsr2 & cmsr2_pxovrf) {
		vcap->count.errors++;
		vcap->count.overrun++;
	}

	if (vcap->cmsr2 & cmsr2_pxframef) {
		vcap->count.frame++;

		/* Read captured buffer size */
		if (vcap->pipe_id == 0)
			bytesused = reg_read(vcap, DCMIPP_P0DCCNTR);
		else
			bytesused = vcap->format.sizeimage;
		dcmipp_capture_process_frame(vcap, bytesused);
	}

	if (vcap->cmsr2 & cmsr2_pxvsyncf) {
		vcap->count.vsync++;
		if (vcap->state == DCMIPP_WAIT_FOR_BUFFER) {
			vcap->count.underrun++;
			goto out;
		}

		/*
		 * On VSYNC, the previously set next buffer is going to become
		 * active thanks to the shadowing mechanism of the DCMIPP. In
		 * most of the cases, since a FRAMEEND has already come,
		 * pointer next is NULL since active is reset during the
		 * FRAMEEND handling. However, in case of framerate adjustment,
		 * there are more VSYNC than FRAMEEND. Thus we recycle the
		 * active (but not used) buffer and put it back into next.
		 */
		swap(vcap->active, vcap->next);
		dcmipp_capture_set_next_frame_or_stop(vcap);
	}

out:
	spin_unlock_irq(&vcap->irqlock);
	return IRQ_HANDLED;
}

static irqreturn_t dcmipp_capture_irq_callback(int irq, void *arg)
{
	struct dcmipp_capture_device *vcap =
			container_of(arg, struct dcmipp_capture_device, ved);
	struct dcmipp_ent_device *ved = arg;

	/* Store interrupt status register */
	vcap->cmsr2 = ved->cmsr2 & DCMIPP_CMIER_PxALL(vcap->pipe_id);
	if (!vcap->cmsr2)
		return IRQ_HANDLED;
	vcap->count.it++;

	/* Clear interrupt */
	reg_write(vcap, DCMIPP_CMFCR, vcap->cmsr2);

	return IRQ_WAKE_THREAD;
}

static int dcmipp_capture_link_validate(struct media_link *link)
{
	struct media_entity *entity = link->sink->entity;
	struct video_device *vd = media_entity_to_video_device(entity);
	struct dcmipp_capture_device *vcap = container_of(vd,
					struct dcmipp_capture_device, vdev);
	struct v4l2_subdev *source_sd =
		media_entity_to_v4l2_subdev(link->source->entity);
	struct v4l2_subdev_format source_fmt = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
		.pad = link->source->index,
	};
	u32 width_aligned;
	int ret, i;

	ret = v4l2_subdev_call(source_sd, pad, get_fmt, NULL, &source_fmt);
	if (ret < 0)
		return 0;

	width_aligned = source_fmt.format.width;

	/*
	 * On pixel pipes there can be alignment constraints.
	 * Depending on the format & pixelpacker constraints, vcap width is
	 * different from mbus width.  Compute expected vcap width based on
	 * mbus width
	 */
	if (vcap->pipe_id != 0)
		width_aligned = round_up(source_fmt.format.width,
					 1 << hdw_pixel_alignment(vcap->format.pixelformat));

	if (width_aligned != vcap->format.width ||
	    source_fmt.format.height != vcap->format.height) {
		dev_err(vcap->dev, "Wrong width or height %ux%u (%ux%u expected)\n",
			vcap->format.width, vcap->format.height,
			width_aligned, source_fmt.format.height);
		return -EINVAL;
	}

	for (i = 0; i < vcap->pix_map_array_size; i++) {
		if (vcap->pix_map[i].pixelformat == vcap->format.pixelformat &&
		    vcap->pix_map[i].code == source_fmt.format.code)
			break;
	}

	if (i == vcap->pix_map_array_size) {
		dev_err(vcap->dev, "mbus code 0x%x do not match capture device format (0x%x)\n",
			vcap->format.pixelformat, source_fmt.format.code);
		return -EINVAL;
	}

	return 0;
}

static const struct media_entity_operations dcmipp_capture_entity_ops = {
	.link_validate = dcmipp_capture_link_validate,
};

static int dcmipp_name_to_pipe_id(const char *name)
{
	if (strstr(name, "dump"))
		return 0;
	else if (strstr(name, "main"))
		return 1;
	else if (strstr(name, "aux"))
		return 2;
	else
		return -EINVAL;
}

struct dcmipp_ent_device *dcmipp_capture_ent_init(const char *entity_name,
						  struct dcmipp_device *dcmipp)
{
	struct dcmipp_capture_device *vcap;
	struct device *dev = dcmipp->dev;
	struct video_device *vdev;
	struct vb2_queue *q;
	const unsigned long pad_flag = MEDIA_PAD_FL_SINK;
	int ret = 0;

	/* Allocate the dcmipp_capture_device struct */
	vcap = kzalloc_obj(*vcap);
	if (!vcap)
		return ERR_PTR(-ENOMEM);

	/* Retrieve the pipe_id */
	vcap->pipe_id = dcmipp_name_to_pipe_id(entity_name);
	if (vcap->pipe_id < 0) {
		ret = -EIO;
		dev_err(dev, "failed to retrieve pipe_id\n");
		goto err_free_vcap;
	}

	/* Initialize supported format table format */
	if (vcap->pipe_id == 0) {
		vcap->pix_map = dcmipp_capture_dump_pix_map_list;
		vcap->pix_map_array_size = ARRAY_SIZE(dcmipp_capture_dump_pix_map_list);
	} else {
		vcap->pix_map = dcmipp_capture_pixel_pix_map_list;
		vcap->pix_map_array_size = ARRAY_SIZE(dcmipp_capture_pixel_pix_map_list);
	}

	/* Allocate the pads */
	vcap->ved.pads = dcmipp_pads_init(1, &pad_flag);
	if (IS_ERR(vcap->ved.pads)) {
		ret = PTR_ERR(vcap->ved.pads);
		goto err_free_vcap;
	}

	vcap->ved.dcmipp = dcmipp;

	/* Initialize the media entity */
	vcap->vdev.entity.name = entity_name;
	vcap->vdev.entity.function = MEDIA_ENT_F_IO_V4L;
	vcap->vdev.entity.ops = &dcmipp_capture_entity_ops;
	ret = media_entity_pads_init(&vcap->vdev.entity, 1, vcap->ved.pads);
	if (ret)
		goto err_clean_pads;

	/* Initialize the lock */
	mutex_init(&vcap->lock);

	/* Initialize the vb2 queue */
	q = &vcap->queue;
	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	q->io_modes = VB2_MMAP | VB2_DMABUF;
	q->lock = &vcap->lock;
	q->drv_priv = vcap;
	q->buf_struct_size = sizeof(struct dcmipp_buf);
	q->ops = &dcmipp_capture_qops;
	q->mem_ops = &vb2_dma_contig_memops;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->min_queued_buffers = 1;
	q->dev = dev;

	/* DCMIPP requires 16 bytes aligned buffers */
	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(dev, "Failed to set DMA mask\n");
		goto err_mutex_destroy;
	}

	ret = vb2_queue_init(q);
	if (ret) {
		dev_err(dev, "%s: vb2 queue init failed (err=%d)\n",
			entity_name, ret);
		goto err_clean_m_ent;
	}

	/* Initialize buffer list and its lock */
	INIT_LIST_HEAD(&vcap->buffers);
	spin_lock_init(&vcap->irqlock);

	/* Set default frame format */
	vcap->format = fmt_default;

	/* Fill the dcmipp_ent_device struct */
	vcap->ved.ent = &vcap->vdev.entity;
	vcap->ved.handler = dcmipp_capture_irq_callback;
	vcap->ved.thread_fn = dcmipp_capture_irq_thread;
	vcap->dev = dev;
	vcap->regs = dcmipp->regs;

	/* Initialize the video_device struct */
	vdev = &vcap->vdev;
	vdev->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING |
			    V4L2_CAP_IO_MC;
	vdev->release = dcmipp_capture_release;
	vdev->fops = &dcmipp_capture_fops;
	vdev->ioctl_ops = &dcmipp_capture_ioctl_ops;
	vdev->lock = &vcap->lock;
	vdev->queue = q;
	vdev->v4l2_dev = &dcmipp->v4l2_dev;
	strscpy(vdev->name, entity_name, sizeof(vdev->name));
	video_set_drvdata(vdev, &vcap->ved);

	/* Register the video_device with the v4l2 and the media framework */
	ret = video_register_device(vdev, VFL_TYPE_VIDEO, -1);
	if (ret) {
		dev_err(dev, "%s: video register failed (err=%d)\n",
			vcap->vdev.name, ret);
		goto err_clean_m_ent;
	}

	return &vcap->ved;

err_clean_m_ent:
	media_entity_cleanup(&vcap->vdev.entity);
err_mutex_destroy:
	mutex_destroy(&vcap->lock);
err_clean_pads:
	dcmipp_pads_cleanup(vcap->ved.pads);
err_free_vcap:
	kfree(vcap);

	return ERR_PTR(ret);
}
