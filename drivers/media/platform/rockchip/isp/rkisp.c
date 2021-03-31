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
#include <linux/rk-camera-module.h>
#include <linux/videodev2.h>
#include <linux/vmalloc.h>
#include <linux/kfifo.h>
#include <linux/interrupt.h>
#include <linux/rk-preisp.h>
#include <linux/rkisp21-config.h>
#include <linux/iommu.h>
#include <media/v4l2-event.h>
#include <media/media-entity.h>

#include "common.h"
#include "regs.h"
#include "rkisp_tb_helper.h"

#define ISP_SUBDEV_NAME DRIVER_NAME "-isp-subdev"
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
 * | Sensor image/ISP in_frm                                 |
 * | +---------------------------------------------------+   |
 * | | ISP_ACQ (for black level)                         |   |
 * | | in_crop                                           |   |
 * | | +--------------------------------------------+    |   |
 * | | |    ISP_IS                                  |    |   |
 * | | |    rkisp_isp_subdev: out_crop              |    |   |
 * | | |                                            |    |   |
 * | | |                                            |    |   |
 * | | |                                            |    |   |
 * | | |                                            |    |   |
 * | | +--------------------------------------------+    |   |
 * | +---------------------------------------------------+   |
 * +---------------------------------------------------------+
 */

static inline struct rkisp_device *sd_to_isp_dev(struct v4l2_subdev *sd)
{
	return container_of(sd->v4l2_dev, struct rkisp_device, v4l2_dev);
}

static int mbus_pixelcode_to_mipi_dt(u32 pixelcode)
{
	int mipi_dt;

	switch (pixelcode) {
	case MEDIA_BUS_FMT_Y8_1X8:
	case MEDIA_BUS_FMT_SRGGB8_1X8:
	case MEDIA_BUS_FMT_SBGGR8_1X8:
	case MEDIA_BUS_FMT_SGBRG8_1X8:
	case MEDIA_BUS_FMT_SGRBG8_1X8:
		mipi_dt = CIF_CSI2_DT_RAW8;
		break;
	case MEDIA_BUS_FMT_Y10_1X10:
	case MEDIA_BUS_FMT_SBGGR10_1X10:
	case MEDIA_BUS_FMT_SRGGB10_1X10:
	case MEDIA_BUS_FMT_SGBRG10_1X10:
	case MEDIA_BUS_FMT_SGRBG10_1X10:
		mipi_dt = CIF_CSI2_DT_RAW10;
		break;
	case MEDIA_BUS_FMT_Y12_1X12:
	case MEDIA_BUS_FMT_SRGGB12_1X12:
	case MEDIA_BUS_FMT_SBGGR12_1X12:
	case MEDIA_BUS_FMT_SGBRG12_1X12:
	case MEDIA_BUS_FMT_SGRBG12_1X12:
		mipi_dt = CIF_CSI2_DT_RAW12;
		break;
	case MEDIA_BUS_FMT_YUYV8_2X8:
	case MEDIA_BUS_FMT_YVYU8_2X8:
	case MEDIA_BUS_FMT_UYVY8_2X8:
	case MEDIA_BUS_FMT_VYUY8_2X8:
		mipi_dt = CIF_CSI2_DT_YUV422_8b;
		break;
	default:
		mipi_dt = -EINVAL;
	}
	return mipi_dt;
}

/* Get sensor by enabled media link */
static struct v4l2_subdev *get_remote_sensor(struct v4l2_subdev *sd)
{
	struct media_pad *local, *remote;
	struct media_entity *sensor_me;
	struct v4l2_subdev *remote_sd = NULL;

	local = &sd->entity.pads[0];
	if (!local)
		goto end;
	remote = rkisp_media_entity_remote_pad(local);
	if (!remote)
		goto end;

	//skip csi subdev
	if (!strcmp(remote->entity->name, CSI_DEV_NAME)) {
		local = &remote->entity->pads[CSI_SINK];
		if (!local)
			goto end;
		remote = media_entity_remote_pad(local);
		if (!remote)
			goto end;
	}

	sensor_me = remote->entity;
	remote_sd = media_entity_to_v4l2_subdev(sensor_me);
end:
	return remote_sd;
}

static struct rkisp_sensor_info *sd_to_sensor(struct rkisp_device *dev,
					       struct v4l2_subdev *sd)
{
	int i;

	for (i = 0; i < dev->num_sensors; ++i)
		if (dev->sensors[i].sd == sd)
			return &dev->sensors[i];

	return NULL;
}

int rkisp_align_sensor_resolution(struct rkisp_device *dev,
				  struct v4l2_rect *crop, bool user)
{
	struct v4l2_subdev *sensor = NULL;
	struct v4l2_subdev_selection sel;
	u32 code = dev->isp_sdev.in_frm.code;
	u32 src_w = dev->isp_sdev.in_frm.width;
	u32 src_h = dev->isp_sdev.in_frm.height;
	u32 dest_w, dest_h, w, h;
	int ret = 0;

	if (!crop)
		return -EINVAL;

	if (dev->isp_ver == ISP_V12) {
		w = clamp_t(u32, src_w,
			    CIF_ISP_INPUT_W_MIN,
			    CIF_ISP_INPUT_W_MAX_V12);
		h = clamp_t(u32, src_h,
			    CIF_ISP_INPUT_H_MIN,
			    CIF_ISP_INPUT_H_MAX_V12);
	} else if (dev->isp_ver == ISP_V13) {
		w = clamp_t(u32, src_w,
			    CIF_ISP_INPUT_W_MIN,
			    CIF_ISP_INPUT_W_MAX_V13);
		h = clamp_t(u32, src_h,
			    CIF_ISP_INPUT_H_MIN,
			    CIF_ISP_INPUT_H_MAX_V13);
	} else {
		w  = clamp_t(u32, src_w,
			     CIF_ISP_INPUT_W_MIN,
			     CIF_ISP_INPUT_W_MAX);
		h = clamp_t(u32, src_h,
			    CIF_ISP_INPUT_H_MIN,
			    CIF_ISP_INPUT_H_MAX);
	}

	if (dev->active_sensor)
		sensor = dev->active_sensor->sd;
	if (sensor) {
		/* crop info from sensor */
		sel.pad = 0;
		sel.which = V4L2_SUBDEV_FORMAT_ACTIVE;
		sel.target = V4L2_SEL_TGT_CROP;
		/* crop by sensor, isp don't input crop */
		ret = v4l2_subdev_call(sensor, pad, get_selection, NULL, &sel);
		if (!ret && !user) {
			crop->left = 0;
			crop->top = 0;
			crop->width = clamp_t(u32, sel.r.width,
				CIF_ISP_INPUT_W_MIN, w);
			crop->height = clamp_t(u32, sel.r.height,
				CIF_ISP_INPUT_H_MIN, h);
			return 0;
		}

		if (ret) {
			sel.target = V4L2_SEL_TGT_CROP_BOUNDS;
			/* only crop bounds, want to isp to do input crop */
			ret = v4l2_subdev_call(sensor, pad, get_selection, NULL, &sel);
			if (!ret) {
				crop->left = ALIGN(sel.r.left, 2);
				crop->width = ALIGN(sel.r.width, 2);

				crop->left = clamp_t(u32, crop->left, 0, w);
				crop->top = clamp_t(u32, sel.r.top, 0, h);
				crop->width = clamp_t(u32, crop->width,
					CIF_ISP_INPUT_W_MIN, w - crop->left);
				crop->height = clamp_t(u32, sel.r.height,
					CIF_ISP_INPUT_H_MIN, h - crop->top);
				return 0;
			}
		}
	}

	/* crop from user */
	if (user) {
		crop->left = clamp_t(u32, crop->left, 0, w);
		crop->top = clamp_t(u32, crop->top, 0, h);
		crop->width = clamp_t(u32, crop->width,
				CIF_ISP_INPUT_W_MIN, w - crop->left);
		crop->height = clamp_t(u32, crop->height,
				CIF_ISP_INPUT_H_MIN, h - crop->top);
		if ((code & RKISP_MEDIA_BUS_FMT_MASK) == RKISP_MEDIA_BUS_FMT_BAYER &&
		    (ALIGN_DOWN(crop->width, 16) != crop->width ||
		     ALIGN_DOWN(crop->height, 8) != crop->height))
			v4l2_warn(&dev->v4l2_dev,
				  "Note: bayer raw need width 16 align, height 8 align!\n"
				  "suggest (%d,%d)/%dx%d, specical requirements, Ignore!\n",
				  ALIGN_DOWN(crop->left, 4), crop->top,
				  ALIGN_DOWN(crop->width, 16), ALIGN_DOWN(crop->height, 8));
		return 0;
	}

	/* yuv format */
	if ((code & RKISP_MEDIA_BUS_FMT_MASK) != RKISP_MEDIA_BUS_FMT_BAYER) {
		crop->left = 0;
		crop->top = 0;
		crop->width = min_t(u32, src_w, CIF_ISP_INPUT_W_MAX);
		crop->height = min_t(u32, src_h, CIF_ISP_INPUT_H_MAX);
		return 0;
	}

	/* bayer raw processed by isp need:
	 * width 16 align
	 * height 8 align
	 * width and height no exceeding the max limit
	 */
	dest_w = ALIGN_DOWN(w, 16);
	dest_h = ALIGN_DOWN(h, 8);

	/* try to center of crop
	 *4 align to no change bayer raw format
	 */
	crop->left = ALIGN_DOWN((src_w - dest_w) >> 1, 4);
	crop->top = (src_h - dest_h) >> 1;
	crop->width = dest_w;
	crop->height = dest_h;
	return 0;
}

struct media_pad *rkisp_media_entity_remote_pad(struct media_pad *pad)
{
	struct media_link *link;

	list_for_each_entry(link, &pad->entity->links, list) {
		if (!(link->flags & MEDIA_LNK_FL_ENABLED) ||
		    !strcmp(link->source->entity->name,
			    DMARX0_VDEV_NAME) ||
		    !strcmp(link->source->entity->name,
			    DMARX1_VDEV_NAME) ||
		    !strcmp(link->source->entity->name,
			    DMARX2_VDEV_NAME))
			continue;
		if (link->source == pad)
			return link->sink;
		if (link->sink == pad)
			return link->source;
	}

	return NULL;
}

int rkisp_update_sensor_info(struct rkisp_device *dev)
{
	struct v4l2_subdev *sd = &dev->isp_sdev.sd;
	struct rkisp_sensor_info *sensor;
	struct v4l2_subdev *sensor_sd;
	struct v4l2_subdev_format *fmt;
	int i, ret = 0;

	sensor_sd = get_remote_sensor(sd);
	if (!sensor_sd)
		return -ENODEV;

	sensor = sd_to_sensor(dev, sensor_sd);
	ret = v4l2_subdev_call(sensor->sd, video, g_mbus_config,
			       &sensor->mbus);
	if (ret && ret != -ENOIOCTLCMD)
		return ret;

	if (sensor->mbus.type == V4L2_MBUS_CSI2) {
		u8 vc = 0;

		memset(dev->csi_dev.mipi_di, 0,
		       sizeof(dev->csi_dev.mipi_di));
		memset(sensor->fmt, 0, sizeof(sensor->fmt));
		for (i = 0; i < dev->csi_dev.max_pad - 1; i++) {
			fmt = &sensor->fmt[i];
			fmt->pad = i;
			fmt->which = V4L2_SUBDEV_FORMAT_ACTIVE;
			ret = v4l2_subdev_call(sensor->sd, pad, get_fmt,
					       &sensor->cfg, fmt);
			if (ret && ret != -ENOIOCTLCMD)
				return ret;
			ret = mbus_pixelcode_to_mipi_dt(fmt->format.code);
			if (ret < 0) {
				v4l2_err(&dev->v4l2_dev,
					 "Invalid mipi data type\n");
				return ret;
			}
			/* v4l2_subdev_format reserved[0]
			 * using as mipi virtual channel
			 */
			switch (fmt->reserved[0]) {
			case V4L2_MBUS_CSI2_CHANNEL_3:
				vc = 3;
				break;
			case V4L2_MBUS_CSI2_CHANNEL_2:
				vc = 2;
				break;
			case V4L2_MBUS_CSI2_CHANNEL_1:
				vc = 1;
				break;
			case V4L2_MBUS_CSI2_CHANNEL_0:
			default:
				vc = 0;
			}
			dev->csi_dev.mipi_di[i] = CIF_MIPI_DATA_SEL_DT(ret) |
				CIF_MIPI_DATA_SEL_VC(vc);
			v4l2_dbg(1, rkisp_debug, &dev->v4l2_dev,
				  "CSI ch%d vc:%d dt:0x%x %dx%d\n",
				  i, vc, ret,
				  fmt->format.width,
				  fmt->format.height);
		}
	} else {
		sensor->fmt[0].pad = 0;
		sensor->fmt[0].which = V4L2_SUBDEV_FORMAT_ACTIVE;
		ret = v4l2_subdev_call(sensor->sd, pad, get_fmt,
				       &sensor->cfg, &sensor->fmt[0]);
		if (ret && ret != -ENOIOCTLCMD)
			return ret;
	}

	v4l2_subdev_call(sensor->sd, video, g_frame_interval, &sensor->fi);
	dev->active_sensor = sensor;

	return ret;
}

u32 rkisp_mbus_pixelcode_to_v4l2(u32 pixelcode)
{
	u32 pixelformat;

	switch (pixelcode) {
	case MEDIA_BUS_FMT_Y8_1X8:
		pixelformat = V4L2_PIX_FMT_GREY;
		break;
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
	case MEDIA_BUS_FMT_Y10_1X10:
		pixelformat = V4L2_PIX_FMT_Y10;
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
	case MEDIA_BUS_FMT_Y12_1X12:
		pixelformat = V4L2_PIX_FMT_Y12;
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

void rkisp_check_idle(struct rkisp_device *dev, u32 irq)
{
	u32 val = 0;

	dev->irq_ends |= (irq & dev->irq_ends_mask);
	v4l2_dbg(3, rkisp_debug, &dev->v4l2_dev,
		 "%s irq:0x%x ends:0x%x mask:0x%x\n",
		 __func__, irq, dev->irq_ends, dev->irq_ends_mask);
	if (dev->irq_ends != dev->irq_ends_mask || !IS_HDR_RDBK(dev->csi_dev.rd_mode))
		return;

	if (!(dev->irq_ends_mask & (ISP_FRAME_MP | ISP_FRAME_SP | ISP_FRAME_MPFBC)))
		dev->isp_state = ISP_STOP;

	dev->irq_ends = 0;
	switch (dev->csi_dev.rd_mode) {
	case HDR_RDBK_FRAME3://for rd1 rd0 rd2
		val |= RAW1_RD_FRAME;
		/* FALLTHROUGH */
	case HDR_RDBK_FRAME2://for rd0 rd2
		val |= RAW0_RD_FRAME;
		/* FALLTHROUGH */
	default:// for rd2
		val |= RAW2_RD_FRAME;
		/* FALLTHROUGH */
	}
	rkisp2_rawrd_isr(val, dev);
	if (!(dev->irq_ends_mask & (ISP_FRAME_MP | ISP_FRAME_SP | ISP_FRAME_MPFBC)))
		dev->isp_state = ISP_STOP;
	if (dev->dmarx_dev.trigger == T_MANUAL)
		rkisp_csi_trigger_event(dev, T_CMD_END, NULL);
	if (dev->isp_state == ISP_STOP)
		wake_up(&dev->sync_onoff);
}

static void rkisp_set_state(struct rkisp_device *dev, u32 state)
{
	u32 mask = 0xff;

	if (state < ISP_STOP)
		mask = 0xff00;
	dev->isp_state &= mask;
	dev->isp_state |= state;
}


/*
 * Image Stabilization.
 * This should only be called when configuring CIF
 * or at the frame end interrupt
 */
static void rkisp_config_ism(struct rkisp_device *dev)
{
	void __iomem *base = dev->base_addr;
	struct v4l2_rect *out_crop = &dev->isp_sdev.out_crop;
	u32 val;

	/* isp2.0 no ism */
	if (dev->isp_ver == ISP_V20 || dev->isp_ver == ISP_V21)
		return;

	writel(0, base + CIF_ISP_IS_RECENTER);
	writel(0, base + CIF_ISP_IS_MAX_DX);
	writel(0, base + CIF_ISP_IS_MAX_DY);
	writel(0, base + CIF_ISP_IS_DISPLACE);
	writel(out_crop->left, base + CIF_ISP_IS_H_OFFS);
	writel(out_crop->top, base + CIF_ISP_IS_V_OFFS);
	writel(out_crop->width, base + CIF_ISP_IS_H_SIZE);
	if (dev->cap_dev.stream[RKISP_STREAM_SP].interlaced)
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
static int rkisp_config_isp(struct rkisp_device *dev)
{
	struct ispsd_in_fmt *in_fmt;
	struct ispsd_out_fmt *out_fmt;
	struct v4l2_rect *in_crop;
	struct rkisp_sensor_info *sensor;
	u32 isp_ctrl = 0;
	u32 irq_mask = 0;
	u32 signal = 0;
	u32 acq_mult = 0;
	u32 acq_prop = 0;
	u32 extend_line = 0;

	sensor = dev->active_sensor;
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
			    in_fmt->mbus_code == MEDIA_BUS_FMT_Y12_1X12) {
				if (dev->isp_ver == ISP_V20 || dev->isp_ver == ISP_V21)
					rkisp_write(dev, ISP_DEBAYER_CONTROL, 0, false);
				else
					rkisp_write(dev, CIF_ISP_DEMOSAIC,
						CIF_ISP_DEMOSAIC_BYPASS |
						CIF_ISP_DEMOSAIC_TH(0xc), false);
			} else {
				if (dev->isp_ver == ISP_V20 || dev->isp_ver == ISP_V21)
					rkisp_write(dev, ISP_DEBAYER_CONTROL,
						SW_DEBAYER_EN |
						SW_DEBAYER_FILTER_G_EN |
						SW_DEBAYER_FILTER_C_EN, false);
				else
					rkisp_write(dev, CIF_ISP_DEMOSAIC,
						CIF_ISP_DEMOSAIC_TH(0xc), false);
			}

			if (sensor && sensor->mbus.type == V4L2_MBUS_BT656)
				isp_ctrl = CIF_ISP_CTRL_ISP_MODE_BAYER_ITU656;
			else
				isp_ctrl = CIF_ISP_CTRL_ISP_MODE_BAYER_ITU601;

			if (dev->isp_ver == ISP_V20 &&
			    dev->csi_dev.rd_mode == HDR_RDBK_FRAME1)
				extend_line = RKMODULE_EXTEND_LINE;
		}

		if (dev->isp_inp == INP_DMARX_ISP)
			acq_prop = CIF_ISP_ACQ_PROP_DMA_RGB;
	} else if (in_fmt->fmt_type == FMT_YUV) {
		acq_mult = 2;
		if (sensor &&
		    (sensor->mbus.type == V4L2_MBUS_CSI2 ||
		     sensor->mbus.type == V4L2_MBUS_CCP2)) {
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

	rkisp_write(dev, CIF_ISP_CTRL, isp_ctrl, false);
	acq_prop |= signal | in_fmt->yuv_seq |
		CIF_ISP_ACQ_PROP_BAYER_PAT(in_fmt->bayer_pat) |
		CIF_ISP_ACQ_PROP_FIELD_SEL_ALL;
	rkisp_write(dev, CIF_ISP_ACQ_PROP, acq_prop, false);
	rkisp_write(dev, CIF_ISP_ACQ_NR_FRAMES, 0, true);

	/* Acquisition Size */
	rkisp_write(dev, CIF_ISP_ACQ_H_OFFS, acq_mult * in_crop->left, false);
	rkisp_write(dev, CIF_ISP_ACQ_V_OFFS, in_crop->top, false);
	rkisp_write(dev, CIF_ISP_ACQ_H_SIZE, acq_mult * in_crop->width, false);

	/* ISP Out Area differ with ACQ is only FIFO, so don't crop in this */
	rkisp_write(dev, CIF_ISP_OUT_H_OFFS, 0, true);
	rkisp_write(dev, CIF_ISP_OUT_V_OFFS, 0, true);
	rkisp_write(dev, CIF_ISP_OUT_H_SIZE, in_crop->width, false);

	if (dev->cap_dev.stream[RKISP_STREAM_SP].interlaced) {
		rkisp_write(dev, CIF_ISP_ACQ_V_SIZE, in_crop->height / 2, false);
		rkisp_write(dev, CIF_ISP_OUT_V_SIZE, in_crop->height / 2, false);
	} else {
		rkisp_write(dev, CIF_ISP_ACQ_V_SIZE, in_crop->height + extend_line, false);
		rkisp_write(dev, CIF_ISP_OUT_V_SIZE, in_crop->height + extend_line, false);
	}

	/* interrupt mask */
	irq_mask |= CIF_ISP_FRAME | CIF_ISP_V_START | CIF_ISP_PIC_SIZE_ERROR |
		    CIF_ISP_FRAME_IN;
	if (dev->isp_ver == ISP_V20 || dev->isp_ver == ISP_V21)
		irq_mask |= ISP2X_LSC_LUT_ERR;
	if (dev->isp_ver == ISP_V20)
		irq_mask |= ISP2X_HDR_DONE;
	rkisp_write(dev, CIF_ISP_IMSC, irq_mask, true);

	if ((dev->isp_ver == ISP_V20 || dev->isp_ver == ISP_V21) &&
	    IS_HDR_RDBK(dev->hdr.op_mode)) {
		irq_mask = ISP2X_3A_RAWAE_BIG;
		rkisp_write(dev, ISP_ISP3A_IMSC, irq_mask, true);
	}

	if (out_fmt->fmt_type == FMT_BAYER)
		rkisp_params_disable_isp(&dev->params_vdev);
	else
		rkisp_params_first_cfg(&dev->params_vdev, in_fmt,
				       dev->isp_sdev.quantization);

	if (!dev->hw_dev->is_single && atomic_read(&dev->hw_dev->refcnt) <= 1) {
		rkisp_update_regs(dev, CIF_ISP_ACQ_H_OFFS, CIF_ISP_ACQ_V_SIZE);
		rkisp_update_regs(dev, CIF_ISP_OUT_H_SIZE, CIF_ISP_OUT_V_SIZE);
	}
	return 0;
}

static int rkisp_config_dvp(struct rkisp_device *dev)
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

	if (!IS_ERR(dev->hw_dev->grf) &&
	    (dev->isp_ver == ISP_V12 || dev->isp_ver == ISP_V13))
		regmap_update_bits(dev->hw_dev->grf, GRF_VI_CON0,
			ISP_CIF_DATA_WIDTH_MASK, data_width);
	return 0;
}

static int rkisp_config_lvds(struct rkisp_device *dev)
{
	struct rkisp_sensor_info *sensor = dev->active_sensor;
	struct ispsd_in_fmt *in_fmt = &dev->isp_sdev.in_fmt;
	struct rkmodule_lvds_cfg cfg;
	struct v4l2_subdev *sd = NULL;
	u32 ret = 0, val, lane, data;

	sd = get_remote_sensor(sensor->sd);
	ret = v4l2_subdev_call(sd, core, ioctl, RKMODULE_GET_LVDS_CFG, &cfg);
	if (ret)
		goto err;

	switch (sensor->mbus.flags & V4L2_MBUS_CSI2_LANES) {
	case V4L2_MBUS_CSI2_1_LANE:
		lane = 1;
		break;
	case V4L2_MBUS_CSI2_2_LANE:
		lane = 2;
		break;
	case V4L2_MBUS_CSI2_3_LANE:
		lane = 3;
		break;
	case V4L2_MBUS_CSI2_4_LANE:
	default:
		lane = 4;
	}
	lane = BIT(lane) - 1;

	switch (in_fmt->bus_width) {
	case 8:
		data = 0;
		break;
	case 10:
		data = 1;
		break;
	case 12:
		data = 2;
		break;
	default:
		ret = -EINVAL;
		goto err;
	}

	val = SW_LVDS_SAV(cfg.frm_sync_code[LVDS_CODE_GRP_LINEAR].odd_sync_code.act.sav) |
	      SW_LVDS_EAV(cfg.frm_sync_code[LVDS_CODE_GRP_LINEAR].odd_sync_code.act.eav);
	writel(val, dev->base_addr + LVDS_SAV_EAV_ACT);
	val = SW_LVDS_SAV(cfg.frm_sync_code[LVDS_CODE_GRP_LINEAR].odd_sync_code.blk.sav) |
	      SW_LVDS_EAV(cfg.frm_sync_code[LVDS_CODE_GRP_LINEAR].odd_sync_code.blk.eav);
	writel(val, dev->base_addr + LVDS_SAV_EAV_BLK);
	val = SW_LVDS_EN | SW_LVDS_WIDTH(data) | SW_LVDS_LANE_EN(lane) | cfg.mode;
	writel(val, dev->base_addr + LVDS_CTRL);
	v4l2_dbg(1, rkisp_debug, &dev->v4l2_dev,
		 "lvds CTRL:0x%x ACT:0x%x BLK:0x%x\n",
		 readl(dev->base_addr + LVDS_CTRL),
		 readl(dev->base_addr + LVDS_SAV_EAV_ACT),
		 readl(dev->base_addr + LVDS_SAV_EAV_BLK));
	return ret;
err:
	v4l2_err(&dev->v4l2_dev, "%s error ret:%d\n", __func__, ret);
	return ret;
}

/* Configure MUX */
static int rkisp_config_path(struct rkisp_device *dev)
{
	int ret = 0;
	struct rkisp_sensor_info *sensor = dev->active_sensor;
	u32 dpcl = readl(dev->base_addr + CIF_VI_DPCL);

	/* isp input interface selects */
	if ((sensor && sensor->mbus.type == V4L2_MBUS_CSI2) ||
	    dev->isp_inp & (INP_RAWRD0 | INP_RAWRD1 | INP_RAWRD2 | INP_CIF)) {
		/* mipi sensor->isp or isp read from ddr */
		dpcl |= CIF_VI_DPCL_IF_SEL_MIPI;
	} else if (sensor &&
		   (sensor->mbus.type == V4L2_MBUS_BT656 ||
		    sensor->mbus.type == V4L2_MBUS_PARALLEL)) {
		/* dvp sensor->isp */
		ret = rkisp_config_dvp(dev);
		dpcl |= CIF_VI_DPCL_IF_SEL_PARALLEL;
	} else if (dev->isp_inp == INP_DMARX_ISP) {
		/* read from ddr, no sensor connect, debug only */
		dpcl |= CIF_VI_DPCL_DMA_SW_ISP;
	} else if (sensor && sensor->mbus.type == V4L2_MBUS_CCP2) {
		/* lvds sensor->isp */
		ret = rkisp_config_lvds(dev);
		dpcl |= VI_DPCL_IF_SEL_LVDS;
	} else {
		v4l2_err(&dev->v4l2_dev, "Invalid input\n");
		ret = -EINVAL;
	}

	writel(dpcl, dev->base_addr + CIF_VI_DPCL);

	return ret;
}

/* Hareware configure Entry */
static int rkisp_config_cif(struct rkisp_device *dev)
{
	int ret = 0;

	v4l2_dbg(1, rkisp_debug, &dev->v4l2_dev,
		 "%s CIF_ID:0x%x SP:%d, MP:%d\n", __func__,
		 readl(dev->base_addr + CIF_VI_ID),
		 dev->cap_dev.stream[RKISP_STREAM_SP].streaming,
		 dev->cap_dev.stream[RKISP_STREAM_MP].streaming);

	ret = rkisp_config_isp(dev);
	if (ret < 0)
		return ret;
	ret = rkisp_config_path(dev);
	if (ret < 0)
		return ret;
	rkisp_config_ism(dev);

	return 0;
}

static bool rkisp_is_need_3a(struct rkisp_device *dev)
{
	struct rkisp_isp_subdev *isp_sdev = &dev->isp_sdev;

	return isp_sdev->in_fmt.fmt_type == FMT_BAYER &&
	       isp_sdev->out_fmt.fmt_type == FMT_YUV;
}

static void rkisp_start_3a_run(struct rkisp_device *dev)
{
	struct rkisp_isp_params_vdev *params_vdev = &dev->params_vdev;
	struct video_device *vdev = &params_vdev->vnode.vdev;
	struct v4l2_event ev = {
		.type = CIFISP_V4L2_EVENT_STREAM_START,
	};
	int ret;

	if (!rkisp_is_need_3a(dev) || dev->isp_ver == ISP_V20)
		return;

	v4l2_event_queue(vdev, &ev);
	/* rk3326/px30 require first params queued before
	 * rkisp_params_configure_isp() called
	 */
	ret = wait_event_timeout(dev->sync_onoff,
			params_vdev->streamon && !params_vdev->first_params,
			msecs_to_jiffies(1000));
	if (!ret)
		v4l2_warn(&dev->v4l2_dev,
			  "waiting on params stream on event timeout\n");
	else
		v4l2_dbg(1, rkisp_debug, &dev->v4l2_dev,
			 "Waiting for 3A on use %d ms\n", 1000 - ret);
}

static void rkisp_stop_3a_run(struct rkisp_device *dev)
{
	struct rkisp_isp_params_vdev *params_vdev = &dev->params_vdev;
	struct video_device *vdev = &params_vdev->vnode.vdev;
	struct v4l2_event ev = {
		.type = CIFISP_V4L2_EVENT_STREAM_STOP,
	};
	int ret;

	if (!rkisp_is_need_3a(dev) || dev->isp_ver == ISP_V20)
		return;

	v4l2_event_queue(vdev, &ev);
	ret = wait_event_timeout(dev->sync_onoff, !params_vdev->streamon,
				 msecs_to_jiffies(1000));
	if (!ret)
		v4l2_warn(&dev->v4l2_dev,
			  "waiting on params stream off event timeout\n");
	else
		v4l2_dbg(1, rkisp_debug, &dev->v4l2_dev,
			 "Waiting for 3A off use %d ms\n", 1000 - ret);
}

/* Mess register operations to stop isp */
static int rkisp_isp_stop(struct rkisp_device *dev)
{
	void __iomem *base = dev->base_addr;
	unsigned long old_rate, safe_rate;
	u32 val;
	u32 i;

	v4l2_dbg(1, rkisp_debug, &dev->v4l2_dev,
		 "%s refcnt:%d\n", __func__,
		 atomic_read(&dev->hw_dev->refcnt));

	if (atomic_read(&dev->hw_dev->refcnt) > 1)
		goto end;
	/*
	 * ISP(mi) stop in mi frame end -> Stop ISP(mipi) ->
	 * Stop ISP(isp) ->wait for ISP isp off
	 */
	/* stop and clear MI, MIPI, and ISP interrupts */
	if (dev->isp_ver == ISP_V12 || dev->isp_ver == ISP_V13) {
		writel(0, base + CIF_ISP_CSI0_MASK1);
		writel(0, base + CIF_ISP_CSI0_MASK2);
		writel(0, base + CIF_ISP_CSI0_MASK3);
		readl(base + CIF_ISP_CSI0_ERR1);
		readl(base + CIF_ISP_CSI0_ERR2);
		readl(base + CIF_ISP_CSI0_ERR3);
	} else if (dev->isp_ver == ISP_V20 || dev->isp_ver == ISP_V21) {
		writel(0, base + CSI2RX_MASK_PHY);
		writel(0, base + CSI2RX_MASK_PACKET);
		writel(0, base + CSI2RX_MASK_OVERFLOW);
		writel(0, base + CSI2RX_MASK_STAT);
		readl(base + CSI2RX_ERR_PHY);
		readl(base + CSI2RX_ERR_PACKET);
		readl(base + CSI2RX_ERR_OVERFLOW);
		readl(base + CSI2RX_ERR_STAT);
	} else {
		writel(0, base + CIF_MIPI_IMSC);
		writel(~0, base + CIF_MIPI_ICR);
	}

	writel(0, base + CIF_ISP_IMSC);
	writel(~0, base + CIF_ISP_ICR);

	if (dev->isp_ver == ISP_V20 || dev->isp_ver == ISP_V21) {
		writel(0, base + ISP_ISP3A_IMSC);
		writel(~0, base + ISP_ISP3A_ICR);
	}

	writel(0, base + CIF_MI_IMSC);
	writel(~0, base + CIF_MI_ICR);
	if (dev->isp_ver == ISP_V12 || dev->isp_ver == ISP_V13) {
		writel(0, base + CIF_ISP_CSI0_CTRL0);
	} else if (dev->isp_ver < ISP_V12) {
		val = readl(base + CIF_MIPI_CTRL);
		val = val & (~CIF_MIPI_CTRL_SHUTDOWNLANES(0xf));
		writel(val & (~CIF_MIPI_CTRL_OUTPUT_ENA), base + CIF_MIPI_CTRL);
		udelay(20);
	}
	/* stop lsc to avoid lsclut error */
	if (dev->isp_ver == ISP_V20 || dev->isp_ver == ISP_V21)
		writel(0, base + ISP_LSC_CTRL);
	/* stop ISP */
	val = readl(base + CIF_ISP_CTRL);
	val &= ~(CIF_ISP_CTRL_ISP_INFORM_ENABLE | CIF_ISP_CTRL_ISP_ENABLE);
	writel(val, base + CIF_ISP_CTRL);

	val = readl(base + CIF_ISP_CTRL);
	writel(val | CIF_ISP_CTRL_ISP_CFG_UPD, base + CIF_ISP_CTRL);

	readx_poll_timeout_atomic(readl, base + CIF_ISP_RIS,
				  val, val & CIF_ISP_OFF, 20, 100);
	v4l2_dbg(1, rkisp_debug, &dev->v4l2_dev,
		 "MI_CTRL:%x, ISP_CTRL:%x\n",
		 readl(base + CIF_MI_CTRL), readl(base + CIF_ISP_CTRL));

	val = rkisp_read(dev, CTRL_VI_ISP_CLK_CTRL, true);
	if (!in_interrupt()) {
		/* normal case */
		/* check the isp_clk before isp reset operation */
		old_rate = clk_get_rate(dev->hw_dev->clks[0]);
		safe_rate = dev->hw_dev->clk_rate_tbl[0].clk_rate * 1000000UL;
		if (old_rate > safe_rate) {
			clk_set_rate(dev->hw_dev->clks[0], safe_rate);
			udelay(100);
		}
		rkisp_soft_reset(dev->hw_dev);
	}
	rkisp_write(dev, CTRL_VI_ISP_CLK_CTRL, val, true);

	if (dev->isp_ver == ISP_V12 || dev->isp_ver == ISP_V13) {
		writel(0, base + CIF_ISP_CSI0_CSI2_RESETN);
		writel(0, base + CIF_ISP_CSI0_CTRL0);
		writel(0, base + CIF_ISP_CSI0_MASK1);
		writel(0, base + CIF_ISP_CSI0_MASK2);
		writel(0, base + CIF_ISP_CSI0_MASK3);
	} else if (dev->isp_ver == ISP_V20 || dev->isp_ver == ISP_V21) {
		writel(0, base + CSI2RX_CSI2_RESETN);
	}

	dev->hw_dev->is_idle = true;
	dev->hw_dev->is_mi_update = false;
end:
	dev->irq_ends_mask = 0;
	dev->hdr.op_mode = 0;
	rkisp_set_state(dev, ISP_STOP);

	if (dev->isp_ver == ISP_V20 || dev->isp_ver == ISP_V21)
		kfifo_reset(&dev->csi_dev.rdbk_kfifo);
	if (dev->emd_vc <= CIF_ISP_ADD_DATA_VC_MAX) {
		for (i = 0; i < RKISP_EMDDATA_FIFO_MAX; i++)
			kfifo_free(&dev->emd_data_fifo[i].mipi_kfifo);
		dev->emd_vc = 0xFF;
	}

	if (dev->hdr.sensor)
		dev->hdr.sensor = NULL;

	rkisp_params_stream_stop(&dev->params_vdev);

	return 0;
}

/* Mess register operations to start isp */
static int rkisp_isp_start(struct rkisp_device *dev)
{
	struct rkisp_sensor_info *sensor = dev->active_sensor;
	void __iomem *base = dev->base_addr;
	u32 val;
	bool is_direct = true;

	v4l2_dbg(1, rkisp_debug, &dev->v4l2_dev,
		 "%s refcnt:%d\n", __func__,
		 atomic_read(&dev->hw_dev->refcnt));

	/* Activate MIPI */
	if (sensor && sensor->mbus.type == V4L2_MBUS_CSI2) {
		if (dev->isp_ver == ISP_V12 || dev->isp_ver == ISP_V13) {
			/* clear interrupts state */
			readl(base + CIF_ISP_CSI0_ERR1);
			readl(base + CIF_ISP_CSI0_ERR2);
			readl(base + CIF_ISP_CSI0_ERR3);
			/* csi2host enable */
			writel(1, base + CIF_ISP_CSI0_CTRL0);
		} else if (dev->isp_ver < ISP_V12) {
			val = readl(base + CIF_MIPI_CTRL);
			writel(val | CIF_MIPI_CTRL_OUTPUT_ENA,
			       base + CIF_MIPI_CTRL);
		}
	}
	/* Activate ISP */
	val = rkisp_read(dev, CIF_ISP_CTRL, false);
	val |= CIF_ISP_CTRL_ISP_CFG_UPD | CIF_ISP_CTRL_ISP_ENABLE |
	       CIF_ISP_CTRL_ISP_INFORM_ENABLE | CIF_ISP_CTRL_ISP_CFG_UPD_PERMANENT;
	if (dev->isp_ver == ISP_V20)
		val |= NOC_HURRY_PRIORITY(2) | NOC_HURRY_W_MODE(2) | NOC_HURRY_R_MODE(1);
	if (atomic_read(&dev->hw_dev->refcnt) > 1)
		is_direct = false;
	rkisp_write(dev, CIF_ISP_CTRL, val, is_direct);

	dev->isp_err_cnt = 0;
	dev->isp_isr_cnt = 0;
	dev->isp_state = ISP_START | ISP_FRAME_END;
	dev->irq_ends_mask |= ISP_FRAME_END | ISP_FRAME_IN;
	dev->irq_ends = 0;

	/* XXX: Is the 1000us too long?
	 * CIF spec says to wait for sufficient time after enabling
	 * the MIPI interface and before starting the sensor output.
	 */
	if (dev->hw_dev->is_single)
		usleep_range(1000, 1200);

	v4l2_dbg(1, rkisp_debug, &dev->v4l2_dev,
		 "%s MI_CTRL 0x%08x ISP_CTRL 0x%08x\n", __func__,
		 readl(base + CIF_MI_CTRL), readl(base + CIF_ISP_CTRL));

	rkisp_csi_trigger_event(dev, T_CMD_QUEUE, NULL);
	return 0;
}

/***************************** isp sub-devs *******************************/

static const struct ispsd_in_fmt rkisp_isp_input_formats[] = {
	{
		.name		= "SBGGR10_1X10",
		.mbus_code	= MEDIA_BUS_FMT_SBGGR10_1X10,
		.fmt_type	= FMT_BAYER,
		.mipi_dt	= CIF_CSI2_DT_RAW10,
		.bayer_pat	= RAW_BGGR,
		.bus_width	= 10,
	}, {
		.name		= "SRGGB10_1X10",
		.mbus_code	= MEDIA_BUS_FMT_SRGGB10_1X10,
		.fmt_type	= FMT_BAYER,
		.mipi_dt	= CIF_CSI2_DT_RAW10,
		.bayer_pat	= RAW_RGGB,
		.bus_width	= 10,
	}, {
		.name		= "SGBRG10_1X10",
		.mbus_code	= MEDIA_BUS_FMT_SGBRG10_1X10,
		.fmt_type	= FMT_BAYER,
		.mipi_dt	= CIF_CSI2_DT_RAW10,
		.bayer_pat	= RAW_GBRG,
		.bus_width	= 10,
	}, {
		.name		= "SGRBG10_1X10",
		.mbus_code	= MEDIA_BUS_FMT_SGRBG10_1X10,
		.fmt_type	= FMT_BAYER,
		.mipi_dt	= CIF_CSI2_DT_RAW10,
		.bayer_pat	= RAW_GRBG,
		.bus_width	= 10,
	}, {
		.name		= "SRGGB12_1X12",
		.mbus_code	= MEDIA_BUS_FMT_SRGGB12_1X12,
		.fmt_type	= FMT_BAYER,
		.mipi_dt	= CIF_CSI2_DT_RAW12,
		.bayer_pat	= RAW_RGGB,
		.bus_width	= 12,
	}, {
		.name		= "SBGGR12_1X12",
		.mbus_code	= MEDIA_BUS_FMT_SBGGR12_1X12,
		.fmt_type	= FMT_BAYER,
		.mipi_dt	= CIF_CSI2_DT_RAW12,
		.bayer_pat	= RAW_BGGR,
		.bus_width	= 12,
	}, {
		.name		= "SGBRG12_1X12",
		.mbus_code	= MEDIA_BUS_FMT_SGBRG12_1X12,
		.fmt_type	= FMT_BAYER,
		.mipi_dt	= CIF_CSI2_DT_RAW12,
		.bayer_pat	= RAW_GBRG,
		.bus_width	= 12,
	}, {
		.name		= "SGRBG12_1X12",
		.mbus_code	= MEDIA_BUS_FMT_SGRBG12_1X12,
		.fmt_type	= FMT_BAYER,
		.mipi_dt	= CIF_CSI2_DT_RAW12,
		.bayer_pat	= RAW_GRBG,
		.bus_width	= 12,
	}, {
		.name		= "SRGGB8_1X8",
		.mbus_code	= MEDIA_BUS_FMT_SRGGB8_1X8,
		.fmt_type	= FMT_BAYER,
		.mipi_dt	= CIF_CSI2_DT_RAW8,
		.bayer_pat	= RAW_RGGB,
		.bus_width	= 8,
	}, {
		.name		= "SBGGR8_1X8",
		.mbus_code	= MEDIA_BUS_FMT_SBGGR8_1X8,
		.fmt_type	= FMT_BAYER,
		.mipi_dt	= CIF_CSI2_DT_RAW8,
		.bayer_pat	= RAW_BGGR,
		.bus_width	= 8,
	}, {
		.name		= "SGBRG8_1X8",
		.mbus_code	= MEDIA_BUS_FMT_SGBRG8_1X8,
		.fmt_type	= FMT_BAYER,
		.mipi_dt	= CIF_CSI2_DT_RAW8,
		.bayer_pat	= RAW_GBRG,
		.bus_width	= 8,
	}, {
		.name		= "SGRBG8_1X8",
		.mbus_code	= MEDIA_BUS_FMT_SGRBG8_1X8,
		.fmt_type	= FMT_BAYER,
		.mipi_dt	= CIF_CSI2_DT_RAW8,
		.bayer_pat	= RAW_GRBG,
		.bus_width	= 8,
	}, {
		.name		= "YUYV8_2X8",
		.mbus_code	= MEDIA_BUS_FMT_YUYV8_2X8,
		.fmt_type	= FMT_YUV,
		.mipi_dt	= CIF_CSI2_DT_YUV422_8b,
		.yuv_seq	= CIF_ISP_ACQ_PROP_YCBYCR,
		.bus_width	= 8,
	}, {
		.name		= "YVYU8_2X8",
		.mbus_code	= MEDIA_BUS_FMT_YVYU8_2X8,
		.fmt_type	= FMT_YUV,
		.mipi_dt	= CIF_CSI2_DT_YUV422_8b,
		.yuv_seq	= CIF_ISP_ACQ_PROP_YCRYCB,
		.bus_width	= 8,
	}, {
		.name		= "UYVY8_2X8",
		.mbus_code	= MEDIA_BUS_FMT_UYVY8_2X8,
		.fmt_type	= FMT_YUV,
		.mipi_dt	= CIF_CSI2_DT_YUV422_8b,
		.yuv_seq	= CIF_ISP_ACQ_PROP_CBYCRY,
		.bus_width	= 8,
	}, {
		.name		= "VYUY8_2X8",
		.mbus_code	= MEDIA_BUS_FMT_VYUY8_2X8,
		.fmt_type	= FMT_YUV,
		.mipi_dt	= CIF_CSI2_DT_YUV422_8b,
		.yuv_seq	= CIF_ISP_ACQ_PROP_CRYCBY,
		.bus_width	= 8,
	}, {
		.name		= "YUYV10_2X10",
		.mbus_code	= MEDIA_BUS_FMT_YUYV10_2X10,
		.fmt_type	= FMT_YUV,
		.mipi_dt	= CIF_CSI2_DT_YUV422_8b,
		.yuv_seq	= CIF_ISP_ACQ_PROP_YCBYCR,
		.bus_width	= 10,
	}, {
		.name		= "YVYU10_2X10",
		.mbus_code	= MEDIA_BUS_FMT_YVYU10_2X10,
		.fmt_type	= FMT_YUV,
		.mipi_dt	= CIF_CSI2_DT_YUV422_8b,
		.yuv_seq	= CIF_ISP_ACQ_PROP_YCRYCB,
		.bus_width	= 10,
	}, {
		.name		= "UYVY10_2X10",
		.mbus_code	= MEDIA_BUS_FMT_UYVY10_2X10,
		.fmt_type	= FMT_YUV,
		.mipi_dt	= CIF_CSI2_DT_YUV422_8b,
		.yuv_seq	= CIF_ISP_ACQ_PROP_CBYCRY,
		.bus_width	= 10,
	}, {
		.name		= "VYUY10_2X10",
		.mbus_code	= MEDIA_BUS_FMT_VYUY10_2X10,
		.fmt_type	= FMT_YUV,
		.mipi_dt	= CIF_CSI2_DT_YUV422_8b,
		.yuv_seq	= CIF_ISP_ACQ_PROP_CRYCBY,
		.bus_width	= 10,
	}, {
		.name		= "YUYV12_2X12",
		.mbus_code	= MEDIA_BUS_FMT_YUYV12_2X12,
		.fmt_type	= FMT_YUV,
		.mipi_dt	= CIF_CSI2_DT_YUV422_8b,
		.yuv_seq	= CIF_ISP_ACQ_PROP_YCBYCR,
		.bus_width	= 12,
	}, {
		.name		= "YVYU12_2X12",
		.mbus_code	= MEDIA_BUS_FMT_YVYU12_2X12,
		.fmt_type	= FMT_YUV,
		.mipi_dt	= CIF_CSI2_DT_YUV422_8b,
		.yuv_seq	= CIF_ISP_ACQ_PROP_YCRYCB,
		.bus_width	= 12,
	}, {
		.name		= "UYVY12_2X12",
		.mbus_code	= MEDIA_BUS_FMT_UYVY12_2X12,
		.fmt_type	= FMT_YUV,
		.mipi_dt	= CIF_CSI2_DT_YUV422_8b,
		.yuv_seq	= CIF_ISP_ACQ_PROP_CBYCRY,
		.bus_width	= 12,
	}, {
		.name		= "VYUY12_2X12",
		.mbus_code	= MEDIA_BUS_FMT_VYUY12_2X12,
		.fmt_type	= FMT_YUV,
		.mipi_dt	= CIF_CSI2_DT_YUV422_8b,
		.yuv_seq	= CIF_ISP_ACQ_PROP_CRYCBY,
		.bus_width	= 12,
	}, {
		.name		= "Y8_1X8",
		.mbus_code	= MEDIA_BUS_FMT_Y8_1X8,
		.fmt_type	= FMT_BAYER,
		.mipi_dt	= CIF_CSI2_DT_RAW8,
		.yuv_seq	= CIF_ISP_ACQ_PROP_YCBYCR,
		.bus_width	= 8,
	}, {
		.name		= "Y10_1X8",
		.mbus_code	= MEDIA_BUS_FMT_Y10_1X10,
		.fmt_type	= FMT_BAYER,
		.mipi_dt	= CIF_CSI2_DT_RAW10,
		.yuv_seq	= CIF_ISP_ACQ_PROP_YCBYCR,
		.bus_width	= 10,
	}, {
		.name		= "Y12_1X12",
		.mbus_code	= MEDIA_BUS_FMT_Y12_1X12,
		.fmt_type	= FMT_BAYER,
		.mipi_dt	= CIF_CSI2_DT_RAW12,
		.yuv_seq	= CIF_ISP_ACQ_PROP_YCBYCR,
		.bus_width	= 12,
	}
};

static const struct ispsd_out_fmt rkisp_isp_output_formats[] = {
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
	int i, array_size = ARRAY_SIZE(rkisp_isp_input_formats);

	for (i = 0; i < array_size; i++) {
		fmt = &rkisp_isp_input_formats[i];
		if (fmt->mbus_code == mbus_code)
			return fmt;
	}

	return NULL;
}

static const struct ispsd_out_fmt *find_out_fmt(u32 mbus_code)
{
	const struct ispsd_out_fmt *fmt;
	int i, array_size = ARRAY_SIZE(rkisp_isp_output_formats);

	for (i = 0; i < array_size; i++) {
		fmt = &rkisp_isp_output_formats[i];
		if (fmt->mbus_code == mbus_code)
			return fmt;
	}

	return NULL;
}

static int rkisp_isp_sd_enum_mbus_code(struct v4l2_subdev *sd,
					struct v4l2_subdev_pad_config *cfg,
					struct v4l2_subdev_mbus_code_enum *code)
{
	unsigned int i = code->index;

	if (code->pad == RKISP_ISP_PAD_SINK) {
		if (i >= ARRAY_SIZE(rkisp_isp_input_formats))
			return -EINVAL;
		code->code = rkisp_isp_input_formats[i].mbus_code;
	} else {
		if (i >= ARRAY_SIZE(rkisp_isp_output_formats))
			return -EINVAL;
		code->code = rkisp_isp_output_formats[i].mbus_code;
	}

	return 0;
}

#define sd_to_isp_sd(_sd) container_of(_sd, struct rkisp_isp_subdev, sd)
static int rkisp_isp_sd_get_fmt(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *mf;
	struct rkisp_isp_subdev *isp_sd = sd_to_isp_sd(sd);

	if (!fmt)
		goto err;

	if (fmt->pad != RKISP_ISP_PAD_SINK &&
	    fmt->pad != RKISP_ISP_PAD_SOURCE_PATH)
		goto err;

	mf = &fmt->format;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		if (!cfg)
			goto err;
		mf = v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
	}

	if (fmt->pad == RKISP_ISP_PAD_SINK) {
		*mf = isp_sd->in_frm;
	} else if (fmt->pad == RKISP_ISP_PAD_SOURCE_PATH) {
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

static int rkisp_isp_sd_set_fmt(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_format *fmt)
{
	struct rkisp_device *isp_dev = sd_to_isp_dev(sd);
	struct rkisp_isp_subdev *isp_sd = &isp_dev->isp_sdev;
	struct v4l2_mbus_framefmt *mf;

	if (!fmt)
		goto err;

	if (fmt->pad != RKISP_ISP_PAD_SINK &&
	    fmt->pad != RKISP_ISP_PAD_SOURCE_PATH)
		goto err;

	mf = &fmt->format;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		if (!cfg)
			goto err;
		mf = v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
	}

	if (fmt->pad == RKISP_ISP_PAD_SINK) {
		const struct ispsd_in_fmt *in_fmt;

		in_fmt = find_in_fmt(mf->code);
		if (!in_fmt ||
		    mf->width < CIF_ISP_INPUT_W_MIN ||
		    mf->height < CIF_ISP_INPUT_H_MIN)
			goto err;

		isp_sd->in_fmt = *in_fmt;
		isp_sd->in_frm = *mf;
	} else if (fmt->pad == RKISP_ISP_PAD_SOURCE_PATH) {
		const struct ispsd_out_fmt *out_fmt;

		out_fmt = find_out_fmt(mf->code);
		if (!out_fmt)
			goto err;
		isp_sd->out_fmt = *out_fmt;
		/* window size is set in s_selection */
		mf->width  = isp_sd->out_crop.width;
		mf->height = isp_sd->out_crop.height;
		/* full range by default */
		if (!mf->quantization)
			mf->quantization = V4L2_QUANTIZATION_FULL_RANGE;
		/*
		 * It is quantization for output,
		 * isp use bt601 limit-range in internal
		 */
		isp_sd->quantization = mf->quantization;
	}

	mf->field = V4L2_FIELD_NONE;
	return 0;
err:
	return -EINVAL;
}

static void rkisp_isp_sd_try_crop(struct v4l2_subdev *sd,
				  struct v4l2_rect *crop,
				  u32 pad)
{
	struct rkisp_isp_subdev *isp_sd = sd_to_isp_sd(sd);
	struct rkisp_device *dev = sd_to_isp_dev(sd);
	struct v4l2_rect in_crop = isp_sd->in_crop;

	crop->left = ALIGN(crop->left, 2);
	crop->width = ALIGN(crop->width, 2);

	if (pad == RKISP_ISP_PAD_SINK) {
		/* update sensor info if sensor link be changed */
		rkisp_update_sensor_info(dev);
		rkisp_align_sensor_resolution(dev, crop, true);
	} else if (pad == RKISP_ISP_PAD_SOURCE_PATH) {
		crop->left = clamp_t(u32, crop->left, 0, in_crop.width);
		crop->top = clamp_t(u32, crop->top, 0, in_crop.height);
		crop->width = clamp_t(u32, crop->width, CIF_ISP_OUTPUT_W_MIN,
				in_crop.width - crop->left);
		crop->height = clamp_t(u32, crop->height, CIF_ISP_OUTPUT_H_MIN,
				in_crop.height - crop->top);
	}
}

static int rkisp_isp_sd_get_selection(struct v4l2_subdev *sd,
				      struct v4l2_subdev_pad_config *cfg,
				      struct v4l2_subdev_selection *sel)
{
	struct rkisp_isp_subdev *isp_sd = sd_to_isp_sd(sd);
	struct rkisp_device *dev = sd_to_isp_dev(sd);
	struct v4l2_rect *crop;
	u32 max_w, max_h;

	if (!sel)
		goto err;
	if (sel->pad != RKISP_ISP_PAD_SOURCE_PATH &&
	    sel->pad != RKISP_ISP_PAD_SINK)
		goto err;

	crop = &sel->r;
	if (sel->which == V4L2_SUBDEV_FORMAT_TRY) {
		if (!cfg)
			goto err;
		crop = v4l2_subdev_get_try_crop(sd, cfg, sel->pad);
	}

	*crop = isp_sd->in_crop;
	switch (sel->target) {
	case V4L2_SEL_TGT_CROP_BOUNDS:
		crop->left = 0;
		crop->top = 0;
		if (sel->pad == RKISP_ISP_PAD_SINK) {
			if (dev->isp_ver == ISP_V12) {
				max_w = CIF_ISP_INPUT_W_MAX_V12;
				max_h = CIF_ISP_INPUT_H_MAX_V12;
			} else if (dev->isp_ver == ISP_V13) {
				max_w = CIF_ISP_INPUT_W_MAX_V13;
				max_h = CIF_ISP_INPUT_H_MAX_V13;
			} else {
				max_w = CIF_ISP_INPUT_W_MAX;
				max_h = CIF_ISP_INPUT_H_MAX;
			}
			crop->width = min_t(u32, isp_sd->in_frm.width, max_w);
			crop->height = min_t(u32, isp_sd->in_frm.height, max_h);
		}
		break;
	case V4L2_SEL_TGT_CROP:
		if (sel->pad == RKISP_ISP_PAD_SOURCE_PATH)
			*crop = isp_sd->out_crop;
		break;
	default:
		goto err;
	}

	return 0;
err:
	return -EINVAL;
}

static int rkisp_isp_sd_set_selection(struct v4l2_subdev *sd,
				      struct v4l2_subdev_pad_config *cfg,
				      struct v4l2_subdev_selection *sel)
{
	struct rkisp_isp_subdev *isp_sd = sd_to_isp_sd(sd);
	struct rkisp_device *dev = sd_to_isp_dev(sd);
	struct v4l2_rect *crop;

	if (!sel)
		goto err;
	if (sel->pad != RKISP_ISP_PAD_SOURCE_PATH &&
	    sel->pad != RKISP_ISP_PAD_SINK)
		goto err;
	if (sel->target != V4L2_SEL_TGT_CROP)
		goto err;

	crop = &sel->r;
	if (sel->which == V4L2_SUBDEV_FORMAT_TRY) {
		if (!cfg)
			goto err;
		crop = v4l2_subdev_get_try_crop(sd, cfg, sel->pad);
	}

	rkisp_isp_sd_try_crop(sd, crop, sel->pad);

	v4l2_dbg(1, rkisp_debug, &dev->v4l2_dev,
		 "%s: pad: %d sel(%d,%d)/%dx%d\n", __func__, sel->pad,
		 crop->left, crop->top, crop->width, crop->height);

	if (sel->pad == RKISP_ISP_PAD_SINK) {
		isp_sd->in_crop = *crop;
		/* ISP20 don't have out crop */
		if (dev->isp_ver == ISP_V20 || dev->isp_ver == ISP_V21) {
			isp_sd->out_crop = *crop;
			isp_sd->out_crop.left = 0;
			isp_sd->out_crop.top = 0;
			dev->br_dev.crop = isp_sd->out_crop;
		}
	} else {
		if (dev->isp_ver == ISP_V20 || dev->isp_ver == ISP_V21)
			*crop = isp_sd->out_crop;
		isp_sd->out_crop = *crop;
	}

	return 0;
err:
	return -EINVAL;
}

static void rkisp_isp_read_add_fifo_data(struct rkisp_device *dev)
{
	struct v4l2_device *v4l2_dev = &dev->v4l2_dev;
	void __iomem *base = dev->base_addr;
	u32 mipi_status = 0;
	u32 data_len = 0;
	u32 fifo_data = 0;
	u32 i, idx, cur_frame_id;

	if (dev->isp_ver != ISP_V10 &&
	    dev->isp_ver != ISP_V10_1)
		return;

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
		dev->emd_data_idx = (idx + 1) % RKISP_EMDDATA_FIFO_MAX;
	}

	v4l2_dbg(1, rkisp_debug, &dev->v4l2_dev,
		 "emd kfifo size: %d, frame_id %d\n",
		 kfifo_len(&dev->emd_data_fifo[idx].mipi_kfifo),
		 dev->emd_data_fifo[idx].frame_id);
}

static int rkisp_isp_sd_s_stream(struct v4l2_subdev *sd, int on)
{
	struct rkisp_device *isp_dev = sd_to_isp_dev(sd);
	int ret = 0;

	if (!on) {
		rkisp_stop_3a_run(isp_dev);
		atomic_dec(&isp_dev->hw_dev->refcnt);
		wait_event_timeout(isp_dev->sync_onoff,
			isp_dev->irq_ends_mask == (ISP_FRAME_END | ISP_FRAME_IN) &&
			(!IS_HDR_RDBK(isp_dev->csi_dev.rd_mode) ||
			 isp_dev->isp_state & ISP_STOP), msecs_to_jiffies(5));
		return rkisp_isp_stop(isp_dev);
	}

	rkisp_start_3a_run(isp_dev);
	atomic_inc(&isp_dev->hw_dev->refcnt);
	atomic_set(&isp_dev->isp_sdev.frm_sync_seq, 0);
	ret = rkisp_config_cif(isp_dev);
	if (ret < 0)
		goto out;

	ret = rkisp_isp_start(isp_dev);
out:
	if (ret < 0)
		atomic_dec(&isp_dev->hw_dev->refcnt);
	return ret;
}

static int rkisp_isp_sd_s_power(struct v4l2_subdev *sd, int on)
{
	struct rkisp_device *isp_dev = sd_to_isp_dev(sd);
	int ret;

	v4l2_dbg(1, rkisp_debug, &isp_dev->v4l2_dev,
		 "%s on:%d\n", __func__, on);

	if (on) {
		if (isp_dev->isp_ver == ISP_V20 || isp_dev->isp_ver == ISP_V21)
			kfifo_reset(&isp_dev->csi_dev.rdbk_kfifo);
		ret = pm_runtime_get_sync(isp_dev->dev);
	} else {
		ret = pm_runtime_put_sync(isp_dev->dev);
	}

	if (ret < 0)
		v4l2_err(sd, "%s on:%d failed:%d\n", __func__, on, ret);
	return ret;
}

static int rkisp_subdev_link_setup(struct media_entity *entity,
				    const struct media_pad *local,
				    const struct media_pad *remote,
				    u32 flags)
{
	struct v4l2_subdev *sd = media_entity_to_v4l2_subdev(entity);
	struct rkisp_device *dev;
	struct rkisp_stream *stream = NULL;
	u8 rawrd = INP_RAWRD0 | INP_RAWRD1 | INP_RAWRD2;

	if (local->index != RKISP_ISP_PAD_SINK &&
	    local->index != RKISP_ISP_PAD_SOURCE_PATH)
		return 0;
	if (!sd)
		return -ENODEV;
	dev = sd_to_isp_dev(sd);
	if (!dev)
		return -ENODEV;

	if (!strcmp(remote->entity->name, DMA_VDEV_NAME)) {
		stream = &dev->dmarx_dev.stream[RKISP_STREAM_DMARX];
		if (flags & MEDIA_LNK_FL_ENABLED) {
			if (dev->isp_inp & ~INP_DMARX_ISP)
				goto err;
			dev->isp_inp = INP_DMARX_ISP;
		} else {
			if (dev->active_sensor)
				dev->active_sensor = NULL;
			dev->isp_inp = INP_INVAL;
		}
	} else if (!strcmp(remote->entity->name, CSI_DEV_NAME)) {
		if (flags & MEDIA_LNK_FL_ENABLED) {
			if (dev->isp_inp & ~(INP_CSI | INP_CIF | rawrd))
				goto err;
			dev->isp_inp |= INP_CSI;
		} else {
			if (dev->active_sensor)
				dev->active_sensor = NULL;
			dev->isp_inp &= ~INP_CSI;
		}
	} else if (!strcmp(remote->entity->name, DMARX0_VDEV_NAME)) {
		stream = &dev->dmarx_dev.stream[RKISP_STREAM_RAWRD0];
		if (flags & MEDIA_LNK_FL_ENABLED) {
			if (dev->isp_inp & ~(INP_CSI | INP_CIF | rawrd))
				goto err;
			dev->isp_inp |= INP_RAWRD0;
		} else {
			dev->isp_inp &= ~INP_RAWRD0;
		}
	} else if (!strcmp(remote->entity->name, DMARX1_VDEV_NAME)) {
		stream = &dev->dmarx_dev.stream[RKISP_STREAM_RAWRD1];
		if (flags & MEDIA_LNK_FL_ENABLED) {
			if (dev->isp_inp & ~(INP_CSI | INP_CIF | rawrd))
				goto err;
			dev->isp_inp |= INP_RAWRD1;
		} else {
			dev->isp_inp &= ~INP_RAWRD1;
		}
	} else if (!strcmp(remote->entity->name, DMARX2_VDEV_NAME)) {
		stream = &dev->dmarx_dev.stream[RKISP_STREAM_RAWRD2];
		if (flags & MEDIA_LNK_FL_ENABLED) {
			if (dev->isp_inp & ~(INP_CSI | INP_CIF | rawrd))
				goto err;
			dev->isp_inp |= INP_RAWRD2;
		} else {
			dev->isp_inp &= ~INP_RAWRD2;
		}
	} else if (!strcmp(remote->entity->name, SP_VDEV_NAME)) {
		stream = &dev->cap_dev.stream[RKISP_STREAM_SP];
	} else if (!strcmp(remote->entity->name, MP_VDEV_NAME)) {
		stream = &dev->cap_dev.stream[RKISP_STREAM_MP];
		if (flags & MEDIA_LNK_FL_ENABLED &&
		    dev->br_dev.linked)
			goto err;
	} else if (!strcmp(remote->entity->name, BRIDGE_DEV_NAME)) {
		if (flags & MEDIA_LNK_FL_ENABLED &&
		    dev->cap_dev.stream[RKISP_STREAM_MP].linked)
			goto err;
		dev->br_dev.linked = flags & MEDIA_LNK_FL_ENABLED;
	} else if (!strcmp(remote->entity->name, "rockchip-mipi-dphy-rx")) {
		if (flags & MEDIA_LNK_FL_ENABLED) {
			if (dev->isp_inp & ~INP_LVDS)
				goto err;
			dev->isp_inp |= INP_LVDS;
		} else {
			if (dev->active_sensor)
				dev->active_sensor = NULL;
			dev->isp_inp &= ~INP_LVDS;
		}
	} else if (strstr(remote->entity->name, "rkcif")) {
		if (flags & MEDIA_LNK_FL_ENABLED) {
			if (dev->isp_inp & ~(INP_CIF | rawrd))
				goto err;
			dev->isp_inp |= INP_CIF;
		} else {
			 dev->isp_inp &= ~INP_CIF;
		}
	} else {
		if (flags & MEDIA_LNK_FL_ENABLED) {
			if (dev->isp_inp & ~INP_DVP)
				goto err;
			dev->isp_inp |= INP_DVP;
		} else {
			if (dev->active_sensor)
				dev->active_sensor = NULL;
			dev->isp_inp &= ~INP_INVAL;
		}
	}

	if (stream)
		stream->linked = flags & MEDIA_LNK_FL_ENABLED;
	if (dev->isp_inp & rawrd)
		dev->dmarx_dev.trigger = T_MANUAL;
	else
		dev->dmarx_dev.trigger = T_AUTO;

	v4l2_dbg(1, rkisp_debug, &dev->v4l2_dev,
		 "isp input:0x%x\n", dev->isp_inp);
	return 0;
err:
	v4l2_err(sd, "link error %s -> %s\n"
		 "\tcsi dvp lvds dmaread can't work together\n"
		 "\trawrd can't work with dvp lvds dmaread\n"
		 "\tbridge can't work with mainpath/selfpath\n",
		 local->entity->name, remote->entity->name);
	return -EINVAL;
}

static int rkisp_subdev_link_validate(struct media_link *link)
{
	if (link->source->index == RKISP_ISP_PAD_SINK_PARAMS)
		return 0;

	return v4l2_subdev_link_validate(link);
}

#ifdef CONFIG_MEDIA_CONTROLLER
static int rkisp_subdev_fmt_link_validate(struct v4l2_subdev *sd,
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
#endif

void
rkisp_isp_queue_event_sof(struct rkisp_isp_subdev *isp)
{
	struct v4l2_event event = {
		.type = V4L2_EVENT_FRAME_SYNC,
		.u.frame_sync.frame_sequence =
			atomic_inc_return(&isp->frm_sync_seq) - 1,
	};

	event.timestamp = ns_to_timespec(ktime_get_ns());
	v4l2_event_queue(isp->sd.devnode, &event);
}

static int rkisp_isp_sd_subs_evt(struct v4l2_subdev *sd, struct v4l2_fh *fh,
				  struct v4l2_event_subscription *sub)
{
	if (sub->type != V4L2_EVENT_FRAME_SYNC)
		return -EINVAL;

	/* Line number. For now only zero accepted. */
	if (sub->id != 0)
		return -EINVAL;

	return v4l2_event_subscribe(fh, sub, 0, NULL);
}

static long rkisp_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct rkisp_device *isp_dev = sd_to_isp_dev(sd);
	struct rkisp_thunderboot_resmem *resmem;
	struct rkisp_thunderboot_resmem_head *head;
	struct rkisp_ldchbuf_info *ldchbuf;
	struct rkisp_ldchbuf_size *ldchsize;
	struct rkisp_thunderboot_shmem *shmem;
	struct isp2x_buf_idxfd *idxfd;
	void *resmem_va;
	long ret = 0;

	if (!arg && cmd != RKISP_CMD_FREE_SHARED_BUF)
		return -EINVAL;

	switch (cmd) {
	case RKISP_CMD_TRIGGER_READ_BACK:
		rkisp_csi_trigger_event(isp_dev, T_CMD_QUEUE, arg);
		break;
	case RKISP_CMD_CSI_MEMORY_MODE:
		if (*((int *)arg) == CSI_MEM_BYTE_BE)
			isp_dev->csi_dev.memory = SW_CSI_RWA_WR_SIMG_SWP |
						SW_CSI_RAW_WR_SIMG_MODE;
		else if (*((int *)arg) == CSI_MEM_BYTE_LE)
			isp_dev->csi_dev.memory = SW_CSI_RAW_WR_SIMG_MODE;
		else
			isp_dev->csi_dev.memory = 0;
		break;
	case RKISP_CMD_GET_SHARED_BUF:
		resmem = (struct rkisp_thunderboot_resmem *)arg;
		resmem->resmem_padr = isp_dev->resmem_pa;
		resmem->resmem_size = isp_dev->resmem_size;
		if (!isp_dev->resmem_pa || !isp_dev->resmem_size) {
			v4l2_info(sd, "no reserved memory for thunderboot\n");
			break;
		}

		rkisp_chk_tb_over(isp_dev);
		dma_sync_single_for_cpu(isp_dev->dev, isp_dev->resmem_addr,
					sizeof(struct rkisp_thunderboot_resmem_head),
					DMA_FROM_DEVICE);

		resmem_va = phys_to_virt(isp_dev->resmem_pa);
		head = (struct rkisp_thunderboot_resmem_head *)resmem_va;
		if (head->complete != RKISP_TB_OK) {
			resmem->resmem_size = 0;
			dma_unmap_single(isp_dev->dev, isp_dev->resmem_pa,
					 sizeof(struct rkisp_thunderboot_resmem_head),
					 DMA_FROM_DEVICE);
			free_reserved_area(phys_to_virt(isp_dev->resmem_pa),
					   phys_to_virt(isp_dev->resmem_pa) + isp_dev->resmem_size,
					   -1, "rkisp_thunderboot");

			isp_dev->resmem_pa = 0;
			isp_dev->resmem_size = 0;
		}
		break;
	case RKISP_CMD_FREE_SHARED_BUF:
		if (isp_dev->resmem_pa && isp_dev->resmem_size) {
			dma_unmap_single(isp_dev->dev, isp_dev->resmem_pa,
					 sizeof(struct rkisp_thunderboot_resmem_head),
					 DMA_FROM_DEVICE);
			free_reserved_area(phys_to_virt(isp_dev->resmem_pa),
					   phys_to_virt(isp_dev->resmem_pa) + isp_dev->resmem_size,
					   -1, "rkisp_thunderboot");
		}

		isp_dev->resmem_pa = 0;
		isp_dev->resmem_size = 0;
		break;
	case RKISP_CMD_GET_LDCHBUF_INFO:
		ldchbuf = (struct rkisp_ldchbuf_info *)arg;
		rkisp_params_get_ldchbuf_inf(&isp_dev->params_vdev, ldchbuf);
		break;
	case RKISP_CMD_SET_LDCHBUF_SIZE:
		ldchsize = (struct rkisp_ldchbuf_size *)arg;
		rkisp_params_set_ldchbuf_size(&isp_dev->params_vdev, ldchsize);
		break;
	case RKISP_CMD_GET_SHM_BUFFD:
		shmem = (struct rkisp_thunderboot_shmem *)arg;
		ret = rkisp_tb_shm_ioctl(shmem);
		break;
	case RKISP_CMD_GET_FBCBUF_FD:
		idxfd = (struct isp2x_buf_idxfd *)arg;
		ret = rkisp_bridge_get_fbcbuf_fd(isp_dev, idxfd);
		break;
	default:
		ret = -ENOIOCTLCMD;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long rkisp_compat_ioctl32(struct v4l2_subdev *sd,
				 unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct isp2x_csi_trigger trigger;
	struct rkisp_thunderboot_resmem resmem;
	struct rkisp_ldchbuf_info ldchbuf;
	struct rkisp_ldchbuf_size ldchsize;
	struct rkisp_thunderboot_shmem shmem;
	struct isp2x_buf_idxfd idxfd;
	long ret = 0;
	int mode;

	if (!up && cmd != RKISP_CMD_FREE_SHARED_BUF)
		return -EINVAL;

	switch (cmd) {
	case RKISP_CMD_TRIGGER_READ_BACK:
		ret = copy_from_user(&trigger, up, sizeof(trigger));
		if (!ret)
			ret = rkisp_ioctl(sd, cmd, &trigger);
		break;
	case RKISP_CMD_CSI_MEMORY_MODE:
		ret = copy_from_user(&mode, up, sizeof(int));
		if (!ret)
			ret = rkisp_ioctl(sd, cmd, &mode);
		break;
	case RKISP_CMD_GET_SHARED_BUF:
		ret = rkisp_ioctl(sd, cmd, &resmem);
		if (!ret)
			ret = copy_to_user(up, &resmem, sizeof(resmem));
		break;
	case RKISP_CMD_FREE_SHARED_BUF:
		ret = rkisp_ioctl(sd, cmd, NULL);
		break;
	case RKISP_CMD_GET_LDCHBUF_INFO:
		ret = rkisp_ioctl(sd, cmd, &ldchbuf);
		if (!ret)
			ret = copy_to_user(up, &ldchbuf, sizeof(ldchbuf));
		break;
	case RKISP_CMD_SET_LDCHBUF_SIZE:
		ret = copy_from_user(&ldchsize, up, sizeof(ldchsize));
		if (!ret)
			ret = rkisp_ioctl(sd, cmd, &ldchsize);
		break;
	case RKISP_CMD_GET_SHM_BUFFD:
		ret = copy_from_user(&shmem, up, sizeof(shmem));
		if (!ret) {
			ret = rkisp_ioctl(sd, cmd, &shmem);
			if (!ret)
				ret = copy_to_user(up, &shmem, sizeof(shmem));
		}
		break;
	case RKISP_CMD_GET_FBCBUF_FD:
		ret = rkisp_ioctl(sd, cmd, &idxfd);
		if (!ret)
			ret = copy_to_user(up, &idxfd, sizeof(idxfd));
		break;
	default:
		ret = -ENOIOCTLCMD;
	}

	return ret;
}
#endif

static const struct v4l2_subdev_pad_ops rkisp_isp_sd_pad_ops = {
	.enum_mbus_code = rkisp_isp_sd_enum_mbus_code,
	.get_selection = rkisp_isp_sd_get_selection,
	.set_selection = rkisp_isp_sd_set_selection,
	.get_fmt = rkisp_isp_sd_get_fmt,
	.set_fmt = rkisp_isp_sd_set_fmt,
#ifdef CONFIG_MEDIA_CONTROLLER
	.link_validate = rkisp_subdev_fmt_link_validate,
#endif
};

static const struct media_entity_operations rkisp_isp_sd_media_ops = {
	.link_setup = rkisp_subdev_link_setup,
	.link_validate = rkisp_subdev_link_validate,
};

static const struct v4l2_subdev_video_ops rkisp_isp_sd_video_ops = {
	.s_stream = rkisp_isp_sd_s_stream,
};

static const struct v4l2_subdev_core_ops rkisp_isp_core_ops = {
	.subscribe_event = rkisp_isp_sd_subs_evt,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
	.s_power = rkisp_isp_sd_s_power,
	.ioctl = rkisp_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = rkisp_compat_ioctl32,
#endif
};

static struct v4l2_subdev_ops rkisp_isp_sd_ops = {
	.core = &rkisp_isp_core_ops,
	.video = &rkisp_isp_sd_video_ops,
	.pad = &rkisp_isp_sd_pad_ops,
};

static void rkisp_isp_sd_init_default_fmt(struct rkisp_isp_subdev *isp_sd)
{
	struct v4l2_mbus_framefmt *in_frm = &isp_sd->in_frm;
	struct v4l2_rect *in_crop = &isp_sd->in_crop;
	struct v4l2_rect *out_crop = &isp_sd->out_crop;
	struct ispsd_in_fmt *in_fmt = &isp_sd->in_fmt;
	struct ispsd_out_fmt *out_fmt = &isp_sd->out_fmt;

	*in_fmt = rkisp_isp_input_formats[0];
	in_frm->width = RKISP_DEFAULT_WIDTH;
	in_frm->height = RKISP_DEFAULT_HEIGHT;
	in_frm->code = in_fmt->mbus_code;

	in_crop->width = in_frm->width;
	in_crop->height = in_frm->height;
	in_crop->left = 0;
	in_crop->top = 0;

	/* propagate to source */
	*out_crop = *in_crop;
	*out_fmt = rkisp_isp_output_formats[0];
}

int rkisp_register_isp_subdev(struct rkisp_device *isp_dev,
			       struct v4l2_device *v4l2_dev)
{
	struct rkisp_isp_subdev *isp_sdev = &isp_dev->isp_sdev;
	struct v4l2_subdev *sd = &isp_sdev->sd;
	int ret;

	v4l2_subdev_init(sd, &rkisp_isp_sd_ops);
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS;
	sd->entity.ops = &rkisp_isp_sd_media_ops;
	sd->entity.function = MEDIA_ENT_F_V4L2_SUBDEV_UNKNOWN;
	snprintf(sd->name, sizeof(sd->name), ISP_SUBDEV_NAME);

	isp_sdev->pads[RKISP_ISP_PAD_SINK].flags =
		MEDIA_PAD_FL_SINK | MEDIA_PAD_FL_MUST_CONNECT;
	isp_sdev->pads[RKISP_ISP_PAD_SINK_PARAMS].flags = MEDIA_PAD_FL_SINK;
	isp_sdev->pads[RKISP_ISP_PAD_SOURCE_PATH].flags = MEDIA_PAD_FL_SOURCE;
	isp_sdev->pads[RKISP_ISP_PAD_SOURCE_STATS].flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&sd->entity, RKISP_ISP_PAD_MAX,
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

	rkisp_isp_sd_init_default_fmt(isp_sdev);
	isp_dev->hdr.sensor = NULL;
	isp_dev->isp_state = ISP_STOP;

	return 0;
err_cleanup_media_entity:
	media_entity_cleanup(&sd->entity);
	return ret;
}

void rkisp_unregister_isp_subdev(struct rkisp_device *isp_dev)
{
	struct v4l2_subdev *sd = &isp_dev->isp_sdev.sd;

	v4l2_device_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
}

#define shm_head_poll_timeout(isp_dev, cond, sleep_us, timeout_us)	\
({ \
	u64 __timeout_us = (timeout_us); \
	unsigned long __sleep_us = (sleep_us); \
	ktime_t __timeout = ktime_add_us(ktime_get(), __timeout_us); \
	might_sleep_if((__sleep_us) != 0); \
	for (;;) { \
		dma_sync_single_for_cpu(isp_dev->dev, isp_dev->resmem_addr, \
			sizeof(struct rkisp_thunderboot_resmem_head), \
			DMA_FROM_DEVICE); \
		if (cond) \
			break; \
		if (__timeout_us && \
		    ktime_compare(ktime_get(), __timeout) > 0) { \
			break; \
		} \
		if (__sleep_us) \
			usleep_range((__sleep_us >> 2) + 1, __sleep_us); \
	} \
	(cond) ? 0 : -ETIMEDOUT; \
})

#ifdef CONFIG_VIDEO_ROCKCHIP_THUNDER_BOOT_ISP
void rkisp_chk_tb_over(struct rkisp_device *isp_dev)
{
	struct rkisp_thunderboot_resmem_head *head;
	enum rkisp_tb_state tb_state;
	void *resmem_va;

	if (!isp_dev->resmem_pa || !isp_dev->resmem_size) {
		v4l2_info(&isp_dev->v4l2_dev,
			  "no reserved memory for thunderboot\n");
		return;
	}

	resmem_va = phys_to_virt(isp_dev->resmem_pa);
	head = (struct rkisp_thunderboot_resmem_head *)resmem_va;
	if (isp_dev->hw_dev->is_thunderboot) {
		shm_head_poll_timeout(isp_dev, !!head->enable, 2000, 200 * USEC_PER_MSEC);
		shm_head_poll_timeout(isp_dev, !!head->complete, 5000, 500 * USEC_PER_MSEC);
		if (head->complete != RKISP_TB_OK)
			v4l2_info(&isp_dev->v4l2_dev,
				  "wait thunderboot over timeout\n");

		v4l2_info(&isp_dev->v4l2_dev,
			  "thunderboot info: %d, %d, %d, %d, %d, %d, 0x%x\n",
			  head->enable,
			  head->complete,
			  head->frm_total,
			  head->hdr_mode,
			  head->width,
			  head->height,
			  head->bus_fmt);

		tb_state = RKISP_TB_OK;
		if (head->complete != RKISP_TB_OK) {
			head->frm_total = 0;
			tb_state = RKISP_TB_NG;
		}

		rkisp_tb_set_state(tb_state);
		rkisp_tb_unprotect_clk();
		rkisp_register_irq(isp_dev->hw_dev);
		isp_dev->hw_dev->is_thunderboot = false;
	}
}
#endif

/****************  Interrupter Handler ****************/

void rkisp_mipi_isr(unsigned int mis, struct rkisp_device *dev)
{
	struct v4l2_device *v4l2_dev = &dev->v4l2_dev;
	void __iomem *base = dev->base_addr;
	u32 val;

	v4l2_dbg(3, rkisp_debug, &dev->v4l2_dev,
		 "mipi isr:0x%x\n", mis);

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

void rkisp_mipi_v13_isr(unsigned int err1, unsigned int err2,
			 unsigned int err3, struct rkisp_device *dev)
{
	struct v4l2_device *v4l2_dev = &dev->v4l2_dev;
	void __iomem *base = dev->base_addr;
	u32 val, mask;

	v4l2_dbg(3, rkisp_debug, &dev->v4l2_dev,
		 "mipi isr err1:0x%x err2:0x%x err3:0x%x\n",
		 err1, err2, err3);

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

void rkisp_isp_isr(unsigned int isp_mis,
		   unsigned int isp3a_mis,
		   struct rkisp_device *dev)
{
	void __iomem *base = dev->base_addr;
	unsigned int isp_mis_tmp = 0;
	unsigned int isp_err = 0;
	u32 si3a_isr_mask = ISP2X_SIAWB_DONE | ISP2X_SIAF_FIN |
		ISP2X_YUVAE_END | ISP2X_SIHST_RDY;
	u32 raw3a_isr_mask = ISP2X_3A_RAWAE_BIG | ISP2X_3A_RAWAE_CH0 |
		ISP2X_3A_RAWAE_CH1 | ISP2X_3A_RAWAE_CH2 |
		ISP2X_3A_RAWHIST_BIG | ISP2X_3A_RAWHIST_CH0 |
		ISP2X_3A_RAWHIST_CH1 | ISP2X_3A_RAWHIST_CH2 |
		ISP2X_3A_RAWAF_SUM | ISP2X_3A_RAWAF_LUM |
		ISP2X_3A_RAWAF | ISP2X_3A_RAWAWB;

	/*
	 * The last time that rx perform 'back read' don't clear done flag
	 * in advance, otherwise the statistics will be abnormal.
	 */
	if (isp3a_mis & ISP2X_3A_RAWAE_BIG && dev->params_vdev.rdbk_times > 0)
		writel(BIT(31), base + RAWAE_BIG1_BASE + RAWAE_BIG_CTRL);

	v4l2_dbg(3, rkisp_debug, &dev->v4l2_dev,
		 "isp isr:0x%x, 0x%x\n", isp_mis, isp3a_mis);
	dev->isp_isr_cnt++;
	/* start edge of v_sync */
	if (isp_mis & CIF_ISP_V_START) {
		if (!(dev->isp_state & ISP_FRAME_VS))
			dev->isp_sdev.dbg.timestamp = ktime_get_ns();
		rkisp_set_state(dev, ISP_FRAME_VS);
		/* last vsync to config next buf */
		if (!dev->csi_dev.filt_state[CSI_F_VS])
			rkisp_bridge_update_mi(dev);
		else
			dev->csi_dev.filt_state[CSI_F_VS]--;
		if (IS_HDR_RDBK(dev->hdr.op_mode)) {
			/* read 3d lut at isp readback */
			if (!dev->hw_dev->is_single)
				rkisp_write(dev, ISP_3DLUT_UPDATE, 0, true);
			rkisp_stats_rdbk_enable(&dev->stats_vdev, true);
			goto vs_skip;
		}
		if (dev->cap_dev.stream[RKISP_STREAM_SP].interlaced) {
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
					dev->cap_dev.stream[RKISP_STREAM_SP].u.sp.field =
						(readl(addr) >> 16) % 2;
			} else {
				dev->cap_dev.stream[RKISP_STREAM_SP].u.sp.field =
					(readl(base + CIF_ISP_FLAGS_SHD) >> 2) & BIT(0);
			}
		}

		if (dev->vs_irq < 0) {
			dev->isp_sdev.frm_timestamp = ktime_get_ns();
			rkisp_isp_queue_event_sof(&dev->isp_sdev);
		}
vs_skip:
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

		if (dev->isp_err_cnt++ > RKISP_CONTI_ERR_MAX) {
			rkisp_isp_stop(dev);
			rkisp_set_state(dev, ISP_ERROR);
			v4l2_err(&dev->v4l2_dev,
				 "Too many isp error, stop isp!\n");
		}
	}

	if (isp_mis & ISP2X_LSC_LUT_ERR) {
		writel(ISP2X_LSC_LUT_ERR, base + CIF_ISP_ICR);

		isp_err = readl(base + CIF_ISP_ERR);
		v4l2_err(&dev->v4l2_dev,
			"ISP2X_LSC_LUT_ERR. ISP_ERR 0x%x\n", isp_err);
		writel(isp_err, base + CIF_ISP_ERR_CLR);
	}

	/* sampled input frame is complete */
	if (isp_mis & CIF_ISP_FRAME_IN) {
		rkisp_set_state(dev, ISP_FRAME_IN);
		writel(CIF_ISP_FRAME_IN, base + CIF_ISP_ICR);
		isp_mis_tmp = readl(base + CIF_ISP_MIS);
		if (isp_mis_tmp & CIF_ISP_FRAME_IN)
			v4l2_err(&dev->v4l2_dev, "isp icr frame_in err: 0x%x\n",
				 isp_mis_tmp);

		dev->isp_err_cnt = 0;
	}

	/* frame was completely put out */
	if (isp_mis & CIF_ISP_FRAME) {
		dev->isp_sdev.dbg.interval =
			ktime_get_ns() - dev->isp_sdev.dbg.timestamp;
		/* Clear Frame In (ISP) */
		rkisp_set_state(dev, ISP_FRAME_END);
		writel(CIF_ISP_FRAME, base + CIF_ISP_ICR);
		isp_mis_tmp = readl(base + CIF_ISP_MIS);
		if (isp_mis_tmp & CIF_ISP_FRAME)
			v4l2_err(&dev->v4l2_dev,
				 "isp icr frame end err: 0x%x\n", isp_mis_tmp);
		rkisp_dmarx_get_frame(dev, &dev->isp_sdev.dbg.id, NULL, NULL, true);
		rkisp_isp_read_add_fifo_data(dev);
	}

	if ((isp_mis & (CIF_ISP_FRAME | si3a_isr_mask)) ||
	    (isp3a_mis & raw3a_isr_mask)) {
		u32 irq = isp_mis;

		/* FRAME to get EXP and HIST together */
		if (isp_mis & CIF_ISP_FRAME)
			irq |= ((CIF_ISP_EXP_END |
				CIF_ISP_HIST_MEASURE_RDY) &
				readl(base + CIF_ISP_RIS));

		rkisp_stats_isr(&dev->stats_vdev, irq, isp3a_mis);

		if ((isp_mis & CIF_ISP_FRAME) && dev->stats_vdev.rdbk_mode)
			rkisp_stats_rdbk_enable(&dev->stats_vdev, false);
	}

	/*
	 * Then update changed configs. Some of them involve
	 * lot of register writes. Do those only one per frame.
	 * Do the updates in the order of the processing flow.
	 */
	rkisp_params_isr(&dev->params_vdev, isp_mis);

	if (isp_mis & CIF_ISP_FRAME_IN)
		rkisp_check_idle(dev, ISP_FRAME_IN);
	if (isp_mis & CIF_ISP_FRAME)
		rkisp_check_idle(dev, ISP_FRAME_END);
}

irqreturn_t rkisp_vs_isr_handler(int irq, void *ctx)
{
	struct device *dev = ctx;
	struct rkisp_device *rkisp_dev = dev_get_drvdata(dev);

	if (rkisp_dev->vs_irq >= 0)
		rkisp_isp_queue_event_sof(&rkisp_dev->isp_sdev);

	return IRQ_HANDLED;
}

