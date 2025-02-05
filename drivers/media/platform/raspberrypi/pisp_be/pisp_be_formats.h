/* SPDX-License-Identifier: GPL-2.0 */
/*
 * PiSP Back End driver image format definitions.
 *
 * Copyright (c) 2021-2024 Raspberry Pi Ltd
 */

#ifndef _PISP_BE_FORMATS_
#define _PISP_BE_FORMATS_

#include <linux/bits.h>
#include <linux/videodev2.h>

#define PISPBE_MAX_PLANES	3
#define P3(x)			((x) * 8)

struct pisp_be_format {
	unsigned int fourcc;
	unsigned int align;
	unsigned int bit_depth;
	/* 0P3 factor for plane sizing */
	unsigned int plane_factor[PISPBE_MAX_PLANES];
	unsigned int num_planes;
	unsigned int colorspace_mask;
	enum v4l2_colorspace colorspace_default;
};

#define V4L2_COLORSPACE_MASK(colorspace) BIT(colorspace)

#define V4L2_COLORSPACE_MASK_JPEG	\
	V4L2_COLORSPACE_MASK(V4L2_COLORSPACE_JPEG)
#define V4L2_COLORSPACE_MASK_SMPTE170M	\
	V4L2_COLORSPACE_MASK(V4L2_COLORSPACE_SMPTE170M)
#define V4L2_COLORSPACE_MASK_REC709	\
	V4L2_COLORSPACE_MASK(V4L2_COLORSPACE_REC709)
#define V4L2_COLORSPACE_MASK_SRGB	\
	V4L2_COLORSPACE_MASK(V4L2_COLORSPACE_SRGB)
#define V4L2_COLORSPACE_MASK_RAW	\
	V4L2_COLORSPACE_MASK(V4L2_COLORSPACE_RAW)

/*
 * All three colour spaces SRGB, SMPTE170M and REC709 are fundamentally sRGB
 * underneath (as near as makes no difference to us), just with different YCbCr
 * encodings. Therefore the ISP can generate sRGB on its main output and any of
 * the others on its low resolution output. Applications should, when using both
 * outputs, program the colour spaces on them to be the same, matching whatever
 * is requested for the low resolution output, even if the main output is
 * producing an RGB format. In turn this requires us to allow all these colour
 * spaces for every YUV/RGB output format.
 */
#define V4L2_COLORSPACE_MASK_ALL_SRGB (V4L2_COLORSPACE_MASK_JPEG	| \
				       V4L2_COLORSPACE_MASK_SRGB	| \
				       V4L2_COLORSPACE_MASK_SMPTE170M	| \
				       V4L2_COLORSPACE_MASK_REC709)

static const struct pisp_be_format supported_formats[] = {
	/* Single plane YUV formats */
	{
		.fourcc		    = V4L2_PIX_FMT_YUV420,
		/* 128 alignment to ensure U/V planes are 64 byte aligned. */
		.align		    = 128,
		.bit_depth	    = 8,
		.plane_factor	    = { P3(1), P3(0.25), P3(0.25) },
		.num_planes	    = 1,
		.colorspace_mask    = V4L2_COLORSPACE_MASK_ALL_SRGB,
		.colorspace_default = V4L2_COLORSPACE_SMPTE170M,
	},
	{
		.fourcc		    = V4L2_PIX_FMT_YVU420,
		/* 128 alignment to ensure U/V planes are 64 byte aligned. */
		.align		    = 128,
		.bit_depth	    = 8,
		.plane_factor	    = { P3(1), P3(0.25), P3(0.25) },
		.num_planes	    = 1,
		.colorspace_mask    = V4L2_COLORSPACE_MASK_ALL_SRGB,
		.colorspace_default = V4L2_COLORSPACE_SMPTE170M,
	},
	{
		.fourcc		    = V4L2_PIX_FMT_NV12,
		.align		    = 32,
		.bit_depth	    = 8,
		.plane_factor	    = { P3(1), P3(0.5) },
		.num_planes	    = 1,
		.colorspace_mask    = V4L2_COLORSPACE_MASK_ALL_SRGB,
		.colorspace_default = V4L2_COLORSPACE_SMPTE170M,
	},
	{
		.fourcc		    = V4L2_PIX_FMT_NV21,
		.align		    = 32,
		.bit_depth	    = 8,
		.plane_factor	    = { P3(1), P3(0.5) },
		.num_planes	    = 1,
		.colorspace_mask    = V4L2_COLORSPACE_MASK_ALL_SRGB,
		.colorspace_default = V4L2_COLORSPACE_SMPTE170M,
	},
	{
		.fourcc		    = V4L2_PIX_FMT_YUYV,
		.align		    = 64,
		.bit_depth	    = 16,
		.plane_factor	    = { P3(1) },
		.num_planes	    = 1,
		.colorspace_mask    = V4L2_COLORSPACE_MASK_ALL_SRGB,
		.colorspace_default = V4L2_COLORSPACE_SMPTE170M,
	},
	{
		.fourcc		    = V4L2_PIX_FMT_UYVY,
		.align		    = 64,
		.bit_depth	    = 16,
		.plane_factor	    = { P3(1) },
		.num_planes	    = 1,
		.colorspace_mask    = V4L2_COLORSPACE_MASK_ALL_SRGB,
		.colorspace_default = V4L2_COLORSPACE_SMPTE170M,
	},
	{
		.fourcc		    = V4L2_PIX_FMT_YVYU,
		.align		    = 64,
		.bit_depth	    = 16,
		.plane_factor	    = { P3(1) },
		.num_planes	    = 1,
		.colorspace_mask    = V4L2_COLORSPACE_MASK_ALL_SRGB,
		.colorspace_default = V4L2_COLORSPACE_SMPTE170M,
	},
	{
		.fourcc		    = V4L2_PIX_FMT_VYUY,
		.align		    = 64,
		.bit_depth	    = 16,
		.plane_factor	    = { P3(1) },
		.num_planes	    = 1,
		.colorspace_mask    = V4L2_COLORSPACE_MASK_ALL_SRGB,
		.colorspace_default = V4L2_COLORSPACE_SMPTE170M,
	},
	/* Multiplane YUV formats */
	{
		.fourcc		    = V4L2_PIX_FMT_YUV420M,
		.align		    = 64,
		.bit_depth	    = 8,
		.plane_factor	    = { P3(1), P3(0.25), P3(0.25) },
		.num_planes	    = 3,
		.colorspace_mask    = V4L2_COLORSPACE_MASK_ALL_SRGB,
		.colorspace_default = V4L2_COLORSPACE_SMPTE170M,
	},
	{
		.fourcc		    = V4L2_PIX_FMT_NV12M,
		.align		    = 32,
		.bit_depth	    = 8,
		.plane_factor	    = { P3(1), P3(0.5) },
		.num_planes	    = 2,
		.colorspace_mask    = V4L2_COLORSPACE_MASK_ALL_SRGB,
		.colorspace_default = V4L2_COLORSPACE_SMPTE170M,
	},
	{
		.fourcc		    = V4L2_PIX_FMT_NV21M,
		.align		    = 32,
		.bit_depth	    = 8,
		.plane_factor	    = { P3(1), P3(0.5) },
		.num_planes	    = 2,
		.colorspace_mask    = V4L2_COLORSPACE_MASK_ALL_SRGB,
		.colorspace_default = V4L2_COLORSPACE_SMPTE170M,
	},
	{
		.fourcc		    = V4L2_PIX_FMT_YVU420M,
		.align		    = 64,
		.bit_depth	    = 8,
		.plane_factor	    = { P3(1), P3(0.25), P3(0.25) },
		.num_planes	    = 3,
		.colorspace_mask    = V4L2_COLORSPACE_MASK_ALL_SRGB,
		.colorspace_default = V4L2_COLORSPACE_SMPTE170M,
	},
	{
		.fourcc		    = V4L2_PIX_FMT_YUV422M,
		.align		    = 64,
		.bit_depth	    = 8,
		.plane_factor	    = { P3(1), P3(0.5), P3(0.5) },
		.num_planes	    = 3,
		.colorspace_mask    = V4L2_COLORSPACE_MASK_ALL_SRGB,
		.colorspace_default = V4L2_COLORSPACE_SMPTE170M,
	},
	{
		.fourcc		    = V4L2_PIX_FMT_YVU422M,
		.align		    = 64,
		.bit_depth	    = 8,
		.plane_factor	    = { P3(1), P3(0.5), P3(0.5) },
		.num_planes	    = 3,
		.colorspace_mask    = V4L2_COLORSPACE_MASK_ALL_SRGB,
		.colorspace_default = V4L2_COLORSPACE_SMPTE170M,
	},
	{
		.fourcc		    = V4L2_PIX_FMT_YUV444M,
		.align		    = 64,
		.bit_depth	    = 8,
		.plane_factor	    = { P3(1), P3(1), P3(1) },
		.num_planes	    = 3,
		.colorspace_mask    = V4L2_COLORSPACE_MASK_ALL_SRGB,
		.colorspace_default = V4L2_COLORSPACE_SMPTE170M,
	},
	{
		.fourcc		    = V4L2_PIX_FMT_YVU444M,
		.align		    = 64,
		.bit_depth	    = 8,
		.plane_factor	    = { P3(1), P3(1), P3(1) },
		.num_planes	    = 3,
		.colorspace_mask    = V4L2_COLORSPACE_MASK_ALL_SRGB,
		.colorspace_default = V4L2_COLORSPACE_SMPTE170M,
	},
	/* RGB formats */
	{
		.fourcc		    = V4L2_PIX_FMT_RGB24,
		.align		    = 32,
		.bit_depth	    = 24,
		.plane_factor	    = { P3(1.0) },
		.num_planes	    = 1,
		.colorspace_mask    = V4L2_COLORSPACE_MASK_ALL_SRGB,
		.colorspace_default = V4L2_COLORSPACE_SRGB,
	},
	{
		.fourcc		    = V4L2_PIX_FMT_BGR24,
		.align		    = 32,
		.bit_depth	    = 24,
		.plane_factor	    = { P3(1.0) },
		.num_planes	    = 1,
		.colorspace_mask    = V4L2_COLORSPACE_MASK_ALL_SRGB,
		.colorspace_default = V4L2_COLORSPACE_SRGB,
	},
	{
		.fourcc		    = V4L2_PIX_FMT_XBGR32,
		.align		    = 64,
		.bit_depth	    = 32,
		.plane_factor	    = { P3(1.0) },
		.num_planes	    = 1,
		.colorspace_mask    = V4L2_COLORSPACE_MASK_ALL_SRGB,
		.colorspace_default = V4L2_COLORSPACE_SRGB,
	},
	{
		.fourcc		    = V4L2_PIX_FMT_RGBX32,
		.align		    = 64,
		.bit_depth	    = 32,
		.plane_factor	    = { P3(1.0) },
		.num_planes	    = 1,
		.colorspace_mask    = V4L2_COLORSPACE_MASK_ALL_SRGB,
		.colorspace_default = V4L2_COLORSPACE_SRGB,
	},
	{
		.fourcc		    = V4L2_PIX_FMT_RGB48,
		.align		    = 64,
		.bit_depth	    = 48,
		.plane_factor	    = { P3(1.0) },
		.num_planes	    = 1,
		.colorspace_mask    = V4L2_COLORSPACE_MASK_ALL_SRGB,
		.colorspace_default = V4L2_COLORSPACE_SRGB,
	},
	{
		.fourcc		    = V4L2_PIX_FMT_BGR48,
		.align		    = 64,
		.bit_depth	    = 48,
		.plane_factor	    = { P3(1.0) },
		.num_planes	    = 1,
		.colorspace_mask    = V4L2_COLORSPACE_MASK_ALL_SRGB,
		.colorspace_default = V4L2_COLORSPACE_SRGB,
	},
	/* Bayer formats - 8-bit */
	{
		.fourcc		    = V4L2_PIX_FMT_SRGGB8,
		.bit_depth	    = 8,
		.align		    = 32,
		.plane_factor	    = { P3(1.0) },
		.num_planes	    = 1,
		.colorspace_mask    = V4L2_COLORSPACE_MASK_RAW,
		.colorspace_default = V4L2_COLORSPACE_RAW,
	},
	{
		.fourcc		    = V4L2_PIX_FMT_SBGGR8,
		.bit_depth	    = 8,
		.align		    = 32,
		.plane_factor	    = { P3(1.0) },
		.num_planes	    = 1,
		.colorspace_mask    = V4L2_COLORSPACE_MASK_RAW,
		.colorspace_default = V4L2_COLORSPACE_RAW,
	},
	{
		.fourcc		    = V4L2_PIX_FMT_SGRBG8,
		.bit_depth	    = 8,
		.align		    = 32,
		.plane_factor	    = { P3(1.0) },
		.num_planes	    = 1,
		.colorspace_mask    = V4L2_COLORSPACE_MASK_RAW,
		.colorspace_default = V4L2_COLORSPACE_RAW,
	},
	{
		.fourcc		    = V4L2_PIX_FMT_SGBRG8,
		.bit_depth	    = 8,
		.align		    = 32,
		.plane_factor	    = { P3(1.0) },
		.num_planes	    = 1,
		.colorspace_mask    = V4L2_COLORSPACE_MASK_RAW,
		.colorspace_default = V4L2_COLORSPACE_RAW,
	},
	/* Bayer formats - 16-bit */
	{
		.fourcc		    = V4L2_PIX_FMT_SRGGB16,
		.bit_depth	    = 16,
		.align		    = 32,
		.plane_factor	    = { P3(1.0) },
		.num_planes	    = 1,
		.colorspace_mask    = V4L2_COLORSPACE_MASK_RAW,
		.colorspace_default = V4L2_COLORSPACE_RAW,
	},
	{
		.fourcc		    = V4L2_PIX_FMT_SBGGR16,
		.bit_depth	    = 16,
		.align		    = 32,
		.plane_factor	    = { P3(1.0) },
		.num_planes	    = 1,
		.colorspace_mask    = V4L2_COLORSPACE_MASK_RAW,
		.colorspace_default = V4L2_COLORSPACE_RAW,
	},
	{
		.fourcc		    = V4L2_PIX_FMT_SGRBG16,
		.bit_depth	    = 16,
		.align		    = 32,
		.plane_factor	    = { P3(1.0) },
		.num_planes	    = 1,
		.colorspace_mask    = V4L2_COLORSPACE_MASK_RAW,
		.colorspace_default = V4L2_COLORSPACE_RAW,
	},
	{
		.fourcc		    = V4L2_PIX_FMT_SGBRG16,
		.bit_depth	    = 16,
		.align		    = 32,
		.plane_factor	    = { P3(1.0) },
		.num_planes	    = 1,
		.colorspace_mask    = V4L2_COLORSPACE_MASK_RAW,
		.colorspace_default = V4L2_COLORSPACE_RAW,
	},
	{
		/* Bayer formats unpacked to 16bpp */
		/* 10 bit */
		.fourcc		    = V4L2_PIX_FMT_SRGGB10,
		.bit_depth	    = 16,
		.align		    = 32,
		.plane_factor	    = { P3(1.0) },
		.num_planes	    = 1,
		.colorspace_mask    = V4L2_COLORSPACE_MASK_RAW,
		.colorspace_default = V4L2_COLORSPACE_RAW,
	},
	{
		.fourcc		    = V4L2_PIX_FMT_SBGGR10,
		.bit_depth	    = 16,
		.align		    = 32,
		.plane_factor	    = { P3(1.0) },
		.num_planes	    = 1,
		.colorspace_mask    = V4L2_COLORSPACE_MASK_RAW,
		.colorspace_default = V4L2_COLORSPACE_RAW,
	},
	{
		.fourcc		    = V4L2_PIX_FMT_SGRBG10,
		.bit_depth	    = 16,
		.align		    = 32,
		.plane_factor	    = { P3(1.0) },
		.num_planes	    = 1,
		.colorspace_mask    = V4L2_COLORSPACE_MASK_RAW,
		.colorspace_default = V4L2_COLORSPACE_RAW,
	},
	{
		.fourcc		    = V4L2_PIX_FMT_SGBRG10,
		.bit_depth	    = 16,
		.align		    = 32,
		.plane_factor	    = { P3(1.0) },
		.num_planes	    = 1,
		.colorspace_mask    = V4L2_COLORSPACE_MASK_RAW,
		.colorspace_default = V4L2_COLORSPACE_RAW,
	},
	{
		/* 12 bit */
		.fourcc		    = V4L2_PIX_FMT_SRGGB12,
		.bit_depth	    = 16,
		.align		    = 32,
		.plane_factor	    = { P3(1.0) },
		.num_planes	    = 1,
		.colorspace_mask    = V4L2_COLORSPACE_MASK_RAW,
		.colorspace_default = V4L2_COLORSPACE_RAW,
	},
	{
		.fourcc		    = V4L2_PIX_FMT_SBGGR12,
		.bit_depth	    = 16,
		.align		    = 32,
		.plane_factor	    = { P3(1.0) },
		.num_planes	    = 1,
		.colorspace_mask    = V4L2_COLORSPACE_MASK_RAW,
		.colorspace_default = V4L2_COLORSPACE_RAW,
	},
	{
		.fourcc		    = V4L2_PIX_FMT_SGRBG12,
		.bit_depth	    = 16,
		.align		    = 32,
		.plane_factor	    = { P3(1.0) },
		.num_planes	    = 1,
		.colorspace_mask    = V4L2_COLORSPACE_MASK_RAW,
		.colorspace_default = V4L2_COLORSPACE_RAW,
	},
	{
		.fourcc		    = V4L2_PIX_FMT_SGBRG12,
		.bit_depth	    = 16,
		.align		    = 32,
		.plane_factor	    = { P3(1.0) },
		.num_planes	    = 1,
		.colorspace_mask    = V4L2_COLORSPACE_MASK_RAW,
		.colorspace_default = V4L2_COLORSPACE_RAW,
	},
	{
		/* 14 bit */
		.fourcc		    = V4L2_PIX_FMT_SRGGB14,
		.bit_depth	    = 16,
		.align		    = 32,
		.plane_factor	    = { P3(1.0) },
		.num_planes	    = 1,
		.colorspace_mask    = V4L2_COLORSPACE_MASK_RAW,
		.colorspace_default = V4L2_COLORSPACE_RAW,
	},
	{
		.fourcc		    = V4L2_PIX_FMT_SBGGR14,
		.bit_depth	    = 16,
		.align		    = 32,
		.plane_factor	    = { P3(1.0) },
		.num_planes	    = 1,
		.colorspace_mask    = V4L2_COLORSPACE_MASK_RAW,
		.colorspace_default = V4L2_COLORSPACE_RAW,
	},
	{
		.fourcc		    = V4L2_PIX_FMT_SGRBG14,
		.bit_depth	    = 16,
		.align		    = 32,
		.plane_factor	    = { P3(1.0) },
		.num_planes	    = 1,
		.colorspace_mask    = V4L2_COLORSPACE_MASK_RAW,
		.colorspace_default = V4L2_COLORSPACE_RAW,
	},
	{
		.fourcc		    = V4L2_PIX_FMT_SGBRG14,
		.bit_depth	    = 16,
		.align		    = 32,
		.plane_factor	    = { P3(1.0) },
		.num_planes	    = 1,
		.colorspace_mask    = V4L2_COLORSPACE_MASK_RAW,
		.colorspace_default = V4L2_COLORSPACE_RAW,
	},
	/* Bayer formats - 16-bit PiSP Compressed */
	{
		.fourcc		    = V4L2_PIX_FMT_PISP_COMP1_BGGR,
		.bit_depth	    = 8,
		.align		    = 32,
		.plane_factor	    = { P3(1.0) },
		.num_planes	    = 1,
		.colorspace_mask    = V4L2_COLORSPACE_MASK_RAW,
		.colorspace_default = V4L2_COLORSPACE_RAW,
	},
	{
		.fourcc		    = V4L2_PIX_FMT_PISP_COMP1_RGGB,
		.bit_depth	    = 8,
		.align		    = 32,
		.plane_factor	    = { P3(1.0) },
		.num_planes	    = 1,
		.colorspace_mask    = V4L2_COLORSPACE_MASK_RAW,
		.colorspace_default = V4L2_COLORSPACE_RAW,
	},
	{
		.fourcc		    = V4L2_PIX_FMT_PISP_COMP1_GRBG,
		.bit_depth	    = 8,
		.align		    = 32,
		.plane_factor	    = { P3(1.0) },
		.num_planes	    = 1,
		.colorspace_mask    = V4L2_COLORSPACE_MASK_RAW,
		.colorspace_default = V4L2_COLORSPACE_RAW,
	},
	{
		.fourcc		    = V4L2_PIX_FMT_PISP_COMP1_GBRG,
		.bit_depth	    = 8,
		.align		    = 32,
		.plane_factor	    = { P3(1.0) },
		.num_planes	    = 1,
		.colorspace_mask    = V4L2_COLORSPACE_MASK_RAW,
		.colorspace_default = V4L2_COLORSPACE_RAW,
	},
	/* Greyscale Formats */
	{
		.fourcc		= V4L2_PIX_FMT_GREY,
		.bit_depth	= 8,
		.align		= 32,
		.num_planes	= 1,
		.colorspace_mask    = V4L2_COLORSPACE_MASK_RAW,
		.colorspace_default = V4L2_COLORSPACE_RAW,
	},
	{
		.fourcc		= V4L2_PIX_FMT_Y16,
		.bit_depth	= 16,
		.align		= 32,
		.plane_factor	= { P3(1.0) },
		.num_planes	= 1,
		.colorspace_mask    = V4L2_COLORSPACE_MASK_RAW,
		.colorspace_default = V4L2_COLORSPACE_RAW,
	},
	{
		.fourcc		= V4L2_PIX_FMT_PISP_COMP1_MONO,
		.bit_depth	= 8,
		.align		= 32,
		.plane_factor	= { P3(1.0) },
		.num_planes	= 1,
		.colorspace_mask    = V4L2_COLORSPACE_MASK_RAW,
		.colorspace_default = V4L2_COLORSPACE_RAW,
	},
};

static const struct pisp_be_format meta_out_supported_formats[] = {
	/* Configuration buffer format. */
	{
		.fourcc		    = V4L2_META_FMT_RPI_BE_CFG,
	},
};

#endif /* _PISP_BE_FORMATS_ */
