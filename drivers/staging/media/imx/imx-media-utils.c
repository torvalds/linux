// SPDX-License-Identifier: GPL-2.0+
/*
 * V4L2 Media Controller Driver for Freescale i.MX5/6 SOC
 *
 * Copyright (c) 2016 Mentor Graphics Inc.
 */
#include <linux/module.h>
#include "imx-media.h"

/*
 * List of supported pixel formats for the subdevs.
 *
 * In all of these tables, the non-mbus formats (with no
 * mbus codes) must all fall at the end of the table.
 */

static const struct imx_media_pixfmt yuv_formats[] = {
	{
		.fourcc	= V4L2_PIX_FMT_UYVY,
		.codes  = {
			MEDIA_BUS_FMT_UYVY8_2X8,
			MEDIA_BUS_FMT_UYVY8_1X16
		},
		.cs     = IPUV3_COLORSPACE_YUV,
		.bpp    = 16,
	}, {
		.fourcc	= V4L2_PIX_FMT_YUYV,
		.codes  = {
			MEDIA_BUS_FMT_YUYV8_2X8,
			MEDIA_BUS_FMT_YUYV8_1X16
		},
		.cs     = IPUV3_COLORSPACE_YUV,
		.bpp    = 16,
	},
	/***
	 * non-mbus YUV formats start here. NOTE! when adding non-mbus
	 * formats, NUM_NON_MBUS_YUV_FORMATS must be updated below.
	 ***/
	{
		.fourcc	= V4L2_PIX_FMT_YUV420,
		.cs     = IPUV3_COLORSPACE_YUV,
		.bpp    = 12,
		.planar = true,
	}, {
		.fourcc = V4L2_PIX_FMT_YVU420,
		.cs     = IPUV3_COLORSPACE_YUV,
		.bpp    = 12,
		.planar = true,
	}, {
		.fourcc = V4L2_PIX_FMT_YUV422P,
		.cs     = IPUV3_COLORSPACE_YUV,
		.bpp    = 16,
		.planar = true,
	}, {
		.fourcc = V4L2_PIX_FMT_NV12,
		.cs     = IPUV3_COLORSPACE_YUV,
		.bpp    = 12,
		.planar = true,
	}, {
		.fourcc = V4L2_PIX_FMT_NV16,
		.cs     = IPUV3_COLORSPACE_YUV,
		.bpp    = 16,
		.planar = true,
	},
};

#define NUM_NON_MBUS_YUV_FORMATS 5
#define NUM_YUV_FORMATS ARRAY_SIZE(yuv_formats)
#define NUM_MBUS_YUV_FORMATS (NUM_YUV_FORMATS - NUM_NON_MBUS_YUV_FORMATS)

static const struct imx_media_pixfmt rgb_formats[] = {
	{
		.fourcc	= V4L2_PIX_FMT_RGB565,
		.codes  = {MEDIA_BUS_FMT_RGB565_2X8_LE},
		.cs     = IPUV3_COLORSPACE_RGB,
		.bpp    = 16,
		.cycles = 2,
	}, {
		.fourcc	= V4L2_PIX_FMT_RGB24,
		.codes  = {
			MEDIA_BUS_FMT_RGB888_1X24,
			MEDIA_BUS_FMT_RGB888_2X12_LE
		},
		.cs     = IPUV3_COLORSPACE_RGB,
		.bpp    = 24,
	}, {
		.fourcc	= V4L2_PIX_FMT_XRGB32,
		.codes  = {MEDIA_BUS_FMT_ARGB8888_1X32},
		.cs     = IPUV3_COLORSPACE_RGB,
		.bpp    = 32,
		.ipufmt = true,
	},
	/*** raw bayer and grayscale formats start here ***/
	{
		.fourcc = V4L2_PIX_FMT_SBGGR8,
		.codes  = {MEDIA_BUS_FMT_SBGGR8_1X8},
		.cs     = IPUV3_COLORSPACE_RGB,
		.bpp    = 8,
		.bayer  = true,
	}, {
		.fourcc = V4L2_PIX_FMT_SGBRG8,
		.codes  = {MEDIA_BUS_FMT_SGBRG8_1X8},
		.cs     = IPUV3_COLORSPACE_RGB,
		.bpp    = 8,
		.bayer  = true,
	}, {
		.fourcc = V4L2_PIX_FMT_SGRBG8,
		.codes  = {MEDIA_BUS_FMT_SGRBG8_1X8},
		.cs     = IPUV3_COLORSPACE_RGB,
		.bpp    = 8,
		.bayer  = true,
	}, {
		.fourcc = V4L2_PIX_FMT_SRGGB8,
		.codes  = {MEDIA_BUS_FMT_SRGGB8_1X8},
		.cs     = IPUV3_COLORSPACE_RGB,
		.bpp    = 8,
		.bayer  = true,
	}, {
		.fourcc = V4L2_PIX_FMT_SBGGR16,
		.codes  = {
			MEDIA_BUS_FMT_SBGGR10_1X10,
			MEDIA_BUS_FMT_SBGGR12_1X12,
			MEDIA_BUS_FMT_SBGGR14_1X14,
			MEDIA_BUS_FMT_SBGGR16_1X16
		},
		.cs     = IPUV3_COLORSPACE_RGB,
		.bpp    = 16,
		.bayer  = true,
	}, {
		.fourcc = V4L2_PIX_FMT_SGBRG16,
		.codes  = {
			MEDIA_BUS_FMT_SGBRG10_1X10,
			MEDIA_BUS_FMT_SGBRG12_1X12,
			MEDIA_BUS_FMT_SGBRG14_1X14,
			MEDIA_BUS_FMT_SGBRG16_1X16,
		},
		.cs     = IPUV3_COLORSPACE_RGB,
		.bpp    = 16,
		.bayer  = true,
	}, {
		.fourcc = V4L2_PIX_FMT_SGRBG16,
		.codes  = {
			MEDIA_BUS_FMT_SGRBG10_1X10,
			MEDIA_BUS_FMT_SGRBG12_1X12,
			MEDIA_BUS_FMT_SGRBG14_1X14,
			MEDIA_BUS_FMT_SGRBG16_1X16,
		},
		.cs     = IPUV3_COLORSPACE_RGB,
		.bpp    = 16,
		.bayer  = true,
	}, {
		.fourcc = V4L2_PIX_FMT_SRGGB16,
		.codes  = {
			MEDIA_BUS_FMT_SRGGB10_1X10,
			MEDIA_BUS_FMT_SRGGB12_1X12,
			MEDIA_BUS_FMT_SRGGB14_1X14,
			MEDIA_BUS_FMT_SRGGB16_1X16,
		},
		.cs     = IPUV3_COLORSPACE_RGB,
		.bpp    = 16,
		.bayer  = true,
	}, {
		.fourcc = V4L2_PIX_FMT_GREY,
		.codes = {MEDIA_BUS_FMT_Y8_1X8},
		.cs     = IPUV3_COLORSPACE_RGB,
		.bpp    = 8,
		.bayer  = true,
	}, {
		.fourcc = V4L2_PIX_FMT_Y16,
		.codes = {
			MEDIA_BUS_FMT_Y10_1X10,
			MEDIA_BUS_FMT_Y12_1X12,
		},
		.cs     = IPUV3_COLORSPACE_RGB,
		.bpp    = 16,
		.bayer  = true,
	},
	/***
	 * non-mbus RGB formats start here. NOTE! when adding non-mbus
	 * formats, NUM_NON_MBUS_RGB_FORMATS must be updated below.
	 ***/
	{
		.fourcc	= V4L2_PIX_FMT_BGR24,
		.cs     = IPUV3_COLORSPACE_RGB,
		.bpp    = 24,
	}, {
		.fourcc	= V4L2_PIX_FMT_BGR32,
		.cs     = IPUV3_COLORSPACE_RGB,
		.bpp    = 32,
	},
};

#define NUM_NON_MBUS_RGB_FORMATS 2
#define NUM_RGB_FORMATS ARRAY_SIZE(rgb_formats)
#define NUM_MBUS_RGB_FORMATS (NUM_RGB_FORMATS - NUM_NON_MBUS_RGB_FORMATS)

static const struct imx_media_pixfmt ipu_yuv_formats[] = {
	{
		.fourcc = V4L2_PIX_FMT_YUV32,
		.codes  = {MEDIA_BUS_FMT_AYUV8_1X32},
		.cs     = IPUV3_COLORSPACE_YUV,
		.bpp    = 32,
		.ipufmt = true,
	},
};

#define NUM_IPU_YUV_FORMATS ARRAY_SIZE(ipu_yuv_formats)

static const struct imx_media_pixfmt ipu_rgb_formats[] = {
	{
		.fourcc	= V4L2_PIX_FMT_XRGB32,
		.codes  = {MEDIA_BUS_FMT_ARGB8888_1X32},
		.cs     = IPUV3_COLORSPACE_RGB,
		.bpp    = 32,
		.ipufmt = true,
	},
};

#define NUM_IPU_RGB_FORMATS ARRAY_SIZE(ipu_rgb_formats)

static void init_mbus_colorimetry(struct v4l2_mbus_framefmt *mbus,
				  const struct imx_media_pixfmt *fmt)
{
	mbus->colorspace = (fmt->cs == IPUV3_COLORSPACE_RGB) ?
		V4L2_COLORSPACE_SRGB : V4L2_COLORSPACE_SMPTE170M;
	mbus->xfer_func = V4L2_MAP_XFER_FUNC_DEFAULT(mbus->colorspace);
	mbus->ycbcr_enc = V4L2_MAP_YCBCR_ENC_DEFAULT(mbus->colorspace);
	mbus->quantization =
		V4L2_MAP_QUANTIZATION_DEFAULT(fmt->cs == IPUV3_COLORSPACE_RGB,
					      mbus->colorspace,
					      mbus->ycbcr_enc);
}

static const
struct imx_media_pixfmt *__find_format(u32 fourcc,
				       u32 code,
				       bool allow_non_mbus,
				       bool allow_bayer,
				       const struct imx_media_pixfmt *array,
				       u32 array_size)
{
	const struct imx_media_pixfmt *fmt;
	int i, j;

	for (i = 0; i < array_size; i++) {
		fmt = &array[i];

		if ((!allow_non_mbus && !fmt->codes[0]) ||
		    (!allow_bayer && fmt->bayer))
			continue;

		if (fourcc && fmt->fourcc == fourcc)
			return fmt;

		if (!code)
			continue;

		for (j = 0; fmt->codes[j]; j++) {
			if (code == fmt->codes[j])
				return fmt;
		}
	}
	return NULL;
}

static const struct imx_media_pixfmt *find_format(u32 fourcc,
						  u32 code,
						  enum codespace_sel cs_sel,
						  bool allow_non_mbus,
						  bool allow_bayer)
{
	const struct imx_media_pixfmt *ret;

	switch (cs_sel) {
	case CS_SEL_YUV:
		return __find_format(fourcc, code, allow_non_mbus, allow_bayer,
				     yuv_formats, NUM_YUV_FORMATS);
	case CS_SEL_RGB:
		return __find_format(fourcc, code, allow_non_mbus, allow_bayer,
				     rgb_formats, NUM_RGB_FORMATS);
	case CS_SEL_ANY:
		ret = __find_format(fourcc, code, allow_non_mbus, allow_bayer,
				    yuv_formats, NUM_YUV_FORMATS);
		if (ret)
			return ret;
		return __find_format(fourcc, code, allow_non_mbus, allow_bayer,
				     rgb_formats, NUM_RGB_FORMATS);
	default:
		return NULL;
	}
}

static int enum_format(u32 *fourcc, u32 *code, u32 index,
		       enum codespace_sel cs_sel,
		       bool allow_non_mbus,
		       bool allow_bayer)
{
	const struct imx_media_pixfmt *fmt;
	u32 mbus_yuv_sz = NUM_MBUS_YUV_FORMATS;
	u32 mbus_rgb_sz = NUM_MBUS_RGB_FORMATS;
	u32 yuv_sz = NUM_YUV_FORMATS;
	u32 rgb_sz = NUM_RGB_FORMATS;

	switch (cs_sel) {
	case CS_SEL_YUV:
		if (index >= yuv_sz ||
		    (!allow_non_mbus && index >= mbus_yuv_sz))
			return -EINVAL;
		fmt = &yuv_formats[index];
		break;
	case CS_SEL_RGB:
		if (index >= rgb_sz ||
		    (!allow_non_mbus && index >= mbus_rgb_sz))
			return -EINVAL;
		fmt = &rgb_formats[index];
		if (!allow_bayer && fmt->bayer)
			return -EINVAL;
		break;
	case CS_SEL_ANY:
		if (!allow_non_mbus) {
			if (index >= mbus_yuv_sz) {
				index -= mbus_yuv_sz;
				if (index >= mbus_rgb_sz)
					return -EINVAL;
				fmt = &rgb_formats[index];
				if (!allow_bayer && fmt->bayer)
					return -EINVAL;
			} else {
				fmt = &yuv_formats[index];
			}
		} else {
			if (index >= yuv_sz + rgb_sz)
				return -EINVAL;
			if (index >= yuv_sz) {
				fmt = &rgb_formats[index - yuv_sz];
				if (!allow_bayer && fmt->bayer)
					return -EINVAL;
			} else {
				fmt = &yuv_formats[index];
			}
		}
		break;
	default:
		return -EINVAL;
	}

	if (fourcc)
		*fourcc = fmt->fourcc;
	if (code)
		*code = fmt->codes[0];

	return 0;
}

const struct imx_media_pixfmt *
imx_media_find_format(u32 fourcc, enum codespace_sel cs_sel, bool allow_bayer)
{
	return find_format(fourcc, 0, cs_sel, true, allow_bayer);
}
EXPORT_SYMBOL_GPL(imx_media_find_format);

int imx_media_enum_format(u32 *fourcc, u32 index, enum codespace_sel cs_sel)
{
	return enum_format(fourcc, NULL, index, cs_sel, true, false);
}
EXPORT_SYMBOL_GPL(imx_media_enum_format);

const struct imx_media_pixfmt *
imx_media_find_mbus_format(u32 code, enum codespace_sel cs_sel,
			   bool allow_bayer)
{
	return find_format(0, code, cs_sel, false, allow_bayer);
}
EXPORT_SYMBOL_GPL(imx_media_find_mbus_format);

int imx_media_enum_mbus_format(u32 *code, u32 index, enum codespace_sel cs_sel,
			       bool allow_bayer)
{
	return enum_format(NULL, code, index, cs_sel, false, allow_bayer);
}
EXPORT_SYMBOL_GPL(imx_media_enum_mbus_format);

const struct imx_media_pixfmt *
imx_media_find_ipu_format(u32 code, enum codespace_sel cs_sel)
{
	const struct imx_media_pixfmt *array, *fmt, *ret = NULL;
	u32 array_size;
	int i, j;

	switch (cs_sel) {
	case CS_SEL_YUV:
		array_size = NUM_IPU_YUV_FORMATS;
		array = ipu_yuv_formats;
		break;
	case CS_SEL_RGB:
		array_size = NUM_IPU_RGB_FORMATS;
		array = ipu_rgb_formats;
		break;
	case CS_SEL_ANY:
		array_size = NUM_IPU_YUV_FORMATS + NUM_IPU_RGB_FORMATS;
		array = ipu_yuv_formats;
		break;
	default:
		return NULL;
	}

	for (i = 0; i < array_size; i++) {
		if (cs_sel == CS_SEL_ANY && i >= NUM_IPU_YUV_FORMATS)
			fmt = &ipu_rgb_formats[i - NUM_IPU_YUV_FORMATS];
		else
			fmt = &array[i];

		for (j = 0; code && fmt->codes[j]; j++) {
			if (code == fmt->codes[j]) {
				ret = fmt;
				goto out;
			}
		}
	}

out:
	return ret;
}
EXPORT_SYMBOL_GPL(imx_media_find_ipu_format);

int imx_media_enum_ipu_format(u32 *code, u32 index, enum codespace_sel cs_sel)
{
	switch (cs_sel) {
	case CS_SEL_YUV:
		if (index >= NUM_IPU_YUV_FORMATS)
			return -EINVAL;
		*code = ipu_yuv_formats[index].codes[0];
		break;
	case CS_SEL_RGB:
		if (index >= NUM_IPU_RGB_FORMATS)
			return -EINVAL;
		*code = ipu_rgb_formats[index].codes[0];
		break;
	case CS_SEL_ANY:
		if (index >= NUM_IPU_YUV_FORMATS + NUM_IPU_RGB_FORMATS)
			return -EINVAL;
		if (index >= NUM_IPU_YUV_FORMATS) {
			index -= NUM_IPU_YUV_FORMATS;
			*code = ipu_rgb_formats[index].codes[0];
		} else {
			*code = ipu_yuv_formats[index].codes[0];
		}
		break;
	default:
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(imx_media_enum_ipu_format);

int imx_media_init_mbus_fmt(struct v4l2_mbus_framefmt *mbus,
			    u32 width, u32 height, u32 code, u32 field,
			    const struct imx_media_pixfmt **cc)
{
	const struct imx_media_pixfmt *lcc;

	mbus->width = width;
	mbus->height = height;
	mbus->field = field;
	if (code == 0)
		imx_media_enum_mbus_format(&code, 0, CS_SEL_YUV, false);
	lcc = imx_media_find_mbus_format(code, CS_SEL_ANY, false);
	if (!lcc) {
		lcc = imx_media_find_ipu_format(code, CS_SEL_ANY);
		if (!lcc)
			return -EINVAL;
	}

	mbus->code = code;
	init_mbus_colorimetry(mbus, lcc);
	if (cc)
		*cc = lcc;

	return 0;
}
EXPORT_SYMBOL_GPL(imx_media_init_mbus_fmt);

/*
 * Initializes the TRY format to the ACTIVE format on all pads
 * of a subdev. Can be used as the .init_cfg pad operation.
 */
int imx_media_init_cfg(struct v4l2_subdev *sd,
		       struct v4l2_subdev_pad_config *cfg)
{
	struct v4l2_mbus_framefmt *mf_try;
	struct v4l2_subdev_format format;
	unsigned int pad;
	int ret;

	for (pad = 0; pad < sd->entity.num_pads; pad++) {
		memset(&format, 0, sizeof(format));

		format.pad = pad;
		format.which = V4L2_SUBDEV_FORMAT_ACTIVE;
		ret = v4l2_subdev_call(sd, pad, get_fmt, NULL, &format);
		if (ret)
			continue;

		mf_try = v4l2_subdev_get_try_format(sd, cfg, pad);
		*mf_try = format.format;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(imx_media_init_cfg);

/*
 * Default the colorspace in tryfmt to SRGB if set to an unsupported
 * colorspace or not initialized. Then set the remaining colorimetry
 * parameters based on the colorspace if they are uninitialized.
 *
 * tryfmt->code must be set on entry.
 *
 * If this format is destined to be routed through the Image Converter,
 * Y`CbCr encoding must be fixed. The IC supports only BT.601 Y`CbCr
 * or Rec.709 Y`CbCr encoding.
 */
void imx_media_try_colorimetry(struct v4l2_mbus_framefmt *tryfmt,
			       bool ic_route)
{
	const struct imx_media_pixfmt *cc;
	bool is_rgb = false;

	cc = imx_media_find_mbus_format(tryfmt->code, CS_SEL_ANY, true);
	if (!cc)
		cc = imx_media_find_ipu_format(tryfmt->code, CS_SEL_ANY);
	if (cc && cc->cs == IPUV3_COLORSPACE_RGB)
		is_rgb = true;

	switch (tryfmt->colorspace) {
	case V4L2_COLORSPACE_SMPTE170M:
	case V4L2_COLORSPACE_REC709:
	case V4L2_COLORSPACE_JPEG:
	case V4L2_COLORSPACE_SRGB:
	case V4L2_COLORSPACE_BT2020:
	case V4L2_COLORSPACE_OPRGB:
	case V4L2_COLORSPACE_DCI_P3:
	case V4L2_COLORSPACE_RAW:
		break;
	default:
		tryfmt->colorspace = V4L2_COLORSPACE_SRGB;
		break;
	}

	if (tryfmt->xfer_func == V4L2_XFER_FUNC_DEFAULT)
		tryfmt->xfer_func =
			V4L2_MAP_XFER_FUNC_DEFAULT(tryfmt->colorspace);

	if (ic_route) {
		if (tryfmt->ycbcr_enc != V4L2_YCBCR_ENC_601 &&
		    tryfmt->ycbcr_enc != V4L2_YCBCR_ENC_709)
			tryfmt->ycbcr_enc = V4L2_YCBCR_ENC_601;
	} else {
		if (tryfmt->ycbcr_enc == V4L2_YCBCR_ENC_DEFAULT) {
			tryfmt->ycbcr_enc =
				V4L2_MAP_YCBCR_ENC_DEFAULT(tryfmt->colorspace);
		}
	}

	if (tryfmt->quantization == V4L2_QUANTIZATION_DEFAULT)
		tryfmt->quantization =
			V4L2_MAP_QUANTIZATION_DEFAULT(is_rgb,
						      tryfmt->colorspace,
						      tryfmt->ycbcr_enc);
}
EXPORT_SYMBOL_GPL(imx_media_try_colorimetry);

int imx_media_mbus_fmt_to_pix_fmt(struct v4l2_pix_format *pix,
				  struct v4l2_rect *compose,
				  const struct v4l2_mbus_framefmt *mbus,
				  const struct imx_media_pixfmt *cc)
{
	u32 width;
	u32 stride;

	if (!cc) {
		cc = imx_media_find_ipu_format(mbus->code, CS_SEL_ANY);
		if (!cc)
			cc = imx_media_find_mbus_format(mbus->code, CS_SEL_ANY,
							true);
		if (!cc)
			return -EINVAL;
	}

	/*
	 * TODO: the IPU currently does not support the AYUV32 format,
	 * so until it does convert to a supported YUV format.
	 */
	if (cc->ipufmt && cc->cs == IPUV3_COLORSPACE_YUV) {
		u32 code;

		imx_media_enum_mbus_format(&code, 0, CS_SEL_YUV, false);
		cc = imx_media_find_mbus_format(code, CS_SEL_YUV, false);
	}

	/* Round up width for minimum burst size */
	width = round_up(mbus->width, 8);

	/* Round up stride for IDMAC line start address alignment */
	if (cc->planar)
		stride = round_up(width, 16);
	else
		stride = round_up((width * cc->bpp) >> 3, 8);

	pix->width = width;
	pix->height = mbus->height;
	pix->pixelformat = cc->fourcc;
	pix->colorspace = mbus->colorspace;
	pix->xfer_func = mbus->xfer_func;
	pix->ycbcr_enc = mbus->ycbcr_enc;
	pix->quantization = mbus->quantization;
	pix->field = mbus->field;
	pix->bytesperline = stride;
	pix->sizeimage = cc->planar ? ((stride * pix->height * cc->bpp) >> 3) :
			 stride * pix->height;

	/*
	 * set capture compose rectangle, which is fixed to the
	 * source subdevice mbus format.
	 */
	if (compose) {
		compose->left = 0;
		compose->top = 0;
		compose->width = mbus->width;
		compose->height = mbus->height;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(imx_media_mbus_fmt_to_pix_fmt);

int imx_media_mbus_fmt_to_ipu_image(struct ipu_image *image,
				    struct v4l2_mbus_framefmt *mbus)
{
	int ret;

	memset(image, 0, sizeof(*image));

	ret = imx_media_mbus_fmt_to_pix_fmt(&image->pix, &image->rect,
					    mbus, NULL);
	if (ret)
		return ret;

	return 0;
}
EXPORT_SYMBOL_GPL(imx_media_mbus_fmt_to_ipu_image);

int imx_media_ipu_image_to_mbus_fmt(struct v4l2_mbus_framefmt *mbus,
				    struct ipu_image *image)
{
	const struct imx_media_pixfmt *fmt;

	fmt = imx_media_find_format(image->pix.pixelformat, CS_SEL_ANY, true);
	if (!fmt)
		return -EINVAL;

	memset(mbus, 0, sizeof(*mbus));
	mbus->width = image->pix.width;
	mbus->height = image->pix.height;
	mbus->code = fmt->codes[0];
	mbus->field = image->pix.field;
	mbus->colorspace = image->pix.colorspace;
	mbus->xfer_func = image->pix.xfer_func;
	mbus->ycbcr_enc = image->pix.ycbcr_enc;
	mbus->quantization = image->pix.quantization;

	return 0;
}
EXPORT_SYMBOL_GPL(imx_media_ipu_image_to_mbus_fmt);

void imx_media_free_dma_buf(struct imx_media_dev *imxmd,
			    struct imx_media_dma_buf *buf)
{
	if (buf->virt)
		dma_free_coherent(imxmd->md.dev, buf->len,
				  buf->virt, buf->phys);

	buf->virt = NULL;
	buf->phys = 0;
}
EXPORT_SYMBOL_GPL(imx_media_free_dma_buf);

int imx_media_alloc_dma_buf(struct imx_media_dev *imxmd,
			    struct imx_media_dma_buf *buf,
			    int size)
{
	imx_media_free_dma_buf(imxmd, buf);

	buf->len = PAGE_ALIGN(size);
	buf->virt = dma_alloc_coherent(imxmd->md.dev, buf->len, &buf->phys,
				       GFP_DMA | GFP_KERNEL);
	if (!buf->virt) {
		dev_err(imxmd->md.dev, "failed to alloc dma buffer\n");
		return -ENOMEM;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(imx_media_alloc_dma_buf);

/* form a subdev name given a group id and ipu id */
void imx_media_grp_id_to_sd_name(char *sd_name, int sz, u32 grp_id, int ipu_id)
{
	int id;

	switch (grp_id) {
	case IMX_MEDIA_GRP_ID_IPU_CSI0...IMX_MEDIA_GRP_ID_IPU_CSI1:
		id = (grp_id >> IMX_MEDIA_GRP_ID_IPU_CSI_BIT) - 1;
		snprintf(sd_name, sz, "ipu%d_csi%d", ipu_id + 1, id);
		break;
	case IMX_MEDIA_GRP_ID_IPU_VDIC:
		snprintf(sd_name, sz, "ipu%d_vdic", ipu_id + 1);
		break;
	case IMX_MEDIA_GRP_ID_IPU_IC_PRP:
		snprintf(sd_name, sz, "ipu%d_ic_prp", ipu_id + 1);
		break;
	case IMX_MEDIA_GRP_ID_IPU_IC_PRPENC:
		snprintf(sd_name, sz, "ipu%d_ic_prpenc", ipu_id + 1);
		break;
	case IMX_MEDIA_GRP_ID_IPU_IC_PRPVF:
		snprintf(sd_name, sz, "ipu%d_ic_prpvf", ipu_id + 1);
		break;
	default:
		break;
	}
}
EXPORT_SYMBOL_GPL(imx_media_grp_id_to_sd_name);

struct v4l2_subdev *
imx_media_find_subdev_by_fwnode(struct imx_media_dev *imxmd,
				struct fwnode_handle *fwnode)
{
	struct v4l2_subdev *sd;

	list_for_each_entry(sd, &imxmd->v4l2_dev.subdevs, list) {
		if (sd->fwnode == fwnode)
			return sd;
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(imx_media_find_subdev_by_fwnode);

struct v4l2_subdev *
imx_media_find_subdev_by_devname(struct imx_media_dev *imxmd,
				 const char *devname)
{
	struct v4l2_subdev *sd;

	list_for_each_entry(sd, &imxmd->v4l2_dev.subdevs, list) {
		if (!strcmp(devname, dev_name(sd->dev)))
			return sd;
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(imx_media_find_subdev_by_devname);

/*
 * Adds a video device to the master video device list. This is called by
 * an async subdev that owns a video device when it is registered.
 */
int imx_media_add_video_device(struct imx_media_dev *imxmd,
			       struct imx_media_video_dev *vdev)
{
	mutex_lock(&imxmd->mutex);

	list_add_tail(&vdev->list, &imxmd->vdev_list);

	mutex_unlock(&imxmd->mutex);
	return 0;
}
EXPORT_SYMBOL_GPL(imx_media_add_video_device);

/*
 * Search upstream/downstream for a subdevice in the current pipeline
 * with given grp_id, starting from start_entity. Returns the subdev's
 * source/sink pad that it was reached from. If grp_id is zero, just
 * returns the nearest source/sink pad to start_entity. Must be called
 * with mdev->graph_mutex held.
 */
static struct media_pad *
find_pipeline_pad(struct imx_media_dev *imxmd,
		  struct media_entity *start_entity,
		  u32 grp_id, bool upstream)
{
	struct media_entity *me = start_entity;
	struct media_pad *pad = NULL;
	struct v4l2_subdev *sd;
	int i;

	for (i = 0; i < me->num_pads; i++) {
		struct media_pad *spad = &me->pads[i];

		if ((upstream && !(spad->flags & MEDIA_PAD_FL_SINK)) ||
		    (!upstream && !(spad->flags & MEDIA_PAD_FL_SOURCE)))
			continue;

		pad = media_entity_remote_pad(spad);
		if (!pad || !is_media_entity_v4l2_subdev(pad->entity))
			continue;

		if (grp_id != 0) {
			sd = media_entity_to_v4l2_subdev(pad->entity);
			if (sd->grp_id & grp_id)
				return pad;

			return find_pipeline_pad(imxmd, pad->entity,
						 grp_id, upstream);
		} else {
			return pad;
		}
	}

	return NULL;
}

/*
 * Search upstream for a subdev in the current pipeline with
 * given grp_id. Must be called with mdev->graph_mutex held.
 */
static struct v4l2_subdev *
find_upstream_subdev(struct imx_media_dev *imxmd,
		     struct media_entity *start_entity,
		     u32 grp_id)
{
	struct v4l2_subdev *sd;
	struct media_pad *pad;

	if (is_media_entity_v4l2_subdev(start_entity)) {
		sd = media_entity_to_v4l2_subdev(start_entity);
		if (sd->grp_id & grp_id)
			return sd;
	}

	pad = find_pipeline_pad(imxmd, start_entity, grp_id, true);

	return pad ? media_entity_to_v4l2_subdev(pad->entity) : NULL;
}

/*
 * Find the upstream mipi-csi2 virtual channel reached from the given
 * start entity in the current pipeline.
 * Must be called with mdev->graph_mutex held.
 */
int imx_media_find_mipi_csi2_channel(struct imx_media_dev *imxmd,
				     struct media_entity *start_entity)
{
	struct media_pad *pad;
	int ret = -EPIPE;

	pad = find_pipeline_pad(imxmd, start_entity, IMX_MEDIA_GRP_ID_CSI2,
				true);
	if (pad) {
		ret = pad->index - 1;
		dev_dbg(imxmd->md.dev, "found vc%d from %s\n",
			ret, start_entity->name);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(imx_media_find_mipi_csi2_channel);

/*
 * Find a source pad reached upstream from the given start entity in
 * the current pipeline. Must be called with mdev->graph_mutex held.
 */
struct media_pad *
imx_media_find_upstream_pad(struct imx_media_dev *imxmd,
			    struct media_entity *start_entity,
			    u32 grp_id)
{
	struct media_pad *pad;

	pad = find_pipeline_pad(imxmd, start_entity, grp_id, true);
	if (!pad)
		return ERR_PTR(-ENODEV);

	return pad;
}
EXPORT_SYMBOL_GPL(imx_media_find_upstream_pad);

/*
 * Find a subdev reached upstream from the given start entity in
 * the current pipeline.
 * Must be called with mdev->graph_mutex held.
 */
struct v4l2_subdev *
imx_media_find_upstream_subdev(struct imx_media_dev *imxmd,
			       struct media_entity *start_entity,
			       u32 grp_id)
{
	struct v4l2_subdev *sd;

	sd = find_upstream_subdev(imxmd, start_entity, grp_id);
	if (!sd)
		return ERR_PTR(-ENODEV);

	return sd;
}
EXPORT_SYMBOL_GPL(imx_media_find_upstream_subdev);

/*
 * Turn current pipeline streaming on/off starting from entity.
 */
int imx_media_pipeline_set_stream(struct imx_media_dev *imxmd,
				  struct media_entity *entity,
				  bool on)
{
	struct v4l2_subdev *sd;
	int ret = 0;

	if (!is_media_entity_v4l2_subdev(entity))
		return -EINVAL;
	sd = media_entity_to_v4l2_subdev(entity);

	mutex_lock(&imxmd->md.graph_mutex);

	if (on) {
		ret = __media_pipeline_start(entity, &imxmd->pipe);
		if (ret)
			goto out;
		ret = v4l2_subdev_call(sd, video, s_stream, 1);
		if (ret)
			__media_pipeline_stop(entity);
	} else {
		v4l2_subdev_call(sd, video, s_stream, 0);
		if (entity->pipe)
			__media_pipeline_stop(entity);
	}

out:
	mutex_unlock(&imxmd->md.graph_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(imx_media_pipeline_set_stream);

MODULE_DESCRIPTION("i.MX5/6 v4l2 media controller driver");
MODULE_AUTHOR("Steve Longerbeam <steve_longerbeam@mentor.com>");
MODULE_LICENSE("GPL");
