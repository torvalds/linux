// SPDX-License-Identifier: GPL-2.0+
/*
 * vsp1_uds.c  --  R-Car VSP1 Up and Down Scaler
 *
 * Copyright (C) 2013-2014 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 */

#include <linux/device.h>
#include <linux/gfp.h>

#include <media/v4l2-subdev.h>

#include "vsp1.h"
#include "vsp1_dl.h"
#include "vsp1_pipe.h"
#include "vsp1_uds.h"

#define UDS_MIN_SIZE				4U
#define UDS_MAX_SIZE				8190U

#define UDS_MIN_FACTOR				0x0100
#define UDS_MAX_FACTOR				0xffff

/* -----------------------------------------------------------------------------
 * Device Access
 */

static inline void vsp1_uds_write(struct vsp1_uds *uds,
				  struct vsp1_dl_body *dlb, u32 reg, u32 data)
{
	vsp1_dl_body_write(dlb, reg + uds->entity.index * VI6_UDS_OFFSET, data);
}

/* -----------------------------------------------------------------------------
 * Scaling Computation
 */

void vsp1_uds_set_alpha(struct vsp1_entity *entity, struct vsp1_dl_body *dlb,
			unsigned int alpha)
{
	struct vsp1_uds *uds = to_uds(&entity->subdev);

	vsp1_uds_write(uds, dlb, VI6_UDS_ALPVAL,
		       alpha << VI6_UDS_ALPVAL_VAL0_SHIFT);
}

/*
 * uds_output_size - Return the output size for an input size and scaling ratio
 * @input: input size in pixels
 * @ratio: scaling ratio in U4.12 fixed-point format
 */
static unsigned int uds_output_size(unsigned int input, unsigned int ratio)
{
	if (ratio > 4096) {
		/* Down-scaling */
		unsigned int mp;

		mp = ratio / 4096;
		mp = mp < 4 ? 1 : (mp < 8 ? 2 : 4);

		return (input - 1) / mp * mp * 4096 / ratio + 1;
	} else {
		/* Up-scaling */
		return (input - 1) * 4096 / ratio + 1;
	}
}

/*
 * uds_output_limits - Return the min and max output sizes for an input size
 * @input: input size in pixels
 * @minimum: minimum output size (returned)
 * @maximum: maximum output size (returned)
 */
static void uds_output_limits(unsigned int input,
			      unsigned int *minimum, unsigned int *maximum)
{
	*minimum = max(uds_output_size(input, UDS_MAX_FACTOR), UDS_MIN_SIZE);
	*maximum = min(uds_output_size(input, UDS_MIN_FACTOR), UDS_MAX_SIZE);
}

/*
 * uds_passband_width - Return the passband filter width for a scaling ratio
 * @ratio: scaling ratio in U4.12 fixed-point format
 */
static unsigned int uds_passband_width(unsigned int ratio)
{
	if (ratio >= 4096) {
		/* Down-scaling */
		unsigned int mp;

		mp = ratio / 4096;
		mp = mp < 4 ? 1 : (mp < 8 ? 2 : 4);

		return 64 * 4096 * mp / ratio;
	} else {
		/* Up-scaling */
		return 64;
	}
}

static unsigned int uds_compute_ratio(unsigned int input, unsigned int output)
{
	/* TODO: This is an approximation that will need to be refined. */
	return (input - 1) * 4096 / (output - 1);
}

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Pad Operations
 */

static int uds_enum_mbus_code(struct v4l2_subdev *subdev,
			      struct v4l2_subdev_pad_config *cfg,
			      struct v4l2_subdev_mbus_code_enum *code)
{
	static const unsigned int codes[] = {
		MEDIA_BUS_FMT_ARGB8888_1X32,
		MEDIA_BUS_FMT_AYUV8_1X32,
	};

	return vsp1_subdev_enum_mbus_code(subdev, cfg, code, codes,
					  ARRAY_SIZE(codes));
}

static int uds_enum_frame_size(struct v4l2_subdev *subdev,
			       struct v4l2_subdev_pad_config *cfg,
			       struct v4l2_subdev_frame_size_enum *fse)
{
	struct vsp1_uds *uds = to_uds(subdev);
	struct v4l2_subdev_pad_config *config;
	struct v4l2_mbus_framefmt *format;
	int ret = 0;

	config = vsp1_entity_get_pad_config(&uds->entity, cfg, fse->which);
	if (!config)
		return -EINVAL;

	format = vsp1_entity_get_pad_format(&uds->entity, config,
					    UDS_PAD_SINK);

	mutex_lock(&uds->entity.lock);

	if (fse->index || fse->code != format->code) {
		ret = -EINVAL;
		goto done;
	}

	if (fse->pad == UDS_PAD_SINK) {
		fse->min_width = UDS_MIN_SIZE;
		fse->max_width = UDS_MAX_SIZE;
		fse->min_height = UDS_MIN_SIZE;
		fse->max_height = UDS_MAX_SIZE;
	} else {
		uds_output_limits(format->width, &fse->min_width,
				  &fse->max_width);
		uds_output_limits(format->height, &fse->min_height,
				  &fse->max_height);
	}

done:
	mutex_unlock(&uds->entity.lock);
	return ret;
}

static void uds_try_format(struct vsp1_uds *uds,
			   struct v4l2_subdev_pad_config *config,
			   unsigned int pad, struct v4l2_mbus_framefmt *fmt)
{
	struct v4l2_mbus_framefmt *format;
	unsigned int minimum;
	unsigned int maximum;

	switch (pad) {
	case UDS_PAD_SINK:
		/* Default to YUV if the requested format is not supported. */
		if (fmt->code != MEDIA_BUS_FMT_ARGB8888_1X32 &&
		    fmt->code != MEDIA_BUS_FMT_AYUV8_1X32)
			fmt->code = MEDIA_BUS_FMT_AYUV8_1X32;

		fmt->width = clamp(fmt->width, UDS_MIN_SIZE, UDS_MAX_SIZE);
		fmt->height = clamp(fmt->height, UDS_MIN_SIZE, UDS_MAX_SIZE);
		break;

	case UDS_PAD_SOURCE:
		/* The UDS scales but can't perform format conversion. */
		format = vsp1_entity_get_pad_format(&uds->entity, config,
						    UDS_PAD_SINK);
		fmt->code = format->code;

		uds_output_limits(format->width, &minimum, &maximum);
		fmt->width = clamp(fmt->width, minimum, maximum);
		uds_output_limits(format->height, &minimum, &maximum);
		fmt->height = clamp(fmt->height, minimum, maximum);
		break;
	}

	fmt->field = V4L2_FIELD_NONE;
	fmt->colorspace = V4L2_COLORSPACE_SRGB;
}

static int uds_set_format(struct v4l2_subdev *subdev,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct vsp1_uds *uds = to_uds(subdev);
	struct v4l2_subdev_pad_config *config;
	struct v4l2_mbus_framefmt *format;
	int ret = 0;

	mutex_lock(&uds->entity.lock);

	config = vsp1_entity_get_pad_config(&uds->entity, cfg, fmt->which);
	if (!config) {
		ret = -EINVAL;
		goto done;
	}

	uds_try_format(uds, config, fmt->pad, &fmt->format);

	format = vsp1_entity_get_pad_format(&uds->entity, config, fmt->pad);
	*format = fmt->format;

	if (fmt->pad == UDS_PAD_SINK) {
		/* Propagate the format to the source pad. */
		format = vsp1_entity_get_pad_format(&uds->entity, config,
						    UDS_PAD_SOURCE);
		*format = fmt->format;

		uds_try_format(uds, config, UDS_PAD_SOURCE, format);
	}

done:
	mutex_unlock(&uds->entity.lock);
	return ret;
}

/* -----------------------------------------------------------------------------
 * V4L2 Subdevice Operations
 */

static const struct v4l2_subdev_pad_ops uds_pad_ops = {
	.init_cfg = vsp1_entity_init_cfg,
	.enum_mbus_code = uds_enum_mbus_code,
	.enum_frame_size = uds_enum_frame_size,
	.get_fmt = vsp1_subdev_get_pad_format,
	.set_fmt = uds_set_format,
};

static const struct v4l2_subdev_ops uds_ops = {
	.pad    = &uds_pad_ops,
};

/* -----------------------------------------------------------------------------
 * VSP1 Entity Operations
 */

static void uds_configure_stream(struct vsp1_entity *entity,
				 struct vsp1_pipeline *pipe,
				 struct vsp1_dl_body *dlb)
{
	struct vsp1_uds *uds = to_uds(&entity->subdev);
	const struct v4l2_mbus_framefmt *output;
	const struct v4l2_mbus_framefmt *input;
	unsigned int hscale;
	unsigned int vscale;
	bool multitap;

	input = vsp1_entity_get_pad_format(&uds->entity, uds->entity.config,
					   UDS_PAD_SINK);
	output = vsp1_entity_get_pad_format(&uds->entity, uds->entity.config,
					    UDS_PAD_SOURCE);

	hscale = uds_compute_ratio(input->width, output->width);
	vscale = uds_compute_ratio(input->height, output->height);

	dev_dbg(uds->entity.vsp1->dev, "hscale %u vscale %u\n", hscale, vscale);

	/*
	 * Multi-tap scaling can't be enabled along with alpha scaling when
	 * scaling down with a factor lower than or equal to 1/2 in either
	 * direction.
	 */
	if (uds->scale_alpha && (hscale >= 8192 || vscale >= 8192))
		multitap = false;
	else
		multitap = true;

	vsp1_uds_write(uds, dlb, VI6_UDS_CTRL,
		       (uds->scale_alpha ? VI6_UDS_CTRL_AON : 0) |
		       (multitap ? VI6_UDS_CTRL_BC : 0));

	vsp1_uds_write(uds, dlb, VI6_UDS_PASS_BWIDTH,
		       (uds_passband_width(hscale)
				<< VI6_UDS_PASS_BWIDTH_H_SHIFT) |
		       (uds_passband_width(vscale)
				<< VI6_UDS_PASS_BWIDTH_V_SHIFT));

	/* Set the scaling ratios. */
	vsp1_uds_write(uds, dlb, VI6_UDS_SCALE,
		       (hscale << VI6_UDS_SCALE_HFRAC_SHIFT) |
		       (vscale << VI6_UDS_SCALE_VFRAC_SHIFT));
}

static void uds_configure_partition(struct vsp1_entity *entity,
				    struct vsp1_pipeline *pipe,
				    struct vsp1_dl_list *dl,
				    struct vsp1_dl_body *dlb)
{
	struct vsp1_uds *uds = to_uds(&entity->subdev);
	struct vsp1_partition *partition = pipe->partition;
	const struct v4l2_mbus_framefmt *output;

	output = vsp1_entity_get_pad_format(&uds->entity, uds->entity.config,
					    UDS_PAD_SOURCE);

	/* Input size clipping. */
	vsp1_uds_write(uds, dlb, VI6_UDS_HSZCLIP, VI6_UDS_HSZCLIP_HCEN |
		       (0 << VI6_UDS_HSZCLIP_HCL_OFST_SHIFT) |
		       (partition->uds_sink.width
				<< VI6_UDS_HSZCLIP_HCL_SIZE_SHIFT));

	/* Output size clipping. */
	vsp1_uds_write(uds, dlb, VI6_UDS_CLIP_SIZE,
		       (partition->uds_source.width
				<< VI6_UDS_CLIP_SIZE_HSIZE_SHIFT) |
		       (output->height
				<< VI6_UDS_CLIP_SIZE_VSIZE_SHIFT));
}

static unsigned int uds_max_width(struct vsp1_entity *entity,
				  struct vsp1_pipeline *pipe)
{
	struct vsp1_uds *uds = to_uds(&entity->subdev);
	const struct v4l2_mbus_framefmt *output;
	const struct v4l2_mbus_framefmt *input;
	unsigned int hscale;

	input = vsp1_entity_get_pad_format(&uds->entity, uds->entity.config,
					   UDS_PAD_SINK);
	output = vsp1_entity_get_pad_format(&uds->entity, uds->entity.config,
					    UDS_PAD_SOURCE);
	hscale = output->width / input->width;

	/*
	 * The maximum width of the UDS is 304 pixels. These are input pixels
	 * in the event of up-scaling, and output pixels in the event of
	 * downscaling.
	 *
	 * To support overlapping partition windows we clamp at units of 256 and
	 * the remaining pixels are reserved.
	 */
	if (hscale <= 2)
		return 256;
	else if (hscale <= 4)
		return 512;
	else if (hscale <= 8)
		return 1024;
	else
		return 2048;
}

/* -----------------------------------------------------------------------------
 * Partition Algorithm Support
 */

static void uds_partition(struct vsp1_entity *entity,
			  struct vsp1_pipeline *pipe,
			  struct vsp1_partition *partition,
			  unsigned int partition_idx,
			  struct vsp1_partition_window *window)
{
	struct vsp1_uds *uds = to_uds(&entity->subdev);
	const struct v4l2_mbus_framefmt *output;
	const struct v4l2_mbus_framefmt *input;

	/* Initialise the partition state. */
	partition->uds_sink = *window;
	partition->uds_source = *window;

	input = vsp1_entity_get_pad_format(&uds->entity, uds->entity.config,
					   UDS_PAD_SINK);
	output = vsp1_entity_get_pad_format(&uds->entity, uds->entity.config,
					    UDS_PAD_SOURCE);

	partition->uds_sink.width = window->width * input->width
				  / output->width;
	partition->uds_sink.left = window->left * input->width
				 / output->width;

	*window = partition->uds_sink;
}

static const struct vsp1_entity_operations uds_entity_ops = {
	.configure_stream = uds_configure_stream,
	.configure_partition = uds_configure_partition,
	.max_width = uds_max_width,
	.partition = uds_partition,
};

/* -----------------------------------------------------------------------------
 * Initialization and Cleanup
 */

struct vsp1_uds *vsp1_uds_create(struct vsp1_device *vsp1, unsigned int index)
{
	struct vsp1_uds *uds;
	char name[6];
	int ret;

	uds = devm_kzalloc(vsp1->dev, sizeof(*uds), GFP_KERNEL);
	if (uds == NULL)
		return ERR_PTR(-ENOMEM);

	uds->entity.ops = &uds_entity_ops;
	uds->entity.type = VSP1_ENTITY_UDS;
	uds->entity.index = index;

	sprintf(name, "uds.%u", index);
	ret = vsp1_entity_init(vsp1, &uds->entity, name, 2, &uds_ops,
			       MEDIA_ENT_F_PROC_VIDEO_SCALER);
	if (ret < 0)
		return ERR_PTR(ret);

	return uds;
}
