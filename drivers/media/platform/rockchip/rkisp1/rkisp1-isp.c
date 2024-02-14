// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Rockchip ISP1 Driver - ISP Subdevice
 *
 * Copyright (C) 2019 Collabora, Ltd.
 *
 * Based on Rockchip ISP1 driver by Rockchip Electronics Co., Ltd.
 * Copyright (C) 2017 Rockchip Electronics Co., Ltd.
 */

#include <linux/iopoll.h>
#include <linux/pm_runtime.h>
#include <linux/videodev2.h>
#include <linux/vmalloc.h>

#include <media/v4l2-event.h>

#include "rkisp1-common.h"

#define RKISP1_DEF_SINK_PAD_FMT MEDIA_BUS_FMT_SRGGB10_1X10
#define RKISP1_DEF_SRC_PAD_FMT MEDIA_BUS_FMT_YUYV8_2X8

#define RKISP1_ISP_DEV_NAME	RKISP1_DRIVER_NAME "_isp"

/*
 * NOTE: MIPI controller and input MUX are also configured in this file.
 * This is because ISP Subdev describes not only ISP submodule (input size,
 * format, output size, format), but also a virtual route device.
 */

/*
 * There are many variables named with format/frame in below code,
 * please see here for their meaning.
 * Cropping in the sink pad defines the image region from the sensor.
 * Cropping in the source pad defines the region for the Image Stabilizer (IS)
 *
 * Cropping regions of ISP
 *
 * +---------------------------------------------------------+
 * | Sensor image                                            |
 * | +---------------------------------------------------+   |
 * | | CIF_ISP_ACQ (for black level)                     |   |
 * | | sink pad format                                   |   |
 * | | +--------------------------------------------+    |   |
 * | | |    CIF_ISP_OUT                             |    |   |
 * | | |    sink pad crop                           |    |   |
 * | | |    +---------------------------------+     |    |   |
 * | | |    |   CIF_ISP_IS                    |     |    |   |
 * | | |    |   source pad crop and format    |     |    |   |
 * | | |    +---------------------------------+     |    |   |
 * | | +--------------------------------------------+    |   |
 * | +---------------------------------------------------+   |
 * +---------------------------------------------------------+
 */

/* ----------------------------------------------------------------------------
 * Camera Interface registers configurations
 */

/*
 * Image Stabilization.
 * This should only be called when configuring CIF
 * or at the frame end interrupt
 */
static void rkisp1_config_ism(struct rkisp1_isp *isp,
			      struct v4l2_subdev_state *sd_state)
{
	const struct v4l2_rect *src_crop =
		v4l2_subdev_state_get_crop(sd_state,
					   RKISP1_ISP_PAD_SOURCE_VIDEO);
	struct rkisp1_device *rkisp1 = isp->rkisp1;
	u32 val;

	rkisp1_write(rkisp1, RKISP1_CIF_ISP_IS_RECENTER, 0);
	rkisp1_write(rkisp1, RKISP1_CIF_ISP_IS_MAX_DX, 0);
	rkisp1_write(rkisp1, RKISP1_CIF_ISP_IS_MAX_DY, 0);
	rkisp1_write(rkisp1, RKISP1_CIF_ISP_IS_DISPLACE, 0);
	rkisp1_write(rkisp1, RKISP1_CIF_ISP_IS_H_OFFS, src_crop->left);
	rkisp1_write(rkisp1, RKISP1_CIF_ISP_IS_V_OFFS, src_crop->top);
	rkisp1_write(rkisp1, RKISP1_CIF_ISP_IS_H_SIZE, src_crop->width);
	rkisp1_write(rkisp1, RKISP1_CIF_ISP_IS_V_SIZE, src_crop->height);

	/* IS(Image Stabilization) is always on, working as output crop */
	rkisp1_write(rkisp1, RKISP1_CIF_ISP_IS_CTRL, 1);
	val = rkisp1_read(rkisp1, RKISP1_CIF_ISP_CTRL);
	val |= RKISP1_CIF_ISP_CTRL_ISP_CFG_UPD;
	rkisp1_write(rkisp1, RKISP1_CIF_ISP_CTRL, val);
}

/*
 * configure ISP blocks with input format, size......
 */
static int rkisp1_config_isp(struct rkisp1_isp *isp,
			     struct v4l2_subdev_state *sd_state,
			     enum v4l2_mbus_type mbus_type, u32 mbus_flags)
{
	struct rkisp1_device *rkisp1 = isp->rkisp1;
	u32 isp_ctrl = 0, irq_mask = 0, acq_mult = 0, acq_prop = 0;
	const struct rkisp1_mbus_info *sink_fmt;
	const struct rkisp1_mbus_info *src_fmt;
	const struct v4l2_mbus_framefmt *src_frm;
	const struct v4l2_mbus_framefmt *sink_frm;
	const struct v4l2_rect *sink_crop;

	sink_frm = v4l2_subdev_state_get_format(sd_state,
						RKISP1_ISP_PAD_SINK_VIDEO);
	sink_crop = v4l2_subdev_state_get_crop(sd_state,
					       RKISP1_ISP_PAD_SINK_VIDEO);
	src_frm = v4l2_subdev_state_get_format(sd_state,
					       RKISP1_ISP_PAD_SOURCE_VIDEO);

	sink_fmt = rkisp1_mbus_info_get_by_code(sink_frm->code);
	src_fmt = rkisp1_mbus_info_get_by_code(src_frm->code);

	if (sink_fmt->pixel_enc == V4L2_PIXEL_ENC_BAYER) {
		acq_mult = 1;
		if (src_fmt->pixel_enc == V4L2_PIXEL_ENC_BAYER) {
			if (mbus_type == V4L2_MBUS_BT656)
				isp_ctrl = RKISP1_CIF_ISP_CTRL_ISP_MODE_RAW_PICT_ITU656;
			else
				isp_ctrl = RKISP1_CIF_ISP_CTRL_ISP_MODE_RAW_PICT;
		} else {
			rkisp1_write(rkisp1, RKISP1_CIF_ISP_DEMOSAIC,
				     RKISP1_CIF_ISP_DEMOSAIC_TH(0xc));

			if (mbus_type == V4L2_MBUS_BT656)
				isp_ctrl = RKISP1_CIF_ISP_CTRL_ISP_MODE_BAYER_ITU656;
			else
				isp_ctrl = RKISP1_CIF_ISP_CTRL_ISP_MODE_BAYER_ITU601;
		}
	} else if (sink_fmt->pixel_enc == V4L2_PIXEL_ENC_YUV) {
		acq_mult = 2;
		if (mbus_type == V4L2_MBUS_CSI2_DPHY) {
			isp_ctrl = RKISP1_CIF_ISP_CTRL_ISP_MODE_ITU601;
		} else {
			if (mbus_type == V4L2_MBUS_BT656)
				isp_ctrl = RKISP1_CIF_ISP_CTRL_ISP_MODE_ITU656;
			else
				isp_ctrl = RKISP1_CIF_ISP_CTRL_ISP_MODE_ITU601;
		}

		irq_mask |= RKISP1_CIF_ISP_DATA_LOSS;
	}

	/* Set up input acquisition properties */
	if (mbus_type == V4L2_MBUS_BT656 || mbus_type == V4L2_MBUS_PARALLEL) {
		if (mbus_flags & V4L2_MBUS_PCLK_SAMPLE_RISING)
			acq_prop |= RKISP1_CIF_ISP_ACQ_PROP_POS_EDGE;

		switch (sink_fmt->bus_width) {
		case 8:
			acq_prop |= RKISP1_CIF_ISP_ACQ_PROP_IN_SEL_8B_ZERO;
			break;
		case 10:
			acq_prop |= RKISP1_CIF_ISP_ACQ_PROP_IN_SEL_10B_ZERO;
			break;
		case 12:
			acq_prop |= RKISP1_CIF_ISP_ACQ_PROP_IN_SEL_12B;
			break;
		default:
			dev_err(rkisp1->dev, "Invalid bus width %u\n",
				sink_fmt->bus_width);
			return -EINVAL;
		}
	}

	if (mbus_type == V4L2_MBUS_PARALLEL) {
		if (mbus_flags & V4L2_MBUS_VSYNC_ACTIVE_LOW)
			acq_prop |= RKISP1_CIF_ISP_ACQ_PROP_VSYNC_LOW;

		if (mbus_flags & V4L2_MBUS_HSYNC_ACTIVE_LOW)
			acq_prop |= RKISP1_CIF_ISP_ACQ_PROP_HSYNC_LOW;
	}

	rkisp1_write(rkisp1, RKISP1_CIF_ISP_CTRL, isp_ctrl);
	rkisp1_write(rkisp1, RKISP1_CIF_ISP_ACQ_PROP,
		     acq_prop | sink_fmt->yuv_seq |
		     RKISP1_CIF_ISP_ACQ_PROP_BAYER_PAT(sink_fmt->bayer_pat) |
		     RKISP1_CIF_ISP_ACQ_PROP_FIELD_SEL_ALL);
	rkisp1_write(rkisp1, RKISP1_CIF_ISP_ACQ_NR_FRAMES, 0);

	/* Acquisition Size */
	rkisp1_write(rkisp1, RKISP1_CIF_ISP_ACQ_H_OFFS, 0);
	rkisp1_write(rkisp1, RKISP1_CIF_ISP_ACQ_V_OFFS, 0);
	rkisp1_write(rkisp1, RKISP1_CIF_ISP_ACQ_H_SIZE,
		     acq_mult * sink_frm->width);
	rkisp1_write(rkisp1, RKISP1_CIF_ISP_ACQ_V_SIZE, sink_frm->height);

	/* ISP Out Area */
	rkisp1_write(rkisp1, RKISP1_CIF_ISP_OUT_H_OFFS, sink_crop->left);
	rkisp1_write(rkisp1, RKISP1_CIF_ISP_OUT_V_OFFS, sink_crop->top);
	rkisp1_write(rkisp1, RKISP1_CIF_ISP_OUT_H_SIZE, sink_crop->width);
	rkisp1_write(rkisp1, RKISP1_CIF_ISP_OUT_V_SIZE, sink_crop->height);

	irq_mask |= RKISP1_CIF_ISP_FRAME | RKISP1_CIF_ISP_V_START |
		    RKISP1_CIF_ISP_PIC_SIZE_ERROR;
	rkisp1_write(rkisp1, RKISP1_CIF_ISP_IMSC, irq_mask);

	if (src_fmt->pixel_enc == V4L2_PIXEL_ENC_BAYER) {
		rkisp1_params_disable(&rkisp1->params);
	} else {
		struct v4l2_mbus_framefmt *src_frm;

		src_frm = v4l2_subdev_state_get_format(sd_state,
						       RKISP1_ISP_PAD_SOURCE_VIDEO);
		rkisp1_params_pre_configure(&rkisp1->params, sink_fmt->bayer_pat,
					    src_frm->quantization,
					    src_frm->ycbcr_enc);
	}

	isp->sink_fmt = sink_fmt;

	return 0;
}

/* Configure MUX */
static void rkisp1_config_path(struct rkisp1_isp *isp,
			       enum v4l2_mbus_type mbus_type)
{
	struct rkisp1_device *rkisp1 = isp->rkisp1;
	u32 dpcl = rkisp1_read(rkisp1, RKISP1_CIF_VI_DPCL);

	if (mbus_type == V4L2_MBUS_BT656 || mbus_type == V4L2_MBUS_PARALLEL)
		dpcl |= RKISP1_CIF_VI_DPCL_IF_SEL_PARALLEL;
	else if (mbus_type == V4L2_MBUS_CSI2_DPHY)
		dpcl |= RKISP1_CIF_VI_DPCL_IF_SEL_MIPI;

	rkisp1_write(rkisp1, RKISP1_CIF_VI_DPCL, dpcl);
}

/* Hardware configure Entry */
static int rkisp1_config_cif(struct rkisp1_isp *isp,
			     struct v4l2_subdev_state *sd_state,
			     enum v4l2_mbus_type mbus_type, u32 mbus_flags)
{
	int ret;

	ret = rkisp1_config_isp(isp, sd_state, mbus_type, mbus_flags);
	if (ret)
		return ret;

	rkisp1_config_path(isp, mbus_type);
	rkisp1_config_ism(isp, sd_state);

	return 0;
}

static void rkisp1_isp_stop(struct rkisp1_isp *isp)
{
	struct rkisp1_device *rkisp1 = isp->rkisp1;
	u32 val;

	/*
	 * ISP(mi) stop in mi frame end -> Stop ISP(mipi) ->
	 * Stop ISP(isp) ->wait for ISP isp off
	 */

	/* Mask MI and ISP interrupts */
	rkisp1_write(rkisp1, RKISP1_CIF_ISP_IMSC, 0);
	rkisp1_write(rkisp1, RKISP1_CIF_MI_IMSC, 0);

	/* Flush posted writes */
	rkisp1_read(rkisp1, RKISP1_CIF_MI_IMSC);

	/*
	 * Wait until the IRQ handler has ended. The IRQ handler may get called
	 * even after this, but it will return immediately as the MI and ISP
	 * interrupts have been masked.
	 */
	synchronize_irq(rkisp1->irqs[RKISP1_IRQ_ISP]);
	if (rkisp1->irqs[RKISP1_IRQ_ISP] != rkisp1->irqs[RKISP1_IRQ_MI])
		synchronize_irq(rkisp1->irqs[RKISP1_IRQ_MI]);

	/* Clear MI and ISP interrupt status */
	rkisp1_write(rkisp1, RKISP1_CIF_ISP_ICR, ~0);
	rkisp1_write(rkisp1, RKISP1_CIF_MI_ICR, ~0);

	/* stop ISP */
	val = rkisp1_read(rkisp1, RKISP1_CIF_ISP_CTRL);
	val &= ~(RKISP1_CIF_ISP_CTRL_ISP_INFORM_ENABLE |
		 RKISP1_CIF_ISP_CTRL_ISP_ENABLE);
	rkisp1_write(rkisp1, RKISP1_CIF_ISP_CTRL, val);

	val = rkisp1_read(rkisp1,	RKISP1_CIF_ISP_CTRL);
	rkisp1_write(rkisp1, RKISP1_CIF_ISP_CTRL,
		     val | RKISP1_CIF_ISP_CTRL_ISP_CFG_UPD);

	readx_poll_timeout(readl, rkisp1->base_addr + RKISP1_CIF_ISP_RIS,
			   val, val & RKISP1_CIF_ISP_OFF, 20, 100);
	rkisp1_write(rkisp1, RKISP1_CIF_VI_IRCL,
		     RKISP1_CIF_VI_IRCL_MIPI_SW_RST |
		     RKISP1_CIF_VI_IRCL_ISP_SW_RST);
	rkisp1_write(rkisp1, RKISP1_CIF_VI_IRCL, 0x0);
}

static void rkisp1_config_clk(struct rkisp1_isp *isp)
{
	struct rkisp1_device *rkisp1 = isp->rkisp1;

	u32 val = RKISP1_CIF_VI_ICCL_ISP_CLK | RKISP1_CIF_VI_ICCL_CP_CLK |
		  RKISP1_CIF_VI_ICCL_MRSZ_CLK | RKISP1_CIF_VI_ICCL_SRSZ_CLK |
		  RKISP1_CIF_VI_ICCL_JPEG_CLK | RKISP1_CIF_VI_ICCL_MI_CLK |
		  RKISP1_CIF_VI_ICCL_IE_CLK | RKISP1_CIF_VI_ICCL_MIPI_CLK |
		  RKISP1_CIF_VI_ICCL_DCROP_CLK;

	rkisp1_write(rkisp1, RKISP1_CIF_VI_ICCL, val);

	/* ensure sp and mp can run at the same time in V12 */
	if (rkisp1->info->isp_ver == RKISP1_V12) {
		val = RKISP1_CIF_CLK_CTRL_MI_Y12 | RKISP1_CIF_CLK_CTRL_MI_SP |
		      RKISP1_CIF_CLK_CTRL_MI_RAW0 | RKISP1_CIF_CLK_CTRL_MI_RAW1 |
		      RKISP1_CIF_CLK_CTRL_MI_READ | RKISP1_CIF_CLK_CTRL_MI_RAWRD |
		      RKISP1_CIF_CLK_CTRL_CP | RKISP1_CIF_CLK_CTRL_IE;
		rkisp1_write(rkisp1, RKISP1_CIF_VI_ISP_CLK_CTRL_V12, val);
	}
}

static void rkisp1_isp_start(struct rkisp1_isp *isp,
			     struct v4l2_subdev_state *sd_state)
{
	struct rkisp1_device *rkisp1 = isp->rkisp1;
	const struct v4l2_mbus_framefmt *src_fmt;
	const struct rkisp1_mbus_info *src_info;
	u32 val;

	rkisp1_config_clk(isp);

	/* Activate ISP */
	val = rkisp1_read(rkisp1, RKISP1_CIF_ISP_CTRL);
	val |= RKISP1_CIF_ISP_CTRL_ISP_CFG_UPD |
	       RKISP1_CIF_ISP_CTRL_ISP_ENABLE |
	       RKISP1_CIF_ISP_CTRL_ISP_INFORM_ENABLE;
	rkisp1_write(rkisp1, RKISP1_CIF_ISP_CTRL, val);

	src_fmt = v4l2_subdev_state_get_format(sd_state,
					       RKISP1_ISP_PAD_SOURCE_VIDEO);
	src_info = rkisp1_mbus_info_get_by_code(src_fmt->code);

	if (src_info->pixel_enc != V4L2_PIXEL_ENC_BAYER)
		rkisp1_params_post_configure(&rkisp1->params);
}

/* ----------------------------------------------------------------------------
 * Subdev pad operations
 */

static inline struct rkisp1_isp *to_rkisp1_isp(struct v4l2_subdev *sd)
{
	return container_of(sd, struct rkisp1_isp, sd);
}

static int rkisp1_isp_enum_mbus_code(struct v4l2_subdev *sd,
				     struct v4l2_subdev_state *sd_state,
				     struct v4l2_subdev_mbus_code_enum *code)
{
	unsigned int i, dir;
	int pos = 0;

	if (code->pad == RKISP1_ISP_PAD_SINK_VIDEO) {
		dir = RKISP1_ISP_SD_SINK;
	} else if (code->pad == RKISP1_ISP_PAD_SOURCE_VIDEO) {
		dir = RKISP1_ISP_SD_SRC;
	} else {
		if (code->index > 0)
			return -EINVAL;
		code->code = MEDIA_BUS_FMT_METADATA_FIXED;
		return 0;
	}

	for (i = 0; ; i++) {
		const struct rkisp1_mbus_info *fmt =
			rkisp1_mbus_info_get_by_index(i);

		if (!fmt)
			return -EINVAL;

		if (fmt->direction & dir)
			pos++;

		if (code->index == pos - 1) {
			code->code = fmt->mbus_code;
			if (fmt->pixel_enc == V4L2_PIXEL_ENC_YUV &&
			    dir == RKISP1_ISP_SD_SRC)
				code->flags =
					V4L2_SUBDEV_MBUS_CODE_CSC_QUANTIZATION;
			return 0;
		}
	}

	return -EINVAL;
}

static int rkisp1_isp_enum_frame_size(struct v4l2_subdev *sd,
				      struct v4l2_subdev_state *sd_state,
				      struct v4l2_subdev_frame_size_enum *fse)
{
	const struct rkisp1_mbus_info *mbus_info;

	if (fse->pad == RKISP1_ISP_PAD_SINK_PARAMS ||
	    fse->pad == RKISP1_ISP_PAD_SOURCE_STATS)
		return -ENOTTY;

	if (fse->index > 0)
		return -EINVAL;

	mbus_info = rkisp1_mbus_info_get_by_code(fse->code);
	if (!mbus_info)
		return -EINVAL;

	if (!(mbus_info->direction & RKISP1_ISP_SD_SINK) &&
	    fse->pad == RKISP1_ISP_PAD_SINK_VIDEO)
		return -EINVAL;

	if (!(mbus_info->direction & RKISP1_ISP_SD_SRC) &&
	    fse->pad == RKISP1_ISP_PAD_SOURCE_VIDEO)
		return -EINVAL;

	fse->min_width = RKISP1_ISP_MIN_WIDTH;
	fse->max_width = RKISP1_ISP_MAX_WIDTH;
	fse->min_height = RKISP1_ISP_MIN_HEIGHT;
	fse->max_height = RKISP1_ISP_MAX_HEIGHT;

	return 0;
}

static int rkisp1_isp_init_state(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state)
{
	struct v4l2_mbus_framefmt *sink_fmt, *src_fmt;
	struct v4l2_rect *sink_crop, *src_crop;

	/* Video. */
	sink_fmt = v4l2_subdev_state_get_format(sd_state,
						RKISP1_ISP_PAD_SINK_VIDEO);
	sink_fmt->width = RKISP1_DEFAULT_WIDTH;
	sink_fmt->height = RKISP1_DEFAULT_HEIGHT;
	sink_fmt->field = V4L2_FIELD_NONE;
	sink_fmt->code = RKISP1_DEF_SINK_PAD_FMT;
	sink_fmt->colorspace = V4L2_COLORSPACE_RAW;
	sink_fmt->xfer_func = V4L2_XFER_FUNC_NONE;
	sink_fmt->ycbcr_enc = V4L2_YCBCR_ENC_601;
	sink_fmt->quantization = V4L2_QUANTIZATION_FULL_RANGE;

	sink_crop = v4l2_subdev_state_get_crop(sd_state,
					       RKISP1_ISP_PAD_SINK_VIDEO);
	sink_crop->width = RKISP1_DEFAULT_WIDTH;
	sink_crop->height = RKISP1_DEFAULT_HEIGHT;
	sink_crop->left = 0;
	sink_crop->top = 0;

	src_fmt = v4l2_subdev_state_get_format(sd_state,
					       RKISP1_ISP_PAD_SOURCE_VIDEO);
	*src_fmt = *sink_fmt;
	src_fmt->code = RKISP1_DEF_SRC_PAD_FMT;
	src_fmt->colorspace = V4L2_COLORSPACE_SRGB;
	src_fmt->xfer_func = V4L2_XFER_FUNC_SRGB;
	src_fmt->ycbcr_enc = V4L2_YCBCR_ENC_601;
	src_fmt->quantization = V4L2_QUANTIZATION_LIM_RANGE;

	src_crop = v4l2_subdev_state_get_crop(sd_state,
					      RKISP1_ISP_PAD_SOURCE_VIDEO);
	*src_crop = *sink_crop;

	/* Parameters and statistics. */
	sink_fmt = v4l2_subdev_state_get_format(sd_state,
						RKISP1_ISP_PAD_SINK_PARAMS);
	src_fmt = v4l2_subdev_state_get_format(sd_state,
					       RKISP1_ISP_PAD_SOURCE_STATS);
	sink_fmt->width = 0;
	sink_fmt->height = 0;
	sink_fmt->field = V4L2_FIELD_NONE;
	sink_fmt->code = MEDIA_BUS_FMT_METADATA_FIXED;
	*src_fmt = *sink_fmt;

	return 0;
}

static void rkisp1_isp_set_src_fmt(struct rkisp1_isp *isp,
				   struct v4l2_subdev_state *sd_state,
				   struct v4l2_mbus_framefmt *format)
{
	const struct rkisp1_mbus_info *sink_info;
	const struct rkisp1_mbus_info *src_info;
	struct v4l2_mbus_framefmt *sink_fmt;
	struct v4l2_mbus_framefmt *src_fmt;
	const struct v4l2_rect *src_crop;
	bool set_csc;

	sink_fmt = v4l2_subdev_state_get_format(sd_state,
						RKISP1_ISP_PAD_SINK_VIDEO);
	src_fmt = v4l2_subdev_state_get_format(sd_state,
					       RKISP1_ISP_PAD_SOURCE_VIDEO);
	src_crop = v4l2_subdev_state_get_crop(sd_state,
					      RKISP1_ISP_PAD_SOURCE_VIDEO);

	/*
	 * Media bus code. The ISP can operate in pass-through mode (Bayer in,
	 * Bayer out or YUV in, YUV out) or process Bayer data to YUV, but
	 * can't convert from YUV to Bayer.
	 */
	sink_info = rkisp1_mbus_info_get_by_code(sink_fmt->code);

	src_fmt->code = format->code;
	src_info = rkisp1_mbus_info_get_by_code(src_fmt->code);
	if (!src_info || !(src_info->direction & RKISP1_ISP_SD_SRC)) {
		src_fmt->code = RKISP1_DEF_SRC_PAD_FMT;
		src_info = rkisp1_mbus_info_get_by_code(src_fmt->code);
	}

	if (sink_info->pixel_enc == V4L2_PIXEL_ENC_YUV &&
	    src_info->pixel_enc == V4L2_PIXEL_ENC_BAYER) {
		src_fmt->code = sink_fmt->code;
		src_info = sink_info;
	}

	/*
	 * The source width and height must be identical to the source crop
	 * size.
	 */
	src_fmt->width  = src_crop->width;
	src_fmt->height = src_crop->height;

	/*
	 * Copy the color space for the sink pad. When converting from Bayer to
	 * YUV, default to a limited quantization range.
	 */
	src_fmt->colorspace = sink_fmt->colorspace;
	src_fmt->xfer_func = sink_fmt->xfer_func;
	src_fmt->ycbcr_enc = sink_fmt->ycbcr_enc;

	if (sink_info->pixel_enc == V4L2_PIXEL_ENC_BAYER &&
	    src_info->pixel_enc == V4L2_PIXEL_ENC_YUV)
		src_fmt->quantization = V4L2_QUANTIZATION_LIM_RANGE;
	else
		src_fmt->quantization = sink_fmt->quantization;

	/*
	 * Allow setting the source color space fields when the SET_CSC flag is
	 * set and the source format is YUV. If the sink format is YUV, don't
	 * set the color primaries, transfer function or YCbCr encoding as the
	 * ISP is bypassed in that case and passes YUV data through without
	 * modifications.
	 *
	 * The color primaries and transfer function are configured through the
	 * cross-talk matrix and tone curve respectively. Settings for those
	 * hardware blocks are conveyed through the ISP parameters buffer, as
	 * they need to combine color space information with other image tuning
	 * characteristics and can't thus be computed by the kernel based on the
	 * color space. The source pad colorspace and xfer_func fields are thus
	 * ignored by the driver, but can be set by userspace to propagate
	 * accurate color space information down the pipeline.
	 */
	set_csc = format->flags & V4L2_MBUS_FRAMEFMT_SET_CSC;

	if (set_csc && src_info->pixel_enc == V4L2_PIXEL_ENC_YUV) {
		if (sink_info->pixel_enc == V4L2_PIXEL_ENC_BAYER) {
			if (format->colorspace != V4L2_COLORSPACE_DEFAULT)
				src_fmt->colorspace = format->colorspace;
			if (format->xfer_func != V4L2_XFER_FUNC_DEFAULT)
				src_fmt->xfer_func = format->xfer_func;
			if (format->ycbcr_enc != V4L2_YCBCR_ENC_DEFAULT)
				src_fmt->ycbcr_enc = format->ycbcr_enc;
		}

		if (format->quantization != V4L2_QUANTIZATION_DEFAULT)
			src_fmt->quantization = format->quantization;
	}

	*format = *src_fmt;

	/*
	 * Restore the SET_CSC flag if it was set to indicate support for the
	 * CSC setting API.
	 */
	if (set_csc)
		format->flags |= V4L2_MBUS_FRAMEFMT_SET_CSC;
}

static void rkisp1_isp_set_src_crop(struct rkisp1_isp *isp,
				    struct v4l2_subdev_state *sd_state,
				    struct v4l2_rect *r)
{
	struct v4l2_mbus_framefmt *src_fmt;
	const struct v4l2_rect *sink_crop;
	struct v4l2_rect *src_crop;

	src_crop = v4l2_subdev_state_get_crop(sd_state,
					      RKISP1_ISP_PAD_SOURCE_VIDEO);
	sink_crop = v4l2_subdev_state_get_crop(sd_state,
					       RKISP1_ISP_PAD_SINK_VIDEO);

	src_crop->left = ALIGN(r->left, 2);
	src_crop->width = ALIGN(r->width, 2);
	src_crop->top = r->top;
	src_crop->height = r->height;
	rkisp1_sd_adjust_crop_rect(src_crop, sink_crop);

	*r = *src_crop;

	/* Propagate to out format */
	src_fmt = v4l2_subdev_state_get_format(sd_state,
					       RKISP1_ISP_PAD_SOURCE_VIDEO);
	rkisp1_isp_set_src_fmt(isp, sd_state, src_fmt);
}

static void rkisp1_isp_set_sink_crop(struct rkisp1_isp *isp,
				     struct v4l2_subdev_state *sd_state,
				     struct v4l2_rect *r)
{
	struct v4l2_rect *sink_crop, *src_crop;
	const struct v4l2_mbus_framefmt *sink_fmt;

	sink_crop = v4l2_subdev_state_get_crop(sd_state,
					       RKISP1_ISP_PAD_SINK_VIDEO);
	sink_fmt = v4l2_subdev_state_get_format(sd_state,
						RKISP1_ISP_PAD_SINK_VIDEO);

	sink_crop->left = ALIGN(r->left, 2);
	sink_crop->width = ALIGN(r->width, 2);
	sink_crop->top = r->top;
	sink_crop->height = r->height;
	rkisp1_sd_adjust_crop(sink_crop, sink_fmt);

	*r = *sink_crop;

	/* Propagate to out crop */
	src_crop = v4l2_subdev_state_get_crop(sd_state,
					      RKISP1_ISP_PAD_SOURCE_VIDEO);
	rkisp1_isp_set_src_crop(isp, sd_state, src_crop);
}

static void rkisp1_isp_set_sink_fmt(struct rkisp1_isp *isp,
				    struct v4l2_subdev_state *sd_state,
				    struct v4l2_mbus_framefmt *format)
{
	const struct rkisp1_mbus_info *mbus_info;
	struct v4l2_mbus_framefmt *sink_fmt;
	struct v4l2_rect *sink_crop;
	bool is_yuv;

	sink_fmt = v4l2_subdev_state_get_format(sd_state,
						RKISP1_ISP_PAD_SINK_VIDEO);
	sink_fmt->code = format->code;
	mbus_info = rkisp1_mbus_info_get_by_code(sink_fmt->code);
	if (!mbus_info || !(mbus_info->direction & RKISP1_ISP_SD_SINK)) {
		sink_fmt->code = RKISP1_DEF_SINK_PAD_FMT;
		mbus_info = rkisp1_mbus_info_get_by_code(sink_fmt->code);
	}

	sink_fmt->width = clamp_t(u32, format->width,
				  RKISP1_ISP_MIN_WIDTH,
				  RKISP1_ISP_MAX_WIDTH);
	sink_fmt->height = clamp_t(u32, format->height,
				   RKISP1_ISP_MIN_HEIGHT,
				   RKISP1_ISP_MAX_HEIGHT);

	/*
	 * Adjust the color space fields. Accept any color primaries and
	 * transfer function for both YUV and Bayer. For YUV any YCbCr encoding
	 * and quantization range is also accepted. For Bayer formats, the YCbCr
	 * encoding isn't applicable, and the quantization range can only be
	 * full.
	 */
	is_yuv = mbus_info->pixel_enc == V4L2_PIXEL_ENC_YUV;

	sink_fmt->colorspace = format->colorspace ? :
			       (is_yuv ? V4L2_COLORSPACE_SRGB :
				V4L2_COLORSPACE_RAW);
	sink_fmt->xfer_func = format->xfer_func ? :
			      V4L2_MAP_XFER_FUNC_DEFAULT(sink_fmt->colorspace);
	if (is_yuv) {
		sink_fmt->ycbcr_enc = format->ycbcr_enc ? :
			V4L2_MAP_YCBCR_ENC_DEFAULT(sink_fmt->colorspace);
		sink_fmt->quantization = format->quantization ? :
			V4L2_MAP_QUANTIZATION_DEFAULT(false, sink_fmt->colorspace,
						      sink_fmt->ycbcr_enc);
	} else {
		/*
		 * The YCbCr encoding isn't applicable for non-YUV formats, but
		 * V4L2 has no "no encoding" value. Hardcode it to Rec. 601, it
		 * should be ignored by userspace.
		 */
		sink_fmt->ycbcr_enc = V4L2_YCBCR_ENC_601;
		sink_fmt->quantization = V4L2_QUANTIZATION_FULL_RANGE;
	}

	*format = *sink_fmt;

	/* Propagate to in crop */
	sink_crop = v4l2_subdev_state_get_crop(sd_state,
					       RKISP1_ISP_PAD_SINK_VIDEO);
	rkisp1_isp_set_sink_crop(isp, sd_state, sink_crop);
}

static int rkisp1_isp_set_fmt(struct v4l2_subdev *sd,
			      struct v4l2_subdev_state *sd_state,
			      struct v4l2_subdev_format *fmt)
{
	struct rkisp1_isp *isp = to_rkisp1_isp(sd);

	if (fmt->pad == RKISP1_ISP_PAD_SINK_VIDEO)
		rkisp1_isp_set_sink_fmt(isp, sd_state, &fmt->format);
	else if (fmt->pad == RKISP1_ISP_PAD_SOURCE_VIDEO)
		rkisp1_isp_set_src_fmt(isp, sd_state, &fmt->format);
	else
		fmt->format = *v4l2_subdev_state_get_format(sd_state,
							    fmt->pad);

	return 0;
}

static int rkisp1_isp_get_selection(struct v4l2_subdev *sd,
				    struct v4l2_subdev_state *sd_state,
				    struct v4l2_subdev_selection *sel)
{
	int ret = 0;

	if (sel->pad != RKISP1_ISP_PAD_SOURCE_VIDEO &&
	    sel->pad != RKISP1_ISP_PAD_SINK_VIDEO)
		return -EINVAL;

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP_BOUNDS:
		if (sel->pad == RKISP1_ISP_PAD_SINK_VIDEO) {
			struct v4l2_mbus_framefmt *fmt;

			fmt = v4l2_subdev_state_get_format(sd_state, sel->pad);
			sel->r.height = fmt->height;
			sel->r.width = fmt->width;
			sel->r.left = 0;
			sel->r.top = 0;
		} else {
			sel->r = *v4l2_subdev_state_get_crop(sd_state,
							     RKISP1_ISP_PAD_SINK_VIDEO);
		}
		break;

	case V4L2_SEL_TGT_CROP:
		sel->r = *v4l2_subdev_state_get_crop(sd_state, sel->pad);
		break;

	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int rkisp1_isp_set_selection(struct v4l2_subdev *sd,
				    struct v4l2_subdev_state *sd_state,
				    struct v4l2_subdev_selection *sel)
{
	struct rkisp1_isp *isp = to_rkisp1_isp(sd);
	int ret = 0;

	if (sel->target != V4L2_SEL_TGT_CROP)
		return -EINVAL;

	dev_dbg(isp->rkisp1->dev, "%s: pad: %d sel(%d,%d)/%dx%d\n", __func__,
		sel->pad, sel->r.left, sel->r.top, sel->r.width, sel->r.height);

	if (sel->pad == RKISP1_ISP_PAD_SINK_VIDEO)
		rkisp1_isp_set_sink_crop(isp, sd_state, &sel->r);
	else if (sel->pad == RKISP1_ISP_PAD_SOURCE_VIDEO)
		rkisp1_isp_set_src_crop(isp, sd_state, &sel->r);
	else
		ret = -EINVAL;

	return ret;
}

static int rkisp1_subdev_link_validate(struct media_link *link)
{
	if (link->sink->index == RKISP1_ISP_PAD_SINK_PARAMS)
		return 0;

	return v4l2_subdev_link_validate(link);
}

static const struct v4l2_subdev_pad_ops rkisp1_isp_pad_ops = {
	.enum_mbus_code = rkisp1_isp_enum_mbus_code,
	.enum_frame_size = rkisp1_isp_enum_frame_size,
	.get_selection = rkisp1_isp_get_selection,
	.set_selection = rkisp1_isp_set_selection,
	.get_fmt = v4l2_subdev_get_fmt,
	.set_fmt = rkisp1_isp_set_fmt,
	.link_validate = v4l2_subdev_link_validate_default,
};

/* ----------------------------------------------------------------------------
 * Stream operations
 */

static int rkisp1_isp_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct rkisp1_isp *isp = to_rkisp1_isp(sd);
	struct rkisp1_device *rkisp1 = isp->rkisp1;
	struct v4l2_subdev_state *sd_state;
	struct media_pad *source_pad;
	struct media_pad *sink_pad;
	enum v4l2_mbus_type mbus_type;
	u32 mbus_flags;
	int ret;

	if (!enable) {
		v4l2_subdev_call(rkisp1->source, video, s_stream, false);
		rkisp1_isp_stop(isp);
		return 0;
	}

	sink_pad = &isp->pads[RKISP1_ISP_PAD_SINK_VIDEO];
	source_pad = media_pad_remote_pad_unique(sink_pad);
	if (IS_ERR(source_pad)) {
		dev_dbg(rkisp1->dev, "Failed to get source for ISP: %ld\n",
			PTR_ERR(source_pad));
		return -EPIPE;
	}

	rkisp1->source = media_entity_to_v4l2_subdev(source_pad->entity);
	if (!rkisp1->source) {
		/* This should really not happen, so is not worth a message. */
		return -EPIPE;
	}

	if (rkisp1->source == &rkisp1->csi.sd) {
		mbus_type = V4L2_MBUS_CSI2_DPHY;
		mbus_flags = 0;
	} else {
		const struct rkisp1_sensor_async *asd;
		struct v4l2_async_connection *asc;

		asc = v4l2_async_connection_unique(rkisp1->source);
		if (!asc)
			return -EPIPE;

		asd = container_of(asc, struct rkisp1_sensor_async, asd);

		mbus_type = asd->mbus_type;
		mbus_flags = asd->mbus_flags;
	}

	isp->frame_sequence = -1;

	sd_state = v4l2_subdev_lock_and_get_active_state(sd);

	ret = rkisp1_config_cif(isp, sd_state, mbus_type, mbus_flags);
	if (ret)
		goto out_unlock;

	rkisp1_isp_start(isp, sd_state);

	ret = v4l2_subdev_call(rkisp1->source, video, s_stream, true);
	if (ret) {
		rkisp1_isp_stop(isp);
		goto out_unlock;
	}

out_unlock:
	v4l2_subdev_unlock_state(sd_state);
	return ret;
}

static int rkisp1_isp_subs_evt(struct v4l2_subdev *sd, struct v4l2_fh *fh,
			       struct v4l2_event_subscription *sub)
{
	if (sub->type != V4L2_EVENT_FRAME_SYNC)
		return -EINVAL;

	/* V4L2_EVENT_FRAME_SYNC doesn't require an id, so zero should be set */
	if (sub->id != 0)
		return -EINVAL;

	return v4l2_event_subscribe(fh, sub, 0, NULL);
}

static const struct media_entity_operations rkisp1_isp_media_ops = {
	.link_validate = rkisp1_subdev_link_validate,
};

static const struct v4l2_subdev_video_ops rkisp1_isp_video_ops = {
	.s_stream = rkisp1_isp_s_stream,
};

static const struct v4l2_subdev_core_ops rkisp1_isp_core_ops = {
	.subscribe_event = rkisp1_isp_subs_evt,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_ops rkisp1_isp_ops = {
	.core = &rkisp1_isp_core_ops,
	.video = &rkisp1_isp_video_ops,
	.pad = &rkisp1_isp_pad_ops,
};

static const struct v4l2_subdev_internal_ops rkisp1_isp_internal_ops = {
	.init_state = rkisp1_isp_init_state,
};

int rkisp1_isp_register(struct rkisp1_device *rkisp1)
{
	struct rkisp1_isp *isp = &rkisp1->isp;
	struct media_pad *pads = isp->pads;
	struct v4l2_subdev *sd = &isp->sd;
	int ret;

	isp->rkisp1 = rkisp1;

	v4l2_subdev_init(sd, &rkisp1_isp_ops);
	sd->internal_ops = &rkisp1_isp_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS;
	sd->entity.ops = &rkisp1_isp_media_ops;
	sd->entity.function = MEDIA_ENT_F_PROC_VIDEO_PIXEL_FORMATTER;
	sd->owner = THIS_MODULE;
	strscpy(sd->name, RKISP1_ISP_DEV_NAME, sizeof(sd->name));

	pads[RKISP1_ISP_PAD_SINK_VIDEO].flags = MEDIA_PAD_FL_SINK |
						MEDIA_PAD_FL_MUST_CONNECT;
	pads[RKISP1_ISP_PAD_SINK_PARAMS].flags = MEDIA_PAD_FL_SINK;
	pads[RKISP1_ISP_PAD_SOURCE_VIDEO].flags = MEDIA_PAD_FL_SOURCE;
	pads[RKISP1_ISP_PAD_SOURCE_STATS].flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&sd->entity, RKISP1_ISP_PAD_MAX, pads);
	if (ret)
		goto err_entity_cleanup;

	ret = v4l2_subdev_init_finalize(sd);
	if (ret)
		goto err_subdev_cleanup;

	ret = v4l2_device_register_subdev(&rkisp1->v4l2_dev, sd);
	if (ret) {
		dev_err(rkisp1->dev, "Failed to register isp subdev\n");
		goto err_subdev_cleanup;
	}

	return 0;

err_subdev_cleanup:
	v4l2_subdev_cleanup(sd);
err_entity_cleanup:
	media_entity_cleanup(&sd->entity);
	isp->sd.v4l2_dev = NULL;
	return ret;
}

void rkisp1_isp_unregister(struct rkisp1_device *rkisp1)
{
	struct rkisp1_isp *isp = &rkisp1->isp;

	if (!isp->sd.v4l2_dev)
		return;

	v4l2_device_unregister_subdev(&isp->sd);
	v4l2_subdev_cleanup(&isp->sd);
	media_entity_cleanup(&isp->sd.entity);
}

/* ----------------------------------------------------------------------------
 * Interrupt handlers
 */

static void rkisp1_isp_queue_event_sof(struct rkisp1_isp *isp)
{
	struct v4l2_event event = {
		.type = V4L2_EVENT_FRAME_SYNC,
	};

	event.u.frame_sync.frame_sequence = isp->frame_sequence;
	v4l2_event_queue(isp->sd.devnode, &event);
}

irqreturn_t rkisp1_isp_isr(int irq, void *ctx)
{
	struct device *dev = ctx;
	struct rkisp1_device *rkisp1 = dev_get_drvdata(dev);
	u32 status, isp_err;

	status = rkisp1_read(rkisp1, RKISP1_CIF_ISP_MIS);
	if (!status)
		return IRQ_NONE;

	rkisp1_write(rkisp1, RKISP1_CIF_ISP_ICR, status);

	/* Vertical sync signal, starting generating new frame */
	if (status & RKISP1_CIF_ISP_V_START) {
		rkisp1->isp.frame_sequence++;
		rkisp1_isp_queue_event_sof(&rkisp1->isp);
		if (status & RKISP1_CIF_ISP_FRAME) {
			WARN_ONCE(1, "irq delay is too long, buffers might not be in sync\n");
			rkisp1->debug.irq_delay++;
		}
	}
	if (status & RKISP1_CIF_ISP_PIC_SIZE_ERROR) {
		/* Clear pic_size_error */
		isp_err = rkisp1_read(rkisp1, RKISP1_CIF_ISP_ERR);
		if (isp_err & RKISP1_CIF_ISP_ERR_INFORM_SIZE)
			rkisp1->debug.inform_size_error++;
		if (isp_err & RKISP1_CIF_ISP_ERR_IS_SIZE)
			rkisp1->debug.img_stabilization_size_error++;
		if (isp_err & RKISP1_CIF_ISP_ERR_OUTFORM_SIZE)
			rkisp1->debug.outform_size_error++;
		rkisp1_write(rkisp1, RKISP1_CIF_ISP_ERR_CLR, isp_err);
	} else if (status & RKISP1_CIF_ISP_DATA_LOSS) {
		/* keep track of data_loss in debugfs */
		rkisp1->debug.data_loss++;
	}

	if (status & RKISP1_CIF_ISP_FRAME) {
		u32 isp_ris;

		rkisp1->debug.complete_frames++;

		/* New frame from the sensor received */
		isp_ris = rkisp1_read(rkisp1, RKISP1_CIF_ISP_RIS);
		if (isp_ris & RKISP1_STATS_MEAS_MASK)
			rkisp1_stats_isr(&rkisp1->stats, isp_ris);
		/*
		 * Then update changed configs. Some of them involve
		 * lot of register writes. Do those only one per frame.
		 * Do the updates in the order of the processing flow.
		 */
		rkisp1_params_isr(rkisp1);
	}

	return IRQ_HANDLED;
}
