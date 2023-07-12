// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021-2023 StarFive Technology Co., Ltd.
 *
 */

#include "stfcamss.h"
#include "stf_video.h"
#include <media/media-entity.h>
#include <media/v4l2-event.h>
#include <media/v4l2-mc.h>
#include <media/videobuf2-dma-sg.h>
#include <media/videobuf2-vmalloc.h>
#include <media/videobuf2-dma-contig.h>

static const struct stfcamss_format_info formats_pix_st7110_wr[] = {
	{ MEDIA_BUS_FMT_AYUV8_1X32, V4L2_PIX_FMT_AYUV32, 1,
	  { { 1, 1 } }, { { 1, 1 } }, { 32 } },
	{ MEDIA_BUS_FMT_YUYV8_2X8, V4L2_PIX_FMT_YUYV, 1,
	  { { 1, 1 } }, { { 1, 1 } }, { 16 } },
	{ MEDIA_BUS_FMT_RGB565_2X8_LE, V4L2_PIX_FMT_RGB565, 1,
	  { { 1, 1 } }, { { 1, 1 } }, { 16 } },
	{ MEDIA_BUS_FMT_SRGGB8_1X8, V4L2_PIX_FMT_SRGGB8, 1,
	  { { 1, 1 } }, { { 1, 1 } }, { 8 } },
	{ MEDIA_BUS_FMT_SGRBG8_1X8, V4L2_PIX_FMT_SGRBG8, 1,
	  { { 1, 1 } }, { { 1, 1 } }, { 8 } },
	{ MEDIA_BUS_FMT_SGBRG8_1X8, V4L2_PIX_FMT_SGBRG8, 1,
	  { { 1, 1 } }, { { 1, 1 } }, { 8 } },
	{ MEDIA_BUS_FMT_SBGGR8_1X8, V4L2_PIX_FMT_SBGGR8, 1,
	  { { 1, 1 } }, { { 1, 1 } }, { 8 } },
	{ MEDIA_BUS_FMT_SRGGB10_1X10, V4L2_PIX_FMT_SRGGB10, 1,
	  { { 1, 1 } }, { { 1, 1 } }, { 10 } },
	{ MEDIA_BUS_FMT_SGRBG10_1X10, V4L2_PIX_FMT_SGRBG10, 1,
	  { { 1, 1 } }, { { 1, 1 } }, { 10 } },
	{ MEDIA_BUS_FMT_SGBRG10_1X10, V4L2_PIX_FMT_SGBRG10, 1,
	  { { 1, 1 } }, { { 1, 1 } }, { 10 } },
	{ MEDIA_BUS_FMT_SBGGR10_1X10, V4L2_PIX_FMT_SBGGR10, 1,
	  { { 1, 1 } }, { { 1, 1 } }, { 10 } },
};

static const struct stfcamss_format_info formats_raw_st7110_isp[] = {
	{ MEDIA_BUS_FMT_SBGGR12_1X12, V4L2_PIX_FMT_SBGGR12, 1,
	  { { 1, 1 } }, { { 1, 1 } }, { 12 } },
	{ MEDIA_BUS_FMT_SRGGB12_1X12, V4L2_PIX_FMT_SRGGB12, 1,
	  { { 1, 1 } }, { { 1, 1 } }, { 12 } },
	{ MEDIA_BUS_FMT_SGRBG12_1X12, V4L2_PIX_FMT_SGRBG12, 1,
	  { { 1, 1 } }, { { 1, 1 } }, { 12 } },
	{ MEDIA_BUS_FMT_SGBRG12_1X12, V4L2_PIX_FMT_SGBRG12, 1,
	  { { 1, 1 } }, { { 1, 1 } }, { 12 } },
};

static const struct stfcamss_format_info formats_pix_st7110_isp[] = {
	// { MEDIA_BUS_FMT_YUYV12_2X12, V4L2_PIX_FMT_NV12M, 2,
	//  { { 1, 1 }, { 1, 1 } }, { { 1, 1 }, { 1, 1 } }, { 8 , 4 } },
	{ MEDIA_BUS_FMT_Y12_1X12, V4L2_PIX_FMT_NV12, 1,
	  { { 1, 1 } }, { { 2, 3 } }, { 8 } },
	{ MEDIA_BUS_FMT_Y12_1X12, V4L2_PIX_FMT_NV21, 1,
	  { { 1, 1 } }, { { 2, 3 } }, { 8 } },
};

static const struct stfcamss_format_info formats_st7110_isp_iti[] = {
	//  raw format
	{ MEDIA_BUS_FMT_SRGGB10_1X10, V4L2_PIX_FMT_SRGGB10, 1,
	  { { 1, 1 } }, { { 1, 1 } }, { 10 } },
	{ MEDIA_BUS_FMT_SGRBG10_1X10, V4L2_PIX_FMT_SGRBG10, 1,
	  { { 1, 1 } }, { { 1, 1 } }, { 10 } },
	{ MEDIA_BUS_FMT_SGBRG10_1X10, V4L2_PIX_FMT_SGBRG10, 1,
	  { { 1, 1 } }, { { 1, 1 } }, { 10 } },
	{ MEDIA_BUS_FMT_SBGGR10_1X10, V4L2_PIX_FMT_SBGGR10, 1,
	  { { 1, 1 } }, { { 1, 1 } }, { 10 } },
	{ MEDIA_BUS_FMT_SRGGB12_1X12, V4L2_PIX_FMT_SRGGB12, 1,
	  { { 1, 1 } }, { { 1, 1 } }, { 12 } },
	{ MEDIA_BUS_FMT_SGRBG12_1X12, V4L2_PIX_FMT_SGRBG12, 1,
	  { { 1, 1 } }, { { 1, 1 } }, { 12 } },
	{ MEDIA_BUS_FMT_SGBRG12_1X12, V4L2_PIX_FMT_SGBRG12, 1,
	  { { 1, 1 } }, { { 1, 1 } }, { 12 } },
	{ MEDIA_BUS_FMT_SBGGR12_1X12, V4L2_PIX_FMT_SBGGR12, 1,
	  { { 1, 1 } }, { { 1, 1 } }, { 12 } },

	// YUV420
	{ MEDIA_BUS_FMT_Y12_1X12, V4L2_PIX_FMT_NV12, 1,
	  { { 1, 1 } }, { { 2, 3 } }, { 8 } },
	{ MEDIA_BUS_FMT_Y12_1X12, V4L2_PIX_FMT_NV21, 1,
	  { { 1, 1 } }, { { 2, 3 } }, { 8 } },

	// YUV444
	{ MEDIA_BUS_FMT_YUV8_1X24, V4L2_PIX_FMT_NV24, 1,
	  { { 1, 1 } }, { { 1, 3 } }, { 8 } },
	{ MEDIA_BUS_FMT_VUY8_1X24, V4L2_PIX_FMT_NV42, 1,
	  { { 1, 1 } }, { { 1, 3 } }, { 8 } },
};

static int video_find_format(u32 code, u32 pixelformat,
				const struct stfcamss_format_info *formats,
				unsigned int nformats)
{
	int i;

	for (i = 0; i < nformats; i++) {
		if (formats[i].code == code &&
			formats[i].pixelformat == pixelformat)
			return i;
	}

	for (i = 0; i < nformats; i++)
		if (formats[i].code == code)
			return i;

	for (i = 0; i < nformats; i++)
		if (formats[i].pixelformat == pixelformat)
			return i;

	return -EINVAL;
}

static int __video_try_fmt(struct stfcamss_video *video,
		struct v4l2_format *f, int is_mp)
{
	struct v4l2_pix_format *pix;
	struct v4l2_pix_format_mplane *pix_mp;
	const struct stfcamss_format_info *fi;
	u32 width, height;
	u32 bpl;
	int i, j;

	st_debug(ST_VIDEO, "%s, fmt.type = 0x%x\n", __func__, f->type);
	pix = &f->fmt.pix;
	pix_mp = &f->fmt.pix_mp;

	if (is_mp) {
		for (i = 0; i < video->nformats; i++)
			if (pix_mp->pixelformat
				== video->formats[i].pixelformat)
				break;

		if (i == video->nformats)
			i = 0; /* default format */

		fi = &video->formats[i];
		width = pix_mp->width;
		height = pix_mp->height;

		memset(pix_mp, 0, sizeof(*pix_mp));

		pix_mp->pixelformat = fi->pixelformat;
		pix_mp->width = clamp_t(u32, width, STFCAMSS_FRAME_MIN_WIDTH,
				STFCAMSS_FRAME_MAX_WIDTH);
		pix_mp->height = clamp_t(u32, height, STFCAMSS_FRAME_MIN_HEIGHT,
				STFCAMSS_FRAME_MAX_HEIGHT);
		pix_mp->num_planes = fi->planes;
		for (j = 0; j < pix_mp->num_planes; j++) {
			bpl = pix_mp->width / fi->hsub[j].numerator *
				fi->hsub[j].denominator * fi->bpp[j] / 8;
			bpl = ALIGN(bpl, video->bpl_alignment);
			pix_mp->plane_fmt[j].bytesperline = bpl;
			pix_mp->plane_fmt[j].sizeimage = pix_mp->height /
				fi->vsub[j].numerator
				* fi->vsub[j].denominator * bpl;
		}

		pix_mp->field = V4L2_FIELD_NONE;
		pix_mp->colorspace = V4L2_COLORSPACE_SRGB;
		pix_mp->flags = 0;
		pix_mp->ycbcr_enc =
			V4L2_MAP_YCBCR_ENC_DEFAULT(pix_mp->colorspace);
		pix_mp->quantization =
			V4L2_MAP_QUANTIZATION_DEFAULT(true,
				pix_mp->colorspace, pix_mp->ycbcr_enc);
		pix_mp->xfer_func =
			V4L2_MAP_XFER_FUNC_DEFAULT(pix_mp->colorspace);

		st_info(ST_VIDEO, "w, h = %d, %d, bpp = %d\n", pix_mp->width,
				pix_mp->height, fi->bpp[0]);
		st_info(ST_VIDEO, "i = %d, p = %d, s = 0x%x\n", i,
				pix_mp->num_planes, pix_mp->plane_fmt[0].sizeimage);

	} else {
		for (i = 0; i < video->nformats; i++)
			if (pix->pixelformat == video->formats[i].pixelformat)
				break;

		if (i == video->nformats)
			i = 0; /* default format */

		fi = &video->formats[i];
		width = pix->width;
		height = pix->height;

		memset(pix, 0, sizeof(*pix));

		pix->pixelformat = fi->pixelformat;
		pix->width = clamp_t(u32, width, STFCAMSS_FRAME_MIN_WIDTH,
				STFCAMSS_FRAME_MAX_WIDTH);
		pix->height = clamp_t(u32, height, STFCAMSS_FRAME_MIN_HEIGHT,
				STFCAMSS_FRAME_MAX_HEIGHT);
		bpl = pix->width / fi->hsub[0].numerator *
			fi->hsub[0].denominator * fi->bpp[0] / 8;
		bpl = ALIGN(bpl, video->bpl_alignment);
		pix->bytesperline = bpl;
		pix->sizeimage = pix->height /
			fi->vsub[0].numerator
			* fi->vsub[0].denominator * bpl;

		pix->field = V4L2_FIELD_NONE;
		pix->colorspace = V4L2_COLORSPACE_SRGB;
		pix->flags = 0;
		pix->ycbcr_enc =
			V4L2_MAP_YCBCR_ENC_DEFAULT(pix->colorspace);
		pix->quantization =
			V4L2_MAP_QUANTIZATION_DEFAULT(true,
				pix->colorspace, pix->ycbcr_enc);
		pix->xfer_func =
			V4L2_MAP_XFER_FUNC_DEFAULT(pix->colorspace);

		st_info(ST_VIDEO, "w, h = %d, %d, bpp = %d\n", pix->width,
				pix->height, fi->bpp[0]);
		st_info(ST_VIDEO, "i = %d, s = 0x%x\n", i, pix->sizeimage);
	}
	return 0;
}

static int stf_video_init_format(struct stfcamss_video *video, int is_mp)
{
	int ret;
	struct v4l2_format format = {
		.type = video->type,
		.fmt.pix = {
			.width = 1920,
			.height = 1080,
			.pixelformat = V4L2_PIX_FMT_RGB565,
		},
	};

	ret = __video_try_fmt(video, &format, is_mp);

	if (ret < 0)
		return ret;

	video->active_fmt = format;

	return 0;
}

static int video_queue_setup(struct vb2_queue *q,
	unsigned int *num_buffers, unsigned int *num_planes,
	unsigned int sizes[], struct device *alloc_devs[])
{
	struct stfcamss_video *video = vb2_get_drv_priv(q);
	const struct v4l2_pix_format *format =
			&video->active_fmt.fmt.pix;
	const struct v4l2_pix_format_mplane *format_mp =
			&video->active_fmt.fmt.pix_mp;
	unsigned int i;

	st_debug(ST_VIDEO, "%s, planes = %d\n", __func__, *num_planes);

	if (video->is_mp) {
		if (*num_planes) {
			if (*num_planes != format_mp->num_planes)
				return -EINVAL;

			for (i = 0; i < *num_planes; i++)
				if (sizes[i] <
					format_mp->plane_fmt[i].sizeimage)
					return -EINVAL;

			return 0;
		}

		*num_planes = format_mp->num_planes;

		for (i = 0; i < *num_planes; i++)
			sizes[i] = format_mp->plane_fmt[i].sizeimage;
	} else {
		if (*num_planes) {
			if (*num_planes != 1)
				return -EINVAL;

			if (sizes[0] < format->sizeimage)
				return -EINVAL;
		}

		*num_planes  = 1;
		sizes[0] = format->sizeimage;
		if (!sizes[0])
			st_err(ST_VIDEO, "%s: error size is zero!!!\n", __func__);
	}
	if ((stf_vin_map_isp_pad(video->id, STF_ISP_PAD_SRC)
		== STF_ISP_PAD_SRC_SCD_Y) &&
		sizes[0] < ISP_SCD_Y_BUFFER_SIZE) {
		sizes[0] = ISP_SCD_Y_BUFFER_SIZE;
	}

	st_info(ST_VIDEO, "%s, planes = %d, size = %d\n",
			__func__, *num_planes, sizes[0]);
	return 0;
}

static int video_buf_init(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct stfcamss_video *video = vb2_get_drv_priv(vb->vb2_queue);
	struct stfcamss_buffer *buffer =
		container_of(vbuf, struct stfcamss_buffer, vb);
	const struct v4l2_pix_format *fmt = &video->active_fmt.fmt.pix;
	const struct v4l2_pix_format_mplane *fmt_mp =
				&video->active_fmt.fmt.pix_mp;
	//struct sg_table *sgt;
	dma_addr_t *paddr;
	unsigned int i;

	buffer->sizeimage = 0;

	if (video->is_mp) {
		for (i = 0; i < fmt_mp->num_planes; i++) {
			paddr = vb2_plane_cookie(vb, i);
			buffer->addr[i] = *paddr;
		buffer->sizeimage += vb2_plane_size(vb, i);
		}

		if (fmt_mp->num_planes == 1
			&& (fmt_mp->pixelformat == V4L2_PIX_FMT_NV12
			|| fmt_mp->pixelformat == V4L2_PIX_FMT_NV21
			|| fmt_mp->pixelformat == V4L2_PIX_FMT_NV16
			|| fmt_mp->pixelformat == V4L2_PIX_FMT_NV61))
			buffer->addr[1] = buffer->addr[0] +
					fmt_mp->plane_fmt[0].bytesperline *
					fmt_mp->height;
	} else {
		paddr = vb2_plane_cookie(vb, 0);
		buffer->sizeimage = vb2_plane_size(vb, 0);
		buffer->addr[0] = *paddr;
		if (fmt->pixelformat == V4L2_PIX_FMT_NV12
			|| fmt->pixelformat == V4L2_PIX_FMT_NV21
			|| fmt->pixelformat == V4L2_PIX_FMT_NV16
			|| fmt->pixelformat == V4L2_PIX_FMT_NV61)
			buffer->addr[1] = buffer->addr[0] +
				fmt->bytesperline *
				fmt->height;
	}

	if (stf_vin_map_isp_pad(video->id, STF_ISP_PAD_SRC)
		== STF_ISP_PAD_SRC_SCD_Y)
		buffer->addr[1] = buffer->addr[0] + ISP_YHIST_BUFFER_SIZE;

	return 0;
}

static int video_buf_prepare(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct stfcamss_video *video = vb2_get_drv_priv(vb->vb2_queue);
	const struct v4l2_pix_format *fmt = &video->active_fmt.fmt.pix;
	const struct v4l2_pix_format_mplane *fmt_mp =
					&video->active_fmt.fmt.pix_mp;
	unsigned int i;

	if (video->is_mp) {
		for (i = 0; i < fmt_mp->num_planes; i++) {
			if (fmt_mp->plane_fmt[i].sizeimage
					> vb2_plane_size(vb, i))
				return -EINVAL;

			vb2_set_plane_payload(vb, i,
					fmt_mp->plane_fmt[i].sizeimage);
		}
	} else {
		if (fmt->sizeimage > vb2_plane_size(vb, 0)) {
			st_err(ST_VIDEO, "sizeimage = %d, plane size = %d\n",
				fmt->sizeimage, (unsigned int)vb2_plane_size(vb, 0));
			return -EINVAL;
		}
		vb2_set_plane_payload(vb, 0, fmt->sizeimage);
	}

	vbuf->field = V4L2_FIELD_NONE;

	return 0;
}

static void video_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct stfcamss_video *video = vb2_get_drv_priv(vb->vb2_queue);
	struct stfcamss_buffer *buffer =
		container_of(vbuf, struct stfcamss_buffer, vb);

	video->ops->queue_buffer(video, buffer);
}

static int video_mbus_to_pix_mp(const struct v4l2_mbus_framefmt *mbus,
				struct v4l2_pix_format_mplane *pix,
				const struct stfcamss_format_info *f,
				unsigned int alignment)
{
	unsigned int i;
	u32 bytesperline;

	memset(pix, 0, sizeof(*pix));
	v4l2_fill_pix_format_mplane(pix, mbus);
	pix->pixelformat = f->pixelformat;
	pix->num_planes = f->planes;
	for (i = 0; i < pix->num_planes; i++) {
		bytesperline = pix->width / f->hsub[i].numerator *
			f->hsub[i].denominator * f->bpp[i] / 8;
		bytesperline = ALIGN(bytesperline, alignment);
		pix->plane_fmt[i].bytesperline = bytesperline;
		pix->plane_fmt[i].sizeimage = pix->height /
				f->vsub[i].numerator * f->vsub[i].denominator *
				bytesperline;
	}

	return 0;
}

static int video_mbus_to_pix(const struct v4l2_mbus_framefmt *mbus,
			struct v4l2_pix_format *pix,
			const struct stfcamss_format_info *f,
			unsigned int alignment)
{
	u32 bytesperline;

	memset(pix, 0, sizeof(*pix));
	v4l2_fill_pix_format(pix, mbus);
	pix->pixelformat = f->pixelformat;
	bytesperline = pix->width / f->hsub[0].numerator *
		f->hsub[0].denominator * f->bpp[0] / 8;
	bytesperline = ALIGN(bytesperline, alignment);
	pix->bytesperline = bytesperline;
	pix->sizeimage = pix->height /
			f->vsub[0].numerator * f->vsub[0].denominator *
			bytesperline;
	return 0;
}

static struct v4l2_subdev *video_remote_subdev(
		struct stfcamss_video *video, u32 *pad)
{
	struct media_pad *remote;

	remote = media_entity_remote_pad(&video->pad);

	if (!remote || !is_media_entity_v4l2_subdev(remote->entity))
		return NULL;

	if (pad)
		*pad = remote->index;

	return media_entity_to_v4l2_subdev(remote->entity);
}

static int video_get_subdev_format(struct stfcamss_video *video,
		struct v4l2_format *format)
{
	struct v4l2_pix_format *pix = &video->active_fmt.fmt.pix;
	struct v4l2_pix_format_mplane *pix_mp =
				&video->active_fmt.fmt.pix_mp;
	struct v4l2_subdev_format fmt;
	struct v4l2_subdev *subdev;
	u32 pixelformat;
	u32 pad;
	int ret;

	subdev = video_remote_subdev(video, &pad);
	if (subdev == NULL)
		return -EPIPE;

	fmt.pad = pad;
	fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;

	ret = v4l2_subdev_call(subdev, pad, get_fmt, NULL, &fmt);
	if (ret)
		return ret;

	if (video->is_mp)
		pixelformat = pix_mp->pixelformat;
	else
		pixelformat = pix->pixelformat;
	ret = video_find_format(fmt.format.code, pixelformat,
				video->formats, video->nformats);
	if (ret < 0)
		return ret;

	format->type = video->type;

	if (video->is_mp)
		return video_mbus_to_pix_mp(&fmt.format, &format->fmt.pix_mp,
				&video->formats[ret], video->bpl_alignment);
	else
		return video_mbus_to_pix(&fmt.format, &format->fmt.pix,
				&video->formats[ret], video->bpl_alignment);
}

static int video_check_format(struct stfcamss_video *video)
{
	struct v4l2_pix_format *pix = &video->active_fmt.fmt.pix;
	struct v4l2_pix_format_mplane *pix_mp =
				&video->active_fmt.fmt.pix_mp;
	struct v4l2_format format;
	struct v4l2_pix_format *sd_pix = &format.fmt.pix;
	struct v4l2_pix_format_mplane *sd_pix_mp = &format.fmt.pix_mp;
	int ret;

	if (video->is_mp) {
		sd_pix_mp->pixelformat = pix_mp->pixelformat;
		ret = video_get_subdev_format(video, &format);
		if (ret < 0)
			return ret;

		if (pix_mp->pixelformat != sd_pix_mp->pixelformat ||
			pix_mp->height > sd_pix_mp->height ||
			pix_mp->width > sd_pix_mp->width ||
			pix_mp->num_planes != sd_pix_mp->num_planes ||
			pix_mp->field != format.fmt.pix_mp.field) {
			st_err(ST_VIDEO,
				"%s, not match:\n"
				"0x%x 0x%x\n0x%x 0x%x\n0x%x 0x%x\n",
				__func__,
				pix_mp->pixelformat, sd_pix_mp->pixelformat,
				pix_mp->height, sd_pix_mp->height,
				pix_mp->field, format.fmt.pix_mp.field);
			return -EPIPE;
		}

	} else {
		sd_pix->pixelformat = pix->pixelformat;
		ret = video_get_subdev_format(video, &format);
		if (ret < 0)
			return ret;

		if (pix->pixelformat != sd_pix->pixelformat ||
			pix->height > sd_pix->height ||
			pix->width > sd_pix->width ||
			pix->field != format.fmt.pix.field) {
			st_err(ST_VIDEO,
				"%s, not match:\n"
				"0x%x 0x%x\n0x%x 0x%x\n0x%x 0x%x\n",
				__func__,
				pix->pixelformat, sd_pix->pixelformat,
				pix->height, sd_pix->height,
				pix->field, format.fmt.pix.field);
			return -EPIPE;
		}
	}
	return 0;
}

static int video_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct stfcamss_video *video = vb2_get_drv_priv(q);
	struct video_device *vdev = &video->vdev;
	struct media_entity *entity;
	struct media_pad *pad;
	struct v4l2_subdev *subdev;
	int ret;

	ret = media_pipeline_start(&vdev->entity, &video->stfcamss->pipe);
	if (ret < 0) {
		st_err(ST_VIDEO,
			"Failed to media_pipeline_start: %d\n", ret);
		return ret;
	}

	ret = video_check_format(video);
	if (ret < 0)
		goto error;
	entity = &vdev->entity;
	while (1) {
		pad = &entity->pads[0];
		if (!(pad->flags & MEDIA_PAD_FL_SINK))
			break;

		pad = media_entity_remote_pad(pad);
		if (!pad || !is_media_entity_v4l2_subdev(pad->entity))
			break;

		entity = pad->entity;
		subdev = media_entity_to_v4l2_subdev(entity);

		ret = v4l2_subdev_call(subdev, video, s_stream, 1);
		if (ret < 0 && ret != -ENOIOCTLCMD)
			goto error;
	}
	return 0;

error:
	media_pipeline_stop(&vdev->entity);
	video->ops->flush_buffers(video, VB2_BUF_STATE_QUEUED);
	return ret;
}

static void video_stop_streaming(struct vb2_queue *q)
{
	struct stfcamss_video *video = vb2_get_drv_priv(q);
	struct video_device *vdev = &video->vdev;
	struct media_entity *entity;
	struct media_pad *pad;
	struct v4l2_subdev *subdev;

	entity = &vdev->entity;
	while (1) {
		pad = &entity->pads[0];
		if (!(pad->flags & MEDIA_PAD_FL_SINK))
			break;

		pad = media_entity_remote_pad(pad);
		if (!pad || !is_media_entity_v4l2_subdev(pad->entity))
			break;

		entity = pad->entity;
		subdev = media_entity_to_v4l2_subdev(entity);

		v4l2_subdev_call(subdev, video, s_stream, 0);
	}

	media_pipeline_stop(&vdev->entity);
	video->ops->flush_buffers(video, VB2_BUF_STATE_ERROR);
}

static const struct vb2_ops stf_video_vb2_q_ops = {
	.queue_setup     = video_queue_setup,
	.wait_prepare    = vb2_ops_wait_prepare,
	.wait_finish     = vb2_ops_wait_finish,
	.buf_init        = video_buf_init,
	.buf_prepare     = video_buf_prepare,
	.buf_queue       = video_buf_queue,
	.start_streaming = video_start_streaming,
	.stop_streaming  = video_stop_streaming,
};

/* -----------------------------------------------------
 * V4L2 ioctls
 */

static int getcrop_pad_id(int video_id)
{
	return stf_vin_map_isp_pad(video_id, STF_ISP_PAD_SRC);
}

static int video_querycap(struct file *file, void *fh,
			struct v4l2_capability *cap)
{
	struct stfcamss_video *video = video_drvdata(file);

	strscpy(cap->driver, "stf camss", sizeof(cap->driver));
	strscpy(cap->card, "Starfive Camera Subsystem", sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info), "platform:%s",
		dev_name(video->stfcamss->dev));
	return 0;
}

static int video_get_unique_pixelformat_by_index(struct stfcamss_video *video,
						int ndx)
{
	int i, j, k;

	/* find index "i" of "k"th unique pixelformat in formats array */
	k = -1;
	for (i = 0; i < video->nformats; i++) {
		for (j = 0; j < i; j++) {
			if (video->formats[i].pixelformat ==
				video->formats[j].pixelformat)
				break;
		}

		if (j == i)
			k++;

		if (k == ndx)
			return i;
	}

	return -EINVAL;
}

static int video_get_pixelformat_by_mbus_code(struct stfcamss_video *video,
						u32 mcode)
{
	int i;

	for (i = 0; i < video->nformats; i++) {
		if (video->formats[i].code == mcode)
			return i;
	}

	return -EINVAL;
}

static int video_enum_fmt(struct file *file, void *fh, struct v4l2_fmtdesc *f)
{
	struct stfcamss_video *video = video_drvdata(file);
	int i;

	st_debug(ST_VIDEO, "%s:\n0x%x 0x%x\n 0x%x, 0x%x\n0x%x\n",
		__func__,
		f->type, video->type,
		f->index, video->nformats,
		f->mbus_code);

	if (f->type != video->type)
		return -EINVAL;
	if (f->index >= video->nformats)
		return -EINVAL;

	if (f->mbus_code) {
		/* Each entry in formats[] table has unique mbus_code */
		if (f->index > 0)
			return -EINVAL;

		i = video_get_pixelformat_by_mbus_code(video, f->mbus_code);
	} else {
		i = video_get_unique_pixelformat_by_index(video, f->index);
	}

	if (i < 0)
		return -EINVAL;

	f->pixelformat = video->formats[i].pixelformat;

	return 0;
}

static int video_enum_framesizes(struct file *file, void *fh,
				struct v4l2_frmsizeenum *fsize)
{
	struct v4l2_subdev_frame_size_enum fse = {0};
	struct v4l2_subdev_mbus_code_enum code = {0};
	struct stfcamss_video *video = video_drvdata(file);
	struct video_device *vdev = &video->vdev;
	struct media_entity *entity = &vdev->entity;
	struct media_entity *sensor;
	struct v4l2_subdev *subdev;
	struct media_pad *pad;
	bool support_selection = false;
	int i;
	int ret;

	for (i = 0; i < video->nformats; i++) {
		if (video->formats[i].pixelformat == fsize->pixel_format)
			break;
	}

	if (i == video->nformats)
		return -EINVAL;

	entity = &vdev->entity;
	while (1) {
		pad = &entity->pads[0];
		if (!(pad->flags & MEDIA_PAD_FL_SINK))
			break;

		pad = media_entity_remote_pad(pad);
		if (!pad || !is_media_entity_v4l2_subdev(pad->entity))
			break;

		entity = pad->entity;
		subdev = media_entity_to_v4l2_subdev(entity);

		if (subdev->ops->pad->set_selection) {
			support_selection = true;
			break;
		}
	}

	if (support_selection) {
		if (fsize->index)
			return -EINVAL;
		fsize->type = V4L2_FRMSIZE_TYPE_CONTINUOUS;
		fsize->stepwise.min_width = STFCAMSS_FRAME_MIN_WIDTH;
		fsize->stepwise.max_width = STFCAMSS_FRAME_MAX_WIDTH;
		fsize->stepwise.min_height = STFCAMSS_FRAME_MIN_HEIGHT;
		fsize->stepwise.max_height = STFCAMSS_FRAME_MAX_HEIGHT;
		fsize->stepwise.step_width = 1;
		fsize->stepwise.step_height = 1;
	} else {
		entity = &vdev->entity;
		sensor = stfcamss_find_sensor(entity);
		if (!sensor)
			return -ENOTTY;

		subdev = media_entity_to_v4l2_subdev(sensor);
		code.index = 0;
		code.which = V4L2_SUBDEV_FORMAT_ACTIVE;
		ret = v4l2_subdev_call(subdev, pad, enum_mbus_code, NULL, &code);
		if (ret < 0)
			return -EINVAL;
		fse.index = fsize->index;
		fse.code = code.code;
		fse.which = V4L2_SUBDEV_FORMAT_ACTIVE;
		ret = v4l2_subdev_call(subdev, pad, enum_frame_size, NULL, &fse);
		if (ret < 0)
			return -EINVAL;
		fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
		fsize->discrete.width = fse.min_width;
		fsize->discrete.height = fse.min_height;
	}

	return 0;
}

static int video_enum_frameintervals(struct file *file, void *fh,
				struct v4l2_frmivalenum *fival)
{
	int ret = 0;
	struct stfcamss_video *video = video_drvdata(file);
	struct video_device *vdev = &video->vdev;
	struct media_entity *entity = &vdev->entity;
	struct media_entity *sensor;
	struct v4l2_subdev *subdev;
	struct v4l2_subdev_mbus_code_enum code = {0};
	struct v4l2_subdev_frame_interval_enum fie = {0};

	sensor = stfcamss_find_sensor(entity);
	if (!sensor)
		return -ENOTTY;
	fie.index = fival->index;
	fie.width = fival->width;
	fie.height = fival->height;
	fie.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	subdev = media_entity_to_v4l2_subdev(sensor);

	code.index = 0;
	code.which = V4L2_SUBDEV_FORMAT_ACTIVE;

	/* Don't care about the code, just find by pixelformat */
	ret = video_find_format(0, fival->pixel_format,
				video->formats, video->nformats);
	if (ret < 0)
		return -EINVAL;

	ret = v4l2_subdev_call(subdev, pad, enum_mbus_code, NULL, &code);
	if (ret < 0)
		return -EINVAL;

	fie.code = code.code;
	ret = v4l2_subdev_call(subdev, pad, enum_frame_interval, NULL, &fie);
	if (ret < 0)
		return ret;

	fival->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	fival->discrete = fie.interval;

	return 0;
}

static int video_g_fmt(struct file *file, void *fh, struct v4l2_format *f)
{
	struct stfcamss_video *video = video_drvdata(file);

	st_debug(ST_VIDEO, "%s, fmt.type = 0x%x\n", __func__, f->type);
	st_debug(ST_VIDEO, "%s, active_fmt.type = 0x%x,0x%x\n",
			__func__, video->active_fmt.type,
			video->active_fmt.fmt.pix.pixelformat);
	*f = video->active_fmt;
	return 0;
}

static int video_g_fmt_mp(struct file *file, void *fh, struct v4l2_format *f)
{
	struct stfcamss_video *video = video_drvdata(file);

	st_debug(ST_VIDEO, "%s, fmt.type = 0x%x\n", __func__, f->type);
	st_debug(ST_VIDEO, "%s, active_fmt.type = 0x%x\n",
			__func__, video->active_fmt.type);
	*f = video->active_fmt;
	return 0;
}

static int video_entity_s_fmt(struct stfcamss_video *video,
			struct media_entity *entity,
			struct v4l2_subdev_state *state,
			struct v4l2_subdev_format *fmt)
{
	struct v4l2_subdev *subdev;
	struct media_pad *pad;
	struct v4l2_mbus_framefmt *mf = &fmt->format;
	struct v4l2_subdev_format fmt_src;
	u32 width, height, code;
	int ret, index = 0;

	code = mf->code;
	width = mf->width;
	height = mf->height;
	subdev = media_entity_to_v4l2_subdev(entity);
	while (1) {
		if (index >= entity->num_pads)
			break;
		pad = &entity->pads[index];
		pad = media_entity_remote_pad(pad);
		if (pad && is_media_entity_v4l2_subdev(pad->entity)) {
			fmt->pad = index;
			ret = v4l2_subdev_call(subdev, pad, set_fmt, state, fmt);
			if (mf->code != code ||
				mf->width != width || mf->height != height) {
				st_warn(ST_VIDEO,
					"\"%s\":%d pad fmt has been"
					" changed to 0x%x %ux%u\n",
					subdev->name, fmt->pad, mf->code,
					mf->width, mf->height);
			}
			if (index) {
				fmt_src.pad = index;
				fmt_src.which = V4L2_SUBDEV_FORMAT_ACTIVE,
				ret = v4l2_subdev_call(subdev, pad, get_fmt, state, &fmt_src);
				if (ret)
					return ret;

				fmt->format.code = fmt_src.format.code;
				ret = video_entity_s_fmt(video, pad->entity, state, fmt);
			}
		}

		if (ret < 0 && ret != -ENOIOCTLCMD)
			break;
		index++;
	}
	return ret;
}

static int video_pipeline_s_fmt(struct stfcamss_video *video,
			struct v4l2_subdev_state *state,
			struct v4l2_format *f)
{
	struct video_device *vdev = &video->vdev;
	struct media_entity *entity = &vdev->entity;
	struct v4l2_subdev *subdev;
	int ret, index;
	struct v4l2_subdev_format fmt = {
		.pad = 0,
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
		.reserved = {getcrop_pad_id(video->id)}
	};
	struct v4l2_mbus_framefmt *mf = &fmt.format;
	struct v4l2_pix_format *pix = &f->fmt.pix;
	struct v4l2_pix_format_mplane *pix_mp = &f->fmt.pix_mp;
	struct media_entity *sensor;
	u32 width, height;
	struct media_pad *pad;

	/* pix to mbus format */
	if (video->is_mp) {
		index = video_find_format(mf->code,
					pix_mp->pixelformat,
					video->formats, video->nformats);
		if (index < 0)
			return index;
		v4l2_fill_mbus_format_mplane(mf, pix_mp);
		mf->code = video->formats[index].code;
	} else {
		index = video_find_format(mf->code,
					pix->pixelformat,
					video->formats, video->nformats);
		if (index < 0)
			return index;
		v4l2_fill_mbus_format(mf, pix, video->formats[index].code);
	}

	width = mf->width;
	height = mf->height;

	sensor = stfcamss_find_sensor(entity);
	if (!sensor) {
		st_err(ST_VIDEO, "Can't find sensor\n");
		return -ENOTTY;
	}

	subdev = media_entity_to_v4l2_subdev(sensor);
	ret = v4l2_subdev_call(subdev, pad, get_fmt, state, &fmt);
	if (ret)
		return ret;

	/*
	 * Starting from sensor subdevice, walk within
	 * pipeline and set format on each subdevice
	 */
	pad = media_entity_remote_pad(&sensor->pads[0]);
	ret = video_entity_s_fmt(video, pad->entity, state, &fmt);
	if (ret < 0 && ret != -ENOIOCTLCMD)
		return ret;

	index = video_find_format(mf->code,
				video->formats[index].pixelformat,
				video->formats, video->nformats);
	st_debug(ST_VIDEO, "%s, code=%x, index=%d\n",
			__func__, mf->code, index);

	if (index < 0)
		return index;

	if (video->is_mp)
		video_mbus_to_pix_mp(mf, pix_mp,
				&video->formats[index], video->bpl_alignment);
	else
		video_mbus_to_pix(mf, pix,
				&video->formats[index], video->bpl_alignment);

	ret = __video_try_fmt(video, f, video->is_mp);
	if (ret < 0)
		return ret;

	return 0;
}

static int video_s_fmt(struct file *file, void *fh, struct v4l2_format *f)
{
	struct stfcamss_video *video = video_drvdata(file);
	int ret;

	st_debug(ST_VIDEO, "%s, fmt.type = 0x%x, v4l2fmt=%x\n",
			__func__, f->type, f->fmt.pix.pixelformat);

	if (vb2_is_busy(&video->vb2_q))
		return -EBUSY;

	ret = __video_try_fmt(video, f, false);
	if (ret < 0)
		return ret;

	ret = video_pipeline_s_fmt(video, NULL, f);

	st_debug(ST_VIDEO, "%s, pixelformat=0x%x, ret=%d\n",
			__func__, f->fmt.pix.pixelformat, ret);
	if (ret < 0)
		return ret;

	video->active_fmt = *f;

	return 0;
}

static int video_s_fmt_mp(struct file *file, void *fh, struct v4l2_format *f)
{
	struct stfcamss_video *video = video_drvdata(file);
	int ret;

	st_debug(ST_VIDEO, "%s, fmt.type = 0x%x\n", __func__, f->type);
	if (vb2_is_busy(&video->vb2_q))
		return -EBUSY;

	ret = __video_try_fmt(video, f, true);
	if (ret < 0)
		return ret;

	ret = video_pipeline_s_fmt(video, NULL, f);
	if (ret < 0)
		return ret;

	video->active_fmt = *f;

	return 0;
}

static int video_try_fmt(struct file *file,
		void *fh, struct v4l2_format *f)
{
	struct stfcamss_video *video = video_drvdata(file);

	return __video_try_fmt(video, f, false);
}

static int video_try_fmt_mp(struct file *file,
		void *fh, struct v4l2_format *f)
{
	struct stfcamss_video *video = video_drvdata(file);

	return __video_try_fmt(video, f, true);
}

static int video_enum_input(struct file *file, void *fh,
			struct v4l2_input *input)
{
	if (input->index > 0)
		return -EINVAL;

	strscpy(input->name, "camera", sizeof(input->name));
	input->type = V4L2_INPUT_TYPE_CAMERA;

	return 0;
}

static int video_g_input(struct file *file, void *fh, unsigned int *input)
{
	*input = 0;

	return 0;
}

static int video_s_input(struct file *file, void *fh, unsigned int input)
{
	return input == 0 ? 0 : -EINVAL;
}

static int video_g_parm(struct file *file, void *priv,
			struct v4l2_streamparm *p)
{
	struct stfcamss_video *video = video_drvdata(file);
	struct video_device *vdev = &video->vdev;
	struct media_entity *entity;
	struct v4l2_subdev *subdev;
	struct media_pad *pad;
	int ret, is_support = 0;

	entity = &vdev->entity;
	while (1) {
		pad = &entity->pads[0];
		if (!(pad->flags & MEDIA_PAD_FL_SINK))
			break;

		pad = media_entity_remote_pad(pad);
		if (!pad || !is_media_entity_v4l2_subdev(pad->entity))
			break;

		entity = pad->entity;
		subdev = media_entity_to_v4l2_subdev(entity);

		ret = v4l2_g_parm_cap(vdev, subdev, p);
		if (ret < 0 && ret != -ENOIOCTLCMD)
			break;
		if (!ret)
			is_support = 1;
	}

	return is_support ? 0 : ret;
}

static int video_s_parm(struct file *file, void *priv,
			struct v4l2_streamparm *p)
{
	struct stfcamss_video *video = video_drvdata(file);
	struct video_device *vdev = &video->vdev;
	struct media_entity *entity;
	struct v4l2_subdev *subdev;
	struct media_pad *pad;
	struct v4l2_streamparm tmp_p;
	int ret, is_support = 0;

	entity = &vdev->entity;
	while (1) {
		pad = &entity->pads[0];
		if (!(pad->flags & MEDIA_PAD_FL_SINK))
			break;

		pad = media_entity_remote_pad(pad);
		if (!pad || !is_media_entity_v4l2_subdev(pad->entity))
			break;

		entity = pad->entity;
		subdev = media_entity_to_v4l2_subdev(entity);

		tmp_p = *p;
		ret = v4l2_s_parm_cap(vdev, subdev, &tmp_p);
		if (ret < 0 && ret != -ENOIOCTLCMD)
			break;
		if (!ret) {
			is_support = 1;
			*p = tmp_p;
		}
	}

	return is_support ? 0 : ret;
}

/* Crop ioctls */
int video_g_pixelaspect(struct file *file, void *fh,
			    int buf_type, struct v4l2_fract *aspect)
{
	return 0;
}

int video_g_selection(struct file *file, void *fh,
			  struct v4l2_selection *s)
{
	struct stfcamss_video *video = video_drvdata(file);
	struct video_device *vdev = &video->vdev;
	struct media_entity *entity;
	struct v4l2_subdev *subdev;
	struct media_pad *pad;
	struct v4l2_subdev_selection sel = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
		.pad = getcrop_pad_id(video->id),
		.target = s->target,
		.r = s->r,
		.flags = s->flags,
	};
	int ret;

	st_debug(ST_VIDEO, "%s, target = 0x%x, 0x%x\n",
			__func__, sel.target, s->target);
	if (s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE
		&& s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		return -EINVAL;

	entity = &vdev->entity;
	while (1) {
		pad = &entity->pads[0];
		if (!(pad->flags & MEDIA_PAD_FL_SINK))
			break;

		pad = media_entity_remote_pad(pad);
		if (!pad || !is_media_entity_v4l2_subdev(pad->entity))
			break;

		entity = pad->entity;
		subdev = media_entity_to_v4l2_subdev(entity);

		ret = v4l2_subdev_call(subdev, pad, get_selection, NULL, &sel);
		if (!ret) {
			s->r = sel.r;
			s->flags = sel.flags;
			break;
		}
		if (ret != -ENOIOCTLCMD)
			break;
	}

	return ret;
}

int video_s_selection(struct file *file, void *fh,
			struct v4l2_selection *s)
{
	struct stfcamss_video *video = video_drvdata(file);
	struct video_device *vdev = &video->vdev;
	struct media_entity *entity;
	struct v4l2_subdev *subdev;
	struct media_pad *pad;
	struct v4l2_subdev_selection sel = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
		.pad = getcrop_pad_id(video->id),
		.target = s->target,
		.r = s->r,
		.flags = s->flags,
	};
	struct v4l2_pix_format *format = &video->active_fmt.fmt.pix;
	struct v4l2_pix_format_mplane *format_mp =
						&video->active_fmt.fmt.pix_mp;
	int ret;

	st_debug(ST_VIDEO, "%s, target = 0x%x, 0x%x\n",
			__func__, sel.target, s->target);
	if (s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE
		&& s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		return -EINVAL;

	entity = &vdev->entity;
	while (1) {
		pad = &entity->pads[0];
		if (!(pad->flags & MEDIA_PAD_FL_SINK))
			break;

		pad = media_entity_remote_pad(pad);
		if (!pad || !is_media_entity_v4l2_subdev(pad->entity))
			break;

		entity = pad->entity;
		subdev = media_entity_to_v4l2_subdev(entity);

		ret = v4l2_subdev_call(subdev, pad, set_selection, NULL, &sel);
		if (!ret) {
			s->r = sel.r;
			s->flags = sel.flags;
			format->width = s->r.width;
			format->height = s->r.height;
			format_mp->width = s->r.width;
			format_mp->height = s->r.height;
			ret = __video_try_fmt(video, &video->active_fmt,
					video->is_mp);
			if (ret < 0)
				return ret;
			break;
		}
		if (ret != -ENOIOCTLCMD)
			break;
	}

	st_debug(ST_VIDEO, "ret = 0x%x, -EINVAL = 0x%x\n", ret, -EINVAL);

	return ret;
}

static const struct v4l2_ioctl_ops stf_vid_ioctl_ops = {
	.vidioc_querycap                = video_querycap,
	.vidioc_enum_fmt_vid_cap        = video_enum_fmt,
	.vidioc_enum_framesizes         = video_enum_framesizes,
	.vidioc_enum_frameintervals     = video_enum_frameintervals,
	.vidioc_g_fmt_vid_cap           = video_g_fmt,
	.vidioc_s_fmt_vid_cap           = video_s_fmt,
	.vidioc_try_fmt_vid_cap         = video_try_fmt,
	.vidioc_reqbufs                 = vb2_ioctl_reqbufs,
	.vidioc_querybuf                = vb2_ioctl_querybuf,
	.vidioc_qbuf                    = vb2_ioctl_qbuf,
	.vidioc_expbuf                  = vb2_ioctl_expbuf,
	.vidioc_dqbuf                   = vb2_ioctl_dqbuf,
	.vidioc_create_bufs             = vb2_ioctl_create_bufs,
	.vidioc_prepare_buf             = vb2_ioctl_prepare_buf,
	.vidioc_streamon                = vb2_ioctl_streamon,
	.vidioc_streamoff               = vb2_ioctl_streamoff,
	.vidioc_enum_input              = video_enum_input,
	.vidioc_g_input                 = video_g_input,
	.vidioc_s_input                 = video_s_input,
	.vidioc_g_parm                  = video_g_parm,
	.vidioc_s_parm                  = video_s_parm,
	.vidioc_s_selection             = video_s_selection,
	.vidioc_g_selection             = video_g_selection,
};

static const struct v4l2_ioctl_ops stf_vid_ioctl_ops_mp = {
	.vidioc_querycap                = video_querycap,
	.vidioc_enum_fmt_vid_cap        = video_enum_fmt,
	.vidioc_enum_framesizes         = video_enum_framesizes,
	.vidioc_enum_frameintervals     = video_enum_frameintervals,
	.vidioc_g_fmt_vid_cap_mplane    = video_g_fmt_mp,
	.vidioc_s_fmt_vid_cap_mplane    = video_s_fmt_mp,
	.vidioc_try_fmt_vid_cap_mplane  = video_try_fmt_mp,
	.vidioc_reqbufs                 = vb2_ioctl_reqbufs,
	.vidioc_querybuf                = vb2_ioctl_querybuf,
	.vidioc_qbuf                    = vb2_ioctl_qbuf,
	.vidioc_expbuf                  = vb2_ioctl_expbuf,
	.vidioc_dqbuf                   = vb2_ioctl_dqbuf,
	.vidioc_create_bufs             = vb2_ioctl_create_bufs,
	.vidioc_prepare_buf             = vb2_ioctl_prepare_buf,
	.vidioc_streamon                = vb2_ioctl_streamon,
	.vidioc_streamoff               = vb2_ioctl_streamoff,
	.vidioc_enum_input              = video_enum_input,
	.vidioc_g_input                 = video_g_input,
	.vidioc_s_input                 = video_s_input,
	.vidioc_g_parm                  = video_g_parm,
	.vidioc_s_parm                  = video_s_parm,
	.vidioc_s_selection             = video_s_selection,
	.vidioc_g_selection             = video_g_selection,
};

static const struct v4l2_ioctl_ops stf_vid_ioctl_ops_out = {
	.vidioc_querycap                = video_querycap,
	.vidioc_enum_fmt_vid_out        = video_enum_fmt,
	.vidioc_enum_framesizes         = video_enum_framesizes,
	.vidioc_enum_frameintervals     = video_enum_frameintervals,
	.vidioc_g_fmt_vid_out           = video_g_fmt,
	.vidioc_s_fmt_vid_out           = video_s_fmt,
	.vidioc_try_fmt_vid_out         = video_try_fmt,
	.vidioc_reqbufs                 = vb2_ioctl_reqbufs,
	.vidioc_querybuf                = vb2_ioctl_querybuf,
	.vidioc_qbuf                    = vb2_ioctl_qbuf,
	.vidioc_expbuf                  = vb2_ioctl_expbuf,
	.vidioc_dqbuf                   = vb2_ioctl_dqbuf,
	.vidioc_create_bufs             = vb2_ioctl_create_bufs,
	.vidioc_prepare_buf             = vb2_ioctl_prepare_buf,
	.vidioc_streamon                = vb2_ioctl_streamon,
	.vidioc_streamoff               = vb2_ioctl_streamoff,
};

static int video_open(struct file *file)
{
	struct video_device *vdev = video_devdata(file);
	struct stfcamss_video *video = video_drvdata(file);
	struct v4l2_fh *vfh;
	int ret;

	mutex_lock(&video->lock);

	vfh = kzalloc(sizeof(*vfh), GFP_KERNEL);
	if (vfh == NULL) {
		ret = -ENOMEM;
		goto error_alloc;
	}

	v4l2_fh_init(vfh, vdev);
	v4l2_fh_add(vfh);

	file->private_data = vfh;

	if (!video->pm_count) {
		ret = v4l2_pipeline_pm_get(&vdev->entity);
		if (ret < 0) {
			st_err(ST_VIDEO,
				"Failed to power up pipeline: %d\n", ret);
			goto error_pm_use;
		}
	}

	video->pm_count++;

	mutex_unlock(&video->lock);

	return 0;

error_pm_use:
	v4l2_fh_release(file);
error_alloc:
	mutex_unlock(&video->lock);
	return ret;
}

static int video_release(struct file *file)
{
	struct video_device *vdev = video_devdata(file);
	struct stfcamss_video *video = video_drvdata(file);

	vb2_fop_release(file);

	video->pm_count--;

	if (!video->pm_count)
		v4l2_pipeline_pm_put(&vdev->entity);

	file->private_data = NULL;

	return 0;
}

static const struct v4l2_file_operations stf_vid_fops = {
	.owner          = THIS_MODULE,
	.unlocked_ioctl = video_ioctl2,
	.open           = video_open,
	.release        = video_release,
	.poll           = vb2_fop_poll,
	.mmap           = vb2_fop_mmap,
	.read           = vb2_fop_read,
};

static void stf_video_release(struct video_device *vdev)
{
	struct stfcamss_video *video = video_get_drvdata(vdev);

	media_entity_cleanup(&vdev->entity);

	mutex_destroy(&video->q_lock);
	mutex_destroy(&video->lock);
}

int stf_video_register(struct stfcamss_video *video,
			struct v4l2_device *v4l2_dev,
			const char *name, int is_mp)
{
	struct video_device *vdev;
	struct vb2_queue *q;
	struct media_pad *pad = &video->pad;
	int ret;
	enum isp_pad_id isp_pad;

	vdev = &video->vdev;

	mutex_init(&video->q_lock);

	q = &video->vb2_q;
	q->drv_priv = video;
	q->mem_ops = &vb2_dma_contig_memops;
	q->ops = &stf_video_vb2_q_ops;
	//q->type = is_mp ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE :
	//	V4L2_BUF_TYPE_VIDEO_CAPTURE;
	q->type = video->type;
	q->io_modes = VB2_DMABUF | VB2_MMAP | VB2_READ;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->buf_struct_size = sizeof(struct stfcamss_buffer);
	q->dev = video->stfcamss->dev;
	q->lock = &video->q_lock;
	q->min_buffers_needed = STFCAMSS_MIN_BUFFERS;
	ret = vb2_queue_init(q);
	if (ret < 0) {
		st_err(ST_VIDEO,
			"Failed to init vb2 queue: %d\n", ret);
		goto err_vb2_init;
	}

	pad->flags = MEDIA_PAD_FL_SINK;
	ret = media_entity_pads_init(&vdev->entity, 1, pad);
	if (ret < 0) {
		st_err(ST_VIDEO,
			"Failed to init video entity: %d\n",
			ret);
		goto err_vb2_init;
	}

	mutex_init(&video->lock);

	isp_pad = stf_vin_map_isp_pad(video->id, STF_ISP_PAD_SRC);
	if (video->id == VIN_LINE_WR) {
		video->formats = formats_pix_st7110_wr;
		video->nformats = ARRAY_SIZE(formats_pix_st7110_wr);
		video->bpl_alignment = STFCAMSS_FRAME_WIDTH_ALIGN_8;
	} else if (isp_pad == STF_ISP_PAD_SRC
		|| isp_pad == STF_ISP_PAD_SRC_SS0
		|| isp_pad == STF_ISP_PAD_SRC_SS1) {
		video->formats = formats_pix_st7110_isp;
		video->nformats = ARRAY_SIZE(formats_pix_st7110_isp);
		video->bpl_alignment = STFCAMSS_FRAME_WIDTH_ALIGN_8;
	} else if (isp_pad == STF_ISP_PAD_SRC_ITIW
		|| isp_pad == STF_ISP_PAD_SRC_ITIR) {
		video->formats = formats_st7110_isp_iti;
		video->nformats = ARRAY_SIZE(formats_st7110_isp_iti);
		video->bpl_alignment = STFCAMSS_FRAME_WIDTH_ALIGN_8;
	} else { // raw/scdump/yhist
		video->formats = formats_raw_st7110_isp;
		video->nformats = ARRAY_SIZE(formats_raw_st7110_isp);
		video->bpl_alignment = STFCAMSS_FRAME_WIDTH_ALIGN_128;
	}
	video->is_mp = is_mp;

	ret = stf_video_init_format(video, is_mp);
	if (ret < 0) {
		st_err(ST_VIDEO, "Failed to init format: %d\n", ret);
		goto err_vid_init_format;
	}

	vdev->fops = &stf_vid_fops;
	if (isp_pad == STF_ISP_PAD_SRC_ITIR) {
		vdev->device_caps = V4L2_CAP_VIDEO_OUTPUT;
		vdev->vfl_dir = VFL_DIR_TX;
	} else {
		vdev->device_caps = is_mp ? V4L2_CAP_VIDEO_CAPTURE_MPLANE :
			V4L2_CAP_VIDEO_CAPTURE;
		vdev->vfl_dir = VFL_DIR_RX;
	}
	vdev->device_caps |= V4L2_CAP_STREAMING | V4L2_CAP_READWRITE;
	if (video->type == V4L2_CAP_VIDEO_OUTPUT)
		vdev->ioctl_ops = &stf_vid_ioctl_ops_out;
	else
		vdev->ioctl_ops = is_mp ? &stf_vid_ioctl_ops_mp : &stf_vid_ioctl_ops;
	vdev->release = stf_video_release;
	vdev->v4l2_dev = v4l2_dev;
	vdev->queue = &video->vb2_q;
	vdev->lock = &video->lock;
	//strlcpy(vdev->name, name, sizeof(vdev->name));
	strscpy(vdev->name, name, sizeof(vdev->name));

	ret = video_register_device(vdev, VFL_TYPE_VIDEO, video->id);
	if (ret < 0) {
		st_err(ST_VIDEO,
			"Failed to register video device: %d\n",
			ret);
		goto err_vid_reg;
	}

	video_set_drvdata(vdev, video);
	return 0;

err_vid_reg:
err_vid_init_format:
	media_entity_cleanup(&vdev->entity);
	mutex_destroy(&video->lock);
err_vb2_init:
	mutex_destroy(&video->q_lock);
	return ret;
}

void stf_video_unregister(struct stfcamss_video *video)
{
	vb2_video_unregister_device(&video->vdev);
}
