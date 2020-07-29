// SPDX-License-Identifier: GPL-2.0
/*
 * Rockchip CIF Driver
 *
 * Copyright (C) 2018 Rockchip Electronics Co., Ltd.
 */

#include <linux/delay.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <media/v4l2-common.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf2-dma-contig.h>

#include "dev.h"
#include "mipi-csi2.h"

#define CIF_REQ_BUFS_MIN	1
#define CIF_MIN_WIDTH		64
#define CIF_MIN_HEIGHT		64
#define CIF_MAX_WIDTH		8192
#define CIF_MAX_HEIGHT		8192

#define OUTPUT_STEP_WISE		8

#define RKCIF_PLANE_Y			0
#define RKCIF_PLANE_CBCR		1

#define STREAM_PAD_SINK				0
#define STREAM_PAD_SOURCE			1

/*
 * Round up height when allocate memory so that Rockchip encoder can
 * use DMA buffer directly, though this may waste a bit of memory.
 */
#define MEMORY_ALIGN_ROUND_UP_HEIGHT		16

/* Get xsubs and ysubs for fourcc formats
 *
 * @xsubs: horizontal color samples in a 4*4 matrix, for yuv
 * @ysubs: vertical color samples in a 4*4 matrix, for yuv
 */
static int fcc_xysubs(u32 fcc, u32 *xsubs, u32 *ysubs)
{
	switch (fcc) {
	case V4L2_PIX_FMT_NV16:
	case V4L2_PIX_FMT_NV61:
	case V4L2_PIX_FMT_UYVY:
	case V4L2_PIX_FMT_VYUY:
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_YVYU:
		*xsubs = 2;
		*ysubs = 1;
		break;
	case V4L2_PIX_FMT_NV21:
	case V4L2_PIX_FMT_NV12:
		*xsubs = 2;
		*ysubs = 2;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct cif_output_fmt out_fmts[] = {
	{
		.fourcc = V4L2_PIX_FMT_NV16,
		.cplanes = 2,
		.mplanes = 1,
		.fmt_val = YUV_OUTPUT_422 | UV_STORAGE_ORDER_UVUV,
		.bpp = { 8, 16 },
	}, {
		.fourcc = V4L2_PIX_FMT_NV61,
		.fmt_val = YUV_OUTPUT_422 | UV_STORAGE_ORDER_VUVU,
		.cplanes = 2,
		.mplanes = 1,
		.bpp = { 8, 16 },
	}, {
		.fourcc = V4L2_PIX_FMT_NV12,
		.fmt_val = YUV_OUTPUT_420 | UV_STORAGE_ORDER_UVUV,
		.cplanes = 2,
		.mplanes = 1,
		.bpp = { 8, 16 },
	}, {
		.fourcc = V4L2_PIX_FMT_NV21,
		.fmt_val = YUV_OUTPUT_420 | UV_STORAGE_ORDER_VUVU,
		.cplanes = 2,
		.mplanes = 1,
		.bpp = { 8, 16 },
	}, {
		.fourcc = V4L2_PIX_FMT_YUYV,
		.cplanes = 2,
		.mplanes = 1,
		.bpp = { 8, 16 },
	}, {
		.fourcc = V4L2_PIX_FMT_YVYU,
		.cplanes = 2,
		.mplanes = 1,
		.bpp = { 8, 16 },
	}, {
		.fourcc = V4L2_PIX_FMT_UYVY,
		.cplanes = 2,
		.mplanes = 1,
		.bpp = { 8, 16 },
	}, {
		.fourcc = V4L2_PIX_FMT_VYUY,
		.cplanes = 2,
		.mplanes = 1,
		.bpp = { 8, 16 },
	}, {
		.fourcc = V4L2_PIX_FMT_RGB24,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 24 },
	}, {
		.fourcc = V4L2_PIX_FMT_RGB565,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 16 },
	}, {
		.fourcc = V4L2_PIX_FMT_BGR666,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 18 },
	}, {
		.fourcc = V4L2_PIX_FMT_SRGGB8,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 8 },
		.raw_bpp = 8,
	}, {
		.fourcc = V4L2_PIX_FMT_SGRBG8,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 8 },
		.raw_bpp = 8,
	}, {
		.fourcc = V4L2_PIX_FMT_SGBRG8,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 8 },
		.raw_bpp = 8,
	}, {
		.fourcc = V4L2_PIX_FMT_SBGGR8,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 8 },
		.raw_bpp = 8,
	}, {
		.fourcc = V4L2_PIX_FMT_SRGGB10,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 16 },
		.raw_bpp = 10,
	}, {
		.fourcc = V4L2_PIX_FMT_SGRBG10,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 16 },
		.raw_bpp = 10,
	}, {
		.fourcc = V4L2_PIX_FMT_SGBRG10,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 16 },
		.raw_bpp = 10,
	}, {
		.fourcc = V4L2_PIX_FMT_SBGGR10,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 16 },
		.raw_bpp = 10,
	}, {
		.fourcc = V4L2_PIX_FMT_SRGGB12,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 16 },
		.raw_bpp = 12,
	}, {
		.fourcc = V4L2_PIX_FMT_SGRBG12,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 16 },
		.raw_bpp = 12,
	}, {
		.fourcc = V4L2_PIX_FMT_SGBRG12,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 16 },
		.raw_bpp = 12,
	}, {
		.fourcc = V4L2_PIX_FMT_SBGGR12,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 16 },
		.raw_bpp = 12,
	}, {
		.fourcc = V4L2_PIX_FMT_SBGGR16,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 16 },
		.raw_bpp = 16,
	}, {
		.fourcc = V4L2_PIX_FMT_Y16,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 16 },
	}

	/* TODO: We can support NV12M/NV21M/NV16M/NV61M too */
};

static const struct cif_input_fmt in_fmts[] = {
	{
		.mbus_code	= MEDIA_BUS_FMT_YUYV8_2X8,
		.dvp_fmt_val	= YUV_INPUT_422 | YUV_INPUT_ORDER_YUYV,
		.csi_fmt_val	= CSI_WRDDR_TYPE_YUV422,
		.fmt_type	= CIF_FMT_TYPE_YUV,
		.field		= V4L2_FIELD_NONE,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_YUYV8_2X8,
		.dvp_fmt_val	= YUV_INPUT_422 | YUV_INPUT_ORDER_YUYV,
		.csi_fmt_val	= CSI_WRDDR_TYPE_YUV422,
		.fmt_type	= CIF_FMT_TYPE_YUV,
		.field		= V4L2_FIELD_INTERLACED,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_YVYU8_2X8,
		.dvp_fmt_val	= YUV_INPUT_422 | YUV_INPUT_ORDER_YVYU,
		.csi_fmt_val	= CSI_WRDDR_TYPE_YUV422,
		.fmt_type	= CIF_FMT_TYPE_YUV,
		.field		= V4L2_FIELD_NONE,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_YVYU8_2X8,
		.dvp_fmt_val	= YUV_INPUT_422 | YUV_INPUT_ORDER_YVYU,
		.csi_fmt_val	= CSI_WRDDR_TYPE_YUV422,
		.fmt_type	= CIF_FMT_TYPE_YUV,
		.field		= V4L2_FIELD_INTERLACED,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_UYVY8_2X8,
		.dvp_fmt_val	= YUV_INPUT_422 | YUV_INPUT_ORDER_UYVY,
		.csi_fmt_val	= CSI_WRDDR_TYPE_YUV422,
		.fmt_type	= CIF_FMT_TYPE_YUV,
		.field		= V4L2_FIELD_NONE,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_UYVY8_2X8,
		.dvp_fmt_val	= YUV_INPUT_422 | YUV_INPUT_ORDER_UYVY,
		.csi_fmt_val	= CSI_WRDDR_TYPE_YUV422,
		.fmt_type	= CIF_FMT_TYPE_YUV,
		.field		= V4L2_FIELD_INTERLACED,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_VYUY8_2X8,
		.dvp_fmt_val	= YUV_INPUT_422 | YUV_INPUT_ORDER_VYUY,
		.csi_fmt_val	= CSI_WRDDR_TYPE_YUV422,
		.fmt_type	= CIF_FMT_TYPE_YUV,
		.field		= V4L2_FIELD_NONE,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_VYUY8_2X8,
		.dvp_fmt_val	= YUV_INPUT_422 | YUV_INPUT_ORDER_VYUY,
		.csi_fmt_val	= CSI_WRDDR_TYPE_YUV422,
		.fmt_type	= CIF_FMT_TYPE_YUV,
		.field		= V4L2_FIELD_INTERLACED,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SBGGR8_1X8,
		.dvp_fmt_val	= INPUT_MODE_RAW | RAW_DATA_WIDTH_8,
		.csi_fmt_val	= CSI_WRDDR_TYPE_RAW8,
		.fmt_type	= CIF_FMT_TYPE_RAW,
		.field		= V4L2_FIELD_NONE,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SGBRG8_1X8,
		.dvp_fmt_val	= INPUT_MODE_RAW | RAW_DATA_WIDTH_8,
		.csi_fmt_val	= CSI_WRDDR_TYPE_RAW8,
		.fmt_type	= CIF_FMT_TYPE_RAW,
		.field		= V4L2_FIELD_NONE,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SGRBG8_1X8,
		.dvp_fmt_val	= INPUT_MODE_RAW | RAW_DATA_WIDTH_8,
		.csi_fmt_val	= CSI_WRDDR_TYPE_RAW8,
		.fmt_type	= CIF_FMT_TYPE_RAW,
		.field		= V4L2_FIELD_NONE,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SRGGB8_1X8,
		.dvp_fmt_val	= INPUT_MODE_RAW | RAW_DATA_WIDTH_8,
		.csi_fmt_val	= CSI_WRDDR_TYPE_RAW8,
		.fmt_type	= CIF_FMT_TYPE_RAW,
		.field		= V4L2_FIELD_NONE,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SBGGR10_1X10,
		.dvp_fmt_val	= INPUT_MODE_RAW | RAW_DATA_WIDTH_10,
		.csi_fmt_val	= CSI_WRDDR_TYPE_RAW10,
		.fmt_type	= CIF_FMT_TYPE_RAW,
		.field		= V4L2_FIELD_NONE,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SGBRG10_1X10,
		.dvp_fmt_val	= INPUT_MODE_RAW | RAW_DATA_WIDTH_10,
		.csi_fmt_val	= CSI_WRDDR_TYPE_RAW10,
		.fmt_type	= CIF_FMT_TYPE_RAW,
		.field		= V4L2_FIELD_NONE,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SGRBG10_1X10,
		.dvp_fmt_val	= INPUT_MODE_RAW | RAW_DATA_WIDTH_10,
		.csi_fmt_val	= CSI_WRDDR_TYPE_RAW10,
		.fmt_type	= CIF_FMT_TYPE_RAW,
		.field		= V4L2_FIELD_NONE,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SRGGB10_1X10,
		.dvp_fmt_val	= INPUT_MODE_RAW | RAW_DATA_WIDTH_10,
		.csi_fmt_val	= CSI_WRDDR_TYPE_RAW10,
		.fmt_type	= CIF_FMT_TYPE_RAW,
		.field		= V4L2_FIELD_NONE,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SBGGR12_1X12,
		.dvp_fmt_val	= INPUT_MODE_RAW | RAW_DATA_WIDTH_12,
		.csi_fmt_val	= CSI_WRDDR_TYPE_RAW12,
		.fmt_type	= CIF_FMT_TYPE_RAW,
		.field		= V4L2_FIELD_NONE,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SGBRG12_1X12,
		.dvp_fmt_val	= INPUT_MODE_RAW | RAW_DATA_WIDTH_12,
		.csi_fmt_val	= CSI_WRDDR_TYPE_RAW12,
		.fmt_type	= CIF_FMT_TYPE_RAW,
		.field		= V4L2_FIELD_NONE,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SGRBG12_1X12,
		.dvp_fmt_val	= INPUT_MODE_RAW | RAW_DATA_WIDTH_12,
		.csi_fmt_val	= CSI_WRDDR_TYPE_RAW12,
		.fmt_type	= CIF_FMT_TYPE_RAW,
		.field		= V4L2_FIELD_NONE,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SRGGB12_1X12,
		.dvp_fmt_val	= INPUT_MODE_RAW | RAW_DATA_WIDTH_12,
		.csi_fmt_val	= CSI_WRDDR_TYPE_RAW12,
		.fmt_type	= CIF_FMT_TYPE_RAW,
		.field		= V4L2_FIELD_NONE,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_RGB888_1X24,
		.csi_fmt_val	= CSI_WRDDR_TYPE_RGB888,
		.field		= V4L2_FIELD_NONE,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_Y8_1X8,
		.dvp_fmt_val	= INPUT_MODE_RAW | RAW_DATA_WIDTH_8,
		.csi_fmt_val	= CSI_WRDDR_TYPE_RAW8,
		.fmt_type	= CIF_FMT_TYPE_RAW,
		.field		= V4L2_FIELD_NONE,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_Y10_1X10,
		.dvp_fmt_val	= INPUT_MODE_RAW | RAW_DATA_WIDTH_10,
		.csi_fmt_val	= CSI_WRDDR_TYPE_RAW10,
		.fmt_type	= CIF_FMT_TYPE_RAW,
		.field		= V4L2_FIELD_NONE,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_Y12_1X12,
		.dvp_fmt_val	= INPUT_MODE_RAW | RAW_DATA_WIDTH_12,
		.csi_fmt_val	= CSI_WRDDR_TYPE_RAW12,
		.fmt_type	= CIF_FMT_TYPE_RAW,
		.field		= V4L2_FIELD_NONE,
	}
};

static struct v4l2_subdev *get_remote_sensor(struct rkcif_stream *stream)
{
	struct media_pad *local, *remote;
	struct media_entity *sensor_me;

	local = &stream->vnode.vdev.entity.pads[0];
	if (!local)
		return NULL;
	remote = media_entity_remote_pad(local);
	if (!remote)
		return NULL;

	sensor_me = remote->entity;

	return media_entity_to_v4l2_subdev(sensor_me);
}

static void get_remote_terminal_sensor(struct rkcif_stream *stream,
				       struct v4l2_subdev **sensor_sd)
{
	struct media_graph graph;
	struct media_entity *entity = &stream->vnode.vdev.entity;
	struct media_device *mdev = entity->graph_obj.mdev;
	int ret;

	/* Walk the graph to locate sensor nodes. */
	mutex_lock(&mdev->graph_mutex);
	ret = media_graph_walk_init(&graph, mdev);
	if (ret) {
		mutex_unlock(&mdev->graph_mutex);
		*sensor_sd = NULL;
		return;
	}

	media_graph_walk_start(&graph, entity);
	while ((entity = media_graph_walk_next(&graph))) {
		if (entity->function == MEDIA_ENT_F_CAM_SENSOR)
			break;
	}
	mutex_unlock(&mdev->graph_mutex);
	media_graph_walk_cleanup(&graph);

	if (entity)
		*sensor_sd = media_entity_to_v4l2_subdev(entity);
	else
		*sensor_sd = NULL;
}

static struct rkcif_sensor_info *sd_to_sensor(struct rkcif_device *dev,
					      struct v4l2_subdev *sd)
{
	u32 i;

	for (i = 0; i < dev->num_sensors; ++i)
		if (dev->sensors[i].sd == sd)
			return &dev->sensors[i];

	if (i == dev->num_sensors) {
		for (i = 0; i < dev->num_sensors; ++i) {
			if (dev->sensors[i].mbus.type == V4L2_MBUS_CCP2)
				return &dev->lvds_subdev.sensor_self;
		}
	}

	return NULL;
}

static int rkcif_update_sensor_info(struct rkcif_stream *stream)
{
	struct rkcif_sensor_info *sensor;
	struct v4l2_subdev *sensor_sd;
	int ret = 0;

	sensor_sd = get_remote_sensor(stream);
	if (!sensor_sd)
		return -ENODEV;

	sensor = sd_to_sensor(stream->cifdev, sensor_sd);
	if (!sensor) {
		v4l2_err(&stream->cifdev->v4l2_dev, "get sensor for updating failed!\n");
		return -ENODEV;
	}
	ret = v4l2_subdev_call(sensor->sd, video, g_mbus_config,
			       &sensor->mbus);
	if (ret && ret != -ENOIOCTLCMD)
		return ret;
	stream->cifdev->active_sensor = sensor;

	get_remote_terminal_sensor(stream, &stream->cifdev->terminal_sensor);

	return ret;
}

static unsigned char get_data_type(u32 pixelformat, u8 cmd_mode_en)
{
	switch (pixelformat) {
	/* csi raw8 */
	case MEDIA_BUS_FMT_SBGGR8_1X8:
	case MEDIA_BUS_FMT_SGBRG8_1X8:
	case MEDIA_BUS_FMT_SGRBG8_1X8:
	case MEDIA_BUS_FMT_SRGGB8_1X8:
		return 0x2a;
	/* csi raw10 */
	case MEDIA_BUS_FMT_SBGGR10_1X10:
	case MEDIA_BUS_FMT_SGBRG10_1X10:
	case MEDIA_BUS_FMT_SGRBG10_1X10:
	case MEDIA_BUS_FMT_SRGGB10_1X10:
		return 0x2b;
	/* csi raw12 */
	case MEDIA_BUS_FMT_SBGGR12_1X12:
	case MEDIA_BUS_FMT_SGBRG12_1X12:
	case MEDIA_BUS_FMT_SGRBG12_1X12:
	case MEDIA_BUS_FMT_SRGGB12_1X12:
		return 0x2c;
	/* csi uyvy 422 */
	case MEDIA_BUS_FMT_UYVY8_2X8:
	case MEDIA_BUS_FMT_VYUY8_2X8:
	case MEDIA_BUS_FMT_YUYV8_2X8:
	case MEDIA_BUS_FMT_YVYU8_2X8:
		return 0x1e;
	case MEDIA_BUS_FMT_RGB888_1X24: {
		if (cmd_mode_en) /* dsi command mode*/
			return 0x39;
		else /* dsi video mode */
			return 0x3e;
	}

	default:
		return 0x2b;
	}
}

static int get_csi_crop_align(const struct cif_input_fmt *fmt_in)
{
	switch (fmt_in->csi_fmt_val) {
	case CSI_WRDDR_TYPE_RGB888:
		return 24;
	case CSI_WRDDR_TYPE_RAW10:
	case CSI_WRDDR_TYPE_RAW12:
		return 4;
	case CSI_WRDDR_TYPE_RAW8:
	case CSI_WRDDR_TYPE_YUV422:
		return 8;
	default:
		return -1;
	}
}

static const struct
cif_input_fmt *get_input_fmt(struct v4l2_subdev *sd, struct v4l2_rect *rect,
			     u32 pad)
{
	struct v4l2_subdev_format fmt;
	int ret;
	u32 i;

	fmt.pad = pad;
	fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	ret = v4l2_subdev_call(sd, pad, get_fmt, NULL, &fmt);
	if (ret < 0) {
		v4l2_warn(sd->v4l2_dev,
			  "sensor fmt invalid, set to default size\n");
		goto set_default;
	}

	v4l2_dbg(1, rkcif_debug, sd->v4l2_dev,
		 "remote fmt: mbus code:0x%x, size:%dx%d, field: %d\n",
		 fmt.format.code, fmt.format.width,
		 fmt.format.height, fmt.format.field);
	rect->left = 0;
	rect->top = 0;
	rect->width = fmt.format.width;
	rect->height = fmt.format.height;

	for (i = 0; i < ARRAY_SIZE(in_fmts); i++)
		if (fmt.format.code == in_fmts[i].mbus_code &&
		    fmt.format.field == in_fmts[i].field)
			return &in_fmts[i];

	v4l2_err(sd->v4l2_dev, "remote sensor mbus code not supported\n");

set_default:
	rect->left = 0;
	rect->top = 0;
	rect->width = RKCIF_DEFAULT_WIDTH;
	rect->height = RKCIF_DEFAULT_HEIGHT;

	return NULL;
}

static const struct
cif_output_fmt *find_output_fmt(struct rkcif_stream *stream, u32 pixelfmt)
{
	const struct cif_output_fmt *fmt;
	u32 i;

	for (i = 0; i < ARRAY_SIZE(out_fmts); i++) {
		fmt = &out_fmts[i];
		if (fmt->fourcc == pixelfmt)
			return fmt;
	}

	return NULL;
}

static enum cif_reg_index get_reg_index_of_id_ctrl0(int channel_id)
{
	enum cif_reg_index index;

	switch (channel_id) {
	case 0:
		index = CIF_REG_MIPI_LVDS_ID0_CTRL0;
		break;
	case 1:
		index = CIF_REG_MIPI_LVDS_ID1_CTRL0;
		break;
	case 2:
		index = CIF_REG_MIPI_LVDS_ID2_CTRL0;
		break;
	case 3:
		index = CIF_REG_MIPI_LVDS_ID3_CTRL0;
		break;
	default:
		index = CIF_REG_MIPI_LVDS_ID0_CTRL0;
		break;
	}

	return index;
}

static enum cif_reg_index get_reg_index_of_id_ctrl1(int channel_id)
{
	enum cif_reg_index index;

	switch (channel_id) {
	case 0:
		index = CIF_REG_MIPI_LVDS_ID0_CTRL1;
		break;
	case 1:
		index = CIF_REG_MIPI_LVDS_ID1_CTRL1;
		break;
	case 2:
		index = CIF_REG_MIPI_LVDS_ID2_CTRL1;
		break;
	case 3:
		index = CIF_REG_MIPI_LVDS_ID3_CTRL1;
		break;
	default:
		index = CIF_REG_MIPI_LVDS_ID0_CTRL1;
		break;
	}

	return index;
}

static enum cif_reg_index get_reg_index_of_frm0_y_addr(int channel_id)
{
	enum cif_reg_index index;

	switch (channel_id) {
	case 0:
		index = CIF_REG_MIPI_LVDS_FRAME0_ADDR_Y_ID0;
		break;
	case 1:
		index = CIF_REG_MIPI_LVDS_FRAME0_ADDR_Y_ID1;
		break;
	case 2:
		index = CIF_REG_MIPI_LVDS_FRAME0_ADDR_Y_ID2;
		break;
	case 3:
		index = CIF_REG_MIPI_LVDS_FRAME0_ADDR_Y_ID3;
		break;
	default:
		index = CIF_REG_MIPI_LVDS_FRAME0_ADDR_Y_ID0;
		break;
	}

	return index;
}

static enum cif_reg_index get_reg_index_of_frm1_y_addr(int channel_id)
{
	enum cif_reg_index index;

	switch (channel_id) {
	case 0:
		index = CIF_REG_MIPI_LVDS_FRAME1_ADDR_Y_ID0;
		break;
	case 1:
		index = CIF_REG_MIPI_LVDS_FRAME1_ADDR_Y_ID1;
		break;
	case 2:
		index = CIF_REG_MIPI_LVDS_FRAME1_ADDR_Y_ID2;
		break;
	case 3:
		index = CIF_REG_MIPI_LVDS_FRAME1_ADDR_Y_ID3;
		break;
	default:
		index = CIF_REG_MIPI_LVDS_FRAME1_ADDR_Y_ID0;
		break;
	}

	return index;
}

static enum cif_reg_index get_reg_index_of_frm0_uv_addr(int channel_id)
{
	enum cif_reg_index index;

	switch (channel_id) {
	case 0:
		index = CIF_REG_MIPI_LVDS_FRAME0_ADDR_UV_ID0;
		break;
	case 1:
		index = CIF_REG_MIPI_LVDS_FRAME0_ADDR_UV_ID1;
		break;
	case 2:
		index = CIF_REG_MIPI_LVDS_FRAME0_ADDR_UV_ID2;
		break;
	case 3:
		index = CIF_REG_MIPI_LVDS_FRAME0_ADDR_UV_ID3;
		break;
	default:
		index = CIF_REG_MIPI_LVDS_FRAME0_ADDR_UV_ID0;
		break;
	}

	return index;
}

static enum cif_reg_index get_reg_index_of_frm1_uv_addr(int channel_id)
{
	enum cif_reg_index index;

	switch (channel_id) {
	case 0:
		index = CIF_REG_MIPI_LVDS_FRAME1_ADDR_UV_ID0;
		break;
	case 1:
		index = CIF_REG_MIPI_LVDS_FRAME1_ADDR_UV_ID1;
		break;
	case 2:
		index = CIF_REG_MIPI_LVDS_FRAME1_ADDR_UV_ID2;
		break;
	case 3:
		index = CIF_REG_MIPI_LVDS_FRAME1_ADDR_UV_ID3;
		break;
	default:
		index = CIF_REG_MIPI_LVDS_FRAME1_ADDR_UV_ID0;
		break;
	}

	return index;
}

static enum cif_reg_index get_reg_index_of_frm0_y_vlw(int channel_id)
{
	enum cif_reg_index index;

	switch (channel_id) {
	case 0:
		index = CIF_REG_MIPI_LVDS_FRAME0_VLW_Y_ID0;
		break;
	case 1:
		index = CIF_REG_MIPI_LVDS_FRAME0_VLW_Y_ID1;
		break;
	case 2:
		index = CIF_REG_MIPI_LVDS_FRAME0_VLW_Y_ID2;
		break;
	case 3:
		index = CIF_REG_MIPI_LVDS_FRAME0_VLW_Y_ID3;
		break;
	default:
		index = CIF_REG_MIPI_LVDS_FRAME0_VLW_Y_ID0;
		break;
	}

	return index;
}

static enum cif_reg_index get_reg_index_of_frm1_y_vlw(int channel_id)
{
	enum cif_reg_index index;

	switch (channel_id) {
	case 0:
		index = CIF_REG_MIPI_LVDS_FRAME1_VLW_Y_ID0;
		break;
	case 1:
		index = CIF_REG_MIPI_LVDS_FRAME1_VLW_Y_ID1;
		break;
	case 2:
		index = CIF_REG_MIPI_LVDS_FRAME1_VLW_Y_ID2;
		break;
	case 3:
		index = CIF_REG_MIPI_LVDS_FRAME1_VLW_Y_ID3;
		break;
	default:
		index = CIF_REG_MIPI_LVDS_FRAME1_VLW_Y_ID0;
		break;
	}

	return index;
}

static enum cif_reg_index get_reg_index_of_frm0_uv_vlw(int channel_id)
{
	enum cif_reg_index index;

	switch (channel_id) {
	case 0:
		index = CIF_REG_MIPI_LVDS_FRAME0_VLW_UV_ID0;
		break;
	case 1:
		index = CIF_REG_MIPI_LVDS_FRAME0_VLW_UV_ID1;
		break;
	case 2:
		index = CIF_REG_MIPI_LVDS_FRAME0_VLW_UV_ID2;
		break;
	case 3:
		index = CIF_REG_MIPI_LVDS_FRAME0_VLW_UV_ID3;
		break;
	default:
		index = CIF_REG_MIPI_LVDS_FRAME0_VLW_UV_ID0;
		break;
	}

	return index;
}

static enum cif_reg_index get_reg_index_of_frm1_uv_vlw(int channel_id)
{
	enum cif_reg_index index;

	switch (channel_id) {
	case 0:
		index = CIF_REG_MIPI_LVDS_FRAME1_VLW_UV_ID0;
		break;
	case 1:
		index = CIF_REG_MIPI_LVDS_FRAME1_VLW_UV_ID1;
		break;
	case 2:
		index = CIF_REG_MIPI_LVDS_FRAME1_VLW_UV_ID2;
		break;
	case 3:
		index = CIF_REG_MIPI_LVDS_FRAME1_VLW_UV_ID3;
		break;
	default:
		index = CIF_REG_MIPI_LVDS_FRAME1_VLW_UV_ID0;
		break;
	}

	return index;
}

static enum cif_reg_index get_reg_index_of_id_crop_start(int channel_id)
{
	enum cif_reg_index index;

	switch (channel_id) {
	case 0:
		index = CIF_REG_MIPI_LVDS_ID0_CROP_START;
		break;
	case 1:
		index = CIF_REG_MIPI_LVDS_ID1_CROP_START;
		break;
	case 2:
		index = CIF_REG_MIPI_LVDS_ID2_CROP_START;
		break;
	case 3:
		index = CIF_REG_MIPI_LVDS_ID3_CROP_START;
		break;
	default:
		index = CIF_REG_MIPI_LVDS_ID0_CROP_START;
		break;
	}

	return index;
}

static enum cif_reg_index get_reg_index_of_lvds_sav_eav_act0(int channel_id)
{
	enum cif_reg_index index;

	switch (channel_id) {
	case 0:
		index = CIF_REG_LVDS_SAV_EAV_ACT0_ID0;
		break;
	case 1:
		index = CIF_REG_LVDS_SAV_EAV_ACT0_ID1;
		break;
	case 2:
		index = CIF_REG_LVDS_SAV_EAV_ACT0_ID2;
		break;
	case 3:
		index = CIF_REG_LVDS_SAV_EAV_ACT0_ID3;
		break;
	default:
		index = CIF_REG_LVDS_SAV_EAV_ACT0_ID0;
		break;
	}

	return index;
}

static enum cif_reg_index get_reg_index_of_lvds_sav_eav_act1(int channel_id)
{
	enum cif_reg_index index;

	switch (channel_id) {
	case 0:
		index = CIF_REG_LVDS_SAV_EAV_ACT1_ID0;
		break;
	case 1:
		index = CIF_REG_LVDS_SAV_EAV_ACT1_ID1;
		break;
	case 2:
		index = CIF_REG_LVDS_SAV_EAV_ACT1_ID2;
		break;
	case 3:
		index = CIF_REG_LVDS_SAV_EAV_ACT1_ID3;
		break;
	default:
		index = CIF_REG_LVDS_SAV_EAV_ACT1_ID0;
		break;
	}

	return index;
}

static enum cif_reg_index get_reg_index_of_lvds_sav_eav_blk0(int channel_id)
{
	enum cif_reg_index index;

	switch (channel_id) {
	case 0:
		index = CIF_REG_LVDS_SAV_EAV_BLK0_ID0;
		break;
	case 1:
		index = CIF_REG_LVDS_SAV_EAV_BLK0_ID1;
		break;
	case 2:
		index = CIF_REG_LVDS_SAV_EAV_BLK0_ID2;
		break;
	case 3:
		index = CIF_REG_LVDS_SAV_EAV_BLK0_ID3;
		break;
	default:
		index = CIF_REG_LVDS_SAV_EAV_BLK0_ID0;
		break;
	}

	return index;
}

static enum cif_reg_index get_reg_index_of_lvds_sav_eav_blk1(int channel_id)
{
	enum cif_reg_index index;

	switch (channel_id) {
	case 0:
		index = CIF_REG_LVDS_SAV_EAV_BLK1_ID0;
		break;
	case 1:
		index = CIF_REG_LVDS_SAV_EAV_BLK1_ID1;
		break;
	case 2:
		index = CIF_REG_LVDS_SAV_EAV_BLK1_ID2;
		break;
	case 3:
		index = CIF_REG_LVDS_SAV_EAV_BLK1_ID3;
		break;
	default:
		index = CIF_REG_LVDS_SAV_EAV_BLK1_ID0;
		break;
	}

	return index;
}

/***************************** stream operations ******************************/
static void rkcif_assign_new_buffer_oneframe(struct rkcif_stream *stream,
					     enum rkcif_yuvaddr_state stat)
{
	struct rkcif_dummy_buffer *dummy_buf = &stream->dummy_buf;
	struct rkcif_device *dev = stream->cifdev;
	struct rkcif_buffer *buffer = NULL;
	u32 frm_addr_y, frm_addr_uv;

	spin_lock(&stream->vbq_lock);
	if (stat == RKCIF_YUV_ADDR_STATE_INIT) {
		if (!stream->curr_buf) {
			if (!list_empty(&stream->buf_head)) {
				stream->curr_buf = list_first_entry(&stream->buf_head,
								    struct rkcif_buffer,
								    queue);
				list_del(&stream->curr_buf->queue);
			}
		}

		if (stream->curr_buf) {
			rkcif_write_register(dev, CIF_REG_DVP_FRM0_ADDR_Y,
					     stream->curr_buf->buff_addr[RKCIF_PLANE_Y]);
			rkcif_write_register(dev, CIF_REG_DVP_FRM0_ADDR_UV,
					     stream->curr_buf->buff_addr[RKCIF_PLANE_CBCR]);
		} else {
			rkcif_write_register(dev, CIF_REG_DVP_FRM0_ADDR_Y,
					     dummy_buf->dma_addr);
			rkcif_write_register(dev, CIF_REG_DVP_FRM0_ADDR_UV,
					     dummy_buf->dma_addr);
		}

		if (!stream->next_buf) {
			if (!list_empty(&stream->buf_head)) {
				stream->next_buf = list_first_entry(&stream->buf_head,
								    struct rkcif_buffer, queue);
				list_del(&stream->next_buf->queue);
			}
		}

		if (stream->next_buf) {
			rkcif_write_register(dev, CIF_REG_DVP_FRM1_ADDR_Y,
					     stream->next_buf->buff_addr[RKCIF_PLANE_Y]);
			rkcif_write_register(dev, CIF_REG_DVP_FRM1_ADDR_UV,
					     stream->next_buf->buff_addr[RKCIF_PLANE_CBCR]);
		} else {
			rkcif_write_register(dev, CIF_REG_DVP_FRM1_ADDR_Y,
					     dummy_buf->dma_addr);
			rkcif_write_register(dev, CIF_REG_DVP_FRM1_ADDR_UV,
					     dummy_buf->dma_addr);
		}
	} else if (stat == RKCIF_YUV_ADDR_STATE_UPDATE) {
		if (!list_empty(&stream->buf_head)) {
			if (stream->frame_phase == CIF_CSI_FRAME0_READY) {
				stream->curr_buf = list_first_entry(&stream->buf_head,
								    struct rkcif_buffer, queue);
				list_del(&stream->curr_buf->queue);
				buffer = stream->curr_buf;
			}

			if (stream->frame_phase == CIF_CSI_FRAME1_READY) {
				stream->next_buf = list_first_entry(&stream->buf_head,
								    struct rkcif_buffer, queue);
				list_del(&stream->next_buf->queue);
				buffer = stream->next_buf;
			}
		} else {
			if (stream->frame_phase == CIF_CSI_FRAME0_READY)
				stream->curr_buf = NULL;
			if (stream->frame_phase == CIF_CSI_FRAME1_READY)
				stream->next_buf = NULL;
			buffer = NULL;
		}

		if (stream->frame_phase == CIF_CSI_FRAME0_READY) {
			frm_addr_y = CIF_REG_DVP_FRM0_ADDR_Y;
			frm_addr_uv = CIF_REG_DVP_FRM0_ADDR_UV;
		}

		if (stream->frame_phase == CIF_CSI_FRAME1_READY) {
			frm_addr_y = CIF_REG_DVP_FRM1_ADDR_Y;
			frm_addr_uv = CIF_REG_DVP_FRM1_ADDR_UV;
		}

		if (buffer) {
			rkcif_write_register(dev, frm_addr_y,
					     buffer->buff_addr[RKCIF_PLANE_Y]);
			rkcif_write_register(dev, frm_addr_uv,
					     buffer->buff_addr[RKCIF_PLANE_CBCR]);
		} else {
			rkcif_write_register(dev, frm_addr_y,
					     dummy_buf->dma_addr);
			rkcif_write_register(dev, frm_addr_uv,
					     dummy_buf->dma_addr);
			v4l2_dbg(1, rkcif_debug, &dev->v4l2_dev,
				 "frame Drop to dummy buf\n");
		}
	}
	spin_unlock(&stream->vbq_lock);
}

static void rkcif_assign_new_buffer_pingpong(struct rkcif_stream *stream,
					     int init, int csi_ch)
{
	struct rkcif_dummy_buffer *dummy_buf = &stream->dummy_buf;
	struct rkcif_device *dev = stream->cifdev;
	struct v4l2_mbus_config *mbus_cfg = &dev->active_sensor->mbus;
	struct rkcif_buffer *buffer = NULL;
	u32 frm_addr_y, frm_addr_uv;

	if (init) {
		u32 frm0_addr_y, frm0_addr_uv;
		u32 frm1_addr_y, frm1_addr_uv;

		if (mbus_cfg->type == V4L2_MBUS_CSI2 ||
		    mbus_cfg->type == V4L2_MBUS_CCP2) {
			frm0_addr_y = get_reg_index_of_frm0_y_addr(csi_ch);
			frm0_addr_uv = get_reg_index_of_frm0_uv_addr(csi_ch);
			frm1_addr_y = get_reg_index_of_frm1_y_addr(csi_ch);
			frm1_addr_uv = get_reg_index_of_frm1_uv_addr(csi_ch);
		} else {
			frm0_addr_y = CIF_REG_DVP_FRM0_ADDR_Y;
			frm0_addr_uv = CIF_REG_DVP_FRM0_ADDR_UV;
			frm1_addr_y = CIF_REG_DVP_FRM1_ADDR_Y;
			frm1_addr_uv = CIF_REG_DVP_FRM1_ADDR_UV;
		}

		spin_lock(&stream->vbq_lock);
		if (!stream->curr_buf) {
			if (!list_empty(&stream->buf_head)) {
				stream->curr_buf = list_first_entry(&stream->buf_head,
								    struct rkcif_buffer,
								    queue);
				list_del(&stream->curr_buf->queue);
			}
		}

		if (stream->curr_buf) {
			rkcif_write_register(dev, frm0_addr_y,
					     stream->curr_buf->buff_addr[RKCIF_PLANE_Y]);
			if (dev->chip_id != CHIP_RV1126_CIF_LITE)
				rkcif_write_register(dev, frm0_addr_uv,
						     stream->curr_buf->buff_addr[RKCIF_PLANE_CBCR]);
		} else {
			rkcif_write_register(dev, frm0_addr_y, dummy_buf->dma_addr);
			if (dev->chip_id != CHIP_RV1126_CIF_LITE)
				rkcif_write_register(dev, frm0_addr_uv, dummy_buf->dma_addr);
		}

		if (!stream->next_buf) {
			if (!list_empty(&stream->buf_head)) {
				stream->next_buf = list_first_entry(&stream->buf_head,
								    struct rkcif_buffer, queue);
				list_del(&stream->next_buf->queue);
			}
		}

		if (stream->next_buf) {
			rkcif_write_register(dev, frm1_addr_y,
					     stream->next_buf->buff_addr[RKCIF_PLANE_Y]);
			if (dev->chip_id != CHIP_RV1126_CIF_LITE)
				rkcif_write_register(dev, frm1_addr_uv,
						     stream->next_buf->buff_addr[RKCIF_PLANE_CBCR]);
		} else {
			rkcif_write_register(dev, frm1_addr_y, dummy_buf->dma_addr);
			if (dev->chip_id != CHIP_RV1126_CIF_LITE)
				rkcif_write_register(dev, frm1_addr_uv, dummy_buf->dma_addr);
		}
		spin_unlock(&stream->vbq_lock);
	} else {
		if (mbus_cfg->type == V4L2_MBUS_CSI2 ||
		    mbus_cfg->type == V4L2_MBUS_CCP2) {
			frm_addr_y = stream->frame_phase & CIF_CSI_FRAME1_READY ?
				     get_reg_index_of_frm1_y_addr(csi_ch) :
				     get_reg_index_of_frm0_y_addr(csi_ch);
			frm_addr_uv = stream->frame_phase & CIF_CSI_FRAME1_READY ?
				      get_reg_index_of_frm1_uv_addr(csi_ch) :
				      get_reg_index_of_frm0_uv_addr(csi_ch);
		} else {
			frm_addr_y = stream->frame_phase & CIF_CSI_FRAME1_READY ?
				     CIF_FRM1_ADDR_Y : CIF_FRM0_ADDR_Y;
			frm_addr_uv = stream->frame_phase & CIF_CSI_FRAME1_READY ?
				      CIF_FRM1_ADDR_UV : CIF_FRM0_ADDR_UV;
		}

		spin_lock(&stream->vbq_lock);
		if (!list_empty(&stream->buf_head)) {
			if (stream->frame_phase == CIF_CSI_FRAME0_READY) {
				stream->curr_buf = list_first_entry(&stream->buf_head,
								    struct rkcif_buffer, queue);
				list_del(&stream->curr_buf->queue);
				buffer = stream->curr_buf;
			}

			if (stream->frame_phase == CIF_CSI_FRAME1_READY) {
				stream->next_buf = list_first_entry(&stream->buf_head,
								    struct rkcif_buffer, queue);
				list_del(&stream->next_buf->queue);
				buffer = stream->next_buf;
			}
		} else {
			if (stream->frame_phase == CIF_CSI_FRAME0_READY)
				stream->curr_buf = NULL;
			if (stream->frame_phase == CIF_CSI_FRAME1_READY)
				stream->next_buf = NULL;
			buffer = NULL;
		}
		spin_unlock(&stream->vbq_lock);

		if (buffer) {
			rkcif_write_register(dev, frm_addr_y,
					     buffer->buff_addr[RKCIF_PLANE_Y]);
			if (dev->chip_id != CHIP_RV1126_CIF_LITE)
				rkcif_write_register(dev, frm_addr_uv,
						     buffer->buff_addr[RKCIF_PLANE_CBCR]);
		} else {
			rkcif_write_register(dev, frm_addr_y, dummy_buf->dma_addr);
			if (dev->chip_id != CHIP_RV1126_CIF_LITE)
				rkcif_write_register(dev, frm_addr_uv, dummy_buf->dma_addr);
			v4l2_dbg(1, rkcif_debug, &dev->v4l2_dev,
				 "frame Drop to dummy buf\n");
		}
	}
}

static void rkcif_csi_get_vc_num(struct rkcif_device *dev,
				 unsigned int mbus_flags)
{
	int i, vc_num = 0;

	for (i = 0; i < RKCIF_MAX_CSI_CHANNEL; i++) {
		if (mbus_flags & V4L2_MBUS_CSI2_CHANNEL_0) {
			dev->channels[vc_num].vc = vc_num;
			vc_num++;
			mbus_flags ^= V4L2_MBUS_CSI2_CHANNEL_0;
			continue;
		}
		if (mbus_flags & V4L2_MBUS_CSI2_CHANNEL_1) {
			dev->channels[vc_num].vc = vc_num;
			vc_num++;
			mbus_flags ^= V4L2_MBUS_CSI2_CHANNEL_1;
			continue;
		}

		if (mbus_flags & V4L2_MBUS_CSI2_CHANNEL_2) {
			dev->channels[vc_num].vc = vc_num;
			vc_num++;
			mbus_flags ^= V4L2_MBUS_CSI2_CHANNEL_2;
			continue;
		}

		if (mbus_flags & V4L2_MBUS_CSI2_CHANNEL_3) {
			dev->channels[vc_num].vc = vc_num;
			vc_num++;
			mbus_flags ^= V4L2_MBUS_CSI2_CHANNEL_3;
			continue;
		}
	}

	dev->num_channels = vc_num ? vc_num : 1;
	if (dev->num_channels == 1)
		dev->channels[0].vc = 0;
}

static void rkcif_csi_set_lvds_sav_eav(struct rkcif_stream *stream,
				       struct csi_channel_info *channel)
{
	struct rkcif_device *dev = stream->cifdev;
	struct rkmodule_lvds_cfg *lvds_cfg = &channel->lvds_cfg;
	struct rkmodule_lvds_frame_sync_code *frm_sync_code = NULL;
	struct rkmodule_lvds_frm_sync_code *odd_sync_code = NULL;
	struct rkmodule_lvds_frm_sync_code *even_sync_code = NULL;

	if (dev->hdr.mode == NO_HDR) {
		frm_sync_code = &lvds_cfg->frm_sync_code[LVDS_CODE_GRP_LINEAR];
		odd_sync_code = &frm_sync_code->odd_sync_code;
		even_sync_code = odd_sync_code;
	} else {
		if (channel->id == RKCIF_STREAM_MIPI_ID0)
			frm_sync_code = &lvds_cfg->frm_sync_code[LVDS_CODE_GRP_LONG];
		else if (channel->id == RKCIF_STREAM_MIPI_ID1)
			frm_sync_code = &lvds_cfg->frm_sync_code[LVDS_CODE_GRP_SHORT];
		else if (channel->id == RKCIF_STREAM_MIPI_ID2)
			frm_sync_code = &lvds_cfg->frm_sync_code[LVDS_CODE_GRP_MEDIUM];
		else
			frm_sync_code = &lvds_cfg->frm_sync_code[LVDS_CODE_GRP_LONG];

		odd_sync_code = &frm_sync_code->odd_sync_code;
		even_sync_code = &frm_sync_code->even_sync_code;
	}

	if (odd_sync_code &&  even_sync_code) {
		rkcif_write_register(stream->cifdev,
				     get_reg_index_of_lvds_sav_eav_act0(channel->id),
				     SW_LVDS_EAV_ACT(odd_sync_code->act.eav) |
				     SW_LVDS_SAV_ACT(odd_sync_code->act.sav));

		rkcif_write_register(stream->cifdev,
				     get_reg_index_of_lvds_sav_eav_blk0(channel->id),
				     SW_LVDS_EAV_BLK(odd_sync_code->blk.eav) |
				     SW_LVDS_SAV_BLK(odd_sync_code->blk.sav));

		rkcif_write_register(stream->cifdev,
				     get_reg_index_of_lvds_sav_eav_act1(channel->id),
				     SW_LVDS_EAV_ACT(even_sync_code->act.eav) |
				     SW_LVDS_SAV_ACT(even_sync_code->act.sav));

		rkcif_write_register(stream->cifdev,
				     get_reg_index_of_lvds_sav_eav_blk1(channel->id),
				     SW_LVDS_EAV_BLK(even_sync_code->blk.eav) |
				     SW_LVDS_SAV_BLK(even_sync_code->blk.sav));
	}
}

static int rkcif_csi_channel_init(struct rkcif_stream *stream,
				  struct csi_channel_info *channel)
{
	struct rkcif_device *dev = stream->cifdev;
	const struct cif_output_fmt *fmt;
	u32 fourcc;

	channel->enable = 1;
	channel->width = stream->pixm.width;
	channel->height = stream->pixm.height;
	channel->fmt_val = stream->cif_fmt_in->csi_fmt_val;
	channel->cmd_mode_en = 0; /* default use DSI Video Mode */

	if (stream->crop_enable) {
		channel->crop_en = 1;

		if (channel->fmt_val == CSI_WRDDR_TYPE_RGB888)
			channel->crop_st_x = 3 * stream->crop.left;
		else
			channel->crop_st_x = stream->crop.left;

		channel->crop_st_y = stream->crop.top;
		channel->width = stream->crop.width;
		channel->height = stream->crop.height;
	} else {
		channel->width = stream->pixm.width;
		channel->height = stream->pixm.height;
		channel->crop_en = 0;
	}

	fmt = find_output_fmt(stream, stream->pixm.pixelformat);
	if (!fmt) {
		v4l2_err(&dev->v4l2_dev, "can not find output format: 0x%x",
			 stream->pixm.pixelformat);
		return -EINVAL;
	}

	/*
	 * for mipi or lvds, when enable compact, the virtual width of raw10/raw12
	 * needs aligned with :ALIGN(bits_per_pixel * width / 8, 8), if enable 16bit mode
	 * needs aligned with :ALIGN(bits_per_pixel * width * 2, 8)
	 */
	if (dev->hdr.mode == NO_HDR &&
	    dev->chip_id != CHIP_RV1126_CIF &&
	    dev->chip_id != CHIP_RV1126_CIF_LITE) {
		if (channel->fmt_val == CSI_WRDDR_TYPE_RAW10 ||
		    channel->fmt_val == CSI_WRDDR_TYPE_RAW12)
			channel->virtual_width = ALIGN(channel->width * 2, 8);
		else
			channel->virtual_width = ALIGN(channel->width * fmt->bpp[0] / 8, 8);
	} else {
		channel->virtual_width = ALIGN(channel->width * fmt->raw_bpp / 8, 8);
	}

	if (channel->fmt_val == CSI_WRDDR_TYPE_RGB888)
		channel->width = channel->width * fmt->bpp[0] / 8;
	/*
	 * rk cif don't support output yuyv fmt data
	 * if user request yuyv fmt, the input mode must be RAW8
	 * and the width is double Because the real input fmt is
	 * yuyv
	 */
	fourcc  = stream->cif_fmt_out->fourcc;
	if (fourcc == V4L2_PIX_FMT_YUYV || fourcc == V4L2_PIX_FMT_YVYU ||
	    fourcc == V4L2_PIX_FMT_UYVY || fourcc == V4L2_PIX_FMT_VYUY) {
		channel->fmt_val = CSI_WRDDR_TYPE_RAW8;
		channel->width *= 2;
		channel->virtual_width *= 2;
	}

	channel->data_type = get_data_type(stream->cif_fmt_in->mbus_code,
					   channel->cmd_mode_en);

	return 0;
}

static int rkcif_csi_channel_set(struct rkcif_stream *stream,
				       struct csi_channel_info *channel,
				       enum v4l2_mbus_type mbus_type)
{
	unsigned int val = 0x0;
	struct rkcif_device *dev = stream->cifdev;

	if (channel->id >= 4)
		return -EINVAL;

	if (!channel->enable) {
		rkcif_write_register(dev, get_reg_index_of_id_ctrl0(channel->id),
				     CSI_DISABLE_CAPTURE);
		return 0;
	}

	rkcif_write_register_and(dev, CIF_REG_MIPI_LVDS_INTSTAT,
				 ~(CSI_START_INTSTAT(channel->id) |
				 CSI_DMA_END_INTSTAT(channel->id) |
				 CSI_LINE_INTSTAT(channel->id)));

	/* enable id0 frame start int for sof(long frame, for hdr) */
	if (channel->id == RKCIF_STREAM_MIPI_ID0)
		rkcif_write_register_or(dev, CIF_REG_MIPI_LVDS_INTEN,
					CSI_START_INTEN(channel->id));

	rkcif_write_register(dev, CIF_REG_MIPI_LVDS_LINE_INT_NUM_ID0_1,
			     0x3fff << 16 | 0x3fff);
	rkcif_write_register(dev, CIF_REG_MIPI_LVDS_LINE_INT_NUM_ID2_3,
			     0x3fff << 16 | 0x3fff);

	rkcif_write_register_or(dev, CIF_REG_MIPI_LVDS_INTEN,
				CSI_DMA_END_INTEN(channel->id));

	rkcif_write_register(dev, CIF_REG_MIPI_WATER_LINE, 0x70012);

	val = CIF_MIPI_LVDS_SW_PRESS_VALUE(0x3) |
		CIF_MIPI_LVDS_SW_PRESS_ENABLE |
		CIF_MIPI_LVDS_SW_HURRY_VALUE(0x3) |
		CIF_MIPI_LVDS_SW_HURRY_ENABLE |
		CIF_MIPI_LVDS_SW_WATER_LINE_25 |
		CIF_MIPI_LVDS_SW_WATER_LINE_ENABLE;
	if (mbus_type  == V4L2_MBUS_CSI2) {
		val &= ~CIF_MIPI_LVDS_SW_SEL_LVDS;
	} else if (mbus_type  == V4L2_MBUS_CCP2) {
		if (channel->fmt_val == CSI_WRDDR_TYPE_RAW12)
			val |= CIF_MIPI_LVDS_SW_LVDS_WIDTH_12BITS;
		else if (channel->fmt_val == CSI_WRDDR_TYPE_RAW10)
			val |= CIF_MIPI_LVDS_SW_LVDS_WIDTH_10BITS;
		else
			val |= CIF_MIPI_LVDS_SW_LVDS_WIDTH_8BITS;
		val |= CIF_MIPI_LVDS_SW_SEL_LVDS;
	}
	rkcif_write_register(dev, CIF_REG_MIPI_LVDS_CTRL, val);

	rkcif_write_register_or(dev, CIF_REG_MIPI_LVDS_INTEN,
				CSI_ALL_ERROR_INTEN);

	rkcif_write_register(dev, get_reg_index_of_id_ctrl1(channel->id),
			     channel->width | (channel->height << 16));

	rkcif_write_register(dev, get_reg_index_of_frm0_y_vlw(channel->id),
			     channel->virtual_width);
	rkcif_write_register(dev, get_reg_index_of_frm1_y_vlw(channel->id),
			     channel->virtual_width);
	rkcif_write_register(dev, get_reg_index_of_frm0_uv_vlw(channel->id),
			     channel->virtual_width);
	rkcif_write_register(dev, get_reg_index_of_frm1_uv_vlw(channel->id),
			     channel->virtual_width);

	if (channel->crop_en)
		rkcif_write_register(dev, get_reg_index_of_id_crop_start(channel->id),
				     channel->crop_st_y << 16 | channel->crop_st_x);

	/* Set up an buffer for the next frame */
	rkcif_assign_new_buffer_pingpong(stream, 1, channel->id);

	if (mbus_type  == V4L2_MBUS_CSI2) {
		val = CSI_ENABLE_CAPTURE | channel->fmt_val |
		      channel->cmd_mode_en << 4 | channel->crop_en << 5 |
		      channel->id << 8 | channel->data_type << 10;

		if (dev->chip_id == CHIP_RV1126_CIF ||
		    dev->chip_id == CHIP_RV1126_CIF_LITE)
			val |= CSI_ENABLE_MIPI_COMPACT;
	} else if (mbus_type  == V4L2_MBUS_CCP2) {
		rkcif_csi_set_lvds_sav_eav(stream, channel);
		val = LVDS_ENABLE_CAPTURE | LVDS_MODE(channel->lvds_cfg.mode) |
		      LVDS_MAIN_LANE(0) | LVDS_FID(0) |
		      LVDS_LANES_ENABLED(dev->active_sensor->lanes);

		if (dev->chip_id == CHIP_RV1126_CIF ||
		    dev->chip_id == CHIP_RV1126_CIF_LITE)
			val |= LVDS_COMPACT;
	}

	rkcif_write_register(dev, get_reg_index_of_id_ctrl0(channel->id), val);

	return 0;
}

static int rkcif_csi_stream_start(struct rkcif_stream *stream)
{
	struct rkcif_device *dev = stream->cifdev;
	struct rkcif_sensor_info *active_sensor = dev->active_sensor;
	unsigned int flags = active_sensor->mbus.flags;
	enum v4l2_mbus_type mbus_type = active_sensor->mbus.type;
	struct csi_channel_info *channel;
	u32 ret = 0;

	stream->frame_idx = 0;

	if (mbus_type == V4L2_MBUS_CSI2) {
		rkcif_csi_get_vc_num(dev, flags);

		channel = &dev->channels[stream->id];
		channel->id = stream->id;
		rkcif_csi_channel_init(stream, channel);
		rkcif_csi_channel_set(stream, channel, V4L2_MBUS_CSI2);
	} else if (mbus_type == V4L2_MBUS_CCP2) {
		rkcif_csi_get_vc_num(dev, flags);

		channel = &dev->channels[stream->id];
		channel->id = stream->id;

		ret = v4l2_subdev_call(dev->terminal_sensor, core,
				       ioctl, RKMODULE_GET_LVDS_CFG,
				       &channel->lvds_cfg);
		if (ret) {
			v4l2_err(&dev->v4l2_dev, "Err: get lvds config failed!!\n");
			return ret;
		}

		rkcif_csi_channel_init(stream, channel);
		rkcif_csi_channel_set(stream, channel, V4L2_MBUS_CCP2);
	}

	stream->state = RKCIF_STATE_STREAMING;
	dev->workmode = RKCIF_WORKMODE_PINGPONG;

	return 0;
}

static void rkcif_stream_stop(struct rkcif_stream *stream)
{
	struct rkcif_device *cif_dev = stream->cifdev;
	struct v4l2_mbus_config *mbus_cfg = &cif_dev->active_sensor->mbus;
	u32 val;
	int id;

	if (mbus_cfg->type == V4L2_MBUS_CSI2 ||
	    mbus_cfg->type == V4L2_MBUS_CCP2) {
		id = stream->id;
		val = rkcif_read_register(cif_dev, get_reg_index_of_id_ctrl0(id));
		if (mbus_cfg->type == V4L2_MBUS_CSI2)
			val &= ~CSI_ENABLE_CAPTURE;
		else
			val &= ~LVDS_ENABLE_CAPTURE;

		rkcif_write_register(cif_dev, get_reg_index_of_id_ctrl0(id), val);

		rkcif_write_register_or(cif_dev, CIF_REG_MIPI_LVDS_INTSTAT,
					CSI_START_INTSTAT(id) |
					CSI_DMA_END_INTSTAT(id) |
					CSI_LINE_INTSTAT(id));

		rkcif_write_register_and(cif_dev, CIF_REG_MIPI_LVDS_INTEN,
					 ~(CSI_START_INTEN(id) |
					   CSI_DMA_END_INTEN(id) |
					   CSI_LINE_INTEN(id)));

		rkcif_write_register_and(cif_dev, CIF_REG_MIPI_LVDS_INTEN,
					~CSI_ALL_ERROR_INTEN);

	} else {
		val = rkcif_read_register(cif_dev, CIF_REG_DVP_CTRL);
		rkcif_write_register(cif_dev, CIF_REG_DVP_CTRL,
				     val & (~ENABLE_CAPTURE));
		rkcif_write_register(cif_dev, CIF_REG_DVP_INTEN, 0x0);
		rkcif_write_register(cif_dev, CIF_REG_DVP_INTSTAT, 0x3ff);
		rkcif_write_register(cif_dev, CIF_REG_DVP_FRAME_STATUS, 0x0);
	}

	stream->state = RKCIF_STATE_READY;
}

static int rkcif_queue_setup(struct vb2_queue *queue,
			     unsigned int *num_buffers,
			     unsigned int *num_planes,
			     unsigned int sizes[],
			     struct device *alloc_ctxs[])
{
	struct rkcif_stream *stream = queue->drv_priv;
	struct rkcif_device *dev = stream->cifdev;
	const struct v4l2_pix_format_mplane *pixm = NULL;
	const struct cif_output_fmt *cif_fmt;
	u32 i, height;

	pixm = &stream->pixm;
	cif_fmt = stream->cif_fmt_out;

	*num_planes = cif_fmt->mplanes;

	if (stream->crop_enable)
		height = stream->crop.height;
	else
		height = pixm->height;

	for (i = 0; i < cif_fmt->mplanes; i++) {
		const struct v4l2_plane_pix_format *plane_fmt;
		int h = round_up(height, MEMORY_ALIGN_ROUND_UP_HEIGHT);

		plane_fmt = &pixm->plane_fmt[i];
		sizes[i] = plane_fmt->sizeimage / height * h;
	}

	v4l2_dbg(1, rkcif_debug, &dev->v4l2_dev, "%s count %d, size %d\n",
		 v4l2_type_names[queue->type], *num_buffers, sizes[0]);

	return 0;
}

/*
 * The vb2_buffer are stored in rkcif_buffer, in order to unify
 * mplane buffer and none-mplane buffer.
 */
static void rkcif_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct rkcif_buffer *cifbuf = to_rkcif_buffer(vbuf);
	struct vb2_queue *queue = vb->vb2_queue;
	struct rkcif_stream *stream = queue->drv_priv;
	struct v4l2_pix_format_mplane *pixm = &stream->pixm;
	const struct cif_output_fmt *fmt = stream->cif_fmt_out;
	unsigned long lock_flags = 0;
	int i;

	memset(cifbuf->buff_addr, 0, sizeof(cifbuf->buff_addr));
	/* If mplanes > 1, every c-plane has its own m-plane,
	 * otherwise, multiple c-planes are in the same m-plane
	 */
	for (i = 0; i < fmt->mplanes; i++) {
		void *addr = vb2_plane_vaddr(vb, i);

		cifbuf->buff_addr[i] = vb2_dma_contig_plane_dma_addr(vb, i);
		if (rkcif_debug && addr && !stream->cifdev->iommu_en) {
			memset(addr, 0, pixm->plane_fmt[i].sizeimage);
			v4l2_dbg(1, rkcif_debug, &stream->cifdev->v4l2_dev,
				 "Clear buffer, size: 0x%08x\n",
				 pixm->plane_fmt[i].sizeimage);
		}
	}

	if (fmt->mplanes == 1) {
		for (i = 0; i < fmt->cplanes - 1; i++)
			cifbuf->buff_addr[i + 1] = cifbuf->buff_addr[i] +
				pixm->plane_fmt[i].bytesperline * pixm->height;
	}

	spin_lock_irqsave(&stream->vbq_lock, lock_flags);
	list_add_tail(&cifbuf->queue, &stream->buf_head);
	spin_unlock_irqrestore(&stream->vbq_lock, lock_flags);
}

static int rkcif_create_dummy_buf(struct rkcif_stream *stream)
{
	u32 fourcc;
	struct rkcif_dummy_buffer *dummy_buf = &stream->dummy_buf;
	struct rkcif_device *dev = stream->cifdev;

	/* get a maximum plane size */
	dummy_buf->size = max3(stream->pixm.plane_fmt[0].bytesperline *
		stream->pixm.height,
		stream->pixm.plane_fmt[1].sizeimage,
		stream->pixm.plane_fmt[2].sizeimage);
	/*
	 * rk cif don't support output yuyv fmt data
	 * if user request yuyv fmt, the input mode must be RAW8
	 * and the width is double Because the real input fmt is
	 * yuyv
	 */
	fourcc  = stream->cif_fmt_out->fourcc;
	if (fourcc == V4L2_PIX_FMT_YUYV || fourcc == V4L2_PIX_FMT_YVYU ||
	    fourcc == V4L2_PIX_FMT_UYVY || fourcc == V4L2_PIX_FMT_VYUY)
		dummy_buf->size *= 2;

	dummy_buf->vaddr = dma_alloc_coherent(dev->dev, dummy_buf->size,
					      &dummy_buf->dma_addr,
					      GFP_KERNEL);
	if (!dummy_buf->vaddr) {
		v4l2_err(&dev->v4l2_dev,
			 "Failed to allocate the memory for dummy buffer\n");
		return -ENOMEM;
	}

	v4l2_info(&dev->v4l2_dev, "Allocate dummy buffer, size: 0x%08x\n",
		  dummy_buf->size);

	return 0;
}

static void rkcif_destroy_dummy_buf(struct rkcif_stream *stream)
{
	struct rkcif_dummy_buffer *dummy_buf = &stream->dummy_buf;
	struct rkcif_device *dev = stream->cifdev;

	if (dummy_buf->vaddr)
		dma_free_coherent(dev->dev, dummy_buf->size,
				  dummy_buf->vaddr, dummy_buf->dma_addr);
}

static void rkcif_stop_streaming(struct vb2_queue *queue)
{
	struct rkcif_stream *stream = queue->drv_priv;
	struct rkcif_vdev_node *node = &stream->vnode;
	struct rkcif_device *dev = stream->cifdev;
	struct v4l2_device *v4l2_dev = &dev->v4l2_dev;
	struct rkcif_buffer *buf = NULL;
	unsigned int val;
	int ret;

	mutex_lock(&dev->stream_lock);

	stream->stopping = true;

	ret = wait_event_timeout(stream->wq_stopped,
				 stream->state != RKCIF_STATE_STREAMING,
				 msecs_to_jiffies(1000));
	if (!ret) {
		rkcif_stream_stop(stream);
		stream->stopping = false;
	}

	media_pipeline_stop(&node->vdev.entity);
	ret = dev->pipe.set_stream(&dev->pipe, false);
	if (ret < 0)
		v4l2_err(v4l2_dev, "pipeline stream-off failed error:%d\n",
			 ret);

	/* release buffers */
	if (stream->curr_buf)
		list_add_tail(&stream->curr_buf->queue, &stream->buf_head);

	if (stream->next_buf &&
	    stream->next_buf != stream->curr_buf)
		list_add_tail(&stream->next_buf->queue, &stream->buf_head);

	if (dev->hdr.mode != NO_HDR) {
		unsigned int index;
		struct rkcif_buffer *tmp_buf;
		bool has_added = false;
		for (index = 0; index < RDBK_MAX; index++) {
			if (dev->rdbk_buf[index]) {
				if (dev->rdbk_buf[index] != stream->curr_buf &&
				    dev->rdbk_buf[index] != stream->next_buf) {
					list_for_each_entry(tmp_buf, &stream->buf_head, queue) {
						if (tmp_buf == dev->rdbk_buf[index]) {
							has_added = true;
							break;
						}
					}
					if (!has_added)
						list_add_tail(&dev->rdbk_buf[index]->queue,
							      &stream->buf_head);
				}
				dev->rdbk_buf[index] = NULL;
			}
		}
	}

	stream->curr_buf = NULL;
	stream->next_buf = NULL;

	while (!list_empty(&stream->buf_head)) {
		buf = list_first_entry(&stream->buf_head,
				       struct rkcif_buffer, queue);
		list_del(&buf->queue);
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
	}

	rkcif_destroy_dummy_buf(stream);

	ret = dev->pipe.close(&dev->pipe);
	if (ret < 0)
		v4l2_err(v4l2_dev, "pipeline close failed error:%d\n", ret);

	if (dev->hdr.mode == HDR_X2) {
		if (dev->stream[RKCIF_STREAM_MIPI_ID0].state == RKCIF_STATE_READY &&
		    dev->stream[RKCIF_STREAM_MIPI_ID1].state == RKCIF_STATE_READY) {
			dev->can_be_reset = true;
		}
	} else if (dev->hdr.mode == HDR_X3) {
		if (dev->stream[RKCIF_STREAM_MIPI_ID0].state == RKCIF_STATE_READY &&
		    dev->stream[RKCIF_STREAM_MIPI_ID1].state == RKCIF_STATE_READY &&
		    dev->stream[RKCIF_STREAM_MIPI_ID2].state == RKCIF_STATE_READY) {
			dev->can_be_reset = true;
		}
	} else {
		dev->can_be_reset = true;
	}

	if (dev->can_be_reset) {
		if (dev->hdr.mode != NO_HDR) {
			rkcif_stop_luma(&dev->luma_vdev);
			val = rkcif_read_register(dev, CIF_REG_MIPI_LVDS_CTRL);
			val |= CIF_MIPI_LVDS_SW_DMA_IDLE;
			rkcif_write_register(dev, CIF_REG_MIPI_LVDS_CTRL, val);
			usleep_range(5, 10);
			rkcif_soft_reset(dev, false);
		} else {
			rkcif_soft_reset(dev, false);
		}
		dev->can_be_reset = false;
	}
	pm_runtime_put(dev->dev);

	mutex_unlock(&dev->stream_lock);
}

/*
 * CIF supports the following input modes,
 *    YUV, the default mode
 *    PAL,
 *    NTSC,
 *    RAW, if the input format is raw bayer
 *    JPEG, TODO
 *    MIPI, TODO
 */
static u32 rkcif_determine_input_mode(struct rkcif_stream *stream)
{
	struct rkcif_device *dev = stream->cifdev;
	struct rkcif_sensor_info *sensor_info = dev->active_sensor;
	v4l2_std_id std;
	u32 mode = INPUT_MODE_YUV;
	int ret;

	ret = v4l2_subdev_call(sensor_info->sd, video, querystd, &std);
	if (ret == 0) {
		/* retrieve std from sensor if exist */
		switch (std) {
		case V4L2_STD_NTSC:
			mode = INPUT_MODE_NTSC;
			break;
		case V4L2_STD_PAL:
			mode = INPUT_MODE_PAL;
			break;
		case V4L2_STD_ATSC:
			mode = INPUT_MODE_BT1120;
			break;
		default:
			v4l2_err(&dev->v4l2_dev,
				 "std: %lld is not supported", std);
		}
	} else {
		/* determine input mode by mbus_code (fmt_type) */
		switch (stream->cif_fmt_in->fmt_type) {
		case CIF_FMT_TYPE_YUV:
			mode = INPUT_MODE_YUV;
			break;
		case CIF_FMT_TYPE_RAW:
			mode = INPUT_MODE_RAW;
			break;
		}
	}

	return mode;
}

static inline u32 rkcif_scl_ctl(struct rkcif_stream *stream)
{
	u32 fmt_type = stream->cif_fmt_in->fmt_type;

	return (fmt_type == CIF_FMT_TYPE_YUV) ?
		ENABLE_YUV_16BIT_BYPASS : ENABLE_RAW_16BIT_BYPASS;
}

/**
 * rkcif_align_bits_per_pixel() - return the bit width of per pixel for stored
 * In raw or jpeg mode, data is stored by 16-bits,so need to align it.
 */
static u32 rkcif_align_bits_per_pixel(const struct cif_output_fmt *fmt,
				      int plane_index)
{
	u32 bpp = 0, i;

	if (fmt) {
		switch (fmt->fourcc) {
		case V4L2_PIX_FMT_NV16:
		case V4L2_PIX_FMT_NV61:
		case V4L2_PIX_FMT_NV12:
		case V4L2_PIX_FMT_NV21:
		case V4L2_PIX_FMT_YUYV:
		case V4L2_PIX_FMT_YVYU:
		case V4L2_PIX_FMT_UYVY:
		case V4L2_PIX_FMT_VYUY:
		case V4L2_PIX_FMT_Y16:
			bpp = fmt->bpp[plane_index];
			break;
		case V4L2_PIX_FMT_RGB24:
		case V4L2_PIX_FMT_RGB565:
		case V4L2_PIX_FMT_BGR666:
		case V4L2_PIX_FMT_SRGGB8:
		case V4L2_PIX_FMT_SGRBG8:
		case V4L2_PIX_FMT_SGBRG8:
		case V4L2_PIX_FMT_SRGGB10:
		case V4L2_PIX_FMT_SGRBG10:
		case V4L2_PIX_FMT_SGBRG10:
		case V4L2_PIX_FMT_SBGGR10:
		case V4L2_PIX_FMT_SRGGB12:
		case V4L2_PIX_FMT_SGRBG12:
		case V4L2_PIX_FMT_SGBRG12:
		case V4L2_PIX_FMT_SBGGR12:
		case V4L2_PIX_FMT_SBGGR16:
			bpp = max(fmt->bpp[plane_index], (u8)CIF_RAW_STORED_BIT_WIDTH);
			for (i = 1; i < 5; i++) {
				if (i * CIF_RAW_STORED_BIT_WIDTH >= bpp) {
					bpp = i * CIF_RAW_STORED_BIT_WIDTH;
					break;
				}
			}
			break;
		default:
			pr_err("fourcc: %d is not supported!\n", fmt->fourcc);
			break;
		}
	}

	return bpp;
}

/**
 * rkcif_cal_raw_vir_line_ratio() - return ratio for virtual line width setting
 * In raw or jpeg mode, data is stored by 16-bits,
 * so need to align virtual line width.
 */
static u32 rkcif_cal_raw_vir_line_ratio(const struct cif_output_fmt *fmt)
{
	u32 ratio = 0, bpp = 0;

	if (fmt) {
		bpp = rkcif_align_bits_per_pixel(fmt, 0);
		ratio = bpp / CIF_YUV_STORED_BIT_WIDTH;
	}

	return ratio;
}

static int rkcif_stream_start(struct rkcif_stream *stream)
{
	u32 val, mbus_flags, href_pol, vsync_pol,
	    xfer_mode = 0, yc_swap = 0, skip_top = 0,
	    inputmode = 0, mipimode = 0, workmode = 0;
	struct rkcif_device *dev = stream->cifdev;
	struct rkcif_sensor_info *sensor_info;
	const struct cif_output_fmt *fmt;

	sensor_info = dev->active_sensor;
	stream->frame_idx = 0;

	mbus_flags = sensor_info->mbus.flags;
	href_pol = (mbus_flags & V4L2_MBUS_HSYNC_ACTIVE_HIGH) ?
		    HSY_HIGH_ACTIVE : HSY_LOW_ACTIVE;
	vsync_pol = (mbus_flags & V4L2_MBUS_VSYNC_ACTIVE_HIGH) ?
		     VSY_HIGH_ACTIVE : VSY_LOW_ACTIVE;

	if (rkcif_determine_input_mode(stream) == INPUT_MODE_BT1120) {
		if (stream->cif_fmt_in->field == V4L2_FIELD_NONE)
			xfer_mode = BT1120_TRANSMIT_PROGRESS;
		else
			xfer_mode = BT1120_TRANSMIT_INTERFACE;
		if (!CIF_FETCH_IS_Y_FIRST(stream->cif_fmt_in->dvp_fmt_val))
			yc_swap = BT1120_YC_SWAP;
	}

	if (dev->active_sensor->mbus.type == V4L2_MBUS_CSI2) {
		inputmode = INPUT_MODE_MIPI;

		/* if cif is linked with mipi,
		 * href pol must be set as high active,
		 * vsyn pol must be set as low active.
		 */
		href_pol = HSY_HIGH_ACTIVE;
		vsync_pol = VSY_LOW_ACTIVE;

		if (stream->cif_fmt_in->fmt_type == CIF_FMT_TYPE_YUV)
			mipimode = MIPI_MODE_YUV;
		else if (stream->cif_fmt_in->fmt_type == CIF_FMT_TYPE_RAW)
			mipimode = MIPI_MODE_RGB;
		else
			mipimode = MIPI_MODE_32BITS_BYPASS;
	} else {
		inputmode = rkcif_determine_input_mode(stream);
	}

	val = vsync_pol | href_pol | inputmode | mipimode
	      | stream->cif_fmt_out->fmt_val
	      | stream->cif_fmt_in->dvp_fmt_val
	      | xfer_mode | yc_swap;
	rkcif_write_register(dev, CIF_REG_DVP_FOR, val);

	val = stream->pixm.width;
	if (stream->cif_fmt_in->fmt_type == CIF_FMT_TYPE_RAW) {
		fmt = find_output_fmt(stream, stream->pixm.pixelformat);
		val = stream->pixm.width * rkcif_cal_raw_vir_line_ratio(fmt);
	}
	rkcif_write_register(dev, CIF_REG_DVP_VIR_LINE_WIDTH, val);
	rkcif_write_register(dev, CIF_REG_DVP_SET_SIZE,
			     stream->pixm.width | (stream->pixm.height << 16));

	v4l2_subdev_call(sensor_info->sd, sensor, g_skip_top_lines, &skip_top);

	/* TODO: set crop properly */
	rkcif_write_register(dev, CIF_REG_DVP_CROP, skip_top << CIF_CROP_Y_SHIFT);
	rkcif_write_register(dev, CIF_REG_DVP_FRAME_STATUS, FRAME_STAT_CLS);
	rkcif_write_register(dev, CIF_REG_DVP_INTSTAT, INTSTAT_CLS);
	rkcif_write_register(dev, CIF_REG_DVP_SCL_CTRL, rkcif_scl_ctl(stream));

	if ((dev->chip_id == CHIP_RK1808_CIF ||
	     dev->chip_id == CHIP_RV1126_CIF) &&
	    rkcif_determine_input_mode(stream) == INPUT_MODE_BT1120)
		rkcif_assign_new_buffer_pingpong(stream, 1, 0);
	else
		/* Set up an buffer for the next frame */
		rkcif_assign_new_buffer_oneframe(stream,
						 RKCIF_YUV_ADDR_STATE_INIT);

	rkcif_write_register(dev, CIF_REG_DVP_INTEN,
			     FRAME_END_EN | PST_INF_FRAME_END);

	if (dev->workmode == RKCIF_WORKMODE_ONEFRAME)
		workmode = MODE_ONEFRAME;
	else if (dev->workmode == RKCIF_WORKMODE_PINGPONG)
		workmode = MODE_PINGPONG;
	else
		workmode = MODE_LINELOOP;

	if ((dev->chip_id == CHIP_RK1808_CIF ||
	     dev->chip_id == CHIP_RV1126_CIF) &&
	    rkcif_determine_input_mode(stream) == INPUT_MODE_BT1120) {
		dev->workmode = RKCIF_WORKMODE_PINGPONG;
		rkcif_write_register(dev, CIF_REG_DVP_CTRL,
				     AXI_BURST_16 | MODE_PINGPONG | ENABLE_CAPTURE);
	} else {
		rkcif_write_register(dev, CIF_REG_DVP_CTRL,
				     AXI_BURST_16 | workmode | ENABLE_CAPTURE);
	}

	stream->state = RKCIF_STATE_STREAMING;

	return 0;
}

static int rkcif_sanity_check_fmt(struct rkcif_stream *stream,
				  const struct v4l2_rect *s_crop)
{
	struct rkcif_device *dev = stream->cifdev;
	struct v4l2_device *v4l2_dev = &dev->v4l2_dev;
	struct v4l2_rect input, *crop;

	stream->cif_fmt_in = get_input_fmt(dev->active_sensor->sd,
					   &input, stream->id + 1);
	if (!stream->cif_fmt_in) {
		v4l2_err(v4l2_dev, "Input fmt is invalid\n");
		return -EINVAL;
	}

	if (s_crop)
		crop = (struct v4l2_rect *)s_crop;
	else
		crop = &stream->crop;

	if (crop->width + crop->left > input.width ||
	    crop->height + crop->top > input.height) {
		v4l2_err(v4l2_dev, "crop size is bigger than input\n");
		return -EINVAL;
	}

	if (dev->active_sensor->mbus.type == V4L2_MBUS_CSI2) {
		if (crop->left > 0) {
			int align_x = get_csi_crop_align(stream->cif_fmt_in);

			if (align_x > 0 && crop->left % align_x != 0) {
				v4l2_err(v4l2_dev,
					 "ERROR: crop left must align %d\n",
					 align_x);
				return -EINVAL;
			}
		}
	} else if (dev->active_sensor->mbus.type == V4L2_MBUS_CCP2) {
		if (crop->left % 4 != 0 && crop->width % 4 != 0) {
			v4l2_err(v4l2_dev,
				 "ERROR: lvds crop left and width must align %d\n", 4);
			return -EINVAL;
		}
	}

	return 0;
}

static int rkcif_start_streaming(struct vb2_queue *queue, unsigned int count)
{
	struct rkcif_stream *stream = queue->drv_priv;
	struct rkcif_vdev_node *node = &stream->vnode;
	struct rkcif_device *dev = stream->cifdev;
	struct v4l2_device *v4l2_dev = &dev->v4l2_dev;
	struct rkcif_sensor_info *sensor_info = dev->active_sensor;
	struct v4l2_subdev_selection input_sel;
	struct rkmodule_hdr_cfg hdr_cfg;
	/* struct v4l2_subdev *sd; */
	int ret;

	mutex_lock(&dev->stream_lock);

	if (WARN_ON(stream->state != RKCIF_STATE_READY)) {
		ret = -EBUSY;
		v4l2_err(v4l2_dev, "stream in busy state\n");
		goto destroy_buf;
	}

	if (dev->active_sensor) {
		ret = rkcif_update_sensor_info(stream);
		if (ret < 0) {
			v4l2_err(v4l2_dev,
				 "update sensor info failed %d\n",
				 ret);
			goto out;
		}
	}

	if (dev->terminal_sensor) {
		ret = v4l2_subdev_call(dev->terminal_sensor,
				       core, ioctl,
				       RKMODULE_GET_HDR_CFG,
				       &hdr_cfg);
		if (!ret)
			dev->hdr.mode = hdr_cfg.hdr_mode;
		else
			dev->hdr.mode = NO_HDR;

		input_sel.target = V4L2_SEL_TGT_CROP_BOUNDS;
		ret = v4l2_subdev_call(dev->terminal_sensor,
				       pad, get_selection, NULL,
				       &input_sel);
		if (!ret) {
			stream->crop = input_sel.r;
			stream->crop_enable = true;
		}
	}

	ret = rkcif_sanity_check_fmt(stream, NULL);
	if (ret < 0)
		goto destroy_buf;

	ret = rkcif_create_dummy_buf(stream);
	if (ret < 0) {
		v4l2_err(v4l2_dev, "Failed to create dummy_buf, %d\n", ret);
		goto destroy_buf;
	}

	/* enable clocks/power-domains */
	ret = pm_runtime_get_sync(dev->dev);
	if (ret < 0) {
		v4l2_err(v4l2_dev, "Failed to get runtime pm, %d\n",
			 ret);
		goto  destroy_buf;
	}

	ret = dev->pipe.open(&dev->pipe, &node->vdev.entity, true);
	if (ret < 0) {
		v4l2_err(v4l2_dev, "open cif pipeline failed %d\n",
			 ret);
		goto destroy_buf;
	}

	/*
	 * start sub-devices
	 * When use bt601, the sampling edge of cif is random,
	 * can be rising or fallling after powering on cif.
	 * To keep the coherence of edge, open sensor in advance.
	 */
	if (sensor_info->mbus.type == V4L2_MBUS_PARALLEL) {
		ret = dev->pipe.set_stream(&dev->pipe, true);
		if (ret < 0)
			goto runtime_put;
	}

	if (dev->chip_id == CHIP_RK1808_CIF ||
	    dev->chip_id == CHIP_RV1126_CIF ||
	    dev->chip_id == CHIP_RV1126_CIF_LITE) {
		if (dev->active_sensor->mbus.type == V4L2_MBUS_CSI2 ||
		    dev->active_sensor->mbus.type == V4L2_MBUS_CCP2)
			ret = rkcif_csi_stream_start(stream);
		else
			ret = rkcif_stream_start(stream);
	} else {
		ret = rkcif_stream_start(stream);
	}

	if (ret < 0)
		goto runtime_put;

	ret = media_pipeline_start(&node->vdev.entity, &dev->pipe.pipe);
	if (ret < 0) {
		v4l2_err(&dev->v4l2_dev, "start pipeline failed %d\n",
			 ret);
		goto pipe_stream_off;
	}

	if (sensor_info->mbus.type != V4L2_MBUS_PARALLEL) {
		ret = dev->pipe.set_stream(&dev->pipe, true);
		if (ret < 0)
			goto stop_stream;
	}

	if (dev->hdr.mode == HDR_X2) {
		if (dev->stream[RKCIF_STREAM_MIPI_ID0].state == RKCIF_STATE_STREAMING &&
		    dev->stream[RKCIF_STREAM_MIPI_ID1].state == RKCIF_STATE_STREAMING) {
			rkcif_start_luma(&dev->luma_vdev,
				dev->stream[RKCIF_STREAM_MIPI_ID0].cif_fmt_in);
		}
	} else if (dev->hdr.mode == HDR_X3) {
		if (dev->stream[RKCIF_STREAM_MIPI_ID0].state == RKCIF_STATE_STREAMING &&
		    dev->stream[RKCIF_STREAM_MIPI_ID1].state == RKCIF_STATE_STREAMING &&
		    dev->stream[RKCIF_STREAM_MIPI_ID2].state == RKCIF_STATE_STREAMING) {
			rkcif_start_luma(&dev->luma_vdev,
				dev->stream[RKCIF_STREAM_MIPI_ID0].cif_fmt_in);
		}
	}

	v4l2_info(&dev->v4l2_dev, "%s successfully!\n", __func__);
	goto out;

stop_stream:
	rkcif_stream_stop(stream);
pipe_stream_off:
	dev->pipe.set_stream(&dev->pipe, false);
runtime_put:
	pm_runtime_put(dev->dev);
destroy_buf:
	if (stream->next_buf)
		vb2_buffer_done(&stream->next_buf->vb.vb2_buf,
				VB2_BUF_STATE_QUEUED);
	if (stream->curr_buf)
		vb2_buffer_done(&stream->curr_buf->vb.vb2_buf,
				VB2_BUF_STATE_QUEUED);
	while (!list_empty(&stream->buf_head)) {
		struct rkcif_buffer *buf;

		buf = list_first_entry(&stream->buf_head,
				       struct rkcif_buffer, queue);
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_QUEUED);
		list_del(&buf->queue);
	}

	rkcif_destroy_dummy_buf(stream);
out:
	mutex_unlock(&dev->stream_lock);
	return ret;
}

static struct vb2_ops rkcif_vb2_ops = {
	.queue_setup = rkcif_queue_setup,
	.buf_queue = rkcif_buf_queue,
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
	.stop_streaming = rkcif_stop_streaming,
	.start_streaming = rkcif_start_streaming,
};

static int rkcif_init_vb2_queue(struct vb2_queue *q,
				struct rkcif_stream *stream,
				enum v4l2_buf_type buf_type)
{
	q->type = buf_type;
	q->io_modes = VB2_MMAP | VB2_DMABUF;
	q->drv_priv = stream;
	q->ops = &rkcif_vb2_ops;
	q->mem_ops = &vb2_dma_contig_memops;
	q->buf_struct_size = sizeof(struct rkcif_buffer);
	q->min_buffers_needed = CIF_REQ_BUFS_MIN;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->lock = &stream->vnode.vlock;
	q->dev = stream->cifdev->dev;

	return vb2_queue_init(q);
}

static void rkcif_set_fmt(struct rkcif_stream *stream,
			  struct v4l2_pix_format_mplane *pixm,
			  bool try)
{
	struct rkcif_device *dev = stream->cifdev;
	const struct cif_output_fmt *fmt;
	struct v4l2_rect input_rect;
	struct v4l2_subdev_selection input_sel;
	unsigned int imagesize = 0, planes;
	u32 xsubs = 1, ysubs = 1, i;
	struct rkmodule_hdr_cfg hdr_cfg;
	int ret;

	fmt = find_output_fmt(stream, pixm->pixelformat);
	if (!fmt)
		fmt = &out_fmts[0];

	input_rect.width = RKCIF_DEFAULT_WIDTH;
	input_rect.height = RKCIF_DEFAULT_HEIGHT;

	if (dev->active_sensor && dev->active_sensor->sd)
		get_input_fmt(dev->active_sensor->sd,
			      &input_rect, stream->id + 1);

	if (dev->terminal_sensor) {
		ret = v4l2_subdev_call(dev->terminal_sensor,
				       core, ioctl,
				       RKMODULE_GET_HDR_CFG,
				       &hdr_cfg);
		if (!ret)
			dev->hdr.mode = hdr_cfg.hdr_mode;
		else
			dev->hdr.mode = NO_HDR;
	}

	/* CIF has not scale function,
	 * the size should not be larger than input
	 */
	pixm->width = clamp_t(u32, pixm->width,
			      CIF_MIN_WIDTH, input_rect.width);
	pixm->height = clamp_t(u32, pixm->height,
			       CIF_MIN_HEIGHT, input_rect.height);
	pixm->num_planes = fmt->mplanes;
	pixm->field = V4L2_FIELD_NONE;
	pixm->quantization = V4L2_QUANTIZATION_DEFAULT;

	if (dev->terminal_sensor) {
		input_sel.target = V4L2_SEL_TGT_CROP_BOUNDS;

		ret = v4l2_subdev_call(dev->terminal_sensor,
				       pad, get_selection, NULL,
				       &input_sel);
		if (!ret) {
			stream->crop = input_sel.r;
			stream->crop_enable = true;
		}
	}

	/* calculate plane size and image size */
	fcc_xysubs(fmt->fourcc, &xsubs, &ysubs);

	planes = fmt->cplanes ? fmt->cplanes : fmt->mplanes;

	for (i = 0; i < planes; i++) {
		struct v4l2_plane_pix_format *plane_fmt;
		int width, height, bpl, size, bpp;

		if (i == 0) {
			if (stream->crop_enable) {
				width = stream->crop.width;
				height = stream->crop.height;
			} else {
				width = pixm->width;
				height = pixm->height;
			}
		} else {
			if (stream->crop_enable) {
				width = stream->crop.width / xsubs;
				height = stream->crop.height / ysubs;
			} else {
				width = pixm->width / xsubs;
				height = pixm->height / ysubs;
			}
		}

		if (dev->hdr.mode == NO_HDR) {
			bpp = rkcif_align_bits_per_pixel(fmt, i);
			bpl = width * bpp / CIF_YUV_STORED_BIT_WIDTH;
		} else {
			/* compact mode need bytesperline 4byte align */
			bpl = ALIGN(width * fmt->raw_bpp / 8, 4);
		}
		size = bpl * height;
		imagesize += size;

		if (fmt->mplanes > i) {
			/* Set bpl and size for each mplane */
			plane_fmt = pixm->plane_fmt + i;
			plane_fmt->bytesperline = bpl;
			plane_fmt->sizeimage = size;
		}
		v4l2_dbg(1, rkcif_debug, &stream->cifdev->v4l2_dev,
			 "C-Plane %i size: %d, Total imagesize: %d\n",
			 i, size, imagesize);
	}

	/* convert to non-MPLANE format.
	 * It's important since we want to unify non-MPLANE
	 * and MPLANE.
	 */
	if (fmt->mplanes == 1)
		pixm->plane_fmt[0].sizeimage = imagesize;

	if (!try) {
		stream->cif_fmt_out = fmt;
		stream->pixm = *pixm;

		v4l2_dbg(1, rkcif_debug, &stream->cifdev->v4l2_dev,
			 "%s: req(%d, %d) out(%d, %d)\n", __func__,
			 pixm->width, pixm->height,
			 stream->pixm.width, stream->pixm.height);
	}
}

void rkcif_stream_init(struct rkcif_device *dev, u32 id)
{
	struct rkcif_stream *stream = &dev->stream[id];
	struct v4l2_pix_format_mplane pixm;

	memset(stream, 0, sizeof(*stream));
	memset(&pixm, 0, sizeof(pixm));
	stream->id = id;
	stream->cifdev = dev;

	INIT_LIST_HEAD(&stream->buf_head);
	spin_lock_init(&stream->vbq_lock);
	stream->state = RKCIF_STATE_READY;
	init_waitqueue_head(&stream->wq_stopped);

	/* Set default format */
	pixm.pixelformat = V4L2_PIX_FMT_NV12;
	pixm.width = RKCIF_DEFAULT_WIDTH;
	pixm.height = RKCIF_DEFAULT_HEIGHT;
	rkcif_set_fmt(stream, &pixm, false);

	stream->crop.left = 0;
	stream->crop.top = 0;
	stream->crop.width = RKCIF_DEFAULT_WIDTH;
	stream->crop.height = RKCIF_DEFAULT_HEIGHT;
	stream->crop_enable = false;
}

static int rkcif_fh_open(struct file *filp)
{
	struct video_device *vdev = video_devdata(filp);
	struct rkcif_vdev_node *vnode = vdev_to_node(vdev);
	struct rkcif_stream *stream = to_rkcif_stream(vnode);
	struct rkcif_device *cifdev = stream->cifdev;
	int ret;

	/* Make sure active sensor is valid before .set_fmt() */
	ret = rkcif_update_sensor_info(stream);
	if (ret < 0) {
		v4l2_err(vdev,
			 "update sensor info failed %d\n",
			 ret);
		return ret;
	}

	/*
	 * Soft reset via CRU.
	 * Because CRU would reset iommu too, so there's not chance
	 * to reset cif once we hold buffers after buf queued
	 */
	if (cifdev->chip_id == CHIP_RK1808_CIF ||
	    cifdev->chip_id == CHIP_RV1126_CIF ||
	    cifdev->chip_id == CHIP_RV1126_CIF_LITE) {
		mutex_lock(&cifdev->stream_lock);
		if (!atomic_read(&cifdev->fh_cnt))
			rkcif_soft_reset(cifdev, true);
		atomic_inc(&cifdev->fh_cnt);
		mutex_unlock(&cifdev->stream_lock);
	} else {
		rkcif_soft_reset(cifdev, true);
	}

	ret = v4l2_fh_open(filp);
	if (!ret) {
		ret = v4l2_pipeline_pm_use(&vnode->vdev.entity, 1);
		if (ret < 0)
			vb2_fop_release(filp);
	}

	return ret;
}

static int rkcif_fh_release(struct file *filp)
{
	struct video_device *vdev = video_devdata(filp);
	struct rkcif_vdev_node *vnode = vdev_to_node(vdev);
	struct rkcif_stream *stream = to_rkcif_stream(vnode);
	struct rkcif_device *cifdev = stream->cifdev;
	int ret = 0;

	ret = vb2_fop_release(filp);
	if (!ret) {
		ret = v4l2_pipeline_pm_use(&vnode->vdev.entity, 0);
		if (ret < 0)
			v4l2_err(&cifdev->v4l2_dev,
				 "set pipeline power failed %d\n", ret);
	}

	mutex_lock(&cifdev->stream_lock);
	if (!atomic_dec_return(&cifdev->fh_cnt))
		rkcif_soft_reset(cifdev, true);
	else if (atomic_read(&cifdev->fh_cnt) < 0)
		atomic_set(&cifdev->fh_cnt, 0);
	mutex_unlock(&cifdev->stream_lock);

	return ret;
}

static const struct v4l2_file_operations rkcif_fops = {
	.open = rkcif_fh_open,
	.release = rkcif_fh_release,
	.unlocked_ioctl = video_ioctl2,
	.poll = vb2_fop_poll,
	.mmap = vb2_fop_mmap,
};

static int rkcif_enum_input(struct file *file, void *priv,
			    struct v4l2_input *input)
{
	if (input->index > 0)
		return -EINVAL;

	input->type = V4L2_INPUT_TYPE_CAMERA;
	strlcpy(input->name, "Camera", sizeof(input->name));

	return 0;
}

static int rkcif_try_fmt_vid_cap_mplane(struct file *file, void *fh,
					struct v4l2_format *f)
{
	struct rkcif_stream *stream = video_drvdata(file);

	rkcif_set_fmt(stream, &f->fmt.pix_mp, true);

	return 0;
}

static int rkcif_enum_framesizes(struct file *file, void *prov,
				 struct v4l2_frmsizeenum *fsize)
{
	struct v4l2_frmsize_stepwise *s = &fsize->stepwise;
	struct rkcif_stream *stream = video_drvdata(file);
	struct rkcif_device *dev = stream->cifdev;
	struct v4l2_rect input_rect;

	if (fsize->index != 0)
		return -EINVAL;

	if (!find_output_fmt(stream, fsize->pixel_format))
		return -EINVAL;

	input_rect.width = RKCIF_DEFAULT_WIDTH;
	input_rect.height = RKCIF_DEFAULT_HEIGHT;

	if (dev->active_sensor && dev->active_sensor->sd)
		get_input_fmt(dev->active_sensor->sd,
			      &input_rect, stream->id + 1);

	fsize->type = V4L2_FRMSIZE_TYPE_STEPWISE;
	s->min_width = CIF_MIN_WIDTH;
	s->min_height = CIF_MIN_HEIGHT;
	s->max_width = input_rect.width;
	s->max_height = input_rect.height;
	s->step_width = OUTPUT_STEP_WISE;
	s->step_height = OUTPUT_STEP_WISE;

	return 0;
}

static int rkcif_enum_frameintervals(struct file *file, void *fh,
				     struct v4l2_frmivalenum *fival)
{
	struct rkcif_stream *stream = video_drvdata(file);
	struct rkcif_device *dev = stream->cifdev;
	struct rkcif_sensor_info *sensor = dev->active_sensor;
	struct v4l2_subdev_frame_interval fi;
	int ret;

	if (fival->index != 0)
		return -EINVAL;

	if (!sensor || !sensor->sd) {
		/* TODO: active_sensor is NULL if using DMARX path */
		v4l2_err(&dev->v4l2_dev, "%s Not active sensor\n", __func__);
		return -ENODEV;
	}

	ret = v4l2_subdev_call(sensor->sd, video, g_frame_interval, &fi);
	if (ret && ret != -ENOIOCTLCMD) {
		return ret;
	} else if (ret == -ENOIOCTLCMD) {
		/* Set a default value for sensors not implements ioctl */
		fi.interval.numerator = 1;
		fi.interval.denominator = 30;
	}

	fival->type = V4L2_FRMIVAL_TYPE_CONTINUOUS;
	fival->stepwise.step.numerator = 1;
	fival->stepwise.step.denominator = 1;
	fival->stepwise.max.numerator = 1;
	fival->stepwise.max.denominator = 1;
	fival->stepwise.min.numerator = fi.interval.numerator;
	fival->stepwise.min.denominator = fi.interval.denominator;

	return 0;
}

static int rkcif_enum_fmt_vid_cap_mplane(struct file *file, void *priv,
					 struct v4l2_fmtdesc *f)
{
	const struct cif_output_fmt *fmt = NULL;

	if (f->index >= ARRAY_SIZE(out_fmts))
		return -EINVAL;

	fmt = &out_fmts[f->index];
	f->pixelformat = fmt->fourcc;

	return 0;
}

static int rkcif_s_fmt_vid_cap_mplane(struct file *file,
				      void *priv, struct v4l2_format *f)
{
	struct rkcif_stream *stream = video_drvdata(file);
	struct rkcif_device *dev = stream->cifdev;

	if (vb2_is_busy(&stream->vnode.buf_queue)) {
		v4l2_err(&dev->v4l2_dev, "%s queue busy\n", __func__);
		return -EBUSY;
	}

	rkcif_set_fmt(stream, &f->fmt.pix_mp, false);

	return 0;
}

static int rkcif_g_fmt_vid_cap_mplane(struct file *file, void *fh,
				      struct v4l2_format *f)
{
	struct rkcif_stream *stream = video_drvdata(file);

	f->fmt.pix_mp = stream->pixm;

	return 0;
}

static int rkcif_querycap(struct file *file, void *priv,
			  struct v4l2_capability *cap)
{
	struct rkcif_stream *stream = video_drvdata(file);
	struct device *dev = stream->cifdev->dev;

	strlcpy(cap->driver, dev->driver->name, sizeof(cap->driver));
	strlcpy(cap->card, dev->driver->name, sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info),
		 "platform:%s", dev_name(dev));

	return 0;
}

static int rkcif_s_crop(struct file *file, void *fh, const struct v4l2_crop *a)
{
	struct rkcif_stream *stream = video_drvdata(file);
	struct rkcif_device *dev = stream->cifdev;
	const struct v4l2_rect *rect = &a->c;
	int ret;

	v4l2_info(&dev->v4l2_dev, "S_CROP(%ux%u@%u:%u) type: %d\n",
		  rect->width, rect->height, rect->left, rect->top, a->type);

	ret = rkcif_sanity_check_fmt(stream, rect);
	if (ret)
		return ret;

	stream->crop = *rect;
	stream->crop_enable = true;

	return 0;
}

static int rkcif_g_crop(struct file *file, void *fh, struct v4l2_crop *a)
{
	struct rkcif_stream *stream = video_drvdata(file);

	a->c = stream->crop;

	return 0;
}

static const struct v4l2_ioctl_ops rkcif_v4l2_ioctl_ops = {
	.vidioc_reqbufs = vb2_ioctl_reqbufs,
	.vidioc_querybuf = vb2_ioctl_querybuf,
	.vidioc_create_bufs = vb2_ioctl_create_bufs,
	.vidioc_qbuf = vb2_ioctl_qbuf,
	.vidioc_expbuf = vb2_ioctl_expbuf,
	.vidioc_dqbuf = vb2_ioctl_dqbuf,
	.vidioc_prepare_buf = vb2_ioctl_prepare_buf,
	.vidioc_streamon = vb2_ioctl_streamon,
	.vidioc_streamoff = vb2_ioctl_streamoff,
	.vidioc_enum_input = rkcif_enum_input,
	.vidioc_try_fmt_vid_cap_mplane = rkcif_try_fmt_vid_cap_mplane,
	.vidioc_enum_fmt_vid_cap_mplane = rkcif_enum_fmt_vid_cap_mplane,
	.vidioc_s_fmt_vid_cap_mplane = rkcif_s_fmt_vid_cap_mplane,
	.vidioc_g_fmt_vid_cap_mplane = rkcif_g_fmt_vid_cap_mplane,
	.vidioc_querycap = rkcif_querycap,
	.vidioc_s_crop = rkcif_s_crop,
	.vidioc_g_crop = rkcif_g_crop,
	.vidioc_enum_frameintervals = rkcif_enum_frameintervals,
	.vidioc_enum_framesizes = rkcif_enum_framesizes,
};

static void rkcif_unregister_stream_vdev(struct rkcif_stream *stream)
{
	media_entity_cleanup(&stream->vnode.vdev.entity);
	video_unregister_device(&stream->vnode.vdev);
}

static int rkcif_register_stream_vdev(struct rkcif_stream *stream,
				      bool is_multi_input)
{
	struct rkcif_device *dev = stream->cifdev;
	struct v4l2_device *v4l2_dev = &dev->v4l2_dev;
	struct video_device *vdev = &stream->vnode.vdev;
	struct rkcif_vdev_node *node;
	int ret = 0;
	char *vdev_name;

	if (is_multi_input) {
		switch (stream->id) {
		case RKCIF_STREAM_MIPI_ID0:
			vdev_name = CIF_MIPI_ID0_VDEV_NAME;
			break;
		case RKCIF_STREAM_MIPI_ID1:
			vdev_name = CIF_MIPI_ID1_VDEV_NAME;
			break;
		case RKCIF_STREAM_MIPI_ID2:
			vdev_name = CIF_MIPI_ID2_VDEV_NAME;
			break;
		case RKCIF_STREAM_MIPI_ID3:
			vdev_name = CIF_MIPI_ID3_VDEV_NAME;
			break;
		case RKCIF_STREAM_DVP:
			vdev_name = CIF_DVP_VDEV_NAME;
			break;
		default:
			v4l2_err(v4l2_dev, "Invalid stream\n");
			goto unreg;
		}
	} else {
		vdev_name = CIF_VIDEODEVICE_NAME;
	}

	strlcpy(vdev->name, vdev_name, sizeof(vdev->name));
	node = vdev_to_node(vdev);
	mutex_init(&node->vlock);

	vdev->ioctl_ops = &rkcif_v4l2_ioctl_ops;
	vdev->release = video_device_release_empty;
	vdev->fops = &rkcif_fops;
	vdev->minor = -1;
	vdev->v4l2_dev = v4l2_dev;
	vdev->lock = &node->vlock;
	vdev->device_caps = V4L2_CAP_VIDEO_CAPTURE_MPLANE |
			    V4L2_CAP_STREAMING;
	video_set_drvdata(vdev, stream);
	vdev->vfl_dir = VFL_DIR_RX;
	node->pad.flags = MEDIA_PAD_FL_SINK;

	rkcif_init_vb2_queue(&node->buf_queue, stream,
			     V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
	vdev->queue = &node->buf_queue;

	ret = video_register_device(vdev, VFL_TYPE_GRABBER, -1);
	if (ret < 0) {
		v4l2_err(v4l2_dev,
			 "video_register_device failed with error %d\n", ret);
		return ret;
	}

	ret = media_entity_pads_init(&vdev->entity, 1, &node->pad);
	if (ret < 0)
		goto unreg;

	return 0;
unreg:
	video_unregister_device(vdev);
	return ret;
}

void rkcif_unregister_stream_vdevs(struct rkcif_device *dev,
				   int stream_num)
{
	struct rkcif_stream *stream;
	int i;

	for (i = 0; i < stream_num; i++) {
		stream = &dev->stream[i];
		rkcif_unregister_stream_vdev(stream);
	}
}

int rkcif_register_stream_vdevs(struct rkcif_device *dev,
				int stream_num,
				bool is_multi_input)
{
	struct rkcif_stream *stream;
	int i, j, ret;

	for (i = 0; i < stream_num; i++) {
		stream = &dev->stream[i];
		stream->cifdev = dev;
		ret = rkcif_register_stream_vdev(stream, is_multi_input);
		if (ret < 0)
			goto err;
	}

	return 0;
err:
	for (j = 0; j < i; j++) {
		stream = &dev->stream[j];
		rkcif_unregister_stream_vdev(stream);
	}

	return ret;
}

static struct v4l2_subdev *get_lvds_remote_sensor(struct v4l2_subdev *sd)
{
	struct media_pad *local, *remote;
	struct media_entity *sensor_me;

	local = &sd->entity.pads[RKCIF_LVDS_PAD_SINK];
	remote = media_entity_remote_pad(local);
	if (!remote) {
		v4l2_warn(sd, "No link between dphy and sensor with lvds\n");
		return NULL;
	}

	sensor_me = media_entity_remote_pad(local)->entity;
	return media_entity_to_v4l2_subdev(sensor_me);
}

static int rkcif_lvds_subdev_link_setup(struct media_entity *entity,
					const struct media_pad *local,
					const struct media_pad *remote,
					u32 flags)
{
	return 0;
}

static int rkcif_lvds_sd_set_fmt(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_format *fmt)
{
	struct v4l2_subdev *sensor = get_lvds_remote_sensor(sd);

	/*
	 * Do not allow format changes and just relay whatever
	 * set currently in the sensor.
	 */
	return v4l2_subdev_call(sensor, pad, get_fmt, NULL, fmt);
}

static int rkcif_lvds_sd_get_fmt(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_format *fmt)
{
	struct v4l2_subdev *sensor = get_lvds_remote_sensor(sd);

	/*
	 * Do not allow format changes and just relay whatever
	 * set currently in the sensor.
	 */
	return v4l2_subdev_call(sensor, pad, get_fmt, NULL, fmt);
}

static int rkcif_lvds_g_mbus_config(struct v4l2_subdev *sd,
				    struct v4l2_mbus_config *mbus)
{
	struct v4l2_subdev *sensor_sd = get_lvds_remote_sensor(sd);
	int ret;

	ret = v4l2_subdev_call(sensor_sd, video, g_mbus_config, mbus);
	if (ret)
		return ret;

	return 0;
}

static int rkcif_lvds_sd_s_stream(struct v4l2_subdev *sd, int on)
{
	struct rkcif_lvds_subdev *subdev = container_of(sd, struct rkcif_lvds_subdev, sd);

	if (on)
		atomic_set(&subdev->frm_sync_seq, 0);

	return 0;
}

static int rkcif_lvds_sd_s_power(struct v4l2_subdev *sd, int on)
{
	return 0;
}

static int rkcif_lvds_subscribe_event(struct v4l2_subdev *sd, struct v4l2_fh *fh,
				      struct v4l2_event_subscription *sub)
{
	if (sub->type != V4L2_EVENT_FRAME_SYNC)
		return -EINVAL;

	return v4l2_event_subscribe(fh, sub, 0, NULL);
}

static const struct media_entity_operations rkcif_lvds_sd_media_ops = {
	.link_setup = rkcif_lvds_subdev_link_setup,
	.link_validate = v4l2_subdev_link_validate,
};

static const struct v4l2_subdev_pad_ops rkcif_lvds_sd_pad_ops = {
	.set_fmt = rkcif_lvds_sd_set_fmt,
	.get_fmt = rkcif_lvds_sd_get_fmt,
};

static const struct v4l2_subdev_video_ops rkcif_lvds_sd_video_ops = {
	.g_mbus_config = rkcif_lvds_g_mbus_config,
	.s_stream = rkcif_lvds_sd_s_stream,
};

static const struct v4l2_subdev_core_ops rkcif_lvds_sd_core_ops = {
	.s_power = rkcif_lvds_sd_s_power,
	.subscribe_event = rkcif_lvds_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static struct v4l2_subdev_ops rkcif_lvds_sd_ops = {
	.core = &rkcif_lvds_sd_core_ops,
	.video = &rkcif_lvds_sd_video_ops,
	.pad = &rkcif_lvds_sd_pad_ops,
};

static void rkcif_lvds_event_inc_sof(struct rkcif_device *dev)
{
	struct rkcif_lvds_subdev *subdev = &dev->lvds_subdev;

	if (subdev) {
		struct v4l2_event event = {
			.type = V4L2_EVENT_FRAME_SYNC,
			.u.frame_sync.frame_sequence =
				atomic_inc_return(&subdev->frm_sync_seq) - 1,
		};
		v4l2_event_queue(subdev->sd.devnode, &event);
	}
}

static u32 rkcif_lvds_get_sof(struct rkcif_device *dev)
{
	if (dev)
		return atomic_read(&dev->lvds_subdev.frm_sync_seq) - 1;

	return 0;
}

int rkcif_register_lvds_subdev(struct rkcif_device *dev)
{
	struct v4l2_device *v4l2_dev = &dev->v4l2_dev;
	struct rkcif_lvds_subdev *lvds_subdev = &dev->lvds_subdev;
	struct v4l2_subdev *sd;
	int ret;

	memset(lvds_subdev, 0, sizeof(*lvds_subdev));
	lvds_subdev->cifdev = dev;
	sd = &lvds_subdev->sd;
	lvds_subdev->state = RKCIF_LVDS_STOP;
	v4l2_subdev_init(sd, &rkcif_lvds_sd_ops);
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS;
	sd->entity.ops = &rkcif_lvds_sd_media_ops;
	if (dev->chip_id == CHIP_RV1126_CIF)
		snprintf(sd->name, sizeof(sd->name), "rkcif-lvds-subdev");
	else
		snprintf(sd->name, sizeof(sd->name), "rkcif-lite-lvds-subdev");

	lvds_subdev->pads[RKCIF_LVDS_PAD_SINK].flags =
		MEDIA_PAD_FL_SINK | MEDIA_PAD_FL_MUST_CONNECT;
	lvds_subdev->pads[RKCIF_LVDS_PAD_SRC_ID0].flags = MEDIA_PAD_FL_SOURCE;
	lvds_subdev->pads[RKCIF_LVDS_PAD_SRC_ID1].flags = MEDIA_PAD_FL_SOURCE;
	lvds_subdev->pads[RKCIF_LVDS_PAD_SRC_ID2].flags = MEDIA_PAD_FL_SOURCE;
	lvds_subdev->pads[RKCIF_LVDS_PAD_SRC_ID3].flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&sd->entity, RKCIF_LVDS_PAD_MAX,
				     lvds_subdev->pads);
	if (ret < 0)
		return ret;
	sd->owner = THIS_MODULE;
	v4l2_set_subdevdata(sd, lvds_subdev);
	ret = v4l2_device_register_subdev(v4l2_dev, sd);
	if (ret < 0)
		goto free_media;

	ret = v4l2_device_register_subdev_nodes(v4l2_dev);
	if (ret < 0)
		goto free_subdev;
	return ret;
free_subdev:
	v4l2_device_unregister_subdev(sd);
free_media:
	media_entity_cleanup(&sd->entity);
	v4l2_err(sd, "Failed to register subdev, ret:%d\n", ret);
	return ret;
}

void rkcif_unregister_lvds_subdev(struct rkcif_device *dev)
{
	struct v4l2_subdev *sd = &dev->lvds_subdev.sd;

	v4l2_device_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
}

static void rkcif_vb_done_oneframe(struct rkcif_stream *stream,
				   struct vb2_v4l2_buffer *vb_done)
{
	const struct cif_output_fmt *fmt = stream->cif_fmt_out;
	u32 i;

	/* Dequeue a filled buffer */
	for (i = 0; i < fmt->mplanes; i++) {
		vb2_set_plane_payload(&vb_done->vb2_buf, i,
				      stream->pixm.plane_fmt[i].sizeimage);
	}

	if (stream->cifdev->hdr.mode == NO_HDR)
		vb_done->vb2_buf.timestamp = ktime_get_ns();

	vb2_buffer_done(&vb_done->vb2_buf, VB2_BUF_STATE_DONE);
}

void rkcif_irq_oneframe(struct rkcif_device *cif_dev)
{
	/* TODO: xuhf-debug: add stream type */
	struct rkcif_stream *stream;
	u32 lastline, lastpix, ctl, cif_frmst, intstat, frmid;

	intstat = rkcif_read_register(cif_dev, CIF_REG_DVP_INTSTAT);
	cif_frmst = rkcif_read_register(cif_dev, CIF_REG_DVP_FRAME_STATUS);
	lastline = rkcif_read_register(cif_dev, CIF_REG_DVP_LAST_LINE);
	lastpix = rkcif_read_register(cif_dev, CIF_REG_DVP_LAST_PIX);
	ctl = rkcif_read_register(cif_dev, CIF_REG_DVP_CTRL);
	frmid = CIF_GET_FRAME_ID(cif_frmst);

	/* There are two irqs enabled:
	 *  - PST_INF_FRAME_END: cif FIFO is ready, this is prior to FRAME_END
	 *  -         FRAME_END: cif has saved frame to memory, a frame ready
	 */
	if (cif_dev->chip_id == CHIP_RK1808_CIF ||
	    cif_dev->chip_id == CHIP_RV1126_CIF)
		stream = &cif_dev->stream[RKCIF_STREAM_DVP];
	else
		stream = &cif_dev->stream[RKCIF_STREAM_CIF];

	if ((intstat & PST_INF_FRAME_END)) {
		rkcif_write_register(cif_dev, CIF_REG_DVP_INTSTAT,
				     PST_INF_FRAME_END_CLR);

		if (stream->stopping)
			/* To stop CIF ASAP, before FRAME_END irq */
			rkcif_write_register(cif_dev, CIF_REG_DVP_CTRL,
					     ctl & (~ENABLE_CAPTURE));
	}

	if ((intstat & FRAME_END)) {
		struct vb2_v4l2_buffer *vb_done = NULL;

		rkcif_write_register(cif_dev, CIF_REG_DVP_INTSTAT,
				     FRAME_END_CLR);

		if (stream->stopping) {
			rkcif_stream_stop(stream);
			stream->stopping = false;
			wake_up(&stream->wq_stopped);
			return;
		}

		if (lastline != stream->pixm.height ||
		    !(cif_frmst & CIF_F0_READY)) {
			/* Clearing status must be complete before fe packet
			 * arrives while cif is connected with mipi,
			 * so it should be placed before printing log here,
			 * otherwise it would be delayed.
			 * At the same time, don't clear the frame id
			 * for switching address.
			 */
			rkcif_write_register(cif_dev, CIF_REG_DVP_FRAME_STATUS,
					     FRM0_STAT_CLS);
			v4l2_err(&cif_dev->v4l2_dev,
				 "Bad frame, irq:0x%x frmst:0x%x size:%dx%d\n",
				 intstat, cif_frmst, lastline, lastpix);

			return;
		}

		if (frmid % 2 != 0) {
			stream->frame_phase = CIF_CSI_FRAME0_READY;
			if (stream->curr_buf)
				vb_done = &stream->curr_buf->vb;
		} else {
			stream->frame_phase = CIF_CSI_FRAME1_READY;
			if (stream->next_buf)
				vb_done = &stream->next_buf->vb;
		}

		rkcif_assign_new_buffer_oneframe(stream,
						 RKCIF_YUV_ADDR_STATE_UPDATE);

		/* In one-frame mode:
		 * 1,must clear status manually by writing 0 to enable
		 * the next frame end irq;
		 * 2,do not clear the frame id for switching address.
		 */
		rkcif_write_register(cif_dev, CIF_REG_DVP_FRAME_STATUS,
				     cif_frmst & FRM0_STAT_CLS);

		stream->frame_idx++;
		if (vb_done) {
			vb_done->sequence = stream->frame_idx;
			rkcif_vb_done_oneframe(stream, vb_done);
		}
	}
}

static int rkcif_csi_g_mipi_id(struct v4l2_device *v4l2_dev,
			       unsigned int intstat)
{
	if (intstat & CSI_FRAME_END_ID0) {
		if ((intstat & CSI_FRAME_END_ID0) ==
		    CSI_FRAME_END_ID0)
			v4l2_warn(v4l2_dev, "frame0/1 trigger simultaneously in ID0\n");
		return RKCIF_STREAM_MIPI_ID0;
	}

	if (intstat & CSI_FRAME_END_ID1) {
		if ((intstat & CSI_FRAME_END_ID1) ==
		    CSI_FRAME_END_ID1)
			v4l2_warn(v4l2_dev, "frame0/1 trigger simultaneously in ID1\n");
		return RKCIF_STREAM_MIPI_ID1;
	}

	if (intstat & CSI_FRAME_END_ID2) {
		if ((intstat & CSI_FRAME_END_ID2) ==
		    CSI_FRAME_END_ID2)
			v4l2_warn(v4l2_dev, "frame0/1 trigger simultaneously in ID2\n");
		return RKCIF_STREAM_MIPI_ID2;
	}

	if (intstat & CSI_FRAME_END_ID3) {
		if ((intstat & CSI_FRAME_END_ID3) ==
		    CSI_FRAME_END_ID3)
			v4l2_warn(v4l2_dev, "frame0/1 trigger simultaneously in ID3\n");
		return RKCIF_STREAM_MIPI_ID3;
	}

	return -EINVAL;
}

static void rdbk_frame_end(struct rkcif_stream *stream)
{
	struct rkcif_device *dev = stream->cifdev;
	u64 l_ts, s_ts;

	if (stream->id != RKCIF_STREAM_MIPI_ID1)
		return;

	if (dev->hdr.mode == HDR_X3) {
		/* TODO: HDR_X3 */
	} else if (dev->hdr.mode == HDR_X2) {
		if (dev->rdbk_buf[RDBK_L] && dev->rdbk_buf[RDBK_S]) {
			l_ts = dev->rdbk_buf[RDBK_L]->vb.vb2_buf.timestamp;
			s_ts = dev->rdbk_buf[RDBK_S]->vb.vb2_buf.timestamp;

			if ((s_ts - l_ts) > 30000000LL || s_ts < l_ts) {
				v4l2_err(&dev->v4l2_dev,
					 "HDR_X2:timestamp is not match (%lld, %lld)\n",
					 l_ts, s_ts);
				goto RDBK_FRM_UNMATCH;
			}

			dev->rdbk_buf[RDBK_S]->vb.sequence =
				dev->rdbk_buf[RDBK_L]->vb.sequence;
			rkcif_vb_done_oneframe(stream, &dev->rdbk_buf[RDBK_L]->vb);
			rkcif_vb_done_oneframe(stream, &dev->rdbk_buf[RDBK_S]->vb);
		} else {
			if (!dev->rdbk_buf[RDBK_L])
				v4l2_err(&dev->v4l2_dev, "lost long frames\n");
			if (!dev->rdbk_buf[RDBK_S])
				v4l2_err(&dev->v4l2_dev, "lost short frames\n");
			goto RDBK_FRM_UNMATCH;
		}
	} else {
		rkcif_vb_done_oneframe(stream, &dev->rdbk_buf[RDBK_S]->vb);
	}

	dev->rdbk_buf[RDBK_L] = NULL;
	dev->rdbk_buf[RDBK_M] = NULL;
	dev->rdbk_buf[RDBK_S] = NULL;
	return;

RDBK_FRM_UNMATCH:
	if (dev->rdbk_buf[RDBK_L]) {
		dev->rdbk_buf[RDBK_L]->vb.vb2_buf.state = VB2_BUF_STATE_ACTIVE;
		rkcif_buf_queue(&dev->rdbk_buf[RDBK_L]->vb.vb2_buf);
	}
	if (dev->rdbk_buf[RDBK_M]) {
		dev->rdbk_buf[RDBK_M]->vb.vb2_buf.state = VB2_BUF_STATE_ACTIVE;
		rkcif_buf_queue(&dev->rdbk_buf[RDBK_M]->vb.vb2_buf);
	}
	if (dev->rdbk_buf[RDBK_S]) {
		dev->rdbk_buf[RDBK_S]->vb.vb2_buf.state = VB2_BUF_STATE_ACTIVE;
		rkcif_buf_queue(&dev->rdbk_buf[RDBK_S]->vb.vb2_buf);
	}

	dev->rdbk_buf[RDBK_L] = NULL;
	dev->rdbk_buf[RDBK_M] = NULL;
	dev->rdbk_buf[RDBK_S] = NULL;
}

void rkcif_irq_pingpong(struct rkcif_device *cif_dev)
{
	/* TODO: xuhf-debug: add stream type */
	struct rkcif_stream *stream;
	struct rkcif_buffer *active_buf = NULL;
	struct v4l2_mbus_config *mbus = &cif_dev->active_sensor->mbus;
	void __iomem *base = cif_dev->base_addr;
	unsigned int intstat, i = 0xff;

	if ((mbus->type == V4L2_MBUS_CSI2 ||
	     mbus->type == V4L2_MBUS_CCP2) &&
	    (cif_dev->chip_id == CHIP_RK1808_CIF ||
	     cif_dev->chip_id == CHIP_RV1126_CIF)) {
		int mipi_id;
		struct vb2_v4l2_buffer *vb_done = NULL;
		u32 lastline = 0;

		intstat = read_cif_reg(base, CIF_CSI_INTSTAT);
		lastline = read_cif_reg(base, CIF_CSI_LINE_CNT_ID0_1);

		/* clear all interrupts that has been triggered */
		write_cif_reg(base, CIF_CSI_INTSTAT, intstat);

		if (intstat & CSI_FIFO_OVERFLOW) {
			v4l2_err(&cif_dev->v4l2_dev,
				 "ERROR: csi fifo overflow, intstat:0x%x, lastline:%d!!\n",
				  intstat, lastline);
			return;
		}

		if (intstat & CSI_BANDWIDTH_LACK) {
			v4l2_err(&cif_dev->v4l2_dev,
				 "ERROR: csi bandwidth lack, intstat:0x%x!!\n",
				 intstat);
			return;
		}

		if (intstat & CSI_ALL_ERROR_INTEN) {
			v4l2_err(&cif_dev->v4l2_dev,
				 "ERROR: CSI_ALL_ERROR_INTEN:0x%x!!\n", intstat);
			return;
		}

		if (intstat & CSI_FRAME0_START_ID0) {
			if (mbus->type == V4L2_MBUS_CSI2)
				rkcif_csi2_event_inc_sof();
			else if (mbus->type == V4L2_MBUS_CCP2)
				rkcif_lvds_event_inc_sof(cif_dev);
		}

		if (intstat & CSI_FRAME1_START_ID0) {
			if (mbus->type == V4L2_MBUS_CSI2)
				rkcif_csi2_event_inc_sof();
			else if (mbus->type == V4L2_MBUS_CCP2)
				rkcif_lvds_event_inc_sof(cif_dev);
		}

		/* if do not reach frame dma end, return irq */
		mipi_id = rkcif_csi_g_mipi_id(&cif_dev->v4l2_dev, intstat);
		if (mipi_id < 0)
			return;

		for (i = 0; i < RKCIF_MAX_STREAM_MIPI; i++) {
			mipi_id = rkcif_csi_g_mipi_id(&cif_dev->v4l2_dev,
						      intstat);
			if (mipi_id < 0)
				continue;

			stream = &cif_dev->stream[mipi_id];

			if (stream->stopping) {
				rkcif_stream_stop(stream);
				stream->stopping = false;
				wake_up(&stream->wq_stopped);
				continue;
			}

			if (stream->state != RKCIF_STATE_STREAMING)
				continue;

			switch (mipi_id) {
			case RKCIF_STREAM_MIPI_ID0:
				stream->frame_phase = SW_FRM_END_ID0(intstat);
				intstat &= ~CSI_FRAME_END_ID0;
				break;
			case RKCIF_STREAM_MIPI_ID1:
				stream->frame_phase = SW_FRM_END_ID1(intstat);
				intstat &= ~CSI_FRAME_END_ID1;
				break;
			case RKCIF_STREAM_MIPI_ID2:
				stream->frame_phase = SW_FRM_END_ID2(intstat);
				intstat &= ~CSI_FRAME_END_ID2;
				break;
			case RKCIF_STREAM_MIPI_ID3:
				stream->frame_phase = SW_FRM_END_ID3(intstat);
				intstat &= ~CSI_FRAME_END_ID3;
				break;
			}

			stream->frame_idx++;

			active_buf = NULL;
			if (stream->frame_phase & CIF_CSI_FRAME1_READY) {
				if (stream->next_buf)
					active_buf = stream->next_buf;
			} else if (stream->frame_phase & CIF_CSI_FRAME0_READY) {
				if (stream->curr_buf)
					active_buf = stream->curr_buf;
			}

			rkcif_assign_new_buffer_pingpong(stream, 0, mipi_id);
			if (cif_dev->chip_id == CHIP_RV1126_CIF)
				rkcif_luma_isr(&cif_dev->luma_vdev, mipi_id);

			if (active_buf) {
				vb_done = &active_buf->vb;
				vb_done->vb2_buf.timestamp = ktime_get_ns();
				vb_done->sequence = stream->frame_idx;
			}

			if (cif_dev->hdr.mode == NO_HDR) {
				if (active_buf)
					rkcif_vb_done_oneframe(stream, vb_done);
			} else if (cif_dev->hdr.mode == HDR_X2) {
				if (mipi_id == RKCIF_STREAM_MIPI_ID0) {
					if (cif_dev->rdbk_buf[RDBK_L]) {
						v4l2_err(&cif_dev->v4l2_dev,
							 "multiple long data in hdr frame,frm_idx:%d,state:0x%x\n",
							 stream->frame_idx,
							 cif_dev->rdbk_buf[RDBK_L]->vb.vb2_buf.state);
						cif_dev->rdbk_buf[RDBK_L]->vb.vb2_buf.state = VB2_BUF_STATE_ACTIVE;
						rkcif_buf_queue(&cif_dev->rdbk_buf[RDBK_L]->vb.vb2_buf);
					}
					cif_dev->rdbk_buf[RDBK_L] = active_buf;
				} else if (mipi_id == RKCIF_STREAM_MIPI_ID1) {
					if (cif_dev->rdbk_buf[RDBK_S]) {
						v4l2_err(&cif_dev->v4l2_dev,
							 "multiple short data in hdr frame, state:0x%x\n",
							 cif_dev->rdbk_buf[RDBK_S]->vb.vb2_buf.state);
						cif_dev->rdbk_buf[RDBK_S]->vb.vb2_buf.state = VB2_BUF_STATE_ACTIVE;
						rkcif_buf_queue(&cif_dev->rdbk_buf[RDBK_S]->vb.vb2_buf);
					}
					cif_dev->rdbk_buf[RDBK_S] = active_buf;
					rdbk_frame_end(stream);
				}
			} else if (cif_dev->hdr.mode == HDR_X3) {
				/*TODO: HDR_X3*/
			}
		}
	} else {
		u32 lastline, lastpix, ctl, cif_frmst, frmid;
		struct rkcif_stream *stream;

		intstat = rkcif_read_register(cif_dev, CIF_REG_DVP_INTSTAT);
		cif_frmst = rkcif_read_register(cif_dev, CIF_REG_DVP_FRAME_STATUS);
		lastline = rkcif_read_register(cif_dev, CIF_REG_DVP_LAST_LINE);
		lastline = CIF_FETCH_Y_LAST_LINE(lastline);
		lastpix = rkcif_read_register(cif_dev, CIF_REG_DVP_LAST_PIX);
		lastpix =  CIF_FETCH_Y_LAST_LINE(lastpix);
		ctl = rkcif_read_register(cif_dev, CIF_REG_DVP_CTRL);

		if (cif_dev->chip_id == CHIP_RK1808_CIF ||
		    cif_dev->chip_id == CHIP_RV1126_CIF)
			stream = &cif_dev->stream[RKCIF_STREAM_DVP];
		else
			stream = &cif_dev->stream[RKCIF_STREAM_CIF];

		/* There are two irqs enabled:
		 *  - PST_INF_FRAME_END: cif FIFO is ready,
		 *    this is prior to FRAME_END
		 *  - FRAME_END: cif has saved frame to memory,
		 *    a frame ready
		 */
		if ((intstat & PST_INF_FRAME_END)) {
			rkcif_write_register(cif_dev, CIF_REG_DVP_INTSTAT,
					     PST_INF_FRAME_END_CLR);

			if (stream->stopping)
				/* To stop CIF ASAP, before FRAME_END irq */
				rkcif_write_register(cif_dev, CIF_REG_DVP_CTRL,
						     ctl & (~ENABLE_CAPTURE));
		}

		if ((intstat & FRAME_END)) {
			struct vb2_v4l2_buffer *vb_done = NULL;

			rkcif_write_register(cif_dev, CIF_REG_DVP_INTSTAT,
					     FRAME_END_CLR);

			if (stream->stopping) {
				rkcif_stream_stop(stream);
				stream->stopping = false;
				wake_up(&stream->wq_stopped);
				return;
			}

			frmid = CIF_GET_FRAME_ID(cif_frmst);
			if ((cif_frmst == 0xfffd0002) || (cif_frmst == 0xfffe0002)) {
				v4l2_info(&cif_dev->v4l2_dev, "frmid:%d, frmstat:0x%x\n",
					  frmid, cif_frmst);
				rkcif_write_register(cif_dev, CIF_REG_DVP_FRAME_STATUS,
						     FRAME_STAT_CLS);
			}

			if (lastline != stream->pixm.height ||
			    (!(cif_frmst & CIF_F0_READY) &&
			     !(cif_frmst & CIF_F1_READY))) {
				v4l2_err(&cif_dev->v4l2_dev,
					 "Bad frame, pp irq:0x%x frmst:0x%x size:%dx%d\n",
					 intstat, cif_frmst, lastpix, lastline);
				return;
			}

			if (cif_frmst & CIF_F0_READY) {
				if (stream->curr_buf)
					vb_done = &stream->curr_buf->vb;
				stream->frame_phase = CIF_CSI_FRAME0_READY;
			} else if (cif_frmst & CIF_F1_READY) {
				if (stream->next_buf)
					vb_done = &stream->next_buf->vb;
				stream->frame_phase = CIF_CSI_FRAME1_READY;
			}

			rkcif_assign_new_buffer_oneframe(stream,
							 RKCIF_YUV_ADDR_STATE_UPDATE);

			stream->frame_idx++;
			if (vb_done) {
				vb_done->sequence = stream->frame_idx;
				rkcif_vb_done_oneframe(stream, vb_done);
			}
		}
	}
}

void rkcif_irq_lite_lvds(struct rkcif_device *cif_dev)
{
	struct rkcif_stream *stream;
	struct rkcif_buffer *active_buf = NULL;
	struct v4l2_mbus_config *mbus = &cif_dev->active_sensor->mbus;
	unsigned int intstat, i = 0xff;

	if (mbus->type == V4L2_MBUS_CCP2 &&
	    cif_dev->chip_id == CHIP_RV1126_CIF_LITE) {
		int mipi_id;
		struct vb2_v4l2_buffer *vb_done = NULL;
		u32 lastline = 0;

		intstat = rkcif_read_register(cif_dev, CIF_REG_MIPI_LVDS_INTSTAT);
		lastline = rkcif_read_register(cif_dev, CIF_REG_MIPI_LVDS_LINE_INT_NUM_ID0_1);

		/* clear all interrupts that has been triggered */
		rkcif_write_register(cif_dev, CIF_REG_MIPI_LVDS_INTSTAT, intstat);

		if (intstat & CSI_FIFO_OVERFLOW) {
			v4l2_err(&cif_dev->v4l2_dev,
				 "ERROR: cif lite lvds fifo overflow, intstat:0x%x, lastline:%d!!\n",
				  intstat, lastline);
			return;
		}

		if (intstat & CSI_BANDWIDTH_LACK) {
			v4l2_err(&cif_dev->v4l2_dev,
				 "ERROR: cif lite lvds bandwidth lack, intstat:0x%x!!\n",
				 intstat);
			return;
		}

		if (intstat & CSI_ALL_ERROR_INTEN) {
			v4l2_err(&cif_dev->v4l2_dev,
				 "ERROR: cif lite lvds all err:0x%x!!\n", intstat);
			return;
		}

		if (intstat & CSI_FRAME0_START_ID0)
			rkcif_lvds_event_inc_sof(cif_dev);


		if (intstat & CSI_FRAME1_START_ID0)
			rkcif_lvds_event_inc_sof(cif_dev);

		/* if do not reach frame dma end, return irq */
		mipi_id = rkcif_csi_g_mipi_id(&cif_dev->v4l2_dev, intstat);
		if (mipi_id < 0)
			return;

		for (i = 0; i < RKCIF_MAX_STREAM_MIPI; i++) {
			mipi_id = rkcif_csi_g_mipi_id(&cif_dev->v4l2_dev,
						      intstat);
			if (mipi_id < 0)
				continue;

			stream = &cif_dev->stream[mipi_id];

			if (stream->stopping) {
				rkcif_stream_stop(stream);
				stream->stopping = false;
				wake_up(&stream->wq_stopped);
				continue;
			}

			if (stream->state != RKCIF_STATE_STREAMING)
				continue;

			switch (mipi_id) {
			case RKCIF_STREAM_MIPI_ID0:
				stream->frame_phase = SW_FRM_END_ID0(intstat);
				intstat &= ~CSI_FRAME_END_ID0;
				break;
			case RKCIF_STREAM_MIPI_ID1:
				stream->frame_phase = SW_FRM_END_ID1(intstat);
				intstat &= ~CSI_FRAME_END_ID1;
				break;
			case RKCIF_STREAM_MIPI_ID2:
				stream->frame_phase = SW_FRM_END_ID2(intstat);
				intstat &= ~CSI_FRAME_END_ID2;
				break;
			case RKCIF_STREAM_MIPI_ID3:
				stream->frame_phase = SW_FRM_END_ID3(intstat);
				intstat &= ~CSI_FRAME_END_ID3;
				break;
			}

			stream->frame_idx++;

			active_buf = NULL;
			if (stream->frame_phase & CIF_CSI_FRAME1_READY) {
				if (stream->next_buf)
					active_buf = stream->next_buf;
			} else if (stream->frame_phase & CIF_CSI_FRAME0_READY) {
				if (stream->curr_buf)
					active_buf = stream->curr_buf;
			}

			rkcif_assign_new_buffer_pingpong(stream, 0, mipi_id);
			rkcif_luma_isr(&cif_dev->luma_vdev, mipi_id);

			if (active_buf) {
				vb_done = &active_buf->vb;
				vb_done->vb2_buf.timestamp = ktime_get_ns();
				vb_done->sequence = stream->frame_idx;
			}

			if (cif_dev->hdr.mode == NO_HDR) {
				if (active_buf)
					rkcif_vb_done_oneframe(stream, vb_done);
			} else if (cif_dev->hdr.mode == HDR_X2) {
				if (mipi_id == RKCIF_STREAM_MIPI_ID0) {
					if (cif_dev->rdbk_buf[RDBK_L]) {
						v4l2_err(&cif_dev->v4l2_dev,
							 "cif lite multiple long data in hdr frame,frm_idx:%d,state:0x%x\n",
							 stream->frame_idx,
							 cif_dev->rdbk_buf[RDBK_L]->vb.vb2_buf.state);
						cif_dev->rdbk_buf[RDBK_L]->vb.vb2_buf.state = VB2_BUF_STATE_ACTIVE;
						rkcif_buf_queue(&cif_dev->rdbk_buf[RDBK_L]->vb.vb2_buf);
					}
					cif_dev->rdbk_buf[RDBK_L] = active_buf;
				} else if (mipi_id == RKCIF_STREAM_MIPI_ID1) {
					if (cif_dev->rdbk_buf[RDBK_S]) {
						v4l2_err(&cif_dev->v4l2_dev,
							 "cif lite multiple short data in hdr frame, state:0x%x\n",
							 cif_dev->rdbk_buf[RDBK_S]->vb.vb2_buf.state);
						cif_dev->rdbk_buf[RDBK_S]->vb.vb2_buf.state = VB2_BUF_STATE_ACTIVE;
						rkcif_buf_queue(&cif_dev->rdbk_buf[RDBK_S]->vb.vb2_buf);
					}
					cif_dev->rdbk_buf[RDBK_S] = active_buf;
					rdbk_frame_end(stream);
				}
			} else if (cif_dev->hdr.mode == HDR_X3) {
				/*TODO: HDR_X3*/
			}
		}
	}
}

u32 rkcif_get_sof(struct rkcif_device *cif_dev)
{
	struct rkcif_sensor_info *sensor = cif_dev->active_sensor;

	if (sensor->mbus.type == V4L2_MBUS_CSI2)
		return rkcif_csi2_get_sof();
	else if (sensor->mbus.type == V4L2_MBUS_CCP2)
		return rkcif_lvds_get_sof(cif_dev);
	else if (sensor->mbus.type == V4L2_MBUS_PARALLEL)
		/*TODO*/
		return 0;

	return 0;
}
