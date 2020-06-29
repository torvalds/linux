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
#include <linux/phy/phy.h>
#include <linux/phy/phy-mipi-dphy.h>
#include <linux/pm_runtime.h>
#include <linux/videodev2.h>
#include <linux/vmalloc.h>
#include <media/v4l2-event.h>

#include "rkisp1-common.h"

#define RKISP1_DEF_SINK_PAD_FMT MEDIA_BUS_FMT_SRGGB10_1X10
#define RKISP1_DEF_SRC_PAD_FMT MEDIA_BUS_FMT_YUYV8_2X8

#define RKISP1_ISP_DEV_NAME	RKISP1_DRIVER_NAME "_isp"

#define RKISP1_DIR_SRC BIT(0)
#define RKISP1_DIR_SINK BIT(1)
#define RKISP1_DIR_SINK_SRC (RKISP1_DIR_SINK | RKISP1_DIR_SRC)

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

static const struct rkisp1_isp_mbus_info rkisp1_isp_formats[] = {
	{
		.mbus_code	= MEDIA_BUS_FMT_YUYV8_2X8,
		.pixel_enc	= V4L2_PIXEL_ENC_YUV,
		.direction	= RKISP1_DIR_SRC,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SRGGB10_1X10,
		.pixel_enc	= V4L2_PIXEL_ENC_BAYER,
		.mipi_dt	= RKISP1_CIF_CSI2_DT_RAW10,
		.bayer_pat	= RKISP1_RAW_RGGB,
		.bus_width	= 10,
		.direction	= RKISP1_DIR_SINK_SRC,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SBGGR10_1X10,
		.pixel_enc	= V4L2_PIXEL_ENC_BAYER,
		.mipi_dt	= RKISP1_CIF_CSI2_DT_RAW10,
		.bayer_pat	= RKISP1_RAW_BGGR,
		.bus_width	= 10,
		.direction	= RKISP1_DIR_SINK_SRC,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SGBRG10_1X10,
		.pixel_enc	= V4L2_PIXEL_ENC_BAYER,
		.mipi_dt	= RKISP1_CIF_CSI2_DT_RAW10,
		.bayer_pat	= RKISP1_RAW_GBRG,
		.bus_width	= 10,
		.direction	= RKISP1_DIR_SINK_SRC,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SGRBG10_1X10,
		.pixel_enc	= V4L2_PIXEL_ENC_BAYER,
		.mipi_dt	= RKISP1_CIF_CSI2_DT_RAW10,
		.bayer_pat	= RKISP1_RAW_GRBG,
		.bus_width	= 10,
		.direction	= RKISP1_DIR_SINK_SRC,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SRGGB12_1X12,
		.pixel_enc	= V4L2_PIXEL_ENC_BAYER,
		.mipi_dt	= RKISP1_CIF_CSI2_DT_RAW12,
		.bayer_pat	= RKISP1_RAW_RGGB,
		.bus_width	= 12,
		.direction	= RKISP1_DIR_SINK_SRC,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SBGGR12_1X12,
		.pixel_enc	= V4L2_PIXEL_ENC_BAYER,
		.mipi_dt	= RKISP1_CIF_CSI2_DT_RAW12,
		.bayer_pat	= RKISP1_RAW_BGGR,
		.bus_width	= 12,
		.direction	= RKISP1_DIR_SINK_SRC,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SGBRG12_1X12,
		.pixel_enc	= V4L2_PIXEL_ENC_BAYER,
		.mipi_dt	= RKISP1_CIF_CSI2_DT_RAW12,
		.bayer_pat	= RKISP1_RAW_GBRG,
		.bus_width	= 12,
		.direction	= RKISP1_DIR_SINK_SRC,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SGRBG12_1X12,
		.pixel_enc	= V4L2_PIXEL_ENC_BAYER,
		.mipi_dt	= RKISP1_CIF_CSI2_DT_RAW12,
		.bayer_pat	= RKISP1_RAW_GRBG,
		.bus_width	= 12,
		.direction	= RKISP1_DIR_SINK_SRC,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SRGGB8_1X8,
		.pixel_enc	= V4L2_PIXEL_ENC_BAYER,
		.mipi_dt	= RKISP1_CIF_CSI2_DT_RAW8,
		.bayer_pat	= RKISP1_RAW_RGGB,
		.bus_width	= 8,
		.direction	= RKISP1_DIR_SINK_SRC,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SBGGR8_1X8,
		.pixel_enc	= V4L2_PIXEL_ENC_BAYER,
		.mipi_dt	= RKISP1_CIF_CSI2_DT_RAW8,
		.bayer_pat	= RKISP1_RAW_BGGR,
		.bus_width	= 8,
		.direction	= RKISP1_DIR_SINK_SRC,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SGBRG8_1X8,
		.pixel_enc	= V4L2_PIXEL_ENC_BAYER,
		.mipi_dt	= RKISP1_CIF_CSI2_DT_RAW8,
		.bayer_pat	= RKISP1_RAW_GBRG,
		.bus_width	= 8,
		.direction	= RKISP1_DIR_SINK_SRC,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SGRBG8_1X8,
		.pixel_enc	= V4L2_PIXEL_ENC_BAYER,
		.mipi_dt	= RKISP1_CIF_CSI2_DT_RAW8,
		.bayer_pat	= RKISP1_RAW_GRBG,
		.bus_width	= 8,
		.direction	= RKISP1_DIR_SINK_SRC,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_YUYV8_1X16,
		.pixel_enc	= V4L2_PIXEL_ENC_YUV,
		.mipi_dt	= RKISP1_CIF_CSI2_DT_YUV422_8b,
		.yuv_seq	= RKISP1_CIF_ISP_ACQ_PROP_YCBYCR,
		.bus_width	= 16,
		.direction	= RKISP1_DIR_SINK,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_YVYU8_1X16,
		.pixel_enc	= V4L2_PIXEL_ENC_YUV,
		.mipi_dt	= RKISP1_CIF_CSI2_DT_YUV422_8b,
		.yuv_seq	= RKISP1_CIF_ISP_ACQ_PROP_YCRYCB,
		.bus_width	= 16,
		.direction	= RKISP1_DIR_SINK,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_UYVY8_1X16,
		.pixel_enc	= V4L2_PIXEL_ENC_YUV,
		.mipi_dt	= RKISP1_CIF_CSI2_DT_YUV422_8b,
		.yuv_seq	= RKISP1_CIF_ISP_ACQ_PROP_CBYCRY,
		.bus_width	= 16,
		.direction	= RKISP1_DIR_SINK,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_VYUY8_1X16,
		.pixel_enc	= V4L2_PIXEL_ENC_YUV,
		.mipi_dt	= RKISP1_CIF_CSI2_DT_YUV422_8b,
		.yuv_seq	= RKISP1_CIF_ISP_ACQ_PROP_CRYCBY,
		.bus_width	= 16,
		.direction	= RKISP1_DIR_SINK,
	},
};

/* ----------------------------------------------------------------------------
 * Helpers
 */

const struct rkisp1_isp_mbus_info *rkisp1_isp_mbus_info_get(u32 mbus_code)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(rkisp1_isp_formats); i++) {
		const struct rkisp1_isp_mbus_info *fmt = &rkisp1_isp_formats[i];

		if (fmt->mbus_code == mbus_code)
			return fmt;
	}

	return NULL;
}

static struct v4l2_subdev *rkisp1_get_remote_sensor(struct v4l2_subdev *sd)
{
	struct media_pad *local, *remote;
	struct media_entity *sensor_me;

	local = &sd->entity.pads[RKISP1_ISP_PAD_SINK_VIDEO];
	remote = media_entity_remote_pad(local);
	if (!remote) {
		dev_warn(sd->dev, "No link between isp and sensor\n");
		return NULL;
	}

	sensor_me = remote->entity;
	return media_entity_to_v4l2_subdev(sensor_me);
}

static struct v4l2_mbus_framefmt *
rkisp1_isp_get_pad_fmt(struct rkisp1_isp *isp,
		       struct v4l2_subdev_pad_config *cfg,
		       unsigned int pad, u32 which)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return v4l2_subdev_get_try_format(&isp->sd, cfg, pad);
	else
		return v4l2_subdev_get_try_format(&isp->sd, isp->pad_cfg, pad);
}

static struct v4l2_rect *
rkisp1_isp_get_pad_crop(struct rkisp1_isp *isp,
			struct v4l2_subdev_pad_config *cfg,
			unsigned int pad, u32 which)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return v4l2_subdev_get_try_crop(&isp->sd, cfg, pad);
	else
		return v4l2_subdev_get_try_crop(&isp->sd, isp->pad_cfg, pad);
}

/* ----------------------------------------------------------------------------
 * Camera Interface registers configurations
 */

/*
 * Image Stabilization.
 * This should only be called when configuring CIF
 * or at the frame end interrupt
 */
static void rkisp1_config_ism(struct rkisp1_device *rkisp1)
{
	struct v4l2_rect *src_crop =
		rkisp1_isp_get_pad_crop(&rkisp1->isp, NULL,
					RKISP1_ISP_PAD_SOURCE_VIDEO,
					V4L2_SUBDEV_FORMAT_ACTIVE);
	u32 val;

	rkisp1_write(rkisp1, 0, RKISP1_CIF_ISP_IS_RECENTER);
	rkisp1_write(rkisp1, 0, RKISP1_CIF_ISP_IS_MAX_DX);
	rkisp1_write(rkisp1, 0, RKISP1_CIF_ISP_IS_MAX_DY);
	rkisp1_write(rkisp1, 0, RKISP1_CIF_ISP_IS_DISPLACE);
	rkisp1_write(rkisp1, src_crop->left, RKISP1_CIF_ISP_IS_H_OFFS);
	rkisp1_write(rkisp1, src_crop->top, RKISP1_CIF_ISP_IS_V_OFFS);
	rkisp1_write(rkisp1, src_crop->width, RKISP1_CIF_ISP_IS_H_SIZE);
	rkisp1_write(rkisp1, src_crop->height, RKISP1_CIF_ISP_IS_V_SIZE);

	/* IS(Image Stabilization) is always on, working as output crop */
	rkisp1_write(rkisp1, 1, RKISP1_CIF_ISP_IS_CTRL);
	val = rkisp1_read(rkisp1, RKISP1_CIF_ISP_CTRL);
	val |= RKISP1_CIF_ISP_CTRL_ISP_CFG_UPD;
	rkisp1_write(rkisp1, val, RKISP1_CIF_ISP_CTRL);
}

/*
 * configure ISP blocks with input format, size......
 */
static int rkisp1_config_isp(struct rkisp1_device *rkisp1)
{
	u32 isp_ctrl = 0, irq_mask = 0, acq_mult = 0, signal = 0;
	const struct rkisp1_isp_mbus_info *src_fmt, *sink_fmt;
	struct rkisp1_sensor_async *sensor;
	struct v4l2_mbus_framefmt *sink_frm;
	struct v4l2_rect *sink_crop;

	sensor = rkisp1->active_sensor;
	sink_fmt = rkisp1->isp.sink_fmt;
	src_fmt = rkisp1->isp.src_fmt;
	sink_frm = rkisp1_isp_get_pad_fmt(&rkisp1->isp, NULL,
					  RKISP1_ISP_PAD_SINK_VIDEO,
					  V4L2_SUBDEV_FORMAT_ACTIVE);
	sink_crop = rkisp1_isp_get_pad_crop(&rkisp1->isp, NULL,
					    RKISP1_ISP_PAD_SINK_VIDEO,
					    V4L2_SUBDEV_FORMAT_ACTIVE);

	if (sink_fmt->pixel_enc == V4L2_PIXEL_ENC_BAYER) {
		acq_mult = 1;
		if (src_fmt->pixel_enc == V4L2_PIXEL_ENC_BAYER) {
			if (sensor->mbus_type == V4L2_MBUS_BT656)
				isp_ctrl = RKISP1_CIF_ISP_CTRL_ISP_MODE_RAW_PICT_ITU656;
			else
				isp_ctrl = RKISP1_CIF_ISP_CTRL_ISP_MODE_RAW_PICT;
		} else {
			rkisp1_write(rkisp1, RKISP1_CIF_ISP_DEMOSAIC_TH(0xc),
				     RKISP1_CIF_ISP_DEMOSAIC);

			if (sensor->mbus_type == V4L2_MBUS_BT656)
				isp_ctrl = RKISP1_CIF_ISP_CTRL_ISP_MODE_BAYER_ITU656;
			else
				isp_ctrl = RKISP1_CIF_ISP_CTRL_ISP_MODE_BAYER_ITU601;
		}
	} else if (sink_fmt->pixel_enc == V4L2_PIXEL_ENC_YUV) {
		acq_mult = 2;
		if (sensor->mbus_type == V4L2_MBUS_CSI2_DPHY) {
			isp_ctrl = RKISP1_CIF_ISP_CTRL_ISP_MODE_ITU601;
		} else {
			if (sensor->mbus_type == V4L2_MBUS_BT656)
				isp_ctrl = RKISP1_CIF_ISP_CTRL_ISP_MODE_ITU656;
			else
				isp_ctrl = RKISP1_CIF_ISP_CTRL_ISP_MODE_ITU601;
		}

		irq_mask |= RKISP1_CIF_ISP_DATA_LOSS;
	}

	/* Set up input acquisition properties */
	if (sensor->mbus_type == V4L2_MBUS_BT656 ||
	    sensor->mbus_type == V4L2_MBUS_PARALLEL) {
		if (sensor->mbus_flags & V4L2_MBUS_PCLK_SAMPLE_RISING)
			signal = RKISP1_CIF_ISP_ACQ_PROP_POS_EDGE;
	}

	if (sensor->mbus_type == V4L2_MBUS_PARALLEL) {
		if (sensor->mbus_flags & V4L2_MBUS_VSYNC_ACTIVE_LOW)
			signal |= RKISP1_CIF_ISP_ACQ_PROP_VSYNC_LOW;

		if (sensor->mbus_flags & V4L2_MBUS_HSYNC_ACTIVE_LOW)
			signal |= RKISP1_CIF_ISP_ACQ_PROP_HSYNC_LOW;
	}

	rkisp1_write(rkisp1, isp_ctrl, RKISP1_CIF_ISP_CTRL);
	rkisp1_write(rkisp1, signal | sink_fmt->yuv_seq |
		     RKISP1_CIF_ISP_ACQ_PROP_BAYER_PAT(sink_fmt->bayer_pat) |
		     RKISP1_CIF_ISP_ACQ_PROP_FIELD_SEL_ALL,
		     RKISP1_CIF_ISP_ACQ_PROP);
	rkisp1_write(rkisp1, 0, RKISP1_CIF_ISP_ACQ_NR_FRAMES);

	/* Acquisition Size */
	rkisp1_write(rkisp1, 0, RKISP1_CIF_ISP_ACQ_H_OFFS);
	rkisp1_write(rkisp1, 0, RKISP1_CIF_ISP_ACQ_V_OFFS);
	rkisp1_write(rkisp1,
		     acq_mult * sink_frm->width, RKISP1_CIF_ISP_ACQ_H_SIZE);
	rkisp1_write(rkisp1, sink_frm->height, RKISP1_CIF_ISP_ACQ_V_SIZE);

	/* ISP Out Area */
	rkisp1_write(rkisp1, sink_crop->left, RKISP1_CIF_ISP_OUT_H_OFFS);
	rkisp1_write(rkisp1, sink_crop->top, RKISP1_CIF_ISP_OUT_V_OFFS);
	rkisp1_write(rkisp1, sink_crop->width, RKISP1_CIF_ISP_OUT_H_SIZE);
	rkisp1_write(rkisp1, sink_crop->height, RKISP1_CIF_ISP_OUT_V_SIZE);

	irq_mask |= RKISP1_CIF_ISP_FRAME | RKISP1_CIF_ISP_V_START |
		    RKISP1_CIF_ISP_PIC_SIZE_ERROR | RKISP1_CIF_ISP_FRAME_IN;
	rkisp1_write(rkisp1, irq_mask, RKISP1_CIF_ISP_IMSC);

	if (src_fmt->pixel_enc == V4L2_PIXEL_ENC_BAYER) {
		rkisp1_params_disable(&rkisp1->params);
	} else {
		struct v4l2_mbus_framefmt *src_frm;

		src_frm = rkisp1_isp_get_pad_fmt(&rkisp1->isp, NULL,
						 RKISP1_ISP_PAD_SINK_VIDEO,
						 V4L2_SUBDEV_FORMAT_ACTIVE);
		rkisp1_params_configure(&rkisp1->params, sink_fmt->bayer_pat,
					src_frm->quantization);
	}

	return 0;
}

static int rkisp1_config_dvp(struct rkisp1_device *rkisp1)
{
	const struct rkisp1_isp_mbus_info *sink_fmt = rkisp1->isp.sink_fmt;
	u32 val, input_sel;

	switch (sink_fmt->bus_width) {
	case 8:
		input_sel = RKISP1_CIF_ISP_ACQ_PROP_IN_SEL_8B_ZERO;
		break;
	case 10:
		input_sel = RKISP1_CIF_ISP_ACQ_PROP_IN_SEL_10B_ZERO;
		break;
	case 12:
		input_sel = RKISP1_CIF_ISP_ACQ_PROP_IN_SEL_12B;
		break;
	default:
		dev_err(rkisp1->dev, "Invalid bus width\n");
		return -EINVAL;
	}

	val = rkisp1_read(rkisp1, RKISP1_CIF_ISP_ACQ_PROP);
	rkisp1_write(rkisp1, val | input_sel, RKISP1_CIF_ISP_ACQ_PROP);

	return 0;
}

static int rkisp1_config_mipi(struct rkisp1_device *rkisp1)
{
	const struct rkisp1_isp_mbus_info *sink_fmt = rkisp1->isp.sink_fmt;
	unsigned int lanes = rkisp1->active_sensor->lanes;
	u32 mipi_ctrl;

	if (lanes < 1 || lanes > 4)
		return -EINVAL;

	mipi_ctrl = RKISP1_CIF_MIPI_CTRL_NUM_LANES(lanes - 1) |
		    RKISP1_CIF_MIPI_CTRL_SHUTDOWNLANES(0xf) |
		    RKISP1_CIF_MIPI_CTRL_ERR_SOT_SYNC_HS_SKIP |
		    RKISP1_CIF_MIPI_CTRL_CLOCKLANE_ENA;

	rkisp1_write(rkisp1, mipi_ctrl, RKISP1_CIF_MIPI_CTRL);

	/* Configure Data Type and Virtual Channel */
	rkisp1_write(rkisp1,
		     RKISP1_CIF_MIPI_DATA_SEL_DT(sink_fmt->mipi_dt) |
		     RKISP1_CIF_MIPI_DATA_SEL_VC(0),
		     RKISP1_CIF_MIPI_IMG_DATA_SEL);

	/* Clear MIPI interrupts */
	rkisp1_write(rkisp1, ~0, RKISP1_CIF_MIPI_ICR);
	/*
	 * Disable RKISP1_CIF_MIPI_ERR_DPHY interrupt here temporary for
	 * isp bus may be dead when switch isp.
	 */
	rkisp1_write(rkisp1,
		     RKISP1_CIF_MIPI_FRAME_END | RKISP1_CIF_MIPI_ERR_CSI |
		     RKISP1_CIF_MIPI_ERR_DPHY |
		     RKISP1_CIF_MIPI_SYNC_FIFO_OVFLW(0x03) |
		     RKISP1_CIF_MIPI_ADD_DATA_OVFLW,
		     RKISP1_CIF_MIPI_IMSC);

	dev_dbg(rkisp1->dev, "\n  MIPI_CTRL 0x%08x\n"
		"  MIPI_IMG_DATA_SEL 0x%08x\n"
		"  MIPI_STATUS 0x%08x\n"
		"  MIPI_IMSC 0x%08x\n",
		rkisp1_read(rkisp1, RKISP1_CIF_MIPI_CTRL),
		rkisp1_read(rkisp1, RKISP1_CIF_MIPI_IMG_DATA_SEL),
		rkisp1_read(rkisp1, RKISP1_CIF_MIPI_STATUS),
		rkisp1_read(rkisp1, RKISP1_CIF_MIPI_IMSC));

	return 0;
}

/* Configure MUX */
static int rkisp1_config_path(struct rkisp1_device *rkisp1)
{
	struct rkisp1_sensor_async *sensor = rkisp1->active_sensor;
	u32 dpcl = rkisp1_read(rkisp1, RKISP1_CIF_VI_DPCL);
	int ret = 0;

	if (sensor->mbus_type == V4L2_MBUS_BT656 ||
	    sensor->mbus_type == V4L2_MBUS_PARALLEL) {
		ret = rkisp1_config_dvp(rkisp1);
		dpcl |= RKISP1_CIF_VI_DPCL_IF_SEL_PARALLEL;
	} else if (sensor->mbus_type == V4L2_MBUS_CSI2_DPHY) {
		ret = rkisp1_config_mipi(rkisp1);
		dpcl |= RKISP1_CIF_VI_DPCL_IF_SEL_MIPI;
	}

	rkisp1_write(rkisp1, dpcl, RKISP1_CIF_VI_DPCL);

	return ret;
}

/* Hardware configure Entry */
static int rkisp1_config_cif(struct rkisp1_device *rkisp1)
{
	u32 cif_id;
	int ret;

	cif_id = rkisp1_read(rkisp1, RKISP1_CIF_VI_ID);
	dev_dbg(rkisp1->dev, "CIF_ID 0x%08x\n", cif_id);

	ret = rkisp1_config_isp(rkisp1);
	if (ret)
		return ret;
	ret = rkisp1_config_path(rkisp1);
	if (ret)
		return ret;
	rkisp1_config_ism(rkisp1);

	return 0;
}

static void rkisp1_isp_stop(struct rkisp1_device *rkisp1)
{
	u32 val;

	/*
	 * ISP(mi) stop in mi frame end -> Stop ISP(mipi) ->
	 * Stop ISP(isp) ->wait for ISP isp off
	 */
	/* stop and clear MI, MIPI, and ISP interrupts */
	rkisp1_write(rkisp1, 0, RKISP1_CIF_MIPI_IMSC);
	rkisp1_write(rkisp1, ~0, RKISP1_CIF_MIPI_ICR);

	rkisp1_write(rkisp1, 0, RKISP1_CIF_ISP_IMSC);
	rkisp1_write(rkisp1, ~0, RKISP1_CIF_ISP_ICR);

	rkisp1_write(rkisp1, 0, RKISP1_CIF_MI_IMSC);
	rkisp1_write(rkisp1, ~0, RKISP1_CIF_MI_ICR);
	val = rkisp1_read(rkisp1, RKISP1_CIF_MIPI_CTRL);
	rkisp1_write(rkisp1, val & (~RKISP1_CIF_MIPI_CTRL_OUTPUT_ENA),
		     RKISP1_CIF_MIPI_CTRL);
	/* stop ISP */
	val = rkisp1_read(rkisp1, RKISP1_CIF_ISP_CTRL);
	val &= ~(RKISP1_CIF_ISP_CTRL_ISP_INFORM_ENABLE |
		 RKISP1_CIF_ISP_CTRL_ISP_ENABLE);
	rkisp1_write(rkisp1, val, RKISP1_CIF_ISP_CTRL);

	val = rkisp1_read(rkisp1,	RKISP1_CIF_ISP_CTRL);
	rkisp1_write(rkisp1, val | RKISP1_CIF_ISP_CTRL_ISP_CFG_UPD,
		     RKISP1_CIF_ISP_CTRL);

	readx_poll_timeout(readl, rkisp1->base_addr + RKISP1_CIF_ISP_RIS,
			   val, val & RKISP1_CIF_ISP_OFF, 20, 100);
	rkisp1_write(rkisp1,
		     RKISP1_CIF_IRCL_MIPI_SW_RST | RKISP1_CIF_IRCL_ISP_SW_RST,
		     RKISP1_CIF_IRCL);
	rkisp1_write(rkisp1, 0x0, RKISP1_CIF_IRCL);
}

static void rkisp1_config_clk(struct rkisp1_device *rkisp1)
{
	u32 val = RKISP1_CIF_ICCL_ISP_CLK | RKISP1_CIF_ICCL_CP_CLK |
		  RKISP1_CIF_ICCL_MRSZ_CLK | RKISP1_CIF_ICCL_SRSZ_CLK |
		  RKISP1_CIF_ICCL_JPEG_CLK | RKISP1_CIF_ICCL_MI_CLK |
		  RKISP1_CIF_ICCL_IE_CLK | RKISP1_CIF_ICCL_MIPI_CLK |
		  RKISP1_CIF_ICCL_DCROP_CLK;

	rkisp1_write(rkisp1, val, RKISP1_CIF_ICCL);
}

static void rkisp1_isp_start(struct rkisp1_device *rkisp1)
{
	struct rkisp1_sensor_async *sensor = rkisp1->active_sensor;
	u32 val;

	rkisp1_config_clk(rkisp1);

	/* Activate MIPI */
	if (sensor->mbus_type == V4L2_MBUS_CSI2_DPHY) {
		val = rkisp1_read(rkisp1, RKISP1_CIF_MIPI_CTRL);
		rkisp1_write(rkisp1, val | RKISP1_CIF_MIPI_CTRL_OUTPUT_ENA,
			     RKISP1_CIF_MIPI_CTRL);
	}
	/* Activate ISP */
	val = rkisp1_read(rkisp1, RKISP1_CIF_ISP_CTRL);
	val |= RKISP1_CIF_ISP_CTRL_ISP_CFG_UPD |
	       RKISP1_CIF_ISP_CTRL_ISP_ENABLE |
	       RKISP1_CIF_ISP_CTRL_ISP_INFORM_ENABLE;
	rkisp1_write(rkisp1, val, RKISP1_CIF_ISP_CTRL);

	/*
	 * CIF spec says to wait for sufficient time after enabling
	 * the MIPI interface and before starting the sensor output.
	 */
	usleep_range(1000, 1200);
}

/* ----------------------------------------------------------------------------
 * Subdev pad operations
 */

static int rkisp1_isp_enum_mbus_code(struct v4l2_subdev *sd,
				     struct v4l2_subdev_pad_config *cfg,
				     struct v4l2_subdev_mbus_code_enum *code)
{
	unsigned int i, dir;
	int pos = 0;

	if (code->pad == RKISP1_ISP_PAD_SINK_VIDEO) {
		dir = RKISP1_DIR_SINK;
	} else if (code->pad == RKISP1_ISP_PAD_SOURCE_VIDEO) {
		dir = RKISP1_DIR_SRC;
	} else {
		if (code->index > 0)
			return -EINVAL;
		code->code = MEDIA_BUS_FMT_FIXED;
		return 0;
	}

	if (code->index >= ARRAY_SIZE(rkisp1_isp_formats))
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(rkisp1_isp_formats); i++) {
		const struct rkisp1_isp_mbus_info *fmt = &rkisp1_isp_formats[i];

		if (fmt->direction & dir)
			pos++;

		if (code->index == pos - 1) {
			code->code = fmt->mbus_code;
			return 0;
		}
	}

	return -EINVAL;
}

static int rkisp1_isp_init_config(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg)
{
	struct v4l2_mbus_framefmt *sink_fmt, *src_fmt;
	struct v4l2_rect *sink_crop, *src_crop;

	sink_fmt = v4l2_subdev_get_try_format(sd, cfg,
					      RKISP1_ISP_PAD_SINK_VIDEO);
	sink_fmt->width = RKISP1_DEFAULT_WIDTH;
	sink_fmt->height = RKISP1_DEFAULT_HEIGHT;
	sink_fmt->field = V4L2_FIELD_NONE;
	sink_fmt->code = RKISP1_DEF_SINK_PAD_FMT;

	sink_crop = v4l2_subdev_get_try_crop(sd, cfg,
					     RKISP1_ISP_PAD_SINK_VIDEO);
	sink_crop->width = RKISP1_DEFAULT_WIDTH;
	sink_crop->height = RKISP1_DEFAULT_HEIGHT;
	sink_crop->left = 0;
	sink_crop->top = 0;

	src_fmt = v4l2_subdev_get_try_format(sd, cfg,
					     RKISP1_ISP_PAD_SOURCE_VIDEO);
	*src_fmt = *sink_fmt;
	src_fmt->code = RKISP1_DEF_SRC_PAD_FMT;
	src_fmt->quantization = V4L2_QUANTIZATION_FULL_RANGE;

	src_crop = v4l2_subdev_get_try_crop(sd, cfg,
					    RKISP1_ISP_PAD_SOURCE_VIDEO);
	*src_crop = *sink_crop;

	sink_fmt = v4l2_subdev_get_try_format(sd, cfg,
					      RKISP1_ISP_PAD_SINK_PARAMS);
	src_fmt = v4l2_subdev_get_try_format(sd, cfg,
					     RKISP1_ISP_PAD_SOURCE_STATS);
	sink_fmt->width = RKISP1_DEFAULT_WIDTH;
	sink_fmt->height = RKISP1_DEFAULT_HEIGHT;
	sink_fmt->field = V4L2_FIELD_NONE;
	sink_fmt->code = MEDIA_BUS_FMT_FIXED;
	*src_fmt = *sink_fmt;

	return 0;
}

static void rkisp1_isp_set_src_fmt(struct rkisp1_isp *isp,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_mbus_framefmt *format,
				   unsigned int which)
{
	const struct rkisp1_isp_mbus_info *mbus_info;
	struct v4l2_mbus_framefmt *src_fmt;
	const struct v4l2_rect *src_crop;

	src_fmt = rkisp1_isp_get_pad_fmt(isp, cfg,
					 RKISP1_ISP_PAD_SOURCE_VIDEO, which);
	src_crop = rkisp1_isp_get_pad_crop(isp, cfg,
					   RKISP1_ISP_PAD_SOURCE_VIDEO, which);

	src_fmt->code = format->code;
	mbus_info = rkisp1_isp_mbus_info_get(src_fmt->code);
	if (!mbus_info || !(mbus_info->direction & RKISP1_DIR_SRC)) {
		src_fmt->code = RKISP1_DEF_SRC_PAD_FMT;
		mbus_info = rkisp1_isp_mbus_info_get(src_fmt->code);
	}
	if (which == V4L2_SUBDEV_FORMAT_ACTIVE)
		isp->src_fmt = mbus_info;
	src_fmt->width  = src_crop->width;
	src_fmt->height = src_crop->height;
	src_fmt->quantization = format->quantization;
	/* full range by default */
	if (!src_fmt->quantization)
		src_fmt->quantization = V4L2_QUANTIZATION_FULL_RANGE;

	*format = *src_fmt;
}

static void rkisp1_isp_set_src_crop(struct rkisp1_isp *isp,
				    struct v4l2_subdev_pad_config *cfg,
				    struct v4l2_rect *r, unsigned int which)
{
	struct v4l2_mbus_framefmt *src_fmt;
	const struct v4l2_rect *sink_crop;
	struct v4l2_rect *src_crop;

	src_crop = rkisp1_isp_get_pad_crop(isp, cfg,
					   RKISP1_ISP_PAD_SOURCE_VIDEO,
					   which);
	sink_crop = rkisp1_isp_get_pad_crop(isp, cfg,
					    RKISP1_ISP_PAD_SINK_VIDEO,
					    which);

	src_crop->left = ALIGN(r->left, 2);
	src_crop->width = ALIGN(r->width, 2);
	src_crop->top = r->top;
	src_crop->height = r->height;
	rkisp1_sd_adjust_crop_rect(src_crop, sink_crop);

	*r = *src_crop;

	/* Propagate to out format */
	src_fmt = rkisp1_isp_get_pad_fmt(isp, cfg,
					 RKISP1_ISP_PAD_SOURCE_VIDEO, which);
	rkisp1_isp_set_src_fmt(isp, cfg, src_fmt, which);
}

static void rkisp1_isp_set_sink_crop(struct rkisp1_isp *isp,
				     struct v4l2_subdev_pad_config *cfg,
				     struct v4l2_rect *r, unsigned int which)
{
	struct v4l2_rect *sink_crop, *src_crop;
	struct v4l2_mbus_framefmt *sink_fmt;

	sink_crop = rkisp1_isp_get_pad_crop(isp, cfg, RKISP1_ISP_PAD_SINK_VIDEO,
					    which);
	sink_fmt = rkisp1_isp_get_pad_fmt(isp, cfg, RKISP1_ISP_PAD_SINK_VIDEO,
					  which);

	sink_crop->left = ALIGN(r->left, 2);
	sink_crop->width = ALIGN(r->width, 2);
	sink_crop->top = r->top;
	sink_crop->height = r->height;
	rkisp1_sd_adjust_crop(sink_crop, sink_fmt);

	*r = *sink_crop;

	/* Propagate to out crop */
	src_crop = rkisp1_isp_get_pad_crop(isp, cfg,
					   RKISP1_ISP_PAD_SOURCE_VIDEO, which);
	rkisp1_isp_set_src_crop(isp, cfg, src_crop, which);
}

static void rkisp1_isp_set_sink_fmt(struct rkisp1_isp *isp,
				    struct v4l2_subdev_pad_config *cfg,
				    struct v4l2_mbus_framefmt *format,
				    unsigned int which)
{
	const struct rkisp1_isp_mbus_info *mbus_info;
	struct v4l2_mbus_framefmt *sink_fmt;
	struct v4l2_rect *sink_crop;

	sink_fmt = rkisp1_isp_get_pad_fmt(isp, cfg, RKISP1_ISP_PAD_SINK_VIDEO,
					  which);
	sink_fmt->code = format->code;
	mbus_info = rkisp1_isp_mbus_info_get(sink_fmt->code);
	if (!mbus_info || !(mbus_info->direction & RKISP1_DIR_SINK)) {
		sink_fmt->code = RKISP1_DEF_SINK_PAD_FMT;
		mbus_info = rkisp1_isp_mbus_info_get(sink_fmt->code);
	}
	if (which == V4L2_SUBDEV_FORMAT_ACTIVE)
		isp->sink_fmt = mbus_info;

	sink_fmt->width = clamp_t(u32, format->width,
				  RKISP1_ISP_MIN_WIDTH,
				  RKISP1_ISP_MAX_WIDTH);
	sink_fmt->height = clamp_t(u32, format->height,
				   RKISP1_ISP_MIN_HEIGHT,
				   RKISP1_ISP_MAX_HEIGHT);

	*format = *sink_fmt;

	/* Propagate to in crop */
	sink_crop = rkisp1_isp_get_pad_crop(isp, cfg, RKISP1_ISP_PAD_SINK_VIDEO,
					    which);
	rkisp1_isp_set_sink_crop(isp, cfg, sink_crop, which);
}

static int rkisp1_isp_get_fmt(struct v4l2_subdev *sd,
			      struct v4l2_subdev_pad_config *cfg,
			      struct v4l2_subdev_format *fmt)
{
	struct rkisp1_isp *isp = container_of(sd, struct rkisp1_isp, sd);

	mutex_lock(&isp->ops_lock);
	fmt->format = *rkisp1_isp_get_pad_fmt(isp, cfg, fmt->pad, fmt->which);
	mutex_unlock(&isp->ops_lock);
	return 0;
}

static int rkisp1_isp_set_fmt(struct v4l2_subdev *sd,
			      struct v4l2_subdev_pad_config *cfg,
			      struct v4l2_subdev_format *fmt)
{
	struct rkisp1_isp *isp = container_of(sd, struct rkisp1_isp, sd);

	mutex_lock(&isp->ops_lock);
	if (fmt->pad == RKISP1_ISP_PAD_SINK_VIDEO)
		rkisp1_isp_set_sink_fmt(isp, cfg, &fmt->format, fmt->which);
	else if (fmt->pad == RKISP1_ISP_PAD_SOURCE_VIDEO)
		rkisp1_isp_set_src_fmt(isp, cfg, &fmt->format, fmt->which);
	else
		fmt->format = *rkisp1_isp_get_pad_fmt(isp, cfg, fmt->pad,
						      fmt->which);

	mutex_unlock(&isp->ops_lock);
	return 0;
}

static int rkisp1_isp_get_selection(struct v4l2_subdev *sd,
				    struct v4l2_subdev_pad_config *cfg,
				    struct v4l2_subdev_selection *sel)
{
	struct rkisp1_isp *isp = container_of(sd, struct rkisp1_isp, sd);
	int ret = 0;

	if (sel->pad != RKISP1_ISP_PAD_SOURCE_VIDEO &&
	    sel->pad != RKISP1_ISP_PAD_SINK_VIDEO)
		return -EINVAL;

	mutex_lock(&isp->ops_lock);
	switch (sel->target) {
	case V4L2_SEL_TGT_CROP_BOUNDS:
		if (sel->pad == RKISP1_ISP_PAD_SINK_VIDEO) {
			struct v4l2_mbus_framefmt *fmt;

			fmt = rkisp1_isp_get_pad_fmt(isp, cfg, sel->pad,
						     sel->which);
			sel->r.height = fmt->height;
			sel->r.width = fmt->width;
			sel->r.left = 0;
			sel->r.top = 0;
		} else {
			sel->r = *rkisp1_isp_get_pad_crop(isp, cfg,
						RKISP1_ISP_PAD_SINK_VIDEO,
						sel->which);
		}
		break;
	case V4L2_SEL_TGT_CROP:
		sel->r = *rkisp1_isp_get_pad_crop(isp, cfg, sel->pad,
						  sel->which);
		break;
	default:
		ret = -EINVAL;
	}
	mutex_unlock(&isp->ops_lock);
	return ret;
}

static int rkisp1_isp_set_selection(struct v4l2_subdev *sd,
				    struct v4l2_subdev_pad_config *cfg,
				    struct v4l2_subdev_selection *sel)
{
	struct rkisp1_device *rkisp1 =
		container_of(sd->v4l2_dev, struct rkisp1_device, v4l2_dev);
	struct rkisp1_isp *isp = container_of(sd, struct rkisp1_isp, sd);
	int ret = 0;

	if (sel->target != V4L2_SEL_TGT_CROP)
		return -EINVAL;

	dev_dbg(rkisp1->dev, "%s: pad: %d sel(%d,%d)/%dx%d\n", __func__,
		sel->pad, sel->r.left, sel->r.top, sel->r.width, sel->r.height);
	mutex_lock(&isp->ops_lock);
	if (sel->pad == RKISP1_ISP_PAD_SINK_VIDEO)
		rkisp1_isp_set_sink_crop(isp, cfg, &sel->r, sel->which);
	else if (sel->pad == RKISP1_ISP_PAD_SOURCE_VIDEO)
		rkisp1_isp_set_src_crop(isp, cfg, &sel->r, sel->which);
	else
		ret = -EINVAL;

	mutex_unlock(&isp->ops_lock);
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
	.get_selection = rkisp1_isp_get_selection,
	.set_selection = rkisp1_isp_set_selection,
	.init_cfg = rkisp1_isp_init_config,
	.get_fmt = rkisp1_isp_get_fmt,
	.set_fmt = rkisp1_isp_set_fmt,
	.link_validate = v4l2_subdev_link_validate_default,
};

/* ----------------------------------------------------------------------------
 * Stream operations
 */

static int rkisp1_mipi_csi2_start(struct rkisp1_isp *isp,
				  struct rkisp1_sensor_async *sensor)
{
	union phy_configure_opts opts;
	struct phy_configure_opts_mipi_dphy *cfg = &opts.mipi_dphy;
	s64 pixel_clock;

	if (!sensor->pixel_rate_ctrl) {
		dev_warn(sensor->sd->dev, "No pixel rate control in subdev\n");
		return -EPIPE;
	}

	pixel_clock = v4l2_ctrl_g_ctrl_int64(sensor->pixel_rate_ctrl);
	if (!pixel_clock) {
		dev_err(sensor->sd->dev, "Invalid pixel rate value\n");
		return -EINVAL;
	}

	phy_mipi_dphy_get_default_config(pixel_clock, isp->sink_fmt->bus_width,
					 sensor->lanes, cfg);
	phy_set_mode(sensor->dphy, PHY_MODE_MIPI_DPHY);
	phy_configure(sensor->dphy, &opts);
	phy_power_on(sensor->dphy);

	return 0;
}

static void rkisp1_mipi_csi2_stop(struct rkisp1_sensor_async *sensor)
{
	phy_power_off(sensor->dphy);
}

static int rkisp1_isp_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct rkisp1_device *rkisp1 =
		container_of(sd->v4l2_dev, struct rkisp1_device, v4l2_dev);
	struct rkisp1_isp *isp = &rkisp1->isp;
	struct v4l2_subdev *sensor_sd;
	int ret = 0;

	if (!enable) {
		rkisp1_isp_stop(rkisp1);
		rkisp1_mipi_csi2_stop(rkisp1->active_sensor);
		return 0;
	}

	sensor_sd = rkisp1_get_remote_sensor(sd);
	if (!sensor_sd)
		return -ENODEV;
	rkisp1->active_sensor = container_of(sensor_sd->asd,
					     struct rkisp1_sensor_async, asd);

	if (rkisp1->active_sensor->mbus_type != V4L2_MBUS_CSI2_DPHY)
		return -EINVAL;

	atomic_set(&rkisp1->isp.frame_sequence, -1);
	mutex_lock(&isp->ops_lock);
	ret = rkisp1_config_cif(rkisp1);
	if (ret)
		goto mutex_unlock;

	ret = rkisp1_mipi_csi2_start(&rkisp1->isp, rkisp1->active_sensor);
	if (ret)
		goto mutex_unlock;

	rkisp1_isp_start(rkisp1);

mutex_unlock:
	mutex_unlock(&isp->ops_lock);
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

int rkisp1_isp_register(struct rkisp1_device *rkisp1,
			struct v4l2_device *v4l2_dev)
{
	struct rkisp1_isp *isp = &rkisp1->isp;
	struct media_pad *pads = isp->pads;
	struct v4l2_subdev *sd = &isp->sd;
	int ret;

	v4l2_subdev_init(sd, &rkisp1_isp_ops);
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

	isp->sink_fmt = rkisp1_isp_mbus_info_get(RKISP1_DEF_SINK_PAD_FMT);
	isp->src_fmt = rkisp1_isp_mbus_info_get(RKISP1_DEF_SRC_PAD_FMT);

	mutex_init(&isp->ops_lock);
	ret = media_entity_pads_init(&sd->entity, RKISP1_ISP_PAD_MAX, pads);
	if (ret)
		return ret;

	ret = v4l2_device_register_subdev(v4l2_dev, sd);
	if (ret) {
		dev_err(sd->dev, "Failed to register isp subdev\n");
		goto err_cleanup_media_entity;
	}

	rkisp1_isp_init_config(sd, rkisp1->isp.pad_cfg);
	return 0;

err_cleanup_media_entity:
	media_entity_cleanup(&sd->entity);

	return ret;
}

void rkisp1_isp_unregister(struct rkisp1_device *rkisp1)
{
	struct v4l2_subdev *sd = &rkisp1->isp.sd;

	v4l2_device_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
}

/* ----------------------------------------------------------------------------
 * Interrupt handlers
 */

void rkisp1_mipi_isr(struct rkisp1_device *rkisp1)
{
	u32 val, status;

	status = rkisp1_read(rkisp1, RKISP1_CIF_MIPI_MIS);
	if (!status)
		return;

	rkisp1_write(rkisp1, status, RKISP1_CIF_MIPI_ICR);

	/*
	 * Disable DPHY errctrl interrupt, because this dphy
	 * erctrl signal is asserted until the next changes
	 * of line state. This time is may be too long and cpu
	 * is hold in this interrupt.
	 */
	if (status & RKISP1_CIF_MIPI_ERR_CTRL(0x0f)) {
		val = rkisp1_read(rkisp1, RKISP1_CIF_MIPI_IMSC);
		rkisp1_write(rkisp1, val & ~RKISP1_CIF_MIPI_ERR_CTRL(0x0f),
			     RKISP1_CIF_MIPI_IMSC);
		rkisp1->isp.is_dphy_errctrl_disabled = true;
	}

	/*
	 * Enable DPHY errctrl interrupt again, if mipi have receive
	 * the whole frame without any error.
	 */
	if (status == RKISP1_CIF_MIPI_FRAME_END) {
		/*
		 * Enable DPHY errctrl interrupt again, if mipi have receive
		 * the whole frame without any error.
		 */
		if (rkisp1->isp.is_dphy_errctrl_disabled) {
			val = rkisp1_read(rkisp1, RKISP1_CIF_MIPI_IMSC);
			val |= RKISP1_CIF_MIPI_ERR_CTRL(0x0f);
			rkisp1_write(rkisp1, val, RKISP1_CIF_MIPI_IMSC);
			rkisp1->isp.is_dphy_errctrl_disabled = false;
		}
	} else {
		rkisp1->debug.mipi_error++;
	}
}

static void rkisp1_isp_queue_event_sof(struct rkisp1_isp *isp)
{
	struct v4l2_event event = {
		.type = V4L2_EVENT_FRAME_SYNC,
	};

	/*
	 * Increment the frame sequence on the vsync signal.
	 * This will allow applications to detect dropped.
	 * Note that there is a debugfs counter for dropped
	 * frames, but using this event is more accurate.
	 */
	event.u.frame_sync.frame_sequence =
		atomic_inc_return(&isp->frame_sequence);
	v4l2_event_queue(isp->sd.devnode, &event);
}

void rkisp1_isp_isr(struct rkisp1_device *rkisp1)
{
	u32 status, isp_err;

	status = rkisp1_read(rkisp1, RKISP1_CIF_ISP_MIS);
	if (!status)
		return;

	rkisp1_write(rkisp1, status, RKISP1_CIF_ISP_ICR);

	/* Vertical sync signal, starting generating new frame */
	if (status & RKISP1_CIF_ISP_V_START)
		rkisp1_isp_queue_event_sof(&rkisp1->isp);

	if (status & RKISP1_CIF_ISP_PIC_SIZE_ERROR) {
		/* Clear pic_size_error */
		isp_err = rkisp1_read(rkisp1, RKISP1_CIF_ISP_ERR);
		rkisp1_write(rkisp1, isp_err, RKISP1_CIF_ISP_ERR_CLR);
		rkisp1->debug.pic_size_error++;
	} else if (status & RKISP1_CIF_ISP_DATA_LOSS) {
		/* keep track of data_loss in debugfs */
		rkisp1->debug.data_loss++;
	}

	if (status & RKISP1_CIF_ISP_FRAME) {
		u32 isp_ris;

		/* New frame from the sensor received */
		isp_ris = rkisp1_read(rkisp1, RKISP1_CIF_ISP_RIS);
		if (isp_ris & (RKISP1_CIF_ISP_AWB_DONE |
			       RKISP1_CIF_ISP_AFM_FIN |
			       RKISP1_CIF_ISP_EXP_END |
			       RKISP1_CIF_ISP_HIST_MEASURE_RDY))
			rkisp1_stats_isr(&rkisp1->stats, isp_ris);
	}

	/*
	 * Then update changed configs. Some of them involve
	 * lot of register writes. Do those only one per frame.
	 * Do the updates in the order of the processing flow.
	 */
	rkisp1_params_isr(rkisp1, status);
}
