// SPDX-License-Identifier: GPL-2.0
/*
 * Rockchip CIF Driver
 *
 * Copyright (C) 2018 Rockchip Electronics Co., Ltd.
 */

#include <linux/delay.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/iommu.h>
#include <media/v4l2-common.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf2-dma-contig.h>
#include <media/videobuf2-dma-sg.h>
#include <soc/rockchip/rockchip-system-status.h>
#include <dt-bindings/soc/rockchip-system-status.h>

#include "dev.h"
#include "mipi-csi2.h"

#define CIF_REQ_BUFS_MIN	3
#define CIF_MIN_WIDTH		64
#define CIF_MIN_HEIGHT		64
#define CIF_MAX_WIDTH		8192
#define CIF_MAX_HEIGHT		8192

#define OUTPUT_STEP_WISE	8

#define RKCIF_PLANE_Y		0
#define RKCIF_PLANE_CBCR	1

#define STREAM_PAD_SINK		0
#define STREAM_PAD_SOURCE	1

#define CIF_TIMEOUT_FRAME_NUM	(2)

#define CIF_DVP_PCLK_DUAL_EDGE	(V4L2_MBUS_PCLK_SAMPLE_RISING |\
				 V4L2_MBUS_PCLK_SAMPLE_FALLING)

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
		.csi_fmt_val = CSI_WRDDR_TYPE_YUV422,
		.fmt_type = CIF_FMT_TYPE_YUV,
	}, {
		.fourcc = V4L2_PIX_FMT_NV61,
		.fmt_val = YUV_OUTPUT_422 | UV_STORAGE_ORDER_VUVU,
		.cplanes = 2,
		.mplanes = 1,
		.bpp = { 8, 16 },
		.csi_fmt_val = CSI_WRDDR_TYPE_YUV422,
		.fmt_type = CIF_FMT_TYPE_YUV,
	}, {
		.fourcc = V4L2_PIX_FMT_NV12,
		.fmt_val = YUV_OUTPUT_420 | UV_STORAGE_ORDER_UVUV,
		.cplanes = 2,
		.mplanes = 1,
		.bpp = { 8, 16 },
		.csi_fmt_val = CSI_WRDDR_TYPE_YUV420SP,
		.fmt_type = CIF_FMT_TYPE_YUV,
	}, {
		.fourcc = V4L2_PIX_FMT_NV21,
		.fmt_val = YUV_OUTPUT_420 | UV_STORAGE_ORDER_VUVU,
		.cplanes = 2,
		.mplanes = 1,
		.bpp = { 8, 16 },
		.csi_fmt_val = CSI_WRDDR_TYPE_YUV420SP,
		.fmt_type = CIF_FMT_TYPE_YUV,
	}, {
		.fourcc = V4L2_PIX_FMT_YUYV,
		.cplanes = 2,
		.mplanes = 1,
		.bpp = { 8, 16 },
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW8,
		.fmt_type = CIF_FMT_TYPE_YUV,
	}, {
		.fourcc = V4L2_PIX_FMT_YVYU,
		.cplanes = 2,
		.mplanes = 1,
		.bpp = { 8, 16 },
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW8,
		.fmt_type = CIF_FMT_TYPE_YUV,
	}, {
		.fourcc = V4L2_PIX_FMT_UYVY,
		.cplanes = 2,
		.mplanes = 1,
		.bpp = { 8, 16 },
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW8,
		.fmt_type = CIF_FMT_TYPE_YUV,
	}, {
		.fourcc = V4L2_PIX_FMT_VYUY,
		.cplanes = 2,
		.mplanes = 1,
		.bpp = { 8, 16 },
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW8,
		.fmt_type = CIF_FMT_TYPE_YUV,
	}, {
		.fourcc = V4L2_PIX_FMT_RGB24,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 24 },
		.csi_fmt_val = CSI_WRDDR_TYPE_RGB888,
		.fmt_type = CIF_FMT_TYPE_RAW,
	}, {
		.fourcc = V4L2_PIX_FMT_RGB565,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 16 },
		.fmt_type = CIF_FMT_TYPE_RAW,
	}, {
		.fourcc = V4L2_PIX_FMT_BGR666,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 18 },
		.fmt_type = CIF_FMT_TYPE_RAW,
	}, {
		.fourcc = V4L2_PIX_FMT_SRGGB8,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 8 },
		.raw_bpp = 8,
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW8,
		.fmt_type = CIF_FMT_TYPE_RAW,
	}, {
		.fourcc = V4L2_PIX_FMT_SGRBG8,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 8 },
		.raw_bpp = 8,
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW8,
		.fmt_type = CIF_FMT_TYPE_RAW,
	}, {
		.fourcc = V4L2_PIX_FMT_SGBRG8,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 8 },
		.raw_bpp = 8,
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW8,
		.fmt_type = CIF_FMT_TYPE_RAW,
	}, {
		.fourcc = V4L2_PIX_FMT_SBGGR8,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 8 },
		.raw_bpp = 8,
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW8,
		.fmt_type = CIF_FMT_TYPE_RAW,
	}, {
		.fourcc = V4L2_PIX_FMT_SRGGB10,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 16 },
		.raw_bpp = 10,
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW10,
		.fmt_type = CIF_FMT_TYPE_RAW,
	}, {
		.fourcc = V4L2_PIX_FMT_SGRBG10,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 16 },
		.raw_bpp = 10,
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW10,
		.fmt_type = CIF_FMT_TYPE_RAW,
	}, {
		.fourcc = V4L2_PIX_FMT_SGBRG10,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 16 },
		.raw_bpp = 10,
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW10,
		.fmt_type = CIF_FMT_TYPE_RAW,
	}, {
		.fourcc = V4L2_PIX_FMT_SBGGR10,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 16 },
		.raw_bpp = 10,
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW10,
		.fmt_type = CIF_FMT_TYPE_RAW,
	}, {
		.fourcc = V4L2_PIX_FMT_SRGGB12,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 16 },
		.raw_bpp = 12,
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW12,
		.fmt_type = CIF_FMT_TYPE_RAW,
	}, {
		.fourcc = V4L2_PIX_FMT_SGRBG12,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 16 },
		.raw_bpp = 12,
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW12,
		.fmt_type = CIF_FMT_TYPE_RAW,
	}, {
		.fourcc = V4L2_PIX_FMT_SGBRG12,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 16 },
		.raw_bpp = 12,
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW12,
		.fmt_type = CIF_FMT_TYPE_RAW,
	}, {
		.fourcc = V4L2_PIX_FMT_SBGGR12,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 16 },
		.raw_bpp = 12,
		.csi_fmt_val = CSI_WRDDR_TYPE_RAW12,
		.fmt_type = CIF_FMT_TYPE_RAW,
	}, {
		.fourcc = V4L2_PIX_FMT_SBGGR16,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 16 },
		.raw_bpp = 16,
		.fmt_type = CIF_FMT_TYPE_RAW,
	}, {
		.fourcc = V4L2_PIX_FMT_Y16,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = { 16 },
		.fmt_type = CIF_FMT_TYPE_RAW,
	}, {
		.fourcc = V4L2_PIX_FMT_GREY,
		.cplanes = 1,
		.mplanes = 1,
		.bpp = {8},
		.fmt_type = CIF_FMT_TYPE_RAW,
	}

	/* TODO: We can support NV12M/NV21M/NV16M/NV61M too */
};

static const struct cif_input_fmt in_fmts[] = {
	{
		.mbus_code	= MEDIA_BUS_FMT_YUYV8_2X8,
		.dvp_fmt_val	= YUV_INPUT_422 | YUV_INPUT_ORDER_YUYV,
		.csi_fmt_val	= CSI_WRDDR_TYPE_YUV422,
		.csi_yuv_order	= CSI_YUV_INPUT_ORDER_YUYV,
		.fmt_type	= CIF_FMT_TYPE_YUV,
		.field		= V4L2_FIELD_NONE,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_YUYV8_2X8,
		.dvp_fmt_val	= YUV_INPUT_422 | YUV_INPUT_ORDER_YUYV,
		.csi_fmt_val	= CSI_WRDDR_TYPE_YUV422,
		.csi_yuv_order	= CSI_YUV_INPUT_ORDER_YUYV,
		.fmt_type	= CIF_FMT_TYPE_YUV,
		.field		= V4L2_FIELD_INTERLACED,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_YVYU8_2X8,
		.dvp_fmt_val	= YUV_INPUT_422 | YUV_INPUT_ORDER_YVYU,
		.csi_fmt_val	= CSI_WRDDR_TYPE_YUV422,
		.csi_yuv_order	= CSI_YUV_INPUT_ORDER_YVYU,
		.fmt_type	= CIF_FMT_TYPE_YUV,
		.field		= V4L2_FIELD_NONE,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_YVYU8_2X8,
		.dvp_fmt_val	= YUV_INPUT_422 | YUV_INPUT_ORDER_YVYU,
		.csi_fmt_val	= CSI_WRDDR_TYPE_YUV422,
		.csi_yuv_order	= CSI_YUV_INPUT_ORDER_YVYU,
		.fmt_type	= CIF_FMT_TYPE_YUV,
		.field		= V4L2_FIELD_INTERLACED,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_UYVY8_2X8,
		.dvp_fmt_val	= YUV_INPUT_422 | YUV_INPUT_ORDER_UYVY,
		.csi_fmt_val	= CSI_WRDDR_TYPE_YUV422,
		.csi_yuv_order	= CSI_YUV_INPUT_ORDER_UYVY,
		.fmt_type	= CIF_FMT_TYPE_YUV,
		.field		= V4L2_FIELD_NONE,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_UYVY8_2X8,
		.dvp_fmt_val	= YUV_INPUT_422 | YUV_INPUT_ORDER_UYVY,
		.csi_fmt_val	= CSI_WRDDR_TYPE_YUV422,
		.csi_yuv_order	= CSI_YUV_INPUT_ORDER_UYVY,
		.fmt_type	= CIF_FMT_TYPE_YUV,
		.field		= V4L2_FIELD_INTERLACED,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_VYUY8_2X8,
		.dvp_fmt_val	= YUV_INPUT_422 | YUV_INPUT_ORDER_VYUY,
		.csi_fmt_val	= CSI_WRDDR_TYPE_YUV422,
		.csi_yuv_order	= CSI_YUV_INPUT_ORDER_VYUY,
		.fmt_type	= CIF_FMT_TYPE_YUV,
		.field		= V4L2_FIELD_NONE,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_VYUY8_2X8,
		.dvp_fmt_val	= YUV_INPUT_422 | YUV_INPUT_ORDER_VYUY,
		.csi_fmt_val	= CSI_WRDDR_TYPE_YUV422,
		.csi_yuv_order	= CSI_YUV_INPUT_ORDER_VYUY,
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

static struct v4l2_subdev *get_remote_sensor(struct rkcif_stream *stream, u16 *index)
{
	struct media_pad *local, *remote;
	struct media_entity *sensor_me;
	struct v4l2_subdev *sub = NULL;

	local = &stream->vnode.vdev.entity.pads[0];
	if (!local) {
		v4l2_err(&stream->cifdev->v4l2_dev,
			 "%s: video pad[0] is null\n", __func__);
		return NULL;
	}

	remote = media_entity_remote_pad(local);
	if (!remote) {
		v4l2_err(&stream->cifdev->v4l2_dev,
			 "%s: remote pad is null\n", __func__);
		return NULL;
	}

	if (index)
		*index = remote->index;

	sensor_me = remote->entity;

	sub = media_entity_to_v4l2_subdev(sensor_me);

	return sub;

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

static enum cif_reg_index get_dvp_reg_index_of_frm0_y_addr(int channel_id)
{
	enum cif_reg_index index;

	switch (channel_id) {
	case 0:
		index = CIF_REG_DVP_FRM0_ADDR_Y;
		break;
	case 1:
		index = CIF_REG_DVP_FRM0_ADDR_Y_ID1;
		break;
	case 2:
		index = CIF_REG_DVP_FRM0_ADDR_Y_ID2;
		break;
	case 3:
		index = CIF_REG_DVP_FRM0_ADDR_Y_ID3;
		break;
	default:
		index = CIF_REG_DVP_FRM0_ADDR_Y;
		break;
	}

	return index;
}

static enum cif_reg_index get_dvp_reg_index_of_frm1_y_addr(int channel_id)
{
	enum cif_reg_index index;

	switch (channel_id) {
	case 0:
		index = CIF_REG_DVP_FRM1_ADDR_Y;
		break;
	case 1:
		index = CIF_REG_DVP_FRM1_ADDR_Y_ID1;
		break;
	case 2:
		index = CIF_REG_DVP_FRM1_ADDR_Y_ID2;
		break;
	case 3:
		index = CIF_REG_DVP_FRM1_ADDR_Y_ID3;
		break;
	default:
		index = CIF_REG_DVP_FRM0_ADDR_Y;
		break;
	}

	return index;
}

static enum cif_reg_index get_dvp_reg_index_of_frm0_uv_addr(int channel_id)
{
	enum cif_reg_index index;

	switch (channel_id) {
	case 0:
		index = CIF_REG_DVP_FRM0_ADDR_UV;
		break;
	case 1:
		index = CIF_REG_DVP_FRM0_ADDR_UV_ID1;
		break;
	case 2:
		index = CIF_REG_DVP_FRM0_ADDR_UV_ID2;
		break;
	case 3:
		index = CIF_REG_DVP_FRM0_ADDR_UV_ID3;
		break;
	default:
		index = CIF_REG_DVP_FRM0_ADDR_UV;
		break;
	}

	return index;
}

static enum cif_reg_index get_dvp_reg_index_of_frm1_uv_addr(int channel_id)
{
	enum cif_reg_index index;

	switch (channel_id) {
	case 0:
		index = CIF_REG_DVP_FRM1_ADDR_UV;
		break;
	case 1:
		index = CIF_REG_DVP_FRM1_ADDR_UV_ID1;
		break;
	case 2:
		index = CIF_REG_DVP_FRM1_ADDR_UV_ID2;
		break;
	case 3:
		index = CIF_REG_DVP_FRM1_ADDR_UV_ID3;
		break;
	default:
		index = CIF_REG_DVP_FRM1_ADDR_UV;
		break;
	}

	return index;
}

/***************************** stream operations ******************************/
static int rkcif_assign_new_buffer_oneframe(struct rkcif_stream *stream,
					     enum rkcif_yuvaddr_state stat)
{
	struct rkcif_device *dev = stream->cifdev;
	struct rkcif_buffer *buffer = NULL;
	u32 frm_addr_y = CIF_REG_DVP_FRM0_ADDR_Y;
	u32 frm_addr_uv = CIF_REG_DVP_FRM0_ADDR_UV;
	int ret = 0;

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
		}
	} else if (stat == RKCIF_YUV_ADDR_STATE_UPDATE) {
		if (!list_empty(&stream->buf_head)) {
			if (stream->frame_phase == CIF_CSI_FRAME0_READY) {
				stream->curr_buf = list_first_entry(&stream->buf_head,
								    struct rkcif_buffer, queue);
				list_del(&stream->curr_buf->queue);
				buffer = stream->curr_buf;
			} else if (stream->frame_phase == CIF_CSI_FRAME1_READY) {
				stream->next_buf = list_first_entry(&stream->buf_head,
								    struct rkcif_buffer, queue);
				list_del(&stream->next_buf->queue);
				buffer = stream->next_buf;
			}
		} else {
			buffer = NULL;
		}

		if (stream->frame_phase == CIF_CSI_FRAME0_READY) {
			frm_addr_y = CIF_REG_DVP_FRM0_ADDR_Y;
			frm_addr_uv = CIF_REG_DVP_FRM0_ADDR_UV;
		} else if (stream->frame_phase == CIF_CSI_FRAME1_READY) {
			frm_addr_y = CIF_REG_DVP_FRM1_ADDR_Y;
			frm_addr_uv = CIF_REG_DVP_FRM1_ADDR_UV;
		}

		if (buffer) {
			rkcif_write_register(dev, frm_addr_y,
					     buffer->buff_addr[RKCIF_PLANE_Y]);
			rkcif_write_register(dev, frm_addr_uv,
					     buffer->buff_addr[RKCIF_PLANE_CBCR]);
		} else {
			ret = -EINVAL;
			v4l2_dbg(1, rkcif_debug, &dev->v4l2_dev,
				 "not active buffer, frame Drop\n");
		}
	}
	spin_unlock(&stream->vbq_lock);
	return ret;
}

static void rkcif_assign_new_buffer_init(struct rkcif_stream *stream,
					 int channel_id)
{
	struct rkcif_device *dev = stream->cifdev;
	struct v4l2_mbus_config *mbus_cfg = &dev->active_sensor->mbus;
	u32 frm0_addr_y, frm0_addr_uv;
	u32 frm1_addr_y, frm1_addr_uv;

	if (mbus_cfg->type == V4L2_MBUS_CSI2 ||
	    mbus_cfg->type == V4L2_MBUS_CCP2) {
		frm0_addr_y = get_reg_index_of_frm0_y_addr(channel_id);
		frm0_addr_uv = get_reg_index_of_frm0_uv_addr(channel_id);
		frm1_addr_y = get_reg_index_of_frm1_y_addr(channel_id);
		frm1_addr_uv = get_reg_index_of_frm1_uv_addr(channel_id);
	} else {
		frm0_addr_y = get_dvp_reg_index_of_frm0_y_addr(channel_id);
		frm0_addr_uv = get_dvp_reg_index_of_frm0_uv_addr(channel_id);
		frm1_addr_y = get_dvp_reg_index_of_frm1_y_addr(channel_id);
		frm1_addr_uv = get_dvp_reg_index_of_frm1_uv_addr(channel_id);
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
		if (stream->cif_fmt_out->fmt_type != CIF_FMT_TYPE_RAW)
			rkcif_write_register(dev, frm0_addr_uv,
					     stream->curr_buf->buff_addr[RKCIF_PLANE_CBCR]);
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
		if (stream->cif_fmt_out->fmt_type != CIF_FMT_TYPE_RAW)
			rkcif_write_register(dev, frm1_addr_uv,
					     stream->next_buf->buff_addr[RKCIF_PLANE_CBCR]);
	}

	stream->is_dvp_yuv_addr_init = true;

	/* for BT.656/BT.1120 multi channels function,
	 * yuv addr of unused channel must be set
	 */
	if (mbus_cfg->type == V4L2_MBUS_BT656) {
		int ch_id;

		for (ch_id = 0; ch_id < RKCIF_MAX_STREAM_DVP; ch_id++) {
			if (dev->stream[ch_id].is_dvp_yuv_addr_init)
				continue;
			if (stream->curr_buf) {
				rkcif_write_register(dev,
						     get_dvp_reg_index_of_frm0_y_addr(ch_id),
						     stream->curr_buf->buff_addr[RKCIF_PLANE_Y]);
				rkcif_write_register(dev,
						     get_dvp_reg_index_of_frm0_uv_addr(ch_id),
						     stream->curr_buf->buff_addr[RKCIF_PLANE_CBCR]);
			}
			if (stream->next_buf) {
				rkcif_write_register(dev,
						     get_dvp_reg_index_of_frm1_y_addr(ch_id),
						     stream->next_buf->buff_addr[RKCIF_PLANE_Y]);
				rkcif_write_register(dev,
						     get_dvp_reg_index_of_frm1_uv_addr(ch_id),
						     stream->next_buf->buff_addr[RKCIF_PLANE_CBCR]);
			}
		}
	}

	spin_unlock(&stream->vbq_lock);
}

static int rkcif_assign_new_buffer_update(struct rkcif_stream *stream,
					   int channel_id)
{
	struct rkcif_device *dev = stream->cifdev;
	struct v4l2_mbus_config *mbus_cfg = &dev->active_sensor->mbus;
	struct rkcif_buffer *buffer = NULL;
	u32 frm_addr_y, frm_addr_uv;
	int ret = 0;

	if (mbus_cfg->type == V4L2_MBUS_CSI2 ||
	    mbus_cfg->type == V4L2_MBUS_CCP2) {
		frm_addr_y = stream->frame_phase & CIF_CSI_FRAME0_READY ?
			     get_reg_index_of_frm0_y_addr(channel_id) :
			     get_reg_index_of_frm1_y_addr(channel_id);
		frm_addr_uv = stream->frame_phase & CIF_CSI_FRAME0_READY ?
			      get_reg_index_of_frm0_uv_addr(channel_id) :
			      get_reg_index_of_frm1_uv_addr(channel_id);
	} else {
		frm_addr_y = stream->frame_phase & CIF_CSI_FRAME0_READY ?
			     get_dvp_reg_index_of_frm0_y_addr(channel_id) :
			     get_dvp_reg_index_of_frm1_y_addr(channel_id);
		frm_addr_uv = stream->frame_phase & CIF_CSI_FRAME0_READY ?
			      get_dvp_reg_index_of_frm0_uv_addr(channel_id) :
			      get_dvp_reg_index_of_frm1_uv_addr(channel_id);
	}

	spin_lock(&stream->vbq_lock);
	if (!list_empty(&stream->buf_head)) {
		if (stream->frame_phase == CIF_CSI_FRAME0_READY) {
			stream->curr_buf = list_first_entry(&stream->buf_head,
							    struct rkcif_buffer, queue);
			if (stream->curr_buf) {
				list_del(&stream->curr_buf->queue);
				buffer = stream->curr_buf;
			}
		} else if (stream->frame_phase == CIF_CSI_FRAME1_READY) {
			stream->next_buf = list_first_entry(&stream->buf_head,
							    struct rkcif_buffer, queue);
			if (stream->next_buf) {
				list_del(&stream->next_buf->queue);
				buffer = stream->next_buf;
			}
		}
	} else {
		buffer = NULL;
	}
	spin_unlock(&stream->vbq_lock);

	if (buffer) {
		rkcif_write_register(dev, frm_addr_y,
				     buffer->buff_addr[RKCIF_PLANE_Y]);
		if (stream->cif_fmt_out->fmt_type != CIF_FMT_TYPE_RAW)
			rkcif_write_register(dev, frm_addr_uv,
					     buffer->buff_addr[RKCIF_PLANE_CBCR]);
	} else {
		ret = -EINVAL;
		v4l2_info(&dev->v4l2_dev,
			 "not active buffer, skip current frame, %s stream[%d]\n",
			 (mbus_cfg->type == V4L2_MBUS_CSI2 ||
			  mbus_cfg->type == V4L2_MBUS_CCP2) ? "mipi/lvds" : "dvp",
			  stream->id);
	}
	return ret;
}

static int rkcif_assign_new_buffer_pingpong(struct rkcif_stream *stream,
					     int init, int channel_id)
{
	int ret = 0;

	if (init)
		rkcif_assign_new_buffer_init(stream, channel_id);
	else
		ret = rkcif_assign_new_buffer_update(stream, channel_id);
	return ret;
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

		if (dev->hdr.mode == HDR_X2) {
			if (channel->id == RKCIF_STREAM_MIPI_ID1)
				frm_sync_code = &lvds_cfg->frm_sync_code[LVDS_CODE_GRP_SHORT];
			else
				frm_sync_code = &lvds_cfg->frm_sync_code[LVDS_CODE_GRP_LONG];
		} else if (dev->hdr.mode == HDR_X3) {
			if (channel->id == RKCIF_STREAM_MIPI_ID1)
				frm_sync_code = &lvds_cfg->frm_sync_code[LVDS_CODE_GRP_MEDIUM];
			else if (channel->id == RKCIF_STREAM_MIPI_ID2)
				frm_sync_code = &lvds_cfg->frm_sync_code[LVDS_CODE_GRP_SHORT];
			else
				frm_sync_code = &lvds_cfg->frm_sync_code[LVDS_CODE_GRP_LONG];
		}

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

	channel->fmt_val = stream->cif_fmt_out->csi_fmt_val;

	channel->cmd_mode_en = 0; /* default use DSI Video Mode */

	if (stream->crop_enable) {
		channel->crop_en = 1;

		if (channel->fmt_val == CSI_WRDDR_TYPE_RGB888)
			channel->crop_st_x = 3 * stream->crop[CROP_SRC_ACT].left;
		else
			channel->crop_st_x = stream->crop[CROP_SRC_ACT].left;

		channel->crop_st_y = stream->crop[CROP_SRC_ACT].top;
		channel->width = stream->crop[CROP_SRC_ACT].width;
		channel->height = stream->crop[CROP_SRC_ACT].height;
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
	 * needs aligned with :ALIGN(bits_per_pixel * width * 2, 8), to optimize reading and
	 * writing of ddr, aliged with 256
	 */
	if (fmt->fmt_type == CIF_FMT_TYPE_RAW && stream->is_compact) {
		channel->virtual_width = ALIGN(channel->width * fmt->raw_bpp / 8, 256);
	} else {
		if (fmt->fmt_type == CIF_FMT_TYPE_RAW)
			channel->virtual_width = ALIGN(channel->width * 2, 8);
		else
			channel->virtual_width = ALIGN(channel->width * fmt->bpp[0] / 8, 8);
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

	rkcif_write_register(dev, CIF_REG_MIPI_WATER_LINE,
			     CIF_MIPI_LVDS_SW_WATER_LINE_25_RK1808 |
			     CIF_MIPI_LVDS_SW_WATER_LINE_ENABLE_RK1808 |
			     CIF_MIPI_LVDS_SW_HURRY_VALUE_RK1808(0x3) |
			     CIF_MIPI_LVDS_SW_HURRY_ENABLE_RK1808);

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
	rkcif_assign_new_buffer_pingpong(stream,
					 RKCIF_YUV_ADDR_STATE_INIT,
					 channel->id);

	if (mbus_type  == V4L2_MBUS_CSI2) {
		val = CSI_ENABLE_CAPTURE | channel->fmt_val |
		      channel->cmd_mode_en << 4 | CSI_ENABLE_CROP |
		      channel->id << 8 | channel->data_type << 10;

		if (stream->is_compact)
			val |= CSI_ENABLE_MIPI_COMPACT;
		else
			val &= ~CSI_ENABLE_MIPI_COMPACT;

		if (stream->cifdev->chip_id >= CHIP_RK3568_CIF)
			val |= stream->cif_fmt_in->csi_yuv_order;
	} else if (mbus_type  == V4L2_MBUS_CCP2) {
		rkcif_csi_set_lvds_sav_eav(stream, channel);
		val = LVDS_ENABLE_CAPTURE | LVDS_MODE(channel->lvds_cfg.mode) |
		      LVDS_MAIN_LANE(0) | LVDS_FID(0) |
		      LVDS_LANES_ENABLED(dev->active_sensor->lanes);

		if (stream->is_compact)
			val |= LVDS_COMPACT;
		else
			val &= ~LVDS_COMPACT;
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

	if (stream->state != RKCIF_STATE_RESET_IN_STREAMING)
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

		ret = v4l2_subdev_call(dev->terminal_sensor.sd, core,
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

static bool rkcif_is_extending_line_for_height(struct rkcif_device *dev,
							 struct rkcif_stream *stream,
							 const struct cif_input_fmt *fmt)
{
	bool is_extended = false;
	struct rkmodule_hdr_cfg hdr_cfg;
	int ret;

	if (dev->chip_id == CHIP_RV1126_CIF ||
	    dev->chip_id == CHIP_RV1126_CIF_LITE) {
		if (dev->terminal_sensor.sd) {
			ret = v4l2_subdev_call(dev->terminal_sensor.sd,
					       core, ioctl,
					       RKMODULE_GET_HDR_CFG,
					       &hdr_cfg);
			if (!ret)
				dev->hdr.mode = hdr_cfg.hdr_mode;
			else
				dev->hdr.mode = NO_HDR;
		}

		if (fmt && fmt->fmt_type == CIF_FMT_TYPE_RAW) {
			if ((dev->hdr.mode == HDR_X2 &&
			    stream->id == RKCIF_STREAM_MIPI_ID1) ||
			    (dev->hdr.mode == HDR_X3 &&
			     stream->id == RKCIF_STREAM_MIPI_ID2) ||
			     (dev->hdr.mode == NO_HDR)) {
				is_extended = true;
			}
		}
	}

	return is_extended;
}

static int rkcif_queue_setup(struct vb2_queue *queue,
			     unsigned int *num_buffers,
			     unsigned int *num_planes,
			     unsigned int sizes[],
			     struct device *alloc_ctxs[])
{
	struct rkcif_stream *stream = queue->drv_priv;
	struct rkcif_extend_info *extend_line = &stream->extend_line;
	struct rkcif_device *dev = stream->cifdev;
	const struct v4l2_pix_format_mplane *pixm = NULL;
	const struct cif_output_fmt *cif_fmt;
	const struct cif_input_fmt *in_fmt;
	bool is_extended = false;
	u32 i, height;

	pixm = &stream->pixm;
	cif_fmt = stream->cif_fmt_out;
	in_fmt = stream->cif_fmt_in;

	*num_planes = cif_fmt->mplanes;

	if (stream->crop_enable)
		height = stream->crop[CROP_SRC_ACT].height;
	else
		height = pixm->height;

	is_extended = rkcif_is_extending_line_for_height(dev, stream, in_fmt);
	if (is_extended && extend_line->is_extended) {
		height = extend_line->pixm.height;
		pixm = &extend_line->pixm;
	}

	for (i = 0; i < cif_fmt->mplanes; i++) {
		const struct v4l2_plane_pix_format *plane_fmt;
		int h = round_up(height, MEMORY_ALIGN_ROUND_UP_HEIGHT);

		plane_fmt = &pixm->plane_fmt[i];
		sizes[i] = plane_fmt->sizeimage / height * h;
	}

	v4l2_dbg(1, rkcif_debug, &dev->v4l2_dev, "%s count %d, size %d, extended(%d, %d)\n",
		 v4l2_type_names[queue->type], *num_buffers, sizes[0],
		 is_extended, extend_line->is_extended);

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
	struct rkcif_hw *hw_dev = stream->cifdev->hw_dev;
	unsigned long lock_flags = 0;
	int i;

	memset(cifbuf->buff_addr, 0, sizeof(cifbuf->buff_addr));
	/* If mplanes > 1, every c-plane has its own m-plane,
	 * otherwise, multiple c-planes are in the same m-plane
	 */
	for (i = 0; i < fmt->mplanes; i++) {
		void *addr = vb2_plane_vaddr(vb, i);

		if (hw_dev->iommu_en) {
			struct sg_table *sgt = vb2_dma_sg_plane_desc(vb, i);

			cifbuf->buff_addr[i] = sg_dma_address(sgt->sgl);
		} else {
			cifbuf->buff_addr[i] = vb2_dma_contig_plane_dma_addr(vb, i);
		}
		if (rkcif_debug && addr && !hw_dev->iommu_en) {
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

static void rkcif_do_cru_reset(struct rkcif_device *dev)
{
	struct rkcif_hw *cif_hw = dev->hw_dev;

	unsigned int val, i;

	if (dev->luma_vdev.enable)
		rkcif_stop_luma(&dev->luma_vdev);

	if (dev->hdr.mode != NO_HDR) {
		if (dev->chip_id == CHIP_RK1808_CIF) {
			val = rkcif_read_register(dev, CIF_REG_MIPI_WATER_LINE);
			val |= CIF_MIPI_LVDS_SW_DMA_IDLE_RK1808;
			rkcif_write_register(dev, CIF_REG_MIPI_WATER_LINE, val);
		} else {
			val = rkcif_read_register(dev, CIF_REG_MIPI_LVDS_CTRL);
			val |= CIF_MIPI_LVDS_SW_DMA_IDLE;
			rkcif_write_register(dev, CIF_REG_MIPI_LVDS_CTRL, val);
		}
		udelay(5);
	}

	for (i = 0; i < ARRAY_SIZE(cif_hw->cif_rst); i++)
		if (cif_hw->cif_rst[i])
			reset_control_assert(cif_hw->cif_rst[i]);

	udelay(10);

	for (i = 0; i < ARRAY_SIZE(cif_hw->cif_rst); i++)
		if (cif_hw->cif_rst[i])
			reset_control_deassert(cif_hw->cif_rst[i]);

	if (cif_hw->iommu_en) {
		struct iommu_domain *domain;

		domain = iommu_get_domain_for_dev(cif_hw->dev);
		if (domain) {
			domain->ops->detach_dev(domain, cif_hw->dev);
			domain->ops->attach_dev(domain, cif_hw->dev);
		}
	}
}

static void rkcif_release_rdbk_buf(struct rkcif_stream *stream)
{
	struct rkcif_device *dev = stream->cifdev;
	struct rkcif_buffer *rdbk_buf;
	struct rkcif_buffer *tmp_buf;
	unsigned long lock_flags = 0;
	bool has_added;
	int index = 0;

	if (stream->id == RKCIF_STREAM_MIPI_ID0)
		index = RDBK_L;
	else if (stream->id == RKCIF_STREAM_MIPI_ID1)
		index = RDBK_M;
	else if (stream->id == RKCIF_STREAM_MIPI_ID2)
		index = RDBK_S;
	else
		return;

	spin_lock_irqsave(&dev->hdr_lock, lock_flags);
	rdbk_buf = dev->rdbk_buf[index];
	if (rdbk_buf) {
		if (rdbk_buf != stream->curr_buf &&
		    rdbk_buf != stream->next_buf) {

			has_added = false;

			list_for_each_entry(tmp_buf, &stream->buf_head, queue) {
				if (tmp_buf == rdbk_buf) {
					has_added = true;
					break;
				}
			}

			if (!has_added)
				list_add_tail(&rdbk_buf->queue, &stream->buf_head);
		}
		dev->rdbk_buf[index] = NULL;
	}
	spin_unlock_irqrestore(&dev->hdr_lock, lock_flags);

}

static void rkcif_stop_streaming(struct vb2_queue *queue)
{
	struct rkcif_stream *stream = queue->drv_priv;
	struct rkcif_vdev_node *node = &stream->vnode;
	struct rkcif_device *dev = stream->cifdev;
	struct v4l2_device *v4l2_dev = &dev->v4l2_dev;
	struct rkcif_buffer *buf = NULL;
	int ret;
	struct rkcif_hw *hw_dev = dev->hw_dev;
	bool can_reset = true;
	int i;

	mutex_lock(&dev->stream_lock);

	v4l2_info(&dev->v4l2_dev, "stream[%d] start stopping\n", stream->id);

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

	dev->is_start_hdr = false;
	stream->is_dvp_yuv_addr_init = false;

	/* release buffers */
	if (stream->curr_buf)
		list_add_tail(&stream->curr_buf->queue, &stream->buf_head);

	if (stream->next_buf &&
	    stream->next_buf != stream->curr_buf)
		list_add_tail(&stream->next_buf->queue, &stream->buf_head);

	if (dev->hdr.mode != NO_HDR)
		rkcif_release_rdbk_buf(stream);

	stream->curr_buf = NULL;
	stream->next_buf = NULL;

	while (!list_empty(&stream->buf_head)) {
		buf = list_first_entry(&stream->buf_head,
				       struct rkcif_buffer, queue);
		list_del(&buf->queue);
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
	}

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

	for (i = 0; i < hw_dev->dev_num; i++) {
		if (atomic_read(&hw_dev->cif_dev[i]->pipe.stream_cnt) != 0) {
			can_reset = false;
			break;
		}
	}

	if (dev->can_be_reset && can_reset) {
		rkcif_do_cru_reset(dev);
		dev->can_be_reset = false;
		dev->reset_work_cancel = true;
	}
	pm_runtime_put(dev->dev);

	v4l2_info(&dev->v4l2_dev, "stream[%d] stopping finished\n", stream->id);

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
	struct rkcif_sensor_info *terminal_sensor = &dev->terminal_sensor;
	__u32 intf = BT656_STD_RAW;
	u32 mode = INPUT_MODE_YUV;
	v4l2_std_id std;
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
			if (dev->chip_id >= CHIP_RK3568_CIF) {
				if (sensor_info->mbus.type == V4L2_MBUS_BT656)
					mode = INPUT_MODE_BT656_YUV422;
				else
					mode = INPUT_MODE_YUV;
			} else {
				mode = INPUT_MODE_YUV;
			}
			break;
		case CIF_FMT_TYPE_RAW:
			if (dev->chip_id >= CHIP_RK3568_CIF) {
				ret = v4l2_subdev_call(terminal_sensor->sd,
						       core, ioctl,
						       RKMODULE_GET_BT656_INTF_TYPE,
						       &intf);
				if (!ret) {
					if (intf == BT656_SONY_RAW)
						mode = INPUT_MODE_SONY_RAW;
					else
						mode = INPUT_MODE_RAW;
				} else {
					mode = INPUT_MODE_RAW;
				}
			} else {
				mode = INPUT_MODE_RAW;
			}
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
		case V4L2_PIX_FMT_GREY:
		case V4L2_PIX_FMT_Y16:
			bpp = fmt->bpp[plane_index];
			break;
		case V4L2_PIX_FMT_RGB24:
		case V4L2_PIX_FMT_RGB565:
		case V4L2_PIX_FMT_BGR666:
		case V4L2_PIX_FMT_SRGGB8:
		case V4L2_PIX_FMT_SGRBG8:
		case V4L2_PIX_FMT_SGBRG8:
		case V4L2_PIX_FMT_SBGGR8:
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

static void rkcif_sync_crop_info(struct rkcif_stream *stream)
{
	struct rkcif_device *dev = stream->cifdev;
	struct v4l2_subdev_selection input_sel;
	int ret;

	if (dev->terminal_sensor.sd) {
		input_sel.target = V4L2_SEL_TGT_CROP_BOUNDS;

		ret = v4l2_subdev_call(dev->terminal_sensor.sd,
				       pad, get_selection, NULL,
				       &input_sel);
		if (!ret) {
			stream->crop[CROP_SRC_SENSOR] = input_sel.r;
			stream->crop_enable = true;
			stream->crop_mask |= CROP_SRC_SENSOR_MASK;
			dev->terminal_sensor.selection = input_sel;
		} else {
			dev->terminal_sensor.selection.r = dev->terminal_sensor.raw_rect;
		}
	}

	if ((stream->crop_mask & 0x3) == (CROP_SRC_USR_MASK | CROP_SRC_SENSOR_MASK)) {
		if (stream->crop[CROP_SRC_USR].left + stream->crop[CROP_SRC_USR].width >
		    stream->crop[CROP_SRC_SENSOR].width ||
		    stream->crop[CROP_SRC_USR].top + stream->crop[CROP_SRC_USR].height >
		    stream->crop[CROP_SRC_SENSOR].height)
			stream->crop[CROP_SRC_USR] = stream->crop[CROP_SRC_SENSOR];
	}

	if (stream->crop_mask & CROP_SRC_USR_MASK) {
		stream->crop[CROP_SRC_ACT] = stream->crop[CROP_SRC_USR];
		if (stream->crop_mask & CROP_SRC_SENSOR_MASK) {
			stream->crop[CROP_SRC_ACT].left = stream->crop[CROP_SRC_USR].left +
							  stream->crop[CROP_SRC_SENSOR].left;
			stream->crop[CROP_SRC_ACT].top = stream->crop[CROP_SRC_USR].top +
							  stream->crop[CROP_SRC_SENSOR].top;
		}
	} else {
		stream->crop[CROP_SRC_ACT] = stream->crop[CROP_SRC_SENSOR];
	}
}

/**rkcif_sanity_check_fmt - check fmt for setting
 * @stream - the stream for setting
 * @s_crop - the crop information
 */
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
		crop = &stream->crop[CROP_SRC_ACT];

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

int rkcif_update_sensor_info(struct rkcif_stream *stream)
{
	struct rkcif_sensor_info *sensor, *terminal_sensor;
	struct v4l2_subdev *sensor_sd;
	int ret = 0;

	sensor_sd = get_remote_sensor(stream, NULL);
	if (!sensor_sd) {
		v4l2_err(&stream->cifdev->v4l2_dev,
			 "%s: stream[%d] get remote sensor_sd failed!\n",
			 __func__, stream->id);
		return -ENODEV;
	}

	sensor = sd_to_sensor(stream->cifdev, sensor_sd);
	if (!sensor) {
		v4l2_err(&stream->cifdev->v4l2_dev,
			 "%s: stream[%d] get remote sensor failed!\n",
			 __func__, stream->id);
		return -ENODEV;
	}
	ret = v4l2_subdev_call(sensor->sd, video, g_mbus_config,
			       &sensor->mbus);
	if (ret && ret != -ENOIOCTLCMD) {
		v4l2_err(&stream->cifdev->v4l2_dev,
			 "%s: get remote %s mbus failed!\n", __func__, sensor->sd->name);
		return ret;
	}

	stream->cifdev->active_sensor = sensor;

	terminal_sensor = &stream->cifdev->terminal_sensor;
	get_remote_terminal_sensor(stream, &terminal_sensor->sd);
	if (terminal_sensor->sd) {
		ret = v4l2_subdev_call(terminal_sensor->sd, video, g_mbus_config,
				       &terminal_sensor->mbus);
		if (ret && ret != -ENOIOCTLCMD) {
			v4l2_err(&stream->cifdev->v4l2_dev,
				 "%s: get terminal %s mbus failed!\n",
				 __func__, terminal_sensor->sd->name);
			return ret;
		}
	}

	if (terminal_sensor->mbus.type == V4L2_MBUS_CSI2 ||
	    terminal_sensor->mbus.type == V4L2_MBUS_CCP2) {
		switch (terminal_sensor->mbus.flags & V4L2_MBUS_CSI2_LANES) {
		case V4L2_MBUS_CSI2_1_LANE:
			terminal_sensor->lanes = 1;
			break;
		case V4L2_MBUS_CSI2_2_LANE:
			terminal_sensor->lanes = 2;
			break;
		case V4L2_MBUS_CSI2_3_LANE:
			terminal_sensor->lanes = 3;
			break;
		case V4L2_MBUS_CSI2_4_LANE:
			terminal_sensor->lanes = 4;
			break;
		default:
			v4l2_err(&stream->cifdev->v4l2_dev, "%s:get sd:%s lane num failed!\n",
				 __func__,
				 terminal_sensor->sd ?
				 terminal_sensor->sd->name : "null");
			return -EINVAL;
		}
	}

	return ret;
}

static int rkcif_stream_start(struct rkcif_stream *stream)
{
	u32 val, mbus_flags, href_pol, vsync_pol,
	    xfer_mode = 0, yc_swap = 0, inputmode = 0,
	    mipimode = 0, workmode = 0, multi_id = 0,
	    multi_id_en = BT656_1120_MULTI_ID_DISABLE,
	    multi_id_mode = BT656_1120_MULTI_ID_MODE_1,
	    multi_id_sel = BT656_1120_MULTI_ID_SEL_LSB,
	    bt1120_edge_mode = BT1120_CLOCK_SINGLE_EDGES,
	    bt1120_flags = 0;
	struct rkmodule_bt656_mbus_info bt1120_info;
	struct rkcif_device *dev = stream->cifdev;
	struct rkcif_sensor_info *sensor_info;
	struct v4l2_mbus_config *mbus;
	struct rkcif_dvp_sof_subdev *sof_sd = &dev->dvp_sof_subdev;
	const struct cif_output_fmt *fmt;

	if (stream->state != RKCIF_STATE_RESET_IN_STREAMING)
		stream->frame_idx = 0;

	sensor_info = dev->active_sensor;
	mbus = &sensor_info->mbus;

	if (sensor_info->sd && mbus->type == V4L2_MBUS_BT656) {
		int ret;

		multi_id_en = BT656_1120_MULTI_ID_ENABLE;

		ret = v4l2_subdev_call(sensor_info->sd,
				       core, ioctl,
				       RKMODULE_GET_BT656_MBUS_INFO,
				       &bt1120_info);
		if (ret) {
			v4l2_warn(&dev->v4l2_dev,
				  "waring: no muti channel info for BT.656\n");
		} else {
			bt1120_flags = bt1120_info.flags;
			if (bt1120_flags & RKMODULE_CAMERA_BT656_PARSE_ID_LSB)
				multi_id_sel = BT656_1120_MULTI_ID_SEL_LSB;
			else
				multi_id_sel = BT656_1120_MULTI_ID_SEL_MSB;

			if (((bt1120_flags & RKMODULE_CAMERA_BT656_CHANNELS) >> 2) > 3)
				multi_id_mode = BT656_1120_MULTI_ID_MODE_4;
			else if (((bt1120_flags & RKMODULE_CAMERA_BT656_CHANNELS) >> 2) > 1)
				multi_id_mode = BT656_1120_MULTI_ID_MODE_2;

			multi_id = DVP_SW_MULTI_ID(stream->id, stream->id, bt1120_info.id_en_bits);
			rkcif_write_register_or(dev, CIF_REG_DVP_MULTI_ID, multi_id);
		}
	}

	mbus_flags = mbus->flags;
	if ((mbus_flags & CIF_DVP_PCLK_DUAL_EDGE) == CIF_DVP_PCLK_DUAL_EDGE) {
		bt1120_edge_mode = BT1120_CLOCK_DOUBLE_EDGES;
		rkcif_enable_dvp_clk_dual_edge(dev, true);
	} else {
		bt1120_edge_mode = BT1120_CLOCK_SINGLE_EDGES;
		rkcif_enable_dvp_clk_dual_edge(dev, false);
	}

	if (mbus_flags & V4L2_MBUS_PCLK_SAMPLE_RISING)
		rkcif_config_dvp_clk_sampling_edge(dev, RKCIF_CLK_RISING);
	else
		rkcif_config_dvp_clk_sampling_edge(dev, RKCIF_CLK_FALLING);

	href_pol = (mbus_flags & V4L2_MBUS_HSYNC_ACTIVE_HIGH) ?
		    HSY_HIGH_ACTIVE : HSY_LOW_ACTIVE;
	vsync_pol = (mbus_flags & V4L2_MBUS_VSYNC_ACTIVE_HIGH) ?
		     VSY_HIGH_ACTIVE : VSY_LOW_ACTIVE;

	inputmode = rkcif_determine_input_mode(stream);
	if (dev->chip_id <= CHIP_RK1808_CIF) {
		if (inputmode == INPUT_MODE_BT1120) {
			if (stream->cif_fmt_in->field == V4L2_FIELD_NONE)
				xfer_mode = BT1120_TRANSMIT_PROGRESS;
			else
				xfer_mode = BT1120_TRANSMIT_INTERFACE;
			if (CIF_FETCH_IS_Y_FIRST(stream->cif_fmt_in->dvp_fmt_val))
				yc_swap = BT1120_YC_SWAP;
		}
	} else {
		if (sensor_info->mbus.type == V4L2_MBUS_BT656) {
			if (stream->cif_fmt_in->field == V4L2_FIELD_NONE)
				xfer_mode = BT1120_TRANSMIT_PROGRESS;
			else
				xfer_mode = BT1120_TRANSMIT_INTERFACE;
		}

		if (inputmode == INPUT_MODE_BT1120) {
			if (CIF_FETCH_IS_Y_FIRST(stream->cif_fmt_in->dvp_fmt_val))
				yc_swap = BT1120_YC_SWAP;
		}
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
	}

	val = vsync_pol | href_pol | inputmode | mipimode
	      | stream->cif_fmt_out->fmt_val
	      | stream->cif_fmt_in->dvp_fmt_val
	      | xfer_mode | yc_swap | multi_id_en
	      | multi_id_sel | multi_id_mode | bt1120_edge_mode;
	rkcif_write_register(dev, CIF_REG_DVP_FOR, val);

	val = stream->pixm.width;
	if (stream->cif_fmt_in->fmt_type == CIF_FMT_TYPE_RAW) {
		fmt = find_output_fmt(stream, stream->pixm.pixelformat);
		val = stream->pixm.width * rkcif_cal_raw_vir_line_ratio(fmt);
	}
	rkcif_write_register(dev, CIF_REG_DVP_VIR_LINE_WIDTH, val);
	rkcif_write_register(dev, CIF_REG_DVP_SET_SIZE,
			     stream->pixm.width | (stream->pixm.height << 16));

	if (stream->crop_enable) {
		dev->channels[stream->id].crop_en = 1;
		dev->channels[stream->id].crop_st_x = stream->crop[CROP_SRC_ACT].left;
		dev->channels[stream->id].crop_st_y = stream->crop[CROP_SRC_ACT].top;
		dev->channels[stream->id].width = stream->crop[CROP_SRC_ACT].width;
		dev->channels[stream->id].height = stream->crop[CROP_SRC_ACT].height;
	} else {
		dev->channels[stream->id].crop_st_y = 0;
		dev->channels[stream->id].crop_st_x = 0;
		dev->channels[stream->id].width = stream->pixm.width;
		dev->channels[stream->id].height = stream->pixm.height;
		dev->channels[stream->id].crop_en = 0;
	}

	rkcif_write_register(dev, CIF_REG_DVP_CROP,
			     dev->channels[stream->id].crop_st_y << CIF_CROP_Y_SHIFT |
			     dev->channels[stream->id].crop_st_x);

	if (atomic_read(&dev->pipe.stream_cnt) <= 1)
		rkcif_write_register(dev, CIF_REG_DVP_FRAME_STATUS, FRAME_STAT_CLS);

	rkcif_write_register(dev, CIF_REG_DVP_INTSTAT, INTSTAT_CLS);
	rkcif_write_register(dev, CIF_REG_DVP_SCL_CTRL, rkcif_scl_ctl(stream));

	if (dev->chip_id < CHIP_RK1808_CIF)
		rkcif_assign_new_buffer_oneframe(stream,
						 RKCIF_YUV_ADDR_STATE_INIT);
	else
		rkcif_assign_new_buffer_pingpong(stream,
						 RKCIF_YUV_ADDR_STATE_INIT,
						 stream->id);

	rkcif_write_register_or(dev, CIF_REG_DVP_INTEN,
				DVP_DMA_END_INTSTAT(stream->id) |
				INTSTAT_ERR | PST_INF_FRAME_END);

	/* enable line int for sof */
	rkcif_write_register(dev, CIF_REG_DVP_LINE_INT_NUM, 0x1);
	rkcif_write_register_or(dev, CIF_REG_DVP_INTEN, LINE_INT_EN);

	if (dev->workmode == RKCIF_WORKMODE_ONEFRAME)
		workmode = MODE_ONEFRAME;
	else if (dev->workmode == RKCIF_WORKMODE_PINGPONG)
		workmode = MODE_PINGPONG;
	else
		workmode = MODE_LINELOOP;

	if (inputmode == INPUT_MODE_BT1120) {
		workmode = MODE_PINGPONG;
		dev->workmode = RKCIF_WORKMODE_PINGPONG;
	}

	rkcif_write_register(dev, CIF_REG_DVP_CTRL,
			     AXI_BURST_16 | workmode | ENABLE_CAPTURE);

	atomic_set(&sof_sd->frm_sync_seq, 0);
	stream->state = RKCIF_STATE_STREAMING;
	stream->cifdev->dvp_sof_in_oneframe = 0;

	return 0;
}

static int rkcif_start_streaming(struct vb2_queue *queue, unsigned int count)
{
	struct rkcif_stream *stream = queue->drv_priv;
	struct rkcif_vdev_node *node = &stream->vnode;
	struct rkcif_device *dev = stream->cifdev;
	struct v4l2_device *v4l2_dev = &dev->v4l2_dev;
	struct rkcif_sensor_info *sensor_info = dev->active_sensor;
	struct rkcif_sensor_info *terminal_sensor = &dev->terminal_sensor;
	struct rkmodule_hdr_cfg hdr_cfg;
	int rkmodule_stream_seq = RKMODULE_START_STREAM_DEFAULT;
	int ret;

	mutex_lock(&dev->stream_lock);

	if (WARN_ON(stream->state != RKCIF_STATE_READY)) {
		ret = -EBUSY;
		v4l2_err(v4l2_dev, "stream in busy state\n");
		goto destroy_buf;
	}

	stream->fs_cnt_in_single_frame = 0;

	if (dev->active_sensor) {
		ret = rkcif_update_sensor_info(stream);
		if (ret < 0) {
			v4l2_err(v4l2_dev,
				 "update sensor info failed %d\n",
				 ret);
			goto out;
		}
	}

	if (terminal_sensor->sd) {
		ret = v4l2_subdev_call(terminal_sensor->sd,
				       core, ioctl,
				       RKMODULE_GET_HDR_CFG,
				       &hdr_cfg);
		if (!ret)
			dev->hdr.mode = hdr_cfg.hdr_mode;
		else
			dev->hdr.mode = NO_HDR;

		ret = v4l2_subdev_call(terminal_sensor->sd,
				       video, g_frame_interval, &terminal_sensor->fi);
		if (ret)
			terminal_sensor->fi.interval = (struct v4l2_fract) {1, 30};

		ret = v4l2_subdev_call(terminal_sensor->sd,
				       core, ioctl,
				       RKMODULE_GET_START_STREAM_SEQ,
				       &rkmodule_stream_seq);
		if (ret)
			rkmodule_stream_seq = RKMODULE_START_STREAM_DEFAULT;

		rkcif_sync_crop_info(stream);
	}

	ret = rkcif_sanity_check_fmt(stream, NULL);
	if (ret < 0)
		goto destroy_buf;

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
	if (sensor_info->mbus.type == V4L2_MBUS_PARALLEL ||
	    rkmodule_stream_seq == RKMODULE_START_STREAM_FRONT) {
		ret = dev->pipe.set_stream(&dev->pipe, true);
		if (ret < 0)
			goto runtime_put;
	}

	if (dev->chip_id >= CHIP_RK1808_CIF) {
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

	if (sensor_info->mbus.type != V4L2_MBUS_PARALLEL &&
	    rkmodule_stream_seq != RKMODULE_START_STREAM_FRONT) {
		ret = dev->pipe.set_stream(&dev->pipe, true);
		if (ret < 0)
			goto stop_stream;
	}

	if (dev->hdr.mode == NO_HDR) {
		if (dev->stream[RKCIF_STREAM_MIPI_ID0].state == RKCIF_STATE_STREAMING) {
			rkcif_start_luma(&dev->luma_vdev,
					dev->stream[RKCIF_STREAM_MIPI_ID0].cif_fmt_in);
		}
	} else if (dev->hdr.mode == HDR_X2) {
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

	dev->reset_work_cancel = false;

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
	struct rkcif_hw *hw_dev = stream->cifdev->hw_dev;

	q->type = buf_type;
	q->io_modes = VB2_MMAP | VB2_DMABUF;
	q->drv_priv = stream;
	q->ops = &rkcif_vb2_ops;
	if (hw_dev->iommu_en)
		q->mem_ops = &vb2_dma_sg_memops;
	else
		q->mem_ops = &vb2_dma_contig_memops;
	q->buf_struct_size = sizeof(struct rkcif_buffer);
	q->min_buffers_needed = CIF_REQ_BUFS_MIN;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->lock = &stream->vnode.vlock;
	q->dev = hw_dev->dev;
	q->allow_cache_hints = 1;
	q->bidirectional = 1;
	q->gfp_flags = GFP_DMA32;
	return vb2_queue_init(q);
}

static void rkcif_set_fmt(struct rkcif_stream *stream,
			  struct v4l2_pix_format_mplane *pixm,
			  bool try)
{
	struct rkcif_device *dev = stream->cifdev;
	const struct cif_output_fmt *fmt;
	const struct cif_input_fmt *cif_fmt_in = NULL;
	struct v4l2_rect input_rect;
	unsigned int imagesize = 0, ex_imagesize = 0, planes;
	u32 xsubs = 1, ysubs = 1, i;
	struct rkmodule_hdr_cfg hdr_cfg;
	struct rkcif_extend_info *extend_line = &stream->extend_line;
	int ret;

	fmt = find_output_fmt(stream, pixm->pixelformat);
	if (!fmt)
		fmt = &out_fmts[0];

	input_rect.width = RKCIF_DEFAULT_WIDTH;
	input_rect.height = RKCIF_DEFAULT_HEIGHT;

	if (dev->active_sensor && dev->active_sensor->sd) {
		cif_fmt_in = get_input_fmt(dev->active_sensor->sd,
			      &input_rect, stream->id + 1);
		stream->cif_fmt_in = cif_fmt_in;
	}

	if (dev->terminal_sensor.sd) {
		ret = v4l2_subdev_call(dev->terminal_sensor.sd,
				       core, ioctl,
				       RKMODULE_GET_HDR_CFG,
				       &hdr_cfg);
		if (!ret)
			dev->hdr.mode = hdr_cfg.hdr_mode;
		else
			dev->hdr.mode = NO_HDR;

		dev->terminal_sensor.raw_rect = input_rect;
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

	rkcif_sync_crop_info(stream);
	/* calculate plane size and image size */
	fcc_xysubs(fmt->fourcc, &xsubs, &ysubs);

	planes = fmt->cplanes ? fmt->cplanes : fmt->mplanes;

	for (i = 0; i < planes; i++) {
		struct v4l2_plane_pix_format *plane_fmt;
		int width, height, bpl, size, bpp, ex_size;

		if (i == 0) {
			if (stream->crop_enable) {
				width = stream->crop[CROP_SRC_ACT].width;
				height = stream->crop[CROP_SRC_ACT].height;
			} else {
				width = pixm->width;
				height = pixm->height;
			}
		} else {
			if (stream->crop_enable) {
				width = stream->crop[CROP_SRC_ACT].width / xsubs;
				height = stream->crop[CROP_SRC_ACT].height / ysubs;
			} else {
				width = pixm->width / xsubs;
				height = pixm->height / ysubs;
			}
		}

		extend_line->pixm.height = height + RKMODULE_EXTEND_LINE;

		/* compact mode need bytesperline 4bytes align,
		 * align 8 to bring into correspondence with virtual width.
		 * to optimize reading and writing of ddr, aliged with 256.
		 */
		if (fmt->fmt_type == CIF_FMT_TYPE_RAW && stream->is_compact &&
		    (dev->active_sensor->mbus.type == V4L2_MBUS_CSI2 ||
		     dev->active_sensor->mbus.type == V4L2_MBUS_CCP2)) {
			bpl = ALIGN(width * fmt->raw_bpp / 8, 256);
		} else {
			bpp = rkcif_align_bits_per_pixel(fmt, i);
			bpl = width * bpp / CIF_YUV_STORED_BIT_WIDTH;
		}
		size = bpl * height;
		imagesize += size;
		ex_size = bpl * extend_line->pixm.height;
		ex_imagesize += ex_size;

		if (fmt->mplanes > i) {
			/* Set bpl and size for each mplane */
			plane_fmt = pixm->plane_fmt + i;
			plane_fmt->bytesperline = bpl;
			plane_fmt->sizeimage = size;

			plane_fmt = extend_line->pixm.plane_fmt + i;
			plane_fmt->bytesperline = bpl;
			plane_fmt->sizeimage = ex_size;
		}
		v4l2_dbg(1, rkcif_debug, &stream->cifdev->v4l2_dev,
			 "C-Plane %i size: %d, Total imagesize: %d\n",
			 i, size, imagesize);
	}

	/* convert to non-MPLANE format.
	 * It's important since we want to unify non-MPLANE
	 * and MPLANE.
	 */
	if (fmt->mplanes == 1) {
		pixm->plane_fmt[0].sizeimage = imagesize;
		extend_line->pixm.plane_fmt[0].sizeimage = ex_imagesize;
	}

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
	int i;

	memset(stream, 0, sizeof(*stream));
	memset(&pixm, 0, sizeof(pixm));
	stream->id = id;
	stream->cifdev = dev;

	INIT_LIST_HEAD(&stream->buf_head);
	spin_lock_init(&stream->vbq_lock);
	spin_lock_init(&stream->fps_lock);
	stream->state = RKCIF_STATE_READY;
	init_waitqueue_head(&stream->wq_stopped);

	/* Set default format */
	pixm.pixelformat = V4L2_PIX_FMT_NV12;
	pixm.width = RKCIF_DEFAULT_WIDTH;
	pixm.height = RKCIF_DEFAULT_HEIGHT;
	rkcif_set_fmt(stream, &pixm, false);

	for (i = 0; i < CROP_SRC_MAX; i++) {
		stream->crop[i].left = 0;
		stream->crop[i].top = 0;
		stream->crop[i].width = RKCIF_DEFAULT_WIDTH;
		stream->crop[i].height = RKCIF_DEFAULT_HEIGHT;
	}

	stream->crop_enable = false;
	stream->crop_dyn_en = false;
	stream->crop_mask = 0x0;

	if (dev->chip_id >= CHIP_RV1126_CIF)
		stream->is_compact = true;
	else
		stream->is_compact = false;

	if (dev->chip_id == CHIP_RV1126_CIF ||
	    dev->chip_id == CHIP_RV1126_CIF_LITE)
		stream->extend_line.is_extended = true;
	else
		stream->extend_line.is_extended = false;

	stream->is_dvp_yuv_addr_init = false;
	stream->is_fs_fe_not_paired = false;
	stream->fs_cnt_in_single_frame = 0;

}

static int rkcif_fh_open(struct file *filp)
{
	struct video_device *vdev = video_devdata(filp);
	struct rkcif_vdev_node *vnode = vdev_to_node(vdev);
	struct rkcif_stream *stream = to_rkcif_stream(vnode);
	struct rkcif_device *cifdev = stream->cifdev;
	int ret;

	ret = rkcif_attach_hw(cifdev);
	if (ret)
		return ret;

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
	    cifdev->chip_id == CHIP_RV1126_CIF_LITE ||
	    cifdev->chip_id == CHIP_RK3568_CIF) {
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

static int rkcif_cropcap(struct file *file, void *fh,
			 struct v4l2_cropcap *cap)
{
	struct rkcif_stream *stream = video_drvdata(file);
	struct rkcif_device *dev = stream->cifdev;
	struct v4l2_rect *raw_rect = &dev->terminal_sensor.raw_rect;
	int ret = 0;

	if (stream->crop_mask & CROP_SRC_SENSOR) {
		cap->bounds.left = stream->crop[CROP_SRC_SENSOR].left;
		cap->bounds.top = stream->crop[CROP_SRC_SENSOR].top;
		cap->bounds.width = stream->crop[CROP_SRC_SENSOR].width;
		cap->bounds.height = stream->crop[CROP_SRC_SENSOR].height;
	} else {
		cap->bounds.left = raw_rect->left;
		cap->bounds.top = raw_rect->top;
		cap->bounds.width = raw_rect->width;
		cap->bounds.height = raw_rect->height;
	}

	cap->defrect = cap->bounds;
	cap->pixelaspect.numerator = 1;
	cap->pixelaspect.denominator = 1;

	return ret;
}

static int rkcif_s_crop(struct file *file, void *fh, const struct v4l2_crop *a)
{
	struct rkcif_stream *stream = video_drvdata(file);
	struct rkcif_device *dev = stream->cifdev;
	const struct v4l2_rect *rect = &a->c;
	struct v4l2_rect sensor_crop;
	struct v4l2_rect *raw_rect = &dev->terminal_sensor.raw_rect;
	int ret;

	ret = rkcif_sanity_check_fmt(stream, rect);
	if (ret) {
		v4l2_err(&dev->v4l2_dev, "set crop failed\n");
		return ret;
	}

	if (stream->crop_mask & CROP_SRC_SENSOR) {
		sensor_crop = stream->crop[CROP_SRC_SENSOR];
		if (rect->left + rect->width > sensor_crop.width ||
		    rect->top + rect->height > sensor_crop.height) {
			v4l2_err(&dev->v4l2_dev,
				 "crop size is bigger than sensor input:left:%d, top:%d, width:%d, height:%d\n",
				 sensor_crop.left, sensor_crop.top, sensor_crop.width, sensor_crop.height);
			return -EINVAL;
		}
	} else {
		if (rect->left + rect->width > raw_rect->width ||
		    rect->top + rect->height > raw_rect->height) {
			v4l2_err(&dev->v4l2_dev,
				 "crop size is bigger than sensor raw input:left:%d, top:%d, width:%d, height:%d\n",
				 raw_rect->left, raw_rect->top, raw_rect->width, raw_rect->height);
			return -EINVAL;
		}
	}

	stream->crop[CROP_SRC_USR] = *rect;
	stream->crop_enable = true;
	stream->crop_mask |= CROP_SRC_USR_MASK;
	stream->crop[CROP_SRC_ACT] = stream->crop[CROP_SRC_USR];
	if (stream->crop_mask & CROP_SRC_SENSOR) {
		stream->crop[CROP_SRC_ACT].left = sensor_crop.left + stream->crop[CROP_SRC_USR].left;
		stream->crop[CROP_SRC_ACT].top = sensor_crop.top + stream->crop[CROP_SRC_USR].top;
	}

	if (stream->state == RKCIF_STATE_STREAMING) {
		stream->crop_dyn_en = true;

		v4l2_info(&dev->v4l2_dev, "enable dynamic crop, S_CROP(%ux%u@%u:%u) type: %d\n",
			  rect->width, rect->height, rect->left, rect->top, a->type);
	} else {
		v4l2_info(&dev->v4l2_dev, "static crop, S_CROP(%ux%u@%u:%u) type: %d\n",
			  rect->width, rect->height, rect->left, rect->top, a->type);
	}

	return ret;
}

static int rkcif_g_crop(struct file *file, void *fh, struct v4l2_crop *a)
{
	struct rkcif_stream *stream = video_drvdata(file);

	a->c = stream->crop[CROP_SRC_ACT];

	return 0;
}

static int rkcif_s_selection(struct file *file, void *fh,
				  struct v4l2_selection *s)
{
	struct rkcif_stream *stream = video_drvdata(file);
	struct rkcif_device *dev = stream->cifdev;
	struct v4l2_subdev *sensor_sd;
	struct v4l2_subdev_selection sd_sel;
	u16 pad = 0;
	int ret = 0;

	if (!s) {
		v4l2_dbg(1, rkcif_debug, &dev->v4l2_dev, "sel is null\n");
		goto err;
	}

	sensor_sd = get_remote_sensor(stream, &pad);

	sd_sel.r = s->r;
	sd_sel.pad = pad;
	sd_sel.target = s->target;
	sd_sel.which = V4L2_SUBDEV_FORMAT_ACTIVE;

	ret = v4l2_subdev_call(sensor_sd, pad, set_selection, NULL, &sd_sel);
	if (!ret) {
		s->r = sd_sel.r;
		v4l2_dbg(1, rkcif_debug, &dev->v4l2_dev, "%s: pad:%d, which:%d, target:%d\n",
			 __func__, pad, sd_sel.which, sd_sel.target);
	}

	return ret;

err:
	return -EINVAL;
}

static int rkcif_g_selection(struct file *file, void *fh,
				  struct v4l2_selection *s)
{
	struct rkcif_stream *stream = video_drvdata(file);
	struct rkcif_device *dev = stream->cifdev;
	struct v4l2_subdev *sensor_sd;
	struct v4l2_subdev_selection sd_sel;
	u16 pad = 0;
	int ret = 0;

	if (!s) {
		v4l2_dbg(1, rkcif_debug, &dev->v4l2_dev, "sel is null\n");
		goto err;
	}

	if (s->target == V4L2_SEL_TGT_CROP_BOUNDS) {
		sensor_sd = get_remote_sensor(stream, &pad);

		sd_sel.pad = pad;
		sd_sel.target = s->target;
		sd_sel.which = V4L2_SUBDEV_FORMAT_ACTIVE;

		v4l2_dbg(1, rkcif_debug, &dev->v4l2_dev, "%s(line:%d): sd:%s pad:%d, which:%d, target:%d\n",
			 __func__, __LINE__, sensor_sd->name, pad, sd_sel.which, sd_sel.target);

		ret = v4l2_subdev_call(sensor_sd, pad, get_selection, NULL, &sd_sel);
		if (!ret) {
			s->r = sd_sel.r;
		} else {
			s->r.left = 0;
			s->r.top = 0;
			s->r.width = stream->pixm.width;
			s->r.height = stream->pixm.height;
		}
	}

	if (s->target == V4L2_SEL_TGT_CROP) {
		if (stream->crop_mask & (CROP_SRC_USR_MASK | CROP_SRC_SENSOR_MASK)) {
			s->r = stream->crop[CROP_SRC_ACT];
		} else {
			s->r.left = 0;
			s->r.top = 0;
			s->r.width = stream->pixm.width;
			s->r.height = stream->pixm.height;
		}
	}

	return ret;
err:
	return -EINVAL;
}

static int rkcif_g_ctrl(struct file *file, void *fh,
			   struct v4l2_control *ctrl)
{
	struct rkcif_stream *stream = video_drvdata(file);

	switch (ctrl->id) {
	case V4L2_CID_CIF_DATA_COMPACT:
		if (stream->is_compact)
			ctrl->value = CSI_MEM_COMPACT;
		else
			ctrl->value = CSI_MEM_BYTE_LE;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int rkcif_s_ctrl(struct file *file, void *fh,
			   struct v4l2_control *ctrl)
{
	struct rkcif_stream *stream = video_drvdata(file);
	struct rkcif_device *dev = stream->cifdev;

	if (stream->state == RKCIF_STATE_STREAMING) {
		v4l2_err(&dev->v4l2_dev, "set failed, the stream is streaming\n");
		return -EBUSY;
	}

	switch (ctrl->id) {
	case V4L2_CID_CIF_DATA_COMPACT:
		if (ctrl->value == CSI_LVDS_MEM_COMPACT)
			stream->is_compact = true;
		else
			stream->is_compact = false;
	break;

	default:
		return -EINVAL;
	}

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
	.vidioc_cropcap = rkcif_cropcap,
	.vidioc_s_crop = rkcif_s_crop,
	.vidioc_g_crop = rkcif_g_crop,
	.vidioc_s_selection = rkcif_s_selection,
	.vidioc_g_selection = rkcif_g_selection,
	.vidioc_enum_frameintervals = rkcif_enum_frameintervals,
	.vidioc_enum_framesizes = rkcif_enum_framesizes,
	.vidioc_g_ctrl = rkcif_g_ctrl,
	.vidioc_s_ctrl = rkcif_s_ctrl,
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

	if (dev->chip_id < CHIP_RV1126_CIF) {
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
	} else {
		if (dev->inf_id == RKCIF_MIPI_LVDS) {
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
			default:
				v4l2_err(v4l2_dev, "Invalid stream\n");
				goto unreg;
			}
		} else {
			switch (stream->id) {
			case RKCIF_STREAM_MIPI_ID0:
				vdev_name = CIF_DVP_ID0_VDEV_NAME;
				break;
			case RKCIF_STREAM_MIPI_ID1:
				vdev_name = CIF_DVP_ID1_VDEV_NAME;
				break;
			case RKCIF_STREAM_MIPI_ID2:
				vdev_name = CIF_DVP_ID2_VDEV_NAME;
				break;
			case RKCIF_STREAM_MIPI_ID3:
				vdev_name = CIF_DVP_ID3_VDEV_NAME;
				break;
			default:
				v4l2_err(v4l2_dev, "Invalid stream\n");
				goto unreg;
			}
		}
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
	struct rkcif_lvds_subdev *subdev = container_of(sd, struct rkcif_lvds_subdev, sd);
	struct v4l2_subdev *sensor = get_lvds_remote_sensor(sd);
	int ret;

	/*
	 * Do not allow format changes and just relay whatever
	 * set currently in the sensor.
	 */
	ret = v4l2_subdev_call(sensor, pad, get_fmt, NULL, fmt);
	if (!ret)
		subdev->in_fmt = fmt->format;

	return ret;
}

static struct v4l2_rect *rkcif_lvds_sd_get_crop(struct rkcif_lvds_subdev *subdev,
						      struct v4l2_subdev_pad_config *cfg,
						      enum v4l2_subdev_format_whence which)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return v4l2_subdev_get_try_crop(&subdev->sd, cfg, RKCIF_LVDS_PAD_SINK);
	else
		return &subdev->crop;
}

static int rkcif_lvds_sd_set_selection(struct v4l2_subdev *sd,
					       struct v4l2_subdev_pad_config *cfg,
					       struct v4l2_subdev_selection *sel)
{
	struct rkcif_lvds_subdev *subdev = container_of(sd, struct rkcif_lvds_subdev, sd);
	struct v4l2_subdev *sensor = get_lvds_remote_sensor(sd);
	int ret = 0;

	ret = v4l2_subdev_call(sensor, pad, set_selection,
			       cfg, sel);
	if (!ret)
		subdev->crop = sel->r;

	return ret;
}

static int rkcif_lvds_sd_get_selection(struct v4l2_subdev *sd,
					       struct v4l2_subdev_pad_config *cfg,
					       struct v4l2_subdev_selection *sel)
{
	struct rkcif_lvds_subdev *subdev = container_of(sd, struct rkcif_lvds_subdev, sd);
	struct v4l2_subdev *sensor = get_lvds_remote_sensor(sd);
	struct v4l2_subdev_format fmt;
	int ret = 0;

	if (!sel) {
		v4l2_dbg(1, rkcif_debug, sd, "sel is null\n");
		goto err;
	}

	if (sel->pad > RKCIF_LVDS_PAD_SRC_ID3) {
		v4l2_dbg(1, rkcif_debug, sd, "pad[%d] isn't matched\n", sel->pad);
		goto err;
	}

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP_BOUNDS:
		if (sel->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
			ret = v4l2_subdev_call(sensor, pad, get_selection,
					       cfg, sel);
			if (ret) {
				fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
				ret = v4l2_subdev_call(sensor, pad, get_fmt, NULL, &fmt);
				if (!ret) {
					subdev->in_fmt = fmt.format;
					sel->r.top = 0;
					sel->r.left = 0;
					sel->r.width = subdev->in_fmt.width;
					sel->r.height = subdev->in_fmt.height;
					subdev->crop = sel->r;
				} else {
					sel->r = subdev->crop;
				}
			} else {
				subdev->crop = sel->r;
			}
		} else {
			sel->r = *v4l2_subdev_get_try_crop(sd, cfg, sel->pad);
		}
		break;

	case V4L2_SEL_TGT_CROP:
		sel->r = *rkcif_lvds_sd_get_crop(subdev, cfg, sel->which);
		break;

	default:
		return -EINVAL;
	}

	return 0;
err:
	return -EINVAL;
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

static int rkcif_sof_subscribe_event(struct v4l2_subdev *sd, struct v4l2_fh *fh,
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
	.set_selection = rkcif_lvds_sd_set_selection,
	.get_selection = rkcif_lvds_sd_get_selection,
};

static const struct v4l2_subdev_video_ops rkcif_lvds_sd_video_ops = {
	.g_mbus_config = rkcif_lvds_g_mbus_config,
	.s_stream = rkcif_lvds_sd_s_stream,
};

static const struct v4l2_subdev_core_ops rkcif_lvds_sd_core_ops = {
	.s_power = rkcif_lvds_sd_s_power,
	.subscribe_event = rkcif_sof_subscribe_event,
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

	lvds_subdev->in_fmt.width = RKCIF_DEFAULT_WIDTH;
	lvds_subdev->in_fmt.height = RKCIF_DEFAULT_HEIGHT;
	lvds_subdev->crop.left = 0;
	lvds_subdev->crop.top = 0;
	lvds_subdev->crop.width = RKCIF_DEFAULT_WIDTH;
	lvds_subdev->crop.height = RKCIF_DEFAULT_HEIGHT;

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

static void rkcif_dvp_event_inc_sof(struct rkcif_device *dev)
{
	struct rkcif_dvp_sof_subdev *subdev = &dev->dvp_sof_subdev;

	if (subdev) {
		struct v4l2_event event = {
			.type = V4L2_EVENT_FRAME_SYNC,
			.u.frame_sync.frame_sequence =
				atomic_inc_return(&subdev->frm_sync_seq) - 1,
		};
		v4l2_event_queue(subdev->sd.devnode, &event);
	}
}

static u32 rkcif_dvp_get_sof(struct rkcif_device *dev)
{
	if (dev)
		return atomic_read(&dev->dvp_sof_subdev.frm_sync_seq) - 1;

	return 0;
}

static const struct v4l2_subdev_core_ops rkcif_dvp_sof_sd_core_ops = {
	.subscribe_event = rkcif_sof_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static struct v4l2_subdev_ops rkcif_dvp_sof_sd_ops = {
	.core = &rkcif_dvp_sof_sd_core_ops,
};

int rkcif_register_dvp_sof_subdev(struct rkcif_device *dev)
{
	struct v4l2_device *v4l2_dev = &dev->v4l2_dev;
	struct rkcif_dvp_sof_subdev *subdev = &dev->dvp_sof_subdev;
	struct v4l2_subdev *sd;
	int ret;

	memset(subdev, 0, sizeof(*subdev));
	subdev->cifdev = dev;
	sd = &subdev->sd;
	v4l2_subdev_init(sd, &rkcif_dvp_sof_sd_ops);
	sd->owner = THIS_MODULE;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS;
	snprintf(sd->name, sizeof(sd->name), "rkcif-dvp-sof");

	v4l2_set_subdevdata(sd, subdev);
	ret = v4l2_device_register_subdev(v4l2_dev, sd);
	if (ret < 0)
		goto end;

	ret = v4l2_device_register_subdev_nodes(v4l2_dev);
	if (ret < 0)
		goto free_subdev;

	return ret;

free_subdev:
	v4l2_device_unregister_subdev(sd);

end:
	v4l2_err(sd, "Failed to register subdev, ret:%d\n", ret);
	return ret;
}

void rkcif_unregister_dvp_sof_subdev(struct rkcif_device *dev)
{
	struct v4l2_subdev *sd = &dev->dvp_sof_subdev.sd;

	v4l2_device_unregister_subdev(sd);
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
	int ret = 0;

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

		/* In one-frame mode:
		 * 1,must clear status manually by writing 0 to enable
		 * the next frame end irq;
		 * 2,do not clear the frame id for switching address.
		 */
		rkcif_write_register(cif_dev, CIF_REG_DVP_FRAME_STATUS,
				     cif_frmst & FRM0_STAT_CLS);
		ret = rkcif_assign_new_buffer_oneframe(stream,
						 RKCIF_YUV_ADDR_STATE_UPDATE);

		if (vb_done && (!ret)) {
			vb_done->sequence = stream->frame_idx;
			rkcif_vb_done_oneframe(stream, vb_done);
		}

		stream->frame_idx++;
		cif_dev->irq_stats.all_frm_end_cnt++;
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

static int rkcif_dvp_g_ch_id(struct v4l2_device *v4l2_dev,
			     u32 *intstat, u32 frm_stat)
{
	if (*intstat & DVP_FRAME_END_ID0) {
		if ((frm_stat & DVP_CHANNEL0_FRM_READ) ==
		    DVP_CHANNEL0_FRM_READ)
			v4l2_warn(v4l2_dev, "frame0/1 trigger simultaneously in DVP ID0\n");

		*intstat &= ~DVP_FRAME_END_ID0;
		return RKCIF_STREAM_MIPI_ID0;
	}

	if (*intstat & DVP_FRAME_END_ID1) {
		if ((frm_stat & DVP_CHANNEL1_FRM_READ) ==
		    DVP_CHANNEL1_FRM_READ)
			v4l2_warn(v4l2_dev, "frame0/1 trigger simultaneously in DVP ID1\n");

		*intstat &= ~DVP_FRAME_END_ID1;
		return RKCIF_STREAM_MIPI_ID1;
	}

	if (*intstat & DVP_FRAME_END_ID2) {
		if ((frm_stat & DVP_CHANNEL2_FRM_READ) ==
		    DVP_CHANNEL2_FRM_READ)
			v4l2_warn(v4l2_dev, "frame0/1 trigger simultaneously in DVP ID2\n");
			*intstat &= ~DVP_FRAME_END_ID2;
		return RKCIF_STREAM_MIPI_ID2;
	}

	if (*intstat & DVP_FRAME_END_ID3) {
		if ((frm_stat & DVP_CHANNEL3_FRM_READ) ==
		    DVP_CHANNEL3_FRM_READ)
			v4l2_warn(v4l2_dev, "frame0/1 trigger simultaneously in DVP ID3\n");
			*intstat &= ~DVP_FRAME_END_ID3;
		return RKCIF_STREAM_MIPI_ID3;
	}

	return -EINVAL;
}

static bool rkcif_is_csi2_err_trigger_reset(struct rkcif_timer *timer)
{
	struct rkcif_device *dev = container_of(timer,
						struct rkcif_device,
						reset_watchdog_timer);
	struct rkcif_stream *stream = &dev->stream[RKCIF_STREAM_MIPI_ID0];

	bool is_triggered = false;
	unsigned long lock_flags;

	spin_lock_irqsave(&timer->csi2_err_lock, lock_flags);

	if (timer->csi2_err_cnt_even != 0 &&
	    timer->csi2_err_cnt_odd != 0) {
		is_triggered = true;
		timer->csi2_err_cnt_odd = 0;
		timer->csi2_err_cnt_even = 0;
		timer->reset_src = RKCIF_RESET_SRC_ERR_CSI2;
		v4l2_info(&dev->v4l2_dev, "do csi2 err reset\n");
	}

	/*
	 * when fs cnt is beyond 2, it indicates that frame end is not coming,
	 * or fs and fe had been not paired.
	 */
	if (stream->is_fs_fe_not_paired ||
	    stream->fs_cnt_in_single_frame > RKCIF_FS_DETECTED_NUM) {
		is_triggered = true;
		v4l2_info(&dev->v4l2_dev, "reset for fs & fe not paired\n");
	}

	spin_unlock_irqrestore(&timer->csi2_err_lock, lock_flags);

	return is_triggered;
}

static bool rkcif_is_triggered_monitoring(struct rkcif_device *dev)
{
	struct rkcif_timer *timer = &dev->reset_watchdog_timer;
	struct rkcif_stream *stream = &dev->stream[RKCIF_STREAM_MIPI_ID0];
	bool ret = false;

	if (timer->monitor_mode == RKCIF_MONITOR_MODE_IDLE)
		ret = false;

	if (timer->monitor_mode == RKCIF_MONITOR_MODE_CONTINUE ||
	    timer->monitor_mode == RKCIF_MONITOR_MODE_HOTPLUG) {
		if (stream->frame_idx >= timer->triggered_frame_num)
			ret = true;
	}

	if (timer->monitor_mode == RKCIF_MONITOR_MODE_TRIGGER) {
		timer->is_csi2_err_occurred = rkcif_is_csi2_err_trigger_reset(timer);
		ret = timer->is_csi2_err_occurred;
	}

	return ret;
}

static s32 rkcif_get_sensor_vblank(struct rkcif_device *dev)
{
	struct rkcif_sensor_info *terminal_sensor = &dev->terminal_sensor;
	struct v4l2_subdev *sd = terminal_sensor->sd;
	struct v4l2_ctrl_handler *hdl = sd->ctrl_handler;
	struct v4l2_ctrl *ctrl = NULL;

	if (!list_empty(&hdl->ctrls)) {
		list_for_each_entry(ctrl, &hdl->ctrls, node) {
			if (ctrl->id == V4L2_CID_VBLANK)
				return ctrl->val;
		}
	}

	return 0;
}

static void rkcif_cal_csi_crop_width_vwidth(struct rkcif_stream *stream,
					    u32 raw_width, u32 *crop_width,
					    u32 *crop_vwidth)
{
	struct rkcif_device *dev = stream->cifdev;
	struct csi_channel_info *channel = &dev->channels[stream->id];
	const struct cif_output_fmt *fmt;
	u32 fourcc;

	fmt = find_output_fmt(stream, stream->pixm.pixelformat);
	if (!fmt) {
		v4l2_err(&dev->v4l2_dev, "can not find output format: 0x%x",
			 stream->pixm.pixelformat);
		return;
	}

	*crop_width = raw_width;

	/*
	 * for mipi or lvds, when enable compact, the virtual width of raw10/raw12
	 * needs aligned with :ALIGN(bits_per_pixel * width / 8, 8), if enable 16bit mode
	 * needs aligned with :ALIGN(bits_per_pixel * width * 2, 8), to optimize reading and
	 * writing of ddr, aliged with 256
	 */
	if (fmt->fmt_type == CIF_FMT_TYPE_RAW && stream->is_compact) {
		*crop_vwidth = ALIGN(raw_width * fmt->raw_bpp / 8, 256);
	} else {
		if (fmt->fmt_type == CIF_FMT_TYPE_RAW)
			*crop_vwidth = ALIGN(raw_width * 2, 8);
		else
			*crop_vwidth = ALIGN(raw_width * fmt->bpp[0] / 8, 8);
	}

	if (channel->fmt_val == CSI_WRDDR_TYPE_RGB888)
		*crop_width = raw_width * fmt->bpp[0] / 8;
	/*
	 * rk cif don't support output yuyv fmt data
	 * if user request yuyv fmt, the input mode must be RAW8
	 * and the width is double Because the real input fmt is
	 * yuyv
	 */
	fourcc = stream->cif_fmt_out->fourcc;
	if (fourcc == V4L2_PIX_FMT_YUYV || fourcc == V4L2_PIX_FMT_YVYU ||
	    fourcc == V4L2_PIX_FMT_UYVY || fourcc == V4L2_PIX_FMT_VYUY) {
		*crop_width = 2 * raw_width;
		*crop_vwidth *= 2;
	}
}

static void rkcif_dynamic_crop(struct rkcif_stream *stream)
{
	struct rkcif_device *cif_dev = stream->cifdev;
	struct v4l2_mbus_config *mbus;
	const struct cif_output_fmt *fmt;
	u32 raw_width, crop_width = 64, crop_vwidth = 64,
	    crop_height = 64, crop_x = 0, crop_y = 0;

	if (!cif_dev->active_sensor)
		return;

	mbus = &cif_dev->active_sensor->mbus;
	if (mbus->type == V4L2_MBUS_CSI2 ||
	    mbus->type == V4L2_MBUS_CCP2) {
		struct csi_channel_info *channel = &cif_dev->channels[stream->id];

		if (channel->fmt_val == CSI_WRDDR_TYPE_RGB888)
			crop_x = 3 * stream->crop[CROP_SRC_ACT].left;
		else
			crop_x = stream->crop[CROP_SRC_ACT].left;

		crop_y = stream->crop[CROP_SRC_ACT].top;
		raw_width = stream->crop[CROP_SRC_ACT].width;
		crop_height = stream->crop[CROP_SRC_ACT].height;

		rkcif_cal_csi_crop_width_vwidth(stream,
						raw_width,
						&crop_width, &crop_vwidth);
		rkcif_write_register(cif_dev,
				     get_reg_index_of_id_crop_start(channel->id),
				     crop_y << 16 | crop_x);
		rkcif_write_register(cif_dev, get_reg_index_of_id_ctrl1(channel->id),
				     crop_height << 16 | crop_width);

		rkcif_write_register(cif_dev,
				     get_reg_index_of_frm0_y_vlw(channel->id),
				     crop_vwidth);
		rkcif_write_register(cif_dev,
				     get_reg_index_of_frm1_y_vlw(channel->id),
				     crop_vwidth);
		rkcif_write_register(cif_dev,
				     get_reg_index_of_frm0_uv_vlw(channel->id),
				     crop_vwidth);
		rkcif_write_register(cif_dev,
				     get_reg_index_of_frm1_uv_vlw(channel->id),
				     crop_vwidth);
	} else {

		raw_width = stream->crop[CROP_SRC_ACT].width;
		crop_width = raw_width;
		crop_vwidth = raw_width;
		crop_height = stream->crop[CROP_SRC_ACT].height;
		crop_x = stream->crop[CROP_SRC_ACT].left;
		crop_y = stream->crop[CROP_SRC_ACT].top;

		rkcif_write_register(cif_dev, CIF_REG_DVP_CROP,
				     crop_y << CIF_CROP_Y_SHIFT | crop_x);

		if (stream->cif_fmt_in->fmt_type == CIF_FMT_TYPE_RAW) {
			fmt = find_output_fmt(stream, stream->pixm.pixelformat);
			crop_vwidth = raw_width * rkcif_cal_raw_vir_line_ratio(fmt);
		}
		rkcif_write_register(cif_dev, CIF_REG_DVP_VIR_LINE_WIDTH, crop_vwidth);

		rkcif_write_register(cif_dev, CIF_REG_DVP_SET_SIZE,
				     crop_height << 16 | crop_width);
	}

	stream->crop_dyn_en = false;
}

static void rkcif_monitor_reset_event(struct rkcif_device *dev)
{
	struct rkcif_stream *stream = &dev->stream[RKCIF_STREAM_MIPI_ID0];
	struct rkcif_timer *timer = &dev->reset_watchdog_timer;
	unsigned int cycle = 0;
	u64 fps, timestamp0, timestamp1;
	unsigned long lock_flags = 0, fps_flags = 0;

	if (timer->monitor_mode == RKCIF_MONITOR_MODE_IDLE)
		return;

	if (stream->state != RKCIF_STATE_STREAMING)
		return;

	if (timer->is_running)
		return;

	timer->is_triggered = rkcif_is_triggered_monitoring(dev);

	if (timer->is_triggered) {

		struct v4l2_rect *raw_rect = &dev->terminal_sensor.raw_rect;
		enum rkcif_monitor_mode mode;
		s32 vblank = 0;
		u32 vts = 0;

		spin_lock_irqsave(&stream->fps_lock, fps_flags);
		timestamp0 = stream->fps_stats.frm0_timestamp;
		timestamp1 = stream->fps_stats.frm1_timestamp;
		spin_unlock_irqrestore(&stream->fps_lock, fps_flags);

		spin_lock_irqsave(&timer->timer_lock, lock_flags);

		fps = timestamp0 > timestamp1 ?
		      timestamp0 - timestamp1 : timestamp1 - timestamp0;
		fps = div_u64(fps, 1000);
		timer->frame_end_cycle_us = fps;

		vblank = rkcif_get_sensor_vblank(dev);
		timer->raw_height = raw_rect->height;
		vts = timer->raw_height + vblank;
		timer->vts = vts;

		timer->line_end_cycle = div_u64(timer->frame_end_cycle_us, timer->vts);
		fps = div_u64(timer->frame_end_cycle_us, 1000);
		cycle = fps * timer->frm_num_of_monitor_cycle;
		timer->cycle = msecs_to_jiffies(cycle);

		timer->run_cnt = 0;
		timer->is_running = true;
		timer->is_buf_stop_update = false;
		timer->last_buf_wakeup_cnt = dev->buf_wake_up_cnt;
		/* in trigger mode, monitoring count is fps */
		mode = timer->monitor_mode;
		if (mode == RKCIF_MONITOR_MODE_CONTINUE ||
		    mode == RKCIF_MONITOR_MODE_HOTPLUG)
			timer->max_run_cnt = 0xffffffff - CIF_TIMEOUT_FRAME_NUM;
		else
			timer->max_run_cnt = div_u64(1000, fps) * 1;

		timer->timer.expires = jiffies + timer->cycle;
		mod_timer(&timer->timer, timer->timer.expires);

		spin_unlock_irqrestore(&timer->timer_lock, lock_flags);

		v4l2_dbg(1, rkcif_debug, &dev->v4l2_dev,
			 "%s:mode:%d, raw height:%d,vblank:%d, cycle:%ld, fps:%llu\n",
			  __func__, timer->monitor_mode, raw_rect->height,
			  vblank, timer->cycle, div_u64(1000, fps));
	}
}

static void rkcif_rdbk_frame_end(struct rkcif_stream *stream)
{
	struct rkcif_device *dev = stream->cifdev;
	struct rkcif_sensor_info *sensor = &stream->cifdev->terminal_sensor;
	u32 denominator, numerator;
	u64 l_ts, m_ts, s_ts, time = 30000000LL;
	int ret, fps = -1;

	if (dev->hdr.mode == HDR_X2) {
		if (stream->id != RKCIF_STREAM_MIPI_ID1 ||
		    dev->stream[RKCIF_STREAM_MIPI_ID0].state != RKCIF_STATE_STREAMING ||
		    dev->stream[RKCIF_STREAM_MIPI_ID1].state != RKCIF_STATE_STREAMING)
			return;
	} else if (dev->hdr.mode == HDR_X3) {
		if (stream->id != RKCIF_STREAM_MIPI_ID2 ||
		    dev->stream[RKCIF_STREAM_MIPI_ID0].state != RKCIF_STATE_STREAMING ||
		    dev->stream[RKCIF_STREAM_MIPI_ID1].state != RKCIF_STATE_STREAMING ||
		    dev->stream[RKCIF_STREAM_MIPI_ID2].state != RKCIF_STATE_STREAMING)
			return;
	}

	numerator = sensor->fi.interval.numerator;
	denominator = sensor->fi.interval.denominator;
	if (denominator && numerator)
		time = numerator * 1000 / denominator * 1000 * 1000;

	if (dev->hdr.mode == HDR_X3) {
		if (dev->rdbk_buf[RDBK_L] &&
		    dev->rdbk_buf[RDBK_M] &&
		    dev->rdbk_buf[RDBK_S]) {
			l_ts = dev->rdbk_buf[RDBK_L]->vb.vb2_buf.timestamp;
			m_ts = dev->rdbk_buf[RDBK_M]->vb.vb2_buf.timestamp;
			s_ts = dev->rdbk_buf[RDBK_S]->vb.vb2_buf.timestamp;

			if (m_ts < l_ts || s_ts < m_ts) {
				v4l2_err(&dev->v4l2_dev,
					 "s/m/l frame err, timestamp s:%lld m:%lld l:%lld\n",
					 s_ts, m_ts, l_ts);
				goto RDBK_FRM_UNMATCH;
			}

			if ((m_ts - l_ts) > time || (s_ts - m_ts) > time) {
				ret = v4l2_subdev_call(sensor->sd,
						       video,
						       g_frame_interval,
						       &sensor->fi);
				if (!ret) {
					denominator = sensor->fi.interval.denominator;
					numerator = sensor->fi.interval.numerator;
					if (denominator && numerator) {
						time = numerator * 1000 / denominator * 1000 * 1000;
						fps = denominator / numerator;
					}
				}

				if ((m_ts - l_ts) > time || (s_ts - m_ts) > time) {
					v4l2_err(&dev->v4l2_dev,
						 "timestamp no match, s:%lld m:%lld l:%lld, fps:%d\n",
						 s_ts, m_ts, l_ts, fps);
					goto RDBK_FRM_UNMATCH;
				}
			}

			dev->rdbk_buf[RDBK_M]->vb.sequence = dev->rdbk_buf[RDBK_L]->vb.sequence;
			dev->rdbk_buf[RDBK_S]->vb.sequence = dev->rdbk_buf[RDBK_L]->vb.sequence;
			rkcif_vb_done_oneframe(stream, &dev->rdbk_buf[RDBK_L]->vb);
			rkcif_vb_done_oneframe(stream, &dev->rdbk_buf[RDBK_M]->vb);
			rkcif_vb_done_oneframe(stream, &dev->rdbk_buf[RDBK_S]->vb);
		} else {
			if (!dev->rdbk_buf[RDBK_L])
				v4l2_err(&dev->v4l2_dev, "lost long frames\n");
			if (!dev->rdbk_buf[RDBK_M])
				v4l2_err(&dev->v4l2_dev, "lost medium frames\n");
			if (!dev->rdbk_buf[RDBK_S])
				v4l2_err(&dev->v4l2_dev, "lost short frames\n");
			goto RDBK_FRM_UNMATCH;
		}
	} else if (dev->hdr.mode == HDR_X2) {
		if (dev->rdbk_buf[RDBK_L] && dev->rdbk_buf[RDBK_M]) {
			l_ts = dev->rdbk_buf[RDBK_L]->vb.vb2_buf.timestamp;
			s_ts = dev->rdbk_buf[RDBK_M]->vb.vb2_buf.timestamp;

			if (s_ts < l_ts) {
				v4l2_err(&dev->v4l2_dev,
					 "s/l frame err, timestamp s:%lld l:%lld\n",
					 s_ts, l_ts);
				goto RDBK_FRM_UNMATCH;
			}

			if ((s_ts - l_ts) > time) {
				ret = v4l2_subdev_call(sensor->sd,
						       video,
						       g_frame_interval,
						       &sensor->fi);
				if (!ret) {
					denominator = sensor->fi.interval.denominator;
					numerator = sensor->fi.interval.numerator;
					if (denominator && numerator) {
						time = numerator * 1000 / denominator * 1000 * 1000;
						fps = denominator / numerator;
					}
				}
				if ((s_ts - l_ts) > time) {
					v4l2_err(&dev->v4l2_dev,
						 "timestamp no match, s:%lld l:%lld, fps:%d\n",
						 s_ts, l_ts, fps);
					goto RDBK_FRM_UNMATCH;
				}
			}

			dev->rdbk_buf[RDBK_M]->vb.sequence =
				dev->rdbk_buf[RDBK_L]->vb.sequence;
			rkcif_vb_done_oneframe(stream, &dev->rdbk_buf[RDBK_L]->vb);
			rkcif_vb_done_oneframe(stream, &dev->rdbk_buf[RDBK_M]->vb);
		} else {
			if (!dev->rdbk_buf[RDBK_L])
				v4l2_err(&dev->v4l2_dev, "lost long frames\n");
			if (!dev->rdbk_buf[RDBK_M])
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

static void rkcif_update_stream(struct rkcif_device *cif_dev,
				struct rkcif_stream *stream,
				int mipi_id)
{
	struct rkcif_buffer *active_buf = NULL;
	struct vb2_v4l2_buffer *vb_done = NULL;
	unsigned long lock_flags = 0;
	int ret = 0;

	if (stream->frame_phase == (CIF_CSI_FRAME0_READY | CIF_CSI_FRAME1_READY)) {

		v4l2_err(&cif_dev->v4l2_dev, "stream[%d], frm0/frm1 end simultaneously,frm id:%d\n",
			 stream->id, stream->frame_idx);

		stream->frame_idx++;
		return;
	}

	spin_lock(&stream->fps_lock);
	if (stream->frame_phase & CIF_CSI_FRAME0_READY) {
		if (stream->curr_buf)
			active_buf = stream->curr_buf;
		stream->fps_stats.frm0_timestamp = ktime_get_ns();
	} else if (stream->frame_phase & CIF_CSI_FRAME1_READY) {
		if (stream->next_buf)
			active_buf = stream->next_buf;
		stream->fps_stats.frm1_timestamp = ktime_get_ns();
	}
	spin_unlock(&stream->fps_lock);

	cif_dev->buf_wake_up_cnt += 1;
	ret = rkcif_assign_new_buffer_pingpong(stream,
					       RKCIF_YUV_ADDR_STATE_UPDATE,
					       mipi_id);
	if (ret)
		goto end;

	if (cif_dev->chip_id == CHIP_RV1126_CIF ||
	    cif_dev->chip_id == CHIP_RV1126_CIF_LITE ||
	    cif_dev->chip_id == CHIP_RK3568_CIF)
		rkcif_luma_isr(&cif_dev->luma_vdev, mipi_id, stream->frame_idx);

	if (active_buf) {
		vb_done = &active_buf->vb;
		vb_done->vb2_buf.timestamp = ktime_get_ns();
		vb_done->sequence = stream->frame_idx;
	}

	if (cif_dev->hdr.mode == NO_HDR) {
		if (active_buf)
			rkcif_vb_done_oneframe(stream, vb_done);
	} else {
		if (cif_dev->is_start_hdr) {
			spin_lock_irqsave(&cif_dev->hdr_lock, lock_flags);
			if (mipi_id == RKCIF_STREAM_MIPI_ID0) {
				if (cif_dev->rdbk_buf[RDBK_L]) {
					v4l2_err(&cif_dev->v4l2_dev,
						 "multiple long data in %s frame,frm_idx:%d,state:0x%x\n",
						 cif_dev->hdr.mode == HDR_X2 ? "hdr_x2" : "hdr_x3",
						 stream->frame_idx,
						 cif_dev->rdbk_buf[RDBK_L]->vb.vb2_buf.state);
					cif_dev->rdbk_buf[RDBK_L]->vb.vb2_buf.state = VB2_BUF_STATE_ACTIVE;
					rkcif_buf_queue(&cif_dev->rdbk_buf[RDBK_L]->vb.vb2_buf);
				}
				cif_dev->rdbk_buf[RDBK_L] = active_buf;
			} else if (mipi_id == RKCIF_STREAM_MIPI_ID1) {
				if (cif_dev->rdbk_buf[RDBK_M]) {
					v4l2_err(&cif_dev->v4l2_dev,
						 "multiple %s frame,frm_idx:%d,state:0x%x\n",
						 cif_dev->hdr.mode == HDR_X2 ? "short data in hdr_x2" : "medium data in hdr_x3",
						 stream->frame_idx,
						 cif_dev->rdbk_buf[RDBK_M]->vb.vb2_buf.state);
					cif_dev->rdbk_buf[RDBK_M]->vb.vb2_buf.state = VB2_BUF_STATE_ACTIVE;
					rkcif_buf_queue(&cif_dev->rdbk_buf[RDBK_M]->vb.vb2_buf);
				}

				cif_dev->rdbk_buf[RDBK_M] = active_buf;
				if (cif_dev->hdr.mode == HDR_X2)
					rkcif_rdbk_frame_end(stream);
			} else if (mipi_id == RKCIF_STREAM_MIPI_ID2) {
				if (cif_dev->rdbk_buf[RDBK_S]) {
					v4l2_err(&cif_dev->v4l2_dev,
						 "multiple %s frame, frm_idx:%d,state:0x%x\n",
						 cif_dev->hdr.mode == HDR_X2 ? "err short data in hdr_x3" : "short data in hdr_x3",
						 stream->frame_idx,
						 cif_dev->rdbk_buf[RDBK_S]->vb.vb2_buf.state);
					cif_dev->rdbk_buf[RDBK_S]->vb.vb2_buf.state = VB2_BUF_STATE_ACTIVE;
					rkcif_buf_queue(&cif_dev->rdbk_buf[RDBK_S]->vb.vb2_buf);
				}
				cif_dev->rdbk_buf[RDBK_S] = active_buf;
				if (cif_dev->hdr.mode == HDR_X3)
					rkcif_rdbk_frame_end(stream);
			}
			spin_unlock_irqrestore(&cif_dev->hdr_lock, lock_flags);
		} else {
			if (active_buf) {
				vb_done->vb2_buf.state = VB2_BUF_STATE_ACTIVE;
				rkcif_buf_queue(&vb_done->vb2_buf);
			}

			v4l2_info(&cif_dev->v4l2_dev,
				  "warning:hdr runs stream[%d], stream[0]:%s stream[1]:%s stream[2]:%s stream[3]:%s\n",
				  stream->id,
				  cif_dev->stream[0].state != RKCIF_STATE_STREAMING ? "stopped" : "running",
				  cif_dev->stream[1].state != RKCIF_STATE_STREAMING ? "stopped" : "running",
				  cif_dev->stream[2].state != RKCIF_STATE_STREAMING ? "stopped" : "running",
				  cif_dev->stream[3].state != RKCIF_STATE_STREAMING ? "stopped" : "running");
		}
	}
end:
	stream->frame_idx++;
}

u32 rkcif_get_sof(struct rkcif_device *cif_dev)
{
	u32 val = 0x0;
	struct rkcif_sensor_info *sensor = cif_dev->active_sensor;

	if (sensor->mbus.type == V4L2_MBUS_CSI2)
		val = rkcif_csi2_get_sof();
	else if (sensor->mbus.type == V4L2_MBUS_CCP2)
		val = rkcif_lvds_get_sof(cif_dev);
	else if (sensor->mbus.type == V4L2_MBUS_PARALLEL ||
		 sensor->mbus.type == V4L2_MBUS_BT656)
		val = rkcif_dvp_get_sof(cif_dev);

	return val;
}

static int rkcif_do_reset_work(struct rkcif_device *cif_dev,
				    enum rkmodule_reset_src reset_src)
{
	struct rkcif_pipeline *p = &cif_dev->pipe;
	struct rkcif_stream *stream = NULL;
	struct rkcif_stream *resume_stream[RKCIF_MAX_STREAM_MIPI] = { NULL };
	struct rkcif_sensor_info *terminal_sensor = &cif_dev->terminal_sensor;
	struct rkcif_resume_info *resume_info = &cif_dev->reset_work.resume_info;
	struct rkcif_timer *timer = &cif_dev->reset_watchdog_timer;
	int i, j, ret = 0;
	u32 on, sof_cnt;
	u64 fps;

	mutex_lock(&cif_dev->stream_lock);

	if (cif_dev->reset_work_cancel)
		goto unlock_stream;

	v4l2_dbg(1, rkcif_debug, &cif_dev->v4l2_dev, "do rkcif reset\n");

	fps = div_u64(timer->frame_end_cycle_us, 1000);
	for (i = 0, j = 0; i < RKCIF_MAX_STREAM_MIPI; i++) {
		stream = &cif_dev->stream[i];

		if (stream->state == RKCIF_STATE_STREAMING) {

			v4l2_dbg(1, rkcif_debug, &cif_dev->v4l2_dev,
				 "stream[%d] stopping\n", stream->id);

			stream->stopping = true;

			ret = wait_event_timeout(stream->wq_stopped,
						 stream->state != RKCIF_STATE_STREAMING,
						 msecs_to_jiffies(fps));
			if (!ret) {
				rkcif_stream_stop(stream);
				stream->stopping = false;
			}

			if (stream->id == RKCIF_STREAM_MIPI_ID0) {
				sof_cnt = rkcif_get_sof(cif_dev);
				v4l2_err(&cif_dev->v4l2_dev,
					 "%s: stream[%d] sync frmid & csi_sof, frm_id:%d, csi_sof:%d\n",
					 __func__,
					 stream->id,
					 stream->frame_idx,
					 sof_cnt);

				resume_info->frm_sync_seq = sof_cnt;
				if (stream->frame_idx != sof_cnt)
					stream->frame_idx = sof_cnt;
			}

			stream->state = RKCIF_STATE_RESET_IN_STREAMING;
			stream->is_fs_fe_not_paired = false;
			stream->fs_cnt_in_single_frame = 0;
			resume_stream[j] = stream;
			j += 1;

			v4l2_dbg(1, rkcif_debug, &cif_dev->v4l2_dev,
				 "%s stop stream[%d] in streaming, frm_id:%d, csi_sof:%d\n",
				 __func__, stream->id, stream->frame_idx, rkcif_csi2_get_sof());

		}
	}

	on = 0;
	for (i = 0; i < p->num_subdevs; i++) {

		if (p->subdevs[i] == terminal_sensor->sd) {

			if (reset_src == RKCIF_RESET_SRC_ERR_CSI2 ||
			    reset_src == RKCIF_RESET_SRC_ERR_HOTPLUG ||
			    reset_src == RKICF_RESET_SRC_ERR_CUTOFF) {

				ret = v4l2_subdev_call(p->subdevs[i], core, ioctl,
						       RKMODULE_SET_QUICK_STREAM, &on);
				if (ret)
					v4l2_err(&cif_dev->v4l2_dev, "quick stream off subdev:%s failed\n",
						 p->subdevs[i]->name);
			}
		} else {
			ret = v4l2_subdev_call(p->subdevs[i], video, s_stream, on);
		}

		if (ret)
			v4l2_err(&cif_dev->v4l2_dev, "%s:stream %s subdev:%s failed\n",
				 __func__, on ? "on" : "off", p->subdevs[i]->name);

	}

	rockchip_clear_system_status(SYS_STATUS_CIF0);

	rkcif_do_cru_reset(cif_dev);

	rkcif_disable_sys_clk(cif_dev->hw_dev);

	udelay(5);

	ret = rkcif_enable_sys_clk(cif_dev->hw_dev);
	if (ret < 0) {
		v4l2_err(&cif_dev->v4l2_dev, "%s:resume cif clk failed\n", __func__);
		goto unlock_stream;
	}

	for (i = 0; i < j; i++) {
		resume_stream[i]->fs_cnt_in_single_frame = 0;
		ret = rkcif_csi_stream_start(resume_stream[i]);
		if (ret) {
			v4l2_err(&cif_dev->v4l2_dev, "%s:resume stream[%d] failed\n",
				 __func__, stream->id);
			goto unlock_stream;
		}

		v4l2_dbg(1, rkcif_debug, &cif_dev->v4l2_dev,
			 "resume stream[%d], frm_idx:%d, csi_sof:%d\n",
			 resume_stream[i]->id, resume_stream[i]->frame_idx,
			 rkcif_csi2_get_sof());
	}

	rockchip_set_system_status(SYS_STATUS_CIF0);

	on = 1;
	for (i = 0; i < p->num_subdevs; i++) {

		if (p->subdevs[i] == terminal_sensor->sd) {

			rkcif_csi2_set_sof(resume_info->frm_sync_seq);

			if (reset_src == RKCIF_RESET_SRC_ERR_CSI2 ||
			    reset_src == RKCIF_RESET_SRC_ERR_HOTPLUG ||
			    reset_src == RKICF_RESET_SRC_ERR_CUTOFF) {
				ret = v4l2_subdev_call(p->subdevs[i], core, ioctl,
						       RKMODULE_SET_QUICK_STREAM, &on);
				if (ret)
					v4l2_err(&cif_dev->v4l2_dev,
						 "quick stream on subdev:%s failed\n",
						 p->subdevs[i]->name);
			}
		} else {
			if (p->subdevs[i] == terminal_sensor->sd)
				rkcif_csi2_set_sof(resume_info->frm_sync_seq);

			ret = v4l2_subdev_call(p->subdevs[i], video, s_stream, on);
		}

		if (ret)
			v4l2_err(&cif_dev->v4l2_dev, "reset subdev:%s failed\n",
				 p->subdevs[i]->name);
	}

	rkcif_start_luma(&cif_dev->luma_vdev,
			 cif_dev->stream[RKCIF_STREAM_MIPI_ID0].cif_fmt_in);

	rkcif_monitor_reset_event(cif_dev);

	v4l2_dbg(1, rkcif_debug, &cif_dev->v4l2_dev, "do rkcif reset successfully!\n");

	mutex_unlock(&cif_dev->stream_lock);
	return 0;

unlock_stream:
	mutex_unlock(&cif_dev->stream_lock);
	return ret;
}

void rkcif_reset_work(struct work_struct *work)
{
	struct rkcif_work_struct *reset_work = container_of(work,
							    struct rkcif_work_struct,
							    work);
	struct rkcif_device *dev = container_of(reset_work,
						struct rkcif_device,
						reset_work);
	struct rkcif_timer *timer = &dev->reset_watchdog_timer;
	int ret;

	ret = rkcif_do_reset_work(dev, reset_work->reset_src);
	if (ret)
		v4l2_info(&dev->v4l2_dev, "do reset work failed!\n");

	timer->has_been_init = false;
}

static bool rkcif_is_reduced_frame_rate(struct rkcif_device *dev)
{
	struct rkcif_timer *timer = &dev->reset_watchdog_timer;
	struct rkcif_stream *stream = &dev->stream[RKCIF_STREAM_MIPI_ID0];
	struct v4l2_rect *raw_rect = &dev->terminal_sensor.raw_rect;
	u64 fps, timestamp0, timestamp1, diff_time;
	unsigned long fps_flags = 0;
	unsigned int deviation = 1;
	bool is_reduced = false;

	spin_lock_irqsave(&stream->fps_lock, fps_flags);
	timestamp0 = stream->fps_stats.frm0_timestamp;
	timestamp1 = stream->fps_stats.frm1_timestamp;
	spin_unlock_irqrestore(&stream->fps_lock, fps_flags);

	fps = timestamp0 > timestamp1 ?
	      timestamp0 - timestamp1 : timestamp1 - timestamp0;
	fps =  div_u64(fps, 1000);
	diff_time = fps > timer->frame_end_cycle_us ?
		    fps - timer->frame_end_cycle_us : 0;
	deviation = DIV_ROUND_UP(timer->vts, 100);

	v4l2_dbg(1, rkcif_debug, &dev->v4l2_dev, "diff_time:%lld,devi_t:%ld,devi_h:%d\n",
		  diff_time, timer->line_end_cycle * deviation, deviation);

	if (diff_time > timer->line_end_cycle * deviation) {
		s32 vblank = 0;
		unsigned int vts;

		is_reduced = true;
		vblank = rkcif_get_sensor_vblank(dev);
		vts = vblank + timer->raw_height;

		v4l2_dbg(1, rkcif_debug, &dev->v4l2_dev, "old vts:%d,new vts:%d\n", timer->vts, vts);

		v4l2_dbg(1, rkcif_debug, &dev->v4l2_dev,
			  "reduce frame rate,vblank:%d, height(raw output):%d, fps:%lld, frm_end_t:%ld, line_t:%ld, diff:%lld\n",
			  rkcif_get_sensor_vblank(dev),
			  raw_rect->height,
			  fps,
			  timer->frame_end_cycle_us,
			  timer->line_end_cycle,
			  diff_time);

		timer->vts = vts;
		timer->frame_end_cycle_us = fps;
		timer->line_end_cycle = div_u64(timer->frame_end_cycle_us, timer->vts);
	} else {
		is_reduced = false;
	}

	timer->frame_end_cycle_us = fps;

	fps = div_u64(fps, 1000);
	fps = fps * timer->frm_num_of_monitor_cycle;
	timer->cycle = msecs_to_jiffies(fps);
	timer->timer.expires = jiffies + timer->cycle;

	return is_reduced;

}

static void rkcif_init_reset_work(struct rkcif_timer *timer)
{
	struct rkcif_device *dev = container_of(timer,
						struct rkcif_device,
						reset_watchdog_timer);
	unsigned long lock_flags = 0;

	if (timer->has_been_init)
		return;

	v4l2_info(&dev->v4l2_dev,
		  "do reset work schedule, run_cnt:%d, reset source:%d\n",
		  timer->run_cnt, timer->reset_src);

	spin_lock_irqsave(&timer->timer_lock, lock_flags);
	timer->is_running = false;
	timer->is_triggered = false;
	timer->csi2_err_cnt_odd = 0;
	timer->csi2_err_cnt_even = 0;
	timer->csi2_err_fs_fe_cnt = 0;
	timer->notifer_called_cnt = 0;
	timer->last_buf_wakeup_cnt = dev->buf_wake_up_cnt;
	spin_unlock_irqrestore(&timer->timer_lock, lock_flags);

	dev->reset_work.reset_src = timer->reset_src;
	if (schedule_work(&dev->reset_work.work)) {
		timer->has_been_init = true;
		v4l2_info(&dev->v4l2_dev,
			 "schedule reset work successfully\n");
	} else {
		v4l2_info(&dev->v4l2_dev,
			 "schedule reset work failed\n");
	}
}

void rkcif_reset_watchdog_timer_handler(struct timer_list *t)
{
	struct rkcif_timer *timer = container_of(t, struct rkcif_timer, timer);
	struct rkcif_device *dev = container_of(timer,
						struct rkcif_device,
						reset_watchdog_timer);
	struct rkcif_sensor_info *terminal_sensor = &dev->terminal_sensor;

	unsigned long lock_flags = 0;
	unsigned int i, stream_num = 1;
	int ret, is_reset = 0;
	struct rkmodule_vicap_reset_info rst_info;

	if (dev->hdr.mode == NO_HDR) {
		i = 0;
		if (dev->stream[i].state != RKCIF_STATE_STREAMING)
			goto end_detect;
	} else {
		if (dev->hdr.mode == HDR_X3)
			stream_num = 3;
		else if (dev->hdr.mode == HDR_X2)
			stream_num = 2;

		for (i = 0; i < stream_num; i++) {
			if (dev->stream[i].state != RKCIF_STATE_STREAMING)
				goto end_detect;
		}
	}

	timer->run_cnt += 1;

	if (timer->last_buf_wakeup_cnt < dev->buf_wake_up_cnt) {

		v4l2_dbg(1, rkcif_debug, &dev->v4l2_dev,
			 "info: frame end still update(%d, %d) in detecting cnt:%d, mode:%d\n",
			  timer->last_buf_wakeup_cnt, dev->buf_wake_up_cnt,
			  timer->run_cnt, timer->monitor_mode);

		timer->last_buf_wakeup_cnt = dev->buf_wake_up_cnt;

		rkcif_is_reduced_frame_rate(dev);

		if (timer->monitor_mode == RKCIF_MONITOR_MODE_HOTPLUG) {
			ret = v4l2_subdev_call(terminal_sensor->sd,
					       core, ioctl,
					       RKMODULE_GET_VICAP_RST_INFO,
					       &rst_info);
			if (ret)
				is_reset = 0;
			else
				is_reset = rst_info.is_reset;
			rst_info.is_reset = 0;
			timer->reset_src = RKCIF_RESET_SRC_ERR_HOTPLUG;
			v4l2_subdev_call(terminal_sensor->sd, core, ioctl,
					 RKMODULE_SET_VICAP_RST_INFO, &rst_info);
			if (!is_reset)
				is_reset = rkcif_is_csi2_err_trigger_reset(timer);
		} else if (timer->monitor_mode == RKCIF_MONITOR_MODE_CONTINUE) {
			is_reset = rkcif_is_csi2_err_trigger_reset(timer);
		} else if (timer->monitor_mode == RKCIF_MONITOR_MODE_TRIGGER) {
			is_reset = timer->is_csi2_err_occurred;
			if (is_reset)
				timer->reset_src = RKCIF_RESET_SRC_ERR_CSI2;
			timer->is_csi2_err_occurred = false;
		}

		if (is_reset) {
			rkcif_init_reset_work(timer);
			return;
		}

		if (timer->monitor_mode == RKCIF_MONITOR_MODE_CONTINUE ||
		    timer->monitor_mode == RKCIF_MONITOR_MODE_HOTPLUG) {
			if (timer->run_cnt == timer->max_run_cnt)
				timer->run_cnt = 0x0;
			mod_timer(&timer->timer, jiffies + timer->cycle);
		} else {
			if (timer->run_cnt <= timer->max_run_cnt) {
				mod_timer(&timer->timer, jiffies + timer->cycle);
			} else {
				spin_lock_irqsave(&timer->timer_lock, lock_flags);
				timer->is_triggered = false;
				timer->is_running = false;
				spin_unlock_irqrestore(&timer->timer_lock, lock_flags);
				v4l2_info(&dev->v4l2_dev, "stop reset detecting!\n");
			}
		}
	} else if (timer->last_buf_wakeup_cnt == dev->buf_wake_up_cnt) {

		bool is_reduced = rkcif_is_reduced_frame_rate(dev);

		if (is_reduced) {
			mod_timer(&timer->timer, jiffies + timer->cycle);
			v4l2_info(&dev->v4l2_dev, "%s fps is reduced\n", __func__);
		} else {

			v4l2_info(&dev->v4l2_dev,
				  "do reset work due to frame end is stopped, run_cnt:%d\n",
				  timer->run_cnt);

			timer->reset_src = RKICF_RESET_SRC_ERR_CUTOFF;
			rkcif_init_reset_work(timer);
		}
	}

	return;
end_detect:

	spin_lock_irqsave(&timer->timer_lock, lock_flags);
	timer->is_triggered = false;
	timer->is_running = false;
	spin_unlock_irqrestore(&timer->timer_lock, lock_flags);

	v4l2_info(&dev->v4l2_dev,
		  "stream[%d] is stopped, stop reset detect!\n",
		  dev->stream[i].id);
}

int rkcif_reset_notifier(struct notifier_block *nb,
			 unsigned long action, void *data)
{
	struct rkcif_device *dev = container_of(nb, struct rkcif_device, reset_notifier);
	struct rkcif_timer *timer = &dev->reset_watchdog_timer;
	unsigned long lock_flags = 0, val;

	if (timer->is_running) {
		val = action & CSI2_ERR_COUNT_ALL_MASK;
		spin_lock_irqsave(&timer->csi2_err_lock, lock_flags);
		if ((val % timer->csi2_err_ref_cnt) == 0) {
			timer->notifer_called_cnt++;
			if ((timer->notifer_called_cnt % 2) == 0)
				timer->csi2_err_cnt_even = val;
			else
				timer->csi2_err_cnt_odd = val;
		}

		timer->csi2_err_fs_fe_cnt = (action & CSI2_ERR_FSFE_MASK) >> 8;
		spin_unlock_irqrestore(&timer->csi2_err_lock, lock_flags);
	}

	return 0;
}

void rkcif_irq_pingpong(struct rkcif_device *cif_dev)
{
	struct rkcif_stream *stream;
	struct rkcif_stream *detect_stream = &cif_dev->stream[0];
	struct v4l2_mbus_config *mbus;
	unsigned int intstat = 0x0, i = 0xff, bak_intstat = 0x0;
	int ret = 0;

	if (!cif_dev->active_sensor)
		return;

	mbus = &cif_dev->active_sensor->mbus;
	if ((mbus->type == V4L2_MBUS_CSI2 ||
	     mbus->type == V4L2_MBUS_CCP2) &&
	    (cif_dev->chip_id == CHIP_RK1808_CIF ||
	     cif_dev->chip_id == CHIP_RV1126_CIF ||
	     cif_dev->chip_id == CHIP_RK3568_CIF)) {
		int mipi_id;
		u32 lastline = 0;

		intstat = rkcif_read_register(cif_dev, CIF_REG_MIPI_LVDS_INTSTAT);
		lastline = rkcif_read_register(cif_dev, CIF_REG_MIPI_LVDS_LINE_LINE_CNT_ID0_1);

		/* clear all interrupts that has been triggered */
		rkcif_write_register(cif_dev, CIF_REG_MIPI_LVDS_INTSTAT, intstat);

		if (intstat & CSI_FIFO_OVERFLOW) {
			cif_dev->irq_stats.csi_overflow_cnt++;
			v4l2_err(&cif_dev->v4l2_dev,
				 "ERROR: csi fifo overflow, intstat:0x%x, lastline:%d!!\n",
				  intstat, lastline);
			return;
		}

		if (intstat & CSI_BANDWIDTH_LACK) {
			cif_dev->irq_stats.csi_bwidth_lack_cnt++;
			v4l2_err(&cif_dev->v4l2_dev,
				 "ERROR: csi bandwidth lack, intstat:0x%x!!\n",
				 intstat);
			return;
		}

		if (intstat & CSI_ALL_ERROR_INTEN) {
			cif_dev->irq_stats.all_err_cnt++;
			v4l2_err(&cif_dev->v4l2_dev,
				 "ERROR: CSI_ALL_ERROR_INTEN:0x%x!!\n", intstat);
			return;
		}

		if ((intstat & (CSI_FRAME0_START_ID0 | CSI_FRAME1_START_ID0)) ==
			(CSI_FRAME0_START_ID0 | CSI_FRAME1_START_ID0)) {
			v4l2_err(&cif_dev->v4l2_dev, "%s:ERR: double fs in one fs int\n",
				 __func__);
		}

		if (intstat & CSI_FRAME0_START_ID0) {
			if (mbus->type == V4L2_MBUS_CSI2)
				rkcif_csi2_event_inc_sof();
			else if (mbus->type == V4L2_MBUS_CCP2)
				rkcif_lvds_event_inc_sof(cif_dev);

			if (detect_stream->fs_cnt_in_single_frame >= 1)
				v4l2_warn(&cif_dev->v4l2_dev,
					  "%s:warn: fs has been incread:%u(frm0)\n",
					  __func__, detect_stream->fs_cnt_in_single_frame);
			detect_stream->fs_cnt_in_single_frame++;
		}

		if (intstat & CSI_FRAME1_START_ID0) {
			if (mbus->type == V4L2_MBUS_CSI2)
				rkcif_csi2_event_inc_sof();
			else if (mbus->type == V4L2_MBUS_CCP2)
				rkcif_lvds_event_inc_sof(cif_dev);

			if (detect_stream->fs_cnt_in_single_frame >= 1)
				v4l2_warn(&cif_dev->v4l2_dev, "%s:warn: fs has been incread:%u(frm1)\n",
				 __func__, detect_stream->fs_cnt_in_single_frame);
			detect_stream->fs_cnt_in_single_frame++;
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

			if (stream->crop_dyn_en)
				rkcif_dynamic_crop(stream);

			rkcif_update_stream(cif_dev, stream, mipi_id);
			rkcif_monitor_reset_event(cif_dev);
			if (mipi_id == RKCIF_STREAM_MIPI_ID0) {
				if ((intstat & (CSI_FRAME1_START_ID0 | CSI_FRAME0_START_ID0)) == 0 &&
				    detect_stream->fs_cnt_in_single_frame > 1) {
					v4l2_err(&cif_dev->v4l2_dev,
						 "%s ERR: multi fs in oneframe, bak_int:0x%x, fs_num:%u\n",
						 __func__,
						 bak_intstat,
						 detect_stream->fs_cnt_in_single_frame);
					detect_stream->is_fs_fe_not_paired = true;
					detect_stream->fs_cnt_in_single_frame = 0;
				} else {
					detect_stream->fs_cnt_in_single_frame--;
				}
			}
		}
		cif_dev->irq_stats.all_frm_end_cnt++;
	} else {
		u32 lastline, lastpix, ctl;
		u32 cif_frmst, frmid, int_en;
		struct rkcif_stream *stream;
		int ch_id;

		intstat = rkcif_read_register(cif_dev, CIF_REG_DVP_INTSTAT);
		cif_frmst = rkcif_read_register(cif_dev, CIF_REG_DVP_FRAME_STATUS);
		lastline = rkcif_read_register(cif_dev, CIF_REG_DVP_LAST_LINE);
		lastline = CIF_FETCH_Y_LAST_LINE(lastline);
		lastpix = rkcif_read_register(cif_dev, CIF_REG_DVP_LAST_PIX);
		lastpix =  CIF_FETCH_Y_LAST_LINE(lastpix);
		ctl = rkcif_read_register(cif_dev, CIF_REG_DVP_CTRL);

		rkcif_write_register(cif_dev, CIF_REG_DVP_INTSTAT, intstat);

		stream = &cif_dev->stream[RKCIF_STREAM_CIF];

		if ((intstat & LINE_INT_END) && !(intstat & FRAME_END) &&
		    (cif_dev->dvp_sof_in_oneframe == 0)) {
			if ((intstat & (PRE_INF_FRAME_END | PST_INF_FRAME_END)) == 0x0) {
				if ((intstat & INTSTAT_ERR) == 0x0) {
					rkcif_dvp_event_inc_sof(cif_dev);
					int_en = rkcif_read_register(cif_dev, CIF_REG_DVP_INTEN);
					int_en &= ~LINE_INT_EN;
					rkcif_write_register(cif_dev, CIF_REG_DVP_INTEN, int_en);
					cif_dev->dvp_sof_in_oneframe = 1;
				}
			}
		}

		if (intstat & BUS_ERR) {
			cif_dev->irq_stats.dvp_bus_err_cnt++;
			v4l2_info(&cif_dev->v4l2_dev, "dvp bus err\n");
		}

		if (intstat & DVP_ALL_OVERFLOW) {
			cif_dev->irq_stats.dvp_overflow_cnt++;
			v4l2_info(&cif_dev->v4l2_dev, "dvp overflow err\n");
		}

		if (intstat & LINE_ERR) {
			cif_dev->irq_stats.dvp_line_err_cnt++;
			v4l2_info(&cif_dev->v4l2_dev, "dvp line err\n");
		}

		if (intstat & PIX_ERR) {
			cif_dev->irq_stats.dvp_pix_err_cnt++;
			v4l2_info(&cif_dev->v4l2_dev, "dvp pix err\n");
		}

		if (intstat & INTSTAT_ERR) {
			cif_dev->irq_stats.all_err_cnt++;
			v4l2_err(&cif_dev->v4l2_dev,
				 "ERROR: DVP_ALL_ERROR_INTEN:0x%x!!\n", intstat);
		}

		/* There are two irqs enabled:
		 *  - PST_INF_FRAME_END: cif FIFO is ready,
		 *    this is prior to FRAME_END
		 *  - FRAME_END: cif has saved frame to memory,
		 *    a frame ready
		 */
		if ((intstat & PST_INF_FRAME_END)) {
			stream = &cif_dev->stream[RKCIF_STREAM_CIF];
			if (stream->stopping)
				/* To stop CIF ASAP, before FRAME_END irq */
				rkcif_write_register(cif_dev, CIF_REG_DVP_CTRL,
						     ctl & (~ENABLE_CAPTURE));
		}

		if (cif_dev->chip_id <= CHIP_RK1808_CIF) {

			stream = &cif_dev->stream[RKCIF_STREAM_CIF];

			if ((intstat & FRAME_END)) {
				struct vb2_v4l2_buffer *vb_done = NULL;

				int_en = rkcif_read_register(cif_dev, CIF_REG_DVP_INTEN);
				int_en |= LINE_INT_EN;
				rkcif_write_register(cif_dev, CIF_REG_DVP_INTEN, int_en);
				cif_dev->dvp_sof_in_oneframe = 0;

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

					cif_dev->dvp_sof_in_oneframe = 1;
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

				spin_lock(&stream->fps_lock);
				if (stream->frame_phase & CIF_CSI_FRAME0_READY)
					stream->fps_stats.frm0_timestamp = ktime_get_ns();
				else if (stream->frame_phase & CIF_CSI_FRAME1_READY)
					stream->fps_stats.frm1_timestamp = ktime_get_ns();
				spin_unlock(&stream->fps_lock);

				ret = rkcif_assign_new_buffer_oneframe(stream,
								 RKCIF_YUV_ADDR_STATE_UPDATE);

				if (vb_done && (!ret)) {
					vb_done->sequence = stream->frame_idx;
					rkcif_vb_done_oneframe(stream, vb_done);
				}
				stream->frame_idx++;
				cif_dev->irq_stats.all_frm_end_cnt++;
			}
		} else {
			for (i = 0; i < RKCIF_MAX_STREAM_DVP; i++) {
				ch_id = rkcif_dvp_g_ch_id(&cif_dev->v4l2_dev, &intstat, cif_frmst);

				if (ch_id < 0)
					continue;

				if (ch_id == RKCIF_STREAM_MIPI_ID0) {
					int_en = rkcif_read_register(cif_dev, CIF_REG_DVP_INTEN);
					int_en |= LINE_INT_EN;
					rkcif_write_register(cif_dev, CIF_REG_DVP_INTEN, int_en);
					cif_dev->dvp_sof_in_oneframe = 0;
				}

				stream = &cif_dev->stream[ch_id];

				if (stream->stopping) {
					rkcif_stream_stop(stream);
					stream->stopping = false;
					wake_up(&stream->wq_stopped);
					continue;
				}

				if (stream->state != RKCIF_STATE_STREAMING)
					continue;

				switch (ch_id) {
				case RKCIF_STREAM_MIPI_ID0:
					stream->frame_phase = DVP_FRM_STS_ID0(cif_frmst);
					break;
				case RKCIF_STREAM_MIPI_ID1:
					stream->frame_phase = DVP_FRM_STS_ID1(cif_frmst);
					break;
				case RKCIF_STREAM_MIPI_ID2:
					stream->frame_phase = DVP_FRM_STS_ID2(cif_frmst);
					break;
				case RKCIF_STREAM_MIPI_ID3:
					stream->frame_phase = DVP_FRM_STS_ID3(cif_frmst);
					break;
				}


				frmid = CIF_GET_FRAME_ID(cif_frmst);
				if ((frmid == 0xfffd) || (frmid == 0xfffe)) {
					v4l2_info(&cif_dev->v4l2_dev, "frmid:%d, frmstat:0x%x\n",
						  frmid, cif_frmst);
					rkcif_write_register(cif_dev, CIF_REG_DVP_FRAME_STATUS,
							     FRAME_STAT_CLS);
				}
				rkcif_update_stream(cif_dev, stream, ch_id);
				cif_dev->irq_stats.all_frm_end_cnt++;
			}
		}

		if (stream->crop_dyn_en)
			rkcif_dynamic_crop(stream);
	}
}

void rkcif_irq_lite_lvds(struct rkcif_device *cif_dev)
{
	struct rkcif_stream *stream;
	struct v4l2_mbus_config *mbus = &cif_dev->active_sensor->mbus;
	unsigned int intstat, i = 0xff;

	if (mbus->type == V4L2_MBUS_CCP2 &&
	    cif_dev->chip_id == CHIP_RV1126_CIF_LITE) {
		int mipi_id;
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

			rkcif_update_stream(cif_dev, stream, mipi_id);
			rkcif_monitor_reset_event(cif_dev);
		}
		cif_dev->irq_stats.all_frm_end_cnt++;
	}
}
