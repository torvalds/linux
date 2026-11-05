// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for STM32 Digital Camera Memory Interface Pixel Processor
 *
 * Copyright (C) STMicroelectronics SA 2026
 * Authors: Hugues Fruchet <hugues.fruchet@foss.st.com>
 *          Alain Volmat <alain.volmat@foss.st.com>
 *          for STMicroelectronics.
 */

#include <linux/v4l2-mediabus.h>
#include <media/v4l2-rect.h>
#include <media/v4l2-subdev.h>

#include "dcmipp-common.h"
#include "dcmipp-pixelcommon.h"

#define DCMIPP_ENT(id, pad) (1 << (2 * (id) + (pad)))
#define DCMIPP_ISP_SINK			(DCMIPP_ENT(DCMIPP_ISP, 0))
#define DCMIPP_ISP_SRC			(DCMIPP_ENT(DCMIPP_ISP, 1))
#define DCMIPP_ISP_INOUT		(DCMIPP_ISP_SINK | DCMIPP_ISP_SRC)
#define DCMIPP_MAIN_POSTPROC_SINK	(DCMIPP_ENT(DCMIPP_MAIN, 0))
#define DCMIPP_MAIN_POSTPROC_SRC	(DCMIPP_ENT(DCMIPP_MAIN, 1))
#define DCMIPP_MAIN_POSTPROC_INOUT					\
	(DCMIPP_MAIN_POSTPROC_SINK | DCMIPP_MAIN_POSTPROC_SRC)
#define DCMIPP_AUX_POSTPROC_SINK	(DCMIPP_ENT(DCMIPP_AUX, 0))
#define DCMIPP_AUX_POSTPROC_SRC	(DCMIPP_ENT(DCMIPP_AUX, 1))
#define DCMIPP_AUX_POSTPROC_INOUT					\
	(DCMIPP_AUX_POSTPROC_SINK | DCMIPP_AUX_POSTPROC_SRC)
#define DCMIPP_ALL_POSTPROC_SINK					\
	(DCMIPP_MAIN_POSTPROC_SINK | DCMIPP_AUX_POSTPROC_SINK)
#define DCMIPP_ALL_POSTPROC_INOUT					\
	(DCMIPP_MAIN_POSTPROC_INOUT | DCMIPP_AUX_POSTPROC_INOUT)

#define PIXMAP_MBUS(mbus, applicable_pipes)		\
	{						\
		.code = MEDIA_BUS_FMT_##mbus,		\
		.pipes = applicable_pipes,		\
	}
static const struct dcmipp_pixelpipe_pix_map
dcmipp_pixel_formats_list[] = {
	/* RGB formats */
	/* RGB565 / RGB888 */
	PIXMAP_MBUS(RGB565_2X8_LE, DCMIPP_AUX_POSTPROC_SINK | DCMIPP_ISP_SINK),
	PIXMAP_MBUS(RGB565_1X16, DCMIPP_AUX_POSTPROC_SINK | DCMIPP_ISP_SINK),
	PIXMAP_MBUS(RGB888_3X8, DCMIPP_AUX_POSTPROC_SINK | DCMIPP_ISP_SINK),
	PIXMAP_MBUS(RGB888_1X24, DCMIPP_ALL_POSTPROC_INOUT | DCMIPP_ISP_INOUT),
	/* YUV formats */
	PIXMAP_MBUS(YUYV8_2X8, DCMIPP_AUX_POSTPROC_SINK | DCMIPP_ISP_SINK),
	PIXMAP_MBUS(UYVY8_1X16, DCMIPP_AUX_POSTPROC_SINK | DCMIPP_ISP_SINK),
	PIXMAP_MBUS(YUV8_1X24, DCMIPP_ALL_POSTPROC_INOUT | DCMIPP_ISP_SRC),
	/* GREY */
	PIXMAP_MBUS(Y8_1X8, DCMIPP_AUX_POSTPROC_SINK | DCMIPP_ISP_SINK),
	PIXMAP_MBUS(Y10_1X10, DCMIPP_AUX_POSTPROC_SINK | DCMIPP_ISP_SINK),
	PIXMAP_MBUS(Y12_1X12, DCMIPP_AUX_POSTPROC_SINK | DCMIPP_ISP_SINK),
	PIXMAP_MBUS(Y14_1X14, DCMIPP_AUX_POSTPROC_SINK | DCMIPP_ISP_SINK),
	/* Raw Bayer */
	/* Raw 8 */
	PIXMAP_MBUS(SBGGR8_1X8, DCMIPP_ISP_SINK),
	PIXMAP_MBUS(SGBRG8_1X8, DCMIPP_ISP_SINK),
	PIXMAP_MBUS(SGRBG8_1X8, DCMIPP_ISP_SINK),
	PIXMAP_MBUS(SRGGB8_1X8, DCMIPP_ISP_SINK),
	/* Raw 10 */
	PIXMAP_MBUS(SBGGR10_1X10, DCMIPP_ISP_SINK),
	PIXMAP_MBUS(SGBRG10_1X10, DCMIPP_ISP_SINK),
	PIXMAP_MBUS(SGRBG10_1X10, DCMIPP_ISP_SINK),
	PIXMAP_MBUS(SRGGB10_1X10, DCMIPP_ISP_SINK),
	/* Raw 12 */
	PIXMAP_MBUS(SBGGR12_1X12, DCMIPP_ISP_SINK),
	PIXMAP_MBUS(SGBRG12_1X12, DCMIPP_ISP_SINK),
	PIXMAP_MBUS(SGRBG12_1X12, DCMIPP_ISP_SINK),
	PIXMAP_MBUS(SRGGB12_1X12, DCMIPP_ISP_SINK),
	/* Raw 14 */
	PIXMAP_MBUS(SBGGR14_1X14, DCMIPP_ISP_SINK),
	PIXMAP_MBUS(SGBRG14_1X14, DCMIPP_ISP_SINK),
	PIXMAP_MBUS(SGRBG14_1X14, DCMIPP_ISP_SINK),
	PIXMAP_MBUS(SRGGB14_1X14, DCMIPP_ISP_SINK),
};

const struct dcmipp_pixelpipe_pix_map *
dcmipp_pixelpipe_pix_map_by_code(__u32 code, unsigned int id, unsigned int pad)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(dcmipp_pixel_formats_list); i++) {
		if (dcmipp_pixel_formats_list[i].code == code &&
		    dcmipp_pixel_formats_list[i].pipes & DCMIPP_ENT(id, pad))
			return &dcmipp_pixel_formats_list[i];
	}

	return NULL;
}

int dcmipp_pixelpipe_enum_mbus_code(unsigned int id,
				    struct v4l2_subdev_mbus_code_enum *code)
{
	unsigned int index = code->index;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(dcmipp_pixel_formats_list); i++) {
		if (!(dcmipp_pixel_formats_list[i].pipes &
		      DCMIPP_ENT(id, code->pad)))
			continue;

		if (index == 0)
			break;

		index--;
	}

	if (i == ARRAY_SIZE(dcmipp_pixel_formats_list))
		return -EINVAL;

	code->code = dcmipp_pixel_formats_list[i].code;

	return 0;
}

int dcmipp_pixelpipe_enum_frame_size(unsigned int id,
				     struct v4l2_subdev_frame_size_enum *fse)
{
	const struct dcmipp_pixelpipe_pix_map *vpix;

	if (fse->index)
		return -EINVAL;

	/* Only accept code in the pix map table */
	vpix = dcmipp_pixelpipe_pix_map_by_code(fse->code, id, fse->pad);
	if (!vpix)
		return -EINVAL;

	fse->min_width = DCMIPP_FRAME_MIN_WIDTH;
	fse->max_width = DCMIPP_FRAME_MAX_WIDTH;
	fse->min_height = DCMIPP_FRAME_MIN_HEIGHT;
	fse->max_height = DCMIPP_FRAME_MAX_HEIGHT;

	return 0;
}

int dcmipp_pixelpipe_get_selection(struct v4l2_subdev *sd,
				   const struct v4l2_subdev_client_info *ci,
				   struct v4l2_subdev_state *state,
				   struct v4l2_subdev_selection *s)
{
	struct v4l2_mbus_framefmt *sink_fmt;

	if (IS_SRC(s->pad))
		return -EINVAL;

	switch (s->target) {
	case V4L2_SEL_TGT_CROP:
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
	case V4L2_SEL_TGT_COMPOSE_DEFAULT:
		s->r = *v4l2_subdev_state_get_crop(state, s->pad);
		break;
	case V4L2_SEL_TGT_CROP_BOUNDS:
	case V4L2_SEL_TGT_CROP_DEFAULT:
		sink_fmt = v4l2_subdev_state_get_format(state, s->pad);
		s->r.top = 0;
		s->r.left = 0;
		s->r.width = sink_fmt->width;
		s->r.height = sink_fmt->height;
		break;
	case V4L2_SEL_TGT_COMPOSE:
		s->r = *v4l2_subdev_state_get_compose(state, s->pad);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

__u32 dcmipp_pixelpipe_src_format(__u32 input_format)
{
	if (input_format >= MEDIA_BUS_FMT_Y8_1X8 &&
	    input_format < MEDIA_BUS_FMT_SBGGR8_1X8)
		return MEDIA_BUS_FMT_YUV8_1X24;

	return MEDIA_BUS_FMT_RGB888_1X24;
}
