/*
 * Rockchip isp1 driver
 *
 * Copyright (C) 2017 Rockchip Electronics Co., Ltd.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/clk.h>
#include <linux/iopoll.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/videodev2.h>
#include <linux/vmalloc.h>
#include <linux/kfifo.h>
#include <linux/interrupt.h>
#include <linux/rk-preisp.h>
#include <linux/iommu.h>
#include <media/v4l2-event.h>
#include <media/media-entity.h>

#include "common.h"
#include "regs.h"

/*
 * NOTE: MIPI controller and input MUX are also configured in this file,
 * because ISP Subdev is not only describe ISP submodule(input size,format, output size, format),
 * but also a virtual route device.
 */

/*
 * There are many variables named with format/frame in below code,
 * please see here for their meaning.
 *
 * Cropping regions of ISP
 *
 * +---------------------------------------------------------+
 * | Sensor image                                            |
 * | +---------------------------------------------------+   |
 * | | ISP_ACQ (for black level)                         |   |
 * | | in_frm                                            |   |
 * | | +--------------------------------------------+    |   |
 * | | |    ISP_OUT                                 |    |   |
 * | | |    in_crop                                 |    |   |
 * | | |    +---------------------------------+     |    |   |
 * | | |    |   ISP_IS                        |     |    |   |
 * | | |    |   rkisp1_isp_subdev: out_crop   |     |    |   |
 * | | |    +---------------------------------+     |    |   |
 * | | +--------------------------------------------+    |   |
 * | +---------------------------------------------------+   |
 * +---------------------------------------------------------+
 */

static inline struct rkisp1_device *sd_to_isp_dev(struct v4l2_subdev *sd)
{
	return container_of(sd->v4l2_dev, struct rkisp1_device, v4l2_dev);
}

/* Get sensor by enabled media link */
static struct v4l2_subdev *get_remote_sensor(struct v4l2_subdev *sd)
{
	struct media_pad *local, *remote;
	struct media_entity *sensor_me;

	local = &sd->entity.pads[RKISP1_ISP_PAD_SINK];
	if (!local)
		return NULL;
	remote = media_entity_remote_pad(local);
	if (!remote)
		return NULL;

	sensor_me = remote->entity;

	return media_entity_to_v4l2_subdev(sensor_me);
}

static void get_remote_mipi_sensor(struct rkisp1_device *dev,
				  struct v4l2_subdev **sensor_sd)
{
	struct media_graph graph;
	struct media_entity *entity = &dev->isp_sdev.sd.entity;
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

static struct rkisp1_sensor_info *sd_to_sensor(struct rkisp1_device *dev,
					       struct v4l2_subdev *sd)
{
	int i;

	for (i = 0; i < dev->num_sensors; ++i)
		if (dev->sensors[i].sd == sd)
			return &dev->sensors[i];

	return NULL;
}

int rkisp1_update_sensor_info(struct rkisp1_device *dev)
{
	struct v4l2_subdev *sd = &dev->isp_sdev.sd;
	struct rkisp1_sensor_info *sensor;
	struct v4l2_subdev *sensor_sd;
	int ret = 0;

	sensor_sd = get_remote_sensor(sd);
	if (!sensor_sd)
		return -ENODEV;

	sensor = sd_to_sensor(dev, sensor_sd);
	ret = v4l2_subdev_call(sensor->sd, video, g_mbus_config,
			       &sensor->mbus);
	if (ret && ret != -ENOIOCTLCMD)
		return ret;
	sensor->fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	ret = v4l2_subdev_call(sensor->sd, pad, get_fmt,
			       &sensor->cfg, &sensor->fmt);
	if (ret && ret != -ENOIOCTLCMD)
		return ret;
	dev->active_sensor = sensor;

	return ret;
}

u32 rkisp1_mbus_pixelcode_to_v4l2(u32 pixelcode)
{
	u32 pixelformat;

	switch (pixelcode) {
	case MEDIA_BUS_FMT_SBGGR8_1X8:
		pixelformat = V4L2_PIX_FMT_SBGGR8;
		break;
	case MEDIA_BUS_FMT_SGBRG8_1X8:
		pixelformat = V4L2_PIX_FMT_SGBRG8;
		break;
	case MEDIA_BUS_FMT_SGRBG8_1X8:
		pixelformat = V4L2_PIX_FMT_SGRBG8;
		break;
	case MEDIA_BUS_FMT_SRGGB8_1X8:
		pixelformat = V4L2_PIX_FMT_SRGGB8;
		break;
	case MEDIA_BUS_FMT_SBGGR10_1X10:
		pixelformat = V4L2_PIX_FMT_SBGGR10;
		break;
	case MEDIA_BUS_FMT_SGBRG10_1X10:
		pixelformat = V4L2_PIX_FMT_SGBRG10;
		break;
	case MEDIA_BUS_FMT_SGRBG10_1X10:
		pixelformat = V4L2_PIX_FMT_SGRBG10;
		break;
	case MEDIA_BUS_FMT_SRGGB10_1X10:
		pixelformat = V4L2_PIX_FMT_SRGGB10;
		break;
	case MEDIA_BUS_FMT_SBGGR12_1X12:
		pixelformat = V4L2_PIX_FMT_SBGGR12;
		break;
	case MEDIA_BUS_FMT_SGBRG12_1X12:
		pixelformat = V4L2_PIX_FMT_SGBRG12;
		break;
	case MEDIA_BUS_FMT_SGRBG12_1X12:
		pixelformat = V4L2_PIX_FMT_SGRBG12;
		break;
	case MEDIA_BUS_FMT_SRGGB12_1X12:
		pixelformat = V4L2_PIX_FMT_SRGGB12;
		break;
	default:
		pixelformat = V4L2_PIX_FMT_SRGGB10;
	}

	return pixelformat;
}

/****************  register operations ****************/

static void rkisp1_config_clk(struct rkisp1_device *dev, int on)
{
	u32 val = !on ? 0 :
		CIF_ICCL_ISP_CLK | CIF_ICCL_CP_CLK | CIF_ICCL_MRSZ_CLK |
		CIF_ICCL_SRSZ_CLK | CIF_ICCL_JPEG_CLK | CIF_ICCL_MI_CLK |
		CIF_ICCL_IE_CLK | CIF_ICCL_MIPI_CLK | CIF_ICCL_DCROP_CLK;

	writel(val, dev->base_addr + CIF_ICCL);

#if RKISP1_RK3326_USE_OLDMIPI
	if (dev->isp_ver == ISP_V13) {
#else
	if (dev->isp_ver == ISP_V12 || dev->isp_ver == ISP_V13) {
#endif
		val = !on ? 0 :
		      CIF_CLK_CTRL_MI_Y12 | CIF_CLK_CTRL_MI_SP |
		      CIF_CLK_CTRL_MI_RAW0 | CIF_CLK_CTRL_MI_RAW1 |
		      CIF_CLK_CTRL_MI_READ | CIF_CLK_CTRL_MI_RAWRD |
		      CIF_CLK_CTRL_CP | CIF_CLK_CTRL_IE;

		writel(val, dev->base_addr + CIF_VI_ISP_CLK_CTRL_V12);
	}
}

/*
 * Image Stabilization.
 * This should only be called when configuring CIF
 * or at the frame end interrupt
 */
static void rkisp1_config_ism(struct rkisp1_device *dev)
{
	void __iomem *base = dev->base_addr;
	struct v4l2_rect *out_crop = &dev->isp_sdev.out_crop;
	u32 val;

	writel(0, base + CIF_ISP_IS_RECENTER);
	writel(0, base + CIF_ISP_IS_MAX_DX);
	writel(0, base + CIF_ISP_IS_MAX_DY);
	writel(0, base + CIF_ISP_IS_DISPLACE);
	writel(out_crop->left, base + CIF_ISP_IS_H_OFFS);
	writel(out_crop->top, base + CIF_ISP_IS_V_OFFS);
	writel(out_crop->width, base + CIF_ISP_IS_H_SIZE);
	if (dev->stream[RKISP1_STREAM_SP].interlaced)
		writel(out_crop->height / 2, base + CIF_ISP_IS_V_SIZE);
	else
		writel(out_crop->height, base + CIF_ISP_IS_V_SIZE);

	/* IS(Image Stabilization) is always on, working as output crop */
	writel(1, base + CIF_ISP_IS_CTRL);
	val = readl(base + CIF_ISP_CTRL);
	val |= CIF_ISP_CTRL_ISP_CFG_UPD;
	writel(val, base + CIF_ISP_CTRL);
}

/*
 * configure isp blocks with input format, size......
 */
static int rkisp1_config_isp(struct rkisp1_device *dev)
{
	struct ispsd_in_fmt *in_fmt;
	struct ispsd_out_fmt *out_fmt;
	struct v4l2_mbus_framefmt *in_frm;
	struct v4l2_rect *in_crop;
	struct rkisp1_sensor_info *sensor;
	void __iomem *base = dev->base_addr;
	u32 isp_ctrl = 0;
	u32 irq_mask = 0;
	u32 signal = 0;
	u32 acq_mult = 0;
	u32 acq_prop = 0;

	sensor = dev->active_sensor;
	in_frm = &dev->isp_sdev.in_frm;
	in_fmt = &dev->isp_sdev.in_fmt;
	out_fmt = &dev->isp_sdev.out_fmt;
	in_crop = &dev->isp_sdev.in_crop;

	if (in_fmt->fmt_type == FMT_BAYER) {
		acq_mult = 1;
		if (out_fmt->fmt_type == FMT_BAYER) {
			if (sensor && sensor->mbus.type == V4L2_MBUS_BT656)
				isp_ctrl =
					CIF_ISP_CTRL_ISP_MODE_RAW_PICT_ITU656;
			else
				isp_ctrl =
					CIF_ISP_CTRL_ISP_MODE_RAW_PICT;
		} else {
			/* demosaicing bypass for grey sensor */
			if (in_fmt->mbus_code == MEDIA_BUS_FMT_Y8_1X8 ||
			    in_fmt->mbus_code == MEDIA_BUS_FMT_Y10_1X10 ||
			    in_fmt->mbus_code == MEDIA_BUS_FMT_Y12_1X12)
				writel(CIF_ISP_DEMOSAIC_BYPASS |
				       CIF_ISP_DEMOSAIC_TH(0xc),
				       base + CIF_ISP_DEMOSAIC);
			else
				writel(CIF_ISP_DEMOSAIC_TH(0xc),
				       base + CIF_ISP_DEMOSAIC);

			if (sensor && sensor->mbus.type == V4L2_MBUS_BT656)
				isp_ctrl = CIF_ISP_CTRL_ISP_MODE_BAYER_ITU656;
			else
				isp_ctrl = CIF_ISP_CTRL_ISP_MODE_BAYER_ITU601;
		}

		if (dev->isp_inp == INP_DMARX_ISP)
			acq_prop = CIF_ISP_ACQ_PROP_DMA_RGB;
	} else if (in_fmt->fmt_type == FMT_YUV) {
		acq_mult = 2;
		if (sensor && sensor->mbus.type == V4L2_MBUS_CSI2) {
			isp_ctrl = CIF_ISP_CTRL_ISP_MODE_ITU601;
		} else {
			if (sensor && sensor->mbus.type == V4L2_MBUS_BT656)
				isp_ctrl = CIF_ISP_CTRL_ISP_MODE_ITU656;
			else
				isp_ctrl = CIF_ISP_CTRL_ISP_MODE_ITU601;
		}

		irq_mask |= CIF_ISP_DATA_LOSS;
		if (dev->isp_inp == INP_DMARX_ISP)
			acq_prop = CIF_ISP_ACQ_PROP_DMA_YUV;
	}

	/* Set up input acquisition properties */
	if (sensor && (sensor->mbus.type == V4L2_MBUS_BT656 ||
		sensor->mbus.type == V4L2_MBUS_PARALLEL)) {
		if (sensor->mbus.flags &
			V4L2_MBUS_PCLK_SAMPLE_RISING)
			signal = CIF_ISP_ACQ_PROP_POS_EDGE;
	}

	if (sensor && sensor->mbus.type == V4L2_MBUS_PARALLEL) {
		if (sensor->mbus.flags & V4L2_MBUS_VSYNC_ACTIVE_LOW)
			signal |= CIF_ISP_ACQ_PROP_VSYNC_LOW;

		if (sensor->mbus.flags & V4L2_MBUS_HSYNC_ACTIVE_LOW)
			signal |= CIF_ISP_ACQ_PROP_HSYNC_LOW;
	}

	writel(isp_ctrl, base + CIF_ISP_CTRL);
	acq_prop |= signal | in_fmt->yuv_seq |
		CIF_ISP_ACQ_PROP_BAYER_PAT(in_fmt->bayer_pat) |
		CIF_ISP_ACQ_PROP_FIELD_SEL_ALL;
	writel(acq_prop, base + CIF_ISP_ACQ_PROP);
	writel(0, base + CIF_ISP_ACQ_NR_FRAMES);

	/* Acquisition Size */
	writel(0, base + CIF_ISP_ACQ_H_OFFS);
	writel(0, base + CIF_ISP_ACQ_V_OFFS);
	writel(acq_mult * in_frm->width, base + CIF_ISP_ACQ_H_SIZE);

	/* ISP Out Area */
	writel(in_crop->left, base + CIF_ISP_OUT_H_OFFS);
	writel(in_crop->top, base + CIF_ISP_OUT_V_OFFS);
	writel(in_crop->width, base + CIF_ISP_OUT_H_SIZE);

	if (dev->stream[RKISP1_STREAM_SP].interlaced) {
		writel(in_frm->height / 2, base + CIF_ISP_ACQ_V_SIZE);
		writel(in_crop->height / 2, base + CIF_ISP_OUT_V_SIZE);
	} else {
		writel(in_frm->height, base + CIF_ISP_ACQ_V_SIZE);
		writel(in_crop->height, base + CIF_ISP_OUT_V_SIZE);
	}

	/* interrupt mask */
	irq_mask |= CIF_ISP_FRAME | CIF_ISP_V_START | CIF_ISP_PIC_SIZE_ERROR |
		    CIF_ISP_FRAME_IN | CIF_ISP_AWB_DONE | CIF_ISP_AFM_FIN;
	writel(irq_mask, base + CIF_ISP_IMSC);

	if (out_fmt->fmt_type == FMT_BAYER)
		rkisp1_params_disable_isp(&dev->params_vdev);
	else
		rkisp1_params_configure_isp(&dev->params_vdev, in_fmt,
				     dev->isp_sdev.quantization);

	return 0;
}

static int rkisp1_config_dvp(struct rkisp1_device *dev)
{
	struct ispsd_in_fmt *in_fmt = &dev->isp_sdev.in_fmt;
	void __iomem *base = dev->base_addr;
	u32 val, input_sel, data_width;

	switch (in_fmt->bus_width) {
	case 8:
		input_sel = CIF_ISP_ACQ_PROP_IN_SEL_8B_ZERO;
		data_width = ISP_CIF_DATA_WIDTH_8B;
		break;
	case 10:
		input_sel = CIF_ISP_ACQ_PROP_IN_SEL_10B_ZERO;
		data_width = ISP_CIF_DATA_WIDTH_10B;
		break;
	case 12:
		input_sel = CIF_ISP_ACQ_PROP_IN_SEL_12B;
		data_width = ISP_CIF_DATA_WIDTH_12B;
		break;
	default:
		v4l2_err(&dev->v4l2_dev, "Invalid bus width\n");
		return -EINVAL;
	}

	val = readl(base + CIF_ISP_ACQ_PROP);
	writel(val | input_sel, base + CIF_ISP_ACQ_PROP);

	if (!IS_ERR(dev->grf) &&
		(dev->isp_ver == ISP_V12 ||
		dev->isp_ver == ISP_V13))
		regmap_update_bits(dev->grf,
			GRF_VI_CON0,
			ISP_CIF_DATA_WIDTH_MASK,
			data_width);

	return 0;
}

static int rkisp1_config_mipi(struct rkisp1_device *dev)
{
	u32 mipi_ctrl;
	void __iomem *base = dev->base_addr;
	struct ispsd_in_fmt *in_fmt = &dev->isp_sdev.in_fmt;
	struct rkisp1_sensor_info *sensor = dev->active_sensor;
	struct v4l2_subdev *mipi_sensor;
	struct v4l2_ctrl *ctrl;
	u32 emd_vc, emd_dt;
	int lanes, ret, i;

	/*
	 * sensor->mbus is set in isp or d-phy notifier_bound function
	 */
	switch (sensor->mbus.flags & V4L2_MBUS_CSI2_LANES) {
	case V4L2_MBUS_CSI2_4_LANE:
		lanes = 4;
		break;
	case V4L2_MBUS_CSI2_3_LANE:
		lanes = 3;
		break;
	case V4L2_MBUS_CSI2_2_LANE:
		lanes = 2;
		break;
	case V4L2_MBUS_CSI2_1_LANE:
		lanes = 1;
		break;
	default:
		return -EINVAL;
	}

	emd_vc = 0xFF;
	emd_dt = 0;
	dev->hdr_sensor = NULL;
	get_remote_mipi_sensor(dev, &mipi_sensor);
	if (mipi_sensor) {
		ctrl = v4l2_ctrl_find(mipi_sensor->ctrl_handler,
				      CIFISP_CID_EMB_VC);
		if (ctrl)
			emd_vc = v4l2_ctrl_g_ctrl(ctrl);

		ctrl = v4l2_ctrl_find(mipi_sensor->ctrl_handler,
				      CIFISP_CID_EMB_DT);
		if (ctrl)
			emd_dt = v4l2_ctrl_g_ctrl(ctrl);
		dev->hdr_sensor = mipi_sensor;
	}

	dev->emd_dt = emd_dt;
	dev->emd_vc = emd_vc;
	dev->emd_data_idx = 0;
	if (emd_vc <= CIF_ISP_ADD_DATA_VC_MAX) {
		for (i = 0; i < RKISP1_EMDDATA_FIFO_MAX; i++) {
			ret = kfifo_alloc(&dev->emd_data_fifo[i].mipi_kfifo,
					  CIFISP_ADD_DATA_FIFO_SIZE,
					  GFP_ATOMIC);
			if (ret) {
				v4l2_err(&dev->v4l2_dev,
					 "kfifo_alloc failed with error %d\n",
					 ret);
				return ret;
			}
		}
	}

#if RKISP1_RK3326_USE_OLDMIPI
	if (dev->isp_ver == ISP_V13) {
#else
	if (dev->isp_ver == ISP_V13 ||
		dev->isp_ver == ISP_V12) {
#endif
		/* lanes */
		writel(lanes - 1, base + CIF_ISP_CSI0_CTRL1);

		/* linecnt */
		writel(0x3FFF, base + CIF_ISP_CSI0_CTRL2);

		/* Configure Data Type and Virtual Channel */
		writel(CIF_MIPI_DATA_SEL_DT(in_fmt->mipi_dt) | CIF_MIPI_DATA_SEL_VC(0),
		       base + CIF_ISP_CSI0_DATA_IDS_1);

		/* clear interrupts state */
		readl(base + CIF_ISP_CSI0_ERR1);
		readl(base + CIF_ISP_CSI0_ERR2);
		readl(base + CIF_ISP_CSI0_ERR3);
		/* set interrupts mask */
		writel(0x1FFFFFF0, base + CIF_ISP_CSI0_MASK1);
		writel(0x03FFFFFF, base + CIF_ISP_CSI0_MASK2);
		writel(CIF_ISP_CSI0_IMASK_FRAME_END(0x3F) |
		       CIF_ISP_CSI0_IMASK_RAW0_OUT_V_END |
		       CIF_ISP_CSI0_IMASK_RAW1_OUT_V_END |
		       CIF_ISP_CSI0_IMASK_LINECNT,
		       base + CIF_ISP_CSI0_MASK3);
	} else {
		mipi_ctrl = CIF_MIPI_CTRL_NUM_LANES(lanes - 1) |
			    CIF_MIPI_CTRL_SHUTDOWNLANES(0xf) |
			    CIF_MIPI_CTRL_ERR_SOT_SYNC_HS_SKIP |
			    CIF_MIPI_CTRL_CLOCKLANE_ENA;

#if RKISP1_RK3326_USE_OLDMIPI
		if (dev->isp_ver == ISP_V12) {
			writel(0, base + CIF_ISP_CSI0_CTRL0);
			writel(0, base + CIF_ISP_CSI0_MASK1);
			writel(0, base + CIF_ISP_CSI0_MASK2);
			writel(0, base + CIF_ISP_CSI0_MASK3);
			/* clear interrupts state */
			readl(base + CIF_ISP_CSI0_ERR1);
			readl(base + CIF_ISP_CSI0_ERR2);
			readl(base + CIF_ISP_CSI0_ERR3);
		}
#endif
		writel(mipi_ctrl, base + CIF_MIPI_CTRL);

		/* Configure Data Type and Virtual Channel */
		writel(CIF_MIPI_DATA_SEL_DT(in_fmt->mipi_dt) | CIF_MIPI_DATA_SEL_VC(0),
		       base + CIF_MIPI_IMG_DATA_SEL);

		writel(CIF_MIPI_DATA_SEL_DT(emd_dt) | CIF_MIPI_DATA_SEL_VC(emd_vc),
		       base + CIF_MIPI_ADD_DATA_SEL_1);
		writel(CIF_MIPI_DATA_SEL_DT(emd_dt) | CIF_MIPI_DATA_SEL_VC(emd_vc),
		       base + CIF_MIPI_ADD_DATA_SEL_2);
		writel(CIF_MIPI_DATA_SEL_DT(emd_dt) | CIF_MIPI_DATA_SEL_VC(emd_vc),
		       base + CIF_MIPI_ADD_DATA_SEL_3);
		writel(CIF_MIPI_DATA_SEL_DT(emd_dt) | CIF_MIPI_DATA_SEL_VC(emd_vc),
		       base + CIF_MIPI_ADD_DATA_SEL_4);

		/* Clear MIPI interrupts */
		writel(~0, base + CIF_MIPI_ICR);
		/*
		 * Disable CIF_MIPI_ERR_DPHY interrupt here temporary for
		 * isp bus may be dead when switch isp.
		 */
		writel(CIF_MIPI_FRAME_END | CIF_MIPI_ERR_CSI | CIF_MIPI_ERR_DPHY |
		       CIF_MIPI_SYNC_FIFO_OVFLW(0x0F) | CIF_MIPI_ADD_DATA_OVFLW,
		       base + CIF_MIPI_IMSC);
	}

	v4l2_dbg(1, rkisp1_debug, &dev->v4l2_dev, "\n  MIPI_CTRL 0x%08x\n"
		 "  MIPI_IMG_DATA_SEL 0x%08x\n"
		 "  MIPI_STATUS 0x%08x\n"
		 "  MIPI_IMSC 0x%08x\n",
		 readl(base + CIF_MIPI_CTRL),
		 readl(base + CIF_MIPI_IMG_DATA_SEL),
		 readl(base + CIF_MIPI_STATUS),
		 readl(base + CIF_MIPI_IMSC));

	return 0;
}

/* Configure MUX */
static int rkisp1_config_path(struct rkisp1_device *dev)
{
	int ret = 0;
	struct rkisp1_sensor_info *sensor = dev->active_sensor;
	u32 dpcl = readl(dev->base_addr + CIF_VI_DPCL);

	if (sensor && (sensor->mbus.type == V4L2_MBUS_BT656 ||
		sensor->mbus.type == V4L2_MBUS_PARALLEL)) {
		ret = rkisp1_config_dvp(dev);
		dpcl |= CIF_VI_DPCL_IF_SEL_PARALLEL;
		dev->isp_inp = INP_DVP;
	} else if (sensor && sensor->mbus.type == V4L2_MBUS_CSI2) {
		ret = rkisp1_config_mipi(dev);
		dpcl |= CIF_VI_DPCL_IF_SEL_MIPI;
		dev->isp_inp = INP_CSI;
	} else if (dev->isp_inp == INP_DMARX_ISP) {
		dpcl |= CIF_VI_DPCL_DMA_SW_ISP;
	}

	writel(dpcl, dev->base_addr + CIF_VI_DPCL);

	return ret;
}

/* Hareware configure Entry */
static int rkisp1_config_cif(struct rkisp1_device *dev)
{
	int ret = 0;
	u32 cif_id;

	v4l2_dbg(1, rkisp1_debug, &dev->v4l2_dev,
		 "SP streaming = %d, MP streaming = %d\n",
		 dev->stream[RKISP1_STREAM_SP].streaming,
		 dev->stream[RKISP1_STREAM_MP].streaming);

	cif_id = readl(dev->base_addr + CIF_VI_ID);
	v4l2_dbg(1, rkisp1_debug, &dev->v4l2_dev, "CIF_ID 0x%08x\n", cif_id);

	ret = rkisp1_config_isp(dev);
	if (ret < 0)
		return ret;
	ret = rkisp1_config_path(dev);
	if (ret < 0)
		return ret;
	rkisp1_config_ism(dev);

	return 0;
}

static bool rkisp1_is_need_3a(struct rkisp1_device *dev)
{
	struct rkisp1_isp_subdev *isp_sdev = &dev->isp_sdev;

	return isp_sdev->in_fmt.fmt_type == FMT_BAYER &&
	       isp_sdev->out_fmt.fmt_type == FMT_YUV;
}

static void rkisp1_start_3a_run(struct rkisp1_device *dev)
{
	struct rkisp1_isp_params_vdev *params_vdev = &dev->params_vdev;
	struct video_device *vdev = &params_vdev->vnode.vdev;
	struct v4l2_event ev = {
		.type = CIFISP_V4L2_EVENT_STREAM_START,
	};
	int ret;

	if (!rkisp1_is_need_3a(dev))
		return;

	v4l2_event_queue(vdev, &ev);
	/* rk3326/px30 require first params queued before
	 * rkisp1_params_configure_isp() called
	 */
	ret = wait_event_timeout(dev->sync_onoff,
			params_vdev->streamon && !params_vdev->first_params,
			msecs_to_jiffies(1000));
	if (!ret)
		v4l2_warn(&dev->v4l2_dev,
			  "waiting on params stream on event timeout\n");
	else
		v4l2_dbg(1, rkisp1_debug, &dev->v4l2_dev,
			 "Waiting for 3A on use %d ms\n", 1000 - ret);
}

static void rkisp1_stop_3a_run(struct rkisp1_device *dev)
{
	struct rkisp1_isp_params_vdev *params_vdev = &dev->params_vdev;
	struct video_device *vdev = &params_vdev->vnode.vdev;
	struct v4l2_event ev = {
		.type = CIFISP_V4L2_EVENT_STREAM_STOP,
	};
	int ret;

	if (!rkisp1_is_need_3a(dev))
		return;

	v4l2_event_queue(vdev, &ev);
	ret = wait_event_timeout(dev->sync_onoff, !params_vdev->streamon,
				 msecs_to_jiffies(1000));
	if (!ret)
		v4l2_warn(&dev->v4l2_dev,
			  "waiting on params stream off event timeout\n");
	else
		v4l2_dbg(1, rkisp1_debug, &dev->v4l2_dev,
			 "Waiting for 3A off use %d ms\n", 1000 - ret);
}

/* Mess register operations to stop isp */
static int rkisp1_isp_stop(struct rkisp1_device *dev)
{
	void __iomem *base = dev->base_addr;
	unsigned long old_rate, safe_rate;
	u32 val;
	u32 i;

	v4l2_dbg(1, rkisp1_debug, &dev->v4l2_dev,
		 "SP streaming = %d, MP streaming = %d\n",
		 dev->stream[RKISP1_STREAM_SP].streaming,
		 dev->stream[RKISP1_STREAM_MP].streaming);

	/*
	 * ISP(mi) stop in mi frame end -> Stop ISP(mipi) ->
	 * Stop ISP(isp) ->wait for ISP isp off
	 */
	/* stop and clear MI, MIPI, and ISP interrupts */
#if RKISP1_RK3326_USE_OLDMIPI
	if (dev->isp_ver == ISP_V13) {
#else
	if (dev->isp_ver == ISP_V12 || dev->isp_ver == ISP_V13) {
#endif
		writel(0, base + CIF_ISP_CSI0_MASK1);
		writel(0, base + CIF_ISP_CSI0_MASK2);
		writel(0, base + CIF_ISP_CSI0_MASK3);
		readl(base + CIF_ISP_CSI0_ERR1);
		readl(base + CIF_ISP_CSI0_ERR2);
		readl(base + CIF_ISP_CSI0_ERR3);
	} else {
		writel(0, base + CIF_MIPI_IMSC);
		writel(~0, base + CIF_MIPI_ICR);
	}

	writel(0, base + CIF_ISP_IMSC);
	writel(~0, base + CIF_ISP_ICR);

	writel(0, base + CIF_MI_IMSC);
	writel(~0, base + CIF_MI_ICR);
#if RKISP1_RK3326_USE_OLDMIPI
	if (dev->isp_ver == ISP_V13) {
#else
	if (dev->isp_ver == ISP_V12 || dev->isp_ver == ISP_V13) {
#endif
		writel(0, base + CIF_ISP_CSI0_CTRL0);
	} else {
		val = readl(base + CIF_MIPI_CTRL);
		val = val & (~CIF_MIPI_CTRL_SHUTDOWNLANES(0xf));
		writel(val & (~CIF_MIPI_CTRL_OUTPUT_ENA), base + CIF_MIPI_CTRL);
		udelay(20);
	}
	/* stop ISP */
	val = readl(base + CIF_ISP_CTRL);
	val &= ~(CIF_ISP_CTRL_ISP_INFORM_ENABLE | CIF_ISP_CTRL_ISP_ENABLE);
	writel(val, base + CIF_ISP_CTRL);

	val = readl(base + CIF_ISP_CTRL);
	writel(val | CIF_ISP_CTRL_ISP_CFG_UPD, base + CIF_ISP_CTRL);

	readx_poll_timeout_atomic(readl, base + CIF_ISP_RIS,
				  val, val & CIF_ISP_OFF, 20, 100);
	v4l2_dbg(1, rkisp1_debug, &dev->v4l2_dev,
		"streaming(MP:%d, SP:%d), MI_CTRL:%x, ISP_CTRL:%x, MIPI_CTRL:%x\n",
		 dev->stream[RKISP1_STREAM_SP].streaming,
		 dev->stream[RKISP1_STREAM_MP].streaming,
		 readl(base + CIF_MI_CTRL),
		 readl(base + CIF_ISP_CTRL),
		 readl(base + CIF_MIPI_CTRL));

	if (!in_interrupt()) {
		/* normal case */
		/* check the isp_clk before isp reset operation */
		old_rate = clk_get_rate(dev->clks[0]);
		safe_rate = dev->clk_rate_tbl[0] * 1000000UL;
		if (old_rate > safe_rate) {
			clk_set_rate(dev->clks[0], safe_rate);
			udelay(100);
		}
		writel(CIF_IRCL_CIF_SW_RST, base + CIF_IRCL);
		/* restore the old ispclk after reset */
		if (old_rate != safe_rate)
			clk_set_rate(dev->clks[0], old_rate);
	} else {
		/* abnormal case, in irq function */
		writel(CIF_IRCL_CIF_SW_RST, base + CIF_IRCL);
	}
	if (dev->isp_ver == ISP_V12 || dev->isp_ver == ISP_V13) {
		writel(0, base + CIF_ISP_CSI0_CSI2_RESETN);
		writel(0, base + CIF_ISP_CSI0_CTRL0);
		writel(0, base + CIF_ISP_CSI0_MASK1);
		writel(0, base + CIF_ISP_CSI0_MASK2);
		writel(0, base + CIF_ISP_CSI0_MASK3);
	}

	rkisp1_config_clk(dev, true);
	if (!in_interrupt()) {
		struct iommu_domain *domain;

		domain = iommu_get_domain_for_dev(dev->dev);
		if (domain) {
			domain->ops->detach_dev(domain, dev->dev);
			domain->ops->attach_dev(domain, dev->dev);
		}
	}
	dev->isp_state = ISP_STOP;

	if (dev->emd_vc <= CIF_ISP_ADD_DATA_VC_MAX) {
		for (i = 0; i < RKISP1_EMDDATA_FIFO_MAX; i++)
			kfifo_free(&dev->emd_data_fifo[i].mipi_kfifo);
		dev->emd_vc = 0xFF;
	}

	if (dev->hdr_sensor)
		dev->hdr_sensor = NULL;

	return 0;
}

/* Mess register operations to start isp */
static int rkisp1_isp_start(struct rkisp1_device *dev)
{
	struct rkisp1_sensor_info *sensor = dev->active_sensor;
	void __iomem *base = dev->base_addr;
	u32 val;

	v4l2_dbg(1, rkisp1_debug, &dev->v4l2_dev,
		 "SP streaming = %d, MP streaming = %d\n",
		 dev->stream[RKISP1_STREAM_SP].streaming,
		 dev->stream[RKISP1_STREAM_MP].streaming);

	/* Activate MIPI */
	if (sensor && sensor->mbus.type == V4L2_MBUS_CSI2) {
#if RKISP1_RK3326_USE_OLDMIPI
		if (dev->isp_ver == ISP_V13) {
#else
		if (dev->isp_ver == ISP_V12 || dev->isp_ver == ISP_V13) {
#endif
			/* clear interrupts state */
			readl(base + CIF_ISP_CSI0_ERR1);
			readl(base + CIF_ISP_CSI0_ERR2);
			readl(base + CIF_ISP_CSI0_ERR3);
			/* csi2host enable */
			writel(1, base + CIF_ISP_CSI0_CTRL0);
		} else {
			val = readl(base + CIF_MIPI_CTRL);
			writel(val | CIF_MIPI_CTRL_OUTPUT_ENA,
			       base + CIF_MIPI_CTRL);
		}
	}
	/* Activate ISP */
	val = readl(base + CIF_ISP_CTRL);
	val |= CIF_ISP_CTRL_ISP_CFG_UPD | CIF_ISP_CTRL_ISP_ENABLE |
	       CIF_ISP_CTRL_ISP_INFORM_ENABLE | CIF_ISP_CTRL_ISP_CFG_UPD_PERMANENT;
	writel(val, base + CIF_ISP_CTRL);

	dev->isp_err_cnt = 0;
	dev->isp_state = ISP_START;

	/* XXX: Is the 1000us too long?
	 * CIF spec says to wait for sufficient time after enabling
	 * the MIPI interface and before starting the sensor output.
	 */
	usleep_range(1000, 1200);

	v4l2_dbg(1, rkisp1_debug, &dev->v4l2_dev,
		 "SP streaming = %d, MP streaming = %d MI_CTRL 0x%08x\n"
		 "  ISP_CTRL 0x%08x MIPI_CTRL 0x%08x\n",
		 dev->stream[RKISP1_STREAM_SP].streaming,
		 dev->stream[RKISP1_STREAM_MP].streaming,
		 readl(base + CIF_MI_CTRL),
		 readl(base + CIF_ISP_CTRL),
		 readl(base + CIF_MIPI_CTRL));

	return 0;
}

/***************************** isp sub-devs *******************************/

static const struct ispsd_in_fmt rkisp1_isp_input_formats[] = {
	{
		.mbus_code	= MEDIA_BUS_FMT_SBGGR10_1X10,
		.fmt_type	= FMT_BAYER,
		.mipi_dt	= CIF_CSI2_DT_RAW10,
		.bayer_pat	= RAW_BGGR,
		.bus_width	= 10,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SRGGB10_1X10,
		.fmt_type	= FMT_BAYER,
		.mipi_dt	= CIF_CSI2_DT_RAW10,
		.bayer_pat	= RAW_RGGB,
		.bus_width	= 10,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SGBRG10_1X10,
		.fmt_type	= FMT_BAYER,
		.mipi_dt	= CIF_CSI2_DT_RAW10,
		.bayer_pat	= RAW_GBRG,
		.bus_width	= 10,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SGRBG10_1X10,
		.fmt_type	= FMT_BAYER,
		.mipi_dt	= CIF_CSI2_DT_RAW10,
		.bayer_pat	= RAW_GRBG,
		.bus_width	= 10,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SRGGB12_1X12,
		.fmt_type	= FMT_BAYER,
		.mipi_dt	= CIF_CSI2_DT_RAW12,
		.bayer_pat	= RAW_RGGB,
		.bus_width	= 12,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SBGGR12_1X12,
		.fmt_type	= FMT_BAYER,
		.mipi_dt	= CIF_CSI2_DT_RAW12,
		.bayer_pat	= RAW_BGGR,
		.bus_width	= 12,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SGBRG12_1X12,
		.fmt_type	= FMT_BAYER,
		.mipi_dt	= CIF_CSI2_DT_RAW12,
		.bayer_pat	= RAW_GBRG,
		.bus_width	= 12,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SGRBG12_1X12,
		.fmt_type	= FMT_BAYER,
		.mipi_dt	= CIF_CSI2_DT_RAW12,
		.bayer_pat	= RAW_GRBG,
		.bus_width	= 12,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SRGGB8_1X8,
		.fmt_type	= FMT_BAYER,
		.mipi_dt	= CIF_CSI2_DT_RAW8,
		.bayer_pat	= RAW_RGGB,
		.bus_width	= 8,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SBGGR8_1X8,
		.fmt_type	= FMT_BAYER,
		.mipi_dt	= CIF_CSI2_DT_RAW8,
		.bayer_pat	= RAW_BGGR,
		.bus_width	= 8,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SGBRG8_1X8,
		.fmt_type	= FMT_BAYER,
		.mipi_dt	= CIF_CSI2_DT_RAW8,
		.bayer_pat	= RAW_GBRG,
		.bus_width	= 8,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SGRBG8_1X8,
		.fmt_type	= FMT_BAYER,
		.mipi_dt	= CIF_CSI2_DT_RAW8,
		.bayer_pat	= RAW_GRBG,
		.bus_width	= 8,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_YUYV8_2X8,
		.fmt_type	= FMT_YUV,
		.mipi_dt	= CIF_CSI2_DT_YUV422_8b,
		.yuv_seq	= CIF_ISP_ACQ_PROP_YCBYCR,
		.bus_width	= 8,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_YVYU8_2X8,
		.fmt_type	= FMT_YUV,
		.mipi_dt	= CIF_CSI2_DT_YUV422_8b,
		.yuv_seq	= CIF_ISP_ACQ_PROP_YCRYCB,
		.bus_width	= 8,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_UYVY8_2X8,
		.fmt_type	= FMT_YUV,
		.mipi_dt	= CIF_CSI2_DT_YUV422_8b,
		.yuv_seq	= CIF_ISP_ACQ_PROP_CBYCRY,
		.bus_width	= 8,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_VYUY8_2X8,
		.fmt_type	= FMT_YUV,
		.mipi_dt	= CIF_CSI2_DT_YUV422_8b,
		.yuv_seq	= CIF_ISP_ACQ_PROP_CRYCBY,
		.bus_width	= 8,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_YUYV10_2X10,
		.fmt_type	= FMT_YUV,
		.mipi_dt	= CIF_CSI2_DT_YUV422_8b,
		.yuv_seq	= CIF_ISP_ACQ_PROP_YCBYCR,
		.bus_width	= 10,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_YVYU10_2X10,
		.fmt_type	= FMT_YUV,
		.mipi_dt	= CIF_CSI2_DT_YUV422_8b,
		.yuv_seq	= CIF_ISP_ACQ_PROP_YCRYCB,
		.bus_width	= 10,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_UYVY10_2X10,
		.fmt_type	= FMT_YUV,
		.mipi_dt	= CIF_CSI2_DT_YUV422_8b,
		.yuv_seq	= CIF_ISP_ACQ_PROP_CBYCRY,
		.bus_width	= 10,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_VYUY10_2X10,
		.fmt_type	= FMT_YUV,
		.mipi_dt	= CIF_CSI2_DT_YUV422_8b,
		.yuv_seq	= CIF_ISP_ACQ_PROP_CRYCBY,
		.bus_width	= 10,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_YUYV12_2X12,
		.fmt_type	= FMT_YUV,
		.mipi_dt	= CIF_CSI2_DT_YUV422_8b,
		.yuv_seq	= CIF_ISP_ACQ_PROP_YCBYCR,
		.bus_width	= 12,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_YVYU12_2X12,
		.fmt_type	= FMT_YUV,
		.mipi_dt	= CIF_CSI2_DT_YUV422_8b,
		.yuv_seq	= CIF_ISP_ACQ_PROP_YCRYCB,
		.bus_width	= 12,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_UYVY12_2X12,
		.fmt_type	= FMT_YUV,
		.mipi_dt	= CIF_CSI2_DT_YUV422_8b,
		.yuv_seq	= CIF_ISP_ACQ_PROP_CBYCRY,
		.bus_width	= 12,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_VYUY12_2X12,
		.fmt_type	= FMT_YUV,
		.mipi_dt	= CIF_CSI2_DT_YUV422_8b,
		.yuv_seq	= CIF_ISP_ACQ_PROP_CRYCBY,
		.bus_width	= 12,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_Y8_1X8,
		.fmt_type	= FMT_BAYER,
		.mipi_dt	= CIF_CSI2_DT_RAW8,
		.yuv_seq	= CIF_ISP_ACQ_PROP_YCBYCR,
		.bus_width	= 8,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_Y10_1X10,
		.fmt_type	= FMT_BAYER,
		.mipi_dt	= CIF_CSI2_DT_RAW10,
		.yuv_seq	= CIF_ISP_ACQ_PROP_YCBYCR,
		.bus_width	= 10,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_Y12_1X12,
		.fmt_type	= FMT_BAYER,
		.mipi_dt	= CIF_CSI2_DT_RAW12,
		.yuv_seq	= CIF_ISP_ACQ_PROP_YCBYCR,
		.bus_width	= 12,
	}
};

static const struct ispsd_out_fmt rkisp1_isp_output_formats[] = {
	{
		.mbus_code	= MEDIA_BUS_FMT_YUYV8_2X8,
		.fmt_type	= FMT_YUV,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SRGGB12_1X12,
		.fmt_type	= FMT_BAYER,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SBGGR12_1X12,
		.fmt_type	= FMT_BAYER,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SGBRG12_1X12,
		.fmt_type	= FMT_BAYER,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SGRBG12_1X12,
		.fmt_type	= FMT_BAYER,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SRGGB10_1X10,
		.fmt_type	= FMT_BAYER,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SBGGR10_1X10,
		.fmt_type	= FMT_BAYER,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SGBRG10_1X10,
		.fmt_type	= FMT_BAYER,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SGRBG10_1X10,
		.fmt_type	= FMT_BAYER,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SRGGB8_1X8,
		.fmt_type	= FMT_BAYER,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SBGGR8_1X8,
		.fmt_type	= FMT_BAYER,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SGBRG8_1X8,
		.fmt_type	= FMT_BAYER,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SGRBG8_1X8,
		.fmt_type	= FMT_BAYER,
	},
};

static const struct ispsd_in_fmt *find_in_fmt(u32 mbus_code)
{
	const struct ispsd_in_fmt *fmt;
	int i, array_size = ARRAY_SIZE(rkisp1_isp_input_formats);

	for (i = 0; i < array_size; i++) {
		fmt = &rkisp1_isp_input_formats[i];
		if (fmt->mbus_code == mbus_code)
			return fmt;
	}

	return NULL;
}

static const struct ispsd_out_fmt *find_out_fmt(u32 mbus_code)
{
	const struct ispsd_out_fmt *fmt;
	int i, array_size = ARRAY_SIZE(rkisp1_isp_output_formats);

	for (i = 0; i < array_size; i++) {
		fmt = &rkisp1_isp_output_formats[i];
		if (fmt->mbus_code == mbus_code)
			return fmt;
	}

	return NULL;
}

static int rkisp1_isp_sd_enum_mbus_code(struct v4l2_subdev *sd,
					struct v4l2_subdev_pad_config *cfg,
					struct v4l2_subdev_mbus_code_enum *code)
{
	int i = code->index;

	if (code->pad == RKISP1_ISP_PAD_SINK) {
		if (i >= ARRAY_SIZE(rkisp1_isp_input_formats))
			return -EINVAL;
		code->code = rkisp1_isp_input_formats[i].mbus_code;
	} else {
		if (i >= ARRAY_SIZE(rkisp1_isp_output_formats))
			return -EINVAL;
		code->code = rkisp1_isp_output_formats[i].mbus_code;
	}

	return 0;
}

#define sd_to_isp_sd(_sd) container_of(_sd, struct rkisp1_isp_subdev, sd)
static int rkisp1_isp_sd_get_fmt(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_format *fmt)
{
	struct rkisp1_isp_subdev *isp_sd = sd_to_isp_sd(sd);
	struct v4l2_mbus_framefmt *mf;

	if (!fmt)
		goto err;

	if (fmt->pad != RKISP1_ISP_PAD_SINK &&
	    fmt->pad != RKISP1_ISP_PAD_SOURCE_PATH)
		goto err;

	mf = &fmt->format;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		if (!cfg)
			goto err;
		mf = v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
	}

	if (fmt->pad == RKISP1_ISP_PAD_SINK) {
		*mf = isp_sd->in_frm;
	} else if (fmt->pad == RKISP1_ISP_PAD_SOURCE_PATH) {
		/* format of source pad */
		mf->code = isp_sd->out_fmt.mbus_code;
		/* window size of source pad */
		mf->width = isp_sd->out_crop.width;
		mf->height = isp_sd->out_crop.height;
		mf->quantization = isp_sd->quantization;
	}
	mf->field = V4L2_FIELD_NONE;

	return 0;
err:
	return -EINVAL;
}

static void rkisp1_isp_sd_try_fmt(struct v4l2_subdev *sd,
				  unsigned int pad,
				  struct v4l2_mbus_framefmt *fmt)
{
	struct rkisp1_device *isp_dev = sd_to_isp_dev(sd);
	struct rkisp1_isp_subdev *isp_sd = &isp_dev->isp_sdev;
	const struct ispsd_in_fmt *in_fmt;
	const struct ispsd_out_fmt *out_fmt;

	switch (pad) {
	case RKISP1_ISP_PAD_SINK:
		in_fmt = find_in_fmt(fmt->code);
		if (in_fmt)
			fmt->code = in_fmt->mbus_code;
		else
			fmt->code = MEDIA_BUS_FMT_SRGGB10_1X10;

		if (isp_dev->isp_ver == ISP_V12) {
			fmt->width  = clamp_t(u32, fmt->width,
				      CIF_ISP_INPUT_W_MIN,
				      CIF_ISP_INPUT_W_MAX_V12);
			fmt->height = clamp_t(u32, fmt->height,
				      CIF_ISP_INPUT_H_MIN,
				      CIF_ISP_INPUT_H_MAX_V12);
		} else if (isp_dev->isp_ver == ISP_V13) {
			fmt->width  = clamp_t(u32, fmt->width,
				      CIF_ISP_INPUT_W_MIN,
				      CIF_ISP_INPUT_W_MAX_V13);
			fmt->height = clamp_t(u32, fmt->height,
				      CIF_ISP_INPUT_H_MIN,
				      CIF_ISP_INPUT_H_MAX_V13);
		} else {
			fmt->width  = clamp_t(u32, fmt->width,
				      CIF_ISP_INPUT_W_MIN,
				      CIF_ISP_INPUT_W_MAX);
			fmt->height = clamp_t(u32, fmt->height,
				      CIF_ISP_INPUT_H_MIN,
				      CIF_ISP_INPUT_H_MAX);
		}
		break;
	case RKISP1_ISP_PAD_SOURCE_PATH:
		out_fmt = find_out_fmt(fmt->code);
		if (out_fmt)
			fmt->code = out_fmt->mbus_code;
		else
			fmt->code = MEDIA_BUS_FMT_YUYV8_2X8;
		/* window size is set in s_selection */
		fmt->width  = isp_sd->out_crop.width;
		fmt->height = isp_sd->out_crop.height;
		/* full range by default */
		if (!fmt->quantization)
			fmt->quantization = V4L2_QUANTIZATION_FULL_RANGE;
		break;
	}

	fmt->field = V4L2_FIELD_NONE;
}

static int rkisp1_isp_sd_set_fmt(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_format *fmt)
{
	struct rkisp1_device *isp_dev = sd_to_isp_dev(sd);
	struct rkisp1_isp_subdev *isp_sd = &isp_dev->isp_sdev;
	struct v4l2_mbus_framefmt *mf;

	if (!fmt)
		goto err;

	if (fmt->pad != RKISP1_ISP_PAD_SINK &&
	    fmt->pad != RKISP1_ISP_PAD_SOURCE_PATH)
		goto err;

	mf = &fmt->format;
	rkisp1_isp_sd_try_fmt(sd, fmt->pad, mf);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		if (!cfg)
			goto err;
		mf = v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
	}

	if (fmt->pad == RKISP1_ISP_PAD_SINK) {
		const struct ispsd_in_fmt *in_fmt;

		in_fmt = find_in_fmt(mf->code);
		isp_sd->in_fmt = *in_fmt;
		isp_sd->in_frm = *mf;
	} else if (fmt->pad == RKISP1_ISP_PAD_SOURCE_PATH) {
		const struct ispsd_out_fmt *out_fmt;

		/* Ignore width/height */
		out_fmt = find_out_fmt(mf->code);
		isp_sd->out_fmt = *out_fmt;
		/*
		 * It is quantization for output,
		 * isp use bt601 limit-range in internal
		 */
		isp_sd->quantization = mf->quantization;
	}

	return 0;
err:
	return -EINVAL;
}

static void rkisp1_isp_sd_try_crop(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_selection *sel)
{
	struct rkisp1_isp_subdev *isp_sd = sd_to_isp_sd(sd);
	struct v4l2_mbus_framefmt in_frm = isp_sd->in_frm;
	struct v4l2_rect in_crop = isp_sd->in_crop;
	struct v4l2_rect *input = &sel->r;

	if (sel->which == V4L2_SUBDEV_FORMAT_TRY) {
		in_frm = *v4l2_subdev_get_try_format(sd, cfg, RKISP1_ISP_PAD_SINK);
		in_crop = *v4l2_subdev_get_try_crop(sd, cfg, RKISP1_ISP_PAD_SINK);
	}

	input->left = ALIGN(input->left, 2);
	input->width = ALIGN(input->width, 2);

	if (sel->pad == RKISP1_ISP_PAD_SINK) {
		input->left = clamp_t(u32, input->left, 0, in_frm.width);
		input->top = clamp_t(u32, input->top, 0, in_frm.height);
		input->width = clamp_t(u32, input->width, CIF_ISP_INPUT_W_MIN,
				in_frm.width - input->left);
		input->height = clamp_t(u32, input->height,
				CIF_ISP_INPUT_H_MIN,
				in_frm.height - input->top);
	} else if (sel->pad == RKISP1_ISP_PAD_SOURCE_PATH) {
		input->left = clamp_t(u32, input->left, 0, in_crop.width);
		input->top = clamp_t(u32, input->top, 0, in_crop.height);
		input->width = clamp_t(u32, input->width, CIF_ISP_OUTPUT_W_MIN,
				in_crop.width - input->left);
		input->height = clamp_t(u32, input->height, CIF_ISP_OUTPUT_H_MIN,
				in_crop.height - input->top);
	}
}

static int rkisp1_isp_sd_get_selection(struct v4l2_subdev *sd,
				       struct v4l2_subdev_pad_config *cfg,
				       struct v4l2_subdev_selection *sel)
{
	struct rkisp1_isp_subdev *isp_sd = sd_to_isp_sd(sd);
	struct v4l2_rect *crop;

	if (!sel)
		goto err;

	if (sel->pad != RKISP1_ISP_PAD_SOURCE_PATH &&
	    sel->pad != RKISP1_ISP_PAD_SINK)
		goto err;

	crop = &sel->r;
	if (sel->which == V4L2_SUBDEV_FORMAT_TRY) {
		if (!cfg)
			goto err;
		crop = v4l2_subdev_get_try_crop(sd, cfg, sel->pad);
	}

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP_BOUNDS:
		if (sel->pad == RKISP1_ISP_PAD_SINK) {
			crop->height = isp_sd->in_frm.height;
			crop->width = isp_sd->in_frm.width;
			crop->left = 0;
			crop->top = 0;
		} else {
			*crop = isp_sd->in_crop;
		}
		break;
	case V4L2_SEL_TGT_CROP:
		if (sel->pad == RKISP1_ISP_PAD_SINK)
			*crop = isp_sd->in_crop;
		else
			*crop = isp_sd->out_crop;
		break;
	default:
		goto err;
	}

	return 0;
err:
	return -EINVAL;
}

static int rkisp1_isp_sd_set_selection(struct v4l2_subdev *sd,
				       struct v4l2_subdev_pad_config *cfg,
				       struct v4l2_subdev_selection *sel)
{
	struct rkisp1_isp_subdev *isp_sd = sd_to_isp_sd(sd);
	struct rkisp1_device *dev = sd_to_isp_dev(sd);
	struct v4l2_rect *crop;

	if (!sel)
		goto err;

	if (sel->pad != RKISP1_ISP_PAD_SOURCE_PATH &&
	    sel->pad != RKISP1_ISP_PAD_SINK)
		goto err;
	if (sel->target != V4L2_SEL_TGT_CROP)
		goto err;

	v4l2_dbg(1, rkisp1_debug, &dev->v4l2_dev,
		 "%s: pad: %d sel(%d,%d)/%dx%d\n", __func__, sel->pad,
		 sel->r.left, sel->r.top, sel->r.width, sel->r.height);
	rkisp1_isp_sd_try_crop(sd, cfg, sel);

	crop = &sel->r;
	if (sel->which == V4L2_SUBDEV_FORMAT_TRY) {
		if (!cfg)
			goto err;
		crop = v4l2_subdev_get_try_crop(sd, cfg, sel->pad);
	}

	if (sel->pad == RKISP1_ISP_PAD_SINK)
		isp_sd->in_crop = *crop;
	else
		isp_sd->out_crop = *crop;

	return 0;
err:
	goto err;
}

static void rkisp1_isp_read_add_fifo_data(struct rkisp1_device *dev)
{
	struct v4l2_device *v4l2_dev = &dev->v4l2_dev;
	void __iomem *base = dev->base_addr;
	u32 mipi_status = 0;
	u32 data_len = 0;
	u32 fifo_data = 0;
	u32 i, idx, cur_frame_id;

	cur_frame_id = atomic_read(&dev->isp_sdev.frm_sync_seq) - 1;
	idx = dev->emd_data_idx;
	dev->emd_data_fifo[idx].frame_id = 0;
	kfifo_reset_out(&dev->emd_data_fifo[idx].mipi_kfifo);
	for (i = 0; i < CIFISP_ADD_DATA_FIFO_SIZE / 4; i++) {
		mipi_status = readl(base + CIF_MIPI_STATUS);
		if (!(mipi_status & 0x01))
			break;

		fifo_data = readl(base + CIF_MIPI_ADD_DATA_FIFO);
		kfifo_in(&dev->emd_data_fifo[idx].mipi_kfifo,
			 &fifo_data, sizeof(fifo_data));
		data_len += 4;

		if (kfifo_is_full(&dev->emd_data_fifo[idx].mipi_kfifo))
			v4l2_warn(v4l2_dev, "%s: mipi_kfifo is full!\n",
				  __func__);
	}

	if (data_len) {
		dev->emd_data_fifo[idx].frame_id = cur_frame_id;
		dev->emd_data_fifo[idx].data_len = data_len;
		dev->emd_data_idx = (idx + 1) % RKISP1_EMDDATA_FIFO_MAX;
	}

	v4l2_dbg(1, rkisp1_debug, &dev->v4l2_dev,
		 "emd kfifo size: %d, frame_id %d\n",
		 kfifo_len(&dev->emd_data_fifo[idx].mipi_kfifo),
		 dev->emd_data_fifo[idx].frame_id);
}

static int rkisp1_isp_sd_s_stream(struct v4l2_subdev *sd, int on)
{
	struct rkisp1_device *isp_dev = sd_to_isp_dev(sd);
	int ret = 0;

	if (!on) {
		rkisp1_stop_3a_run(isp_dev);

		return rkisp1_isp_stop(isp_dev);
	}

	rkisp1_start_3a_run(isp_dev);

	atomic_set(&isp_dev->isp_sdev.frm_sync_seq, 0);
	ret = rkisp1_config_cif(isp_dev);
	if (ret < 0)
		return ret;

	return rkisp1_isp_start(isp_dev);
}

static int rkisp1_isp_sd_s_power(struct v4l2_subdev *sd, int on)
{
	struct rkisp1_device *isp_dev = sd_to_isp_dev(sd);
	void __iomem *base = isp_dev->base_addr;
	int ret;

	v4l2_dbg(1, rkisp1_debug, &isp_dev->v4l2_dev, "s_power: %d\n", on);

	if (on) {
		ret = pm_runtime_get_sync(isp_dev->dev);
		if (ret < 0)
			return ret;

		rkisp1_config_clk(isp_dev, on);
		if (isp_dev->isp_ver == ISP_V12 ||
		    isp_dev->isp_ver == ISP_V13) {
			/* disable csi_rx interrupt */
			writel(0, base + CIF_ISP_CSI0_CTRL0);
			writel(0, base + CIF_ISP_CSI0_MASK1);
			writel(0, base + CIF_ISP_CSI0_MASK2);
			writel(0, base + CIF_ISP_CSI0_MASK3);
		}
	} else {
		rkisp1_config_clk(isp_dev, on);
		ret = pm_runtime_put(isp_dev->dev);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int rkisp1_subdev_link_setup(struct media_entity *entity,
				    const struct media_pad *local,
				    const struct media_pad *remote,
				    u32 flags)
{
	struct v4l2_subdev *sd = media_entity_to_v4l2_subdev(entity);
	struct rkisp1_device *dev;

	if (!sd)
		return -ENODEV;
	dev = sd_to_isp_dev(sd);
	if (!dev)
		return -ENODEV;

	if (!strcmp(remote->entity->name, DMA_VDEV_NAME)) {
		if (flags & MEDIA_LNK_FL_ENABLED)
			dev->isp_inp = INP_DMARX_ISP;
		else
			dev->isp_inp = INP_INVAL;
	}

	return 0;
}

static int rkisp1_subdev_link_validate(struct media_link *link)
{
	if (link->source->index == RKISP1_ISP_PAD_SINK_PARAMS)
		return 0;

	return v4l2_subdev_link_validate(link);
}

static int rkisp1_subdev_fmt_link_validate(struct v4l2_subdev *sd,
			     struct media_link *link,
			     struct v4l2_subdev_format *source_fmt,
			     struct v4l2_subdev_format *sink_fmt)
{
	if (source_fmt->format.code != sink_fmt->format.code)
		return -EINVAL;

	/* Crop is available */
	if (source_fmt->format.width < sink_fmt->format.width ||
		source_fmt->format.height < sink_fmt->format.height)
		return -EINVAL;

	return 0;
}

static void
riksp1_isp_queue_event_sof(struct rkisp1_isp_subdev *isp)
{
	struct v4l2_event event = {
		.type = V4L2_EVENT_FRAME_SYNC,
		.u.frame_sync.frame_sequence =
			atomic_inc_return(&isp->frm_sync_seq) - 1,
	};
	v4l2_event_queue(isp->sd.devnode, &event);
}

static int rkisp1_isp_sd_subs_evt(struct v4l2_subdev *sd, struct v4l2_fh *fh,
				  struct v4l2_event_subscription *sub)
{
	if (sub->type != V4L2_EVENT_FRAME_SYNC)
		return -EINVAL;

	/* Line number. For now only zero accepted. */
	if (sub->id != 0)
		return -EINVAL;

	return v4l2_event_subscribe(fh, sub, 0, NULL);
}

static const struct v4l2_subdev_pad_ops rkisp1_isp_sd_pad_ops = {
	.enum_mbus_code = rkisp1_isp_sd_enum_mbus_code,
	.get_selection = rkisp1_isp_sd_get_selection,
	.set_selection = rkisp1_isp_sd_set_selection,
	.get_fmt = rkisp1_isp_sd_get_fmt,
	.set_fmt = rkisp1_isp_sd_set_fmt,
	.link_validate = rkisp1_subdev_fmt_link_validate,
};

static const struct media_entity_operations rkisp1_isp_sd_media_ops = {
	.link_setup = rkisp1_subdev_link_setup,
	.link_validate = rkisp1_subdev_link_validate,
};

static const struct v4l2_subdev_video_ops rkisp1_isp_sd_video_ops = {
	.s_stream = rkisp1_isp_sd_s_stream,
};

static const struct v4l2_subdev_core_ops rkisp1_isp_core_ops = {
	.subscribe_event = rkisp1_isp_sd_subs_evt,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
	.s_power = rkisp1_isp_sd_s_power,
};

static struct v4l2_subdev_ops rkisp1_isp_sd_ops = {
	.core = &rkisp1_isp_core_ops,
	.video = &rkisp1_isp_sd_video_ops,
	.pad = &rkisp1_isp_sd_pad_ops,
};

static void rkisp1_isp_sd_init_default_fmt(struct rkisp1_isp_subdev *isp_sd)
{
	struct v4l2_mbus_framefmt *in_frm = &isp_sd->in_frm;
	struct v4l2_rect *in_crop = &isp_sd->in_crop;
	struct v4l2_rect *out_crop = &isp_sd->out_crop;
	struct ispsd_in_fmt *in_fmt = &isp_sd->in_fmt;
	struct ispsd_out_fmt *out_fmt = &isp_sd->out_fmt;

	*in_fmt = rkisp1_isp_input_formats[0];
	in_frm->width = RKISP1_DEFAULT_WIDTH;
	in_frm->height = RKISP1_DEFAULT_HEIGHT;
	in_frm->code = in_fmt->mbus_code;

	in_crop->width = in_frm->width;
	in_crop->height = in_frm->height;
	in_crop->left = 0;
	in_crop->top = 0;

	/* propagate to source */
	*out_crop = *in_crop;
	*out_fmt = rkisp1_isp_output_formats[0];
}

int rkisp1_register_isp_subdev(struct rkisp1_device *isp_dev,
			       struct v4l2_device *v4l2_dev)
{
	struct rkisp1_isp_subdev *isp_sdev = &isp_dev->isp_sdev;
	struct v4l2_subdev *sd = &isp_sdev->sd;
	int ret;

	v4l2_subdev_init(sd, &rkisp1_isp_sd_ops);
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS;
	sd->entity.ops = &rkisp1_isp_sd_media_ops;
	sd->entity.function = MEDIA_ENT_F_V4L2_SUBDEV_UNKNOWN;
	snprintf(sd->name, sizeof(sd->name), "rkisp1-isp-subdev");

	isp_sdev->pads[RKISP1_ISP_PAD_SINK].flags =
		MEDIA_PAD_FL_SINK | MEDIA_PAD_FL_MUST_CONNECT;
	isp_sdev->pads[RKISP1_ISP_PAD_SINK_PARAMS].flags = MEDIA_PAD_FL_SINK;
	isp_sdev->pads[RKISP1_ISP_PAD_SOURCE_PATH].flags = MEDIA_PAD_FL_SOURCE;
	isp_sdev->pads[RKISP1_ISP_PAD_SOURCE_STATS].flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&sd->entity, RKISP1_ISP_PAD_MAX,
				isp_sdev->pads);
	if (ret < 0)
		return ret;

	sd->owner = THIS_MODULE;
	v4l2_set_subdevdata(sd, isp_dev);

	sd->grp_id = GRP_ID_ISP;
	ret = v4l2_device_register_subdev(v4l2_dev, sd);
	if (ret < 0) {
		v4l2_err(sd, "Failed to register isp subdev\n");
		goto err_cleanup_media_entity;
	}

	rkisp1_isp_sd_init_default_fmt(isp_sdev);
	isp_dev->hdr_sensor = NULL;
	isp_dev->isp_state = ISP_STOP;

	return 0;
err_cleanup_media_entity:
	media_entity_cleanup(&sd->entity);
	return ret;
}

void rkisp1_unregister_isp_subdev(struct rkisp1_device *isp_dev)
{
	struct v4l2_subdev *sd = &isp_dev->isp_sdev.sd;

	v4l2_device_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
}

/****************  Interrupter Handler ****************/

void rkisp1_mipi_isr(unsigned int mis, struct rkisp1_device *dev)
{
	struct v4l2_device *v4l2_dev = &dev->v4l2_dev;
	void __iomem *base = dev->base_addr;
	u32 val;

	writel(~0, base + CIF_MIPI_ICR);

	/*
	 * Disable DPHY errctrl interrupt, because this dphy
	 * erctrl signal is asserted until the next changes
	 * of line state. This time is may be too long and cpu
	 * is hold in this interrupt.
	 */
	if (mis & CIF_MIPI_ERR_DPHY) {
		val = readl(base + CIF_MIPI_IMSC);
		writel(val & ~CIF_MIPI_ERR_DPHY, base + CIF_MIPI_IMSC);
		dev->isp_sdev.dphy_errctrl_disabled = true;
	}

	/*
	 * Enable DPHY errctrl interrupt again, if mipi have receive
	 * the whole frame without any error.
	 */
	if (mis == CIF_MIPI_FRAME_END) {
		/*
		 * Enable DPHY errctrl interrupt again, if mipi have receive
		 * the whole frame without any error.
		 */
		if (dev->isp_sdev.dphy_errctrl_disabled) {
			val = readl(base + CIF_MIPI_IMSC);
			val |= CIF_MIPI_ERR_DPHY;
			writel(val, base + CIF_MIPI_IMSC);
			dev->isp_sdev.dphy_errctrl_disabled = false;
		}
	} else {
		v4l2_warn(v4l2_dev, "MIPI mis error: 0x%08x\n", mis);
		val = readl(base + CIF_MIPI_CTRL);
		writel(val | CIF_MIPI_CTRL_FLUSH_FIFO, base + CIF_MIPI_CTRL);
	}
}

void rkisp1_mipi_v13_isr(unsigned int err1, unsigned int err2,
			 unsigned int err3, struct rkisp1_device *dev)
{
	struct v4l2_device *v4l2_dev = &dev->v4l2_dev;
	void __iomem *base = dev->base_addr;
	u32 val, mask;

	/*
	 * Disable DPHY errctrl interrupt, because this dphy
	 * erctrl signal is asserted until the next changes
	 * of line state. This time is may be too long and cpu
	 * is hold in this interrupt.
	 */
	mask = CIF_ISP_CSI0_IMASK1_PHY_ERRSOTSYNC(0x0F) |
	       CIF_ISP_CSI0_IMASK1_PHY_ERREOTSYNC(0x0F);
	if (mask & err1) {
		val = readl(base + CIF_ISP_CSI0_MASK1);
		writel(val & ~mask, base + CIF_ISP_CSI0_MASK1);
		dev->isp_sdev.dphy_errctrl_disabled = true;
	}

	mask = CIF_ISP_CSI0_IMASK2_PHY_ERRSOTHS(0x0F) |
	       CIF_ISP_CSI0_IMASK2_PHY_ERRCONTROL(0x0F);
	if (mask & err2) {
		val = readl(base + CIF_ISP_CSI0_MASK2);
		writel(val & ~mask, base + CIF_ISP_CSI0_MASK2);
		dev->isp_sdev.dphy_errctrl_disabled = true;
	}

	mask = CIF_ISP_CSI0_IMASK_FRAME_END(0x3F);
	if ((err3 & mask) && !err1 && !err2) {
		/*
		 * Enable DPHY errctrl interrupt again, if mipi have receive
		 * the whole frame without any error.
		 */
		if (dev->isp_sdev.dphy_errctrl_disabled) {
			writel(0x1FFFFFF0, base + CIF_ISP_CSI0_MASK1);
			writel(0x03FFFFFF, base + CIF_ISP_CSI0_MASK2);
			dev->isp_sdev.dphy_errctrl_disabled = false;
		}
	}

	if (err1)
		v4l2_warn(v4l2_dev, "MIPI error: err1: 0x%08x\n", err1);

	if (err2)
		v4l2_warn(v4l2_dev, "MIPI error: err2: 0x%08x\n", err2);
}

void rkisp1_isp_isr(unsigned int isp_mis, struct rkisp1_device *dev)
{
	void __iomem *base = dev->base_addr;
	unsigned int isp_mis_tmp = 0;
	unsigned int isp_err = 0;

	/* start edge of v_sync */
	if (isp_mis & CIF_ISP_V_START) {
		if (dev->stream[RKISP1_STREAM_SP].interlaced) {
			/* 0 = ODD 1 = EVEN */
			if (dev->active_sensor->mbus.type == V4L2_MBUS_CSI2) {
				void __iomem *addr = NULL;

				if (dev->isp_ver == ISP_V10 ||
				    dev->isp_ver == ISP_V10_1)
					addr = base + CIF_MIPI_FRAME;
				else if (dev->isp_ver == ISP_V12 ||
					 dev->isp_ver == ISP_V13)
					addr = base + CIF_ISP_CSI0_FRAME_NUM_RO;

				if (addr)
					dev->stream[RKISP1_STREAM_SP].u.sp.field =
						(readl(addr) >> 16) % 2;
			} else {
				dev->stream[RKISP1_STREAM_SP].u.sp.field =
					(readl(base + CIF_ISP_FLAGS_SHD) >> 2) & BIT(0);
			}
		}

		if (dev->vs_irq < 0)
			riksp1_isp_queue_event_sof(&dev->isp_sdev);

		writel(CIF_ISP_V_START, base + CIF_ISP_ICR);
		isp_mis_tmp = readl(base + CIF_ISP_MIS);
		if (isp_mis_tmp & CIF_ISP_V_START)
			v4l2_err(&dev->v4l2_dev, "isp icr v_statr err: 0x%x\n",
				 isp_mis_tmp);
	}

	if ((isp_mis & (CIF_ISP_DATA_LOSS | CIF_ISP_PIC_SIZE_ERROR))) {
		if ((isp_mis & CIF_ISP_PIC_SIZE_ERROR)) {
			/* Clear pic_size_error */
			writel(CIF_ISP_PIC_SIZE_ERROR, base + CIF_ISP_ICR);
			isp_err = readl(base + CIF_ISP_ERR);
			v4l2_err(&dev->v4l2_dev,
				 "CIF_ISP_PIC_SIZE_ERROR (0x%08x)", isp_err);
			writel(isp_err, base + CIF_ISP_ERR_CLR);
		}

		if ((isp_mis & CIF_ISP_DATA_LOSS)) {
			/* Clear data_loss */
			writel(CIF_ISP_DATA_LOSS, base + CIF_ISP_ICR);
			v4l2_err(&dev->v4l2_dev, "CIF_ISP_DATA_LOSS\n");
			writel(CIF_ISP_DATA_LOSS, base + CIF_ISP_ICR);
		}

		if (dev->isp_err_cnt++ > RKISP1_CONTI_ERR_MAX) {
			rkisp1_isp_stop(dev);
			dev->isp_state = ISP_ERROR;
			v4l2_err(&dev->v4l2_dev,
				 "Too many isp error, stop isp!\n");
		}
	}

	/* sampled input frame is complete */
	if (isp_mis & CIF_ISP_FRAME_IN) {
		writel(CIF_ISP_FRAME_IN, base + CIF_ISP_ICR);
		isp_mis_tmp = readl(base + CIF_ISP_MIS);
		if (isp_mis_tmp & CIF_ISP_FRAME_IN)
			v4l2_err(&dev->v4l2_dev, "isp icr frame_in err: 0x%x\n",
				 isp_mis_tmp);

		dev->isp_err_cnt = 0;
	}

	/* frame was completely put out */
	if (isp_mis & CIF_ISP_FRAME) {
		/* Clear Frame In (ISP) */
		writel(CIF_ISP_FRAME, base + CIF_ISP_ICR);
		isp_mis_tmp = readl(base + CIF_ISP_MIS);
		if (isp_mis_tmp & CIF_ISP_FRAME)
			v4l2_err(&dev->v4l2_dev,
				 "isp icr frame end err: 0x%x\n", isp_mis_tmp);

		rkisp1_isp_read_add_fifo_data(dev);
	}

	if (isp_mis & (CIF_ISP_FRAME | CIF_ISP_AWB_DONE | CIF_ISP_AFM_FIN)) {
		u32 irq = isp_mis;

		/* FRAME to get EXP and HIST together */
		if (isp_mis & CIF_ISP_FRAME)
			irq |= ((CIF_ISP_EXP_END |
				CIF_ISP_HIST_MEASURE_RDY) &
				readl(base + CIF_ISP_RIS));

		rkisp1_stats_isr(&dev->stats_vdev, irq);
	}

	/*
	 * Then update changed configs. Some of them involve
	 * lot of register writes. Do those only one per frame.
	 * Do the updates in the order of the processing flow.
	 */
	rkisp1_params_isr(&dev->params_vdev, isp_mis);
}

irqreturn_t rkisp1_vs_isr_handler(int irq, void *ctx)
{
	struct device *dev = ctx;
	struct rkisp1_device *rkisp1_dev = dev_get_drvdata(dev);

	if (rkisp1_dev->vs_irq >= 0)
		riksp1_isp_queue_event_sof(&rkisp1_dev->isp_sdev);

	return IRQ_HANDLED;
}

