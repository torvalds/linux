// SPDX-License-Identifier: GPL-2.0-only
/*
 * Microchip Image Sensor Controller (ISC) common driver base
 *
 * Copyright (C) 2016-2019 Microchip Technology, Inc.
 *
 * Author: Songjun Wu
 * Author: Eugen Hristev <eugen.hristev@microchip.com>
 *
 */
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/videodev2.h>
#include <linux/atmel-isc-media.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-image-sizes.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf2-dma-contig.h>

#include "microchip-isc-regs.h"
#include "microchip-isc.h"

#define ISC_IS_FORMAT_RAW(mbus_code) \
	(((mbus_code) & 0xf000) == 0x3000)

#define ISC_IS_FORMAT_GREY(mbus_code) \
	(((mbus_code) == MEDIA_BUS_FMT_Y10_1X10) | \
	(((mbus_code) == MEDIA_BUS_FMT_Y8_1X8)))

static inline void isc_update_v4l2_ctrls(struct isc_device *isc)
{
	struct isc_ctrls *ctrls = &isc->ctrls;

	/* In here we set the v4l2 controls w.r.t. our pipeline config */
	v4l2_ctrl_s_ctrl(isc->r_gain_ctrl, ctrls->gain[ISC_HIS_CFG_MODE_R]);
	v4l2_ctrl_s_ctrl(isc->b_gain_ctrl, ctrls->gain[ISC_HIS_CFG_MODE_B]);
	v4l2_ctrl_s_ctrl(isc->gr_gain_ctrl, ctrls->gain[ISC_HIS_CFG_MODE_GR]);
	v4l2_ctrl_s_ctrl(isc->gb_gain_ctrl, ctrls->gain[ISC_HIS_CFG_MODE_GB]);

	v4l2_ctrl_s_ctrl(isc->r_off_ctrl, ctrls->offset[ISC_HIS_CFG_MODE_R]);
	v4l2_ctrl_s_ctrl(isc->b_off_ctrl, ctrls->offset[ISC_HIS_CFG_MODE_B]);
	v4l2_ctrl_s_ctrl(isc->gr_off_ctrl, ctrls->offset[ISC_HIS_CFG_MODE_GR]);
	v4l2_ctrl_s_ctrl(isc->gb_off_ctrl, ctrls->offset[ISC_HIS_CFG_MODE_GB]);
}

static inline void isc_update_awb_ctrls(struct isc_device *isc)
{
	struct isc_ctrls *ctrls = &isc->ctrls;

	/* In here we set our actual hw pipeline config */

	regmap_write(isc->regmap, ISC_WB_O_RGR,
		     ((ctrls->offset[ISC_HIS_CFG_MODE_R])) |
		     ((ctrls->offset[ISC_HIS_CFG_MODE_GR]) << 16));
	regmap_write(isc->regmap, ISC_WB_O_BGB,
		     ((ctrls->offset[ISC_HIS_CFG_MODE_B])) |
		     ((ctrls->offset[ISC_HIS_CFG_MODE_GB]) << 16));
	regmap_write(isc->regmap, ISC_WB_G_RGR,
		     ctrls->gain[ISC_HIS_CFG_MODE_R] |
		     (ctrls->gain[ISC_HIS_CFG_MODE_GR] << 16));
	regmap_write(isc->regmap, ISC_WB_G_BGB,
		     ctrls->gain[ISC_HIS_CFG_MODE_B] |
		     (ctrls->gain[ISC_HIS_CFG_MODE_GB] << 16));
}

static inline void isc_reset_awb_ctrls(struct isc_device *isc)
{
	unsigned int c;

	for (c = ISC_HIS_CFG_MODE_GR; c <= ISC_HIS_CFG_MODE_B; c++) {
		/* gains have a fixed point at 9 decimals */
		isc->ctrls.gain[c] = 1 << 9;
		/* offsets are in 2's complements */
		isc->ctrls.offset[c] = 0;
	}
}

static int isc_queue_setup(struct vb2_queue *vq,
			   unsigned int *nbuffers, unsigned int *nplanes,
			   unsigned int sizes[], struct device *alloc_devs[])
{
	struct isc_device *isc = vb2_get_drv_priv(vq);
	unsigned int size = isc->fmt.fmt.pix.sizeimage;

	if (*nplanes)
		return sizes[0] < size ? -EINVAL : 0;

	*nplanes = 1;
	sizes[0] = size;

	return 0;
}

static int isc_buffer_prepare(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct isc_device *isc = vb2_get_drv_priv(vb->vb2_queue);
	unsigned long size = isc->fmt.fmt.pix.sizeimage;

	if (vb2_plane_size(vb, 0) < size) {
		dev_err(isc->dev, "buffer too small (%lu < %lu)\n",
			vb2_plane_size(vb, 0), size);
		return -EINVAL;
	}

	vb2_set_plane_payload(vb, 0, size);

	vbuf->field = isc->fmt.fmt.pix.field;

	return 0;
}

static void isc_crop_pfe(struct isc_device *isc)
{
	struct regmap *regmap = isc->regmap;
	u32 h, w;

	h = isc->fmt.fmt.pix.height;
	w = isc->fmt.fmt.pix.width;

	/*
	 * In case the sensor is not RAW, it will output a pixel (12-16 bits)
	 * with two samples on the ISC Data bus (which is 8-12)
	 * ISC will count each sample, so, we need to multiply these values
	 * by two, to get the real number of samples for the required pixels.
	 */
	if (!ISC_IS_FORMAT_RAW(isc->config.sd_format->mbus_code)) {
		h <<= 1;
		w <<= 1;
	}

	/*
	 * We limit the column/row count that the ISC will output according
	 * to the configured resolution that we want.
	 * This will avoid the situation where the sensor is misconfigured,
	 * sending more data, and the ISC will just take it and DMA to memory,
	 * causing corruption.
	 */
	regmap_write(regmap, ISC_PFE_CFG1,
		     (ISC_PFE_CFG1_COLMIN(0) & ISC_PFE_CFG1_COLMIN_MASK) |
		     (ISC_PFE_CFG1_COLMAX(w - 1) & ISC_PFE_CFG1_COLMAX_MASK));

	regmap_write(regmap, ISC_PFE_CFG2,
		     (ISC_PFE_CFG2_ROWMIN(0) & ISC_PFE_CFG2_ROWMIN_MASK) |
		     (ISC_PFE_CFG2_ROWMAX(h - 1) & ISC_PFE_CFG2_ROWMAX_MASK));

	regmap_update_bits(regmap, ISC_PFE_CFG0,
			   ISC_PFE_CFG0_COLEN | ISC_PFE_CFG0_ROWEN,
			   ISC_PFE_CFG0_COLEN | ISC_PFE_CFG0_ROWEN);
}

static void isc_start_dma(struct isc_device *isc)
{
	struct regmap *regmap = isc->regmap;
	u32 sizeimage = isc->fmt.fmt.pix.sizeimage;
	u32 dctrl_dview;
	dma_addr_t addr0;

	addr0 = vb2_dma_contig_plane_dma_addr(&isc->cur_frm->vb.vb2_buf, 0);
	regmap_write(regmap, ISC_DAD0 + isc->offsets.dma, addr0);

	switch (isc->config.fourcc) {
	case V4L2_PIX_FMT_YUV420:
		regmap_write(regmap, ISC_DAD1 + isc->offsets.dma,
			     addr0 + (sizeimage * 2) / 3);
		regmap_write(regmap, ISC_DAD2 + isc->offsets.dma,
			     addr0 + (sizeimage * 5) / 6);
		break;
	case V4L2_PIX_FMT_YUV422P:
		regmap_write(regmap, ISC_DAD1 + isc->offsets.dma,
			     addr0 + sizeimage / 2);
		regmap_write(regmap, ISC_DAD2 + isc->offsets.dma,
			     addr0 + (sizeimage * 3) / 4);
		break;
	default:
		break;
	}

	dctrl_dview = isc->config.dctrl_dview;

	regmap_write(regmap, ISC_DCTRL + isc->offsets.dma,
		     dctrl_dview | ISC_DCTRL_IE_IS);
	spin_lock(&isc->awb_lock);
	regmap_write(regmap, ISC_CTRLEN, ISC_CTRL_CAPTURE);
	spin_unlock(&isc->awb_lock);
}

static void isc_set_pipeline(struct isc_device *isc, u32 pipeline)
{
	struct regmap *regmap = isc->regmap;
	struct isc_ctrls *ctrls = &isc->ctrls;
	u32 val, bay_cfg;
	const u32 *gamma;
	unsigned int i;

	/* WB-->CFA-->CC-->GAM-->CSC-->CBC-->SUB422-->SUB420 */
	for (i = 0; i < ISC_PIPE_LINE_NODE_NUM; i++) {
		val = pipeline & BIT(i) ? 1 : 0;
		regmap_field_write(isc->pipeline[i], val);
	}

	if (!pipeline)
		return;

	bay_cfg = isc->config.sd_format->cfa_baycfg;

	regmap_write(regmap, ISC_WB_CFG, bay_cfg);
	isc_update_awb_ctrls(isc);
	isc_update_v4l2_ctrls(isc);

	regmap_write(regmap, ISC_CFA_CFG, bay_cfg | ISC_CFA_CFG_EITPOL);

	gamma = &isc->gamma_table[ctrls->gamma_index][0];
	regmap_bulk_write(regmap, ISC_GAM_BENTRY, gamma, GAMMA_ENTRIES);
	regmap_bulk_write(regmap, ISC_GAM_GENTRY, gamma, GAMMA_ENTRIES);
	regmap_bulk_write(regmap, ISC_GAM_RENTRY, gamma, GAMMA_ENTRIES);

	isc->config_dpc(isc);
	isc->config_csc(isc);
	isc->config_cbc(isc);
	isc->config_cc(isc);
	isc->config_gam(isc);
}

static int isc_update_profile(struct isc_device *isc)
{
	struct regmap *regmap = isc->regmap;
	u32 sr;
	int counter = 100;

	regmap_write(regmap, ISC_CTRLEN, ISC_CTRL_UPPRO);

	regmap_read(regmap, ISC_CTRLSR, &sr);
	while ((sr & ISC_CTRL_UPPRO) && counter--) {
		usleep_range(1000, 2000);
		regmap_read(regmap, ISC_CTRLSR, &sr);
	}

	if (counter < 0) {
		v4l2_warn(&isc->v4l2_dev, "Time out to update profile\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static void isc_set_histogram(struct isc_device *isc, bool enable)
{
	struct regmap *regmap = isc->regmap;
	struct isc_ctrls *ctrls = &isc->ctrls;

	if (enable) {
		regmap_write(regmap, ISC_HIS_CFG + isc->offsets.his,
			     ISC_HIS_CFG_MODE_GR |
			     (isc->config.sd_format->cfa_baycfg
					<< ISC_HIS_CFG_BAYSEL_SHIFT) |
					ISC_HIS_CFG_RAR);
		regmap_write(regmap, ISC_HIS_CTRL + isc->offsets.his,
			     ISC_HIS_CTRL_EN);
		regmap_write(regmap, ISC_INTEN, ISC_INT_HISDONE);
		ctrls->hist_id = ISC_HIS_CFG_MODE_GR;
		isc_update_profile(isc);
		regmap_write(regmap, ISC_CTRLEN, ISC_CTRL_HISREQ);

		ctrls->hist_stat = HIST_ENABLED;
	} else {
		regmap_write(regmap, ISC_INTDIS, ISC_INT_HISDONE);
		regmap_write(regmap, ISC_HIS_CTRL + isc->offsets.his,
			     ISC_HIS_CTRL_DIS);

		ctrls->hist_stat = HIST_DISABLED;
	}
}

static int isc_configure(struct isc_device *isc)
{
	struct regmap *regmap = isc->regmap;
	u32 pfe_cfg0, dcfg, mask, pipeline;
	struct isc_subdev_entity *subdev = isc->current_subdev;

	pfe_cfg0 = isc->config.sd_format->pfe_cfg0_bps;
	pipeline = isc->config.bits_pipeline;

	dcfg = isc->config.dcfg_imode | isc->dcfg;

	pfe_cfg0  |= subdev->pfe_cfg0 | ISC_PFE_CFG0_MODE_PROGRESSIVE;
	mask = ISC_PFE_CFG0_BPS_MASK | ISC_PFE_CFG0_HPOL_LOW |
	       ISC_PFE_CFG0_VPOL_LOW | ISC_PFE_CFG0_PPOL_LOW |
	       ISC_PFE_CFG0_MODE_MASK | ISC_PFE_CFG0_CCIR_CRC |
	       ISC_PFE_CFG0_CCIR656 | ISC_PFE_CFG0_MIPI;

	regmap_update_bits(regmap, ISC_PFE_CFG0, mask, pfe_cfg0);

	isc->config_rlp(isc);

	regmap_write(regmap, ISC_DCFG + isc->offsets.dma, dcfg);

	/* Set the pipeline */
	isc_set_pipeline(isc, pipeline);

	/*
	 * The current implemented histogram is available for RAW R, B, GB, GR
	 * channels. We need to check if sensor is outputting RAW BAYER
	 */
	if (isc->ctrls.awb &&
	    ISC_IS_FORMAT_RAW(isc->config.sd_format->mbus_code))
		isc_set_histogram(isc, true);
	else
		isc_set_histogram(isc, false);

	/* Update profile */
	return isc_update_profile(isc);
}

static int isc_prepare_streaming(struct vb2_queue *vq)
{
	struct isc_device *isc = vb2_get_drv_priv(vq);

	return media_pipeline_start(isc->video_dev.entity.pads, &isc->mpipe);
}

static int isc_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct isc_device *isc = vb2_get_drv_priv(vq);
	struct regmap *regmap = isc->regmap;
	struct isc_buffer *buf;
	unsigned long flags;
	int ret;

	/* Enable stream on the sub device */
	ret = v4l2_subdev_call(isc->current_subdev->sd, video, s_stream, 1);
	if (ret && ret != -ENOIOCTLCMD) {
		dev_err(isc->dev, "stream on failed in subdev %d\n", ret);
		goto err_start_stream;
	}

	ret = pm_runtime_resume_and_get(isc->dev);
	if (ret < 0) {
		dev_err(isc->dev, "RPM resume failed in subdev %d\n",
			ret);
		goto err_pm_get;
	}

	ret = isc_configure(isc);
	if (unlikely(ret))
		goto err_configure;

	/* Enable DMA interrupt */
	regmap_write(regmap, ISC_INTEN, ISC_INT_DDONE);

	spin_lock_irqsave(&isc->dma_queue_lock, flags);

	isc->sequence = 0;
	isc->stop = false;
	reinit_completion(&isc->comp);

	isc->cur_frm = list_first_entry(&isc->dma_queue,
					struct isc_buffer, list);
	list_del(&isc->cur_frm->list);

	isc_crop_pfe(isc);
	isc_start_dma(isc);

	spin_unlock_irqrestore(&isc->dma_queue_lock, flags);

	/* if we streaming from RAW, we can do one-shot white balance adj */
	if (ISC_IS_FORMAT_RAW(isc->config.sd_format->mbus_code))
		v4l2_ctrl_activate(isc->do_wb_ctrl, true);

	return 0;

err_configure:
	pm_runtime_put_sync(isc->dev);
err_pm_get:
	v4l2_subdev_call(isc->current_subdev->sd, video, s_stream, 0);

err_start_stream:
	spin_lock_irqsave(&isc->dma_queue_lock, flags);
	list_for_each_entry(buf, &isc->dma_queue, list)
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_QUEUED);
	INIT_LIST_HEAD(&isc->dma_queue);
	spin_unlock_irqrestore(&isc->dma_queue_lock, flags);

	return ret;
}

static void isc_unprepare_streaming(struct vb2_queue *vq)
{
	struct isc_device *isc = vb2_get_drv_priv(vq);

	/* Stop media pipeline */
	media_pipeline_stop(isc->video_dev.entity.pads);
}

static void isc_stop_streaming(struct vb2_queue *vq)
{
	struct isc_device *isc = vb2_get_drv_priv(vq);
	unsigned long flags;
	struct isc_buffer *buf;
	int ret;

	mutex_lock(&isc->awb_mutex);
	v4l2_ctrl_activate(isc->do_wb_ctrl, false);

	isc->stop = true;

	/* Wait until the end of the current frame */
	if (isc->cur_frm && !wait_for_completion_timeout(&isc->comp, 5 * HZ))
		dev_err(isc->dev, "Timeout waiting for end of the capture\n");

	mutex_unlock(&isc->awb_mutex);

	/* Disable DMA interrupt */
	regmap_write(isc->regmap, ISC_INTDIS, ISC_INT_DDONE);

	pm_runtime_put_sync(isc->dev);

	/* Disable stream on the sub device */
	ret = v4l2_subdev_call(isc->current_subdev->sd, video, s_stream, 0);
	if (ret && ret != -ENOIOCTLCMD)
		dev_err(isc->dev, "stream off failed in subdev\n");

	/* Release all active buffers */
	spin_lock_irqsave(&isc->dma_queue_lock, flags);
	if (unlikely(isc->cur_frm)) {
		vb2_buffer_done(&isc->cur_frm->vb.vb2_buf,
				VB2_BUF_STATE_ERROR);
		isc->cur_frm = NULL;
	}
	list_for_each_entry(buf, &isc->dma_queue, list)
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
	INIT_LIST_HEAD(&isc->dma_queue);
	spin_unlock_irqrestore(&isc->dma_queue_lock, flags);
}

static void isc_buffer_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct isc_buffer *buf = container_of(vbuf, struct isc_buffer, vb);
	struct isc_device *isc = vb2_get_drv_priv(vb->vb2_queue);
	unsigned long flags;

	spin_lock_irqsave(&isc->dma_queue_lock, flags);
	if (!isc->cur_frm && list_empty(&isc->dma_queue) &&
	    vb2_start_streaming_called(vb->vb2_queue)) {
		isc->cur_frm = buf;
		isc_start_dma(isc);
	} else {
		list_add_tail(&buf->list, &isc->dma_queue);
	}
	spin_unlock_irqrestore(&isc->dma_queue_lock, flags);
}

static const struct vb2_ops isc_vb2_ops = {
	.queue_setup		= isc_queue_setup,
	.wait_prepare		= vb2_ops_wait_prepare,
	.wait_finish		= vb2_ops_wait_finish,
	.buf_prepare		= isc_buffer_prepare,
	.start_streaming	= isc_start_streaming,
	.stop_streaming		= isc_stop_streaming,
	.buf_queue		= isc_buffer_queue,
	.prepare_streaming	= isc_prepare_streaming,
	.unprepare_streaming	= isc_unprepare_streaming,
};

static int isc_querycap(struct file *file, void *priv,
			struct v4l2_capability *cap)
{
	struct isc_device *isc = video_drvdata(file);

	strscpy(cap->driver, "microchip-isc", sizeof(cap->driver));
	strscpy(cap->card, "Microchip Image Sensor Controller", sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info),
		 "platform:%s", isc->v4l2_dev.name);

	return 0;
}

static int isc_enum_fmt_vid_cap(struct file *file, void *priv,
				struct v4l2_fmtdesc *f)
{
	struct isc_device *isc = video_drvdata(file);
	u32 index = f->index;
	u32 i, supported_index = 0;
	struct isc_format *fmt;

	/*
	 * If we are not asked a specific mbus_code, we have to report all
	 * the formats that we can output.
	 */
	if (!f->mbus_code) {
		if (index >= isc->controller_formats_size)
			return -EINVAL;

		f->pixelformat = isc->controller_formats[index].fourcc;

		return 0;
	}

	/*
	 * If a specific mbus_code is requested, check if we support
	 * this mbus_code as input for the ISC.
	 * If it's supported, then we report the corresponding pixelformat
	 * as first possible option for the ISC.
	 * E.g. mbus MEDIA_BUS_FMT_YUYV8_2X8 and report
	 * 'YUYV' (YUYV 4:2:2)
	 */
	fmt = isc_find_format_by_code(isc, f->mbus_code, &i);
	if (!fmt)
		return -EINVAL;

	if (!index) {
		f->pixelformat = fmt->fourcc;

		return 0;
	}

	supported_index++;

	/* If the index is not raw, we don't have anymore formats to report */
	if (!ISC_IS_FORMAT_RAW(f->mbus_code))
		return -EINVAL;

	/*
	 * We are asked for a specific mbus code, which is raw.
	 * We have to search through the formats we can convert to.
	 * We have to skip the raw formats, we cannot convert to raw.
	 * E.g. 'AR12' (16-bit ARGB 4-4-4-4), 'AR15' (16-bit ARGB 1-5-5-5), etc.
	 */
	for (i = 0; i < isc->controller_formats_size; i++) {
		if (isc->controller_formats[i].raw)
			continue;
		if (index == supported_index) {
			f->pixelformat = isc->controller_formats[i].fourcc;
			return 0;
		}
		supported_index++;
	}

	return -EINVAL;
}

static int isc_g_fmt_vid_cap(struct file *file, void *priv,
			     struct v4l2_format *fmt)
{
	struct isc_device *isc = video_drvdata(file);

	*fmt = isc->fmt;

	return 0;
}

/*
 * Checks the current configured format, if ISC can output it,
 * considering which type of format the ISC receives from the sensor
 */
static int isc_try_validate_formats(struct isc_device *isc)
{
	int ret;
	bool bayer = false, yuv = false, rgb = false, grey = false;

	/* all formats supported by the RLP module are OK */
	switch (isc->try_config.fourcc) {
	case V4L2_PIX_FMT_SBGGR8:
	case V4L2_PIX_FMT_SGBRG8:
	case V4L2_PIX_FMT_SGRBG8:
	case V4L2_PIX_FMT_SRGGB8:
	case V4L2_PIX_FMT_SBGGR10:
	case V4L2_PIX_FMT_SGBRG10:
	case V4L2_PIX_FMT_SGRBG10:
	case V4L2_PIX_FMT_SRGGB10:
	case V4L2_PIX_FMT_SBGGR12:
	case V4L2_PIX_FMT_SGBRG12:
	case V4L2_PIX_FMT_SGRBG12:
	case V4L2_PIX_FMT_SRGGB12:
		ret = 0;
		bayer = true;
		break;

	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_YUV422P:
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_UYVY:
	case V4L2_PIX_FMT_VYUY:
		ret = 0;
		yuv = true;
		break;

	case V4L2_PIX_FMT_RGB565:
	case V4L2_PIX_FMT_ABGR32:
	case V4L2_PIX_FMT_XBGR32:
	case V4L2_PIX_FMT_ARGB444:
	case V4L2_PIX_FMT_ARGB555:
		ret = 0;
		rgb = true;
		break;
	case V4L2_PIX_FMT_GREY:
	case V4L2_PIX_FMT_Y10:
	case V4L2_PIX_FMT_Y16:
		ret = 0;
		grey = true;
		break;
	default:
	/* any other different formats are not supported */
		dev_err(isc->dev, "Requested unsupported format.\n");
		ret = -EINVAL;
	}
	dev_dbg(isc->dev,
		"Format validation, requested rgb=%u, yuv=%u, grey=%u, bayer=%u\n",
		rgb, yuv, grey, bayer);

	if (bayer &&
	    !ISC_IS_FORMAT_RAW(isc->try_config.sd_format->mbus_code)) {
		dev_err(isc->dev, "Cannot output RAW if we do not receive RAW.\n");
		return -EINVAL;
	}

	if (grey && !ISC_IS_FORMAT_RAW(isc->try_config.sd_format->mbus_code) &&
	    !ISC_IS_FORMAT_GREY(isc->try_config.sd_format->mbus_code)) {
		dev_err(isc->dev, "Cannot output GREY if we do not receive RAW/GREY.\n");
		return -EINVAL;
	}

	if ((rgb || bayer || yuv) &&
	    ISC_IS_FORMAT_GREY(isc->try_config.sd_format->mbus_code)) {
		dev_err(isc->dev, "Cannot convert GREY to another format.\n");
		return -EINVAL;
	}

	return ret;
}

/*
 * Configures the RLP and DMA modules, depending on the output format
 * configured for the ISC.
 * If direct_dump == true, just dump raw data 8/16 bits depending on format.
 */
static int isc_try_configure_rlp_dma(struct isc_device *isc, bool direct_dump)
{
	isc->try_config.rlp_cfg_mode = 0;

	switch (isc->try_config.fourcc) {
	case V4L2_PIX_FMT_SBGGR8:
	case V4L2_PIX_FMT_SGBRG8:
	case V4L2_PIX_FMT_SGRBG8:
	case V4L2_PIX_FMT_SRGGB8:
		isc->try_config.rlp_cfg_mode = ISC_RLP_CFG_MODE_DAT8;
		isc->try_config.dcfg_imode = ISC_DCFG_IMODE_PACKED8;
		isc->try_config.dctrl_dview = ISC_DCTRL_DVIEW_PACKED;
		isc->try_config.bpp = 8;
		isc->try_config.bpp_v4l2 = 8;
		break;
	case V4L2_PIX_FMT_SBGGR10:
	case V4L2_PIX_FMT_SGBRG10:
	case V4L2_PIX_FMT_SGRBG10:
	case V4L2_PIX_FMT_SRGGB10:
		isc->try_config.rlp_cfg_mode = ISC_RLP_CFG_MODE_DAT10;
		isc->try_config.dcfg_imode = ISC_DCFG_IMODE_PACKED16;
		isc->try_config.dctrl_dview = ISC_DCTRL_DVIEW_PACKED;
		isc->try_config.bpp = 16;
		isc->try_config.bpp_v4l2 = 16;
		break;
	case V4L2_PIX_FMT_SBGGR12:
	case V4L2_PIX_FMT_SGBRG12:
	case V4L2_PIX_FMT_SGRBG12:
	case V4L2_PIX_FMT_SRGGB12:
		isc->try_config.rlp_cfg_mode = ISC_RLP_CFG_MODE_DAT12;
		isc->try_config.dcfg_imode = ISC_DCFG_IMODE_PACKED16;
		isc->try_config.dctrl_dview = ISC_DCTRL_DVIEW_PACKED;
		isc->try_config.bpp = 16;
		isc->try_config.bpp_v4l2 = 16;
		break;
	case V4L2_PIX_FMT_RGB565:
		isc->try_config.rlp_cfg_mode = ISC_RLP_CFG_MODE_RGB565;
		isc->try_config.dcfg_imode = ISC_DCFG_IMODE_PACKED16;
		isc->try_config.dctrl_dview = ISC_DCTRL_DVIEW_PACKED;
		isc->try_config.bpp = 16;
		isc->try_config.bpp_v4l2 = 16;
		break;
	case V4L2_PIX_FMT_ARGB444:
		isc->try_config.rlp_cfg_mode = ISC_RLP_CFG_MODE_ARGB444;
		isc->try_config.dcfg_imode = ISC_DCFG_IMODE_PACKED16;
		isc->try_config.dctrl_dview = ISC_DCTRL_DVIEW_PACKED;
		isc->try_config.bpp = 16;
		isc->try_config.bpp_v4l2 = 16;
		break;
	case V4L2_PIX_FMT_ARGB555:
		isc->try_config.rlp_cfg_mode = ISC_RLP_CFG_MODE_ARGB555;
		isc->try_config.dcfg_imode = ISC_DCFG_IMODE_PACKED16;
		isc->try_config.dctrl_dview = ISC_DCTRL_DVIEW_PACKED;
		isc->try_config.bpp = 16;
		isc->try_config.bpp_v4l2 = 16;
		break;
	case V4L2_PIX_FMT_ABGR32:
	case V4L2_PIX_FMT_XBGR32:
		isc->try_config.rlp_cfg_mode = ISC_RLP_CFG_MODE_ARGB32;
		isc->try_config.dcfg_imode = ISC_DCFG_IMODE_PACKED32;
		isc->try_config.dctrl_dview = ISC_DCTRL_DVIEW_PACKED;
		isc->try_config.bpp = 32;
		isc->try_config.bpp_v4l2 = 32;
		break;
	case V4L2_PIX_FMT_YUV420:
		isc->try_config.rlp_cfg_mode = ISC_RLP_CFG_MODE_YYCC;
		isc->try_config.dcfg_imode = ISC_DCFG_IMODE_YC420P;
		isc->try_config.dctrl_dview = ISC_DCTRL_DVIEW_PLANAR;
		isc->try_config.bpp = 12;
		isc->try_config.bpp_v4l2 = 8; /* only first plane */
		break;
	case V4L2_PIX_FMT_YUV422P:
		isc->try_config.rlp_cfg_mode = ISC_RLP_CFG_MODE_YYCC;
		isc->try_config.dcfg_imode = ISC_DCFG_IMODE_YC422P;
		isc->try_config.dctrl_dview = ISC_DCTRL_DVIEW_PLANAR;
		isc->try_config.bpp = 16;
		isc->try_config.bpp_v4l2 = 8; /* only first plane */
		break;
	case V4L2_PIX_FMT_YUYV:
		isc->try_config.rlp_cfg_mode = ISC_RLP_CFG_MODE_YCYC | ISC_RLP_CFG_YMODE_YUYV;
		isc->try_config.dcfg_imode = ISC_DCFG_IMODE_PACKED32;
		isc->try_config.dctrl_dview = ISC_DCTRL_DVIEW_PACKED;
		isc->try_config.bpp = 16;
		isc->try_config.bpp_v4l2 = 16;
		break;
	case V4L2_PIX_FMT_UYVY:
		isc->try_config.rlp_cfg_mode = ISC_RLP_CFG_MODE_YCYC | ISC_RLP_CFG_YMODE_UYVY;
		isc->try_config.dcfg_imode = ISC_DCFG_IMODE_PACKED32;
		isc->try_config.dctrl_dview = ISC_DCTRL_DVIEW_PACKED;
		isc->try_config.bpp = 16;
		isc->try_config.bpp_v4l2 = 16;
		break;
	case V4L2_PIX_FMT_VYUY:
		isc->try_config.rlp_cfg_mode = ISC_RLP_CFG_MODE_YCYC | ISC_RLP_CFG_YMODE_VYUY;
		isc->try_config.dcfg_imode = ISC_DCFG_IMODE_PACKED32;
		isc->try_config.dctrl_dview = ISC_DCTRL_DVIEW_PACKED;
		isc->try_config.bpp = 16;
		isc->try_config.bpp_v4l2 = 16;
		break;
	case V4L2_PIX_FMT_GREY:
		isc->try_config.rlp_cfg_mode = ISC_RLP_CFG_MODE_DATY8;
		isc->try_config.dcfg_imode = ISC_DCFG_IMODE_PACKED8;
		isc->try_config.dctrl_dview = ISC_DCTRL_DVIEW_PACKED;
		isc->try_config.bpp = 8;
		isc->try_config.bpp_v4l2 = 8;
		break;
	case V4L2_PIX_FMT_Y16:
		isc->try_config.rlp_cfg_mode = ISC_RLP_CFG_MODE_DATY10 | ISC_RLP_CFG_LSH;
		fallthrough;
	case V4L2_PIX_FMT_Y10:
		isc->try_config.rlp_cfg_mode |= ISC_RLP_CFG_MODE_DATY10;
		isc->try_config.dcfg_imode = ISC_DCFG_IMODE_PACKED16;
		isc->try_config.dctrl_dview = ISC_DCTRL_DVIEW_PACKED;
		isc->try_config.bpp = 16;
		isc->try_config.bpp_v4l2 = 16;
		break;
	default:
		return -EINVAL;
	}

	if (direct_dump) {
		isc->try_config.rlp_cfg_mode = ISC_RLP_CFG_MODE_DAT8;
		isc->try_config.dcfg_imode = ISC_DCFG_IMODE_PACKED8;
		isc->try_config.dctrl_dview = ISC_DCTRL_DVIEW_PACKED;
		return 0;
	}

	return 0;
}

/*
 * Configuring pipeline modules, depending on which format the ISC outputs
 * and considering which format it has as input from the sensor.
 */
static int isc_try_configure_pipeline(struct isc_device *isc)
{
	switch (isc->try_config.fourcc) {
	case V4L2_PIX_FMT_RGB565:
	case V4L2_PIX_FMT_ARGB555:
	case V4L2_PIX_FMT_ARGB444:
	case V4L2_PIX_FMT_ABGR32:
	case V4L2_PIX_FMT_XBGR32:
		/* if sensor format is RAW, we convert inside ISC */
		if (ISC_IS_FORMAT_RAW(isc->try_config.sd_format->mbus_code)) {
			isc->try_config.bits_pipeline = CFA_ENABLE |
				WB_ENABLE | GAM_ENABLES | DPC_BLCENABLE |
				CC_ENABLE;
		} else {
			isc->try_config.bits_pipeline = 0x0;
		}
		break;
	case V4L2_PIX_FMT_YUV420:
		/* if sensor format is RAW, we convert inside ISC */
		if (ISC_IS_FORMAT_RAW(isc->try_config.sd_format->mbus_code)) {
			isc->try_config.bits_pipeline = CFA_ENABLE |
				CSC_ENABLE | GAM_ENABLES | WB_ENABLE |
				SUB420_ENABLE | SUB422_ENABLE | CBC_ENABLE |
				DPC_BLCENABLE;
		} else {
			isc->try_config.bits_pipeline = 0x0;
		}
		break;
	case V4L2_PIX_FMT_YUV422P:
		/* if sensor format is RAW, we convert inside ISC */
		if (ISC_IS_FORMAT_RAW(isc->try_config.sd_format->mbus_code)) {
			isc->try_config.bits_pipeline = CFA_ENABLE |
				CSC_ENABLE | WB_ENABLE | GAM_ENABLES |
				SUB422_ENABLE | CBC_ENABLE | DPC_BLCENABLE;
		} else {
			isc->try_config.bits_pipeline = 0x0;
		}
		break;
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_UYVY:
	case V4L2_PIX_FMT_VYUY:
		/* if sensor format is RAW, we convert inside ISC */
		if (ISC_IS_FORMAT_RAW(isc->try_config.sd_format->mbus_code)) {
			isc->try_config.bits_pipeline = CFA_ENABLE |
				CSC_ENABLE | WB_ENABLE | GAM_ENABLES |
				SUB422_ENABLE | CBC_ENABLE | DPC_BLCENABLE;
		} else {
			isc->try_config.bits_pipeline = 0x0;
		}
		break;
	case V4L2_PIX_FMT_GREY:
	case V4L2_PIX_FMT_Y16:
		/* if sensor format is RAW, we convert inside ISC */
		if (ISC_IS_FORMAT_RAW(isc->try_config.sd_format->mbus_code)) {
			isc->try_config.bits_pipeline = CFA_ENABLE |
				CSC_ENABLE | WB_ENABLE | GAM_ENABLES |
				CBC_ENABLE | DPC_BLCENABLE;
		} else {
			isc->try_config.bits_pipeline = 0x0;
		}
		break;
	default:
		if (ISC_IS_FORMAT_RAW(isc->try_config.sd_format->mbus_code))
			isc->try_config.bits_pipeline = WB_ENABLE | DPC_BLCENABLE;
		else
			isc->try_config.bits_pipeline = 0x0;
	}

	/* Tune the pipeline to product specific */
	isc->adapt_pipeline(isc);

	return 0;
}

static void isc_try_fse(struct isc_device *isc,
			struct v4l2_subdev_state *sd_state)
{
	int ret;
	struct v4l2_subdev_frame_size_enum fse = {};

	/*
	 * If we do not know yet which format the subdev is using, we cannot
	 * do anything.
	 */
	if (!isc->config.sd_format)
		return;

	fse.code = isc->try_config.sd_format->mbus_code;
	fse.which = V4L2_SUBDEV_FORMAT_TRY;

	ret = v4l2_subdev_call(isc->current_subdev->sd, pad, enum_frame_size,
			       sd_state, &fse);
	/*
	 * Attempt to obtain format size from subdev. If not available,
	 * just use the maximum ISC can receive.
	 */
	if (ret) {
		sd_state->pads->try_crop.width = isc->max_width;
		sd_state->pads->try_crop.height = isc->max_height;
	} else {
		sd_state->pads->try_crop.width = fse.max_width;
		sd_state->pads->try_crop.height = fse.max_height;
	}
}

static int isc_try_fmt(struct isc_device *isc, struct v4l2_format *f)
{
	struct v4l2_pix_format *pixfmt = &f->fmt.pix;
	unsigned int i;

	if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	isc->try_config.fourcc = isc->controller_formats[0].fourcc;

	/* find if the format requested is supported */
	for (i = 0; i < isc->controller_formats_size; i++)
		if (isc->controller_formats[i].fourcc == pixfmt->pixelformat) {
			isc->try_config.fourcc = pixfmt->pixelformat;
			break;
		}

	isc_try_configure_rlp_dma(isc, false);

	/* Limit to Microchip ISC hardware capabilities */
	v4l_bound_align_image(&pixfmt->width, 16, isc->max_width, 0,
			      &pixfmt->height, 16, isc->max_height, 0, 0);
	/* If we did not find the requested format, we will fallback here */
	pixfmt->pixelformat = isc->try_config.fourcc;
	pixfmt->colorspace = V4L2_COLORSPACE_SRGB;
	pixfmt->field = V4L2_FIELD_NONE;

	pixfmt->bytesperline = (pixfmt->width * isc->try_config.bpp_v4l2) >> 3;
	pixfmt->sizeimage = ((pixfmt->width * isc->try_config.bpp) >> 3) *
			     pixfmt->height;

	isc->try_fmt = *f;

	return 0;
}

static int isc_set_fmt(struct isc_device *isc, struct v4l2_format *f)
{
	isc_try_fmt(isc, f);

	/* make the try configuration active */
	isc->config = isc->try_config;
	isc->fmt = isc->try_fmt;

	dev_dbg(isc->dev, "ISC set_fmt to %.4s @%dx%d\n",
		(char *)&f->fmt.pix.pixelformat,
		f->fmt.pix.width, f->fmt.pix.height);

	return 0;
}

static int isc_validate(struct isc_device *isc)
{
	int ret;
	int i;
	struct isc_format *sd_fmt = NULL;
	struct v4l2_pix_format *pixfmt = &isc->fmt.fmt.pix;
	struct v4l2_subdev_format format = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
		.pad = isc->remote_pad,
	};
	struct v4l2_subdev_pad_config pad_cfg = {};
	struct v4l2_subdev_state pad_state = {
		.pads = &pad_cfg,
	};

	/* Get current format from subdev */
	ret = v4l2_subdev_call(isc->current_subdev->sd, pad, get_fmt, NULL,
			       &format);
	if (ret)
		return ret;

	/* Identify the subdev's format configuration */
	for (i = 0; i < isc->formats_list_size; i++)
		if (isc->formats_list[i].mbus_code == format.format.code) {
			sd_fmt = &isc->formats_list[i];
			break;
		}

	/* Check if the format is not supported */
	if (!sd_fmt) {
		dev_err(isc->dev,
			"Current subdevice is streaming a media bus code that is not supported 0x%x\n",
			format.format.code);
		return -EPIPE;
	}

	/* At this moment we know which format the subdev will use */
	isc->try_config.sd_format = sd_fmt;

	/* If the sensor is not RAW, we can only do a direct dump */
	if (!ISC_IS_FORMAT_RAW(isc->try_config.sd_format->mbus_code))
		isc_try_configure_rlp_dma(isc, true);

	/* Limit to Microchip ISC hardware capabilities */
	v4l_bound_align_image(&format.format.width, 16, isc->max_width, 0,
			      &format.format.height, 16, isc->max_height, 0, 0);

	/* Check if the frame size is the same. Otherwise we may overflow */
	if (pixfmt->height != format.format.height ||
	    pixfmt->width != format.format.width) {
		dev_err(isc->dev,
			"ISC not configured with the proper frame size: %dx%d\n",
			format.format.width, format.format.height);
		return -EPIPE;
	}

	dev_dbg(isc->dev,
		"Identified subdev using format %.4s with %dx%d %d bpp\n",
		(char *)&sd_fmt->fourcc, pixfmt->width, pixfmt->height,
		isc->try_config.bpp);

	/* Reset and restart AWB if the subdevice changed the format */
	if (isc->try_config.sd_format && isc->config.sd_format &&
	    isc->try_config.sd_format != isc->config.sd_format) {
		isc->ctrls.hist_stat = HIST_INIT;
		isc_reset_awb_ctrls(isc);
		isc_update_v4l2_ctrls(isc);
	}

	/* Validate formats */
	ret = isc_try_validate_formats(isc);
	if (ret)
		return ret;

	/* Obtain frame sizes if possible to have crop requirements ready */
	isc_try_fse(isc, &pad_state);

	/* Configure ISC pipeline for the config */
	ret = isc_try_configure_pipeline(isc);
	if (ret)
		return ret;

	isc->config = isc->try_config;

	dev_dbg(isc->dev, "New ISC configuration in place\n");

	return 0;
}

static int isc_s_fmt_vid_cap(struct file *file, void *priv,
			     struct v4l2_format *f)
{
	struct isc_device *isc = video_drvdata(file);

	if (vb2_is_busy(&isc->vb2_vidq))
		return -EBUSY;

	return isc_set_fmt(isc, f);
}

static int isc_try_fmt_vid_cap(struct file *file, void *priv,
			       struct v4l2_format *f)
{
	struct isc_device *isc = video_drvdata(file);

	return isc_try_fmt(isc, f);
}

static int isc_enum_input(struct file *file, void *priv,
			  struct v4l2_input *inp)
{
	if (inp->index != 0)
		return -EINVAL;

	inp->type = V4L2_INPUT_TYPE_CAMERA;
	inp->std = 0;
	strscpy(inp->name, "Camera", sizeof(inp->name));

	return 0;
}

static int isc_g_input(struct file *file, void *priv, unsigned int *i)
{
	*i = 0;

	return 0;
}

static int isc_s_input(struct file *file, void *priv, unsigned int i)
{
	if (i > 0)
		return -EINVAL;

	return 0;
}

static int isc_g_parm(struct file *file, void *fh, struct v4l2_streamparm *a)
{
	struct isc_device *isc = video_drvdata(file);

	return v4l2_g_parm_cap(video_devdata(file), isc->current_subdev->sd, a);
}

static int isc_s_parm(struct file *file, void *fh, struct v4l2_streamparm *a)
{
	struct isc_device *isc = video_drvdata(file);

	return v4l2_s_parm_cap(video_devdata(file), isc->current_subdev->sd, a);
}

static int isc_enum_framesizes(struct file *file, void *fh,
			       struct v4l2_frmsizeenum *fsize)
{
	struct isc_device *isc = video_drvdata(file);
	int ret = -EINVAL;
	int i;

	if (fsize->index)
		return -EINVAL;

	for (i = 0; i < isc->controller_formats_size; i++)
		if (isc->controller_formats[i].fourcc == fsize->pixel_format)
			ret = 0;

	if (ret)
		return ret;

	fsize->type = V4L2_FRMSIZE_TYPE_CONTINUOUS;

	fsize->stepwise.min_width = 16;
	fsize->stepwise.max_width = isc->max_width;
	fsize->stepwise.min_height = 16;
	fsize->stepwise.max_height = isc->max_height;
	fsize->stepwise.step_width = 1;
	fsize->stepwise.step_height = 1;

	return 0;
}

static const struct v4l2_ioctl_ops isc_ioctl_ops = {
	.vidioc_querycap		= isc_querycap,
	.vidioc_enum_fmt_vid_cap	= isc_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap		= isc_g_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap		= isc_s_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap		= isc_try_fmt_vid_cap,

	.vidioc_enum_input		= isc_enum_input,
	.vidioc_g_input			= isc_g_input,
	.vidioc_s_input			= isc_s_input,

	.vidioc_reqbufs			= vb2_ioctl_reqbufs,
	.vidioc_querybuf		= vb2_ioctl_querybuf,
	.vidioc_qbuf			= vb2_ioctl_qbuf,
	.vidioc_expbuf			= vb2_ioctl_expbuf,
	.vidioc_dqbuf			= vb2_ioctl_dqbuf,
	.vidioc_create_bufs		= vb2_ioctl_create_bufs,
	.vidioc_prepare_buf		= vb2_ioctl_prepare_buf,
	.vidioc_streamon		= vb2_ioctl_streamon,
	.vidioc_streamoff		= vb2_ioctl_streamoff,

	.vidioc_g_parm			= isc_g_parm,
	.vidioc_s_parm			= isc_s_parm,
	.vidioc_enum_framesizes		= isc_enum_framesizes,

	.vidioc_log_status		= v4l2_ctrl_log_status,
	.vidioc_subscribe_event		= v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event	= v4l2_event_unsubscribe,
};

static int isc_open(struct file *file)
{
	struct isc_device *isc = video_drvdata(file);
	struct v4l2_subdev *sd = isc->current_subdev->sd;
	int ret;

	if (mutex_lock_interruptible(&isc->lock))
		return -ERESTARTSYS;

	ret = v4l2_fh_open(file);
	if (ret < 0)
		goto unlock;

	if (!v4l2_fh_is_singular_file(file))
		goto unlock;

	ret = v4l2_subdev_call(sd, core, s_power, 1);
	if (ret < 0 && ret != -ENOIOCTLCMD) {
		v4l2_fh_release(file);
		goto unlock;
	}

	ret = isc_set_fmt(isc, &isc->fmt);
	if (ret) {
		v4l2_subdev_call(sd, core, s_power, 0);
		v4l2_fh_release(file);
	}

unlock:
	mutex_unlock(&isc->lock);
	return ret;
}

static int isc_release(struct file *file)
{
	struct isc_device *isc = video_drvdata(file);
	struct v4l2_subdev *sd = isc->current_subdev->sd;
	bool fh_singular;
	int ret;

	mutex_lock(&isc->lock);

	fh_singular = v4l2_fh_is_singular_file(file);

	ret = _vb2_fop_release(file, NULL);

	if (fh_singular)
		v4l2_subdev_call(sd, core, s_power, 0);

	mutex_unlock(&isc->lock);

	return ret;
}

static const struct v4l2_file_operations isc_fops = {
	.owner		= THIS_MODULE,
	.open		= isc_open,
	.release	= isc_release,
	.unlocked_ioctl	= video_ioctl2,
	.read		= vb2_fop_read,
	.mmap		= vb2_fop_mmap,
	.poll		= vb2_fop_poll,
};

irqreturn_t microchip_isc_interrupt(int irq, void *dev_id)
{
	struct isc_device *isc = (struct isc_device *)dev_id;
	struct regmap *regmap = isc->regmap;
	u32 isc_intsr, isc_intmask, pending;
	irqreturn_t ret = IRQ_NONE;

	regmap_read(regmap, ISC_INTSR, &isc_intsr);
	regmap_read(regmap, ISC_INTMASK, &isc_intmask);

	pending = isc_intsr & isc_intmask;

	if (likely(pending & ISC_INT_DDONE)) {
		spin_lock(&isc->dma_queue_lock);
		if (isc->cur_frm) {
			struct vb2_v4l2_buffer *vbuf = &isc->cur_frm->vb;
			struct vb2_buffer *vb = &vbuf->vb2_buf;

			vb->timestamp = ktime_get_ns();
			vbuf->sequence = isc->sequence++;
			vb2_buffer_done(vb, VB2_BUF_STATE_DONE);
			isc->cur_frm = NULL;
		}

		if (!list_empty(&isc->dma_queue) && !isc->stop) {
			isc->cur_frm = list_first_entry(&isc->dma_queue,
							struct isc_buffer, list);
			list_del(&isc->cur_frm->list);

			isc_start_dma(isc);
		}

		if (isc->stop)
			complete(&isc->comp);

		ret = IRQ_HANDLED;
		spin_unlock(&isc->dma_queue_lock);
	}

	if (pending & ISC_INT_HISDONE) {
		schedule_work(&isc->awb_work);
		ret = IRQ_HANDLED;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(microchip_isc_interrupt);

static void isc_hist_count(struct isc_device *isc, u32 *min, u32 *max)
{
	struct regmap *regmap = isc->regmap;
	struct isc_ctrls *ctrls = &isc->ctrls;
	u32 *hist_count = &ctrls->hist_count[ctrls->hist_id];
	u32 *hist_entry = &ctrls->hist_entry[0];
	u32 i;

	*min = 0;
	*max = HIST_ENTRIES;

	regmap_bulk_read(regmap, ISC_HIS_ENTRY + isc->offsets.his_entry,
			 hist_entry, HIST_ENTRIES);

	*hist_count = 0;
	/*
	 * we deliberately ignore the end of the histogram,
	 * the most white pixels
	 */
	for (i = 1; i < HIST_ENTRIES; i++) {
		if (*hist_entry && !*min)
			*min = i;
		if (*hist_entry)
			*max = i;
		*hist_count += i * (*hist_entry++);
	}

	if (!*min)
		*min = 1;

	dev_dbg(isc->dev, "isc wb: hist_id %u, hist_count %u",
		ctrls->hist_id, *hist_count);
}

static void isc_wb_update(struct isc_ctrls *ctrls)
{
	struct isc_device *isc = container_of(ctrls, struct isc_device, ctrls);
	u32 *hist_count = &ctrls->hist_count[0];
	u32 c, offset[4];
	u64 avg = 0;
	/* We compute two gains, stretch gain and grey world gain */
	u32 s_gain[4], gw_gain[4];

	/*
	 * According to Grey World, we need to set gains for R/B to normalize
	 * them towards the green channel.
	 * Thus we want to keep Green as fixed and adjust only Red/Blue
	 * Compute the average of the both green channels first
	 */
	avg = (u64)hist_count[ISC_HIS_CFG_MODE_GR] +
		(u64)hist_count[ISC_HIS_CFG_MODE_GB];
	avg >>= 1;

	dev_dbg(isc->dev, "isc wb: green components average %llu\n", avg);

	/* Green histogram is null, nothing to do */
	if (!avg)
		return;

	for (c = ISC_HIS_CFG_MODE_GR; c <= ISC_HIS_CFG_MODE_B; c++) {
		/*
		 * the color offset is the minimum value of the histogram.
		 * we stretch this color to the full range by substracting
		 * this value from the color component.
		 */
		offset[c] = ctrls->hist_minmax[c][HIST_MIN_INDEX];
		/*
		 * The offset is always at least 1. If the offset is 1, we do
		 * not need to adjust it, so our result must be zero.
		 * the offset is computed in a histogram on 9 bits (0..512)
		 * but the offset in register is based on
		 * 12 bits pipeline (0..4096).
		 * we need to shift with the 3 bits that the histogram is
		 * ignoring
		 */
		ctrls->offset[c] = (offset[c] - 1) << 3;

		/*
		 * the offset is then taken and converted to 2's complements,
		 * and must be negative, as we subtract this value from the
		 * color components
		 */
		ctrls->offset[c] = -ctrls->offset[c];

		/*
		 * the stretch gain is the total number of histogram bins
		 * divided by the actual range of color component (Max - Min)
		 * If we compute gain like this, the actual color component
		 * will be stretched to the full histogram.
		 * We need to shift 9 bits for precision, we have 9 bits for
		 * decimals
		 */
		s_gain[c] = (HIST_ENTRIES << 9) /
			(ctrls->hist_minmax[c][HIST_MAX_INDEX] -
			ctrls->hist_minmax[c][HIST_MIN_INDEX] + 1);

		/*
		 * Now we have to compute the gain w.r.t. the average.
		 * Add/lose gain to the component towards the average.
		 * If it happens that the component is zero, use the
		 * fixed point value : 1.0 gain.
		 */
		if (hist_count[c])
			gw_gain[c] = div_u64(avg << 9, hist_count[c]);
		else
			gw_gain[c] = 1 << 9;

		dev_dbg(isc->dev,
			"isc wb: component %d, s_gain %u, gw_gain %u\n",
			c, s_gain[c], gw_gain[c]);
		/* multiply both gains and adjust for decimals */
		ctrls->gain[c] = s_gain[c] * gw_gain[c];
		ctrls->gain[c] >>= 9;

		/* make sure we are not out of range */
		ctrls->gain[c] = clamp_val(ctrls->gain[c], 0, GENMASK(12, 0));

		dev_dbg(isc->dev, "isc wb: component %d, final gain %u\n",
			c, ctrls->gain[c]);
	}
}

static void isc_awb_work(struct work_struct *w)
{
	struct isc_device *isc =
		container_of(w, struct isc_device, awb_work);
	struct regmap *regmap = isc->regmap;
	struct isc_ctrls *ctrls = &isc->ctrls;
	u32 hist_id = ctrls->hist_id;
	u32 baysel;
	unsigned long flags;
	u32 min, max;
	int ret;

	if (ctrls->hist_stat != HIST_ENABLED)
		return;

	isc_hist_count(isc, &min, &max);

	dev_dbg(isc->dev,
		"isc wb mode %d: hist min %u , max %u\n", hist_id, min, max);

	ctrls->hist_minmax[hist_id][HIST_MIN_INDEX] = min;
	ctrls->hist_minmax[hist_id][HIST_MAX_INDEX] = max;

	if (hist_id != ISC_HIS_CFG_MODE_B) {
		hist_id++;
	} else {
		isc_wb_update(ctrls);
		hist_id = ISC_HIS_CFG_MODE_GR;
	}

	ctrls->hist_id = hist_id;
	baysel = isc->config.sd_format->cfa_baycfg << ISC_HIS_CFG_BAYSEL_SHIFT;

	ret = pm_runtime_resume_and_get(isc->dev);
	if (ret < 0)
		return;

	/*
	 * only update if we have all the required histograms and controls
	 * if awb has been disabled, we need to reset registers as well.
	 */
	if (hist_id == ISC_HIS_CFG_MODE_GR || ctrls->awb == ISC_WB_NONE) {
		/*
		 * It may happen that DMA Done IRQ will trigger while we are
		 * updating white balance registers here.
		 * In that case, only parts of the controls have been updated.
		 * We can avoid that by locking the section.
		 */
		spin_lock_irqsave(&isc->awb_lock, flags);
		isc_update_awb_ctrls(isc);
		spin_unlock_irqrestore(&isc->awb_lock, flags);

		/*
		 * if we are doing just the one time white balance adjustment,
		 * we are basically done.
		 */
		if (ctrls->awb == ISC_WB_ONETIME) {
			dev_info(isc->dev,
				 "Completed one time white-balance adjustment.\n");
			/* update the v4l2 controls values */
			isc_update_v4l2_ctrls(isc);
			ctrls->awb = ISC_WB_NONE;
		}
	}
	regmap_write(regmap, ISC_HIS_CFG + isc->offsets.his,
		     hist_id | baysel | ISC_HIS_CFG_RAR);

	/*
	 * We have to make sure the streaming has not stopped meanwhile.
	 * ISC requires a frame to clock the internal profile update.
	 * To avoid issues, lock the sequence with a mutex
	 */
	mutex_lock(&isc->awb_mutex);

	/* streaming is not active anymore */
	if (isc->stop) {
		mutex_unlock(&isc->awb_mutex);
		return;
	}

	isc_update_profile(isc);

	mutex_unlock(&isc->awb_mutex);

	/* if awb has been disabled, we don't need to start another histogram */
	if (ctrls->awb)
		regmap_write(regmap, ISC_CTRLEN, ISC_CTRL_HISREQ);

	pm_runtime_put_sync(isc->dev);
}

static int isc_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct isc_device *isc = container_of(ctrl->handler,
					     struct isc_device, ctrls.handler);
	struct isc_ctrls *ctrls = &isc->ctrls;

	if (ctrl->flags & V4L2_CTRL_FLAG_INACTIVE)
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		ctrls->brightness = ctrl->val & ISC_CBC_BRIGHT_MASK;
		break;
	case V4L2_CID_CONTRAST:
		ctrls->contrast = ctrl->val & ISC_CBC_CONTRAST_MASK;
		break;
	case V4L2_CID_GAMMA:
		ctrls->gamma_index = ctrl->val;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct v4l2_ctrl_ops isc_ctrl_ops = {
	.s_ctrl	= isc_s_ctrl,
};

static int isc_s_awb_ctrl(struct v4l2_ctrl *ctrl)
{
	struct isc_device *isc = container_of(ctrl->handler,
					     struct isc_device, ctrls.handler);
	struct isc_ctrls *ctrls = &isc->ctrls;

	if (ctrl->flags & V4L2_CTRL_FLAG_INACTIVE)
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_AUTO_WHITE_BALANCE:
		if (ctrl->val == 1)
			ctrls->awb = ISC_WB_AUTO;
		else
			ctrls->awb = ISC_WB_NONE;

		/* configure the controls with new values from v4l2 */
		if (ctrl->cluster[ISC_CTRL_R_GAIN]->is_new)
			ctrls->gain[ISC_HIS_CFG_MODE_R] = isc->r_gain_ctrl->val;
		if (ctrl->cluster[ISC_CTRL_B_GAIN]->is_new)
			ctrls->gain[ISC_HIS_CFG_MODE_B] = isc->b_gain_ctrl->val;
		if (ctrl->cluster[ISC_CTRL_GR_GAIN]->is_new)
			ctrls->gain[ISC_HIS_CFG_MODE_GR] = isc->gr_gain_ctrl->val;
		if (ctrl->cluster[ISC_CTRL_GB_GAIN]->is_new)
			ctrls->gain[ISC_HIS_CFG_MODE_GB] = isc->gb_gain_ctrl->val;

		if (ctrl->cluster[ISC_CTRL_R_OFF]->is_new)
			ctrls->offset[ISC_HIS_CFG_MODE_R] = isc->r_off_ctrl->val;
		if (ctrl->cluster[ISC_CTRL_B_OFF]->is_new)
			ctrls->offset[ISC_HIS_CFG_MODE_B] = isc->b_off_ctrl->val;
		if (ctrl->cluster[ISC_CTRL_GR_OFF]->is_new)
			ctrls->offset[ISC_HIS_CFG_MODE_GR] = isc->gr_off_ctrl->val;
		if (ctrl->cluster[ISC_CTRL_GB_OFF]->is_new)
			ctrls->offset[ISC_HIS_CFG_MODE_GB] = isc->gb_off_ctrl->val;

		isc_update_awb_ctrls(isc);

		mutex_lock(&isc->awb_mutex);
		if (vb2_is_streaming(&isc->vb2_vidq)) {
			/*
			 * If we are streaming, we can update profile to
			 * have the new settings in place.
			 */
			isc_update_profile(isc);
		} else {
			/*
			 * The auto cluster will activate automatically this
			 * control. This has to be deactivated when not
			 * streaming.
			 */
			v4l2_ctrl_activate(isc->do_wb_ctrl, false);
		}
		mutex_unlock(&isc->awb_mutex);

		/* if we have autowhitebalance on, start histogram procedure */
		if (ctrls->awb == ISC_WB_AUTO &&
		    vb2_is_streaming(&isc->vb2_vidq) &&
		    ISC_IS_FORMAT_RAW(isc->config.sd_format->mbus_code))
			isc_set_histogram(isc, true);

		/*
		 * for one time whitebalance adjustment, check the button,
		 * if it's pressed, perform the one time operation.
		 */
		if (ctrls->awb == ISC_WB_NONE &&
		    ctrl->cluster[ISC_CTRL_DO_WB]->is_new &&
		    !(ctrl->cluster[ISC_CTRL_DO_WB]->flags &
		    V4L2_CTRL_FLAG_INACTIVE)) {
			ctrls->awb = ISC_WB_ONETIME;
			isc_set_histogram(isc, true);
			dev_dbg(isc->dev, "One time white-balance started.\n");
		}
		return 0;
	}
	return 0;
}

static int isc_g_volatile_awb_ctrl(struct v4l2_ctrl *ctrl)
{
	struct isc_device *isc = container_of(ctrl->handler,
					     struct isc_device, ctrls.handler);
	struct isc_ctrls *ctrls = &isc->ctrls;

	switch (ctrl->id) {
	/* being a cluster, this id will be called for every control */
	case V4L2_CID_AUTO_WHITE_BALANCE:
		ctrl->cluster[ISC_CTRL_R_GAIN]->val =
					ctrls->gain[ISC_HIS_CFG_MODE_R];
		ctrl->cluster[ISC_CTRL_B_GAIN]->val =
					ctrls->gain[ISC_HIS_CFG_MODE_B];
		ctrl->cluster[ISC_CTRL_GR_GAIN]->val =
					ctrls->gain[ISC_HIS_CFG_MODE_GR];
		ctrl->cluster[ISC_CTRL_GB_GAIN]->val =
					ctrls->gain[ISC_HIS_CFG_MODE_GB];

		ctrl->cluster[ISC_CTRL_R_OFF]->val =
			ctrls->offset[ISC_HIS_CFG_MODE_R];
		ctrl->cluster[ISC_CTRL_B_OFF]->val =
			ctrls->offset[ISC_HIS_CFG_MODE_B];
		ctrl->cluster[ISC_CTRL_GR_OFF]->val =
			ctrls->offset[ISC_HIS_CFG_MODE_GR];
		ctrl->cluster[ISC_CTRL_GB_OFF]->val =
			ctrls->offset[ISC_HIS_CFG_MODE_GB];
		break;
	}
	return 0;
}

static const struct v4l2_ctrl_ops isc_awb_ops = {
	.s_ctrl = isc_s_awb_ctrl,
	.g_volatile_ctrl = isc_g_volatile_awb_ctrl,
};

#define ISC_CTRL_OFF(_name, _id, _name_str) \
	static const struct v4l2_ctrl_config _name = { \
		.ops = &isc_awb_ops, \
		.id = _id, \
		.name = _name_str, \
		.type = V4L2_CTRL_TYPE_INTEGER, \
		.flags = V4L2_CTRL_FLAG_SLIDER, \
		.min = -4095, \
		.max = 4095, \
		.step = 1, \
		.def = 0, \
	}

ISC_CTRL_OFF(isc_r_off_ctrl, ISC_CID_R_OFFSET, "Red Component Offset");
ISC_CTRL_OFF(isc_b_off_ctrl, ISC_CID_B_OFFSET, "Blue Component Offset");
ISC_CTRL_OFF(isc_gr_off_ctrl, ISC_CID_GR_OFFSET, "Green Red Component Offset");
ISC_CTRL_OFF(isc_gb_off_ctrl, ISC_CID_GB_OFFSET, "Green Blue Component Offset");

#define ISC_CTRL_GAIN(_name, _id, _name_str) \
	static const struct v4l2_ctrl_config _name = { \
		.ops = &isc_awb_ops, \
		.id = _id, \
		.name = _name_str, \
		.type = V4L2_CTRL_TYPE_INTEGER, \
		.flags = V4L2_CTRL_FLAG_SLIDER, \
		.min = 0, \
		.max = 8191, \
		.step = 1, \
		.def = 512, \
	}

ISC_CTRL_GAIN(isc_r_gain_ctrl, ISC_CID_R_GAIN, "Red Component Gain");
ISC_CTRL_GAIN(isc_b_gain_ctrl, ISC_CID_B_GAIN, "Blue Component Gain");
ISC_CTRL_GAIN(isc_gr_gain_ctrl, ISC_CID_GR_GAIN, "Green Red Component Gain");
ISC_CTRL_GAIN(isc_gb_gain_ctrl, ISC_CID_GB_GAIN, "Green Blue Component Gain");

static int isc_ctrl_init(struct isc_device *isc)
{
	const struct v4l2_ctrl_ops *ops = &isc_ctrl_ops;
	struct isc_ctrls *ctrls = &isc->ctrls;
	struct v4l2_ctrl_handler *hdl = &ctrls->handler;
	int ret;

	ctrls->hist_stat = HIST_INIT;
	isc_reset_awb_ctrls(isc);

	ret = v4l2_ctrl_handler_init(hdl, 13);
	if (ret < 0)
		return ret;

	/* Initialize product specific controls. For example, contrast */
	isc->config_ctrls(isc, ops);

	ctrls->brightness = 0;

	v4l2_ctrl_new_std(hdl, ops, V4L2_CID_BRIGHTNESS, -1024, 1023, 1, 0);
	v4l2_ctrl_new_std(hdl, ops, V4L2_CID_GAMMA, 0, isc->gamma_max, 1,
			  isc->gamma_max);
	isc->awb_ctrl = v4l2_ctrl_new_std(hdl, &isc_awb_ops,
					  V4L2_CID_AUTO_WHITE_BALANCE,
					  0, 1, 1, 1);

	/* do_white_balance is a button, so min,max,step,default are ignored */
	isc->do_wb_ctrl = v4l2_ctrl_new_std(hdl, &isc_awb_ops,
					    V4L2_CID_DO_WHITE_BALANCE,
					    0, 0, 0, 0);

	if (!isc->do_wb_ctrl) {
		ret = hdl->error;
		v4l2_ctrl_handler_free(hdl);
		return ret;
	}

	v4l2_ctrl_activate(isc->do_wb_ctrl, false);

	isc->r_gain_ctrl = v4l2_ctrl_new_custom(hdl, &isc_r_gain_ctrl, NULL);
	isc->b_gain_ctrl = v4l2_ctrl_new_custom(hdl, &isc_b_gain_ctrl, NULL);
	isc->gr_gain_ctrl = v4l2_ctrl_new_custom(hdl, &isc_gr_gain_ctrl, NULL);
	isc->gb_gain_ctrl = v4l2_ctrl_new_custom(hdl, &isc_gb_gain_ctrl, NULL);
	isc->r_off_ctrl = v4l2_ctrl_new_custom(hdl, &isc_r_off_ctrl, NULL);
	isc->b_off_ctrl = v4l2_ctrl_new_custom(hdl, &isc_b_off_ctrl, NULL);
	isc->gr_off_ctrl = v4l2_ctrl_new_custom(hdl, &isc_gr_off_ctrl, NULL);
	isc->gb_off_ctrl = v4l2_ctrl_new_custom(hdl, &isc_gb_off_ctrl, NULL);

	/*
	 * The cluster is in auto mode with autowhitebalance enabled
	 * and manual mode otherwise.
	 */
	v4l2_ctrl_auto_cluster(10, &isc->awb_ctrl, 0, true);

	v4l2_ctrl_handler_setup(hdl);

	return 0;
}

static int isc_async_bound(struct v4l2_async_notifier *notifier,
			   struct v4l2_subdev *subdev,
			   struct v4l2_async_subdev *asd)
{
	struct isc_device *isc = container_of(notifier->v4l2_dev,
					      struct isc_device, v4l2_dev);
	struct isc_subdev_entity *subdev_entity =
		container_of(notifier, struct isc_subdev_entity, notifier);
	int pad;

	if (video_is_registered(&isc->video_dev)) {
		dev_err(isc->dev, "only supports one sub-device.\n");
		return -EBUSY;
	}

	subdev_entity->sd = subdev;

	pad = media_entity_get_fwnode_pad(&subdev->entity, asd->match.fwnode,
					  MEDIA_PAD_FL_SOURCE);
	if (pad < 0) {
		dev_err(isc->dev, "failed to find pad for %s\n", subdev->name);
		return pad;
	}

	isc->remote_pad = pad;

	return 0;
}

static void isc_async_unbind(struct v4l2_async_notifier *notifier,
			     struct v4l2_subdev *subdev,
			     struct v4l2_async_subdev *asd)
{
	struct isc_device *isc = container_of(notifier->v4l2_dev,
					      struct isc_device, v4l2_dev);
	mutex_destroy(&isc->awb_mutex);
	cancel_work_sync(&isc->awb_work);
	video_unregister_device(&isc->video_dev);
	v4l2_ctrl_handler_free(&isc->ctrls.handler);
}

struct isc_format *isc_find_format_by_code(struct isc_device *isc,
					   unsigned int code, int *index)
{
	struct isc_format *fmt = &isc->formats_list[0];
	unsigned int i;

	for (i = 0; i < isc->formats_list_size; i++) {
		if (fmt->mbus_code == code) {
			*index = i;
			return fmt;
		}

		fmt++;
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(isc_find_format_by_code);

static int isc_set_default_fmt(struct isc_device *isc)
{
	struct v4l2_format f = {
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
		.fmt.pix = {
			.width		= VGA_WIDTH,
			.height		= VGA_HEIGHT,
			.field		= V4L2_FIELD_NONE,
			.pixelformat	= isc->controller_formats[0].fourcc,
		},
	};
	int ret;

	ret = isc_try_fmt(isc, &f);
	if (ret)
		return ret;

	isc->fmt = f;
	return 0;
}

static int isc_async_complete(struct v4l2_async_notifier *notifier)
{
	struct isc_device *isc = container_of(notifier->v4l2_dev,
					      struct isc_device, v4l2_dev);
	struct video_device *vdev = &isc->video_dev;
	struct vb2_queue *q = &isc->vb2_vidq;
	int ret = 0;

	INIT_WORK(&isc->awb_work, isc_awb_work);

	ret = v4l2_device_register_subdev_nodes(&isc->v4l2_dev);
	if (ret < 0) {
		dev_err(isc->dev, "Failed to register subdev nodes\n");
		return ret;
	}

	isc->current_subdev = container_of(notifier,
					   struct isc_subdev_entity, notifier);
	mutex_init(&isc->lock);
	mutex_init(&isc->awb_mutex);

	init_completion(&isc->comp);

	/* Initialize videobuf2 queue */
	q->type			= V4L2_BUF_TYPE_VIDEO_CAPTURE;
	q->io_modes		= VB2_MMAP | VB2_DMABUF | VB2_READ;
	q->drv_priv		= isc;
	q->buf_struct_size	= sizeof(struct isc_buffer);
	q->ops			= &isc_vb2_ops;
	q->mem_ops		= &vb2_dma_contig_memops;
	q->timestamp_flags	= V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->lock			= &isc->lock;
	q->min_buffers_needed	= 1;
	q->dev			= isc->dev;

	ret = vb2_queue_init(q);
	if (ret < 0) {
		dev_err(isc->dev, "vb2_queue_init() failed: %d\n", ret);
		goto isc_async_complete_err;
	}

	/* Init video dma queues */
	INIT_LIST_HEAD(&isc->dma_queue);
	spin_lock_init(&isc->dma_queue_lock);
	spin_lock_init(&isc->awb_lock);

	ret = isc_set_default_fmt(isc);
	if (ret) {
		dev_err(isc->dev, "Could not set default format\n");
		goto isc_async_complete_err;
	}

	ret = isc_ctrl_init(isc);
	if (ret) {
		dev_err(isc->dev, "Init isc ctrols failed: %d\n", ret);
		goto isc_async_complete_err;
	}

	/* Register video device */
	strscpy(vdev->name, KBUILD_MODNAME, sizeof(vdev->name));
	vdev->release		= video_device_release_empty;
	vdev->fops		= &isc_fops;
	vdev->ioctl_ops		= &isc_ioctl_ops;
	vdev->v4l2_dev		= &isc->v4l2_dev;
	vdev->vfl_dir		= VFL_DIR_RX;
	vdev->queue		= q;
	vdev->lock		= &isc->lock;
	vdev->ctrl_handler	= &isc->ctrls.handler;
	vdev->device_caps	= V4L2_CAP_STREAMING | V4L2_CAP_VIDEO_CAPTURE |
				  V4L2_CAP_IO_MC;
	video_set_drvdata(vdev, isc);

	ret = video_register_device(vdev, VFL_TYPE_VIDEO, -1);
	if (ret < 0) {
		dev_err(isc->dev, "video_register_device failed: %d\n", ret);
		goto isc_async_complete_err;
	}

	ret = isc_scaler_link(isc);
	if (ret < 0)
		goto isc_async_complete_unregister_device;

	ret = media_device_register(&isc->mdev);
	if (ret < 0)
		goto isc_async_complete_unregister_device;

	return 0;

isc_async_complete_unregister_device:
	video_unregister_device(vdev);

isc_async_complete_err:
	mutex_destroy(&isc->awb_mutex);
	mutex_destroy(&isc->lock);
	return ret;
}

const struct v4l2_async_notifier_operations microchip_isc_async_ops = {
	.bound = isc_async_bound,
	.unbind = isc_async_unbind,
	.complete = isc_async_complete,
};
EXPORT_SYMBOL_GPL(microchip_isc_async_ops);

void microchip_isc_subdev_cleanup(struct isc_device *isc)
{
	struct isc_subdev_entity *subdev_entity;

	list_for_each_entry(subdev_entity, &isc->subdev_entities, list) {
		v4l2_async_nf_unregister(&subdev_entity->notifier);
		v4l2_async_nf_cleanup(&subdev_entity->notifier);
	}

	INIT_LIST_HEAD(&isc->subdev_entities);
}
EXPORT_SYMBOL_GPL(microchip_isc_subdev_cleanup);

int microchip_isc_pipeline_init(struct isc_device *isc)
{
	struct device *dev = isc->dev;
	struct regmap *regmap = isc->regmap;
	struct regmap_field *regs;
	unsigned int i;

	/*
	 * DPCEN-->GDCEN-->BLCEN-->WB-->CFA-->CC-->
	 * GAM-->VHXS-->CSC-->CBC-->SUB422-->SUB420
	 */
	const struct reg_field regfields[ISC_PIPE_LINE_NODE_NUM] = {
		REG_FIELD(ISC_DPC_CTRL, 0, 0),
		REG_FIELD(ISC_DPC_CTRL, 1, 1),
		REG_FIELD(ISC_DPC_CTRL, 2, 2),
		REG_FIELD(ISC_WB_CTRL, 0, 0),
		REG_FIELD(ISC_CFA_CTRL, 0, 0),
		REG_FIELD(ISC_CC_CTRL, 0, 0),
		REG_FIELD(ISC_GAM_CTRL, 0, 0),
		REG_FIELD(ISC_GAM_CTRL, 1, 1),
		REG_FIELD(ISC_GAM_CTRL, 2, 2),
		REG_FIELD(ISC_GAM_CTRL, 3, 3),
		REG_FIELD(ISC_VHXS_CTRL, 0, 0),
		REG_FIELD(ISC_CSC_CTRL + isc->offsets.csc, 0, 0),
		REG_FIELD(ISC_CBC_CTRL + isc->offsets.cbc, 0, 0),
		REG_FIELD(ISC_SUB422_CTRL + isc->offsets.sub422, 0, 0),
		REG_FIELD(ISC_SUB420_CTRL + isc->offsets.sub420, 0, 0),
	};

	for (i = 0; i < ISC_PIPE_LINE_NODE_NUM; i++) {
		regs = devm_regmap_field_alloc(dev, regmap, regfields[i]);
		if (IS_ERR(regs))
			return PTR_ERR(regs);

		isc->pipeline[i] =  regs;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(microchip_isc_pipeline_init);

static int isc_link_validate(struct media_link *link)
{
	struct video_device *vdev =
		media_entity_to_video_device(link->sink->entity);
	struct isc_device *isc = video_get_drvdata(vdev);
	int ret;

	ret = v4l2_subdev_link_validate(link);
	if (ret)
		return ret;

	return isc_validate(isc);
}

static const struct media_entity_operations isc_entity_operations = {
	.link_validate = isc_link_validate,
};

int isc_mc_init(struct isc_device *isc, u32 ver)
{
	const struct of_device_id *match;
	int ret;

	isc->video_dev.entity.function = MEDIA_ENT_F_IO_V4L;
	isc->video_dev.entity.flags = MEDIA_ENT_FL_DEFAULT;
	isc->video_dev.entity.ops = &isc_entity_operations;

	isc->pads[ISC_PAD_SINK].flags = MEDIA_PAD_FL_SINK;

	ret = media_entity_pads_init(&isc->video_dev.entity, ISC_PADS_NUM,
				     isc->pads);
	if (ret < 0) {
		dev_err(isc->dev, "media entity init failed\n");
		return ret;
	}

	isc->mdev.dev = isc->dev;

	match = of_match_node(isc->dev->driver->of_match_table,
			      isc->dev->of_node);

	strscpy(isc->mdev.driver_name, KBUILD_MODNAME,
		sizeof(isc->mdev.driver_name));
	strscpy(isc->mdev.model, match->compatible, sizeof(isc->mdev.model));
	snprintf(isc->mdev.bus_info, sizeof(isc->mdev.bus_info), "platform:%s",
		 isc->v4l2_dev.name);
	isc->mdev.hw_revision = ver;

	media_device_init(&isc->mdev);

	isc->v4l2_dev.mdev = &isc->mdev;

	return isc_scaler_init(isc);
}
EXPORT_SYMBOL_GPL(isc_mc_init);

void isc_mc_cleanup(struct isc_device *isc)
{
	media_entity_cleanup(&isc->video_dev.entity);
	media_device_cleanup(&isc->mdev);
}
EXPORT_SYMBOL_GPL(isc_mc_cleanup);

/* regmap configuration */
#define MICROCHIP_ISC_REG_MAX    0xd5c
const struct regmap_config microchip_isc_regmap_config = {
	.reg_bits       = 32,
	.reg_stride     = 4,
	.val_bits       = 32,
	.max_register	= MICROCHIP_ISC_REG_MAX,
};
EXPORT_SYMBOL_GPL(microchip_isc_regmap_config);

MODULE_AUTHOR("Songjun Wu");
MODULE_AUTHOR("Eugen Hristev");
MODULE_DESCRIPTION("Microchip ISC common code base");
MODULE_LICENSE("GPL v2");
