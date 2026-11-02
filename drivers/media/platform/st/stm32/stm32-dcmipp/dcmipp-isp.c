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

#define DCMIPP_P1FSCR	0x804
#define DCMIPP_P1FSCR_PIPEDIFF BIT(18)

#define DCMIPP_P1SRCR	0x820
#define DCMIPP_P1SRCR_LASTLINE_SHIFT	0
#define DCMIPP_P1SRCR_FIRSTLINEDEL_SHIFT	12
#define DCMIPP_P1SRCR_CROPEN		BIT(15)

#define DCMIPP_P1DECR	0x830
#define DCMIPP_P1DECR_ENABLE		BIT(0)
#define DCMIPP_P1DECR_HDEC_SHIFT	1
#define DCMIPP_P1DECR_VDEC_SHIFT	3

#define DCMIPP_P1DMCR	0x870
#define DCMIPP_P1DMCR_ENABLE		BIT(0)
#define DCMIPP_P1DMCR_TYPE_SHIFT	1
#define DCMIPP_P1DMCR_TYPE_MASK		GENMASK(2, 1)
#define DCMIPP_P1DMCR_TYPE_RGGB		0x0
#define DCMIPP_P1DMCR_TYPE_GRBG		0x1
#define DCMIPP_P1DMCR_TYPE_GBRG		0x2
#define DCMIPP_P1DMCR_TYPE_BGGR		0x3

#define ISP_MEDIA_BUS_SINK_FMT_DEFAULT MEDIA_BUS_FMT_RGB565_1X16
#define ISP_MEDIA_BUS_SRC_FMT_DEFAULT MEDIA_BUS_FMT_RGB888_1X24

struct dcmipp_isp_device {
	struct dcmipp_ent_device ved;
	struct v4l2_subdev sd;
	struct device *dev;

	void __iomem *regs;
};

static const struct v4l2_mbus_framefmt fmt_default = {
	.width = DCMIPP_FMT_WIDTH_DEFAULT,
	.height = DCMIPP_FMT_HEIGHT_DEFAULT,
	.code = ISP_MEDIA_BUS_SINK_FMT_DEFAULT,
	.field = V4L2_FIELD_NONE,
	.colorspace = DCMIPP_COLORSPACE_DEFAULT,
	.ycbcr_enc = DCMIPP_YCBCR_ENC_DEFAULT,
	.quantization = DCMIPP_QUANTIZATION_DEFAULT,
	.xfer_func = DCMIPP_XFER_FUNC_DEFAULT,
};

static inline unsigned int dcmipp_isp_set_compose(__u32 size, __u32 req)
{
	unsigned int i = 0;

	if (req > size)
		return size;

	/* Maximum decimation factor is 8 */
	while (size > req && i++ < 3)
		size /= 2;

	return size;
}

static void dcmipp_isp_adjust_fmt(struct v4l2_mbus_framefmt *fmt, u32 pad)
{
	/* Only accept code in the pix map table */
	if (!dcmipp_pixelpipe_pix_map_by_code(fmt->code, DCMIPP_ISP, pad))
		fmt->code = IS_SRC(pad) ? ISP_MEDIA_BUS_SRC_FMT_DEFAULT :
					  ISP_MEDIA_BUS_SINK_FMT_DEFAULT;

	fmt->width = clamp_t(u32, fmt->width, DCMIPP_FRAME_MIN_WIDTH,
			     DCMIPP_FRAME_MAX_WIDTH) & ~1;
	fmt->height = clamp_t(u32, fmt->height, DCMIPP_FRAME_MIN_HEIGHT,
			      DCMIPP_FRAME_MAX_HEIGHT);

	if (fmt->field == V4L2_FIELD_ANY || fmt->field == V4L2_FIELD_ALTERNATE)
		fmt->field = V4L2_FIELD_NONE;

	dcmipp_colorimetry_clamp(fmt);
}

static int dcmipp_isp_init_state(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state)
{
	for (unsigned int i = 0; i < sd->entity.num_pads; i++) {
		struct v4l2_mbus_framefmt *mf;

		mf = v4l2_subdev_state_get_format(state, i);
		*mf = fmt_default;
		mf->code = IS_SRC(i) ? ISP_MEDIA_BUS_SRC_FMT_DEFAULT :
				       ISP_MEDIA_BUS_SINK_FMT_DEFAULT;

		if (IS_SINK(i)) {
			struct v4l2_rect r = {
				.top = 0,
				.left = 0,
				.width = DCMIPP_FMT_WIDTH_DEFAULT,
				.height = DCMIPP_FMT_HEIGHT_DEFAULT,
			};

			*v4l2_subdev_state_get_crop(state, i) = r;
			*v4l2_subdev_state_get_compose(state, i) = r;
		}
	}

	return 0;
}

static int dcmipp_isp_enum_mbus_code(struct v4l2_subdev *sd,
				     struct v4l2_subdev_state *state,
				     struct v4l2_subdev_mbus_code_enum *code)
{
	return dcmipp_pixelpipe_enum_mbus_code(DCMIPP_ISP, code);
}

static int dcmipp_isp_enum_frame_size(struct v4l2_subdev *sd,
				      struct v4l2_subdev_state *state,
				      struct v4l2_subdev_frame_size_enum *fse)
{
	return dcmipp_pixelpipe_enum_frame_size(DCMIPP_ISP, fse);
}

static int dcmipp_isp_set_fmt(struct v4l2_subdev *sd,
			      const struct v4l2_subdev_client_info *ci,
			      struct v4l2_subdev_state *state,
			      struct v4l2_subdev_format *fmt)
{
	if (fmt->which == V4L2_SUBDEV_FORMAT_ACTIVE &&
	    v4l2_subdev_is_streaming(sd))
		return -EBUSY;

	dcmipp_isp_adjust_fmt(&fmt->format, fmt->pad);

	if (IS_SINK(fmt->pad)) {
		struct v4l2_mbus_framefmt *src_fmt =
			v4l2_subdev_state_get_format(state, 1);
		struct v4l2_rect r = {
			.top = 0,
			.left = 0,
			.width = fmt->format.width,
			.height = fmt->format.height,
		};

		/* Adjust SINK pad crop/compose */
		*v4l2_subdev_state_get_crop(state, 0) = r;
		*v4l2_subdev_state_get_compose(state, 0) = r;

		/* Forward format to SRC pads */
		*src_fmt = fmt->format;
		src_fmt->code = dcmipp_pixelpipe_src_format(fmt->format.code);
		*v4l2_subdev_state_get_format(state, 2) = *src_fmt;
	} else {
		struct v4l2_mbus_framefmt *sink_fmt =
			v4l2_subdev_state_get_format(state, 0);
		struct v4l2_rect *compose =
			v4l2_subdev_state_get_compose(state, 0);

		fmt->format = *sink_fmt;
		fmt->format.code = dcmipp_pixelpipe_src_format(sink_fmt->code);
		if (compose->width && compose->height) {
			fmt->format.width = compose->width;
			fmt->format.height = compose->height;
		}
		/* Set to the 2nd SRC pad */
		*v4l2_subdev_state_get_format(state, fmt->pad == 1 ? 2 : 1) =
			fmt->format;
	}

	/* Update the selected pad format */
	*v4l2_subdev_state_get_format(state, fmt->pad) = fmt->format;

	return 0;
}

static void dcmipp_isp_adjust_crop(struct v4l2_rect *r,
				   const struct v4l2_mbus_framefmt *fmt)
{
	struct v4l2_rect crop_min = {
		.width = fmt->width,
		.height = DCMIPP_FRAME_MIN_HEIGHT,
	};

	/*
	 * Crop is related here to the statistics removal, that is
	 * firsts and/or lasts lines of the frame. Up to the first
	 * 7 lines can be skipped
	 */
#define DCMIPP_ISP_MAX_TOP_CROP	7
	v4l2_rect_set_min_size(r, &crop_min);
	if (r->top > DCMIPP_ISP_MAX_TOP_CROP)
		r->top = DCMIPP_ISP_MAX_TOP_CROP;
	if ((r->height + r->top) > fmt->height)
		r->height = fmt->height - r->top;
}

static int dcmipp_isp_set_selection(struct v4l2_subdev *sd,
				    const struct v4l2_subdev_client_info *ci,
				    struct v4l2_subdev_state *state,
				    struct v4l2_subdev_selection *s)
{
	struct dcmipp_isp_device *isp = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *sink_fmt, *src_fmt;
	struct v4l2_rect *crop, *compose;
	__u32 pad;

	if (IS_SRC(s->pad))
		return -EINVAL;

	if (s->which == V4L2_SUBDEV_FORMAT_ACTIVE &&
	    v4l2_subdev_is_streaming(sd))
		return -EBUSY;

	crop = v4l2_subdev_state_get_crop(state, s->pad);
	compose = v4l2_subdev_state_get_compose(state, s->pad);

	switch (s->target) {
	case V4L2_SEL_TGT_CROP:
		sink_fmt = v4l2_subdev_state_get_format(state, s->pad);
		dcmipp_isp_adjust_crop(&s->r, sink_fmt);

		*crop = s->r;
		*compose = s->r;

		dev_dbg(isp->dev, "s_selection: crop (%d,%d)/%ux%u\n",
			crop->left, crop->top, crop->width, crop->height);
		break;
	case V4L2_SEL_TGT_COMPOSE:
		s->r.top = 0;
		s->r.left = 0;
		if (s->r.width < DCMIPP_FRAME_MIN_WIDTH)
			s->r.width = DCMIPP_FRAME_MIN_WIDTH;
		s->r.width = dcmipp_isp_set_compose(crop->width, s->r.width);
		if (s->r.height < DCMIPP_FRAME_MIN_HEIGHT)
			s->r.height = DCMIPP_FRAME_MIN_HEIGHT;
		s->r.height = dcmipp_isp_set_compose(crop->height, s->r.height);
		*compose = s->r;

		dev_dbg(isp->dev, "s_selection: compose (%d,%d)/%ux%u\n",
			compose->left, compose->top,
			compose->width, compose->height);
		break;
	default:
		return -EINVAL;
	}

	/* Update the source pad size */
	for (pad = 1; pad < sd->entity.num_pads; pad++) {
		src_fmt = v4l2_subdev_state_get_format(state, pad);
		src_fmt->width = s->r.width;
		src_fmt->height = s->r.height;
	}

	return 0;
}

#define STM32_DCMIPP_IS_BAYER_VARIANT(code, variant)	\
	((code) == MEDIA_BUS_FMT_S##variant##8_1X8 ||	\
	 (code) == MEDIA_BUS_FMT_S##variant##10_1X10 ||	\
	 (code) == MEDIA_BUS_FMT_S##variant##12_1X12 ||	\
	 (code) == MEDIA_BUS_FMT_S##variant##14_1X14 ||	\
	 (code) == MEDIA_BUS_FMT_S##variant##16_1X16)
static void dcmipp_isp_config_demosaicing(struct dcmipp_isp_device *isp,
					  struct v4l2_subdev_state *state)
{
	__u32 code = v4l2_subdev_state_get_format(state, 0)->code;
	unsigned int val = 0;

	/* Disable demosaicing */
	reg_clear(isp, DCMIPP_P1DMCR,
		  DCMIPP_P1DMCR_ENABLE | DCMIPP_P1DMCR_TYPE_MASK);

	/* Only perform demosaicing if format is bayer */
	if (code < MEDIA_BUS_FMT_SBGGR8_1X8 || code >= MEDIA_BUS_FMT_JPEG_1X8)
		return;

	dev_dbg(isp->dev, "Input is RawBayer, enable Demosaicing\n");

	if (STM32_DCMIPP_IS_BAYER_VARIANT(code, BGGR))
		val = DCMIPP_P1DMCR_TYPE_BGGR << DCMIPP_P1DMCR_TYPE_SHIFT;
	else if (STM32_DCMIPP_IS_BAYER_VARIANT(code, GBRG))
		val = DCMIPP_P1DMCR_TYPE_GBRG << DCMIPP_P1DMCR_TYPE_SHIFT;
	else if (STM32_DCMIPP_IS_BAYER_VARIANT(code, GRBG))
		val = DCMIPP_P1DMCR_TYPE_GRBG << DCMIPP_P1DMCR_TYPE_SHIFT;
	else if (STM32_DCMIPP_IS_BAYER_VARIANT(code, RGGB))
		val = DCMIPP_P1DMCR_TYPE_RGGB << DCMIPP_P1DMCR_TYPE_SHIFT;

	val |= DCMIPP_P1DMCR_ENABLE;

	reg_set(isp, DCMIPP_P1DMCR, val);
}

static bool dcmipp_isp_is_aux_output_enabled(struct dcmipp_isp_device *isp)
{
	struct media_link *link;

	for_each_media_entity_data_link(isp->ved.ent, link) {
		if (link->source != &isp->ved.pads[2])
			continue;

		if (!(link->flags & MEDIA_LNK_FL_ENABLED))
			continue;

		if (!strcmp(link->sink->entity->name, "dcmipp_aux_postproc"))
			return true;
	}

	return false;
}

static void dcmipp_isp_config_decimation(struct dcmipp_isp_device *isp,
					 struct v4l2_subdev_state *state)
{
	struct v4l2_rect *crop = v4l2_subdev_state_get_crop(state, 0);
	struct v4l2_rect *compose = v4l2_subdev_state_get_compose(state, 0);
	u32 decr;

	decr = (fls(crop->width / compose->width) - 1) << DCMIPP_P1DECR_HDEC_SHIFT |
	       (fls(crop->height / compose->height) - 1) << DCMIPP_P1DECR_VDEC_SHIFT;
	if (decr)
		decr |= DCMIPP_P1DECR_ENABLE;

	reg_write(isp, DCMIPP_P1DECR, decr);
}

static int dcmipp_isp_enable_streams(struct v4l2_subdev *sd,
				     struct v4l2_subdev_state *state,
				     u32 pad, u64 streams_mask)
{
	struct dcmipp_isp_device *isp = v4l2_get_subdevdata(sd);
	struct v4l2_rect *crop = v4l2_subdev_state_get_crop(state, 0);
	struct v4l2_subdev *s_subdev;
	struct media_pad *s_pad;
	int ret;

	/* Perform configuration only if no other pad is enabled */
	if (sd->enabled_pads)
		return 0;

	/* Get source subdev */
	s_pad = media_pad_remote_pad_first(&sd->entity.pads[0]);
	if (!s_pad || !is_media_entity_v4l2_subdev(s_pad->entity))
		return -EINVAL;
	s_subdev = media_entity_to_v4l2_subdev(s_pad->entity);

	/* Check if link between ISP & Pipe2 postproc is enabled */
	if (dcmipp_isp_is_aux_output_enabled(isp))
		reg_clear(isp, DCMIPP_P1FSCR, DCMIPP_P1FSCR_PIPEDIFF);
	else
		reg_set(isp, DCMIPP_P1FSCR, DCMIPP_P1FSCR_PIPEDIFF);

	/* Configure Statistic Removal */
	crop = v4l2_subdev_state_get_crop(state, 0);
	reg_write(isp, DCMIPP_P1SRCR,
		  ((crop->top << DCMIPP_P1SRCR_FIRSTLINEDEL_SHIFT) |
		   (crop->height << DCMIPP_P1SRCR_LASTLINE_SHIFT) |
		   DCMIPP_P1SRCR_CROPEN));

	/* Configure Decimation */
	dcmipp_isp_config_decimation(isp, state);

	/* Configure Demosaicing */
	dcmipp_isp_config_demosaicing(isp, state);

	ret = v4l2_subdev_enable_streams(s_subdev, s_pad->index, BIT_ULL(0));
	if (ret < 0) {
		dev_err(isp->dev,
			"failed to start source subdev streaming (%d)\n", ret);
		return ret;
	}

	return 0;
}

static int dcmipp_isp_disable_streams(struct v4l2_subdev *sd,
				      struct v4l2_subdev_state *state,
				      u32 pad, u64 streams_mask)
{
	struct dcmipp_isp_device *isp = v4l2_get_subdevdata(sd);
	struct v4l2_subdev *s_subdev;
	struct media_pad *s_pad;
	int ret;

	/* Don't do anything if there are still other pads enabled */
	if ((sd->enabled_pads & ~BIT(pad)))
		return 0;

	/* Get source subdev */
	s_pad = media_pad_remote_pad_first(&sd->entity.pads[0]);
	if (!s_pad || !is_media_entity_v4l2_subdev(s_pad->entity))
		return -EINVAL;
	s_subdev = media_entity_to_v4l2_subdev(s_pad->entity);

	/* Disable all blocks */
	reg_write(isp, DCMIPP_P1SRCR, 0);
	reg_write(isp, DCMIPP_P1DECR, 0);
	reg_write(isp, DCMIPP_P1DMCR, 0);

	ret = v4l2_subdev_disable_streams(s_subdev, s_pad->index, BIT_ULL(0));
	if (ret < 0) {
		dev_err(isp->dev,
			"failed to disable source subdev streaming (%d)\n", ret);
		return ret;
	}

	return 0;
}

static const struct v4l2_subdev_pad_ops dcmipp_isp_pad_ops = {
	.enum_mbus_code		= dcmipp_isp_enum_mbus_code,
	.enum_frame_size	= dcmipp_isp_enum_frame_size,
	.get_fmt		= v4l2_subdev_get_fmt,
	.set_fmt		= dcmipp_isp_set_fmt,
	.get_selection		= dcmipp_pixelpipe_get_selection,
	.set_selection		= dcmipp_isp_set_selection,
	.enable_streams		= dcmipp_isp_enable_streams,
	.disable_streams	= dcmipp_isp_disable_streams,
};

static const struct v4l2_subdev_video_ops dcmipp_isp_video_ops = {
	.s_stream = v4l2_subdev_s_stream_helper,
};

static const struct v4l2_subdev_ops dcmipp_isp_ops = {
	.pad = &dcmipp_isp_pad_ops,
	.video = &dcmipp_isp_video_ops,
};

static void dcmipp_isp_release(struct v4l2_subdev *sd)
{
	struct dcmipp_isp_device *isp = v4l2_get_subdevdata(sd);

	kfree(isp);
}

static const struct v4l2_subdev_internal_ops dcmipp_isp_int_ops = {
	.init_state = dcmipp_isp_init_state,
	.release = dcmipp_isp_release,
};

void dcmipp_isp_ent_release(struct dcmipp_ent_device *ved)
{
	struct dcmipp_isp_device *isp =
			container_of(ved, struct dcmipp_isp_device, ved);

	dcmipp_ent_sd_unregister(ved, &isp->sd);
}

struct dcmipp_ent_device *dcmipp_isp_ent_init(const char *entity_name,
					      struct dcmipp_device *dcmipp)
{
	struct dcmipp_isp_device *isp;
	const unsigned long pads_flag[] = {
		MEDIA_PAD_FL_SINK, MEDIA_PAD_FL_SOURCE,
		MEDIA_PAD_FL_SOURCE,
	};
	int ret;

	/* Allocate the isp struct */
	isp = kzalloc_obj(*isp);
	if (!isp)
		return ERR_PTR(-ENOMEM);

	isp->regs = dcmipp->regs;

	/* Initialize ved and sd */
	ret = dcmipp_ent_sd_register(&isp->ved, &isp->sd,
				     &dcmipp->v4l2_dev, entity_name,
				     MEDIA_ENT_F_PROC_VIDEO_PIXEL_FORMATTER,
				     ARRAY_SIZE(pads_flag), pads_flag,
				     &dcmipp_isp_int_ops, &dcmipp_isp_ops,
				     NULL, NULL);
	if (ret) {
		kfree(isp);
		return ERR_PTR(ret);
	}

	isp->ved.dcmipp = dcmipp;
	isp->dev = dcmipp->dev;

	return &isp->ved;
}
